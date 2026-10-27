#include "Workshop.h"

#include <algorithm>
#include <format>

namespace
{
	// What a core lot holds, besides the per-cylinder parts.
	const PartType CORE_SINGLES[] = { PartType::BLOCK, PartType::CRANKSHAFT, PartType::CYLINDER_HEAD, PartType::CAMSHAFT };
	constexpr double CORE_DISCOUNT = 0.8;	// a whole core costs less than its parts bought one by one
}

Workshop::Workshop(const EngineSpec& spec, double startingMoney) : m_spec(&spec), m_money(startingMoney)
{
	// Every tool and machine has to be bought: the shop starts with bare walls.
	const auto& tools = ToolCatalog::All();
	for (const auto& tool : tools)
		m_ownedTools.push_back(tool.ownedAtStart);

	const auto& machines = MachineCatalog::All();
	for (const auto& machine : machines)
		m_ownedMachines.push_back(machine.ownedAtStart);

	// One engine stand, and a tired core engine dragged in off a trailer to put on it.
	m_engines.push_back(std::make_unique<EngineBuild>(spec));
	AddCore();

	Log(std::format("Opened the shop with ${:.0f} and a worn-out {} core.", startingMoney, spec.name));
}

Workshop::~Workshop()
{
}

void Workshop::AddCore()
{
	const auto& spec = *m_spec;
	for (auto type : CORE_SINGLES)
		m_inventory.push_back(PartFactory::Create(type, spec, PartQuality::OEM, PartCondition::USED));
	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
		m_inventory.push_back(PartFactory::Create(PartType::CONNECTING_ROD, spec, PartQuality::OEM, PartCondition::USED));
	for (auto type : { PartType::INTAKE_VALVE, PartType::EXHAUST_VALVE })
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			m_inventory.push_back(PartFactory::Create(type, spec, PartQuality::OEM, PartCondition::USED));
	for (int spring = 0; spring < spec.cylinders * 2; spring++)
		m_inventory.push_back(PartFactory::Create(PartType::VALVE_SPRING, spec, PartQuality::OEM, PartCondition::USED));
}

const EngineSpec& Workshop::GetSpec() const
{
	return *m_spec;
}

double Workshop::GetMoney() const
{
	return m_money;
}

double Workshop::GetHours() const
{
	return m_hours;
}

const std::vector<std::string>& Workshop::GetLog() const
{
	return m_log;
}

void Workshop::Log(const std::string& message)
{
	m_log.push_back(std::format("[{:6.1f}h] {}", m_hours, message));
}

bool Workshop::Spend(double amount, const std::string& what)
{
	if (amount > m_money)
	{
		Log(std::format("Can't afford {} (${:.0f}).", what, amount));
		return false;
	}
	m_money -= amount;
	return true;
}

std::vector<double> Workshop::SizeOptions(PartType type)
{
	switch (type)
	{
		case PartType::PISTON:
		case PartType::PISTON_RING:		return { 0.0, 0.25, 0.50, 0.75, 1.00 };
		case PartType::MAIN_BEARING:
		case PartType::ROD_BEARING:		return { 0.0, -0.25, -0.50, -0.75 };
		case PartType::INTAKE_VALVE:
		case PartType::EXHAUST_VALVE:	return { 0.0, 0.075, 0.15, 0.38 };
		default:						return { 0.0 };
	}
}

bool Workshop::SoldUsed(PartType type)
{
	return type == PartType::BLOCK || type == PartType::CRANKSHAFT || type == PartType::CYLINDER_HEAD || type == PartType::CONNECTING_ROD
		|| type == PartType::CAMSHAFT;
}

double Workshop::Quote(const PartOrder& order) const
{
	return PartFactory::Price(order.type, order.quality, order.condition) * std::max(order.quantity, 0);
}

bool Workshop::Buy(const PartOrder& order)
{
	if (order.quantity <= 0)
		return false;
	if (order.condition == PartCondition::USED && !SoldUsed(order.type))
		return false;

	if (!Spend(Quote(order), PartTypeName(order.type)))
		return false;

	for (int i = 0; i < order.quantity; i++)
		m_inventory.push_back(PartFactory::Create(order.type, *m_spec, order.quality, order.condition, order.sizeOffset));

	Log(std::format("Bought {}x {}.", order.quantity, m_inventory.back()->GetName()));
	return true;
}

