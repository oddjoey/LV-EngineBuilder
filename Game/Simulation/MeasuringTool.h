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
	COMBINATION_WRENCH,	// one wrench out of its set, used on its own
	MARKER,			// a paint marker: draws lines on the walls and floor
	SPRAY_CAN,		// spray paint: on the walls and floor, or a coat over a part or tool
	THINNER,		// a rag and paint thinner: wipes paint off
	PIN_PRESS,		// presses a piston pin into a piston on its rod, or out of it
	RING_COMPRESSOR,	// squeezes a piston's ring in, for it to go down the bore
	// The car's (appended)
	FUNNEL,				// pours fluid from the shop's stock in at a filler
	BRAKE_BLEEDER,		// a hose and catch bottle on a bleed nipple: one go at bleeding it
	SPRING_COMPRESSOR,	// squeezes a front spring to go on or come off its strut
	TIRE_IRONS,			// with the bead breaker: a tire onto its wheel, or off
	TIMING_LIGHT,		// reads the ignition timing off the crank pulley on a running engine
	TIRE_GAUGE,			// reads a tire's pressure, and its inflator pumps it up
	ALIGNMENT_GAUGE,	// reads a front wheel's camber, caster and toe
	SCREWDRIVER,		// the carburettor's idle speed and mixture screws, and the rockers' lash adjusters
	VALVE_SPRING_COMPRESSOR	// a C-clamp over a valve in a loose head: squeezes its spring down for the retainer and keepers
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
	bool ownedAtStart = false;
	// The shop rank (see ShopRank) the store sells it from: the better tools are earned.
	int rank = 1;
	// A perk (a SkillKind) the shop needs before the store sells it, or -1.
	int skill = -1;

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
	int manualBook = 0;			// which: 0 the engine's (LV-16), 1 the project car's chassis manual
	// Paint: the colour a marker or spray can puts on (RGB).
	uint32_t paintColor = 0;
	bool Paints() const { return handKind == HandToolKind::MARKER || handKind == HandToolKind::SPRAY_CAN || handKind == HandToolKind::THINNER; }
	bool IsHandTool() const { return handKind != HandToolKind::NONE; }
	// Spanners and the like: how many seconds the button is held to run a typical nut or bolt in or
	// out. A combination wrench is the slowest, a ratchet quicker, a cordless impact quickest.
	double TurnSeconds() const;

	bool CanMeasure(const Feature& feature) const;
	bool CanCheck(AssemblyCheck check) const;
	// A reading as the tool would display it: true value plus error, snapped to the resolution.
	// scatter scales its repeatability (Steady Hands makes it less than 1).
	double Read(double trueValue, double scatter = 1.0) const;
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
