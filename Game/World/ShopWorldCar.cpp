// The project car in the 3D shop: where each part sits on it, fitting parts to it by hand, and its nuts and bolts.
#include "ShopWorld.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <glm/gtc/matrix_transform.hpp>

#include "../Audio/Sounds.h"
#include "../Controls.h"
#include "CarBody.h"
#include "Roads.h"
#include "ShopWorldShared.h"

using namespace ShopLayout;

namespace
{
	// The car's space: +X its nose, +Y up from the ground under its wheels, +Z its right side (as you sit in it).
	using CarBody::FRONT_AXLE_X;
	using CarBody::REAR_AXLE_X;
	constexpr float WHEEL_Z = 0.69f;		// the tires' middles, either side
	constexpr float WHEEL_Y = 0.29f;		// the axle height: the tire's radius
	constexpr float BODY_SIDE_Z = 0.8f;		// the panels' outer faces
	constexpr float KINGPIN_Z = 0.55f;		// the front struts' axes, either side: what the steered wheels turn about
	// The drivetrain's line: the crank's axis, the back of the block the bellhousing bolts to, and the top plates of
	// the engine's mounts.
	constexpr float SHAFT_Y = CarBody::CRANK_AXIS_Y;
	constexpr float BLOCK_REAR = CarBody::BLOCK_REAR_X;
	constexpr float MOUNT_TOP_Y = CarBody::ENGINE_MOUNT_Y + 0.072f;
	constexpr float GEARBOX_X = BLOCK_REAR - 0.3f;			// the gearbox's own middle: its bellhousing's mouth 0.3 ahead
	constexpr float GEARBOX_TAIL_X = GEARBOX_X - 0.64f;
	constexpr float DIFF_NOSE_X = CarBody::REAR_AXLE_X + 0.25f;
	// The brake booster on the firewall in front of the driver, clear of the wheel's housing.
	constexpr float BOOSTER_Y = 0.7f, BOOSTER_Z = -0.33f;

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
	// What's on a front knuckle (the hub, the brake, the wheel) turns with the steering about its strut's axis.
	auto steered = [this](float s, const CarPose& pose) {
		glm::vec3 pivot = { FRONT_AXLE_X, 0.f, s * KINGPIN_Z };
		glm::quat turn = TurnY(m_drive.steer);
		return CarPose{ pivot + turn * (pose.offset - pivot), turn * pose.rotation };
	};
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
			// They go round with the wheel, and turn with it when it's steered.
			float turned = angle - m_drive.spin;
			glm::vec3 local = { std::cos(turned) * 0.05f, std::sin(turned) * 0.05f, side * 0.075f };
			CarPose nut = { glm::vec3(x, WHEEL_Y, side * WHEEL_Z) + local, FacingSide(side) };
			return x > 0.f ? steered(side, nut) : nut;
		}

		// The engine's own mounting nuts sit on its mounts.
		if (parent == CarSlot::ENGINE)
			return { { CarBody::ENGINE_X, MOUNT_TOP_Y, (hole == 0 ? -1.f : 1.f) * (CarBody::ENGINE_MOUNT_Z + 0.025f) }, NONE };

		// The body panels' bolts, each where it really goes (the models have the lips, hinges and posts they're in).
		using namespace CarBody;
		const glm::quat FACING_BACK = Pitch(glm::half_pi<float>());		// its head toward the tail
		const glm::quat FACING_FRONT = Pitch(-glm::half_pi<float>());	// its head toward the nose
		const glm::quat HEAD_DOWN = Roll(glm::pi<float>());
		float panelSide = SideOf(parent, parentIndex);
		// The rest, each on the part it holds where a spanner gets at it. For those that come in pairs, which end;
		// for fours, which pair and which of it; and for a ring of them round a shaft, where on the ring.
		const float s = panelSide;
		const float end = hole == 0 ? -1.f : 1.f;
		const float pair = hole < 2 ? -1.f : 1.f, of = hole % 2 == 0 ? -1.f : 1.f;
		auto ring = [&](const glm::vec3& middle, float radius, int count) {
			float turn = glm::two_pi<float>() * (hole + 0.5f) / static_cast<float>(count);
			return middle + glm::vec3(0.f, std::sin(turn) * radius, std::cos(turn) * radius);
		};
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
			// Front suspension. The strut's top mount hangs under the tower's plate, its three nuts on top of it; the
			// knuckle clamps the strut's foot, and the control arm's ball joint bolts up under it.
			case CarSlot::STRUT_MOUNT_NUT:
			{
				float turn = glm::two_pi<float>() * hole / 3.f;
				return { { FRONT_AXLE_X + std::cos(turn) * 0.055f, 0.755f, s * 0.55f + std::sin(turn) * 0.055f }, NONE };
			}
			case CarSlot::STRUT_KNUCKLE_BOLT:	return steered(s, { { FRONT_AXLE_X + end * 0.03f, 0.43f, s * 0.59f }, FacingSide(s) });
			case CarSlot::BALL_JOINT_BOLT:		return { { FRONT_AXLE_X + end * 0.03f, 0.14f, s * 0.55f }, HEAD_DOWN };
			case CarSlot::CONTROL_ARM_BOLT:		return { { FRONT_AXLE_X + 0.06f, 0.165f, s * 0.18f }, FACING_FRONT };
			case CarSlot::STRUT_ROD_NUT:		return hole == 0 ? CarPose{ { 0.705f, 0.202f, s * 0.42f }, FACING_BACK } : CarPose{ { 0.76f, 0.202f, s * 0.42f }, FACING_FRONT };
			case CarSlot::SWAY_BAR_BOLT:		return { { FRONT_AXLE_X + 0.35f + of * 0.03f, 0.17f, pair * 0.35f }, HEAD_DOWN };
			// Steering: the tie rod end's nut on top of the knuckle's arm, the rack's clamps either side of its middle.
			case CarSlot::TIE_ROD_END_NUT:		return steered(s, { { FRONT_AXLE_X + 0.18f, CarBody::TIE_ROD_Y + 0.025f, s * KINGPIN_Z }, NONE });
			case CarSlot::STEERING_RACK_BOLT:	return { { FRONT_AXLE_X + 0.2f + of * 0.045f, CarBody::TIE_ROD_Y + 0.02f, pair * 0.2f }, NONE };
			case CarSlot::CALIPER_BOLT:			return steered(s, { { FRONT_AXLE_X + 0.085f, 0.265f + hole * 0.05f, s * 0.645f }, FacingSide(s) });
			// Rear suspension: through the eyes at each end of the links and the lateral rod, and the shock's two ends.
			case CarSlot::REAR_LINK_BOLT:		return { { hole == 0 ? REAR_AXLE_X + 0.01f : REAR_AXLE_X + 0.59f, 0.196f, s * 0.516f }, FacingSide(s) };
			case CarSlot::UPPER_LINK_BOLT:		return { { hole == 0 ? REAR_AXLE_X + 0.06f : REAR_AXLE_X + 0.38f, 0.374f, s * 0.294f }, FacingSide(s) };
			case CarSlot::LATERAL_ROD_BOLT:		return { { REAR_AXLE_X - 0.166f, 0.316f, end * 0.47f }, FACING_BACK };
			case CarSlot::REAR_SHOCK_BOLT:		return { { REAR_AXLE_X - 0.124f, hole == 0 ? 0.25f : 0.64f, s * 0.56f }, FACING_BACK };
			case CarSlot::DIFFERENTIAL_NUT:		return { ring({ REAR_AXLE_X + 0.08f, WHEEL_Y, 0.f }, 0.105f, 8), FACING_FRONT };
			// Drivetrain: down through each engine mount either side of its stud, round the clutch's cover and the
			// bellhousing's flange, up into the gearbox's mount, and round the driveshaft's flange at the diff.
			case CarSlot::ENGINE_MOUNT_BOLT:	return { { CarBody::ENGINE_X + end * 0.045f, MOUNT_TOP_Y, s * (CarBody::ENGINE_MOUNT_Z + 0.02f) }, NONE };
			case CarSlot::PRESSURE_PLATE_BOLT:	return { ring({ BLOCK_REAR - 0.105f, SHAFT_Y, 0.f }, 0.095f, 6), FACING_BACK };
			case CarSlot::BELLHOUSING_BOLT:		return { ring({ BLOCK_REAR - 0.012f, SHAFT_Y, 0.f }, 0.168f, 4), FACING_BACK };
			case CarSlot::TRANSMISSION_MOUNT_BOLT: return { { GEARBOX_X - 0.3f + of * 0.02f, 0.16f, pair * 0.09f }, HEAD_DOWN };
			case CarSlot::DRIVESHAFT_FLANGE_NUT: return { ring({ REAR_AXLE_X + 0.24f, WHEEL_Y, 0.f }, 0.05f, 4), FACING_FRONT };
			case CarSlot::STARTER_BOLT:			return { { BLOCK_REAR - 0.135f, SHAFT_Y - 0.025f + end * 0.06f, -0.2f }, FACING_BACK };
			// On the firewall: the master cylinder's two studs on the booster's face, the booster's four round it.
			case CarSlot::MASTER_CYLINDER_NUT:	return { { 1.06f, BOOSTER_Y, BOOSTER_Z + end * 0.04f }, FACING_FRONT };
			case CarSlot::BOOSTER_NUT:			return { ring({ 0.9f, BOOSTER_Y, BOOSTER_Z }, 0.125f, 4), FACING_FRONT };
			// The tank's four on its corners in the trunk, each seat's four at the feet of its frame on the floor.
			case CarSlot::FUEL_TANK_BOLT:		return { { -1.72f + of * 0.27f, 0.49f, pair * 0.37f }, NONE };
			case CarSlot::SEAT_BOLT:			return { { -0.28f + of * 0.235f, 0.22f, s * 0.37f + pair * 0.17f }, NONE };
			case CarSlot::CATALYTIC_CONVERTER_NUT: return { ring({ 0.352f, 0.12f, 0.28f }, 0.045f, 3), FACING_FRONT };
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
		case C::HEADLIGHT:
		{
			// A pop-up: hinged along the back edge of its lid, which lies in the hood's surface till it swings up.
			using namespace CarBody;
			float x = NOSE_X - LAMP_LID_LENGTH + GAP;
			float up = glm::smoothstep(0.f, 1.f, m_headlights.amount);
			return { { x, HoodY(x), side * (LAMP_LID_INNER + HOOD_HALF) * 0.5f }, Pitch(HoodPitch() + LAMP_POP * up) * ACROSS };
		}
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
		case C::INSTRUMENT_CLUSTER:	return { { 0.39f, 0.705f, -0.37f }, ACROSS };	// in the dash's face, under its hood
		case C::SEAT:				return { { -0.3f, 0.22f, side * 0.37f }, NONE };
		case C::STEERING_WHEEL:		return { { 0.22f, 0.78f, -0.37f }, Pitch(1.17f) * TurnY(m_drive.steer * 4.f) };	// on the column's end, square to it
		// Front suspension, round the front wheels.
		case C::FRONT_STRUT:
		case C::FRONT_COIL_SPRING:	return steered(side, { { FRONT_AXLE_X, 0.165f, side * KINGPIN_Z }, NONE });	// its top mount up under the tower's plate
		// The knuckle round the strut's foot, its spindle at the wheel's middle and its arm forward to the tie rod.
		case C::STEERING_KNUCKLE:	return steered(side, { { FRONT_AXLE_X, WHEEL_Y - 0.1f, side * KINGPIN_Z }, NONE });
		case C::LOWER_CONTROL_ARM:	return { { FRONT_AXLE_X, 0.14f, side * 0.37f }, ACROSS };
		case C::STRUT_ROD:			return { { FRONT_AXLE_X - 0.25f, 0.19f, side * 0.42f }, NONE };	// trailing back from the arm to the floor
		case C::FRONT_SWAY_BAR:		return { { FRONT_AXLE_X + 0.35f, 0.17f, 0.f }, ACROSS };
		// Rear suspension: the axle across under the boot floor, on its links.
		case C::REAR_AXLE:			return { { REAR_AXLE_X, WHEEL_Y - 0.15f, 0.f }, ACROSS };
		case C::DIFFERENTIAL:		return { { REAR_AXLE_X, WHEEL_Y - 0.14f, 0.f }, TurnY(glm::half_pi<float>()) };
		case C::LOWER_TRAILING_LINK: return { { REAR_AXLE_X + 0.3f, 0.18f, side * 0.5f }, NONE };
		case C::UPPER_TRAILING_LINK: return { { REAR_AXLE_X + 0.22f, 0.36f, side * 0.28f }, NONE };
		case C::LATERAL_ROD:		return { { REAR_AXLE_X - 0.15f, 0.3f, 0.f }, ACROSS };
		case C::REAR_COIL_SPRING:	return { { REAR_AXLE_X + 0.05f, 0.33f, side * 0.5f }, NONE };	// sat on the axle
		case C::REAR_SHOCK:			return { { REAR_AXLE_X - 0.1f, 0.22f, side * 0.56f }, NONE };
		// Steering
		// The rack ahead of the axle, under the engine's nose (its pinion to the driver's side), a tie rod end from each
		// of its ends out to the knuckle's arm; the column from the firewall up to the wheel.
		case C::STEERING_RACK:		return { { FRONT_AXLE_X + 0.2f, CarBody::TIE_ROD_Y - 0.026f, 0.f }, TurnY(-glm::half_pi<float>()) };
		case C::TIE_ROD_END:
		{
			// Pushed and pulled by the rack: it goes with the end of the knuckle's arm.
			const glm::vec3 arm = { 0.18f, 0.f, 0.f };
			return { glm::vec3(FRONT_AXLE_X + 0.2f, CarBody::TIE_ROD_Y - 0.012f, side * 0.47f) + TurnY(m_drive.steer) * arm - arm, ACROSS };
		}
		case C::STEERING_COLUMN:	return { { 0.55f, 0.61f, -0.37f }, Pitch(-0.4f) };
		// Brakes: the booster and master cylinder on the driver's side of the firewall, the lines along the floor.
		case C::BRAKE_BOOSTER:		return { { 0.98f, BOOSTER_Y - 0.11f, BOOSTER_Z }, NONE };
		case C::BRAKE_MASTER_CYLINDER: return { { 1.15f, BOOSTER_Y - 0.025f, BOOSTER_Z }, NONE };
		case C::BRAKE_LINE_SET:		return { { -0.2f, 0.15f, -0.3f }, NONE };
		case C::BRAKE_HOSE:			return index < 2 ? CarPose{ { FRONT_AXLE_X - 0.08f, 0.3f, side * 0.42f }, NONE }
										: CarPose{ { REAR_AXLE_X + 0.15f, 0.3f, 0.12f }, NONE };
		case C::BRAKE_ROTOR:		return steered(side, { { FRONT_AXLE_X, WHEEL_Y, side * 0.6f }, Pitch(-m_drive.spin) * FacingSide(side) });
		// The caliper over the rotor's front edge with the pads in it; the shoes inside the drum, the wheel cylinder behind.
		case C::BRAKE_PAD_SET:		return steered(side, { { FRONT_AXLE_X + 0.085f, WHEEL_Y - 0.015f, side * 0.605f }, NONE });
		case C::BRAKE_CALIPER:		return steered(side, { { FRONT_AXLE_X + 0.085f, WHEEL_Y - 0.05f, side * 0.605f }, NONE });
		case C::WHEEL_CYLINDER:		return { { REAR_AXLE_X, WHEEL_Y + 0.07f, side * 0.58f }, ACROSS };
		case C::BRAKE_SHOE_SET:		return { { REAR_AXLE_X, WHEEL_Y - 0.025f, side * 0.63f }, NONE };
		case C::BRAKE_DRUM:			return { { REAR_AXLE_X, WHEEL_Y, side * 0.6f }, Pitch(-m_drive.spin) * FacingSide(side) };
		case C::PARKING_BRAKE_CABLE: return { { -0.5f, 0.15f, 0.05f }, NONE };
		// Drivetrain: the clutch behind the engine's flywheel, the gearbox down the tunnel to the driveshaft.
		// (The mounts on their perches either side of the crankcase, each one's bracket in to the block.)
		case C::ENGINE_MOUNT:		return { { CarBody::ENGINE_X, CarBody::ENGINE_MOUNT_Y, side * CarBody::ENGINE_MOUNT_Z }, side < 0.f ? TurnY(glm::pi<float>()) : NONE };
		case C::CLUTCH_DISC:		return { { BLOCK_REAR - 0.025f, SHAFT_Y, 0.f }, Pitch(glm::half_pi<float>()) };
		case C::PRESSURE_PLATE:		return { { BLOCK_REAR - 0.065f, SHAFT_Y, 0.f }, Pitch(glm::half_pi<float>()) };
		case C::RELEASE_BEARING:	return { { BLOCK_REAR - 0.125f, SHAFT_Y, 0.f }, Pitch(glm::half_pi<float>()) };
		case C::TRANSMISSION:		return { { GEARBOX_X, SHAFT_Y - 0.15f, 0.f }, TurnY(glm::pi<float>()) };
		case C::TRANSMISSION_MOUNT:	return { { GEARBOX_X - 0.3f, 0.16f, 0.f }, ACROSS };
		case C::CLUTCH_MASTER_CYLINDER: return { { 0.93f, 0.66f, -0.56f }, NONE };
		case C::CLUTCH_RELEASE_CYLINDER: return { { BLOCK_REAR - 0.15f, SHAFT_Y - 0.09f, 0.175f }, NONE };
		case C::DRIVESHAFT:
		{
			// From the gearbox's tail down to the diff's nose.
			float run = GEARBOX_TAIL_X - DIFF_NOSE_X, drop = SHAFT_Y - WHEEL_Y;
			return { { (GEARBOX_TAIL_X + DIFF_NOSE_X) * 0.5f, (SHAFT_Y + WHEEL_Y) * 0.5f - 0.035f, 0.f }, Pitch(std::atan2(drop, run)) };
		}
		// Fuel, under the boot floor and along the right of the floor.
		case C::FUEL_TANK:			return { { -1.72f, 0.24f, 0.f }, ACROSS };
		case C::FUEL_SENDER:		return { { -1.6f, 0.3f, 0.18f }, NONE };	// down in the tank, its top just proud of it
		case C::FUEL_LINE_SET:		return { { 0.f, 0.15f, 0.35f }, NONE };
		case C::FUEL_FILTER:		return { { 0.93f, 0.625f, 0.5f }, NONE };
		// Electrical
		// The battery on its tray behind the left headlight, the main loom across behind the dash and the engine's
		// across the firewall, the fuse box up under the dash by the driver's door, the coil on the right inner wing.
		case C::BATTERY:			return { { 1.66f, 0.52f, -0.5f }, NONE };
		case C::MAIN_HARNESS:		return { { 0.82f, 0.56f, 0.f }, ACROSS };
		case C::ENGINE_HARNESS:		return { { 0.915f, 0.74f, 0.2f }, ACROSS };
		case C::FUSE_BOX:			return { { 0.6f, 0.5f, -0.6f }, NONE };
		case C::IGNITION_COIL:		return { { 1.5f, 0.6f, 0.61f }, NONE };
		case C::STARTER:			return { { BLOCK_REAR - 0.045f, SHAFT_Y - 0.07f, -0.2f }, NONE };
		// Cooling: the radiator across the nose, the fan behind it.
		case C::RADIATOR:			return { { 1.98f, 0.24f, 0.f }, ACROSS };
		case C::RADIATOR_HOSE:		return index == 0 ? CarPose{ { 1.72f, 0.68f, 0.15f }, NONE } : CarPose{ { 1.72f, 0.32f, -0.2f }, NONE };
		case C::COOLING_FAN:		return { { 1.94f, 0.45f, 0.f }, Pitch(glm::half_pi<float>()) };	// against the back of the radiator's core
		// Exhaust: from the downpipe back under the right of the floor to the tailpipe at the back.
		case C::CATALYTIC_CONVERTER: return { { 0.15f, 0.07f, 0.28f }, NONE };
		case C::MUFFLER:			return { { -1.55f, 0.03f, 0.45f }, TurnY(glm::pi<float>()) };
		// Wheels, at their corners: the tire round the wheel.
		case C::WHEEL:
		case C::TIRE:
		{
			// Rolled round its axle (the model stands on the ground, its axle the tire's radius up) by how far the car's
			// gone, and the front ones turned by the steering with their knuckles.
			glm::quat turn = ACROSS * Roll(m_drive.spin);
			CarPose rolled = { glm::vec3(axleX, WHEEL_Y, side * WHEEL_Z) - turn * glm::vec3(0.f, WHEEL_Y, 0.f), turn };
			return front ? steered(side, rolled) : rolled;
		}
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
	if (m_quarterGlass)
	{
		m_quarterGlass->SetVisible(present);
		m_quarterGlass->SetPickable(present);
		m_targets[m_quarterGlass.get()] = { TargetKind::STATION, {}, 0, m_carStation };
		m_quarterGlass->SetPosition(CarToWorld({}));
		m_quarterGlass->SetRotation(CarRotation());
	}
	// Each door's glass goes where the door is, while the door is on the car.
	for (int side = 0; side < 2; side++)
		if (const auto& glass = m_doorGlass[side])
		{
			bool on = present && build->assembly.IsFilled(CarSlot::DOOR, side);
			glass->SetVisible(on);
			glass->SetPickable(on);
			if (on)
			{
				// Aimed at through its glass, it's the door that's aimed at.
				m_targets[glass.get()] = { TargetKind::ITEM, ItemRef::Part(build->assembly.Get(CarSlot::DOOR, side)->GetId()), 0, 0 };
				auto pose = PoseOnCar(CarSlot::DOOR, side);
				glass->SetPosition(CarToWorld(pose.offset));
				glass->SetRotation(CarRotation() * pose.rotation);
			}
		}
	if (m_fuelFlap)
	{
		m_fuelFlap->SetVisible(present);
		m_fuelFlap->SetPosition(CarToWorld({ CarBody::FLAP_X + CarBody::FLAP_HALF, CarBody::FLAP_Y, CarBody::SIDE }));
		m_fuelFlap->SetRotation(CarRotation() * TurnY(CarBody::FLAP_OPEN * CarPanelOpen(CarPanel::FUEL_FLAP)));
	}
	PoseCarInstruments(workshop, deltaTime);
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


