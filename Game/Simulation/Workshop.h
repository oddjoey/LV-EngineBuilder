#pragma once

#include <map>
#include <set>
#include <memory>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "CarAssembly.h"
#include "Dyno.h"
#include "EngineAssembly.h"
#include "RoadTest.h"
#include "Machine.h"
#include "MeasuringTool.h"
#include "PartFactory.h"
#include "ShopRank.h"
#include "Skills.h"
#include "Upgrades.h"

struct PartOrder
{
	PartType type = PartType::BLOCK;
	PartCondition condition = PartCondition::NEW;
	double sizeOffset = 0.0;
	int quantity = 1;
};

// One line in the parts computer's cart.
struct CartLine
{
	enum class Kind { PART, TOOL, MACHINE, ENGINE_STAND, FURNITURE, DYNO, HOIST, FLUID };
	Kind kind = Kind::PART;
	PartOrder order = {};		// PART: what, and how many (order.quantity)
	size_t index = 0;			// TOOL / MACHINE / FURNITURE: which one; FLUID: the FluidProduct
	int quantity = 1;			// ENGINE_STAND / FURNITURE / TOOL / FLUID: how many
};

// The project car: an LE86 shell on the drive, built up round an engine from one of the stands.
struct CarBuild
{
	explicit CarBuild(const CarSpec& spec) : assembly(spec) {}

	CarAssembly assembly;
	RoadTestResult lastRoadTest = {};
	RoadTestResult previousRoadTest = {};	// the drive before it, to compare with (not saved)
	bool roadTestCurrent = false;
	// How hot the engine in it is: 1 just after a road test, cooling to 0 (cold) over a while (not saved: a loaded game's
	// engine has cooled). The valve lash reads tighter cold than hot.
	double engineHeat = 0.0;
};

struct AssemblyReading
{
	double value = 0.0;
	bool stale = false; // parts have been swapped since this reading was taken
};

// One engine being built on its own stand: what's fitted to it, the clearances checked on it, and
// its last dyno pull.
// A ring set on its own in a bore of a block off the engine, to be gapped: the block, the bore, and whether it's been
// pushed down square with a piston yet.
struct BoreRing
{
	uint32_t block = 0;
	int cylinder = 0;
	bool squared = false;
};

// A valve, stem seal, spring or retainer in a cylinder head off the engine: the head, and its place in it.
struct HeadFitting
{
	uint32_t head = 0;
	AssemblySlot slot = AssemblySlot::COUNT;
	int index = 0;
};

struct EngineBuild
{
	explicit EngineBuild(const EngineSpec& spec) : assembly(spec) {}

	EngineAssembly assembly;
	std::map<std::string, AssemblyReading> readings = {};
	DynoResult lastDyno = {};
	bool dynoCurrent = false;
	// Cylinders whose rod and piston are in the top of the bore, not yet pushed home with the ring compressor.
	std::set<int> unseated = {};
	// Cylinders whose ring, sat on its own in the bore to be gapped, has been pushed down square with a piston.
	std::set<int> squaredRings = {};
};

// The last engine sold: the buyer's dyno pull of it, what they paid and the reputation it earned. Only
// known once it's sold.
struct EngineSale
{
	bool sold = false;
	int engine = 0;
	DynoResult pull = {};
	double price = 0.0;
	double reputation = 0.0;
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
	double reputation = 0.0;		// earned building engines: sets the shop's rank (ShopRank)
};

// The player's shop: money, inventory, owned equipment and the engines on the stands.
// Every game rule goes through here so the UI never mutates the simulation directly.
class Workshop
{
	friend class SaveGame;
	friend class NetState;
public:
	// Engines are built one per engine stand; more stands can be bought, up to this many.
	static constexpr int MAX_ENGINES = 4;
	static constexpr double ENGINE_STAND_PRICE = 250.0;

	Workshop(const EngineSpec& spec, double startingMoney);
	// What a new shop opens with: just enough to finish its first engine (the parts its core lacks, new, and a
	// dyno pull), and sell it on to pay for the next.
	static double StartingMoney(const EngineSpec& spec);
	~Workshop();

