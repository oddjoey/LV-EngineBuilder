#include "CarAssembly.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

namespace
{
	// How a slot's positions are laid out on the car.
	enum class Layout
	{
		SINGLE,		// one
		SIDES,		// left and right, not at a wheel (doors, seats, fenders, engine mounts)
		FRONT,		// the front wheels' corners
		REAR,		// the rear wheels'
		FOUR,		// all four corners
		LIST		// numbered, each its own (the hoses)
	};

	// A part slot's prerequisite: the matching position of another slot (same corner or side where both have
	// one), or every position of it.
	struct Need
	{
		CarSlot slot = CarSlot::BODY_SHELL;
		bool all = false;
	};

	struct SlotRule
	{
		int count = 1;
		Layout layout = Layout::SINGLE;
		std::vector<Need> needs = {};
	};

	using C = CarSlot;
	using L = Layout;

	// One rule per part slot and the engine, in CarSlot order: the order a bare shell is built up in.
	const SlotRule RULES[] = {
		/* BODY_SHELL */				{ 1, L::SINGLE, {} },
		/* FRONT_FENDER */				{ 2, L::SIDES,	{ { C::BODY_SHELL } } },
		/* DOOR */						{ 2, L::SIDES,	{ { C::BODY_SHELL } } },
		/* HOOD */						{ 1, L::SINGLE, { { C::FRONT_FENDER, true } } },
		/* TRUNK_LID */					{ 1, L::SINGLE, { { C::BODY_SHELL } } },
		/* FRONT_BUMPER */				{ 1, L::SINGLE, { { C::FRONT_FENDER, true } } },
		/* REAR_BUMPER */				{ 1, L::SINGLE, { { C::BODY_SHELL } } },
		/* HEADLIGHT */					{ 2, L::SIDES,	{ { C::FRONT_FENDER }, { C::MAIN_HARNESS } } },
		/* TAIL_LIGHT */				{ 2, L::SIDES,	{ { C::MAIN_HARNESS } } },
		/* WINDSHIELD */				{ 1, L::SINGLE, { { C::BODY_SHELL } } },
		/* REAR_WINDOW */				{ 1, L::SINGLE, { { C::BODY_SHELL } } },
		/* DASHBOARD */					{ 1, L::SINGLE, { { C::MAIN_HARNESS }, { C::FUSE_BOX } } },
		/* INSTRUMENT_CLUSTER */		{ 1, L::SINGLE, { { C::DASHBOARD } } },
		/* SEAT */						{ 2, L::SIDES,	{ { C::MAIN_HARNESS } } },
		/* STEERING_WHEEL */			{ 1, L::SINGLE, { { C::STEERING_COLUMN } } },
		// A strut goes in with its spring already compressed onto it (the spring is fitted first, off the car).
		/* FRONT_STRUT */				{ 2, L::FRONT,	{ { C::BODY_SHELL }, { C::FRONT_COIL_SPRING } } },
		/* FRONT_COIL_SPRING */			{ 2, L::FRONT,	{ { C::BODY_SHELL } } },
		/* STEERING_KNUCKLE */			{ 2, L::FRONT,	{ { C::FRONT_STRUT }, { C::LOWER_CONTROL_ARM } } },
		/* LOWER_CONTROL_ARM */			{ 2, L::FRONT,	{ { C::BODY_SHELL } } },
		/* STRUT_ROD */					{ 2, L::FRONT,	{ { C::LOWER_CONTROL_ARM } } },
		/* FRONT_SWAY_BAR */			{ 1, L::SINGLE, { { C::LOWER_CONTROL_ARM, true } } },
		/* REAR_AXLE */					{ 1, L::SINGLE, { { C::BODY_SHELL } } },
		/* DIFFERENTIAL */				{ 1, L::SINGLE, { { C::REAR_AXLE } } },
		/* LOWER_TRAILING_LINK */		{ 2, L::REAR,	{ { C::REAR_AXLE } } },
		/* UPPER_TRAILING_LINK */		{ 2, L::REAR,	{ { C::REAR_AXLE } } },
		/* LATERAL_ROD */				{ 1, L::SINGLE, { { C::REAR_AXLE } } },
		/* REAR_COIL_SPRING */			{ 2, L::REAR,	{ { C::LOWER_TRAILING_LINK } } },
		/* REAR_SHOCK */				{ 2, L::REAR,	{ { C::REAR_AXLE } } },
		/* STEERING_RACK */				{ 1, L::SINGLE, { { C::BODY_SHELL } } },
		/* TIE_ROD_END */				{ 2, L::FRONT,	{ { C::STEERING_RACK }, { C::STEERING_KNUCKLE } } },
		/* STEERING_COLUMN */			{ 1, L::SINGLE, { { C::STEERING_RACK }, { C::DASHBOARD } } },
		/* BRAKE_BOOSTER */				{ 1, L::SINGLE, { { C::BODY_SHELL } } },
		/* BRAKE_MASTER_CYLINDER */		{ 1, L::SINGLE, { { C::BRAKE_BOOSTER } } },
		/* BRAKE_LINE_SET */			{ 1, L::SINGLE, { { C::BRAKE_MASTER_CYLINDER } } },
		/* BRAKE_HOSE */				{ 3, L::LIST,	{ { C::BRAKE_LINE_SET } } },	// and see ComputePrerequisites
		/* BRAKE_ROTOR */				{ 2, L::FRONT,	{ { C::STEERING_KNUCKLE } } },
		/* BRAKE_PAD_SET */				{ 2, L::FRONT,	{ { C::BRAKE_ROTOR } } },
		/* BRAKE_CALIPER */				{ 2, L::FRONT,	{ { C::BRAKE_PAD_SET } } },
		/* WHEEL_CYLINDER */			{ 2, L::REAR,	{ { C::REAR_AXLE } } },
		/* BRAKE_SHOE_SET */			{ 2, L::REAR,	{ { C::WHEEL_CYLINDER } } },
		/* BRAKE_DRUM */				{ 2, L::REAR,	{ { C::BRAKE_SHOE_SET } } },
		/* PARKING_BRAKE_CABLE */		{ 1, L::SINGLE, { { C::BRAKE_SHOE_SET, true } } },
		/* ENGINE_MOUNT */				{ 2, L::SIDES,	{ { C::BODY_SHELL } } },
		/* CLUTCH_DISC */				{ 1, L::SINGLE, { { C::ENGINE } } },
		/* PRESSURE_PLATE */			{ 1, L::SINGLE, { { C::CLUTCH_DISC } } },
		/* RELEASE_BEARING */			{ 1, L::SINGLE, { { C::PRESSURE_PLATE } } },
		/* TRANSMISSION */				{ 1, L::SINGLE, { { C::RELEASE_BEARING } } },
		/* TRANSMISSION_MOUNT */		{ 1, L::SINGLE, { { C::TRANSMISSION } } },
		/* CLUTCH_MASTER_CYLINDER */	{ 1, L::SINGLE, { { C::BODY_SHELL } } },
		/* CLUTCH_RELEASE_CYLINDER */	{ 1, L::SINGLE, { { C::TRANSMISSION }, { C::CLUTCH_MASTER_CYLINDER } } },
		/* DRIVESHAFT */				{ 1, L::SINGLE, { { C::TRANSMISSION }, { C::DIFFERENTIAL } } },
		/* FUEL_TANK */					{ 1, L::SINGLE, { { C::BODY_SHELL } } },
		/* FUEL_SENDER */				{ 1, L::SINGLE, { { C::FUEL_TANK } } },
		/* FUEL_LINE_SET */				{ 1, L::SINGLE, { { C::FUEL_TANK } } },
		/* FUEL_FILTER */				{ 1, L::SINGLE, { { C::FUEL_LINE_SET } } },
		/* BATTERY */					{ 1, L::SINGLE, { { C::MAIN_HARNESS } } },
		/* MAIN_HARNESS */				{ 1, L::SINGLE, { { C::BODY_SHELL } } },
		/* ENGINE_HARNESS */			{ 1, L::SINGLE, { { C::MAIN_HARNESS }, { C::ENGINE } } },
		/* FUSE_BOX */					{ 1, L::SINGLE, { { C::MAIN_HARNESS } } },
		/* IGNITION_COIL */				{ 1, L::SINGLE, { { C::ENGINE_HARNESS } } },
		/* STARTER */					{ 1, L::SINGLE, { { C::TRANSMISSION }, { C::ENGINE_HARNESS } } },
		/* RADIATOR */					{ 1, L::SINGLE, { { C::BODY_SHELL } } },
		/* RADIATOR_HOSE */				{ 2, L::LIST,	{ { C::RADIATOR }, { C::ENGINE } } },
		/* COOLING_FAN */				{ 1, L::SINGLE, { { C::RADIATOR }, { C::MAIN_HARNESS } } },
		/* CATALYTIC_CONVERTER */		{ 1, L::SINGLE, { { C::ENGINE } } },
		/* MUFFLER */					{ 1, L::SINGLE, { { C::CATALYTIC_CONVERTER } } },
		// A wheel goes on with its tire mounted (fitted first, off the car), over the brake at its corner.
		/* WHEEL */						{ 4, L::FOUR,	{ { C::TIRE }, { C::BRAKE_CALIPER }, { C::BRAKE_DRUM } } },
		/* TIRE */						{ 4, L::FOUR,	{ { C::BODY_SHELL } } },
		/* ENGINE */					{ 1, L::SINGLE, { { C::ENGINE_MOUNT, true } } },
	};
	static_assert(std::size(RULES) == static_cast<size_t>(CarSlot::ENGINE) + 1, "Every car part slot needs a rule.");

