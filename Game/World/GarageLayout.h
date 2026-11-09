#pragma once

#include <vector>

#include <glm/glm.hpp>

// The shop building (a wooden shack) and the lot around it, in metres. The interior runs from
// -HALF_WIDTH to HALF_WIDTH in X and -HALF_DEPTH to HALF_DEPTH in Z; the roll-up door is in the
// front (+Z) wall and the street runs along X in front of the lot.
namespace GarageLayout
{
	constexpr float HALF_WIDTH = 5.f;
	constexpr float HALF_DEPTH = 3.5f;
	constexpr float HEIGHT = 3.6f;			// floor to ceiling
	constexpr float WALL = 0.12f;			// thickness, built outward from the interior faces
	constexpr float PLASTER = 0.08f;		// the inner layer of that (plank lining); the rest is board siding

	constexpr float DOOR_MIN_X = -1.2f;
	constexpr float DOOR_MAX_X = 2.0f;
	constexpr float DOOR_HEIGHT = 2.8f;
	constexpr float DOOR_SECONDS = 2.2f;	// to roll fully up or down

	constexpr float ROOF_RIDGE_Y = 4.6f;
	constexpr float ROOF_OVERHANG = 0.4f;

	// Upgrades. The side bay is built onto the left wall, with an opening knocked through it.
	constexpr float BAY_MIN_X = -10.f;			// the bay's far (left) wall, inside face
	constexpr float BAY_HALF_DEPTH = 3.f;
	constexpr float BAY_HEIGHT = 3.2f;
	constexpr float BAY_OPENING_HALF = 2.2f;	// the opening through the old left wall runs z -2.2..2.2
	constexpr float BAY_OPENING_HEIGHT = 2.9f;
	inline constexpr glm::vec3 BAY_LAMPS[] = { { -7.5f, 3.18f, -1.4f }, { -7.5f, 3.18f, 1.4f } };
	// The extension's own roll-up door, in its front wall, as wide as the shop's.
	constexpr float BAY_DOOR_MIN_X = -9.f;
	constexpr float BAY_DOOR_MAX_X = BAY_DOOR_MIN_X + (DOOR_MAX_X - DOOR_MIN_X);
	constexpr float BAY_DOOR_HEIGHT = 2.6f;

	// The second story sits on the shack's ceiling, reached by stairs up the outside of the back
	// wall to a door in the upstairs back wall.
	constexpr float UPPER_FLOOR_Y = HEIGHT + WALL + 0.02f;	// the plywood floor on top of the ceiling
	constexpr float UPPER_HEIGHT = 2.6f;		// upstairs floor to ceiling
	constexpr float UPPER_DOOR_MIN_X = -1.25f;
	constexpr float UPPER_DOOR_MAX_X = -0.25f;
	constexpr float UPPER_DOOR_HEIGHT = 2.1f;
	constexpr float UPPER_RIDGE_Y = UPPER_FLOOR_Y + UPPER_HEIGHT + WALL + (ROOF_RIDGE_Y - HEIGHT - WALL);
	inline constexpr glm::vec3 UPPER_LAMPS[] = { { -2.5f, UPPER_FLOOR_Y + UPPER_HEIGHT - 0.02f, 0.f }, { 2.5f, UPPER_FLOOR_Y + UPPER_HEIGHT - 0.02f, 0.f } };
	constexpr int STAIR_STEPS = 20;
	constexpr float STAIR_BOTTOM_X = 6.2f;		// the first step, past the corner; they climb toward -X
	constexpr float STAIR_TOP_X = 0.f;			// the landing runs on from here to the door
	constexpr float LANDING_END_X = -1.5f;
	constexpr float STAIR_NEAR_Z = -HALF_DEPTH - WALL - 0.04f;
	constexpr float STAIR_FAR_Z = STAIR_NEAR_Z - 1.1f;
	// The loft's stairs inside: a steep flight up the front wall left of the roll-up door, climbing
	// toward the left wall, through a stairwell in the loft floor railed round upstairs.
	constexpr int INSIDE_STAIR_STEPS = 16;
	constexpr float INSIDE_STAIR_BOTTOM_X = -1.5f;		// the first step; they climb toward -X
	constexpr float INSIDE_STAIR_TOP_X = -HALF_WIDTH;
	constexpr float INSIDE_STAIR_NEAR_Z = HALF_DEPTH - 0.95f;	// the open side; the other is the front wall
	constexpr float STAIRWELL_MAX_X = -2.6f;			// the hole runs from the left wall to here
	constexpr float STAIRWELL_RAIL_MIN_X = -4.f;		// upstairs, the way onto the stairs is left of here

	enum class Wall { BACK, FRONT, LEFT, RIGHT };

	// A window opening. u runs along the wall (X for back and front, Z for the sides), v is height.
	struct Window
	{
		Wall wall = Wall::BACK;
		float u0 = 0.f, u1 = 0.f;
		float v0 = 0.f, v1 = 0.f;
	};

	// High on the back wall, over the bench and shelving; eye level at the front (right of the door) and
	// down each side.
	inline constexpr Window WINDOWS[] = {
		{ Wall::BACK, -4.3f, -2.0f, 2.45f, 3.25f },
		{ Wall::BACK, 1.6f, 3.9f, 2.45f, 3.25f },
		{ Wall::FRONT, 2.8f, 4.4f, 1.15f, 2.35f },
		{ Wall::LEFT, -2.0f, -0.6f, 1.1f, 2.3f },
		{ Wall::LEFT, 0.6f, 2.0f, 1.1f, 2.3f },
		{ Wall::RIGHT, -0.9f, 0.9f, 1.4f, 2.4f },
	};

