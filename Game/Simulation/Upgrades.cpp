#include "Upgrades.h"

namespace UpgradeCatalog
{
	const std::vector<UpgradeInfo>& All()
	{
		static const std::vector<UpgradeInfo> upgrades = {
			{ UpgradeKind::SIDE_BAY, "Side Bay",
				"A second bay built onto the side of the shack: 5 x 6 m, with a wide opening knocked through. "
				"Room for more machines, benches and stands, and where the lift goes.", 8000.0 },
			{ UpgradeKind::SECOND_STORY, "Second Story",
				"A whole room on top of the shack, up stairs inside and up the back wall outside. Somewhere to keep "
				"parts and tools out of the way.", 15000.0 },
			{ UpgradeKind::POST_LIFT, "Two-Post Lift",
				"A 4-tonne two-post lift, delivered crated. Raise and lower it with E; it goes in the side bay if you have one.", 3500.0 },
		};
		return upgrades;
	}
}
