#include "WorkshopUI.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "ItemIcons.h"
#include "MaterialText.h"
#include "Ratings.h"
#include "ModelPreview.h"
#include "../FrameProfiler.h"
#include "PartIcons.h"
#include "../Simulation/PartCatalog.h"
#include "../World/ShopAssets.h"
#include "../World/ToolShapes.h"

namespace
{
	// Not const: the store's light pages deepen them while it's drawn (WebsiteTheme).
	ImVec4 COLOR_GOOD = { 0.45f, 0.90f, 0.45f, 1.f };
	ImVec4 COLOR_WARN = { 1.00f, 0.80f, 0.30f, 1.f };
	ImVec4 COLOR_BAD = { 1.00f, 0.40f, 0.40f, 1.f };
	ImVec4 COLOR_DIM = { 0.55f, 0.55f, 0.55f, 1.f };
	ImVec4 COLOR_XRAY = { 0.55f, 0.75f, 1.00f, 1.f };

	const char* STATION_NAMES[Feature::STATIONS] = { "Top", "Middle", "Bottom" };
	const char* AXIS_NAMES[Feature::AXES] = { "Thrust", "Pin" };

	// What a measuring tool reads (FeatureKind), and the checks on an assembled engine it does (AssemblyCheck).
	const char* FeatureKindUse(FeatureKind kind)
	{
		switch (kind)
		{
			case FeatureKind::BORE:			return "bores";
			case FeatureKind::JOURNAL:		return "journals";
			case FeatureKind::FLATNESS:		return "flatness";
			case FeatureKind::LENGTH:		return "lengths";
			case FeatureKind::THICKNESS:	return "thicknesses";
			case FeatureKind::VOLUME:		return "volumes";
			case FeatureKind::RUNOUT:		return "runout";
			case FeatureKind::FORCE:		return "spring force";
			case FeatureKind::SQUARENESS:	return "spring squareness";
			case FeatureKind::ALIGNMENT:	return "rod bend and twist";
		}
		return "other features";
	}

	const char* AssemblyCheckUse(AssemblyCheck check)
	{
		switch (check)
		{
			case AssemblyCheck::OIL_CLEARANCE:		return "bearing oil clearance";
			case AssemblyCheck::CAM_CLEARANCE:		return "cam oil clearance";
			case AssemblyCheck::RING_GAP:			return "ring gap";
			case AssemblyCheck::DECK_CLEARANCE:		return "deck clearance";
			case AssemblyCheck::INSTALLED_HEIGHT:	return "installed height";
			case AssemblyCheck::ROD_SIDE_CLEARANCE:	return "rod side clearance";
			case AssemblyCheck::BELT_DEFLECTION:	return "belt deflection";
			case AssemblyCheck::NONE:				break;
		}
		return "";
	}

	// How to use a tool, step by step, for its page in the store.
	std::vector<std::string> HowToUse(const MeasuringTool& tool)
	{
		const std::string doUp = "Aim at a nut or bolt its size (they're marked) and hold the left button to do it up, the right button to undo it. "
			"Let go part way and it stays part way; one done up only part way doesn't come out by hand.";
		switch (tool.handKind)
		{
			case HandToolKind::SOCKETS:
				return { "Hold a ratchet, breaker bar, torque wrench or cordless driver and aim at a socket in this set (on the tool board, a bench "
					"or the floor): E puts that socket on the tool, or swaps it for the one on it.",
					"Aim at the empty spot in the set with a socket on the tool and press E to put it back." };
			case HandToolKind::WRENCHES:
				return { "Look at the wrench you need in the tray and press E to take it out: each wrench is a tool of its own, one size.",
					"Use the wrench on its own (see each wrench). Hang it back up on the tool board by setting it down where it goes." };
			case HandToolKind::COMBINATION_WRENCH:
				return { doUp, "It does a bolt up close to spec by feel; only a torque wrench gets it to spec." };
			case HandToolKind::RATCHET:
			case HandToolKind::BREAKER_BAR:
			case HandToolKind::IMPACT:
			{
				std::vector<std::string> steps = { "Put a socket on it first: hold it, aim at the socket you need in a socket set and press E.", doUp };
				if (tool.handKind == HandToolKind::IMPACT || tool.powered)
					steps.push_back("It runs off a battery pack: hold it and press E on a pack to clip it on, C to take it off.");
				if (tool.handKind == HandToolKind::BREAKER_BAR)
					steps.push_back("Its length cracks loose the big, tight bolts, like the crank pulley's.");
				return steps;
			}
			case HandToolKind::TORQUE_WRENCH:
				return { "Put a socket on it first: hold it, aim at the socket you need in a socket set and press E.",
					"Aim at a nut or bolt and hold the left button: it's done up to the torque the manual gives for that hole, as close as the "
					"wrench is accurate. A snugged-up bolt only needs the last of the turn.",
					"It never undoes a bolt (that knocks it out of calibration): use a ratchet, breaker bar or wrench." };
			case HandToolKind::BATTERY:
				return { "Hold a cordless tool and press E on the pack to clip it on; C takes it back off.",
					"To charge it, set a charger down, then hold the pack and press E on the charger." };
			case HandToolKind::CHARGER:
				return { "Set it down somewhere (right-click), then hold a battery pack and press E on it to put the pack on to charge.",
					"E on the charger takes the pack back off." };
			case HandToolKind::PIN_PRESS:
				return { "First the piston onto its connecting rod, off the engine: hold the piston and press E on the rod (on the bench, a shelf "
					"or the floor).",
					"Hold the press, aim at the piston and hold the left button: a loose piston pin is pressed through the piston and the rod's "
					"small end. The right button presses it out again.",
					"A rod only goes in the engine with its piston pinned on (and its ring and upper bearing shell fitted)." };
			case HandToolKind::RING_COMPRESSOR:
				return { "Fit the rod, with its piston pinned and ringed and its upper shell in, to the engine: it stops in the top of the bore, "
					"held up by the ring.",
					"Hold the compressor, aim at that piston and hold the left button: the ring is squeezed into its groove and the piston goes "
					"home, the rod onto the crank pin.",
					"Until it's home, its rod cap, the oil pump and the head gasket can't go on." };
		case HandToolKind::VALVE_SPRING_COMPRESSOR:
			return { "With the head off the engine (on the bench, a shelf or the floor), right way up, a valve, its stem seal and its spring in.",
				"Hold the compressor, aim at the valve and press E: it clamps over it and squeezes the spring down.",
				"Now hold the retainer and press E on the valve: it goes on, its keepers in the stem's groove. (R takes one off the same way.)",
				"E on the compressor takes it off again, the spring up against the retainer, for the next valve." };
			case HandToolKind::MARKER:
				return { "Aim at a wall or the floor and hold the left button to draw. The mouse wheel sets how thick the line is." };
			case HandToolKind::SPRAY_CAN:
				return { "Aim at a wall or the floor and hold the left button to spray; the mouse wheel sets how wide.",
					"Aim at a part or tool to give it a coat instead." };
			case HandToolKind::THINNER:
				return { "Hold the left button on paint on a wall or the floor to wipe it off (the mouse wheel sets how wide), or on a part or "
					"tool to strip its coat." };
			case HandToolKind::NONE:
				break;
		}
		if (tool.manual)
			return { "Hold it and left-click to open it. Every spec, step and torque figure is in it." };

		// A measuring tool.
		std::vector<std::string> steps;
		if (!tool.kinds.empty() || tool.measuresFinish)
			steps.push_back("Hold it and aim at a part (on the bench, a shelf, the floor, or the engine): the spots it can read are marked. "
				"Left-click to take a reading of the one you aim at.");
		if (std::find(tool.kinds.begin(), tool.kinds.end(), FeatureKind::BORE) != tool.kinds.end())
			steps.push_back("A bore is read at the top, middle and bottom, across the thrust side and along the pin: aim at each.");
		bool plastigauge = std::any_of(tool.assemblyChecks.begin(), tool.assemblyChecks.end(),
			[](AssemblyCheck check) { return check == AssemblyCheck::OIL_CLEARANCE || check == AssemblyCheck::CAM_CLEARANCE; });
		if (plastigauge)
		{
			steps.push_back("With the crank (or cam) in its shells and the cap off, aim at the journal and left-click to lay a strip across it.");
			steps.push_back("Fit the cap with its shell and torque it to spec, without turning the engine over. Take the cap off again, then "
				"aim at the journal and left-click to read the squashed strip: its width is the oil clearance.");
		}
		else if (!tool.assemblyChecks.empty())
			steps.push_back("On an engine on its stand, once the parts involved are on: aim at the marked spot and left-click.");
		if (std::find(tool.assemblyChecks.begin(), tool.assemblyChecks.end(), AssemblyCheck::RING_GAP) != tool.assemblyChecks.end())
			steps.push_back("Ring gap: hold a ring and press E at the top of an empty bore to set it in on its own (a block off the engine, or on the stand with the head off). Hold a "
				"piston and press E on the ring to push it down square, then aim at it and left-click. Take the ring out with E.");
		steps.push_back("Each reading is kept on the part, against its spec: look at the part, or set it on the workbench and press E at the bench.");
		return steps;
	}

	// What a feature is, and what it means for the engine: shown when it's opened on the workbench.
	std::string FeatureExplanation(PartType type, const std::string& name)
	{
		auto starts = [&](std::string_view prefix) { return name.starts_with(prefix); };
		auto ends = [&](std::string_view suffix) { return name.ends_with(suffix); };
		auto has = [&](std::string_view part) { return name.find(part) != std::string::npos; };
		bool exhaust = starts("Exhaust");

		// The bottom end.
		if (starts("Bore "))
			return "The cylinder the piston runs in. It wears oval and tapered (most at the top, where the rings turn round), and every "
				"bit it's out costs ring seal: compression lost and oil burnt. Bored and honed oversize, it takes oversize pistons.";
		if (starts("Main Housing"))
			return "The bore in the block, with its main cap bolted on, that the crank's main bearing shells sit in. It has to be round "
				"and in line with the others, or the shells are crushed out of shape and the crank binds or wears them through.";
		if (starts("Main Journal"))
			return "Where the crankshaft turns in the main bearings. Its size against the shells sets the main oil clearance: too much "
				"and the oil pressure falls and the bearings knock, too little and they run dry and seize. Worn, it's ground undersize "
				"for undersize shells.";
		if (starts("Rod Journal") && ends("Width"))
			return "How wide the crank pin is between its cheeks. With the rod's big end width, it sets rod side clearance: room for the "
				"rod to float and oil to spill out the sides and cool the bearing.";
		if (starts("Rod Journal"))
			return "The crank pin a connecting rod runs on. Its size against the rod shells sets the rod oil clearance, the most loaded "
				"bearing in the engine. Worn, it's ground undersize for undersize shells.";
		if (name == FeatureNames::DECK_HEIGHT)
			return "From the crank's centre line to the block's top deck. With the stroke, rod length and piston, it sets how far below "
				"the deck the piston stops at the top: the deck clearance, which sets quench and compression.";
		if (name == FeatureNames::DECK_FLATNESS)
			return type == PartType::CYLINDER_HEAD
				? "How flat the head's face is where it meets the gasket. A warped head (from overheating, usually) leaks compression and "
					"coolant past the gasket. Skimmed flat, it's a little thinner, which raises compression."
				: "How flat the block's top face is where the head gasket sits. A warped deck lets the gasket leak: compression, coolant or "
					"oil getting where it shouldn't. Skimmed flat, the pistons come up closer to the head.";
		if (name == FeatureNames::STROKE)
			return "How far the piston travels, set by how far the crank pins sit off the centre line. It sets the engine's "
				"displacement, and with the rod and piston, how high the piston comes up the bore.";
		if (name == FeatureNames::BIG_END)
			return "The bore at the crank end of the rod, with its cap torqued on, that the rod shells sit in. Stretched oval by the "
				"loads, it lets the shells move and spin; resized, it's round again.";
		if (name == FeatureNames::CENTER_LENGTH)
			return "From the centre of the big end to the centre of the small end (the pin). Each rod has to match: a longer one pushes its "
				"piston higher and changes that cylinder's compression and deck clearance.";
		if (name == FeatureNames::BIG_END_WIDTH)
			return "How wide the big end is across its faces. With the crank pin's width, it sets the rod side clearance.";
		if (name == FeatureNames::ROD_BEND)
			return "How far the rod's two bores are out of parallel, seen from the side. A bent rod cocks the piston in the bore: it wears "
				"the skirt and bore on one side and loads the bearing on its edge.";
		if (name == FeatureNames::ROD_TWIST)
			return "How far the rod's two bores are twisted against each other. A twisted rod rocks the piston and edge-loads the bearing, "
				"the same as a bent one.";
		if (name == FeatureNames::SKIRT)
			return "The piston's diameter at the skirt, where it's widest. Against the bore it sets piston to wall clearance: too little and "
				"it scuffs or seizes once hot, too much and it rocks and slaps, and the rings never seal.";
		if (name == FeatureNames::COMPRESSION_HEIGHT)
			return "From the centre of the pin to the piston's crown. It sets how high the crown comes up the bore, so the deck clearance and "
				"the compression.";
		if (name == FeatureNames::DISH_VOLUME)
			return "How much the dish in the crown holds. It adds to the room above the piston at the top of its stroke, so a bigger dish "
				"means lower compression.";
		if (name == FeatureNames::CLOSED_DIAMETER)
			return "How big the ring is when squeezed shut, ends touching. In its bore it sets the end gap, the room the ends need to grow "
				"into once hot: too small a gap and the ends butt and break the ring; too big and the gas blows by. It's read as the end "
				"gap with feelers, the ring squared on its own in its bore, and opened up on a ring filer.";
		if (name == FeatureNames::WALL_THICKNESS)
			return "How thick the bearing shell is. With the housing and journal it sets the oil clearance; undersize shells are thicker to "
				"make up for a journal ground smaller. A shell worn thin opens the clearance up.";
		if (name == FeatureNames::COMPRESSED_THICKNESS)
			return "How thick the head gasket is once the head is torqued down on it. It sets how far the head sits above the pistons: "
				"thinner raises the compression and tightens the quench, thicker lowers them.";

		// The head and valvetrain.
		if (starts("Chamber"))
			return "How much the combustion chamber in the head holds. It's most of the room above the piston at the top of its stroke, so a "
				"smaller chamber means more compression. Every cylinder's should match, or they don't make the same power.";
		if (has("Guide"))
			return std::format("The bore in the head the {} valve's stem slides in. Worn, the stem rocks in it: the valve doesn't seat "
				"square{}. Reamed oversize, it takes an oversize stem.", exhaust ? "exhaust" : "intake",
				exhaust ? " and runs hot" : " and oil is sucked down the stem into the cylinder");
		if (has("Seat") && ends("Width"))
			return "How wide a band the valve seals on in the seat. Too narrow and the valve can't get its heat out into the head and burns; "
				"too wide and it doesn't press hard enough to seal and carbon gets trapped. Cut with a seat cutter.";
		if (has("Seat") && ends("Runout"))
			return "How far the seat wobbles round its guide. A seat out of true with its guide leaves the valve sealing on one side only: "
				"it leaks compression and burns where it doesn't touch.";
		if (has("Seat") && ends("Recession"))
			return "How far the seat has sunk into the head from wear (and valve hammering). Sunk, the valve sits higher, the spring's "
				"installed height grows and the spring pressure falls.";
		if (starts("Cam Bore"))
			return "The bore in the head, with its cam cap on, that a cam journal turns in. Against the journal it sets the cam oil "
				"clearance.";
		if (starts("Cam Journal"))
			return "Where the camshaft turns in the head. Against its bore it sets the cam oil clearance: worn, the oil pressure falls and "
				"the cam rattles in its bores.";
		if (has("Lobe") && ends("Height"))
			return "How tall the cam lobe is, heel to nose. The difference from the base circle is how far it opens the valve: a lobe worn "
				"down opens its valve less and that cylinder breathes, and makes power, less.";
		if (has("Face Flatness") && (type == PartType::CYLINDER_HEAD))
			return std::format("How flat the head's {} port face is, where the manifold bolts on. Warped, the manifold gasket leaks{}.",
				exhaust ? "exhaust" : "intake", exhaust ? ": hot gas ticks out past it and burns the gasket" : " air in: the engine runs lean and idles badly");
		if (name == FeatureNames::FUEL_PUMP_ECCENTRIC)
			return "The height of the cam's eccentric that works the mechanical fuel pump's arm. Worn down, the pump strokes less and can't "
				"keep up with the fuel at high revs.";
		if (name == FeatureNames::CAM_END_PLAY)
			return "How far the cam slides end to end in the head. Too much and it walks back and forth, knocking the timing out and "
				"wearing its thrust faces.";
		if (name == FeatureNames::CAM_RUNOUT)
			return "How far the cam's middle wobbles as it turns, held at its ends. A bent cam loads its journals unevenly and wears them "
				"out of round.";
		if (name == FeatureNames::STEM)
			return "The diameter of the valve's stem. Against its guide it sets the stem clearance: worn, it rocks and leaks oil past the "
				"seal.";
		if (name == FeatureNames::MARGIN)
			return "The thickness of the valve head's rim, above the face. Ground too thin (or burnt thin), the edge can't hold its heat and "
				"burns through or cracks off.";
		if (name == FeatureNames::FACE_RUNOUT)
			return "How far the valve's face wobbles round its stem. A face out of true with the stem seals on one side only, the same as a "
				"bad seat.";
		if (name == FeatureNames::VALVE_LENGTH)
			return "The valve's length, tip to head. Ground faces and sunk seats leave the tip higher, which changes the spring's "
				"installed height and the valvetrain's geometry.";
		if (name == FeatureNames::SEAT_SINK)
			return "How far the valve sits down into its seat from face and seat grinding. The more it sinks, the higher the stem tip comes "
				"up and the lower the spring pressure.";
		if (name == FeatureNames::FREE_LENGTH)
			return "The spring's length sitting free. A tired spring sags shorter and pushes less; one much shorter than its mates is done.";
		if (name == FeatureNames::SEAT_PRESSURE)
			return "How hard the spring pushes at its installed height, holding the valve shut. Too little and the valve floats at high revs "
				"and bounces on its seat; too much and it wears the cam lobe.";
		if (name == FeatureNames::SPRING_RATE)
			return "How much harder the spring pushes for each millimetre it's squeezed. It sets the pressure with the valve open, keeping "
				"the valve following the cam.";
		if (name == FeatureNames::SPRING_SQUARENESS)
			return "How far the spring leans off upright, stood on its end. A spring out of square pushes the valve sideways, wearing the "
				"guide and the stem.";

		// Ancillaries.
		if (name == FeatureNames::ROTOR_TIP_CLEARANCE)
			return "The gap between the oil pump's inner and outer rotor tips. Worn wide, oil slips back round inside the pump and the oil "
				"pressure falls, worst at idle when hot.";
		if (name == FeatureNames::ROTOR_SIDE_CLEARANCE)
			return "The gap between the oil pump's rotors and its cover. Worn wide, oil leaks past the sides of the rotors and the pump "
				"makes less pressure.";
		if (name == FeatureNames::RAIL_FLATNESS || name == FeatureNames::FLANGE_FLATNESS || name == FeatureNames::FACE_FLATNESS)
			return "How flat the face is where it bolts on with a gasket. Bent or warped, the gasket can't seal it: a leak of oil, coolant, air "
				"or exhaust, whichever it holds in.";
		if (name == FeatureNames::ROCKER_BORE)
			return "The bore of the rocker arm that rides on its shaft. Worn, the rocker rocks on the shaft and its tip wanders across the "
				"valve stem, noisy and wearing.";
		if (name == FeatureNames::ROCKER_SHAFT_DIAMETER)
			return "The diameter of the shaft the rockers pivot on. Worn under each rocker, it lets them rock and clatter and leaks the oil "
				"fed through it.";
		if (name == FeatureNames::ROOT_DIAMETER)
			return "The diameter across the bottom of the teeth. Worn down, the belt rides lower and its teeth slip against the sprocket's: "
				"the cam timing drifts and the belt wears.";
		if (name == FeatureNames::BELT_TOOTH_HEIGHT)
			return "How tall the belt's teeth stand. Worn short, they can ride up over the sprocket's teeth: the timing jumps, and the valves "
				"can hit the pistons.";
		if (name == FeatureNames::SHAFT_PLAY || name == FeatureNames::IDLER_PLAY)
			return "How much the shaft rocks in its bearing. Worn bearings let it wobble: noise, a belt that runs off true and wears, and "
				"before long a bearing that seizes.";
		if (name == FeatureNames::TENSIONER_TRAVEL)
			return "How far the tensioner has left to take up the belt's stretch. With little left, it can't keep the belt tight as it "
				"stretches further.";
		if (name == FeatureNames::RING_RUNOUT)
			return "How far the crank pulley's outer ring wobbles. It's bonded on rubber that damps the crank's twisting; a ring that's "
				"slipped or wobbles means the rubber is going, and the crank is left undamped.";
		if (name == FeatureNames::ELECTRODE_GAP)
			return "The gap the spark jumps. Worn wide it needs more voltage than the coil gives and misfires under load; too tight and the "
				"spark is small and lights the mixture poorly.";
		if (name == FeatureNames::THROTTLE_BORE)
			return "The bore the throttle plate shuts in. Worn oval, the plate can't shut it off and air leaks past at idle.";
		if (name == FeatureNames::INJECTOR_FLOW || name == FeatureNames::JET_FLOW)
			return "How much fuel it passes in 15 seconds on the bench. A clogged one passes less and leaves its cylinders lean, hotter and "
				"down on power.";
		if (name == FeatureNames::FLYWHEEL_RUNOUT)
			return "How far the face the clutch grips wobbles as it turns. Out of true, the clutch judders as it takes up.";
		return "";
	}

	std::string SizeClassLabel(double sizeClass)
	{
		return std::abs(sizeClass) < 1e-6 ? "STD" : std::format("{:+.2f}", sizeClass);
	}

	void ColoredRangeText(double value, const Range& range, const std::string& text)
	{
		ImGui::TextColored(range.Contains(value) ? COLOR_GOOD : COLOR_BAD, "%s", text.c_str());
	}

	constexpr const char* XRAY_TIP = "The part's real geometry (debug x-ray). The player never gets to see this.";

	// The UI is drawn into a linear target and shown brighter, so colours meant as they'd look on a web page (the
	// store's) are written as on screen and darkened to match.
	float WebChannel(float value)
	{
		return std::pow(value, 2.2f);
	}
	ImU32 Web(int r, int g, int b, int a = 255)
	{
		auto channel = [](int value) { return static_cast<int>(std::lround(WebChannel(value / 255.f) * 255.f)); };
		return IM_COL32(channel(r), channel(g), channel(b), a);
	}
	ImVec4 WebV(float r, float g, float b, float a = 1.f)
	{
		return { WebChannel(r), WebChannel(g), WebChannel(b), a };
	}

