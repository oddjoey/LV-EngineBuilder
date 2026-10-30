#include "ShopAssets.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "LV/Engine/Graphics/Models/MeshBuilder.h"
#include "HouseLayout.h"

// The house built onto the garage, its furniture, and the yard and park round it.

using namespace GarageLayout;

namespace HouseLayout
{
	const std::vector<Wall>& Walls()
	{
		static const std::vector<Wall> walls = [] {
			const float left = HALF_WIDTH + WALL;		// the garage's outside face
			const float right = HOUSE_MAX_X + WALL;
			const float half = HOUSE_PARTITION * 0.5f;
			auto windows = [](GarageLayout::Wall side) {
				std::vector<Opening> openings;
				for (const auto& window : HOUSE_WINDOWS)
					if (window.wall == side)
						openings.push_back({ window.u0, window.u1, window.v0, window.v1 });
				return openings;
			};
			std::vector<Wall> list = {
				{ { true, HOUSE_HALF_DEPTH, left, right, 1.f }, windows(GarageLayout::Wall::FRONT) },
				{ { true, -HOUSE_HALF_DEPTH, left, right, -1.f }, windows(GarageLayout::Wall::BACK) },
				{ { false, HOUSE_MAX_X, -HOUSE_HALF_DEPTH, HOUSE_HALF_DEPTH, 1.f }, windows(GarageLayout::Wall::RIGHT) },
				{ { false, KITCHEN_X, -HOUSE_HALF_DEPTH, HALL_Z + half, 0.f }, {} },
				{ { false, BATH_X, -HOUSE_HALF_DEPTH, HALL_Z - half, 0.f }, {} },
				{ { true, HALL_Z, KITCHEN_X - half, HOUSE_MAX_X, 0.f },
					{ { BATH_DOOR_MIN_X, BATH_DOOR_MAX_X, 0.f, HOUSE_DOOR_HEIGHT }, { BED_DOOR_MIN_X, BED_DOOR_MAX_X, 0.f, HOUSE_DOOR_HEIGHT } } },
			};
			list[0].openings.push_back({ FRONT_DOOR_MIN_X, FRONT_DOOR_MAX_X, 0.f, HOUSE_DOOR_HEIGHT });
			list[1].openings.push_back({ BACK_DOOR_MIN_X, BACK_DOOR_MAX_X, 0.f, HOUSE_DOOR_HEIGHT });
			for (auto& wall : list)
				std::sort(wall.openings.begin(), wall.openings.end(), [](const Opening& a, const Opening& b) { return a.u0 < b.u0; });
			return list;
		}();
		return walls;
	}

	namespace
	{
		// Where a box spanning u0..u1 along a wall, v0..v1 up it and d0..d1 out from its plane sits.
		Solid WallPiece(const WallRun& run, float u0, float u1, float v0, float v1, float d0, float d1)
		{
			float sign = run.outward == 0.f ? 1.f : run.outward;
			float a = run.plane + sign * d0, b = run.plane + sign * d1;
			float low = (std::min)(a, b), high = (std::max)(a, b);
			if (run.alongX)
				return { { u0, v0, low }, { u1, v1, high } };
			return { { low, v0, u0 }, { high, v1, u1 } };
		}

		// The wall's thickness as d0..d1: outward from an outside wall's face, either side of an inside wall's.
		glm::vec2 Thickness(const WallRun& run)
		{
			return run.outward == 0.f ? glm::vec2(-HOUSE_PARTITION * 0.5f, HOUSE_PARTITION * 0.5f) : glm::vec2(0.f, WALL);
		}
	}

	std::vector<Solid> WallSolids()
	{
		std::vector<Solid> solids;
		for (const auto& wall : Walls())
		{
			glm::vec2 depth = Thickness(wall.run);
			float cursor = wall.run.from;
			for (const auto& opening : wall.openings)
			{
				if (opening.v0 > 0.f)
					continue;	// a window: solid as far as anything's concerned
				solids.push_back(WallPiece(wall.run, cursor, opening.u0, 0.f, HOUSE_HEIGHT, depth.x, depth.y));
				solids.push_back(WallPiece(wall.run, opening.u0, opening.u1, opening.v1, HOUSE_HEIGHT, depth.x, depth.y));
				cursor = opening.u1;
			}
			solids.push_back(WallPiece(wall.run, cursor, wall.run.to, 0.f, HOUSE_HEIGHT, depth.x, depth.y));
		}
		return solids;
	}

	namespace
	{
		const glm::vec3 CABINET = { 0.72f, 0.55f, 0.38f };
		const glm::vec3 DARK_WOOD = { 0.45f, 0.32f, 0.22f };
		const glm::vec3 COUNTER = { 0.24f, 0.24f, 0.26f };
		const glm::vec3 APPLIANCE = { 0.92f, 0.92f, 0.9f };
		const glm::vec3 STEEL = { 0.7f, 0.72f, 0.74f };
		const glm::vec3 BLACK = { 0.08f, 0.08f, 0.09f };
		const glm::vec3 PORCELAIN = { 0.95f, 0.95f, 0.95f };
		const glm::vec3 LINEN = { 0.9f, 0.9f, 0.86f };
		const glm::vec3 STONE = { 0.55f, 0.55f, 0.53f };
		const glm::vec3 WATER = { 0.22f, 0.36f, 0.42f };
		const glm::vec3 LEAVES = { 0.7f, 0.85f, 0.7f };

