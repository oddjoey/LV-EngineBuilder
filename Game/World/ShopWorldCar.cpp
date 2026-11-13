// The project car in the 3D shop: where each part sits on it, fitting parts to it by hand, and its nuts and bolts.
#include "ShopWorld.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <glm/gtc/matrix_transform.hpp>

#include "../Audio/Sounds.h"
#include "../Controls.h"
#include "CarBody.h"
#include "ShopWorldShared.h"

using namespace ShopLayout;

namespace
{
	// The car's space: +X its nose, +Y up from the ground under its wheels, +Z its right side (as you sit in it).
	constexpr float FRONT_AXLE_X = 1.2f;
	constexpr float REAR_AXLE_X = -1.2f;
	constexpr float WHEEL_Z = 0.69f;		// the tires' middles, either side
	constexpr float WHEEL_Y = 0.29f;		// the axle height: the tire's radius
	constexpr float BODY_SIDE_Z = 0.8f;		// the panels' outer faces

	// Not turned. (A quaternion written as {} is all zeros, not this.)
	const glm::quat NONE = glm::quat(1.f, 0.f, 0.f, 0.f);
	glm::quat TurnY(float radians) { return glm::angleAxis(radians, glm::vec3(0.f, 1.f, 0.f)); }
	glm::quat Pitch(float radians) { return glm::angleAxis(radians, glm::vec3(0.f, 0.f, 1.f)); }	// about Z: +X up toward +Y
	glm::quat Roll(float radians) { return glm::angleAxis(radians, glm::vec3(1.f, 0.f, 0.f)); }		// about X: +Y toward +Z
	// A model built lengthways along X, turned to lie across the car (along Z).
	const glm::quat ACROSS = TurnY(glm::half_pi<float>());
	// A disc built lying flat, stood up facing out of the car's side (its face toward +Z, or -Z on the left).
	glm::quat FacingSide(float side) { return Roll(side * glm::half_pi<float>()); }

	// Which way a position faces: -1 its left, +1 its right.
	float SideOf(CarSlot slot, int index)
	{
		if (auto corner = CarAssembly::CornerOf(slot, index))
			return static_cast<int>(*corner) % 2 == 0 ? -1.f : 1.f;
		return index == 0 ? -1.f : 1.f;
	}

	bool IsFront(CarSlot slot, int index)
	{
		auto corner = CarAssembly::CornerOf(slot, index);
		return !corner || *corner == Corner::FRONT_LEFT || *corner == Corner::FRONT_RIGHT;
	}
}

