#include "EngineBuilderGame.h"

#include <cmath>
#include <format>

#include <imgui.h>

#include "LV/Engine/Lordi.h"
#include "LV/Engine/Graphics/IMGUI/IMGUIManager.h"
#include "LV/Engine/Objects/CameraManager.h"
#include "LV/Engine/Utility/InputManager.h"
#include "LV/Engine/Utility/PerformanceManager.h"
#include "FrameProfiler.h"
#include "Save/SaveGame.h"

EngineBuilderGame::EngineBuilderGame()
{
	m_workshop = std::make_unique<Workshop>(EngineSpec::InlineFour(), 4000.0);
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
	m_ui.SetBackpackContents([this] { m_inventoryUI.DrawContents(m_world, *m_workshop, m_player); });
	m_ui.SetBenchContents([this] { return m_world.GetBenchItems(); });
	m_ui.SetModelLookup([this](const std::string& name) -> uint32_t {
		try { return m_assets.Model(name); }
		catch (const std::exception&) { return 0; }
	});
}

void EngineBuilderGame::ApplySettings()
{
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
	char* profile = nullptr;
	size_t length = 0;
	if (_dupenv_s(&profile, &length, "LV_PROFILE") == 0 && profile)
	{
		FrameProfiler::Get().SetEnabled(profile[0] == '1');
		free(profile);
	}
	ApplyVideoSettings(true);
	// Lordi's debug panels live in the dev menu (F3) instead of their own Home window.
	IMGUIManager::Get()->SetDebugWindowEnabled(false);
	RegisterConsoleCommands();
	// Every sound is made here, once, before the first frame.
	m_audio.Init();

	// Carry on where the last session left off.
	if (SaveGame::Exists() && !LoadCareer())
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
	// splash is up).
	bool typing = ImGui::GetIO().WantTextInput || splash;
	// ` opens and closes the console, even while typing in it; Esc closes it first.
	if (!splash && m_input->WasKeyPressed(GLFW_KEY_GRAVE_ACCENT) && (!typing || m_console.IsOpen()))
		m_console.Toggle();
	else if (m_console.IsOpen() && m_input->WasKeyPressed(GLFW_KEY_ESCAPE))
		m_console.Close();
	else if (!typing)
	{
		if (m_input->WasKeyPressed(GLFW_KEY_ESCAPE))
		{
			if (m_world.IsMovingStation())
				m_world.CancelMove();
			else if (m_devMenu.IsOpen())
				m_devMenu.Close();
			else if (m_ui.IsOpen())
				m_ui.Close();
			else if (m_pauseMenu.IsOpen())
				m_pauseMenu.Close();
			else
				m_pauseMenu.Open();
		}
		else if (m_input->WasKeyPressed(GLFW_KEY_TAB) && !m_pauseMenu.IsOpen() && !m_world.IsMovingStation())
		{
			// Tab is the backpack, and closes whatever workshop screen is open. Stations (the bench,
			// machines, store) open with E.
			if (m_ui.IsOpen())
				m_ui.Close();
			else
				m_ui.Open(WorkshopTab::BACKPACK);
		}
		else if (m_input->WasKeyPressed(GLFW_KEY_F3))
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

	bool walking = !splash && !m_ui.IsOpen() && !m_pauseMenu.IsOpen() && !m_devMenu.IsOpen() && !m_console.IsOpen();
	m_input->SetCursorLocked(walking);
	if (walking)
	{
		glm::vec3 before = m_player.GetFeetPosition();
		m_player.Update(deltaTime, *m_input, m_world.GetObstacles(), m_world.GetRoomBounds(), m_world.GetWalkSurfaces());
		glm::vec3 step = m_player.GetFeetPosition() - before;
		m_workshop->AddDistanceWalked(glm::length(glm::vec2(step.x, step.z)));
	}
	m_player.ApplyToCamera(*m_camera);

	if (m_devMenu.TakeAssembleRequest())
		m_workshop->DevAssembleEngine();

	m_ui.Update(deltaTime);
	m_world.SetEngineRpm(m_ui.GetLiveRpm(), m_ui.GetPullEngine());

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

	// Bed: in the evening or the small hours, sleep through to seven and save; in the day, not yet.
	if (m_world.TakeSleepRequest())
	{
		if (m_timeOfDay >= 19.f || m_timeOfDay < 5.f)
		{
			m_timeOfDay = 7.f;
			m_world.Notify(SaveCareer() ? "You slept till 7:00. Game saved." : "You slept till 7:00.");
		}
		else
			m_world.Notify("It's too early to sleep. Come back after 19:00.");
	}

	// The sun keeps moving, even with the menus open.
	// The dev menu can speed it up, or stop it.
	if (m_settings.dayNightCycle && !m_devMenu.GetSettings().timePaused)
	{
		float hours = (std::min)(deltaTime, 0.1f) * 24.f / (m_settings.dayLengthMinutes * 60.f) * m_devMenu.GetSettings().timeScale;
		m_timeOfDay = std::fmod(m_timeOfDay + hours, 24.f);
	}
	m_lighting.Update(m_player.GetEyePosition(), m_settings.renderDistance, m_timeOfDay, m_world.GetDoorOpenAmount(),
		m_settings.brightness, m_world.GetLampHeads(), m_world.GetInterior());
	{
		PROFILE_SCOPE("audio");
		m_audio.Update(deltaTime, m_player, m_world, *m_workshop, m_settings, m_timeOfDay);
	}
}

bool EngineBuilderGame::SaveCareer()
{
	std::string error;
	if (!SaveGame::Save(*m_workshop, m_world, m_player, m_timeOfDay, error))
	{
		m_pauseMenu.SetStatus(error);
		return false;
	}
	m_pauseMenu.SetStatus("Game saved.");
	return true;
}

bool EngineBuilderGame::LoadCareer()
{
	// Load into a fresh workshop and only swap it in if the whole save was read.
	auto workshop = std::make_unique<Workshop>(EngineSpec::InlineFour(), 4000.0);
	std::string error;
	if (!SaveGame::Load(*workshop, m_world, m_player, m_timeOfDay, error))
	{
		m_pauseMenu.SetStatus(error);
		return false;
	}
	m_workshop = std::move(workshop);
	// The player may now be somewhere else entirely.
	Renderer::Get()->ResetTemporalHistory();
	m_inventoryUI.CancelDrag();
	m_ui.Close();
	m_pauseMenu.SetStatus("Game loaded.");
	return true;
}

void EngineBuilderGame::StartNewCareer()
{
	m_workshop = std::make_unique<Workshop>(EngineSpec::InlineFour(), 4000.0);
	SaveGame::StartNew(*m_workshop, m_world, m_player);
	m_timeOfDay = 9.5f;
	Renderer::Get()->ResetTemporalHistory();
	m_inventoryUI.CancelDrag();
	m_ui.Close();
}

void EngineBuilderGame::HandlePauseAction(PauseMenu::Action action)
{
	switch (action)
	{
		case PauseMenu::Action::SAVE:
			SaveCareer();
			break;
		case PauseMenu::Action::LOAD:
			if (LoadCareer())
				m_pauseMenu.Close();
			break;
		case PauseMenu::Action::NEW_GAME:
			StartNewCareer();
			m_pauseMenu.Close();
			break;
		case PauseMenu::Action::SAVE_AND_QUIT:
			if (SaveCareer())
				Lordi::Get()->RequestQuit();
			break;
		case PauseMenu::Action::QUIT:
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

	// The workshop window has its own units toggle; keep the setting in step with it.
	m_settings.imperialUnits = m_ui.GetUnits() == Units::System::IMPERIAL;

	if (!m_ui.IsOpen() && !m_pauseMenu.IsOpen() && !m_console.IsOpen())
		m_world.DrawHud(*m_workshop, m_player, m_settings.showControls);

	ImDrawList* foreground = ImGui::GetForegroundDrawList();
	float right = ImGui::GetIO().DisplaySize.x;
	if (!m_ui.IsOpen() && !m_pauseMenu.IsOpen() && !m_console.IsOpen())
		foreground->AddText({ right - 60.f, 32.f }, IM_COL32(220, 220, 220, 230), ShopLighting::ClockText(m_timeOfDay).c_str());
	if (m_settings.showFps)
		foreground->AddText({ right - 90.f, 12.f }, IM_COL32(200, 200, 200, 220), std::format("{:.0f} fps", ImGui::GetIO().Framerate).c_str());

	PauseMenuContext context;
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
		context.hasSave = SaveGame::Exists();
		context.lastSaved = context.hasSave ? SaveGame::LastSavedText() : "";
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
