#pragma once

#include <string>

#include "Material.h"
#include "Tolerance.h"

// The factory service manual for one engine family: every blueprint dimension and the
// assembled clearances the builder is aiming for.
struct EngineSpec
{
	std::string name = "";
	int cylinders = 0;
	int mainJournals = 0;
	int redline = 0;

	// Block
	Tolerance bore = {};
	Tolerance mainHousing = {};
	Tolerance deckHeight = {};
	double maxBoreTaper = 0.0;
	double maxBoreOutOfRound = 0.0;
	double maxDeckFlatness = 0.0;
	Range boreFinish = {};

	// Crankshaft
	Tolerance mainJournal = {};
	Tolerance rodJournal = {};
	Tolerance stroke = {};
	double maxJournalTaper = 0.0;
	double maxJournalOutOfRound = 0.0;
	double maxJournalFinish = 0.0;

	// Connecting rods
	Tolerance rodHousing = {};
	Tolerance rodLength = {};

	// Pistons and rings
	Tolerance pistonSkirt = {};
	Tolerance compressionHeight = {};
	Tolerance dishVolume = {};
	double ringClosedDiameter = 0.0;

	// Bearings
	Tolerance mainShell = {};
	Tolerance rodShell = {};

	// Head and gasket
	Tolerance chamberVolume = {};
	double maxHeadFlatness = 0.0;
	Tolerance gasketThickness = {};
	double gasketBore = 0.0;

	// Valvetrain: single overhead cam, one intake and one exhaust valve per cylinder.
	int camJournals = 0;
	Tolerance camBore = {};
	Tolerance camJournal = {};
	Tolerance lobeLift = {};
	Tolerance guideBore = {};
	double maxGuideTaper = 0.0;			// bellmouthing
	double maxGuideOutOfRound = 0.0;
	Tolerance intakeStem = {};
	Tolerance exhaustStem = {};
	Range seatWidth = {};
	double maxSeatRunout = 0.0;
	double maxValveRunout = 0.0;
	Tolerance valveMargin = {};
	double minValveMargin = 0.0;		// below this the valve burns
	Tolerance valveLength = {};
	double installedHeight = 0.0;		// nominal, with a new valve in a fresh seat
	Tolerance springFreeLength = {};
	Tolerance springSeatPressure = {};	// measured at the nominal installed height
	double springRate = 0.0;			// N/mm

	// Assembly targets
	Range camClearance = {};
	Range intakeStemClearance = {};
	Range exhaustStemClearance = {};
	Range installedHeightRange = {};
	Range seatPressure = {};
	Range pistonToWall = {};
	Range mainClearance = {};
	Range rodClearance = {};
	Range ringGap = {};
	Range quench = {};
	Range compressionRatio = {};

	// Piston alloys grow differently when hot, so the cold clearance target depends on the piston.
	Range PistonToWallFor(MaterialType pistonMaterial) const;

	static const EngineSpec& InlineFour();
};
