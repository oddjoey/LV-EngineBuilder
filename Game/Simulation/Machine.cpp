#include "Machine.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "Random.h"

// Most of a machine's error is in where it lands the size. The shape it cuts is far more
// consistent, so roundness and taper errors are only a fraction of the size error.
static constexpr double GEOMETRY_ERROR = 0.3;

namespace MachineCatalog
{
	const std::vector<Machine>& All()
	{
		static const std::vector<Machine> machines = [] {
			std::vector<Machine> list;

			Machine ringFiler;
			ringFiler.name = "Hand Ring Filer";
			ringFiler.description = "Crank-driven grinding wheel for opening up ring end gaps.";
			ringFiler.operation = MachineOperation::FILE_RING;
			ringFiler.sigma = 0.008;
			ringFiler.maxRemoval = 0.50;
			ringFiler.price = 150.0;
			ringFiler.hoursPerUse = 0.15;
			list.push_back(ringFiler);

			Machine manualHone;
			manualHone.name = "Drill-Driven Cylinder Hone";
			manualHone.description = "Rigid stone hone on a drill. Cheap, but geometry depends on your hands.";
			manualHone.operation = MachineOperation::HONE;
			manualHone.sigma = 0.003;
			manualHone.finishRa = 0.45;
			// Honing is slow: there's no limit on stock, but every hundredth costs time.
			manualHone.maxRemoval = 0.0;
			manualHone.price = 300.0;
			manualHone.hoursPerUse = 0.3;
			manualHone.hoursPerMm = 8.0;
			list.push_back(manualHone);

			Machine powerHone;
			powerHone.name = "Torque-Plate Power Hone";
			powerHone.description = "Hones with a torque plate bolted on so the bore is shaped as it will be when assembled.";
			powerHone.operation = MachineOperation::HONE;
			powerHone.sigma = 0.0012;
			powerHone.finishRa = 0.35;
			powerHone.maxRemoval = 0.0;
			powerHone.price = 4500.0;
			powerHone.hoursPerUse = 0.2;
			powerHone.hoursPerMm = 3.0; 
			list.push_back(powerHone);

			Machine boringBar;
			boringBar.name = "Portable Boring Bar";
			boringBar.description = "Takes bores oversize in one pass. Leaves a rough finish: always hone afterwards.";
			boringBar.operation = MachineOperation::BORE;
			boringBar.sigma = 0.008;
			boringBar.finishRa = 2.5;
			boringBar.maxRemoval = 1.50;
			boringBar.price = 3500.0;
			boringBar.hoursPerUse = 0.75;
			list.push_back(boringBar);

			Machine crankGrinder;
			crankGrinder.name = "Crankshaft Grinder";
			crankGrinder.description = "Regrinds journals to an undersize. You will need matching undersize bearings.";
			crankGrinder.operation = MachineOperation::GRIND;
			crankGrinder.sigma = 0.003;
			crankGrinder.finishRa = 0.30;
			crankGrinder.maxRemoval = 0.80;
			crankGrinder.price = 12000.0;
			crankGrinder.hoursPerUse = 0.6;
			list.push_back(crankGrinder);

			Machine polisher;
			polisher.name = "Belt Journal Polisher";
			polisher.description = "Takes a few microns off and leaves the journal mirror-smooth.";
			polisher.operation = MachineOperation::POLISH;
			polisher.sigma = 0.0008;
			polisher.finishRa = 0.12;
			polisher.maxRemoval = 0.004;
			polisher.price = 600.0;
			polisher.hoursPerUse = 0.2;
			list.push_back(polisher);

			Machine surfaceGrinder;
			surfaceGrinder.name = "Surface Grinder";
			surfaceGrinder.description = "Decks blocks and heads flat. Every cut changes deck height or chamber volume.";
			surfaceGrinder.operation = MachineOperation::SURFACE;
			surfaceGrinder.sigma = 0.004;
			surfaceGrinder.finishRa = 0.8;
			surfaceGrinder.maxRemoval = 0.50;
			surfaceGrinder.price = 6000.0;
			surfaceGrinder.hoursPerUse = 1.0;
			list.push_back(surfaceGrinder);

			Machine reamer;
			reamer.name = "Valve Guide Reamer Set";
			reamer.description = "Reams worn guides straight and round to suit oversize-stem valves.";
			reamer.operation = MachineOperation::REAM;
			reamer.sigma = 0.002;
			reamer.finishRa = 0.6;
			reamer.maxRemoval = 0.45;
			reamer.price = 180.0;
			reamer.hoursPerUse = 0.2;
			list.push_back(reamer);

			Machine seatCutter;
			seatCutter.name = "Valve Seat Cutter";
			seatCutter.description = "Three-angle carbide cutters. Trues the seat, but every cut sinks it a little deeper.";
			seatCutter.operation = MachineOperation::CUT_SEAT;
			seatCutter.sigma = 0.05;
			seatCutter.maxRemoval = 0.8;
			seatCutter.price = 900.0;
			seatCutter.hoursPerUse = 0.25;
			list.push_back(seatCutter);

			Machine refacer;
			refacer.name = "Valve Refacer";
			refacer.description = "Regrinds valve faces true. Eats margin, and the valve sits deeper afterwards.";
			refacer.operation = MachineOperation::FACE_VALVE;
			refacer.sigma = 0.003;
			refacer.maxRemoval = 0.3;
			refacer.price = 1400.0;
			refacer.hoursPerUse = 0.15;
			list.push_back(refacer);

			// Added later, so saved machine lists keep their order.
			Machine lineBore;
			lineBore.name = "Line Boring Machine";
			lineBore.description = "Grinds the main caps' parting faces, bolts them back on and bores all five main housings back to "
				"size, round and in line, in one setup. The fix for a distorted or misaligned main saddle.";
			lineBore.operation = MachineOperation::LINE_BORE;
			lineBore.sigma = 0.002;
			lineBore.finishRa = 0.8;
			// How far over size a housing can be and still be brought back by grinding its cap.
			lineBore.maxRemoval = 0.05;
			lineBore.price = 9000.0;
			lineBore.hoursPerUse = 2.5;
			list.push_back(lineBore);

			// The budget way to deck, after the line borer for the same reason.
			Machine sandingBeam;
			sandingBeam.name = "Deck Sanding Beam";
			sandingBeam.description = "The budget way to deck: a long, dead-straight hardwood beam faced with 80 then 180 grit, worked "
				"across a block or head deck by hand in long crossing strokes. Gets a warped deck close to flat for next to nothing, but "
				"not as flat as a grinder, with a rougher finish and a lot slower. And you never quite know how much came off: measure "
				"the deck height (or chamber volumes) afterwards.";
			sandingBeam.operation = MachineOperation::SURFACE;
			sandingBeam.sigma = 0.022;
			sandingBeam.finishRa = 1.4;
			sandingBeam.maxRemoval = 0.10;
			sandingBeam.price = 90.0;
			sandingBeam.hoursPerUse = 0.5;
			sandingBeam.hoursPerMm = 25.0;
			sandingBeam.removalError = 0.35;
			list.push_back(sandingBeam);

			return list;
		}();
		return machines;
	}
}

