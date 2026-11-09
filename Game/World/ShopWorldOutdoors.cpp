#include "ShopWorld.h"

#include <cmath>

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

	auto scenery = [&](const std::string& model, const std::string& texture, const glm::vec3& position, float yaw = 0.f) {
		return Spawn(model, texture, position, yaw);
	};

	// The building
	m_garageSiding = scenery(ShopModels::GARAGE_SIDING, ShopTextures::PLANKS, {});
	m_garageSiding->SetTint(WEATHERED_BOARDS);
	m_garageRoof = scenery(ShopModels::GARAGE_ROOF, ShopTextures::SIDING, {});
	m_garageFrames = scenery(ShopModels::GARAGE_FRAMES, ShopTextures::WOOD, {});
	m_garageGlass = scenery(ShopModels::WINDOW_GLASS, "", {});
	m_garageGlass->SetTint({ 0.8f, 0.9f, 0.95f, 0.18f })->SetCastsShadows(false);

	auto door = Spawn(ShopModels::ROLLUP_DOOR, ShopTextures::PAINT, { (DOOR_MIN_X + DOOR_MAX_X) * 0.5f, DOOR_HEIGHT, HALF_DEPTH + 0.06f });
	// The lamps only light the inside, and the door's shadow from them would fall outside: leaving it out
	// of their maps saves redrawing every one of them each frame it rolls up or down. (The sun still
	// sees it, so it keeps the daylight out.)
	door->SetCastsLampShadows(false);
	m_doorStation = AddStation(StationType::DOOR, "Roll-up door", door, false);

	// The desert, the drive and the highway.
	scenery(ShopModels::GROUND, ShopTextures::SAND, {});
	scenery(ShopModels::PAVING, ShopTextures::CONCRETE, {});
	scenery(ShopModels::ROAD, ShopTextures::ASPHALT, {});
	scenery(ShopModels::ROAD_MARKINGS, "", {})->SetCastsShadows(false);

	// Saguaros, boulders and scrub scattered over the sand, the same every game: thicker further out, and
	// kept off the drive and the highway and out of the way of the shack (and its side bay).
	auto hash = [](int i, int seed) {
		uint32_t h = static_cast<uint32_t>(i) * 2654435761u + static_cast<uint32_t>(seed) * 2246822519u;
		h = (h ^ (h >> 15)) * 2246822519u;
		return static_cast<float>((h ^ (h >> 13)) & 0xFFFF) / 65535.f;
	};
	for (int i = 0; i < 320; i++)
	{
		float angle = hash(i, 1) * glm::two_pi<float>();
		float distance = 14.f + 150.f * std::sqrt(hash(i, 2));
		glm::vec2 at = { std::sin(angle) * distance, std::cos(angle) * distance };
		bool onHighway = at.y > HIGHWAY_NEAR_Z - 5.f && at.y < HIGHWAY_FAR_Z + 5.f;
		bool onDrive = at.x > BAY_DOOR_MIN_X - 4.f && at.x < DRIVE_MAX_X + 4.f && at.y > 0.f && at.y < HIGHWAY_NEAR_Z;
		bool byShack = at.x > BAY_MIN_X - 6.f && at.x < HALF_WIDTH + 8.f && at.y > -HALF_DEPTH - 8.f && at.y < HALF_DEPTH + 8.f;
		if (onHighway || onDrive || byShack)
			continue;
		glm::vec3 position = { at.x, 0.f, at.y };
		float scale = 0.7f + 0.6f * hash(i, 4);
		float yaw = hash(i, 5) * glm::two_pi<float>();
		float pick = hash(i, 3);
		pObject piece;
		if (pick < 0.3f)
		{
			piece = scenery(ShopModels::CACTUS, "", position, yaw);
			m_sceneryObstacles.push_back(Footprint(at, glm::vec2(0.25f * scale)));
		}
		else if (pick < 0.55f)
		{
			piece = scenery(ShopModels::BOULDER, ShopTextures::ROCK, position, yaw);
			m_sceneryObstacles.push_back(Footprint(at, glm::vec2(1.f * scale)));
		}
		else
			piece = scenery(ShopModels::SCRUB, ShopTextures::FOLIAGE, position, yaw);
		piece->SetScale(glm::vec3(scale));
	}

	// Lights along the highway, and a lamp over the roll-up door.
	for (const auto& pole : STREET_LIGHTS)
	{
		scenery(ShopModels::STREET_LIGHT, ShopTextures::PAINT, pole);
		auto head = scenery(ShopModels::LAMP_HEAD, "", pole + glm::vec3(0.f, STREET_LIGHT_HEIGHT - 0.13f, STREET_LIGHT_REACH));
		head->SetCastsShadows(false);
		m_lampHeads.push_back(head);
		m_sceneryObstacles.push_back(Footprint({ pole.x, pole.z }, { 0.18f, 0.18f }));
	}
	{
		auto lamp = scenery(ShopModels::LAMP_HEAD, "", DOOR_LIGHT);
		lamp->SetScale({ 1.4f, 1.f, 0.5f });
		lamp->SetCastsShadows(false);
		m_lampHeads.push_back(lamp);
	}

	// The bins out back, and a few drums by the side wall.
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
	if (interior.InGarage(probe))
		return feet.y > 0.2f ? Sound::FOOTSTEP_WOOD : Sound::FOOTSTEP_CONCRETE;	// the stairs and the loft are wood
	// Up the outside stairs and along the landing.
	if (feet.y > 0.3f)
		return Sound::FOOTSTEP_WOOD;
	// The drive, the apron and the highway sound hard; the sand soft.
	auto on = [&](const Paved& paved) { return feet.x > paved.min.x && feet.x < paved.max.x && feet.z > paved.min.y && feet.z < paved.max.y; };
	for (const auto& paved : PAVED)
		if (on(paved))
			return Sound::FOOTSTEP_CONCRETE;
	if (m_bayBuilt && (on(BAY_DRIVE) || on(BAY_DRIVE_LINK)))
		return Sound::FOOTSTEP_CONCRETE;
	bool highway = (feet.z >= HIGHWAY_NEAR_Z && feet.z <= HIGHWAY_NEAR_Z + HIGHWAY_CARRIAGEWAY)
		|| (feet.z >= HIGHWAY_FAR_Z - HIGHWAY_CARRIAGEWAY && feet.z <= HIGHWAY_FAR_Z);
	return highway ? Sound::FOOTSTEP_CONCRETE : Sound::FOOTSTEP_GRASS;
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
