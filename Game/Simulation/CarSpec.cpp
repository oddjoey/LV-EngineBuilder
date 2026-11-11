#include "CarSpec.h"

#include <algorithm>

// ---- Fluids ----

const char* FluidProductName(FluidProduct product)
{
	switch (product)
	{
		case FluidProduct::OIL_5W30:			return "5W-30 Engine Oil";
		case FluidProduct::OIL_10W30:			return "10W-30 Engine Oil";
		case FluidProduct::OIL_20W50:			return "20W-50 Engine Oil";
		case FluidProduct::COOLANT_50_50:		return "50/50 Coolant";
		case FluidProduct::WATER:				return "Distilled Water";
		case FluidProduct::DOT3:				return "DOT 3 Brake Fluid";
		case FluidProduct::DOT4:				return "DOT 4 Brake Fluid";
		case FluidProduct::DOT5_SILICONE:		return "DOT 5 Silicone Brake Fluid";
		case FluidProduct::GEAR_OIL_75W90_GL4:	return "75W-90 GL-4 Gear Oil";
		case FluidProduct::GEAR_OIL_80W90_GL5:	return "80W-90 GL-5 Hypoid Oil";
		case FluidProduct::ATF:					return "Automatic Transmission Fluid";
		case FluidProduct::PETROL_87:			return "Unleaded Petrol (87)";
		case FluidProduct::PETROL_91:			return "Premium Unleaded (91)";
		case FluidProduct::DIESEL:				return "Diesel";
		case FluidProduct::COUNT:				break;
	}
	return "";
}

const char* FluidCircuitName(FluidCircuit circuit)
{
	switch (circuit)
	{
		case FluidCircuit::ENGINE_OIL:		return "Engine oil";
		case FluidCircuit::COOLANT:			return "Coolant";
		case FluidCircuit::BRAKES:			return "Brake fluid";
		case FluidCircuit::CLUTCH:			return "Clutch fluid";
		case FluidCircuit::TRANSMISSION:	return "Gearbox oil";
		case FluidCircuit::DIFFERENTIAL:	return "Differential oil";
		case FluidCircuit::FUEL:			return "Fuel";
		case FluidCircuit::COUNT:			break;
	}
	return "";
}

const char* BleedPointName(BleedPoint point)
{
	switch (point)
	{
		case BleedPoint::REAR_RIGHT:	return "Rear right wheel cylinder";
		case BleedPoint::REAR_LEFT:		return "Rear left wheel cylinder";
		case BleedPoint::FRONT_RIGHT:	return "Front right caliper";
		case BleedPoint::FRONT_LEFT:	return "Front left caliper";
		case BleedPoint::CLUTCH:		return "Clutch release cylinder";
		case BleedPoint::COUNT:			break;
	}
	return "";
}

FluidFit FluidFitFor(FluidCircuit circuit, FluidProduct product)
{
	using P = FluidProduct;
	switch (circuit)
	{
		case FluidCircuit::ENGINE_OIL:
			if (product == P::OIL_10W30)
				return FluidFit::RIGHT;
			if (product == P::OIL_5W30 || product == P::OIL_20W50)
				return FluidFit::ACCEPTABLE;
			if (product == P::GEAR_OIL_75W90_GL4 || product == P::GEAR_OIL_80W90_GL5 || product == P::ATF)
				return FluidFit::WRONG;
			return FluidFit::RUINOUS;
		case FluidCircuit::COOLANT:
			if (product == P::COOLANT_50_50)
				return FluidFit::RIGHT;
			if (product == P::WATER)
				return FluidFit::WRONG;		// no corrosion inhibitor, no antifreeze, boils sooner
			return FluidFit::RUINOUS;
		case FluidCircuit::BRAKES:
		case FluidCircuit::CLUTCH:
			if (product == P::DOT3)
				return FluidFit::RIGHT;
			if (product == P::DOT4)
				return FluidFit::ACCEPTABLE;
			return FluidFit::RUINOUS;		// silicone, or anything mineral, swells and kills the rubber seals
		case FluidCircuit::TRANSMISSION:
			if (product == P::GEAR_OIL_75W90_GL4)
				return FluidFit::RIGHT;
			if (product == P::GEAR_OIL_80W90_GL5 || product == P::ATF || product == P::OIL_10W30 || product == P::OIL_20W50)
				return FluidFit::WRONG;
			return FluidFit::RUINOUS;
		case FluidCircuit::DIFFERENTIAL:
			if (product == P::GEAR_OIL_80W90_GL5)
				return FluidFit::RIGHT;
			if (product == P::GEAR_OIL_75W90_GL4)
				return FluidFit::WRONG;		// not enough extreme-pressure additive for hypoid gears
			return FluidFit::RUINOUS;
		case FluidCircuit::FUEL:
			if (product == P::PETROL_87)
				return FluidFit::RIGHT;
			if (product == P::PETROL_91)
				return FluidFit::ACCEPTABLE;
			return FluidFit::RUINOUS;
		case FluidCircuit::COUNT:
			break;
	}
	return FluidFit::RUINOUS;
}

