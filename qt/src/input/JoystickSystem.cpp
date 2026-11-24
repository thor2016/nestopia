#include "input/JoystickSystem.h"

#include <algorithm>
#include <cstdlib>

#include <QStringList>
#include <QtDebug>

#include "input/NesButtons.h"

// --------------------------------------------------------------------------
// JoyBinding
// --------------------------------------------------------------------------

QString JoyBinding::toString(void) const
{
	switch (type)
	{
		case Button: return QStringLiteral("button:%1").arg(index);
		case Axis:   return QStringLiteral("axis:%1:%2").arg(index).arg(value);
		case Hat:    return QStringLiteral("hat:%1:%2").arg(index).arg(value);
		case None:   break;
	}

	return QString();
}

JoyBinding JoyBinding::fromString(const QString &text)
{
	JoyBinding binding;
	const QStringList parts = text.split(QLatin1Char(':'));
	bool ok = true;

	if (parts.size() == 2 && parts[0] == QLatin1String("button"))
	{
		binding.type = Button;
		binding.index = parts[1].toInt(&ok);
	}
	else if (parts.size() == 3 && parts[0] == QLatin1String("axis"))
	{
		binding.type = Axis;
		binding.index = parts[1].toInt(&ok);
		binding.value = parts[2].toInt() < 0 ? -1 : 1;
	}
	else if (parts.size() == 3 && parts[0] == QLatin1String("hat"))
	{
		binding.type = Hat;
		binding.index = parts[1].toInt(&ok);
		binding.value = parts[2].toInt();
	}
	else
	{
		ok = false;
	}

	return (ok && binding.index >= 0) ? binding : JoyBinding();
}

QString JoyBinding::displayName(void) const
{
	switch (type)
	{
		case Button:
			return QStringLiteral("Button %1").arg(index);
		case Axis:
			return QStringLiteral("Axis %1%2").arg(index).arg(value < 0 ? QLatin1Char('-') : QLatin1Char('+'));
		case Hat:
		{
			QString direction;

			if (value & SDL_HAT_UP)    direction += QStringLiteral("Up");
			if (value & SDL_HAT_DOWN)  direction += QStringLiteral("Down");
			if (value & SDL_HAT_LEFT)  direction += QStringLiteral("Left");
			if (value & SDL_HAT_RIGHT) direction += QStringLiteral("Right");

			return QStringLiteral("Hat %1 %2").arg(index).arg(direction);
		}
		case None:
			break;
	}

	return QString();
}

// --------------------------------------------------------------------------
// JoyState
// --------------------------------------------------------------------------

bool JoyState::evaluate(const JoyBinding &binding) const
{
	const size_t index = size_t(binding.index);

	switch (binding.type)
	{
		case JoyBinding::Button:
			return index < buttons.size() && buttons[index] != 0;
		case JoyBinding::Axis:
			if (index >= axes.size())
			{
				return false;
			}
			return binding.value < 0 ? axes[index] < -kAxisThreshold : axes[index] > kAxisThreshold;
		case JoyBinding::Hat:
			return index < hats.size() && (hats[index] & binding.value) != 0;
		case JoyBinding::None:
			break;
	}

	return false;
}

bool JoyState::detectNew(const JoyState &baseline, JoyBinding &found) const
{
	for (size_t i = 0; i < buttons.size(); ++i)
	{
		const bool before = i < baseline.buttons.size() && baseline.buttons[i] != 0;

		if (buttons[i] != 0 && !before)
		{
			found = JoyBinding{JoyBinding::Button, int(i), 0};
			return true;
		}
	}

	for (size_t i = 0; i < axes.size(); ++i)
	{
		const int before = i < baseline.axes.size() ? baseline.axes[i] : 0;

		if (std::abs(int(axes[i])) > kAxisThreshold && std::abs(before) <= kAxisThreshold)
		{
			found = JoyBinding{JoyBinding::Axis, int(i), axes[i] < 0 ? -1 : 1};
			return true;
		}
	}

	for (size_t i = 0; i < hats.size(); ++i)
	{
		const int before = i < baseline.hats.size() ? baseline.hats[i] : 0;
		const int pressed = hats[i] & ~before;

		if (pressed != 0)
		{
			// One direction per binding
			const int directions[] = {SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT};

			for (int direction : directions)
			{
				if (pressed & direction)
				{
					found = JoyBinding{JoyBinding::Hat, int(i), direction};
					return true;
				}
			}
		}
	}

	return false;
}

