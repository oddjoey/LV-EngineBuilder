#include "Workshop.h"

#include <algorithm>
#include <format>

#include "Furniture.h"
#include "PartCatalog.h"
#include "Random.h"

namespace
{
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
	m_upgrades.assign(UpgradeCatalog::All().size(), false);

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
	// A torn-down core: every hard part, worn. The pistons, rings and bearings are scrap, and the
	// gaskets, seals, filter and head bolts are never reused, so those all have to be bought.
	const auto& spec = *m_spec;
	for (auto type : PartCatalog::All())
		if (PartCatalog::Get(type).inCore)
			for (int i = 0; i < PartCatalog::CountPerEngine(type, spec); i++)
				AddPart(PartFactory::Create(type, spec, PartQuality::OEM, PartCondition::USED));
}

void Workshop::AddPart(const pPart& part)
{
	// A rod comes with its cap, a block with its main caps, a bearing shell with its other half.
	m_inventory.push_back(part);
	for (const auto& companion : PartFactory::CreateCompanions(part, *m_spec))
		m_inventory.push_back(companion);
}

int Workshop::AddMissingFasteners()
{
	// Saves from before nuts and bolts were parts: each block comes with the reusable ones a core
	// would have.
	int blocks = static_cast<int>(std::count_if(m_inventory.begin(), m_inventory.end(), [](const pPart& part) { return part->GetType() == PartType::BLOCK; }));
	int added = 0;
	for (int block = 0; block < blocks; block++)
		for (auto type : PartCatalog::All())
		{
			const auto& info = PartCatalog::Get(type);
			if (!info.fastener.IsFastener() || !info.inCore || info.singleUse)
				continue;
			for (int i = 0; i < PartCatalog::CountPerEngine(type, *m_spec); i++, added++)
				m_inventory.push_back(PartFactory::Create(type, *m_spec, PartQuality::OEM, PartCondition::USED));
		}
	if (added > 0)
		Log(std::format("Found {} nuts and bolts that came with your engines. They're in a delivery by the door.", added));
	return added;
}

int Workshop::AddMissingHalves()
{
	// Saves from before parts came in halves: every block, rod and bearing shell gets the caps and
	// lower shells it would have come with.
	std::vector<pPart> originals = m_inventory;
	int added = 0;
	auto hasMatch = [&](PartType type, uint32_t id) {
		return std::any_of(m_inventory.begin(), m_inventory.end(), [&](const pPart& part) { return part->GetType() == type && part->GetMatchedTo() == id; });
	};
	for (const auto& part : originals)
	{
		auto type = part->GetType();
		bool needs = (type == PartType::BLOCK && !hasMatch(PartType::MAIN_CAP, part->GetId()))
			|| (type == PartType::CONNECTING_ROD && !hasMatch(PartType::ROD_CAP, part->GetId()))
			|| type == PartType::MAIN_BEARING || type == PartType::ROD_BEARING;
		if (!needs)
			continue;
		for (const auto& companion : PartFactory::CreateCompanions(part, *m_spec))
		{
			m_inventory.push_back(companion);
			added++;
		}
	}
	if (added > 0)
		Log(std::format("Found {} rod caps, main caps and lower bearing shells that go with your parts. They're in a delivery by the door.", added));
	return added;
}

