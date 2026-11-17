#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CarFluids.h"
#include "CarSpec.h"
#include "Part.h"
#include "PartCatalog.h"

// Where each part goes on the car. From BODY_SHELL to TIRE each slot takes the PartType of the same
// name (FIRST_CAR_PART + the slot's number); ENGINE is the engine from one of the shop's stands, lowered
// in on the hoist; after it, one slot per set of the car's holes (CarAssembly::Holes), like the engine's.
enum class CarSlot
{
	BODY_SHELL,
	FRONT_FENDER,		// 0 left, 1 right (as you sit in it)
	DOOR,
	HOOD,
	TRUNK_LID,
	FRONT_BUMPER,
	REAR_BUMPER,
	HEADLIGHT,
	TAIL_LIGHT,
	WINDSHIELD,
	REAR_WINDOW,
	DASHBOARD,
	INSTRUMENT_CLUSTER,
	SEAT,
	STEERING_WHEEL,
	FRONT_STRUT,
	FRONT_COIL_SPRING,	// carried by its strut: on with it, compressed onto it off the car
	STEERING_KNUCKLE,
	LOWER_CONTROL_ARM,
	STRUT_ROD,
	FRONT_SWAY_BAR,
	REAR_AXLE,
	DIFFERENTIAL,
	LOWER_TRAILING_LINK,
	UPPER_TRAILING_LINK,
	LATERAL_ROD,
	REAR_COIL_SPRING,
	REAR_SHOCK,
	STEERING_RACK,
	TIE_ROD_END,
	STEERING_COLUMN,
	BRAKE_BOOSTER,
	BRAKE_MASTER_CYLINDER,
	BRAKE_LINE_SET,
	BRAKE_HOSE,			// 0 front left, 1 front right, 2 the rear axle's
	BRAKE_ROTOR,
	BRAKE_PAD_SET,
	BRAKE_CALIPER,
	WHEEL_CYLINDER,		// 0 rear left, 1 rear right
	BRAKE_SHOE_SET,
	BRAKE_DRUM,
	PARKING_BRAKE_CABLE,
	ENGINE_MOUNT,
	CLUTCH_DISC,
	PRESSURE_PLATE,
	RELEASE_BEARING,
	TRANSMISSION,
	TRANSMISSION_MOUNT,
	CLUTCH_MASTER_CYLINDER,
	CLUTCH_RELEASE_CYLINDER,
	DRIVESHAFT,
	FUEL_TANK,
	FUEL_SENDER,
	FUEL_LINE_SET,
	FUEL_FILTER,
	BATTERY,
	MAIN_HARNESS,
	FUSE_BOX,
	IGNITION_COIL,
	STARTER,
	RADIATOR,
	RADIATOR_HOSE,		// 0 upper, 1 lower
	COOLING_FAN,
	CATALYTIC_CONVERTER,
	MUFFLER,
	WHEEL,				// 0 front left, 1 front right, 2 rear left, 3 rear right
	TIRE,				// carried by its wheel: mounted on it off the car
	// The engine from a stand. Not a part: CarAssembly::GetEngine says which.
	ENGINE,
	// Nuts and bolts: one slot per set of holes (CarAssembly::Holes), taking that set's size.
	STRUT_MOUNT_NUT,
	STRUT_KNUCKLE_BOLT,
	BALL_JOINT_BOLT,
	CONTROL_ARM_BOLT,
	STRUT_ROD_NUT,
	SWAY_BAR_BOLT,
	TIE_ROD_END_NUT,
	STEERING_RACK_BOLT,
	CALIPER_BOLT,
	REAR_LINK_BOLT,
	UPPER_LINK_BOLT,
	LATERAL_ROD_BOLT,
	REAR_SHOCK_BOLT,
	DIFFERENTIAL_NUT,
	ENGINE_MOUNT_BOLT,
	ENGINE_MOUNT_NUT,
	PRESSURE_PLATE_BOLT,
	BELLHOUSING_BOLT,
	TRANSMISSION_MOUNT_BOLT,
	DRIVESHAFT_FLANGE_NUT,
	STARTER_BOLT,
	MASTER_CYLINDER_NUT,
	BOOSTER_NUT,
	FUEL_TANK_BOLT,
	SEAT_BOLT,
	FENDER_BOLT,
	DOOR_HINGE_BOLT,
	HOOD_HINGE_BOLT,
	BUMPER_BOLT,
	CATALYTIC_CONVERTER_NUT,
	LUG_NUT,
	COUNT
};
constexpr int CAR_PART_SLOTS = static_cast<int>(CarSlot::ENGINE);
constexpr int CAR_FASTENER_SLOTS = static_cast<int>(CarSlot::COUNT) - static_cast<int>(CarSlot::STRUT_MOUNT_NUT);
static_assert(static_cast<int>(PartType::TIRE) - static_cast<int>(FIRST_CAR_PART) == static_cast<int>(CarSlot::TIRE),
	"The car's part slots must line up with their part types.");

