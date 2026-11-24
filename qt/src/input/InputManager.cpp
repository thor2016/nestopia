#include "input/InputManager.h"

#include <Qt>

#include "core/NesCore.h"

namespace
{
	QString keyName(int player, int button)
	{
		return QStringLiteral("Input/Player%1/Key_%2").arg(player + 1).arg(QLatin1String(nesButtonName(button)));
	}

	QString joyName(int player, int button, int slot)
	{
		return QStringLiteral("Input/Player%1/Joy%2_%3").arg(player + 1).arg(slot).arg(QLatin1String(nesButtonName(button)));
	}

	QString deviceName(int player)
	{
		return QStringLiteral("Input/Player%1/JoystickDevice").arg(player + 1);
	}

	QString configuredName(int player)
	{
		return QStringLiteral("Input/Player%1/JoystickConfigured").arg(player + 1);
	}
}

InputConfig InputConfig::defaults(void)
{
	InputConfig config;
	PlayerBindings &p1 = config.players[0];

	p1.keys[ButtonA] = Qt::Key_F;
	p1.keys[ButtonB] = Qt::Key_D;
	p1.keys[ButtonSelect] = Qt::Key_S;
	p1.keys[ButtonStart] = Qt::Key_Return;
	p1.keys[ButtonUp] = Qt::Key_Up;
	p1.keys[ButtonDown] = Qt::Key_Down;
	p1.keys[ButtonLeft] = Qt::Key_Left;
	p1.keys[ButtonRight] = Qt::Key_Right;

	return config;
}

void InputConfig::load(QSettings &settings)
{
	// Missing keys keep the value of the defaults. Joystick bindings are not
	// part of the defaults, they are derived from the connected device.
	for (int p = 0; p < kPlayers; ++p)
	{
		PlayerBindings &player = players[p];

		player.joyDevice = settings.value(deviceName(p), player.joyDevice).toString();
		player.joyConfigured = settings.value(configuredName(p), player.joyConfigured).toBool();

		for (int b = 0; b < ButtonCount; ++b)
		{
			player.keys[b] = settings.value(keyName(p, b), player.keys[b]).toInt();

			for (int slot = 0; slot < 2; ++slot)
			{
				const QString text = settings.value(joyName(p, b, slot), player.joy[b][slot].toString()).toString();
				player.joy[b][slot] = JoyBinding::fromString(text);
			}
		}
	}
}

void InputConfig::save(QSettings &settings) const
{
	for (int p = 0; p < kPlayers; ++p)
	{
		const PlayerBindings &player = players[p];

		settings.setValue(deviceName(p), player.joyDevice);
		settings.setValue(configuredName(p), player.joyConfigured);

		for (int b = 0; b < ButtonCount; ++b)
		{
			settings.setValue(keyName(p, b), player.keys[b]);

			for (int slot = 0; slot < 2; ++slot)
			{
				settings.setValue(joyName(p, b, slot), player.joy[b][slot].toString());
			}
		}
	}
}

InputManager::InputManager(void)
	: inputConfig(InputConfig::defaults())
{
}

void InputManager::keyEvent(int key, bool pressed)
{
	if (pressed)
	{
		pressedKeys.insert(key);
	}
	else
	{
		pressedKeys.remove(key);
	}
}

void InputManager::releaseAllKeys(void)
{
	pressedKeys.clear();
}

int InputManager::resolveJoystick(int player) const
{
	const QString &selection = inputConfig.players[player].joyDevice;

	if (selection == QLatin1String("none"))
	{
		return -1;
	}

	if (selection.isEmpty())
	{
		return player < joystickSystem.deviceCount() ? player : -1;
	}

	return joystickSystem.findDevice(selection);
}

unsigned int InputManager::padBits(int player) const
{
	const PlayerBindings &bindings = inputConfig.players[player];
	const int joystick = resolveJoystick(player);
	unsigned int bits = 0;

	for (int b = 0; b < ButtonCount; ++b)
	{
		bool down = bindings.keys[b] != 0 && pressedKeys.contains(bindings.keys[b]);

		if (!down && joystick >= 0)
		{
			const JoyState &state = joystickSystem.device(joystick).state;

			down = state.evaluate(bindings.joy[b][0]) || state.evaluate(bindings.joy[b][1]);
		}

		if (down)
		{
			bits |= 1u << b;
		}
	}

	return bits;
}

void InputManager::applyTo(NesCore &core)
{
	joystickSystem.update();

	for (int p = 0; p < InputConfig::kPlayers; ++p)
	{
		PlayerBindings &bindings = inputConfig.players[p];
		const int joystick = resolveJoystick(p);

		if (joystick >= 0 && !bindings.joyConfigured)
		{
			joystickSystem.defaultBindings(joystick, bindings.joy);
		}

		core.setPadButtons(p, padBits(p));
	}
}
