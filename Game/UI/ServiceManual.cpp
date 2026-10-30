#include "ServiceManual.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>

#include <imgui.h>

#include "../Simulation/EngineSpec.h"
#include "../Simulation/Machine.h"
#include "../Simulation/MeasuringTool.h"
#include "../Simulation/PartCatalog.h"
#include "../Simulation/Units.h"

namespace
{
	// The interface is drawn into an sRGB target, so colours given as they should look are taken back
	// to linear first (or the navy comes out pale blue and the ink grey).
	ImU32 Shade(int r, int g, int b, int a = 255)
	{
		auto linear = [](int value) { return static_cast<int>(std::pow(value / 255.0, 2.2) * 255.0 + 0.5); };
		return IM_COL32(linear(r), linear(g), linear(b), a);
	}
	const ImU32 PAPER = Shade(244, 238, 222);
	const ImU32 PAPER_SHADE = Shade(200, 190, 165);
	const ImU32 INK = Shade(30, 27, 24);
	const ImU32 INK_FAINT = Shade(110, 100, 86);
	const ImU32 ACCENT = Shade(28, 48, 100);		// the cover's navy, for headings
	const ImU32 NOTE_FILL = Shade(232, 222, 190);
	const ImU32 COVER = Shade(24, 36, 76);
	const ImU32 GOLD = Shade(206, 160, 56);
	// Reading size, against the interface font.
	constexpr float TEXT_SCALE = 1.2f;

	// Text sizes as multiples of the interface font.
	constexpr float HEADING_SCALE = 1.35f;
	constexpr float BODY_SCALE = 1.f;
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
	auto begin = [&](const std::string& title) {
		m_chapters.push_back({ title, {} });
		chapter = &m_chapters.back();
		stepNumber = 0;
	};
	auto heading = [&](const std::string& text) { chapter->lines.push_back({ Kind::HEADING, text }); stepNumber = 0; };
	auto text = [&](const std::string& text) { chapter->lines.push_back({ Kind::TEXT, text }); };
	auto step = [&](const std::string& text) { chapter->lines.push_back({ Kind::STEP, text, std::to_string(++stepNumber) }); };
	auto bullet = [&](const std::string& text) { chapter->lines.push_back({ Kind::BULLET, text }); };
	auto figure = [&](const std::string& name, const std::string& value) { chapter->lines.push_back({ Kind::SPEC, name, value }); };
	auto note = [&](const std::string& text) { chapter->lines.push_back({ Kind::NOTE, text }); };

	// The contents are filled in once every chapter exists.
	begin("Contents");

	begin("1. General Information");
	heading("The LV-16");
	text(std::format("The LV-16 is a {:.1f} litre ({:.0f} cc) inline four: a grey iron block with {} main bearings, an aluminium head with a "
		"single overhead camshaft working one intake and one exhaust valve per cylinder through bucket lifters, and a toothed timing belt "
		"driving the cam from the crank. It revs to {} rpm.", displacement / 1000.0, displacement, spec.mainJournals, spec.redline));
	heading("Engine Data");
	figure("Cylinders", std::to_string(spec.cylinders) + ", in line");
	figure("Bore x stroke", std::format("{:.0f} x {:.0f} mm", spec.bore.nominal, spec.stroke.nominal));
	figure("Displacement", std::format("{:.0f} cc", displacement));
	figure("Compression ratio", std::format("{:.1f} - {:.1f} : 1", spec.compressionRatio.min, spec.compressionRatio.max));
	figure("Main bearings", std::to_string(spec.mainJournals));
	figure("Camshaft", std::format("SOHC, {} journals", spec.camJournals));
	figure("Valves", std::format("{} per cylinder", 2));
	figure("Timing drive", "Toothed belt");
	figure("Redline", std::format("{} rpm", spec.redline));
	heading("Using This Manual");
	text("Every dimension here is the blueprint the engine was built to. The job is always the same: measure a part, compare it with "
		"the book, then machine it back into spec, fit a part that brings it there, or replace it. Clearances are what really matter, and "
		"they come from two parts together, so measure both before you decide.");
	text(std::string("Figures are shown in ") + (imperial ? "inches and thousandths of an inch" : "millimetres") +
		", the units set in Settings. Torques are in newton metres.");
	note("Nothing in the shop tells you a part is good or bad. Only your measurements do, and the dyno finds out the rest.");
	heading("Shop Controls");
	figure("Walk / run", "WASD / Shift");
	figure("Jump / crouch", "Space / Ctrl");
	figure("Use, pick up, fit", "E");
	figure("Measure (tool in hand)", "Left click");
	figure("Set down / drop", "Right click / G");
	figure("Hotbar", "1 - 0, mouse wheel");
	figure("Backpack", "Tab");
	figure("Move equipment", "F");
	figure("Turn the engine over", "R");
	figure("Roll the engine on its stand", "Z / X");

