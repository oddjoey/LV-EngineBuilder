#include "MeasuringTool.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

#include "Random.h"
#include "Skills.h"

bool MeasuringTool::CanMeasure(const Feature& feature) const
{
	if (std::find(kinds.begin(), kinds.end(), feature.kind) == kinds.end())
		return false;

	double size = feature.spec.nominal;
	if (minSize > 0.0 && size < minSize)
		return false;
	if (maxSize > 0.0 && size > maxSize)
		return false;
	return true;
}

bool MeasuringTool::CanCheck(AssemblyCheck check) const
{
	return std::find(assemblyChecks.begin(), assemblyChecks.end(), check) != assemblyChecks.end();
}

double MeasuringTool::Read(double trueValue, double scatter) const
{
	double reading = Random::Normal(trueValue, sigma * scatter);
	if (resolution > 0.0)
		reading = std::round(reading / resolution) * resolution;
	return reading;
}

double MeasuringTool::TurnSeconds() const
{
	switch (handKind)
	{
		case HandToolKind::WRENCHES:
		case HandToolKind::COMBINATION_WRENCH:	return 4.0;
		case HandToolKind::BREAKER_BAR:		return 3.0;
		case HandToolKind::TORQUE_WRENCH:	return 2.5;
		case HandToolKind::RATCHET:			return powered ? 1.0 : 2.0;
		case HandToolKind::IMPACT:			return 0.4;
		default:							return 0.0;
	}
}

