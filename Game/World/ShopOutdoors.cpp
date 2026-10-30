#include "ShopAssets.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "LV/Engine/Graphics/Models/MeshBuilder.h"
#include "GarageLayout.h"

// The garage building (walls with window openings, roof, roll-up door) and the lot and street
// around it.

namespace
{
	using namespace GarageLayout;

	const glm::vec3 WHITE = { 1.f, 1.f, 1.f };
	const glm::vec3 FRAME_GREY = { 0.28f, 0.30f, 0.32f };
	const glm::vec3 TRIM_GREEN = { 0.42f, 0.50f, 0.46f };
	const glm::vec3 CEILING = { 0.45f, 0.45f, 0.47f };

	// Which version of the garage is being built: with the opening knocked through into the side bay
	// (whose left-wall windows go with it), and with the second story on top (and no roof of its own).
	struct Variant
	{
		bool bay = false;
		bool upper = false;
	};
	Variant g_variant;

	std::vector<Window> Windows()
	{
		std::vector<Window> windows;
		for (const auto& window : WINDOWS)
			if (!(g_variant.bay && window.wall == Wall::LEFT))
				windows.push_back(window);
		return windows;
	}

	void Box(MeshBuilder& builder, const glm::vec3& center, const glm::vec3& size, const glm::vec3& color = WHITE)
	{
		builder.ResetTransform().SetColor(color).AddBox(center, size);
	}

	void TurnedBox(MeshBuilder& builder, const glm::vec3& center, const glm::vec3& size, float angleAboutX, const glm::vec3& color = WHITE)
	{
		glm::mat4 transform = glm::rotate(glm::translate(glm::mat4(1.f), center), angleAboutX, { 1.f, 0.f, 0.f });
		builder.SetTransform(transform).SetColor(color).AddBox({}, size);
		builder.ResetTransform();
	}

	// One wall's footprint: which axis it runs along, where its interior face is and which way is out.
	struct WallSpan
	{
		bool alongX = true;
		float plane = 0.f;
		float outward = 1.f;
		float from = 0.f;
		float to = 0.f;
	};

	WallSpan SpanOf(Wall wall)
	{
		// The front and back walls run past the side walls to close the corners.
		float wide = HALF_WIDTH + WALL;
		switch (wall)
		{
			case Wall::BACK:	return { true, -HALF_DEPTH, -1.f, -wide, wide };
			case Wall::FRONT:	return { true, HALF_DEPTH, 1.f, -wide, wide };
			case Wall::LEFT:	return { false, -HALF_WIDTH, -1.f, -HALF_DEPTH, HALF_DEPTH };
			default:			return { false, HALF_WIDTH, 1.f, -HALF_DEPTH, HALF_DEPTH };
		}
	}

	// A box spanning u0..u1 along the wall, v0..v1 in height and d0..d1 outward from its inner face.
	void WallBox(MeshBuilder& builder, const WallSpan& span, float u0, float u1, float v0, float v1, float d0, float d1, const glm::vec3& color = WHITE)
	{
		float depth = span.plane + span.outward * (d0 + d1) * 0.5f;
		glm::vec3 center = span.alongX ? glm::vec3((u0 + u1) * 0.5f, (v0 + v1) * 0.5f, depth) : glm::vec3(depth, (v0 + v1) * 0.5f, (u0 + u1) * 0.5f);
		glm::vec3 size = span.alongX ? glm::vec3(u1 - u0, v1 - v0, d1 - d0) : glm::vec3(d1 - d0, v1 - v0, u1 - u0);
		Box(builder, center, size, color);
	}

	struct Opening
	{
		float u0, u1, v0, v1;
	};

	std::vector<Opening> OpeningsIn(Wall wall)
	{
		std::vector<Opening> openings;
		for (const auto& window : Windows())
			if (window.wall == wall)
				openings.push_back({ window.u0, window.u1, window.v0, window.v1 });
		if (wall == Wall::FRONT)
		{
			openings.push_back({ DOOR_MIN_X, DOOR_MAX_X, 0.f, DOOR_HEIGHT });
			openings.push_back({ SIDE_DOOR_MIN_X, SIDE_DOOR_MAX_X, 0.f, SIDE_DOOR_HEIGHT });
		}
		if (wall == Wall::LEFT && g_variant.bay)
			openings.push_back({ -BAY_OPENING_HALF, BAY_OPENING_HALF, 0.f, BAY_OPENING_HEIGHT });
		if (wall == Wall::RIGHT)
			openings.push_back({ HOUSE_LINK_MIN_Z, HOUSE_LINK_MAX_Z, 0.f, HOUSE_DOOR_HEIGHT });
		std::sort(openings.begin(), openings.end(), [](const Opening& a, const Opening& b) { return a.u0 < b.u0; });
		return openings;
	}

	// One layer of a wall (d0..d1 outward), solid except for its window and door openings.
	void WallLayer(MeshBuilder& builder, Wall wall, float d0, float d1, float height, const glm::vec3& color = WHITE)
	{
		auto span = SpanOf(wall);
		float cursor = span.from;
		for (const auto& opening : OpeningsIn(wall))
		{
			if (opening.u0 > cursor)
				WallBox(builder, span, cursor, opening.u0, 0.f, height, d0, d1, color);
			if (opening.v0 > 0.f)
				WallBox(builder, span, opening.u0, opening.u1, 0.f, opening.v0, d0, d1, color);
			if (opening.v1 < height)
				WallBox(builder, span, opening.u0, opening.u1, opening.v1, height, d0, d1, color);
			cursor = opening.u1;
		}
		if (cursor < span.to)
			WallBox(builder, span, cursor, span.to, 0.f, height, d0, d1, color);
	}

	constexpr Wall ALL_WALLS[] = { Wall::BACK, Wall::FRONT, Wall::LEFT, Wall::RIGHT };

	// The roof's pitch: from the top of the walls at the front and back up to the ridge.
	float RoofSlope()
	{
		return (ROOF_RIDGE_Y - (HEIGHT + WALL)) / (HALF_DEPTH + WALL);
	}

	// Two pitched slabs meeting at a ridge along X, eaves overhanging the walls by `overhang`.
	void GableRoof(MeshBuilder& builder, float length, float halfDepth, float ridgeY, float slope, float overhang, float thickness)
	{
		float angle = std::atan(slope);
		float run = halfDepth + overhang;
		float slabLength = run / std::cos(angle);
		for (float side : { 1.f, -1.f })
		{
			float z = side * run * 0.5f;
			float y = ridgeY - run * 0.5f * slope;
			glm::vec3 normal = { 0.f, std::cos(angle), side * std::sin(angle) };
			TurnedBox(builder, glm::vec3(0.f, y, z) + normal * (thickness * 0.5f), { length, thickness, slabLength }, side * angle);
		}
	}

