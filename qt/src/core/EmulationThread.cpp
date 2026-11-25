#include "core/EmulationThread.h"

#include <chrono>
#include <thread>

#include "input/InputManager.h"
#include "profiler/Profiler.h"

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
	  pausedFlag(false),
	  debuggerEnabled(false),
	  haltedFlag(false),
	  haltGuards(0),
	  resumePending(false),
	  resumeRequest{DebugRequest::RUN, 0, 0}
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
		haltCondition.wakeAll();
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

EmulationThread::HaltGuard::HaltGuard(EmulationThread &owner)
	: thread(owner)
{
	Locker lock(&thread.emuMutex);

	++thread.haltGuards;
	thread.haltCondition.wakeAll();

	while (thread.haltedFlag.load())
	{
		thread.haltExitCondition.wait(&thread.emuMutex);
	}
}

EmulationThread::HaltGuard::~HaltGuard(void)
{
	Locker lock(&thread.emuMutex);
	--thread.haltGuards;
}

void EmulationThread::setDebuggerEnabled(bool enabled)
{
	HaltGuard guard(*this);
	Locker lock(&emuMutex);

	nesCore.debugger().SetCallback(enabled ? &EmulationThread::onDebuggerEvent : nullptr, this);
	debuggerEnabled.store(enabled);
}

bool EmulationThread::resume(const DebugRequest &request)
{
	Locker lock(&emuMutex);

	if (!haltedFlag.load())
	{
		return false;
	}

	resumeRequest = request;
	resumePending = true;
	haltCondition.wakeAll();
	return true;
}

void EmulationThread::requestBreak(void)
{
	Locker lock(&emuMutex);
	nesCore.debugger().RequestBreak();
}

void NST_CALLBACK EmulationThread::onDebuggerEvent(void *userData, const Nes::Api::Debugger::Event &event,
	DebugRequest &request)
{
	static_cast<EmulationThread *>(userData)->halt(event, request);
}

bool EmulationThread::haltAllowed(void) const
{
	return !quitFlag.load() && haltGuards == 0;
}

// Runs on the emulation thread inside runFrame() with the mutex held
void EmulationThread::halt(const Nes::Api::Debugger::Event &event, DebugRequest &request)
{
	if (!haltAllowed())
	{
		return;
	}

	haltedFlag.store(true);
	resumePending = false;

	emit halted(quint16(event.address), int(event.reason));

	while (!resumePending && haltAllowed())
	{
		// Releases the emulation mutex while halted
		haltCondition.wait(&emuMutex);
	}

	if (resumePending && haltAllowed())
	{
		request = resumeRequest;
	}

	resumePending = false;
	haltedFlag.store(false);
	haltExitCondition.wakeAll();

	emit resumed();
}

bool EmulationThread::shouldRun(void) const
{
	return !pausedFlag.load() && nesCore.isLoaded();
}

void EmulationThread::run(void)
{
	NESTUX_PROFILER_SET_THREAD_NAME("Emulator");

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
			NESTUX_PROFILE_FUNC("EMU Thread Input Update");
			inputManager->applyTo(nesCore);
		}

		nesCore.runFrame();

		const quint64 frameNumber = nesCore.frameCount();
		const auto period = std::chrono::nanoseconds(int64_t(1e9 / nesCore.frameRate()));

		lock.unlock();

		{
			NESTUX_PROFILE_FUNC("EMU Thread Frame Signal");
			emit frameFinished(frameNumber);
		}

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

		{
			NESTUX_PROFILE_FUNC("EMU Thread Frame Pacing Sleep");
			sleepUntil(deadline);
		}

		{
			NESTUX_PROFILE_FUNC("EMU Thread Lock Wait");
			lock.relock();
		}
	}
}