	const EngineSpec& GetSpec() const;
	double GetMoney() const;
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
	// Sales tax on an order, charged on top of its total at the checkout (the store's prices are before it).
	static constexpr double SALES_TAX = 0.08;
	double CartTax(const std::vector<CartLine>& cart) const;
	// Why a line can't be bought (already owned, no room for another stand), or nothing.
	std::optional<std::string> CartLineProblem(const CartLine& line, const std::vector<CartLine>& cart) const;
	bool Checkout(const std::vector<CartLine>& cart);
	// Developer shortcut: fills every empty position on the active engine with a new OEM part,
	// fitted in build order, free of charge.
	void DevAssembleEngine();
	// Takes everything off the active engine down to the head gasket, and the head with its valvetrain in it: the head.
	pPart DevPullHead();
	// Takes the parts in that slot off the active engine, and everything fitted over them first. How many came off.
	int DevStrip(AssemblySlot slot);
	// Developer console: things for nothing. Parts, tools and machines are delivered like a purchase
	// but don't count as spending.
	void DevAddMoney(double amount);
	void DevAddReputation(double amount);
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
	// Loading a save from before the 4A-C's hardware: a used flywheel, rocker shafts, distributor and
	// downpipe for each block, with their bolts.
	int AddMissing4ACParts();

	// Inventory
	const std::vector<pPart>& GetInventory() const;
	std::vector<pPart> GetInventory(PartType type) const;
	bool SellPart(const pPart& part);

	// Equipment
	// An id is one copy of a tool (ToolCatalog::IdOf): owned when the shop has at least that many.
	bool OwnsTool(size_t toolId) const;
	// How many of a tool the shop has.
	int ToolCount(size_t toolIndex) const;
	// The shop's rank (1 up), from its reputation; a tool above it isn't sold yet.
	int Rank() const;
	bool ToolUnlocked(size_t toolIndex) const;
	// Why the store won't sell it yet (the perk or rank it needs), or nothing if it will.
	std::optional<std::string> ToolLockedBecause(size_t toolIndex) const;