const char* Machining::OperationName(MachineOperation operation)
{
	switch (operation)
	{
		case MachineOperation::BORE:		return "Bore";
		case MachineOperation::HONE:		return "Hone";
		case MachineOperation::GRIND:		return "Grind";
		case MachineOperation::POLISH:		return "Polish";
		case MachineOperation::SURFACE:		return "Surface";
		case MachineOperation::FILE_RING:	return "File Ring";
		case MachineOperation::REAM:		return "Ream";
		case MachineOperation::CUT_SEAT:	return "Cut Seat";
		case MachineOperation::FACE_VALVE:	return "Reface";
		case MachineOperation::LINE_BORE:	return "Line Bore";
	}
	return "Unknown";
}

const char* Machining::ParameterName(MachineOperation operation)
{
	switch (operation)
	{
		case MachineOperation::BORE:
		case MachineOperation::HONE:
		case MachineOperation::REAM:
		case MachineOperation::GRIND:		return "Target diameter";
		case MachineOperation::SURFACE:		return "Cut depth";
		case MachineOperation::FILE_RING:	return "Gap to add";
		case MachineOperation::CUT_SEAT:	return "Target seat width";
		default:							return "";
	}
}

bool Machining::UsesParameter(MachineOperation operation)
{
	return operation != MachineOperation::POLISH && operation != MachineOperation::FACE_VALVE && operation != MachineOperation::LINE_BORE;
}