	// Every set of the car's holes, in CarSlot order from STRUT_MOUNT_NUT. The engine's own mounting points are
	// on its BLOCK.
	const std::vector<FastenerHoles> HOLES = {
		//  name						parent							per	size				Nm
		{ "Strut Mount Nut",			PartType::FRONT_STRUT,			3,	PartType::NUT_M8,	23.0 },
		{ "Strut to Knuckle Bolt",		PartType::STEERING_KNUCKLE,		2,	PartType::BOLT_M12,	145.0 },
		{ "Ball Joint Bolt",			PartType::STEERING_KNUCKLE,		2,	PartType::BOLT_M10,	64.0 },
		{ "Control Arm Bolt",			PartType::LOWER_CONTROL_ARM,	1,	PartType::BOLT_M12,	83.0 },
		{ "Strut Rod Nut",				PartType::STRUT_ROD,			2,	PartType::NUT_M12,	88.0 },
		{ "Sway Bar Bolt",				PartType::FRONT_SWAY_BAR,		4,	PartType::BOLT_M8,	19.0 },
		{ "Tie Rod End Nut",			PartType::TIE_ROD_END,			1,	PartType::NUT_M12,	59.0 },
		{ "Steering Rack Bolt",			PartType::STEERING_RACK,		4,	PartType::BOLT_M12,	57.0 },
		{ "Caliper Bolt",				PartType::BRAKE_CALIPER,		2,	PartType::BOLT_M12,	88.0 },
		{ "Lower Link Bolt",			PartType::LOWER_TRAILING_LINK,	2,	PartType::BOLT_M12,	118.0 },
		{ "Upper Link Bolt",			PartType::UPPER_TRAILING_LINK,	2,	PartType::BOLT_M12,	118.0 },
		{ "Lateral Rod Bolt",			PartType::LATERAL_ROD,			2,	PartType::BOLT_M12,	64.0 },
		{ "Rear Shock Bolt",			PartType::REAR_SHOCK,			2,	PartType::BOLT_M10,	36.0 },
		{ "Differential Nut",			PartType::DIFFERENTIAL,			8,	PartType::NUT_M8,	23.0 },
		{ "Engine Mount Bolt",			PartType::ENGINE_MOUNT,			2,	PartType::BOLT_M10,	39.0 },
		{ "Engine Mount Nut",			PartType::BLOCK,				2,	PartType::NUT_M10,	59.0 },
		{ "Pressure Plate Bolt",		PartType::PRESSURE_PLATE,		6,	PartType::BOLT_M8,	19.0 },
		{ "Bellhousing Bolt",			PartType::TRANSMISSION,			4,	PartType::BOLT_M12,	64.0 },
		{ "Transmission Mount Bolt",	PartType::TRANSMISSION_MOUNT,	4,	PartType::BOLT_M10,	26.0 },
		{ "Driveshaft Flange Nut",		PartType::DRIVESHAFT,			4,	PartType::NUT_M10,	74.0 },
		{ "Starter Bolt",				PartType::STARTER,				2,	PartType::BOLT_M10,	39.0 },
		{ "Master Cylinder Nut",		PartType::BRAKE_MASTER_CYLINDER, 2,	PartType::NUT_M10,	13.0 },
		{ "Brake Booster Nut",			PartType::BRAKE_BOOSTER,		4,	PartType::NUT_M8,	13.0 },
		{ "Fuel Tank Bolt",				PartType::FUEL_TANK,			4,	PartType::BOLT_M8,	20.0 },
		{ "Seat Bolt",					PartType::SEAT,					4,	PartType::BOLT_M10,	37.0 },
		{ "Fender Bolt",				PartType::FRONT_FENDER,			8,	PartType::BOLT_M6,	6.0 },
		{ "Door Hinge Bolt",			PartType::DOOR,					4,	PartType::BOLT_M8,	26.0 },
		{ "Hood Hinge Bolt",			PartType::HOOD,					4,	PartType::BOLT_M8,	13.0 },
		{ "Front Bumper Bolt",			PartType::FRONT_BUMPER,			4,	PartType::BOLT_M10,	29.0 },
		{ "Catalytic Converter Nut",	PartType::CATALYTIC_CONVERTER,	3,	PartType::NUT_M10,	43.0 },
		{ "Lug Nut",					PartType::WHEEL,				4,	PartType::LUG_NUT,	103.0 },
	};

