#pragma once

#include <atomic>
#include <cstdint>

#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

#include "core/NesCore.h"

class InputManager;

// Runs NesCore on its own thread at the frame rate of the loaded image.
//
// There is a single emulation mutex. It protects NesCore and everything that
// belongs to the emulation state (frame buffer, input bindings, joystick
// state). The thread holds it while it runs a frame and releases it while it
// waits for the next frame, so the GUI thread can lock it at any time to read
// or modify the emulation state:
//
//     EmulationThread::Locker lock(thread.mutex());
//     thread.core().reset(false);
//
// The mutex is not recursive. Do not call locking member functions of this
// class while holding it, except where noted.
//
// With the debugger enabled the thread can halt inside a frame, before an
// instruction. It then waits with the mutex released, so the GUI can lock it
// to inspect the machine, and continues when resume() is called. Anything that
// must not happen in the middle of a frame (reset, loading or unloading an
// image, swapping the audio driver) has to construct a HaltGuard first:
//
//     EmulationThread::HaltGuard guard(thread);
//     EmulationThread::Locker lock(thread.mutex());
//     thread.core().reset(false);
class EmulationThread : public QThread
{
	Q_OBJECT

	public:
		using Locker = QMutexLocker<QMutex>;
		using DebugRequest = Nes::Api::Debugger::Request;

		// Holds off debugger halts for its lifetime. If the thread is halted
		// it runs on to the end of the frame and the constructor returns once
		// it has left the halt. Construct without holding the mutex.
		class HaltGuard
		{
			public:
				explicit HaltGuard(EmulationThread &thread);
				~HaltGuard(void);

				HaltGuard(const HaltGuard &) = delete;
				HaltGuard &operator=(const HaltGuard &) = delete;

			private:
				EmulationThread &thread;
		};

		explicit EmulationThread(QObject *parent = nullptr);
		~EmulationThread(void) override;

		QMutex *mutex(void) { return &emuMutex; }
		NesCore &core(void) { return nesCore; }

		// Polled once per frame with the mutex held. Set it before starting
		// the thread.
		void setInputManager(InputManager *manager);

		// Stops the loop and waits for the thread to exit. Must be called
		// without holding the emulation mutex.
		void stop(void);

		// Paused or without a loaded image the thread sleeps and no frames
		// are generated. These lock the mutex themselves.
		void setPaused(bool paused);
		bool isPaused(void) const { return pausedFlag.load(); }
		// Wakes the thread after the caller changed the emulation state. Call
		// with the mutex held, e.g. after loading an image.
		void wake(void);

		// Installs or removes the debugger callback. Breakpoints only stop
		// the machine while it is enabled, and only then does the CPU use its
		// slower checking run loop. These lock the mutex themselves.
		void setDebuggerEnabled(bool enabled);
		bool isDebuggerEnabled(void) const { return debuggerEnabled.load(); }
		bool isHalted(void) const { return haltedFlag.load(); }
		// Continues a halted machine as the request says. Returns false if it
		// was not halted.
		bool resume(const DebugRequest &request);
		// Halts before the next instruction the thread executes. A paused
		// thread only gets there once it is unpaused.
		void requestBreak(void);

	signals:
		// Emitted at the end of every executed frame, after the thread has
		// released the emulation mutex. Receivers living in the GUI thread
		// get it queued and may lock the mutex to read the frame.
		void frameFinished(quint64 frameNumber);
		// Emitted from inside a frame when the debugger halts the machine.
		// reason is a Nes::Api::Debugger::Reason. The mutex is available
		// until the matching resumed().
		void halted(quint16 address, int reason);
		void resumed(void);

	protected:
		void run(void) override;

	private:
		bool shouldRun(void) const;
		static void NST_CALLBACK onDebuggerEvent(void *userData, const Nes::Api::Debugger::Event &event,
			DebugRequest &request);
		void halt(const Nes::Api::Debugger::Event &event, DebugRequest &request);
		bool haltAllowed(void) const;

		QMutex emuMutex;
		QWaitCondition wakeCondition;
		// The halted thread waits on haltCondition, HaltGuard on haltExitCondition
		QWaitCondition haltCondition;
		QWaitCondition haltExitCondition;
		NesCore nesCore;
		InputManager *inputManager;
		std::atomic<bool> quitFlag;
		std::atomic<bool> pausedFlag;
		std::atomic<bool> debuggerEnabled;
		std::atomic<bool> haltedFlag;
		// Guarded by the mutex
		int haltGuards;
		bool resumePending;
		DebugRequest resumeRequest;
};