	// Ceiling lamps, which double as the shop's point lights.
	inline constexpr glm::vec3 LAMPS[] = {
		{ -3.f, 3.58f, -1.6f }, { 0.f, 3.58f, -1.6f }, { 3.f, 3.58f, -1.6f },
		{ -3.f, 3.58f, 1.4f }, { 0.f, 3.58f, 1.4f }, { 3.f, 3.58f, 1.4f },
	};

	// Outside: open desert in every direction, ringed by mountains on the horizon (see ShopLighting, which
	// keeps them there). A divided highway runs along X in front of the shack, out of sight both ways,
	// and a paved drive runs down to it from the roll-up door.
	// The highway: two lanes each way, with shoulders, and a strip of sand between the carriageways.
	constexpr float HIGHWAY_NEAR_Z = 40.f;			// the near edge of the near shoulder
	constexpr float HIGHWAY_SHOULDER = 1.5f;
	constexpr float HIGHWAY_LANE = 3.6f;
	constexpr float HIGHWAY_MEDIAN = 5.f;
	constexpr float HIGHWAY_CARRIAGEWAY = HIGHWAY_SHOULDER * 2.f + HIGHWAY_LANE * 2.f;
	constexpr float HIGHWAY_FAR_Z = HIGHWAY_NEAR_Z + HIGHWAY_CARRIAGEWAY * 2.f + HIGHWAY_MEDIAN;
	constexpr float HIGHWAY_HALF_LENGTH = 1000.f;
	// How far you can walk: well out into the desert either side and behind, and a little way past the highway.
	constexpr float WORLD_HALF_WIDTH = 150.f;
	constexpr float WORLD_BACK_Z = -150.f;
	constexpr float WORLD_FAR_Z = HIGHWAY_FAR_Z + 40.f;

	// Pavement: the drive down to the highway, and an apron round the back and left of the shack. Nothing
	// overlaps, so nothing flickers; footsteps on them sound like concrete.
	struct Paved
	{
		glm::vec2 min = {};
		glm::vec2 max = {};
	};
	constexpr float DRIVE_MIN_X = -1.7f;
	constexpr float DRIVE_MAX_X = 2.5f;
	inline constexpr Paved PAVED[] = {
		{ { DRIVE_MIN_X, HALF_DEPTH + WALL }, { DRIVE_MAX_X, HIGHWAY_NEAR_Z } },
		{ { -HALF_WIDTH - WALL - 0.9f, -HALF_DEPTH - WALL - 0.9f }, { HALF_WIDTH + WALL + 0.9f, -HALF_DEPTH - WALL } },
		{ { -HALF_WIDTH - WALL - 0.9f, -HALF_DEPTH - WALL }, { -HALF_WIDTH - WALL, HALF_DEPTH + WALL } },
	};
	// The extension's drive, out of its roll-up door and across to the main one (only once it's built).
	inline constexpr Paved BAY_DRIVE = { { BAY_DOOR_MIN_X - 0.3f, BAY_HALF_DEPTH + WALL }, { BAY_DOOR_MAX_X + 0.3f, HALF_DEPTH + 6.f } };
	inline constexpr Paved BAY_DRIVE_LINK = { { BAY_DOOR_MIN_X - 0.3f, HALF_DEPTH + 6.f }, { DRIVE_MIN_X, HALF_DEPTH + 9.f } };

	// Lights along the near shoulder of the highway, either side of where the drive comes out, arms out over it.
	inline constexpr glm::vec3 STREET_LIGHTS[] = {
		{ -105.f, 0.f, HIGHWAY_NEAR_Z - 0.8f }, { -75.f, 0.f, HIGHWAY_NEAR_Z - 0.8f }, { -45.f, 0.f, HIGHWAY_NEAR_Z - 0.8f },
		{ -15.f, 0.f, HIGHWAY_NEAR_Z - 0.8f }, { 15.f, 0.f, HIGHWAY_NEAR_Z - 0.8f }, { 45.f, 0.f, HIGHWAY_NEAR_Z - 0.8f },
		{ 75.f, 0.f, HIGHWAY_NEAR_Z - 0.8f }, { 105.f, 0.f, HIGHWAY_NEAR_Z - 0.8f },
	};
	constexpr float STREET_LIGHT_HEIGHT = 7.f;
	constexpr float STREET_LIGHT_REACH = 2.8f;		// how far the arm reaches out over the road, to the lamp
	inline constexpr glm::vec3 DOOR_LIGHT = { (DOOR_MIN_X + DOOR_MAX_X) * 0.5f, 3.2f, HALF_DEPTH + WALL + 0.15f };

	// The menu's view: a camera circling the shack from high over the desert, looking in at it.
	inline constexpr glm::vec3 MENU_ORBIT_CENTER = { 0.f, 2.f, 0.f };
	constexpr float MENU_ORBIT_RADIUS = 17.f;
	constexpr float MENU_ORBIT_HEIGHT = 8.f;
	constexpr float MENU_ORBIT_SPEED = 0.04f;		// radians a second
}

// The inside of the shack as it stands, upgrades and all: where the interior lighting applies, and the
// lamps.
struct ShopInterior
{
	glm::vec3 min = {};
	glm::vec3 max = {};
	std::vector<glm::vec3> lamps = {};

	bool InGarage(const glm::vec3& point) const
	{
		return glm::all(glm::greaterThan(point, min)) && glm::all(glm::lessThan(point, max));
	}
	bool Contains(const glm::vec3& point) const { return InGarage(point); }
};
