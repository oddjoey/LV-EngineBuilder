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
		{ PartType::LIFTER,					"Bucket Lifter",			S::VALVETRAIN,	14.0,	PER_VALVE,		true,	true,	false,	false,	1, 1,
			"Bucket tappet the cam lobe pushes on. A worn bucket is loose in its bore and ticks." },
		{ PartType::CRANK_SPROCKET,			"Crank Sprocket",			S::TIMING,		35.0,	1,				true,	true,	false,	false,	1, 1,
			"Drives the timing belt off the crank snout. Worn teeth let the belt ride up and skip." },
		{ PartType::CAM_SPROCKET,			"Cam Sprocket",				S::TIMING,		45.0,	1,				true,	true,	false,	false,	2, 2,
			"Twice the teeth of the crank sprocket, so the cam turns at half crank speed." },
		{ PartType::TIMING_BELT,			"Timing Belt",				S::TIMING,		45.0,	1,				true,	true,	false,	false,	2, 2,
			"Toothed rubber belt with glass-fibre cords. Its teeth wear down with the miles, letting the cam fall behind; worn far enough it skips a tooth, and the valves meet the pistons." },
		{ PartType::BELT_IDLER,			"Belt Idler",				S::TIMING,		25.0,	2,				true,	true,	false,	false,	1, 1,
			"Smooth pulley on a sealed bearing that routes the belt. A worn bearing has play and growls, and a seized one shreds the belt." },
		{ PartType::BELT_TENSIONER,		"Belt Tensioner",			S::TIMING,		40.0,	1,				true,	true,	false,	false,	1, 2,
			"Spring-loaded pulley that keeps the slack side tight. How far its arm has travelled shows how much the belt has worn." },
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
		{ PartType::IGNITION_COIL,			"Ignition Coil",			S::IGNITION,	45.0,	PER_CYLINDER,	true,	true,	false,	false,	1, 2,
			"Coil-on-plug: one per cylinder, pushed down through the valve cover onto its plug." },
		{ PartType::INTAKE_MANIFOLD_GASKET,	"Intake Manifold Gasket",	S::FUEL_AND_AIR, 18.0,	1,				false,	false,	true,	false,	4, 1,
			"Seals the runners to the intake ports. A leak here is unmetered air. Single use." },
		{ PartType::INTAKE_MANIFOLD,		"Intake Manifold",			S::FUEL_AND_AIR, 260.0,	1,				true,	true,	false,	true,	5, 3,
			"Cast aluminium plenum and runners. A warped flange sucks air past the gasket and leans the engine out." },
		{ PartType::THROTTLE_BODY,			"Throttle Body",			S::FUEL_AND_AIR, 150.0,	1,				true,	true,	false,	false,	2, 2,
			"Single 50 mm butterfly at the front of the plenum. Carbon builds up in the bore." },
		{ PartType::FUEL_INJECTOR,			"Fuel Injector",			S::FUEL_AND_AIR, 55.0,	PER_CYLINDER,	true,	true,	false,	false,	1, 2,
			"Port injector. Flow-test it with a burette: a clogged one runs its cylinder lean." },
		{ PartType::FUEL_RAIL,				"Fuel Rail",				S::FUEL_AND_AIR, 70.0,	1,				true,	true,	false,	false,	5, 1,
			"Feeds pressurised fuel to the injectors and clamps them in." },
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
			"M6 x 1.0 flange bolt, 10 mm head. Covers, the pan, the pump and pickup, the cam caps, the throttle body and fuel rail: 8 to 10 Nm, and overdoing it bends a cover or strips the thread.",
			nullptr, true, { "M6x1.0", 10 } },
		{ PartType::BOLT_M8,				"M8 Bolt",					S::FASTENERS,	0.8,	0,		false,	true,	false,	false,	1, 1,
			"M8 x 1.25 flange bolt, 12 mm head. Timing cover, water pump and thermostat housing: around 20 Nm.",
			nullptr, true, { "M8x1.25", 12 } },
		{ PartType::NUT_M8,					"M8 Nut",					S::FASTENERS,	0.8,	0,		false,	true,	false,	false,	1, 1,
			"M8 x 1.25 flange nut, 13 mm. Two clamp each rod cap at 45 Nm, where a loose one lets the rod go through the side of the block; it also goes on the manifold studs, 22 Nm on the intake and 25 on the exhaust.",
			nullptr, true, { "M8x1.25", 13, true } },
		{ PartType::BOLT_M10,				"M10 Bolt",					S::FASTENERS,	4.0,	0,		false,	true,	false,	false,	1, 1,
			"M10 x 1.25 bolt, 14 mm head. Two hold each main cap at 65 Nm. Too loose and the cap walks and the bearing spins.",
			nullptr, true, { "M10x1.25", 14 } },
		{ PartType::BOLT_M12,				"M12 Bolt",					S::FASTENERS,	5.0,	0,		false,	true,	false,	false,	1, 1,
			"M12 x 1.25 bolt, 17 mm head. Ten clamp the head down at 90 Nm, done up evenly; one holds the cam sprocket on at 60 Nm.",
			nullptr, true, { "M12x1.25", 17 } },
		{ PartType::BOLT_M14,				"M14 Bolt",					S::FASTENERS,	12.0,	0,		false,	true,	false,	false,	1, 1,
			"M14 x 1.5 bolt, 22 mm head. Holds the crank pulley on at 145 Nm: it needs a big torque wrench, and a breaker bar to get it off.",
			nullptr, true, { "M14x1.5", 22 } },

		// Added after the fasteners so saved part types keep their numbers.
		{ PartType::CAM_CAP,				"Cam Cap",					S::VALVETRAIN,	12.0,	PER_CAM,		false,	false,	false,	false,	1, 1,
			"Holds the cam down in its journal. Line-bored with its head: it fits that head, in its own numbered position, and nowhere else. Plastigauge goes under it.", nullptr, false },
	};
	static_assert(std::size(INFO) == static_cast<size_t>(PartType::COUNT), "Every part type needs a catalog row.");

	// Every set of holes, in AssemblySlot order. Holes for the same size can want different torques.
	const std::vector<FastenerHoles> HOLES = {
		//  name						parent						per	size					Nm		deg
		{ "Head Bolt",					PartType::CYLINDER_HEAD,	10,	PartType::BOLT_M12,		90.0 },
		{ "Main Cap Bolt",				PartType::MAIN_CAP,			2,	PartType::BOLT_M10,		65.0 },
		{ "Rod Nut",					PartType::ROD_CAP,			2,	PartType::NUT_M8,		45.0 },
		{ "Oil Pan Bolt",				PartType::OIL_PAN,			16,	PartType::BOLT_M6,		10.0 },
		{ "Valve Cover Bolt",			PartType::VALVE_COVER,		10,	PartType::BOLT_M6,		8.0 },
		{ "Timing Cover Bolt",			PartType::TIMING_COVER,		10,	PartType::BOLT_M8,		22.0 },
		{ "Intake Manifold Nut",		PartType::INTAKE_MANIFOLD,	8,	PartType::NUT_M8,		22.0 },
		{ "Exhaust Manifold Nut",		PartType::EXHAUST_MANIFOLD,	8,	PartType::NUT_M8,		25.0 },
		{ "Water Pump Bolt",			PartType::WATER_PUMP,		4,	PartType::BOLT_M8,		22.0 },
		{ "Thermostat Housing Bolt",	PartType::THERMOSTAT_HOUSING, 2, PartType::BOLT_M8,		20.0 },
		{ "Oil Pump Bolt",				PartType::OIL_PUMP,			5,	PartType::BOLT_M6,		10.0 },
		{ "Oil Pickup Bolt",			PartType::OIL_PICKUP,		2,	PartType::BOLT_M6,		10.0 },
		{ "Crank Pulley Bolt",			PartType::CRANK_PULLEY,		1,	PartType::BOLT_M14,		145.0 },
		{ "Cam Sprocket Bolt",			PartType::CAM_SPROCKET,		1,	PartType::BOLT_M12,		60.0 },
		{ "Throttle Body Bolt",			PartType::THROTTLE_BODY,	4,	PartType::BOLT_M6,		9.0 },
		{ "Fuel Rail Bolt",				PartType::FUEL_RAIL,		2,	PartType::BOLT_M6,		10.0 },
		{ "Cam Cap Bolt",				PartType::CAM_CAP,			2,	PartType::BOLT_M6,		10.0 },
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
					count += holes.perParent * CountPerEngine(holes.parent, spec);
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