	const SlotRule& Rule(CarSlot slot)
	{
		return RULES[static_cast<size_t>(slot)];
	}

	bool HasCorners(Layout layout) { return layout == L::FRONT || layout == L::REAR || layout == L::FOUR; }

	// Which side a position is on (0 left, 1 right), where it has one.
	std::optional<int> SideOf(CarSlot slot, int index)
	{
		switch (Rule(slot).layout)
		{
			case L::SIDES:
			case L::FRONT:
			case L::REAR:	return index;
			case L::FOUR:	return index % 2;
			default:		return std::nullopt;
		}
	}

	// What the circuit's parts are, for CircuitOpen: every position of each.
	std::vector<CarSlot> CircuitParts(FluidCircuit circuit)
	{
		switch (circuit)
		{
			case FluidCircuit::ENGINE_OIL:		return { C::ENGINE };
			case FluidCircuit::COOLANT:			return { C::ENGINE, C::RADIATOR, C::RADIATOR_HOSE };
			case FluidCircuit::BRAKES:			return { C::BRAKE_MASTER_CYLINDER, C::BRAKE_LINE_SET, C::BRAKE_HOSE, C::BRAKE_CALIPER, C::WHEEL_CYLINDER };
			case FluidCircuit::CLUTCH:			return { C::CLUTCH_MASTER_CYLINDER, C::CLUTCH_RELEASE_CYLINDER };
			case FluidCircuit::TRANSMISSION:	return { C::TRANSMISSION };
			case FluidCircuit::DIFFERENTIAL:	return { C::REAR_AXLE, C::DIFFERENTIAL };
			case FluidCircuit::FUEL:			return { C::FUEL_TANK, C::FUEL_SENDER };
			case FluidCircuit::COUNT:			break;
		}
		return {};
	}

