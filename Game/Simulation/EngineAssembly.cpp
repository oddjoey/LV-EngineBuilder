#include "EngineAssembly.h"

#include <format>
#include <numbers>
#include <stdexcept>

#include "PartCatalog.h"

const char* AssemblySlotName(AssemblySlot slot)
{
	switch (slot)
	{
		case AssemblySlot::BLOCK:			return "Block";
		case AssemblySlot::CRANKSHAFT:		return "Crankshaft";
		case AssemblySlot::CYLINDER_HEAD:	return "Cylinder Head";
		case AssemblySlot::HEAD_GASKET:		return "Head Gasket";
		case AssemblySlot::MAIN_BEARING:	return "Main Bearing Upper";
		case AssemblySlot::PISTON:			return "Piston";
		case AssemblySlot::PISTON_RING:		return "Top Ring";
		case AssemblySlot::CONNECTING_ROD:	return "Connecting Rod";
		case AssemblySlot::ROD_BEARING:		return "Rod Bearing Upper";
		case AssemblySlot::CAMSHAFT:		return "Camshaft";
		case AssemblySlot::INTAKE_VALVE:	return "Intake Valve";
		case AssemblySlot::EXHAUST_VALVE:	return "Exhaust Valve";
		case AssemblySlot::VALVE_SPRING:	return "Valve Spring";
		case AssemblySlot::COUNT:			return "Unknown";
		default:
			if (const auto* holes = EngineAssembly::Holes(slot))
				return holes->name;
			return PartTypeName(EngineAssembly::SlotPartType(slot));
	}
}

EngineAssembly::EngineAssembly(const EngineSpec& spec) : m_spec(&spec)
{
	for (size_t slot = 0; slot < m_slots.size(); slot++)
		m_slots[slot].resize(SlotCount(static_cast<AssemblySlot>(slot)));

	// The build order never changes, so it's worked out once, both ways round: what each position
	// needs, and what needs it.
	for (size_t slot = 0; slot < m_slots.size(); slot++)
	{
		m_prerequisites[slot].resize(m_slots[slot].size());
		m_dependents[slot].resize(m_slots[slot].size());
	}
	for (size_t slot = 0; slot < m_slots.size(); slot++)
		for (int index = 0; index < static_cast<int>(m_slots[slot].size()); index++)
		{
			auto position = static_cast<AssemblySlot>(slot);
			m_prerequisites[slot][index] = ComputePrerequisites(position, index);
			for (const auto& [other, otherIndex] : m_prerequisites[slot][index])
				m_dependents[static_cast<size_t>(other)][otherIndex].push_back({ position, index });
		}
}

const std::vector<std::pair<AssemblySlot, int>>& EngineAssembly::Prerequisites(AssemblySlot slot, int index) const
{
	return m_prerequisites[static_cast<size_t>(slot)][index];
}

std::optional<std::pair<AssemblySlot, int>> EngineAssembly::Find(const Part* part) const
{
	auto it = m_positions.find(part);
	if (it == m_positions.end())
		return std::nullopt;
	return it->second;
}

EngineAssembly::~EngineAssembly()
{
}

const EngineSpec& EngineAssembly::GetSpec() const
{
	return *m_spec;
}

PartType EngineAssembly::SlotPartType(AssemblySlot slot)
{
	switch (slot)
	{
		case AssemblySlot::BLOCK:			return PartType::BLOCK;
		case AssemblySlot::CRANKSHAFT:		return PartType::CRANKSHAFT;
		case AssemblySlot::CYLINDER_HEAD:	return PartType::CYLINDER_HEAD;
		case AssemblySlot::HEAD_GASKET:		return PartType::HEAD_GASKET;
		case AssemblySlot::MAIN_BEARING:	return PartType::MAIN_BEARING;
		case AssemblySlot::PISTON:			return PartType::PISTON;
		case AssemblySlot::PISTON_RING:		return PartType::PISTON_RING;
		case AssemblySlot::CONNECTING_ROD:	return PartType::CONNECTING_ROD;
		case AssemblySlot::ROD_BEARING:		return PartType::ROD_BEARING;
		case AssemblySlot::CAMSHAFT:		return PartType::CAMSHAFT;
		case AssemblySlot::INTAKE_VALVE:	return PartType::INTAKE_VALVE;
		case AssemblySlot::EXHAUST_VALVE:	return PartType::EXHAUST_VALVE;
		case AssemblySlot::VALVE_SPRING:	return PartType::VALVE_SPRING;
		case AssemblySlot::CAM_CAP:		return PartType::CAM_CAP;
		case AssemblySlot::COUNT:			return PartType::BLOCK;
		default:
			if (const auto* holes = Holes(slot))
				return holes->size;
			return static_cast<PartType>(slot);
	}
}

