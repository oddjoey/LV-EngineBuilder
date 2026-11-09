#include "EngineAssembly.h"

#include <algorithm>
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
		case AssemblySlot::CAM_CAP:			return PartType::CAM_CAP;
		case AssemblySlot::ROCKER_SHAFT:	return PartType::ROCKER_SHAFT;
		case AssemblySlot::DISTRIBUTOR:		return PartType::DISTRIBUTOR;
		case AssemblySlot::FLYWHEEL:		return PartType::FLYWHEEL;
		case AssemblySlot::EXHAUST_PIPE:	return PartType::EXHAUST_PIPE;
		case AssemblySlot::ALTERNATOR:		return PartType::ALTERNATOR;
		case AssemblySlot::DRIVE_BELT:		return PartType::DRIVE_BELT;
		case AssemblySlot::DRIVE_BELT_TENSIONER:	return PartType::DRIVE_BELT_TENSIONER;
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
		case AssemblySlot::CAM_CAP:			return m_spec->camJournals;
		case AssemblySlot::COUNT:			return 0;
		case AssemblySlot::BLOCK:
		case AssemblySlot::CRANKSHAFT:
		case AssemblySlot::CYLINDER_HEAD:
		case AssemblySlot::HEAD_GASKET:
		case AssemblySlot::CAMSHAFT:		return 1;
		default:
			if (const auto* holes = Holes(slot))
				return holes->Count(SlotCount(SlotFor(holes->parent)));
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

	auto all = [&](AssemblySlot other)  
	{
		for (int i = 0; i < SlotCount(other); i++)
			needed.push_back({ other, i });
	};

	auto one = [&](AssemblySlot other) 
	{ 
			needed.push_back({ other, 0 }); 
	};

	using A = AssemblySlot;

	// A nut or bolt goes in once the part it holds is on (all of them, for one through every one).
	if (const auto* holes = Holes(slot))
	{
		if (!holes->throughAll)
			return { { SlotFor(holes->parent), index / holes->perParent } };
		std::vector<std::pair<AssemblySlot, int>> parents;
		for (int parent = 0; parent < SlotCount(SlotFor(holes->parent)); parent++)
			parents.push_back({ SlotFor(holes->parent), parent });
		return parents;
	}

	// Everything else: what has to be there first, worked out below. A part only counts once it's
	// bolted down, so its nuts and bolts are added afterwards.
	auto withFasteners = [&]() 
	{
		std::vector<std::pair<AssemblySlot, int>> expanded = needed;
		for (const auto& [other, otherIndex] : needed)
			for (int fastener = static_cast<int>(A::HEAD_BOLT); fastener < static_cast<int>(A::COUNT); fastener++)
			{
				const auto* holes = Holes(static_cast<A>(fastener));

				if (SlotFor(holes->parent) != other)
					continue;

				for (int hole = 0; hole < holes->perParent; hole++)
					expanded.push_back({ static_cast<A>(fastener), holes->throughAll ? hole : otherIndex * holes->perParent + hole });
			}

		return expanded;
	};

	switch (slot)
	{
		case A::BLOCK:
			break;

		// Bottom end: core plugs into the water jacket, the upper main shells in the block, the
		// crank laid in them, then each journal's main cap with its lower shell in it (see GoesInWith).
		// Once it's capped, the rear main seal is pressed over the flange.
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
			one(A::CRANKSHAFT);
			break;
		case A::REAR_MAIN_SEAL:
			all(A::MAIN_CAP);
			break;

		// The flywheel bolts to the crank flange behind the seal.
		case A::FLYWHEEL:
			one(A::REAR_MAIN_SEAL);
			break;

		// Each cylinder, once the crank is capped: the rod, with its upper shell in it and its piston pinned on,
		// down the bore onto the crank pin (see GoesInWith), the piston's ring, then the rod cap with the
		// lower shell in it.
		case A::CONNECTING_ROD:
			all(A::MAIN_CAP);
			break;
		case A::PISTON:
			needed.push_back({ A::CONNECTING_ROD, index });
			break;
		case A::PISTON_RING:
			needed.push_back({ A::PISTON, index });
			break;
		case A::ROD_BEARING:
			needed.push_back({ A::CONNECTING_ROD, index });
			break;
		case A::ROD_BEARING_LOWER:
			needed.push_back({ A::ROD_BEARING, index });
			break;
		case A::ROD_CAP:
			needed.push_back({ A::ROD_BEARING, index });
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
		// keepers. The cam goes in once every valve is closed up, then its caps, then the rocker
		// shafts over it and an arm on its shaft for each valve.
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
		case A::CAMSHAFT:
			all(A::VALVE_RETAINER);
			break;

		// Then the caps over its journals, each in its own numbered place.
		case A::CAM_CAP:
			one(A::CAMSHAFT);
			break;
		case A::ROCKER_SHAFT:
			all(A::CAM_CAP);
			break;
		case A::ROCKER_ARM:
			needed.push_back({ A::ROCKER_SHAFT, index % 2 });
			break;

		// Timing drive: pulleys on the crank (in front of the pump) and the cam, the belt round them with
		// the marks lined up, then the spring-loaded idler pulley onto its back to take up the slack.
		case A::CRANK_SPROCKET:
			one(A::OIL_PUMP);
			break;
		case A::CAM_SPROCKET:
			all(A::CAM_CAP);
			break;
		case A::TIMING_BELT:
			one(A::CRANK_SPROCKET);
			one(A::CAM_SPROCKET);
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

		// The valve cover closes up the top once the timing drive and the rockers are in.
		case A::VALVE_COVER_GASKET:
			one(A::BELT_TENSIONER);
			all(A::ROCKER_ARM);
			break;
		case A::VALVE_COVER:
			one(A::VALVE_COVER_GASKET);
			break;

		// Ignition: plugs into the head, the distributor into the back of the head on the cam, then
		// a lead from its cap down through the cover onto each plug.
		case A::SPARK_PLUG:
			one(A::HEAD_BOLT);
			break;
		case A::DISTRIBUTOR:
			all(A::CAM_CAP);
			break;
		case A::PLUG_LEAD:
			one(A::VALVE_COVER);
			one(A::DISTRIBUTOR);
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
		case A::CARBURETTOR:
			one(A::INTAKE_MANIFOLD);
			break;

		// The fuel pump's arm rides on the cam.
		case A::FUEL_PUMP:
			all(A::CAM_CAP);
			break;
		case A::EXHAUST_MANIFOLD:
			one(A::EXHAUST_MANIFOLD_GASKET);
			break;
		case A::EXHAUST_PIPE:
			one(A::EXHAUST_MANIFOLD);
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

		// Accessory drive: the alternator on its bracket at the front, then the V-belt round the crank
		// pulley, the water pump and the alternator (its pivot sets the tension).
		case A::ALTERNATOR:
			one(A::TIMING_COVER);
			break;
		case A::DRIVE_BELT:
			one(A::CRANK_PULLEY);
			one(A::WATER_PUMP);
			one(A::ALTERNATOR);
			break;
		default:
			break;
	}

	// The leads only push onto the plugs and the cap, over the valve cover: they need those there, not
	// bolted down, so the cover's (and the distributor's) bolts can be undone with them on.
	if (slot == A::PLUG_LEAD)
		return needed;
	return withFasteners();
}