// Driving it.
namespace
{
	constexpr float WHEELBASE = FRONT_AXLE_X - REAR_AXLE_X;
	constexpr float CAR_HALF_LENGTH = 2.12f;
	constexpr float CAR_HALF_WIDTH = 0.83f;
	constexpr float REVERSE_SPEED = 7.f;		// m/s
	constexpr float REDLINE = 6200.f;
	constexpr float GET_OUT_SPEED = 0.6f;		// m/s: it has to be all but stopped
	// Each gear runs out at this much of the car's top speed.
	constexpr float GEAR_TOP[] = { 0.2f, 0.24f, 0.42f, 0.62f, 0.82f, 1.02f };	// reverse, then first to fifth
	// Where the head of whoever's in each seat is: the driver's (on the left), the front passenger's, and the two
	// places on the back seat.
	const glm::vec3 SEAT_HEADS[] = { { -0.36f, 1.07f, -0.37f }, { -0.36f, 1.07f, 0.37f }, { -1.0f, 1.03f, -0.33f }, { -1.0f, 1.03f, 0.33f } };
	const char* GEAR_NAMES[] = { "R", "1st", "2nd", "3rd", "4th", "5th" };

	// "1:23.4"
	std::string LapText(float seconds)
	{
		int minutes = static_cast<int>(seconds) / 60;
		return std::format("{}:{:04.1f}", minutes, seconds - static_cast<float>(minutes * 60));
	}

