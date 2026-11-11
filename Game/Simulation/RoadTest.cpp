#include "RoadTest.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace
{
	constexpr double GRAVITY = 9.81;
	constexpr double AIR_DENSITY = 1.2;
	constexpr double DRIVER_MASS = 75.0;
	constexpr double QUARTER_MILE = 402.34;
	constexpr double HUNDRED = 100.0 / 3.6;		// m/s
	constexpr double LAUNCH_RPM = 3000.0;
	constexpr double SHIFT_TIME = 0.35;
	constexpr double PETROL_DENSITY = 0.745;	// kg/l
	constexpr double BSFC = 0.40;				// kg/kWh at a light cruising load, for a carburettor

	struct Report
	{
		RoadTestResult& result;
		double penalty = 0.0;

		void operator()(FindingSeverity severity, const std::string& message, double cost = -1.0)
		{
			result.findings.push_back({ severity, message });
			if (cost < 0.0)
				cost = severity == FindingSeverity::FAILURE ? 30.0 : severity == FindingSeverity::WARNING ? 4.0 : 0.0;
			penalty += cost;
		}
	};

	// Torque off the engine's pull at an rpm, between its points.
	double TorqueAt(const DynoResult& engine, double rpm)
	{
		const auto& curve = engine.curve;
		if (curve.empty())
			return 0.0;
		if (rpm <= curve.front().rpm)
			return curve.front().torqueNm;
		for (size_t i = 1; i < curve.size(); i++)
			if (rpm <= curve[i].rpm)
			{
				double t = (rpm - curve[i - 1].rpm) / (curve[i].rpm - curve[i - 1].rpm);
				return curve[i - 1].torqueNm + (curve[i].torqueNm - curve[i - 1].torqueNm) * t;
			}
		return 0.0;		// past the end of the pull: on the limiter, or where it let go
	}

	// The worst state a part in any position of a slot is in.
	PartCondition WorstCondition(const CarAssembly& car, CarSlot slot)
	{
		PartCondition worst = PartCondition::NEW;
		for (int index = 0; index < car.SlotCount(slot); index++)
			if (auto part = car.Get(slot, index); part && part->GetCondition() > worst)
				worst = part->GetCondition();
		return worst;
	}
}

