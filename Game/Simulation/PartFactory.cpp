#include "PartFactory.h"

#include <cmath>
#include <format>
#include <stdexcept>

#include "PartCatalog.h"
#include "Random.h"

pPart PartFactory::Create(PartType type, const EngineSpec& spec, PartQuality quality, PartCondition condition, double sizeOffset)
{
	pPart part = nullptr;
	switch (type)
	{
		case PartType::BLOCK:			part = CreateBlock(spec, quality, condition); break;
		case PartType::CRANKSHAFT:		part = CreateCrankshaft(spec, quality, condition); break;
		case PartType::CONNECTING_ROD:	part = CreateConnectingRod(spec, quality, condition); break;
		case PartType::PISTON:			part = CreatePiston(spec, quality, condition, sizeOffset); break;
		case PartType::PISTON_RING:		part = CreatePistonRing(spec, quality, condition, sizeOffset); break;
		case PartType::MAIN_BEARING:
		case PartType::ROD_BEARING:
		case PartType::MAIN_BEARING_LOWER:
		case PartType::ROD_BEARING_LOWER:	part = CreateBearing(type, spec, quality, condition, sizeOffset); break;
		case PartType::CYLINDER_HEAD:	part = CreateCylinderHead(spec, quality, condition); break;
		case PartType::HEAD_GASKET:		part = CreateHeadGasket(spec, quality, condition); break;
		case PartType::CAMSHAFT:		part = CreateCamshaft(spec, quality, condition); break;
		case PartType::INTAKE_VALVE:
		case PartType::EXHAUST_VALVE:	part = CreateValve(type == PartType::EXHAUST_VALVE, spec, quality, condition, sizeOffset); break;
		case PartType::VALVE_SPRING:	part = CreateValveSpring(spec, quality, condition); break;
		default:						part = CreateAncillary(type, spec, quality, condition); break;
	}

	if (!part)
		throw std::runtime_error("Unknown part type!");

	part->m_sizeOffset = sizeOffset;
	part->m_value = Price(type, quality, condition);
	return part;
}

std::vector<pPart> PartFactory::CreateCompanions(const pPart& part, const EngineSpec& spec)
{
	// What comes in the same box: a rod's cap, a block's main caps, a head's cam caps, a bearing's
	// other shell.
	std::vector<pPart> companions;
	auto make = [&](PartType type) {
		auto companion = Create(type, spec, part->GetQuality(), part->GetCondition() == PartCondition::DAMAGED ? PartCondition::USED : part->GetCondition(),
			part->GetSizeOffset());
		companions.push_back(companion);
		return companion;
	};
	switch (part->GetType())
	{
		case PartType::BLOCK:
			for (int journal = 0; journal < spec.mainJournals; journal++)
			{
				auto cap = make(PartType::MAIN_CAP);
				cap->MatchTo(part->GetId(), journal);
				cap->m_name = std::format("Main Cap {} (block #{})", journal + 1, part->GetId());
			}
			break;
		case PartType::CYLINDER_HEAD:
			for (int journal = 0; journal < spec.camJournals; journal++)
			{
				auto cap = make(PartType::CAM_CAP);
				cap->MatchTo(part->GetId(), journal);
				cap->m_material = part->GetMaterial();
				cap->m_name = std::format("Cam Cap {} (head #{})", journal + 1, part->GetId());
			}
			break;
		case PartType::CONNECTING_ROD:
		{
			auto cap = make(PartType::ROD_CAP);
			cap->MatchTo(part->GetId());
			cap->m_material = part->GetMaterial();
			cap->m_name = std::format("Rod Cap (rod #{})", part->GetId());
			break;
		}
		case PartType::MAIN_BEARING:	make(PartType::MAIN_BEARING_LOWER); break;
		case PartType::ROD_BEARING:		make(PartType::ROD_BEARING_LOWER); break;
		default:						break;
	}
	return companions;
}

double PartFactory::PriceWithCompanions(PartType type, PartQuality quality, PartCondition condition, const EngineSpec& spec)
{
	double price = Price(type, quality, condition);
	switch (type)
	{
		case PartType::BLOCK:			price += Price(PartType::MAIN_CAP, quality, condition) * spec.mainJournals; break;
		case PartType::CYLINDER_HEAD:	price += Price(PartType::CAM_CAP, quality, condition) * spec.camJournals; break;
		case PartType::CONNECTING_ROD:	price += Price(PartType::ROD_CAP, quality, condition); break;
		case PartType::MAIN_BEARING:	price += Price(PartType::MAIN_BEARING_LOWER, quality, condition); break;
		case PartType::ROD_BEARING:		price += Price(PartType::ROD_BEARING_LOWER, quality, condition); break;
		default:						break;
	}
	return price;
}

double PartFactory::Price(PartType type, PartQuality quality, PartCondition condition)
{
	double base = PartCatalog::Get(type).basePrice;

	static const double qualityMultiplier[] = { 0.6, 1.0, 1.8 };
	base *= qualityMultiplier[static_cast<size_t>(quality)];

	if (condition == PartCondition::USED)
		base *= 0.3;
	return std::round(base);
}

