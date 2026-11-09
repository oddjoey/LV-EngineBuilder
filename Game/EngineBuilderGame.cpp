#include "EngineBuilderGame.h"

#include <cmath>
#include <format>

#include <glm/gtc/quaternion.hpp>

#include <imgui.h>

#include "LV/Engine/Lordi.h"
#include "LV/Engine/Graphics/IMGUI/IMGUIManager.h"
#include "LV/Engine/Objects/CameraManager.h"
#include "LV/Engine/Utility/InputManager.h"
#include "LV/Engine/Utility/PerformanceManager.h"
#include "Controls.h"
#include "FrameProfiler.h"
#include "Save/SaveGame.h"
#include "UI/MonkeyUI.h"
#include "UI/RankUI.h"
#include "World/GarageLayout.h"

namespace
{
	// The interface's colours: ImGui's own dark style, or (dark mode) a deeper, neutral near-black with
	// amber for what's ticked and dragged. Colours are given as they should look and taken back to
	// linear, as the interface is drawn into an sRGB target.
	void ApplyTheme(bool dark)
	{
		ImGui::StyleColorsDark();
		if (!dark)
			return;
		auto shade = [](int r, int g, int b, int a = 255) {
			auto linear = [](int value) { return static_cast<float>(std::pow(value / 255.0, 2.2)); };
			return ImVec4(linear(r), linear(g), linear(b), a / 255.f);
		};
		ImVec4* colors = ImGui::GetStyle().Colors;
		colors[ImGuiCol_Text] = shade(226, 224, 220);
		colors[ImGuiCol_TextDisabled] = shade(128, 126, 122);
		colors[ImGuiCol_WindowBg] = shade(17, 17, 19, 245);
		colors[ImGuiCol_ChildBg] = shade(21, 21, 24, 0);
		colors[ImGuiCol_PopupBg] = shade(22, 22, 25, 250);
		colors[ImGuiCol_Border] = shade(52, 52, 58);
		colors[ImGuiCol_FrameBg] = shade(34, 34, 38);
		colors[ImGuiCol_FrameBgHovered] = shade(50, 50, 56);
		colors[ImGuiCol_FrameBgActive] = shade(64, 64, 72);
		colors[ImGuiCol_TitleBg] = shade(14, 14, 16);
		colors[ImGuiCol_TitleBgActive] = shade(26, 26, 30);
		colors[ImGuiCol_MenuBarBg] = shade(21, 21, 24);
		colors[ImGuiCol_ScrollbarBg] = shade(15, 15, 17);
		colors[ImGuiCol_ScrollbarGrab] = shade(58, 58, 64);
		colors[ImGuiCol_ScrollbarGrabHovered] = shade(76, 76, 84);
		colors[ImGuiCol_ScrollbarGrabActive] = shade(96, 96, 106);
		colors[ImGuiCol_CheckMark] = shade(232, 176, 82);
		colors[ImGuiCol_SliderGrab] = shade(150, 148, 144);
		colors[ImGuiCol_SliderGrabActive] = shade(232, 176, 82);
		colors[ImGuiCol_Button] = shade(44, 44, 50);
		colors[ImGuiCol_ButtonHovered] = shade(64, 64, 72);
		colors[ImGuiCol_ButtonActive] = shade(84, 84, 94);
		colors[ImGuiCol_Header] = shade(46, 46, 52);
		colors[ImGuiCol_HeaderHovered] = shade(64, 64, 72);
		colors[ImGuiCol_HeaderActive] = shade(84, 84, 94);
		colors[ImGuiCol_Separator] = shade(52, 52, 58);
		colors[ImGuiCol_SeparatorHovered] = shade(90, 90, 100);
		colors[ImGuiCol_SeparatorActive] = shade(232, 176, 82);
		colors[ImGuiCol_ResizeGrip] = shade(52, 52, 58);
		colors[ImGuiCol_ResizeGripHovered] = shade(90, 90, 100);
		colors[ImGuiCol_ResizeGripActive] = shade(232, 176, 82);
		colors[ImGuiCol_Tab] = shade(28, 28, 32);
		colors[ImGuiCol_TabHovered] = shade(64, 64, 72);
		colors[ImGuiCol_TabSelected] = shade(46, 46, 52);
		colors[ImGuiCol_TabSelectedOverline] = shade(232, 176, 82);
		colors[ImGuiCol_TabDimmed] = shade(22, 22, 25);
		colors[ImGuiCol_TabDimmedSelected] = shade(36, 36, 40);
		colors[ImGuiCol_PlotHistogram] = shade(232, 176, 82);
		colors[ImGuiCol_TableHeaderBg] = shade(28, 28, 32);
		colors[ImGuiCol_TableBorderStrong] = shade(52, 52, 58);
		colors[ImGuiCol_TableBorderLight] = shade(38, 38, 42);
		colors[ImGuiCol_TableRowBgAlt] = shade(255, 255, 255, 8);
		colors[ImGuiCol_TextSelectedBg] = shade(232, 176, 82, 90);
		colors[ImGuiCol_NavCursor] = shade(232, 176, 82);
	}
}