ShopWorld::CarPose ShopWorld::PoseOnCar(CarSlot slot, int index) const
{
	// Nuts and bolts: in a ring on whatever they hold, on its top (or, for a wheel's, round its hub).
	if (const auto* holes = CarAssembly::Holes(slot))
	{
		CarSlot parent = CarAssembly::SlotFor(holes->parent);
		int parentIndex = holes->throughAll ? 0 : index / holes->perParent;
		int hole = holes->throughAll ? index : index % holes->perParent;
		float angle = glm::two_pi<float>() * (hole + 0.5f) / static_cast<float>((std::max)(holes->perParent, 1));
		if (slot == CarSlot::LUG_NUT)
		{
			// Round the hub's face, the nut's axis out of the wheel.
			float side = SideOf(parent, parentIndex);
			float x = CarAssembly::CornerOf(parent, parentIndex) >= Corner::REAR_LEFT ? REAR_AXLE_X : FRONT_AXLE_X;
			glm::vec3 at = { x + std::cos(angle) * 0.05f, WHEEL_Y + std::sin(angle) * 0.05f, side * (WHEEL_Z + 0.075f) };
			return { at, FacingSide(side) };
		}
		// The engine's own mounting nuts sit on its mounts.
		if (parent == CarSlot::ENGINE)
			return { { 1.45f, 0.44f, (hole == 0 ? -1.f : 1.f) * 0.25f }, NONE };
		// The body panels' bolts, each where it really goes (the models have the lips, hinges and posts they're in).
		using namespace CarBody;
		const glm::quat FACING_BACK = Pitch(glm::half_pi<float>());		// its head toward the tail
		const glm::quat HEAD_DOWN = Roll(glm::pi<float>());
		float panelSide = SideOf(parent, parentIndex);
		switch (slot)
		{
			case CarSlot::FENDER_BOLT:
			{
				// Six down through the fender's lip under the hood's edge onto the inner wing (reached with the hood up),
				// and two into the hinge post behind the door's front edge (reached with the door open).
				if (hole < 6)
				{
					float x = 1.04f + hole * 0.19f;
					return { { x, HoodY(x) - 0.035f, panelSide * 0.675f }, Pitch(HoodPitch()) };
				}
				return { { DOOR_FRONT_X, hole == 6 ? 0.34f : 0.59f, panelSide * 0.69f }, FACING_BACK };
			}
			case CarSlot::DOOR_HINGE_BOLT:
			{
				// Two through each hinge's leaf on the hinge post, in the door's opening: they stay put as the door swings.
				float y = (hole < 2 ? 0.42f : 0.74f) + (hole % 2 == 0 ? -0.022f : 0.022f);
				return { { DOOR_FRONT_X - 0.01f, y, panelSide * 0.745f }, FACING_BACK };
			}
			case CarSlot::HOOD_HINGE_BOLT:
			{
				// Up through the hinge arms under the hood's back corners: they go up with it.
				CarPose hood = PoseOnCar(CarSlot::HOOD, 0);
				glm::vec3 local = { hole % 2 == 0 ? 0.055f : 0.125f, -0.038f, hole < 2 ? -0.6f : 0.6f };
				return { hood.offset + hood.rotation * local, hood.rotation * HEAD_DOWN };
			}
			case CarSlot::BUMPER_BOLT:
			{
				// Up through the bottom of the bumper's beam into the ends of the chassis rails, two a side.
				constexpr float ACROSS_Z[] = { -0.52f, -0.4f, 0.4f, 0.52f };
				return { { NOSE_X + 0.03f, 0.25f, ACROSS_Z[std::clamp(hole, 0, 3)] }, HEAD_DOWN };
			}
			default:
				break;
		}
		CarPose on = PoseOnCar(parent, parentIndex);
		const auto& bounds = PartBounds(CarAssembly::SlotPartType(parent));
		glm::vec3 extent = (bounds.second - bounds.first) * 0.5f;
		glm::vec3 middle = (bounds.first + bounds.second) * 0.5f;
		float radius = (std::min)((std::min)(extent.x, extent.z) * 0.8f, 0.08f);
		glm::vec3 local = { middle.x + std::cos(angle) * radius, bounds.second.y, middle.z + std::sin(angle) * radius };
		return { on.offset + on.rotation * local, on.rotation };
	}

	using C = CarSlot;
	float side = SideOf(slot, index);
	bool front = IsFront(slot, index);
	float axleX = front ? FRONT_AXLE_X : REAR_AXLE_X;
	switch (slot)
	{
		// Body
		// The panels sit on the body's own lines (CarBody.h, which their models are cut to as well). The ones that open
		// are placed by their hinge lines and turned about them by how far open they are.
		case C::BODY_SHELL:			return {};
		case C::FRONT_FENDER:		return { { CarBody::DOOR_FRONT_X, CarBody::SILL_Y, side * CarBody::SIDE }, NONE };
		case C::DOOR:
			return { { CarBody::DOOR_FRONT_X - CarBody::GAP, CarBody::ROCKER_TOP + CarBody::GAP, side * CarBody::SIDE },
				TurnY(side * CarBody::DOOR_OPEN * CarPanelOpen(side < 0.f ? CarPanel::DOOR_LEFT : CarPanel::DOOR_RIGHT)) };
		case C::HOOD:
			return { { CarBody::HOOD_REAR_X + CarBody::GAP, CarBody::BELT_Y, 0.f }, Pitch(CarBody::HoodPitch() + CarBody::HOOD_OPEN * CarPanelOpen(CarPanel::HOOD)) };
		case C::TRUNK_LID:
			return { { CarBody::TRUNK_FRONT_X - CarBody::GAP, CarBody::BELT_Y, 0.f }, Pitch(-CarBody::TRUNK_OPEN * CarPanelOpen(CarPanel::TRUNK)) };
		case C::FRONT_BUMPER:		return { { CarBody::NOSE_X + 0.035f, 0.25f, 0.f }, ACROSS };
		case C::REAR_BUMPER:		return { { CarBody::TAIL_X - 0.035f, 0.25f, 0.f }, ACROSS };
		case C::HEADLIGHT:			return { { CarBody::NOSE_X - 0.155f, 0.625f, side * 0.52f }, ACROSS };
		case C::TAIL_LIGHT:			return { { CarBody::TAIL_X + 0.01f, 0.555f, side * 0.5f }, ACROSS };
		// The glass lies in its opening: the windshield from the cowl back up to the roof, the rear window from the roof
		// back down to the deck, each about the middle of its slope.
		case C::WINDSHIELD:
		{
			using namespace CarBody;
			float rise = ROOF_Y - BELT_Y;
			return { { (WINDSHIELD_BASE_X + WINDSHIELD_TOP_X) * 0.5f, BELT_Y + rise * 0.5f, 0.f }, Pitch(-std::atan2(rise, WINDSHIELD_BASE_X - WINDSHIELD_TOP_X)) * ACROSS };
		}
		case C::REAR_WINDOW:
		{
			using namespace CarBody;
			float rise = ROOF_Y - BELT_Y;
			return { { (REAR_WINDOW_BASE_X + REAR_WINDOW_TOP_X) * 0.5f, BELT_Y + rise * 0.5f, 0.f }, Pitch(std::atan2(rise, REAR_WINDOW_TOP_X - REAR_WINDOW_BASE_X)) * ACROSS };
		}
		// Interior
		case C::DASHBOARD:			return { { 0.6f, 0.55f, 0.f }, ACROSS };
		case C::INSTRUMENT_CLUSTER:	return { { 0.5f, 0.9f, -0.37f }, ACROSS };
		case C::SEAT:				return { { -0.3f, 0.22f, side * 0.37f }, NONE };
		case C::STEERING_WHEEL:		return { { 0.42f, 0.92f, -0.37f }, Pitch(1.13f) };
		// Front suspension, round the front wheels.
		case C::FRONT_STRUT:
		case C::FRONT_COIL_SPRING:	return { { FRONT_AXLE_X, 0.2f, side * 0.55f }, NONE };	// its top mount just under the hood
		case C::STEERING_KNUCKLE:	return { { FRONT_AXLE_X, 0.14f, side * 0.6f }, NONE };
		case C::LOWER_CONTROL_ARM:	return { { FRONT_AXLE_X, 0.14f, side * 0.4f }, ACROSS };
		case C::STRUT_ROD:			return { { FRONT_AXLE_X + 0.25f, 0.16f, side * 0.45f }, NONE };
		case C::FRONT_SWAY_BAR:		return { { FRONT_AXLE_X + 0.35f, 0.2f, 0.f }, ACROSS };
		// Rear suspension: the axle across under the boot floor, on its links.
		case C::REAR_AXLE:			return { { REAR_AXLE_X, WHEEL_Y - 0.15f, 0.f }, ACROSS };
		case C::DIFFERENTIAL:		return { { REAR_AXLE_X, WHEEL_Y - 0.14f, 0.f }, TurnY(glm::half_pi<float>()) };
		case C::LOWER_TRAILING_LINK: return { { REAR_AXLE_X + 0.3f, 0.18f, side * 0.5f }, NONE };
		case C::UPPER_TRAILING_LINK: return { { REAR_AXLE_X + 0.22f, 0.36f, side * 0.28f }, NONE };
		case C::LATERAL_ROD:		return { { REAR_AXLE_X - 0.15f, 0.3f, 0.f }, ACROSS };
		case C::REAR_COIL_SPRING:	return { { REAR_AXLE_X + 0.05f, 0.3f, side * 0.5f }, NONE };
		case C::REAR_SHOCK:			return { { REAR_AXLE_X - 0.1f, 0.22f, side * 0.56f }, NONE };
		// Steering
		case C::STEERING_RACK:		return { { FRONT_AXLE_X - 0.15f, 0.22f, 0.f }, ACROSS };
		case C::TIE_ROD_END:		return { { FRONT_AXLE_X - 0.12f, 0.22f, side * 0.5f }, ACROSS };
		case C::STEERING_COLUMN:	return { { 0.68f, 0.73f, -0.37f }, Pitch(-0.54f) };
		// Brakes: the booster and master cylinder on the driver's side of the firewall, the lines along the floor.
		case C::BRAKE_BOOSTER:		return { { 0.98f, 0.55f, -0.42f }, NONE };
		case C::BRAKE_MASTER_CYLINDER: return { { 1.15f, 0.65f, -0.42f }, NONE };
		case C::BRAKE_LINE_SET:		return { { -0.2f, 0.15f, -0.3f }, NONE };
		case C::BRAKE_HOSE:			return index < 2 ? CarPose{ { FRONT_AXLE_X - 0.05f, 0.3f, side * 0.6f }, NONE }
										: CarPose{ { REAR_AXLE_X + 0.15f, 0.3f, 0.12f }, NONE };
		case C::BRAKE_ROTOR:		return { { FRONT_AXLE_X, WHEEL_Y, side * 0.6f }, FacingSide(side) };
		case C::BRAKE_PAD_SET:		return { { FRONT_AXLE_X + 0.1f, WHEEL_Y + 0.07f, side * 0.6f }, NONE };
		case C::BRAKE_CALIPER:		return { { FRONT_AXLE_X + 0.1f, WHEEL_Y + 0.05f, side * 0.6f }, NONE };
		case C::WHEEL_CYLINDER:		return { { REAR_AXLE_X, WHEEL_Y + 0.08f, side * 0.6f }, ACROSS };
		case C::BRAKE_SHOE_SET:		return { { REAR_AXLE_X, WHEEL_Y - 0.03f, side * 0.6f }, ACROSS };
		case C::BRAKE_DRUM:			return { { REAR_AXLE_X, WHEEL_Y, side * 0.6f }, FacingSide(side) };
		case C::PARKING_BRAKE_CABLE: return { { -0.5f, 0.15f, 0.05f }, NONE };
		// Drivetrain: the clutch behind the engine's flywheel, the gearbox down the tunnel to the driveshaft.
		case C::ENGINE_MOUNT:		return { { 1.45f, 0.36f, side * 0.25f }, NONE };
		case C::CLUTCH_DISC:		return { { 0.92f, 0.35f, 0.f }, Pitch(glm::half_pi<float>()) * TurnY(0.f) };
		case C::PRESSURE_PLATE:		return { { 0.88f, 0.35f, 0.f }, Pitch(glm::half_pi<float>()) };
		case C::RELEASE_BEARING:	return { { 0.82f, 0.35f, 0.f }, Pitch(glm::half_pi<float>()) };
		case C::TRANSMISSION:		return { { 0.62f, 0.2f, 0.f }, TurnY(glm::pi<float>()) };
		case C::TRANSMISSION_MOUNT:	return { { 0.27f, 0.16f, 0.f }, ACROSS };
		case C::CLUTCH_MASTER_CYLINDER: return { { 0.98f, 0.62f, -0.6f }, NONE };
		case C::CLUTCH_RELEASE_CYLINDER: return { { 0.74f, 0.26f, 0.16f }, NONE };
		case C::DRIVESHAFT:			return { { -0.38f, 0.2f, 0.f }, NONE };
		// Fuel, under the boot floor and along the right of the floor.
		case C::FUEL_TANK:			return { { -1.72f, 0.24f, 0.f }, ACROSS };
		case C::FUEL_SENDER:		return { { -1.6f, 0.48f, 0.18f }, NONE };
		case C::FUEL_LINE_SET:		return { { 0.f, 0.15f, 0.35f }, NONE };
		case C::FUEL_FILTER:		return { { 0.98f, 0.4f, 0.45f }, NONE };
		// Electrical
		case C::BATTERY:			return { { 1.78f, 0.52f, -0.5f }, NONE };
		case C::MAIN_HARNESS:		return { { 0.62f, 0.4f, 0.f }, ACROSS };
		case C::ENGINE_HARNESS:		return { { 1.1f, 0.66f, 0.36f }, NONE };
		case C::FUSE_BOX:			return { { 0.62f, 0.32f, -0.62f }, NONE };
		case C::IGNITION_COIL:		return { { 1.55f, 0.6f, 0.45f }, NONE };
		case C::STARTER:			return { { 0.9f, 0.28f, -0.2f }, NONE };
		// Cooling: the radiator across the nose, the fan behind it.
		case C::RADIATOR:			return { { 1.98f, 0.3f, 0.f }, ACROSS };
		case C::RADIATOR_HOSE:		return { { 1.68f, index == 0 ? 0.7f : 0.32f, 0.12f }, NONE };
		case C::COOLING_FAN:		return { { 1.9f, 0.46f, 0.f }, Pitch(glm::half_pi<float>()) };
		// Exhaust: from the downpipe back under the right of the floor to the tailpipe at the back.
		case C::CATALYTIC_CONVERTER: return { { 0.15f, 0.13f, 0.28f }, NONE };
		case C::MUFFLER:			return { { -1.55f, 0.12f, 0.45f }, TurnY(glm::pi<float>()) };
		// Wheels, at their corners: the tire round the wheel.
		case C::WHEEL:
		case C::TIRE:				return { { axleX, 0.f, side * WHEEL_Z }, ACROSS };
		default:					break;
	}
	return {};
}

