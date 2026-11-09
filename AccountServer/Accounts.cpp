// Passwords are hashed with PBKDF2-HMAC-SHA256: Windows' own cryptography there, OpenSSL elsewhere (macOS).
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

#include "Accounts.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <vector>

#include "../Game/Net/NetProtocol.h"
#include "../Game/Save/SaveText.h"


namespace
{
	constexpr long long FILE_VERSION = 2;
	// Slow on purpose, so a stolen accounts file is slow to guess passwords from: a few tens of
	// milliseconds per log in.
	constexpr long long ITERATIONS = 100'000;
	constexpr size_t SALT_BYTES = 16;
	constexpr size_t HASH_BYTES = 32;

	std::string Hex(const std::vector<unsigned char>& bytes)
	{
		std::string text;
		for (unsigned char byte : bytes)
			text += std::format("{:02x}", byte);
		return text;
	}

	std::vector<unsigned char> Unhex(const std::string& text)
	{
		std::vector<unsigned char> bytes;
		for (size_t i = 0; i + 1 < text.size(); i += 2)
			bytes.push_back(static_cast<unsigned char>(std::stoi(text.substr(i, 2), nullptr, 16)));
		return bytes;
	}

	std::vector<unsigned char> RandomBytes(size_t count)
	{
		std::vector<unsigned char> bytes(count);
#ifdef _WIN32
		BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
		RAND_bytes(bytes.data(), static_cast<int>(count));
#endif
		return bytes;
	}

	std::string HashPassword(const std::string& password, const std::string& salt, long long iterations)
	{
#ifdef _WIN32
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
			return "";
		auto saltBytes = Unhex(salt);
		std::vector<unsigned char> hash(HASH_BYTES);
		NTSTATUS status = BCryptDeriveKeyPBKDF2(algorithm,
			reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())), static_cast<ULONG>(password.size()),
			saltBytes.data(), static_cast<ULONG>(saltBytes.size()), static_cast<ULONGLONG>(iterations),
			hash.data(), static_cast<ULONG>(hash.size()), 0);
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return status == 0 ? Hex(hash) : "";
#else
		auto saltBytes = Unhex(salt);
		std::vector<unsigned char> hash(HASH_BYTES);
		bool hashed = PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), saltBytes.data(), static_cast<int>(saltBytes.size()),
			static_cast<int>(iterations), EVP_sha256(), static_cast<int>(hash.size()), hash.data()) == 1;
		return hashed ? Hex(hash) : "";
#endif
	}

	// Compared in full whatever the difference, so the time taken doesn't hint at how close a guess was.
	bool SameText(const std::string& a, const std::string& b)
	{
		if (a.size() != b.size())
			return false;
		unsigned char difference = 0;
		for (size_t i = 0; i < a.size(); i++)
			difference |= static_cast<unsigned char>(a[i] ^ b[i]);
		return difference == 0;
	}
}

std::string Accounts::Key(const std::string& username)
{
	std::string key = username;
	std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return key;
}

bool Accounts::Load(const std::string& path, std::string& error)
{
	m_path = path;
	m_accounts.clear();
	m_nextId = 1;
	if (!std::filesystem::exists(path))
		return true;
	std::ifstream file(path, std::ios::binary);
	std::stringstream text;
	text << file.rdbuf();
	Reader in(text.str());
	in.Expect("lvaccounts");
	if (in.Int() != FILE_VERSION)
		in.Fail("it was saved by a different version of the server");
	in.Expect("next_id");
	m_nextId = static_cast<int>(in.Int());
	in.Expect("accounts");
	long long count = in.Int();
	for (long long i = 0; i < count && !in.Failed(); i++)
	{
		Account account;
		account.id = static_cast<int>(in.Int());
		account.username = in.Next();
		account.salt = in.Next();
		account.hash = in.Next();
		account.iterations = in.Int();
		m_nextId = (std::max)(m_nextId, account.id + 1);
		m_accounts[Key(account.username)] = account;
	}
	if (in.Failed())
	{
		error = std::format("{} couldn't be read: {}.", path, in.Error());
		return false;
	}
	return true;
}

bool Accounts::Save(std::string& error)
{
	Writer out;
	out.Word("lvaccounts").Int(FILE_VERSION).Line();
	out.Word("next_id").Int(m_nextId).Line();
	out.Word("accounts").Int(m_accounts.size()).Line();
	for (const auto& [key, account] : m_accounts)
	{
		out.Int(account.id).Str(account.username).Str(account.salt).Str(account.hash).Int(account.iterations).Line();
	}

	// Written beside the old one, then swapped in, so a crash mid-save never loses the accounts.
	std::string temporary = m_path + ".tmp";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		file << out.Text();
		if (!file)
		{
			error = std::format("couldn't write {}", temporary);
			return false;
		}
	}
	std::error_code fileError;
	std::filesystem::rename(temporary, m_path, fileError);
	if (fileError)
	{
		error = fileError.message();
		return false;
	}
	m_dirty = false;
	return true;
}

Accounts::Account* Accounts::Create(const std::string& username, const std::string& password, std::string& error)
{
	error = NetProtocol::UsernameProblem(username);
	if (error.empty() && (password.size() < NetProtocol::PASSWORD_MIN || password.size() > NetProtocol::PASSWORD_MAX))
		error = std::format("A password is {} to {} characters long.", NetProtocol::PASSWORD_MIN, NetProtocol::PASSWORD_MAX);
	if (error.empty() && m_accounts.contains(Key(username)))
		error = std::format("There's already an account called {} here.", username);
	if (!error.empty())
		return nullptr;

	Account account;
	account.id = m_nextId++;
	account.username = username;
	account.salt = Hex(RandomBytes(SALT_BYTES));
	account.iterations = ITERATIONS;
	account.hash = HashPassword(password, account.salt, account.iterations);
	if (account.hash.empty())
	{
		error = "The server couldn't secure the password.";
		return nullptr;
	}
	m_dirty = true;
	return &(m_accounts[Key(username)] = account);
}

Accounts::Account* Accounts::LogIn(const std::string& username, const std::string& password, std::string& error)
{
	auto it = m_accounts.find(Key(username));
	// Checked the same way whether the account exists or not, so the answer doesn't come back
	// quicker for names that aren't taken.
	std::string hash = HashPassword(password, it != m_accounts.end() ? it->second.salt : std::string(SALT_BYTES * 2, '0'),
		it != m_accounts.end() ? it->second.iterations : ITERATIONS);
	if (it == m_accounts.end() || hash.empty() || !SameText(hash, it->second.hash))
	{
		error = "Wrong username or password.";
		return nullptr;
	}
	return &it->second;
}