AssemblySlot EngineAssembly::SlotFor(PartType type)
{
	for (int slot = 0; slot < static_cast<int>(AssemblySlot::COUNT); slot++)
		if (SlotPartType(static_cast<AssemblySlot>(slot)) == type)
			return static_cast<AssemblySlot>(slot);
	return AssemblySlot::COUNT;
}

const FastenerHoles* EngineAssembly::Holes(AssemblySlot slot)
{
	int hole = static_cast<int>(slot) - static_cast<int>(AssemblySlot::HEAD_BOLT);
	if (hole < 0 || hole >= FASTENER_SLOTS)
		return nullptr;
	const auto& holes = PartCatalog::Holes();
	if (holes.size() != static_cast<size_t>(FASTENER_SLOTS))
		throw std::runtime_error("Every fastener slot needs a set of holes in the part catalog!");
	return &holes[hole];
}

int EngineAssembly::SlotCount(AssemblySlot slot) const
{
	switch (slot)
	{
		case AssemblySlot::MAIN_BEARING:	return m_spec->mainJournals;
		case AssemblySlot::PISTON:
		case AssemblySlot::PISTON_RING:
		case AssemblySlot::CONNECTING_ROD:
		case AssemblySlot::ROD_BEARING:
		case AssemblySlot::INTAKE_VALVE:
		case AssemblySlot::EXHAUST_VALVE:	return m_spec->cylinders;
		case AssemblySlot::VALVE_SPRING:	return m_spec->cylinders * 2;
		case AssemblySlot::CAM_CAP:		return m_spec->camJournals;
		case AssemblySlot::COUNT:			return 0;
		case AssemblySlot::BLOCK:
		case AssemblySlot::CRANKSHAFT:
		case AssemblySlot::CYLINDER_HEAD:
		case AssemblySlot::HEAD_GASKET:
		case AssemblySlot::CAMSHAFT:		return 1;
		default:
			if (const auto* holes = Holes(slot))
				return holes->perParent * SlotCount(SlotFor(holes->parent));
			return PartCatalog::CountPerEngine(SlotPartType(slot), *m_spec);
	}
}

bool EngineAssembly::Install(AssemblySlot slot, int index, const pPart& part, std::string& error)
{
	if (index < 0 || index >= SlotCount(slot))
	{
		error = "No such position.";
		return false;
	}
	if (!part)
	{
		error = "No part selected.";
		return false;
	}
	if (part->GetType() != SlotPartType(slot))
	{
		error = std::format("A {} doesn't go there.", PartTypeName(part->GetType()));
		return false;
	}
	if (part->GetCondition() == PartCondition::DAMAGED)
	{
		error = std::format("{} is damaged: {}", part->GetName(), part->GetDamageReason());
		return false;
	}
	if (Contains(part))
	{
		error = "That part is already installed.";
		return false;
	}
	// Caps only go back on what they were machined with.
	if (slot == AssemblySlot::ROD_CAP)
	{
		auto rod = Get(AssemblySlot::CONNECTING_ROD, index);
		if (rod && part->GetMatchedTo() != rod->GetId())
		{
			error = std::format("That cap belongs to rod #{}, not the rod in cylinder {} (#{}).", part->GetMatchedTo(), index + 1, rod->GetId());
			return false;
		}
	}
	if (slot == AssemblySlot::MAIN_CAP)
	{
		auto block = Get(AssemblySlot::BLOCK, 0);
		if (block && part->GetMatchedTo() != block->GetId())
		{
			error = std::format("That main cap was line-bored with block #{}, not this one (#{}).", part->GetMatchedTo(), block->GetId());
			return false;
		}
		if (part->GetMatchedPosition() != index)
		{
			error = std::format("That's main cap {}: it only fits main journal {}.", part->GetMatchedPosition() + 1, part->GetMatchedPosition() + 1);
			return false;
		}
	}
	if (slot == AssemblySlot::CAM_CAP)
	{
		auto head = Get(AssemblySlot::CYLINDER_HEAD, 0);
		if (head && part->GetMatchedTo() != head->GetId())
		{
			error = std::format("That cam cap was line-bored with head #{}, not this one (#{}).", part->GetMatchedTo(), head->GetId());
			return false;
		}
		if (part->GetMatchedPosition() != index)
		{
			error = std::format("That's cam cap {}: it only fits cam journal {}.", part->GetMatchedPosition() + 1, part->GetMatchedPosition() + 1);
			return false;
		}
	}

	if (auto previous = m_slots[static_cast<size_t>(slot)][index])
		m_positions.erase(previous.get());
	m_slots[static_cast<size_t>(slot)][index] = part;
	m_positions[part.get()] = { slot, index };
	return true;
}

