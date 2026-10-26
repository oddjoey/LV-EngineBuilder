#pragma once

#include <string>

#include "../Inventory/PlayerInventory.h"
#include "../Simulation/Units.h"

class ShopWorld;
class Workshop;
class FirstPersonController;

// The backpack tab: a Tarkov-style grid with drag and drop, R to turn an item while dragging,
// a hotbar row to bind items to 1-0, and dragging out of the window to drop. Beside it, the
// item under the cursor (or the one clicked last) in detail, and what the engine still needs.
class InventoryUI
{
public:
	// Draws into the current window (the workshop window's Backpack tab).
	void DrawContents(ShopWorld& world, Workshop& workshop, const FirstPersonController& player);
	void CancelDrag();
	void SetUnits(Units::System units);
private:
	// How full the backpack is, what's in it, and the tidy-up button.
	void DrawSummary(ShopWorld& world, Workshop& workshop, float width);
	void DrawDetails(ShopWorld& world, Workshop& workshop, const FirstPersonController& player, const ItemRef& item);
	// Each kind of part the engine takes: how many are fitted and how many more are in the shop.
	void DrawEngineChecklist(ShopWorld& world, Workshop& workshop);

	ItemRef		m_dragging = {};
	glm::ivec2	m_dragSize = {};		// as currently turned
	glm::vec2	m_grabOffset = {};		// cursor position within the dragged item, in cells
	ItemRef		m_contextItem = {};
	ItemRef		m_selected = {};		// the last item clicked, shown in the details panel
	std::string	m_tidyStatus = "";
	Units::System m_units = Units::System::METRIC;
};