// The plugs on the car's harness, each of which has to be pushed onto what it feeds by hand: the battery, the two
// headlights and the cooling fan at the front, and the starter, the coil, the distributor, the alternator and the two
// senders on the engine (the coolant's temperature, the oil's pressure). Last, and not the harness's, is the battery's
// own ground strap: from its negative post to a stud on the body. Till that's on, nothing electrical on the car works.
//
// The car's pipes are done up the same way, each a union on the end of a pipe: the brake line set's five (the master
// cylinder's two ports, front and rear, and the three flexible hoses), the clutch's one (the pipe off its master
// cylinder, onto the release cylinder), and the fuel line set's three (the tank, the filter, and the hose on from the
// filter to the engine's pump). A brake or clutch union undone lets air in and fluid out; with a fuel one off the
// engine gets none.
enum class CarPlug { BATTERY, HEADLIGHT_LEFT, HEADLIGHT_RIGHT, COOLING_FAN, STARTER, IGNITION_COIL, DISTRIBUTOR, ALTERNATOR, COOLANT_SENDER, OIL_SENDER,
	GROUND,
	BRAKE_FRONT_PORT, BRAKE_REAR_PORT, BRAKE_HOSE_LEFT, BRAKE_HOSE_RIGHT, BRAKE_HOSE_REAR, CLUTCH_PIPE, FUEL_TANK, FUEL_FILTER, FUEL_PUMP,
	COUNT };
// What a plug is: a wire's connector, the battery's strap, a hard pipe's flare nut, or a rubber hose's end.
enum class CarPlugKind { WIRE, STRAP, PIPE, HOSE };
inline CarPlugKind CarPlugKindOf(CarPlug plug)
{
	return plug == CarPlug::GROUND ? CarPlugKind::STRAP : plug >= CarPlug::FUEL_TANK && plug <= CarPlug::FUEL_PUMP ? CarPlugKind::HOSE
		: plug >= CarPlug::BRAKE_FRONT_PORT && plug <= CarPlug::CLUTCH_PIPE ? CarPlugKind::PIPE : CarPlugKind::WIRE;
}
// Those that go onto something on the engine itself, not a part of the car.
inline bool CarPlugOnEngine(CarPlug plug)
{
	return (plug >= CarPlug::DISTRIBUTOR && plug <= CarPlug::OIL_SENDER) || plug == CarPlug::FUEL_PUMP;
}
constexpr int CAR_PLUGS = static_cast<int>(CarPlug::COUNT);
constexpr uint32_t ALL_CAR_PLUGS = (1u << CAR_PLUGS) - 1u;
// What a plug is called by what it goes onto ("distributor"), and whether it's on the harness's engine branch.
const char* CarPlugName(CarPlug plug);
// All but the ground strap are the main harness's: the battery's lead, the lights and fan at the front, and the
// branch that comes through the firewall to the engine (the starter, the coil, the distributor, the alternator and
// the two senders).
inline bool OnEngineBranch(CarPlug plug) { return plug >= CarPlug::STARTER && plug <= CarPlug::OIL_SENDER; }
// A plug's wire can be clipped to the car along its way, to lead it tidily round things: up to this many clips on
// each, in order from the loom, each a point in the car's space (+x its nose, +y up, +z its right).
constexpr int CAR_WIRE_CLIPS = 6;
using CarWirePoint = std::array<float, 3>;

// How a front seat is set: slid along its rails (metres, forward positive), its back leant back (radians), and its
// back tipped forward (to let someone into the back) or not.
struct CarSeatSetting
{
	float slide = 0.f;
	float recline = 0.26f;
	bool folded = false;
	bool operator==(const CarSeatSetting&) const = default;
};

// The four corners, for parts that come in pairs or fours.
enum class Corner { FRONT_LEFT, FRONT_RIGHT, REAR_LEFT, REAR_RIGHT };
const char* CornerName(Corner corner);

const char* CarSlotName(CarSlot slot);

