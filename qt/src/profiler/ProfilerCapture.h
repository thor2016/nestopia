// ProfilerCapture.h
//
// Collector thread that drains profiler markers from all threads and streams
// them to a binary capture file.
//
// Capture file layout (all integers little-endian):
//
//   Header (28 bytes)
//     char[8]  magic         "NSTQPROF"
//     u32      version       ProfilerCapture::FORMAT_VERSION
//     u64      countFreq     timestamp counts per second
//     u64      startCounts   timestamp (counts) when the capture started
//
//   Followed by a sequence of chunks until EOF:
//     u8       chunkType
//     u32      payloadSize
//     u8[]     payload
//
//   CHUNK_STRING   : u32 stringId, utf8 text (rest of payload)
//   CHUNK_SITE     : u32 siteId, u32 line, u32 fileStrId, u32 funcStrId, u32 commentStrId
//   CHUNK_THREAD   : u32 threadId, utf8 name (rest of payload). May repeat if a thread is renamed.
//   CHUNK_MARKERS  : u32 threadId, u32 count, then count records of:
//                        u8 type (MARKER_START/MARKER_END), u32 siteId, u64 timestamp (counts)
//   CHUNK_FOOTER   : u64 totalMarkerCount
//
// String and site definitions always precede the first marker block that uses them.
// A truncated capture (e.g. after a crash) is valid up to the last complete chunk.

#pragma once

#ifdef NESTOPIAQT_PROFILER_ENABLE

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>

#include <QByteArray>
#include <QString>
#include <QThread>

#include "profiler/Profiler.h"
#include "profiler/ProfilerCaptureFormat.h"

class ProfilerCaptureThread : public QThread
{
	Q_OBJECT

	public:
		explicit ProfilerCaptureThread(QObject *parent = nullptr);
		~ProfilerCaptureThread(void) override;

		void setFilePath(const QString &path) { filePath = path; }
		void setPollIntervalMs(unsigned int ms) { pollIntervalMs = ms; }

		uint64_t markerCount(void) const { return markers; }
		uint64_t bytesWritten(void) const { return bytes; }

		// Ask the thread to flush remaining markers, finalize the file and exit
		void stopCapture(void);

	signals:
		void captureError(const QString &message);

	protected:
		void run(void) override;

	private:
		uint32_t internString(const char *str);
		uint32_t internSite(const Profiler::Site *site);

		QString filePath;
		unsigned int pollIntervalMs = 10;

		std::atomic<uint64_t> markers{0};
		std::atomic<uint64_t> bytes{0};

		// Only accessed from the collector thread
		QByteArray outBuf;
		std::unordered_map<const char *, uint32_t> stringPtrCache;
		std::unordered_map<std::string, uint32_t> stringIds;
		std::unordered_map<const Profiler::Site *, uint32_t> sitePtrCache;
		std::map<std::array<uint32_t, 4>, uint32_t> siteIds;
		std::unordered_map<uint32_t, std::string> announcedThreads;
};

#endif // NESTOPIAQT_PROFILER_ENABLE
