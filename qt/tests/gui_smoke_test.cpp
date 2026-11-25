// Builds the main window and the input dialog on the offscreen platform and
// lets them run for a moment. Catches crashes and failures to start, exit code
// 77 skips the test. The OpenGL viewer falls back to QWidget when the
// platform has no GL, which is part of what is tested.
#include <cstdio>

#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QThread>
#include <QTimer>

#include "debugger/DebuggerWindow.h"
#include "input/InputConfigDialog.h"
#include "ui/MainWindow.h"

namespace
{
	bool waitForStatus(QApplication &app, QLabel *status, const QString &prefix, int timeout)
	{
		QElapsedTimer timer;
		timer.start();

		while (!status->text().startsWith(prefix) && timer.elapsed() < timeout)
		{
			app.processEvents();
			QThread::msleep(5);
		}

		return status->text().startsWith(prefix);
	}

	bool waitForNextHalt(QApplication &app, QLabel *status, const QString &previous, int timeout)
	{
		QElapsedTimer timer;
		timer.start();

		while ((status->text() == previous || !status->text().startsWith(QStringLiteral("Halted at "))) &&
			timer.elapsed() < timeout)
		{
			app.processEvents();
			QThread::msleep(5);
		}

		return status->text().startsWith(QStringLiteral("Halted at ")) && status->text() != previous;
	}
}

int main(int argc, char *argv[])
{
	if (argc < 2 || argv[1][0] == '\0')
	{
		std::fprintf(stderr, "No test ROM given, skipping\n");
		return 77;
	}

	// Keep the settings of the user out of the test
	QStandardPaths::setTestModeEnabled(true);

	QApplication app(argc, argv);

	QCoreApplication::setOrganizationName(QStringLiteral("NestopiaQtTest"));
	QCoreApplication::setApplicationName(QStringLiteral("NestopiaQtTest"));

	MainWindow window;

	window.show();

	if (!window.openRom(QString::fromLocal8Bit(argv[1])))
	{
		std::fprintf(stderr, "FAILED: could not open the ROM\n");
		return 1;
	}

	QAction *debuggerAction = window.findChild<QAction *>(QStringLiteral("debuggerAction"));

	if (debuggerAction == nullptr)
	{
		std::fprintf(stderr, "FAILED: debugger menu action is missing\n");
		return 1;
	}

	debuggerAction->trigger();
	QApplication::processEvents();
	DebuggerWindow *debuggerWindow = window.findChild<DebuggerWindow *>(QStringLiteral("debuggerWindow"));

	if (!debuggerAction->isChecked() || debuggerWindow == nullptr || !debuggerWindow->isVisible())
	{
		std::fprintf(stderr, "FAILED: debugger window did not open\n");
		return 1;
	}

	auto *status = debuggerWindow->findChild<QLabel *>(QStringLiteral("debuggerStatus"));
	auto *breakAction = debuggerWindow->findChild<QAction *>(QStringLiteral("debuggerBreakAction"));
	auto *stepAction = debuggerWindow->findChild<QAction *>(QStringLiteral("debuggerStepAction"));
	auto *addBreakpoint = debuggerWindow->findChild<QPushButton *>(QStringLiteral("debuggerAddBreakpoint"));
	auto *removeBreakpoint = debuggerWindow->findChild<QPushButton *>(QStringLiteral("debuggerRemoveBreakpoint"));
	auto *breakpoints = debuggerWindow->findChild<QTableWidget *>(QStringLiteral("debuggerBreakpoints"));
	auto *addBookmark = debuggerWindow->findChild<QPushButton *>(QStringLiteral("debuggerAddBookmark"));
	auto *bookmarks = debuggerWindow->findChild<QListWidget *>(QStringLiteral("debuggerBookmarks"));

	if (status == nullptr || breakAction == nullptr || stepAction == nullptr ||
		addBreakpoint == nullptr || removeBreakpoint == nullptr || breakpoints == nullptr ||
		addBookmark == nullptr || bookmarks == nullptr)
	{
		std::fprintf(stderr, "FAILED: debugger controls are missing\n");
		return 1;
	}

	QTimer::singleShot(100, []() {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (dialog == nullptr)
		{
			return;
		}
		dialog->findChild<QLineEdit *>(QStringLiteral("breakpointDialogAddress"))->setText(QStringLiteral("5000"));
		dialog->accept();
	});
	addBreakpoint->click();

	if (breakpoints->rowCount() != 1)
	{
		std::fprintf(stderr, "FAILED: execute breakpoint was not added\n");
		return 1;
	}

	breakpoints->item(0, 0)->setCheckState(Qt::Unchecked);

	if (breakpoints->rowCount() != 1 || breakpoints->item(0, 0)->checkState() != Qt::Unchecked)
	{
		std::fprintf(stderr, "FAILED: disabled execute breakpoint was not retained\n");
		return 1;
	}

	addBreakpoint->click();
	breakpoints->selectRow(0);
	removeBreakpoint->click();

	if (breakpoints->rowCount() != 0)
	{
		std::fprintf(stderr, "FAILED: execute breakpoint was not removed\n");
		return 1;
	}

	addBookmark->click();

	if (bookmarks->count() != 1)
	{
		std::fprintf(stderr, "FAILED: bookmark was not added\n");
		return 1;
	}

	breakAction->trigger();

	if (!waitForStatus(app, status, QStringLiteral("Halted at "), 3000))
	{
		std::fprintf(stderr, "FAILED: break request did not halt the emulator\n");
		return 1;
	}

	const QString haltedAt = status->text();
	stepAction->trigger();

	if (!waitForNextHalt(app, status, haltedAt, 3000))
	{
		std::fprintf(stderr, "FAILED: step did not stop at the next instruction\n");
		return 1;
	}

	debuggerWindow->reject();
	QApplication::processEvents();

	if (debuggerAction->isChecked())
	{
		std::fprintf(stderr, "FAILED: debugger menu action did not follow the window\n");
		return 1;
	}

	QTimer::singleShot(1000, &app, &QCoreApplication::quit);
	app.exec();

	InputManager input;
	EmulationThread emulation;
	InputConfigDialog dialog(emulation, input, nullptr);

	dialog.show();
	QTimer::singleShot(500, &app, &QCoreApplication::quit);
	app.exec();

	std::puts("OK");
	return 0;
}