	// Whether a box lying on the ground (its middle, the way its length runs, half its length and width) overlaps
	// an upright one.
	bool BoxesOverlap(glm::vec2 middle, glm::vec2 along, float halfLength, float halfWidth, glm::vec2 boxMin, glm::vec2 boxMax)
	{
		glm::vec2 across = { -along.y, along.x };
		glm::vec2 boxMiddle = (boxMin + boxMax) * 0.5f;
		glm::vec2 boxHalf = (boxMax - boxMin) * 0.5f;
		glm::vec2 between = boxMiddle - middle;
		// Apart along the world's axes?
		float reachX = std::abs(along.x) * halfLength + std::abs(across.x) * halfWidth;
		float reachZ = std::abs(along.y) * halfLength + std::abs(across.y) * halfWidth;
		if (std::abs(between.x) > reachX + boxHalf.x || std::abs(between.y) > reachZ + boxHalf.y)
			return false;
		// Or along the car's own?
		auto reach = [&](glm::vec2 axis) { return std::abs(axis.x) * boxHalf.x + std::abs(axis.y) * boxHalf.y; };
		return std::abs(glm::dot(between, along)) <= halfLength + reach(along) && std::abs(glm::dot(between, across)) <= halfWidth + reach(across);
	}
}

bool ShopWorld::CarBlocked(const glm::vec3& position, float yaw) const
{
	glm::vec2 middle = { position.x, position.z };
	glm::vec2 along = { std::cos(yaw), -std::sin(yaw) };
	// The world's edge.
	auto bounds = GetRoomBounds();
	for (float end : { -1.f, 1.f })
		for (float side : { -1.f, 1.f })
		{
			glm::vec2 corner = middle + along * (end * CAR_HALF_LENGTH) + glm::vec2(-along.y, along.x) * (side * CAR_HALF_WIDTH);
			if (corner.x < bounds.min.x || corner.x > bounds.max.x || corner.y < bounds.min.y || corner.y > bounds.max.y)
				return true;
		}
	// Anything that stands in the way of its body: not what its wheels roll over, nor what's above its roof.
	for (const auto& obstacle : m_obstacles)
		if (obstacle.top > 0.2f && obstacle.bottom < CarBody::ROOF_Y
			&& BoxesOverlap(middle, along, CAR_HALF_LENGTH, CAR_HALF_WIDTH, obstacle.min, obstacle.max))
			return true;
	return false;
}