double PartFactory::Spread(PartQuality quality)
{
	switch (quality)
	{
		case PartQuality::BUDGET:		return 1.6;
		case PartQuality::PERFORMANCE:	return 0.6;
		default:						return 1.0;
	}
}

double PartFactory::Sample(const Tolerance& tolerance, PartQuality quality)
{
	// An OEM process holds the tolerance band at +/-3 sigma; budget parts sometimes fall outside it.
	return Random::Normal(tolerance.Mid(), tolerance.Width() / 6.0 * Spread(quality));
}

double PartFactory::SmallDeviation(double limit, PartQuality quality)
{
	return std::abs(Random::Normal(0.0, limit * 0.2 * Spread(quality)));
}

void PartFactory::ShapeRound(Feature& feature, double size, double taper, double outOfRound)
{
	// Station 0 is the top of a bore, where wear is worst; axis 0 is the thrust axis.
	for (int station = 0; station < Feature::STATIONS; station++)
		for (int axis = 0; axis < Feature::AXES; axis++)
		{
			double stationFactor = 1.0 - static_cast<double>(station) / (Feature::STATIONS - 1);
			double axisFactor = axis == 0 ? 0.5 : -0.5;
			feature.samples[station][axis] = size + taper * (stationFactor - 0.5) + outOfRound * axisFactor + Random::Normal(0.0, 0.0004);
		}
	feature.value = size;
}

Feature PartFactory::MakeFeature(const std::string& name, FeatureKind kind, const Tolerance& spec)
{
	Feature feature;
	feature.name = name;
	feature.kind = kind;
	feature.spec = spec;
	return feature;
}

std::string PartFactory::SizeLabel(double sizeOffset)
{
	if (std::abs(sizeOffset) < 1e-6)
		return "STD";
	return std::format("{:+.2f}", sizeOffset);
}

pPart PartFactory::CreateBlock(const EngineSpec& spec, PartQuality quality, PartCondition condition)
{
	bool used = condition == PartCondition::USED;
	auto part = std::make_shared<Part>(PartType::BLOCK, std::format("{} {} Block", used ? "Used" : PartQualityName(quality), spec.name),
		MaterialType::GREY_IRON, quality, condition);

	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
	{
		auto bore = MakeFeature(FeatureNames::Bore(cylinder), FeatureKind::BORE, spec.bore);
		bore.sizeClasses = { 0.0, 0.25, 0.50, 0.75, 1.00 };
		bore.maxTaper = spec.maxBoreTaper;
		bore.maxOutOfRound = spec.maxBoreOutOfRound;
		bore.minRa = spec.boreFinish.min;
		bore.maxRa = spec.boreFinish.max;
		bore.operations = { MachineOperation::BORE, MachineOperation::HONE };

		if (used)
		{
			// Rings wear the top of the bore most and the thrust side harder than the pin side.
			ShapeRound(bore, spec.bore.Mid() + Random::Uniform(0.02, 0.09), Random::Uniform(0.015, 0.060), Random::Uniform(0.008, 0.035));
			bore.surfaceRa = Random::Chance(0.15) ? Random::Uniform(1.2, 2.0) : Random::Uniform(0.05, 0.15);
		}
		else
		{
			ShapeRound(bore, Sample(spec.bore, quality), SmallDeviation(spec.maxBoreTaper, quality), SmallDeviation(spec.maxBoreOutOfRound, quality));
			bore.surfaceRa = Random::Uniform(spec.boreFinish.min + 0.05, spec.boreFinish.max - 0.10);
		}
		part->m_features.push_back(bore);
	}

	for (int journal = 0; journal < spec.mainJournals; journal++)
	{
		auto housing = MakeFeature(FeatureNames::MainHousing(journal), FeatureKind::BORE, spec.mainHousing);
		housing.maxTaper = 0.008;
		housing.maxOutOfRound = 0.008;
		housing.operations = { MachineOperation::LINE_BORE };
		double outOfRound = used && Random::Chance(0.2) ? Random::Uniform(0.010, 0.020) : SmallDeviation(0.008, quality);
		ShapeRound(housing, Sample(spec.mainHousing, quality), SmallDeviation(0.008, quality), outOfRound);
		housing.surfaceRa = Random::Uniform(0.6, 1.2);
		part->m_features.push_back(housing);
	}

	auto flatness = MakeFeature(FeatureNames::DECK_FLATNESS, FeatureKind::FLATNESS, Tolerance::Limits(0.0, spec.maxDeckFlatness));
	flatness.maxRa = 1.6;
	flatness.operations = { MachineOperation::SURFACE };
	flatness.value = used ? Random::Uniform(0.02, 0.12) : SmallDeviation(spec.maxDeckFlatness, quality);
	flatness.surfaceRa = Random::Uniform(0.6, 1.2);
	part->m_features.push_back(flatness);

	auto deckHeight = MakeFeature(FeatureNames::DECK_HEIGHT, FeatureKind::LENGTH, spec.deckHeight);
	deckHeight.value = Sample(spec.deckHeight, quality);
	part->m_features.push_back(deckHeight);

	return part;
}

