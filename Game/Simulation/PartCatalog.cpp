#include "PartCatalog.h"

#include <stdexcept>

#include "EngineSpec.h"

const char* PartSystemName(PartSystem system)
{
	switch (system)
	{
		case PartSystem::BOTTOM_END:	return "Bottom end";
		case PartSystem::TOP_END:		return "Top end";
		case PartSystem::VALVETRAIN:	return "Valvetrain";
		case PartSystem::TIMING:		return "Timing drive";
		case PartSystem::LUBRICATION:	return "Lubrication";
		case PartSystem::COOLING:		return "Cooling";
		case PartSystem::FUEL_AND_AIR:	return "Fuel and air";
		case PartSystem::EXHAUST:		return "Exhaust";
		case PartSystem::IGNITION:		return "Ignition";
		case PartSystem::FASTENERS:		return "Nuts and bolts";
		case PartSystem::BODY:			return "Body";
		case PartSystem::INTERIOR:		return "Interior";
		case PartSystem::FRONT_SUSPENSION: return "Front suspension";
		case PartSystem::REAR_SUSPENSION: return "Rear suspension";
		case PartSystem::STEERING:		return "Steering";
		case PartSystem::BRAKES:		return "Brakes";
		case PartSystem::DRIVETRAIN:	return "Clutch and drivetrain";
		case PartSystem::FUEL_SYSTEM:	return "Fuel system";
		case PartSystem::ELECTRICAL:	return "Electrical";
		case PartSystem::RADIATOR_AND_FAN: return "Radiator and fan";
		case PartSystem::CAR_EXHAUST:	return "Exhaust system";
		case PartSystem::WHEELS:		return "Wheels and tires";
		default:						return "Other";
	}
}

namespace
{
	using namespace PartCount;
	using S = PartSystem;