std::string EngineAssembly::PositionName(AssemblySlot slot, int index) const
{
	// Nuts and bolts are named for the hole they go in on their part.
	if (const auto* holes = Holes(slot))
	{
		if (holes->throughAll)
			return std::format("{} {}", AssemblySlotName(slot), index + 1);
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
	// What a rod or cap carries comes off in it; it comes off once nothing over it or them is on.
	if (auto host = CarriedBy(slot); host && Get(*host, index))
		return std::format("The {} comes off with the {}.", PositionName(slot, index), PositionName(*host, index));
	// Everything it carries, and what that carries (a rod: its shell, its piston, the piston's ring).
	std::vector<AssemblySlot> carried = GoesInWith(slot);
	for (size_t i = 0; i < carried.size(); i++)
		for (auto with : GoesInWith(carried[i]))
			carried.push_back(with);
	auto isCarried = [&](AssemblySlot other, int otherIndex) { return otherIndex == index && std::find(carried.begin(), carried.end(), other) != carried.end(); };
	auto blocking = [&](AssemblySlot from) -> std::optional<std::string> {
		for (const auto& [other, otherIndex] : m_dependents[static_cast<size_t>(from)][index])
			if (Get(other, otherIndex) && !isCarried(other, otherIndex))
				return std::format("The {} has to come off first.", PositionName(other, otherIndex));
		return std::nullopt;
	};
	if (auto blocked = blocking(slot))
		return blocked;
	for (auto with : carried)
		if (Get(with, index))
			if (auto blocked = blocking(with))
				return blocked;
	return std::nullopt;
}

std::vector<AssemblySlot> EngineAssembly::GoesInWith(AssemblySlot host)
{
	switch (host)
	{
		case AssemblySlot::CONNECTING_ROD:	return { AssemblySlot::ROD_BEARING, AssemblySlot::PISTON };
		case AssemblySlot::PISTON:			return { AssemblySlot::PISTON_RING };
		case AssemblySlot::ROD_CAP:			return { AssemblySlot::ROD_BEARING_LOWER };
		case AssemblySlot::MAIN_CAP:		return { AssemblySlot::MAIN_BEARING_LOWER };
		default:							return {};
	}
}

std::optional<AssemblySlot> EngineAssembly::CarriedBy(AssemblySlot slot)
{
	switch (slot)
	{
		case AssemblySlot::ROD_BEARING:
		case AssemblySlot::PISTON:				return AssemblySlot::CONNECTING_ROD;
		case AssemblySlot::PISTON_RING:			return AssemblySlot::PISTON;
		case AssemblySlot::ROD_BEARING_LOWER:	return AssemblySlot::ROD_CAP;
		case AssemblySlot::MAIN_BEARING_LOWER:	return AssemblySlot::MAIN_CAP;
		default:								return std::nullopt;
	}
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

std::optional<double> EngineAssembly::RodSideClearance(int cylinder) const
{
	// The rod, with its bearings and cap, on its crank pin: the pin's width less the big end's.
	auto width = FindFeature(AssemblySlot::CONNECTING_ROD, cylinder, FeatureNames::BIG_END_WIDTH);
	auto pin = FindFeature(AssemblySlot::CRANKSHAFT, 0, FeatureNames::RodJournalWidth(cylinder));
	if (!width || !pin || !Get(AssemblySlot::ROD_CAP, cylinder))
		return std::nullopt;
	return pin->Size() - width->Size();
}

std::optional<double> EngineAssembly::BeltDeflection(bool fanBelt) const
{
	// A nut or bolt done up at all (snugged or torqued).
	auto boltedUp = [&](AssemblySlot slot, int count) {
		for (int index = 0; index < count; index++)
			if (auto bolt = Get(slot, index); !bolt || bolt->GetTightness() == FastenerTightness::LOOSE)
				return false;
		return true;
	};
	if (fanBelt)
	{
		auto belt = Get(AssemblySlot::DRIVE_BELT, 0);
		if (!belt || !Get(AssemblySlot::CRANK_PULLEY, 0) || !Get(AssemblySlot::WATER_PUMP, 0) || !Get(AssemblySlot::ALTERNATOR, 0))
			return std::nullopt;
		// Each belt sits its own way within its range, the same every time it's checked: a new one about
		// the middle of the new figure, a used one stretched to about the middle of the used one.
		double spread = static_cast<double>((belt->GetId() * 2654435761u) % 1000u) / 1000.0 - 0.5;
		double deflection = belt->GetCondition() == PartCondition::NEW ? 9.5 + spread * 1.6 : 11.0 + spread * 3.0;
		// Swung out on its pivot and bolted there it pulls the belt tight; loose, it hangs.
		if (!boltedUp(AssemblySlot::ALTERNATOR_BOLT, SlotCount(AssemblySlot::ALTERNATOR_BOLT)))
			deflection += 6.0;
		return deflection;
	}
	auto belt = FindFeature(AssemblySlot::TIMING_BELT, 0, FeatureNames::BELT_TOOTH_HEIGHT);
	auto spring = FindFeature(AssemblySlot::BELT_TENSIONER, 0, FeatureNames::TENSIONER_TRAVEL);
	if (!belt || !spring)
		return std::nullopt;
	// Set right, 6.5 mm. The further the idler's spring has had to travel, the less it has left to pull
	// with; worn teeth are a stretched belt.
	double deflection = 6.5 + (spring->Size() - 7.5) * 0.2 + (std::max)(0.0, 1.95 - belt->Size()) * 6.0;
	// Until the idler's bolted, nothing holds it against the belt.
	if (!boltedUp(AssemblySlot::BELT_IDLER_BOLT, SlotCount(AssemblySlot::BELT_IDLER_BOLT)))
		deflection += 4.0;
	return (std::max)(deflection, 3.0);
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
