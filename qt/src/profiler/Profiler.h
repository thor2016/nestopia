// Profiler.h
//
// High precision timing of scopes. A marker is written to a thread local buffer at the start
// and at the end of a profiled scope, a collector (see ProfilerCapture.h) drains the buffers
// of all threads. Nothing of this is compiled in unless NESTUX_PROFILER_ENABLE is defined.
//
// To time a scope, put the macro at the top of it:
//
//     NESTUX_PROFILE_FUNC("Text that describes the scope");
//
// Without NESTUX_PROFILER_ENABLE the macros expand to nothing. While the profiler is
// compiled in but not capturing, a profiled scope costs one relaxed load and a branch.

#pragma once

#ifdef NESTUX_PROFILER_ENABLE

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <vector>

namespace Profiler
{
	// Compile time description of one profiled scope
	struct Site
	{
		int         line;
		const char *file;
		const char *func;
		const char *comment;
	};

	enum class MarkerType : uint8_t
	{
		Start,
		End
	};

	struct Marker
	{
		const Site *site;
		uint64_t    timestamp; // counts, see timestampFrequency
		MarkerType  type;
	};

	// Monotonic time in nanoseconds
	constexpr uint64_t timestampFrequency = 1000000000;

	inline uint64_t timestampNow(void)
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	// The producer writes to the active buffer while the collector drains an older one
	struct MarkerBuffer
	{
		static constexpr unsigned int maxBuffers = 4;

		MarkerBuffer(void);
		~MarkerBuffer(void);

		void push(const Marker &marker);

		std::vector<Marker> vec[maxBuffers];
		std::atomic<unsigned int> activeBufferIndex{0};
		// Released by the producer after every push and acquired by the collector before it
		// reads a buffer, which makes the markers written so far visible to the collector
		std::atomic<uint64_t> published{0};
		uint64_t    pushCount = 0; // only used by the producer
		uint32_t    threadId = 0;
		std::string threadName;
	};

	// Master switch, kept as a plain global so the disabled check is a single inlined load
	extern std::atomic<bool> enabledFlag;

#if defined(__GNUC__) || defined(__clang__)
#define NESTUX_PROFILER_UNLIKELY(x)  __builtin_expect(!!(x), 0)
#else
#define NESTUX_PROFILER_UNLIKELY(x)  (x)
#endif

	class FuncScoped
	{
		public:
			// When profiling is disabled this is one relaxed load and a branch, with no calls
			explicit FuncScoped(const Site *s)
			{
				if (NESTUX_PROFILER_UNLIKELY(enabledFlag.load(std::memory_order_relaxed)))
				{
					site = s;
					pushMarker(s, MarkerType::Start);
				}
			}

			~FuncScoped(void)
			{
				if (NESTUX_PROFILER_UNLIKELY(site != nullptr))
				{
					pushMarker(site, MarkerType::End);
				}
			}

			FuncScoped(const FuncScoped &) = delete;
			FuncScoped &operator=(const FuncScoped &) = delete;

		private:
			static void pushMarker(const Site *s, MarkerType type);

			// Non-null only if the start marker was recorded, so that end markers always pair up
			const Site *site = nullptr;
	};

	class Manager
	{
		public:
			// Called once per thread with the markers drained from that thread's buffer (in time order)
			typedef std::function<void(uint32_t threadId, const std::string &threadName,
				const Marker *markers, size_t count)> DrainCallback;

			static Manager &instance(void);

			void setEnabled(bool enable) { enabledFlag.store(enable, std::memory_order_relaxed); }
			bool isEnabled(void) const { return enabledFlag.load(std::memory_order_relaxed); }

			// Markers of threads that exit are only kept while a collector is active
			void setExternalCollector(bool active) { externalCollector.store(active); }

			void setThreadName(MarkerBuffer *buffer, const char *name);

			// Drains one buffer per thread, or all of them (flushAll) to empty the pipeline.
			// The callback is invoked with the thread list locked.
			void drainMarkers(const DrainCallback &callback, bool flushAll = false);

			void addThreadBuffer(MarkerBuffer *buffer);
			void removeThreadBuffer(MarkerBuffer *buffer);

		private:
			Manager(void) = default;

			// Markers left over from threads that exited before the collector drained them
			struct RetiredBuffer
			{
				uint32_t threadId;
				std::string threadName;
				std::vector<Marker> markers;
			};

			std::mutex threadListMutex;
			std::list<RetiredBuffer> retiredList;
			std::list<MarkerBuffer *> bufferList;
			std::atomic<bool> externalCollector{false};
			uint32_t nextThreadId = 1;
	};

	// Names the calling thread in captures. Cheap to repeat with the same string.
	void setThreadName(const char *name);
}

#if defined(__GNUC__) || defined(__clang__)
#define NESTUX_PROFILE_FUNC_NAME  __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
#define NESTUX_PROFILE_FUNC_NAME  __FUNCSIG__
#else
#define NESTUX_PROFILE_FUNC_NAME  __func__
#endif

#define NESTUX_PROFILE_CONCAT_IMPL(a, b) a##b
#define NESTUX_PROFILE_CONCAT(a, b) NESTUX_PROFILE_CONCAT_IMPL(a, b)

#define NESTUX_PROFILE_FUNC(comment)  \
	static constexpr Profiler::Site NESTUX_PROFILE_CONCAT(profSite, __LINE__) = \
		{ __LINE__, __FILE__, NESTUX_PROFILE_FUNC_NAME, comment }; \
	Profiler::FuncScoped NESTUX_PROFILE_CONCAT(profScope, __LINE__)( &NESTUX_PROFILE_CONCAT(profSite, __LINE__) )

#define NESTUX_PROFILER_SET_THREAD_NAME(name)  Profiler::setThreadName(name)

#else  // NESTUX_PROFILER_ENABLE not defined

#define NESTUX_PROFILE_FUNC(comment)
#define NESTUX_PROFILER_SET_THREAD_NAME(name)

#endif // NESTUX_PROFILER_ENABLE
