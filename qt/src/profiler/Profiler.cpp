#include "profiler/Profiler.h"

#ifdef NESTOPIAQT_PROFILER_ENABLE

namespace Profiler
{
	std::atomic<bool> enabledFlag{false};

	namespace
	{
		// Created by the first marker or thread name of each thread
		thread_local MarkerBuffer threadBuffer;
	}

	// -----------------------------------------------------------------------
	// Scoped marker
	// -----------------------------------------------------------------------

	void FuncScoped::pushMarker(const Site *s, MarkerType type)
	{
		Marker marker;

		marker.site = s;
		marker.timestamp = timestampNow();
		marker.type = type;

		threadBuffer.push(marker);
	}

	// -----------------------------------------------------------------------
	// Marker buffer
	// -----------------------------------------------------------------------

	MarkerBuffer::MarkerBuffer(void)
	{
		for (unsigned int i = 0; i < maxBuffers; i++)
		{
			vec[i].reserve(1024);
		}

		Manager::instance().addThreadBuffer(this);
	}

	MarkerBuffer::~MarkerBuffer(void)
	{
		Manager::instance().removeThreadBuffer(this);
	}

	void MarkerBuffer::push(const Marker &marker)
	{
		// Acquire pairs with the collector's increment, so a buffer it cleared is reused safely
		const unsigned int idx = activeBufferIndex.load(std::memory_order_acquire) % maxBuffers;

		vec[idx].push_back(marker);
		published.store(++pushCount, std::memory_order_release);
	}

	// -----------------------------------------------------------------------
	// Manager
	// -----------------------------------------------------------------------

	Manager &Manager::instance(void)
	{
		static Manager manager;

		return manager;
	}

	void Manager::addThreadBuffer(MarkerBuffer *buffer)
	{
		std::lock_guard<std::mutex> guard(threadListMutex);

		buffer->threadId = nextThreadId++;
		bufferList.push_back(buffer);
	}

	void Manager::removeThreadBuffer(MarkerBuffer *buffer)
	{
		std::lock_guard<std::mutex> guard(threadListMutex);

		for (auto it = bufferList.begin(); it != bufferList.end(); ++it)
		{
			if (*it != buffer)
			{
				continue;
			}

			if (externalCollector.load())
			{
				// Keep the unread markers, oldest buffer first, for the next drain
				RetiredBuffer retired;

				retired.threadId = buffer->threadId;
				retired.threadName = buffer->threadName;

				const unsigned int active = buffer->activeBufferIndex.load();

				for (unsigned int i = 1; i <= MarkerBuffer::maxBuffers; i++)
				{
					const auto &v = buffer->vec[(active + i) % MarkerBuffer::maxBuffers];

					retired.markers.insert(retired.markers.end(), v.begin(), v.end());
				}

				if (!retired.markers.empty())
				{
					retiredList.push_back(std::move(retired));
				}
			}

			bufferList.erase(it);
			break;
		}
	}

	void Manager::setThreadName(MarkerBuffer *buffer, const char *name)
	{
		std::lock_guard<std::mutex> guard(threadListMutex);

		buffer->threadName = name ? name : "";
	}

	void Manager::drainMarkers(const DrainCallback &callback, bool flushAll)
	{
		std::lock_guard<std::mutex> guard(threadListMutex);

		for (const RetiredBuffer &retired : retiredList)
		{
			callback(retired.threadId, retired.threadName, retired.markers.data(), retired.markers.size());
		}

		retiredList.clear();

		const unsigned int passes = flushAll ? MarkerBuffer::maxBuffers : 1;

		for (MarkerBuffer *buffer : bufferList)
		{
			// Everything the producer pushed before its last publish is visible from here on
			buffer->published.load(std::memory_order_acquire);

			for (unsigned int pass = 0; pass < passes; pass++)
			{
				// Swap the producer to the next buffer and drain the one that was retired
				// two swaps ago, so a producer that is still finishing a push is not touched.
				unsigned int idx = buffer->activeBufferIndex++;

				idx = (idx + 2) % MarkerBuffer::maxBuffers;

				auto &v = buffer->vec[idx];

				if (!v.empty())
				{
					callback(buffer->threadId, buffer->threadName, v.data(), v.size());
					v.clear();
				}
			}
		}
	}

	void setThreadName(const char *name)
	{
		static thread_local const char *lastName = nullptr;

		if (name != lastName)
		{
			lastName = name;
			Manager::instance().setThreadName(&threadBuffer, name);
		}
	}
}

#endif // NESTOPIAQT_PROFILER_ENABLE
