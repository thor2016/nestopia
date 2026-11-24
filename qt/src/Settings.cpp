#include "Settings.h"

#include <algorithm>

namespace
{
	template<typename Enum>
	Enum enumValue(QSettings &settings, const QString &key, Enum fallback, int maximum)
	{
		const int value = settings.value(key, int(fallback)).toInt();

		return (value >= 0 && value <= maximum) ? Enum(value) : fallback;
	}
}

void Config::load(QSettings &settings)
{
	videoDriver = enumValue(settings, QStringLiteral("Video/Driver"), videoDriver, ViewerBase::DriverVulkan);
	videoFilter = enumValue(settings, QStringLiteral("Video/Filter"), videoFilter, int(NesCore::VideoFilter::NtscRgb));
	aspect = enumValue(settings, QStringLiteral("Video/Aspect"), aspect, int(AspectMode::Stretch4x3));
	linearFilter = settings.value(QStringLiteral("Video/LinearFilter"), linearFilter).toBool();
	vsync = settings.value(QStringLiteral("Video/VSync"), vsync).toBool();
	windowScale = std::clamp(settings.value(QStringLiteral("Video/WindowScale"), windowScale).toInt(), 1, 8);

	audioDriver = enumValue(settings, QStringLiteral("Audio/Driver"), audioDriver, AudioDriverBase::DriverQt);
	sampleRate = unsigned(std::clamp(settings.value(QStringLiteral("Audio/SampleRate"), int(sampleRate)).toInt(), 8000, 192000));
	volume = std::clamp(settings.value(QStringLiteral("Audio/Volume"), volume).toInt(), 0, 100);
	muted = settings.value(QStringLiteral("Audio/Muted"), muted).toBool();
	audioDevice = settings.value(QStringLiteral("Audio/Device"), audioDevice).toString();
	audioBufferMs = std::clamp(settings.value(QStringLiteral("Audio/BufferMs"), audioBufferMs).toInt(), 20, 500);

	lastRomDirectory = settings.value(QStringLiteral("Application/LastRomDirectory")).toString();
	windowGeometry = settings.value(QStringLiteral("Application/WindowGeometry")).toByteArray();

	input.load(settings);
}

void Config::save(QSettings &settings) const
{
	settings.setValue(QStringLiteral("Video/Driver"), int(videoDriver));
	settings.setValue(QStringLiteral("Video/Filter"), int(videoFilter));
	settings.setValue(QStringLiteral("Video/Aspect"), int(aspect));
	settings.setValue(QStringLiteral("Video/LinearFilter"), linearFilter);
	settings.setValue(QStringLiteral("Video/VSync"), vsync);
	settings.setValue(QStringLiteral("Video/WindowScale"), windowScale);

	settings.setValue(QStringLiteral("Audio/Driver"), int(audioDriver));
	settings.setValue(QStringLiteral("Audio/SampleRate"), int(sampleRate));
	settings.setValue(QStringLiteral("Audio/Volume"), volume);
	settings.setValue(QStringLiteral("Audio/Muted"), muted);
	settings.setValue(QStringLiteral("Audio/Device"), audioDevice);
	settings.setValue(QStringLiteral("Audio/BufferMs"), audioBufferMs);

	settings.setValue(QStringLiteral("Application/LastRomDirectory"), lastRomDirectory);
	settings.setValue(QStringLiteral("Application/WindowGeometry"), windowGeometry);

	input.save(settings);
}
