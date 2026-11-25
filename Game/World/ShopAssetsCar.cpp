// The project car's models: stand-ins for its parts, and the car itself on the drive.
#include "ShopAssets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "LV/Engine/Graphics/Models/MeshBuilder.h"
#include "../Simulation/Furniture.h"
#include "../Simulation/PartCatalog.h"
#include "CarBody.h"
#include "ToolShapes.h"

namespace
{
	glm::mat4 At(const glm::vec3& position) { return glm::translate(glm::mat4(1.f), position); }
	// Primitives are built along Y; these turn them to run along X or Z.
	glm::mat4 AlongX(const glm::vec3& position) { return glm::rotate(At(position), glm::half_pi<float>(), { 0.f, 0.f, 1.f }); }
	glm::mat4 AlongZ(const glm::vec3& position) { return glm::rotate(At(position), glm::half_pi<float>(), { 1.f, 0.f, 0.f }); }

	void Box(MeshBuilder& builder, const glm::vec3& center, const glm::vec3& size, const glm::vec3& color)
	{
		builder.ResetTransform().SetColor(color).AddBox(center, size);
	}

	void Cylinder(MeshBuilder& builder, const glm::mat4& transform, float radius, float length, const glm::vec3& color, int segments = 20)
	{
		builder.SetTransform(transform).SetColor(color).AddCylinder({}, radius, length, segments);
		builder.ResetTransform();
	}

	void Tube(MeshBuilder& builder, const glm::mat4& transform, float outer, float inner, float length, const glm::vec3& color, int segments = 24)
	{
		builder.SetTransform(transform).SetColor(color).AddTube({}, outer, inner, length, segments, 0.f, glm::two_pi<float>());
		builder.ResetTransform();
	}

	// A quad as two flat triangles, turned to face `facing` whichever order its corners came in. Degenerate halves
	// (where a panel's edge runs to nothing) are left out.
	void Quad(MeshBuilder& builder, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, const glm::vec3& facing)
	{
		glm::vec3 normal = glm::cross(p1 - p0, p2 - p0) + glm::cross(p2 - p0, p3 - p0);
		if (glm::dot(normal, facing) < 0.f)
			std::swap(p1, p3);
		auto triangle = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
			if (glm::length(glm::cross(b - a, c - a)) > 1e-8f)
				builder.AddFlatTriangle(a, b, c);
		};
		triangle(p0, p1, p2);
		triangle(p0, p2, p3);
	}

	// A body panel seen side on: between bottom(x) and top(x) from x0 to x1, thick across the car about z. The
	// bottom edge can be cut round a wheel arch (Arched).
	template <class Bottom, class Top>
	void SidePanel(MeshBuilder& builder, float x0, float x1, Bottom bottom, Top top, float z, float thick, const glm::vec3& color, int steps = 40)
	{
		builder.ResetTransform().SetColor(color);
		float outer = z + thick * 0.5f, inner = z - thick * 0.5f;
		for (int i = 0; i < steps; i++)
		{
			float xa = x0 + (x1 - x0) * i / steps, xb = x0 + (x1 - x0) * (i + 1) / steps;
			float a0 = bottom(xa), a1 = top(xa), b0 = bottom(xb), b1 = top(xb);
			if (a1 - a0 < 1e-4f && b1 - b0 < 1e-4f)
				continue;
			Quad(builder, { xa, a0, outer }, { xb, b0, outer }, { xb, b1, outer }, { xa, a1, outer }, { 0.f, 0.f, 1.f });
			Quad(builder, { xa, a0, inner }, { xb, b0, inner }, { xb, b1, inner }, { xa, a1, inner }, { 0.f, 0.f, -1.f });
			Quad(builder, { xa, a1, inner }, { xb, b1, inner }, { xb, b1, outer }, { xa, a1, outer }, { -(b1 - a1), xb - xa, 0.f });
			Quad(builder, { xa, a0, inner }, { xb, b0, inner }, { xb, b0, outer }, { xa, a0, outer }, { b0 - a0, -(xb - xa), 0.f });
			if (i == 0)
				Quad(builder, { xa, a0, inner }, { xa, a1, inner }, { xa, a1, outer }, { xa, a0, outer }, { -1.f, 0.f, 0.f });
			if (i == steps - 1)
				Quad(builder, { xb, b0, inner }, { xb, b1, inner }, { xb, b1, outer }, { xb, b0, outer }, { 1.f, 0.f, 0.f });
		}
	}

	// A panel's bottom edge: straight at base, but cut up round a wheel arch of radius r about (cx, cy).
	auto Arched(float base, float cx, float cy, float r)
	{
		return [=](float x) {
			float dx = x - cx;
			return std::abs(dx) < r ? (std::max)(base, cy + std::sqrt(r * r - dx * dx)) : base;
		};
	}
	auto Level(float y) { return [=](float) { return y; }; }

	// A wheel arch flared out from a panel's outer face (at z, `out` the way it faces, +1 or -1): a broad band round
	// the arch standing a little off the skin, and a narrower lip round its edge standing further out, over the tire.
	void ArchFlare(MeshBuilder& builder, float cx, float cy, float r, float z, float out, const glm::vec3& color)
	{
		auto band = [&](float inner, float outer, float from, float to) {
			builder.SetTransform(AlongZ({ cx, cy, z + out * (from + to) * 0.5f })).SetColor(color)
				.AddTube({}, r + outer, r + inner, to - from, 28, glm::pi<float>(), glm::two_pi<float>());
			builder.ResetTransform();
		};
		band(0.f, 0.075f, -0.012f, 0.006f);
		band(0.f, 0.055f, 0.006f, 0.018f);
		band(-0.004f, 0.034f, 0.018f, 0.034f);
	}

	// The lip rolled round a wheel arch, on the panel's outer face at z.
	void ArchLip(MeshBuilder& builder, float cx, float cy, float r, float z, float width, const glm::vec3& color)
	{
		// Round the top of the arch only: the tube is turned to lie along z, and its upper half is the arc from pi to 2 pi.
		builder.SetTransform(AlongZ({ cx, cy, z })).SetColor(color).AddTube({}, r + 0.022f, r, width, 24, glm::pi<float>(), glm::two_pi<float>());
		builder.ResetTransform();
	}

	// A convex outline (in x and y, counter-clockwise or not) pushed out across z0 to z1, through the given transform.
	void Prism(MeshBuilder& builder, const std::vector<glm::vec2>& outline, float z0, float z1, const glm::vec3& color,
		const glm::mat4& transform = glm::mat4(1.f))
	{
		builder.SetTransform(transform).SetColor(color);
		glm::vec2 centre = {};
		for (const auto& point : outline)
			centre += point / static_cast<float>(outline.size());
		for (size_t i = 0; i < outline.size(); i++)
		{
			glm::vec2 a = outline[i], b = outline[(i + 1) % outline.size()];
			glm::vec2 out = (a + b) * 0.5f - centre;
			Quad(builder, { a, z0 }, { b, z0 }, { b, z1 }, { a, z1 }, { out, 0.f });
			Quad(builder, { centre, z1 }, { a, z1 }, { b, z1 }, { centre, z1 }, { 0.f, 0.f, 1.f });
			Quad(builder, { centre, z0 }, { a, z0 }, { b, z0 }, { centre, z0 }, { 0.f, 0.f, -1.f });
		}
		builder.ResetTransform();
	}

	// A box through a transform (turned or tilted).
	void TurnedBox(MeshBuilder& builder, const glm::mat4& transform, const glm::vec3& centre, const glm::vec3& size, const glm::vec3& color)
	{
		builder.SetTransform(transform).SetColor(color).AddBox(centre, size);
		builder.ResetTransform();
	}

	// A bar from a to b, thick deep and width across, its width lying as near `across` as it can.
	void Beam(MeshBuilder& builder, const glm::vec3& a, const glm::vec3& b, const glm::vec3& across, float thick, float width, const glm::vec3& color)
	{
		glm::vec3 x = glm::normalize(b - a);
		glm::vec3 z = glm::normalize(across - x * glm::dot(across, x));
		glm::vec3 y = glm::cross(z, x);
		glm::mat4 frame(glm::vec4(x, 0.f), glm::vec4(y, 0.f), glm::vec4(z, 0.f), glm::vec4((a + b) * 0.5f, 1.f));
		builder.SetTransform(frame).SetColor(color).AddBox({}, { glm::length(b - a), thick, width });
		builder.ResetTransform();
	}

	// A flat convex sheet through these corners (all in one plane, whose normal is given), thick through it: glass,
	// and the pillars that lean in with it.
	void Slab(MeshBuilder& builder, const std::vector<glm::vec3>& outline, glm::vec3 normal, float thick, const glm::vec3& color)
	{
		builder.ResetTransform().SetColor(color);
		normal = glm::normalize(normal);
		glm::vec3 centre = {};
		for (const auto& point : outline)
			centre += point / static_cast<float>(outline.size());
		glm::vec3 up = normal * (thick * 0.5f);
		for (size_t i = 0; i < outline.size(); i++)
		{
			glm::vec3 a = outline[i], b = outline[(i + 1) % outline.size()];
			Quad(builder, centre + up, a + up, b + up, centre + up, normal);
			Quad(builder, centre - up, a - up, b - up, centre - up, -normal);
			Quad(builder, a - up, b - up, b + up, a + up, (a + b) * 0.5f - centre);
		}
	}

	const glm::vec3 BODY_WHITE = { 0.90f, 0.90f, 0.87f };
	const glm::vec3 TRIM_BLACK = { 0.08f, 0.08f, 0.09f };
	const glm::vec3 GLASS = { 0.45f, 0.58f, 0.64f };
	const glm::vec3 RUBBER = { 0.07f, 0.07f, 0.07f };
	const glm::vec3 IRON = { 0.36f, 0.34f, 0.33f };
	const glm::vec3 ALLOY = { 0.70f, 0.71f, 0.72f };
	const glm::vec3 ARM_GREY = { 0.20f, 0.20f, 0.21f };
	const glm::vec3 SPRING_RED = { 0.62f, 0.12f, 0.10f };
	const glm::vec3 VINYL = { 0.15f, 0.14f, 0.14f };
	const glm::vec3 AMBER = { 0.95f, 0.55f, 0.10f };
	const glm::vec3 LENS_RED = { 0.75f, 0.08f, 0.06f };
	const glm::vec3 COPPER = { 0.72f, 0.45f, 0.22f };
	const glm::vec3 OFF_WHITE = { 0.85f, 0.85f, 0.80f };
	const glm::vec3 LINING = { 0.28f, 0.26f, 0.24f };
	const glm::vec3 CREASE = { 0.74f, 0.74f, 0.72f };	// a pressed line along the body's side, in shadow
	const glm::vec3 DASH_BLACK = { 0.07f, 0.07f, 0.075f };
	const glm::vec3 POD_GREY = { 0.55f, 0.56f, 0.57f };	// the silver pods either side of the gauges
	const glm::vec3 CLOTH = { 0.4f, 0.41f, 0.44f };		// the seats' grey tweed, and the doors' inserts
	const glm::vec3 CLOTH_DARK = { 0.2f, 0.2f, 0.22f };
	const glm::vec3 CARPET = { 0.34f, 0.28f, 0.33f };	// the mauve mats

	// A number in lit segments, as a calculator shows it, for the gauges' faces: in the plane z, its middle at
	// (x, y), its digits `height` tall. (Laid out as the driver reads it: on the cluster, their left is +x.)
	void Digits(MeshBuilder& builder, int number, float x, float y, float z, float height, const glm::vec3& color)
	{
		// Which of the seven segments each digit lights: top, top right, bottom right, bottom, bottom left, top left, middle.
		static const int SEGMENTS[10] = { 0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110, 0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111 };
		std::string text = std::to_string(number);
		const float wide = height * 0.5f, pitch = height * 0.72f, thick = height * 0.16f;
		builder.ResetTransform().SetColor(color);
		for (size_t i = 0; i < text.size(); i++)
		{
			// Across as the driver sees it (to their right), which is -x here.
			float across = (static_cast<float>(i) - (static_cast<float>(text.size()) - 1.f) * 0.5f) * pitch;
			int lit = SEGMENTS[text[i] - '0'];
			auto bar = [&](float u, float v, bool upright) {
				builder.AddBox({ x - (across + u), y + v, z }, upright ? glm::vec3(thick, height * 0.5f, 0.0006f) : glm::vec3(wide, thick, 0.0006f));
			};
			if (lit & 1) bar(0.f, height * 0.5f, false);
			if (lit & 2) bar(wide * 0.5f, height * 0.25f, true);
			if (lit & 4) bar(wide * 0.5f, -height * 0.25f, true);
			if (lit & 8) bar(0.f, -height * 0.5f, false);
			if (lit & 16) bar(-wide * 0.5f, -height * 0.25f, true);
			if (lit & 32) bar(-wide * 0.5f, height * 0.25f, true);
			if (lit & 64) bar(0.f, 0.f, false);
		}
	}

	// The tire's radius, standing: a wheel and its tire are both centred this high, so one sits on the other.
	constexpr float TIRE_RADIUS = 0.29f;
}

