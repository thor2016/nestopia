#include "ui/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QShortcut>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QtDebug>

#include "debugger/DebuggerWindow.h"
#include "input/InputConfigDialog.h"
#include "profiler/Profiler.h"
#include "profiler/ProfilerDialog.h"

#ifndef NESTOPIAQT_DATABASE_FALLBACK
#define NESTOPIAQT_DATABASE_FALLBACK ""
#endif

namespace
{
	constexpr int kBaseWidth = 256;
	constexpr int kBaseHeight = 240;
}

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent),
	  debuggerWindow(nullptr),
	  framesThisSecond(0),
	  shutDown(false),
	  statusRom(nullptr),
	  statusFps(nullptr),
	  statusDrivers(nullptr),
	  closeRomAction(nullptr),
	  pauseAction(nullptr),
	  muteAction(nullptr),
	  fullscreenAction(nullptr),
	  debuggerAction(nullptr),
	  videoDriverGroup(nullptr),
	  audioDriverGroup(nullptr)
{
	config.load(settings);
	input.setConfig(config.input);
	input.joysticks().init();

	const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

	{
		EmulationThread::Locker lock(emulation.mutex());
		NesCore &core = emulation.core();

		core.setSaveDirectory(QDir(dataDir).filePath(QStringLiteral("saves")));

		const QString databases[] = {
			QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("NstDatabase.xml")),
			QStringLiteral(NESTOPIAQT_DATABASE_FALLBACK)
		};

		bool databaseLoaded = false;

		for (const QString &path : databases)
		{
			if (!path.isEmpty() && core.loadDatabase(path))
			{
				databaseLoaded = true;
				break;
			}
		}

		if (!databaseLoaded)
		{
			qWarning() << "Cartridge database NstDatabase.xml not found, images are not identified";
		}

		core.setVideoFilter(config.videoFilter);
	}

	emulation.setInputManager(&input);

	setAcceptDrops(true);
	setWindowTitle(QStringLiteral("NestopiaQt"));

	statusRom = new QLabel(this);
	statusFps = new QLabel(this);
	statusDrivers = new QLabel(this);
	statusBar()->addWidget(statusRom, 1);
	statusBar()->addPermanentWidget(statusFps);
	statusBar()->addPermanentWidget(statusDrivers);

	createMenus();

	// The viewer is created first so the window has its final content
	ViewerBase::Driver driver = config.videoDriver;

	if (!installViewer(driver))
	{
		const ViewerBase::Driver fallbacks[] = {ViewerBase::DriverOpenGL, ViewerBase::DriverQWidget};

		for (ViewerBase::Driver candidate : fallbacks)
		{
			if (candidate != driver && installViewer(candidate))
			{
				statusBar()->showMessage(tr("%1 video driver is not available, using %2")
					.arg(QLatin1String(ViewerBase::driverName(driver)))
					.arg(QLatin1String(ViewerBase::driverName(candidate))), 8000);
				config.videoDriver = candidate;
				break;
			}
		}
	}

	startAudio();

	connect(&emulation, &EmulationThread::frameFinished, this, &MainWindow::onFrameFinished, Qt::QueuedConnection);
	connect(&fpsTimer, &QTimer::timeout, this, &MainWindow::onFpsTimer);
	fpsTimer.start(1000);

	qApp->installEventFilter(this);

	if (!config.windowGeometry.isEmpty())
	{
		restoreGeometry(config.windowGeometry);
	}
	else
	{
		applyWindowScale();
	}

	updateTitle();
	updateStatus();

	emulation.start();
}

MainWindow::~MainWindow(void)
{
	shutdown();
}

void MainWindow::shutdown(void)
{
	if (shutDown)
	{
		return;
	}

	shutDown = true;

	if (debuggerWindow != nullptr && debuggerWindow->isVisible())
	{
		debuggerWindow->close();
	}

	delete debuggerWindow;
	debuggerWindow = nullptr;

	// Stops a running capture and writes its footer
	delete profilerDialog;

	// No more frames, no more audio writes
	emulation.stop();

	{
		EmulationThread::Locker lock(emulation.mutex());
		emulation.core().setAudioDriver(nullptr);
		emulation.core().unload();
	}

	audio.reset();
	input.joysticks().shutdown();
}