bool ShopWorld::OnPaving(glm::vec2 point) const
{
	using namespace GarageLayout;
	auto in = [&](glm::vec2 low, glm::vec2 high) { return point.x >= low.x && point.x <= high.x && point.y >= low.y && point.y <= high.y; };
	// The shop's own floors and the apron round them, its drive, and the neighbours' drives.
	if (in({ BAY_MIN_X - 1.f, -HALF_DEPTH - 1.2f }, { HALF_WIDTH + 1.2f, HALF_DEPTH + 0.3f }))
		return true;
	for (const auto& paved : PAVED)
		if (in(paved.min - glm::vec2(0.5f), paved.max + glm::vec2(0.5f)))
			return true;
	if (m_bayBuilt && (in(BAY_DRIVE.min, BAY_DRIVE.max) || in(BAY_DRIVE_LINK.min, BAY_DRIVE_LINK.max)))
		return true;
	for (const auto& building : m_neighbourObstacles)
	{
		float offset = (building.min.x + building.max.x) * 0.5f;
		if (in({ offset + DRIVE_MIN_X - 0.5f, -HALF_DEPTH - 1.2f }, { offset + DRIVE_MAX_X + 0.5f, HIGHWAY_NEAR_Z }))
			return true;
	}
	return Roads::On(point, 0.4f);
}

std::optional<int> ShopWorld::AimedSeat(const Workshop& workshop, const FirstPersonController& player) const
{
	const auto* build = workshop.GetCar();
	if (!build || !m_hover || m_carStation >= m_stations.size() || !m_stations[m_carStation].present)
		return std::nullopt;
	// Aimed at the car, any of it.
	bool atCar = m_hover->kind == TargetKind::STATION && m_hover->stationIndex == m_carStation;
	if (m_hover->kind == TargetKind::ITEM)
		if (const auto* visual = Find(m_hover->item))
			atCar = visual->place == ItemPlace::CAR;
	if (!atCar)
		return std::nullopt;
	// The seat nearest the line of sight: a front one has to be in the car to be sat in (the back seat is part of it).
	glm::vec3 eye = player.GetEyePosition(), forward = player.GetForward();
	std::optional<int> nearest;
	float nearestOff = 0.f;
	for (int seat = 0; seat < SEATS; seat++)
	{
		if (seat < 2 && !build->assembly.IsFilled(CarSlot::SEAT, seat))
			continue;
		glm::vec3 at = CarToWorld(SEAT_HEADS[seat] - glm::vec3(0.f, 0.4f, 0.f));
		float along = (std::max)(glm::dot(at - eye, forward), 0.f);
		float off = glm::length(at - (eye + forward * along));
		if (!nearest || off < nearestOff)
		{
			nearest = seat;
			nearestOff = off;
		}
	}
	return nearest;
}

void ShopWorld::Sit(int seat, Workshop& workshop)
{
	if (m_seat >= 0 || m_carStation >= m_stations.size() || !m_stations[m_carStation].present)
		return;
	m_seat = std::clamp(seat, 0, SEATS - 1);
	m_seatJustIn = true;
	m_seatLastYaw = m_stations[m_carStation].yaw;
	// Nothing in hand in there.
	m_handEmpty = true;
	SyncHand();
	Cue(Sound::SET_DOWN, GetCarPosition());
	// In the driver's seat the key's turned straight away (they still sit there if it won't go).
	if (m_seat == 0)
		StartDrive(workshop);
}

