// ProfilerDialog.h
//
// Controls for capturing profiler markers to a file and for viewing capture timelines.

#pragma once

#include <QDialog>
#include <QString>

#include "profiler/ProfilerCaptureFile.h"
#include "profiler/ProfilerViewer.h"

#ifdef NESTOPIAQT_PROFILER_ENABLE
#include "profiler/ProfilerCapture.h"
#endif

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class ProfilerDialog : public QDialog
{
	Q_OBJECT

	public:
		explicit ProfilerDialog(QWidget *parent = nullptr);
		~ProfilerDialog(void) override;

	protected:
		void closeEvent(QCloseEvent *event) override;
		void reject(void) override;

	private:
		void startCapture(void);
		void stopCapture(void);
		void updateStatus(void);
		void loadCapture(const QString &path);
		void populateFrameCombo(void);
		void updateFrameControls(void);

		QLineEdit   *pathEdit;
		QPushButton *browseBtn;
		QPushButton *startStopBtn;
		QLabel      *statusLabel;
		QTimer      *updateTimer;

		QPushButton *openBtn;
		QLabel      *loadLabel;
		ProfilerViewer *viewer;
		QComboBox   *frameCombo;
		QSpinBox    *frameSpin;
		QPushButton *framePrevBtn;
		QPushButton *frameNextBtn;
		QLabel      *frameLabel;
		QLineEdit   *findEdit;
		QLabel      *findLabel;

#ifdef NESTOPIAQT_PROFILER_ENABLE
		ProfilerCaptureThread *captureThread = nullptr;
#endif
		ProfilerLoadThread *loadThread = nullptr;
		QString loadName;

	private slots:
		void browseClicked(void);
		void startStopClicked(void);
		void captureFinished(void);
		void captureError(const QString &message);
		void closeWindow(void);
		void openClicked(void);
		void loadProgress(int percent);
		void loadFinished(void);
		void frameSiteChanged(int comboIndex);
		void frameSpinChanged(int value);
		void viewerFrameSelected(int index);
		void focusFind(void);
		void framePrevClicked(void);
		void frameNextClicked(void);
};
