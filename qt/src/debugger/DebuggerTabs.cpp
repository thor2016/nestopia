#include "debugger/DebuggerTabs.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QTabBar>

namespace
{
	constexpr const char *tabMimeType = "application/x-nestopia-debugger-tab";
}

DebuggerTabs::DebuggerTabs(QWidget *parent)
	: QTabWidget(parent)
{
	setMovable(true);
	setAcceptDrops(true);
	tabBar()->installEventFilter(this);
	connect(tabBar(), &QTabBar::tabMoved, this, &DebuggerTabs::arrangementChanged);
}

bool DebuggerTabs::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == tabBar())
	{
		if (event->type() == QEvent::MouseButtonPress)
		{
			dragStart = static_cast<QMouseEvent *>(event)->position().toPoint();
		}
		else if (event->type() == QEvent::MouseMove)
		{
			auto *mouse = static_cast<QMouseEvent *>(event);
			const QPoint position = mouse->position().toPoint();
			if ((mouse->buttons() & Qt::LeftButton) && !tabBar()->rect().contains(position) &&
				(position - dragStart).manhattanLength() >= QApplication::startDragDistance() && currentWidget() != nullptr)
			{
				QDrag drag(this);
				auto *mime = new QMimeData;
				mime->setData(tabMimeType, currentWidget()->objectName().toUtf8());
				drag.setMimeData(mime);
				// QDrag consumes the release; end QTabBar's internal reorder first.
				QMouseEvent release(QEvent::MouseButtonRelease, mouse->position(), mouse->globalPosition(),
					Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
				QApplication::sendEvent(tabBar(), &release);
				drag.exec(Qt::MoveAction);
				return true;
			}
		}
	}
	return QTabWidget::eventFilter(watched, event);
}

void DebuggerTabs::dragEnterEvent(QDragEnterEvent *event)
{
	auto *source = qobject_cast<DebuggerTabs *>(event->source());
	if (source != nullptr && source != this && source->window() == window() &&
		event->mimeData()->hasFormat(tabMimeType))
	{
		event->acceptProposedAction();
	}
}

void DebuggerTabs::dropEvent(QDropEvent *event)
{
	auto *source = qobject_cast<DebuggerTabs *>(event->source());
	if (source == nullptr || source == this || source->window() != window() ||
		!event->mimeData()->hasFormat(tabMimeType))
	{
		return;
	}
	const QString name = QString::fromUtf8(event->mimeData()->data(tabMimeType));
	for (int index = 0; index < source->count(); ++index)
	{
		QWidget *page = source->widget(index);
		if (page->objectName() == name)
		{
			const QString label = source->tabText(index);
			source->removeTab(index);
			setCurrentIndex(addTab(page, label));
			emit arrangementChanged();
			event->acceptProposedAction();
			break;
		}
	}
}

void DebuggerTabs::contextMenuEvent(QContextMenuEvent *event)
{
	const int index = tabBar()->tabAt(tabBar()->mapFrom(this, event->pos()));
	if (index >= 0)
	{
		setCurrentIndex(index);
	}
	QWidget *page = currentWidget();
	if (page == nullptr)
	{
		return;
	}
	QMenu menu(this);
	QMenu *moveMenu = menu.addMenu(tr("Move Tab To..."));
	const QString rows[] = {tr("Upper"), tr("Mid-Upper"), tr("Mid-Lower"), tr("Lower")};
	for (int row = 0; row < 4; ++row)
	{
		QMenu *rowMenu = moveMenu->addMenu(rows[row]);
		for (int column = 0; column < 2; ++column)
		{
			rowMenu->addAction(column == 0 ? tr("Left") : tr("Right"), this,
				[this, page, column, row]() { emit moveRequested(page, column, row); });
		}
	}
	menu.exec(event->globalPos());
}