bool ShopWorld::DevStartDrive(Workshop& workshop)
{
	if (m_seat != 0)
	{
		LeaveSeat(nullptr);
		m_seat = 0;
		m_seatJustIn = true;
	}
	return m_drive.active || StartDrive(workshop);
}

bool ShopWorld::StartDrive(Workshop& workshop)
{
	if (m_drive.active || m_seat != 0 || m_carStation >= m_stations.size() || !m_stations[m_carStation].present)
		return false;
	auto car = workshop.StartCar();
	if (!car.goes)
	{
		Toast(car.problem);
		Cue(Sound::CLICK, GetCarPosition());
		return false;
	}
	// What it'll do, from how it's built: the pull off the line from its time to 100 km/h (one that never gets there
	// crawls up to what it can do), the brakes from how far it takes to stop, the cornering from its grip.
	float devSeconds = m_drive.devSeconds, devSteer = m_drive.devSteer;
	m_drive = {};
	m_drive.devSeconds = devSeconds;
	m_drive.devSteer = devSteer;
	m_drive.active = true;
	m_drive.topSpeed = (std::max)(static_cast<float>(car.topSpeed) / 3.6f, 3.f);
	m_drive.pull = car.zeroToHundred > 0.0 ? std::clamp(27.8f / static_cast<float>(car.zeroToHundred) * 1.7f, 0.8f, 7.f)
		: std::clamp(m_drive.topSpeed / 25.f, 0.5f, 2.f);
	m_drive.braking = car.brakingDistance > 0.0 ? std::clamp(386.f / static_cast<float>(car.brakingDistance), 2.f, 11.f) : 6.f;
	m_drive.grip = std::clamp(static_cast<float>(car.lateralGrip), 0.3f, 1.3f) * 9.81f;
	m_drive.idleRpm = car.idleRpm > 0.0 ? static_cast<float>(car.idleRpm) : 850.f;
	m_drive.rpm = m_drive.idleRpm;
	// Everything shut before it moves off.
	for (auto& panel : m_carPanels)
		panel.open = false;
	// It's no longer in its own way.
	RebuildCollision();
	return true;
}

void ShopWorld::EndDrive()
{
	if (!m_drive.active)
		return;
	m_drive.active = false;
	m_drive.speed = 0.f;
	m_drive.steer = 0.f;
	m_supportChanged = true;
	m_carLaidOutAt = glm::vec3(-1e9f);	// its wheels are put straight again
	RebuildCollision();
}

void ShopWorld::LeaveSeat(FirstPersonController* player)
{
	if (m_seat < 0)
		return;
	EndDrive();
	// Out beside the door of the seat they were in, or wherever round the car there's room to stand (the car's own
	// box isn't in the way of that).
	if (player)
	{
		const float side = m_seat % 2 == 0 ? -1.f : 1.f, along = m_seat < 2 ? 0.2f : -0.7f;
		const glm::vec3 SPOTS[] = { { along, 0.f, side * 1.35f }, { along, 0.f, -side * 1.35f }, { -2.7f, 0.f, 0.f }, { 2.7f, 0.f, 0.f },
			{ -1.f, 0.f, side * 1.35f }, { -1.f, 0.f, -side * 1.35f } };
		glm::vec3 out = CarToWorld(SPOTS[0]);
		for (const auto& spot : SPOTS)
		{
			glm::vec3 at = CarToWorld(spot);
			bool clear = true;
			for (size_t i = 0; i < m_obstacles.size(); i++)
			{
				const auto& obstacle = m_obstacles[i];
				if (obstacle.top > 0.3f && obstacle.bottom < 1.5f && at.x > obstacle.min.x - 0.3f && at.x < obstacle.max.x + 0.3f
					&& at.z > obstacle.min.y - 0.3f && at.z < obstacle.max.y + 0.3f)
					clear = false;
			}
			if (clear)
			{
				out = at;
				break;
			}
		}
		player->SetPosition(out);
	}
	m_seat = -1;
	Cue(Sound::SET_DOWN, GetCarPosition());
}

void ShopWorld::ToggleHeadlights(const Workshop& workshop)
{
	const auto* build = workshop.GetCar();
	if (!build)
		return;
	const auto& car = build->assembly;
	if (!m_headlights.on && !car.IsFilled(CarSlot::BATTERY, 0))
	{
		Toast("Nothing: there's no battery in it.");
		return;
	}
	if (!m_headlights.on && !car.IsFilled(CarSlot::HEADLIGHT, 0) && !car.IsFilled(CarSlot::HEADLIGHT, 1))
	{
		Toast("It has no headlights on it.");
		return;
	}
	m_headlights.on = !m_headlights.on;
	Cue(Sound::CLICK, GetCarPosition());
}

std::vector<PointLight> ShopWorld::GetCarLights() const
{
	std::vector<PointLight> lights;
	if (!m_headlights.on || m_headlights.amount < 0.9f || m_carStation >= m_stations.size() || !m_stations[m_carStation].present)
		return lights;
	// The pool of light on the road ahead: a bright patch in front of the bumper, and a wider, dimmer one beyond it.
	const glm::vec3 BEAM = { 1.f, 0.95f, 0.8f };
	lights.push_back({ CarToWorld({ CarBody::NOSE_X + 3.5f, 1.3f, 0.f }), 10.f, BEAM * 9.f, LightZone::EVERYWHERE });
	lights.push_back({ CarToWorld({ CarBody::NOSE_X + 13.f, 2.5f, 0.f }), 22.f, BEAM * 11.f, LightZone::EVERYWHERE });
	return lights;
}

void ShopWorld::Drive(float deltaTime, Workshop& workshop, FirstPersonController& player, const InputManager& input, bool playerHasControl)
{
	if (m_seat < 0 || m_carStation >= m_stations.size())
		return;
	deltaTime = (std::min)(deltaTime, 0.05f);
	auto& station = m_stations[m_carStation];
	// The car gone from under them: out they get.
	if (!station.present)
		return LeaveSeat(&player);
	const bool justIn = m_seatJustIn;
	if (justIn)
	{
		// Facing out through the windshield.
		player.SetYaw(station.yaw - glm::half_pi<float>());
		player.SetPitch(-0.06f);
		m_seatJustIn = false;
		m_seatLastYaw = station.yaw;
	}
	if (playerHasControl)
		player.UpdateLook(input);

	if (m_drive.active)
		MoveCar(deltaTime, workshop, input, playerHasControl);
	else if (m_seat == 0 && playerHasControl && !justIn && Controls::Pressed(input, Control::FORWARD))
		StartDrive(workshop);	// the key, turned again
	if (m_seat == 0 && playerHasControl && Controls::Pressed(input, Control::HEADLIGHTS))
		ToggleHeadlights(workshop);

	// Whoever's in it goes with it: kept in their seat, and turned as the car turns.
	float turned = std::remainder(station.yaw - m_seatLastYaw, glm::two_pi<float>());
	m_seatLastYaw = station.yaw;
	player.SetYaw(player.GetYaw() + turned);
	player.SetPosition(CarToWorld(SEAT_HEADS[m_seat]) - (player.GetEyePosition() - player.GetFeetPosition()));

	// [F] gets out, once it's stopped.
	if (playerHasControl && !justIn && Controls::Pressed(input, Control::MOVE))
	{
		if (!m_drive.active || std::abs(m_drive.speed) <= GET_OUT_SPEED)
			LeaveSeat(&player);
		else
			Toast("Stop the car before getting out.");
	}
}

