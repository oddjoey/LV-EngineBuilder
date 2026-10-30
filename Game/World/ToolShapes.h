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
	HEIGHT_MICROMETER,
	RATCHET,
	BREAKER_BAR,
	SOCKET_SET,
	WRENCH_SET,
	TORQUE_WRENCH
};

inline ToolShape ToolShapeFor(const MeasuringTool& tool)
{
	switch (tool.handKind)
	{
		case HandToolKind::RATCHET:			return ToolShape::RATCHET;
		case HandToolKind::BREAKER_BAR:		return ToolShape::BREAKER_BAR;
		case HandToolKind::SOCKETS:			return ToolShape::SOCKET_SET;
		case HandToolKind::WRENCHES:		return ToolShape::WRENCH_SET;
		case HandToolKind::TORQUE_WRENCH:	return ToolShape::TORQUE_WRENCH;
		default:							break;
	}
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
		case ToolShape::RATCHET:			return { 1, 3 };
		case ToolShape::BREAKER_BAR:		return { 1, 5 };
		case ToolShape::SOCKET_SET:			return { 3, 2 };
		case ToolShape::WRENCH_SET:			return { 3, 2 };
		case ToolShape::TORQUE_WRENCH:		return { 1, 5 };
	}
	return { 1, 1 };
}

inline std::string ToolModelName(const MeasuringTool& tool)
{
	return "Tool: " + tool.name;
}

// Sockets are separate from their set, so one can come out and go on a ratchet. The set's model is
// just the open case; each socket is the one socket model, scaled to its size.
inline const std::string SOCKET_MODEL = "Tool: Socket";
constexpr float SOCKET_MODEL_RADIUS = 0.01f;
constexpr float SOCKET_HEIGHT = 0.03f;

inline float SocketRadius(int sizeMm)
{
	return 0.0035f + sizeMm * 0.0004f;
}

// The middle of a socket standing in its place in the set, in the set's own space.
inline glm::vec3 SocketSlot(const MeasuringTool& set, size_t index)
{
	int count = static_cast<int>(set.sizes.size());
	return { -0.11f + index * 0.22f / (count > 1 ? count - 1 : 1), 0.016f + SOCKET_HEIGHT * 0.5f, 0.f };
}

// The handle's length along X for a ratchet, breaker bar or torque wrench.
inline float DriveToolLength(const MeasuringTool& tool)
{
	return tool.handKind == HandToolKind::RATCHET ? 0.2f : tool.handKind == HandToolKind::BREAKER_BAR ? 0.45f : 0.42f;
}

// The middle of a socket on a drive tool's square drive, under its head, in the tool's own space.
inline glm::vec3 DriveSocketPosition(const MeasuringTool& tool)
{
	return { DriveToolLength(tool) * 0.5f, -0.008f - SOCKET_HEIGHT * 0.5f, 0.f };
}

inline bool IsDriveTool(const MeasuringTool& tool)
{
	return tool.handKind == HandToolKind::RATCHET || tool.handKind == HandToolKind::BREAKER_BAR || tool.handKind == HandToolKind::TORQUE_WRENCH;
}
