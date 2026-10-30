#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

// Something the player can pick up: a part (by part id) or a measuring tool (by catalog index).
struct ItemRef
{
	enum class Kind : uint8_t { NONE, PART, TOOL };

	Kind kind = Kind::NONE;
	uint32_t id = 0;

	static ItemRef Part(uint32_t partId) { return { Kind::PART, partId }; }
	static ItemRef Tool(uint32_t toolIndex) { return { Kind::TOOL, toolIndex }; }

	bool IsNone() const { return kind == Kind::NONE; }
	bool IsPart() const { return kind == Kind::PART; }
	bool IsTool() const { return kind == Kind::TOOL; }
	bool operator==(const ItemRef& other) const = default;
};

struct ItemRefHash
{
	size_t operator()(const ItemRef& item) const
	{
		return std::hash<uint64_t>()((static_cast<uint64_t>(item.kind) << 32) | item.id);
	}
};

struct GridItem
{
	ItemRef item = {};
	glm::ivec2 position = {};
	glm::ivec2 size = {};	// in cells, as currently turned
};

// What the player carries: a hotbar of ten slots that each hold one whole item, whatever its size,
// and a Tarkov-style backpack where items take up a rectangle of cells and can be turned 90
// degrees. An item is in one or the other. The item in the selected hotbar slot is the one in hand;
// anything in the backpack has to go on the hotbar before it can be held.
class PlayerInventory
{
public:
	static constexpr int WIDTH = 10;
	static constexpr int HEIGHT = 6;
	static constexpr int HOTBAR_SLOTS = 10;

	// Whether an item of this size could ever go in the backpack, turned or not.
	static bool CanEverFit(glm::ivec2 size);

	// Backpack. AddToBackpack puts the item in the first space it fits, turning it if needed (taking
	// it off the hotbar); false if there's no room.
	bool AddToBackpack(const ItemRef& item, glm::ivec2 size);
	bool CanPlace(glm::ivec2 position, glm::ivec2 size, const ItemRef& ignore = {}) const;
	// Moves an item to a spot in the backpack, from elsewhere in it or from the hotbar.
	bool MoveTo(const ItemRef& item, glm::ivec2 position, glm::ivec2 size);
	// Repacks the backpack biggest first from the top left. False (and nothing moved) if it wouldn't
	// all fit that way.
	bool Tidy();
	int UsedCells() const;
	bool InBackpack(const ItemRef& item) const;
	const GridItem* Find(const ItemRef& item) const;
	const std::vector<GridItem>& GetItems() const;

	// Hotbar. PutInHotbar needs the slot empty (or holding the item already) and takes the item out
	// of the backpack or its other slot.
	bool PutInHotbar(int slot, const ItemRef& item);
	// An item from the backpack into a hotbar slot, and whatever was in the slot (occupantSize big)
	// into the backpack: where the item was if it fits there, else anywhere. False (nothing moved) if
	// it doesn't fit at all.
	bool ExchangeWithBackpack(int slot, const ItemRef& item, glm::ivec2 occupantSize);
	void SwapHotbar(int a, int b);
	ItemRef GetHotbar(int slot) const;
	std::optional<int> HotbarSlotOf(const ItemRef& item) const;
	// The selected slot if it's empty, else the next empty one after it (wrapping round).
	std::optional<int> FreeHotbarSlot() const;
	int GetSelected() const;
	void SetSelected(int slot);
	ItemRef GetSelectedItem() const;

	// Anywhere: the backpack or the hotbar.
	bool Contains(const ItemRef& item) const;
	void Remove(const ItemRef& item);

	// Replaces the whole backpack and hotbar, e.g. when loading a save. An item on the hotbar comes
	// out of the backpack (older saves had hotbar items in both).
	void Restore(const std::vector<GridItem>& items, const std::array<ItemRef, HOTBAR_SLOTS>& hotbar, int selected);
private:
	std::optional<glm::ivec2> FindSpace(glm::ivec2 size) const;
	void RemoveFromBackpack(const ItemRef& item);
	void RemoveFromHotbar(const ItemRef& item);

	std::vector<GridItem>					m_items = {};
	std::array<ItemRef, HOTBAR_SLOTS>		m_hotbar = {};
	int										m_selected = 0;
};