// --------------------------------------------------------------------------
// JoystickSystem
// --------------------------------------------------------------------------

JoystickSystem::JoystickSystem(void)
	: knownCount(0),
	  initialized(false)
{
}

JoystickSystem::~JoystickSystem(void)
{
	shutdown();
}

bool JoystickSystem::init(void)
{
	if (initialized)
	{
		return true;
	}

	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0)
	{
		qWarning() << "SDL joystick init failed:" << SDL_GetError();
		return false;
	}

	// The state is polled, nothing reads the SDL event queue
	SDL_JoystickEventState(SDL_IGNORE);
	SDL_GameControllerEventState(SDL_IGNORE);

	initialized = true;
	rescan();
	return true;
}

void JoystickSystem::shutdown(void)
{
	if (!initialized)
	{
		return;
	}

	for (Device &device : devices)
	{
		if (device.controller != nullptr)
		{
			SDL_GameControllerClose(device.controller);
		}

		if (device.joystick != nullptr)
		{
			SDL_JoystickClose(device.joystick);
		}
	}

	devices.clear();
	SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
	initialized = false;
}

int JoystickSystem::findDevice(const QString &key) const
{
	for (size_t i = 0; i < devices.size(); ++i)
	{
		if (devices[i].key == key)
		{
			return int(i);
		}
	}

	return -1;
}

void JoystickSystem::readState(Device &device)
{
	JoyState &state = device.state;
	SDL_Joystick *joystick = device.joystick;

	state.buttons.resize(size_t(std::max(SDL_JoystickNumButtons(joystick), 0)));
	state.axes.resize(size_t(std::max(SDL_JoystickNumAxes(joystick), 0)));
	state.hats.resize(size_t(std::max(SDL_JoystickNumHats(joystick), 0)));

	for (size_t i = 0; i < state.buttons.size(); ++i)
	{
		state.buttons[i] = SDL_JoystickGetButton(joystick, int(i));
	}

	for (size_t i = 0; i < state.axes.size(); ++i)
	{
		state.axes[i] = SDL_JoystickGetAxis(joystick, int(i));
	}

	for (size_t i = 0; i < state.hats.size(); ++i)
	{
		state.hats[i] = SDL_JoystickGetHat(joystick, int(i));
	}
}

void JoystickSystem::update(void)
{
	if (!initialized)
	{
		return;
	}

	// Updates the state of open devices and detects hot plugging
	SDL_JoystickUpdate();

	bool changed = knownCount != SDL_NumJoysticks();

	for (const Device &device : devices)
	{
		if (!SDL_JoystickGetAttached(device.joystick))
		{
			changed = true;
		}
	}

	if (changed)
	{
		rescan();
	}

	for (Device &device : devices)
	{
		readState(device);
	}
}

void JoystickSystem::rescan(void)
{
	std::vector<Device> current;
	const int count = SDL_NumJoysticks();

	knownCount = count;

	for (int i = 0; i < count; ++i)
	{
		const SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(i);
		auto existing = std::find_if(devices.begin(), devices.end(),
			[instance](const Device &d) { return d.instanceId == instance && SDL_JoystickGetAttached(d.joystick); });

		if (existing != devices.end())
		{
			current.push_back(*existing);
			existing->joystick = nullptr;
			existing->controller = nullptr;
			continue;
		}

		Device device;
		device.joystick = SDL_JoystickOpen(i);

		if (device.joystick == nullptr)
		{
			continue;
		}

		// Skip devices that have nothing to press, e.g. some motion sensors
		if (SDL_JoystickNumButtons(device.joystick) == 0 && SDL_JoystickNumHats(device.joystick) == 0)
		{
			SDL_JoystickClose(device.joystick);
			continue;
		}

		device.instanceId = SDL_JoystickInstanceID(device.joystick);
		device.name = QString::fromUtf8(SDL_JoystickName(device.joystick));

		if (SDL_IsGameController(i))
		{
			device.controller = SDL_GameControllerOpen(i);
		}

		char guid[64];
		SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(device.joystick), guid, sizeof(guid));
		device.key = QString::fromLatin1(guid);

		current.push_back(device);
	}

	// Devices that went away
	for (Device &device : devices)
	{
		if (device.controller != nullptr)
		{
			SDL_GameControllerClose(device.controller);
		}

		if (device.joystick != nullptr)
		{
			SDL_JoystickClose(device.joystick);
		}
	}

	// Number identical devices so they get distinct keys
	for (size_t i = 0; i < current.size(); ++i)
	{
		int occurrence = 0;

		for (size_t j = 0; j < i; ++j)
		{
			if (current[j].key.section(QLatin1Char(':'), 0, 0) == current[i].key.section(QLatin1Char(':'), 0, 0))
			{
				++occurrence;
			}
		}

		current[i].key = current[i].key.section(QLatin1Char(':'), 0, 0) + QLatin1Char(':') + QString::number(occurrence);
	}

	devices = std::move(current);

	for (Device &device : devices)
	{
		readState(device);
	}
}

