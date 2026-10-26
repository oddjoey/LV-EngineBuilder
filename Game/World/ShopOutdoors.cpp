#include "ShopAssets.h"

#include <algorithm>
#include <cmath>
#include <functional>
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
		for (const auto& window : WINDOWS)
			if (window.wall == wall)
				openings.push_back({ window.u0, window.u1, window.v0, window.v1 });
		if (wall == Wall::FRONT)
			openings.push_back({ DOOR_MIN_X, DOOR_MAX_X, 0.f, DOOR_HEIGHT });
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
					WallBox(shell, span, DOOR_MAX_X + 0.08f, to, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);
				}
				else
					WallBox(shell, span, from, to, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);

				for (const auto& window : WINDOWS)
					if (window.wall == wall)
						WallBox(shell, span, window.u0 - 0.06f, window.u1 + 0.06f, window.v0 - 0.04f, window.v0, -0.06f, PLASTER, { 0.85f, 0.85f, 0.82f });
			}
			Box(shell, { 0.f, HEIGHT + WALL * 0.5f, 0.f }, { outerX * 2.f, WALL, outerZ * 2.f }, CEILING);

			// Roll-up door: the coil housing over the opening and the guide rails either side.
			auto front = SpanOf(Wall::FRONT);
			WallBox(shell, front, DOOR_MIN_X - 0.12f, DOOR_MAX_X + 0.12f, DOOR_HEIGHT, DOOR_HEIGHT + 0.42f, -0.42f, 0.f, FRAME_GREY);
			for (float u : { DOOR_MIN_X - 0.05f, DOOR_MAX_X + 0.05f })
				WallBox(shell, front, u - 0.05f, u + 0.05f, 0.f, DOOR_HEIGHT, -0.08f, 0.f, FRAME_GREY);
			add(ShopModels::WALLS, shell);
		}

		// Outside: corrugated siding and the gable ends.
		{
			MeshBuilder siding;
			siding.SetUVScale(0.5f);
			for (auto wall : ALL_WALLS)
				WallLayer(siding, wall, PLASTER, WALL, wallTop);
			Gables(siding, outerX, outerZ, wallTop, ROOF_RIDGE_Y);
			add(ShopModels::GARAGE_SIDING, siding);
		}
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
				for (const auto& window : WINDOWS)
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

			for (float x : { -outerX, outerX })
				for (float z : { -outerZ, outerZ })
					Box(frames, { x, wallTop * 0.5f, z }, { 0.16f, wallTop, 0.16f }, FRAME_GREY);

			float roofLength = (outerX + 0.3f) * 2.f;
			float eaveZ = outerZ + ROOF_OVERHANG;
			float eaveY = ROOF_RIDGE_Y - eaveZ * RoofSlope();
			for (float side : { 1.f, -1.f })
				Box(frames, { 0.f, eaveY, side * eaveZ }, { roofLength, 0.2f, 0.04f }, FRAME_GREY);
			Box(frames, { 0.f, ROOF_RIDGE_Y + 0.12f, 0.f }, { roofLength, 0.06f, 0.32f }, FRAME_GREY);
			add(ShopModels::GARAGE_FRAMES, frames);
		}
		{
			MeshBuilder glass;
			for (auto wall : ALL_WALLS)
				for (const auto& window : WINDOWS)
					if (window.wall == wall)
						WallBox(glass, SpanOf(wall), window.u0, window.u1, window.v0, window.v1, 0.055f, 0.065f, { 0.75f, 0.85f, 0.9f });
			add(ShopModels::WINDOW_GLASS, glass);
		}

		// Hangs down from its top edge, so it can be rolled up by shrinking it in Y.
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

	void LoadLot(const std::function<void(const std::string&, const MeshBuilder&)>& add)
	{
		constexpr float STREET_HALF_LENGTH = 160.f;
		constexpr float DRIVE_MIN_X = -4.6f;
		constexpr float DRIVE_MAX_X = 1.6f;
		constexpr float SIDEWALK_NEAR_Z = 16.f;
		constexpr float SIDEWALK_FAR_Z = 17.6f;

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
			// Driveway out to the street, and the sidewalk either side of it. Nothing overlaps, so
			// nothing flickers.
			MeshBuilder paving;
			paving.SetUVScale(0.5f);
			float driveFrom = HALF_DEPTH;
			Box(paving, { (DRIVE_MIN_X + DRIVE_MAX_X) * 0.5f, -0.05f, (driveFrom + STREET_NEAR_Z) * 0.5f }, { DRIVE_MAX_X - DRIVE_MIN_X, 0.1f, STREET_NEAR_Z - driveFrom });
			float sidewalkZ = (SIDEWALK_NEAR_Z + SIDEWALK_FAR_Z) * 0.5f;
			float sidewalkDepth = SIDEWALK_FAR_Z - SIDEWALK_NEAR_Z;
			float leftLength = DRIVE_MIN_X + STREET_HALF_LENGTH;
			float rightLength = STREET_HALF_LENGTH - DRIVE_MAX_X;
			Box(paving, { DRIVE_MIN_X - leftLength * 0.5f, -0.05f, sidewalkZ }, { leftLength, 0.1f, sidewalkDepth });
			Box(paving, { DRIVE_MAX_X + rightLength * 0.5f, -0.05f, sidewalkZ }, { rightLength, 0.1f, sidewalkDepth });
			// A strip of concrete round the building.
			Box(paving, { 0.f, -0.05f, -HALF_DEPTH - WALL - 0.45f }, { (HALF_WIDTH + WALL + 0.9f) * 2.f, 0.1f, 0.9f });
			for (float side : { 1.f, -1.f })
				Box(paving, { side * (HALF_WIDTH + WALL + 0.45f), -0.05f, 0.f }, { 0.9f, 0.1f, (HALF_DEPTH + WALL) * 2.f });
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
			// Board fence round the back and sides of the lot, posts every 2.5 m.
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
			float frontZ = SIDEWALK_NEAR_Z - 1.f;
			run({ -LOT_HALF_WIDTH, LOT_BACK_Z }, { LOT_HALF_WIDTH, LOT_BACK_Z });
			run({ -LOT_HALF_WIDTH, LOT_BACK_Z }, { -LOT_HALF_WIDTH, frontZ });
			run({ LOT_HALF_WIDTH, LOT_BACK_Z }, { LOT_HALF_WIDTH, frontZ });
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
}

void ShopAssets::LoadOutdoors()
{
	auto add = [&](const std::string& name, const MeshBuilder& builder) { AddModel(name, builder); };
	LoadGarage(add);
	LoadLot(add);
}
