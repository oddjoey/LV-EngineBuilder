#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "EngineSpec.h"
#include "Part.h"

enum class AssemblySlot
{
	BLOCK,
	CRANKSHAFT,
	CYLINDER_HEAD,
	HEAD_GASKET,
	MAIN_BEARING,	// one per main journal
	PISTON,			// one per cylinder from here down
	PISTON_RING,
	CONNECTING_ROD,
	ROD_BEARING,
	CAMSHAFT,
	INTAKE_VALVE,	// one per cylinder
	EXHAUST_VALVE,	// one per cylinder
	VALVE_SPRING,	// two per cylinder, see EngineAssembly::SpringIndex

	// From here to MAIN_CAP each slot takes the PartType of the same number, and per-valve slots are
	// indexed like the springs. See PartCatalog for what each one is.
	CORE_PLUG,
	REAR_MAIN_SEAL,
	OIL_PUMP,
	OIL_PICKUP,
	OIL_PAN_GASKET,
	OIL_PAN,
	OIL_DRAIN_PLUG,
	OIL_FILTER,
	VALVE_STEM_SEAL,
	VALVE_RETAINER,
	LIFTER,
	CRANK_SPROCKET,
	CAM_SPROCKET,
	TIMING_BELT,
	BELT_IDLER,
	BELT_TENSIONER,
	TIMING_COVER_GASKET,
	TIMING_COVER,
	FRONT_CRANK_SEAL,
	CRANK_PULLEY,
	VALVE_COVER_GASKET,
	VALVE_COVER,
	SPARK_PLUG,
	IGNITION_COIL,
	INTAKE_MANIFOLD_GASKET,
	INTAKE_MANIFOLD,
	THROTTLE_BODY,
	FUEL_INJECTOR,
	FUEL_RAIL,
	EXHAUST_MANIFOLD_GASKET,
	EXHAUST_MANIFOLD,
	WATER_PUMP_GASKET,
	WATER_PUMP,
	THERMOSTAT,
	THERMOSTAT_HOUSING,
	MAIN_BEARING_LOWER,	// MAIN_BEARING is the upper shell, in the block
	ROD_BEARING_LOWER,	// ROD_BEARING is the upper shell, in the rod
	ROD_CAP,
	MAIN_CAP,
	CAM_CAP,		// added later: its PartType is at the end of that list, not this number
	// Nuts and bolts: one slot per set of holes (PartCatalog::Holes), taking that set's size. A
	// hole's index / perParent is the index of the part it holds on.
	HEAD_BOLT,
	MAIN_CAP_BOLT,
	ROD_NUT,
	OIL_PAN_BOLT,
	VALVE_COVER_BOLT,
	TIMING_COVER_BOLT,
	INTAKE_MANIFOLD_NUT,
	EXHAUST_MANIFOLD_NUT,
	WATER_PUMP_BOLT,
	THERMOSTAT_HOUSING_BOLT,
	OIL_PUMP_BOLT,
	OIL_PICKUP_BOLT,
	CRANK_PULLEY_BOLT,
	CAM_SPROCKET_BOLT,
	THROTTLE_BODY_BOLT,
	FUEL_RAIL_BOLT,
	CAM_CAP_BOLT,
	COUNT
};
static_assert(static_cast<int>(AssemblySlot::CORE_PLUG) == static_cast<int>(PartType::CORE_PLUG)
	&& static_cast<int>(AssemblySlot::MAIN_CAP) == static_cast<int>(PartType::MAIN_CAP), "Ancillary slots must line up with their part types.");
constexpr int FASTENER_SLOTS = static_cast<int>(AssemblySlot::COUNT) - static_cast<int>(AssemblySlot::HEAD_BOLT);

struct FastenerHoles;

const char* AssemblySlotName(AssemblySlot slot);

// An engine on the stand. Clearances are computed from the parts' true geometry: this is what
// the engine will actually run with, whether or not the builder measured it.
class EngineAssembly
{
public:
	explicit EngineAssembly(const EngineSpec& spec);
	~EngineAssembly();

