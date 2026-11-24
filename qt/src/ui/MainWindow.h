#pragma once

#include <functional>
#include <memory>

#include <QMainWindow>
#include <QSettings>
#include <QTimer>

#include "Settings.h"
#include "audio/AudioDriver.h"
#include "core/EmulationThread.h"
#include "input/InputManager.h"
#include "video/ViewerInterface.h"

class QAction;
class QActionGroup;
class QLabel;
class QMenu;

class MainWindow : public QMainWindow
{
	Q_OBJECT

	public:
		explicit MainWindow(QWidget *parent = nullptr);
		~MainWindow(void) override;

		bool openRom(const QString &path);

	protected:
		void closeEvent(QCloseEvent *event) override;
		void dragEnterEvent(QDragEnterEvent *event) override;
		void dropEvent(QDropEvent *event) override;
		bool eventFilter(QObject *watched, QEvent *event) override;
		void changeEvent(QEvent *event) override;

	private slots:
		void onOpenRom(void);
		void onCloseRom(void);
		void onFrameFinished(quint64 frameNumber);
		void onFpsTimer(void);
		void onInputConfig(void);
		void onAbout(void);

	private:
		void createMenus(void);
		QAction *addCheckable(QMenu *menu, const QString &text, bool checked);
		void connectChoice(QActionGroup *group, QMenu *menu, const QString &text, int value, bool checked,
			std::function<void(int)> apply);

		bool installViewer(ViewerBase::Driver driver);
		void selectVideoDriver(ViewerBase::Driver driver);
		bool startAudio(void);
		void stopAudio(void);
		void setPaused(bool paused);
		void reset(bool hard);
		void setFullscreen(bool fullscreen);
		void applyWindowScale(void);
		void updateTitle(void);
		void updateStatus(void);
		void pushFrameToViewer(void);
		void saveConfig(void);
		void shutdown(void);

		QSettings settings;
		Config config;
		InputManager input;
		EmulationThread emulation;
		std::unique_ptr<AudioDriverBase> audio;
		std::unique_ptr<ViewerBase> viewer;

		QTimer fpsTimer;
		quint64 framesThisSecond;
		bool shutDown;

		QLabel *statusRom;
		QLabel *statusFps;
		QLabel *statusDrivers;

		QAction *closeRomAction;
		QAction *pauseAction;
		QAction *muteAction;
		QAction *fullscreenAction;
		QActionGroup *videoDriverGroup;
		QActionGroup *audioDriverGroup;
};
