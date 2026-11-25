// The LE86's side of the workshop: the car the shop starts with, working on it, its fluids and the road test.
#include "Workshop.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>

#include "PartFactory.h"
#include "Random.h"
#include "../Save/SaveText.h"

namespace
{
	constexpr double ROAD_TEST_HOURS = 0.4;
	constexpr double ROAD_TEST_FUEL = 3.0;
	constexpr double BURP_MINUTES = 12.0;
	constexpr double BURP_FUEL = 0.3;
}

bool Workshop::OwnsCar() const
{
	return m_car != nullptr;
}

const CarBuild* Workshop::GetCar() const
{
	return m_car;
}

void Workshop::SetActiveCar(int car) const
{
	// (Past the last car, or before the first, there's no car active: the car's calls do nothing.)
	m_activeCar = (std::max)(car, 0);
	m_car = m_activeCar < GetCarCount() ? m_cars[m_activeCar].get() : nullptr;
}

int Workshop::CarOf(const Part& part) const
{
	for (int car = 0; car < GetCarCount(); car++)
		if (m_cars[car]->assembly.Find(&part))
			return car;
	return -1;
}

int Workshop::CarWithEngine(int engine) const
{
	if (engine < 0)
		return -1;
	for (int car = 0; car < GetCarCount(); car++)
		if (m_cars[car]->assembly.GetEngine() == engine)
			return car;
	return -1;
}

int Workshop::CarOwnedBy(int owner) const
{
	for (int car = 0; car < GetCarCount(); car++)
		if (m_cars[car]->owner == owner)
			return car;
	return -1;
}

void Workshop::SetCarOwner(int car, int owner)
{
	if (car >= 0 && car < GetCarCount())
		m_cars[car]->owner = owner;
}

void Workshop::CarChanged()
{
	if (m_car)
		m_car->roadTestCurrent = false;
}

int Workshop::BuildStartingCar(int engine)
{
	// The LE86, complete and running, its LV-16 in it under that stand's number. Everything on both was fitted new
	// once and has done its miles since: used, and here and there worn past its limit.
	const size_t before = m_inventory.size();
	m_cars.push_back(std::make_unique<CarBuild>(CarSpec::LE86Coupe()));
	SetActiveCar(GetCarCount() - 1);
	m_activeEngine = engine;
	FitNewEngineParts();
	WearStartingEngine(engine, 75.0);
	FitNewCarParts();
	// (Its plugs and unions first: a circuit with a union undone holds nothing.)
	DevPlugCar(true);
	FillAndBleedCar();
	WearStartingCar();
	for (size_t i = before; i < m_inventory.size(); i++)
		m_inventory[i]->MarkUsed();
	// Its fillers' caps and plugs, on.
	IssueCarCaps(*m_car);
	CarChanged();
	return m_activeCar;
}

void Workshop::SetUpStartingCar()
{
	m_cars.clear();
	BuildStartingCar(0);
	// The shop's key (on the tool board) is the one cut for its ignition.
	if (const auto ignition = m_car->assembly.Get(CarSlot::IGNITION_SWITCH, 0))
		m_keyCuts[CarKeyTool()] = ignition->GetId();
}

std::optional<Workshop::NewCar> Workshop::AddCarFor(int owner)
{
	// Its engine needs a stand's number of its own (the stand stays empty while it's in the car).
	if (GetEngineCount() >= MAX_ENGINES)
		return std::nullopt;
	const int activeEngine = m_activeEngine, activeCar = m_activeCar;
	m_engines.push_back(std::make_unique<EngineBuild>(*m_spec));
	// (It comes in the car, with no stand of its own: one can be bought for it.)
	m_engines.back()->stand = false;
	NewCar made;
	made.car = BuildStartingCar(GetEngineCount() - 1);
	m_cars[made.car]->owner = owner;
	if (const auto ignition = m_car->assembly.Get(CarSlot::IGNITION_SWITCH, 0))
		made.key = CutKeyFor(*ignition);
	m_activeEngine = activeEngine;
	SetActiveCar(activeCar);
	return made;
}

namespace
{
	// A worn part's feature: set to this value (its samples moved with it, so it measures what it is).
	void WearTo(Part& part, const std::string& name, double value)
	{
		Feature* feature = part.FindFeature(name);
		if (!feature)
			return;
		double shift = value - feature->value;
		feature->value = value;
		for (auto& station : feature->samples)
			for (double& sample : station)
				sample += shift;
	}

	// Worn past its limit by this much more than its whole tolerance allows (factor 1.5: half again past it).
	void WearPast(Part& part, const std::string& name, double factor)
	{
		if (const Feature* feature = part.FindFeature(name))
			WearTo(part, name, feature->spec.Max() * factor);
	}
}

void Workshop::WearStartingEngine(int engine, double health)
{
	// The faults it can have, the likeliest first, each one a thing to find (on the dyno, or with the right tool) and put
	// right on its own: they go on one at a time till the dyno makes it about as healthy as asked. None of them
	// stops it running or wrecks anything.
	auto& assembly = m_engines[engine]->assembly;
	auto at = [&](AssemblySlot slot, int index = 0) { return assembly.Get(slot, index); };
	const std::vector<std::function<void()>> faults = {
		// Two plugs worn wide: those cylinders misfire under load.
		[&] { if (auto plug = at(AssemblySlot::SPARK_PLUG, 1)) WearTo(*plug, FeatureNames::ELECTRODE_GAP, 1.32); },
		// The timing belt's teeth worn: the cam's a few degrees late.
		[&] { if (auto belt = at(AssemblySlot::TIMING_BELT)) WearTo(*belt, FeatureNames::BELT_TOOTH_HEIGHT, 1.66); },
		// The water pump's bearing shot: it weeps at the weep hole.
		[&] { if (auto pump = at(AssemblySlot::WATER_PUMP)) WearPast(*pump, FeatureNames::SHAFT_PLAY, 1.8); },
		[&] { if (auto plug = at(AssemblySlot::SPARK_PLUG, 2)) WearTo(*plug, FeatureNames::ELECTRODE_GAP, 1.28); },
		// The distributor shaft worn: the timing wanders.
		[&] { if (auto distributor = at(AssemblySlot::DISTRIBUTOR)) WearPast(*distributor, FeatureNames::SHAFT_PLAY, 1.6); },
		// The valve cover's rail bent: it weeps oil down the head.
		[&] { if (auto cover = at(AssemblySlot::VALVE_COVER)) WearPast(*cover, FeatureNames::RAIL_FLATNESS, 1.7); },
		// The alternator's bearing whining.
		[&] { if (auto alternator = at(AssemblySlot::ALTERNATOR)) WearPast(*alternator, FeatureNames::SHAFT_PLAY, 1.5); },
		// The exhaust manifold warped: it ticks.
		[&] { if (auto manifold = at(AssemblySlot::EXHAUST_MANIFOLD)) WearPast(*manifold, FeatureNames::FLANGE_FLATNESS, 1.6); },
	};
	for (const auto& fault : faults)
	{
		if (!assembly.IsComplete() || Dyno::Run(assembly).healthScore <= health + 1.5)
			break;
		fault();
	}
	m_engines[engine]->dynoCurrent = false;
}

void Workshop::WearStartingCar()
{
	if (!m_car)
		return;
	auto& car = m_car->assembly;
	// Tires hard and cracked with age, the driver's side engine mount split, and the front pads worn to their backing.
	for (int index = 0; index < car.SlotCount(CarSlot::TIRE); index++)
		if (auto tire = car.Get(CarSlot::TIRE, index))
			tire->MarkDamaged("Hardened and cracked with age");
	if (auto mount = car.Get(CarSlot::ENGINE_MOUNT, 0))
		mount->MarkDamaged("Its rubber is split");
	if (auto pads = car.Get(CarSlot::BRAKE_PAD_SET, 0))
		pads->MarkDamaged("Worn through to the backing plates");
	// And the oil's well past its change.
	auto fluids = std::array<FluidLevel, FLUID_CIRCUITS>{};
	for (int c = 0; c < FLUID_CIRCUITS; c++)
		fluids[c] = car.GetFluid(static_cast<FluidCircuit>(c));
	auto& oil = fluids[static_cast<size_t>(FluidCircuit::ENGINE_OIL)];
	oil.hours = (std::max)(oil.hours, car.GetSpec().Fluid(FluidCircuit::ENGINE_OIL).changeHours * 1.4);
	car.RestoreFluids(fluids);
}

bool Workshop::IsOnCar(const Part& part) const
{
	return CarOf(part) >= 0;	// (any of them)
}

pPart Workshop::CarriedOn(const Part& host) const
{
	auto it = m_carCarried.find(host.GetId());
	return it == m_carCarried.end() ? nullptr : FindPart(it->second);
}

pPart Workshop::CarHostOf(const Part& part) const
{
	for (const auto& [host, carried] : m_carCarried)
		if (carried == part.GetId())
			return FindPart(host);
	return nullptr;
}

bool Workshop::FitOntoCarPart(const pPart& host, const pPart& part)
{
	if (!host || !part || !FindPart(host->GetId()) || !FindPart(part->GetId()))
		return false;
	bool spring = host->GetType() == PartType::FRONT_STRUT && part->GetType() == PartType::FRONT_COIL_SPRING;
	bool tire = host->GetType() == PartType::WHEEL && part->GetType() == PartType::TIRE;
	if (!spring && !tire)
	{
		Log(std::format("A {} doesn't go on a {}.", part->GetName(), host->GetName()));
		return false;
	}
	if (spring ? !OwnsToolOfKind(HandToolKind::SPRING_COMPRESSOR) : !OwnsToolOfKind(HandToolKind::TIRE_IRONS))
	{
		Log(spring ? "It takes a strut spring compressor to get a spring onto its strut: they're sold at the parts computer."
			: "It takes tire irons and a bead breaker to get a tire onto its wheel: they're sold at the parts computer.");
		return false;
	}
	if (IsOnCar(*host) || IsOnCar(*part))
	{
		Log(std::format("Take it off the car first: the {} goes on {} off the car.", PartTypeName(part->GetType()), spring ? "its strut" : "its wheel"));
		return false;
	}
	if (CarriedOn(*host))
	{
		Log(std::format("There's already a {} on that {}.", PartTypeName(part->GetType()), host->GetName()));
		return false;
	}
	if (CarHostOf(*part))
	{
		Log(std::format("That {} is on another {}.", part->GetName(), PartTypeName(host->GetType())));
		return false;
	}
	m_carCarried[host->GetId()] = part->GetId();
	Log(spring ? std::format("Compressed the {} onto the {} and did up its top mount.", part->GetName(), host->GetName())
		: std::format("Mounted the {} on the {} and balanced it.", part->GetName(), host->GetName()));
	return true;
}

