#include "Profiles.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

#include "../Game/Save/SaveText.h"

namespace
{
	constexpr long long FILE_VERSION = 2;
}

bool Profiles::Load(const std::string& path, std::string& error)
{
	m_path = path;
	m_profiles.clear();
	if (!std::filesystem::exists(path))
		return true;
	std::ifstream file(path, std::ios::binary);
	std::stringstream text;
	text << file.rdbuf();
	Reader in(text.str());
	in.Expect("lvprofiles");
	long long version = in.Int();
	if (version < 1 || version > FILE_VERSION)
		in.Fail("it was saved by a different version of the server");
	in.Expect("profiles");
	long long count = in.Int();
	for (long long i = 0; i < count && !in.Failed(); i++)
	{
		Profile profile;
		profile.account = static_cast<int>(in.Int());
		profile.username = in.Next();
		profile.layout = in.Next();
		profile.placed = in.Int() != 0;
		profile.x = static_cast<float>(in.Num());
		profile.y = static_cast<float>(in.Num());
		profile.z = static_cast<float>(in.Num());
		profile.yaw = static_cast<float>(in.Num());
		profile.pitch = static_cast<float>(in.Num());
		// Before version 2 a server had one garage, which nobody belongs to now: everyone chooses afresh.
		if (version >= 2)
			profile.garage = static_cast<int>(in.Int());
		m_profiles[profile.account] = profile;
	}
	if (in.Failed())
	{
		error = std::format("{} couldn't be read: {}.", path, in.Error());
		return false;
	}
	return true;
}

bool Profiles::Save(std::string& error)
{
	Writer out;
	out.Word("lvprofiles").Int(FILE_VERSION).Line();
	out.Word("profiles").Int(m_profiles.size()).Line();
	for (const auto& [account, profile] : m_profiles)
		out.Int(profile.account).Str(profile.username).Str(profile.layout).Int(profile.placed)
			.Num(profile.x).Num(profile.y).Num(profile.z).Num(profile.yaw).Num(profile.pitch).Int(profile.garage).Line();

	// Written beside the old one, then swapped in, so a crash mid-save never loses it.
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

Profiles::Profile& Profiles::Get(int account, const std::string& username)
{
	auto [it, added] = m_profiles.try_emplace(account);
	auto& profile = it->second;
	// The name as the account server has it now.
	if (added || profile.username != username)
	{
		profile.account = account;
		profile.username = username;
		m_dirty = true;
	}
	return profile;
}
