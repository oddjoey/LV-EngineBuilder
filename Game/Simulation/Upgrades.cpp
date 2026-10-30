#include "Upgrades.h"

namespace UpgradeCatalog
{
	const std::vector<UpgradeInfo>& All()
	{
		static const std::vector<UpgradeInfo> upgrades = {
			{ UpgradeKind::SIDE_BAY, "Garage Extension",
				"A second bay built onto the far side of the garage from the house: 6 x 8 m, with a wide opening knocked through. "
				"Room for more machines, benches and stands, and where the lift goes. The builders are done the moment it's paid for.", 8000.0 },
			{ UpgradeKind::SECOND_STORY, "Loft over the Garage",
				"A whole room on top of the garage, up steel stairs from the backyard, looking out over the house roof. Somewhere to keep "
				"parts and tools out of the way.", 15000.0 },
			{ UpgradeKind::POST_LIFT, "Two-Post Lift",
				"A 4-tonne two-post lift, delivered crated. Raise and lower it with E; it goes in the side bay if you have one.", 3500.0 },
		};
		return upgrades;
	}
}