	// Skill points, the shop's to share: earned with reputation, spent on perks (see Skills).
	int SkillPointsEarned() const;
	int SkillPointsSpent() const;
	int SkillPoints() const;	// left to spend
	int SkillLevel(SkillKind skill) const;
	// Whether it can be bought: the root of its branch, or the perk above it is bought.
	bool SkillUnlocked(SkillKind skill) const;
	bool BuySkill(SkillKind skill);
	// What the perks bought do, as factors on the usual (1 without them).
	double TurnTimeFactor() const;
	double MeasureScatterFactor() const;
	double BatteryUseFactor() const;
	double SaleFactor() const;
	// The perks: what parts, and tools and machines, cost against the list price; what a loose part sells for;
	// how much more reputation the work earns; how much a machine's error is cut down.
	double PartPriceFactor() const;
	double EquipmentPriceFactor() const;
	double PartSalePrice(const Part& part) const;
	double ReputationFactor() const;
	double MachiningErrorFactor() const;
	double WalkSpeedFactor() const;
	double DynoCost() const;
	static constexpr int MAX_TOOL_COPIES = 8;
	bool OwnsMachine(size_t machineIndex) const;
	bool BuyTool(size_t toolIndex);
	bool BuyMachine(size_t machineIndex);
	// Furniture: any number of each, in the order bought (FurnitureCatalog indices).
	bool BuyFurniture(size_t furnitureIndex);
	const std::vector<size_t>& GetFurniture() const;
	// Shop upgrades: one of each, by UpgradeKind, built by buying its perk (UpgradePerk).
	bool OwnsUpgrade(UpgradeKind upgrade) const;
	static SkillKind UpgradePerk(UpgradeKind upgrade);
	// The dyno: a new shop hasn't one. Bought, it comes crated.
	bool OwnsDyno() const;
	bool BuyDyno();
	static double DynoPrice();
	// The engine hoist: bought like the dyno, and crated till it's opened.
	bool OwnsHoist() const;
	bool BuyHoist();
	static double HoistPrice();
	// The most precise owned tool for the job, or -1 if none is owned.
	int BestToolFor(const Feature& feature) const;
	int BestToolFor(AssemblyCheck check) const;
	int FinishTool() const;
	// Nuts and bolts go in finger tight; doing them up takes the tool in your hand. toolIndex is a
	// ratchet, breaker bar or torque wrench with a sizeMm socket on it, or the combination wrench
	// set at sizeMm. A torque wrench torques it to setNm, what it is set to (to its own accuracy; 0 is the hole's
	// spec, for the tests and helpers that do it by the book); anything else only snugs it up by feel.
	bool TightenFastener(AssemblySlot slot, int index, size_t toolIndex, int sizeMm, double setNm = 0.0);
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
	// Put together off the engine (EngineAssembly::GoesInWith), and in and off it together: the upper rod shell
	// fitted into a connecting rod and the lower shells into the rod and main caps, and a piston onto its rod,
	// held by a pin pressed in with the Piston Pin Press. A rod or cap goes on only with its shell in, and a rod
	// only with its piston pinned on. A pin stays in its piston on the engine too.
	pPart ShellIn(const Part& host) const;
	pPart PistonOn(const Part& rod) const;
	pPart PinIn(const Part& piston) const;
	// The shop's part with that id, if it has it.
	pPart FindPart(uint32_t id) const;
	// What a part is fitted into or onto (a shell's rod or cap, a piston's rod, a pin's piston), if anything.
	pPart HostOf(const Part& part) const;
	bool FitShell(const pPart& host, const pPart& shell);
	pPart TakeShellOut(const pPart& host);
	bool FitPiston(const pPart& rod, const pPart& piston);
	pPart TakePistonOff(const pPart& rod);
	// The piston on a loose rod: a pin from the shop's loose ones pressed in, or out again. The one pressed.
	pPart PressPinIn(const pPart& piston);
	pPart PressPinOut(const pPart& piston);
	// A piston's ring, fitted off the engine.
	pPart RingOn(const Part& piston) const;
	bool FitRing(const pPart& piston, const pPart& ring);
	pPart TakeRingOff(const pPart& piston);
	// A rod goes in with its piston only as far as the ring: it sits in the top of the bore until the ring
	// compressor squeezes the ring and it's pushed home (SeatPiston, on the active engine). Nothing goes on under
	// or over it till then: its cap, the oil pump, the head gasket.
	const std::set<int>& UnseatedPistons(int engine) const;
	bool SeatPiston(int cylinder);
	// A ring gapped on its own: set in the top of an open bore (the head off, no piston in it), pushed down square
	// with a piston, then the feeler gauge reads its gap. It sits in the cylinder's ring position with no piston
	// there. On an engine, the cylinders with one and whether it's been squared yet.
	std::map<int, bool> RingsInBore(int engine) const;
	std::optional<std::string> RingInBoreBlockedBy(int engine, int cylinder) const;
	bool SquareRing(int cylinder);
	// The same off the engine, in a block on the bench, a shelf or the floor: the ring rides in the block till it's taken out.
	pPart RingInLooseBore(const Part& block, int cylinder) const;
	const BoreRing* LooseBoreOf(const Part& ring) const;
	std::optional<std::string> LooseBoreBlockedBy(const Part& block, int cylinder) const;
	bool SetRingInLooseBore(const pPart& block, int cylinder, const pPart& ring);
	bool SquareLooseRing(const Part& ring);
	pPart TakeRingOutOfLooseBore(const Part& ring);
	bool MeasureLooseRingGap(Part& ring, int toolIndex);
	// The valvetrain goes into a cylinder head off the engine (EngineAssembly::InHead), position by position, each in
	// order (valve, stem seal, spring, retainer), and the head goes onto the engine only with every one in.
	pPart InLooseHead(const Part& head, AssemblySlot slot, int index) const;
	const HeadFitting* LooseHeadFittingOf(const Part& part) const;
	std::optional<std::string> HeadFitBlockedBy(const Part& head, AssemblySlot slot, int index) const;
	bool FitIntoHead(const pPart& head, AssemblySlot slot, int index, const pPart& part);
	// Why it can't come out of its head yet (what's over it), or nothing.
	std::optional<std::string> HeadTakeOutBlockedBy(const Part& part) const;
	pPart TakeOutOfHead(const Part& part);
	// What a place in a loose head (or on a loose rocker shaft: the cylinder it's over) is called.
	std::string LooseFittingName(AssemblySlot slot, int index) const;
	// The Valve Spring Compressor, over a valve in a loose head (EngineAssembly::SpringIndex), its spring squeezed down: a retainer
	// only goes on, or comes off, at the valve it's on, and nothing else at that valve moves while it's there. Not saved: put
	// away, it lets the spring back up.
	struct ValveCompressor { uint32_t head = 0; int index = 0; };
	const std::optional<ValveCompressor>& GetValveCompressor() const { return m_valveCompressor; }
	std::optional<std::string> CompressValveSpringBlockedBy(const Part& head, int index) const;
	bool CompressValveSpring(const pPart& head, int index);
	void ReleaseValveSpring();
	bool CompressedAt(const Part& head, int index) const { return m_valveCompressor && m_valveCompressor->head == head.GetId() && m_valveCompressor->index == index; }
	// A piston pin that isn't in a piston.
	pPart LoosePin() const;
	// Whether a shell of that type fits a part of the other.
	static bool ShellFits(PartType host, PartType shell);
	// index is the journal or cylinder; for INSTALLED_HEIGHT it is EngineAssembly::SpringIndex.
	// rod picks the rod bearing rather than the main for OIL_CLEARANCE.
	bool MeasureAssembly(AssemblyCheck check, int index, bool rod = false, int toolIndex = -1);
	// The same, for a gap settled earlier and read now: Plastigauge squashed under a cap that's
	// since come off again. trueValue is what it was squashed to.
	bool RecordAssemblyReading(AssemblyCheck check, int index, bool rod, int toolIndex, std::optional<double> trueValue);
	const AssemblyReading* GetAssemblyReading(AssemblyCheck check, int index, bool rod = false) const;
	static std::string AssemblyReadingLabel(AssemblyCheck check, int index, bool rod = false);

