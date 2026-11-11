#include "ItemIcons.h"

#include <algorithm>
#include <cmath>

#include "../World/ToolShapes.h"

namespace ItemIcons
{
	void DrawTool(ImDrawList* drawList, const MeasuringTool& tool, ImVec2 min, float size, ImU32 color)
	{
		auto p = [&](float x, float y) { return ImVec2(min.x + x * size, min.y + y * size); };
		float line = std::max(1.f, size * 0.045f);
		const ImU32 dial = IM_COL32(235, 235, 225, 255);

		switch (ToolShapeFor(tool))
		{
			case ToolShape::CALIPER:
				drawList->AddRectFilled(p(0.10f, 0.42f), p(0.92f, 0.52f), color);
				drawList->AddRectFilled(p(0.12f, 0.20f), p(0.20f, 0.42f), color);
				drawList->AddRectFilled(p(0.36f, 0.24f), p(0.44f, 0.42f), color);
				drawList->AddRectFilled(p(0.32f, 0.52f), p(0.56f, 0.66f), color);
				drawList->AddRectFilled(p(0.36f, 0.55f), p(0.52f, 0.63f), IM_COL32(60, 70, 60, 255));
				break;
			case ToolShape::MICROMETER:
			case ToolShape::HEIGHT_MICROMETER:
				drawList->PathArcTo(p(0.40f, 0.50f), size * 0.26f, 1.57f, 4.71f, 20);
				drawList->PathStroke(color, 0, line * 2.2f);
				drawList->AddRectFilled(p(0.40f, 0.20f), p(0.56f, 0.30f), color);
				drawList->AddRectFilled(p(0.56f, 0.18f), p(0.88f, 0.32f), color, size * 0.03f);
				drawList->AddLine(p(0.60f, 0.25f), p(0.86f, 0.25f), IM_COL32(40, 40, 40, 255), line);
				if (ToolShapeFor(tool) == ToolShape::HEIGHT_MICROMETER)
					drawList->AddRectFilled(p(0.10f, 0.80f), p(0.70f, 0.88f), color);
				break;
			case ToolShape::TELESCOPING_GAUGE:
				drawList->AddLine(p(0.50f, 0.15f), p(0.50f, 0.90f), color, line * 1.6f);
				drawList->AddLine(p(0.25f, 0.20f), p(0.75f, 0.20f), color, line * 2.f);
				drawList->AddCircleFilled(p(0.50f, 0.85f), size * 0.06f, color);
				break;
			case ToolShape::BORE_GAUGE:
			case ToolShape::SMALL_BORE_GAUGE:
				drawList->AddLine(p(0.50f, 0.32f), p(0.50f, 0.90f), color, line * 1.6f);
				drawList->AddCircleFilled(p(0.50f, 0.22f), size * 0.15f, color);
				drawList->AddCircleFilled(p(0.50f, 0.22f), size * 0.11f, dial);
				drawList->AddLine(p(0.50f, 0.22f), p(0.57f, 0.15f), IM_COL32(200, 40, 40, 255), line);
				drawList->AddLine(p(0.40f, 0.88f), p(0.60f, 0.88f), color, line * 2.f);
				break;
			case ToolShape::SMALL_HOLE_GAUGE:
				for (int i = 0; i < 4; i++)
				{
					float x = 0.22f + i * 0.18f;
					drawList->AddLine(p(x, 0.30f + i * 0.04f), p(x, 0.85f), color, line * 1.3f);
					drawList->AddCircleFilled(p(x, 0.28f + i * 0.04f), size * (0.04f + i * 0.012f), color);
				}
				break;
			case ToolShape::STRAIGHTEDGE:
				drawList->AddRectFilled(p(0.05f, 0.40f), p(0.95f, 0.52f), color);
				for (int i = 0; i < 9; i++)
					drawList->AddLine(p(0.10f + i * 0.1f, 0.40f), p(0.10f + i * 0.1f, 0.45f), IM_COL32(40, 40, 40, 255), line * 0.6f);
				drawList->AddRectFilled(p(0.55f, 0.62f), p(0.90f, 0.68f), color);
				break;
			case ToolShape::BURETTE:
				drawList->AddRect(p(0.42f, 0.10f), p(0.58f, 0.75f), color, 0.f, 0, line);
				drawList->AddRectFilled(p(0.43f, 0.40f), p(0.57f, 0.74f), IM_COL32(120, 170, 230, 200));
				drawList->AddLine(p(0.50f, 0.75f), p(0.50f, 0.88f), color, line);
				drawList->AddRectFilled(p(0.25f, 0.88f), p(0.75f, 0.94f), color);
				break;
			case ToolShape::PROFILOMETER:
				drawList->AddRectFilled(p(0.15f, 0.30f), p(0.75f, 0.62f), color, size * 0.04f);
				drawList->AddRectFilled(p(0.22f, 0.36f), p(0.55f, 0.52f), IM_COL32(60, 120, 90, 255));
				drawList->AddLine(p(0.75f, 0.55f), p(0.92f, 0.70f), color, line);
				drawList->AddCircleFilled(p(0.92f, 0.72f), size * 0.03f, color);
				break;
			case ToolShape::PLASTIGAUGE:
				drawList->AddRectFilled(p(0.18f, 0.25f), p(0.82f, 0.75f), IM_COL32(80, 150, 80, 255), size * 0.04f);
				drawList->AddLine(p(0.25f, 0.50f), p(0.75f, 0.50f), dial, line * 1.5f);
				break;
			case ToolShape::FEELERS:
				for (int i = 0; i < 5; i++)
				{
					float angle = -0.6f + i * 0.3f;
					drawList->AddLine(p(0.30f, 0.80f), p(0.30f + std::sin(angle + 0.8f) * 0.6f, 0.80f - std::cos(angle + 0.8f) * 0.6f), color, line * 1.6f);
				}
				drawList->AddCircleFilled(p(0.30f, 0.80f), size * 0.06f, color);
				break;
			case ToolShape::DIAL_INDICATOR:
			case ToolShape::RUNOUT_GAUGE:
				drawList->AddCircleFilled(p(0.50f, 0.40f), size * 0.24f, color);
				drawList->AddCircleFilled(p(0.50f, 0.40f), size * 0.19f, dial);
				drawList->AddLine(p(0.50f, 0.40f), p(0.62f, 0.28f), IM_COL32(200, 40, 40, 255), line);
				drawList->AddLine(p(0.50f, 0.64f), p(0.50f, 0.86f), color, line * 1.3f);
				if (ToolShapeFor(tool) == ToolShape::RUNOUT_GAUGE)
				{
					drawList->AddTriangleFilled(p(0.08f, 0.94f), p(0.30f, 0.94f), p(0.19f, 0.78f), color);
					drawList->AddTriangleFilled(p(0.70f, 0.94f), p(0.92f, 0.94f), p(0.81f, 0.78f), color);
				}
				break;
			case ToolShape::SPRING_TESTER:
				drawList->AddRectFilled(p(0.15f, 0.85f), p(0.85f, 0.93f), color);
				drawList->AddRectFilled(p(0.70f, 0.15f), p(0.78f, 0.85f), color);
				drawList->AddLine(p(0.20f, 0.30f), p(0.78f, 0.22f), color, line * 1.8f);
				drawList->AddCircleFilled(p(0.40f, 0.50f), size * 0.12f, color);
				drawList->AddCircleFilled(p(0.40f, 0.50f), size * 0.08f, dial);
				break;
			case ToolShape::RATCHET:
			case ToolShape::BREAKER_BAR:
			case ToolShape::TORQUE_WRENCH:
				drawList->AddLine(p(0.15f, 0.85f), p(0.75f, 0.25f), color, line * 3.f);
				drawList->AddCircleFilled(p(0.78f, 0.22f), size * 0.1f, color);
				if (ToolShapeFor(tool) == ToolShape::TORQUE_WRENCH)
					drawList->AddCircleFilled(p(0.42f, 0.58f), size * 0.08f, dial);
				break;
			case ToolShape::SOCKET_SET:
				for (int i = 0; i < 5; i++)
				{
					drawList->AddCircle(p(0.16f + i * 0.17f, 0.5f), size * (0.05f + i * 0.012f), color, 6, line * 1.4f);
				}
				drawList->AddRect(p(0.05f, 0.3f), p(0.95f, 0.7f), color, size * 0.03f, 0, line);
				break;
			case ToolShape::WRENCH_SET:
				for (int i = 0; i < 3; i++)
				{
					float y = 0.3f + i * 0.2f;
					drawList->AddLine(p(0.2f, y), p(0.8f, y), color, line * 2.f);
					drawList->AddCircle(p(0.16f, y), size * 0.06f, color, 12, line * 1.3f);
				}
				break;
			case ToolShape::MANUAL:
				// A book: the cover, its spine, a title band and lines of text.
				drawList->AddRect(p(0.22f, 0.12f), p(0.82f, 0.9f), color, size * 0.03f, 0, line * 1.6f);
				drawList->AddRectFilled(p(0.22f, 0.12f), p(0.3f, 0.9f), color);
				drawList->AddRectFilled(p(0.38f, 0.24f), p(0.74f, 0.36f), color);
				for (int i = 0; i < 3; i++)
					drawList->AddLine(p(0.38f, 0.5f + i * 0.1f), p(0.72f, 0.5f + i * 0.1f), color, line);
				break;
			case ToolShape::STEEL_SQUARE:
				// The stock up the left, the blade along the bottom.
				drawList->AddRectFilled(p(0.18f, 0.10f), p(0.32f, 0.86f), color);
				drawList->AddRectFilled(p(0.32f, 0.78f), p(0.90f, 0.86f), color);
				break;
			case ToolShape::ROD_ALIGNER:
				// The base, the upright plate, and the mandrel out from it.
				drawList->AddRectFilled(p(0.10f, 0.80f), p(0.90f, 0.90f), color);
				drawList->AddRectFilled(p(0.14f, 0.12f), p(0.24f, 0.80f), color);
				drawList->AddLine(p(0.24f, 0.50f), p(0.80f, 0.50f), color, line * 3.f);
				drawList->AddCircle(p(0.66f, 0.50f), size * 0.12f, color, 20, line);
				break;
			case ToolShape::CORDLESS_RATCHET:
				// A ratchet with a fat motor housing and a pack on the end.
				drawList->AddLine(p(0.25f, 0.75f), p(0.75f, 0.25f), color, line * 3.f);
				drawList->AddCircleFilled(p(0.78f, 0.22f), size * 0.1f, color);
				drawList->AddLine(p(0.25f, 0.75f), p(0.45f, 0.55f), color, line * 6.f);
				drawList->AddRectFilled(p(0.08f, 0.76f), p(0.30f, 0.94f), color, size * 0.03f);
				break;
			case ToolShape::IMPACT:
				// A pistol: the body and nose across the top, the grip down, the pack under it.
				drawList->AddRectFilled(p(0.12f, 0.20f), p(0.72f, 0.42f), color, size * 0.05f);
				drawList->AddRectFilled(p(0.72f, 0.26f), p(0.86f, 0.36f), color);
				drawList->AddRectFilled(p(0.30f, 0.42f), p(0.46f, 0.74f), color);
				drawList->AddRectFilled(p(0.20f, 0.74f), p(0.58f, 0.90f), color, size * 0.03f);
				break;
			case ToolShape::BATTERY:
			{
				// A pack, with a bar for how big it is.
				drawList->AddRect(p(0.15f, 0.30f), p(0.85f, 0.82f), color, size * 0.05f, 0, line * 1.5f);
				drawList->AddRectFilled(p(0.35f, 0.18f), p(0.65f, 0.30f), color);
				float filled = std::clamp(static_cast<float>(tool.capacityAh) / 6.f, 0.2f, 1.f);
				drawList->AddRectFilled(p(0.24f, 0.70f - 0.32f * filled), p(0.76f, 0.72f), color);
				break;
			}
			case ToolShape::WRENCH:
				// One spanner: the ring at the top, the open jaws at the bottom.
				drawList->AddLine(p(0.28f, 0.8f), p(0.72f, 0.24f), color, line * 2.6f);
				drawList->AddCircle(p(0.74f, 0.21f), size * 0.1f, color, 16, line * 1.6f);
				drawList->AddLine(p(0.2f, 0.84f), p(0.18f, 0.94f), color, line * 2.f);
				drawList->AddLine(p(0.32f, 0.9f), p(0.34f, 0.98f), color, line * 2.f);
				break;
			case ToolShape::CHARGER:
				// The cradle, and a bolt of lightning over it.
				drawList->AddRectFilled(p(0.10f, 0.62f), p(0.90f, 0.86f), color, size * 0.04f);
				drawList->AddTriangleFilled(p(0.54f, 0.10f), p(0.36f, 0.38f), p(0.50f, 0.38f), color);
				drawList->AddTriangleFilled(p(0.50f, 0.32f), p(0.64f, 0.32f), p(0.44f, 0.58f), color);
				break;
			case ToolShape::MARKER:
			case ToolShape::SPRAY_CAN:
			{
				// In its own colour (greyed with the rest when the icon is): a marker on the slant, or a can.
				ImU32 paint = IM_COL32((tool.paintColor >> 16) & 0xFF, (tool.paintColor >> 8) & 0xFF, tool.paintColor & 0xFF,
					(color >> IM_COL32_A_SHIFT) & 0xFF);
				if (ToolShapeFor(tool) == ToolShape::MARKER)
				{
					drawList->AddLine(p(0.22f, 0.78f), p(0.70f, 0.30f), paint, line * 4.f);
					drawList->AddLine(p(0.70f, 0.30f), p(0.80f, 0.20f), color, line * 2.f);
				}
				else
				{
					drawList->AddRectFilled(p(0.30f, 0.30f), p(0.70f, 0.90f), paint, size * 0.05f);
					drawList->AddRect(p(0.30f, 0.30f), p(0.70f, 0.90f), color, size * 0.05f, 0, line);
					drawList->AddRectFilled(p(0.44f, 0.16f), p(0.56f, 0.30f), color);
					for (float y : { 0.12f, 0.2f, 0.28f })
						drawList->AddCircleFilled(p(0.72f, y), size * 0.025f, paint);
				}
				break;
			}
			case ToolShape::RAG:
				// A tin with a rag over its lid.
				drawList->AddRect(p(0.25f, 0.40f), p(0.75f, 0.88f), color, size * 0.04f, 0, line * 1.5f);
				drawList->AddRectFilled(p(0.18f, 0.28f), p(0.82f, 0.40f), color, size * 0.03f);
				drawList->AddLine(p(0.30f, 0.28f), p(0.36f, 0.18f), color, line);
				drawList->AddLine(p(0.66f, 0.28f), p(0.60f, 0.18f), color, line);
				break;
			case ToolShape::PIN_PRESS:
				// A C-frame with the screw through its top jaw, pressing down on a pin.
				drawList->AddLine(p(0.25f, 0.2f), p(0.25f, 0.85f), color, line * 1.8f);
				drawList->AddLine(p(0.25f, 0.2f), p(0.7f, 0.2f), color, line * 1.8f);
				drawList->AddLine(p(0.25f, 0.85f), p(0.7f, 0.85f), color, line * 1.8f);
				drawList->AddLine(p(0.62f, 0.08f), p(0.62f, 0.55f), color, line * 1.2f);
				drawList->AddLine(p(0.5f, 0.08f), p(0.74f, 0.08f), color, line);
				drawList->AddRectFilled(p(0.52f, 0.6f), p(0.72f, 0.75f), color, size * 0.02f);
				break;
			case ToolShape::RING_COMPRESSOR:
				// The band, side on, and its ratchet key.
				drawList->AddRect(p(0.15f, 0.35f), p(0.7f, 0.7f), color, size * 0.03f, 0, line * 1.6f);
				drawList->AddRectFilled(p(0.7f, 0.45f), p(0.78f, 0.6f), color);
				drawList->AddLine(p(0.78f, 0.52f), p(0.9f, 0.52f), color, line);
				drawList->AddLine(p(0.9f, 0.4f), p(0.9f, 0.64f), color, line * 1.4f);
				break;
			case ToolShape::FUNNEL:
				// A funnel: a cone over its spout.
				drawList->AddTriangle(p(0.2f, 0.2f), p(0.8f, 0.2f), p(0.5f, 0.6f), color, line * 1.4f);
				drawList->AddLine(p(0.5f, 0.6f), p(0.5f, 0.85f), color, line * 1.6f);
				break;
			case ToolShape::BLEEDER:
				// The bottle and its hose.
				drawList->AddRect(p(0.3f, 0.4f), p(0.62f, 0.88f), color, size * 0.04f, 0, line * 1.4f);
				drawList->AddBezierCubic(p(0.46f, 0.4f), p(0.46f, 0.1f), p(0.8f, 0.1f), p(0.8f, 0.4f), color, line);
				break;
			case ToolShape::SPRING_COMPRESSOR:
				// A coil between two hooks.
				for (int coil = 0; coil < 4; coil++)
					drawList->AddLine(p(0.3f, 0.3f + coil * 0.12f), p(0.7f, 0.36f + coil * 0.12f), color, line * 1.2f);
				drawList->AddLine(p(0.2f, 0.18f), p(0.2f, 0.82f), color, line * 1.6f);
				drawList->AddLine(p(0.8f, 0.18f), p(0.8f, 0.82f), color, line * 1.6f);
				break;
			case ToolShape::TIRE_IRONS:
				drawList->AddLine(p(0.2f, 0.8f), p(0.75f, 0.2f), color, line * 1.8f);
				drawList->AddLine(p(0.3f, 0.85f), p(0.85f, 0.3f), color, line * 1.8f);
				break;
			case ToolShape::TIMING_LIGHT:
				// A strobe gun and its flash.
				drawList->AddRectFilled(p(0.2f, 0.35f), p(0.65f, 0.55f), color, size * 0.03f);
				drawList->AddLine(p(0.3f, 0.55f), p(0.25f, 0.85f), color, line * 1.8f);
				drawList->AddLine(p(0.72f, 0.45f), p(0.9f, 0.3f), color, line);
				drawList->AddLine(p(0.72f, 0.45f), p(0.9f, 0.6f), color, line);
				break;
			case ToolShape::TIRE_GAUGE:
				drawList->AddCircle(p(0.4f, 0.4f), size * 0.22f, color, 16, line * 1.4f);
				drawList->AddLine(p(0.4f, 0.4f), p(0.52f, 0.28f), color, line);
				drawList->AddLine(p(0.56f, 0.56f), p(0.85f, 0.85f), color, line * 1.6f);
				break;
			case ToolShape::ALIGNMENT_GAUGE:
				// A bubble level.
				drawList->AddRect(p(0.15f, 0.38f), p(0.85f, 0.62f), color, size * 0.05f, 0, line * 1.4f);
				drawList->AddCircleFilled(p(0.55f, 0.5f), size * 0.06f, color);
				break;
			case ToolShape::SCREWDRIVER:
				drawList->AddLine(p(0.2f, 0.8f), p(0.45f, 0.55f), color, line * 3.f);
				drawList->AddLine(p(0.45f, 0.55f), p(0.82f, 0.18f), color, line);
				break;
			case ToolShape::PICKAXE:
				// A long handle and the curved head across its end: the point one way, the adze the other.
				drawList->AddLine(p(0.22f, 0.88f), p(0.66f, 0.26f), color, line * 2.2f);
				drawList->AddBezierQuadratic(p(0.32f, 0.3f), p(0.62f, 0.08f), p(0.94f, 0.36f), color, line * 3.f);
				break;
		}
	}