pPart PartFactory::CreateCrankshaft(const EngineSpec& spec, PartQuality quality, PartCondition condition)
{
	bool used = condition == PartCondition::USED;
	auto material = quality == PartQuality::PERFORMANCE ? MaterialType::FORGED_4340 : MaterialType::NODULAR_IRON;
	auto part = std::make_shared<Part>(PartType::CRANKSHAFT, std::format("{} {} Crankshaft", used ? "Used" : PartQualityName(quality),
		quality == PartQuality::PERFORMANCE ? "Forged" : "Cast"), material, quality, condition);

	auto addJournal = [&](const std::string& name, const Tolerance& tolerance)
	{
		auto journal = MakeFeature(name, FeatureKind::JOURNAL, tolerance);
		journal.sizeClasses = { 0.0, -0.25, -0.50, -0.75 };
		journal.maxTaper = spec.maxJournalTaper;
		journal.maxOutOfRound = spec.maxJournalOutOfRound;
		journal.maxRa = spec.maxJournalFinish;
		journal.operations = { MachineOperation::GRIND, MachineOperation::POLISH };

		if (used)
		{
			ShapeRound(journal, tolerance.Mid() - Random::Uniform(0.005, 0.025), Random::Uniform(0.002, 0.012), Random::Uniform(0.003, 0.015));
			journal.surfaceRa = Random::Uniform(0.25, 0.80);
		}
		else
		{
			ShapeRound(journal, Sample(tolerance, quality), SmallDeviation(spec.maxJournalTaper, quality), SmallDeviation(spec.maxJournalOutOfRound, quality));
			journal.surfaceRa = Random::Uniform(0.08, 0.20);
		}
		part->m_features.push_back(journal);
	};

	for (int journal = 0; journal < spec.mainJournals; journal++)
		addJournal(FeatureNames::MainJournal(journal), spec.mainJournal);
	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
		addJournal(FeatureNames::RodJournal(cylinder), spec.rodJournal);

	auto stroke = MakeFeature(FeatureNames::STROKE, FeatureKind::LENGTH, spec.stroke);
	stroke.value = Sample(spec.stroke, quality);
	part->m_features.push_back(stroke);

	return part;
}

pPart PartFactory::CreateConnectingRod(const EngineSpec& spec, PartQuality quality, PartCondition condition)
{
	bool used = condition == PartCondition::USED;
	auto material = quality == PartQuality::PERFORMANCE ? MaterialType::FORGED_4340 : MaterialType::POWDER_METAL;
	auto part = std::make_shared<Part>(PartType::CONNECTING_ROD, std::format("{} Connecting Rod", used ? "Used" : PartQualityName(quality)),
		material, quality, condition);

	auto bigEnd = MakeFeature(FeatureNames::BIG_END, FeatureKind::BORE, spec.rodHousing);
	bigEnd.maxTaper = 0.006;
	bigEnd.maxOutOfRound = 0.006;
	// Used rods stretch into an oval at the big end.
	double outOfRound = used ? Random::Uniform(0.002, 0.015) : SmallDeviation(0.006, quality);
	ShapeRound(bigEnd, Sample(spec.rodHousing, quality), SmallDeviation(0.006, quality), outOfRound);
	bigEnd.surfaceRa = Random::Uniform(0.4, 0.8);
	part->m_features.push_back(bigEnd);

	auto length = MakeFeature(FeatureNames::CENTER_LENGTH, FeatureKind::LENGTH, spec.rodLength);
	length.value = Sample(spec.rodLength, quality);
	part->m_features.push_back(length);

	return part;
}

pPart PartFactory::CreatePiston(const EngineSpec& spec, PartQuality quality, PartCondition condition, double sizeOffset)
{
	MaterialType material = MaterialType::CAST_ALUMINUM;
	if (quality == PartQuality::OEM)
		material = MaterialType::HYPEREUTECTIC_ALUMINUM;
	else if (quality == PartQuality::PERFORMANCE)
		material = MaterialType::FORGED_2618;

	auto part = std::make_shared<Part>(PartType::PISTON, std::format("{} Piston {}", PartQualityName(quality), SizeLabel(sizeOffset)),
		material, quality, condition);

	// Pistons are ground to suit their alloy: forged pistons are made smaller because they grow more hot.
	double clearanceShift = spec.PistonToWallFor(material).min - spec.pistonToWall.min;
	auto skirtSpec = spec.pistonSkirt.Shifted(sizeOffset - clearanceShift);

	auto skirt = MakeFeature(FeatureNames::SKIRT, FeatureKind::LENGTH, skirtSpec);
	skirt.value = Sample(skirtSpec, quality);
	if (condition == PartCondition::USED)
		skirt.value -= Random::Uniform(0.005, 0.015);
	part->m_features.push_back(skirt);

	auto compressionHeight = MakeFeature(FeatureNames::COMPRESSION_HEIGHT, FeatureKind::LENGTH, spec.compressionHeight);
	compressionHeight.value = Sample(spec.compressionHeight, quality);
	part->m_features.push_back(compressionHeight);

	auto dish = MakeFeature(FeatureNames::DISH_VOLUME, FeatureKind::VOLUME, spec.dishVolume);
	dish.value = Sample(spec.dishVolume, quality);
	part->m_features.push_back(dish);

	return part;
}

