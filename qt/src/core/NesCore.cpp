#include "core/NesCore.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtDebug>

#include "audio/AudioDriver.h"

namespace
{
	using Nes::Api::Machine;
	using Nes::Api::Video;
	using Nes::Api::User;

	constexpr double kNtscFps = double(Machine::CLK_NTSC_DOT) / double(Machine::CLK_NTSC_VSYNC);
	constexpr double kPalFps = double(Machine::CLK_PAL_DOT) / double(Machine::CLK_PAL_VSYNC);

	QString describe(Nes::Result result)
	{
		switch (result)
		{
			case Nes::RESULT_ERR_WRONG_MODE:               return QStringLiteral("NTSC/PAL region mismatch");
			case Nes::RESULT_ERR_MISSING_BIOS:             return QStringLiteral("Missing BIOS image");
			case Nes::RESULT_ERR_UNSUPPORTED_MAPPER:       return QStringLiteral("Unsupported or malformed mapper");
			case Nes::RESULT_ERR_UNSUPPORTED_VSSYSTEM:     return QStringLiteral("Vs. DualSystem is not supported");
			case Nes::RESULT_ERR_UNSUPPORTED_FILE_VERSION: return QStringLiteral("File format version is not supported");
			case Nes::RESULT_ERR_UNSUPPORTED:              return QStringLiteral("Unsupported operation");
			case Nes::RESULT_ERR_INVALID_CRC:              return QStringLiteral("Invalid CRC checksum");
			case Nes::RESULT_ERR_CORRUPT_FILE:             return QStringLiteral("Corrupt file");
			case Nes::RESULT_ERR_INVALID_FILE:             return QStringLiteral("Not a valid NES image");
			case Nes::RESULT_ERR_INVALID_PARAM:            return QStringLiteral("Invalid parameter");
			case Nes::RESULT_ERR_NOT_READY:                return QStringLiteral("System not ready");
			case Nes::RESULT_ERR_OUT_OF_MEMORY:            return QStringLiteral("Out of memory");
			default:                                       return QStringLiteral("Error %1").arg(int(result));
		}
	}
}

NesCore::NesCore(void)
	: emulator(),
	  machine(emulator),
	  video(emulator),
	  sound(emulator),
	  input(emulator),
	  cartridge(emulator),
	  audio(nullptr),
	  filter(VideoFilter::None),
	  loaded(false),
	  width(Video::Output::WIDTH),
	  frames(0),
	  soundPhase(0.0),
	  frame(size_t(kMaxFrameWidth) * kFrameHeight, 0)
{
	User::fileIoCallback.Set(fileIoCallback, this);
	User::logCallback.Set(logCallback, this);
	User::questionCallback.Set(questionCallback, this);

	setVideoFilter(VideoFilter::None);
}

NesCore::~NesCore(void)
{
	unload();

	User::fileIoCallback.Set(nullptr, nullptr);
	User::logCallback.Set(nullptr, nullptr);
	User::questionCallback.Set(nullptr, nullptr);
}

void NesCore::setSaveDirectory(const QString &dir)
{
	saveDir = dir;
}

bool NesCore::loadDatabase(const QString &path)
{
	std::ifstream stream(path.toStdString().c_str(), std::ios::in | std::ios::binary);

	if (!stream.is_open())
	{
		return false;
	}

	Nes::Api::Cartridge::Database database(cartridge.GetDatabase());

	if (NES_FAILED(database.Load(stream)))
	{
		return false;
	}

	database.Enable(true);
	return true;
}

