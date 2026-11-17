// The project car's wiring in the 3D shop: the plugs on its harness, taken in hand and pushed onto what they feed.
// And its plumbing, done the same way: the unions on its brake pipes and its clutch's pipe, and its fuel hoses' ends.
#include "ShopWorld.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <utility>

#include "LV/Engine/Objects/ObjectManager.h"
#include "LV/Engine/Utility/InputManager.h"
#include "../Audio/Sounds.h"
#include "../Controls.h"
#include "CarBody.h"
#include "ShopWorldShared.h"

using namespace ShopLayout;

namespace
{
	constexpr float PLUG_REACH = 2.6f;			// how much wire a plug has, from its loom round its clips to the plug
	constexpr float GROUND_REACH = 1.1f; 		// and how long the battery's ground strap is
	constexpr float AIM_SIZE = 0.065f;			// how near the crosshair a plug or a socket has to be to be meant
	constexpr float MARKS_WITHIN = 2.f;			// how near the engine the eye has to be for the looms' and line sets' marks to show

	// Where a plug's wire leaves its loom, in the car's space. The harness comes through the firewall in the bay's back
	// corner on the driver's side, above the clutch's cylinder, and all ten of its tails come off it there, side by side.
	glm::vec3 Anchor(CarPlug plug)
	{
		int index = static_cast<int>(plug);
		// The ground strap: off the battery's negative post (the one toward the nose).
		if (plug == CarPlug::GROUND)
			return { 1.74f, 0.735f, -0.5f };
		const glm::vec3 block = { CarBody::BRAKE_BLOCK[0], CarBody::BRAKE_BLOCK[1], CarBody::BRAKE_BLOCK[2] };
		const glm::vec3 filter = { CarBody::FUEL_FILTER[0], CarBody::FUEL_FILTER[1], CarBody::FUEL_FILTER[2] };
		switch (plug)
		{
			// The brake pipes: all five off the junction block under the master cylinder, side by side.
			case CarPlug::BRAKE_FRONT_PORT:
			case CarPlug::BRAKE_REAR_PORT:
			case CarPlug::BRAKE_HOSE_LEFT:
			case CarPlug::BRAKE_HOSE_RIGHT:
			case CarPlug::BRAKE_HOSE_REAR:
				return block + glm::vec3(-0.03f + 0.015f * static_cast<float>(index - static_cast<int>(CarPlug::BRAKE_FRONT_PORT)), 0.014f, 0.f);
			// The clutch's pipe: out of the underside of its master cylinder's nose.
			case CarPlug::CLUTCH_PIPE:
				return glm::vec3(CarBody::CLUTCH_MASTER[0], CarBody::CLUTCH_MASTER[1], CarBody::CLUTCH_MASTER[2]) + glm::vec3(0.05f, -0.03f, 0.f);
			// The fuel line's back end under the floor, short of the tank; its front end, up through the wheel housing
			// under the filter; and the hose on to the pump, off the top of the filter.
			case CarPlug::FUEL_TANK:	return { -0.62f, 0.17f, 0.35f };
			case CarPlug::FUEL_FILTER:	return { CarBody::FUEL_RISER[0], CarBody::FUEL_RISER[1], CarBody::FUEL_RISER[2] };
			case CarPlug::FUEL_PUMP:	return filter + glm::vec3(0.f, CarBody::FUEL_FILTER_HEIGHT, 0.f);
			default:					break;
		}
		return { 0.905f, 0.79f, -0.66f + 0.04f * static_cast<float>(index) };
	}

	// Where it hangs when it's on nothing: off its loom, down the firewall.
	glm::vec3 Hanging(CarPlug plug)
	{
		// (The ground strap lies off the battery's top, toward the engine.)
		if (plug == CarPlug::GROUND)
			return Anchor(plug) + glm::vec3(-0.02f, -0.05f, 0.13f);
		if (plug == CarPlug::CLUTCH_PIPE)
			return Anchor(plug) + glm::vec3(0.05f, -0.13f, 0.04f);
		return Anchor(plug) + glm::vec3(0.055f, -0.13f, 0.f);
	}

	// How much there is of each: the wires' the same, the pipes' and hoses' by how far each has to go.
	float Reach(CarPlug plug)
	{
		switch (plug)
		{
			case CarPlug::GROUND:			return GROUND_REACH;
			case CarPlug::BRAKE_FRONT_PORT:
			case CarPlug::BRAKE_REAR_PORT:
			case CarPlug::BRAKE_HOSE_LEFT:	return 1.6f;
			case CarPlug::BRAKE_HOSE_RIGHT:	return 2.8f;
			case CarPlug::BRAKE_HOSE_REAR:	return 5.f;
			case CarPlug::CLUTCH_PIPE:		return 2.4f;
			case CarPlug::FUEL_TANK:		return 4.f;
			case CarPlug::FUEL_FILTER:		return 1.4f;
			case CarPlug::FUEL_PUMP:		return 1.8f;
			default:						return PLUG_REACH;
		}
	}

	// A loom's plug that's on nothing and led nowhere is tucked away in its loom: it isn't seen, and is picked from the
	// loom's menu. The same for a line set's pipes. (The ground strap is no loom's: it lies by the battery; and the
	// clutch's one pipe hangs off its master cylinder.)
	bool Tucked(const CarAssembly& car, CarPlug plug)
	{
		return CarAssembly::HasPlugMenu(CarAssembly::PlugHarness(plug)) && !car.IsPlugged(plug) && car.GetWireClips(plug).empty();
	}

	// Where the harness's plugs are picked from: the middle of its tails, in the bay's back corner on the driver's side.
	// The brake pipes' from their junction block, the fuel hoses' from where the line comes up under the filter.
	glm::vec3 LoomPoint(CarSlot harness)
	{
		if (harness == CarSlot::BRAKE_LINE_SET)
			return { CarBody::BRAKE_BLOCK[0], CarBody::BRAKE_BLOCK[1], CarBody::BRAKE_BLOCK[2] };
		if (harness == CarSlot::FUEL_LINE_SET)
			return { CarBody::FUEL_RISER[0], CarBody::FUEL_RISER[1], CarBody::FUEL_RISER[2] };
		return (Anchor(CarPlug::BATTERY) + Anchor(CarPlug::OIL_SENDER)) * 0.5f;
	}
	const CarSlot MENU_HARNESSES[] = { CarSlot::MAIN_HARNESS, CarSlot::BRAKE_LINE_SET, CarSlot::FUEL_LINE_SET };

