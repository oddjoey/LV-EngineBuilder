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
		// Modelled on the Toyota 4A-C (1.6 SOHC, 81 x 77 mm, 9.0:1). From its spec sheet (4ac specs.pdf):
		// journals, oil clearances, piston clearance, top ring gap, valve stems, guides and seats, springs,
		// rockers and cam; from the 4A block family (club4ag.com): deck height, rod length, bore wear limits
		// and quench. The
		// housings, guide and cam bores, skirt and ring are sized from them so a mid-spec build lands
		// mid-spec; what neither gives (head, gasket, cam lift, valve sizes) is the LV-16's own.
		EngineSpec s;
		s.name = "LV-16 Inline-4";
		s.cylinders = 4;
		s.mainJournals = 5;
		s.redline = 5500;
		s.ratedPowerHp = 70.0;
		s.ratedPowerRpm = 4800;
		s.ratedTorqueNm = 115.0;	// 85 ft-lb
		s.ratedTorqueRpm = 2800;

		s.bore = Tolerance::Limits(81.000, 81.018);
		// Housing bores are the journal, plus both shells, plus a mid-spec oil clearance.
		s.mainHousing = Tolerance::Limits(52.000, 52.016);
		// 1.6 litres: an 81 mm bore over a 77 mm stroke. Crank radius + rod length + compression
		// height = 38.50 + 122.00 + 30.25 = 190.75, leaving the piston 0.25 below deck: with the gasket, 0.95 mm
		// of quench, mid-way in the 4A's 0.035 - 0.040 in.
		s.deckHeight = Tolerance::Bilateral(191.00, 0.05);
		s.maxBoreTaper = 0.020;
		s.maxBoreOutOfRound = 0.020;
		s.maxDeckFlatness = 0.050;
		s.boreFinish = { 0.20, 0.60 };

		s.mainJournal = Tolerance::Limits(47.985, 48.000);
		s.rodJournal = Tolerance::Limits(39.985, 40.000);
		s.stroke = Tolerance::Bilateral(77.00, 0.05);
		s.maxJournalTaper = 0.005;
		s.maxJournalOutOfRound = 0.005;
		s.maxJournalFinish = 0.25;

		s.rodHousing = Tolerance::Limits(43.000, 43.016);
		s.rodLength = Tolerance::Bilateral(122.00, 0.03);
		s.maxRodBend = 0.05;
		s.maxRodTwist = 0.05;
		// Side play 0.15 - 0.25 mm, replace past 0.30. The widths themselves are the LV-16's own, sized to give it.
		s.rodBigEndWidth = Tolerance::Limits(19.975, 20.000);
		s.rodJournalWidth = Tolerance::Limits(20.175, 20.200);

		s.pistonSkirt = Tolerance::Limits(80.892, 80.906);
		s.compressionHeight = Tolerance::Bilateral(30.25, 0.02);
		s.dishVolume = Tolerance::Bilateral(4.0, 0.2);
		// Gives roughly a 0.30 mm gap in a mid-tolerance bore.
		s.ringClosedDiameter = 80.914;

		s.mainShell = Tolerance::Limits(1.988, 1.994);
		s.rodShell = Tolerance::Limits(1.488, 1.494);

		// Sized for the 4A-C's 9.0:1 with a 4 cc dish, the piston 0.25 below deck and a 0.70 mm gasket.
		s.chamberVolume = Tolerance::Bilateral(40.6, 0.5);
		s.maxHeadFlatness = 0.050;
		s.maxPortFaceFlatness = 0.10;
		s.maxIntakeManifoldFlatness = 0.20;
		s.maxResurface = 0.10;
		s.gasketThickness = Tolerance::Bilateral(0.70, 0.02);
		s.gasketBore = 82.5;

		s.camJournals = 4;
		s.camBore = Tolerance::Limits(28.985, 29.006);
		s.camJournal = Tolerance::Limits(28.930, 28.950);
		// Lobe height 39.440 - 39.450 mm, worn out below 39.140. The lift (and so the base circle) is the LV-16's own.
		s.lobeLift = Tolerance::Bilateral(8.80, 0.005);
		s.lobeHeight = Tolerance::Limits(39.440, 39.450);
		s.minLobeHeight = 39.140;
		s.camEndPlay = { 0.080, 0.180 };
		s.maxCamEndPlay = 0.250;
		s.maxCamRunout = 0.10;
		// The fuel pump eccentric is the LV-16's own: worn below its minimum, the pump's stroke is too short.
		s.fuelPumpEccentric = Tolerance::Limits(34.960, 34.990);
		s.minFuelPumpEccentric = 34.700;
		s.guideBore = Tolerance::Limits(7.010, 7.030);
		s.maxGuideTaper = 0.010;
		s.maxGuideOutOfRound = 0.008;
		s.intakeStem = Tolerance::Limits(6.970, 6.985);
		s.exhaustStem = Tolerance::Limits(6.965, 6.980);
		s.seatWidth = { 1.20, 1.60 };
		s.maxSeatRunout = 0.025;
		s.maxValveRunout = 0.025;
		s.valveMargin = Tolerance::Limits(0.9, 1.3);
		s.minValveMargin = 0.7;
		s.valveLength = Tolerance::Bilateral(105.0, 0.10);
		s.installedHeight = 38.6;
		s.springFreeLength = Tolerance::Bilateral(44.60, 0.4);
		// 23.6 kg at 38.6 mm: 231 N, from 6.0 mm of compression. Below 21 kg (206 N) it's replaced.
		s.springSeatPressure = Tolerance::Limits(206.0, 256.0);
		s.springRate = 38.5;
		s.maxSpringSquareness = 2.0;
		// Rocker arm to shaft: 0.010 - 0.048 mm, replace past 0.060.
		s.rockerBore = Tolerance::Limits(16.000, 16.018);
		s.rockerShaft = Tolerance::Limits(15.970, 15.990);

		s.camClearance = { 0.037, 0.073 };
		s.maxCamClearance = 0.10;
		s.intakeStemClearance = { 0.025, 0.060 };
		s.exhaustStemClearance = { 0.030, 0.065 };
		s.maxIntakeStemClearance = 0.080;
		s.maxExhaustStemClearance = 0.10;
		s.rockerClearance = { 0.010, 0.048 };
		s.maxRockerClearance = 0.060;
		s.installedHeightRange = { 38.2, 39.2 };
		s.seatPressure = { 206.0, 265.0 };
		s.pistonToWall = { 0.10, 0.12 };
		s.mainClearance = { 0.012, 0.049 };
		s.rodClearance = { 0.020, 0.051 };
		s.rodSideClearance = { 0.15, 0.25 };
		s.maxRodSideClearance = 0.30;
		s.timingBeltDeflection = { 6.0, 7.0 };
		s.fanBeltDeflectionNew = { 8.5, 10.5 };
		s.fanBeltDeflectionUsed = { 10.0, 12.0 };
		s.ringGap = { 0.25, 0.35 };
		s.quench = { 0.89, 1.02 };
		s.compressionRatio = { 8.6, 9.6 };
		return s;
	}();
	return spec;
}
