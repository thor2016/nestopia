#include "profiler/ProfilerCapture.h"

#ifdef NESTOPIAQT_PROFILER_ENABLE

#include <QFile>

namespace
{
	void putU8(QByteArray &b, uint8_t v)
	{
		b.append(static_cast<char>(v));
	}

	void putU32(QByteArray &b, uint32_t v)
	{
		char d[4];

		for (int i = 0; i < 4; i++)
		{
			d[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
		}

		b.append(d, 4);
	}

	void putU64(QByteArray &b, uint64_t v)
	{
		char d[8];

		for (int i = 0; i < 8; i++)
		{
			d[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
		}

		b.append(d, 8);
	}

	void putChunk(QByteArray &out, ProfilerCapture::ChunkType type, const QByteArray &payload)
	{
		putU8(out, type);
		putU32(out, static_cast<uint32_t>(payload.size()));
		out.append(payload);
	}
}

ProfilerCaptureThread::ProfilerCaptureThread(QObject *parent)
	: QThread(parent)
{
	setObjectName(QStringLiteral("ProfilerCaptureThread"));
}

ProfilerCaptureThread::~ProfilerCaptureThread(void)
{
	stopCapture();
	wait();
}

void ProfilerCaptureThread::stopCapture(void)
{
	requestInterruption();
}

uint32_t ProfilerCaptureThread::internString(const char *str)
{
	if (str == nullptr)
	{
		str = "";
	}

	auto pit = stringPtrCache.find(str);

	if (pit != stringPtrCache.end())
	{
		return pit->second;
	}

	// Different pointers may hold identical text (e.g. __FILE__ across translation units)
	const std::string text(str);
	uint32_t id;
	auto it = stringIds.find(text);

	if (it != stringIds.end())
	{
		id = it->second;
	}
	else
	{
		id = static_cast<uint32_t>(stringIds.size());
		stringIds[text] = id;

		QByteArray payload;

		putU32(payload, id);
		payload.append(text.data(), static_cast<qsizetype>(text.size()));
		putChunk(outBuf, ProfilerCapture::CHUNK_STRING, payload);
	}

	stringPtrCache[str] = id;
	return id;
}

uint32_t ProfilerCaptureThread::internSite(const Profiler::Site *site)
{
	auto pit = sitePtrCache.find(site);

	if (pit != sitePtrCache.end())
	{
		return pit->second;
	}

	// Identical sites in different translation units get one id
	const uint32_t fileId = internString(site->file);
	const uint32_t funcId = internString(site->func);
	const uint32_t commentId = internString(site->comment);
	const std::array<uint32_t, 4> key = { static_cast<uint32_t>(site->line), fileId, funcId, commentId };

	uint32_t id;
	auto it = siteIds.find(key);

	if (it != siteIds.end())
	{
		id = it->second;
	}
	else
	{
		id = static_cast<uint32_t>(siteIds.size());
		siteIds[key] = id;

		QByteArray payload;

		putU32(payload, id);
		putU32(payload, key[0]);
		putU32(payload, fileId);
		putU32(payload, funcId);
		putU32(payload, commentId);
		putChunk(outBuf, ProfilerCapture::CHUNK_SITE, payload);
	}

	sitePtrCache[site] = id;
	return id;
}

void ProfilerCaptureThread::run(void)
{
	Profiler::Manager &manager = Profiler::Manager::instance();

	QFile file(filePath);

	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		emit captureError(tr("Failed to open capture file for writing: %1").arg(file.errorString()));
		return;
	}

	markers = 0;
	bytes = 0;
	outBuf.clear();
	stringPtrCache.clear();
	stringIds.clear();
	sitePtrCache.clear();
	siteIds.clear();
	announcedThreads.clear();

	outBuf.append(ProfilerCapture::FILE_MAGIC, sizeof(ProfilerCapture::FILE_MAGIC));
	putU32(outBuf, ProfilerCapture::FORMAT_VERSION);
	putU64(outBuf, Profiler::timestampFrequency);
	putU64(outBuf, Profiler::timestampNow());

	bool writeOk = true;

	auto flushOutput = [&]() -> bool
	{
		if (outBuf.isEmpty())
		{
			return true;
		}

		const bool ok = file.write(outBuf) == outBuf.size();

		bytes += outBuf.size();
		outBuf.clear();

		if (!ok)
		{
			emit captureError(tr("Failed to write capture file: %1").arg(file.errorString()));
		}

		return ok;
	};

	auto onMarkers = [this](uint32_t threadId, const std::string &threadName,
		const Profiler::Marker *threadMarkers, size_t count)
	{
		auto ait = announcedThreads.find(threadId);

		if (ait == announcedThreads.end() || ait->second != threadName)
		{
			announcedThreads[threadId] = threadName;

			QByteArray payload;

			putU32(payload, threadId);
			payload.append(threadName.data(), static_cast<qsizetype>(threadName.size()));
			putChunk(outBuf, ProfilerCapture::CHUNK_THREAD, payload);
		}

		QByteArray payload;

		payload.reserve(static_cast<qsizetype>(8 + count * ProfilerCapture::MARKER_RECORD_SIZE));
		putU32(payload, threadId);
		putU32(payload, static_cast<uint32_t>(count));

		for (size_t i = 0; i < count; i++)
		{
			const Profiler::Marker &m = threadMarkers[i];

			putU8(payload, (m.type == Profiler::MarkerType::Start) ? ProfilerCapture::MARKER_START : ProfilerCapture::MARKER_END);
			putU32(payload, internSite(m.site));
			putU64(payload, m.timestamp);
		}

		putChunk(outBuf, ProfilerCapture::CHUNK_MARKERS, payload);
		markers += count;
	};

	manager.setExternalCollector(true);
	manager.setEnabled(true);

	while (writeOk && !isInterruptionRequested())
	{
		manager.drainMarkers(onMarkers);
		writeOk = flushOutput();

		if (writeOk)
		{
			msleep(pollIntervalMs);
		}
	}

	// Stop producers, give in-flight writers time to finish, then empty every buffer
	manager.setEnabled(false);
	msleep(pollIntervalMs * 2);
	manager.drainMarkers(onMarkers, true);
	manager.setExternalCollector(false);

	QByteArray footer;

	putU64(footer, markers);
	putChunk(outBuf, ProfilerCapture::CHUNK_FOOTER, footer);

	if (writeOk)
	{
		flushOutput();
	}

	file.close();
}

#endif // NESTOPIAQT_PROFILER_ENABLE