void ShopWorld::MoveCar(float deltaTime, Workshop& workshop, const InputManager& input, bool playerHasControl)
{
	auto& station = m_stations[m_carStation];
	auto& drive = m_drive;
	// The pedals and the wheel. Forward pulls away (or brakes, going backward); back brakes, then reverses.
	float go = 0.f, steerTo = 0.f;
	bool handbrake = false;
	if (playerHasControl)
	{
		go = (Controls::Held(input, Control::FORWARD) ? 1.f : 0.f) - (Controls::Held(input, Control::BACK) ? 1.f : 0.f);
		steerTo = (Controls::Held(input, Control::LEFT) ? 1.f : 0.f) - (Controls::Held(input, Control::RIGHT) ? 1.f : 0.f);
		handbrake = Controls::Held(input, Control::JUMP);
	}
	// (The developer's foot: forward for so long, or backward for a time given as less than nothing.)
	if (drive.devSeconds != 0.f)
	{
		go = drive.devSeconds > 0.f ? 1.f : -1.f;
		drive.devSeconds = go > 0.f ? (std::max)(0.f, drive.devSeconds - deltaTime) : (std::min)(0.f, drive.devSeconds + deltaTime);
		steerTo = drive.devSteer;
	}
	bool forward = drive.speed > 0.05f, backward = drive.speed < -0.05f;
	float throttle = 0.f, brake = 0.f;
	int direction = forward ? 1 : backward ? -1 : go > 0.f ? 1 : go < 0.f ? -1 : 0;
	if (go != 0.f && (go > 0.f) == (direction > 0))
		throttle = 1.f;
	else if (go != 0.f)
		brake = 1.f;
	if (handbrake)
		throttle = 0.f;
	drive.throttle = throttle;

	// Off the asphalt, in the sand: it bogs down, won't pull or go as it does on the road, and slides sooner.
	drive.offRoad = !OnPaving({ station.position.x, station.position.z });
	float limit = direction >= 0 ? drive.topSpeed : REVERSE_SPEED;
	float pull = drive.pull, grip = drive.grip;
	if (drive.offRoad)
	{
		limit *= 0.45f;
		pull *= 0.7f;
		grip *= 0.6f;
	}
	// How hard it pulls falls away to nothing at its top speed; the drag of the engine and the road slows it with the foot off.
	float pace = std::abs(drive.speed);
	if (throttle > 0.f && pace < limit)
		pace += pull * (std::max)(0.f, 1.f - (pace / limit) * (pace / limit)) * (direction < 0 ? 0.6f : 1.f) * deltaTime;
	else
		pace -= (0.35f + pace * 0.03f + brake * drive.braking + (handbrake ? drive.braking * 0.6f : 0.f)) * deltaTime;
	if (drive.offRoad)
		pace -= (0.5f + pace * 0.09f) * deltaTime;
	pace = (std::max)(0.f, pace);
	drive.speed = pace * static_cast<float>(direction);

	// The wheel: only as far as the tires will hold at speed.
	float lock = (std::min)(CarBody::FULL_LOCK, std::atan(WHEELBASE * grip / (std::max)(pace * pace, 0.01f)));
	float turnRate = 1.6f * deltaTime;
	drive.steer += std::clamp(steerTo * lock - drive.steer, -turnRate, turnRate);
	drive.steer = std::clamp(drive.steer, -CarBody::FULL_LOCK, CarBody::FULL_LOCK);

	// Moved a little at a time, so nothing thin is driven straight through. Its back axle follows the front wheels round.
	const glm::vec3 from = station.position;
	bool wasBlocked = CarBlocked(station.position, station.yaw);
	float travel = drive.speed * deltaTime;
	int steps = std::clamp(static_cast<int>(std::ceil(std::abs(travel) / 0.08f)), 1, 12);
	float moved = 0.f;
	for (int step = 0; step < steps; step++)
	{
		float part = travel / static_cast<float>(steps);
		float yaw = station.yaw + part / WHEELBASE * std::tan(drive.steer);
		// Turning about the back axle, not the middle of the car.
		glm::vec3 heading = { std::cos(station.yaw), 0.f, -std::sin(station.yaw) };
		glm::vec3 axle = station.position + heading * (REAR_AXLE_X + part);
		glm::vec3 position = axle - glm::vec3(std::cos(yaw), 0.f, -std::sin(yaw)) * REAR_AXLE_X;
		// Into something: it stops (with a thump, if it was moving). One that's parked in something already can
		// still be driven out of it.
		bool blocked = CarBlocked(position, yaw);
		if (!wasBlocked && blocked)
		{
			if (std::abs(drive.speed) > 1.5f && drive.bumped > 0.4f)
				Cue(Sound::DROP, GetCarPosition());
			drive.bumped = 0.f;
			drive.speed = -drive.speed * 0.15f;
			break;
		}
		wasBlocked = blocked;
		station.position = position;
		station.yaw = yaw;
		moved += part;
	}
	drive.bumped += deltaTime;
	station.yaw = std::remainder(station.yaw, glm::two_pi<float>());
	station.object->SetPosition(station.position);
	station.object->SetRotation(station.Rotation());
	drive.spin = std::fmod(drive.spin + moved / WHEEL_Y, glm::two_pi<float>());

	// A lap of the highway, timed from the chequered line across its front straight each time it's crossed the same way.
	{
		using namespace GarageLayout;
		const float line = Roads::START_LINE_X;
		bool crossed = (from.x - line) * (station.position.x - line) < 0.f && station.position.z > HIGHWAY_NEAR_Z && station.position.z < HIGHWAY_FAR_Z;
		if (crossed)
		{
			int way = station.position.x > from.x ? 1 : -1;
			if (drive.lapWay == way && drive.lap > 20.f)
			{
				drive.lastLap = drive.lap;
				if (m_bestLap <= 0.f || drive.lap < m_bestLap)
				{
					m_bestLap = drive.lap;
					Toast(std::format("Best lap: {}", LapText(drive.lap)));
				}
			}
			drive.lapWay = way;
			drive.lap = 0.f;
		}
		else if (drive.lapWay != 0)
			drive.lap += deltaTime;
	}

	// The engine's note: up through each gear as it gathers speed, and with the foot down.
	pace = std::abs(drive.speed);
	if (direction < 0)
		drive.gear = 0;
	else
	{
		// Up a gear near the red line, down one when the revs have dropped well off.
		int gear = (std::max)(drive.gear, 1);
		while (gear < 5 && pace > GEAR_TOP[gear] * drive.topSpeed * 0.9f)
			gear++;
		while (gear > 1 && pace < GEAR_TOP[gear - 1] * drive.topSpeed * 0.55f)
			gear--;
		drive.gear = gear;
	}
	float revs = drive.idleRpm + (REDLINE - drive.idleRpm) * (std::min)(1.f, pace / (GEAR_TOP[drive.gear] * drive.topSpeed));
	revs += throttle * 250.f;
	drive.rpm += (revs - drive.rpm) * (1.f - std::exp(-deltaTime * 6.f));

	// Fuel burned by the mile; out of it, that's that.
	if (!workshop.DriveCar(deltaTime, std::abs(moved), throttle))
	{
		Toast("It's run out of fuel.");
		EndDrive();
	}
}

