// Tests the profiler: capture of markers from several threads into a file, reading the
// file back, the timeline widget logic, the markers placed in the emulation thread and the
// capture controls of the main window. Needs no ROM, a program is generated. Exit code 77
// skips the test when the profiler is not compiled in.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>

#include <QApplication>
#include <QAction>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include "core/EmulationThread.h"
#include "profiler/Profiler.h"
#include "profiler/ProfilerCaptureFile.h"
#include "profiler/ProfilerDialog.h"
#include "profiler/ProfilerViewer.h"
#include "ui/MainWindow.h"

#ifdef NESTOPIAQT_PROFILER_ENABLE
#include "profiler/ProfilerCapture.h"
#endif

namespace
{
	int failures = 0;

	void check(bool condition, const char *description)
	{
		if (!condition)
		{
			std::fprintf(stderr, "FAIL: %s\n", description);
			++failures;
		}
	}

	bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 5000)
	{
		QElapsedTimer timer;
		timer.start();

		while (!condition() && timer.elapsed() < timeoutMs)
		{
			QApplication::processEvents();
			QThread::msleep(2);
		}

		return condition();
	}

	// NROM program that loops forever, enough to run frames
	QByteArray program(void)
	{
		QByteArray rom(16 + 0x8000 + 0x2000, '\0');
		const unsigned char header[] = {'N', 'E', 'S', 0x1A, 2, 1};
		const unsigned char loop[] = {0x4C, 0x00, 0x80};

		std::copy(std::begin(header), std::end(header), rom.data());

		char *prg = rom.data() + 16;

		std::copy(std::begin(loop), std::end(loop), prg);

		for (int vector : {0x7FFA, 0x7FFC, 0x7FFE})
		{
			prg[vector] = 0;
			prg[vector + 1] = char(0x80);
		}

		return rom;
	}

	const ProfilerThreadData *findThread(const ProfilerCaptureData &data, const QString &name)
	{
		for (const ProfilerThreadData &t : data.threads)
		{
			if (t.name == name)
			{
				return &t;
			}
		}

		return nullptr;
	}

	int findSite(const ProfilerCaptureData &data, const QString &comment)
	{
		for (size_t i = 0; i < data.sites.size(); i++)
		{
			if (data.sites[i].comment == comment)
			{
				return static_cast<int>(i);
			}
		}

		return -1;
	}

	size_t countBlocks(const ProfilerThreadData &thread, size_t depth, const ProfilerCaptureData &data,
		const QString &comment)
	{
		size_t count = 0;

		if (depth < thread.depths.size())
		{
			for (const ProfilerBlock &b : thread.depths[depth])
			{
				count += (data.sites[b.site].comment == comment) ? 1 : 0;
			}
		}

		return count;
	}