glm::quat ShopWorld::CarRotation() const
{
	return m_carStation < m_stations.size() ? m_stations[m_carStation].Rotation() : glm::quat(1.f, 0.f, 0.f, 0.f);
}

glm::vec3 ShopWorld::CarToWorld(const glm::vec3& offset) const
{
	glm::vec3 origin = m_carStation < m_stations.size() ? m_stations[m_carStation].position : glm::vec3(0.f);
	return origin + CarRotation() * offset;
}

glm::vec3 ShopWorld::CarSlotCenter(CarSlot slot, int index) const
{
	auto pose = PoseOnCar(slot, index);
	PartType type = CarAssembly::SlotPartType(slot);
	if (slot == CarSlot::ENGINE)
		return CarToWorld(CAR_ENGINE_MOUNT);
	const auto& bounds = PartBounds(type);
	glm::vec3 center = (bounds.first + bounds.second) * 0.5f;
	// The right-hand fender and door are the mirror of the part's own (left-hand) model.
	if ((slot == CarSlot::FRONT_FENDER || slot == CarSlot::DOOR) && index == 1)
		center.z = -center.z;
	return CarToWorld(pose.offset + pose.rotation * center);
}

float ShopWorld::CarPanelOpen(CarPanel panel) const
{
	return glm::smoothstep(0.f, 1.f, m_carPanels[static_cast<size_t>(panel)].amount);
}