	void DrawMachine(ImDrawList* drawList, const Machine& machine, ImVec2 min, float size, ImU32 color)
	{
		auto p = [&](float x, float y) { return ImVec2(min.x + x * size, min.y + y * size); };
		float line = std::max(1.f, size * 0.045f);
		ImU32 dim = (color & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT);

		// Every machine stands on a base; the working head tells them apart.
		drawList->AddRectFilled(p(0.12f, 0.72f), p(0.88f, 0.92f), dim);
		drawList->AddRect(p(0.12f, 0.72f), p(0.88f, 0.92f), color, 0.f, 0, line);

		switch (machine.operation)
		{
			case MachineOperation::BORE:
			case MachineOperation::HONE:
			case MachineOperation::REAM:
				drawList->AddRectFilled(p(0.62f, 0.15f), p(0.74f, 0.72f), color);
				drawList->AddRectFilled(p(0.30f, 0.15f), p(0.74f, 0.30f), color);
				drawList->AddLine(p(0.42f, 0.30f), p(0.42f, 0.60f), color, line * 1.8f);
				if (machine.operation == MachineOperation::HONE)
					drawList->AddRectFilled(p(0.37f, 0.52f), p(0.47f, 0.64f), IM_COL32(160, 130, 100, 255));
				break;
			case MachineOperation::GRIND:
			case MachineOperation::POLISH:
				drawList->AddCircle(p(0.60f, 0.42f), size * 0.20f, color, 24, line * 1.5f);
				drawList->AddLine(p(0.15f, 0.62f), p(0.85f, 0.62f), color, line * 1.6f);
				break;
			case MachineOperation::SURFACE:
				if (machine.removalError > 0.0)
				{
					// A long beam with a handle at each end, sandpaper underneath.
					drawList->AddRectFilled(p(0.08f, 0.50f), p(0.92f, 0.62f), color);
					drawList->AddRectFilled(p(0.08f, 0.62f), p(0.92f, 0.66f), IM_COL32(200, 180, 130, 255));
					drawList->AddRectFilled(p(0.18f, 0.40f), p(0.28f, 0.50f), color);
					drawList->AddRectFilled(p(0.72f, 0.40f), p(0.82f, 0.50f), color);
					break;
				}
				drawList->AddRectFilled(p(0.66f, 0.12f), p(0.78f, 0.72f), color);
				drawList->AddCircle(p(0.45f, 0.40f), size * 0.13f, color, 20, line * 1.4f);
				drawList->AddRectFilled(p(0.20f, 0.62f), p(0.70f, 0.70f), color);
				break;
			case MachineOperation::FILE_RING:
				drawList->AddCircle(p(0.45f, 0.48f), size * 0.18f, color, 20, line * 1.4f);
				drawList->AddLine(p(0.45f, 0.48f), p(0.75f, 0.25f), color, line * 1.4f);
				break;
			case MachineOperation::CUT_SEAT:
				drawList->AddRectFilled(p(0.64f, 0.12f), p(0.76f, 0.72f), color);
				drawList->AddLine(p(0.45f, 0.20f), p(0.45f, 0.55f), color, line * 1.6f);
				drawList->AddTriangleFilled(p(0.35f, 0.55f), p(0.55f, 0.55f), p(0.45f, 0.68f), color);
				break;
			case MachineOperation::FACE_VALVE:
				drawList->AddCircle(p(0.30f, 0.45f), size * 0.15f, color, 20, line * 1.4f);
				drawList->AddLine(p(0.50f, 0.45f), p(0.88f, 0.45f), color, line * 1.4f);
				drawList->AddTriangleFilled(p(0.45f, 0.36f), p(0.45f, 0.54f), p(0.52f, 0.45f), color);
				break;
			case MachineOperation::LINE_BORE:
				// A long bar through a row of saddles.
				drawList->AddLine(p(0.06f, 0.45f), p(0.94f, 0.45f), color, line * 2.f);
				for (int i = 0; i < 5; i++)
				{
					float x = 0.18f + i * 0.16f;
					drawList->AddRectFilled(p(x - 0.04f, 0.45f), p(x + 0.04f, 0.72f), dim);
					drawList->AddCircle(p(x, 0.45f), size * 0.06f, color, 14, line);
				}
				drawList->AddRectFilled(p(0.04f, 0.32f), p(0.12f, 0.58f), color);
				break;
		}
	}

