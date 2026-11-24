// Headless test ROM runner.
//
// Runs NES test ROMs on NesCore without a window or audio and classifies the
// result of each one:
//
//   pass / fail:XX  ROMs that follow the blargg protocol: the status byte at
//                   $6000 (0 is a pass) is valid once $6001-$6003 hold DE B0 61.
//                   Status $81 asks for a reset, which the runner performs.
//   screen:<hash>   ROMs that show their result on screen. The runner runs a
//                   fixed number of frames and reports a hash of the picture,
//                   which is compared against a baseline.
//   timeout         The protocol was detected but the test never finished.
//   loaderror       The core refused the image.
//   crash, wall-timeout
//                   The child process that runs the ROM died or hung.
//
// Every ROM runs in its own child process so that a crash or a hang in the core
// only affects that ROM, and so that ROMs run in parallel. The core keeps its
// callbacks in process wide statics, which rules out threads.
//
// Exit code 0 on success, 1 if the results differ from --baseline, 2 on a usage
// error.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QXmlStreamReader>

#include "core/NesCore.h"

#ifndef NST_RUNNER_DATABASE
#define NST_RUNNER_DATABASE ""
#endif

namespace
{
	constexpr int kDefaultScreenFrames = 600;
	constexpr int kDefaultMaxFrames = 7200;
	constexpr int kDefaultTimeoutSeconds = 120;
	// Frames between two resets requested by a test
	constexpr int kResetCooldownFrames = 120;
	// Frames between the reset request and the reset, 10 frames are about 166 ms
	constexpr int kResetDelayFrames = 10;
	// Frames to run after the final status so that the message is complete
	constexpr int kSettleFrames = 3;
	constexpr unsigned int kStatusRunning = 0x80;
	constexpr unsigned int kStatusNeedsReset = 0x81;

	struct Entry
	{
		QString name;
		QString path;
		int frames = 0;
		// "pass" or "fail" as recorded in the manifest, empty if unknown
		QString reference;
	};

	struct Result
	{
		// pass, fail:XX, screen, timeout, loaderror, crash, wall-timeout, missing
		QString verdict;
		int frames = 0;
		QString hash;
		QString message;

		// Value that is compared against the baseline
		QString key(void) const
		{
			return verdict == QLatin1String("screen") ? QStringLiteral("screen:") + hash : verdict;
		}

		bool isProtocolVerdict(void) const
		{
			return verdict == QLatin1String("pass") || verdict.startsWith(QLatin1String("fail:"));
		}
	};

	void quietMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
	{
		// The core reports every image it loads through the log callback
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
		{
			std::fprintf(stderr, "%s\n", qPrintable(message));
		}
	}

	QString frameHash(const NesCore &core)
	{
		const uint32_t *pixels = core.frameBuffer();
		const size_t count = size_t(core.frameWidth()) * size_t(core.frameHeight());
		uint64_t hash = 1469598103934665603ull;

		for (size_t i = 0; i < count; ++i)
		{
			hash = (hash ^ (pixels[i] & 0xFFFFFF)) * 1099511628211ull;
		}

		return QStringLiteral("%1").arg(hash, 16, 16, QLatin1Char('0'));
	}

	QString readMessage(NesCore &core)
	{
		QString text;

		for (unsigned int address = 0x6004; address < 0x6004 + 400; ++address)
		{
			const unsigned int value = core.peekCpu(address);

			if (value == 0)
			{
				break;
			}

			if (value == '\n')
			{
				text += QStringLiteral(" | ");
			}
			else
			{
				text += (value >= 0x20 && value < 0x7F) ? QChar(char(value)) : QLatin1Char('?');
			}
		}

		return text.simplified();
	}

	bool hasSignature(NesCore &core)
	{
		return core.peekCpu(0x6001) == 0xDE && core.peekCpu(0x6002) == 0xB0 && core.peekCpu(0x6003) == 0x61;
	}

	void printChildResult(const QString &verdict, int frames, const QString &hash, const QString &message)
	{
		std::printf("RESULT\t%s\t%d\t%s\t%s\n", qPrintable(verdict), frames, qPrintable(hash), qPrintable(message));
		std::fflush(stdout);
	}

