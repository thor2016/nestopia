#include <algorithm>
#include <cstdio>
#include <functional>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolBar>

#include "debugger/DebuggerTabs.h"
#include "debugger/DebuggerWindow.h"
#include "debugger/DisassemblyView.h"

namespace
{
	int failures = 0;

	void check(bool condition, const char *description)
	{
		if (!condition)
		{
			std::fprintf(stderr, "FAIL: %s\n", description);
			++failures;
		}
	}

	bool waitUntil(const std::function<bool()> &condition)
	{
		QElapsedTimer timer;
		timer.start();
		while (!condition() && timer.elapsed() < 3000)
		{
			QApplication::processEvents();
			QThread::msleep(2);
		}
		return condition();
	}

	QByteArray program(void)
	{
		QByteArray rom(16 + 0x8000 + 0x2000, '\0');
		const unsigned char header[] = {'N', 'E', 'S', 0x1A, 2, 1};
		std::copy(std::begin(header), std::end(header), rom.data());
		char *prg = rom.data() + 16;
		std::fill(prg, prg + 0x8000, char(0xEA));
		const unsigned char main[] = {0x78, 0xA2, 0xFF, 0x9A, 0x20, 0x10, 0x80, 0x4C, 0x04, 0x80};
		const unsigned char sub[] = {0xA9, 0x01, 0x60};
		std::copy(std::begin(main), std::end(main), prg);
		std::copy(std::begin(sub), std::end(sub), prg + 0x10);
		prg[0x0FFF] = char(0xAD);
		prg[0x1000] = char(0xA9);
		prg[0x1001] = char(0xEA);
		for (int vector : {0x7FFA, 0x7FFC, 0x7FFE})
		{
			prg[vector] = 0;
			prg[vector + 1] = char(0x80);
		}
		return rom;
	}

	void mouse(DisassemblyView *view, QEvent::Type type, QPoint position)
	{
		QMouseEvent event(type, position, view->viewport()->mapToGlobal(position),
			Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view->viewport(), &event);
	}

	void key(DisassemblyView *view, int value, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		QKeyEvent event(QEvent::KeyPress, value, modifiers);
		QApplication::sendEvent(view, &event);
	}