pPart PartFactory::CreatePistonRing(const EngineSpec& spec, PartQuality quality, PartCondition condition, double sizeOffset)
{
	auto part = std::make_shared<Part>(PartType::PISTON_RING, std::format("{} Top Ring {}", PartQualityName(quality), SizeLabel(sizeOffset)),
		MaterialType::DUCTILE_IRON, quality, condition);

	// Performance rings are "file-fit": deliberately left tight so the builder can gap each one
	// to its own bore. Cheaper rings are pre-gapped and land wherever they land.
	double fileFitAllowance = quality == PartQuality::PERFORMANCE ? 0.05 : 0.0;
	auto closedSpec = Tolerance::Bilateral(spec.ringClosedDiameter + sizeOffset + fileFitAllowance, 0.02);

	auto closed = MakeFeature(FeatureNames::CLOSED_DIAMETER, FeatureKind::LENGTH, closedSpec);
	closed.directlyMeasurable = false;
	closed.operations = { MachineOperation::FILE_RING };
	closed.value = Sample(closedSpec, quality);
	part->m_features.push_back(closed);

	return part;
}

pPart PartFactory::CreateBearing(PartType type, const EngineSpec& spec, PartQuality quality, PartCondition condition, double sizeOffset)
{
	// Undersize bearings have thicker walls to make up for the reground journal.
	bool main = type == PartType::MAIN_BEARING || type == PartType::MAIN_BEARING_LOWER;
	auto shellSpec = (main ? spec.mainShell : spec.rodShell).Shifted(-sizeOffset * 0.5);
	auto part = std::make_shared<Part>(type, std::format("{} {} {}", PartQualityName(quality), PartTypeName(type), SizeLabel(sizeOffset)),
		MaterialType::BIMETAL, quality, condition);

	auto wall = MakeFeature(FeatureNames::WALL_THICKNESS, FeatureKind::THICKNESS, shellSpec);
	wall.value = Sample(shellSpec, quality);
	part->m_features.push_back(wall);

	return part;
}

pPart PartFactory::CreateCylinderHead(const EngineSpec& spec, PartQuality quality, PartCondition condition)
{
	bool used = condition == PartCondition::USED;
	auto part = std::make_shared<Part>(PartType::CYLINDER_HEAD, std::format("{} Cylinder Head", used ? "Used" : PartQualityName(quality)),
		MaterialType::ALUMINUM_A356, quality, condition);

	auto flatness = MakeFeature(FeatureNames::DECK_FLATNESS, FeatureKind::FLATNESS, Tolerance::Limits(0.0, spec.maxHeadFlatness));
	flatness.maxRa = 1.6;
	flatness.operations = { MachineOperation::SURFACE };
	// Aluminium heads warp when an engine overheats.
	flatness.value = used ? Random::Uniform(0.03, 0.15) : SmallDeviation(spec.maxHeadFlatness, quality);
	flatness.surfaceRa = Random::Uniform(0.6, 1.2);
	part->m_features.push_back(flatness);

	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
	{
		auto chamber = MakeFeature(FeatureNames::Chamber(cylinder), FeatureKind::VOLUME, spec.chamberVolume);
		chamber.value = Sample(spec.chamberVolume, quality);
		part->m_features.push_back(chamber);
	}

	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
		for (bool exhaust : { false, true })
		{
			// Guides wear into a bellmouth at both ends from the valve rocking in them.
			auto guide = MakeFeature(FeatureNames::Guide(cylinder, exhaust), FeatureKind::BORE, spec.guideBore);
			guide.sizeClasses = { 0.0, 0.075, 0.15, 0.38 };
			guide.maxTaper = spec.maxGuideTaper;
			guide.maxOutOfRound = spec.maxGuideOutOfRound;
			guide.operations = { MachineOperation::REAM };
			if (used)
			{
				// Exhaust guides run hotter and wear faster.
				double wear = Random::Uniform(0.008, exhaust ? 0.045 : 0.03);
				ShapeRound(guide, spec.guideBore.Mid() + wear, Random::Uniform(0.006, 0.03), Random::Uniform(0.004, 0.018));
			}
			else
				ShapeRound(guide, Sample(spec.guideBore, quality), SmallDeviation(spec.maxGuideTaper, quality), SmallDeviation(spec.maxGuideOutOfRound, quality));
			guide.surfaceRa = Random::Uniform(0.4, 0.8);
			part->m_features.push_back(guide);

			// Seats get hammered wider and pounded down into the head over the miles.
			auto width = MakeFeature(FeatureNames::SeatWidth(cylinder, exhaust), FeatureKind::LENGTH, Tolerance::Limits(spec.seatWidth.min, spec.seatWidth.max));
			width.operations = { MachineOperation::CUT_SEAT };
			width.value = used ? Random::Uniform(1.6, 2.3) : Random::Uniform(spec.seatWidth.min + 0.1, spec.seatWidth.max - 0.1);
			part->m_features.push_back(width);

			auto runout = MakeFeature(FeatureNames::SeatRunout(cylinder, exhaust), FeatureKind::RUNOUT, Tolerance::Limits(0.0, spec.maxSeatRunout));
			runout.value = used ? Random::Uniform(0.015, 0.07) : SmallDeviation(spec.maxSeatRunout, quality);
			part->m_features.push_back(runout);

			// How far the seat has sunk; shows up as installed height on the stand, not directly.
			auto recession = MakeFeature(FeatureNames::SeatRecession(cylinder, exhaust), FeatureKind::LENGTH, Tolerance::Limits(0.0, 0.5));
			recession.directlyMeasurable = false;
			recession.value = used ? Random::Uniform(0.08, 0.45) : Random::Uniform(0.0, 0.04);
			part->m_features.push_back(recession);
		}

	for (int journal = 0; journal < spec.camJournals; journal++)
	{
		auto camBore = MakeFeature(FeatureNames::CamBore(journal), FeatureKind::BORE, spec.camBore);
		camBore.maxTaper = 0.008;
		camBore.maxOutOfRound = 0.008;
		// Aluminium cam bores scuff if an engine ran low on oil.
		double wear = used && Random::Chance(0.25) ? Random::Uniform(0.01, 0.03) : 0.0;
		ShapeRound(camBore, Sample(spec.camBore, quality) + wear, SmallDeviation(0.008, quality), SmallDeviation(0.008, quality) + wear * 0.5);
		camBore.surfaceRa = Random::Uniform(0.4, 0.9);
		part->m_features.push_back(camBore);
	}

	return part;
}