	// Which way a pipe or a hose is going as it goes into what it's done up on, in the car's space (a wire's plug
	// goes on whichever way it comes).
	glm::vec3 Entry(CarPlug plug)
	{
		switch (plug)
		{
			case CarPlug::BRAKE_FRONT_PORT:
			case CarPlug::BRAKE_REAR_PORT:
			case CarPlug::BRAKE_HOSE_LEFT:	return { 0.f, 0.f, -1.f };
			case CarPlug::BRAKE_HOSE_RIGHT:	return { 0.f, 0.f, 1.f };
			case CarPlug::BRAKE_HOSE_REAR:
			case CarPlug::FUEL_TANK:		return { -1.f, 0.f, 0.f };
			case CarPlug::FUEL_FILTER:		return { 0.f, 1.f, 0.f };
			case CarPlug::FUEL_PUMP:		return { 1.f, 0.f, 0.f };
			default:						return { 0.f, -1.f, 0.f };
		}
	}

	// The way a pipe lies from where it starts to what it's on, when it's been led nowhere in particular: the points it
	// turns at on its way (before the last straight into its union).
	std::vector<glm::vec3> Route(CarPlug plug, const glm::vec3& from, const glm::vec3& socket)
	{
		switch (plug)
		{
			// Inboard under the master cylinder, and up to its ports.
			case CarPlug::BRAKE_FRONT_PORT:
			case CarPlug::BRAKE_REAR_PORT:	return { { socket.x, 0.56f, -0.222f } };
			case CarPlug::BRAKE_HOSE_LEFT:	return { { socket.x, 0.56f, -0.33f } };
			// Back to the firewall, across it, and forward along the far wheel housing.
			case CarPlug::BRAKE_HOSE_RIGHT:	return { { 0.905f, 0.5f, -0.36f }, { 0.905f, 0.5f, 0.33f }, { socket.x, 0.56f, 0.33f } };
			// Down the firewall and back along the floor.
			case CarPlug::BRAKE_HOSE_REAR:	return { { 0.905f, 0.45f, -0.36f }, { 0.87f, 0.17f, -0.3f }, { -0.85f, 0.17f, -0.3f } };
			// Back to the firewall and across it, over the gearbox.
			case CarPlug::CLUTCH_PIPE:		return { { 0.905f, 0.56f, -0.5f }, { 0.905f, 0.56f, socket.z } };
			case CarPlug::FUEL_TANK:		return { { -1.3f, 0.17f, 0.35f } };
			case CarPlug::FUEL_FILTER:		return {};
			// Up off the filter, across the firewall, and forward to the pump on the side of the head.
			default:						return { { from.x, from.y + 0.035f, from.z - 0.03f }, { 0.925f, from.y + 0.035f, socket.z }, { 0.95f, socket.y, socket.z } };
		}
	}

	// What the thing in hand, and the run behind it, are called.
	const char* RunName(CarPlug plug)
	{
		switch (CarPlugKindOf(plug))
		{
			case CarPlugKind::STRAP:	return "strap";
			case CarPlugKind::PIPE:		return "pipe";
			case CarPlugKind::HOSE:		return "hose";
			default:					return "wire";
		}
	}
	bool Plumbing(CarPlug plug) { return CarPlugKindOf(plug) == CarPlugKind::PIPE || CarPlugKindOf(plug) == CarPlugKind::HOSE; }

	// The menu's keys: 1 to 9, then 0 for a tenth.
	int MenuKey(int i) { return i < 9 ? GLFW_KEY_1 + i : GLFW_KEY_0; }

	std::vector<CarPlug> PlugsOn(CarSlot harness)
	{
		std::vector<CarPlug> plugs;
		for (int p = 0; p < CAR_PLUGS; p++)
			if (CarAssembly::PlugHarness(static_cast<CarPlug>(p)) == harness)
				plugs.push_back(static_cast<CarPlug>(p));
		return plugs;
	}

	// What a plug, and what it goes onto, are called.
	std::string PlugLabel(CarPlug plug)
	{
		if (plug == CarPlug::CLUTCH_PIPE)
			return "clutch's pipe";
		if (CarPlugKindOf(plug) == CarPlugKind::PIPE)
			return std::format("brake pipe for the {}", CarPlugName(plug));
		if (CarPlugKindOf(plug) == CarPlugKind::HOSE)
			return std::format("fuel hose for the {}", CarPlugName(plug));
		return plug == CarPlug::GROUND ? "battery's ground strap" : plug == CarPlug::BATTERY ? "harness's battery lead" : std::format("{}'s plug", CarPlugName(plug));
	}
	std::string SocketLabel(CarPlug plug)
	{
		if (Plumbing(plug))
			return std::format("the {}", CarPlugName(plug));
		return plug == CarPlug::GROUND ? "the ground stud on the body" : plug == CarPlug::BATTERY ? "the battery's positive post"
			: std::format("the {}'s socket", CarPlugName(plug));
	}
	// What a part's plugs are, in its list: "plug", "pipe" or "hose".
	const char* MenuNoun(CarSlot harness) { return harness == CarSlot::BRAKE_LINE_SET ? "pipe" : harness == CarSlot::FUEL_LINE_SET ? "hose" : "plug"; }
	const char* MenuPullAll(CarSlot harness)
	{
		return harness == CarSlot::BRAKE_LINE_SET ? "Undo every union" : harness == CarSlot::FUEL_LINE_SET ? "Pull every hose off" : "Pull every plug off";
	}

	glm::vec3 ToVec(const CarWirePoint& point) { return { point[0], point[1], point[2] }; }

	// Turned so its own +x lies along a direction.
	glm::quat Along(const glm::vec3& direction)
	{
		glm::vec3 x = glm::normalize(direction);
		glm::vec3 up = std::abs(x.y) > 0.98f ? glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 1.f, 0.f);
		glm::vec3 z = glm::normalize(glm::cross(x, up));
		glm::vec3 y = glm::cross(z, x);
		return glm::quat_cast(glm::mat3(x, y, z));
	}
}

bool ShopWorld::PlugsReachable(const Workshop& workshop) const
{
	const auto* build = workshop.GetCar();
	if (!build || m_carStation >= m_stations.size() || !m_stations[m_carStation].present || IsSeated())
		return false;
	// They're all under the hood.
	return !build->assembly.IsFilled(CarSlot::HOOD, 0) || CarPanelOpen(CarPanel::HOOD) > 0.6f;
}

