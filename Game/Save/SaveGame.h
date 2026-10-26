#pragma once

#include <string>

class Workshop;
class ShopWorld;
class FirstPersonController;

// Saves and loads a whole career: the shop's money, clock, equipment and every part's true
// geometry and readings, the engine on the stand, and where everything is in the 3D shop.
// The file is plain text, versioned, and read completely before anything is replaced, so a
// damaged save leaves the running game untouched.
class SaveGame
{
public:
	static constexpr const char* FILE_NAME = "savegame.lvsave";

	static bool Exists();
	// When the save was last written, e.g. "Oct 26, 14:05", or empty if there is none.
	static std::string LastSavedText();

	static bool Save(const Workshop& workshop, const ShopWorld& world, const FirstPersonController& player, float timeOfDay,
		std::string& error);
	// workshop should be freshly constructed; its state is replaced by the save's.
	static bool Load(Workshop& workshop, ShopWorld& world, FirstPersonController& player, float& timeOfDay, std::string& error);

	// Puts a freshly constructed workshop into the world: everything back to where a new career
	// starts.
	static void StartNew(Workshop& workshop, ShopWorld& world, FirstPersonController& player);
private:
	// Clears every item and delivery out of the 3D shop and lays out the workshop's parts afresh.
	static void ResetWorld(Workshop& workshop, ShopWorld& world);
};