std::string ShopWorld::SeatPrompt() const
{
	if (m_seat != 0)
		return "[F] Get out";
	if (!m_drive.active)
		return "[W] Start it   [L] Headlights   [F] Get out";
	return std::abs(m_drive.speed) <= GET_OUT_SPEED ? "[W]/[S] Drive   [A]/[D] Steer   [Space] Handbrake   [L] Headlights   [F] Get out"
		: "[Space] Handbrake   [L] Headlights";
}

void ShopWorld::PoseCarInstruments(const Workshop& workshop, float deltaTime)
{
	const auto* build = workshop.GetCar();
	bool present = build && m_carStation < m_stations.size() && m_stations[m_carStation].present;
	// The headlights' pods swing up and down; what's on the car is posed again while they do.
	float target = m_headlights.on ? 1.f : 0.f;
	if (m_headlights.amount != target)
	{
		float step = deltaTime / 0.7f;
		m_headlights.amount = m_headlights.amount < target ? (std::min)(target, m_headlights.amount + step) : (std::max)(target, m_headlights.amount - step);
		m_carPanelsMoving = true;
	}
	for (int side = 0; side < 2; side++)
		if (const auto& lens = m_headlightLenses[side])
		{
			bool fitted = present && build->assembly.IsFilled(CarSlot::HEADLIGHT, side);
			lens->SetVisible(fitted);
			if (!fitted)
				continue;
			auto pose = PoseOnCar(CarSlot::HEADLIGHT, side);
			lens->SetPosition(CarToWorld(pose.offset));
			lens->SetRotation(CarRotation() * pose.rotation);
			lens->SetEmissive(m_headlights.on && m_headlights.amount > 0.6f ? 4.f : 0.f);
		}

	// The gauges: live with the engine running, their needles settling to what they read.
	bool cluster = present && build->assembly.IsFilled(CarSlot::INSTRUMENT_CLUSTER, 0);
	float reads[GAUGES] = {};
	if (cluster && m_drive.active)
	{
		double tank = build->assembly.GetSpec().Fluid(FluidCircuit::FUEL).capacity;
		reads[0] = std::abs(m_drive.speed) * 3.6f / 200.f;
		reads[1] = m_drive.rpm / 8000.f;
		reads[2] = tank > 0.0 ? static_cast<float>(build->assembly.GetFluid(FluidCircuit::FUEL).Total() / tank) : 0.f;
		reads[3] = static_cast<float>(workshop.CarEngineHeat()) * 0.62f;	// warmed right through, it sits a little over half way
	}
	using namespace CarBody;
	const glm::vec3 PIVOTS[GAUGES] = { { GAUGE_BIG_X, GAUGE_Y, -0.0575f }, { -GAUGE_BIG_X, GAUGE_Y, -0.0575f },
		{ 0.f, GAUGE_Y + GAUGE_SMALL_Y, -0.0575f }, { 0.f, GAUGE_Y - GAUGE_SMALL_Y, -0.0575f } };
	auto pose = PoseOnCar(CarSlot::INSTRUMENT_CLUSTER, 0);
	for (int gauge = 0; gauge < GAUGES; gauge++)
	{
		m_gaugeReads[gauge] += (std::clamp(reads[gauge], 0.f, 1.f) - m_gaugeReads[gauge]) * (1.f - std::exp(-deltaTime * 7.f));
		const auto& needle = m_gaugeNeedles[gauge];
		if (!needle)
			continue;
		needle->SetVisible(cluster);
		if (!cluster)
			continue;
		bool big = gauge < 2;
		float turn = (big ? GAUGE_SWEEP : GAUGE_SMALL_SWEEP) * (m_gaugeReads[gauge] - 0.5f);
		needle->SetPosition(CarToWorld(pose.offset + pose.rotation * PIVOTS[gauge]));
		needle->SetRotation(CarRotation() * pose.rotation * glm::angleAxis(turn, glm::vec3(0.f, 0.f, 1.f)));
		needle->SetScale(glm::vec3(big ? 1.f : GAUGE_SMALL / GAUGE_BIG));
	}
}

ShopWorld::CameraView ShopWorld::DriveCamera(const FirstPersonController& player)
{
	glm::quat look = player.GetRotation();
	if (!m_thirdPerson)
	{
		m_thirdPersonView = { CarToWorld(SEAT_HEADS[std::clamp(m_seat, 0, SEATS - 1)]), look };
		return m_thirdPersonView;
	}
	// Behind and above it, swung round the car with the mouse (or out in front, looking back at it), kept out of the walls.
	glm::vec3 middle = CarToWorld({ 0.f, 1.1f, 0.f });
	glm::vec3 forward = look * glm::vec3(0.f, 0.f, -1.f);
	glm::vec3 offset = (m_lookingBack ? forward : -forward) * 6.2f + glm::vec3(0.f, 1.1f, 0.f);
	float distance = glm::length(offset);
	glm::vec3 direction = offset / distance;
	if (auto wall = WallHit(middle, direction); wall && *wall < distance)
		distance = (std::max)(*wall - 0.2f, 0.5f);
	glm::vec3 position = middle + direction * distance;
	position.y = (std::max)(position.y, 0.3f);
	m_thirdPersonView = { position, glm::quatLookAt(glm::normalize(middle - position), glm::vec3(0.f, 1.f, 0.f)) };
	return m_thirdPersonView;
}