		struct Pieces
		{
			std::vector<Piece> list;
			void Add(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color, Material material, bool blocks = false)
			{
				list.push_back({ min, max, color, material, blocks });
			}
			// Four legs under a top spanning min..max (in x and z), inset a little.
			void Legs(const glm::vec3& min, const glm::vec3& max, float height, float size, const glm::vec3& color, Material material)
			{
				for (float x : { min.x + 0.03f, max.x - 0.03f - size })
					for (float z : { min.z + 0.03f, max.z - 0.03f - size })
						Add({ x, 0.f, z }, { x + size, height, z + size }, color, material);
			}
			// A chair facing along +-Z (facing = +1: the sitter looks toward +Z), its seat centred on (x, z).
			void Chair(float x, float z, float facing)
			{
				glm::vec3 min = { x - 0.21f, 0.f, z - 0.21f }, max = { x + 0.21f, 0.f, z + 0.21f };
				Legs(min, max, 0.44f, 0.04f, DARK_WOOD, Material::WOOD);
				Add({ min.x, 0.44f, min.z }, { max.x, 0.48f, max.z }, DARK_WOOD, Material::WOOD, true);
				float back = facing > 0.f ? min.z : max.z - 0.04f;
				Add({ min.x, 0.48f, back }, { max.x, 0.92f, back + 0.04f }, DARK_WOOD, Material::WOOD);
			}
			// A park bench along X or Z, the sitter facing toward facing (a unit direction in the plane).
			void Bench(const glm::vec2& center, const glm::vec2& facing)
			{
				bool alongX = std::abs(facing.y) > 0.5f;
				glm::vec2 half = alongX ? glm::vec2(0.9f, 0.22f) : glm::vec2(0.22f, 0.9f);
				glm::vec3 min = { center.x - half.x, 0.f, center.y - half.y }, max = { center.x + half.x, 0.f, center.y + half.y };
				for (float t : { 0.1f, 0.9f })
				{
					glm::vec3 at = glm::mix(min, max, t);
					glm::vec3 legMin = alongX ? glm::vec3(at.x - 0.04f, 0.f, min.z) : glm::vec3(min.x, 0.f, at.z - 0.04f);
					glm::vec3 legMax = alongX ? glm::vec3(at.x + 0.04f, 0.42f, max.z) : glm::vec3(max.x, 0.42f, at.z + 0.04f);
					Add(legMin, legMax, BLACK, Material::PAINTED);
				}
				Add({ min.x, 0.42f, min.z }, { max.x, 0.46f, max.z }, CABINET, Material::WOOD, true);
				glm::vec2 backAt = center - facing * 0.2f;
				glm::vec3 backMin = alongX ? glm::vec3(min.x, 0.5f, backAt.y - 0.02f) : glm::vec3(backAt.x - 0.02f, 0.5f, min.z);
				glm::vec3 backMax = alongX ? glm::vec3(max.x, 0.85f, backAt.y + 0.02f) : glm::vec3(backAt.x + 0.02f, 0.85f, max.z);
				Add(backMin, backMax, CABINET, Material::WOOD);
			}
			// A picnic table: top and two benches along X.
			void PicnicTable(float x, float z)
			{
				Add({ x - 0.9f, 0.72f, z - 0.4f }, { x + 0.9f, 0.76f, z + 0.4f }, CABINET, Material::WOOD, true);
				for (float side : { -1.f, 1.f })
				{
					Add({ x - 0.9f, 0.42f, z + side * 0.7f - 0.15f }, { x + 0.9f, 0.46f, z + side * 0.7f + 0.15f }, CABINET, Material::WOOD, true);
					Add({ x + side * 0.7f - 0.04f, 0.f, z - 0.85f }, { x + side * 0.7f + 0.04f, 0.72f, z + 0.85f }, DARK_WOOD, Material::WOOD);
				}
			}
		};
	}