namespace ToolCatalog
{
	const std::vector<MeasuringTool>& All()
	{
		static const std::vector<MeasuringTool> tools = [] {
			std::vector<MeasuringTool> list;

			MeasuringTool caliper;
			caliper.name = "Digital Caliper";
			caliper.description = "Quick and cheap. Fine for lengths, useless for clearances.";
			caliper.kinds = { FeatureKind::JOURNAL, FeatureKind::LENGTH, FeatureKind::THICKNESS };
			caliper.resolution = 0.01;
			caliper.sigma = 0.010;
			caliper.price = 40.0;
			list.push_back(caliper);

			MeasuringTool micrometer;
			micrometer.name = "Outside Micrometer Set";
			micrometer.description = "Reads to a micron. Journals, skirts, shells and lengths.";
			micrometer.kinds = { FeatureKind::JOURNAL, FeatureKind::LENGTH, FeatureKind::THICKNESS };
			micrometer.resolution = 0.001;
			micrometer.sigma = 0.0012;
			micrometer.price = 350.0;
			list.push_back(micrometer);

			MeasuringTool telescoping;
			telescoping.name = "Telescoping Gauge";
			telescoping.description = "Feel-dependent bore measurement. Good enough to spot wear.";
			telescoping.kinds = { FeatureKind::BORE };
			telescoping.minSize = 12.0;
			telescoping.resolution = 0.01;
			telescoping.sigma = 0.006;
			telescoping.price = 60.0;
			list.push_back(telescoping);

			MeasuringTool boreGauge;
			boreGauge.name = "Dial Bore Gauge";
			boreGauge.description = "Zeroed off a setting ring. The right way to measure a bore.";
			boreGauge.kinds = { FeatureKind::BORE };
			boreGauge.minSize = 18.0;
			boreGauge.resolution = 0.001;
			boreGauge.sigma = 0.0015;
			boreGauge.price = 450.0;
			list.push_back(boreGauge);

			MeasuringTool smallHole;
			smallHole.name = "Small Hole Gauge Set";
			smallHole.description = "Split-ball gauges for valve guides. Feel it, lock it, mic it.";
			smallHole.kinds = { FeatureKind::BORE };
			smallHole.maxSize = 13.0;
			smallHole.resolution = 0.005;
			smallHole.sigma = 0.004;
			smallHole.price = 45.0;
			list.push_back(smallHole);

			MeasuringTool smallBore;
			smallBore.name = "Dial Small-Bore Gauge";
			smallBore.description = "Indicating gauge for guides: reads bellmouth top and bottom in one go.";
			smallBore.kinds = { FeatureKind::BORE };
			smallBore.maxSize = 18.0;
			smallBore.resolution = 0.001;
			smallBore.sigma = 0.0012;
			smallBore.price = 380.0;
			list.push_back(smallBore);

			MeasuringTool runout;
			runout.name = "Runout Gauge & V-Blocks";
			runout.description = "Dial test indicator for valve face and seat runout.";
			runout.kinds = { FeatureKind::RUNOUT };
			runout.resolution = 0.002;
			runout.sigma = 0.002;
			runout.price = 140.0;
			list.push_back(runout);

			MeasuringTool springTester;
			springTester.name = "Valve Spring Tester";
			springTester.description = "Squeezes a spring to installed height and reads the force.";
			springTester.kinds = { FeatureKind::FORCE };
			springTester.resolution = 1.0;
			springTester.sigma = 1.5;
			springTester.price = 250.0;
			list.push_back(springTester);

			MeasuringTool straightedge;
			straightedge.name = "Straightedge & Feelers";
			straightedge.description = "Lay it across the deck in a star pattern and find the gap.";
			straightedge.kinds = { FeatureKind::FLATNESS };
			straightedge.resolution = 0.01;
			straightedge.sigma = 0.005;
			straightedge.price = 80.0;
			list.push_back(straightedge);

			MeasuringTool burette;
			burette.name = "Burette & CC Plate";
			burette.description = "CC the chambers and dishes for a real compression ratio.";
			burette.kinds = { FeatureKind::VOLUME };
			burette.resolution = 0.1;
			burette.sigma = 0.05;
			burette.price = 90.0;
			list.push_back(burette);

			MeasuringTool profilometer;
			profilometer.name = "Surface Profilometer";
			profilometer.description = "Drags a stylus across the surface to read its finish (Ra).";
			profilometer.measuresFinish = true;
			profilometer.resolution = 0.01;
			profilometer.sigma = 0.02;
			profilometer.price = 1500.0;
			list.push_back(profilometer);

			MeasuringTool plastigauge;
			plastigauge.name = "Plastigauge";
			plastigauge.description = "Crushable thread: lay a strip across the bare journal, torque the cap (and lower shell) down over it, then take the cap off and compare the squashed width to the card. Don't turn the crank while it's in.";
			plastigauge.assemblyChecks = { AssemblyCheck::OIL_CLEARANCE, AssemblyCheck::CAM_CLEARANCE };
			plastigauge.resolution = 0.0125;
			plastigauge.sigma = 0.005;
			plastigauge.price = 15.0;
			list.push_back(plastigauge);

			MeasuringTool feeler;
			feeler.name = "Feeler Gauge Set";
			feeler.description = "Square the ring in its bore and find the blade that just slides. Also slips between a rod's big end and the crank cheek for its side clearance.";
			feeler.assemblyChecks = { AssemblyCheck::RING_GAP, AssemblyCheck::ROD_SIDE_CLEARANCE };
			feeler.resolution = 0.025;
			feeler.sigma = 0.004;
			feeler.price = 25.0;
			list.push_back(feeler);

			MeasuringTool dialIndicator;
			dialIndicator.name = "Dial Indicator & Deck Bridge";
			dialIndicator.description = "Find TDC and read how far the piston sits below the deck.";
			dialIndicator.assemblyChecks = { AssemblyCheck::DECK_CLEARANCE };
			dialIndicator.resolution = 0.01;
			dialIndicator.sigma = 0.004;
			dialIndicator.price = 120.0;
			list.push_back(dialIndicator);

			MeasuringTool heightMic;
			heightMic.name = "Installed Height Micrometer";
			heightMic.description = "Sits on the spring seat under a closed valve's retainer.";
			heightMic.assemblyChecks = { AssemblyCheck::INSTALLED_HEIGHT };
			heightMic.resolution = 0.01;
			heightMic.sigma = 0.008;
			heightMic.price = 90.0;
			list.push_back(heightMic);

			// Hand tools, for every nut and bolt on the engine. (After the measuring tools, so saved
			// tool lists keep their order.)
			auto hand = [&](const char* name, const char* description, HandToolKind kind, double price, std::vector<int> sizes = {},
				double torqueError = 0.0, double maxTorque = 0.0) {
				MeasuringTool tool;
				tool.name = name;
				tool.description = description;
				tool.handKind = kind;
				tool.price = price;
				tool.sizes = std::move(sizes);
				tool.torqueError = torqueError;
				tool.maxTorque = maxTorque;
				list.push_back(tool);
			};
			hand("3/8in Ratchet", "Drives the sockets. Quick for running bolts in and out, but it can't tell you how tight they are.",
				HandToolKind::RATCHET, 35.0);
			hand("1/2in Breaker Bar", "A long bar for cracking loose the big, tight ones, like the crank pulley bolt.", HandToolKind::BREAKER_BAR, 45.0);
			hand("Metric Socket Set 8-19 mm", "Six-point sockets: 8, 10, 12, 13, 14, 15, 17 and 19 mm. Most of the engine's nuts and bolts.",
				HandToolKind::SOCKETS, 90.0, { 8, 10, 12, 13, 14, 15, 17, 19 });
			hand("Large Socket Set 21-36 mm", "Big sockets: 21, 22, 24, 27, 30, 32 and 36 mm, for the crank pulley bolt and the like.",
				HandToolKind::SOCKETS, 70.0, { 21, 22, 24, 27, 30, 32, 36 });
			hand("Combination Wrench Set 8-19 mm", "Open end and ring spanners, 8 to 19 mm, in their tray: take out the one a nut or bolt needs. Turns it without a ratchet, but slowly, and with no torque reading.",
				HandToolKind::WRENCHES, 80.0, { 8, 10, 12, 13, 14, 15, 17, 19 });
			hand("Beam Torque Wrench", "A needle against a scale. Cheap, but only good to about 10%.", HandToolKind::TORQUE_WRENCH, 45.0, {}, 0.10, 150.0);
			hand("Click Torque Wrench", "Clicks at the set torque. Good to about 4%, up to 210 Nm.", HandToolKind::TORQUE_WRENCH, 160.0, {}, 0.04, 210.0);
			hand("Digital Torque Wrench", "Beeps at the set torque. Good to about 2%, up to 340 Nm.", HandToolKind::TORQUE_WRENCH, 340.0, {}, 0.02, 340.0);

			// The factory service manual: held and read (left click), not used on anything.
			MeasuringTool manual;
			manual.name = "LV-16 Factory Service Manual";
			manual.description = "The workshop book for the LV-16: every blueprint dimension and clearance, the torque for every bolt, and the "
				"whole job step by step, from tearing down a worn core to the dyno. Hold it and left click to read.";
			manual.manual = true;
			manual.price = 60.0;
			list.push_back(manual);

			// Added after the manual, so saved tool lists keep their order.
			MeasuringTool square;
			square.name = "Steel Square";
			square.description = "Stand a valve spring against it on the bench, turn it, and measure the widest gap at the top: no more than 2 mm out of square.";
			square.kinds = { FeatureKind::SQUARENESS };
			square.resolution = 0.1;
			square.sigma = 0.05;
			square.price = 25.0;
			list.push_back(square);

			MeasuringTool aligner;
			aligner.name = "Connecting Rod Aligner";
			aligner.description = "Hangs a rod on a mandrel through its big end, with a V-block on the small end's pin: feelers under the V-block read bend, then twist, per 100 mm of length.";
			aligner.kinds = { FeatureKind::ALIGNMENT };
			aligner.resolution = 0.01;
			aligner.sigma = 0.005;
			aligner.price = 180.0;
			list.push_back(aligner);

			// Cordless tools, their battery packs and chargers. (Added last, so saved tool lists keep their
			// order.) Every cordless tool takes every pack.
			auto cordless = [&](const char* name, const char* description, HandToolKind kind, double price, double maxTorque, double drawAh) {
				MeasuringTool tool;
				tool.name = name;
				tool.description = description;
				tool.handKind = kind;
				tool.price = price;
				tool.powered = true;
				tool.maxTorque = maxTorque;
				tool.drawAh = drawAh;
				list.push_back(tool);
			};
			cordless("3/8in Cordless Ratchet", "Drives the sockets with a motor: twice as quick as the hand ratchet, but it stalls past 70 Nm. "
				"Runs off a battery pack.", HandToolKind::RATCHET, 160.0, 70.0, 0.03);
			cordless("1/2in Cordless Impact Wrench", "Hammers nuts and bolts in and out faster than anything, up to 400 Nm. It runs them up "
				"tighter than it should, so finish with a torque wrench. Runs off a battery pack.", HandToolKind::IMPACT, 260.0, 400.0, 0.06);
			auto battery = [&](const char* name, double price, double capacityAh) {
				MeasuringTool tool;
				tool.name = name;
				tool.description = std::format("An 18 V pack for the cordless tools, holding {:.0f} Ah: the bigger it is, the longer it lasts "
					"between charges, and the longer it takes to charge.", capacityAh);
				tool.handKind = HandToolKind::BATTERY;
				tool.price = price;
				tool.capacityAh = capacityAh;
				list.push_back(tool);
			};
			battery("18V 2.0 Ah Battery", 45.0, 2.0);
			battery("18V 4.0 Ah Battery", 80.0, 4.0);
			battery("18V 6.0 Ah Battery", 120.0, 6.0);
			auto charger = [&](const char* name, const char* description, double price, double amps) {
				MeasuringTool tool;
				tool.name = name;
				tool.description = description;
				tool.handKind = HandToolKind::CHARGER;
				tool.price = price;
				tool.chargeAmps = amps;
				list.push_back(tool);
			};
			charger("Battery Charger 2 A", "Charges one battery pack at 2 A: a 4 Ah pack takes two minutes. Set it down somewhere, then hold a "
				"pack and press E on it.", 40.0, 2.0);
			charger("Rapid Charger 6 A", "Charges one battery pack at 6 A, three times as fast as the standard charger.", 110.0, 6.0);

			// Added after the cordless tools.
			MeasuringTool beltGauge;
			beltGauge.name = "Belt Tension Gauge";
			beltGauge.description = "Pushes the middle of a belt's run with a set load (2 kg for the timing belt, 10 kg for the fan belt) and reads how far it gives.";
			beltGauge.assemblyChecks = { AssemblyCheck::BELT_DEFLECTION };
			beltGauge.resolution = 0.5;
			beltGauge.sigma = 0.2;
			beltGauge.price = 45.0;
			list.push_back(beltGauge);

			// The combination wrenches, one tool each, so one at a time comes out of the set and goes on a nut
			// or bolt. They come with the set (and only with it).
			size_t wrenchSet = 0;
			for (size_t i = 0; i < list.size(); i++)
				if (list[i].handKind == HandToolKind::WRENCHES)
					wrenchSet = i;
			for (int size : list[wrenchSet].sizes)
			{
				MeasuringTool wrench;
				wrench.name = std::format("{} mm Combination Wrench", size);
				wrench.description = std::format("An open end and a ring, both {} mm. Out of the combination wrench set.", size);
				wrench.handKind = HandToolKind::COMBINATION_WRENCH;
				wrench.sizes = { size };
				wrench.partOf = static_cast<int>(wrenchSet);
				list.push_back(wrench);
			}

			// Paint, for decorating the shop: markers draw on the walls and floor, spray cans spray them
			// (or coat a part or a tool all over), and a rag with thinner wipes it off. (Added after the
			// wrenches, so saved tool lists keep their order.)
			auto paint = [&](HandToolKind kind, const std::string& name, const std::string& description, double price, uint32_t color) {
				MeasuringTool tool;
				tool.name = name;
				tool.description = description;
				tool.handKind = kind;
				tool.price = price;
				tool.paintColor = color;
				list.push_back(tool);
			};
			for (auto [color, rgb] : std::initializer_list<std::pair<const char*, uint32_t>>{
				{ "Black", 0x1A1A1A }, { "White", 0xF2F2EE }, { "Red", 0xC8231E }, { "Silver", 0xB4B8BC }, { "Yellow", 0xF0C814 } })
			{
				std::string lower = color;
				lower[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[0])));
				paint(HandToolKind::MARKER, std::format("Paint Marker ({})", color), std::format("A {} paint marker. Aim at a wall or the floor and hold the left button "
					"to draw; the mouse wheel sets how thick the line is.", lower), 6.0, rgb);
			}
			for (auto [color, rgb] : std::initializer_list<std::pair<const char*, uint32_t>>{
				{ "Red", 0xC8231E }, { "Black", 0x1A1A1A }, { "White", 0xF2F2EE }, { "Blue", 0x1E5AC8 }, { "Green", 0x28962D },
				{ "Yellow", 0xF0C814 }, { "Orange", 0xE66E14 }, { "Purple", 0x7832AA }, { "Pink", 0xE65A9B }, { "Silver", 0xB4B8BC } })
				paint(HandToolKind::SPRAY_CAN, std::format("Spray Paint ({})", color), "A can of spray paint. Hold the left button to spray "
					"the wall or floor it's pointed at (the mouse wheel sets how wide), or to give a part or tool a coat of it.", 9.0, rgb);
			paint(HandToolKind::THINNER, "Rag & Paint Thinner", "Hold the left button to wipe paint off the walls and floor (the mouse wheel sets how wide), or strip a "
				"coat off a part or tool.", 12.0, 0);
			// The Showroom Paint perk's colours.
			for (auto [color, rgb] : std::initializer_list<std::pair<const char*, uint32_t>>{
				{ "Gold", 0xD4A52A }, { "Chrome", 0xDCE0E6 }, { "Candy Teal", 0x12A89A } })
			{
				paint(HandToolKind::SPRAY_CAN, std::format("Spray Paint ({})", color), "Showroom spray paint. Hold the left button to spray "
					"the wall or floor it's pointed at (the mouse wheel sets how wide), or to give a part or tool a coat of it.", 18.0, rgb);
				list.back().skill = static_cast<int>(SkillKind::SHOWROOM_PAINT);
			}