pPart Workshop::TakeOffCarPart(const pPart& host)
{
	if (!host || IsOnCar(*host))
		return nullptr;
	auto carried = CarriedOn(*host);
	if (!carried)
		return nullptr;
	bool strut = host->GetType() == PartType::FRONT_STRUT;
	if (!OwnsToolOfKind(strut ? HandToolKind::SPRING_COMPRESSOR : HandToolKind::TIRE_IRONS))
	{
		Log(strut ? "Not without a strut spring compressor: the spring's under tons of load." : "It takes tire irons and a bead breaker to get a tire off.");
		return nullptr;
	}
	m_carCarried.erase(host->GetId());
	Log(std::format("Took the {} off the {}.", carried->GetName(), host->GetName()));
	return carried;
}

bool Workshop::InstallCarPart(CarSlot slot, int index, const pPart& part)
{
	if (!m_car)
	{
		Log("The shop hasn't a car to fit it to.");
		return false;
	}
	auto& car = m_car->assembly;
	if (!part || std::find(m_inventory.begin(), m_inventory.end(), part) == m_inventory.end())
		return false;
	if (EngineOf(*part) >= 0)
	{
		Log(std::format("That {} is on engine {}.", part->GetName(), EngineOf(*part) + 1));
		return false;
	}
	if (IsOnCar(*part))
	{
		Log(std::format("That {} is on the car already.", part->GetName()));
		return false;
	}
	if (auto host = CarAssembly::CarriedBy(slot))
	{
		Log(std::format("The {} goes onto a {} off the car, and on with it.", PartTypeName(part->GetType()), PartTypeName(CarAssembly::SlotPartType(*host))));
		return false;
	}
	if (CarHostOf(*part))
	{
		Log(std::format("That {} is on a {}: it goes on with it.", part->GetName(), CarHostOf(*part)->GetName()));
		return false;
	}
	if (part->GetType() != CarAssembly::SlotPartType(slot))
	{
		Log(std::format("Only a {} goes there.", PartTypeName(CarAssembly::SlotPartType(slot))));
		return false;
	}
	// The clutch bolts to the flywheel on the back of the engine in the bay.
	if (slot == CarSlot::CLUTCH_DISC && car.GetEngine() >= 0 && !GetAssembly(car.GetEngine()).Get(AssemblySlot::FLYWHEEL, 0))
	{
		Log("The flywheel has to be on the engine first: the clutch bolts to it (with the gearbox out, there's room to fit it in the car).");
		return false;
	}
	// A strut goes in with its spring on it, a wheel with its tire.
	pPart carried = nullptr;
	std::optional<CarSlot> carriedSlot;
	if (slot == CarSlot::FRONT_STRUT || slot == CarSlot::WHEEL)
	{
		carriedSlot = slot == CarSlot::FRONT_STRUT ? CarSlot::FRONT_COIL_SPRING : CarSlot::TIRE;
		carried = CarriedOn(*part);
		if (!carried)
		{
			Log(slot == CarSlot::FRONT_STRUT ? std::format("Compress a front coil spring onto the {} first: it goes in with it.", part->GetName())
				: std::format("Mount a tire on the {} first: it goes on with it.", part->GetName()));
			return false;
		}
	}
	// Everything under it, and its own nuts and bolts, first. The carried part's place counts as ready.
	for (const auto& [other, otherIndex] : car.Prerequisites(slot, index))
		if (!car.IsFilled(other, otherIndex) && !(carriedSlot && other == *carriedSlot && otherIndex == index))
		{
			Log(std::format("The {} has to go on first.", car.PositionName(other, otherIndex)));
			return false;
		}

	std::string error;
	if (carried && !car.Install(*carriedSlot, index, carried, error))
	{
		Log(error);
		return false;
	}
	if (!car.Install(slot, index, part, error))
	{
		if (carried)
			car.Remove(*carriedSlot, index);
		Log(error);
		return false;
	}
	// Nuts and bolts go in finger tight.
	part->SetTorque(0.0);
	part->SetTightness(FastenerTightness::LOOSE);
	if (carried)
	{
		m_carCarried.erase(part->GetId());
		m_stats.partsFitted++;
	}
	m_stats.partsFitted++;
	if (slot == CarSlot::DIFFERENTIAL)
		Log(std::format("Fitted the differential: a {:.3f} final drive.", car.GetTune().finalDrive));
	CarChanged();
	return true;
}

bool Workshop::RemoveCarPart(CarSlot slot, int index)
{
	if (!m_car)
		return false;
	auto& car = m_car->assembly;
	auto part = car.Get(slot, index);
	if (!part)
		return false;
	if (slot == CarSlot::BODY_SHELL)
	{
		Log("The shell is the car: it stays where it is.");
		return false;
	}
	if (auto host = CarAssembly::CarriedBy(slot))
	{
		Log(std::format("The {} comes off with its {}.", part->GetName(), PartTypeName(CarAssembly::SlotPartType(*host))));
		return false;
	}
	auto carriedSlot = slot == CarSlot::FRONT_STRUT ? std::optional(CarSlot::FRONT_COIL_SPRING)
		: slot == CarSlot::WHEEL ? std::optional(CarSlot::TIRE) : std::nullopt;
	if (auto blocked = car.RemovalBlockedBy(slot, index))
	{
		Log(std::format("The {} has to come off first.", *blocked));
		return false;
	}
	if (CarAssembly::IsFastenerSlot(slot) && part->GetTightness() != FastenerTightness::LOOSE)
	{
		int head = PartCatalog::Get(part->GetType()).fastener.headMm;
		Log(std::format("Loosen the {} first: a {} mm socket on a ratchet or breaker bar, or a {} mm wrench.", part->GetName(), head, head));
		return false;
	}
	car.Remove(slot, index);
	part->SetTorque(0.0);
	part->SetTightness(FastenerTightness::LOOSE);
	if (carriedSlot)
		if (auto carried = car.Remove(*carriedSlot, index))
			m_carCarried[part->GetId()] = carried->GetId();
	if (PartCatalog::Get(part->GetType()).singleUse && part->GetCondition() != PartCondition::DAMAGED)
		part->MarkDamaged("Single use: it was fitted once and can't go back on");
	CarChanged();
	return true;
}

std::optional<std::string> Workshop::CarTurningProblem(CarSlot slot, int index, size_t toolIndex, int sizeMm, bool tightening) const
{
	if (!m_car)
		return "There's no car.";
	return TurningProblemFor(CarAssembly::Holes(slot), m_car->assembly.Get(slot, index), toolIndex, sizeMm, tightening);
}

bool Workshop::TightenCarFastener(CarSlot slot, int index, size_t toolIndex, int sizeMm, double setNm)
{
	if (auto problem = CarTurningProblem(slot, index, toolIndex, sizeMm, true))
	{
		Log(*problem);
		return false;
	}
	DoUp(*CarAssembly::Holes(slot), *m_car->assembly.Get(slot, index), toolIndex, setNm);
	CarChanged();
	return true;
}

bool Workshop::LoosenCarFastener(CarSlot slot, int index, size_t toolIndex, int sizeMm)
{
	if (auto problem = CarTurningProblem(slot, index, toolIndex, sizeMm, false))
	{
		Log(*problem);
		return false;
	}
	auto part = m_car->assembly.Get(slot, index);
	part->SetTorque(0.0);
	part->SetTightness(FastenerTightness::LOOSE);
	CarChanged();
	return true;
}

std::optional<std::pair<size_t, int>> Workshop::BestTurningTool(PartType fastener, double torqueNm, bool tightening) const
{
	int size = PartCatalog::Get(fastener).fastener.headMm;
	if (size <= 0)
		return std::nullopt;
	const auto& tools = ToolCatalog::All();
	auto has = [&](const MeasuringTool& tool) { return std::find(tool.sizes.begin(), tool.sizes.end(), size) != tool.sizes.end(); };
	bool sockets = false;
	for (size_t i = 0; i < tools.size(); i++)
		if (tools[i].handKind == HandToolKind::SOCKETS && OwnsTool(i) && has(tools[i]))
			sockets = true;
	auto find = [&](HandToolKind kind, double atLeast) -> std::optional<size_t> {
		for (size_t i = 0; i < tools.size(); i++)
			if (tools[i].handKind == kind && OwnsTool(i) && (atLeast <= 0.0 || tools[i].maxTorque >= atLeast))
				return i;
		return std::nullopt;
	};
	if (sockets)
	{
		if (tightening)
			if (auto wrench = find(HandToolKind::TORQUE_WRENCH, torqueNm))
				return std::pair{ *wrench, size };
		if (torqueNm > 70.0 || !tightening)
			if (auto bar = find(HandToolKind::BREAKER_BAR, 0.0))
				return std::pair{ *bar, size };
		if (auto ratchet = find(HandToolKind::RATCHET, 0.0))
			return std::pair{ *ratchet, size };
		if (auto bar = find(HandToolKind::BREAKER_BAR, 0.0))
			return std::pair{ *bar, size };
	}
	for (size_t i = 0; i < tools.size(); i++)
		if (tools[i].handKind == HandToolKind::COMBINATION_WRENCH && OwnsTool(i) && has(tools[i]))
			return std::pair{ i, size };
	return std::nullopt;
}

int Workshop::GetEngineInCar() const
{
	return m_car ? m_car->assembly.GetEngine() : -1;
}

bool Workshop::ServiceableInCar(AssemblySlot slot)
{
	switch (slot)
	{
		case AssemblySlot::VALVE_COVER:
		case AssemblySlot::VALVE_COVER_GASKET:
		case AssemblySlot::VALVE_COVER_BOLT:
		case AssemblySlot::SPARK_PLUG:
		case AssemblySlot::PLUG_LEAD:
		case AssemblySlot::CARBURETTOR:
		case AssemblySlot::CARBURETTOR_BOLT:
		case AssemblySlot::OIL_FILTER:
		case AssemblySlot::DRAIN_PLUG:	// (from under the car, to drain the oil)
			return true;
		default:
			return false;
	}
}

