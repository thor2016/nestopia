#pragma once

#include <QTabWidget>

class DebuggerTabs : public QTabWidget
{
	Q_OBJECT

	public:
		explicit DebuggerTabs(QWidget *parent = nullptr);

	signals:
		void moveRequested(QWidget *page, int column, int row);
		void arrangementChanged(void);

	protected:
		bool eventFilter(QObject *watched, QEvent *event) override;
		void dragEnterEvent(QDragEnterEvent *event) override;
		void dropEvent(QDropEvent *event) override;
		void contextMenuEvent(QContextMenuEvent *event) override;

	private:
		QPoint dragStart;
};
