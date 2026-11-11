#pragma once

#include <optional>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

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
	TORQUE_WRENCH,
	MANUAL,			// the factory service manual
	STEEL_SQUARE,
	ROD_ALIGNER,
	CORDLESS_RATCHET,
	IMPACT,
	BATTERY,
	CHARGER,
	WRENCH,			// one combination wrench
	MARKER,
	SPRAY_CAN,
	RAG,			// the rag and thinner
	PIN_PRESS,		// the piston pin press
	RING_COMPRESSOR,
	FUNNEL,
	BLEEDER,
	SPRING_COMPRESSOR,
	TIRE_IRONS,
	TIMING_LIGHT,
	TIRE_GAUGE,
	ALIGNMENT_GAUGE,
	SCREWDRIVER,
	PICKAXE
};

inline ToolShape ToolShapeFor(const MeasuringTool& tool)
{
	if (tool.manual)
		return ToolShape::MANUAL;
	switch (tool.handKind)
	{
		case HandToolKind::RATCHET:			return tool.powered ? ToolShape::CORDLESS_RATCHET : ToolShape::RATCHET;
		case HandToolKind::IMPACT:			return ToolShape::IMPACT;
		case HandToolKind::BATTERY:			return ToolShape::BATTERY;
		case HandToolKind::CHARGER:			return ToolShape::CHARGER;
		case HandToolKind::COMBINATION_WRENCH:	return ToolShape::WRENCH;
		case HandToolKind::MARKER:			return ToolShape::MARKER;
		case HandToolKind::SPRAY_CAN:		return ToolShape::SPRAY_CAN;
		case HandToolKind::THINNER:			return ToolShape::RAG;
		case HandToolKind::PIN_PRESS:		return ToolShape::PIN_PRESS;
		case HandToolKind::RING_COMPRESSOR:	return ToolShape::RING_COMPRESSOR;
		case HandToolKind::FUNNEL:			return ToolShape::FUNNEL;
		case HandToolKind::BRAKE_BLEEDER:		return ToolShape::BLEEDER;
		case HandToolKind::SPRING_COMPRESSOR:	return ToolShape::SPRING_COMPRESSOR;
		case HandToolKind::TIRE_IRONS:		return ToolShape::TIRE_IRONS;
		case HandToolKind::TIMING_LIGHT:		return ToolShape::TIMING_LIGHT;
		case HandToolKind::TIRE_GAUGE:		return ToolShape::TIRE_GAUGE;
		case HandToolKind::ALIGNMENT_GAUGE:	return ToolShape::ALIGNMENT_GAUGE;
		case HandToolKind::SCREWDRIVER:		return ToolShape::SCREWDRIVER;
		case HandToolKind::PICKAXE:		return ToolShape::PICKAXE;
		case HandToolKind::BREAKER_BAR:		return ToolShape::BREAKER_BAR;
		case HandToolKind::SOCKETS:			return ToolShape::SOCKET_SET;
		case HandToolKind::WRENCHES:		return ToolShape::WRENCH_SET;
		case HandToolKind::TORQUE_WRENCH:	return ToolShape::TORQUE_WRENCH;
		default:							break;
	}
	const std::string& name = tool.name;
	auto has = [&](const char* text) { return name.find(text) != std::string::npos; };
	if (has("Caliper")) return ToolShape::CALIPER;
	if (has("Steel Square")) return ToolShape::STEEL_SQUARE;
	if (has("Rod Aligner")) return ToolShape::ROD_ALIGNER;
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
		case ToolShape::MANUAL:			return { 2, 3 };
		case ToolShape::STEEL_SQUARE:		return { 2, 2 };
		case ToolShape::ROD_ALIGNER:		return { 2, 3 };
		case ToolShape::CORDLESS_RATCHET:	return { 1, 4 };
		case ToolShape::IMPACT:			return { 2, 2 };
		case ToolShape::BATTERY:			return { 1, 1 };
		case ToolShape::CHARGER:			return { 2, 1 };
		case ToolShape::WRENCH:			return { 1, 2 };
		case ToolShape::MARKER:			return { 1, 1 };
		case ToolShape::SPRAY_CAN:		return { 1, 2 };
		case ToolShape::RAG:			return { 1, 1 };
		case ToolShape::PIN_PRESS:		return { 2, 2 };
		case ToolShape::RING_COMPRESSOR:	return { 2, 1 };
		case ToolShape::FUNNEL:			return { 2, 2 };
		case ToolShape::BLEEDER:			return { 1, 2 };
		case ToolShape::SPRING_COMPRESSOR:	return { 1, 3 };
		case ToolShape::TIRE_IRONS:		return { 1, 4 };
		case ToolShape::TIMING_LIGHT:		return { 2, 2 };
		case ToolShape::TIRE_GAUGE:		return { 1, 2 };
		case ToolShape::ALIGNMENT_GAUGE:	return { 2, 2 };
		case ToolShape::SCREWDRIVER:		return { 1, 2 };
		case ToolShape::PICKAXE:		return { 1, 3 };
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

// The handle's length along X for a ratchet (hand or cordless), breaker bar or torque wrench.
inline float DriveToolLength(const MeasuringTool& tool)
{
	if (tool.handKind == HandToolKind::RATCHET)
		return tool.powered ? 0.28f : 0.2f;
	return tool.handKind == HandToolKind::BREAKER_BAR ? 0.45f : 0.42f;
}

// A cordless impact wrench lies on its side: the nose along +X, the pistol grip down +Z from the body.
constexpr glm::vec3 IMPACT_NOSE = { 0.105f, 0.03f, -0.04f };	// the end of the anvil
constexpr glm::vec3 IMPACT_GRIP = { -0.025f, 0.03f, 0.045f };	// the middle of the grip
constexpr float IMPACT_GRIP_END = 0.095f;						// where the battery clips on, along Z

// The middle of a socket on a drive tool's square drive, in the tool's own space, and how it's turned
// there (a socket model stands along Y): under the head of a ratchet, breaker bar or torque wrench, out
// in front of an impact's nose.
inline glm::vec3 DriveSocketPosition(const MeasuringTool& tool)
{
	if (tool.handKind == HandToolKind::IMPACT)
		return IMPACT_NOSE + glm::vec3(SOCKET_HEIGHT * 0.5f, 0.f, 0.f);
	return { DriveToolLength(tool) * 0.5f, -0.008f - SOCKET_HEIGHT * 0.5f, 0.f };
}
inline glm::quat DriveSocketTurn(const MeasuringTool& tool)
{
	if (tool.handKind == HandToolKind::IMPACT)
		return glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));
	return glm::quat(1.f, 0.f, 0.f, 0.f);
}

