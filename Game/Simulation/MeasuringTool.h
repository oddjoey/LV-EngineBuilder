#pragma once

#include <string>
#include <vector>

#include "Feature.h"

// Checks that only make sense once parts are fitted together.
enum class AssemblyCheck
{
	NONE,
	OIL_CLEARANCE,		// Plastigauge across a main or rod bearing
	CAM_CLEARANCE,		// Plastigauge across a cam journal, under its cam cap
	RING_GAP,			// ring squared in its bore, feeler gauge across the ends
	DECK_CLEARANCE,		// dial indicator over the piston at TDC
	INSTALLED_HEIGHT	// spring seat to retainer, valve held closed
};

// Spanners and the like: what turns a nut or bolt, and what says how tight it is.
enum class HandToolKind
{
	NONE,			// a measuring tool
	SOCKETS,		// a set of sockets, needs a ratchet or breaker bar to drive them
	WRENCHES,		// combination wrenches, used on their own
	RATCHET,
	BREAKER_BAR,
	TORQUE_WRENCH	// drives sockets and sets torque
};

struct MeasuringTool
{
	std::string name = "";
	std::string description = "";
	std::vector<FeatureKind> kinds = {};
	bool measuresFinish = false;
	std::vector<AssemblyCheck> assemblyChecks = {};
	// Diameter range the tool physically fits; 0 means no limit.
	double minSize = 0.0;
	double maxSize = 0.0;
	double resolution = 0.0;	// smallest increment the tool can show
	double sigma = 0.0;			// repeatability error, 1 standard deviation
	double price = 0.0;
	double hoursPerUse = 0.0;
	bool ownedAtStart = false;

	// Hand tools only.
	HandToolKind handKind = HandToolKind::NONE;
	std::vector<int> sizes = {};	// sockets and wrenches: across-flats sizes in mm
	double torqueError = 0.0;		// torque wrenches: 1 standard deviation, as a fraction of the setting
	double maxTorque = 0.0;			// torque wrenches: Nm
	// A book (the factory service manual): held and read, used on nothing.
	bool manual = false;
	bool IsHandTool() const { return handKind != HandToolKind::NONE; }

	bool CanMeasure(const Feature& feature) const;
	bool CanCheck(AssemblyCheck check) const;
	// A reading as the tool would display it: true value plus error, snapped to the resolution.
	double Read(double trueValue) const;
};

namespace ToolCatalog
{
	const std::vector<MeasuringTool>& All();
}