			// The big combination wrenches, for the crank pulley bolt (22 mm) and the like, and the wrenches out
			// of the set. (After the paint, so saved tool lists keep their order.)
			hand("Combination Wrench Set 21-24 mm", "Big open end and ring spanners, 21, 22 and 24 mm, in their tray: the crank pulley bolt is 22. "
				"Slow, and with no torque reading.", HandToolKind::WRENCHES, 45.0, { 21, 22, 24 });
			size_t bigWrenchSet = list.size() - 1;
			for (int size : list[bigWrenchSet].sizes)
			{
				MeasuringTool wrench;
				wrench.name = std::format("{} mm Combination Wrench", size);
				wrench.description = std::format("An open end and a ring, both {} mm. Out of the big combination wrench set.", size);
				wrench.handKind = HandToolKind::COMBINATION_WRENCH;
				wrench.sizes = { size };
				wrench.partOf = static_cast<int>(bigWrenchSet);
				list.push_back(wrench);
			}

			// The press for the piston pins (a piston goes onto its rod off the engine, held by its pin) and the compressor
			// that squeezes its ring in to go down the bore. (Last, so saved tool lists keep their order.)
			hand("Piston Pin Press", "A C-frame press with a screw and a pusher: with the piston sat on its rod, hold the left button to press "
				"a pin through the piston and the rod's small end, the right button to press it out again.", HandToolKind::PIN_PRESS, 40.0);
			hand("Piston Ring Compressor", "A spring-steel band ratcheted round the piston: with a rod and piston sat in the top of its bore, hold "
				"the left button on the piston to squeeze its ring into its groove and tap it home, the rod onto the crank pin.", HandToolKind::RING_COMPRESSOR, 25.0);

