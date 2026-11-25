#include "profiler/ProfilerDialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
	// Magnifying glass with a plus or minus inside the lens, drawn in the given color
	QIcon makeZoomIcon(const QColor &color, bool zoomIn)
	{
		const int size = 64;
		QPixmap pm(size, size);

		pm.fill(Qt::transparent);

		QPainter p(&pm);

		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(color, 7, Qt::SolidLine, Qt::RoundCap));
		p.drawEllipse(QPointF(27, 27), 20, 20);
		p.setPen(QPen(color, 6, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(QPointF(43, 43), QPointF(58, 58));
		p.setPen(QPen(color, 7, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(QPointF(17, 27), QPointF(37, 27));

		if (zoomIn)
		{
			p.drawLine(QPointF(27, 17), QPointF(27, 37));
		}

		p.end();
		return QIcon(pm);
	}

	// The marker the emulator places around each emulated frame (NesCore::runFrame)
	const QLatin1String emulatorFrameComment("Emulate Single Frame");

	const QLatin1String captureFilter("Profiler Captures (*.nstprof);;All Files (*)");
}

ProfilerDialog::ProfilerDialog(QWidget *parent)
	: QDialog(parent, Qt::Window)
{
	QVBoxLayout *mainLayout = new QVBoxLayout();
	QVBoxLayout *vbox;
	QHBoxLayout *hbox;
	QGroupBox *captureBox, *viewBox;
	QPushButton *closeBtn;
	QSettings settings;

	setObjectName(QStringLiteral("profilerDialog"));
	setWindowTitle(tr("Profiler - NestopiaQt"));
	resize(1100, 640);

	const QString defaultPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
		.filePath(QStringLiteral("profile.nstprof"));

	// Capture controls
	captureBox = new QGroupBox(tr("Capture"));
	vbox = new QVBoxLayout();
	captureBox->setLayout(vbox);

	hbox = new QHBoxLayout();
	hbox->addWidget(new QLabel(tr("Capture File:")));
	pathEdit = new QLineEdit(settings.value(QStringLiteral("profiler/capturePath"), defaultPath).toString());
	pathEdit->setObjectName(QStringLiteral("profilerPath"));
	hbox->addWidget(pathEdit, 1);
	browseBtn = new QPushButton(tr("Browse..."));
	hbox->addWidget(browseBtn);
	startStopBtn = new QPushButton(tr("Start Capture"));
	startStopBtn->setObjectName(QStringLiteral("profilerStartStop"));
	hbox->addWidget(startStopBtn);
	vbox->addLayout(hbox);

	statusLabel = new QLabel(tr("Idle"));
	statusLabel->setObjectName(QStringLiteral("profilerStatus"));
	vbox->addWidget(statusLabel);
	mainLayout->addWidget(captureBox);

	// Viewer
	viewBox = new QGroupBox(tr("Viewer"));
	vbox = new QVBoxLayout();
	viewBox->setLayout(vbox);

	hbox = new QHBoxLayout();
	openBtn = new QPushButton(tr("Open Capture..."));
	loadLabel = new QLabel(tr("No capture loaded"));
	loadLabel->setObjectName(QStringLiteral("profilerLoadLabel"));
	hbox->addWidget(openBtn);
	hbox->addWidget(loadLabel, 1);
	vbox->addLayout(hbox);

	viewer = new ProfilerViewer();
	vbox->addWidget(viewer, 1);

	hbox = new QHBoxLayout();
	QPushButton *zoomInBtn = new QPushButton(tr("Zoom In"));
	QPushButton *zoomOutBtn = new QPushButton(tr("Zoom Out"));
	QPushButton *zoomFitBtn = new QPushButton(tr("Fit"));
	hbox->addWidget(zoomInBtn);
	hbox->addWidget(zoomOutBtn);
	hbox->addWidget(zoomFitBtn);
	hbox->addSpacing(16);
	hbox->addWidget(new QLabel(tr("Frame Marker:")));
	frameCombo = new QComboBox();
	frameCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	hbox->addWidget(frameCombo);
	hbox->addWidget(new QLabel(tr("Frame:")));
	frameSpin = new QSpinBox();
	hbox->addWidget(frameSpin);
	framePrevBtn = new QPushButton(tr("Prev"));
	frameNextBtn = new QPushButton(tr("Next"));
	hbox->addWidget(framePrevBtn);
	hbox->addWidget(frameNextBtn);
	hbox->addStretch(1);
	vbox->addLayout(hbox);

	frameLabel = new QLabel();
	vbox->addWidget(frameLabel);

	hbox = new QHBoxLayout();
	hbox->addWidget(new QLabel(tr("Find Marker:")));
	findEdit = new QLineEdit();
	findEdit->setPlaceholderText(tr("name or function (empty matches all)"));
	findEdit->setClearButtonEnabled(true);
	hbox->addWidget(findEdit, 1);
	QPushButton *findFirstBtn = new QPushButton(tr("First"));
	QPushButton *findPrevBtn = new QPushButton(tr("Prev"));
	QPushButton *findNextBtn = new QPushButton(tr("Next"));
	QPushButton *findLastBtn = new QPushButton(tr("Last"));
	QPushButton *longestBtn = new QPushButton(tr("Longest"));
	QPushButton *nextLongestBtn = new QPushButton(tr("Next Longest"));
	hbox->addWidget(findFirstBtn);
	hbox->addWidget(findPrevBtn);
	hbox->addWidget(findNextBtn);
	hbox->addWidget(findLastBtn);
	hbox->addSpacing(16);
	hbox->addWidget(longestBtn);
	hbox->addWidget(nextLongestBtn);
	vbox->addLayout(hbox);

	findLabel = new QLabel();
	vbox->addWidget(findLabel);
	mainLayout->addWidget(viewBox, 1);

	hbox = new QHBoxLayout();
	closeBtn = new QPushButton(tr("Close"));
	hbox->addStretch(1);
	hbox->addWidget(closeBtn);
	mainLayout->addLayout(hbox);

	setLayout(mainLayout);

	// Prefer theme icons and fall back to the style's standard icons
	auto icon = [this](const char *theme, QStyle::StandardPixmap fallback)
	{
		return QIcon::fromTheme(QString::fromLatin1(theme), style()->standardIcon(fallback));
	};
	const QIcon zoomInIcon = makeZoomIcon(palette().color(QPalette::ButtonText), true);
	const QIcon zoomOutIcon = makeZoomIcon(palette().color(QPalette::ButtonText), false);

	QMenuBar *menuBar = new QMenuBar(this);

	menuBar->setNativeMenuBar(false);

	QMenu *menu = menuBar->addMenu(tr("&File"));
	QAction *act;

	act = menu->addAction(tr("&Open Capture..."), this, &ProfilerDialog::openClicked);
	act->setShortcut(QKeySequence::Open);
	act->setIcon(icon("document-open", QStyle::SP_DialogOpenButton));
	menu->addSeparator();
	act = menu->addAction(tr("&Close"), this, &ProfilerDialog::closeWindow);
	act->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
	act->setIcon(icon("window-close", QStyle::SP_DialogCloseButton));

	menu = menuBar->addMenu(tr("&View"));
	act = menu->addAction(tr("Zoom &In"), viewer, &ProfilerViewer::zoomIn);
	act->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::Key_Equal),
		QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal)});
	act->setIcon(zoomInIcon);
	act = menu->addAction(tr("Zoom &Out"), viewer, &ProfilerViewer::zoomOut);
	act->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Minus), QKeySequence(Qt::Key_Minus)});
	act->setIcon(zoomOutIcon);
	act = menu->addAction(tr("&Fit All"), viewer, &ProfilerViewer::zoomFit);
	act->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_0), QKeySequence(Qt::Key_0)});
	act->setIcon(icon("zoom-fit-best", QStyle::SP_FileDialogDetailedView));
	menu->addSeparator();
	act = menu->addAction(tr("&Previous Frame"), this, &ProfilerDialog::framePrevClicked);
	act->setShortcuts({QKeySequence(Qt::Key_BracketLeft), QKeySequence(Qt::Key_Comma)});
	act->setIcon(icon("go-previous", QStyle::SP_ArrowLeft));
	act = menu->addAction(tr("&Next Frame"), this, &ProfilerDialog::frameNextClicked);
	act->setShortcuts({QKeySequence(Qt::Key_BracketRight), QKeySequence(Qt::Key_Period)});
	act->setIcon(icon("go-next", QStyle::SP_ArrowRight));

	menu = menuBar->addMenu(tr("F&ind"));
	act = menu->addAction(tr("&Find Marker..."), this, &ProfilerDialog::focusFind);
	act->setShortcut(QKeySequence::Find);
	act->setIcon(icon("edit-find", QStyle::SP_FileDialogContentsView));
	menu->addSeparator();
	act = menu->addAction(tr("&First Marker"), viewer, &ProfilerViewer::findFirst);
	act->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home));
	act->setIcon(icon("go-first", QStyle::SP_MediaSkipBackward));
	act = menu->addAction(tr("&Next Marker"), viewer, &ProfilerViewer::findNext);
	act->setShortcuts({QKeySequence(Qt::Key_F3), QKeySequence(Qt::CTRL | Qt::Key_G)});
	act->setIcon(icon("go-next", QStyle::SP_MediaSeekForward));
	act = menu->addAction(tr("&Previous Marker"), viewer, &ProfilerViewer::findPrevious);
	act->setShortcuts({QKeySequence(Qt::SHIFT | Qt::Key_F3), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G)});
	act->setIcon(icon("go-previous", QStyle::SP_MediaSeekBackward));
	act = menu->addAction(tr("&Last Marker"), viewer, &ProfilerViewer::findLast);
	act->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_End));
	act->setIcon(icon("go-last", QStyle::SP_MediaSkipForward));
	menu->addSeparator();
	act = menu->addAction(tr("L&ongest Marker"), viewer, &ProfilerViewer::findLongest);
	act->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
	act->setIcon(icon("go-top", QStyle::SP_ArrowUp));
	act = menu->addAction(tr("Ne&xt Longest Marker"), viewer, &ProfilerViewer::findNextLongest);
	act->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
	act->setIcon(icon("go-down", QStyle::SP_ArrowDown));

	mainLayout->setMenuBar(menuBar);

	// No button is the dialog default, so Enter in a text field never triggers one
	for (QPushButton *btn : findChildren<QPushButton *>())
	{
		btn->setAutoDefault(false);
		btn->setDefault(false);
	}

	browseBtn->setIcon(icon("document-open", QStyle::SP_DirOpenIcon));
	startStopBtn->setIcon(icon("media-record", QStyle::SP_MediaPlay));
	openBtn->setIcon(icon("document-open", QStyle::SP_DialogOpenButton));
	zoomInBtn->setIcon(zoomInIcon);
	zoomOutBtn->setIcon(zoomOutIcon);
	zoomFitBtn->setIcon(icon("zoom-fit-best", QStyle::SP_FileDialogDetailedView));
	framePrevBtn->setIcon(icon("go-previous", QStyle::SP_ArrowLeft));
	frameNextBtn->setIcon(icon("go-next", QStyle::SP_ArrowRight));
	findFirstBtn->setIcon(icon("go-first", QStyle::SP_MediaSkipBackward));
	findPrevBtn->setIcon(icon("go-previous", QStyle::SP_MediaSeekBackward));
	findNextBtn->setIcon(icon("go-next", QStyle::SP_MediaSeekForward));
	findLastBtn->setIcon(icon("go-last", QStyle::SP_MediaSkipForward));
	longestBtn->setIcon(icon("go-top", QStyle::SP_ArrowUp));
	nextLongestBtn->setIcon(icon("go-down", QStyle::SP_ArrowDown));
	closeBtn->setIcon(icon("window-close", QStyle::SP_DialogCloseButton));
	updateFrameControls();

	updateTimer = new QTimer(this);
	updateTimer->setInterval(250);

	connect(browseBtn, &QPushButton::clicked, this, &ProfilerDialog::browseClicked);
	connect(startStopBtn, &QPushButton::clicked, this, &ProfilerDialog::startStopClicked);
	connect(closeBtn, &QPushButton::clicked, this, &ProfilerDialog::closeWindow);
	connect(updateTimer, &QTimer::timeout, this, &ProfilerDialog::updateStatus);

	connect(openBtn, &QPushButton::clicked, this, &ProfilerDialog::openClicked);
	connect(zoomInBtn, &QPushButton::clicked, viewer, &ProfilerViewer::zoomIn);
	connect(zoomOutBtn, &QPushButton::clicked, viewer, &ProfilerViewer::zoomOut);
	connect(zoomFitBtn, &QPushButton::clicked, viewer, &ProfilerViewer::zoomFit);
	connect(frameCombo, &QComboBox::currentIndexChanged, this, &ProfilerDialog::frameSiteChanged);
	connect(frameSpin, &QSpinBox::valueChanged, this, &ProfilerDialog::frameSpinChanged);
	connect(viewer, &ProfilerViewer::frameSelected, this, &ProfilerDialog::viewerFrameSelected);
	connect(findEdit, &QLineEdit::textChanged, viewer, &ProfilerViewer::setFindText);
	connect(findEdit, &QLineEdit::returnPressed, viewer, &ProfilerViewer::findNext);
	connect(findFirstBtn, &QPushButton::clicked, viewer, &ProfilerViewer::findFirst);
	connect(findPrevBtn, &QPushButton::clicked, viewer, &ProfilerViewer::findPrevious);
	connect(findNextBtn, &QPushButton::clicked, viewer, &ProfilerViewer::findNext);
	connect(findLastBtn, &QPushButton::clicked, viewer, &ProfilerViewer::findLast);
	connect(longestBtn, &QPushButton::clicked, viewer, &ProfilerViewer::findLongest);
	connect(nextLongestBtn, &QPushButton::clicked, viewer, &ProfilerViewer::findNextLongest);
	connect(viewer, &ProfilerViewer::findResult, findLabel, &QLabel::setText);
	connect(framePrevBtn, &QPushButton::clicked, this, &ProfilerDialog::framePrevClicked);
	connect(frameNextBtn, &QPushButton::clicked, this, &ProfilerDialog::frameNextClicked);

	restoreGeometry(settings.value(QStringLiteral("profiler/geometry")).toByteArray());
}

