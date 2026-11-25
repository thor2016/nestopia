#include "video/ViewerQWidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>

#include "profiler/Profiler.h"

ViewerQWidget::ViewerQWidget(const Options &options, QWidget *parent)
	: QWidget(parent),
	  ViewerBase(options)
{
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_NoSystemBackground);
	setMinimumSize(256, 240);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ViewerQWidget::queueRedraw(void)
{
	const QRect target = targetRect(size());

	if (fullRepaint || target.isEmpty() || target != lastPaintRect)
	{
		update();
	}
	else
	{
		update(target);
	}
}

void ViewerQWidget::resizeEvent(QResizeEvent *event)
{
	NESTOPIAQT_PROFILE_FUNC("Viewer QWidget resizeEvent");

	fullRepaint = true;
	QWidget::resizeEvent(event);
}

void ViewerQWidget::optionsChanged(void)
{
	fullRepaint = true;
	update();
}

void ViewerQWidget::paintEvent(QPaintEvent *event)
{
	NESTOPIAQT_PROFILE_FUNC("Viewer QWidget paintEvent");

	QPainter painter;
	{
		NESTOPIAQT_PROFILE_FUNC("Viewer QWidget painter begin");
		painter.begin(this);
	}

	const QRect target = targetRect(size());

	if (target.isEmpty())
	{
		painter.fillRect(rect(), Qt::black);
		lastPaintRect = QRect();
		return;
	}

	// Only the area around the picture needs the background, the picture covers the rest
	const int viewWidth = width();
	const int viewHeight = height();
	const int right = target.x() + target.width();
	const int bottom = target.y() + target.height();

	{
		NESTOPIAQT_PROFILE_FUNC("Viewer QWidget fill background");
		painter.fillRect(0, 0, target.x(), viewHeight, Qt::black);
		painter.fillRect(right, 0, viewWidth - right, viewHeight, Qt::black);
		painter.fillRect(target.x(), 0, target.width(), target.y(), Qt::black);
		painter.fillRect(target.x(), bottom, target.width(), viewHeight - bottom, Qt::black);
	}

	painter.setRenderHint(QPainter::SmoothPixmapTransform, linearFilter());

	if (linearFilter())
	{
		NESTOPIAQT_PROFILE_FUNC("Viewer QWidget drawImage linear");
		painter.drawImage(target, frame());
	}
	else
	{
		NESTOPIAQT_PROFILE_FUNC("Viewer QWidget drawImage nearest");
		painter.drawImage(target, frame());
	}

	{
		NESTOPIAQT_PROFILE_FUNC("Viewer QWidget painter end");
		painter.end();
	}

	lastPaintRect = target;

	// A partial repaint leaves the area around the picture as it was
	if (event->rect().contains(rect()))
	{
		fullRepaint = false;
	}
}
