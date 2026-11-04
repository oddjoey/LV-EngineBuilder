#include "ServerBrowser.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>

#include "NetProtocol.h"
#include "../Save/SaveText.h"

namespace
{
	constexpr auto QUERY_TIMEOUT = std::chrono::seconds(3);

	const char* DEFAULT_LIST =
		"# Garage servers the game can join, one per line:\n"
		"#   address[:port] | region | name\n"
		"# \"Join the closest garage\" pings every one and joins the fastest with room.\n";

	std::string Trim(const std::string& text)
	{
		size_t first = text.find_first_not_of(" \t\r");
		size_t last = text.find_last_not_of(" \t\r");
		return first == std::string::npos ? "" : text.substr(first, last - first + 1);
	}
}

void ServerBrowser::Load()
{
	// The first time, an empty list explaining how to fill it in.
	if (!std::filesystem::exists(FILE_NAME))
		std::ofstream(FILE_NAME) << DEFAULT_LIST;

	m_servers.clear();
	std::ifstream file(FILE_NAME);
	std::string line;
	while (std::getline(file, line))
	{
		line = Trim(line);
		if (line.empty() || line[0] == '#')
			continue;
		std::vector<std::string> fields;
		size_t at = 0;
		while (true)
		{
			size_t bar = line.find('|', at);
			fields.push_back(Trim(line.substr(at, bar == std::string::npos ? std::string::npos : bar - at)));
			if (bar == std::string::npos)
				break;
			at = bar + 1;
		}

		Server server;
		server.address = fields[0];
		server.port = NetProtocol::DEFAULT_PORT;
		if (size_t colon = server.address.rfind(':'); colon != std::string::npos)
		{
			try { server.port = static_cast<uint16_t>(std::stoi(server.address.substr(colon + 1))); }
			catch (...) {}
			server.address = server.address.substr(0, colon);
		}
		server.region = fields.size() > 1 ? fields[1] : "";
		server.name = fields.size() > 2 ? fields[2] : "";
		if (!server.address.empty())
			m_servers.push_back(server);
	}
}

void ServerBrowser::Refresh()
{
	m_queries.clear();
	Load();
	m_refreshed = false;
	for (size_t i = 0; i < m_servers.size(); i++)
	{
		auto& server = m_servers[i];
		std::string error;
		auto connection = Net::Connection::Begin(server.address, server.port, error);
		if (!connection)
		{
			server.state = Server::State::OFFLINE;
			server.error = error;
			continue;
		}
		m_queries.push_back({ i, std::move(connection), Clock::now(), {}, false });
	}
	if (m_queries.empty())
		m_refreshed = true;
}

void ServerBrowser::Update()
{
	if (m_queries.empty())
		return;
	auto now = Clock::now();
	for (auto it = m_queries.begin(); it != m_queries.end();)
	{
		auto& query = *it;
		auto& server = m_servers[query.server];
		bool done = false;
		std::string error;
		auto status = query.connection->PollConnect(error);
		if (status == Net::Connection::Status::FAILED)
		{
			server.state = Server::State::OFFLINE;
			server.error = error;
			done = true;
		}
		else if (status == Net::Connection::Status::OPEN)
		{
			// Timed from the question, not the connection, so it's one trip there and back.
			if (!query.sent)
			{
				query.connection->Send(Writer().Word("query").Text());
				query.asked = Clock::now();
				query.sent = true;
			}
			std::vector<std::string> messages;
			bool open = query.connection->Receive(messages);
			for (const auto& message : messages)
			{
				Reader in(message);
				if (in.Next() != "info")
					continue;
				long long version = in.Int();
				std::string name = in.Next();
				std::string region = in.Next();
				int players = static_cast<int>(in.Int());
				int maxPlayers = static_cast<int>(in.Int());
				if (in.Failed())
					continue;
				server.pingMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - query.asked).count());
				if (server.name.empty())
					server.name = name;
				if (server.region.empty())
					server.region = region;
				server.players = players;
				server.maxPlayers = maxPlayers;
				server.state = version != NetProtocol::VERSION ? Server::State::INCOMPATIBLE
					: players >= maxPlayers ? Server::State::FULL : Server::State::ONLINE;
				done = true;
			}
			if (!done && !open)
			{
				server.state = Server::State::OFFLINE;
				server.error = "it closed the connection";
				done = true;
			}
		}
		if (!done && now - query.started > QUERY_TIMEOUT)
		{
			server.state = Server::State::OFFLINE;
			server.error = "no answer";
			done = true;
		}
		it = done ? m_queries.erase(it) : it + 1;
	}
	if (m_queries.empty())
		m_refreshed = true;
}

const ServerBrowser::Server* ServerBrowser::Closest() const
{
	const Server* closest = nullptr;
	for (const auto& server : m_servers)
		if (server.state == Server::State::ONLINE && (!closest || server.pingMs < closest->pingMs))
			closest = &server;
	return closest;
}
