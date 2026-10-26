#include "LV/Engine/Lordi.h"
#include "Game/EngineBuilderGame.h"
#include "Game/GameSettings.h"

int main() 
{
	auto lordi = Lordi::Get();
	auto settings = lordi->GetSettings();

	// The window, vsync and MSAA come from the player's video settings. MSAA can only change
	// here, at startup; the rest is applied again once the game is running.
	auto video = GameSettings::Load();
	bool windowed = video.displayMode == static_cast<int>(DisplayMode::WINDOWED);

	GraphicsSettings graphicsSettings {};
	graphicsSettings.sWindowName = "LV: Engine Builder";
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