std::optional<std::string> Workshop::EngineWorkProblem(int engine, AssemblySlot slot) const
{
	bool flywheel = AtFlywheel(slot);
	if (engine == m_engineOnDyno)
		return "That engine is on the dyno: lift it back onto a stand with the engine hoist to work on it.";
	if (engine == m_engineOnHoist)
		return flywheel ? std::nullopt
			: std::optional<std::string>("That engine is hanging from the hoist: lower it onto a stand to work on it (only the flywheel goes on or off there).");
	if (int inCar = CarWithEngine(engine); inCar >= 0)
	{
		if (flywheel)
		{
			const auto& car = m_cars[inCar]->assembly;
			if (car.IsFilled(CarSlot::CLUTCH_DISC, 0) || car.IsFilled(CarSlot::TRANSMISSION, 0))
				return "The clutch and gearbox are in the way: take them off the car to get at the flywheel.";
			return std::nullopt;
		}
		if (!ServiceableInCar(slot))
			return "That engine is in the car: only the service parts (valve cover, plugs and leads, carburettor, oil filter, oil drain plug) can be got at, "
				"and the flywheel with the gearbox out. Lift it out with the hoist for the rest.";
		return std::nullopt;
	}
	if (flywheel)
		return "There's no room at the flywheel on the stand: its plate is bolted over the back of the block. Hang the engine from the "
			"hoist to fit or take off the flywheel (or do it in the car, before the clutch and gearbox go on).";
	return std::nullopt;
}

bool Workshop::LowerEngineIntoCar()
{
	if (!m_car)
		return false;
	if (m_engineOnHoist < 0)
	{
		Log("There's no engine on the hoist to lower in.");
		return false;
	}
	std::string error;
	if (!m_car->assembly.SetEngine(m_engineOnHoist, error))
	{
		Log(error);
		return false;
	}
	Log(std::format("Lowered engine {} into the car's bay. Bolt it to its mounts, then the clutch and gearbox go on behind it.", m_engineOnHoist + 1));
	m_activeEngine = m_engineOnHoist;
	m_engineOnHoist = -1;
	CarChanged();
	return true;
}

bool Workshop::HoistEngineFromCar()
{
	if (!m_car || m_car->assembly.GetEngine() < 0)
		return false;
	if (!m_ownsHoist)
	{
		Log("It takes an engine hoist to lift the engine out.");
		return false;
	}
	if (m_engineOnHoist >= 0)
	{
		Log(std::format("Engine {} is hanging from the hoist already: lower it first.", m_engineOnHoist + 1));
		return false;
	}
	auto& car = m_car->assembly;
	if (auto blocked = car.RemovalBlockedBy(CarSlot::ENGINE, 0))
	{
		Log(std::format("The {} has to come off first.", *blocked));
		return false;
	}
	int engine = car.TakeEngine();
	m_engineOnHoist = engine;
	m_activeEngine = engine;
	Log(std::format("Lifted engine {} out of the car onto the hoist. Its oil and coolant were drained first.", engine + 1));
	CarChanged();
	return true;
}

double Workshop::FluidUnitLitres(FluidProduct product)
{
	switch (product)
	{
		case FluidProduct::OIL_5W30:
		case FluidProduct::OIL_10W30:
		case FluidProduct::OIL_20W50:
		case FluidProduct::COOLANT_50_50:
		case FluidProduct::WATER:				return 4.0;
		case FluidProduct::DOT3:
		case FluidProduct::DOT4:
		case FluidProduct::DOT5_SILICONE:		return 0.5;
		case FluidProduct::GEAR_OIL_75W90_GL4:
		case FluidProduct::GEAR_OIL_80W90_GL5:
		case FluidProduct::ATF:					return 1.0;
		case FluidProduct::PETROL_87:
		case FluidProduct::PETROL_91:
		case FluidProduct::DIESEL:				return 10.0;
		case FluidProduct::COUNT:				break;
	}
	return 0.0;
}

double Workshop::FluidUnitPrice(FluidProduct product)
{
	switch (product)
	{
		case FluidProduct::OIL_5W30:			return 26.0;
		case FluidProduct::OIL_10W30:			return 22.0;
		case FluidProduct::OIL_20W50:			return 24.0;
		case FluidProduct::COOLANT_50_50:		return 18.0;
		case FluidProduct::WATER:				return 3.0;
		case FluidProduct::DOT3:				return 7.0;
		case FluidProduct::DOT4:				return 9.0;
		case FluidProduct::DOT5_SILICONE:		return 15.0;
		case FluidProduct::GEAR_OIL_75W90_GL4:	return 13.0;
		case FluidProduct::GEAR_OIL_80W90_GL5:	return 12.0;
		case FluidProduct::ATF:					return 9.0;
		case FluidProduct::PETROL_87:			return 14.0;
		case FluidProduct::PETROL_91:			return 17.0;
		case FluidProduct::DIESEL:				return 15.0;
		case FluidProduct::COUNT:				break;
	}
	return 0.0;
}

std::optional<size_t> Workshop::ContainerToolFor(FluidProduct product)
{
	const auto& tools = ToolCatalog::All();
	for (size_t i = 0; i < tools.size(); i++)
		if (tools[i].handKind == HandToolKind::FLUID_CONTAINER && tools[i].fluidProduct == static_cast<int>(product))
			return i;
	return std::nullopt;
}

std::optional<size_t> Workshop::CapToolFor(FluidCircuit circuit)
{
	const auto& tools = ToolCatalog::All();
	for (size_t i = 0; i < tools.size(); i++)
		if (tools[i].handKind == HandToolKind::FILLER_CAP && tools[i].capCircuit == static_cast<int>(circuit))
			return i;
	return std::nullopt;
}

double Workshop::ContainerLitres(size_t container) const
{
	auto it = m_containerLitres.find(container);
	return it == m_containerLitres.end() ? 0.0 : it->second;
}

double Workshop::FluidStock(FluidProduct product) const
{
	const auto tool = ContainerToolFor(product);
	if (!tool)
		return 0.0;
	double litres = 0.0;
	for (size_t copy = 0; copy < static_cast<size_t>(MAX_TOOL_COPIES); copy++)
		if (const size_t id = ToolCatalog::IdOf(*tool, copy); OwnsTool(id))
			litres += ContainerLitres(id);
	return litres;
}

int Workshop::FluidRoom(FluidProduct product) const
{
	const auto tool = ContainerToolFor(product);
	return tool ? MAX_TOOL_COPIES - ToolCount(*tool) : 0;
}

bool Workshop::AddFluidContainer(FluidProduct product, double litres)
{
	const auto tool = ContainerToolFor(product);
	if (!tool)
		return false;
	if (m_toolCounts.size() <= *tool)
		m_toolCounts.resize(ToolCatalog::All().size(), 0);
	// A spent one's copy first (it was thrown away when it ran dry): this is that copy again, full.
	for (size_t copy = 0; copy < static_cast<size_t>(m_toolCounts[*tool]); copy++)
		if (const size_t id = ToolCatalog::IdOf(*tool, copy); ContainerLitres(id) <= 0.0)
		{
			m_containerLitres[id] = litres;
			return true;
		}
	if (m_toolCounts[*tool] >= MAX_TOOL_COPIES)
		return false;
	m_containerLitres[ToolCatalog::IdOf(*tool, static_cast<size_t>(m_toolCounts[*tool]))] = litres;
	m_toolCounts[*tool]++;
	return true;
}

double Workshop::PourFromContainer(FluidCircuit circuit, size_t container, double litres, bool quiet)
{
	const auto& tool = ToolCatalog::Of(container);
	if (!m_car || tool.handKind != HandToolKind::FLUID_CONTAINER || !OwnsTool(container))
		return 0.0;
	const auto product = static_cast<FluidProduct>(tool.fluidProduct);
	auto& car = m_car->assembly;
	if (car.CapOn(circuit))
	{
		if (!quiet)
			Log(std::format("The {}'s cap is on: take it off first.", FluidCircuitLower(circuit)));
		return 0.0;
	}
	double& left = m_containerLitres[container];
	litres = (std::min)(litres, left);
	if (litres <= 0.0)
		return 0.0;
	double poured = 0.0;
	// (Oil poured in with the pan's drain plug out goes straight through.)
	const auto open = circuit == FluidCircuit::ENGINE_OIL && !CarOilDrainPlugIn() ? std::optional<std::string>("oil drain plug") : car.CircuitOpen(circuit);
	if (open)
	{
		left -= litres;
		if (!quiet)
			Log(std::format("The {} ran straight out onto the floor: the {} isn't on.", FluidProductName(product), *open));
	}
	else
	{
		poured = car.Fill(circuit, product, litres);
		left -= poured;
		if (!quiet && poured < litres)
			Log(std::format("Poured {:.2f} l of {} into the {}: it's full.", poured, FluidProductName(product), FluidCircuitName(circuit)));
		else if (!quiet)
			Log(std::format("Poured {:.2f} l of {} into the {}.", poured, FluidProductName(product), FluidCircuitName(circuit)));
	}
	// Run dry: the empty goes in the bin (it's gone from the shop: OwnsTool), its copy kept for the next one bought.
	if (left <= 1e-6)
	{
		left = 0.0;
		Log(std::format("The {} is empty: it goes in the bin.", FluidProductName(product)));
	}
	CarChanged();
	return poured;
}

void Workshop::IssueCarCaps(CarBuild& build)
{
	if (m_toolCounts.size() < ToolCatalog::All().size())
		m_toolCounts.resize(ToolCatalog::All().size(), 0);
	for (int c = 0; c < FLUID_CIRCUITS; c++)
	{
		const auto circuit = static_cast<FluidCircuit>(c);
		const auto tool = CapToolFor(circuit);
		if (!tool || build.assembly.CapOn(circuit) || m_toolCounts[*tool] >= MAX_TOOL_COPIES)
			continue;
		build.assembly.SetCap(circuit, ToolCatalog::IdOf(*tool, static_cast<size_t>(m_toolCounts[*tool])));
		m_toolCounts[*tool]++;
	}
	build.capsIssued = true;
}

bool Workshop::TakeCarCap(FluidCircuit circuit)
{
	if (!m_car || !m_car->assembly.CapOn(circuit))
		return false;
	m_car->assembly.SetCap(circuit, std::nullopt);
	Log(std::format("Took the {}'s cap off.", FluidCircuitLower(circuit)));
	CarChanged();
	return true;
}

