#include "InventoryUI.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <unordered_set>

#include <imgui.h>

#include "../Simulation/EngineAssembly.h"
#include "../Simulation/Material.h"
#include "../Simulation/PartCatalog.h"
#include "../Simulation/Workshop.h"
#include "../World/ShopWorld.h"
#include "ItemIcons.h"
#include "Ratings.h"

namespace
{
	constexpr float CELL = 56.f;
	constexpr float HOTBAR_SLOT = 52.f;
	const ImU32 CELL_COLOR = IM_COL32(32, 35, 40, 255);
	const ImU32 CELL_LINE = IM_COL32(60, 64, 72, 255);
	const ImU32 ITEM_FILL = IM_COL32(58, 66, 80, 255);
	const ImU32 TOOL_FILL = IM_COL32(52, 74, 70, 255);
	const ImU32 EQUIPPED_LINE = IM_COL32(255, 200, 80, 255);
}

void InventoryUI::CancelDrag()
{
	m_dragging = {};
}

void InventoryUI::SetUnits(Units::System units)
{
	m_units = units;
}

void InventoryUI::DrawContents(ShopWorld& world, Workshop& workshop, const FirstPersonController& player)
{
	auto& inventory = world.GetInventory();
	ImGuiIO& io = ImGui::GetIO();

	const float gridWidth = PlayerInventory::WIDTH * CELL;
	const float gridHeight = PlayerInventory::HEIGHT * CELL;

	// Left: the grid and hotbar. Right: details of one item and the engine's parts list.
	ImGui::BeginGroup();
	// Wrapped to the grid's width: any wider and it pushes the details panel off the window.
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + gridWidth);
	ImGui::TextDisabled("Drag to move   R while dragging: turn   Drag out: drop   Right click: options   "
		"Shift+click: between hotbar and backpack   1-0 on an item: onto that hotbar slot");
	ImGui::PopTextWrapPos();

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 gridMin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("Grid", { gridWidth, gridHeight });
	bool gridHovered = ImGui::IsItemHovered();

	for (int y = 0; y < PlayerInventory::HEIGHT; y++)
		for (int x = 0; x < PlayerInventory::WIDTH; x++)
		{
			ImVec2 min = { gridMin.x + x * CELL, gridMin.y + y * CELL };
			drawList->AddRectFilled(min, { min.x + CELL, min.y + CELL }, CELL_COLOR);
			drawList->AddRect(min, { min.x + CELL, min.y + CELL }, CELL_LINE);
		}

	ImVec2 mouse = io.MousePos;
	ItemRef hovered = {};

	for (const auto& entry : inventory.GetItems())
	{
		if (entry.item == m_dragging)
			continue;
		auto info = world.DescribeItem(entry.item, workshop);
		ImVec2 min = { gridMin.x + entry.position.x * CELL + 2.f, gridMin.y + entry.position.y * CELL + 2.f };
		ImVec2 max = { min.x + entry.size.x * CELL - 4.f, min.y + entry.size.y * CELL - 4.f };

		drawList->AddRectFilled(min, max, info.isTool ? TOOL_FILL : ITEM_FILL, 4.f);

		float iconSize = (std::min)(max.x - min.x, max.y - min.y) - 6.f;
		ItemIcons::DrawItem(drawList, info.isTool, info.toolIndex, info.partType,
			{ (min.x + max.x - iconSize) * 0.5f, (min.y + max.y - iconSize) * 0.5f }, iconSize);

		bool over = mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y;
		if (over && gridHovered)
			hovered = entry.item;
	}

	// Start dragging, shift-click it onto the hotbar, or open the options for an item.
	// The details panel shows the item; the tooltip just names it.
	if (!hovered.IsNone() && m_dragging.IsNone())
	{
		ImGui::SetTooltip("%s", world.DescribeItem(hovered, workshop).name.c_str());

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyShift)
		{
			// Into the selected slot if it's empty, else the first empty one.
			auto slot = inventory.FreeHotbarSlot();
			m_status = slot && world.MoveToHotbar(hovered, *slot) ? "" : "The hotbar's full.";
		}
		else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			const GridItem* entry = inventory.Find(hovered);
			m_selected = hovered;
			m_dragging = hovered;
			m_dragSize = entry->size;
			m_grabOffset = { (mouse.x - gridMin.x) / CELL - entry->position.x, (mouse.y - gridMin.y) / CELL - entry->position.y };
		}
		else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			m_contextItem = hovered;
			ImGui::OpenPopup("ItemOptions");
		}
	}

	// Hotbar row: the items themselves, whatever their size. The selected slot is the one in hand.
	ImGui::Spacing();
	ImGui::TextUnformatted("Hotbar");
	ImVec2 hotbarMin = ImGui::GetCursorScreenPos();
	float slotStride = HOTBAR_SLOT + 6.f;
	// A button, like the grid, so pressing and dragging here moves items rather than the window.
	ImGui::InvisibleButton("HotbarRow", { PlayerInventory::HOTBAR_SLOTS * slotStride, HOTBAR_SLOT });
	int hoveredSlot = -1;
	for (int slot = 0; slot < PlayerInventory::HOTBAR_SLOTS; slot++)
	{
		ImVec2 min = { hotbarMin.x + slot * slotStride, hotbarMin.y };
		ImVec2 max = { min.x + HOTBAR_SLOT, min.y + HOTBAR_SLOT };
		bool over = mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y;
		if (over)
			hoveredSlot = slot;

		auto item = inventory.GetHotbar(slot);
		bool selected = slot == inventory.GetSelected() && !world.IsHandEmptied();
		drawList->AddRectFilled(min, max, over && !m_dragging.IsNone() ? IM_COL32(60, 80, 110, 255) : CELL_COLOR, 4.f);
		drawList->AddRect(min, max, selected ? EQUIPPED_LINE : CELL_LINE, 4.f, 0, selected ? 2.f : 1.f);
		if (!item.IsNone() && item != m_dragging)
		{
			auto info = world.DescribeItem(item, workshop);
			ItemIcons::DrawItem(drawList, info.isTool, info.toolIndex, info.partType, { min.x + 5.f, min.y + 5.f }, HOTBAR_SLOT - 10.f);
			if (over && m_dragging.IsNone())
			{
				ImGui::SetTooltip("%s", info.name.c_str());
				hovered = item;
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyShift)
				{
					// Shift-click: straight into the backpack, if there's room.
					m_status = world.MoveToBackpack(item) ? "" : std::format("No room in the backpack for the {}.", info.name);
					hovered = {};
				}
				else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					// Picked up by its middle, at the size it takes in the backpack.
					m_selected = item;
					m_dragging = item;
					m_dragSize = glm::max(info.size, glm::ivec2(1));
					m_grabOffset = glm::vec2(m_dragSize) * 0.5f;
				}
				else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					m_contextItem = item;
					ImGui::OpenPopup("ItemOptions");
				}
			}
		}
		drawList->AddText({ min.x + 3.f, min.y + 1.f }, IM_COL32(200, 200, 200, 220), std::to_string((slot + 1) % 10).c_str());
	}

	// 1-0 with the cursor on an item puts it on that hotbar slot (swapping with what's there).
	if (!hovered.IsNone() && m_dragging.IsNone())
		for (int slot = 0; slot < PlayerInventory::HOTBAR_SLOTS; slot++)
			if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + (slot + 1) % 10), false))
				world.MoveToHotbar(hovered, slot);

	// Dragging: the item follows the cursor and snaps to the cell under it.
	if (!m_dragging.IsNone())
	{
		if (ImGui::IsKeyPressed(ImGuiKey_R, false))
		{
			m_dragSize = { m_dragSize.y, m_dragSize.x };
			m_grabOffset = { m_grabOffset.y, m_grabOffset.x };
		}

		glm::ivec2 cell = { static_cast<int>(std::floor((mouse.x - gridMin.x) / CELL - m_grabOffset.x + 0.5f)),
			static_cast<int>(std::floor((mouse.y - gridMin.y) / CELL - m_grabOffset.y + 0.5f)) };
		bool overGrid = mouse.x >= gridMin.x && mouse.x < gridMin.x + gridWidth && mouse.y >= gridMin.y && mouse.y < gridMin.y + gridHeight;
		auto info = world.DescribeItem(m_dragging, workshop);
		// Something that can't go in the backpack can only go between hotbar slots.
		bool fits = PlayerInventory::CanEverFit(info.size) && inventory.CanPlace(cell, m_dragSize, m_dragging);

		ImDrawList* front = ImGui::GetForegroundDrawList();
		if (overGrid)
		{
			ImVec2 min = { gridMin.x + cell.x * CELL, gridMin.y + cell.y * CELL };
			front->AddRectFilled(min, { min.x + m_dragSize.x * CELL, min.y + m_dragSize.y * CELL },
				fits ? IM_COL32(80, 200, 100, 70) : IM_COL32(220, 80, 70, 70), 4.f);
		}
		ImVec2 ghostMin = { mouse.x - m_grabOffset.x * CELL, mouse.y - m_grabOffset.y * CELL };
		ImVec2 ghostMax = { ghostMin.x + m_dragSize.x * CELL, ghostMin.y + m_dragSize.y * CELL };
		front->AddRectFilled(ghostMin, ghostMax, IM_COL32(70, 80, 100, 200), 4.f);
		float iconSize = (std::min)(ghostMax.x - ghostMin.x, ghostMax.y - ghostMin.y) - 6.f;
		ItemIcons::DrawItem(front, info.isTool, info.toolIndex, info.partType,
			{ (ghostMin.x + ghostMax.x - iconSize) * 0.5f, (ghostMin.y + ghostMax.y - iconSize) * 0.5f }, iconSize);

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			bool overWindow = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_RootAndChildWindows);
			if (hoveredSlot >= 0)
				world.MoveToHotbar(m_dragging, hoveredSlot);
			else if (overGrid)
			{
				// Off the hotbar (maybe out of hand) or across the backpack.
				if (fits && inventory.MoveTo(m_dragging, cell, m_dragSize))
					world.SyncHand();
			}
			else if (!overWindow)
				world.DropFromInventory(m_dragging, player);
			m_dragging = {};
		}
	}

	if (ImGui::BeginPopup("ItemOptions"))
	{
		auto info = world.DescribeItem(m_contextItem, workshop);
		ImGui::TextDisabled("%s", info.name.c_str());
		ImGui::Separator();
		bool equipped = m_contextItem == world.GetEquipped();
		bool onHotbar = inventory.HotbarSlotOf(m_contextItem).has_value();
		if (ImGui::MenuItem(equipped ? "Put in backpack" : "Take in hand"))
			world.ToggleEquip(m_contextItem);
		if (onHotbar && !equipped && ImGui::MenuItem("Put in backpack"))
			world.MoveToBackpack(m_contextItem);
		if (ImGui::BeginMenu("Put on hotbar"))
		{
			for (int slot = 0; slot < PlayerInventory::HOTBAR_SLOTS; slot++)
			{
				auto there = inventory.GetHotbar(slot);
				std::string label = there.IsNone() ? std::format("Slot {}", (slot + 1) % 10)
					: std::format("Slot {} (swap with {})", (slot + 1) % 10, world.DescribeItem(there, workshop).name);
				if (ImGui::MenuItem(label.c_str(), nullptr, there == m_contextItem))
					world.MoveToHotbar(m_contextItem, slot);
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Drop"))
			world.DropFromInventory(m_contextItem, player);
		ImGui::EndPopup();
	}

	ImGui::Spacing();
	DrawSummary(world, workshop, gridWidth);
	ImGui::EndGroup();

	// What the panel shows: the item under the cursor, else the one being dragged, else the last
	// one clicked, else whatever is in hand.
	if (!m_selected.IsNone() && !inventory.Contains(m_selected) && m_selected != world.GetEquipped())
		m_selected = {};
	ItemRef shown = !hovered.IsNone() ? hovered : !m_dragging.IsNone() ? m_dragging : !m_selected.IsNone() ? m_selected : world.GetEquipped();

	ImGui::SameLine();
	if (ImGui::BeginChild("BackpackSide", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		DrawDetails(world, workshop, player, shown);
		ImGui::Spacing();
		DrawPlayerStats(world, workshop);
	}
	ImGui::EndChild();
}

void InventoryUI::DrawSummary(ShopWorld& world, Workshop& workshop, float width)
{
	auto& inventory = world.GetInventory();
	const int totalCells = PlayerInventory::WIDTH * PlayerInventory::HEIGHT;
	int usedCells = inventory.UsedCells();

	float buttonWidth = 90.f;
	ImGui::ProgressBar(static_cast<float>(usedCells) / totalCells, { width - buttonWidth - ImGui::GetStyle().ItemSpacing.x, 0.f },
		std::format("{} / {} cells used", usedCells, totalCells).c_str());
	ImGui::SameLine();
	ImGui::BeginDisabled(inventory.GetItems().empty());
	if (ImGui::Button("Tidy up", { buttonWidth, 0.f }))
		m_status = inventory.Tidy() ? "" : "Couldn't pack it any tighter.";
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Repack the backpack biggest first, from the top left. The hotbar is left as it is.");

	int parts = 0, tools = 0;
	double partsValue = 0.0;
	for (const auto& entry : inventory.GetItems())
	{
		auto info = world.DescribeItem(entry.item, workshop);
		if (info.isTool)
			tools++;
		else if (info.part)
		{
			parts++;
			partsValue += info.part->GetValue();
		}
	}
	ImGui::Text("%d part%s ($%.0f)   %d tool%s", parts, parts == 1 ? "" : "s", partsValue, tools, tools == 1 ? "" : "s");
	ImGui::SameLine();
	if (auto equipped = world.GetEquipped(); !equipped.IsNone())
		ImGui::Text("   In hand: %s", world.DescribeItem(equipped, workshop).name.c_str());
	else
		ImGui::TextDisabled("   Hands empty");
	if (!m_status.empty())
		ImGui::TextDisabled("%s", m_status.c_str());
}

void InventoryUI::DrawDetails(ShopWorld& world, Workshop& workshop, const FirstPersonController& player, const ItemRef& item)
{
	ImGui::SeparatorText("Item");
	if (item.IsNone())
	{
		ImGui::TextDisabled("Point at or click an item to see it here.");
		return;
	}

	auto& inventory = world.GetInventory();
	auto info = world.DescribeItem(item, workshop);

	// A big icon with the name and where the item is beside it.
	constexpr float ICON = 84.f;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 iconMin = ImGui::GetCursorScreenPos();
	drawList->AddRectFilled(iconMin, { iconMin.x + ICON, iconMin.y + ICON }, info.isTool ? TOOL_FILL : ITEM_FILL, 6.f);
	ItemIcons::DrawItem(drawList, info.isTool, info.toolIndex, info.partType, { iconMin.x + 6.f, iconMin.y + 6.f }, ICON - 12.f);
	ImGui::Dummy({ ICON, ICON });
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::PushTextWrapPos(0.f);
	ImGui::TextUnformatted(info.name.c_str());
	ImGui::PopTextWrapPos();
	ImGui::TextDisabled("Takes %d x %d cells", info.size.x, info.size.y);
	bool equipped = item == world.GetEquipped();
	bool carried = inventory.Contains(item);
	if (equipped)
		ImGui::TextColored({ 1.f, 0.8f, 0.3f, 1.f }, "In your hand");
	if (auto slot = inventory.HotbarSlotOf(item))
		ImGui::TextDisabled("Hotbar slot %d", (*slot + 1) % 10);
	else if (inventory.InBackpack(item))
		ImGui::TextDisabled("In your backpack: put it on the hotbar to use it");
	ImGui::EndGroup();

	ImGui::PushTextWrapPos(0.f);
	ImGui::TextDisabled("%s", info.description.c_str());
	ImGui::PopTextWrapPos();

	if (info.isTool)
	{
		const auto& tool = ToolCatalog::All()[info.toolIndex];
		Ratings::Bar("Precision", Ratings::ToolPrecision(tool), 160.f);
		Ratings::Bar("Speed", Ratings::ToolSpeed(tool), 160.f);
		if (tool.resolution > 0.0)
			ImGui::Text("Reads to %s, repeatable to %s", Units::FormatSize(tool.resolution, m_units).c_str(),
				Units::FormatDeviation(tool.sigma, m_units).c_str());
		if (tool.maxSize > 0.0)
			ImGui::TextDisabled("Fits %s to %s", Units::FormatSize(tool.minSize, m_units).c_str(), Units::FormatSize(tool.maxSize, m_units).c_str());
	}
	else if (info.part && ImGui::BeginTable("PartFacts", 2, ImGuiTableFlags_SizingFixedFit))
	{
		const Part& part = *info.part;
		auto row = [](const char* label) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", label);
			ImGui::TableNextColumn();
		};

		row("Material");
		ImGui::TextUnformatted(GetMaterial(part.GetMaterial()).name);
		row("Quality");
		ImGui::TextUnformatted(PartQualityName(part.GetQuality()));
		row("Condition");
		if (part.GetCondition() == PartCondition::DAMAGED)
			ImGui::TextColored({ 1.f, 0.45f, 0.4f, 1.f }, "%s", PartConditionName(part.GetCondition()));
		else
			ImGui::TextUnformatted(PartConditionName(part.GetCondition()));
		row("Size");
		double offset = part.GetSizeOffset();
		if (std::abs(offset) < 1e-6)
			ImGui::TextUnformatted("Standard");
		else
			ImGui::Text("%s %s", Units::FormatSize(std::abs(offset), m_units).c_str(), offset > 0.0 ? "oversize" : "undersize");
		row("Worth");
		ImGui::Text("$%.0f", part.GetValue());

		// How much of the part has been checked, and whether machining has outdated any of it.
		int features = static_cast<int>(part.GetFeatures().size());
		int measured = 0, stale = 0;
		for (const auto& feature : part.GetFeatures())
			if (const auto* reading = part.GetReading(feature.name))
				(reading->stale ? stale : measured)++;
		if (features > 0)
		{
			row("Measured");
			ImGui::Text("%d of %d", measured, features);
			if (stale > 0)
			{
				ImGui::SameLine();
				ImGui::TextColored({ 1.f, 0.75f, 0.3f, 1.f }, "(%d out of date)", stale);
			}
		}
		ImGui::EndTable();
	}

	if (carried)
	{
		if (ImGui::Button(equipped ? "Put in backpack" : "Take in hand"))
			world.ToggleEquip(item);
		ImGui::SameLine();
		if (ImGui::Button("Drop"))
		{
			world.DropFromInventory(item, player);
			if (m_selected == item)
				m_selected = {};
		}
	}
}

void InventoryUI::DrawPlayerStats(ShopWorld& world, Workshop& workshop)
{
	const auto& stats = workshop.GetStats();
	const ImVec4 good = { 0.45f, 0.85f, 0.5f, 1.f };
	const ImVec4 bad = { 1.f, 0.45f, 0.45f, 1.f };
	bool imperial = m_units == Units::System::IMPERIAL;

	auto section = [&](const char* title) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Spacing();
		ImGui::TextDisabled("%s", title);
	};
	auto row = [&](const char* label, const std::string& value, std::optional<ImVec4> color = std::nullopt) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(label);
		ImGui::TableNextColumn();
		if (color)
			ImGui::TextColored(*color, "%s", value.c_str());
		else
			ImGui::TextUnformatted(value.c_str());
	};
	auto money = [](double amount) { return std::format("${:.0f}", amount); };

	ImGui::SeparatorText("Player stats");
	if (!ImGui::BeginTable("PlayerStats", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		return;
	ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_WidthStretch, 2.f);
	ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.f);

	section("Money");
	row("In the bank", money(workshop.GetMoney()), good);
	row("Earned", money(stats.moneyEarned));
	row("Spent", money(stats.moneySpent));
	double profit = stats.moneyEarned - stats.moneySpent;
	row("Profit", std::format("{}{}", profit < 0.0 ? "-" : "", money(std::abs(profit))), profit < 0.0 ? bad : good);

	section("Shop");
	row("Hours on the clock", std::format("{:.1f} h", workshop.GetHours()));
	row("Parts bought", std::to_string(stats.partsBought));
	row("Parts sold", std::to_string(stats.partsSold));
	row("Parts fitted", std::to_string(stats.partsFitted));
	row("Measurements taken", std::to_string(stats.measurements));
	row("Machining cuts", std::to_string(stats.cuts));
	if (int packages = world.GetUnopenedPackages(); packages > 0)
		row("Deliveries waiting", std::to_string(packages), ImVec4(1.f, 0.8f, 0.35f, 1.f));

	section("Dyno and sales");
	row("Dyno pulls", std::to_string(stats.dynoPulls));
	row("Engines blown up", std::to_string(stats.dynoFailures), stats.dynoFailures > 0 ? std::optional<ImVec4>(bad) : std::nullopt);
	row("Best power", stats.bestPowerKw > 0.0 ? std::format("{:.0f} hp ({:.0f} kW)", stats.bestPowerKw * 1.341, stats.bestPowerKw) : "-");
	row("Best torque", stats.bestTorqueNm > 0.0
		? (imperial ? std::format("{:.0f} lb-ft", stats.bestTorqueNm * 0.7376) : std::format("{:.0f} Nm", stats.bestTorqueNm)) : "-");
	row("Engines sold", std::to_string(stats.enginesSold));
	row("Best sale", stats.bestEngineSale > 0.0 ? money(stats.bestEngineSale) : "-");

	section("On foot");
	double metres = stats.metresWalked;
	row("Distance walked", imperial ? (metres >= 1609.0 ? std::format("{:.2f} mi", metres / 1609.34) : std::format("{:.0f} ft", metres * 3.2808))
		: (metres >= 1000.0 ? std::format("{:.2f} km", metres / 1000.0) : std::format("{:.0f} m", metres)));

	ImGui::EndTable();
}
