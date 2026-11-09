#include "ServiceManual.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>

#include <imgui.h>

#include "../Controls.h"
#include "ModelPreview.h"
#include "../Simulation/EngineSpec.h"
#include "../Simulation/Machine.h"
#include "../Simulation/MeasuringTool.h"
#include "../Simulation/PartCatalog.h"
#include "../Simulation/Units.h"
#include "../World/ShopAssets.h"
#include "../World/ToolShapes.h"

namespace
{
	// The interface is drawn into an sRGB target, so colours given as they should look are taken back
	// to linear first (or the navy comes out pale blue and the ink grey).
	ImU32 Shade(int r, int g, int b, int a = 255)
	{
		auto linear = [](int value) { return static_cast<int>(std::pow(value / 255.0, 2.2) * 255.0 + 0.5); };
		return IM_COL32(linear(r), linear(g), linear(b), a);
	}
	// The page's inks: cream paper and black ink, or (dark mode) charcoal paper and pale ink. Set as the
	// book is drawn (SetInks).
	ImU32 PAPER = 0;
	ImU32 PAPER_SHADE = 0;
	ImU32 INK = 0;
	ImU32 INK_FAINT = 0;
	ImU32 ACCENT = 0;		// the cover's navy, for headings (a paler blue on a dark page)
	ImU32 NOTE_FILL = 0;
	void SetInks(bool dark)
	{
		PAPER = dark ? Shade(34, 33, 36) : Shade(244, 238, 222);
		PAPER_SHADE = dark ? Shade(82, 78, 72) : Shade(200, 190, 165);
		INK = dark ? Shade(228, 222, 208) : Shade(30, 27, 24);
		INK_FAINT = dark ? Shade(150, 142, 128) : Shade(110, 100, 86);
		ACCENT = dark ? Shade(140, 168, 230) : Shade(28, 48, 100);
		NOTE_FILL = dark ? Shade(52, 49, 44) : Shade(232, 222, 190);
	}
	const ImU32 COVER = Shade(24, 36, 76);
	const ImU32 GOLD = Shade(206, 160, 56);
	// Reading size, against the interface font.
	constexpr float TEXT_SCALE = 1.2f;

	// Text sizes as multiples of the interface font.
	constexpr float HEADING_SCALE = 1.35f;
	constexpr float BODY_SCALE = 1.f;
	constexpr float ITEM_NAME_SCALE = 1.1f;
	constexpr float CAPTION_SCALE = 0.75f;
	// 3D views, square, in multiples of the text size: a tool's beside its description, a part's under its step.
	constexpr float ITEM_VIEW = 5.5f;
	constexpr float STEP_VIEW = 4.2f;
}

void ServiceManual::Open()
{
	m_open = true;
}

void ServiceManual::Close()
{
	m_open = false;
}

bool ServiceManual::Turn(int spreads)
{
	int before = m_spread;
	m_spread = std::clamp(m_spread + spreads, 0, (std::max)(SpreadCount() - 1, 0));
	return m_spread != before;
}

bool ServiceManual::IsOpen() const
{
	return m_open;
}

void ServiceManual::SetModelLookup(const std::function<uint32_t(const std::string&)>& lookup)
{
	m_modelLookup = lookup;
	m_models.clear();
}