glm::vec3 ShopWorld::PlugSocket(const Workshop& workshop, CarPlug plug) const
{
	using namespace CarBody;
	// On a part of the car: off the middle of where it sits.
	auto onCar = [&](CarSlot slot, int index, const glm::vec3& from) {
		auto pose = PoseOnCar(slot, index);
		const auto& bounds = PartBounds(CarAssembly::SlotPartType(slot));
		return pose.offset + pose.rotation * ((bounds.first + bounds.second) * 0.5f) + from;
	};
	// On a part of the engine in the bay: off its middle, wherever the engine has it (or, with that part off, about
	// where it would be).
	auto onEngine = [&](AssemblySlot slot, const glm::vec3& from, const glm::vec3& otherwise) {
		int engine = workshop.GetEngineInCar();
		if (engine < 0 || m_carStation >= m_stations.size() || !workshop.GetAssembly(engine).Get(slot, 0))
			return otherwise;
		return glm::inverse(CarRotation()) * (SlotWorldCenter(engine, slot, 0) - m_stations[m_carStation].position) + from;
	};
	const glm::vec3 master = PoseOnCar(CarSlot::BRAKE_MASTER_CYLINDER, 0).offset, slave = PoseOnCar(CarSlot::CLUTCH_RELEASE_CYLINDER, 0).offset;
	switch (plug)
	{
		// The master cylinder's two outlets on its inboard side; a bracket on each front wheel housing's wall, where the
		// hose comes through from the wheel; the front end of the hose down to the back axle; the union on top of the
		// clutch's release cylinder; the tank's outlet on its front; the filter's stub underneath; and the pump's inlet.
		case CarPlug::BRAKE_FRONT_PORT:	return master + glm::vec3(0.035f, 0.017f, 0.038f);
		case CarPlug::BRAKE_REAR_PORT:	return master + glm::vec3(-0.035f, 0.017f, 0.038f);
		case CarPlug::BRAKE_HOSE_LEFT:	return { FRONT_AXLE_X + 0.1f, 0.6f, -0.395f };
		case CarPlug::BRAKE_HOSE_RIGHT:	return { FRONT_AXLE_X + 0.1f, 0.6f, 0.395f };
		case CarPlug::BRAKE_HOSE_REAR:	return { REAR_AXLE_X + 0.17f, 0.33f, 0.12f };
		case CarPlug::CLUTCH_PIPE:		return slave + glm::vec3(-0.012f, 0.029f, 0.f);
		case CarPlug::FUEL_TANK:		return { -1.42f, 0.42f, 0.3f };
		case CarPlug::FUEL_FILTER:		return { FUEL_FILTER[0], FUEL_FILTER[1], FUEL_FILTER[2] };
		case CarPlug::FUEL_PUMP:
		{
			// The back end of the pipe across the pump's top, on the side of the head.
			int engine = workshop.GetEngineInCar();
			if (engine < 0 || m_carStation >= m_stations.size() || !workshop.GetAssembly(engine).Get(AssemblySlot::FUEL_PUMP, 0))
				return { ENGINE_X - 0.2f, ENGINE_DECK_Y + 0.1f, -0.13f };
			auto pose = PoseOnStand(engine, AssemblySlot::FUEL_PUMP, 0);
			glm::vec3 inlet = StandToWorld(engine, pose.offset + pose.rotation * (glm::vec3(0.036f, 0.022f, 0.f) * pose.scale));
			return glm::inverse(CarRotation()) * (inlet - m_stations[m_carStation].position);
		}
		// The battery's positive post; the back of each headlight's well; the fan's motor.
		// (Both looms' leads go on the positive post: the main one's on its outer side, the engine's on the side toward the engine.)
		case CarPlug::BATTERY:			return onCar(CarSlot::BATTERY, 0, { -0.08f, 0.115f, 0.f });
		case CarPlug::HEADLIGHT_LEFT:	return { NOSE_X - LAMP_LID_LENGTH - 0.012f, 0.6f, -0.53f };
		case CarPlug::HEADLIGHT_RIGHT:	return { NOSE_X - LAMP_LID_LENGTH - 0.012f, 0.6f, 0.53f };
		case CarPlug::COOLING_FAN:		return { 1.885f, 0.635f, 0.12f };
		// The ground strap: wherever on the body it's been bolted.
		case CarPlug::GROUND:			return workshop.GetCar() ? ToVec(workshop.GetCar()->assembly.GetGroundPoint()) : ToVec(CarAssembly::DEFAULT_GROUND_POINT);
		// The starter's solenoid, the top of the coil, and on the engine the distributor's body, the back of the
		// alternator, the sender in the water outlet and the one in the block over the oil filter.
		case CarPlug::STARTER:			return onCar(CarSlot::STARTER, 0, { 0.f, 0.075f, 0.f });
		case CarPlug::IGNITION_COIL:	return onCar(CarSlot::IGNITION_COIL, 0, { 0.042f, 0.052f, 0.f });	// by its + terminal, toward the nose
		case CarPlug::DISTRIBUTOR:		return onEngine(AssemblySlot::DISTRIBUTOR, { 0.f, 0.045f, 0.f }, { ENGINE_X - 0.24f, ENGINE_DECK_Y + 0.12f, 0.06f });
		case CarPlug::ALTERNATOR:		return onEngine(AssemblySlot::ALTERNATOR, { 0.f, 0.075f, 0.f }, { ENGINE_X + 0.2f, ENGINE_DECK_Y - 0.05f, -0.2f });
		case CarPlug::COOLANT_SENDER:	return onEngine(AssemblySlot::THERMOSTAT_HOUSING, { 0.f, 0.045f, 0.f }, { ENGINE_X + 0.22f, ENGINE_DECK_Y + 0.1f, -0.08f });
		default:						return onEngine(AssemblySlot::OIL_FILTER, { 0.f, 0.075f, 0.f }, { ENGINE_X, ENGINE_DECK_Y - 0.2f, 0.14f });
	}
}

glm::vec3 ShopWorld::PlugRest(const Workshop& workshop, CarPlug plug) const
{
	const auto* build = workshop.GetCar();
	if (build)
		if (const auto& clips = build->assembly.GetWireClips(plug); !clips.empty())
			return ToVec(clips.back()) + glm::vec3(0.f, -0.1f, 0.f);
	return Hanging(plug);
}

std::optional<ShopWorld::CarAim> ShopWorld::AimAtCar(const FirstPersonController& player, float reach) const
{
	return AimAtCar(player.GetEyePosition(), player.GetForward(), reach);
}

