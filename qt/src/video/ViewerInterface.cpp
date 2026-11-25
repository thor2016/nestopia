#include "video/ViewerInterface.h"

#include <algorithm>
#include <cstring>

#include "core/NesCore.h"
#include "video/ViewerGL.h"
#include "video/ViewerQWidget.h"
#include "video/ViewerSDL.h"
#ifdef NESTUX_HAVE_VULKAN
#include "video/ViewerVulkan.h"
#endif

ViewerBase::ViewerBase(const Options &options)
	: viewerOptions(options),
	  frameDirty(false),
	  frameResized(false)
{
}

ViewerBase::~ViewerBase(void)
{
}

const char *ViewerBase::driverName(Driver driver)
{
	switch (driver)
	{
		case DriverOpenGL:  return "OpenGL";
		case DriverSDL:     return "SDL";
		case DriverQWidget: return "QWidget";
		case DriverVulkan:  return "Vulkan";
	}

	return "";
}

bool ViewerBase::isCompiledIn(Driver driver)
{
	switch (driver)
	{
		case DriverOpenGL:
		case DriverSDL:
		case DriverQWidget:
			return true;
		case DriverVulkan:
#ifdef NESTUX_HAVE_VULKAN
			return true;
#else
			return false;
#endif
	}

	return false;
}

ViewerBase *ViewerBase::create(Driver driver, const Options &options, QWidget *parent)
{
	switch (driver)
	{
		case DriverOpenGL:
			return new ViewerGL(options, parent);
		case DriverSDL:
			return new ViewerSDL(options, parent);
		case DriverQWidget:
			return new ViewerQWidget(options, parent);
		case DriverVulkan:
#ifdef NESTUX_HAVE_VULKAN
			return new ViewerVulkan(options, parent);
#else
			return nullptr;
#endif
	}

	return nullptr;
}

void ViewerBase::setFrame(const uint32_t *pixels, int width, int height)
{
	if (pixels == nullptr || width <= 0 || height <= 0)
	{
		return;
	}

	if (frameImage.width() != width || frameImage.height() != height)
	{
		frameImage = QImage(width, height, QImage::Format_ARGB32);
		frameResized = true;
	}

	// The core leaves the alpha byte unset
	uint32_t *dest = reinterpret_cast<uint32_t *>(frameImage.bits());
	const size_t count = size_t(width) * size_t(height);

	for (size_t i = 0; i < count; ++i)
	{
		dest[i] = pixels[i] | 0xFF000000u;
	}

	frameDirty = true;
}

void ViewerBase::setLinearFilter(bool linear)
{
	if (viewerOptions.linearFilter != linear)
	{
		viewerOptions.linearFilter = linear;
		optionsChanged();
	}
}

void ViewerBase::setAspectMode(AspectMode mode)
{
	if (viewerOptions.aspect != mode)
	{
		viewerOptions.aspect = mode;
		optionsChanged();
	}
}

QRect ViewerBase::targetRect(const QSize &area) const
{
	if (frameImage.isNull() || area.isEmpty())
	{
		return QRect();
	}

	// The NTSC filter output is already widened, the NES picture is 256 wide
	const double sourceWidth = frameImage.width() == NesCore::kMaxFrameWidth ? 256.0 : double(frameImage.width());
	const double sourceHeight = double(frameImage.height());
	double aspect = sourceWidth / sourceHeight;

	switch (viewerOptions.aspect)
	{
		case AspectMode::Square:     break;
		case AspectMode::Par8x7:     aspect *= 8.0 / 7.0; break;
		case AspectMode::Stretch4x3: aspect = 4.0 / 3.0; break;
	}

	int width = area.width();
	int height = int(double(width) / aspect + 0.5);

	if (height > area.height())
	{
		height = area.height();
		width = int(double(height) * aspect + 0.5);
	}

	width = std::min(width, area.width());

	return QRect((area.width() - width) / 2, (area.height() - height) / 2, width, height);
}