void ServiceManual::Build(bool imperial)
{
	const EngineSpec& spec = EngineSpec::InlineFour();
	auto system = imperial ? Units::System::IMPERIAL : Units::System::METRIC;
	auto size = [&](double mm) { return Units::FormatSize(mm, system); };
	auto deviation = [&](double mm) { return Units::FormatDeviation(mm, system); };
	auto tolerance = [&](const Tolerance& t) { return size(t.Min()) + " - " + size(t.Max()); };
	auto clearance = [&](const Range& r) { return deviation(r.min) + " - " + deviation(r.max); };
	auto most = [&](double mm) { return "max " + deviation(mm); };
	auto torque = [&](double nm) { return imperial ? std::format("{:.0f} Nm ({:.0f} ft-lb)", nm, nm * 0.737562) : std::format("{:.0f} Nm", nm); };
	const double pi = 3.14159265358979;
	double displacement = spec.cylinders * pi * 0.25 * spec.bore.Mid() * spec.bore.Mid() * spec.stroke.nominal / 1000.0;

	m_chapters.clear();
	Chapter* chapter = nullptr;
	int stepNumber = 0;
	int figureNumber = 0;
	auto begin = [&](const std::string& title) {
		m_chapters.push_back({ title, {} });
		chapter = &m_chapters.back();
		stepNumber = 0;
		figureNumber = 0;
	};
	auto heading = [&](const std::string& text) { chapter->lines.push_back({ Kind::HEADING, text }); stepNumber = 0; };
	auto text = [&](const std::string& text) { chapter->lines.push_back({ Kind::TEXT, text }); };
	// A step that brings parts in shows each of them under it.
	auto step = [&](const std::string& text, const std::vector<PartType>& parts = {}) {
		Line line = { Kind::STEP, text, std::to_string(++stepNumber) };
		for (PartType part : parts)
			line.views.push_back({ PartTypeName(part), PartTypeName(part) });
		chapter->lines.push_back(std::move(line));
	};
	// A tool or machine: its 3D view beside its name, description and figures.
	auto item = [&](const std::string& name, const std::string& description, const std::string& model,
		std::vector<std::pair<std::string, std::string>> specs) {
		Line line = { Kind::ITEM, name, description };
		line.views.push_back({ model, "" });
		line.specs = std::move(specs);
		chapter->lines.push_back(std::move(line));
	};
	auto figure = [&](const std::string& name, const std::string& value, const std::string& tip = "") {
		Line line = { Kind::SPEC, name, value };
		const char* known = SpecTip(name);
		line.tip = !tip.empty() ? tip : known ? known : "";
		chapter->lines.push_back(std::move(line));
	};
	auto note = [&](const std::string& text) { chapter->lines.push_back({ Kind::NOTE, text }); };
	// Figures are numbered within their chapter: Fig. 7.2 is the second in chapter 7.
	auto illustrate = [&](Figure figure, const std::string& caption) {
		chapter->lines.push_back({ Kind::FIGURE, std::format("Fig. {}.{}  {}", m_chapters.size() - 1, ++figureNumber, caption), std::to_string(static_cast<int>(figure)) });
	};

	// The contents are filled in once every chapter exists.
	begin("Contents");

	begin("1. General Information");
	heading("The LV-16");
	text(std::format("The LV-16 is a {:.1f} litre ({:.0f} cc) inline four: a grey iron block with {} main bearings, an aluminium head with a "
		"single overhead camshaft working one intake and one exhaust valve per cylinder through rocker arms, and a toothed timing belt "
		"driving the cam from the crank. A two-barrel carburettor feeds it and a distributor on the back of the cam sparks it. It makes "
		"70 hp at 4800 rpm and 85 ft-lb at 2800, and revs to {} rpm.", displacement / 1000.0, displacement, spec.mainJournals, spec.redline));
	heading("Engine Data");
	figure("Cylinders", std::to_string(spec.cylinders) + ", in line");
	figure("Bore x stroke", std::format("{:.0f} x {:.0f} mm", spec.bore.nominal, spec.stroke.nominal));
	figure("Displacement", std::format("{:.0f} cc", displacement));
	figure("Compression ratio", std::format("{:.1f} - {:.1f} : 1", spec.compressionRatio.min, spec.compressionRatio.max));
	figure("Main bearings", std::to_string(spec.mainJournals));
	figure("Camshaft", std::format("SOHC, {} journals", spec.camJournals));
	figure("Valves", std::format("{} per cylinder", 2));
	figure("Valvetrain", "Rocker arms on two shafts");
	figure("Timing drive", "Toothed belt");
	figure("Fuel system", "Two-barrel carburettor");
	figure("Ignition", "Distributor");
	figure("Rated output", "70 hp @ 4800 rpm, 85 ft-lb @ 2800 rpm");
	figure("Redline", std::format("{} rpm", spec.redline));
	heading("Using This Manual");
	text("Every dimension here is the blueprint the engine was built to. The job is always the same: measure a part, compare it with "
		"the book, then machine it back into spec, fit a part that brings it there, or replace it. Clearances are what really matter, and "
		"they come from two parts together, so measure both before you decide.");
	text(std::string("Figures are shown in ") + (imperial ? "inches and thousandths of an inch" : "millimetres") +
		", the units set in Settings. Torques are in newton metres.");
	note("Nothing in the shop tells you a part is good or bad. Only your measurements do, and the dyno finds out the rest.");
	heading("Shop Controls");
	// As they're bound now (Settings, Controls).
	using Controls::NameOf;
	figure("Walk / run", std::format("{}{}{}{} / {}", NameOf(Control::FORWARD), NameOf(Control::LEFT), NameOf(Control::BACK), NameOf(Control::RIGHT), NameOf(Control::RUN)));
	figure("Jump / crouch", std::format("{} / {}", NameOf(Control::JUMP), NameOf(Control::CROUCH)));
	figure("Use, pick up, fit", NameOf(Control::USE));
	figure("Measure, or hold to do a bolt up", NameOf(Control::PRIMARY));
	figure("Set down / drop", std::format("{} / {}", NameOf(Control::SET_DOWN), NameOf(Control::DROP)));
	figure("Hold to undo a bolt (spanner in hand)", NameOf(Control::SET_DOWN));
	figure("Hotbar", "1 - 0, mouse wheel");
	figure("Backpack", NameOf(Control::BACKPACK));
	figure("Move equipment", NameOf(Control::MOVE));
	figure("Turn the engine over", NameOf(Control::ROTATE));
	figure("Roll the engine on its stand", std::format("{} / {}", NameOf(Control::ROLL_BACK), NameOf(Control::ROLL_FORWARD)));

	begin("2. Tools and Equipment");
	heading("Measuring Tools");
	text("Each tool reads only what it fits, and no better than its resolution. Cheap tools are quick; precise tools are slow but "
		"repeatable. Buy the tool the tolerance needs: a caliper is no use for a clearance measured in hundredths.");
	illustrate(Figure::TOOL_RESOLUTION, "How finely each tool reads. Green bars read finer than the bore tolerance; red ones can't tell a good bore from a bad one.");
	for (const auto& tool : ToolCatalog::All())
	{
		if (tool.IsHandTool() || tool.manual)
			continue;
		std::vector<std::pair<std::string, std::string>> specs;
		if (tool.resolution > 0.0)
			specs.push_back({ "Reads to", deviation(tool.resolution) });
		if (tool.sigma > 0.0)
			specs.push_back({ "Repeatability", "+/- " + deviation(tool.sigma) });
		if (tool.maxSize > 0.0)
			specs.push_back({ "Fits", size(tool.minSize) + " - " + size(tool.maxSize) });
		item(tool.name, tool.description, ToolModelName(tool), std::move(specs));
	}
	heading("Hand Tools");
	for (const auto& tool : ToolCatalog::All())
	{
		if (!tool.IsHandTool() || tool.partOf >= 0 || tool.Paints())
			continue;
		std::vector<std::pair<std::string, std::string>> specs;
		if (!tool.sizes.empty())
		{
			std::string sizes;
			for (int mm : tool.sizes)
				sizes += (sizes.empty() ? "" : ", ") + std::to_string(mm);
			specs.push_back({ "Sizes", sizes + " mm" });
		}
		if (tool.torqueError > 0.0)
			specs.push_back({ "Accuracy", std::format("+/- {:.0f}%", tool.torqueError * 100.0) });
		if (tool.maxTorque > 0.0)
			specs.push_back({ "Up to", torque(tool.maxTorque) });
		if (tool.TurnSeconds() > 0.0)
			specs.push_back({ "Time per bolt", std::format("about {:.1f} s", tool.TurnSeconds()) });
		if (tool.drawAh > 0.0)
			specs.push_back({ "Battery use", std::format("{:.2f} Ah a bolt, more for tight ones", tool.drawAh) });
		if (tool.capacityAh > 0.0)
			specs.push_back({ "Holds", std::format("{:.0f} Ah", tool.capacityAh) });
		if (tool.chargeAmps > 0.0)
			specs.push_back({ "Charges at", std::format("{:.0f} A", tool.chargeAmps) });
		item(tool.name, tool.description, ToolModelName(tool), std::move(specs));
	}
	note("Every nut and bolt needs a socket or wrench of its size. Only a torque wrench does one up to spec; anything else leaves it "
		"snug and no more.");
	heading("Machines");
	text("Machining only ever removes metal. Every cut is final, so measure twice and take less than you think.");
	for (const auto& machine : MachineCatalog::All())
	{
		std::vector<std::pair<std::string, std::string>> specs;
		if (machine.sigma > 0.0)
			specs.push_back({ "Holds size to", "+/- " + deviation(machine.sigma) });
		if (machine.finishRa > 0.0)
			specs.push_back({ "Finish", std::format("{:.2f} um Ra", machine.finishRa) });
		if (machine.maxRemoval > 0.0)
			specs.push_back({ "Most per setup", deviation(machine.maxRemoval) });
		item(machine.name, machine.description, ShopAssets::FixtureFor(machine).model, std::move(specs));
	}

	begin("3. Getting Started");
	heading("The Job, Start to Finish");
	step("Lay the parts out. A new shop starts with a worn-out core already stripped onto the parts shelves.");
	step("Measure every part against this book (chapters 4 to 9). Note what is worn, what can be machined and what must be replaced.");
	step("Buy what you need at the parts computer. Parts and tools are delivered in boxes and crates at the roll-up door: open them with F.");
	step("Machine the parts that can be saved: hold the part, walk up to the machine and press E to set it up, then set the target in the "
		"machine's screen.");
	step("Check the clearances with the parts together: piston to wall, bearing oil clearance, ring gap, cam clearance.");
	step("Assemble on the engine stand in the order in chapter 10, torquing every bolt to chapter 11.");
	step("Final checks (chapter 12), then sell it from its stand: the buyer runs it and pays for what it makes. With a dyno of your own, "
		"run it first and put right what the dyno finds.");
	heading("At the Workbench");
	text("The workbench is where a part is read. Everything you learn about a part with your tools is written up there, feature by "
		"feature, against this book's figures. Nowhere else shows a part's details: the engine stand lists what's fitted and the engine's "
		"checks, and a machine shows only what it can cut.");
	step(std::format("Set the part down on the bench top: hold it and right-click, or {} to drop it there. Anything resting on the "
		"top counts, however it got there.", NameOf(Control::DROP)));
	step(std::format("Look at the bench and press {} to open it. Every part on it is listed down the side; pick one to read it.",
		NameOf(Control::USE)));
	step("The header gives its number, material and condition, and why it's damaged if it is. A nut or bolt also shows its "
		"thread and head size, the hole it's in and how tight it was done up, or every hole it fits.");
	step("Below that, the part's features (bores, journals, faces, gaps), grouped by area on a big part. Each row has the spec, your "
		"last measured size and finish, a verdict, and which tools can measure it.");
	step("Click a row to open it: the spec beside your last reading, station by station, what to do to bring it back into spec, and "
		"the machines that can cut it.");
	text("A part that isn't fitted can be sold from the bench for half its value.");
	text("A feature reads \"Not measured\" until you measure it in the shop: hold the tool and aim at the feature on the part. A reading "
		"turns grey and stale once the part has been cut or changed since, so measure it again. A feature that says \"Check on stand\" "
		"can only be measured with the engine together, such as an oil clearance or a deck height.");
	note("The bench only reports what you measured, with the tool you measured it with. A caliper's verdict on a bore is only as good as "
		"a caliper. Any workbench reads parts, the shop's own or one bought from the store: each shows what's on its own top.");
	heading("At the Engine Stand");
	text("Hold a part and look at the engine: the place it fits is shown, and E fits it there. Look at a fitted part with empty hands "
		"and E takes it off. R turns the crank over; Z and X roll the whole engine on the stand so you can get at the bottom end.");
	text(std::format("{} at the stand lists the parts fitted and the engine's checks (clearances, torques, timing, belt deflection). "
		"To read a part's details, take it off and set it on the workbench.", NameOf(Control::USE)));
	text("Empty bolt holes are highlighted when you are close to the engine. Look at a nut or bolt to see its size, how tight it is "
		"and its torque spec.");
	heading("Taking an Engine Apart");
	text("Work backwards through the assembly sequence: covers and manifolds first, then the timing belt, the head, the oil pan, the "
		"rods and pistons, and finally the main caps and the crank. Each part comes off once what holds it is undone.");
	step(std::format("Hold the wrench set (or, with the Sockets & Ratchets perk, a socket set and a ratchet; the beam torque wrench and its sockets only do bolts up). E on a nut or bolt takes out the "
		"right wrench, or puts the right socket on. Hold {} on it to undo it, {} to do it back up; let go part way and it stays part "
		"way, and it only comes out by hand once it's undone all the way.", NameOf(Control::SET_DOWN), NameOf(Control::PRIMARY)));
	step("The crank pulley bolt is 22 mm and very tight: the big wrench set turns it, a breaker bar more easily.");
	step("Undo the head, main cap, rocker shaft and cam cap bolts in the reverse of their tightening order (chapter 11), a little at a "
		"time, so nothing is left bearing on a few bolts.");
	step("Take the part off with E once its fasteners are out. Keep the rod and main caps with their own rod and journal: they are "
		"machined as a pair.");
	note("Single-use parts (the head gasket, among others) are crushed or stretched once fitted. Never put one back on.");

	begin("4. Cylinder Block");
	heading("Specifications");
	figure("Bore diameter", tolerance(spec.bore));
	figure("Bore taper", most(spec.maxBoreTaper));
	figure("Bore out of round", most(spec.maxBoreOutOfRound));
	figure("Bore finish", std::format("{:.2f} - {:.2f} um Ra", spec.boreFinish.min, spec.boreFinish.max));
	figure("Main housing bore", tolerance(spec.mainHousing));
	figure("Deck height", tolerance(spec.deckHeight));
	figure("Deck flatness", most(spec.maxDeckFlatness));
	figure("Resurfacing limit", most(spec.maxResurface));
	heading("Measuring the Bores");
	step("Use a dial bore gauge (or a telescoping gauge and micrometer for a rough check).");
	step("Measure each bore at the top, middle and bottom of the ring travel, across and along the block.");
	step("Taper is the biggest difference top to bottom; out of round, the biggest difference across. Wear is always worst at the top.");
	illustrate(Figure::BORE_POINTS, "Where to measure a bore: three heights, two directions at each.");
	step("Check the finish with the profilometer: too smooth and the rings never seat, too rough and they wear out.");
	heading("Machining the Bores");
	text("A bore that is only glazed or slightly out of round can be honed back to a fresh finish. Past that, bore it oversize on the "
		"boring bar and fit oversize pistons, then hone to finish. Bore every cylinder to the same oversize.");
	note("Leave honing stock: bore a few hundredths under the final size and hone the rest.");
	heading("Deck and Main Housings");
	text(std::format("Lay the straightedge across the deck in several directions and slip feelers under it. A warped deck is surfaced flat "
		"on the surface grinder, which also lowers the deck height and raises compression. No more than {} may come off in all: a deck "
		"still warped past that means a new block. Main housings that are out of line or oversize are line bored.", deviation(spec.maxResurface)));

	begin("5. Crankshaft");
	heading("Specifications");
	figure("Main journal diameter", tolerance(spec.mainJournal));
	figure("Rod journal diameter", tolerance(spec.rodJournal));
	figure("Journal taper", most(spec.maxJournalTaper));
	figure("Journal out of round", most(spec.maxJournalOutOfRound));
	figure("Journal finish", std::format("max {:.2f} um Ra", spec.maxJournalFinish));
	figure("Stroke", tolerance(spec.stroke));
	heading("Inspection");
	step("Measure every journal with the outside micrometer at both ends and at two angles 90 degrees apart.");
	step("Taper is the difference end to end; out of round, the difference between the two angles.");
	illustrate(Figure::JOURNAL_POINTS, "Measuring a journal: both ends, two angles 90 degrees apart.");
	step("Check the finish: scored journals chew up bearings.");
	heading("Machining");
	text("A journal that is just rough can be polished. A worn, tapered or scored journal is ground undersize on the crankshaft "
		"grinder, and then takes undersize bearing shells (with thicker walls) to bring the oil clearance back. Grind all the mains to one "
		"size and all the rod journals to one size.");
	note("Grinding a journal changes its oil clearance. Always Plastigauge the bearings after grinding (chapter 7).");

	begin("6. Rods, Pistons and Rings");
	heading("Connecting Rods");
	figure("Big end bore (housing)", tolerance(spec.rodHousing));
	figure("Centre to centre length", tolerance(spec.rodLength));
	figure("Bend, per 100 mm", most(spec.maxRodBend));
	figure("Twist, per 100 mm", most(spec.maxRodTwist));
	figure("Side clearance on the crank pin", clearance(spec.rodSideClearance));
	figure("Side clearance limit", most(spec.maxRodSideClearance));
	text("Big ends stretch oval with miles. Measure the housing with the cap torqued on at two angles. Rod length moves the piston at "
		"top dead centre, which changes deck clearance and compression.");
	step("Hang each rod on the rod aligner and check it for bend, then twist. Past the limit, replace the rod: it holds its piston "
		"crooked in the bore.", { PartType::CONNECTING_ROD });
	step("Fit the rod, with its bearings and cap, on its crank pin. Slip feelers between the big end and the crank cheek: that's the "
		"side clearance. Past the limit, replace the rod.");
	heading("Pistons");
	figure("Skirt diameter (standard)", tolerance(spec.pistonSkirt));
	figure("Compression height", tolerance(spec.compressionHeight));
	figure("Dish volume", std::format("{:.1f} - {:.1f} cc", spec.dishVolume.Min(), spec.dishVolume.Max()));
	heading("Piston to Wall Clearance");
	text("Bore diameter minus skirt diameter, measured at the skirt. Forged pistons grow more when hot and need more room cold:");
	figure("Cast", clearance(spec.PistonToWallFor(MaterialType::CAST_ALUMINUM)));
	figure("Hypereutectic", clearance(spec.PistonToWallFor(MaterialType::HYPEREUTECTIC_ALUMINUM)));
	figure("Forged 2618", clearance(spec.PistonToWallFor(MaterialType::FORGED_2618)));
	illustrate(Figure::PISTON_CLEARANCE, "Piston to wall clearance: bore diameter minus skirt diameter (gap exaggerated).");
	note("Too tight and the piston scuffs or seizes when hot. Too loose and it rocks and slaps, and the rings never seal.");
	heading("Ring End Gap");
	figure("Top ring end gap", clearance(spec.ringGap));
	illustrate(Figure::RING_GAP, "Checking a ring's end gap, the ring squared in its own bore.");
	step("Push the ring squarely into its own bore with a piston, about a third of the way down.", { PartType::PISTON_RING, PartType::PISTON });
	step("Measure the gap between the ends with the feeler gauge set.");
	step("Too tight: file it open on the ring filer, a little at a time, and measure again. Too wide: fit a new ring.");
	note("Gap each ring to the bore it goes in. Ends that butt when hot break the ring land.");

	begin("7. Bearings and Oil Clearance");
	heading("Specifications");
	figure("Main shell thickness (std)", tolerance(spec.mainShell));
	figure("Rod shell thickness (std)", tolerance(spec.rodShell));
	figure("Main oil clearance", clearance(spec.mainClearance));
	figure("Rod oil clearance", clearance(spec.rodClearance));
	text("Oil clearance is the housing bore, minus the journal, minus both shells. Too tight and the bearing runs dry and spins; too "
		"loose and oil pressure falls away and the bearing knocks.");
	illustrate(Figure::OIL_CLEARANCE, "A main bearing end on (oil clearance exaggerated).");
	heading("Checking With Plastigauge");
	text("Plastigauge is a soft thread that squashes flat between the journal and the bearing. How wide it spreads is the clearance.");
	step("Fit the upper shells in the block and lay the crankshaft in them.", { PartType::MAIN_BEARING, PartType::CRANKSHAFT });
	step("Hold the Plastigauge and lay a strip across the journal, along its length.");
	step("Fit the lower shell into the main cap, then the cap over the journal.", { PartType::MAIN_BEARING_LOWER, PartType::MAIN_CAP });
	step("Torque the cap bolts to spec. Do not turn the crank while the strip is in.", { PartType::BOLT_M10 });
	step("Undo the bolts and take the cap off. The flattened strip stays on the journal.");
	step("Read its width against the scale on the packet: that is the oil clearance.");
	illustrate(Figure::PLASTIGAUGE_SCALE, "The Plastigauge scale. Readings in red are outside the oil clearance spec.");
	text("Rod bearings are checked the same way: the rod, with its upper shell in it, on the crank pin, a strip across the journal, then the "
		"rod cap with its lower shell in it, torqued and removed again.");
	note("Out of spec? Undersize shells close up a ground journal; a worn journal is ground and fitted with them.");
	heading("The Crank Won't Turn");
	text("The engine only turns over with every upper main shell, the crank, every lower shell and every main cap in place.");

	begin("8. Cylinder Head and Valves");
	heading("Head");
	figure("Combustion chamber volume", std::format("{:.1f} - {:.1f} cc", spec.chamberVolume.Min(), spec.chamberVolume.Max()));
	figure("Head gasket face flatness", most(spec.maxHeadFlatness));
	figure("Manifold face flatness", most(spec.maxPortFaceFlatness));
	figure("Resurfacing limit (gasket face)", most(spec.maxResurface));
	figure("Intake manifold flange flatness", most(spec.maxIntakeManifoldFlatness));
	figure("Head gasket thickness (compressed)", tolerance(spec.gasketThickness));
	figure("Head gasket bore", size(spec.gasketBore));
	text(std::format("Check the gasket face and both manifold faces with the straightedge and feelers, and the intake manifold's flange "
		"too. Surface what's warped. Surfacing the gasket face raises compression, and no more than {} may come off it in all: past "
		"that, replace the head. The chambers are measured with the burette and cc plate.", deviation(spec.maxResurface)));
	heading("Valve Guides and Stems");
	figure("Guide bore", tolerance(spec.guideBore));
	figure("Guide taper (bellmouth)", most(spec.maxGuideTaper));
	figure("Guide out of round", most(spec.maxGuideOutOfRound));
	figure("Intake stem diameter", tolerance(spec.intakeStem));
	figure("Exhaust stem diameter", tolerance(spec.exhaustStem));
	figure("Intake stem to guide", clearance(spec.intakeStemClearance));
	figure("Exhaust stem to guide", clearance(spec.exhaustStemClearance));
	figure("Intake stem to guide, limit", most(spec.maxIntakeStemClearance));
	figure("Exhaust stem to guide, limit", most(spec.maxExhaustStemClearance));
	text("Measure guides with the small hole gauge or small-bore gauge; stems with the micrometer. A guide worn a little past new is "
		"still serviceable; past the limit, ream it oversize and fit an oversize-stem valve.");
	heading("Valves and Seats");
	figure("Seat width", std::format("{} - {}", deviation(spec.seatWidth.min), deviation(spec.seatWidth.max)));
	figure("Seat runout", most(spec.maxSeatRunout));
	figure("Valve runout", most(spec.maxValveRunout));
	figure("Valve margin", tolerance(spec.valveMargin));
	figure("Valve margin minimum", deviation(spec.minValveMargin));
	figure("Overall length", tolerance(spec.valveLength));
	text("Recut worn seats with the seat cutter and reface valves on the refacer. Refacing thins the margin: below the minimum the "
		"valve burns, so replace it.");
	illustrate(Figure::VALVE_DIMENSIONS, "A valve installed in the head, and what is measured.");
	heading("Springs");
	figure("Free length", tolerance(spec.springFreeLength));
	figure("Out of square", most(spec.maxSpringSquareness));
	figure("Installed height", size(spec.installedHeight));
	figure("Tension at installed height", std::format("{:.0f} N ({:.1f} kg)", spec.springRate * (spec.springFreeLength.nominal - spec.installedHeight),
		spec.springRate * (spec.springFreeLength.nominal - spec.installedHeight) / 9.80665));
	figure("Minimum tension", std::format("{:.0f} N ({:.0f} kg)", spec.springSeatPressure.Min(), spec.springSeatPressure.Min() / 9.80665));
	figure("Spring rate", std::format("{:.0f} N/mm", spec.springRate));
	figure("Installed height, in the head", std::format("{} - {}", size(spec.installedHeightRange.min), size(spec.installedHeightRange.max)));
	step("Stand each spring on the bench against the steel square and turn it: the widest gap at the top is how far out of square it is.",
		{ PartType::VALVE_SPRING });
	step("Measure its free length with the caliper.");
	step("Squeeze it to the installed height on the valve spring tester and read the tension.");
	step("Replace any spring out of square, short, or under the minimum tension.");
	text("With the valve, seal, spring and retainer fitted, measure the installed height with the installed height micrometer: cutting "
		"seats and refacing valves sinks the valve and raises it, which weakens the spring. Weak springs let the valves float at high rpm; "
		"an out-of-square one pushes the stem sideways and wears its guide.");
	illustrate(Figure::SPRING_CURVE, "Spring force against height. A good spring passes through the green box at its installed height.");

	begin("9. Camshaft");
	heading("Specifications");
	figure("Cam bearing bore (head)", tolerance(spec.camBore));
	figure("Cam journal diameter", tolerance(spec.camJournal));
	figure("Lobe height", tolerance(spec.lobeHeight));
	figure("Lobe height minimum", size(spec.minLobeHeight));
	figure("Cam oil clearance", clearance(spec.camClearance));
	figure("Cam oil clearance limit", most(spec.maxCamClearance));
	figure("End play", clearance(spec.camEndPlay));
	figure("End play limit", most(spec.maxCamEndPlay));
	figure("Bend (runout)", most(spec.maxCamRunout));
	figure("Fuel pump eccentric height", tolerance(spec.fuelPumpEccentric));
	figure("Fuel pump eccentric minimum", size(spec.minFuelPumpEccentric));
	text("Lobe height is airflow: a worn lobe costs power on that cylinder. Mic every lobe across its nose; below the minimum, replace "
		"the cam.");
	illustrate(Figure::CAM_PROFILE, "Valve lift through one turn of the cam, new and worn.");
	heading("Checking the Camshaft");
	step("End play: with the cam in the head and its caps torqued, set a dial indicator on its nose and push it back and forth. Past the "
		"limit, replace the cam and/or the head.", { PartType::CAMSHAFT });
	step("Bend: lay the cam in the V-blocks, set the pointer on the centre journal and turn it once. Past the limit, replace it.");
	step("Lobes: mic each lobe's height, and the fuel pump eccentric between the first lobes and the second journal. Below "
		"either minimum, replace the cam: a worn eccentric short-strokes the fuel pump and starves the engine at high rpm.");
	heading("Checking Cam Clearance");
	step("Lay the camshaft in the head, the rocker shafts off.", { PartType::CAMSHAFT });
	step("Lay a Plastigauge strip across each journal.");
	step("Fit each cam cap in its own numbered place and torque its bolts. Do not turn the cam while the strips are in.",
		{ PartType::CAM_CAP, PartType::BOLT_M6 });
	step("Undo the caps, lift them off and read the strips. Past the limit, replace the cam and the head.");
	heading("Rocker Arms and Shafts");
	figure("Rocker arm bore", tolerance(spec.rockerBore));
	figure("Rocker shaft diameter", tolerance(spec.rockerShaft));
	figure("Arm to shaft clearance", clearance(spec.rockerClearance));
	figure("Arm to shaft clearance, limit", most(spec.maxRockerClearance));
	text("Measure each arm's bore with the small-bore gauge and the shaft under each arm with the micrometer: the shafts wear "
		"where the arms ride. Past the limit, replace the arm, the shaft or both: the arm rocks on its shaft and ticks.");

	begin("10. Assembly Sequence");
	text("The engine goes together in this order. A part can only be fitted once everything it depends on is in place and done up; the "
		"stand tells you what is missing.");
	illustrate(Figure::ASSEMBLY_FLOW, "The build at a glance.");
	heading("Bottom End");
	step("Block on the stand. Core plugs.", { PartType::BLOCK, PartType::CORE_PLUG });
	step("Upper main shells in the block, oiled.", { PartType::MAIN_BEARING });
	step("Crankshaft into the shells.", { PartType::CRANKSHAFT });
	step("Lower main shells into the main caps, then each cap onto its own journal. Torque the main cap bolts.", { PartType::MAIN_BEARING_LOWER, PartType::MAIN_CAP, PartType::BOLT_M10 });
	step("Rear main seal over the crank flange, then the flywheel on the flange, its bolts torqued.", { PartType::REAR_MAIN_SEAL, PartType::FLYWHEEL, PartType::BOLT_M10 });
	step("Oil pump over the crank snout, then the pickup.", { PartType::OIL_PUMP, PartType::OIL_PICKUP });
	heading("Pistons and Rods, Each Cylinder");
	step("Off the engine: the piston onto its connecting rod, its pin pressed through both with the Piston Pin Press, the ring "
		"(gapped) onto the piston, and the upper rod shell into the rod.", { PartType::PISTON, PartType::PISTON_PIN, PartType::PISTON_RING, PartType::ROD_BEARING });
	step("Squeeze the ring into its groove with the Piston Ring Compressor, then the rod, piston and all, down the bore and onto "
		"the crank pin.", { PartType::CONNECTING_ROD });
	step("Lower rod shell into the rod cap, then the cap onto the rod. Torque the rod nuts.", { PartType::ROD_BEARING_LOWER, PartType::ROD_CAP, PartType::NUT_M8 });
	note("Shells, pistons and rings go into and onto their rods, caps and pistons off the engine: hold one, aim at what it goes on (on "
		"the bench, a shelf or the floor) and press E; R takes the last one off again. With the Piston Pin Press or the Piston Ring "
		"Compressor in hand and aimed at the piston, hold the left button to press its pin in or squeeze its ring, the right to undo it. A rod "
		"goes on only with its shell in and its piston pinned, ringed and squeezed, a cap only with its shell, and they come off with them.");
	heading("Top End");
	step("Head gasket, once every piston and rod is in.", { PartType::HEAD_GASKET });
	step("Cylinder head. Torque the head bolts.", { PartType::CYLINDER_HEAD, PartType::BOLT_M12 });
	step("Each valve: valve, stem seal, spring, retainer and keepers.", { PartType::INTAKE_VALVE, PartType::EXHAUST_VALVE, PartType::VALVE_STEM_SEAL, PartType::VALVE_SPRING, PartType::VALVE_RETAINER });
	step("Camshaft into the head, then the cam caps, torqued.", { PartType::CAMSHAFT, PartType::CAM_CAP });
	step("Rocker shafts over the cam, intake side and exhaust side, bolts torqued. Then a rocker arm onto its shaft over each valve.", { PartType::ROCKER_SHAFT, PartType::ROCKER_ARM, PartType::BOLT_M8 });
	heading("Timing Belt");
	step("Crank timing pulley (in front of the pump) and cam timing pulley.", { PartType::CRANK_SPROCKET, PartType::CAM_SPROCKET });
	step("Timing belt round the two pulleys with the marks lined up, then the idler pulley onto the back of its slack side, its "
		"bolt torqued. Check the belt's deflection halfway down the tight side: 6 - 7 mm under 2 kg.", { PartType::TIMING_BELT, PartType::BELT_TENSIONER, PartType::BOLT_M10 });
	illustrate(Figure::BELT_ROUTING, "Timing belt routing, from the front, with the timing marks at TDC. The arrow is where its deflection is checked.");
	figure("Timing belt deflection (2 kg)", size(spec.timingBeltDeflection.min) + " - " + size(spec.timingBeltDeflection.max));
	step("Timing cover gasket and cover, front crank seal, crank pulley (its bolt to full torque).", { PartType::TIMING_COVER_GASKET, PartType::TIMING_COVER, PartType::FRONT_CRANK_SEAL, PartType::CRANK_PULLEY, PartType::BOLT_M14 });
	heading("Closing Up and Ancillaries");
	step("Oil pan gasket and pan, once the pickup, rear seal, timing cover and rod caps are on. Drain plug and oil filter.", { PartType::OIL_PAN_GASKET, PartType::OIL_PAN, PartType::OIL_DRAIN_PLUG, PartType::OIL_FILTER });
	step("Valve cover gasket and cover, once the rockers are in. Spark plugs; the distributor into the back of the head on the cam; then a lead from its cap down through the cover onto each plug.", { PartType::VALVE_COVER_GASKET, PartType::VALVE_COVER, PartType::SPARK_PLUG, PartType::DISTRIBUTOR, PartType::PLUG_LEAD });
	step("Intake manifold and gasket, then the carburettor on top of the plenum. The mechanical fuel pump on the side of the head at the cam, its arm in on the cam's eccentric.", { PartType::INTAKE_MANIFOLD_GASKET, PartType::INTAKE_MANIFOLD, PartType::CARBURETTOR, PartType::FUEL_PUMP });
	step("Exhaust manifold and gasket, then the downpipe on its collector.", { PartType::EXHAUST_MANIFOLD_GASKET, PartType::EXHAUST_MANIFOLD, PartType::EXHAUST_PIPE });
	step("Water pump and gasket on the timing cover; thermostat and its housing.", { PartType::WATER_PUMP_GASKET, PartType::WATER_PUMP, PartType::THERMOSTAT, PartType::THERMOSTAT_HOUSING });
	step("Alternator on its pivot at the front. The fan belt round the crank pulley, the water pump and the alternator; swing the "
		"alternator out until the belt deflects 8.5 - 10.5 mm under 10 kg between the water pump and the alternator (10 - 12 mm "
		"for a used belt), then torque its bolts.", { PartType::ALTERNATOR, PartType::DRIVE_BELT, PartType::BOLT_M10 });
	illustrate(Figure::DRIVE_BELT_ROUTING, "Fan belt routing, from the front. The arrow is where its deflection is checked.");
	figure("Fan belt deflection, new (10 kg)", size(spec.fanBeltDeflectionNew.min) + " - " + size(spec.fanBeltDeflectionNew.max));
	figure("Fan belt deflection, used (10 kg)", size(spec.fanBeltDeflectionUsed.min) + " - " + size(spec.fanBeltDeflectionUsed.max));
	figure("Fan belt size", "11 x 980 mm V-belt");

	begin("11. Torque Specifications");
	text("Torque with a torque wrench driving the right socket. The engine's real clamping force is only as good as the wrench: a "
		"beam wrench is good to about 10%, a click wrench about 4%, a digital one about 2%.");
	illustrate(Figure::TORQUE_ACCURACY, "Where a head bolt really ends up with each torque wrench.");
	heading("Tightening Sequences");
	text("Head bolts, main cap bolts, rocker shaft bolts and cam cap bolts are done up in the order shown, from the middle outward, "
		"a little at a time, so the part is pulled down evenly. They come undone in the reverse order.");
	illustrate(Figure::HEAD_BOLT_SEQUENCE, "Head bolt sequence.");
	illustrate(Figure::MAIN_CAP_SEQUENCE, "Main cap bolt sequence, cap 1 at the front. To take the caps off, start at 10 and work back.");
	illustrate(Figure::ROCKER_SHAFT_SEQUENCE, "Rocker shaft bolt sequence: five bolts down through both shafts.");
	illustrate(Figure::CAM_CAP_SEQUENCE, "Cam cap bolt sequence: both bolts on a cap, a little at a time.");
	heading("Fasteners");
	for (const auto& holes : PartCatalog::Holes())
	{
		const auto& info = PartCatalog::Get(holes.size);
		figure(std::format("{} ({} x{}, {} mm)", holes.name, info.fastener.thread, holes.perParent, info.fastener.headMm), torque(holes.torqueNm),
			std::format("How tight to do up the {}, with a torque wrench on a {} mm socket. {} is the thread (M: metric, then its diameter in mm); "
				"x{} is how many there are {}.", holes.name, info.fastener.headMm, info.fastener.thread, holes.perParent,
				holes.throughAll ? "in all, each through every one of the parts it holds" : "on each part"));
	}
	note("Head bolts, main cap bolts and rod nuts carry the engine. Under-torqued, a cap walks and a gasket leaks; over-torqued, a bolt "
		"stretches past its limit.");

	begin("12. Final Checks and the Dyno");
	heading("Deck Clearance and Quench");
	figure("Deck height (block)", tolerance(spec.deckHeight));
	figure("Quench (piston to head)", clearance(spec.quench));
	step("With a piston at top dead centre (R turns the crank), set the dial indicator and deck bridge across the bore.");
	step("Read how far the piston sits below the deck. Add the compressed head gasket thickness to get the quench.");
	text("Quench too tight and the piston can touch the head; too loose and the engine knocks. Decking the block or a thicker gasket "
		"moves it.");
	heading("Compression Ratio");
	figure("Target", std::format("{:.1f} - {:.1f} : 1", spec.compressionRatio.min, spec.compressionRatio.max));
	text("The ratio comes from the bore, stroke, chamber volume, piston dish, deck clearance and gasket. Surfacing the head or block "
		"raises it; too high and the engine detonates.");
	illustrate(Figure::COMPRESSION_GRAPH, "Compression ratio against metal taken off the block deck.");
	heading("Dyno");
	text("A new shop has no dyno: a finished engine is sold untested, and its figures come with the buyer's payment. A dyno is bought at "
		"the parts computer.");
	step("With the engine complete on its stand, open the dyno (E at the dyno cell) and move the engine onto it.");
	step("Run a pull. The chart shows power and torque against rpm; the readout lists anything the engine is unhappy about.");
	step("Sell it from the dyno, or put it back on a free stand to fix what it found.");
	note("A sound, accurate build makes more power and sells for more. Every clearance out of spec costs something on the dyno.");

	begin("13. Troubleshooting");
	auto problem = [&](const std::string& symptom, const std::string& causes) {
		heading(symptom);
		text(causes);
	};
	problem("Low compression on one cylinder", "Ring gap too wide, a worn or glazed bore, a burnt valve (margin too thin) or a valve "
		"not seating (seat runout, weak spring).");
	problem("Knocking at low rpm", "Main or rod oil clearance too wide, or piston slap from too much piston to wall clearance.");
	problem("Detonation, power falls away", "Compression too high (head or deck surfaced too far) or quench too wide.");
	problem("Bearing failure", "Oil clearance too tight, a journal tapered or out of round, or a cap bolt under-torqued.");
	problem("Power falls off at high rpm", "Weak valve springs (installed height too tall, low seat pressure) letting the valves float, "
		"or a worn cam lobe.");
	problem("Engine won't turn over", "A main shell, cap or the crank itself is missing, or a fastener is holding something it "
		"shouldn't.");
	problem("Oil leaks", "A gasket or seal left out, a single-use gasket reused, or its bolts not torqued.");

	// The contents: every chapter but itself.
	for (size_t i = 1; i < m_chapters.size(); i++)
		m_chapters[0].lines.push_back({ Kind::CONTENTS, m_chapters[i].title, std::to_string(i) });

	m_built = true;
	m_builtImperial = imperial;
	m_pagedWidth = 0.f;
}