	// The store's website colours, light or dark (the Dark mode setting), as they'd look on screen.
	struct SitePalette
	{
		// The window's ImGui colours.
		ImVec4 page, panel, popup, text, textDisabled, border, frame, frameHovered, frameActive;
		ImVec4 button, buttonHovered, buttonActive, header, headerHovered, headerActive;
		ImVec4 scrollbar, grab, grabHovered, grabActive, check, checkActive, tableHeader, tableLine, rowAlt;
		// The shared status colours, to read on its pages.
		ImVec4 good, warn, bad, dim, xray;
		// The browser, the site's header and menu, and the page.
		ImU32 browser, tab, toolbar, navButton, address, addressBorder, lock, browserLine;
		ImU32 ink, muted, brand, cartText, menu, menuLine, linkActive, linkHover, linkUnderline, delivery, heading;
		ImVec4 home;
		// The product cards.
		ImU32 card, cardShadow, cardShadowHovered, photo, cardBorder, cardBorderHovered, cardInk, cardPrice, cardMuted;
	};

	SitePalette MakeSitePalette(bool dark)
	{
		SitePalette p;
		if (!dark)
		{
			p.page = WebV(0.957f, 0.961f, 0.969f); p.panel = WebV(1.f, 1.f, 1.f); p.popup = WebV(1.f, 1.f, 1.f);
			p.text = WebV(0.12f, 0.13f, 0.16f); p.textDisabled = WebV(0.42f, 0.45f, 0.50f); p.border = WebV(0.85f, 0.86f, 0.88f);
			p.frame = WebV(0.94f, 0.95f, 0.96f); p.frameHovered = WebV(0.90f, 0.92f, 0.95f); p.frameActive = WebV(0.86f, 0.89f, 0.94f);
			p.button = WebV(1.00f, 0.60f, 0.00f); p.buttonHovered = WebV(1.00f, 0.68f, 0.18f); p.buttonActive = WebV(0.92f, 0.52f, 0.00f);
			p.header = WebV(0.86f, 0.91f, 0.98f); p.headerHovered = WebV(0.80f, 0.87f, 0.97f); p.headerActive = WebV(0.74f, 0.83f, 0.96f);
			p.scrollbar = WebV(0.95f, 0.95f, 0.96f); p.grab = WebV(0.75f, 0.77f, 0.80f); p.grabHovered = WebV(0.65f, 0.67f, 0.71f);
			p.grabActive = WebV(0.55f, 0.58f, 0.62f); p.check = WebV(0.10f, 0.45f, 0.85f); p.checkActive = WebV(0.08f, 0.38f, 0.75f);
			p.tableHeader = WebV(0.92f, 0.93f, 0.95f); p.tableLine = WebV(0.90f, 0.91f, 0.93f); p.rowAlt = WebV(0.97f, 0.97f, 0.98f);
			p.good = WebV(0.10f, 0.55f, 0.22f); p.warn = WebV(0.78f, 0.47f, 0.00f); p.bad = WebV(0.80f, 0.15f, 0.15f);
			p.dim = WebV(0.47f, 0.49f, 0.53f); p.xray = WebV(0.15f, 0.40f, 0.80f);
			p.browser = Web(222, 225, 230); p.tab = Web(248, 249, 251); p.toolbar = Web(248, 249, 251); p.navButton = Web(232, 234, 238);
			p.address = Web(255, 255, 255); p.addressBorder = Web(200, 204, 212); p.lock = Web(60, 140, 70); p.browserLine = Web(200, 204, 212);
			p.ink = Web(30, 34, 42); p.muted = Web(110, 116, 126); p.brand = Web(22, 52, 96); p.cartText = Web(30, 34, 42);
			p.menu = Web(255, 255, 255); p.menuLine = Web(220, 223, 228); p.linkActive = Web(22, 52, 96); p.linkHover = Web(20, 90, 170);
			p.linkUnderline = Web(200, 204, 212); p.delivery = Web(25, 135, 65); p.heading = Web(22, 52, 96); p.home = WebV(0.08f, 0.35f, 0.67f);
			p.card = Web(255, 255, 255); p.cardShadow = Web(0, 0, 0, 16); p.cardShadowHovered = Web(0, 0, 0, 38); p.photo = Web(52, 58, 70);
			p.cardBorder = Web(220, 223, 228); p.cardBorderHovered = Web(150, 160, 175);
			p.cardInk = Web(30, 34, 42); p.cardPrice = Web(25, 135, 65); p.cardMuted = Web(120, 126, 136);
		}
		else
		{
			p.page = WebV(0.09f, 0.10f, 0.12f); p.panel = WebV(0.13f, 0.14f, 0.17f); p.popup = WebV(0.15f, 0.16f, 0.19f);
			p.text = WebV(0.90f, 0.91f, 0.93f); p.textDisabled = WebV(0.58f, 0.61f, 0.66f); p.border = WebV(0.24f, 0.26f, 0.30f);
			p.frame = WebV(0.18f, 0.19f, 0.23f); p.frameHovered = WebV(0.22f, 0.24f, 0.28f); p.frameActive = WebV(0.26f, 0.28f, 0.33f);
			p.button = WebV(0.88f, 0.48f, 0.04f); p.buttonHovered = WebV(0.96f, 0.56f, 0.10f); p.buttonActive = WebV(0.78f, 0.40f, 0.00f);
			p.header = WebV(0.20f, 0.30f, 0.45f); p.headerHovered = WebV(0.25f, 0.36f, 0.52f); p.headerActive = WebV(0.28f, 0.40f, 0.58f);
			p.scrollbar = WebV(0.11f, 0.12f, 0.14f); p.grab = WebV(0.30f, 0.32f, 0.36f); p.grabHovered = WebV(0.38f, 0.40f, 0.45f);
			p.grabActive = WebV(0.46f, 0.49f, 0.54f); p.check = WebV(0.40f, 0.65f, 1.00f); p.checkActive = WebV(0.50f, 0.72f, 1.00f);
			p.tableHeader = WebV(0.17f, 0.18f, 0.22f); p.tableLine = WebV(0.20f, 0.21f, 0.25f); p.rowAlt = WebV(0.15f, 0.16f, 0.19f);
			p.good = WebV(0.40f, 0.82f, 0.50f); p.warn = WebV(1.00f, 0.75f, 0.30f); p.bad = WebV(1.00f, 0.45f, 0.45f);
			p.dim = WebV(0.55f, 0.58f, 0.62f); p.xray = WebV(0.55f, 0.75f, 1.00f);
			p.browser = Web(30, 32, 38); p.tab = Web(46, 49, 56); p.toolbar = Web(46, 49, 56); p.navButton = Web(60, 64, 72);
			p.address = Web(24, 26, 30); p.addressBorder = Web(66, 70, 80); p.lock = Web(95, 205, 125); p.browserLine = Web(20, 22, 26);
			p.ink = Web(230, 232, 236); p.muted = Web(150, 156, 166); p.brand = Web(16, 34, 62); p.cartText = Web(22, 24, 28);
			p.menu = Web(28, 30, 36); p.menuLine = Web(50, 54, 62); p.linkActive = Web(255, 153, 0); p.linkHover = Web(110, 175, 255);
			p.linkUnderline = Web(90, 96, 108); p.delivery = Web(95, 205, 125); p.heading = Web(240, 242, 245); p.home = WebV(0.43f, 0.69f, 1.f);
			p.card = Web(36, 39, 46); p.cardShadow = Web(0, 0, 0, 70); p.cardShadowHovered = Web(0, 0, 0, 110); p.photo = Web(22, 24, 30);
			p.cardBorder = Web(56, 60, 68); p.cardBorderHovered = Web(105, 112, 126);
			p.cardInk = Web(230, 232, 236); p.cardPrice = Web(95, 205, 125); p.cardMuted = Web(140, 146, 156);
		}
		return p;
	}

	// The palette the store is being drawn in (set by WebsiteTheme), light otherwise.
	const SitePalette* g_site = nullptr;
	const SitePalette& Site()
	{
		static const SitePalette light = MakeSitePalette(false);
		return g_site ? *g_site : light;
	}

	// The store's product cards: a card with a shadow, the icon on a dark tile like a product photo (the icons are
	// drawn light), the name and the price in green.
	void ProductCard(ImDrawList* drawList, ImVec2 min, ImVec2 max, float photoBottom, bool selected, bool hovered)
	{
		const SitePalette& site = Site();
		drawList->AddRectFilled({ min.x + 1.f, min.y + 3.f }, { max.x + 1.f, max.y + 3.f }, hovered ? site.cardShadowHovered : site.cardShadow, 8.f);
		drawList->AddRectFilled(min, max, site.card, 8.f);
		drawList->AddRectFilled({ min.x + 6.f, min.y + 6.f }, { max.x - 6.f, photoBottom }, site.photo, 6.f);
		ImU32 border = selected ? Web(255, 153, 0) : hovered ? site.cardBorderHovered : site.cardBorder;
		drawList->AddRect(min, max, border, 8.f, 0, selected ? 2.5f : 1.f);
	}

	// TableHeadersRow, but each column header explains itself on hover. Tips past the column count are ignored.
	void HeadersRow(std::initializer_list<const char*> tips)
	{
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		for (int column = 0; column < ImGui::TableGetColumnCount(); column++)
		{
			if (!ImGui::TableSetColumnIndex(column))
				continue;
			ImGui::PushID(column);
			ImGui::TableHeader(ImGui::TableGetColumnName(column));
			if (column < static_cast<int>(tips.size()) && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tips.begin()[column]);
			ImGui::PopID();
		}
	}
}

WorkshopUI::WorkshopUI()
{
}

WorkshopUI::~WorkshopUI()
{
}

std::string WorkshopUI::Size(double mm) const
{
	return Units::FormatSize(mm, m_units);
}

std::string WorkshopUI::Deviation(double mm) const
{
	return Units::FormatDeviation(mm, m_units);
}

std::string WorkshopUI::FeatureValue(const Feature& feature, double value) const
{
	switch (feature.kind)
	{
		case FeatureKind::FLATNESS:
		case FeatureKind::RUNOUT:
		case FeatureKind::SQUARENESS:
		case FeatureKind::ALIGNMENT:	return Deviation(value);
		case FeatureKind::VOLUME:	return Units::FormatVolume(value);
		case FeatureKind::FORCE:	return Units::FormatForce(value);
		default:					return Size(value);
	}
}

std::string WorkshopUI::SpecText(const Feature& feature) const
{
	switch (feature.kind)
	{
		case FeatureKind::FLATNESS:
		case FeatureKind::RUNOUT:
		case FeatureKind::SQUARENESS:	return "<= " + Deviation(feature.spec.Max());
		case FeatureKind::ALIGNMENT:	return "<= " + Deviation(feature.spec.Max()) + " per 100 mm";
		case FeatureKind::VOLUME:	return std::format("{:.1f} - {:.1f} cc", feature.spec.Min(), feature.spec.Max());
		case FeatureKind::FORCE:	return std::format("{:.0f} - {:.0f} N", feature.spec.Min(), feature.spec.Max());
		default:
		{
			auto text = Size(feature.spec.Min()) + " - " + Size(feature.spec.Max());
			if (feature.sizeClasses.size() > 1)
				text += " STD";
			return text;
		}
	}
}

std::string WorkshopUI::RangeText(const Range& range) const
{
	return Deviation(range.min) + " - " + Deviation(range.max);
}

Feature WorkshopUI::FromReading(const Feature& feature, const FeatureReading& reading)
{
	Feature measured = feature;
	for (int station = 0; station < Feature::STATIONS; station++)
		for (int axis = 0; axis < Feature::AXES; axis++)
			measured.samples[station][axis] = reading.samples[station][axis];
	measured.value = reading.value;
	measured.surfaceRa = reading.finishRa.value_or(0.0);
	return measured;
}

std::optional<double> WorkshopUI::MeasuredSize(const pPart& part, const std::string& featureName)
{
	if (!part)
		return std::nullopt;

	auto feature = part->FindFeature(featureName);
	auto reading = part->GetReading(featureName);
	if (!feature || !reading || !reading->hasSize || reading->stale)
		return std::nullopt;
	return FromReading(*feature, *reading).Size();
}

void WorkshopUI::Open(WorkshopTab tab)
{
	m_open = true;
	m_requestedTab = tab;
	m_currentTab = tab;
	if (tab != WorkshopTab::MACHINE)
		m_activeMachine = -1;
}

void WorkshopUI::OpenStore(StoreCategory category)
{
	// The parts computer replaces whatever workshop screen was open.
	m_open = false;
	m_activeMachine = -1;
	m_storeOpen = true;
	m_requestedCategory = category;
}

void WorkshopUI::OpenMachine(int machineIndex)
{
	if (machineIndex < 0 || machineIndex >= static_cast<int>(MachineCatalog::All().size()))
		return;
	m_activeMachine = machineIndex;
	Open(WorkshopTab::MACHINE);
}

void WorkshopUI::Close()
{
	m_open = false;
	m_storeOpen = false;
	m_activeMachine = -1;
}

void WorkshopUI::SetXray(bool xray)
{
	m_xray = xray;
}

bool WorkshopUI::IsOpen() const
{
	return m_open || m_storeOpen;
}

void WorkshopUI::SelectPart(const Part& part, int machineIndex)
{
	if (m_selectedPartId != part.GetId())
		m_lastMachiningMessage.clear();
	m_selectedPartId = part.GetId();
	m_selectedFeature.clear();
	m_selectedMachine = -1;

	const auto& machines = MachineCatalog::All();
	if (machineIndex < 0 || machineIndex >= static_cast<int>(machines.size()))
		return;

	for (const auto& feature : part.GetFeatures())
		if (Machining::CanMachine(machines[machineIndex], feature))
		{
			m_selectedFeature = feature.name;
			m_selectedMachine = machineIndex;
			m_machineParameter = machines[machineIndex].operation == MachineOperation::SURFACE ? Units::ToDisplay(0.05, m_units) : 0.0;
			return;
		}
}

void WorkshopUI::Update(float deltaTime)
{
	constexpr float SPIN_DOWN_SECONDS = 1.5f;
	if (m_pullActive)
	{
		m_pullTime += deltaTime;
		if (m_pullTime >= PULL_SECONDS)
		{
			m_pullActive = false;
			m_spinDownTime = m_pullFailed ? 0.f : SPIN_DOWN_SECONDS;
		}
	}
	else if (m_spinDownTime > 0.f)
		m_spinDownTime = (std::max)(0.f, m_spinDownTime - deltaTime);
}

int WorkshopUI::GetPullEngine() const
{
	return m_pullEngine;
}

float WorkshopUI::GetLiveRpm() const
{
	if (m_pullActive)
		return m_pullStartRpm + (m_pullEndRpm - m_pullStartRpm) * (std::min)(1.f, m_pullTime / PULL_SECONDS);
	// A failed engine stops dead; a healthy one coasts down off the dyno.
	if (m_spinDownTime > 0.f && m_pullEndRpm > 0.f)
		return m_pullEndRpm * m_spinDownTime / 1.5f;
	return 0.f;
}

void WorkshopUI::SetPartAvailability(const std::function<bool(uint32_t)>& isUsable)
{
	m_isUsable = isUsable;
}

void WorkshopUI::SetBackpackContents(const std::function<void()>& drawContents)
{
	m_backpackContents = drawContents;
}

void WorkshopUI::SetUnits(Units::System units)
{
	m_units = units;
}

bool WorkshopUI::IsUsable(const Part& part) const
{
	return !m_isUsable || m_isUsable(part.GetId());
}

void WorkshopUI::Draw(Workshop& workshop)
{
	if (m_storeOpen)
		DrawStoreWindow(workshop);

	if (!m_open)
	{
		// Closed with the window's X: the machine tab goes with it.
		m_activeMachine = -1;
		return;
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize({ 1150.f, 780.f }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });
	// Opening from the shop floor should always show the window, even if it was left collapsed.
	if (m_requestedTab)
	{
		ImGui::SetNextWindowFocus();
		ImGui::SetNextWindowCollapsed(false);
	}

	// One screen at a time: Tab is the inventory, and each station's screen only opens from that
	// station. The window is named after what it shows.
	if (m_currentTab == WorkshopTab::BACKPACK)
	{
		// Never so small that the monkey, grid and hotbar crowd the item panel beside them off
		// the window (a size saved from an older, narrower layout included).
		ImGui::SetNextWindowSizeConstraints({ 1200.f, 600.f }, { FLT_MAX, FLT_MAX });
		if (ImGui::Begin("Inventory###Inventory", &m_open) && m_backpackContents)
			m_backpackContents();
		ImGui::End();
		m_requestedTab = std::nullopt;
		return;
	}

	std::string title = "Workbench";
	if (m_currentTab == WorkshopTab::MACHINE && m_activeMachine >= 0)
		title = MachineCatalog::All()[m_activeMachine].name;
	else if (m_currentTab == WorkshopTab::ENGINE_STAND)
		title = "Engine Stand";
	else if (m_currentTab == WorkshopTab::DYNO)
		title = "Dyno Cell";
	else if (m_currentTab == WorkshopTab::CAR)
		title = "LE86";

	if (ImGui::Begin(std::format("{}###Workshop", title).c_str(), &m_open))
	{
		DrawStatusBar(workshop);
		ImGui::Separator();

		float logHeight = ImGui::GetTextLineHeightWithSpacing() * 7.f;
		if (ImGui::BeginChild("Main", { 0.f, -logHeight }))
		{
			switch (m_currentTab)
			{
				case WorkshopTab::MACHINE:
					if (m_activeMachine >= 0)
						DrawMachineTab(workshop);
					break;
				case WorkshopTab::ENGINE_STAND:	DrawAssemblyTab(workshop);	break;
				case WorkshopTab::DYNO:			DrawDynoTab(workshop);		break;
				case WorkshopTab::CAR:			DrawCarTab(workshop);		break;
				default:						DrawInventoryTab(workshop);	break;
			}
		}
		ImGui::EndChild();
		m_requestedTab = std::nullopt;

		DrawLog(workshop);
	}
	ImGui::End();
}

void WorkshopUI::DrawStatusBar(Workshop& workshop)
{
	// Not the engine's name: there'll be more than one kind of engine.
	ImGui::TextColored(COLOR_GOOD, "$%.0f", workshop.GetMoney());

	// The shop's rank, and how far it is to the next.
	ImGui::SameLine(0.f, 30.f);
	int rank = workshop.Rank();
	double reputation = workshop.GetStats().reputation;
	ImGui::Text("Rank %d: %s", rank, ShopRank::Info(rank).name);
	if (rank < ShopRank::MaxRank())
	{
		const auto& here = ShopRank::Info(rank);
		const auto& next = ShopRank::Info(rank + 1);
		ImGui::SameLine();
		ImGui::ProgressBar(static_cast<float>((reputation - here.reputation) / (next.reputation - here.reputation)), { 120.f, 0.f },
			std::format("{:.0f} / {:.0f}", reputation, next.reputation).c_str());
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Reputation. Sell good engines (and run clean dyno pulls) to reach rank %d, %s: a $%.0f bonus and better tools in the store.",
				rank + 1, next.name, next.bonus);
	}

	if (m_xray)
	{
		ImGui::SameLine(0.f, 30.f);
		ImGui::TextColored(COLOR_XRAY, "X-ray on (F3)");
	}

	ImGui::SameLine(0.f, 30.f);
	ImGui::TextDisabled("Tab / Esc: back to the shop floor");
}

std::string WorkshopUI::FeatureSection(PartType type, const std::string& featureName)
{
	auto has = [&](const char* text) { return featureName.find(text) != std::string::npos; };
	switch (type)
	{
		case PartType::BLOCK:
			if (has("Bore")) return "Cylinder bores";
			if (has("Main Housing")) return "Main bearing housings";
			return "Deck";
		case PartType::CYLINDER_HEAD:
			if (has("Guide")) return "Valve guides";
			if (has("Seat")) return "Valve seats";
			if (has("Cam Bore")) return "Cam bores";
			if (has("Chamber")) return "Combustion chambers";
			return "Deck";
		case PartType::CRANKSHAFT:
			if (has("Main Journal")) return "Main journals";
			if (has("Rod Journal")) return "Rod journals";
			return "Stroke";
		case PartType::CAMSHAFT:
			if (has("Journal")) return "Journals";
			return "Lobes";
		default:
			return "Measurements";
	}
}