void MainWindow::saveConfig(void)
{
	if (!isFullScreen())
	{
		config.windowGeometry = saveGeometry();
	}

	{
		EmulationThread::Locker lock(emulation.mutex());
		config.input = input.config();
	}

	config.save(settings);
	settings.sync();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	saveConfig();
	shutdown();
	QMainWindow::closeEvent(event);
}

// --------------------------------------------------------------------------
// Menus
// --------------------------------------------------------------------------

QAction *MainWindow::addCheckable(QMenu *menu, const QString &text, bool checked)
{
	QAction *action = menu->addAction(text);

	action->setCheckable(true);
	action->setChecked(checked);
	return action;
}

void MainWindow::connectChoice(QActionGroup *group, QMenu *menu, const QString &text, int value, bool checked,
	std::function<void(int)> apply)
{
	QAction *action = menu->addAction(text);

	action->setCheckable(true);
	action->setChecked(checked);
	action->setData(value);
	group->addAction(action);

	connect(action, &QAction::triggered, this, [apply, value]() { apply(value); });
}

void MainWindow::createMenus(void)
{
	// ---- File
	QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

	QAction *openAction = fileMenu->addAction(tr("&Open ROM..."), this, &MainWindow::onOpenRom);
	openAction->setShortcut(QKeySequence::Open);

	closeRomAction = fileMenu->addAction(tr("&Close ROM"), this, &MainWindow::onCloseRom);
	closeRomAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
	closeRomAction->setEnabled(false);

	fileMenu->addSeparator();

	QAction *quitAction = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
	quitAction->setShortcut(QKeySequence::Quit);

	// ---- Emulation
	QMenu *emulationMenu = menuBar()->addMenu(tr("&Emulation"));

	pauseAction = addCheckable(emulationMenu, tr("&Pause"), false);
	pauseAction->setShortcuts({QKeySequence(Qt::Key_Pause), QKeySequence(Qt::CTRL | Qt::Key_P)});
	connect(pauseAction, &QAction::triggered, this, [this](bool checked) { setPaused(checked); });

	QAction *softReset = emulationMenu->addAction(tr("&Reset"), this, [this]() { reset(false); });
	softReset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));

	QAction *hardReset = emulationMenu->addAction(tr("&Power Cycle"), this, [this]() { reset(true); });
	hardReset->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));

	// ---- Debug
	QMenu *debugMenu = menuBar()->addMenu(tr("&Debug"));
	debuggerAction = addCheckable(debugMenu, tr("&Debugger"), false);
	debuggerAction->setObjectName(QStringLiteral("debuggerAction"));
	debuggerAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
	connect(debuggerAction, &QAction::toggled, this, &MainWindow::setDebuggerVisible);

	QAction *profilerAction = debugMenu->addAction(tr("&Profiler..."), this, &MainWindow::onProfiler);
	profilerAction->setObjectName(QStringLiteral("profilerAction"));
	profilerAction->setStatusTip(tr("Capture profiler markers from all threads to a file and view captures."));

	// ---- View
	QMenu *viewMenu = menuBar()->addMenu(tr("&View"));

	QMenu *sizeMenu = viewMenu->addMenu(tr("Window &Size"));
	QActionGroup *sizeGroup = new QActionGroup(this);

	for (int scale = 1; scale <= 5; ++scale)
	{
		connectChoice(sizeGroup, sizeMenu, tr("%1x").arg(scale), scale, scale == config.windowScale,
			[this](int value) {
				config.windowScale = value;
				if (isFullScreen())
				{
					setFullscreen(false);
				}
				applyWindowScale();
			});
	}

	QMenu *aspectMenu = viewMenu->addMenu(tr("&Aspect Ratio"));
	QActionGroup *aspectGroup = new QActionGroup(this);
	const std::pair<const char *, AspectMode> aspects[] = {
		{"Square pixels", AspectMode::Square},
		{"NTSC pixels (8:7)", AspectMode::Par8x7},
		{"4:3", AspectMode::Stretch4x3}
	};

	for (const auto &entry : aspects)
	{
		connectChoice(aspectGroup, aspectMenu, tr(entry.first), int(entry.second), config.aspect == entry.second,
			[this](int value) {
				config.aspect = AspectMode(value);
				viewer->setAspectMode(config.aspect);
			});
	}

	QAction *linear = addCheckable(viewMenu, tr("&Linear Filtering"), config.linearFilter);
	connect(linear, &QAction::triggered, this, [this](bool checked) {
		config.linearFilter = checked;
		viewer->setLinearFilter(checked);
	});

	viewMenu->addSeparator();

	fullscreenAction = addCheckable(viewMenu, tr("&Fullscreen"), false);
	fullscreenAction->setShortcuts({QKeySequence(Qt::Key_F11), QKeySequence(Qt::ALT | Qt::Key_Return)});
	connect(fullscreenAction, &QAction::triggered, this, [this](bool checked) { setFullscreen(checked); });

	QShortcut *leaveFullscreen = new QShortcut(QKeySequence(Qt::Key_Escape), this);
	connect(leaveFullscreen, &QShortcut::activated, this, [this]() {
		if (isFullScreen())
		{
			setFullscreen(false);
		}
	});

	// ---- Options
	QMenu *optionsMenu = menuBar()->addMenu(tr("&Options"));

	optionsMenu->addAction(tr("&Input..."), this, &MainWindow::onInputConfig);

	QMenu *videoMenu = optionsMenu->addMenu(tr("&Video"));

	QMenu *videoDriverMenu = videoMenu->addMenu(tr("&Driver"));
	videoDriverGroup = new QActionGroup(this);

	for (int d = ViewerBase::DriverOpenGL; d <= ViewerBase::DriverVulkan; ++d)
	{
		const ViewerBase::Driver driver = ViewerBase::Driver(d);

		connectChoice(videoDriverGroup, videoDriverMenu, QLatin1String(ViewerBase::driverName(driver)), d,
			config.videoDriver == driver, [this](int value) { selectVideoDriver(ViewerBase::Driver(value)); });

		videoDriverGroup->actions().last()->setEnabled(ViewerBase::isCompiledIn(driver));
	}

	QMenu *filterMenu = videoMenu->addMenu(tr("&Filter"));
	QActionGroup *filterGroup = new QActionGroup(this);
	const std::pair<const char *, NesCore::VideoFilter> filters[] = {
		{"None", NesCore::VideoFilter::None},
		{"NTSC composite", NesCore::VideoFilter::NtscComposite},
		{"NTSC S-Video", NesCore::VideoFilter::NtscSVideo},
		{"NTSC RGB", NesCore::VideoFilter::NtscRgb}
	};

	for (const auto &entry : filters)
	{
		connectChoice(filterGroup, filterMenu, tr(entry.first), int(entry.second), config.videoFilter == entry.second,
			[this](int value) {
				config.videoFilter = NesCore::VideoFilter(value);

				{
					EmulationThread::HaltGuard guard(emulation);
					EmulationThread::Locker lock(emulation.mutex());
					emulation.core().setVideoFilter(config.videoFilter);
				}

				pushFrameToViewer();
			});
	}

	QAction *vsync = addCheckable(videoMenu, tr("V&Sync"), config.vsync);
	connect(vsync, &QAction::triggered, this, [this](bool checked) {
		config.vsync = checked;
		// Applied when the viewer is created
		installViewer(config.videoDriver);
	});

	QMenu *audioMenu = optionsMenu->addMenu(tr("&Audio"));

	QMenu *audioDriverMenu = audioMenu->addMenu(tr("&Driver"));
	audioDriverGroup = new QActionGroup(this);

	for (int d = AudioDriverBase::DriverSDL; d <= AudioDriverBase::DriverQt; ++d)
	{
		const AudioDriverBase::Driver driver = AudioDriverBase::Driver(d);

		connectChoice(audioDriverGroup, audioDriverMenu, QLatin1String(AudioDriverBase::driverName(driver)), d,
			config.audioDriver == driver, [this](int value) {
				config.audioDriver = AudioDriverBase::Driver(value);
				startAudio();
				updateStatus();
			});
	}

	QMenu *rateMenu = audioMenu->addMenu(tr("&Sample Rate"));
	QActionGroup *rateGroup = new QActionGroup(this);

	for (unsigned int rate : {44100u, 48000u, 96000u})
	{
		connectChoice(rateGroup, rateMenu, tr("%1 Hz").arg(rate), int(rate), config.sampleRate == rate,
			[this](int value) {
				config.sampleRate = unsigned(value);
				startAudio();
			});
	}

	QMenu *volumeMenu = audioMenu->addMenu(tr("&Volume"));
	QActionGroup *volumeGroup = new QActionGroup(this);

	for (int volume : {25, 50, 75, 100})
	{
		connectChoice(volumeGroup, volumeMenu, tr("%1%").arg(volume), volume, config.volume == volume,
			[this](int value) {
				config.volume = value;

				if (audio)
				{
					audio->setVolume(float(value) / 100.0f);
				}
			});
	}

	muteAction = addCheckable(audioMenu, tr("&Mute"), config.muted);
	muteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
	connect(muteAction, &QAction::triggered, this, [this](bool checked) {
		config.muted = checked;

		if (audio)
		{
			audio->setMuted(checked);
		}
	});

	// ---- Help
	QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));

	helpMenu->addAction(tr("&About"), this, &MainWindow::onAbout);

	// Shortcuts of a hidden menu bar do not work, in fullscreen it is hidden
	addActions({openAction, closeRomAction, quitAction, pauseAction, softReset, hardReset, debuggerAction,
		fullscreenAction, muteAction});
}