	// Gable-end triangles closing the space under a roof, on the faces at x = +-halfLength.
	void Gables(MeshBuilder& builder, float halfLength, float halfDepth, float baseY, float ridgeY)
	{
		builder.ResetTransform().SetColor(WHITE);
		for (float side : { 1.f, -1.f })
		{
			glm::vec3 a = { side * halfLength, baseY, side * halfDepth };
			glm::vec3 b = { side * halfLength, baseY, -side * halfDepth };
			glm::vec3 c = { side * halfLength, ridgeY, 0.f };
			builder.AddFlatTriangle(a, b, c);
		}
	}

	void LoadGarage(const std::function<void(const std::string&, const MeshBuilder&)>& add)
	{
		const float outerX = HALF_WIDTH + WALL;
		const float outerZ = HALF_DEPTH + WALL;
		const float wallTop = HEIGHT + WALL;

		for (bool bay : { false, true })
			for (bool upper : { false, true })
			{
				g_variant = { bay, upper };
				// Inside: plaster walls, the painted band along the bottom, ceiling, door housing and sills.
				{
					MeshBuilder shell;
					shell.SetUVScale(0.5f);
					for (auto wall : ALL_WALLS)
					{
						WallLayer(shell, wall, 0.f, PLASTER, HEIGHT);
						// The band stops at the door.
						auto span = SpanOf(wall);
						float from = (std::max)(span.from, span.alongX ? -HALF_WIDTH : -HALF_DEPTH);
						float to = (std::min)(span.to, span.alongX ? HALF_WIDTH : HALF_DEPTH);
						if (wall == Wall::FRONT)
						{
							WallBox(shell, span, from, DOOR_MIN_X - 0.08f, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);
							WallBox(shell, span, DOOR_MAX_X + 0.08f, SIDE_DOOR_MIN_X - 0.02f, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);
							WallBox(shell, span, SIDE_DOOR_MAX_X + 0.02f, to, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);
						}
						else if (wall == Wall::LEFT && bay)
						{
							WallBox(shell, span, from, -BAY_OPENING_HALF - 0.02f, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);
							WallBox(shell, span, BAY_OPENING_HALF + 0.02f, to, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);
						}
						else if (wall == Wall::RIGHT)
						{
							WallBox(shell, span, from, HOUSE_LINK_MIN_Z - 0.06f, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);
							WallBox(shell, span, HOUSE_LINK_MAX_Z + 0.06f, to, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);
						}
						else
							WallBox(shell, span, from, to, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);

						for (const auto& window : Windows())
							if (window.wall == wall)
								WallBox(shell, span, window.u0 - 0.06f, window.u1 + 0.06f, window.v0 - 0.04f, window.v0, -0.06f, PLASTER, { 0.85f, 0.85f, 0.82f });
					}
					// The ceiling, with the stairwell up to the loft cut through it when there is one.
					if (upper)
					{
						Box(shell, { 0.f, HEIGHT + WALL * 0.5f, (INSIDE_STAIR_NEAR_Z - outerZ) * 0.5f }, { outerX * 2.f, WALL, INSIDE_STAIR_NEAR_Z + outerZ }, CEILING);
						Box(shell, { (STAIRWELL_MAX_X + outerX) * 0.5f, HEIGHT + WALL * 0.5f, (INSIDE_STAIR_NEAR_Z + outerZ) * 0.5f }, { outerX - STAIRWELL_MAX_X, WALL, outerZ - INSIDE_STAIR_NEAR_Z }, CEILING);
					}
					else
						Box(shell, { 0.f, HEIGHT + WALL * 0.5f, 0.f }, { outerX * 2.f, WALL, outerZ * 2.f }, CEILING);

					// Roll-up door: the coil housing over the opening and the guide rails either side.
					auto front = SpanOf(Wall::FRONT);
					WallBox(shell, front, DOOR_MIN_X - 0.12f, DOOR_MAX_X + 0.12f, DOOR_HEIGHT, DOOR_HEIGHT + 0.42f, -0.42f, 0.f, FRAME_GREY);
					for (float u : { DOOR_MIN_X - 0.05f, DOOR_MAX_X + 0.05f })
						WallBox(shell, front, u - 0.05f, u + 0.05f, 0.f, DOOR_HEIGHT, -0.08f, 0.f, FRAME_GREY);
					add(ShopModels::GarageVariant(ShopModels::WALLS, bay, upper), shell);
				}

				// Outside: corrugated siding and the gable ends.
				{
					MeshBuilder siding;
					siding.SetUVScale(0.5f);
					for (auto wall : ALL_WALLS)
						WallLayer(siding, wall, PLASTER, WALL, wallTop);
					if (!upper)
						Gables(siding, outerX, outerZ, wallTop, ROOF_RIDGE_Y);
					add(ShopModels::GarageVariant(ShopModels::GARAGE_SIDING, bay, upper), siding);
				}
				// One roof and one door: the variants only change the walls.
				if (!bay && !upper)
				{
					MeshBuilder roof;
					roof.SetUVScale(0.5f);
					GableRoof(roof, (outerX + 0.3f) * 2.f, outerZ, ROOF_RIDGE_Y, RoofSlope(), ROOF_OVERHANG, 0.1f);
					add(ShopModels::GARAGE_ROOF, roof);
				}

				// Window and door frames, corner boards, fascia and ridge cap.
				{
					MeshBuilder frames;
					frames.SetUVScale(2.f);
					constexpr float BAR = 0.06f;
					for (auto wall : ALL_WALLS)
					{
						auto span = SpanOf(wall);
						for (const auto& window : Windows())
						{
							if (window.wall != wall)
								continue;
							WallBox(frames, span, window.u0 - BAR, window.u1 + BAR, window.v0 - BAR, window.v0, WALL, WALL + 0.03f, FRAME_GREY);
							WallBox(frames, span, window.u0 - BAR, window.u1 + BAR, window.v1, window.v1 + BAR, WALL, WALL + 0.03f, FRAME_GREY);
							WallBox(frames, span, window.u0 - BAR, window.u0, window.v0, window.v1, WALL, WALL + 0.03f, FRAME_GREY);
							WallBox(frames, span, window.u1, window.u1 + BAR, window.v0, window.v1, WALL, WALL + 0.03f, FRAME_GREY);
							// Wide windows get a mullion down the middle.
							if (window.u1 - window.u0 > 2.2f)
							{
								float middle = (window.u0 + window.u1) * 0.5f;
								WallBox(frames, span, middle - 0.025f, middle + 0.025f, window.v0, window.v1, 0.03f, 0.09f, FRAME_GREY);
							}
						}
					}
					auto front = SpanOf(Wall::FRONT);
					WallBox(frames, front, DOOR_MIN_X - 0.1f, DOOR_MAX_X + 0.1f, DOOR_HEIGHT, DOOR_HEIGHT + 0.1f, WALL, WALL + 0.04f, FRAME_GREY);
					for (float u : { DOOR_MIN_X - 0.1f, DOOR_MAX_X })
						WallBox(frames, front, u, u + 0.1f, 0.f, DOOR_HEIGHT, WALL, WALL + 0.04f, FRAME_GREY);
					// The people door: its frame, and the door itself standing open against the inside of the wall.
					WallBox(frames, front, SIDE_DOOR_MIN_X - 0.08f, SIDE_DOOR_MAX_X + 0.08f, SIDE_DOOR_HEIGHT, SIDE_DOOR_HEIGHT + 0.08f, WALL, WALL + 0.03f, FRAME_GREY);
					for (float u : { SIDE_DOOR_MIN_X - 0.08f, SIDE_DOOR_MAX_X })
						WallBox(frames, front, u, u + 0.08f, 0.f, SIDE_DOOR_HEIGHT, WALL, WALL + 0.03f, FRAME_GREY);
					Box(frames, { SIDE_DOOR_MIN_X + 0.02f, SIDE_DOOR_HEIGHT * 0.5f - 0.02f, HALF_DEPTH - 0.45f }, { 0.045f, SIDE_DOOR_HEIGHT - 0.04f, 0.9f }, { 0.42f, 0.5f, 0.46f });
					// The door into the house: a casing round it on this side (the house trims its own).
					WallBox(frames, SpanOf(Wall::RIGHT), HOUSE_LINK_MIN_Z - 0.06f, HOUSE_LINK_MAX_Z + 0.06f, HOUSE_DOOR_HEIGHT, HOUSE_DOOR_HEIGHT + 0.06f, -0.02f, 0.f, FRAME_GREY);
					for (float u : { HOUSE_LINK_MIN_Z - 0.06f, HOUSE_LINK_MAX_Z })
						WallBox(frames, SpanOf(Wall::RIGHT), u, u + 0.06f, 0.f, HOUSE_DOOR_HEIGHT, -0.02f, 0.f, FRAME_GREY);

					for (float x : { -outerX, outerX })
						for (float z : { -outerZ, outerZ })
							Box(frames, { x, wallTop * 0.5f, z }, { 0.16f, wallTop, 0.16f }, FRAME_GREY);

					if (!upper)
					{
					float roofLength = (outerX + 0.3f) * 2.f;
					float eaveZ = outerZ + ROOF_OVERHANG;
					float eaveY = ROOF_RIDGE_Y - eaveZ * RoofSlope();
					for (float side : { 1.f, -1.f })
						Box(frames, { 0.f, eaveY, side * eaveZ }, { roofLength, 0.2f, 0.04f }, FRAME_GREY);
					Box(frames, { 0.f, ROOF_RIDGE_Y + 0.12f, 0.f }, { roofLength, 0.06f, 0.32f }, FRAME_GREY);
					}
					add(ShopModels::GarageVariant(ShopModels::GARAGE_FRAMES, bay, upper), frames);
				}
				{
					MeshBuilder glass;
					for (auto wall : ALL_WALLS)
						for (const auto& window : Windows())
							if (window.wall == wall)
								WallBox(glass, SpanOf(wall), window.u0, window.u1, window.v0, window.v1, 0.055f, 0.065f, { 0.75f, 0.85f, 0.9f });
					add(ShopModels::GarageVariant(ShopModels::WINDOW_GLASS, bay, upper), glass);
				}

				// Hangs down from its top edge, so it can be rolled up by shrinking it in Y.
				if (!bay && !upper)
				{
					MeshBuilder door;
					door.SetUVScale(1.f);
					float width = DOOR_MAX_X - DOOR_MIN_X;
					Box(door, { 0.f, -DOOR_HEIGHT * 0.5f, 0.f }, { width, DOOR_HEIGHT, 0.04f }, { 0.62f, 0.64f, 0.67f });
					for (int slat = 0; slat < 14; slat++)
					{
						float y = -DOOR_HEIGHT + 0.1f + slat * 0.2f;
						for (float side : { 1.f, -1.f })
							Box(door, { 0.f, y, side * 0.025f }, { width, 0.02f, 0.01f }, { 0.46f, 0.48f, 0.51f });
					}
					Box(door, { 0.f, -DOOR_HEIGHT + 0.04f, 0.f }, { width, 0.08f, 0.07f }, { 0.25f, 0.26f, 0.28f });
					Box(door, { 0.f, -DOOR_HEIGHT + 0.9f, 0.05f }, { 0.3f, 0.04f, 0.04f }, { 0.25f, 0.26f, 0.28f });
					add(ShopModels::ROLLUP_DOOR, door);
				}
			}
		g_variant = {};
	}

