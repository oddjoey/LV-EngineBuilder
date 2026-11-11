// The project car's models: stand-ins for its parts, and the car itself on the drive.
#include "ShopAssets.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "LV/Engine/Graphics/Models/MeshBuilder.h"
#include "../Simulation/PartCatalog.h"

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

	// The tire's radius, standing: a wheel and its tire are both centred this high, so one sits on the other.
	constexpr float TIRE_RADIUS = 0.29f;
}

void ShopAssets::LoadCarParts()
{
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

	// Body
	{
		// The shell as a part: it only ever sits on the drive as the car, but every part has a model.
		MeshBuilder shell;
		Box(shell, { 0.f, 0.55f, 0.f }, { 4.2f, 0.6f, 1.6f }, BODY_WHITE);
		Box(shell, { -0.15f, 1.05f, 0.f }, { 1.9f, 0.4f, 1.36f }, BODY_WHITE);
		add(PartType::BODY_SHELL, shell);
	}
	{
		// From the door to the nose, cut round the front wheel's arch (the axle 0.3 m back from its middle, 0.29 m
		// above the car's floor line, which is 0.27 m below the fender), its front corner rolled under toward the
		// headlight, and its top edge turned in to bolt to the apron.
		MeshBuilder fender;
		auto top = [](float x) { return x < 0.5f ? 0.55f : 0.55f - (x - 0.5f) * 0.6f; };
		SidePanel(fender, -0.625f, 0.625f, Arched(0.f, -0.3f, 0.02f, 0.34f), top, 0.f, 0.04f, BODY_WHITE);
		ArchLip(fender, -0.3f, 0.02f, 0.34f, 0.f, 0.06f, BODY_WHITE);
		Box(fender, { -0.0625f, 0.545f, -0.03f }, { 1.125f, 0.012f, 0.08f }, BODY_WHITE);
		add(PartType::FRONT_FENDER, fender);
	}
	{
		// From the A-pillar back to the B-pillar: the skin up to the beltline, the glass above it (its front edge
		// leaning back with the windshield), a rubbing strip, the beltline trim and a handle, on both faces so one
		// model does either side.
		MeshBuilder door;
		auto top = [](float x) { return x > 0.55f ? 0.64f - (x - 0.55f) * 0.5f : 0.64f; };
		SidePanel(door, -0.625f, 0.625f, Level(0.02f), top, 0.f, 0.08f, BODY_WHITE);
		Prism(door, { { -0.6f, 0.64f }, { 0.6f, 0.64f }, { 0.27f, 1.0f }, { -0.6f, 1.0f } }, -0.006f, 0.006f, GLASS);
		for (float face : { -1.f, 1.f })
		{
			Box(door, { 0.f, 0.635f, face * 0.043f }, { 1.24f, 0.022f, 0.008f }, TRIM_BLACK);		// the beltline trim
			Box(door, { 0.f, 0.3f, face * 0.043f }, { 1.24f, 0.035f, 0.008f }, TRIM_BLACK);		// the rubbing strip
			Box(door, { -0.42f, 0.53f, face * 0.046f }, { 0.13f, 0.03f, 0.012f }, ALLOY);			// the handle
		}
		Box(door, { -0.6f, 0.82f, 0.f }, { 0.025f, 0.36f, 0.03f }, TRIM_BLACK);				// the glass's rear run
		add(PartType::DOOR, door);
	}
	{
		// The hood: flat, its nose rounded down over the grille.
		MeshBuilder hood;
		Prism(hood, { { -0.55f, 0.f }, { 0.52f, 0.f }, { 0.55f, 0.018f }, { 0.53f, 0.04f }, { -0.55f, 0.05f } }, -0.675f, 0.675f, BODY_WHITE);
		add(PartType::HOOD, hood);
	}
	{
		// The trunk lid, its back edge turned down over the tail panel.
		MeshBuilder lid;
		Prism(lid, { { -0.26f, 0.f }, { 0.26f, 0.f }, { 0.26f, 0.04f }, { -0.24f, 0.04f }, { -0.26f, 0.025f } }, -0.65f, 0.65f, BODY_WHITE);
		add(PartType::TRUNK_LID, lid);
	}
	// The big black urethane bumpers, wrapped round the corners. Built across the car with their face toward +z
	// (the front bumper) or -z (the rear, turned the same way on the car).
	auto bumper = [&](PartType type, float face) {
		MeshBuilder bumper;
		Box(bumper, { 0.f, 0.1f, face * 0.03f }, { 1.3f, 0.19f, 0.16f }, TRIM_BLACK);
		for (float end : { -1.f, 1.f })
			TurnedBox(bumper, glm::rotate(At({ end * 0.7f, 0.1f, face * -0.01f }), end * face * 0.55f, glm::vec3(0.f, 1.f, 0.f)), {}, { 0.2f, 0.19f, 0.12f }, TRIM_BLACK);
		Box(bumper, { 0.f, 0.12f, face * 0.111f }, { 1.24f, 0.035f, 0.004f }, { 0.22f, 0.22f, 0.23f });	// the rubbing strip
		Box(bumper, { 0.f, 0.004f, face * 0.01f }, { 1.2f, 0.008f, 0.12f }, TRIM_BLACK);
		add(type, bumper);
	};
	bumper(PartType::FRONT_BUMPER, 1.f);
	bumper(PartType::REAR_BUMPER, -1.f);
	{
		// Two rectangular lamps side by side behind a bright bezel, in a black bucket.
		MeshBuilder pod;
		Box(pod, { 0.f, 0.06f, 0.f }, { 0.42f, 0.12f, 0.3f }, TRIM_BLACK);
		Box(pod, { 0.f, 0.065f, 0.151f }, { 0.41f, 0.11f, 0.004f }, ALLOY);
		for (float x : { -0.1f, 0.1f })
			Box(pod, { x, 0.065f, 0.155f }, { 0.18f, 0.085f, 0.006f }, { 0.93f, 0.94f, 0.9f });
		add(PartType::HEADLIGHT, pod);
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
		// Glass in its black rubber gasket.
		auto glass = [&](PartType type, float length, float width) {
			MeshBuilder pane;
			Box(pane, { 0.f, 0.006f, 0.f }, { length, 0.012f, width }, GLASS);
			for (float side : { -1.f, 1.f })
			{
				Box(pane, { 0.f, 0.008f, side * (width * 0.5f - 0.012f) }, { length, 0.016f, 0.024f }, TRIM_BLACK);
				Box(pane, { side * (length * 0.5f - 0.012f), 0.008f, 0.f }, { 0.024f, 0.016f, width }, TRIM_BLACK);
			}
			add(type, pane);
		};
		glass(PartType::WINDSHIELD, 1.3f, 0.62f);
		glass(PartType::REAR_WINDOW, 1.2f, 0.6f);
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
		// Two big dials, the tach and the speedo, facing the driver (-z on the dash).
		MeshBuilder cluster;
		Box(cluster, { 0.f, 0.07f, 0.f }, { 0.35f, 0.14f, 0.1f }, TRIM_BLACK);
		for (float x : { -0.085f, 0.085f })
		{
			Tube(cluster, AlongZ({ x, 0.07f, -0.05f }), 0.058f, 0.05f, 0.008f, ALLOY, 24);
			Cylinder(cluster, AlongZ({ x, 0.07f, -0.051f }), 0.05f, 0.004f, { 0.05f, 0.05f, 0.06f }, 24);
			TurnedBox(cluster, glm::rotate(At({ x, 0.07f, -0.054f }), 0.7f, glm::vec3(0.f, 0.f, 1.f)), { 0.f, 0.02f, 0.f }, { 0.004f, 0.04f, 0.002f }, AMBER);
		}
		add(PartType::INSTRUMENT_CLUSTER, cluster);
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
		Tube(wheel, At({ 0.f, 0.015f, 0.f }), 0.19f, 0.17f, 0.03f, TRIM_BLACK, 28);
		Box(wheel, { 0.f, 0.015f, 0.f }, { 0.34f, 0.02f, 0.03f }, ALLOY);
		Cylinder(wheel, At({ 0.f, 0.02f, 0.f }), 0.04f, 0.04f, TRIM_BLACK, 16);
		add(PartType::STEERING_WHEEL, wheel);
	}

	// Front suspension: the strut stands up, and its spring fits round it.
	{
		MeshBuilder strut;
		Cylinder(strut, At({ 0.f, 0.22f, 0.f }), 0.026f, 0.44f, IRON, 16);
		Cylinder(strut, At({ 0.f, 0.5f, 0.f }), 0.011f, 0.14f, ALLOY, 12);
		Cylinder(strut, At({ 0.f, 0.14f, 0.f }), 0.075f, 0.008f, IRON, 20);
		Cylinder(strut, At({ 0.f, 0.56f, 0.f }), 0.07f, 0.02f, IRON, 20);
		add(PartType::FRONT_STRUT, strut);
	}
	{
		MeshBuilder spring;
		for (int coil = 0; coil < 6; coil++)
			Tube(spring, At({ 0.f, 0.16f + coil * 0.065f, 0.f }), 0.068f, 0.055f, 0.012f, SPRING_RED, 20);
		add(PartType::FRONT_COIL_SPRING, spring);
	}
	{
		MeshBuilder knuckle;
		Box(knuckle, { 0.f, 0.15f, 0.f }, { 0.08f, 0.3f, 0.1f }, IRON);
		Cylinder(knuckle, AlongX({ 0.05f, 0.12f, 0.f }), 0.045f, 0.06f, ALLOY, 16);
		Box(knuckle, { 0.f, 0.04f, 0.1f }, { 0.05f, 0.04f, 0.16f }, IRON);
		add(PartType::STEERING_KNUCKLE, knuckle);
	}
	box(PartType::LOWER_CONTROL_ARM, { 0.42f, 0.05f, 0.12f }, ARM_GREY);
	lying(PartType::STRUT_ROD, 0.012f, 0.5f, ARM_GREY);
	lying(PartType::FRONT_SWAY_BAR, 0.011f, 1.1f, TRIM_BLACK);

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
		Cylinder(rack, AlongX({ 0.f, 0.03f, 0.f }), 0.03f, 0.62f, ALLOY, 14);
		for (float side : { -1.f, 1.f })
			Cylinder(rack, AlongX({ side * 0.37f, 0.03f, 0.f }), 0.024f, 0.12f, RUBBER, 12);
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
	box(PartType::ENGINE_MOUNT, { 0.12f, 0.08f, 0.08f }, RUBBER);
	upright(PartType::CLUTCH_DISC, 0.1f, 0.009f, LINING);
	upright(PartType::PRESSURE_PLATE, 0.11f, 0.04f, ALLOY);
	{
		MeshBuilder bearing;
		Tube(bearing, At({ 0.f, 0.015f, 0.f }), 0.03f, 0.016f, 0.03f, ALLOY, 20);
		add(PartType::RELEASE_BEARING, bearing);
	}
	{
		MeshBuilder gearbox;
		Cylinder(gearbox, AlongX({ -0.12f, 0.15f, 0.f }), 0.15f, 0.2f, ALLOY, 24);
		Box(gearbox, { 0.15f, 0.12f, 0.f }, { 0.38f, 0.2f, 0.18f }, ALLOY);
		Cylinder(gearbox, At({ 0.12f, 0.3f, 0.f }), 0.008f, 0.16f, TRIM_BLACK, 10);
		add(PartType::TRANSMISSION, gearbox);
	}
	box(PartType::TRANSMISSION_MOUNT, { 0.22f, 0.06f, 0.08f }, RUBBER);
	upright(PartType::CLUTCH_MASTER_CYLINDER, 0.018f, 0.15f, ALLOY);
	upright(PartType::CLUTCH_RELEASE_CYLINDER, 0.015f, 0.1f, ALLOY);
	lying(PartType::DRIVESHAFT, 0.035f, 1.3f, TRIM_BLACK);

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
	box(PartType::MAIN_HARNESS, { 0.6f, 0.1f, 0.4f }, TRIM_BLACK);
	box(PartType::ENGINE_HARNESS, { 0.45f, 0.08f, 0.3f }, TRIM_BLACK);
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
	box(PartType::CATALYTIC_CONVERTER, { 0.4f, 0.12f, 0.2f }, ALLOY);
	{
		MeshBuilder muffler;
		Cylinder(muffler, AlongX({ 0.f, 0.1f, 0.f }), 0.1f, 0.5f, IRON, 18);
		Cylinder(muffler, AlongX({ 0.45f, 0.1f, 0.f }), 0.024f, 0.45f, IRON, 12);
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
		Cylinder(wheel, AlongX({ 0.f, TIRE_RADIUS, 0.f }), 0.157f, 0.012f, { 0.62f, 0.63f, 0.65f }, 32);
		for (int vent = 0; vent < 6; vent++)
		{
			float angle = vent * glm::two_pi<float>() / 6.f;
			Cylinder(wheel, AlongX({ 0.f, TIRE_RADIUS + std::sin(angle) * 0.105f, std::cos(angle) * 0.105f }), 0.026f, 0.014f, TRIM_BLACK, 12);
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
	// The bare shell, as the car stands on the drive: everything welded (floor, sills, roof and pillars, the rear
	// quarters, the firewall, strut towers and aprons), with the openings where the bolt-on panels, glass and
	// lights go, up on four jack stands. Its nose (the engine bay) is +X, its right +Z.
	MeshBuilder car;
	// A plate between two points in the side view (x, y), so wide across the car, centred at z.
	auto slab = [&](glm::vec2 from, glm::vec2 to, float z, float width, float thick, const glm::vec3& color) {
		glm::vec2 run = to - from, middle = (from + to) * 0.5f;
		car.SetTransform(glm::rotate(At({ middle.x, middle.y, z }), std::atan2(run.y, run.x), glm::vec3(0.f, 0.f, 1.f))).SetColor(color);
		car.AddBox({}, { glm::length(run), thick, width });
		car.ResetTransform();
	};
	const glm::vec3 PRIMER = { 0.62f, 0.62f, 0.6f };		// bare metal and primer where the paint's been taken back

	Box(car, { -0.15f, 0.2f, 0.f }, { 3.6f, 0.04f, 1.5f }, PRIMER);					// the floor pan, front to boot
	for (float side : { -1.f, 1.f })
	{
		float z = side * 0.79f;
		// The sill under the door, its bottom edge rolled under.
		SidePanel(car, -0.45f, 0.85f, Level(0.2f), Level(0.42f), z, 0.05f, BODY_WHITE, 4);
		Box(car, { 0.2f, 0.205f, side * 0.76f }, { 1.3f, 0.03f, 0.06f }, PRIMER);
		// The rear quarter, from the door's shut line back to the tail, cut round the rear arch, the beltline
		// rising a touch to the deck, with its arch lip.
		auto beltline = [](float x) { return 0.86f + (std::max)(0.f, -0.45f - x) * 0.01f; };
		SidePanel(car, -2.07f, -0.43f, Arched(0.25f, -1.2f, 0.29f, 0.34f), beltline, z, 0.04f, BODY_WHITE, 48);
		ArchLip(car, -1.2f, 0.29f, 0.34f, z, 0.06f, BODY_WHITE);
		// A black rubbing strip along the quarter, in line with the door's.
		Box(car, { -0.73f, 0.52f, side * 0.812f }, { 0.58f, 0.035f, 0.008f }, TRIM_BLACK);
		Box(car, { -1.85f, 0.52f, side * 0.812f }, { 0.42f, 0.035f, 0.008f }, TRIM_BLACK);
		// The fuel filler flap on the right quarter.
		if (side > 0.f)
			Box(car, { -1.68f, 0.72f, z + 0.021f }, { 0.12f, 0.12f, 0.004f }, { 0.8f, 0.8f, 0.77f });
		Box(car, { -0.48f, 1.05f, side * 0.7f }, { 0.12f, 0.38f, 0.05f }, BODY_WHITE);	// the B-pillar
		// The fixed quarter window, filling the opening: the B-pillar's back edge in front, the roof over it, the
		// beltline under it, and behind, the C-pillar's front edge sloping down from the roof to the deck.
		Prism(car, { { -0.535f, 0.86f }, { -1.495f, 0.86f }, { -0.94f, 1.245f }, { -0.535f, 1.245f } },
			side * 0.69f - 0.006f, side * 0.69f + 0.006f, GLASS);
		slab({ 0.92f, 0.86f }, { 0.45f, 1.24f }, side * 0.66f, 0.06f, 0.06f, BODY_WHITE);	// the A-pillar
		slab({ -1.0f, 1.24f }, { -1.52f, 0.88f }, side * 0.66f, 0.10f, 0.06f, BODY_WHITE);	// the C-pillar
		// The front strut tower: a top plate the strut's mount bolts up under (its nuts on top), walled in from the bay.
		Box(car, { 1.2f, 0.745f, side * 0.55f }, { 0.24f, 0.02f, 0.2f }, PRIMER);
		Box(car, { 1.2f, 0.6f, side * 0.44f }, { 0.24f, 0.29f, 0.02f }, PRIMER);
		Box(car, { 1.5f, 0.45f, side * 0.64f }, { 1.2f, 0.35f, 0.03f }, PRIMER);		// the inner wing (apron)
		Box(car, { 1.55f, 0.25f, side * 0.45f }, { 1.3f, 0.08f, 0.08f }, PRIMER);		// the front chassis rail
		Box(car, { -1.15f, 0.55f, side * 0.6f }, { 0.25f, 0.3f, 0.2f }, PRIMER);		// the rear spring and shock tower
	}
	Box(car, { -0.27f, 1.255f, 0.f }, { 1.45f, 0.03f, 1.36f }, BODY_WHITE);			// the roof
	Box(car, { 0.5f, 1.235f, 0.f }, { 0.1f, 0.03f, 1.34f }, BODY_WHITE);				// the windshield header
	Box(car, { 0.88f, 0.55f, 0.f }, { 0.03f, 0.66f, 1.5f }, PRIMER);					// the firewall
	Box(car, { 0.95f, 0.86f, 0.f }, { 0.16f, 0.02f, 1.4f }, BODY_WHITE);				// the cowl, under the windshield
	Box(car, { 2.03f, 0.5f, 0.f }, { 0.04f, 0.46f, 1.3f }, PRIMER);					// the radiator support
	// The header panel across the nose between the headlights, and the black slatted grille in it.
	Box(car, { 2.07f, 0.775f, 0.f }, { 0.04f, 0.05f, 1.5f }, BODY_WHITE);
	Box(car, { 2.07f, 0.68f, 0.f }, { 0.03f, 0.14f, 0.6f }, TRIM_BLACK);
	for (int slat = 0; slat < 4; slat++)
		Box(car, { 2.087f, 0.63f + slat * 0.033f, 0.f }, { 0.006f, 0.012f, 0.58f }, { 0.25f, 0.25f, 0.26f });
	Box(car, { -2.07f, 0.55f, 0.f }, { 0.03f, 0.6f, 1.56f }, BODY_WHITE);			// the tail panel
	Box(car, { -1.53f, 0.87f, 0.f }, { 0.08f, 0.02f, 1.42f }, BODY_WHITE);			// the deck under the rear window
	Box(car, { 0.1f, 0.3f, 0.f }, { 1.5f, 0.16f, 0.3f }, PRIMER);					// the transmission tunnel
	AddModel(ShopModels::PROJECT_CAR, car);

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
