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
	constexpr float WHEEL_Z = 0.75f;		// the tires' middles, either side: their outer walls just proud of the body, under its flared arches
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
			float turned = angle - (x > 0.f ? m_drive.spin : m_drive.rearSpin);
			// (On its stud, against the face of the wheel's centre.)
			glm::vec3 local = { std::cos(turned) * 0.05f, std::sin(turned) * 0.05f, side * 0.031f };
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
			case CarSlot::TRANSMISSION_MOUNT_BOLT: return { { GEARBOX_X - 0.3f + of * 0.02f, SHAFT_Y - 0.18f, pair * 0.09f }, HEAD_DOWN };
			case CarSlot::DRIVESHAFT_FLANGE_NUT: return { ring({ REAR_AXLE_X + 0.24f, WHEEL_Y, 0.f }, 0.05f, 4), FACING_FRONT };
			case CarSlot::STARTER_BOLT:			return { { BLOCK_REAR - 0.135f, SHAFT_Y - 0.025f + end * 0.06f, -0.2f }, FACING_BACK };
			// On the firewall: the master cylinder's two studs on the booster's face, the booster's four round it.
			case CarSlot::MASTER_CYLINDER_NUT:	return { { 1.06f, BOOSTER_Y, BOOSTER_Z + end * 0.04f }, FACING_FRONT };
			case CarSlot::BOOSTER_NUT:			return { ring({ 0.9f, BOOSTER_Y, BOOSTER_Z }, 0.125f, 4), FACING_FRONT };
			// The tank's four on its corners in the trunk, each seat's four at the feet of its frame on the floor.
			case CarSlot::FUEL_TANK_BOLT:		return { { -1.72f + of * 0.27f, 0.49f, pair * 0.37f }, NONE };
			case CarSlot::SEAT_BOLT:			return { { -0.28f + of * 0.235f, 0.22f, s * 0.37f + pair * 0.17f }, NONE };
			// Up through the flange where the front pipe meets the engine's downpipe, heads down.
			case CarSlot::CATALYTIC_CONVERTER_NUT:
			{
				float turn = glm::two_pi<float>() * (hole + 0.5f) / 3.f;
				return { { EXHAUST_JOINT[0] + std::cos(turn) * 0.03f, EXHAUST_JOINT[1] - 0.012f, EXHAUST_JOINT[2] + std::sin(turn) * 0.03f }, HEAD_DOWN };
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
			// (Its latch released, it stands up a finger's width off the nose.)
			return { { CarBody::HOOD_REAR_X + CarBody::GAP, CarBody::BELT_Y, 0.f },
				Pitch(CarBody::HoodPitch() + CarBody::HOOD_OPEN * (std::max)(CarPanelOpen(CarPanel::HOOD), m_hoodLatched ? 0.f : 0.035f)) };
		case C::TRUNK_LID:
			return { { CarBody::TRUNK_FRONT_X - CarBody::GAP, CarBody::BELT_Y, 0.f }, Pitch(-CarBody::TRUNK_OPEN * CarPanelOpen(CarPanel::TRUNK)) };
		case C::FRONT_BUMPER:		return { { CarBody::NOSE_X + 0.035f, 0.25f, 0.f }, ACROSS };
		case C::REAR_BUMPER:		return { { CarBody::TAIL_X - 0.035f, 0.25f, 0.f }, ACROSS };
		case C::HEADLIGHT:
		{
			// A pop-up: hinged along the back edge of its lid, which lies in the hood's surface till it tilts up.
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
		case C::SEAT:				return { { CarBody::SEAT_X + m_seats[std::clamp(index, 0, 1)].slide, CarBody::SEAT_Y, side * CarBody::SEAT_Z }, NONE };	// slid along its rails
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
		case C::BRAKE_LINE_SET:		return { { -0.2f, 0.15f, -0.5f }, NONE };	// outboard of the exhaust
		case C::BRAKE_HOSE:			return index < 2 ? CarPose{ { FRONT_AXLE_X - 0.08f, 0.3f, side * 0.42f }, NONE }
										: CarPose{ { REAR_AXLE_X + 0.15f, 0.3f, 0.12f }, NONE };
		case C::BRAKE_ROTOR:		return steered(side, { { FRONT_AXLE_X, WHEEL_Y, side * 0.6f }, Pitch(-m_drive.spin) * FacingSide(side) });
		// The caliper over the rotor's front edge with the pads in it; the shoes inside the drum, the wheel cylinder behind.
		case C::BRAKE_PAD_SET:		return steered(side, { { FRONT_AXLE_X + 0.085f, WHEEL_Y - 0.015f, side * 0.605f }, NONE });
		case C::BRAKE_CALIPER:		return steered(side, { { FRONT_AXLE_X + 0.085f, WHEEL_Y - 0.05f, side * 0.605f }, NONE });
		case C::WHEEL_CYLINDER:		return { { REAR_AXLE_X, WHEEL_Y + 0.07f, side * 0.58f }, ACROSS };
		case C::BRAKE_SHOE_SET:		return { { REAR_AXLE_X, WHEEL_Y - 0.025f, side * 0.63f }, NONE };
		case C::BRAKE_DRUM:			return { { REAR_AXLE_X, WHEEL_Y, side * 0.6f }, Pitch(-m_drive.rearSpin) * FacingSide(side) };
		case C::PARKING_BRAKE_CABLE: return { { -0.5f, 0.15f, 0.05f }, NONE };
		// Drivetrain: the clutch behind the engine's flywheel, the gearbox down the tunnel to the driveshaft.
		// (The mounts on their perches either side of the crankcase, each one's bracket in to the block.)
		case C::ENGINE_MOUNT:		return { { CarBody::ENGINE_X, CarBody::ENGINE_MOUNT_Y, side * CarBody::ENGINE_MOUNT_Z }, side < 0.f ? TurnY(glm::pi<float>()) : NONE };
		case C::CLUTCH_DISC:		return { { BLOCK_REAR - 0.025f, SHAFT_Y, 0.f }, Pitch(glm::half_pi<float>()) };
		case C::PRESSURE_PLATE:		return { { BLOCK_REAR - 0.065f, SHAFT_Y, 0.f }, Pitch(glm::half_pi<float>()) };
		case C::RELEASE_BEARING:	return { { BLOCK_REAR - 0.125f, SHAFT_Y, 0.f }, Pitch(glm::half_pi<float>()) };
		case C::TRANSMISSION:		return { { GEARBOX_X, SHAFT_Y - 0.15f, 0.f }, TurnY(glm::pi<float>()) };
		case C::TRANSMISSION_MOUNT:	return { { GEARBOX_X - 0.3f, SHAFT_Y - 0.18f, 0.f }, ACROSS };
		case C::CLUTCH_MASTER_CYLINDER: return { { CarBody::CLUTCH_MASTER[0], CarBody::CLUTCH_MASTER[1], CarBody::CLUTCH_MASTER[2] }, NONE };
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
		case C::FUEL_FILTER:		return { { CarBody::FUEL_FILTER[0], CarBody::FUEL_FILTER[1], CarBody::FUEL_FILTER[2] }, NONE };
		// Electrical
		// The battery on its tray behind the left headlight, the main loom across behind the dash and the engine's
		// across the firewall, the fuse box up under the dash by the driver's door, the coil on the right inner wing.
		case C::BATTERY:			return { { 1.66f, 0.52f, -0.5f }, NONE };
		case C::MAIN_HARNESS:		return { { 0.82f, 0.56f, 0.f }, ACROSS };

		case C::FUSE_BOX:			return { { 0.6f, 0.5f, -0.6f }, NONE };
		case C::IGNITION_COIL:		return { { CarBody::IGNITION_COIL[0], CarBody::IGNITION_COIL[1], CarBody::IGNITION_COIL[2] }, NONE };
		case C::STARTER:			return { { BLOCK_REAR - 0.045f, SHAFT_Y - 0.07f, -0.2f }, NONE };
		// Cooling: the radiator across the nose, the fan behind it.
		case C::RADIATOR:			return { { 1.98f, 0.24f, 0.f }, ACROSS };
		case C::RADIATOR_HOSE:
		{
			// Both on the car's right, from the radiator's outlets back to the engine: the upper one from the top tank
			// to the thermostat's housing on the head, bowed up over the fan; the lower one from the bottom tank up to
			// the water pump's inlet, bowed out and down clear of the belt. (The model is 0.48 m between its ends along
			// its own X, its bow toward its +Y, its middle its radius above its origin.)
			const glm::vec3 from = index == 0 ? glm::vec3(1.905f, 0.635f, 0.22f) : glm::vec3(1.905f, 0.255f, 0.25f);
			const glm::vec3 to = index == 0 ? glm::vec3(CarBody::ENGINE_X + 0.185f, CarBody::ENGINE_DECK_Y + 0.1f, 0.128f)
				: glm::vec3(CarBody::ENGINE_X + 0.245f, CarBody::ENGINE_DECK_Y - 0.1f, 0.135f);
			const glm::vec3 x = glm::normalize(to - from);
			const glm::vec3 bow = index == 0 ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(0.f, -0.5f, 1.f);
			const glm::vec3 y = glm::normalize(bow - x * glm::dot(bow, x));
			const glm::quat turn = glm::quat_cast(glm::mat3(x, y, glm::cross(x, y)));
			return { (from + to) * 0.5f - turn * glm::vec3(0.f, 0.021f, 0.f), turn };
		}
		case C::COOLING_FAN:		return { { 1.94f, 0.45f, 0.f }, Pitch(glm::half_pi<float>()) };	// against the back of the radiator's core
		// Exhaust: down the car's left, from the engine's downpipe back under the floor to the tailpipe (see CarBody.h).
		case C::CATALYTIC_CONVERTER: return { { CarBody::CATALYST[0], CarBody::CATALYST[1], CarBody::CATALYST[2] }, NONE };
		case C::MUFFLER:			return { { CarBody::MUFFLER_X, CarBody::MUFFLER_Y, CarBody::MUFFLER_Z }, TurnY(glm::pi<float>()) };
		// Wheels, at their corners: the tire round the wheel.
		case C::WHEEL:
		case C::TIRE:
		{
			// Rolled round its axle (the model stands on the ground, its axle the tire's radius up) by how far the car's
			// gone, and the front ones turned by the steering with their knuckles.
			glm::quat turn = ACROSS * Roll(front ? m_drive.spin : m_drive.rearSpin);
			CarPose rolled = { glm::vec3(axleX, WHEEL_Y, side * WHEEL_Z) - turn * glm::vec3(0.f, WHEEL_Y, 0.f), turn };
			return front ? steered(side, rolled) : rolled;
		}
		default:					break;
	}
	return {};
}

glm::quat ShopWorld::CarBaseRotation() const
{
	// Turned to where it points, and tipped by how high each corner's held: nose up with the front higher than the
	// back, over to its right with the left higher than the right.
	if (m_carStation >= m_stations.size())
		return glm::quat(1.f, 0.f, 0.f, 0.f);
	const auto& h = m_cornerHeights;
	glm::quat turned = m_stations[m_carStation].Rotation();
	if (h[0] == h[1] && h[1] == h[2] && h[2] == h[3])
		return turned;
	constexpr float TRACK = 2.f * WHEEL_Z;
	float nose = std::atan2((h[0] + h[1] - h[2] - h[3]) * 0.5f, FRONT_AXLE_X - REAR_AXLE_X);
	float over = std::atan2((h[0] + h[2] - h[1] - h[3]) * 0.5f, TRACK);
	return turned * Pitch(nose) * Roll(over);
}

