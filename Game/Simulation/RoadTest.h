#pragma once

#include <string>
#include <vector>

#include "CarAssembly.h"
#include "Dyno.h"

// What the tune does to the engine: a factor on its torque across the rev range, and what can be heard,
// smelled or seen of it.
struct EngineTuneEffect
{
	double torqueFactor = 1.0;
	double fuelFactor = 1.0;		// fuel used against a factory tune
	bool idles = true;				// settles at idle rather than stalling or hunting
	std::vector<DynoFinding> findings = {};
};

// One moment of the run down the strip, for the results' graph.
struct RoadTestSample
{
	float time = 0.f;		// s from the launch
	float speed = 0.f;		// km/h
	float rpm = 0.f;
	int gear = 0;			// 0 is first
};

// A drive round the block and down the strip, the way a buyer would test it. Nothing in it is known
// until it's been driven.
struct RoadTestResult
{
	bool ran = false;
	bool cranks = false;			// the starter turns it over
	bool starts = false;			// it fires and keeps running
	bool drives = false;			// it moves under its own power
	bool stops = false;				// the brakes pull it up
	bool steers = false;
	bool failed = false;			// something gave up on the test

	double zeroToHundred = 0.0;		// s, 0 to 100 km/h (0: never got there)
	double quarterMile = 0.0;		// s
	double quarterMileSpeed = 0.0;	// km/h at the end of it
	double topSpeed = 0.0;			// km/h
	double brakingDistance = 0.0;	// m, 100 to 0 km/h
	double lateralGrip = 0.0;		// g, on the skidpad
	double fuelEconomy = 0.0;		// litres per 100 km, at a steady 90 km/h
	double idleRpm = 0.0;
	double healthScore = 0.0;		// 0 to 1, like the dyno's: what a buyer pays for

	std::vector<DynoFinding> findings = {};
	// The run down the strip, every tenth of a second, to the top speed. Not saved: a loaded game has the figures
	// but not the graph till it's driven again.
	std::vector<RoadTestSample> trace = {};
};

class RoadTest
{
public:
	// Drives the car. engine is the pull of the engine that's in it (Dyno::Run), and engineComplete whether
	// every part of the engine is on; with no engine in, or one that won't run, it goes nowhere.
	static RoadTestResult Run(const CarAssembly& car, const DynoResult& engine, bool engineComplete);
	// What the tune does to an engine with this compression ratio, on that fuel.
	static EngineTuneEffect TuneEffect(const CarTune& tune, const CarSpec& spec, double compressionRatio, FluidProduct fuel);
};
