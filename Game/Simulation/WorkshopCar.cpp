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
	return m_car.get();
}

void Workshop::CarChanged()
{
	if (m_car)
		m_car->roadTestCurrent = false;
}

void Workshop::SetUpStartingCar()
{
	// The LE86, complete and running, its LV-16 in it under the first stand's number. Everything on both was fitted new
	// once and has done its miles since: used, and here and there worn past its limit.
	m_car = std::make_unique<CarBuild>(CarSpec::LE86Coupe());
	m_activeEngine = 0;
	FitNewEngineParts();
	WearStartingEngine(0, 75.0);
	FitNewCarParts();
	FillAndBleedCar();
	WearStartingCar();
	for (const auto& part : m_inventory)
		part->MarkUsed();
	CarChanged();
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
	return m_car && m_car->assembly.Find(&part).has_value();
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
	if (engine >= 0 && engine == GetEngineInCar())
	{
		if (flywheel)
		{
			const auto& car = m_car->assembly;
			if (car.IsFilled(CarSlot::CLUTCH_DISC, 0) || car.IsFilled(CarSlot::TRANSMISSION, 0))
				return "The clutch and gearbox are in the way: take them off the car to get at the flywheel.";
			return std::nullopt;
		}
		if (!ServiceableInCar(slot))
			return "That engine is in the car: only the top-end service parts (valve cover, plugs and leads, carburettor, oil filter) can be got at, "
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

double Workshop::FluidStock(FluidProduct product) const
{
	return product < FluidProduct::COUNT ? m_fluidStock[static_cast<size_t>(product)] : 0.0;
}

double Workshop::PourIntoCar(FluidCircuit circuit, FluidProduct product, double litres, bool quiet)
{
	if (!m_car || product >= FluidProduct::COUNT)
		return 0.0;
	auto& stock = m_fluidStock[static_cast<size_t>(product)];
	litres = (std::min)(litres, stock);
	if (litres <= 0.0)
	{
		if (!quiet)
			Log(std::format("There's no {} in stock: buy some at the parts computer.", FluidProductName(product)));
		return 0.0;
	}
	auto& car = m_car->assembly;
	if (auto open = car.CircuitOpen(circuit))
	{
		stock -= litres;
		if (!quiet)
			Log(std::format("The {} ran straight out onto the floor: the {} isn't on.", FluidProductName(product), *open));
		return 0.0;
	}
	double poured = car.Fill(circuit, product, litres);
	stock -= poured;
	if (!quiet && poured < litres)
		Log(std::format("Poured {:.2f} l of {} into the {}: it's full.", poured, FluidProductName(product), FluidCircuitName(circuit)));
	else if (!quiet)
		Log(std::format("Poured {:.2f} l of {} into the {}.", poured, FluidProductName(product), FluidCircuitName(circuit)));
	CarChanged();
	return poured;
}

bool Workshop::OwnsToolOfKind(HandToolKind kind) const
{
	const auto& tools = ToolCatalog::All();
	for (size_t i = 0; i < tools.size(); i++)
		if (tools[i].handKind == kind && OwnsTool(i))
			return true;
	return false;
}

double Workshop::DrainCar(FluidCircuit circuit)
{
	if (!m_car)
		return 0.0;
	double drained = m_car->assembly.Drain(circuit);
	if (drained > 0.0)
		Log(std::format("Drained {:.2f} l of {} into the drain pan.", drained, FluidCircuitName(circuit)));
	CarChanged();
	return drained;
}

double Workshop::BleedCar(BleedPoint point)
{
	if (!m_car)
		return 1.0;
	auto& car = m_car->assembly;
	auto circuit = point == BleedPoint::CLUTCH ? FluidCircuit::CLUTCH : FluidCircuit::BRAKES;
	if (auto open = car.CircuitOpen(circuit))
	{
		Log(std::format("Nothing to bleed: the {} isn't on.", *open));
		return 1.0;
	}
	if (car.GetFluid(circuit).Total() <= 0.0)
	{
		Log(std::format("Fill the {} reservoir first.", FluidCircuitName(circuit)));
		return 1.0;
	}
	double before = car.GetFluid(circuit).air[static_cast<size_t>(point)];
	double air = car.Bleed(point);
	// What's seen in the clear tube on the nipple.
	const char* seen = before > 0.6 ? "mostly air, spitting" : before > 0.2 ? "fluid with a stream of bubbles" : before > 0.03 ? "fluid with the odd bubble"
		: "clear fluid, no bubbles";
	Log(std::format("Bled the {}: {}.", BleedPointName(point), seen));
	const auto& spec = car.GetSpec().Fluid(circuit);
	if (car.GetFluid(circuit).Total() < spec.level.min * 0.6)
		Log("The reservoir ran dry and the master cylinder drew air: top it up and start again from the farthest wheel.");
	else if (car.GetFluid(circuit).Total() < spec.level.min)
		Log("The reservoir is below MIN: top it up before it runs dry.");
	CarChanged();
	return air;
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

void Workshop::CoolCarEngine(double seconds)
{
	if (m_car && m_car->engineHeat > 0.0)
		m_car->engineHeat = (std::max)(0.0, m_car->engineHeat - seconds / CAR_ENGINE_COOL_SECONDS);
}

const RoadTestResult& Workshop::RoadTestCar()
{
	static const RoadTestResult NONE = {};
	if (!m_car)
		return NONE;
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
		m_car = std::make_unique<CarBuild>(CarSpec::LE86Coupe());
	int fitted = FitNewCarParts();
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
		if (GetAssembly(engine).IsComplete() && engine != m_engineOnDyno && engine != m_engineOnHoist)
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
	for (int p = 0; p < BLEED_POINTS; p++)
	{
		for (int go = 0; go < 4; go++)
			car.Bleed(static_cast<BleedPoint>(p));
		auto circuit = p == static_cast<int>(BleedPoint::CLUTCH) ? FluidCircuit::CLUTCH : FluidCircuit::BRAKES;
		car.Fill(circuit, spec.Fluid(circuit).product, 1.0);
	}
	car.BurpCoolant(BURP_MINUTES);
	car.Fill(FluidCircuit::COOLANT, FluidProduct::COOLANT_50_50, 1.0);
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

	constexpr long long CAR_TEXT_VERSION = 2;		// 2: no car sale (cars aren't sold)
}

std::string Workshop::CarState() const
{
	Writer out;
	out.Int(CAR_TEXT_VERSION).Int(m_car ? 1 : 0);
	if (m_car)
	{
		const auto& car = m_car->assembly;
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
		WriteRoadTest(out, m_car->lastRoadTest);
		out.Int(m_car->roadTestCurrent);
	}
	out.Int(m_carCarried.size());
	for (const auto& [host, carried] : m_carCarried)
		out.Int(host).Int(carried);
	for (double litres : m_fluidStock)
		out.Num(litres);
	return out.Text();
}

bool Workshop::RestoreCarState(const std::string& text)
{
	Reader in(text);
	long long version = in.Int();
	if (version != CAR_TEXT_VERSION)
		return false;
	std::unique_ptr<CarBuild> build;
	if (in.Int() != 0)
	{
		build = std::make_unique<CarBuild>(CarSpec::LE86Coupe());
		auto& car = build->assembly;
		size_t installed = in.Count();
		for (size_t i = 0; i < installed && !in.Failed(); i++)
		{
			long long slot = in.Int(), index = in.Int(), id = in.Int();
			auto part = FindPart(static_cast<uint32_t>(id));
			if (!part || slot < 0 || slot >= static_cast<long long>(CarSlot::COUNT) || EngineOf(*part) >= 0)
				continue;
			// Restored as it was, so the fluids it held aren't spilled again: they're read in after.
			std::string error;
			car.Install(static_cast<CarSlot>(slot), static_cast<int>(index), part, error);
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
	std::array<double, FLUID_PRODUCTS> stock = {};
	for (double& litres : stock)
		litres = in.Num();
	if (in.Failed())
		return false;

	// In place where there's a car already, so anything holding on to it keeps working.
	if (m_car && build)
		*m_car = std::move(*build);
	else
		m_car = std::move(build);
	m_carCarried = carried;
	m_fluidStock = stock;
	return true;
}

