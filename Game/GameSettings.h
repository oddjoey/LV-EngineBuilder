#pragma once

#include <string>

// Player preferences, kept in a small key=value file next to the executable's working directory.
struct GameSettings
{
	float mouseSensitivity = 1.f;	// multiplier on the base look speed
	bool invertY = false;
	float fieldOfView = 70.f;		// degrees, vertical
	bool showControls = true;		// the key hint line at the bottom of the HUD
	bool showFps = false;
	bool imperialUnits = false;

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

	// Time of day
	bool dayNightCycle = true;
	float dayLengthMinutes = 30.f;	// real minutes for a full 24 hours

	static constexpr const char* FILE_NAME = "game_settings.cfg";

	static GameSettings Load();
	void Save() const;

	// Shadow map size for shadowQuality; 0 when shadows are off.
	unsigned int ShadowResolution() const;

	bool operator==(const GameSettings& other) const = default;
};
