#include "Controls.h"

#include <cctype>
#include <format>

#include "LV/Engine/Utility/InputManager.h"

namespace
{
	const Controls::Info INFO[Controls::COUNT] =
	{
		{ "forward", "Walk forward", "Moving", "", GLFW_KEY_W },
		{ "back", "Walk back", "Moving", "", GLFW_KEY_S },
		{ "left", "Step left", "Moving", "", GLFW_KEY_A },
		{ "right", "Step right", "Moving", "", GLFW_KEY_D },
		{ "run", "Run (hold)", "Moving", "", GLFW_KEY_LEFT_SHIFT },
		{ "jump", "Jump", "Moving", "", GLFW_KEY_SPACE },
		{ "crouch", "Crouch (hold)", "Moving", "", GLFW_KEY_LEFT_CONTROL },
		{ "use", "Use: pick up, open, work at stations, fit parts, the door", "Working", "E", GLFW_KEY_E },
		{ "primary", "Measure or read with the tool in hand, or hold to turn a nut or bolt (otherwise, use)", "Working", "LMB", Controls::MOUSE + GLFW_MOUSE_BUTTON_LEFT },
		{ "set_down", "Set what you're holding down where you aim", "Working", "RMB", Controls::MOUSE + GLFW_MOUSE_BUTTON_RIGHT },
		{ "drop", "Drop what you're holding", "Working", "G", GLFW_KEY_G },
		{ "move", "Move equipment, or open a delivery", "Working", "F", GLFW_KEY_F },
		{ "rotate", "Turn the engine over by hand; turn equipment or a backpack item", "Working", "R", GLFW_KEY_R },
		{ "roll_back", "Roll the engine on its stand one way", "Working", "Z", GLFW_KEY_Z },
		{ "roll_forward", "Roll the engine on its stand the other way", "Working", "X", GLFW_KEY_X },
		{ "wrench_size", "Take the battery off a cordless tool", "Working", "C", GLFW_KEY_C },
		{ "backpack", "Open or close your backpack", "Screens and view", "Tab", GLFW_KEY_TAB },
		{ "third_person", "View: first person, from behind, from in front, and back", "Screens and view", "F5", GLFW_KEY_F5 },
		{ "dev_menu", "Developer menu", "Screens and view", "F3", GLFW_KEY_F3 },
	};

	Controls::Bindings s_bindings = Controls::Defaults();

	bool IsMouse(int binding) { return binding >= Controls::MOUSE; }
}

const Controls::Info& Controls::InfoOf(Control control)
{
	return INFO[static_cast<size_t>(control)];
}

Controls::Bindings Controls::Defaults()
{
	Bindings bindings = {};
	for (size_t i = 0; i < COUNT; i++)
		bindings[i] = INFO[i].binding;
	return bindings;
}

void Controls::Set(const Bindings& bindings)
{
	s_bindings = bindings;
}

int Controls::BindingOf(Control control)
{
	return s_bindings[static_cast<size_t>(control)];
}

bool Controls::Held(const InputManager& input, Control control)
{
	int binding = BindingOf(control);
	return IsMouse(binding) ? input.IsMouseButtonDown(binding - MOUSE) : input.IsKeyPressed(binding);
}

bool Controls::Pressed(const InputManager& input, Control control)
{
	int binding = BindingOf(control);
	return IsMouse(binding) ? input.WasMouseButtonPressed(binding - MOUSE) : input.WasKeyPressed(binding);
}

