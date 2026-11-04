#pragma once

#include <map>
#include <string>

// The players' accounts on this server, kept in one file beside the garage. A password is never
// kept, only a salted PBKDF2-SHA256 hash of it.
class Accounts
{
public:
	struct Account
	{
		int id = 0;					// the player's number in the garage, for good
		std::string username = "";	// as they typed it when they made the account
		std::string salt = "";		// hex
		std::string hash = "";		// hex
		long long iterations = 0;
		// What's theirs to come back to: how their backpack and hotbar were laid out, and where they
		// stood (the things in them are in the garage, carried by them).
		std::string layout = "";
		bool placed = false;
		float x = 0.f, y = 0.f, z = 0.f, yaw = 0.f, pitch = 0.f;
	};

	bool Load(const std::string& path, std::string& error);
	bool Save(std::string& error);
	bool IsDirty() const { return m_dirty; }
	void MarkDirty() { m_dirty = true; }
	size_t Count() const { return m_accounts.size(); }

	// A new account (null, and why not, if the name is taken or doesn't suit).
	Account* Create(const std::string& username, const std::string& password, std::string& error);
	// The account, if the password is right (null, and why, if not).
	Account* LogIn(const std::string& username, const std::string& password, std::string& error);
private:
	static std::string Key(const std::string& username);

	std::string							m_path = "";
	std::map<std::string, Account>		m_accounts = {};	// by username in lower case
	int									m_nextId = 1;
	bool								m_dirty = false;
};