void JoystickSystem::defaultBindings(int deviceIndex, JoyBinding (&out)[8][2]) const
{
	for (auto &button : out)
	{
		button[0] = JoyBinding();
		button[1] = JoyBinding();
	}

	SDL_GameController *controller = (deviceIndex >= 0 && deviceIndex < deviceCount())
		? devices[size_t(deviceIndex)].controller : nullptr;

	if (controller != nullptr)
	{
		auto fromBind = [](const SDL_GameControllerButtonBind &bind, int axisValue) {
			JoyBinding binding;

			switch (bind.bindType)
			{
				case SDL_CONTROLLER_BINDTYPE_BUTTON:
					binding = JoyBinding{JoyBinding::Button, bind.value.button, 0};
					break;
				case SDL_CONTROLLER_BINDTYPE_HAT:
					binding = JoyBinding{JoyBinding::Hat, bind.value.hat.hat, bind.value.hat.hat_mask};
					break;
				case SDL_CONTROLLER_BINDTYPE_AXIS:
					binding = JoyBinding{JoyBinding::Axis, bind.value.axis, axisValue};
					break;
				default:
					break;
			}

			return binding;
		};

		auto button = [&](SDL_GameControllerButton b) {
			return fromBind(SDL_GameControllerGetBindForButton(controller, b), 1);
		};

		auto axis = [&](SDL_GameControllerAxis a, int direction) {
			return fromBind(SDL_GameControllerGetBindForAxis(controller, a), direction);
		};

		// The NES layout has B on the left and A on the right, which are the
		// bottom and right buttons of a controller.
		out[ButtonA][0] = button(SDL_CONTROLLER_BUTTON_B);
		out[ButtonB][0] = button(SDL_CONTROLLER_BUTTON_A);
		out[ButtonSelect][0] = button(SDL_CONTROLLER_BUTTON_BACK);
		out[ButtonStart][0] = button(SDL_CONTROLLER_BUTTON_START);
		out[ButtonUp][0] = button(SDL_CONTROLLER_BUTTON_DPAD_UP);
		out[ButtonDown][0] = button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
		out[ButtonLeft][0] = button(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
		out[ButtonRight][0] = button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
		out[ButtonUp][1] = axis(SDL_CONTROLLER_AXIS_LEFTY, -1);
		out[ButtonDown][1] = axis(SDL_CONTROLLER_AXIS_LEFTY, 1);
		out[ButtonLeft][1] = axis(SDL_CONTROLLER_AXIS_LEFTX, -1);
		out[ButtonRight][1] = axis(SDL_CONTROLLER_AXIS_LEFTX, 1);
		return;
	}

	// Generic joystick, assume the common layout
	out[ButtonA][0] = JoyBinding{JoyBinding::Button, 1, 0};
	out[ButtonB][0] = JoyBinding{JoyBinding::Button, 0, 0};
	out[ButtonSelect][0] = JoyBinding{JoyBinding::Button, 6, 0};
	out[ButtonStart][0] = JoyBinding{JoyBinding::Button, 7, 0};
	out[ButtonUp][0] = JoyBinding{JoyBinding::Hat, 0, SDL_HAT_UP};
	out[ButtonDown][0] = JoyBinding{JoyBinding::Hat, 0, SDL_HAT_DOWN};
	out[ButtonLeft][0] = JoyBinding{JoyBinding::Hat, 0, SDL_HAT_LEFT};
	out[ButtonRight][0] = JoyBinding{JoyBinding::Hat, 0, SDL_HAT_RIGHT};
	out[ButtonUp][1] = JoyBinding{JoyBinding::Axis, 1, -1};
	out[ButtonDown][1] = JoyBinding{JoyBinding::Axis, 1, 1};
	out[ButtonLeft][1] = JoyBinding{JoyBinding::Axis, 0, -1};
	out[ButtonRight][1] = JoyBinding{JoyBinding::Axis, 0, 1};
}