double Workshop::CorePrice() const
{
	const auto& spec = *m_spec;
	auto used = [](PartType type) { return PartFactory::Price(type, PartQuality::OEM, PartCondition::USED); };
	double price = 0.0;
	for (auto type : CORE_SINGLES)
		price += used(type);
	price += used(PartType::CONNECTING_ROD) * spec.cylinders;
	price += (used(PartType::INTAKE_VALVE) + used(PartType::EXHAUST_VALVE)) * spec.cylinders;
	price += used(PartType::VALVE_SPRING) * spec.cylinders * 2;
	return std::round(price * CORE_DISCOUNT);
}

bool Workshop::BuyCore()
{
	if (!Spend(CorePrice(), std::format("a {} core", m_spec->name)))
		return false;
	AddCore();
	Log(std::format("Bought a used {} core.", m_spec->name));
	return true;
}

const std::vector<pPart>& Workshop::GetInventory() const
{
	return m_inventory;
}

std::vector<pPart> Workshop::GetInventory(PartType type) const
{
	std::vector<pPart> parts;
	for (const auto& part : m_inventory)
		if (part->GetType() == type)
			parts.push_back(part);
	return parts;
}

bool Workshop::SellPart(const pPart& part)
{
	if (EngineOf(*part) >= 0)
	{
		Log("Take it off the engine stand before selling it.");
		return false;
	}

	auto it = std::find(m_inventory.begin(), m_inventory.end(), part);
	if (it == m_inventory.end())
		return false;

	double price = std::round(part->GetValue() * 0.5);
	m_money += price;
	Log(std::format("Sold {} for ${:.0f}.", part->GetName(), price));
	m_inventory.erase(it);
	return true;
}

bool Workshop::OwnsTool(size_t toolIndex) const
{
	return toolIndex < m_ownedTools.size() && m_ownedTools[toolIndex];
}

bool Workshop::OwnsMachine(size_t machineIndex) const
{
	return machineIndex < m_ownedMachines.size() && m_ownedMachines[machineIndex];
}

bool Workshop::BuyTool(size_t toolIndex)
{
	const auto& tools = ToolCatalog::All();
	if (toolIndex >= tools.size() || OwnsTool(toolIndex))
		return false;
	if (!Spend(tools[toolIndex].price, tools[toolIndex].name))
		return false;

	m_ownedTools[toolIndex] = true;
	Log(std::format("Bought a {}.", tools[toolIndex].name));
	return true;
}

bool Workshop::BuyMachine(size_t machineIndex)
{
	const auto& machines = MachineCatalog::All();
	if (machineIndex >= machines.size() || OwnsMachine(machineIndex))
		return false;
	if (!Spend(machines[machineIndex].price, machines[machineIndex].name))
		return false;

	m_ownedMachines[machineIndex] = true;
	Log(std::format("Bought a {}.", machines[machineIndex].name));
	return true;
}

int Workshop::BestToolFor(const Feature& feature) const
{
	const auto& tools = ToolCatalog::All();
	int best = -1;
	for (size_t i = 0; i < tools.size(); i++)
		if (m_ownedTools[i] && tools[i].CanMeasure(feature))
			if (best < 0 || tools[i].sigma < tools[best].sigma)
				best = static_cast<int>(i);
	return best;
}

int Workshop::BestToolFor(AssemblyCheck check) const
{
	const auto& tools = ToolCatalog::All();
	int best = -1;
	for (size_t i = 0; i < tools.size(); i++)
		if (m_ownedTools[i] && tools[i].CanCheck(check))
			if (best < 0 || tools[i].sigma < tools[best].sigma)
				best = static_cast<int>(i);
	return best;
}

int Workshop::FinishTool() const
{
	const auto& tools = ToolCatalog::All();
	for (size_t i = 0; i < tools.size(); i++)
		if (m_ownedTools[i] && tools[i].measuresFinish)
			return static_cast<int>(i);
	return -1;
}