// How a car is set up: what the player has adjusted, engine and chassis. The parts set what the car
// could do; this is how close it gets.
struct CarTune
{
	// Engine
	double ignitionTiming = 5.0;		// degrees BTDC at idle: turn the distributor body, read with a timing light
	double idleSpeed = 800.0;			// rpm: the carburettor's idle speed screw
	double idleMixture = 2.5;			// turns out: the mixture screw
	double mainJet = 105.0;				// the primary main jet's number
	double intakeLash = 0.20;			// mm, hot: each rocker's adjusting screw
	double exhaustLash = 0.30;
	// Chassis
	double frontCamber = -0.5;			// degrees: set by the parts on a stock car
	double caster = 2.75;				// degrees: the strut rods' nuts
	double frontToe = 1.0;				// mm in: the tie rod ends
	std::array<double, 4> tirePressure = { 180.0, 180.0, 180.0, 180.0 };	// kPa, by Corner
	double finalDrive = 0.0;			// the fitted differential's ratio (0: none fitted)

	static CarTune Factory(const CarSpec& spec);
};

// The car on the shop floor: the parts bolted to it, the engine in it, its fluids and how it's set up.
// Like EngineAssembly this holds the true state; the player only knows what they've checked.
class CarAssembly
{
public:
	explicit CarAssembly(const CarSpec& spec);

	const CarSpec& GetSpec() const;

	static PartType SlotPartType(CarSlot slot);
	// The slot a part type goes in on the car (BLOCK for the engine's own mounting points), or COUNT.
	static CarSlot SlotFor(PartType type);
	static bool IsFastenerSlot(CarSlot slot);
	// Every set of the car's holes, in CarSlot order from STRUT_MOUNT_NUT.
	static const std::vector<FastenerHoles>& Holes();
	static const FastenerHoles* Holes(CarSlot slot);
	// Put together off the car and in with its host: a front spring on its strut, a tire on its wheel.
	static std::optional<CarSlot> CarriedBy(CarSlot slot);
	int SlotCount(CarSlot slot) const;
	// The corner a position is at, if it's at one.
	static std::optional<Corner> CornerOf(CarSlot slot, int index);
	// "Front Left Brake Caliper", "Rear Right Wheel", "Upper Radiator Hose", "Lug Nut 3 (Front Left Wheel)".
	std::string PositionName(CarSlot slot, int index) const;

	bool Install(CarSlot slot, int index, const pPart& part, std::string& error);
	pPart Remove(CarSlot slot, int index);
	pPart Get(CarSlot slot, int index) const;
	bool Contains(const pPart& part) const;
	std::optional<std::pair<CarSlot, int>> Find(const Part* part) const;
	std::vector<pPart> GetAllParts() const;
	bool IsFilled(CarSlot slot, int index) const;
	// Every position of a slot is filled (and, for a part, its own bolts are in).
	bool IsSlotComplete(CarSlot slot) const;

	// The engine in the bay: which of the shop's engines (Workshop's numbering), or -1.
	int GetEngine() const;
	bool SetEngine(int engine, std::string& error);
	int TakeEngine();

	// The real build order, like the engine's: Install doesn't enforce it, the workshop checks it.
	const std::vector<std::pair<CarSlot, int>>& Prerequisites(CarSlot slot, int index) const;
	std::optional<std::string> MissingPrerequisite(CarSlot slot, int index) const;
	std::optional<std::string> RemovalBlockedBy(CarSlot slot, int index) const;

	bool IsComplete() const;
	std::vector<std::string> MissingParts() const;

	// Fluids. A circuit holds fluid once its parts are on and closed up (see CircuitSealed); poured into
	// one that isn't, it runs straight out onto the floor.
	const FluidLevel& GetFluid(FluidCircuit circuit) const;
	// Why the circuit can't hold fluid yet: the part that's missing, or nothing.
	std::optional<std::string> CircuitOpen(FluidCircuit circuit) const;
	// Pours some in; returns what went in (the rest overflowed or ran out).
	double Fill(FluidCircuit circuit, FluidProduct product, double litres);
	// Drains it all out (into the drain pan); returns how much came out.
	double Drain(FluidCircuit circuit);
	// One go at a bleed point: the pedal pumped and the nipple cracked. Lets some air out of that line
	// (and a little fluid), as long as the reservoir doesn't run dry. Returns the air left in that line.
	double Bleed(BleedPoint point);
	// Running the engine with the radiator cap off till it's warm: burps the coolant's air out.
	void BurpCoolant(double minutes);
	// Fluid that's been used up running it: fuel burned, oil and coolant aged.
	void Run(double hours, double fuelLitres);
	// Puts the fluids back as they were saved, once the parts are on.
	void RestoreFluids(const std::array<FluidLevel, FLUID_CIRCUITS>& fluids) { m_fluids = fluids; }

	CarTune& GetTune();
	const CarTune& GetTune() const;