pPart PartFactory::CreateCamshaft(const EngineSpec& spec, PartQuality quality, PartCondition condition)
{
	bool used = condition == PartCondition::USED;
	auto part = std::make_shared<Part>(PartType::CAMSHAFT, std::format("{} Camshaft", used ? "Used" : PartQualityName(quality)),
		quality == PartQuality::PERFORMANCE ? MaterialType::FORGED_4340 : MaterialType::NODULAR_IRON, quality, condition);

	for (int journal = 0; journal < spec.camJournals; journal++)
	{
		auto feature = MakeFeature(FeatureNames::CamJournal(journal), FeatureKind::JOURNAL, spec.camJournal);
		feature.maxTaper = 0.006;
		feature.maxOutOfRound = 0.006;
		feature.maxRa = 0.3;
		feature.operations = { MachineOperation::POLISH };
		double wear = used ? Random::Uniform(0.002, 0.015) : 0.0;
		ShapeRound(feature, Sample(spec.camJournal, quality) - wear, SmallDeviation(0.006, quality), SmallDeviation(0.006, quality) + wear * 0.3);
		feature.surfaceRa = used ? Random::Uniform(0.2, 0.6) : Random::Uniform(0.1, 0.2);
		part->m_features.push_back(feature);
	}

	// Performance cams grind a little more lift; worn lobes lose it, and the odd one goes flat.
	auto liftSpec = quality == PartQuality::PERFORMANCE ? spec.lobeLift.Shifted(0.6) : spec.lobeLift;
	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
		for (bool exhaust : { false, true })
		{
			auto lobe = MakeFeature(FeatureNames::LobeLift(cylinder, exhaust), FeatureKind::LENGTH, liftSpec);
			lobe.value = Sample(liftSpec, quality);
			if (used)
				lobe.value -= Random::Chance(0.1) ? Random::Uniform(0.5, 1.5) : Random::Uniform(0.01, 0.12);
			part->m_features.push_back(lobe);
		}

	return part;
}