	void LoadLot(const std::function<void(const std::string&, const MeshBuilder&)>& add)
	{
		{
			MeshBuilder sky;
			sky.AddSphere({}, 1.f, 16, 32, true);
			add(ShopModels::SKY_DOME, sky);
		}
		{
			MeshBuilder ground;
			ground.SetUVScale(0.3f);
			Box(ground, { 0.f, -0.055f, 0.f }, { 400.f, 0.1f, 400.f });
			add(ShopModels::GROUND, ground);
		}
		{
			// The drive, sidewalks, paths and patio (see GarageLayout::PAVED).
			MeshBuilder paving;
			paving.SetUVScale(0.5f);
			for (const auto& paved : PAVED)
			{
				glm::vec2 middle = (paved.min + paved.max) * 0.5f, size = paved.max - paved.min;
				Box(paving, { middle.x, -0.05f, middle.y }, { size.x, 0.1f, size.y });
			}
			add(ShopModels::PAVING, paving);
		}
		{
			MeshBuilder road;
			road.SetUVScale(0.25f);
			Box(road, { 0.f, -0.05f, (STREET_NEAR_Z + STREET_FAR_Z) * 0.5f }, { STREET_HALF_LENGTH * 2.f, 0.1f, STREET_FAR_Z - STREET_NEAR_Z });
			add(ShopModels::ROAD, road);
		}
		{
			MeshBuilder markings;
			const glm::vec3 yellow = { 0.85f, 0.68f, 0.18f };
			const glm::vec3 white = { 0.85f, 0.85f, 0.82f };
			float middle = (STREET_NEAR_Z + STREET_FAR_Z) * 0.5f;
			for (float x = -STREET_HALF_LENGTH; x < STREET_HALF_LENGTH; x += 6.f)
				Box(markings, { x + 1.5f, 0.002f, middle }, { 3.f, 0.004f, 0.12f }, yellow);
			for (float z : { STREET_NEAR_Z + 0.25f, STREET_FAR_Z - 0.25f })
				Box(markings, { 0.f, 0.002f, z }, { STREET_HALF_LENGTH * 2.f, 0.004f, 0.1f }, white);
			add(ShopModels::ROAD_MARKINGS, markings);
		}
		{
			// Board fences round the lots (see GarageLayout::FENCES), posts every 2.5 m.
			MeshBuilder fence;
			fence.SetUVScale(1.f);
			const glm::vec3 boards = { 0.78f, 0.72f, 0.64f };
			const glm::vec3 posts = { 0.62f, 0.56f, 0.48f };
			constexpr float HEIGHT_BOARDS = 1.6f;
			auto run = [&](glm::vec2 from, glm::vec2 to) {
				glm::vec2 delta = to - from;
				float length = glm::length(delta);
				int panels = (std::max)(1, static_cast<int>(std::round(length / 2.5f)));
				glm::vec2 step = delta / static_cast<float>(panels);
				bool alongX = std::abs(delta.x) > std::abs(delta.y);
				for (int i = 0; i < panels; i++)
				{
					glm::vec2 center = from + step * (i + 0.5f);
					glm::vec3 size = alongX ? glm::vec3(std::abs(step.x), HEIGHT_BOARDS, 0.04f) : glm::vec3(0.04f, HEIGHT_BOARDS, std::abs(step.y));
					Box(fence, { center.x, HEIGHT_BOARDS * 0.5f + 0.05f, center.y }, size, boards);
				}
				for (int i = 0; i <= panels; i++)
				{
					glm::vec2 post = from + step * static_cast<float>(i);
					Box(fence, { post.x, 0.9f, post.y }, { 0.1f, 1.8f, 0.1f }, posts);
				}
			};
			for (const auto& fenceRun : FENCES)
				run(fenceRun.from, fenceRun.to);
			add(ShopModels::FENCE, fence);
		}
		{
			MeshBuilder trunk;
			trunk.SetUVScale(2.f);
			trunk.SetColor(WHITE).AddCylinder({ 0.f, 1.6f, 0.f }, 0.17f, 3.2f, 10);
			trunk.SetTransform(glm::rotate(glm::translate(glm::mat4(1.f), { 0.35f, 2.6f, 0.f }), -0.7f, { 0.f, 0.f, 1.f }));
			trunk.AddCylinder({}, 0.07f, 1.2f, 8);
			trunk.ResetTransform();
			add(ShopModels::TREE_TRUNK, trunk);

			MeshBuilder crown;
			crown.SetUVScale(0.6f);
			crown.SetColor(WHITE);
			crown.AddSphere({ 0.f, 4.3f, 0.f }, 1.9f, 7, 10);
			crown.AddSphere({ 1.0f, 3.7f, 0.4f }, 1.35f, 6, 9);
			crown.AddSphere({ -0.9f, 3.9f, -0.5f }, 1.45f, 6, 9);
			crown.AddSphere({ 0.2f, 5.3f, -0.2f }, 1.25f, 6, 9);
			add(ShopModels::TREE_CROWN, crown);

			MeshBuilder pine;
			pine.SetUVScale(0.6f);
			pine.SetColor({ 0.75f, 0.85f, 0.8f });
			pine.AddCone({ 0.f, 1.1f, 0.f }, 1.9f, 3.0f, 10);
			pine.AddCone({ 0.f, 2.7f, 0.f }, 1.5f, 2.7f, 10);
			pine.AddCone({ 0.f, 4.2f, 0.f }, 1.05f, 2.4f, 10);
			add(ShopModels::PINE_CROWN, pine);
		}
		{
			// A house: front on +Z, centred on its footprint.
			constexpr float HALF_LENGTH = 5.f;
			constexpr float HALF_DEPTH_HOUSE = 3.5f;
			constexpr float WALL_HEIGHT = 3.f;
			constexpr float RIDGE = 5.1f;
			float slope = (RIDGE - WALL_HEIGHT) / HALF_DEPTH_HOUSE;

			MeshBuilder walls;
			walls.SetUVScale(0.6f);
			Box(walls, { 0.f, WALL_HEIGHT * 0.5f, 0.f }, { HALF_LENGTH * 2.f, WALL_HEIGHT, HALF_DEPTH_HOUSE * 2.f });
			Gables(walls, HALF_LENGTH, HALF_DEPTH_HOUSE, WALL_HEIGHT, RIDGE);
			Box(walls, { 2.8f, 4.2f, -1.f }, { 0.6f, 1.6f, 0.6f });	// chimney
			add(ShopModels::HOUSE_WALLS, walls);

			MeshBuilder roof;
			roof.SetUVScale(0.6f);
			GableRoof(roof, HALF_LENGTH * 2.f + 0.6f, HALF_DEPTH_HOUSE, RIDGE, slope, 0.45f, 0.12f);
			add(ShopModels::HOUSE_ROOF, roof);

			MeshBuilder trim;
			const glm::vec3 pane = { 0.16f, 0.20f, 0.25f };
			const glm::vec3 frame = { 0.92f, 0.92f, 0.88f };
			for (float side : { 1.f, -1.f })
				for (float x : { -3.2f, 3.2f, -0.f })
				{
					if (side > 0.f && x == 0.f)
						continue;	// the front door goes there
					float z = side * (HALF_DEPTH_HOUSE + 0.02f);
					Box(trim, { x, 1.65f, z }, { 1.5f, 1.3f, 0.04f }, frame);
					Box(trim, { x, 1.65f, z + side * 0.01f }, { 1.3f, 1.1f, 0.04f }, pane);
				}
			Box(trim, { 0.f, 1.1f, HALF_DEPTH_HOUSE + 0.02f }, { 1.2f, 2.2f, 0.04f }, frame);
			Box(trim, { 0.f, 1.05f, HALF_DEPTH_HOUSE + 0.03f }, { 1.0f, 2.1f, 0.04f }, { 0.36f, 0.16f, 0.12f });
			Box(trim, { 0.f, 0.08f, HALF_DEPTH_HOUSE + 0.6f }, { 2.0f, 0.16f, 1.2f }, { 0.6f, 0.6f, 0.58f });	// step
			add(ShopModels::HOUSE_TRIM, trim);
		}
		{
			// Pole with an arm reaching over the road (+Z); the glowing head is its own model.
			MeshBuilder pole;
			const glm::vec3 grey = { 0.42f, 0.44f, 0.46f };
			pole.SetColor(grey).AddCylinder({ 0.f, STREET_LIGHT_HEIGHT * 0.5f, 0.f }, 0.08f, STREET_LIGHT_HEIGHT, 10);
			pole.SetColor(grey).AddCylinder({ 0.f, 0.3f, 0.f }, 0.16f, 0.6f, 10);
			Box(pole, { 0.f, STREET_LIGHT_HEIGHT - 0.05f, 0.6f }, { 0.08f, 0.08f, 1.2f }, grey);
			Box(pole, { 0.f, STREET_LIGHT_HEIGHT - 0.07f, 1.15f }, { 0.34f, 0.1f, 0.6f }, grey);
			add(ShopModels::STREET_LIGHT, pole);

			MeshBuilder head;
			Box(head, {}, { 0.28f, 0.04f, 0.5f }, { 1.f, 0.95f, 0.82f });
			add(ShopModels::LAMP_HEAD, head);
		}
		{
			// A plain hatchback, nose along +X.
			MeshBuilder body;
			body.SetUVScale(2.f);
			Box(body, { 0.f, 0.62f, 0.f }, { 4.2f, 0.62f, 1.76f });
			Box(body, { -0.35f, 1.18f, 0.f }, { 2.3f, 0.52f, 1.6f });
			Box(body, { 1.45f, 0.95f, 0.f }, { 1.3f, 0.06f, 1.7f });
			add(ShopModels::CAR_BODY, body);

			MeshBuilder trim;
			const glm::vec3 tyre = { 0.12f, 0.12f, 0.12f };
			const glm::vec3 glassColor = { 0.10f, 0.13f, 0.16f };
			for (float x : { -1.35f, 1.35f })
				for (float z : { -0.8f, 0.8f })
				{
					trim.SetTransform(glm::rotate(glm::translate(glm::mat4(1.f), { x, 0.33f, z }), glm::half_pi<float>(), { 1.f, 0.f, 0.f }));
					trim.SetColor(tyre).AddCylinder({}, 0.33f, 0.22f, 16);
					trim.SetColor({ 0.6f, 0.6f, 0.62f }).AddCylinder({}, 0.2f, 0.24f, 12);
				}
			trim.ResetTransform();
			Box(trim, { 0.79f, 1.18f, 0.f }, { 0.04f, 0.42f, 1.5f }, glassColor);		// windscreen
			Box(trim, { -1.51f, 1.18f, 0.f }, { 0.04f, 0.42f, 1.5f }, glassColor);
			for (float z : { -0.81f, 0.81f })
				Box(trim, { -0.35f, 1.2f, z }, { 2.1f, 0.38f, 0.02f }, glassColor);
			for (float x : { -2.12f, 2.12f })
				Box(trim, { x, 0.45f, 0.f }, { 0.08f, 0.18f, 1.7f }, { 0.2f, 0.2f, 0.2f });	// bumpers
			add(ShopModels::CAR_TRIM, trim);
		}
		{
			MeshBuilder dumpster;
			const glm::vec3 green = { 0.25f, 0.42f, 0.30f };
			Box(dumpster, { 0.f, 0.62f, 0.f }, { 1.9f, 1.04f, 1.15f }, green);
			Box(dumpster, { 0.f, 1.16f, 0.f }, { 1.95f, 0.05f, 1.2f }, { 0.15f, 0.15f, 0.16f });
			for (float x : { -0.8f, 0.8f })
				for (float z : { -0.45f, 0.45f })
					dumpster.SetColor({ 0.1f, 0.1f, 0.1f }).AddCylinder({ x, 0.05f, z }, 0.05f, 0.1f, 8);
			add(ShopModels::DUMPSTER, dumpster);

			MeshBuilder drum;
			drum.SetUVScale(2.f);
			drum.SetColor(WHITE).AddCylinder({ 0.f, 0.44f, 0.f }, 0.29f, 0.88f, 16);
			for (float y : { 0.3f, 0.6f })
				drum.SetColor({ 0.8f, 0.8f, 0.8f }).AddCylinder({ 0.f, y, 0.f }, 0.3f, 0.03f, 16);
			add(ShopModels::OIL_DRUM, drum);
		}
	}

