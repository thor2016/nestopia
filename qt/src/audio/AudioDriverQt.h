#pragma once

#include "audio/AudioDriver.h"

class QThread;
class QAudioSink;
class QtAudioContext;

class AudioDriverQt : public AudioDriverBase
{
	public:
		AudioDriverQt(void);
		~AudioDriverQt(void) override;

		Driver driver(void) const override { return DriverQt; }
		QString driverInfo(void) const override;

	protected:
		bool openDevice(unsigned int sampleRate, const QString &device) override;
		void closeDevice(void) override;

	private:
		QThread *thread;
		QtAudioContext *context;
};
