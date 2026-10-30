#include "PartIcons.h"

#include <algorithm>
#include <cmath>

#include "../Simulation/PartCatalog.h"

namespace PartIcons
{
	void Draw(ImDrawList* drawList, PartType type, ImVec2 min, float size, ImU32 color)
	{
		// Icons are authored in a unit square.
		auto p = [&](float x, float y) { return ImVec2(min.x + x * size, min.y + y * size); };
		float line = std::max(1.f, size * 0.045f);
		float pi = 3.14159265f;
		ImU32 dim = (color & 0x00FFFFFF) | (static_cast<ImU32>(((color >> IM_COL32_A_SHIFT) & 0xFF) * 0.35f) << IM_COL32_A_SHIFT);

		switch (type)
		{
			case PartType::BLOCK:
				drawList->AddRectFilled(p(0.10f, 0.30f), p(0.90f, 0.80f), dim, size * 0.03f);
				drawList->AddRect(p(0.10f, 0.30f), p(0.90f, 0.80f), color, size * 0.03f, 0, line);
				drawList->AddRect(p(0.16f, 0.80f), p(0.84f, 0.90f), color, 0.f, 0, line);
				for (int i = 0; i < 4; i++)
					drawList->AddCircle(p(0.22f + i * 0.187f, 0.45f), size * 0.07f, color, 16, line);
				break;
			case PartType::CYLINDER_HEAD:
				drawList->AddRectFilled(p(0.10f, 0.45f), p(0.90f, 0.75f), dim);
				drawList->AddRect(p(0.10f, 0.45f), p(0.90f, 0.75f), color, 0.f, 0, line);
				drawList->AddLine(p(0.10f, 0.33f), p(0.90f, 0.33f), color, line * 1.4f);
				for (int i = 0; i < 5; i++)
				{
					drawList->AddLine(p(0.14f + i * 0.18f, 0.33f), p(0.14f + i * 0.18f, 0.45f), color, line);
					drawList->AddCircleFilled(p(0.14f + i * 0.18f, 0.33f), size * 0.04f, color);
				}
				for (int i = 0; i < 4; i++)
					drawList->AddRectFilled(p(0.18f + i * 0.19f, 0.75f), p(0.26f + i * 0.19f, 0.82f), color);
				break;
			case PartType::HEAD_GASKET:
				drawList->AddRect(p(0.06f, 0.36f), p(0.94f, 0.64f), color, size * 0.05f, 0, line);
				for (int i = 0; i < 4; i++)
					drawList->AddCircle(p(0.20f + i * 0.20f, 0.50f), size * 0.085f, color, 18, line);
				break;
			case PartType::CRANKSHAFT:
				drawList->AddLine(p(0.05f, 0.50f), p(0.95f, 0.50f), color, line * 1.6f);
				for (int i = 0; i < 4; i++)
				{
					float x = 0.20f + i * 0.20f;
					float sign = (i == 0 || i == 3) ? -1.f : 1.f;
					drawList->AddRectFilled(p(x - 0.07f, 0.50f), p(x - 0.04f, 0.50f + sign * 0.24f), color);
					drawList->AddRectFilled(p(x + 0.04f, 0.50f), p(x + 0.07f, 0.50f + sign * 0.24f), color);
					drawList->AddLine(p(x - 0.05f, 0.50f + sign * 0.22f), p(x + 0.05f, 0.50f + sign * 0.22f), color, line * 1.6f);
					drawList->AddCircleFilled(p(x, 0.50f - sign * 0.10f), size * 0.07f, dim);
				}
				break;
			case PartType::CONNECTING_ROD:
				drawList->AddCircle(p(0.50f, 0.20f), size * 0.09f, color, 16, line * 1.4f);
				drawList->AddCircle(p(0.50f, 0.70f), size * 0.18f, color, 24, line * 1.4f);
				drawList->AddLine(p(0.46f, 0.28f), p(0.42f, 0.53f), color, line);
				drawList->AddLine(p(0.54f, 0.28f), p(0.58f, 0.53f), color, line);
				drawList->AddLine(p(0.30f, 0.80f), p(0.70f, 0.80f), color, line);
				break;
			case PartType::PISTON:
				drawList->AddRectFilled(p(0.24f, 0.18f), p(0.76f, 0.82f), dim, size * 0.06f);
				drawList->AddRect(p(0.24f, 0.18f), p(0.76f, 0.82f), color, size * 0.06f, 0, line);
				for (int i = 0; i < 3; i++)
					drawList->AddLine(p(0.24f, 0.28f + i * 0.06f), p(0.76f, 0.28f + i * 0.06f), color, line);
				drawList->AddCircle(p(0.50f, 0.62f), size * 0.075f, color, 16, line);
				break;
			case PartType::PISTON_RING:
				drawList->PathArcTo(p(0.50f, 0.50f), size * 0.32f, -pi * 0.5f + 0.25f, pi * 1.5f - 0.25f, 32);
				drawList->PathStroke(color, 0, line * 2.f);
				break;
			case PartType::MAIN_BEARING:
			case PartType::ROD_BEARING:
			case PartType::MAIN_BEARING_LOWER:
			case PartType::ROD_BEARING_LOWER:
			{
				// One half-shell: arched for an upper, cupped for a lower, with the other half faint.
				float radius = type == PartType::MAIN_BEARING || type == PartType::MAIN_BEARING_LOWER ? 0.32f : 0.24f;
				bool upper = type == PartType::MAIN_BEARING || type == PartType::ROD_BEARING;
				drawList->PathArcTo(p(0.50f, 0.50f), size * radius, upper ? pi + 0.05f : 0.05f, upper ? 2.f * pi - 0.05f : pi - 0.05f, 24);
				drawList->PathStroke(color, 0, line * 2.8f);
				drawList->PathArcTo(p(0.50f, 0.50f), size * radius, upper ? 0.05f : pi + 0.05f, upper ? pi - 0.05f : 2.f * pi - 0.05f, 24);
				drawList->PathStroke(dim, 0, line);
				break;
			}
			case PartType::ROD_CAP:
				drawList->PathArcTo(p(0.50f, 0.30f), size * 0.24f, 0.f, pi, 24);
				drawList->PathStroke(color, 0, line * 3.f);
				drawList->AddRectFilled(p(0.18f, 0.24f), p(0.30f, 0.34f), color);
				drawList->AddRectFilled(p(0.70f, 0.24f), p(0.82f, 0.34f), color);
				drawList->AddRectFilled(p(0.40f, 0.56f), p(0.60f, 0.64f), color);
				break;
			case PartType::MAIN_CAP:
			{
				ImVec2 cap[] = { p(0.10f, 0.30f), p(0.90f, 0.30f), p(0.80f, 0.74f), p(0.20f, 0.74f) };
				drawList->AddConvexPolyFilled(cap, 4, dim);
				drawList->AddPolyline(cap, 4, color, ImDrawFlags_Closed, line);
				drawList->PathArcTo(p(0.50f, 0.30f), size * 0.20f, 0.f, pi, 20);
				drawList->PathStroke(color, 0, line * 2.f);
				drawList->AddLine(p(0.20f, 0.20f), p(0.20f, 0.80f), color, line * 1.5f);
				drawList->AddLine(p(0.80f, 0.20f), p(0.80f, 0.80f), color, line * 1.5f);
				break;
			}
			case PartType::CAM_CAP:
			{
				// A low bridge, its arch over the journal, a bolt either side.
				ImVec2 cap[] = { p(0.12f, 0.36f), p(0.88f, 0.36f), p(0.88f, 0.66f), p(0.12f, 0.66f) };
				drawList->AddConvexPolyFilled(cap, 4, dim);
				drawList->AddPolyline(cap, 4, color, ImDrawFlags_Closed, line);
				drawList->PathArcTo(p(0.50f, 0.66f), size * 0.16f, pi, 2.f * pi, 20);
				drawList->PathStroke(color, 0, line * 2.f);
				drawList->AddLine(p(0.22f, 0.26f), p(0.22f, 0.72f), color, line * 1.5f);
				drawList->AddLine(p(0.78f, 0.26f), p(0.78f, 0.72f), color, line * 1.5f);
				break;
			}
			case PartType::CAMSHAFT:
				drawList->AddLine(p(0.04f, 0.50f), p(0.96f, 0.50f), color, line * 1.4f);
				for (int i = 0; i < 8; i++)
				{
					float x = 0.12f + i * 0.108f;
					float sign = i % 2 == 0 ? -1.f : 1.f;
					drawList->AddCircleFilled(p(x, 0.50f), size * 0.055f, color);
					drawList->AddTriangleFilled(p(x - 0.045f, 0.50f), p(x + 0.045f, 0.50f), p(x, 0.50f + sign * 0.15f), color);
				}
				drawList->AddCircle(p(0.04f, 0.50f), size * 0.10f, color, 20, line);
				break;
			case PartType::INTAKE_VALVE:
			case PartType::EXHAUST_VALVE:
			{
				drawList->AddLine(p(0.50f, 0.10f), p(0.50f, 0.72f), color, line * 1.4f);
				ImVec2 face[] = { p(0.28f, 0.84f), p(0.72f, 0.84f), p(0.57f, 0.72f), p(0.43f, 0.72f) };
				ImU32 fill = type == PartType::EXHAUST_VALVE ? IM_COL32(190, 120, 80, 255) : dim;
				drawList->AddConvexPolyFilled(face, 4, fill);
				drawList->AddPolyline(face, 4, color, ImDrawFlags_Closed, line);
				drawList->AddLine(p(0.45f, 0.16f), p(0.55f, 0.16f), color, line);
				break;
			}
			case PartType::VALVE_SPRING:
			{
				ImVec2 points[12];
				for (int i = 0; i < 12; i++)
					points[i] = p(i % 2 == 0 ? 0.30f : 0.70f, 0.14f + i * 0.065f);
				drawList->AddPolyline(points, 12, color, ImDrawFlags_None, line * 1.3f);
				drawList->AddLine(p(0.26f, 0.12f), p(0.74f, 0.12f), color, line * 1.5f);
				drawList->AddLine(p(0.26f, 0.88f), p(0.74f, 0.88f), color, line * 1.5f);
				break;
			}
			default:
				DrawAncillary(drawList, type, min, size, color);
				break;
		}
	}