bool Machining::CanMachine(const Machine& machine, const Feature& feature)
{
	return feature.SupportsOperation(machine.operation);
}

MachiningResult Machining::Apply(const Machine& machine, Part& part, Feature& feature, double parameter, const EngineSpec& spec)
{
	if (part.GetCondition() == PartCondition::DAMAGED)
		return { false, "That part is damaged beyond machining." };
	if (!CanMachine(machine, feature))
		return { false, std::format("A {} can't work on {}.", machine.name, feature.name) };

	// Harder materials deflect the tool more and cut less predictably.
	double sigma = machine.sigma / GetMaterial(part.GetMaterial()).machinability;

	MachiningResult result;
	switch (machine.operation)
	{
		case MachineOperation::BORE:
		case MachineOperation::HONE:
		case MachineOperation::REAM:		result = CutBore(machine, feature, parameter, sigma); break;
		case MachineOperation::GRIND:		result = GrindJournal(machine, feature, parameter, sigma); break;
		case MachineOperation::POLISH:		result = Polish(machine, feature); break;
		case MachineOperation::SURFACE:		result = Surface(machine, part, feature, parameter, sigma, spec); break;
		case MachineOperation::FILE_RING:	result = FileRing(machine, feature, parameter, sigma); break;
		case MachineOperation::CUT_SEAT:	result = CutSeat(machine, part, feature, parameter, sigma); break;
		case MachineOperation::FACE_VALVE:	result = FaceValve(machine, part, feature, sigma); break;
		case MachineOperation::LINE_BORE:	result = LineBore(machine, part, sigma, spec); break;
	}

	if (result.success)
	{
		feature.lastOperation = machine.operation;
		part.MarkReadingStale(feature.name);
	}
	return result;
}

MachiningResult Machining::LineBore(const Machine& machine, Part& part, double sigma, const EngineSpec& spec)
{
	// Every main housing at once: the caps' parting faces are ground down a little so the housings
	// close up undersize, then one long bar bores them all back to the middle of the spec, round,
	// straight and on one axis. It can only bring back a housing that isn't too far oversize to begin
	// with: grinding the cap only closes it up so much.
	std::vector<Feature*> housings;
	for (int journal = 0; journal < spec.mainJournals; journal++)
		if (auto housing = part.FindFeature(FeatureNames::MainHousing(journal)))
			housings.push_back(housing);
	if (housings.empty())
		return { false, "Only a block's main housings can be line bored." };

	for (const auto* housing : housings)
		if (housing->MaxSample() > housing->spec.Max() + machine.maxRemoval)
			return { false, std::format("{} is too far oversize to bring back by grinding its cap. The block needs oversize-OD bearings, or replacing.",
				housing->name) };

	// The bar holds one size along the whole line; each housing is a hair off that.
	double lineSize = spec.mainHousing.Mid() + Random::Normal(0.0, sigma);
	for (auto* housing : housings)
	{
		for (auto& station : housing->samples)
			for (double& sample : station)
				sample = lineSize + Random::Normal(0.0, sigma * GEOMETRY_ERROR);
		housing->value = housing->Size();
		housing->surfaceRa = std::max(0.2, Random::Normal(machine.finishRa, 0.1));
		housing->lastOperation = MachineOperation::LINE_BORE;
		part.MarkReadingStale(housing->name);
	}
	return { true, std::format("Line bored all {} main housings back to size and in line. Measure them again before fitting the bearings.",
		housings.size()) };
}

MachiningResult Machining::CutSeat(const Machine& machine, Part& part, Feature& width, double targetWidth, double sigma)
{
	// The seat's runout and recession live in sibling features named after the width feature.
	std::string prefix = width.name.substr(0, width.name.rfind(" Width"));
	auto runout = part.FindFeature(prefix + " Runout");
	auto recession = part.FindFeature(prefix + " Recession");
	if (!runout || !recession)
		return { false, "That isn't a valve seat." };
	if (targetWidth < 0.8 || targetWidth > 2.5)
		return { false, "Seat cutters can't make a seat that width." };

	// The 45 degree cut has to go deep enough to clean up the runout, and widening a seat means
	// cutting deeper still. Narrowing it with the 30/60 degree cutters doesn't sink it further.
	double depth = runout->value + 0.03 + (std::max)(0.0, targetWidth - width.value) * 0.35;
	if (depth > machine.maxRemoval)
		return { false, "That seat is too far gone to cut. It needs a new seat insert." };

	recession->value += depth;
	runout->value = std::abs(Random::Normal(0.0, sigma * 0.1));
	width.value = (std::max)(0.5, targetWidth + Random::Normal(0.0, sigma));
	runout->lastOperation = MachineOperation::CUT_SEAT;
	part.MarkReadingStale(runout->name);
	return { true, std::format("Cut {}. Check installed height: the seat sank {:.2f} mm.", prefix, depth) };
}