std::vector<std::pair<AssemblySlot, int>> EngineAssembly::ComputePrerequisites(AssemblySlot slot, int index) const
{
	std::vector<std::pair<AssemblySlot, int>> needed;
	auto all = [&](AssemblySlot other) {
		for (int i = 0; i < SlotCount(other); i++)
			needed.push_back({ other, i });
	};

	auto one = [&](AssemblySlot other) { needed.push_back({ other, 0 }); };
	using A = AssemblySlot;

	// A nut or bolt goes in once the part it holds is on.
	if (const auto* holes = Holes(slot))
		return { { SlotFor(holes->parent), index / holes->perParent } };

	// Everything else: what has to be there first, worked out below. A part only counts once it's
	// bolted down, so its nuts and bolts are added afterwards.
	auto withFasteners = [&]() {
		std::vector<std::pair<AssemblySlot, int>> expanded = needed;
		for (const auto& [other, otherIndex] : needed)
			for (int fastener = static_cast<int>(A::HEAD_BOLT); fastener < static_cast<int>(A::COUNT); fastener++)
			{
				const auto* holes = Holes(static_cast<A>(fastener));
				if (SlotFor(holes->parent) != other)
					continue;
				for (int hole = 0; hole < holes->perParent; hole++)
					expanded.push_back({ static_cast<A>(fastener), otherIndex * holes->perParent + hole });
			}
		return expanded;
	};

	switch (slot)
	{
		case A::BLOCK:
			break;
		// Bottom end: core plugs into the water jacket, the upper main shells in the block, the
		// crank laid in them, then each journal's lower shell and the main cap over it. Once it's
		// capped, the rear main seal is pressed over the flange.
		case A::CORE_PLUG:
		case A::MAIN_BEARING:
			one(A::BLOCK);
			break;
		case A::CRANKSHAFT:
			all(A::MAIN_BEARING);
			break;
		case A::MAIN_BEARING_LOWER:
			one(A::CRANKSHAFT);
			break;
		case A::MAIN_CAP:
			needed.push_back({ A::MAIN_BEARING_LOWER, index });
			break;
		case A::REAR_MAIN_SEAL:
			all(A::MAIN_CAP);
			break;
		// Each cylinder, once the crank is capped: the piston down its bore, its ring, the rod onto
		// the crank pin with its upper shell, then the lower shell and the rod cap.
		case A::PISTON:
			all(A::MAIN_CAP);
			break;
		case A::PISTON_RING:
		case A::CONNECTING_ROD:
			needed.push_back({ A::PISTON, index });
			break;
		case A::ROD_BEARING:
			needed.push_back({ A::CONNECTING_ROD, index });
			break;
		case A::ROD_BEARING_LOWER:
			needed.push_back({ A::ROD_BEARING, index });
			break;
		case A::ROD_CAP:
			needed.push_back({ A::ROD_BEARING_LOWER, index });
			break;
		// The oil pump slides over the crank snout, the pickup bolts to it.
		case A::OIL_PUMP:
			all(A::MAIN_CAP);
			break;
		case A::OIL_PICKUP:
			one(A::OIL_PUMP);
			break;
		// Top end: the gasket only once the bottom end is finished, the head on it, then the
		// bolts that clamp it down.
		case A::HEAD_GASKET:
			all(A::PISTON);
			all(A::PISTON_RING);
			all(A::CONNECTING_ROD);
			all(A::ROD_BEARING);
			all(A::ROD_BEARING_LOWER);
			all(A::ROD_CAP);
			break;
		case A::CYLINDER_HEAD:
			one(A::HEAD_GASKET);
			break;
		// Valvetrain, valve by valve: valve, stem seal over its guide, spring, retainer and
		// keepers, and the bucket on top. The cam goes on the buckets once they're all in.
		case A::INTAKE_VALVE:
		case A::EXHAUST_VALVE:
			one(A::HEAD_BOLT);
			break;
		case A::VALVE_STEM_SEAL:
			needed.push_back({ index % 2 == 1 ? A::EXHAUST_VALVE : A::INTAKE_VALVE, index / 2 });
			break;
		case A::VALVE_SPRING:
			needed.push_back({ A::VALVE_STEM_SEAL, index });
			break;
		case A::VALVE_RETAINER:
			needed.push_back({ A::VALVE_SPRING, index });
			break;
		case A::LIFTER:
			needed.push_back({ A::VALVE_RETAINER, index });
			break;
		case A::CAMSHAFT:
			all(A::LIFTER);
			break;
		// Then the caps over its journals, each in its own numbered place.
		case A::CAM_CAP:
			one(A::CAMSHAFT);
			break;
		// Timing drive: sprockets on the crank (in front of the pump) and the cam, the idlers, the
		// belt round the lot with the marks lined up, then the tensioner to take up the slack.
		case A::CRANK_SPROCKET:
			one(A::OIL_PUMP);
			break;
		case A::CAM_SPROCKET:
			all(A::CAM_CAP);
			break;
		case A::BELT_IDLER:
			one(A::HEAD_BOLT);
			break;
		case A::TIMING_BELT:
			one(A::CRANK_SPROCKET);
			one(A::CAM_SPROCKET);
			all(A::BELT_IDLER);
			break;
		case A::BELT_TENSIONER:
			one(A::TIMING_BELT);
			break;
		case A::TIMING_COVER_GASKET:
			one(A::BELT_TENSIONER);
			break;
		case A::TIMING_COVER:
			one(A::TIMING_COVER_GASKET);
			break;
		case A::FRONT_CRANK_SEAL:
			one(A::TIMING_COVER);
			break;
		case A::CRANK_PULLEY:
			one(A::FRONT_CRANK_SEAL);
			break;
		// The sump closes up the bottom once everything inside it is done, the timing cover
		// included (the pan seals against its lower edge).
		case A::OIL_PAN_GASKET:
			one(A::OIL_PICKUP);
			one(A::REAR_MAIN_SEAL);
			one(A::TIMING_COVER);
			all(A::ROD_CAP);
			break;
		case A::OIL_PAN:
			one(A::OIL_PAN_GASKET);
			break;
		case A::OIL_DRAIN_PLUG:
		case A::OIL_FILTER:
			one(A::OIL_PAN);
			break;
		// The valve cover closes up the top once the timing drive is in.
		case A::VALVE_COVER_GASKET:
			one(A::BELT_TENSIONER);
			break;
		case A::VALVE_COVER:
			one(A::VALVE_COVER_GASKET);
			break;
		// Ignition: plugs into the head, coils down through the cover onto them.
		case A::SPARK_PLUG:
			one(A::HEAD_BOLT);
			break;
		case A::IGNITION_COIL:
			one(A::VALVE_COVER);
			needed.push_back({ A::SPARK_PLUG, index });
			break;
		// Intake side: gasket, manifold, then what hangs off it.
		case A::INTAKE_MANIFOLD_GASKET:
		case A::EXHAUST_MANIFOLD_GASKET:
		case A::THERMOSTAT:
			one(A::HEAD_BOLT);
			break;
		case A::INTAKE_MANIFOLD:
			one(A::INTAKE_MANIFOLD_GASKET);
			break;
		case A::THROTTLE_BODY:
		case A::FUEL_INJECTOR:
			one(A::INTAKE_MANIFOLD);
			break;
		case A::FUEL_RAIL:
			all(A::FUEL_INJECTOR);
			break;
		case A::EXHAUST_MANIFOLD:
			one(A::EXHAUST_MANIFOLD_GASKET);
			break;
		// Cooling: the water pump bolts to the timing cover; the thermostat sits in the head's
		// water outlet under its housing.
		case A::WATER_PUMP_GASKET:
			one(A::TIMING_COVER);
			break;
		case A::WATER_PUMP:
			one(A::WATER_PUMP_GASKET);
			break;
		case A::THERMOSTAT_HOUSING:
			one(A::THERMOSTAT);
			break;
		default:
			break;
	}
	return withFasteners();
}

