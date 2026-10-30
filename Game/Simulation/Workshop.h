#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Dyno.h"
#include "EngineAssembly.h"
#include "Machine.h"
#include "MeasuringTool.h"
#include "PartFactory.h"
#include "Upgrades.h"

struct PartOrder
{
	PartType type = PartType::BLOCK;
	PartQuality quality = PartQuality::OEM;
	PartCondition condition = PartCondition::NEW;
	double sizeOffset = 0.0;
	int quantity = 1;
};

// One line in the parts computer's cart.
struct CartLine
{
	enum class Kind { PART, TOOL, MACHINE, ENGINE_STAND, FURNITURE, UPGRADE };
	Kind kind = Kind::PART;
	PartOrder order = {};		// PART: what, and how many (order.quantity)
	size_t index = 0;			// TOOL / MACHINE / FURNITURE / UPGRADE: which one
	int quantity = 1;			// ENGINE_STAND / FURNITURE: how many
};

struct AssemblyReading
{
	double value = 0.0;
	bool stale = false; // parts have been swapped since this reading was taken
};

// One engine being built on its own stand: what's fitted to it, the clearances checked on it, and
// its last dyno pull.
struct EngineBuild
{
	explicit EngineBuild(const EngineSpec& spec) : assembly(spec) {}

	EngineAssembly assembly;
	std::map<std::string, AssemblyReading> readings = {};
	DynoResult lastDyno = {};
	bool dynoCurrent = false;
};

// A running tally of the player's career, shown in the backpack and saved with the game.
struct PlayerStats
{
	double moneySpent = 0.0;
	double moneyEarned = 0.0;
	int partsBought = 0;
	int coresBought = 0;
	int partsSold = 0;
	int partsFitted = 0;
	int measurements = 0;
	int cuts = 0;
	int dynoPulls = 0;
	int dynoFailures = 0;
	int enginesSold = 0;
	double bestPowerKw = 0.0;
	double bestTorqueNm = 0.0;
	double bestEngineSale = 0.0;
	double metresWalked = 0.0;
};

// The player's shop: money, the clock, inventory, owned equipment and the engines on the stands.
// Every game rule goes through here so the UI never mutates the simulation directly.
class Workshop
{
	friend class SaveGame;
public:
	// Engines are built one per engine stand; more stands can be bought, up to this many.
	static constexpr int MAX_ENGINES = 4;
	static constexpr double ENGINE_STAND_PRICE = 250.0;

	Workshop(const EngineSpec& spec, double startingMoney);
	~Workshop();

	const EngineSpec& GetSpec() const;
	double GetMoney() const;
	double GetHours() const;
	const std::vector<std::string>& GetLog() const;
	const PlayerStats& GetStats() const;
	// The shop floor reports how far the player has walked.
	void AddDistanceWalked(double metres);

	// Supplier
	static std::vector<double> SizeOptions(PartType type);
	static bool SoldUsed(PartType type);
	double Quote(const PartOrder& order) const;
	bool Buy(const PartOrder& order);
	// A tired core to start another engine with: every hard part, used, as the shop started with
	// (see PartInfo::inCore). Sold as a lot, cheaper than piece by piece.
	// The cart: what a line costs, the whole cart's total, and buying it all in one go. Checkout
	// buys nothing unless the shop can pay for everything; whatever was bought is shipped together.
	double LineTotal(const CartLine& line) const;
	double CartTotal(const std::vector<CartLine>& cart) const;
	// Why a line can't be bought (already owned, no room for another stand), or nothing.
	std::optional<std::string> CartLineProblem(const CartLine& line, const std::vector<CartLine>& cart) const;
	bool Checkout(const std::vector<CartLine>& cart);
	// Developer shortcut: fills every empty position on the active engine with a new OEM part,
	// fitted in build order, free of charge.
	void DevAssembleEngine();
	// Developer console: things for nothing. Parts, tools and machines are delivered like a purchase
	// but don't count as spending.
	void DevAddMoney(double amount);
	void DevGivePart(const PartOrder& order);
	bool DevGiveTool(size_t toolIndex);
	bool DevGiveMachine(size_t machineIndex);
	void DevGiveFurniture(size_t furnitureIndex);
	void DevGiveUpgrade(UpgradeKind upgrade);
	// Loading a save from before parts came in halves: adds the caps and lower bearing shells that
	// go with the parts it has. Returns how many were added.
	int AddMissingHalves();
	// Loading a save from before nuts and bolts were parts: a used set for each block.
	int AddMissingFasteners();
	// Loading a save from before cam caps were parts: each head gets its set, and an engine with its
	// cam in has them fitted and bolted down to spec, so it's no less built than it was.
	int AddMissingCamCaps();

	// Inventory
	const std::vector<pPart>& GetInventory() const;
	std::vector<pPart> GetInventory(PartType type) const;
	bool SellPart(const pPart& part);