void ShopWorld::ToggleCarPanel(CarPanel panel)
{
	auto& state = m_carPanels[static_cast<size_t>(panel)];
	state.open = !state.open;
	Cue(state.open ? Sound::PICK_UP : Sound::SET_DOWN, CarToWorld({ 0.f, 0.8f, 0.f }));
}

std::optional<ShopWorld::CarPanel> ShopWorld::AimedCarPanel(const Workshop& workshop, const FirstPersonController& player) const
{
	const auto* build = workshop.GetCar();
	if (!build || m_carStation >= m_stations.size() || !m_stations[m_carStation].present || !m_hover)
		return std::nullopt;
	// A door, the hood or the lid on the car, under the crosshair.
	if (m_hover->kind == TargetKind::ITEM)
	{
		const auto* visual = Find(m_hover->item);
		if (visual && visual->part && visual->place == ItemPlace::CAR)
			if (auto at = build->assembly.Find(visual->part.get()))
				switch (at->first)
				{
					case CarSlot::DOOR:			return at->second == 0 ? CarPanel::DOOR_LEFT : CarPanel::DOOR_RIGHT;
					case CarSlot::HOOD:			return CarPanel::HOOD;
					case CarSlot::TRUNK_LID:	return CarPanel::TRUNK;
					default:					break;
				}
		return std::nullopt;
	}
	// The fuel flap isn't a part: aimed at by its own box, on the car's quarter.
	if (m_fuelFlap && m_fuelFlap->IsVisible() && m_hover->kind == TargetKind::STATION && m_hover->stationIndex == m_carStation)
	{
		auto box = WorldBounds(m_fuelFlap);
		if (auto distance = RayBox(player.GetEyePosition(), player.GetForward(), box.min - glm::vec3(0.02f), box.max + glm::vec3(0.02f));
				distance && *distance <= INTERACT_DISTANCE)
			return CarPanel::FUEL_FLAP;
	}
	return std::nullopt;
}

