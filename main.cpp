#include "LV/Engine/Lordi.h"
#include "Game/EngineBuilderGame.h"
#include "Game/GameSettings.h"

#include <cstdio>
#include <filesystem>
#ifdef _WIN32
#include <io.h>
#endif

namespace
{
	// A Release build on Windows has no console, so what it logs goes to game.log beside it instead. The run
	// before is kept as game.previous.log, so a crash's log is still there after starting the game again.
	void LogToFile()
	{
#if defined(_WIN32) && !defined(_DEBUG)
		std::error_code error;
		std::filesystem::rename("game.log", "game.previous.log", error);
		// Not freopen_s: that locks the file, and the log should be readable while the game's running.
#pragma warning(suppress: 4996)
		if (!std::freopen("game.log", "w", stdout))
			return;
		// Errors into the same file, at the same place.
		_dup2(_fileno(stdout), _fileno(stderr));
		// Written straight through, so nothing's lost if the game crashes.
		std::setvbuf(stdout, nullptr, _IONBF, 0);
		std::setvbuf(stderr, nullptr, _IONBF, 0);
#endif
	}
}

int main()
{
	LogToFile();

	auto lordi = Lordi::Get();
	auto settings = lordi->GetSettings();

	// The window, vsync and MSAA come from the player's video settings. MSAA can only change
	// here, at startup; the rest is applied again once the game is running.
	auto video = GameSettings::Load();
	bool windowed = video.displayMode == static_cast<int>(DisplayMode::WINDOWED);

	GraphicsSettings graphicsSettings{};
	graphicsSettings.sWindowName = "MonkeyKobo Motor Works";
	// Lordi is the LV submodule; its compiled shaders are loaded from there.
	graphicsSettings.sEngineRoot = "LV/";
	graphicsSettings.uiWindowWidth = windowed ? video.resolutionWidth : 1280;
	graphicsSettings.uiWindowHeight = windowed ? video.resolutionHeight : 720;
	graphicsSettings.eVSync = video.vsync ? VSyncOptions::ENABLED : VSyncOptions::NONE;
	graphicsSettings.eColorBlending = ColorBlendingOptions::NONE;
	graphicsSettings.eMSAA = video.msaa >= 8 ? MSAAOptions::X8 : video.msaa >= 4 ? MSAAOptions::X4 : video.msaa >= 2 ? MSAAOptions::X2 : MSAAOptions::X1;

	settings->SetGraphicsSettings(graphicsSettings);

	lordi->SetGame(std::make_shared<EngineBuilderGame>());

	lordi->Start();

	return 0;
}