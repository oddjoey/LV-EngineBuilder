#pragma once

#include <string>

#include "Controls.h"
#include "World/MonkeyLook.h"

// Player preferences, kept in a small key=value file next to the executable's working directory.
struct GameSettings
{
	float mouseSensitivity = 1.f;	// multiplier on the base look speed
	bool invertY = false;
	float fieldOfView = 70.f;		// degrees, vertical
	bool showFps = false;
	bool imperialUnits = false;
	bool darkMode = false;			// the menus, screens and the service manual in dark colours

	// First person: your own arm (and what it holds) and body
	bool viewArm = true;
	bool viewBody = true;
	float viewHandRight = 0.f;		// metres the hand is moved from where it's held: to the right,
	float viewHandUp = 0.f;			// up,
	float viewHandForward = 0.f;	// and out from you
	float viewBodyBack = 0.3f;		// metres the body stands behind your eyes: further back, less of it in view

	// Video
	int displayMode = 0;			// DisplayMode: windowed, borderless, fullscreen
	int resolutionWidth = 1920;
	int resolutionHeight = 1080;
	bool vsync = true;
	int msaa = 4;					// samples: 1, 2, 4 or 8; takes effect on restart
	int dlss = 0;					// DLSSMode: off, DLAA, quality, balanced, performance, ultra performance
	int shadowQuality = 2;			// 0 off, 1 low, 2 medium, 3 high
	float shadowDistance = 40.f;	// metres
	float renderDistance = 250.f;	// metres
	float brightness = 1.f;
	int frameLimit = 0;				// frames per second, 0 = unlimited

	// Audio: each from 0 (silent) to 1 (full)
	float masterVolume = 0.8f;
	float effectsVolume = 1.f;		// footsteps, tools, parts, machines, the dyno
	float ambienceVolume = 0.7f;	// wind, birds, the shop's hum
	float interfaceVolume = 0.8f;	// the till, gauge beeps, menu clicks

	// How your monkey looks (the backpack's Monkey tab).
	MonkeyLook monkey = {};

	// What each control is on (see Controls).
	Controls::Bindings controls = Controls::Defaults();

	// Multiplayer: the account you log in with (the password is never saved).
	std::string username = "";

	static constexpr const char* FILE_NAME = "game_settings.cfg";
	// How far the hand can be moved each way, and the body set back.
	static constexpr float VIEW_HAND_REACH = 0.15f;
	static constexpr float VIEW_BODY_BACK_MIN = 0.1f;
	static constexpr float VIEW_BODY_BACK_MAX = 0.6f;

	static GameSettings Load();
	void Save() const;

	// Shadow map size for shadowQuality; 0 when shadows are off.
	unsigned int ShadowResolution() const;

	bool operator==(const GameSettings& other) const = default;
};
