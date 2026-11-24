#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include <QString>

// Common base of the audio output backends (SDL, Qt).
//
// The emulation thread produces mono signed 16 bit samples at the rate of the
// video timer, the audio device consumes them at the rate of its own clock.
// Both clocks differ slightly, so write() resamples every block by a factor
// derived from the FIFO fill level (dynamic rate control) and keeps the FIFO
// around half full. That is the "audio resampled to match" half of the
// timer driven frame pacing.
//
// A backend only opens and closes a device whose thread periodically calls
// fillAudio(). The FIFO has its own lock, it is shared by the emulation
// thread and the device thread and is independent of the emulation mutex.
class AudioDriverBase
{
	public:
		enum Driver
		{
			DriverSDL = 0,
			DriverQt
		};

		struct Options
		{
			unsigned int sampleRate = 48000;
			// Size of the FIFO in milliseconds, half of it is the target latency
			int bufferMs = 100;
			// Backend specific device identifier, empty selects the default
			QString device;
		};

		AudioDriverBase(void);
		virtual ~AudioDriverBase(void);

		AudioDriverBase(const AudioDriverBase &) = delete;
		AudioDriverBase &operator=(const AudioDriverBase &) = delete;

		static bool isAvailable(Driver driver);
		// nullptr if the backend is not available
		static AudioDriverBase *create(Driver driver);
		static const char *driverName(Driver driver);

		virtual Driver driver(void) const = 0;
		virtual QString driverInfo(void) const { return QString(); }

		// Opens the device, returns false and leaves the driver closed on failure.
		bool init(const Options &options);
		void kill(void);
		bool isOpen(void) const { return opened; }
		unsigned int sampleRate(void) const { return rate; }

		// Queues samples from the emulation thread, never blocks on the device.
		void write(const int16_t *samples, int count);

		// While paused the device outputs silence. Leaving the pause empties the FIFO.
		void setPaused(bool paused);
		void setMuted(bool muted) { mutedFlag.store(muted); }
		bool isMuted(void) const { return mutedFlag.load(); }
		// 0.0 - 1.0
		void setVolume(float volume) { volumeValue.store(volume); }
		float volume(void) const { return volumeValue.load(); }

		struct Stats
		{
			unsigned int fifoSize = 0;
			unsigned int fifoUsed = 0;
			// Counted once the FIFO has been primed after init or a pause
			unsigned int underruns = 0;
			unsigned int overruns = 0;
		};

		Stats stats(void) const;

	protected:
		// Open the device for mono signed 16 bit samples, on success the
		// device must be running and call fillAudio() from its own thread.
		virtual bool openDevice(unsigned int sampleRate, const QString &device) = 0;
		virtual void closeDevice(void) = 0;

		// Device thread entry point
		void fillAudio(int16_t *out, int count);

		// Samples per device callback the backend should aim for
		int periodSamples(void) const;

	private:
		mutable std::mutex lock;
		std::vector<int16_t> fifo;
		unsigned int readPos;
		unsigned int writePos;
		unsigned int used;
		unsigned int target;
		unsigned int underrunCount;
		unsigned int overrunCount;

		// Resampler state, emulation thread only
		double phase;
		int16_t previous;
		std::vector<int16_t> scratch;

		// Device thread only
		int16_t lastOut;

		unsigned int rate;
		bool opened;
		bool needsPrime;
		std::atomic<bool> deviceStarted;
		std::atomic<bool> pausedFlag;
		std::atomic<bool> mutedFlag;
		std::atomic<float> volumeValue;
};