	// Blargg style tests report through 8K of RAM at $6000. The iNES header of
	// most of them declares no PRG RAM, so the core would leave $6000 unmapped.
	// A copy of the image in memory is patched to declare it, the file is not touched.
	void declareWorkRam(QByteArray &image)
	{
		const bool ines = image.size() >= 16 && image.startsWith("NES\x1A");
		const bool nes2 = ines && (uchar(image[7]) & 0x0C) == 0x08;

		if (ines && !nes2 && image[8] == 0 && (uchar(image[6]) & 0x02) == 0)
		{
			image[8] = 1;
		}
	}

	// Runs a single ROM and prints one RESULT line
	int runChild(const QString &rom, const QString &database, int screenFrames, int maxFrames, bool keepHeader)
	{
		NesCore core;
		QString error;

		if (!database.isEmpty())
		{
			core.loadDatabase(database);
		}

		QFile file(rom);

		if (!file.open(QIODevice::ReadOnly))
		{
			printChildResult(QStringLiteral("loaderror"), 0, QString(), file.errorString());
			return 0;
		}

		QByteArray image = file.readAll();

		file.close();

		if (!keepHeader)
		{
			declareWorkRam(image);
		}

		if (!core.loadRomData(image, QFileInfo(rom).completeBaseName(), error))
		{
			printChildResult(QStringLiteral("loaderror"), 0, QString(), error);
			return 0;
		}

		bool signature = false;
		int nextReset = 0;
		int resetAt = -1;

		for (int frame = 1; frame <= maxFrames; ++frame)
		{
			core.runFrame();

			if (!signature)
			{
				signature = hasSignature(core);
			}

			if (signature)
			{
				const unsigned int status = core.peekCpu(0x6000);

				if (status == kStatusNeedsReset)
				{
					// The reset button is pressed at least 100 ms after the request
					if (resetAt < 0 && frame >= nextReset)
					{
						resetAt = frame + kResetDelayFrames;
					}

					if (resetAt >= 0 && frame >= resetAt)
					{
						core.reset(false);
						resetAt = -1;
						nextReset = frame + kResetCooldownFrames;
					}
				}
				else
				{
					resetAt = -1;
				}

				if (status < kStatusRunning)
				{
					for (int i = 0; i < kSettleFrames; ++i)
					{
						core.runFrame();
					}

					const QString verdict = status == 0 ? QStringLiteral("pass")
						: QStringLiteral("fail:%1").arg(QString::number(status, 16).rightJustified(2, QLatin1Char('0')).toUpper());

					printChildResult(verdict, frame, frameHash(core), readMessage(core));
					return 0;
				}
			}
			else if (frame >= screenFrames)
			{
				printChildResult(QStringLiteral("screen"), frame, frameHash(core), QString());
				return 0;
			}
		}

		printChildResult(QStringLiteral("timeout"), maxFrames, frameHash(core), readMessage(core));
		return 0;
	}

	// ----------------------------------------------------------------------
	// Driver
	// ----------------------------------------------------------------------

	struct Settings
	{
		QString program;
		QString database;
		int screenFrames = kDefaultScreenFrames;
		int maxFrames = kDefaultMaxFrames;
		int timeoutSeconds = kDefaultTimeoutSeconds;
		int jobs = 1;
		bool keepHeader = false;
	};

	// Runs the entries on child processes, up to jobs at a time
	class Scheduler : public QObject
	{
		public:
			Scheduler(const std::vector<Entry> &list, const Settings &options)
				: entries(list), settings(options), results(list.size()), loop(nullptr), next(0), running(0), finished(0)
			{
			}

			std::vector<Result> run(void)
			{
				QEventLoop eventLoop;

				loop = &eventLoop;
				pump();

				if (finished < entries.size())
				{
					eventLoop.exec();
				}

				loop = nullptr;
				return results;
			}

		private:
			void pump(void)
			{
				while (running < settings.jobs && next < entries.size())
				{
					start(next++);
				}

				if (finished == entries.size() && loop != nullptr)
				{
					loop->quit();
				}
			}

