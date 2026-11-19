#pragma once

#include <string>
#include <vector>

// Shop furniture: more room to put things, somewhere to sit, and things to look at. It does nothing
// for the engines; the world gives each piece its model and its tops (see ShopWorld).
enum class FurnitureKind
{
	SHELVING,
	WORKBENCH,
	TOOL_CART,
	STOOL,
	OFFICE_CHAIR,
	OIL_DRUM,
	TRASH_CAN,
	FLOOR_MAT,
	POTTED_PLANT,
	SHOP_FAN,
	GAS_PUMP,
	MONCHU,			// a statue for show, its model from Resources/Models/monchu.fbx
	FLOOR_JACK,		// a trolley jack, rolled under one of the car's lifting points and pumped up
	JACK_STAND		// a stand set under the car where it's been jacked up, to hold it there
};

struct FurnitureInfo
{
	FurnitureKind kind = FurnitureKind::SHELVING;
	std::string name = "";
	std::string description = "";
	double price = 0.0;
};

namespace FurnitureCatalog
{
	// In store order; the index is what the shop saves.
	const std::vector<FurnitureInfo>& All();
}
