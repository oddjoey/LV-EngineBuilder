#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "GarageLayout.h"

// The house built onto the garage, and the yard and park round it: the house's walls and the gaps
// in them, and every piece of furniture as boxes. The models are built from these, and so is what
// the player bumps into, so the two always agree.
namespace HouseLayout
{
	// A run of wall along X or Z, from..to. Outside walls have their inner face on plane and are
	// built outward (outward is +1 or -1); inside walls are centred on it (outward is 0).
	struct WallRun
	{
		bool alongX = true;
		float plane = 0.f;
		float from = 0.f;
		float to = 0.f;
		float outward = 0.f;
	};

	// A gap in a wall: u along it, v up from the floor. Doors go to the floor.
	struct Opening
	{
		float u0 = 0.f, u1 = 0.f;
		float v0 = 0.f, v1 = 0.f;
	};

	struct Wall
	{
		WallRun run = {};
		std::vector<Opening> openings = {};	// in order along the wall
	};

	// The front, back and right outside walls, then the inside walls.
	const std::vector<Wall>& Walls();

	struct Solid
	{
		glm::vec3 min = {};
		glm::vec3 max = {};
	};
	// The walls as solid boxes, split round the doorways: windows count as wall.
	std::vector<Solid> WallSolids();

	// What each piece is made of, which is the model (and texture) it goes in.
	enum class Material
	{
		WOOD,		// cabinets, tables, the shed, fence-like things
		PAINTED,	// appliances, fittings, metal
		PLAIN,		// fabric, water, stone: just the colour
		FOLIAGE,	// hedges and plants
		COUNT
	};

	struct Piece
	{
		glm::vec3 min = {};
		glm::vec3 max = {};
		glm::vec3 color = { 1.f, 1.f, 1.f };
		Material material = Material::WOOD;
		bool blocks = false;	// the player walks round it
	};

	// The house's furniture, kitchen to bedroom (the bed is a model of its own: it's slept in).
	const std::vector<Piece>& Furniture();
	// The backyard, the front yard and the park across the street.
	const std::vector<Piece>& Yard();

	// The bed, centred on HOUSE_BED with its head to the back wall.
	constexpr float BED_HALF_WIDTH = 0.8f;
	constexpr float BED_HALF_LENGTH = 1.05f;
}