int Workshop::AddMissingCamCaps()
{
	std::vector<pPart> heads;
	for (const auto& part : m_inventory)
		if (part->GetType() == PartType::CYLINDER_HEAD)
			heads.push_back(part);
	int added = 0;
	for (const auto& head : heads)
		for (const auto& cap : PartFactory::CreateCompanions(head, *m_spec))
		{
			m_inventory.push_back(cap);
			added++;
		}

	// Where the cam is already in, its caps go straight on, held down by a new set of bolts.
	for (auto& engine : m_engines)
	{
		auto& assembly = engine->assembly;
		auto head = assembly.Get(AssemblySlot::CYLINDER_HEAD, 0);
		if (!head || !assembly.Get(AssemblySlot::CAMSHAFT, 0))
			continue;
		for (const auto& part : m_inventory)
			if (part->GetType() == PartType::CAM_CAP && part->GetMatchedTo() == head->GetId())
			{
				std::string error;
				assembly.Install(AssemblySlot::CAM_CAP, part->GetMatchedPosition(), part, error);
			}
		const auto* holes = EngineAssembly::Holes(AssemblySlot::CAM_CAP_BOLT);
		for (int hole = 0; hole < assembly.SlotCount(AssemblySlot::CAM_CAP_BOLT); hole++)
		{
			if (!assembly.Get(AssemblySlot::CAM_CAP, hole / holes->perParent) || assembly.Get(AssemblySlot::CAM_CAP_BOLT, hole))
				continue;
			auto bolt = PartFactory::Create(holes->size, *m_spec, PartQuality::OEM, PartCondition::NEW);
			bolt->SetTorque(holes->torqueNm);
			bolt->SetTightness(FastenerTightness::TORQUED);
			m_inventory.push_back(bolt);
			std::string error;
			assembly.Install(AssemblySlot::CAM_CAP_BOLT, hole, bolt, error);
			added++;
		}
	}
	if (added > 0)
	{
		AssemblyChanged();
		Log(std::format("Your heads now come with cam caps: {} caps and bolts added (the ones for a cam that's in are already on).", added));
	}
	return added;
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

const PlayerStats& Workshop::GetStats() const
{
	return m_stats;
}

void Workshop::AddDistanceWalked(double metres)
{
	m_stats.metresWalked += metres;
}

bool Workshop::Spend(double amount, const std::string& what)
{
	if (amount > m_money)
	{
		Log(std::format("Can't afford {} (${:.0f}).", what, amount));
		return false;
	}
	m_money -= amount;
	m_stats.moneySpent += amount;
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
	return PartCatalog::Get(type).soldUsed;
}

double Workshop::Quote(const PartOrder& order) const
{
	return PartFactory::PriceWithCompanions(order.type, order.quality, order.condition, *m_spec) * std::max(order.quantity, 0);
}

bool Workshop::Buy(const PartOrder& order)
{
	if (order.quantity <= 0)
		return false;
	if (order.condition == PartCondition::USED && !SoldUsed(order.type))
		return false;

	if (!Spend(Quote(order), PartTypeName(order.type)))
		return false;

	std::string name;
	for (int i = 0; i < order.quantity; i++)
	{
		auto part = PartFactory::Create(order.type, *m_spec, order.quality, order.condition, order.sizeOffset);
		name = part->GetName();
		AddPart(part);
	}
	m_stats.partsBought += order.quantity;

	Log(std::format("Bought {}x {}.", order.quantity, name));
	return true;
}

void Workshop::DevAssembleEngine()
{
	// Keep passing over the slots until nothing more will go on: each pass fits whatever its
	// prerequisites now allow.
	auto& assembly = GetAssembly();
	int fitted = 0;
	for (bool progress = true; progress;)
	{
		progress = false;
		for (int slotIndex = 0; slotIndex < static_cast<int>(AssemblySlot::COUNT); slotIndex++)
		{
			auto slot = static_cast<AssemblySlot>(slotIndex);
			for (int index = 0; index < assembly.SlotCount(slot); index++)
			{
				if (assembly.Get(slot, index) || assembly.MissingPrerequisite(slot, index))
					continue;
				auto part = PartFactory::Create(EngineAssembly::SlotPartType(slot), *m_spec, PartQuality::OEM, PartCondition::NEW);
				// Caps are matched to what's already on the engine.
				if (slot == AssemblySlot::ROD_CAP)
					part->MatchTo(assembly.Get(AssemblySlot::CONNECTING_ROD, index)->GetId());
				else if (slot == AssemblySlot::MAIN_CAP)
					part->MatchTo(assembly.Get(AssemblySlot::BLOCK, 0)->GetId(), index);
				else if (slot == AssemblySlot::CAM_CAP)
					part->MatchTo(assembly.Get(AssemblySlot::CYLINDER_HEAD, 0)->GetId(), index);
				m_inventory.push_back(part);
				std::string error;
				if (assembly.Install(slot, index, part, error))
				{
					// Done up exactly to spec.
					if (const auto* holes = EngineAssembly::Holes(slot))
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
	if (fitted > 0)
		AssemblyChanged();
	Log(std::format("Dev: fitted {} new OEM parts to engine {}.", fitted, m_activeEngine + 1));
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
	m_stats.moneyEarned += price;
	m_stats.partsSold++;
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

bool Workshop::BuyFurniture(size_t furnitureIndex)
{
	const auto& furniture = FurnitureCatalog::All();
	if (furnitureIndex >= furniture.size() || !Spend(furniture[furnitureIndex].price, furniture[furnitureIndex].name))
		return false;
	m_furniture.push_back(furnitureIndex);
	Log(std::format("Bought a {}.", furniture[furnitureIndex].name));
	return true;
}

const std::vector<size_t>& Workshop::GetFurniture() const
{
	return m_furniture;
}

bool Workshop::OwnsUpgrade(UpgradeKind upgrade) const
{
	auto index = static_cast<size_t>(upgrade);
	return index < m_upgrades.size() && m_upgrades[index];
}

bool Workshop::BuyUpgrade(UpgradeKind upgrade)
{
	auto index = static_cast<size_t>(upgrade);
	const auto& info = UpgradeCatalog::All()[index];
	if (OwnsUpgrade(upgrade) || !Spend(info.price, info.name))
		return false;
	m_upgrades[index] = true;
	Log(upgrade == UpgradeKind::POST_LIFT ? std::format("Bought a {}. It's at the door in its crate.", info.name)
		: std::format("The builders have finished the {}.", info.name));
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

std::optional<std::string> Workshop::TurningProblem(AssemblySlot slot, int index, size_t toolIndex, int sizeMm, bool tightening) const
{
	const auto* holes = EngineAssembly::Holes(slot);
	auto part = GetAssembly().Get(slot, index);
	if (!holes || !part)
		return "There's nothing there to turn.";
	const auto& tools = ToolCatalog::All();
	if (toolIndex >= tools.size() || !OwnsTool(toolIndex))
		return "You don't have that tool.";
	const auto& tool = tools[toolIndex];
	const auto& fastener = PartCatalog::Get(part->GetType()).fastener;

	bool drive = tool.handKind == HandToolKind::RATCHET || tool.handKind == HandToolKind::BREAKER_BAR || tool.handKind == HandToolKind::TORQUE_WRENCH;
	if (!drive && tool.handKind != HandToolKind::WRENCHES)
		return std::format("A {} can't turn a {}.", tool.name, part->GetName());
	if (sizeMm <= 0)
		return drive ? std::format("Fit a socket to the {} first: look at one in a socket set and press E.", tool.name)
			: std::string("Pick a wrench size first: [C] changes it.");
	if (sizeMm != fastener.headMm)
		return std::format("A {} mm {} doesn't fit a {} mm {}.", sizeMm, drive ? "socket" : "wrench", fastener.headMm, fastener.nut ? "nut" : "bolt head");
	if (tool.handKind == HandToolKind::TORQUE_WRENCH)
	{
		if (!tightening)
			return std::format("Don't loosen with the {}: it knocks it out of calibration. Use a ratchet, breaker bar or wrench.", tool.name);
		if (tool.maxTorque < holes->torqueNm)
			return std::format("The {} only goes to {:.0f} Nm; this takes {:.0f}.", tool.name, tool.maxTorque, holes->torqueNm);
	}
	return std::nullopt;
}

bool Workshop::TightenFastener(AssemblySlot slot, int index, size_t toolIndex, int sizeMm)
{
	if (auto problem = TurningProblem(slot, index, toolIndex, sizeMm, true))
	{
		Log(*problem);
		return false;
	}
	auto part = GetAssembly().Get(slot, index);
	const auto* holes = EngineAssembly::Holes(slot);
	const auto& tools = ToolCatalog::All();
	const auto& tool = tools[toolIndex];

	double torque = 0.0;
	if (tool.handKind == HandToolKind::TORQUE_WRENCH)
	{
		// As tight as the wrench clicks, give or take its accuracy.
		torque = holes->torqueNm * (std::max)(0.3, 1.0 + Random::Normal(0.0, tool.torqueError));
		part->SetTightness(FastenerTightness::TORQUED);
	}
	else
	{
		// By feel. A short ratchet seldom gets a big bolt there; a breaker bar easily goes past.
		double feel = tool.handKind == HandToolKind::BREAKER_BAR ? 1.1 : tool.handKind == HandToolKind::RATCHET ? 0.8 : 0.85;
		torque = holes->torqueNm * std::clamp(Random::Normal(feel, 0.25), 0.2, 2.0);
		if (tool.handKind == HandToolKind::RATCHET)
			torque = (std::min)(torque, 70.0);
		part->SetTightness(FastenerTightness::SNUG);
	}
	// A stretched one is already past yield: it goes on, but it won't clamp as it should.
	if (part->GetCondition() == PartCondition::DAMAGED)
		torque *= 0.7;
	part->SetTorque(torque);
	AssemblyChanged();
	m_hours += 0.02;
	return true;
}

bool Workshop::LoosenFastener(AssemblySlot slot, int index, size_t toolIndex, int sizeMm)
{
	if (auto problem = TurningProblem(slot, index, toolIndex, sizeMm, false))
	{
		Log(*problem);
		return false;
	}
	auto part = GetAssembly().Get(slot, index);
	part->SetTorque(0.0);
	part->SetTightness(FastenerTightness::LOOSE);
	AssemblyChanged();
	m_hours += 0.02;
	return true;
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
	m_stats.measurements++;
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
	m_stats.measurements++;
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
		m_stats.cuts++;
		// (By hand there's no telling how much came off without measuring.)
		if (machine.hoursPerMm > 0.0)
			result.message += machine.removalError > 0.0 ? std::format(" Took {:.1f} h.", hours)
				: std::format(" Took {:.1f} h to remove {:.3f} mm.", hours, result.removed);
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
		if (m_engines[engine]->assembly.Find(&part))
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

double Workshop::LineTotal(const CartLine& line) const
{
	switch (line.kind)
	{
		case CartLine::Kind::PART:			return Quote(line.order);
		case CartLine::Kind::TOOL:			return line.index < ToolCatalog::All().size() ? ToolCatalog::All()[line.index].price : 0.0;
		case CartLine::Kind::MACHINE:		return line.index < MachineCatalog::All().size() ? MachineCatalog::All()[line.index].price : 0.0;
		case CartLine::Kind::ENGINE_STAND:	return ENGINE_STAND_PRICE * line.quantity;
		case CartLine::Kind::FURNITURE:		return line.index < FurnitureCatalog::All().size() ? FurnitureCatalog::All()[line.index].price * line.quantity : 0.0;
		case CartLine::Kind::UPGRADE:		return line.index < UpgradeCatalog::All().size() ? UpgradeCatalog::All()[line.index].price : 0.0;
	}
	return 0.0;
}

double Workshop::CartTotal(const std::vector<CartLine>& cart) const
{
	double total = 0.0;
	for (const auto& line : cart)
		total += LineTotal(line);
	return total;
}

std::optional<std::string> Workshop::CartLineProblem(const CartLine& line, const std::vector<CartLine>& cart) const
{
	switch (line.kind)
	{
		case CartLine::Kind::TOOL:
			if (OwnsTool(line.index))
				return "Already owned.";
			break;
		case CartLine::Kind::MACHINE:
			if (OwnsMachine(line.index))
				return "Already owned.";
			break;
		case CartLine::Kind::UPGRADE:
			if (OwnsUpgrade(static_cast<UpgradeKind>(line.index)))
				return "Already done.";
			break;
		case CartLine::Kind::ENGINE_STAND:
		{
			int stands = 0;
			for (const auto& other : cart)
				if (other.kind == CartLine::Kind::ENGINE_STAND)
					stands += other.quantity;
			if (GetEngineCount() + stands > MAX_ENGINES)
				return std::format("There's only room for {} engine stands.", MAX_ENGINES);
			break;
		}
		case CartLine::Kind::PART:
			if (line.order.quantity <= 0 || (line.order.condition == PartCondition::USED && !SoldUsed(line.order.type)))
				return "Not sold like that.";
			break;
		default:
			break;
	}
	return std::nullopt;
}

bool Workshop::Checkout(const std::vector<CartLine>& cart)
{
	if (cart.empty())
		return false;
	for (const auto& line : cart)
		if (auto problem = CartLineProblem(line, cart))
		{
			Log(std::format("Can't check out: {}", *problem));
			return false;
		}
	double total = CartTotal(cart);
	if (total > m_money)
	{
		Log(std::format("Can't afford the order (${:.0f}).", total));
		return false;
	}

	// Paid for line by line, so each purchase is logged and counted as usual; the total was
	// checked above, so none of them can fail for money.
	for (const auto& line : cart)
		switch (line.kind)
		{
			case CartLine::Kind::PART:			Buy(line.order); break;
			case CartLine::Kind::TOOL:			BuyTool(line.index); break;
			case CartLine::Kind::MACHINE:		BuyMachine(line.index); break;
			case CartLine::Kind::ENGINE_STAND:	for (int i = 0; i < line.quantity; i++) BuyEngineStand(); break;
			case CartLine::Kind::FURNITURE:		for (int i = 0; i < line.quantity; i++) BuyFurniture(line.index); break;
			case CartLine::Kind::UPGRADE:		BuyUpgrade(static_cast<UpgradeKind>(line.index)); break;
		}
	Log(std::format("Order placed: {} line{}, ${:.0f}. It's on its way to the roll-up door.", cart.size(), cart.size() == 1 ? "" : "s", total));
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
	if (m_activeEngine == m_engineOnDyno)
	{
		Log("That engine is on the dyno: put it back on a stand to work on it.");
		return false;
	}
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
	// Nuts and bolts go in finger tight: doing them up is a job for the tool in your hand
	// (TightenFastener).

	if (assembly.Get(slot, index))
		assembly.Remove(slot, index);

	std::string error;
	if (!assembly.Install(slot, index, part, error))
	{
		Log(error);
		return false;
	}
	part->SetTorque(0.0);
	part->SetTightness(FastenerTightness::LOOSE);
	m_stats.partsFitted++;

	AssemblyChanged();
	m_hours += 0.1;
	return true;
}

bool Workshop::RemovePart(AssemblySlot slot, int index)
{
	if (m_activeEngine == m_engineOnDyno)
	{
		Log("That engine is on the dyno: put it back on a stand to work on it.");
		return false;
	}
	// In reverse build order: nothing comes off while something is fitted on top of it.
	if (auto blocked = GetAssembly().RemovalBlockedBy(slot, index))
	{
		Log(*blocked);
		return false;
	}
	// A nut or bolt comes out by hand once it's been loosened with a tool of its size.
	if (auto part = GetAssembly().Get(slot, index); part && EngineAssembly::Holes(slot) && part->GetTightness() != FastenerTightness::LOOSE)
	{
		Log(std::format("Loosen the {} first: a {} mm socket on a ratchet or breaker bar, or a {} mm wrench.", part->GetName(),
			PartCatalog::Get(part->GetType()).fastener.headMm, PartCatalog::Get(part->GetType()).fastener.headMm));
		return false;
	}
	if (auto removed = GetAssembly().Remove(slot, index))
	{
		removed->SetTorque(0.0);
		removed->SetTightness(FastenerTightness::LOOSE);
		// Gaskets crush, seals tear and torque-to-yield bolts stretch: once fitted, they're done.
		if (PartCatalog::Get(removed->GetType()).singleUse && removed->GetCondition() != PartCondition::DAMAGED)
			removed->MarkDamaged("Single use: it was fitted once and can't go back on");
		AssemblyChanged();
	}
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
	return RecordAssemblyReading(check, index, rod, toolIndex, trueValue);
}

bool Workshop::RecordAssemblyReading(AssemblyCheck check, int index, bool rod, int toolIndex, std::optional<double> trueValue)
{
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
	m_stats.measurements++;
	Log(std::format("Checked {} on engine {} with the {}.", label, m_activeEngine + 1, tool.name));
	return true;
}

const AssemblyReading* Workshop::GetAssemblyReading(AssemblyCheck check, int index, bool rod) const
{
	const auto& readings = Active().readings;
	auto it = readings.find(AssemblyReadingLabel(check, index, rod));
	return it == readings.end() ? nullptr : &it->second;
}

int Workshop::GetEngineOnDyno() const
{
	return m_engineOnDyno;
}

bool Workshop::MoveEngineToDyno(int engine)
{
	if (engine < 0 || engine >= GetEngineCount())
		return false;
	if (m_engineOnDyno >= 0)
	{
		Log(std::format("Engine {} is on the dyno already: take it off first.", m_engineOnDyno + 1));
		return false;
	}
	if (!m_engines[engine]->assembly.IsComplete())
	{
		Log(std::format("Engine {} isn't finished: only a complete engine goes on the dyno.", engine + 1));
		return false;
	}
	m_engineOnDyno = engine;
	m_activeEngine = engine;
	Log(std::format("Moved engine {} off its stand and onto the dyno.", engine + 1));
	return true;
}

bool Workshop::IsStandFree(int stand) const
{
	if (stand < 0 || stand >= GetEngineCount())
		return false;
	return stand == m_engineOnDyno || m_engines[stand]->assembly.GetAllParts().empty();
}

bool Workshop::ReturnEngineFromDyno(int stand)
{
	if (m_engineOnDyno < 0 || !IsStandFree(stand))
		return false;
	// Engines are numbered by their stand, so going onto another stand swaps it with that stand's
	// empty build.
	if (stand != m_engineOnDyno)
		std::swap(m_engines[stand], m_engines[m_engineOnDyno]);
	m_engineOnDyno = -1;
	m_activeEngine = stand;
	Log(std::format("Took the engine off the dyno and onto stand {}.", stand + 1));
	return true;
}

const DynoResult& Workshop::RunDyno()
{
	constexpr double DYNO_COST = 50.0;

	if (m_engineOnDyno < 0)
	{
		Log("There's no engine on the dyno.");
		return Active().lastDyno;
	}
	m_activeEngine = m_engineOnDyno;
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
	m_stats.dynoPulls++;
	if (pull.failed)
		m_stats.dynoFailures++;
	else
	{
		m_stats.bestPowerKw = std::max(m_stats.bestPowerKw, pull.peakPowerKw);
		m_stats.bestTorqueNm = std::max(m_stats.bestTorqueNm, pull.peakTorqueNm);
	}
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
	// Sold off the dyno, it leaves the dyno free too.
	if (m_activeEngine == m_engineOnDyno)
		m_engineOnDyno = -1;
	m_money += value;
	m_stats.moneyEarned += value;
	m_stats.enginesSold++;
	m_stats.bestEngineSale = std::max(m_stats.bestEngineSale, value);
	Log(std::format("Sold engine {} for ${:.0f}. Its stand is free for the next one.", m_activeEngine + 1, value));
	return true;
}

void Workshop::DevAddMoney(double amount)
{
	m_money = (std::max)(0.0, m_money + amount);
	Log(std::format("Dev: {}${:.0f}.", amount < 0.0 ? "took away " : "added ", std::abs(amount)));
}

void Workshop::DevGivePart(const PartOrder& order)
{
	std::string name;
	for (int i = 0; i < order.quantity; i++)
	{
		auto part = PartFactory::Create(order.type, *m_spec, order.quality, order.condition, order.sizeOffset);
		name = part->GetName();
		AddPart(part);
	}
	Log(std::format("Dev: {}x {}, delivered to the door.", order.quantity, name));
}

bool Workshop::DevGiveTool(size_t toolIndex)
{
	if (toolIndex >= m_ownedTools.size() || m_ownedTools[toolIndex])
		return false;
	m_ownedTools[toolIndex] = true;
	Log(std::format("Dev: a {}.", ToolCatalog::All()[toolIndex].name));
	return true;
}

bool Workshop::DevGiveMachine(size_t machineIndex)
{
	if (machineIndex >= m_ownedMachines.size() || m_ownedMachines[machineIndex])
		return false;
	m_ownedMachines[machineIndex] = true;
	Log(std::format("Dev: a {}, delivered in its crate.", MachineCatalog::All()[machineIndex].name));
	return true;
}

void Workshop::DevGiveFurniture(size_t furnitureIndex)
{
	if (furnitureIndex >= FurnitureCatalog::All().size())
		return;
	m_furniture.push_back(furnitureIndex);
	Log(std::format("Dev: a {}, delivered in its crate.", FurnitureCatalog::All()[furnitureIndex].name));
}

void Workshop::DevGiveUpgrade(UpgradeKind upgrade)
{
	auto index = static_cast<size_t>(upgrade);
	if (index >= m_upgrades.size() || m_upgrades[index])
		return;
	m_upgrades[index] = true;
	Log(std::format("Dev: the {}.", UpgradeCatalog::All()[index].name));
}
