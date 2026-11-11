#include "ShopWorld.h"

#include <algorithm>

#include "LV/Engine/Objects/ObjectManager.h"
#include "GarageLayout.h"
#include "ShopWorldShared.h"

using namespace ShopLayout;

void ShopWorld::SyncUpgrades(const Workshop& workshop)
{
	bool bay = workshop.OwnsUpgrade(UpgradeKind::SIDE_BAY);
	bool upper = workshop.OwnsUpgrade(UpgradeKind::SECOND_STORY);
	if (bay != m_bayBuilt || upper != m_upperBuilt || !m_upgradesApplied)
		BuildUpgrades(bay, upper);
}

void ShopWorld::BuildUpgrades(bool bay, bool upper)
{
	using namespace GarageLayout;
	const float outerX = HALF_WIDTH + WALL;
	const float outerZ = HALF_DEPTH + WALL;

	// Start again from the plain garage: take away whatever the last upgrades added.
	auto objectManager = ObjectManager::Get();
	for (auto& object : m_upgradeObjects)
		objectManager->RemoveObject(object);
	m_upgradeObjects.clear();
	m_bayBuilt = bay;
	m_upperBuilt = upper;
	m_upgradesApplied = true;

	// The garage's own walls, siding, frames and glass in the version that goes with them. Objects are
	// drawn grouped by the model they were made with, so a new version means a new object.
	auto variant = [&](pObject& object, const std::string& base, const std::string& texture) {
		if (!object)
			return;
		objectManager->RemoveObject(object);
		object = Spawn(ShopModels::GarageVariant(base, bay, upper), texture, {});
	};
	variant(m_garageWalls, ShopModels::WALLS, ShopTextures::PLANKS);
	variant(m_garageSiding, ShopModels::GARAGE_SIDING, ShopTextures::PLANKS);
	if (m_garageSiding)
		m_garageSiding->SetTint(WEATHERED_BOARDS);
	variant(m_garageFrames, ShopModels::GARAGE_FRAMES, ShopTextures::WOOD);
	variant(m_garageGlass, ShopModels::WINDOW_GLASS, "");
	if (m_garageGlass)
		m_garageGlass->SetTint({ 0.8f, 0.9f, 0.95f, 0.18f })->SetCastsShadows(false);
	if (m_garageRoof)
		m_garageRoof->SetVisible(!upper);
	for (auto& drum : m_sideDrums)
		drum->SetVisible(!bay);

	auto lamp = [&](const glm::vec3& position) {
		auto object = Spawn(ShopModels::LAMP, ShopTextures::PAINT, position);
		object->SetEmissive(1.4f);
		object->SetCastsShadows(false);
		m_upgradeObjects.push_back(object);
		m_shopLamps.push_back(position);
	};
	m_shopLamps.assign(std::begin(LAMPS), std::end(LAMPS));

	// Walls: the front is split around the roll-up door, which gets its own box while it's down. The left
	// wall is split round the opening into the side bay, if there is one.
	m_walls = {
		{ { -outerX, 0.f, -outerZ }, { outerX, HEIGHT, -HALF_DEPTH } },
		{ { -outerX, 0.f, HALF_DEPTH }, { DOOR_MIN_X, HEIGHT, outerZ } },
		{ { DOOR_MAX_X, 0.f, HALF_DEPTH }, { outerX, HEIGHT, outerZ } },
		{ { HALF_WIDTH, 0.f, -outerZ }, { outerX, HEIGHT, outerZ } },
	};
	if (bay)
	{
		m_walls.push_back({ { -outerX, 0.f, -outerZ }, { -HALF_WIDTH, HEIGHT, -BAY_OPENING_HALF } });
		m_walls.push_back({ { -outerX, 0.f, BAY_OPENING_HALF }, { -HALF_WIDTH, HEIGHT, outerZ } });
		m_walls.push_back({ { -outerX, BAY_OPENING_HEIGHT, -BAY_OPENING_HALF }, { -HALF_WIDTH, HEIGHT, BAY_OPENING_HALF } });
	}
	else
		m_walls.push_back({ { -outerX, 0.f, -outerZ }, { -HALF_WIDTH, HEIGHT, outerZ } });

	m_walkSurfaces.clear();
	m_upgradeSurfaces.clear();
	std::vector<FloorObstacle> extraObstacles;

	// Until the bay is built, sand where its floor and drive will go (the lot's sand leaves them out).
	if (!bay)
		m_upgradeObjects.push_back(Spawn(ShopModels::BAY_SAND, ShopTextures::SAND, {}));
	if (bay)
	{
		m_upgradeObjects.push_back(Spawn(ShopModels::BAY_FLOOR, ShopTextures::CONCRETE, {}));
		m_upgradeObjects.push_back(Spawn(ShopModels::BAY_SHELL, ShopTextures::PLANKS, {}));
		m_upgradeObjects.push_back(Spawn(ShopModels::BAY_EXTERIOR, ShopTextures::PLANKS, {}));
		m_upgradeObjects.back()->SetTint(WEATHERED_BOARDS);
		m_upgradeObjects.push_back(Spawn(ShopModels::BAY_DRIVE, ShopTextures::CONCRETE, {}));
		for (const auto& position : BAY_LAMPS)
			lamp(position);
		// Its front wall is split round its own roll-up door, which gets a box of its own while it's down.
		const float right = -outerX;
		m_walls.push_back({ { BAY_MIN_X - WALL, 0.f, -BAY_HALF_DEPTH - WALL }, { right, BAY_HEIGHT, -BAY_HALF_DEPTH } });
		m_walls.push_back({ { BAY_MIN_X - WALL, 0.f, BAY_HALF_DEPTH }, { BAY_DOOR_MIN_X, BAY_HEIGHT, BAY_HALF_DEPTH + WALL } });
		m_walls.push_back({ { BAY_DOOR_MAX_X, 0.f, BAY_HALF_DEPTH }, { right, BAY_HEIGHT, BAY_HALF_DEPTH + WALL } });
		m_walls.push_back({ { BAY_DOOR_MIN_X, BAY_DOOR_HEIGHT, BAY_HALF_DEPTH }, { BAY_DOOR_MAX_X, BAY_HEIGHT, BAY_HALF_DEPTH + WALL } });
		m_walls.push_back({ { BAY_MIN_X - WALL, 0.f, -BAY_HALF_DEPTH - WALL }, { BAY_MIN_X, BAY_HEIGHT, BAY_HALF_DEPTH + WALL } });
	}

	if (upper)
	{
		m_upgradeObjects.push_back(Spawn(ShopModels::UPPER_SHELL, ShopTextures::PLANKS, {}));
		m_upgradeObjects.push_back(Spawn(ShopModels::UPPER_EXTERIOR, ShopTextures::PLANKS, {}));
		m_upgradeObjects.back()->SetTint(WEATHERED_BOARDS);
		m_upgradeObjects.push_back(Spawn(ShopModels::UPPER_ROOF, ShopTextures::SIDING, {}));
		m_upgradeObjects.push_back(Spawn(ShopModels::UPPER_FRAMES, ShopTextures::WOOD, {}));
		auto glass = Spawn(ShopModels::UPPER_GLASS, "", {});
		glass->SetTint({ 0.8f, 0.9f, 0.95f, 0.18f })->SetCastsShadows(false);
		m_upgradeObjects.push_back(glass);
		m_upgradeObjects.push_back(Spawn(ShopModels::OUTSIDE_STAIRS, ShopTextures::PAINT, {}));
		m_upgradeObjects.push_back(Spawn(ShopModels::INSIDE_STAIRS, ShopTextures::WOOD, {}));
		for (const auto& position : UPPER_LAMPS)
			lamp(position);

		// The upstairs walls (the back one split round the door off the landing), and the floor
		// between the two stories, so nothing set down upstairs falls through.
		const float bottom = HEIGHT + WALL;
		const float top = UPPER_FLOOR_Y + UPPER_HEIGHT;
		m_walls.push_back({ { -outerX, bottom, -outerZ }, { UPPER_DOOR_MIN_X, top, -HALF_DEPTH } });
		m_walls.push_back({ { UPPER_DOOR_MAX_X, bottom, -outerZ }, { outerX, top, -HALF_DEPTH } });
		m_walls.push_back({ { -outerX, bottom, HALF_DEPTH }, { outerX, top, outerZ } });
		m_walls.push_back({ { -outerX, bottom, -outerZ }, { -HALF_WIDTH, top, outerZ } });
		m_walls.push_back({ { HALF_WIDTH, bottom, -outerZ }, { outerX, top, outerZ } });
		// The floor between them, round the stairwell: nothing is seen, reached or labelled through it.
		m_walls.push_back({ { -outerX, HEIGHT, -outerZ }, { outerX, UPPER_FLOOR_Y, INSIDE_STAIR_NEAR_Z } });
		m_walls.push_back({ { STAIRWELL_MAX_X, HEIGHT, INSIDE_STAIR_NEAR_Z }, { outerX, UPPER_FLOOR_Y, outerZ } });
		m_walls.push_back({ { -outerX, HEIGHT, HALF_DEPTH }, { STAIRWELL_MAX_X, UPPER_FLOOR_Y, outerZ } });
		m_upgradeSurfaces.push_back({ { -HALF_WIDTH, UPPER_FLOOR_Y - 0.05f, -HALF_DEPTH }, { HALF_WIDTH, UPPER_FLOOR_Y, INSIDE_STAIR_NEAR_Z } });
		m_upgradeSurfaces.push_back({ { STAIRWELL_MAX_X, UPPER_FLOOR_Y - 0.05f, INSIDE_STAIR_NEAR_Z }, { HALF_WIDTH, UPPER_FLOOR_Y, HALF_DEPTH } });

		// Somewhere to stand: each tread, the landing (on through the door), and the floor upstairs.
		float run = (STAIR_BOTTOM_X - STAIR_TOP_X) / STAIR_STEPS;
		float rise = UPPER_FLOOR_Y / STAIR_STEPS;
		for (int step = 0; step < STAIR_STEPS; step++)
		{
			float x1 = STAIR_BOTTOM_X - step * run;
			m_walkSurfaces.push_back({ { x1 - run, STAIR_FAR_Z }, { x1, STAIR_NEAR_Z }, (step + 1) * rise });
		}
		m_walkSurfaces.push_back({ { LANDING_END_X, STAIR_FAR_Z }, { STAIR_TOP_X, -HALF_DEPTH }, UPPER_FLOOR_Y });
		m_walkSurfaces.push_back({ { -HALF_WIDTH, -HALF_DEPTH }, { HALF_WIDTH, INSIDE_STAIR_NEAR_Z }, UPPER_FLOOR_Y });
		m_walkSurfaces.push_back({ { STAIRWELL_MAX_X, INSIDE_STAIR_NEAR_Z }, { HALF_WIDTH, HALF_DEPTH }, UPPER_FLOOR_Y });

		// No walking through the stairs from underneath, or off the open side of the stairs and landing.
		extraObstacles.push_back({ { LANDING_END_X, STAIR_FAR_Z }, { STAIR_BOTTOM_X - 3.f * run, STAIR_NEAR_Z }, 0.f, 0.5f });
		extraObstacles.push_back({ { LANDING_END_X, STAIR_FAR_Z - 0.05f }, { STAIR_BOTTOM_X, STAIR_FAR_Z + 0.05f }, 0.5f, top });
		extraObstacles.push_back({ { LANDING_END_X - 0.05f, STAIR_FAR_Z }, { LANDING_END_X + 0.05f, STAIR_NEAR_Z }, bottom, top });

		// The stairs inside: each tread to stand on; under each one past the first couple, solid (no
		// walking under them, but the bottom steps are walked straight onto); a handrail up the open
		// side; and the railing round the stairwell upstairs, open where the stairs come up.
		float insideRun = (INSIDE_STAIR_BOTTOM_X - INSIDE_STAIR_TOP_X) / INSIDE_STAIR_STEPS;
		float insideRise = UPPER_FLOOR_Y / INSIDE_STAIR_STEPS;
		for (int step = 0; step < INSIDE_STAIR_STEPS; step++)
		{
			float x1 = INSIDE_STAIR_BOTTOM_X - step * insideRun, x0 = x1 - insideRun;
			float height = (step + 1) * insideRise;
			m_walkSurfaces.push_back({ { x0, INSIDE_STAIR_NEAR_Z }, { x1, HALF_DEPTH }, height });
			if (step < 2)
				continue;
			extraObstacles.push_back({ { x0, INSIDE_STAIR_NEAR_Z }, { x1, HALF_DEPTH }, 0.f, height - 0.25f });
			if (x0 > STAIRWELL_RAIL_MIN_X - 0.01f)
				extraObstacles.push_back({ { x0, INSIDE_STAIR_NEAR_Z - 0.05f }, { x1, INSIDE_STAIR_NEAR_Z }, height, height + 1.f });
		}
		extraObstacles.push_back({ { STAIRWELL_RAIL_MIN_X, INSIDE_STAIR_NEAR_Z - 0.03f }, { STAIRWELL_MAX_X, INSIDE_STAIR_NEAR_Z + 0.03f }, UPPER_FLOOR_Y, UPPER_FLOOR_Y + 1.f });
		extraObstacles.push_back({ { STAIRWELL_MAX_X - 0.03f, INSIDE_STAIR_NEAR_Z }, { STAIRWELL_MAX_X + 0.03f, HALF_DEPTH }, UPPER_FLOOR_Y, UPPER_FLOOR_Y + 1.f });
	}

	// What the player bumps into: the scenery, the drums by the left wall unless the bay replaced
	// them, every wall at its own height, and the stairs.
	m_staticObstacles = m_sceneryObstacles;
	if (!bay)
		m_staticObstacles.insert(m_staticObstacles.end(), m_drumObstacles.begin(), m_drumObstacles.end());
	for (const auto& wall : m_walls)
		m_staticObstacles.push_back({ { wall.min.x, wall.min.z }, { wall.max.x, wall.max.z }, wall.min.y, wall.max.y });
	m_staticObstacles.insert(m_staticObstacles.end(), extraObstacles.begin(), extraObstacles.end());
}