	const std::vector<Piece>& Furniture()
	{
		static const std::vector<Piece> furniture = [] {
			Pieces p;
			const float left = HOUSE_MIN_X;
			const float back = -HOUSE_HALF_DEPTH;
			const float hall = HALL_Z + HOUSE_PARTITION * 0.5f;		// the living room side of the hall wall
			const float bathBack = HALL_Z - HOUSE_PARTITION * 0.5f;	// the bathroom and bedroom side

			// Kitchen: counters along the back wall under the window and up the garage wall, the cooker
			// between them, the fridge, an island with stools.
			p.Add({ left, 0.f, back }, { 10.8f, 0.86f, back + 0.6f }, CABINET, Material::WOOD, true);
			p.Add({ left, 0.86f, back }, { 10.85f, 0.9f, back + 0.65f }, COUNTER, Material::PAINTED);
			p.Add({ 9.f, 0.9f, back + 0.1f }, { 9.9f, 0.905f, back + 0.5f }, STEEL, Material::PAINTED);
			p.Add({ 9.42f, 0.9f, back + 0.05f }, { 9.48f, 1.15f, back + 0.12f }, STEEL, Material::PAINTED);
			p.Add({ left, 1.45f, back }, { 8.3f, 2.2f, back + 0.35f }, CABINET, Material::WOOD);
			p.Add({ 10.5f, 1.45f, back }, { 10.85f, 2.2f, back + 0.35f }, CABINET, Material::WOOD);
			p.Add({ left, 0.f, -4.4f }, { left + 0.6f, 0.86f, -3.75f }, CABINET, Material::WOOD, true);
			p.Add({ left, 0.86f, -4.4f }, { left + 0.65f, 0.9f, -3.75f }, COUNTER, Material::PAINTED);
			p.Add({ left, 0.f, -3.75f }, { left + 0.65f, 0.92f, -3.f }, APPLIANCE, Material::PAINTED, true);
			p.Add({ left + 0.08f, 0.92f, -3.65f }, { left + 0.57f, 0.925f, -3.1f }, BLACK, Material::PAINTED);
			p.Add({ left + 0.62f, 0.35f, -3.7f }, { left + 0.655f, 0.85f, -3.05f }, BLACK, Material::PAINTED);	// oven door glass
			p.Add({ left, 1.55f, -3.75f }, { left + 0.5f, 1.75f, -3.f }, STEEL, Material::PAINTED);
			p.Add({ left, 0.f, -3.f }, { left + 0.6f, 0.86f, -2.3f }, CABINET, Material::WOOD, true);
			p.Add({ left, 0.86f, -3.f }, { left + 0.65f, 0.9f, -2.3f }, COUNTER, Material::PAINTED);
			p.Add({ left, 0.f, -2.25f }, { left + 0.75f, 1.8f, -1.45f }, APPLIANCE, Material::PAINTED, true);
			p.Add({ left + 0.75f, 0.9f, -1.6f }, { left + 0.78f, 1.5f, -1.55f }, STEEL, Material::PAINTED);
			p.Add({ 9.f, 0.f, -2.9f }, { 10.8f, 0.86f, -2.f }, CABINET, Material::WOOD, true);
			p.Add({ 8.95f, 0.86f, -2.95f }, { 10.85f, 0.92f, -1.95f }, COUNTER, Material::PAINTED);
			for (float x : { 9.5f, 10.3f })
			{
				p.Add({ x - 0.17f, 0.62f, -1.75f }, { x + 0.17f, 0.67f, -1.41f }, DARK_WOOD, Material::WOOD, true);
				p.Add({ x - 0.03f, 0.f, -1.61f }, { x + 0.03f, 0.62f, -1.55f }, BLACK, Material::PAINTED);
			}

			// Dining: a table for four, a sideboard against the garage wall, a bench and hooks by the door.
			p.Add({ 9.2f, 0.72f, 1.55f }, { 10.8f, 0.76f, 2.45f }, DARK_WOOD, Material::WOOD, true);
			p.Legs({ 9.2f, 0.f, 1.55f }, { 10.8f, 0.f, 2.45f }, 0.72f, 0.06f, DARK_WOOD, Material::WOOD);
			for (float x : { 9.65f, 10.35f })
			{
				p.Chair(x, 1.25f, 1.f);
				p.Chair(x, 2.75f, -1.f);
			}
			p.Add({ left, 0.f, 0.5f }, { left + 0.45f, 0.85f, 2.5f }, CABINET, Material::WOOD, true);
			p.Add({ left, 0.f, 2.75f }, { left + 0.4f, 0.45f, 3.65f }, CABINET, Material::WOOD, true);
			p.Add({ left, 1.7f, 2.75f }, { left + 0.05f, 1.76f, 3.65f }, DARK_WOOD, Material::WOOD);
			// The front and back doors, open against the inside of the wall, and the door through from the
			// garage, open against the front wall.
			p.Add({ left, 0.f, HOUSE_HALF_DEPTH - 0.045f }, { left + 0.8f, 2.06f, HOUSE_HALF_DEPTH }, { 0.92f, 0.92f, 0.88f }, Material::PAINTED, true);
			p.Add({ FRONT_DOOR_MAX_X - 0.045f, 0.f, HOUSE_HALF_DEPTH - 0.9f }, { FRONT_DOOR_MAX_X, 2.06f, HOUSE_HALF_DEPTH }, { 0.5f, 0.14f, 0.12f }, Material::PAINTED, true);
			p.Add({ BACK_DOOR_MAX_X - 0.045f, 0.f, back }, { BACK_DOOR_MAX_X, 2.06f, back + 0.9f }, { 0.92f, 0.92f, 0.88f }, Material::PAINTED, true);

			// Living room: sofa facing the television on the right wall, a coffee table, an armchair, a
			// bookcase against the hall wall, a rug and a plant.
			p.Add({ 13.6f, 0.f, 0.9f }, { 18.f, 0.012f, 3.9f }, { 0.58f, 0.3f, 0.25f }, Material::PLAIN);
			const glm::vec3 sofa = { 0.33f, 0.4f, 0.52f };
			p.Add({ 14.f, 0.f, 1.3f }, { 14.9f, 0.42f, 3.5f }, sofa, Material::PLAIN, true);
			p.Add({ 14.f, 0.42f, 1.3f }, { 14.22f, 0.86f, 3.5f }, sofa, Material::PLAIN);
			p.Add({ 14.f, 0.42f, 1.3f }, { 14.9f, 0.62f, 1.5f }, sofa, Material::PLAIN);
			p.Add({ 14.f, 0.42f, 3.3f }, { 14.9f, 0.62f, 3.5f }, sofa, Material::PLAIN);
			p.Add({ 14.22f, 0.42f, 1.5f }, { 14.86f, 0.5f, 3.3f }, sofa * 1.15f, Material::PLAIN);
			p.Add({ 15.6f, 0.38f, 1.9f }, { 16.5f, 0.42f, 2.9f }, DARK_WOOD, Material::WOOD, true);
			p.Legs({ 15.6f, 0.f, 1.9f }, { 16.5f, 0.f, 2.9f }, 0.38f, 0.05f, DARK_WOOD, Material::WOOD);
			p.Add({ HOUSE_MAX_X - 0.45f, 0.f, 1.4f }, { HOUSE_MAX_X, 0.5f, 3.f }, DARK_WOOD, Material::WOOD, true);
			p.Add({ HOUSE_MAX_X - 0.2f, 0.5f, 2.15f }, { HOUSE_MAX_X - 0.1f, 0.6f, 2.25f }, BLACK, Material::PAINTED);
			p.Add({ HOUSE_MAX_X - 0.18f, 0.6f, 1.5f }, { HOUSE_MAX_X - 0.12f, 1.25f, 2.9f }, BLACK, Material::PAINTED);
			const glm::vec3 armchair = { 0.55f, 0.45f, 0.3f };
			p.Add({ 17.3f, 0.f, 4.f }, { 18.1f, 0.42f, 4.8f }, armchair, Material::PLAIN, true);
			p.Add({ 17.3f, 0.42f, 4.6f }, { 18.1f, 0.88f, 4.8f }, armchair, Material::PLAIN);
			p.Add({ 13.9f, 0.f, hall }, { 15.6f, 1.9f, hall + 0.04f }, DARK_WOOD, Material::WOOD, true);
			for (float x : { 13.9f, 15.56f })
				p.Add({ x, 0.f, hall }, { x + 0.04f, 1.9f, hall + 0.34f }, DARK_WOOD, Material::WOOD, true);
			const glm::vec3 spines[] = { { 0.6f, 0.15f, 0.12f }, { 0.15f, 0.3f, 0.5f }, { 0.75f, 0.65f, 0.3f }, { 0.2f, 0.45f, 0.25f } };
			for (int shelf = 0; shelf < 4; shelf++)
			{
				float y = 0.02f + shelf * 0.47f;
				p.Add({ 13.94f, y, hall }, { 15.56f, y + 0.03f, hall + 0.34f }, DARK_WOOD, Material::WOOD);
				for (int book = 0; book < 3; book++)
				{
					float x = 14.f + book * 0.5f + shelf * 0.07f;
					p.Add({ x, y + 0.03f, hall + 0.05f }, { x + 0.32f, y + 0.3f + 0.04f * ((book + shelf) % 3), hall + 0.28f }, spines[(book + shelf) % 4], Material::PLAIN);
				}
			}
			p.Add({ 18.5f, 0.f, 4.5f }, { 18.85f, 0.4f, 4.85f }, { 0.7f, 0.38f, 0.25f }, Material::PAINTED, true);
			p.Add({ 18.4f, 0.4f, 4.4f }, { 18.95f, 1.1f, 4.95f }, LEAVES, Material::FOLIAGE);

			// Bathroom: a tub along the back wall, the toilet and basin against the bedroom wall.
			const float bathLeft = KITCHEN_X + HOUSE_PARTITION * 0.5f;
			const float bathRight = BATH_X - HOUSE_PARTITION * 0.5f;
			p.Add({ bathLeft, 0.f, back }, { 13.75f, 0.55f, back + 0.8f }, PORCELAIN, Material::PAINTED, true);
			p.Add({ bathLeft + 0.1f, 0.5f, back + 0.1f }, { 13.65f, 0.553f, back + 0.7f }, { 0.75f, 0.8f, 0.82f }, Material::PAINTED);
			p.Add({ bathRight - 0.3f, 0.4f, -3.3f }, { bathRight, 0.8f, -2.8f }, PORCELAIN, Material::PAINTED, true);
			p.Add({ bathRight - 0.7f, 0.f, -3.25f }, { bathRight - 0.3f, 0.42f, -2.85f }, PORCELAIN, Material::PAINTED, true);
			p.Add({ bathRight - 0.5f, 0.f, -2.2f }, { bathRight, 0.82f, -1.3f }, CABINET, Material::WOOD, true);
			p.Add({ bathRight - 0.55f, 0.82f, -2.25f }, { bathRight, 0.86f, -1.25f }, PORCELAIN, Material::PAINTED);
			p.Add({ bathRight - 0.4f, 0.86f, -1.95f }, { bathRight - 0.15f, 0.87f, -1.55f }, STEEL, Material::PAINTED);
			p.Add({ bathRight - 0.02f, 1.2f, -2.1f }, { bathRight, 1.9f, -1.4f }, { 0.75f, 0.82f, 0.85f }, Material::PAINTED);
			p.Add({ 13.f, 0.f, -4.1f }, { 13.7f, 0.01f, -3.6f }, { 0.3f, 0.5f, 0.55f }, Material::PLAIN);
			p.Add({ BATH_DOOR_MIN_X, 0.f, bathBack - 0.9f }, { BATH_DOOR_MIN_X + 0.045f, 2.06f, bathBack }, { 0.92f, 0.92f, 0.88f }, Material::PAINTED, true);

			// Bedroom: the bedding on the bed, nightstands with lamps either side, a dresser, a rug.
			const glm::vec3 bed = HOUSE_BED;
			p.Add({ bed.x - 0.76f, 0.35f, bed.z - 0.98f }, { bed.x + 0.76f, 0.55f, bed.z + 1.03f }, LINEN, Material::PLAIN);
			p.Add({ bed.x - 0.78f, 0.55f, bed.z - 0.3f }, { bed.x + 0.78f, 0.58f, bed.z + 1.04f }, { 0.3f, 0.45f, 0.4f }, Material::PLAIN);
			for (float side : { -1.f, 1.f })
				p.Add({ bed.x + (side < 0.f ? -0.62f : 0.08f), 0.55f, bed.z - 0.95f }, { bed.x + (side < 0.f ? -0.08f : 0.62f), 0.66f, bed.z - 0.62f }, LINEN, Material::PLAIN);
			for (float x : { 15.6f, 17.9f })
			{
				p.Add({ x, 0.f, back }, { x + 0.5f, 0.55f, back + 0.45f }, CABINET, Material::WOOD, true);
				p.Add({ x + 0.15f, 0.55f, back + 0.12f }, { x + 0.35f, 0.85f, back + 0.32f }, { 0.95f, 0.9f, 0.75f }, Material::PLAIN);
			}
			p.Add({ HOUSE_MAX_X - 0.5f, 0.f, -1.4f }, { HOUSE_MAX_X, 0.95f, bathBack - 0.15f }, CABINET, Material::WOOD, true);
			p.Add({ 15.8f, 0.f, -2.8f }, { 18.2f, 0.012f, -1.3f }, { 0.45f, 0.5f, 0.6f }, Material::PLAIN);
			p.Add({ BED_DOOR_MAX_X - 0.045f, 0.f, bathBack - 0.9f }, { BED_DOOR_MAX_X, 2.06f, bathBack }, { 0.92f, 0.92f, 0.88f }, Material::PAINTED, true);
			return p.list;
		}();
		return furniture;
	}