EngineTuneEffect RoadTest::TuneEffect(const CarTune& tune, const CarSpec& spec, double compressionRatio, FluidProduct fuel)
{
	EngineTuneEffect effect;
	auto report = [&](FindingSeverity severity, const std::string& message) { effect.findings.push_back({ severity, message }); };
	const auto& ts = spec.tune;

	// Ignition timing: more advance is more power, up to where it starts to knock. How far that is depends
	// on the compression ratio and the octane.
	double octane = fuel == FluidProduct::PETROL_91 ? 91.0 : 87.0;
	double advance = tune.ignitionTiming - ts.ignitionTimingNominal;
	double knockMargin = 3.0 + (octane - 87.0) * 0.75 - (std::max)(0.0, compressionRatio - 9.3) * 2.5;
	if (advance >= 0.0)
		effect.torqueFactor += 0.012 * (std::min)(advance, 4.0);
	else
		effect.torqueFactor += 0.025 * advance;
	if (advance > knockMargin + 4.0)
		report(FindingSeverity::FAILURE, std::format("Heavy detonation under load at {:.0f} degrees BTDC: it rattles like gravel in a can, and "
			"that hammers the pistons and the head gasket. Back the timing off.", tune.ignitionTiming));
	else if (advance > knockMargin)
	{
		effect.torqueFactor -= 0.03 * (advance - knockMargin);
		report(FindingSeverity::WARNING, std::format("Pinging when it's pulled hard uphill at {:.0f} degrees BTDC: too much advance for {:.0f} octane "
			"at this compression.", tune.ignitionTiming, octane));
	}
	if (advance < -6.0)
		report(FindingSeverity::WARNING, "The timing is well retarded: it's sluggish, runs hot, and the exhaust manifold glows.");
	else if (advance < -1.5)
		report(FindingSeverity::INFO, "The timing is a little retarded: a few degrees more advance would wake it up.");

	// The main jet: each number is about 1.5% more fuel. Best power is a touch rich of the factory jet's.
	double richness = (tune.mainJet - ts.mainJetNominal) * 0.015;
	effect.torqueFactor -= 25.0 * std::pow(richness - 0.015, 2.0);
	effect.fuelFactor += richness;
	if (richness < -0.075)
		report(FindingSeverity::FAILURE, std::format("Dangerously lean on the #{:.0f} main jet: it surges, pops back through the carburettor, "
			"and burns the exhaust valves.", tune.mainJet));
	else if (richness < -0.045)
		report(FindingSeverity::WARNING, std::format("Lean on the #{:.0f} main jet: it stumbles at full throttle and runs hot.", tune.mainJet));
	else if (richness > 0.06)
		report(FindingSeverity::WARNING, std::format("Rich on the #{:.0f} main jet: black smoke, sooty plugs, and a catalytic converter "
			"that's cooking.", tune.mainJet));

	// Idle: the speed screw and the mixture screw.
	if (tune.idleSpeed < 600.0)
	{
		effect.idles = false;
		report(FindingSeverity::WARNING, std::format("It stalls at the lights: {:.0f} rpm is too slow an idle.", tune.idleSpeed));
	}
	else if (tune.idleSpeed > 1100.0)
		report(FindingSeverity::WARNING, std::format("It idles at {:.0f} rpm: it's hard to get into gear and it creeps.", tune.idleSpeed));
	if (tune.idleMixture < ts.idleMixture.min - 1.0)
	{
		effect.idles = false;
		report(FindingSeverity::WARNING, "The idle mixture is so lean it hunts and dies.");
	}
	else if (tune.idleMixture > ts.idleMixture.max + 1.0)
	{
		effect.idles = false;
		report(FindingSeverity::WARNING, "The idle mixture is so rich it lopes, smells of fuel and dies when it's hot.");
	}
	else if (!ts.idleMixture.Contains(tune.idleMixture))
		report(FindingSeverity::INFO, "The idle is a little rough: the mixture screw wants setting for the best, smoothest idle.");

	// Valve lash: too tight holds the valves off their seats; too loose clatters and costs lift.
	auto lash = [&](double value, const Range& range, const char* side) {
		if (value < range.min - 0.08)
		{
			effect.torqueFactor -= 0.06;
			report(FindingSeverity::FAILURE, std::format("The {} valves have no lash: they're held off their seats, compression is down and "
				"they'll burn.", side));
		}
		else if (value < range.min)
		{
			effect.torqueFactor -= 0.015;
			report(FindingSeverity::WARNING, std::format("The {} valve lash is tight: it'll burn the valves once it's hot.", side));
		}
		else if (value > range.max + 0.1)
		{
			effect.torqueFactor -= 0.03;
			report(FindingSeverity::WARNING, std::format("The {} valve lash is far too loose: it clatters, and wears the rockers and the cam.", side));
		}
		else if (value > range.max)
		{
			effect.torqueFactor -= 0.01;
			report(FindingSeverity::INFO, std::format("A tick from the {} rockers: the lash is a little loose.", side));
		}
	};
	lash(tune.intakeLash, ts.intakeLash, "intake");
	lash(tune.exhaustLash, ts.exhaustLash, "exhaust");

	effect.torqueFactor = (std::max)(0.3, effect.torqueFactor);
	return effect;
}

