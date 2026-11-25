#pragma once

#include <cstdint>

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

class QWidget;

// How the NES picture is shaped to the window
enum class AspectMode
{
	// 1:1 pixels, 256x240
	Square = 0,
	// NTSC pixel aspect ratio of 8:7, about 4:3.5
	Par8x7,
	// Stretched to 4:3
	Stretch4x3
};

// Common interface of the video backends (OpenGL, SDL, QWidget, Vulkan).
//
// A viewer is only used from the GUI thread. It never touches the emulation
// state: the GUI hands it the frame with setFrame() while holding the
// emulation mutex just long enough to copy the pixels.
class ViewerBase
{
	public:
		enum Driver
		{
			DriverOpenGL = 0,
			DriverSDL,
			DriverQWidget,
			DriverVulkan
		};

		struct Options
		{
			bool linearFilter = false;
			AspectMode aspect = AspectMode::Par8x7;
			// Takes effect when the viewer is created
			bool vsync = true;
		};

		virtual ~ViewerBase(void);

		static const char *driverName(Driver driver);
		// Whether the backend was built in
		static bool isCompiledIn(Driver driver);
		// Creates the viewer, nullptr if the backend is not built in. The
		// viewer still has to be initialized with init().
		static ViewerBase *create(Driver driver, const Options &options, QWidget *parent);

		virtual Driver driver(void) const = 0;
		// Backend and device description, empty while unknown
		virtual QString driverInfo(void) const { return QString(); }
		// The widget to put into the window
		virtual QWidget *widget(void) = 0;
		// Prepares the backend. Returns false if it cannot run here, the
		// viewer must then be destroyed.
		virtual bool init(void) { return true; }
		// Schedules a repaint with the latest frame
		virtual void queueRedraw(void) = 0;

		// Copies a 0x00RRGGBB frame to opaque QImage::Format_ARGB32.
		// width is the row length in pixels.
		void setFrame(const uint32_t *pixels, int width, int height);
		const QImage &frame(void) const { return frameImage; }

		void setLinearFilter(bool linear);
		void setAspectMode(AspectMode mode);
		bool linearFilter(void) const { return viewerOptions.linearFilter; }
		AspectMode aspectMode(void) const { return viewerOptions.aspect; }
		const Options &options(void) const { return viewerOptions; }

		// Where the picture goes in an area of the given size
		QRect targetRect(const QSize &area) const;

	protected:
		explicit ViewerBase(const Options &options);

		// Called after the option changed
		virtual void optionsChanged(void) {}

		bool frameChanged(void) const { return frameDirty; }
		void clearFrameChanged(void) { frameDirty = false; }
		bool sizeChanged(void) const { return frameResized; }
		void clearSizeChanged(void) { frameResized = false; }

	private:
		Options viewerOptions;
		QImage frameImage;
		bool frameDirty;
		bool frameResized;
};
