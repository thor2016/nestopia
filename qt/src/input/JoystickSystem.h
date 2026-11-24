#pragma once

#include <cstdint>
#include <vector>

#include <SDL.h>

#include <QString>

// One physical input of a joystick that can be bound to a NES button
struct JoyBinding
{
	enum Type : uint8_t
	{
		None = 0,
		Button,
		Axis,
		Hat
	};

	Type type = None;
	// Button, axis or hat number
	int index = 0;
	// Axis: +1 or -1 for the direction. Hat: SDL_HAT_* direction mask.
	int value = 0;

	bool isValid(void) const { return type != None; }
	bool operator==(const JoyBinding &other) const
	{
		return type == other.type && index == other.index && value == other.value;
	}

	// "button:3", "axis:1:-1", "hat:0:1", empty for None
	QString toString(void) const;
	static JoyBinding fromString(const QString &text);
	// Human readable, e.g. "Button 3", "Axis 1-", "Hat 0 Up"
	QString displayName(void) const;
};

// Snapshot of the inputs of one joystick
struct JoyState
{
	// Axis values beyond this count as pressed
	static constexpr int kAxisThreshold = 16384;

	std::vector<uint8_t> buttons;
	std::vector<int16_t> axes;
	std::vector<uint8_t> hats;

	bool evaluate(const JoyBinding &binding) const;
	// Finds an input that is active in this state but not in baseline
	bool detectNew(const JoyState &baseline, JoyBinding &found) const;
};

// SDL joystick access. Not thread safe on its own: like the rest of the
// emulation state it is only used with the emulation mutex held.
class JoystickSystem
{
	public:
		struct Device
		{
			// Stable identifier "<guid>:<n>", n numbers identical devices
			QString key;
			QString name;
			SDL_Joystick *joystick = nullptr;
			SDL_GameController *controller = nullptr;
			SDL_JoystickID instanceId = -1;
			JoyState state;
		};

		JoystickSystem(void);
		~JoystickSystem(void);

		bool init(void);
		void shutdown(void);

		// Refreshes the state of all devices and picks up hot plugged ones
		void update(void);

		int deviceCount(void) const { return int(devices.size()); }
		const Device &device(int index) const { return devices[size_t(index)]; }
		int findDevice(const QString &key) const;

		// Default bindings in NES button order, two slots per button: the
		// d-pad or buttons first, the left stick second.
		void defaultBindings(int deviceIndex, JoyBinding (&out)[8][2]) const;

	private:
		void rescan(void);
		static void readState(Device &device);

		std::vector<Device> devices;
		int knownCount;
		bool initialized;
};