bool Workshop::PutCarCap(FluidCircuit circuit, size_t cap)
{
	const auto& tool = ToolCatalog::Of(cap);
	if (!m_car || tool.handKind != HandToolKind::FILLER_CAP)
		return false;
	if (tool.capCircuit != static_cast<int>(circuit))
	{
		Log(std::format("The {} doesn't fit the {}'s filler.", tool.name, FluidCircuitLower(circuit)));
		return false;
	}
	if (m_car->assembly.CapOn(circuit))
	{
		Log(std::format("The {}'s cap is on already.", FluidCircuitLower(circuit)));
		return false;
	}
	TakeCapOff(cap);
	m_car->assembly.SetCap(circuit, cap);
	Log(std::format("Put the {} back on.", tool.name));
	CarChanged();
	return true;
}

void Workshop::TakeCapOff(size_t cap)
{
	if (auto on = FindCap(cap))
	{
		m_cars[on->first]->assembly.SetCap(on->second, std::nullopt);
		if (on->first == m_activeCar)
			CarChanged();
	}
}

std::optional<std::pair<int, FluidCircuit>> Workshop::FindCap(size_t cap) const
{
	for (int car = 0; car < GetCarCount(); car++)
		for (int c = 0; c < FLUID_CIRCUITS; c++)
			if (m_cars[car]->assembly.CapOn(static_cast<FluidCircuit>(c)) == cap)
				return std::pair{ car, static_cast<FluidCircuit>(c) };
	return std::nullopt;
}

void Workshop::UpgradeCarKit()
{
	// A drain pan, for a shop from before it had one.
	const auto& tools = ToolCatalog::All();
	for (size_t i = 0; i < tools.size(); i++)
		if (tools[i].handKind == HandToolKind::DRAIN_PAN && ToolCount(i) == 0)
		{
			if (m_toolCounts.size() <= i)
				m_toolCounts.resize(tools.size(), 0);
			m_toolCounts[i] = 1;
		}
	for (auto& car : m_cars)
		if (!car->capsIssued)
			IssueCarCaps(*car);
	// The stock from before, in full containers (and what's over, in one more part full).
	for (int p = 0; p < FLUID_PRODUCTS; p++)
	{
		const auto product = static_cast<FluidProduct>(p);
		const double unit = FluidUnitLitres(product);
		for (double& litres = m_legacyFluidStock[p]; litres > 1e-6;)
		{
			const double into = (std::min)(litres, unit);
			if (!AddFluidContainer(product, into))
				break;
			litres -= into;
		}
		// More than the containers there's room for hold: what's over goes into them, shared out, rather than being lost.
		if (const auto tool = ContainerToolFor(product); tool && m_legacyFluidStock[p] > 1e-6 && ToolCount(*tool) > 0)
		{
			const double each = m_legacyFluidStock[p] / ToolCount(*tool);
			for (size_t copy = 0; copy < static_cast<size_t>(MAX_TOOL_COPIES); copy++)
				if (const size_t id = ToolCatalog::IdOf(*tool, copy); OwnsTool(id))
					m_containerLitres[id] += each;
		}
		m_legacyFluidStock[p] = 0.0;
	}
}

bool Workshop::OwnsToolOfKind(HandToolKind kind) const
{
	const auto& tools = ToolCatalog::All();
	for (size_t i = 0; i < tools.size(); i++)
		if (tools[i].handKind == kind && OwnsTool(i))
			return true;
	return false;
}

bool Workshop::CarOilDrainPlugIn() const
{
	const int engine = GetEngineInCar();
	return engine < 0 || GetAssembly(engine).Get(AssemblySlot::DRAIN_PLUG, 0) != nullptr;
}

double Workshop::DrainCar(FluidCircuit circuit, bool caught)
{
	if (!m_car)
		return 0.0;
	// The oil comes out of the pan's drain hole: its plug (a fastener of the engine's) undone and out first.
	if (circuit == FluidCircuit::ENGINE_OIL && CarOilDrainPlugIn())
	{
		Log("The oil drain plug is in: undo it with a spanner and take it out, the pan under it.");
		return 0.0;
	}
	double drained = m_car->assembly.Drain(circuit);
	// Where it went: in the drain pan, or (with none under it) all over the floor.
	const std::string into = caught ? "into the drain pan" : "all over the floor (no drain pan under it): wipe it up with paint thinner and a rag";
	if (drained <= 0.0)
		Log(std::format("There's nothing in the {} to drain.", FluidCircuitLower(circuit)));
	else if (circuit == FluidCircuit::ENGINE_OIL)
		Log(std::format("{:.2f} l of oil came out of the pan's drain hole, {}. The plug goes back in (on a new washer) before it's filled.", drained, into));
	else if (circuit == FluidCircuit::BRAKES || circuit == FluidCircuit::CLUTCH)
		Log(std::format("Opened the nipple and let {:.2f} l of {} out, {}. Its lines are full of air now.", drained, FluidCircuitLower(circuit), into));
	else
		Log(std::format("Undid the drain plug and let {:.2f} l of {} out, {}. Did the plug back up.", drained, FluidCircuitLower(circuit), into));
	CarChanged();
	return drained;
}

namespace
{
	// What's seen in the bleeder's clear tube as the pedal goes down, by how much air was in that line.
	const char* SeenInBleedTube(double air)
	{
		return air > 0.6 ? "mostly air, spitting" : air > 0.2 ? "fluid with a stream of bubbles" : air > 0.03 ? "fluid with the odd bubble"
			: "clear fluid, no bubbles";
	}
}

bool Workshop::AttachBleeder(BleedPoint point, size_t tool)
{
	if (!m_car)
		return false;
	auto& car = m_car->assembly;
	int index = 0;
	const CarSlot slot = CarAssembly::BleedNipplePart(point, index);
	if (!car.IsFilled(slot, index))
	{
		Log(std::format("There's no {} on to bleed.", CarSlotName(slot)));
		return false;
	}
	if (car.BleederOn(point))
	{
		Log(std::format("There's already a bleeder on the {} nipple.", BleedPointLower(point)));
		return false;
	}
	DetachBleeder(tool);
	car.SetBleeder(point, tool);
	Log(std::format("Bleeder on the {} nipple and the nipple cracked open: pump the {} pedal from the driver's seat.", BleedPointLower(point),
		point == BleedPoint::CLUTCH ? "clutch" : "brake"));
	CarChanged();
	return true;
}

void Workshop::DetachBleeder(size_t tool)
{
	auto on = FindBleeder(tool);
	if (!on)
		return;
	m_cars[on->first]->assembly.SetBleeder(on->second, std::nullopt);
	Log(std::format("Closed the {} nipple and took the bleeder off.", BleedPointLower(on->second)));
	if (on->first == m_activeCar)
		CarChanged();
}

std::optional<std::pair<int, BleedPoint>> Workshop::FindBleeder(size_t tool) const
{
	for (int car = 0; car < GetCarCount(); car++)
		for (int p = 0; p < BLEED_POINTS; p++)
			if (m_cars[car]->assembly.BleederOn(static_cast<BleedPoint>(p)) == tool)
				return std::pair{ car, static_cast<BleedPoint>(p) };
	return std::nullopt;
}

bool Workshop::PumpCarPedal(FluidCircuit circuit)
{
	if (!m_car || !m_car->assembly.BleederOpen(circuit))
		return false;
	auto& car = m_car->assembly;
	if (auto open = car.CircuitOpen(circuit))
	{
		Log(std::format("The pedal does nothing: the {} isn't on.", *open));
		return true;
	}
	if (car.GetFluid(circuit).Total() <= 0.0)
	{
		Log(std::format("The pedal pumps air: fill the {} reservoir first.", FluidCircuitName(circuit)));
		return true;
	}
	// Each open nipple of that circuit gets its share of the stroke; what's seen in each tube is how much air was in it.
	std::string seen;
	for (int p = 0; p < BLEED_POINTS; p++)
	{
		const auto point = static_cast<BleedPoint>(p);
		if (!car.BleederOn(point) || (point == BleedPoint::CLUTCH) != (circuit == FluidCircuit::CLUTCH))
			continue;
		const double before = car.GetFluid(circuit).air[static_cast<size_t>(point)];
		car.Bleed(point);
		seen += std::format("{}{} bleeder: {}", seen.empty() ? "" : "; ", BleedPointName(point), SeenInBleedTube(before));
	}
	Log(seen + ".");
	const auto& spec = car.GetSpec().Fluid(circuit);
	if (car.GetFluid(circuit).Total() < spec.level.min * 0.6)
		Log("The reservoir ran dry and the master cylinder drew air: top it up and start again from the farthest wheel.");
	else if (car.GetFluid(circuit).Total() < spec.level.min)
		Log("The reservoir is below MIN: top it up before it runs dry.");
	CarChanged();
	return true;
}

bool Workshop::CarStarts() const
{
	if (!m_car || GetEngineInCar() < 0)
		return false;
	DynoResult running;
	running.ran = true;
	running.compressionRatio = m_engines[GetEngineInCar()]->lastDyno.compressionRatio;
	return RoadTest::Run(m_car->assembly, running, GetAssembly(GetEngineInCar()).IsComplete()).starts;
}

bool Workshop::BurpCarCoolant()
{
	if (!m_car)
		return false;
	if (!CarStarts())
	{
		Log("It has to start and run to burp the cooling system.");
		return false;
	}
	if (m_car->assembly.CapOn(FluidCircuit::COOLANT))
	{
		Log("Take the radiator cap off first: the air comes out of its filler neck.");
		return false;
	}
	auto& car = m_car->assembly;
	car.BurpCoolant(BURP_MINUTES);
	car.Run(BURP_MINUTES / 60.0, BURP_FUEL);
	const auto& coolant = car.GetFluid(FluidCircuit::COOLANT);
	const auto& spec = car.GetSpec().Fluid(FluidCircuit::COOLANT);
	Log(std::format("Ran it with the radiator cap off till the thermostat opened: {}{}", coolant.airPocket > 0.05 ? "still burping bubbles"
		: "the bubbles have stopped", coolant.Total() < spec.level.min ? ", and the level dropped: top it up." : "."));
	CarChanged();
	return true;
}

