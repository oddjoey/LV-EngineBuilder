#pragma once

#include <array>
#include <string>
#include <vector>

#include "CarFluids.h"
#include "Tolerance.h"

// One circuit as the manual has it: how much it holds, what goes in, and how it's checked.
struct FluidSpec
{
	FluidCircuit circuit = FluidCircuit::ENGINE_OIL;
	double capacity = 0.0;			// litres, drained and refilled
	Range level = {};				// litres where the dipstick, reservoir marks or filler plug say it's right
	FluidProduct product = FluidProduct::OIL_10W30;
	const char* grade = "";			// what the manual calls it
	const char* check = "";			// how it's checked and filled
	double changeHours = 0.0;		// hours of running before it should be changed (0: it isn't)
};

// The factory figures for setting the car up: what the engine is tuned to and where the wheels point.
struct TuneSpec
{
	Range ignitionTiming = {};		// degrees before TDC at idle, vacuum advance hose off and plugged
	double ignitionTimingNominal = 0.0;
	Range idleSpeed = {};			// rpm, warm, in neutral
	double idleSpeedNominal = 0.0;
	Range idleMixture = {};			// turns of the mixture screw out from lightly seated
	double idleMixtureNominal = 0.0;
	Range mainJet = {};				// the primary main jet's number
	double mainJetNominal = 0.0;
	Range intakeLash = {};			// valve clearance, hot, mm
	Range exhaustLash = {};
	double intakeLashNominal = 0.0;
	double exhaustLashNominal = 0.0;
	Range dwell = {};				// points dwell, degrees (kept for the manual: the LV-16's igniter has none to set)

	// Alignment, unladen with a full tank. Camber isn't adjustable on the stock car; caster is set
	// with the strut rods' nuts, toe with the tie rod ends.
	Range frontCamber = {};			// degrees, negative leaning in at the top
	double frontCamberNominal = 0.0;
	Range caster = {};
	double casterNominal = 0.0;
	Range frontToe = {};			// mm, positive toed in, measured across the tire treads
	double frontToeNominal = 0.0;
	Range tirePressure = {};		// kPa, cold
	double tirePressureNominal = 0.0;
	Range steeringWheelPlay = {};	// mm at the rim
};

// What the car stops on: the wear limits the manual gives for measuring them.
struct BrakeSpec
{
	Tolerance rotorThickness = {};	// mm, new
	double minRotorThickness = 0.0;
	double maxRotorRunout = 0.0;
	Tolerance padThickness = {};	// lining only
	double minPadThickness = 0.0;
	Tolerance drumDiameter = {};	// inside, new
	double maxDrumDiameter = 0.0;
	Tolerance shoeLining = {};
	double minShoeLining = 0.0;
	Range pedalHeight = {};			// mm from the floor mat
	Range pedalFreePlay = {};
	double minPedalReserve = 0.0;	// pedal to floor, pushed hard with the engine running
	double frontPistonMm = 0.0;		// caliper bore
	double rearPistonMm = 0.0;		// wheel cylinder bore
	double masterBoreMm = 0.0;
	double rotorEffectiveRadius = 0.0;	// m
	double drumRadius = 0.0;			// m
};

// The rest of the manual: tires, gearing and the body.
struct CarSpec
{
	std::string name = "";
	std::string code = "";
	int year = 0;

	double wheelbase = 0.0;			// m
	double frontTrack = 0.0;
	double rearTrack = 0.0;
	double length = 0.0;
	double width = 0.0;
	double height = 0.0;
	double curbMass = 0.0;			// kg, full of fluids, with the LV-16 and no driver
	double frontWeightShare = 0.0;	// of the curb mass on the front wheels
	double centreOfMassHeight = 0.0;
	double dragArea = 0.0;			// Cd x frontal area, m^2
	double rollingResistance = 0.0;
	double tireRadius = 0.0;		// m, loaded
	double tireGrip = 0.0;			// peak friction on dry tarmac, at the right pressure
	const char* tireSize = "";
	Tolerance treadDepth = {};		// mm new
	double minTreadDepth = 0.0;

	std::vector<double> gearRatios = {};	// first up
	double reverseRatio = 0.0;
	double finalDriveNominal = 0.0;
	std::vector<double> finalDriveOptions = {};	// what the store sells, the factory's first
	double drivetrainEfficiency = 0.0;
	double clutchCapacityNm = 0.0;	// a new disc and plate
	Tolerance clutchDiscThickness = {};
	double minClutchRivetDepth = 0.0;
	Range clutchPedalFreePlay = {};

	Range batteryVoltage = {};		// resting, charged
	double minCrankingVoltage = 0.0;
	Range chargingVoltage = {};		// at 2,000 rpm
	Range frontSpringFreeLength = {};
	Range rearSpringFreeLength = {};
	double frontSpringRate = 0.0;	// N/mm
	double rearSpringRate = 0.0;

	std::array<FluidSpec, FLUID_CIRCUITS> fluids = {};
	TuneSpec tune = {};
	BrakeSpec brakes = {};

	const FluidSpec& Fluid(FluidCircuit circuit) const { return fluids[static_cast<size_t>(circuit)]; }

	// The LE86 MonkeyKobo Comet GT coupe, as MonkeyKobo built it: the LV-16, the MK50 and the 6.7 inch axle.
	static const CarSpec& LE86Coupe();
};
