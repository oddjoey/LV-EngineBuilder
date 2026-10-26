#include "ShopWorld.h"

#include <glm/gtc/constants.hpp>

#include "LV/Engine/Objects/ObjectManager.h"
#include "GarageLayout.h"
#include "ShopWorldShared.h"

using namespace ShopLayout;

namespace
{
	FloorObstacle Footprint(const glm::vec2& center, const glm::vec2& halfSize)
	{
		return { center - halfSize, center + halfSize };
	}
}

void ShopWorld::BuildOutdoors()
{
	using namespace GarageLayout;
	const float outerX = HALF_WIDTH + WALL;
	const float outerZ = HALF_DEPTH + WALL;

	auto scenery = [&](const std::string& model, const std::string& texture, const glm::vec3& position, float yaw = 0.f) {
		return Spawn(model, texture, position, yaw);
	};

	// The building
	scenery(ShopModels::GARAGE_SIDING, ShopTextures::SIDING, {});
	scenery(ShopModels::GARAGE_ROOF, ShopTextures::SHINGLES, {});
	scenery(ShopModels::GARAGE_FRAMES, ShopTextures::PAINT, {});
	scenery(ShopModels::WINDOW_GLASS, "", {})->SetTint({ 0.8f, 0.9f, 0.95f, 0.18f })->SetCastsShadows(false);

	auto door = Spawn(ShopModels::ROLLUP_DOOR, ShopTextures::PAINT, { (DOOR_MIN_X + DOOR_MAX_X) * 0.5f, DOOR_HEIGHT, HALF_DEPTH + 0.06f });
	m_doorStation = AddStation(StationType::DOOR, "Roll-up door", door, false);

	// The lot and the street
	scenery(ShopModels::GROUND, ShopTextures::GRASS, {});
	scenery(ShopModels::PAVING, ShopTextures::CONCRETE, {});
	scenery(ShopModels::ROAD, ShopTextures::ASPHALT, {});
	scenery(ShopModels::ROAD_MARKINGS, "", {})->SetCastsShadows(false);
	scenery(ShopModels::FENCE, ShopTextures::WOOD, {});

	struct Tree { glm::vec2 position; float scale; bool pine; };
	const Tree trees[] = {
		{ { -14.f, -9.f }, 1.15f, false }, { { -9.5f, -11.5f }, 0.95f, true }, { { 4.f, -11.f }, 1.2f, false },
		{ { 13.5f, -8.5f }, 1.05f, true }, { { 16.5f, -2.f }, 0.9f, false }, { { -16.5f, 1.5f }, 1.1f, true },
		{ { -15.f, 10.5f }, 1.25f, false }, { { 12.f, 11.f }, 1.0f, false }, { { 17.f, 7.f }, 1.1f, true },
		// Across the street and beyond the fence
		{ { -30.f, 33.f }, 1.3f, false }, { { -12.f, 31.f }, 1.0f, true }, { { 7.f, 33.5f }, 1.2f, false },
		{ { 26.f, 31.5f }, 1.1f, false }, { { -27.f, -6.f }, 1.3f, true }, { { 28.f, 4.f }, 1.25f, false },
		{ { -34.f, 12.f }, 1.0f, false }, { { 33.f, -12.f }, 1.2f, true },
	};
	for (const auto& tree : trees)
	{
		glm::vec3 position = { tree.position.x, 0.f, tree.position.y };
		float yaw = tree.position.x * 1.7f + tree.position.y;
		auto trunk = scenery(ShopModels::TREE_TRUNK, ShopTextures::BARK, position, yaw);
		auto crown = scenery(tree.pine ? ShopModels::PINE_CROWN : ShopModels::TREE_CROWN, ShopTextures::FOLIAGE, position, yaw);
		trunk->SetScale(glm::vec3(tree.scale));
		crown->SetScale(glm::vec3(tree.scale));
		m_staticObstacles.push_back(Footprint(tree.position, glm::vec2(0.25f * tree.scale)));
	}

	// Houses across the street face it; two more sit beyond the side fences.
	struct House { glm::vec2 position; float yaw; glm::vec3 tint; };
	const House houses[] = {
		{ { -22.f, 37.f }, glm::pi<float>(), { 1.f, 1.f, 1.f } },
		{ { -3.f, 38.f }, glm::pi<float>(), { 0.9f, 0.95f, 1.05f } },
		{ { 17.f, 37.5f }, glm::pi<float>(), { 1.05f, 0.95f, 0.9f } },
		{ { -32.f, 2.f }, glm::half_pi<float>(), { 0.95f, 1.f, 1.f } },
		{ { 33.f, -3.f }, -glm::half_pi<float>(), { 1.f, 0.92f, 0.88f } },
	};
	for (const auto& house : houses)
	{
		glm::vec3 position = { house.position.x, 0.f, house.position.y };
		scenery(ShopModels::HOUSE_WALLS, ShopTextures::BRICK, position, house.yaw)->SetTint(glm::vec4(house.tint, 1.f));
		scenery(ShopModels::HOUSE_ROOF, ShopTextures::SHINGLES, position, house.yaw);
		scenery(ShopModels::HOUSE_TRIM, ShopTextures::PAINT, position, house.yaw);
	}

	// Street lights, and a lamp over the roll-up door.
	for (const auto& pole : STREET_LIGHTS)
	{
		scenery(ShopModels::STREET_LIGHT, ShopTextures::PAINT, pole);
		auto head = scenery(ShopModels::LAMP_HEAD, "", pole + glm::vec3(0.f, STREET_LIGHT_HEIGHT - 0.13f, 1.15f));
		head->SetCastsShadows(false);
		m_lampHeads.push_back(head);
		m_staticObstacles.push_back(Footprint({ pole.x, pole.z }, { 0.18f, 0.18f }));
	}
	auto porch = scenery(ShopModels::LAMP_HEAD, "", DOOR_LIGHT);
	porch->SetScale({ 1.4f, 1.f, 0.5f });
	porch->SetCastsShadows(false);
	m_lampHeads.push_back(porch);

	// A customer's car on the drive, the bins, a few drums by the side wall.
	scenery(ShopModels::CAR_BODY, ShopTextures::PAINT, { -1.5f, 0.f, 11.2f }, glm::half_pi<float>())->SetTint({ 0.62f, 0.12f, 0.1f, 1.f });
	scenery(ShopModels::CAR_TRIM, ShopTextures::RUBBER, { -1.5f, 0.f, 11.2f }, glm::half_pi<float>());
	m_staticObstacles.push_back(Footprint({ -1.5f, 11.2f }, { 0.95f, 2.2f }));
	scenery(ShopModels::CAR_BODY, ShopTextures::PAINT, { 9.f, 0.f, 24.f }, glm::pi<float>())->SetTint({ 0.2f, 0.32f, 0.55f, 1.f });
	scenery(ShopModels::CAR_TRIM, ShopTextures::RUBBER, { 9.f, 0.f, 24.f }, glm::pi<float>());
	m_staticObstacles.push_back(Footprint({ 9.f, 24.f }, { 2.2f, 0.95f }));

	scenery(ShopModels::DUMPSTER, ShopTextures::PAINT, { outerX + 1.6f, 0.f, -2.5f }, glm::half_pi<float>());
	m_staticObstacles.push_back(Footprint({ outerX + 1.6f, -2.5f }, { 0.65f, 1.0f }));
	const glm::vec3 drumColors[] = { { 0.2f, 0.35f, 0.6f }, { 0.7f, 0.2f, 0.15f }, { 0.2f, 0.35f, 0.6f } };
	for (int i = 0; i < 3; i++)
	{
		glm::vec3 position = { -outerX - 0.7f, 0.f, -3.2f + i * 0.65f };
		scenery(ShopModels::OIL_DRUM, ShopTextures::PAINT, position, i * 0.8f)->SetTint(glm::vec4(drumColors[i], 1.f));
		m_staticObstacles.push_back(Footprint({ position.x, position.z }, { 0.3f, 0.3f }));
	}

	// Walls: the front is split around the door, which gets its own box while it's down.
	m_walls = {
		{ { -outerX, 0.f, -outerZ }, { outerX, HEIGHT, -HALF_DEPTH } },
		{ { -outerX, 0.f, HALF_DEPTH }, { DOOR_MIN_X, HEIGHT, outerZ } },
		{ { DOOR_MAX_X, 0.f, HALF_DEPTH }, { outerX, HEIGHT, outerZ } },
		{ { -outerX, 0.f, -outerZ }, { -HALF_WIDTH, HEIGHT, outerZ } },
		{ { HALF_WIDTH, 0.f, -outerZ }, { outerX, HEIGHT, outerZ } },
	};
	for (const auto& wall : m_walls)
		m_staticObstacles.push_back({ { wall.min.x, wall.min.z }, { wall.max.x, wall.max.z } });
}