ProfilerDialog::~ProfilerDialog(void)
{
	QSettings settings;

	settings.setValue(QStringLiteral("profiler/geometry"), saveGeometry());

#ifdef NESTUX_PROFILER_ENABLE
	if (captureThread != nullptr)
	{
		captureThread->stopCapture();
		captureThread->wait();
	}
#endif

	if (loadThread != nullptr)
	{
		loadThread->requestInterruption();
		loadThread->wait();
	}
}

// Esc must not close the window; it is used to clear the measurement reference
void ProfilerDialog::reject(void)
{
}

void ProfilerDialog::closeEvent(QCloseEvent *event)
{
	done(0);
	deleteLater();
	event->accept();
}

void ProfilerDialog::closeWindow(void)
{
	done(0);
	deleteLater();
}

void ProfilerDialog::focusFind(void)
{
	findEdit->setFocus();
	findEdit->selectAll();
}

void ProfilerDialog::browseClicked(void)
{
	const QString path = QFileDialog::getSaveFileName(this, tr("Select Capture File"),
		pathEdit->text(), captureFilter);

	if (!path.isEmpty())
	{
		pathEdit->setText(path);
	}
}

void ProfilerDialog::startStopClicked(void)
{
#ifndef NESTUX_PROFILER_ENABLE
	QMessageBox::information(this, tr("Profiler"),
		tr("Capturing profiler data is not available in this build.\n\n"
		   "Rebuild NestopiaQt with the profiler enabled "
		   "(cmake -DNESTUX_PROFILER_ENABLE=ON) to record captures. "
		   "Existing capture files can still be opened and viewed."));
#else
	if (captureThread != nullptr)
	{
		stopCapture();
	}
	else
	{
		startCapture();
	}
#endif
}

