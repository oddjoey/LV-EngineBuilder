#pragma once

#include <string>
#include <vector>

#include "EngineSpec.h"
#include "Part.h"

struct Machine
{
	std::string name = "";
	std::string description = "";
	MachineOperation operation = MachineOperation::BORE;
	double sigma = 0.0;			// size error per cut, 1 standard deviation
	double finishRa = 0.0;		// surface finish left behind
	double maxRemoval = 0.0;	// most material one setup can take (0 = no limit)
	double price = 0.0;
	double hoursPerUse = 0.0;	// setting up, whatever the cut
	double hoursPerMm = 0.0;	// on top of setup, for each mm of diameter removed
	bool ownedAtStart = false;
};

namespace MachineCatalog
{
	const std::vector<Machine>& All();
}

struct MachiningResult
{
	bool success = false;
	std::string message = "";
	double removed = 0.0;		// mm of diameter actually taken off, where that applies
};

// Material only ever comes off. Every operation is irreversible: overbore a cylinder and the
// only way back is the next oversize piston, or a sleeve.
class Machining
{
public:
	static const char* OperationName(MachineOperation operation);
	// What the operation's parameter means, e.g. "Target diameter".
	static const char* ParameterName(MachineOperation operation);
	static bool UsesParameter(MachineOperation operation);

	static bool CanMachine(const Machine& machine, const Feature& feature);
	static MachiningResult Apply(const Machine& machine, Part& part, Feature& feature, double parameter, const EngineSpec& spec);
private:
	static MachiningResult CutBore(const Machine& machine, Feature& feature, double target, double sigma);
	static MachiningResult GrindJournal(const Machine& machine, Feature& feature, double target, double sigma);
	static MachiningResult Polish(const Machine& machine, Feature& feature);
	static MachiningResult Surface(const Machine& machine, Part& part, Feature& feature, double depth, double sigma, const EngineSpec& spec);
	static MachiningResult FileRing(const Machine& machine, Feature& feature, double gapIncrease, double sigma);
	static MachiningResult CutSeat(const Machine& machine, Part& part, Feature& width, double targetWidth, double sigma);
	static MachiningResult FaceValve(const Machine& machine, Part& part, Feature& faceRunout, double sigma);
};