	// One row per PartType, in enum order.
	const PartInfo INFO[] = {
		//  type							name						system			price	count			used	core	once	big		grid
		{ PartType::BLOCK,					"Block",					S::BOTTOM_END,	800.0,	1,				true,	true,	false,	true,	4, 4,
			"Grey iron LV-16 block, with its five main caps. A used core is cheap, but plan on boring, honing and decking it." },
		{ PartType::CRANKSHAFT,				"Crankshaft",				S::BOTTOM_END,	600.0,	1,				true,	true,	false,	true,	6, 2,
			"Five-main crank. Worn journals can be ground undersize to suit thicker bearings." },
		{ PartType::CONNECTING_ROD,			"Connecting Rod",			S::BOTTOM_END,	55.0,	PER_CYLINDER,	true,	true,	false,	false,	1, 3,
			"Rod and its own cap, machined together. Big ends stretch oval with miles. Center length moves the piston at TDC." },
		{ PartType::PISTON,					"Piston",					S::BOTTOM_END,	70.0,	PER_CYLINDER,	false,	false,	false,	false,	2, 2,
			"Cast, hypereutectic or forged 2618. Forged grows more when hot: give it room." },
		{ PartType::PISTON_RING,			"Piston Ring",				S::BOTTOM_END,	15.0,	PER_CYLINDER,	false,	false,	false,	false,	1, 1,
			"Top compression ring. Gap it to its own bore; too tight and the ends butt when hot." },
		{ PartType::MAIN_BEARING,			"Main Bearing Upper",		S::BOTTOM_END,	6.0,	PER_MAIN,		false,	false,	false,	false,	1, 1,
			"Shell pair for one main journal: the upper shell sits in the block, the lower one in the main cap. Undersizes have thicker walls for reground cranks.",
			"Main Bearing Pair" },
		{ PartType::ROD_BEARING,			"Rod Bearing Upper",		S::BOTTOM_END,	5.0,	PER_CYLINDER,	false,	false,	false,	false,	1, 1,
			"Shell pair for one rod journal: the upper shell sits in the rod, the lower one in its cap. Plastigauge it once the cap is torqued.",
			"Rod Bearing Pair" },
		{ PartType::CYLINDER_HEAD,			"Cylinder Head",			S::TOP_END,		800.0,	1,				true,	true,	false,	true,	5, 3,
			"Aluminium SOHC 8-valve head. Check the deck, cam bores, guides and seats." },
		{ PartType::HEAD_GASKET,			"Head Gasket",				S::TOP_END,		45.0,	1,				false,	false,	true,	false,	4, 2,
			"Multi-layer steel. Its compressed thickness sets quench and compression. Crushed once torqued: never reuse one." },
		{ PartType::CAMSHAFT,				"Camshaft",					S::VALVETRAIN,	320.0,	1,				true,	true,	false,	true,	5, 1,
			"Single overhead cam. Lobe lift is airflow; a flat lobe costs power." },
		{ PartType::INTAKE_VALVE,			"Intake Valve",				S::VALVETRAIN,	22.0,	PER_CYLINDER,	false,	true,	false,	false,	1, 3,
			"Intake valve. Oversize stems suit guides reamed to match." },
		{ PartType::EXHAUST_VALVE,			"Exhaust Valve",			S::VALVETRAIN,	26.0,	PER_CYLINDER,	false,	true,	false,	false,	1, 3,
			"Exhaust valve. Runs hotter than the intake and needs more guide clearance." },
		{ PartType::VALVE_SPRING,			"Valve Spring",				S::VALVETRAIN,	9.0,	PER_VALVE,		false,	true,	false,	false,	1, 1,
			"Seat pressure keeps the valve on the cam. Weak springs float at high rpm." },

		{ PartType::CORE_PLUG,				"Core Plug",				S::BOTTOM_END,	4.0,	3,				false,	false,	true,	false,	1, 1,
			"Pressed-in steel cup sealing a casting hole in the water jacket. They rust through; knocking one out destroys it." },
		{ PartType::REAR_MAIN_SEAL,			"Rear Main Seal",			S::BOTTOM_END,	18.0,	1,				false,	false,	true,	false,	1, 1,
			"FKM lip seal around the crank flange. The classic oil leak if it's reused or the crank is scored." },
		{ PartType::OIL_PUMP,				"Oil Pump",					S::LUBRICATION,	120.0,	1,				true,	true,	false,	false,	2, 2,
			"Crank-driven gerotor pump. Worn rotors leak internally and the engine runs on low oil pressure. Check the tip and side clearances." },
		{ PartType::OIL_PICKUP,				"Oil Pickup",				S::LUBRICATION,	30.0,	1,				true,	true,	false,	false,	1, 3,
			"Tube and screen that feed the pump from the bottom of the pan." },
		{ PartType::OIL_PAN_GASKET,			"Oil Pan Gasket",			S::LUBRICATION,	22.0,	1,				false,	false,	true,	false,	4, 2,
			"Seals the pan to the block rails. Single use." },
		{ PartType::OIL_PAN,				"Oil Pan",					S::LUBRICATION,	90.0,	1,				true,	true,	false,	true,	4, 3,
			"Stamped steel sump. Over-tightened bolts dimple the rail and it never seals again: check it with a straightedge." },
		{ PartType::OIL_DRAIN_PLUG,			"Oil Drain Plug",			S::LUBRICATION,	6.0,	1,				true,	true,	false,	false,	1, 1,
			"Magnetic drain plug with its crush washer." },
		{ PartType::OIL_FILTER,				"Oil Filter",				S::LUBRICATION,	9.0,	1,				false,	false,	true,	false,	1, 1,
			"Spin-on full-flow filter. Always new on a fresh engine." },
		{ PartType::VALVE_STEM_SEAL,		"Valve Stem Seal",			S::VALVETRAIN,	3.0,	PER_VALVE,		false,	false,	true,	false,	1, 1,
			"Rubber umbrella over the top of each guide. Hard old seals let it smoke on overrun. Single use." },
		{ PartType::VALVE_RETAINER,			"Valve Retainer",			S::VALVETRAIN,	6.0,	PER_VALVE,		true,	true,	false,	false,	1, 1,
			"Retainer and split keepers that lock the spring onto the valve stem." },
		{ PartType::ROCKER_ARM,				"Rocker Arm",				S::VALVETRAIN,	14.0,	PER_VALVE,		true,	true,	false,	false,	1, 1,
			"Pivots on its rocker shaft: the cam lobe lifts one end, the other pushes the valve open. A worn bore rocks on the shaft and ticks." },
		{ PartType::CRANK_SPROCKET,			"Crank Sprocket",			S::TIMING,		35.0,	1,				true,	true,	false,	false,	1, 1,
			"Drives the timing belt off the crank snout. Worn teeth let the belt ride up and skip." },
		{ PartType::CAM_SPROCKET,			"Cam Sprocket",				S::TIMING,		45.0,	1,				true,	true,	false,	false,	2, 2,
			"Twice the teeth of the crank sprocket, so the cam turns at half crank speed." },
		{ PartType::TIMING_BELT,			"Timing Belt",				S::TIMING,		45.0,	1,				true,	true,	false,	false,	2, 2,
			"Toothed rubber belt with glass-fibre cords. Its teeth wear down with the miles, letting the cam fall behind; worn far enough it skips a tooth, and the valves meet the pistons." },
		{ PartType::BELT_IDLER,			"Belt Idler",				S::TIMING,		25.0,	0,				false,	false,	false,	false,	1, 1,
			"A plain idler pulley, from before the timing belt ran on its one idler pulley. Nothing takes one now.", nullptr, false },
		{ PartType::BELT_TENSIONER,		"Timing Idler Pulley",		S::TIMING,		40.0,	1,				true,	true,	false,	false,	1, 2,
			"The spring-loaded idler pulley that leans on the back of the timing belt's slack side and keeps it tight. How far its spring has travelled shows how much the belt has stretched: a weak one leaves the belt slack." },
		{ PartType::TIMING_COVER_GASKET,	"Timing Cover Gasket",		S::TIMING,		15.0,	1,				false,	false,	true,	false,	2, 3,
			"Seals the timing cover to the block and head. Single use." },
		{ PartType::TIMING_COVER,			"Timing Cover",				S::TIMING,		140.0,	1,				true,	true,	false,	true,	3, 3,
			"Cover over the timing belt, keeping grit and oil off it. Carries the front crank seal and the water pump." },
		{ PartType::FRONT_CRANK_SEAL,		"Front Crank Seal",			S::TIMING,		10.0,	1,				false,	false,	true,	false,	1, 1,
			"Lip seal in the timing cover around the crank snout. Single use." },
		{ PartType::CRANK_PULLEY,			"Crank Pulley",				S::TIMING,		85.0,	1,				true,	true,	false,	false,	2, 2,
			"Harmonic balancer: an outer ring on a rubber layer that damps crank twist. Old rubber lets the ring wander off true." },
		{ PartType::VALVE_COVER_GASKET,		"Valve Cover Gasket",		S::VALVETRAIN,	20.0,	1,				false,	false,	true,	false,	4, 1,
			"Moulded rubber gasket and spark plug tube seals. Single use." },
		{ PartType::VALVE_COVER,			"Valve Cover",				S::VALVETRAIN,	110.0,	1,				true,	true,	false,	true,	5, 2,
			"Keeps the oil in over the cam. A bent sealing rail leaks." },
		{ PartType::SPARK_PLUG,				"Spark Plug",				S::IGNITION,	8.0,	PER_CYLINDER,	false,	true,	false,	false,	1, 1,
			"Gap 0.8 to 0.9 mm. Electrodes wear the gap open until the coil can't jump it under load." },
		{ PartType::PLUG_LEAD,				"Spark Plug Lead",			S::IGNITION,	12.0,	PER_CYLINDER,	true,	true,	false,	false,	1, 2,
			"Carries the spark from the distributor cap to its plug, pushed down through the valve cover onto it." },
		{ PartType::INTAKE_MANIFOLD_GASKET,	"Intake Manifold Gasket",	S::FUEL_AND_AIR, 18.0,	1,				false,	false,	true,	false,	4, 1,
			"Seals the runners to the intake ports. A leak here is unmetered air. Single use." },
		{ PartType::INTAKE_MANIFOLD,		"Intake Manifold",			S::FUEL_AND_AIR, 260.0,	1,				true,	true,	false,	true,	5, 3,
			"Cast aluminium plenum and runners. A warped flange sucks air past the gasket and leans the engine out." },
		{ PartType::CARBURETTOR,			"Carburettor",				S::FUEL_AND_AIR, 220.0,	1,				true,	true,	false,	false,	2, 2,
			"Two-barrel downdraught carburettor on top of the plenum. Flow-test the main jet with a burette: a varnished one runs the whole engine lean. Carbon builds up in the primary bore." },
		{ PartType::FUEL_INJECTOR,			"Fuel Injector",			S::FUEL_AND_AIR, 55.0,	0,				false,	false,	false,	false,	1, 2,
			"Port injector, from before the engine went to a carburettor. Nothing takes one now.", nullptr, false },
		{ PartType::FUEL_PUMP,				"Fuel Pump",				S::FUEL_AND_AIR, 60.0,	1,				true,	true,	false,	false,	2, 2,
			"Mechanical diaphragm pump on the side of the head, worked by the camshaft. Feeds the carburettor."  },
		{ PartType::EXHAUST_MANIFOLD_GASKET, "Exhaust Manifold Gasket",	S::EXHAUST,		20.0,	1,				false,	false,	true,	false,	4, 1,
			"Multi-layer steel. Single use: it crushes to the flange." },
		{ PartType::EXHAUST_MANIFOLD,		"Exhaust Manifold",			S::EXHAUST,		180.0,	1,				true,	true,	false,	true,	5, 2,
			"Cast iron 4-into-1. Heat cycles warp the flange; machine it flat or it ticks and leaks." },
		{ PartType::WATER_PUMP_GASKET,		"Water Pump Gasket",		S::COOLING,		8.0,	1,				false,	false,	true,	false,	1, 1,
			"Seals the water pump to the timing cover. Single use." },
		{ PartType::WATER_PUMP,				"Water Pump",				S::COOLING,		75.0,	1,				true,	true,	false,	false,	2, 2,
			"Belt-driven impeller pump. Play in the bearing means the seal is next to go." },
		{ PartType::THERMOSTAT,				"Thermostat",				S::COOLING,		18.0,	1,				false,	false,	false,	false,	1, 1,
			"Wax pellet valve, opens at 88 C. Keeps the engine at temperature." },
		{ PartType::THERMOSTAT_HOUSING,		"Thermostat Housing",		S::COOLING,		30.0,	1,				true,	true,	false,	false,	1, 1,
			"Water outlet that holds the thermostat against the head." },

		{ PartType::MAIN_BEARING_LOWER,		"Main Bearing Lower",		S::BOTTOM_END,	6.0,	PER_MAIN,		false,	false,	false,	false,	1, 1,
			"The lower half of a main bearing pair. It sits in the main cap.", nullptr, false },
		{ PartType::ROD_BEARING_LOWER,		"Rod Bearing Lower",		S::BOTTOM_END,	5.0,	PER_CYLINDER,	false,	false,	false,	false,	1, 1,
			"The lower half of a rod bearing pair. It sits in the rod cap.", nullptr, false },
		{ PartType::ROD_CAP,				"Rod Cap",					S::BOTTOM_END,	15.0,	PER_CYLINDER,	false,	false,	false,	false,	1, 1,
			"Bolts the big end round the crank pin. Machined with its own rod: it fits that rod and no other.", nullptr, false },
		{ PartType::MAIN_CAP,				"Main Cap",					S::BOTTOM_END,	20.0,	PER_MAIN,		false,	false,	false,	false,	1, 1,
			"Holds the crank in its main journal. Line-bored with its block: it fits that block, in its own numbered position, and nowhere else.", nullptr, false },

		// Nuts and bolts by size: any of a size fits any hole of that size. The holes they go in
		// (and their torques) are in HOLES below; the per-engine count is worked out from them.
		//  type							name						system			price	count	used	core	once	big		grid
		{ PartType::BOLT_M6,				"M6 Bolt",					S::FASTENERS,	0.5,	0,		false,	true,	false,	false,	1, 1,
			"M6 x 1.0 flange bolt, 10 mm head. Covers, the pan, the pickup, the carburettor and fuel pump at 8 to 10 Nm, the cam caps at 13 and the oil pump at 21: overdoing it bends a cover or strips the thread.",
			nullptr, true, { "M6x1.0", 10 } },
		{ PartType::BOLT_M8,				"M8 Bolt",					S::FASTENERS,	0.8,	0,		false,	true,	false,	false,	1, 1,
			"M8 x 1.25 flange bolt, 12 mm head. Timing cover, water pump and thermostat housing at around 20 Nm; the rocker shafts at 25.",
			nullptr, true, { "M8x1.25", 12 } },
		{ PartType::NUT_M8,					"M8 Nut",					S::FASTENERS,	0.8,	0,		false,	true,	false,	false,	1, 1,
			"M8 x 1.25 flange nut, 13 mm. Two clamp each rod cap at 40 Nm, where a loose one lets the rod go through the side of the block; it also goes on the manifold studs, 25 Nm on the intake and the exhaust.",
			nullptr, true, { "M8x1.25", 13, true } },
		{ PartType::BOLT_M10,				"M10 Bolt",					S::FASTENERS,	4.0,	0,		false,	true,	false,	false,	1, 1,
			"M10 x 1.25 bolt, 14 mm head. Two hold each main cap at 59 Nm: too loose and the cap walks and the bearing spins. Six hold the flywheel at 79 Nm, and they hold the belt idlers (42), the downpipe (40), the alternator (39) and the distributor gear (29).",
			nullptr, true, { "M10x1.25", 14 } },
		{ PartType::BOLT_M12,				"M12 Bolt",					S::FASTENERS,	5.0,	0,		false,	true,	false,	false,	1, 1,
			"M12 x 1.25 bolt, 17 mm head. Ten clamp the head down at 59 Nm, done up evenly; one holds the cam sprocket on at 46 Nm.",
			nullptr, true, { "M12x1.25", 17 } },
		{ PartType::BOLT_M14,				"M14 Bolt",					S::FASTENERS,	12.0,	0,		false,	true,	false,	false,	1, 1,
			"M14 x 1.5 bolt, 22 mm head. Holds the crank pulley on at 118 Nm: it needs a big torque wrench, and a breaker bar to get it off.",
			nullptr, true, { "M14x1.5", 22 } },

		// Added after the fasteners so saved part types keep their numbers.
		{ PartType::CAM_CAP,				"Cam Cap",					S::VALVETRAIN,	12.0,	PER_CAM,		false,	false,	false,	false,	1, 1,
			"Holds the cam down in its journal. Line-bored with its head: it fits that head, in its own numbered position, and nowhere else. Plastigauge goes under it.", nullptr, false },

		// The rest of the 4A-C, appended so saved part types keep their numbers.
		{ PartType::ROCKER_SHAFT,			"Rocker Shaft",				S::VALVETRAIN,	45.0,	2,				true,	true,	false,	false,	5, 1,
			"Hollow steel shaft the rocker arms pivot on: one for the intakes, one for the exhausts, oil fed down the middle. Each arm wears its own spot on it: mic it there." },
		{ PartType::DISTRIBUTOR,			"Distributor",				S::IGNITION,	140.0,	1,				true,	true,	false,	false,	2, 2,
			"Gear-driven off the back of the camshaft: it times the spark and sends it down the right plug lead. A worn shaft bearing lets the rotor wobble and the timing wander." },
		{ PartType::FLYWHEEL,				"Flywheel",					S::BOTTOM_END,	180.0,	1,				true,	true,	false,	true,	3, 3,
			"Bolted to the back of the crank: the starter's ring gear and the clutch face. A warped or hot-spotted face judders; it can be skimmed flat." },
		{ PartType::EXHAUST_PIPE,			"Exhaust Downpipe",			S::EXHAUST,		70.0,	1,				true,	true,	false,	false,	1, 4,
			"Takes the exhaust from the manifold's collector down and away. A warped flange leaks and ticks." },
		{ PartType::ALTERNATOR,				"Alternator",				S::IGNITION,	120.0,	1,				true,	true,	false,	false,	2, 2,
			"Charges the battery, driven by the serpentine belt off the crank pulley. A worn bearing whines and wobbles its pulley." },
		{ PartType::DRIVE_BELT,				"V-Belt",					S::COOLING,		15.0,	1,				false,	false,	false,	false,	2, 2,
			"The fan belt, 11 x 980 mm: off the crank pulley, up round the water pump and across to the alternator, whose pivot sets how tight it is. Old ones stretch, crack and glaze, so a rebuild gets a new one." },
		{ PartType::DRIVE_BELT_TENSIONER,	"Belt Tensioner",			S::COOLING,		45.0,	0,				false,	false,	false,	false,	1, 2,
			"A sprung pulley for a serpentine belt, from before the fan belt went back to a V-belt. Nothing takes one now.", nullptr, false },
		{ PartType::PISTON_PIN,				"Piston Pin",				S::BOTTOM_END,	8.0,	PER_CYLINDER,	false,	false,	false,	false,	1, 1,
			"Hardened steel pin the piston pivots on, through its bosses and the rod's small end. Pressed in with the Piston Pin Press, with the "
			"piston on its rod. Comes with each piston.", nullptr, true },

		// The car. perEngine is 0 for all of them: CarAssembly counts them per car.
		//  type							name						system				price	count	used	core	once	big		grid
		{ PartType::BODY_SHELL,				"Body Shell",				S::BODY,			1800.0,	0,		true,	false,	false,	true,	0, 0,
			"A bare LE86 Comet coupe unibody: floor, roof, firewall and strut towers in one welded steel shell. Check the rails, the rear arches and the floor under the battery tray for rust before you build on it." },
		{ PartType::FRONT_FENDER,			"Front Fender",				S::BODY,			140.0,	0,		true,	false,	false,	true,	0, 0,
			"Bolt-on steel front wing, left or right. Eight bolts along the top rail, the A-pillar and the bumper bracket." },
		{ PartType::DOOR,					"Door",						S::BODY,			220.0,	0,		true,	false,	false,	true,	0, 0,
			"Frameless coupe door with its glass, regulator and latch. Hung on two hinges; shim it till the gaps are even and the latch catches without slamming." },
		{ PartType::HOOD,					"Hood",						S::BODY,			160.0,	0,		true,	false,	false,	true,	0, 0,
			"Steel bonnet on two hinges. Line it up with the fenders before the hinge bolts are done up." },
		{ PartType::TRUNK_LID,				"Trunk Lid",				S::BODY,			150.0,	0,		true,	false,	false,	true,	0, 0,
			"The notchback's boot lid, sprung on its torsion bars." },
		{ PartType::FRONT_BUMPER,			"Front Bumper",				S::BODY,			180.0,	0,		true,	false,	false,	true,	0, 0,
			"Urethane-faced front bumper on its energy absorbers. Bolts to the fenders and the front rails." },
		{ PartType::REAR_BUMPER,			"Rear Bumper",				S::BODY,			170.0,	0,		true,	false,	false,	true,	0, 0,
			"Rear bumper on its energy absorbers." },
		{ PartType::HEADLIGHT,				"Pop-up Headlight",			S::BODY,			120.0,	0,		true,	false,	false,	false,	3, 2,
			"Sealed-beam headlight in its flip-up pod, with the motor that raises it. Needs the main harness to light and to lift." },
		{ PartType::TAIL_LIGHT,				"Tail Light",				S::BODY,			60.0,	0,		true,	false,	false,	false,	3, 2,
			"Tail, brake, indicator and reversing lamp cluster for one side." },
		{ PartType::WINDSHIELD,				"Windshield",				S::BODY,			260.0,	0,		false,	false,	true,	true,	0, 0,
			"Laminated windscreen, bonded in with urethane. Cut out, it's scrap: a new one goes back in." },
		{ PartType::REAR_WINDOW,			"Rear Window",				S::BODY,			190.0,	0,		false,	false,	true,	true,	0, 0,
			"Toughened rear screen with its demister grid, bonded in like the windshield." },

		{ PartType::DASHBOARD,				"Dashboard",				S::INTERIOR,		240.0,	0,		true,	false,	false,	true,	0, 0,
			"The padded dash with its heater box and vents. The harness has to be in behind it first." },
		{ PartType::INSTRUMENT_CLUSTER,		"Instrument Cluster",		S::INTERIOR,		110.0,	0,		true,	false,	false,	false,	3, 2,
			"Speedo, tacho, fuel, temperature and the warning lamps. Reads the senders through the harness." },
		{ PartType::SEAT,					"Seat",						S::INTERIOR,		130.0,	0,		true,	false,	false,	true,	0, 0,
			"Reclining front bucket on its runners, four bolts to the floor." },
		{ PartType::STEERING_WHEEL,			"Steering Wheel",			S::INTERIOR,		70.0,	0,		true,	false,	false,	false,	3, 3,
			"Three-spoke wheel on a splined boss: line the spokes up with the front wheels straight ahead before the nut goes on." },

		{ PartType::FRONT_STRUT,			"Front Strut",				S::FRONT_SUSPENSION, 95.0,	0,		true,	false,	false,	false,	1, 5,
			"MacPherson strut: the damper cartridge in its tube, with the top mount that bolts into the tower. Its spring goes on off the car, squeezed in a spring compressor. A blown one bounces and wallows." },
		{ PartType::FRONT_COIL_SPRING,		"Front Coil Spring",		S::FRONT_SUSPENSION, 45.0,	0,		true,	false,	false,	false,	2, 3,
			"Front coil, 22 N/mm, fitted over its strut. Tired ones sag: measure the free length." },
		{ PartType::STEERING_KNUCKLE,		"Steering Knuckle",			S::FRONT_SUSPENSION, 85.0,	0,		true,	false,	false,	false,	2, 3,
			"The front upright: its hub and wheel bearing, bolted to the foot of the strut, its ball joint in the lower arm and its steering arm on the tie rod end." },
		{ PartType::LOWER_CONTROL_ARM,		"Lower Control Arm",		S::FRONT_SUSPENSION, 60.0,	0,		true,	false,	false,	false,	3, 1,
			"Pressed-steel lower arm with its ball joint. Its inner bush pivots on the crossmember." },
		{ PartType::STRUT_ROD,				"Strut Rod",				S::FRONT_SUSPENSION, 35.0,	0,		true,	false,	false,	false,	4, 1,
			"The tension rod from the lower arm forward to the chassis. The nuts on its threaded end set the caster." },
		{ PartType::FRONT_SWAY_BAR,			"Front Sway Bar",			S::FRONT_SUSPENSION, 55.0,	0,		true,	false,	false,	true,	0, 0,
			"Anti-roll bar across the front, clamped to the chassis and linked to both lower arms." },

		{ PartType::REAR_AXLE,				"Rear Axle",				S::REAR_SUSPENSION,	450.0,	0,		true,	false,	false,	true,	0, 0,
			"The live rear axle: its banjo housing, half shafts and bearings. The differential carrier bolts into its nose." },
		{ PartType::DIFFERENTIAL,			"Differential",				S::REAR_SUSPENSION,	320.0,	0,		true,	false,	false,	true,	0, 0,
			"The 6.7 inch drop-out carrier: ring and pinion and an open diff. Bought by its final drive ratio; a shorter one pulls harder and tops out sooner." },
		{ PartType::LOWER_TRAILING_LINK,	"Lower Trailing Link",		S::REAR_SUSPENSION,	40.0,	0,		true,	false,	false,	false,	4, 1,
			"One of the two long links that locate the axle fore and aft, from under the floor back to the axle tubes." },
		{ PartType::UPPER_TRAILING_LINK,	"Upper Trailing Link",		S::REAR_SUSPENSION,	40.0,	0,		true,	false,	false,	false,	3, 1,
			"One of the two short upper links that stop the axle winding up under power." },
		{ PartType::LATERAL_ROD,			"Lateral Rod",				S::REAR_SUSPENSION,	45.0,	0,		true,	false,	false,	false,	5, 1,
			"The Panhard rod: across from the body to the axle, it keeps the axle centred under the car." },
		{ PartType::REAR_COIL_SPRING,		"Rear Coil Spring",			S::REAR_SUSPENSION,	40.0,	0,		true,	false,	false,	false,	2, 2,
			"Rear coil, sat on the lower link. Weaker than the front's." },
		{ PartType::REAR_SHOCK,				"Rear Shock",				S::REAR_SUSPENSION,	55.0,	0,		true,	false,	false,	false,	1, 4,
			"Rear damper, from the axle up into the body." },

		{ PartType::STEERING_RACK,			"Steering Rack",			S::STEERING,		210.0,	0,		true,	false,	false,	true,	0, 0,
			"Manual rack and pinion, bolted across the front crossmember. Worn racks knock and wander." },
		{ PartType::TIE_ROD_END,			"Tie Rod End",				S::STEERING,		25.0,	0,		true,	false,	false,	false,	2, 1,
			"Ball joint on the end of each track rod. Screwing it in or out sets the toe; count the turns when you swap one." },
		{ PartType::STEERING_COLUMN,		"Steering Column",			S::STEERING,		140.0,	0,		true,	false,	false,	true,	0, 0,
			"Collapsible column with the ignition switch and the stalks, down through the firewall to the rack's pinion." },

		{ PartType::BRAKE_BOOSTER,			"Brake Booster",			S::BRAKES,			130.0,	0,		true,	false,	false,	false,	3, 3,
			"Vacuum servo on the firewall: it uses the intake's vacuum to multiply your foot. Without it the pedal goes rock hard." },
		{ PartType::BRAKE_MASTER_CYLINDER,	"Brake Master Cylinder",	S::BRAKES,			90.0,	0,		true,	false,	false,	false,	2, 1,
			"Tandem master cylinder and reservoir: one circuit to the front, one to the rear. A new or rebuilt one is bench bled before it goes on." },
		{ PartType::BRAKE_LINE_SET,			"Brake Line Set",			S::BRAKES,			120.0,	0,		false,	false,	false,	true,	0, 0,
			"The steel lines under the car from the master cylinder through the proportioning valve to each wheel's hose. Old ones rust from the outside in." },
		{ PartType::BRAKE_HOSE,				"Brake Hose",				S::BRAKES,			18.0,	0,		false,	false,	false,	false,	1, 2,
			"Flexible hose from the steel line to a front caliper, or (the third) down to the rear axle. Old hoses balloon and the pedal goes soft." },
		{ PartType::BRAKE_ROTOR,			"Brake Rotor",				S::BRAKES,			55.0,	0,		true,	false,	false,	false,	2, 2,
			"Ventilated front disc. Measure its thickness and runout: under the minimum it's scrap, and runout shakes the pedal." },
		{ PartType::BRAKE_PAD_SET,			"Brake Pad Set",			S::BRAKES,			30.0,	0,		false,	false,	false,	false,	1, 1,
			"A pair of pads for one front caliper. Bed new ones in on the road test." },
		{ PartType::BRAKE_CALIPER,			"Brake Caliper",			S::BRAKES,			75.0,	0,		true,	false,	false,	false,	2, 2,
			"Single-piston sliding caliper. A seized slide pin drags the pads and pulls the car to one side." },
		{ PartType::WHEEL_CYLINDER,			"Wheel Cylinder",			S::BRAKES,			22.0,	0,		true,	false,	false,	false,	1, 1,
			"The little twin-piston cylinder that pushes a drum's shoes apart. They weep with age." },
		{ PartType::BRAKE_SHOE_SET,			"Brake Shoe Set",			S::BRAKES,			35.0,	0,		false,	false,	false,	false,	2, 2,
			"A pair of shoes, their springs and the adjuster for one rear drum." },
		{ PartType::BRAKE_DRUM,				"Brake Drum",				S::BRAKES,			45.0,	0,		true,	false,	false,	false,	2, 2,
			"Rear drum. Its inside diameter wears out past the limit cast into it." },
		{ PartType::PARKING_BRAKE_CABLE,	"Parking Brake Cable",		S::BRAKES,			40.0,	0,		false,	false,	false,	false,	3, 1,
			"From the lever back to both rear shoes." },

		{ PartType::ENGINE_MOUNT,			"Engine Mount",				S::DRIVETRAIN,		35.0,	0,		true,	false,	false,	false,	2, 1,
			"Rubber mount from each side of the block to the crossmember. Split ones let the engine rock and clunk." },
		{ PartType::CLUTCH_DISC,			"Clutch Disc",				S::DRIVETRAIN,		70.0,	0,		false,	false,	false,	false,	2, 2,
			"200 mm sprung clutch plate. Centre it on the flywheel with an alignment tool or the gearbox won't go in." },
		{ PartType::PRESSURE_PLATE,			"Pressure Plate",			S::DRIVETRAIN,		95.0,	0,		true,	false,	false,	false,	2, 2,
			"Diaphragm-spring clutch cover, bolted to the flywheel over the disc." },
		{ PartType::RELEASE_BEARING,		"Release Bearing",			S::DRIVETRAIN,		25.0,	0,		false,	false,	false,	false,	1, 1,
			"Throw-out bearing on the gearbox's release fork. Always new with a clutch." },
		{ PartType::TRANSMISSION,			"Transmission",				S::DRIVETRAIN,		650.0,	0,		true,	false,	false,	true,	0, 0,
			"The MK50 five-speed gearbox, filled with GL-4 gear oil: its brass synchros don't like GL-5." },
		{ PartType::TRANSMISSION_MOUNT,		"Transmission Mount",		S::DRIVETRAIN,		30.0,	0,		true,	false,	false,	false,	2, 1,
			"Rubber mount and crossmember under the gearbox's tail." },
		{ PartType::CLUTCH_MASTER_CYLINDER,	"Clutch Master Cylinder",	S::DRIVETRAIN,		55.0,	0,		true,	false,	false,	false,	1, 2,
			"Pedal-worked cylinder on the firewall, with its own small reservoir of brake fluid." },
		{ PartType::CLUTCH_RELEASE_CYLINDER, "Clutch Release Cylinder",	S::DRIVETRAIN,		35.0,	0,		true,	false,	false,	false,	1, 1,
			"The slave cylinder on the bellhousing that pushes the release fork. Bled at its own nipple." },
		{ PartType::DRIVESHAFT,				"Driveshaft",				S::DRIVETRAIN,		180.0,	0,		true,	false,	false,	true,	0, 0,
			"Two-piece propshaft with its centre bearing, from the gearbox to the differential's flange." },

		{ PartType::FUEL_TANK,				"Fuel Tank",				S::FUEL_SYSTEM,		160.0,	0,		true,	false,	false,	true,	0, 0,
			"50 litre steel tank under the boot floor. Old ones rust inside and the flakes block the filter." },
		{ PartType::FUEL_SENDER,			"Fuel Sender",				S::FUEL_SYSTEM,		45.0,	0,		true,	false,	false,	false,	1, 3,
			"The float and pickup in the tank: it draws the fuel and works the gauge." },
		{ PartType::FUEL_LINE_SET,			"Fuel Line Set",			S::FUEL_SYSTEM,		70.0,	0,		false,	false,	false,	true,	0, 0,
			"Steel feed and return lines along the floor, with the rubber hoses at each end." },
		{ PartType::FUEL_FILTER,			"Fuel Filter",				S::FUEL_SYSTEM,		12.0,	0,		false,	false,	true,	false,	1, 1,
			"Inline filter before the engine's fuel pump. Always new on a car that's stood." },

		{ PartType::BATTERY,				"Battery",					S::ELECTRICAL,		90.0,	0,		false,	false,	false,	false,	2, 2,
			"12 V, 40 Ah lead-acid battery. Charge it before the first crank: a flat one won't turn the starter." },
		{ PartType::MAIN_HARNESS,			"Main Harness",				S::ELECTRICAL,		260.0,	0,		true,	false,	false,	true,	0, 0,
			"The body's wiring: dash, lights, fuse box and the run back to the boot. Chafed or cut wires blow fuses." },
		{ PartType::ENGINE_HARNESS,			"Engine Harness",			S::ELECTRICAL,		120.0,	0,		true,	false,	false,	false,	3, 2,
			"The engine bay's loom: starter, alternator, coil, distributor and the senders. Plugs into the main harness at the firewall." },
		{ PartType::FUSE_BOX,				"Fuse Box",					S::ELECTRICAL,		50.0,	0,		true,	false,	false,	false,	2, 1,
			"Fuses and relays under the dash. Every circuit goes through it." },
		{ PartType::IGNITION_COIL,			"Ignition Coil",			S::ELECTRICAL,		45.0,	0,		true,	false,	false,	false,	1, 2,
			"Steps the battery's 12 V up to the thousands the distributor sends to the plugs." },
		{ PartType::STARTER,				"Starter",					S::ELECTRICAL,		110.0,	0,		true,	false,	false,	false,	2, 2,
			"Bolted through the bellhousing, its pinion meshes with the flywheel's ring gear to crank the engine." },

		{ PartType::RADIATOR,				"Radiator",					S::RADIATOR_AND_FAN, 140.0,	0,		true,	false,	false,	true,	0, 0,
			"Brass and copper crossflow radiator with its pressure cap. Pressure test an old one: they weep at the seams." },
		{ PartType::RADIATOR_HOSE,			"Radiator Hose",			S::RADIATOR_AND_FAN, 15.0,	0,		false,	false,	false,	false,	2, 1,
			"Upper (thermostat housing to radiator) or lower (radiator back to the water pump). Soft or cracked ones split hot." },
		{ PartType::COOLING_FAN,			"Cooling Fan",				S::RADIATOR_AND_FAN, 85.0,	0,		true,	false,	false,	false,	3, 3,
			"Electric fan behind the radiator, switched by a thermo switch in the coolant." },

		{ PartType::CATALYTIC_CONVERTER,	"Catalytic Converter",		S::CAR_EXHAUST,		230.0,	0,		true,	false,	false,	false,	2, 4,
			"From the downpipe back. A melted one from a rich-running engine chokes the exhaust." },
		{ PartType::MUFFLER,				"Muffler",					S::CAR_EXHAUST,		95.0,	0,		true,	false,	false,	false,	2, 5,
			"Tailpipe and silencer. Blown ones are loud and rattle." },

		{ PartType::WHEEL,					"Wheel",					S::WHEELS,			60.0,	0,		true,	false,	false,	false,	3, 3,
			"13 x 5.5 inch steel wheel. Its tire is fitted to it off the car." },
		{ PartType::TIRE,					"Tire",						S::WHEELS,			65.0,	0,		true,	false,	false,	false,	3, 3,
			"185/70 R13 radial. Check the tread depth and the date code: old rubber hardens and stops gripping." },

		{ PartType::LUG_NUT,				"Lug Nut",					S::FASTENERS,		1.0,	0,		false,	false,	false,	false,	1, 1,
			"M12 x 1.5 conical-seat wheel nut, 21 mm. Four on each wheel at 103 Nm, done up across in a star.",
			nullptr, true, { "M12x1.5", 21, true } },
		{ PartType::NUT_M10,				"M10 Nut",					S::FASTENERS,		1.0,	0,		false,	false,	false,	false,	1, 1,
			"M10 x 1.25 flange nut, 14 mm. The engine mounts, the driveshaft flange, the master cylinder and the exhaust's joints.",
			nullptr, true, { "M10x1.25", 14, true } },
		{ PartType::NUT_M12,				"M12 Nut",					S::FASTENERS,		1.5,	0,		false,	false,	false,	false,	1, 1,
			"M12 x 1.25 castle nut, 17 mm. The tie rod ends' taper studs, at 59 Nm: line its slot up with the cotter pin hole by going tighter, never looser.",
			nullptr, true, { "M12x1.25", 17, true } },
	};
	static_assert(std::size(INFO) == static_cast<size_t>(PartType::COUNT), "Every part type needs a catalog row.");