			void start(size_t index)
			{
				const Entry &entry = entries[index];

				if (!QFileInfo::exists(entry.path))
				{
					results[index].verdict = QStringLiteral("missing");
					++finished;
					return;
				}

				QProcess *process = new QProcess(this);
				QTimer *timer = new QTimer(process);
				const int frames = entry.frames > 0 ? entry.frames : settings.screenFrames;

				process->setStandardErrorFile(QProcess::nullDevice());
				timer->setSingleShot(true);
				connect(timer, &QTimer::timeout, process, [process]() {
					process->setProperty("wallTimeout", true);
					process->kill();
				});

				connect(process, &QProcess::finished, this, [this, process, index](int, QProcess::ExitStatus) {
					results[index] = parse(process);
					process->deleteLater();
					--running;
					++finished;
					std::fprintf(stderr, "\r[%zu/%zu]", finished, entries.size());
					pump();
				});

				QStringList arguments = {
					QStringLiteral("--child"), entry.path,
					QStringLiteral("--database"), settings.database,
					QStringLiteral("--screen-frames"), QString::number(frames),
					QStringLiteral("--max-frames"), QString::number(settings.maxFrames)
				};

				if (settings.keepHeader)
				{
					arguments << QStringLiteral("--keep-header");
				}

				++running;
				timer->start(settings.timeoutSeconds * 1000);
				process->start(settings.program, arguments);
			}

			static Result parse(QProcess *process)
			{
				Result result;

				if (process->property("wallTimeout").toBool())
				{
					result.verdict = QStringLiteral("wall-timeout");
					return result;
				}

				const QList<QByteArray> lines = process->readAllStandardOutput().split('\n');

				for (const QByteArray &line : lines)
				{
					const QList<QByteArray> fields = line.split('\t');

					if (fields.size() >= 5 && fields[0] == "RESULT")
					{
						result.verdict = QString::fromUtf8(fields[1]);
						result.frames = fields[2].toInt();
						result.hash = QString::fromUtf8(fields[3]);
						result.message = QString::fromUtf8(fields[4]);
						return result;
					}
				}

				result.verdict = QStringLiteral("crash");
				return result;
			}

			const std::vector<Entry> &entries;
			Settings settings;
			std::vector<Result> results;
			QEventLoop *loop;
			size_t next;
			int running;
			size_t finished;
	};

	// Reads the <test> elements of a test_roms.xml style manifest
	bool readManifest(const QString &path, const QString &root, std::vector<Entry> &entries)
	{
		QFile file(path);

		if (!file.open(QIODevice::ReadOnly))
		{
			return false;
		}

		QXmlStreamReader xml(&file);

		while (!xml.atEnd())
		{
			xml.readNext();

			if (xml.isStartElement() && xml.name() == QLatin1String("test"))
			{
				const QXmlStreamAttributes attributes = xml.attributes();
				Entry entry;

				entry.name = attributes.value(QLatin1String("filename")).toString();
				entry.path = QDir(root).filePath(entry.name);
				entry.frames = attributes.value(QLatin1String("runframes")).toInt();
				entry.reference = attributes.value(QLatin1String("testresult")).toString();

				if (!entry.name.isEmpty())
				{
					entries.push_back(entry);
				}
			}
		}

		return !xml.hasError();
	}

	void addPath(const QString &argument, const QString &root, std::vector<Entry> &entries)
	{
		const QFileInfo info(argument);
		QStringList files;

		if (info.isDir())
		{
			QDirIterator iterator(info.absoluteFilePath(), {QStringLiteral("*.nes")}, QDir::Files, QDirIterator::Subdirectories);

			while (iterator.hasNext())
			{
				files << iterator.next();
			}

			files.sort();
		}
		else
		{
			files << info.absoluteFilePath();
		}

		for (const QString &file : files)
		{
			Entry entry;

			entry.path = file;
			entry.name = QDir(root).relativeFilePath(file);
			entries.push_back(entry);
		}
	}

	QMap<QString, QString> readBaseline(const QString &path, bool &ok)
	{
		QMap<QString, QString> map;
		QFile file(path);

		ok = file.open(QIODevice::ReadOnly);

		while (ok && !file.atEnd())
		{
			const QString line = QString::fromUtf8(file.readLine()).trimmed();
			const int tab = line.indexOf(QLatin1Char('\t'));

			if (tab > 0)
			{
				map.insert(line.left(tab), line.mid(tab + 1));
			}
		}

		return map;
	}

