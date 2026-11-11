#include "ServiceManual.h"

#include <format>

#include "../Controls.h"
#include "../Simulation/CarAssembly.h"
#include "../Simulation/CarSpec.h"
#include "../Simulation/EngineSpec.h"
#include "../Simulation/MeasuringTool.h"
#include "../Simulation/PartCatalog.h"
#include "../Simulation/Units.h"
#include "../World/ToolShapes.h"

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
		line.tip = tip;
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
	text("The engine goes into the bay, and comes out of it, on the engine hoist. It rolls out of the shop, down the drive and along the track into the salvage yard, and in at the gate to the car's nose.");

	begin("3. Building the Car");
	heading("The Project");
	text(std::format("The car is bought from the parts computer's store (the Project Car page) as a bare shell on jack stands in the salvage yard beside the shop, "
		"its parts strewn round it and its nuts and bolts in a box by the gate. Whatever's in the lot is used: measure and check it, and buy new where it's worn. Fluids are bought on the "
		"same page. A finished car is road tested and sold from the Car tab."));
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
	text("The clutch goes on the engine's flywheel once the engine is in the car (chapter 9), and the gearbox bolts up behind it.");
	step("Clutch disc against the flywheel, the long side of its hub toward the gearbox.", { PartType::CLUTCH_DISC });
	step("Pressure plate over it, its bolts done up evenly.", { PartType::PRESSURE_PLATE });
	step("Release bearing, then the gearbox, onto the bellhousing bolts.", { PartType::RELEASE_BEARING, PartType::TRANSMISSION });
	step("Transmission mount under its tail, and the driveshaft from the gearbox to the differential.", { PartType::TRANSMISSION_MOUNT, PartType::DRIVESHAFT });
	step("Clutch master cylinder on the firewall, and the release cylinder on the bellhousing; fill and bleed them (chapter 11).",
		{ PartType::CLUTCH_MASTER_CYLINDER, PartType::CLUTCH_RELEASE_CYLINDER });
	figure("Pressure plate bolts", holeTorque(CarSlot::PRESSURE_PLATE_BOLT));
	figure("Bellhousing bolts", holeTorque(CarSlot::BELLHOUSING_BOLT));
	figure("Transmission mount bolts", holeTorque(CarSlot::TRANSMISSION_MOUNT_BOLT));
	figure("Driveshaft flange nuts", holeTorque(CarSlot::DRIVESHAFT_FLANGE_NUT));

	begin("9. The Engine in the Car");
	heading("Putting It In");
	text("Any finished engine from the shop's stands goes in. Build and check it with its own manual first: once it's in, only the top "
		"of it can be got at.");
	step("Engine mounts on the crossmember, left and right.", { PartType::ENGINE_MOUNT });
	step("Lift the finished engine off its stand with the engine hoist.");
	step("Roll the hoist out to the car and over the bay, and lower the engine onto its mounts.");
	step("Its two mount nuts, then the clutch and gearbox (chapter 8), the starter, the engine harness and the radiator hoses.");
	figure("Engine mount bolts (mount to body)", holeTorque(CarSlot::ENGINE_MOUNT_BOLT));
	figure("Engine mount nuts (engine to mount)", holeTorque(CarSlot::ENGINE_MOUNT_NUT));
	heading("Service in the Car");
	text("With the engine in, these can come off and go back on where it sits:");
	bullet("The valve cover, its gasket and bolts (for the valve lash)");
	bullet("Spark plugs and plug leads");
	bullet("The carburettor and its bolts");
	bullet("The oil filter");
	text("Anything else needs the engine out.");
	heading("Taking It Out");
	step("Take off what's behind and round it: the gearbox and clutch, the starter, the engine harness, the radiator hoses and the exhaust's "
		"downpipe, and its mount nuts.");
	step("Roll the hoist over the bay and lift it out. Its oil and coolant are drained first.");

	begin("10. Fuel, Electrical, Cooling and Exhaust");
	heading("Fuel");
	step("Fuel tank under the boot floor, and its sender.", { PartType::FUEL_TANK, PartType::FUEL_SENDER });
	step("Fuel line set forward along the floor, and the filter in it by the engine.", { PartType::FUEL_LINE_SET, PartType::FUEL_FILTER });
	figure("Fuel tank bolts", holeTorque(CarSlot::FUEL_TANK_BOLT));
	figure("Tank capacity", litres(car.Fluid(FluidCircuit::FUEL).capacity));
	figure("Fuel", car.Fluid(FluidCircuit::FUEL).grade);
	heading("Electrical");
	figure("Battery, resting and charged", band(car.batteryVoltage, "V", 1));
	figure("Battery while cranking, at least", std::format("{:.1f} V", car.minCrankingVoltage));
	figure("Charging voltage at 2000 rpm", band(car.chargingVoltage, "V", 1));
	step("Main harness through the body, and the fuse box on it under the dash.", { PartType::MAIN_HARNESS, PartType::FUSE_BOX });
	step("Battery in the engine bay.", { PartType::BATTERY });
	step("Engine harness, once the engine is in, then the ignition coil on it.", { PartType::ENGINE_HARNESS, PartType::IGNITION_COIL });
	step("Starter, on the bellhousing once the gearbox is on.", { PartType::STARTER });
	figure("Starter bolts", holeTorque(CarSlot::STARTER_BOLT));
	heading("Cooling");
	step("Radiator across the nose, and the cooling fan behind it (it needs the harness to run).", { PartType::RADIATOR, PartType::COOLING_FAN });
	step("Upper and lower radiator hoses, from the radiator to the engine.", { PartType::RADIATOR_HOSE });
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
	problem("Turning the key does nothing", "The battery, main harness, fuse box, engine harness or starter isn't on, or the battery is flat.");
	problem("It cranks but won't fire", "The engine isn't finished, the tank is dry or holds the wrong fuel, the fuel line or filter is "
		"missing, the ignition coil isn't on, or the timing is miles out.");
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