void WorkshopUI::DrawInventoryTab(Workshop& workshop)
{
	// Only what's actually on the workbench. Parts set up in a machine have that machine's tab.
	std::vector<BenchItem> items = m_benchContents ? m_benchContents() : std::vector<BenchItem>{};
	std::erase_if(items, [](const BenchItem& item) { return item.machineIndex >= 0; });
	pPart selected = nullptr;
	m_selectedPartMachine = -1;
	for (const auto& item : items)
		if (item.part->GetId() == m_selectedPartId)
		{
			selected = item.part;
			m_selectedPartMachine = item.machineIndex;
		}
	if (!selected && !items.empty())
	{
		selected = items.front().part;
		m_selectedPartId = selected->GetId();
		m_selectedPartMachine = items.front().machineIndex;
		m_selectedFeature.clear();
	}

	if (ImGui::BeginChild("OnTheBench", { 300.f, 0.f }, ImGuiChildFlags_Borders))
	{
		if (items.empty())
		{
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("The workbench is empty. Set a part down on top of it (right-click to place it, or G to drop it).");
			ImGui::PopTextWrapPos();
		}

		std::string currentLocation;
		for (const auto& item : items)
		{
			if (item.location != currentLocation)
			{
				ImGui::SeparatorText(item.location.c_str());
				currentLocation = item.location;
			}

			const auto& part = item.part;
			ImGui::PushID(static_cast<int>(part->GetId()));
			// What's fitted to a part is listed under it, stepped in.
			float indent = item.depth * 16.f;
			if (indent > 0.f)
				ImGui::Indent(indent);
			PartIcons::Icon(part->GetType(), ImGui::GetTextLineHeight() * 1.3f);
			ImGui::SameLine();
			bool damaged = part->GetCondition() == PartCondition::DAMAGED;
			if (damaged)
				ImGui::PushStyleColor(ImGuiCol_Text, COLOR_BAD);
			if (ImGui::Selectable(std::format("{}###part{}", part->GetName(), part->GetId()).c_str(), part == selected))
			{
				m_selectedPartId = part->GetId();
				m_selectedFeature.clear();
				m_lastMachiningMessage.clear();
				if (item.machineIndex >= 0)
					SelectPart(*part, item.machineIndex);
			}
			if (damaged)
				ImGui::PopStyleColor();
			if (indent > 0.f)
				ImGui::Unindent(indent);
			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	if (ImGui::BeginChild("Inspector", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		if (selected)
			DrawPartInspector(workshop, selected);
		else
			ImGui::TextDisabled("Nothing on the bench.");
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawMachineTab(Workshop& workshop)
{
	const auto& machine = MachineCatalog::All()[m_activeMachine];

	// The machine itself: what it does.
	ItemIcons::MachineIcon(machine, 56.f);
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::Text("%s", machine.name.c_str());
	ImGui::TextDisabled("%s   |   leaves %.2f um Ra", Machining::OperationName(machine.operation), machine.finishRa);
	ImGui::PushTextWrapPos(0.f);
	ImGui::TextDisabled("%s", machine.description.c_str());
	ImGui::PopTextWrapPos();
	ImGui::EndGroup();
	ImGui::Separator();

	// The part set up in it.
	pPart part = nullptr;
	if (m_benchContents)
		for (const auto& item : m_benchContents())
			if (item.machineIndex == m_activeMachine)
				part = item.part;
	if (!part)
	{
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("Nothing is set up in the %s. Hold a part it can work on and press E at the machine.", machine.name.c_str());
		ImGui::PopTextWrapPos();
		return;
	}

	if (m_selectedPartId != part->GetId())
		m_lastMachiningMessage.clear();
	m_selectedPartId = part->GetId();
	m_selectedPartMachine = m_activeMachine;

	// Only the features this machine can cut; keep the selection on one of them.
	std::vector<const Feature*> cuttable;
	for (const auto& feature : part->GetFeatures())
		if (Machining::CanMachine(machine, feature))
			cuttable.push_back(&feature);
	bool selectionCuttable = std::any_of(cuttable.begin(), cuttable.end(), [&](const Feature* feature) { return feature->name == m_selectedFeature; });
	if (!selectionCuttable)
		m_selectedFeature = cuttable.empty() ? "" : cuttable.front()->name;

	if (ImGui::BeginChild("MachineFeatures", { 280.f, 0.f }, ImGuiChildFlags_Borders))
	{
		PartIcons::Icon(part->GetType(), ImGui::GetTextLineHeight() * 2.f);
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::PushTextWrapPos(0.f);
		ImGui::Text("#%u %s", part->GetId(), part->GetName().c_str());
		ImGui::PopTextWrapPos();
		MaterialText(part->GetMaterial());
		ImGui::SameLine();
		ImGui::TextDisabled("|  %s", PartConditionName(part->GetCondition()));
		ImGui::EndGroup();
		if (part->GetCondition() == PartCondition::DAMAGED)
			ImGui::TextColored(COLOR_BAD, "Damaged: %s", part->GetDamageReason().c_str());

		ImGui::SeparatorText("What it can cut");
		if (cuttable.empty())
			ImGui::TextDisabled("Nothing on this part.");
		// Each with a coloured mark: green in spec, red out, grey not measured (or out of date).
		for (const Feature* feature : cuttable)
		{
			auto reading = part->GetReading(feature->name);
			ImVec4 mark = COLOR_DIM;
			if (reading && !reading->stale && reading->hasSize)
				mark = FromReading(*feature, *reading).Evaluate(false) == FeatureIssue::NONE ? COLOR_GOOD : COLOR_BAD;
			ImGui::TextColored(mark, "o");
			ImGui::SameLine();
			if (ImGui::Selectable(feature->name.c_str(), m_selectedFeature == feature->name) && m_selectedFeature != feature->name)
			{
				m_selectedFeature = feature->name;
				m_lastMachiningMessage.clear();
			}
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("MachineWork", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		if (auto feature = part->FindFeature(m_selectedFeature))
		{
			ImGui::Text("%s", feature->name.c_str());
			DrawFeatureDetail(workshop, *part, *feature);
			DrawMachiningPanel(workshop, *part, *feature);
		}
		else
			ImGui::TextDisabled("Pick a feature on the left.");
	}
	ImGui::EndChild();
}

void WorkshopUI::SetBenchContents(const std::function<std::vector<BenchItem>()>& benchContents)
{
	m_benchContents = benchContents;
}

void WorkshopUI::DrawPartInspector(Workshop& workshop, const pPart& part)
{
	PartIcons::Icon(part->GetType(), 44.f);
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::Text("%s", part->GetName().c_str());
	ImGui::TextDisabled("#%u  |", part->GetId());
	ImGui::SameLine();
	MaterialText(part->GetMaterial());
	ImGui::SameLine();
	ImGui::TextDisabled("|  %s", PartConditionName(part->GetCondition()));
	ImGui::EndGroup();

	// What it is and what it does, what it's made of, and the shop's note on it.
	ImGui::Spacing();
	ImGui::PushTextWrapPos(0.f);
	ImGui::TextUnformatted(PartCatalog::Purpose(part->GetType()));
	const Material& material = GetMaterial(part->GetMaterial());
	ImGui::TextDisabled("Made of");
	ImGui::SameLine();
	MaterialText(part->GetMaterial());
	ImGui::SameLine();
	ImGui::TextDisabled("(%s): %s", MaterialTierName(material.tier), MaterialTierMeaning(material.tier));
	if (const char* note = PartCatalog::Get(part->GetType()).description; note && *note)
		ImGui::TextDisabled("%s", note);
	ImGui::PopTextWrapPos();
	ImGui::Spacing();

	if (part->GetCondition() == PartCondition::DAMAGED)
		ImGui::TextColored(COLOR_BAD, "Damaged: %s", part->GetDamageReason().c_str());

	// Nuts and bolts: the size, the hole it's in and how it was done up (as far as the wrench
	// could tell), or every kind of hole it fits.
	if (const auto& fastener = PartCatalog::Get(part->GetType()).fastener; fastener.IsFastener())
	{
		ImGui::Text("%s %s, %d mm", fastener.thread, fastener.nut ? "nut" : "bolt", fastener.headMm);
		int engine = workshop.EngineOf(*part);
		auto position = engine >= 0 ? workshop.GetAssembly(engine).Find(part.get()) : std::nullopt;
		if (const auto* holes = position ? EngineAssembly::Holes(position->first) : nullptr)
		{
			ImGui::Text("In: %s", workshop.GetAssembly(engine).PositionName(position->first, position->second).c_str());
			switch (part->GetTightness())
			{
				case FastenerTightness::TORQUED:	ImGui::TextColored(COLOR_GOOD, "Done up with a torque wrench."); break;
				case FastenerTightness::SNUG:		ImGui::TextColored(COLOR_WARN, "Snugged up by feel, not torqued: go over it with a torque wrench."); break;
				case FastenerTightness::LOOSE:		ImGui::TextColored(COLOR_BAD, "Finger tight: do it up with a %d mm socket or wrench.", fastener.headMm); break;
			}
			if (part->GetTorque() > 0.0)
			{
				if (m_xray)
					ImGui::TextColored(COLOR_XRAY, "X-ray: really %.1f Nm (%.0f%% of spec)", part->GetTorque(), part->GetTorque() / holes->torqueNm * 100.0);
			}
		}
		else
			for (const auto& holes : PartCatalog::Holes())
				if (holes.size == part->GetType())
					ImGui::BulletText("Fits the %ss", holes.name);
	}

	if (!IsUsable(*part))
	{
		ImGui::Spacing();
		ImGui::TextColored(COLOR_WARN, "Still in its delivery box. Open the delivery by the roll-up door first.");
		return;
	}

	bool installed = workshop.EngineOf(*part) >= 0;
	ImGui::BeginDisabled(installed);
	if (ImGui::Button(std::format("Sell for ${:.0f}", workshop.PartSalePrice(*part)).c_str()))
	{
		workshop.SellPart(part);
		ImGui::EndDisabled();
		return;
	}
	ImGui::EndDisabled();
	if (installed)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(on the engine stand)");
	}

	ImGui::Spacing();

	int columns = m_xray ? 8 : 7;
	ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

	// Say plainly that the rows open up.
	ImGui::TextColored(COLOR_XRAY, "Click any feature for its full readings and how to bring it back into spec.");

	// Big parts have dozens of features, so they're grouped by area, each in its own section.
	std::vector<std::string> sections;
	for (const auto& feature : part->GetFeatures())
	{
		auto section = FeatureSection(part->GetType(), feature.name);
		if (std::find(sections.begin(), sections.end(), section) == sections.end())
			sections.push_back(section);
	}

	for (const auto& section : sections)
	{
		if (sections.size() > 1 && !ImGui::CollapsingHeader(section.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			continue;
		if (!ImGui::BeginTable(("Features " + section).c_str(), columns, flags))
			continue;

		ImGui::TableSetupColumn("Feature");
		ImGui::TableSetupColumn("Spec");
		ImGui::TableSetupColumn("Measured");
		ImGui::TableSetupColumn("Finish");
		ImGui::TableSetupColumn("Verdict");
		ImGui::TableSetupColumn("Measure with", ImGuiTableColumnFlags_WidthFixed, 230.f);
		if (m_xray)
			ImGui::TableSetupColumn("True");
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 64.f);
		HeadersRow({
			"The surface or dimension. Click a row for its readings, how to fix it and the machining.",
			"The size it has to be, with its tolerance band.",
			"Your last reading of its size (the average of all stations). Stale once the part's been cut or swapped since.",
			"Your last surface finish reading, in Ra (lower is smoother).",
			"How your last reading compares to spec: in spec, or what's wrong with it.",
			"The tool for the job. Hold it and aim at the feature in the shop.",
			m_xray ? XRAY_TIP : "Click a row to open its details.",
			"Click a row to open its details." });

		for (const auto& feature : part->GetFeatures())
		{
			if (FeatureSection(part->GetType(), feature.name) != section)
				continue;
			ImGui::PushID(feature.name.c_str());
			ImGui::TableNextRow();
			auto reading = part->GetReading(feature.name);

			ImGui::TableSetColumnIndex(0);
			bool selected = m_selectedFeature == feature.name;
			if (ImGui::Selectable(feature.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
			{
				if (!selected)
				{
					m_selectedFeature = feature.name;
					m_selectedMachine = -1;
					m_lastMachiningMessage.clear();
				}
				m_scrollToDetail = true;
			}
			bool hovered = ImGui::IsItemHovered();
			if (hovered)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				ImGui::SetTooltip("Click for %s's readings and how to fix it.", feature.name.c_str());
			}

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(SpecText(feature).c_str());

			// A ring is read by its end gap, squared in a bore.
			const FeatureReading* endGap = feature.name == FeatureNames::CLOSED_DIAMETER ? part->GetReading(FeatureNames::END_GAP) : nullptr;
			const Range& gapSpec = workshop.GetSpec().ringGap;

			ImGui::TableSetColumnIndex(2);
			if (endGap)
			{
				ImGui::TextColored(endGap->stale ? COLOR_DIM : ImGui::GetStyleColorVec4(ImGuiCol_Text), "gap %s%s", Deviation(endGap->value).c_str(),
					endGap->stale ? " (stale)" : "");
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("End gap, squared in a bore: %s", endGap->toolName.c_str());
			}
			else if (!feature.directlyMeasurable)
				ImGui::TextDisabled(feature.name == FeatureNames::CLOSED_DIAMETER ? "Gap it in a bore" : "Check on stand");
			else if (reading && reading->hasSize)
			{
				auto measured = FromReading(feature, *reading);
				auto text = FeatureValue(feature, measured.Size());
				if (reading->stale)
					ImGui::TextColored(COLOR_DIM, "%s (stale)", text.c_str());
				else
					ImGui::TextUnformatted(text.c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", reading->toolName.c_str());
			}
			else
				ImGui::TextDisabled("-");

			ImGui::TableSetColumnIndex(3);
			if (!feature.HasFinishSpec())
				ImGui::TextDisabled("n/a");
			else if (reading && reading->finishRa)
				ImGui::TextColored(reading->stale ? COLOR_DIM : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Units::FormatFinish(*reading->finishRa).c_str());
			else
				ImGui::TextDisabled("-");

			ImGui::TableSetColumnIndex(4);
			if (endGap && !endGap->stale)
				ImGui::TextColored(gapSpec.Contains(endGap->value) ? COLOR_GOOD : COLOR_BAD, "%s",
					endGap->value < gapSpec.min ? "Gap too tight" : endGap->value > gapSpec.max ? "Gap too wide" : "Gap in spec");
			else if (reading && !reading->stale && (reading->hasSize || reading->finishRa))
			{
				auto measured = FromReading(feature, *reading);
				uint32_t issues = 0;
				if (reading->hasSize)
					issues |= measured.Evaluate(false);
				if (reading->finishRa)
					issues |= measured.Evaluate(true) & (FeatureIssue::ROUGH | FeatureIssue::GLAZED);

				std::string verdict = DescribeIssues(issues, feature.kind);
				if (reading->hasSize && issues == FeatureIssue::NONE)
					if (auto sizeClass = measured.MatchSizeClass(); sizeClass && *sizeClass != 0.0)
						verdict += " (" + SizeClassLabel(*sizeClass) + ")";
				ImGui::TextColored(issues == FeatureIssue::NONE ? COLOR_GOOD : COLOR_BAD, "%s", verdict.c_str());
			}
			else
				ImGui::TextDisabled("Not measured");

			// Measuring happens in the shop: these say which tool to pick up.
			ImGui::TableSetColumnIndex(5);
			if (feature.directlyMeasurable)
				DrawToolCell(workshop, [&](const MeasuringTool& tool) { return tool.CanMeasure(feature); });
			if (feature.HasFinishSpec())
				DrawToolCell(workshop, [](const MeasuringTool& tool) { return tool.measuresFinish; }, "Ra: ");

			if (m_xray)
			{
				ImGui::TableSetColumnIndex(6);
				std::string truth = FeatureValue(feature, feature.Size());
				if (feature.IsRound())
					truth += std::format("  T{:.3f} O{:.3f}", feature.Taper(), feature.OutOfRound());
				if (feature.HasFinishSpec())
					truth += std::format("  {:.2f}Ra", feature.surfaceRa);
				ImGui::TextColored(COLOR_XRAY, "%s", truth.c_str());
			}

			// The open-me cue, brighter on the row under the mouse and on the open one.
			ImGui::TableSetColumnIndex(m_xray ? 7 : 6);
			if (selected)
				ImGui::TextColored(COLOR_GOOD, "Open v");
			else
				ImGui::TextColored(hovered ? COLOR_XRAY : COLOR_DIM, "Details >");
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	if (auto feature = part->FindFeature(m_selectedFeature))
	{
		ImGui::Spacing();
		ImGui::SeparatorText(feature->name.c_str());
		// The details open below the whole table, which can be off screen on a big part.
		if (m_scrollToDetail)
		{
			ImGui::SetScrollHereY(0.f);
			m_scrollToDetail = false;
		}
		// What it is and what it means, between its name and its spec.
		if (auto explanation = FeatureExplanation(part->GetType(), feature->name); !explanation.empty())
		{
			ImGui::TextWrapped("%s", explanation.c_str());
			ImGui::Spacing();
		}
		DrawFeatureDetail(workshop, *part, *feature);
		DrawFixAdvice(workshop, *part, *feature);
		DrawMachiningPanel(workshop, *part, *feature);
	}
	else
	{
		ImGui::Spacing();
		ImGui::TextDisabled("No feature open. Click one above to see its readings and how to fix it.");
	}
}

void WorkshopUI::DrawFeatureDetail(Workshop& workshop, Part& part, const Feature& feature)
{
	auto reading = part.GetReading(feature.name);

	// One label per value, in two side-by-side cards: what it should be, and what you found.
	auto row = [](const char* label) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextDisabled("%s", label);
		ImGui::TableSetColumnIndex(1);
	};
	ImGuiTableFlags cardFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg;

	if (ImGui::BeginTable("SpecAndReading", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableNextRow();

		// Spec
		ImGui::TableSetColumnIndex(0);
		ImGui::SeparatorText("Spec");
		if (ImGui::BeginTable("Spec", 2, cardFlags))
		{
			row(feature.IsRound() ? "Diameter" : "Size");
			ImGui::TextUnformatted(SpecText(feature).c_str());
			if (feature.sizeClasses.size() > 1)
				for (double sizeClass : feature.sizeClasses)
				{
					if (std::abs(sizeClass) < 1e-6)
						continue;
					row(std::format("{} oversize", SizeClassLabel(sizeClass)).c_str());
					ImGui::TextUnformatted(Size(feature.spec.Mid() + sizeClass).c_str());
				}
			if (feature.maxTaper > 0.0)
			{
				row("Max taper");
				ImGui::Text("%s", Deviation(feature.maxTaper).c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Largest allowed difference between top and bottom.");
			}
			if (feature.maxOutOfRound > 0.0)
			{
				row("Max out-of-round");
				ImGui::Text("%s", Deviation(feature.maxOutOfRound).c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Largest allowed difference between the thrust and pin directions.");
			}
			if (feature.HasFinishSpec())
			{
				row("Finish");
				if (feature.maxRa > 0.0)
					ImGui::Text("%.2f - %.2f um Ra", feature.minRa, feature.maxRa);
				else
					ImGui::Text("at least %.2f um Ra", feature.minRa);
			}
			ImGui::EndTable();
		}

		// Your readings
		ImGui::TableSetColumnIndex(1);
		ImGui::SeparatorText("Your last reading");
		bool hasReading = reading && (reading->hasSize || reading->finishRa);
		// A ring: its end gap, squared in a bore.
		const FeatureReading* endGap = feature.name == FeatureNames::CLOSED_DIAMETER ? part.GetReading(FeatureNames::END_GAP) : nullptr;
		if (endGap)
		{
			const Range& gapSpec = workshop.GetSpec().ringGap;
			if (endGap->stale)
				ImGui::TextColored(COLOR_WARN, "Out of date: it's been filed since.");
			if (ImGui::BeginTable("Reading", 2, cardFlags))
			{
				row("End gap");
				ImGui::TextColored(endGap->stale ? COLOR_DIM : gapSpec.Contains(endGap->value) ? COLOR_GOOD : COLOR_BAD, "%s", Deviation(endGap->value).c_str());
				row("Target");
				ImGui::Text("%s - %s", Deviation(gapSpec.min).c_str(), Deviation(gapSpec.max).c_str());
				row("Measured with");
				ImGui::TextUnformatted(endGap->toolName.c_str());
				ImGui::EndTable();
			}
		}
		else if (!hasReading)
		{
			ImGui::PushTextWrapPos(0.f);
			if (feature.directlyMeasurable && workshop.BestToolFor(feature) < 0)
				ImGui::TextDisabled("Not measured yet, and none of your tools can measure it: the Measure with column says which to buy.");
			else if (feature.directlyMeasurable)
				ImGui::TextDisabled("Not measured yet. Use the %s on it.", ToolHint(workshop, workshop.BestToolFor(feature)).c_str());
			else if (feature.name == FeatureNames::CLOSED_DIAMETER)
				ImGui::TextDisabled("Not gapped yet. Set it on its own in a bore (in a block on the stand or off it), push it down square "
					"with a piston and read the gap with the feeler gauge.");
			else
				ImGui::TextDisabled("Not measured yet. This one is checked with the part on the engine stand.");
			ImGui::PopTextWrapPos();
		}
		else
		{
			if (reading->stale)
				ImGui::TextColored(COLOR_WARN, "Out of date: it's been cut since.");
			auto measured = FromReading(feature, *reading);
			uint32_t issues = reading->hasSize ? measured.Evaluate(false) : FeatureIssue::NONE;
			ImVec4 valueColor = reading->stale ? COLOR_DIM : ImGui::GetStyleColorVec4(ImGuiCol_Text);
			auto judged = [&](bool bad) { return reading->stale ? COLOR_DIM : bad ? COLOR_BAD : COLOR_GOOD; };

			if (ImGui::BeginTable("Reading", 2, cardFlags))
			{
				if (reading->hasSize)
				{
					row(feature.IsRound() ? "Diameter" : "Size");
					ImGui::TextColored(judged(issues & (FeatureIssue::UNDERSIZE | FeatureIssue::OVERSIZE | FeatureIssue::BEYOND_LIMITS)),
						"%s", FeatureValue(feature, measured.Size()).c_str());
					if (auto sizeClass = measured.MatchSizeClass(); sizeClass && *sizeClass != 0.0)
					{
						ImGui::SameLine();
						ImGui::TextDisabled("(%s)", SizeClassLabel(*sizeClass).c_str());
					}
					if (feature.IsRound())
					{
						row("Taper");
						ImGui::TextColored(judged(issues & FeatureIssue::TAPER), "%s", Deviation(measured.Taper()).c_str());
						row("Out-of-round");
						ImGui::TextColored(judged(issues & FeatureIssue::OUT_OF_ROUND), "%s", Deviation(measured.OutOfRound()).c_str());
					}
				}
				if (reading->finishRa)
				{
					row("Finish");
					uint32_t finishIssues = measured.Evaluate(true) & (FeatureIssue::ROUGH | FeatureIssue::GLAZED);
					ImGui::TextColored(judged(finishIssues != 0), "%s", Units::FormatFinish(*reading->finishRa).c_str());
					issues |= finishIssues;
				}
				row("Verdict");
				ImGui::TextColored(judged(issues != FeatureIssue::NONE), "%s", DescribeIssues(issues, feature.kind).c_str());
				row("Measured with");
				ImGui::TextColored(valueColor, "%s", reading->toolName.c_str());
				ImGui::EndTable();
			}
		}
		ImGui::EndTable();
	}

	// Round features: every gauge reading, so the taper and out-of-round can be seen where they are.
	if (feature.IsRound() && reading && reading->hasSize)
	{
		if (ImGui::TreeNodeEx("Gauge readings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("Readings", Feature::AXES + 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("Depth");
				for (auto axisName : AXIS_NAMES)
					ImGui::TableSetupColumn(axisName);
				ImGui::TableSetupColumn("Out-of-round");
				HeadersRow({
					"Where along the bore or journal the reading was taken. Top vs. bottom shows taper.",
					"Across the thrust axis, where the piston pushes hardest.",
					"Along the pin axis, at right angles to thrust.",
					"Thrust minus pin at this depth." });

				for (int station = 0; station < Feature::STATIONS; station++)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextDisabled("%s", STATION_NAMES[station]);
					for (int axis = 0; axis < Feature::AXES; axis++)
					{
						ImGui::TableSetColumnIndex(axis + 1);
						ImGui::TextUnformatted(Size(reading->samples[station][axis]).c_str());
					}
					ImGui::TableSetColumnIndex(Feature::AXES + 1);
					ImGui::TextDisabled("%s", Deviation(std::abs(reading->samples[station][0] - reading->samples[station][1])).c_str());
				}
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}
	}

	if (m_xray)
	{
		std::string truth = FeatureValue(feature, feature.Size());
		if (feature.IsRound())
			truth += std::format("   taper {}   out-of-round {}", Deviation(feature.Taper()), Deviation(feature.OutOfRound()));
		if (feature.HasFinishSpec())
			truth += std::format("   {:.2f} um Ra", feature.surfaceRa);
		ImGui::TextColored(COLOR_XRAY, "X-ray: %s", truth.c_str());
	}

	if (part.GetType() == PartType::PISTON_RING)
		ImGui::TextWrapped("A ring's gap depends on the bore it lives in. Set it on its own in its bore (in a block on the stand or off it), push it down square with a piston and check the gap with feelers, then set it up in the ring filer to open the gap.");
	if (feature.name == FeatureNames::SKIRT)
		ImGui::TextWrapped("Piston-to-wall clearance for this alloy: %s (cold). Subtract this skirt from the measured bore.",
			RangeText(workshop.GetSpec().PistonToWallFor(part.GetMaterial())).c_str());
}

void WorkshopUI::DrawFixAdvice(Workshop& workshop, const Part& part, const Feature& feature)
{
	ImGui::SeparatorText("Bringing it back into spec");
	ImGui::PushTextWrapPos(0.f);

	auto step = [](const std::string& text) { ImGui::Bullet(); ImGui::TextUnformatted(text.c_str()); };
	// Which machine does an operation, and whether the shop has one yet.
	auto machineFor = [&](MachineOperation operation) {
		const auto& machines = MachineCatalog::All();
		std::string text;
		for (size_t i = 0; i < machines.size(); i++)
		{
			if (machines[i].operation != operation)
				continue;
			if (!text.empty())
				text += " or ";
			text += workshop.OwnsMachine(i) ? std::format("the {} (yours)", machines[i].name)
				: std::format("the {} (${:.0f} at the parts computer)", machines[i].name, machines[i].price);
		}
		return text.empty() ? std::string("a machine shop") : text;
	};
	auto replace = [&](const std::string& why) {
		step(std::format("{} There's no machine for it here: replace the {} (Parts at the parts computer).", why, PartTypeName(part.GetType())));
	};

	// Only ever what the player has measured.
	auto reading = part.GetReading(feature.name);
	bool measured = reading && !reading->stale && (reading->hasSize || reading->finishRa);
	if (!feature.directlyMeasurable)
	{
		if (part.GetType() == PartType::PISTON_RING)
		{
			step("Set the ring on its own in its bore (in the block, on the stand or off it), push it down square with a piston and check the gap with feelers.");
			step(std::format("Too tight: set the ring up in {} and open the gap a little at a time, checking as you go.", machineFor(MachineOperation::FILE_RING)));
			step("Too wide: filing only opens it. Fit a new ring, or the next oversize ring with an overbored cylinder.");
		}
		else
			step("This one only shows up once the part is on the engine (installed height, spring rate): check it on the stand.");
		ImGui::PopTextWrapPos();
		return;
	}
	if (!measured)
	{
		ImGui::TextDisabled(reading && reading->stale ? "Your reading is out of date: measure it again to see what it needs."
			: "Measure it first: the advice comes from your own readings.");
		ImGui::PopTextWrapPos();
		return;
	}

	auto found = FromReading(feature, *reading);
	uint32_t issues = reading->hasSize ? found.Evaluate(false) : FeatureIssue::NONE;
	if (reading->finishRa)
		issues |= found.Evaluate(true) & (FeatureIssue::ROUGH | FeatureIssue::GLAZED);
	if (issues == FeatureIssue::NONE)
	{
		ImGui::TextColored(COLOR_GOOD, "In spec by your reading: nothing to do.");
		if (auto sizeClass = found.MatchSizeClass(); sizeClass && *sizeClass != 0.0)
			step(std::format("It's at the {} size class, so it needs matching {} parts.", SizeClassLabel(*sizeClass),
				part.GetType() == PartType::BLOCK ? "oversize pistons and rings" : part.GetType() == PartType::CRANKSHAFT ? "undersize bearings"
				: part.GetType() == PartType::CYLINDER_HEAD ? "oversize-stem valves" : ""));
		ImGui::PopTextWrapPos();
		return;
	}

	bool sizeWrong = issues & (FeatureIssue::OVERSIZE | FeatureIssue::UNDERSIZE | FeatureIssue::TAPER | FeatureIssue::OUT_OF_ROUND | FeatureIssue::BEYOND_LIMITS);
	bool finishWrong = issues & (FeatureIssue::ROUGH | FeatureIssue::GLAZED);
	auto supports = [&](MachineOperation operation) { return feature.SupportsOperation(operation); };

	// The size class to machine to: the first one that cleans up the worst of the wear, leaving a
	// little stock for the finishing pass.
	auto cleanupClass = [&](bool growing) -> std::optional<double> {
		constexpr double FINISH_STOCK = 0.02;
		for (double sizeClass : feature.sizeClasses)
		{
			if (growing && feature.spec.Min() + sizeClass >= found.MaxSample() + FINISH_STOCK)
				return sizeClass;
			if (!growing && feature.spec.Max() + sizeClass <= found.MinSample() - FINISH_STOCK)
				return sizeClass;
		}
		return std::nullopt;
	};

	if (feature.kind == FeatureKind::BORE && supports(MachineOperation::BORE))
	{
		// A cylinder bore.
		if (sizeWrong)
		{
			if (auto sizeClass = cleanupClass(true))
			{
				step(std::format("Bore it out to the {} oversize ({}) with {}.", SizeClassLabel(*sizeClass),
					Size(feature.spec.Mid() + *sizeClass), machineFor(MachineOperation::BORE)));
				step(std::format("Then hone it to the finish ({:.2f} - {:.2f} um Ra) with {}.", feature.minRa, feature.maxRa, machineFor(MachineOperation::HONE)));
				step(std::format("Fit a {} piston and top ring to it.", SizeClassLabel(*sizeClass)));
			}
			else
				step("It's worn past the last oversize: the block needs sleeving at a machine shop, or replacing.");
		}
		else if (finishWrong)
			step(std::format("The size is fine but the finish isn't: hone it to {:.2f} - {:.2f} um Ra with {} to break the glaze and lay down a crosshatch.",
				feature.minRa, feature.maxRa, machineFor(MachineOperation::HONE)));
	}
	else if (feature.kind == FeatureKind::BORE && supports(MachineOperation::REAM))
	{
		// A valve guide.
		if (auto sizeClass = cleanupClass(true); sizeClass && sizeWrong)
		{
			step(std::format("Ream it to the {} oversize with {}.", SizeClassLabel(*sizeClass), machineFor(MachineOperation::REAM)));
			step(std::format("Fit a {} oversize-stem valve to it.", SizeClassLabel(*sizeClass)));
		}
		else
			step("It's worn past the largest oversize: the guide needs replacing at a machine shop, or a new head.");
	}
	else if (feature.kind == FeatureKind::JOURNAL && supports(MachineOperation::GRIND))
	{
		// A crank journal.
		if (sizeWrong)
		{
			if (auto sizeClass = cleanupClass(false))
			{
				step(std::format("Grind it down to the {} undersize ({}) with {}.", SizeClassLabel(*sizeClass),
					Size(feature.spec.Mid() + *sizeClass), machineFor(MachineOperation::GRIND)));
				step(std::format("Polish it afterwards with {}.", machineFor(MachineOperation::POLISH)));
				step(std::format("Fit {} bearings to it (thicker shells make up the difference).", SizeClassLabel(*sizeClass)));
			}
			else
				step("It's worn past the last undersize: the crank needs replacing.");
		}
		else if (finishWrong)
			step(std::format("Polish it with {}. If the scoring's deep, polishing won't take it out: grind it to the next undersize instead.",
				machineFor(MachineOperation::POLISH)));
	}
	else if (feature.kind == FeatureKind::JOURNAL && supports(MachineOperation::POLISH))
	{
		// A cam journal: it can be polished, not ground.
		if (finishWrong && !sizeWrong)
			step(std::format("Polish it with {}.", machineFor(MachineOperation::POLISH)));
		else
			replace("Worn undersize.");
	}
	else if (feature.kind == FeatureKind::FLATNESS && supports(MachineOperation::SURFACE))
	{
		step(std::format("Skim it flat with {}. Take off at least your reading ({}) so the cut cleans it up.", machineFor(MachineOperation::SURFACE),
			Deviation(found.Size())));
		if (part.GetType() == PartType::BLOCK)
			step("Decking the block lowers the deck height: the pistons sit closer to the head, so recheck deck clearance and quench.");
		else if (part.GetType() == PartType::CYLINDER_HEAD)
			step("Skimming the head shrinks the chambers and raises compression: CC them again afterwards.");
	}
	else if (feature.kind == FeatureKind::RUNOUT && supports(MachineOperation::CUT_SEAT))
		step(std::format("Recut the seat true with {}. Each cut sinks the seat a little, which raises the installed height.", machineFor(MachineOperation::CUT_SEAT)));
	else if (feature.kind == FeatureKind::RUNOUT && supports(MachineOperation::FACE_VALVE))
		step(std::format("Reface the valve with {}. It takes off margin, so check the margin is still thick enough afterwards.", machineFor(MachineOperation::FACE_VALVE)));
	else if (supports(MachineOperation::CUT_SEAT))
		step(std::format("Cut the seat to width with {}: a top cut narrows it, a bottom cut moves it down.", machineFor(MachineOperation::CUT_SEAT)));
	else if (feature.name == FeatureNames::MARGIN)
		replace("The margin can't be built back up.");
	else if (feature.name == FeatureNames::WALL_THICKNESS)
		replace("Bearing shells are never machined.");
	else if (feature.name == FeatureNames::FREE_LENGTH || feature.name == FeatureNames::SEAT_PRESSURE)
		replace("A tired spring has sagged.");
	else if (feature.kind == FeatureKind::BORE && supports(MachineOperation::LINE_BORE))
	{
		// A main housing: the whole line is done together.
		if (found.MaxSample() > feature.spec.Max() + 0.05)
			step("It's too far oversize to bring back by line boring: the block needs oversize-OD main bearings at a machine shop, or replacing.");
		else
		{
			step(std::format("Line bore the block with {}: it grinds the main caps and bores all {} housings back to {}, round and in line, in one setup.",
				machineFor(MachineOperation::LINE_BORE), workshop.GetSpec().mainJournals, Size(feature.spec.Mid())));
			step("Measure every housing again afterwards, then Plastigauge the mains once the crank and caps are in.");
		}
	}
	else
		replace("Wear like this can't be machined back.");

	ImGui::PopTextWrapPos();
}

void WorkshopUI::DrawMachiningPanel(Workshop& workshop, Part& part, const Feature& feature)
{
	if (feature.operations.empty())
		return;

	// Cutting only happens with the part set up in a machine, and only that machine can work on it.
	const auto& machines = MachineCatalog::All();
	ImGui::Spacing();
	ImGui::SeparatorText("Machining");

	if (m_selectedPartMachine < 0 || m_selectedPartMachine >= static_cast<int>(machines.size()))
	{
		std::string machinesFor;
		for (const auto& candidate : machines)
			if (Machining::CanMachine(candidate, feature))
				machinesFor += (machinesFor.empty() ? "" : ", ") + candidate.name;
		ImGui::PushTextWrapPos(0.f);
		if (machinesFor.empty())
			ImGui::TextDisabled("Set this part up in a machine to cut it.");
		else
			ImGui::TextDisabled("Set this part up in a machine to cut it (%s).", machinesFor.c_str());
		ImGui::PopTextWrapPos();
		return;
	}

	const auto& machine = machines[m_selectedPartMachine];
	if (!Machining::CanMachine(machine, feature))
	{
		ImGui::TextDisabled("The %s can't work on this feature.", machine.name.c_str());
		return;
	}
	if (m_selectedMachine != m_selectedPartMachine)
	{
		m_selectedMachine = m_selectedPartMachine;
		m_machineParameter = machine.operation == MachineOperation::SURFACE ? Units::ToDisplay(0.05, m_units) : 0.0;
	}

	if (Machining::UsesParameter(machine.operation))
	{
		ImGui::SetNextItemWidth(180.f);
		const char* format = m_units == Units::System::IMPERIAL ? "%.5f" : "%.4f";
		ImGui::InputDouble(Machining::ParameterName(machine.operation), &m_machineParameter, 0.0, 0.0, format);
		ImGui::SameLine();
		ImGui::TextDisabled(m_units == Units::System::IMPERIAL ? "in" : "mm");

		// Quick targets: hone/grind to the middle of a size class; bore leaves stock for the hone.
		if (machine.operation == MachineOperation::BORE || machine.operation == MachineOperation::HONE || machine.operation == MachineOperation::GRIND)
		{
			constexpr double HONE_STOCK = 0.06;
			for (double sizeClass : feature.sizeClasses)
			{
				double target = feature.spec.Mid() + sizeClass - (machine.operation == MachineOperation::BORE ? HONE_STOCK : 0.0);
				ImGui::SameLine();
				if (ImGui::SmallButton(SizeClassLabel(sizeClass).c_str()))
					m_machineParameter = Units::ToDisplay(target, m_units);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(machine.operation == MachineOperation::BORE ? "%s, leaving 0.06 mm for the hone" : "%s", Size(target).c_str());
			}
		}
	}

	if (machine.operation == MachineOperation::LINE_BORE)
	{
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("Does all %d main housings at once, whichever one is open: the caps are ground and the housings bored back to "
			"%s, round and in line. A housing more than %s oversize can't be brought back.", workshop.GetSpec().mainJournals,
			Size(workshop.GetSpec().mainHousing.Mid()).c_str(), Deviation(machine.maxRemoval).c_str());
		ImGui::PopTextWrapPos();
	}
	ImGui::TextColored(COLOR_WARN, "Material only comes off. This can't be undone.");
	if (ImGui::Button(Machining::OperationName(machine.operation)))
	{
		auto result = workshop.MachineFeature(m_selectedMachine, part, feature.name, Units::FromDisplay(m_machineParameter, m_units));
		m_lastMachiningMessage = result.message;
	}
	if (!m_lastMachiningMessage.empty())
		ImGui::TextWrapped("%s", m_lastMachiningMessage.c_str());
}

void WorkshopUI::DrawPartsStore(Workshop& workshop, bool hardware)
{
	// Each tab keeps to its own kind: switching tabs picks the first of that kind.
	// The car's parts aren't the engine store's (the car has its own section of the store, once the shop has one).
	// (The project car's own nuts, its lug nuts and M10 and M12 nuts, are hardware like any other and sold here too.)
	auto belongs = [&](PartType type) {
		bool fastener = PartCatalog::Get(type).system == PartSystem::FASTENERS;
		return fastener == hardware && (!IsCarPart(type) || fastener);
	};
	if (!belongs(m_order.type))
		for (auto type : PartCatalog::All())
			if (belongs(type) && PartCatalog::Get(type).listed)
			{
				m_order = {};
				m_order.type = type;
				if (!Workshop::SoldUsed(type))
					m_order.condition = PartCondition::NEW;
				break;
			}

	// Catalogue cards on the left, the order form for the selected part on the right.
	constexpr float CARD_WIDTH = 118.f;
	constexpr float CARD_HEIGHT = 112.f;
	constexpr float ICON_SIZE = 64.f;

	if (ImGui::BeginChild("Catalogue", { ImGui::GetContentRegionAvail().x * 0.55f, 0.f }, ImGuiChildFlags_Borders))
	{
		if (hardware)
		{
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("Nuts and bolts by size: any of a size fits every hole of that size. They go in finger tight; do them up "
				"with a socket on a ratchet (or a wrench) of their size, and to spec with a torque wrench.");
			ImGui::PopTextWrapPos();
		}
		else
			ImGui::TextDisabled("Parts ship to the roll-up door. Budget parts scatter wider; measure everything.");
		ImGui::Spacing();
		// The search bar: what's typed narrows the cards to the ones whose name, or section, has it in (any case).
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##search", hardware ? "Search hardware (e.g. M8, nut)" : "Search parts (e.g. gasket, cooling)",
			m_storeSearch, sizeof(m_storeSearch));
		auto lower = [](std::string text) {
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		};
		std::string search = lower(m_storeSearch);
		search.erase(0, search.find_first_not_of(' '));
		search.erase(search.find_last_not_of(' ') + 1);
		auto matches = [&](PartType type, const std::string& sectionName) {
			return search.empty() || lower(PartCatalog::StoreName(type)).find(search) != std::string::npos
				|| lower(PartTypeName(type)).find(search) != std::string::npos || lower(sectionName).find(search) != std::string::npos;
		};
		ImGui::Spacing();

		float available = ImGui::GetContentRegionAvail().x;
		int columns = (std::max)(1, static_cast<int>(available / (CARD_WIDTH + ImGui::GetStyle().ItemSpacing.x)));
		int column = 0;
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// One section per engine system; the hardware in two, its bolts and then its nuts, each smallest first.
		// (The other halves of a set aren't listed: they come with it.)
		auto isNut = [](PartType type) { return std::string_view(PartTypeName(type)).find("Nut") != std::string_view::npos; };
		std::vector<std::pair<PartType, std::string>> shown;
		for (int pass = 0; pass < (hardware ? 2 : 1); pass++)
			for (auto type : PartCatalog::All())
				if (belongs(type) && PartCatalog::Get(type).listed && (!hardware || isNut(type) == (pass == 1)))
					shown.push_back({ type, hardware ? (pass == 0 ? "Bolts" : "Nuts") : PartSystemName(PartCatalog::Get(type).system) });
		// The wheels' lug nuts after the plain nuts, M8 to M12.
		std::stable_partition(shown.begin(), shown.end(), [](const auto& entry) { return entry.first != PartType::LUG_NUT; });
		std::erase_if(shown, [&](const auto& entry) { return !matches(entry.first, entry.second); });
		if (shown.empty())
			ImGui::TextDisabled("Nothing matches \"%s\".", m_storeSearch);
		std::string section;
		for (const auto& entry : shown)
		{
			PartType type = entry.first;
			if (entry.second != section)
			{
				if (column != 0)
					ImGui::NewLine();
				column = 0;
				ImGui::SeparatorText(entry.second.c_str());
				section = entry.second;
			}
			ImGui::PushID(static_cast<int>(type));
			ImVec2 cardMin = ImGui::GetCursorScreenPos();
			bool selected = m_order.type == type;
			if (ImGui::InvisibleButton("card", { CARD_WIDTH, CARD_HEIGHT }))
			{
				m_order.type = type;
				m_order.sizeOffset = 0.0;
				m_order.quantity = 1;
				if (!Workshop::SoldUsed(type))
					m_order.condition = PartCondition::NEW;
			}
			bool hovered = ImGui::IsItemHovered();
			ImVec2 cardMax = { cardMin.x + CARD_WIDTH, cardMin.y + CARD_HEIGHT };

			ProductCard(drawList, cardMin, cardMax, cardMin.y + ICON_SIZE + 10.f, selected, hovered);

			PartIcons::Draw(drawList, type, { cardMin.x + (CARD_WIDTH - ICON_SIZE) * 0.5f, cardMin.y + 8.f }, ICON_SIZE);

			// Long names are clipped to the card, with the full name on hover.
			const char* name = PartCatalog::StoreName(type);
			ImVec2 textSize = ImGui::CalcTextSize(name);
			drawList->PushClipRect(cardMin, cardMax, true);
			drawList->AddText({ cardMin.x + (std::max)(4.f, (CARD_WIDTH - textSize.x) * 0.5f), cardMin.y + ICON_SIZE + 14.f }, Site().cardInk, name);
			drawList->PopClipRect();
			if (hovered && textSize.x > CARD_WIDTH - 8.f)
				ImGui::SetTooltip("%s", name);
			double cheapest = PartFactory::PriceWithCompanions(type, PartQuality::OEM,
				Workshop::SoldUsed(type) ? PartCondition::USED : PartCondition::NEW, workshop.GetSpec()) * workshop.PartPriceFactor();
			// Cents for the small stuff, so a bolt isn't "from $0".
			std::string from = cheapest < 10.0 ? std::format("from ${:.2f}", cheapest) : std::format("from ${:.0f}", cheapest);
			ImVec2 fromSize = ImGui::CalcTextSize(from.c_str());
			drawList->AddText({ cardMin.x + (CARD_WIDTH - fromSize.x) * 0.5f, cardMin.y + ICON_SIZE + 30.f }, Site().cardPrice, from.c_str());

			if (++column < columns)
				ImGui::SameLine();
			else
				column = 0;
			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	if (!ImGui::BeginChild("Order", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}

	ImGui::Text("%s", PartCatalog::StoreName(m_order.type));
	DrawPreview(PartTypeName(m_order.type));
	ImGui::PushTextWrapPos(0.f);
	ImGui::TextDisabled("%s", PartIcons::Description(m_order.type));
	ImGui::PopTextWrapPos();
	ImGui::Separator();

	if (Workshop::SoldUsed(m_order.type))
	{
		int condition = static_cast<int>(m_order.condition);
		ImGui::RadioButton("New", &condition, static_cast<int>(PartCondition::NEW));
		ImGui::SameLine();
		ImGui::RadioButton("Used core", &condition, static_cast<int>(PartCondition::USED));
		m_order.condition = static_cast<PartCondition>(condition);
	}

	auto sizes = Workshop::SizeOptions(m_order.type);
	if (sizes.size() > 1)
	{
		ImGui::SetNextItemWidth(220.f);
		if (ImGui::BeginCombo("Size", SizeClassLabel(m_order.sizeOffset).c_str()))
		{
			for (double size : sizes)
				if (ImGui::Selectable(SizeClassLabel(size).c_str(), m_order.sizeOffset == size))
					m_order.sizeOffset = size;
			ImGui::EndCombo();
		}
		ImGui::PushTextWrapPos(0.f);
		if (m_order.type == PartType::PISTON || m_order.type == PartType::PISTON_RING)
			ImGui::TextDisabled("Oversize parts suit cylinders bored to the matching size class.");
		else if (m_order.type == PartType::HEAD_GASKET)
			ImGui::TextDisabled("The fire rings are cut for cylinders bored to that size class. A gasket smaller than the bores won't go on.");
		else if (m_order.type == PartType::INTAKE_VALVE || m_order.type == PartType::EXHAUST_VALVE)
			ImGui::TextDisabled("Oversize stems suit guides reamed to the matching size class.");
		else
			ImGui::TextDisabled("Undersize bearings have thicker walls to suit journals ground to that size class.");
		ImGui::PopTextWrapPos();
	}

	ImGui::SetNextItemWidth(220.f);
	ImGui::InputInt("Quantity", &m_order.quantity);
	m_order.quantity = std::clamp(m_order.quantity, 1, 16);

	ImGui::Spacing();
	CartLine line;
	line.kind = CartLine::Kind::PART;
	line.order = m_order;
	double quote = workshop.Quote(m_order);
	AddToCartButton(workshop, line, (quote < 10.0 ? std::format("Add to cart (${:.2f})", quote) : std::format("Add to cart (${:.0f})", quote)).c_str());

	ImGui::EndChild();
}

void WorkshopUI::DrawStoreTab(Workshop& workshop)
{
	switch (m_storeCategory)
	{
		case StoreCategory::PARTS:		DrawPartsStore(workshop, false); break;
		case StoreCategory::HARDWARE:	DrawPartsStore(workshop, true); break;
		case StoreCategory::TOOLS:		DrawToolStore(workshop, false); break;
		case StoreCategory::MACHINES:	DrawMachineStore(workshop); break;
		case StoreCategory::FURNITURE:	DrawFurnitureStore(workshop); break;
		case StoreCategory::PAINT:		DrawToolStore(workshop, true); break;
		case StoreCategory::CAR:		DrawCarStore(workshop); break;
	}
}

namespace
{
	// The parts computer is a web shop: each section of the catalogue is a page of the site.
	struct StorePage
	{
		StoreCategory category;
		const char* link;		// in the site's menu
		const char* path;		// in the address bar
		const char* title;
		const char* blurb;
	};
	const StorePage STORE_PAGES[] = {
		{ StoreCategory::PARTS, "Engine Parts", "parts", "Engine Parts", "New and used parts for every system, shipped the same day." },
		{ StoreCategory::HARDWARE, "Nuts & Bolts", "hardware", "Nuts & Bolts", "Fasteners by size: any of a size fits every hole of that size." },
		{ StoreCategory::TOOLS, "Tools", "tools", "Tools", "Measuring tools, spanners and the specialist tools for the job." },
		{ StoreCategory::MACHINES, "Machine Shop", "machine-shop", "Machine Shop", "Machines to bring parts back into spec, the dyno and the hoist." },
		{ StoreCategory::FURNITURE, "Shop Fittings", "shop-fittings", "Shop Fittings", "Benches, shelving, engine stands and the rest of a working shop." },
		{ StoreCategory::PAINT, "Paint", "paint", "Paint", "Markers, spray cans and thinner, for the parts and the walls." },
		{ StoreCategory::CAR, "LE86 Parts", "le86", "LE86 Parts", "Every part for the LE86, and the fluids to fill it." },
	};
	constexpr const char* STORE_NAME = "MonkeyKobo";
	constexpr const char* STORE_HOST = "monkeykobo.love/shop";	// the site, and its shop

	// Lower case, words joined with dashes, for the address bar.
	std::string Slug(const std::string& name)
	{
		std::string slug;
		for (char c : name)
		{
			if (std::isalnum(static_cast<unsigned char>(c)))
				slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			else if (!slug.empty() && slug.back() != '-')
				slug += '-';
		}
		while (!slug.empty() && slug.back() == '-')
			slug.pop_back();
		return slug;
	}

	// A website's colours for the parts computer's window, light or dark (the Dark mode setting), pushed for as long as
	// it's drawn; and the shared status colours set to read on its pages.
	class WebsiteTheme
	{
	public:
		explicit WebsiteTheme(bool dark)
		{
			static const SitePalette light = MakeSitePalette(false), darkPalette = MakeSitePalette(true);
			const SitePalette& p = dark ? darkPalette : light;
			g_site = &p;
			const std::pair<ImGuiCol, ImVec4> colors[] = {
				{ ImGuiCol_WindowBg, p.page }, { ImGuiCol_ChildBg, p.panel }, { ImGuiCol_PopupBg, p.popup },
				{ ImGuiCol_Text, p.text }, { ImGuiCol_TextDisabled, p.textDisabled },
				{ ImGuiCol_Border, p.border }, { ImGuiCol_Separator, p.border },
				{ ImGuiCol_FrameBg, p.frame }, { ImGuiCol_FrameBgHovered, p.frameHovered }, { ImGuiCol_FrameBgActive, p.frameActive },
				{ ImGuiCol_Button, p.button }, { ImGuiCol_ButtonHovered, p.buttonHovered }, { ImGuiCol_ButtonActive, p.buttonActive },
				{ ImGuiCol_Header, p.header }, { ImGuiCol_HeaderHovered, p.headerHovered }, { ImGuiCol_HeaderActive, p.headerActive },
				{ ImGuiCol_ScrollbarBg, p.scrollbar }, { ImGuiCol_ScrollbarGrab, p.grab }, { ImGuiCol_ScrollbarGrabHovered, p.grabHovered },
				{ ImGuiCol_ScrollbarGrabActive, p.grabActive },
				{ ImGuiCol_CheckMark, p.check }, { ImGuiCol_SliderGrab, p.check }, { ImGuiCol_SliderGrabActive, p.checkActive },
				{ ImGuiCol_TableHeaderBg, p.tableHeader }, { ImGuiCol_TableBorderStrong, p.border }, { ImGuiCol_TableBorderLight, p.tableLine },
				{ ImGuiCol_TableRowBgAlt, p.rowAlt }, { ImGuiCol_PlotHistogram, p.button },
			};
			for (const auto& [index, color] : colors)
				ImGui::PushStyleColor(index, color);
			m_pushed = static_cast<int>(std::size(colors));
			m_saved = { COLOR_GOOD, COLOR_WARN, COLOR_BAD, COLOR_DIM, COLOR_XRAY };
			COLOR_GOOD = p.good;
			COLOR_WARN = p.warn;
			COLOR_BAD = p.bad;
			COLOR_DIM = p.dim;
			COLOR_XRAY = p.xray;
		}
		~WebsiteTheme()
		{
			ImGui::PopStyleColor(m_pushed);
			COLOR_GOOD = m_saved[0];
			COLOR_WARN = m_saved[1];
			COLOR_BAD = m_saved[2];
			COLOR_DIM = m_saved[3];
			COLOR_XRAY = m_saved[4];
			g_site = nullptr;
		}
	private:
		int m_pushed = 0;
		std::array<ImVec4, 5> m_saved = {};
	};

	// Text at a size of its own (headings, the logo), drawn with the UI's font.
	void BigText(ImDrawList* drawList, ImVec2 at, float size, ImU32 color, const char* text)
	{
		drawList->AddText(ImGui::GetFont(), size, at, color, text);
	}
	float BigTextWidth(float size, const char* text)
	{
		return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.f, text).x;
	}
}

void WorkshopUI::DrawStoreWindow(Workshop& workshop)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize({ 1180.f, 800.f }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });
	if (m_requestedCategory)
	{
		ImGui::SetNextWindowFocus();
		ImGui::SetNextWindowCollapsed(false);
		m_storeCategory = *m_requestedCategory;
		m_requestedCategory = std::nullopt;
	}

	WebsiteTheme theme(m_darkMode);
	const SitePalette& site = Site();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });
	bool open = ImGui::Begin("Parts Computer", &m_storeOpen, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	if (!open)
	{
		ImGui::End();
		return;
	}

	const StorePage* page = &STORE_PAGES[0];
	for (const auto& candidate : STORE_PAGES)
		if (candidate.category == m_storeCategory)
			page = &candidate;

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetWindowPos();
	const float width = ImGui::GetWindowWidth();
	const float font = ImGui::GetFontSize();
	const ImU32 ink = site.ink, muted = site.muted, orange = Web(255, 153, 0, 255);
	const ImU32 brand = site.brand, white = Web(255, 255, 255, 255);

	// The browser: one tab, the buttons and the address bar.
	constexpr float BROWSER_HEIGHT = 74.f;
	drawList->AddRectFilled(origin, { origin.x + width, origin.y + BROWSER_HEIGHT }, site.browser);
	ImVec2 tabMin = { origin.x + 10.f, origin.y + 6.f }, tabMax = { origin.x + 290.f, origin.y + 34.f };
	drawList->AddRectFilled(tabMin, tabMax, site.tab, 8.f, ImDrawFlags_RoundCornersTop);
	drawList->AddRectFilled({ tabMin.x + 10.f, tabMin.y + 9.f }, { tabMin.x + 20.f, tabMin.y + 19.f }, orange, 2.f);
	std::string tabTitle = std::format("{} | {}", page->title, STORE_NAME);
	drawList->PushClipRect(tabMin, { tabMax.x - 24.f, tabMax.y }, true);
	drawList->AddText({ tabMin.x + 28.f, tabMin.y + (tabMax.y - tabMin.y - font) * 0.5f }, ink, tabTitle.c_str());
	drawList->PopClipRect();
	drawList->AddText({ tabMax.x - 18.f, tabMin.y + (tabMax.y - tabMin.y - font) * 0.5f }, muted, "x");
	drawList->AddText({ tabMax.x + 12.f, tabMin.y + (tabMax.y - tabMin.y - font) * 0.5f }, muted, "+");

	// Closing the browser closes the store.
	ImGui::SetCursorScreenPos({ origin.x + width - 44.f, origin.y + 4.f });
	if (ImGui::InvisibleButton("CloseStore", { 40.f, 30.f }))
		m_storeOpen = false;
	bool closeHovered = ImGui::IsItemHovered();
	if (closeHovered)
		drawList->AddRectFilled({ origin.x + width - 44.f, origin.y + 4.f }, { origin.x + width - 4.f, origin.y + 34.f }, Web(220, 60, 50, 255), 4.f);
	ImVec2 cross = { origin.x + width - 24.f, origin.y + 19.f };
	ImU32 crossColor = closeHovered ? white : ink;
	drawList->AddLine({ cross.x - 5.f, cross.y - 5.f }, { cross.x + 5.f, cross.y + 5.f }, crossColor, 1.5f);
	drawList->AddLine({ cross.x - 5.f, cross.y + 5.f }, { cross.x + 5.f, cross.y - 5.f }, crossColor, 1.5f);

	float barY = origin.y + 40.f;
	drawList->AddRectFilled({ origin.x, barY - 4.f }, { origin.x + width, origin.y + BROWSER_HEIGHT }, site.toolbar);
	float navX = origin.x + 14.f;
	for (const char* glyph : { "<", ">", "@" })
	{
		drawList->AddCircleFilled({ navX + 10.f, barY + 14.f }, 12.f, site.navButton);
		drawList->AddText({ navX + 10.f - ImGui::CalcTextSize(glyph).x * 0.5f, barY + 14.f - font * 0.5f }, muted, glyph);
		navX += 30.f;
	}
	ImVec2 addressMin = { navX + 8.f, barY + 1.f }, addressMax = { origin.x + width - 50.f, barY + 27.f };
	drawList->AddRectFilled(addressMin, addressMax, site.address, 13.f);
	drawList->AddRect(addressMin, addressMax, site.addressBorder, 13.f);
	// A padlock: the store's on https.
	ImVec2 lock = { addressMin.x + 16.f, addressMin.y + 15.f };
	drawList->PathArcTo({ lock.x, lock.y - 3.f }, 3.5f, 3.14159265f, 6.2831853f, 8);
	drawList->PathStroke(site.lock, 0, 1.5f);
	drawList->AddRectFilled({ lock.x - 5.f, lock.y - 3.f }, { lock.x + 5.f, lock.y + 4.f }, site.lock, 1.5f);
	// The page, and the product open on it.
	std::string product;
	if (m_storeCategory == StoreCategory::PARTS || m_storeCategory == StoreCategory::HARDWARE)
		product = Slug(PartCatalog::StoreName(m_order.type));
	else if ((m_storeCategory == StoreCategory::TOOLS || m_storeCategory == StoreCategory::PAINT) && m_selectedTool >= 0
		&& m_selectedTool < static_cast<int>(ToolCatalog::All().size()))
		product = Slug(ToolCatalog::Of(static_cast<size_t>(m_selectedTool)).name);
	std::string host = std::format("https://{}/", STORE_HOST);
	std::string path = product.empty() ? page->path : std::format("{}/{}", page->path, product);
	float textY = addressMin.y + (addressMax.y - addressMin.y - font) * 0.5f;
	drawList->PushClipRect(addressMin, { addressMax.x - 10.f, addressMax.y }, true);
	drawList->AddText({ addressMin.x + 30.f, textY }, muted, host.c_str());
	drawList->AddText({ addressMin.x + 30.f + ImGui::CalcTextSize(host.c_str()).x, textY }, ink, path.c_str());
	drawList->PopClipRect();
	drawList->AddText({ addressMax.x + 14.f, textY }, muted, "*");
	drawList->AddLine({ origin.x, origin.y + BROWSER_HEIGHT - 1.f }, { origin.x + width, origin.y + BROWSER_HEIGHT - 1.f }, site.browserLine);

	// The site's header: the logo, the balance and the cart.
	constexpr float HEADER_HEIGHT = 62.f;
	float headerY = origin.y + BROWSER_HEIGHT;
	drawList->AddRectFilled({ origin.x, headerY }, { origin.x + width, headerY + HEADER_HEIGHT }, brand);
	const float logoSize = font * 1.9f;
	BigText(drawList, { origin.x + 22.f, headerY + 8.f }, logoSize, orange, "Monkey");
	BigText(drawList, { origin.x + 22.f + BigTextWidth(logoSize, "Monkey"), headerY + 8.f }, logoSize, white, "Kobo");
	drawList->AddText({ origin.x + 24.f, headerY + 12.f + logoSize }, Web(190, 205, 225, 255),
		"Engine parts, tools and machines, delivered to your roll-up door");

	double cartTotal = workshop.CartTotal(m_cart);
	std::string cart = std::format("Cart ({})   ${:.0f}", m_cart.size(), cartTotal);
	float cartWidth = ImGui::CalcTextSize(cart.c_str()).x + 30.f;
	ImVec2 cartMin = { origin.x + width - 22.f - cartWidth, headerY + 15.f }, cartMax = { origin.x + width - 22.f, headerY + HEADER_HEIGHT - 15.f };
	drawList->AddRectFilled(cartMin, cartMax, orange, 16.f);
	drawList->AddText({ cartMin.x + 15.f, cartMin.y + (cartMax.y - cartMin.y - font) * 0.5f }, site.cartText, cart.c_str());
	std::string balance = std::format("${:.0f}", workshop.GetMoney());
	float balanceSize = font * 1.35f;
	float balanceX = cartMin.x - 28.f - (std::max)(BigTextWidth(balanceSize, balance.c_str()), ImGui::CalcTextSize("Your balance").x);
	drawList->AddText({ balanceX, headerY + 10.f }, Web(190, 205, 225, 255), "Your balance");
	BigText(drawList, { balanceX, headerY + 12.f + font }, balanceSize, white, balance.c_str());

	// The menu: a link for each section, the open one underlined.
	constexpr float MENU_HEIGHT = 38.f;
	float menuY = headerY + HEADER_HEIGHT;
	drawList->AddRectFilled({ origin.x, menuY }, { origin.x + width, menuY + MENU_HEIGHT }, site.menu);
	drawList->AddLine({ origin.x, menuY + MENU_HEIGHT - 1.f }, { origin.x + width, menuY + MENU_HEIGHT - 1.f }, site.menuLine);
	float linkX = origin.x + 22.f;
	for (const auto& link : STORE_PAGES)
	{
		float linkWidth = ImGui::CalcTextSize(link.link).x;
		ImGui::SetCursorScreenPos({ linkX - 8.f, menuY });
		ImGui::PushID(link.path);
		if (ImGui::InvisibleButton("Link", { linkWidth + 16.f, MENU_HEIGHT }))
			m_storeCategory = link.category;
		bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();
		bool active = link.category == m_storeCategory;
		if (hovered)
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		drawList->AddText({ linkX, menuY + (MENU_HEIGHT - font) * 0.5f }, active ? site.linkActive : hovered ? site.linkHover : ink, link.link);
		if (active || hovered)
			drawList->AddRectFilled({ linkX - 2.f, menuY + MENU_HEIGHT - 4.f }, { linkX + linkWidth + 2.f, menuY + MENU_HEIGHT - 1.f },
				active ? orange : site.linkUnderline);
		linkX += linkWidth + 34.f;
	}
	const char* delivery = "Free delivery to the roll-up door";
	drawList->AddText({ origin.x + width - 22.f - ImGui::CalcTextSize(delivery).x, menuY + (MENU_HEIGHT - font) * 0.5f }, site.delivery, delivery);

	// The page: where you are, its heading, the catalogue with the cart beside it, and the footer.
	ImGui::SetCursorScreenPos({ origin.x, menuY + MENU_HEIGHT });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 20.f, 12.f });
	ImGui::PushStyleColor(ImGuiCol_ChildBg, site.page);
	bool pageOpen = ImGui::BeginChild("Page", { 0.f, 0.f }, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
	if (pageOpen)
	{
		ImDrawList* pageList = ImGui::GetWindowDrawList();
		ImGui::TextColored(site.home, "Home");
		ImGui::SameLine(0.f, 6.f);
		ImGui::TextDisabled(">");
		ImGui::SameLine(0.f, 6.f);
		ImGui::TextUnformatted(page->title);
		ImVec2 heading = ImGui::GetCursorScreenPos();
		const float headingSize = font * 1.6f;
		BigText(pageList, heading, headingSize, site.heading, page->title);
		pageList->AddText({ heading.x + BigTextWidth(headingSize, page->title) + 16.f, heading.y + headingSize - font - 2.f }, muted, page->blurb);
		ImGui::Dummy({ 1.f, headingSize + 6.f });

		const float footerHeight = ImGui::GetTextLineHeightWithSpacing() * 6.f + 26.f;
		constexpr float CART_WIDTH = 330.f;
		if (ImGui::BeginChild("StoreMain", { -CART_WIDTH - ImGui::GetStyle().ItemSpacing.x, -footerHeight }, ImGuiChildFlags_None,
			ImGuiWindowFlags_NoBackground))
			DrawStoreTab(workshop);
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
		if (ImGui::BeginChild("Cart", { 0.f, -footerHeight }, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding))
			DrawCart(workshop);
		ImGui::EndChild();
		ImGui::PopStyleVar();

		// The footer: order updates, and the small print.
		ImGui::TextDisabled("Order updates");
		if (ImGui::BeginChild("Updates", { 0.f, ImGui::GetTextLineHeightWithSpacing() * 5.f }, ImGuiChildFlags_None))
			DrawLog(workshop);
		ImGui::EndChild();
		ImGui::TextDisabled("%s  |  Delivered to the roll-up door: open it with E. Machines and stands are set up when their crate is opened.  |  Tab / Esc: close",
			std::format("(c) {} Ltd", STORE_NAME).c_str());
	}
	ImGui::EndChild();
	ImGui::End();
}

void WorkshopUI::SetModelLookup(const std::function<uint32_t(const std::string&)>& lookup)
{
	m_modelLookup = lookup;
}

void WorkshopUI::DrawPreview(const std::string& modelName)
{
	// A turntable view of the item, as wide as the panel.
	uint32_t model = m_modelLookup ? m_modelLookup(modelName) : 0;
	if (!model)
		return;
	PROFILE_SCOPE("ui: model preview");
	float width = ImGui::GetContentRegionAvail().x;
	ModelPreview::Draw(model, width, (std::min)(width * 0.6f, 220.f));
}

std::string WorkshopUI::CartLineName(const CartLine& line) const
{
	switch (line.kind)
	{
		case CartLine::Kind::PART:
		{
			std::string name = std::format("{} {}", line.order.condition == PartCondition::USED ? "Used" : "New",
				PartCatalog::StoreName(line.order.type));
			if (Workshop::SizeOptions(line.order.type).size() > 1)
				name += " " + SizeClassLabel(line.order.sizeOffset);
			return name;
		}
		case CartLine::Kind::TOOL:			return ToolCatalog::Of(line.index).name;
		case CartLine::Kind::MACHINE:		return MachineCatalog::All()[line.index].name;
		case CartLine::Kind::ENGINE_STAND:	return "Engine stand";
		case CartLine::Kind::FURNITURE:		return FurnitureCatalog::All()[line.index].name;
		case CartLine::Kind::DYNO:			return "Dyno";
		case CartLine::Kind::HOIST:			return "Engine hoist";
		case CartLine::Kind::FLUID:			return line.index < static_cast<size_t>(FLUID_PRODUCTS) ? std::format("{} ({:g} l)",
			FluidProductName(static_cast<FluidProduct>(line.index)), Workshop::FluidUnitLitres(static_cast<FluidProduct>(line.index))) : "";
	}
	return "";
}

int WorkshopUI::InCart(CartLine::Kind kind, size_t index) const
{
	int count = 0;
	for (const auto& line : m_cart)
		if (line.kind == kind && (kind == CartLine::Kind::ENGINE_STAND || line.index == index))
			count += kind == CartLine::Kind::PART ? line.order.quantity : line.quantity;
	return count;
}

void WorkshopUI::AddToCart(const CartLine& line)
{
	for (auto& existing : m_cart)
	{
		if (existing.kind != line.kind)
			continue;
		if (line.kind == CartLine::Kind::PART && existing.order.type == line.order.type
			&& existing.order.condition == line.order.condition && existing.order.sizeOffset == line.order.sizeOffset)
		{
			existing.order.quantity = (std::min)(existing.order.quantity + line.order.quantity, 99);
			return;
		}
		if (line.kind == CartLine::Kind::ENGINE_STAND || ((line.kind == CartLine::Kind::FURNITURE || line.kind == CartLine::Kind::TOOL
			|| line.kind == CartLine::Kind::FLUID) && existing.index == line.index))
		{
			existing.quantity += line.quantity;
			return;
		}
	}
	m_cart.push_back(line);
}

void WorkshopUI::AddToCartButton(Workshop& workshop, const CartLine& line, const char* label)
{
	// One of a kind (a machine, the dyno) can only be in the cart once; tools and stands only up to the
	// room left.
	std::optional<std::string> problem;
	if ((line.kind == CartLine::Kind::MACHINE || line.kind == CartLine::Kind::DYNO || line.kind == CartLine::Kind::HOIST)
		&& InCart(line.kind, line.index) > 0)
		problem = "In the cart.";
	else
	{
		CartLine withMore = line;
		if (line.kind == CartLine::Kind::TOOL)
			withMore.quantity += InCart(line.kind, line.index);
		auto withIt = m_cart;
		withIt.push_back(withMore);
		problem = workshop.CartLineProblem(withMore, withIt);
	}

	ImGui::BeginDisabled(problem.has_value());
	if (ImGui::Button(label, { 280.f, 34.f }))
	{
		AddToCart(line);
		m_cartMessage.clear();
	}
	ImGui::EndDisabled();
	if (problem)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("%s", problem->c_str());
	}
}

void WorkshopUI::DrawCart(Workshop& workshop)
{
	ImGui::SeparatorText(std::format("Cart ({})", m_cart.size()).c_str());
	if (m_cart.empty())
	{
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("Empty. Add parts, tools, machines and engine stands from the catalogue, then check out once: "
			"the order arrives together, with the parts boxed by system and each machine in its own crate.");
		ImGui::PopTextWrapPos();
	}

	std::optional<size_t> remove;
	for (size_t i = 0; i < m_cart.size(); i++)
	{
		auto& line = m_cart[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::TextUnformatted(CartLineName(line).c_str());
		ImGui::TextColored(COLOR_GOOD, "$%.0f", workshop.LineTotal(line));

		// Parts, stands, furniture and tools come in quantities; machines are one each.
		int* quantity = line.kind == CartLine::Kind::PART ? &line.order.quantity
			: line.kind == CartLine::Kind::ENGINE_STAND || line.kind == CartLine::Kind::FURNITURE || line.kind == CartLine::Kind::TOOL
				|| line.kind == CartLine::Kind::FLUID ? &line.quantity : nullptr;
		if (quantity)
		{
			ImGui::SameLine(110.f);
			if (ImGui::SmallButton("-") && *quantity > 1)
				(*quantity)--;
			ImGui::SameLine();
			ImGui::Text("%d", *quantity);
			ImGui::SameLine();
			if (ImGui::SmallButton("+") && *quantity < 99)
				(*quantity)++;
		}
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.f);
		if (ImGui::SmallButton("Remove"))
			remove = i;
		if (auto problem = workshop.CartLineProblem(line, m_cart))
			ImGui::TextColored(COLOR_BAD, "%s", problem->c_str());
		ImGui::Separator();
		ImGui::PopID();
	}
	if (remove)
		m_cart.erase(m_cart.begin() + *remove);

	// The store's prices are before sales tax; it's added here, at the cart.
	double subtotal = workshop.CartTotal(m_cart);
	double tax = workshop.CartTax(m_cart);
	double total = subtotal + tax;
	ImGui::Spacing();
	ImGui::Text("Subtotal");
	ImGui::SameLine(110.f);
	ImGui::Text("$%.2f", subtotal);
	ImGui::Text("Sales tax");
	ImGui::SameLine(110.f);
	ImGui::TextDisabled("$%.2f  (%.0f%%)", tax, Workshop::SALES_TAX * 100.0);
	ImGui::Text("Total");
	ImGui::SameLine(110.f);
	ImGui::TextColored(total > workshop.GetMoney() ? COLOR_BAD : COLOR_GOOD, "$%.2f", total);
	ImGui::Text("After");
	ImGui::SameLine(110.f);
	ImGui::TextDisabled("$%.2f", workshop.GetMoney() - total);

	bool problems = std::any_of(m_cart.begin(), m_cart.end(), [&](const CartLine& line) { return workshop.CartLineProblem(line, m_cart).has_value(); });
	ImGui::Spacing();
	ImGui::BeginDisabled(m_cart.empty() || total > workshop.GetMoney() || problems);
	if (ImGui::Button("Check out", { -1.f, 36.f }))
	{
		if (workshop.Checkout(m_cart))
		{
			m_cartMessage = std::format("Ordered {} line{} for ${:.2f}, tax included. It's on its way to the roll-up door.", m_cart.size(), m_cart.size() == 1 ? "" : "s", total);
			m_cart.clear();
		}
	}
	ImGui::EndDisabled();
	if (!m_cart.empty() && total > workshop.GetMoney())
		ImGui::TextColored(COLOR_BAD, "Not enough money for all of it.");
	ImGui::BeginDisabled(m_cart.empty());
	if (ImGui::Button("Empty the cart", { -1.f, 0.f }))
		m_cart.clear();
	ImGui::EndDisabled();
	if (!m_cartMessage.empty())
	{
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextColored(COLOR_GOOD, "%s", m_cartMessage.c_str());
		ImGui::PopTextWrapPos();
	}
}

void WorkshopUI::DrawEnginePicker(Workshop& workshop)
{
	int engines = workshop.GetEngineCount();
	if (engines <= 1)
		return;

	// Switching engines mid-pull would swap the sheet out from under the run.
	ImGui::BeginDisabled(m_pullActive);
	int active = workshop.GetActiveEngine();
	ImGui::TextUnformatted("Engine:");
	for (int engine = 0; engine < engines; engine++)
	{
		ImGui::SameLine();
		if (ImGui::RadioButton(std::format("{}##Engine{}", engine + 1, engine).c_str(), active == engine))
			workshop.SetActiveEngine(engine);
	}
	ImGui::EndDisabled();
	ImGui::SameLine(0.f, 20.f);
	ImGui::TextDisabled("(on engine stand %d; E at a stand also picks its engine)", workshop.GetActiveEngine() + 1);
	ImGui::Separator();
}

bool WorkshopUI::StoreCard(const char* id, const char* name, const std::string& price, bool selected, bool owned,
	const std::function<void(ImDrawList*, ImVec2, float)>& drawIcon, const std::vector<float>& ratings)
{
	constexpr float CARD_WIDTH = 132.f;
	constexpr float CARD_HEIGHT = 132.f;
	constexpr float ICON_SIZE = 62.f;

	ImGui::PushID(id);
	ImVec2 cardMin = ImGui::GetCursorScreenPos();
	bool clicked = ImGui::InvisibleButton("card", { CARD_WIDTH, CARD_HEIGHT });
	bool hovered = ImGui::IsItemHovered();
	ImVec2 cardMax = { cardMin.x + CARD_WIDTH, cardMin.y + CARD_HEIGHT };
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	ProductCard(drawList, cardMin, cardMax, cardMin.y + ICON_SIZE + 10.f, selected, hovered);

	drawIcon(drawList, { cardMin.x + (CARD_WIDTH - ICON_SIZE) * 0.5f, cardMin.y + 8.f }, ICON_SIZE);

	// Long names are clipped to the card rather than spilling into the next one.
	ImVec2 textSize = ImGui::CalcTextSize(name);
	float textX = cardMin.x + (std::max)(4.f, (CARD_WIDTH - textSize.x) * 0.5f);
	drawList->PushClipRect(cardMin, cardMax, true);
	drawList->AddText({ textX, cardMin.y + ICON_SIZE + 16.f }, Site().cardInk, name);
	drawList->PopClipRect();

	std::string footer = owned ? "Owned" : price;
	ImVec2 footerSize = ImGui::CalcTextSize(footer.c_str());
	drawList->AddText({ cardMin.x + (CARD_WIDTH - footerSize.x) * 0.5f, cardMin.y + ICON_SIZE + 34.f },
		owned ? Site().cardMuted : Site().cardPrice, footer.c_str());

	// Efficiency at a glance: one thin bar per rating along the bottom of the card.
	for (size_t i = 0; i < ratings.size(); i++)
		Ratings::MiniBar(drawList, { cardMin.x + 10.f, cardMax.y - 16.f + i * 6.f - (ratings.size() - 1) * 6.f }, CARD_WIDTH - 20.f, ratings[i]);

	if (hovered && textSize.x > CARD_WIDTH - 8.f)
		ImGui::SetTooltip("%s", name);

	ImGui::PopID();

	// Flow the cards into rows.
	float nextX = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + CARD_WIDTH;
	if (nextX < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
		ImGui::SameLine();
	return clicked;
}

std::string WorkshopUI::ToolPrecision(const MeasuringTool& tool, double value) const
{
	if (tool.measuresFinish)
		return Units::FormatFinish(value);
	if (tool.kinds.size() == 1 && tool.kinds[0] == FeatureKind::VOLUME)
		return Units::FormatVolume(value);
	if (tool.kinds.size() == 1 && tool.kinds[0] == FeatureKind::FORCE)
		return Units::FormatForce(value);
	return Deviation(value);
}

std::string WorkshopUI::ToolHint(Workshop& workshop, int toolIndex) const
{
	(void)workshop;
	if (toolIndex < 0)
		return "no tool owned";
	return ToolCatalog::Of(toolIndex).name;
}

void WorkshopUI::DrawToolCell(Workshop& workshop, const std::function<bool(const MeasuringTool&)>& canDo, const char* prefix) const
{
	const auto& tools = ToolCatalog::All();
	std::vector<size_t> able;
	for (size_t i = 0; i < tools.size(); i++)
		if (canDo(tools[i]))
			able.push_back(i);
	if (able.empty())
	{
		ImGui::TextDisabled("%s-", prefix);
		return;
	}
	// The ones you own first, most precise first (the one measuring uses), then the rest cheapest first.
	std::stable_sort(able.begin(), able.end(), [&](size_t a, size_t b) {
		bool ownsA = workshop.OwnsTool(a);
		if (ownsA != workshop.OwnsTool(b))
			return ownsA;
		return ownsA ? tools[a].sigma < tools[b].sigma : tools[a].price < tools[b].price;
	});

	const auto& first = tools[able.front()];
	if (workshop.OwnsTool(able.front()))
		ImGui::Text("%s%s", prefix, first.name.c_str());
	else
		ImGui::TextColored(COLOR_WARN, "%s%s (not owned)", prefix, first.name.c_str());
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::TextUnformatted("Can be measured with:");
		for (size_t i : able)
		{
			if (workshop.OwnsTool(i))
				ImGui::TextColored(COLOR_GOOD, "  %s (owned)", tools[i].name.c_str());
			else
				ImGui::TextDisabled("  %s ($%.0f at the parts computer)", tools[i].name.c_str(), tools[i].price);
		}
		ImGui::TextDisabled("Hold the tool and aim at it in the shop.");
		ImGui::EndTooltip();
	}
}

void WorkshopUI::DrawToolStore(Workshop& workshop, bool paint)
{
	const auto& tools = ToolCatalog::All();
	m_selectedTool = std::clamp(m_selectedTool, 0, static_cast<int>(tools.size()) - 1);
	// The paint has a tab of its own: keep the selection on this tab's side.
	if (tools[m_selectedTool].Paints() != paint || tools[m_selectedTool].partOf >= 0)
		for (size_t i = 0; i < tools.size(); i++)
			if (tools[i].Paints() == paint && tools[i].partOf < 0)
			{
				m_selectedTool = static_cast<int>(i);
				break;
			}

	if (ImGui::BeginChild("ToolCatalogue", { ImGui::GetContentRegionAvail().x * 0.55f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::TextDisabled("Tools are delivered to the roll-up door. Once unpacked, put them away on the tool board or in your backpack.");
		ImGui::Spacing();

		// Grouped by the job they do, cheapest first within each: the measuring tools by what they
		// measure, then the spanners.
		static const char* const SHELVES[] = {
			"Outside sizes: journals, stems, lengths",
			"Bores",
			"Flatness and runout",
			"On the engine: clearances, gaps, heights",
			"Springs, volumes and surface finish",
			"Sockets and drives",
			"Wrenches",
			"Torque",
			"Pistons and rods",
			"Cordless: tools, batteries and chargers",
			"Manuals",
			"Paint markers",
			"Spray paint",
			"Cleaning up",
		};
		auto shelfOf = [](const MeasuringTool& tool) {
			if (tool.manual)
				return 10;
			if (tool.Paints())
				return tool.handKind == HandToolKind::MARKER ? 11 : tool.handKind == HandToolKind::SPRAY_CAN ? 12 : 13;
			if (tool.powered || tool.handKind == HandToolKind::BATTERY || tool.handKind == HandToolKind::CHARGER)
				return 9;
			if (tool.handKind == HandToolKind::PIN_PRESS || tool.handKind == HandToolKind::RING_COMPRESSOR || tool.handKind == HandToolKind::VALVE_SPRING_COMPRESSOR)
				return 8;
			auto measures = [&](FeatureKind kind) { return std::find(tool.kinds.begin(), tool.kinds.end(), kind) != tool.kinds.end(); };
			switch (tool.handKind)
			{
				case HandToolKind::SOCKETS:
				case HandToolKind::RATCHET:
				case HandToolKind::BREAKER_BAR:		return 5;
				case HandToolKind::WRENCHES:		return 6;
				case HandToolKind::TORQUE_WRENCH:	return 7;
				default:							break;
			}
			if (!tool.assemblyChecks.empty())
				return 3;
			if (tool.measuresFinish || measures(FeatureKind::FORCE) || measures(FeatureKind::VOLUME))
				return 4;
			if (measures(FeatureKind::BORE))
				return 1;
			if (measures(FeatureKind::FLATNESS) || measures(FeatureKind::RUNOUT) || measures(FeatureKind::SQUARENESS) || measures(FeatureKind::ALIGNMENT))
				return 2;
			return 0;
		};
		std::vector<size_t> order(tools.size());
		for (size_t i = 0; i < tools.size(); i++)
			order[i] = i;
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			int shelfA = shelfOf(tools[a]), shelfB = shelfOf(tools[b]);
			return shelfA != shelfB ? shelfA < shelfB : tools[a].price < tools[b].price;
		});

		int shelf = -1;
		for (size_t i : order)
		{
			const auto& tool = tools[i];
			// Sold only in their set.
			if (tool.partOf >= 0)
				continue;
			if (tool.Paints() != paint)
				continue;
			if (int thisShelf = shelfOf(tool); thisShelf != shelf)
			{
				if (shelf >= 0)
					ImGui::NewLine();
				ImGui::SeparatorText(SHELVES[thisShelf]);
				shelf = thisShelf;
			}
			// One the shop hasn't the rank for yet shows the rank it needs, greyed.
			bool locked = !workshop.ToolUnlocked(i);
			std::string price = !locked ? std::format("${:.0f}", tool.price * workshop.EquipmentPriceFactor()) : tool.skill >= 0 && workshop.SkillLevel(static_cast<SkillKind>(tool.skill)) == 0 ? std::string("Perk") : std::format("Rank {}", tool.rank);
			if (StoreCard(tool.name.c_str(), tool.name.c_str(), price, m_selectedTool == static_cast<int>(i), workshop.OwnsTool(i),
				[&](ImDrawList* drawList, ImVec2 min, float size) {
					ItemIcons::DrawTool(drawList, tool, min, size, locked ? IM_COL32(120, 120, 120, 160) : PartIcons::DEFAULT_COLOR); },
				tool.manual || tool.Paints() || tool.handKind == HandToolKind::BATTERY || tool.handKind == HandToolKind::CHARGER ? std::vector<float>{}
					: tool.TurnSeconds() > 0.0 ? std::vector<float>{ Ratings::ToolPrecision(tool), Ratings::ToolSpeed(tool) } : std::vector<float>{ Ratings::ToolPrecision(tool) }))
				m_selectedTool = static_cast<int>(i);
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("ToolDetail", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		const auto& tool = tools[m_selectedTool];
		ImGui::Text("%s", tool.name.c_str());
		DrawPreview(ToolModelName(tool));
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("%s", tool.description.c_str());
		ImGui::PopTextWrapPos();
		// Step by step, how it's used in the shop.
		ImGui::SeparatorText("How to use");
		ImGui::PushTextWrapPos(0.f);
		auto steps = HowToUse(tool);
		for (size_t step = 0; step < steps.size(); step++)
			ImGui::Text("%zu. %s", step + 1, steps[step].c_str());
		ImGui::PopTextWrapPos();
		ImGui::Separator();

		if (tool.manual)
		{
			ImGui::PushTextWrapPos(0.f);
			if (tool.manualBook == 1)
				ImGui::TextDisabled("Chapters: general information, tools, building the car, the body and interior, front suspension and "
					"steering, the rear axle, brakes, clutch and drivetrain, the engine in the car, fuel, electrics, cooling and exhaust, fluids "
					"and capacities, the tune-up, wheels, tires and alignment, torque specifications, and the road test and troubleshooting.");
			else
				ImGui::TextDisabled("Chapters: general information, tools, teardown, the block, crankshaft, rods, pistons and rings, bearings and "
					"Plastigauge, the head and valves, the camshaft, the assembly sequence, torque specifications, final checks and the dyno, "
					"and troubleshooting. Dimensions are shown in the units you play in.");
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			DrawToolBuy(workshop, static_cast<size_t>(m_selectedTool));
			ImGui::EndChild();
			return;
		}
		if (tool.IsHandTool())
		{
			// What it does for the engine's nuts and bolts.
			std::string sizes;
			for (int size : tool.sizes)
				sizes += (sizes.empty() ? "" : ", ") + std::to_string(size);
			if (!sizes.empty())
				ImGui::Text("Sizes: %s mm", sizes.c_str());
			if (tool.handKind == HandToolKind::TORQUE_WRENCH)
			{
				Ratings::Bar("Precision", Ratings::ToolPrecision(tool));
				ImGui::Text("Accurate to about +/- %.0f%%, up to %.0f Nm. Drives a socket.", tool.torqueError * 100.0, tool.maxTorque);
			}
			if (tool.TurnSeconds() > 0.0)
			{
				Ratings::Bar("Speed", Ratings::ToolSpeed(tool));
				ImGui::Text("About %.1f s a nut or bolt (hold the left mouse button).", tool.TurnSeconds());
			}
			ImGui::PushTextWrapPos(0.f);
			if (tool.powered)
				ImGui::Text("Turns up to %.0f Nm. Uses about %.2f Ah of a battery pack for each nut or bolt (more for the tight ones): "
					"buy a pack and a charger to go with it.", tool.maxTorque, tool.drawAh);
			else if (tool.handKind == HandToolKind::BATTERY)
				ImGui::Text("Holds %.0f Ah: about %.0f nuts and bolts with the impact wrench. Takes %.0f minutes on the 2 A charger, %.0f on the 6 A.",
					tool.capacityAh, tool.capacityAh / 0.08, tool.capacityAh / 2.0, tool.capacityAh / 6.0);
			else if (tool.handKind == HandToolKind::CHARGER)
				ImGui::Text("Charges at %.0f A: a 4 Ah pack from flat in %.0f minutes. Set it down, then hold a pack and press E on it. "
					"E on the charger takes the pack back off.", tool.chargeAmps, 4.0 / tool.chargeAmps);
			if (tool.handKind == HandToolKind::BATTERY || tool.handKind == HandToolKind::CHARGER)
				ImGui::TextDisabled("Hold a cordless tool and press E on a pack to clip it on; C takes it back off.");
			else if (tool.Paints())
				ImGui::TextDisabled("For decorating: the walls and the floor take paint from as far off as you can see them, the ring round the crosshair showing how big (the mouse wheel changes it), and a spray can "
					"or the thinner works on any part or tool too. It never wears off, but thinner takes it back off.");
			else if (tool.handKind != HandToolKind::PIN_PRESS && tool.handKind != HandToolKind::RING_COMPRESSOR && tool.handKind != HandToolKind::VALVE_SPRING_COMPRESSOR)
				ImGui::TextDisabled("Every nut and bolt on the engine needs a socket (driven by a ratchet, impact or torque wrench) or a wrench of "
					"its size, and a torque wrench to be done up to spec. How close the torque wrench gets "
					"is how tight they really are, and the dyno finds out.");
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			DrawToolBuy(workshop, static_cast<size_t>(m_selectedTool));
			ImGui::EndChild();
			return;
		}

		Ratings::Bar("Precision", Ratings::ToolPrecision(tool));
		ImGui::Spacing();
		ImGui::Text("Reads to   %s", ToolPrecision(tool, tool.resolution).c_str());
		ImGui::Text("Repeats to +/- %s", ToolPrecision(tool, tool.sigma * 2.0).c_str());
		if (tool.minSize > 0.0 || tool.maxSize > 0.0)
			ImGui::Text("Fits bores %s", tool.maxSize > 0.0 ? std::format("up to {:.0f} mm", tool.maxSize).c_str() : std::format("from {:.0f} mm", tool.minSize).c_str());

		std::string uses;
		for (auto kind : tool.kinds)
		{
			uses += (uses.empty() ? "" : ", ") + std::string(FeatureKindUse(kind));
		}
		if (tool.measuresFinish)
			uses += (uses.empty() ? "" : ", ") + std::string("surface finish");
		for (auto check : tool.assemblyChecks)
		{
			uses += (uses.empty() ? "" : ", ") + std::string(AssemblyCheckUse(check));
		}
		ImGui::PushTextWrapPos(0.f);
		ImGui::Text("Measures   %s", uses.c_str());
		ImGui::PopTextWrapPos();

		ImGui::Spacing();
		DrawToolBuy(workshop, static_cast<size_t>(m_selectedTool));
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawMachineStore(Workshop& workshop)
{
	const auto& machines = MachineCatalog::All();
	// The dyno and the hoist have cards of their own, shown first and numbered after the machines.
	const int DYNO = static_cast<int>(machines.size());
	const int HOIST = DYNO + 1;
	m_selectedStoreMachine = std::clamp(m_selectedStoreMachine, 0, HOIST);

	if (ImGui::BeginChild("MachineCatalogue", { ImGui::GetContentRegionAvail().x * 0.55f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::TextDisabled("Machines are delivered in a crate and set up when it's opened. Press F on one to move it.");
		ImGui::Spacing();
		ImGui::SeparatorText("Testing");
		if (StoreCard("Dyno", "Dyno", std::format("${:.0f}", Workshop::DynoPrice()), m_selectedStoreMachine == DYNO, workshop.OwnsDyno(),
			[&](ImDrawList* drawList, ImVec2 min, float size) { ItemIcons::DrawDyno(drawList, min, size); }, {}))
			m_selectedStoreMachine = DYNO;
		if (ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x + 1.f)
			ImGui::NewLine();
		ImGui::SeparatorText("Lifting");
		if (StoreCard("Engine hoist", "Engine hoist", std::format("${:.0f}", Workshop::HoistPrice()), m_selectedStoreMachine == HOIST, workshop.OwnsHoist(),
			[&](ImDrawList* drawList, ImVec2 min, float size) { ItemIcons::DrawHoist(drawList, min, size); }, {}))
			m_selectedStoreMachine = HOIST;
		// Grouped by the job they do, cheapest first within each, so the budget way and the proper
		// way to do a job sit side by side.
		auto group = [](MachineOperation operation) {
			switch (operation)
			{
				case MachineOperation::BORE:
				case MachineOperation::HONE:		return 0;
				case MachineOperation::SURFACE:		return 1;
				case MachineOperation::LINE_BORE:	return 2;
				case MachineOperation::GRIND:
				case MachineOperation::POLISH:		return 3;
				case MachineOperation::REAM:
				case MachineOperation::CUT_SEAT:
				case MachineOperation::FACE_VALVE:	return 4;
				case MachineOperation::FILE_RING:	return 5;
			}
			return 6;
		};
		constexpr const char* GROUP_NAMES[] = { "Cylinder bores", "Block and head decks", "Main bearing housings", "Crankshaft",
			"Cylinder head and valves", "Piston rings", "Other" };
		std::vector<size_t> order(machines.size());
		for (size_t i = 0; i < order.size(); i++)
			order[i] = i;
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			int groupA = group(machines[a].operation), groupB = group(machines[b].operation);
			return groupA != groupB ? groupA < groupB : machines[a].price < machines[b].price;
		});

		int section = -1;
		for (size_t i : order)
		{
			const auto& machine = machines[i];
			if (group(machine.operation) != section)
			{
				// The last card may have left the cursor on its row.
				if (ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x + 1.f)
					ImGui::NewLine();
				section = group(machine.operation);
				ImGui::SeparatorText(GROUP_NAMES[section]);
			}
			if (StoreCard(machine.name.c_str(), machine.name.c_str(), std::format("${:.0f}", machine.price * workshop.EquipmentPriceFactor()), m_selectedStoreMachine == static_cast<int>(i),
				workshop.OwnsMachine(i), [&](ImDrawList* drawList, ImVec2 min, float size) { ItemIcons::DrawMachine(drawList, machine, min, size); },
				{ Ratings::MachineAccuracy(machine) }))
				m_selectedStoreMachine = static_cast<int>(i);
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("MachineDetail", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		if (m_selectedStoreMachine == HOIST)
		{
			ImGui::Text("Engine hoist");
			DrawPreview(ShopModels::ENGINE_HOIST);
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("A shop crane on casters. Push it up to an engine stand or the dyno and press E to lift the engine off; push it "
				"anywhere with the engine on its hook, and press E by a free stand or the dyno to lower it on. Nothing can be fitted to or "
				"taken off an engine while it hangs.");
			ImGui::PopTextWrapPos();
			ImGui::Separator();
			if (workshop.OwnsHoist())
				ImGui::TextColored(COLOR_GOOD, "Owned. If it isn't set up yet, its crate is waiting by the roll-up door.");
			else
			{
				CartLine line;
				line.kind = CartLine::Kind::HOIST;
				AddToCartButton(workshop, line, std::format("Add to cart (${:.0f})", Workshop::HoistPrice()).c_str());
			}
			ImGui::EndChild();
			return;
		}
		if (m_selectedStoreMachine == DYNO)
		{
			ImGui::Text("Dyno");
			DrawPreview(ShopModels::DYNO);
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("An engine dynamometer: bolt a finished engine to it and run it to redline, to see its power and torque, oil "
				"pressure, blow-by and everything wrong with it before a buyer does. Each pull costs $%.0f in fuel. Without one, engines are sold "
				"untested: the buyer runs it, and you find out what it made when they pay.", workshop.DynoCost());
			ImGui::PopTextWrapPos();
			ImGui::Separator();
			if (workshop.OwnsDyno())
				ImGui::TextColored(COLOR_GOOD, "Owned. If it isn't set up yet, its crate is waiting by the roll-up door.");
			else
			{
				CartLine line;
				line.kind = CartLine::Kind::DYNO;
				AddToCartButton(workshop, line, std::format("Add to cart (${:.0f})", Workshop::DynoPrice()).c_str());
			}
			ImGui::EndChild();
			return;
		}
		const auto& machine = machines[m_selectedStoreMachine];
		ImGui::Text("%s", machine.name.c_str());
		DrawPreview(ShopAssets::FixtureFor(machine).model);
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("%s", machine.description.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Separator();

		Ratings::Bar("Accuracy", Ratings::MachineAccuracy(machine));
		if (float finish = Ratings::MachineFinish(machine); finish >= 0.f)
			Ratings::Bar("Finish", finish);
		ImGui::Spacing();
		ImGui::Text("Operation   %s", Machining::OperationName(machine.operation));
		ImGui::Text("Accuracy    +/- %s", Deviation(machine.sigma * 2.0).c_str());
		if (machine.finishRa > 0.0)
			ImGui::Text("Finish      %s", Units::FormatFinish(machine.finishRa).c_str());
		if (machine.maxRemoval > 0.0)
			ImGui::Text("Max per cut %s", Deviation(machine.maxRemoval).c_str());
		else
			ImGui::Text("Max per cut no limit");

		ImGui::Spacing();
		if (workshop.OwnsMachine(m_selectedStoreMachine))
			ImGui::TextColored(COLOR_GOOD, "Owned. If it isn't set up yet, its crate is waiting by the roll-up door.");
		else
		{
			CartLine line;
			line.kind = CartLine::Kind::MACHINE;
			line.index = static_cast<size_t>(m_selectedStoreMachine);
			AddToCartButton(workshop, line, std::format("Add to cart (${:.0f})", machine.price * workshop.EquipmentPriceFactor()).c_str());
		}
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawFurnitureStore(Workshop& workshop)
{
	const auto& furniture = FurnitureCatalog::All();
	// After the furniture, one more card: an engine stand, for building another engine alongside.
	const int STAND = static_cast<int>(furniture.size());
	m_selectedStoreFurniture = std::clamp(m_selectedStoreFurniture, 0, STAND);
	auto drawStand = [](ImDrawList* drawList, ImVec2 at, float s) {
		ImU32 color = IM_COL32(220, 90, 70, 255);
		drawList->AddLine({ at.x + s * 0.3f, at.y + s * 0.9f }, { at.x + s * 0.3f, at.y + s * 0.25f }, color, 2.5f);
		drawList->AddLine({ at.x + s * 0.3f, at.y + s * 0.3f }, { at.x + s * 0.75f, at.y + s * 0.3f }, color, 2.5f);
		drawList->AddRectFilled({ at.x + s * 0.72f, at.y + s * 0.12f }, { at.x + s * 0.82f, at.y + s * 0.5f }, color);
		drawList->AddLine({ at.x + s * 0.1f, at.y + s * 0.9f }, { at.x + s * 0.9f, at.y + s * 0.9f }, color, 2.5f);
	};

	if (ImGui::BeginChild("FurnitureCatalogue", { ImGui::GetContentRegionAvail().x * 0.55f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::TextDisabled("Delivered in a crate and set up when it's opened. Press F on a piece to move it.");
		ImGui::Spacing();
		for (size_t i = 0; i < furniture.size(); i++)
		{
			const auto& piece = furniture[i];
			if (StoreCard(piece.name.c_str(), piece.name.c_str(), std::format("${:.0f}", piece.price), m_selectedStoreFurniture == static_cast<int>(i),
				false, [&](ImDrawList* drawList, ImVec2 min, float size) { ItemIcons::DrawFurniture(drawList, piece, min, size); }, {}))
				m_selectedStoreFurniture = static_cast<int>(i);
		}
		if (StoreCard("Engine stand", "Engine stand", std::format("${:.0f}", Workshop::ENGINE_STAND_PRICE), m_selectedStoreFurniture == STAND,
			false, drawStand, {}))
			m_selectedStoreFurniture = STAND;
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("FurnitureDetail", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		if (m_selectedStoreFurniture == STAND)
		{
			int engines = workshop.GetEngineCount();
			ImGui::Text("Engine stand");
			DrawPreview(ShopModels::ENGINE_STAND);
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("Holds one engine while it's built, checked and run on the dyno: one more stand, one more engine on the go at once. Set up when its crate is opened; press F on it to move it.");
			ImGui::PopTextWrapPos();
			ImGui::Separator();
			ImGui::TextDisabled("You have %d of %d.", engines, Workshop::MAX_ENGINES);
			CartLine stand;
			stand.kind = CartLine::Kind::ENGINE_STAND;
			AddToCartButton(workshop, stand, std::format("Add to cart (${:.0f})", Workshop::ENGINE_STAND_PRICE).c_str());
			if (engines >= Workshop::MAX_ENGINES)
				ImGui::TextDisabled("There's no room for more.");
			ImGui::EndChild();
			return;
		}
		const auto& piece = furniture[m_selectedStoreFurniture];
		ImGui::Text("%s", piece.name.c_str());
		DrawPreview(ShopAssets::FurnitureModel(piece));
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("%s", piece.description.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Separator();
		int owned = static_cast<int>(std::count(workshop.GetFurniture().begin(), workshop.GetFurniture().end(), static_cast<size_t>(m_selectedStoreFurniture)));
		if (owned > 0)
			ImGui::TextDisabled("You have %d.", owned);
		CartLine line;
		line.kind = CartLine::Kind::FURNITURE;
		line.index = static_cast<size_t>(m_selectedStoreFurniture);
		AddToCartButton(workshop, line, std::format("Add to cart (${:.0f})", piece.price).c_str());
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawSellButton(Workshop& workshop)
{
	// No price on the button: nobody knows what it's worth until the buyer has run it.
	if (ImGui::Button("Sell the engine"))
		workshop.SellEngine();
	ImGui::SameLine();
	ImGui::TextDisabled("The buyer runs it on their dyno, then pays for what it makes and how healthy it is.");
}

void WorkshopUI::DrawLastSale(const Workshop& workshop)
{
	const auto& sale = workshop.GetLastSale();
	if (!sale.sold)
		return;
	const auto& pull = sale.pull;
	ImGui::SeparatorText(std::format("Engine {}, sold", sale.engine + 1).c_str());
	if (pull.failed)
	{
		ImGui::TextColored(COLOR_BAD, "It let go on the buyer's dyno at %.0f rpm. They took it as a core, for $%.0f.", pull.failureRpm, sale.price);
	}
	else
	{
		ImGui::TextColored(COLOR_GOOD, "Sold for $%.0f (+%.0f reputation).", sale.price, sale.reputation);
		ImGui::Text("%.0f hp at %.0f rpm, %.0f Nm at %.0f rpm, %.0f%% healthy.", pull.peakPowerKw * 1.341, pull.peakPowerRpm,
			pull.peakTorqueNm, pull.peakTorqueRpm, pull.healthScore);
		ImGui::TextDisabled("Oil pressure %.1f bar, blow-by %.1f%%, compression %.2f:1.", pull.oilPressureBar, pull.blowbyPercent, pull.compressionRatio);
	}
	// What the buyer's dyno sheet found.
	ImGui::PushTextWrapPos(0.f);
	for (const auto& finding : pull.findings)
	{
		ImVec4 color = finding.severity == FindingSeverity::FAILURE ? COLOR_BAD : finding.severity == FindingSeverity::WARNING ? COLOR_WARN : COLOR_DIM;
		ImGui::TextColored(color, "%s", finding.message.c_str());
	}
	ImGui::PopTextWrapPos();
}

void WorkshopUI::DrawAssemblyTab(Workshop& workshop)
{
	// Read-only, like the workbench: parts are fitted and taken off at the stand itself, by hand.
	const auto& assembly = workshop.GetAssembly();
	ImGui::Text("Engine %d", workshop.GetActiveEngine() + 1);
	ImGui::SameLine(0.f, 20.f);
	auto missing = assembly.MissingParts();
	if (missing.empty())
		ImGui::TextColored(COLOR_GOOD, workshop.OwnsDyno() ? "Long block complete. Check your clearances, then take it to the dyno, or sell it."
			: "Long block complete. Check your clearances, then sell it: the buyer runs it.");
	else
		ImGui::TextDisabled("%zu positions still empty. Carry a part to the stand and press E to fit it.", missing.size());

	// What can go on next, in build order.
	std::vector<std::string> ready;
	for (size_t slotIndex = 0; slotIndex < static_cast<size_t>(AssemblySlot::COUNT); slotIndex++)
	{
		auto slot = static_cast<AssemblySlot>(slotIndex);
		// The valvetrain goes in the head off the engine, not onto the engine.
		if (EngineAssembly::InHead(slot))
			continue;
		int count = 0;
		for (int index = 0; index < assembly.SlotCount(slot); index++)
			if (!assembly.Get(slot, index) && !assembly.MissingPrerequisite(slot, index))
				count++;
		if (count > 0)
			ready.push_back(count > 1 ? std::format("{} x{}", AssemblySlotName(slot), count) : AssemblySlotName(slot));
	}
	if (!ready.empty())
	{
		std::string list;
		for (const auto& entry : ready)
			list += (list.empty() ? "" : ", ") + entry;
		ImGui::TextColored(COLOR_WARN, "Next to fit: %s", list.c_str());
	}
	// A finished engine sells straight off its stand, untested; the last one sold from it, what it fetched.
	if (missing.empty() && workshop.GetActiveEngine() != workshop.GetEngineOnDyno())
		DrawSellButton(workshop);
	if (workshop.GetLastSale().sold && workshop.GetLastSale().engine == workshop.GetActiveEngine() && assembly.GetAllParts().empty())
		DrawLastSale(workshop);
	ImGui::Separator();

	// What's fitted down the side, the engine's checks beside it. Parts are looked at on the bench.
	if (ImGui::BeginChild("Fitted", { 300.f, 0.f }, ImGuiChildFlags_Borders))
		DrawAssemblySlots(workshop);
	ImGui::EndChild();

	ImGui::SameLine();

	if (ImGui::BeginChild("Checks", { 0.f, 0.f }, ImGuiChildFlags_Borders))
		DrawAssemblyChecks(workshop);
	ImGui::EndChild();
}

void WorkshopUI::DrawAssemblySlots(Workshop& workshop)
{
	const auto& assembly = workshop.GetAssembly();

	bool any = false;
	for (size_t slotIndex = 0; slotIndex < static_cast<size_t>(AssemblySlot::COUNT); slotIndex++)
	{
		auto slot = static_cast<AssemblySlot>(slotIndex);
		bool header = false;
		for (int index = 0; index < assembly.SlotCount(slot); index++)
		{
			auto part = assembly.Get(slot, index);
			if (!part)
				continue;
			if (!header)
			{
				ImGui::SeparatorText(AssemblySlotName(slot));
				header = true;
			}
			any = true;

			ImGui::PushID(static_cast<int>(part->GetId()));
			PartIcons::Icon(part->GetType(), ImGui::GetTextLineHeight() * 1.3f);
			ImGui::SameLine();
			std::string position = assembly.SlotCount(slot) > 1 ? assembly.PositionName(slot, index) + ": " : "";
			ImGui::Text("%s#%u %s", position.c_str(), part->GetId(), part->GetName().c_str());
			ImGui::PopID();
		}
	}

	if (!any)
	{
		ImGui::Spacing();
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("Nothing fitted yet. Start with the block: carry it to the stand and press E.");
		ImGui::PopTextWrapPos();
	}
}

void WorkshopUI::DrawAssemblyChecks(Workshop& workshop)
{
	const auto& spec = workshop.GetSpec();
	auto& assembly = workshop.GetAssembly();
	ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

	// One row of a clearance table: the player's reading against the target, a button to take
	// it, and the true value in X-ray mode.
	auto checkRow = [&](const std::string& label, AssemblyCheck check, int index, bool rod, const Range& target, std::optional<double> truth)
	{
		// Installed height and belt deflection are sizes; everything else here is a small clearance.
		auto format = [&](double value) { return check == AssemblyCheck::INSTALLED_HEIGHT || check == AssemblyCheck::BELT_DEFLECTION ? Size(value) : Deviation(value); };

		ImGui::PushID(label.c_str());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(label.c_str());

		ImGui::TableSetColumnIndex(1);
		ImGui::TextDisabled("%s - %s", format(target.min).c_str(), format(target.max).c_str());

		ImGui::TableSetColumnIndex(2);
		if (auto reading = workshop.GetAssemblyReading(check, index, rod))
		{
			if (reading->stale)
				ImGui::TextColored(COLOR_DIM, "%s (stale)", format(reading->value).c_str());
			else
				ColoredRangeText(reading->value, target, format(reading->value));
		}
		else
			ImGui::TextDisabled("-");

		ImGui::TableSetColumnIndex(3);
		if (truth)
			DrawToolCell(workshop, [&](const MeasuringTool& tool) { return tool.CanCheck(check); });
		else
			ImGui::TextDisabled("fit parts first");

		if (m_xray)
		{
			ImGui::TableSetColumnIndex(4);
			if (truth)
				ImGui::TextColored(COLOR_XRAY, "%s", format(*truth).c_str());
		}
		ImGui::PopID();
	};

	int columns = m_xray ? 5 : 4;
	auto beginTable = [&](const char* id)
	{
		if (!ImGui::BeginTable(id, columns, flags))
			return false;
		ImGui::TableSetupColumn("Check");
		ImGui::TableSetupColumn("Target");
		ImGui::TableSetupColumn("Reading");
		ImGui::TableSetupColumn("Measure with", ImGuiTableColumnFlags_WidthFixed, 230.f);
		if (m_xray)
			ImGui::TableSetupColumn("True");
		HeadersRow({
			"What's being checked, and where on the engine.",
			"The range the engine spec calls for.",
			"Your last reading on the assembled engine. Stale once something it depends on has changed.",
			"The tool for the check. Hold it and aim at the spot on the engine.",
			XRAY_TIP });
		return true;
	};

	ImGui::SeparatorText("Bearing oil clearance (Plastigauge)");
	if (beginTable("Bearings"))
	{
		for (int journal = 0; journal < spec.mainJournals; journal++)
			checkRow(std::format("Main {}", journal + 1), AssemblyCheck::OIL_CLEARANCE, journal, false, spec.mainClearance, assembly.MainClearance(journal));
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			checkRow(std::format("Rod {}", cylinder + 1), AssemblyCheck::OIL_CLEARANCE, cylinder, true, spec.rodClearance, assembly.RodClearance(cylinder));
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Top ring end gap (feelers)");
	if (beginTable("Rings"))
	{
		// Gapped on its own: a ring set in the bore with no piston, pushed down square with one.
		auto rings = workshop.RingsInBore(workshop.GetActiveEngine());
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			checkRow(std::format("Cylinder {}", cylinder + 1), AssemblyCheck::RING_GAP, cylinder, false, spec.ringGap,
				rings.contains(cylinder) && rings.at(cylinder) ? assembly.RingGap(cylinder) : std::nullopt);
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Rod side clearance (feelers)");
	if (beginTable("RodSide"))
	{
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			checkRow(std::format("Rod {}", cylinder + 1), AssemblyCheck::ROD_SIDE_CLEARANCE, cylinder, true, spec.rodSideClearance,
				assembly.RodSideClearance(cylinder));
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Deck clearance (dial indicator, + = below deck)");
	if (beginTable("Deck"))
	{
		Range deckTarget = { spec.quench.min - spec.gasketThickness.nominal, spec.quench.max - spec.gasketThickness.nominal };
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			checkRow(std::format("Cylinder {}", cylinder + 1), AssemblyCheck::DECK_CLEARANCE, cylinder, false, deckTarget, assembly.DeckClearance(cylinder));
		ImGui::EndTable();
	}

	// Numbers the builder has to work out from their own measurements.
	ImGui::SeparatorText("Your calculations");
	auto block = assembly.Get(AssemblySlot::BLOCK, 0);
	auto crankshaft = assembly.Get(AssemblySlot::CRANKSHAFT, 0);
	auto head = assembly.Get(AssemblySlot::CYLINDER_HEAD, 0);
	auto gasket = assembly.Get(AssemblySlot::HEAD_GASKET, 0);
	auto gasketThickness = MeasuredSize(gasket, FeatureNames::COMPRESSED_THICKNESS);
	auto stroke = MeasuredSize(crankshaft, FeatureNames::STROKE);

	if (ImGui::BeginTable("Calculations", m_xray ? 4 : 3, flags))
	{
		ImGui::TableSetupColumn("Cylinder");
		ImGui::TableSetupColumn("Piston-to-wall");
		ImGui::TableSetupColumn("Quench / Compression");
		if (m_xray)
			ImGui::TableSetupColumn("True");
		HeadersRow({
			"Which cylinder.",
			"Bore minus piston skirt, from your own readings. Too tight scuffs when hot; too loose rocks and slaps.",
			"Quench: piston-to-head gap at top dead centre (deck clearance plus gasket). Compression ratio comes from the swept and chamber volumes.",
			XRAY_TIP });

		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
		{
			auto piston = assembly.Get(AssemblySlot::PISTON, cylinder);
			auto bore = MeasuredSize(block, FeatureNames::Bore(cylinder));
			auto skirt = MeasuredSize(piston, FeatureNames::SKIRT);
			auto chamber = MeasuredSize(head, FeatureNames::Chamber(cylinder));
			auto dish = MeasuredSize(piston, FeatureNames::DISH_VOLUME);
			auto deckReading = workshop.GetAssemblyReading(AssemblyCheck::DECK_CLEARANCE, cylinder);
			std::optional<double> deck = deckReading && !deckReading->stale ? std::optional<double>(deckReading->value) : std::nullopt;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%d", cylinder + 1);

			ImGui::TableSetColumnIndex(1);
			if (bore && skirt && piston)
				ColoredRangeText(*bore - *skirt, spec.PistonToWallFor(piston->GetMaterial()), Deviation(*bore - *skirt));
			else
				ImGui::TextDisabled("measure bore + skirt");

			ImGui::TableSetColumnIndex(2);
			if (deck && gasketThickness)
			{
				double quench = *deck + *gasketThickness;
				ColoredRangeText(quench, spec.quench, Deviation(quench));
				if (bore && stroke && chamber && dish)
					if (auto ratio = EngineAssembly::CompressionRatioFrom(*bore, *stroke, *chamber, *dish, spec.gasketBore + gasket->GetSizeOffset(), *gasketThickness, *deck))
					{
						ImGui::SameLine();
						ImGui::TextColored(spec.compressionRatio.Contains(*ratio) ? COLOR_GOOD : COLOR_BAD, " / %.2f:1", *ratio);
					}
			}
			else
				ImGui::TextDisabled("deck + gasket, then cc chamber/dish");

			if (m_xray)
			{
				ImGui::TableSetColumnIndex(3);
				auto pistonToWall = assembly.PistonToWall(cylinder);
				auto quench = assembly.Quench(cylinder);
				auto ratio = assembly.CompressionRatio(cylinder);
				ImGui::TextColored(COLOR_XRAY, "%s  %s  %.2f:1", pistonToWall ? Deviation(*pistonToWall).c_str() : "-",
					quench ? Deviation(*quench).c_str() : "-", ratio.value_or(0.0));
			}
		}
		ImGui::EndTable();
	}

	// Valvetrain
	ImGui::SeparatorText("Cam journal oil clearance (Plastigauge)");
	if (beginTable("Cam"))
	{
		for (int journal = 0; journal < spec.camJournals; journal++)
			checkRow(std::format("Cam journal {}", journal + 1), AssemblyCheck::CAM_CLEARANCE, journal, false, spec.camClearance, assembly.CamClearance(journal));
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Valve spring installed height (height mic)");
	if (beginTable("InstalledHeight"))
	{
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			for (bool exhaust : { false, true })
				checkRow(std::format("{} {}", FeatureNames::ValveSide(exhaust), cylinder + 1), AssemblyCheck::INSTALLED_HEIGHT,
					EngineAssembly::SpringIndex(cylinder, exhaust), false, spec.installedHeightRange, assembly.InstalledHeight(cylinder, exhaust));
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Belt deflection (belt tension gauge, mm)");
	if (beginTable("Belts"))
	{
		auto fanBelt = assembly.Get(AssemblySlot::DRIVE_BELT, 0);
		bool runIn = fanBelt && fanBelt->GetCondition() != PartCondition::NEW;
		checkRow("Timing belt (2 kg)", AssemblyCheck::BELT_DEFLECTION, 0, false, spec.timingBeltDeflection, assembly.BeltDeflection(false));
		checkRow(runIn ? "Fan belt (10 kg, used)" : "Fan belt (10 kg, new)", AssemblyCheck::BELT_DEFLECTION, 1, false,
			runIn ? spec.fanBeltDeflectionUsed : spec.fanBeltDeflectionNew, assembly.BeltDeflection(true));
		ImGui::EndTable();
	}

	// Stem clearance from the guide and stem readings; seat pressure from the spring tester's
	// reading, corrected for this valve's actual installed height using the spring's nominal rate.
	if (ImGui::BeginTable("ValveCalculations", m_xray ? 4 : 3, flags))
	{
		ImGui::TableSetupColumn("Valve");
		ImGui::TableSetupColumn("Stem-to-guide");
		ImGui::TableSetupColumn("Seat pressure");
		if (m_xray)
			ImGui::TableSetupColumn("True");
		HeadersRow({
			"Which valve, by cylinder.",
			"Guide bore minus valve stem, from your own readings. Too tight seizes; too loose wears and burns oil.",
			"Spring force holding the valve shut, corrected for this valve's actual installed height.",
			XRAY_TIP });

		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			for (bool exhaust : { false, true })
			{
				auto valve = assembly.Get(exhaust ? AssemblySlot::EXHAUST_VALVE : AssemblySlot::INTAKE_VALVE, cylinder);
				auto spring = assembly.Get(AssemblySlot::VALVE_SPRING, EngineAssembly::SpringIndex(cylinder, exhaust));
				auto guide = MeasuredSize(head, FeatureNames::Guide(cylinder, exhaust));
				auto stem = MeasuredSize(valve, FeatureNames::STEM);
				auto testedPressure = MeasuredSize(spring, FeatureNames::SEAT_PRESSURE);
				auto heightReading = workshop.GetAssemblyReading(AssemblyCheck::INSTALLED_HEIGHT, EngineAssembly::SpringIndex(cylinder, exhaust));

				ImGui::PushID(cylinder * 2 + (exhaust ? 1 : 0));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s %d", FeatureNames::ValveSide(exhaust).c_str(), cylinder + 1);

				ImGui::TableSetColumnIndex(1);
				if (guide && stem)
					ColoredRangeText(*guide - *stem, exhaust ? spec.exhaustStemClearance : spec.intakeStemClearance, Deviation(*guide - *stem));
				else
					ImGui::TextDisabled("measure guide + stem");

				ImGui::TableSetColumnIndex(2);
				if (testedPressure && heightReading && !heightReading->stale)
				{
					double pressure = *testedPressure - spec.springRate * (heightReading->value - spec.installedHeight);
					ColoredRangeText(pressure, spec.seatPressure, Units::FormatForce(pressure));
				}
				else
					ImGui::TextDisabled("test spring + installed height");

				if (m_xray)
				{
					ImGui::TableSetColumnIndex(3);
					auto clearance = assembly.StemClearance(cylinder, exhaust);
					auto pressure = assembly.SeatPressure(cylinder, exhaust);
					ImGui::TextColored(COLOR_XRAY, "%s  %s", clearance ? Deviation(*clearance).c_str() : "-", pressure ? Units::FormatForce(*pressure).c_str() : "-");
				}
				ImGui::PopID();
			}
		ImGui::EndTable();
	}
}
namespace
{
	// A step for axis ticks that gives about the wanted number of them: 1, 2, 2.5 or 5 times a power of ten.
	float NiceStep(float range, int ticks)
	{
		float raw = range / (std::max)(ticks, 1);
		float power = std::pow(10.f, std::floor(std::log10((std::max)(raw, 1e-6f))));
		for (float multiple : { 1.f, 2.f, 2.5f, 5.f, 10.f })
			if (raw <= multiple * power)
				return multiple * power;
		return 10.f * power;
	}

	// The pull as a dyno sheet draws it: torque (left scale) and power (right scale) against engine
	// speed, with the peaks marked, the redline, where it let go, and the needle while it runs. Only
	// the first shownPoints of the curve are drawn, so a live pull grows from left to right.
	void DrawDynoChart(const DynoResult& result, int shownPoints, float liveRpm, bool live, float redline)
	{
		const ImU32 TORQUE = IM_COL32(255, 170, 60, 255);
		const ImU32 POWER = IM_COL32(90, 170, 255, 255);
		const ImU32 GRID = IM_COL32(255, 255, 255, 28);
		const ImU32 AXIS = IM_COL32(200, 200, 200, 160);
		const ImU32 LABEL = IM_COL32(190, 190, 190, 255);
		const ImU32 REDLINE = IM_COL32(230, 70, 60, 200);
		constexpr float HP_PER_KW = 1.341f;
		constexpr float LEFT = 58.f, RIGHT = 58.f, TOP = 26.f, BOTTOM = 40.f;

		ImVec2 origin = ImGui::GetCursorScreenPos();
		ImVec2 size = { ImGui::GetContentRegionAvail().x, 300.f };
		ImGui::InvisibleButton("DynoChart", size);
		bool hovered = ImGui::IsItemHovered();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 plotMin = { origin.x + LEFT, origin.y + TOP };
		ImVec2 plotMax = { origin.x + size.x - RIGHT, origin.y + size.y - BOTTOM };
		drawList->AddRectFilled(origin, { origin.x + size.x, origin.y + size.y }, IM_COL32(18, 20, 24, 255), 4.f);

		// Scales: whole thousands of rpm across, and round numbers up both sides.
		float minRpm = std::floor(result.curve.front().rpm / 1000.f) * 1000.f;
		float maxRpm = std::ceil((std::max)(result.curve.back().rpm, redline) / 1000.f) * 1000.f;
		float maxTorque = 0.f, maxPower = 0.f;
		for (const auto& point : result.curve)
		{
			maxTorque = (std::max)(maxTorque, point.torqueNm);
			maxPower = (std::max)(maxPower, point.powerKw * HP_PER_KW);
		}
		float torqueStep = NiceStep(maxTorque * 1.15f, 5), powerStep = NiceStep(maxPower * 1.15f, 5);
		float torqueTop = std::ceil(maxTorque * 1.15f / torqueStep) * torqueStep;
		float powerTop = std::ceil(maxPower * 1.15f / powerStep) * powerStep;
		auto xOf = [&](float rpm) { return plotMin.x + (rpm - minRpm) / (maxRpm - minRpm) * (plotMax.x - plotMin.x); };
		auto yOf = [&](float value, float top) { return plotMax.y - value / top * (plotMax.y - plotMin.y); };

		// Grid and the rpm axis.
		for (float rpm = minRpm; rpm <= maxRpm + 1.f; rpm += 1000.f)
		{
			float x = xOf(rpm);
			drawList->AddLine({ x, plotMin.y }, { x, plotMax.y }, GRID);
			std::string text = std::format("{:.0f}", rpm);
			drawList->AddText({ x - ImGui::CalcTextSize(text.c_str()).x * 0.5f, plotMax.y + 4.f }, LABEL, text.c_str());
		}
		// Torque on the left, power on the right, each against its own scale.
		for (int tick = 0; tick * torqueStep <= torqueTop + 0.01f; tick++)
		{
			float y = yOf(tick * torqueStep, torqueTop);
			drawList->AddLine({ plotMin.x, y }, { plotMax.x, y }, GRID);
			std::string text = std::format("{:.0f}", tick * torqueStep);
			drawList->AddText({ plotMin.x - 6.f - ImGui::CalcTextSize(text.c_str()).x, y - 7.f }, TORQUE, text.c_str());
		}
		for (int tick = 0; tick * powerStep <= powerTop + 0.01f; tick++)
		{
			float y = yOf(tick * powerStep, powerTop);
			drawList->AddText({ plotMax.x + 6.f, y - 7.f }, POWER, std::format("{:.0f}", tick * powerStep).c_str());
		}
		drawList->AddRect(plotMin, plotMax, AXIS);

		// Axis titles and the key.
		const char* xTitle = "Engine speed (rpm)";
		drawList->AddText({ (plotMin.x + plotMax.x - ImGui::CalcTextSize(xTitle).x) * 0.5f, plotMax.y + 20.f }, LABEL, xTitle);
		drawList->AddText({ origin.x + 6.f, origin.y + 5.f }, TORQUE, "Torque (Nm)");
		const char* powerTitle = "Power (hp)";
		drawList->AddText({ origin.x + size.x - 6.f - ImGui::CalcTextSize(powerTitle).x, origin.y + 5.f }, POWER, powerTitle);

		// The redline.
		if (redline > minRpm && redline < maxRpm)
			for (float y = plotMin.y; y < plotMax.y; y += 8.f)
				drawList->AddLine({ xOf(redline), y }, { xOf(redline), (std::min)(y + 4.f, plotMax.y) }, REDLINE, 1.5f);

		// The curves, as far as the pull has got.
		std::vector<ImVec2> torqueLine, powerLine;
		for (int i = 0; i < shownPoints && i < static_cast<int>(result.curve.size()); i++)
		{
			const auto& point = result.curve[i];
			torqueLine.push_back({ xOf(point.rpm), yOf(point.torqueNm, torqueTop) });
			powerLine.push_back({ xOf(point.rpm), yOf(point.powerKw * HP_PER_KW, powerTop) });
		}
		drawList->PushClipRect(plotMin, plotMax, true);
		if (torqueLine.size() > 1)
		{
			drawList->AddPolyline(torqueLine.data(), static_cast<int>(torqueLine.size()), TORQUE, 0, 2.5f);
			drawList->AddPolyline(powerLine.data(), static_cast<int>(powerLine.size()), POWER, 0, 2.5f);
		}
		drawList->PopClipRect();

		// Peaks, once the pull has passed them, and where it broke.
		bool finished = !live || shownPoints >= static_cast<int>(result.curve.size());
		auto mark = [&](float rpm, float value, float top, ImU32 color, const std::string& text) {
			ImVec2 at = { xOf(rpm), yOf(value, top) };
			drawList->AddCircleFilled(at, 4.5f, color);
			ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
			float x = (std::min)(at.x + 6.f, plotMax.x - textSize.x - 2.f);
			drawList->AddText({ x, at.y - textSize.y - 4.f }, color, text.c_str());
		};
		if (finished)
		{
			mark(static_cast<float>(result.peakTorqueRpm), static_cast<float>(result.peakTorqueNm), torqueTop, TORQUE,
				std::format("{:.0f} Nm @ {:.0f}", result.peakTorqueNm, result.peakTorqueRpm));
			mark(static_cast<float>(result.peakPowerRpm), static_cast<float>(result.peakPowerKw) * HP_PER_KW, powerTop, POWER,
				std::format("{:.0f} hp @ {:.0f}", result.peakPowerKw * HP_PER_KW, result.peakPowerRpm));
			if (result.failed)
			{
				float x = xOf(result.failureRpm);
				drawList->AddLine({ x - 8.f, plotMin.y + 8.f }, { x + 8.f, plotMin.y + 24.f }, REDLINE, 3.f);
				drawList->AddLine({ x + 8.f, plotMin.y + 8.f }, { x - 8.f, plotMin.y + 24.f }, REDLINE, 3.f);
				drawList->AddText({ (std::min)(x + 12.f, plotMax.x - 120.f), plotMin.y + 8.f }, REDLINE, std::format("Failed at {:.0f}", result.failureRpm).c_str());
			}
		}

		// The needle, while it pulls.
		if (live && liveRpm >= minRpm && liveRpm <= maxRpm)
			drawList->AddLine({ xOf(liveRpm), plotMin.y }, { xOf(liveRpm), plotMax.y }, IM_COL32(255, 255, 255, 200), 1.5f);

		// Pointing at the chart reads it off at that speed.
		if (hovered && finished && !result.curve.empty())
		{
			float mouseX = ImGui::GetIO().MousePos.x;
			const DynoPoint* nearest = &result.curve.front();
			for (const auto& point : result.curve)
				if (std::abs(xOf(point.rpm) - mouseX) < std::abs(xOf(nearest->rpm) - mouseX))
					nearest = &point;
			float x = xOf(nearest->rpm);
			drawList->AddLine({ x, plotMin.y }, { x, plotMax.y }, IM_COL32(255, 255, 255, 90));
			drawList->AddCircleFilled({ x, yOf(nearest->torqueNm, torqueTop) }, 3.5f, TORQUE);
			drawList->AddCircleFilled({ x, yOf(nearest->powerKw * HP_PER_KW, powerTop) }, 3.5f, POWER);
			ImGui::SetTooltip("%.0f rpm\nTorque %.1f Nm (%.1f lb-ft)\nPower %.1f hp (%.1f kW)", nearest->rpm, nearest->torqueNm,
				nearest->torqueNm * 0.7376f, nearest->powerKw * HP_PER_KW, nearest->powerKw);
		}
	}
}

void WorkshopUI::DrawDynoTab(Workshop& workshop)
{
	int onDyno = workshop.GetEngineOnDyno();

	// Nothing on the dyno: pick a finished engine to move over from its stand.
	if (onDyno < 0)
	{
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("The dyno is empty. A finished engine comes off its stand and is strapped down here to be run; afterwards the engine hoist takes it back onto a free stand, or it's sold from here.");
		ImGui::PopTextWrapPos();
		ImGui::SeparatorText("Engines");
		for (int engine = 0; engine < workshop.GetEngineCount(); engine++)
		{
			const auto& assembly = workshop.GetAssembly(engine);
			ImGui::PushID(engine);
			int needed = 0;
			for (int slot = 0; slot < static_cast<int>(AssemblySlot::COUNT); slot++)
				needed += assembly.SlotCount(static_cast<AssemblySlot>(slot));
			int fitted = static_cast<int>(assembly.GetAllParts().size());
			ImGui::Text("Engine %d (stand %d)", engine + 1, engine + 1);
			ImGui::SameLine(200.f);
			ImGui::TextColored(fitted == needed ? COLOR_GOOD : fitted > 0 ? COLOR_WARN : COLOR_DIM, "%d / %d parts", fitted, needed);
			ImGui::SameLine(330.f);
			ImGui::BeginDisabled(!assembly.IsComplete());
			if (ImGui::Button("Move onto the dyno"))
				workshop.MoveEngineToDyno(engine);
			ImGui::EndDisabled();
			if (!assembly.IsComplete())
			{
				ImGui::SameLine();
				ImGui::TextDisabled(fitted == 0 ? "No engine" : "Not finished");
			}
			ImGui::PopID();
		}
		DrawLastSale(workshop);
		return;
	}

	// The engine on the dyno is the one the sheet is about.
	workshop.SetActiveEngine(onDyno);
	const auto& spec = workshop.GetSpec();
	ImGui::Text("Engine %d is on the dyno.", onDyno + 1);

	ImGui::BeginDisabled(workshop.GetMoney() < 50.0 || m_pullActive);
	if (ImGui::Button("Pull to redline ($50)"))
	{
		const auto& pull = workshop.RunDyno();
		if (pull.ran && workshop.IsDynoResultCurrent() && !pull.curve.empty())
		{
			m_pullEngine = onDyno;
			m_pullActive = true;
			m_pullTime = 0.f;
			m_spinDownTime = 0.f;
			m_pullStartRpm = pull.curve.front().rpm;
			m_pullEndRpm = pull.failed ? pull.failureRpm : pull.curve.back().rpm;
			m_pullFailed = pull.failed;
		}
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("A wide-open-throttle pull from %d rpm to the %d rpm redline, recording torque and power all the way.", 1000, spec.redline);

	const auto& result = workshop.GetLastDynoResult();
	if (result.ran)
	{
		if (!workshop.IsDynoResultCurrent())
			ImGui::TextColored(COLOR_WARN, "The engine has changed since this pull.");

		// While the pull runs, the needle sweeps and the curves draw as it goes.
		float liveRpm = m_pullActive ? GetLiveRpm() : 1e9f;
		int shownPoints = 0;
		for (const auto& point : result.curve)
			if (point.rpm <= liveRpm)
				shownPoints++;

		if (m_pullActive)
		{
			const DynoPoint* now = shownPoints > 0 ? &result.curve[shownPoints - 1] : nullptr;
			ImGui::SetWindowFontScale(1.6f);
			ImGui::TextColored(COLOR_WARN, "%5.0f rpm", liveRpm);
			ImGui::SetWindowFontScale(1.f);
			ImGui::SameLine(0.f, 30.f);
			ImGui::Text("Torque %.1f Nm   Power %.1f hp", now ? now->torqueNm : 0.f, now ? now->powerKw * 1.341f : 0.f);
		}
		else if (result.failed)
			ImGui::TextColored(COLOR_BAD, "FAILED at %.0f rpm", result.failureRpm);

		if (!result.curve.empty())
			DrawDynoChart(result, shownPoints, liveRpm, m_pullActive, static_cast<float>(spec.redline));

		if (!m_pullActive)
		{
			// The numbers off the sheet.
			double litres = result.displacementCc / 1000.0;
			double horsepower = result.peakPowerKw * 1.341;
			double stockCc = spec.cylinders * std::numbers::pi / 4.0 * spec.bore.Mid() * spec.bore.Mid() * spec.stroke.Mid() / 1000.0;
			// Each spec's name in its own colour, red to violet down the sheet.
			const ImVec4 RAINBOW[] = {
				{ 1.00f, 0.38f, 0.38f, 1.f },	// red
				{ 1.00f, 0.60f, 0.25f, 1.f },	// orange
				{ 1.00f, 0.88f, 0.30f, 1.f },	// yellow
				{ 0.45f, 0.90f, 0.40f, 1.f },	// green
				{ 0.35f, 0.85f, 0.85f, 1.f },	// teal
				{ 0.40f, 0.62f, 1.00f, 1.f },	// blue
				{ 0.60f, 0.50f, 1.00f, 1.f },	// indigo
				{ 0.85f, 0.50f, 1.00f, 1.f },	// violet
			};
			constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;
			if (ImGui::BeginTable("DynoNumbers", 3, flags))
			{
				ImGui::TableSetupColumn("Spec", ImGuiTableColumnFlags_WidthStretch, 0.8f);
				ImGui::TableSetupColumn("This pull", ImGuiTableColumnFlags_WidthStretch, 1.2f);
				ImGui::TableSetupColumn("Stock", ImGuiTableColumnFlags_WidthStretch, 1.2f);
				ImGui::TableHeadersRow();
				size_t rowIndex = 0;
				// The spec's name explains itself when hovered, and so does the rest of its row.
				// A value given as a fraction (0 to 1) is drawn as a bar.
				auto row = [&](const char* label, const char* tooltip, const std::string& value, const std::string& stock,
					std::optional<ImVec4> color = std::nullopt, std::optional<float> fraction = std::nullopt) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextColored(RAINBOW[rowIndex++ % std::size(RAINBOW)], "%s", label);
					bool hovered = ImGui::IsItemHovered();
					ImGui::TableNextColumn();
					if (fraction)
						ImGui::ProgressBar(*fraction, { -1.f, 0.f }, value.c_str());
					else if (color)
						ImGui::TextColored(*color, "%s", value.c_str());
					else
						ImGui::TextUnformatted(value.c_str());
					hovered |= ImGui::IsItemHovered();
					ImGui::TableNextColumn();
					ImGui::TextDisabled("%s", stock.c_str());
					hovered |= ImGui::IsItemHovered();
					if (hovered)
						ImGui::SetTooltip("%s", tooltip);
				};
				row("Peak power",
					"The most power the engine made anywhere in the pull, and the rpm it made it at. Power is torque times\n"
					"rpm: how fast the engine can do work, and what decides top speed. Worn or out-of-spec parts, low\n"
					"compression and poor sealing all take it away.",
					std::format("{:.1f} hp ({:.1f} kW) @ {:.0f}", horsepower, result.peakPowerKw, result.peakPowerRpm),
					std::format("{:.0f} hp @ {}", spec.ratedPowerHp, spec.ratedPowerRpm),
					horsepower >= spec.ratedPowerHp * 0.95 ? COLOR_GOOD : COLOR_WARN);
				row("Peak torque",
					"The hardest the crank twisted, and the rpm it did it at. Torque is the pull you feel pulling away and\n"
					"climbing hills. It peaks where the engine breathes best, well below redline.",
					std::format("{:.1f} Nm ({:.1f} lb-ft) @ {:.0f}", result.peakTorqueNm, result.peakTorqueNm * 0.7376, result.peakTorqueRpm),
					std::format("{:.0f} Nm ({:.0f} lb-ft) @ {}", spec.ratedTorqueNm, spec.ratedTorqueNm * 0.7376, spec.ratedTorqueRpm),
					result.peakTorqueNm >= spec.ratedTorqueNm * 0.95 ? COLOR_GOOD : COLOR_WARN);
				row("Specific output",
					"Peak power per litre of displacement: how hard the engine works each litre, so engines of different\n"
					"sizes can be compared.",
					litres > 0.0 ? std::format("{:.1f} hp per litre", horsepower / litres) : "-",
					std::format("{:.1f} hp per litre", spec.ratedPowerHp / (stockCc / 1000.0)));
				row("Displacement",
					"The volume the pistons sweep from bottom to top, all cylinders together. Boring the block oversize\n"
					"or changing the stroke changes it.",
					std::format("{:.0f} cc ({:.2f} L)", result.displacementCc, litres),
					std::format("{:.0f} cc ({:.2f} L)", stockCc, stockCc / 1000.0));
				row("Compression",
					"How far the mixture is squeezed: the cylinder's volume with the piston at the bottom over its volume at\n"
					"the top. Higher burns more efficiently and makes more power, but too high knocks. Set by the chamber,\n"
					"the piston dish, the deck clearance and the head gasket.",
					std::format("{:.2f}:1", result.compressionRatio),
					std::format("{:.1f} - {:.1f}:1", spec.compressionRatio.min, spec.compressionRatio.max),
					spec.compressionRatio.Contains(result.compressionRatio) ? COLOR_GOOD : COLOR_WARN);
				row("Oil pressure",
					"Hot oil pressure in the main gallery. It's set by the pump and how tight the bearings are: worn or loose\n"
					"bearings let the oil out and pressure drops. Below 1.5 bar the bearings run short of oil.",
					std::format("{:.1f} bar", result.oilPressureBar), "1.5 bar or more",
					result.oilPressureBar >= 1.5 ? COLOR_GOOD : COLOR_BAD);
				row("Blow-by",
					"Combustion gas that leaks past the rings into the crankcase, as a share of what's in the cylinder.\n"
					"Every engine has a little. More means worn rings, a bad ring gap, scored bores or leaking valves,\n"
					"and it costs power and pressurises the crankcase.",
					std::format("{:.1f}%", result.blowbyPercent), "about 1%",
					result.blowbyPercent <= 2.0 ? COLOR_GOOD : COLOR_WARN);
				row("Health",
					"An overall score from everything on the dyno sheet below. Each problem found takes some off, worse ones\n"
					"more. A buyer pays more for a healthier engine.",
					std::format("{:.0f}%", result.healthScore), "100%", std::nullopt, static_cast<float>(result.healthScore / 100.0));
				ImGui::EndTable();
			}

			ImGui::SeparatorText("Dyno sheet");
			for (const auto& finding : result.findings)
			{
				ImVec4 color = finding.severity == FindingSeverity::FAILURE ? COLOR_BAD : finding.severity == FindingSeverity::WARNING ? COLOR_WARN : COLOR_DIM;
				ImGui::TextColored(color, "%s", finding.message.c_str());
			}
		}
	}
	if (m_pullActive)
		return;

	// Done with it: sell it from here, or take it back to a stand on the hoist.
	ImGui::SeparatorText("Off the dyno");
	if (workshop.GetEngineOnDyno() < 0)
	{
		DrawLastSale(workshop);
		return;
	}
	DrawSellButton(workshop);
	// Back to a stand it goes on the engine hoist, lifted off the dyno and lowered onto a free stand.
	ImGui::PushTextWrapPos(0.f);
	ImGui::TextDisabled("To work on it again, lift it off the dyno with the engine hoist and lower it onto a free engine stand.");
	ImGui::PopTextWrapPos();
}


void WorkshopUI::DrawLog(Workshop& workshop)
{
	if (ImGui::BeginChild("Log", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		for (const auto& line : workshop.GetLog())
			ImGui::TextUnformatted(line.c_str());

		if (workshop.GetLog().size() != m_lastLogSize)
		{
			ImGui::SetScrollHereY(1.f);
			m_lastLogSize = workshop.GetLog().size();
		}
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawToolBuy(Workshop& workshop, size_t toolIndex)
{
	// How many the shop has, and another to buy (for a second bench, or a friend in the garage).
	const auto& tool = ToolCatalog::Of(toolIndex);
	int owned = workshop.ToolCount(toolIndex);
	if (owned > 0)
		ImGui::TextColored(COLOR_GOOD, owned == 1 ? "You have one." : "You have %d.", owned);
	if (owned >= Workshop::MAX_TOOL_COPIES)
	{
		ImGui::TextDisabled("That's as many as the shop has room for.");
		return;
	}
	if (!workshop.ToolUnlocked(toolIndex))
	{
		if (tool.skill >= 0 && workshop.SkillLevel(static_cast<SkillKind>(tool.skill)) == 0)
		{
			ImGui::TextColored(COLOR_WARN, "Locked: needs the %s perk.", Skills::Of(static_cast<SkillKind>(tool.skill)).name);
			ImGui::TextDisabled("Spend skill points on it in the backpack's Rank & Skills tab.");
		}
		else
		{
			ImGui::TextColored(COLOR_WARN, "Locked: sold to rank %d shops (%s) and up.", tool.rank, ShopRank::Info(tool.rank).name);
			ImGui::TextDisabled("Sell good engines to raise the shop's rank, or buy Supplier Connections in the Rank & Skills tab.");
		}
		return;
	}
	CartLine line;
	line.kind = CartLine::Kind::TOOL;
	line.index = toolIndex;
	std::string label = owned > 0 ? std::format("Buy another (${:.0f})", tool.price * workshop.EquipmentPriceFactor()) : std::format("Add to cart (${:.0f})", tool.price * workshop.EquipmentPriceFactor());
	AddToCartButton(workshop, line, label.c_str());
}