	// Dyno cell
	// Once the shop has one, a finished engine goes from its stand onto the dyno to be run, and afterwards
	// back onto a free stand (its own, or any empty one) to be worked on, or it's sold off the dyno. Only one engine
	// fits on the dyno; while it's there nothing on it can be fitted or taken off.
	int GetEngineOnDyno() const;
	bool MoveEngineToDyno(int engine);
	// A stand the engine on the dyno can go back on: its own, or one with no engine on it.
	bool IsStandFree(int stand) const;
	bool ReturnEngineFromDyno(int stand);
	const DynoResult& RunDyno();
	const DynoResult& GetLastDynoResult() const;
	bool IsDynoResultCurrent() const;

	// Engine hoist
	// It lifts an engine off its stand (or the dyno), to hang from its hook while it's pushed about, and lowers
	// it onto a free stand or the dyno. Like on the dyno, nothing is fitted to or taken off a hanging engine.
	int GetEngineOnHoist() const;
	bool HoistEngine(int engine);
	// A stand the hanging engine can go on: its own, or one with no engine on it.
	bool IsStandFreeForHoist(int stand) const;
	bool LowerEngineOntoStand(int stand);
	bool LowerEngineOntoDyno();
	// Sells the active engine, finished, off its stand or the dyno. The buyer runs it first and pays for what
	// it makes and how healthy it is; only then are its figures known (GetLastSale).
	bool SellEngine();
	const EngineSale& GetLastSale() const;