glm::vec3 ShopWorld::CarOrigin() const
{
	if (m_carStation >= m_stations.size())
		return glm::vec3(0.f);
	const auto& h = m_cornerHeights;
	return m_stations[m_carStation].position + glm::vec3(0.f, (h[0] + h[1] + h[2] + h[3]) * 0.25f, 0.f);
}

glm::quat ShopWorld::CarRotation() const
{
	// (As it stands, and leant on its springs as it's driven.)
	glm::quat base = CarBaseRotation();
	return m_drive.active ? base * Roll(m_drive.roll) * Pitch(m_drive.pitch) : base;
}

glm::vec3 ShopWorld::CarToWorld(const glm::vec3& offset) const
{
	return CarOrigin() + CarRotation() * offset;
}

std::array<ShopBox, 3> ShopWorld::CarBoxes() const
{
	// As long and wide as the shell's model, from the floor under it.
	glm::vec3 low = { -2.2f, 0.f, -0.86f }, high = { 2.2f, CarBody::ROOF_Y, 0.86f };
	if (m_carStation < m_stations.size() && m_stations[m_carStation].object)
		if (const auto* model = ModelOf(m_stations[m_carStation].object))
		{
			low = model->GetBoundsMin();
			high = model->GetBoundsMax();
		}
	// The hood swings up about its back edge (1.1 m long, to 0.9 rad); the lid about its front (0.5 m, to 1.05 rad).
	const float hood = CarBody::BELT_Y + 0.02f + 0.87f * CarPanelOpen(CarPanel::HOOD);
	const float trunk = CarBody::BELT_Y + 0.02f + 0.45f * CarPanelOpen(CarPanel::TRUNK);
	const float front = CarBody::WINDSHIELD_BASE_X, back = CarBody::REAR_WINDOW_BASE_X;
	return { ShopBox{ { front, low.y, low.z }, { high.x, hood, high.z } },
		ShopBox{ { back, low.y, low.z }, { front, high.y, high.z } },
		ShopBox{ { low.x, low.y, low.z }, { back, trunk, high.z } } };
}

std::optional<float> ShopWorld::RayCarBody(const glm::vec3& origin, const glm::vec3& direction) const
{
	if (m_carStation >= m_stations.size())
		return std::nullopt;
	// Into the car's own space, where the boxes are square to it.
	const glm::quat back = glm::inverse(CarRotation());
	const glm::vec3 from = back * (origin - CarToWorld(glm::vec3(0.f))), along = back * direction;
	std::optional<float> nearest;
	for (const auto& box : CarBoxes())
		if (auto hit = RayBox(from, along, box.min, box.max); hit && (!nearest || *hit < *nearest))
			nearest = hit;
	return nearest;
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
	if (panel == CarPanel::HOOD && !state.open)
		m_hoodLatched = true;
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
	PoseSeatBacks(workshop, deltaTime);
	UpdateExhaustSmoke(workshop, deltaTime);
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
	if (*aimed == CarPanel::HOOD && !IsCarPanelOpen(*aimed) && m_hoodLatched)
	{
		Toast("The hood's latched: pull its release, under the dash by the driver's door.");
		Cue(Sound::CLICK, GetCarPosition());
		return true;
	}
	ToggleCarPanel(*aimed);
	return true;
}

void ShopWorld::PlaceOnCar(ItemVisual& visual, CarSlot slot, int index) const
{
	auto pose = PoseOnCar(slot, index);
	// A nut or bolt stands out of its hole while it's loose, as on the engine.
	if (CarAssembly::IsFastenerSlot(slot) && visual.part && visual.part->GetTightness() == FastenerTightness::LOOSE && !PartWayDoneUp(*visual.part))
		pose.offset += pose.rotation * glm::vec3(0.f, 0.012f, 0.f);
	// What's on the road, not on the springs (the wheels and what turns with them, the brakes, the back axle), stays
	// level as the body leans.
	bool unsprung = false;
	switch (slot)
	{
		case CarSlot::WHEEL: case CarSlot::TIRE: case CarSlot::LUG_NUT: case CarSlot::BRAKE_ROTOR: case CarSlot::BRAKE_DRUM:
		case CarSlot::BRAKE_CALIPER: case CarSlot::BRAKE_PAD_SET: case CarSlot::CALIPER_BOLT: case CarSlot::BRAKE_SHOE_SET:
		case CarSlot::WHEEL_CYLINDER: case CarSlot::STEERING_KNUCKLE: case CarSlot::REAR_AXLE: case CarSlot::DIFFERENTIAL:
		case CarSlot::DIFFERENTIAL_NUT:
			unsprung = true;
			break;
		default:
			break;
	}
	if (unsprung && m_carStation < m_stations.size())
	{
		visual.object->SetPosition(CarOrigin() + CarBaseRotation() * pose.offset);
		visual.object->SetRotation(CarBaseRotation() * pose.rotation);
	}
	else
	{
		visual.object->SetPosition(CarToWorld(pose.offset));
		visual.object->SetRotation(CarRotation() * pose.rotation);
	}
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
	constexpr float CAR_HALF_WIDTH = 0.85f;
	constexpr float REVERSE_SPEED = 7.f;		// m/s
	constexpr float REDLINE = 6200.f;
	constexpr float GET_OUT_SPEED = 0.6f;		// m/s: it has to be all but stopped
	// Each gear runs out at this much of the car's top speed.
	constexpr float GEAR_TOP[] = { 0.2f, 0.24f, 0.42f, 0.62f, 0.82f, 1.02f };	// reverse, then first to fifth
	// Where the head of whoever's in each seat is: the driver's (on the left), the front passenger's, and the two
	// places on the back seat.
	const glm::vec3 SEAT_HEADS[] = { { -0.36f, 1.07f, -0.37f }, { -0.36f, 1.07f, 0.37f }, { -1.0f, 1.03f, -0.33f }, { -1.0f, 1.03f, 0.33f } };
	const char* GEAR_NAMES[] = { "R", "N", "1st", "2nd", "3rd", "4th", "5th" };
	const char* GearName(int gear) { return GEAR_NAMES[std::clamp(gear, -1, 5) + 1]; }
	// How much of the car's top speed a gear runs out at.
	float GearTop(int gear) { return GEAR_TOP[gear < 0 ? 0 : std::clamp(gear, 1, 5)]; }

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
	if (m_lotBuilt && in(PARKING_LOT.min, PARKING_LOT.max))
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
	if (!build || m_carStation >= m_stations.size() || !m_stations[m_carStation].present)
		return std::nullopt;
	// Only a seat that's itself under the crosshair. A front seat is a part (its base, or its back, which is aimed at
	// as the seat): the one looked at, in the place it's bolted in.
	if (m_hover && m_hover->kind == TargetKind::ITEM)
	{
		const auto* visual = Find(m_hover->item);
		if (visual && visual->part && visual->place == ItemPlace::CAR && visual->part->GetType() == PartType::SEAT)
		{
			if (auto at = build->assembly.Find(visual->part.get()); at && at->first == CarSlot::SEAT)
				return std::clamp(at->second, 0, 1);
			return std::nullopt;
		}
	}
	// The back seat is part of the car's shell: the first thing the crosshair is on has to be the shell (not a door,
	// a window or a front seat in the way), at a point within the bench; the half of the bench it's in is the place.
	// (What's hovered won't do for this: parts are found before the shell, so under the bench it's the axle.)
	auto aim = AimAtCar(player, INTERACT_DISTANCE);
	if (!aim || !aim->shell)
		return std::nullopt;
	using namespace CarBody;
	glm::vec3 hit = glm::inverse(CarRotation()) * (aim->point - CarOrigin());
	constexpr float SLACK = 0.03f;
	if (hit.x < REAR_SEAT_BACK_X - SLACK || hit.x > REAR_SEAT_FRONT_X + SLACK || hit.y < REAR_SEAT_BASE_Y - SLACK || hit.y > REAR_SEAT_TOP_Y + SLACK
		|| std::abs(hit.z) > REAR_SEAT_HALF + SLACK)
		return std::nullopt;
	return hit.z < 0.f ? 2 : 3;
}

glm::vec3 ShopWorld::SeatHead(int seat) const
{
	seat = std::clamp(seat, 0, SEATS - 1);
	glm::vec3 head = SEAT_HEADS[seat];
	if (seat < 2)
	{
		// A front seat's goes with the seat along its rails, and back and down as its back is leant back.
		constexpr float SHOULDERS = 0.55f;		// how far up the back from its hinge the head is carried
		const auto& set = m_seats[seat];
		head.x += set.slide - (std::sin(set.recline) - std::sin(CarBody::SEAT_RECLINE)) * SHOULDERS;
		head.y += (std::cos(set.recline) - std::cos(CarBody::SEAT_RECLINE)) * SHOULDERS;
	}
	return head;
}

void ShopWorld::PoseSeatBacks(const Workshop& workshop, float deltaTime)
{
	using namespace CarBody;
	const auto* build = workshop.GetCar();
	bool present = build && m_carStation < m_stations.size() && m_stations[m_carStation].present;
	for (int side = 0; side < 2; side++)
	{
		auto& set = m_seats[side];
		const auto& back = m_seatBacks[side];
		pPart seat = present ? build->assembly.Get(CarSlot::SEAT, side) : nullptr;
		// As the car has it set (which is saved with the car, and the same for everyone in the garage).
		const CarSeatSetting setting = build ? build->assembly.GetSeatSetting(side) : CarSeatSetting{};
		if (set.slide != setting.slide)
			m_carLaidOutAt = glm::vec3(-1e9f);	// the seat's laid out again where it's been slid to
		set.slide = setting.slide;
		set.recline = setting.recline;
		set.folded = seat && setting.folded;
		float target = set.folded ? 1.f : 0.f;
		if (set.fold != target)
		{
			float step = seat ? deltaTime / 0.4f : 1.f;
			set.fold = set.fold < target ? (std::min)(target, set.fold + step) : (std::max)(target, set.fold - step);
		}
		if (!back)
			continue;
		const ItemVisual* visual = seat ? Find(ItemRef::Part(seat->GetId())) : nullptr;
		bool shown = visual && visual->place == ItemPlace::CAR && visual->object->IsVisible();
		back->SetVisible(shown);
		back->SetPickable(shown);
		if (!shown)
			continue;
		// Aimed at, it's the seat that's aimed at; and it looks as the seat does (its cloth, and lit up when it's picked out).
		m_targets[back.get()] = { TargetKind::ITEM, visual->ref, 0, 0 };
		back->SetTexture(visual->object->GetTextureIndex());
		back->SetTint(visual->object->GetTint());
		back->SetEmissive(visual->object->GetEmissive());
		auto pose = PoseOnCar(CarSlot::SEAT, side);
		float angle = glm::mix(set.recline, SEAT_FOLD, glm::smoothstep(0.f, 1.f, set.fold));
		back->SetPosition(CarToWorld(pose.offset + glm::vec3(SEAT_HINGE_X, SEAT_HINGE_Y, 0.f)));
		back->SetRotation(CarRotation() * Pitch(angle));
	}
}