			// The project car's tools. (Last again, for the same reason.)
			hand("Fluid Funnel", "A funnel and a jug. At a filler on the car, hold the left button to pour in the fluid picked (R picks another "
				"from what's in stock), the right button to drain the circuit into the pan.", HandToolKind::FUNNEL, 8.0);
			hand("Brake Bleeder Kit", "A clear hose and a catch bottle for a bleed nipple. Click on a caliper, wheel cylinder or the clutch release "
				"cylinder: someone pumps the pedal, the nipple's cracked, and you watch the bubbles. Keep the reservoir topped up.",
				HandToolKind::BRAKE_BLEEDER, 18.0);
			hand("Strut Spring Compressor", "Two hooked screws that squeeze a coil spring short. Aimed at a loose front strut, hold the left button to "
				"compress a spring onto it (one lying nearby), the right to take its spring off. Never undo a strut's top nut without it.",
				HandToolKind::SPRING_COMPRESSOR, 55.0);
			hand("Tire Irons & Bead Breaker", "Aimed at a loose wheel, hold the left button to lever a tire lying nearby onto it, the right to break the "
				"bead and take its tire off.", HandToolKind::TIRE_IRONS, 40.0);
			hand("Timing Light", "An inductive strobe clipped on the number one plug lead. With the engine running, aim it at the crank pulley's "
				"marks to read the timing; the buttons turn the distributor, left to advance, right to retard.", HandToolKind::TIMING_LIGHT, 65.0);
			hand("Tire Gauge & Inflator", "Aimed at a tire, it reads the pressure. Hold the left button to pump it up off the compressor, the right "
				"to let some out.", HandToolKind::TIRE_GAUGE, 25.0);
			hand("Camber & Toe Gauge", "A magnetic camber gauge and a toe bar. Aimed at a front wheel it reads its camber, caster and the toe; the "
				"buttons turn the tie rods, left for more toe in, right for less.", HandToolKind::ALIGNMENT_GAUGE, 120.0);
			hand("Flat Screwdriver", "For the carburettor's idle speed and mixture screws with the engine running, and, with the valve cover off, "
				"the rockers' lash adjusters (a feeler gauge reads the gap). Left turns a screw in, right out.", HandToolKind::SCREWDRIVER, 6.0);

