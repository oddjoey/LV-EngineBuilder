#pragma once

#include <imgui.h>

#include "../Simulation/Machine.h"
#include "../Simulation/MeasuringTool.h"

// 0-1 scores comparing tools and machines with others of their kind, for the bars shown next to
// their icons. Precision and accuracy are on a log scale, since a tenfold improvement matters
// about as much at any level.
namespace Ratings
{
	float ToolPrecision(const MeasuringTool& tool);
	float ToolSpeed(const MeasuringTool& tool);
	float MachineAccuracy(const Machine& machine);
	float MachineSpeed(const Machine& machine);
	// Negative when the machine doesn't leave a finish worth rating.
	float MachineFinish(const Machine& machine);

	// A labelled bar for detail panels and tooltips.
	void Bar(const char* label, float value, float width = 160.f);
	// A thin bar drawn straight onto a card.
	void MiniBar(ImDrawList* drawList, ImVec2 min, float width, float value);
}