float ServiceManual::Measure(const Line& line, float width, float fontSize) const
{
	ImFont* font = ImGui::GetFont();
	float gap = fontSize * 0.4f;
	auto wrapped = [&](const std::string& text, float scale, float wrap) {
		return font->CalcTextSizeA(fontSize * scale, FLT_MAX, wrap, text.c_str()).y;
	};
	switch (line.kind)
	{
		case Kind::HEADING:		return fontSize * 0.6f + wrapped(line.text, HEADING_SCALE, width) + fontSize * 0.5f;
		case Kind::STEP:
			return wrapped(line.text, BODY_SCALE, width - fontSize * 2.f) + (line.views.empty() ? 0.f : LayoutViews(line.views, width, fontSize)) + gap;
		case Kind::ITEM:
		{
			float textWidth = width - fontSize * (ITEM_VIEW + 0.8f);
			float text = wrapped(line.text, ITEM_NAME_SCALE, textWidth) + fontSize * 0.25f + wrapped(line.value, BODY_SCALE, textWidth) +
				fontSize * 0.35f + fontSize * 1.15f * line.specs.size();
			return (std::max)(fontSize * ITEM_VIEW, text) + fontSize * 1.2f;
		}
		case Kind::BULLET:		return wrapped(line.text, BODY_SCALE, width - fontSize * 1.4f) + gap;
		case Kind::NOTE:		return wrapped(line.text, BODY_SCALE, width - fontSize * 1.6f) + fontSize * 1.2f + gap;
		case Kind::CONTENTS:	return fontSize * 1.6f;
		case Kind::FIGURE:
			return fontSize * 0.4f + fontSize * FigureLines(static_cast<Figure>(std::stoi(line.value))) + fontSize * 0.5f + wrapped(line.text, 0.9f, width) + gap * 2.f;
		case Kind::SPEC:
		{
			float valueWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, line.value.c_str()).x;
			return wrapped(line.text, BODY_SCALE, (std::max)(width - valueWidth - fontSize * 1.5f, width * 0.3f)) + gap * 0.6f;
		}
		default:				return wrapped(line.text, BODY_SCALE, width) + gap;
	}
}