	void DrawAncillary(ImDrawList* drawList, PartType type, ImVec2 min, float size, ImU32 color)
	{
		auto p = [&](float x, float y) { return ImVec2(min.x + x * size, min.y + y * size); };
		float line = std::max(1.f, size * 0.045f);
		float pi = 3.14159265f;
		ImU32 dim = (color & 0x00FFFFFF) | (static_cast<ImU32>(((color >> IM_COL32_A_SHIFT) & 0xFF) * 0.35f) << IM_COL32_A_SHIFT);

		// Shared shapes.
		auto gear = [&](float cx, float cy, float radius, int teeth) {
			for (int i = 0; i < teeth; i++)
			{
				float a = i * 2.f * pi / teeth;
				drawList->AddLine(p(cx + std::cos(a) * radius * 0.82f, cy + std::sin(a) * radius * 0.82f),
					p(cx + std::cos(a) * radius, cy + std::sin(a) * radius), color, line * 1.6f);
			}
			drawList->AddCircleFilled(p(cx, cy), size * radius * 0.84f, dim, 28);
			drawList->AddCircle(p(cx, cy), size * radius * 0.84f, color, 28, line);
			drawList->AddCircle(p(cx, cy), size * radius * 0.25f, color, 16, line);
		};
		auto gasket = [&](ImVec2 a, ImVec2 b, int holes) {
			drawList->AddRect(p(a.x, a.y), p(b.x, b.y), color, size * 0.04f, 0, line * 1.3f);
			drawList->AddRect(p(a.x + 0.06f, a.y + 0.06f), p(b.x - 0.06f, b.y - 0.06f), dim, size * 0.03f, 0, line);
			for (int i = 0; i < holes; i++)
			{
				float t = holes > 1 ? static_cast<float>(i) / (holes - 1) : 0.5f;
				drawList->AddCircleFilled(p(a.x + 0.03f + t * (b.x - a.x - 0.06f), a.y + 0.03f), size * 0.018f, color);
				drawList->AddCircleFilled(p(a.x + 0.03f + t * (b.x - a.x - 0.06f), b.y - 0.03f), size * 0.018f, color);
			}
		};
		auto seal = [&](float radius) {
			drawList->AddCircle(p(0.50f, 0.50f), size * radius, color, 32, line * 2.2f);
			drawList->AddCircle(p(0.50f, 0.50f), size * radius * 0.7f, color, 32, line);
			drawList->AddCircleFilled(p(0.50f, 0.50f), size * radius * 0.85f, dim, 32);
		};
		auto bolt = [&](float x, float top, float bottom) {
			drawList->AddRectFilled(p(x - 0.07f, top), p(x + 0.07f, top + 0.10f), color, size * 0.01f);
			drawList->AddLine(p(x, top + 0.10f), p(x, bottom), color, line * 2.f);
			for (float y = top + 0.16f; y < bottom; y += 0.06f)
				drawList->AddLine(p(x - 0.035f, y), p(x + 0.035f, y + 0.02f), color, line * 0.8f);
		};

		switch (type)
		{
			case PartType::CORE_PLUG:
				drawList->AddCircleFilled(p(0.50f, 0.50f), size * 0.32f, dim, 32);
				drawList->AddCircle(p(0.50f, 0.50f), size * 0.32f, color, 32, line * 1.6f);
				drawList->AddCircle(p(0.50f, 0.50f), size * 0.22f, color, 32, line);
				break;
			case PartType::REAR_MAIN_SEAL:
				seal(0.36f);
				break;
			case PartType::FRONT_CRANK_SEAL:
				seal(0.26f);
				break;
			case PartType::VALVE_STEM_SEAL:
				drawList->AddRectFilled(p(0.36f, 0.35f), p(0.64f, 0.72f), dim, size * 0.05f);
				drawList->AddRect(p(0.36f, 0.35f), p(0.64f, 0.72f), color, size * 0.05f, 0, line);
				drawList->AddLine(p(0.30f, 0.72f), p(0.70f, 0.72f), color, line * 1.5f);
				drawList->AddLine(p(0.50f, 0.15f), p(0.50f, 0.85f), color, line * 0.8f);
				break;
			case PartType::OIL_PUMP:
				drawList->AddRectFilled(p(0.12f, 0.18f), p(0.88f, 0.82f), dim, size * 0.08f);
				drawList->AddRect(p(0.12f, 0.18f), p(0.88f, 0.82f), color, size * 0.08f, 0, line);
				drawList->AddCircle(p(0.50f, 0.50f), size * 0.24f, color, 28, line);
				for (int i = 0; i < 5; i++)
				{
					float a = i * 2.f * pi / 5.f;
					drawList->AddCircleFilled(p(0.50f + std::cos(a) * 0.11f, 0.50f + std::sin(a) * 0.11f), size * 0.05f, color);
				}
				break;
			case PartType::OIL_PICKUP:
				drawList->AddLine(p(0.25f, 0.10f), p(0.25f, 0.60f), color, line * 2.f);
				drawList->AddLine(p(0.25f, 0.60f), p(0.55f, 0.75f), color, line * 2.f);
				drawList->AddEllipseFilled(p(0.65f, 0.80f), { size * 0.24f, size * 0.08f }, dim);
				drawList->AddEllipse(p(0.65f, 0.80f), { size * 0.24f, size * 0.08f }, color, 0.f, 24, line);
				drawList->AddRectFilled(p(0.15f, 0.08f), p(0.35f, 0.14f), color);
				break;
			case PartType::OIL_PAN_GASKET:			gasket({ 0.06f, 0.24f }, { 0.94f, 0.76f }, 5); break;
			case PartType::TIMING_COVER_GASKET:		gasket({ 0.22f, 0.06f }, { 0.78f, 0.94f }, 3); break;
			case PartType::VALVE_COVER_GASKET:		gasket({ 0.04f, 0.32f }, { 0.96f, 0.68f }, 5); break;
			case PartType::WATER_PUMP_GASKET:		gasket({ 0.22f, 0.22f }, { 0.78f, 0.78f }, 2); break;
			case PartType::INTAKE_MANIFOLD_GASKET:
			case PartType::EXHAUST_MANIFOLD_GASKET:
				drawList->AddRect(p(0.04f, 0.32f), p(0.96f, 0.68f), color, size * 0.03f, 0, line * 1.3f);
				for (int i = 0; i < 4; i++)
				{
					ImVec2 c = p(0.17f + i * 0.22f, 0.50f);
					if (type == PartType::INTAKE_MANIFOLD_GASKET)
						drawList->AddCircle(c, size * 0.075f, color, 18, line);
					else
						drawList->AddEllipse(c, { size * 0.06f, size * 0.10f }, color, 0.f, 18, line);
				}
				break;
			case PartType::OIL_PAN:
			{
				ImVec2 pan[] = { p(0.06f, 0.30f), p(0.94f, 0.30f), p(0.84f, 0.80f), p(0.16f, 0.80f) };
				drawList->AddConvexPolyFilled(pan, 4, dim);
				drawList->AddPolyline(pan, 4, color, ImDrawFlags_Closed, line);
				drawList->AddLine(p(0.02f, 0.30f), p(0.98f, 0.30f), color, line * 1.6f);
				drawList->AddRectFilled(p(0.62f, 0.80f), p(0.70f, 0.86f), color);
				break;
			}
			case PartType::OIL_DRAIN_PLUG:
				drawList->AddRectFilled(p(0.30f, 0.20f), p(0.70f, 0.38f), color, size * 0.02f);
				drawList->AddRectFilled(p(0.26f, 0.38f), p(0.74f, 0.42f), dim);
				bolt(0.50f, 0.32f, 0.82f);
				break;
			case PartType::OIL_FILTER:
				drawList->AddRectFilled(p(0.24f, 0.16f), p(0.76f, 0.84f), dim, size * 0.06f);
				drawList->AddRect(p(0.24f, 0.16f), p(0.76f, 0.84f), color, size * 0.06f, 0, line);
				for (int i = 0; i < 6; i++)
					drawList->AddLine(p(0.30f + i * 0.08f, 0.24f), p(0.30f + i * 0.08f, 0.34f), color, line);
				drawList->AddLine(p(0.24f, 0.74f), p(0.76f, 0.74f), color, line);
				break;
			case PartType::BOLT_M6:			bolt(0.50f, 0.30f, 0.62f); break;
			case PartType::BOLT_M8:			bolt(0.50f, 0.26f, 0.70f); break;
			case PartType::BOLT_M10:		bolt(0.50f, 0.20f, 0.78f); break;
			case PartType::BOLT_M12:		bolt(0.50f, 0.18f, 0.74f); break;
			case PartType::BOLT_M14:		bolt(0.50f, 0.14f, 0.80f); break;
			case PartType::NUT_M8:
			{
				// A hex nut seen end on.
				float across = 0.30f;
				ImVec2 corners[6];
				for (int i = 0; i < 6; i++)
					corners[i] = p(0.50f + std::cos(i * pi / 3.f) * across, 0.50f + std::sin(i * pi / 3.f) * across);
				drawList->AddConvexPolyFilled(corners, 6, dim);
				drawList->AddPolyline(corners, 6, color, ImDrawFlags_Closed, line * 1.4f);
				drawList->AddCircle(p(0.50f, 0.50f), size * across * 0.5f, color, 24, line);
				break;
			}
			case PartType::VALVE_RETAINER:
				drawList->AddEllipseFilled(p(0.50f, 0.42f), { size * 0.30f, size * 0.10f }, dim);
				drawList->AddEllipse(p(0.50f, 0.42f), { size * 0.30f, size * 0.10f }, color, 0.f, 24, line);
				drawList->AddTriangleFilled(p(0.40f, 0.48f), p(0.60f, 0.48f), p(0.50f, 0.78f), color);
				break;
			case PartType::LIFTER:
				drawList->AddRectFilled(p(0.24f, 0.24f), p(0.76f, 0.76f), dim, size * 0.04f);
				drawList->AddRect(p(0.24f, 0.24f), p(0.76f, 0.76f), color, size * 0.04f, 0, line);
				drawList->AddLine(p(0.24f, 0.34f), p(0.76f, 0.34f), color, line * 1.5f);
				break;
			case PartType::CRANK_SPROCKET:
				gear(0.50f, 0.50f, 0.26f, 14);
				break;
			case PartType::CAM_SPROCKET:
				gear(0.50f, 0.50f, 0.40f, 24);
				break;
			case PartType::TIMING_BELT:
				// A loop of belt, its teeth on the inside.
				drawList->AddRect(p(0.28f, 0.08f), p(0.72f, 0.92f), color, size * 0.2f, 0, line * 3.f);
				for (int i = 0; i < 7; i++)
				{
					drawList->AddLine(p(0.30f, 0.20f + i * 0.10f), p(0.35f, 0.20f + i * 0.10f), dim, line * 1.5f);
					drawList->AddLine(p(0.65f, 0.20f + i * 0.10f), p(0.70f, 0.20f + i * 0.10f), dim, line * 1.5f);
				}
				break;
			case PartType::BELT_IDLER:
				// A smooth pulley round its bearing.
				drawList->AddCircleFilled(p(0.50f, 0.50f), size * 0.34f, dim, 32);
				drawList->AddCircle(p(0.50f, 0.50f), size * 0.34f, color, 32, line * 2.f);
				drawList->AddCircle(p(0.50f, 0.50f), size * 0.12f, color, 20, line * 1.5f);
				break;
			case PartType::BELT_TENSIONER:
				// The sprung arm from its pivot to the pulley.
				drawList->AddLine(p(0.26f, 0.70f), p(0.62f, 0.38f), color, line * 3.f);
				drawList->AddCircleFilled(p(0.26f, 0.70f), size * 0.08f, color, 16);
				drawList->AddCircleFilled(p(0.64f, 0.36f), size * 0.22f, dim, 28);
				drawList->AddCircle(p(0.64f, 0.36f), size * 0.22f, color, 28, line * 2.f);
				break;
			case PartType::TIMING_COVER:
				drawList->AddRectFilled(p(0.22f, 0.04f), p(0.78f, 0.96f), dim, size * 0.12f);
				drawList->AddRect(p(0.22f, 0.04f), p(0.78f, 0.96f), color, size * 0.12f, 0, line);
				drawList->AddCircle(p(0.50f, 0.76f), size * 0.10f, color, 20, line);
				drawList->AddCircle(p(0.50f, 0.22f), size * 0.12f, color, 20, line * 0.7f);
				break;
			case PartType::CRANK_PULLEY:
				drawList->AddCircleFilled(p(0.50f, 0.50f), size * 0.40f, dim, 32);
				for (float radius : { 0.40f, 0.33f, 0.26f })
					drawList->AddCircle(p(0.50f, 0.50f), size * radius, color, 32, line);
				drawList->AddCircleFilled(p(0.50f, 0.50f), size * 0.08f, color, 16);
				break;
			case PartType::VALVE_COVER:
				drawList->AddRectFilled(p(0.04f, 0.30f), p(0.96f, 0.70f), dim, size * 0.10f);
				drawList->AddRect(p(0.04f, 0.30f), p(0.96f, 0.70f), color, size * 0.10f, 0, line);
				for (int i = 0; i < 4; i++)
					drawList->AddCircle(p(0.20f + i * 0.20f, 0.50f), size * 0.05f, color, 14, line);
				break;
			case PartType::SPARK_PLUG:
				drawList->AddRectFilled(p(0.42f, 0.06f), p(0.58f, 0.40f), IM_COL32(235, 235, 230, 255), size * 0.04f);
				drawList->AddRectFilled(p(0.34f, 0.40f), p(0.66f, 0.54f), color);
				bolt(0.50f, 0.54f, 0.84f);
				drawList->AddLine(p(0.50f, 0.84f), p(0.60f, 0.92f), color, line * 1.5f);
				break;
			case PartType::IGNITION_COIL:
				drawList->AddRectFilled(p(0.28f, 0.08f), p(0.72f, 0.28f), color, size * 0.04f);
				drawList->AddRectFilled(p(0.40f, 0.28f), p(0.60f, 0.88f), dim, size * 0.04f);
				drawList->AddRect(p(0.40f, 0.28f), p(0.60f, 0.88f), color, size * 0.04f, 0, line);
				break;
			case PartType::INTAKE_MANIFOLD:
				drawList->AddRectFilled(p(0.06f, 0.14f), p(0.94f, 0.42f), dim, size * 0.10f);
				drawList->AddRect(p(0.06f, 0.14f), p(0.94f, 0.42f), color, size * 0.10f, 0, line);
				for (int i = 0; i < 4; i++)
				{
					float x = 0.17f + i * 0.22f;
					drawList->AddLine(p(x, 0.42f), p(x, 0.84f), color, line * 3.f);
				}
				drawList->AddLine(p(0.06f, 0.86f), p(0.94f, 0.86f), color, line * 1.5f);
				break;
			case PartType::THROTTLE_BODY:
				drawList->AddCircleFilled(p(0.50f, 0.50f), size * 0.34f, dim, 32);
				drawList->AddCircle(p(0.50f, 0.50f), size * 0.34f, color, 32, line * 1.5f);
				drawList->AddLine(p(0.30f, 0.66f), p(0.70f, 0.34f), color, line * 1.6f);
				break;
			case PartType::FUEL_INJECTOR:
				drawList->AddRectFilled(p(0.36f, 0.08f), p(0.64f, 0.24f), color, size * 0.03f);
				drawList->AddRectFilled(p(0.40f, 0.24f), p(0.60f, 0.74f), dim, size * 0.03f);
				drawList->AddRect(p(0.40f, 0.24f), p(0.60f, 0.74f), color, size * 0.03f, 0, line);
				drawList->AddTriangleFilled(p(0.44f, 0.74f), p(0.56f, 0.74f), p(0.50f, 0.90f), color);
				break;
			case PartType::FUEL_RAIL:
				drawList->AddRectFilled(p(0.04f, 0.38f), p(0.96f, 0.52f), dim, size * 0.06f);
				drawList->AddRect(p(0.04f, 0.38f), p(0.96f, 0.52f), color, size * 0.06f, 0, line);
				for (int i = 0; i < 4; i++)
					drawList->AddRectFilled(p(0.13f + i * 0.22f, 0.52f), p(0.21f + i * 0.22f, 0.64f), color);
				break;
			case PartType::EXHAUST_MANIFOLD:
				for (int i = 0; i < 4; i++)
				{
					float x = 0.14f + i * 0.24f;
					drawList->AddBezierQuadratic(p(x, 0.12f), p(x, 0.60f), p(0.50f, 0.72f), IM_COL32(190, 120, 80, 255), line * 3.f);
				}
				drawList->AddLine(p(0.50f, 0.70f), p(0.50f, 0.94f), IM_COL32(190, 120, 80, 255), line * 4.f);
				drawList->AddLine(p(0.04f, 0.12f), p(0.96f, 0.12f), color, line * 1.6f);
				break;
			case PartType::WATER_PUMP:
				drawList->AddCircleFilled(p(0.50f, 0.50f), size * 0.36f, dim, 32);
				drawList->AddCircle(p(0.50f, 0.50f), size * 0.36f, color, 32, line);
				for (int i = 0; i < 6; i++)
				{
					float a = i * 2.f * pi / 6.f;
					drawList->AddBezierQuadratic(p(0.50f + std::cos(a) * 0.06f, 0.50f + std::sin(a) * 0.06f),
						p(0.50f + std::cos(a + 0.5f) * 0.20f, 0.50f + std::sin(a + 0.5f) * 0.20f),
						p(0.50f + std::cos(a + 0.3f) * 0.32f, 0.50f + std::sin(a + 0.3f) * 0.32f), color, line);
				}
				break;
			case PartType::THERMOSTAT:
				drawList->AddEllipseFilled(p(0.50f, 0.38f), { size * 0.34f, size * 0.09f }, dim);
				drawList->AddEllipse(p(0.50f, 0.38f), { size * 0.34f, size * 0.09f }, color, 0.f, 24, line);
				for (int i = 0; i < 4; i++)
					drawList->AddLine(p(0.42f, 0.48f + i * 0.09f), p(0.58f, 0.52f + i * 0.09f), color, line);
				drawList->AddRectFilled(p(0.46f, 0.20f), p(0.54f, 0.30f), color);
				break;
			case PartType::THERMOSTAT_HOUSING:
				drawList->AddLine(p(0.30f, 0.80f), p(0.30f, 0.40f), color, line * 5.f);
				drawList->AddBezierQuadratic(p(0.30f, 0.42f), p(0.30f, 0.22f), p(0.62f, 0.22f), color, line * 5.f);
				drawList->AddLine(p(0.60f, 0.22f), p(0.86f, 0.22f), color, line * 5.f);
				drawList->AddLine(p(0.10f, 0.80f), p(0.50f, 0.80f), color, line * 1.6f);
				break;
			default:
				drawList->AddRect(p(0.2f, 0.2f), p(0.8f, 0.8f), color, 0.f, 0, line);
				break;
		}
	}

	void Icon(PartType type, float size, ImU32 color)
	{
		ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImGui::Dummy({ size, size });
		Draw(ImGui::GetWindowDrawList(), type, cursor, size, color);
	}

	const char* Description(PartType type)
	{
		return PartCatalog::Get(type).description;
	}
}
