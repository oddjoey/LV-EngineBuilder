#pragma once

#include <string>

#include <glm/glm.hpp>

#include "../Simulation/MeasuringTool.h"

// What a measuring tool physically is, which decides its model, its icon and how much room it
// takes in the backpack.
enum class ToolShape
{
	CALIPER,
	MICROMETER,
	TELESCOPING_GAUGE,
	BORE_GAUGE,
	SMALL_HOLE_GAUGE,
	SMALL_BORE_GAUGE,
	STRAIGHTEDGE,
	BURETTE,
	PROFILOMETER,
	PLASTIGAUGE,
	FEELERS,
	DIAL_INDICATOR,
	RUNOUT_GAUGE,
	SPRING_TESTER,
	HEIGHT_MICROMETER
};

inline ToolShape ToolShapeFor(const MeasuringTool& tool)
{
	const std::string& name = tool.name;
	auto has = [&](const char* text) { return name.find(text) != std::string::npos; };
	if (has("Caliper")) return ToolShape::CALIPER;
	if (has("Installed Height")) return ToolShape::HEIGHT_MICROMETER;
	if (has("Micrometer")) return ToolShape::MICROMETER;
	if (has("Telescoping")) return ToolShape::TELESCOPING_GAUGE;
	if (has("Small Hole")) return ToolShape::SMALL_HOLE_GAUGE;
	if (has("Small-Bore")) return ToolShape::SMALL_BORE_GAUGE;
	if (has("Bore Gauge")) return ToolShape::BORE_GAUGE;
	if (has("Straightedge")) return ToolShape::STRAIGHTEDGE;
	if (has("Burette")) return ToolShape::BURETTE;
	if (has("Profilometer")) return ToolShape::PROFILOMETER;
	if (has("Plastigauge")) return ToolShape::PLASTIGAUGE;
	if (has("Feeler")) return ToolShape::FEELERS;
	if (has("Runout")) return ToolShape::RUNOUT_GAUGE;
	if (has("Spring Tester")) return ToolShape::SPRING_TESTER;
	return ToolShape::DIAL_INDICATOR;
}

// Backpack cells, width x height.
inline glm::ivec2 ToolGridSize(ToolShape shape)
{
	switch (shape)
	{
		case ToolShape::CALIPER:			return { 1, 3 };
		case ToolShape::MICROMETER:			return { 2, 2 };
		case ToolShape::TELESCOPING_GAUGE:	return { 1, 2 };
		case ToolShape::BORE_GAUGE:			return { 1, 4 };
		case ToolShape::SMALL_HOLE_GAUGE:	return { 1, 1 };
		case ToolShape::SMALL_BORE_GAUGE:	return { 1, 2 };
		case ToolShape::STRAIGHTEDGE:		return { 1, 5 };
		case ToolShape::BURETTE:			return { 1, 3 };
		case ToolShape::PROFILOMETER:		return { 2, 2 };
		case ToolShape::PLASTIGAUGE:		return { 1, 1 };
		case ToolShape::FEELERS:			return { 1, 1 };
		case ToolShape::DIAL_INDICATOR:		return { 2, 2 };
		case ToolShape::RUNOUT_GAUGE:		return { 2, 2 };
		case ToolShape::SPRING_TESTER:		return { 2, 3 };
		case ToolShape::HEIGHT_MICROMETER:	return { 1, 2 };
	}
	return { 1, 1 };
}

inline std::string ToolModelName(const MeasuringTool& tool)
{
	return "Tool: " + tool.name;
}
