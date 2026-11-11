#pragma once

#include <array>
#include <optional>
#include <string>

class InputManager;

// What the player does, each on a key or mouse button of their choosing (GameSettings keeps them).
// A few stay where they are: Esc for menus, ` for the console, 1-0 for the hotbar, and the mouse to
// look.
enum class Control
{
	FORWARD, BACK, LEFT, RIGHT, RUN, JUMP, CROUCH,
	USE, PRIMARY, SET_DOWN, DROP, MOVE, ROTATE, ROLL_BACK, ROLL_FORWARD, WRENCH_SIZE, TURN_HELD,
	BACKPACK, THIRD_PERSON, DEV_MENU,
	COUNT
};

namespace Controls
{
	constexpr size_t COUNT = static_cast<size_t>(Control::COUNT);
	// A binding is a GLFW key, or MOUSE plus a GLFW mouse button.
	constexpr int MOUSE = 1000;
	using Bindings = std::array<int, COUNT>;

	struct Info
	{
		const char* id = "";		// in the settings file
		const char* name = "";		// in the controls list
		const char* group = "";
		// How the HUD's prompts name it, in brackets ("[E] Pick up"): those are shown with the key
		// it's on now. Empty if none do.
		const char* token = "";
		int binding = 0;			// the default
	};
	const Info& InfoOf(Control control);
	Bindings Defaults();

	// The bindings in use, from the settings.
	void Set(const Bindings& bindings);
	int BindingOf(Control control);

	// Down now, and gone down this frame.
	bool Held(const InputManager& input, Control control);
	bool Pressed(const InputManager& input, Control control);

	// "E", "Left Shift", "Mouse 4".
	std::string Name(int binding);
	std::string NameOf(Control control);
	// The text with each bracketed key in it ("[E]", "[LMB]", "[Ctrl+E]") named by what it's bound to now.
	std::string Keys(const std::string& text);

	// The first key or button gone down this frame, for picking a new binding.
	std::optional<int> FirstPressed(const InputManager& input);
	// In this build: the developer menu's key is for debug builds only.
	bool Available(Control control);
	// Keys that can't be bound, the ones that stay where they are: why not, or empty if it can be.
	std::string Reserved(int binding);
}
