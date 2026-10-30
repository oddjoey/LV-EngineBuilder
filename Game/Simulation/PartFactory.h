#pragma once

#include "EngineSpec.h"
#include "Part.h"

// Manufactures parts with realistic variance. New parts scatter around the blueprint (budget
// suppliers scatter wider and sometimes miss); used parts arrive worn, tapered and glazed.
class PartFactory
{
public:
	// sizeOffset: oversize for pistons/rings (+0.25, +0.50...), journal undersize for bearings (-0.25...).
	static pPart Create(PartType type, const EngineSpec& spec, PartQuality quality, PartCondition condition, double sizeOffset = 0.0);
	static double Price(PartType type, PartQuality quality, PartCondition condition);
	// What buying this quality of this part gets you, line by line, for the store to explain.
	static std::vector<std::string> QualityNotes(PartType type, PartQuality quality);
	// The parts that come with this one: a rod's cap and a block's main caps (matched to it), and a
	// bearing's lower shell.
	static std::vector<pPart> CreateCompanions(const pPart& part, const EngineSpec& spec);
	// What one costs in the store, with its companions.
	static double PriceWithCompanions(PartType type, PartQuality quality, PartCondition condition, const EngineSpec& spec);
private:
	static double Spread(PartQuality quality);
	static double Sample(const Tolerance& tolerance, PartQuality quality);
	static double SmallDeviation(double limit, PartQuality quality);
	static void ShapeRound(Feature& feature, double size, double taper, double outOfRound);
	static Feature MakeFeature(const std::string& name, FeatureKind kind, const Tolerance& spec);
	static std::string SizeLabel(double sizeOffset);

	static pPart CreateBlock(const EngineSpec& spec, PartQuality quality, PartCondition condition);
	static pPart CreateCrankshaft(const EngineSpec& spec, PartQuality quality, PartCondition condition);
	static pPart CreateConnectingRod(const EngineSpec& spec, PartQuality quality, PartCondition condition);
	static pPart CreatePiston(const EngineSpec& spec, PartQuality quality, PartCondition condition, double sizeOffset);
	static pPart CreatePistonRing(const EngineSpec& spec, PartQuality quality, PartCondition condition, double sizeOffset);
	static pPart CreateBearing(PartType type, const EngineSpec& spec, PartQuality quality, PartCondition condition, double sizeOffset);
	static pPart CreateCylinderHead(const EngineSpec& spec, PartQuality quality, PartCondition condition);
	static pPart CreateHeadGasket(const EngineSpec& spec, PartQuality quality, PartCondition condition);
	static pPart CreateCamshaft(const EngineSpec& spec, PartQuality quality, PartCondition condition);
	static pPart CreateValve(bool exhaust, const EngineSpec& spec, PartQuality quality, PartCondition condition, double sizeOffset);
	static pPart CreateValveSpring(const EngineSpec& spec, PartQuality quality, PartCondition condition);
	// Everything past the long block's core parts: seals, gaskets, the timing drive, oiling,
	// cooling, fuel, ignition and exhaust.
	static pPart CreateAncillary(PartType type, const EngineSpec& spec, PartQuality quality, PartCondition condition);
};
