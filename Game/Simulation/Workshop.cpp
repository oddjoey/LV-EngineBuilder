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
		m_toolCounts.push_back(tool.ownedAtStart ? 1 : 0);

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

namespace
{
	constexpr double DYNO_COST = 50.0;
	constexpr double DYNO_PRICE = 1200.0;
	constexpr double HOIST_PRICE = 350.0;

	// How tight a combination wrench does a nut or bolt up, as a share of its spec: by feel, but a spanner's
	// short reach and a steady pull get it close every time, never dangerously loose or tight. Under spec
	// costs a little on the dyno (a weeping head gasket); a torque wrench does better.
	double WrenchFeel()
	{
		return std::clamp(Random::Normal(0.9, 0.07), 0.75, 1.1);
	}

	// Builds an engine out of these parts, with new OEM parts wherever they run out, every nut and bolt done
	// up by feel with the combination wrenches a new shop has, and runs it on the dyno.
	bool PassesWithNewParts(const std::vector<pPart>& parts, const EngineSpec& spec)
	{
		EngineAssembly assembly(spec);
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
					auto type = EngineAssembly::SlotPartType(slot);
					std::string error;
					pPart fitted = nullptr;
					for (const auto& part : parts)
						if (part->GetType() == type && !assembly.Contains(part) && assembly.Install(slot, index, part, error))
						{
							fitted = part;
							break;
						}
					if (!fitted)
					{
						auto part = PartFactory::Create(type, spec, PartQuality::OEM, PartCondition::NEW);
						if (!assembly.Install(slot, index, part, error))
							continue;
						fitted = part;
					}
					if (const auto* holes = EngineAssembly::Holes(slot))
					{
						fitted->SetTorque(holes->torqueNm * WrenchFeel());
						fitted->SetTightness(FastenerTightness::SNUG);
					}
					progress = true;
				}
			}
		}
		return assembly.IsComplete() && !Dyno::Run(assembly).failed;
	}
}

double Workshop::StartingMoney(const EngineSpec& spec)
{
	// Exactly enough for the first engine: new OEM parts for everything the core doesn't come with. A new
	// shop has no dyno: the buyer runs it.
	// Bought as the store sells them: the parts that come with a partner (lower shells, caps) are in its
	// price, and the core's caps came with its rods, block and head.
	double money = 0.0;
	for (auto type : PartCatalog::All())
		// (A piston comes with its pin, which is sold on its own too.)
		if (const auto& info = PartCatalog::Get(type); !info.inCore && info.listed && type != PartType::PISTON_PIN)
			money += PartFactory::PriceWithCompanions(type, PartQuality::OEM, PartCondition::NEW, spec) * PartCatalog::CountPerEngine(type, spec);
	return money;
}

