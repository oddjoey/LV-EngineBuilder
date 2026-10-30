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
			DrawTool(drawList, ToolCatalog::All()[toolIndex], min, size, color);
		else
			PartIcons::Draw(drawList, partType, min, size, color);
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
		}
	}
}
