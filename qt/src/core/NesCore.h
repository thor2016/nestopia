#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <QString>

#include "NstApiEmulator.hpp"
#include "NstApiMachine.hpp"
#include "NstApiVideo.hpp"
#include "NstApiSound.hpp"
#include "NstApiInput.hpp"
#include "NstApiCartridge.hpp"
#include "NstApiUser.hpp"

class AudioDriverBase;

// Thin wrapper around the Nestopia core API.
//
// Threading: NesCore owns no lock. Every member function must be called with
// the emulation mutex held (see EmulationThread::mutex()). The Nestopia API
// registers its callbacks in process wide statics, so only one NesCore may
// exist at a time.
class NesCore
{
	public:
		// Nestopia output filters. The NTSC entries select the nes_ntsc
		// filter with the matching signal preset.
		enum class VideoFilter
		{
			None = 0,
			NtscComposite,
			NtscSVideo,
			NtscRgb
		};

		static constexpr int kMaxFrameWidth = Nes::Api::Video::Output::NTSC_WIDTH;
		static constexpr int kFrameHeight = Nes::Api::Video::Output::HEIGHT;
		static constexpr int kPadCount = 4;

		NesCore(void);
		~NesCore(void);

		NesCore(const NesCore &) = delete;
		NesCore &operator=(const NesCore &) = delete;

		// Directory battery backed RAM files are loaded from and saved to.
		void setSaveDirectory(const QString &dir);

		// Loads the cartridge database used to identify images.
		bool loadDatabase(const QString &path);

		// Loads an image, powers the machine on. On failure returns false and
		// fills error.
		bool loadRom(const QString &path, QString &error);
		void unload(void);
		bool isLoaded(void) const;
		QString romName(void) const { return romBaseName; }

		void reset(bool hard);

		// Runs the machine for one video frame. Renders the frame into the
		// internal frame buffer and queues the generated audio on the audio
		// driver, if one is attached. Requires a loaded image.
		void runFrame(void);

		// Frames per second of the loaded image (NTSC or PAL timing), the
		// NTSC rate when nothing is loaded.
		double frameRate(void) const;

		// The audio driver receives the samples of every frame and defines
		// the sample rate of the core. Pass nullptr to run without sound.
		void setAudioDriver(AudioDriverBase *driver);

		bool setVideoFilter(VideoFilter filter);
		VideoFilter videoFilter(void) const { return filter; }

		// Latest completed frame, 32 bit 0x00RRGGBB pixels, frameWidth()
		// pixels per row.
		const uint32_t *frameBuffer(void) const { return frame.data(); }
		int frameWidth(void) const { return width; }
		int frameHeight(void) const { return kFrameHeight; }
		uint64_t frameCount(void) const { return frames; }

		// Sets the NES button bits (Nes::Api::Input::Controllers::Pad) of a port
		void setPadButtons(int port, unsigned int buttons);

	private:
		static void NST_CALLBACK fileIoCallback(void *userData, Nes::Api::User::File &file);
		static void NST_CALLBACK logCallback(void *userData, const char *text, unsigned long length);
		static Nes::Api::User::Answer NST_CALLBACK questionCallback(void *userData, Nes::Api::User::Question question);
		void handleFileIo(Nes::Api::User::File &file);
		QString saveFilePath(const char *extension) const;
		void applyAudioConfig(void);

		Nes::Api::Emulator emulator;
		Nes::Api::Machine machine;
		Nes::Api::Video video;
		Nes::Api::Sound sound;
		Nes::Api::Input input;
		Nes::Api::Cartridge cartridge;
		Nes::Api::Input::Controllers controllers;

		AudioDriverBase *audio;
		VideoFilter filter;
		bool loaded;
		int width;
		uint64_t frames;
		double soundPhase;
		QString saveDir;
		QString romBaseName;
		std::vector<uint32_t> frame;
		std::vector<int16_t> soundBuffer;
};
