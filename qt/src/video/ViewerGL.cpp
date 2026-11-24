#include "video/ViewerGL.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

ViewerGL::ViewerGL(const Options &options, QWidget *parent)
	: QOpenGLWidget(parent),
	  ViewerBase(options)
{
	QSurfaceFormat format = QSurfaceFormat::defaultFormat();
	format.setSwapInterval(options.vsync ? 1 : 0);
	setFormat(format);

	setMinimumSize(256, 240);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

ViewerGL::~ViewerGL(void)
{
	releaseGL();
}

void ViewerGL::releaseGL(void)
{
	if (!isValid())
	{
		texture.reset();
		return;
	}

	makeCurrent();
	texture.reset();
	blitter.destroy();
	doneCurrent();
}

void ViewerGL::initializeGL(void)
{
	QOpenGLFunctions *functions = context()->functions();

	if (!blitter.create())
	{
		qWarning("OpenGL viewer: texture blitter is not supported");
	}

	info = QStringLiteral("OpenGL %1 (%2)")
		.arg(QString::fromLatin1(reinterpret_cast<const char *>(functions->glGetString(GL_VERSION))))
		.arg(QString::fromLatin1(reinterpret_cast<const char *>(functions->glGetString(GL_RENDERER))));
}

void ViewerGL::paintGL(void)
{
	QOpenGLFunctions *functions = context()->functions();
	const QSize pixelSize = size() * devicePixelRatioF();

	functions->glViewport(0, 0, pixelSize.width(), pixelSize.height());
	functions->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	functions->glClear(GL_COLOR_BUFFER_BIT);

	const QImage &image = frame();

	if (image.isNull() || !blitter.isCreated())
	{
		return;
	}

	bool upload = frameChanged();

	if (!texture || sizeChanged())
	{
		texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
		texture->setSize(image.width(), image.height());
		texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
		texture->setMipLevels(1);
		texture->allocateStorage();
		clearSizeChanged();
		upload = true;
	}

	if (upload)
	{
		// QImage::Format_RGB32 is B, G, R, A in memory on little endian
		texture->setData(QOpenGLTexture::BGRA, QOpenGLTexture::UInt8, image.constBits());
		clearFrameChanged();
	}

	texture->setMinMagFilters(linearFilter() ? QOpenGLTexture::Linear : QOpenGLTexture::Nearest,
		linearFilter() ? QOpenGLTexture::Linear : QOpenGLTexture::Nearest);
	texture->setWrapMode(QOpenGLTexture::ClampToEdge);

	const QRect target = targetRect(pixelSize);

	if (target.isEmpty())
	{
		return;
	}

	blitter.bind();
	blitter.blit(texture->textureId(),
		QOpenGLTextureBlitter::targetTransform(target, QRect(QPoint(0, 0), pixelSize)),
		QOpenGLTextureBlitter::OriginTopLeft);
	blitter.release();
}
