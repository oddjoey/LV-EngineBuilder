#include "PauseMenu.h"

#include <format>

#include <imgui.h>

#include "LV/Engine/Utility/InputManager.h"
#include "../Controls.h"
#include "../Net/NetProtocol.h"

namespace
{
	constexpr float MENU_WIDTH = 420.f;
	constexpr float BUTTON_HEIGHT = 40.f;

	// The controls that can't be changed (see Controls).
	const char* FIXED_CONTROLS[][2] =
	{
		{ "Mouse", "Look around" },
		{ "1 - 0, mouse wheel", "Take a hotbar item in hand / put it back" },
		{ "Esc", "Close screens / this menu" },
		{ "`", "Console" },
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
	m_binding = -1;
	m_bindingNote.clear();
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
	ImGui::Checkbox("Dark mode (menus, screens and the service manual)", &settings.darkMode);
	ImGui::Checkbox("Show FPS", &settings.showFps);

	// Your own arm and body in first person. The hand moves with what it holds.
	ImGui::SeparatorText("First person");
	ImGui::Checkbox("Show your arm", &settings.viewArm);
	ImGui::SameLine();
	ImGui::Checkbox("Show your body", &settings.viewBody);
	constexpr float REACH = GameSettings::VIEW_HAND_REACH * 100.f;
	auto centimetres = [](const char* label, float& metres, float min, float max) {
		float value = metres * 100.f;
		if (ImGui::SliderFloat(label, &value, min, max, "%.0f cm"))
			metres = value / 100.f;
	};
	centimetres("Hand left / right", settings.viewHandRight, -REACH, REACH);
	centimetres("Hand down / up", settings.viewHandUp, -REACH, REACH);
	centimetres("Hand near / far", settings.viewHandForward, -REACH, REACH);
	ImGui::BeginDisabled(!settings.viewBody);
	centimetres("Body behind your view", settings.viewBodyBack, GameSettings::VIEW_BODY_BACK_MIN * 100.f, GameSettings::VIEW_BODY_BACK_MAX * 100.f);
	ImGui::EndDisabled();
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + MENU_WIDTH * 1.6f - 48.f);
	ImGui::TextDisabled("The further back the body stands, the less of it you see looking down.");
	ImGui::PopTextWrapPos();
	if (ImGui::Button("Reset first person"))
	{
		GameSettings defaults;
		settings.viewArm = defaults.viewArm;
		settings.viewBody = defaults.viewBody;
		settings.viewHandRight = defaults.viewHandRight;
		settings.viewHandUp = defaults.viewHandUp;
		settings.viewHandForward = defaults.viewHandForward;
		settings.viewBodyBack = defaults.viewBodyBack;
	}
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

void PauseMenu::DrawAudioSettings(GameSettings& settings)
{
	// Shown as percentages, and heard straight away as they're dragged.
	auto volume = [](const char* label, float& value, const char* tip) 
	{
		int percent = static_cast<int>(std::round(value * 100.f));
		if (ImGui::SliderInt(label, &percent, 0, 100, "%d%%"))
			value = percent / 100.f;
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tip);
	};

	volume("Master volume", settings.masterVolume, "Everything the game plays.");
	volume("Effects", settings.effectsVolume, "Footsteps, tools, parts going on and coming off, machines, the dyno.");
	volume("Ambience", settings.ambienceVolume, "Wind and birds outside, the hum of the shop's lights.");
	volume("Interface", settings.interfaceVolume, "The till when you buy or sell, gauge beeps, machining cuts.");
}

