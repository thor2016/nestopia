// Builds the main window and the input dialog on the offscreen platform and
// lets them run for a moment. Catches crashes and failures to start, exit code
// 77 skips the test. The OpenGL viewer falls back to QWidget when the
// platform has no GL, which is part of what is tested.
#include <cstdio>

#include <QApplication>
#include <QStandardPaths>
#include <QTimer>

#include "input/InputConfigDialog.h"
#include "ui/MainWindow.h"

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