bool Workshop::MeasureSize(Part& part, const std::string& featureName, int toolIndex)
{
	auto feature = part.FindFeature(featureName);
	if (!feature)
		return false;
	if (!feature->directlyMeasurable)
	{
		Log(std::format("{} can't be measured directly.", featureName));
		return false;
	}

	if (toolIndex < 0)
		toolIndex = BestToolFor(*feature);
	if (toolIndex < 0 || !OwnsTool(toolIndex) || !ToolCatalog::All()[toolIndex].CanMeasure(*feature))
	{
		Log(std::format("You don't have a tool that can measure {}.", featureName));
		return false;
	}
	const auto& tool = ToolCatalog::All()[toolIndex];

	FeatureReading reading;
	if (auto previous = part.GetReading(featureName); previous && !previous->stale)
		reading = *previous;

	reading.toolName = tool.name;
	reading.hasSize = true;
	reading.stale = false;
	for (int station = 0; station < Feature::STATIONS; station++)
		for (int axis = 0; axis < Feature::AXES; axis++)
			reading.samples[station][axis] = tool.Read(feature->samples[station][axis]);
	reading.value = tool.Read(feature->value);
	if (feature->kind == FeatureKind::FLATNESS)
		reading.value = std::max(0.0, reading.value);

	part.RecordReading(featureName, reading);
	m_hours += tool.hoursPerUse;
	Log(std::format("Measured {} on the {} with the {}.", featureName, part.GetName(), tool.name));
	return true;
}

bool Workshop::MeasureFinish(Part& part, const std::string& featureName, int toolIndex)
{
	auto feature = part.FindFeature(featureName);
	if (!feature || !feature->HasFinishSpec())
		return false;

	if (toolIndex < 0)
		toolIndex = FinishTool();
	if (toolIndex < 0 || !OwnsTool(toolIndex) || !ToolCatalog::All()[toolIndex].measuresFinish)
	{
		Log("You need a profilometer to measure surface finish.");
		return false;
	}
	const auto& tool = ToolCatalog::All()[toolIndex];

	FeatureReading reading;
	if (auto previous = part.GetReading(featureName); previous && !previous->stale)
		reading = *previous;

	reading.stale = false;
	reading.finishRa = std::max(0.01, tool.Read(feature->surfaceRa));
	part.RecordReading(featureName, reading);
	m_hours += tool.hoursPerUse;
	Log(std::format("Checked the finish of {} on the {}.", featureName, part.GetName()));
	return true;
}

MachiningResult Workshop::MachineFeature(size_t machineIndex, Part& part, const std::string& featureName, double parameter)
{
	const auto& machines = MachineCatalog::All();
	if (machineIndex >= machines.size() || !OwnsMachine(machineIndex))
		return { false, "You don't own that machine." };

	const auto& machine = machines[machineIndex];

	if (EngineOf(part) >= 0)
		return { false, "Pull it off the engine stand before machining it." };

	auto feature = part.FindFeature(featureName);
	if (!feature)
		return { false, "No such feature." };

	auto result = Machining::Apply(machine, part, *feature, parameter, *m_spec);
	if (result.success)
	{
		// Setup, plus time for however much stock came off.
		double hours = machine.hoursPerUse + result.removed * machine.hoursPerMm;
		m_hours += hours;
		if (machine.hoursPerMm > 0.0)
			result.message += std::format(" Took {:.1f} h to remove {:.3f} mm.", hours, result.removed);
	}
	Log(result.message);
	return result;
}

int Workshop::GetEngineCount() const
{
	return static_cast<int>(m_engines.size());
}

int Workshop::GetActiveEngine() const
{
	return m_activeEngine;
}

void Workshop::SetActiveEngine(int engine)
{
	if (engine >= 0 && engine < GetEngineCount())
		m_activeEngine = engine;
}

const EngineBuild& Workshop::GetEngine(int engine) const
{
	return *m_engines[std::clamp(engine, 0, GetEngineCount() - 1)];
}