	begin("2. Tools and Equipment");
	heading("Measuring Tools");
	text("Each tool reads only what it fits, and no better than its resolution. Cheap tools are quick; precise tools are slow but "
		"repeatable. Buy the tool the tolerance needs: a caliper is no use for a clearance measured in hundredths.");
	for (const auto& tool : ToolCatalog::All())
		if (!tool.IsHandTool() && !tool.manual)
			figure(tool.name, tool.resolution > 0.0 ? deviation(tool.resolution) : "");
	heading("What Each Is For");
	for (const auto& tool : ToolCatalog::All())
		if (!tool.IsHandTool() && !tool.manual)
			bullet(tool.name + ": " + tool.description);
	heading("Hand Tools");
	for (const auto& tool : ToolCatalog::All())
		if (tool.IsHandTool())
			bullet(tool.name + ": " + tool.description);
	note("Every nut and bolt needs a socket or wrench of its size. Only a torque wrench does one up to spec; anything else leaves it "
		"snug and no more.");
	heading("Machines");
	text("Machining only ever removes metal. Every cut is final, so measure twice and take less than you think.");
	for (const auto& machine : MachineCatalog::All())
		bullet(machine.name + ": " + machine.description);

	begin("3. Getting Started");
	heading("The Job, Start to Finish");
	step("Lay the parts out. A new shop starts with a worn-out core already stripped onto the parts shelves.");
	step("Measure every part against this book (chapters 4 to 9). Note what is worn, what can be machined and what must be replaced.");
	step("Buy what you need at the parts computer. Parts and tools are delivered in boxes and crates at the roll-up door: open them with F.");
	step("Machine the parts that can be saved: hold the part, walk up to the machine and press E to set it up, then set the target in the "
		"machine's screen.");
	step("Check the clearances with the parts together: piston to wall, bearing oil clearance, ring gap, cam clearance.");
	step("Assemble on the engine stand in the order in chapter 10, torquing every bolt to chapter 11.");
	step("Final checks (chapter 12), then run it on the dyno and sell it, or take it back to a stand to put right what the dyno found.");
	heading("At the Engine Stand");
	text("Hold a part and look at the engine: the place it fits is shown, and E fits it there. Look at a fitted part with empty hands "
		"and E takes it off. R turns the crank over; Z and X roll the whole engine on the stand so you can get at the bottom end.");
	text("Empty bolt holes are highlighted when you are close to the engine. Look at a nut or bolt to see its size, how tight it is "
		"and its torque spec.");
	heading("Taking an Engine Apart");
	text("Work backwards through the assembly sequence: covers and manifolds first, then the timing belt, the head, the oil pan, the "
		"rods and pistons, and finally the main caps and the crank. Each part comes off once what holds it is undone.");
	step("Hold the socket set and a ratchet (or the wrench set). E on a nut or bolt puts the right socket on and loosens it.");
	step("The crank pulley bolt is very tight: use the breaker bar.");
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
	heading("Measuring the Bores");
	step("Use a dial bore gauge (or a telescoping gauge and micrometer for a rough check).");
	step("Measure each bore at the top, middle and bottom of the ring travel, across and along the block.");
	step("Taper is the biggest difference top to bottom; out of round, the biggest difference across. Wear is always worst at the top.");
	step("Check the finish with the profilometer: too smooth and the rings never seat, too rough and they wear out.");
	heading("Machining the Bores");
	text("A bore that is only glazed or slightly out of round can be honed back to a fresh finish. Past that, bore it oversize on the "
		"boring bar and fit oversize pistons, then hone to finish. Bore every cylinder to the same oversize.");
	note("Leave honing stock: bore a few hundredths under the final size and hone the rest.");
	heading("Deck and Main Housings");
	text("Lay the straightedge across the deck in several directions and slip feelers under it. A warped deck is surfaced flat on the "
		"surface grinder, which also lowers the deck height and raises compression. Main housings that are out of line or oversize are "
		"line bored.");

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
	text("Big ends stretch oval with miles. Measure the housing with the cap torqued on at two angles. Rod length moves the piston at "
		"top dead centre, which changes deck clearance and compression.");
	heading("Pistons");
	figure("Skirt diameter (standard)", tolerance(spec.pistonSkirt));
	figure("Compression height", tolerance(spec.compressionHeight));
	figure("Dish volume", std::format("{:.1f} - {:.1f} cc", spec.dishVolume.Min(), spec.dishVolume.Max()));
	heading("Piston to Wall Clearance");
	text("Bore diameter minus skirt diameter, measured at the skirt. Forged pistons grow more when hot and need more room cold:");
	figure("Cast", clearance(spec.PistonToWallFor(MaterialType::CAST_ALUMINUM)));
	figure("Hypereutectic", clearance(spec.PistonToWallFor(MaterialType::HYPEREUTECTIC_ALUMINUM)));
	figure("Forged 2618", clearance(spec.PistonToWallFor(MaterialType::FORGED_2618)));
	note("Too tight and the piston scuffs or seizes when hot. Too loose and it rocks and slaps, and the rings never seal.");
	heading("Ring End Gap");
	figure("Top ring end gap", clearance(spec.ringGap));
	step("Push the ring squarely into its own bore with a piston, about a third of the way down.");
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
	heading("Checking With Plastigauge");
	text("Plastigauge is a soft thread that squashes flat between the journal and the bearing. How wide it spreads is the clearance.");
	step("Fit the upper shells in the block and lay the crankshaft in them.");
	step("Hold the Plastigauge and lay a strip across the journal, along its length.");
	step("Fit the lower shell and the main cap over it.");
	step("Torque the cap bolts to spec. Do not turn the crank while the strip is in.");
	step("Undo the bolts and take the cap off. The flattened strip stays on the journal.");
	step("Read its width against the scale on the packet: that is the oil clearance.");
	text("Rod bearings are checked the same way: the rod and its upper shell on the crank pin, a strip across the journal, then the lower "
		"shell and rod cap, torqued and removed again.");
	note("Out of spec? Undersize shells close up a ground journal; a worn journal is ground and fitted with them.");
	heading("The Crank Won't Turn");
	text("The engine only turns over with every upper main shell, the crank, every lower shell and every main cap in place.");

