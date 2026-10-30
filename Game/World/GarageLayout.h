#pragma once

#include <vector>

#include <glm/glm.hpp>

// The garage building and the lot around it, in metres. The garage interior runs from
// -HALF_WIDTH to HALF_WIDTH in X and -HALF_DEPTH to HALF_DEPTH in Z; the roll-up door is in the
// front (+Z) wall and the street runs along X in front of the lot.
namespace GarageLayout
{
	constexpr float HALF_WIDTH = 7.f;
	constexpr float HALF_DEPTH = 5.f;
	constexpr float HEIGHT = 3.6f;			// floor to ceiling
	constexpr float WALL = 0.12f;			// thickness, built outward from the interior faces
	constexpr float PLASTER = 0.08f;		// the inner layer of that; the rest is metal siding

	constexpr float DOOR_MIN_X = -3.1f;
	constexpr float DOOR_MAX_X = 0.1f;
	constexpr float DOOR_HEIGHT = 2.8f;
	constexpr float DOOR_SECONDS = 2.2f;	// to roll fully up or down

	constexpr float ROOF_RIDGE_Y = 4.95f;
	constexpr float ROOF_OVERHANG = 0.4f;

	// Upgrades. The side bay is built onto the left wall, with an opening knocked through it.
	constexpr float BAY_MIN_X = -13.f;			// the bay's far (left) wall, inside face
	constexpr float BAY_HALF_DEPTH = 4.f;
	constexpr float BAY_HEIGHT = 3.2f;
	constexpr float BAY_OPENING_HALF = 3.f;		// the opening through the old left wall runs z -3..3
	constexpr float BAY_OPENING_HEIGHT = 2.9f;
	inline constexpr glm::vec3 BAY_LAMPS[] = { { -10.f, 3.18f, -1.8f }, { -10.f, 3.18f, 1.8f } };
	// The extension's own roll-up door, in its front wall, as wide as the shop's.
	constexpr float BAY_DOOR_MIN_X = -11.7f;
	constexpr float BAY_DOOR_MAX_X = BAY_DOOR_MIN_X + (DOOR_MAX_X - DOOR_MIN_X);
	constexpr float BAY_DOOR_HEIGHT = 2.6f;

	// The second story sits on the garage's ceiling, reached by stairs up the outside of the back
	// wall to a door in the upstairs back wall.
	constexpr float UPPER_FLOOR_Y = HEIGHT + WALL + 0.02f;	// the plywood floor on top of the ceiling
	constexpr float UPPER_HEIGHT = 2.6f;		// upstairs floor to ceiling
	constexpr float UPPER_DOOR_MIN_X = -1.25f;
	constexpr float UPPER_DOOR_MAX_X = -0.25f;
	constexpr float UPPER_DOOR_HEIGHT = 2.1f;
	constexpr float UPPER_RIDGE_Y = UPPER_FLOOR_Y + UPPER_HEIGHT + WALL + (ROOF_RIDGE_Y - HEIGHT - WALL);
	inline constexpr glm::vec3 UPPER_LAMPS[] = { { -3.5f, UPPER_FLOOR_Y + UPPER_HEIGHT - 0.02f, 0.f }, { 3.5f, UPPER_FLOOR_Y + UPPER_HEIGHT - 0.02f, 0.f } };
	constexpr int STAIR_STEPS = 20;
	constexpr float STAIR_BOTTOM_X = 6.2f;		// the first step; they climb toward -X
	constexpr float STAIR_TOP_X = 0.f;			// the landing runs on from here to the door
	constexpr float LANDING_END_X = -1.5f;
	constexpr float STAIR_NEAR_Z = -HALF_DEPTH - WALL - 0.04f;
	constexpr float STAIR_FAR_Z = STAIR_NEAR_Z - 1.1f;
	// The loft's stairs inside: a steep flight up the front wall left of the roll-up door, climbing
	// toward the left wall, through a stairwell in the loft floor railed round upstairs.
	constexpr int INSIDE_STAIR_STEPS = 16;
	constexpr float INSIDE_STAIR_BOTTOM_X = -3.4f;		// the first step; they climb toward -X
	constexpr float INSIDE_STAIR_TOP_X = -HALF_WIDTH;
	constexpr float INSIDE_STAIR_NEAR_Z = 4.05f;		// the open side; the other is the front wall
	constexpr float STAIRWELL_MAX_X = -4.6f;			// the hole runs from the left wall to here
	constexpr float STAIRWELL_RAIL_MIN_X = -6.f;		// upstairs, the way onto the stairs is left of here