	// Every set of holes, in AssemblySlot order. Holes for the same size can want different torques.
	const std::vector<FastenerHoles> HOLES = {
		//  name						parent						per	size					Nm		deg
		{ "Head Bolt",					PartType::CYLINDER_HEAD,	10,	PartType::BOLT_M12,		59.0 },
		{ "Main Cap Bolt",				PartType::MAIN_CAP,			2,	PartType::BOLT_M10,		59.0 },
		{ "Rod Nut",					PartType::ROD_CAP,			2,	PartType::NUT_M8,		40.0 },
		{ "Oil Pan Bolt",				PartType::OIL_PAN,			16,	PartType::BOLT_M6,		10.0 },
		{ "Valve Cover Bolt",			PartType::VALVE_COVER,		10,	PartType::BOLT_M6,		8.0 },
		{ "Timing Cover Bolt",			PartType::TIMING_COVER,		10,	PartType::BOLT_M8,		22.0 },
		{ "Intake Manifold Nut",		PartType::INTAKE_MANIFOLD,	8,	PartType::NUT_M8,		25.0 },
		{ "Exhaust Manifold Nut",		PartType::EXHAUST_MANIFOLD,	8,	PartType::NUT_M8,		25.0 },
		{ "Water Pump Bolt",			PartType::WATER_PUMP,		4,	PartType::BOLT_M8,		22.0 },
		{ "Thermostat Housing Bolt",	PartType::THERMOSTAT_HOUSING, 2, PartType::BOLT_M8,		20.0 },
		{ "Oil Pump Bolt",				PartType::OIL_PUMP,			5,	PartType::BOLT_M6,		21.0 },
		{ "Oil Pickup Bolt",			PartType::OIL_PICKUP,		2,	PartType::BOLT_M6,		10.0 },
		{ "Crank Pulley Bolt",			PartType::CRANK_PULLEY,		1,	PartType::BOLT_M14,		118.0 },
		{ "Cam Sprocket Bolt",			PartType::CAM_SPROCKET,		1,	PartType::BOLT_M12,		46.0 },
		{ "Carburettor Bolt",			PartType::CARBURETTOR,	4,	PartType::BOLT_M6,		9.0 },
		{ "Fuel Pump Bolt",				PartType::FUEL_PUMP,		2,	PartType::BOLT_M6,		10.0 },
		{ "Cam Cap Bolt",				PartType::CAM_CAP,			2,	PartType::BOLT_M6,		13.0 },
		{ "Rocker Shaft Bolt",			PartType::ROCKER_SHAFT,		5,	PartType::BOLT_M8,		25.0,	true },
		{ "Distributor Gear Bolt",		PartType::DISTRIBUTOR,		1,	PartType::BOLT_M10,		29.0 },
		{ "Flywheel Bolt",				PartType::FLYWHEEL,			6,	PartType::BOLT_M10,		79.0 },
		{ "Idler Pulley Bolt",			PartType::BELT_TENSIONER,	1,	PartType::BOLT_M10,		37.0 },
		{ "Exhaust Pipe Bolt",			PartType::EXHAUST_PIPE,		3,	PartType::BOLT_M10,		40.0 },
		{ "Alternator Bolt",			PartType::ALTERNATOR,		2,	PartType::BOLT_M10,		39.0 },
		{ "Belt Tensioner Bolt",		PartType::DRIVE_BELT_TENSIONER, 1, PartType::BOLT_M10,	42.0 },
	};
}

