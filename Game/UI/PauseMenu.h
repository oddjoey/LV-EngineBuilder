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
	bool hasSave = false;
	std::string lastSaved = "";		// when the save was written
};

// The Esc menu: resume, save/load, settings, controls and quit. The shop keeps running behind it.
class PauseMenu
{
public:
	enum class Action { NONE, RESUME, SAVE, LOAD, NEW_GAME, QUIT, SAVE_AND_QUIT };

	void Open();
	void Close();
	bool IsOpen() const;
	// Draws the menu if it's open. Changes to settings apply immediately and are saved on leaving
	// the settings page.
	Action Draw(GameSettings& settings, const PauseMenuContext& context);
	// A line under the menu buttons, e.g. "Game saved." or why loading failed.
	void SetStatus(const std::string& status);
private:
	enum class Page { MAIN, SETTINGS, CONTROLS, CONFIRM_QUIT, CONFIRM_LOAD, CONFIRM_NEW };

	bool BigButton(const char* label);
	void DrawGameSettings(GameSettings& settings);
	void DrawVideoSettings(GameSettings& settings, const PauseMenuContext& context);
	void DrawAudioSettings(GameSettings& settings);
	void DrawTimeSettings(GameSettings& settings, const PauseMenuContext& context);

	bool m_open = false;
	Page m_page = Page::MAIN;
	std::string m_status = "";
};
