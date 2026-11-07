#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "LV/Engine/Networking/Socket.h"
#include "NetProtocol.h"

// The servers the game knows of, listed in servers.txt: the account server, and the garages and how
// close each one is. Each garage is asked what it is, and the time the answer takes to come back is
// its ping.
class ServerBrowser
{
public:
	static constexpr const char* FILE_NAME = "servers.txt";

	struct Server
	{
		enum class State { ASKING, ONLINE, FULL, OFFLINE, INCOMPATIBLE };

		std::string name = "";
		std::string region = "";
		std::string address = "";
		uint16_t port = 0;
		State state = State::ASKING;
		int pingMs = -1;
		int players = 0;
		int maxPlayers = 0;
		std::string error = "";		// why it's offline
	};

	// Reads the list again and asks every server on it.
	void Refresh();
	// Each frame: collects the answers.
	void Update();
	bool IsRefreshing() const { return !m_queries.empty(); }
	bool HasRefreshed() const { return m_refreshed; }
	const std::vector<Server>& GetServers() const { return m_servers; }
	// The account server, from the "account = address:port" line (this computer if there isn't one).
	// Read with the list: call LoadList first if it hasn't been refreshed.
	void LoadList() { Load(); }
	const std::string& GetAccountAddress() const { return m_accountAddress; }
	uint16_t GetAccountPort() const { return m_accountPort; }
private:
	using Clock = std::chrono::steady_clock;
	struct Query
	{
		size_t server = 0;
		std::unique_ptr<Net::Connection> connection = nullptr;
		Clock::time_point started = {};
		Clock::time_point asked = {};
		bool sent = false;
	};

	void Load();

	std::vector<Server>			m_servers = {};
	std::vector<Query>			m_queries = {};
	bool						m_refreshed = false;
	std::string					m_accountAddress = "";
	uint16_t					m_accountPort = NetProtocol::ACCOUNT_PORT;
};
