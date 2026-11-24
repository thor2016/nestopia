#pragma once

#include <SDL.h>

#include "audio/AudioDriver.h"

class AudioDriverSDL : public AudioDriverBase
{
	public:
		AudioDriverSDL(void);
		~AudioDriverSDL(void) override;

		Driver driver(void) const override { return DriverSDL; }
		QString driverInfo(void) const override;

	protected:
		bool openDevice(unsigned int sampleRate, const QString &device) override;
		void closeDevice(void) override;

	private:
		static void SDLCALL audioCallback(void *userData, Uint8 *stream, int length);

		SDL_AudioDeviceID deviceId;
		bool subsystemInit;
};