RoadTestResult RoadTest::Run(const CarAssembly& car, const DynoResult& engine, bool engineComplete)
{
	RoadTestResult result;
	result.ran = true;
	Report report{ result };
	const auto& spec = car.GetSpec();
	const auto& tune = car.GetTune();

	auto has = [&](CarSlot slot) { return car.IsSlotComplete(slot); };
	auto firstMissing = [&](std::initializer_list<CarSlot> slots) -> std::optional<std::string> {
		for (auto slot : slots)
			for (int index = 0; index < car.SlotCount(slot); index++)
				if (!car.IsFilled(slot, index))
					return car.PositionName(slot, index);
		// Its nuts and bolts, too.
		for (auto slot : slots)
			if (!has(slot))
				return std::format("bolts for the {}", CarSlotName(slot));
		return std::nullopt;
	};
	auto fluid = [&](FluidCircuit circuit) -> const FluidLevel& { return car.GetFluid(circuit); };
	auto level = [&](FluidCircuit circuit) { return fluid(circuit).Total(); };

	// Nuts and bolts that matter on the road: finger tight is a failure, and well under spec a warning.
	auto checkFasteners = [&](CarSlot slot, bool critical) {
		const auto* holes = CarAssembly::Holes(slot);
		int loose = 0, under = 0;
		for (int index = 0; index < car.SlotCount(slot); index++)
			if (auto part = car.Get(slot, index))
			{
				if (part->GetTightness() == FastenerTightness::LOOSE)
					loose++;
				else if (part->GetTorque() < holes->torqueNm * 0.7)
					under++;
			}
		if (loose > 0)
			report(critical ? FindingSeverity::FAILURE : FindingSeverity::WARNING, std::format("{} {}{} only finger tight.", loose,
				holes->name, loose == 1 ? " was" : "s were"));
		else if (under > 0)
			report(FindingSeverity::WARNING, std::format("{} {}{} well under the {:.0f} Nm spec.", under, holes->name,
				under == 1 ? " was" : "s were", holes->torqueNm));
		return loose == 0 || !critical;
	};

	// ---- Fluids ----
	for (int circuit = 0; circuit < FLUID_CIRCUITS; circuit++)
	{
		auto c = static_cast<FluidCircuit>(circuit);
		const auto& fs = spec.Fluid(c);
		const auto& f = fluid(c);
		double amount = f.Total();
		if (amount <= 0.0)
			continue;	// said where it matters below
		if (auto main = f.Main(); main != FluidProduct::COUNT)
		{
			auto fit = FluidFitFor(c, main);
			if (fit == FluidFit::RUINOUS)
				report(FindingSeverity::FAILURE, std::format("{} in the {}: {}.", FluidProductName(main), FluidCircuitName(c),
					c == FluidCircuit::FUEL ? "it won't burn it" : "it's attacking the seals and the metal"));
			else if (fit == FluidFit::WRONG)
				report(FindingSeverity::WARNING, std::format("{} in the {}: the manual says {}.", FluidProductName(main), FluidCircuitName(c), fs.grade));
			else if (f.Contamination(c) > 0.1)
				report(FindingSeverity::WARNING, std::format("The {} is mixed with the wrong stuff: drain and refill it.", FluidCircuitName(c)));
		}
		if (c != FluidCircuit::FUEL && amount > fs.level.max * 1.1)
			report(FindingSeverity::WARNING, std::format("The {} is overfilled ({:.2f} l against {:.2f} l): {}.", FluidCircuitName(c), amount,
				fs.level.max, c == FluidCircuit::ENGINE_OIL ? "the crank whips it to foam" : "it pushes out of the seals and breathers"));
		if (fs.changeHours > 0.0 && f.hours > fs.changeHours)
			report(FindingSeverity::WARNING, std::format("The {} is worn out: due for a change.", FluidCircuitName(c)));
	}

	// ---- Cranking ----
	bool hasEngine = car.GetEngine() >= 0;
	if (!hasEngine)
		report(FindingSeverity::FAILURE, "There's no engine in it.");
	else if (auto missing = firstMissing({ CarSlot::BATTERY, CarSlot::MAIN_HARNESS, CarSlot::FUSE_BOX, CarSlot::ENGINE_HARNESS,
		CarSlot::STARTER, CarSlot::STEERING_COLUMN }))
		report(FindingSeverity::FAILURE, std::format("Turning the key does nothing: the {} isn't on.", *missing));
	else if (WorstCondition(car, CarSlot::BATTERY) == PartCondition::DAMAGED)
		report(FindingSeverity::FAILURE, "The battery's dead: the starter just clicks.");
	else if (WorstCondition(car, CarSlot::STARTER) == PartCondition::DAMAGED)
		report(FindingSeverity::FAILURE, "The starter whirrs but doesn't catch the flywheel.");
	else
		result.cranks = true;
	if (result.cranks && WorstCondition(car, CarSlot::BATTERY) == PartCondition::USED)
		report(FindingSeverity::INFO, "The old battery cranks it slowly: it won't like a cold morning.", 1.0);

	// ---- Starting and running ----
	EngineTuneEffect tuneEffect;
	FluidProduct fuel = fluid(FluidCircuit::FUEL).Main();
	if (result.cranks)
	{
		tuneEffect = TuneEffect(tune, spec, engine.compressionRatio, fuel);
		if (!engineComplete || !engine.ran)
			report(FindingSeverity::FAILURE, "It cranks but won't fire: the engine isn't finished.");
		else if (auto missing = firstMissing({ CarSlot::FUEL_TANK, CarSlot::FUEL_SENDER, CarSlot::FUEL_LINE_SET, CarSlot::FUEL_FILTER }))
			report(FindingSeverity::FAILURE, std::format("It cranks but won't fire: no fuel gets to it without the {}.", *missing));
		else if (level(FluidCircuit::FUEL) < 1.0)
			report(FindingSeverity::FAILURE, "It cranks but won't fire: the tank's dry.");
		else if (fuel != FluidProduct::COUNT && FluidFitFor(FluidCircuit::FUEL, fuel) == FluidFit::RUINOUS)
			report(FindingSeverity::FAILURE, std::format("It won't run on {}.", FluidProductName(fuel)));
		else if (!has(CarSlot::IGNITION_COIL))
			report(FindingSeverity::FAILURE, "It cranks but there's no spark: the ignition coil isn't on.");
		else if (std::abs(tune.ignitionTiming - spec.tune.ignitionTimingNominal) > 20.0)
			report(FindingSeverity::FAILURE, "It kicks back against the starter and won't catch: the timing is miles out.");
		else
			result.starts = true;
	}
	if (result.starts)
	{
		result.findings.insert(result.findings.end(), tuneEffect.findings.begin(), tuneEffect.findings.end());
		for (const auto& finding : tuneEffect.findings)
			report.penalty += finding.severity == FindingSeverity::FAILURE ? 20.0 : finding.severity == FindingSeverity::WARNING ? 3.0 : 0.0;
		result.idleRpm = tuneEffect.idles ? tune.idleSpeed : 0.0;

		const auto& oilSpec = spec.Fluid(FluidCircuit::ENGINE_OIL);
		double oil = level(FluidCircuit::ENGINE_OIL);
		if (oil < oilSpec.level.min * 0.5)
		{
			report(FindingSeverity::FAILURE, std::format("The oil light never went out: with {:.1f} l in it the bearings ran dry and it seized.", oil));
			result.failed = true;
		}
		else if (oil < oilSpec.level.min)
			report(FindingSeverity::WARNING, std::format("The oil is low ({:.1f} l): the light flickers in the corners.", oil));

		const auto& coolantSpec = spec.Fluid(FluidCircuit::COOLANT);
		double coolant = level(FluidCircuit::COOLANT);
		if (auto missing = car.CircuitOpen(FluidCircuit::COOLANT); missing || coolant < coolantSpec.level.min * 0.5)
		{
			report(FindingSeverity::FAILURE, "The temperature gauge went straight to H and it blew steam: the cooling system is empty.");
			result.failed = true;
		}
		else if (coolant < coolantSpec.level.min || fluid(FluidCircuit::COOLANT).airPocket > 0.3)
			report(FindingSeverity::WARNING, "It runs hot and the heater blows cold: the cooling system is low or full of air. Burp it.");
		if (!has(CarSlot::COOLING_FAN))
			report(FindingSeverity::WARNING, "It overheats in traffic: there's no cooling fan.");

		if (!has(CarSlot::CATALYTIC_CONVERTER))
			report(FindingSeverity::WARNING, "It's deafening and it stinks: the exhaust ends at the downpipe.");
		else if (!has(CarSlot::MUFFLER))
			report(FindingSeverity::WARNING, "It's loud enough to set off car alarms: there's no muffler.");
		if (has(CarSlot::CATALYTIC_CONVERTER))
			checkFasteners(CarSlot::CATALYTIC_CONVERTER_NUT, false);
		if (engine.failed)
		{
			report(FindingSeverity::FAILURE, std::format("The engine let go on the test at {:.0f} rpm.", engine.failureRpm));
			result.failed = true;
		}
		if (!tuneEffect.idles)
			report(FindingSeverity::INFO, "It has to be kept running on the throttle.", 0.0);
	}

	// ---- Driving ----
	bool canDrive = result.starts && !result.failed;
	if (canDrive)
	{
		if (auto missing = firstMissing({ CarSlot::CLUTCH_DISC, CarSlot::PRESSURE_PLATE, CarSlot::RELEASE_BEARING, CarSlot::TRANSMISSION,
			CarSlot::TRANSMISSION_MOUNT, CarSlot::CLUTCH_MASTER_CYLINDER, CarSlot::CLUTCH_RELEASE_CYLINDER, CarSlot::DRIVESHAFT,
			CarSlot::REAR_AXLE, CarSlot::DIFFERENTIAL, CarSlot::LOWER_TRAILING_LINK, CarSlot::UPPER_TRAILING_LINK, CarSlot::LATERAL_ROD,
			CarSlot::REAR_COIL_SPRING, CarSlot::REAR_SHOCK, CarSlot::FRONT_STRUT, CarSlot::STEERING_KNUCKLE, CarSlot::LOWER_CONTROL_ARM,
			CarSlot::STRUT_ROD, CarSlot::WHEEL, CarSlot::TIRE }))
		{
			report(FindingSeverity::FAILURE, std::format("It goes nowhere: the {} isn't on.", *missing));
			canDrive = false;
		}
		else if (fluid(FluidCircuit::CLUTCH).Total() < spec.Fluid(FluidCircuit::CLUTCH).level.min * 0.5
			|| fluid(FluidCircuit::CLUTCH).air[static_cast<size_t>(BleedPoint::CLUTCH)] > 0.3)
		{
			report(FindingSeverity::FAILURE, "The clutch pedal goes to the floor and it grinds going into gear: the clutch hydraulics need filling and bleeding.");
			canDrive = false;
		}
		else
		{
			// What can't wait: a wheel or the axle coming off.
			bool holding = true;
			for (auto slot : { CarSlot::LUG_NUT, CarSlot::STRUT_KNUCKLE_BOLT, CarSlot::BALL_JOINT_BOLT, CarSlot::DRIVESHAFT_FLANGE_NUT,
				CarSlot::REAR_LINK_BOLT, CarSlot::UPPER_LINK_BOLT, CarSlot::LATERAL_ROD_BOLT, CarSlot::ENGINE_MOUNT_NUT, CarSlot::BELLHOUSING_BOLT })
				holding = checkFasteners(slot, true) && holding;
			for (auto slot : { CarSlot::STRUT_MOUNT_NUT, CarSlot::CONTROL_ARM_BOLT, CarSlot::STRUT_ROD_NUT, CarSlot::SWAY_BAR_BOLT,
				CarSlot::REAR_SHOCK_BOLT, CarSlot::DIFFERENTIAL_NUT, CarSlot::ENGINE_MOUNT_BOLT, CarSlot::PRESSURE_PLATE_BOLT,
				CarSlot::TRANSMISSION_MOUNT_BOLT, CarSlot::STARTER_BOLT, CarSlot::SEAT_BOLT })
				checkFasteners(slot, false);
			if (!holding)
			{
				report(FindingSeverity::FAILURE, "Something came adrift on the first lap round the block: it's on a flatbed home.");
				result.failed = true;
				canDrive = false;
			}
		}
	}
	if (canDrive)
	{
		result.drives = true;
		const auto& gearSpec = spec.Fluid(FluidCircuit::TRANSMISSION);
		if (level(FluidCircuit::TRANSMISSION) < gearSpec.level.min * 0.5)
			report(FindingSeverity::FAILURE, "The gearbox howled and then locked in third: it was run without oil.");
		else if (level(FluidCircuit::TRANSMISSION) < gearSpec.level.min)
			report(FindingSeverity::WARNING, "The gearbox whines: it's low on oil.");
		const auto& diffSpec = spec.Fluid(FluidCircuit::DIFFERENTIAL);
		if (level(FluidCircuit::DIFFERENTIAL) < diffSpec.level.min * 0.5)
			report(FindingSeverity::FAILURE, "The diff screamed and its pinion bearing burnt out: it was run dry.");
		else if (level(FluidCircuit::DIFFERENTIAL) < diffSpec.level.min)
			report(FindingSeverity::WARNING, "The diff whines on the overrun: it's low on oil.");
		if (WorstCondition(car, CarSlot::CLUTCH_DISC) == PartCondition::DAMAGED)
			report(FindingSeverity::WARNING, "The clutch judders pulling away and slips in the higher gears.");
		if (WorstCondition(car, CarSlot::ENGINE_MOUNT) == PartCondition::DAMAGED)
			report(FindingSeverity::WARNING, "The engine clunks on and off the throttle: a mount is split.");
	}

	// ---- Steering ----
	if (result.drives)
	{
		if (auto missing = firstMissing({ CarSlot::STEERING_RACK, CarSlot::TIE_ROD_END, CarSlot::STEERING_COLUMN, CarSlot::STEERING_WHEEL }))
			report(FindingSeverity::FAILURE, std::format("It can't be steered: the {} isn't on.", *missing));
		else if (checkFasteners(CarSlot::TIE_ROD_END_NUT, true) && checkFasteners(CarSlot::STEERING_RACK_BOLT, true))
			result.steers = true;
		if (result.steers && WorstCondition(car, CarSlot::STEERING_RACK) == PartCondition::DAMAGED)
			report(FindingSeverity::WARNING, "The steering knocks over bumps and has a lot of play: the rack is worn out.");
	}

	// ---- Brakes ----
	double brakeFactor = 0.0;
	if (result.drives)
	{
		const auto& brakeFluid = fluid(FluidCircuit::BRAKES);
		const auto& brakeSpec = spec.Fluid(FluidCircuit::BRAKES);
		double air = 0.0;
		for (int point = 0; point < static_cast<int>(BleedPoint::CLUTCH); point++)
			air = (std::max)(air, brakeFluid.air[point]);
		if (auto missing = firstMissing({ CarSlot::BRAKE_MASTER_CYLINDER, CarSlot::BRAKE_LINE_SET, CarSlot::BRAKE_HOSE, CarSlot::BRAKE_ROTOR,
			CarSlot::BRAKE_PAD_SET, CarSlot::BRAKE_CALIPER, CarSlot::WHEEL_CYLINDER, CarSlot::BRAKE_SHOE_SET, CarSlot::BRAKE_DRUM }))
			report(FindingSeverity::FAILURE, std::format("No brakes: the {} isn't on. It wasn't taken out.", *missing));
		else if (brakeFluid.Total() < brakeSpec.level.min * 0.5 || air > 0.5)
			report(FindingSeverity::FAILURE, "The brake pedal goes straight to the floor: the brakes need filling and bleeding. It wasn't taken out.");
		else if (!checkFasteners(CarSlot::CALIPER_BOLT, true))
			report(FindingSeverity::FAILURE, "A caliper came loose on its bracket at the first stop.");
		else
		{
			result.stops = true;
			brakeFactor = 1.0;
			if (air > 0.1)
			{
				brakeFactor *= 0.75;
				report(FindingSeverity::WARNING, "The pedal's spongy: there's still air in the brake lines.");
			}
			if (!has(CarSlot::BRAKE_BOOSTER))
			{
				brakeFactor *= 0.5;
				report(FindingSeverity::WARNING, "The pedal's rock hard: no booster.");
			}
			if (brakeFluid.Total() < brakeSpec.level.min)
				report(FindingSeverity::WARNING, "The brake fluid is below MIN in the reservoir.");
			if (WorstCondition(car, CarSlot::BRAKE_ROTOR) == PartCondition::DAMAGED)
			{
				brakeFactor *= 0.85;
				report(FindingSeverity::WARNING, "The pedal shudders under braking: a rotor is warped or worn under the minimum.");
			}
			if (WorstCondition(car, CarSlot::BRAKE_PAD_SET) == PartCondition::DAMAGED || WorstCondition(car, CarSlot::BRAKE_SHOE_SET) == PartCondition::DAMAGED)
			{
				brakeFactor *= 0.7;
				report(FindingSeverity::WARNING, "Metal on metal from the brakes: the linings are worn through.");
			}
			if (WorstCondition(car, CarSlot::BRAKE_CALIPER) == PartCondition::DAMAGED)
				report(FindingSeverity::WARNING, "It pulls to one side under braking: a caliper's sticking.");
			if (!has(CarSlot::PARKING_BRAKE_CABLE))
				report(FindingSeverity::WARNING, "There's no handbrake.");
		}
	}

	// ---- On the road ----
	if (result.drives && result.steers && result.stops)
	{
		double mass = spec.curbMass + DRIVER_MASS;
		double finalDrive = tune.finalDrive > 0.0 ? tune.finalDrive : spec.finalDriveNominal;
		double redline = engine.curve.empty() ? 0.0 : engine.curve.back().rpm;

		// Tires: their state, their pressures, and where the wheels point.
		double grip = spec.tireGrip;
		auto tires = WorstCondition(car, CarSlot::TIRE);
		if (tires == PartCondition::USED)
			grip *= 0.94;
		else if (tires == PartCondition::DAMAGED)
		{
			grip *= 0.75;
			report(FindingSeverity::WARNING, "The tires are hard and cracked: they don't grip, wet or dry.");
		}
		const auto& ts = spec.tune;
		double pressureLoss = 0.0;
		for (int corner = 0; corner < 4; corner++)
		{
			double pressure = tune.tirePressure[corner];
			pressureLoss += std::abs(pressure - ts.tirePressureNominal) / 10.0 * 0.02 / 4.0;
			if (pressure < 120.0)
				report(FindingSeverity::WARNING, std::format("The {} tire is nearly flat ({:.0f} kPa): it squirms and overheats.",
					CornerName(static_cast<Corner>(corner)), pressure));
		}
		grip *= 1.0 - (std::min)(pressureLoss, 0.25);
		if (std::abs(tune.tirePressure[0] - tune.tirePressure[1]) > 20.0)
			report(FindingSeverity::WARNING, "It pulls to one side: the front tire pressures don't match.");

		double lateral = grip;
		double camberOff = std::abs(tune.frontCamber - ts.frontCamberNominal);
		lateral *= 1.0 - 0.03 * camberOff;
		if (!ts.frontCamber.Contains(tune.frontCamber))
			report(FindingSeverity::WARNING, std::format("The front camber is out ({:.2f} degrees): it'll wear the tire edges.", tune.frontCamber));
		if (tune.frontToe < ts.frontToe.min - 1.0)
			report(FindingSeverity::WARNING, std::format("The front wheels are toed out ({:.1f} mm): it darts at every rut.", tune.frontToe));
		else if (tune.frontToe > ts.frontToe.max + 2.0)
			report(FindingSeverity::WARNING, std::format("Far too much toe in ({:.1f} mm): the front tires scrub and squeal.", tune.frontToe));
		else if (!ts.frontToe.Contains(tune.frontToe))
			report(FindingSeverity::INFO, std::format("The toe is a little out ({:.1f} mm): it'll wear the tires.", tune.frontToe));
		if (tune.caster < ts.caster.min)
			report(FindingSeverity::WARNING, "It wanders at speed and the wheel doesn't come back on its own: not enough caster.");
		lateral *= 1.0 - 0.02 * std::abs(tune.frontToe - ts.frontToeNominal);

		// Dampers and springs.
		for (auto slot : { CarSlot::FRONT_STRUT, CarSlot::REAR_SHOCK })
		{
			auto condition = WorstCondition(car, slot);
			if (condition == PartCondition::USED)
				lateral *= 0.97;
			else if (condition == PartCondition::DAMAGED)
			{
				lateral *= 0.88;
				report(FindingSeverity::WARNING, std::format("It bounces after every bump and wallows in corners: a {} is blown.", CarSlotName(slot)));
			}
		}
		if (!has(CarSlot::FRONT_SWAY_BAR))
		{
			lateral *= 0.93;
			report(FindingSeverity::INFO, "It rolls a lot in corners without the front sway bar.", 1.0);
		}
		result.lateralGrip = lateral;

		// Down the strip: launched at 3,000 rpm, shifted at redline.
		double exhaustFactor = has(CarSlot::CATALYTIC_CONVERTER) ? 1.0 : 1.02;
		double clutchCapacity = spec.clutchCapacityNm * (WorstCondition(car, CarSlot::CLUTCH_DISC) == PartCondition::DAMAGED ? 0.6
			: WorstCondition(car, CarSlot::CLUTCH_DISC) == PartCondition::USED ? 0.9 : 1.0);
		bool clutchSlipped = false;
		auto wheelRpm = [&](double speed) { return speed / spec.tireRadius * 60.0 / (2.0 * 3.14159265358979); };
		double speed = 0.0, distance = 0.0, time = 0.0, accel = 0.0, shifting = 0.0;
		int gear = 0;
		const int gears = static_cast<int>(spec.gearRatios.size());
		constexpr double DT = 0.005;
		double steady = 0.0;
		double nextSample = 0.0;
		while (time < 120.0 && redline > 0.0)
		{
			double ratio = spec.gearRatios[gear] * finalDrive;
			double rpm = wheelRpm(speed) * ratio;
			double force = 0.0;
			if (shifting > 0.0)
				shifting -= DT;
			else if (rpm >= redline - 50.0 && gear < gears - 1)
			{
				gear++;
				shifting = SHIFT_TIME;
			}
			else
			{
				bool slipping = gear == 0 && rpm < LAUNCH_RPM;
				double engineRpm = slipping ? LAUNCH_RPM : rpm;
				double torque = TorqueAt(engine, (std::min)(engineRpm, redline)) * tuneEffect.torqueFactor * exhaustFactor;
				if (engineRpm >= redline)
					torque = 0.0;	// on the limiter
				if (torque > clutchCapacity)
				{
					torque = clutchCapacity;
					clutchSlipped = true;
				}
				force = torque * ratio * spec.drivetrainEfficiency / spec.tireRadius;
				// The rear tires can only put down so much: weight shifts back onto them as it squats.
				double rearLoad = mass * GRAVITY * (1.0 - spec.frontWeightShare) + mass * accel * spec.centreOfMassHeight / spec.wheelbase;
				force = (std::min)(force, grip * rearLoad);
			}
			double drag = 0.5 * AIR_DENSITY * spec.dragArea * speed * speed + spec.rollingResistance * mass * GRAVITY;
			// The wheels, gears and flywheel spun up with it: a lot in first, hardly any in top.
			double inertia = 1.04 + 0.0012 * std::pow(spec.gearRatios[gear] * finalDrive, 2.0);
			accel = (force - drag) / (mass * inertia);
			if (speed <= 0.0 && accel < 0.0)
				accel = 0.0;
			double before = speed;
			speed = (std::max)(0.0, speed + accel * DT);
			distance += (before + speed) * 0.5 * DT;
			time += DT;
			if (time >= nextSample && result.trace.size() < 1200)
			{
				// Slipping the clutch off the line, the engine holds the launch revs.
				bool launching = gear == 0 && shifting <= 0.0 && rpm < LAUNCH_RPM;
				result.trace.push_back({ static_cast<float>(time), static_cast<float>(speed * 3.6), static_cast<float>(launching ? LAUNCH_RPM : rpm), gear });
				nextSample += 0.1;
			}
			if (result.zeroToHundred == 0.0 && speed >= HUNDRED)
				result.zeroToHundred = time;
			if (result.quarterMile == 0.0 && distance >= QUARTER_MILE)
			{
				result.quarterMile = time;
				result.quarterMileSpeed = speed * 3.6;
			}
			result.topSpeed = (std::max)(result.topSpeed, speed * 3.6);
			// Flat out in top: once it stops gaining, that's its top speed.
			steady = gear == gears - 1 && shifting <= 0.0 && std::abs(accel) < 0.02 ? steady + DT : 0.0;
			if (steady > 2.0 && result.quarterMile > 0.0)
				break;
		}
		if (clutchSlipped)
			report(FindingSeverity::WARNING, "The clutch slips at full throttle: it can't hold what the engine makes.");

		// From 100 km/h, as hard as it'll stop without locking up.
		double decel = (std::min)(grip * 1.05, brakeFactor * 1.1) * GRAVITY;
		result.brakingDistance = HUNDRED * HUNDRED / (2.0 * decel);

		// Cruising at 90 km/h in top.
		double cruise = 90.0 / 3.6;
		double cruisePower = (0.5 * AIR_DENSITY * spec.dragArea * cruise * cruise + spec.rollingResistance * mass * GRAVITY) * cruise / 1000.0;
		double fuelPerHour = cruisePower / spec.drivetrainEfficiency * BSFC * tuneEffect.fuelFactor / PETROL_DENSITY;
		result.fuelEconomy = fuelPerHour / 90.0 * 100.0;
	}
	else if (result.drives)
		report(FindingSeverity::INFO, "It wasn't driven any further than the end of the drive.", 0.0);

	// What a buyer makes of it: the engine's own health, less what the drive turned up.
	double health = (engine.ran ? engine.healthScore : 0.0) - report.penalty;
	if (!result.drives)
		health = (std::min)(health, 20.0);
	result.healthScore = std::clamp(health, 0.0, 100.0);
	return result;
}
