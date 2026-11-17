// The car's parts, in the same line art as the engine's (PartIcons.cpp): the parts store's cards, the hotbar and the backpack.
#include "PartIcons.h"

#include <algorithm>
#include <cmath>

namespace PartIcons
{
	void DrawCarPart(ImDrawList* drawList, PartType type, ImVec2 min, float size, ImU32 color)
	{
		// Icons are authored in a unit square.
		auto p = [&](float x, float y) { return ImVec2(min.x + x * size, min.y + y * size); };
		const float line = std::max(1.f, size * 0.045f);
		const float pi = 3.14159265f;
		const ImU32 dim = (color & 0x00FFFFFF) | (static_cast<ImU32>(((color >> IM_COL32_A_SHIFT) & 0xFF) * 0.35f) << IM_COL32_A_SHIFT);

		// Shared shapes: a filled, outlined box; a line; a ring; a dot; a filled, outlined disc; an outlined shape.
		auto box = [&](float x0, float y0, float x1, float y1, float round = 0.03f) {
			drawList->AddRectFilled(p(x0, y0), p(x1, y1), dim, size * round);
			drawList->AddRect(p(x0, y0), p(x1, y1), color, size * round, 0, line);
		};
		auto stroke = [&](float x0, float y0, float x1, float y1, float width = 1.f) { drawList->AddLine(p(x0, y0), p(x1, y1), color, line * width); };
		auto ring = [&](float x, float y, float radius, float width = 1.f) { drawList->AddCircle(p(x, y), size * radius, color, 28, line * width); };
		auto dot = [&](float x, float y, float radius) { drawList->AddCircleFilled(p(x, y), size * radius, color, 16); };
		auto disc = [&](float x, float y, float radius, float width = 1.f) {
			drawList->AddCircleFilled(p(x, y), size * radius, dim, 28);
			drawList->AddCircle(p(x, y), size * radius, color, 28, line * width);
		};
		auto shape = [&](std::initializer_list<ImVec2> points, bool fill = true, float width = 1.f) {
			ImVec2 at[16];
			int count = 0;
			for (const auto& point : points)
				if (count < 16)
					at[count++] = p(point.x, point.y);
			if (fill)
				drawList->AddConvexPolyFilled(at, count, dim);
			drawList->AddPolyline(at, count, color, ImDrawFlags_Closed, line * width);
		};
		auto path = [&](std::initializer_list<ImVec2> points, float width = 1.f) {
			ImVec2 at[16];
			int count = 0;
			for (const auto& point : points)
				if (count < 16)
					at[count++] = p(point.x, point.y);
			drawList->AddPolyline(at, count, color, ImDrawFlags_None, line * width);
		};
		// A coil spring stood up, seen from the side.
		auto spring = [&](float x, float top, float bottom, float half, int coils) {
			ImVec2 at[24];
			int count = 0;
			for (int i = 0; i <= coils * 2 && count < 24; i++)
				at[count++] = p(x + (i % 2 == 0 ? -half : half), top + (bottom - top) * i / (coils * 2.f));
			drawList->AddPolyline(at, count, color, ImDrawFlags_None, line);
			stroke(x - half, top, x + half, top, 1.5f);
			stroke(x - half, bottom, x + half, bottom, 1.5f);
		};
		// A rod with an eye at each end.
		auto link = [&](float x0, float y0, float x1, float y1, float eye, float width) {
			stroke(x0, y0, x1, y1, width);
			disc(x0, y0, eye, 1.3f);
			disc(x1, y1, eye, 1.3f);
		};
		// A loom: a trunk with branches off it, a plug on the end of each.
		auto loom = [&](float x0, float x1, int branches) {
			stroke(x0, 0.5f, x1, 0.5f, 2.6f);
			for (float x : { x0, x1 })
				drawList->AddRectFilled(p(x - 0.04f, 0.43f), p(x + 0.04f, 0.57f), color, size * 0.01f);
			for (int i = 0; i < branches; i++)
			{
				float x = x0 + (x1 - x0) * (i + 1.f) / (branches + 1.f);
				float y = i % 2 == 0 ? 0.24f : 0.76f;
				stroke(x, 0.5f, x + 0.05f, y, 1.2f);
				drawList->AddRectFilled(p(x + 0.01f, y - 0.045f), p(x + 0.09f, y + 0.045f), color, size * 0.01f);
			}
		};
		// A hydraulic cylinder lying down with a reservoir on top of it.
		auto master = [&](float x0, float x1, float tankX0, float tankX1) {
			box(x0, 0.54f, x1, 0.70f);
			box(tankX0, 0.26f, tankX1, 0.54f);
			stroke(tankX0 + 0.03f, 0.21f, tankX1 - 0.03f, 0.21f, 1.6f);
			stroke(x1, 0.62f, x1 + 0.10f, 0.62f, 1.4f);
			drawList->AddRectFilled(p(x0 - 0.05f, 0.58f), p(x0, 0.66f), color);
		};

		switch (type)
		{
			// ---- Body ----
			case PartType::BODY_SHELL:
				shape({ { 0.05f, 0.66f }, { 0.08f, 0.52f }, { 0.30f, 0.47f }, { 0.42f, 0.30f }, { 0.70f, 0.30f }, { 0.82f, 0.47f }, { 0.95f, 0.52f }, { 0.95f, 0.66f } });
				stroke(0.44f, 0.34f, 0.44f, 0.47f);
				stroke(0.58f, 0.31f, 0.58f, 0.64f);
				disc(0.27f, 0.68f, 0.09f, 1.4f);
				disc(0.75f, 0.68f, 0.09f, 1.4f);
				break;
			case PartType::FRONT_FENDER:
				drawList->PathLineTo(p(0.08f, 0.36f));
				drawList->PathLineTo(p(0.92f, 0.28f));
				drawList->PathLineTo(p(0.92f, 0.74f));
				drawList->PathLineTo(p(0.74f, 0.74f));
				drawList->PathArcTo(p(0.50f, 0.74f), size * 0.24f, 2.f * pi, pi, 20);
				drawList->PathLineTo(p(0.08f, 0.74f));
				drawList->PathStroke(color, ImDrawFlags_Closed, line * 1.3f);
				drawList->PathArcTo(p(0.50f, 0.74f), size * 0.29f, 2.f * pi - 0.25f, pi + 0.25f, 20);
				drawList->PathStroke(dim, 0, line);
				break;
			case PartType::DOOR:
				shape({ { 0.16f, 0.90f }, { 0.16f, 0.44f }, { 0.30f, 0.11f }, { 0.84f, 0.11f }, { 0.84f, 0.90f } });
				shape({ { 0.24f, 0.44f }, { 0.34f, 0.19f }, { 0.77f, 0.19f }, { 0.77f, 0.44f } }, false);
				stroke(0.60f, 0.56f, 0.76f, 0.56f, 1.8f);
				break;
			case PartType::HOOD:
				shape({ { 0.20f, 0.24f }, { 0.80f, 0.24f }, { 0.93f, 0.78f }, { 0.07f, 0.78f } });
				stroke(0.40f, 0.24f, 0.34f, 0.78f);
				stroke(0.60f, 0.24f, 0.66f, 0.78f);
				break;
			case PartType::TRUNK_LID:
				box(0.08f, 0.30f, 0.92f, 0.72f, 0.06f);
				stroke(0.08f, 0.42f, 0.92f, 0.42f);
				dot(0.50f, 0.62f, 0.035f);
				break;
			case PartType::FRONT_BUMPER:
				box(0.05f, 0.40f, 0.95f, 0.58f, 0.07f);
				drawList->AddRectFilled(p(0.14f, 0.45f), p(0.27f, 0.53f), color);
				drawList->AddRectFilled(p(0.73f, 0.45f), p(0.86f, 0.53f), color);
				stroke(0.20f, 0.66f, 0.80f, 0.66f, 1.4f);
				break;
			case PartType::REAR_BUMPER:
				box(0.05f, 0.40f, 0.95f, 0.60f, 0.07f);
				drawList->AddRect(p(0.38f, 0.44f), p(0.62f, 0.56f), color, 0.f, 0, line);
				break;
			case PartType::HEADLIGHT:
				box(0.10f, 0.30f, 0.90f, 0.70f, 0.04f);
				drawList->AddRect(p(0.16f, 0.36f), p(0.84f, 0.64f), color, size * 0.02f, 0, line * 0.8f);
				for (int i = 1; i < 4; i++)
					stroke(0.16f + i * 0.17f, 0.36f, 0.16f + i * 0.17f, 0.64f, 0.7f);
				break;
			case PartType::TAIL_LIGHT:
				box(0.07f, 0.34f, 0.93f, 0.66f, 0.04f);
				drawList->AddRectFilled(p(0.07f, 0.34f), p(0.36f, 0.66f), color, size * 0.04f);
				stroke(0.64f, 0.34f, 0.64f, 0.66f);
				break;
			case PartType::WINDSHIELD:
				shape({ { 0.24f, 0.26f }, { 0.76f, 0.26f }, { 0.93f, 0.74f }, { 0.07f, 0.74f } });
				stroke(0.32f, 0.64f, 0.44f, 0.38f, 0.8f);
				stroke(0.42f, 0.64f, 0.50f, 0.46f, 0.8f);
				break;
			case PartType::REAR_WINDOW:
				shape({ { 0.16f, 0.30f }, { 0.84f, 0.30f }, { 0.92f, 0.70f }, { 0.08f, 0.70f } });
				for (int i = 0; i < 3; i++)
					stroke(0.17f, 0.40f + i * 0.10f, 0.83f, 0.40f + i * 0.10f, 0.6f);
				break;

			// ---- Interior ----
			case PartType::DASHBOARD:
				box(0.05f, 0.32f, 0.95f, 0.70f, 0.06f);
				drawList->AddRect(p(0.12f, 0.39f), p(0.42f, 0.60f), color, size * 0.04f, 0, line);
				drawList->AddRectFilled(p(0.52f, 0.41f), p(0.66f, 0.48f), color);
				drawList->AddRectFilled(p(0.72f, 0.41f), p(0.86f, 0.48f), color);
				stroke(0.54f, 0.58f, 0.86f, 0.58f);
				break;
			case PartType::INSTRUMENT_CLUSTER:
				box(0.05f, 0.27f, 0.95f, 0.73f, 0.10f);
				ring(0.31f, 0.50f, 0.15f);
				ring(0.69f, 0.50f, 0.15f);
				stroke(0.31f, 0.50f, 0.22f, 0.41f, 1.2f);
				stroke(0.69f, 0.50f, 0.78f, 0.44f, 1.2f);
				break;
			case PartType::SEAT:
				shape({ { 0.26f, 0.10f }, { 0.42f, 0.10f }, { 0.52f, 0.62f }, { 0.34f, 0.66f } });
				box(0.32f, 0.62f, 0.86f, 0.76f, 0.05f);
				stroke(0.30f, 0.86f, 0.88f, 0.86f, 1.4f);
				stroke(0.40f, 0.76f, 0.40f, 0.86f);
				stroke(0.78f, 0.76f, 0.78f, 0.86f);
				break;
			case PartType::STEERING_WHEEL:
				ring(0.50f, 0.50f, 0.36f, 2.4f);
				stroke(0.50f, 0.50f, 0.15f, 0.46f, 1.8f);
				stroke(0.50f, 0.50f, 0.85f, 0.46f, 1.8f);
				stroke(0.50f, 0.50f, 0.50f, 0.85f, 1.8f);
				dot(0.50f, 0.50f, 0.09f);
				break;

			// ---- Front suspension ----
			case PartType::FRONT_STRUT:
				stroke(0.32f, 0.09f, 0.68f, 0.09f, 1.8f);
				stroke(0.50f, 0.09f, 0.50f, 0.40f, 1.4f);
				stroke(0.27f, 0.40f, 0.73f, 0.40f, 1.8f);
				box(0.41f, 0.40f, 0.59f, 0.80f);
				box(0.34f, 0.76f, 0.66f, 0.92f);
				break;
			case PartType::FRONT_COIL_SPRING:
				spring(0.50f, 0.10f, 0.90f, 0.20f, 5);
				break;
			case PartType::STEERING_KNUCKLE:
				stroke(0.50f, 0.40f, 0.40f, 0.10f, 2.6f);
				stroke(0.48f, 0.62f, 0.40f, 0.90f, 2.6f);
				stroke(0.42f, 0.60f, 0.10f, 0.72f, 2.f);
				stroke(0.70f, 0.52f, 0.95f, 0.52f, 2.6f);
				disc(0.55f, 0.52f, 0.16f, 1.4f);
				dot(0.55f, 0.52f, 0.05f);
				break;
			case PartType::LOWER_CONTROL_ARM:
				shape({ { 0.16f, 0.40f }, { 0.82f, 0.45f }, { 0.82f, 0.55f }, { 0.16f, 0.60f } });
				disc(0.14f, 0.50f, 0.11f, 1.4f);
				dot(0.86f, 0.50f, 0.07f);
				break;
			case PartType::STRUT_ROD:
				stroke(0.14f, 0.80f, 0.84f, 0.24f, 2.2f);
				for (int i = 0; i < 3; i++)
					stroke(0.70f + i * 0.05f, 0.29f - i * 0.04f, 0.76f + i * 0.05f, 0.37f - i * 0.04f, 0.8f);
				disc(0.14f, 0.80f, 0.07f, 1.3f);
				break;
			case PartType::FRONT_SWAY_BAR:
				path({ { 0.12f, 0.26f }, { 0.12f, 0.62f }, { 0.20f, 0.70f }, { 0.80f, 0.70f }, { 0.88f, 0.62f }, { 0.88f, 0.26f } }, 2.2f);
				disc(0.12f, 0.24f, 0.06f, 1.3f);
				disc(0.88f, 0.24f, 0.06f, 1.3f);
				drawList->AddRectFilled(p(0.30f, 0.64f), p(0.36f, 0.76f), color);
				drawList->AddRectFilled(p(0.64f, 0.64f), p(0.70f, 0.76f), color);
				break;

			// ---- Rear suspension ----
			case PartType::REAR_AXLE:
				stroke(0.07f, 0.50f, 0.93f, 0.50f, 3.f);
				disc(0.50f, 0.50f, 0.18f, 1.4f);
				stroke(0.07f, 0.34f, 0.07f, 0.66f, 2.f);
				stroke(0.93f, 0.34f, 0.93f, 0.66f, 2.f);
				break;
			case PartType::DIFFERENTIAL:
				for (int i = 0; i < 14; i++)
				{
					float a = i * 2.f * pi / 14.f;
					stroke(0.42f + std::cos(a) * 0.25f, 0.50f + std::sin(a) * 0.25f, 0.42f + std::cos(a) * 0.31f, 0.50f + std::sin(a) * 0.31f, 1.6f);
				}
				disc(0.42f, 0.50f, 0.26f);
				ring(0.42f, 0.50f, 0.11f);
				box(0.72f, 0.42f, 0.95f, 0.58f);
				for (int i = 0; i < 3; i++)
					stroke(0.77f + i * 0.06f, 0.42f, 0.77f + i * 0.06f, 0.58f, 0.7f);
				break;
			case PartType::LOWER_TRAILING_LINK:	link(0.12f, 0.50f, 0.88f, 0.50f, 0.09f, 3.f); break;
			case PartType::UPPER_TRAILING_LINK:	link(0.24f, 0.50f, 0.76f, 0.50f, 0.08f, 2.2f); break;
			case PartType::LATERAL_ROD:			link(0.12f, 0.72f, 0.88f, 0.28f, 0.07f, 1.8f); break;
			case PartType::REAR_COIL_SPRING:
				spring(0.50f, 0.22f, 0.78f, 0.28f, 4);
				break;
			case PartType::REAR_SHOCK:
				stroke(0.50f, 0.16f, 0.50f, 0.42f, 1.4f);
				box(0.40f, 0.42f, 0.60f, 0.82f);
				stroke(0.38f, 0.42f, 0.62f, 0.42f, 1.6f);
				disc(0.50f, 0.11f, 0.06f, 1.3f);
				disc(0.50f, 0.88f, 0.06f, 1.3f);
				break;

			// ---- Steering ----
			case PartType::STEERING_RACK:
				stroke(0.03f, 0.56f, 0.97f, 0.56f, 1.4f);
				box(0.26f, 0.49f, 0.74f, 0.63f);
				for (float x : { 0.14f, 0.80f })
					path({ { x, 0.50f }, { x + 0.02f, 0.62f }, { x + 0.04f, 0.50f }, { x + 0.06f, 0.62f } }, 0.6f);
				stroke(0.38f, 0.49f, 0.30f, 0.22f, 2.2f);
				break;
			case PartType::TIE_ROD_END:
				stroke(0.08f, 0.42f, 0.56f, 0.42f, 2.4f);
				for (int i = 0; i < 4; i++)
					stroke(0.12f + i * 0.07f, 0.36f, 0.15f + i * 0.07f, 0.48f, 0.7f);
				stroke(0.70f, 0.54f, 0.70f, 0.86f, 2.f);
				disc(0.70f, 0.42f, 0.14f, 1.4f);
				break;
			case PartType::STEERING_COLUMN:
				stroke(0.14f, 0.84f, 0.80f, 0.22f, 3.4f);
				stroke(0.72f, 0.12f, 0.90f, 0.30f, 1.8f);
				stroke(0.52f, 0.48f, 0.38f, 0.34f, 1.2f);
				disc(0.14f, 0.84f, 0.07f, 1.3f);
				break;

			// ---- Brakes ----
			case PartType::BRAKE_BOOSTER:
				disc(0.42f, 0.50f, 0.33f, 1.4f);
				ring(0.42f, 0.50f, 0.20f);
				box(0.75f, 0.43f, 0.95f, 0.57f);
				break;
			case PartType::BRAKE_MASTER_CYLINDER:
				master(0.16f, 0.82f, 0.28f, 0.70f);
				stroke(0.49f, 0.26f, 0.49f, 0.54f, 0.8f);
				break;
			case PartType::BRAKE_LINE_SET:
				path({ { 0.08f, 0.24f }, { 0.58f, 0.24f }, { 0.58f, 0.44f }, { 0.92f, 0.44f } }, 1.3f);
				path({ { 0.08f, 0.52f }, { 0.38f, 0.52f }, { 0.38f, 0.76f }, { 0.92f, 0.76f } }, 1.3f);
				for (auto at : { ImVec2(0.08f, 0.24f), ImVec2(0.92f, 0.44f), ImVec2(0.08f, 0.52f), ImVec2(0.92f, 0.76f) })
					drawList->AddRectFilled(p(at.x - 0.04f, at.y - 0.04f), p(at.x + 0.04f, at.y + 0.04f), color);
				break;
			case PartType::BRAKE_HOSE:
				drawList->AddBezierCubic(p(0.14f, 0.24f), p(0.14f, 0.70f), p(0.86f, 0.30f), p(0.86f, 0.76f), color, line * 2.6f);
				drawList->AddRectFilled(p(0.08f, 0.12f), p(0.20f, 0.26f), color, size * 0.01f);
				drawList->AddRectFilled(p(0.80f, 0.74f), p(0.92f, 0.88f), color, size * 0.01f);
				break;
			case PartType::BRAKE_ROTOR:
				disc(0.50f, 0.50f, 0.39f, 1.4f);
				ring(0.50f, 0.50f, 0.17f);
				for (int i = 0; i < 4; i++)
					dot(0.50f + std::cos(i * pi * 0.5f + 0.78f) * 0.10f, 0.50f + std::sin(i * pi * 0.5f + 0.78f) * 0.10f, 0.022f);
				for (int i = 0; i < 12; i++)
				{
					float a = i * pi / 6.f;
					stroke(0.50f + std::cos(a) * 0.24f, 0.50f + std::sin(a) * 0.24f, 0.50f + std::cos(a + 0.2f) * 0.33f, 0.50f + std::sin(a + 0.2f) * 0.33f, 0.6f);
				}
				break;
			case PartType::BRAKE_PAD_SET:
				for (float y : { 0.20f, 0.56f })
				{
					box(0.14f, y + 0.05f, 0.86f, y + 0.24f, 0.05f);
					stroke(0.08f, y + 0.03f, 0.92f, y + 0.03f, 2.2f);
					stroke(0.50f, y + 0.07f, 0.50f, y + 0.22f, 0.8f);
				}
				break;
			case PartType::BRAKE_CALIPER:
				shape({ { 0.16f, 0.20f }, { 0.84f, 0.20f }, { 0.84f, 0.82f }, { 0.62f, 0.82f }, { 0.62f, 0.46f }, { 0.38f, 0.46f }, { 0.38f, 0.82f }, { 0.16f, 0.82f } }, false, 1.4f);
				ring(0.73f, 0.64f, 0.06f);
				stroke(0.24f, 0.20f, 0.20f, 0.08f, 1.4f);
				break;
			case PartType::WHEEL_CYLINDER:
				stroke(0.10f, 0.52f, 0.90f, 0.52f, 2.2f);
				box(0.26f, 0.40f, 0.74f, 0.64f);
				stroke(0.10f, 0.42f, 0.10f, 0.62f, 1.8f);
				stroke(0.90f, 0.42f, 0.90f, 0.62f, 1.8f);
				stroke(0.50f, 0.40f, 0.50f, 0.24f, 1.6f);
				break;
			case PartType::BRAKE_SHOE_SET:
				drawList->PathArcTo(p(0.50f, 0.50f), size * 0.34f, pi * 0.64f, pi * 1.36f, 20);
				drawList->PathStroke(color, 0, line * 3.f);
				drawList->PathArcTo(p(0.50f, 0.50f), size * 0.34f, -pi * 0.36f, pi * 0.36f, 20);
				drawList->PathStroke(color, 0, line * 3.f);
				drawList->PathArcTo(p(0.50f, 0.50f), size * 0.25f, pi * 0.66f, pi * 1.34f, 20);
				drawList->PathStroke(dim, 0, line * 2.f);
				drawList->PathArcTo(p(0.50f, 0.50f), size * 0.25f, -pi * 0.34f, pi * 0.34f, 20);
				drawList->PathStroke(dim, 0, line * 2.f);
				break;
			case PartType::BRAKE_DRUM:
				disc(0.50f, 0.50f, 0.39f, 1.4f);
				ring(0.50f, 0.50f, 0.31f, 0.8f);
				ring(0.50f, 0.50f, 0.09f);
				for (int i = 0; i < 4; i++)
					dot(0.50f + std::cos(i * pi * 0.5f + 0.78f) * 0.17f, 0.50f + std::sin(i * pi * 0.5f + 0.78f) * 0.17f, 0.025f);
				break;
			case PartType::PARKING_BRAKE_CABLE:
				stroke(0.10f, 0.50f, 0.44f, 0.50f, 1.4f);
				drawList->AddBezierQuadratic(p(0.44f, 0.50f), p(0.66f, 0.50f), p(0.92f, 0.24f), color, line * 1.4f);
				drawList->AddBezierQuadratic(p(0.44f, 0.50f), p(0.66f, 0.50f), p(0.92f, 0.76f), color, line * 1.4f);
				drawList->AddRectFilled(p(0.05f, 0.44f), p(0.14f, 0.56f), color);
				dot(0.92f, 0.24f, 0.035f);
				dot(0.92f, 0.76f, 0.035f);
				break;

			// ---- Drivetrain ----
			case PartType::ENGINE_MOUNT:
				stroke(0.50f, 0.10f, 0.50f, 0.26f, 1.8f);
				stroke(0.20f, 0.27f, 0.80f, 0.27f, 2.f);
				box(0.28f, 0.31f, 0.72f, 0.66f);
				for (int i = 0; i < 3; i++)
					stroke(0.30f, 0.40f + i * 0.09f, 0.70f, 0.40f + i * 0.09f, 0.6f);
				stroke(0.12f, 0.71f, 0.88f, 0.71f, 2.f);
				break;
			case PartType::CLUTCH_DISC:
				ring(0.50f, 0.50f, 0.39f, 1.4f);
				drawList->AddCircle(p(0.50f, 0.50f), size * 0.33f, dim, 28, line * 2.6f);
				for (int i = 0; i < 4; i++)
				{
					float a = i * pi * 0.5f + 0.78f;
					dot(0.50f + std::cos(a) * 0.17f, 0.50f + std::sin(a) * 0.17f, 0.045f);
				}
				ring(0.50f, 0.50f, 0.25f, 0.8f);
				ring(0.50f, 0.50f, 0.07f, 1.6f);
				break;
			case PartType::PRESSURE_PLATE:
				disc(0.50f, 0.50f, 0.39f, 1.4f);
				ring(0.50f, 0.50f, 0.28f);
				for (int i = 0; i < 12; i++)
				{
					float a = i * pi / 6.f;
					stroke(0.50f + std::cos(a) * 0.09f, 0.50f + std::sin(a) * 0.09f, 0.50f + std::cos(a) * 0.27f, 0.50f + std::sin(a) * 0.27f, 0.8f);
				}
				break;
			case PartType::RELEASE_BEARING:
				drawList->AddRectFilled(p(0.10f, 0.42f), p(0.24f, 0.58f), color);
				drawList->AddRectFilled(p(0.76f, 0.42f), p(0.90f, 0.58f), color);
				disc(0.50f, 0.50f, 0.29f, 2.f);
				ring(0.50f, 0.50f, 0.15f);
				break;
			case PartType::TRANSMISSION:
				shape({ { 0.05f, 0.22f }, { 0.30f, 0.32f }, { 0.30f, 0.70f }, { 0.05f, 0.80f } });
				box(0.30f, 0.34f, 0.70f, 0.68f, 0.f);
				shape({ { 0.70f, 0.38f }, { 0.95f, 0.45f }, { 0.95f, 0.57f }, { 0.70f, 0.64f } });
				stroke(0.62f, 0.34f, 0.66f, 0.14f, 1.4f);
				dot(0.66f, 0.12f, 0.04f);
				break;
			case PartType::TRANSMISSION_MOUNT:
				box(0.38f, 0.32f, 0.62f, 0.54f);
				stroke(0.06f, 0.60f, 0.94f, 0.60f, 3.f);
				dot(0.12f, 0.60f, 0.03f);
				dot(0.88f, 0.60f, 0.03f);
				break;
			case PartType::CLUTCH_MASTER_CYLINDER:
				master(0.24f, 0.76f, 0.38f, 0.58f);
				break;
			case PartType::CLUTCH_RELEASE_CYLINDER:
				box(0.16f, 0.40f, 0.60f, 0.62f);
				path({ { 0.60f, 0.42f }, { 0.64f, 0.60f }, { 0.68f, 0.42f }, { 0.72f, 0.60f } }, 0.9f);
				stroke(0.60f, 0.51f, 0.94f, 0.51f, 1.6f);
				stroke(0.28f, 0.40f, 0.28f, 0.24f, 1.6f);
				break;
			case PartType::DRIVESHAFT:
				box(0.20f, 0.43f, 0.80f, 0.57f, 0.f);
				for (float x : { 0.12f, 0.88f })
				{
					disc(x, 0.50f, 0.09f, 1.3f);
					stroke(x - 0.06f, 0.50f, x + 0.06f, 0.50f, 0.8f);
					stroke(x, 0.44f, x, 0.56f, 0.8f);
				}
				break;

			// ---- Fuel ----
			case PartType::FUEL_TANK:
				box(0.07f, 0.36f, 0.93f, 0.78f, 0.10f);
				stroke(0.74f, 0.36f, 0.86f, 0.16f, 2.4f);
				stroke(0.30f, 0.36f, 0.30f, 0.78f, 0.8f);
				stroke(0.62f, 0.36f, 0.62f, 0.78f, 0.8f);
				break;
			case PartType::FUEL_SENDER:
				stroke(0.26f, 0.20f, 0.70f, 0.20f, 2.2f);
				stroke(0.48f, 0.20f, 0.48f, 0.52f, 1.6f);
				stroke(0.48f, 0.52f, 0.76f, 0.78f, 1.f);
				disc(0.78f, 0.80f, 0.09f, 1.3f);
				box(0.40f, 0.44f, 0.56f, 0.62f);
				break;
			case PartType::FUEL_LINE_SET:
				path({ { 0.06f, 0.30f }, { 0.34f, 0.30f }, { 0.46f, 0.52f }, { 0.94f, 0.52f } }, 1.4f);
				path({ { 0.06f, 0.46f }, { 0.28f, 0.46f }, { 0.40f, 0.68f }, { 0.94f, 0.68f } }, 1.4f);
				for (float x : { 0.56f, 0.78f })
					drawList->AddRectFilled(p(x, 0.47f), p(x + 0.05f, 0.73f), color);
				break;
			case PartType::FUEL_FILTER:
				stroke(0.50f, 0.08f, 0.50f, 0.92f, 1.6f);
				box(0.33f, 0.26f, 0.67f, 0.74f, 0.08f);
				for (int i = 0; i < 3; i++)
					stroke(0.41f + i * 0.09f, 0.34f, 0.41f + i * 0.09f, 0.66f, 0.7f);
				break;

			// ---- Electrical ----
			case PartType::BATTERY:
				box(0.10f, 0.34f, 0.90f, 0.84f);
				drawList->AddRectFilled(p(0.20f, 0.23f), p(0.34f, 0.34f), color);
				drawList->AddRectFilled(p(0.66f, 0.23f), p(0.80f, 0.34f), color);
				stroke(0.20f, 0.58f, 0.34f, 0.58f, 1.2f);
				stroke(0.27f, 0.51f, 0.27f, 0.65f, 1.2f);
				stroke(0.66f, 0.58f, 0.80f, 0.58f, 1.2f);
				break;
			case PartType::MAIN_HARNESS:		loom(0.08f, 0.92f, 5); break;

			case PartType::FUSE_BOX:
				box(0.10f, 0.24f, 0.90f, 0.76f, 0.05f);
				for (int row = 0; row < 2; row++)
					for (int column = 0; column < 4; column++)
						drawList->AddRectFilled(p(0.19f + column * 0.17f, 0.33f + row * 0.21f), p(0.30f + column * 0.17f, 0.46f + row * 0.21f), color);
				break;
			case PartType::IGNITION_COIL:
				box(0.32f, 0.36f, 0.68f, 0.90f, 0.06f);
				box(0.43f, 0.14f, 0.57f, 0.36f);
				dot(0.37f, 0.30f, 0.035f);
				dot(0.63f, 0.30f, 0.035f);
				stroke(0.32f, 0.52f, 0.68f, 0.52f, 0.8f);
				break;
			case PartType::STARTER:
				box(0.30f, 0.46f, 0.88f, 0.78f, 0.06f);
				box(0.38f, 0.22f, 0.78f, 0.42f, 0.06f);
				shape({ { 0.30f, 0.50f }, { 0.08f, 0.55f }, { 0.08f, 0.69f }, { 0.30f, 0.74f } });
				for (int i = 0; i < 3; i++)
					stroke(0.12f + i * 0.05f, 0.56f, 0.12f + i * 0.05f, 0.68f, 0.7f);
				break;

			// ---- Cooling ----
			case PartType::RADIATOR:
				box(0.14f, 0.24f, 0.86f, 0.84f);
				drawList->AddRectFilled(p(0.14f, 0.24f), p(0.86f, 0.33f), color);
				drawList->AddRectFilled(p(0.26f, 0.15f), p(0.38f, 0.24f), color);
				for (int i = 1; i < 7; i++)
					stroke(0.14f + i * 0.103f, 0.36f, 0.14f + i * 0.103f, 0.82f, 0.6f);
				stroke(0.86f, 0.74f, 0.96f, 0.74f, 2.f);
				break;
			case PartType::RADIATOR_HOSE:
				drawList->AddBezierCubic(p(0.12f, 0.22f), p(0.62f, 0.22f), p(0.38f, 0.78f), p(0.88f, 0.78f), color, line * 4.2f);
				drawList->AddBezierCubic(p(0.12f, 0.22f), p(0.62f, 0.22f), p(0.38f, 0.78f), p(0.88f, 0.78f), IM_COL32(0, 0, 0, 110), line * 1.6f);
				drawList->AddRectFilled(p(0.15f, 0.13f), p(0.20f, 0.31f), color);
				drawList->AddRectFilled(p(0.80f, 0.69f), p(0.85f, 0.87f), color);
				break;
			case PartType::COOLING_FAN:
				ring(0.50f, 0.50f, 0.41f, 1.2f);
				for (int i = 0; i < 5; i++)
				{
					float a = i * 2.f * pi / 5.f;
					ImVec2 blade[] = { p(0.50f + std::cos(a) * 0.08f, 0.50f + std::sin(a) * 0.08f),
						p(0.50f + std::cos(a - 0.30f) * 0.34f, 0.50f + std::sin(a - 0.30f) * 0.34f),
						p(0.50f + std::cos(a + 0.42f) * 0.34f, 0.50f + std::sin(a + 0.42f) * 0.34f) };
					drawList->AddConvexPolyFilled(blade, 3, dim);
					drawList->AddPolyline(blade, 3, color, ImDrawFlags_Closed, line * 0.9f);
				}
				dot(0.50f, 0.50f, 0.08f);
				break;

			// ---- Exhaust ----
			case PartType::CATALYTIC_CONVERTER:
				stroke(0.03f, 0.50f, 0.97f, 0.50f, 2.6f);
				shape({ { 0.18f, 0.46f }, { 0.30f, 0.32f }, { 0.70f, 0.32f }, { 0.82f, 0.46f }, { 0.82f, 0.54f }, { 0.70f, 0.68f }, { 0.30f, 0.68f }, { 0.18f, 0.54f } });
				for (int i = 0; i < 4; i++)
					stroke(0.36f + i * 0.09f, 0.38f, 0.36f + i * 0.09f, 0.62f, 0.6f);
				break;
			case PartType::MUFFLER:
				stroke(0.02f, 0.50f, 0.14f, 0.50f, 2.6f);
				stroke(0.80f, 0.57f, 0.97f, 0.57f, 2.6f);
				box(0.14f, 0.33f, 0.80f, 0.67f, 0.14f);
				stroke(0.25f, 0.34f, 0.25f, 0.66f, 0.8f);
				stroke(0.69f, 0.34f, 0.69f, 0.66f, 0.8f);
				break;

			// ---- Wheels ----
			case PartType::WHEEL:
				ring(0.50f, 0.50f, 0.40f, 2.2f);
				ring(0.50f, 0.50f, 0.32f, 0.8f);
				for (int i = 0; i < 5; i++)
				{
					float a = i * 2.f * pi / 5.f - pi * 0.5f;
					stroke(0.50f + std::cos(a) * 0.09f, 0.50f + std::sin(a) * 0.09f, 0.50f + std::cos(a) * 0.32f, 0.50f + std::sin(a) * 0.32f, 2.f);
				}
				disc(0.50f, 0.50f, 0.10f, 1.3f);
				break;
			case PartType::TIRE:
				drawList->AddCircle(p(0.50f, 0.50f), size * 0.335f, dim, 32, size * 0.16f);
				ring(0.50f, 0.50f, 0.42f, 1.4f);
				ring(0.50f, 0.50f, 0.25f, 1.2f);
				for (int i = 0; i < 16; i++)
				{
					float a = i * pi / 8.f;
					stroke(0.50f + std::cos(a) * 0.36f, 0.50f + std::sin(a) * 0.36f, 0.50f + std::cos(a + 0.12f) * 0.42f, 0.50f + std::sin(a + 0.12f) * 0.42f, 0.8f);
				}
				break;

			default:
				drawList->AddRect(p(0.2f, 0.2f), p(0.8f, 0.8f), color, 0.f, 0, line);
				break;
		}
	}