std::optional<ShopWorld::CarAim> ShopWorld::AimAtCar(const glm::vec3& eye, const glm::vec3& direction, float reach) const
{
	if (m_carStation >= m_stations.size() || !m_stations[m_carStation].present)
		return std::nullopt;
	auto hit = ObjectManager::Get()->Raycast(eye, direction, reach, [&](const pObject& object) { return m_targets.contains(object.get()); }, true);
	if (!hit)
		return std::nullopt;
	return CarAim{ eye + direction * hit->distance, hit->distance, hit->object == m_stations[m_carStation].object };
}

void ShopWorld::DropHeldPlug(Workshop& workshop)
{
	if (m_heldPlug < 0)
		return;
	workshop.UnclipLooseCarWire(static_cast<CarPlug>(m_heldPlug));
	m_heldPlug = -1;
	Cue(Sound::SET_DOWN, GetCarPosition(), 0.5f);
}

void ShopWorld::TakePlug(Workshop& workshop, CarPlug plug)
{
	const auto* build = workshop.GetCar();
	if (!build || !build->assembly.IsFilled(CarAssembly::PlugHarness(plug), 0))
		return;
	if (build->assembly.IsPlugged(plug))
	{
		workshop.DisconnectCarPlug(plug);
		// Pulled off with the engine running: without its spark, or its fuel, it stops.
		if (m_drive.active && (plug == CarPlug::IGNITION_COIL || plug == CarPlug::DISTRIBUTOR || CarPlugKindOf(plug) == CarPlugKind::HOSE))
		{
			Toast(CarPlugKindOf(plug) == CarPlugKind::HOSE ? "It dies: no fuel." : "It dies: no spark.");
			EndDrive();
		}
	}
	m_plugMenu.reset();
	m_heldPlug = static_cast<int>(plug);
	Cue(Sound::PICK_UP, CarToWorld(Anchor(plug)), 0.6f);
}

bool ShopWorld::UpdatePlugMenu(Workshop& workshop, const FirstPersonController& player, const InputManager& input)
{
	if (!m_plugMenu)
		return false;
	const auto* build = workshop.GetCar();
	// Walked off, the hood shut, the loom gone or something taken in hand: it shuts.
	if (!build || !PlugsReachable(workshop) || GetHeld() || !build->assembly.IsFilled(m_plugMenu->harness, 0)
		|| glm::distance(player.GetEyePosition(), CarToWorld(LoomPoint(m_plugMenu->harness))) > INTERACT_DISTANCE + 0.5f)
	{
		m_plugMenu.reset();
		return false;
	}
	const auto plugs = PlugsOn(m_plugMenu->harness);
	const int count = static_cast<int>(plugs.size());
	// Its last row, after the plugs, pulls every one of them off.
	const int rows = count + 1;
	m_fitPoints.push_back({ CarToWorld(LoomPoint(m_plugMenu->harness)), true, true });
	m_prompt = std::format("[1]-[{}] Take that {}   [Backspace] {}   [Wheel] then [LMB] Pick   [RMB] Never mind", count >= 10 ? 0 : count,
		MenuNoun(m_plugMenu->harness), MenuPullAll(m_plugMenu->harness));
	// Its number takes a plug straight off; the wheel steps through the rows and a click does the one it's on.
	std::optional<int> chosen;
	for (int i = 0; i < count; i++)
		if (i < 10 && input.WasKeyPressed(MenuKey(i)))
			chosen = i;
	if (input.WasKeyPressed(GLFW_KEY_BACKSPACE))
		chosen = count;
	if (float scroll = input.GetScrollDelta(); std::abs(scroll) >= 0.5f)
		m_plugMenu->selected = (m_plugMenu->selected + (scroll > 0.f ? rows - 1 : 1)) % rows;
	if (!chosen && (Controls::Pressed(input, Control::PRIMARY) || Controls::Pressed(input, Control::USE) || std::exchange(m_devPress, false)))
		chosen = m_plugMenu->selected;
	if (chosen && *chosen == count)
	{
		// Every one pulled, its wire unclipped and back in the loom; the list stays open on them.
		const CarSlot harness = m_plugMenu->harness;
		bool dies = m_drive.active && harness != CarSlot::BRAKE_LINE_SET;
		int pulled = workshop.DisconnectCarPlugs(harness);
		if (pulled > 0)
		{
			Cue(Sound::CLICK, CarToWorld(LoomPoint(harness)), 0.7f);
			if (dies)
			{
				Toast(harness == CarSlot::FUEL_LINE_SET ? "It dies: no fuel." : "It dies: no spark.");
				EndDrive();
			}
			else
				Toast(std::format("{} {}{} {}.", pulled, MenuNoun(harness), pulled == 1 ? "" : "s", harness == CarSlot::BRAKE_LINE_SET ? "undone" : "pulled off"));
		}
		else
			Toast(harness == CarSlot::MAIN_HARNESS ? "None of them is plugged into anything." : "None of them is on anything.");
	}
	else if (chosen)
		TakePlug(workshop, plugs[*chosen]);
	else if (Controls::Pressed(input, Control::SET_DOWN) || Controls::Pressed(input, Control::DROP) || Controls::Pressed(input, Control::MOVE))
		m_plugMenu.reset();
	return true;
}

