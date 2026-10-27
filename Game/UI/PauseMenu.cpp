#include "PauseMenu.h"

#include <format>

#include <imgui.h>

namespace
{
	constexpr float MENU_WIDTH = 420.f;
	constexpr float BUTTON_HEIGHT = 40.f;

	const char* CONTROLS[][2] = {
		{ "W A S D", "Walk" },
		{ "Shift", "Run" },
		{ "Space", "Jump" },
		{ "Mouse", "Look around" },
		{ "E", "Pick up into your backpack, open deliveries, use stations, fit parts, open the roll-up door" },
		{ "Left click", "Measure with the tool in your hand (otherwise same as E)" },
		{ "Right click", "Set what you're holding down where you're aiming" },
		{ "G", "Drop what you're holding" },
		{ "Q", "Stow what you're holding in your backpack (or, when it won't fit, on the parts shelf or tool board)" },
		{ "1 - 0", "Take a hotbar item in hand / put it back" },
		{ "Tab", "Open or close your backpack: drag items, R turns them, drag out to drop" },
		{ "F", "Move equipment: machines, bench, stands, shelf, tool board (R rotates, F cancels)" },
		{ "R (hold)", "Turn over the engine you're looking at, by hand" },
		{ "Z / X (hold)", "Roll the engine you're looking at on its stand" },
		{ "Esc", "Close screens / this menu" },
		{ "F3", "Developer menu: x-ray, bounding boxes, time of day, Lordi's debug panels" },
	};
}

void PauseMenu::Open()
{
	m_open = true;
	m_page = Page::MAIN;
	m_status.clear();
}

void PauseMenu::Close()
{
	m_open = false;
}

bool PauseMenu::IsOpen() const
{
	return m_open;
}

bool PauseMenu::BigButton(const char* label)
{
	return ImGui::Button(label, { -1.f, BUTTON_HEIGHT });
}

void PauseMenu::SetStatus(const std::string& status)
{
	m_status = status;
}

void PauseMenu::DrawGameSettings(GameSettings& settings)
{
	ImGui::SliderFloat("Mouse sensitivity", &settings.mouseSensitivity, 0.2f, 3.f, "%.2fx");
	ImGui::Checkbox("Invert mouse Y", &settings.invertY);
	ImGui::SliderFloat("Field of view", &settings.fieldOfView, 55.f, 100.f, "%.0f deg");
	ImGui::Checkbox("Imperial units in the workshop", &settings.imperialUnits);
	ImGui::Checkbox("Show control hints", &settings.showControls);
	ImGui::Checkbox("Show FPS", &settings.showFps);
}

