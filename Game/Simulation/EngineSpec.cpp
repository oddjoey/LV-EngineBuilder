#include "EngineSpec.h"

Range EngineSpec::PistonToWallFor(MaterialType pistonMaterial) const
{
	switch (pistonMaterial)
	{
		case MaterialType::HYPEREUTECTIC_ALUMINUM:	return pistonToWall.Shifted(-0.005);
		case MaterialType::FORGED_2618:				return pistonToWall.Shifted(0.030);
		default:									return pistonToWall;
	}
}

const EngineSpec& EngineSpec::InlineFour()
{
	static const EngineSpec spec = [] {
		EngineSpec s;
		s.name = "LV-20 Inline-4";
		s.cylinders = 4;
		s.mainJournals = 5;
		s.redline = 7000;

		s.bore = Tolerance::Limits(86.000, 86.018);
		s.mainHousing = Tolerance::Limits(59.000, 59.019);
		// Crank radius + rod length + compression height = 216.75, leaving the piston 0.20 below deck.
		s.deckHeight = Tolerance::Bilateral(216.95, 0.05);
		s.maxBoreTaper = 0.010;
		s.maxBoreOutOfRound = 0.008;
		s.maxDeckFlatness = 0.050;
		s.boreFinish = { 0.20, 0.60 };

		s.mainJournal = Tolerance::Limits(54.976, 54.994);
		s.rodJournal = Tolerance::Limits(47.976, 47.992);
		s.stroke = Tolerance::Bilateral(86.00, 0.05);
		s.maxJournalTaper = 0.005;
		s.maxJournalOutOfRound = 0.005;
		s.maxJournalFinish = 0.25;

		s.rodHousing = Tolerance::Limits(51.000, 51.016);
		s.rodLength = Tolerance::Bilateral(143.75, 0.03);

		s.pistonSkirt = Tolerance::Limits(85.966, 85.980);
		s.compressionHeight = Tolerance::Bilateral(30.00, 0.02);
		s.dishVolume = Tolerance::Bilateral(4.0, 0.2);
		// Gives roughly a 0.38 mm gap in a mid-tolerance bore.
		s.ringClosedDiameter = 85.888;

		s.mainShell = Tolerance::Limits(1.988, 1.994);
		s.rodShell = Tolerance::Limits(1.488, 1.494);

		s.chamberVolume = Tolerance::Bilateral(46.0, 0.5);
		s.maxHeadFlatness = 0.050;
		s.gasketThickness = Tolerance::Bilateral(0.70, 0.02);
		s.gasketBore = 87.5;

		s.camJournals = 5;
		s.camBore = Tolerance::Limits(26.000, 26.021);
		s.camJournal = Tolerance::Limits(25.959, 25.980);
		s.lobeLift = Tolerance::Bilateral(8.80, 0.03);
		s.guideBore = Tolerance::Limits(7.000, 7.015);
		s.maxGuideTaper = 0.010;
		s.maxGuideOutOfRound = 0.008;
		s.intakeStem = Tolerance::Limits(6.975, 6.990);
		s.exhaustStem = Tolerance::Limits(6.955, 6.970);
		s.seatWidth = { 1.2, 1.7 };
		s.maxSeatRunout = 0.025;
		s.maxValveRunout = 0.025;
		s.valveMargin = Tolerance::Limits(0.9, 1.3);
		s.minValveMargin = 0.7;
		s.valveLength = Tolerance::Bilateral(105.0, 0.10);
		s.installedHeight = 38.0;
		s.springFreeLength = Tolerance::Bilateral(44.0, 0.4);
		s.springSeatPressure = Tolerance::Limits(255.0, 285.0);
		s.springRate = 45.0;

		s.camClearance = { 0.025, 0.075 };
		s.intakeStemClearance = { 0.015, 0.050 };
		s.exhaustStemClearance = { 0.030, 0.065 };
		s.installedHeightRange = { 37.6, 38.6 };
		s.seatPressure = { 235.0, 310.0 };
		s.pistonToWall = { 0.025, 0.050 };
		s.mainClearance = { 0.025, 0.065 };
		s.rodClearance = { 0.025, 0.065 };
		s.ringGap = { 0.30, 0.50 };
		s.quench = { 0.70, 1.30 };
		s.compressionRatio = { 9.5, 10.8 };
		return s;
	}();
	return spec;
}