	enum class Wall { BACK, FRONT, LEFT, RIGHT };

	// A window opening. u runs along the wall (X for back and front, Z for the sides), v is height.
	struct Window
	{
		Wall wall = Wall::BACK;
		float u0 = 0.f, u1 = 0.f;
		float v0 = 0.f, v1 = 0.f;
	};

	// High on the back wall, over the shelving; eye level at the front and left. The right wall is the
	// house's now, and has none.
	inline constexpr Window WINDOWS[] = {
		{ Wall::BACK, -5.6f, -2.0f, 2.45f, 3.25f },
		{ Wall::BACK, -1.2f, 1.2f, 2.45f, 3.25f },
		{ Wall::BACK, 2.0f, 5.6f, 2.45f, 3.25f },
		{ Wall::FRONT, -6.4f, -4.0f, 1.15f, 2.35f },
		{ Wall::FRONT, 1.6f, 3.6f, 1.15f, 2.35f },
		{ Wall::LEFT, -3.5f, -1.5f, 1.1f, 2.3f },
		{ Wall::LEFT, 0.5f, 2.5f, 1.1f, 2.3f },
	};

	// A people door in the front wall, at the right-hand end, out onto the porch and round to the house.
	constexpr float SIDE_DOOR_MIN_X = 4.7f;
	constexpr float SIDE_DOOR_MAX_X = 5.6f;
	constexpr float SIDE_DOOR_HEIGHT = 2.1f;

	// The house, built onto the garage's right wall: its left wall is the garage's. Single story,
	// open kitchen and dining on the garage side, the living room at the front, the bathroom and
	// bedroom at the back. Interior faces, like the garage's.
	constexpr float HOUSE_MIN_X = HALF_WIDTH + WALL + PLASTER;	// plaster on the house side of the garage wall
	constexpr float HOUSE_MAX_X = 19.f;
	constexpr float HOUSE_HALF_DEPTH = HALF_DEPTH;
	constexpr float HOUSE_HEIGHT = 2.7f;
	constexpr float HOUSE_RIDGE_Y = 4.4f;
	constexpr float HOUSE_PARTITION = 0.1f;		// the inside walls' thickness
	constexpr float HOUSE_DOOR_HEIGHT = 2.1f;
	// The inside walls: kitchen | bathroom along x = KITCHEN_X, bathroom | bedroom along x = BATH_X,
	// and the living room in front of both along z = 0.
	constexpr float KITCHEN_X = 12.f;
	constexpr float BATH_X = 15.2f;
	constexpr float HALL_Z = 0.f;
	// Doorways: the front door and back door, and off the living room into the bathroom and bedroom.
	constexpr float FRONT_DOOR_MIN_X = 8.2f, FRONT_DOOR_MAX_X = 9.1f;
	constexpr float BACK_DOOR_MIN_X = 10.95f, BACK_DOOR_MAX_X = 11.85f;
	constexpr float BATH_DOOR_MIN_X = 12.7f, BATH_DOOR_MAX_X = 13.55f;
	constexpr float BED_DOOR_MIN_X = 16.5f, BED_DOOR_MAX_X = 17.35f;
	// The door between the garage and the house, at the front end of the garage's right wall.
	constexpr float HOUSE_LINK_MIN_Z = 4.12f;
	constexpr float HOUSE_LINK_MAX_Z = 4.92f;
	// A porch roof along the front of the house and over the garage's people door.
	constexpr float PORCH_MIN_X = 4.2f;
	constexpr float PORCH_DEPTH = 1.8f;
	constexpr float PORCH_HEIGHT = 2.45f;		// under the eaves of the house roof
	// The house's own windows: u along the wall, v from the floor (back and front: X; right: Z).
	inline constexpr Window HOUSE_WINDOWS[] = {
		{ Wall::FRONT, 10.f, 11.4f, 0.9f, 2.1f },
		{ Wall::FRONT, 13.9f, 15.9f, 0.9f, 2.1f },
		{ Wall::FRONT, 17.f, 18.4f, 0.9f, 2.1f },
		{ Wall::BACK, 8.4f, 10.4f, 1.1f, 2.1f },		// over the kitchen sink
		{ Wall::BACK, 13.3f, 14.3f, 1.5f, 2.1f },		// the bathroom, high up
		{ Wall::BACK, 16.3f, 17.7f, 1.4f, 2.1f },		// over the bed
		{ Wall::RIGHT, -3.6f, -1.6f, 0.9f, 2.1f },
		{ Wall::RIGHT, 3.4f, 4.6f, 0.9f, 2.1f },
	};
	// Ceiling lights, one a room: kitchen, dining, living room, bathroom, bedroom.
	inline constexpr glm::vec3 HOUSE_LAMPS[] = {
		{ 9.6f, HOUSE_HEIGHT - 0.03f, -2.8f }, { 9.8f, HOUSE_HEIGHT - 0.03f, 2.2f }, { 16.f, HOUSE_HEIGHT - 0.03f, 2.5f },
		{ 13.6f, HOUSE_HEIGHT - 0.03f, -2.5f }, { 17.1f, HOUSE_HEIGHT - 0.03f, -2.5f },
	};
	inline constexpr glm::vec3 HOUSE_BED = { 17.f, 0.f, -3.93f };		// the middle of the bed, its head to the back wall
	inline constexpr glm::vec3 PORCH_LIGHT = { 8.65f, PORCH_HEIGHT - 0.04f, HALF_DEPTH + WALL + PORCH_DEPTH * 0.5f };