	const std::vector<Piece>& Yard()
	{
		static const std::vector<Piece> yard = [] {
			Pieces p;
			const float porchFront = HALF_DEPTH + WALL + PORCH_DEPTH;

			// The porch roof's posts, clear of the doors and the path.
			for (float x : { PORCH_MIN_X + 0.1f, 7.f, 11.5f, 15.f, HOUSE_MAX_X + WALL - 0.1f })
				p.Add({ x - 0.07f, 0.f, porchFront - 0.17f }, { x + 0.07f, PORCH_HEIGHT, porchFront - 0.03f }, { 0.95f, 0.95f, 0.92f }, Material::PAINTED, true);
			// A mailbox by the path, and hedges along the front of the house.
			p.Add({ 9.5f, 0.f, 15.4f }, { 9.58f, 1.f, 15.48f }, DARK_WOOD, Material::WOOD, true);
			p.Add({ 9.32f, 1.f, 15.28f }, { 9.76f, 1.22f, 15.6f }, BLACK, Material::PAINTED);
			p.Add({ 9.4f, 0.f, porchFront + 0.4f }, { 13.5f, 0.75f, porchFront + 1.1f }, LEAVES, Material::FOLIAGE, true);
			p.Add({ 15.5f, 0.f, porchFront + 0.4f }, { 19.f, 0.75f, porchFront + 1.1f }, LEAVES, Material::FOLIAGE, true);

			// The patio: a picnic table and a barbecue.
			p.PicnicTable(12.5f, -7.3f);
			p.Add({ 15.9f, 0.62f, -6.6f }, { 16.5f, 0.95f, -6.05f }, BLACK, Material::PAINTED, true);
			p.Add({ 15.93f, 0.95f, -6.57f }, { 16.47f, 1.05f, -6.08f }, BLACK, Material::PAINTED);
			p.Legs({ 15.9f, 0.f, -6.6f }, { 16.5f, 0.f, -6.05f }, 0.62f, 0.04f, STEEL, Material::PAINTED);

			// Vegetable beds, a shed in the far corner, a fire pit with log seats, a swing set.
			for (float z : { -14.f, -11.f })
			{
				p.Add({ 21.f, 0.f, z }, { 25.f, 0.3f, z + 1.2f }, DARK_WOOD, Material::WOOD, true);
				p.Add({ 21.1f, 0.3f, z + 0.1f }, { 24.9f, 0.31f, z + 1.1f }, { 0.3f, 0.22f, 0.15f }, Material::PLAIN);
				for (int plant = 0; plant < 6; plant++)
				{
					float x = 21.35f + plant * 0.62f;
					p.Add({ x - 0.15f, 0.31f, z + 0.45f }, { x + 0.15f, 0.62f + 0.05f * (plant % 3), z + 0.75f }, LEAVES, Material::FOLIAGE);
				}
			}
			const glm::vec3 shedRed = { 0.55f, 0.2f, 0.16f };
			p.Add({ -22.f, 0.f, -23.5f }, { -18.f, 2.3f, -20.5f }, shedRed, Material::PAINTED, true);
			p.Add({ -22.2f, 2.3f, -23.7f }, { -17.8f, 2.42f, -20.3f }, { 0.25f, 0.25f, 0.27f }, Material::PAINTED);
			p.Add({ -20.6f, 0.f, -20.5f }, { -19.4f, 2.f, -20.46f }, { 0.92f, 0.92f, 0.88f }, Material::PAINTED);
			p.Add({ -21.6f, 1.2f, -20.5f }, { -21.f, 1.8f, -20.47f }, { 0.2f, 0.25f, 0.3f }, Material::PAINTED);
			p.Add({ -0.6f, 0.f, -18.6f }, { 0.6f, 0.3f, -17.4f }, STONE, Material::PLAIN, true);
			p.Add({ -0.45f, 0.3f, -18.45f }, { 0.45f, 0.31f, -17.55f }, { 0.12f, 0.1f, 0.09f }, Material::PLAIN);
			p.Add({ -2.3f, 0.f, -18.8f }, { -1.9f, 0.42f, -17.2f }, DARK_WOOD, Material::WOOD, true);
			p.Add({ 1.9f, 0.f, -18.8f }, { 2.3f, 0.42f, -17.2f }, DARK_WOOD, Material::WOOD, true);
			p.Add({ -0.8f, 0.f, -20.3f }, { 0.8f, 0.42f, -19.9f }, DARK_WOOD, Material::WOOD, true);
			const glm::vec3 swingBlue = { 0.2f, 0.35f, 0.6f };
			p.Add({ -14.f, 2.2f, -14.05f }, { -10.f, 2.3f, -13.95f }, swingBlue, Material::PAINTED);
			for (float x : { -14.f, -10.1f })
			{
				for (float z : { -14.45f, -13.65f })
					p.Add({ x, 0.f, z }, { x + 0.1f, 2.3f, z + 0.1f }, swingBlue, Material::PAINTED, true);
				p.Add({ x, 2.2f, -14.45f }, { x + 0.1f, 2.3f, -13.55f }, swingBlue, Material::PAINTED);
			}
			for (float x : { -13.f, -11.6f })
			{
				p.Add({ x - 0.25f, 0.45f, -14.15f }, { x + 0.25f, 0.5f, -13.85f }, BLACK, Material::PAINTED);
				for (float side : { -0.22f, 0.22f })
					p.Add({ x + side - 0.01f, 0.5f, -14.01f }, { x + side + 0.01f, 2.2f, -13.99f }, STEEL, Material::PAINTED);
			}

			// The park across the street: benches along the path, a fountain in the square, a pond,
			// picnic tables and a sandpit.
			for (float z : { 31.5f, 35.5f })
			{
				p.Bench({ -1.7f, z }, { 1.f, 0.f });
				p.Bench({ 1.7f, z }, { -1.f, 0.f });
			}
			p.Bench({ -3.f, 46.6f }, { 0.f, -1.f });
			p.Bench({ 3.f, 46.6f }, { 0.f, -1.f });
			p.Add({ -1.2f, 0.f, 41.8f }, { 1.2f, 0.5f, 44.2f }, STONE, Material::PLAIN, true);
			p.Add({ -1.05f, 0.5f, 41.95f }, { 1.05f, 0.505f, 44.05f }, WATER, Material::PLAIN);
			p.Add({ -0.15f, 0.5f, 42.85f }, { 0.15f, 1.3f, 43.15f }, STONE, Material::PLAIN);
			p.Add({ 8.f, 0.f, 34.f }, { 15.f, 0.15f, 42.f }, STONE, Material::PLAIN, true);
			p.Add({ 8.2f, 0.15f, 34.2f }, { 14.8f, 0.155f, 41.8f }, WATER, Material::PLAIN);
			p.PicnicTable(-10.f, 44.f);
			p.PicnicTable(11.f, 45.5f);
			p.Add({ -13.f, 0.f, 33.f }, { -9.f, 0.25f, 37.f }, CABINET, Material::WOOD, true);
			p.Add({ -12.9f, 0.25f, 33.1f }, { -9.1f, 0.26f, 36.9f }, { 0.85f, 0.75f, 0.55f }, Material::PLAIN);
			// A hedge along the park's sides, open at the street.
			for (float x : { -19.6f, 19.f })
				p.Add({ x, 0.f, 29.5f }, { x + 0.6f, 1.1f, WORLD_FAR_Z - 0.5f }, LEAVES, Material::FOLIAGE, true);
			return p.list;
		}();
		return yard;
	}
}