pPart PartFactory::CreateValve(bool exhaust, const EngineSpec& spec, PartQuality quality, PartCondition condition, double sizeOffset)
{
	bool used = condition == PartCondition::USED;
	auto type = exhaust ? PartType::EXHAUST_VALVE : PartType::INTAKE_VALVE;
	auto material = exhaust || quality == PartQuality::PERFORMANCE ? MaterialType::FORGED_4340 : MaterialType::POWDER_METAL;
	auto part = std::make_shared<Part>(type, std::format("{} {} {}", used ? "Used" : PartQualityName(quality), PartTypeName(type), SizeLabel(sizeOffset)),
		material, quality, condition);

	// Oversize-stem valves suit guides reamed to the matching size.
	auto stemSpec = (exhaust ? spec.exhaustStem : spec.intakeStem).Shifted(sizeOffset);
	auto stem = MakeFeature(FeatureNames::STEM, FeatureKind::JOURNAL, stemSpec);
	stem.maxTaper = 0.008;
	stem.maxOutOfRound = 0.006;
	double stemWear = used ? Random::Uniform(0.003, 0.015) : 0.0;
	ShapeRound(stem, Sample(stemSpec, quality) - stemWear, SmallDeviation(0.008, quality) + stemWear * 0.6, SmallDeviation(0.006, quality));
	stem.surfaceRa = Random::Uniform(0.1, 0.3);
	part->m_features.push_back(stem);

	auto margin = MakeFeature(FeatureNames::MARGIN, FeatureKind::LENGTH, spec.valveMargin);
	margin.value = used ? Random::Uniform(0.75, 1.15) : Sample(spec.valveMargin, quality);
	part->m_features.push_back(margin);

	auto runout = MakeFeature(FeatureNames::FACE_RUNOUT, FeatureKind::RUNOUT, Tolerance::Limits(0.0, spec.maxValveRunout));
	runout.operations = { MachineOperation::FACE_VALVE };
	runout.value = used ? Random::Uniform(0.01, 0.06) : SmallDeviation(spec.maxValveRunout, quality);
	part->m_features.push_back(runout);

	auto length = MakeFeature(FeatureNames::VALVE_LENGTH, FeatureKind::LENGTH, spec.valveLength);
	length.value = Sample(spec.valveLength, quality);
	part->m_features.push_back(length);

	// How much deeper the valve sits because its face has been reground.
	auto sink = MakeFeature(FeatureNames::SEAT_SINK, FeatureKind::LENGTH, Tolerance::Limits(0.0, 0.5));
	sink.directlyMeasurable = false;
	part->m_features.push_back(sink);

	return part;
}

pPart PartFactory::CreateValveSpring(const EngineSpec& spec, PartQuality quality, PartCondition condition)
{
	bool used = condition == PartCondition::USED;
	auto part = std::make_shared<Part>(PartType::VALVE_SPRING, std::format("{} Valve Spring", used ? "Used" : PartQualityName(quality)),
		MaterialType::FORGED_4340, quality, condition);

	// Performance springs are stiffer; old springs sag shorter and lose pressure.
	double stiffness = quality == PartQuality::PERFORMANCE ? 1.15 : 1.0;
	double sag = used ? Random::Uniform(0.2, 1.2) : 0.0;

	auto rate = MakeFeature(FeatureNames::SPRING_RATE, FeatureKind::FORCE, Tolerance::Bilateral(spec.springRate * stiffness, spec.springRate * 0.05));
	rate.directlyMeasurable = false;
	rate.value = Sample(rate.spec, quality);
	part->m_features.push_back(rate);

	auto freeLength = MakeFeature(FeatureNames::FREE_LENGTH, FeatureKind::LENGTH, spec.springFreeLength);
	freeLength.value = Sample(spec.springFreeLength, quality) - sag;
	part->m_features.push_back(freeLength);

	// What a spring tester reads with the spring squeezed to the nominal installed height.
	auto pressureSpec = Tolerance::Limits(spec.springSeatPressure.Min() * stiffness, spec.springSeatPressure.Max() * stiffness);
	auto pressure = MakeFeature(FeatureNames::SEAT_PRESSURE, FeatureKind::FORCE, pressureSpec);
	pressure.value = rate.value * (freeLength.value - spec.installedHeight);
	part->m_features.push_back(pressure);

	return part;
}

pPart PartFactory::CreateHeadGasket(const EngineSpec& spec, PartQuality quality, PartCondition condition)
{
	auto part = std::make_shared<Part>(PartType::HEAD_GASKET, std::format("{} MLS Head Gasket", PartQualityName(quality)),
		MaterialType::MLS_STEEL, quality, condition);

	auto thickness = MakeFeature(FeatureNames::COMPRESSED_THICKNESS, FeatureKind::THICKNESS, spec.gasketThickness);
	thickness.value = Sample(spec.gasketThickness, quality);
	part->m_features.push_back(thickness);

	return part;
}