	// How full a circuit can be got: the engine's sump can be overfilled; everything else overflows at its mark.
	double MaxFill(const FluidSpec& spec)
	{
		return spec.circuit == FluidCircuit::ENGINE_OIL ? spec.capacity * 1.4 : spec.capacity;
	}

	// Litres out of the nipple with each go (half that for the clutch's small bore): a dozen goes empties the
	// brake reservoir down to where it starts drawing air, so it wants topping up partway round.
	constexpr double BLEED_FLUID = 0.02;
	constexpr double BLEED_KEEPS = 0.4;		// of the air in the line left after each go
}

const char* CornerName(Corner corner)
{
	switch (corner)
	{
		case Corner::FRONT_LEFT:	return "Front Left";
		case Corner::FRONT_RIGHT:	return "Front Right";
		case Corner::REAR_LEFT:		return "Rear Left";
		case Corner::REAR_RIGHT:	return "Rear Right";
	}
	return "";
}

const char* CarSlotName(CarSlot slot)
{
	if (slot == CarSlot::ENGINE)
		return "Engine";
	if (CarAssembly::IsFastenerSlot(slot))
		return CarAssembly::Holes(slot)->name;
	if (static_cast<int>(slot) < CAR_PART_SLOTS)
		return PartTypeName(CarAssembly::SlotPartType(slot));
	return "";
}

CarTune CarTune::Factory(const CarSpec& spec)
{
	CarTune tune;
	tune.ignitionTiming = spec.tune.ignitionTimingNominal;
	tune.idleSpeed = spec.tune.idleSpeedNominal;
	tune.idleMixture = spec.tune.idleMixtureNominal;
	tune.mainJet = spec.tune.mainJetNominal;
	tune.intakeLash = spec.tune.intakeLashNominal;
	tune.exhaustLash = spec.tune.exhaustLashNominal;
	tune.frontCamber = spec.tune.frontCamberNominal;
	tune.caster = spec.tune.casterNominal;
	tune.frontToe = spec.tune.frontToeNominal;
	tune.tirePressure.fill(spec.tune.tirePressureNominal);
	tune.finalDrive = 0.0;
	return tune;
}

// ---- Slots ----

CarAssembly::CarAssembly(const CarSpec& spec)
	: m_spec(&spec), m_tune(CarTune::Factory(spec))
{
	for (size_t slot = 0; slot < SLOTS; slot++)
	{
		int count = SlotCount(static_cast<CarSlot>(slot));
		m_slots[slot].resize(count);
		m_prerequisites[slot].resize(count);
		m_dependents[slot].resize(count);
	}
	for (size_t slot = 0; slot < SLOTS; slot++)
		for (int index = 0; index < SlotCount(static_cast<CarSlot>(slot)); index++)
		{
			m_prerequisites[slot][index] = ComputePrerequisites(static_cast<CarSlot>(slot), index);
			for (const auto& [other, otherIndex] : m_prerequisites[slot][index])
				m_dependents[static_cast<size_t>(other)][otherIndex].push_back({ static_cast<CarSlot>(slot), index });
		}
	// A brake or clutch circuit that's never been filled is all air.
	for (auto& fluid : m_fluids)
		fluid.air.fill(1.0);
	m_fluids[static_cast<size_t>(FluidCircuit::COOLANT)].airPocket = 1.0;
}

const CarSpec& CarAssembly::GetSpec() const
{
	return *m_spec;
}

PartType CarAssembly::SlotPartType(CarSlot slot)
{
	if (static_cast<int>(slot) < CAR_PART_SLOTS)
		return static_cast<PartType>(static_cast<int>(FIRST_CAR_PART) + static_cast<int>(slot));
	if (slot == CarSlot::ENGINE)
		return PartType::BLOCK;
	if (const auto* holes = Holes(slot))
		return holes->size;
	return PartType::COUNT;
}

CarSlot CarAssembly::SlotFor(PartType type)
{
	if (type == PartType::BLOCK)
		return CarSlot::ENGINE;
	for (int slot = 0; slot < CAR_PART_SLOTS; slot++)
		if (SlotPartType(static_cast<CarSlot>(slot)) == type)
			return static_cast<CarSlot>(slot);
	// A fastener size: the first set of holes it goes in.
	for (int slot = static_cast<int>(CarSlot::STRUT_MOUNT_NUT); slot < static_cast<int>(CarSlot::COUNT); slot++)
		if (SlotPartType(static_cast<CarSlot>(slot)) == type)
			return static_cast<CarSlot>(slot);
	return CarSlot::COUNT;
}

bool CarAssembly::IsFastenerSlot(CarSlot slot)
{
	return slot >= CarSlot::STRUT_MOUNT_NUT && slot < CarSlot::COUNT;
}