double FluidLevel::Total() const
{
	double total = 0.0;
	for (const auto& [product, amount] : litres)
		total += amount;
	return total;
}

FluidProduct FluidLevel::Main() const
{
	FluidProduct main = FluidProduct::COUNT;
	double most = 0.0;
	for (const auto& [product, amount] : litres)
		if (amount > most)
		{
			most = amount;
			main = product;
		}
	return main;
}

double FluidLevel::Contamination(FluidCircuit circuit) const
{
	double total = Total();
	if (total <= 0.0)
		return 0.0;
	double bad = 0.0;
	for (const auto& [product, amount] : litres)
		if (auto fit = FluidFitFor(circuit, product); fit == FluidFit::WRONG || fit == FluidFit::RUINOUS)
			bad += amount;
	return bad / total;
}

// ---- The LE86 ----

const CarSpec& CarSpec::LE86Coupe()
{
	static const CarSpec spec = [] {
		CarSpec car;
		car.name = "Comet GT Coupe";
		car.code = "LE86";
		car.year = 1985;

		car.wheelbase = 2.400;
		car.frontTrack = 1.355;
		car.rearTrack = 1.345;
		car.length = 4.205;
		car.width = 1.625;
		car.height = 1.335;
		car.curbMass = 970.0;
		car.frontWeightShare = 0.53;
		car.centreOfMassHeight = 0.50;
		car.dragArea = 0.35 * 1.70;
		car.rollingResistance = 0.013;
		car.tireRadius = 0.285;
		car.tireGrip = 0.92;
		car.tireSize = "185/70 R13";
		car.treadDepth = Tolerance::Limits(7.5, 8.5);
		car.minTreadDepth = 1.6;

		// The MK50 five-speed.
		car.gearRatios = { 3.587, 2.022, 1.384, 1.000, 0.861 };
		car.reverseRatio = 3.484;
		car.finalDriveNominal = 3.909;
		car.finalDriveOptions = { 3.909, 4.100, 4.300, 4.556, 4.778 };
		car.drivetrainEfficiency = 0.86;
		car.clutchCapacityNm = 170.0;
		car.clutchDiscThickness = Tolerance::Limits(8.1, 8.5);
		car.minClutchRivetDepth = 0.3;
		car.clutchPedalFreePlay = { 5.0, 15.0 };

		car.batteryVoltage = { 12.5, 12.9 };
		car.minCrankingVoltage = 9.6;
		car.chargingVoltage = { 13.9, 15.1 };
		car.frontSpringFreeLength = { 318.0, 326.0 };
		car.rearSpringFreeLength = { 338.0, 346.0 };
		car.frontSpringRate = 22.0;
		car.rearSpringRate = 16.0;

		using P = FluidProduct;
		using C = FluidCircuit;
		car.fluids = { {
			{ C::ENGINE_OIL,	3.5,	{ 3.0, 3.5 },	P::OIL_10W30,			"API SF 10W-30 (20W-50 in hot climates)",
				"Dipstick, engine off and warm, on level ground: between the L and F marks. 0.5 l takes it from L to F.", 50.0 },
			{ C::COOLANT,		5.6,	{ 5.3, 5.6 },	P::COOLANT_50_50,		"Ethylene glycol long-life coolant, 50/50 with water",
				"Fill at the radiator cap cold, run it with the cap off till the thermostat opens and the bubbles stop, then top up the radiator and bring the overflow tank to FULL.", 200.0 },
			{ C::BRAKES,		0.55,	{ 0.50, 0.55 },	P::DOT3,				"DOT 3 (SAE J1703)",
				"Reservoir between MIN and MAX. Bleed farthest first: rear right, rear left, front right, front left, until no bubbles come out.", 0.0 },
			{ C::CLUTCH,		0.15,	{ 0.12, 0.15 },	P::DOT3,				"DOT 3 (SAE J1703)",
				"Its own reservoir on the firewall. Bleed at the release cylinder on the bellhousing.", 0.0 },
			{ C::TRANSMISSION,	1.6,	{ 1.5, 1.6 },	P::GEAR_OIL_75W90_GL4,	"API GL-4 75W-90 gear oil",
				"Fill at the side plug until it runs out of the hole. Never GL-5: its additives attack the synchro rings.", 0.0 },
			{ C::DIFFERENTIAL,	1.1,	{ 1.0, 1.1 },	P::GEAR_OIL_80W90_GL5,	"API GL-5 80W-90 hypoid gear oil",
				"Fill at the carrier's plug until it runs out of the hole.", 0.0 },
			{ C::FUEL,			50.0,	{ 5.0, 50.0 },	P::PETROL_87,			"Unleaded petrol, 87 octane (R+M)/2 or higher",
				"At the filler. Anything above the warning light's reserve will do; never diesel.", 0.0 },
		} };

		auto& tune = car.tune;
		tune.ignitionTiming = { 4.0, 6.0 };
		tune.ignitionTimingNominal = 5.0;
		tune.idleSpeed = { 750.0, 850.0 };
		tune.idleSpeedNominal = 800.0;
		tune.idleMixture = { 2.0, 3.0 };
		tune.idleMixtureNominal = 2.5;
		tune.mainJet = { 103.0, 107.0 };
		tune.mainJetNominal = 105.0;
		tune.intakeLash = { 0.15, 0.25 };
		tune.exhaustLash = { 0.25, 0.35 };
		tune.intakeLashNominal = 0.20;
		tune.exhaustLashNominal = 0.30;
		tune.dwell = { 52.0, 58.0 };
		tune.frontCamber = { -1.25, 0.25 };
		tune.frontCamberNominal = -0.5;
		tune.caster = { 2.0, 3.5 };
		tune.casterNominal = 2.75;
		tune.frontToe = { 0.0, 2.0 };
		tune.frontToeNominal = 1.0;
		tune.tirePressure = { 170.0, 190.0 };
		tune.tirePressureNominal = 180.0;
		tune.steeringWheelPlay = { 0.0, 30.0 };

		auto& brakes = car.brakes;
		brakes.rotorThickness = Tolerance::Limits(17.9, 18.1);
		brakes.minRotorThickness = 16.0;
		brakes.maxRotorRunout = 0.15;
		brakes.padThickness = Tolerance::Limits(9.5, 10.5);
		brakes.minPadThickness = 1.0;
		brakes.drumDiameter = Tolerance::Limits(199.9, 200.1);
		brakes.maxDrumDiameter = 201.0;
		brakes.shoeLining = Tolerance::Limits(3.8, 4.2);
		brakes.minShoeLining = 1.0;
		brakes.pedalHeight = { 160.0, 170.0 };
		brakes.pedalFreePlay = { 3.0, 6.0 };
		brakes.minPedalReserve = 70.0;
		brakes.frontPistonMm = 51.1;
		brakes.rearPistonMm = 19.05;
		brakes.masterBoreMm = 20.64;
		brakes.rotorEffectiveRadius = 0.105;
		brakes.drumRadius = 0.100;
		return car;
	}();
	return spec;
}
