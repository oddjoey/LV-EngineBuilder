#include "AccountServer.h"

#include "../Game/LocalTime.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace
{
	// A connection gets this long to log in, then it's dropped: logging in is one question and answer.
	constexpr float LOGIN_TIMEOUT = 20.f;

	void Log(const std::string& text)
	{
		std::printf("[%s] %s\n", LocalTimeText(std::chrono::system_clock::now(), "%Y-%m-%d %H:%M:%S").c_str(), text.c_str());
		std::fflush(stdout);
	}
}

AccountServer::Config AccountServer::Config::Load(const std::string& path)
{
	Config config;
	if (!std::filesystem::exists(path))
	{
		std::ofstream file(path);
		file << "# The account server's settings. Restart the server after changing them.\n";
		file << "port=" << config.port << "\n";
		file << "accounts_file=" << config.accountsFile << "\n";
		file << "# The server's private identity key: keep it secret, and keep it. Tickets are signed with it.\n";
		file << "identity_file=" << config.identityFile << "\n";
		file << "# Its public half, written out for the garage servers: copy it beside each one.\n";
		file << "public_identity_file=" << config.publicIdentityFile << "\n";
		file << "# How long a log in lasts before the player has to log in again.\n";
		file << "ticket_hours=" << config.ticketHours << "\n";
		return config;
	}

	std::ifstream file(path);
	std::string line;
	while (std::getline(file, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;
		auto equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		std::string key = line.substr(0, equals);
		std::string text = line.substr(equals + 1);
		std::stringstream value(text);
		if (key == "port") value >> config.port;
		else if (key == "accounts_file") config.accountsFile = text;
		else if (key == "identity_file") config.identityFile = text;
		else if (key == "public_identity_file") config.publicIdentityFile = text;
		else if (key == "ticket_hours") value >> config.ticketHours;
	}
	config.port = std::clamp(config.port, 1024, 65535);
	config.ticketHours = std::clamp(config.ticketHours, 0.1f, 24.f * 30.f);
	return config;
}

bool AccountServer::Start(const Config& config, std::string& error)
{
	m_config = config;
	if (!m_accounts.Load(config.accountsFile, error))
		return false;
	m_identity = Net::Identity::LoadOrCreate(config.identityFile, error);
	if (!m_identity)
	{
		error = std::format("The server's identity: {}.", error);
		return false;
	}
	// What the garage servers check tickets against.
	{
		std::ofstream file(config.publicIdentityFile, std::ios::trunc);
		file << m_identity->PublicKey() << "\n";
		if (!file)
		{
			error = std::format("Couldn't write {}.", config.publicIdentityFile);
			return false;
		}
	}
	m_listener = Net::Listener::Listen(static_cast<uint16_t>(config.port), error);
	if (!m_listener)
	{
		error = std::format("Couldn't listen on port {}: {}.", config.port, error);
		return false;
	}
	Log(std::format("The account server is open on port {}, with {} accounts.", config.port, m_accounts.Count()));
	Log(std::format("Identity fingerprint: {}", Net::Fingerprint(m_identity->PublicKey())));
	Log(std::format("Garage servers check log ins against {}: copy it beside each one.", config.publicIdentityFile));
	return true;
}

void AccountServer::Stop()
{
	std::string error;
	if (m_accounts.IsDirty() && !m_accounts.Save(error))
		Log("Couldn't save the accounts: " + error);
	for (auto& peer : m_peers)
		peer.connection->Flush();
	m_peers.clear();
	m_listener.reset();
}

void AccountServer::Update(float deltaTime)
{
	while (auto connection = m_listener->Accept())
		m_peers.push_back({ std::move(connection) });

	for (size_t i = 0; i < m_peers.size();)
	{
		auto& peer = m_peers[i];
		bool open = true;
		peer.age += deltaTime;
		Receive(peer, open);
		peer.connection->Flush();
		if (open && peer.age < LOGIN_TIMEOUT)
		{
			i++;
			continue;
		}
		m_peers.erase(m_peers.begin() + static_cast<std::ptrdiff_t>(i));
	}
}

void AccountServer::Receive(Peer& peer, bool& open)
{
	std::vector<std::string> messages;
	open = peer.connection->Receive(messages);
	for (const auto& message : messages)
	{
		Reader in(message);
		std::string type = in.Next();
		if (type == "secure" && !peer.connection->IsEncrypted())
			Secure(peer, in, open);
		// Never a password in the clear.
		else if (type == "login" && peer.connection->IsEncrypted())
			LogIn(peer, in);
		else
		{
			Refuse(peer, "That account server is running a different version of the game.");
			open = false;
		}
		if (!open)
			break;
	}
}

void AccountServer::Secure(Peer& peer, Reader& in, bool& open)
{
	long long version = in.Int();
	std::string clientKey = in.Next();
	if (in.Failed() || version != NetProtocol::VERSION)
	{
		Refuse(peer, "That account server is running a different version of the game.");
		open = false;
		return;
	}
	auto keyExchange = Net::KeyExchange::Create();
	std::unique_ptr<Net::Cipher> sending, receiving;
	const std::string serverKey = keyExchange ? keyExchange->PublicKey() : "";
	if (!keyExchange || !keyExchange->Derive(clientKey, true, clientKey, serverKey, sending, receiving))
	{
		Refuse(peer, "The account server couldn't make a safe connection.");
		open = false;
		return;
	}
	peer.connection->Send(Writer().Word("secure").Word(serverKey).Word(m_identity->PublicKey())
		.Word(m_identity->Sign(Net::HandshakeTranscript(clientKey, serverKey))).Text());
	peer.connection->Encrypt(std::move(sending), std::move(receiving));
}

void AccountServer::LogIn(Peer& peer, Reader& in)
{
	bool newAccount = in.Int() != 0;
	std::string username = in.Next();
	std::string password = in.Next();
	std::string error = in.Failed() ? "That log in didn't arrive whole." : "";
	Accounts::Account* account = nullptr;
	if (error.empty())
		account = newAccount ? m_accounts.Create(username, password, error) : m_accounts.LogIn(username, password, error);
	std::fill(password.begin(), password.end(), '\0');
	if (!account)
	{
		Log(std::format("Turned away {} from {}: {}", username, peer.connection->GetAddress(), error));
		Refuse(peer, error);
		return;
	}
	if (newAccount)
	{
		std::string saveError;
		if (!m_accounts.Save(saveError))
			Log("Couldn't save the accounts: " + saveError);
	}

	NetProtocol::Ticket ticket;
	ticket.account = account->id;
	ticket.username = account->username;
	ticket.issued = NetProtocol::UnixNow();
	ticket.expires = ticket.issued + static_cast<long long>(m_config.ticketHours * 3600.f);
	std::string text = NetProtocol::TicketText(ticket);
	peer.connection->Send(Writer().Word("ticket").Str(text).Str(m_identity->Sign(text)).Text());
	Log(std::format("{} (account {}) {} from {}.", account->username, account->id, newAccount ? "made an account" : "logged in",
		peer.connection->GetAddress()));
}

void AccountServer::Refuse(Peer& peer, const std::string& why)
{
	peer.connection->Send(Writer().Word("reject").Str(why).Text());
	peer.connection->Flush();
}