const std::vector<WalkSurface>& ShopWorld::GetWalkSurfaces() const
{
	return m_walkSurfaces;
}

ShopInterior ShopWorld::GetInterior() const
{
	using namespace GarageLayout;
	ShopInterior interior;
	interior.min = { m_bayBuilt ? BAY_MIN_X : -HALF_WIDTH, -0.5f, -HALF_DEPTH };
	interior.max = { HALF_WIDTH, m_upperBuilt ? UPPER_FLOOR_Y + UPPER_HEIGHT : HEIGHT, HALF_DEPTH };
	interior.lamps = m_shopLamps;
	return interior;
}

bool ShopWorld::OnShopFloor(const ShopBox& footprint) const
{
	using namespace GarageLayout;
	auto within = [&](glm::vec2 min, glm::vec2 max) {
		return footprint.min.x >= min.x && footprint.max.x <= max.x && footprint.min.z >= min.y && footprint.max.z <= max.y;
	};
	if (within(ROOM_BOUNDS.min, ROOM_BOUNDS.max))
		return true;
	if (!m_bayBuilt)
		return false;
	// In the bay, or straddling the opening between it and the shop.
	return within({ BAY_MIN_X, -BAY_HALF_DEPTH }, { -HALF_WIDTH - WALL, BAY_HALF_DEPTH })
		|| within({ BAY_MIN_X, -BAY_OPENING_HALF }, { HALF_WIDTH, BAY_OPENING_HALF });
}