std::string EngineAssembly::PositionName(AssemblySlot slot, int index) const
{
	// Nuts and bolts are named for the hole they go in on their part.
	if (const auto* holes = Holes(slot))
		{
			std::string parent = PositionName(SlotFor(holes->parent), index / holes->perParent);
			return holes->perParent > 1 ? std::format("{} {} on {}", AssemblySlotName(slot), index % holes->perParent + 1, parent)
				: std::format("{} on {}", AssemblySlotName(slot), parent);
		}
	// Per-valve positions are named for the valve they sit on.
	if (PartCatalog::Get(SlotPartType(slot)).perEngine == PartCount::PER_VALVE && slot != AssemblySlot::COUNT)
		return std::format("{} {} {}", index % 2 == 1 ? "Exhaust" : "Intake", AssemblySlotName(slot), index / 2 + 1);
	if (SlotCount(slot) > 1)
		return std::format("{} {}", AssemblySlotName(slot), index + 1);
	return AssemblySlotName(slot);
}

std::optional<std::string> EngineAssembly::MissingPrerequisite(AssemblySlot slot, int index) const
{
	for (const auto& [other, otherIndex] : Prerequisites(slot, index))
		if (!Get(other, otherIndex))
			return std::format("The {} has to go on first.", PositionName(other, otherIndex));
	return std::nullopt;
}

