#include "Dyno.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "PartCatalog.h"
#include "Random.h"

void Dyno::ScoreBore(Part& block, const std::string& boreName)
{
	if (auto bore = block.FindFeature(boreName))
	{
		for (auto& station : bore->samples)
			station[0] += Random::Uniform(0.005, 0.020);
		bore->surfaceRa = Random::Uniform(2.5, 4.0);
		block.MarkReadingStale(boreName);
	}
}

void Dyno::ScoreJournal(Part& crankshaft, const std::string& journalName)
{
	if (auto journal = crankshaft.FindFeature(journalName))
	{
		for (auto& station : journal->samples)
			for (double& sample : station)
				sample -= Random::Uniform(0.005, 0.015);
		journal->surfaceRa = Random::Uniform(1.5, 2.5);
		crankshaft.MarkReadingStale(journalName);
	}
}

double Dyno::ThermalEfficiency(double compressionRatio)
{
	// Air-standard Otto cycle with a realistic ratio of specific heats for a fuel-air mix.
	return 1.0 - std::pow(compressionRatio, -0.3);
}

DynoResult Dyno::Run(EngineAssembly& assembly)
{
	using namespace OperatingTemperature;

	DynoResult result;
	if (!assembly.IsComplete())
	{
		result.findings.push_back({ FindingSeverity::FAILURE, "The engine isn't fully assembled." });
		return result;
	}
	result.ran = true;

	const auto& spec = assembly.GetSpec();
	auto block = assembly.Get(AssemblySlot::BLOCK, 0);
	auto crankshaft = assembly.Get(AssemblySlot::CRANKSHAFT, 0);
	auto head = assembly.Get(AssemblySlot::CYLINDER_HEAD, 0);
	auto gasket = assembly.Get(AssemblySlot::HEAD_GASKET, 0);
	double blockExpansion = GetMaterial(block->GetMaterial()).expansion;

	double health = 100.0;
	double sealingLoss = 0.0;
	double friction = 0.12;

	auto report = [&](FindingSeverity severity, const std::string& message, double healthPenalty)
	{
		result.findings.push_back({ severity, message });
		health -= healthPenalty;
		if (severity == FindingSeverity::FAILURE)
			result.failed = true;
	};

	// Head sealing
	auto blockDeck = block->FindFeature(FeatureNames::DECK_FLATNESS);
	auto headDeck = head->FindFeature(FeatureNames::DECK_FLATNESS);
	double worstWarp = std::max(blockDeck->value / spec.maxDeckFlatness, headDeck->value / spec.maxHeadFlatness);
	if (worstWarp > 3.0)
	{
		report(FindingSeverity::FAILURE, "Blew the head gasket: the decks are nowhere near flat.", 40.0);
		gasket->MarkDamaged("Blown between cylinders");
	}
	else if (worstWarp > 1.0)
	{
		report(FindingSeverity::WARNING, "Head gasket is weeping: a deck surface is out of flat.", 20.0);
		sealingLoss += 0.03 * spec.cylinders;
	}

	// Cylinders
	double compressionSum = 0.0;
	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
	{
		auto piston = assembly.Get(AssemblySlot::PISTON, cylinder);
		auto ring = assembly.Get(AssemblySlot::PISTON_RING, cylinder);
		auto boreName = FeatureNames::Bore(cylinder);
		const Feature* bore = block->FindFeature(boreName);
		const Feature* skirt = piston->FindFeature(FeatureNames::SKIRT);
		int number = cylinder + 1;

		double pistonToWall = *assembly.PistonToWall(cylinder);
		Range pistonTarget = spec.PistonToWallFor(piston->GetMaterial());
		double ringGap = *assembly.RingGap(cylinder);
		double quench = *assembly.Quench(cylinder);
		double compression = *assembly.CompressionRatio(cylinder);
		compressionSum += compression;

		double pistonExpansion = GetMaterial(piston->GetMaterial()).expansion * PISTON_SKIRT_RISE * SKIRT_DESIGN_FACTOR;
		double hotPistonToWall = bore->MinSample() * (1.0 + blockExpansion * BORE_RISE) - skirt->Size() * (1.0 + pistonExpansion);

		double ringExpansion = GetMaterial(ring->GetMaterial()).expansion * TOP_RING_RISE;
		double hotRingGap = ringGap - std::numbers::pi * bore->MinSample() * (ringExpansion - blockExpansion * BORE_TOP_RISE);

		if (hotPistonToWall <= 0.0)
		{
			report(FindingSeverity::FAILURE, std::format("Piston {} seized: hot, the skirt grew bigger than the bore.", number), 40.0);
			piston->MarkDamaged("Seized and scuffed skirt");
			ScoreBore(*block, boreName);
		}
		else if (pistonToWall < pistonTarget.min)
		{
			report(FindingSeverity::WARNING, std::format("Piston {} is tight in its bore. Expect skirt scuffing.", number), 8.0);
			friction += (pistonTarget.min - pistonToWall) / pistonTarget.min * 0.06;
		}
		else if (pistonToWall > 0.15)
		{
			report(FindingSeverity::FAILURE, std::format("Piston {} rocked in its bore until the skirt cracked.", number), 40.0);
			piston->MarkDamaged("Cracked skirt from piston slap");
		}
		else if (pistonToWall > pistonTarget.max)
		{
			report(FindingSeverity::WARNING, std::format("Piston {} is loose: piston slap and blow-by.", number), 5.0);
			sealingLoss += (pistonToWall - pistonTarget.max) * 0.5;
		}

		if (hotRingGap <= 0.0)
		{
			report(FindingSeverity::FAILURE, std::format("Ring ends butted in cylinder {} and broke the ring land.", number), 40.0);
			ring->MarkDamaged("Broken: ends butted when hot");
			piston->MarkDamaged("Broken top ring land");
			ScoreBore(*block, boreName);
		}
		else if (ringGap < spec.ringGap.min)
			report(FindingSeverity::WARNING, std::format("Cylinder {} ring gap is tight. It survived this pull, but it won't take much heat.", number), 5.0);
		else if (ringGap > spec.ringGap.max)
		{
			report(FindingSeverity::WARNING, std::format("Cylinder {} ring gap is wide: blow-by.", number), 3.0);
			sealingLoss += (ringGap - spec.ringGap.max) * 0.15;
		}

		uint32_t boreIssues = bore->Evaluate();
		if (boreIssues & FeatureIssue::TAPER)
		{
			report(FindingSeverity::WARNING, std::format("Bore {} is tapered: the ring can't seal through the whole stroke.", number), 4.0);
			sealingLoss += (bore->Taper() - bore->maxTaper) * 1.0;
		}
		if (boreIssues & FeatureIssue::OUT_OF_ROUND)
		{
			report(FindingSeverity::WARNING, std::format("Bore {} is out of round: the ring can't seal all the way around.", number), 4.0);
			sealingLoss += (bore->OutOfRound() - bore->maxOutOfRound) * 1.5;
		}
		if (boreIssues & FeatureIssue::GLAZED)
		{
			report(FindingSeverity::WARNING, std::format("Bore {} is glazed: the rings will never seat.", number), 6.0);
			sealingLoss += 0.02;
		}
		if (boreIssues & FeatureIssue::ROUGH)
		{
			report(FindingSeverity::WARNING, std::format("Bore {} finish is rough: it'll burn oil while the rings wear in.", number), 3.0);
			sealingLoss += 0.01;
		}

		if (quench < 0.5)
		{
			report(FindingSeverity::FAILURE, std::format("Piston {} hit the head at high rpm: only {:.2f} mm of quench.", number, quench), 40.0);
			piston->MarkDamaged("Crown hit the cylinder head");
		}
		else if (quench < spec.quench.min)
			report(FindingSeverity::WARNING, std::format("Cylinder {} quench is tight: rod stretch at redline could close it up.", number), 5.0);
		else if (quench > spec.quench.max)
			report(FindingSeverity::INFO, std::format("Cylinder {} quench is loose: weaker combustion and more knock.", number), 3.0);

		if (compression > 12.5)
		{
			report(FindingSeverity::FAILURE, std::format("Cylinder {} detonated at {:.1f}:1 and holed the piston.", number, compression), 40.0);
			piston->MarkDamaged("Holed by detonation");
		}
		else if (compression > spec.compressionRatio.max)
			report(FindingSeverity::WARNING, std::format("Cylinder {} is knocking at {:.1f}:1 on pump fuel.", number, compression), 10.0);
		else if (compression < spec.compressionRatio.min)
			report(FindingSeverity::INFO, std::format("Cylinder {} compression is low at {:.1f}:1.", number, compression), 3.0);
	}

	// Bearings
	double clearanceSum = 0.0;
	int bearingCount = 0;
	auto checkBearing = [&](const std::string& label, double clearance, const Range& target, const pPart& bearing, const std::string& journalName)
	{
		clearanceSum += clearance;
		bearingCount++;

		if (clearance < 0.010)
		{
			report(FindingSeverity::FAILURE, std::format("Spun the {} bearing: no room for an oil film.", label), 40.0);
			bearing->MarkDamaged("Spun and welded to the journal");
			ScoreJournal(*crankshaft, journalName);
		}
		else if (clearance > 0.110)
		{
			report(FindingSeverity::FAILURE, std::format("The {} bearing knocked itself apart.", label), 40.0);
			bearing->MarkDamaged("Hammered out by excess clearance");
			ScoreJournal(*crankshaft, journalName);
		}
		else if (clearance < target.min)
		{
			report(FindingSeverity::WARNING, std::format("{} bearing clearance is tight: high friction and heat.", label), 6.0);
			friction += (target.min - clearance) / target.min * 0.05;
		}
		else if (clearance > target.max)
			report(FindingSeverity::WARNING, std::format("{} bearing clearance is loose: low oil pressure.", label), 5.0);

		uint32_t journalIssues = crankshaft->FindFeature(journalName)->Evaluate();
		if (journalIssues & FeatureIssue::ROUGH)
		{
			report(FindingSeverity::WARNING, std::format("{} journal is rough and will wipe its bearing.", label), 5.0);
			friction += 0.01;
		}
		if (journalIssues & (FeatureIssue::TAPER | FeatureIssue::OUT_OF_ROUND))
			report(FindingSeverity::WARNING, std::format("{} journal isn't round and straight: uneven bearing load.", label), 4.0);
	};

	for (int journal = 0; journal < spec.mainJournals; journal++)
	{
		auto label = std::format("Main {}", journal + 1);
		checkBearing(label, *assembly.MainClearance(journal), spec.mainClearance, assembly.Get(AssemblySlot::MAIN_BEARING, journal), FeatureNames::MainJournal(journal));

		if (block->FindFeature(FeatureNames::MainHousing(journal))->Evaluate(false) != FeatureIssue::NONE)
			report(FindingSeverity::WARNING, std::format("Main housing {} is distorted: the block needs line boring.", journal + 1), 4.0);
	}

	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
	{
		auto label = std::format("Rod {}", cylinder + 1);
		checkBearing(label, *assembly.RodClearance(cylinder), spec.rodClearance, assembly.Get(AssemblySlot::ROD_BEARING, cylinder), FeatureNames::RodJournal(cylinder));

		if (assembly.Get(AssemblySlot::CONNECTING_ROD, cylinder)->FindFeature(FeatureNames::BIG_END)->Evaluate(false) != FeatureIssue::NONE)
			report(FindingSeverity::WARNING, std::format("Rod {} big end is oval: the bearing crush is uneven.", cylinder + 1), 4.0);
	}

	// Valvetrain
	auto camshaft = assembly.Get(AssemblySlot::CAMSHAFT, 0);
	for (int journal = 0; journal < spec.camJournals; journal++)
	{
		double clearance = *assembly.CamClearance(journal);
		if (clearance < 0.010)
		{
			report(FindingSeverity::FAILURE, std::format("Cam journal {} seized in the head.", journal + 1), 40.0);
			camshaft->MarkDamaged("Seized in its bores");
			if (auto bore = head->FindFeature(FeatureNames::CamBore(journal)))
			{
				bore->surfaceRa = Random::Uniform(2.0, 3.0);
				head->MarkReadingStale(bore->name);
			}
		}
		else if (clearance < spec.camClearance.min)
		{
			report(FindingSeverity::WARNING, std::format("Cam journal {} is tight in its bore.", journal + 1), 5.0);
			friction += 0.01;
		}
		else if (clearance > spec.camClearance.max)
			report(FindingSeverity::WARNING, std::format("Cam journal {} is loose: it'll bleed off oil pressure.", journal + 1), 3.0);
	}

	double liftSum = 0.0;
	double floatRpm = spec.redline * 2.0;
	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
		for (bool exhaust : { false, true })
		{
			auto valve = assembly.Get(exhaust ? AssemblySlot::EXHAUST_VALVE : AssemblySlot::INTAKE_VALVE, cylinder);
			auto piston = assembly.Get(AssemblySlot::PISTON, cylinder);
			std::string label = std::format("{} valve {}", FeatureNames::ValveSide(exhaust), cylinder + 1);

			double stemClearance = *assembly.StemClearance(cylinder, exhaust);
			Range stemTarget = exhaust ? spec.exhaustStemClearance : spec.intakeStemClearance;
			if (stemClearance < 0.008)
			{
				report(FindingSeverity::FAILURE, std::format("{} stuck in its guide and the piston hit it.", label), 40.0);
				valve->MarkDamaged("Bent by the piston");
				piston->MarkDamaged("Valve strike on the crown");
			}
			else if (stemClearance < stemTarget.min)
				report(FindingSeverity::WARNING, std::format("{} is tight in its guide: it'll stick once it's hot.", label), 6.0);
			else if (stemClearance > stemTarget.max)
			{
				report(FindingSeverity::WARNING, std::format("{} guide is worn loose: it'll smoke on overrun.", label), 3.0);
				sealingLoss += 0.005;
			}

			const Feature* seatWidth = head->FindFeature(FeatureNames::SeatWidth(cylinder, exhaust));
			const Feature* seatRunout = head->FindFeature(FeatureNames::SeatRunout(cylinder, exhaust));
			const Feature* faceRunout = valve->FindFeature(FeatureNames::FACE_RUNOUT);
			const Feature* margin = valve->FindFeature(FeatureNames::MARGIN);

			if (seatRunout->value > spec.maxSeatRunout || faceRunout->value > spec.maxValveRunout)
			{
				report(FindingSeverity::WARNING, std::format("{} doesn't seal on its seat: compression is leaking past it.", label), 6.0);
				sealingLoss += 0.04;
			}
			if (seatWidth->value < spec.seatWidth.min)
				report(FindingSeverity::WARNING, std::format("{} seat is narrow: the valve can't shed its heat.", label), 3.0);
			else if (seatWidth->value > spec.seatWidth.max)
			{
				report(FindingSeverity::WARNING, std::format("{} seat is wide: it'll trap carbon and lose seal.", label), 2.0);
				sealingLoss += 0.01;
			}

			if (margin->value < spec.minValveMargin)
			{
				report(FindingSeverity::FAILURE, std::format("Burned the {}: its margin was knife-thin.", label), 40.0);
				valve->MarkDamaged("Burned through at the margin");
			}

			double installedHeight = *assembly.InstalledHeight(cylinder, exhaust);
			double seatPressure = *assembly.SeatPressure(cylinder, exhaust);
			if (seatPressure < 150.0)
			{
				report(FindingSeverity::FAILURE, std::format("{} floated at high rpm and met the piston.", label), 40.0);
				valve->MarkDamaged("Bent by valve float");
				piston->MarkDamaged("Valve strike on the crown");
			}
			else if (seatPressure < spec.seatPressure.min)
			{
				// A weak spring can't keep the valve on the cam above a certain speed.
				double rpm = spec.redline * seatPressure / spec.seatPressure.min;
				floatRpm = (std::min)(floatRpm, rpm);
				report(FindingSeverity::WARNING, std::format("{} spring is weak ({:.0f} N): it floats above {:.0f} rpm.", label, seatPressure, rpm), 5.0);
			}
			else if (seatPressure > spec.seatPressure.max + 60.0)
			{
				report(FindingSeverity::WARNING, std::format("{} spring pressure is very high: it'll wipe the cam lobe.", label), 4.0);
				friction += 0.005;
			}
			if (!spec.installedHeightRange.Contains(installedHeight))
				report(FindingSeverity::INFO, std::format("{} installed height is {:.2f} mm: outside spec, it wants shimming.", label, installedHeight), 1.0);

			double lift = camshaft->FindFeature(FeatureNames::LobeLift(cylinder, exhaust))->value;
			liftSum += lift;
			if (lift < spec.lobeLift.Min() - 0.3)
				report(FindingSeverity::WARNING, std::format("{} cam lobe is worn flat.", label), 6.0);
		}

	// Lift is airflow: a worn cam starves the engine, a hotter one feeds it.
	double liftFactor = std::clamp(liftSum / (spec.cylinders * 2) / spec.lobeLift.nominal, 0.6, 1.1);

	// Everything bolted around the long block.
	double powerFactor = 1.0;
	double pumpFactor = 1.0;
	auto value = [&](AssemblySlot slot, int index, const std::string& name) {
		auto part = assembly.Get(slot, index);
		const Feature* feature = part ? part->FindFeature(name) : nullptr;
		return feature ? feature->value : 0.0;
	};
	auto overLimit = [&](AssemblySlot slot, int index, const std::string& name) {
		auto part = assembly.Get(slot, index);
		const Feature* feature = part ? part->FindFeature(name) : nullptr;
		return feature && feature->value > feature->spec.Max();
	};

	// Oil pump: worn rotors leak oil back to their own inlet.
	double tipWear = std::max(0.0, value(AssemblySlot::OIL_PUMP, 0, FeatureNames::ROTOR_TIP_CLEARANCE) - 0.16) / 0.10;
	double sideWear = std::max(0.0, value(AssemblySlot::OIL_PUMP, 0, FeatureNames::ROTOR_SIDE_CLEARANCE) - 0.09) / 0.05;
	if (tipWear + sideWear > 0.0)
	{
		pumpFactor = std::clamp(1.0 - 0.3 * (tipWear + sideWear), 0.35, 1.0);
		report(FindingSeverity::WARNING, "The oil pump's rotors are worn: it can't hold pressure when hot.", 6.0);
	}

	// Timing drive: worn belt teeth let the cam fall behind the crank. Worn sprocket teeth make it
	// worse, and far enough gone the belt skips a tooth and the valves meet the pistons.
	double toothHeight = value(AssemblySlot::TIMING_BELT, 0, FeatureNames::BELT_TOOTH_HEIGHT);
	double crankRoot = value(AssemblySlot::CRANK_SPROCKET, 0, FeatureNames::ROOT_DIAMETER);
	double camRoot = value(AssemblySlot::CAM_SPROCKET, 0, FeatureNames::ROOT_DIAMETER);
	double effectiveStretch = std::max(0.0, 1.85 - toothHeight) * 2.0 + std::max(0.0, 38.45 - crankRoot) + std::max(0.0, 79.65 - camRoot) * 0.5;
	if (effectiveStretch > 0.9)
	{
		report(FindingSeverity::FAILURE, "The timing belt skipped a tooth: the cam fell behind and the valves hit the pistons.", 40.0);
		assembly.Get(AssemblySlot::TIMING_BELT, 0)->MarkDamaged("Skipped a tooth");
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
		{
			assembly.Get(AssemblySlot::INTAKE_VALVE, cylinder)->MarkDamaged("Bent by the piston");
			assembly.Get(AssemblySlot::PISTON, cylinder)->MarkDamaged("Valve strike on the crown");
		}
	}
	else if (effectiveStretch > 0.05)
	{
		// Roughly 7 crank degrees of cam retard per millimetre of worn play in the drive.
		double retard = effectiveStretch * 7.0;
		powerFactor -= retard * 0.008;
		report(FindingSeverity::WARNING, std::format("The timing belt's teeth are worn: the cam runs {:.0f} degrees retarded and power is down.", retard), 5.0);
	}
	for (int idler = 0; idler < assembly.SlotCount(AssemblySlot::BELT_IDLER); idler++)
		if (overLimit(AssemblySlot::BELT_IDLER, idler, FeatureNames::IDLER_PLAY))
			report(FindingSeverity::WARNING, std::format("Belt idler {}'s bearing is worn: it growls, and the belt wanders on it.", idler + 1), 3.0);
	if (overLimit(AssemblySlot::BELT_TENSIONER, 0, FeatureNames::TENSIONER_TRAVEL))
		report(FindingSeverity::WARNING, "The belt tensioner is near the end of its travel: it can't take up any more slack.", 3.0);

	// Clamping: every nut and bolt, by how tight it really is against its spec.
	for (int slotIndex = 0; slotIndex < static_cast<int>(AssemblySlot::COUNT); slotIndex++)
	{
		auto slot = static_cast<AssemblySlot>(slotIndex);
		const auto* holes = EngineAssembly::Holes(slot);
		if (!holes)
			continue;
		double loosest = 10.0, tightest = 0.0;
		int loosestIndex = 0, tightestIndex = 0;
		for (int index = 0; index < assembly.SlotCount(slot); index++)
		{
			auto bolt = assembly.Get(slot, index);
			if (!bolt)
				continue;
			double ratio = bolt->GetTorque() / holes->torqueNm;
			if (ratio < loosest) { loosest = ratio; loosestIndex = index; }
			if (ratio > tightest) { tightest = ratio; tightestIndex = index; }
		}
		std::string loosestAt = assembly.PositionName(slot, loosestIndex);
		bool critical = slot == AssemblySlot::HEAD_BOLT || slot == AssemblySlot::MAIN_CAP_BOLT || slot == AssemblySlot::ROD_NUT;

		// Too tight: a bolt yields and stretches, or snaps.
		if (tightest > 1.4)
		{
			if (slot == AssemblySlot::HEAD_BOLT)
			{
				report(FindingSeverity::FAILURE, "A head bolt was done up far too tight and snapped: the head lifted and blew the gasket.", 40.0);
				gasket->MarkDamaged("Blown when a head bolt snapped");
			}
			else
				report(FindingSeverity::WARNING, std::format("The {} was done up far too tight and snapped.", assembly.PositionName(slot, tightestIndex)), critical ? 15.0 : 3.0);
			assembly.Get(slot, tightestIndex)->MarkDamaged("Snapped from over-tightening");
		}
		else if (tightest > 1.15)
			report(FindingSeverity::WARNING, std::format("The {} is over-torqued: it's stretched, and the threads may be pulled.", assembly.PositionName(slot, tightestIndex)), 2.0);

		// Too loose: what it holds moves.
		if (slot == AssemblySlot::HEAD_BOLT && loosest < 0.6)
		{
			report(FindingSeverity::FAILURE, std::format("The head isn't clamped ({} was barely done up): the gasket blew between the cylinders.", loosestAt), 40.0);
			gasket->MarkDamaged("Blown: the head wasn't clamped down");
		}
		else if (slot == AssemblySlot::HEAD_BOLT && loosest < 0.85)
		{
			report(FindingSeverity::WARNING, "The head bolts are uneven: the gasket weeps.", 8.0);
			sealingLoss += 0.02 * spec.cylinders;
		}
		else if (slot == AssemblySlot::MAIN_CAP_BOLT && loosest < 0.6)
		{
			int journal = loosestIndex / holes->perParent;
			report(FindingSeverity::FAILURE, std::format("Main cap {} walked on its loose bolts and spun its bearing.", journal + 1), 40.0);
			if (auto bearing = assembly.Get(AssemblySlot::MAIN_BEARING, journal))
				bearing->MarkDamaged("Spun when its cap came loose");
			ScoreJournal(*crankshaft, FeatureNames::MainJournal(journal));
		}
		else if (slot == AssemblySlot::ROD_NUT && loosest < 0.7)
		{
			int cylinder = loosestIndex / holes->perParent;
			report(FindingSeverity::FAILURE, std::format("Rod {}'s cap came off its loose nuts: the rod went through the side of the block.", cylinder + 1), 60.0);
			assembly.Get(AssemblySlot::CONNECTING_ROD, cylinder)->MarkDamaged("Broken when its cap came loose");
			block->MarkDamaged("Holed by a thrown rod");
		}
		else if (critical && loosest < 0.85)
			report(FindingSeverity::WARNING, std::format("The {} is under-torqued: the cap can shift under load.", loosestAt), 6.0);
		else if (!critical && loosest < 0.8)
			report(FindingSeverity::WARNING, std::format("The {} is loose: {}.", loosestAt,
				slot == AssemblySlot::INTAKE_MANIFOLD_NUT ? "unmetered air is leaning the mixture" : slot == AssemblySlot::EXHAUST_MANIFOLD_NUT ? "it ticks and leaks"
				: "it leaks"), 2.0);
	}

	for (int valve = 0; valve < assembly.SlotCount(AssemblySlot::LIFTER); valve++)
	{
		auto lifter = assembly.Get(AssemblySlot::LIFTER, valve);
		if (lifter && lifter->FindFeature(FeatureNames::BUCKET_DIAMETER)->Evaluate(false) != FeatureIssue::NONE)
		{
			report(FindingSeverity::INFO, "A worn bucket lifter is loose in its bore and ticks.", 1.0);
			break;
		}
	}

	// Fuel and spark, cylinder by cylinder. A leaking intake flange pulls in unmetered air,
	// leaning every cylinder.
	double vacuumLeak = 0.0;
	if (overLimit(AssemblySlot::INTAKE_MANIFOLD, 0, FeatureNames::FLANGE_FLATNESS))
	{
		vacuumLeak = 0.05;
		report(FindingSeverity::WARNING, "The intake manifold flange is warped: a vacuum leak is leaning the engine out.", 5.0);
	}
	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
	{
		int number = cylinder + 1;
		double flow = value(AssemblySlot::FUEL_INJECTOR, cylinder, FeatureNames::INJECTOR_FLOW);
		double lean = (60.0 - flow) / 60.0 + vacuumLeak;
		if (lean > 0.16)
		{
			report(FindingSeverity::FAILURE, std::format("Cylinder {} ran so lean it melted a hole through the piston.", number), 40.0);
			assembly.Get(AssemblySlot::PISTON, cylinder)->MarkDamaged("Melted by a lean mixture");
		}
		else if (lean > 0.06)
		{
			report(FindingSeverity::WARNING, std::format("Cylinder {} is running lean: its injector is clogged. It's knocking and hot.", number), 6.0);
			powerFactor -= 0.02;
		}
		else if (lean < -0.05)
			report(FindingSeverity::INFO, std::format("Cylinder {}'s injector is over-fuelling it: rich and sooty.", number), 1.0);

		double gap = value(AssemblySlot::SPARK_PLUG, cylinder, FeatureNames::ELECTRODE_GAP);
		if (gap > 1.1)
		{
			report(FindingSeverity::WARNING, std::format("Cylinder {} misfires under load: its spark plug gap is worn to {:.2f} mm.", number, gap), 5.0);
			powerFactor -= 0.6 / spec.cylinders * std::min(1.0, (gap - 1.1) / 0.3 + 0.3);
		}
		else if (gap < 0.7)
			report(FindingSeverity::INFO, std::format("Cylinder {}'s plug gap is tight: a weak spark at idle.", number), 1.0);
	}
	auto throttle = assembly.Get(AssemblySlot::THROTTLE_BODY, 0);
	if (throttle && value(AssemblySlot::THROTTLE_BODY, 0, FeatureNames::THROTTLE_BORE) < 49.85)
		report(FindingSeverity::INFO, "The throttle bore is caked with carbon: the idle hunts.", 1.0);

	if (overLimit(AssemblySlot::EXHAUST_MANIFOLD, 0, FeatureNames::FLANGE_FLATNESS))
	{
		report(FindingSeverity::WARNING, "The exhaust manifold is warped: it ticks and leaks at the flange.", 3.0);
		powerFactor -= 0.01;
	}

	// Oil and coolant leaks.
	if (overLimit(AssemblySlot::OIL_PAN, 0, FeatureNames::RAIL_FLATNESS))
		report(FindingSeverity::WARNING, "The oil pan rail is bent: it leaks oil onto the dyno floor.", 3.0);
	if (overLimit(AssemblySlot::TIMING_COVER, 0, FeatureNames::FACE_FLATNESS))
		report(FindingSeverity::WARNING, "The timing cover face isn't flat: oil seeps out of the front of the engine.", 3.0);
	if (overLimit(AssemblySlot::VALVE_COVER, 0, FeatureNames::RAIL_FLATNESS))
		report(FindingSeverity::WARNING, "The valve cover rail is bent: oil weeps down the side of the head.", 2.0);
	if (overLimit(AssemblySlot::WATER_PUMP, 0, FeatureNames::SHAFT_PLAY))
		report(FindingSeverity::WARNING, "Coolant is dripping from the water pump's weep hole: its bearing is shot and it runs hot.", 5.0);

	// The harmonic balancer damps crank twist; with its rubber gone it doesn't.
	double damperRunout = value(AssemblySlot::CRANK_PULLEY, 0, FeatureNames::RING_RUNOUT);
	if (damperRunout > 0.6)
	{
		report(FindingSeverity::WARNING, "The crank pulley's outer ring has walked off its rubber: it wobbles and threw the belt.", 6.0);
		friction += 0.01;
	}
	else if (damperRunout > 0.25)
		report(FindingSeverity::WARNING, "The crank pulley's damper rubber is tired: crank vibration near redline.", 3.0);

	powerFactor = std::clamp(powerFactor, 0.4, 1.0);

	// Oil pressure rises steeply as bearing clearance closes up, until the relief valve opens.
	double averageClearance = std::max(0.005, clearanceSum / bearingCount);
	result.oilPressureBar = std::min(5.5, 4.2 * std::pow(0.042 / averageClearance, 1.5)) * pumpFactor;
	if (result.oilPressureBar < 1.5)
		report(FindingSeverity::WARNING, std::format("Oil pressure is only {:.1f} bar when hot.", result.oilPressureBar), 10.0);

	result.compressionRatio = compressionSum / spec.cylinders;
	result.displacementCc = assembly.Displacement();

	double sealing = std::clamp(0.01 + sealingLoss / spec.cylinders, 0.0, 0.6);
	result.blowbyPercent = sealing * 100.0;
	friction = std::min(friction, 0.5);

	double bmepBar = 12.5 * ThermalEfficiency(result.compressionRatio) / ThermalEfficiency(10.0) * (1.0 - sealing);
	double torqueScale = bmepBar * 1e5 * (result.displacementCc * 1e-6) / (4.0 * std::numbers::pi);

	if (result.failed)
		result.failureRpm = static_cast<float>(Random::Uniform(3000.0, spec.redline));

	for (int rpm = 1000; rpm <= spec.redline; rpm += 250)
	{
		if (result.failed && rpm > result.failureRpm)
			break;

		double rpmOffset = (rpm - 4800.0) / 3800.0;
		double volumetricEfficiency = std::clamp(0.78 + 0.22 * (1.0 - rpmOffset * rpmOffset), 0.55, 1.0) * liftFactor;
		// Past the float point the valves stop following the cam and power falls away.
		if (rpm > floatRpm)
			volumetricEfficiency *= (std::max)(0.3, 1.0 - (rpm - floatRpm) / 1500.0);
		double rpmFriction = friction + 0.03 * std::pow(static_cast<double>(rpm) / spec.redline, 2.0);

		double torque = torqueScale * volumetricEfficiency * (1.0 - rpmFriction) * powerFactor;
		double power = torque * rpm * 2.0 * std::numbers::pi / 60.0 / 1000.0;
		result.curve.push_back({ static_cast<float>(rpm), static_cast<float>(torque), static_cast<float>(power) });

		if (torque > result.peakTorqueNm)
		{
			result.peakTorqueNm = torque;
			result.peakTorqueRpm = rpm;
		}
		if (power > result.peakPowerKw)
		{
			result.peakPowerKw = power;
			result.peakPowerRpm = rpm;
		}
	}

	result.healthScore = std::clamp(health, 0.0, 100.0);
	if (result.findings.empty())
		result.findings.push_back({ FindingSeverity::INFO, "Clean pull. Every clearance in spec." });

	return result;
}
