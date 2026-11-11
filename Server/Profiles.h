#pragma once

#include <map>
#include <string>

// What each player has to come back to on this server, by their account number (the account itself is the
// account server's): the garage they belong to and, in it, how their backpack and hotbar were laid out and
// where they stood. The things in the backpack are in the garage's records, carried by them.
class Profiles
{
public:
	struct Profile
	{
		int account = 0;
		std::string username = "";
		std::string layout = "";
		bool placed = false;
		float x = 0.f, y = 0.f, z = 0.f, yaw = 0.f, pitch = 0.f;
		int garage = 0;		// the garage they belong to (GarageServer), or 0 for none yet
	};

	bool Load(const std::string& path, std::string& error);
	bool Save(std::string& error);
	bool IsDirty() const { return m_dirty; }
	void MarkDirty() { m_dirty = true; }

	// The player's profile, new if they haven't been here before.
	Profile& Get(int account, const std::string& username);
private:
	std::string					m_path = "";
	std::map<int, Profile>		m_profiles = {};
	bool						m_dirty = false;
};
