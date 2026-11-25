#include "audio/AudioDriver.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio/AudioDriverQt.h"
#include "audio/AudioDriverSDL.h"
#include "profiler/Profiler.h"

namespace
{
	// The FIFO fill level may change the playback rate by this much
	constexpr double kMaxRateAdjust = 0.005;
	// Length of the fade out when the FIFO runs dry
	constexpr int kUnderrunRamp = 128;
}

AudioDriverBase::AudioDriverBase(void)
	: readPos(0),
	  writePos(0),
	  used(0),
	  target(0),
	  underrunCount(0),
	  overrunCount(0),
	  phase(0.0),
	  previous(0),
	  lastOut(0),
	  rate(0),
	  opened(false),
	  needsPrime(true),
	  deviceStarted(false),
	  pausedFlag(false),
	  mutedFlag(false),
	  volumeValue(1.0f)
{
}

AudioDriverBase::~AudioDriverBase(void)
{
}

bool AudioDriverBase::isAvailable(Driver driver)
{
	switch (driver)
	{
		case DriverSDL: return true;
		case DriverQt:  return true;
	}

	return false;
}

AudioDriverBase *AudioDriverBase::create(Driver driver)
{
	switch (driver)
	{
		case DriverSDL: return new AudioDriverSDL();
		case DriverQt:  return new AudioDriverQt();
	}

	return nullptr;
}

const char *AudioDriverBase::driverName(Driver driver)
{
	switch (driver)
	{
		case DriverSDL: return "SDL";
		case DriverQt:  return "Qt";
	}

	return "";
}

int AudioDriverBase::periodSamples(void) const
{
	int period = 128;

	while (period < int(rate / 100))
	{
		period <<= 1;
	}

	return period;
}

bool AudioDriverBase::init(const Options &options)
{
	kill();

	rate = options.sampleRate;

	const unsigned int capacity = std::max(1024u, rate * unsigned(std::max(options.bufferMs, 20)) / 1000u);

	{
		std::lock_guard<std::mutex> guard(lock);
		fifo.assign(capacity, 0);
		readPos = writePos = used = 0;
		target = capacity / 2;
		underrunCount = overrunCount = 0;
	}

	phase = 0.0;
	previous = 0;
	lastOut = 0;
	needsPrime = true;
	deviceStarted.store(false);
	pausedFlag.store(false);

	if (!openDevice(rate, options.device))
	{
		return false;
	}

	opened = true;
	return true;
}

void AudioDriverBase::kill(void)
{
	if (opened)
	{
		// Stops the device thread first, fillAudio() is not called afterwards
		closeDevice();
		opened = false;
	}

	std::lock_guard<std::mutex> guard(lock);
	fifo.clear();
	readPos = writePos = used = 0;
}

void AudioDriverBase::setPaused(bool paused)
{
	std::lock_guard<std::mutex> guard(lock);

	readPos = writePos = used = 0;
	needsPrime = true;
	pausedFlag.store(paused);
}

void AudioDriverBase::write(const int16_t *samples, int count)
{
	NESTUX_PROFILE_FUNC("Audio FIFO Write");

	if (!opened || count <= 0 || pausedFlag.load())
	{
		return;
	}

	unsigned int level;
	unsigned int goal;
	bool prime;

	{
		std::lock_guard<std::mutex> guard(lock);
		level = needsPrime ? target : used;
		goal = target;
		prime = needsPrime;
	}

	if (prime)
	{
		phase = 0.0;
	}

	// Below the target the stream is stretched to refill the FIFO, above it
	// the stream is compressed. Output samples per input sample:
	const double error = goal ? std::clamp((double(goal) - double(level)) / double(goal), -1.0, 1.0) : 0.0;
	const double step = 1.0 / (1.0 + error * kMaxRateAdjust);

	scratch.resize(size_t(double(count) / step) + 4);

	size_t produced = 0;
	double pos = phase;

	// Linear interpolation. Position -1 is the last sample of the previous
	// block, 0 is the first sample of this block.
	while (pos < double(count - 1))
	{
		const int index = int(std::floor(pos));
		const double fraction = pos - index;
		const int s0 = index < 0 ? previous : samples[index];
		const int s1 = samples[index + 1];

		scratch[produced++] = int16_t(s0 + (s1 - s0) * fraction);
		pos += step;
	}

	phase = pos - count;
	previous = samples[count - 1];

	std::lock_guard<std::mutex> guard(lock);

	if (fifo.empty())
	{
		return;
	}

	const unsigned int capacity = unsigned(fifo.size());

	if (needsPrime)
	{
		// Start with a half full FIFO of silence so the rate control does not
		// have to stretch the stream for seconds to reach the target level.
		for (unsigned int i = 0; i < target && used < capacity; ++i)
		{
			fifo[writePos] = 0;
			writePos = (writePos + 1) % capacity;
			++used;
		}

		needsPrime = false;
	}

	// Some devices take a while to start pulling samples. The FIFO already
	// holds the primed silence, more samples would only overflow it.
	if (!deviceStarted.load())
	{
		return;
	}
	for (size_t i = 0; i < produced; ++i)
	{
		if (used >= capacity)
		{
			++overrunCount;
			break;
		}

		fifo[writePos] = scratch[i];
		writePos = (writePos + 1) % capacity;
		++used;
	}

	// The device stalled, e.g. while it was starting. The rate control would
	// need seconds to work this off, drop the oldest samples instead.
	if (used > capacity - capacity / 4)
	{
		const unsigned int excess = used - target;

		readPos = (readPos + excess) % capacity;
		used -= excess;
		++overrunCount;
	}
}

AudioDriverBase::Stats AudioDriverBase::stats(void) const
{
	Stats result;

	std::lock_guard<std::mutex> guard(lock);

	result.fifoSize = unsigned(fifo.size());
	result.fifoUsed = used;
	result.underruns = underrunCount;
	result.overruns = overrunCount;
	return result;
}

void AudioDriverBase::fillAudio(int16_t *out, int count)
{
	// Runs on the thread of the audio device
	NESTUX_PROFILER_SET_THREAD_NAME("Audio Device");
	NESTUX_PROFILE_FUNC("Audio Device Fill");

	deviceStarted.store(true);
	int copied = 0;

	{
		std::lock_guard<std::mutex> guard(lock);

		if (!pausedFlag.load() && !fifo.empty())
		{
			const unsigned int capacity = unsigned(fifo.size());
			copied = int(std::min<unsigned int>(unsigned(count), used));

			for (int i = 0; i < copied; ++i)
			{
				out[i] = fifo[readPos];
				readPos = (readPos + 1) % capacity;
			}

			used -= unsigned(copied);

			if (copied < count && !needsPrime)
			{
				++underrunCount;
			}
		}
	}

	if (copied > 0)
	{
		lastOut = out[copied - 1];
	}

	// Fade out instead of a hard step to silence when the FIFO ran dry
	for (int i = copied; i < count; ++i)
	{
		const int ramp = i - copied;

		out[i] = ramp < kUnderrunRamp ? int16_t(lastOut * (kUnderrunRamp - ramp) / kUnderrunRamp) : 0;
	}

	if (copied < count)
	{
		lastOut = 0;
	}

	const float gain = mutedFlag.load() ? 0.0f : volumeValue.load();

	if (gain != 1.0f)
	{
		for (int i = 0; i < count; ++i)
		{
			out[i] = int16_t(out[i] * gain);
		}
	}
}
