#include "ServiceManual.h"

#include <format>

#include "../Controls.h"
#include "../Simulation/CarAssembly.h"
#include "../Simulation/CarSpec.h"
#include "../Simulation/EngineSpec.h"
#include "../Simulation/MeasuringTool.h"
#include "../Simulation/PartCatalog.h"
#include "../Simulation/Units.h"
#include "../Simulation/Workshop.h"
#include "../World/ToolShapes.h"

#include <map>

namespace
{
	// What each of the chassis book's figures means, shown when the reader hovers over it (as the engine's book does,
	// see ServiceManualTips.cpp). By the name the book prints; the bolt figures and gears are explained by ChassisTip.
	const std::map<std::string, const char*> CHASSIS_TIPS = {
		// General
		{ "Wheelbase", "The distance between the front and rear axles' centres. A longer wheelbase is steadier in a straight line; a shorter one turns in quicker." },
		{ "Track, front / rear", "The distance between the middles of the left and right tires on each axle. Wider is more stable in a bend." },
		{ "Length x width x height", "The car's outside size: bumper to bumper, side to side, and ground to roof." },
		{ "Curb mass", "What the car weighs ready to drive, with every fluid full and no one in it. Less mass accelerates, stops and turns better." },
		{ "Weight on the front wheels", "How much of the car's weight sits on the front axle. Near half makes a car that's neutral in a bend; more on the front pushes wide." },
		{ "Tires", "The tire size: width in mm / sidewall height as a percentage of the width, R for radial, and the rim's diameter in inches." },
		{ "Gearbox", "The manual gearbox and how many forward gears it has." },
		{ "Final drive", "The ratio of the differential's gears: turns of the driveshaft for one turn of the wheels. Higher pulls harder, lower goes faster." },
		{ "Brakes, front / rear", "What stops each axle. Ventilated discs shed heat better; drums hold the handbrake easily." },
		{ "Suspension, front / rear", "How each axle is carried: a MacPherson strut at the front, a live axle on four links and coils at the back." },
		{ "Fit the part in hand where you aim", "Carry the part to the car: where it can go is marked. Aim at the mark and press the key." },
		{ "Take a part off (its bolts out first)", "Undo and take out its nuts and bolts, and anything fitted over it, then take the part off." },
		{ "Do a nut or bolt up (spanner in hand)", "Hold the button with a spanner of the right size on it. Only a torque wrench does it up to the figure in the book." },
		{ "Undo it", "Hold the button with a spanner of the right size on a done-up nut or bolt." },
		{ "Use a car tool", "Each of the car's tools is aimed at what it works on; the prompt says what each button does there." },
		// Springs and steering
		{ "Front spring free length", "The front coil spring's length on the bench with no load on it. A spring that's sagged shorter sits the car low and soft." },
		{ "Front spring rate", "How much harder the front spring pushes for every millimetre it's squeezed. Stiffer holds the body flatter, but rides harder." },
		{ "Rear spring free length", "The rear coil spring's length with no load on it. Shorter than this, it's sagged." },
		{ "Rear spring rate", "How much harder the rear spring pushes for every millimetre it's squeezed." },
		{ "Steering wheel free play", "How far the rim of the wheel turns before the front wheels start to. More is wear in the rack or the tie rod ends." },
		// Brakes
		{ "Front rotor thickness (new)", "How thick a new front disc is, read with a micrometer across its braking faces." },
		{ "Rotor minimum thickness", "The thinnest a disc may be worn or skimmed. Thinner, it overheats and can crack: replace it." },
		{ "Rotor runout, most", "How far the disc's face may wobble as it turns, read with a dial indicator. More and the pedal pulses." },
		{ "Pad lining (new)", "How thick a new pad's friction material is, not counting its steel backing." },
		{ "Pad lining minimum", "The least lining a pad may have left. Worn past it, the backing plate scores the disc." },
		{ "Rear drum inside diameter (new)", "The diameter of a new drum's braking surface, inside." },
		{ "Drum diameter, most", "The biggest a drum may be worn or turned out to. Past it, the shoes can't reach it properly: replace it." },
		{ "Shoe lining (new)", "How thick a new brake shoe's lining is." },
		{ "Shoe lining minimum", "The least lining a shoe may have left before it's replaced." },
		{ "Pedal height from the floor", "How high the brake pedal sits at rest, from the floor under it." },
		{ "Pedal free play", "How far the pedal moves before it starts pushing the master cylinder." },
		{ "Pedal reserve, pushed hard, at least", "How far off the floor the pedal stays when it's pushed hard. Less means air in the lines or worn brakes." },
		{ "Caliper / wheel cylinder / master bore", "The pistons' sizes: the front caliper's, the rear wheel cylinder's and the master cylinder's. Their ratio sets how the braking is shared." },
		{ "Fluid", "The fluid the system takes. Use only what it says, or one it says will do." },
		{ "System capacity", "How much fluid the whole system holds from empty, lines and all." },
		// Clutch and drivetrain
		{ "Clutch disc thickness (new)", "How thick a new clutch disc is across its linings." },
		{ "Rivet depth, at least", "How far the rivets must stay below the lining's face. Less and they score the flywheel: the disc is worn out." },
		{ "Clutch pedal free play", "How far the clutch pedal moves before it starts to work the clutch. Too little and the clutch slips." },
		{ "Clutch capacity, new", "The most torque a new clutch holds before it slips. A worn one holds less." },
		{ "Reverse", "The reverse gear's ratio: turns of the engine for one turn of the gearbox's output." },
		// Electrical
		{ "Battery, resting and charged", "The battery's voltage with nothing running, fully charged." },
		{ "Battery while cranking, at least", "The least the battery's voltage may fall to while the starter turns. Lower, the battery is tired or flat." },
		{ "Charging voltage at 2000 rpm", "What the alternator holds the battery at with the engine at 2000 rpm. Lower, it isn't charging; higher, it's overcharging." },
		// Fluids
		{ "Tank capacity", "How much fuel the tank holds." },
		{ "Fuel", "The fuel the engine is built for. A higher octane will do; a lower one pings." },
		{ "Capacity", "How much the circuit holds from dry, refilled after it's drained." },
		{ "Correct level", "Where the level should be, between the marks on its dipstick, reservoir or filler." },
		{ "Use", "The fluid this circuit takes." },
		{ "Change every", "How many hours of running the fluid is good for before it's drained and replaced." },
		// Tune-up
		{ "Ignition timing at idle", "How far before top dead centre the plug fires at idle, read with a timing light on the crank pulley." },
		{ "Idle speed, warm, in neutral", "The engine's speed ticking over, warmed up, out of gear: set with the carburettor's idle speed screw." },
		{ "Idle mixture screw", "How far the mixture screw is turned out from lightly seated. Out is richer, in is leaner." },
		{ "Primary main jet", "The size of the carburettor's main jet: bigger runs richer at full throttle." },
		{ "Valve lash, intake, hot", "The gap between the intake rocker and its valve's tip with the engine hot, read with a feeler gauge." },
		{ "Valve lash, exhaust, hot", "The gap between the exhaust rocker and its valve's tip with the engine hot." },
		{ "Valve lash, intake, cold", "The intake gap with the engine cold: tighter than hot, as the valve is shorter." },
		{ "Valve lash, exhaust, cold", "The exhaust gap with the engine cold." },
		// Wheels and alignment
		{ "Size", "The tire size: width in mm / sidewall as a percentage of the width, R for radial, and the rim's diameter in inches." },
		{ "Pressure, cold", "What the tires are pumped to, measured cold (before a drive). Soft grips less and tugs the car to one side." },
		{ "Tread depth (new)", "How deep a new tire's tread grooves are." },
		{ "Tread depth, at least", "The least a tire may have: shallower, it can't clear water and must be replaced." },
		{ "Front camber", "How far the top of a front wheel leans in (negative) or out from upright, seen from the front. A little negative grips harder in a bend." },
		{ "Caster", "How far the steering's axis leans back, seen from the side. More makes the wheel come back to the middle by itself and the car steadier." },
		{ "Front toe-in", "How much closer together the front tires are at the front than at the back. A little toe-in keeps it straight; too much scrubs the tires." },
	};

