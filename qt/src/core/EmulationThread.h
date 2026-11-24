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
class EmulationThread : public QThread
{
	Q_OBJECT

	public:
		using Locker = QMutexLocker<QMutex>;

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

	signals:
		// Emitted at the end of every executed frame, after the thread has
		// released the emulation mutex. Receivers living in the GUI thread
		// get it queued and may lock the mutex to read the frame.
		void frameFinished(quint64 frameNumber);

	protected:
		void run(void) override;

	private:
		bool shouldRun(void) const;

		QMutex emuMutex;
		QWaitCondition wakeCondition;
		NesCore nesCore;
		InputManager *inputManager;
		std::atomic<bool> quitFlag;
		std::atomic<bool> pausedFlag;
};