	begin("8. Cylinder Head and Valves");
	heading("Head");
	figure("Combustion chamber volume", std::format("{:.1f} - {:.1f} cc", spec.chamberVolume.Min(), spec.chamberVolume.Max()));
	figure("Head gasket face flatness", most(spec.maxHeadFlatness));
	figure("Head gasket thickness (compressed)", tolerance(spec.gasketThickness));
	figure("Head gasket bore", size(spec.gasketBore));
	text("Check the head face with the straightedge and feelers; surface it if warped (it raises compression). The chambers are "
		"measured with the burette and cc plate.");
	heading("Valve Guides and Stems");
	figure("Guide bore", tolerance(spec.guideBore));
	figure("Guide taper (bellmouth)", most(spec.maxGuideTaper));
	figure("Guide out of round", most(spec.maxGuideOutOfRound));
	figure("Intake stem diameter", tolerance(spec.intakeStem));
	figure("Exhaust stem diameter", tolerance(spec.exhaustStem));
	figure("Intake stem to guide", clearance(spec.intakeStemClearance));
	figure("Exhaust stem to guide", clearance(spec.exhaustStemClearance));
	text("Measure guides with the small hole gauge or small-bore gauge; stems with the micrometer. Worn guides are reamed oversize "
		"and fitted with oversize-stem valves.");
	heading("Valves and Seats");
	figure("Seat width", std::format("{} - {}", deviation(spec.seatWidth.min), deviation(spec.seatWidth.max)));
	figure("Seat runout", most(spec.maxSeatRunout));
	figure("Valve runout", most(spec.maxValveRunout));
	figure("Valve margin", tolerance(spec.valveMargin));
	figure("Valve margin minimum", deviation(spec.minValveMargin));
	figure("Overall length", tolerance(spec.valveLength));
	text("Recut worn seats with the seat cutter and reface valves on the refacer. Refacing thins the margin: below the minimum the "
		"valve burns, so replace it.");
	heading("Springs");
	figure("Free length", tolerance(spec.springFreeLength));
	figure("Seat pressure at installed height", std::format("{:.0f} - {:.0f} N", spec.springSeatPressure.Min(), spec.springSeatPressure.Max()));
	figure("Spring rate", std::format("{:.0f} N/mm", spec.springRate));
	figure("Installed height", std::format("{} - {}", size(spec.installedHeightRange.min), size(spec.installedHeightRange.max)));
	figure("Seat pressure, installed", std::format("{:.0f} - {:.0f} N", spec.seatPressure.min, spec.seatPressure.max));
	text("Test springs on the valve spring tester. With the valve, seal, spring and retainer fitted, measure the installed height with "
		"the installed height micrometer: cutting seats and refacing valves sinks the valve and raises it, which weakens the spring. "
		"Weak springs let the valves float at high rpm.");