EngineBuilderGame::EngineBuilderGame()
{
	m_workshop = std::make_unique<Workshop>(EngineSpec::InlineFour(), Workshop::StartingMoney(EngineSpec::InlineFour()));
	m_settings = GameSettings::Load();
	m_savedSettings = m_settings;
}

EngineBuilderGame::~EngineBuilderGame()
{
}

void EngineBuilderGame::OnLoadAssets()
{
	m_assets.Load();
	m_world.Build(m_assets);
	m_lighting.Build(m_assets);

	m_player.SetPosition(m_world.GetSpawnPosition());
	m_player.SetYaw(m_world.GetSpawnYaw());

	auto cameraManager = CameraManager::Get();
	Camera cameraInfo = {};
	cameraInfo.SetNearPlane(0.03f)->SetFarPlane(m_settings.renderDistance);
	m_camera = cameraManager->CreateCamera(cameraInfo);
	cameraManager->SetActiveCamera(m_camera);

	ApplySettings();
	m_player.ApplyToCamera(*m_camera);

	m_ui.SetPartAvailability([this](uint32_t partId) { return !m_world.IsPackaged(partId); });
	// The backpack, a tab beside it to dress your monkey, and one for the shop's rank.
	m_ui.SetBackpackContents([this] {
		if (!ImGui::BeginTabBar("BackpackTabs"))
			return;
		// Your monkey down the left, as you're dressed, then the backpack.
		if (ImGui::BeginTabItem("Backpack"))
		{
			if (ImGui::BeginChild("Monkey", { 270.f, 0.f }, ImGuiChildFlags_Borders))
				MonkeyUI::Draw(m_settings.monkey, [this](const std::string& name) -> uint32_t {
					try { return m_assets.Model(name); }
					catch (const std::exception&) { return 0; }
				}, m_workshop->SkillLevel(SkillKind::WARDROBE) > 0, m_workshop->SkillLevel(SkillKind::PREMIUM_COLORS) > 0);
			ImGui::EndChild();
			ImGui::SameLine();
			m_inventoryUI.DrawContents(m_world, *m_workshop, m_player);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Stats"))
		{
			m_inventoryUI.CancelDrag();
			m_inventoryUI.DrawPlayerStats(m_world, *m_workshop);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(m_workshop->SkillPoints() > 0 ? std::format("Rank & Skills ({})###Rank", m_workshop->SkillPoints()).c_str() : "Rank & Skills###Rank"))
		{
			m_inventoryUI.CancelDrag();
			RankUI::Draw(*m_workshop);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	});
	m_ui.SetBenchContents([this] { return m_world.GetBenchItems(); });
	auto modelLookup = [this](const std::string& name) -> uint32_t 
	{
		try { return m_assets.Model(name); }
		catch (const std::exception&) { return 0; }
	};

	m_ui.SetModelLookup(modelLookup);
	m_manual.SetModelLookup(modelLookup);
}

void EngineBuilderGame::ApplySettings()
{
	Controls::Set(m_settings.controls);
	// Once the interface is up (the first settings come before it).
	if (m_appliedDarkMode != m_settings.darkMode && ImGui::GetCurrentContext())
	{
		ApplyTheme(m_settings.darkMode);
		m_appliedDarkMode = m_settings.darkMode;
	}
	m_player.SetMouseSensitivity(m_settings.mouseSensitivity);
	m_player.SetInvertY(m_settings.invertY);
	if (m_camera)
	{
		m_camera->SetFov(m_settings.fieldOfView);
		m_camera->SetFarPlane(m_settings.renderDistance);
	}
	auto units = m_settings.imperialUnits ? Units::System::IMPERIAL : Units::System::METRIC;
	m_ui.SetUnits(units);
	m_world.SetUnits(units);
	m_inventoryUI.SetUnits(units);
	m_world.SetFieldOfView(m_settings.fieldOfView);
	m_world.SetViewModelSettings({ m_settings.viewArm, m_settings.viewBody,
		{ m_settings.viewHandRight, m_settings.viewHandUp, m_settings.viewHandForward }, m_settings.viewBodyBack });
}

void EngineBuilderGame::ApplyVideoSettings(bool force)
{
	const auto& video = m_settings;
	const auto& applied = m_appliedVideo;
	auto graphics = Graphics::Get();

	bool displayChanged = video.displayMode != applied.displayMode || video.resolutionWidth != applied.resolutionWidth
		|| video.resolutionHeight != applied.resolutionHeight;
	if (force || displayChanged)
		graphics->SetDisplayMode(static_cast<DisplayMode>(video.displayMode), video.resolutionWidth, video.resolutionHeight);
	if (force || video.vsync != applied.vsync)
		graphics->SetVSync(video.vsync ? VSyncOptions::ENABLED : VSyncOptions::NONE);
	if (force || video.shadowQuality != applied.shadowQuality || video.shadowDistance != applied.shadowDistance)
		Renderer::Get()->SetShadowSettings({ video.ShadowResolution(), video.shadowDistance });
	if (force || video.dlss != applied.dlss)
		graphics->SetDLSSMode(static_cast<DLSSMode>(video.dlss));
	if (force || video.frameLimit != applied.frameLimit)
		Lordi::Get()->SetFrameLimit(video.frameLimit);

	m_appliedVideo = m_settings;
}

void EngineBuilderGame::OnStart()
{
	m_input = InputManager::Get();
	m_input->SetCursorLocked(true);
	// LV_PROFILE=1 in the environment starts with the frame profiler on.
#ifdef _WIN32
	char* profile = nullptr;
	size_t length = 0;
	if (_dupenv_s(&profile, &length, "LV_PROFILE") == 0 && profile)
	{
		FrameProfiler::Get().SetEnabled(profile[0] == '1');
		free(profile);
	}
#else
	if (const char* profile = std::getenv("LV_PROFILE"))
		FrameProfiler::Get().SetEnabled(profile[0] == '1');
#endif
	ApplyVideoSettings(true);
	// Lordi's debug panels live in the dev menu (F3) instead of their own Home window.
	IMGUIManager::Get()->SetDebugWindowEnabled(false);
	RegisterConsoleCommands();
	// Every sound is made here, once, before the first frame.
	m_audio.Init();

	// The game is played in a garage on a server: it starts on the server list, over the menu's view of the world.
	ReturnToMenu();
	m_pauseMenu.Open();
}

void EngineBuilderGame::OnUpdate(float deltaTime)
{
	// The renderer's own timings for the frame before, alongside the game's.
	auto& profiler = FrameProfiler::Get();
	if (profiler.IsEnabled())
	{
		auto performance = PerformanceManager::Get();
		profiler.Add("render: record", performance->GetGraphicsCommandBufferDuration().count());
		profiler.Add("render: imgui", performance->GetIMGUICommandBufferDuration().count());
		profiler.Add("render: wait for GPU", performance->GetCPUWaitForGPUDuration().count());
	}
	profiler.EndFrame();
	PROFILE_SCOPE("game update");

	// The studio card comes first: until it's gone, keys only skip it.
	bool splash = m_splash.IsShowing();
	if (splash)
		m_splash.Update(deltaTime, m_input->WasKeyPressed(GLFW_KEY_ESCAPE) || m_input->WasKeyPressed(GLFW_KEY_SPACE)
			|| m_input->WasKeyPressed(GLFW_KEY_ENTER) || m_input->WasKeyPressed(GLFW_KEY_E) || m_input->WasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT));

	// Don't steal Tab/Esc from a text field the player is typing in (and none of it while the
	// splash is up, or while a control is waiting for its new key).
	bool typing = ImGui::GetIO().WantTextInput || splash || m_pauseMenu.IsBinding();
	// ` opens and closes the console, even while typing in it; Esc closes it first.
	if (!splash && m_input->WasKeyPressed(GLFW_KEY_GRAVE_ACCENT) && (!typing || m_console.IsOpen()))
		m_console.Toggle();
	else if (m_console.IsOpen() && m_input->WasKeyPressed(GLFW_KEY_ESCAPE))
		m_console.Close();
	else if (!typing)
	{
		if (m_input->WasKeyPressed(GLFW_KEY_ESCAPE))
		{
			if (m_manual.IsOpen())
				m_manual.Close();
			else if (m_world.IsMovingStation())
				m_world.CancelMove();
			else if (m_devMenu.IsOpen())
				m_devMenu.Close();
			else if (m_ui.IsOpen())
				m_ui.Close();
			else if (m_pauseMenu.IsOpen())
			{
				// Out of a garage the menu stays up: behind it is just the view of the world.
				if (!InMenu())
					m_pauseMenu.Close();
			}
			else
				m_pauseMenu.Open();
		}
		else if (Controls::Pressed(*m_input, Control::BACKPACK) && !m_pauseMenu.IsOpen() && !m_world.IsMovingStation())
		{
			// Tab is the backpack, and closes whatever workshop screen is open. Stations (the bench,
			// machines, store) open with E.
			if (m_manual.IsOpen())
				m_manual.Close();
			else if (m_ui.IsOpen())
				m_ui.Close();
			else
				m_ui.Open(WorkshopTab::BACKPACK);
		}
		else if (Controls::Pressed(*m_input, Control::THIRD_PERSON) && !InMenu())
			m_world.CycleView();
		else if (DEV_MENU && Controls::Pressed(*m_input, Control::DEV_MENU))
			m_devMenu.Toggle();
	}

	if (!m_ui.IsOpen())
		m_inventoryUI.CancelDrag();

	ApplySettings();
	ApplyVideoSettings(false);
	if (!m_pauseMenu.IsOpen() && !(m_settings == m_savedSettings))
	{
		m_settings.Save();
		m_savedSettings = m_settings;
	}

	bool walking = !splash && !InMenu() && !m_ui.IsOpen() && !m_manual.IsOpen() && !m_pauseMenu.IsOpen() && !m_devMenu.IsOpen() && !m_console.IsOpen();
	m_input->SetCursorLocked(walking);
	if (walking)
	{
		glm::vec3 before = m_player.GetFeetPosition();
		m_player.SetSpeedScale(static_cast<float>(m_workshop->WalkSpeedFactor()));
		m_player.Update(deltaTime, *m_input, m_world.GetObstacles(), m_world.GetRoomBounds(), m_world.GetWalkSurfaces());
		glm::vec3 step = m_player.GetFeetPosition() - before;
		m_workshop->AddDistanceWalked(glm::length(glm::vec2(step.x, step.z)));
	}
	// Out of a garage, the menu's view: a camera slowly circling the garage. In one, the player's eyes,
	// with their own arm and body in view, or (F5) over their shoulder.
	if (InMenu())
	{
		using namespace GarageLayout;
		m_menuAngle = std::fmod(m_menuAngle + deltaTime * MENU_ORBIT_SPEED, glm::two_pi<float>());
		glm::vec3 eye = MENU_ORBIT_CENTER + glm::vec3(std::sin(m_menuAngle) * MENU_ORBIT_RADIUS, MENU_ORBIT_HEIGHT, std::cos(m_menuAngle) * MENU_ORBIT_RADIUS);
		m_camera->SetPosition(eye);
		m_camera->SetRotation(glm::quatLookAt(glm::normalize(MENU_ORBIT_CENTER - eye), glm::vec3(0.f, 1.f, 0.f)));
	}
	else if (m_world.IsThirdPerson())
	{
		auto view = m_world.UpdateThirdPersonCamera(m_player);
		m_camera->SetPosition(view.position);
		m_camera->SetRotation(view.rotation);
	}
	else
		m_player.ApplyToCamera(*m_camera);
	m_world.SetViewModelVisible(!InMenu());

	if (m_devMenu.TakeAssembleRequest())
		m_workshop->DevAssembleEngine();

	m_ui.Update(deltaTime);
	m_world.SetEngineRpm(m_ui.GetLiveRpm(), m_ui.GetPullEngine());

	// As the player dressed it, once the shop has the Wardrobe perk; the shop's monkey until then.
	MonkeyLook worn = m_workshop->SkillLevel(SkillKind::WARDROBE) > 0 ? m_settings.monkey : MonkeyLook{};
	if (!(m_world.GetPlayerLook() == worn))
		m_world.SetPlayerLook(worn);
	if (auto request = m_world.Update(deltaTime, *m_workshop, m_player, *m_input, walking))
	{
		if (request->storeCategory)
			m_ui.OpenStore(*request->storeCategory);
		else if (request->tab == WorkshopTab::MACHINE)
			m_ui.OpenMachine(request->machineIndex);
		else
			m_ui.Open(request->tab);
		for (const auto& part : m_workshop->GetInventory())
			if (part->GetId() == request->partId)
				m_ui.SelectPart(*part, request->machineIndex);
	}

	// After the shop has updated: what was bought or done in the screens last frame has its 3D items and
	// deliveries by now, so it all goes out together. What the others did is laid out next frame.
	UpdateNetwork(deltaTime);

	// Turning the pages of the open manual.
	if (m_manual.IsOpen() && !typing)
	{
		int turn = (m_input->WasKeyPressed(GLFW_KEY_RIGHT) || Controls::Pressed(*m_input, Control::RIGHT) ? 1 : 0)
			- (m_input->WasKeyPressed(GLFW_KEY_LEFT) || Controls::Pressed(*m_input, Control::LEFT) ? 1 : 0);
		if (turn != 0 && m_manual.Turn(turn))
			m_audio.PlayInterface(Sound::PAGE_TURN, 0.7f);
	}

	// The service manual in hand, clicked: open the book.
	if (m_world.TakeReadRequest())
	{
		m_ui.Close();
		m_manual.Open();
		m_audio.PlayInterface(Sound::PAGE_TURN, 0.8f);
	}

	// The sun keeps moving, even with the menus open.
	// The dev menu can speed it up, or stop it. In a garage on a server it goes at the server's pace,
	// and the server's time is the one that counts.
	if (m_net.IsPlaying())
	{
		if (float dayLength = m_net.GetDayLengthMinutes(); dayLength > 0.f)
			m_timeOfDay = std::fmod(m_timeOfDay + (std::min)(deltaTime, 0.1f) * 24.f / (dayLength * 60.f), 24.f);
	}
	else if (m_settings.dayNightCycle && !m_devMenu.GetSettings().timePaused)
	{
		float hours = (std::min)(deltaTime, 0.1f) * 24.f / (m_settings.dayLengthMinutes * 60.f) * m_devMenu.GetSettings().timeScale;
		m_timeOfDay = std::fmod(m_timeOfDay + hours, 24.f);
	}
	m_lighting.Update(m_camera->GetPosition(), m_settings.renderDistance, m_timeOfDay, m_world.GetDoorOpenAmount(),
		m_settings.brightness, m_world.GetLampHeads(), m_world.GetInterior());
	{
		PROFILE_SCOPE("audio");
		m_audio.Update(deltaTime, m_player, m_world, *m_workshop, m_settings, m_timeOfDay);
	}
}

void EngineBuilderGame::ReturnToMenu()
{
	m_offline = false;
	m_workshop = std::make_unique<Workshop>(EngineSpec::InlineFour(), Workshop::StartingMoney(EngineSpec::InlineFour()));
	SaveGame::StartNew(*m_workshop, m_world, m_player);
	m_timeOfDay = 9.5f;
	Renderer::Get()->ResetTemporalHistory();
	m_inventoryUI.CancelDrag();
	m_ui.Close();
	m_manual.Close();
}

void EngineBuilderGame::PlayOffline()
{
	if (!DEV_OFFLINE || m_net.IsOnline())
		return;

	// The local save, as before there were servers, or a new shop if there isn't one.
	auto workshop = std::make_unique<Workshop>(EngineSpec::InlineFour(), Workshop::StartingMoney(EngineSpec::InlineFour()));
	std::string error;
	if (SaveGame::Exists() && SaveGame::Load(*workshop, m_world, m_player, m_timeOfDay, error))
		m_workshop = std::move(workshop);
	else
	{
		m_workshop = std::make_unique<Workshop>(EngineSpec::InlineFour(), Workshop::StartingMoney(EngineSpec::InlineFour()));
		SaveGame::StartNew(*m_workshop, m_world, m_player);
		m_timeOfDay = 9.5f;
	}
	m_offline = true;
	Renderer::Get()->ResetTemporalHistory();
	m_inventoryUI.CancelDrag();
	m_ui.Close();
	m_pauseMenu.Close();
	m_world.Notify(error.empty() ? "Playing offline (debug build)." : "Playing offline (debug build). The save couldn't be read: " + error);
}

bool EngineBuilderGame::SaveOffline()
{
	if (!m_offline)
		return false;
	std::string error;
	if (!SaveGame::Save(*m_workshop, m_world, m_player, m_timeOfDay, error))
	{
		m_pauseMenu.SetStatus(error);
		return false;
	}
	m_pauseMenu.SetStatus("Game saved.");
	return true;
}

void EngineBuilderGame::JoinGarage(const std::string& address, int port)
{
	if (m_net.IsOnline() || m_offline || !m_login.IsLoggedIn())
		return;
	// A ticket that's run out would only be turned away.
	if (m_login.HasExpired())
	{
		m_login.LogOut();
		m_pauseMenu.SetStatus("Your log in has run out: log in again.");
		return;
	}
	std::string error;
	if (!m_net.Join(address, static_cast<uint16_t>(port), m_login.GetTicket(), m_login.GetSignature(), error))
	{
		m_pauseMenu.SetStatus(error);
		return;
	}
	m_pauseMenu.SetStatus("");
}

void EngineBuilderGame::LogIn()
{
	if (m_login.IsWorking() || !CheckLogin())
		return;
	// The account server is the one named in servers.txt.
	m_servers.LoadList();
	m_login.Begin(m_servers.GetAccountAddress(), m_servers.GetAccountPort(), m_settings.username, m_pauseMenu.GetPassword(),
		m_pauseMenu.IsNewAccount());
	m_settings.Save();
	m_pauseMenu.SetStatus("");
}

void EngineBuilderGame::UpdateLogin(float deltaTime)
{
	bool working = m_login.IsWorking();
	m_login.Update(deltaTime);
	if (!working || m_login.IsWorking())
		return;
	if (m_login.IsLoggedIn())
	{
		// In: the password is forgotten, and the garages are asked how they are.
		m_pauseMenu.ClearPassword();
		m_pauseMenu.SetNewAccount(false);
		m_pauseMenu.SetStatus("");
		m_servers.Refresh();
	}
	else
		m_pauseMenu.SetStatus(m_login.GetError());
}

bool EngineBuilderGame::CheckLogin()
{
	// The server checks too; this just says what's wrong without a trip there.
	std::string problem = NetProtocol::UsernameProblem(m_settings.username);
	std::string password = m_pauseMenu.GetPassword();
	if (problem.empty() && password.empty())
		problem = "Type your password.";
	else if (problem.empty() && m_pauseMenu.IsNewAccount() && password.size() < NetProtocol::PASSWORD_MIN)
		problem = std::format("A password is at least {} characters long.", NetProtocol::PASSWORD_MIN);
	if (!problem.empty())
		m_pauseMenu.SetStatus(problem);
	return problem.empty();
}

void EngineBuilderGame::LeaveGarage()
{
	if (m_offline)
	{
		bool saved = SaveOffline();
		ReturnToMenu();
		m_pauseMenu.Open();
		m_pauseMenu.SetStatus(saved ? "Offline game saved." : "Left the offline game.");
		return;
	}
	bool playing = m_net.IsPlaying();
	m_net.Leave(m_world);
	if (playing)
		ReturnToMenu();
	m_pauseMenu.Open();
	m_pauseMenu.SetStatus(playing ? "Left the garage." : "Stopped joining.");
}

void EngineBuilderGame::UpdateNetwork(float deltaTime)
{
	UpdateLogin(deltaTime);
	m_servers.Update();
	m_net.Update(deltaTime, *m_workshop, m_world, m_player, m_timeOfDay);

	// The garage has arrived: the view leaves the menu's camera for the player's own eyes, at the
	// garage's door or where they last stood in it.
	if (m_net.TakeWelcome())
	{
		m_workshop = std::make_unique<Workshop>(EngineSpec::InlineFour(), Workshop::StartingMoney(EngineSpec::InlineFour()));
		SaveGame::StartNew(*m_workshop, m_world, m_player);
		m_net.ApplyWelcome(*m_workshop, m_world, m_player);
		Renderer::Get()->ResetTemporalHistory();
		m_inventoryUI.CancelDrag();
		m_ui.Close();
		m_pauseMenu.SetStatus("");
		m_pauseMenu.Close();
	}
	// Out of the garage (or never got in): back to the menu and the server list, with why.
	if (m_net.TakeDisconnected())
	{
		ReturnToMenu();
		m_pauseMenu.Open();
	}
	for (const auto& notice : m_net.TakeNotices())
	{
		m_world.Notify(notice);
		m_pauseMenu.SetStatus(notice);
	}
}

void EngineBuilderGame::HandlePauseAction(PauseMenu::Action action)
{
	switch (action)
	{
		case PauseMenu::Action::LOGIN:
			LogIn();
			break;
		case PauseMenu::Action::LOGOUT:
			m_login.LogOut();
			m_pauseMenu.SetStatus("Logged out.");
			break;
		case PauseMenu::Action::REFRESH_SERVERS:
			m_servers.Refresh();
			break;
		case PauseMenu::Action::JOIN:
			JoinGarage(m_pauseMenu.GetJoinAddress(), m_pauseMenu.GetJoinPort());
			break;
		case PauseMenu::Action::LEAVE:
			LeaveGarage();
			break;
		case PauseMenu::Action::PLAY_OFFLINE:
			PlayOffline();
			break;
		case PauseMenu::Action::SAVE:
			SaveOffline();
			break;
		case PauseMenu::Action::QUIT:
			// An offline game is kept: there's no server to keep it.
			SaveOffline();
			Lordi::Get()->RequestQuit();
			break;
		default:
			break;
	}
}

void EngineBuilderGame::OnGUI()
{
	PROFILE_SCOPE("game GUI");

	const auto& dev = m_devMenu.GetSettings();
	if (dev.showBounds)
		m_world.DrawDebugBounds(m_player);

	m_ui.SetXray(dev.xray);
	m_ui.Draw(*m_workshop);
	m_devMenu.Draw(m_timeOfDay);

	if (m_manual.Draw(m_settings.imperialUnits, m_settings.darkMode))
		m_audio.PlayInterface(Sound::PAGE_TURN, 0.7f);

	if (!InMenu() && !m_ui.IsOpen() && !m_manual.IsOpen() && !m_pauseMenu.IsOpen() && !m_console.IsOpen())
		m_world.DrawHud(*m_workshop, m_player);
	if (!m_pauseMenu.IsOpen())
		m_net.DrawNameTags(m_world, m_player);

	ImDrawList* foreground = ImGui::GetForegroundDrawList();
	float right = ImGui::GetIO().DisplaySize.x;

	if (!m_ui.IsOpen() && !m_pauseMenu.IsOpen() && !m_console.IsOpen())
		foreground->AddText({ right - 60.f, 32.f }, IM_COL32(220, 220, 220, 230), ShopLighting::ClockText(m_timeOfDay).c_str());
	if (m_settings.showFps)
		foreground->AddText({ right - 90.f, 12.f }, IM_COL32(200, 200, 200, 220), std::format("{:.0f} fps", ImGui::GetIO().Framerate).c_str());

	PauseMenuContext context;
	context.pressedInput = Controls::FirstPressed(*m_input);
	if (m_pauseMenu.IsOpen())
	{
		context.timeOfDay = &m_timeOfDay;
		context.resolutions = Graphics::Get()->GetDisplayResolutions();
		context.maxMsaa = (std::min)(8, static_cast<int>(Graphics::Get()->GetMaxMSAA()));

		auto graphics = Graphics::Get();
		context.dlssAvailable = graphics->IsDLSSAvailable();
		context.dlssStatus = graphics->GetDLSSStatus();

		if (graphics->IsUpscaling())
		{
			auto render = graphics->GetRenderExtent();
			context.renderSize = { render.width, render.height };

			ImVec2 display = ImGui::GetIO().DisplaySize;
			context.outputSize = { static_cast<uint32_t>(display.x), static_cast<uint32_t>(display.y) };
		}

		context.online = m_net.IsPlaying();
		context.joining = m_net.IsJoining();
		context.offline = m_offline;
		context.canPlayOffline = DEV_OFFLINE;
		context.loggedIn = m_login.IsLoggedIn();
		context.loggingIn = m_login.IsWorking();
		context.username = m_login.GetIdentity().username;
		context.server = m_net.GetServer();
		for (const auto& player : m_net.GetPlayers())
			context.players.push_back({ player.prefix, std::format("{}{}", player.name, player.local ? " (you)" : "") });

		// The server list: asked for the first time the menu shows it, once logged in.
		if (m_login.IsLoggedIn() && !m_servers.HasRefreshed() && !m_servers.IsRefreshing())
			m_servers.Refresh();
		context.refreshingServers = m_servers.IsRefreshing();
		for (const auto& server : m_servers.GetServers())
		{
			using State = ServerBrowser::Server::State;
			PauseMenuContext::Server row = { server.name, server.region, server.address, server.port };
			row.ping = server.state == State::ASKING ? "..."
				: server.state == State::OFFLINE ? "offline"
				: server.state == State::INCOMPATIBLE ? "other version"
				: std::format("{} ms", server.pingMs);
			if (server.state == State::ONLINE || server.state == State::FULL)
				row.players = std::format("{} / {}", server.players, server.maxPlayers);
			row.joinable = server.state == State::ONLINE;
			context.servers.push_back(row);
		}
	}

	HandlePauseAction(m_pauseMenu.Draw(m_settings, context));

	// Over everything else.
	m_console.Draw();
	m_splash.Draw();
}

void EngineBuilderGame::OnShutdown()
{
	m_settings.Save();
	if (m_input)
		m_input->SetCursorLocked(false);
}