	const EngineSpec& GetSpec() const;

	static PartType SlotPartType(AssemblySlot slot);
	// The slot a part type goes in (the first one, for a nut or bolt that fits several).
	static AssemblySlot SlotFor(PartType type);
	// The set of holes a fastener slot is, or null.
	static const FastenerHoles* Holes(AssemblySlot slot);
	int SlotCount(AssemblySlot slot) const;

	bool Install(AssemblySlot slot, int index, const pPart& part, std::string& error);
	pPart Remove(AssemblySlot slot, int index);
	pPart Get(AssemblySlot slot, int index) const;
	bool Contains(const pPart& part) const;
	std::vector<pPart> GetAllParts() const;
	void Clear();

	bool IsComplete() const;
	std::vector<std::string> MissingParts() const;

	// The real build order: main bearings before the crank, the bottom end before the head gasket,
	// the head before its valves, springs and cam. Install itself doesn't enforce it (a saved
	// engine is restored as it was); the workshop checks it when the builder fits or removes a part.
	const std::vector<std::pair<AssemblySlot, int>>& Prerequisites(AssemblySlot slot, int index) const;
	// Where a part is fitted, if it is (a lookup, not a search).
	std::optional<std::pair<AssemblySlot, int>> Find(const Part* part) const;
	// "Main Bearing 3", "Exhaust Valve Spring 2", "Crankshaft".
	std::string PositionName(AssemblySlot slot, int index) const;
	// What has to be fitted before this position can be, or nothing if it's ready.
	std::optional<std::string> MissingPrerequisite(AssemblySlot slot, int index) const;
	// What has to come off before the part in this position can, or nothing if it's free.
	std::optional<std::string> RemovalBlockedBy(AssemblySlot slot, int index) const;

	std::optional<double> MainClearance(int journal) const;
	std::optional<double> RodClearance(int cylinder) const;
	std::optional<double> PistonToWall(int cylinder) const;
	std::optional<double> RingGap(int cylinder) const;
	std::optional<double> DeckClearance(int cylinder) const;	// positive = piston below deck at TDC
	std::optional<double> Quench(int cylinder) const;			// piston crown to head
	std::optional<double> SweptVolume(int cylinder) const;		// cc
	std::optional<double> CompressionRatio(int cylinder) const;
	double Displacement() const;

	static int SpringIndex(int cylinder, bool exhaust) { return cylinder * 2 + (exhaust ? 1 : 0); }
	std::optional<double> CamClearance(int journal) const;
	std::optional<double> StemClearance(int cylinder, bool exhaust) const;
	// Spring seat to retainer with the valve shut: grows as seats and valve faces are cut.
	std::optional<double> InstalledHeight(int cylinder, bool exhaust) const;
	// Spring force holding the valve shut at its actual installed height.
	std::optional<double> SeatPressure(int cylinder, bool exhaust) const;

	// Shared with the UI so the builder can work out a ratio from their own measurements.
	static std::optional<double> CompressionRatioFrom(double bore, double stroke, double chamberVolume, double dishVolume,
		double gasketBore, double gasketThickness, double deckClearance);
private:
	const Feature* FindFeature(AssemblySlot slot, int index, const std::string& featureName) const;
	std::vector<std::pair<AssemblySlot, int>> ComputePrerequisites(AssemblySlot slot, int index) const;

	using Positions = std::vector<std::pair<AssemblySlot, int>>;
	static constexpr size_t SLOTS = static_cast<size_t>(AssemblySlot::COUNT);

	const EngineSpec*									m_spec = nullptr;
	std::array<std::vector<pPart>, SLOTS>				m_slots = {};
	std::array<std::vector<Positions>, SLOTS>			m_prerequisites = {};	// per position: what has to be fitted first
	std::array<std::vector<Positions>, SLOTS>			m_dependents = {};		// per position: what's fitted on top of it
	std::unordered_map<const Part*, std::pair<AssemblySlot, int>> m_positions = {};
};
