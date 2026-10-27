#pragma once

#include <array>
#include <optional>
#include <string>
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
	COUNT
};

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
	std::vector<std::pair<AssemblySlot, int>> Prerequisites(AssemblySlot slot, int index) const;
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

	const EngineSpec*									m_spec = nullptr;
	std::array<std::vector<pPart>, static_cast<size_t>(AssemblySlot::COUNT)> m_slots = {};
};