namespace PartCatalog
{
	const PartInfo& Get(PartType type)
	{
		auto index = static_cast<size_t>(type);
		if (index >= std::size(INFO) || INFO[index].type != type)
			throw std::runtime_error("Part catalog out of order!");
		return INFO[index];
	}

	const std::vector<PartType>& All()
	{
		static const std::vector<PartType> types = [] {
			std::vector<PartType> list;
			for (int system = 0; system < static_cast<int>(PartSystem::COUNT); system++)
				for (const auto& info : INFO)
					if (static_cast<int>(info.system) == system)
						list.push_back(info.type);
			return list;
		}();
		return types;
	}

	const char* StoreName(PartType type)
	{
		const auto& info = Get(type);
		return info.storeName ? info.storeName : info.name;
	}

	const std::vector<FastenerHoles>& Holes()
	{
		return HOLES;
	}

	int CountPerEngine(PartType type, const EngineSpec& spec)
	{
		// A size: however many holes it goes in.
		if (Get(type).fastener.IsFastener())
		{
			int count = 0;
			for (const auto& holes : HOLES)
				if (holes.size == type)
					count += holes.Count(CountPerEngine(holes.parent, spec));
			return count;
		}
		switch (Get(type).perEngine)
		{
			case PER_CYLINDER:	return spec.cylinders;
			case PER_VALVE:		return spec.cylinders * 2;
			case PER_MAIN:		return spec.mainJournals;
			case PER_CAM:		return spec.camJournals;
			default:			return Get(type).perEngine;
		}
	}
}