void ShopWorld::DrawPlugMenu(const Workshop& workshop) const
{
	const auto* build = workshop.GetCar();
	if (!m_plugMenu || !build)
		return;
	const auto& car = build->assembly;
	const auto plugs = PlugsOn(m_plugMenu->harness);
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	ImVec2 display = ImGui::GetIO().DisplaySize;
	const float line = ImGui::GetTextLineHeightWithSpacing() + 4.f;
	const std::string title = m_plugMenu->harness == CarSlot::BRAKE_LINE_SET ? "Brake lines: which pipe?"
		: m_plugMenu->harness == CarSlot::FUEL_LINE_SET ? "Fuel lines: which hose?" : "Harness: which plug?";
	std::vector<std::pair<std::string, std::string>> rows;
	float nameWidth = 0.f, stateWidth = 0.f;
	int connected = 0;
	for (int i = 0; i < static_cast<int>(plugs.size()); i++)
	{
		connected += car.IsPlugged(plugs[i]) ? 1 : 0;
		std::string name = CarPlugName(plugs[i]);
		name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
		size_t clips = car.GetWireClips(plugs[i]).size();
		std::string state = car.IsPlugged(plugs[i]) ? "connected" : !workshop.CarPlugSocketFitted(plugs[i]) ? "loose (nothing to put it on)" : "loose";
		if (clips > 0)
			state += std::format(", {} clip{}", clips, clips == 1 ? "" : "s");
		rows.push_back({ i < 10 ? std::format("{}   {}", (i + 1) % 10, name) : std::format("    {}", name), state });
		nameWidth = (std::max)(nameWidth, ImGui::CalcTextSize(rows.back().first.c_str()).x);
		stateWidth = (std::max)(stateWidth, ImGui::CalcTextSize(state.c_str()).x);
	}
	// And the button that pulls them all.
	rows.push_back({ std::format("Bksp  {}", MenuPullAll(m_plugMenu->harness)), connected > 0 ? std::format("{} connected", connected) : "none connected" });
	nameWidth = (std::max)(nameWidth, ImGui::CalcTextSize(rows.back().first.c_str()).x);
	stateWidth = (std::max)(stateWidth, ImGui::CalcTextSize(rows.back().second.c_str()).x);
	const float width = (std::max)(nameWidth + stateWidth + 60.f, ImGui::CalcTextSize(title.c_str()).x + 32.f);
	const float height = line * (rows.size() + 1) + 30.f;
	// Beside the crosshair, clear of what's being looked at.
	ImVec2 min = { display.x * 0.5f + 70.f, display.y * 0.5f - height * 0.5f };
	ImVec2 max = { min.x + width, min.y + height };
	drawList->AddRectFilled(min, max, IM_COL32(18, 20, 24, 215), 6.f);
	drawList->AddRect(min, max, IM_COL32(255, 210, 120, 150), 6.f);
	ShadowText(drawList, { min.x + 16.f, min.y + 10.f }, IM_COL32(255, 210, 120, 240), title, false);
	for (int i = 0; i < static_cast<int>(rows.size()); i++)
	{
		// (The pull-all row, last, set off a little from the plugs, in red.)
		const bool all = i == static_cast<int>(plugs.size());
		float y = min.y + 14.f + line * (i + 1) + (all ? 8.f : 0.f);
		bool picked = i == m_plugMenu->selected;
		if (all)
			drawList->AddLine({ min.x + 10.f, y - 6.f }, { max.x - 10.f, y - 6.f }, IM_COL32(255, 210, 120, 90));
		if (picked)
			drawList->AddRectFilled({ min.x + 6.f, y - 3.f }, { max.x - 6.f, y + line - 5.f }, all ? IM_COL32(255, 110, 90, 70) : IM_COL32(255, 210, 120, 60), 4.f);
		bool on = !all && car.IsPlugged(plugs[i]);
		if (all)
		{
			ShadowText(drawList, { min.x + 16.f, y }, picked ? IM_COL32(255, 200, 190, 255) : IM_COL32(255, 150, 130, 235), rows[i].first, false);
			ShadowText(drawList, { min.x + 16.f + nameWidth + 28.f, y }, IM_COL32(200, 200, 200, 210), rows[i].second, false);
			continue;
		}
		ShadowText(drawList, { min.x + 16.f, y }, picked ? IM_COL32(255, 255, 255, 255) : IM_COL32(225, 225, 220, 230), rows[i].first, false);
		ShadowText(drawList, { min.x + 16.f + nameWidth + 28.f, y }, on ? IM_COL32(130, 235, 150, 235) : IM_COL32(255, 170, 110, 235), rows[i].second, false);
	}
}

void ShopWorld::DevTakePlug(Workshop& workshop, int plug)
{
	if (plug >= 0 && plug < CAR_PLUGS)
		TakePlug(workshop, static_cast<CarPlug>(plug));
}

