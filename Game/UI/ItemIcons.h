#pragma once

#include <imgui.h>

#include "../Simulation/Furniture.h"
#include "../Simulation/Machine.h"
#include "../Simulation/MeasuringTool.h"
#include "PartIcons.h"

// Line-art icons for measuring tools and machines, in the same style as PartIcons.
namespace ItemIcons
{
	void DrawTool(ImDrawList* drawList, const MeasuringTool& tool, ImVec2 min, float size, ImU32 color = PartIcons::DEFAULT_COLOR);
	void DrawMachine(ImDrawList* drawList, const Machine& machine, ImVec2 min, float size, ImU32 color = PartIcons::DEFAULT_COLOR);
	void DrawFurniture(ImDrawList* drawList, const FurnitureInfo& furniture, ImVec2 min, float size, ImU32 color = PartIcons::DEFAULT_COLOR);
	// A part or a tool, whichever the item is.
	void DrawItem(ImDrawList* drawList, bool isTool, int toolIndex, PartType partType, ImVec2 min, float size, ImU32 color = PartIcons::DEFAULT_COLOR);
	void MachineIcon(const Machine& machine, float size, ImU32 color = PartIcons::DEFAULT_COLOR);
}