	// A box turned about Z (for stair stringers), centred on center.
	void ZTurnedBox(MeshBuilder& builder, const glm::vec3& center, const glm::vec3& size, float angle, const glm::vec3& color)
	{
		glm::mat4 transform = glm::rotate(glm::translate(glm::mat4(1.f), center), angle, { 0.f, 0.f, 1.f });
		builder.SetTransform(transform).SetColor(color).AddBox({}, size);
		builder.ResetTransform();
	}

	// Openings in the upstairs walls: the door off the stair landing at the back, windows elsewhere.
	// v is height above the upstairs floor.
	std::vector<Opening> UpperOpeningsIn(Wall wall)
	{
		switch (wall)
		{
			case Wall::BACK:	return { { UPPER_DOOR_MIN_X, UPPER_DOOR_MAX_X, 0.f, UPPER_DOOR_HEIGHT } };
			case Wall::FRONT:	return { { -5.2f, -3.2f, 0.9f, 1.9f }, { 3.2f, 5.2f, 0.9f, 1.9f } };
			case Wall::LEFT:	return { { -1.f, 1.f, 0.9f, 1.9f } };
			default:			return { { -1.f, 1.f, 0.9f, 1.9f } };
		}
	}

	// One layer of an upstairs wall, from baseY up height, with its openings left out.
	void UpperWallLayer(MeshBuilder& builder, Wall wall, float d0, float d1, float baseY, float height, const glm::vec3& color = WHITE)
	{
		auto span = SpanOf(wall);
		float cursor = span.from;
		for (const auto& opening : UpperOpeningsIn(wall))
		{
			WallBox(builder, span, cursor, opening.u0, baseY, baseY + height, d0, d1, color);
			if (opening.v0 > 0.f)
				WallBox(builder, span, opening.u0, opening.u1, baseY, baseY + opening.v0, d0, d1, color);
			WallBox(builder, span, opening.u0, opening.u1, baseY + opening.v1, baseY + height, d0, d1, color);
			cursor = opening.u1;
		}
		WallBox(builder, span, cursor, span.to, baseY, baseY + height, d0, d1, color);
	}

