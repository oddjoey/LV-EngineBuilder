#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "../World/MonkeyLook.h"

// The backpack's Monkey tab: your monkey in 3D, turning, and its fur, face, cap and tail to choose.
// Changes show straight away, on you and to everyone else in the garage.
namespace MonkeyUI
{
	// modelLookup finds a shop model by name, for the 3D picture.
	void Draw(MonkeyLook& look, const std::function<uint32_t(const std::string&)>& modelLookup);
}
