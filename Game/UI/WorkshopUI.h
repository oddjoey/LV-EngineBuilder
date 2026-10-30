#pragma once

#include <functional>

#include <imgui.h>
#include <optional>
#include <string>

#include "../Simulation/Units.h"
#include "../Simulation/Workshop.h"

// Which screen the workshop window shows; only one at a time. BACKPACK is the inventory (Tab), the
// rest open from their station. STORE isn't one: it opens the parts computer's own window (see
// OpenStore).
enum class WorkshopTab { BACKPACK, BENCH, MACHINE, ENGINE_STAND, DYNO, STORE };

// A part the Bench tab can work on, and where it's sitting.
struct BenchItem
{
	pPart part = nullptr;
	std::string location = "";
	int machineIndex = -1;
};
enum class StoreCategory { PARTS, HARDWARE, TOOLS, MACHINES, FURNITURE, UPGRADES };

// ImGui front end for the workshop. It only ever shows the player what they've measured;
// the true geometry is visible only with the X-ray developer toggle.
class WorkshopUI
{
public:
	WorkshopUI();
	~WorkshopUI();

	void Draw(Workshop& workshop);

	void Open(WorkshopTab tab);
	// The parts computer: a window of its own, separate from the workshop screens.
	void OpenStore(StoreCategory category);
	// The tab for one machine: the part set up in it and only what that machine can do to it.
	void OpenMachine(int machineIndex);
	void Close();
	bool IsOpen() const;
	// Puts a part on the bench view; with a machine, also picks the first feature that machine can cut.
	void SelectPart(const Part& part, int machineIndex = -1);

	// Advances the dyno pull animation, which keeps running while the window is closed.
	void Update(float deltaTime);
	// Engine speed of the pull in progress (or coasting down after it), 0 when idle.
	float GetLiveRpm() const;
	// The engine on the dyno for that pull.
	int GetPullEngine() const;
	// Lets the world say a part can't be used yet (it's still in its delivery box).
	void SetPartAvailability(const std::function<bool(uint32_t)>& isUsable);

	// The Backpack tab's contents, drawn by whoever owns the inventory.
	void SetBackpackContents(const std::function<void()>& drawContents);
	// What's on the workbench and in the machines, which is all the Bench tab shows.
	void SetBenchContents(const std::function<std::vector<BenchItem>()>& benchContents);

	void SetUnits(Units::System units);
	Units::System GetUnits() const;
	// How the store finds an item's 3D model by name, for its turntable preview.
	void SetModelLookup(const std::function<uint32_t(const std::string&)>& lookup);
	// Developer x-ray: the parts' true geometry next to the player's readings.
	void SetXray(bool xray);
private:
	static constexpr float PULL_SECONDS = 6.f;

	static std::string FeatureSection(PartType type, const std::string& featureName);
	bool IsUsable(const Part& part) const;