bool NesCore::loadRom(const QString &path, QString &error)
{
	QFile file(path);

	if (!file.open(QIODevice::ReadOnly))
	{
		error = file.errorString();
		return false;
	}

	const QByteArray data = file.readAll();
	file.close();

	unload();

	std::istringstream stream(std::string(data.constData(), size_t(data.size())), std::ios::in | std::ios::binary);

	// The file callbacks derive the save file name from this
	romBaseName = QFileInfo(path).completeBaseName();

	const Nes::Result result = machine.Load(stream, Machine::FAVORED_NES_NTSC, Machine::DONT_ASK_PROFILE);

	if (NES_FAILED(result))
	{
		romBaseName.clear();
		error = describe(result);
		return false;
	}

	input.AutoSelectControllers();
	input.AutoSelectAdapter();

	setVideoFilter(filter);
	applyAudioConfig();

	if (NES_FAILED(machine.Power(true)))
	{
		machine.Unload();
		romBaseName.clear();
		error = QStringLiteral("Failed to power on the machine");
		return false;
	}

	loaded = true;
	soundPhase = 0.0;
	frames = 0;
	return true;
}

void NesCore::unload(void)
{
	if (!loaded)
	{
		return;
	}

	machine.Power(false);
	machine.Unload();
	loaded = false;
	romBaseName.clear();
}

bool NesCore::isLoaded(void) const
{
	return loaded;
}

void NesCore::reset(bool hard)
{
	if (loaded)
	{
		machine.Reset(hard);
	}
}

double NesCore::frameRate(void) const
{
	return (loaded && machine.GetMode() == Machine::PAL) ? kPalFps : kNtscFps;
}

void NesCore::setAudioDriver(AudioDriverBase *driver)
{
	audio = driver;
	soundPhase = 0.0;
	applyAudioConfig();
}

void NesCore::applyAudioConfig(void)
{
	if (audio != nullptr)
	{
		sound.SetSampleRate(audio->sampleRate());
		sound.SetVolume(Nes::Api::Sound::ALL_CHANNELS, Nes::Api::Sound::DEFAULT_VOLUME);
		sound.SetSpeed(Nes::Api::Sound::DEFAULT_SPEED);
	}
}

bool NesCore::setVideoFilter(VideoFilter newFilter)
{
	Video::RenderState state;
	const bool ntsc = newFilter != VideoFilter::None;

	state.filter = ntsc ? Video::RenderState::FILTER_NTSC : Video::RenderState::FILTER_NONE;
	state.width = ntsc ? Video::Output::NTSC_WIDTH : Video::Output::WIDTH;
	state.height = Video::Output::HEIGHT;
	state.bits.count = 32;
	state.bits.mask.r = 0xFF0000;
	state.bits.mask.g = 0x00FF00;
	state.bits.mask.b = 0x0000FF;

	if (NES_FAILED(video.SetRenderState(state)))
	{
		return false;
	}

	switch (newFilter)
	{
		case VideoFilter::NtscComposite:
			video.SetSaturation(Video::DEFAULT_SATURATION_COMP);
			video.SetSharpness(Video::DEFAULT_SHARPNESS_COMP);
			video.SetColorResolution(Video::DEFAULT_COLOR_RESOLUTION_COMP);
			video.SetColorBleed(Video::DEFAULT_COLOR_BLEED_COMP);
			video.SetColorArtifacts(Video::DEFAULT_COLOR_ARTIFACTS_COMP);
			video.SetColorFringing(Video::DEFAULT_COLOR_FRINGING_COMP);
			break;
		case VideoFilter::NtscSVideo:
			video.SetSaturation(Video::DEFAULT_SATURATION_SVIDEO);
			video.SetSharpness(Video::DEFAULT_SHARPNESS_SVIDEO);
			video.SetColorResolution(Video::DEFAULT_COLOR_RESOLUTION_SVIDEO);
			video.SetColorBleed(Video::DEFAULT_COLOR_BLEED_SVIDEO);
			video.SetColorArtifacts(Video::DEFAULT_COLOR_ARTIFACTS_SVIDEO);
			video.SetColorFringing(Video::DEFAULT_COLOR_FRINGING_SVIDEO);
			break;
		case VideoFilter::NtscRgb:
			video.SetSaturation(Video::DEFAULT_SATURATION_RGB);
			video.SetSharpness(Video::DEFAULT_SHARPNESS_RGB);
			video.SetColorResolution(Video::DEFAULT_COLOR_RESOLUTION_RGB);
			video.SetColorBleed(Video::DEFAULT_COLOR_BLEED_RGB);
			video.SetColorArtifacts(Video::DEFAULT_COLOR_ARTIFACTS_RGB);
			video.SetColorFringing(Video::DEFAULT_COLOR_FRINGING_RGB);
			break;
		case VideoFilter::None:
			break;
	}

	filter = newFilter;
	width = state.width;
	std::fill(frame.begin(), frame.end(), 0u);
	return true;
}