	// The LE86 (WorkshopCar.cpp): the shop's from the start, its LV-16 in it, both worn (see SetUpStartingCar). It's kept,
	// not sold: worked on and driven.
	bool OwnsCar() const;
	const CarBuild* GetCar() const;
	// Which part a car part is on, off the car: the spring compressed onto a front strut, the tire on its
	// wheel. And the other way: what a spring or tire is on.
	pPart CarriedOn(const Part& host) const;
	pPart CarHostOf(const Part& part) const;
	bool IsOnCar(const Part& part) const;
	// The spring onto its strut (with the spring compressor), the tire onto its wheel; and off again.
	bool FitOntoCarPart(const pPart& host, const pPart& part);
	pPart TakeOffCarPart(const pPart& host);
	// Following the car's build order (CarAssembly::Prerequisites), logging why not.
	bool InstallCarPart(CarSlot slot, int index, const pPart& part);
	bool RemoveCarPart(CarSlot slot, int index);
	std::optional<std::string> CarTurningProblem(CarSlot slot, int index, size_t toolIndex, int sizeMm, bool tightening) const;
	bool TightenCarFastener(CarSlot slot, int index, size_t toolIndex, int sizeMm, double setNm = 0.0);
	bool LoosenCarFastener(CarSlot slot, int index, size_t toolIndex, int sizeMm);
	// The best tool the shop has to do up (or undo) a nut or bolt of this size to this torque, and the socket
	// or wrench size on it: a torque wrench if one reaches, else a breaker bar, ratchet or wrench.
	std::optional<std::pair<size_t, int>> BestTurningTool(PartType fastener, double torqueNm, bool tightening) const;
	// The engine in the car's bay: lowered in off the hoist and lifted out onto it again. While it's in, its
	// stand stays empty for it and nothing on it can be changed.
	int GetEngineInCar() const;
	// What can be got at with the engine in the car, and taken off or fitted there: the valve cover (for the lash), the
	// plugs and leads, the carburettor and the oil filter, with their gaskets and bolts.
	static bool ServiceableInCar(AssemblySlot slot);
	// Why that position on that engine can't be worked on where the engine is, or nothing: not on the dyno; on the hoist only
	// the flywheel (and its bolts); in the car, the service parts, and the flywheel once the clutch and gearbox are off; and on
	// a stand, anything but the flywheel, as the stand's plate is bolted over the back of the block.
	std::optional<std::string> EngineWorkProblem(int engine, AssemblySlot slot) const;
	static bool AtFlywheel(AssemblySlot slot) { return slot == AssemblySlot::FLYWHEEL || slot == AssemblySlot::FLYWHEEL_BOLT; }
	bool LowerEngineIntoCar();
	bool HoistEngineFromCar();
	// Fluids are bought by the bottle and kept in the shop's stock, by the litre, till they're poured.
	static double FluidUnitLitres(FluidProduct product);
	static double FluidUnitPrice(FluidProduct product);
	double FluidStock(FluidProduct product) const;
	// What went in: what's in stock, up to what it takes; poured into a circuit that's open, it runs out on the floor.
	// quiet: poured a little at a time (held at the filler), so it isn't logged each time.
	double PourIntoCar(FluidCircuit circuit, FluidProduct product, double litres, bool quiet = false);
	double DrainCar(FluidCircuit circuit);
	// Whether the shop owns a tool of that kind (the spring compressor, the tire irons...).
	bool OwnsToolOfKind(HandToolKind kind) const;
	// Whether the car's engine fires and keeps running (to burp the coolant, or strobe the timing).
	bool CarStarts() const;
	// One go at a bleed point. The air left in that line, which the player only sees as how many bubbles come out.
	double BleedCar(BleedPoint point);
	// Runs the engine with the radiator cap off till the thermostat opens, to let the air out.
	bool BurpCarCoolant();
	void SetCarTune(const CarTune& tune);
	// A drive round the block and down the strip, the engine as it is in the car.
	const RoadTestResult& RoadTestCar();
	// Getting in and turning the key, to drive it about the place. What it does there comes from the same sums as the
	// road test (which isn't run, or logged): how hard it pulls, how fast it'll go, how it stops and holds the road.
	struct CarDrive
	{
		bool goes = false;				// it starts, moves, steers and stops
		std::string problem = "";		// why not, when it doesn't
		double topSpeed = 0.0;			// km/h
		double zeroToHundred = 0.0;		// s (0: it never gets there)
		double brakingDistance = 0.0;	// m, 100 to 0 km/h
		double lateralGrip = 0.0;		// g
		double idleRpm = 0.0;
	};
	CarDrive StartCar();
	// Driven for this long, this far: fuel burned, the oil and coolant aged, the engine warmed. False once the tank is dry.
	bool DriveCar(double seconds, double metres, double throttle);
	// The engine in the car cooling for this many seconds (a road test warms it right up).
	void CoolCarEngine(double seconds);
	double CarEngineHeat() const { return m_car ? m_car->engineHeat : 0.0; }
	// How much tighter the valve lash reads cold than hot.
	static constexpr double LASH_HOT_GROWTH = 0.05;
	static constexpr double CAR_ENGINE_COOL_SECONDS = 20.0 * 60.0;
	// Developer shortcut: fits new OEM parts everywhere they'll go, the active engine in the bay if it's
	// finished and free, and fills and bleeds everything.
	void DevAssembleCar();
	// Developer shortcut: takes every one of a kind of part off the car, and its nuts and bolts, whatever is in the way.
	int DevStripCar(PartType type);
	// The car, what's fitted to its loose parts and the fluids in stock, as one line of save text (for the save
	// and the network); and back, once the inventory holds the parts.
	std::string CarState() const;
	bool RestoreCarState(const std::string& text);
private:
	void CarChanged();
	// Why a tool can't turn this nut or bolt, the engine's or the car's.
	std::optional<std::string> TurningProblemFor(const FastenerHoles* holes, const pPart& part, size_t toolIndex, int sizeMm, bool tightening) const;
	// Does it up with the tool, as tight as that tool gets it.
	void DoUp(const FastenerHoles& holes, Part& part, size_t toolIndex, double setNm = 0.0);
	void Log(const std::string& message);
	// What's fitted to a part in that slot's place (its shell, piston or ring), and where those are kept.
	pPart FittedIn(const Part& host, AssemblySlot slot) const;
	std::map<uint32_t, uint32_t>& FittingsFor(AssemblySlot slot);
	// Reputation earned; each rank reached on the way pays its bonus and says what it opens up.
	void EarnReputation(double amount);
	bool Spend(double amount, const std::string& what);
	void AssemblyChanged();
	// Every empty position on the active engine filled with a new OEM part, in build order, done up to spec. How many.
	int FitNewEngineParts();
	// The same for the car: new OEM parts wherever they'll go, the active engine into its bay once it's finished. How many.
	int FitNewCarParts();
	// The car full of the right fluids, bled and burped.
	void FillAndBleedCar();
	// The shop's start (WorkshopCar.cpp): the LE86, complete and running, its LV-16 in it, both worn.
	void SetUpStartingCar();
	// The faults the engine starts with, added one at a time till the dyno makes it about health (0 to 100) healthy.
	void WearStartingEngine(int engine, double health);
	// And the car's: perished tires, a split engine mount, worn-through front pads and oil that's overdue.
	void WearStartingCar();
	// Into the inventory, with whatever comes in the same box (see PartFactory::CreateCompanions).
	void AddPart(const pPart& part);
	EngineBuild& Active();
	const EngineBuild& Active() const;