	void DrawItem(ImDrawList* drawList, bool isTool, int toolIndex, PartType partType, ImVec2 min, float size, ImU32 color)
	{
		if (isTool)
			DrawTool(drawList, ToolCatalog::Of(toolIndex), min, size, color);
		else if (toolIndex == PACKAGE)
			DrawPackage(drawList, min, size, color);
		else
			PartIcons::Draw(drawList, partType, min, size, color);
	}

	void DrawPackage(ImDrawList* drawList, ImVec2 min, float size, ImU32 color)
	{
		// A taped-up cardboard box, seen from a corner above.
		auto p = [&](float x, float y) { return ImVec2(min.x + x * size, min.y + y * size); };
		float line = std::max(1.f, size * 0.045f);
		ImU32 dim = (color & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT);
		ImVec2 top[4] = { p(0.5f, 0.12f), p(0.9f, 0.3f), p(0.5f, 0.48f), p(0.1f, 0.3f) };
		ImVec2 left[4] = { p(0.1f, 0.3f), p(0.5f, 0.48f), p(0.5f, 0.9f), p(0.1f, 0.72f) };
		ImVec2 right[4] = { p(0.5f, 0.48f), p(0.9f, 0.3f), p(0.9f, 0.72f), p(0.5f, 0.9f) };
		drawList->AddConvexPolyFilled(left, 4, dim);
		drawList->AddConvexPolyFilled(right, 4, dim);
		for (const ImVec2* face : { top, left, right })
			drawList->AddPolyline(face, 4, color, ImDrawFlags_Closed, line);
		drawList->AddLine(p(0.3f, 0.21f), p(0.7f, 0.39f), color, line * 1.4f);
	}