const EngineAssembly& Workshop::GetAssembly(int engine) const
{
	return GetEngine(engine).assembly;
}

int Workshop::EngineOf(const Part& part) const
{
	for (int engine = 0; engine < GetEngineCount(); engine++)
		for (const auto& installed : m_engines[engine]->assembly.GetAllParts())
			if (installed.get() == &part)
				return engine;
	return -1;
}

bool Workshop::BuyEngineStand()
{
	if (GetEngineCount() >= MAX_ENGINES)
	{
		Log("There's no room for another engine stand.");
		return false;
	}
	if (!Spend(ENGINE_STAND_PRICE, "an engine stand"))
		return false;

	m_engines.push_back(std::make_unique<EngineBuild>(*m_spec));
	Log(std::format("Bought engine stand {}.", GetEngineCount()));
	return true;
}

EngineBuild& Workshop::Active()
{
	return *m_engines[m_activeEngine];
}

const EngineBuild& Workshop::Active() const
{
	return *m_engines[m_activeEngine];
}

EngineAssembly& Workshop::GetAssembly()
{
	return Active().assembly;
}

const EngineAssembly& Workshop::GetAssembly() const
{
	return Active().assembly;
}

void Workshop::AssemblyChanged()
{
	Active().dynoCurrent = false;
	for (auto& [label, reading] : Active().readings)
		reading.stale = true;
}

bool Workshop::InstallPart(AssemblySlot slot, int index, const pPart& part)
{
	if (std::find(m_inventory.begin(), m_inventory.end(), part) == m_inventory.end())
		return false;
	// A part can only be in one engine at a time.
	if (int engine = EngineOf(*part); engine >= 0 && engine != m_activeEngine)
	{
		Log(std::format("That {} is on engine {}.", part->GetName(), engine + 1));
		return false;
	}

	auto& assembly = GetAssembly();
	if (auto missing = assembly.MissingPrerequisite(slot, index))
	{
		Log(*missing);
		return false;
	}
	if (assembly.Get(slot, index))
		assembly.Remove(slot, index);

	std::string error;
	if (!assembly.Install(slot, index, part, error))
	{
		Log(error);
		return false;
	}

	AssemblyChanged();
	m_hours += 0.1;
	return true;
}

bool Workshop::RemovePart(AssemblySlot slot, int index)
{
	// In reverse build order: nothing comes off while something is fitted on top of it.
	if (auto blocked = GetAssembly().RemovalBlockedBy(slot, index))
	{
		Log(*blocked);
		return false;
	}
	if (GetAssembly().Remove(slot, index))
		AssemblyChanged();
	return true;
}

std::string Workshop::AssemblyReadingLabel(AssemblyCheck check, int index, bool rod)
{
	switch (check)
	{
		case AssemblyCheck::OIL_CLEARANCE:		return std::format("{} {} oil clearance", rod ? "Rod" : "Main", index + 1);
		case AssemblyCheck::CAM_CLEARANCE:		return std::format("Cam journal {} oil clearance", index + 1);
		case AssemblyCheck::RING_GAP:			return std::format("Cylinder {} ring gap", index + 1);
		case AssemblyCheck::DECK_CLEARANCE:		return std::format("Cylinder {} deck clearance", index + 1);
		case AssemblyCheck::INSTALLED_HEIGHT:	return std::format("{} {} installed height", FeatureNames::ValveSide(index % 2 == 1), index / 2 + 1);
		default:								return "";
	}
}