void ShopWorld::AnimateCarPanels(const Workshop& workshop, float deltaTime)
{
	constexpr float SWING_SECONDS = 0.45f;
	m_carPanelsMoving = false;
	const auto* build = workshop.GetCar();
	bool present = build && m_carStation < m_stations.size() && m_stations[m_carStation].present;
	auto fitted = [&](CarPanel panel) {
		if (!present)
			return false;
		const auto& car = build->assembly;
		switch (panel)
		{
			case CarPanel::DOOR_LEFT:	return car.IsFilled(CarSlot::DOOR, 0);
			case CarPanel::DOOR_RIGHT:	return car.IsFilled(CarSlot::DOOR, 1);
			case CarPanel::HOOD:		return car.IsFilled(CarSlot::HOOD, 0);
			case CarPanel::TRUNK:		return car.IsFilled(CarSlot::TRUNK_LID, 0);
			default:					return true;
		}
	};
	for (size_t p = 0; p < m_carPanels.size(); p++)
	{
		auto& state = m_carPanels[p];
		// One that's off the car is shut again for when it goes back on.
		if (!fitted(static_cast<CarPanel>(p)))
			state.open = false;
		float target = state.open ? 1.f : 0.f;
		if (state.amount == target)
			continue;
		float step = fitted(static_cast<CarPanel>(p)) ? deltaTime / SWING_SECONDS : 1.f;
		state.amount = state.amount < target ? (std::min)(target, state.amount + step) : (std::max)(target, state.amount - step);
		m_carPanelsMoving = true;
	}
	if (m_fuelFlap)
	{
		m_fuelFlap->SetVisible(present);
		m_fuelFlap->SetPosition(CarToWorld({ CarBody::FLAP_X + CarBody::FLAP_HALF, CarBody::FLAP_Y, CarBody::SIDE }));
		m_fuelFlap->SetRotation(CarRotation() * TurnY(CarBody::FLAP_OPEN * CarPanelOpen(CarPanel::FUEL_FLAP)));
	}
}