			// The car's own book, for the project car: added last, so saved tool lists keep their order.
			MeasuringTool chassisManual;
			chassisManual.name = "LE86 Chassis Service Manual";
			chassisManual.description = "The factory book for the MonkeyKobo Comet coupe round the engine: the body, suspension, steering, brakes, "
				"drivetrain, fuel, electrics and cooling, every fluid and its capacity, the torque for every chassis bolt, the tune-up and "
				"alignment figures, and the build from a bare shell to the road test. Hold it and left click to read.";
			chassisManual.manual = true;
			chassisManual.manualBook = 1;
			chassisManual.price = 45.0;
			list.push_back(chassisManual);
			hand("Valve Spring Compressor", "A C-clamp for the head off the engine: its cup over a valve's spring, its foot under the valve. Aim it "
				"at a valve with its spring in and press E: it squeezes the spring down so the retainer and keepers go on (or come off). Then E "
				"on the compressor takes it off, for the next valve.", HandToolKind::VALVE_SPRING_COMPRESSOR, 35.0);

			// A new shop starts with nothing but combination wrenches for every nut and bolt, done up by feel,
			// and the service manual (hung on the tool board with the rest). Everything else is bought, the pin press and
			// ring compressor included (the starting money covers those).
			for (auto& tool : list)
				for (const char* starter : { "Combination Wrench Set 8-19 mm", "Combination Wrench Set 21-24 mm", "LV-16 Factory Service Manual" })
					if (tool.name == starter)
						tool.ownedAtStart = true;