void ServiceManual::Paginate(float width, float height, float fontSize)
{
	m_pages.clear();
	m_chapterPage.assign(m_chapters.size(), 0);
	m_pages.push_back({ -1, {} });	// the cover
	for (size_t chapter = 0; chapter < m_chapters.size(); chapter++)
	{
		// Every chapter starts on a fresh page; a chapter's title takes the top of its first page.
		m_chapterPage[chapter] = static_cast<int>(m_pages.size());
		m_pages.push_back({ static_cast<int>(chapter), {} });
		float used = fontSize * 2.6f;
		const auto& lines = m_chapters[chapter].lines;
		for (size_t i = 0; i < lines.size(); i++)
		{
			float need = Measure(lines[i], width, fontSize);
			// A heading never sits alone at the bottom of a page.
			float keep = lines[i].kind == Kind::HEADING && i + 1 < lines.size() ? Measure(lines[i + 1], width, fontSize) : 0.f;
			if (used + need + keep > height && !m_pages.back().lines.empty())
			{
				m_pages.push_back({ static_cast<int>(chapter), {} });
				used = 0.f;
			}
			m_pages.back().lines.push_back(lines[i]);
			used += need;
		}
	}
	m_spread = std::clamp(m_spread, 0, SpreadCount() - 1);
}