void PauseMenu::DrawVideoSettings(GameSettings& settings, const PauseMenuContext& context)
{
	const char* modes[] = { "Windowed", "Borderless fullscreen", "Fullscreen" };
	ImGui::Combo("Display mode", &settings.displayMode, modes, IM_ARRAYSIZE(modes));

	// Borderless always runs at the desktop's resolution.
	ImGui::BeginDisabled(settings.displayMode == 1);
	std::string current = std::format("{} x {}", settings.resolutionWidth, settings.resolutionHeight);
	if (ImGui::BeginCombo("Resolution", current.c_str()))
	{
		for (const auto& [width, height] : context.resolutions)
		{
			bool selected = static_cast<int>(width) == settings.resolutionWidth && static_cast<int>(height) == settings.resolutionHeight;
			if (ImGui::Selectable(std::format("{} x {}", width, height).c_str(), selected))
			{
				settings.resolutionWidth = static_cast<int>(width);
				settings.resolutionHeight = static_cast<int>(height);
			}
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();

	ImGui::Checkbox("V-Sync", &settings.vsync);
	int limit = settings.frameLimit;
	if (ImGui::SliderInt("Frame rate limit", &limit, 0, 240, limit == 0 ? "Unlimited" : "%d fps"))
		settings.frameLimit = limit < 20 ? 0 : limit;

	ImGui::Separator();
	// DLSS draws the scene smaller and lets NVIDIA's upscaler rebuild it at full size, smoothing
	// edges as it goes. DLAA keeps the full size and only does the smoothing.
	const char* dlssModes[] = { "Off", "DLAA (native resolution)", "Quality", "Balanced", "Performance", "Ultra Performance" };
	ImGui::BeginDisabled(!context.dlssAvailable);
	ImGui::Combo("NVIDIA DLSS", &settings.dlss, dlssModes, IM_ARRAYSIZE(dlssModes));
	ImGui::EndDisabled();
	if (!context.dlssAvailable)
		ImGui::TextDisabled("%s", context.dlssStatus.c_str());
	else if (settings.dlss != 0 && context.renderSize.first)
		ImGui::TextDisabled("Rendering at %u x %u, upscaled to %u x %u", context.renderSize.first, context.renderSize.second,
			context.outputSize.first, context.outputSize.second);
	bool dlssOn = context.dlssAvailable && settings.dlss != 0;

	// Only the sample counts the GPU supports.
	const int samples[] = { 1, 2, 4, 8 };
	std::string msaaLabel = settings.msaa == 1 ? "Off" : std::format("{}x", settings.msaa);
	if (ImGui::BeginCombo("Anti-aliasing (MSAA)", msaaLabel.c_str()))
	{
		for (int count : samples)
			if (count <= context.maxMsaa && ImGui::Selectable(count == 1 ? "Off" : std::format("{}x", count).c_str(), settings.msaa == count))
				settings.msaa = count;
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::TextDisabled(dlssOn ? "(not used with DLSS)" : "(on restart)");

	ImGui::Separator();
	const char* shadowQualities[] = { "Off", "Low", "Medium", "High" };
	ImGui::Combo("Shadows", &settings.shadowQuality, shadowQualities, IM_ARRAYSIZE(shadowQualities));
	ImGui::BeginDisabled(settings.shadowQuality == 0);
	ImGui::SliderFloat("Shadow distance", &settings.shadowDistance, 15.f, 120.f, "%.0f m");
	ImGui::EndDisabled();
	ImGui::SliderFloat("View distance", &settings.renderDistance, 80.f, 500.f, "%.0f m");
	ImGui::SliderFloat("Brightness", &settings.brightness, 0.5f, 2.f, "%.2f");
}

void PauseMenu::DrawTimeSettings(GameSettings& settings, const PauseMenuContext& context)
{
	if (context.timeOfDay)
	{
		int minutes = static_cast<int>(*context.timeOfDay * 60.f) % (24 * 60);
		ImGui::Text("It's %02d:%02d", minutes / 60, minutes % 60);
		ImGui::SliderFloat("Time of day", context.timeOfDay, 0.f, 23.99f, "%.1f h");
	}
	ImGui::Checkbox("Day and night cycle", &settings.dayNightCycle);
	ImGui::BeginDisabled(!settings.dayNightCycle);
	ImGui::SliderFloat("Length of a day", &settings.dayLengthMinutes, 5.f, 240.f, "%.0f min");
	ImGui::EndDisabled();
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + MENU_WIDTH * 1.6f - 48.f);
	ImGui::TextDisabled("The sun moves across the sky and the street lights come on at dusk. The shop lamps are always on.");
	ImGui::PopTextWrapPos();
}

PauseMenu::Action PauseMenu::Draw(GameSettings& settings, const PauseMenuContext& context)
{
	if (!m_open)
		return Action::NONE;

	// Dim the shop behind the menu.
	ImGuiIO& io = ImGui::GetIO();
	ImGui::GetBackgroundDrawList()->AddRectFilled({ 0.f, 0.f }, io.DisplaySize, IM_COL32(0, 0, 0, 140));

	ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f }, ImGuiCond_Always, { 0.5f, 0.5f });
	bool wide = m_page == Page::CONTROLS || m_page == Page::SETTINGS;
	ImGui::SetNextWindowSize({ wide ? MENU_WIDTH * 1.6f : MENU_WIDTH, 0.f });
	// Keep the menu in front, but not while one of its dropdowns is open: taking focus every frame
	// would close the dropdown as soon as it opened.
	if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
		ImGui::SetNextWindowFocus();

	Action action = Action::NONE;
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 24.f, 20.f });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 8.f, 10.f });

	if (ImGui::Begin("Pause Menu", nullptr, flags))
	{
		switch (m_page)
		{
			case Page::MAIN:
				ImGui::SetWindowFontScale(1.5f);
				ImGui::TextUnformatted("Paused");
				ImGui::SetWindowFontScale(1.f);
				ImGui::Separator();
				if (BigButton("Resume"))
					action = Action::RESUME;
				if (BigButton("Save Game"))
					action = Action::SAVE;
				ImGui::BeginDisabled(!context.hasSave);
				if (BigButton("Load Game"))
					m_page = Page::CONFIRM_LOAD;
				ImGui::EndDisabled();
				if (context.hasSave && !context.lastSaved.empty())
					ImGui::TextDisabled("Last saved %s", context.lastSaved.c_str());
				if (BigButton("New Game"))
					m_page = Page::CONFIRM_NEW;
				if (BigButton("Settings"))
					m_page = Page::SETTINGS;
				if (BigButton("Controls"))
					m_page = Page::CONTROLS;
				if (BigButton("Quit to Desktop"))
					m_page = Page::CONFIRM_QUIT;
				if (!m_status.empty())
				{
					ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + MENU_WIDTH - 48.f);
					ImGui::TextColored({ 1.f, 0.85f, 0.4f, 1.f }, "%s", m_status.c_str());
					ImGui::PopTextWrapPos();
				}
				break;

			case Page::SETTINGS:
				ImGui::TextUnformatted("Settings");
				ImGui::Separator();
				if (ImGui::BeginTabBar("SettingsTabs"))
				{
					if (ImGui::BeginTabItem("Game"))
					{
						DrawGameSettings(settings);
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Video"))
					{
						DrawVideoSettings(settings, context);
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Time of day"))
					{
						DrawTimeSettings(settings, context);
						ImGui::EndTabItem();
					}
					ImGui::EndTabBar();
				}
				ImGui::Spacing();
				if (BigButton("Back"))
				{
					settings.Save();
					m_page = Page::MAIN;
				}
				break;

			case Page::CONTROLS:
				ImGui::TextUnformatted("Controls");
				ImGui::Separator();
				if (ImGui::BeginTable("Controls", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
				{
					for (const auto& control : CONTROLS)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored({ 1.f, 0.85f, 0.4f, 1.f }, "%s", control[0]);
						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(control[1]);
					}
					ImGui::EndTable();
				}
				if (BigButton("Back"))
					m_page = Page::MAIN;
				break;

			case Page::CONFIRM_QUIT:
				ImGui::TextUnformatted("Quit to desktop?");
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + MENU_WIDTH - 48.f);
				ImGui::TextDisabled("Anything since your last save will be lost unless you save now.");
				ImGui::PopTextWrapPos();
				ImGui::Spacing();
				if (BigButton("Save and Quit"))
					action = Action::SAVE_AND_QUIT;
				if (BigButton("Quit Without Saving"))
					action = Action::QUIT;
				if (BigButton("Cancel"))
					m_page = Page::MAIN;
				break;

			case Page::CONFIRM_LOAD:
				ImGui::TextUnformatted("Load the saved game?");
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + MENU_WIDTH - 48.f);
				ImGui::TextDisabled("Anything since your last save will be lost.");
				ImGui::PopTextWrapPos();
				ImGui::Spacing();
				if (BigButton("Load"))
				{
					action = Action::LOAD;
					m_page = Page::MAIN;
				}
				if (BigButton("Cancel"))
					m_page = Page::MAIN;
				break;

			case Page::CONFIRM_NEW:
				ImGui::TextUnformatted("Start a new career?");
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + MENU_WIDTH - 48.f);
				ImGui::TextDisabled("You start again with a worn-out core and $4000. Your save file is kept until you save over it.");
				ImGui::PopTextWrapPos();
				ImGui::Spacing();
				if (BigButton("Start Over"))
				{
					action = Action::NEW_GAME;
					m_page = Page::MAIN;
				}
				if (BigButton("Cancel"))
					m_page = Page::MAIN;
				break;
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(2);

	if (action == Action::RESUME)
		Close();
	return action;
}