#ifdef NESTUX_PROFILER_ENABLE
void ProfilerDialog::startCapture(void)
{
	QSettings settings;
	const QString path = pathEdit->text().trimmed();

	if (path.isEmpty())
	{
		QMessageBox::warning(this, tr("Profiler"), tr("Please select a capture file."));
		return;
	}

	settings.setValue(QStringLiteral("profiler/capturePath"), path);

	// The default location may not exist yet
	QDir().mkpath(QFileInfo(path).absolutePath());

	captureThread = new ProfilerCaptureThread(this);
	captureThread->setFilePath(path);

	connect(captureThread, &QThread::finished, this, &ProfilerDialog::captureFinished);
	connect(captureThread, &ProfilerCaptureThread::captureError, this, &ProfilerDialog::captureError);

	pathEdit->setEnabled(false);
	browseBtn->setEnabled(false);
	startStopBtn->setText(tr("Stop Capture"));
	startStopBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-stop"), style()->standardIcon(QStyle::SP_MediaStop)));

	captureThread->start();
	updateTimer->start();
	updateStatus();
}

void ProfilerDialog::stopCapture(void)
{
	if (captureThread != nullptr)
	{
		startStopBtn->setEnabled(false);
		statusLabel->setText(tr("Finalizing capture..."));
		captureThread->stopCapture();
	}
}