float ServiceManual::LayoutViews(const std::vector<View>& views, float width, float fontSize,
	const std::function<void(size_t, float, float)>& place) const
{
	// Indented with the step's text, as many to a row as fit; each row as tall as its longest caption.
	ImFont* font = ImGui::GetFont();
	float indent = fontSize * 2.f, size = fontSize * STEP_VIEW, spacing = fontSize * 0.5f;
	size_t perRow = static_cast<size_t>((std::max)(1.f, std::floor((width - indent + spacing) / (size + spacing))));
	float y = fontSize * 0.3f;
	for (size_t first = 0; first < views.size(); first += perRow)
	{
		size_t last = (std::min)(first + perRow, views.size());
		float captionHeight = 0.f;
		for (size_t i = first; i < last; i++)
		{
			captionHeight = (std::max)(captionHeight, font->CalcTextSizeA(fontSize * CAPTION_SCALE, FLT_MAX, size, views[i].caption.c_str()).y);
			if (place)
				place(i, indent + (i - first) * (size + spacing), y);
		}
		y += size + fontSize * 0.2f + captionHeight + fontSize * 0.3f;
	}
	return y;
}

void ServiceManual::DrawView(ImDrawList* drawList, const std::string& model, const ImVec2& min, const ImVec2& max)
{
	// The same backdrop as the parts computer's previews, so a part looks the same in the book as in the shop.
	drawList->AddRectFilledMultiColor(min, max, IM_COL32(52, 56, 64, 255), IM_COL32(52, 56, 64, 255), IM_COL32(28, 30, 34, 255), IM_COL32(28, 30, 34, 255));
	drawList->AddRect(min, max, PAPER_SHADE);
	if (!m_modelLookup)
		return;
	auto it = m_models.find(model);
	if (it == m_models.end())
		it = m_models.emplace(model, m_modelLookup(model)).first;
	if (it->second)
		ModelPreview::DrawAt(drawList, { min.x + 2.f, min.y + 2.f }, { max.x - 2.f, max.y - 2.f }, it->second);
}

