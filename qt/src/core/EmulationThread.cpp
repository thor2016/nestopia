#include "core/EmulationThread.h"

#include <chrono>
#include <thread>

#include "input/InputManager.h"

namespace
{
	using Clock = std::chrono::steady_clock;

	// Sleeps until the deadline, the last part is spent spinning because the
	// kernel wakes sleeping threads a little late.
	void sleepUntil(Clock::time_point deadline)
	{
		constexpr auto spinTime = std::chrono::microseconds(150);

		std::this_thread::sleep_until(deadline - spinTime);

		while (Clock::now() < deadline)
		{
			std::this_thread::yield();
		}
	}
}

EmulationThread::EmulationThread(QObject *parent)
	: QThread(parent),
	  inputManager(nullptr),
	  quitFlag(false),
	  pausedFlag(false)
{
	setObjectName(QStringLiteral("EmulationThread"));
}

EmulationThread::~EmulationThread(void)
{
	stop();
}

void EmulationThread::setInputManager(InputManager *manager)
{
	inputManager = manager;
}

void EmulationThread::stop(void)
{
	if (!isRunning())
	{
		return;
	}

	quitFlag.store(true);

	{
		Locker lock(&emuMutex);
		wakeCondition.wakeAll();
	}

	wait();
	quitFlag.store(false);
}

void EmulationThread::setPaused(bool paused)
{
	Locker lock(&emuMutex);

	pausedFlag.store(paused);
	wakeCondition.wakeAll();
}

void EmulationThread::wake(void)
{
	wakeCondition.wakeAll();
}

bool EmulationThread::shouldRun(void) const
{
	return !pausedFlag.load() && nesCore.isLoaded();
}

void EmulationThread::run(void)
{
	Clock::time_point deadline;
	bool paced = false;

	Locker lock(&emuMutex);

	while (!quitFlag.load())
	{
		if (!shouldRun())
		{
			// Releases the emulation mutex while waiting
			paced = false;
			wakeCondition.wait(&emuMutex);
			continue;
		}

		if (inputManager != nullptr)
		{
			inputManager->applyTo(nesCore);
		}

		nesCore.runFrame();

		const quint64 frameNumber = nesCore.frameCount();
		const auto period = std::chrono::nanoseconds(int64_t(1e9 / nesCore.frameRate()));

		lock.unlock();

		emit frameFinished(frameNumber);

		const Clock::time_point now = Clock::now();

		if (!paced)
		{
			deadline = now;
			paced = true;
		}

		deadline += period;

		if (now > deadline + std::chrono::milliseconds(100))
		{
			// The thread was starved for a long time, do not run catch up frames
			deadline = now;
		}

		sleepUntil(deadline);

		lock.relock();
	}
}
