#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "LV/Engine/Objects/Object.h"
#include "LV/Engine/Networking/Crypto.h"
#include "LV/Engine/Networking/Socket.h"
#include "NetProtocol.h"
#include "NetState.h"
#include "../World/PlayerAvatar.h"

class Workshop;
class ShopWorld;
class FirstPersonController;

// Playing in a garage on a server (see LV-EngineServer). The server keeps the garage; each player's
// game is a copy kept in step with it. Each game sends what has changed in the garage there (a part
// measured, an item moved, money spent), and the server puts every change in one order and passes
// it to everyone. Where two players change the same thing at once, the later change wins, except
// money and the tallies, which add up.
class NetSession
{
public:
	enum class Role
	{
		OFFLINE,
		CONNECTING,		// the connection is being made
		JOINING,		// connected, waiting for the garage to arrive
		PLAYING
	};

	struct PlayerInfo
	{
		int id = 0;
		std::string name = "";
		bool local = false;
	};

	// Each player's colour: their overalls (theirs to everyone else, and their own sleeve and body in
	// first person) and their name over their head.
	static glm::vec4 PlayerColor(int id);

	// Starts connecting to a garage server, with the ticket from logging in (see AccountLogin). The
	// garage arrives a moment later: see TakeWelcome.
	bool Join(const std::string& address, uint16_t port, const std::string& ticket, const std::string& signature, std::string& error);
	// Leaves the garage (or stops trying to join it).
	void Leave(ShopWorld& world);

	Role GetRole() const { return m_role; }
	bool IsOnline() const { return m_role != Role::OFFLINE; }
	bool IsJoining() const { return m_role == Role::CONNECTING || m_role == Role::JOINING; }
	bool IsPlaying() const { return m_role == Role::PLAYING; }
	// "203.0.113.5:27616"
	const std::string& GetServer() const { return m_server; }
	int GetLocalId() const { return m_localId; }
	std::vector<PlayerInfo> GetPlayers() const;
	// The server keeps the clock: how long its days are, in minutes (0: the sun stands still).
	float GetDayLengthMinutes() const { return m_dayLengthMinutes; }

	// Each frame, after the shop updates: changes from the others are applied, changes made here are
	// sent, and the other players are moved to where they are.
	void Update(float deltaTime, Workshop& workshop, ShopWorld& world, const FirstPersonController& player, float& timeOfDay);
	// The garage has arrived. The game clears its own shop, then calls ApplyWelcome: the player gets
	// back what they had and where they were.
	bool TakeWelcome();
	void ApplyWelcome(Workshop& workshop, ShopWorld& world, FirstPersonController& player);
	// The server has gone (or turned us away). The game goes back to its own shop.
	bool TakeDisconnected();
	// Things to tell the player, like "Sam joined the garage."
	std::vector<std::string> TakeNotices();

	// Each other player's name over their head.
	void DrawNameTags(const ShopWorld& world, const FirstPersonController& player) const;
private:
	using Op = NetProtocol::Op;
	using Batch = NetProtocol::Batch;

	struct Remote
	{
		std::string name = "";
		glm::vec3 feet = {};
		float yaw = 0.f;
		float pitch = 0.f;
		// NetProtocol's presence: 0 down, 1 the right one out, 2 both; and crouched or not.
		int arms = 0;
		bool crouching = false;
		MonkeyLook look = {};
		// Where they're drawn: eased towards where they are, between the updates.
		glm::vec3 shownFeet = {};
		float shownYaw = 0.f;
		float shownPitch = 0.f;
		bool placed = false;
		PlayerAvatar avatar = {};
	};

	void Receive(ShopWorld& world, float& timeOfDay);
	// The server's half of the handshake: checked, and the connection encrypted before logging in.
	// False if it didn't check out (and the session has ended).
	bool Secure(Reader& in, ShopWorld& world);
	void Sync(Workshop& workshop, ShopWorld& world);
	void Disconnect(ShopWorld& world, const std::string& why);

	// What changed between the garage as it was and as it is: whole records, and money and the
	// tallies as how much they went up or down.
	std::vector<Op> Diff(const StateRecords& now) const;
	std::string PresenceMessage(const FirstPersonController& player, const ShopWorld& world) const;
	void ReadPresence(const std::string& message, ShopWorld& world);
	void SetClock(float time, float& timeOfDay);
	void RemoveRemote(int id, ShopWorld& world);
	void MoveAvatars(float deltaTime, ShopWorld& world);
	void Notice(const std::string& text) { m_notices.push_back(text); }

	Role										m_role = Role::OFFLINE;
	std::unique_ptr<Net::Connection>			m_connection = nullptr;
	std::string									m_server = "";
	// Until the connection is encrypted: this side of the key exchange. Then the ticket from the
	// account server (and its signature) is sent, to say who we are.
	std::unique_ptr<Net::KeyExchange>			m_keyExchange = nullptr;
	std::string									m_ticket = "";
	std::string									m_ticketSignature = "";
	int											m_localId = 0;
	std::string									m_localName = "";
	float										m_connectTime = 0.f;
	std::map<int, Remote>						m_remotes = {};
	// The garage as last sent or received. Local changes are measured against it.
	StateRecords								m_shadow = {};
	// Changes from the server, applied at the next sync.
	std::vector<Batch>							m_incoming = {};
	// Our changes the server hasn't passed back yet, by the batch they went in. A change from someone
	// else to the same record in the meantime is ignored: ours comes after it.
	std::map<std::string, long long>			m_pending = {};
	long long									m_nextSequence = 1;
	bool										m_welcomeReady = false;
	// The garage is new and empty: this player's fresh shop becomes it.
	bool										m_seed = false;
	StateRecords								m_welcomeRecords = {};
	// From the account: the backpack's layout, and where they stood (if they've been here before).
	std::string									m_welcomeLayout = "";
	bool										m_welcomePlaced = false;
	glm::vec3									m_welcomeFeet = {};
	float										m_welcomeYaw = 0.f;
	float										m_welcomePitch = 0.f;
	// The numbers this player gives new parts come from this slot's band (NetProtocol::ID_BAND).
	int											m_slot = 0;
	// The layout as last sent to the account, and when to check it again.
	std::string									m_sentLayout = "";
	float										m_profileTimer = 0.f;
	float										m_dayLengthMinutes = 0.f;
	bool										m_disconnected = false;
	float										m_syncTimer = 0.f;
	float										m_presenceTimer = 0.f;
	std::vector<std::string>					m_notices = {};
};