#ifdef NESTOPIAQT_PROFILER_ENABLE
	void innerScope(void)
	{
		NESTOPIAQT_PROFILE_FUNC("Inner");
	}

	void outerScope(void)
	{
		NESTOPIAQT_PROFILE_FUNC("Outer");
		innerScope();
	}

	void testCaptureRoundTrip(const QString &path, ProfilerCaptureData &data)
	{
		constexpr int workerScopes = 200;
		constexpr int mainScopes = 50;

		check(!Profiler::Manager::instance().isEnabled(), "profiler is off before a capture");

		// Not captured, the profiler is off
		for (int i = 0; i < 10; i++)
		{
			outerScope();
		}

		ProfilerCaptureThread capture;

		capture.setFilePath(path);
		capture.start();
		check(waitUntil([]() { return Profiler::Manager::instance().isEnabled(); }), "capture turns the profiler on");

		NESTOPIAQT_PROFILER_SET_THREAD_NAME("Main");

		// This thread is gone before the capture ends, its markers must still arrive
		std::thread worker([]()
		{
			NESTOPIAQT_PROFILER_SET_THREAD_NAME("Worker");

			for (int i = 0; i < workerScopes; i++)
			{
				outerScope();
			}
		});

		for (int i = 0; i < mainScopes; i++)
		{
			outerScope();
		}

		worker.join();
		capture.stopCapture();
		check(capture.wait(5000), "capture thread finishes");
		check(!Profiler::Manager::instance().isEnabled(), "capture turns the profiler off");

		const uint64_t expected = uint64_t(workerScopes + mainScopes) * 4;

		check(capture.markerCount() == expected, "capture counts all markers and none from before it");

		QString error;

		check(profilerLoadCapture(path, data, error), "capture file loads");
		check(data.markerCount == expected, "file holds all markers");
		check(data.hasFooter && data.footerCount == expected, "file has a matching footer");
		check(!data.truncated && data.unmatchedStarts == 0 && data.strayEnds == 0, "file is complete and balanced");
		check(data.countFreq == Profiler::timestampFrequency, "file has the timestamp frequency");

		const ProfilerThreadData *workerData = findThread(data, QStringLiteral("Worker"));
		const ProfilerThreadData *mainData = findThread(data, QStringLiteral("Main"));

		check(workerData != nullptr && mainData != nullptr, "threads are named in the file");

		if (workerData != nullptr && mainData != nullptr)
		{
			check(workerData->depths.size() == 2, "nested scopes make two depths");
			check(countBlocks(*workerData, 0, data, QStringLiteral("Outer")) == workerScopes, "worker outer blocks");
			check(countBlocks(*workerData, 1, data, QStringLiteral("Inner")) == workerScopes, "worker inner blocks");
			check(countBlocks(*mainData, 0, data, QStringLiteral("Outer")) == mainScopes, "main outer blocks");

			bool ordered = true;

			for (const auto &row : workerData->depths)
			{
				for (size_t i = 0; i < row.size(); i++)
				{
					ordered = ordered && row[i].start <= row[i].end && (i == 0 || row[i - 1].end <= row[i].start);
				}
			}

			check(ordered, "blocks are ordered in time and do not overlap");

			bool nested = true;

			for (const ProfilerBlock &inner : workerData->depths[1])
			{
				bool inside = false;

				for (const ProfilerBlock &outer : workerData->depths[0])
				{
					inside = inside || (outer.start <= inner.start && inner.end <= outer.end);
				}

				nested = nested && inside;
			}

			check(nested, "inner blocks lie inside outer blocks");
		}

		const int outer = findSite(data, QStringLiteral("Outer"));

		check(outer >= 0 && data.sites[outer].func.contains(QStringLiteral("outerScope")), "site keeps the function name");
		check(outer >= 0 && data.sites[outer].file.endsWith(QStringLiteral("profiler_test.cpp")), "site keeps the file name");
	}

	void testBadFiles(const QString &path, const QString &dir)
	{
		QFile file(path);

		check(file.open(QIODevice::ReadOnly), "capture file can be reopened");

		const QByteArray bytes = file.readAll();

		file.close();

		ProfilerCaptureData data;
		QString error;
		const QString cut = dir + QStringLiteral("/truncated.nstprof");

		QFile out(cut);

		check(out.open(QIODevice::WriteOnly), "truncated file can be written");
		out.write(bytes.left(bytes.size() - 7));
		out.close();

		check(profilerLoadCapture(cut, data, error), "truncated file still loads");
		check(data.truncated && !data.hasFooter, "truncated file is reported as such");
		check(data.markerCount > 0, "truncated file keeps the complete chunks");

		const QString bad = dir + QStringLiteral("/bad.nstprof");

		QFile junk(bad);

		check(junk.open(QIODevice::WriteOnly), "junk file can be written");
		junk.write(QByteArray(64, 'x'));
		junk.close();

		check(!profilerLoadCapture(bad, data, error), "file with a wrong magic is refused");
		check(!profilerLoadCapture(dir + QStringLiteral("/missing.nstprof"), data, error), "missing file is refused");
	}

	void testViewer(const std::shared_ptr<const ProfilerCaptureData> &data)
	{
		ProfilerViewer viewer;
		QStringList results;
		int selected = 0;

		QObject::connect(&viewer, &ProfilerViewer::findResult, &viewer, [&results](const QString &text) { results << text; });
		QObject::connect(&viewer, &ProfilerViewer::frameSelected, &viewer, [&selected](int) { ++selected; });

		viewer.resize(900, 400);
		viewer.show();
		viewer.setData(data);
		QApplication::processEvents();
		check(!viewer.grab().isNull(), "viewer paints a capture");

		const int outer = findSite(*data, QStringLiteral("Outer"));

		viewer.setFrameSite(outer);
		check(viewer.frameCount() == 250, "frames are the blocks of the chosen site across threads");
		check(!viewer.frameDescription(0).isEmpty(), "frame has a description");

		viewer.gotoFrame(10);
		QApplication::processEvents();
		check(!viewer.grab().isNull(), "viewer paints a zoomed frame");

		viewer.setFindText(QStringLiteral("inner"));
		check(viewer.findFirst(), "find matches case insensitively");
		check(!results.isEmpty() && results.last().startsWith(QStringLiteral("Inner")),
			"find reports the marker it selected");
		check(selected >= 1, "find selects the frame around the marker");
		check(viewer.findNext() && viewer.findPrevious() && viewer.findLast(), "find steps through markers");
		check(viewer.findLongest() && viewer.findNextLongest(), "find steps through markers by length");

		viewer.setFindText(QStringLiteral("no such marker"));
		check(!viewer.findFirst(), "find without a match fails");
		check(results.last().contains(QStringLiteral("No markers match")), "find reports a miss");

		viewer.zoomIn();
		viewer.zoomOut();
		viewer.zoomFit();
		check(!viewer.grab().isNull(), "viewer paints after zooming");
	}

	void testEmulatorMarkers(const QString &path)
	{
		EmulationThread thread;
		QString error;

		{
			EmulationThread::Locker lock(thread.mutex());

			check(thread.core().loadRomData(program(), QStringLiteral("profiler-test"), error), "program loads");
		}

		ProfilerCaptureThread capture;

		capture.setFilePath(path);
		thread.start();
		capture.start();
		QThread::msleep(500);
		capture.stopCapture();
		check(capture.wait(5000), "capture thread finishes");
		thread.stop();

		ProfilerCaptureData data;
		QString loadError;

		check(profilerLoadCapture(path, data, loadError), "emulator capture loads");

		const ProfilerThreadData *emulator = findThread(data, QStringLiteral("Emulator"));

		check(emulator != nullptr, "emulation thread is named");

		if (emulator != nullptr)
		{
			const size_t frames = countBlocks(*emulator, 0, data, QStringLiteral("Emulate Single Frame"));

			check(frames >= 5, "emulated frames are marked");
			check(countBlocks(*emulator, 1, data, QStringLiteral("Nestopia Execute")) == frames,
				"each frame has an execute marker inside");
		}
	}

	void testDialog(const QString &path)
	{
		MainWindow window;

		window.show();

		QAction *action = window.findChild<QAction *>(QStringLiteral("profilerAction"));

		check(action != nullptr, "profiler menu action exists");

		if (action == nullptr)
		{
			return;
		}

		action->trigger();
		QApplication::processEvents();

		ProfilerDialog *dialog = window.findChild<ProfilerDialog *>(QStringLiteral("profilerDialog"));

		check(dialog != nullptr && dialog->isVisible(), "profiler dialog opens");

		if (dialog == nullptr)
		{
			return;
		}

		action->trigger();
		check(window.findChildren<ProfilerDialog *>().size() == 1, "the dialog opens only once");

		auto *pathEdit = dialog->findChild<QLineEdit *>(QStringLiteral("profilerPath"));
		auto *startStop = dialog->findChild<QPushButton *>(QStringLiteral("profilerStartStop"));
		auto *status = dialog->findChild<QLabel *>(QStringLiteral("profilerStatus"));

		check(pathEdit != nullptr && startStop != nullptr && status != nullptr, "capture controls exist");

		if (pathEdit == nullptr || startStop == nullptr || status == nullptr)
		{
			return;
		}

		pathEdit->setText(path);
		startStop->click();
		check(!pathEdit->isEnabled() && startStop->text() == QStringLiteral("Stop Capture"), "capture starts");
		check(waitUntil([&]() { return status->text().startsWith(QStringLiteral("Capturing")); }), "status shows the capture");

		startStop->click();
		check(waitUntil([&]() { return status->text().startsWith(QStringLiteral("Capture saved")); }), "capture is saved");
		check(pathEdit->isEnabled() && startStop->text() == QStringLiteral("Start Capture"), "controls return to idle");

		ProfilerCaptureData data;
		QString error;

		check(profilerLoadCapture(path, data, error) && data.hasFooter, "dialog capture file is complete");

		// Closing with a capture running must finalize the file
		startStop->click();
		check(waitUntil([&]() { return status->text().startsWith(QStringLiteral("Capturing")); }), "second capture starts");
		dialog->close();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		check(window.findChild<ProfilerDialog *>() == nullptr, "dialog is gone after closing");
		check(!Profiler::Manager::instance().isEnabled(), "closing the dialog stops the capture");
		check(profilerLoadCapture(path, data, error) && data.hasFooter, "file is finalized when the dialog closes");

		action->trigger();
		QApplication::processEvents();
		check(window.findChild<ProfilerDialog *>() != nullptr, "dialog opens again");
	}
#endif
}

int main(int argc, char **argv)
{
#ifndef NESTOPIAQT_PROFILER_ENABLE
	(void)argc;
	(void)argv;
	std::fprintf(stderr, "Profiler is not compiled in, skipping\n");
	return 77;
#else
	QStandardPaths::setTestModeEnabled(true);

	QApplication app(argc, argv);
	QTemporaryDir dir;

	if (!dir.isValid())
	{
		return 1;
	}

	QCoreApplication::setOrganizationName(QStringLiteral("NestopiaQtProfilerTest"));
	QCoreApplication::setApplicationName(QStringLiteral("Profiler"));
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

	ProfilerCaptureData loaded;
	const QString capturePath = dir.filePath(QStringLiteral("capture.nstprof"));

	testCaptureRoundTrip(capturePath, loaded);
	testBadFiles(capturePath, dir.path());
	testViewer(std::make_shared<const ProfilerCaptureData>(loaded));
	testEmulatorMarkers(dir.filePath(QStringLiteral("emulator.nstprof")));
	testDialog(dir.filePath(QStringLiteral("dialog.nstprof")));

	if (failures != 0)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}

	std::printf("profiler: all checks passed\n");
	return 0;
#endif
}