void NesCore::setPadButtons(int port, unsigned int buttons)
{
	if (port >= 0 && port < kPadCount)
	{
		controllers.pad[port].buttons = buttons;
	}
}

void NesCore::runFrame(void)
{
	Video::Output videoOutput(frame.data(), long(width) * long(sizeof(uint32_t)));
	Nes::Api::Sound::Output soundOutput;
	int sampleCount = 0;

	if (audio != nullptr)
	{
		// A frame spans a fractional number of samples, carry the remainder
		soundPhase += double(audio->sampleRate()) / frameRate();
		sampleCount = int(soundPhase);
		soundPhase -= sampleCount;
		sampleCount = std::min(sampleCount, int(Nes::Api::Sound::Output::MAX_LENGTH));

		if (soundBuffer.size() < size_t(sampleCount))
		{
			soundBuffer.resize(size_t(sampleCount));
		}

		soundOutput = Nes::Api::Sound::Output(soundBuffer.data(), uint(sampleCount));
	}

	emulator.Execute(&videoOutput, audio != nullptr ? &soundOutput : nullptr, &controllers);
	++frames;

	if (audio != nullptr)
	{
		audio->write(soundBuffer.data(), sampleCount);
	}
}

QString NesCore::saveFilePath(const char *extension) const
{
	return QDir(saveDir).filePath(romBaseName + QString::fromLatin1(extension));
}

void NesCore::handleFileIo(User::File &file)
{
	const char *extension = nullptr;
	bool load = false;

	switch (file.GetAction())
	{
		case User::File::LOAD_BATTERY:    extension = ".sav"; load = true; break;
		case User::File::SAVE_BATTERY:    extension = ".sav"; break;
		case User::File::LOAD_EEPROM:     extension = ".eep"; load = true; break;
		case User::File::SAVE_EEPROM:     extension = ".eep"; break;
		case User::File::LOAD_TURBOFILE:  extension = ".tfl"; load = true; break;
		case User::File::SAVE_TURBOFILE:  extension = ".tfl"; break;
		default:
			return;
	}

	if (romBaseName.isEmpty() || saveDir.isEmpty())
	{
		return;
	}

	QFile saveFile(saveFilePath(extension));

	if (load)
	{
		if (saveFile.open(QIODevice::ReadOnly))
		{
			const QByteArray data = saveFile.readAll();
			file.SetContent(data.constData(), ulong(data.size()));
		}
	}
	else
	{
		const void *data = nullptr;
		ulong size = 0;

		if (NES_SUCCEEDED(file.GetContent(data, size)) && data != nullptr && size != 0)
		{
			QDir().mkpath(saveDir);

			if (saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
			{
				saveFile.write(static_cast<const char *>(data), qint64(size));
			}
			else
			{
				qWarning() << "Unable to write" << saveFile.fileName() << saveFile.errorString();
			}
		}
	}
}

void NST_CALLBACK NesCore::fileIoCallback(void *userData, User::File &file)
{
	static_cast<NesCore *>(userData)->handleFileIo(file);
}

void NST_CALLBACK NesCore::logCallback(void *, const char *text, unsigned long length)
{
	qInfo().noquote() << "[nestopia]" << QString::fromLatin1(text, int(length)).trimmed();
}

User::Answer NST_CALLBACK NesCore::questionCallback(void *, User::Question)
{
	return User::ANSWER_YES;
}
