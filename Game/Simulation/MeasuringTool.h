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
	INSTALLED_HEIGHT,	// spring seat to retainer, valve held closed
	ROD_SIDE_CLEARANCE,	// rod on its crank pin, feeler gauge between the big end and the crank cheek
	BELT_DEFLECTION		// how far a belt's run gives under a set push: index 0 the timing belt, 1 the fan belt
};

// Spanners and the like: what turns a nut or bolt, and what says how tight it is.
enum class HandToolKind
{
	NONE,			// a measuring tool
	SOCKETS,		// a set of sockets, needs a ratchet or breaker bar to drive them
	WRENCHES,		// a set of combination wrenches: the tray they come in (each wrench is a tool of its own)
	RATCHET,
	BREAKER_BAR,
	TORQUE_WRENCH,	// drives sockets and sets torque
	IMPACT,			// a cordless impact wrench: drives sockets, the quickest of all, off a battery
	BATTERY,		// a battery pack for the cordless tools
	CHARGER,		// charges a battery pack set on it
	COMBINATION_WRENCH	// one wrench out of its set, used on its own
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
	// Comes in a set (the set's tool index): owned with it, sold only with it, and kept in it.
	int partOf = -1;
	double torqueError = 0.0;		// torque wrenches: 1 standard deviation, as a fraction of the setting
	double maxTorque = 0.0;			// torque wrenches: Nm; cordless tools: the most they can turn
	// Cordless tools run off a battery pack (see ShopWorld), using this much of it (Ah) for each nut or
	// bolt turned, more for the tight ones.
	bool powered = false;
	double drawAh = 0.0;
	double capacityAh = 0.0;		// battery packs: how much they hold
	double chargeAmps = 0.0;		// chargers: how fast they charge
	// A book (the factory service manual): held and read, used on nothing.
	bool manual = false;
	bool IsHandTool() const { return handKind != HandToolKind::NONE; }
	// Spanners and the like: how many seconds the button is held to run a typical nut or bolt in or
	// out. A combination wrench is the slowest, a ratchet quicker, a cordless impact quickest.
	double TurnSeconds() const;

	bool CanMeasure(const Feature& feature) const;
	bool CanCheck(AssemblyCheck check) const;
	// A reading as the tool would display it: true value plus error, snapped to the resolution.
	double Read(double trueValue) const;
};

namespace ToolCatalog
{
	const std::vector<MeasuringTool>& All();

	// A shop can own several of a tool: each one in the world is known by an id, its catalog index plus
	// COPY_STRIDE for each copy before it (the first copy's id is its index).
	constexpr size_t COPY_STRIDE = 4096;
	inline size_t IndexOf(size_t id) { return id % COPY_STRIDE; }
	inline size_t CopyOf(size_t id) { return id / COPY_STRIDE; }
	inline size_t IdOf(size_t index, size_t copy) { return index + copy * COPY_STRIDE; }
	// The tool an id (or a plain index) is a copy of.
	inline const MeasuringTool& Of(size_t id) { return All()[IndexOf(id)]; }
}
