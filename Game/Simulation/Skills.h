#pragma once

#include <array>
#include <cstddef>

// Skill points: the shop earns them with its reputation (one for every REPUTATION_PER_POINT) and
// shares them, whoever earned them, to spend on perks: cosmetics, the shop's trade, better tools,
// better hands and building onto the shack. A perk has levels, each dearer than the last. They make a
// tree: past the root of each branch, a perk opens up once the one above it has been bought. Saves
// keep them by number, so new ones go on the end.
enum class SkillKind
{
	PREMIUM_COLORS,		// cosmetic: gold, chrome and neon for your monkey
	SHOWROOM_PAINT,		// cosmetic: gold, chrome and candy teal spray paint in the store
	DYNO_CONTRACT,		// shop: cheaper dyno pulls
	SIDE_BAY,			// building: a bay built onto the left wall (UpgradeKind::SIDE_BAY)
	SUPPLIER,			// tools: the store sells tools a rank above the shop's
	QUICK_HANDS,		// stats: nuts and bolts turn quicker
	STEADY_HANDS,		// stats: measurements scatter less
	BATTERY_SAVER,		// tools: cordless tools use less of their pack
	SALESMAN,			// shop: engines sell for more
	LONG_LEGS,			// stats: walk and run quicker
	SOCKETS,			// tools: the store sells ratchets, sockets and everything that drives them
	SECOND_STORY,		// building: a room on top of the shack (UpgradeKind::SECOND_STORY)
	POST_LIFT,			// building: a two-post lift (UpgradeKind::POST_LIFT)
	WARDROBE,			// cosmetic: dressing your monkey at all (fur, face, cap and tail)
	COUNT
};

namespace Skills
{
	constexpr double REPUTATION_PER_POINT = 20.0;
	constexpr size_t COUNT = static_cast<size_t>(SkillKind::COUNT);

	enum class Group { COSMETICS, SHOP, TOOLS, STATS, BUILDING };
	inline const char* GroupName(Group group)
	{
		switch (group)
		{
			case Group::COSMETICS:	return "Cosmetics";
			case Group::SHOP:		return "Shop";
			case Group::TOOLS:		return "Tools";
			case Group::STATS:		return "Stats";
			default:				return "Building";
		}
	}

	struct Info
	{
		SkillKind kind;
		Group group;
		const char* name;
		const char* description;	// what a level does
		int maxLevel;
		int cost;					// points for the first level; each after costs this much more
		SkillKind after;			// the perk to buy first, or COUNT for the root of a branch
	};

	// A branch per group, each perk opening up the ones under it.
	inline const std::array<Info, COUNT>& All()
	{
		constexpr SkillKind ROOT = SkillKind::COUNT;
		static const std::array<Info, COUNT> skills = { {
			{ SkillKind::PREMIUM_COLORS, Group::COSMETICS, "Premium Colours", "Gold, chrome and neon fur and caps for your monkey, in the Backpack tab.", 1, 3, SkillKind::WARDROBE },
			{ SkillKind::SHOWROOM_PAINT, Group::COSMETICS, "Showroom Paint", "Gold, chrome and candy teal spray paint in the store's Paint tab.", 1, 3, SkillKind::PREMIUM_COLORS },
			{ SkillKind::DYNO_CONTRACT, Group::SHOP, "Dyno Fuel Contract", "Each level takes 25% off a dyno pull.", 2, 1, SkillKind::SALESMAN },
			{ SkillKind::SIDE_BAY, Group::BUILDING, "Side Bay", "A bay built onto the shack's left wall, with an opening knocked through and a roll-up door of its own.", 1, 4, ROOT },
			{ SkillKind::SUPPLIER, Group::TOOLS, "Supplier Connections", "Each level, the store sells you tools one shop rank higher than yours.", 2, 4, SkillKind::BATTERY_SAVER },
			{ SkillKind::QUICK_HANDS, Group::STATS, "Quick Hands", "Each level, nuts and bolts turn 15% quicker with any tool.", 3, 1, SkillKind::LONG_LEGS },
			{ SkillKind::STEADY_HANDS, Group::STATS, "Steady Hands", "Each level, every measuring tool scatters 15% less from reading to reading.", 3, 2, SkillKind::QUICK_HANDS },
			{ SkillKind::BATTERY_SAVER, Group::TOOLS, "Battery Saver", "Each level, cordless tools use 25% less of their pack.", 2, 1, SkillKind::SOCKETS },
			{ SkillKind::SALESMAN, Group::SHOP, "Salesman", "Each level, engines sell for 5% more.", 3, 2, ROOT },
			{ SkillKind::LONG_LEGS, Group::STATS, "Long Legs", "Each level, you walk and run 8% quicker.", 2, 1, ROOT },
			{ SkillKind::SOCKETS, Group::TOOLS, "Sockets & Ratchets", "The store sells ratchets, breaker bars, the large sockets, the click and digital torque wrenches and the "
				"cordless drivers. (The beam torque wrench and the 8-19 mm sockets are sold from the start.)", 1, 2, ROOT },
			{ SkillKind::SECOND_STORY, Group::BUILDING, "Second Story", "A room on top of the shack, up stairs inside and out. Parts and tools set down up there stay there.", 1, 6, SkillKind::SIDE_BAY },
			{ SkillKind::POST_LIFT, Group::BUILDING, "Two-Post Lift", "A two-post lift, delivered crated and set up in the side bay. E raises and lowers it.", 1, 5, SkillKind::SIDE_BAY },
			{ SkillKind::WARDROBE, Group::COSMETICS, "Wardrobe", "Dress your monkey, beside the backpack: its fur, face, cap and tail, in any colour. "
				"Without it, you're the shop's monkey like everyone else.", 1, 1, ROOT },
		} };
		return skills;
	}
	inline const Info& Of(SkillKind kind) { return All()[static_cast<size_t>(kind)]; }
	// How far down its branch a perk is: 0 for the root.
	inline int Depth(SkillKind kind)
	{
		int depth = 0;
		for (SkillKind up = Of(kind).after; up != SkillKind::COUNT; up = Of(up).after)
			depth++;
		return depth;
	}
	// What the next level (1 up) costs.
	inline int CostOfLevel(SkillKind kind, int level) { return Of(kind).cost * level; }
}