namespace
{
	using namespace HouseLayout;

	const glm::vec3 WHITE = { 1.f, 1.f, 1.f };
	const glm::vec3 PAINT_WHITE = { 0.95f, 0.95f, 0.92f };
	const glm::vec3 WALL_PAINT = { 0.93f, 0.9f, 0.84f };

	void Box(MeshBuilder& builder, const glm::vec3& min, const glm::vec3& max, const glm::vec3& color = WHITE)
	{
		builder.ResetTransform().SetColor(color).AddBox((min + max) * 0.5f, max - min);
	}

	void WallBox(MeshBuilder& builder, const WallRun& run, float u0, float u1, float v0, float v1, float d0, float d1, const glm::vec3& color)
	{
		float sign = run.outward == 0.f ? 1.f : run.outward;
		float a = run.plane + sign * d0, b = run.plane + sign * d1;
		float low = (std::min)(a, b), high = (std::max)(a, b);
		if (run.alongX)
			Box(builder, { u0, v0, low }, { u1, v1, high }, color);
		else
			Box(builder, { low, v0, u0 }, { high, v1, u1 }, color);
	}

	// One layer of a wall between heights v0 and v1, d0..d1 out from its plane, with its openings left out.
	void WallLayer(MeshBuilder& builder, const HouseLayout::Wall& wall, float d0, float d1, float v0, float v1, const glm::vec3& color)
	{
		float cursor = wall.run.from;
		for (const auto& opening : wall.openings)
		{
			if (opening.u0 > cursor)
				WallBox(builder, wall.run, cursor, opening.u0, v0, v1, d0, d1, color);
			if (opening.v0 > v0)
				WallBox(builder, wall.run, opening.u0, opening.u1, v0, (std::min)(opening.v0, v1), d0, d1, color);
			if (opening.v1 < v1)
				WallBox(builder, wall.run, opening.u0, opening.u1, (std::max)(opening.v1, v0), v1, d0, d1, color);
			cursor = opening.u1;
		}
		if (cursor < wall.run.to)
			WallBox(builder, wall.run, cursor, wall.run.to, v0, v1, d0, d1, color);
	}