int ServiceManual::SpreadCount() const
{
	return static_cast<int>((m_pages.size() + 1) / 2);
}

void ServiceManual::HoverSpec(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const std::string& name, const std::string& tip, float fontSize) const
{
	if (!ImGui::IsMouseHoveringRect(min, max))
		return;
	drawList->AddRectFilled(min, max, NOTE_FILL, 3.f);
	// On paper like the book's notes, with the figure's name in the heading navy.
	ImGui::PushStyleColor(ImGuiCol_PopupBg, PAPER);
	ImGui::PushStyleColor(ImGuiCol_Border, GOLD);
	ImGui::PushStyleColor(ImGuiCol_Text, INK);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { fontSize * 0.6f, fontSize * 0.5f });
	if (ImGui::BeginTooltip())
	{
		ImGui::PushTextWrapPos(fontSize * 22.f);
		ImGui::PushStyleColor(ImGuiCol_Text, ACCENT);
		ImGui::TextUnformatted(name.c_str());
		ImGui::PopStyleColor();
		ImGui::TextUnformatted(tip.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(3);
}

void ServiceManual::DrawCover(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float fontSize) const
{
	// The title page: the maker's band, the engine's name, a cutaway of four cylinders, the book's title.
	ImFont* font = ImGui::GetFont();
	float width = max.x - min.x, height = max.y - min.y;
	auto centred = [&](const char* text, float scale, float y, ImU32 color) {
		float size = fontSize * scale;
		ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
		drawList->AddText(font, size, { min.x + (width - extent.x) * 0.5f, y }, color, text);
	};
	// The maker's band is the cover's navy on any page, so the gold name on it always reads.
	drawList->AddRectFilled({ min.x, min.y + height * 0.08f }, { max.x, min.y + height * 0.16f }, COVER);
	centred("MONKEY KOBO MOTOR WORKS", 1.3f, min.y + height * 0.12f - fontSize * 0.65f, GOLD);
	centred("LV-16", 4.f, min.y + height * 0.22f, ACCENT);
	centred("1.6 LITRE SOHC INLINE FOUR", 1.2f, min.y + height * 0.22f + fontSize * 4.6f, INK);

	// Four bores across a block, pistons and rods below them, a crank along the bottom.
	float blockLeft = min.x + width * 0.2f, blockRight = max.x - width * 0.2f;
	float blockTop = min.y + height * 0.45f, blockBottom = min.y + height * 0.66f;
	drawList->AddRect({ blockLeft, blockTop }, { blockRight, blockBottom }, ACCENT, 4.f, 0, 2.5f);
	float pitch = (blockRight - blockLeft) / 4.f;
	for (int cylinder = 0; cylinder < 4; cylinder++)
	{
		float x0 = blockLeft + pitch * cylinder + pitch * 0.15f, x1 = blockLeft + pitch * (cylinder + 1) - pitch * 0.15f;
		float pistonTop = blockTop + (cylinder == 1 || cylinder == 2 ? 0.45f : 0.12f) * (blockBottom - blockTop);
		drawList->AddRect({ x0, blockTop + 4.f }, { x1, blockBottom - 4.f }, INK_FAINT, 0.f, 0, 1.2f);
		drawList->AddRectFilled({ x0 + 2.f, pistonTop }, { x1 - 2.f, pistonTop + pitch * 0.35f }, ACCENT);
		drawList->AddLine({ (x0 + x1) * 0.5f, pistonTop + pitch * 0.35f }, { (x0 + x1) * 0.5f, blockBottom + height * 0.04f }, ACCENT, 3.f);
	}
	drawList->AddLine({ blockLeft - 10.f, blockBottom + height * 0.04f }, { blockRight + 10.f, blockBottom + height * 0.04f }, ACCENT, 4.f);

	centred("FACTORY SERVICE MANUAL", 1.6f, min.y + height * 0.78f, ACCENT);
	centred("Specifications  -  Inspection  -  Machining  -  Assembly", 1.f, min.y + height * 0.78f + fontSize * 2.2f, INK_FAINT);
	drawList->AddRectFilled({ min.x, max.y - height * 0.06f }, { max.x, max.y - height * 0.04f }, GOLD);
}

bool ServiceManual::DrawPage(ImDrawList* drawList, int page, const ImVec2& min, const ImVec2& max, float fontSize)
{
	if (page < 0 || page >= static_cast<int>(m_pages.size()))
		return false;
	if (page == 0)
	{
		DrawCover(drawList, min, max, fontSize);
		return false;
	}

	ImFont* font = ImGui::GetFont();
	const Page& content = m_pages[page];
	const Chapter& chapter = m_chapters[content.chapter];
	float width = max.x - min.x;
	bool first = m_chapterPage[content.chapter] == page;
	bool jumped = false;

	// Running header (or the chapter's title on its first page) and the page number at the foot.
	float y = min.y;
	if (first)
	{
		drawList->AddText(font, fontSize * 1.6f, { min.x, y }, ACCENT, chapter.title.c_str());
		y += fontSize * 2.f;
		drawList->AddLine({ min.x, y }, { max.x, y }, ACCENT, 2.f);
		y += fontSize * 0.6f;
	}
	else
	{
		drawList->AddText(font, fontSize * 0.85f, { min.x, min.y - fontSize * 1.6f }, INK_FAINT, chapter.title.c_str());
		drawList->AddLine({ min.x, min.y - fontSize * 0.5f }, { max.x, min.y - fontSize * 0.5f }, PAPER_SHADE, 1.f);
	}
	std::string number = std::to_string(page);
	ImVec2 numberSize = font->CalcTextSizeA(fontSize * 0.9f, FLT_MAX, 0.f, number.c_str());
	drawList->AddText(font, fontSize * 0.9f, { min.x + (width - numberSize.x) * 0.5f, max.y + fontSize * 0.9f }, INK_FAINT, number.c_str());

	for (const auto& line : content.lines)
	{
		float height = Measure(line, width, fontSize);
		switch (line.kind)
		{
			case Kind::HEADING:
				drawList->AddText(font, fontSize * HEADING_SCALE, { min.x, y + fontSize * 0.6f }, ACCENT, line.text.c_str(), nullptr, width);
				break;
			case Kind::STEP:
			{
				std::string label = line.value + ".";
				drawList->AddText(font, fontSize, { min.x, y }, ACCENT, label.c_str());
				drawList->AddText(font, fontSize, { min.x + fontSize * 2.f, y }, INK, line.text.c_str(), nullptr, width - fontSize * 2.f);
				// The parts it brings in, in rows under it, each captioned.
				if (!line.views.empty())
				{
					float top = y + font->CalcTextSizeA(fontSize, FLT_MAX, width - fontSize * 2.f, line.text.c_str()).y;
					float viewSize = fontSize * STEP_VIEW;
					LayoutViews(line.views, width, fontSize, [&](size_t i, float x, float viewY) {
						ImVec2 viewMin = { min.x + x, top + viewY }, viewMax = { viewMin.x + viewSize, viewMin.y + viewSize };
						DrawView(drawList, line.views[i].model, viewMin, viewMax);
						const char* caption = line.views[i].caption.c_str();
						ImVec2 extent = font->CalcTextSizeA(fontSize * CAPTION_SCALE, FLT_MAX, viewSize, caption);
						drawList->AddText(font, fontSize * CAPTION_SCALE, { viewMin.x + (std::max)((viewSize - extent.x) * 0.5f, 0.f), viewMax.y + fontSize * 0.2f },
							INK_FAINT, caption, nullptr, viewSize);
					});
				}
				break;
			}
			case Kind::ITEM:
			{
				// The 3D view on the left; name, description and figures beside it; a hairline under the lot.
				float viewSize = fontSize * ITEM_VIEW, textX = min.x + viewSize + fontSize * 0.8f, textWidth = max.x - textX;
				if (!line.views.empty())
					DrawView(drawList, line.views[0].model, { min.x, y }, { min.x + viewSize, y + viewSize });
				float textY = y;
				drawList->AddText(font, fontSize * ITEM_NAME_SCALE, { textX, textY }, ACCENT, line.text.c_str(), nullptr, textWidth);
				textY += font->CalcTextSizeA(fontSize * ITEM_NAME_SCALE, FLT_MAX, textWidth, line.text.c_str()).y + fontSize * 0.25f;
				drawList->AddText(font, fontSize, { textX, textY }, INK, line.value.c_str(), nullptr, textWidth);
				textY += font->CalcTextSizeA(fontSize, FLT_MAX, textWidth, line.value.c_str()).y + fontSize * 0.35f;
				for (const auto& [name, figure] : line.specs)
				{
					if (const char* tip = SpecTip(name))
						HoverSpec(drawList, { textX - fontSize * 0.2f, textY - fontSize * 0.05f }, { max.x + fontSize * 0.2f, textY + fontSize * 1.1f }, name, tip, fontSize);
					ImVec2 nameSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, name.c_str());
					ImVec2 figureSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, figure.c_str());
					drawList->AddText(font, fontSize, { textX, textY }, INK_FAINT, name.c_str());
					drawList->AddText(font, fontSize, { max.x - figureSize.x, textY }, ACCENT, figure.c_str());
					for (float x = textX + nameSize.x + fontSize * 0.5f; x < max.x - figureSize.x - fontSize * 0.5f; x += fontSize * 0.5f)
						drawList->AddCircleFilled({ x, textY + fontSize * 0.75f }, 1.f, PAPER_SHADE);
					textY += fontSize * 1.15f;
				}
				float rule = y + height - fontSize * 0.5f;
				drawList->AddLine({ min.x, rule }, { max.x, rule }, PAPER_SHADE, 1.f);
				break;
			}
			case Kind::BULLET:
				drawList->AddCircleFilled({ min.x + fontSize * 0.45f, y + fontSize * 0.55f }, fontSize * 0.16f, ACCENT);
				drawList->AddText(font, fontSize, { min.x + fontSize * 1.4f, y }, INK, line.text.c_str(), nullptr, width - fontSize * 1.4f);
				break;
			case Kind::NOTE:
			{
				ImVec2 boxMax = { max.x, y + height - fontSize * 0.4f };
				drawList->AddRectFilled({ min.x, y }, boxMax, NOTE_FILL, 3.f);
				drawList->AddRectFilled({ min.x, y }, { min.x + 4.f, boxMax.y }, GOLD);
				drawList->AddText(font, fontSize, { min.x + fontSize * 0.8f, y + fontSize * 0.6f }, INK, line.text.c_str(), nullptr, width - fontSize * 1.6f);
				break;
			}
			case Kind::SPEC:
			{
				// Name on the left, figure flush right, dotted leaders between.
				if (!line.tip.empty())
					HoverSpec(drawList, { min.x - fontSize * 0.2f, y - fontSize * 0.05f }, { max.x + fontSize * 0.2f, y + height - fontSize * 0.1f }, line.text, line.tip, fontSize);
				ImVec2 valueSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, line.value.c_str());
				float nameWidth = (std::max)(width - valueSize.x - fontSize * 1.5f, width * 0.3f);
				ImVec2 nameSize = font->CalcTextSizeA(fontSize, FLT_MAX, nameWidth, line.text.c_str());
				drawList->AddText(font, fontSize, { min.x, y }, INK, line.text.c_str(), nullptr, nameWidth);
				drawList->AddText(font, fontSize, { max.x - valueSize.x, y + nameSize.y - fontSize }, ACCENT, line.value.c_str());
				float dotsY = y + nameSize.y - fontSize * 0.25f;
				float lastLine = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, line.text.c_str()).x;
				float dotsFrom = min.x + (nameSize.y > fontSize * 1.5f ? 0.f : lastLine) + fontSize * 0.5f;
				for (float x = dotsFrom; x < max.x - valueSize.x - fontSize * 0.5f; x += fontSize * 0.5f)
					drawList->AddCircleFilled({ x, dotsY }, 1.f, PAPER_SHADE);
				break;
			}
			case Kind::CONTENTS:
			{
				// A chapter and its page, clickable.
				int target = std::stoi(line.value);
				std::string pageNumber = std::to_string(m_chapterPage[target]);
				ImVec2 rowMin = { min.x, y }, rowMax = { max.x, y + fontSize * 1.4f };
				bool hovered = ImGui::IsMouseHoveringRect(rowMin, rowMax);
				if (hovered)
					drawList->AddRectFilled(rowMin, rowMax, NOTE_FILL, 3.f);
				drawList->AddText(font, fontSize * 1.1f, { min.x + fontSize * 0.3f, y + fontSize * 0.15f }, hovered ? ACCENT : INK, line.text.c_str());
				ImVec2 numberExtent = font->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, 0.f, pageNumber.c_str());
				drawList->AddText(font, fontSize * 1.1f, { max.x - numberExtent.x - fontSize * 0.3f, y + fontSize * 0.15f }, ACCENT, pageNumber.c_str());
				if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					m_spread = m_chapterPage[target] / 2;
					jumped = true;
				}
				break;
			}
			case Kind::FIGURE:
			{
				// The drawing, then its caption under it.
				float top = y + fontSize * 0.4f;
				float bottom = top + fontSize * FigureLines(static_cast<Figure>(std::stoi(line.value)));
				DrawFigure(drawList, static_cast<Figure>(std::stoi(line.value)), { min.x, top }, { max.x, bottom }, fontSize);
				drawList->AddText(font, fontSize * 0.9f, { min.x, bottom + fontSize * 0.5f }, INK_FAINT, line.text.c_str(), nullptr, width);
				break;
			}
			default:
				drawList->AddText(font, fontSize, { min.x, y }, INK, line.text.c_str(), nullptr, width);
				break;
		}
		y += height;
	}
	return jumped;
}