	// The looms' plugs. The loom a plug is on, and the part of the car it goes onto (nothing for those that go
	// onto the engine: the workshop knows what's on that). One is only ever on while its loom is fitted: taking the
	// loom off, or what the plug is on, pulls it.
	static CarSlot PlugHarness(CarPlug plug)
	{
		if (plug >= CarPlug::BRAKE_FRONT_PORT && plug <= CarPlug::BRAKE_HOSE_REAR)
			return CarSlot::BRAKE_LINE_SET;
		if (plug == CarPlug::CLUTCH_PIPE)
			return CarSlot::CLUTCH_MASTER_CYLINDER;
		if (plug >= CarPlug::FUEL_TANK && plug <= CarPlug::FUEL_PUMP)
			return CarSlot::FUEL_LINE_SET;
		return plug == CarPlug::GROUND ? CarSlot::BATTERY : CarSlot::MAIN_HARNESS;
	}
	// Whether a part's plugs are picked from a list at it (a loom or a line set, with several), not just found by it.
	static bool HasPlugMenu(CarSlot harness)
	{
		return harness == CarSlot::MAIN_HARNESS || harness == CarSlot::BRAKE_LINE_SET || harness == CarSlot::FUEL_LINE_SET;
	}
	static std::optional<std::pair<CarSlot, int>> PlugTarget(CarPlug plug);
	bool IsPlugged(CarPlug plug) const { return (m_plugs & (1u << static_cast<int>(plug))) != 0; }
	void SetPlugged(CarPlug plug, bool plugged);
	uint32_t GetPlugs() const { return m_plugs; }
	void RestorePlugs(uint32_t plugs) { m_plugs = plugs & ALL_CAR_PLUGS; }
	// The first of a circuit's unions that isn't done up, by what it goes onto.
	std::optional<CarPlug> OpenUnion(FluidCircuit circuit) const;
	// The clips a plug's wire is led through (they go with its loom: taking the loom off takes them too).
	const std::vector<CarWirePoint>& GetWireClips(CarPlug plug) const { return m_wireClips[static_cast<int>(plug)]; }
	bool AddWireClip(CarPlug plug, const CarWirePoint& at);
	void RemoveWireClip(CarPlug plug, int index);
	// The two front seats' settings (0 the driver's, 1 the passenger's).
	const CarSeatSetting& GetSeatSetting(int seat) const { return m_seatSettings[seat == 1 ? 1 : 0]; }
	void SetSeatSetting(int seat, const CarSeatSetting& setting) { m_seatSettings[seat == 1 ? 1 : 0] = setting; }
	// Where on the body the battery's ground strap is bolted (when CarPlug::GROUND is on), in the car's space: anywhere
	// on the shell its strap reaches.
	static constexpr CarWirePoint DEFAULT_GROUND_POINT = { 1.42f, 0.65f, -0.462f };	// on the wheel housing, behind the battery
	const CarWirePoint& GetGroundPoint() const { return m_groundPoint; }
	void SetGroundPoint(const CarWirePoint& at) { m_groundPoint = at; }
	// And which way the panel it's bolted to faces there (a unit vector out of it): its stud stands out along this.
	static constexpr CarWirePoint DEFAULT_GROUND_NORMAL = { 0.552f, 0.834f, 0.f };
	const CarWirePoint& GetGroundNormal() const { return m_groundNormal; }
	void SetGroundNormal(const CarWirePoint& normal) { m_groundNormal = normal; }
private:
	using Positions = std::vector<std::pair<CarSlot, int>>;
	static constexpr size_t SLOTS = static_cast<size_t>(CarSlot::COUNT);

	Positions ComputePrerequisites(CarSlot slot, int index) const;
	// Whatever's opened up when a part of a circuit goes on or comes off: its fluid spills and air gets in.
	void OpenCircuitAt(CarSlot slot, int index);
	// The bleed points a brake or clutch part feeds.
	static std::vector<BleedPoint> BleedPointsOf(CarSlot slot, int index);

	const CarSpec*								m_spec = nullptr;
	std::array<std::vector<pPart>, SLOTS>		m_slots = {};
	std::array<std::vector<Positions>, SLOTS>	m_prerequisites = {};
	std::array<std::vector<Positions>, SLOTS>	m_dependents = {};
	std::unordered_map<const Part*, std::pair<CarSlot, int>> m_positions = {};
	int											m_engine = -1;
	std::array<FluidLevel, FLUID_CIRCUITS>		m_fluids = {};
	CarTune										m_tune = {};
	uint32_t									m_plugs = 0;	// a bit for each CarPlug that's pushed home
	std::array<std::vector<CarWirePoint>, CAR_PLUGS> m_wireClips = {};
	std::array<CarSeatSetting, 2>				m_seatSettings = {};
	CarWirePoint								m_groundNormal = DEFAULT_GROUND_NORMAL;
	CarWirePoint								m_groundPoint = DEFAULT_GROUND_POINT;
};
