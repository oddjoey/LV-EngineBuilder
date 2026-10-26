#include "EngineBuilderGame.h"

#include <cmath>
#include <format>

#include <imgui.h>

#include "LV/Engine/Lordi.h"
#include "LV/Engine/Objects/CameraManager.h"
#include "LV/Engine/Utility/InputManager.h"
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
	ApplyVideoSettings(true);

	// Carry on where the last session left off.
	if (SaveGame::Exists() && !LoadCareer())
		m_pauseMenu.Open();
}

void EngineBuilderGame::OnUpdate(float deltaTime)
{
	// Don't steal Tab/Esc from a text field the player is typing in.
	bool typing = ImGui::GetIO().WantTextInput;
	if (!typing)
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

	bool walking = !m_ui.IsOpen() && !m_pauseMenu.IsOpen() && !m_devMenu.IsOpen();
	m_input->SetCursorLocked(walking);
	if (walking)
		m_player.Update(deltaTime, *m_input, m_world.GetObstacles(), m_world.GetRoomBounds());
	m_player.ApplyToCamera(*m_camera);

	m_ui.Update(deltaTime);
	m_world.SetEngineRpm(m_ui.GetLiveRpm());

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

	// The sun keeps moving, even with the menus open.
	if (m_settings.dayNightCycle)
		m_timeOfDay = std::fmod(m_timeOfDay + (std::min)(deltaTime, 0.1f) * 24.f / (m_settings.dayLengthMinutes * 60.f), 24.f);
	m_lighting.Update(m_player.GetEyePosition(), m_settings.renderDistance, m_timeOfDay, m_world.GetDoorOpenAmount(),
		m_settings.brightness, m_world.GetLampHeads());
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
	const auto& dev = m_devMenu.GetSettings();
	if (dev.showBounds)
		m_world.DrawDebugBounds(m_player);
	m_ui.SetXray(dev.xray);
	m_ui.Draw(*m_workshop);
	m_devMenu.Draw();

	// The workshop window has its own units toggle; keep the setting in step with it.
	m_settings.imperialUnits = m_ui.GetUnits() == Units::System::IMPERIAL;

	if (!m_ui.IsOpen() && !m_pauseMenu.IsOpen())
		m_world.DrawHud(*m_workshop, m_player, m_settings.showControls);

	ImDrawList* foreground = ImGui::GetForegroundDrawList();
	float right = ImGui::GetIO().DisplaySize.x;
	if (!m_ui.IsOpen() && !m_pauseMenu.IsOpen())
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
}

void EngineBuilderGame::OnShutdown()
{
	m_settings.Save();
	if (m_input)
		m_input->SetCursorLocked(false);
}
