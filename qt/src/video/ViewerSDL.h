#pragma once

#include <SDL.h>

#include <QWidget>

#include "video/ViewerInterface.h"

// SDL viewer. Embeds an SDL renderer in a native child window of the Qt
// window with SDL_CreateWindowFrom(), which only works on X11. On other
// platforms init() fails and the caller is expected to fall back.
class ViewerSDL : public QWidget, public ViewerBase
{
	Q_OBJECT

	public:
		ViewerSDL(const Options &options, QWidget *parent);
		~ViewerSDL(void) override;

		Driver driver(void) const override { return DriverSDL; }
		QString driverInfo(void) const override { return info; }
		QWidget *widget(void) override { return this; }
		bool init(void) override;
		void queueRedraw(void) override { update(); }

		QPaintEngine *paintEngine(void) const override { return nullptr; }

	protected:
		void paintEvent(QPaintEvent *event) override;
		void resizeEvent(QResizeEvent *event) override;
		void optionsChanged(void) override;

	private:
		bool createRenderer(void);
		void destroyRenderer(void);
		void render(void);

		SDL_Window *window;
		SDL_Renderer *renderer;
		SDL_Texture *texture;
		int textureWidth;
		int textureHeight;
		bool subsystemInit;
		QString info;
};