const std::vector<FastenerHoles>& CarAssembly::Holes()
{
	if (HOLES.size() != static_cast<size_t>(CAR_FASTENER_SLOTS))
		throw std::runtime_error("Every car fastener slot needs a set of holes!");
	return HOLES;
}

const FastenerHoles* CarAssembly::Holes(CarSlot slot)
{
	if (!IsFastenerSlot(slot))
		return nullptr;
	return &Holes()[static_cast<size_t>(slot) - static_cast<size_t>(CarSlot::STRUT_MOUNT_NUT)];
}

std::optional<CarSlot> CarAssembly::CarriedBy(CarSlot slot)
{
	switch (slot)
	{
		case CarSlot::FRONT_COIL_SPRING:	return CarSlot::FRONT_STRUT;
		case CarSlot::TIRE:					return CarSlot::WHEEL;
		default:							return std::nullopt;
	}
}

int CarAssembly::SlotCount(CarSlot slot) const
{
	if (const auto* holes = Holes(slot))
		return holes->Count(SlotCount(SlotFor(holes->parent)));
	if (slot < CarSlot::COUNT && static_cast<int>(slot) <= static_cast<int>(CarSlot::ENGINE))
		return Rule(slot).count;
	return 0;
}

std::optional<Corner> CarAssembly::CornerOf(CarSlot slot, int index)
{
	if (const auto* holes = Holes(slot))
	{
		auto parent = SlotFor(holes->parent);
		return holes->throughAll ? std::nullopt : CornerOf(parent, index / holes->perParent);
	}
	if (static_cast<int>(slot) > static_cast<int>(CarSlot::ENGINE))
		return std::nullopt;
	switch (Rule(slot).layout)
	{
		case L::FRONT:	return static_cast<Corner>(index);
		case L::REAR:	return static_cast<Corner>(2 + index);
		case L::FOUR:	return static_cast<Corner>(index);
		default:		break;
	}
	if (slot == CarSlot::BRAKE_HOSE && index < 2)
		return static_cast<Corner>(index);
	return std::nullopt;
}

std::string CarAssembly::PositionName(CarSlot slot, int index) const
{
	if (const auto* holes = Holes(slot))
	{
		auto parent = SlotFor(holes->parent);
		if (holes->throughAll || SlotCount(parent) <= 1)
			return std::format("{} {}", holes->name, index + 1);
		return std::format("{} {} ({})", holes->name, index % holes->perParent + 1, PositionName(parent, index / holes->perParent));
	}
	std::string name = CarSlotName(slot);
	if (slot == CarSlot::ENGINE || static_cast<int>(slot) > static_cast<int>(CarSlot::ENGINE))
		return name;
	switch (Rule(slot).layout)
	{
		case L::SIDES:	return std::format("{} {}", index == 0 ? "Left" : "Right", name);
		case L::FRONT:
		case L::REAR:
		case L::FOUR:
			// "Front Left Strut", not "Front Left Front Strut".
			for (const char* end : { "Front ", "Rear " })
				if (name.starts_with(end))
					name = name.substr(std::char_traits<char>::length(end));
			return std::format("{} {}", CornerName(*CornerOf(slot, index)), name);
		case L::LIST:
			if (slot == CarSlot::RADIATOR_HOSE)
				return std::format("{} {}", index == 0 ? "Upper" : "Lower", name);
			if (slot == CarSlot::BRAKE_HOSE)
				return index < 2 ? std::format("{} {}", CornerName(static_cast<Corner>(index)), name) : std::format("Rear Axle {}", name);
			return std::format("{} {}", name, index + 1);
		case L::SINGLE:	break;
	}
	return name;
}