void ProfilerDialog::captureFinished(void)
{
	updateTimer->stop();

	if (captureThread != nullptr)
	{
		statusLabel->setText(tr("Capture saved: %1 markers, %2 bytes")
			.arg(captureThread->markerCount()).arg(captureThread->bytesWritten()));
		captureThread->deleteLater();
		captureThread = nullptr;
	}

	pathEdit->setEnabled(true);
	browseBtn->setEnabled(true);
	startStopBtn->setEnabled(true);
	startStopBtn->setText(tr("Start Capture"));
	startStopBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-record"), style()->standardIcon(QStyle::SP_MediaPlay)));
}

void ProfilerDialog::captureError(const QString &message)
{
	QMessageBox::critical(this, tr("Profiler"), message);
}

void ProfilerDialog::updateStatus(void)
{
	if (captureThread != nullptr)
	{
		statusLabel->setText(tr("Capturing: %1 markers, %2 bytes")
			.arg(captureThread->markerCount()).arg(captureThread->bytesWritten()));
	}
}
#else
void ProfilerDialog::captureFinished(void) {}
void ProfilerDialog::captureError(const QString &) {}
void ProfilerDialog::updateStatus(void) {}
#endif

void ProfilerDialog::openClicked(void)
{
	const QString path = QFileDialog::getOpenFileName(this, tr("Open Capture File"),
		pathEdit->text(), captureFilter);

	if (!path.isEmpty())
	{
		loadCapture(path);
	}
}

