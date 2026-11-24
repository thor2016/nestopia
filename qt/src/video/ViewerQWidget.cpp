#include "video/ViewerQWidget.h"

#include <QPaintEvent>
#include <QPainter>

ViewerQWidget::ViewerQWidget(const Options &options, QWidget *parent)
	: QWidget(parent),
	  ViewerBase(options)
{
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_NoSystemBackground);
	setMinimumSize(256, 240);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ViewerQWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this);

	painter.fillRect(rect(), Qt::black);

	const QRect target = targetRect(size());

	if (target.isEmpty())
	{
		return;
	}

	painter.setRenderHint(QPainter::SmoothPixmapTransform, linearFilter());
	painter.drawImage(target, frame());
}
