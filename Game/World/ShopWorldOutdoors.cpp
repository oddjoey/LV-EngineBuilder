#include "ShopWorld.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

#include "LV/Engine/Objects/ObjectManager.h"
#include "GarageLayout.h"
#include "HouseLayout.h"
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

	auto scenery = [&](const std::string& model, const std::string& texture, const glm::vec3& position, float yaw = 0.f) {
		return Spawn(model, texture, position, yaw);
	};

	// The building
	m_garageSiding = scenery(ShopModels::GARAGE_SIDING, ShopTextures::SIDING, {});
	m_garageRoof = scenery(ShopModels::GARAGE_ROOF, ShopTextures::SHINGLES, {});
	m_garageFrames = scenery(ShopModels::GARAGE_FRAMES, ShopTextures::PAINT, {});
	m_garageGlass = scenery(ShopModels::WINDOW_GLASS, "", {});
	m_garageGlass->SetTint({ 0.8f, 0.9f, 0.95f, 0.18f })->SetCastsShadows(false);

	auto door = Spawn(ShopModels::ROLLUP_DOOR, ShopTextures::PAINT, { (DOOR_MIN_X + DOOR_MAX_X) * 0.5f, DOOR_HEIGHT, HALF_DEPTH + 0.06f });
	// The lamps only light the inside, and the door's shadow from them would fall outside: leaving it out
	// of their maps saves redrawing every one of them each frame it rolls up or down. (The sun still
	// sees it, so it keeps the daylight out.)
	door->SetCastsLampShadows(false);
	m_doorStation = AddStation(StationType::DOOR, "Roll-up door", door, false);

	// The house: shell, brick, roof, trim, glass and floor, a light in every room, and the furniture.
	scenery(ShopModels::HOME_SHELL, ShopTextures::PLASTER, {});
	scenery(ShopModels::HOME_BRICK, ShopTextures::BRICK, {});
	scenery(ShopModels::HOME_ROOF, ShopTextures::SHINGLES, {});
	scenery(ShopModels::HOME_FRAMES, ShopTextures::PAINT, {});
	scenery(ShopModels::HOME_FLOOR, ShopTextures::WOOD, {});
	scenery(ShopModels::HOME_GLASS, "", {})->SetTint({ 0.8f, 0.9f, 0.95f, 0.18f })->SetCastsShadows(false);
	for (const auto& position : HOUSE_LAMPS)
	{
		auto light = scenery(ShopModels::HOME_LIGHT, "", position);
		light->SetEmissive(1.4f);
		light->SetCastsShadows(false);
	}
	const std::string materialTextures[] = { ShopTextures::WOOD, ShopTextures::PAINT, "", ShopTextures::FOLIAGE };
	for (int material = 0; material < static_cast<int>(HouseLayout::Material::COUNT); material++)
	{
		scenery(ShopModels::HomeFurniture(material), materialTextures[material], {});
		scenery(ShopModels::YardPieces(material), materialTextures[material], {});
	}
	for (const auto* pieces : { &HouseLayout::Furniture(), &HouseLayout::Yard() })
		for (const auto& piece : *pieces)
			if (piece.blocks)
				m_sceneryObstacles.push_back({ { piece.min.x, piece.min.z }, { piece.max.x, piece.max.z }, piece.min.y, piece.max.y });

	// The lot and the street
	scenery(ShopModels::GROUND, ShopTextures::GRASS, {});
	scenery(ShopModels::PAVING, ShopTextures::CONCRETE, {});
	scenery(ShopModels::ROAD, ShopTextures::ASPHALT, {});
	scenery(ShopModels::ROAD_MARKINGS, "", {})->SetCastsShadows(false);
	scenery(ShopModels::FENCE, ShopTextures::WOOD, {});
	for (const auto& fence : FENCES)
		m_sceneryObstacles.push_back({ glm::min(fence.from, fence.to) - glm::vec2(0.08f), glm::max(fence.from, fence.to) + glm::vec2(0.08f) });

	struct Tree { glm::vec2 position; float scale; bool pine; };
	const Tree trees[] = {
		// The backyard and the front yard
		{ { -14.f, -9.f }, 1.15f, false }, { { -9.5f, -11.5f }, 0.95f, true }, { { 4.f, -11.f }, 1.2f, false },
		{ { 13.5f, -12.5f }, 1.05f, true }, { { 26.f, -4.f }, 0.9f, false }, { { -17.f, 1.5f }, 1.1f, true },
		{ { -15.f, 10.5f }, 1.25f, false }, { { 14.f, 11.f }, 1.0f, false }, { { 24.f, 8.f }, 1.1f, true },
		{ { -25.f, -20.f }, 1.3f, false }, { { 6.f, -24.f }, 1.15f, true }, { { 27.f, -24.f }, 1.2f, false },
		{ { -27.f, -8.f }, 1.0f, true }, { { 18.f, -20.f }, 1.1f, false }, { { -8.f, -24.f }, 0.95f, false },
		{ { 27.f, 12.f }, 1.0f, true },
		// The neighbours'
		{ { -38.f, -10.f }, 1.2f, false }, { { 52.f, -12.f }, 1.1f, true }, { { -55.f, 9.f }, 1.0f, false },
		{ { 38.f, 10.f }, 1.15f, false }, { { 60.f, -6.f }, 1.25f, true }, { { -62.f, -15.f }, 1.1f, false },
		// The park, and the lots across the street
		{ { -15.f, 30.f }, 1.2f, false }, { { -6.f, 38.f }, 1.3f, false }, { { -16.f, 45.f }, 1.1f, true },
		{ { 6.f, 31.f }, 1.0f, false }, { { 16.5f, 32.f }, 1.15f, true }, { { 17.f, 44.f }, 1.25f, false },
		{ { -42.f, 44.f }, 1.1f, false }, { { -62.f, 42.f }, 1.2f, true }, { { 42.f, 44.f }, 1.0f, false },
		{ { 62.f, 42.f }, 1.15f, false }, { { -24.f, 43.f }, 1.05f, true }, { { 24.f, 43.f }, 1.2f, false },
		// Out past where you can walk
		{ { -80.f, 10.f }, 1.4f, false }, { { 80.f, -5.f }, 1.3f, true }, { { -75.f, 55.f }, 1.2f, false },
		{ { 78.f, 60.f }, 1.35f, false }, { { 0.f, 60.f }, 1.3f, true }, { { -40.f, 58.f }, 1.2f, false },
		{ { 40.f, 58.f }, 1.25f, true }, { { -30.f, -38.f }, 1.3f, false }, { { 30.f, -40.f }, 1.2f, true },
		{ { 0.f, -36.f }, 1.35f, false }, { { -60.f, -40.f }, 1.2f, false }, { { 60.f, -38.f }, 1.3f, true },
	};
	for (const auto& tree : trees)
	{
		glm::vec3 position = { tree.position.x, 0.f, tree.position.y };
		float yaw = tree.position.x * 1.7f + tree.position.y;
		auto trunk = scenery(ShopModels::TREE_TRUNK, ShopTextures::BARK, position, yaw);
		auto crown = scenery(tree.pine ? ShopModels::PINE_CROWN : ShopModels::TREE_CROWN, ShopTextures::FOLIAGE, position, yaw);
		trunk->SetScale(glm::vec3(tree.scale));
		crown->SetScale(glm::vec3(tree.scale));
		m_sceneryObstacles.push_back(Footprint(tree.position, glm::vec2(0.25f * tree.scale)));
	}

	// The neighbours either side face the street, as do the houses across it.
	struct House { glm::vec2 position; float yaw; glm::vec3 tint; };
	const House houses[] = {
		{ { -45.f, 3.f }, 0.f, { 0.95f, 1.f, 1.f } },
		{ { 45.f, 3.f }, 0.f, { 1.f, 0.92f, 0.88f } },
		{ { -62.f, 3.f }, 0.f, { 1.05f, 1.f, 0.92f } },
		{ { 62.f, 3.f }, 0.f, { 0.9f, 0.92f, 1.f } },
		{ { -52.f, 38.f }, glm::pi<float>(), { 1.f, 1.f, 1.f } },
		{ { -32.f, 38.f }, glm::pi<float>(), { 0.9f, 0.95f, 1.05f } },
		{ { 32.f, 38.f }, glm::pi<float>(), { 1.05f, 0.95f, 0.9f } },
		{ { 52.f, 38.f }, glm::pi<float>(), { 0.95f, 1.05f, 0.95f } },
	};
	for (const auto& house : houses)
	{
		glm::vec3 position = { house.position.x, 0.f, house.position.y };
		scenery(ShopModels::HOUSE_WALLS, ShopTextures::BRICK, position, house.yaw)->SetTint(glm::vec4(house.tint, 1.f));
		scenery(ShopModels::HOUSE_ROOF, ShopTextures::SHINGLES, position, house.yaw);
		scenery(ShopModels::HOUSE_TRIM, ShopTextures::PAINT, position, house.yaw);
		m_sceneryObstacles.push_back(Footprint(house.position, { 5.f, 3.5f }));
	}

	// Street lights, a lamp over the roll-up door and one on the porch.
	for (const auto& pole : STREET_LIGHTS)
	{
		scenery(ShopModels::STREET_LIGHT, ShopTextures::PAINT, pole);
		auto head = scenery(ShopModels::LAMP_HEAD, "", pole + glm::vec3(0.f, STREET_LIGHT_HEIGHT - 0.13f, 1.15f));
		head->SetCastsShadows(false);
		m_lampHeads.push_back(head);
		m_sceneryObstacles.push_back(Footprint({ pole.x, pole.z }, { 0.18f, 0.18f }));
	}
	for (const auto& position : { DOOR_LIGHT, PORCH_LIGHT })
	{
		auto lamp = scenery(ShopModels::LAMP_HEAD, "", position);
		lamp->SetScale({ 1.4f, 1.f, 0.5f });
		lamp->SetCastsShadows(false);
		m_lampHeads.push_back(lamp);
	}

	// A customer's car on the drive, cars parked along the street, the bins out back, a few drums
	// by the side wall.
	struct Car { glm::vec2 position; float yaw; glm::vec3 color; };
	const Car cars[] = {
		{ { -1.5f, 11.2f }, glm::half_pi<float>(), { 0.62f, 0.12f, 0.1f } },
		{ { 9.f, 24.f }, glm::pi<float>(), { 0.2f, 0.32f, 0.55f } },
		{ { -35.f, 20.f }, 0.f, { 0.85f, 0.85f, 0.82f } },
		{ { 40.f, 24.f }, glm::pi<float>(), { 0.25f, 0.4f, 0.28f } },
	};
	for (const auto& car : cars)
	{
		glm::vec3 position = { car.position.x, 0.f, car.position.y };
		scenery(ShopModels::CAR_BODY, ShopTextures::PAINT, position, car.yaw)->SetTint(glm::vec4(car.color, 1.f));
		scenery(ShopModels::CAR_TRIM, ShopTextures::RUBBER, position, car.yaw);
		bool alongZ = std::abs(std::sin(car.yaw)) > 0.5f;
		m_sceneryObstacles.push_back(Footprint(car.position, alongZ ? glm::vec2(0.95f, 2.2f) : glm::vec2(2.2f, 0.95f)));
	}

	scenery(ShopModels::DUMPSTER, ShopTextures::PAINT, { -4.5f, 0.f, -7.3f });
	m_sceneryObstacles.push_back(Footprint({ -4.5f, -7.3f }, { 1.0f, 0.65f }));
	const glm::vec3 drumColors[] = { { 0.2f, 0.35f, 0.6f }, { 0.7f, 0.2f, 0.15f }, { 0.2f, 0.35f, 0.6f } };
	for (int i = 0; i < 3; i++)
	{
		glm::vec3 position = { -outerX - 0.7f, 0.f, -3.2f + i * 0.65f };
		auto drum = scenery(ShopModels::OIL_DRUM, ShopTextures::PAINT, position, i * 0.8f);
		drum->SetTint(glm::vec4(drumColors[i], 1.f));
		m_sideDrums.push_back(drum);
		m_drumObstacles.push_back(Footprint({ position.x, position.z }, { 0.3f, 0.3f }));
	}

	// The walls and what the player bumps into, as the shop stands without upgrades (see BuildUpgrades).
	BuildUpgrades(false, false);
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
	if (m_bayBuilt && m_bayDoorAmount < 0.85f)
	{
		using namespace GarageLayout;
		float bottom = BAY_DOOR_HEIGHT * m_bayDoorAmount;
		walls.push_back({ { BAY_DOOR_MIN_X, bottom, BAY_HALF_DEPTH }, { BAY_DOOR_MAX_X, BAY_DOOR_HEIGHT, BAY_HALF_DEPTH + WALL } });
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

Sound ShopWorld::FootstepAt(const glm::vec3& feet) const
{
	using namespace GarageLayout;
	ShopInterior interior = GetInterior();
	glm::vec3 probe = feet + glm::vec3(0.f, 0.1f, 0.f);
	// Floorboards through the house, tiles in the bathroom.
	if (interior.InHouse(probe))
	{
		bool bathroom = feet.x > KITCHEN_X && feet.x < BATH_X && feet.z < HALL_Z;
		return bathroom ? Sound::FOOTSTEP_CONCRETE : Sound::FOOTSTEP_WOOD;
	}
	if (interior.InGarage(probe))
		return feet.y > 0.2f ? Sound::FOOTSTEP_WOOD : Sound::FOOTSTEP_CONCRETE;	// the stairs and the loft are wood
	// Up the outside stairs and along the landing.
	if (feet.y > 0.3f)
		return Sound::FOOTSTEP_WOOD;
	// The drive, sidewalks, paths, patio and the street itself.
	for (const auto& paved : PAVED)
		if (feet.x > paved.min.x && feet.x < paved.max.x && feet.z > paved.min.y && feet.z < paved.max.y)
			return Sound::FOOTSTEP_CONCRETE;
	if (m_bayBuilt && feet.x > BAY_DRIVE.min.x && feet.x < BAY_DRIVE.max.x && feet.z > BAY_DRIVE.min.y && feet.z < BAY_DRIVE.max.y)
		return Sound::FOOTSTEP_CONCRETE;
	bool street = feet.z >= STREET_NEAR_Z && feet.z <= STREET_FAR_Z;
	return street ? Sound::FOOTSTEP_CONCRETE : Sound::FOOTSTEP_GRASS;
}

void ShopWorld::UpdateDoor(float deltaTime)
{
	using namespace GarageLayout;
	float step = deltaTime / DOOR_SECONDS;
	auto roll = [&](bool open, float& amount) {
		float target = open ? 1.f : 0.f;
		bool moving = amount != target;
		amount = amount < target ? (std::min)(target, amount + step) : (std::max)(target, amount - step);
		return moving;
	};
	m_doorMoving = roll(m_doorOpen, m_doorAmount);

	// Rolled up, only the bottom bar shows under the housing.
	auto& door = m_stations[m_doorStation];
	door.object->SetScale({ 1.f, 1.f - 0.97f * m_doorAmount, 1.f });

	// The extension's door is the same door, a little shorter, and only there with the extension.
	if (m_bayDoorStation < m_stations.size())
	{
		auto& bayDoor = m_stations[m_bayDoorStation];
		if (bayDoor.present != m_bayBuilt)
		{
			bayDoor.present = m_bayBuilt;
			bayDoor.object->SetVisible(m_bayBuilt);
			bayDoor.object->SetPickable(m_bayBuilt);
		}
		m_doorMoving = (m_bayBuilt && roll(m_bayDoorOpen, m_bayDoorAmount)) || m_doorMoving;
		bayDoor.object->SetScale({ 1.f, BAY_DOOR_HEIGHT / DOOR_HEIGHT * (1.f - 0.97f * m_bayDoorAmount), 1.f });
	}
}

bool ShopWorld::IsBayDoor(const Station& station) const
{
	return m_bayDoorStation < m_stations.size() && &station == &m_stations[m_bayDoorStation];
}

bool& ShopWorld::DoorOpenFor(const Station& station)
{
	return IsBayDoor(station) ? m_bayDoorOpen : m_doorOpen;
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