std::optional<std::string> EngineAssembly::RemovalBlockedBy(AssemblySlot slot, int index) const
{
	for (const auto& [other, otherIndex] : m_dependents[static_cast<size_t>(slot)][index])
		if (Get(other, otherIndex))
			return std::format("The {} has to come off first.", PositionName(other, otherIndex));
	return std::nullopt;
}

pPart EngineAssembly::Remove(AssemblySlot slot, int index)
{
	if (static_cast<size_t>(slot) >= m_slots.size() || index < 0 || index >= static_cast<int>(m_slots[static_cast<size_t>(slot)].size()))
		return nullptr;

	auto& entry = m_slots[static_cast<size_t>(slot)][index];
	auto removed = entry;
	if (removed)
		m_positions.erase(removed.get());
	entry = nullptr;
	return removed;
}

pPart EngineAssembly::Get(AssemblySlot slot, int index) const
{
	if (static_cast<size_t>(slot) >= m_slots.size() || index < 0 || index >= static_cast<int>(m_slots[static_cast<size_t>(slot)].size()))
		return nullptr;
	return m_slots[static_cast<size_t>(slot)][index];
}

bool EngineAssembly::Contains(const pPart& part) const
{
	return part && m_positions.contains(part.get());
}

std::vector<pPart> EngineAssembly::GetAllParts() const
{
	std::vector<pPart> parts;
	for (const auto& slot : m_slots)
		for (const auto& installed : slot)
			if (installed)
				parts.push_back(installed);
	return parts;
}

void EngineAssembly::Clear()
{
	for (auto& slot : m_slots)
		for (auto& installed : slot)
			installed = nullptr;
	m_positions.clear();
}

bool EngineAssembly::IsComplete() const
{
	return MissingParts().empty();
}

std::vector<std::string> EngineAssembly::MissingParts() const
{
	std::vector<std::string> missing;
	for (size_t slot = 0; slot < m_slots.size(); slot++)
		for (size_t index = 0; index < m_slots[slot].size(); index++)
			if (!m_slots[slot][index])
			{
				auto slotType = static_cast<AssemblySlot>(slot);
				if (SlotCount(slotType) > 1)
					missing.push_back(std::format("{} {}", AssemblySlotName(slotType), index + 1));
				else
					missing.push_back(AssemblySlotName(slotType));
			}
	return missing;
}

const Feature* EngineAssembly::FindFeature(AssemblySlot slot, int index, const std::string& featureName) const
{
	auto part = Get(slot, index);
	return part ? part->FindFeature(featureName) : nullptr;
}