void MainWindow::setDebuggerVisible(bool visible)
{
	if (visible)
	{
		if (debuggerWindow == nullptr)
		{
			debuggerWindow = new DebuggerWindow(emulation, this);
			connect(debuggerWindow, &DebuggerWindow::closed, this, [this]() {
				QSignalBlocker blocker(debuggerAction);
				debuggerAction->setChecked(false);
			});
		}

		if (emulation.isPaused())
		{
			setPaused(false);
		}

		debuggerWindow->show();
		debuggerWindow->raise();
		debuggerWindow->activateWindow();
	}
	else if (debuggerWindow != nullptr && debuggerWindow->isVisible())
	{
		debuggerWindow->close();
	}
}

void MainWindow::onProfiler(void)
{
	if (profilerDialog == nullptr)
	{
		// Deletes itself when closed
		profilerDialog = new ProfilerDialog(this);
	}

	profilerDialog->show();
	profilerDialog->raise();
	profilerDialog->activateWindow();
}

// --------------------------------------------------------------------------
// Video
// --------------------------------------------------------------------------

bool MainWindow::installViewer(ViewerBase::Driver driver)
{
	ViewerBase::Options options;

	options.linearFilter = config.linearFilter;
	options.aspect = config.aspect;
	options.vsync = config.vsync;

	std::unique_ptr<ViewerBase> created(ViewerBase::create(driver, options, this));

	if (!created || !created->init() || created->widget() == nullptr)
	{
		return false;
	}

	// takeCentralWidget() releases the old widget so the viewer can destroy it
	takeCentralWidget();
	viewer = std::move(created);
	setCentralWidget(viewer->widget());
	viewer->widget()->setFocus();

	pushFrameToViewer();
	viewer->queueRedraw();
	updateStatus();
	return true;
}