	QString shorten(const QString &text, int length)
	{
		return text.size() <= length ? text : text.left(length - 3) + QStringLiteral("...");
	}
}

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);

	qInstallMessageHandler(quietMessageHandler);

	QCommandLineParser parser;

	parser.setApplicationDescription(QStringLiteral("Runs NES test ROMs headless and reports the result of each"));
	parser.addHelpOption();
	parser.addPositionalArgument(QStringLiteral("rom"), QStringLiteral("ROM files or directories that are searched for .nes files"), QStringLiteral("[rom...]"));

	QCommandLineOption manifest({QStringLiteral("m"), QStringLiteral("manifest")}, QStringLiteral("test_roms.xml style manifest, supplies the frame count and the reference result"), QStringLiteral("file"));
	QCommandLineOption romRoot(QStringLiteral("rom-root"), QStringLiteral("Directory the manifest names are relative to, default is the directory of the manifest"), QStringLiteral("dir"));
	QCommandLineOption database(QStringLiteral("database"), QStringLiteral("NstDatabase.xml"), QStringLiteral("file"), QStringLiteral(NST_RUNNER_DATABASE));
	QCommandLineOption screenFrames(QStringLiteral("screen-frames"), QStringLiteral("Frames to run ROMs that do not report through $6000"), QStringLiteral("n"), QString::number(kDefaultScreenFrames));
	QCommandLineOption maxFrames(QStringLiteral("max-frames"), QStringLiteral("Frame limit of ROMs that report through $6000"), QStringLiteral("n"), QString::number(kDefaultMaxFrames));
	QCommandLineOption timeout(QStringLiteral("timeout"), QStringLiteral("Seconds before a ROM is killed"), QStringLiteral("s"), QString::number(kDefaultTimeoutSeconds));
	QCommandLineOption jobs({QStringLiteral("j"), QStringLiteral("jobs")}, QStringLiteral("ROMs run in parallel"), QStringLiteral("n"), QString::number(std::max(1, QThread::idealThreadCount())));
	QCommandLineOption filter(QStringLiteral("filter"), QStringLiteral("Only run ROMs whose name contains the text"), QStringLiteral("text"));
	QCommandLineOption tsv(QStringLiteral("tsv"), QStringLiteral("Write all results to a tab separated file"), QStringLiteral("file"));
	QCommandLineOption baseline(QStringLiteral("baseline"), QStringLiteral("Compare the results with a baseline file, exit code 1 on a difference"), QStringLiteral("file"));
	QCommandLineOption writeBaseline(QStringLiteral("write-baseline"), QStringLiteral("Write the results as a baseline file"), QStringLiteral("file"));
	QCommandLineOption keepHeader(QStringLiteral("keep-header"), QStringLiteral("Run the images as they are, without declaring 8K of PRG RAM for headers that have none"));
	QCommandLineOption child(QStringLiteral("child"), QStringLiteral("Internal: run one ROM and print its result"), QStringLiteral("rom"));

	parser.addOptions({manifest, romRoot, database, screenFrames, maxFrames, timeout, jobs, filter, tsv, baseline, writeBaseline, keepHeader, child});
	parser.process(app);

	if (parser.isSet(child))
	{
		return runChild(parser.value(child), parser.value(database), parser.value(screenFrames).toInt(), parser.value(maxFrames).toInt(), parser.isSet(keepHeader));
	}

	QString root = parser.value(romRoot);

	if (root.isEmpty())
	{
		root = parser.isSet(manifest) ? QFileInfo(parser.value(manifest)).absolutePath() : QDir::currentPath();
	}

	std::vector<Entry> entries;

	if (parser.isSet(manifest) && !readManifest(parser.value(manifest), root, entries))
	{
		std::fprintf(stderr, "Unable to read the manifest %s\n", qPrintable(parser.value(manifest)));
		return 2;
	}

	for (const QString &argument : parser.positionalArguments())
	{
		addPath(argument, root, entries);
	}

	if (parser.isSet(filter))
	{
		const QString text = parser.value(filter);

		entries.erase(std::remove_if(entries.begin(), entries.end(), [&text](const Entry &entry) {
			return !entry.name.contains(text);
		}), entries.end());
	}

	if (entries.empty())
	{
		std::fprintf(stderr, "No ROMs to run, give ROM paths or --manifest\n");
		return 2;
	}

	Settings settings;

	settings.program = QCoreApplication::applicationFilePath();
	settings.database = parser.value(database);
	settings.screenFrames = parser.value(screenFrames).toInt();
	settings.maxFrames = parser.value(maxFrames).toInt();
	settings.timeoutSeconds = parser.value(timeout).toInt();
	settings.jobs = std::max(1, parser.value(jobs).toInt());
	settings.keepHeader = parser.isSet(keepHeader);

	Scheduler scheduler(entries, settings);
	const std::vector<Result> results = scheduler.run();

	std::fprintf(stderr, "\r          \r");

	// ---- Table
	int nameWidth = 4;

	for (const Entry &entry : entries)
	{
		nameWidth = std::max(nameWidth, int(entry.name.size()));
	}

	QTextStream out(stdout);
	QMap<QString, int> counts;
	QStringList disagreements;

	out << QStringLiteral("%1  %2  %3  %4  %5\n")
		.arg(QStringLiteral("ROM"), -nameWidth).arg(QStringLiteral("RESULT"), -12)
		.arg(QStringLiteral("FRAMES"), 6).arg(QStringLiteral("REF"), -4).arg(QStringLiteral("MESSAGE"));

	for (size_t i = 0; i < entries.size(); ++i)
	{
		const Entry &entry = entries[i];
		const Result &result = results[i];
		const QString category = result.isProtocolVerdict() ? result.verdict.section(QLatin1Char(':'), 0, 0) : result.verdict;
		QString reference = entry.reference;

		++counts[category];

		// A ROM that reports through $6000 gives a hard verdict that can be held against the manifest
		if (!entry.reference.isEmpty() && result.isProtocolVerdict() && entry.reference != category)
		{
			reference += QLatin1Char('!');
			disagreements << entry.name;
		}

		out << QStringLiteral("%1  %2  %3  %4  %5\n")
			.arg(entry.name, -nameWidth).arg(result.verdict, -12).arg(result.frames, 6)
			.arg(reference, -4).arg(shorten(result.message, 70));
	}

	out << QStringLiteral("\n%1 ROMs").arg(entries.size());

	for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
	{
		out << QStringLiteral(", %1 %2").arg(it.value()).arg(it.key());
	}

	out << QStringLiteral("\n");

	if (!disagreements.isEmpty())
	{
		out << QStringLiteral("%1 verdicts differ from the manifest (marked with !): %2\n")
			.arg(disagreements.size()).arg(disagreements.join(QStringLiteral(", ")));
	}

	// ---- Files
	if (parser.isSet(tsv))
	{
		QFile file(parser.value(tsv));

		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
			QTextStream stream(&file);

			for (size_t i = 0; i < entries.size(); ++i)
			{
				stream << entries[i].name << '\t' << results[i].key() << '\t' << results[i].frames << '\t'
					<< entries[i].reference << '\t' << results[i].hash << '\t' << results[i].message << '\n';
			}
		}
	}

	if (parser.isSet(writeBaseline))
	{
		QFile file(parser.value(writeBaseline));

		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
			QTextStream stream(&file);

			for (size_t i = 0; i < entries.size(); ++i)
			{
				stream << entries[i].name << '\t' << results[i].key() << '\n';
			}
		}
	}

	if (parser.isSet(baseline))
	{
		bool ok = false;
		const QMap<QString, QString> expected = readBaseline(parser.value(baseline), ok);

		if (!ok)
		{
			std::fprintf(stderr, "Unable to read the baseline %s\n", qPrintable(parser.value(baseline)));
			return 2;
		}

		int differences = 0;

		for (size_t i = 0; i < entries.size(); ++i)
		{
			const auto found = expected.constFind(entries[i].name);

			if (found == expected.constEnd())
			{
				out << QStringLiteral("NEW      %1: %2\n").arg(entries[i].name, results[i].key());
				++differences;
			}
			else if (found.value() != results[i].key())
			{
				out << QStringLiteral("CHANGED  %1: %2 -> %3\n").arg(entries[i].name, found.value(), results[i].key());
				++differences;
			}
		}

		out << (differences == 0 ? QStringLiteral("Baseline matches\n")
			: QStringLiteral("%1 differences from the baseline\n").arg(differences));
		out.flush();
		return differences == 0 ? 0 : 1;
	}

	out.flush();
	return 0;
}
