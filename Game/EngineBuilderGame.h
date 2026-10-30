#pragma once

#include <memory>

#include "LV/Engine/Game.h"
#include "LV/Engine/Objects/Camera.h"
#include "Audio/GameAudio.h"
#include "GameSettings.h"
#include "Simulation/Workshop.h"
#include "UI/Console.h"
#include "UI/DevMenu.h"
#include "UI/InventoryUI.h"
#include "UI/PauseMenu.h"
#include "UI/SplashScreen.h"
#include "UI/WorkshopUI.h"
#include "World/FirstPersonController.h"
#include "World/ShopAssets.h"
#include "World/ShopLighting.h"
#include "World/ShopWorld.h"

class InputManager;

// Walking the shop floor in first person (cursor captured), or one of the screens with the
// cursor free: the backpack (Tab), a station's workshop screen (E) or the pause menu (Esc). Esc backs out
// of whatever is open, or opens the pause menu.
class EngineBuilderGame : public Game
{
public:
	EngineBuilderGame();
	~EngineBuilderGame();

	void OnLoadAssets() override;
	void OnStart() override;
	void OnUpdate(float deltaTime) override;
	void OnGUI() override;
	void OnShutdown() override;
private:
	void ApplySettings();
	// Display mode, vsync, shadows and the frame cap: only touched when they change (or forced).
	void ApplyVideoSettings(bool force);
	void HandlePauseAction(PauseMenu::Action action);
	bool SaveCareer();
	bool LoadCareer();
	void StartNewCareer();
	// Everything the ` console can do.
	void RegisterConsoleCommands();

	std::unique_ptr<Workshop>		m_workshop = nullptr;
	GameSettings					m_settings = {};
	WorkshopUI						m_ui = {};
	PauseMenu						m_pauseMenu = {};
	DevMenu							m_devMenu = {};
	Console							m_console = {};
	SplashScreen					m_splash = {};
	InventoryUI						m_inventoryUI = {};
	ShopAssets						m_assets = {};
	ShopWorld						m_world = {};
	ShopLighting					m_lighting = {};
	GameAudio						m_audio = {};
	float							m_timeOfDay = 9.5f;		// hours
	GameSettings					m_appliedVideo = {};
	GameSettings					m_savedSettings = {};
	FirstPersonController			m_player = {};
	pCamera							m_camera = nullptr;
	std::shared_ptr<InputManager>	m_input = nullptr;
};