bool ServiceManual::Draw(bool imperial, bool dark)
{
	if (!m_open)
		return false;
	SetInks(dark);
	SetFigureInks(dark);
	if (!m_built || m_builtImperial != imperial)
		Build(imperial);

	// The open book fills most of the screen: two pages either side of the spine, the controls under it.
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImVec2 bookSize = { (std::min)(viewport->WorkSize.x * 0.9f, 1300.f), (std::min)(viewport->WorkSize.y * 0.9f, 860.f) };
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, { 0.5f, 0.5f });
	ImGui::SetNextWindowSize(bookSize, ImGuiCond_Always);
	ImGui::SetNextWindowFocus();
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
	bool turned = false;
	if (ImGui::Begin("LV-16 Factory Service Manual", nullptr, flags))
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		float fontSize = ImGui::GetFontSize() * TEXT_SCALE;
		ImVec2 origin = ImGui::GetWindowPos();
		float controlsHeight = ImGui::GetFrameHeightWithSpacing() + 12.f;
		ImVec2 coverMin = origin, coverMax = { origin.x + bookSize.x, origin.y + bookSize.y - controlsHeight };

		// Covers, the two pages, and the shadow down the spine.
		drawList->AddRectFilled(coverMin, coverMax, COVER, 10.f);
		float spine = (coverMin.x + coverMax.x) * 0.5f;
		ImVec2 leftMin = { coverMin.x + 14.f, coverMin.y + 12.f }, leftMax = { spine - 2.f, coverMax.y - 12.f };
		ImVec2 rightMin = { spine + 2.f, coverMin.y + 12.f }, rightMax = { coverMax.x - 14.f, coverMax.y - 12.f };
		drawList->AddRectFilled(leftMin, leftMax, PAPER, 4.f);
		drawList->AddRectFilled(rightMin, rightMax, PAPER, 4.f);
		drawList->AddRectFilledMultiColor({ spine - 30.f, leftMin.y }, { spine, leftMax.y }, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 60), IM_COL32(0, 0, 0, 60), IM_COL32(0, 0, 0, 0));
		drawList->AddRectFilledMultiColor({ spine, rightMin.y }, { spine + 30.f, rightMax.y }, IM_COL32(0, 0, 0, 60), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 60));

		// Text inside each page's margins: room at the top for the running header, at the foot for the number.
		float marginX = fontSize * 2.2f, marginTop = fontSize * 3.f, marginBottom = fontSize * 3.f;
		auto inner = [&](ImVec2 pageMin, ImVec2 pageMax) {
			return std::pair{ ImVec2(pageMin.x + marginX, pageMin.y + marginTop), ImVec2(pageMax.x - marginX, pageMax.y - marginBottom) };
		};
		auto [leftTextMin, leftTextMax] = inner(leftMin, leftMax);
		auto [rightTextMin, rightTextMax] = inner(rightMin, rightMax);
		float textWidth = leftTextMax.x - leftTextMin.x, textHeight = leftTextMax.y - leftTextMin.y;
		if (std::abs(textWidth - m_pagedWidth) > 0.5f || std::abs(textHeight - m_pagedHeight) > 0.5f)
		{
			Paginate(textWidth, textHeight, fontSize);
			m_pagedWidth = textWidth;
			m_pagedHeight = textHeight;
		}

		int before = m_spread;
		if (DrawPage(drawList, m_spread * 2, leftTextMin, leftTextMax, fontSize) | DrawPage(drawList, m_spread * 2 + 1, rightTextMin, rightTextMax, fontSize))
			turned = true;

		// Under the book: back, where we are, forward, close.
		ImGui::SetCursorScreenPos({ origin.x, coverMax.y + 8.f });
		float buttonWidth = 150.f;
		ImGui::SetCursorPosX((std::max)((bookSize.x - buttonWidth * 3.f - ImGui::GetStyle().ItemSpacing.x * 3.f - 420.f) * 0.5f, 0.f));
		ImGui::BeginDisabled(m_spread == 0);
		if (ImGui::Button("< Previous", { buttonWidth, 0.f }))
			m_spread--;
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(m_spread == 0);
		if (ImGui::Button("Contents", { buttonWidth, 0.f }))
			m_spread = m_chapterPage.empty() ? 0 : m_chapterPage[0] / 2;
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(m_spread >= SpreadCount() - 1);
		if (ImGui::Button("Next >", { buttonWidth, 0.f }))
			m_spread++;
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Pages %d-%d of %d   Left/Right or A/D: turn   Esc: close", m_spread * 2, m_spread * 2 + 1, static_cast<int>(m_pages.size()) - 1);
		turned = turned || m_spread != before;
	}
	ImGui::End();
	return turned;
}
