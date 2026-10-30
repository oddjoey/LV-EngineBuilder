#pragma once

#include <vector>

#include "Part.h"

struct EngineSpec;

// Which part of the engine a part belongs to; the store and checklists group by it.
enum class PartSystem
{
	BOTTOM_END,
	TOP_END,
	VALVETRAIN,
	TIMING,
	LUBRICATION,
	COOLING,
	FUEL_AND_AIR,
	EXHAUST,
	IGNITION,
	FASTENERS,
	COUNT
};

const char* PartSystemName(PartSystem system);

// How many of a part one engine takes.
namespace PartCount
{
	constexpr int PER_CYLINDER = -1;
	constexpr int PER_VALVE = -2;		// one per valve: two per cylinder
	constexpr int PER_MAIN = -3;		// one per main journal
	constexpr int PER_CAM = -4;			// one per cam journal
}

// What a nut or bolt is: its thread and the spanner it takes. Where it goes and how tight is the
// hole's business (PartCatalog::Holes).
struct FastenerSpec
{
	const char* thread = "";
	int headMm = 0;						// socket or wrench size; 0: not a fastener
	bool nut = false;
	bool IsFastener() const { return headMm > 0; }
};

// A set of holes on the engine: the parts they hold on, the size that goes in them and how tight.
struct FastenerHoles
{
	const char* name = "";				// "Oil Pan Bolt"
	PartType parent = PartType::BLOCK;	// the part they hold on
	int perParent = 0;					// holes per parent part
	PartType size = PartType::BOLT_M6;
	double torqueNm = 0.0;
};

// Everything the game knows about a kind of part apart from its geometry.
struct PartInfo
{
	PartType type = PartType::BLOCK;
	const char* name = "";
	PartSystem system = PartSystem::BOTTOM_END;
	double basePrice = 0.0;			// new, OEM quality
	int perEngine = 1;				// a count, or one of PartCount
	bool soldUsed = false;			// the store offers used ones
	bool inCore = false;			// a used core engine comes with them
	bool singleUse = false;			// crushed, stretched or torn once fitted: never goes back on
	bool big = false;				// carried in your hands, delivered in a crate
	int gridWidth = 1;				// backpack cells (0 x 0 for anything that can't go in it)
	int gridHeight = 1;
	const char* description = "";
	// What the store calls it, when it's sold as a set (bearing shells come in pairs).
	const char* storeName = nullptr;
	// Sold on its own. The other halves of a set (lower shells, caps) come with their partner.
	bool listed = true;
	FastenerSpec fastener = {};
};

namespace PartCatalog
{
	const PartInfo& Get(PartType type);
	// Every part type, in build order within each system.
	const std::vector<PartType>& All();
	// How many one engine of this spec takes.
	int CountPerEngine(PartType type, const EngineSpec& spec);
	// The name on the store's card.
	const char* StoreName(PartType type);
	// Every set of holes on the engine, in AssemblySlot order from AssemblySlot::HEAD_BOLT.
	const std::vector<FastenerHoles>& Holes();
}