	void DrawFluid(ImDrawList* drawList, bool can, ImU32 tint, ImVec2 min, float size, ImU32 color)
	{
		auto p = [&](float x, float y) { return ImVec2(min.x + x * size, min.y + y * size); };
		const float line = std::max(1.f, size * 0.045f);
		if (can)
		{
			// A jerry can: its handle, its spout at the corner.
			drawList->AddRectFilled(p(0.20f, 0.30f), p(0.80f, 0.90f), tint, size * 0.05f);
			drawList->AddRect(p(0.20f, 0.30f), p(0.80f, 0.90f), color, size * 0.05f, 0, line);
			drawList->AddRect(p(0.34f, 0.14f), p(0.60f, 0.30f), color, size * 0.03f, 0, line * 1.4f);
			drawList->AddLine(p(0.70f, 0.30f), p(0.82f, 0.14f), color, line * 2.4f);
			drawList->AddLine(p(0.26f, 0.38f), p(0.74f, 0.82f), color, line * 0.8f);
			drawList->AddLine(p(0.74f, 0.38f), p(0.26f, 0.82f), color, line * 0.8f);
			return;
		}
		// A bottle: shouldered, a cap on its neck, a label across it.
		ImVec2 body[] = { p(0.30f, 0.90f), p(0.30f, 0.40f), p(0.42f, 0.26f), p(0.58f, 0.26f), p(0.70f, 0.40f), p(0.70f, 0.90f) };
		drawList->AddConvexPolyFilled(body, 6, tint);
		drawList->AddPolyline(body, 6, color, ImDrawFlags_Closed, line);
		drawList->AddRectFilled(p(0.42f, 0.12f), p(0.58f, 0.24f), color, size * 0.01f);
		drawList->AddRectFilled(p(0.30f, 0.52f), p(0.70f, 0.74f), IM_COL32(235, 235, 228, 235));
		drawList->AddLine(p(0.36f, 0.60f), p(0.64f, 0.60f), IM_COL32(60, 60, 60, 220), line * 0.8f);
		drawList->AddLine(p(0.36f, 0.67f), p(0.56f, 0.67f), IM_COL32(60, 60, 60, 220), line * 0.8f);
	}
}
