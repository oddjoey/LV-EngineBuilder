#include "AccountLogin.h"

#include <algorithm>
#include <format>

#include "ClientHandshake.h"

namespace
{
	constexpr float TIMEOUT = 10.f;

	void Forget(std::string& secret)
	{
		std::fill(secret.begin(), secret.end(), '\0');
		secret.clear();
	}
}

void AccountLogin::Begin(const std::string& address, uint16_t port, const std::string& username, const std::string& password, bool newAccount)
{
	LogOut();
	m_state = State::WORKING;
	m_host = std::format("{}:{}", address, port);
	m_time = 0.f;
	m_keyExchange = Net::KeyExchange::Create();
	if (!m_keyExchange)
	{
		Fail("Windows cryptography didn't start, so the connection can't be made safe.");
		return;
	}
	std::string error;
	m_connection = Net::Connection::Begin(address, port, error);
	if (!m_connection)
	{
		Fail(std::format("Couldn't reach the account server: {}.", error));
		return;
	}
	// Keys first: the password only goes once the connection is encrypted.
	ClientHandshake::Start(*m_connection, *m_keyExchange);
	m_username = username;
	m_password = password;
	m_newAccount = newAccount;
}

void AccountLogin::Update(float deltaTime)
{
	if (m_state != State::WORKING)
		return;
	m_time += deltaTime;
	std::string error;
	auto status = m_connection->PollConnect(error);
	if (status == Net::Connection::Status::FAILED)
	{
		Fail(std::format("Couldn't reach the account server: {}.", error));
		return;
	}
	if (status == Net::Connection::Status::CONNECTING || m_time > TIMEOUT)
	{
		if (m_time > TIMEOUT)
			Fail("The account server didn't answer.");
		return;
	}

	std::vector<std::string> messages;
	bool open = m_connection->Receive(messages);
	for (const auto& message : messages)
	{
		Reader in(message);
		std::string type = in.Next();
		if (type == "reject")
		{
			Fail(in.Next());
			return;
		}
		if (type == "secure" && !m_connection->IsEncrypted())
		{
			if (std::string why = ClientHandshake::Finish(in, *m_connection, *m_keyExchange, m_host); !why.empty())
			{
				Fail(why);
				return;
			}
			m_keyExchange.reset();
			m_connection->Send(Writer().Word("login").Int(m_newAccount).Str(m_username).Str(m_password).Text());
			Forget(m_password);
		}
		else if (type == "ticket" && m_connection->IsEncrypted())
		{
			m_ticket = in.Next();
			m_signature = in.Next();
			auto identity = NetProtocol::ReadTicket(m_ticket);
			if (in.Failed() || !identity)
			{
				Fail("The account server's answer didn't make sense.");
				return;
			}
			m_identity = *identity;
			m_state = State::LOGGED_IN;
			m_connection.reset();
			return;
		}
	}
	if (!open)
		Fail("The account server closed the connection.");
}

void AccountLogin::LogOut()
{
	m_state = State::IDLE;
	m_connection.reset();
	m_keyExchange.reset();
	Forget(m_password);
	m_ticket.clear();
	m_signature.clear();
	m_identity = {};
	m_error.clear();
}

void AccountLogin::Fail(const std::string& why)
{
	m_state = State::FAILED;
	m_error = why;
	m_connection.reset();
	m_keyExchange.reset();
	Forget(m_password);
}