const char* PartCatalog::Purpose(PartType type)
{
	switch (type)
	{
		case PartType::BLOCK:					return "The engine's foundation: the cylinders the pistons run in, the saddles the crank turns in, and the passages that carry oil and coolant round it. Everything else bolts to it.";
		case PartType::CRANKSHAFT:				return "Turns the pistons' up and down into the spin that drives the car. It runs in the main bearings, and each rod's big end runs on one of its offset crank pins.";
		case PartType::CONNECTING_ROD:			return "Joins a piston to the crankshaft: the small end takes the piston's pin, the big end (closed by its cap) wraps a crank pin on its bearing shells. It carries every power stroke.";
		case PartType::PISTON:					return "Slides up and down its cylinder. Burning fuel pushes it down, and through its rod that turns the crank; its rings seal it against the bore.";
		case PartType::PISTON_RING:				return "A springy split ring in the piston's top groove that presses out against the bore, sealing in the compression and keeping the gas from blowing past into the crankcase.";
		case PartType::MAIN_BEARING:
		case PartType::MAIN_BEARING_LOWER:		return "A main bearing shell: one half of the soft-faced sleeve the crankshaft spins in. Oil fed between it and the journal holds the crank off the metal.";
		case PartType::ROD_BEARING:
		case PartType::ROD_BEARING_LOWER:		return "A rod bearing shell: one half of the sleeve a connecting rod's big end turns on the crank pin with. It rides on a film of oil, and takes the hardest knocks in the engine.";
		case PartType::CYLINDER_HEAD:			return "Closes the top of the cylinders. It holds the combustion chambers, the valves and their springs, the ports the mixture comes in and the exhaust goes out of, and the camshaft.";
		case PartType::HEAD_GASKET:				return "The thin seal crushed between the block and the head: it keeps the combustion pressure in each cylinder and the oil and coolant in their own passages.";
		case PartType::CAMSHAFT:				return "Driven by the timing belt at half the crank's speed, its lobes open each valve at the right moment, for the right time, by the right amount.";
		case PartType::INTAKE_VALVE:			return "Opens to let the fuel and air mixture into the cylinder, then shuts tight on its seat for the compression and power strokes.";
		case PartType::EXHAUST_VALVE:			return "Opens to let the burnt gas out of the cylinder after the power stroke. It runs much hotter than the intake valve.";
		case PartType::VALVE_SPRING:			return "Snaps its valve shut after the cam has opened it, and holds it on its seat. Too weak, and the valve can't keep up at high revs.";
		case PartType::CORE_PLUG:				return "A pressed-in cap over a hole in the block's water jacket, left from casting it. It keeps the coolant in, and gives way before the block cracks if it freezes.";
		case PartType::REAR_MAIN_SEAL:			return "The lip seal round the back of the crankshaft, where it comes out of the block to the flywheel: it keeps the oil in.";
		case PartType::OIL_PUMP:				return "Driven off the front of the crank, it draws oil up from the pan and pushes it under pressure through the filter to the bearings, cam and valvetrain.";
		case PartType::OIL_PICKUP:				return "The tube and strainer the oil pump draws through, reaching down into the bottom of the pan so it stays under the oil.";
		case PartType::OIL_PAN_GASKET:			return "Seals the oil pan to the bottom of the block.";
		case PartType::OIL_PAN:					return "The sump under the engine: it holds the oil, and the oil drains back into it from everywhere it's pumped.";
		case PartType::OIL_DRAIN_PLUG:			return "The plug in the bottom of the pan the old oil is let out through.";
		case PartType::OIL_FILTER:				return "Strains the dirt and metal out of the oil on its way from the pump to the bearings.";
		case PartType::VALVE_STEM_SEAL:			return "A small seal over each valve guide that lets just enough oil down the stem to lubricate it, and keeps the rest from being sucked into the cylinder and burnt.";
		case PartType::VALVE_RETAINER:			return "The cap on top of a valve spring, locked to the valve stem by its keepers: it holds the spring compressed on the valve.";
		case PartType::ROCKER_ARM:				return "A lever on the rocker shaft: the cam lobe pushes one end up, and the other end pushes its valve down open.";
		case PartType::ROCKER_SHAFT:			return "The hollow shaft the rocker arms pivot on, which also carries the oil to them.";
		case PartType::CRANK_SPROCKET:			return "The toothed pulley on the front of the crank that drives the timing belt.";
		case PartType::CAM_SPROCKET:			return "The toothed pulley on the camshaft, twice the crank sprocket's size, so the cam turns once for every two turns of the crank.";
		case PartType::TIMING_BELT:				return "The toothed belt that turns the camshaft in step with the crankshaft. If it slips a tooth the valve timing is off; if it breaks the engine stops, and the valves can hit the pistons.";
		case PartType::BELT_IDLER:				return "A smooth pulley that guides the timing belt round its path.";
		case PartType::BELT_TENSIONER:			return "A spring-loaded pulley pressing on the timing belt to keep it at the right tension.";
		case PartType::TIMING_COVER_GASKET:		return "Seals the timing cover to the front of the engine.";
		case PartType::TIMING_COVER:			return "Covers the timing belt and its pulleys at the front of the engine, keeping out the dirt and keeping fingers out.";
		case PartType::FRONT_CRANK_SEAL:		return "The lip seal round the crank snout where it comes out through the timing cover: it keeps the oil in.";
		case PartType::CRANK_PULLEY:			return "The pulley on the front of the crank that drives the V-belt (water pump and alternator). Its rubber-mounted outer ring damps the crank's twisting.";
		case PartType::VALVE_COVER_GASKET:		return "Seals the valve cover to the head.";
		case PartType::VALVE_COVER:				return "Closes up the top of the head over the cam and rockers, keeping the oil in and the dirt out.";
		case PartType::SPARK_PLUG:				return "Screws into the head, its tip in the combustion chamber, and sparks across its gap to light the mixture.";
		case PartType::PLUG_LEAD:				return "The insulated lead carrying the high voltage from the distributor to its spark plug.";
		case PartType::INTAKE_MANIFOLD_GASKET:	return "Seals the intake manifold to the head's intake ports.";
		case PartType::INTAKE_MANIFOLD:			return "Carries the fuel and air mixture from the carburettor and shares it out to each cylinder's intake port.";
		case PartType::CARBURETTOR:				return "Mixes fuel into the air the engine draws in, in the right proportion, and its throttle sets how much goes in: how hard the engine works.";
		case PartType::FUEL_INJECTOR:			return "Sprays fuel into the intake. No engine here takes one: it's from before the carburettor.";
		case PartType::FUEL_PUMP:				return "Worked by an eccentric on the camshaft, it lifts fuel from the tank to the carburettor.";
		case PartType::EXHAUST_MANIFOLD_GASKET:	return "Seals the exhaust manifold to the head's exhaust ports, against the hottest gas in the engine.";
		case PartType::EXHAUST_MANIFOLD:		return "Collects the burnt gas from each cylinder's exhaust port into one pipe.";
		case PartType::EXHAUST_PIPE:			return "Carries the exhaust away from the manifold.";
		case PartType::WATER_PUMP_GASKET:		return "Seals the water pump to the front of the block.";
		case PartType::WATER_PUMP:				return "Driven by the V-belt, it pushes coolant round the block and head and out to the radiator.";
		case PartType::THERMOSTAT:				return "A valve that stays shut until the coolant is hot, so the engine warms up quickly, then opens to let it through to the radiator.";
		case PartType::THERMOSTAT_HOUSING:		return "The outlet the thermostat sits in, where the hose to the radiator joins.";
		case PartType::ROD_CAP:					return "Closes the big end of its connecting rod round the crank pin, holding the lower bearing shell. Machined together with its rod: it only fits that one.";
		case PartType::MAIN_CAP:				return "Bolts up under a main saddle to close it round the crank journal, holding the lower main shell. Line-bored with its block: it only fits there.";
		case PartType::CAM_CAP:					return "Bolts down over a cam journal to hold the camshaft in the head. Line-bored with its head: it only fits there.";
		case PartType::BOLT_M6:
		case PartType::BOLT_M8:
		case PartType::BOLT_M10:
		case PartType::BOLT_M12:
		case PartType::BOLT_M14:				return "A bolt, holding a part on by being stretched a little as it's done up: torqued right, it clamps hard enough that the joint never moves.";
		case PartType::NUT_M8:					return "A nut, clamping a part on as it's done up a stud or bolt.";
		case PartType::DISTRIBUTOR:				return "Driven off the camshaft, it sends the spark to each cylinder's plug lead in turn, at the right moment in its stroke.";
		case PartType::FLYWHEEL:				return "The heavy disc on the back of the crank: it smooths out the power strokes, carries the starter ring, and is what the clutch grips.";
		case PartType::ALTERNATOR:				return "Driven by the V-belt, it makes the electricity that runs the car and charges the battery.";
		case PartType::DRIVE_BELT:				return "The V-belt from the crank pulley that turns the water pump and the alternator.";
		case PartType::DRIVE_BELT_TENSIONER:	return "A spring-loaded pulley that keeps the V-belt tight so it doesn't slip.";
		case PartType::PISTON_PIN:				return "The hardened pin the piston pivots on in its rod's small end, pressed through both.";
		case PartType::COUNT:					break;
	}
	return "";
}
