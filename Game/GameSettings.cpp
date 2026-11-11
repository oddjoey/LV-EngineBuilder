#include "GameSettings.h"

#include "LV/Engine/Utility/InputManager.h"
#include "Net/NetProtocol.h"

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
		else if (key == "show_fps") value >> settings.showFps;
		else if (key == "imperial_units") value >> settings.imperialUnits;
		else if (key == "dark_mode") value >> settings.darkMode;
		else if (key == "monkey_fur") value >> std::hex >> settings.monkey.fur;
		else if (key == "monkey_skin") value >> std::hex >> settings.monkey.skin;
		else if (key == "monkey_cap") value >> std::hex >> settings.monkey.cap;
		else if (key == "monkey_tail") value >> settings.monkey.tail;
		else if (key == "view_arm") value >> settings.viewArm;
		else if (key == "view_body") value >> settings.viewBody;
		else if (key == "view_hand_right") value >> settings.viewHandRight;
		else if (key == "view_hand_up") value >> settings.viewHandUp;
		else if (key == "view_hand_forward") value >> settings.viewHandForward;
		else if (key == "view_body_back") value >> settings.viewBodyBack;
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
		else if (key == "master_volume") value >> settings.masterVolume;
		else if (key == "effects_volume") value >> settings.effectsVolume;
		else if (key == "ambience_volume") value >> settings.ambienceVolume;
		else if (key == "interface_volume") value >> settings.interfaceVolume;
		else if (key == "username") settings.username = line.substr(equals + 1);
		else if (key.starts_with("control_"))
		{
			for (size_t i = 0; i < Controls::COUNT; i++)
				if (key.substr(8) == Controls::InfoOf(static_cast<Control>(i)).id)
					value >> settings.controls[i];
		}
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
	settings.viewHandRight = std::clamp(settings.viewHandRight, -VIEW_HAND_REACH, VIEW_HAND_REACH);
	settings.viewHandUp = std::clamp(settings.viewHandUp, -VIEW_HAND_REACH, VIEW_HAND_REACH);
	settings.viewHandForward = std::clamp(settings.viewHandForward, -VIEW_HAND_REACH, VIEW_HAND_REACH);
	settings.viewBodyBack = std::clamp(settings.viewBodyBack, VIEW_BODY_BACK_MIN, VIEW_BODY_BACK_MAX);
	settings.username = settings.username.substr(0, NetProtocol::USERNAME_MAX);
	// Something that isn't a key or a mouse button (or is kept for something else) goes back to its default.
	for (size_t i = 0; i < Controls::COUNT; i++)
	{
		int& binding = settings.controls[i];
		bool key = binding >= GLFW_KEY_SPACE && binding <= GLFW_KEY_LAST;
		bool mouse = binding >= Controls::MOUSE && binding <= Controls::MOUSE + GLFW_MOUSE_BUTTON_LAST;
		if ((!key && !mouse) || !Controls::Reserved(binding).empty())
			binding = Controls::InfoOf(static_cast<Control>(i)).binding;
	}
	for (float* volume : { &settings.masterVolume, &settings.effectsVolume, &settings.ambienceVolume, &settings.interfaceVolume })
		*volume = std::clamp(*volume, 0.f, 1.f);
	return settings;
}

void GameSettings::Save() const
{
	std::ofstream file(FILE_NAME);
	file << "mouse_sensitivity=" << mouseSensitivity << "\n";
	file << "invert_y=" << invertY << "\n";
	file << "field_of_view=" << fieldOfView << "\n";
	file << "show_fps=" << showFps << "\n";
	file << "imperial_units=" << imperialUnits << "\n";
	file << "dark_mode=" << darkMode << "\n";
	file << "monkey_fur=" << std::hex << monkey.fur << "\n";
	file << "monkey_skin=" << monkey.skin << "\n";
	file << "monkey_cap=" << monkey.cap << std::dec << "\n";
	file << "monkey_tail=" << monkey.tail << "\n";
	file << "view_arm=" << viewArm << "\n";
	file << "view_body=" << viewBody << "\n";
	file << "view_hand_right=" << viewHandRight << "\n";
	file << "view_hand_up=" << viewHandUp << "\n";
	file << "view_hand_forward=" << viewHandForward << "\n";
	file << "view_body_back=" << viewBodyBack << "\n";
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
	file << "master_volume=" << masterVolume << "\n";
	file << "effects_volume=" << effectsVolume << "\n";
	file << "ambience_volume=" << ambienceVolume << "\n";
	file << "interface_volume=" << interfaceVolume << "\n";
	file << "username=" << username << "\n";
	for (size_t i = 0; i < Controls::COUNT; i++)
		file << "control_" << Controls::InfoOf(static_cast<Control>(i)).id << "=" << controls[i] << "\n";
}

unsigned int GameSettings::ShadowResolution() const
{
	constexpr unsigned int sizes[] = { 0, 1024, 2048, 4096 };
	return sizes[std::clamp(shadowQuality, 0, 3)];
}
