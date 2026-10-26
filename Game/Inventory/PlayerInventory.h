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

// A Tarkov-style backpack: items take up a rectangle of cells and can be turned 90 degrees.
// The hotbar binds number keys to items in the grid.
class PlayerInventory
{
public:
	static constexpr int WIDTH = 10;
	static constexpr int HEIGHT = 6;
	static constexpr int HOTBAR_SLOTS = 10;

	// Whether an item of this size could ever go in the backpack, turned or not.
	static bool CanEverFit(glm::ivec2 size);

	// Puts the item in the first space it fits, turning it if needed, and binds it to the first free
	// hotbar slot. False if there's no room.
	bool Add(const ItemRef& item, glm::ivec2 size);
	bool CanPlace(glm::ivec2 position, glm::ivec2 size, const ItemRef& ignore = {}) const;
	bool MoveTo(const ItemRef& item, glm::ivec2 position, glm::ivec2 size);
	// Takes the item out of the grid and off the hotbar.
	void Remove(const ItemRef& item);
	// Repacks everything biggest first from the top left. Hotbar bindings stay. False (and nothing
	// moved) if it wouldn't all fit that way.
	bool Tidy();
	int UsedCells() const;

	bool Contains(const ItemRef& item) const;
	const GridItem* Find(const ItemRef& item) const;
	const std::vector<GridItem>& GetItems() const;

	void AssignHotbar(int slot, const ItemRef& item);
	ItemRef GetHotbar(int slot) const;
	std::optional<int> HotbarSlotOf(const ItemRef& item) const;

	// Replaces the whole backpack and hotbar, e.g. when loading a save.
	void Restore(const std::vector<GridItem>& items, const std::array<ItemRef, HOTBAR_SLOTS>& hotbar);
private:
	std::optional<glm::ivec2> FindSpace(glm::ivec2 size) const;

	std::vector<GridItem>					m_items = {};
	std::array<ItemRef, HOTBAR_SLOTS>		m_hotbar = {};
};