MachiningResult Machining::FaceValve(const Machine& machine, Part& part, Feature& faceRunout, double sigma)
{
	auto margin = part.FindFeature(FeatureNames::MARGIN);
	auto sink = part.FindFeature(FeatureNames::SEAT_SINK);
	if (!margin || !sink)
		return { false, "That isn't a valve." };

	double removal = faceRunout.value + 0.04;
	if (removal > machine.maxRemoval)
		return { false, "Too much runout to grind out. That valve is bent." };

	// A 45 degree face: taking material off the face thins the margin and drops the valve deeper.
	margin->value -= removal * 1.4;
	sink->value += removal * 1.4;
	faceRunout.value = std::abs(Random::Normal(0.0, sigma));
	part.MarkReadingStale(margin->name);

	if (margin->value < 0.7)
		return { true, "Refaced the valve, but the margin is knife-thin now. It'll burn." };
	return { true, "Refaced the valve. The margin is a little thinner." };
}

MachiningResult Machining::CutBore(const Machine& machine, Feature& feature, double target, double sigma)
{
	double oldMin = feature.MinSample();
	double oldSize = feature.Size();
	double oldRa = feature.surfaceRa;

	if (target <= oldMin)
		return { false, "That target is smaller than the bore already is. A cutter can only make a bore bigger." };
	if (machine.maxRemoval > 0.0 && target - oldMin > machine.maxRemoval)
		return { false, std::format("Too much stock for one {} setup (max {:.3f} mm).", Machining::OperationName(machine.operation), machine.maxRemoval) };

	bool cleanedUp = true;
	double sizeError = Random::Normal(0.0, sigma);
	for (int station = 0; station < Feature::STATIONS; station++)
		for (int axis = 0; axis < Feature::AXES; axis++)
		{
			// A boring bar deflects a little more the deeper it reaches.
			double deflection = machine.operation == MachineOperation::BORE ? -sigma * 0.5 * station / (Feature::STATIONS - 1) : 0.0;
			double cut = target + sizeError + Random::Normal(0.0, sigma * GEOMETRY_ERROR) + deflection;
			if (cut > feature.samples[station][axis])
				feature.samples[station][axis] = cut;
			else
				cleanedUp = false;
		}

	if (machine.operation == MachineOperation::HONE)
	{
		// The stones need to take ~0.02 mm to erase the previous surface entirely.
		double removal = feature.Size() - oldSize;
		double blend = std::clamp(removal / 0.02, 0.0, 1.0);
		feature.surfaceRa = std::max(0.05, oldRa + (Random::Normal(machine.finishRa, 0.03) - oldRa) * blend);
	}
	else
		feature.surfaceRa = cleanedUp ? std::max(0.05, Random::Normal(machine.finishRa, machine.finishRa * 0.1)) : std::max(oldRa, machine.finishRa);

	double removed = (std::max)(0.0, feature.Size() - oldSize);
	if (!cleanedUp)
		return { true, std::format("{} didn't fully clean up: some of the old surface is still there.", feature.name), removed };

	const char* verb = machine.operation == MachineOperation::HONE ? "Honed" : machine.operation == MachineOperation::REAM ? "Reamed" : "Bored";
	return { true, std::format("{} {} to target.", verb, feature.name), removed };
}