bool ShopWorld::UpdateCarPanels(const Workshop& workshop, const FirstPersonController& player, const InputManager& input)
{
	auto aimed = AimedCarPanel(workshop, player);
	if (!aimed)
		return false;
	static const char* NAMES[] = { "door", "door", "hood", "trunk", "fuel flap" };
	std::string hint = std::format("[LMB] {} the {}", IsCarPanelOpen(*aimed) ? "Shut" : "Open", NAMES[static_cast<size_t>(*aimed)]);
	m_prompt = m_prompt.empty() ? hint : m_prompt + "   " + hint;
	if (!Controls::Pressed(input, Control::PRIMARY))
		return false;
	ToggleCarPanel(*aimed);
	return true;
}

void ShopWorld::PlaceOnCar(ItemVisual& visual, CarSlot slot, int index) const
{
	auto pose = PoseOnCar(slot, index);
	// A nut or bolt stands out of its hole while it's loose, as on the engine.
	if (CarAssembly::IsFastenerSlot(slot) && visual.part && visual.part->GetTightness() == FastenerTightness::LOOSE && !PartWayDoneUp(*visual.part))
		pose.offset += pose.rotation * glm::vec3(0.f, 0.012f, 0.f);
	visual.object->SetPosition(CarToWorld(pose.offset));
	visual.object->SetRotation(CarRotation() * pose.rotation);
	visual.object->SetScale({ 1.f, 1.f, 1.f });
}