bool ShopWorld::OnDrive(const ShopBox& footprint) const
{
	using namespace GarageLayout;
	auto within = [&](glm::vec2 min, glm::vec2 max) {
		return footprint.min.x >= min.x && footprint.max.x <= max.x && footprint.min.z >= min.y && footprint.max.z <= max.y;
	};
	// Out on the drive in front of the door, or through the doorway itself while it's up.
	constexpr float DRIVE_DEPTH = 9.f;
	if (within({ DRIVE_MIN_X, HALF_DEPTH + WALL }, { DRIVE_MAX_X, HALF_DEPTH + WALL + DRIVE_DEPTH }))
		return true;
	if (m_doorOpen && within({ DOOR_MIN_X, -HALF_DEPTH }, { DOOR_MAX_X, HALF_DEPTH + WALL + DRIVE_DEPTH }))
		return true;
	// Along the track and into the salvage yard, to the car. Rolling from one onto the next it's partly on each,
	// so it's enough that every corner of it is on the drive, the track or the yard.
	auto onGround = [&](float x, float z) {
		auto in = [&](const Paved& area) { return x >= area.min.x && x <= area.max.x && z >= area.min.y && z <= area.max.y; };
		// Past the gate only while it's open.
		return in(PAVED[0]) || (m_yardGateOpen && (in(YARD_TRACK) || in(YARD)));
	};
	return onGround(footprint.min.x, footprint.min.z) && onGround(footprint.max.x, footprint.min.z) && onGround(footprint.min.x, footprint.max.z)
		&& onGround(footprint.max.x, footprint.max.z);
}