void ShopAssets::LoadCarParts()
{
	using namespace CarBody;
	// Each part its rough size and colour, resting on y = 0, built up from boxes and cylinders. A front spring is
	// centred on its strut, so one sits on the other off the car.
	auto add = [&](PartType type, const MeshBuilder& builder) { AddModel(PartTypeName(type), builder); };
	auto box = [&](PartType type, glm::vec3 size, glm::vec3 color) {
		MeshBuilder builder;
		Box(builder, { 0.f, size.y * 0.5f, 0.f }, size, color);
		add(type, builder);
	};
	// Standing up along Y (or a disc lying flat).
	auto upright = [&](PartType type, float radius, float height, glm::vec3 color) {
		MeshBuilder builder;
		Cylinder(builder, At({ 0.f, height * 0.5f, 0.f }), radius, height, color, 16);
		add(type, builder);
	};
	// Lying along X.
	auto lying = [&](PartType type, float radius, float length, glm::vec3 color) {
		MeshBuilder builder;
		Cylinder(builder, AlongX({ 0.f, radius, 0.f }), radius, length, color, 16);
		add(type, builder);
	};

	// Body. The bolt-on panels are cut to the body's lines (CarBody.h), each built about where it bolts or hinges on,
	// so on the car they meet the shell and each other with an even shut line and their faces flush.
	{
		// The shell as a part: it only ever stands as the car, but every part has a model.
		MeshBuilder shell;
		Box(shell, { 0.f, 0.55f, 0.f }, { 4.2f, 0.6f, 1.6f }, BODY_WHITE);
		Box(shell, { -0.15f, 1.05f, 0.f }, { 1.9f, 0.4f, 1.36f }, BODY_WHITE);
		add(PartType::BODY_SHELL, shell);
	}
	// The fenders and doors are handed: the left one (z toward the car's middle is +) is the part's own model, the
	// right one its mirror, put on when it's fitted to that side. `in` is which way z runs into the car.
	for (float side : { -1.f, 1.f })
	{
		const float in = -side;
		{
			// The front fender, from the door's shut line to the nose. Its origin is the bottom of its back edge on the
			// outer skin. The skin is cut round the wheel arch, with a rolled lip; its top turns in level with the hood
			// to the hood's edge, drops, and turns in again as the lip its bolts go down through onto the inner wing.
			MeshBuilder fender;
			const float length = NOSE_X - DOOR_FRONT_X;
			const float archX = FRONT_AXLE_X - DOOR_FRONT_X, archY = AXLE_Y - SILL_Y;
			auto top = [](float x) { return HoodY(x + DOOR_FRONT_X) - SILL_Y; };
			SidePanel(fender, 0.f, length, Arched(0.f, archX, archY, ARCH_RADIUS), top, in * SKIN * 0.5f, SKIN, BODY_WHITE, 48);
			ArchLip(fender, archX, archY, ARCH_RADIUS, in * 0.016f, 0.048f, BODY_WHITE);
			ArchFlare(fender, archX, archY, ARCH_RADIUS, 0.f, side, BODY_WHITE);
			// Its pressed lines: a shoulder under the hood's edge, and the sill's line carried forward to the bumper;
			// the black sill moulding either side of the arch; the round repeater lamp behind the arch; the seam where
			// the nose panel joins; and the splash guard hanging behind the wheel.
			{
				const float fall = std::hypot(NOSE_X - HOOD_REAR_X, BELT_Y - NOSE_Y), cowl = HOOD_REAR_X - DOOR_FRONT_X;
				glm::mat4 slope = glm::rotate(At({ cowl, BELT_Y - SILL_Y, 0.f }), HoodPitch(), glm::vec3(0.f, 0.f, 1.f));
				TurnedBox(fender, slope, { fall * 0.5f, -0.045f, side * 0.002f }, { fall - 0.02f, 0.01f, 0.004f }, CREASE);
				Box(fender, { cowl * 0.5f, BELT_Y - SILL_Y - 0.045f, side * 0.002f }, { cowl, 0.01f, 0.004f }, CREASE);
				Box(fender, { 0.03f, 0.04f, side * 0.004f }, { 0.06f, 0.07f, 0.01f }, TRIM_BLACK);
				Box(fender, { 0.96f, 0.055f, side * 0.004f }, { 0.5f, 0.05f, 0.01f }, TRIM_BLACK);
				Cylinder(fender, AlongZ({ 0.1f, 0.42f, side * 0.004f }), 0.022f, 0.01f, { 0.9f, 0.5f, 0.1f }, 14);
				Cylinder(fender, AlongZ({ 0.1f, 0.42f, side * 0.002f }), 0.027f, 0.008f, TRIM_BLACK, 14);
				Box(fender, { length - 0.2f, 0.27f, side * 0.001f }, { 0.004f, 0.3f, 0.003f }, CREASE);
				Box(fender, { archX - ARCH_RADIUS - 0.012f, -0.02f, in * 0.05f }, { 0.012f, 0.16f, 0.14f }, RUBBER);
			}
			const float cowl = HOOD_REAR_X - DOOR_FRONT_X, beltTop = BELT_Y - SILL_Y, shelf = SIDE - HOOD_HALF;
			Box(fender, { cowl * 0.5f, beltTop - 0.01f, in * shelf * 0.5f }, { cowl, 0.02f, shelf }, BODY_WHITE);
			const float fall = std::hypot(NOSE_X - HOOD_REAR_X, BELT_Y - NOSE_Y);
			glm::mat4 slope = glm::rotate(At({ cowl, beltTop, 0.f }), HoodPitch(), glm::vec3(0.f, 0.f, 1.f));
			TurnedBox(fender, slope, { fall * 0.5f, -0.01f, in * shelf * 0.5f }, { fall, 0.02f, shelf }, BODY_WHITE);
			TurnedBox(fender, slope, { fall * 0.5f, -0.025f, in * (shelf - 0.003f) }, { fall, 0.03f, 0.006f }, BODY_WHITE);
			TurnedBox(fender, slope, { fall * 0.5f, -0.04f, in * (shelf + 0.025f) }, { fall, 0.01f, 0.05f }, BODY_WHITE);
			// The rubbing strip ahead of the arch, in line with the door's, and the side marker lamp.
			Box(fender, { 0.795f, RUB_STRIP_Y - SILL_Y, side * 0.002f }, { 0.17f, 0.012f, 0.004f }, CREASE);
			Box(fender, { NOSE_X - 0.08f - DOOR_FRONT_X, HoodY(NOSE_X - 0.08f) - SILL_Y - 0.06f, side * 0.004f }, { 0.15f, 0.045f, 0.008f }, { 0.7f, 0.33f, 0.08f });
			AddModel(side < 0.f ? std::string(PartTypeName(PartType::FRONT_FENDER)) : ShopModels::FENDER_RIGHT, fender);
		}
		{
			// The door, from the fender's shut line back to the quarter's. Its origin is its hinge line: the bottom of
			// its front edge on the outer skin, so it swings about there. The skin up to the belt, the trim panel inside
			// it (set back from the front edge, clear of the hinge post as it swings), and above the belt the glass in
			// its black frame, leaning in with the rest of the glass, its front edge raked with the windshield.
			MeshBuilder door;
			const float length = DOOR_FRONT_X - DOOR_REAR_X - 2.f * GAP, bottomY = ROCKER_TOP + GAP, height = BELT_Y - bottomY;
			SidePanel(door, -length, 0.f, Level(0.f), Level(height), in * SKIN * 0.5f, SKIN, BODY_WHITE, 4);
			Box(door, { -(length + 0.05f) * 0.5f, height * 0.5f, in * 0.055f }, { length - 0.11f, height - 0.04f, 0.04f }, VINYL);
			Box(door, { -0.72f, 0.3f, in * 0.085f }, { 0.5f, 0.04f, 0.03f }, TRIM_BLACK);				// the armrest
			// The card: a grey cloth insert over the armrest with the lock's button on it, the pull under it, and a map
			// pocket along the bottom.
			Box(door, { -0.68f, 0.43f, in * 0.076f }, { 0.74f, 0.19f, 0.004f }, CLOTH);
			Box(door, { -0.95f, 0.48f, in * 0.08f }, { 0.05f, 0.03f, 0.008f }, TRIM_BLACK);
			Box(door, { -0.72f, 0.27f, in * 0.1f }, { 0.2f, 0.02f, 0.012f }, { 0.13f, 0.13f, 0.14f });
			Box(door, { -0.6f, 0.1f, in * 0.082f }, { 0.6f, 0.1f, 0.016f }, { 0.11f, 0.11f, 0.12f });
			// The inside handle, well back from the hinge so the dashboard's end doesn't hide it with the door shut: a
			// black cup let into the cloth, a bright lever standing out of it (hinged at its front end, a finger pull
			// turned in at the back), and the lock's slide beside it.
			Box(door, { DOOR_HANDLE_X, DOOR_HANDLE_Y, in * 0.079f }, { 0.17f, 0.075f, 0.008f }, TRIM_BLACK);
			Box(door, { DOOR_HANDLE_X, DOOR_HANDLE_Y, in * 0.0815f }, { 0.15f, 0.055f, 0.006f }, { 0.02f, 0.02f, 0.022f });
			Box(door, { DOOR_HANDLE_X + 0.01f, DOOR_HANDLE_Y, in * DOOR_HANDLE_IN }, { 0.11f, 0.03f, 0.012f }, ALLOY);
			Box(door, { DOOR_HANDLE_X - 0.05f, DOOR_HANDLE_Y, in * (DOOR_HANDLE_IN - 0.004f) }, { 0.014f, 0.034f, 0.02f }, ALLOY);
			Cylinder(door, AlongZ({ DOOR_HANDLE_X + 0.062f, DOOR_HANDLE_Y, in * (DOOR_HANDLE_IN - 0.003f) }), 0.012f, 0.016f, { 0.55f, 0.56f, 0.58f }, 12);
			Box(door, { DOOR_HANDLE_X - 0.11f, DOOR_HANDLE_Y, in * 0.082f }, { 0.03f, 0.016f, 0.008f }, { 0.7f, 0.12f, 0.1f });
			Cylinder(door, AlongZ({ -0.48f, 0.2f, in * 0.08f }), 0.022f, 0.012f, TRIM_BLACK, 12);		// the window winder
			auto on = [&](float x, float y) { return glm::vec3(x, y, in * (SIDE - SideZ(y + bottomY))); };
			const glm::vec3 lean = { 0.f, GLASS_Z - ROOF_Z, side * (ROOF_Y - BELT_Y) };
			const float glass = 0.34f, glassTop = height + glass;
			const float rake = (WINDSHIELD_TOP_X - WINDSHIELD_BASE_X) / (ROOF_Y - BELT_Y);			// how far back it leans per metre up
			// The glass is a model of its own, so it can be seen through (ShopWorld keeps it on the door).
			MeshBuilder pane;
			Slab(pane, { on(-length + 0.022f, height), on(-0.05f, height), on(-0.05f + rake * glass, glassTop), on(-length + 0.022f, glassTop) },
				lean, 0.008f, GLASS);
			AddModel(side < 0.f ? ShopModels::DOOR_GLASS : ShopModels::DOOR_GLASS_RIGHT, pane);
			glm::vec3 corner = on(-0.035f + rake * (glass + 0.01f), glassTop + 0.01f);
			Beam(door, on(-0.035f, height), corner, { 0.f, 0.f, 1.f }, 0.03f, 0.03f, TRIM_BLACK);
			Beam(door, corner, on(-length + 0.008f, glassTop + 0.01f), { 0.f, 0.f, 1.f }, 0.025f, 0.03f, TRIM_BLACK);
			Beam(door, on(-length + 0.012f, height), on(-length + 0.012f, glassTop + 0.01f), { 1.f, 0.f, 0.f }, 0.03f, 0.03f, TRIM_BLACK);
			// Outside: the beltline trim, the rubbing strip, the handle in its recess with the lock under it, and the mirror.
			Box(door, { -length * 0.5f, height - 0.008f, side * 0.004f }, { length, 0.016f, 0.008f }, TRIM_BLACK);
			Box(door, { -length * 0.5f, RUB_STRIP_Y - bottomY, side * 0.002f }, { length, 0.012f, 0.004f }, CREASE);
			Box(door, { -length + 0.16f, 0.4f, side * 0.002f }, { 0.13f, 0.04f, 0.004f }, TRIM_BLACK);
			Box(door, { -length + 0.16f, 0.405f, side * 0.007f }, { 0.11f, 0.022f, 0.006f }, ALLOY);
			Cylinder(door, AlongZ({ -length + 0.16f, 0.345f, side * 0.002f }), 0.009f, 0.006f, ALLOY, 10);
			Box(door, { -0.11f, height + 0.03f, side * 0.012f }, { 0.04f, 0.03f, 0.03f }, TRIM_BLACK);
			Box(door, { -0.11f, height + 0.06f, side * 0.07f }, { 0.045f, 0.08f, 0.11f }, TRIM_BLACK);
			Box(door, { -0.134f, height + 0.06f, side * 0.07f }, { 0.004f, 0.066f, 0.096f }, { 0.7f, 0.78f, 0.82f });	// the mirror's glass
			AddModel(side < 0.f ? std::string(PartTypeName(PartType::DOOR)) : ShopModels::DOOR_RIGHT, door);
		}
	}
	{
		// The hood, between the fenders from the cowl to the nose. Its origin is its hinge line: the middle of its back
		// edge, on its top face, and it's built level (on the car it's tipped to the hood line). A bulge down its
		// middle, its nose turned down over the grille, the washer jets, and underneath its bracing and hinge arms.
		MeshBuilder hood;
		const float length = (NOSE_X - HOOD_REAR_X - 2.f * GAP) / std::cos(HoodPitch()), width = 2.f * (HOOD_HALF - GAP);
		// (Its front corners are cut out for the pop-up headlights' lids, which lie in its surface.)
		const float back = length - LAMP_LID_LENGTH / std::cos(HoodPitch()) - GAP, tongue = 2.f * (LAMP_LID_INNER - GAP);
		// Its middle stands a little proud between two creases, over the engine, and runs down into the tongue between
		// the lids; the strips either side lie on the hood line.
		constexpr float RAISED = 0.022f, RAISED_FROM = 0.06f;
		const float half = LAMP_LID_INNER - 0.03f, flat = back - 0.18f, strip = width * 0.5f - half;
		for (float z : { -1.f, 1.f })
			Box(hood, { back * 0.5f, -0.011f, z * (half + strip * 0.5f) }, { back, 0.022f, strip }, BODY_WHITE);
		Box(hood, { RAISED_FROM * 0.5f, -0.011f, 0.f }, { RAISED_FROM, 0.022f, 2.f * half }, BODY_WHITE);
		Box(hood, { (RAISED_FROM + flat) * 0.5f, RAISED - 0.011f, 0.f }, { flat - RAISED_FROM, 0.022f, 2.f * half }, BODY_WHITE);
		Prism(hood, { { flat, RAISED - 0.022f }, { back, -0.022f }, { back, 0.f }, { flat, RAISED } }, -half, half, BODY_WHITE);
		Box(hood, { (back + length) * 0.5f, -0.011f, 0.f }, { length - back, 0.022f, tongue }, BODY_WHITE);
		Box(hood, { length - 0.008f, -0.03f, 0.f }, { 0.016f, 0.04f, tongue }, BODY_WHITE);
		for (float z : { -1.f, 1.f })
			Box(hood, { back * 0.5f, -0.026f, z * (half + strip * 0.5f) }, { back - 0.2f, 0.008f, strip - 0.12f }, { 0.55f, 0.55f, 0.53f });
		for (float z : { -0.6f, 0.6f })
			Box(hood, { 0.09f, -0.03f, z }, { 0.14f, 0.016f, 0.04f }, IRON);
		for (float z : { -0.25f, 0.25f })
			Box(hood, { 0.1f, RAISED + 0.004f, z }, { 0.02f, 0.008f, 0.03f }, TRIM_BLACK);
		add(PartType::HOOD, hood);
	}
	{
		// The trunk lid, between the quarters from the deck to the tail. Its origin is its hinge line: the middle of its
		// front edge, on its top face. Its back edge turns down over the tail panel to the lights, and a black ducktail
		// spoiler sweeps up off its back third, the car's whole width (its ends ride over the quarters' tops), kicked
		// up to a blunt trailing edge that overhangs the tail a little.
		MeshBuilder lid;
		const float length = TRUNK_FRONT_X - TAIL_X - 2.f * GAP, width = 2.f * (TRUNK_HALF - GAP);
		Box(lid, { -length * 0.5f, -0.011f, 0.f }, { length, 0.022f, width }, BODY_WHITE);
		Box(lid, { -length + 0.008f, -0.0575f, 0.f }, { 0.016f, 0.115f, width }, BODY_WHITE);
		Box(lid, { -length * 0.5f, -0.026f, 0.f }, { length - 0.12f, 0.008f, width - 0.3f }, { 0.55f, 0.55f, 0.53f });
		const glm::vec3 SPOILER = { 0.1f, 0.1f, 0.11f };
		constexpr float SPOILER_HALF = SIDE - 0.035f;
		Prism(lid, { { -length + 0.2f, 0.f }, { -length - 0.03f, 0.075f }, { -length - 0.036f, 0.052f }, { -length, 0.f } },
			-SPOILER_HALF, SPOILER_HALF, SPOILER);
		// (Its ends are turned down a little onto the quarters.)
		for (float end : { -1.f, 1.f })
			Prism(lid, { { -length + 0.14f, 0.f }, { -length - 0.03f, 0.06f }, { -length - 0.036f, 0.04f }, { -length, 0.f } },
				end * SPOILER_HALF, end * (SPOILER_HALF + 0.02f), SPOILER);
		add(PartType::TRUNK_LID, lid);
	}
	// The big black urethane bumpers: a beam across the car, wrapped back round the corners along the sides, a rubbing
	// strip in its face and a lip under it. Built across the car with their face toward +z (the front bumper) or -z
	// (the rear, turned the same way on the car), their origin where the beam's back meets the body, 0.04 behind it.
	auto bumper = [&](PartType type, float face) {
		MeshBuilder bumper;
		if (face > 0.f)
		{
			// The front one is deep: a body-coloured band on top under the lamps, the black face under it with the
			// grille's slot between the indicators and the corner lamps, and a chin spoiler jutting out at the bottom.
			const glm::vec3 SLOT = { 0.03f, 0.03f, 0.035f };
			Box(bumper, { 0.f, 0.315f, 0.03f }, { 1.636f, 0.13f, 0.14f }, BODY_WHITE);
			Box(bumper, { 0.f, 0.125f, 0.025f }, { 1.636f, 0.25f, 0.13f }, TRIM_BLACK);
			for (float end : { -1.f, 1.f })
			{
				Box(bumper, { end * 0.808f, 0.315f, -0.19f }, { 0.02f, 0.13f, 0.3f }, BODY_WHITE);
				Box(bumper, { end * 0.808f, 0.125f, -0.19f }, { 0.02f, 0.25f, 0.3f }, TRIM_BLACK);
				Box(bumper, { end * 0.47f, 0.185f, 0.091f }, { 0.17f, 0.075f, 0.004f }, AMBER);
				Box(bumper, { end * 0.65f, 0.185f, 0.091f }, { 0.17f, 0.075f, 0.004f }, { 0.9f, 0.91f, 0.88f });
				Box(bumper, { end * 0.56f, 0.185f, 0.094f }, { 0.006f, 0.075f, 0.002f }, TRIM_BLACK);
			}
			Box(bumper, { 0.f, 0.185f, 0.0905f }, { 0.74f, 0.085f, 0.004f }, SLOT);
			for (int slat = 0; slat < 3; slat++)
				Box(bumper, { 0.f, 0.16f + slat * 0.025f, 0.093f }, { 0.72f, 0.006f, 0.004f }, { 0.2f, 0.2f, 0.21f });
			Box(bumper, { -0.07f, 0.185f, 0.096f }, { 0.17f, 0.04f, 0.004f }, { 0.75f, 0.76f, 0.77f });		// the badge
			Box(bumper, { 0.f, 0.075f, 0.092f }, { 0.22f, 0.045f, 0.004f }, { 0.75f, 0.76f, 0.77f });		// the plate's bracket
			Box(bumper, { 0.f, -0.03f, 0.02f }, { 1.62f, 0.06f, 0.1f }, TRIM_BLACK);
			Box(bumper, { 0.f, -0.07f, 0.07f }, { 1.636f, 0.03f, 0.22f }, TRIM_BLACK);
			for (float end : { -1.f, 1.f })
				Box(bumper, { end * 0.808f, -0.045f, -0.14f }, { 0.02f, 0.09f, 0.2f }, TRIM_BLACK);
			add(type, bumper);
			return;
		}
		// The rear one matches it: a body-coloured band on top, standing a little proud, its top a shelf under the
		// tail lights in line with the sides' rubbing strips; the black face under it with a moulding along it; and
		// a valance tucked under. Both bands wrap round the corners toward the arches. (The plate is up between the
		// tail lights, not on it.)
		Box(bumper, { 0.f, 0.085f, face * 0.03f }, { 1.67f, 0.21f, 0.14f }, TRIM_BLACK);
		Box(bumper, { 0.f, 0.23f, face * 0.035f }, { 1.674f, 0.08f, 0.15f }, BODY_WHITE);
		for (float end : { -1.f, 1.f })
		{
			Box(bumper, { end * 0.8125f, 0.085f, face * -0.19f }, { 0.045f, 0.21f, 0.3f }, TRIM_BLACK);
			Box(bumper, { end * 0.8145f, 0.23f, face * -0.19f }, { 0.045f, 0.08f, 0.3f }, BODY_WHITE);
			Box(bumper, { end * 0.8125f, -0.04f, face * -0.12f }, { 0.04f, 0.05f, 0.16f }, TRIM_BLACK);
		}
		Box(bumper, { 0.f, 0.186f, face * 0.101f }, { 1.66f, 0.008f, 0.004f }, { 0.02f, 0.02f, 0.025f });		// the seam under the band
		Box(bumper, { 0.f, 0.11f, face * 0.101f }, { 1.6f, 0.03f, 0.004f }, { 0.2f, 0.2f, 0.21f });
		Box(bumper, { 0.f, -0.045f, face * 0.01f }, { 1.63f, 0.06f, 0.1f }, TRIM_BLACK);
		Box(bumper, { 0.f, -0.07f, face * 0.03f }, { 1.5f, 0.014f, 0.15f }, TRIM_BLACK);
		add(type, bumper);
	};
	bumper(PartType::FRONT_BUMPER, 1.f);
	bumper(PartType::REAR_BUMPER, -1.f);
	{
		// A pop-up. Built about the hinge it swings up on: the back edge of its lid, on the lid's top face, the lid
		// lying forward from there along +z (on the car it lies in the hood's surface at the nose's corner, and
		// swings up by LAMP_POP). Under the lid's front the lamp's housing hangs, canted so that its face stands
		// square to the road once it's up, with cheeks down either side from the lid to the housing. The lamp's lens
		// is a model of its own, so it can be lit.
		MeshBuilder pod, lens;
		const float wide = HOOD_HALF - LAMP_LID_INNER - 2.f * GAP, deep = LAMP_LID_LENGTH / std::cos(HoodPitch()) - 2.f * GAP;
		// (It tilts up about the back edge of its lid: the housing under the lid's front is canted so that its face,
		// with the big square lamp in it, stands square to the road once it's up, and a black cheek closes each side
		// from the lid down to the housing.)
		const float up = HoodPitch() + LAMP_POP, sine = std::sin(up), cosine = std::cos(up);
		constexpr float FACE = LAMP_FACE, HOUSING = 0.13f, UNDER = -0.022f;
		Box(pod, { 0.f, -0.011f, deep * 0.5f }, { wide, 0.022f, deep }, BODY_WHITE);
		Box(pod, { 0.f, -0.03f, deep - 0.006f }, { wide, 0.016f, 0.012f }, BODY_WHITE);
		// Side on (z forward, y up): the housing's top under the lid, its face down from there, and its bottom back.
		const glm::vec2 topFront = { deep - 0.03f, UNDER }, topBack = { deep - 0.2f, UNDER };
		const glm::vec2 faceBottom = topFront + glm::vec2(-sine, -cosine) * FACE;
		const glm::vec2 bottomBack = faceBottom + glm::vec2(-cosine, sine) * HOUSING;
		const glm::mat4 sideOn = glm::rotate(glm::mat4(1.f), -glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f));
		Prism(pod, { topBack, topFront, faceBottom, bottomBack }, -(wide * 0.5f - 0.012f), wide * 0.5f - 0.012f, TRIM_BLACK, sideOn);
		for (float side : { -1.f, 1.f })
			Prism(pod, { { 0.012f, UNDER }, topFront, faceBottom }, side * (wide * 0.5f - 0.004f) - 0.004f, side * (wide * 0.5f - 0.004f) + 0.004f, TRIM_BLACK, sideOn);
		// The lamp in the housing's face: a dark rim, and the lens in it.
		const glm::vec2 faceMiddle = (topFront + faceBottom) * 0.5f;
		const glm::mat4 face = glm::rotate(At({ 0.f, faceMiddle.y, faceMiddle.x }), up, glm::vec3(1.f, 0.f, 0.f));
		TurnedBox(pod, face, { 0.f, 0.f, 0.002f }, { wide - 0.05f, FACE - 0.03f, 0.004f }, { 0.16f, 0.16f, 0.17f });
		TurnedBox(lens, face, { 0.f, 0.f, 0.006f }, { wide - 0.085f, FACE - 0.065f, 0.006f }, { 0.96f, 0.96f, 0.9f });
		add(PartType::HEADLIGHT, pod);
		AddModel(ShopModels::HEADLIGHT_LENS, lens);
	}
	{
		// The tail light, a long lamp from the garnish out to the body's corner in a black frame. From the corner in:
		// the amber indicator, the red stop and tail lamp, and by the garnish the white reverse lamp over a bright red
		// reflector; the lens ribbed across. Handed: the part's own model is the left one (built along x, its outer
		// end toward +x, which lies toward the car's left once it's turned across), the right one its mirror.
		for (float out : { 1.f, -1.f })
		{
			MeshBuilder lamp;
			const glm::vec3 SMOKED_AMBER = { 0.8f, 0.4f, 0.07f }, DEEP_RED = { 0.6f, 0.05f, 0.04f }, RIB = { 0.05f, 0.03f, 0.03f };
			Box(lamp, { 0.f, 0.0925f, 0.f }, { 0.52f, 0.185f, 0.07f }, TRIM_BLACK);
			Box(lamp, { out * 0.1525f, 0.0925f, 0.f }, { 0.185f, 0.155f, 0.08f }, SMOKED_AMBER);
			Box(lamp, { out * -0.0425f, 0.0925f, 0.f }, { 0.185f, 0.155f, 0.08f }, DEEP_RED);
			Box(lamp, { out * -0.195f, 0.131f, 0.f }, { 0.1f, 0.078f, 0.08f }, { 0.9f, 0.9f, 0.88f });
			Box(lamp, { out * -0.195f, 0.051f, 0.f }, { 0.1f, 0.072f, 0.08f }, { 0.9f, 0.1f, 0.07f });
			for (float y : { 0.066f, 0.119f })
				Box(lamp, { out * 0.055f, y, 0.f }, { 0.38f, 0.004f, 0.083f }, RIB);
			// (The corner's end cap: the lens wraps a little way round onto the body's side.)
			Box(lamp, { out * 0.254f, 0.0925f, 0.01f }, { 0.012f, 0.155f, 0.06f }, SMOKED_AMBER);
			if (out > 0.f)
				add(PartType::TAIL_LIGHT, lamp);
			else
				AddModel(ShopModels::TAIL_LIGHT_RIGHT, lamp);
		}
	}
	{
		// The windshield and the rear window: glass in a black rubber gasket, cut to the opening, wider at the belt
		// than at the roof. Lying flat across the car (x), each with the end that goes forward on the car toward +z:
		// the windshield's bottom, the rear window's top.
		auto glass = [&](PartType type, float halfFront, float halfBack, float length) {
			MeshBuilder pane;
			auto sheet = [&](float grow, float y, float thick, const glm::vec3& color) {
				Slab(pane, { { -halfFront - grow, y, length * 0.5f + grow }, { halfFront + grow, y, length * 0.5f + grow },
					{ halfBack + grow, y, -length * 0.5f - grow }, { -halfBack - grow, y, -length * 0.5f - grow } }, { 0.f, 1.f, 0.f }, thick, color);
			};
			// All glass: the whole part is drawn see-through, and the opening's own edges frame it.
			sheet(0.f, 0.008f, 0.008f, GLASS);
			add(type, pane);
		};
		const float rise = ROOF_Y - BELT_Y;
		glass(PartType::WINDSHIELD, GLASS_Z - 0.028f, ROOF_Z - 0.028f, std::hypot(WINDSHIELD_BASE_X - WINDSHIELD_TOP_X, rise));
		glass(PartType::REAR_WINDOW, ROOF_Z - 0.018f, GLASS_Z - 0.018f, std::hypot(REAR_WINDOW_TOP_X - REAR_WINDOW_BASE_X, rise));
	}
	{
		// The fuel filler's flap, hinged at its front edge (its origin, on its outer face): it lies back along -x.
		MeshBuilder flap;
		Box(flap, { -FLAP_HALF, 0.f, -0.003f }, { 2.f * FLAP_HALF - 0.008f, 2.f * FLAP_HALF - 0.008f, 0.006f }, BODY_WHITE);
		AddModel(ShopModels::FUEL_FLAP, flap);
	}

	// Interior. The dashboard is built across the car (x), the windshield toward +z, the driver at +x.
	{
		MeshBuilder dash;
		// Side on: a deep top sloping down to the windshield, the face toward the seats, the lower edge under it.
		Prism(dash, { { -0.25f, 0.f }, { 0.05f, 0.f }, { 0.25f, 0.2f }, { 0.25f, 0.27f }, { -0.15f, 0.33f }, { -0.25f, 0.3f } }, -0.7f, 0.7f, DASH_BLACK,
			glm::rotate(glm::mat4(1.f), -glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f)));
		const float face = -0.25f;		// the face toward the seats
		// In the car's space, a point of the car's on the dash (it's built across the car, the driver at +x).
		auto fromCar = [](const float point[3]) { return glm::vec3(-point[2], point[1] - DASH_Y, point[0] - DASH_X); };
		// A round vent: a bright ring, dark inside, with its vanes.
		auto roundVent = [&](float x, float y, float radius) {
			Tube(dash, AlongZ({ x, y, face - 0.03f }), radius, radius * 0.82f, 0.012f, TRIM_BLACK, 20);
			Cylinder(dash, AlongZ({ x, y, face - 0.026f }), radius * 0.82f, 0.004f, { 0.02f, 0.02f, 0.02f }, 20);
			for (float vane : { -0.4f, 0.f, 0.4f })
				Box(dash, { x, y + vane * radius, face - 0.03f }, { radius * 1.3f, 0.004f, 0.004f }, { 0.16f, 0.16f, 0.17f });
		};
		// A slatted vent.
		auto vent = [&](float x, float y, float wide, float high) {
			Box(dash, { x, y, face - 0.004f }, { wide, high, 0.01f }, { 0.02f, 0.02f, 0.02f });
			for (int slat = 0; slat < 4; slat++)
				Box(dash, { x, y - high * 0.5f + high * (slat + 0.5f) / 4.f, face - 0.009f }, { wide - 0.006f, 0.004f, 0.004f }, { 0.17f, 0.17f, 0.18f });
		};
		// The binnacle the gauges sit back in, in front of the driver: a hood over them and a cheek either side.
		Box(dash, { 0.37f, 0.318f, face + 0.03f }, { 0.43f, 0.03f, 0.2f }, DASH_BLACK);
		for (float side : { -1.f, 1.f })
			Box(dash, { 0.37f + side * 0.2f, 0.23f, face + 0.02f }, { 0.016f, 0.16f, 0.14f }, DASH_BLACK);
		Box(dash, { 0.37f, 0.148f, face - 0.012f }, { 0.4f, 0.014f, 0.03f }, DASH_BLACK);
		// Either side of it a silver pod: a round vent over a round knob. The knob by the door is the headlights'.
		const glm::vec3 knob = fromCar(HEADLIGHT_KNOB);
		for (float x : { knob.x, 0.37f - (knob.x - 0.37f) })
		{
			Box(dash, { x, 0.23f, face - 0.012f }, { 0.108f, 0.16f, 0.028f }, POD_GREY);
			roundVent(x, 0.268f, 0.034f);
			Cylinder(dash, AlongZ({ x, knob.y, face - 0.034f }), 0.03f, 0.016f, TRIM_BLACK, 20);
			Cylinder(dash, AlongZ({ x, knob.y, face - 0.046f }), 0.022f, 0.016f, { 0.13f, 0.13f, 0.14f }, 16);
			Box(dash, { x, knob.y + 0.012f, face - 0.055f }, { 0.004f, 0.02f, 0.002f }, OFF_WHITE);
		}
		// The middle: two vents over the heater's panel with its slides, the radio, and the console down to the tunnel
		// with three small dials in it. It's set over toward the passenger, between the glovebox and the silver pod on
		// the inner side of the gauges' binnacle, with a gap either side so it clears the pod and the binnacle's cheek.
		const float stackRight = 0.37f - (knob.x - 0.37f) - 0.054f - 0.012f;		// the inner pod's edge, less the gap
		const float stackLeft = -0.2f, stack = stackRight - stackLeft, mid = (stackLeft + stackRight) * 0.5f;
		Box(dash, { mid, 0.14f, face - 0.012f }, { stack, 0.3f, 0.03f }, { 0.1f, 0.1f, 0.105f });
		vent(mid - stack * 0.24f, 0.255f, stack * 0.42f, 0.05f);
		vent(mid + stack * 0.24f, 0.255f, stack * 0.42f, 0.05f);
		Box(dash, { mid, 0.195f, face - 0.029f }, { stack - 0.04f, 0.045f, 0.004f }, { 0.03f, 0.03f, 0.03f });
		for (int slide = 0; slide < 3; slide++)
		{
			Box(dash, { mid, 0.207f - slide * 0.012f, face - 0.032f }, { stack - 0.08f, 0.003f, 0.002f }, slide == 0 ? glm::vec3(0.7f, 0.15f, 0.1f) : glm::vec3(0.25f, 0.4f, 0.7f));
			Box(dash, { mid - 0.05f + slide * 0.045f, 0.207f - slide * 0.012f, face - 0.036f }, { 0.012f, 0.009f, 0.008f }, ALLOY);
		}
		Box(dash, { mid, 0.125f, face - 0.03f }, { stack - 0.06f, 0.05f, 0.006f }, { 0.62f, 0.63f, 0.64f });
		Box(dash, { mid + 0.015f, 0.13f, face - 0.034f }, { 0.08f, 0.022f, 0.002f }, { 0.05f, 0.09f, 0.08f });
		for (float x : { -0.07f, 0.07f })
			Cylinder(dash, AlongZ({ mid + x, 0.118f, face - 0.036f }), 0.011f, 0.01f, TRIM_BLACK, 12);
		Box(dash, { mid, -0.07f, face + 0.02f }, { stack - 0.03f, 0.16f, 0.1f }, { 0.1f, 0.1f, 0.105f });
		for (float x : { -0.06f, 0.f, 0.06f })
		{
			Tube(dash, AlongZ({ mid + x, 0.045f, face - 0.03f }), 0.024f, 0.019f, 0.008f, ALLOY, 16);
			Cylinder(dash, AlongZ({ mid + x, 0.045f, face - 0.028f }), 0.019f, 0.004f, { 0.03f, 0.03f, 0.04f }, 16);
			TurnedBox(dash, glm::rotate(At({ mid + x, 0.045f, face - 0.032f }), 0.6f + x * 9.f, glm::vec3(0.f, 0.f, 1.f)), { 0.f, 0.008f, 0.f }, { 0.002f, 0.015f, 0.001f }, { 0.95f, 0.5f, 0.1f });
		}
		// The passenger's side: the glovebox with its catch, a vent at the door's end, and a ledge over it.
		Box(dash, { -0.43f, 0.13f, face - 0.003f }, { 0.34f, 0.14f, 0.006f }, { 0.1f, 0.1f, 0.105f });
		Box(dash, { -0.43f, 0.185f, face - 0.008f }, { 0.05f, 0.014f, 0.006f }, TRIM_BLACK);
		vent(-0.62f, 0.255f, 0.09f, 0.05f);
		Box(dash, { -0.4f, 0.226f, face - 0.008f }, { 0.4f, 0.008f, 0.016f }, { 0.12f, 0.12f, 0.125f });
		// On top, the demister's slots along the windshield and the speaker's grille.
		for (float x : { -0.4f, 0.f, 0.4f })
			Box(dash, { x, 0.3f, 0.12f }, { 0.26f, 0.004f, 0.02f }, { 0.02f, 0.02f, 0.02f });
		// Under the driver's side: the knee panel, the hood's release hanging below it by the door, and right of the
		// column the hole the ignition switch pushes into (the switch is a part of its own, and the key an item).
		Box(dash, { 0.37f, 0.06f, face + 0.03f }, { 0.4f, 0.12f, 0.03f }, { 0.1f, 0.1f, 0.105f });
		const glm::vec3 release = fromCar(HOOD_RELEASE), barrel = fromCar(IGNITION_BARREL);
		Box(dash, { release.x, release.y + 0.03f, release.z + 0.03f }, { 0.03f, 0.05f, 0.07f }, TRIM_BLACK);
		Box(dash, { release.x, release.y, release.z }, { 0.07f, 0.022f, 0.02f }, TRIM_BLACK);
		Box(dash, { release.x, release.y, release.z - 0.011f }, { 0.04f, 0.012f, 0.002f }, { 0.9f, 0.85f, 0.2f });
		Cylinder(dash, AlongZ({ barrel.x, barrel.y, face - 0.0008f }), 0.021f, 0.002f, { 0.015f, 0.015f, 0.015f }, 16);
		add(PartType::DASHBOARD, dash);
	}
	{
		// The ignition switch, built facing the driver as it sits in the dash (-z toward the seats), about its barrel's middle:
		// the alloy bezel round the lock's black face with the key's slot in it, and behind them (inside the dash) the
		// switch's body and its plug.
		MeshBuilder ignition;
		Cylinder(ignition, AlongZ({ 0.f, 0.f, 0.f }), 0.02f, 0.03f, ALLOY, 16);
		Cylinder(ignition, AlongZ({ 0.f, 0.f, -0.018f }), 0.012f, 0.01f, TRIM_BLACK, 12);
		Box(ignition, { 0.f, 0.f, -0.0232f }, { 0.0025f, 0.012f, 0.001f }, { 0.02f, 0.02f, 0.02f });		// the key's slot
		Cylinder(ignition, AlongZ({ 0.f, 0.f, 0.04f }), 0.016f, 0.05f, { 0.22f, 0.22f, 0.23f }, 14);
		Box(ignition, { 0.f, -0.004f, 0.075f }, { 0.018f, 0.012f, 0.02f }, { 0.1f, 0.1f, 0.1f });
		add(PartType::IGNITION_SWITCH, ignition);
	}
	{
		// Facing the driver (-z on the dash; +x is their left): a flat black panel, the speedo on the left and the tach
		// on the right, white marks and numbers round each (a turn about z is clockwise to the driver, from straight
		// up), the fuel gauge's bar out at the far left and the oil and temperature gauges' bars at the far right, and
		// the warning lamps between. The needles are models of their own, which ShopWorld turns.
		// (Its markings, the numbers, marks and lamps, are built first: they're a model of their own as well, which ShopWorld
		// lights green over the faces with the dash lights on.)
		MeshBuilder cluster;
		const glm::vec3 MARK = { 0.92f, 0.92f, 0.88f }, RED = { 0.9f, 0.12f, 0.1f }, DIM = { 0.2f, 0.21f, 0.2f };
		constexpr float FACE = -0.0505f, PAINT = -0.0512f;
		// A big dial: a mark at every step from nothing up to `top`, a long one with its number at every `numbered`
		// of them, red from `red` on.
		auto dial = [&](float x, float top, float step, int numbered, float shown, float red) {
			int marks = static_cast<int>(std::round(top / step));
			for (int mark = 0; mark <= marks; mark++)
			{
				bool major = mark % numbered == 0;
				float value = mark * step, turn = GAUGE_SWEEP * (static_cast<float>(mark) / marks - 0.5f);
				const glm::vec3& color = value >= red ? RED : MARK;
				float length = GAUGE_BIG * (major ? 0.14f : 0.08f);
				TurnedBox(cluster, glm::rotate(At({ x, GAUGE_Y, PAINT }), turn, glm::vec3(0.f, 0.f, 1.f)), { 0.f, GAUGE_BIG - length * 0.5f, 0.f },
					{ GAUGE_BIG * (major ? 0.045f : 0.028f), length, 0.0006f }, color);
				if (major)
					Digits(cluster, static_cast<int>(std::round(value / shown)), x - std::sin(turn) * GAUGE_BIG * 0.66f, GAUGE_Y + std::cos(turn) * GAUGE_BIG * 0.66f,
						PAINT, GAUGE_BIG * 0.15f, value >= red ? RED : MARK);
			}
			Cylinder(cluster, AlongZ({ x, GAUGE_Y, -0.0575f }), GAUGE_BIG * 0.17f, 0.005f, { 0.02f, 0.02f, 0.02f }, 16);		// the needle's cap
		};
		dial(GAUGE_BIG_X, SPEEDO_TOP, 10.f, 2, 1.f, 1e9f);
		dial(-GAUGE_BIG_X, TACH_TOP, 500.f, 2, 1000.f, 6500.f);
		// The odometer's windows in the speedo, and the main beam's lamp under the tach.
		Box(cluster, { GAUGE_BIG_X, GAUGE_Y + 0.02f, PAINT }, { 0.03f, 0.008f, 0.0006f }, { 0.12f, 0.12f, 0.13f });
		Box(cluster, { GAUGE_BIG_X, GAUGE_Y - 0.022f, PAINT }, { 0.022f, 0.007f, 0.0006f }, { 0.12f, 0.12f, 0.13f });
		Digits(cluster, 86259, GAUGE_BIG_X, GAUGE_Y + 0.02f, PAINT - 0.0004f, 0.005f, { 0.75f, 0.75f, 0.72f });
		Box(cluster, { -GAUGE_BIG_X, GAUGE_Y - 0.045f, PAINT }, { 0.014f, 0.007f, 0.0006f }, { 0.25f, 0.3f, 0.5f });
		// A little gauge: its bar, a mark at each end and the middle, the hot (or empty) end red.
		auto bar = [&](float x, bool redLeft) {
			const float y = GAUGE_Y - GAUGE_SMALL_Y + GAUGE_SMALL;
			Box(cluster, { x, y + 0.004f, PAINT }, { 0.026f, 0.0016f, 0.0006f }, MARK);
			for (float end : { -1.f, 0.f, 1.f })
				Box(cluster, { x + end * 0.0122f, y + 0.001f, PAINT }, { 0.0016f, 0.005f, 0.0006f }, (redLeft ? end > 0.5f : end < -0.5f) ? RED : MARK);
			Box(cluster, { x, GAUGE_Y - GAUGE_SMALL_Y - 0.004f, -0.0565f }, { 0.02f, 0.012f, 0.004f }, { 0.02f, 0.02f, 0.02f });		// over the needle's root
		};
		bar(GAUGE_FUEL_X, true);		// empty is on the driver's left, which is +x
		bar(GAUGE_OIL_X, true);
		bar(GAUGE_TEMP_X, false);		// hot is on their right
		// The warning lamps (unlit), and the turn signals' arrows over the join.
		for (int lamp = 0; lamp < 3; lamp++)
			Box(cluster, { GAUGE_FUEL_X - 0.004f, GAUGE_Y + 0.045f - lamp * 0.013f, PAINT }, { 0.022f, 0.006f, 0.0006f }, lamp == 0 ? glm::vec3(0.45f, 0.08f, 0.07f) : DIM);
		for (float x : { -0.014f, 0.014f })
			Box(cluster, { x, GAUGE_Y + 0.052f, PAINT }, { 0.012f, 0.007f, 0.0006f }, { 0.15f, 0.4f, 0.28f });
		Box(cluster, { GAUGE_OIL_X - 0.012f, GAUGE_Y + 0.022f, PAINT }, { 0.014f, 0.008f, 0.0006f }, DIM);
		MeshBuilder markings = cluster;
		Box(cluster, { 0.f, GAUGE_Y, 0.f }, { 0.38f, 0.14f, 0.1f }, { 0.03f, 0.03f, 0.035f });
		Box(cluster, { 0.f, GAUGE_Y, FACE }, { 0.004f, 0.13f, 0.001f }, { 0.1f, 0.1f, 0.11f });		// the join down its middle
		add(PartType::INSTRUMENT_CLUSTER, cluster);
		AddModel(ShopModels::CLUSTER_MARKINGS, markings, true);

		// A needle: from a little behind its pivot up along +y, as long as a big dial wants (the little ones' are scaled down).
		MeshBuilder needle;
		Box(needle, { 0.f, GAUGE_BIG * 0.37f, 0.f }, { 0.0032f, GAUGE_BIG * 1.1f, 0.0012f }, { 0.96f, 0.9f, 0.62f });
		AddModel(ShopModels::GAUGE_NEEDLE, needle);
	}
	{
		// A bucket seat facing +x: the cushion between its bolsters on a black frame, the back reclined with its own
		// bolsters, and the headrest.
		// Grey tweed, its bolsters and headrest darker, the cloth ribbed across. In two pieces: the base (the cushion on
		// its frame and rails), and the back, which hinges on it (in the car the two are models of their own, so the
		// back can be reclined and tipped forward; loose, the seat is the one model with its back as it comes).
		auto base = [&](MeshBuilder& seat) {
			Box(seat, { 0.02f, 0.03f, 0.f }, { 0.42f, 0.06f, 0.42f }, TRIM_BLACK);
			Box(seat, { 0.02f, 0.1f, 0.f }, { 0.48f, 0.09f, 0.42f }, CLOTH);
			for (int rib = 0; rib < 5; rib++)
				Box(seat, { -0.14f + rib * 0.085f, 0.146f, 0.f }, { 0.012f, 0.004f, 0.38f }, CLOTH_DARK);
			for (float side : { -1.f, 1.f })
			{
				Box(seat, { 0.02f, 0.13f, side * 0.23f }, { 0.48f, 0.12f, 0.06f }, CLOTH_DARK);
				Box(seat, { 0.02f, 0.008f, side * 0.17f }, { 0.56f, 0.016f, 0.03f }, IRON);		// its rails
			}
			// The recliner's knob by the hinge, the hinge's plates, and the slide's bar under the cushion's front.
			// (A knob on each side, so the one seat does for either side of the car: the one by the door is the one used.)
			for (float side : { -1.f, 1.f })
			{
				Cylinder(seat, AlongZ({ SEAT_RECLINER[0], SEAT_RECLINER[1], side * -SEAT_RECLINER[2] }), 0.034f, 0.024f, TRIM_BLACK, 14);
				Box(seat, { SEAT_RECLINER[0], SEAT_RECLINER[1] + 0.022f, side * (0.013f - SEAT_RECLINER[2]) }, { 0.008f, 0.02f, 0.004f }, OFF_WHITE);
			}
			for (float side : { -1.f, 1.f })
				Box(seat, { SEAT_HINGE_X, SEAT_HINGE_Y, side * 0.245f }, { 0.07f, 0.07f, 0.012f }, TRIM_BLACK);
			Box(seat, { SEAT_SLIDE_BAR[0] - 0.02f, SEAT_SLIDE_BAR[1], 0.f }, { 0.018f, 0.018f, 0.32f }, ALLOY);
			for (float side : { -1.f, 1.f })
				Box(seat, { SEAT_SLIDE_BAR[0] - 0.05f, SEAT_SLIDE_BAR[1], side * 0.155f }, { 0.06f, 0.014f, 0.014f }, ALLOY);
		};
		auto back = [&](MeshBuilder& seat, const glm::mat4& hinge) {
			TurnedBox(seat, hinge, { 0.f, 0.28f, 0.f }, { 0.1f, 0.56f, 0.44f }, CLOTH);
			for (int rib = 0; rib < 5; rib++)
				TurnedBox(seat, hinge, { 0.052f, 0.1f + rib * 0.09f, 0.f }, { 0.004f, 0.012f, 0.38f }, CLOTH_DARK);
			for (float side : { -1.f, 1.f })
				TurnedBox(seat, hinge, { 0.04f, 0.26f, side * 0.23f }, { 0.14f, 0.5f, 0.06f }, CLOTH_DARK);
			TurnedBox(seat, hinge, { -0.052f, 0.28f, 0.f }, { 0.004f, 0.5f, 0.4f }, VINYL);			// its vinyl back, with a map pocket
			TurnedBox(seat, hinge, { -0.056f, 0.2f, 0.f }, { 0.006f, 0.2f, 0.3f }, { 0.11f, 0.11f, 0.12f });
			// The headrest on its two posts.
			for (float side : { -1.f, 1.f })
				TurnedBox(seat, hinge, { 0.f, 0.575f, side * 0.06f }, { 0.012f, 0.05f, 0.012f }, ALLOY);
			TurnedBox(seat, hinge, { 0.f, 0.66f, 0.f }, { 0.09f, 0.14f, 0.26f }, CLOTH_DARK);
			// The catch on its shoulder that lets it tip forward.
			for (float side : { -1.f, 1.f })
			{
				TurnedBox(seat, hinge, { SEAT_FOLD_CATCH[0], SEAT_FOLD_CATCH[1], side * (0.012f - SEAT_FOLD_CATCH[2]) }, { 0.05f, 0.07f, 0.02f }, TRIM_BLACK);
				TurnedBox(seat, hinge, { SEAT_FOLD_CATCH[0], SEAT_FOLD_CATCH[1] + 0.012f, side * (0.026f - SEAT_FOLD_CATCH[2]) }, { 0.03f, 0.03f, 0.012f }, { 0.75f, 0.15f, 0.1f });
			}
		};
		MeshBuilder seat, seatBase, seatBack;
		base(seat);
		back(seat, glm::rotate(At({ SEAT_HINGE_X, SEAT_HINGE_Y, 0.f }), SEAT_RECLINE, glm::vec3(0.f, 0.f, 1.f)));
		add(PartType::SEAT, seat);
		base(seatBase);
		AddModel(ShopModels::SEAT_BASE, seatBase);
		back(seatBack, glm::mat4(1.f));		// about its hinge, upright
		AddModel(ShopModels::SEAT_BACK, seatBack);
	}
	{
		MeshBuilder wheel;
		// A black leather wheel with three flat black spokes (on the car +x is the top of the wheel): one out to each
		// side, a little under the hub, and one down, so the gauges show through the top half; the horn's pad in the
		// middle with its badge.
		Tube(wheel, At({ 0.f, 0.015f, 0.f }), 0.19f, 0.165f, 0.032f, { 0.05f, 0.05f, 0.055f }, 32);
		Box(wheel, { -0.03f, 0.018f, 0.f }, { 0.05f, 0.012f, 0.335f }, ARM_GREY);
		Box(wheel, { -0.1f, 0.018f, 0.f }, { 0.15f, 0.012f, 0.05f }, ARM_GREY);
		Cylinder(wheel, At({ -0.015f, 0.028f, 0.f }), 0.045f, 0.03f, TRIM_BLACK, 20);
		Box(wheel, { -0.015f, 0.045f, 0.f }, { 0.022f, 0.004f, 0.022f }, { 0.25f, 0.4f, 0.7f });
		Cylinder(wheel, At({ -0.01f, -0.02f, 0.f }), 0.04f, 0.05f, TRIM_BLACK, 16);		// its boss, back to the column
		add(PartType::STEERING_WHEEL, wheel);
	}

	// Front suspension: the strut stands up, and its spring fits round it.
	{
		MeshBuilder strut;
		Cylinder(strut, At({ 0.f, 0.22f, 0.f }), 0.026f, 0.44f, IRON, 16);
		Cylinder(strut, At({ 0.f, 0.5f, 0.f }), 0.011f, 0.14f, ALLOY, 12);
		Cylinder(strut, At({ 0.f, 0.14f, 0.f }), 0.065f, 0.008f, IRON, 20);
		Cylinder(strut, At({ 0.f, 0.56f, 0.f }), 0.065f, 0.02f, IRON, 20);
		add(PartType::FRONT_STRUT, strut);
	}
	{
		MeshBuilder spring;
		for (int coil = 0; coil < 6; coil++)
			Tube(spring, At({ 0.f, 0.16f + coil * 0.065f, 0.f }), 0.045f, 0.034f, 0.012f, SPRING_RED, 20);	// clear of the tire's inner wall
		add(PartType::FRONT_COIL_SPRING, spring);
	}
	{
		// The knuckle as it goes on the car (+x forward), one for each side: the housing that clamps round the strut's
		// foot, the spindle out to the wheel a tenth of a metre up, and the steering arm low down, forward to the tie rod.
		for (float side : { -1.f, 1.f })
		{
			MeshBuilder knuckle;
			Box(knuckle, { 0.f, 0.13f, 0.f }, { 0.1f, 0.26f, 0.08f }, IRON);
			Cylinder(knuckle, AlongZ({ 0.f, 0.1f, side * 0.05f }), 0.045f, 0.06f, ALLOY, 16);
			// (The arm drops to the tie rod's height, under the engine's sump: the knuckle sits a tenth under the axle.)
			const float arm = TIE_ROD_Y - (AXLE_Y - 0.1f);
			Box(knuckle, { 0.1f, arm, 0.f }, { 0.2f, 0.04f, 0.05f }, IRON);
			Cylinder(knuckle, At({ 0.18f, arm, 0.f }), 0.02f, 0.05f, IRON, 12);
			AddModel(side < 0.f ? std::string(PartTypeName(PartType::STEERING_KNUCKLE)) : ShopModels::KNUCKLE_RIGHT, knuckle);
		}
	}
	box(PartType::LOWER_CONTROL_ARM, { 0.42f, 0.05f, 0.12f }, ARM_GREY);
	lying(PartType::STRUT_ROD, 0.012f, 0.5f, ARM_GREY);
	{
		// The sway bar, built lying along x (across the car; local +z is toward the nose), its bottom on the floor: the bar
		// under the nose, its ends bent back toward the lower arms and a drop link from each down to a tab bolted on the
		// arm's front, and a rubber bushing either side in a steel strap whose flanges bolt up under the chassis rail.
		// (On the car its middle is 0.196 up, 0.35 ahead of the axle: see ShopWorld's PoseOnCar.)
		MeshBuilder bar;
		constexpr float Y = 0.05f, END = 0.455f, BACK = -0.27f, R = 0.011f;
		Cylinder(bar, AlongX({ 0.f, Y, 0.f }), R, 2.f * END, TRIM_BLACK, 12);
		for (float side : { -1.f, 1.f })
		{
			Cylinder(bar, AlongZ({ side * END, Y, BACK * 0.5f }), R, -BACK, TRIM_BLACK, 12);
			for (float z : { 0.f, BACK })
				bar.ResetTransform().SetColor(TRIM_BLACK).AddSphere({ side * END, Y, z }, R, 4, 12);
			// The drop link down to the arm, and its tab.
			Cylinder(bar, At({ side * END, 0.025f, BACK }), 0.007f, 0.05f, IRON, 8);
			Cylinder(bar, At({ side * END, Y, BACK }), 0.014f, 0.012f, RUBBER, 10);
			Box(bar, { side * END, 0.014f, BACK - 0.02f }, { 0.024f, 0.012f, 0.05f }, IRON);
			// The bushing in its strap.
			const float x = side * 0.36f;
			Box(bar, { x, 0.047f, 0.f }, { 0.04f, 0.034f, 0.045f }, RUBBER);
			Box(bar, { x, 0.028f, 0.f }, { 0.034f, 0.004f, 0.053f }, IRON);
			for (float face : { -1.f, 1.f })
			{
				Box(bar, { x, 0.046f, face * 0.0245f }, { 0.034f, 0.036f, 0.004f }, IRON);
				Box(bar, { x, 0.061f, face * 0.045f }, { 0.034f, 0.006f, 0.036f }, IRON);
			}
		}
		add(PartType::FRONT_SWAY_BAR, bar);
	}

	// Rear suspension: the axle across, the diff in its nose.
	{
		MeshBuilder axle;
		Cylinder(axle, AlongX({ 0.f, 0.15f, 0.f }), 0.04f, 1.32f, ARM_GREY, 16);
		Cylinder(axle, AlongZ({ 0.f, 0.15f, 0.f }), 0.13f, 0.12f, ARM_GREY, 20);
		for (float side : { -1.f, 1.f })
			Cylinder(axle, AlongX({ side * 0.67f, 0.15f, 0.f }), 0.08f, 0.012f, IRON, 20);
		// Brackets welded on the tube (on the car its x is across, to the car's left; its z toward the nose): under it
		// for each lower trailing link, on its top at the front for each upper one, and back to the lateral rod's
		// right-hand end.
		for (float side : { -1.f, 1.f })
			for (float cheek : { -0.022f, 0.022f })
			{
				Box(axle, { side * 0.5f + cheek, 0.06f, 0.01f }, { 0.006f, 0.14f, 0.05f }, ARM_GREY);
				Box(axle, { side * 0.28f + cheek, 0.195f, 0.06f }, { 0.006f, 0.07f, 0.05f }, ARM_GREY);
			}
		for (float cheek : { -0.022f, 0.022f })
			Box(axle, { -0.47f + cheek, 0.176f, -0.09f }, { 0.006f, 0.045f, 0.19f }, ARM_GREY);
		// The brake pipes' T on top of the tube beside the diff, which the rear hose comes down to.
		Box(axle, { 0.15f, 0.192f, 0.02f }, { 0.03f, 0.024f, 0.02f }, ALLOY);
		add(PartType::REAR_AXLE, axle);
	}
	{
		MeshBuilder diff;
		Cylinder(diff, AlongZ({ 0.f, 0.14f, 0.f }), 0.13f, 0.16f, IRON, 20);
		Cylinder(diff, AlongZ({ 0.f, 0.14f, 0.17f }), 0.04f, 0.18f, IRON, 14);
		add(PartType::DIFFERENTIAL, diff);
	}
	lying(PartType::LOWER_TRAILING_LINK, 0.016f, 0.62f, TRIM_BLACK);
	lying(PartType::UPPER_TRAILING_LINK, 0.014f, 0.36f, TRIM_BLACK);
	lying(PartType::LATERAL_ROD, 0.016f, 1.0f, TRIM_BLACK);
	{
		MeshBuilder spring;
		for (int coil = 0; coil < 5; coil++)
			Tube(spring, At({ 0.f, 0.02f + coil * 0.06f, 0.f }), 0.065f, 0.053f, 0.011f, SPRING_RED, 20);
		add(PartType::REAR_COIL_SPRING, spring);
	}
	upright(PartType::REAR_SHOCK, 0.024f, 0.45f, { 0.75f, 0.6f, 0.12f });

	// Steering
	{
		MeshBuilder rack;
		// Slim, to pass under the engine's sump: its tube, the boots on its ends, and the pinion's housing standing up
		// off it on the driver's side. A clamp near each end (a rubber bushing round the tube, in a bracket) has a flange
		// on top that bolts up under the pad on the chassis rail over it.
		constexpr float Y = 0.036f;
		Cylinder(rack, AlongX({ 0.f, Y, 0.f }), 0.026f, 0.68f, ALLOY, 14);
		for (float side : { -1.f, 1.f })
		{
			Cylinder(rack, AlongX({ side * 0.38f, Y, 0.f }), 0.022f, 0.08f, RUBBER, 12);	// over the inner tie rods' balls
			Cylinder(rack, AlongX({ side * 0.32f, Y, 0.f }), 0.03f, 0.04f, RUBBER, 14);
			Box(rack, { side * 0.32f, 0.033f, 0.f }, { 0.028f, 0.062f, 0.068f }, IRON);
			Box(rack, { side * 0.32f, 0.063f, 0.f }, { 0.036f, 0.006f, 0.13f }, IRON);
		}
		Cylinder(rack, At({ -0.18f, 0.11f, 0.f }), 0.02f, 0.1f, ALLOY, 12);
		add(PartType::STEERING_RACK, rack);
	}
	{
		// The inner tie rod, along +x from its ball (in the rack's end, under the boot) out to the jam nut and the thread
		// the tie rod end screws onto. (Its bottom on the floor: the ball's middle is 0.016 up.)
		MeshBuilder inner;
		constexpr float UP = 0.016f;
		inner.SetColor(IRON).AddSphere({ 0.f, UP, 0.f }, 0.016f, 6, 12);
		Cylinder(inner, AlongX({ 0.06f, UP, 0.f }), 0.009f, 0.12f, IRON, 10);
		Cylinder(inner, AlongX({ 0.1f, UP, 0.f }), 0.014f, 0.012f, ALLOY, 6);
		add(PartType::TIE_ROD_INNER, inner);
	}
	{
		// The tie rod end, built about its taper stud (up through the steering arm's eye), along +x inward: the ball's
		// housing under the arm, the neck up from it, the body, and its threaded shank that the inner rod goes into.
		// (Its bottom on the floor: the stud's middle, level with the rods, is 0.055 up.)
		MeshBuilder end;
		constexpr float UP = 0.055f;
		Cylinder(end, At({ 0.f, UP, 0.f }), 0.009f, 0.06f, ALLOY, 10);
		Cylinder(end, At({ 0.f, UP - 0.042f, 0.f }), 0.02f, 0.026f, IRON, 14);
		Box(end, { 0.032f, UP - 0.034f, 0.f }, { 0.044f, 0.018f, 0.024f }, IRON);
		Box(end, { 0.064f, UP - 0.014f, 0.f }, { 0.03f, 0.04f, 0.026f }, IRON);
		Cylinder(end, AlongX({ 0.09f, UP, 0.f }), 0.0095f, 0.03f, IRON, 10);
		add(PartType::TIE_ROD_END, end);
	}
	{
		// The column, lying along x (its top end, at the wheel, toward -x), with its shroud under the wheel and a
		// stalk out either side: the indicators', and the wipers'.
		MeshBuilder column;
		Cylinder(column, AlongX({ 0.f, 0.03f, 0.f }), 0.025f, 0.8f, TRIM_BLACK, 12);
		Box(column, { -0.28f, 0.03f, 0.f }, { 0.16f, 0.085f, 0.1f }, DASH_BLACK);
		for (float side : { -1.f, 1.f })
		{
			Box(column, { -0.32f, 0.04f, side * 0.1f }, { 0.016f, 0.014f, 0.11f }, TRIM_BLACK);
			Box(column, { -0.32f, 0.04f, side * 0.16f }, { 0.022f, 0.02f, 0.03f }, { 0.13f, 0.13f, 0.14f });
		}
		add(PartType::STEERING_COLUMN, column);
	}

	// Brakes
	lying(PartType::BRAKE_BOOSTER, 0.11f, 0.13f, TRIM_BLACK);
	{
		// The brake master cylinder, as it hangs off the booster's front face (its +x the car's nose, its bore 0.025 up,
		// +z in toward the engine): the flange against the booster with a nut on each of its two studs, the cast body out
		// forward along the bore, the tandem reservoir clipped onto its two bosses (white, with its level marks, its
		// filler neck and the cap with the level switch's plug on it), and the two circuits' outlets out of the body's
		// inboard side, each with its flare nut and hard line turning down.
		MeshBuilder master;
		const glm::vec3 BOTTLE = { 0.88f, 0.88f, 0.84f };
		const float BORE_Y = 0.025f;
		Box(master, { -0.096f, BORE_Y, 0.f }, { 0.012f, 0.052f, 0.13f }, ALLOY);
		for (float z : { -0.055f, 0.055f })
		{
			Cylinder(master, AlongZ({ -0.096f, BORE_Y, z }), 0.014f, 0.012f, ALLOY, 14);	// the ear's round end
			Cylinder(master, AlongX({ -0.0885f, BORE_Y, z }), 0.0105f, 0.003f, IRON, 14);	// the washer
			Cylinder(master, AlongX({ -0.083f, BORE_Y, z }), 0.0085f, 0.008f, IRON, 6);		// the nut
			Cylinder(master, AlongX({ -0.075f, BORE_Y, z }), 0.004f, 0.01f, IRON, 8);		// the stud's end
		}
		// The body down the bore: a waist behind the flange, the main casting, its ribs, and the closed front end.
		Cylinder(master, AlongX({ -0.078f, BORE_Y, 0.f }), 0.021f, 0.024f, ALLOY, 18);
		Cylinder(master, AlongX({ 0.005f, BORE_Y, 0.f }), 0.025f, 0.15f, ALLOY, 18);
		for (float x : { -0.055f, -0.005f, 0.045f })
			Cylinder(master, AlongX({ x, BORE_Y, 0.f }), 0.0265f, 0.004f, ALLOY, 18);
		Cylinder(master, AlongX({ 0.084f, BORE_Y, 0.f }), 0.021f, 0.01f, ALLOY, 18);
		// The reservoir: its two bosses, the bottle across them, its marks, and its neck and cap.
		for (float x : { -0.035f, 0.04f })
			Cylinder(master, At({ x, BORE_Y + 0.029f, 0.f }), 0.012f, 0.012f, ALLOY, 14);
		Box(master, { 0.003f, 0.083f, 0.f }, { 0.13f, 0.048f, 0.072f }, BOTTLE);
		Box(master, { 0.003f, 0.109f, 0.f }, { 0.12f, 0.006f, 0.062f }, BOTTLE);			// its shoulder
		for (float y : { 0.072f, 0.096f })
			Box(master, { 0.003f, y, 0.0365f }, { 0.07f, 0.0025f, 0.001f }, { 0.15f, 0.15f, 0.16f });
		Cylinder(master, At({ 0.03f, 0.117f, 0.f }), 0.017f, 0.01f, BOTTLE, 18);
		// (Its cap is a thing of its own: ShopWorld::FillerSeat puts it on the neck.)
		// The two outlets, front and rear circuits, out of the inboard side: the brake pipes' unions screw into them.
		for (float x : { -0.035f, 0.035f })
		{
			Cylinder(master, AlongZ({ x, BORE_Y - 0.008f, 0.029f }), 0.0095f, 0.014f, ALLOY, 14);
			Cylinder(master, AlongZ({ x, BORE_Y - 0.008f, 0.0365f }), 0.0045f, 0.002f, { 0.08f, 0.08f, 0.09f }, 10);
		}
		add(PartType::BRAKE_MASTER_CYLINDER, master);
	}
	box(PartType::BRAKE_LINE_SET, { 1.2f, 0.02f, 0.12f }, COPPER);
	upright(PartType::BRAKE_HOSE, 0.006f, 0.35f, RUBBER);
	// What a wheel bolts to: the hub's flange out at the wheel's mounting face, its four studs standing out of it
	// (through the wheel's holes, with thread left over for the nuts). On the front disc and the rear drum alike.
	auto hubAndStuds = [&](MeshBuilder& builder, float from) {
		constexpr float FACE = 0.12f;	// from the brake's own middle out to where the wheel's centre sits against it
		Cylinder(builder, At({ 0.f, (from + FACE) * 0.5f, 0.f }), 0.058f, FACE - from, IRON, 20);
		for (float x : { -1.f, 1.f })
			for (float z : { -1.f, 1.f })
				Cylinder(builder, At({ x * 0.03536f, FACE + 0.042f, z * 0.03536f }), 0.0055f, 0.084f, { 0.72f, 0.73f, 0.75f }, 10);
	};
	{
		MeshBuilder rotor;
		Cylinder(rotor, At({ 0.f, 0.009f, 0.f }), 0.121f, 0.018f, IRON, 24);
		hubAndStuds(rotor, 0.018f);
		add(PartType::BRAKE_ROTOR, rotor);
	}
	box(PartType::BRAKE_PAD_SET, { 0.1f, 0.03f, 0.05f }, LINING);
	{
		// The caliper, as it sits over the front edge of the disc (whose middle is 0.085 behind and 0.05 above its own
		// origin; the disc runs through the slot between its halves, either side of z = 0): two cast halves following the
		// disc's edge round, the bridge over the edge joining them, a piston bore bulging out of each half, the two
		// mounting bosses its bolts go through (at CALIPER_BOLT's places), and the bleed nipple and the hose's banjo on
		// top. The same both sides, so it's built the same either way out.
		MeshBuilder caliper;
		const glm::vec3 CAST = { 0.38f, 0.37f, 0.36f }, BODY = { 0.55f, 0.13f, 0.1f };
		const glm::vec3 disc = { -0.085f, 0.05f, 0.f };
		auto round = [&](float angle, float radius) {
			return glm::rotate(glm::translate(glm::mat4(1.f), disc), angle, glm::vec3(0.f, 0.f, 1.f)) * glm::translate(glm::mat4(1.f), { radius, 0.f, 0.f });
		};
		for (float angle : { -0.42f, 0.f, 0.42f })
		{
			for (float face : { -1.f, 1.f })
				caliper.SetTransform(round(angle, 0.104f)).SetColor(BODY).AddBox({ 0.f, 0.f, face * 0.027f }, { 0.052f, 0.054f, 0.026f });
			caliper.SetTransform(round(angle, 0.137f)).SetColor(BODY).AddBox({}, { 0.018f, 0.056f, 0.08f });
		}
		caliper.ResetTransform();
		for (float face : { -1.f, 1.f })
			for (float angle : { -0.2f, 0.2f })
			{
				const glm::vec3 at = glm::vec3(round(angle, 0.1f) * glm::vec4(0.f, 0.f, 0.f, 1.f));
				Cylinder(caliper, AlongZ(at + glm::vec3(0.f, 0.f, face * 0.043f)), 0.022f, 0.008f, BODY, 16);
			}
		// The mounting bosses, and its slide pins' boots.
		for (float y : { 0.025f, 0.075f })
		{
			Cylinder(caliper, AlongZ({ 0.f, y, 0.f }), 0.013f, 0.08f, CAST, 12);
			for (float face : { -1.f, 1.f })
				Cylinder(caliper, AlongZ({ 0.f, y, face * 0.03f }), 0.009f, 0.02f, RUBBER, 10);
		}
		// The bleed nipple up out of its top, and the hose's banjo union on it.
		const glm::vec3 top = glm::vec3(round(0.5f, 0.12f) * glm::vec4(0.f, 0.f, 0.f, 1.f));
		Cylinder(caliper, At(top + glm::vec3(0.f, 0.02f, -0.012f)), 0.004f, 0.025f, ALLOY, 8);
		Cylinder(caliper, AlongZ(top + glm::vec3(0.f, 0.01f, 0.014f)), 0.01f, 0.012f, ALLOY, 10);
		add(PartType::BRAKE_CALIPER, caliper);
	}
	lying(PartType::WHEEL_CYLINDER, 0.012f, 0.06f, IRON);
	box(PartType::BRAKE_SHOE_SET, { 0.2f, 0.05f, 0.04f }, LINING);
	{
		MeshBuilder drum;
		Cylinder(drum, At({ 0.f, 0.03f, 0.f }), 0.11f, 0.06f, IRON, 24);
		hubAndStuds(drum, 0.06f);
		add(PartType::BRAKE_DRUM, drum);
	}
	{
		// The handbrake's cables, built in the car's space and moved into their part's (its place is PARKING_BRAKE_ORIGIN):
		// the front one from the lever's foot down the tunnel beside the driveshaft and out under the floor to the
		// equalizer, and from that one back out to each rear drum's backing plate, under the floor and over the lower
		// links, with their guides.
		MeshBuilder cable;
		const glm::vec3 origin = { PARKING_BRAKE_ORIGIN[0], PARKING_BRAKE_ORIGIN[1], PARKING_BRAKE_ORIGIN[2] };
		auto run = [&](glm::vec3 a, glm::vec3 b, float radius, const glm::vec3& color) {
			a -= origin;
			b -= origin;
			const glm::vec3 along = b - a, up = { 0.f, 1.f, 0.f };
			const float length = glm::length(along);
			const glm::vec3 dir = along / length, axis = glm::cross(up, dir);
			glm::mat4 transform = glm::translate(glm::mat4(1.f), (a + b) * 0.5f);
			if (glm::length(axis) > 1e-5f)
				transform = glm::rotate(transform, std::acos(glm::clamp(glm::dot(up, dir), -1.f, 1.f)), glm::normalize(axis));
			else if (dir.y < 0.f)
				transform = glm::rotate(transform, glm::pi<float>(), glm::vec3(1.f, 0.f, 0.f));
			Cylinder(cable, transform, radius, length, color, 8);
			cable.ResetTransform().SetColor(color).AddSphere(b, radius, 3, 8);
		};
		constexpr float Y = 0.17f, LANE = 0.075f;
		const glm::vec3 equalizer = { -0.75f, Y, LANE };
		run({ -0.33f, 0.41f, LANE }, { -0.33f, Y + 0.02f, LANE }, 0.004f, TRIM_BLACK);
		run({ -0.33f, Y + 0.02f, LANE }, { -0.36f, Y, LANE }, 0.004f, TRIM_BLACK);
		run({ -0.36f, Y, LANE }, equalizer, 0.004f, TRIM_BLACK);
		cable.ResetTransform().SetColor(IRON).AddBox(equalizer - origin, { 0.03f, 0.012f, 0.06f });
		for (float side : { -1.f, 1.f })
		{
			const glm::vec3 from = equalizer + glm::vec3(-0.01f, 0.f, side * 0.025f);
			const glm::vec3 out = { -0.9f, Y, side * 0.42f }, back = { -1.09f, Y, side * 0.55f }, plate = { -1.13f, 0.24f, side * 0.565f };
			run(from, out, 0.0035f, TRIM_BLACK);
			run(out, back, 0.0035f, TRIM_BLACK);
			run(back, plate, 0.0035f, TRIM_BLACK);
			// A guide clipped up under the floor, and the cable's end fitting at the backing plate.
			cable.ResetTransform().SetColor(IRON).AddBox(out - origin + glm::vec3(0.f, 0.006f, 0.f), { 0.02f, 0.012f, 0.012f });
			run(plate - glm::vec3(0.f, 0.02f, 0.f) * 1.f, plate, 0.007f, IRON);
		}
		add(PartType::PARKING_BRAKE_CABLE, cable);
	}

	// Drivetrain
	{
		// The rubber block on its perch, a steel plate on top, and the bracket off that in to the side of the crankcase
		// (built for the car's right: -z is in toward the engine).
		MeshBuilder mount;
		Box(mount, { 0.f, 0.005f, 0.f }, { 0.12f, 0.01f, 0.09f }, IRON);
		Box(mount, { 0.f, 0.035f, 0.f }, { 0.1f, 0.05f, 0.075f }, RUBBER);
		Box(mount, { 0.f, 0.066f, 0.f }, { 0.12f, 0.012f, 0.09f }, IRON);
		const float reach = ENGINE_MOUNT_Z - 0.1f;		// to the crankcase's side
		Box(mount, { 0.f, 0.084f, -reach * 0.5f - 0.01f }, { 0.07f, 0.024f, reach + 0.02f }, IRON);
		Box(mount, { 0.f, 0.1f, -reach + 0.006f }, { 0.11f, 0.08f, 0.012f }, IRON);
		for (float x : { -0.03f, 0.03f })
			Prism(mount, { { -reach + 0.01f, 0.096f }, { -0.05f, 0.096f }, { -reach + 0.01f, 0.135f } }, x - 0.004f, x + 0.004f, IRON,
				glm::rotate(glm::mat4(1.f), -glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f)));
		add(PartType::ENGINE_MOUNT, mount);
	}
	upright(PartType::CLUTCH_DISC, 0.1f, 0.009f, LINING);
	upright(PartType::PRESSURE_PLATE, 0.11f, 0.04f, ALLOY);
	{
		MeshBuilder bearing;
		Tube(bearing, At({ 0.f, 0.015f, 0.f }), 0.03f, 0.016f, 0.03f, ALLOY, 20);
		add(PartType::RELEASE_BEARING, bearing);
	}
	{
		MeshBuilder gearbox;
		// Front to back along +x, its shafts 0.15 up: the bellhousing (hollow, over the clutch) with its flange against
		// the engine, the gear case, the long tail housing out to the driveshaft, and the lever up off the tail's end
		// with its boot and knob.
		Tube(gearbox, AlongX({ -0.2f, 0.15f, 0.f }), 0.16f, 0.15f, 0.2f, ALLOY, 28);
		// (Its flange in pieces, so as to leave the notch the starter's motor runs forward through, on the car's left.)
		for (int piece = 0; piece < 40; piece++)
		{
			const float angle = glm::two_pi<float>() * (piece + 0.5f) / 40.f;	// from straight up, toward +z (the car's left)
			if (std::abs(angle - glm::radians(97.f)) < glm::radians(16.f))
				continue;
			gearbox.SetTransform(glm::rotate(glm::translate(glm::mat4(1.f), { -0.294f, 0.15f, 0.f }), angle, glm::vec3(1.f, 0.f, 0.f)))
				.SetColor(ALLOY).AddBox({ 0.f, 0.163f, 0.f }, { 0.012f, 0.026f, glm::two_pi<float>() * 0.176f / 40.f * 1.08f });
		}
		gearbox.ResetTransform();
		Cylinder(gearbox, AlongX({ -0.105f, 0.15f, 0.f }), 0.16f, 0.01f, ALLOY, 28);
		Box(gearbox, { 0.07f, 0.15f, 0.f }, { 0.34f, 0.2f, 0.18f }, ALLOY);
		for (float x : { -0.03f, 0.07f, 0.17f })
			Box(gearbox, { x, 0.15f, 0.f }, { 0.012f, 0.22f, 0.2f }, ALLOY);			// the case's ribs
		// The slave cylinder's two bosses cast out of the bellhousing's (car's) right side, low down, its flange's ears
		// against their front faces and its bolts into them (here the car's right is -z, its nose -x: see the slave's
		// pose and CLUTCH_SLAVE_BOLT), the cylinder's body between them; and a web under them back onto the housing.
		for (float dy : { -0.124f, -0.056f })
		{
			const float surface = std::sqrt(0.16f * 0.16f - dy * dy) - 0.012f, outer = 0.184f;
			Box(gearbox, { -0.162f, 0.15f + dy, -(surface + outer) * 0.5f }, { 0.018f, 0.024f, outer - surface }, ALLOY);
			Cylinder(gearbox, AlongX({ -0.162f, 0.15f + dy, -0.169f }), 0.013f, 0.018f, ALLOY, 14);
		}
		Box(gearbox, { -0.157f, 0.012f, -0.12f }, { 0.01f, 0.012f, 0.08f }, ALLOY);
		// And the starter's two on its left (+z here), just ahead of the starter's flange: STARTER_BOLT comes forward
		// through the flange's ears into them, the motor running on forward between them.
		for (float dy : { -0.085f, 0.035f })
		{
			const float surface = std::sqrt(0.16f * 0.16f - dy * dy) - 0.012f, outer = 0.214f;
			Box(gearbox, { -0.186f, 0.15f + dy, (surface + outer) * 0.5f }, { 0.018f, 0.026f, outer - surface }, ALLOY);
			Cylinder(gearbox, AlongX({ -0.186f, 0.15f + dy, 0.2f }), 0.015f, 0.018f, ALLOY, 14);
		}
		Cylinder(gearbox, AlongX({ 0.44f, 0.15f, 0.f }), 0.055f, 0.4f, ALLOY, 18);
		Cylinder(gearbox, AlongX({ 0.632f, 0.15f, 0.f }), 0.045f, 0.016f, IRON, 18);	// the output flange
		Box(gearbox, { 0.59f, 0.215f, 0.f }, { 0.07f, 0.03f, 0.07f }, ALLOY);
		Cylinder(gearbox, At({ 0.59f, 0.255f, 0.f }), 0.035f, 0.05f, RUBBER, 12);
		// (On the car the lever's a model of its own, which moves through the gate with the gear: see below.)
		MeshBuilder body = gearbox;
		Cylinder(gearbox, At({ 0.59f, 0.35f, 0.f }), 0.008f, 0.2f, ALLOY, 10);
		gearbox.SetColor(TRIM_BLACK).AddSphere({ 0.59f, 0.46f, 0.f }, 0.022f, 6, 10);
		add(PartType::TRANSMISSION, gearbox);
		AddModel(ShopModels::TRANSMISSION_BODY, body);
		// The gear lever, from its ball in the gearbox's tail up through the boot to its knob, about the ball.
		MeshBuilder lever;
		Cylinder(lever, At({ 0.f, 0.1f, 0.f }), 0.008f, 0.2f, ALLOY, 10);
		lever.SetColor(TRIM_BLACK).AddSphere({ 0.f, 0.205f, 0.f }, 0.022f, 6, 10);
		AddModel(ShopModels::GEAR_LEVER, lever);
	}
	box(PartType::TRANSMISSION_MOUNT, { 0.22f, 0.06f, 0.08f }, RUBBER);
	{
		// The clutch's master cylinder, as it sits on the firewall (its +x the car's nose, the firewall 0.05 behind its
		// middle): the flange bolted to the firewall with a nut on each of its two studs, the cast body out forward of it,
		// the white fluid reservoir standing on top with its level marks and its cap, and the outlet's union out of the
		// front with the flare nut and the hard line turning down toward the slave.
		MeshBuilder master;
		const glm::vec3 BOTTLE = { 0.88f, 0.88f, 0.84f };
		Box(master, { -0.045f, 0.f, 0.f }, { 0.008f, 0.04f, 0.09f }, ALLOY);
		for (float z : { -0.034f, 0.034f })
		{
			Cylinder(master, AlongZ({ -0.045f, 0.f, z }), 0.012f, 0.005f, ALLOY, 14);		// the ear's round end, flat to the wall
			Cylinder(master, AlongX({ -0.037f, 0.f, z }), 0.0075f, 0.008f, IRON, 6);		// its nut
			Cylinder(master, AlongX({ -0.0405f, 0.f, z }), 0.0095f, 0.002f, IRON, 14);	// its washer
			Cylinder(master, AlongX({ -0.03f, 0.f, z }), 0.0035f, 0.008f, IRON, 8);		// the stud's end
		}
		Cylinder(master, AlongX({ 0.01f, 0.f, 0.f }), 0.018f, 0.11f, ALLOY, 18);
		Cylinder(master, AlongX({ 0.066f, 0.f, 0.f }), 0.016f, 0.006f, ALLOY, 18);		// the closed front end
		for (float x : { -0.02f, 0.015f, 0.045f })
			Cylinder(master, AlongX({ x, 0.f, 0.f }), 0.0195f, 0.003f, ALLOY, 18);		// casting ribs
		// The reservoir's boss on the body and the bottle on it: its level marks, and the cap with its grip ribs.
		Cylinder(master, At({ 0.f, 0.021f, 0.f }), 0.012f, 0.01f, ALLOY, 14);
		Cylinder(master, At({ 0.f, 0.052f, 0.f }), 0.024f, 0.052f, BOTTLE, 20);
		for (float y : { 0.04f, 0.066f })
			Box(master, { 0.f, y, -0.0242f }, { 0.016f, 0.003f, 0.001f }, { 0.15f, 0.15f, 0.16f });
		Cylinder(master, At({ 0.f, 0.082f, 0.f }), 0.018f, 0.008f, BOTTLE, 18);			// the neck
		// (Its cap is a thing of its own: ShopWorld::FillerSeat puts it on the neck.)
		// The outlet: a union out of the front end's underside, the pipe to the slave cylinder off it.
		Cylinder(master, At({ 0.05f, -0.021f, 0.f }), 0.008f, 0.01f, ALLOY, 14);
		Cylinder(master, At({ 0.05f, -0.029f, 0.f }), 0.0072f, 0.008f, IRON, 6);
		add(PartType::CLUTCH_MASTER_CYLINDER, master);
	}
	{
		// The clutch's slave cylinder, as it sits on the bellhousing's right side (its +x the car's nose, -z in toward
		// the bellhousing): a cast body along x, its flange with two bolt ears against the housing, the rubber boot out
		// of its front and the pushrod out of that to the release fork, the line's union into its top with the flare
		// nut and a stub of the hard line, and the bleed nipple behind it with its cap.
		MeshBuilder slave;
		Cylinder(slave, AlongX({ 0.f, 0.f, 0.f }), 0.017f, 0.08f, ALLOY, 18);
		Cylinder(slave, AlongX({ -0.042f, 0.f, 0.f }), 0.0185f, 0.006f, ALLOY, 18);			// the closed end
		for (float x : { -0.025f, 0.f, 0.025f })
			Cylinder(slave, AlongX({ x, 0.f, 0.f }), 0.0178f, 0.003f, ALLOY, 18);				// casting ribs
		// The flange, across the body near its front, standing out above and below it to the ears.
		Box(slave, { 0.026f, 0.f, -0.006f }, { 0.01f, 0.09f, 0.03f }, ALLOY);
		for (float y : { -0.034f, 0.034f })
		{
			Cylinder(slave, AlongX({ 0.026f, y, -0.006f }), 0.011f, 0.01f, ALLOY, 14);		// the ear's round end
			Cylinder(slave, AlongX({ 0.026f, y, -0.006f }), 0.0045f, 0.0104f, { 0.08f, 0.08f, 0.09f }, 10);	// its bolt hole (the bolts are CLUTCH_SLAVE_BOLT)
		}
		// The boot, stepped down from the bore to round the rod, and the rod with its rounded end in the fork.
		Cylinder(slave, AlongX({ 0.05f, 0.f, 0.f }), 0.0145f, 0.012f, RUBBER, 16);
		Cylinder(slave, AlongX({ 0.061f, 0.f, 0.f }), 0.011f, 0.01f, RUBBER, 16);
		Cylinder(slave, AlongX({ 0.07f, 0.f, 0.f }), 0.0075f, 0.008f, RUBBER, 14);
		Cylinder(slave, AlongX({ 0.092f, 0.f, 0.f }), 0.0042f, 0.045f, IRON, 10);
		slave.SetColor(IRON).AddSphere({ 0.115f, 0.f, 0.f }, 0.0055f, 6, 10);
		// The union boss on top: the pipe from the master cylinder screws into it.
		Cylinder(slave, At({ -0.012f, 0.021f, 0.f }), 0.0085f, 0.012f, ALLOY, 14);
		Cylinder(slave, At({ -0.012f, 0.0275f, 0.f }), 0.004f, 0.002f, { 0.08f, 0.08f, 0.09f }, 10);
		// The bleed nipple on the closed end's top, angled back, with its rubber cap.
		glm::mat4 bleed = glm::rotate(At({ -0.034f, 0.02f, 0.f }), 0.5f, glm::vec3(0.f, 0.f, 1.f));
		Cylinder(slave, bleed, 0.0055f, 0.01f, ALLOY, 10);
		Cylinder(slave, bleed * At({ 0.f, 0.009f, 0.f }), 0.004f, 0.008f, IRON, 6);
		Cylinder(slave, bleed * At({ 0.f, 0.017f, 0.f }), 0.0042f, 0.007f, RUBBER, 10);
		add(PartType::CLUTCH_SLAVE_CYLINDER, slave);
	}
	{
		// From the gearbox's tail to the diff's nose, along X about its middle, its axis r up: the tube, a universal joint
		// at each end (the tube's yoke, the spider, the other yoke), the slip yoke into the gearbox at the front (+X) and
		// the flange to the diff at the back, and a balance weight welded on the tube (so it's seen to turn).
		MeshBuilder shaft;
		const float r = 0.035f, length = CarBody::ENGINE_X - 0.19f, half = length * 0.5f;
		Cylinder(shaft, AlongX({ 0.f, r, 0.f }), r, length - 0.13f, TRIM_BLACK, 16);
		for (float end : { -1.f, 1.f })
		{
			const float x = end * (half - 0.05f);
			// The tube's yoke: its two ears either side (across Z), round the spider's cross arms.
			Cylinder(shaft, AlongX({ x - end * 0.025f, r, 0.f }), r * 0.9f, 0.02f, TRIM_BLACK, 14);
			for (float s : { -1.f, 1.f })
				Box(shaft, { x - end * 0.004f, r, s * 0.027f }, { 0.036f, 0.022f, 0.01f }, TRIM_BLACK);
			// The spider: a cross, one arm up and down, the other across.
			Box(shaft, { x, r, 0.f }, { 0.016f, 0.062f, 0.016f }, IRON);
			Box(shaft, { x, r, 0.f }, { 0.016f, 0.016f, 0.062f }, IRON);
			// The other yoke's ears, up and down, and what it's on: the slip yoke's sleeve into the gearbox's tail, or
			// the flange bolted to the diff's.
			for (float s : { -1.f, 1.f })
				Box(shaft, { x + end * 0.004f, r + s * 0.027f, 0.f }, { 0.036f, 0.01f, 0.022f }, IRON);
			if (end > 0.f)
				Cylinder(shaft, AlongX({ half - 0.012f, r, 0.f }), 0.022f, 0.05f, IRON, 14);
			else
				Cylinder(shaft, AlongX({ -half + 0.02f, r, 0.f }), 0.045f, 0.012f, IRON, 16);
		}
		Box(shaft, { 0.22f, 2.f * r + 0.003f, 0.f }, { 0.03f, 0.007f, 0.026f }, IRON);
		add(PartType::DRIVESHAFT, shaft);
	}

	// Fuel
	{
		// The tank (across the car, so its x is the car's -z and its z the car's x, about its middle at -1.72), and its
		// outlet stub down out of its underside near the front, through the boot floor (CarBody's FUEL_TANK_OUTLET).
		// A pressed steel shell with rounded edges, its two halves' welded seam flange standing out round its middle, swages
		// pressed across its top, a boss at each corner for its bolts (FUEL_TANK_BOLT), the sender's ring and locking screws
		// (FUEL_SENDER sits in it), a drain plug underneath at the back, and the filler neck out of its right-hand side (its
		// -x) up to the cap in the quarter (FLAP_X, FLAP_Y), with its breather beside it.
		MeshBuilder tank;
		const glm::vec3 SHELL = { 0.1f, 0.1f, 0.11f }, SEAM = { 0.14f, 0.14f, 0.15f };
		constexpr float HX = 0.4f, H = 0.25f, HZ = 0.26f, R = 0.03f;
		Box(tank, { 0.f, H * 0.5f, 0.f }, { 2.f * HX, H - 2.f * R, 2.f * HZ - 2.f * R }, SHELL);
		Box(tank, { 0.f, H * 0.5f, 0.f }, { 2.f * HX - 2.f * R, H - 2.f * R, 2.f * HZ }, SHELL);
		Box(tank, { 0.f, H * 0.5f, 0.f }, { 2.f * HX - 2.f * R, H, 2.f * HZ - 2.f * R }, SHELL);
		for (float sx : { -1.f, 1.f })
			for (float sz : { -1.f, 1.f })
			{
				Cylinder(tank, At({ sx * (HX - R), H * 0.5f, sz * (HZ - R) }), R, H - 2.f * R, SHELL, 12);
				for (float y : { R, H - R })
					tank.ResetTransform().SetColor(SHELL).AddSphere({ sx * (HX - R), y, sz * (HZ - R) }, R, 4, 12);
			}
		for (float y : { R, H - R })
			for (float s : { -1.f, 1.f })
			{
				Cylinder(tank, AlongX({ 0.f, y, s * (HZ - R) }), R, 2.f * HX - 2.f * R, SHELL, 12);
				Cylinder(tank, AlongZ({ s * (HX - R), y, 0.f }), R, 2.f * HZ - 2.f * R, SHELL, 12);
			}
		// The seam flange.
		constexpr float SEAM_Y = 0.15f, LIP = 0.018f;
		for (float s : { -1.f, 1.f })
		{
			Box(tank, { 0.f, SEAM_Y, s * (HZ + LIP * 0.5f - 0.004f) }, { 2.f * HX + 2.f * LIP, 0.005f, LIP + 0.008f }, SEAM);
			Box(tank, { s * (HX + LIP * 0.5f - 0.004f), SEAM_Y, 0.f }, { LIP + 0.008f, 0.005f, 2.f * HZ }, SEAM);
		}
		// Swages across the top (clear of the sender's ring and the bolts).
		for (float z : { -0.2f, -0.07f })
			Box(tank, { 0.f, H + 0.002f, z }, { 2.f * HX - 0.14f, 0.006f, 0.035f }, SHELL);
		// The bolts' bosses, with a washer face each.
		for (float sx : { -1.f, 1.f })
			for (float sz : { -1.f, 1.f })
			{
				Cylinder(tank, At({ sx * 0.37f, H + 0.004f, sz * 0.23f }), 0.022f, 0.008f, SEAM, 14);
				Cylinder(tank, At({ sx * 0.37f, H + 0.009f, sz * 0.23f }), 0.014f, 0.002f, ALLOY, 12);
			}
		// The sender's ring (the sender's at the car's -1.6, z 0.18: here x -0.18, z 0.08) and its screws round it.
		{
			const glm::vec3 sender = { -0.18f, H, -1.6f - FUEL_TANK_X };
			Tube(tank, At(sender + glm::vec3(0.f, 0.005f, 0.f)), 0.062f, 0.04f, 0.01f, SEAM, 24);
			for (int i = 0; i < 6; ++i)
			{
				const float a = glm::two_pi<float>() * (i + 0.5f) / 6.f;
				Cylinder(tank, At(sender + glm::vec3(std::cos(a) * 0.052f, 0.012f, std::sin(a) * 0.052f)), 0.005f, 0.004f, ALLOY, 8);
			}
		}
		// The outlet stub down through the boot floor, with its hose clamp's band where the hose goes on.
		{
			const glm::vec3 outlet = { -FUEL_TANK_OUTLET[2], 0.f, FUEL_TANK_OUTLET[0] - FUEL_TANK_X };
			const float drop = 0.24f - FUEL_TANK_OUTLET[1];
			Cylinder(tank, At(outlet + glm::vec3(0.f, -drop * 0.5f, 0.f)), 0.011f, drop, ALLOY, 10);
			Cylinder(tank, At(outlet + glm::vec3(0.f, -0.003f, 0.f)), 0.022f, 0.006f, SEAM, 14);	// its welded collar
		}
		// The drain plug, under the back on the left.
		Cylinder(tank, At({ 0.22f, -0.006f, -0.2f }), 0.018f, 0.012f, SEAM, 14);
		Cylinder(tank, At({ 0.22f, -0.016f, -0.2f }), 0.011f, 0.01f, ALLOY, 6);
		// The filler neck: a steel spout out of the side near the top, the rubber hose angled up and out to the steel pipe
		// in the pocket behind the cap, a clamp at each joint. And the breather's thin pipe up beside it.
		{
			const glm::vec3 up = { 0.f, 1.f, 0.f };
			auto segment = [&](glm::vec3 a, glm::vec3 b, float radius, const glm::vec3& color, int segments) {
				const glm::vec3 along = b - a, dir = glm::normalize(along), axis = glm::cross(up, dir);
				glm::mat4 transform = glm::translate(glm::mat4(1.f), (a + b) * 0.5f);
				if (glm::length(axis) > 1e-5f)
					transform = glm::rotate(transform, std::acos(glm::clamp(glm::dot(up, dir), -1.f, 1.f)), glm::normalize(axis));
				Cylinder(tank, transform, radius, glm::length(along), color, segments);
			};
			const float neckZ = FLAP_X - FUEL_TANK_X;
			const glm::vec3 spout = { -HX + 0.01f, 0.19f, neckZ }, spoutEnd = { -HX - 0.06f, 0.215f, neckZ };
			const glm::vec3 pipeEnd = { -(SIDE - SKIN) + 0.005f, FLAP_Y - 0.24f, neckZ }, pipe = pipeEnd + glm::vec3(0.1f, -0.03f, 0.f);
			segment(spout, spoutEnd, 0.03f, SHELL, 16);
			segment(spoutEnd - glm::vec3(0.02f, 0.007f, 0.f), pipe + glm::vec3(0.02f, -0.006f, 0.f), 0.036f, RUBBER, 18);
			segment(pipe, pipeEnd, 0.03f, SHELL, 16);
			for (const glm::vec3& at : { spoutEnd - glm::vec3(0.008f, 0.003f, 0.f), pipe + glm::vec3(0.008f, -0.002f, 0.f) })
				segment(at - glm::vec3(0.006f, 0.002f, 0.f), at + glm::vec3(0.006f, 0.002f, 0.f), 0.039f, ALLOY, 18);
			const glm::vec3 vent = { -HX + 0.05f, H, neckZ + 0.07f }, ventTop = { -HX + 0.05f, H + 0.06f, neckZ + 0.07f };
			segment(vent, ventTop, 0.006f, ALLOY, 8);
			segment(ventTop, pipe + glm::vec3(0.f, 0.04f, 0.045f), 0.006f, RUBBER, 8);
		}
		add(PartType::FUEL_TANK, tank);
	}
	upright(PartType::FUEL_SENDER, 0.04f, 0.25f, ALLOY);
	box(PartType::FUEL_LINE_SET, { 1.2f, 0.02f, 0.08f }, ALLOY);
	{
		// The fuel filter, standing in its clip on the firewall (its -x): a clear canister between two crimped metal
		// ends, the pleated paper element showing through it, a hose stub out of each end (in at the bottom, out at
		// the top, FUEL_FILTER_HEIGHT apart) and an arrow up its side for which way the fuel goes.
		MeshBuilder filter;
		const float top = CarBody::FUEL_FILTER_HEIGHT;
		const glm::vec3 CLEAR = { 0.86f, 0.87f, 0.84f }, PAPER = { 0.82f, 0.66f, 0.3f }, BRASS = { 0.78f, 0.62f, 0.28f };
		Cylinder(filter, At({ 0.f, 0.012f, 0.f }), 0.0045f, 0.024f, BRASS, 10);
		Cylinder(filter, At({ 0.f, 0.006f, 0.f }), 0.0058f, 0.004f, BRASS, 10);					// the stubs' barbs
		Cylinder(filter, At({ 0.f, top - 0.012f, 0.f }), 0.0045f, 0.024f, BRASS, 10);
		Cylinder(filter, At({ 0.f, top - 0.006f, 0.f }), 0.0058f, 0.004f, BRASS, 10);
		Cylinder(filter, At({ 0.f, 0.027f, 0.f }), 0.016f, 0.006f, ALLOY, 16);						// the ends, stepped out to the body
		Cylinder(filter, At({ 0.f, 0.034f, 0.f }), 0.0275f, 0.008f, ALLOY, 20);
		Cylinder(filter, At({ 0.f, top - 0.027f, 0.f }), 0.016f, 0.006f, ALLOY, 16);
		Cylinder(filter, At({ 0.f, top - 0.034f, 0.f }), 0.0275f, 0.008f, ALLOY, 20);
		Cylinder(filter, At({ 0.f, top * 0.5f, 0.f }), 0.0255f, top - 0.076f, CLEAR, 20);
		// The element inside, its pleats standing proud of the canister as ribs all round.
		for (int pleat = 0; pleat < 14; pleat++)
		{
			glm::mat4 turned = glm::rotate(At({ 0.f, top * 0.5f, 0.f }), glm::two_pi<float>() * pleat / 14.f, glm::vec3(0.f, 1.f, 0.f));
			Box(filter, glm::vec3(turned * glm::vec4(0.0245f, 0.f, 0.f, 1.f)), { 0.0045f, top - 0.088f, 0.0045f }, PAPER);
		}
		// Its clip: a sprung band round the middle, and the arm back to the firewall with its screw.
		Cylinder(filter, At({ 0.f, top * 0.5f, 0.f }), 0.0285f, 0.012f, TRIM_BLACK, 20);
		Box(filter, { -0.036f, top * 0.5f, 0.f }, { 0.022f, 0.012f, 0.024f }, TRIM_BLACK);
		Box(filter, { -0.048f, top * 0.5f, 0.f }, { 0.003f, 0.04f, 0.03f }, TRIM_BLACK);
		Cylinder(filter, AlongX({ -0.0455f, top * 0.5f + 0.013f, 0.f }), 0.004f, 0.003f, IRON, 6);
		// The arrow, on the side toward the engine.
		Box(filter, { 0.f, 0.03f, -0.0282f }, { 0.003f, 0.006f, 0.001f }, { 0.1f, 0.1f, 0.1f });
		Box(filter, { 0.f, 0.0345f, -0.0282f }, { 0.007f, 0.003f, 0.001f }, { 0.1f, 0.1f, 0.1f });
		add(PartType::FUEL_FILTER, filter);
	}

	// Electrical
	{
		// The battery, along x (on the car its +x the nose, +z in toward the engine): a moulded case with rounded corners,
		// ribbed down its long sides, the ledge round its foot the tray's hold-down catches, and its lid over the top's
		// edge. On the lid, its six cells' vent caps in two strips, the charge eye, and the two lead posts on their pads
		// (positive at the back with its red ring and +, negative at the front with the ground strap's clamp on it: the
		// strap goes off its top, see ShopWorldCarWiring's Anchor). The maker's label on the side toward the engine.
		MeshBuilder battery;
		const glm::vec3 CASE = { 0.12f, 0.12f, 0.13f }, LID = { 0.17f, 0.17f, 0.18f }, LEAD = { 0.55f, 0.56f, 0.58f };
		const glm::vec3 RED = { 0.75f, 0.12f, 0.1f }, LABEL = { 0.13f, 0.32f, 0.62f }, LIGHT = { 0.9f, 0.9f, 0.88f };
		constexpr float HX = 0.115f, HZ = 0.085f, R = 0.01f, TOP = 0.165f;
		Box(battery, { 0.f, TOP * 0.5f, 0.f }, { 2.f * HX, TOP, 2.f * HZ - 2.f * R }, CASE);
		Box(battery, { 0.f, TOP * 0.5f, 0.f }, { 2.f * HX - 2.f * R, TOP, 2.f * HZ }, CASE);
		for (float sx : { -1.f, 1.f })
			for (float sz : { -1.f, 1.f })
				Cylinder(battery, At({ sx * (HX - R), TOP * 0.5f, sz * (HZ - R) }), R, TOP, CASE, 10);
		// The foot's ledge, and the ribs.
		for (float side : { -1.f, 1.f })
		{
			Box(battery, { 0.f, 0.006f, side * (HZ + 0.004f) }, { 2.f * HX - 0.01f, 0.012f, 0.008f }, CASE);
			for (float x : { -0.075f, -0.025f, 0.025f, 0.075f })
				Box(battery, { x, 0.085f, side * (HZ + 0.0012f) }, { 0.006f, 0.13f, 0.0024f }, CASE);
		}
		// The lid, its lip over the case's top edge.
		Box(battery, { 0.f, TOP + 0.0075f, 0.f }, { 2.f * HX + 0.004f, 0.015f, 2.f * HZ + 0.004f }, LID);
		Box(battery, { 0.f, TOP + 0.016f, 0.f }, { 2.f * HX - 0.03f, 0.002f, 2.f * HZ - 0.02f }, LID);
		// The vent caps: two strips of three, along its middle between the posts.
		for (float z : { -0.035f, 0.035f })
		{
			Box(battery, { 0.f, TOP + 0.019f, z }, { 0.11f, 0.004f, 0.03f }, CASE);
			for (float x : { -0.035f, 0.f, 0.035f })
			{
				Cylinder(battery, At({ x, TOP + 0.022f, z }), 0.0085f, 0.003f, CASE, 12);
				Box(battery, { x, TOP + 0.0237f, z }, { 0.012f, 0.0006f, 0.0015f }, { 0.06f, 0.06f, 0.06f });
			}
		}
		Cylinder(battery, At({ 0.f, TOP + 0.0175f, 0.f }), 0.0065f, 0.003f, CASE, 12);
		Cylinder(battery, At({ 0.f, TOP + 0.0192f, 0.f }), 0.004f, 0.0006f, { 0.2f, 0.7f, 0.25f }, 12);	// the charge eye, green
		// The posts, each on its pad: tapered lead, a coloured ring round the foot of each, and its sign by it.
		for (float sx : { -1.f, 1.f })
		{
			const float x = sx * 0.08f;
			const bool positive = sx < 0.f;
			Cylinder(battery, At({ x, TOP + 0.019f, 0.f }), 0.015f, 0.008f, LID, 14);
			Cylinder(battery, At({ x, TOP + 0.0235f, 0.f }), 0.0125f, 0.0012f, positive ? RED : TRIM_BLACK, 14);
			Cylinder(battery, At({ x, TOP + 0.03f, 0.f }), 0.0088f, 0.012f, LEAD, 12);
			Cylinder(battery, At({ x, TOP + 0.039f, 0.f }), 0.0078f, 0.006f, LEAD, 12);
			Box(battery, { x + sx * 0.022f, TOP + 0.0172f, 0.05f }, { 0.012f, 0.0006f, 0.0028f }, positive ? RED : LIGHT);
			if (positive)
				Box(battery, { x + sx * 0.022f, TOP + 0.0172f, 0.05f }, { 0.0028f, 0.0006f, 0.012f }, RED);
		}
		// The ground strap's clamp on the negative post: its ring, the pinch bolt through its ears, and the barrel up off
		// it the strap's crimped into.
		{
			const glm::vec3 post = { 0.08f, TOP + 0.03f, 0.f };
			Tube(battery, At(post), 0.012f, 0.0089f, 0.014f, LEAD, 14);
			Box(battery, { post.x - 0.0155f, post.y, 0.f }, { 0.008f, 0.012f, 0.012f }, LEAD);
			Cylinder(battery, AlongZ({ post.x - 0.0165f, post.y, 0.f }), 0.0025f, 0.022f, IRON, 6);
			Cylinder(battery, AlongZ({ post.x - 0.0165f, post.y, 0.0095f }), 0.0045f, 0.004f, IRON, 6);
			Cylinder(battery, At({ post.x, post.y + 0.0135f, 0.f }), 0.0055f, 0.013f, LEAD, 10);
		}
		// The label.
		Box(battery, { 0.f, 0.09f, HZ + 0.0026f }, { 0.14f, 0.08f, 0.0008f }, LABEL);
		Box(battery, { 0.f, 0.1f, HZ + 0.003f }, { 0.12f, 0.012f, 0.0008f }, LIGHT);
		Box(battery, { -0.03f, 0.075f, HZ + 0.003f }, { 0.06f, 0.006f, 0.0008f }, LIGHT);
		add(PartType::BATTERY, battery);
	}
	{
		// The harness's battery lead's end: the clamp that goes round the positive post (its axis up, round the origin),
		// pinched by its bolt, the lead coming into its barrel along +x, a red rubber boot over the barrel.
		MeshBuilder clamp;
		const glm::vec3 LEAD = { 0.55f, 0.56f, 0.58f }, RED = { 0.75f, 0.12f, 0.1f };
		Tube(clamp, At({ 0.f, 0.f, 0.f }), 0.012f, 0.0089f, 0.014f, LEAD, 14);
		Box(clamp, { 0.0155f, 0.f, 0.f }, { 0.008f, 0.012f, 0.012f }, LEAD);
		Cylinder(clamp, AlongZ({ 0.0165f, 0.f, 0.f }), 0.0025f, 0.022f, IRON, 6);
		Cylinder(clamp, AlongZ({ 0.0165f, 0.f, 0.0095f }), 0.0045f, 0.004f, IRON, 6);
		Box(clamp, { -0.017f, 0.f, 0.f }, { 0.012f, 0.008f, 0.01f }, LEAD);
		Cylinder(clamp, AlongX({ -0.028f, 0.f, 0.f }), 0.0058f, 0.014f, LEAD, 10);
		Cylinder(clamp, AlongX({ -0.03f, 0.f, 0.f }), 0.0085f, 0.018f, RED, 12);
		Cylinder(clamp, AlongX({ -0.042f, 0.f, 0.f }), 0.0065f, 0.008f, RED, 12);
		AddModel(ShopModels::BATTERY_CLAMP, clamp, true);
	}
	// The looms: a taped trunk with a plug at each end and branches off it.
	auto loom = [&](PartType type, float length, int branches) {
		MeshBuilder harness;
		Cylinder(harness, AlongX({ 0.f, 0.018f, 0.f }), 0.018f, length, TRIM_BLACK, 10);
		for (float side : { -1.f, 1.f })
			Box(harness, { side * (length * 0.5f + 0.015f), 0.018f, 0.f }, { 0.03f, 0.03f, 0.045f }, OFF_WHITE);
		for (int branch = 0; branch < branches; branch++)
		{
			float x = length * ((branch + 1.f) / (branches + 1.f) - 0.5f);
			Cylinder(harness, AlongZ({ x, 0.014f, (branch % 2 == 0 ? 1.f : -1.f) * 0.05f }), 0.009f, 0.1f, TRIM_BLACK, 8);
		}
		add(type, harness);
	};
	loom(PartType::MAIN_HARNESS, 1.2f, 5);

	box(PartType::FUSE_BOX, { 0.2f, 0.05f, 0.12f }, OFF_WHITE);
	{
		// The ignition coil, standing in its clamp on the inner wing (its +z): an oil-filled can, crimped at the top
		// round a bakelite cap; the high tension tower in the cap's middle with the king lead's rubber boot on it, and
		// the two low tension terminals either side of it, a stud and nut each, marked + and -; the clamp's band round
		// the can, pinched by a bolt, on a foot bolted to the wing.
		MeshBuilder coil;
		const glm::vec3 CAN = { 0.11f, 0.12f, 0.14f }, BAKELITE = { 0.2f, 0.11f, 0.07f }, BRASS = { 0.78f, 0.62f, 0.28f };
		Cylinder(coil, At({ 0.f, 0.004f, 0.f }), 0.028f, 0.008f, CAN, 20);							// the rolled bottom
		Cylinder(coil, At({ 0.f, 0.062f, 0.f }), 0.03f, 0.11f, CAN, 24);
		Cylinder(coil, At({ 0.f, 0.119f, 0.f }), 0.0315f, 0.006f, ALLOY, 24);						// the crimp
		Cylinder(coil, At({ 0.f, 0.127f, 0.f }), 0.027f, 0.012f, BAKELITE, 20);
		Cylinder(coil, At({ 0.f, 0.136f, 0.f }), 0.02f, 0.008f, BAKELITE, 18);
		// The tower, and the lead's boot over it with a stub of the lead.
		Cylinder(coil, At({ 0.f, 0.149f, 0.f }), 0.0105f, 0.02f, BAKELITE, 14);
		Cylinder(coil, At({ 0.f, 0.166f, 0.f }), 0.0125f, 0.022f, RUBBER, 14);
		Cylinder(coil, At({ 0.f, 0.181f, 0.f }), 0.009f, 0.01f, RUBBER, 12);
		// The terminals: a brass stud with its nut and washer, and the mark moulded in the cap by each.
		for (float side : { -1.f, 1.f })
		{
			Cylinder(coil, At({ side * 0.019f, 0.137f, 0.f }), 0.0055f, 0.004f, BAKELITE, 10);
			Cylinder(coil, At({ side * 0.019f, 0.1405f, 0.f }), 0.0048f, 0.002f, BRASS, 10);
			Cylinder(coil, At({ side * 0.019f, 0.1435f, 0.f }), 0.0042f, 0.004f, BRASS, 6);
			Cylinder(coil, At({ side * 0.019f, 0.148f, 0.f }), 0.002f, 0.006f, BRASS, 8);
			Box(coil, { side * 0.019f, 0.1335f, -0.012f }, { 0.006f, 0.001f, 0.0015f }, OFF_WHITE);
			if (side > 0.f)
				Box(coil, { side * 0.019f, 0.1335f, -0.012f }, { 0.0015f, 0.001f, 0.006f }, OFF_WHITE);
		}
		// The label round the can.
		Cylinder(coil, At({ 0.f, 0.03f, 0.f }), 0.0303f, 0.026f, { 0.75f, 0.14f, 0.1f }, 24);
		Cylinder(coil, At({ 0.f, 0.03f, 0.f }), 0.0305f, 0.004f, OFF_WHITE, 24);
		// The clamp: its band, the two ears pinched together by the bolt, and the foot on the wing with its two bolts.
		Cylinder(coil, At({ 0.f, 0.078f, 0.f }), 0.0325f, 0.024f, ALLOY, 24);
		Box(coil, { 0.f, 0.078f, -0.039f }, { 0.008f, 0.024f, 0.016f }, ALLOY);
		Cylinder(coil, AlongX({ 0.f, 0.078f, -0.041f }), 0.0035f, 0.018f, IRON, 6);
		Box(coil, { 0.f, 0.078f, 0.036f }, { 0.03f, 0.024f, 0.012f }, ALLOY);
		Box(coil, { 0.f, 0.078f, 0.043f }, { 0.09f, 0.03f, 0.003f }, ALLOY);
		for (float x : { -0.034f, 0.034f })
			Cylinder(coil, AlongZ({ x, 0.078f, 0.0395f }), 0.0055f, 0.004f, IRON, 6);
		add(PartType::IGNITION_COIL, coil);
	}
	{
		// The starter, along x beside the bellhousing on the car's left (its +x the nose), its axis 0.045 up: the cast drive
		// end at the back with the pinion out of its nose, the flange behind the motor with its two bolt ears (STARTER_BOLT's
		// heads sit on their backs, the bolts going forward into the bosses on the bellhousing), the painted motor yoke with
		// its two long through bolts, the brush end cap at the front with its bearing boss, and the solenoid riding on top:
		// its can, its end cap with the battery cable's stud and the small switch terminal (the harness's plug goes on top,
		// see ShopWorldCarWiring), and the copper strap down into the motor.
		MeshBuilder starter;
		constexpr float AXIS = 0.045f, YOKE = 0.04f;
		const glm::vec3 CAST = { 0.62f, 0.63f, 0.65f }, COPPER = { 0.72f, 0.42f, 0.24f }, HOLE = { 0.08f, 0.08f, 0.09f };
		// The drive end and its nose, stepped down toward the pinion.
		Cylinder(starter, AlongX({ -0.105f, AXIS, 0.f }), 0.034f, 0.03f, CAST, 18);
		Cylinder(starter, AlongX({ -0.13f, AXIS, 0.f }), 0.026f, 0.02f, CAST, 16);
		Cylinder(starter, AlongX({ -0.143f, AXIS, 0.f }), 0.02f, 0.006f, CAST, 16);
		Cylinder(starter, AlongX({ -0.152f, AXIS, 0.f }), 0.006f, 0.014f, IRON, 10);					// the shaft
		Cylinder(starter, AlongX({ -0.155f, AXIS, 0.f }), 0.0115f, 0.01f, IRON, 9);					// the pinion (nine teeth)
		for (int tooth = 0; tooth < 9; tooth++)
		{
			const float a = glm::two_pi<float>() * tooth / 9.f;
			Box(starter, { -0.155f, AXIS + std::sin(a) * 0.0125f, std::cos(a) * 0.0125f }, { 0.01f, 0.004f, 0.004f }, IRON);
		}
		// The flange and its ears.
		Box(starter, { -0.084f, AXIS, 0.f }, { 0.012f, 0.12f, 0.05f }, CAST);
		for (float side : { -1.f, 1.f })
		{
			Cylinder(starter, AlongX({ -0.084f, AXIS + side * 0.06f, 0.f }), 0.014f, 0.012f, CAST, 14);
			Cylinder(starter, AlongX({ -0.084f, AXIS + side * 0.06f, 0.f }), 0.0055f, 0.0124f, HOLE, 10);
		}
		// The yoke, its band where the drive end meets it, and the brush end cap.
		Cylinder(starter, AlongX({ 0.01f, AXIS, 0.f }), YOKE, 0.176f, TRIM_BLACK, 20);
		Cylinder(starter, AlongX({ -0.074f, AXIS, 0.f }), YOKE + 0.002f, 0.008f, CAST, 20);
		Cylinder(starter, AlongX({ 0.106f, AXIS, 0.f }), YOKE - 0.002f, 0.016f, CAST, 20);
		Cylinder(starter, AlongX({ 0.12f, AXIS, 0.f }), 0.016f, 0.012f, CAST, 14);
		Cylinder(starter, AlongX({ 0.127f, AXIS, 0.f }), 0.008f, 0.003f, IRON, 10);
		// The through bolts down its sides, from the end cap back to the drive end, with their heads on the cap.
		for (float side : { -1.f, 1.f })
		{
			Cylinder(starter, AlongX({ 0.01f, AXIS - 0.012f, side * (YOKE + 0.002f) }), 0.003f, 0.19f, IRON, 6);
			Cylinder(starter, AlongX({ 0.116f, AXIS - 0.012f, side * (YOKE - 0.006f) }), 0.005f, 0.006f, IRON, 6);
		}
		// The solenoid on top, on a saddle off the drive end.
		constexpr float SOL = AXIS + YOKE + 0.022f;
		Box(starter, { -0.03f, AXIS + YOKE + 0.004f, 0.f }, { 0.03f, 0.016f, 0.03f }, CAST);
		Cylinder(starter, AlongX({ -0.005f, SOL, 0.f }), 0.022f, 0.1f, ALLOY, 18);
		Cylinder(starter, AlongX({ 0.05f, SOL, 0.f }), 0.02f, 0.01f, TRIM_BLACK, 16);				// its end cap
		Cylinder(starter, AlongX({ 0.061f, SOL + 0.008f, 0.f }), 0.004f, 0.014f, COPPER, 8);			// the battery stud
		Cylinder(starter, AlongX({ 0.058f, SOL + 0.008f, 0.f }), 0.0065f, 0.004f, IRON, 6);			// its nut
		Cylinder(starter, AlongX({ 0.059f, SOL - 0.01f, 0.f }), 0.003f, 0.01f, COPPER, 8);			// the motor's stud
		Box(starter, { 0.064f, (SOL - 0.01f + AXIS + YOKE) * 0.5f, 0.f }, { 0.003f, SOL - 0.01f - AXIS - YOKE + 0.004f, 0.01f }, COPPER);
		Cylinder(starter, At({ 0.f, SOL + 0.024f, 0.f }), 0.006f, 0.006f, TRIM_BLACK, 10);			// the switch terminal
		Cylinder(starter, At({ 0.f, SOL + 0.03f, 0.f }), 0.0025f, 0.008f, COPPER, 8);
		add(PartType::STARTER, starter);
	}

	// Cooling
	{
		MeshBuilder radiator;
		const glm::vec3 TANK = { 0.2f, 0.2f, 0.22f };	// its tanks, outlets and neck: black-painted brass
		Box(radiator, { 0.f, 0.2f, 0.f }, { 0.66f, 0.36f, 0.04f }, TRIM_BLACK);
		Box(radiator, { 0.f, 0.395f, 0.f }, { 0.68f, 0.05f, 0.06f }, TANK);
		Box(radiator, { 0.f, 0.015f, 0.f }, { 0.68f, 0.03f, 0.06f }, TANK);
		// (Fitted, its +x is the car's left and its -z the engine's side.) Its two outlets stand out of its tanks toward
		// the engine, both on the car's right: the top one for the hose from the thermostat's housing, the bottom one
		// for the hose to the water pump. (ShopWorld's PoseOnCar puts the hoses' ends on them.)
		for (auto [x, y] : { std::pair{ -0.22f, 0.395f }, std::pair{ -0.25f, 0.015f } })
		{
			Cylinder(radiator, AlongZ({ x, y, -0.052f }), 0.018f, 0.05f, TANK, 14);
			Cylinder(radiator, AlongZ({ x, y, -0.074f }), 0.0205f, 0.006f, TANK, 14);		// the bead the hose's clip sits behind
		}
		// The filler neck and its pressure cap, on the top tank: the cap's two ears, and the small pipe out of the neck's side.
		Cylinder(radiator, At({ 0.12f, 0.43f, 0.f }), 0.021f, 0.022f, TANK, 16);
		// (Its cap is a thing of its own: ShopWorld::FillerSeat puts it on the neck.)
		// The overflow tank it comes with: a white bottle beside the core on the car's left, behind it, its cap, a level line, and the
		// thin hose over to it from the filler neck.
		const glm::vec3 BOTTLE = { 0.88f, 0.88f, 0.84f };
		Box(radiator, { 0.255f, 0.28f, -0.13f }, { 0.075f, 0.24f, 0.1f }, BOTTLE);
		Box(radiator, { 0.255f, 0.33f, -0.181f }, { 0.05f, 0.004f, 0.002f }, { 0.15f, 0.15f, 0.16f });
		Box(radiator, { 0.255f, 0.24f, -0.181f }, { 0.05f, 0.004f, 0.002f }, { 0.15f, 0.15f, 0.16f });
		Cylinder(radiator, At({ 0.255f, 0.41f, -0.13f }), 0.02f, 0.02f, TRIM_BLACK, 14);
		Box(radiator, { 0.255f, 0.28f, -0.055f }, { 0.03f, 0.1f, 0.05f }, TANK);		// its bracket, off the radiator's side
		Cylinder(radiator, AlongX({ 0.19f, 0.432f, -0.03f }), 0.005f, 0.13f, RUBBER, 8);
		Cylinder(radiator, AlongZ({ 0.255f, 0.432f, -0.08f }), 0.005f, 0.105f, RUBBER, 8);
		Cylinder(radiator, At({ 0.255f, 0.427f, -0.13f }), 0.005f, 0.014f, RUBBER, 8);
		Box(radiator, { 0.13f, 0.432f, -0.02f }, { 0.02f, 0.008f, 0.03f }, TANK);
		add(PartType::RADIATOR, radiator);
	}
	{
		// A radiator hose: moulded rubber, bowed between its two ends (0.386 m apart along X, either side of the middle: what
		// both of the car's are between their outlets, see PoseOnCar), a clip round each. Both hoses are this one: PoseOnCar
		// turns it to run from the radiator's outlet to the engine's.
		MeshBuilder hose;
		constexpr float RADIUS = 0.021f, HALF = 0.193f, BOW = 0.035f;
		constexpr int PIECES = 12;
		auto along = [&](float t) { float u = 2.f * t - 1.f; return glm::vec3(u * HALF, RADIUS + BOW * (1.f - u * u), 0.f); };
		for (int piece = 0; piece < PIECES; piece++)
		{
			glm::vec3 from = along(piece / static_cast<float>(PIECES)), to = along((piece + 1) / static_cast<float>(PIECES));
			glm::vec3 run = to - from;
			glm::mat4 lie = glm::rotate(At((from + to) * 0.5f), std::atan2(run.y, run.x), glm::vec3(0.f, 0.f, 1.f)) * AlongX({ 0.f, 0.f, 0.f });
			Cylinder(hose, lie, RADIUS, glm::length(run) * 1.12f, RUBBER, 12);
			if (piece == 0 || piece == PIECES - 1)
				Cylinder(hose, lie, RADIUS + 0.003f, 0.012f, { 0.7f, 0.71f, 0.73f }, 12);
		}
		add(PartType::RADIATOR_HOSE, hose);
	}
	{
		MeshBuilder fan;
		Tube(fan, At({ 0.f, 0.03f, 0.f }), 0.17f, 0.16f, 0.06f, TRIM_BLACK, 28);
		for (int blade = 0; blade < 4; blade++)
		{
			fan.SetTransform(glm::rotate(At({ 0.f, 0.03f, 0.f }), blade * glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f))).SetColor(TRIM_BLACK);
			fan.AddBox({ 0.08f, 0.f, 0.f }, { 0.15f, 0.01f, 0.05f });
			fan.ResetTransform();
		}
		// Its motor behind the blades (+y here, toward the engine on the car), held in the shroud's ring by four struts
		// between the blades: the hub the blades are on, the motor's can and its end cap, the harness's socket on the
		// cap's face (ShopWorldCarWiring's PlugSocket puts the plug there).
		Cylinder(fan, At({ 0.f, 0.03f, 0.f }), 0.032f, 0.022f, TRIM_BLACK, 16);
		Cylinder(fan, At({ 0.f, 0.07f, 0.f }), 0.045f, 0.056f, { 0.3f, 0.3f, 0.32f }, 18);
		Cylinder(fan, At({ 0.f, 0.1f, 0.f }), 0.04f, 0.008f, { 0.6f, 0.61f, 0.63f }, 18);
		for (int strut = 0; strut < 4; strut++)
		{
			fan.SetTransform(glm::rotate(At({ 0.f, 0.07f, 0.f }), (strut + 0.5f) * glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f))).SetColor(TRIM_BLACK);
			fan.AddBox({ 0.104f, 0.f, 0.f }, { 0.12f, 0.008f, 0.014f });
			fan.ResetTransform();
		}
		add(PartType::COOLING_FAN, fan);
	}

	// What the fluids are sold in, for the store: a jug with a handle and cap, and a jerry can for fuel. Built light,
	// so the store's tint colours them like what's inside.
	{
		MeshBuilder jug;
		Box(jug, { 0.f, 0.11f, 0.f }, { 0.16f, 0.22f, 0.09f }, { 0.95f, 0.95f, 0.95f });
		Box(jug, { -0.03f, 0.235f, 0.f }, { 0.08f, 0.03f, 0.07f }, { 0.95f, 0.95f, 0.95f });
		Cylinder(jug, At({ -0.03f, 0.26f, 0.f }), 0.018f, 0.03f, { 0.2f, 0.2f, 0.22f }, 12);
		Tube(jug, AlongZ({ 0.045f, 0.2f, 0.f }), 0.045f, 0.03f, 0.03f, { 0.95f, 0.95f, 0.95f }, 12);
		Box(jug, { 0.f, 0.1f, 0.0455f }, { 0.12f, 0.1f, 0.002f }, { 0.25f, 0.25f, 0.28f });	// the label
		AddModel("Fluid Jug", jug);

		MeshBuilder can;
		Box(can, { 0.f, 0.2f, 0.f }, { 0.34f, 0.4f, 0.16f }, { 0.95f, 0.95f, 0.95f });
		for (float x : { -0.07f, 0.f, 0.07f })
			Box(can, { x, 0.44f, 0.f }, { 0.025f, 0.08f, 0.05f }, { 0.95f, 0.95f, 0.95f });
		Box(can, { 0.f, 0.48f, 0.f }, { 0.19f, 0.02f, 0.05f }, { 0.95f, 0.95f, 0.95f });
		Cylinder(can, glm::rotate(At({ 0.15f, 0.43f, 0.f }), -0.6f, glm::vec3(0.f, 0.f, 1.f)), 0.022f, 0.08f, { 0.3f, 0.3f, 0.32f }, 12);
		for (float z : { -0.081f, 0.081f })
			Box(can, { 0.f, 0.2f, z }, { 0.26f, 0.3f, 0.002f }, { 0.8f, 0.8f, 0.8f });		// the pressed X
		AddModel("Fuel Can", can);
	}

	// Exhaust. Each half is built in the car's own space and moved into its part's (see CarBody.h for the route, which
	// keeps clear of the engine's mount, the crossmember, the arm, the starter, the axle and the floor), so the pipes
	// meet: the engine's downpipe to the front pipe at EXHAUST_JOINT, the converter's tail to the pipe on to the silencer.
	{
		// A pipe along a curve (a cubic Bézier through four points), a straight length at a time, its joints rounded.
		auto bend = [&](MeshBuilder& builder, const glm::vec3& offset, std::array<glm::vec3, 4> p, float radius, const glm::vec3& color, int pieces = 8) {
			auto at = [&](float t) {
				float u = 1.f - t;
				return u * u * u * p[0] + 3.f * u * u * t * p[1] + 3.f * u * t * t * p[2] + t * t * t * p[3] + offset;
			};
			for (int i = 0; i < pieces; i++)
			{
				glm::vec3 a = at(static_cast<float>(i) / pieces), b = at(static_cast<float>(i + 1) / pieces), run = b - a;
				float length = glm::length(run);
				if (length < 1e-5f)
					continue;
				glm::vec3 dir = run / length, up = { 0.f, 1.f, 0.f }, axis = glm::cross(up, dir);
				glm::mat4 turn = glm::length(axis) < 1e-5f ? (dir.y < 0.f ? glm::rotate(glm::mat4(1.f), glm::pi<float>(), glm::vec3(1.f, 0.f, 0.f)) : glm::mat4(1.f))
					: glm::rotate(glm::mat4(1.f), std::acos(glm::clamp(glm::dot(up, dir), -1.f, 1.f)), glm::normalize(axis));
				Cylinder(builder, At((a + b) * 0.5f) * turn, radius, length, color, 12);
				builder.ResetTransform().SetColor(color).AddSphere(b, radius, 4, 12);
			}
		};
		auto straight = [&](MeshBuilder& builder, const glm::vec3& offset, const glm::vec3& a, const glm::vec3& b, float radius, const glm::vec3& color) {
			bend(builder, offset, { a, glm::mix(a, b, 1.f / 3.f), glm::mix(a, b, 2.f / 3.f), b }, radius, color, 1);
		};
		const glm::vec3 PIPE = { 0.42f, 0.4f, 0.38f };
		constexpr float R = EXHAUST_PIPE_RADIUS;
		const glm::vec3 joint = { EXHAUST_JOINT[0], EXHAUST_JOINT[1], EXHAUST_JOINT[2] };
		const float catFront = CATALYST[0] + CATALYST_HALF, catBack = CATALYST[0] - CATALYST_HALF;

		// The converter, and the front pipe up to the joint: its flange there (the downpipe's bolts come up through it),
		// down past the engine's mount, back over the crossmember, and down under the floor to the converter's nose.
		MeshBuilder cat;
		const glm::vec3 toCat = -glm::vec3(CATALYST[0], CATALYST[1], CATALYST[2]);
		Cylinder(cat, At(joint + toCat + glm::vec3(0.f, -0.006f, 0.f)), 0.034f, 0.008f, IRON, 20);
		straight(cat, toCat, joint + glm::vec3(0.f, -0.01f, 0.f), { joint.x, 0.24f, joint.z }, R, PIPE);
		bend(cat, toCat, { glm::vec3(joint.x, 0.24f, joint.z), { joint.x, 0.215f, joint.z }, { 1.33f, 0.215f, joint.z }, { 1.26f, 0.215f, -0.245f } }, R, PIPE);
		straight(cat, toCat, { 1.26f, 0.215f, -0.245f }, { 1.06f, 0.215f, EXHAUST_Z }, R, PIPE);
		bend(cat, toCat, { glm::vec3(1.06f, 0.215f, EXHAUST_Z), { 0.97f, 0.215f, EXHAUST_Z }, { 0.98f, EXHAUST_Y, EXHAUST_Z }, { 0.88f, EXHAUST_Y, EXHAUST_Z } }, R, PIPE);
		straight(cat, toCat, { 0.88f, EXHAUST_Y, EXHAUST_Z }, { catFront + 0.04f, EXHAUST_Y, EXHAUST_Z }, R, PIPE);
		// Its body: a flat oval can with a cone at each end.
		Box(cat, { 0.f, EXHAUST_Y, 0.f }, { 2.f * CATALYST_HALF - 0.08f, 0.09f, 0.17f }, ALLOY);
		for (float end : { -1.f, 1.f })
			Cylinder(cat, AlongX({ end * (CATALYST_HALF - 0.025f), EXHAUST_Y, 0.f }), 0.045f, 0.05f, ALLOY, 14);
		Box(cat, { 0.f, EXHAUST_Y + 0.05f, 0.f }, { 2.f * CATALYST_HALF - 0.12f, 0.01f, 0.19f }, IRON);		// its heat shield
		straight(cat, toCat, { catBack - 0.01f, EXHAUST_Y, EXHAUST_Z }, { catBack - 0.06f, EXHAUST_Y, EXHAUST_Z }, R, PIPE);
		add(PartType::CATALYTIC_CONVERTER, cat);

		// The silencer: the pipe on from the converter, back under the floor and out under the back axle to it, and
		// the tailpipe from it out under the bumper. (Its part is turned end for end on the car: car x is -x here.)
		MeshBuilder muffler;
		const glm::vec3 m = { MUFFLER_X, MUFFLER_Y, MUFFLER_Z };
		// A point of the car's in the silencer's own space.
		auto in = [&](glm::vec3 car) { return glm::vec3(m.x - car.x, car.y - m.y, m.z - car.z); };
		auto pipe = [&](std::array<glm::vec3, 4> p) {
			for (auto& point : p)
				point = in(point);
			bend(muffler, {}, p, R, PIPE);
		};
		const float front = MUFFLER_X + MUFFLER_HALF, middleY = MUFFLER_Y + MUFFLER_RADIUS;
		pipe({ glm::vec3(catBack - 0.06f, EXHAUST_Y, EXHAUST_Z), { -0.4f, EXHAUST_Y, EXHAUST_Z }, { -0.75f, EXHAUST_Y, EXHAUST_Z }, { -0.9f, EXHAUST_Y, EXHAUST_Z } });
		pipe({ glm::vec3(-0.9f, EXHAUST_Y, EXHAUST_Z), { -1.08f, EXHAUST_Y, EXHAUST_Z }, { -1.05f, middleY, MUFFLER_Z }, { front + 0.01f, middleY, MUFFLER_Z } });
		Cylinder(muffler, AlongX({ 0.f, MUFFLER_RADIUS, 0.f }), MUFFLER_RADIUS, 2.f * MUFFLER_HALF, IRON, 18);
		for (float end : { -1.f, 1.f })
			Cylinder(muffler, AlongX({ end * MUFFLER_HALF, MUFFLER_RADIUS, 0.f }), MUFFLER_RADIUS * 0.85f, 0.02f, IRON, 18);
		const float tailIn = in(glm::vec3(TAILPIPE[0], 0.f, 0.f)).x;
		Cylinder(muffler, AlongX({ (MUFFLER_HALF + tailIn - 0.05f) * 0.5f, MUFFLER_RADIUS, 0.f }), R, tailIn - 0.05f - MUFFLER_HALF, PIPE, 12);
		// The tailpipe's tip, out past the bumper: a bright sleeve, sooty inside.
		Tube(muffler, AlongX({ tailIn - 0.05f, MUFFLER_RADIUS, 0.f }), 0.031f, 0.025f, 0.1f, ALLOY, 16);
		Cylinder(muffler, AlongX({ tailIn - 0.03f, MUFFLER_RADIUS, 0.f }), 0.025f, 0.02f, { 0.03f, 0.03f, 0.03f }, 16);
		add(PartType::MUFFLER, muffler);
	}
	{
		// A puff of exhaust smoke: a ball a metre across (ShopWorld sizes it, and fades it out).
		MeshBuilder puff;
		puff.ResetTransform().SetColor({ 1.f, 1.f, 1.f }).AddSphere({}, 0.5f, 6, 10);
		AddModel(ShopModels::SMOKE_PUFF, puff, true);
	}
	{
		// The looms' plugs. A plug: a block connector on the end of its wire, its latch on top, the wire coming in at
		// -x. A socket: the half that's on whatever it plugs into, open toward -x. And a metre of wire along x, which
		// ShopWorld stretches from point to point.
		MeshBuilder plug, socket, wire;
		{
			// The plug (the male half's housing, its blades inside it): its moulded body, the nose that goes into the
			// socket with its seal ring and two terminal blades standing out of its face, grip ribs down its sides, the
			// latch arm along its top (hooked at the front, a thumb pad at the back to press it off), and the rubber boot
			// at the back the wire comes out of, stepped down to the wire.
			const glm::vec3 BODY = OFF_WHITE, LATCH = { 0.62f, 0.62f, 0.58f }, SEAL = { 0.85f, 0.45f, 0.12f }, BRASS = { 0.78f, 0.62f, 0.28f };
			Box(plug, { -0.003f, 0.f, 0.f }, { 0.024f, 0.022f, 0.028f }, BODY);
			Box(plug, { 0.012f, 0.f, 0.f }, { 0.008f, 0.017f, 0.023f }, BODY);						// the nose
			Box(plug, { 0.0085f, 0.f, 0.f }, { 0.0025f, 0.0185f, 0.0245f }, SEAL);					// its seal
			for (float z : { -0.006f, 0.006f })
			{
				Box(plug, { 0.0162f, 0.f, z }, { 0.0005f, 0.008f, 0.005f }, { 0.08f, 0.08f, 0.09f });	// the blade's slot
				Box(plug, { 0.0185f, 0.f, z }, { 0.006f, 0.0055f, 0.0012f }, BRASS);				// the blade
			}
			for (float side : { -1.f, 1.f })
				for (float x : { -0.011f, -0.006f, -0.001f })
					Box(plug, { x, 0.f, side * 0.0145f }, { 0.002f, 0.018f, 0.0012f }, BODY);
			// The latch: its arm on two posts, the hook at its front, the pad at its back.
			Box(plug, { 0.001f, 0.0125f, 0.f }, { 0.004f, 0.003f, 0.01f }, LATCH);
			Box(plug, { 0.003f, 0.0148f, 0.f }, { 0.024f, 0.0018f, 0.009f }, LATCH);
			Box(plug, { 0.0145f, 0.0135f, 0.f }, { 0.002f, 0.0045f, 0.009f }, LATCH);
			Box(plug, { -0.0085f, 0.0163f, 0.f }, { 0.006f, 0.0022f, 0.011f }, LATCH);
			for (float x : { -0.01f, -0.0075f })
				Box(plug, { x, 0.0177f, 0.f }, { 0.0008f, 0.0008f, 0.011f }, BODY);				// its grip lines
			// The boot.
			Cylinder(plug, AlongX({ -0.0175f, 0.f, 0.f }), 0.0095f, 0.005f, RUBBER, 12);
			Cylinder(plug, AlongX({ -0.0225f, 0.f, 0.f }), 0.0075f, 0.005f, RUBBER, 12);
			Cylinder(plug, AlongX({ -0.028f, 0.f, 0.f }), 0.0058f, 0.006f, RUBBER, 10);
			AddModel(ShopModels::HARNESS_PLUG, plug, true);
		}
		{
			// The socket (the female half, on what it plugs into), open toward -x: a shroud the plug's nose goes into, its
			// four walls round the dark cavity with the two terminals' receptacles at the back, the lock's ramp on top the
			// plug's latch hooks over, a polarising key down one side, and the flange it's fixed by at the back, with its
			// two screws.
			const glm::vec3 SHELL = { 0.25f, 0.25f, 0.27f }, DARK_IN = { 0.04f, 0.04f, 0.045f };
			constexpr float DEPTH = 0.018f, H = 0.024f, W = 0.03f, WALL = 0.0025f, FRONT = -0.012f;
			const float middle = FRONT + DEPTH * 0.5f;
			Box(socket, { middle, (H - WALL) * 0.5f, 0.f }, { DEPTH, WALL, W }, SHELL);
			Box(socket, { middle, -(H - WALL) * 0.5f, 0.f }, { DEPTH, WALL, W }, SHELL);
			for (float side : { -1.f, 1.f })
				Box(socket, { middle, 0.f, side * (W - WALL) * 0.5f }, { DEPTH, H - 2.f * WALL, WALL }, SHELL);
			Box(socket, { FRONT + DEPTH - 0.002f, 0.f, 0.f }, { 0.004f, H - 2.f * WALL, W - 2.f * WALL }, DARK_IN);	// the cavity's back
			for (float z : { -0.006f, 0.006f })
			{
				Box(socket, { FRONT + DEPTH - 0.0045f, 0.f, z }, { 0.002f, 0.0075f, 0.004f }, COPPER);
				Box(socket, { FRONT + DEPTH - 0.0052f, 0.f, z }, { 0.0006f, 0.0045f, 0.0016f }, DARK_IN);
			}
			Box(socket, { FRONT + DEPTH * 0.5f, -0.004f, W * 0.5f - WALL - 0.001f }, { DEPTH, 0.004f, 0.002f }, SHELL);	// the key
			// The lock's ramp on the top wall's front, and the flange at the back.
			Box(socket, { FRONT + 0.003f, H * 0.5f + 0.0012f, 0.f }, { 0.004f, 0.0024f, 0.008f }, SHELL);
			Box(socket, { FRONT + DEPTH + 0.002f, 0.f, 0.f }, { 0.004f, H + 0.004f, W + 0.016f }, SHELL);
			for (float side : { -1.f, 1.f })
			{
				Cylinder(socket, AlongX({ FRONT + DEPTH + 0.0005f, 0.f, side * (W * 0.5f + 0.0045f) }), 0.0032f, 0.0012f, ALLOY, 10);
				Box(socket, { FRONT + DEPTH - 0.0002f, 0.f, side * (W * 0.5f + 0.0045f) }, { 0.0004f, 0.0045f, 0.0008f }, DARK_IN);
			}
			AddModel(ShopModels::HARNESS_SOCKET, socket, true);
		}
		Cylinder(wire, AlongX({ 0.5f, 0.f, 0.f }), 0.0045f, 1.f, { 1.f, 1.f, 1.f }, 6);
		AddModel(ShopModels::HARNESS_WIRE, wire, true);
		{
			// A clip that holds a wire where the player has led it, the wire through it along x (ShopWorld turns it along
			// the wire and sizes it to a pipe or hose): a nylon strap round the wire with its buckle on the side, on a
			// saddle base under it, and the fir tree pin down out of the base that pushes into a hole in the panel.
			MeshBuilder clip;
			const glm::vec3 NYLON = { 0.85f, 0.68f, 0.12f }, NYLON_DARK = { 0.7f, 0.55f, 0.08f };
			Tube(clip, AlongX({ 0.f, 0.f, 0.f }), 0.0064f, 0.0046f, 0.006f, NYLON, 14);
			Box(clip, { 0.f, 0.f, 0.0072f }, { 0.007f, 0.0055f, 0.003f }, NYLON_DARK);				// the buckle
			Box(clip, { 0.f, 0.0028f, 0.0088f }, { 0.004f, 0.0012f, 0.0035f }, NYLON);				// the strap's tail out of it
			Box(clip, { 0.f, -0.0068f, 0.f }, { 0.012f, 0.0024f, 0.013f }, NYLON);					// the saddle
			for (float z : { -0.005f, 0.005f })
				Box(clip, { 0.f, -0.0052f, z }, { 0.012f, 0.0012f, 0.003f }, NYLON);				// its cheeks up the wire's sides
			Cylinder(clip, At({ 0.f, -0.0105f, 0.f }), 0.0016f, 0.005f, NYLON, 8);
			for (float y : { -0.0095f, -0.0115f })
				Cylinder(clip, At({ 0.f, y, 0.f }), 0.0028f, 0.0007f, NYLON_DARK, 10);				// the pin's fins
			AddModel(ShopModels::HARNESS_CLIP, clip, true);
		}
		{
			// The stud that holds the battery's ground strap to the body, wherever it's put, standing out of the panel along
			// +x (ShopWorld turns +x to face out of the panel): the bright ring where the paint's scraped off for a good
			// contact, the stud's welded base, a serrated washer under the strap's lug (GROUND_LUG, on it at x 0.0055), the
			// flanged nut on top of that, and the stud's threaded end out past it.
			MeshBuilder stud;
			const glm::vec3 BARE = { 0.78f, 0.78f, 0.8f }, PLATED = { 0.7f, 0.68f, 0.55f };
			Cylinder(stud, AlongX({ 0.0003f, 0.f, 0.f }), 0.0165f, 0.0006f, BARE, 18);
			Cylinder(stud, AlongX({ 0.0012f, 0.f, 0.f }), 0.0075f, 0.0024f, BARE, 14);
			Tube(stud, AlongX({ 0.0031f, 0.f, 0.f }), 0.0105f, 0.0048f, 0.0014f, PLATED, 16);
			for (int tooth = 0; tooth < 10; tooth++)
			{
				const float a = glm::two_pi<float>() * tooth / 10.f;
				Box(stud, { 0.0039f, std::sin(a) * 0.0092f, std::cos(a) * 0.0092f }, { 0.0006f, 0.0018f, 0.0018f }, PLATED);
			}
			Cylinder(stud, AlongX({ 0.0118f, 0.f, 0.f }), 0.004f, 0.019f, PLATED, 10);
			for (float x : { 0.0165f, 0.0185f, 0.0205f })
				Cylinder(stud, AlongX({ x, 0.f, 0.f }), 0.0044f, 0.0007f, PLATED, 10);				// its thread
			Cylinder(stud, AlongX({ 0.0078f, 0.f, 0.f }), 0.0098f, 0.0016f, PLATED, 16);			// the nut's flange
			Cylinder(stud, AlongX({ 0.0114f, 0.f, 0.f }), 0.0085f, 0.0056f, PLATED, 6);
			AddModel(ShopModels::GROUND_STUD, stud, true);
		}
		{
			// The ground strap's end: a ring lug, the strap coming in along +x to it. Its ring lies flat, its hole's axis
			// up (on the stud, ShopWorld turns its up out of the panel and puts it so the ring sits on the washer), the
			// tongue back from it to the barrel crimped on the strap's end, and the black sleeve over the crimp.
			MeshBuilder lug;
			const glm::vec3 TINNED = { 0.76f, 0.74f, 0.68f };
			Tube(lug, At({ 0.f, -0.0035f, 0.f }), 0.0105f, 0.0046f, 0.0016f, TINNED, 18);
			Box(lug, { -0.0115f, -0.0035f, 0.f }, { 0.01f, 0.0016f, 0.011f }, TINNED);
			Cylinder(lug, AlongX({ -0.022f, 0.f, 0.f }), 0.0055f, 0.013f, TINNED, 12);
			for (float x : { -0.025f, -0.019f })
				Cylinder(lug, AlongX({ x, 0.f, 0.f }), 0.0052f, 0.002f, { 0.55f, 0.53f, 0.48f }, 12);	// the crimp's dents
			Cylinder(lug, AlongX({ -0.033f, 0.f, 0.f }), 0.0064f, 0.012f, TRIM_BLACK, 12);
			AddModel(ShopModels::GROUND_LUG, lug, true);
		}
		// The pipes' ends, each going in along +x. A hard pipe's union: the flare nut on the pipe's end, its sleeve
		// behind it. The port it screws into: a seat round the hole. A hose's end, a clip round it; and the stub it
		// pushes onto.
		// (Fitted, the union's hex sits against the port's face at x 0.004, its thread in the port; the hose's end runs
		// over the stub's barbs up to its base at x 0.008.)
		MeshBuilder unionNut, port, hoseEnd, stub;
		const glm::vec3 ZINC = { 0.68f, 0.66f, 0.56f }, STEEL = { 0.62f, 0.63f, 0.66f }, BRASS = { 0.78f, 0.62f, 0.28f }, HOLE = { 0.05f, 0.05f, 0.055f };
		{
			// The flare nut: its hex, the chamfer down to the thread, the thread's turns, and the collar at its back
			// where the pipe comes out, with a stub of the pipe and its flare's lip showing in the nose.
			Cylinder(unionNut, AlongX({ -0.001f, 0.f, 0.f }), 0.0075f, 0.01f, ZINC, 6);
			Cylinder(unionNut, AlongX({ 0.0045f, 0.f, 0.f }), 0.0066f, 0.001f, ZINC, 12);
			for (float x : { 0.0055f, 0.0067f, 0.0079f })
				Cylinder(unionNut, AlongX({ x, 0.f, 0.f }), 0.0062f, 0.0008f, ZINC, 12);
			Cylinder(unionNut, AlongX({ 0.0067f, 0.f, 0.f }), 0.0057f, 0.0034f, ZINC, 12);
			Cylinder(unionNut, AlongX({ 0.0086f, 0.f, 0.f }), 0.0042f, 0.0006f, STEEL, 10);			// the flare
			Cylinder(unionNut, AlongX({ -0.0072f, 0.f, 0.f }), 0.0055f, 0.0025f, ZINC, 12);
			Cylinder(unionNut, AlongX({ -0.014f, 0.f, 0.f }), 0.0032f, 0.012f, STEEL, 10);
			AddModel(ShopModels::PIPE_UNION, unionNut, true);
		}
		{
			// The port: a boss standing out of what it's in, its face with the threaded hole (dark), a chamfer round it,
			// and a hex where it's a screwed-in fitting.
			Cylinder(port, AlongX({ 0.0115f, 0.f, 0.f }), 0.0105f, 0.009f, { 0.6f, 0.61f, 0.63f }, 6);
			Cylinder(port, AlongX({ 0.0055f, 0.f, 0.f }), 0.0098f, 0.003f, { 0.6f, 0.61f, 0.63f }, 16);
			Cylinder(port, AlongX({ 0.0039f, 0.f, 0.f }), 0.0082f, 0.0004f, { 0.7f, 0.71f, 0.73f }, 16);
			Cylinder(port, AlongX({ 0.0036f, 0.f, 0.f }), 0.0063f, 0.0004f, HOLE, 12);
			AddModel(ShopModels::PIPE_PORT, port, true);
		}
		{
			// The hose's end: the rubber, a moulded lip at its very end, and a worm drive clip round it: the band, the
			// screw's housing on top and the screw across it with its hex head.
			Cylinder(hoseEnd, AlongX({ -0.011f, 0.f, 0.f }), 0.0088f, 0.038f, RUBBER, 14);
			Cylinder(hoseEnd, AlongX({ 0.007f, 0.f, 0.f }), 0.0093f, 0.002f, RUBBER, 14);
			Cylinder(hoseEnd, AlongX({ 0.002f, 0.f, 0.f }), 0.0098f, 0.007f, STEEL, 16);
			for (float x : { -0.0005f, 0.0015f, 0.0035f })
				Box(hoseEnd, { x, 0.0099f, 0.f }, { 0.0008f, 0.0003f, 0.004f }, HOLE);				// the band's slots
			Box(hoseEnd, { 0.002f, 0.0108f, 0.f }, { 0.0085f, 0.004f, 0.01f }, STEEL);
			Cylinder(hoseEnd, AlongZ({ 0.002f, 0.0115f, 0.f }), 0.0022f, 0.016f, STEEL, 8);
			Cylinder(hoseEnd, AlongZ({ 0.002f, 0.0115f, 0.0085f }), 0.0034f, 0.003f, STEEL, 6);
			Box(hoseEnd, { 0.002f, 0.0115f, 0.0101f }, { 0.0006f, 0.0045f, 0.0003f }, HOLE);			// its slot
			AddModel(ShopModels::HOSE_END, hoseEnd, true);
		}
		{
			// The stub: a brass spigot out of a hex base on what it's in, two barbs along it and a bead at its tip.
			Cylinder(stub, AlongX({ 0.012f, 0.f, 0.f }), 0.0095f, 0.008f, BRASS, 6);
			Cylinder(stub, AlongX({ -0.004f, 0.f, 0.f }), 0.0058f, 0.024f, BRASS, 12);
			for (float x : { -0.007f, -0.001f })
			{
				Cylinder(stub, AlongX({ x, 0.f, 0.f }), 0.0068f, 0.0015f, BRASS, 12);
				Cylinder(stub, AlongX({ x - 0.0015f, 0.f, 0.f }), 0.0063f, 0.0015f, BRASS, 12);
			}
			Cylinder(stub, AlongX({ -0.0145f, 0.f, 0.f }), 0.0066f, 0.002f, BRASS, 12);
			Cylinder(stub, AlongX({ -0.0158f, 0.f, 0.f }), 0.0042f, 0.0004f, HOLE, 10);
			AddModel(ShopModels::HOSE_STUB, stub, true);
		}
		// The brake pipes' junction block, on its bracket against the wheel housing's wall (its -z): the five unions
		// along its top, where the pipes come off it.
		MeshBuilder block;
		Box(block, { 0.f, 0.f, -0.011f }, { 0.11f, 0.05f, 0.004f }, { 0.3f, 0.31f, 0.33f });
		Box(block, { 0.f, -0.004f, 0.f }, { 0.092f, 0.026f, 0.02f }, ALLOY);
		for (int pipe = 0; pipe < 5; pipe++)
			Cylinder(block, At({ -0.03f + 0.015f * pipe, 0.0115f, 0.f }), 0.0058f, 0.005f, IRON, 6);
		for (float x : { -0.048f, 0.048f })
			Cylinder(block, AlongZ({ x, 0.016f, -0.008f }), 0.0045f, 0.004f, IRON, 6);
		AddModel(ShopModels::BRAKE_LINE_BLOCK, block, true);
		// And the fuel line's front end: up through a grommet in the top of the other wheel housing.
		MeshBuilder riser;
		Cylinder(riser, At({ 0.f, -0.02f, 0.f }), 0.0045f, 0.04f, ALLOY, 10);
		Cylinder(riser, At({ 0.f, -0.003f, 0.f }), 0.0058f, 0.004f, ALLOY, 10);
		Cylinder(riser, At({ 0.f, -0.032f, 0.f }), 0.013f, 0.01f, RUBBER, 14);
		AddModel(ShopModels::FUEL_LINE_RISER, riser, true);
	}

	// Wheels: a 13 inch wheel, and the tire round it. Both sides of the car use the one model turned the same way,
	// so the wheel is built alike either side of its middle: the rim's barrel and lips, the centre disc with its
	// six vents, and the hub with a cap each side.
	{
		MeshBuilder wheel;
		Tube(wheel, AlongX({ 0.f, TIRE_RADIUS, 0.f }), 0.166f, 0.156f, 0.14f, ALLOY, 32);
		for (float side : { -1.f, 1.f })
			Tube(wheel, AlongX({ side * 0.066f, TIRE_RADIUS, 0.f }), 0.174f, 0.156f, 0.01f, ALLOY, 32);
		// The centre: a ring of fins from the hub out to the rim, dark between them.
		Cylinder(wheel, AlongX({ 0.f, TIRE_RADIUS, 0.f }), 0.157f, 0.012f, { 0.2f, 0.2f, 0.22f }, 32);
		for (int fin = 0; fin < 20; fin++)
		{
			wheel.SetTransform(glm::rotate(At({ 0.f, TIRE_RADIUS, 0.f }), fin * glm::two_pi<float>() / 20.f, glm::vec3(1.f, 0.f, 0.f))).SetColor(ALLOY);
			wheel.AddBox({ 0.f, 0.105f, 0.f }, { 0.02f, 0.1f, 0.014f });
			wheel.ResetTransform();
		}
		Cylinder(wheel, AlongX({ 0.f, TIRE_RADIUS, 0.f }), 0.06f, 0.06f, ALLOY, 20);
		// The four stud holes through its centre, seen on either face.
		for (float face : { -1.f, 1.f })
			for (float y : { -1.f, 1.f })
				for (float z : { -1.f, 1.f })
					Cylinder(wheel, AlongX({ face * 0.0302f, TIRE_RADIUS + y * 0.03536f, z * 0.03536f }), 0.0075f, 0.001f, { 0.05f, 0.05f, 0.06f }, 12);
		Cylinder(wheel, AlongX({ 0.f, TIRE_RADIUS, 0.f }), 0.035f, 0.27f, { 0.8f, 0.81f, 0.83f }, 16);		// its hub, in to the brake
		add(PartType::WHEEL, wheel);
	}
	{
		// The tread, and the shoulders stepping in to the sidewalls, so its section reads round.
		MeshBuilder tire;
		Tube(tire, AlongX({ 0.f, TIRE_RADIUS, 0.f }), TIRE_RADIUS, 0.166f, 0.15f, RUBBER, 40);
		Tube(tire, AlongX({ 0.f, TIRE_RADIUS, 0.f }), TIRE_RADIUS - 0.009f, 0.166f, 0.172f, RUBBER, 40);
		Tube(tire, AlongX({ 0.f, TIRE_RADIUS, 0.f }), TIRE_RADIUS - 0.024f, 0.166f, 0.185f, { 0.1f, 0.1f, 0.1f }, 40);
		// Grooves round the tread.
		for (float x : { -0.04f, 0.f, 0.04f })
			Tube(tire, AlongX({ x, TIRE_RADIUS, 0.f }), TIRE_RADIUS + 0.0005f, TIRE_RADIUS - 0.006f, 0.008f, { 0.03f, 0.03f, 0.03f }, 40);
		add(PartType::TIRE, tire);
	}

	BuildProjectCarShell();

	// The floor jack (a shop fitting), in pieces so it can work: see JackLayout.
	{
		using namespace JackLayout;
		const glm::vec3 RED = { 0.72f, 0.1f, 0.08f }, STEEL = { 0.62f, 0.63f, 0.65f }, DARK = { 0.12f, 0.12f, 0.13f };
		// The body: two pressed side plates, low at the front where the saddle tucks in, rising to the pump at the back;
		// cross tubes between them; steel wheels at the front and swivelling castors at the back; the pump's barrel and
		// its reservoir, the ram from it up to the arm, the release valve's knob, and the socket the handle goes in.
		MeshBuilder body;
		body.SetUVScale(3.f);
		const std::vector<glm::vec2> plate = { { -0.04f, 0.03f }, { 0.86f, 0.03f }, { 0.86f, 0.17f }, { 0.64f, 0.19f }, { 0.08f, 0.115f }, { -0.04f, 0.095f } };
		for (float side : { -1.f, 1.f })
		{
			Prism(body, plate, side * 0.085f - 0.006f, side * 0.085f + 0.006f, RED);
			// The plate's rolled lip along its top.
			Box(body, { 0.36f, 0.153f, side * 0.085f }, { 0.56f, 0.012f, 0.02f }, RED * 0.85f);
			// Front wheels, on an axle through the plates.
			Cylinder(body, AlongZ({ 0.02f, 0.042f, side * 0.112f }), 0.042f, 0.026f, STEEL, 18);
			Cylinder(body, AlongZ({ 0.02f, 0.042f, side * 0.126f }), 0.016f, 0.004f, DARK, 12);
			// Castors at the back: a fork, and the wheel in it.
			Box(body, { 0.78f, 0.06f, side * 0.07f }, { 0.05f, 0.02f, 0.05f }, DARK);
			Cylinder(body, AlongZ({ 0.8f, 0.028f, side * 0.07f }), 0.028f, 0.02f, DARK, 14);
		}
		Cylinder(body, AlongZ({ 0.02f, 0.042f, 0.f }), 0.009f, 0.24f, STEEL, 10);		// the front axle
		for (float x : { 0.18f, 0.5f })
			Cylinder(body, AlongZ({ x, 0.06f, 0.f }), 0.012f, 0.17f, RED * 0.85f, 10);		// cross tubes
		Cylinder(body, AlongZ({ ARM_PIVOT_X, ARM_PIVOT_Y, 0.f }), 0.014f, 0.2f, STEEL, 12);	// the arm's pivot pin
		// The pump: its barrel standing at the back between the plates, the reservoir beside it, and the ram angled up
		// from it toward the arm.
		Cylinder(body, At({ 0.76f, 0.11f, 0.f }), 0.032f, 0.14f, DARK, 16);
		Cylinder(body, At({ 0.76f, 0.185f, 0.f }), 0.024f, 0.012f, STEEL, 14);
		Cylinder(body, glm::rotate(At({ 0.68f, 0.115f, 0.f }), 1.25f, glm::vec3(0.f, 0.f, 1.f)), 0.02f, 0.12f, STEEL, 12);
		// The handle's socket, a yoke on top of the pump, and the release valve's knob on its side.
		Box(body, { HANDLE_X, HANDLE_Y - 0.012f, 0.f }, { 0.05f, 0.024f, 0.06f }, DARK);
		Cylinder(body, AlongZ({ HANDLE_X, HANDLE_Y, 0.f }), 0.01f, 0.07f, STEEL, 10);
		Cylinder(body, AlongZ({ 0.8f, 0.12f, 0.055f }), 0.012f, 0.02f, { 0.85f, 0.65f, 0.1f }, 10);
		AddModel(ShopModels::JACK_BODY, body);

		// The lift arm, about its pivot (the origin), out along -x to the saddle's pin: two flat bars and a web between.
		MeshBuilder arm;
		for (float side : { -1.f, 1.f })
			Box(arm, { -ARM_LENGTH * 0.5f, 0.f, side * 0.058f }, { ARM_LENGTH + 0.03f, 0.045f, 0.014f }, RED);
		Box(arm, { -ARM_LENGTH * 0.45f, -0.012f, 0.f }, { ARM_LENGTH * 0.7f, 0.012f, 0.1f }, RED * 0.85f);
		Cylinder(arm, AlongZ({ -ARM_LENGTH, 0.f, 0.f }), 0.012f, 0.14f, STEEL, 10);		// the saddle's pin
		AddModel(ShopModels::JACK_ARM, arm);

		// The saddle, its bottom on the arm's pin: a round cup with a ridged top.
		MeshBuilder saddle;
		Cylinder(saddle, At({ 0.f, 0.008f, 0.f }), 0.022f, 0.016f, STEEL, 14);
		Cylinder(saddle, At({ 0.f, SADDLE_HEIGHT - 0.005f, 0.f }), 0.055f, 0.01f, DARK, 20);
		for (int ridge = -1; ridge <= 1; ridge++)
			Box(saddle, { 0.f, SADDLE_HEIGHT, ridge * 0.022f }, { 0.09f, 0.004f, 0.006f }, STEEL);
		AddModel(ShopModels::JACK_SADDLE, saddle);

		// The pump handle, from its socket (the origin) out along +x: a steel tube with a rubber grip on the end.
		MeshBuilder handle;
		Cylinder(handle, AlongX({ HANDLE_LENGTH * 0.5f, 0.f, 0.f }), 0.013f, HANDLE_LENGTH, STEEL, 12);
		Cylinder(handle, AlongX({ HANDLE_LENGTH - 0.07f, 0.f, 0.f }), 0.019f, 0.14f, DARK, 12);
		AddModel(ShopModels::JACK_HANDLE, handle);
		// (Its furniture model is the body: what's moved about, and what's crated.)
		for (const auto& furniture : FurnitureCatalog::All())
			if (furniture.kind == FurnitureKind::FLOOR_JACK)
				AddModel(FurnitureModel(furniture), body);
	}
}

