// The project car's models: stand-ins for its parts, and the car itself on the drive.
#include "ShopAssets.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "LV/Engine/Graphics/Models/MeshBuilder.h"
#include "../Simulation/PartCatalog.h"
#include "CarBody.h"

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
			const float cowl = HOOD_REAR_X - DOOR_FRONT_X, beltTop = BELT_Y - SILL_Y, shelf = SIDE - HOOD_HALF;
			Box(fender, { cowl * 0.5f, beltTop - 0.01f, in * shelf * 0.5f }, { cowl, 0.02f, shelf }, BODY_WHITE);
			const float fall = std::hypot(NOSE_X - HOOD_REAR_X, BELT_Y - NOSE_Y);
			glm::mat4 slope = glm::rotate(At({ cowl, beltTop, 0.f }), HoodPitch(), glm::vec3(0.f, 0.f, 1.f));
			TurnedBox(fender, slope, { fall * 0.5f, -0.01f, in * shelf * 0.5f }, { fall, 0.02f, shelf }, BODY_WHITE);
			TurnedBox(fender, slope, { fall * 0.5f, -0.025f, in * (shelf - 0.003f) }, { fall, 0.03f, 0.006f }, BODY_WHITE);
			TurnedBox(fender, slope, { fall * 0.5f, -0.04f, in * (shelf + 0.025f) }, { fall, 0.01f, 0.05f }, BODY_WHITE);
			// The rubbing strip ahead of the arch, in line with the door's, and the side marker lamp.
			Box(fender, { 0.795f, RUB_STRIP_Y - SILL_Y, side * 0.002f }, { 0.17f, 0.012f, 0.004f }, CREASE);
			Box(fender, { 1.14f, HoodY(NOSE_X - 0.08f) - SILL_Y - 0.06f, side * 0.004f }, { 0.15f, 0.045f, 0.008f }, { 0.7f, 0.33f, 0.08f });
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
			Box(door, { -0.35f, 0.38f, in * 0.077f }, { 0.08f, 0.02f, 0.006f }, ALLOY);					// the inside handle
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
		// front edge, on its top face. Its back edge turns down over the tail panel to the lights, with the lock in
		// it, and a lip stands along it.
		MeshBuilder lid;
		const float length = TRUNK_FRONT_X - TAIL_X - 2.f * GAP, width = 2.f * (TRUNK_HALF - GAP);
		Box(lid, { -length * 0.5f, -0.011f, 0.f }, { length, 0.022f, width }, BODY_WHITE);
		Box(lid, { -length + 0.008f, -0.07f, 0.f }, { 0.016f, 0.14f, width }, BODY_WHITE);
		Box(lid, { -length + 0.03f, 0.006f, 0.f }, { 0.06f, 0.012f, width - 0.04f }, BODY_WHITE);
		Box(lid, { -length * 0.5f, -0.026f, 0.f }, { length - 0.12f, 0.008f, width - 0.3f }, { 0.55f, 0.55f, 0.53f });
		Cylinder(lid, AlongX({ -length - 0.001f, -0.1f, 0.f }), 0.012f, 0.004f, ALLOY, 12);
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
		Box(bumper, { 0.f, 0.095f, face * 0.03f }, { 1.67f, 0.19f, 0.14f }, TRIM_BLACK);
		for (float end : { -1.f, 1.f })
			Box(bumper, { end * 0.8125f, 0.095f, face * -0.19f }, { 0.045f, 0.19f, 0.3f }, TRIM_BLACK);
		Box(bumper, { 0.f, 0.125f, face * 0.101f }, { 1.6f, 0.03f, 0.004f }, { 0.22f, 0.22f, 0.23f });
		Box(bumper, { 0.f, 0.006f, face * 0.02f }, { 1.6f, 0.012f, 0.1f }, TRIM_BLACK);
		// The number plate, and beside it the indicators (front) or reflectors (rear).
		Box(bumper, { 0.f, 0.07f, face * 0.102f }, { 0.3f, 0.09f, 0.004f }, OFF_WHITE);
		for (float end : { -1.f, 1.f })
			Box(bumper, { end * 0.56f, 0.07f, face * 0.101f }, { 0.17f, 0.045f, 0.004f }, face > 0.f ? AMBER : LENS_RED);
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
		const float up = HoodPitch() + LAMP_POP, sine = std::sin(up), cosine = std::cos(up);
		constexpr float FACE = 0.125f, HOUSING = 0.13f, UNDER = -0.022f;
		Box(pod, { 0.f, -0.011f, deep * 0.5f }, { wide, 0.022f, deep }, BODY_WHITE);
		Box(pod, { 0.f, -0.03f, deep - 0.006f }, { wide, 0.016f, 0.012f }, BODY_WHITE);
		// Side on (z forward, y up): the housing's top under the lid, its face down from there, and its bottom back.
		const glm::vec2 topFront = { deep - 0.03f, UNDER }, topBack = { deep - 0.17f, UNDER };
		const glm::vec2 faceBottom = topFront + glm::vec2(-sine, -cosine) * FACE;
		const glm::vec2 bottomBack = faceBottom + glm::vec2(-cosine, sine) * HOUSING;
		const glm::mat4 sideOn = glm::rotate(glm::mat4(1.f), -glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f));
		Prism(pod, { topBack, topFront, faceBottom, bottomBack }, -(wide * 0.5f - 0.012f), wide * 0.5f - 0.012f, TRIM_BLACK, sideOn);
		for (float side : { -1.f, 1.f })
			Prism(pod, { { 0.012f, UNDER }, topFront, faceBottom }, side * (wide * 0.5f - 0.004f) - 0.004f, side * (wide * 0.5f - 0.004f) + 0.004f, BODY_WHITE, sideOn);
		// The lamp in the housing's face: a bright bezel, and the lens in it.
		const glm::vec2 faceMiddle = (topFront + faceBottom) * 0.5f;
		const glm::mat4 face = glm::rotate(At({ 0.f, faceMiddle.y, faceMiddle.x }), up, glm::vec3(1.f, 0.f, 0.f));
		TurnedBox(pod, face, { 0.f, 0.f, 0.002f }, { wide - 0.05f, FACE - 0.02f, 0.004f }, ALLOY);
		TurnedBox(lens, face, { 0.f, 0.f, 0.006f }, { wide - 0.075f, FACE - 0.045f, 0.006f }, { 0.96f, 0.96f, 0.9f });
		add(PartType::HEADLIGHT, pod);
		AddModel(ShopModels::HEADLIGHT_LENS, lens);
	}
	{
		// The tail light: red stop and tail lamp, amber indicator and the white reverse lamp, in a black frame.
		MeshBuilder lamp;
		Box(lamp, { 0.f, 0.07f, 0.f }, { 0.52f, 0.15f, 0.07f }, TRIM_BLACK);
		Box(lamp, { -0.11f, 0.07f, 0.f }, { 0.27f, 0.12f, 0.08f }, LENS_RED);
		Box(lamp, { 0.1f, 0.095f, 0.f }, { 0.13f, 0.07f, 0.08f }, AMBER);
		Box(lamp, { 0.1f, 0.035f, 0.f }, { 0.13f, 0.045f, 0.08f }, { 0.92f, 0.92f, 0.9f });
		Box(lamp, { 0.2f, 0.07f, 0.f }, { 0.06f, 0.12f, 0.08f }, LENS_RED);
		add(PartType::TAIL_LIGHT, lamp);
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
		Prism(dash, { { -0.25f, 0.f }, { 0.05f, 0.f }, { 0.25f, 0.2f }, { 0.25f, 0.27f }, { -0.15f, 0.33f }, { -0.25f, 0.3f } }, -0.7f, 0.7f, VINYL,
			glm::rotate(glm::mat4(1.f), -glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f)));
		Box(dash, { 0.37f, 0.33f, -0.08f }, { 0.42f, 0.05f, 0.2f }, VINYL);							// the hood over the gauges
		Box(dash, { 0.f, 0.14f, -0.24f }, { 0.26f, 0.24f, 0.06f }, TRIM_BLACK);						// the centre stack
		Box(dash, { 0.f, 0.2f, -0.272f }, { 0.18f, 0.045f, 0.006f }, ALLOY);							// the radio
		for (float x : { -0.55f, -0.08f, 0.08f, 0.6f })
			Box(dash, { x, 0.24f, -0.252f }, { 0.1f, 0.04f, 0.01f }, TRIM_BLACK);						// the vents
		Box(dash, { -0.38f, 0.16f, -0.251f }, { 0.36f, 0.12f, 0.004f }, { 0.2f, 0.19f, 0.19f });		// the glovebox lid
		add(PartType::DASHBOARD, dash);
	}
	{
		// Facing the driver (-z on the dash; +x is their left): two big dials, the speedo on the left and the tach on
		// the right, and between them the fuel gauge over the temperature gauge. Each has its marks round its face
		// (a turn about z is clockwise to the driver, from straight up); the needles are models of their own, which
		// ShopWorld turns (see GaugeTurn there, which these marks are laid out by).
		MeshBuilder cluster;
		Box(cluster, { 0.f, GAUGE_Y, 0.f }, { 0.38f, 0.15f, 0.1f }, TRIM_BLACK);
		const glm::vec3 FACE = { 0.04f, 0.04f, 0.05f }, MARK = { 0.9f, 0.9f, 0.86f }, RED = { 0.85f, 0.1f, 0.08f };
		auto dial = [&](float x, float y, float radius, float sweep, int marks, int redFrom) {
			Tube(cluster, AlongZ({ x, y, -0.05f }), radius + 0.006f, radius, 0.008f, ALLOY, 28);
			Cylinder(cluster, AlongZ({ x, y, -0.051f }), radius, 0.004f, FACE, 28);
			for (int mark = 0; mark <= marks; mark++)
			{
				float turn = sweep * (static_cast<float>(mark) / marks - 0.5f);
				TurnedBox(cluster, glm::rotate(At({ x, y, -0.0535f }), turn, glm::vec3(0.f, 0.f, 1.f)), { 0.f, radius * 0.84f, 0.f },
					{ radius * 0.07f, radius * 0.2f, 0.001f }, mark >= redFrom ? RED : MARK);
			}
			Cylinder(cluster, AlongZ({ x, y, -0.0545f }), radius * 0.13f, 0.004f, TRIM_BLACK, 12);
		};
		dial(GAUGE_BIG_X, GAUGE_Y, GAUGE_BIG, GAUGE_SWEEP, 10, 99);		// 0 to 200 km/h, a mark every 20
		dial(-GAUGE_BIG_X, GAUGE_Y, GAUGE_BIG, GAUGE_SWEEP, 8, 6);		// 0 to 8,000 rpm, a mark every thousand
		dial(0.f, GAUGE_Y + GAUGE_SMALL_Y, GAUGE_SMALL, GAUGE_SMALL_SWEEP, 2, 99);	// empty, half, full
		dial(0.f, GAUGE_Y - GAUGE_SMALL_Y, GAUGE_SMALL, GAUGE_SMALL_SWEEP, 2, 2);	// cold, normal, hot
		add(PartType::INSTRUMENT_CLUSTER, cluster);

		// A needle: from its pivot up along +y, as long as a big dial wants (the little ones' are scaled down).
		MeshBuilder needle;
		Box(needle, { 0.f, GAUGE_BIG * 0.36f, 0.f }, { 0.004f, GAUGE_BIG * 0.92f, 0.0015f }, { 1.f, 0.42f, 0.1f });
		AddModel(ShopModels::GAUGE_NEEDLE, needle);
	}
	{
		// A bucket seat facing +x: the cushion between its bolsters on a black frame, the back reclined with its own
		// bolsters, and the headrest.
		MeshBuilder seat;
		Box(seat, { 0.02f, 0.03f, 0.f }, { 0.42f, 0.06f, 0.42f }, TRIM_BLACK);
		Box(seat, { 0.02f, 0.1f, 0.f }, { 0.48f, 0.09f, 0.42f }, VINYL);
		for (float side : { -1.f, 1.f })
			Box(seat, { 0.02f, 0.13f, side * 0.23f }, { 0.48f, 0.12f, 0.06f }, VINYL);
		glm::mat4 back = glm::rotate(At({ -0.2f, 0.13f, 0.f }), 0.26f, glm::vec3(0.f, 0.f, 1.f));
		TurnedBox(seat, back, { 0.f, 0.28f, 0.f }, { 0.1f, 0.56f, 0.44f }, VINYL);
		for (float side : { -1.f, 1.f })
			TurnedBox(seat, back, { 0.04f, 0.26f, side * 0.23f }, { 0.14f, 0.5f, 0.06f }, VINYL);
		TurnedBox(seat, back, { 0.f, 0.62f, 0.f }, { 0.09f, 0.14f, 0.26f }, VINYL);
		add(PartType::SEAT, seat);
	}
	{
		MeshBuilder wheel;
		// Three spokes (on the car +x is the top of the wheel): one out to each side, a little under the hub, and one
		// down, so the gauges show through the top half.
		Tube(wheel, At({ 0.f, 0.015f, 0.f }), 0.19f, 0.17f, 0.03f, TRIM_BLACK, 28);
		Box(wheel, { -0.025f, 0.015f, 0.f }, { 0.035f, 0.02f, 0.345f }, ALLOY);
		Box(wheel, { -0.1f, 0.015f, 0.f }, { 0.16f, 0.02f, 0.035f }, ALLOY);
		Cylinder(wheel, At({ -0.01f, 0.02f, 0.f }), 0.035f, 0.04f, TRIM_BLACK, 16);
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
	lying(PartType::FRONT_SWAY_BAR, 0.011f, 0.96f, TRIM_BLACK);	// short of the tires on full lock

	// Rear suspension: the axle across, the diff in its nose.
	{
		MeshBuilder axle;
		Cylinder(axle, AlongX({ 0.f, 0.15f, 0.f }), 0.04f, 1.32f, ARM_GREY, 16);
		Cylinder(axle, AlongZ({ 0.f, 0.15f, 0.f }), 0.13f, 0.12f, ARM_GREY, 20);
		for (float side : { -1.f, 1.f })
			Cylinder(axle, AlongX({ side * 0.67f, 0.15f, 0.f }), 0.08f, 0.012f, IRON, 20);
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
		// off it on the driver's side.
		Cylinder(rack, AlongX({ 0.f, 0.026f, 0.f }), 0.026f, 0.62f, ALLOY, 14);
		for (float side : { -1.f, 1.f })
			Cylinder(rack, AlongX({ side * 0.37f, 0.026f, 0.f }), 0.022f, 0.12f, RUBBER, 12);
		Cylinder(rack, At({ -0.18f, 0.1f, 0.f }), 0.02f, 0.1f, ALLOY, 12);
		add(PartType::STEERING_RACK, rack);
	}
	lying(PartType::TIE_ROD_END, 0.012f, 0.16f, IRON);
	lying(PartType::STEERING_COLUMN, 0.03f, 0.8f, TRIM_BLACK);

	// Brakes
	lying(PartType::BRAKE_BOOSTER, 0.11f, 0.13f, TRIM_BLACK);
	{
		MeshBuilder master;
		Cylinder(master, AlongX({ 0.f, 0.025f, 0.f }), 0.025f, 0.2f, ALLOY, 14);
		Box(master, { 0.f, 0.07f, 0.f }, { 0.12f, 0.05f, 0.06f }, OFF_WHITE);
		add(PartType::BRAKE_MASTER_CYLINDER, master);
	}
	box(PartType::BRAKE_LINE_SET, { 1.2f, 0.02f, 0.12f }, COPPER);
	upright(PartType::BRAKE_HOSE, 0.006f, 0.35f, RUBBER);
	upright(PartType::BRAKE_ROTOR, 0.121f, 0.018f, IRON);
	box(PartType::BRAKE_PAD_SET, { 0.1f, 0.03f, 0.05f }, LINING);
	box(PartType::BRAKE_CALIPER, { 0.12f, 0.1f, 0.08f }, IRON);
	lying(PartType::WHEEL_CYLINDER, 0.012f, 0.06f, IRON);
	box(PartType::BRAKE_SHOE_SET, { 0.2f, 0.05f, 0.04f }, LINING);
	upright(PartType::BRAKE_DRUM, 0.11f, 0.06f, IRON);
	lying(PartType::PARKING_BRAKE_CABLE, 0.004f, 1.4f, TRIM_BLACK);

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
		Tube(gearbox, AlongX({ -0.294f, 0.15f, 0.f }), 0.176f, 0.15f, 0.012f, ALLOY, 28);
		Cylinder(gearbox, AlongX({ -0.105f, 0.15f, 0.f }), 0.16f, 0.01f, ALLOY, 28);
		Box(gearbox, { 0.07f, 0.15f, 0.f }, { 0.34f, 0.2f, 0.18f }, ALLOY);
		for (float x : { -0.03f, 0.07f, 0.17f })
			Box(gearbox, { x, 0.15f, 0.f }, { 0.012f, 0.22f, 0.2f }, ALLOY);			// the case's ribs
		Cylinder(gearbox, AlongX({ 0.44f, 0.15f, 0.f }), 0.055f, 0.4f, ALLOY, 18);
		Cylinder(gearbox, AlongX({ 0.632f, 0.15f, 0.f }), 0.045f, 0.016f, IRON, 18);	// the output flange
		Box(gearbox, { 0.59f, 0.215f, 0.f }, { 0.07f, 0.03f, 0.07f }, ALLOY);
		Cylinder(gearbox, At({ 0.59f, 0.255f, 0.f }), 0.035f, 0.05f, RUBBER, 12);
		Cylinder(gearbox, At({ 0.59f, 0.35f, 0.f }), 0.008f, 0.2f, ALLOY, 10);
		gearbox.SetColor(TRIM_BLACK).AddSphere({ 0.59f, 0.46f, 0.f }, 0.022f, 6, 10);
		add(PartType::TRANSMISSION, gearbox);
	}
	box(PartType::TRANSMISSION_MOUNT, { 0.22f, 0.06f, 0.08f }, RUBBER);
	upright(PartType::CLUTCH_MASTER_CYLINDER, 0.018f, 0.15f, ALLOY);
	upright(PartType::CLUTCH_RELEASE_CYLINDER, 0.015f, 0.1f, ALLOY);
	lying(PartType::DRIVESHAFT, 0.035f, 1.07f, TRIM_BLACK);		// from the gearbox's tail to the diff's nose

	// Fuel
	box(PartType::FUEL_TANK, { 0.8f, 0.25f, 0.6f }, TRIM_BLACK);
	upright(PartType::FUEL_SENDER, 0.04f, 0.25f, ALLOY);
	box(PartType::FUEL_LINE_SET, { 1.2f, 0.02f, 0.08f }, ALLOY);
	upright(PartType::FUEL_FILTER, 0.03f, 0.1f, OFF_WHITE);

	// Electrical
	{
		MeshBuilder battery;
		Box(battery, { 0.f, 0.09f, 0.f }, { 0.23f, 0.18f, 0.17f }, TRIM_BLACK);
		Cylinder(battery, At({ -0.08f, 0.19f, 0.f }), 0.01f, 0.02f, LENS_RED, 10);
		Cylinder(battery, At({ 0.08f, 0.19f, 0.f }), 0.01f, 0.02f, ALLOY, 10);
		add(PartType::BATTERY, battery);
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
	loom(PartType::ENGINE_HARNESS, 0.5f, 3);
	box(PartType::FUSE_BOX, { 0.2f, 0.05f, 0.12f }, OFF_WHITE);
	upright(PartType::IGNITION_COIL, 0.03f, 0.13f, TRIM_BLACK);
	{
		MeshBuilder starter;
		Cylinder(starter, AlongX({ 0.f, 0.045f, 0.f }), 0.045f, 0.18f, TRIM_BLACK, 16);
		Cylinder(starter, AlongX({ 0.f, 0.095f, 0.f }), 0.02f, 0.1f, ALLOY, 12);
		add(PartType::STARTER, starter);
	}

	// Cooling
	{
		MeshBuilder radiator;
		Box(radiator, { 0.f, 0.2f, 0.f }, { 0.66f, 0.36f, 0.04f }, TRIM_BLACK);
		Box(radiator, { 0.f, 0.395f, 0.f }, { 0.68f, 0.05f, 0.06f }, COPPER);
		Box(radiator, { 0.f, 0.015f, 0.f }, { 0.68f, 0.03f, 0.06f }, COPPER);
		add(PartType::RADIATOR, radiator);
	}
	lying(PartType::RADIATOR_HOSE, 0.02f, 0.4f, RUBBER);
	{
		MeshBuilder fan;
		Tube(fan, At({ 0.f, 0.03f, 0.f }), 0.17f, 0.16f, 0.06f, TRIM_BLACK, 28);
		for (int blade = 0; blade < 4; blade++)
		{
			fan.SetTransform(glm::rotate(At({ 0.f, 0.03f, 0.f }), blade * glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f))).SetColor(TRIM_BLACK);
			fan.AddBox({ 0.08f, 0.f, 0.f }, { 0.15f, 0.01f, 0.05f });
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

	// Exhaust
	box(PartType::CATALYTIC_CONVERTER, { 0.4f, 0.1f, 0.2f }, ALLOY);
	{
		// Slim enough to hang under the floor.
		MeshBuilder muffler;
		Cylinder(muffler, AlongX({ 0.f, 0.075f, 0.f }), 0.075f, 0.5f, IRON, 18);
		Cylinder(muffler, AlongX({ 0.45f, 0.075f, 0.f }), 0.024f, 0.45f, IRON, 12);
		add(PartType::MUFFLER, muffler);
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
		Cylinder(wheel, AlongX({ 0.f, TIRE_RADIUS, 0.f }), 0.035f, 0.15f, { 0.8f, 0.81f, 0.83f }, 16);
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
	Box(car, { (tubFront + FOOTWELL_X) * 0.5f, 0.2f, 0.f }, { FOOTWELL_X - tubFront, 0.04f, 1.5f }, UNDERSEAL);
	// (Under the engine there's no floor: the pan stops at the firewall.)
	Box(car, { (FOOTWELL_X + FIREWALL_X + 0.015f) * 0.5f, 0.2f, 0.f }, { FIREWALL_X + 0.015f - FOOTWELL_X, 0.04f, 0.82f }, UNDERSEAL);
	Box(car, { REAR_AXLE_X, 0.2f, 0.f }, { tubFront - tubBack, 0.04f, 2.f * REAR_TUB_WALL_Z }, UNDERSEAL);
	Box(car, { (tubBack - 1.95f) * 0.5f, 0.2f, 0.f }, { tubBack + 1.95f, 0.04f, 1.5f }, UNDERSEAL);
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
		Box(car, { -1.8f, RUB_STRIP_Y, side * (SIDE + 0.002f) }, { 0.5f, 0.012f, 0.004f }, CREASE);
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
		SidePanel(car, 0.92f, 2.03f, Arched(0.27f, FRONT_AXLE_X, AXLE_Y, FRONT_TUB + 0.01f), [](float x) { return HoodY(x) - 0.045f; }, side * 0.655f, 0.03f, PRIMER, 44);
		// The engine's mount sits on a perch off the chassis rail, braced under it.
		Box(car, { ENGINE_X, ENGINE_MOUNT_Y - 0.015f, side * (ENGINE_MOUNT_Z + 0.025f) }, { 0.15f, 0.03f, 0.17f }, PRIMER);
		Prism(car, { { ENGINE_MOUNT_Z - 0.05f, ENGINE_MOUNT_Y - 0.03f }, { 0.32f, ENGINE_MOUNT_Y - 0.03f }, { 0.32f, 0.215f } }, -0.006f, 0.006f, PRIMER,
			glm::rotate(At({ ENGINE_X, 0.f, 0.f }), side * -glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f)));
		// The chassis rail, inboard of where the tire's front swings in on full lock.
		Box(car, { 1.55f, 0.25f, side * 0.36f }, { 1.3f, 0.08f, 0.08f }, PRIMER);
		// The front wheel's housing where it comes back into the footwell: the tire's back swings in here on full lock.
		Box(car, { FOOTWELL_X, 0.36f, side * 0.595f }, { 0.02f, 0.32f, 0.31f }, UNDERSEAL);
		Box(car, { (FOOTWELL_X + 0.88f) * 0.5f, 0.36f, side * 0.44f }, { 0.88f - FOOTWELL_X + 0.02f, 0.32f, 0.02f }, UNDERSEAL);
		Box(car, { (FOOTWELL_X + 0.88f) * 0.5f, 0.52f, side * 0.595f }, { 0.88f - FOOTWELL_X + 0.02f, 0.02f, 0.33f }, UNDERSEAL);
		Box(car, { -1.2f, 0.66f, side * 0.52f }, { 0.3f, 0.08f, 0.2f }, UNDERSEAL);
	}
	// The battery's tray, on a bracket off the left inner wing.
	Box(car, { 1.66f, 0.51f, -0.5f }, { 0.27f, 0.02f, 0.21f }, PRIMER);
	Box(car, { 1.7f, 0.46f, -0.62f }, { 0.14f, 0.1f, 0.04f }, PRIMER);
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
	Box(car, { 2.03f, 0.46f, 0.f }, { 0.04f, 0.38f, 1.3f }, PRIMER);
	Box(car, { NOSE_X - 0.02f, 0.43f, 0.f }, { 0.04f, 0.46f, 1.53f }, TRIM_BLACK);
	Box(car, { NOSE_X - 0.02f, 0.66f, 0.f }, { 0.04f, 0.05f, 2.f * LAMP_LID_INNER }, TRIM_BLACK);
	// The tail panel, the black garnish between the tail lights with the number plate on it, the deck under the
	// rear window, and the shelf behind the back seat.
	Box(car, { TAIL_X + 0.015f, 0.53f, 0.f }, { 0.03f, 0.66f, 1.53f }, BODY_WHITE);
	Box(car, { TAIL_X - 0.002f, 0.63f, 0.f }, { 0.004f, 0.15f, 0.48f }, TRIM_BLACK);
	Box(car, { TAIL_X - 0.005f, 0.63f, 0.f }, { 0.004f, 0.11f, 0.32f }, OFF_WHITE);
	Box(car, { (REAR_WINDOW_BASE_X + TRUNK_FRONT_X) * 0.5f, BELT_Y - 0.01f, 0.f }, { REAR_WINDOW_BASE_X - TRUNK_FRONT_X, 0.02f, 2.f * TRUNK_HALF }, BODY_WHITE);
	Box(car, { -1.27f, 0.84f, 0.f }, { 0.46f, 0.02f, 1.44f }, VINYL);
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
		Box(car, { -0.65f, (0.22f + TUNNEL_TOP) * 0.5f, 0.f }, { 0.015f, TUNNEL_TOP - 0.22f, 2.f * TUNNEL }, UNDERSEAL);
	}
	Box(car, { -0.92f, 0.3f, 0.f }, { 0.46f, 0.16f, 1.14f }, VINYL);
	TurnedBox(car, glm::rotate(At({ -1.12f, 0.38f, 0.f }), 0.2f, glm::vec3(0.f, 0.f, 1.f)), { 0.f, 0.22f, 0.f }, { 0.1f, 0.46f, 1.14f }, VINYL);
	AddModel(ShopModels::PROJECT_CAR, car);
	AddModel(ShopModels::CAR_QUARTER_GLASS, quarterGlass);

	// The stands it's up on at its jacking points, till it's on its own wheels (a model of their own, taken away then).
	MeshBuilder stands;
	for (float x : { 0.45f, -0.75f })
		for (float side : { -1.f, 1.f })
		{
			Cylinder(stands, At({ x, 0.01f, side * 0.7f }), 0.1f, 0.02f, SPRING_RED, 4);
			Cylinder(stands, At({ x, 0.1f, side * 0.7f }), 0.02f, 0.18f, SPRING_RED, 8);
		}
	AddModel(ShopModels::CAR_JACK_STANDS, stands);
}
