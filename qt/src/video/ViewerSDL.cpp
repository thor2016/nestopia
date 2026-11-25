#include "video/ViewerSDL.h"

#include <QGuiApplication>
#include <QtDebug>

#include "profiler/Profiler.h"

ViewerSDL::ViewerSDL(const Options &options, QWidget *parent)
	: QWidget(parent),
	  ViewerBase(options),
	  window(nullptr),
	  renderer(nullptr),
	  texture(nullptr),
	  textureWidth(0),
	  textureHeight(0),
	  subsystemInit(false)
{
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_NoSystemBackground);
	setMinimumSize(256, 240);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

ViewerSDL::~ViewerSDL(void)
{
	destroyRenderer();

	if (subsystemInit)
	{
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}
}

bool ViewerSDL::init(void)
{
	// A Qt window on Wayland has no X11 window id that SDL could attach to
	if (QGuiApplication::platformName() != QLatin1String("xcb"))
	{
		qWarning() << "SDL viewer: embedding needs the xcb Qt platform, current platform is"
			<< QGuiApplication::platformName();
		return false;
	}

	// Qt runs on X11 even in a Wayland session, SDL must do the same
	SDL_SetHint(SDL_HINT_VIDEODRIVER, "x11");

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
	{
		qWarning() << "SDL viewer: video init failed:" << SDL_GetError();
		return false;
	}

	subsystemInit = true;

	// Make sure the native window exists
	winId();

	return createRenderer();
}

bool ViewerSDL::createRenderer(void)
{
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linearFilter() ? "1" : "0");

	window = SDL_CreateWindowFrom(reinterpret_cast<void *>(winId()));

	if (window == nullptr)
	{
		qWarning() << "SDL viewer: SDL_CreateWindowFrom failed:" << SDL_GetError();
		return false;
	}

	// SDL reads the size of the X window when it attaches, Qt may not have
	// sent its resize to the server yet. Tell SDL the size explicitly.
	const QSize pixelSize = size() * devicePixelRatioF();

	SDL_SetWindowSize(window, pixelSize.width(), pixelSize.height());

	const Uint32 vsync = options().vsync ? SDL_RENDERER_PRESENTVSYNC : 0;

	renderer = SDL_CreateRenderer(window, -1, vsync | SDL_RENDERER_ACCELERATED);

	if (renderer == nullptr)
	{
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	}

	if (renderer == nullptr)
	{
		qWarning() << "SDL viewer: no renderer available:" << SDL_GetError();
		SDL_DestroyWindow(window);
		window = nullptr;
		return false;
	}

	SDL_RendererInfo rendererInfo;

	if (SDL_GetRendererInfo(renderer, &rendererInfo) == 0)
	{
		info = QStringLiteral("SDL %1.%2.%3 (%4 renderer)")
			.arg(SDL_MAJOR_VERSION).arg(SDL_MINOR_VERSION).arg(SDL_PATCHLEVEL)
			.arg(QString::fromLatin1(rendererInfo.name));
	}

	return true;
}

void ViewerSDL::destroyRenderer(void)
{
	if (texture != nullptr)
	{
		SDL_DestroyTexture(texture);
		texture = nullptr;
	}

	if (renderer != nullptr)
	{
		SDL_DestroyRenderer(renderer);
		renderer = nullptr;
	}

	if (window != nullptr)
	{
		// A foreign window is only detached, the X window stays alive
		SDL_DestroyWindow(window);
		window = nullptr;
	}
}

void ViewerSDL::optionsChanged(void)
{
	// The scale quality hint is read when the texture is created
	if (texture != nullptr)
	{
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linearFilter() ? "1" : "0");
		SDL_DestroyTexture(texture);
		texture = nullptr;
		textureWidth = textureHeight = 0;
	}

	update();
}

void ViewerSDL::resizeEvent(QResizeEvent *)
{
	NESTUX_PROFILE_FUNC("Viewer SDL resizeEvent");

	// SDL does not follow the size of a foreign window, start over
	if (window != nullptr)
	{
		destroyRenderer();
		createRenderer();
	}
}

void ViewerSDL::paintEvent(QPaintEvent *)
{
	render();
}

void ViewerSDL::render(void)
{
	NESTUX_PROFILE_FUNC("Viewer SDL render");

	if (renderer == nullptr)
	{
		return;
	}

	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);

	const QImage &image = frame();

	if (!image.isNull())
	{
		if (texture == nullptr || textureWidth != image.width() || textureHeight != image.height())
		{
			if (texture != nullptr)
			{
				SDL_DestroyTexture(texture);
			}

			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linearFilter() ? "1" : "0");
			texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
				image.width(), image.height());
			textureWidth = image.width();
			textureHeight = image.height();
			// A new texture is empty
			SDL_UpdateTexture(texture, nullptr, image.constBits(), int(image.bytesPerLine()));
			clearFrameChanged();
		}
		else if (frameChanged())
		{
			SDL_UpdateTexture(texture, nullptr, image.constBits(), int(image.bytesPerLine()));
			clearFrameChanged();
		}

		int outputWidth = 0;
		int outputHeight = 0;

		SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight);

		const QRect target = targetRect(QSize(outputWidth, outputHeight));

		if (texture != nullptr && !target.isEmpty())
		{
			const SDL_Rect destination = {target.x(), target.y(), target.width(), target.height()};

			SDL_RenderCopy(renderer, texture, nullptr, &destination);
		}
	}

	SDL_RenderPresent(renderer);
}
