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
			openings.push_back({ DOOR_MIN_X, DOOR_MAX_X, 0.f, DOOR_HEIGHT });
		if (wall == Wall::LEFT && g_variant.bay)
			openings.push_back({ -BAY_OPENING_HALF, BAY_OPENING_HALF, 0.f, BAY_OPENING_HEIGHT });
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
							WallBox(shell, span, DOOR_MAX_X + 0.08f, to, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);
						}
						else if (wall == Wall::LEFT && bay)
						{
							WallBox(shell, span, from, -BAY_OPENING_HALF - 0.02f, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);
							WallBox(shell, span, BAY_OPENING_HALF + 0.02f, to, 0.f, 1.f, -0.02f, 0.f, TRIM_GREEN);
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

	// Sand over the parts of `within` outside every one of `holes`, as a few boxes, its top 5 mm under the
	// paving. Cut into a grid on every rectangle's edges, cell by cell, a row's neighbours run together.
	void SandBoxes(MeshBuilder& builder, const std::vector<Paved>& within, const std::vector<Paved>& holes)
	{
		std::vector<float> xs, zs;
		for (const auto* list : { &within, &holes })
			for (const auto& rect : *list)
			{
				xs.insert(xs.end(), { rect.min.x, rect.max.x });
				zs.insert(zs.end(), { rect.min.y, rect.max.y });
			}
		for (auto* edges : { &xs, &zs })
		{
			std::sort(edges->begin(), edges->end());
			edges->erase(std::unique(edges->begin(), edges->end()), edges->end());
		}
		auto inside = [](const std::vector<Paved>& rects, glm::vec2 point) {
			return std::any_of(rects.begin(), rects.end(), [&](const Paved& rect) {
				return point.x > rect.min.x && point.x < rect.max.x && point.y > rect.min.y && point.y < rect.max.y;
			});
		};
		for (size_t row = 0; row + 1 < zs.size(); row++)
			for (size_t column = 0; column + 1 < xs.size();)
			{
				auto sand = [&](size_t at) {
					glm::vec2 middle = { (xs[at] + xs[at + 1]) * 0.5f, (zs[row] + zs[row + 1]) * 0.5f };
					return inside(within, middle) && !inside(holes, middle);
				};
				if (!sand(column))
				{
					column++;
					continue;
				}
				size_t end = column + 1;
				while (end + 1 < xs.size() && sand(end))
					end++;
				Box(builder, { (xs[column] + xs[end]) * 0.5f, -0.055f, (zs[row] + zs[row + 1]) * 0.5f }, { xs[end] - xs[column], 0.1f, zs[row + 1] - zs[row] });
				column = end;
			}
	}

	void LoadLot(const std::function<void(const std::string&, const MeshBuilder&)>& add)
	{
		{
			MeshBuilder sky;
			sky.AddSphere({}, 1.f, 16, 32, true);
			add(ShopModels::SKY_DOME, sky);
		}
		{
			// Sand as far as the eye can see, but not under the shack's floor, the paving or the highway: only 5 mm
			// below them, it would show through them from any distance. Where the side bay's floor and drive go,
			// sand of its own, there until the bay is built.
			const float outerX = HALF_WIDTH + WALL, outerZ = HALF_DEPTH + WALL;
			std::vector<Paved> covered(std::begin(PAVED), std::end(PAVED));
			covered.push_back({ { -outerX, -outerZ }, { outerX, outerZ } });
			covered.push_back(YARD);
			covered.push_back(YARD_TRACK);
			covered.push_back(QUARRY_PAD);
			for (float start : { HIGHWAY_NEAR_Z, HIGHWAY_NEAR_Z + HIGHWAY_CARRIAGEWAY + HIGHWAY_MEDIAN })
				covered.push_back({ { -HIGHWAY_HALF_LENGTH, start }, { HIGHWAY_HALF_LENGTH, start + HIGHWAY_CARRIAGEWAY } });
			const std::vector<Paved> bay = { { { BAY_MIN_X, -BAY_HALF_DEPTH }, { -outerX, BAY_HALF_DEPTH } }, BAY_DRIVE, BAY_DRIVE_LINK };
			std::vector<Paved> notOpen = covered;
			notOpen.insert(notOpen.end(), bay.begin(), bay.end());

			// One tile of sand covers about 17 m (see ProceduralTextures::Sand).
			MeshBuilder ground;
			ground.SetUVScale(0.06f);
			SandBoxes(ground, { { { -1200.f, -1200.f }, { 1200.f, 1200.f } } }, notOpen);
			add(ShopModels::GROUND, ground);
			MeshBuilder baySand;
			baySand.SetUVScale(0.06f);
			SandBoxes(baySand, bay, covered);
			add(ShopModels::BAY_SAND, baySand);
		}
		{
			// The drive down to the highway and the apron round the shack (see GarageLayout::PAVED).
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
			// The highway's two carriageways, shoulders and all, with sand between them.
			MeshBuilder road;
			road.SetUVScale(0.25f);
			for (float start : { HIGHWAY_NEAR_Z, HIGHWAY_NEAR_Z + HIGHWAY_CARRIAGEWAY + HIGHWAY_MEDIAN })
				Box(road, { 0.f, -0.04f, start + HIGHWAY_CARRIAGEWAY * 0.5f }, { HIGHWAY_HALF_LENGTH * 2.f, 0.1f, HIGHWAY_CARRIAGEWAY });
			add(ShopModels::ROAD, road);
		}
		{
			// Each carriageway: solid white along its outside edge, solid yellow along the median, and dashes
			// between its two lanes.
			MeshBuilder markings;
			const glm::vec3 yellow = { 0.85f, 0.68f, 0.18f };
			const glm::vec3 white = { 0.85f, 0.85f, 0.82f };
			float length = HIGHWAY_HALF_LENGTH * 2.f;
			for (bool farSide : { false, true })
			{
				float start = farSide ? HIGHWAY_NEAR_Z + HIGHWAY_CARRIAGEWAY + HIGHWAY_MEDIAN : HIGHWAY_NEAR_Z;
				float outside = farSide ? start + HIGHWAY_CARRIAGEWAY - HIGHWAY_SHOULDER : start + HIGHWAY_SHOULDER;
				float median = farSide ? start + HIGHWAY_SHOULDER : start + HIGHWAY_CARRIAGEWAY - HIGHWAY_SHOULDER;
				Box(markings, { 0.f, 0.012f, outside }, { length, 0.004f, 0.15f }, white);
				Box(markings, { 0.f, 0.012f, median }, { length, 0.004f, 0.15f }, yellow);
				float between = (outside + median) * 0.5f;
				for (float x = -HIGHWAY_HALF_LENGTH; x < HIGHWAY_HALF_LENGTH; x += 12.f)
					Box(markings, { x + 1.5f, 0.012f, between }, { 3.f, 0.004f, 0.15f }, white);
			}
			add(ShopModels::ROAD_MARKINGS, markings);
		}
		{
			// A saguaro: a ribbed trunk and two arms bent up, one higher than the other.
			MeshBuilder cactus;
			cactus.SetUVScale(2.f);
			const glm::vec3 green = { 0.32f, 0.48f, 0.27f };
			cactus.SetColor(green).AddCylinder({ 0.f, 1.6f, 0.f }, 0.2f, 3.2f, 10);
			cactus.SetColor(green).AddSphere({ 0.f, 3.2f, 0.f }, 0.2f, 4, 10);
			for (auto [side, height, reach, rise] : { std::tuple{ 1.f, 1.3f, 0.55f, 1.1f }, std::tuple{ -1.f, 1.9f, 0.45f, 0.8f } })
			{
				Box(cactus, { side * reach * 0.5f, height, 0.f }, { reach, 0.24f, 0.24f }, green);
				cactus.SetColor(green).AddCylinder({ side * reach, height + rise * 0.5f, 0.f }, 0.14f, rise, 8);
				cactus.SetColor(green).AddSphere({ side * reach, height + rise, 0.f }, 0.14f, 4, 8);
			}
			add(ShopModels::CACTUS, cactus);

			// A boulder: a lumpy, squashed ball, half sunk in the sand.
			MeshBuilder boulder;
			boulder.SetUVScale(1.f);
			boulder.SetColor({ 0.62f, 0.45f, 0.34f });
			boulder.SetTransform(glm::scale(glm::mat4(1.f), { 1.3f, 0.7f, 1.f })).AddSphere({ 0.f, 0.2f, 0.f }, 0.8f, 4, 7);
			boulder.SetTransform(glm::scale(glm::translate(glm::mat4(1.f), { 0.6f, 0.f, 0.3f }), { 0.9f, 0.6f, 0.8f })).AddSphere({ 0.f, 0.15f, 0.f }, 0.55f, 4, 6);
			boulder.ResetTransform();
			add(ShopModels::BOULDER, boulder);

			// Dry scrub: a few tufts of grey-green twigs.
			MeshBuilder scrub;
			scrub.SetUVScale(2.f);
			scrub.SetColor({ 0.55f, 0.55f, 0.4f });
			for (auto [x, z, r] : { std::tuple{ 0.f, 0.f, 0.45f }, std::tuple{ 0.35f, 0.2f, 0.3f }, std::tuple{ -0.3f, 0.25f, 0.32f }, std::tuple{ 0.1f, -0.35f, 0.28f } })
				scrub.AddSphere({ x, r * 0.6f, z }, r, 3, 6);
			add(ShopModels::SCRUB, scrub);
		}
		{
			// The mountains round the horizon: a ring of unit radius about the origin, scaled out to the edge of the
			// view and kept centred on the camera (see ShopLighting), so they're always far off. A tall, jagged range
			// at the back, and lower foothills in front of it. Every face looks in, towards the middle.
			MeshBuilder mountains;
			mountains.SetUVScale(60.f);
			auto hash = [](int i, int seed) {
				uint32_t h = static_cast<uint32_t>(i) * 374761393u + static_cast<uint32_t>(seed) * 668265263u;
				h = (h ^ (h >> 13)) * 1274126177u;
				return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65535.f;
			};
			auto inward = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
				glm::vec3 normal = glm::cross(b - a, c - a);
				if (glm::dot(normal, glm::vec3(a.x + b.x + c.x, 0.f, a.z + b.z + c.z)) > 0.f)
					std::swap(b, c);
				mountains.AddFlatTriangle(a, b, c);
			};
			auto range = [&](float radius, float low, float high, int peaks, int seed, const glm::vec3& rock, const glm::vec3& foot) {
				for (int i = 0; i < peaks; i++)
				{
					// Each peak with the saddle after it, its height between low and high, sharper the higher it is.
					float a0 = glm::two_pi<float>() * i / peaks, a1 = glm::two_pi<float>() * (i + 0.5f + (hash(i, seed) - 0.5f) * 0.4f) / peaks;
					float a2 = glm::two_pi<float>() * (i + 1) / peaks;
					float peak = low + (high - low) * std::pow(hash(i, seed + 1), 1.5f);
					float saddle = low * 0.6f + (peak - low) * 0.25f * hash(i, seed + 2);
					float nextSaddle = low * 0.6f + (low + (high - low) * std::pow(hash(i + 1, seed + 1), 1.5f) - low) * 0.25f * hash(i + 1, seed + 2);
					auto at = [&](float angle, float r, float y) { return glm::vec3(std::sin(angle) * r, y, std::cos(angle) * r); };
					glm::vec3 base0 = at(a0, radius, -0.02f), base1 = at(a1, radius, -0.02f), base2 = at(a2, radius, -0.02f);
					glm::vec3 top0 = at(a0, radius * 1.01f, saddle), top1 = at(a1, radius * 1.02f, peak), top2 = at(a2, radius * 1.01f, nextSaddle);
					glm::vec3 skirt0 = at(a0, radius * 0.96f, -0.02f), skirt1 = at(a1, radius * 0.95f, -0.02f), skirt2 = at(a2, radius * 0.96f, -0.02f);
					mountains.SetColor(rock);
					inward(skirt0, skirt1, top1);
					inward(skirt0, top1, top0);
					inward(skirt1, skirt2, top2);
					inward(skirt1, top2, top1);
					mountains.SetColor(foot);
					inward(base0, base1, skirt1);
					inward(base0, skirt1, skirt0);
					inward(base1, base2, skirt2);
					inward(base1, skirt2, skirt1);
				}
			};
			range(1.f, 0.07f, 0.26f, 72, 11, { 0.66f, 0.47f, 0.37f }, { 0.74f, 0.6f, 0.46f });
			range(0.86f, 0.025f, 0.08f, 96, 23, { 0.76f, 0.6f, 0.45f }, { 0.82f, 0.7f, 0.54f });
			add(ShopModels::MOUNTAINS, mountains);
		}
		{
			// Pole with an arm reaching over the road (+Z); the glowing head is its own model.
			MeshBuilder pole;
			const glm::vec3 grey = { 0.42f, 0.44f, 0.46f };
			pole.SetColor(grey).AddCylinder({ 0.f, STREET_LIGHT_HEIGHT * 0.5f, 0.f }, 0.08f, STREET_LIGHT_HEIGHT, 10);
			pole.SetColor(grey).AddCylinder({ 0.f, 0.3f, 0.f }, 0.16f, 0.6f, 10);
			Box(pole, { 0.f, STREET_LIGHT_HEIGHT - 0.05f, STREET_LIGHT_REACH * 0.5f }, { 0.08f, 0.08f, STREET_LIGHT_REACH + 0.1f }, grey);
			Box(pole, { 0.f, STREET_LIGHT_HEIGHT - 0.07f, STREET_LIGHT_REACH }, { 0.34f, 0.1f, 0.6f }, grey);
			add(ShopModels::STREET_LIGHT, pole);

			MeshBuilder head;
			Box(head, {}, { 0.28f, 0.04f, 0.5f }, { 1.f, 0.95f, 0.82f });
			add(ShopModels::LAMP_HEAD, head);
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
		{
			// The salvage yard's gravel, and the track in from the drive (see GarageLayout::YARD).
			MeshBuilder gravel;
			gravel.SetUVScale(0.7f);
			for (const auto& area : { YARD, YARD_TRACK })
			{
				glm::vec2 middle = (area.min + area.max) * 0.5f, size = area.max - area.min;
				Box(gravel, { middle.x, -0.05f, middle.y }, { size.x, 0.1f, size.y }, { 1.f, 0.86f, 0.68f });
			}
			add(ShopModels::YARD_GROUND, gravel);

			// The fence: sheets of corrugated iron, a patchwork of old and rusty ones, between steel posts, all round
			// but for the gate in the left side, where taller posts carry a crossbar and a sign board.
			MeshBuilder fence;
			fence.SetUVScale(1.f);
			const glm::vec3 sheets[] = { { 0.62f, 0.6f, 0.56f }, { 0.55f, 0.36f, 0.24f }, { 0.48f, 0.5f, 0.5f }, { 0.6f, 0.42f, 0.3f }, { 0.7f, 0.68f, 0.62f } };
			const glm::vec3 post = { 0.3f, 0.3f, 0.32f };
			constexpr float PANEL = 2.f, THICK = 0.03f;
			int sheet = 0;
			// One straight run of fence from a to b (along X or Z), posts at each panel's ends.
			auto run = [&](glm::vec2 a, glm::vec2 b) {
				glm::vec2 along = b - a;
				float length = glm::length(along);
				int panels = (std::max)(1, static_cast<int>(std::round(length / PANEL)));
				bool alongX = std::abs(along.x) > std::abs(along.y);
				for (int i = 0; i < panels; i++)
				{
					glm::vec2 p0 = a + along * (static_cast<float>(i) / panels), p1 = a + along * (static_cast<float>(i + 1) / panels);
					glm::vec2 middle = (p0 + p1) * 0.5f;
					float span = length / panels;
					// Each sheet a little higher or lower than the last, as they were put up.
					float height = YARD_FENCE_HEIGHT - 0.12f * static_cast<float>((sheet * 7) % 3);
					glm::vec3 size = alongX ? glm::vec3(span + 0.02f, height, THICK) : glm::vec3(THICK, height, span + 0.02f);
					Box(fence, { middle.x, height * 0.5f + 0.02f, middle.y }, size, sheets[sheet++ % std::size(sheets)]);
					fence.ResetTransform().SetColor(post).AddCylinder({ p0.x, YARD_FENCE_HEIGHT * 0.5f + 0.05f, p0.y }, 0.05f, YARD_FENCE_HEIGHT + 0.1f, 8);
				}
				fence.ResetTransform().SetColor(post).AddCylinder({ b.x, YARD_FENCE_HEIGHT * 0.5f + 0.05f, b.y }, 0.05f, YARD_FENCE_HEIGHT + 0.1f, 8);
			};
			run({ YARD_MIN_X, YARD_MIN_Z }, { YARD_MAX_X, YARD_MIN_Z });
			run({ YARD_MAX_X, YARD_MIN_Z }, { YARD_MAX_X, YARD_MAX_Z });
			run({ YARD_MAX_X, YARD_MAX_Z }, { YARD_MIN_X, YARD_MAX_Z });
			run({ YARD_MIN_X, YARD_MAX_Z }, { YARD_MIN_X, YARD_GATE_MAX_Z });
			run({ YARD_MIN_X, YARD_GATE_MIN_Z }, { YARD_MIN_X, YARD_MIN_Z });
			// The gate: tall posts, a crossbar, and the yard's sign board.
			constexpr float GATE_HEIGHT = 3.1f;
			for (float z : { YARD_GATE_MIN_Z, YARD_GATE_MAX_Z })
				fence.ResetTransform().SetColor(post).AddCylinder({ YARD_MIN_X, GATE_HEIGHT * 0.5f, z }, 0.08f, GATE_HEIGHT, 10);
			float gateMiddle = (YARD_GATE_MIN_Z + YARD_GATE_MAX_Z) * 0.5f, gateWidth = YARD_GATE_MAX_Z - YARD_GATE_MIN_Z;
			Box(fence, { YARD_MIN_X, GATE_HEIGHT - 0.06f, gateMiddle }, { 0.1f, 0.1f, gateWidth + 0.2f }, post);
			Box(fence, { YARD_MIN_X - 0.06f, GATE_HEIGHT - 0.45f, gateMiddle }, { 0.04f, 0.6f, gateWidth * 0.7f }, { 0.75f, 0.68f, 0.5f });
			Box(fence, { YARD_MIN_X - 0.085f, GATE_HEIGHT - 0.45f, gateMiddle }, { 0.01f, 0.36f, gateWidth * 0.6f }, { 0.55f, 0.15f, 0.1f });
			add(ShopModels::YARD_FENCE, fence);

			// One leaf of the gate, built out from its hinge along +X: a steel frame round a rusty sheet, a brace
			// across it, and the latch on its free end. Each leaf is an object of its own, shut or swung open.
			MeshBuilder leaf;
			leaf.SetUVScale(1.f);
			const float length = gateWidth * 0.5f - 0.03f;
			Box(leaf, { length * 0.5f, 1.f, 0.f }, { length, 1.7f, 0.02f }, sheets[1]);
			for (float y : { 0.14f, 1.86f })
				Box(leaf, { length * 0.5f, y, 0.f }, { length, 0.05f, 0.05f }, post);
			for (float x : { 0.03f, length - 0.03f })
				Box(leaf, { x, 1.f, 0.f }, { 0.05f, 1.77f, 0.05f }, post);
			{
				float diagonal = std::atan2(1.6f, length);
				glm::mat4 brace = glm::rotate(glm::translate(glm::mat4(1.f), { length * 0.5f, 1.f, 0.f }), diagonal, glm::vec3(0.f, 0.f, 1.f));
				leaf.SetTransform(brace).SetColor(post).AddBox({}, { std::sqrt(length * length + 2.56f), 0.04f, 0.04f });
				leaf.ResetTransform();
			}
			Box(leaf, { length - 0.06f, 1.05f, 0.f }, { 0.04f, 0.12f, 0.08f }, { 0.2f, 0.2f, 0.2f });
			add(ShopModels::YARD_GATE_LEAF, leaf);

		{
			// The desert quarry (GarageLayout::QUARRY_PAD). Each piece is built about its own spot and shown when it's bought.
			const glm::vec3 rust = { 0.55f, 0.3f, 0.18f }, yellow = { 0.9f, 0.68f, 0.12f }, steelGrey = { 0.4f, 0.41f, 0.43f };
			const glm::vec3 rock = { 0.72f, 0.58f, 0.45f }, dark = { 0.12f, 0.12f, 0.13f };
			// A cylinder lying along Z.
			auto cylinderZ = [](MeshBuilder& builder, const glm::vec3& at, float radius, float length, const glm::vec3& color, int segments) {
				builder.SetTransform(glm::rotate(glm::translate(glm::mat4(1.f), at), glm::half_pi<float>(), glm::vec3(1.f, 0.f, 0.f))).SetColor(color)
					.AddCylinder({}, radius, length, segments);
				builder.ResetTransform();
			};

			MeshBuilder pad;
			pad.SetUVScale(0.7f);
			{
				glm::vec2 middle = (QUARRY_PAD.min + QUARRY_PAD.max) * 0.5f, size = QUARRY_PAD.max - QUARRY_PAD.min;
				Box(pad, { middle.x, -0.05f, middle.y }, { size.x, 0.1f, size.y }, { 0.95f, 0.85f, 0.7f });
			}
			add(ShopModels::QUARRY_PAD, pad);

			// The sign by the drive: a board on two posts.
			MeshBuilder sign;
			for (float x : { -0.7f, 0.7f })
				Box(sign, { x, 1.f, 0.f }, { 0.08f, 2.f, 0.08f }, { 0.45f, 0.33f, 0.2f });
			Box(sign, { 0.f, 1.65f, 0.f }, { 1.7f, 0.6f, 0.05f }, { 0.85f, 0.75f, 0.5f });
			Box(sign, { 0.f, 1.65f, 0.03f }, { 1.5f, 0.12f, 0.01f }, { 0.6f, 0.16f, 0.1f });
			Box(sign, { 0.f, 1.47f, 0.03f }, { 1.1f, 0.06f, 0.01f }, { 0.3f, 0.25f, 0.2f });
			add(ShopModels::QUARRY_SIGN, sign);

			// The jaw crusher: its frame on skids, the flywheels either side, the feed mouth on top, the motor and belt, and
			// the chute out to the hopper (along +X).
			MeshBuilder crusher;
			for (float z : { -0.6f, 0.6f })
				Box(crusher, { 0.f, 0.08f, z }, { 2.f, 0.16f, 0.14f }, dark);
			Box(crusher, { 0.f, 0.75f, 0.f }, { 1.4f, 1.2f, 1.0f }, yellow);
			for (float z : { -0.58f, 0.58f })
			{
				cylinderZ(crusher, { -0.1f, 0.95f, z }, 0.55f, 0.1f, steelGrey, 20);
				cylinderZ(crusher, { -0.1f, 0.95f, z * 1.1f }, 0.12f, 0.08f, dark, 12);
			}
			// The feed mouth, wider at the top.
			for (float side : { -1.f, 1.f })
			{
				TurnedBox(crusher, { 0.f, 1.55f, side * 0.45f }, { 1.2f, 0.5f, 0.06f }, side * 0.35f, yellow);
				Box(crusher, { side * 0.6f, 1.55f, 0.f }, { 0.06f, 0.5f, 1.0f }, yellow);
			}
			Box(crusher, { -0.8f, 0.45f, 0.f }, { 0.35f, 0.4f, 0.4f }, { 0.25f, 0.35f, 0.55f });	// the motor
			// The chute, down to the hopper.
			{
				glm::mat4 chute = glm::rotate(glm::translate(glm::mat4(1.f), { 1.05f, 0.55f, 0.f }), -0.5f, glm::vec3(0.f, 0.f, 1.f));
				crusher.SetTransform(chute).SetColor(steelGrey).AddBox({}, { 0.9f, 0.05f, 0.6f });
				crusher.ResetTransform();
			}
			add(ShopModels::QUARRY_CRUSHER, crusher);

			// The conveyor: a belt on a frame rising from the rock heap to the crusher's mouth (built along +X, 4 m).
			MeshBuilder conveyor;
			{
				constexpr float LENGTH = 4.2f, RISE = 1.6f;
				float slope = std::atan2(RISE, LENGTH);
				glm::mat4 belt = glm::rotate(glm::translate(glm::mat4(1.f), { LENGTH * 0.5f, 0.35f + RISE * 0.5f, 0.f }), slope, glm::vec3(0.f, 0.f, 1.f));
				float run = std::sqrt(LENGTH * LENGTH + RISE * RISE);
				conveyor.SetTransform(belt).SetColor(dark).AddBox({}, { run, 0.05f, 0.5f });
				conveyor.SetTransform(belt).SetColor(steelGrey).AddBox({ 0.f, -0.06f, 0.29f }, { run, 0.12f, 0.05f });
				conveyor.SetTransform(belt).SetColor(steelGrey).AddBox({ 0.f, -0.06f, -0.29f }, { run, 0.12f, 0.05f });
				// A few stones riding it.
				for (int i = 0; i < 5; i++)
					conveyor.SetTransform(belt).SetColor(rock).AddSphere({ -run * 0.35f + i * run * 0.17f, 0.08f, 0.05f * (i % 2 ? 1.f : -1.f) }, 0.07f, 4, 6);
				conveyor.ResetTransform();
				// Its legs.
				for (float x : { 0.6f, 2.1f, 3.6f })
				{
					float top = 0.35f + RISE * x / LENGTH;
					for (float z : { -0.27f, 0.27f })
						Box(conveyor, { x, top * 0.5f, z }, { 0.06f, top, 0.06f }, steelGrey);
				}
			}
			add(ShopModels::QUARRY_CONVEYOR, conveyor);

			// The hopper: a steel bin up on legs, its chute at the bottom for loading trucks.
			MeshBuilder hopper;
			for (float x : { -0.6f, 0.6f })
				for (float z : { -0.6f, 0.6f })
					Box(hopper, { x, 0.6f, z }, { 0.1f, 1.2f, 0.1f }, steelGrey);
			Box(hopper, { 0.f, 1.65f, 0.f }, { 1.5f, 0.9f, 1.5f }, rust);
			Box(hopper, { 0.f, 2.12f, 0.f }, { 1.6f, 0.06f, 1.6f }, steelGrey);
			Box(hopper, { 0.f, 1.05f, 0.f }, { 0.5f, 0.3f, 0.5f }, rust);
			add(ShopModels::QUARRY_HOPPER, hopper);

			// Crushed stone: a cone of it, its height scaled by how full the hopper is.
			MeshBuilder gravel;
			gravel.ResetTransform().SetColor({ 0.78f, 0.72f, 0.64f }).AddCone({ 0.f, 0.f, 0.f }, 0.9f, 0.8f, 16);
			add(ShopModels::QUARRY_GRAVEL, gravel);

			// The hand's shed: a lean-to of corrugated iron, a door, and a wheelbarrow by it.
			MeshBuilder shed;
			Box(shed, { 0.f, 1.f, 0.f }, { 1.8f, 2.f, 1.5f }, { 0.55f, 0.5f, 0.45f });
			TurnedBox(shed, { 0.f, 2.08f, 0.f }, { 2.f, 0.04f, 1.75f }, -0.12f, rust);
			Box(shed, { 0.3f, 0.9f, 0.76f }, { 0.7f, 1.8f, 0.03f }, { 0.4f, 0.28f, 0.18f });
			Box(shed, { -1.25f, 0.35f, 0.4f }, { 0.6f, 0.2f, 0.4f }, { 0.3f, 0.5f, 0.3f });
			cylinderZ(shed, { -1.5f, 0.12f, 0.4f }, 0.12f, 0.05f, dark, 12);
			add(ShopModels::QUARRY_SHED, shed);
		}
			// Junk: stacks of old tires, a heap of scrap panels, engine blocks on a pallet and a pile of exhaust pipe.
			MeshBuilder junk;
			const glm::vec3 rubber = { 0.08f, 0.08f, 0.08f }, rust = { 0.45f, 0.26f, 0.15f }, scrap = { 0.5f, 0.48f, 0.45f };
			for (const auto& stack : YARD_TIRE_STACKS)
				for (int tire = 0; tire < static_cast<int>(stack.y); tire++)
				{
					glm::vec3 at = { stack.x + 0.04f * static_cast<float>(tire % 2), 0.1f + tire * 0.19f, stack.z };
					junk.ResetTransform().SetColor(rubber).AddTube(at, 0.3f, 0.17f, 0.18f, 20);
				}
			// The scrap heap: panels leaning every which way.
			for (int i = 0; i < 9; i++)
			{
				float a = static_cast<float>(i);
				glm::mat4 turn = glm::rotate(glm::translate(glm::mat4(1.f), YARD_SCRAP_HEAP + glm::vec3(std::sin(a * 2.3f) * 0.8f, 0.25f + 0.08f * (i % 4), std::cos(a * 1.7f) * 0.6f)),
					a * 0.9f, glm::vec3(0.f, 1.f, 0.f));
				turn = glm::rotate(turn, 0.3f + 0.25f * (i % 3), glm::vec3(1.f, 0.f, 0.f));
				junk.SetTransform(turn).SetColor(i % 3 == 0 ? rust : i % 3 == 1 ? scrap : glm::vec3(0.8f, 0.78f, 0.72f)).AddBox({}, { 1.3f, 0.03f, 0.8f });
			}
			junk.ResetTransform();
			// Engine blocks on a pallet.
			Box(junk, YARD_PALLET + glm::vec3(0.f, 0.07f, 0.f), { 1.2f, 0.14f, 1.0f }, { 0.55f, 0.42f, 0.28f });
			for (float x : { -0.3f, 0.3f })
			{
				Box(junk, YARD_PALLET + glm::vec3(x, 0.39f, 0.f), { 0.48f, 0.5f, 0.36f }, rust);
				Box(junk, YARD_PALLET + glm::vec3(x, 0.68f, 0.f), { 0.44f, 0.08f, 0.32f }, { 0.35f, 0.33f, 0.31f });
			}
			// Exhaust pipe and bent tube in a pile.
			for (int i = 0; i < 6; i++)
			{
				glm::mat4 turn = glm::rotate(glm::translate(glm::mat4(1.f), YARD_PIPE_PILE + glm::vec3(0.f, 0.04f + 0.05f * (i % 2), 0.12f * i - 0.3f)),
					0.15f * i - 0.4f, glm::vec3(0.f, 1.f, 0.f));
				junk.SetTransform(glm::rotate(turn, glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f))).SetColor(i % 2 ? rust : scrap).AddCylinder({}, 0.03f, 1.6f, 10);
			}
			junk.ResetTransform();
			add(ShopModels::YARD_JUNK, junk);
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
			Box(floor, { middleX, -0.044f, 0.f }, { width, 0.1f, BAY_HALF_DEPTH * 2.f });
			add(ShopModels::BAY_FLOOR, floor);

			MeshBuilder drive;
			drive.SetUVScale(0.5f);
			for (const auto& paved : { BAY_DRIVE, BAY_DRIVE_LINK })
			{
				glm::vec2 middle = (paved.min + paved.max) * 0.5f, size = paved.max - paved.min;
				Box(drive, { middle.x, -0.044f, middle.y }, { size.x, 0.1f, size.y });
			}
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
}