std::optional<double> EngineAssembly::MainClearance(int journal) const
{
	// Only there once the cap is torqued down on both shells.
	auto housing = FindFeature(AssemblySlot::BLOCK, 0, FeatureNames::MainHousing(journal));
	auto upper = FindFeature(AssemblySlot::MAIN_BEARING, journal, FeatureNames::WALL_THICKNESS);
	auto lower = FindFeature(AssemblySlot::MAIN_BEARING_LOWER, journal, FeatureNames::WALL_THICKNESS);
	auto crankJournal = FindFeature(AssemblySlot::CRANKSHAFT, 0, FeatureNames::MainJournal(journal));
	if (!housing || !upper || !lower || !crankJournal || !Get(AssemblySlot::MAIN_CAP, journal))
		return std::nullopt;
	return housing->Size() - upper->Size() - lower->Size() - crankJournal->Size();
}

std::optional<double> EngineAssembly::RodClearance(int cylinder) const
{
	auto bigEnd = FindFeature(AssemblySlot::CONNECTING_ROD, cylinder, FeatureNames::BIG_END);
	auto upper = FindFeature(AssemblySlot::ROD_BEARING, cylinder, FeatureNames::WALL_THICKNESS);
	auto lower = FindFeature(AssemblySlot::ROD_BEARING_LOWER, cylinder, FeatureNames::WALL_THICKNESS);
	auto crankJournal = FindFeature(AssemblySlot::CRANKSHAFT, 0, FeatureNames::RodJournal(cylinder));
	if (!bigEnd || !upper || !lower || !crankJournal || !Get(AssemblySlot::ROD_CAP, cylinder))
		return std::nullopt;
	return bigEnd->Size() - upper->Size() - lower->Size() - crankJournal->Size();
}

std::optional<double> EngineAssembly::PistonToWall(int cylinder) const
{
	auto bore = FindFeature(AssemblySlot::BLOCK, 0, FeatureNames::Bore(cylinder));
	auto skirt = FindFeature(AssemblySlot::PISTON, cylinder, FeatureNames::SKIRT);
	if (!bore || !skirt)
		return std::nullopt;
	return bore->Size() - skirt->Size();
}

std::optional<double> EngineAssembly::RingGap(int cylinder) const
{
	// The ring has to pass through the tightest part of its bore, so that's where it's gapped.
	auto bore = FindFeature(AssemblySlot::BLOCK, 0, FeatureNames::Bore(cylinder));
	auto ring = FindFeature(AssemblySlot::PISTON_RING, cylinder, FeatureNames::CLOSED_DIAMETER);
	if (!bore || !ring)
		return std::nullopt;
	return std::numbers::pi * (bore->MinSample() - ring->Size());
}

std::optional<double> EngineAssembly::DeckClearance(int cylinder) const
{
	auto deckHeight = FindFeature(AssemblySlot::BLOCK, 0, FeatureNames::DECK_HEIGHT);
	auto stroke = FindFeature(AssemblySlot::CRANKSHAFT, 0, FeatureNames::STROKE);
	auto rodLength = FindFeature(AssemblySlot::CONNECTING_ROD, cylinder, FeatureNames::CENTER_LENGTH);
	auto compressionHeight = FindFeature(AssemblySlot::PISTON, cylinder, FeatureNames::COMPRESSION_HEIGHT);
	if (!deckHeight || !stroke || !rodLength || !compressionHeight)
		return std::nullopt;
	return deckHeight->Size() - (stroke->Size() * 0.5 + rodLength->Size() + compressionHeight->Size());
}

std::optional<double> EngineAssembly::Quench(int cylinder) const
{
	auto deckClearance = DeckClearance(cylinder);
	auto gasket = FindFeature(AssemblySlot::HEAD_GASKET, 0, FeatureNames::COMPRESSED_THICKNESS);
	if (!deckClearance || !gasket)
		return std::nullopt;
	return *deckClearance + gasket->Size();
}

std::optional<double> EngineAssembly::SweptVolume(int cylinder) const
{
	auto bore = FindFeature(AssemblySlot::BLOCK, 0, FeatureNames::Bore(cylinder));
	auto stroke = FindFeature(AssemblySlot::CRANKSHAFT, 0, FeatureNames::STROKE);
	if (!bore || !stroke)
		return std::nullopt;
	double diameter = bore->Size();
	return std::numbers::pi / 4.0 * diameter * diameter * stroke->Size() / 1000.0;
}