	// Equipment
	bool OwnsTool(size_t toolIndex) const;
	bool OwnsMachine(size_t machineIndex) const;
	bool BuyTool(size_t toolIndex);
	bool BuyMachine(size_t machineIndex);
	// Furniture: any number of each, in the order bought (FurnitureCatalog indices).
	bool BuyFurniture(size_t furnitureIndex);
	const std::vector<size_t>& GetFurniture() const;
	// Shop upgrades: one of each, by UpgradeKind.
	bool OwnsUpgrade(UpgradeKind upgrade) const;
	bool BuyUpgrade(UpgradeKind upgrade);
	// The most precise owned tool for the job, or -1 if none is owned.
	int BestToolFor(const Feature& feature) const;
	int BestToolFor(AssemblyCheck check) const;
	int FinishTool() const;
	// Nuts and bolts go in finger tight; doing them up takes the tool in your hand. toolIndex is a
	// ratchet, breaker bar or torque wrench with a sizeMm socket on it, or the combination wrench
	// set at sizeMm. A torque wrench torques it to the hole's spec (to its own accuracy, and the
	// final angle needs a torque angle gauge); anything else only snugs it up by feel.
	bool TightenFastener(AssemblySlot slot, int index, size_t toolIndex, int sizeMm);
	// Back to finger tight, so it can come out. Not with a torque wrench.
	bool LoosenFastener(AssemblySlot slot, int index, size_t toolIndex, int sizeMm);
	// Why that tool can't turn the nut or bolt in that position on the active engine, if it can't.
	std::optional<std::string> TurningProblem(AssemblySlot slot, int index, size_t toolIndex, int sizeMm, bool tightening) const;

	// Bench work
	// toolIndex picks the tool used; -1 uses the most precise one owned.
	bool MeasureSize(Part& part, const std::string& featureName, int toolIndex = -1);
	bool MeasureFinish(Part& part, const std::string& featureName, int toolIndex = -1);
	MachiningResult MachineFeature(size_t machineIndex, Part& part, const std::string& featureName, double parameter);

	// Engines. The active engine is the one being worked on (at its stand or on the dyno); the
	// engine stand, assembly check and dyno calls below all work on it.
	int GetEngineCount() const;
	int GetActiveEngine() const;
	void SetActiveEngine(int engine);
	const EngineBuild& GetEngine(int engine) const;
	const EngineAssembly& GetAssembly(int engine) const;
	// Which engine the part is fitted to, or -1 when it's loose.
	int EngineOf(const Part& part) const;
	bool BuyEngineStand();

	// Engine stand (the active engine)
	EngineAssembly& GetAssembly();
	const EngineAssembly& GetAssembly() const;
	// Both follow the real build order (see EngineAssembly::Prerequisites), logging why not.
	bool InstallPart(AssemblySlot slot, int index, const pPart& part);
	bool RemovePart(AssemblySlot slot, int index);
	// index is the journal or cylinder; for INSTALLED_HEIGHT it is EngineAssembly::SpringIndex.
	// rod picks the rod bearing rather than the main for OIL_CLEARANCE.
	bool MeasureAssembly(AssemblyCheck check, int index, bool rod = false, int toolIndex = -1);
	// The same, for a gap settled earlier and read now: Plastigauge squashed under a cap that's
	// since come off again. trueValue is what it was squashed to.
	bool RecordAssemblyReading(AssemblyCheck check, int index, bool rod, int toolIndex, std::optional<double> trueValue);
	const AssemblyReading* GetAssemblyReading(AssemblyCheck check, int index, bool rod = false) const;
	static std::string AssemblyReadingLabel(AssemblyCheck check, int index, bool rod = false);

	// Dyno cell
	// A finished engine goes from its stand onto the dyno to be run, and afterwards back onto a free
	// stand (its own, or any empty one) to be worked on, or it's sold off the dyno. Only one engine
	// fits on the dyno; while it's there nothing on it can be fitted or taken off.
	int GetEngineOnDyno() const;
	bool MoveEngineToDyno(int engine);
	// A stand the engine on the dyno can go back on: its own, or one with no engine on it.
	bool IsStandFree(int stand) const;
	bool ReturnEngineFromDyno(int stand);
	const DynoResult& RunDyno();
	const DynoResult& GetLastDynoResult() const;
	bool IsDynoResultCurrent() const;
	double EstimateEngineValue() const;
	// Sells the engine on the stand, priced on its last dyno pull.
	bool SellEngine();
private:
	void Log(const std::string& message);
	bool Spend(double amount, const std::string& what);
	void AssemblyChanged();
	void AddCore();
	// Into the inventory, with whatever comes in the same box (see PartFactory::CreateCompanions).
	void AddPart(const pPart& part);
	EngineBuild& Active();
	const EngineBuild& Active() const;

	const EngineSpec*							m_spec = nullptr;
	double										m_money = 0.0;
	double										m_hours = 0.0;
	std::vector<std::string>					m_log = {};
	PlayerStats									m_stats = {};
	std::vector<pPart>							m_inventory = {};
	std::vector<bool>							m_ownedTools = {};
	std::vector<bool>							m_ownedMachines = {};
	std::vector<size_t>							m_furniture = {};
	std::vector<bool>							m_upgrades = {};
	std::vector<std::unique_ptr<EngineBuild>>	m_engines = {};
	int											m_activeEngine = 0;
	int											m_engineOnDyno = -1;	// which engine is on the dyno, or -1
};
