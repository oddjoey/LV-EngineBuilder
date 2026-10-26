#include "EngineAssembly.h"

#include <format>
#include <numbers>

const char* AssemblySlotName(AssemblySlot slot)
{
	switch (slot)
	{
		case AssemblySlot::BLOCK:			return "Block";
		case AssemblySlot::CRANKSHAFT:		return "Crankshaft";
		case AssemblySlot::CYLINDER_HEAD:	return "Cylinder Head";
		case AssemblySlot::HEAD_GASKET:		return "Head Gasket";
		case AssemblySlot::MAIN_BEARING:	return "Main Bearing";
		case AssemblySlot::PISTON:			return "Piston";
		case AssemblySlot::PISTON_RING:		return "Top Ring";
		case AssemblySlot::CONNECTING_ROD:	return "Connecting Rod";
		case AssemblySlot::ROD_BEARING:		return "Rod Bearing";
		case AssemblySlot::CAMSHAFT:		return "Camshaft";
		case AssemblySlot::INTAKE_VALVE:	return "Intake Valve";
		case AssemblySlot::EXHAUST_VALVE:	return "Exhaust Valve";
		case AssemblySlot::VALVE_SPRING:	return "Valve Spring";
		default:							return "Unknown";
	}
}

EngineAssembly::EngineAssembly(const EngineSpec& spec) : m_spec(&spec)
{
	for (size_t slot = 0; slot < m_slots.size(); slot++)
		m_slots[slot].resize(SlotCount(static_cast<AssemblySlot>(slot)));
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
		default:							return PartType::BLOCK;
	}
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
		case AssemblySlot::COUNT:			return 0;
		default:							return 1;
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

	m_slots[static_cast<size_t>(slot)][index] = part;
	return true;
}

pPart EngineAssembly::Remove(AssemblySlot slot, int index)
{
	if (index < 0 || index >= SlotCount(slot))
		return nullptr;

	auto& entry = m_slots[static_cast<size_t>(slot)][index];
	auto removed = entry;
	entry = nullptr;
	return removed;
}

pPart EngineAssembly::Get(AssemblySlot slot, int index) const
{
	if (index < 0 || index >= SlotCount(slot))
		return nullptr;
	return m_slots[static_cast<size_t>(slot)][index];
}

bool EngineAssembly::Contains(const pPart& part) const
{
	for (const auto& slot : m_slots)
		for (const auto& installed : slot)
			if (installed == part)
				return true;
	return false;
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
	auto housing = FindFeature(AssemblySlot::BLOCK, 0, FeatureNames::MainHousing(journal));
	auto shell = FindFeature(AssemblySlot::MAIN_BEARING, journal, FeatureNames::WALL_THICKNESS);
	auto crankJournal = FindFeature(AssemblySlot::CRANKSHAFT, 0, FeatureNames::MainJournal(journal));
	if (!housing || !shell || !crankJournal)
		return std::nullopt;
	return housing->Size() - 2.0 * shell->Size() - crankJournal->Size();
}

std::optional<double> EngineAssembly::RodClearance(int cylinder) const
{
	auto bigEnd = FindFeature(AssemblySlot::CONNECTING_ROD, cylinder, FeatureNames::BIG_END);
	auto shell = FindFeature(AssemblySlot::ROD_BEARING, cylinder, FeatureNames::WALL_THICKNESS);
	auto crankJournal = FindFeature(AssemblySlot::CRANKSHAFT, 0, FeatureNames::RodJournal(cylinder));
	if (!bigEnd || !shell || !crankJournal)
		return std::nullopt;
	return bigEnd->Size() - 2.0 * shell->Size() - crankJournal->Size();
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
	if (!bore || !camJournal)
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
