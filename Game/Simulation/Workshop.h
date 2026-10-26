#pragma once

#include <map>
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

// The player's shop: money, the clock, inventory, owned equipment and the engine on the stand.
// Every game rule goes through here so the UI never mutates the simulation directly.
class Workshop
{
	friend class SaveGame;
public:
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

	// Engine stand
	EngineAssembly& GetAssembly();
	const EngineAssembly& GetAssembly() const;
	bool InstallPart(AssemblySlot slot, int index, const pPart& part);
	void RemovePart(AssemblySlot slot, int index);
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

	const EngineSpec*							m_spec = nullptr;
	double										m_money = 0.0;
	double										m_hours = 0.0;
	std::vector<std::string>					m_log = {};
	std::vector<pPart>							m_inventory = {};
	std::vector<bool>							m_ownedTools = {};
	std::vector<bool>							m_ownedMachines = {};
	EngineAssembly								m_assembly;
	std::map<std::string, AssemblyReading>		m_assemblyReadings = {};
	DynoResult									m_lastDyno = {};
	bool										m_dynoCurrent = false;
};
