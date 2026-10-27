#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Dyno.h"
#include "EngineAssembly.h"
#include "Machine.h"
#include "MeasuringTool.h"
#include "PartFactory.h"

struct PartOrder
{
	PartType type = PartType::BLOCK;
	PartQuality quality = PartQuality::OEM;
	PartCondition condition = PartCondition::NEW;
	double sizeOffset = 0.0;
	int quantity = 1;
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

	// Supplier
	static std::vector<double> SizeOptions(PartType type);
	static bool SoldUsed(PartType type);
	double Quote(const PartOrder& order) const;
	bool Buy(const PartOrder& order);
	// A tired core to start another engine with: block, crank, head, cam, rods, valves and
	// springs, all used, as the shop started with. Sold as a lot, cheaper than piece by piece.
	double CorePrice() const;
	bool BuyCore();

	// Inventory
	const std::vector<pPart>& GetInventory() const;
	std::vector<pPart> GetInventory(PartType type) const;
	bool SellPart(const pPart& part);

	// Equipment
	bool OwnsTool(size_t toolIndex) const;
	bool OwnsMachine(size_t machineIndex) const;
	bool BuyTool(size_t toolIndex);
	bool BuyMachine(size_t machineIndex);
	// The most precise owned tool for the job, or -1 if none is owned.
	int BestToolFor(const Feature& feature) const;
	int BestToolFor(AssemblyCheck check) const;
	int FinishTool() const;

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
	const AssemblyReading* GetAssemblyReading(AssemblyCheck check, int index, bool rod = false) const;
	static std::string AssemblyReadingLabel(AssemblyCheck check, int index, bool rod = false);

	// Dyno cell
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
	EngineBuild& Active();
	const EngineBuild& Active() const;

	const EngineSpec*							m_spec = nullptr;
	double										m_money = 0.0;
	double										m_hours = 0.0;
	std::vector<std::string>					m_log = {};
	std::vector<pPart>							m_inventory = {};
	std::vector<bool>							m_ownedTools = {};
	std::vector<bool>							m_ownedMachines = {};
	std::vector<std::unique_ptr<EngineBuild>>	m_engines = {};
	int											m_activeEngine = 0;
};
