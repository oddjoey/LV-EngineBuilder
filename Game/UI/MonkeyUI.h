#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "../World/MonkeyLook.h"

// Beside the backpack: your monkey in 3D, turning, and its fur, face, cap and tail to choose underneath.
// Changes show straight away, on you and to everyone else in the garage. Dressing up needs the Wardrobe perk.
namespace MonkeyUI
{
	// modelLookup finds a shop model by name, for the 3D picture.
	// wardrobe: the Wardrobe perk is the shop's, without which it can't be dressed (and is shown undressed).
	// premium: the Premium Colours perk, adding gold, chrome and neon.
	void Draw(MonkeyLook& look, const std::function<uint32_t(const std::string&)>& modelLookup, bool wardrobe, bool premium);
}