	void DrawDyno(ImDrawList* drawList, ImVec2 min, float size, ImU32 color)
	{
		// The dyno from the side: its bed, the absorber drum at one end, and a power curve over it.
		auto p = [&](float x, float y) { return ImVec2(min.x + x * size, min.y + y * size); };
		float line = std::max(1.f, size * 0.045f);
		ImU32 dim = (color & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT);
		drawList->AddRectFilled(p(0.08f, 0.72f), p(0.92f, 0.88f), dim);
		drawList->AddRect(p(0.08f, 0.72f), p(0.92f, 0.88f), color, 0.f, 0, line);
		drawList->AddCircle(p(0.72f, 0.6f), size * 0.13f, color, 0, line);
		drawList->AddCircleFilled(p(0.72f, 0.6f), size * 0.04f, color);
		drawList->AddRect(p(0.18f, 0.5f), p(0.5f, 0.72f), color, 0.f, 0, line);
		const ImVec2 curve[] = { p(0.1f, 0.42f), p(0.25f, 0.3f), p(0.42f, 0.2f), p(0.6f, 0.15f), p(0.78f, 0.17f), p(0.9f, 0.24f) };
		drawList->AddPolyline(curve, 6, color, ImDrawFlags_None, line);
	}

	void DrawHoist(ImDrawList* drawList, ImVec2 min, float size, ImU32 color)
	{
		// The hoist from the side: legs on casters, the mast, the boom out over the legs, and an engine on the hook.
		auto p = [&](float x, float y) { return ImVec2(min.x + x * size, min.y + y * size); };
		float line = std::max(1.f, size * 0.045f);
		ImU32 dim = (color & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT);
		drawList->AddLine(p(0.12f, 0.84f), p(0.9f, 0.84f), color, line * 1.4f);
		for (float x : { 0.16f, 0.86f })
			drawList->AddCircleFilled(p(x, 0.9f), size * 0.045f, color);
		drawList->AddLine(p(0.22f, 0.84f), p(0.22f, 0.18f), color, line * 1.4f);
		drawList->AddLine(p(0.16f, 0.2f), p(0.8f, 0.1f), color, line * 1.4f);
		drawList->AddLine(p(0.26f, 0.74f), p(0.52f, 0.16f), color, line);
		drawList->AddLine(p(0.78f, 0.11f), p(0.78f, 0.36f), color, line);
		drawList->AddRectFilled(p(0.64f, 0.4f), p(0.92f, 0.64f), dim, size * 0.03f);
		drawList->AddRect(p(0.64f, 0.4f), p(0.92f, 0.64f), color, size * 0.03f, 0, line);
		drawList->AddLine(p(0.7f, 0.4f), p(0.78f, 0.36f), color, line);
		drawList->AddLine(p(0.86f, 0.4f), p(0.78f, 0.36f), color, line);
	}

