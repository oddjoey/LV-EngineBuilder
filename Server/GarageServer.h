#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../Game/Net/NetProtocol.h"
#include "../LV/Engine/Networking/Crypto.h"
#include "../LV/Engine/Networking/Socket.h"
#include "Accounts.h"

// One garage, kept for everyone who plays in it. The server doesn't run the shop: the players'
// games do. It keeps the garage as the records they send (see NetProtocol), puts every change in
// one order, passes each on to everyone, keeps the clock, and saves the garage to disk.
class GarageServer
{
public:
	struct Config
	{
		static constexpr const char* FILE_NAME = "server.cfg";

		std::string name = "LV Garage";
		std::string region = "";			// shown in the server list, e.g. "US East"
		int port = NetProtocol::DEFAULT_PORT;
		int maxPlayers = 16;				// at once, 1 to NetProtocol::MAX_SLOTS
		std::string garageFile = "garage.lvgarage";
		std::string accountsFile = "accounts.lvaccounts";
		std::string identityFile = "server_identity.key";
		float dayLengthMinutes = 30.f;		// real minutes for a full 24 hours; 0 stops the sun
		float autosaveSeconds = 30.f;

		// Reads the file, writing it with the defaults first if there isn't one.
		static Config Load(const std::string& path);
	};

	bool Start(const Config& config, std::string& error);
	// Call often (every few milliseconds): takes in connections and messages and sends what's due.
	void Update(float deltaTime);
	// Saves the garage and drops everyone.
	void Stop();
private:
	enum class PeerState
	{
		NEW,		// connected, hasn't said what it wants (a server list query, or to play)
		WAITING,	// wants to play, but the garage is new and the first player hasn't sent it yet
		PLAYING
	};
	struct Peer
	{
		std::unique_ptr<Net::Connection> connection = nullptr;
		PeerState state = PeerState::NEW;
		Accounts::Account* account = nullptr;
		int id = 0;			// the account's number
		int slot = 0;		// see NetProtocol::MAX_SLOTS
		std::string name = "";
		float age = 0.f;
	};

	void Receive(Peer& peer, bool& open);
	// The key exchange: the connection is encrypted before anyone logs in.
	void Secure(Peer& peer, Reader& in, bool& open);
	void Refuse(Peer& peer, const std::string& why);
	void Hello(Peer& peer, Reader& in, bool& open);
	void Welcome(Peer& peer, bool seed);
	void ApplyOps(int author, long long sequence, std::vector<NetProtocol::Op> ops);
	void Departed(const Peer& peer);
	// Anyone who was waiting for the garage to be made gets it now (or, if the player making it
	// left, the first of them makes it instead).
	void WelcomeWaiting();
	void SendToPlaying(const std::string& message, int except = -1);
	int PlayerCount() const;
	int FreeSlot() const;
	bool IsLoggedIn(int account) const;
	bool SaveAll();

	bool Load(std::string& error);
	bool Save(std::string& error);
	void AutoSave(float deltaTime);

	Config								m_config = {};
	std::unique_ptr<Net::Listener>		m_listener = nullptr;
	std::vector<Peer>					m_peers = {};
	NetProtocol::Records				m_records = {};
	Accounts							m_accounts = {};
	std::unique_ptr<Net::Identity>		m_identity = nullptr;
	// The player sending a brand new garage its first shop, if anyone is.
	int									m_seeder = 0;
	float								m_timeOfDay = 9.5f;
	float								m_clockTimer = 0.f;
	float								m_saveTimer = 0.f;
	bool								m_dirty = false;
};