void MainWindow::selectVideoDriver(ViewerBase::Driver driver)
{
	if (installViewer(driver))
	{
		config.videoDriver = driver;
		return;
	}

	statusBar()->showMessage(tr("%1 video driver is not available").arg(QLatin1String(ViewerBase::driverName(driver))), 8000);

	// Put the check mark back on the driver that is still in use
	for (QAction *action : videoDriverGroup->actions())
	{
		action->setChecked(action->data().toInt() == int(config.videoDriver));
	}
}

void MainWindow::pushFrameToViewer(void)
{
	NESTOPIAQT_PROFILE_FUNC("Push Frame To Viewer");

	if (!viewer)
	{
		return;
	}

	NESTOPIAQT_PROFILE_FUNC("Push Frame Lock And Copy");
	EmulationThread::Locker lock(emulation.mutex());
	const NesCore &core = emulation.core();

	if (core.isLoaded())
	{
		viewer->setFrame(core.frameBuffer(), core.frameWidth(), core.frameHeight());
	}
}

void MainWindow::applyWindowScale(void)
{
	double widthFactor = 1.0;

	switch (config.aspect)
	{
		case AspectMode::Square:     break;
		case AspectMode::Par8x7:     widthFactor = 8.0 / 7.0; break;
		case AspectMode::Stretch4x3: widthFactor = (4.0 / 3.0) * kBaseHeight / kBaseWidth; break;
	}

	const int width = int(kBaseWidth * widthFactor * config.windowScale + 0.5);
	const int height = kBaseHeight * config.windowScale + menuBar()->sizeHint().height() + statusBar()->sizeHint().height();

	resize(width, height);
}

