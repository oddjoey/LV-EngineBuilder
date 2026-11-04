#pragma once

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
};

// The Esc menu: resume, the garage servers, settings, controls and quit. The shop keeps running
// behind it. Until the player is in a garage it is the front screen, and stays open.
class PauseMenu
{
public:
	enum class Action { NONE, RESUME, QUIT, REFRESH_SERVERS, JOIN_CLOSEST, JOIN, LEAVE };

	void Open();
	// Opens straight onto the server list (keeping the status line, e.g. why the last garage was left).
	void OpenServers();
	void Close();
	bool IsOpen() const;
	// Draws the menu if it's open. Changes to settings apply immediately and are saved on leaving
	// the settings page.
	Action Draw(GameSettings& settings, const PauseMenuContext& context);
	// A line under the menu buttons, e.g. why joining failed.
	void SetStatus(const std::string& status);
	// What's typed on the server list to log in with (the username is in the settings). The password
	// is only kept while the game runs.
	std::string GetPassword() const { return m_password; }
	bool IsNewAccount() const { return m_newAccount; }
	void SetNewAccount(bool newAccount) { m_newAccount = newAccount; }
private:
	enum class Page { MAIN, SETTINGS, CONTROLS, CONFIRM_QUIT, MULTIPLAYER };

	bool BigButton(const char* label);
	void DrawGameSettings(GameSettings& settings);
	void DrawVideoSettings(GameSettings& settings, const PauseMenuContext& context);
	void DrawAudioSettings(GameSettings& settings);
	void DrawTimeSettings(GameSettings& settings, const PauseMenuContext& context);
	Action DrawMultiplayer(GameSettings& settings, const PauseMenuContext& context);

	bool m_open = false;
	Page m_page = Page::MAIN;
	std::string m_status = "";
	char m_password[72] = {};
	bool m_newAccount = false;
};