	// A frame of bars round an opening, d0..d1 out from the wall's plane.
	void Frame(MeshBuilder& builder, const WallRun& run, const Opening& opening, float bar, float d0, float d1, const glm::vec3& color)
	{
		if (opening.v0 > 0.f)
			WallBox(builder, run, opening.u0 - bar, opening.u1 + bar, opening.v0 - bar, opening.v0, d0, d1, color);
		WallBox(builder, run, opening.u0 - bar, opening.u1 + bar, opening.v1, opening.v1 + bar, d0, d1, color);
		WallBox(builder, run, opening.u0 - bar, opening.u0, opening.v0, opening.v1, d0, d1, color);
		WallBox(builder, run, opening.u1, opening.u1 + bar, opening.v0, opening.v1, d0, d1, color);
	}

	// Two slabs pitched up to a ridge along X, centred on centerX, overhanging the walls by overhang.
	void GableRoof(MeshBuilder& builder, float centerX, float length, float halfDepth, float ridgeY, float slope, float overhang, float thickness)
	{
		float angle = std::atan(slope);
		float run = halfDepth + overhang;
		float slabLength = run / std::cos(angle);
		for (float side : { 1.f, -1.f })
		{
			float z = side * run * 0.5f;
			float y = ridgeY - run * 0.5f * slope;
			glm::vec3 normal = { 0.f, std::cos(angle), side * std::sin(angle) };
			glm::mat4 transform = glm::rotate(glm::translate(glm::mat4(1.f), glm::vec3(centerX, y, z) + normal * (thickness * 0.5f)), side * angle, { 1.f, 0.f, 0.f });
			builder.SetTransform(transform).SetColor(WHITE).AddBox({}, { length, thickness, slabLength });
		}
		builder.ResetTransform();
	}
}

