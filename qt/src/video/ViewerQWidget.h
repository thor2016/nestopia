#pragma once

#include <QWidget>

#include "video/ViewerInterface.h"

// Software viewer, draws the frame with QPainter
class ViewerQWidget : public QWidget, public ViewerBase
{
	Q_OBJECT

	public:
		ViewerQWidget(const Options &options, QWidget *parent);

		Driver driver(void) const override { return DriverQWidget; }
		QString driverInfo(void) const override { return QStringLiteral("QPainter"); }
		QWidget *widget(void) override { return this; }
		void queueRedraw(void) override;

	protected:
		void paintEvent(QPaintEvent *event) override;
		void resizeEvent(QResizeEvent *event) override;
		void optionsChanged(void) override;

	private:
		// A frame normally repaints only the picture. Anything that can change
		// the area around it needs the whole widget repainted.
		QRect lastPaintRect;
		bool fullRepaint = true;
};
