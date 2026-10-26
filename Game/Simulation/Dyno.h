#pragma once

#include <string>
#include <vector>

#include "EngineAssembly.h"

enum class FindingSeverity { INFO, WARNING, FAILURE };

struct DynoFinding
{
	FindingSeverity severity = FindingSeverity::INFO;
	std::string message = "";
};

struct DynoPoint
{
	float rpm = 0.f;
	float torqueNm = 0.f;
	float powerKw = 0.f;
};

struct DynoResult
{
	bool ran = false;
	bool failed = false;
	float failureRpm = 0.f;
	std::vector<DynoPoint> curve = {};
	double peakTorqueNm = 0.0;
	double peakTorqueRpm = 0.0;
	double peakPowerKw = 0.0;
	double peakPowerRpm = 0.0;
	double oilPressureBar = 0.0;
	double blowbyPercent = 0.0;
	double compressionRatio = 0.0;
	double displacementCc = 0.0;
	double healthScore = 0.0;
	std::vector<DynoFinding> findings = {};
};

// Running temperature rise over a 20 degC build, used for hot clearances.
namespace OperatingTemperature
{
	constexpr double BORE_RISE = 80.0;
	constexpr double BORE_TOP_RISE = 130.0;		// where the top ring sits at TDC
	constexpr double PISTON_SKIRT_RISE = 110.0;
	constexpr double TOP_RING_RISE = 180.0;
	// Cam-ground, strutted skirts grow less than a plain cylinder of the same alloy would.
	constexpr double SKIRT_DESIGN_FACTOR = 0.5;
}

// A pull from 1000 rpm to redline. Clearances are judged hot, so a cold number that looks
// fine on paper can still seize a piston or butt a ring.
class Dyno
{
public:
	static DynoResult Run(EngineAssembly& assembly);
private:
	static void ScoreBore(Part& block, const std::string& boreName);
	static void ScoreJournal(Part& crankshaft, const std::string& journalName);
	static double ThermalEfficiency(double compressionRatio);
};