CarAssembly::Positions CarAssembly::ComputePrerequisites(CarSlot slot, int index) const
{
	Positions needs;
	auto add = [&](CarSlot other, int otherIndex) {
		if (std::find(needs.begin(), needs.end(), std::make_pair(other, otherIndex)) == needs.end())
			needs.push_back({ other, otherIndex });
	};

	if (const auto* holes = Holes(slot))
	{
		// A nut or bolt needs what it holds on (or, through all of them, every one).
		auto parent = SlotFor(holes->parent);
		if (holes->throughAll)
			for (int i = 0; i < SlotCount(parent); i++)
				add(parent, i);
		else
			add(parent, index / holes->perParent);
		return needs;
	}

	// The matching positions of a needed slot: the same corner or side where both have one, all of it otherwise.
	auto addMatching = [&](const Need& need) {
		int count = SlotCount(need.slot);
		if (need.all || count == 1)
		{
			for (int i = 0; i < count; i++)
				add(need.slot, i);
			return;
		}
		auto corner = CornerOf(slot, index);
		bool otherHasCorners = HasCorners(Rule(need.slot).layout);
		if (corner && otherHasCorners)
		{
			// Only where there's one at this corner: a rear wheel doesn't wait for a front caliper.
			for (int i = 0; i < count; i++)
				if (CornerOf(need.slot, i) == corner)
					add(need.slot, i);
			return;
		}
		auto side = SideOf(slot, index);
		if (corner)
			side = static_cast<int>(*corner) % 2;
		if (side && SideOf(need.slot, 0))
		{
			for (int i = 0; i < count; i++)
				if (SideOf(need.slot, i) == side && (!otherHasCorners || CornerOf(need.slot, i)))
					add(need.slot, i);
			return;
		}
		for (int i = 0; i < count; i++)
			add(need.slot, i);
	};

	for (const auto& need : Rule(slot).needs)
		addMatching(need);
	// The rear brake hose runs down to the axle's tee, where both wheel cylinders' lines meet; the front ones
	// screw into their calipers.
	if (slot == CarSlot::BRAKE_HOSE)
	{
		if (index < 2)
			add(CarSlot::BRAKE_CALIPER, index);
		else
		{
			add(CarSlot::REAR_AXLE, 0);
			add(CarSlot::WHEEL_CYLINDER, 0);
			add(CarSlot::WHEEL_CYLINDER, 1);
		}
	}

	// A part counts as on only once its own nuts and bolts are in: whatever needs it needs them too.
	Positions parts = needs;
	for (const auto& [other, otherIndex] : parts)
	{
		PartType type = SlotPartType(other);
		for (int hole = static_cast<int>(CarSlot::STRUT_MOUNT_NUT); hole < static_cast<int>(CarSlot::COUNT); hole++)
		{
			const auto* holes = Holes(static_cast<CarSlot>(hole));
			if (holes->parent != type)
				continue;
			if (holes->throughAll)
				for (int i = 0; i < SlotCount(static_cast<CarSlot>(hole)); i++)
					add(static_cast<CarSlot>(hole), i);
			else
				for (int i = 0; i < holes->perParent; i++)
					add(static_cast<CarSlot>(hole), otherIndex * holes->perParent + i);
		}
	}
	return needs;
}

const std::vector<std::pair<CarSlot, int>>& CarAssembly::Prerequisites(CarSlot slot, int index) const
{
	return m_prerequisites[static_cast<size_t>(slot)].at(index);
}

bool CarAssembly::IsFilled(CarSlot slot, int index) const
{
	if (slot == CarSlot::ENGINE)
		return m_engine >= 0;
	const auto& positions = m_slots[static_cast<size_t>(slot)];
	return index >= 0 && index < static_cast<int>(positions.size()) && positions[index] != nullptr;
}

bool CarAssembly::IsSlotComplete(CarSlot slot) const
{
	for (int index = 0; index < SlotCount(slot); index++)
		if (!IsFilled(slot, index))
			return false;
	if (IsFastenerSlot(slot))
		return true;
	PartType type = SlotPartType(slot);
	for (int hole = static_cast<int>(CarSlot::STRUT_MOUNT_NUT); hole < static_cast<int>(CarSlot::COUNT); hole++)
		if (Holes(static_cast<CarSlot>(hole))->parent == type && !IsSlotComplete(static_cast<CarSlot>(hole)))
			return false;
	return true;
}

std::optional<std::string> CarAssembly::MissingPrerequisite(CarSlot slot, int index) const
{
	for (const auto& [other, otherIndex] : Prerequisites(slot, index))
		if (!IsFilled(other, otherIndex))
		{
			// A nut or bolt that isn't in yet: say what it holds on, it reads better.
			return PositionName(other, otherIndex);
		}
	return std::nullopt;
}

std::optional<std::string> CarAssembly::RemovalBlockedBy(CarSlot slot, int index) const
{
	for (const auto& [other, otherIndex] : m_dependents[static_cast<size_t>(slot)].at(index))
		if (IsFilled(other, otherIndex))
			return PositionName(other, otherIndex);
	return std::nullopt;
}

bool CarAssembly::Install(CarSlot slot, int index, const pPart& part, std::string& error)
{
	if (slot == CarSlot::ENGINE || slot >= CarSlot::COUNT)
	{
		error = "That isn't a place for a part.";
		return false;
	}
	if (index < 0 || index >= SlotCount(slot))
	{
		error = "There's no such position on the car.";
		return false;
	}
	if (!part || part->GetType() != SlotPartType(slot))
	{
		error = std::format("Only a {} goes there.", PartTypeName(SlotPartType(slot)));
		return false;
	}
	if (Contains(part))
	{
		error = "That part is already on the car.";
		return false;
	}
	auto& position = m_slots[static_cast<size_t>(slot)][index];
	if (position)
	{
		error = std::format("There's already a part on the {}.", PositionName(slot, index));
		return false;
	}
	position = part;
	m_positions[part.get()] = { slot, index };
	OpenCircuitAt(slot, index);
	if (slot == CarSlot::DIFFERENTIAL)
		m_tune.finalDrive = part->GetSizeOffset() > 0.0 ? part->GetSizeOffset() : m_spec->finalDriveNominal;
	return true;
}

pPart CarAssembly::Remove(CarSlot slot, int index)
{
	if (slot == CarSlot::ENGINE || slot >= CarSlot::COUNT || index < 0 || index >= SlotCount(slot))
		return nullptr;
	auto& position = m_slots[static_cast<size_t>(slot)][index];
	pPart part = position;
	if (!part)
		return nullptr;
	OpenCircuitAt(slot, index);
	position = nullptr;
	m_positions.erase(part.get());
	if (slot == CarSlot::DIFFERENTIAL)
		m_tune.finalDrive = 0.0;
	return part;
}