std::string Controls::Name(int binding)
{
	if (IsMouse(binding))
	{
		int button = binding - MOUSE;
		if (button == GLFW_MOUSE_BUTTON_LEFT) return "LMB";
		if (button == GLFW_MOUSE_BUTTON_RIGHT) return "RMB";
		if (button == GLFW_MOUSE_BUTTON_MIDDLE) return "MMB";
		return std::format("Mouse {}", button + 1);
	}
	if (binding >= GLFW_KEY_F1 && binding <= GLFW_KEY_F25)
		return std::format("F{}", binding - GLFW_KEY_F1 + 1);
	if (binding >= GLFW_KEY_KP_0 && binding <= GLFW_KEY_KP_9)
		return std::format("Num {}", binding - GLFW_KEY_KP_0);
	switch (binding)
	{
		case GLFW_KEY_SPACE: return "Space";
		case GLFW_KEY_ENTER: return "Enter";
		case GLFW_KEY_TAB: return "Tab";
		case GLFW_KEY_BACKSPACE: return "Backspace";
		case GLFW_KEY_INSERT: return "Insert";
		case GLFW_KEY_DELETE: return "Delete";
		case GLFW_KEY_RIGHT: return "Right";
		case GLFW_KEY_LEFT: return "Left";
		case GLFW_KEY_DOWN: return "Down";
		case GLFW_KEY_UP: return "Up";
		case GLFW_KEY_PAGE_UP: return "Page Up";
		case GLFW_KEY_PAGE_DOWN: return "Page Down";
		case GLFW_KEY_HOME: return "Home";
		case GLFW_KEY_END: return "End";
		case GLFW_KEY_CAPS_LOCK: return "Caps Lock";
		case GLFW_KEY_SCROLL_LOCK: return "Scroll Lock";
		case GLFW_KEY_NUM_LOCK: return "Num Lock";
		case GLFW_KEY_PRINT_SCREEN: return "Print Screen";
		case GLFW_KEY_PAUSE: return "Pause";
		case GLFW_KEY_KP_DECIMAL: return "Num .";
		case GLFW_KEY_KP_DIVIDE: return "Num /";
		case GLFW_KEY_KP_MULTIPLY: return "Num *";
		case GLFW_KEY_KP_SUBTRACT: return "Num -";
		case GLFW_KEY_KP_ADD: return "Num +";
		case GLFW_KEY_KP_ENTER: return "Num Enter";
		case GLFW_KEY_LEFT_SHIFT: return "Shift";
		case GLFW_KEY_LEFT_CONTROL: return "Ctrl";
		case GLFW_KEY_LEFT_ALT: return "Alt";
		case GLFW_KEY_RIGHT_SHIFT: return "Right Shift";
		case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
		case GLFW_KEY_RIGHT_ALT: return "Right Alt";
		case GLFW_KEY_MENU: return "Menu";
	}
	// The rest by what's printed on them, in the keyboard's own layout.
	if (const char* name = glfwGetKeyName(binding, 0); name && *name)
	{
		std::string text = name;
		for (char& c : text)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		return text;
	}
	return std::format("Key {}", binding);
}

std::string Controls::NameOf(Control control)
{
	return Name(BindingOf(control));
}

std::string Controls::Keys(const std::string& text)
{
	// Each part of what's in brackets, split at +, is a control's token, or the whole is left alone.
	auto rename = [](const std::string& inside) -> std::optional<std::string>
	{
		std::string named;
		size_t start = 0;
		while (true)
		{
			size_t plus = inside.find('+', start);
			std::string part = inside.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
			bool found = false;
			for (size_t i = 0; i < COUNT && !found; i++)
				if (*INFO[i].token && part == INFO[i].token)
				{
					named += (named.empty() ? "" : "+") + Name(s_bindings[i]);
					found = true;
				}
			if (!found)
				return std::nullopt;
			if (plus == std::string::npos)
				return named;
			start = plus + 1;
		}
	};

	std::string result;
	size_t at = 0;
	while (at < text.size())
	{
		size_t open = text.find('[', at);
		size_t close = open == std::string::npos ? std::string::npos : text.find(']', open);
		if (close == std::string::npos)
			break;
		result += text.substr(at, open - at);
		auto named = rename(text.substr(open + 1, close - open - 1));
		result += named ? "[" + *named + "]" : text.substr(open, close - open + 1);
		at = close + 1;
	}
	return result + text.substr((std::min)(at, text.size()));
}

std::optional<int> Controls::FirstPressed(const InputManager& input)
{
	for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; key++)
		if (input.WasKeyPressed(key))
			return key;
	for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; button++)
		if (input.WasMouseButtonPressed(button))
			return MOUSE + button;
	return std::nullopt;
}

std::string Controls::Reserved(int binding)
{
	if (binding == GLFW_KEY_ESCAPE)
		return "Esc is kept for menus.";
	if (binding == GLFW_KEY_GRAVE_ACCENT)
		return "` is kept for the console.";
	if (binding >= GLFW_KEY_0 && binding <= GLFW_KEY_9)
		return "1 - 0 are kept for the hotbar.";
	return "";
}

bool Controls::Available(Control control)
{
#ifdef _DEBUG
	return true;
#else
	return control != Control::DEV_MENU;
#endif
}