pPart PartFactory::CreateAncillary(PartType type, const EngineSpec& spec, PartQuality quality, PartCondition condition)
{
	(void)spec;
	bool used = condition == PartCondition::USED;

	MaterialType material = MaterialType::HARDENED_STEEL;
	switch (type)
	{
		case PartType::CORE_PLUG:
		case PartType::OIL_PICKUP:
		case PartType::OIL_PAN:
		case PartType::OIL_FILTER:
		case PartType::FUEL_RAIL:
		case PartType::THERMOSTAT:				material = MaterialType::STAMPED_STEEL; break;
		case PartType::REAR_MAIN_SEAL:
		case PartType::FRONT_CRANK_SEAL:
		case PartType::VALVE_STEM_SEAL:
		case PartType::VALVE_COVER_GASKET:		material = MaterialType::FKM_RUBBER; break;
		case PartType::TIMING_BELT:				material = MaterialType::HNBR_BELT; break;
		case PartType::OIL_PAN_GASKET:
		case PartType::TIMING_COVER_GASKET:
		case PartType::INTAKE_MANIFOLD_GASKET:
		case PartType::WATER_PUMP_GASKET:		material = MaterialType::COMPOSITE_GASKET; break;
		case PartType::EXHAUST_MANIFOLD_GASKET:	material = MaterialType::MLS_STEEL; break;
		case PartType::OIL_PUMP:
		case PartType::TIMING_COVER:
		case PartType::VALVE_COVER:
		case PartType::INTAKE_MANIFOLD:
		case PartType::THROTTLE_BODY:
		case PartType::WATER_PUMP:
		case PartType::THERMOSTAT_HOUSING:		material = MaterialType::CAST_ALUMINUM; break;
		case PartType::IGNITION_COIL:			material = MaterialType::NYLON; break;
		case PartType::CRANK_PULLEY:
		case PartType::EXHAUST_MANIFOLD:		material = MaterialType::GREY_IRON; break;
		case PartType::SPARK_PLUG:				material = MaterialType::SPARK_PLUG_ALLOY; break;
		case PartType::ROD_CAP:					material = MaterialType::POWDER_METAL; break;
		case PartType::MAIN_CAP:				material = MaterialType::NODULAR_IRON; break;
		case PartType::CAM_CAP:					material = MaterialType::ALUMINUM_A356; break;
		default:								break;
	}

	auto part = std::make_shared<Part>(type, std::format("{} {}", used ? "Used" : PartQualityName(quality), PartTypeName(type)),
		material, quality, condition);

	// A simple measured size: new parts scatter around the spec, used ones have worn by wear.
	auto addLength = [&](const std::string& name, FeatureKind kind, const Tolerance& tolerance, double wearMin, double wearMax) -> Feature&
	{
		auto feature = MakeFeature(name, kind, tolerance);
		feature.value = Sample(tolerance, quality) + (used ? Random::Uniform(wearMin, wearMax) : 0.0);
		part->m_features.push_back(feature);
		return part->m_features.back();
	};
	// A sealing face: flat when new, warped or dented by heat cycles and over-tightened bolts.
	auto addFlatness = [&](const std::string& name, double limit, double usedMax, bool machinable)
	{
		auto feature = MakeFeature(name, FeatureKind::FLATNESS, Tolerance::Limits(0.0, limit));
		feature.value = used ? Random::Uniform(limit * 0.2, usedMax) : SmallDeviation(limit, quality);
		feature.surfaceRa = Random::Uniform(0.8, 1.6);
		if (machinable)
		{
			feature.maxRa = 3.2;
			feature.operations = { MachineOperation::SURFACE };
		}
		part->m_features.push_back(feature);
	};
	auto addRunout = [&](const std::string& name, double limit, double usedMax)
	{
		auto feature = MakeFeature(name, FeatureKind::RUNOUT, Tolerance::Limits(0.0, limit));
		feature.value = used ? Random::Uniform(limit * 0.2, usedMax) : SmallDeviation(limit, quality);
		part->m_features.push_back(feature);
	};
	auto addRound = [&](const std::string& name, FeatureKind kind, const Tolerance& tolerance, double wearMin, double wearMax)
	{
		auto feature = MakeFeature(name, kind, tolerance);
		double wear = used ? Random::Uniform(wearMin, wearMax) : 0.0;
		ShapeRound(feature, Sample(tolerance, quality) + wear, std::abs(wear) * 0.3, std::abs(wear) * 0.4 + SmallDeviation(0.004, quality));
		feature.surfaceRa = Random::Uniform(0.2, 0.6);
		part->m_features.push_back(feature);
	};

	switch (type)
	{
		case PartType::OIL_PUMP:
			// Gerotor clearances, checked with the cover off: inner rotor tip to outer rotor, and
			// rotor face to the housing. Worn pumps leak oil back to their own inlet.
			addLength(FeatureNames::ROTOR_TIP_CLEARANCE, FeatureKind::THICKNESS, Tolerance::Limits(0.04, 0.16), 0.06, 0.18);
			addLength(FeatureNames::ROTOR_SIDE_CLEARANCE, FeatureKind::THICKNESS, Tolerance::Limits(0.03, 0.09), 0.03, 0.07);
			break;
		case PartType::OIL_PAN:
			addFlatness(FeatureNames::RAIL_FLATNESS, 0.10, 0.40, false);
			break;
		case PartType::LIFTER:
			addRound(FeatureNames::BUCKET_DIAMETER, FeatureKind::JOURNAL, Tolerance::Bilateral(30.97, 0.008), -0.035, -0.006);
			break;
		case PartType::CRANK_SPROCKET:
			addRound(FeatureNames::ROOT_DIAMETER, FeatureKind::JOURNAL, Tolerance::Bilateral(38.60, 0.05), -0.30, -0.03);
			break;
		case PartType::CAM_SPROCKET:
			addRound(FeatureNames::ROOT_DIAMETER, FeatureKind::JOURNAL, Tolerance::Bilateral(79.80, 0.06), -0.35, -0.03);
			break;
		case PartType::TIMING_BELT:
			// The teeth wear down with the miles: measure one's height off the back of the belt.
			addLength(FeatureNames::BELT_TOOTH_HEIGHT, FeatureKind::THICKNESS, Tolerance::Limits(1.85, 2.00), -0.45, -0.05);
			break;
		case PartType::BELT_IDLER:
			// Rocked on its bearing: a worn one has play.
			addLength(FeatureNames::IDLER_PLAY, FeatureKind::RUNOUT, Tolerance::Limits(0.00, 0.05), 0.02, 0.25);
			break;
		case PartType::BELT_TENSIONER:
			addLength(FeatureNames::TENSIONER_TRAVEL, FeatureKind::LENGTH, Tolerance::Limits(4.0, 11.0), 1.5, 9.0);
			break;
		case PartType::TIMING_COVER:
			addFlatness(FeatureNames::FACE_FLATNESS, 0.08, 0.15, true);
			break;
		case PartType::CRANK_PULLEY:
			addRunout(FeatureNames::RING_RUNOUT, 0.25, 0.90);
			break;
		case PartType::VALVE_COVER:
			addFlatness(FeatureNames::RAIL_FLATNESS, 0.15, 0.40, true);
			break;
		case PartType::SPARK_PLUG:
			addLength(FeatureNames::ELECTRODE_GAP, FeatureKind::THICKNESS, Tolerance::Limits(0.80, 0.90), 0.20, 0.60);
			break;
		case PartType::INTAKE_MANIFOLD:
			addFlatness(FeatureNames::FLANGE_FLATNESS, 0.10, 0.25, true);
			break;
		case PartType::THROTTLE_BODY:
			// Carbon from the crankcase breather builds up behind the butterfly.
			addRound(FeatureNames::THROTTLE_BORE, FeatureKind::BORE, Tolerance::Bilateral(50.00, 0.05), -0.35, -0.03);
			break;
		case PartType::FUEL_INJECTOR:
			// Fired into a burette for 15 seconds at rail pressure; varnish clogs the old ones.
			addLength(FeatureNames::INJECTOR_FLOW, FeatureKind::VOLUME, Tolerance::Limits(58.5, 61.5), -13.0, -0.5);
			break;
		case PartType::EXHAUST_MANIFOLD:
			addFlatness(FeatureNames::FLANGE_FLATNESS, 0.10, 0.60, true);
			break;
		case PartType::WATER_PUMP:
			addRunout(FeatureNames::SHAFT_PLAY, 0.10, 0.50);
			break;
		default:
			// Seals, gaskets, plugs and the like: nothing to measure, they're simply fitted new.
			break;
	}

	return part;
}

