#include "PartFactory.h"

#include <cmath>
#include <format>
#include <stdexcept>

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
		case PartType::ROD_BEARING:		part = CreateBearing(type, spec, quality, condition, sizeOffset); break;
		case PartType::CYLINDER_HEAD:	part = CreateCylinderHead(spec, quality, condition); break;
		case PartType::HEAD_GASKET:		part = CreateHeadGasket(spec, quality, condition); break;
		case PartType::CAMSHAFT:		part = CreateCamshaft(spec, quality, condition); break;
		case PartType::INTAKE_VALVE:
		case PartType::EXHAUST_VALVE:	part = CreateValve(type == PartType::EXHAUST_VALVE, spec, quality, condition, sizeOffset); break;
		case PartType::VALVE_SPRING:	part = CreateValveSpring(spec, quality, condition); break;
	}

	if (!part)
		throw std::runtime_error("Unknown part type!");

	part->m_sizeOffset = sizeOffset;
	part->m_value = Price(type, quality, condition);
	return part;
}

double PartFactory::Price(PartType type, PartQuality quality, PartCondition condition)
{
	double base = 0.0;
	switch (type)
	{
		case PartType::BLOCK:			base = 900.0; break;
		case PartType::CRANKSHAFT:		base = 600.0; break;
		case PartType::CONNECTING_ROD:	base = 70.0; break;
		case PartType::PISTON:			base = 70.0; break;
		case PartType::PISTON_RING:		base = 15.0; break;
		case PartType::MAIN_BEARING:	base = 12.0; break;
		case PartType::ROD_BEARING:		base = 10.0; break;
		case PartType::CYLINDER_HEAD:	base = 800.0; break;
		case PartType::HEAD_GASKET:		base = 45.0; break;
		case PartType::CAMSHAFT:		base = 320.0; break;
		case PartType::INTAKE_VALVE:	base = 22.0; break;
		case PartType::EXHAUST_VALVE:	base = 26.0; break;
		case PartType::VALVE_SPRING:	base = 9.0; break;
	}

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
	auto shellSpec = (type == PartType::MAIN_BEARING ? spec.mainShell : spec.rodShell).Shifted(-sizeOffset * 0.5);
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