std::vector<ShopBox> ShopWorld::SolidWalls() const
{
	auto walls = m_walls;
	if (m_doorAmount < 0.85f)
	{
		using namespace GarageLayout;
		float bottom = DOOR_HEIGHT * m_doorAmount;
		walls.push_back({ { DOOR_MIN_X, bottom, HALF_DEPTH }, { DOOR_MAX_X, DOOR_HEIGHT, HALF_DEPTH + WALL } });
	}
	return walls;
}

std::optional<float> ShopWorld::WallHit(const glm::vec3& origin, const glm::vec3& direction) const
{
	std::optional<float> nearest;
	for (const auto& wall : SolidWalls())
		if (auto t = RayBox(origin, direction, wall.min, wall.max); t && (!nearest || *t < *nearest))
			nearest = t;
	return nearest;
}

bool ShopWorld::InsideWalls(const ShopBox& box) const
{
	for (const auto& wall : SolidWalls())
		if (box.min.x < wall.max.x && wall.min.x < box.max.x && box.min.y < wall.max.y && wall.min.y < box.max.y
			&& box.min.z < wall.max.z && wall.min.z < box.max.z)
			return true;
	return false;
}

void ShopWorld::UpdateDoor(float deltaTime)
{
	float target = m_doorOpen ? 1.f : 0.f;
	float step = deltaTime / GarageLayout::DOOR_SECONDS;
	m_doorAmount = m_doorAmount < target ? (std::min)(target, m_doorAmount + step) : (std::max)(target, m_doorAmount - step);

	// Rolled up, only the bottom bar shows under the housing.
	auto& door = m_stations[m_doorStation];
	door.object->SetScale({ 1.f, 1.f - 0.97f * m_doorAmount, 1.f });
}

bool ShopWorld::IsDoorOpen() const
{
	return m_doorOpen;
}

void ShopWorld::SetDoorOpen(bool open, bool instant)
{
	m_doorOpen = open;
	if (instant)
		m_doorAmount = open ? 1.f : 0.f;
}

float ShopWorld::GetDoorOpenAmount() const
{
	return m_doorAmount;
}

const std::vector<pObject>& ShopWorld::GetLampHeads() const
{
	return m_lampHeads;
}
