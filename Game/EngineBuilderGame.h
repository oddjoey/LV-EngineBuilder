#pragma once

#include <memory>
#include <optional>

#include "LV/Engine/Game.h"
#include "LV/Engine/Objects/Camera.h"
#include "Audio/GameAudio.h"
#include "GameSettings.h"
#include "Net/AccountLogin.h"
#include "Net/NetSession.h"
#include "Net/ServerBrowser.h"
#include "Simulation/Workshop.h"
#include "UI/Console.h"
#include "UI/DevMenu.h"
#include "UI/InventoryUI.h"
#include "UI/PauseMenu.h"
#include "UI/ServiceManual.h"
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
	// Out of any garage: the menu, over a camera circling an empty shop. Not in a garage (or offline).
	void ReturnToMenu();
	bool InMenu() const { return !m_net.IsPlaying() && !m_offline; }
	// Debug builds only: the local save played without a server, for working on the game.
	void PlayOffline();
	bool SaveOffline();
	// Finding a garage server, joining and leaving from the pause menu, and what the session reports.
	void JoinGarage(const std::string& address, int port);
	// Logging in to the account server, before any garage is shown: whether the username and password
	// typed could (saying why not on the menu), starting it, and seeing how it went.
	bool CheckLogin();
	void LogIn();
	void UpdateLogin(float deltaTime);
	void LeaveGarage();
	void UpdateNetwork(float deltaTime);
	// Everything the ` console can do.
	void RegisterConsoleCommands();

	std::unique_ptr<Workshop>		m_workshop = nullptr;
	GameSettings					m_settings = {};
	WorkshopUI						m_ui = {};
	PauseMenu						m_pauseMenu = {};
	ServiceManual					m_manual = {};
	DevMenu							m_devMenu = {};
	Console							m_console = {};
	SplashScreen					m_splash = {};
	InventoryUI						m_inventoryUI = {};
	ShopAssets						m_assets = {};
	ShopWorld						m_world = {};
	NetSession						m_net = {};
	ServerBrowser					m_servers = {};
	AccountLogin					m_login = {};
	// Playing the local save, with no server (debug builds only: see DEV_OFFLINE).
	bool							m_offline = false;
	// How far round the garage the menu's camera has come.
	float							m_menuAngle = 0.f;
// Debug builds only: playing offline, and the developer menu (F3).
#ifdef _DEBUG
	static constexpr bool			DEV_OFFLINE = true;
	static constexpr bool			DEV_MENU = true;
#else
	static constexpr bool			DEV_OFFLINE = false;
	static constexpr bool			DEV_MENU = false;
#endif
	ShopLighting					m_lighting = {};
	GameAudio						m_audio = {};
	float							m_timeOfDay = 9.5f;		// hours
	GameSettings					m_appliedVideo = {};
	// The interface's colours as last set: nothing yet, so the first settings set them.
	std::optional<bool>				m_appliedDarkMode = std::nullopt;
	GameSettings					m_savedSettings = {};
	FirstPersonController			m_player = {};
	pCamera							m_camera = nullptr;
	std::shared_ptr<InputManager>	m_input = nullptr;
};
