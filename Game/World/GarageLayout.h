#pragma once

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

	enum class Wall { BACK, FRONT, LEFT, RIGHT };

	// A window opening. u runs along the wall (X for back and front, Z for the sides), v is height.
	struct Window
	{
		Wall wall = Wall::BACK;
		float u0 = 0.f, u1 = 0.f;
		float v0 = 0.f, v1 = 0.f;
	};

	// High on the back and right walls, over the shelving and machines; eye level elsewhere.
	inline constexpr Window WINDOWS[] = {
		{ Wall::BACK, -5.6f, -2.0f, 2.45f, 3.25f },
		{ Wall::BACK, -1.2f, 1.2f, 2.45f, 3.25f },
		{ Wall::BACK, 2.0f, 5.6f, 2.45f, 3.25f },
		{ Wall::FRONT, -6.4f, -4.0f, 1.15f, 2.35f },
		{ Wall::FRONT, 1.6f, 3.6f, 1.15f, 2.35f },
		{ Wall::FRONT, 4.4f, 6.2f, 1.15f, 2.35f },
		{ Wall::LEFT, -3.5f, -1.5f, 1.1f, 2.3f },
		{ Wall::LEFT, 0.5f, 2.5f, 1.1f, 2.3f },
		{ Wall::RIGHT, -3.8f, -1.2f, 2.3f, 3.2f },
		{ Wall::RIGHT, 0.2f, 2.8f, 2.3f, 3.2f },
	};

	// Ceiling lamps, which double as the shop's point lights.
	inline constexpr glm::vec3 LAMPS[] = {
		{ -4.f, 3.58f, -2.5f }, { 0.f, 3.58f, -2.5f }, { 4.f, 3.58f, -2.5f },
		{ -4.f, 3.58f, 1.5f }, { 0.f, 3.58f, 1.5f }, { 4.f, 3.58f, 1.5f },
	};

	// The walkable lot: fenced at the back and sides, open to the street at the front.
	constexpr float LOT_HALF_WIDTH = 20.f;
	constexpr float LOT_BACK_Z = -14.f;
	constexpr float STREET_NEAR_Z = 18.f;
	constexpr float STREET_FAR_Z = 26.f;
	constexpr float WALK_LIMIT_Z = 27.5f;	// the far kerb

	// Street lights along the near kerb.
	inline constexpr glm::vec3 STREET_LIGHTS[] = {
		{ -24.f, 0.f, 17.3f }, { -6.f, 0.f, 17.3f }, { 12.f, 0.f, 17.3f }, { 30.f, 0.f, 17.3f },
	};
	constexpr float STREET_LIGHT_HEIGHT = 5.6f;
	inline constexpr glm::vec3 DOOR_LIGHT = { -1.5f, 3.2f, HALF_DEPTH + WALL + 0.15f };
}
