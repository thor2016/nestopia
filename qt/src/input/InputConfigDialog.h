#pragma once

#include <QDialog>
#include <QStringList>
#include <QTimer>

#include "input/InputManager.h"

class EmulationThread;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QTabWidget;
class QTableWidget;

// Edits the keyboard and joystick bindings of the two players. A cell is
// clicked, then the next key or joystick input becomes the binding.
class InputConfigDialog : public QDialog
{
	Q_OBJECT

	public:
		InputConfigDialog(EmulationThread &emulation, InputManager &inputManager, QWidget *parent);

		// The edited configuration, valid after the dialog was accepted
		const InputConfig &result(void) const { return edited; }

	protected:
		void keyPressEvent(QKeyEvent *event) override;

	private slots:
		void onCellClicked(int row, int column);
		void onDeviceChanged(void);
		void onUseJoystickDefaults(void);
		void onClearPlayer(void);
		void poll(void);

	private:
		struct PlayerPage
		{
			QWidget *page = nullptr;
			QComboBox *deviceCombo = nullptr;
			QTableWidget *table = nullptr;
			bool touchedJoystick = false;
		};

		struct Capture
		{
			int player = -1;
			int button = 0;
			// 0 keyboard, 1 and 2 the two joystick slots
			int column = 0;
			JoyState baseline;
			bool baselineValid = false;
		};

		void buildPage(int player);
		void populateDevices(void);
		void refreshTable(int player);
		void startCapture(int player, int row, int column);
		void endCapture(void);
		void assignKey(int key);
		void assignJoystick(const JoyBinding &binding);
		void clearCell(void);
		int currentPlayer(void) const;
		// Joystick index of the player or -1, the emulation mutex must be held
		int resolveDevice(int player) const;
		QString cellText(int player, int button, int column) const;
		void updateHint(void);

		EmulationThread &emulation;
		InputManager &input;
		InputConfig edited;
		PlayerPage pages[InputConfig::kPlayers];
		QStringList deviceKeys;
		Capture capture;
		QTimer pollTimer;
		QTabWidget *tabs;
		QLabel *hint;
		int pollTicks;
};