void ShopWorld::PoseCarPlugs(const Workshop& workshop, const FirstPersonController& player)
{
	const auto* build = workshop.GetCar();
	bool present = build && m_carStation < m_stations.size() && m_stations[m_carStation].present;
	// The harness in the bay, taped thick, along the firewall where its tails come off. (One trunk: the second isn't used.)
	bool harness = present && build->assembly.IsFilled(CarSlot::MAIN_HARNESS, 0);
	const glm::vec3 TRUNKS[1][2] = { { { 0.905f, 0.79f, -0.70f }, { 0.905f, 0.79f, -0.28f } } };
	if (m_loomTrunks[1])
		m_loomTrunks[1]->SetVisible(false);
	for (int t = 0; t < 1; t++)
	{
		const auto& trunk = m_loomTrunks[t];
		if (!trunk)
			continue;
		trunk->SetVisible(harness);
		if (!harness)
			continue;
		glm::vec3 from = CarToWorld(TRUNKS[t][0]), to = CarToWorld(TRUNKS[t][1]);
		trunk->SetPosition(from);
		trunk->SetRotation(Along(to - from));
		trunk->SetScale({ glm::length(to - from), 3.2f, 3.2f });
	}
	// The brake pipes' junction block on the wheel housing, and the fuel line's end up out of the other: each there
	// with its line set.
	for (int f = 0; f < 2; f++)
		if (const auto& fixture = m_lineFixtures[f])
		{
			bool there = present && build->assembly.IsFilled(f == 0 ? CarSlot::BRAKE_LINE_SET : CarSlot::FUEL_LINE_SET, 0);
			fixture->SetVisible(there);
			if (there)
			{
				fixture->SetPosition(CarToWorld(LoomPoint(f == 0 ? CarSlot::BRAKE_LINE_SET : CarSlot::FUEL_LINE_SET)));
				fixture->SetRotation(CarRotation());
			}
		}
	for (int p = 0; p < CAR_PLUGS; p++)
	{
		auto plug = static_cast<CarPlug>(p);
		auto& visual = m_plugVisuals[p];
		if (!visual.plug)
			continue;
		bool loom = present && build->assembly.IsFilled(CarAssembly::PlugHarness(plug), 0);
		// Its socket is there with whatever it's on.
		// (The ground strap's is the bolt that holds it to the body, wherever that's been put: there once it's on.)
		bool socket = present && (plug == CarPlug::GROUND ? build->assembly.IsPlugged(plug) : workshop.CarPlugSocketFitted(plug));
		glm::vec3 socketAt = present ? PlugSocket(workshop, plug) : glm::vec3(0.f);
		visual.socket->SetVisible(socket);
		if (socket)
		{
			visual.socket->SetPosition(CarToWorld(socketAt));
			// (The ground strap's bolt stands out of the panel it's in.)
			visual.socket->SetRotation(plug == CarPlug::GROUND ? Along(CarRotation() * ToVec(build->assembly.GetGroundNormal()))
				: Plumbing(plug) ? Along(CarRotation() * Entry(plug)) : CarRotation());
		}
		visual.plug->SetVisible(loom);
		// The clips its wire is led through, each where it was put.
		static const std::vector<CarWirePoint> NO_CLIPS;
		const auto& clips = loom ? build->assembly.GetWireClips(plug) : NO_CLIPS;
		for (int c = 0; c < CAR_WIRE_CLIPS; c++)
		{
			bool there = c < static_cast<int>(clips.size());
			visual.clips[c]->SetVisible(there);
			if (there)
			{
				visual.clips[c]->SetPosition(CarToWorld(ToVec(clips[c])));
				visual.clips[c]->SetRotation(CarRotation());
			}
		}
		if (!loom)
		{
			for (const auto& wire : visual.wires)
				wire->SetVisible(false);
			if (m_heldPlug == p)
				m_heldPlug = -1;
			continue;
		}
		// Tucked away in its loom, there's nothing of it to see.
		if (m_heldPlug != p && Tucked(build->assembly, plug))
		{
			visual.plug->SetVisible(false);
			for (const auto& wire : visual.wires)
				wire->SetVisible(false);
			continue;
		}
		// The way its wire runs, point to point, from the loom through its clips to wherever the plug is: in the hand,
		// pushed home, or hanging. (One with no clips, pushed home, lies the way it came: up and over to an engine's
		// part, or forward along the inner wing to what's in the nose.)
		glm::vec3 points[CAR_WIRE_CLIPS + 4];
		int count = 0;
		points[count++] = CarToWorld(Anchor(plug));
		for (const auto& clip : clips)
			points[count++] = CarToWorld(ToVec(clip));
		if (m_heldPlug == p)
		{
			m_heldPlugAt = player.GetEyePosition() + player.GetRotation() * glm::vec3(0.11f, -0.1f, -0.42f);
			const glm::vec3 last = points[count - 1];
			glm::vec3 middle = (last + m_heldPlugAt) * 0.5f;
			middle.y -= 0.12f * (std::max)(0.f, 1.f - glm::distance(last, m_heldPlugAt) / Reach(plug));
			points[count++] = middle;
			points[count++] = m_heldPlugAt;
		}
		else if (build->assembly.IsPlugged(plug))
		{
			glm::vec3 from = Anchor(plug);
			if (Plumbing(plug))
			{
				// The way it lies if it's been led nowhere, then squarely into what it's on.
				if (clips.empty())
					for (const auto& turn : Route(plug, from, socketAt))
						points[count++] = CarToWorld(turn);
				points[count++] = CarToWorld(socketAt - Entry(plug) * 0.07f);
			}
			else if (!clips.empty())
				;	// straight on from its last clip
			else if (!OnEngineBranch(plug) && plug != CarPlug::BATTERY && plug != CarPlug::GROUND)
			{
				// Out to the wing on its own side, and along under the fender's lip.
				float side = socketAt.z < 0.f ? -1.f : 1.f;
				if (side > 0.f)
				{
					points[count++] = CarToWorld({ from.x + 0.02f, from.y + 0.03f, 0.6f });
					points[count++] = CarToWorld({ socketAt.x - 0.12f, CarBody::HoodY(socketAt.x - 0.12f) - 0.09f, 0.6f });
				}
				else
					points[count++] = CarToWorld({ socketAt.x - 0.12f, CarBody::HoodY(socketAt.x - 0.12f) - 0.09f, -0.62f });
			}
			else
			{
				glm::vec3 over = (from + socketAt) * 0.5f;
				over.y = (std::max)(from.y, socketAt.y) + 0.03f;
				points[count++] = CarToWorld(over);
			}
			// (The strap's end sits on its stud, a little out from the panel.)
			points[count++] = CarToWorld(socketAt + (plug == CarPlug::GROUND ? ToVec(build->assembly.GetGroundNormal()) * 0.014f
				: Plumbing(plug) ? Entry(plug) * -0.012f : glm::vec3(-0.02f, 0.f, 0.f)));
		}
		else
			points[count++] = CarToWorld(PlugRest(workshop, plug));

		for (int segment = 0; segment < static_cast<int>(visual.wires.size()); segment++)
		{
			const auto& wire = visual.wires[segment];
			bool used = segment + 1 < count && glm::distance(points[segment], points[segment + 1]) > 1e-4f;
			wire->SetVisible(used);
			if (!used)
				continue;
			glm::vec3 run = points[segment + 1] - points[segment];
			wire->SetPosition(points[segment]);
			wire->SetRotation(Along(run));
			// (A brake pipe's thinner than a wire in its sleeve, a fuel hose a good deal fatter.)
			const float thick = CarPlugKindOf(plug) == CarPlugKind::PIPE ? 0.7f : CarPlugKindOf(plug) == CarPlugKind::HOSE ? 1.7f : 1.f;
			wire->SetScale({ glm::length(run), thick, thick });
		}
		glm::vec3 last = points[count - 1] - points[count - 2];
		visual.plug->SetPosition(points[count - 1]);
		visual.plug->SetRotation(glm::length(last) > 1e-4f ? Along(last) : CarRotation());
		// The one in hand stands out.
		visual.plug->SetEmissive(m_heldPlug == p ? 0.35f : 0.f);
	}
}