void ShopAssets::LoadHouse()
{
	const float left = HALF_WIDTH + WALL;
	const float right = HOUSE_MAX_X + WALL;
	const float outerZ = HOUSE_HALF_DEPTH + WALL;
	const float wallTop = HOUSE_HEIGHT + WALL;
	const float slope = (HOUSE_RIDGE_Y - wallTop) / outerZ;
	const auto& walls = Walls();

	// Inside: painted plaster, the inside walls, the ceiling, skirting boards, sills, the bathroom tiles.
	{
		MeshBuilder shell;
		shell.SetUVScale(0.5f);
		const glm::vec3 skirting = { 0.97f, 0.97f, 0.95f };
		for (const auto& wall : walls)
		{
			bool inside = wall.run.outward == 0.f;
			float half = HOUSE_PARTITION * 0.5f;
			if (inside)
			{
				WallLayer(shell, wall, -half, half, 0.f, HOUSE_HEIGHT, WALL_PAINT);
				WallLayer(shell, wall, -half - 0.015f, -half, 0.f, 0.1f, skirting);
				WallLayer(shell, wall, half, half + 0.015f, 0.f, 0.1f, skirting);
			}
			else
			{
				WallLayer(shell, wall, 0.f, PLASTER, 0.f, HOUSE_HEIGHT, WALL_PAINT);
				WallLayer(shell, wall, -0.015f, 0.f, 0.f, 0.1f, skirting);
				for (const auto& opening : wall.openings)
					if (opening.v0 > 0.f)
						WallBox(shell, wall.run, opening.u0 - 0.05f, opening.u1 + 0.05f, opening.v0 - 0.04f, opening.v0, -0.05f, PLASTER, skirting);
			}
		}
		// The garage wall, plastered on this side, round the door through to the garage.
		Box(shell, { left, 0.f, -HOUSE_HALF_DEPTH }, { HOUSE_MIN_X, HOUSE_HEIGHT, HOUSE_LINK_MIN_Z }, WALL_PAINT);
		Box(shell, { left, 0.f, HOUSE_LINK_MAX_Z }, { HOUSE_MIN_X, HOUSE_HEIGHT, HOUSE_HALF_DEPTH }, WALL_PAINT);
		Box(shell, { left, HOUSE_DOOR_HEIGHT, HOUSE_LINK_MIN_Z }, { HOUSE_MIN_X, HOUSE_HEIGHT, HOUSE_LINK_MAX_Z }, WALL_PAINT);
		Box(shell, { HOUSE_MIN_X, 0.f, -HOUSE_HALF_DEPTH }, { HOUSE_MIN_X + 0.015f, 0.1f, HOUSE_LINK_MIN_Z - 0.07f }, skirting);
		Box(shell, { HOUSE_MIN_X, 0.f, HOUSE_LINK_MAX_Z + 0.07f }, { HOUSE_MIN_X + 0.015f, 0.1f, HOUSE_HALF_DEPTH }, skirting);
		Box(shell, { left, HOUSE_HEIGHT, -outerZ }, { right, wallTop, outerZ }, { 0.95f, 0.95f, 0.93f });
		Box(shell, { KITCHEN_X + HOUSE_PARTITION * 0.5f, 0.f, -HOUSE_HALF_DEPTH }, { BATH_X - HOUSE_PARTITION * 0.5f, 0.004f, HALL_Z - HOUSE_PARTITION * 0.5f }, { 0.82f, 0.84f, 0.86f });
		AddModel(ShopModels::HOME_SHELL, shell);
	}

	// Outside: brick, and the gable at the far end (the garage hides the near one).
	{
		MeshBuilder brick;
		brick.SetUVScale(0.6f);
		for (size_t i = 0; i < 3; i++)
			WallLayer(brick, walls[i], PLASTER, WALL, 0.f, wallTop, WHITE);
		brick.ResetTransform().SetColor(WHITE);
		brick.AddFlatTriangle({ right, wallTop, outerZ }, { right, wallTop, -outerZ }, { right, HOUSE_RIDGE_Y, 0.f });
		AddModel(ShopModels::HOME_BRICK, brick);
	}
	{
		MeshBuilder roof;
		roof.SetUVScale(0.5f);
		float roofEnd = right + 0.35f;
		GableRoof(roof, (left + roofEnd) * 0.5f, roofEnd - left, outerZ, HOUSE_RIDGE_Y, slope, 0.4f, 0.1f);
		AddModel(ShopModels::HOME_ROOF, roof);
	}

	// White trim: window and door frames inside and out, mullions, corner boards, the fascia and
	// ridge cap, and the porch roof.
	{
		MeshBuilder frames;
		frames.SetUVScale(2.f);
		for (const auto& wall : walls)
		{
			bool inside = wall.run.outward == 0.f;
			float half = HOUSE_PARTITION * 0.5f;
			for (const auto& opening : wall.openings)
			{
				if (inside)
				{
					Frame(frames, wall.run, opening, 0.07f, -half - 0.02f, -half, PAINT_WHITE);
					Frame(frames, wall.run, opening, 0.07f, half, half + 0.02f, PAINT_WHITE);
					continue;
				}
				Frame(frames, wall.run, opening, 0.07f, WALL, WALL + 0.03f, PAINT_WHITE);
				Frame(frames, wall.run, opening, 0.06f, -0.02f, 0.f, PAINT_WHITE);
				if (opening.v0 > 0.f && opening.u1 - opening.u0 > 1.2f)
				{
					float middle = (opening.u0 + opening.u1) * 0.5f;
					WallBox(frames, wall.run, middle - 0.025f, middle + 0.025f, opening.v0, opening.v1, 0.03f, 0.09f, PAINT_WHITE);
				}
			}
		}
		for (float z : { -outerZ, outerZ })
			Box(frames, { right - 0.08f, 0.f, z - 0.08f }, { right + 0.08f, wallTop, z + 0.08f }, PAINT_WHITE);
		// A casing round the door through to the garage.
		Box(frames, { HOUSE_MIN_X, HOUSE_DOOR_HEIGHT, HOUSE_LINK_MIN_Z - 0.07f }, { HOUSE_MIN_X + 0.02f, HOUSE_DOOR_HEIGHT + 0.07f, HOUSE_LINK_MAX_Z + 0.07f }, PAINT_WHITE);
		for (float z : { HOUSE_LINK_MIN_Z - 0.07f, HOUSE_LINK_MAX_Z })
			Box(frames, { HOUSE_MIN_X, 0.f, z }, { HOUSE_MIN_X + 0.02f, HOUSE_DOOR_HEIGHT, z + 0.07f }, PAINT_WHITE);
		float eaveZ = outerZ + 0.4f;
		float eaveY = HOUSE_RIDGE_Y - eaveZ * slope;
		for (float side : { -1.f, 1.f })
			Box(frames, { left, eaveY - 0.1f, side * eaveZ - 0.02f }, { right + 0.35f, eaveY + 0.1f, side * eaveZ + 0.02f }, PAINT_WHITE);
		Box(frames, { left, HOUSE_RIDGE_Y + 0.09f, -0.16f }, { right + 0.35f, HOUSE_RIDGE_Y + 0.15f, 0.16f }, { 0.3f, 0.3f, 0.32f });

		// The porch roof: a slab on the posts (see HouseLayout::Yard), with a fascia round it.
		const float porchFront = HALF_DEPTH + WALL + PORCH_DEPTH;
		Box(frames, { PORCH_MIN_X, PORCH_HEIGHT, HALF_DEPTH + WALL }, { right + 0.1f, PORCH_HEIGHT + 0.08f, porchFront + 0.1f }, { 0.32f, 0.31f, 0.3f });
		Box(frames, { PORCH_MIN_X, PORCH_HEIGHT - 0.12f, porchFront - 0.17f }, { right + 0.1f, PORCH_HEIGHT, porchFront - 0.03f }, PAINT_WHITE);
		AddModel(ShopModels::HOME_FRAMES, frames);
	}
	{
		MeshBuilder glass;
		for (size_t i = 0; i < 3; i++)
			for (const auto& opening : walls[i].openings)
				if (opening.v0 > 0.f)
					WallBox(glass, walls[i].run, opening.u0, opening.u1, opening.v0, opening.v1, 0.055f, 0.065f, { 0.75f, 0.85f, 0.9f });
		AddModel(ShopModels::HOME_GLASS, glass);
	}
	{
		MeshBuilder floor;
		floor.SetUVScale(2.5f);
		Box(floor, { left, -0.03f, -outerZ }, { right, 0.f, outerZ }, { 0.92f, 0.78f, 0.62f });
		AddModel(ShopModels::HOME_FLOOR, floor);
	}
	{
		MeshBuilder light;
		light.SetColor({ 1.f, 0.96f, 0.86f }).AddCylinder({ 0.f, 0.f, 0.f }, 0.2f, 0.05f, 16);
		AddModel(ShopModels::HOME_LIGHT, light);
	}
	{
		// The bed's frame, legs and headboard (the bedding is furniture), centred on its middle.
		MeshBuilder bed;
		bed.SetUVScale(2.f);
		const glm::vec3 wood = { 0.45f, 0.32f, 0.22f };
		Box(bed, { -BED_HALF_WIDTH, 0.12f, -BED_HALF_LENGTH }, { BED_HALF_WIDTH, 0.35f, BED_HALF_LENGTH }, wood);
		for (float x : { -BED_HALF_WIDTH, BED_HALF_WIDTH - 0.07f })
			Box(bed, { x, 0.f, BED_HALF_LENGTH - 0.07f }, { x + 0.07f, 0.12f, BED_HALF_LENGTH }, wood);
		Box(bed, { -BED_HALF_WIDTH - 0.05f, 0.f, -BED_HALF_LENGTH }, { BED_HALF_WIDTH + 0.05f, 1.05f, -BED_HALF_LENGTH + 0.07f }, wood);
		AddModel(ShopModels::HOME_BED, bed);
	}

	// The furniture and the yard, one model per material.
	for (int material = 0; material < static_cast<int>(HouseLayout::Material::COUNT); material++)
	{
		MeshBuilder furniture, yard;
		furniture.SetUVScale(2.f);
		yard.SetUVScale(1.f);
		for (const auto& piece : Furniture())
			if (piece.material == static_cast<HouseLayout::Material>(material))
				Box(furniture, piece.min, piece.max, piece.color);
		for (const auto& piece : Yard())
			if (piece.material == static_cast<HouseLayout::Material>(material))
				Box(yard, piece.min, piece.max, piece.color);
		AddModel(ShopModels::HomeFurniture(material), furniture);
		AddModel(ShopModels::YardPieces(material), yard);
	}
}
