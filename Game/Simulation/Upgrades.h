#pragma once

#include <string>
#include <vector>

// Work done on the shop itself. The building ones happen as soon as they're paid for; the lift is
// delivered crated like a machine.
enum class UpgradeKind
{
	SIDE_BAY,		// a bay built onto the left wall, with an opening knocked through
	SECOND_STORY,	// a room on top of the garage, up stairs outside the back wall
	POST_LIFT		// a two-post lift
};

struct UpgradeInfo
{
	UpgradeKind kind = UpgradeKind::SIDE_BAY;
	std::string name = "";
	std::string description = "";
	double price = 0.0;
};

namespace UpgradeCatalog
{
	// Indexed by UpgradeKind.
	const std::vector<UpgradeInfo>& All();
}