			// Ratchets, sockets and whatever drives a socket wait for the Sockets & Ratchets perk, but for the
			// beam torque wrench and the sockets it needs: from the start, a bolt can be torqued to spec.
			for (auto& tool : list)
			{
				bool drivesSockets = tool.handKind == HandToolKind::RATCHET || tool.handKind == HandToolKind::BREAKER_BAR || tool.handKind == HandToolKind::SOCKETS
					|| tool.handKind == HandToolKind::TORQUE_WRENCH || tool.handKind == HandToolKind::IMPACT;
				bool fromStart = tool.name == "Beam Torque Wrench" || tool.name == "Metric Socket Set 8-19 mm";
				if (drivesSockets && !fromStart)
					tool.skill = static_cast<int>(SkillKind::SOCKETS);
			}

			// The better tools wait for the shop's rank: precision gauges, the better torque wrenches, and
			// cordless power (with the batteries and chargers that go with it).
			for (auto& tool : list)
				for (auto [name, rank] : std::initializer_list<std::pair<const char*, int>>{
					{ "Outside Micrometer Set", 2 }, { "Valve Spring Tester", 2 }, { "Dial Indicator & Deck Bridge", 2 },
					{ "Installed Height Micrometer", 2 }, { "Click Torque Wrench", 2 },
					{ "Dial Bore Gauge", 3 }, { "Dial Small-Bore Gauge", 3 }, { "3/8in Cordless Ratchet", 3 },
					{ "18V 2.0 Ah Battery", 3 }, { "Battery Charger 2 A", 3 },
					{ "Digital Torque Wrench", 4 }, { "1/2in Cordless Impact Wrench", 4 }, { "18V 4.0 Ah Battery", 4 },
					{ "Surface Profilometer", 5 }, { "18V 6.0 Ah Battery", 5 }, { "Rapid Charger 6 A", 5 } })
					if (tool.name == name)
						tool.rank = rank;

			return list;
		}();
		return tools;
	}
}