void ShopWorld::DevSeat(Workshop& workshop, int seat, float slide, float recline, bool folded)
{
	CarSeatSetting setting;
	setting.slide = std::clamp(slide, -CarBody::SEAT_SLIDE, CarBody::SEAT_SLIDE);
	setting.recline = std::clamp(recline, CarBody::SEAT_RECLINE_MIN, CarBody::SEAT_RECLINE_MAX);
	setting.folded = folded;
	workshop.SetCarSeat(std::clamp(seat, 0, 1), setting);
}

namespace
{
	// A number from 0 to 1, different each time.
	float Chance()
	{
		static uint32_t state = 0x9E3779B9u;
		state = state * 1664525u + 1013904223u;
		return static_cast<float>(state >> 8) / 16777216.f;
	}
}

void ShopWorld::UpdateExhaustSmoke(const Workshop& workshop, float deltaTime)
{
	const auto* build = workshop.GetCar();
	bool running = m_drive.active && build && m_carStation < m_stations.size() && m_stations[m_carStation].present;
	m_smokeRunning = running ? m_smokeRunning + deltaTime : 0.f;
	if (running)
	{
		// Out of the tailpipe; with no silencer on, from under the floor where the pipe ends behind the converter; and
		// with that off too, from the end of the engine's downpipe in the bay.
		using namespace CarBody;
		const glm::vec3 pipe = build->assembly.IsFilled(CarSlot::MUFFLER, 0) ? glm::vec3(TAILPIPE[0], TAILPIPE[1], TAILPIPE[2])
			: build->assembly.IsFilled(CarSlot::CATALYTIC_CONVERTER, 0) ? glm::vec3(CATALYST[0] - 0.3f, EXHAUST_Y, EXHAUST_Z)
			: glm::vec3(ENGINE_X + EXHAUST_JOINT[0], ENGINE_DECK_Y + EXHAUST_JOINT[1], EXHAUST_JOINT[2]);
		// More of it, and blown out harder, the faster it's turning and with the foot down; thick and white for the
		// first few seconds after it's started.
		const float revs = std::clamp((m_drive.rpm - m_drive.idleRpm) / (REDLINE - m_drive.idleRpm), 0.f, 1.f);
		const float cold = std::clamp(1.f - m_smokeRunning / 6.f, 0.f, 1.f);
		const float rate = 5.f + 9.f * revs + 5.f * m_drive.throttle + 6.f * cold;
		m_smokeDue -= deltaTime;
		const auto& station = m_stations[m_carStation];
		const glm::vec3 heading = { std::cos(station.yaw), 0.f, -std::sin(station.yaw) };
		for (int made = 0; m_smokeDue <= 0.f && made < 4; made++)
		{
			m_smokeDue += 1.f / rate;
			// The next one free, or the oldest.
			SmokePuff* puff = &m_smoke[0];
			for (auto& candidate : m_smoke)
				if (candidate.life <= 0.f || (puff->life > 0.f && candidate.age / candidate.life > puff->age / puff->life))
					puff = &candidate;
			if (!puff->object)
				continue;
			puff->age = 0.f;
			puff->life = 1.1f + 0.9f * Chance();
			puff->position = CarToWorld(pipe);
			const float blown = 0.7f + 1.8f * revs + 1.2f * m_drive.throttle;
			puff->velocity = CarRotation() * glm::vec3(-blown, 0.15f + 0.25f * Chance(), (Chance() - 0.5f) * 0.4f) + heading * (m_drive.speed * 0.2f);
			puff->size = 0.07f + 0.05f * revs + 0.03f * Chance();
			puff->thick = 0.3f + 0.3f * cold + 0.15f * m_drive.throttle;
		}
		m_smokeDue = (std::max)(m_smokeDue, -0.2f);
	}
	// Each drifts on, slowing and rising, swelling and thinning to nothing.
	for (auto& puff : m_smoke)
	{
		if (!puff.object)
			continue;
		if (puff.life > 0.f)
		{
			puff.age += deltaTime;
			if (puff.age >= puff.life)
				puff.life = 0.f;
		}
		puff.object->SetVisible(puff.life > 0.f);
		if (puff.life <= 0.f)
			continue;
		puff.velocity *= std::exp(-1.6f * deltaTime);
		puff.velocity.y += 0.4f * deltaTime;
		puff.position += puff.velocity * deltaTime;
		puff.position.y = (std::max)(puff.position.y, 0.04f);
		const float through = puff.age / puff.life;
		const float shade = 0.62f + 0.3f * std::clamp(puff.thick - 0.3f, 0.f, 0.3f) / 0.3f;
		puff.object->SetPosition(puff.position);
		puff.object->SetScale(glm::vec3(puff.size * (1.f + 5.5f * through)));
		puff.object->SetTint({ shade, shade, shade * 1.02f, puff.thick * std::pow(1.f - through, 1.5f) * (std::min)(1.f, puff.age / 0.06f) });
	}
}

void ShopWorld::Sit(int seat, Workshop& workshop)
{
	if (m_seat >= 0 || m_carStation >= m_stations.size() || !m_stations[m_carStation].present)
		return;
	m_seat = std::clamp(seat, 0, SEATS - 1);
	DropHeldPlug(workshop);
	m_seatJustIn = true;
	m_seatLastYaw = m_stations[m_carStation].yaw;
	// Nothing in hand in there.
	m_handEmpty = true;
	SyncHand();
	Cue(Sound::SET_DOWN, GetCarPosition());
	// A front seat that was tipped forward has its back put up to be sat in.
	if (const auto* build = workshop.GetCar(); build && m_seat < 2 && build->assembly.GetSeatSetting(m_seat).folded)
	{
		CarSeatSetting setting = build->assembly.GetSeatSetting(m_seat);
		setting.folded = false;
		workshop.SetCarSeat(m_seat, setting);
	}
	// (In the driver's seat, the key's theirs to turn: see Drive.) Left running, it's theirs to drive off.
	if (CarBeingDriven())
		RebuildCollision();
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
	float devSeconds = m_drive.devSeconds, devSteer = m_drive.devSteer, spin = m_drive.spin, rearSpin = m_drive.rearSpin, steer = m_drive.steer;
	int gear = m_drive.gear;
	m_drive = {};
	// (In whatever gear it was left in, its wheel as it was left, and the clutch pedal as the driver has it.)
	m_drive.gear = gear;
	m_drive.steer = steer;
	m_drive.clutch = m_clutchHeld ? 0.f : 1.f;
	m_keyOn = true;
	m_drive.devSeconds = devSeconds;
	m_drive.devSteer = devSteer;
	// (Its wheels are where they were left.)
	m_drive.spin = spin;
	m_drive.rearSpin = rearSpin;
	m_drive.active = true;
	m_drive.topSpeed = (std::max)(static_cast<float>(car.topSpeed) / 3.6f, 3.f);
	m_drive.pull = car.zeroToHundred > 0.0 ? std::clamp(27.8f / static_cast<float>(car.zeroToHundred) * 1.7f, 0.8f, 7.f)
		: std::clamp(m_drive.topSpeed / 25.f, 0.5f, 2.f);
	m_drive.braking = car.brakingDistance > 0.0 ? std::clamp(386.f / static_cast<float>(car.brakingDistance), 2.f, 11.f) : 6.f;
	m_drive.grip = std::clamp(static_cast<float>(car.lateralGrip), 0.3f, 1.3f) * 9.81f;
	m_drive.idleRpm = car.idleRpm > 0.0 ? static_cast<float>(car.idleRpm) : 850.f;
	m_drive.rpm = m_drive.idleRpm;
	// How it handles, from its alignment, its tires' pressures and its suspension.
	if (const auto* build = workshop.GetCar())
	{
		const auto& fitted = build->assembly;
		const auto& tune = fitted.GetTune();
		const auto& spec = fitted.GetSpec();
		const CarTune factory = CarTune::Factory(spec);
		// Toe: out sharpens how the front bites (and makes it nervous), and far off either way scrubs its grip away.
		// Camber: leaning in a little more than stock grips harder in a bend, leaning out less. Caster brings the
		// wheel back to the middle.
		float toe = static_cast<float>(tune.frontToe - factory.frontToe), camber = static_cast<float>(tune.frontCamber - factory.frontCamber);
		m_drive.frontBite = 11.f * (1.f - std::clamp(toe * 0.04f, -0.25f, 0.25f));
		m_drive.frontGrip = 1.f - (std::min)(0.15f, 0.02f * std::abs(toe)) + std::clamp(-camber * 0.03f, -0.1f, 0.06f);
		m_drive.selfCentre = std::clamp(1.f + 0.6f * static_cast<float>(tune.caster), 0.6f, 3.5f);
		// A soft tire grips less; one softer than the other across the front tugs the car its way.
		auto soft = [&](int corner) { return static_cast<float>((std::max)(0.0, (factory.tirePressure[corner] - tune.tirePressure[corner]) / factory.tirePressure[corner])); };
		m_drive.frontGrip *= 1.f - 0.25f * (soft(0) + soft(1));
		m_drive.rearGrip = 1.f - 0.25f * (soft(2) + soft(3));
		m_drive.tug = 0.03f * static_cast<float>((tune.tirePressure[1] - tune.tirePressure[0]) / factory.tirePressure[0]);
		// Tired struts, shocks and springs let the body lean further and wallow; with no sway bar it rolls more
		// and the tail's quicker to go.
		int tired = 0, counted = 0;
		for (CarSlot slot : { CarSlot::FRONT_STRUT, CarSlot::FRONT_COIL_SPRING, CarSlot::REAR_SHOCK, CarSlot::REAR_COIL_SPRING })
			for (int index = 0; index < fitted.SlotCount(slot); index++, counted++)
				if (auto part = fitted.Get(slot, index); !part || part->GetCondition() == PartCondition::DAMAGED)
					tired++;
		float worn = counted ? static_cast<float>(tired) / static_cast<float>(counted) : 0.f;
		m_drive.lean = 1.f + 0.8f * worn;
		m_drive.damping = 0.7f - 0.45f * worn;
		if (!fitted.IsFilled(CarSlot::FRONT_SWAY_BAR, 0))
		{
			m_drive.lean *= 1.4f;
			m_drive.rearGrip *= 0.95f;
		}
	}
	// It's no longer in its own way.
	RebuildCollision();
	return true;
}

void ShopWorld::EndDrive()
{
	if (!m_drive.active)
		return;
	m_drive.active = false;
	// (The wheel stays turned as it was, and it's left in its gear.)
	m_drive.speed = 0.f;
	m_drive.handbrake = m_drive.footBrake = false;
	m_drive.cut = 0.f;
	m_drive.roll = m_drive.pitch = 0.f;
	m_supportChanged = true;
	m_carLaidOutAt = glm::vec3(-1e9f);	// its wheels are put straight again
	RebuildCollision();
}