	void LoadUpgrades(const std::function<void(const std::string&, const MeshBuilder&)>& add)
	{
		const float outerX = HALF_WIDTH + WALL;
		const float outerZ = HALF_DEPTH + WALL;

		// The side bay: three new walls round the opening in the old left wall, a ceiling, a flat roof.
		{
			const float right = -outerX;				// the bay butts against the garage's left wall
			const float left = BAY_MIN_X;
			const float middleX = (left + right) * 0.5f;
			const float width = right - left;
			// The front wall, x0..x1 along it, z0..z1 through it, up to top: split round the bay's own
			// roll-up door (and the band along the bottom stops either side of it).
			auto frontWall = [](MeshBuilder& builder, float x0, float x1, float z0, float z1, float top, const glm::vec3& color, bool band) {
				float height = band ? 1.f : top;
				for (auto [from, to] : { std::pair{ x0, BAY_DOOR_MIN_X - (band ? 0.08f : 0.f) }, std::pair{ BAY_DOOR_MAX_X + (band ? 0.08f : 0.f), x1 } })
					Box(builder, { (from + to) * 0.5f, height * 0.5f, (z0 + z1) * 0.5f }, { to - from, height, z1 - z0 }, color);
				if (!band)
					Box(builder, { (BAY_DOOR_MIN_X + BAY_DOOR_MAX_X) * 0.5f, (BAY_DOOR_HEIGHT + top) * 0.5f, (z0 + z1) * 0.5f },
						{ BAY_DOOR_MAX_X - BAY_DOOR_MIN_X, top - BAY_DOOR_HEIGHT, z1 - z0 }, color);
			};
			MeshBuilder shell;
			shell.SetUVScale(0.5f);
			Box(shell, { middleX, BAY_HEIGHT * 0.5f, -(BAY_HALF_DEPTH + PLASTER * 0.5f) }, { width, BAY_HEIGHT, PLASTER });
			Box(shell, { middleX, 0.5f, -(BAY_HALF_DEPTH - 0.01f) }, { width, 1.f, 0.02f }, TRIM_GREEN);
			frontWall(shell, left, right, BAY_HALF_DEPTH, BAY_HALF_DEPTH + PLASTER, BAY_HEIGHT, WHITE, false);
			frontWall(shell, left, right, BAY_HALF_DEPTH - 0.02f, BAY_HALF_DEPTH, 1.f, TRIM_GREEN, true);
			// Its roll-up door: the coil housing over the opening and the guide rails either side.
			Box(shell, { (BAY_DOOR_MIN_X + BAY_DOOR_MAX_X) * 0.5f, BAY_DOOR_HEIGHT + 0.2f, BAY_HALF_DEPTH - 0.2f },
				{ BAY_DOOR_MAX_X - BAY_DOOR_MIN_X + 0.24f, 0.4f, 0.4f }, FRAME_GREY);
			for (float x : { BAY_DOOR_MIN_X - 0.05f, BAY_DOOR_MAX_X + 0.05f })
				Box(shell, { x, BAY_DOOR_HEIGHT * 0.5f, BAY_HALF_DEPTH - 0.04f }, { 0.1f, BAY_DOOR_HEIGHT, 0.08f }, FRAME_GREY);
			Box(shell, { left - PLASTER * 0.5f, BAY_HEIGHT * 0.5f, 0.f }, { PLASTER, BAY_HEIGHT, BAY_HALF_DEPTH * 2.f });
			Box(shell, { left + 0.01f, 0.5f, 0.f }, { 0.02f, 1.f, BAY_HALF_DEPTH * 2.f }, TRIM_GREEN);
			Box(shell, { middleX, BAY_HEIGHT + WALL * 0.5f, 0.f }, { width + WALL, WALL, (BAY_HALF_DEPTH + WALL) * 2.f }, CEILING);
			add(ShopModels::BAY_SHELL, shell);

			MeshBuilder outside;
			outside.SetUVScale(0.5f);
			float top = BAY_HEIGHT + WALL;
			Box(outside, { middleX - WALL * 0.5f, top * 0.5f, -(BAY_HALF_DEPTH + (PLASTER + WALL) * 0.5f) }, { width + WALL, top, WALL - PLASTER });
			frontWall(outside, left - WALL, right, BAY_HALF_DEPTH + PLASTER, BAY_HALF_DEPTH + WALL, top, WHITE, false);
			// A frame round the door outside.
			Box(outside, { (BAY_DOOR_MIN_X + BAY_DOOR_MAX_X) * 0.5f, BAY_DOOR_HEIGHT + 0.05f, BAY_HALF_DEPTH + WALL + 0.02f },
				{ BAY_DOOR_MAX_X - BAY_DOOR_MIN_X + 0.2f, 0.1f, 0.04f }, FRAME_GREY);
			for (float x : { BAY_DOOR_MIN_X - 0.05f, BAY_DOOR_MAX_X + 0.05f })
				Box(outside, { x, BAY_DOOR_HEIGHT * 0.5f, BAY_HALF_DEPTH + WALL + 0.02f }, { 0.1f, BAY_DOOR_HEIGHT, 0.04f }, FRAME_GREY);
			Box(outside, { left - (PLASTER + WALL) * 0.5f, top * 0.5f, 0.f }, { WALL - PLASTER, top, (BAY_HALF_DEPTH + WALL) * 2.f });
			// A flat roof just proud of the walls, with a fascia and corner boards.
			Box(outside, { middleX - 0.2f, top + 0.05f, 0.f }, { width + 0.4f + WALL, 0.1f, (BAY_HALF_DEPTH + WALL + 0.3f) * 2.f }, { 0.32f, 0.3f, 0.3f });
			for (float side : { -1.f, 1.f })
			{
				Box(outside, { middleX - 0.2f, top + 0.05f, side * (BAY_HALF_DEPTH + WALL + 0.31f) }, { width + 0.4f + WALL, 0.2f, 0.04f }, FRAME_GREY);
				Box(outside, { left - WALL, top * 0.5f, side * (BAY_HALF_DEPTH + WALL) }, { 0.16f, top, 0.16f }, FRAME_GREY);
			}
			add(ShopModels::BAY_EXTERIOR, outside);

			MeshBuilder floor;
			floor.SetUVScale(0.5f);
			Box(floor, { middleX, -0.048f, 0.f }, { width, 0.1f, BAY_HALF_DEPTH * 2.f });
			add(ShopModels::BAY_FLOOR, floor);

			MeshBuilder drive;
			drive.SetUVScale(0.5f);
			glm::vec2 middle = (BAY_DRIVE.min + BAY_DRIVE.max) * 0.5f, size = BAY_DRIVE.max - BAY_DRIVE.min;
			Box(drive, { middle.x, -0.05f, middle.y }, { size.x, 0.1f, size.y });
			add(ShopModels::BAY_DRIVE, drive);
		}

		// The second story: a plywood floor on the old ceiling, walls, its own ceiling, the roof
		// moved up on top, and stairs up the outside of the back wall.
		{
			const float base = HEIGHT + WALL;			// where the garage walls stop
			const float wallHeight = UPPER_FLOOR_Y - base + UPPER_HEIGHT;
			const float wallTop = base + wallHeight + WALL;
			MeshBuilder shell;
			shell.SetUVScale(0.5f);
			// The plywood floor, round the stairwell up from the shop.
			const glm::vec3 plywood = { 0.78f, 0.66f, 0.48f };
			const float floorY = (base + UPPER_FLOOR_Y) * 0.5f, floorThickness = UPPER_FLOOR_Y - base;
			Box(shell, { 0.f, floorY, (INSIDE_STAIR_NEAR_Z - HALF_DEPTH) * 0.5f }, { HALF_WIDTH * 2.f, floorThickness, INSIDE_STAIR_NEAR_Z + HALF_DEPTH }, plywood);
			Box(shell, { (STAIRWELL_MAX_X + HALF_WIDTH) * 0.5f, floorY, (INSIDE_STAIR_NEAR_Z + HALF_DEPTH) * 0.5f },
				{ HALF_WIDTH - STAIRWELL_MAX_X, floorThickness, HALF_DEPTH - INSIDE_STAIR_NEAR_Z }, plywood);
			for (auto wall : ALL_WALLS)
				UpperWallLayer(shell, wall, 0.f, PLASTER, base, wallHeight);
			Box(shell, { 0.f, base + wallHeight + WALL * 0.5f, 0.f }, { outerX * 2.f, WALL, outerZ * 2.f }, CEILING);
			add(ShopModels::UPPER_SHELL, shell);

			MeshBuilder outside;
			outside.SetUVScale(0.5f);
			for (auto wall : ALL_WALLS)
				UpperWallLayer(outside, wall, PLASTER, WALL, base, wallHeight + WALL);
			float slope = RoofSlope();
			float ridge = wallTop + outerZ * slope;
			Gables(outside, outerX, outerZ, wallTop, ridge);
			add(ShopModels::UPPER_EXTERIOR, outside);
			MeshBuilder roof;
			roof.SetUVScale(0.5f);
			GableRoof(roof, (outerX + 0.3f) * 2.f, outerZ, ridge, slope, ROOF_OVERHANG, 0.1f);
			add(ShopModels::UPPER_ROOF, roof);
			// Frames round the openings, corner boards, fascia and ridge cap.
			MeshBuilder frames;
			frames.SetUVScale(2.f);
			constexpr float BAR = 0.06f;
			for (auto wall : ALL_WALLS)
			{
				auto span = SpanOf(wall);
				for (const auto& opening : UpperOpeningsIn(wall))
				{
					float v0 = base + opening.v0, v1 = base + opening.v1;
					if (opening.v0 > 0.f)
						WallBox(frames, span, opening.u0 - BAR, opening.u1 + BAR, v0 - BAR, v0, WALL, WALL + 0.03f, FRAME_GREY);
					WallBox(frames, span, opening.u0 - BAR, opening.u1 + BAR, v1, v1 + BAR, WALL, WALL + 0.03f, FRAME_GREY);
					WallBox(frames, span, opening.u0 - BAR, opening.u0, v0, v1, WALL, WALL + 0.03f, FRAME_GREY);
					WallBox(frames, span, opening.u1, opening.u1 + BAR, v0, v1, WALL, WALL + 0.03f, FRAME_GREY);
				}
			}
			for (float x : { -outerX, outerX })
				for (float z : { -outerZ, outerZ })
					Box(frames, { x, (base + wallTop) * 0.5f, z }, { 0.16f, wallTop - base, 0.16f }, FRAME_GREY);
			float roofLength = (outerX + 0.3f) * 2.f;
			float eaveZ = outerZ + ROOF_OVERHANG;
			for (float side : { 1.f, -1.f })
				Box(frames, { 0.f, ridge - eaveZ * slope, side * eaveZ }, { roofLength, 0.2f, 0.04f }, FRAME_GREY);
			Box(frames, { 0.f, ridge + 0.12f, 0.f }, { roofLength, 0.06f, 0.32f }, FRAME_GREY);
			add(ShopModels::UPPER_FRAMES, frames);

			MeshBuilder glass;
			for (auto wall : { Wall::FRONT, Wall::LEFT, Wall::RIGHT })
				for (const auto& opening : UpperOpeningsIn(wall))
					WallBox(glass, SpanOf(wall), opening.u0, opening.u1, base + opening.v0, base + opening.v1, 0.055f, 0.065f, { 0.75f, 0.85f, 0.9f });
			add(ShopModels::UPPER_GLASS, glass);
		}

		// Steel stairs up the back wall: treads on two stringers, a landing on posts at the top, and
		// a handrail along the open side.
		{
			const glm::vec3 steel = { 0.36f, 0.38f, 0.4f };
			const glm::vec3 tread = { 0.5f, 0.52f, 0.54f };
			float run = (STAIR_BOTTOM_X - STAIR_TOP_X) / STAIR_STEPS;
			float rise = UPPER_FLOOR_Y / STAIR_STEPS;
			float middleZ = (STAIR_NEAR_Z + STAIR_FAR_Z) * 0.5f;
			float width = STAIR_NEAR_Z - STAIR_FAR_Z;
			MeshBuilder stairs;
			stairs.SetUVScale(2.f);
			for (int step = 0; step < STAIR_STEPS; step++)
			{
				float x = STAIR_BOTTOM_X - (step + 0.5f) * run;
				float top = (step + 1) * rise;
				Box(stairs, { x, top - 0.02f, middleZ }, { run + 0.02f, 0.04f, width - 0.08f }, tread);
			}
			float length = std::sqrt((STAIR_BOTTOM_X - STAIR_TOP_X) * (STAIR_BOTTOM_X - STAIR_TOP_X) + UPPER_FLOOR_Y * UPPER_FLOOR_Y);
			float angle = std::atan2(UPPER_FLOOR_Y, STAIR_BOTTOM_X - STAIR_TOP_X);
			glm::vec3 middle = { (STAIR_BOTTOM_X + STAIR_TOP_X) * 0.5f, UPPER_FLOOR_Y * 0.5f - 0.08f, 0.f };
			for (float z : { STAIR_NEAR_Z - 0.03f, STAIR_FAR_Z + 0.03f })
				ZTurnedBox(stairs, { middle.x, middle.y, z }, { length, 0.22f, 0.05f }, -angle, steel);

			float landingX = (STAIR_TOP_X + LANDING_END_X) * 0.5f;
			Box(stairs, { landingX, UPPER_FLOOR_Y - 0.05f, middleZ }, { STAIR_TOP_X - LANDING_END_X, 0.1f, width }, tread);
			for (float x : { STAIR_TOP_X - 0.05f, LANDING_END_X + 0.05f })
				Box(stairs, { x, UPPER_FLOOR_Y * 0.5f, STAIR_FAR_Z + 0.05f }, { 0.08f, UPPER_FLOOR_Y, 0.08f }, steel);

			// Handrail along the open side, posts every few steps, and round the landing.
			for (int step = 0; step <= STAIR_STEPS; step += 4)
			{
				float x = STAIR_BOTTOM_X - step * run;
				float y = step * rise;
				Box(stairs, { x, y + 0.5f, STAIR_FAR_Z + 0.03f }, { 0.04f, 1.f, 0.04f }, steel);
			}
			ZTurnedBox(stairs, { middle.x, UPPER_FLOOR_Y * 0.5f + 1.f, STAIR_FAR_Z + 0.03f }, { length, 0.05f, 0.05f }, -angle, steel);
			Box(stairs, { landingX, UPPER_FLOOR_Y + 1.f, STAIR_FAR_Z + 0.03f }, { STAIR_TOP_X - LANDING_END_X, 0.05f, 0.05f }, steel);
			Box(stairs, { LANDING_END_X + 0.03f, UPPER_FLOOR_Y + 0.5f, middleZ }, { 0.05f, 1.f, width }, steel);
			add(ShopModels::OUTSIDE_STAIRS, stairs);
		}

		// The steep stairs inside, up the front wall to the stairwell: wooden treads on steel stringers,
		// a handrail up the open side, and a railing round the stairwell upstairs.
		{
			const glm::vec3 steel = { 0.36f, 0.38f, 0.4f };
			const glm::vec3 tread = { 0.72f, 0.58f, 0.4f };
			float run = (INSIDE_STAIR_BOTTOM_X - INSIDE_STAIR_TOP_X) / INSIDE_STAIR_STEPS;
			float rise = UPPER_FLOOR_Y / INSIDE_STAIR_STEPS;
			float middleZ = (INSIDE_STAIR_NEAR_Z + HALF_DEPTH) * 0.5f;
			float width = HALF_DEPTH - INSIDE_STAIR_NEAR_Z;
			MeshBuilder stairs;
			stairs.SetUVScale(2.f);
			for (int step = 0; step < INSIDE_STAIR_STEPS; step++)
			{
				float x = INSIDE_STAIR_BOTTOM_X - (step + 0.5f) * run;
				Box(stairs, { x, (step + 1) * rise - 0.025f, middleZ }, { run + 0.03f, 0.05f, width - 0.1f }, tread);
			}
			float span = INSIDE_STAIR_BOTTOM_X - INSIDE_STAIR_TOP_X;
			float length = std::sqrt(span * span + UPPER_FLOOR_Y * UPPER_FLOOR_Y);
			float angle = std::atan2(UPPER_FLOOR_Y, span);
			glm::vec3 middle = { (INSIDE_STAIR_BOTTOM_X + INSIDE_STAIR_TOP_X) * 0.5f, UPPER_FLOOR_Y * 0.5f - 0.1f, 0.f };
			for (float z : { INSIDE_STAIR_NEAR_Z + 0.03f, HALF_DEPTH - 0.04f })
				ZTurnedBox(stairs, { middle.x, middle.y, z }, { length, 0.24f, 0.05f }, -angle, steel);
			for (int step = 0; step <= INSIDE_STAIR_STEPS - 4; step += 4)
			{
				float x = INSIDE_STAIR_BOTTOM_X - step * run;
				Box(stairs, { x, step * rise + 0.45f, INSIDE_STAIR_NEAR_Z + 0.03f }, { 0.04f, 0.9f, 0.04f }, steel);
			}
			ZTurnedBox(stairs, { middle.x, UPPER_FLOOR_Y * 0.5f + 0.9f, INSIDE_STAIR_NEAR_Z + 0.03f }, { length, 0.05f, 0.05f }, -angle, steel);

			// Upstairs, round the open sides of the stairwell, leaving the way onto the stairs at the top.
			float railLength = STAIRWELL_MAX_X - STAIRWELL_RAIL_MIN_X;
			for (float y : { UPPER_FLOOR_Y + 1.f, UPPER_FLOOR_Y + 0.5f })
			{
				Box(stairs, { (STAIRWELL_RAIL_MIN_X + STAIRWELL_MAX_X) * 0.5f, y, INSIDE_STAIR_NEAR_Z }, { railLength, 0.05f, 0.05f }, steel);
				Box(stairs, { STAIRWELL_MAX_X, y, middleZ }, { 0.05f, 0.05f, width }, steel);
			}
			for (float x : { STAIRWELL_RAIL_MIN_X, (STAIRWELL_RAIL_MIN_X + STAIRWELL_MAX_X) * 0.5f, STAIRWELL_MAX_X })
				Box(stairs, { x, UPPER_FLOOR_Y + 0.5f, INSIDE_STAIR_NEAR_Z }, { 0.05f, 1.f, 0.05f }, steel);
			Box(stairs, { STAIRWELL_MAX_X, UPPER_FLOOR_Y + 0.5f, HALF_DEPTH - 0.03f }, { 0.05f, 1.f, 0.05f }, steel);
			add(ShopModels::INSIDE_STAIRS, stairs);
		}

		// The two-post lift: posts on base plates either side, a header across the top; the
		// carriages and arms are a model of their own, raised and lowered.
		{
			const glm::vec3 blue = { 0.15f, 0.3f, 0.6f };
			const glm::vec3 dark = { 0.15f, 0.15f, 0.16f };
			MeshBuilder posts;
			posts.SetUVScale(2.f);
			for (float side : { -1.f, 1.f })
			{
				Box(posts, { side * 1.5f, 0.01f, 0.f }, { 0.5f, 0.02f, 0.6f }, dark);
				Box(posts, { side * 1.5f, 1.45f, 0.f }, { 0.28f, 2.9f, 0.3f }, blue);
				Box(posts, { side * 1.36f, 1.45f, 0.f }, { 0.02f, 2.7f, 0.1f }, dark);
			}
			Box(posts, { 0.f, 2.95f, 0.f }, { 3.3f, 0.12f, 0.2f }, blue);
			add(ShopModels::POST_LIFT, posts);

			MeshBuilder arms;
			arms.SetUVScale(2.f);
			for (float side : { -1.f, 1.f })
			{
				Box(arms, { side * 1.3f, 0.2f, 0.f }, { 0.14f, 0.4f, 0.22f }, dark);
				for (float swing : { -0.55f, 0.55f })
				{
					glm::vec3 tip = { side * 0.55f, 0.f, swing };
					glm::vec3 root = { side * 1.25f, 0.f, swing * 0.15f };
					glm::vec3 middle = (tip + root) * 0.5f;
					float length = glm::length(tip - root);
					float angle = std::atan2(tip.z - root.z, tip.x - root.x);
					glm::mat4 transform = glm::rotate(glm::translate(glm::mat4(1.f), middle + glm::vec3(0.f, 0.04f, 0.f)), -angle, { 0.f, 1.f, 0.f });
					arms.SetTransform(transform).SetColor({ 0.75f, 0.76f, 0.78f }).AddBox({}, { length, 0.08f, 0.1f });
					arms.ResetTransform();
					Box(arms, tip + glm::vec3(0.f, 0.11f, 0.f), { 0.12f, 0.06f, 0.12f }, { 0.85f, 0.65f, 0.1f });
				}
			}
			add(ShopModels::POST_LIFT_ARMS, arms);
		}

		// Plastigauge: a thin green thread, laid along the journal's length on top of it (origin on
		// the journal's surface).
		{
			MeshBuilder strip;
			Box(strip, { 0.f, 0.0004f, 0.f }, { 0.016f, 0.0008f, 0.0018f }, { 0.20f, 0.78f, 0.30f });
			add(ShopModels::PLASTIGAUGE_STRIP, strip);
		}
	}
}

void ShopAssets::LoadOutdoors()
{
	auto add = [&](const std::string& name, const MeshBuilder& builder) { AddModel(name, builder); };
	LoadGarage(add);
	LoadLot(add);
	LoadUpgrades(add);
	LoadHouse();
}