std::optional<double> EngineAssembly::CompressionRatio(int cylinder) const
{
	auto deckClearance = DeckClearance(cylinder);
	auto bore = FindFeature(AssemblySlot::BLOCK, 0, FeatureNames::Bore(cylinder));
	auto stroke = FindFeature(AssemblySlot::CRANKSHAFT, 0, FeatureNames::STROKE);
	auto chamber = FindFeature(AssemblySlot::CYLINDER_HEAD, 0, FeatureNames::Chamber(cylinder));
	auto dish = FindFeature(AssemblySlot::PISTON, cylinder, FeatureNames::DISH_VOLUME);
	auto gasket = FindFeature(AssemblySlot::HEAD_GASKET, 0, FeatureNames::COMPRESSED_THICKNESS);
	if (!deckClearance || !bore || !stroke || !chamber || !dish || !gasket)
		return std::nullopt;

	return CompressionRatioFrom(bore->Size(), stroke->Size(), chamber->Size(), dish->Size(), m_spec->gasketBore, gasket->Size(), *deckClearance);
}

std::optional<double> EngineAssembly::CompressionRatioFrom(double bore, double stroke, double chamberVolume, double dishVolume,
	double gasketBore, double gasketThickness, double deckClearance)
{
	auto cylinderVolume = [](double diameter, double height) { return std::numbers::pi / 4.0 * diameter * diameter * height / 1000.0; };

	double swept = cylinderVolume(bore, stroke);
	double clearanceVolume = chamberVolume + dishVolume + cylinderVolume(gasketBore, gasketThickness) + cylinderVolume(bore, deckClearance);

	if (clearanceVolume <= 0.0)
		return std::nullopt;
	return (swept + clearanceVolume) / clearanceVolume;
}

std::optional<double> EngineAssembly::CamClearance(int journal) const
{
	auto bore = FindFeature(AssemblySlot::CYLINDER_HEAD, 0, FeatureNames::CamBore(journal));
	auto camJournal = FindFeature(AssemblySlot::CAMSHAFT, 0, FeatureNames::CamJournal(journal));
	if (!bore || !camJournal || !Get(AssemblySlot::CAM_CAP, journal))
		return std::nullopt;
	return bore->Size() - camJournal->Size();
}

std::optional<double> EngineAssembly::StemClearance(int cylinder, bool exhaust) const
{
	auto guide = FindFeature(AssemblySlot::CYLINDER_HEAD, 0, FeatureNames::Guide(cylinder, exhaust));
	auto stem = FindFeature(exhaust ? AssemblySlot::EXHAUST_VALVE : AssemblySlot::INTAKE_VALVE, cylinder, FeatureNames::STEM);
	if (!guide || !stem)
		return std::nullopt;
	return guide->Size() - stem->Size();
}

std::optional<double> EngineAssembly::InstalledHeight(int cylinder, bool exhaust) const
{
	auto valveSlot = exhaust ? AssemblySlot::EXHAUST_VALVE : AssemblySlot::INTAKE_VALVE;
	auto recession = FindFeature(AssemblySlot::CYLINDER_HEAD, 0, FeatureNames::SeatRecession(cylinder, exhaust));
	auto sink = FindFeature(valveSlot, cylinder, FeatureNames::SEAT_SINK);
	auto length = FindFeature(valveSlot, cylinder, FeatureNames::VALVE_LENGTH);
	if (!recession || !sink || !length)
		return std::nullopt;
	return m_spec->installedHeight + recession->Size() + sink->Size() + (length->Size() - m_spec->valveLength.nominal);
}

std::optional<double> EngineAssembly::SeatPressure(int cylinder, bool exhaust) const
{
	auto height = InstalledHeight(cylinder, exhaust);
	int spring = SpringIndex(cylinder, exhaust);
	auto rate = FindFeature(AssemblySlot::VALVE_SPRING, spring, FeatureNames::SPRING_RATE);
	auto freeLength = FindFeature(AssemblySlot::VALVE_SPRING, spring, FeatureNames::FREE_LENGTH);
	if (!height || !rate || !freeLength)
		return std::nullopt;
	return rate->Size() * (freeLength->Size() - *height);
}

double EngineAssembly::Displacement() const
{
	double total = 0.0;
	for (int cylinder = 0; cylinder < m_spec->cylinders; cylinder++)
		total += SweptVolume(cylinder).value_or(0.0);
	return total;
}