void PauseMenu::DrawControls(GameSettings& settings, const PauseMenuContext& context)
{
	const ImVec4 gold = { 1.f, 0.85f, 0.4f, 1.f };
	// Waiting for a key: the first one pressed is it, Esc gives up, and the keys kept for something
	// else are turned down.
	if (m_binding >= 0 && context.pressedInput)
	{
		int pressed = *context.pressedInput;
		if (pressed == GLFW_KEY_ESCAPE)
		{
			m_binding = -1;
			m_bindingNote.clear();
		}
		else if (std::string reserved = Controls::Reserved(pressed); !reserved.empty())
			m_bindingNote = reserved;
		else
		{
			settings.controls[m_binding] = pressed;
			m_binding = -1;
			m_bindingNote.clear();
		}
	}

	ImGui::TextUnformatted("Controls");
	ImGui::Separator();
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + MENU_WIDTH * 1.6f - 48.f);
	if (m_binding >= 0)
		ImGui::TextColored(gold, "Press the key or mouse button for \"%s\" (Esc to cancel).", Controls::InfoOf(static_cast<Control>(m_binding)).name);
	else
		ImGui::TextDisabled("Click a control's key to change it, then press the key or mouse button you want.");
	if (!m_bindingNote.empty())
		ImGui::TextColored({ 1.f, 0.5f, 0.4f, 1.f }, "%s", m_bindingNote.c_str());
	ImGui::PopTextWrapPos();

	// Everything else is off while a key is awaited, so a mouse button pressed for it can't also
	// click something here.
	ImGui::BeginDisabled(m_binding >= 0);
	ImGui::BeginChild("ControlsList", { 0.f, ImGui::GetIO().DisplaySize.y * 0.55f });
	const char* group = "";
	bool table = false;
	for (size_t i = 0; i < Controls::COUNT; i++)
	{
		if (!Controls::Available(static_cast<Control>(i)))
			continue;
		const auto& info = Controls::InfoOf(static_cast<Control>(i));
		if (std::string(group) != info.group)
		{
			if (table)
				ImGui::EndTable();
			group = info.group;
			ImGui::SeparatorText(group);
			table = ImGui::BeginTable(group, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp);
			if (table)
			{
				ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 150.f);
				ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
			}
		}
		if (!table)
			continue;
		ImGui::PushID(static_cast<int>(i));
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		bool waiting = m_binding == static_cast<int>(i);
		std::string label = waiting ? "..." : Controls::Name(settings.controls[i]);
		if (ImGui::Button(label.c_str(), { 140.f, 0.f }))
		{
			m_binding = static_cast<int>(i);
			m_bindingNote.clear();
		}
		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted(info.name);
		// On the same key as something else: both still work, which is fine for things never done at
		// once, but worth knowing.
		std::string shared;
		for (size_t other = 0; other < Controls::COUNT; other++)
			if (other != i && settings.controls[other] == settings.controls[i])
				shared += std::string(shared.empty() ? "" : ", ") + Controls::InfoOf(static_cast<Control>(other)).name;
		if (!shared.empty())
		{
			ImGui::SameLine();
			ImGui::TextColored(gold, "(shared)");
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Also: %s", shared.c_str());
		}
		ImGui::PopID();
	}
	if (table)
		ImGui::EndTable();

	ImGui::SeparatorText("Always");
	if (ImGui::BeginTable("Fixed", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 150.f);
		ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
		for (const auto& control : FIXED_CONTROLS)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(gold, "%s", control[0]);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(control[1]);
		}
		ImGui::EndTable();
	}
	ImGui::EndChild();

	if (ImGui::Button("Reset to defaults"))
		settings.controls = Controls::Defaults();
	if (BigButton("Back"))
	{
		settings.Save();
		m_page = Page::MAIN;
	}
	ImGui::EndDisabled();
}

