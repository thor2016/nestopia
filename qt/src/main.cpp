#include <QApplication>
#include <QCommandLineParser>
#include <QSurfaceFormat>

#include <SDL.h>

#include "profiler/GuiBusyProfiler.h"
#include "profiler/Profiler.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
	// QOpenGLWidget needs shared contexts when the widget is reparented
	QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

	// Qt handles the signals
	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

	QApplication app(argc, argv);

	NESTOPIAQT_PROFILER_SET_THREAD_NAME("GUI");

#ifdef NESTOPIAQT_PROFILER_ENABLE
	GuiBusyProfiler guiBusyProfiler;

	app.installEventFilter(&guiBusyProfiler);
#endif

	QCoreApplication::setOrganizationName(QStringLiteral("NestopiaQt"));
	QCoreApplication::setApplicationName(QStringLiteral("NestopiaQt"));
	QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

	QCommandLineParser parser;

	parser.setApplicationDescription(QStringLiteral("Qt shell for the Nestopia NES emulator core"));
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument(QStringLiteral("rom"), QStringLiteral("NES ROM to open"));
	parser.process(app);

	MainWindow window;

	window.show();

	const QStringList arguments = parser.positionalArguments();

	if (!arguments.isEmpty())
	{
		window.openRom(arguments.first());
	}

	return app.exec();
}