	// What a figure of the chassis book means: its own explanation, or for the nuts and bolts and the gears, what they all are.
	std::string ChassisTip(const std::string& name)
	{
		if (auto it = CHASSIS_TIPS.find(name); it != CHASSIS_TIPS.end())
			return it->second;
		auto has = [&](const char* text) { return name.find(text) != std::string::npos; };
		if (name.rfind("Gear ", 0) == 0)
			return "This gear's ratio: turns of the engine for one turn of the gearbox's output. The low gears have the big numbers, and pull hardest.";
		if (name.rfind("Lug Nut", 0) == 0 || name.rfind("Lug nut", 0) == 0)
			return "The torque the wheel's nuts are done up to, in a star pattern. Loose, the wheel can come off; too tight stretches the studs and warps the disc.";
		if (has("bolt") || has("Bolt") || has("nut") || has("Nut"))
			return "The torque these are done up to with a torque wrench. Left finger tight they can come adrift on the road; done up by feel (snug), "
				"the road test notices.";
		return "";
	}
}

// The project car's book: the LE86 Chassis Service Manual. Like the engine's, every figure in it comes
// from the car's own spec (CarSpec::LE86Coupe), its build order and bolt table (CarAssembly), and the tool
// list, so the book and the car never disagree.

void ServiceManual::BuildChassis(bool imperial)
{
	const CarSpec& car = CarSpec::LE86Coupe();
	const TuneSpec& tune = car.tune;
	const BrakeSpec& brakes = car.brakes;
	auto system = imperial ? Units::System::IMPERIAL : Units::System::METRIC;
	auto deviation = [&](double mm) { return Units::FormatDeviation(mm, system); };
	// Chassis sizes are coarser than the engine's: a tenth of a millimetre, or a hundredth of an inch.
	auto length = [&](double mm) { return imperial ? std::format("{:.2f} in", mm / 25.4) : std::format("{:.1f} mm", mm); };
	auto lengths = [&](const Range& r) { return imperial ? std::format("{:.2f} - {:.2f} in", r.min / 25.4, r.max / 25.4)
		: std::format("{:.1f} - {:.1f} mm", r.min, r.max); };
	auto metres = [&](double m) { return imperial ? std::format("{:.1f} in", m / 0.0254) : std::format("{:.0f} mm", m * 1000.0); };
	auto torque = [&](double nm) { return imperial ? std::format("{:.0f} Nm ({:.0f} ft-lb)", nm, nm * 0.737562) : std::format("{:.0f} Nm", nm); };
	auto litres = [&](double l) { return imperial ? std::format("{:.2f} l ({:.1f} US qt)", l, l * 1.05669) : std::format("{:.2f} l", l); };
	auto pressure = [&](double kpa) { return imperial ? std::format("{:.0f} kPa ({:.0f} psi)", kpa, kpa * 0.145038) : std::format("{:.0f} kPa", kpa); };
	auto pressures = [&](const Range& r) { return imperial ? std::format("{:.0f} - {:.0f} kPa ({:.0f} - {:.0f} psi)", r.min, r.max, r.min * 0.145038, r.max * 0.145038)
		: std::format("{:.0f} - {:.0f} kPa", r.min, r.max); };
	auto mass = [&](double kg) { return imperial ? std::format("{:.0f} kg ({:.0f} lb)", kg, kg * 2.20462) : std::format("{:.0f} kg", kg); };
	auto band = [](const Range& r, const char* unit, int places = 0) { return std::vformat(std::format("{{:.{}f}} - {{:.{}f}} {}", places, places, unit),
		std::make_format_args(r.min, r.max)); };

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
	auto bullet = [&](const std::string& text) { chapter->lines.push_back({ Kind::BULLET, text }); };
	auto step = [&](const std::string& text, const std::vector<PartType>& parts = {}) {
		Line line = { Kind::STEP, text, std::to_string(++stepNumber) };
		for (PartType part : parts)
			line.views.push_back({ PartTypeName(part), PartTypeName(part) });
		chapter->lines.push_back(std::move(line));
	};
	auto item = [&](const std::string& name, const std::string& description, const std::string& model,
		std::vector<std::pair<std::string, std::string>> specs) {
		Line line = { Kind::ITEM, name, description };
		line.views.push_back({ model, "" });
		line.specs = std::move(specs);
		chapter->lines.push_back(std::move(line));
	};
	auto figure = [&](const std::string& name, const std::string& value, const std::string& tip = "") {
		Line line = { Kind::SPEC, name, value };
		line.tip = !tip.empty() ? tip : ChassisTip(name);
		chapter->lines.push_back(std::move(line));
	};
	auto note = [&](const std::string& text) { chapter->lines.push_back({ Kind::NOTE, text }); };
	auto illustrate = [&](Figure figure, const std::string& caption) {
		chapter->lines.push_back({ Kind::FIGURE, std::format("Fig. {}.{}  {}", m_chapters.size() - 1, ++figureNumber, caption), std::to_string(static_cast<int>(figure)) });
	};
	// The torque for a set of the car's holes, by its name in the bolt table.
	auto holeTorque = [&](CarSlot slot) { return torque(CarAssembly::Holes(slot)->torqueNm); };
	using Controls::NameOf;

	begin("Contents");

	begin("1. General Information");
	heading(std::format("The {} {}", car.code, car.name));
	text(std::format("The {} is a {} {}: a steel monocoque with the engine in front, driving the rear wheels through a five-speed "
		"gearbox, a one-piece driveshaft and a live rear axle on four links and coil springs. MacPherson struts carry the front, with disc "
		"brakes; the rear has drums. This book covers everything round the engine. The engine itself, an LV-16 off the shop's stand, has "
		"its own manual.", car.year, car.code, car.name));
	heading("Vehicle Data");
	figure("Wheelbase", metres(car.wheelbase));
	figure("Track, front / rear", metres(car.frontTrack) + " / " + metres(car.rearTrack));
	figure("Length x width x height", imperial ? std::format("{:.0f} x {:.0f} x {:.0f} in", car.length / 0.0254, car.width / 0.0254, car.height / 0.0254)
		: std::format("{:.0f} x {:.0f} x {:.0f} mm", car.length * 1000.0, car.width * 1000.0, car.height * 1000.0));
	figure("Curb mass", mass(car.curbMass));
	figure("Weight on the front wheels", std::format("{:.0f}%", car.frontWeightShare * 100.0));
	figure("Tires", car.tireSize);
	figure("Gearbox", std::format("MK50 five-speed, {} forward", car.gearRatios.size()));
	figure("Final drive", std::format("{:.3f} : 1", car.finalDriveNominal));
	figure("Brakes, front / rear", "Ventilated disc / drum");
	figure("Suspension, front / rear", "MacPherson strut / four-link live axle");
	heading("Using This Manual");
	text(std::string("Figures are shown in ") + (imperial ? "inches, pounds and psi beside the factory's metric figures" : "metric units") +
		", as set in Settings. Torques are in newton metres. Left and right are as you sit in the car.");
	text("The car is built up like the engine: a part only goes on once what it fits to is on, and only counts as on once its own nuts "
		"and bolts are in and done up. Chapter 3 gives the whole order; each system's chapter gives the detail.");
	note("A car that leaves the shop with a bolt finger tight, a circuit full of air or the wrong fluid in it shows it on the road test, "
		"and a buyer pays for what the road test finds.");
	heading("Shop Controls at the Car");
	figure("Fit the part in hand where you aim", NameOf(Control::USE));
	figure("Take a part off (its bolts out first)", NameOf(Control::USE));
	figure("Do a nut or bolt up (spanner in hand)", NameOf(Control::PRIMARY) + " (hold)");
	figure("Undo it", NameOf(Control::SET_DOWN) + " (hold)");
	figure("Use a car tool", std::format("{} / {}", NameOf(Control::PRIMARY), NameOf(Control::SET_DOWN)));

	begin("2. Tools and Equipment");
	heading("Car Tools");
	text("The car needs a few tools the engine doesn't. Each is held and aimed at the part it works on; what it can reach is marked.");
	for (const auto& tool : ToolCatalog::All())
	{
		switch (tool.handKind)
		{
			case HandToolKind::FUNNEL:
			case HandToolKind::BRAKE_BLEEDER:
			case HandToolKind::SPRING_COMPRESSOR:
			case HandToolKind::TIRE_IRONS:
			case HandToolKind::TIMING_LIGHT:
			case HandToolKind::TIRE_GAUGE:
			case HandToolKind::ALIGNMENT_GAUGE:
			case HandToolKind::SCREWDRIVER:
			case HandToolKind::FLOOR_JACK:
			case HandToolKind::JACK_STAND:
				item(tool.name, tool.description, ToolModelName(tool), { { "Price", std::format("${:.0f}", tool.price) } });
				break;
			default:
				break;
		}
	}
	heading("Spanners");
	text("The chassis is held together with M6 to M12 nuts and bolts and the wheels' lug nuts: the same sockets, wrenches and torque "
		"wrenches as the engine. The rear link and knuckle bolts take well over 100 Nm, so a torque wrench that reaches that far is worth having.");
	heading("Engine Hoist");
	text("The engine goes into the bay, and comes out of it, on the engine hoist. It rolls out of the shop's door and down the drive to the car's nose.");

	begin("3. Building the Car");
	heading("The Car");
	text("The shop's LE86 is out on the drive in front of the door: complete, and it runs, but it's done its miles. Its engine is tired (the dyno "
		"and a road test say where), and the car has worn parts of its own. Put it right a job at a time: measure and check, and buy new where "
		"it's worn. Parts and fluids are bought at the parts computer. The Car tab road tests it after each job.");
	illustrate(Figure::CAR_BUILD_FLOW, "The build, from the bare shell to the road. Each stage's chapter has the detail.");
	heading("Fitting a Part");
	step(std::format("Pick the part up and walk to the car. The places it can go are marked: a filled marker is ready for it, a hollow one "
		"is waiting on something that goes on first."));
	step(std::format("Aim at the place and press {}. If something has to go on first, the prompt says what.", NameOf(Control::USE)));
	step("Put its nuts and bolts in the same way, then do each one up to its torque (chapter 14) with a spanner of its size.");
	note("Parts that come in pairs or fours are fitted to each side or corner separately: the front left strut and the front right strut "
		"are two jobs.");
	heading("Taking a Part Off");
	step(std::format("Undo its nuts and bolts (hold {} with the spanner on them) and take them out.", NameOf(Control::SET_DOWN)));
	step(std::format("Take the part off with {}. Anything fitted over it has to come off first; the prompt says what.", NameOf(Control::USE)));
	step("A strut comes off with its spring still on it, and a wheel with its tire.");
	heading("The Car Tab");
	text("The Car tab at the parts computer shows the whole car: what's on, what's missing and what's holding each part up (Build), every "
		"circuit's level and condition (Fluids), the settings (Tune) and the road test (Road Test).");

	begin("4. Body and Interior");
	heading("Panels and Glass");
	text("The front fenders and doors bolt to the shell; the hood needs both fenders on, and the front bumper the same. The trunk lid, "
		"rear bumper and glass go straight onto the shell.");
	step("Fenders, left and right.", { PartType::FRONT_FENDER });
	step("Doors.", { PartType::DOOR });
	step("Hood, trunk lid and bumpers.", { PartType::HOOD, PartType::TRUNK_LID, PartType::FRONT_BUMPER, PartType::REAR_BUMPER });
	step("Windshield and rear window.", { PartType::WINDSHIELD, PartType::REAR_WINDOW });
	step("Headlights and tail lights, once the main harness is in to plug them into.", { PartType::HEADLIGHT, PartType::TAIL_LIGHT });
	figure("Fender bolts", holeTorque(CarSlot::FENDER_BOLT));
	figure("Door hinge bolts", holeTorque(CarSlot::DOOR_HINGE_BOLT));
	figure("Hood hinge bolts", holeTorque(CarSlot::HOOD_HINGE_BOLT));
	figure("Front bumper bolts", holeTorque(CarSlot::BUMPER_BOLT));
	heading("Interior");
	text("The dashboard needs the main harness and fuse box behind it, and the instrument cluster goes in the dashboard. The seats bolt to "
		"the floor over the harness. The steering wheel goes on last, on the column (chapter 5).");
	step("Dashboard, then the instrument cluster.", { PartType::DASHBOARD, PartType::INSTRUMENT_CLUSTER });
	step("Seats.", { PartType::SEAT });
	figure("Seat bolts", holeTorque(CarSlot::SEAT_BOLT));

	begin("5. Front Suspension and Steering");
	heading("Springs and Struts");
	figure("Front spring free length", lengths(car.frontSpringFreeLength));
	figure("Front spring rate", std::format("{:.0f} N/mm", car.frontSpringRate));
	text("The spring goes onto its strut off the car, compressed with the spring compressor. Never take a spring off a strut without one: "
		"it's holding several hundred kilos.");
	step("Set the strut down on the bench or floor, and the spring by it.", { PartType::FRONT_STRUT, PartType::FRONT_COIL_SPRING });
	step(std::format("Hold the spring compressor, aim at the strut and hold {} to compress the spring onto it. {} (held) takes it back off.",
		NameOf(Control::PRIMARY), NameOf(Control::SET_DOWN)));
	step("Fit the strut, spring and all, up into its tower, and its three mount nuts on top under the hood.");
	figure("Strut mount nuts", holeTorque(CarSlot::STRUT_MOUNT_NUT));
	heading("Arms, Knuckles and Sway Bar");
	step("Lower control arm, to the crossmember.", { PartType::LOWER_CONTROL_ARM });
	step("Strut rod, from the arm forward to the body: its nuts set the caster (chapter 13).", { PartType::STRUT_ROD });
	step("Steering knuckle, onto the strut and the arm's ball joint.", { PartType::STEERING_KNUCKLE });
	step("Sway bar across both arms, once both are on.", { PartType::FRONT_SWAY_BAR });
	figure("Strut to knuckle bolts", holeTorque(CarSlot::STRUT_KNUCKLE_BOLT));
	figure("Ball joint bolts", holeTorque(CarSlot::BALL_JOINT_BOLT));
	figure("Control arm bolt", holeTorque(CarSlot::CONTROL_ARM_BOLT));
	figure("Strut rod nuts", holeTorque(CarSlot::STRUT_ROD_NUT));
	figure("Sway bar bolts", holeTorque(CarSlot::SWAY_BAR_BOLT));
	heading("Steering");
	figure("Steering wheel free play", lengths(tune.steeringWheelPlay) + " at the rim");
	step("Steering rack, across the crossmember.", { PartType::STEERING_RACK });
	step("Tie rod ends, from the rack to each knuckle: they set the toe.", { PartType::TIE_ROD_END });
	step("Steering column, through the firewall to the rack, once the dashboard is in; then the wheel.", { PartType::STEERING_COLUMN, PartType::STEERING_WHEEL });
	figure("Steering rack bolts", holeTorque(CarSlot::STEERING_RACK_BOLT));
	figure("Tie rod end nuts", holeTorque(CarSlot::TIE_ROD_END_NUT));

	begin("6. Rear Suspension and Axle");
	heading("The Axle");
	text("The rear axle is a live axle: the differential in its middle, located by two lower and two upper trailing links and a lateral "
		"rod across the car, sprung on coils with a shock beside each.");
	figure("Rear spring free length", lengths(car.rearSpringFreeLength));
	figure("Rear spring rate", std::format("{:.0f} N/mm", car.rearSpringRate));
	step("Axle housing, under the boot floor.", { PartType::REAR_AXLE });
	step("Differential, into the axle's carrier.", { PartType::DIFFERENTIAL });
	step("Lower and upper trailing links, and the lateral rod.", { PartType::LOWER_TRAILING_LINK, PartType::UPPER_TRAILING_LINK, PartType::LATERAL_ROD });
	step("Coil springs on the lower links, and the shocks.", { PartType::REAR_COIL_SPRING, PartType::REAR_SHOCK });
	figure("Differential carrier nuts", holeTorque(CarSlot::DIFFERENTIAL_NUT));
	figure("Lower link bolts", holeTorque(CarSlot::REAR_LINK_BOLT));
	figure("Upper link bolts", holeTorque(CarSlot::UPPER_LINK_BOLT));
	figure("Lateral rod bolts", holeTorque(CarSlot::LATERAL_ROD_BOLT));
	figure("Shock bolts", holeTorque(CarSlot::REAR_SHOCK_BOLT));
	heading("Final Drive Ratios");
	text("The differential sets the final drive. A shorter (higher number) ratio pulls harder in every gear and costs top speed and "
		"fuel. The store sells these:");
	for (double ratio : car.finalDriveOptions)
		bullet(std::format("{:.3f} : 1{}", ratio, ratio == car.finalDriveNominal ? " (factory)" : ""));

	begin("7. Brakes");
	heading("Specifications");
	figure("Front rotor thickness (new)", length(brakes.rotorThickness.Min()) + " - " + length(brakes.rotorThickness.Max()));
	figure("Rotor minimum thickness", length(brakes.minRotorThickness));
	figure("Rotor runout, most", deviation(brakes.maxRotorRunout));
	figure("Pad lining (new)", length(brakes.padThickness.Min()) + " - " + length(brakes.padThickness.Max()));
	figure("Pad lining minimum", length(brakes.minPadThickness));
	figure("Rear drum inside diameter (new)", length(brakes.drumDiameter.Min()) + " - " + length(brakes.drumDiameter.Max()));
	figure("Drum diameter, most", length(brakes.maxDrumDiameter));
	figure("Shoe lining (new)", length(brakes.shoeLining.Min()) + " - " + length(brakes.shoeLining.Max()));
	figure("Shoe lining minimum", length(brakes.minShoeLining));
	figure("Pedal height from the floor", lengths(brakes.pedalHeight));
	figure("Pedal free play", lengths(brakes.pedalFreePlay));
	figure("Pedal reserve, pushed hard, at least", length(brakes.minPedalReserve));
	figure("Caliper / wheel cylinder / master bore", std::format("{:.1f} / {:.2f} / {:.2f} mm", brakes.frontPistonMm, brakes.rearPistonMm, brakes.masterBoreMm));
	heading("Building the Brakes");
	step("Brake booster on the firewall, driver's side, and the master cylinder on the booster.", { PartType::BRAKE_BOOSTER, PartType::BRAKE_MASTER_CYLINDER });
	step("Brake line set along the floor, from the master cylinder.", { PartType::BRAKE_LINE_SET });
	step("Flexible hoses: one to each front caliper, one to the rear axle.", { PartType::BRAKE_HOSE });
	step("Front: rotor on the knuckle's hub, pads, then the caliper over them.", { PartType::BRAKE_ROTOR, PartType::BRAKE_PAD_SET, PartType::BRAKE_CALIPER });
	step("Rear: wheel cylinder on the backing plate, shoes, then the drum over them.", { PartType::WHEEL_CYLINDER, PartType::BRAKE_SHOE_SET, PartType::BRAKE_DRUM });
	step("Parking brake cable, once both rear shoe sets are in.", { PartType::PARKING_BRAKE_CABLE });
	text("Fitting the line set doesn't connect it. Its five pipes come off a junction block on the driver's wheel housing, under the master "
		"cylinder: with the hood up and nothing in hand, click on the block (it's marked with a dot) for a list of them, and take one by its "
		"number. Carry its union to what it goes on and screw it in with the left button: the master cylinder's front and rear ports, a "
		"bracket on each front wheel housing where the hose comes through, and the front end of the rear axle's hose under the floor. Each "
		"pipe only fits its own place, and can be clipped along the way like a wire (the use key), anywhere on the car. Undo a union and "
		"that line lets air in and loses fluid: the brakes won't hold fluid, and the car won't be taken out, till all five are done up.");
	figure("Brake booster nuts", holeTorque(CarSlot::BOOSTER_NUT));
	figure("Master cylinder nuts", holeTorque(CarSlot::MASTER_CYLINDER_NUT));
	figure("Caliper bolts", holeTorque(CarSlot::CALIPER_BOLT));
	heading("Filling and Bleeding");
	figure("Fluid", car.Fluid(FluidCircuit::BRAKES).grade);
	figure("System capacity", litres(car.Fluid(FluidCircuit::BRAKES).capacity));
	text("Every line is full of air when the brakes are built, and opening any part of the system lets air back in. Air compresses: "
		"the pedal goes soft, or to the floor.");
	illustrate(Figure::BRAKE_BLEED_ORDER, "The bleed order: the longest line first.");
	step("Fill the reservoir to MAX with the funnel.");
	step(std::format("Hold the brake bleeder kit and aim at the rear right wheel's bleed nipple. Each {} pumps the pedal and cracks the "
		"nipple, letting some air out and some fluid with it.", NameOf(Control::PRIMARY)));
	step("Keep going until no bubbles come. Watch the reservoir: let it fall below MIN and air is drawn in at the top, and the job starts again.");
	step("Rear left, front right, front left, topping up between them.");
	note("DOT 4 does for DOT 3. DOT 5 silicone does not: it ruins the seals of a system built for DOT 3 the moment it runs.");

	begin("8. Clutch and Drivetrain");
	heading("Specifications");
	figure("Clutch disc thickness (new)", length(car.clutchDiscThickness.Min()) + " - " + length(car.clutchDiscThickness.Max()));
	figure("Rivet depth, at least", length(car.minClutchRivetDepth));
	figure("Clutch pedal free play", lengths(car.clutchPedalFreePlay));
	figure("Clutch capacity, new", torque(car.clutchCapacityNm));
	for (size_t gear = 0; gear < car.gearRatios.size(); gear++)
		figure(std::format("Gear {}", gear + 1), std::format("{:.3f} : 1", car.gearRatios[gear]));
	figure("Reverse", std::format("{:.3f} : 1", car.reverseRatio));
	illustrate(Figure::GEAR_SPEEDS, std::format("Road speed in each gear with the {:.3f} final drive and {} tires.", car.finalDriveNominal, car.tireSize));
	heading("Clutch and Gearbox");
	text("The clutch goes on the engine's flywheel once the engine is in the car (chapter 9), and the gearbox bolts up behind it. The "
		"flywheel has to be on first: fitted on the hoist, or in the car before the clutch goes on.");
	step("Clutch disc against the flywheel, the long side of its hub toward the gearbox.", { PartType::CLUTCH_DISC });
	step("Pressure plate over it, its bolts done up evenly.", { PartType::PRESSURE_PLATE });
	step("Release bearing, then the gearbox, onto the bellhousing bolts.", { PartType::RELEASE_BEARING, PartType::TRANSMISSION });
	step("Transmission mount under its tail, and the driveshaft from the gearbox to the differential.", { PartType::TRANSMISSION_MOUNT, PartType::DRIVESHAFT });
	step("Clutch master cylinder on the firewall, and the release cylinder on the bellhousing.",
		{ PartType::CLUTCH_MASTER_CYLINDER, PartType::CLUTCH_RELEASE_CYLINDER });
	step("The pipe hanging off the master cylinder: take it (left button, hood up) and screw its union into the top of the release "
		"cylinder. Then fill and bleed them (chapter 11). Undone, it lets air in.");
	figure("Pressure plate bolts", holeTorque(CarSlot::PRESSURE_PLATE_BOLT));
	figure("Bellhousing bolts", holeTorque(CarSlot::BELLHOUSING_BOLT));
	figure("Transmission mount bolts", holeTorque(CarSlot::TRANSMISSION_MOUNT_BOLT));
	figure("Driveshaft flange nuts", holeTorque(CarSlot::DRIVESHAFT_FLANGE_NUT));

	begin("9. The Engine in the Car");
	heading("Putting It In");
	text("Any finished engine from the shop's stands goes in. Build and check it with its own manual first: once it's in, only the top "
		"of it can be got at.");
	step("Engine mounts on the crossmember, left and right.", { PartType::ENGINE_MOUNT });
	step("Lift the engine off its stand with the engine hoist. Its flywheel goes on there, on the hoist (or in the bay, before the clutch).");
	step("Roll the hoist out to the car and over the bay, and lower the engine onto its mounts.");
	step("Its two mount nuts, then the clutch and gearbox (chapter 8), the starter, the radiator hoses, and the harness's plugs on the engine (see Electrical).");
	figure("Engine mount bolts (mount to body)", holeTorque(CarSlot::ENGINE_MOUNT_BOLT));
	figure("Engine mount nuts (engine to mount)", holeTorque(CarSlot::ENGINE_MOUNT_NUT));
	heading("Service in the Car");
	text("With the engine in, these can come off and go back on where it sits:");
	bullet("The valve cover, its gasket and bolts (for the valve lash)");
	bullet("Spark plugs and plug leads");
	bullet("The carburettor and its bolts");
	bullet("The oil filter");
	bullet("The flywheel, with the clutch and gearbox off");
	text("Anything else needs the engine out.");
	heading("Taking It Out");
	step("Take off what's behind and round it: the gearbox and clutch, the starter, the harness's plugs on it, the radiator hoses and the exhaust's "
		"downpipe, and its mount nuts.");
	step("Roll the hoist over the bay and lift it out. Its oil and coolant are drained first.");

	begin("10. Fuel, Electrical, Cooling and Exhaust");
	heading("Fuel");
	step("Fuel tank under the boot floor, and its sender.", { PartType::FUEL_TANK, PartType::FUEL_SENDER });
	step("Fuel line set forward along the floor, and the filter on the firewall, in the back corner of the bay on the passenger's side.",
		{ PartType::FUEL_LINE_SET, PartType::FUEL_FILTER });
	text("The line's three hoses are pushed on by hand, like the harness's plugs: click on the end of the line where it comes up through "
		"the wheel housing beside the filter (marked with a dot, hood up) for a list of them. One goes on the tank's outlet, one up into "
		"the bottom of the filter, and the third from the top of the filter to the pump on the side of the engine's head. With any of the "
		"three off, the engine gets no fuel; and the engine can't be lifted out with the pump's hose on it.");
	figure("Fuel tank bolts", holeTorque(CarSlot::FUEL_TANK_BOLT));
	figure("Tank capacity", litres(car.Fluid(FluidCircuit::FUEL).capacity));
	figure("Fuel", car.Fluid(FluidCircuit::FUEL).grade);
	heading("Electrical");
	figure("Battery, resting and charged", band(car.batteryVoltage, "V", 1));
	figure("Battery while cranking, at least", std::format("{:.1f} V", car.minCrankingVoltage));
	figure("Charging voltage at 2000 rpm", band(car.chargingVoltage, "V", 1));
	step("Main harness through the body, and the fuse box on it under the dash. It's the car's only loom: a branch of it comes through the "
		"firewall to the engine.", { PartType::MAIN_HARNESS, PartType::FUSE_BOX });
	step("Battery in the engine bay.", { PartType::BATTERY });
	step("Ignition coil on the inner wing.", { PartType::IGNITION_COIL });
	step("Starter, on the bellhousing once the gearbox is on.", { PartType::STARTER });
	text("The battery has to be grounded: a strap from its negative post lies loose by it till it's bolted to the body. Take the strap (left button), aim at "
		"bare bodywork anywhere it reaches (the inner wing, the firewall, a chassis rail) and click again: a stud goes in there to hold it. It has to be "
		"the body itself, not a part bolted to it. Till it's on, nothing electrical works.");
	text("Fitting the harness doesn't connect it. Its plugs are tucked away in it where it comes through the firewall, in the bay's back corner "
		"on the driver's side: with the hood up and nothing in hand, click on it there (it's marked with a dot) for a list of its plugs, and take one by its number, or with the wheel and a click. "
		"Carry it to what it feeds and push it onto its socket (left button again; the right lets it go, and it goes back in the loom). "
		"A plug that's on something can be pulled straight off it, or picked from the same list. A plug only fits its own socket. It has ten: the battery's positive post, each headlight and the cooling fan "
		"at the front, and on its engine branch the starter, the ignition coil, the distributor, the alternator, the coolant temperature sender "
		"in the water outlet and the oil pressure sender in the block by the filter. Take off a part and its plug comes loose again. The "
		"engine can't be lifted out with the distributor's, the alternator's or either sender's plug on it, and the harness itself only comes out "
		"once every one of its plugs is pulled. (The spark plugs' leads are parts of "
		"their own, from the distributor's cap to each plug: see the engine's manual.)");
	text("To keep the wires tidy, clip them as you go. With a plug in hand, aim at somewhere under the hood and press the use key: the wire "
		"is clipped there, and runs on from that clip. Each wire takes up to six clips, in the order they're put on, so a wire can be led "
		"along the firewall or an inner wing and bent round whatever is in its way. A clip uses up wire, and a wire only has so much. With "
		"nothing in hand, the right button on a clip takes it off. A wire that comes off, pulled or let go, comes out of its clips with it and goes back "
		"into the loom. The list's last line pulls every plug off at once (or Backspace, with the list open).");
	figure("Starter bolts", holeTorque(CarSlot::STARTER_BOLT));
	heading("Cooling");
	step("Radiator across the nose, with its pressure cap and its overflow bottle, and the cooling fan behind it (it needs the harness to run).", { PartType::RADIATOR, PartType::COOLING_FAN });
	step("Upper and lower radiator hoses, both on the right: the upper from the radiator's top outlet to the thermostat housing, the lower from "
		"its bottom outlet up to the water pump.", { PartType::RADIATOR_HOSE });
	heading("Exhaust");
	step("Catalytic converter onto the engine's downpipe, then the muffler behind it.", { PartType::CATALYTIC_CONVERTER, PartType::MUFFLER });
	figure("Catalytic converter nuts", holeTorque(CarSlot::CATALYTIC_CONVERTER_NUT));

	begin("11. Fluids and Capacities");
	heading("Capacities");
	text("A circuit only holds fluid once its parts are on and closed up. Poured into one that isn't, it runs out onto the floor.");
	for (const auto& fluid : car.fluids)
	{
		heading(FluidCircuitName(fluid.circuit));
		figure("Capacity", litres(fluid.capacity));
		figure("Correct level", std::format("{:.2f} - {:.2f} l", fluid.level.min, fluid.level.max));
		figure("Use", fluid.grade);
		if (fluid.changeHours > 0.0)
			figure("Change every", std::format("{:.0f} hours of running", fluid.changeHours));
		text(fluid.check);
	}
	heading("Filling");
	step(std::format("Hold the fluid funnel and aim at the car: each filler is marked with how much is in it. {} (held) pours; {} picks "
		"another fluid from what's in stock; {} (held) drains the circuit into the drain pan.", NameOf(Control::PRIMARY), NameOf(Control::ROTATE),
		NameOf(Control::SET_DOWN)));
	step("Brakes and clutch: bleed them after filling (chapter 7).");
	step("Coolant: burp the air out. With the radiator cap off, run the engine till it's warm (the Fluids page of the Car tab: Run it with "
		"the radiator cap off), then top it up.");
	note("A heavier oil or a higher octane will do. GL-5 hypoid oil in the gearbox eats its brass synchro rings, plain water in the "
		"cooling system rusts and boils, and diesel in the tank stops it dead.");

	begin("12. Tune-up");
	heading("Specifications");
	figure("Ignition timing at idle", std::format("{:.0f} - {:.0f} deg BTDC", tune.ignitionTiming.min, tune.ignitionTiming.max));
	figure("Idle speed, warm, in neutral", band(tune.idleSpeed, "rpm"));
	figure("Idle mixture screw", band(tune.idleMixture, "turns out", 1));
	figure("Primary main jet", std::format("#{:.0f} (#{:.0f} - #{:.0f})", tune.mainJetNominal, tune.mainJet.min, tune.mainJet.max));
	figure("Valve lash, intake, hot", deviation(tune.intakeLash.min) + " - " + deviation(tune.intakeLash.max));
	figure("Valve lash, exhaust, hot", deviation(tune.exhaustLash.min) + " - " + deviation(tune.exhaustLash.max));
	figure("Valve lash, intake, cold", deviation(tune.intakeLash.min - Workshop::LASH_HOT_GROWTH) + " - " + deviation(tune.intakeLash.max - Workshop::LASH_HOT_GROWTH));
	figure("Valve lash, exhaust, cold", deviation(tune.exhaustLash.min - Workshop::LASH_HOT_GROWTH) + " - " + deviation(tune.exhaustLash.max - Workshop::LASH_HOT_GROWTH));
	text(std::format("The lash closes up {} as the engine cools: the valves are shorter in the head. Set it cold (the engine's sat a "
		"good while) to the cold figure, or just after a drive to the hot one.", deviation(Workshop::LASH_HOT_GROWTH)));
	heading("Ignition Timing");
	step("Warm the engine up and leave it idling.");
	step(std::format("Hold the timing light and aim at the crank pulley: it strobes the timing marks and reads the advance. {} advances "
		"it (turns the distributor body), {} retards it.", NameOf(Control::PRIMARY), NameOf(Control::SET_DOWN)));
	text("Too much advance for the fuel and it pings, then detonates under load; too little and it's sluggish and runs hot. A higher "
		"octane fuel takes a little more advance.");
	heading("Idle Speed and Mixture");
	step("With the engine warm and idling, hold the flat screwdriver and aim at the carburettor.");
	step("Set the mixture screw for the smoothest idle: in is leaner, out is richer.");
	step("Then set the idle speed screw to the spec on the tach.");
	heading("Main Jet");
	text("The main jet sets the mixture at full throttle. It's changed on the Tune page of the Car tab. Bigger is richer: too small "
		"and it runs lean and hot, which can burn a piston; too big and it smokes and fouls its plugs.");
	heading("Valve Lash");
	step("Take the valve cover off (its bolts first). The engine can stay in the car.");
	step("Hold the flat screwdriver and aim at a rocker. With a feeler gauge in the shop, the gap is read off.");
	step(std::format("{} turns the adjuster in (tighter), {} out (looser). The intake rockers are set alike, and the exhausts alike.",
		NameOf(Control::PRIMARY), NameOf(Control::SET_DOWN)));
	step("A new valve cover gasket, and the cover back on.");
	note("Tight lash holds the valves off their seats when it's hot and burns them. Loose lash clatters and wears the cam.");

	begin("13. Wheels, Tires and Alignment");
	heading("Tires");
	figure("Size", car.tireSize);
	figure("Pressure, cold", pressures(tune.tirePressure));
	figure("Tread depth (new)", length(car.treadDepth.Min()) + " - " + length(car.treadDepth.Max()));
	figure("Tread depth, at least", length(car.minTreadDepth));
	step("Set the wheel down, and the tire by it.", { PartType::WHEEL, PartType::TIRE });
	step(std::format("Hold the tire irons, aim at the wheel and hold {} to mount the tire on it. {} (held) takes it back off.",
		NameOf(Control::PRIMARY), NameOf(Control::SET_DOWN)));
	step("Fit the wheel at its corner, over its brake, and its four lug nuts.");
	step(std::format("Check every tire with the tire gauge: {} (held) pumps it up, {} lets some out. Set them to {} cold, and the two "
		"fronts the same.", NameOf(Control::PRIMARY), NameOf(Control::SET_DOWN), pressure(tune.tirePressureNominal)));
	figure("Lug nuts", holeTorque(CarSlot::LUG_NUT));
	note("The car comes off its jack stands once all four wheels are on.");
	heading("Alignment");
	figure("Front camber", std::format("{:.2f} to {:.2f} deg", tune.frontCamber.min, tune.frontCamber.max));
	figure("Caster", std::format("{:.2f} to {:.2f} deg", tune.caster.min, tune.caster.max));
	figure("Front toe-in", band(tune.frontToe, "mm", 1));
	illustrate(Figure::ALIGNMENT_ANGLES, "The front wheel's three angles, drawn larger than they are.");
	text("Camber isn't adjustable on this car: it's set by the parts, and a bent knuckle or strut shows up as camber out of spec. "
		"Caster is set by the strut rods' nuts (the Tune page of the Car tab). Toe is set at the tie rod ends.");
	step(std::format("Hold the camber and toe gauge and aim at a front wheel: it reads that wheel's camber and caster, and the toe across "
		"both. {} turns the tie rods for more toe in, {} for less.", NameOf(Control::PRIMARY), NameOf(Control::SET_DOWN)));
	text("Too much toe in or out scrubs the tires and makes it wander; too little caster makes the steering light and nervous.");

	begin("14. Torque Specifications");
	text("Every chassis nut and bolt, its size and its torque. A fastener left snug (done up with no torque wrench) is noticed on the road "
		"test; one left finger tight can come adrift.");
	for (const auto& holes : CarAssembly::Holes())
		figure(std::format("{} ({})", holes.name, PartTypeName(holes.size)), torque(holes.torqueNm));

	begin("15. Road Test and Troubleshooting");
	heading("The Road Test");
	text("The Road Test page of the Car tab takes the finished car out: round the block, down the strip, a stop from 100 km/h and a lap "
		"of the skidpad. It reports what it did and everything the driver noticed.");
	step("It has to crank, start, drive, steer and stop. A car that can't is towed back.");
	step("Read the findings, put them right, and drive it again.");
	step("Sell it from the same page. The price comes from what's on it, its engine and how well it drove.");
	auto problem = [&](const std::string& symptom, const std::string& causes) {
		heading(symptom);
		text(causes);
	};
	problem("Turning the key does nothing (not even the starter)", "The battery, main harness, fuse box or starter isn't on, the harness's lead is off the "
		"battery's positive post, the starter's plug is loose, the battery's ground strap isn't on the body, or the battery is flat.");
	problem("A gauge, a headlight or the fan does nothing", "Its plug isn't pushed onto it: see Electrical.");
	problem("It cranks but gets no fuel, with fuel in the tank", "One of the fuel line's three hoses is off: the tank's, the filter's or the pump's.");
	problem("The brakes or the clutch won't take fluid", "A pipe's union is undone: the funnel says which.");
	problem("It cranks but won't fire", "The engine isn't finished, the tank is dry or holds the wrong fuel, the fuel line or filter is "
		"missing, the ignition coil isn't on, the coil's or the distributor's plug is off, or the timing is miles out.");
	problem("The brake pedal goes to the floor", "The brakes have never been filled and bled, or the reservoir ran dry while bleeding and "
		"drew air in. Spongy: some air is still in the lines.");
	problem("It pulls to one side", "The front tire pressures don't match, or a caliper is sticking.");
	problem("It runs hot", "The coolant is low or full of air (burp it), the fan isn't on, or the timing is retarded.");
	problem("Pinging under load", "Too much ignition advance for the fuel's octane, or a lean main jet.");
	problem("It stalls at idle, or idles too fast", "The idle speed screw. Hunting and dying: the mixture is far too lean; loping and "
		"smelling of fuel: too rich.");
	problem("Clatter from the top of the engine", "Valve lash too loose.");
	problem("The gearbox or diff whines", "It's low on oil. Run dry, it's destroyed on the test.");
	problem("The clutch slips, or grinds into gear", "A worn disc slips; a pedal to the floor and grinding means the clutch hydraulics need "
		"filling and bleeding.");
	problem("Clunks on and off the throttle", "A split engine mount.");
	problem("Something came adrift", "A nut or bolt left finger tight. Check every one against chapter 14.");

	// The contents: every chapter but itself.
	for (size_t i = 1; i < m_chapters.size(); i++)
		m_chapters[0].lines.push_back({ Kind::CONTENTS, m_chapters[i].title, std::to_string(i) });

	m_built = true;
	m_builtImperial = imperial;
	m_builtBook = Book::CHASSIS;
	m_pagedWidth = 0.f;
}
