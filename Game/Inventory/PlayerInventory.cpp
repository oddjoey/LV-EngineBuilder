#include "PlayerInventory.h"

#include <algorithm>

bool PlayerInventory::CanEverFit(glm::ivec2 size)
{
	if (size.x <= 0 || size.y <= 0)
		return false;
	return (size.x <= WIDTH && size.y <= HEIGHT) || (size.y <= WIDTH && size.x <= HEIGHT);
}

bool PlayerInventory::CanPlace(glm::ivec2 position, glm::ivec2 size, const ItemRef& ignore) const
{
	if (position.x < 0 || position.y < 0 || position.x + size.x > WIDTH || position.y + size.y > HEIGHT)
		return false;

	for (const auto& other : m_items)
	{
		if (other.item == ignore)
			continue;
		bool overlaps = position.x < other.position.x + other.size.x && other.position.x < position.x + size.x
			&& position.y < other.position.y + other.size.y && other.position.y < position.y + size.y;
		if (overlaps)
			return false;
	}
	return true;
}

std::optional<glm::ivec2> PlayerInventory::FindSpace(glm::ivec2 size) const
{
	for (int y = 0; y < HEIGHT; y++)
		for (int x = 0; x < WIDTH; x++)
			if (CanPlace({ x, y }, size))
				return glm::ivec2(x, y);
	return std::nullopt;
}

bool PlayerInventory::Tidy()
{
	// Biggest first, lying flat, packed from the top left. If that somehow fits worse than the
	// player's own arrangement, keep theirs.
	std::vector<GridItem> previous = m_items;
	std::vector<GridItem> sorted = m_items;
	std::stable_sort(sorted.begin(), sorted.end(), [](const GridItem& a, const GridItem& b) {
		return a.size.x * a.size.y > b.size.x * b.size.y;
	});

	m_items.clear();
	for (const auto& entry : sorted)
	{
		glm::ivec2 size = { (std::max)(entry.size.x, entry.size.y), (std::min)(entry.size.x, entry.size.y) };
		auto position = FindSpace(size);
		if (!position)
		{
			size = { size.y, size.x };
			position = FindSpace(size);
		}
		if (!position)
		{
			m_items = previous;
			return false;
		}
		m_items.push_back({ entry.item, *position, size });
	}
	return true;
}

int PlayerInventory::UsedCells() const
{
	int cells = 0;
	for (const auto& entry : m_items)
		cells += entry.size.x * entry.size.y;
	return cells;
}

bool PlayerInventory::Add(const ItemRef& item, glm::ivec2 size)
{
	if (item.IsNone() || Contains(item) || !CanEverFit(size))
		return false;

	auto position = FindSpace(size);
	if (!position)
	{
		size = { size.y, size.x };
		position = FindSpace(size);
	}
	if (!position)
		return false;

	m_items.push_back({ item, *position, size });

	if (!HotbarSlotOf(item))
		for (int slot = 0; slot < HOTBAR_SLOTS; slot++)
			if (m_hotbar[slot].IsNone())
			{
				m_hotbar[slot] = item;
				break;
			}
	return true;
}

bool PlayerInventory::MoveTo(const ItemRef& item, glm::ivec2 position, glm::ivec2 size)
{
	auto it = std::find_if(m_items.begin(), m_items.end(), [&](const GridItem& entry) { return entry.item == item; });
	if (it == m_items.end() || !CanPlace(position, size, item))
		return false;

	it->position = position;
	it->size = size;
	return true;
}

void PlayerInventory::Remove(const ItemRef& item)
{
	m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [&](const GridItem& entry) { return entry.item == item; }), m_items.end());
	for (auto& slot : m_hotbar)
		if (slot == item)
			slot = {};
}

bool PlayerInventory::Contains(const ItemRef& item) const
{
	return Find(item) != nullptr;
}

const GridItem* PlayerInventory::Find(const ItemRef& item) const
{
	for (const auto& entry : m_items)
		if (entry.item == item)
			return &entry;
	return nullptr;
}

const std::vector<GridItem>& PlayerInventory::GetItems() const
{
	return m_items;
}

void PlayerInventory::AssignHotbar(int slot, const ItemRef& item)
{
	if (slot < 0 || slot >= HOTBAR_SLOTS)
		return;
	if (!item.IsNone() && !Contains(item))
		return;

	// An item lives in one slot at a time.
	for (auto& existing : m_hotbar)
		if (existing == item)
			existing = {};
	m_hotbar[slot] = item;
}

ItemRef PlayerInventory::GetHotbar(int slot) const
{
	if (slot < 0 || slot >= HOTBAR_SLOTS)
		return {};
	return m_hotbar[slot];
}

std::optional<int> PlayerInventory::HotbarSlotOf(const ItemRef& item) const
{
	for (int slot = 0; slot < HOTBAR_SLOTS; slot++)
		if (!item.IsNone() && m_hotbar[slot] == item)
			return slot;
	return std::nullopt;
}

void PlayerInventory::Restore(const std::vector<GridItem>& items, const std::array<ItemRef, HOTBAR_SLOTS>& hotbar)
{
	m_items = items;
	m_hotbar = hotbar;
}