	const EngineSpec*							m_spec = nullptr;
	double										m_money = 0.0;
	std::vector<std::string>					m_log = {};
	PlayerStats									m_stats = {};
	std::vector<pPart>							m_inventory = {};
	std::vector<int>							m_toolCounts = {};	// how many of each tool
	std::array<int, Skills::COUNT>				m_skills = {};	// each perk's level
	std::vector<bool>							m_ownedMachines = {};
	std::vector<size_t>							m_furniture = {};
	std::vector<bool>							m_upgrades = {};	// paid for, in saves from before they were perks
	bool										m_ownsDyno = false;
	bool										m_ownsHoist = false;
	std::vector<std::unique_ptr<EngineBuild>>	m_engines = {};
	int											m_activeEngine = 0;
	int											m_engineOnDyno = -1;	// which engine is on the dyno, or -1
	int											m_engineOnHoist = -1;	// which engine hangs from the hoist, or -1
	std::map<uint32_t, uint32_t>				m_shells = {};	// a loose rod or cap's id: the shell fitted in it
	std::map<uint32_t, uint32_t>				m_pistons = {};	// a loose rod's id: the piston on it
	std::map<uint32_t, uint32_t>				m_pins = {};	// a piston's id: the pin pressed into it (on the engine too)
	std::map<uint32_t, uint32_t>				m_rings = {};	// a loose piston's id: the ring on it
	std::map<uint32_t, BoreRing>				m_boreRings = {};	// a ring's id: the loose block bore it's set in to be gapped
	std::map<uint32_t, HeadFitting>			m_headFittings = {};	// a valve, seal, spring or retainer's id: where it is in a loose head
	std::optional<ValveCompressor>			m_valveCompressor = std::nullopt;
	EngineSale									m_lastSale = {};
	std::unique_ptr<CarBuild>					m_car = nullptr;
	std::map<uint32_t, uint32_t>				m_carCarried = {};	// a loose strut's or wheel's id: the spring or tire on it
	std::array<double, FLUID_PRODUCTS>			m_fluidStock = {};	// litres of each
};