pPart CarAssembly::Get(CarSlot slot, int index) const
{
	if (slot >= CarSlot::COUNT || index < 0 || index >= static_cast<int>(m_slots[static_cast<size_t>(slot)].size()))
		return nullptr;
	return m_slots[static_cast<size_t>(slot)][index];
}

bool CarAssembly::Contains(const pPart& part) const
{
	return part && m_positions.contains(part.get());
}

std::optional<std::pair<CarSlot, int>> CarAssembly::Find(const Part* part) const
{
	if (auto it = m_positions.find(part); it != m_positions.end())
		return it->second;
	return std::nullopt;
}

std::vector<pPart> CarAssembly::GetAllParts() const
{
	std::vector<pPart> parts;
	for (const auto& positions : m_slots)
		for (const auto& part : positions)
			if (part)
				parts.push_back(part);
	return parts;
}

int CarAssembly::GetEngine() const
{
	return m_engine;
}

bool CarAssembly::SetEngine(int engine, std::string& error)
{
	if (m_engine >= 0)
	{
		error = "There's already an engine in the bay.";
		return false;
	}
	if (auto missing = MissingPrerequisite(CarSlot::ENGINE, 0))
	{
		error = std::format("The {} has to be on before the engine goes in.", *missing);
		return false;
	}
	m_engine = engine;
	OpenCircuitAt(CarSlot::ENGINE, 0);
	return true;
}

int CarAssembly::TakeEngine()
{
	int engine = m_engine;
	if (engine >= 0)
		OpenCircuitAt(CarSlot::ENGINE, 0);
	m_engine = -1;
	return engine;
}

bool CarAssembly::IsComplete() const
{
	for (size_t slot = 0; slot < SLOTS; slot++)
		for (int index = 0; index < SlotCount(static_cast<CarSlot>(slot)); index++)
			if (!IsFilled(static_cast<CarSlot>(slot), index))
				return false;
	return true;
}

std::vector<std::string> CarAssembly::MissingParts() const
{
	std::vector<std::string> missing;
	for (size_t slot = 0; slot < SLOTS; slot++)
		for (int index = 0; index < SlotCount(static_cast<CarSlot>(slot)); index++)
			if (!IsFilled(static_cast<CarSlot>(slot), index))
				missing.push_back(PositionName(static_cast<CarSlot>(slot), index));
	return missing;
}

// ---- Fluids ----

const FluidLevel& CarAssembly::GetFluid(FluidCircuit circuit) const
{
	return m_fluids[static_cast<size_t>(circuit)];
}

std::optional<std::string> CarAssembly::CircuitOpen(FluidCircuit circuit) const
{
	for (auto slot : CircuitParts(circuit))
		for (int index = 0; index < SlotCount(slot); index++)
			if (!IsFilled(slot, index))
				return PositionName(slot, index);
	return std::nullopt;
}

double CarAssembly::Fill(FluidCircuit circuit, FluidProduct product, double litres)
{
	if (litres <= 0.0 || CircuitOpen(circuit))
		return 0.0;
	auto& fluid = m_fluids[static_cast<size_t>(circuit)];
	double room = (std::max)(0.0, MaxFill(m_spec->Fluid(circuit)) - fluid.Total());
	double poured = (std::min)(litres, room);
	if (poured > 0.0)
	{
		fluid.litres[product] += poured;
		// Fresh oil or coolant: as good as its share of what's in there.
		if (circuit == FluidCircuit::ENGINE_OIL || circuit == FluidCircuit::COOLANT)
			fluid.hours *= (fluid.Total() - poured) / fluid.Total();
	}
	return poured;
}

double CarAssembly::Drain(FluidCircuit circuit)
{
	auto& fluid = m_fluids[static_cast<size_t>(circuit)];
	double drained = fluid.Total();
	fluid.litres.clear();
	fluid.hours = 0.0;
	if (circuit == FluidCircuit::BRAKES || circuit == FluidCircuit::CLUTCH)
		fluid.air.fill(1.0);
	if (circuit == FluidCircuit::COOLANT)
		fluid.airPocket = 1.0;
	return drained;
}

double CarAssembly::Bleed(BleedPoint point)
{
	auto circuit = point == BleedPoint::CLUTCH ? FluidCircuit::CLUTCH : FluidCircuit::BRAKES;
	auto& fluid = m_fluids[static_cast<size_t>(circuit)];
	auto& air = fluid.air[static_cast<size_t>(point)];
	if (CircuitOpen(circuit) || fluid.Total() <= 0.0)
		return air;

	// Some fluid goes out of the nipple, the same mix as what's in there.
	double total = fluid.Total();
	double out = (std::min)(circuit == FluidCircuit::CLUTCH ? BLEED_FLUID * 0.5 : BLEED_FLUID, total);
	for (auto& [product, amount] : fluid.litres)
		amount -= out * amount / total;
	air *= BLEED_KEEPS;
	if (air < 0.01)
		air = 0.0;

	// Pumped till the reservoir ran dry: the master cylinder draws air, and it's all to do again.
	const auto& spec = m_spec->Fluid(circuit);
	if (fluid.Total() < spec.level.min * 0.6)
	{
		if (circuit == FluidCircuit::CLUTCH)
			fluid.air[static_cast<size_t>(BleedPoint::CLUTCH)] = 1.0;
		else
			for (int brake = 0; brake < static_cast<int>(BleedPoint::CLUTCH); brake++)
				fluid.air[brake] = (std::min)(1.0, fluid.air[brake] + 0.5);
	}
	return air;
}

