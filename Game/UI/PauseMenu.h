#pragma once

#include <algorithm>
#include <optional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "../GameSettings.h"

// What the menu needs to know about the game besides the settings.
struct PauseMenuContext
{
	float* timeOfDay = nullptr;		// hours, editable from the Time of day settings
	std::vector<std::pair<uint32_t, uint32_t>> resolutions = {};
	int maxMsaa = 1;
	bool dlssAvailable = false;
	std::string dlssStatus = "";	// why DLSS can't be used, when it can't
	std::pair<uint32_t, uint32_t> renderSize = {};		// what DLSS renders at, 0 x 0 when it's off
	std::pair<uint32_t, uint32_t> outputSize = {};
	// Multiplayer
	struct Server
	{
		std::string name = "";
		std::string region = "";
		std::string address = "";
		int port = 0;
		std::string ping = "";		// "42 ms", or why there's no answer
		std::string players = "";	// "3 / 16"
		bool joinable = false;
	};
	bool online = false;			// playing in a garage on a server: the server saves it
	bool joining = false;			// connecting, or waiting for the garage to arrive
	std::string server = "";		// the garage's server, "address:port"
	std::vector<std::string> players = {};	// everyone in the garage
	std::vector<Server> servers = {};		// from the server list
	bool refreshingServers = false;
	bool loggedIn = false;			// to the account server: only then are the garages shown
	bool loggingIn = false;
	std::string username = "";		// who's logged in
	bool offline = false;			// playing the local save without a server (debug builds)
	bool canPlayOffline = false;	// a debug build: offline play is offered
	// The first key or mouse button pressed this frame (see Controls), for changing a binding.
	std::optional<int> pressedInput = std::nullopt;
};

// The menu. Out of a garage it's the main menu, and stays open: the account, the garages to join,
// settings, controls and quitting. In a garage it's the Esc menu, with the shop running behind it.
class PauseMenu
{
public:
	// SAVE and PLAY_OFFLINE are for offline play, in debug builds.
	enum class Action { NONE, RESUME, QUIT, LOGIN, LOGOUT, REFRESH_SERVERS, JOIN, LEAVE, PLAY_OFFLINE, SAVE };

	void Open();
	void Close();
	bool IsOpen() const;
	// Draws the menu if it's open. Changes to settings apply immediately and are saved on leaving
	// the settings page.
	Action Draw(GameSettings& settings, const PauseMenuContext& context);
	// A line under the menu buttons, e.g. why joining failed.
	void SetStatus(const std::string& status);
	// What's typed on the main menu to log in with (the username is in the settings). The password
	// is never saved, and is cleared once logged in.
	std::string GetPassword() const { return m_password; }
	void ClearPassword() { std::fill(std::begin(m_password), std::end(m_password), '\0'); }
	bool IsNewAccount() const { return m_newAccount; }
	void SetNewAccount(bool newAccount) { m_newAccount = newAccount; }
	// The garage picked to join (with Action::JOIN).
	const std::string& GetJoinAddress() const { return m_joinAddress; }
	int GetJoinPort() const { return m_joinPort; }
	// Waiting for the key to put a control on: the game leaves the keys alone meanwhile.
	bool IsBinding() const { return m_binding >= 0; }
private:
	enum class Page { MAIN, SETTINGS, CONTROLS, CONFIRM_QUIT };

	bool BigButton(const char* label);
	void DrawGameSettings(GameSettings& settings);
	void DrawVideoSettings(GameSettings& settings, const PauseMenuContext& context);
	void DrawAudioSettings(GameSettings& settings);
	void DrawTimeSettings(GameSettings& settings, const PauseMenuContext& context);
	Action DrawMainMenu(GameSettings& settings, const PauseMenuContext& context);
	void DrawControls(GameSettings& settings, const PauseMenuContext& context);

	bool m_open = false;
	Page m_page = Page::MAIN;
	std::string m_status = "";
	char m_password[72] = {};
	bool m_newAccount = false;
	std::string m_joinAddress = "";
	int m_joinPort = 0;
	// The control waiting for a key (an index into Controls), or -1; and why the last key pressed for
	// it couldn't be used.
	int m_binding = -1;
	std::string m_bindingNote = "";
};