std::optional<size_t> ShopWorld::FindCarFitTarget(const Workshop& workshop, const FirstPersonController& player, const ItemVisual& held)
{
	const auto* build = workshop.GetCar();
	if (!build || m_carStation >= m_stations.size() || !m_stations[m_carStation].present || !held.part)
		return std::nullopt;
	glm::vec3 eye = player.GetEyePosition();
	glm::vec3 forward = player.GetForward();
	// Within reach of some of it: the car is long, so its middle may be further off than the part you're at.
	if (glm::distance(CarToWorld({ 0.f, 0.6f, 0.f }), eye) > INTERACT_DISTANCE + 2.5f)
		return std::nullopt;
	const auto& car = build->assembly;
	PartType type = held.part->GetType();
	auto model = ModelOf(held.object);
	float radius = glm::length(model->GetBoundsMax() - model->GetBoundsMin()) * 0.5f;
	float best = std::clamp(radius * 0.45f, 0.04f, 0.25f);

	for (int s = 0; s < static_cast<int>(CarSlot::COUNT); s++)
	{
		auto slot = static_cast<CarSlot>(s);
		if (slot == CarSlot::ENGINE || CarAssembly::SlotPartType(slot) != type || CarAssembly::CarriedBy(slot))
			continue;
		bool fastener = CarAssembly::IsFastenerSlot(slot);
		for (int index = 0; index < car.SlotCount(slot); index++)
		{
			if (car.IsFilled(slot, index))
				continue;
			// Ready once what it goes on is on (a strut's spring and a wheel's tire go on with it, in hand).
			bool ready = true;
			for (const auto& [other, otherIndex] : car.Prerequisites(slot, index))
				if (!car.IsFilled(other, otherIndex) && !(CarAssembly::CarriedBy(other) && otherIndex == index))
					ready = false;
			// Holes only exist once the part they're in is on.
			if (fastener && !ready)
				continue;
			glm::vec3 position = CarSlotCenter(slot, index);
			if (glm::distance(position, eye) > INTERACT_DISTANCE + 0.5f)
				continue;
			m_fitPoints.push_back({ position, ready });
			float along = glm::dot(position - eye, forward);
			if (along <= 0.f || along > INTERACT_DISTANCE + 0.5f)
				continue;
			float off = glm::length(position - (eye + forward * along));
			if (off < best)
			{
				best = off;
				m_carFitTarget = CarFitTarget{ slot, index, position, ready };
			}
		}
	}
	return m_carFitTarget ? std::optional(m_carStation) : std::nullopt;
}