	void MachineIcon(const Machine& machine, float size, ImU32 color)
	{
		ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImGui::Dummy({ size, size });
		DrawMachine(ImGui::GetWindowDrawList(), machine, cursor, size, color);
	}

	void DrawFurniture(ImDrawList* drawList, const FurnitureInfo& furniture, ImVec2 min, float size, ImU32 color)
	{
		auto p = [&](float x, float y) { return ImVec2(min.x + x * size, min.y + y * size); };
		float line = std::max(1.f, size * 0.045f);
		ImU32 dim = (color & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT);

		switch (furniture.kind)
		{
			case FurnitureKind::SHELVING:
				drawList->AddLine(p(0.2f, 0.1f), p(0.2f, 0.9f), color, line);
				drawList->AddLine(p(0.8f, 0.1f), p(0.8f, 0.9f), color, line);
				for (float y : { 0.25f, 0.45f, 0.65f, 0.85f })
					drawList->AddLine(p(0.16f, y), p(0.84f, y), color, line * 1.4f);
				break;
			case FurnitureKind::WORKBENCH:
				drawList->AddRectFilled(p(0.08f, 0.38f), p(0.92f, 0.48f), color);
				drawList->AddLine(p(0.16f, 0.48f), p(0.16f, 0.88f), color, line * 1.4f);
				drawList->AddLine(p(0.84f, 0.48f), p(0.84f, 0.88f), color, line * 1.4f);
				drawList->AddLine(p(0.16f, 0.75f), p(0.84f, 0.75f), color, line);
				break;
			case FurnitureKind::TOOL_CART:
				drawList->AddRectFilled(p(0.18f, 0.22f), p(0.82f, 0.78f), dim, size * 0.03f);
				drawList->AddRect(p(0.18f, 0.22f), p(0.82f, 0.78f), color, size * 0.03f, 0, line);
				for (float y : { 0.36f, 0.5f, 0.64f })
					drawList->AddLine(p(0.4f, y), p(0.6f, y), color, line);
				drawList->AddCircleFilled(p(0.28f, 0.86f), size * 0.06f, color);
				drawList->AddCircleFilled(p(0.72f, 0.86f), size * 0.06f, color);
				break;
			case FurnitureKind::STOOL:
				drawList->AddEllipseFilled(p(0.5f, 0.3f), { size * 0.24f, size * 0.07f }, color);
				drawList->AddLine(p(0.36f, 0.33f), p(0.28f, 0.88f), color, line);
				drawList->AddLine(p(0.64f, 0.33f), p(0.72f, 0.88f), color, line);
				drawList->AddLine(p(0.32f, 0.66f), p(0.68f, 0.66f), color, line);
				break;
			case FurnitureKind::OFFICE_CHAIR:
				drawList->AddRectFilled(p(0.3f, 0.12f), p(0.7f, 0.48f), dim, size * 0.05f);
				drawList->AddRect(p(0.3f, 0.12f), p(0.7f, 0.48f), color, size * 0.05f, 0, line);
				drawList->AddRectFilled(p(0.24f, 0.52f), p(0.76f, 0.6f), color, size * 0.03f);
				drawList->AddLine(p(0.5f, 0.6f), p(0.5f, 0.8f), color, line);
				drawList->AddLine(p(0.26f, 0.86f), p(0.74f, 0.86f), color, line);
				break;
			case FurnitureKind::OIL_DRUM:
				drawList->AddRectFilled(p(0.26f, 0.16f), p(0.74f, 0.88f), dim);
				drawList->AddRect(p(0.26f, 0.16f), p(0.74f, 0.88f), color, 0.f, 0, line);
				drawList->AddLine(p(0.26f, 0.4f), p(0.74f, 0.4f), color, line);
				drawList->AddLine(p(0.26f, 0.64f), p(0.74f, 0.64f), color, line);
				break;
			case FurnitureKind::TRASH_CAN:
				drawList->AddQuadFilled(p(0.24f, 0.24f), p(0.76f, 0.24f), p(0.7f, 0.88f), p(0.3f, 0.88f), dim);
				drawList->AddQuad(p(0.24f, 0.24f), p(0.76f, 0.24f), p(0.7f, 0.88f), p(0.3f, 0.88f), color, line);
				drawList->AddLine(p(0.18f, 0.2f), p(0.82f, 0.2f), color, line * 1.6f);
				break;
			case FurnitureKind::FLOOR_MAT:
				drawList->AddQuadFilled(p(0.2f, 0.45f), p(0.92f, 0.45f), p(0.8f, 0.75f), p(0.08f, 0.75f), dim);
				drawList->AddQuad(p(0.2f, 0.45f), p(0.92f, 0.45f), p(0.8f, 0.75f), p(0.08f, 0.75f), color, line);
				break;
			case FurnitureKind::POTTED_PLANT:
				drawList->AddQuadFilled(p(0.32f, 0.6f), p(0.68f, 0.6f), p(0.62f, 0.9f), p(0.38f, 0.9f), color);
				for (float angle : { -2.2f, -1.6f, -1.0f, -2.6f, -0.6f })
					drawList->AddLine(p(0.5f, 0.6f), p(0.5f + std::cos(angle) * 0.32f, 0.6f + std::sin(angle) * 0.42f), color, line * 1.6f);
				break;
			case FurnitureKind::SHOP_FAN:
				drawList->AddCircle(p(0.5f, 0.34f), size * 0.24f, color, 28, line);
				for (int blade = 0; blade < 3; blade++)
				{
					float angle = blade * 2.094f;
					drawList->AddLine(p(0.5f, 0.34f), p(0.5f + std::cos(angle) * 0.18f, 0.34f + std::sin(angle) * 0.18f), color, line * 2.f);
				}
				drawList->AddLine(p(0.5f, 0.58f), p(0.5f, 0.88f), color, line);
				drawList->AddLine(p(0.32f, 0.9f), p(0.68f, 0.9f), color, line * 1.4f);
				break;
			case FurnitureKind::GAS_PUMP:
				drawList->AddRectFilled(p(0.32f, 0.3f), p(0.68f, 0.9f), dim);
				drawList->AddRect(p(0.32f, 0.3f), p(0.68f, 0.9f), color, 0.f, 0, line);
				drawList->AddCircleFilled(p(0.5f, 0.18f), size * 0.1f, color);
				drawList->AddRectFilled(p(0.4f, 0.42f), p(0.6f, 0.56f), color);
				drawList->AddLine(p(0.68f, 0.5f), p(0.8f, 0.7f), color, line);
				break;
			case FurnitureKind::MONCHU:
				// A monkey's head on its plinth.
				drawList->AddRectFilled(p(0.26f, 0.78f), p(0.74f, 0.92f), dim);
				drawList->AddRect(p(0.26f, 0.78f), p(0.74f, 0.92f), color, 0.f, 0, line);
				drawList->AddCircleFilled(p(0.24f, 0.44f), size * 0.08f, color);
				drawList->AddCircleFilled(p(0.76f, 0.44f), size * 0.08f, color);
				drawList->AddCircleFilled(p(0.5f, 0.46f), size * 0.25f, color);
				drawList->AddCircleFilled(p(0.5f, 0.52f), size * 0.15f, dim);
				break;
		}
	}
}