void ProfilerDialog::loadCapture(const QString &path)
{
	if (loadThread != nullptr)
	{
		return;
	}

	loadName = QFileInfo(path).fileName();
	loadThread = new ProfilerLoadThread(this);
	loadThread->setFilePath(path);

	connect(loadThread, &ProfilerLoadThread::progress, this, &ProfilerDialog::loadProgress);
	connect(loadThread, &QThread::finished, this, &ProfilerDialog::loadFinished);

	openBtn->setEnabled(false);
	loadLabel->setText(tr("Loading %1 ...").arg(loadName));
	loadThread->start();
}

void ProfilerDialog::loadProgress(int percent)
{
	if (loadThread != nullptr)
	{
		loadLabel->setText(tr("Loading %1 ... %2%").arg(loadName).arg(percent));
	}
}

void ProfilerDialog::loadFinished(void)
{
	ProfilerLoadThread *t = loadThread;

	loadThread = nullptr;
	openBtn->setEnabled(true);

	if (t == nullptr)
	{
		return;
	}

	if (!t->succeeded())
	{
		loadLabel->setText(tr("Failed to load capture"));
		QMessageBox::critical(this, tr("Profiler"), tr("Failed to load capture: %1").arg(t->errorString()));
		t->deleteLater();
		return;
	}

	auto data = t->data();
	QString info = tr("%1: %2 markers on %3 threads, %4")
		.arg(loadName).arg(data->markerCount).arg(data->threads.size())
		.arg(QString::number(data->countsToSeconds(data->duration), 'f', 3) + QLatin1String(" s"));

	if (data->truncated)
	{
		info += tr("  [file is truncated]");
	}
	else if (!data->hasFooter)
	{
		info += tr("  [capture was not finalized]");
	}

	if (data->unmatchedStarts || data->strayEnds)
	{
		info += tr("  [%1 unclosed, %2 stray end markers]").arg(data->unmatchedStarts).arg(data->strayEnds);
	}

	loadLabel->setText(info);

	viewer->setData(data);
	viewer->setFindText(findEdit->text());
	populateFrameCombo();
	t->deleteLater();
}

