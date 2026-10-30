#include "Furniture.h"

namespace FurnitureCatalog
{
	const std::vector<FurnitureInfo>& All()
	{
		// New kinds go on the end, so saved furniture keeps its meaning.
		static const std::vector<FurnitureInfo> furniture = {
			{ FurnitureKind::SHELVING,		"Steel Shelving Unit",	"Four boards of bolted steel shelving: room for another engine's worth of parts.", 160.0 },
			{ FurnitureKind::WORKBENCH,		"Wooden Workbench",		"A second bench to set parts and tools out on. (Inspecting is still done at the main bench.)", 140.0 },
			{ FurnitureKind::TOOL_CART,		"Rolling Tool Cart",	"Red steel cart with drawers and a tray top. Wheel it next to the engine stand.", 95.0 },
			{ FurnitureKind::STOOL,			"Shop Stool",			"A round seat on four legs, the right height for the bench.", 30.0 },
			{ FurnitureKind::OFFICE_CHAIR,	"Office Chair",			"For the parts computer, or for staring at a dyno sheet.", 65.0 },
			{ FurnitureKind::OIL_DRUM,		"Oil Drum",				"A 55 gallon drum. Empty, but its lid makes a handy table.", 45.0 },
			{ FurnitureKind::TRASH_CAN,		"Shop Trash Can",		"For rags, boxes and the gaskets you'll never use again.", 20.0 },
			{ FurnitureKind::FLOOR_MAT,		"Anti-Fatigue Mat",		"Thick rubber mat. Your knees will thank you.", 35.0 },
			{ FurnitureKind::POTTED_PLANT,	"Potted Plant",			"Something alive in the shop.", 25.0 },
			{ FurnitureKind::SHOP_FAN,		"Pedestal Fan",			"Keeps the air moving on a hot day.", 55.0 },
			{ FurnitureKind::GAS_PUMP,		"Vintage Gas Pump",		"A restored pump from a long-gone filling station. Purely for show.", 350.0 },
		};
		return furniture;
	}
}