void Workshop::SetCarTune(const CarTune& tune)
{
	if (!m_car)
		return;
	auto& current = m_car->assembly.GetTune();
	CarTune set = tune;
	// What the screws, the distributor and the tie rods physically go to. The final drive is whatever diff is in.
	set.ignitionTiming = std::clamp(set.ignitionTiming, -10.0, 30.0);
	set.idleSpeed = std::clamp(set.idleSpeed, 400.0, 2000.0);
	set.idleMixture = std::clamp(set.idleMixture, 0.0, 6.0);
	set.mainJet = std::clamp(std::round(set.mainJet), 90.0, 125.0);
	set.intakeLash = std::clamp(set.intakeLash, 0.0, 0.6);
	set.exhaustLash = std::clamp(set.exhaustLash, 0.0, 0.7);
	set.frontCamber = current.frontCamber;
	set.caster = std::clamp(set.caster, 0.5, 5.0);
	set.frontToe = std::clamp(set.frontToe, -10.0, 10.0);
	for (auto& pressure : set.tirePressure)
		pressure = std::clamp(pressure, 0.0, 350.0);
	set.finalDrive = current.finalDrive;
	current = set;
	CarChanged();
}

void Workshop::CoolCarEngine(double seconds, int runningCar)
{
	for (int car = 0; car < GetCarCount(); car++)
		if (car != runningCar && m_cars[car]->engineHeat > 0.0)
			m_cars[car]->engineHeat = (std::max)(0.0, m_cars[car]->engineHeat - seconds / CAR_ENGINE_COOL_SECONDS);
}

const RoadTestResult& Workshop::RoadTestCar()
{
	static const RoadTestResult NONE = {};
	if (!m_car)
		return NONE;
	SettleCarPlugs();
	auto& build = *m_car;
	int engine = GetEngineInCar();
	DynoResult pull;
	bool complete = false;
	if (engine >= 0)
	{
		complete = GetAssembly(engine).IsComplete();
		if (complete)
			pull = Dyno::Run(m_engines[engine]->assembly);
	}
	if (build.lastRoadTest.ran)
		build.previousRoadTest = build.lastRoadTest;
	build.lastRoadTest = RoadTest::Run(build.assembly, pull, complete);
	build.roadTestCurrent = true;
	const auto& result = build.lastRoadTest;
	if (result.starts)
	{
		build.assembly.Run(result.drives ? ROAD_TEST_HOURS : 0.05, result.drives ? ROAD_TEST_FUEL : 0.2);
		// Run, it's hot (just started, warm).
		build.engineHeat = (std::max)(build.engineHeat, result.drives ? 1.0 : 0.4);
	}
	if (result.drives && result.stops && result.steers)
		Log(std::format("Road test: 0-100 km/h in {:.1f} s, the quarter mile in {:.1f} s, {:.0f} km/h flat out, {:.1f} m to stop from 100.",
			result.zeroToHundred, result.quarterMile, result.topSpeed, result.brakingDistance));
	else if (!result.findings.empty())
		Log(std::format("Road test: {}", result.findings.back().message));
	return result;
}

void Workshop::DevAssembleCar()
{
	if (!m_car)
	{
		m_cars.push_back(std::make_unique<CarBuild>(CarSpec::LE86Coupe()));
		SetActiveCar(GetCarCount() - 1);
	}
	int fitted = FitNewCarParts();
	// Every plug and pipe's union done up before it's filled: with one undone the fluid runs straight out, and there's
	// nothing in the lines to bleed.
	DevPlugCar(true);
	FillAndBleedCar();
	CarChanged();
	Log(std::format("Dev: fitted {} new OEM parts to the car, and filled and bled it.", fitted));
}

int Workshop::FitNewCarParts()
{
	auto& car = m_car->assembly;
	// The active engine goes in if it's finished and free.
	if (car.GetEngine() < 0 && !car.MissingPrerequisite(CarSlot::ENGINE, 0))
	{
		int engine = m_activeEngine;
		if (GetAssembly(engine).IsComplete() && engine != m_engineOnDyno && engine != m_engineOnHoist && CarWithEngine(engine) < 0)
		{
			std::string error;
			car.SetEngine(engine, error);
		}
	}
	int fitted = 0;
	for (bool progress = true; progress;)
	{
		progress = false;
		for (int s = 0; s < static_cast<int>(CarSlot::COUNT); s++)
		{
			auto slot = static_cast<CarSlot>(s);
			for (int index = 0; index < car.SlotCount(slot); index++)
			{
				if (car.IsFilled(slot, index) || car.MissingPrerequisite(slot, index))
					continue;
				if (slot == CarSlot::ENGINE)
				{
					int engine = m_activeEngine;
					std::string error;
					if (GetAssembly(engine).IsComplete() && engine != m_engineOnDyno && engine != m_engineOnHoist && car.SetEngine(engine, error))
						progress = true;
					continue;
				}
				auto part = PartFactory::Create(CarAssembly::SlotPartType(slot), *m_spec, PartQuality::OEM, PartCondition::NEW,
					slot == CarSlot::DIFFERENTIAL ? car.GetSpec().finalDriveNominal : 0.0);
				m_inventory.push_back(part);
				std::string error;
				if (car.Install(slot, index, part, error))
				{
					if (const auto* holes = CarAssembly::Holes(slot))
					{
						part->SetTorque(holes->torqueNm);
						part->SetTightness(FastenerTightness::TORQUED);
					}
					fitted++;
					progress = true;
				}
			}
		}
	}
	return fitted;
}

void Workshop::FillAndBleedCar()
{
	// Full of the right stuff, bled and burped.
	auto& car = m_car->assembly;
	const auto& spec = car.GetSpec();
	for (int c = 0; c < FLUID_CIRCUITS; c++)
	{
		auto circuit = static_cast<FluidCircuit>(c);
		const auto& fs = spec.Fluid(circuit);
		car.Fill(circuit, fs.product, (circuit == FluidCircuit::FUEL ? 30.0 : fs.level.max) - car.GetFluid(circuit).Total());
	}
	// Each line bled till nothing but fluid comes out, the reservoir topped up after every go so it never runs dry
	// (which would let air back into them all).
	for (int p = 0; p < BLEED_POINTS; p++)
	{
		auto point = static_cast<BleedPoint>(p);
		auto circuit = point == BleedPoint::CLUTCH ? FluidCircuit::CLUTCH : FluidCircuit::BRAKES;
		if (car.CircuitOpen(circuit))
			continue;
		for (int go = 0; go < 60 && car.GetFluid(circuit).air[p] > 0.0; go++)
		{
			car.Bleed(point);
			car.Fill(circuit, spec.Fluid(circuit).product, 1.0);
		}
	}
	for (int go = 0; go < 4 && car.GetFluid(FluidCircuit::COOLANT).airPocket > 0.0; go++)
	{
		car.BurpCoolant(BURP_MINUTES);
		car.Fill(FluidCircuit::COOLANT, FluidProduct::COOLANT_50_50, 1.0);
	}
}

int Workshop::DevStripCar(PartType type)
{
	if (!m_car)
		return 0;
	auto& car = m_car->assembly;
	int removed = 0;
	auto slot = CarAssembly::SlotFor(type);
	if (slot == CarSlot::COUNT && !IsCarPart(type) && GetEngineInCar() >= 0)
	{
		// A service part off the engine in the car (the valve cover for the lash), its bolts out first.
		auto engineSlot = EngineAssembly::SlotFor(type);
		if (!ServiceableInCar(engineSlot) || EngineAssembly::Holes(engineSlot))
			return 0;
		SetActiveEngine(GetEngineInCar());
		for (int index = 0; index < GetAssembly().SlotCount(engineSlot); index++)
		{
			if (!GetAssembly().Get(engineSlot, index))
				continue;
			for (int h = 0; h < static_cast<int>(AssemblySlot::COUNT); h++)
			{
				auto holeSlot = static_cast<AssemblySlot>(h);
				const auto* holes = EngineAssembly::Holes(holeSlot);
				if (!holes || holes->parent != type)
					continue;
				for (int i = index * holes->perParent; i < (index + 1) * holes->perParent; i++)
					if (auto bolt = GetAssembly().Get(holeSlot, i))
					{
						bolt->SetTightness(FastenerTightness::LOOSE);
						RemovePart(holeSlot, i);
					}
			}
			if (RemovePart(engineSlot, index))
				removed++;
		}
		Log(std::format("Dev: took {} {} off the engine in the car.", removed, PartTypeName(type)));
		return removed;
	}
	if (slot == CarSlot::COUNT || slot == CarSlot::BODY_SHELL || CarAssembly::IsFastenerSlot(slot))
		return 0;
	for (int index = 0; index < car.SlotCount(slot); index++)
	{
		if (!car.Get(slot, index))
			continue;
		// Its own nuts and bolts out first, loose in the shop.
		for (int h = static_cast<int>(CarSlot::STRUT_MOUNT_NUT); h < static_cast<int>(CarSlot::COUNT); h++)
		{
			const auto* holes = CarAssembly::Holes(static_cast<CarSlot>(h));
			if (holes->parent != type)
				continue;
			for (int i = index * holes->perParent; i < (index + 1) * holes->perParent; i++)
				if (auto bolt = car.Remove(static_cast<CarSlot>(h), i))
				{
					bolt->SetTorque(0.0);
					bolt->SetTightness(FastenerTightness::LOOSE);
				}
		}
		if (auto part = car.Remove(slot, index))
		{
			// A spring or tire comes off with it, still on it.
			if (slot == CarSlot::FRONT_STRUT || slot == CarSlot::WHEEL)
				if (auto carried = car.Remove(slot == CarSlot::FRONT_STRUT ? CarSlot::FRONT_COIL_SPRING : CarSlot::TIRE, index))
					m_carCarried[part->GetId()] = carried->GetId();
			removed++;
		}
	}
	CarChanged();
	Log(std::format("Dev: took {} {} off the car.", removed, PartTypeName(type)));
	return removed;
}

// ---- Saved as text ----

namespace
{
	void WriteRoadTest(Writer& out, const RoadTestResult& r)
	{
		out.Int(r.ran).Int(r.cranks).Int(r.starts).Int(r.drives).Int(r.stops).Int(r.steers).Int(r.failed)
			.Num(r.zeroToHundred).Num(r.quarterMile).Num(r.quarterMileSpeed).Num(r.topSpeed).Num(r.brakingDistance).Num(r.lateralGrip)
			.Num(r.fuelEconomy).Num(r.idleRpm).Num(r.healthScore).Int(r.findings.size());
		for (const auto& finding : r.findings)
			out.Int(static_cast<int>(finding.severity)).Str(finding.message);
	}