void ProfilerDialog::populateFrameCombo(void)
{
	auto data = viewer->getData();
	int defaultIndex = 0;

	frameCombo->blockSignals(true);
	frameCombo->clear();

	if (data)
	{
		// Only list sites that actually produced blocks
		std::vector<bool> used(data->sites.size(), false);

		for (const ProfilerThreadData &t : data->threads)
		{
			for (const auto &row : t.depths)
			{
				for (const ProfilerBlock &b : row)
				{
					used[b.site] = true;
				}
			}
		}

		for (size_t i = 0; i < data->sites.size(); i++)
		{
			if (!used[i])
			{
				continue;
			}

			const ProfilerSite &s = data->sites[i];
			const QString name = s.comment.isEmpty() ? s.func : s.comment;

			if (s.comment == emulatorFrameComment)
			{
				defaultIndex = frameCombo->count();
			}

			frameCombo->addItem(QStringLiteral("%1  (%2:%3)").arg(name).arg(QFileInfo(s.file).fileName()).arg(s.line),
				static_cast<int>(i));
		}
	}

	frameCombo->setCurrentIndex(frameCombo->count() ? defaultIndex : -1);
	frameCombo->blockSignals(false);

	frameSiteChanged(frameCombo->currentIndex());
}

void ProfilerDialog::updateFrameControls(void)
{
	const int count = viewer->frameCount();
	const bool hasFrames = count > 0;

	frameSpin->blockSignals(true);
	frameSpin->setRange(hasFrames ? 1 : 0, count);

	if (!hasFrames)
	{
		frameSpin->setValue(0);
	}

	frameSpin->blockSignals(false);

	frameSpin->setEnabled(hasFrames);
	framePrevBtn->setEnabled(hasFrames);
	frameNextBtn->setEnabled(hasFrames);
	frameCombo->setEnabled(frameCombo->count() > 0);
	frameLabel->setText(hasFrames ? tr("%1 frames found").arg(count) : tr("No frames found"));
}

void ProfilerDialog::frameSiteChanged(int comboIndex)
{
	const int site = (comboIndex >= 0) ? frameCombo->itemData(comboIndex).toInt() : -1;

	viewer->setFrameSite(site);
	updateFrameControls();
}

void ProfilerDialog::frameSpinChanged(int value)
{
	if (value >= 1)
	{
		viewer->gotoFrame(value - 1);
		frameLabel->setText(viewer->frameDescription(value - 1));
	}
}

void ProfilerDialog::viewerFrameSelected(int index)
{
	// Update the spin box without triggering gotoFrame, which would re-zoom the view
	frameSpin->blockSignals(true);
	frameSpin->setValue(index + 1);
	frameSpin->blockSignals(false);
	frameLabel->setText(viewer->frameDescription(index));
}

void ProfilerDialog::framePrevClicked(void)
{
	frameSpin->setValue(frameSpin->value() - 1);
}

void ProfilerDialog::frameNextClicked(void)
{
	frameSpin->setValue(frameSpin->value() + 1);
}
