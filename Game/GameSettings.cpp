#include "GameSettings.h"

#include <algorithm>
#include <fstream>
#include <sstream>

GameSettings GameSettings::Load()
{
	GameSettings settings;
	std::ifstream file(FILE_NAME);
	std::string line;
	while (std::getline(file, line))
	{
		auto equals = line.find('=');
		if (equals == std::string::npos)
			continue;

		std::string key = line.substr(0, equals);
		std::stringstream value(line.substr(equals + 1));
		if (key == "mouse_sensitivity") value >> settings.mouseSensitivity;
		else if (key == "invert_y") value >> settings.invertY;
		else if (key == "field_of_view") value >> settings.fieldOfView;
		else if (key == "show_controls") value >> settings.showControls;
		else if (key == "show_fps") value >> settings.showFps;
		else if (key == "imperial_units") value >> settings.imperialUnits;
		else if (key == "display_mode") value >> settings.displayMode;
		else if (key == "resolution_width") value >> settings.resolutionWidth;
		else if (key == "resolution_height") value >> settings.resolutionHeight;
		else if (key == "vsync") value >> settings.vsync;
		else if (key == "msaa") value >> settings.msaa;
		else if (key == "dlss") value >> settings.dlss;
		else if (key == "shadow_quality") value >> settings.shadowQuality;
		else if (key == "shadow_distance") value >> settings.shadowDistance;
		else if (key == "render_distance") value >> settings.renderDistance;
		else if (key == "brightness") value >> settings.brightness;
		else if (key == "frame_limit") value >> settings.frameLimit;
		else if (key == "day_night_cycle") value >> settings.dayNightCycle;
		else if (key == "day_length_minutes") value >> settings.dayLengthMinutes;
	}

	// A hand-edited file shouldn't be able to break the camera or the window.
	settings.mouseSensitivity = std::clamp(settings.mouseSensitivity, 0.1f, 5.f);
	settings.fieldOfView = std::clamp(settings.fieldOfView, 50.f, 110.f);
	settings.displayMode = std::clamp(settings.displayMode, 0, 2);
	settings.resolutionWidth = std::clamp(settings.resolutionWidth, 800, 7680);
	settings.resolutionHeight = std::clamp(settings.resolutionHeight, 600, 4320);
	settings.msaa = settings.msaa >= 8 ? 8 : settings.msaa >= 4 ? 4 : settings.msaa >= 2 ? 2 : 1;
	settings.dlss = std::clamp(settings.dlss, 0, 5);
	settings.shadowQuality = std::clamp(settings.shadowQuality, 0, 3);
	settings.shadowDistance = std::clamp(settings.shadowDistance, 15.f, 120.f);
	settings.renderDistance = std::clamp(settings.renderDistance, 80.f, 500.f);
	settings.brightness = std::clamp(settings.brightness, 0.5f, 2.f);
	settings.frameLimit = std::clamp(settings.frameLimit, 0, 360);
	settings.dayLengthMinutes = std::clamp(settings.dayLengthMinutes, 5.f, 240.f);
	return settings;
}

void GameSettings::Save() const
{
	std::ofstream file(FILE_NAME);
	file << "mouse_sensitivity=" << mouseSensitivity << "\n";
	file << "invert_y=" << invertY << "\n";
	file << "field_of_view=" << fieldOfView << "\n";
	file << "show_controls=" << showControls << "\n";
	file << "show_fps=" << showFps << "\n";
	file << "imperial_units=" << imperialUnits << "\n";
	file << "display_mode=" << displayMode << "\n";
	file << "resolution_width=" << resolutionWidth << "\n";
	file << "resolution_height=" << resolutionHeight << "\n";
	file << "vsync=" << vsync << "\n";
	file << "msaa=" << msaa << "\n";
	file << "dlss=" << dlss << "\n";
	file << "shadow_quality=" << shadowQuality << "\n";
	file << "shadow_distance=" << shadowDistance << "\n";
	file << "render_distance=" << renderDistance << "\n";
	file << "brightness=" << brightness << "\n";
	file << "frame_limit=" << frameLimit << "\n";
	file << "day_night_cycle=" << dayNightCycle << "\n";
	file << "day_length_minutes=" << dayLengthMinutes << "\n";
}

unsigned int GameSettings::ShadowResolution() const
{
	constexpr unsigned int sizes[] = { 0, 1024, 2048, 4096 };
	return sizes[std::clamp(shadowQuality, 0, 3)];
}