	RoadTestResult ReadRoadTest(Reader& in)
	{
		RoadTestResult r;
		r.ran = in.Int() != 0;
		r.cranks = in.Int() != 0;
		r.starts = in.Int() != 0;
		r.drives = in.Int() != 0;
		r.stops = in.Int() != 0;
		r.steers = in.Int() != 0;
		r.failed = in.Int() != 0;
		r.zeroToHundred = in.Num();
		r.quarterMile = in.Num();
		r.quarterMileSpeed = in.Num();
		r.topSpeed = in.Num();
		r.brakingDistance = in.Num();
		r.lateralGrip = in.Num();
		r.fuelEconomy = in.Num();
		r.idleRpm = in.Num();
		r.healthScore = in.Num();
		size_t findings = in.Count();
		for (size_t i = 0; i < findings && !in.Failed(); i++)
		{
			DynoFinding finding;
			finding.severity = in.Enumerated<FindingSeverity>(3);
			finding.message = in.Next();
			r.findings.push_back(finding);
		}
		return r;
	}

	// 2: no car sale (cars aren't sold). 3: the looms' plugs (a car from before has them all pushed home). 4: the
	// clips on the plugs' wires, and how the front seats are set.
	// 9: the engine harness folded into the main one (its lead to the battery, plug 11 in version 8, is gone).
	// 10: no engine harness slot (the slots after it moved down one).
	// 11: the pipes' unions (the brake lines', the clutch's and the fuel lines'): a car from before has them all done up.
	// 12: inner tie rods, a slot after the steering rack (the slots after it moved up one): a car from before has them on
	// wherever its tie rod ends are.
	// 13: the clutch slave cylinder's two bolts (a slot after the lug nuts, so none moved): a car from before has them in,
	// done up, wherever its slave cylinder is.
	// 14: the ignition switch, a slot after the tires (the slots after it moved up one), and which ignition switch each of
	// the car keys is cut for: a car from before has one in its dash, used, and the shop's first key cut for it.
	// 15: a car for each player: how many cars, and for each whose it is before the rest of it. One from before is the
	// one car, nobody's.
	constexpr long long CAR_TEXT_VERSION = 17;
	constexpr long long FIRST_SLOT_SHIFTED_IN_14 = static_cast<long long>(CarSlot::IGNITION_SWITCH);
	constexpr int CAR_PLUGS_BEFORE_PIPES = 11;
	constexpr long long OLD_ENGINE_HARNESS_SLOT = static_cast<long long>(CarSlot::FUSE_BOX) - 1;	// where it was before version 10
	constexpr long long FIRST_SLOT_SHIFTED_IN_12 = static_cast<long long>(CarSlot::TIE_ROD_INNER);
	constexpr int CAR_PLUGS_IN_VERSION_8 = 12;
	constexpr int CAR_PLUGS_BEFORE_ENGINE_BATTERY = 11;	// and before version 8 gave the engine harness its lead to the battery
	constexpr int CAR_PLUGS_BEFORE_GROUND = 10;	// how many plugs a car had before version 5 gave it a ground strap
}

std::string Workshop::CarState() const
{
	std::vector<std::string> bodies;
	for (int car = 0; car < GetCarCount(); car++)
		bodies.push_back(CarBodyText(car));
	return JoinCarState(bodies, CarSharedText());
}

std::string Workshop::JoinCarState(const std::vector<std::string>& bodies, const std::string& shared)
{
	// (Each piece ends with the space after its last value, as Writer leaves it: they just go end to end.)
	std::string text = Writer().Int(CAR_TEXT_VERSION).Int(bodies.size()).Text();
	for (const auto& body : bodies)
		text += body;
	return text + shared;
}

std::string Workshop::CarBodyText(int carNumber) const
{
	Writer out;
	if (carNumber >= 0 && carNumber < GetCarCount())
	{
		const auto& build = m_cars[carNumber];
		out.Int(build->owner);
		const auto& car = build->assembly;
		std::vector<std::array<long long, 3>> installed;
		for (int slot = 0; slot < static_cast<int>(CarSlot::COUNT); slot++)
			for (int index = 0; index < car.SlotCount(static_cast<CarSlot>(slot)); index++)
				if (auto part = car.Get(static_cast<CarSlot>(slot), index))
					installed.push_back({ slot, index, part->GetId() });
		out.Int(installed.size());
		for (const auto& [slot, index, id] : installed)
			out.Int(slot).Int(index).Int(id);
		out.Int(car.GetEngine());
		for (int circuit = 0; circuit < FLUID_CIRCUITS; circuit++)
		{
			const auto& fluid = car.GetFluid(static_cast<FluidCircuit>(circuit));
			out.Int(fluid.litres.size());
			for (const auto& [product, litres] : fluid.litres)
				out.Int(static_cast<int>(product)).Num(litres);
			for (double air : fluid.air)
				out.Num(air);
			out.Num(fluid.airPocket).Num(fluid.hours);
		}
		const auto& tune = car.GetTune();
		out.Num(tune.ignitionTiming).Num(tune.idleSpeed).Num(tune.idleMixture).Num(tune.mainJet).Num(tune.intakeLash).Num(tune.exhaustLash)
			.Num(tune.frontCamber).Num(tune.caster).Num(tune.frontToe);
		for (double pressure : tune.tirePressure)
			out.Num(pressure);
		out.Num(tune.finalDrive);
		WriteRoadTest(out, build->lastRoadTest);
		out.Int(build->roadTestCurrent);
		out.Int(car.GetPlugs());
		for (int p = 0; p < CAR_PLUGS; p++)
		{
			const auto& clips = car.GetWireClips(static_cast<CarPlug>(p));
			out.Int(clips.size());
			for (const auto& clip : clips)
				out.Num(clip[0]).Num(clip[1]).Num(clip[2]);
		}
		for (int seat = 0; seat < 2; seat++)
		{
			const auto& setting = car.GetSeatSetting(seat);
			out.Num(setting.slide).Num(setting.recline).Int(setting.folded ? 1 : 0);
		}
		out.Num(car.GetGroundPoint()[0]).Num(car.GetGroundPoint()[1]).Num(car.GetGroundPoint()[2]);
		out.Num(car.GetGroundNormal()[0]).Num(car.GetGroundNormal()[1]).Num(car.GetGroundNormal()[2]);
		// The bleeders on its nipples (the tool on each, or -1).
		for (int p = 0; p < BLEED_POINTS; p++)
		{
			const auto tool = car.BleederOn(static_cast<BleedPoint>(p));
			out.Int(tool ? static_cast<long long>(*tool) : -1);
		}
		// The cap on each filler (or -1: off), and whether its caps were ever made.
		for (int c = 0; c < FLUID_CIRCUITS; c++)
		{
			const auto cap = car.CapOn(static_cast<FluidCircuit>(c));
			out.Int(cap ? static_cast<long long>(*cap) : -1);
		}
		out.Int(build->capsIssued ? 1 : 0);
	}
	return out.Text();
}

std::string Workshop::CarSharedText() const
{
	Writer out;
	out.Int(m_carCarried.size());
	for (const auto& [host, carried] : m_carCarried)
		out.Int(host).Int(carried);
	out.Int(m_containerLitres.size());
	for (const auto& [container, litres] : m_containerLitres)
		out.Int(container).Num(litres);
	out.Int(m_keyCuts.size());
	for (const auto& [key, ignition] : m_keyCuts)
		out.Int(key).Int(ignition);
	return out.Text();
}