	begin("9. Camshaft");
	heading("Specifications");
	figure("Cam bearing bore (head)", tolerance(spec.camBore));
	figure("Cam journal diameter", tolerance(spec.camJournal));
	figure("Lobe lift", tolerance(spec.lobeLift));
	figure("Cam oil clearance", clearance(spec.camClearance));
	text("Lobe lift is airflow: a worn, flat lobe costs power on that cylinder. Measure every lobe.");
	heading("Checking Cam Clearance");
	step("Fit the buckets, then lay the camshaft in the head.");
	step("Lay a Plastigauge strip across each journal.");
	step("Fit each cam cap in its own numbered place and torque its bolts.");
	step("Undo the caps, lift them off and read the strips.");

	begin("10. Assembly Sequence");
	text("The engine goes together in this order. A part can only be fitted once everything it depends on is in place and done up; the "
		"stand tells you what is missing.");
	heading("Bottom End");
	step("Block on the stand. Core plugs.");
	step("Upper main shells in the block, oiled.");
	step("Crankshaft into the shells.");
	step("Lower main shells and main caps, each to its own journal. Torque the main cap bolts.");
	step("Rear main seal over the crank flange.");
	step("Oil pump over the crank snout, then the pickup.");
	heading("Pistons and Rods, Each Cylinder");
	step("Piston down its bore, top ring gapped and fitted.");
	step("Connecting rod onto the crank pin with its upper shell.");
	step("Lower rod shell and rod cap. Torque the rod nuts.");
	heading("Top End");
	step("Head gasket, once every piston and rod is in.");
	step("Cylinder head. Torque the head bolts.");
	step("Each valve: valve, stem seal, spring, retainer and keepers, bucket.");
	step("Camshaft on the buckets, then the cam caps, torqued.");
	heading("Timing Belt");
	step("Crank sprocket (in front of the pump) and cam sprocket; belt idlers.");
	step("Timing belt round the lot with the marks lined up, then the tensioner.");
	step("Timing cover gasket and cover, front crank seal, crank pulley (its bolt to full torque).");
	heading("Closing Up and Ancillaries");
	step("Oil pan gasket and pan, once the pickup, rear seal, timing cover and rod caps are on. Drain plug and oil filter.");
	step("Valve cover gasket and cover. Spark plugs, then the coils through the cover.");
	step("Intake manifold and gasket, throttle body, injectors and fuel rail.");
	step("Exhaust manifold and gasket.");
	step("Water pump and gasket on the timing cover; thermostat and its housing.");
	note("Hold Shift while fitting a nut or bolt to run in every empty hole of that size on the part at once.");

	begin("11. Torque Specifications");
	text("Torque with a torque wrench driving the right socket. The engine's real clamping force is only as good as the wrench: a "
		"beam wrench is good to about 10%, a click wrench about 4%, a digital one about 2%.");
	heading("Fasteners");
	for (const auto& holes : PartCatalog::Holes())
	{
		const auto& info = PartCatalog::Get(holes.size);
		figure(std::format("{} ({} x{}, {} mm)", holes.name, info.fastener.thread, holes.perParent, info.fastener.headMm), torque(holes.torqueNm));
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
	heading("Dyno");
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
		case Kind::STEP:		return wrapped(line.text, BODY_SCALE, width - fontSize * 2.f) + gap;
		case Kind::BULLET:		return wrapped(line.text, BODY_SCALE, width - fontSize * 1.4f) + gap;
		case Kind::NOTE:		return wrapped(line.text, BODY_SCALE, width - fontSize * 1.6f) + fontSize * 1.2f + gap;
		case Kind::CONTENTS:	return fontSize * 1.6f;
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

int ServiceManual::SpreadCount() const
{
	return static_cast<int>((m_pages.size() + 1) / 2);
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
	drawList->AddRectFilled({ min.x, min.y + height * 0.08f }, { max.x, min.y + height * 0.16f }, ACCENT);
	centred("LV MOTOR WORKS", 1.3f, min.y + height * 0.12f - fontSize * 0.65f, GOLD);
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
			default:
				drawList->AddText(font, fontSize, { min.x, y }, INK, line.text.c_str(), nullptr, width);
				break;
		}
		y += height;
	}
	return jumped;
}

bool ServiceManual::Draw(bool imperial)
{
	if (!m_open)
		return false;
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
