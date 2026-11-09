// LV-AccountServer: every player's account, for all the garage servers of LV: Engine Builder.
//
//   LV-AccountServer [--config account_server.cfg] [--port 27615]
//
// Runs until Ctrl+C (or the console closing), then saves the accounts.
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "AccountServer.h"

namespace
{
	std::atomic<bool> s_running = true;

#ifdef _WIN32
	BOOL WINAPI OnConsoleEvent(DWORD)
	{
		s_running = false;
		// Closing the console only waits a few seconds: long enough to save.
		std::this_thread::sleep_for(std::chrono::seconds(3));
		return TRUE;
	}
#else
	// Ctrl+C, or being told to stop: the loop finishes and it saves.
	void OnSignal(int)
	{
		s_running = false;
	}
#endif
}

int main(int argc, char** argv)
{
	std::string configPath = AccountServer::Config::FILE_NAME;
	int port = 0;
	for (int i = 1; i + 1 < argc; i += 2)
	{
		std::string option = argv[i];
		if (option == "--config")
			configPath = argv[i + 1];
		else if (option == "--port")
			port = std::atoi(argv[i + 1]);
	}

	auto config = AccountServer::Config::Load(configPath);
	if (port > 0)
		config.port = port;

	AccountServer server;
	std::string error;
	if (!server.Start(config, error))
	{
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
#ifdef _WIN32
	SetConsoleCtrlHandler(OnConsoleEvent, TRUE);
#else
	std::signal(SIGINT, OnSignal);
	std::signal(SIGTERM, OnSignal);
#endif

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
