#pragma once

// The buttons of a NES gamepad. The values are the bit numbers of
// Nes::Api::Input::Controllers::Pad::A, B, SELECT, START, UP, DOWN, LEFT, RIGHT.
enum NesButton
{
	ButtonA = 0,
	ButtonB,
	ButtonSelect,
	ButtonStart,
	ButtonUp,
	ButtonDown,
	ButtonLeft,
	ButtonRight,
	ButtonCount
};

inline const char *nesButtonName(int button)
{
	static const char *const names[ButtonCount] = {
		"A", "B", "Select", "Start", "Up", "Down", "Left", "Right"
	};

	return (button >= 0 && button < ButtonCount) ? names[button] : "";
}
