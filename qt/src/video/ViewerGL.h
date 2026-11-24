#pragma once

#include <memory>

#include <QOpenGLTexture>
#include <QOpenGLTextureBlitter>
#include <QOpenGLWidget>

#include "video/ViewerInterface.h"

// OpenGL viewer, uploads the frame to a texture and blits it
class ViewerGL : public QOpenGLWidget, public ViewerBase
{
	Q_OBJECT

	public:
		ViewerGL(const Options &options, QWidget *parent);
		~ViewerGL(void) override;

		Driver driver(void) const override { return DriverOpenGL; }
		QString driverInfo(void) const override { return info; }
		QWidget *widget(void) override { return this; }
		void queueRedraw(void) override { update(); }

	protected:
		void initializeGL(void) override;
		void paintGL(void) override;
		void optionsChanged(void) override { update(); }

	private:
		void releaseGL(void);

		QOpenGLTextureBlitter blitter;
		std::unique_ptr<QOpenGLTexture> texture;
		QString info;
};
