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
		void queueRedraw(void) override { update(); }

	protected:
		void paintEvent(QPaintEvent *event) override;
		void optionsChanged(void) override { update(); }
};