	Nes::Api::Debugger::Registers registers(EmulationThread &thread)
	{
		EmulationThread::Locker lock(thread.mutex());
		Nes::Api::Debugger::Registers value;
		thread.core().debugger().GetRegisters(value);
		return value;
	}
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QTemporaryDir settingsDir;
	if (!settingsDir.isValid()) return 1;
	QCoreApplication::setOrganizationName(QStringLiteral("NestopiaQtWidgetTest"));
	QCoreApplication::setApplicationName(QStringLiteral("Debugger"));
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
	EmulationThread thread;
	QString error;
	{
		EmulationThread::Locker lock(thread.mutex());
		if (!thread.core().loadRomData(program(), QStringLiteral("widget-test"), error))
		{
			std::fprintf(stderr, "Unable to load program: %s\n", qPrintable(error));
			return 1;
		}
	}
	int halts = 0;
	QObject::connect(&thread, &EmulationThread::halted, &app, [&halts](quint16, int) { ++halts; });
	{
		DebuggerWindow window(thread);
		window.show();
		QApplication::processEvents();
		auto *view = window.findChild<DisassemblyView *>();
		auto *toolbar = window.findChild<QToolBar *>();
		auto *menuBar = window.findChild<QMenuBar *>();
		check(menuBar->isVisible() && menuBar->actions().size() == 4, "dialog-local menus are visible");
		check(toolbar->toolButtonStyle() == Qt::ToolButtonIconOnly, "toolbar is icon-only");
		for (QAction *action : toolbar->actions())
		{
			if (!action->isSeparator()) check(!action->icon().isNull(), "each toolbar command has an icon");
		}
		for (int preset = 1; preset <= 4; ++preset)
		{
			window.findChild<QAction *>(QStringLiteral("debuggerLayout%1").arg(preset))->trigger();
			QApplication::processEvents();
			int visible = 0;
			QSet<QWidget *> pages;
			for (auto *tabs : window.findChildren<DebuggerTabs *>())
			{
				visible += tabs->isVisible() ? 1 : 0;
				for (int index = 0; index < tabs->count(); ++index) pages.insert(tabs->widget(index));
			}
			check(visible == preset && pages.size() == 4, "layout preset preserves each panel exactly once");
		}
		auto *address = window.findChild<QLineEdit *>(QStringLiteral("debuggerAddress"));
		auto *add = window.findChild<QPushButton *>(QStringLiteral("debuggerAddBreakpoint"));
		auto *breakpoints = window.findChild<QTableWidget *>(QStringLiteral("debuggerBreakpoints"));
		address->setText(QStringLiteral("$8004"));
		add->click();
		check(breakpoints->rowCount() == 1, "breakpoint is added before execution");
		thread.start();
		if (!waitUntil([&]() { return halts == 1; }))
		{
			check(false, "generated program hits its breakpoint");
			thread.stop();
			return 1;
		}
		check(registers(thread).pc == 0x8004, "breakpoint halts at JSR");
		breakpoints->item(0, 0)->setCheckState(Qt::Unchecked);
		check(breakpoints->rowCount() == 1, "disabled breakpoint remains in the list");
		const int cw = view->fontMetrics().horizontalAdvance(QLatin1Char('0'));
		view->setPCPlacement(false);
		view->goToAddress(0x8004);
		key(view, Qt::Key_Down);
		check(view->topAddress() == 0x8007, "down scrolls by one complete instruction");
		view->goToAddress(0x8004);
		mouse(view, QEvent::MouseButtonDblClick, QPoint(25 * cw, 5));
		check(view->selectedAddress() == 0x8010, "double-click follows the JSR operand");
		check(registers(thread).pc == 0x8004 && halts == 1, "following an operand never executes code");
		view->navigateBack();
		check(view->selectedAddress() == 0x8004, "back restores navigation address");
		view->navigateForward();
		check(view->selectedAddress() == 0x8010, "forward restores navigation address");
		view->goToAddress(0x8004);
		mouse(view, QEvent::MouseButtonPress, QPoint(cw, 5));
		check(breakpoints->item(0, 0)->checkState() == Qt::Checked, "gutter click enables an execute breakpoint");
		mouse(view, QEvent::MouseButtonPress, QPoint(cw, 5));
		check(breakpoints->item(0, 0)->checkState() == Qt::Unchecked, "gutter click disables without deleting");
		key(view, Qt::Key_C, Qt::ControlModifier);
		check(QApplication::clipboard()->text().contains(QStringLiteral("JSR $8010")), "copy exports assembly text");
		view->goToAddress(0x9000);
		key(view, Qt::Key_Down);
		key(view, Qt::Key_Up);
		check(view->topAddress() == 0x9000, "backward scroll reuses known instruction boundaries");
		const uint16_t top = view->topAddress();
		QElapsedTimer timer;
		timer.start();
		while (timer.elapsed() < 450) QApplication::processEvents();
		check(view->topAddress() == top && view->selectedAddress() == 0x9000,
			"periodic refresh does not reset assembly browsing");
		auto *a = window.findChild<QLineEdit *>(QStringLiteral("debuggerRegisterA"));
		a->setText(QStringLiteral("42"));
		a->setModified(true);
		a->editingFinished();
		check(registers(thread).a == 0x42, "hex register edit updates the stopped CPU");
		auto *carry = window.findChild<QCheckBox *>(QStringLiteral("debuggerFlagC"));
		carry->setChecked(true);
		check((registers(thread).p & 1) != 0, "status checkbox updates the CPU flag");
		a->setFocus();
		a->setText(QStringLiteral("43"));
		a->setModified(true);
		window.findChild<QAction *>(QStringLiteral("debuggerStepAction"))->trigger();
		check(waitUntil([&]() { return halts == 2; }), "icon/menu action steps into JSR");
		check(registers(thread).pc == 0x8010 && view->selectedAddress() == 0x8010,
			"new halt follows the real PC");
		check(registers(thread).a == 0x43 && a->text() == QStringLiteral("43"),
			"stepping commits a focused register edit before executing");
		view->goToAddress(0x2000);
		check(view->selectedText().contains(QStringLiteral("???")), "unreadable I/O remains unknown");
		view->goToPC();

		auto *source = window.findChild<DebuggerTabs *>(QStringLiteral("debuggerTabs00"));
		auto *destination = window.findChild<DebuggerTabs *>(QStringLiteral("debuggerTabs13"));
		QWidget *cpuPanel = source->currentWidget();
		QTimer::singleShot(0, &window, []() {
			auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
			if (menu == nullptr) return;
			QMenu *move = menu->actions().first()->menu();
			QMenu *lower = move->actions().last()->menu();
			lower->actions().last()->trigger();
			menu->close();
		});
		QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(8, 8), source->mapToGlobal(QPoint(8, 8)));
		QApplication::sendEvent(source, &context);
		check(destination->indexOf(cpuPanel) >= 0 && source->count() == 0,
			"Move Tab To context menu moves a panel into an empty group");
		if (argc > 1)
		{
			window.findChild<QAction *>(QStringLiteral("debuggerLayout4"))->trigger();
			QApplication::processEvents();
			check(window.grab().save(QString::fromLocal8Bit(argv[1])), "capture debugger appearance");
			// Keep a custom arrangement for the restore check.
			destination->moveRequested(cpuPanel, 1, 3);
		}
		window.close();
		check(!thread.isDebuggerEnabled() && !thread.isHalted(), "closing disables debugging and releases a halt");
	}
	{
		DebuggerWindow restored(thread);
		auto *group = restored.findChild<DebuggerTabs *>(QStringLiteral("debuggerTabs13"));
		check(group->count() == 1 && group->widget(0)->objectName() == QStringLiteral("debuggerPanel0"),
			"custom panel placement is restored in a new debugger window");
		restored.show();
		restored.reject();
		check(!thread.isDebuggerEnabled(), "Escape/reject disables the debugger");
	}
	thread.stop();
	return failures == 0 ? 0 : 1;
}
