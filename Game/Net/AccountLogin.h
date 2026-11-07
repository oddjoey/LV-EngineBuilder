#pragma once

#include <memory>
#include <string>

#include "LV/Engine/Networking/Crypto.h"
#include "LV/Engine/Networking/Socket.h"
#include "NetProtocol.h"

// Logging in to (or making) an account on the account server, before any garage: an encrypted
// connection, the username and password, and back comes a ticket that any garage takes as proof of
// who the player is (see NetProtocol::Ticket).
class AccountLogin
{
public:
	enum class State { IDLE, WORKING, LOGGED_IN, FAILED };

	// Starts logging in. Its progress comes with Update.
	void Begin(const std::string& address, uint16_t port, const std::string& username, const std::string& password, bool newAccount);
	// Each frame while working.
	void Update(float deltaTime);
	// Forgets the ticket.
	void LogOut();

	State GetState() const { return m_state; }
	bool IsLoggedIn() const { return m_state == State::LOGGED_IN; }
	bool IsWorking() const { return m_state == State::WORKING; }
	// Why it failed.
	const std::string& GetError() const { return m_error; }
	// Once logged in: the ticket to show the garages, and who it says we are.
	const std::string& GetTicket() const { return m_ticket; }
	const std::string& GetSignature() const { return m_signature; }
	const NetProtocol::Ticket& GetIdentity() const { return m_identity; }
	// Has the ticket run out? Then it's time to log in again.
	bool HasExpired() const { return IsLoggedIn() && NetProtocol::TicketExpired(m_identity); }
private:
	void Fail(const std::string& why);

	State								m_state = State::IDLE;
	std::unique_ptr<Net::Connection>	m_connection = nullptr;
	std::unique_ptr<Net::KeyExchange>	m_keyExchange = nullptr;
	std::string							m_host = "";
	// Held only until the connection is encrypted and it's sent.
	std::string							m_username = "";
	std::string							m_password = "";
	bool								m_newAccount = false;
	float								m_time = 0.f;
	std::string							m_error = "";
	std::string							m_ticket = "";
	std::string							m_signature = "";
	NetProtocol::Ticket					m_identity = {};
};