void MainWindow::setFullscreen(bool fullscreen)
{
	if (fullscreen == isFullScreen())
	{
		return;
	}

	menuBar()->setVisible(!fullscreen);
	statusBar()->setVisible(!fullscreen);
	fullscreenAction->setChecked(fullscreen);

	if (fullscreen)
	{
		showFullScreen();
	}
	else
	{
		showNormal();
	}
}

// --------------------------------------------------------------------------
// Audio
// --------------------------------------------------------------------------

void MainWindow::stopAudio(void)
{
	{
		EmulationThread::HaltGuard guard(emulation);
		EmulationThread::Locker lock(emulation.mutex());
		emulation.core().setAudioDriver(nullptr);
	}

	audio.reset();
}

bool MainWindow::startAudio(void)
{
	stopAudio();

	AudioDriverBase::Options options;

	options.sampleRate = config.sampleRate;
	options.bufferMs = config.audioBufferMs;
	options.device = config.audioDevice;

	std::unique_ptr<AudioDriverBase> created(AudioDriverBase::create(config.audioDriver));

	if (!created || !created->init(options))
	{
		statusBar()->showMessage(tr("%1 audio driver failed to start, sound is off")
			.arg(QLatin1String(AudioDriverBase::driverName(config.audioDriver))), 8000);
		updateStatus();
		return false;
	}

	created->setVolume(float(config.volume) / 100.0f);
	created->setMuted(config.muted);
	created->setPaused(emulation.isPaused());

	{
		EmulationThread::HaltGuard guard(emulation);
		EmulationThread::Locker lock(emulation.mutex());
		emulation.core().setAudioDriver(created.get());
	}

	audio = std::move(created);
	updateStatus();
	return true;
}

// --------------------------------------------------------------------------
// Emulation control
// --------------------------------------------------------------------------

void MainWindow::setPaused(bool paused)
{
	if (paused)
	{
		emulation.setPaused(true);

		if (audio)
		{
			audio->setPaused(true);
		}
	}
	else
	{
		// The audio FIFO is primed again before the first frame arrives
		if (audio)
		{
			audio->setPaused(false);
		}

		emulation.setPaused(false);
	}

	pauseAction->setChecked(paused);
	updateStatus();
}

void MainWindow::reset(bool hard)
{
	{
		EmulationThread::HaltGuard guard(emulation);
		EmulationThread::Locker lock(emulation.mutex());
		emulation.core().reset(hard);
	}
	if (debuggerWindow != nullptr)
	{
		debuggerWindow->machineChanged();
	}
}

bool MainWindow::openRom(const QString &path)
{
	QString error;
	bool loaded;

	{
		EmulationThread::HaltGuard guard(emulation);
		EmulationThread::Locker lock(emulation.mutex());

		loaded = emulation.core().loadRom(path, error);
		emulation.wake();
	}

	if (!loaded)
	{
		QMessageBox::critical(this, tr("Open ROM"), tr("Unable to load %1:\n%2").arg(QDir::toNativeSeparators(path), error));
		updateTitle();
		return false;
	}

	config.lastRomDirectory = QFileInfo(path).absolutePath();

	// Start with an empty audio FIFO
	if (audio)
	{
		audio->setPaused(true);
		audio->setPaused(emulation.isPaused());
	}

	updateTitle();
	updateStatus();
	return true;
}

void MainWindow::onOpenRom(void)
{
	const QString path = QFileDialog::getOpenFileName(this, tr("Open ROM"), config.lastRomDirectory,
		tr("NES ROMs (*.nes *.unf *.unif);;All Files (*)"));

	if (!path.isEmpty())
	{
		openRom(path);
	}
}

void MainWindow::onCloseRom(void)
{
	{
		EmulationThread::HaltGuard guard(emulation);
		EmulationThread::Locker lock(emulation.mutex());
		emulation.core().unload();
	}

	// Show black instead of the last frame
	static const std::vector<uint32_t> blank(size_t(NesCore::kMaxFrameWidth) * NesCore::kFrameHeight, 0);

	viewer->setFrame(blank.data(), kBaseWidth, kBaseHeight);
	viewer->queueRedraw();

	updateTitle();
}