void Workshop::AddCore()
{
	// A torn-down core: every hard part, worn. The pistons, rings and bearings are scrap, and the
	// gaskets, seals, filter and head bolts are never reused, so those all have to be bought.
	// The starting money only covers those, so the core has to be one that runs with them: worn, but
	// nothing on it so far gone it has to be machined or replaced. Without that, about half of cores have
	// a carburettor, a bore or a belt past saving. It has to pass a few times, with different new parts and
	// bolts done up by hand each time, so it isn't only just passing.
	const auto& spec = *m_spec;
	constexpr int MOST_TRIES = 50;
	std::vector<pPart> core;
	for (int attempt = 0; attempt < MOST_TRIES; attempt++)
	{
		core.clear();
		for (auto type : PartCatalog::All())
			if (PartCatalog::Get(type).inCore)
				for (int i = 0; i < PartCatalog::CountPerEngine(type, spec); i++)
				{
					auto part = PartFactory::Create(type, spec, PartQuality::OEM, PartCondition::USED);
					core.push_back(part);
					for (const auto& companion : PartFactory::CreateCompanions(part, spec))
						core.push_back(companion);
				}
		constexpr int PASSES = 4;
		int passes = 0;
		while (passes < PASSES && PassesWithNewParts(core, spec))
			passes++;
		if (passes == PASSES)
			break;
	}
	m_inventory.insert(m_inventory.end(), core.begin(), core.end());
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

int Workshop::AddMissing4ACParts()
{
	// Saves from before the LV-16 took the 4A-C's hardware: every block comes with the used rocker
	// shafts, distributor, flywheel, downpipe, alternator and V-belt a core would have, and their bolts.
	int blocks = static_cast<int>(std::count_if(m_inventory.begin(), m_inventory.end(), [](const pPart& part) { return part->GetType() == PartType::BLOCK; }));
	int added = 0;
	auto add = [&](PartType type, int count) {
		for (int i = 0; i < count; i++, added++)
			m_inventory.push_back(PartFactory::Create(type, *m_spec, PartQuality::OEM, PartCondition::USED));
	};
	for (int block = 0; block < blocks; block++)
	{
		for (auto type : { PartType::ROCKER_SHAFT, PartType::DISTRIBUTOR, PartType::FLYWHEEL, PartType::EXHAUST_PIPE, PartType::ALTERNATOR, PartType::DRIVE_BELT })
			add(type, PartCatalog::CountPerEngine(type, *m_spec));
		for (auto slot : { AssemblySlot::ROCKER_SHAFT_BOLT, AssemblySlot::DISTRIBUTOR_BOLT, AssemblySlot::FLYWHEEL_BOLT,
			AssemblySlot::BELT_IDLER_BOLT, AssemblySlot::EXHAUST_PIPE_BOLT, AssemblySlot::ALTERNATOR_BOLT })
		{
			const auto* holes = EngineAssembly::Holes(slot);
			add(holes->size, holes->Count(PartCatalog::CountPerEngine(holes->parent, *m_spec)));
		}
	}
	if (added > 0)
		Log(std::format("Your engines now take a flywheel, rocker shafts, a distributor, a downpipe, an alternator and a V-belt: {} parts and bolts for them are in a delivery by the door.", added));
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

const std::vector<std::string>& Workshop::GetLog() const
{
	return m_log;
}

void Workshop::Log(const std::string& message)
{
	m_log.push_back(message);
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
	return PartFactory::PriceWithCompanions(order.type, PartQuality::OEM, order.condition, *m_spec) * std::max(order.quantity, 0);
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
		auto part = PartFactory::Create(order.type, *m_spec, PartQuality::OEM, order.condition, order.sizeOffset);
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
					// A piston with its pin pressed in.
					if (slot == AssemblySlot::PISTON)
					{
						auto pin = PartFactory::Create(PartType::PISTON_PIN, *m_spec, PartQuality::OEM, PartCondition::NEW);
						m_inventory.push_back(pin);
						m_pins[part->GetId()] = pin->GetId();
					}
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
	if (ShellIn(*part) || PistonOn(*part) || PinIn(*part) || RingOn(*part) || HostOf(*part))
	{
		Log("Take it apart first: the shell out, the ring off, the pin out and the piston off its rod.");
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

bool Workshop::OwnsTool(size_t toolId) const
{
	// An id is one copy of a tool: owned if the shop has that many. What comes in a set is owned with
	// the set (copy for copy).
	size_t index = ToolCatalog::IndexOf(toolId), copy = ToolCatalog::CopyOf(toolId);
	const auto& tools = ToolCatalog::All();
	if (index < tools.size() && tools[index].partOf >= 0)
		return OwnsTool(ToolCatalog::IdOf(static_cast<size_t>(tools[index].partOf), copy));
	return index < m_toolCounts.size() && static_cast<size_t>(m_toolCounts[index]) > copy;
}

int Workshop::ToolCount(size_t toolIndex) const
{
	const auto& tools = ToolCatalog::All();
	if (toolIndex < tools.size() && tools[toolIndex].partOf >= 0)
		return ToolCount(static_cast<size_t>(tools[toolIndex].partOf));
	return toolIndex < m_toolCounts.size() ? m_toolCounts[toolIndex] : 0;
}

int Workshop::Rank() const
{
	return ShopRank::RankFor(m_stats.reputation);
}

bool Workshop::ToolUnlocked(size_t toolIndex) const
{
	// Supplier connections get you the tools a rank or two ahead; some only come with a perk.
	const auto& tool = ToolCatalog::Of(toolIndex);
	if (tool.skill >= 0 && SkillLevel(static_cast<SkillKind>(tool.skill)) == 0)
		return false;
	return tool.rank <= Rank() + SkillLevel(SkillKind::SUPPLIER);
}

std::optional<std::string> Workshop::ToolLockedBecause(size_t toolIndex) const
{
	if (ToolUnlocked(toolIndex))
		return std::nullopt;
	const auto& tool = ToolCatalog::Of(toolIndex);
	if (tool.skill >= 0 && SkillLevel(static_cast<SkillKind>(tool.skill)) == 0)
		return std::format("needs the {} perk (Rank & Skills tab).", Skills::Of(static_cast<SkillKind>(tool.skill)).name);
	return std::format("needs shop rank {} ({}).", tool.rank, ShopRank::Info(tool.rank).name);
}

int Workshop::SkillPointsEarned() const
{
	return static_cast<int>(m_stats.reputation / Skills::REPUTATION_PER_POINT);
}

int Workshop::SkillPointsSpent() const
{
	int spent = 0;
	for (size_t i = 0; i < Skills::COUNT; i++)
		for (int level = 1; level <= m_skills[i]; level++)
			spent += Skills::CostOfLevel(static_cast<SkillKind>(i), level);
	return spent;
}

int Workshop::SkillPoints() const
{
	return (std::max)(0, SkillPointsEarned() - SkillPointsSpent());
}

int Workshop::SkillLevel(SkillKind skill) const
{
	size_t i = static_cast<size_t>(skill);
	if (i >= Skills::COUNT)
		return 0;
	// An upgrade paid for with money, in an older save, counts as its perk (at no cost in points).
	for (auto upgrade : { UpgradeKind::SIDE_BAY, UpgradeKind::SECOND_STORY, UpgradeKind::POST_LIFT })
		if (UpgradePerk(upgrade) == skill && static_cast<size_t>(upgrade) < m_upgrades.size() && m_upgrades[static_cast<size_t>(upgrade)])
			return (std::max)(m_skills[i], 1);
	// Premium Colours came before the Wardrobe it now hangs off: a shop that has it can dress its monkey too.
	if (skill == SkillKind::WARDROBE && m_skills[static_cast<size_t>(SkillKind::PREMIUM_COLORS)] > 0)
		return (std::max)(m_skills[i], 1);
	return m_skills[i];
}

bool Workshop::SkillUnlocked(SkillKind skill) const
{
	SkillKind after = Skills::Of(skill).after;
	return after == SkillKind::COUNT || SkillLevel(after) > 0;
}

bool Workshop::BuySkill(SkillKind skill)
{
	const auto& info = Skills::Of(skill);
	int level = SkillLevel(skill);
	if (level >= info.maxLevel)
		return false;
	if (!SkillUnlocked(skill))
	{
		Log(std::format("{} opens up once {} is bought.", info.name, Skills::Of(info.after).name));
		return false;
	}
	int cost = Skills::CostOfLevel(skill, level + 1);
	if (cost > SkillPoints())
	{
		Log(std::format("{} needs {} skill point{}; the shop has {}.", info.name, cost, cost == 1 ? "" : "s", SkillPoints()));
		return false;
	}
	m_skills[static_cast<size_t>(skill)]++;
	Log(info.maxLevel > 1 ? std::format("{} level {} for {} skill point{}.", info.name, level + 1, cost, cost == 1 ? "" : "s")
		: std::format("{} for {} skill point{}.", info.name, cost, cost == 1 ? "" : "s"));
	if (skill == SkillKind::SIDE_BAY || skill == SkillKind::SECOND_STORY)
		Log(std::format("The builders have finished the {}.", info.name));
	else if (skill == SkillKind::POST_LIFT)
		Log("The lift is at the door in its crate.");
	return true;
}

double Workshop::TurnTimeFactor() const
{
	return std::pow(0.85, SkillLevel(SkillKind::QUICK_HANDS));
}

double Workshop::MeasureScatterFactor() const
{
	return std::pow(0.85, SkillLevel(SkillKind::STEADY_HANDS));
}

double Workshop::BatteryUseFactor() const
{
	return 1.0 - 0.25 * SkillLevel(SkillKind::BATTERY_SAVER);
}

double Workshop::SaleFactor() const
{
	return 1.0 + 0.05 * SkillLevel(SkillKind::SALESMAN);
}

double Workshop::WalkSpeedFactor() const
{
	return 1.0 + 0.08 * SkillLevel(SkillKind::LONG_LEGS);
}

double Workshop::DynoCost() const
{
	return DYNO_COST * (1.0 - 0.25 * SkillLevel(SkillKind::DYNO_CONTRACT));
}

void Workshop::EarnReputation(double amount)
{
	int before = Rank();
	m_stats.reputation += amount;
	for (int rank = before + 1; rank <= Rank(); rank++)
	{
		const auto& info = ShopRank::Info(rank);
		m_money += info.bonus;
		m_stats.moneyEarned += info.bonus;
		std::string unlocked;
		for (const auto& tool : ToolCatalog::All())
			if (tool.rank == rank && tool.partOf < 0)
				unlocked += (unlocked.empty() ? "" : ", ") + tool.name;
		Log(std::format("The shop is now rank {}, {}: a ${:.0f} bonus{}.", rank, info.name, info.bonus,
			unlocked.empty() ? "" : ", and the store now sells the " + unlocked));
	}
}

bool Workshop::OwnsMachine(size_t machineIndex) const
{
	return machineIndex < m_ownedMachines.size() && m_ownedMachines[machineIndex];
}

bool Workshop::BuyTool(size_t toolIndex)
{
	const auto& tools = ToolCatalog::All();
	// As many as you like: each one is another copy (another set, for a set).
	if (toolIndex >= tools.size() || tools[toolIndex].partOf >= 0 || m_toolCounts[toolIndex] >= static_cast<int>(MAX_TOOL_COPIES))
		return false;
	if (auto locked = ToolLockedBecause(toolIndex))
	{
		Log(std::format("The {}: {}", tools[toolIndex].name, *locked));
		return false;
	}
	if (!Spend(tools[toolIndex].price, tools[toolIndex].name))
		return false;

	m_toolCounts[toolIndex]++;
	Log(m_toolCounts[toolIndex] > 1 ? std::format("Bought another {} (you have {}).", tools[toolIndex].name, m_toolCounts[toolIndex])
		: std::format("Bought a {}.", tools[toolIndex].name));
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
	// Built with its perk, or paid for in a save from when they were bought with money.
	auto index = static_cast<size_t>(upgrade);
	return (index < m_upgrades.size() && m_upgrades[index]) || m_skills[static_cast<size_t>(UpgradePerk(upgrade))] > 0;
}

SkillKind Workshop::UpgradePerk(UpgradeKind upgrade)
{
	switch (upgrade)
	{
		case UpgradeKind::SIDE_BAY:		return SkillKind::SIDE_BAY;
		case UpgradeKind::SECOND_STORY:	return SkillKind::SECOND_STORY;
		default:						return SkillKind::POST_LIFT;
	}
}

bool Workshop::OwnsDyno() const
{
	return m_ownsDyno;
}

bool Workshop::BuyDyno()
{
	if (m_ownsDyno || !Spend(DYNO_PRICE, "a dyno"))
		return false;
	m_ownsDyno = true;
	Log("Bought a dyno. It's at the door in its crate.");
	return true;
}

double Workshop::DynoPrice()
{
	return DYNO_PRICE;
}

bool Workshop::OwnsHoist() const
{
	return m_ownsHoist;
}

bool Workshop::BuyHoist()
{
	if (m_ownsHoist || !Spend(HOIST_PRICE, "an engine hoist"))
		return false;
	m_ownsHoist = true;
	Log("Bought an engine hoist. It's at the door in its crate.");
	return true;
}

double Workshop::HoistPrice()
{
	return HOIST_PRICE;
}

int Workshop::BestToolFor(const Feature& feature) const
{
	const auto& tools = ToolCatalog::All();
	int best = -1;
	for (size_t i = 0; i < tools.size(); i++)
		if (OwnsTool(i) && tools[i].CanMeasure(feature))
			if (best < 0 || tools[i].sigma < tools[best].sigma)
				best = static_cast<int>(i);
	return best;
}

int Workshop::BestToolFor(AssemblyCheck check) const
{
	const auto& tools = ToolCatalog::All();
	int best = -1;
	for (size_t i = 0; i < tools.size(); i++)
		if (OwnsTool(i) && tools[i].CanCheck(check))
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
	if (ToolCatalog::IndexOf(toolIndex) >= tools.size() || !OwnsTool(toolIndex))
		return "You don't have that tool.";
	const auto& tool = ToolCatalog::Of(toolIndex);
	const auto& fastener = PartCatalog::Get(part->GetType()).fastener;

	bool drive = tool.handKind == HandToolKind::RATCHET || tool.handKind == HandToolKind::BREAKER_BAR || tool.handKind == HandToolKind::TORQUE_WRENCH
		|| tool.handKind == HandToolKind::IMPACT;
	if (tool.handKind == HandToolKind::WRENCHES)
		return std::string("Take a wrench out of the set: look at the one you need and press [E].");
	if (!drive && tool.handKind != HandToolKind::COMBINATION_WRENCH)
		return std::format("A {} can't turn a {}.", tool.name, part->GetName());
	if (sizeMm <= 0)
		return std::format("Fit a socket to the {} first: look at one in a socket set and press [E].", tool.name);
	if (sizeMm != fastener.headMm)
		return std::format("A {} mm {} doesn't fit a {} mm {}.", sizeMm, drive ? "socket" : "wrench", fastener.headMm, fastener.nut ? "nut" : "bolt head");
	if (tool.handKind == HandToolKind::TORQUE_WRENCH)
	{
		if (!tightening)
			return std::format("Don't loosen with the {}: it knocks it out of calibration. Use a ratchet, breaker bar or wrench.", tool.name);
		if (tool.maxTorque < holes->torqueNm)
			return std::format("The {} only goes to {:.0f} Nm; this takes {:.0f}.", tool.name, tool.maxTorque, holes->torqueNm);
	}
	// A motor only turns so hard: past that it stalls (and a tight one won't crack loose).
	if (tool.powered && tool.maxTorque < holes->torqueNm)
		return std::format("The {} stalls at {:.0f} Nm; this one takes {:.0f}. Use a hand tool.", tool.name, tool.maxTorque, holes->torqueNm);
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
	const auto& tool = ToolCatalog::Of(toolIndex);

	double torque = 0.0;
	if (tool.handKind == HandToolKind::TORQUE_WRENCH)
	{
		// As tight as the wrench clicks, give or take its accuracy.
		torque = holes->torqueNm * (std::max)(0.3, 1.0 + Random::Normal(0.0, tool.torqueError));
		part->SetTightness(FastenerTightness::TORQUED);
	}
	else
	{
		// By feel. A short ratchet seldom gets a big bolt there; a breaker bar easily goes past, and an
		// impact hammers it well beyond. A combination wrench gets it close (see WrenchFeel).
		bool impact = tool.handKind == HandToolKind::IMPACT;
		double feel = impact ? 1.4 : tool.handKind == HandToolKind::BREAKER_BAR ? 1.1 : 0.8;
		torque = tool.handKind == HandToolKind::COMBINATION_WRENCH ? holes->torqueNm * WrenchFeel()
			: holes->torqueNm * std::clamp(Random::Normal(feel, impact ? 0.3 : 0.25), 0.2, 2.0);
		if (tool.handKind == HandToolKind::RATCHET)
			torque = (std::min)(torque, 70.0);
		part->SetTightness(FastenerTightness::SNUG);
	}
	// A stretched one is already past yield: it goes on, but it won't clamp as it should.
	if (part->GetCondition() == PartCondition::DAMAGED)
		torque *= 0.7;
	part->SetTorque(torque);
	AssemblyChanged();
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
	return true;
}

int Workshop::FinishTool() const
{
	const auto& tools = ToolCatalog::All();
	for (size_t i = 0; i < tools.size(); i++)
		if (OwnsTool(i) && tools[i].measuresFinish)
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
	if (toolIndex < 0 || !OwnsTool(toolIndex) || !ToolCatalog::Of(toolIndex).CanMeasure(*feature))
	{
		Log(std::format("You don't have a tool that can measure {}.", featureName));
		return false;
	}
	const auto& tool = ToolCatalog::Of(toolIndex);

	FeatureReading reading;
	if (auto previous = part.GetReading(featureName); previous && !previous->stale)
		reading = *previous;

	reading.toolName = tool.name;
	reading.hasSize = true;
	reading.stale = false;
	for (int station = 0; station < Feature::STATIONS; station++)
		for (int axis = 0; axis < Feature::AXES; axis++)
			reading.samples[station][axis] = tool.Read(feature->samples[station][axis], MeasureScatterFactor());
	reading.value = tool.Read(feature->value, MeasureScatterFactor());
	if (feature->kind == FeatureKind::FLATNESS || feature->kind == FeatureKind::SQUARENESS || feature->kind == FeatureKind::ALIGNMENT)
		reading.value = std::max(0.0, reading.value);

	part.RecordReading(featureName, reading);
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
	if (toolIndex < 0 || !OwnsTool(toolIndex) || !ToolCatalog::Of(toolIndex).measuresFinish)
	{
		Log("You need a profilometer to measure surface finish.");
		return false;
	}
	const auto& tool = ToolCatalog::Of(toolIndex);

	FeatureReading reading;
	if (auto previous = part.GetReading(featureName); previous && !previous->stale)
		reading = *previous;

	reading.stale = false;
	reading.finishRa = std::max(0.01, tool.Read(feature->surfaceRa, MeasureScatterFactor()));
	part.RecordReading(featureName, reading);
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
		m_stats.cuts++;
		// How much came off (by hand there's no telling without measuring).
		if (machine.removalError <= 0.0 && result.removed > 0.0)
			result.message += std::format(" Removed {:.3f} mm.", result.removed);
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
		case CartLine::Kind::TOOL:			return line.index < ToolCatalog::All().size() ? ToolCatalog::Of(line.index).price * line.quantity : 0.0;
		case CartLine::Kind::MACHINE:		return line.index < MachineCatalog::All().size() ? MachineCatalog::All()[line.index].price : 0.0;
		case CartLine::Kind::ENGINE_STAND:	return ENGINE_STAND_PRICE * line.quantity;
		case CartLine::Kind::FURNITURE:		return line.index < FurnitureCatalog::All().size() ? FurnitureCatalog::All()[line.index].price * line.quantity : 0.0;
		case CartLine::Kind::DYNO:			return DYNO_PRICE;
		case CartLine::Kind::HOIST:			return HOIST_PRICE;
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
			if (auto locked = ToolLockedBecause(line.index))
				return *locked;
			if (ToolCount(line.index) + line.quantity > MAX_TOOL_COPIES)
				return std::format("There's only room for {} of each tool.", MAX_TOOL_COPIES);
			break;
		case CartLine::Kind::MACHINE:
			if (OwnsMachine(line.index))
				return "Already owned.";
			break;
		case CartLine::Kind::DYNO:
			if (m_ownsDyno)
				return "Already owned.";
			break;
		case CartLine::Kind::HOIST:
			if (m_ownsHoist)
				return "Already owned.";
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
			case CartLine::Kind::TOOL:			for (int i = 0; i < line.quantity; i++) BuyTool(line.index); break;
			case CartLine::Kind::MACHINE:		BuyMachine(line.index); break;
			case CartLine::Kind::ENGINE_STAND:	for (int i = 0; i < line.quantity; i++) BuyEngineStand(); break;
			case CartLine::Kind::FURNITURE:		for (int i = 0; i < line.quantity; i++) BuyFurniture(line.index); break;
			case CartLine::Kind::DYNO:			BuyDyno(); break;
			case CartLine::Kind::HOIST:			BuyHoist(); break;
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

bool Workshop::ShellFits(PartType host, PartType shell)
{
	if (shell == PartType::PISTON)
		return false;
	auto carried = EngineAssembly::GoesInWith(EngineAssembly::SlotFor(host));
	return std::any_of(carried.begin(), carried.end(), [&](AssemblySlot slot) { return EngineAssembly::SlotPartType(slot) == shell; });
}

pPart Workshop::FindPart(uint32_t id) const
{
	for (const auto& part : m_inventory)
		if (part->GetId() == id)
			return part;
	return nullptr;
}

pPart Workshop::ShellIn(const Part& host) const
{
	auto it = m_shells.find(host.GetId());
	return it == m_shells.end() ? nullptr : FindPart(it->second);
}

pPart Workshop::PistonOn(const Part& rod) const
{
	auto it = m_pistons.find(rod.GetId());
	return it == m_pistons.end() ? nullptr : FindPart(it->second);
}

pPart Workshop::PinIn(const Part& piston) const
{
	auto it = m_pins.find(piston.GetId());
	return it == m_pins.end() ? nullptr : FindPart(it->second);
}

pPart Workshop::HostOf(const Part& part) const
{
	for (const auto* fittings : { &m_shells, &m_pistons, &m_pins, &m_rings })
		for (const auto& [host, fitted] : *fittings)
			if (fitted == part.GetId())
				return FindPart(host);
	return nullptr;
}

bool Workshop::FitShell(const pPart& host, const pPart& shell)
{
	if (!host || !shell || !ShellFits(host->GetType(), shell->GetType()))
		return false;
	if (EngineOf(*host) >= 0 || EngineOf(*shell) >= 0)
	{
		Log("Shells are fitted off the engine.");
		return false;
	}
	if (ShellIn(*host))
	{
		Log(std::format("The {} has a shell in it already.", host->GetName()));
		return false;
	}
	if (HostOf(*shell))
		return false;
	m_shells[host->GetId()] = shell->GetId();
	Log(std::format("Fitted the {} into the {}.", shell->GetName(), host->GetName()));
	return true;
}

pPart Workshop::TakeShellOut(const pPart& host)
{
	auto shell = host ? ShellIn(*host) : nullptr;
	if (!shell)
		return nullptr;
	m_shells.erase(host->GetId());
	Log(std::format("Took the {} out of the {}.", shell->GetName(), host->GetName()));
	return shell;
}

bool Workshop::FitPiston(const pPart& rod, const pPart& piston)
{
	if (!rod || !piston || rod->GetType() != PartType::CONNECTING_ROD || piston->GetType() != PartType::PISTON)
		return false;
	if (EngineOf(*rod) >= 0 || EngineOf(*piston) >= 0)
	{
		Log("A piston goes onto its rod off the engine.");
		return false;
	}
	if (PistonOn(*rod))
	{
		Log(std::format("The {} has a piston on it already.", rod->GetName()));
		return false;
	}
	if (HostOf(*piston))
		return false;
	m_pistons[rod->GetId()] = piston->GetId();
	Log(std::format("Put the {} on the {}: press its pin in to hold it.", piston->GetName(), rod->GetName()));
	return true;
}

pPart Workshop::TakePistonOff(const pPart& rod)
{
	auto piston = rod ? PistonOn(*rod) : nullptr;
	if (!piston)
		return nullptr;
	if (PinIn(*piston))
	{
		Log("Press its pin out first: hold the right button with the Piston Pin Press on it.");
		return nullptr;
	}
	m_pistons.erase(rod->GetId());
	Log(std::format("Took the {} off the {}.", piston->GetName(), rod->GetName()));
	return piston;
}

pPart Workshop::LoosePin() const
{
	for (const auto& part : m_inventory)
		if (part->GetType() == PartType::PISTON_PIN && !HostOf(*part))
			return part;
	return nullptr;
}

pPart Workshop::PressPinIn(const pPart& piston)
{
	if (!piston || PinIn(*piston) || !HostOf(*piston))
		return nullptr;
	auto pin = LoosePin();
	if (!pin)
	{
		Log("There's no piston pin to press in: each piston comes with one, and they're sold with them.");
		return nullptr;
	}
	m_pins[piston->GetId()] = pin->GetId();
	Log(std::format("Pressed a {} into the {}.", pin->GetName(), piston->GetName()));
	return pin;
}

pPart Workshop::PressPinOut(const pPart& piston)
{
	auto pin = piston ? PinIn(*piston) : nullptr;
	if (!pin)
		return nullptr;
	if (EngineOf(*piston) >= 0)
	{
		Log("Take the rod and piston out of the engine first.");
		return nullptr;
	}
	m_pins.erase(piston->GetId());
	Log(std::format("Pressed the {} out of the {}.", pin->GetName(), piston->GetName()));
	return pin;
}

pPart Workshop::RingOn(const Part& piston) const
{
	auto it = m_rings.find(piston.GetId());
	return it == m_rings.end() ? nullptr : FindPart(it->second);
}

bool Workshop::RingsCompressed(const Part& piston) const
{
	return m_compressed.contains(piston.GetId());
}

pPart Workshop::FittedIn(const Part& host, AssemblySlot slot) const
{
	switch (slot)
	{
		case AssemblySlot::PISTON:		return PistonOn(host);
		case AssemblySlot::PISTON_RING:	return RingOn(host);
		default:						return ShellIn(host);
	}
}

std::map<uint32_t, uint32_t>& Workshop::FittingsFor(AssemblySlot slot)
{
	switch (slot)
	{
		case AssemblySlot::PISTON:		return m_pistons;
		case AssemblySlot::PISTON_RING:	return m_rings;
		default:						return m_shells;
	}
}

bool Workshop::FitRing(const pPart& piston, const pPart& ring)
{
	if (!piston || !ring || piston->GetType() != PartType::PISTON || ring->GetType() != PartType::PISTON_RING)
		return false;
	if (EngineOf(*piston) >= 0 || EngineOf(*ring) >= 0)
	{
		Log("A ring goes onto its piston off the engine.");
		return false;
	}
	if (RingOn(*piston))
	{
		Log(std::format("The {} has a ring on it already.", piston->GetName()));
		return false;
	}
	if (HostOf(*ring))
		return false;
	m_rings[piston->GetId()] = ring->GetId();
	Log(std::format("Fitted the {} onto the {}.", ring->GetName(), piston->GetName()));
	return true;
}

pPart Workshop::TakeRingOff(const pPart& piston)
{
	auto ring = piston ? RingOn(*piston) : nullptr;
	if (!ring)
		return nullptr;
	m_rings.erase(piston->GetId());
	m_compressed.erase(piston->GetId());
	Log(std::format("Took the {} off the {}.", ring->GetName(), piston->GetName()));
	return ring;
}

bool Workshop::CompressRings(const pPart& piston, bool squeezed)
{
	if (!piston || EngineOf(*piston) >= 0)
		return false;
	if (squeezed && !RingOn(*piston))
	{
		Log("There's no ring on that piston to squeeze.");
		return false;
	}
	if (squeezed)
		m_compressed.insert(piston->GetId());
	else
		m_compressed.erase(piston->GetId());
	Log(squeezed ? std::format("The {}'s ring is squeezed in the compressor: it can go down the bore.", piston->GetName())
		: std::format("Let the compressor off the {}.", piston->GetName()));
	return true;
}

bool Workshop::InstallPart(AssemblySlot slot, int index, const pPart& part)
{
	if (m_activeEngine == m_engineOnDyno)
	{
		Log("That engine is on the dyno: put it back on a stand to work on it.");
		return false;
	}
	if (m_activeEngine == m_engineOnHoist)
	{
		Log("That engine is hanging from the hoist: lower it onto a stand to work on it.");
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

	// A shell, piston or ring goes in with the rod, cap or piston it's fitted to, and a rod or cap only with them: its
	// shell in, and a rod's piston pinned on, ringed, and the ring squeezed in the compressor to go down the bore.
	if (auto host = EngineAssembly::CarriedBy(slot))
	{
		Log(std::format("The {} goes {} a {} off the engine, and in with it.", part->GetName(), slot == AssemblySlot::PISTON || slot == AssemblySlot::PISTON_RING ? "onto" : "into",
			PartTypeName(EngineAssembly::SlotPartType(*host))));
		return false;
	}
	// What it carries, and what that carries, each with the part it's on.
	std::vector<std::pair<AssemblySlot, pPart>> carried, hosts = { { slot, part } };
	for (size_t host = 0; host < hosts.size(); host++)
		for (auto with : EngineAssembly::GoesInWith(hosts[host].first))
		{
			const Part& on = *hosts[host].second;
			pPart fitted = FittedIn(on, with);
			if (!fitted)
			{
				Log(with == AssemblySlot::PISTON ? std::format("Put a piston on the {} first, pinned: it goes in with it.", on.GetName())
					: with == AssemblySlot::PISTON_RING ? std::format("Fit a piston ring onto the {} first: it goes in with it.", on.GetName())
					: std::format("Fit a {} into the {} first: it goes in with it.", PartTypeName(EngineAssembly::SlotPartType(with)), on.GetName()));
				return false;
			}
			if (with == AssemblySlot::PISTON && !PinIn(*fitted))
			{
				Log("Press the piston's pin in first: hold the left button on it with the Piston Pin Press.");
				return false;
			}
			carried.push_back({ with, fitted });
			hosts.push_back({ with, fitted });
		}
	for (const auto& [with, fitted] : carried)
		if (with == AssemblySlot::PISTON && !m_compressed.contains(fitted->GetId()))
		{
			Log("Squeeze the piston's ring in first: hold the left button on it with the Piston Ring Compressor.");
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
	for (const auto& [with, fitted] : carried)
	{
		if (assembly.Get(with, index))
			assembly.Remove(with, index);
		if (assembly.Install(with, index, fitted, error))
			m_stats.partsFitted++;
	}
	for (const auto& [with, on] : hosts)
	{
		m_shells.erase(on->GetId());
		m_pistons.erase(on->GetId());
		m_rings.erase(on->GetId());
		m_compressed.erase(on->GetId());
	}

	AssemblyChanged();
	return true;
}

bool Workshop::RemovePart(AssemblySlot slot, int index)
{
	if (m_activeEngine == m_engineOnDyno)
	{
		Log("That engine is on the dyno: put it back on a stand to work on it.");
		return false;
	}
	if (m_activeEngine == m_engineOnHoist)
	{
		Log("That engine is hanging from the hoist: lower it onto a stand to work on it.");
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
		// What it carries comes off with it: its shell in it, a rod's piston on it (pinned), the piston's ring.
		std::vector<std::pair<AssemblySlot, pPart>> hosts = { { slot, removed } };
		for (size_t host = 0; host < hosts.size(); host++)
			for (auto with : EngineAssembly::GoesInWith(hosts[host].first))
				if (auto fitted = GetAssembly().Remove(with, index))
				{
					FittingsFor(with)[hosts[host].second->GetId()] = fitted->GetId();
					hosts.push_back({ with, fitted });
				}
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
		case AssemblyCheck::ROD_SIDE_CLEARANCE:	return std::format("Rod {} side clearance", index + 1);
		case AssemblyCheck::BELT_DEFLECTION:	return index == 1 ? "Fan belt deflection" : "Timing belt deflection";
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
		case AssemblyCheck::ROD_SIDE_CLEARANCE:	trueValue = assembly.RodSideClearance(index); break;
		case AssemblyCheck::BELT_DEFLECTION:	trueValue = assembly.BeltDeflection(index == 1); break;
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
	if (toolIndex < 0 || !OwnsTool(toolIndex) || !ToolCatalog::Of(toolIndex).CanCheck(check))
	{
		Log(std::format("You don't have a tool that can check {}.", label));
		return false;
	}
	const auto& tool = ToolCatalog::Of(toolIndex);

	double reading = tool.Read(*trueValue, MeasureScatterFactor());
	// Plastigauge and feelers can't read a negative gap; the parts simply bind.
	if (check != AssemblyCheck::DECK_CLEARANCE)
		reading = std::max(0.0, reading);

	Active().readings[label] = { reading, false };
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
	if (!m_ownsDyno)
	{
		Log("The shop hasn't a dyno: buy one at the parts computer, or sell the engine untested.");
		return false;
	}
	if (m_engineOnDyno >= 0)
	{
		Log(std::format("Engine {} is on the dyno already: take it off first.", m_engineOnDyno + 1));
		return false;
	}
	if (engine == m_engineOnHoist)
	{
		Log(std::format("Engine {} is hanging from the hoist: lower it onto the dyno with the hoist.", engine + 1));
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
	if (stand == m_engineOnHoist)
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

int Workshop::GetEngineOnHoist() const
{
	return m_engineOnHoist;
}

bool Workshop::HoistEngine(int engine)
{
	if (engine < 0 || engine >= GetEngineCount() || !m_ownsHoist)
		return false;
	if (m_engineOnHoist >= 0)
	{
		Log(std::format("Engine {} is hanging from the hoist already: lower it first.", m_engineOnHoist + 1));
		return false;
	}
	bool offDyno = engine == m_engineOnDyno;
	if (!offDyno && m_engines[engine]->assembly.GetAllParts().empty())
	{
		Log(std::format("There's no engine on stand {} to lift.", engine + 1));
		return false;
	}
	if (offDyno)
		m_engineOnDyno = -1;
	m_engineOnHoist = engine;
	m_activeEngine = engine;
	Log(std::format("Lifted engine {} off the {} onto the hoist.", engine + 1, offDyno ? "dyno" : "stand"));
	return true;
}

bool Workshop::IsStandFreeForHoist(int stand) const
{
	if (stand < 0 || stand >= GetEngineCount() || stand == m_engineOnDyno)
		return false;
	return stand == m_engineOnHoist || m_engines[stand]->assembly.GetAllParts().empty();
}

bool Workshop::LowerEngineOntoStand(int stand)
{
	if (m_engineOnHoist < 0 || !IsStandFreeForHoist(stand))
		return false;
	// Engines are numbered by their stand, so going onto another stand swaps it with that stand's
	// empty build.
	if (stand != m_engineOnHoist)
		std::swap(m_engines[stand], m_engines[m_engineOnHoist]);
	m_engineOnHoist = -1;
	m_activeEngine = stand;
	Log(std::format("Lowered the engine off the hoist onto stand {}.", stand + 1));
	return true;
}

bool Workshop::LowerEngineOntoDyno()
{
	if (m_engineOnHoist < 0)
		return false;
	if (!m_ownsDyno)
	{
		Log("The shop hasn't a dyno: buy one at the parts computer.");
		return false;
	}
	if (m_engineOnDyno >= 0)
	{
		Log(std::format("Engine {} is on the dyno already: take it off first.", m_engineOnDyno + 1));
		return false;
	}
	if (!m_engines[m_engineOnHoist]->assembly.IsComplete())
	{
		Log(std::format("Engine {} isn't finished: only a complete engine goes on the dyno.", m_engineOnHoist + 1));
		return false;
	}
	m_engineOnDyno = m_engineOnHoist;
	m_activeEngine = m_engineOnHoist;
	m_engineOnHoist = -1;
	Log(std::format("Lowered engine {} off the hoist onto the dyno.", m_engineOnDyno + 1));
	return true;
}

const DynoResult& Workshop::RunDyno()
{
	if (m_engineOnDyno < 0)
	{
		Log("There's no engine on the dyno.");
		return Active().lastDyno;
	}
	m_activeEngine = m_engineOnDyno;
	auto& engine = Active();
	if (!m_ownsDyno)
	{
		Log("The shop hasn't a dyno: buy one at the parts computer.");
		return engine.lastDyno;
	}
	if (!engine.assembly.IsComplete())
	{
		Log("The engine isn't fully assembled.");
		return engine.lastDyno;
	}
	if (!Spend(DynoCost(), "dyno fuel and time"))
		return engine.lastDyno;

	engine.lastDyno = Dyno::Run(engine.assembly);
	engine.dynoCurrent = true;

	const auto& pull = engine.lastDyno;
	m_stats.dynoPulls++;
	if (pull.failed)
		m_stats.dynoFailures++;
	else
	{
		m_stats.bestPowerKw = std::max(m_stats.bestPowerKw, pull.peakPowerKw);
		m_stats.bestTorqueNm = std::max(m_stats.bestTorqueNm, pull.peakTorqueNm);
		EarnReputation(ShopRank::FOR_CLEAN_PULL);
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

const EngineSale& Workshop::GetLastSale() const
{
	return m_lastSale;
}

bool Workshop::SellEngine()
{
	auto& engine = Active();
	if (!engine.assembly.IsComplete())
	{
		Log(std::format("Engine {} isn't finished: buyers only take a complete engine.", m_activeEngine + 1));
		return false;
	}

	// The buyer runs it on their own dyno before paying, so what it makes, how healthy it is and what
	// it's worth only come out then. They pay for horsepower from an engine they can trust; one that
	// lets go on their dyno is bought as a core, for its parts.
	double partsValue = 0.0;
	for (const auto& part : engine.assembly.GetAllParts())
		partsValue += part->GetValue();
	DynoResult pull = Dyno::Run(engine.assembly);
	double horsepower = pull.failed ? 0.0 : pull.peakPowerKw * 1.341;
	double value = pull.failed ? std::round(partsValue * 0.3 * SaleFactor())
		: std::round((partsValue * 0.8 + horsepower * 8.0 * pull.healthScore / 100.0) * SaleFactor());
	double earned = pull.failed ? 0.0 : ShopRank::ForSale(pull.healthScore, horsepower);
	if (!pull.failed)
	{
		m_stats.bestPowerKw = std::max(m_stats.bestPowerKw, pull.peakPowerKw);
		m_stats.bestTorqueNm = std::max(m_stats.bestTorqueNm, pull.peakTorqueNm);
	}
	m_lastSale = { true, m_activeEngine, pull, value, earned };

	// The pins go with their pistons.
	auto sold = engine.assembly.GetAllParts();
	for (const auto& part : engine.assembly.GetAllParts())
		if (auto pin = PinIn(*part))
		{
			sold.push_back(pin);
			m_pins.erase(part->GetId());
		}
	for (const auto& part : sold)
		m_inventory.erase(std::remove(m_inventory.begin(), m_inventory.end(), part), m_inventory.end());

	engine.assembly.Clear();
	engine.readings.clear();
	engine.dynoCurrent = false;
	// Sold off the dyno, it leaves the dyno free too.
	if (m_activeEngine == m_engineOnDyno)
		m_engineOnDyno = -1;
	if (m_activeEngine == m_engineOnHoist)
		m_engineOnHoist = -1;
	m_money += value;
	m_stats.moneyEarned += value;
	m_stats.enginesSold++;
	m_stats.bestEngineSale = std::max(m_stats.bestEngineSale, value);
	if (pull.failed)
		Log(std::format("Engine {} let go on the buyer's dyno at {:.0f} rpm: they took it as a core, for ${:.0f}. Its stand is free for the next one.",
			m_activeEngine + 1, pull.failureRpm, value));
	else
		Log(std::format("Sold engine {} for ${:.0f}: {:.0f} hp, {:.0f} Nm, {:.0f}% healthy on the buyer's dyno (+{:.0f} reputation). Its stand is free for the next one.",
			m_activeEngine + 1, value, horsepower, pull.peakTorqueNm, pull.healthScore, earned));
	EarnReputation(earned);
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
		auto part = PartFactory::Create(order.type, *m_spec, PartQuality::OEM, order.condition, order.sizeOffset);
		name = part->GetName();
		AddPart(part);
	}
	Log(std::format("Dev: {}x {}, delivered to the door.", order.quantity, name));
}

bool Workshop::DevGiveTool(size_t toolIndex)
{
	if (toolIndex >= m_toolCounts.size() || ToolCatalog::Of(toolIndex).partOf >= 0 || m_toolCounts[toolIndex] >= static_cast<int>(MAX_TOOL_COPIES))
		return false;
	m_toolCounts[toolIndex]++;
	Log(std::format("Dev: a {}.", ToolCatalog::Of(toolIndex).name));
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

void Workshop::DevAddReputation(double amount)
{
	Log(std::format("Dev: +{:.0f} reputation.", amount));
	EarnReputation(amount);
}