inline bool IsDriveTool(const MeasuringTool& tool)
{
	return tool.handKind == HandToolKind::RATCHET || tool.handKind == HandToolKind::BREAKER_BAR || tool.handKind == HandToolKind::TORQUE_WRENCH
		|| tool.handKind == HandToolKind::IMPACT;
}

// A battery pack lies flat, its bottom at the origin, 0.11 m along X and 0.075 m across: the bigger it
// is, the taller. The rail it clips on by is on top.
constexpr float BATTERY_LENGTH = 0.11f;
constexpr float BATTERY_WIDTH = 0.075f;
constexpr float BATTERY_RAIL = 0.008f;
inline float BatteryBodyHeight(const MeasuringTool& battery)
{
	return 0.04f + 0.006f * static_cast<float>(battery.capacityAh);
}
inline float BatteryHeight(const MeasuringTool& battery)
{
	return BatteryBodyHeight(battery) + BATTERY_RAIL;
}

// A charger: a cradle 0.04 m tall that a pack sits in upside down.
constexpr float CHARGER_HEIGHT = 0.04f;

// Where a battery pack goes on a cordless tool or a charger, in that one's own space: the pack's
// origin, and how it's turned so its rail meets the tool's grip (or the charger's cradle). Nothing for
// anything else.
struct BatteryMount
{
	glm::vec3 position = {};
	glm::quat turn = glm::quat(1.f, 0.f, 0.f, 0.f);
};
inline std::optional<BatteryMount> BatteryMountOn(const MeasuringTool& host, const MeasuringTool& battery)
{
	// The pack's X, Y and Z, as they end up on the host.
	auto turn = [](const glm::vec3& x, const glm::vec3& y, const glm::vec3& z) { return glm::quat_cast(glm::mat3(x, y, z)); };
	float height = BatteryHeight(battery);
	if (host.handKind == HandToolKind::IMPACT)
		// Under the grip, the rail up into it.
		return BatteryMount{ { IMPACT_GRIP.x, IMPACT_GRIP.y, IMPACT_GRIP_END + height }, turn({ 1.f, 0.f, 0.f }, { 0.f, 0.f, -1.f }, { 0.f, 1.f, 0.f }) };
	if (host.powered)
		// On the end of the handle.
		return BatteryMount{ { -DriveToolLength(host) * 0.5f - height, 0.02f, 0.f }, turn({ 0.f, 0.f, 1.f }, { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }) };
	if (host.handKind == HandToolKind::CHARGER)
		// Upside down in the cradle.
		return BatteryMount{ { 0.f, CHARGER_HEIGHT + height, 0.f }, turn({ 1.f, 0.f, 0.f }, { 0.f, -1.f, 0.f }, { 0.f, 0.f, -1.f }) };
	return std::nullopt;
}

// A combination wrench lies flat along X, its ring end at +X and its open end at -X: longer for the
// bigger sizes.
inline float WrenchLength(int sizeMm)
{
	return 0.09f + sizeMm * 0.006f;
}

// A wrench's ring, outside.
inline float WrenchRingRadius(int sizeMm)
{
	return 0.004f + sizeMm * 0.0006f;
}

// Where each wrench lies in its set's tray (0.3 m wide, 0.22 m deep), in the tray's own space: front to
// back, side by side, smallest at the left, the rings at alternate ends so they fit.
struct WrenchPlace
{
	glm::vec3 position = {};
	glm::quat turn = glm::quat(1.f, 0.f, 0.f, 0.f);
};
inline WrenchPlace WrenchSlot(const MeasuringTool& set, size_t index)
{
	int count = static_cast<int>(set.sizes.size());
	float x = -0.12f + index * 0.24f / (count > 1 ? count - 1 : 1);
	float yaw = (index % 2 == 0 ? 1.f : -1.f) * glm::half_pi<float>();
	return { { x, 0.016f, 0.f }, glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f)) };
}