void MainWindow::updateTitle(void)
{
	QString rom;
	bool loaded;

	{
		EmulationThread::Locker lock(emulation.mutex());
		loaded = emulation.core().isLoaded();
		rom = emulation.core().romName();
	}

	closeRomAction->setEnabled(loaded);
	statusRom->setText(loaded ? rom : tr("No ROM loaded"));
	setWindowTitle(loaded ? QStringLiteral("%1 - NestopiaQt").arg(rom) : QStringLiteral("NestopiaQt"));
	if (debuggerWindow != nullptr)
	{
		debuggerWindow->machineChanged();
	}
}

void MainWindow::updateStatus(void)
{
	const QString video = viewer ? viewer->driverInfo() : QString();
	QString sound = audio ? audio->driverInfo() : tr("off");

	if (audio)
	{
		const AudioDriverBase::Stats stats = audio->stats();

		sound += tr(", buffer %1%, %2 underruns, %3 overruns")
			.arg(stats.fifoSize ? 100u * stats.fifoUsed / stats.fifoSize : 0u)
			.arg(stats.underruns)
			.arg(stats.overruns);
	}

	statusDrivers->setText(tr("%1 | Audio: %2").arg(
		viewer ? QLatin1String(ViewerBase::driverName(viewer->driver())) : QLatin1String("no video"),
		audio ? QLatin1String(AudioDriverBase::driverName(audio->driver())) : tr("off")));
	statusDrivers->setToolTip(tr("Video: %1\nAudio: %2").arg(video, sound));

	if (emulation.isPaused())
	{
		statusFps->setText(tr("Paused"));
	}
}

void MainWindow::onFrameFinished(quint64)
{
	NESTOPIAQT_PROFILE_FUNC("Frame Finished Handler");

	++framesThisSecond;

	if (!viewer)
	{
		return;
	}

	pushFrameToViewer();
	viewer->queueRedraw();
}

void MainWindow::onFpsTimer(void)
{
	if (!emulation.isPaused())
	{
		statusFps->setText(tr("%1 fps").arg(framesThisSecond));
	}

	framesThisSecond = 0;
	// Device names of some drivers are only known once they are running
	updateStatus();
}

void MainWindow::onInputConfig(void)
{
	InputConfigDialog dialog(emulation, input, this);

	{
		EmulationThread::Locker lock(emulation.mutex());
		input.releaseAllKeys();
	}

	if (dialog.exec() == QDialog::Accepted)
	{
		EmulationThread::Locker lock(emulation.mutex());

		input.setConfig(dialog.result());
		config.input = dialog.result();
	}

	EmulationThread::Locker lock(emulation.mutex());
	input.releaseAllKeys();
}

void MainWindow::onAbout(void)
{
	QMessageBox::about(this, tr("About NestopiaQt"),
		tr("<b>NestopiaQt</b><br>"
		   "A Qt and SDL2 shell around the Nestopia core.<br><br>"
		   "Nestopia is free software licensed under the GNU General Public License, version 2 or later."));
}

// --------------------------------------------------------------------------
// Events
// --------------------------------------------------------------------------

bool MainWindow::eventFilter(QObject *, QEvent *event)
{
	switch (event->type())
	{
		case QEvent::KeyPress:
		{
			const QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

			// Dialogs and menus use the keyboard for themselves
			if (!keyEvent->isAutoRepeat() && isActiveWindow() &&
				QApplication::activeModalWidget() == nullptr && QApplication::activePopupWidget() == nullptr)
			{
				EmulationThread::Locker lock(emulation.mutex());
				input.keyEvent(keyEvent->key(), true);
			}

			break;
		}
		case QEvent::KeyRelease:
		{
			const QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

			if (!keyEvent->isAutoRepeat())
			{
				EmulationThread::Locker lock(emulation.mutex());
				input.keyEvent(keyEvent->key(), false);
			}

			break;
		}
		default:
			break;
	}

	return false;
}

void MainWindow::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::ActivationChange && !isActiveWindow())
	{
		// Releases that happen in another window are not seen
		EmulationThread::Locker lock(emulation.mutex());
		input.releaseAllKeys();
	}

	QMainWindow::changeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls())
	{
		event->acceptProposedAction();
	}
}

void MainWindow::dropEvent(QDropEvent *event)
{
	const QList<QUrl> urls = event->mimeData()->urls();

	if (!urls.isEmpty() && urls.first().isLocalFile())
	{
		openRom(urls.first().toLocalFile());
	}
}
