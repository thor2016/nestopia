#pragma once

#include <QSet>
#include <QSettings>
#include <QString>

#include "input/JoystickSystem.h"
#include "input/NesButtons.h"

class NesCore;

// Bindings of one player
struct PlayerBindings
{
	// Qt::Key per NES button, 0 for unbound
	int keys[ButtonCount] = {};
	// Two joystick inputs per NES button
	JoyBinding joy[ButtonCount][2];
	// Joystick used by the player: empty selects the n-th connected joystick
	// for player n, "none" disables joysticks, otherwise a
	// JoystickSystem::Device::key
	QString joyDevice;
	// False until the joystick bindings were chosen by the user. Until then
	// the defaults of the connected joystick are used.
	bool joyConfigured = false;
};

struct InputConfig
{
	static constexpr int kPlayers = 2;

	PlayerBindings players[kPlayers];

	// fceux style keyboard layout for player 1, nothing on player 2
	static InputConfig defaults(void);

	void load(QSettings &settings);
	void save(QSettings &settings) const;
};

// Turns keyboard and joystick state into NES pad buttons.
//
// All state belongs to the emulation state: every member function must be
// called with the emulation mutex held, key events included. The emulation
// thread calls applyTo() once per frame.
class InputManager
{
	public:
		InputManager(void);

		JoystickSystem &joysticks(void) { return joystickSystem; }

		void setConfig(const InputConfig &newConfig) { inputConfig = newConfig; }
		const InputConfig &config(void) const { return inputConfig; }

		// Qt::Key values of the keyboard
		void keyEvent(int key, bool pressed);
		void releaseAllKeys(void);

		// Samples the devices and updates the pads of the core
		void applyTo(NesCore &core);

		// Joystick index used by a player or -1, see PlayerBindings::joyDevice
		int resolveJoystick(int player) const;

	private:
		unsigned int padBits(int player) const;

		JoystickSystem joystickSystem;
		InputConfig inputConfig;
		QSet<int> pressedKeys;
};