void ShopWorld::LeaveSeat(FirstPersonController* player)
{
	if (m_seat < 0)
		return;
	// (The engine's left running, if it was: see IdleCar.)
	m_drive.speed = 0.f;
	m_cranking = 0.f;
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
	m_supportChanged = true;
	RebuildCollision();
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
	if (!m_headlights.on && (!car.IsPlugged(CarPlug::BATTERY) || !car.IsPlugged(CarPlug::GROUND)
		|| (!car.IsPlugged(CarPlug::HEADLIGHT_LEFT) && !car.IsPlugged(CarPlug::HEADLIGHT_RIGHT))))
	{
		Toast(!car.IsPlugged(CarPlug::BATTERY) ? "Nothing: the harness's lead isn't on the battery."
			: !car.IsPlugged(CarPlug::GROUND) ? "Nothing: the battery's ground strap isn't on the chassis."
			: "Nothing: neither headlight is plugged into the harness.");
		Cue(Sound::CLICK, GetCarPosition());
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

	// The key (clicked in its barrel: see UpdateCarControls). Held down past a moment it's turned to start, and the starter
	// cranks for as long as it's held; let go before the engine catches, it springs back to on. A quick click turns it
	// on (to accessory) or off.
	constexpr float KEY_HOLD = 0.3f;
	m_clutchHeld = playerHasControl && m_seat == 0 && Controls::Held(input, Control::CLUTCH);
	const bool keyHeld = m_keyPressing && playerHasControl && Controls::Held(input, Control::PRIMARY);
	if (m_seat == 0 && keyHeld && !m_drive.active && m_cranking <= 0.f && m_time - m_keyPressedAt >= KEY_HOLD)
		BeginCranking(workshop);
	if (m_keyPressing && !keyHeld)
	{
		m_keyPressing = false;
		if (m_time - m_keyPressedAt < KEY_HOLD && !m_drive.active)
		{
			m_keyOn = !m_keyOn;
			Cue(Sound::CLICK, GetCarPosition(), 0.5f);
		}
	}
	if (m_cranking > 0.f)
	{
		if (!m_crankAuto && !keyHeld)
			m_cranking = 0.f;
		else
		{
			m_cranking -= deltaTime;
			if (m_cranking <= 0.f && m_seat == 0)
			{
				m_crankAuto = false;
				StartDrive(workshop);
			}
		}
	}
	if (m_seat == 0)
		UpdateHandbrake(input, playerHasControl);
	if (CarBeingDriven() && CarOffGround())
		SpinOffGround(deltaTime, input, playerHasControl);
	else if (CarBeingDriven())
		MoveCar(deltaTime, workshop, input, playerHasControl);
	else if (m_seat == 0)
		ParkedControls(deltaTime, input, playerHasControl);
	// Driven off, it leaves whatever's under it (let down to the ground) behind.
	if (CarBeingDriven() && std::abs(m_drive.speed) > 0.2f)
		ReleaseCarSupports();

	// Whoever's in it goes with it: kept in their seat, and turned as the car turns.
	float turned = std::remainder(station.yaw - m_seatLastYaw, glm::two_pi<float>());
	m_seatLastYaw = station.yaw;
	player.SetYaw(player.GetYaw() + turned);
	player.SetPosition(CarToWorld(SeatHead(m_seat)) - (player.GetEyePosition() - player.GetFeetPosition()));

	// [F] gets out, once it's stopped.
	if (playerHasControl && !justIn && Controls::Pressed(input, Control::MOVE))
	{
		if (!CarBeingDriven() || std::abs(m_drive.speed) <= GET_OUT_SPEED)
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
	// (The lever: see UpdateHandbrake.)
	bool handbrake = m_handbrakeOn;
	const float speedBefore = drive.speed;
	if (playerHasControl)
	{
		go = (Controls::Held(input, Control::FORWARD) ? 1.f : 0.f) - (Controls::Held(input, Control::BACK) ? 1.f : 0.f);
		steerTo = (Controls::Held(input, Control::LEFT) ? 1.f : 0.f) - (Controls::Held(input, Control::RIGHT) ? 1.f : 0.f);
	}
	// (The developer's foot: forward for so long, or backward for a time given as less than nothing.)
	const bool devDriving = drive.devSeconds != 0.f;
	if (devDriving)
	{
		go = drive.devSeconds > 0.f ? 1.f : -1.f;
		drive.devSeconds = go > 0.f ? (std::max)(0.f, drive.devSeconds - deltaTime) : (std::min)(0.f, drive.devSeconds + deltaTime);
		steerTo = drive.devSteer;
	}
	drive.handbrake = handbrake;
	// Off the asphalt, in the sand: it bogs down, won't pull or go as it does on the road, and slides sooner.
	drive.offRoad = !OnPaving({ station.position.x, station.position.z });
	float pull = drive.pull, grip = drive.grip;
	if (drive.offRoad)
	{
		pull *= 0.7f;
		grip *= 0.6f;
	}
	// (The gears are the driver's to change. Only the developer's foot, with no gear held, has them changed for it.)
	const bool manual = !devDriving || m_devGear != NO_DEV_GEAR;
	if (manual && devDriving)
		drive.gear = std::clamp(m_devGear, -1, 5);
	float throttle = 0.f, brake = 0.f, pace = std::abs(drive.speed);
	int direction = 0;
	float wheelRevs = 0.f;		// what the wheels would have the engine turning at, in gear
	if (manual)
	{
		// The driver's gearbox: forward is the throttle and back the brake, whatever gear it's in. The clutch pedal
		// has to be down to move the lever (but for knocking it out into neutral), up and down through reverse,
		// neutral and first to fifth; let up, it bites over half a second.
		const bool clutchDown = playerHasControl && !devDriving && Controls::Held(input, Control::CLUTCH);
		drive.clutch = clutchDown ? (std::max)(0.f, drive.clutch - deltaTime / 0.08f) : (std::min)(1.f, drive.clutch + deltaTime / 0.45f);
		// (Without the clutch the lever still goes, but slowly and with a crunch: it's out of gear till it's in.)
		constexpr float CRUNCH_SECONDS = 0.8f;
		const int from = drive.shifting > 0.f ? drive.shiftingTo : drive.gear;
		int wanted = from;
		if (playerHasControl && Controls::Pressed(input, Control::SHIFT_UP) && from < 5)
			wanted++;
		if (playerHasControl && Controls::Pressed(input, Control::SHIFT_DOWN) && from > -1)
			wanted--;
		if (wanted != from)
		{
			if (wanted < 0 && drive.speed > 1.5f)
				Toast("Stop before putting it in reverse.");
			else if (clutchDown || wanted == 0)
			{
				drive.gear = wanted;
				drive.shifting = 0.f;
			}
			else
			{
				drive.gear = 0;
				drive.shiftingTo = wanted;
				drive.shifting = CRUNCH_SECONDS;
				Cue(Sound::RATCHET, GetCarPosition());
			}
		}
		if (drive.shifting > 0.f)
		{
			// With the clutch put down meanwhile, it goes straight in.
			drive.shifting = clutchDown ? 0.f : drive.shifting - deltaTime;
			if (drive.shifting <= 0.f)
				drive.gear = drive.shiftingTo;
		}
		throttle = go > 0.f && !handbrake ? 1.f : 0.f;
		brake = go < 0.f ? 1.f : 0.f;
		float speed = drive.speed;
		const float bite = drive.gear != 0 ? drive.clutch : 0.f;
		if (bite > 0.02f)
		{
			// In gear, as far as the clutch has bitten, the engine turns with the wheels (slipping to pull away from
			// rest): it pulls hardest in the low gears and in the middle of its revs, not at all past the red line,
			// holds the car back with the foot off, and at its idle creeps along.
			const float way = drive.gear < 0 ? -1.f : 1.f;
			const float top = GearTop(drive.gear) * drive.topSpeed;
			const float along = speed * way;
			wheelRevs = REDLINE * (std::max)(along, 0.f) / top;
			const float revs = std::clamp(wheelRevs / REDLINE, drive.idleRpm / REDLINE, 1.2f);
			const float gearing = std::pow(GEAR_TOP[1] / GearTop(drive.gear), 0.85f);
			// (Nothing at all while the rev limiter has the ignition cut.)
			const float torque = drive.cut > 0.f ? 0.f : 0.45f + 1.1f * revs - 0.75f * revs * revs;
			if (throttle > 0.f)
				speed += way * pull * 1.3f * torque * gearing * bite * deltaTime;
			else if (wheelRevs > drive.idleRpm)
				speed -= way * (std::min)(along, (0.3f + 1.6f * gearing * revs) * bite * deltaTime);
			else if (brake == 0.f && !handbrake)
				speed += way * pull * 0.35f * gearing * bite * deltaTime;
			// Braked to a stop in gear with the clutch up, it stalls.
			if (bite > 0.9f && wheelRevs < 300.f && throttle == 0.f && (brake > 0.f || handbrake) && std::abs(speed) < 0.3f)
			{
				Toast("Stalled: clutch down ([Clutch]) before it stops.");
				EndDrive();
				return;
			}
		}
		// What always slows it: the road and the air (which is what sets its top speed), the brakes, the sand.
		// (The handbrake locks the back wheels: they skid, which stops it less hard than the brakes but holds it still.)
		float slowing = 0.25f + 0.32f * drive.pull * (pace / drive.topSpeed) * (pace / drive.topSpeed) + brake * drive.braking + (handbrake ? drive.braking * 0.35f : 0.f);
		if (drive.offRoad)
			slowing += 0.5f + pace * 0.12f;
		float slowed = (std::min)(std::abs(speed), slowing * deltaTime);
		speed -= speed > 0.f ? slowed : -slowed;
		drive.speed = speed;
		pace = std::abs(speed);
		direction = speed > 0.05f ? 1 : speed < -0.05f ? -1 : 0;
	}
	else
	{
		// The developer's foot (`car drive`): forward pulls away (or brakes, going backward); back brakes, then reverses.
		bool forward = drive.speed > 0.05f, backward = drive.speed < -0.05f;
		direction = forward ? 1 : backward ? -1 : go > 0.f ? 1 : go < 0.f ? -1 : 0;
		if (go != 0.f && (go > 0.f) == (direction > 0))
			throttle = 1.f;
		else if (go != 0.f)
			brake = 1.f;
		if (handbrake)
			throttle = 0.f;
		float limit = direction >= 0 ? drive.topSpeed : REVERSE_SPEED;
		if (drive.offRoad)
			limit *= 0.45f;
		// How hard it pulls falls away to nothing at its top speed; the drag of the engine and the road slows it with the foot off.
		if (throttle > 0.f && pace < limit)
			pace += pull * (std::max)(0.f, 1.f - (pace / limit) * (pace / limit)) * (direction < 0 ? 0.6f : 1.f) * deltaTime;
		else
			pace -= (0.35f + pace * 0.03f + brake * drive.braking + (handbrake ? drive.braking * 0.6f : 0.f)) * deltaTime;
		if (drive.offRoad)
			pace -= (0.5f + pace * 0.09f) * deltaTime;
		pace = (std::max)(0.f, pace);
		drive.speed = pace * static_cast<float>(direction);
	}
	drive.throttle = throttle;
	drive.footBrake = brake > 0.f;

	// The wheel goes as far over at any speed, and comes back to the middle by itself (faster the more caster it has).
	float turnRate = (steerTo != 0.f ? 2.f : drive.selfCentre) * deltaTime;
	drive.steer += std::clamp(steerTo * CarBody::FULL_LOCK - drive.steer, -turnRate, turnRate);
	drive.steer = std::clamp(drive.steer, -CarBody::FULL_LOCK, CarBody::FULL_LOCK);

	// What the tires can do. Each axle has its share of the grip: more on the front under braking and on the back
	// pulling away, as the weight shifts. What an axle spends pushing or stopping the car it hasn't left for holding
	// it sideways: the driven back wheels with the foot down, the front ones braking hard, and the back ones locked
	// by the handbrake hardly hold at all. More throttle than the back tires can put down just spins them.
	constexpr float CG_HEIGHT = 0.5f, HALF = WHEELBASE * 0.5f, TURNING_INERTIA = 1.5f, REAR_BITE = 12.f, SHAPE = 1.4f;
	float along = (drive.speed - speedBefore) / (std::max)(deltaTime, 1e-4f);
	const float shift = std::clamp(along * CG_HEIGHT / (9.81f * WHEELBASE), -0.2f, 0.2f);
	const float frontShare = grip * (0.5f - shift) * drive.frontGrip, rearShare = grip * (0.5f + shift) * drive.rearGrip;
	bool spinning = false;
	if (along > rearShare && drive.speed * speedBefore >= 0.f)
	{
		drive.speed = speedBefore + (drive.speed > speedBefore ? rearShare : -rearShare) * deltaTime;
		along = rearShare;
		spinning = true;
	}
	const float frontSpent = along < 0.f ? -along * 0.65f : 0.f, rearSpent = along > 0.f ? along : -along * 0.35f;
	const float frontHold = std::sqrt((std::max)(frontShare * frontShare - frontSpent * frontSpent, frontShare * frontShare * 0.04f));
	float rearHold = std::sqrt((std::max)(rearShare * rearShare - rearSpent * rearSpent, rearShare * rearShare * 0.06f));
	// Locked by the handbrake the back tires are skidding: they hardly hold sideways at all, and the tail swings out.
	if (handbrake && std::abs(drive.speed) > 0.5f)
		rearHold *= 0.12f;

	// Moved a little at a time (so nothing thin is driven straight through), its tires pushing it sideways and round
	// by how far each axle is slipping across the way it's pointed: the front ones turned with the wheel. Turned
	// too hard for its speed the front pushes wide; with the back let go, the tail comes round. At a crawl (and
	// going backward) it just follows its front wheels.
	const glm::vec3 from = station.position;
	bool wasBlocked = CarBlocked(station.position, station.yaw);
	const float turned = std::clamp(drive.steer + drive.tug, -CarBody::FULL_LOCK, CarBody::FULL_LOCK);
	float forward = drive.speed, sideways = drive.slide, turning = drive.yawRate;
	const int steps = std::clamp(static_cast<int>(std::ceil((std::abs(forward) + std::abs(sideways)) * deltaTime / 0.08f)), 6, 16);
	const float step = deltaTime / static_cast<float>(steps);
	float moved = 0.f, frontSlip = 0.f, rearSlip = 0.f;
	for (int i = 0; i < steps; i++)
	{
		if (forward > 1.5f)
		{
			frontSlip = turned - std::atan2(sideways + HALF * turning, forward);
			rearSlip = -std::atan2(sideways - HALF * turning, forward);
			float front = frontHold * std::sin(SHAPE * std::atan(drive.frontBite * frontSlip));
			float rear = rearHold * std::sin(SHAPE * std::atan(REAR_BITE * rearSlip));
			float push = front * std::cos(turned) + rear;
			sideways += (push - forward * turning) * step;
			turning += HALF * (front * std::cos(turned) - rear) / TURNING_INERTIA * step;
			forward += (sideways * turning - front * std::sin(turned)) * step;
		}
		// Following its front wheels round about its back axle, as far as it's too slow for the tires to slip.
		float slipping = forward > 0.f ? std::clamp((forward - 1.5f) / 3.f, 0.f, 1.f) : 0.f;
		float follows = forward * std::tan(turned) / WHEELBASE;
		turning = glm::mix(follows, turning, slipping);
		sideways = glm::mix(follows * HALF, sideways, slipping);

		float yaw = station.yaw + turning * step;
		glm::vec3 heading = { std::cos(station.yaw), 0.f, -std::sin(station.yaw) }, left = { -std::sin(station.yaw), 0.f, -std::cos(station.yaw) };
		glm::vec3 position = station.position + (heading * forward + left * sideways) * step;
		// Into something: it stops (with a thump, if it was moving). One that's parked in something already can
		// still be driven out of it.
		bool blocked = CarBlocked(position, yaw);
		if (!wasBlocked && blocked)
		{
			if (std::abs(forward) + std::abs(sideways) > 1.5f && drive.bumped > 0.4f)
				Cue(Sound::DROP, GetCarPosition());
			drive.bumped = 0.f;
			forward = -forward * 0.15f;
			sideways = turning = 0.f;
			break;
		}
		wasBlocked = blocked;
		station.position = position;
		station.yaw = yaw;
		moved += forward * step;
	}
	drive.speed = forward;
	drive.slide = sideways;
	drive.yawRate = turning;
	drive.sliding = forward > 4.f && (spinning || handbrake || std::abs(rearSlip) > 0.16f || std::abs(frontSlip) > 0.2f);
	drive.bumped += deltaTime;
	station.yaw = std::remainder(station.yaw, glm::two_pi<float>());
	// The body leans out of the bend, squats pulling away and dives braking (and shakes, over the sand).
	float shake = drive.offRoad ? (std::min)(std::abs(forward) / 12.f, 1.f) : 0.f;
	LeanCar(deltaTime, along + shake * 3.f * std::sin(m_time * 31.f), forward * turning + shake * 4.f * std::sin(m_time * 23.f));
	station.object->SetPosition(CarOrigin());
	station.object->SetRotation(CarRotation());
	drive.spin = std::fmod(drive.spin + moved / WHEEL_Y, glm::two_pi<float>());
	// The back wheels turn with it too, but not locked by the handbrake: they slide.
	if (!handbrake)
		drive.rearSpin = std::fmod(drive.rearSpin + moved / WHEEL_Y, glm::two_pi<float>());

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
	float revs = 0.f;
	if (manual)
	{
		// Out of gear it revs freely with the foot down; in gear it turns with the wheels, the clutch slipping (and
		// the revs up a little with the throttle) while that's under its idle.
		const float free = drive.idleRpm + throttle * (REDLINE * 0.92f - drive.idleRpm);
		const float geared = wheelRevs > drive.idleRpm ? (std::min)(wheelRevs, REDLINE + 150.f) : drive.idleRpm + throttle * 1400.f;
		revs = drive.gear == 0 ? free : glm::mix(free, geared, drive.clutch);
		// A door left open is blown shut once it's moving.
		if (pace > 4.f)
			m_carPanels[static_cast<size_t>(CarPanel::DOOR_LEFT)].open = m_carPanels[static_cast<size_t>(CarPanel::DOOR_RIGHT)].open = false;
	}
	else
	{
		if (direction < 0)
			drive.gear = -1;
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
		revs = drive.idleRpm + (REDLINE - drive.idleRpm) * (std::min)(1.f, pace / (GearTop(drive.gear) * drive.topSpeed));
		revs += throttle * 250.f;
	}
	// Turning with the wheels it follows them closely; free of them (out of gear, or the clutch down) it spins up
	// quickly with the foot down and its flywheel carries it on, falling away slowly, with it off.
	const bool freed = manual && (drive.gear == 0 || drive.clutch < 0.5f);
	// The rev limiter: at the red line it cuts the ignition, the revs fall back a few hundred, and it fires again, so
	// held there it bounces off the line, popping in the exhaust.
	constexpr float CUT_SECONDS = 0.07f, CUT_DROP = 1.6f;
	if (drive.cut > 0.f)
	{
		drive.cut -= deltaTime;
		revs = (std::min)(revs, drive.rpm);
		drive.rpm -= REDLINE * CUT_DROP * deltaTime;
	}
	else
		drive.rpm += (revs - drive.rpm) * (1.f - std::exp(-deltaTime * (!freed ? 6.f : revs > drive.rpm ? 5.f : 1.1f)));
	drive.bangDue = (std::max)(0.f, drive.bangDue - deltaTime);
	if (drive.cut <= 0.f && drive.rpm >= REDLINE && throttle > 0.f)
	{
		drive.cut = CUT_SECONDS;
		drive.rpm = (std::min)(drive.rpm, REDLINE + 40.f);
		if (drive.bangDue <= 0.f)
		{
			using namespace CarBody;
			Cue(Sound::EXHAUST_BANG, CarToWorld({ TAILPIPE[0], TAILPIPE[1], TAILPIPE[2] }), 0.9f);
			drive.bangDue = 0.12f;
		}
	}

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
	const std::string handbrake = std::format("[Space] Handbrake {}", m_handbrakeOn ? "off" : "on");
	if (!m_drive.active)
	{
		if (m_cranking > 0.f)
			return "Cranking: keep holding the key";
		return std::format("Hold [LMB] on the key (right of the wheel) to start it   [LMB] Key {}   [A]/[D] Steer   [Up]/[Down] Gear ({}){}   {}   [F] Get out",
			m_keyOn ? "off" : "on", GearName(m_drive.gear), m_drive.gear != 0 ? ": hold [Clutch] to start in gear" : "", handbrake);
	}
	bool stopped = std::abs(m_drive.speed) <= GET_OUT_SPEED;
	return stopped ? std::format("Hold [Clutch] Clutch   [Up]/[Down] Gear up, down   [W] Throttle   [S] Brake   [A]/[D] Steer   {}   [F] Get out", handbrake)
		: "Hold [Clutch] Clutch   [Up]/[Down] Gear   Hold [Space] Handbrake";
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
			// (Lit only if it's plugged in.)
			bool fed = build->assembly.IsPlugged(side == 0 ? CarPlug::HEADLIGHT_LEFT : CarPlug::HEADLIGHT_RIGHT);
			lens->SetEmissive(m_headlights.on && m_headlights.amount > 0.6f && fed ? 4.f : 0.f);
		}

	// The headlights' beams: a faint cone of light out ahead of each lamp that's lit, seen more the darker it is.
	for (int side = 0; side < 2; side++)
		if (const auto& beam = m_headlightBeams[side])
		{
			bool lit = present && m_headlights.on && m_headlights.amount > 0.9f && build->assembly.IsFilled(CarSlot::HEADLIGHT, side)
				&& build->assembly.IsPlugged(side == 0 ? CarPlug::HEADLIGHT_LEFT : CarPlug::HEADLIGHT_RIGHT);
			beam->SetVisible(lit);
			if (!lit)
				continue;
			using namespace CarBody;
			const float z = (side == 0 ? -1.f : 1.f) * (LAMP_LID_INNER + HOOD_HALF) * 0.5f;
			constexpr float LENGTH = 11.f, WIDTH = 2.6f;
			beam->SetPosition(CarToWorld({ NOSE_X - 0.05f, 0.66f, z }));
			beam->SetRotation(CarRotation() * Pitch(-0.055f));
			beam->SetScale({ LENGTH, WIDTH, WIDTH });
			beam->SetTint({ 1.f, 0.96f, 0.82f, 0.025f + 0.07f * m_darkness });
		}

	// The tail lights' stop lamps: dim red with the headlights on, bright with the foot on the brake. (Fed from the
	// battery like the rest of the lights.)
	bool fed = present && build->assembly.IsFilled(CarSlot::BATTERY, 0) && build->assembly.IsPlugged(CarPlug::BATTERY)
		&& build->assembly.IsPlugged(CarPlug::GROUND);
	for (int side = 0; side < 2; side++)
		if (const auto& lamp = m_stopLamps[side])
		{
			bool braking = (m_drive.active || m_keyOn) && m_drive.footBrake;
			bool lit = fed && build->assembly.IsFilled(CarSlot::TAIL_LIGHT, side) && (m_headlights.on || braking);
			lamp->SetVisible(lit);
			if (!lit)
				continue;
			using namespace CarBody;
			// (On the lamp's own model: toward the car's middle from the lamp's own, on its back face. The right one's the mirror.)
			const float out = side == 0 ? 1.f : -1.f;
			auto pose = PoseOnCar(CarSlot::TAIL_LIGHT, side);
			lamp->SetPosition(CarToWorld(pose.offset + pose.rotation * glm::vec3(-out * STOP_LAMP_IN, STOP_LAMP_Y, -0.043f)));
			lamp->SetRotation(CarRotation() * pose.rotation);
			lamp->SetTint({ 1.f, 0.12f, 0.08f, 1.f });
			lamp->SetEmissive(braking ? 3.5f : 0.9f);
		}

	// The handbrake's lever, up while it's pulled (whether the engine's running or not).
	{
		float target = m_handbrakeOn ? 1.f : 0.f;
		m_handbrakeUp += (target - m_handbrakeUp) * (1.f - std::exp(-deltaTime * 18.f));
		if (m_handbrakeLever)
		{
			m_handbrakeLever->SetVisible(present);
			using namespace CarBody;
			m_handbrakeLever->SetPosition(CarToWorld({ HANDBRAKE_PIVOT[0], HANDBRAKE_PIVOT[1], HANDBRAKE_PIVOT[2] }));
			m_handbrakeLever->SetRotation(CarRotation() * Pitch(0.12f + HANDBRAKE_PULL * m_handbrakeUp));
		}
	}

	ShakeEngine(workshop, deltaTime);

	// The gear lever, through its gate to the gear it's in (to neutral while a gear's being crunched in).
	{
		glm::vec2 want = GearGate(m_drive.shifting > 0.f ? 0 : m_drive.gear);
		m_leverAt += (want - m_leverAt) * (1.f - std::exp(-deltaTime * 14.f));
		bool shown = present && build->assembly.IsFilled(CarSlot::TRANSMISSION, 0);
		if (m_gearLever)
		{
			m_gearLever->SetVisible(shown);
			if (shown)
			{
				// (Its ball in the gearbox's tail, which is turned end for end on the car.)
				m_gearLever->SetPosition(CarToWorld({ GEARBOX_X - 0.59f, SHAFT_Y - 0.15f + 0.255f, 0.f }));
				m_gearLever->SetRotation(CarRotation() * glm::angleAxis(-m_leverAt.y * 0.24f, glm::vec3(0.f, 0.f, 1.f))
					* glm::angleAxis(-m_leverAt.x * 0.16f, glm::vec3(1.f, 0.f, 0.f)));
			}
		}
	}

	// The gauges: live with the key on (the fuel and temperature) or the engine running (all of them), their needles
	// settling to what they read.
	bool cluster = present && build->assembly.IsFilled(CarSlot::INSTRUMENT_CLUSTER, 0);
	// (The cluster's numbers and marks glow green, over its faces, with the dash lights on.)
	if (m_clusterMarks)
	{
		bool lit = cluster && GaugesLit(workshop);
		m_clusterMarks->SetVisible(lit);
		if (lit)
		{
			auto at = PoseOnCar(CarSlot::INSTRUMENT_CLUSTER, 0);
			m_clusterMarks->SetPosition(CarToWorld(at.offset + at.rotation * glm::vec3(0.f, 0.f, -0.0004f)));
			m_clusterMarks->SetRotation(CarRotation() * at.rotation);
			m_clusterMarks->SetTint({ 0.3f, 1.7f, 0.45f, 1.f });
			m_clusterMarks->SetEmissive(1.4f);
		}
	}
	float reads[GAUGES] = {};
	using namespace CarBody;
	if (cluster && (m_drive.active || m_keyOn))
	{
		double tank = build->assembly.GetSpec().Fluid(FluidCircuit::FUEL).capacity;
		const bool running = m_drive.active;
		reads[0] = running ? std::abs(m_drive.speed) * 3.6f / SPEEDO_TOP : 0.f;
		reads[1] = running ? m_drive.rpm / TACH_TOP : 0.f;
		reads[2] = tank > 0.0 ? static_cast<float>(build->assembly.GetFluid(FluidCircuit::FUEL).Total() / tank) : 0.f;
		reads[3] = running ? 0.4f + 0.35f * std::clamp(m_drive.rpm / 4000.f, 0.f, 1.f) : 0.f;	// the oil's pressure, up with the revs
		reads[4] = static_cast<float>(workshop.CarEngineHeat()) * 0.5f;		// stone cold at the bottom; warmed right through, it sits half way
		// A gauge whose sender isn't plugged in reads nothing.
		if (!build->assembly.IsPlugged(CarPlug::OIL_SENDER))
			reads[3] = 0.f;
		if (!build->assembly.IsPlugged(CarPlug::COOLANT_SENDER))
			reads[4] = 0.f;
	}
	const float low = GAUGE_Y - GAUGE_SMALL_Y, behind = -0.054f;
	const glm::vec3 PIVOTS[GAUGES] = { { GAUGE_BIG_X, GAUGE_Y, behind }, { -GAUGE_BIG_X, GAUGE_Y, behind },
		{ GAUGE_FUEL_X, low, behind }, { GAUGE_OIL_X, low, behind }, { GAUGE_TEMP_X, low, behind } };
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
		// (Lit green with the headlights, like the faces.)
		needle->SetEmissive(GaugesLit(workshop) ? 1.6f : 0.8f);
		bool big = gauge < 2;
		float turn = (big ? GAUGE_SWEEP : GAUGE_SMALL_SWEEP) * (m_gaugeReads[gauge] - 0.5f);
		needle->SetPosition(CarToWorld(pose.offset + pose.rotation * PIVOTS[gauge]));
		needle->SetRotation(CarRotation() * pose.rotation * glm::angleAxis(turn, glm::vec3(0.f, 0.f, 1.f)));
		needle->SetScale(glm::vec3(big ? 1.f : GAUGE_SMALL / (GAUGE_BIG * 0.92f)));
	}
}

