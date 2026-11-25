// Runs a ROM on the emulation thread for a while and checks the contract of
// EmulationThread: frame pacing, one frameFinished per frame, the emulation
// mutex protecting the state, pausing. Exit code 77 skips the test.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>

#include <SDL.h>

#include "audio/AudioDriver.h"
#include "core/EmulationThread.h"
#include "input/InputManager.h"

#define CHECK(condition, message)                                               \
	do {                                                                        \
		if (!(condition)) {                                                     \
			std::fprintf(stderr, "FAILED: %s (%s:%d)\n", message, __FILE__, __LINE__); \
			return 1;                                                           \
		}                                                                       \
	} while (0)

// Audio device with its own clock that runs a little faster than the
// emulation, like a real sound card does. SDL's dummy driver cannot be used,
// it sleeps whole milliseconds per buffer and drains much too quickly.
class FakeAudioDevice : public AudioDriverBase
{
	public:
		explicit FakeAudioDevice(double clockRatio) : ratio(clockRatio), running(false) {}
		~FakeAudioDevice(void) override { kill(); }

		Driver driver(void) const override { return DriverSDL; }

	protected:
		bool openDevice(unsigned int sampleRate, const QString &) override
		{
			running = true;
			worker = std::thread([this, sampleRate]() {
				constexpr int period = 480;
				std::vector<int16_t> buffer(period);
				const auto start = std::chrono::steady_clock::now();

				for (long long n = 1; running; ++n)
				{
					std::this_thread::sleep_until(start +
						std::chrono::nanoseconds((long long)(double(n) * period * 1e9 / (double(sampleRate) * ratio))));
					fillAudio(buffer.data(), period);
				}
			});
			return true;
		}

		void closeDevice(void) override
		{
			running = false;

			if (worker.joinable())
			{
				worker.join();
			}
		}