void CarAssembly::BurpCoolant(double minutes)
{
	auto& fluid = m_fluids[static_cast<size_t>(FluidCircuit::COOLANT)];
	if (CircuitOpen(FluidCircuit::COOLANT) || fluid.Total() <= 0.0)
		return;
	// The air that comes out leaves room that wants topping up.
	constexpr double TRAPPED_LITRES = 0.6;
	double before = fluid.airPocket;
	fluid.airPocket *= std::exp(-minutes / 3.0);
	if (fluid.airPocket < 0.02)
		fluid.airPocket = 0.0;
	double settled = (before - fluid.airPocket) * TRAPPED_LITRES;
	double total = fluid.Total();
	for (auto& [product, amount] : fluid.litres)
		amount -= (std::min)(settled, total) * amount / total;
}

void CarAssembly::Run(double hours, double fuelLitres)
{
	for (auto circuit : { FluidCircuit::ENGINE_OIL, FluidCircuit::COOLANT })
		m_fluids[static_cast<size_t>(circuit)].hours += hours;
	auto& fuel = m_fluids[static_cast<size_t>(FluidCircuit::FUEL)];
	double total = fuel.Total();
	if (total > 0.0)
		for (auto& [product, amount] : fuel.litres)
			amount = (std::max)(0.0, amount - (std::min)(fuelLitres, total) * amount / total);
}

std::vector<BleedPoint> CarAssembly::BleedPointsOf(CarSlot slot, int index)
{
	switch (slot)
	{
		case CarSlot::BRAKE_MASTER_CYLINDER:
		case CarSlot::BRAKE_LINE_SET:
			return { BleedPoint::REAR_RIGHT, BleedPoint::REAR_LEFT, BleedPoint::FRONT_RIGHT, BleedPoint::FRONT_LEFT };
		case CarSlot::BRAKE_HOSE:
			if (index == 0)
				return { BleedPoint::FRONT_LEFT };
			if (index == 1)
				return { BleedPoint::FRONT_RIGHT };
			return { BleedPoint::REAR_LEFT, BleedPoint::REAR_RIGHT };
		case CarSlot::BRAKE_CALIPER:
			return { index == 0 ? BleedPoint::FRONT_LEFT : BleedPoint::FRONT_RIGHT };
		case CarSlot::WHEEL_CYLINDER:
			return { index == 0 ? BleedPoint::REAR_LEFT : BleedPoint::REAR_RIGHT };
		case CarSlot::CLUTCH_MASTER_CYLINDER:
		case CarSlot::CLUTCH_RELEASE_CYLINDER:
			return { BleedPoint::CLUTCH };
		default:
			return {};
	}
}

void CarAssembly::OpenCircuitAt(CarSlot slot, int index)
{
	auto spill = [&](FluidCircuit circuit, double share) {
		auto& fluid = m_fluids[static_cast<size_t>(circuit)];
		for (auto& [product, amount] : fluid.litres)
			amount *= 1.0 - share;
	};

	// Brakes and clutch: a line opened lets air in where it feeds, and some fluid runs out.
	if (auto points = BleedPointsOf(slot, index); !points.empty())
	{
		auto circuit = points.front() == BleedPoint::CLUTCH ? FluidCircuit::CLUTCH : FluidCircuit::BRAKES;
		auto& fluid = m_fluids[static_cast<size_t>(circuit)];
		for (auto point : points)
			fluid.air[static_cast<size_t>(point)] = 1.0;
		bool master = slot == CarSlot::BRAKE_MASTER_CYLINDER || slot == CarSlot::CLUTCH_MASTER_CYLINDER;
		spill(circuit, master ? 0.9 : 0.15);
		return;
	}
	switch (slot)
	{
		case CarSlot::ENGINE:
			// In or out, the engine goes with its oil drained and the cooling system open.
			Drain(FluidCircuit::ENGINE_OIL);
			Drain(FluidCircuit::COOLANT);
			break;
		case CarSlot::RADIATOR:
		case CarSlot::RADIATOR_HOSE:
			Drain(FluidCircuit::COOLANT);
			break;
		case CarSlot::TRANSMISSION:
			Drain(FluidCircuit::TRANSMISSION);
			break;
		case CarSlot::REAR_AXLE:
		case CarSlot::DIFFERENTIAL:
			Drain(FluidCircuit::DIFFERENTIAL);
			break;
		case CarSlot::FUEL_TANK:
		case CarSlot::FUEL_SENDER:
			Drain(FluidCircuit::FUEL);
			break;
		default:
			break;
	}
}

CarTune& CarAssembly::GetTune()
{
	return m_tune;
}

const CarTune& CarAssembly::GetTune() const
{
	return m_tune;
}
