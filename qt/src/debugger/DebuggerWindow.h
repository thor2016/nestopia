#pragma once

#include <array>

#include <QDialog>
#include <QList>
#include <QMap>

#include "core/EmulationThread.h"

class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenuBar;
class QPlainTextEdit;
class QShowEvent;
class QSplitter;
class QTableWidget;
class QTableWidgetItem;
class QTimer;
class QToolBar;
class DebuggerTabs;
class DisassemblyView;

class DebuggerWindow : public QDialog
{
	Q_OBJECT

	public:
		explicit DebuggerWindow(EmulationThread &emulation, QWidget *parent = nullptr);
		~DebuggerWindow(void) override;
		void machineChanged(void);

	signals:
		void closed(void);

	protected:
		void showEvent(QShowEvent *event) override;

	private:
		using Request = Nes::Api::Debugger::Request;
		using Registers = Nes::Api::Debugger::Registers;

		void buildMenus(QMenuBar *menuBar, QToolBar *toolbar);
		void buildPanels(void);
		void setLayoutPreset(int preset);
		void movePanel(QWidget *panel, int column, int row);
		void updateTabVisibility(void);
		void saveLayout(void);
		void restoreLayout(void);
		void refresh(void);
		void resume(Request::Mode mode);
		void runToAddress(uint address);
		void goToAddress(void);
		bool applyRegister(int index);
		bool commitRegisters(void);
		void applyFlag(int bit, bool checked);
		void addBreakpoint(void);
		void editBreakpoint(void);
		void setBreakpoint(uint address, bool enabled);
		void openBreakpointDialog(uint address, bool editing);
		Nes::Result applyBreakpoint(uint address, bool enabled, const QString &condition);
		void removeBreakpoint(void);
		void addBookmark(void);
		void addBookmarkAt(uint address);
		void removeBookmark(void);
		void onBreakpointChanged(QTableWidgetItem *item);
		void setStatus(const QString &message);
		void updateBreakpoints(void);
		void updateBookmarks(void);

		EmulationThread &emulation;
		QTimer *refreshTimer;
		QToolBar *toolbar;
		QSplitter *horizontal;
		std::array<QSplitter *, 2> columns;
		std::array<std::array<DebuggerTabs *, 4>, 2> tabs;
		std::array<QWidget *, 4> panels;
		std::array<QAction *, 4> presetActions;
		QList<QAction *> haltActions;
		QAction *breakAction;
		DisassemblyView *disassembly;
		QTableWidget *breakpointTable;
		QListWidget *bookmarkList;
		QPlainTextEdit *stackText;
		std::array<QLineEdit *, 6> registerEdits;
		std::array<QCheckBox *, 8> flagEdits;
		std::array<QLineEdit *, 11> ppuFields;
		QLineEdit *cyclesField;
		QLineEdit *instructionsField;
		QLabel *statusLabel;
		QLabel *messageLabel;
		QMap<uint, bool> breakpoints;
		QMap<uint, QString> conditions;
		QMap<uint, QString> bookmarks;
		bool updating = false;
		bool snapshotDirty = true;
		bool followPC = true;
		bool hadImage = false;
		int layoutPreset = 2;
};