std::vector<std::string> PartFactory::QualityNotes(PartType type, PartQuality quality)
{
	std::vector<std::string> notes;
	// What every part's quality changes: how tightly it's made, and what it costs.
	switch (quality)
	{
		case PartQuality::BUDGET:
			notes.push_back("Made to looser tolerances: sizes scatter about 1.6x wider than OEM, and some land outside spec. Measure before you fit.");
			break;
		case PartQuality::OEM:
			notes.push_back("The factory part: made to hold its tolerance band.");
			break;
		case PartQuality::PERFORMANCE:
			notes.push_back("Made to tighter tolerances: sizes held about 40% closer to the middle of the band than OEM.");
			break;
	}

	// And what it changes for this part in particular.
	bool performance = quality == PartQuality::PERFORMANCE;
	switch (type)
	{
		case PartType::CRANKSHAFT:
			notes.push_back(performance ? "Forged 4340 steel: far stronger than the cast nodular iron crank." : "Cast nodular iron.");
			break;
		case PartType::CONNECTING_ROD:
			notes.push_back(performance ? "Forged 4340 steel: stronger than powder-metal rods." : "Powder-metal (sintered) steel.");
			break;
		case PartType::CAMSHAFT:
			notes.push_back(performance ? "Forged steel, ground with about 0.6 mm more lobe lift: more airflow, more power." : "Cast nodular iron, stock lift.");
			break;
		case PartType::PISTON:
			notes.push_back(quality == PartQuality::BUDGET ? "Cast aluminium: the weakest alloy."
				: quality == PartQuality::OEM ? "Hypereutectic (high-silicon) aluminium: stronger, and grows little with heat."
				: "Forged 2618 aluminium: the strongest, but it grows more when hot, so it needs more piston-to-wall clearance.");
			break;
		case PartType::PISTON_RING:
			notes.push_back(performance ? "File-fit: left deliberately tight so you gap each ring to its own bore (ring filer needed)."
				: "Pre-gapped: the end gap lands wherever it lands in your bore.");
			break;
		case PartType::INTAKE_VALVE:
			notes.push_back(performance ? "Forged steel instead of powder metal." : "Powder-metal steel.");
			break;
		case PartType::VALVE_SPRING:
			if (performance)
				notes.push_back("About 15% stiffer: more seat pressure, so the valves don't float at high rpm.");
			break;
		default:
			break;
	}
	return notes;
}