std::string ShopWorld::CarFitPrompt(const Workshop& workshop, const ItemVisual& held) const
{
	const auto& car = workshop.GetCar()->assembly;
	std::string position = car.PositionName(m_carFitTarget->slot, m_carFitTarget->index);
	for (const auto& [other, otherIndex] : car.Prerequisites(m_carFitTarget->slot, m_carFitTarget->index))
		if (!car.IsFilled(other, otherIndex) && !(CarAssembly::CarriedBy(other) && otherIndex == m_carFitTarget->index))
			return std::format("{}: the {} has to go on first", position, car.PositionName(other, otherIndex));
	if (m_carFitTarget->slot == CarSlot::FRONT_STRUT && !workshop.CarriedOn(*held.part))
		return std::format("{}: compress a spring onto the strut first", position);
	if (m_carFitTarget->slot == CarSlot::WHEEL && !workshop.CarriedOn(*held.part))
		return std::format("{}: mount a tire on the wheel first", position);
	if (CarAssembly::Holes(m_carFitTarget->slot))
		return std::format("[E] Put the {} in finger tight: {}", held.part->GetName(), position);
	return std::format("[E] Fit the {}: {}", held.part->GetName(), position);
}

void ShopWorld::FitToCar(Workshop& workshop, ItemVisual& held)
{
	if (!m_carFitTarget || !held.part)
	{
		Toast(std::format("The {} doesn't go on the car there.", held.part ? held.part->GetName() : std::string("part")));
		return;
	}
	if (!workshop.InstallCarPart(m_carFitTarget->slot, m_carFitTarget->index, held.part))
	{
		if (!workshop.GetLog().empty())
			Toast(workshop.GetLog().back());
		return;
	}
	Cue(Sound::FIT_PART, m_carFitTarget->position);
	// Out of the hand and onto the car; the spring or tire it carries goes on with it.
	SetPlace(held, ItemPlace::CAR);
	bool carries = m_carFitTarget->slot == CarSlot::FRONT_STRUT || m_carFitTarget->slot == CarSlot::WHEEL;
	if (carries)
		if (auto carried = workshop.GetCar()->assembly.Get(m_carFitTarget->slot == CarSlot::FRONT_STRUT ? CarSlot::FRONT_COIL_SPRING : CarSlot::TIRE,
			m_carFitTarget->index))
			if (auto* visual = Find(ItemRef::Part(carried->GetId())))
				SetPlace(*visual, ItemPlace::CAR);
}

std::optional<ShopWorld::FastenerTarget> ShopWorld::FindCarFastenerTarget(const Workshop& workshop, const FirstPersonController& player, int sizeMm)
{
	const auto* build = workshop.GetCar();
	if (!build || m_carStation >= m_stations.size() || !m_stations[m_carStation].present)
		return std::nullopt;
	glm::vec3 eye = player.GetEyePosition();
	glm::vec3 forward = player.GetForward();
	const auto& car = build->assembly;
	std::optional<FastenerTarget> best;
	bool bestFits = false;
	float bestOff = 0.f;
	for (int s = static_cast<int>(CarSlot::STRUT_MOUNT_NUT); s < static_cast<int>(CarSlot::COUNT); s++)
	{
		auto slot = static_cast<CarSlot>(s);
		for (int index = 0; index < car.SlotCount(slot); index++)
		{
			auto part = car.Get(slot, index);
			if (!part)
				continue;
			auto pose = PoseOnCar(slot, index);
			float face = PartBounds(part->GetType()).second.y * 0.85f;
			glm::vec3 position = CarToWorld(pose.offset + pose.rotation * glm::vec3(0.f, face, 0.f));
			float along = glm::dot(position - eye, forward);
			if (along <= 0.f || along > INTERACT_DISTANCE + 0.5f)
				continue;
			float off = glm::length(position - (eye + forward * along));
			// Within about 1.5 degrees of the crosshair, the ones this spanner fits first.
			if (off >= along * 0.026f)
				continue;
			bool fits = !sizeMm || PartCatalog::Get(part->GetType()).fastener.headMm == sizeMm;
			if (!best || (fits && !bestFits) || (fits == bestFits && off < bestOff))
			{
				best = FastenerTarget{ -1, AssemblySlot::COUNT, index, position, true, slot };
				bestFits = fits;
				bestOff = off;
			}
		}
	}
	return best;
}