void ShopAssets::BuildProjectCarShell()
{
	using namespace CarBody;
	// The shell, as the car stands on the drive: everything welded (floor, sills, roof and pillars, the rear
	// quarters, the firewall, strut towers and inner wings), with the openings the bolt-on panels, glass and lights
	// go in, cut to the same lines they are (CarBody.h). Its nose (the engine bay) is +X, its right +Z.
	MeshBuilder car;
	const glm::vec3 PRIMER = { 0.62f, 0.62f, 0.6f };		// the engine bay's bare metal
	const glm::vec3 UNDERSEAL = { 0.16f, 0.16f, 0.17f };

	// The floor pan, front to boot: the cabin's and the tail's the body's width, but only between the chassis rails
	// under the engine bay and between the wheel housings over the back axle, leaving the wheels their room.
	constexpr float FOOTWELL_X = 0.79f;				// the front wheel housings come back into the footwells to here
	constexpr float REAR_TUB = 0.335f;				// the rear wheel housings' radius, about the axle
	constexpr float REAR_TUB_WALL_Z = 0.5885f;		// and their inner walls, just inside the tires
	const float tubFront = REAR_AXLE_X + REAR_TUB + 0.02f, tubBack = REAR_AXLE_X - REAR_TUB - 0.02f;
	// The floor either side of the transmission tunnel, which is open underneath (the driveshaft's seen up in it): wide
	// behind the firewall over the gearbox, narrow back from the step; and either side of the low hump the shaft runs
	// back in under the back seat to the diff.
	constexpr float TUNNEL_STEP = 0.47f, TUNNEL_HALF = 0.1f, TUNNEL_MOUTH_HALF = 0.19f;
	auto floorBeside = [&](float x0, float x1, float inner, float outer, float y = 0.2f) {
		for (float side : { -1.f, 1.f })
			Box(car, { (x0 + x1) * 0.5f, y, side * (inner + outer) * 0.5f }, { x1 - x0, 0.04f, outer - inner }, UNDERSEAL);
	};
	// (Under the back seat, a channel each side for the upper trailing link, from the axle forward to its bracket:
	// inside the seat's plinth, so it isn't seen in the cabin.)
	constexpr float LINK_IN = 0.21f, LINK_OUT = 0.35f, LINK_FRONT = -0.8f, LINK_TOP = 0.37f;
	floorBeside(LINK_FRONT, TUNNEL_STEP, TUNNEL_HALF, 0.75f);
	floorBeside(tubFront, LINK_FRONT, TUNNEL_HALF, LINK_IN);
	floorBeside(tubFront, LINK_FRONT, LINK_OUT, 0.75f);
	floorBeside(TUNNEL_STEP, FOOTWELL_X, TUNNEL_MOUTH_HALF, 0.75f);
	// (Under the engine there's no floor: the pan stops at the firewall.)
	floorBeside(FOOTWELL_X, FIREWALL_X + 0.015f, TUNNEL_MOUTH_HALF, 0.41f);
	// Over the back axle the floor kicks up, so the axle, the diff and the end of the shaft hang under the body: its
	// front wall (open below for the shaft, behind the back seat's back), its top, and its back wall ahead of the tank.
	constexpr float KICK_FRONT = -1.105f, KICK_BACK = -1.4125f, KICK_Y = 0.47f, HUMP_TOP = 0.36f;
	floorBeside(KICK_FRONT, tubFront, TUNNEL_HALF, LINK_IN);
	floorBeside(KICK_FRONT, tubFront, LINK_OUT, REAR_TUB_WALL_Z);
	for (float side : { -1.f, 1.f })
	{
		for (float z : { LINK_IN, LINK_OUT })
			Box(car, { (KICK_FRONT + LINK_FRONT) * 0.5f, (0.18f + LINK_TOP) * 0.5f, side * z }, { LINK_FRONT - KICK_FRONT, LINK_TOP - 0.18f, 0.012f }, UNDERSEAL);
		Box(car, { (KICK_FRONT + LINK_FRONT) * 0.5f, LINK_TOP, side * (LINK_IN + LINK_OUT) * 0.5f }, { LINK_FRONT - KICK_FRONT, 0.012f, LINK_OUT - LINK_IN + 0.012f }, UNDERSEAL);
		Box(car, { LINK_FRONT, (0.18f + LINK_TOP) * 0.5f, side * (LINK_IN + LINK_OUT) * 0.5f }, { 0.012f, LINK_TOP - 0.18f, LINK_OUT - LINK_IN }, UNDERSEAL);
	}
	// The kick-up's top. Over the springs, out by the wheel housings, it goes up again to a shelf they seat under (the
	// shocks' tops come up through it into their towers).
	constexpr float PERCH_FRONT = -1.14f, PERCH_IN = 0.42f, PERCH_Y = 0.62f;
	Box(car, { (KICK_FRONT + PERCH_FRONT) * 0.5f, KICK_Y, 0.f }, { KICK_FRONT - PERCH_FRONT, 0.04f, 2.f * REAR_TUB_WALL_Z }, UNDERSEAL);
	Box(car, { (PERCH_FRONT + KICK_BACK) * 0.5f, KICK_Y, 0.f }, { PERCH_FRONT - KICK_BACK, 0.04f, 2.f * PERCH_IN }, UNDERSEAL);
	for (float side : { -1.f, 1.f })
	{
		const float middle = side * (PERCH_IN + REAR_TUB_WALL_Z) * 0.5f, wide = REAR_TUB_WALL_Z - PERCH_IN;
		Box(car, { (PERCH_FRONT + KICK_BACK) * 0.5f, PERCH_Y, middle }, { PERCH_FRONT - KICK_BACK, 0.04f, wide }, UNDERSEAL);
		Box(car, { (PERCH_FRONT + KICK_BACK) * 0.5f, (KICK_Y + PERCH_Y) * 0.5f, side * PERCH_IN }, { PERCH_FRONT - KICK_BACK, PERCH_Y - KICK_Y, 0.012f }, UNDERSEAL);
		Box(car, { PERCH_FRONT, (KICK_Y + PERCH_Y) * 0.5f, middle }, { 0.012f, PERCH_Y - KICK_Y, wide }, UNDERSEAL);
		Box(car, { KICK_BACK, (KICK_Y + PERCH_Y) * 0.5f, middle }, { 0.015f, PERCH_Y - KICK_Y, wide }, UNDERSEAL);
	}
	// Its front wall: solid above the hump and the links' channels, open below for them and the shaft.
	Box(car, { KICK_FRONT, (LINK_TOP + KICK_Y) * 0.5f, 0.f }, { 0.015f, KICK_Y - LINK_TOP + 0.02f, 2.f * REAR_TUB_WALL_Z }, UNDERSEAL);
	Box(car, { KICK_FRONT, (HUMP_TOP + LINK_TOP) * 0.5f, 0.f }, { 0.015f, LINK_TOP - HUMP_TOP, 2.f * TUNNEL_HALF }, UNDERSEAL);
	for (float side : { -1.f, 1.f })
	{
		Box(car, { KICK_FRONT, (0.22f + LINK_TOP) * 0.5f, side * (TUNNEL_HALF + LINK_IN) * 0.5f }, { 0.015f, LINK_TOP - 0.22f, LINK_IN - TUNNEL_HALF }, UNDERSEAL);
		Box(car, { KICK_FRONT, (0.22f + LINK_TOP) * 0.5f, side * (LINK_OUT + REAR_TUB_WALL_Z) * 0.5f }, { 0.015f, LINK_TOP - 0.22f, REAR_TUB_WALL_Z - LINK_OUT }, UNDERSEAL);
	}
	Box(car, { KICK_BACK, (0.18f + KICK_Y) * 0.5f, 0.f }, { 0.015f, KICK_Y - 0.18f, 2.f * REAR_TUB_WALL_Z }, UNDERSEAL);
	// The lower trailing links' brackets, hanging under the floor ahead of the axle, and the lateral rod's, down from
	// the shelf on the left.
	for (float side : { -1.f, 1.f })
		for (float cheek : { -0.022f, 0.022f })
			Box(car, { REAR_AXLE_X + 0.59f, 0.155f, side * 0.5f + cheek }, { 0.05f, 0.05f, 0.006f }, UNDERSEAL);
	for (float cheek : { -0.022f, 0.022f })
		Box(car, { REAR_AXLE_X - 0.166f, (0.3f + PERCH_Y) * 0.5f, -0.47f + cheek }, { 0.05f, PERCH_Y - 0.3f, 0.006f }, UNDERSEAL);
	// The boot floor under the tank, with a hole where the tank's outlet comes down through it.
	{
		const float holeX = FUEL_TANK_OUTLET[0], holeZ = FUEL_TANK_OUTLET[2], HOLE = 0.025f;
		Box(car, { (holeX - HOLE + TAIL_X + 0.12f) * 0.5f, 0.2f, 0.f }, { holeX - HOLE - TAIL_X - 0.12f, 0.04f, 1.5f }, UNDERSEAL);
		Box(car, { (KICK_BACK + holeX + HOLE) * 0.5f, 0.2f, 0.f }, { KICK_BACK - holeX - HOLE, 0.04f, 1.5f }, UNDERSEAL);
		Box(car, { holeX, 0.2f, (holeZ - HOLE - 0.75f) * 0.5f }, { 2.f * HOLE, 0.04f, holeZ - HOLE + 0.75f }, UNDERSEAL);
		Box(car, { holeX, 0.2f, (holeZ + HOLE + 0.75f) * 0.5f }, { 2.f * HOLE, 0.04f, 0.75f - holeZ - HOLE }, UNDERSEAL);
		Tube(car, At({ holeX, 0.2f, holeZ }), HOLE + 0.004f, 0.012f, 0.044f, RUBBER, 14);	// its grommet
	}
	// The hump: from the tunnel's end back to the kick-up, low enough to pass under the back seat's cushion.
	{
		const float x0 = KICK_FRONT, x1 = -0.65f;
		for (float side : { -1.f, 1.f })
			Box(car, { (x0 + x1) * 0.5f, (0.18f + HUMP_TOP) * 0.5f, side * TUNNEL_HALF }, { x1 - x0, HUMP_TOP - 0.18f, 0.015f }, UNDERSEAL);
		Box(car, { (x0 + x1) * 0.5f, HUMP_TOP, 0.f }, { x1 - x0, 0.015f, 2.f * TUNNEL_HALF + 0.015f }, UNDERSEAL);
	}
	const float roofMiddle = (WINDSHIELD_TOP_X + REAR_WINDOW_TOP_X) * 0.5f, roofLength = WINDSHIELD_TOP_X - REAR_WINDOW_TOP_X;
	// The fixed quarter windows are a model of their own, so they can be seen through.
	MeshBuilder quarterGlass;
	for (float side : { -1.f, 1.f })
	{
		const float skinZ = side * (SIDE - SKIN * 0.5f);
		// A point on the leaning side plane of the glass and pillars, and that plane's normal.
		auto on = [&](float x, float y) { return glm::vec3(x, y, side * SideZ(y)); };
		const glm::vec3 lean = { 0.f, GLASS_Z - ROOF_Z, side * (ROOF_Y - BELT_Y) };

		// The sill under the door (the door shuts down onto its top), and the step inside it.
		SidePanel(car, DOOR_REAR_X, DOOR_FRONT_X, Level(SILL_Y), Level(ROCKER_TOP), skinZ, SKIN, BODY_WHITE, 4);
		Box(car, { 0.2f, ROCKER_TOP - 0.01f, side * 0.73f }, { 1.3f, 0.02f, 0.14f }, BODY_WHITE);
		Box(car, { 0.2f, 0.25f, side * 0.67f }, { 1.3f, 0.1f, 0.02f }, BODY_WHITE);

		// The rear quarter, from the door's shut line back to the tail, cut round the rear arch (the tail hangs a
		// little higher than the sill). The right one has the fuel filler's opening in it, with its pocket and cap.
		auto quarterBottom = [low = Arched(SILL_Y, REAR_AXLE_X, AXLE_Y, ARCH_RADIUS), tail = Arched(0.25f, REAR_AXLE_X, AXLE_Y, ARCH_RADIUS)](float x) {
			return x > REAR_AXLE_X ? low(x) : tail(x);
		};
		if (side > 0.f)
		{
			const float x0 = FLAP_X - FLAP_HALF, x1 = FLAP_X + FLAP_HALF;
			SidePanel(car, TAIL_X, x0, quarterBottom, Level(BELT_Y), skinZ, SKIN, BODY_WHITE, 8);
			SidePanel(car, x0, x1, quarterBottom, Level(FLAP_Y - FLAP_HALF), skinZ, SKIN, BODY_WHITE, 2);
			SidePanel(car, x0, x1, Level(FLAP_Y + FLAP_HALF), Level(BELT_Y), skinZ, SKIN, BODY_WHITE, 2);
			SidePanel(car, x1, DOOR_REAR_X, quarterBottom, Level(BELT_Y), skinZ, SKIN, BODY_WHITE, 40);
			Box(car, { FLAP_X, FLAP_Y, SIDE - SKIN - 0.003f }, { 2.f * FLAP_HALF, 2.f * FLAP_HALF, 0.006f }, TRIM_BLACK);
			Cylinder(car, AlongZ({ FLAP_X, FLAP_Y, SIDE - SKIN + 0.01f }), 0.036f, 0.02f, { 0.2f, 0.2f, 0.21f }, 16);
			Box(car, { FLAP_X, FLAP_Y, SIDE - SKIN + 0.021f }, { 0.05f, 0.012f, 0.004f }, ALLOY);
		}
		else
			SidePanel(car, TAIL_X, DOOR_REAR_X, quarterBottom, Level(BELT_Y), skinZ, SKIN, BODY_WHITE, 48);
		ArchLip(car, REAR_AXLE_X, AXLE_Y, ARCH_RADIUS, side * (SIDE - 0.016f), 0.048f, BODY_WHITE);
		ArchFlare(car, REAR_AXLE_X, AXLE_Y, ARCH_RADIUS, side * SIDE, side, BODY_WHITE);
		// The rear wheel's housing: a tub over the tire from the quarter's skin in to a wall beside the boot, the wall
		// open round the axle's end for the brake.
		{
			const float outerZ = SIDE - SKIN, width = outerZ - REAR_TUB_WALL_Z;
			ArchLip(car, REAR_AXLE_X, AXLE_Y, REAR_TUB, side * (REAR_TUB_WALL_Z + width * 0.5f), width, UNDERSEAL);
			const float reach = REAR_TUB + 0.02f;
			SidePanel(car, tubBack, tubFront, Arched(SILL_Y, REAR_AXLE_X, AXLE_Y, 0.17f),
				[reach](float x) { float dx = x - REAR_AXLE_X; return AXLE_Y + std::sqrt((std::max)(reach * reach - dx * dx, 0.f)); },
				side * REAR_TUB_WALL_Z, 0.012f, UNDERSEAL, 36);
		}
		// The rubbing strip along the quarter, in line with the door's, either side of the arch.
		Box(car, { -0.655f, RUB_STRIP_Y, side * (SIDE + 0.002f) }, { 0.4f, 0.012f, 0.004f }, CREASE);
		Box(car, { (TAIL_X + 0.02f + REAR_AXLE_X - ARCH_RADIUS - 0.01f) * 0.5f, RUB_STRIP_Y, side * (SIDE + 0.002f) }, { REAR_AXLE_X - ARCH_RADIUS - 0.01f - TAIL_X - 0.02f, 0.012f, 0.004f }, CREASE);
		// The side skirt under the door, between the arches.
		Box(car, { 0.f, 0.195f, side * (SIDE + 0.004f) }, { 1.7f, 0.09f, 0.03f }, BODY_WHITE);
		Box(car, { 0.f, 0.243f, side * (SIDE + 0.012f) }, { 1.7f, 0.008f, 0.02f }, CREASE);
		// The quarter's top: in to the trunk lid's edge along the deck, and in to the glass under the window.
		Box(car, { (TAIL_X + REAR_WINDOW_BASE_X) * 0.5f, BELT_Y - 0.01f, side * (SIDE + TRUNK_HALF) * 0.5f },
			{ REAR_WINDOW_BASE_X - TAIL_X, 0.02f, SIDE - TRUNK_HALF }, BODY_WHITE);
		Box(car, { (REAR_WINDOW_BASE_X + DOOR_REAR_X) * 0.5f, BELT_Y - 0.01f, side * 0.75f }, { DOOR_REAR_X - REAR_WINDOW_BASE_X, 0.02f, 0.1f }, BODY_WHITE);

		// The door's opening: the hinge post behind the fender with the two hinges on its face, and the lock post.
		Box(car, { DOOR_FRONT_X + 0.035f, 0.58f, side * 0.715f }, { 0.07f, 0.56f, 0.095f }, BODY_WHITE);
		Box(car, { DOOR_REAR_X - 0.04f, 0.58f, side * 0.715f }, { 0.08f, 0.56f, 0.095f }, BODY_WHITE);
		for (float y : { 0.42f, 0.74f })
			Box(car, { DOOR_FRONT_X - 0.004f, y, side * 0.745f }, { 0.012f, 0.085f, 0.045f }, IRON);
		Box(car, { DOOR_REAR_X + 0.004f, 0.6f, side * 0.74f }, { 0.012f, 0.04f, 0.03f }, ALLOY);		// the striker

		// Above the belt: the A-pillar raked back to the roof, the B-pillar behind the door's glass, the fixed quarter
		// window, the C-pillar down to the deck beside the rear window, and the roof's rail over them with its gutter.
		Beam(car, on(WINDSHIELD_BASE_X, BELT_Y), on(WINDSHIELD_TOP_X, ROOF_Y), { 0.f, 0.f, 1.f }, 0.055f, 0.05f, BODY_WHITE);
		Slab(car, { on(-0.54f, BELT_Y), on(DOOR_REAR_X, BELT_Y), on(DOOR_REAR_X, ROOF_Y), on(-0.54f, ROOF_Y) }, lean, 0.03f, TRIM_BLACK);
		Slab(quarterGlass, { on(-0.54f, BELT_Y), on(-1.12f, BELT_Y), on(-0.99f, 1.22f), on(-0.54f, 1.22f) }, lean, 0.008f, GLASS);
		Slab(car, { on(-1.12f, BELT_Y), on(REAR_WINDOW_BASE_X - 0.02f, BELT_Y), on(REAR_WINDOW_TOP_X - 0.02f, ROOF_Y), on(-0.95f, ROOF_Y) },
			lean, 0.03f, BODY_WHITE);
		Box(car, { roofMiddle, ROOF_Y - 0.005f, side * (ROOF_Z - 0.003f) }, { roofLength, 0.04f, 0.04f }, BODY_WHITE);
		Box(car, { roofMiddle, ROOF_Y + 0.012f, side * (ROOF_Z + 0.014f) }, { roofLength - 0.04f, 0.008f, 0.012f }, TRIM_BLACK);
		// The well the headlight's pod sits down in, black inside: its floor, and its wall to the radiator's side, so
		// nothing of the engine bay shows under the pod when it's up.
		{
			const float back = NOSE_X - LAMP_LID_LENGTH + 0.016f, front = NOSE_X - 0.04f;
			Box(car, { (back + front) * 0.5f, 0.495f, side * (LAMP_LID_INNER + 0.69f) * 0.5f }, { front - back, 0.01f, 0.69f - LAMP_LID_INNER }, TRIM_BLACK);
			Box(car, { (back + front) * 0.5f, 0.58f, side * (LAMP_LID_INNER - 0.007f) }, { front - back, 0.17f, 0.01f }, TRIM_BLACK);
			Box(car, { back, 0.56f, side * (LAMP_LID_INNER + 0.69f) * 0.5f }, { 0.01f, 0.13f, 0.69f - LAMP_LID_INNER }, TRIM_BLACK);
		}
		// The nose's corner beside the headlight, behind the fender's skin.
		Box(car, { NOSE_X - 0.02f, 0.65f, side * 0.7475f }, { 0.04f, 0.08f, 0.035f }, BODY_WHITE);

		// The engine bay's sides: the strut tower (a top plate the strut's mount bolts up under, its nuts on top, walled
		// in from the bay), the inner wing up to the fender's bolt lip, and the chassis rail. And the rear spring tower.
		// The front wheel's housing closes the wheel off from the bay: a tub over the tire from the fender's skin in to
		// a wall beside the engine (open below the chassis rail, where the arm, the tie rod and the strut rod come
		// through), and on top of it the strut's tower up to its plate.
		{
			const float outerZ = SIDE - SKIN, width = outerZ - FRONT_TUB_WALL_Z;
			ArchLip(car, FRONT_AXLE_X, AXLE_Y, FRONT_TUB, side * (FRONT_TUB_WALL_Z + width * 0.5f), width, PRIMER);
			const float reach = FRONT_TUB + 0.022f;
			SidePanel(car, FRONT_AXLE_X - reach, FRONT_AXLE_X + reach, Level(0.27f),
				[reach](float x) { float dx = x - FRONT_AXLE_X; return (std::max)(0.27f, AXLE_Y + std::sqrt((std::max)(reach * reach - dx * dx, 0.f))); },
				side * FRONT_TUB_WALL_Z, 0.015f, PRIMER, 44);
			Tube(car, At({ FRONT_AXLE_X, 0.71f, side * 0.55f }), 0.095f, 0.083f, 0.07f, PRIMER, 20);
			Box(car, { FRONT_AXLE_X, 0.745f, side * 0.55f }, { 0.21f, 0.02f, 0.21f }, PRIMER);
		}
		// The inner wing, from the tub up to the fender's bolt lip.
		SidePanel(car, 0.92f, NOSE_X - 0.04f, Arched(0.27f, FRONT_AXLE_X, AXLE_Y, FRONT_TUB + 0.01f), [](float x) { return HoodY(x) - 0.045f; }, side * 0.655f, 0.03f, PRIMER, 44);
		// The engine's mount sits on a perch off the chassis rail, braced under it.
		Box(car, { ENGINE_X, ENGINE_MOUNT_Y - 0.015f, side * (ENGINE_MOUNT_Z + 0.025f) }, { 0.15f, 0.03f, 0.17f }, PRIMER);
		Prism(car, { { ENGINE_MOUNT_Z - 0.05f, ENGINE_MOUNT_Y - 0.03f }, { 0.32f, ENGINE_MOUNT_Y - 0.03f }, { 0.32f, 0.215f } }, -0.006f, 0.006f, PRIMER,
			glm::rotate(At({ ENGINE_X, 0.f, 0.f }), side * -glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f)));
		// The chassis rail, inboard of where the tire's front swings in on full lock.
		Box(car, { (0.9f + NOSE_X + 0.13f) * 0.5f, 0.25f, side * 0.36f }, { NOSE_X + 0.13f - 0.9f, 0.08f, 0.08f }, PRIMER);
		// The front wheel's housing where it comes back into the footwell: the tire's back swings in here on full lock.
		Box(car, { FOOTWELL_X, 0.36f, side * 0.595f }, { 0.02f, 0.32f, 0.31f }, UNDERSEAL);
		Box(car, { (FOOTWELL_X + 0.88f) * 0.5f, 0.36f, side * 0.44f }, { 0.88f - FOOTWELL_X + 0.02f, 0.32f, 0.02f }, UNDERSEAL);
		Box(car, { (FOOTWELL_X + 0.88f) * 0.5f, 0.52f, side * 0.595f }, { 0.88f - FOOTWELL_X + 0.02f, 0.02f, 0.33f }, UNDERSEAL);
		// (The rear shock's tower, behind the back seat's back.)
		Box(car, { -1.27f, 0.66f, side * 0.52f }, { 0.2f, 0.08f, 0.2f }, UNDERSEAL);
	}
	// The battery's tray, on a bracket off the left inner wing.
	Box(car, { BATTERY[0], BATTERY[1] - 0.01f, BATTERY[2] }, { 0.21f, 0.02f, 0.27f }, PRIMER);
	Box(car, { BATTERY[0], BATTERY[1] - 0.06f, -0.625f }, { 0.14f, 0.1f, 0.03f }, PRIMER);
	// The jacking bar under the nose, between the chassis rails, and the jacking pad under the boot floor (CarBody's
	// FRONT_JACK_POINT and REAR_JACK_POINT).
	Box(car, { FRONT_JACK_POINT[0], FRONT_JACK_POINT[1] + 0.02f, 0.f }, { 0.06f, 0.04f, 0.76f }, UNDERSEAL);
	Box(car, { REAR_JACK_POINT[0], REAR_JACK_POINT[1] + 0.003f, 0.f }, { 0.14f, 0.006f, 0.3f }, PRIMER);
	// The pads under the chassis rails' inner edges that the steering rack's clamps bolt up to.
	for (float side : { -1.f, 1.f })
		Box(car, { 1.4f, 0.2025f, side * 0.315f }, { 0.1f, 0.015f, 0.05f }, UNDERSEAL);
	// The front crossmember under the back of the engine, between the chassis rails, the lower arms pivoting on its ears.
	Box(car, { 1.1f, 0.16f, 0.f }, { 0.07f, 0.05f, 0.8f }, UNDERSEAL);
	for (float side : { -1.f, 1.f })
	{
		Box(car, { 1.1f, 0.2f, side * 0.36f }, { 0.07f, 0.04f, 0.08f }, UNDERSEAL);
		Box(car, { 1.17f, 0.165f, side * 0.145f }, { 0.14f, 0.05f, 0.03f }, UNDERSEAL);
	}
	// The roof, a little crowned, and the headers over the windshield and the rear window.
	Box(car, { roofMiddle, ROOF_Y + 0.008f, 0.f }, { roofLength, 0.035f, 2.f * ROOF_Z - 0.02f }, BODY_WHITE);
	Box(car, { roofMiddle, ROOF_Y + 0.03f, 0.f }, { roofLength - 0.16f, 0.012f, 2.f * ROOF_Z - 0.22f }, BODY_WHITE);
	Box(car, { WINDSHIELD_TOP_X + 0.01f, ROOF_Y - 0.008f, 0.f }, { 0.07f, 0.035f, 2.f * ROOF_Z }, BODY_WHITE);
	Box(car, { REAR_WINDOW_TOP_X - 0.01f, ROOF_Y - 0.008f, 0.f }, { 0.07f, 0.035f, 2.f * ROOF_Z }, BODY_WHITE);
	// The firewall, and the cowl over it between the fenders, with its air intake and the wipers parked on it.
	constexpr float TUNNEL_MOUTH = 0.19f, TUNNEL_MOUTH_TOP = 0.52f, TUNNEL = 0.1f, TUNNEL_TOP = 0.42f, TUNNEL_STEP_X = 0.47f;
	Box(car, { FIREWALL_X, 0.69f, 0.f }, { 0.03f, 0.34f, 1.5f }, PRIMER);
	// Lower down it's narrower, between the wheel housings, and open in the middle where the gearbox's bellhousing
	// goes back into the tunnel.
	for (float side : { -1.f, 1.f })
		Box(car, { FIREWALL_X, 0.37f, side * (TUNNEL_MOUTH + 0.44f) * 0.5f }, { 0.03f, 0.3f, 0.44f - TUNNEL_MOUTH }, PRIMER);
	Box(car, { (DOOR_FRONT_X + HOOD_REAR_X) * 0.5f, BELT_Y - 0.01f, 0.f }, { HOOD_REAR_X - DOOR_FRONT_X, 0.02f, 2.f * HOOD_HALF }, BODY_WHITE);
	Box(car, { 0.935f, BELT_Y + 0.001f, 0.f }, { 0.04f, 0.002f, 1.1f }, TRIM_BLACK);
	Box(car, { 0.925f, BELT_Y + 0.012f, -0.3f }, { 0.012f, 0.012f, 0.5f }, TRIM_BLACK);
	Box(car, { 0.925f, BELT_Y + 0.012f, 0.3f }, { 0.012f, 0.012f, 0.42f }, TRIM_BLACK);
	// The nose: the radiator support, the valance down behind the bumper, the black band the headlights and grille
	// sit in with the strip over it the hood shuts onto, and the slatted grille with its badge.
	// (A low wedge: the panel behind the bumper, and over it the dark slot between the headlights' lids that the hood
	// shuts down to.)
	Box(car, { NOSE_X - 0.04f, 0.46f, 0.f }, { 0.04f, 0.38f, 0.72f }, PRIMER);		// (between the headlights' pods, which drop down either side of it)
	Box(car, { NOSE_X - 0.02f, 0.43f, 0.f }, { 0.04f, 0.46f, 1.53f }, TRIM_BLACK);
	Box(car, { NOSE_X - 0.02f, 0.66f, 0.f }, { 0.04f, 0.05f, 2.f * LAMP_LID_INNER }, TRIM_BLACK);
	// The tail panel, the black garnish between the tail lights with the number plate on it, the deck under the
	// rear window, and the shelf behind the back seat.
	Box(car, { TAIL_X + 0.015f, 0.53f, 0.f }, { 0.03f, 0.66f, 1.53f }, BODY_WHITE);
	// (The garnish stands out level with the lights' frames: a raised rim round a recess the plate sits in, the
	// trunk's lock left of the plate and a blanking cap right of it. A dark seam runs under it all, over the bumper.)
	Box(car, { TAIL_X - 0.011f, 0.6475f, 0.f }, { 0.028f, 0.185f, 0.48f }, TRIM_BLACK);
	Box(car, { TAIL_X - 0.026f, 0.6475f, 0.f }, { 0.004f, 0.16f, 0.33f }, { 0.03f, 0.03f, 0.035f });
	for (float y : { 0.561f, 0.734f })
		Box(car, { TAIL_X - 0.028f, y, 0.f }, { 0.006f, 0.012f, 0.48f }, { 0.13f, 0.13f, 0.14f });
	Box(car, { TAIL_X - 0.029f, 0.6475f, 0.f }, { 0.004f, 0.135f, 0.27f }, OFF_WHITE);
	Box(car, { TAIL_X - 0.0315f, 0.64f, 0.f }, { 0.002f, 0.06f, 0.21f }, { 0.12f, 0.16f, 0.4f });			// the plate's lettering
	for (float z : { -0.105f, 0.105f })
		Cylinder(car, AlongX({ TAIL_X - 0.0315f, 0.696f, z }), 0.006f, 0.003f, ALLOY, 8);				// its screws
	Box(car, { TAIL_X - 0.027f, 0.6475f, -0.2f }, { 0.006f, 0.055f, 0.05f }, { 0.13f, 0.13f, 0.14f });
	Cylinder(car, AlongX({ TAIL_X - 0.031f, 0.6475f, -0.2f }), 0.012f, 0.004f, ALLOY, 12);
	Box(car, { TAIL_X - 0.027f, 0.6475f, 0.2f }, { 0.006f, 0.055f, 0.05f }, { 0.13f, 0.13f, 0.14f });
	Box(car, { TAIL_X - 0.002f, 0.535f, 0.f }, { 0.004f, 0.012f, 1.56f }, { 0.12f, 0.12f, 0.13f });
	Box(car, { (REAR_WINDOW_BASE_X + TRUNK_FRONT_X) * 0.5f, BELT_Y - 0.01f, 0.f }, { REAR_WINDOW_BASE_X - TRUNK_FRONT_X, 0.02f, 2.f * TRUNK_HALF }, BODY_WHITE);
	// (The parcel shelf, from the top of the back seat to the rear window, with a speaker's grille either side.)
	Box(car, { -1.37f, 0.84f, 0.f }, { 0.3f, 0.02f, 1.44f }, VINYL);
	for (float side : { -1.f, 1.f })
	{
		Cylinder(car, At({ -1.37f, 0.852f, side * 0.42f }), 0.075f, 0.006f, { 0.05f, 0.05f, 0.055f }, 18);
		Tube(car, At({ -1.37f, 0.854f, side * 0.42f }), 0.08f, 0.07f, 0.006f, { 0.2f, 0.2f, 0.21f }, 18);
	}
	// Inside: the transmission tunnel, and the back seat (it's part of the shell: only the front seats come out).
	// (The tunnel is hollow, over the gearbox: wide and high behind the firewall for the bellhousing and the gear case,
	// then low and narrow over the tail housing, back between the seats.)
	{
		const float mouthLength = FIREWALL_X - 0.015f - TUNNEL_STEP_X, mouthMiddle = TUNNEL_STEP_X + mouthLength * 0.5f;
		const float tailLength = TUNNEL_STEP_X + 0.65f, tailMiddle = TUNNEL_STEP_X - tailLength * 0.5f;
		for (float side : { -1.f, 1.f })
		{
			Box(car, { mouthMiddle, (0.22f + TUNNEL_MOUTH_TOP) * 0.5f, side * TUNNEL_MOUTH }, { mouthLength, TUNNEL_MOUTH_TOP - 0.22f, 0.015f }, UNDERSEAL);
			Box(car, { tailMiddle, (0.22f + TUNNEL_TOP) * 0.5f, side * TUNNEL }, { tailLength, TUNNEL_TOP - 0.22f, 0.015f }, UNDERSEAL);
			Box(car, { TUNNEL_STEP_X, (0.22f + TUNNEL_MOUTH_TOP) * 0.5f, side * (TUNNEL + TUNNEL_MOUTH) * 0.5f },
				{ 0.015f, TUNNEL_MOUTH_TOP - 0.22f, TUNNEL_MOUTH - TUNNEL }, UNDERSEAL);
		}
		Box(car, { mouthMiddle, TUNNEL_MOUTH_TOP, 0.f }, { mouthLength, 0.015f, 2.f * TUNNEL_MOUTH + 0.015f }, UNDERSEAL);
		Box(car, { tailMiddle, TUNNEL_TOP, 0.f }, { tailLength, 0.015f, 2.f * TUNNEL + 0.015f }, UNDERSEAL);
		Box(car, { TUNNEL_STEP_X, (TUNNEL_TOP + TUNNEL_MOUTH_TOP) * 0.5f, 0.f }, { 0.015f, TUNNEL_MOUTH_TOP - TUNNEL_TOP, 2.f * TUNNEL }, UNDERSEAL);
		// (Stepping down at its back end to the hump under the back seat.)
		Box(car, { -0.65f, (0.36f + TUNNEL_TOP) * 0.5f, 0.f }, { 0.015f, TUNNEL_TOP - 0.36f, 2.f * TUNNEL }, UNDERSEAL);
		// And open underneath: its walls come down past the floor's edges.
		for (float side : { -1.f, 1.f })
			Box(car, { tailMiddle, 0.2f, side * TUNNEL }, { tailLength, 0.04f, 0.015f }, UNDERSEAL);
	}
	// The back seat: a bench for two between the wheel housings, in the front seats' grey tweed. A vinyl plinth, a
	// ribbed cushion for each place between darker bolsters with a roll along the front, and a back leant against the
	// shelf, made the same way.
	{
		const glm::vec3 ROLL = { 0.17f, 0.17f, 0.19f };
		Box(car, { -0.92f, 0.27f, 0.f }, { 0.46f, 0.1f, 2.f * REAR_SEAT_HALF }, VINYL);
		Box(car, { -0.695f, 0.325f, 0.f }, { 0.04f, 0.11f, 2.f * REAR_SEAT_HALF }, ROLL);
		Box(car, { -0.92f, 0.35f, 0.f }, { 0.43f, 0.08f, 0.07f }, CLOTH_DARK);
		const glm::mat4 back = glm::rotate(At({ -1.13f, 0.36f, 0.f }), 0.22f, glm::vec3(0.f, 0.f, 1.f));
		TurnedBox(car, back, { -0.03f, 0.25f, 0.f }, { 0.04f, 0.5f, 2.f * REAR_SEAT_HALF }, VINYL);
		TurnedBox(car, back, { 0.012f, 0.24f, 0.f }, { 0.1f, 0.46f, 0.07f }, CLOTH_DARK);
		TurnedBox(car, back, { 0.005f, 0.485f, 0.f }, { 0.11f, 0.05f, 2.f * REAR_SEAT_HALF }, ROLL);
		for (float side : { -1.f, 1.f })
		{
			Box(car, { -0.93f, 0.345f, side * 0.275f }, { 0.43f, 0.07f, 0.48f }, CLOTH);
			for (int rib = 0; rib < 4; rib++)
				Box(car, { -1.08f + rib * 0.1f, 0.381f, side * 0.275f }, { 0.012f, 0.004f, 0.44f }, CLOTH_DARK);
			Box(car, { -0.92f, 0.355f, side * 0.5425f }, { 0.43f, 0.09f, 0.055f }, CLOTH_DARK);
			TurnedBox(car, back, { 0.005f, 0.24f, side * 0.275f }, { 0.09f, 0.46f, 0.48f }, CLOTH);
			for (int rib = 0; rib < 4; rib++)
				TurnedBox(car, back, { 0.052f, 0.09f + rib * 0.1f, side * 0.275f }, { 0.004f, 0.012f, 0.44f }, CLOTH_DARK);
			TurnedBox(car, back, { 0.012f, 0.24f, side * 0.5425f }, { 0.1f, 0.46f, 0.055f }, CLOTH_DARK);
			// Its lap belt's buckle, on the cushion by the middle.
			Box(car, { -1.05f, 0.39f, side * 0.07f }, { 0.05f, 0.02f, 0.03f }, { 0.7f, 0.12f, 0.1f });
		}
	}
	// The cabin's trim. The carpet over the floor either side of the tunnel; the scuff plates on the sills; the cards
	// inside the rear quarters beside the back seat, each with a cloth insert, an armrest and an ashtray; the trim up
	// the lock pillars with the front seats' belts hanging down them; the headliner, with the sun visors, the mirror
	// and the dome lamp.
	{
		const glm::vec3 FLOOR_CARPET = { 0.13f, 0.13f, 0.14f }, HEADLINER = { 0.72f, 0.72f, 0.69f }, WEBBING = { 0.1f, 0.1f, 0.11f };
		for (float side : { -1.f, 1.f })
		{
			Box(car, { 0.055f, 0.2215f, side * 0.385f }, { 1.47f, 0.003f, 0.55f }, FLOOR_CARPET);
			Box(car, { 0.2f, ROCKER_TOP + 0.002f, side * 0.72f }, { 1.2f, 0.004f, 0.07f }, ALLOY);
			// The quarter's card, over the wheel housing, from the lock pillar back to the seat's back.
			const float cardZ = side * (SIDE - SKIN - 0.012f), into = -side;
			Box(car, { -0.86f, 0.585f, cardZ }, { 0.72f, 0.53f, 0.02f }, VINYL);
			Box(car, { -0.85f, 0.64f, cardZ + into * 0.011f }, { 0.56f, 0.2f, 0.004f }, CLOTH);
			Box(car, { -0.86f, 0.845f, side * 0.745f }, { 0.72f, 0.012f, 0.05f }, VINYL);
			// The wheel housing's trim, which shuts the rear wheel's tub, spring and shock off from the cabin: a moulded
			// box over the tub beside the bench, stepped in round the spring at the bench's back corner, with a padded
			// armrest and an ashtray on top; and behind it a filler up to the shelf, beside the seat's back.
			{
				const float inner = REAR_SEAT_HALF, outer = SIDE - SKIN - 0.02f, top = 0.66f;
				Box(car, { -1.05f, (0.3f + top) * 0.5f, side * (inner + outer) * 0.5f }, { 0.42f, top - 0.3f, outer - inner }, VINYL);
				Box(car, { -1.15f, (0.385f + top) * 0.5f, side * (0.42f + inner) * 0.5f }, { 0.22f, top - 0.385f, inner - 0.42f }, VINYL);
				Box(car, { -1.0f, top + 0.008f, side * (inner + outer) * 0.5f }, { 0.3f, 0.016f, outer - inner - 0.03f }, CLOTH_DARK);
				Box(car, { -0.875f, top + 0.004f, side * (inner + outer) * 0.5f }, { 0.05f, 0.01f, 0.07f }, ALLOY);
				Box(car, { -1.205f, (top + 0.83f) * 0.5f, side * (inner + outer + 0.02f) * 0.5f }, { 0.17f, 0.83f - top, outer + 0.02f - inner }, VINYL);
				// (A dark line where the moulding meets the carpet.)
				Box(car, { -0.838f, 0.31f, side * (inner + outer) * 0.5f }, { 0.004f, 0.02f, outer - inner }, TRIM_BLACK);
			}
			// The lock pillar's trim, and the belt from its reel at the pillar's foot up to the guide by the roof.
			Box(car, { DOOR_REAR_X - 0.04f, 0.58f, side * 0.662f }, { 0.085f, 0.56f, 0.012f }, VINYL);
			Box(car, { DOOR_REAR_X - 0.04f, 0.33f, side * 0.64f }, { 0.07f, 0.08f, 0.04f }, TRIM_BLACK);
			Beam(car, { DOOR_REAR_X - 0.04f, 0.37f, side * 0.64f }, { DOOR_REAR_X - 0.045f, 1.16f, side * (SideZ(1.16f) - 0.03f) }, { 1.f, 0.f, 0.f }, 0.004f, 0.045f, WEBBING);
			Box(car, { DOOR_REAR_X - 0.045f, 1.16f, side * (SideZ(1.16f) - 0.03f) }, { 0.03f, 0.05f, 0.02f }, TRIM_BLACK);
			// A sun visor, folded up against the headliner.
			Box(car, { WINDSHIELD_TOP_X - 0.09f, ROOF_Y - 0.03f, side * 0.33f }, { 0.14f, 0.014f, 0.38f }, HEADLINER * 0.9f);
			// The grab handle over the door.
			Box(car, { -0.1f, ROOF_Y - 0.04f, side * (ROOF_Z - 0.04f) }, { 0.16f, 0.02f, 0.016f }, HEADLINER * 0.75f);
		}
		Box(car, { roofMiddle, ROOF_Y - 0.014f, 0.f }, { roofLength - 0.06f, 0.006f, 2.f * ROOF_Z - 0.07f }, HEADLINER);
		// The mirror on its stalk from the windshield's header, and the dome lamp in the middle of the roof.
		Box(car, { WINDSHIELD_TOP_X + 0.03f, ROOF_Y - 0.05f, 0.f }, { 0.02f, 0.06f, 0.02f }, TRIM_BLACK);
		Box(car, { WINDSHIELD_TOP_X + 0.02f, ROOF_Y - 0.085f, 0.f }, { 0.025f, 0.055f, 0.24f }, TRIM_BLACK);
		Box(car, { WINDSHIELD_TOP_X + 0.0065f, ROOF_Y - 0.085f, 0.f }, { 0.002f, 0.045f, 0.225f }, { 0.7f, 0.78f, 0.82f });
		Box(car, { roofMiddle + 0.1f, ROOF_Y - 0.02f, 0.f }, { 0.1f, 0.01f, 0.07f }, { 0.9f, 0.88f, 0.78f });
		// The gear lever's gaiter on the console.
		Cylinder(car, At({ 0.165f, TUNNEL_TOP + 0.055f, 0.f }), 0.042f, 0.03f, RUBBER, 8);
	}
	// The mats, front and back.
	for (float side : { -1.f, 1.f })
	{
		Box(car, { 0.42f, 0.224f, side * 0.4f }, { 0.62f, 0.008f, 0.42f }, CARPET);
		Box(car, { 0.42f, 0.229f, side * 0.4f }, { 0.4f, 0.004f, 0.3f }, CARPET * 0.85f);
		Box(car, { -0.5f, 0.224f, side * 0.4f }, { 0.3f, 0.008f, 0.42f }, CARPET);
	}
	// The pedals in the driver's footwell: the clutch, the brake, and the throttle long and low on the right; a rest
	// for the left foot. Hung from under the dash: each arm runs up and forward to its pivot, so its pad's face looks back
	// and up at the driver's foot (leaning the other way, the arms would rise toward the seat and the pads face the floor).
	{
		const glm::mat4 lean = glm::rotate(At({ 0.5f, 0.3f, 0.f }), -0.5f, glm::vec3(0.f, 0.f, 1.f));
		for (float z : { -0.5f, -0.4f })
		{
			TurnedBox(car, lean, { 0.f, 0.03f, z }, { 0.012f, 0.06f, 0.055f }, ALLOY);
			TurnedBox(car, lean, { 0.03f, 0.12f, z }, { 0.012f, 0.2f, 0.014f }, TRIM_BLACK);
		}
		TurnedBox(car, lean, { 0.f, 0.f, -0.29f }, { 0.012f, 0.13f, 0.04f }, ALLOY);
		TurnedBox(car, lean, { 0.f, 0.f, -0.6f }, { 0.012f, 0.14f, 0.06f }, TRIM_BLACK);
	}
	// Between the seats, on the tunnel: the console round the gear lever, and the handbrake's boot behind it (its lever is
	// a model of its own, which comes up when it's pulled).
	Box(car, { 0.15f, TUNNEL_TOP + 0.02f, 0.f }, { 0.36f, 0.04f, 0.16f }, DASH_BLACK);
	Box(car, { -0.22f, TUNNEL_TOP + 0.015f, 0.f }, { 0.38f, 0.03f, 0.13f }, DASH_BLACK);
	Box(car, { HANDBRAKE_PIVOT[0] + 0.1f, TUNNEL_TOP + 0.034f, HANDBRAKE_PIVOT[2] }, { 0.2f, 0.008f, 0.03f }, RUBBER);	// the slot it comes up through
	AddModel(ShopModels::PROJECT_CAR, car);
	AddModel(ShopModels::CAR_QUARTER_GLASS, quarterGlass);

	// The handbrake's lever, built about its pivot lying forward along +x (on the car it tips up about z), its grip and
	// the button in its end.
	{
		MeshBuilder lever;
		Cylinder(lever, AlongZ({ 0.f, 0.f, 0.f }), 0.014f, 0.03f, IRON, 10);
		Box(lever, { 0.1f, 0.012f, 0.f }, { 0.2f, 0.022f, 0.022f }, TRIM_BLACK);
		Box(lever, { 0.235f, 0.016f, 0.f }, { 0.11f, 0.032f, 0.032f }, { 0.13f, 0.13f, 0.14f });
		Cylinder(lever, AlongX({ 0.295f, 0.016f, 0.f }), 0.008f, 0.012f, ALLOY, 10);
		AddModel(ShopModels::HANDBRAKE_LEVER, lever);
	}
	// A headlight's beam: a cone of light out along +x from its tip at the origin, a metre long and a metre across at its
	// end (ShopWorld stretches it), drawn as light in the air (ObjectShading::BEAM). In bands along it, each's colour
	// how thick the light is there (the shader takes it from the red): building up just out of the lamp and thinning
	// away to nothing by its end, as a real beam spreads and fades.
	{
		MeshBuilder beam;
		constexpr int SIDES = 40, BANDS = 24;
		for (int band = 0; band < BANDS; band++)
		{
			const float x0 = static_cast<float>(band) / BANDS, x1 = static_cast<float>(band + 1) / BANDS, mid = (x0 + x1) * 0.5f;
			const float thick = glm::smoothstep(0.f, 0.12f, mid) * (1.f - mid) * (1.f - mid);
			beam.SetColor(glm::vec3(thick));
			for (int i = 0; i < SIDES; i++)
			{
				const float a0 = glm::two_pi<float>() * i / SIDES, a1 = glm::two_pi<float>() * (i + 1) / SIDES;
				auto ring = [](float x, float a) { return glm::vec3(x, std::sin(a) * 0.5f * x, std::cos(a) * 0.5f * x); };
				const glm::vec3 p00 = ring(x0, a0), p01 = ring(x0, a1), p10 = ring(x1, a0), p11 = ring(x1, a1);
				// (Both faces of it, the shader fading each by how square-on it's seen. The first band's a fan from the tip.)
				beam.AddFlatTriangle(p00, p10, p11).AddFlatTriangle(p00, p11, p10);
				if (band > 0)
					beam.AddFlatTriangle(p00, p11, p01).AddFlatTriangle(p00, p01, p11);
			}
		}
		AddModel(ShopModels::HEADLIGHT_BEAM, beam, true);
	}
	// A tail light's stop and tail lamp: a red pane just proud of the lamp's lens, lit by ShopWorld.
	{
		MeshBuilder stop;
		Box(stop, { 0.f, 0.f, 0.f }, { STOP_LAMP_WIDE - 0.012f, STOP_LAMP_HIGH - 0.012f, 0.004f }, { 1.f, 0.08f, 0.05f });
		AddModel(ShopModels::STOP_LAMP, stop);
	}
	// The ram of a floor jack (or the post of a jack stand) holding the car up: a metre of it up from its foot, scaled to
	// fit, with a saddle on top.
	{
		MeshBuilder post;
		Cylinder(post, At({ 0.f, 0.5f, 0.f }), 0.02f, 1.f, ALLOY, 10);
		AddModel(ShopModels::SUPPORT_POST, post);
	}
}