bool ShopWorld::UpdateCarPlugs(Workshop& workshop, const FirstPersonController& player, const InputManager& input)
{
	const auto* build = workshop.GetCar();
	if (!build || !PlugsReachable(workshop))
	{
		// (One in hand when the hood comes down, or on getting in, goes back into its loom.)
		if (m_heldPlug >= 0)
			workshop.UnclipLooseCarWire(static_cast<CarPlug>(m_heldPlug));
		m_heldPlug = -1;
		m_plugMenu.reset();
		return false;
	}
	workshop.SettleCarPlugs();
	const auto& car = build->assembly;
	const glm::vec3 eye = player.GetEyePosition(), forward = player.GetForward();
	// How far off the crosshair a point is, if it's in front and within reach.
	auto offAim = [&](const glm::vec3& point) -> std::optional<float> {
		float along = glm::dot(point - eye, forward);
		if (along <= 0.f || along > INTERACT_DISTANCE)
			return std::nullopt;
		return glm::length(point - (eye + forward * along));
	};

	if (m_heldPlug >= 0)
	{
		auto held = static_cast<CarPlug>(m_heldPlug);
		// Something else taken in hand, or carried further than its wire goes: it's let go.
		if (GetHeld() || !car.IsFilled(CarAssembly::PlugHarness(held), 0))
		{
			DropHeldPlug(workshop);
			return false;
		}
		// How much of its wire is taken up, from the loom round its clips to the hand.
		const auto& clips = car.GetWireClips(held);
		glm::vec3 last = Anchor(held);
		float taken = 0.f;
		for (const auto& clip : clips)
		{
			taken += glm::distance(last, ToVec(clip));
			last = ToVec(clip);
		}
		if (taken + glm::distance(CarToWorld(last), m_heldPlugAt) > Reach(held))
		{
			Toast(std::format("The {} won't reach that far.", RunName(held)));
			DropHeldPlug(workshop);
			return true;
		}
		// Every socket with nothing on it, marked; and the one aimed at.
		std::optional<CarPlug> aimed;
		float aimedOff = AIM_SIZE;
		for (int p = 0; p < CAR_PLUGS; p++)
		{
			auto other = static_cast<CarPlug>(p);
			// (The ground strap has no socket: it bolts to the body anywhere.)
			if (other == CarPlug::GROUND || car.IsPlugged(other) || !workshop.CarPlugSocketFitted(other))
				continue;
			glm::vec3 at = CarToWorld(PlugSocket(workshop, other));
			m_fitPoints.push_back({ at, true, true });
			if (auto off = offAim(at); off && *off < aimedOff)
			{
				aimed = other;
				aimedOff = *off;
			}
		}
		const std::string name = CarPlugName(held);
		const bool strap = held == CarPlug::GROUND;
		// Somewhere on the car under the crosshair (not a socket): the wire can be clipped there, to lead it round
		// things on its way.
		// (The ground strap bolts down anywhere on the body itself that's under the crosshair.)
		std::optional<glm::vec3> clipAt, groundAt;
		glm::vec3 groundNormal = { 0.f, 1.f, 0.f };
		bool offBody = false;
		if (!aimed)
			if (auto surface = AimAtCar(player, INTERACT_DISTANCE))
			{
				// Stood a little off what it's on, toward the eye; and only under the hood.
				glm::vec3 at = glm::inverse(CarRotation()) * (surface->point - forward * 0.012f - m_stations[m_carStation].position);
				if (strap && surface->shell)
				{
					// Right on the panel; and which way the panel faces, from two more points on it just beside this one.
					groundAt = glm::inverse(CarRotation()) * (surface->point - m_stations[m_carStation].position);
					glm::vec3 normal = -forward;
					const glm::vec3 right = player.GetRotation() * glm::vec3(1.f, 0.f, 0.f), up = player.GetRotation() * glm::vec3(0.f, 1.f, 0.f);
					auto beside = AimAtCar(eye, glm::normalize(forward + right * 0.012f), INTERACT_DISTANCE);
					auto above = AimAtCar(eye, glm::normalize(forward + up * 0.012f), INTERACT_DISTANCE);
					if (beside && above && beside->shell && above->shell && glm::distance(beside->point, surface->point) < 0.06f
						&& glm::distance(above->point, surface->point) < 0.06f)
					{
						glm::vec3 across = glm::cross(beside->point - surface->point, above->point - surface->point);
						if (glm::length(across) > 1e-8f)
							normal = glm::normalize(across) * (glm::dot(across, forward) > 0.f ? -1.f : 1.f);
					}
					groundNormal = glm::inverse(CarRotation()) * normal;
				}
				offBody = strap && !surface->shell;
				if (static_cast<int>(clips.size()) >= CAR_WIRE_CLIPS)
					;
				// (A pipe can be clipped anywhere along the car, on its way back to the axle or the tank.)
				else if (Plumbing(held))
					clipAt = at;
				else if (at.x > CarBody::FIREWALL_X - 0.02f && at.x < CarBody::NOSE_X && std::abs(at.z) < CarBody::HOOD_HALF && at.y > 0.15f
					&& at.y < CarBody::HoodY(at.x) + 0.03f)
					clipAt = at;
			}
		if (clipAt)
			m_fitPoints.push_back({ CarToWorld(*clipAt), false, true });
		const std::string clipHint = clipAt ? std::format("   [E] Clip the {} here", RunName(held)) : "";
		const CarPlugKind kind = CarPlugKindOf(held);
		if (groundAt)
		{
			m_fitPoints.push_back({ CarToWorld(*groundAt), true, true });
			m_prompt = std::format("[LMB] Bolt the ground strap to the body here{}   [RMB] Let it go", clipHint);
		}
		else if (!aimed && strap)
			m_prompt = std::format("The battery's ground strap in hand: {}{}   [RMB] Let it go",
				offBody ? "that isn't the body. Aim at bare bodywork to bolt it down" : "aim at the car's body, anywhere it reaches, to bolt it down ([LMB])", clipHint);
		else if (!aimed)
			m_prompt = kind == CarPlugKind::PIPE ? std::format("The {} in hand   [LMB] Screw its union in{}   [RMB] Let it go", PlugLabel(held), clipHint)
				: kind == CarPlugKind::HOSE ? std::format("The {} in hand   [LMB] Push it on{}   [RMB] Let it go", PlugLabel(held), clipHint)
				: std::format("The {} in hand: push it onto its socket ([LMB]){}   [RMB] Let it go", PlugLabel(held), clipHint);
		else if (*aimed == held)
			m_prompt = held == CarPlug::BATTERY ? "[LMB] Clamp the lead onto the battery's positive post   [RMB] Let it go"
				: kind == CarPlugKind::PIPE ? std::format("[LMB] Screw the pipe's union into the {}   [RMB] Let it go", name)
				: kind == CarPlugKind::HOSE ? std::format("[LMB] Push the hose onto the {}   [RMB] Let it go", name)
				: std::format("[LMB] Push the plug onto the {}   [RMB] Let it go", name);
		else
			m_prompt = std::format("That's {}: this is the {}   [RMB] Let it go", SocketLabel(*aimed), PlugLabel(held));
		if (clipAt && (Controls::Pressed(input, Control::USE) || (!groundAt && std::exchange(m_devPress, false))))
		{
			if (taken + glm::distance(last, *clipAt) > Reach(held) - 0.15f)
				Toast(std::format("There isn't the {} left to reach there.", RunName(held)));
			else if (workshop.ClipCarWire(held, { clipAt->x, clipAt->y, clipAt->z }))
				Cue(Sound::CLICK, CarToWorld(*clipAt), 0.6f);
			return true;
		}
		if (groundAt && (Controls::Pressed(input, Control::PRIMARY) || std::exchange(m_devPress, false)))
		{
			if (taken + glm::distance(last, *groundAt) > Reach(held))
				Toast("The strap won't reach there.");
			else if (workshop.GroundCarBattery({ groundAt->x, groundAt->y, groundAt->z }, { groundNormal.x, groundNormal.y, groundNormal.z }))
			{
				m_heldPlug = -1;
				Cue(Sound::CLICK, CarToWorld(*groundAt));
				Toast("The battery is grounded to the body.");
			}
		}
		else if (aimed && (Controls::Pressed(input, Control::PRIMARY) || std::exchange(m_devPress, false)))
		{
			if (*aimed != held)
			{
				Toast(std::format("It won't go on: that's {}, and this is the {}.", SocketLabel(*aimed), PlugLabel(held)));
				Cue(Sound::CLICK, CarToWorld(PlugSocket(workshop, *aimed)), 0.5f);
			}
			else if (workshop.ConnectCarPlug(held))
			{
				m_heldPlug = -1;
				Cue(Sound::CLICK, CarToWorld(PlugSocket(workshop, held)));
				Toast(kind == CarPlugKind::PIPE ? std::format("The pipe is done up on the {}.", name)
					: kind == CarPlugKind::HOSE ? std::format("The hose is on the {}.", name) : std::format("The {} is plugged in.", name));
			}
		}
		else if (Controls::Pressed(input, Control::SET_DOWN) || Controls::Pressed(input, Control::DROP))
			DropHeldPlug(workshop);
		return true;
	}

	// With empty hands: the plug under the crosshair, wherever it is, is taken (pulled off what it's on, if it's on).
	if (GetHeld())
		return false;
	std::optional<CarPlug> aimed;
	float aimedOff = AIM_SIZE;
	for (int p = 0; p < CAR_PLUGS; p++)
	{
		auto plug = static_cast<CarPlug>(p);
		// (One tucked away in its loom is picked from the loom's menu instead.)
		if (!car.IsFilled(CarAssembly::PlugHarness(plug), 0) || Tucked(car, plug))
			continue;
		glm::vec3 at = CarToWorld(car.IsPlugged(plug) ? PlugSocket(workshop, plug) : PlugRest(workshop, plug));
		if (auto off = offAim(at); off && *off < aimedOff)
		{
			aimed = plug;
			aimedOff = *off;
		}
	}
	// Where things are picked from is marked, baby blue, only from close by the engine: stood back from the car there's
	// nothing over it. (The clutch's pipe, hanging loose off its master cylinder, is marked too.)
	const bool marked = glm::distance(eye, CarToWorld(CAR_ENGINE_MOUNT)) < MARKS_WITHIN;
	if (marked && car.IsFilled(CarSlot::CLUTCH_MASTER_CYLINDER, 0) && !car.IsPlugged(CarPlug::CLUTCH_PIPE))
		m_fitPoints.push_back({ CarToWorld(PlugRest(workshop, CarPlug::CLUTCH_PIPE)), false, true });
	// Or a loom, where its tails leave it: its plugs are picked from a menu.
	if (!aimed)
		for (CarSlot harness : MENU_HARNESSES)
		{
			if (!car.IsFilled(harness, 0))
				continue;
			if (marked)
				m_fitPoints.push_back({ CarToWorld(LoomPoint(harness)), false, true });
			// (The harness's anywhere along its tails; a line set's at the one place its pipes are picked from.)
			bool onIt = false;
			if (harness != CarSlot::MAIN_HARNESS)
			{
				auto off = offAim(CarToWorld(LoomPoint(harness)));
				onIt = off && *off < AIM_SIZE;
			}
			else
				for (CarPlug plug : PlugsOn(harness))
					if (auto off = offAim(CarToWorld(Anchor(plug))); off && *off < AIM_SIZE)
						onIt = true;
			if (!onIt)
				continue;
			std::string hint = harness == CarSlot::BRAKE_LINE_SET ? "[LMB] Pick one of the brake pipes"
				: harness == CarSlot::FUEL_LINE_SET ? "[LMB] Pick one of the fuel hoses" : "[LMB] Pick one of the harness's plugs";
			m_prompt = m_prompt.empty() ? hint : hint + "   " + m_prompt;
			if (!Controls::Pressed(input, Control::PRIMARY) && !std::exchange(m_devPress, false))
				return false;
			// It opens on the first that isn't on anything.
			m_plugMenu = PlugMenu{ harness, 0 };
			const auto plugs = PlugsOn(harness);
			for (int i = 0; i < static_cast<int>(plugs.size()); i++)
				if (!car.IsPlugged(plugs[i]))
				{
					m_plugMenu->selected = i;
					break;
				}
			Cue(Sound::CLICK, CarToWorld(LoomPoint(harness)), 0.4f);
			return true;
		}
	if (!aimed)
	{
		// Or one of the clips on a wire: it comes off.
		std::optional<std::pair<CarPlug, int>> clip;
		float clipOff = 0.035f;
		for (int p = 0; p < CAR_PLUGS; p++)
		{
			const auto& clips = car.GetWireClips(static_cast<CarPlug>(p));
			for (int c = 0; c < static_cast<int>(clips.size()); c++)
				if (auto off = offAim(CarToWorld(ToVec(clips[c]))); off && *off < clipOff)
				{
					clip = { static_cast<CarPlug>(p), c };
					clipOff = *off;
				}
		}
		if (!clip)
			return false;
		std::string hint = Plumbing(clip->first) ? std::format("[RMB] Take this clip off the {}", PlugLabel(clip->first))
			: std::format("[RMB] Take this clip off the {}'s wire", CarPlugName(clip->first));
		m_prompt = m_prompt.empty() ? hint : hint + "   " + m_prompt;
		if (!Controls::Pressed(input, Control::SET_DOWN))
			return false;
		glm::vec3 at = CarToWorld(ToVec(car.GetWireClips(clip->first)[clip->second]));
		workshop.UnclipCarWire(clip->first, clip->second);
		Cue(Sound::CLICK, at, 0.5f);
		return true;
	}
	bool on = car.IsPlugged(*aimed);
	std::string hint = *aimed == CarPlug::GROUND ? (on ? "[LMB] Take the battery's ground strap off the body" : "[LMB] Take the battery's ground strap")
		: Plumbing(*aimed) ? (!on ? std::format("[LMB] Take the {}", PlugLabel(*aimed))
			: CarPlugKindOf(*aimed) == CarPlugKind::PIPE ? std::format("[LMB] Undo the pipe's union on the {}", CarPlugName(*aimed))
			: std::format("[LMB] Pull the hose off the {}", CarPlugName(*aimed)))
		: on ? std::format("[LMB] Pull the plug off the {}", CarPlugName(*aimed)) : std::format("[LMB] Take the {}'s plug", CarPlugName(*aimed));
	m_prompt = m_prompt.empty() ? hint : hint + "   " + m_prompt;
	if (!Controls::Pressed(input, Control::PRIMARY) && !std::exchange(m_devPress, false))
		return false;
	TakePlug(workshop, *aimed);
	return true;
}
