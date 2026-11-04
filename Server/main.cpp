// LV-EngineServer: a dedicated server holding one garage for the players of LV: Engine Builder.
//
//   LV-EngineServer [--config server.cfg] [--port 27616]
//
// Runs until Ctrl+C (or the console closing), then saves the garage.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "GarageServer.h"

namespace
{
	std::atomic<bool> s_running = true;

	BOOL WINAPI OnConsoleEvent(DWORD)
	{
		s_running = false;
		// Closing the console only waits a few seconds: long enough to save.
		std::this_thread::sleep_for(std::chrono::seconds(3));
		return TRUE;
	}
}

int main(int argc, char** argv)
{
	std::string configPath = GarageServer::Config::FILE_NAME;
	int port = 0;
	for (int i = 1; i + 1 < argc; i += 2)
	{
		std::string option = argv[i];
		if (option == "--config")
			configPath = argv[i + 1];
		else if (option == "--port")
			port = std::atoi(argv[i + 1]);
	}

	auto config = GarageServer::Config::Load(configPath);
	if (port > 0)
		config.port = port;

	GarageServer server;
	std::string error;
	if (!server.Start(config, error))
	{
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	SetConsoleCtrlHandler(OnConsoleEvent, TRUE);

	// Every couple of milliseconds: changes are passed on as soon as they come in.
	auto last = std::chrono::steady_clock::now();
	while (s_running)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		auto now = std::chrono::steady_clock::now();
		float deltaTime = std::chrono::duration<float>(now - last).count();
		last = now;
		server.Update(deltaTime);
	}
	server.Stop();
	return 0;
}
