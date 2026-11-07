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
	// What the factory rated a stock engine at, on the dyno sheet beside the pull.
	double ratedPowerHp = 0.0;
	int ratedPowerRpm = 0;
	double ratedTorqueNm = 0.0;
	int ratedTorqueRpm = 0;

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
	double maxRodBend = 0.0;			// per 100 mm of length, on the rod aligner
	double maxRodTwist = 0.0;
	Tolerance rodBigEndWidth = {};		// across the big end's thrust faces
	Tolerance rodJournalWidth = {};		// the crank pin, cheek to cheek

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
	double maxPortFaceFlatness = 0.0;		// the head's manifold faces
	double maxIntakeManifoldFlatness = 0.0;
	double maxResurface = 0.0;				// metal that may come off a block or head deck in all
	Tolerance gasketThickness = {};
	double gasketBore = 0.0;

	// Valvetrain: single overhead cam, one intake and one exhaust valve per cylinder.
	int camJournals = 0;
	Tolerance camBore = {};
	Tolerance camJournal = {};
	Tolerance lobeLift = {};			// what the valve sees: lobe height less the base circle
	Tolerance lobeHeight = {};			// what the micrometer reads, across the nose and base circle
	double minLobeHeight = 0.0;
	Range camEndPlay = {};
	double maxCamEndPlay = 0.0;
	double maxCamRunout = 0.0;			// bend, read on the centre journal in V-blocks
	Tolerance fuelPumpEccentric = {};	// the lobe that works the fuel pump's arm, miked like the others
	double minFuelPumpEccentric = 0.0;
	double CamBaseCircle() const { return lobeHeight.nominal - lobeLift.nominal; }
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
	Tolerance springSeatPressure = {};	// measured at the nominal installed height; below its minimum, replace it
	double springRate = 0.0;			// N/mm
	double maxSpringSquareness = 0.0;	// how far its top may lean off true against a square
	Tolerance rockerBore = {};
	Tolerance rockerShaft = {};

	// Assembly targets
	Range camClearance = {};
	double maxCamClearance = 0.0;		// past this, replace the cam and the head
	Range intakeStemClearance = {};
	Range exhaustStemClearance = {};
	// Worn past these, the guide (or the valve) is replaced.
	double maxIntakeStemClearance = 0.0;
	double maxExhaustStemClearance = 0.0;
	Range rockerClearance = {};
	double maxRockerClearance = 0.0;	// past this, replace the arm or the shaft
	Range installedHeightRange = {};
	Range seatPressure = {};
	Range pistonToWall = {};
	Range mainClearance = {};
	Range rodClearance = {};
	Range rodSideClearance = {};		// big end side play on the crank pin
	double maxRodSideClearance = 0.0;	// past this, replace the rod
	// Belt deflection, mm, pushed in the middle of a run: the timing belt under 2 kg, the fan belt under
	// 10 kg (looser once it's run in).
	Range timingBeltDeflection = {};
	Range fanBeltDeflectionNew = {};
	Range fanBeltDeflectionUsed = {};
	Range ringGap = {};
	Range quench = {};
	Range compressionRatio = {};

	// Piston alloys grow differently when hot, so the cold clearance target depends on the piston.
	Range PistonToWallFor(MaterialType pistonMaterial) const;

	static const EngineSpec& InlineFour();
};
