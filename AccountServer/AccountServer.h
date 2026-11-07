#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../Game/Net/NetProtocol.h"
#include "../LV/Engine/Networking/Crypto.h"
#include "../LV/Engine/Networking/Socket.h"
#include "Accounts.h"

// Every player's account, for every garage. A player logs in here once (or makes an account), over an
// encrypted connection, and is given a ticket: who they are, until when, signed with this server's
// identity key. Garage servers take the ticket as proof, checking the signature against the identity
// this server writes out for them (Config::publicIdentityFile).
class AccountServer
{
public:
	struct Config
	{
		static constexpr const char* FILE_NAME = "account_server.cfg";

		int port = NetProtocol::ACCOUNT_PORT;
		std::string accountsFile = "accounts.lvaccounts";
		std::string identityFile = "account_identity.key";
		// The identity's public half, for the garage servers: copy it beside each one.
		std::string publicIdentityFile = "account_identity.txt";
		float ticketHours = 12.f;

		// Reads the file, writing it with the defaults first if there isn't one.
		static Config Load(const std::string& path);
	};

	bool Start(const Config& config, std::string& error);
	// Call often: takes in connections, logs players in and saves new accounts.
	void Update(float deltaTime);
	void Stop();
private:
	struct Peer
	{
		std::unique_ptr<Net::Connection> connection = nullptr;
		float age = 0.f;
	};

	void Receive(Peer& peer, bool& open);
	void Secure(Peer& peer, Reader& in, bool& open);
	void LogIn(Peer& peer, Reader& in);
	void Refuse(Peer& peer, const std::string& why);

	Config								m_config = {};
	std::unique_ptr<Net::Listener>		m_listener = nullptr;
	std::unique_ptr<Net::Identity>		m_identity = nullptr;
	std::vector<Peer>					m_peers = {};
	Accounts							m_accounts = {};
};