	// Ceiling lamps, which double as the shop's point lights.
	inline constexpr glm::vec3 LAMPS[] = {
		{ -4.f, 3.58f, -2.5f }, { 0.f, 3.58f, -2.5f }, { 4.f, 3.58f, -2.5f },
		{ -4.f, 3.58f, 1.5f }, { 0.f, 3.58f, 1.5f }, { 4.f, 3.58f, 1.5f },
	};

	// The lot: fenced at the back and sides, open to the street at the front, with the backyard
	// behind the house. Neighbours either side, and across the street a park between more houses.
	constexpr float LOT_HALF_WIDTH = 30.f;
	constexpr float LOT_BACK_Z = -28.f;
	constexpr float STREET_NEAR_Z = 18.f;
	constexpr float STREET_FAR_Z = 26.f;
	constexpr float STREET_HALF_LENGTH = 160.f;
	// How far you can walk: along the street to either end of the block, and to the back fences
	// of the lots across it.
	constexpr float WORLD_HALF_WIDTH = 70.f;
	constexpr float WORLD_FAR_Z = 48.f;

	// Concrete: the drive, the sidewalks either side of the street, round the garage, the porch and
	// the path from it, the patio out back, the neighbours' paths and the park's. Nothing overlaps,
	// so nothing flickers; footsteps on them sound like concrete.
	struct Paved
	{
		glm::vec2 min = {};
		glm::vec2 max = {};
	};
	constexpr float DRIVE_MIN_X = -4.6f;
	constexpr float DRIVE_MAX_X = 1.6f;
	constexpr float SIDEWALK_NEAR_Z = 16.f;
	constexpr float SIDEWALK_FAR_Z = 17.6f;
	inline constexpr Paved PAVED[] = {
		{ { DRIVE_MIN_X, HALF_DEPTH }, { DRIVE_MAX_X, STREET_NEAR_Z } },
		{ { -STREET_HALF_LENGTH, SIDEWALK_NEAR_Z }, { DRIVE_MIN_X, SIDEWALK_FAR_Z } },
		{ { DRIVE_MAX_X, SIDEWALK_NEAR_Z }, { STREET_HALF_LENGTH, SIDEWALK_FAR_Z } },
		{ { -STREET_HALF_LENGTH, STREET_FAR_Z }, { STREET_HALF_LENGTH, STREET_FAR_Z + 1.6f } },
		// Round the back and left of the garage (the house is on the right).
		{ { -HALF_WIDTH - WALL - 0.9f, -HALF_DEPTH - WALL - 0.9f }, { HALF_WIDTH + WALL + 0.9f, -HALF_DEPTH - WALL } },
		{ { -HALF_WIDTH - WALL - 0.9f, -HALF_DEPTH - WALL }, { -HALF_WIDTH - WALL, HALF_DEPTH + WALL } },
		// The porch, and the path from the front door down to the sidewalk.
		{ { PORCH_MIN_X, HALF_DEPTH + WALL }, { HOUSE_MAX_X + WALL, HALF_DEPTH + WALL + PORCH_DEPTH } },
		{ { FRONT_DOOR_MIN_X - 0.05f, HALF_DEPTH + WALL + PORCH_DEPTH }, { FRONT_DOOR_MAX_X + 0.05f, SIDEWALK_NEAR_Z } },
		// The patio behind the house.
		{ { HALF_WIDTH + WALL + 1.f, -9.5f }, { 18.f, -HALF_DEPTH - WALL } },
		// The neighbours' front paths (houses at x = +-45 this side, +-32 and +-52 across).
		{ { -45.5f, 7.7f }, { -44.5f, SIDEWALK_NEAR_Z } },
		{ { 44.5f, 7.7f }, { 45.5f, SIDEWALK_NEAR_Z } },
		{ { -52.5f, STREET_FAR_Z + 1.6f }, { -51.5f, 33.3f } },
		{ { -32.5f, STREET_FAR_Z + 1.6f }, { -31.5f, 33.3f } },
		{ { 31.5f, STREET_FAR_Z + 1.6f }, { 32.5f, 33.3f } },
		{ { 51.5f, STREET_FAR_Z + 1.6f }, { 52.5f, 33.3f } },
		// The park: a path in from the street to a square round the fountain.
		{ { -1.f, STREET_FAR_Z + 1.6f }, { 1.f, 40.f } },
		{ { -5.f, 40.f }, { 5.f, 46.f } },
	};
	// The extension's drive, out of its roll-up door to the sidewalk (only once it's built).
	inline constexpr Paved BAY_DRIVE = { { BAY_DOOR_MIN_X - 0.3f, BAY_HALF_DEPTH + WALL }, { BAY_DOOR_MAX_X + 0.3f, SIDEWALK_NEAR_Z } };