bool Workshop::RestoreCarState(const std::string& text)
{
	Reader in(text);
	long long version = in.Int();
	if (version < 2 || version > CAR_TEXT_VERSION)
		return false;
	// Each car (before 15 there was one at most, nobody's).
	std::vector<std::unique_ptr<CarBuild>> builds;
	const size_t carCount = version >= 15 ? in.Count() : (in.Int() != 0 ? 1 : 0);
	for (size_t carNumber = 0; carNumber < carCount && !in.Failed(); carNumber++)
	{
		builds.push_back(std::make_unique<CarBuild>(CarSpec::LE86Coupe()));
		auto& build = builds.back();
		if (version >= 15)
			build->owner = static_cast<int>(in.Int());
		auto& car = build->assembly;
		size_t installed = in.Count();
		for (size_t i = 0; i < installed && !in.Failed(); i++)
		{
			long long slot = in.Int(), index = in.Int(), id = in.Int();
			if (version < 10 && slot == OLD_ENGINE_HARNESS_SLOT)
				continue;
			if (version < 10 && slot > OLD_ENGINE_HARNESS_SLOT)
				slot--;
			if (version < 12 && slot >= FIRST_SLOT_SHIFTED_IN_12)
				slot++;
			if (version < 14 && slot >= FIRST_SLOT_SHIFTED_IN_14)
				slot++;
			auto part = FindPart(static_cast<uint32_t>(id));
			if (!part || slot < 0 || slot >= static_cast<long long>(CarSlot::COUNT) || EngineOf(*part) >= 0)
				continue;
			// Restored as it was, so the fluids it held aren't spilled again: they're read in after.
			std::string error;
			car.Install(static_cast<CarSlot>(slot), static_cast<int>(index), part, error);
		}
		// A car from before the inner tie rods has them on, used, wherever a tie rod end is.
		if (version < 12)
			for (int side = 0; side < car.SlotCount(CarSlot::TIE_ROD_INNER); side++)
				if (car.Get(CarSlot::TIE_ROD_END, side) && !car.Get(CarSlot::TIE_ROD_INNER, side))
				{
					auto inner = PartFactory::Create(PartType::TIE_ROD_INNER, *m_spec, PartQuality::OEM, PartCondition::USED);
					inner->MarkUsed();
					m_inventory.push_back(inner);
					std::string error;
					car.Install(CarSlot::TIE_ROD_INNER, side, inner, error);
				}
		// A car from before the slave cylinder's bolts has them in, torqued, wherever its slave cylinder is.
		if (version < 13 && car.Get(CarSlot::CLUTCH_SLAVE_CYLINDER, 0))
			if (const auto* holes = CarAssembly::Holes(CarSlot::CLUTCH_SLAVE_BOLT))
				for (int hole = 0; hole < car.SlotCount(CarSlot::CLUTCH_SLAVE_BOLT); hole++)
				{
					if (car.IsFilled(CarSlot::CLUTCH_SLAVE_BOLT, hole))
						continue;
					auto bolt = PartFactory::Create(holes->size, *m_spec, PartQuality::OEM, PartCondition::USED);
					bolt->MarkUsed();
					m_inventory.push_back(bolt);
					std::string error;
					if (car.Install(CarSlot::CLUTCH_SLAVE_BOLT, hole, bolt, error))
					{
						bolt->SetTorque(holes->torqueNm);
						bolt->SetTightness(FastenerTightness::TORQUED);
					}
				}
		// A car from before the ignition switch has one in its dash (and the shop's first key is cut for it, below).
		if (version < 14 && car.IsFilled(CarSlot::DASHBOARD, 0) && !car.IsFilled(CarSlot::IGNITION_SWITCH, 0))
		{
			auto ignition = PartFactory::Create(PartType::IGNITION_SWITCH, *m_spec, PartQuality::OEM, PartCondition::USED);
			ignition->MarkUsed();
			m_inventory.push_back(ignition);
			std::string error;
			car.Install(CarSlot::IGNITION_SWITCH, 0, ignition, error);
		}
		int engine = static_cast<int>(in.Int());
		if (engine >= 0 && engine < GetEngineCount() && !GetAssembly(engine).GetAllParts().empty())
		{
			std::string error;
			if (!car.SetEngine(engine, error))
				Log(std::format("The engine came out of the car: {}", error));
		}
		std::array<FluidLevel, FLUID_CIRCUITS> fluids = {};
		for (auto& fluid : fluids)
		{
			size_t products = in.Count();
			for (size_t p = 0; p < products && !in.Failed(); p++)
			{
				auto product = in.Enumerated<FluidProduct>(FLUID_PRODUCTS);
				fluid.litres[product] = in.Num();
			}
			for (double& air : fluid.air)
				air = in.Num();
			fluid.airPocket = in.Num();
			fluid.hours = in.Num();
		}
		auto& tune = car.GetTune();
		tune.ignitionTiming = in.Num();
		tune.idleSpeed = in.Num();
		tune.idleMixture = in.Num();
		tune.mainJet = in.Num();
		tune.intakeLash = in.Num();
		tune.exhaustLash = in.Num();
		tune.frontCamber = in.Num();
		tune.caster = in.Num();
		tune.frontToe = in.Num();
		for (double& pressure : tune.tirePressure)
			pressure = in.Num();
		tune.finalDrive = in.Num();
		build->lastRoadTest = ReadRoadTest(in);
		build->roadTestCurrent = in.Int() != 0;
		car.RestorePlugs(version >= 3 ? static_cast<uint32_t>(in.Int()) : ALL_CAR_PLUGS);
		if (version < 11)
			car.RestorePlugs(car.GetPlugs() | (ALL_CAR_PLUGS & ~((1u << CAR_PLUGS_BEFORE_PIPES) - 1u)));
		// A car from before the ground strap has it on, so it still runs.
		if (version < 5)
			car.SetPlugged(CarPlug::GROUND, true);
		if (version >= 4)
		{
			// (A plug that's since gone has its clips read and dropped.)
			int written = version >= 11 ? CAR_PLUGS : version >= 9 ? CAR_PLUGS_BEFORE_PIPES : version >= 8 ? CAR_PLUGS_IN_VERSION_8
				: version >= 5 ? CAR_PLUGS_BEFORE_ENGINE_BATTERY : CAR_PLUGS_BEFORE_GROUND;
			for (int p = 0; p < written; p++)
			{
				size_t clips = in.Count();
				for (size_t c = 0; c < clips && !in.Failed(); c++)
				{
					CarWirePoint at = { static_cast<float>(in.Num()), static_cast<float>(in.Num()), static_cast<float>(in.Num()) };
					if (p < CAR_PLUGS)
						car.AddWireClip(static_cast<CarPlug>(p), at);
				}
			}
			for (int seat = 0; seat < 2; seat++)
			{
				CarSeatSetting setting;
				setting.slide = static_cast<float>(in.Num());
				setting.recline = static_cast<float>(in.Num());
				setting.folded = in.Int() != 0;
				car.SetSeatSetting(seat, setting);
			}
		}
		if (version >= 6)
		{
			CarWirePoint at = { static_cast<float>(in.Num()), static_cast<float>(in.Num()), static_cast<float>(in.Num()) };
			// (One from before the bolt stood out of its panel is put back where a car starts with it.)
			if (version >= 7)
				car.SetGroundPoint(at);
		}
		if (version >= 7)
		{
			CarWirePoint normal = { static_cast<float>(in.Num()), static_cast<float>(in.Num()), static_cast<float>(in.Num()) };
			car.SetGroundNormal(normal);
		}
		if (version >= 16)
			for (int p = 0; p < BLEED_POINTS && !in.Failed(); p++)
				if (const long long tool = in.Int(); tool >= 0)
					car.SetBleeder(static_cast<BleedPoint>(p), static_cast<size_t>(tool));
		if (version >= 17)
		{
			for (int c = 0; c < FLUID_CIRCUITS && !in.Failed(); c++)
				if (const long long cap = in.Int(); cap >= 0)
					car.SetCap(static_cast<FluidCircuit>(c), static_cast<size_t>(cap));
			build->capsIssued = in.Int() != 0;
		}
		// The fluids go back in last, as they were: installing the parts above opened every circuit.
		car.RestoreFluids(fluids);
	}
	std::map<uint32_t, uint32_t> carried;
	size_t carriedCount = in.Count();
	for (size_t i = 0; i < carriedCount && !in.Failed(); i++)
	{
		auto host = static_cast<uint32_t>(in.Int());
		auto rider = static_cast<uint32_t>(in.Int());
		if (FindPart(host) && FindPart(rider))
			carried[host] = rider;
	}
	// The fluid containers, what's left in each; from before them, the shop's stock by the litre (put in containers by
	// UpgradeCarKit).
	std::map<size_t, double> containers;
	std::array<double, FLUID_PRODUCTS> stock = {};
	if (version >= 17)
	{
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			const auto container = static_cast<size_t>(in.Int());
			containers[container] = in.Num();
		}
	}
	else
		for (double& litres : stock)
			litres = in.Num();
	// The keys, each with the ignition switch it's cut for (one that's gone is dropped). From before, the shop's first
	// key is the car's.
	std::map<size_t, uint32_t> keyCuts;
	if (version >= 14)
	{
		size_t keys = in.Count();
		for (size_t k = 0; k < keys && !in.Failed(); k++)
		{
			auto key = static_cast<size_t>(in.Int());
			auto ignition = static_cast<uint32_t>(in.Int());
			if (FindPart(ignition))
				keyCuts[key] = ignition;
		}
	}
	else if (const auto ignition = !builds.empty() ? builds.front()->assembly.Get(CarSlot::IGNITION_SWITCH, 0) : nullptr)
		keyCuts[CarKeyTool()] = ignition->GetId();
	if (in.Failed())
		return false;

	// In place where there's a car already, so anything holding on to it keeps working.
	for (size_t car = 0; car < builds.size(); car++)
		if (car < m_cars.size())
			*m_cars[car] = std::move(*builds[car]);
		else
			m_cars.push_back(std::move(builds[car]));
	m_cars.resize(builds.size());
	SetActiveCar(m_activeCar);
	m_carCarried = carried;
	m_containerLitres = containers;
	m_legacyFluidStock = stock;
	m_keyCuts = keyCuts;
	SettleCarPlugs();
	return true;
}

bool Workshop::CarPlugSocketFitted(CarPlug plug) const
{
	if (!m_car)
		return false;
	const auto& car = m_car->assembly;
	if (!car.IsFilled(CarAssembly::PlugHarness(plug), 0))
		return false;
	// The battery's ground strap goes to a stud on the body, which is always there.
	if (plug == CarPlug::GROUND)
		return true;
	if (auto target = CarAssembly::PlugTarget(plug))
		return car.IsFilled(target->first, target->second);
	// On the engine: it has to be in the bay, with what the plug goes onto on it (the senders are in the head's
	// water outlet and the block).
	int engine = car.GetEngine();
	if (engine < 0)
		return false;
	const auto& fitted = GetAssembly(engine);
	switch (plug)
	{
		// (The pump's hose comes on from the filter.)
		case CarPlug::FUEL_PUMP:		return car.IsFilled(CarSlot::FUEL_FILTER, 0) && fitted.Get(AssemblySlot::FUEL_PUMP, 0) != nullptr;
		case CarPlug::DISTRIBUTOR:		return fitted.Get(AssemblySlot::DISTRIBUTOR, 0) != nullptr;
		case CarPlug::ALTERNATOR:		return fitted.Get(AssemblySlot::ALTERNATOR, 0) != nullptr;
		case CarPlug::COOLANT_SENDER:	return fitted.Get(AssemblySlot::THERMOSTAT_HOUSING, 0) != nullptr;
		default:						return fitted.Get(AssemblySlot::BLOCK, 0) != nullptr;
	}
}

bool Workshop::ConnectCarPlug(CarPlug plug)
{
	if (!m_car || m_car->assembly.IsPlugged(plug))
		return false;
	if (!CarPlugSocketFitted(plug))
	{
		Log(std::format("There's nothing to put it on: the {} isn't there.", CarPlugName(plug)));
		return false;
	}
	m_car->assembly.SetPlugged(plug, true);
	CarChanged();
	return true;
}

bool Workshop::DisconnectCarPlug(CarPlug plug)
{
	if (!m_car || !m_car->assembly.IsPlugged(plug))
		return false;
	m_car->assembly.SetPlugged(plug, false);
	CarChanged();
	return true;
}

void Workshop::SettleCarPlugs()
{
	if (!m_car)
		return;
	for (int p = 0; p < CAR_PLUGS; p++)
		if (auto plug = static_cast<CarPlug>(p); m_car->assembly.IsPlugged(plug) && !CarPlugSocketFitted(plug))
		{
			m_car->assembly.SetPlugged(plug, false);
			CarChanged();
		}
}