// What the driver's shown on the screen: the map of the roads, with the lap's time; and, from outside the car (third
// person, where the dash can't be seen), its gauges.
void ShopWorld::DrawDriveHud(const Workshop& workshop) const
{
	if (m_seat < 0 || m_carStation >= m_stations.size())
		return;
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	const float unit = display.y / 1080.f;
	const ImU32 PANEL = IM_COL32(10, 10, 12, 150), RIM = IM_COL32(215, 215, 208, 220), INK = IM_COL32(240, 240, 235, 235);
	const ImU32 AMBER_INK = IM_COL32(255, 150, 40, 255), RED_INK = IM_COL32(230, 60, 50, 255);
	const auto& station = m_stations[m_carStation];

	// The map, bottom left: all the roads, north (the back of the lot) up, the shop and the car on it.
	{
		const glm::vec2 low = Roads::Min() - glm::vec2(12.f), high = Roads::Max() + glm::vec2(12.f);
		const float width = 330.f * unit, scale = width / (high.x - low.x), height = (high.y - low.y) * scale;
		const ImVec2 corner = { 24.f * unit, display.y - height - 24.f * unit };
		auto at = [&](glm::vec2 point) { return ImVec2(corner.x + (point.x - low.x) * scale, corner.y + (point.y - low.y) * scale); };
		draw->AddRectFilled(corner, { corner.x + width, corner.y + height }, PANEL, 6.f * unit);
		const auto& pan = Roads::SkidPan();
		draw->AddRectFilled(at(pan.min), at(pan.max), IM_COL32(150, 150, 150, 220));
		std::vector<ImVec2> points;
		for (const auto& strip : Roads::Strips())
		{
			points.clear();
			for (const auto& point : strip.line)
				points.push_back(at(point));
			draw->AddPolyline(points.data(), static_cast<int>(points.size()), strip.medianSide != 0.f ? IM_COL32(235, 235, 230, 235) : IM_COL32(170, 170, 170, 230),
				strip.loop ? ImDrawFlags_Closed : ImDrawFlags_None, (strip.medianSide != 0.f ? 1.6f : 2.f) * unit);
		}
		// The lap's line, the shop with its drive, and the car: an arrow the way it's pointing.
		using namespace GarageLayout;
		draw->AddLine(at({ Roads::START_LINE_X, HIGHWAY_NEAR_Z - 8.f }), at({ Roads::START_LINE_X, HIGHWAY_FAR_Z + 8.f }), INK, 1.5f * unit);
		draw->AddLine(at({ 0.4f, HALF_DEPTH }), at({ 0.4f, HIGHWAY_NEAR_Z }), IM_COL32(170, 170, 170, 230), 1.5f * unit);
		ImVec2 shop = at({ 0.f, 0.f });
		draw->AddRectFilled({ shop.x - 4.f * unit, shop.y - 3.f * unit }, { shop.x + 4.f * unit, shop.y + 3.f * unit }, IM_COL32(200, 150, 90, 255));
		glm::vec2 nose = { std::cos(station.yaw), -std::sin(station.yaw) }, across = { -nose.y, nose.x };
		ImVec2 car = at({ station.position.x, station.position.z });
		auto corner3 = [&](glm::vec2 offset) { return ImVec2(car.x + offset.x * unit, car.y + offset.y * unit); };
		draw->AddTriangleFilled(corner3(nose * 9.f), corner3(-nose * 5.f + across * 5.f), corner3(-nose * 5.f - across * 5.f), AMBER_INK);
		// The lap, over the map.
		if (m_drive.active && (m_drive.lapWay != 0 || m_bestLap > 0.f))
		{
			std::string lap = m_drive.lapWay != 0 ? std::format("Lap {}", LapText(m_drive.lap)) : std::string("Lap -");
			if (m_drive.lastLap > 0.f)
				lap += std::format("   Last {}", LapText(m_drive.lastLap));
			if (m_bestLap > 0.f)
				lap += std::format("   Best {}", LapText(m_bestLap));
			ShadowText(draw, { corner.x + 4.f * unit, corner.y - 22.f * unit }, INK, lap, false);
		}
		else if (m_drive.active)
			ShadowText(draw, { corner.x + 4.f * unit, corner.y - 22.f * unit }, IM_COL32(200, 200, 200, 200), "Cross the chequered line on the highway to time a lap", false);
	}

	// From outside the car, its gauges, bottom right: the tach with the gear in it, the speedo, and the fuel and
	// temperature beside them. (At the wheel they're there on the dash.)
	if (!m_thirdPerson)
		return;
	const auto* build = workshop.GetCar();
	const bool running = m_drive.active;
	// A dial: its marks round from lower left clockwise to lower right, some numbered, the last of them red.
	auto dial = [&](ImVec2 middle, float radius, float reads, int marks, int numberEvery, float numberStep, int redFrom) {
		const float from = glm::pi<float>() * 0.75f, sweep = glm::pi<float>() * 1.5f;
		auto out = [&](float turn, float reach) { return ImVec2(middle.x + std::cos(turn) * radius * reach, middle.y + std::sin(turn) * radius * reach); };
		draw->AddCircleFilled(middle, radius, IM_COL32(10, 10, 12, 190), 64);
		draw->AddCircle(middle, radius, RIM, 64, 2.f * unit);
		for (int mark = 0; mark <= marks; mark++)
		{
			float turn = from + sweep * static_cast<float>(mark) / marks;
			ImU32 color = mark >= redFrom ? RED_INK : INK;
			draw->AddLine(out(turn, 0.8f), out(turn, 0.93f), color, 2.f * unit);
			if (mark % numberEvery == 0)
			{
				std::string number = std::format("{:.0f}", mark * numberStep);
				ImVec2 size = ImGui::CalcTextSize(number.c_str()), spot = out(turn, 0.62f);
				draw->AddText({ spot.x - size.x * 0.5f, spot.y - size.y * 0.5f }, color, number.c_str());
			}
		}
		float turn = from + sweep * std::clamp(reads, 0.f, 1.f);
		draw->AddLine(out(turn + glm::pi<float>(), 0.12f), out(turn, 0.86f), AMBER_INK, 3.f * unit);
		draw->AddCircleFilled(middle, radius * 0.07f, IM_COL32(40, 40, 44, 255), 16);
	};
	const float speed = std::abs(m_drive.speed) * 3.6f;
	const ImVec2 speedo = { display.x - 150.f * unit, display.y - 150.f * unit }, tach = { display.x - 385.f * unit, display.y - 130.f * unit };
	dial(speedo, 118.f * unit, running ? speed / 200.f : 0.f, 10, 2, 20.f, 99);
	dial(tach, 96.f * unit, running ? m_drive.rpm / 8000.f : 0.f, 8, 1, 1.f, 6);
	ShadowText(draw, { speedo.x, speedo.y + 38.f * unit }, INK, std::format("{:.0f}", running ? speed : 0.f), true);
	ShadowText(draw, { speedo.x, speedo.y + 58.f * unit }, IM_COL32(190, 190, 190, 220), "km/h", true);
	ShadowText(draw, { tach.x, tach.y + 30.f * unit }, INK, running ? GEAR_NAMES[std::clamp(m_drive.gear, 0, 5)] : "off", true);
	ShadowText(draw, { tach.x, tach.y + 50.f * unit }, IM_COL32(190, 190, 190, 220), "x1000 rpm", true);
	// Fuel and temperature: a bar each, left of the tach.
	auto bar = [&](float y, const char* name, float reads, bool warn) {
		const ImVec2 corner = { display.x - 640.f * unit, y };
		const float width = 130.f * unit, height = 12.f * unit;
		ShadowText(draw, { corner.x, corner.y - 20.f * unit }, INK, name, false);
		draw->AddRectFilled(corner, { corner.x + width, corner.y + height }, IM_COL32(10, 10, 12, 190), 3.f * unit);
		draw->AddRectFilled(corner, { corner.x + width * std::clamp(reads, 0.f, 1.f), corner.y + height }, warn ? RED_INK : AMBER_INK, 3.f * unit);
		draw->AddRect(corner, { corner.x + width, corner.y + height }, RIM, 3.f * unit, 0, 1.5f * unit);
	};
	double tank = build ? build->assembly.GetSpec().Fluid(FluidCircuit::FUEL).capacity : 0.0;
	double fuel = build ? build->assembly.GetFluid(FluidCircuit::FUEL).Total() : 0.0;
	float full = tank > 0.0 ? static_cast<float>(fuel / tank) : 0.f, heat = static_cast<float>(workshop.CarEngineHeat());
	bar(display.y - 150.f * unit, std::format("Fuel  {:.1f} L", fuel).c_str(), full, full < 0.12f);
	bar(display.y - 90.f * unit, "Temperature", heat * 0.62f, heat > 1.3f);
	if (m_drive.offRoad && running)
		ShadowText(draw, { tach.x, tach.y - 126.f * unit }, IM_COL32(230, 200, 140, 230), "In the sand", true);
}

void ShopWorld::DevSteer(float steer)
{
	m_drive.steer = std::clamp(steer, -1.f, 1.f) * CarBody::FULL_LOCK;
	m_carLaidOutAt = glm::vec3(-1e9f);	// laid out again, turned
}

void ShopWorld::DevPlaceCar(float x, float z, float degrees)
{
	if (m_carStation >= m_stations.size())
		return;
	auto& station = m_stations[m_carStation];
	station.position = { x, 0.f, z };
	station.yaw = glm::radians(degrees);
	station.object->SetPosition(station.position);
	station.object->SetRotation(station.Rotation());
	m_supportChanged = true;
	m_carLaidOutAt = glm::vec3(-1e9f);
	RebuildCollision();
}
