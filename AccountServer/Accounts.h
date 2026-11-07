#pragma once

#include <map>
#include <string>

// Every player's account, kept by the account server in one file. A password is never
// kept, only a salted PBKDF2-SHA256 hash of it.
class Accounts
{
public:
	struct Account
	{
		int id = 0;					// the player's number in every garage, for good
		std::string username = "";	// as they typed it when they made the account
		std::string salt = "";		// hex
		std::string hash = "";		// hex
		long long iterations = 0;
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