MachiningResult Machining::GrindJournal(const Machine& machine, Feature& feature, double target, double sigma)
{
	double oldMax = feature.MaxSample();
	double oldRa = feature.surfaceRa;

	if (target >= oldMax)
		return { false, "That target is bigger than the journal already is. A grinder can only make a journal smaller." };
	if (oldMax - target > machine.maxRemoval)
		return { false, std::format("Too much stock for one grind (max {:.3f} mm).", machine.maxRemoval) };

	bool cleanedUp = true;
	double sizeError = Random::Normal(0.0, sigma);
	for (auto& station : feature.samples)
		for (double& sample : station)
		{
			double cut = target + sizeError + Random::Normal(0.0, sigma * GEOMETRY_ERROR);
			if (cut < sample)
				sample = cut;
			else
				cleanedUp = false;
		}

	feature.surfaceRa = cleanedUp ? std::max(0.05, Random::Normal(machine.finishRa, 0.03)) : std::max(oldRa, machine.finishRa);

	if (!cleanedUp)
		return { true, std::format("{} didn't fully clean up: wear marks remain.", feature.name) };
	return { true, std::format("Ground {} to target.", feature.name) };
}

MachiningResult Machining::Polish(const Machine& machine, Feature& feature)
{
	double removal = Random::Uniform(0.5, 1.0) * machine.maxRemoval;
	for (auto& station : feature.samples)
		for (double& sample : station)
			sample -= removal + Random::Normal(0.0, machine.sigma);

	if (feature.surfaceRa > 0.6)
	{
		feature.surfaceRa *= 0.6;
		return { true, std::format("Polished {}, but the scoring is too deep to polish out. It needs a regrind.", feature.name) };
	}

	feature.surfaceRa = std::max(0.05, Random::Normal(machine.finishRa, 0.02));
	return { true, std::format("Polished {}.", feature.name) };
}

MachiningResult Machining::Surface(const Machine& machine, Part& part, Feature& feature, double depth, double sigma, const EngineSpec& spec)
{
	if (depth <= 0.0)
		return { false, "Cut depth must be greater than zero." };
	if (depth > machine.maxRemoval)
		return { false, std::format("Too deep for one pass (max {:.3f} mm).", machine.maxRemoval) };

	// By hand, more or less than asked for comes off; the machine takes off exactly what it's set to.
	double removed = machine.removalError > 0.0 ? depth * std::clamp(1.0 + Random::Normal(0.0, machine.removalError), 0.3, 2.0) : depth;
	bool cleanedUp = removed >= feature.value;
	if (cleanedUp)
	{
		feature.value = std::abs(Random::Normal(0.0, sigma));
		feature.surfaceRa = std::max(0.2, Random::Normal(machine.finishRa, 0.1));
	}
	else
		feature.value -= removed * 0.9;

	// Taking material off a deck moves everything above it: the block gets shorter and the
	// head's chambers get smaller, both of which raise compression.
	if (part.GetType() == PartType::BLOCK)
	{
		if (auto deckHeight = part.FindFeature(FeatureNames::DECK_HEIGHT))
		{
			deckHeight->value -= removed;
			part.MarkReadingStale(deckHeight->name);
		}
	}
	else if (part.GetType() == PartType::CYLINDER_HEAD)
	{
		double chamberAreaCC = std::numbers::pi / 4.0 * spec.bore.nominal * spec.bore.nominal / 1000.0;
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			if (auto chamber = part.FindFeature(FeatureNames::Chamber(cylinder)))
			{
				chamber->value -= chamberAreaCC * removed;
				part.MarkReadingStale(chamber->name);
			}
	}

	if (machine.removalError > 0.0)
		return { true, std::format("Sanded {} by hand, aiming for {:.3f} mm. {}Measure it to see how much really came off.", part.GetName(), depth,
			cleanedUp ? "" : "It still isn't flat all over. "), removed };
	if (!cleanedUp)
		return { true, std::format("Surfaced {}, but the cut wasn't deep enough to clean it up.", part.GetName()), removed };
	return { true, std::format("Surfaced {}.", part.GetName()), removed };
}

MachiningResult Machining::FileRing(const Machine& machine, Feature& feature, double gapIncrease, double sigma)
{
	if (gapIncrease <= 0.0)
		return { false, "Gap to add must be greater than zero." };
	if (gapIncrease > machine.maxRemoval)
		return { false, std::format("Take it slow: at most {:.2f} mm at a time.", machine.maxRemoval) };

	// The installed gap is pi * (bore - closed diameter), so filing the ends shrinks the closed diameter.
	double removed = std::max(0.0, gapIncrease + Random::Normal(0.0, sigma));
	feature.value -= removed / std::numbers::pi;
	return { true, "Filed the ring ends. Square it in the bore and check the gap again." };
}
