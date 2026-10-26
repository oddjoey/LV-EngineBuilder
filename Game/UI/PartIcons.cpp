#include "PartIcons.h"

#include <algorithm>
#include <cmath>

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
			{
				float radius = type == PartType::MAIN_BEARING ? 0.30f : 0.22f;
				drawList->PathArcTo(p(0.50f, 0.46f), size * radius, pi + 0.05f, 2.f * pi - 0.05f, 24);
				drawList->PathStroke(color, 0, line * 2.5f);
				drawList->PathArcTo(p(0.50f, 0.54f), size * radius, 0.05f, pi - 0.05f, 24);
				drawList->PathStroke(color, 0, line * 2.5f);
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
		switch (type)
		{
			case PartType::BLOCK:			return "Grey iron LV-20 block. A used core is cheap, but plan on boring, honing and decking it.";
			case PartType::CRANKSHAFT:		return "Five-main crank. Worn journals can be ground undersize to suit thicker bearings.";
			case PartType::CYLINDER_HEAD:	return "Aluminium SOHC 8-valve head. Check the deck, cam bores, guides and seats.";
			case PartType::HEAD_GASKET:		return "Multi-layer steel. Its compressed thickness sets quench and compression.";
			case PartType::CONNECTING_ROD:	return "Big ends stretch oval with miles. Center length moves the piston at TDC.";
			case PartType::PISTON:			return "Cast, hypereutectic or forged 2618. Forged grows more when hot: give it room.";
			case PartType::PISTON_RING:		return "Top compression ring. Gap it to its own bore; too tight and the ends butt when hot.";
			case PartType::MAIN_BEARING:	return "Shell pair for one main journal. Undersizes have thicker walls for reground cranks.";
			case PartType::ROD_BEARING:		return "Shell pair for one rod journal. Plastigauge it once it's torqued.";
			case PartType::CAMSHAFT:		return "Single overhead cam. Lobe lift is airflow; a flat lobe costs power.";
			case PartType::INTAKE_VALVE:	return "Intake valve. Oversize stems suit guides reamed to match.";
			case PartType::EXHAUST_VALVE:	return "Exhaust valve. Runs hotter than the intake and needs more guide clearance.";
			case PartType::VALVE_SPRING:	return "Seat pressure keeps the valve on the cam. Weak springs float at high rpm.";
		}
		return "";
	}
}