	void DrawStatusBar(Workshop& workshop);
	void DrawInventoryTab(Workshop& workshop);
	void DrawMachineTab(Workshop& workshop);
	void DrawPartInspector(Workshop& workshop, const pPart& part);
	void DrawFeatureDetail(Workshop& workshop, Part& part, const Feature& feature);
	void DrawMachiningPanel(Workshop& workshop, Part& part, const Feature& feature);
	// What to do about a feature that's out of spec, worked out from the player's own readings:
	// which operation and machine, what size to cut to and what to pair it with, or replace it.
	void DrawFixAdvice(Workshop& workshop, const Part& part, const Feature& feature);
	void DrawStoreTab(Workshop& workshop);
	// The Parts tab, or with hardware the Hardware tab: nuts and bolts by size.
	void DrawPartsStore(Workshop& workshop, bool hardware);
	void DrawToolStore(Workshop& workshop);
	void DrawMachineStore(Workshop& workshop);
	void DrawFurnitureStore(Workshop& workshop);
	void DrawUpgradeStore(Workshop& workshop);
	// Engine stands and whole cores, for building more engines at once.
	void DrawStoreWindow(Workshop& workshop);
	// The cart beside the catalogue: every line, its price, the total and the checkout button.
	void DrawCart(Workshop& workshop);
	// Adds a line, merging it into one already in the cart for the same thing.
	void AddToCart(const CartLine& line);
	// How many of a tool or machine (or stands) are in the cart already.
	int InCart(CartLine::Kind kind, size_t index = 0) const;
	// An "Add to cart" button, greyed out with the reason when the line can't be bought.
	void AddToCartButton(Workshop& workshop, const CartLine& line, const char* label);
	std::string CartLineName(const CartLine& line) const;
	// A slowly turning 3D view of an item, by its model's name.
	void DrawPreview(const std::string& modelName);
	// Which engine the Engine Stand and Dyno tabs show, when there's more than one.
	void DrawEnginePicker(Workshop& workshop);
	// One clickable catalogue card; draws its icon with drawIcon(drawList, min, size).
	bool StoreCard(const char* id, const char* name, const std::string& price, bool selected, bool owned,
		const std::function<void(ImDrawList*, ImVec2, float)>& drawIcon, const std::vector<float>& ratings = {});
	std::string ToolPrecision(const MeasuringTool& tool, double value) const;
	// Which tool to go and get for a measurement, for the hints that replaced the measure buttons.
	std::string ToolHint(Workshop& workshop, int toolIndex) const;
	void DrawAssemblyTab(Workshop& workshop);
	// The parts fitted to the engine, by position; picks the one the stand screen inspects.
	pPart DrawAssemblySlots(Workshop& workshop);
	void DrawAssemblyChecks(Workshop& workshop);
	void DrawDynoTab(Workshop& workshop);
	void DrawLog(Workshop& workshop);

	std::string Size(double mm) const;
	std::string Deviation(double mm) const;
	std::string FeatureValue(const Feature& feature, double value) const;
	std::string SpecText(const Feature& feature) const;
	std::string RangeText(const Range& range) const;

	// The player's own number for a feature, if they've measured it since it was last cut.
	static std::optional<double> MeasuredSize(const pPart& part, const std::string& featureName);
	static Feature FromReading(const Feature& feature, const FeatureReading& reading);

	bool						m_open = false;
	bool						m_storeOpen = false;
	std::optional<WorkshopTab>	m_requestedTab = std::nullopt;
	std::optional<StoreCategory>	m_requestedCategory = std::nullopt;
	int							m_selectedTool = 0;
	int							m_selectedStoreMachine = 0;
	int							m_selectedStoreFurniture = 0;
	int							m_selectedStoreUpgrade = 0;
	WorkshopTab					m_currentTab = WorkshopTab::BENCH;
	Units::System		m_units = Units::System::METRIC;
	bool				m_xray = false;
	uint32_t			m_selectedPartId = 0;
	uint32_t			m_standPartId = 0;		// the fitted part the stand screen shows (0: the engine checks)
	std::string			m_selectedFeature = "";
	bool				m_scrollToDetail = false;	// a feature was just opened: bring its details into view
	int					m_selectedMachine = -1;
	int					m_activeMachine = -1;		// the machine whose tab is showing, or -1 for none
	int					m_selectedPartMachine = -1;	// machine the selected part is set up in, or -1 on the bench
	double				m_machineParameter = 0.0;	// in display units
	std::string			m_lastMachiningMessage = "";
	PartOrder			m_order = {};
	std::vector<CartLine>	m_cart = {};
	std::string			m_cartMessage = "";
	size_t				m_lastLogSize = 0;

	std::function<bool(uint32_t)>	m_isUsable = nullptr;
	std::function<uint32_t(const std::string&)>	m_modelLookup = nullptr;
	std::function<void()>			m_backpackContents = nullptr;
	std::function<std::vector<BenchItem>()>	m_benchContents = nullptr;
	bool				m_pullActive = false;
	float				m_pullTime = 0.f;
	float				m_spinDownTime = 0.f;
	float				m_pullStartRpm = 0.f;
	float				m_pullEndRpm = 0.f;
	bool				m_pullFailed = false;
	int					m_pullEngine = -1;
};