	// Board fences, from one end to the other.
	struct FenceRun
	{
		glm::vec2 from = {};
		glm::vec2 to = {};
	};
	inline constexpr FenceRun FENCES[] = {
		{ { -LOT_HALF_WIDTH, LOT_BACK_Z }, { LOT_HALF_WIDTH, LOT_BACK_Z } },
		{ { -LOT_HALF_WIDTH, LOT_BACK_Z }, { -LOT_HALF_WIDTH, SIDEWALK_NEAR_Z - 1.f } },
		{ { LOT_HALF_WIDTH, LOT_BACK_Z }, { LOT_HALF_WIDTH, SIDEWALK_NEAR_Z - 1.f } },
		// The neighbours' back fences, and the ones behind the lots across the street.
		{ { -WORLD_HALF_WIDTH, LOT_BACK_Z }, { -LOT_HALF_WIDTH, LOT_BACK_Z } },
		{ { LOT_HALF_WIDTH, LOT_BACK_Z }, { WORLD_HALF_WIDTH, LOT_BACK_Z } },
		{ { -WORLD_HALF_WIDTH, WORLD_FAR_Z }, { WORLD_HALF_WIDTH, WORLD_FAR_Z } },
	};

	// Street lights along the near kerb.
	inline constexpr glm::vec3 STREET_LIGHTS[] = {
		{ -60.f, 0.f, 17.3f }, { -42.f, 0.f, 17.3f }, { -24.f, 0.f, 17.3f }, { -6.f, 0.f, 17.3f },
		{ 12.f, 0.f, 17.3f }, { 30.f, 0.f, 17.3f }, { 48.f, 0.f, 17.3f }, { 66.f, 0.f, 17.3f },
	};
	constexpr float STREET_LIGHT_HEIGHT = 5.6f;
	inline constexpr glm::vec3 DOOR_LIGHT = { -1.5f, 3.2f, HALF_DEPTH + WALL + 0.15f };
}

// The inside of the buildings as they stand, upgrades and all: where the interior lighting applies,
// and the lamps. The garage (with its bay and upstairs, if built) and the house are lit separately.
struct ShopInterior
{
	glm::vec3 min = {};
	glm::vec3 max = {};
	std::vector<glm::vec3> lamps = {};
	glm::vec3 houseMin = {};
	glm::vec3 houseMax = {};
	std::vector<glm::vec3> houseLamps = {};

	bool InGarage(const glm::vec3& point) const
	{
		return glm::all(glm::greaterThan(point, min)) && glm::all(glm::lessThan(point, max));
	}
	bool InHouse(const glm::vec3& point) const
	{
		return glm::all(glm::greaterThan(point, houseMin)) && glm::all(glm::lessThan(point, houseMax));
	}
	bool Contains(const glm::vec3& point) const { return InGarage(point) || InHouse(point); }
};