ShopWorld::CameraView ShopWorld::DriveCamera(const FirstPersonController& player)
{
	glm::quat look = player.GetRotation();
	if (!m_thirdPerson)
	{
		m_thirdPersonView = { CarToWorld(SeatHead(m_seat)), look };
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

	// Its gauges, at the bottom of the screen in the middle, just over the hotbar: the tach with the gear in it, the
	// speedo, and between them the fuel and temperature. Faces lit green with the headlights on, as the dash's are.
	const auto* build = workshop.GetCar();
	const bool running = m_drive.active;
	const bool lit = GaugesLit(workshop);
	// (Only the numbers and marks are lit: the faces stay dark.)
	const ImU32 FACE = IM_COL32(10, 10, 12, 190);
	const ImU32 MARK = lit ? IM_COL32(120, 255, 150, 240) : INK;

	// A dial: its marks round from lower left clockwise to lower right, some numbered, the last of them red.
	auto dial = [&](ImVec2 middle, float radius, float reads, int marks, int numberEvery, float numberStep, int redFrom) {
		const float from = glm::pi<float>() * 0.75f, sweep = glm::pi<float>() * 1.5f;
		auto out = [&](float turn, float reach) { return ImVec2(middle.x + std::cos(turn) * radius * reach, middle.y + std::sin(turn) * radius * reach); };

		draw->AddCircleFilled(middle, radius, FACE, 64);
		draw->AddCircle(middle, radius, RIM, 64, 2.f * unit);
		for (int mark = 0; mark <= marks; mark++)
		{
			float turn = from + sweep * static_cast<float>(mark) / marks;
			ImU32 color = mark >= redFrom ? RED_INK : MARK;
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
	// (The hotbar's top is 100 px up from the bottom, the name of what's in hand over it: these stand on that.)
	const float speed = std::abs(m_drive.speed) * 3.6f, radius = 74.f * unit, baseY = display.y - 128.f - radius;
	const ImVec2 tach = { display.x * 0.5f - 150.f * unit, baseY }, speedo = { display.x * 0.5f + 150.f * unit, baseY };
	dial(speedo, radius, running ? speed / 200.f : 0.f, 10, 2, 20.f, 99);
	dial(tach, radius, running ? m_drive.rpm / 8000.f : 0.f, 8, 1, 1.f, 6);
	const ImU32 LABEL = lit ? IM_COL32(110, 220, 140, 220) : IM_COL32(190, 190, 190, 220);
	ShadowText(draw, { speedo.x, speedo.y + 22.f * unit }, MARK, std::format("{:.0f}", running ? speed : 0.f), true);
	ShadowText(draw, { speedo.x, speedo.y + 40.f * unit }, LABEL, "km/h", true);
	ShadowText(draw, { tach.x, tach.y + 22.f * unit }, MARK, running ? GearName(m_drive.gear) : "off", true);
	ShadowText(draw, { tach.x, tach.y + 40.f * unit }, LABEL, "x1000 rpm", true);
	// Fuel and temperature: a bar each, between the dials. The temperature reads from cold at the bottom of its bar.
	auto bar = [&](float y, const std::string& name, float reads, bool warn) {
		const float width = 120.f * unit, height = 11.f * unit;
		const ImVec2 corner = { display.x * 0.5f - width * 0.5f, y };
		ShadowText(draw, { display.x * 0.5f, corner.y - 19.f * unit }, LABEL, name, true);
		draw->AddRectFilled(corner, { corner.x + width, corner.y + height }, FACE, 3.f * unit);
		draw->AddRectFilled(corner, { corner.x + width * std::clamp(reads, 0.f, 1.f), corner.y + height }, warn ? RED_INK : AMBER_INK, 3.f * unit);
		draw->AddRect(corner, { corner.x + width, corner.y + height }, lit ? MARK : RIM, 3.f * unit, 0, 1.5f * unit);
	};
	double tank = build ? build->assembly.GetSpec().Fluid(FluidCircuit::FUEL).capacity : 0.0;
	double fuel = build ? build->assembly.GetFluid(FluidCircuit::FUEL).Total() : 0.0;
	float full = tank > 0.0 ? static_cast<float>(fuel / tank) : 0.f, heat = static_cast<float>(workshop.CarEngineHeat());
	bar(baseY - 30.f * unit, std::format("Fuel  {:.1f} L", fuel), full, full < 0.12f);
	bar(baseY + 30.f * unit, heat < 0.05f ? "Temp: cold" : heat < 0.95f ? "Temp: warming up" : "Temp", heat * 0.5f, heat > 1.3f);
}

bool ShopWorld::GaugesLit(const Workshop& workshop) const
{
	// The dash's lights come on with the headlights' switch, fed from the battery.
	const auto* build = workshop.GetCar();
	return build && m_headlights.on && build->assembly.IsFilled(CarSlot::BATTERY, 0) && build->assembly.IsPlugged(CarPlug::BATTERY)
		&& build->assembly.IsPlugged(CarPlug::GROUND);
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
	ReleaseCarSupports();
	station.position = { x, 0.f, z };
	station.yaw = glm::radians(degrees);
	station.object->SetPosition(station.position);
	station.object->SetRotation(station.Rotation());
	m_supportChanged = true;
	m_carLaidOutAt = glm::vec3(-1e9f);
	RebuildCollision();
}

glm::vec3 ShopWorld::SeatHips(int seat) const
{
	// On the cushion (the front seats' tops, and the back seat's a little higher), forward of where the head is.
	glm::vec3 head = SEAT_HEADS[std::clamp(seat, 0, SEATS - 1)];
	if (seat >= 0 && seat < 2)
		head.x += m_seats[seat].slide;
	return CarToWorld({ head.x + 0.1f, (seat < 2 ? 0.365f : 0.385f) + PlayerAvatar::SEATED_HIP_HEIGHT, head.z });
}

float ShopWorld::SeatYaw() const
{
	return (m_carStation < m_stations.size() ? m_stations[m_carStation].yaw : 0.f) - glm::half_pi<float>();
}

void ShopWorld::TurnKey(Workshop& workshop)
{
	if (m_seat != 0)
		return;
	if (!m_drive.active && m_cranking <= 0.f)
	{
		// (The developer's turn of it: cranks till it starts, out of gear.)
		m_drive.gear = 0;
		m_crankAuto = true;
		BeginCranking(workshop);
	}
	else if (m_drive.active && std::abs(m_drive.speed) <= GET_OUT_SPEED)
	{
		EndDrive();
		m_keyOn = false;
		Cue(Sound::CLICK, GetCarPosition());
	}
	else if (m_drive.active)
		Toast("Stop the car before switching it off.");
}

void ShopWorld::SpinOffGround(float deltaTime, const InputManager& input, bool playerHasControl)
{
	// Up on the lift, a jack or stands, it goes nowhere: the engine revs with the throttle, and in gear with the clutch
	// up the back wheels turn in the air (unless the handbrake holds them).
	auto& drive = m_drive;
	drive.speed = drive.slide = drive.yawRate = 0.f;
	const bool clutchDown = playerHasControl && Controls::Held(input, Control::CLUTCH);
	drive.clutch = clutchDown ? (std::max)(0.f, drive.clutch - deltaTime / 0.08f) : (std::min)(1.f, drive.clutch + deltaTime / 0.45f);
	if (playerHasControl && clutchDown && Controls::Pressed(input, Control::SHIFT_UP) && drive.gear < 5)
		drive.gear++;
	if (playerHasControl && (clutchDown || drive.gear == 1) && Controls::Pressed(input, Control::SHIFT_DOWN) && drive.gear > -1)
		drive.gear--;
	drive.throttle = playerHasControl && Controls::Held(input, Control::FORWARD) ? 1.f : 0.f;
	drive.footBrake = playerHasControl && Controls::Held(input, Control::BACK);
	drive.handbrake = m_handbrakeOn;
	float revs = drive.idleRpm + drive.throttle * (REDLINE * 0.95f - drive.idleRpm);
	if (drive.cut > 0.f)
	{
		drive.cut -= deltaTime;
		drive.rpm -= REDLINE * 1.6f * deltaTime;
	}
	else
		drive.rpm += (revs - drive.rpm) * (1.f - std::exp(-deltaTime * (revs > drive.rpm ? 5.f : 1.5f)));
	if (drive.cut <= 0.f && drive.rpm >= REDLINE && drive.throttle > 0.f)
	{
		drive.cut = 0.07f;
		using namespace CarBody;
		Cue(Sound::EXHAUST_BANG, CarToWorld({ TAILPIPE[0], TAILPIPE[1], TAILPIPE[2] }), 0.9f);
	}
	if (drive.gear != 0 && drive.clutch > 0.5f && !drive.footBrake && !drive.handbrake)
	{
		float wheelSpeed = drive.rpm / REDLINE * GearTop(drive.gear) * drive.topSpeed * (drive.gear < 0 ? -1.f : 1.f);
		drive.rearSpin = std::fmod(drive.rearSpin + wheelSpeed * deltaTime / WHEEL_Y, glm::two_pi<float>());
		m_carLaidOutAt = glm::vec3(-1e9f);
	}
	LeanCar(deltaTime, 0.f, 0.f);
}

void ShopWorld::BeginCranking(Workshop& workshop)
{
	m_keyOn = true;
	auto refuse = [&](const std::string& why) {
		Cue(Sound::CLICK, GetCarPosition(), 0.5f);
		Toast(why);
		m_keyPressing = false;
		m_crankAuto = false;
	};
	// With no battery on it, or nothing from the battery to the starter, there's not a sound: only the key's click.
	workshop.SettleCarPlugs();
	if (auto dead = workshop.CarStarterDead())
		return refuse(*dead);
	// In gear it only cranks with the clutch down (its interlock): otherwise the starter would drive the car.
	if (m_drive.gear != 0 && !m_clutchHeld)
		return refuse(std::format("It's in {}: hold the clutch down ([Clutch]) to start it, or put it in neutral.", GearName(m_drive.gear)));
	m_cranking = 0.9f;
	Cue(Sound::STARTER, GetCarPosition(), 1.6f);
}

void ShopWorld::UpdateHandbrake(const InputManager& input, bool playerHasControl)
{
	if (!playerHasControl)
		return;
	// On the move it's a press: up while the key's held, down when it's let go. At a standstill (or switched off) the key
	// pulls it up and leaves it, or lets it down again.
	const bool moving = m_drive.active && std::abs(m_drive.speed) > 0.5f && !CarOffGround();
	const bool before = m_handbrakeOn;
	if (moving)
		m_handbrakeOn = Controls::Held(input, Control::JUMP);
	else if (Controls::Pressed(input, Control::JUMP))
		m_handbrakeOn = !m_handbrakeOn;
	if (m_handbrakeOn != before)
		Cue(m_handbrakeOn ? Sound::RATCHET : Sound::CLICK, CarToWorld({ CarBody::HANDBRAKE_PIVOT[0], CarBody::HANDBRAKE_PIVOT[1], CarBody::HANDBRAKE_PIVOT[2] }), 0.35f);
}

void ShopWorld::ParkedControls(float deltaTime, const InputManager& input, bool playerHasControl)
{
	// Switched off, at the wheel: it turns the front wheels (they stay where they're left), the lever goes in and out of
	// gear without the clutch, and the brake pedal lights the stop lamps with the key on.
	auto& drive = m_drive;
	drive.speed = drive.slide = drive.yawRate = drive.throttle = 0.f;
	drive.shifting = 0.f;
	drive.clutch = m_clutchHeld ? 0.f : 1.f;
	drive.footBrake = playerHasControl && Controls::Held(input, Control::BACK);
	if (!playerHasControl)
		return;
	float steerTo = (Controls::Held(input, Control::LEFT) ? 1.f : 0.f) - (Controls::Held(input, Control::RIGHT) ? 1.f : 0.f);
	if (steerTo != 0.f)
	{
		// (Heavier standing still: it goes over slower than on the move.)
		float before = drive.steer;
		drive.steer = std::clamp(drive.steer + steerTo * 1.1f * deltaTime, -CarBody::FULL_LOCK, CarBody::FULL_LOCK);
		if (drive.steer != before)
			m_carLaidOutAt = glm::vec3(-1e9f);
	}
	int gear = drive.gear;
	if (Controls::Pressed(input, Control::SHIFT_UP) && gear < 5)
		gear++;
	if (Controls::Pressed(input, Control::SHIFT_DOWN) && gear > -1)
		gear--;
	if (gear != drive.gear)
	{
		drive.gear = gear;
		Cue(Sound::CLICK, GetCarPosition(), 0.4f);
	}
}

float ShopWorld::MountLooseness(const Workshop& workshop)
{
	const auto* build = workshop.GetCar();
	if (!build)
		return 1.f;
	const auto& car = build->assembly;
	// Each mount: new and sound holds it still; worn (used) a little looser; split (damaged) far looser; missing, the
	// engine's only held by the other one and the gearbox.
	float looseness = 0.f;
	for (int side = 0; side < car.SlotCount(CarSlot::ENGINE_MOUNT); side++)
	{
		auto mount = car.Get(CarSlot::ENGINE_MOUNT, side);
		looseness += !mount ? 5.f : mount->GetCondition() == PartCondition::DAMAGED ? 3.5f : mount->GetCondition() == PartCondition::USED ? 1.4f : 1.f;
	}
	looseness /= (std::max)(1, car.SlotCount(CarSlot::ENGINE_MOUNT));
	// And their nuts and bolts: finger tight lets it knock about on them, snugged up by feel a little.
	for (CarSlot slot : { CarSlot::ENGINE_MOUNT_NUT, CarSlot::ENGINE_MOUNT_BOLT })
		for (int index = 0; index < car.SlotCount(slot); index++)
		{
			auto fastener = car.Get(slot, index);
			looseness += !fastener ? 0.6f : fastener->GetTightness() == FastenerTightness::LOOSE ? 0.5f
				: fastener->GetTightness() == FastenerTightness::SNUG ? 0.1f : 0.f;
		}
	return (std::min)(looseness, 7.f);
}

void ShopWorld::ShakeEngine(const Workshop& workshop, float deltaTime)
{
	auto& shake = m_engineShake;
	const bool running = m_drive.active && m_engineInCar >= 0;
	if (!running)
	{
		// Switched off, it settles back down on its mounts.
		const float settle = 1.f - std::exp(-deltaTime * 10.f);
		shake.roll += (0.f - shake.roll) * settle;
		shake.lift += (0.f - shake.lift) * settle;
		shake.torque = 0.f;
		if (std::abs(shake.roll) < 1e-5f && std::abs(shake.lift) < 1e-6f)
			shake.roll = shake.lift = 0.f;
		return;
	}
	shake.mounts = MountLooseness(workshop);
	// The shake: worst at idle (a four's lumpy at low revs), smoothing out as it revs, a little more with the foot down;
	// fast enough to read as a buzz, slow enough to see. (Scaled by how loose the mounts let it be.)
	const float revs = std::clamp((m_drive.rpm - m_drive.idleRpm) / 3000.f, 0.f, 1.f);
	// (On new mounts the top of the engine buzzes a couple of millimetres at idle; on a split one, near a centimetre.)
	const float strength = (0.0055f * (1.f - 0.6f * revs) + 0.002f * m_drive.throttle) * shake.mounts;
	shake.phase = std::fmod(shake.phase + deltaTime * glm::two_pi<float>() * (11.f + 9.f * revs), glm::two_pi<float>() * 100.f);
	// The torque roll: it leans over against its mounts with the throttle, and kicks back as the limiter cuts it.
	const float lean = (m_drive.cut > 0.f ? -0.6f : m_drive.throttle) * 0.012f * shake.mounts;
	shake.torque += (lean - shake.torque) * (1.f - std::exp(-deltaTime * (6.f / std::sqrt(shake.mounts))));
	shake.roll = shake.torque + strength * (std::sin(shake.phase) + 0.45f * std::sin(shake.phase * 2.37f + 1.f));
	shake.lift = strength * 0.3f * std::sin(shake.phase * 1.31f + 0.4f);
}

glm::vec2 ShopWorld::GearGate(int gear)
{
	// The gate, as the driver sees it: first and second on the left, third and fourth in the middle, fifth and reverse on
	// the right; the odd ones forward, the even ones (and reverse) back. Neutral in the middle of it.
	switch (gear)
	{
		case 1:		return { 1.f, 1.f };
		case 2:		return { 1.f, -1.f };
		case 3:		return { 0.f, 1.f };
		case 4:		return { 0.f, -1.f };
		case 5:		return { -1.f, 1.f };
		case -1:	return { -1.f, -1.f };
		default:	return { 0.f, 0.f };
	}
}

void ShopWorld::IdleCar(float deltaTime, Workshop& workshop)
{
	auto& drive = m_drive;
	drive.speed = drive.slide = drive.yawRate = 0.f;
	drive.throttle = 0.f;
	LeanCar(deltaTime, 0.f, 0.f);
	// Left in gear with no foot on the clutch, it stalls.
	if (drive.gear != 0)
	{
		Toast("It stalled: it was left in gear.");
		return EndDrive();
	}
	drive.rpm += (drive.idleRpm - drive.rpm) * (1.f - std::exp(-deltaTime * 4.f));
	if (!workshop.DriveCar(deltaTime, 0.0, 0.0))
	{
		Toast("It's run out of fuel.");
		EndDrive();
	}
}

std::optional<ShopWorld::CarControl> ShopWorld::AimedCarControl(const Workshop& workshop, const FirstPersonController& player) const
{
	const auto* build = workshop.GetCar();
	if (!build || m_carStation >= m_stations.size() || !m_stations[m_carStation].present || (IsSeated() && m_thirdPerson))
		return std::nullopt;
	const auto& car = build->assembly;
	glm::vec3 eye = m_thirdPerson ? m_thirdPersonView.position : player.GetEyePosition();
	glm::vec3 forward = m_thirdPerson ? m_thirdPersonView.rotation * glm::vec3(0.f, 0.f, -1.f) : player.GetForward();
	std::optional<CarControl> nearest;
	float nearestOff = 0.f;
	// The one nearest the line of sight, within arm's reach and close enough to it to be meant.
	auto consider = [&](CarControl control, const glm::vec3& onCar, float size) {
		glm::vec3 at = CarToWorld(onCar);
		float along = glm::dot(at - eye, forward);
		if (along <= 0.f || along > 1.6f)
			return;
		float off = glm::length(at - (eye + forward * along));
		if (off < size && (!nearest || off < nearestOff))
		{
			nearest = control;
			nearestOff = off;
		}
	};
	using namespace CarBody;
	if (car.IsFilled(CarSlot::DASHBOARD, 0))
	{
		consider(CarControl::HEADLIGHTS, { HEADLIGHT_KNOB[0], HEADLIGHT_KNOB[1], HEADLIGHT_KNOB[2] }, 0.045f);
		consider(CarControl::HOOD_RELEASE, { HOOD_RELEASE[0], HOOD_RELEASE[1], HOOD_RELEASE[2] }, 0.06f);
		if (m_seat == 0)
			consider(CarControl::IGNITION, { IGNITION_BARREL[0], IGNITION_BARREL[1], IGNITION_BARREL[2] }, 0.045f);
	}
	// From a seat, the doors' inside handles (wherever the doors have swung to).
	if (IsSeated())
		for (int side = 0; side < 2; side++)
			if (car.IsFilled(CarSlot::DOOR, side))
			{
				auto pose = PoseOnCar(CarSlot::DOOR, side);
				float in = side == 0 ? 1.f : -1.f;
				consider(side == 0 ? CarControl::DOOR_LEFT : CarControl::DOOR_RIGHT, pose.offset + pose.rotation * glm::vec3(DOOR_HANDLE_X, DOOR_HANDLE_Y, in * DOOR_HANDLE_IN), 0.09f);
			}
	// The front seats' own controls, each on the seat's side by its door. From the seat itself, the bar that lets it
	// slide and the knob that leans its back; from anywhere else that can get at it (another seat, or outside with
	// that door open), the catch that tips its back forward.
	for (int side = 0; side < 2; side++)
		if (car.IsFilled(CarSlot::SEAT, side))
		{
			auto pose = PoseOnCar(CarSlot::SEAT, side);
			const float out = side == 0 ? 1.f : -1.f;		// the controls are built on the seat's left: mirrored for the right one
			if (m_seat == side)
			{
				consider(CarControl::SEAT_SLIDE, pose.offset + glm::vec3(SEAT_SLIDE_BAR[0], SEAT_SLIDE_BAR[1], 0.f), 0.1f);
				consider(CarControl::SEAT_RECLINE, pose.offset + glm::vec3(SEAT_RECLINER[0], SEAT_RECLINER[1], out * SEAT_RECLINER[2]), 0.07f);
				continue;
			}
			CarPanel door = side == 0 ? CarPanel::DOOR_LEFT : CarPanel::DOOR_RIGHT;
			if (!IsSeated() && car.IsFilled(CarSlot::DOOR, side) && CarPanelOpen(door) < 0.5f)
				continue;
			const auto& set = m_seats[side];
			float angle = glm::mix(set.recline, SEAT_FOLD, glm::smoothstep(0.f, 1.f, set.fold));
			glm::vec3 catchAt = pose.offset + glm::vec3(SEAT_HINGE_X, SEAT_HINGE_Y, 0.f)
				+ Pitch(angle) * glm::vec3(SEAT_FOLD_CATCH[0], SEAT_FOLD_CATCH[1], out * (SEAT_FOLD_CATCH[2] - 0.02f));
			consider(side == 0 ? CarControl::SEAT_FOLD_LEFT : CarControl::SEAT_FOLD_RIGHT, catchAt, 0.06f);
		}
	return nearest;
}

bool ShopWorld::UpdateCarControls(Workshop& workshop, const FirstPersonController& player, const InputManager& input)
{
	m_aimedCarControl = false;
	auto aimed = AimedCarControl(workshop, player);
	if (!aimed)
		return false;
	m_aimedCarControl = true;
	std::string hint;
	CarPanel door = *aimed == CarControl::DOOR_LEFT ? CarPanel::DOOR_LEFT : CarPanel::DOOR_RIGHT;
	// The seat's own adjusters go both ways: the left button one, the right the other.
	if ((*aimed == CarControl::SEAT_SLIDE || *aimed == CarControl::SEAT_RECLINE) && m_seat >= 0 && m_seat < 2)
	{
		using namespace CarBody;
		const bool slide = *aimed == CarControl::SEAT_SLIDE;
		hint = slide ? "[LMB] Slide the seat forward   [RMB] Slide it back" : "[LMB] Sit the seat's back up   [RMB] Lean it back";
		m_prompt = m_prompt.empty() ? hint : hint + "   " + m_prompt;
		const int way = Controls::Pressed(input, Control::PRIMARY) || std::exchange(m_devPress, false) ? 1 : Controls::Pressed(input, Control::SET_DOWN) ? -1 : 0;
		if (way == 0)
			return false;
		CarSeatSetting set = workshop.GetCar()->assembly.GetSeatSetting(m_seat);
		float& value = slide ? set.slide : set.recline;
		const float before = value;
		value = slide ? std::clamp(value + way * SEAT_SLIDE_STEP, -SEAT_SLIDE, SEAT_SLIDE)
			: std::clamp(value - way * SEAT_RECLINE_STEP, SEAT_RECLINE_MIN, SEAT_RECLINE_MAX);
		if (value == before)
			Toast(slide ? "That's as far as the seat slides." : "That's as far as its back goes.");
		else
			Cue(Sound::CLICK, GetCarPosition(), 0.6f);
		workshop.SetCarSeat(m_seat, set);
		return true;
	}
	if (*aimed == CarControl::SEAT_FOLD_LEFT || *aimed == CarControl::SEAT_FOLD_RIGHT)
	{
		const int side = *aimed == CarControl::SEAT_FOLD_LEFT ? 0 : 1;
		CarSeatSetting set = workshop.GetCar()->assembly.GetSeatSetting(side);
		hint = set.folded ? "[LMB] Put the seat's back up" : "[LMB] Tip the seat's back forward";
		m_prompt = m_prompt.empty() ? hint : hint + "   " + m_prompt;
		if (!Controls::Pressed(input, Control::PRIMARY) && !std::exchange(m_devPress, false))
			return false;
		set.folded = !set.folded;
		workshop.SetCarSeat(side, set);
		Cue(set.folded ? Sound::PICK_UP : Sound::SET_DOWN, GetCarPosition());
		return true;
	}
	// The key: a click turns it on or off (or switches the engine off); held, it starts it (see Drive).
	if (*aimed == CarControl::IGNITION)
	{
		hint = m_drive.active ? "[LMB] Switch it off" : m_cranking > 0.f ? "Cranking: keep holding"
			: m_keyOn ? "[LMB] Key off   Hold [LMB] Start it" : "[LMB] Key on   Hold [LMB] Start it";
		m_prompt = m_prompt.empty() ? hint : hint + "   " + m_prompt;
		if (!Controls::Pressed(input, Control::PRIMARY) && !std::exchange(m_devPress, false))
			return false;
		if (m_drive.active)
			TurnKey(workshop);
		else
		{
			m_keyPressing = true;
			m_keyPressedAt = m_time;
		}
		return true;
	}
	switch (*aimed)
	{
		case CarControl::HEADLIGHTS:	hint = m_headlights.on ? "[LMB] Headlights off" : "[LMB] Headlights on"; break;
		case CarControl::HOOD_RELEASE:	hint = "[LMB] Pull the hood's release"; break;
		case CarControl::IGNITION:		hint = m_drive.active ? "[LMB] Switch it off" : "[LMB] Turn the key"; break;
		default:						hint = IsCarPanelOpen(door) ? "[LMB] Shut the door" : "[LMB] Open the door"; break;
	}
	m_prompt = m_prompt.empty() ? hint : hint + "   " + m_prompt;
	if (!Controls::Pressed(input, Control::PRIMARY) && !std::exchange(m_devPress, false))
		return false;
	switch (*aimed)
	{
		case CarControl::HEADLIGHTS:
			ToggleHeadlights(workshop);
			break;
		case CarControl::HOOD_RELEASE:
			if (IsCarPanelOpen(CarPanel::HOOD) || !m_hoodLatched)
				Toast("It's released already: lift the hood from the front.");
			else
			{
				m_hoodLatched = false;
				m_carLaidOutAt = glm::vec3(-1e9f);	// the hood's laid out again, popped
				Cue(Sound::CLICK, GetCarPosition());
				Toast("The hood's popped: lift it from the front.");
			}
			break;
		case CarControl::IGNITION:
			TurnKey(workshop);
			break;
		default:
			if (std::abs(m_drive.speed) > 2.f)
				Toast("Not while it's moving.");
			else
				ToggleCarPanel(door);
			break;
	}
	return true;
}

void ShopWorld::DevSit(int seat, Workshop& workshop, FirstPersonController& player)
{
	LeaveSeat(&player);
	Sit(seat, workshop);
	// Faced forward now, so a look after it stays where it's put.
	if (m_seat >= 0)
	{
		player.SetYaw(m_stations[m_carStation].yaw - glm::half_pi<float>());
		player.SetPitch(-0.06f);
		m_seatJustIn = false;
	}
}

void ShopWorld::LeanCar(float deltaTime, float alongAcceleration, float sideAcceleration)
{
	// On its springs: each way it leans is pulled to where the push on it would hold it, and damped (less, on tired
	// dampers, so it wallows). Turning left (a push to its left) rolls it to its right; pulling away lifts its nose.
	auto& drive = m_drive;
	constexpr float STIFFNESS = 9.f, ROLL_PER = 0.0058f, PITCH_PER = 0.0032f;
	auto spring = [&](float& angle, float& rate, float target) {
		for (int i = 0; i < 4; i++)
		{
			float h = deltaTime * 0.25f;
			rate += (STIFFNESS * STIFFNESS * (target - angle) - 2.f * drive.damping * STIFFNESS * rate) * h;
			angle += rate * h;
		}
	};
	spring(drive.roll, drive.rollRate, std::clamp(sideAcceleration * ROLL_PER * drive.lean, -0.1f, 0.1f));
	spring(drive.pitch, drive.pitchRate, std::clamp(alongAcceleration * PITCH_PER * drive.lean, -0.06f, 0.06f));
}