bool Workshop::MeasureAssembly(AssemblyCheck check, int index, bool rod, int toolIndex)
{
	const auto& assembly = GetAssembly();
	std::optional<double> trueValue;
	switch (check)
	{
		case AssemblyCheck::OIL_CLEARANCE:		trueValue = rod ? assembly.RodClearance(index) : assembly.MainClearance(index); break;
		case AssemblyCheck::CAM_CLEARANCE:		trueValue = assembly.CamClearance(index); break;
		case AssemblyCheck::RING_GAP:			trueValue = assembly.RingGap(index); break;
		case AssemblyCheck::DECK_CLEARANCE:		trueValue = assembly.DeckClearance(index); break;
		case AssemblyCheck::INSTALLED_HEIGHT:	trueValue = assembly.InstalledHeight(index / 2, index % 2 == 1); break;
		default:								return false;
	}

	auto label = AssemblyReadingLabel(check, index, rod);
	if (!trueValue)
	{
		Log(std::format("Can't check {} until the parts involved are on the stand.", label));
		return false;
	}

	if (toolIndex < 0)
		toolIndex = BestToolFor(check);
	if (toolIndex < 0 || !OwnsTool(toolIndex) || !ToolCatalog::All()[toolIndex].CanCheck(check))
	{
		Log(std::format("You don't have a tool that can check {}.", label));
		return false;
	}
	const auto& tool = ToolCatalog::All()[toolIndex];

	double reading = tool.Read(*trueValue);
	// Plastigauge and feelers can't read a negative gap; the parts simply bind.
	if (check != AssemblyCheck::DECK_CLEARANCE)
		reading = std::max(0.0, reading);

	Active().readings[label] = { reading, false };
	m_hours += tool.hoursPerUse;
	Log(std::format("Checked {} on engine {} with the {}.", label, m_activeEngine + 1, tool.name));
	return true;
}

const AssemblyReading* Workshop::GetAssemblyReading(AssemblyCheck check, int index, bool rod) const
{
	const auto& readings = Active().readings;
	auto it = readings.find(AssemblyReadingLabel(check, index, rod));
	return it == readings.end() ? nullptr : &it->second;
}

const DynoResult& Workshop::RunDyno()
{
	constexpr double DYNO_COST = 50.0;

	auto& engine = Active();
	if (!engine.assembly.IsComplete())
	{
		Log("The engine isn't fully assembled.");
		return engine.lastDyno;
	}
	if (!Spend(DYNO_COST, "dyno fuel and time"))
		return engine.lastDyno;

	engine.lastDyno = Dyno::Run(engine.assembly);
	engine.dynoCurrent = true;
	m_hours += 1.5;

	const auto& pull = engine.lastDyno;
	if (pull.failed)
		Log(std::format("Engine {}: dyno pull ended in failure at {:.0f} rpm.", m_activeEngine + 1, pull.failureRpm));
	else
		Log(std::format("Engine {}: dyno pull, {:.0f} hp at {:.0f} rpm, {:.0f} Nm at {:.0f} rpm.", m_activeEngine + 1,
			pull.peakPowerKw * 1.341, pull.peakPowerRpm, pull.peakTorqueNm, pull.peakTorqueRpm));

	return engine.lastDyno;
}

const DynoResult& Workshop::GetLastDynoResult() const
{
	return Active().lastDyno;
}

bool Workshop::IsDynoResultCurrent() const
{
	return Active().dynoCurrent;
}

double Workshop::EstimateEngineValue() const
{
	const auto& engine = Active();
	if (!engine.dynoCurrent || !engine.lastDyno.ran || engine.lastDyno.failed)
		return 0.0;

	double partsValue = 0.0;
	for (const auto& part : engine.assembly.GetAllParts())
		partsValue += part->GetValue();

	// Buyers pay for horsepower, but only from an engine they trust.
	double horsepower = engine.lastDyno.peakPowerKw * 1.341;
	return std::round(partsValue * 0.8 + horsepower * 8.0 * engine.lastDyno.healthScore / 100.0);
}

bool Workshop::SellEngine()
{
	double value = EstimateEngineValue();
	if (value <= 0.0)
	{
		Log("Nobody will buy an engine without a clean dyno sheet for it as it sits.");
		return false;
	}

	auto& engine = Active();
	for (const auto& part : engine.assembly.GetAllParts())
		m_inventory.erase(std::remove(m_inventory.begin(), m_inventory.end(), part), m_inventory.end());

	engine.assembly.Clear();
	engine.readings.clear();
	engine.dynoCurrent = false;
	m_money += value;
	Log(std::format("Sold engine {} for ${:.0f}. Its stand is free for the next one.", m_activeEngine + 1, value));
	return true;
}
