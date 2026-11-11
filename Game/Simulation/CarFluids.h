#pragma once

#include <array>
#include <map>
#include <string>

// What can be poured into a car: each bottle or can the store sells is one of these.
enum class FluidProduct
{
	OIL_5W30,
	OIL_10W30,
	OIL_20W50,
	COOLANT_50_50,		// long-life ethylene glycol, pre-mixed with water
	WATER,
	DOT3,
	DOT4,
	DOT5_SILICONE,		// won't mix with glycol fluids: it ruins the seals of a system built for DOT 3
	GEAR_OIL_75W90_GL4,
	GEAR_OIL_80W90_GL5,	// hypoid oil: right for the diff, but its additives eat the gearbox's brass synchros
	ATF,
	PETROL_87,
	PETROL_91,
	DIESEL,
	COUNT
};

// The car's fluid circuits: each is filled, drained and checked on its own.
enum class FluidCircuit
{
	ENGINE_OIL,		// in the engine's pan, poured in at the valve cover cap
	COOLANT,		// the engine's jackets, the radiator and the hoses
	BRAKES,			// the master cylinder, the lines and the calipers and wheel cylinders
	CLUTCH,			// the clutch master and release cylinders
	TRANSMISSION,	// the gearbox, filled at its side plug till it runs out
	DIFFERENTIAL,	// the axle, likewise
	FUEL,			// the tank
	COUNT
};
constexpr int FLUID_CIRCUITS = static_cast<int>(FluidCircuit::COUNT);
constexpr int FLUID_PRODUCTS = static_cast<int>(FluidProduct::COUNT);

const char* FluidProductName(FluidProduct product);
const char* FluidCircuitName(FluidCircuit circuit);

// How a product suits a circuit.
enum class FluidFit
{
	RIGHT,			// what the manual calls for
	ACCEPTABLE,		// works: a heavier oil, a higher octane, DOT 4 for DOT 3
	WRONG,			// will do harm over time (GL-5 in the gearbox, plain water in the cooling system)
	RUINOUS			// harms it as soon as it runs (diesel in the tank, silicone in a glycol brake system)
};
FluidFit FluidFitFor(FluidCircuit circuit, FluidProduct product);

// A brake or clutch bleed point: a caliper's or wheel cylinder's nipple, or the clutch release cylinder's.
enum class BleedPoint
{
	REAR_RIGHT,		// farthest from the master cylinder: bled first
	REAR_LEFT,
	FRONT_RIGHT,
	FRONT_LEFT,
	CLUTCH,
	COUNT
};
constexpr int BLEED_POINTS = static_cast<int>(BleedPoint::COUNT);
const char* BleedPointName(BleedPoint point);

// One circuit's contents: what's been poured in, and how much air is trapped in it.
struct FluidLevel
{
	std::map<FluidProduct, double> litres = {};	// a drain leaves what was in it behind in the pan or the floor, not here
	// Brakes and clutch: air in the line to each bleed point, 0 (bled) to 1 (dry). Coolant: air in the
	// jackets, burped out by running it with the cap off.
	std::array<double, BLEED_POINTS> air = {};
	double airPocket = 0.0;
	// Hours run since it was filled, for oil and coolant: they wear out.
	double hours = 0.0;

	double Total() const;
	// What makes up most of it, or COUNT if it's empty.
	FluidProduct Main() const;
	// How much of it isn't what the circuit wants (0 to 1).
	double Contamination(FluidCircuit circuit) const;
};
