#include "audio/AudioDriverSDL.h"

#include <QtDebug>

AudioDriverSDL::AudioDriverSDL(void)
	: deviceId(0),
	  subsystemInit(false)
{
}

AudioDriverSDL::~AudioDriverSDL(void)
{
	kill();
}

QString AudioDriverSDL::driverInfo(void) const
{
	const char *name = SDL_GetCurrentAudioDriver();

	return QStringLiteral("SDL %1.%2.%3 (%4)")
		.arg(SDL_MAJOR_VERSION).arg(SDL_MINOR_VERSION).arg(SDL_PATCHLEVEL)
		.arg(name != nullptr ? QString::fromLatin1(name) : QStringLiteral("no driver"));
}

bool AudioDriverSDL::openDevice(unsigned int sampleRate, const QString &device)
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		qWarning() << "SDL audio init failed:" << SDL_GetError();
		return false;
	}

	subsystemInit = true;

	SDL_AudioSpec wanted;
	SDL_AudioSpec obtained;

	SDL_zero(wanted);
	wanted.freq = int(sampleRate);
	wanted.format = AUDIO_S16SYS;
	wanted.channels = 1;
	wanted.samples = Uint16(periodSamples());
	wanted.callback = audioCallback;
	wanted.userdata = this;

	const QByteArray deviceName = device.toUtf8();
	const char *name = deviceName.isEmpty() ? nullptr : deviceName.constData();

	// No allowed changes, SDL converts to whatever the hardware needs
	deviceId = SDL_OpenAudioDevice(name, 0, &wanted, &obtained, 0);

	if (deviceId == 0)
	{
		qWarning() << "SDL_OpenAudioDevice failed:" << SDL_GetError();
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		subsystemInit = false;
		return false;
	}

	SDL_PauseAudioDevice(deviceId, 0);
	return true;
}

void AudioDriverSDL::closeDevice(void)
{
	if (deviceId != 0)
	{
		// Waits for a running callback to return
		SDL_CloseAudioDevice(deviceId);
		deviceId = 0;
	}

	if (subsystemInit)
	{
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		subsystemInit = false;
	}
}

void SDLCALL AudioDriverSDL::audioCallback(void *userData, Uint8 *stream, int length)
{
	static_cast<AudioDriverSDL *>(userData)->fillAudio(reinterpret_cast<int16_t *>(stream), length / int(sizeof(int16_t)));
}
