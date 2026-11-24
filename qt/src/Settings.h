#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>

#include "audio/AudioDriver.h"
#include "core/NesCore.h"
#include "input/InputManager.h"
#include "video/ViewerInterface.h"

// Persistent application settings
struct Config
{
	ViewerBase::Driver videoDriver = ViewerBase::DriverOpenGL;
	NesCore::VideoFilter videoFilter = NesCore::VideoFilter::None;
	AspectMode aspect = AspectMode::Par8x7;
	bool linearFilter = false;
	bool vsync = true;
	int windowScale = 3;

	AudioDriverBase::Driver audioDriver = AudioDriverBase::DriverSDL;
	unsigned int sampleRate = 48000;
	// 0 - 100
	int volume = 100;
	bool muted = false;
	QString audioDevice;
	int audioBufferMs = 100;

	QString lastRomDirectory;
	QByteArray windowGeometry;

	InputConfig input = InputConfig::defaults();

	void load(QSettings &settings);
	void save(QSettings &settings) const;
};