void ShopWorld::UpdateLift(float deltaTime)
{
	// The carriages run up and down the posts; the arms go with them wherever the lift is moved to.
	constexpr float LOWERED = 0.08f, RAISED = 1.75f, SPEED = 0.35f;
	float target = m_liftRaised ? RAISED : LOWERED;
	float step = SPEED * deltaTime;
	m_liftMoving = m_liftHeight != target && m_liftStation < m_stations.size() && m_stations[m_liftStation].present;
	m_liftHeight = m_liftHeight < target ? (std::min)(target, m_liftHeight + step) : (std::max)(target, m_liftHeight - step);
	if (!m_liftArms || m_liftStation >= m_stations.size())
		return;
	const auto& lift = m_stations[m_liftStation];
	m_liftArms->SetVisible(lift.present);
	m_liftArms->SetPosition(lift.object->GetPosition() + lift.object->GetRotation() * glm::vec3(0.f, m_liftHeight, 0.f));
	m_liftArms->SetRotation(lift.object->GetRotation());
}

bool ShopWorld::OnPlot(const ShopBox& footprint) const
{
	using namespace GarageLayout;
	// On the shop's own land outside: within the plot's bounds, not in or through the building (the floor inside is
	// OnShopFloor's), and clear of everything that stands there (walls, stairs, the yard's fence and junk, the drums,
	// the neighbours' shacks).
	if (footprint.min.x < PLOT_MIN.x || footprint.max.x > PLOT_MAX.x || footprint.min.z < PLOT_MIN.y || footprint.max.z > PLOT_MAX.y)
		return false;
	ShopInterior interior = GetInterior();
	glm::vec3 middle = (footprint.min + footprint.max) * 0.5f;
	for (glm::vec3 corner : { glm::vec3(footprint.min.x, 0.5f, footprint.min.z), glm::vec3(footprint.max.x, 0.5f, footprint.min.z),
			glm::vec3(footprint.min.x, 0.5f, footprint.max.z), glm::vec3(footprint.max.x, 0.5f, footprint.max.z), glm::vec3(middle.x, 0.5f, middle.z) })
		if (interior.InGarage(corner))
			return false;
	if (InsideWalls(footprint))
		return false;
	auto blocks = [&](const FloorObstacle& obstacle) {
		return obstacle.top > 0.05f && obstacle.bottom < footprint.max.y && footprint.min.x < obstacle.max.x && obstacle.min.x < footprint.max.x
			&& footprint.min.z < obstacle.max.y && obstacle.min.y < footprint.max.z;
	};
	for (const auto* list : { &m_staticObstacles, &m_neighbourObstacles })
		for (const auto& obstacle : *list)
			if (blocks(obstacle))
				return false;
	// Not on the quarry's pad: it's the quarry's.
	if (footprint.min.x < QUARRY_PAD.max.x && QUARRY_PAD.min.x < footprint.max.x && footprint.min.z < QUARRY_PAD.max.y && QUARRY_PAD.min.y < footprint.max.z)
		return false;
	// Not across the yard's gateway while the gate's shut.
	if (!m_yardGateOpen && footprint.max.x > YARD_MIN_X - 0.1f && footprint.min.x < YARD_MIN_X + 0.1f
			&& footprint.max.z > YARD_GATE_MIN_Z && footprint.min.z < YARD_GATE_MAX_Z)
		return false;
	return true;
}