	private:
		double ratio;
		std::atomic<bool> running;
		std::thread worker;
};

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);

	if (argc < 2 || argv[1][0] == '\0')
	{
		std::fprintf(stderr, "No test ROM given, skipping\n");
		return 77;
	}

	const QString romPath = QString::fromLocal8Bit(argv[1]);
	const QString databasePath = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
	const QString screenshotPath = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();

	// Declared first so it outlives the emulation thread on every exit path
	std::unique_ptr<AudioDriverBase> audio;
	EmulationThread emulation;
	InputManager input;

	emulation.setInputManager(&input);
	// NESTUX_TEST_AUDIO=sdl or qt runs against a real audio device
	const QByteArray realAudio = qgetenv("NESTUX_TEST_AUDIO").toLower();
	// Sanitizer builds are too slow to keep the pace
	const bool timing = !qEnvironmentVariableIsSet("NESTUX_TEST_SKIP_TIMING");
	if (realAudio == "sdl")
	{
		audio.reset(AudioDriverBase::create(AudioDriverBase::DriverSDL));
	}
	else if (realAudio == "qt")
	{
		audio.reset(AudioDriverBase::create(AudioDriverBase::DriverQt));
	}
	else
	{
		audio.reset(new FakeAudioDevice(1.001));
	}

	AudioDriverBase::Options audioOptions;

	CHECK(audio->init(audioOptions), "audio init");

	{
		EmulationThread::Locker lock(emulation.mutex());
		NesCore &core = emulation.core();
		QString error;

		if (!databasePath.isEmpty())
		{
			core.loadDatabase(databasePath);
		}

		core.setAudioDriver(audio.get());
		CHECK(core.loadRom(romPath, error), qPrintable(error));
		CHECK(core.frameRate() > 59.0 && core.frameRate() < 61.0, "NTSC frame rate");
	}

	std::atomic<quint64> emitted(0);
	quint64 received = 0;
	quint64 lastFrameNumber = 0;
	bool frameNumbersInOrder = true;

	// Direct: runs on the emulation thread, counts every emit
	QObject::connect(&emulation, &EmulationThread::frameFinished, &emulation, [&](quint64) { ++emitted; },
		Qt::DirectConnection);
	// Queued: delivered to the main thread
	QObject::connect(&emulation, &EmulationThread::frameFinished, &app, [&](quint64 number) {
		++received;
		frameNumbersInOrder = frameNumbersInOrder && number > lastFrameNumber;
		lastFrameNumber = number;
	}, Qt::QueuedConnection);

	emulation.start();

	// The GUI thread reads the emulation state while it runs
	int lockedReads = 0;
	QTimer sampler;

	QObject::connect(&sampler, &QTimer::timeout, [&]() {
		EmulationThread::Locker lock(emulation.mutex());
		(void)emulation.core().frameCount();

		if (++lockedReads % 25 == 0 && qEnvironmentVariableIsSet("NESTUX_TEST_VERBOSE"))
		{
			const AudioDriverBase::Stats stats = audio->stats();

			std::printf("t=%4lld ms fifo=%u/%u underruns=%u overruns=%u\n", (long long)(lockedReads * 10),
				stats.fifoUsed, stats.fifoSize, stats.underruns, stats.overruns);
		}
	});
	sampler.start(10);

	QElapsedTimer elapsed;
	elapsed.start();
	QTimer::singleShot(2000, &app, &QCoreApplication::quit);
	app.exec();
	sampler.stop();

	const double seconds = double(elapsed.nsecsElapsed()) / 1e9;
	quint64 framesRun;
	bool nonBlack = false;

	{
		EmulationThread::Locker lock(emulation.mutex());
		framesRun = emulation.core().frameCount();

		const uint32_t *pixels = emulation.core().frameBuffer();

		for (int i = 0; i < NesCore::kMaxFrameWidth * NesCore::kFrameHeight && !nonBlack; ++i)
		{
			nonBlack = (pixels[i] & 0xFFFFFF) != 0;
		}

		if (!screenshotPath.isEmpty())
		{
			QImage image(emulation.core().frameWidth(), emulation.core().frameHeight(), QImage::Format_RGB32);

			for (int y = 0; y < image.height(); ++y)
			{
				std::memcpy(image.scanLine(y), pixels + y * image.width(), size_t(image.width()) * 4);
			}

			image.save(screenshotPath);
		}
	}

	const double fps = double(framesRun) / seconds;

	std::printf("frames=%llu seconds=%.3f fps=%.2f emitted=%llu received=%llu lockedReads=%d\n",
		(unsigned long long)framesRun, seconds, fps, (unsigned long long)emitted.load(),
		(unsigned long long)received, lockedReads);

	CHECK(!timing || (fps > 58.5 && fps < 61.5), "frame pacing is not close to 60.1 fps");
	CHECK(nonBlack, "the frame buffer is black");
	CHECK(lockedReads > 50, "the GUI thread could not lock the emulation mutex");
	CHECK(frameNumbersInOrder, "frame numbers arrived out of order");
	CHECK(emitted.load() == framesRun, "frameFinished was not emitted once per frame");
	CHECK(received > framesRun / 2, "the GUI thread missed too many frames");

	if (audio && timing)
	{
		const AudioDriverBase::Stats stats = audio->stats();

		std::printf("audio fifo=%u/%u underruns=%u overruns=%u\n", stats.fifoUsed, stats.fifoSize,
			stats.underruns, stats.overruns);

		CHECK(stats.underruns <= 2, "audio underruns, the rate control does not keep the FIFO filled");
		// A real device may stall while it starts, which trims the FIFO once
		CHECK(stats.overruns <= (realAudio.isEmpty() ? 0u : 2u), "audio overruns");
		CHECK(stats.fifoUsed > stats.fifoSize / 5 && stats.fifoUsed < stats.fifoSize * 4 / 5,
			"audio FIFO level drifted away from the target");
	}

	// Keyboard input reaches the pad on the emulation thread: A changes the picture of the test ROM
	auto checksum = [&]() {
		EmulationThread::Locker lock(emulation.mutex());
		const uint32_t *pixels = emulation.core().frameBuffer();
		uint64_t sum = 1469598103934665603ull;

		for (int i = 0; i < NesCore::kMaxFrameWidth * NesCore::kFrameHeight; ++i)
		{
			sum = (sum ^ pixels[i]) * 1099511628211ull;
		}

		return sum;
	};

	const uint64_t before = checksum();

	{
		EmulationThread::Locker lock(emulation.mutex());
		input.keyEvent(Qt::Key_F, true);
	}

	QThread::msleep(150);

	{
		EmulationThread::Locker lock(emulation.mutex());
		input.keyEvent(Qt::Key_F, false);
	}

	QThread::msleep(150);
	CHECK(checksum() != before, "keyboard input did not reach the emulated pad");

	// A virtual joystick hot plugged into the running emulation drives player 1
	{
		EmulationThread::Locker lock(emulation.mutex());
		CHECK(input.joysticks().init(), "SDL joystick init");
	}

	const int virtualIndex = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 8, 1);

	if (virtualIndex >= 0)
	{
		SDL_Joystick *virtualJoystick = SDL_JoystickOpen(virtualIndex);

		CHECK(virtualJoystick != nullptr, "virtual joystick open");
		QThread::msleep(100);

		int virtualDevice = -1;

		{
			EmulationThread::Locker lock(emulation.mutex());
			JoystickSystem &joysticks = input.joysticks();

			// Other joysticks may be connected to the machine running the test
			for (int i = 0; i < joysticks.deviceCount(); ++i)
			{
				if (joysticks.device(i).name == QLatin1String("Virtual Controller"))
				{
					virtualDevice = i;
				}
			}

			CHECK(virtualDevice >= 0, "the hot plugged joystick was not detected");

			InputConfig config = input.config();

			config.players[0].joyDevice = joysticks.device(virtualDevice).key;
			input.setConfig(config);
			CHECK(input.resolveJoystick(0) == virtualDevice, "joystick not assigned to player 1");
		}

		const uint64_t beforeJoystick = checksum();

		// The generic layout maps button 0 to B, which toggles the BG pattern table
		SDL_JoystickSetVirtualButton(virtualJoystick, 0, 1);
		QThread::msleep(150);
		SDL_JoystickSetVirtualButton(virtualJoystick, 0, 0);
		QThread::msleep(150);
		CHECK(checksum() != beforeJoystick, "joystick input did not reach the emulated pad");

		SDL_JoystickClose(virtualJoystick);
		SDL_JoystickDetachVirtual(virtualIndex);
		QThread::msleep(100);

		{
			EmulationThread::Locker lock(emulation.mutex());

			for (int i = 0; i < input.joysticks().deviceCount(); ++i)
			{
				CHECK(input.joysticks().device(i).name != QLatin1String("Virtual Controller"),
					"the removed joystick is still listed");
			}
		}
	}
	else
	{
		std::fprintf(stderr, "Virtual joysticks are not available, skipping the joystick check\n");
	}

	// Pausing stops the frames
	emulation.setPaused(true);
	QThread::msleep(100);

	quint64 pausedAt;
	{
		EmulationThread::Locker lock(emulation.mutex());
		pausedAt = emulation.core().frameCount();
	}

	QThread::msleep(200);

	{
		EmulationThread::Locker lock(emulation.mutex());
		CHECK(emulation.core().frameCount() == pausedAt, "frames were generated while paused");
	}

	emulation.setPaused(false);
	QThread::msleep(200);

	{
		EmulationThread::Locker lock(emulation.mutex());
		CHECK(emulation.core().frameCount() > pausedAt, "emulation did not resume");
	}

	emulation.stop();

	{
		EmulationThread::Locker lock(emulation.mutex());
		emulation.core().setAudioDriver(nullptr);
		emulation.core().unload();
	}

	audio.reset();
	std::puts("OK");
	return 0;
}