PauseMenu::Action PauseMenu::DrawMainMenu(GameSettings& settings, const PauseMenuContext& context)
{
	// The game is played in a garage on a server: until you're in one, this is the main menu, over the
	// camera circling the garage.
	Action action = Action::NONE;
	ImGui::SetWindowFontScale(1.5f);
	ImGui::TextUnformatted("LV: Engine Builder");
	ImGui::SetWindowFontScale(1.f);
	ImGui::Separator();
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + MENU_WIDTH * 1.6f - 48.f);

	// Logging in comes first: the garages are only shown to someone logged in.
	ImGui::SeparatorText("Account");
	if (!context.loggedIn)
	{
		ImGui::BeginDisabled(context.loggingIn);
		if (ImGui::RadioButton("Existing", !m_newAccount))
			m_newAccount = false;
		ImGui::SameLine();
		if (ImGui::RadioButton("New", m_newAccount))
			m_newAccount = true;
		char username[32] = {};
		settings.username.copy(username, sizeof(username) - 1);
		ImGui::SetNextItemWidth(-90.f);
		if (ImGui::InputText("Username", username, sizeof(username)))
			settings.username = username;
		ImGui::SetNextItemWidth(-90.f);
		bool entered = ImGui::InputText("Password", m_password, sizeof(m_password), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
		if (m_newAccount)
			ImGui::TextDisabled("A username is 3 to 20 letters, digits, - or _; a password at least 6 characters.");
		if (BigButton(context.loggingIn ? "Logging in..." : m_newAccount ? "Create account" : "Log in") || entered)
			action = Action::LOGIN;
		ImGui::EndDisabled();
	}
	else
	{
		ImGui::Text("Logged in as %s.", context.username.c_str());
		ImGui::SameLine();
		ImGui::BeginDisabled(context.joining);
		if (ImGui::SmallButton("Log out"))
			action = Action::LOGOUT;
		ImGui::EndDisabled();
	}

	if (context.loggedIn)
	{
		ImGui::BeginDisabled(context.joining);
		ImGui::SeparatorText("Garages");
		ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("Servers", 5, flags))
		{
			ImGui::TableSetupColumn("Server", ImGuiTableColumnFlags_WidthStretch, 3.f);
			ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthStretch, 1.6f);
			ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthStretch, 1.6f);
			ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthStretch, 1.f);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 50.f);
			ImGui::TableHeadersRow();
			for (size_t i = 0; i < context.servers.size(); i++)
			{
				const auto& server = context.servers[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(server.name.empty() ? server.address.c_str() : server.name.c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s:%d", server.address.c_str(), server.port);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(server.region.c_str());
				ImGui::TableNextColumn();
				if (server.joinable)
					ImGui::TextUnformatted(server.ping.c_str());
				else
					ImGui::TextDisabled("%s", server.ping.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(server.players.c_str());
				ImGui::TableNextColumn();
				ImGui::BeginDisabled(!server.joinable);
				if (ImGui::SmallButton("Join"))
				{
					m_joinAddress = server.address;
					m_joinPort = server.port;
					action = Action::JOIN;
				}
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (context.servers.empty())
			ImGui::TextDisabled("No servers listed. Add them to servers.txt beside the game.");
		ImGui::BeginDisabled(context.refreshingServers);
		if (ImGui::Button("Refresh"))
			action = Action::REFRESH_SERVERS;
		ImGui::EndDisabled();
		ImGui::EndDisabled();
	}

	if (context.choosingGarage)
	{
		// The first time on this server: a garage of one's own, or someone else's to ask to join.
		ImGui::SeparatorText("Choose your garage");
		ImGui::TextWrapped("It's your first time on %s. Make a garage of your own, on its own plot, or ask to join someone else's: "
			"its members who are online vote on letting you in.", context.server.c_str());
		ImGui::BeginDisabled(context.requestingGarage);
		if (BigButton("Make a new garage"))
			action = Action::CREATE_GARAGE;
		if (context.garageChoices.empty())
			ImGui::TextDisabled("No garages here yet: yours would be the first.");
		else if (ImGui::BeginTable("GarageChoices", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Garage", ImGuiTableColumnFlags_WidthStretch, 3.f);
			ImGui::TableSetupColumn("Members", ImGuiTableColumnFlags_WidthStretch, 1.6f);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 100.f);
			ImGui::TableHeadersRow();
			for (const auto& garage : context.garageChoices)
			{
				ImGui::PushID(garage.id);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(garage.name.c_str());
				ImGui::TableNextColumn();
				if (garage.members == 0)
					ImGui::TextDisabled("none: yours to take");
				else
					ImGui::Text("%d (%d online)", garage.members, garage.online);
				ImGui::TableNextColumn();
				if (ImGui::SmallButton(garage.members == 0 ? "Take it" : "Ask to join"))
				{
					m_requestedGarage = garage.id;
					action = Action::REQUEST_GARAGE;
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::EndDisabled();
		if (!context.garageStatus.empty())
			ImGui::TextColored({ 1.f, 0.85f, 0.4f, 1.f }, "%s", context.garageStatus.c_str());
		if (context.requestingGarage && BigButton("Take back the request"))
			action = Action::CANCEL_REQUEST;
		if (BigButton("Leave the server"))
			action = Action::LEAVE;
	}
	else if (context.joining)
	{
		ImGui::TextColored({ 1.f, 0.85f, 0.4f, 1.f }, "Joining %s...", context.server.c_str());
		if (BigButton("Cancel"))
			action = Action::LEAVE;
	}
	if (!m_status.empty())
		ImGui::TextColored({ 1.f, 0.85f, 0.4f, 1.f }, "%s", m_status.c_str());

	ImGui::Separator();
	if (context.canPlayOffline)
	{
		ImGui::BeginDisabled(context.joining);
		if (BigButton("Play offline (debug build)"))
			action = Action::PLAY_OFFLINE;
		ImGui::EndDisabled();
	}
	// Settings, controls and quitting side by side along the bottom.
	float third = (ImGui::GetContentRegionAvail().x - 2.f * ImGui::GetStyle().ItemSpacing.x) / 3.f;
	if (ImGui::Button("Settings", { third, BUTTON_HEIGHT }))
		m_page = Page::SETTINGS;
	ImGui::SameLine();
	if (ImGui::Button("Controls", { third, BUTTON_HEIGHT }))
		m_page = Page::CONTROLS;
	ImGui::SameLine();
	if (ImGui::Button("Quit to Desktop", { third, BUTTON_HEIGHT }))
		m_page = Page::CONFIRM_QUIT;
	ImGui::PopTextWrapPos();
	return action;
}

PauseMenu::Action PauseMenu::Draw(GameSettings& settings, const PauseMenuContext& context)
{
	if (!m_open)
		return Action::NONE;

	// Dim the shop behind the menu.
	ImGuiIO& io = ImGui::GetIO();
	ImGui::GetBackgroundDrawList()->AddRectFilled({ 0.f, 0.f }, io.DisplaySize, IM_COL32(0, 0, 0, 140));
	ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f }, ImGuiCond_Always, { 0.5f, 0.5f });

	bool mainMenu = m_page == Page::MAIN && !context.online && !context.offline;
	bool wide = m_page == Page::CONTROLS || m_page == Page::SETTINGS || mainMenu;
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
				if (!context.online && !context.offline)
				{
					action = DrawMainMenu(settings, context);
					break;
				}
				ImGui::SetWindowFontScale(1.5f);
				ImGui::TextUnformatted(context.online ? "Paused" : "Paused (offline)");
				ImGui::SetWindowFontScale(1.f);
				ImGui::Separator();
				if (BigButton("Resume"))
					action = Action::RESUME;
				if (context.online)
				{
					ImGui::SeparatorText(std::format("In the garage at {} ({})", context.server, context.players.size()).c_str());
					for (const auto& player : context.players)
					{
						ImGui::Bullet();
						if (!player.prefix.empty())
						{
							constexpr uint32_t RGB = NetProtocol::PREFIX_COLOR;
							ImGui::TextColored({ ((RGB >> 16) & 0xFF) / 255.f, ((RGB >> 8) & 0xFF) / 255.f, (RGB & 0xFF) / 255.f, 1.f }, "%s", player.prefix.c_str());
							ImGui::SameLine();
						}
						ImGui::TextUnformatted(player.name.c_str());
					}
					if (BigButton("Leave the garage"))
						action = Action::LEAVE;
					ImGui::TextDisabled("The garage is saved by its server.");
				}
				else
				{
					if (BigButton("Save"))
						action = Action::SAVE;
					if (BigButton("Back to the main menu"))
						action = Action::LEAVE;
					ImGui::TextDisabled("Offline, from the local save: a debug build only. It's saved when you leave.");
				}
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
					if (ImGui::BeginTabItem("Audio"))
					{
						DrawAudioSettings(settings);
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
				DrawControls(settings, context);
				break;

			case Page::CONFIRM_QUIT:
				ImGui::TextUnformatted("Quit to desktop?");
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + MENU_WIDTH - 48.f);
				ImGui::TextDisabled(context.online ? "You'll leave the garage. Its server keeps everything done in it."
					: context.offline ? "The offline game is saved first." : "See you next time.");
				ImGui::PopTextWrapPos();
				ImGui::Spacing();
				if (BigButton("Quit"))
					action = Action::QUIT;
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
