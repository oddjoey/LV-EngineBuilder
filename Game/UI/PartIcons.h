#pragma once

#include <imgui.h>

#include "../Simulation/Part.h"

// Line-art icons for each part type, drawn with ImGui so they stay crisp at any size.
namespace PartIcons
{
	constexpr ImU32 DEFAULT_COLOR = IM_COL32(205, 210, 220, 255);

	// Draws into the square starting at `min` with side `size`.
	void Draw(ImDrawList* drawList, PartType type, ImVec2 min, float size, ImU32 color = DEFAULT_COLOR);
	// The seals, gaskets and bolt-ons; Draw hands them over.
	void DrawAncillary(ImDrawList* drawList, PartType type, ImVec2 min, float size, ImU32 color = DEFAULT_COLOR);
	// Takes up a size x size spot in the current window and draws the icon there.
	void Icon(PartType type, float size, ImU32 color = DEFAULT_COLOR);
	// A sentence or two for the supplier's catalogue.
	const char* Description(PartType type);
}
