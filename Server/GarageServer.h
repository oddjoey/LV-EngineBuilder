#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../Game/Net/NetProtocol.h"
#include "../LV/Engine/Networking/Crypto.h"
#include "../LV/Engine/Networking/Socket.h"
#include "Profiles.h"

// The garages of one world, kept for everyone who plays in them. The server doesn't run the shops: the
// players' games do. It keeps each garage as the records its players send (see NetProtocol), puts every
// change in one order and passes it on to the garage's players, keeps the clock, and saves the garages
// to disk. A player belongs to one garage: the first time they come they make one of their own, or ask
// to join one, which its members vote on.
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
		std::string profilesFile = "profiles.lvprofiles";
		// Prefixes put in front of players' names (see LoadPrefixes): read again whenever it changes.
		std::string prefixesFile = "prefixes.txt";
		// The account server's public identity (it writes the file out): log ins are checked against it.
		std::string accountIdentityFile = "account_identity.txt";
		std::string identityFile = "server_identity.key";
		float dayLengthMinutes = 30.f;		// real minutes for a full 24 hours; 0 stops the sun
		float autosaveSeconds = 30.f;

		// Reads the file, writing it with the defaults first if there isn't one.
		static Config Load(const std::string& path);
	};

	bool Start(const Config& config, std::string& error);
	// Call often (every few milliseconds): takes in connections and messages and sends what's due.
	void Update(float deltaTime);
	// Saves the garages and drops everyone.
	void Stop();
private:
	enum class PeerState
	{
		NEW,		// connected, hasn't said what it wants (a server list query, or to play)
		CHOOSING,	// logged in, not in a garage yet: making one or asking to join one
		REQUESTING,	// asked to join a garage: waiting for its members' vote (or for one to come online)
		WAITING,	// in a garage that's new, waiting for its first player to send it
		PLAYING
	};
	struct Peer
	{
		std::unique_ptr<Net::Connection> connection = nullptr;
		PeerState state = PeerState::NEW;
		Profiles::Profile* profile = nullptr;
		int id = 0;			// the account's number
		int slot = 0;		// see NetProtocol::MAX_SLOTS
		std::string name = "";		// their username
		std::string prefix = "";		// shown in front of their name, e.g. "[Owner]"; empty for none
		int garage = 0;		// the garage they're in (or asking to join), or 0
		float age = 0.f;
	};
	struct Garage
	{
		int id = 0;
		std::string name = "";		// "joey's garage"
		int plot = 0;				// where it stands (NetProtocol::PlotOffsetX)
		std::set<int> members = {};	// account numbers
		NetProtocol::Records records = {};
		// The player sending a brand new garage its first shop, if anyone is.
		int seeder = 0;
	};
	// Someone asking to join a garage. Its members online vote, once one is online.
	struct JoinRequest
	{
		int requester = 0;		// their account
		int garage = 0;
		bool voting = false;
		float timeLeft = 0.f;
		std::map<int, bool> ballots = {};	// member to yes
	};

	void Receive(Peer& peer, bool& open);
	// The key exchange: the connection is encrypted before anyone logs in.
	void Secure(Peer& peer, Reader& in, bool& open);
	void Refuse(Peer& peer, const std::string& why);
	void Hello(Peer& peer, Reader& in, bool& open);
	// Into their garage: welcomed, or (a brand new one) asked to send it first.
	void Enter(Peer& peer, int garage);
	void Welcome(Peer& peer, bool seed);
	void ApplyOps(int garage, int author, long long sequence, std::vector<NetProtocol::Op> ops);
	void Departed(const Peer& peer);
	// Anyone who was waiting for this garage to be made gets it now (or, if the player making it left,
	// the first of them makes it instead).
	void WelcomeWaiting(int garage);

	// Choosing a garage: the list, making one, asking to join one, and the vote.
	void SendChoices(Peer& peer);
	void CreateGarage(Peer& peer);
	void RequestGarage(Peer& peer, int garage);
	void CancelRequest(Peer& peer);
	void Ballot(Peer& peer, int requester, bool yes);
	// Votes start on the requests to join a garage once any of its members is online.
	void StartVotes(int garage);
	// Each request being voted on: decided once enough have voted (or time's up), or put back to wait
	// if its garage's members have all gone.
	void UpdateVotes(float deltaTime);
	void Admit(JoinRequest request);
	void TurnAway(JoinRequest request, const std::string& why);
	void EndVote(const JoinRequest& request, const std::string& outcome);
	// Everyone playing is told where every garage stands (a new one has been made).
	void SendPlots();
	Writer& WritePlots(Writer& out) const;

	void SendToPlaying(const std::string& message, int except = -1);
	void SendToGarage(int garage, const std::string& message, int except = -1);
	Peer* FindPeer(int account);
	Garage* FindGarage(int id);
	int OnlineMembers(int garage) const;
	int PlayerCount() const;
	int FreeSlot() const;
	int FreePlot() const;
	bool IsLoggedIn(int account) const;
	bool SaveAll();
	// The prefixes file: one player a line, username=prefix, e.g. "joey=[Owner]". Written with an
	// example if there isn't one. True if what it says has changed since it was last read.
	bool LoadPrefixes();
	// Everyone playing whose prefix has changed is shown with the new one.
	void RenamePlayers();
	std::string PrefixOf(const std::string& username) const;

	bool Load(std::string& error);
	bool Save(std::string& error);
	void AutoSave(float deltaTime);

	Config								m_config = {};
	std::unique_ptr<Net::Listener>		m_listener = nullptr;
	std::vector<Peer>					m_peers = {};
	std::map<int, Garage>				m_garages = {};
	int									m_nextGarage = 1;
	std::vector<JoinRequest>			m_requests = {};
	Profiles							m_profiles = {};
	std::string							m_accountIdentity = "";
	std::unique_ptr<Net::Identity>		m_identity = nullptr;
	float								m_timeOfDay = 9.5f;
	float								m_clockTimer = 0.f;
	float								m_saveTimer = 0.f;
	// Lower-cased username to prefix.
	std::map<std::string, std::string>	m_prefixes = {};
	std::string							m_prefixesText = "";
	float								m_prefixesTimer = 0.f;
	bool								m_dirty = false;
};