bool Workshop::ClipCarWire(CarPlug plug, const CarWirePoint& at)
{
	return m_car && m_car->assembly.IsFilled(CarAssembly::PlugHarness(plug), 0) && m_car->assembly.AddWireClip(plug, at);
}

void Workshop::UnclipCarWire(CarPlug plug, int index)
{
	if (m_car)
		m_car->assembly.RemoveWireClip(plug, index);
}

void Workshop::UnclipLooseCarWire(CarPlug plug)
{
	if (m_car && !m_car->assembly.IsPlugged(plug))
		m_car->assembly.SetPlugged(plug, false);
}

int Workshop::DisconnectCarPlugs(CarSlot harness)
{
	int pulled = 0;
	for (int p = 0; p < CAR_PLUGS; p++)
		if (auto plug = static_cast<CarPlug>(p); CarAssembly::PlugHarness(plug) == harness && DisconnectCarPlug(plug))
			pulled++;
	return pulled;
}

void Workshop::SetCarSeat(int seat, const CarSeatSetting& setting)
{
	if (m_car)
		m_car->assembly.SetSeatSetting(seat, setting);
}

std::optional<std::string> Workshop::CarStarterDead() const
{
	if (!m_car)
		return "There's no car.";
	const auto& car = m_car->assembly;
	for (CarSlot slot : { CarSlot::BATTERY, CarSlot::MAIN_HARNESS, CarSlot::FUSE_BOX, CarSlot::IGNITION_SWITCH, CarSlot::STARTER })
		if (!car.IsFilled(slot, 0))
			return std::format("Nothing: the {} isn't on.", car.PositionName(slot, 0));
	if (!car.IsPlugged(CarPlug::BATTERY))
		return "Nothing: the main harness's lead isn't on the battery.";
	if (!car.IsPlugged(CarPlug::GROUND))
		return "Nothing: the battery's ground strap isn't on the body.";
	if (!car.IsPlugged(CarPlug::STARTER))
		return "Nothing: the harness's plug isn't on the starter.";
	return std::nullopt;
}

size_t Workshop::CarKeyTool()
{
	static const size_t index = [] {
		const auto& tools = ToolCatalog::All();
		for (size_t i = 0; i < tools.size(); i++)
			if (tools[i].handKind == HandToolKind::CAR_KEY)
				return i;
		return tools.size();
	}();
	return index;
}

std::optional<uint32_t> Workshop::KeyCutFor(size_t keyToolId) const
{
	auto it = m_keyCuts.find(keyToolId);
	return it == m_keyCuts.end() ? std::nullopt : std::optional(it->second);
}

bool Workshop::KeyFitsCar(size_t keyToolId) const
{
	const auto ignition = m_car ? m_car->assembly.Get(CarSlot::IGNITION_SWITCH, 0) : nullptr;
	auto cut = KeyCutFor(keyToolId);
	return ignition && cut && *cut == ignition->GetId();
}

std::optional<size_t> Workshop::CutKeyFor(const Part& ignitionSwitch)
{
	size_t key = CarKeyTool();
	if (key >= m_toolCounts.size() || m_toolCounts[key] >= static_cast<int>(MAX_TOOL_COPIES))
		return std::nullopt;
	size_t id = ToolCatalog::IdOf(key, static_cast<size_t>(m_toolCounts[key]));
	m_toolCounts[key]++;
	m_keyCuts[id] = ignitionSwitch.GetId();
	return id;
}

void Workshop::DevCutKeyForCar(size_t keyToolId)
{
	if (const auto ignition = m_car ? m_car->assembly.Get(CarSlot::IGNITION_SWITCH, 0) : nullptr)
		m_keyCuts[keyToolId] = ignition->GetId();
}

bool Workshop::GroundCarBattery(const CarWirePoint& at, const CarWirePoint& normal)
{
	if (!m_car || m_car->assembly.IsPlugged(CarPlug::GROUND) || !CarPlugSocketFitted(CarPlug::GROUND))
		return false;
	m_car->assembly.SetGroundPoint(at);
	m_car->assembly.SetGroundNormal(normal);
	return ConnectCarPlug(CarPlug::GROUND);
}

void Workshop::DevPlugCar(bool plugged)
{
	if (!m_car)
		return;
	for (int p = 0; p < CAR_PLUGS; p++)
		m_car->assembly.SetPlugged(static_cast<CarPlug>(p), plugged && CarPlugSocketFitted(static_cast<CarPlug>(p)));
	// Pulled, they're as they came: no clips on their wires, the ground strap's bolt back where it started.
	if (!plugged)
	{
		for (int p = 0; p < CAR_PLUGS; p++)
			while (!m_car->assembly.GetWireClips(static_cast<CarPlug>(p)).empty())
				m_car->assembly.RemoveWireClip(static_cast<CarPlug>(p), 0);
		m_car->assembly.SetGroundPoint(CarAssembly::DEFAULT_GROUND_POINT);
		m_car->assembly.SetGroundNormal(CarAssembly::DEFAULT_GROUND_NORMAL);
	}
	CarChanged();
}


Workshop::CarDrive Workshop::StartCar()
{
	CarDrive drive;
	if (!m_car)
	{
		drive.problem = "There's no car.";
		return drive;
	}
	SettleCarPlugs();
	auto& build = *m_car;
	int engine = GetEngineInCar();
	DynoResult pull;
	bool complete = false;
	if (engine >= 0)
	{
		complete = GetAssembly(engine).IsComplete();
		if (complete)
			pull = Dyno::Run(m_engines[engine]->assembly);
	}
	RoadTestResult result = RoadTest::Run(build.assembly, pull, complete, true);
	// (It starts as warm as it was left: stone cold if it's sat, and warms up from there as it runs.)
	drive.goes = result.starts && result.drives && result.steers && result.stops && result.topSpeed > 1.0;
	if (!drive.goes)
	{
		drive.problem = engine < 0 ? "There's no engine in it."
			: !result.findings.empty() ? result.findings.back().message
			: !result.starts ? "It won't start."
			: !result.drives ? "It runs, but it won't move."
			: !result.steers ? "It can't be steered." : "It has no brakes.";
		return drive;
	}
	drive.topSpeed = result.topSpeed;
	drive.zeroToHundred = result.zeroToHundred;
	drive.brakingDistance = result.brakingDistance;
	drive.lateralGrip = result.lateralGrip;
	drive.idleRpm = result.idleRpm;
	drive.clutchWorks = result.clutchWorks;
	drive.brakesWork = result.brakesWork;
	return drive;
}

bool Workshop::DriveCar(double seconds, double metres, double throttle)
{
	if (!m_car)
		return false;
	auto& build = *m_car;
	// About a litre an hour ticking over, and nine litres a hundred kilometres on the move (more with the foot down).
	double litres = seconds / 3600.0 * 1.0 + metres / 100000.0 * 9.0 * (0.7 + 0.9 * std::clamp(throttle, 0.0, 1.0));
	build.assembly.Run(seconds / 3600.0, litres);
	// It warms up as a car does: from cold, some five minutes ticking over, half that driven, quicker still worked
	// hard; then the thermostat opens and holds it there.
	double pace = seconds > 0.0 ? metres / seconds : 0.0;
	double warming = 1.0 / CAR_ENGINE_WARM_SECONDS * (1.0 + 1.2 * std::clamp(pace / 25.0, 0.0, 1.0) + 0.8 * std::clamp(throttle, 0.0, 1.0));
	build.engineHeat = (std::min)(1.0, build.engineHeat + seconds * warming);
	return build.assembly.GetFluid(FluidCircuit::FUEL).Total() > 0.01;
}

int Workshop::DiscardLooseParts(uint32_t from, uint32_t to)
{
	const size_t before = m_inventory.size();
	std::erase_if(m_inventory, [&](const pPart& part) {
		return part->GetId() >= from && part->GetId() < to && EngineOf(*part) < 0 && CarOf(*part) < 0;
	});
	return static_cast<int>(before - m_inventory.size());
}

uint64_t Workshop::PlacementSignature() const
{
	uint64_t hash = 1469598103934665603ull;
	auto mix = [&](uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
	for (const auto& engine : m_engines)
		mix(engine->assembly.Revision());
	mix(m_cars.size());
	for (const auto& car : m_cars)
		mix(car->assembly.Revision());
	for (const auto* fittings : { &m_shells, &m_pistons, &m_pins, &m_rings, &m_carCarried })
	{
		mix(fittings->size());
		for (const auto& [host, fitted] : *fittings)
			mix((static_cast<uint64_t>(host) << 32) | fitted);
	}
	mix(m_boreRings.size());
	for (const auto& [ring, at] : m_boreRings)
		mix((static_cast<uint64_t>(ring) << 32) | at.block);
	mix(m_headFittings.size());
	for (const auto& [part, at] : m_headFittings)
		mix((static_cast<uint64_t>(part) << 32) | at.head);
	return hash;
}

void Workshop::DevAirInCarLines()
{
	if (!m_car)
		return;
	std::array<FluidLevel, FLUID_CIRCUITS> fluids;
	for (int circuit = 0; circuit < FLUID_CIRCUITS; circuit++)
		fluids[circuit] = m_car->assembly.GetFluid(static_cast<FluidCircuit>(circuit));
	fluids[static_cast<size_t>(FluidCircuit::BRAKES)].air.fill(1.0);
	fluids[static_cast<size_t>(FluidCircuit::CLUTCH)].air.fill(1.0);
	m_car->assembly.RestoreFluids(fluids);
	CarChanged();
}

int Workshop::DevAddFluid(FluidProduct product, int count)
{
	int added = 0;
	while (added < count && AddFluidContainer(product, FluidUnitLitres(product)))
		added++;
	return added;
}

std::optional<std::string> Workshop::CarPowerProblem() const
{
	if (!m_car)
		return "There's no car.";
	const auto& car = m_car->assembly;
	for (CarSlot slot : { CarSlot::BATTERY, CarSlot::MAIN_HARNESS, CarSlot::FUSE_BOX })
		if (!car.IsFilled(slot, 0))
			return std::format("Nothing: the {} isn't on.", car.PositionName(slot, 0));
	if (!car.IsPlugged(CarPlug::BATTERY))
		return "Nothing: the main harness's lead isn't on the battery.";
	if (!car.IsPlugged(CarPlug::GROUND))
		return "Nothing: the battery's ground strap isn't on the body.";
	return std::nullopt;
}
