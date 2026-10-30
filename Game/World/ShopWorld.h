#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "LV/Engine/Objects/Object.h"
#include "../Audio/Sounds.h"
#include "../Inventory/PlayerInventory.h"
#include "../Simulation/Units.h"
#include "../Simulation/Workshop.h"
#include "../UI/WorkshopUI.h"
#include "FirstPersonController.h"
#include "GarageLayout.h"
#include "ShopAssets.h"

class InputManager;

enum class StationType
{
	WORKBENCH,
	ENGINE_STAND,
	MACHINE,
	DYNO,
	COMPUTER,
	TOOL_BOARD,
	SHELF,
	PALLET_RACK,
	DOOR,		// the roll-up door: opens and closes, never moves
	FURNITURE,	// bought shelving, benches, seats and decorations (see FurnitureCatalog)
	LIFT,		// the two-post lift upgrade: raised and lowered with E
	BED			// in the house: sleep till morning
};

// Bounding box colours for the developer view (ImGui's packed ABGR).
constexpr uint32_t DebugColor(uint32_t r, uint32_t g, uint32_t b) { return 0xFF000000u | (b << 16) | (g << 8) | r; }
constexpr uint32_t DEBUG_WALL_COLOR = DebugColor(255, 70, 60);			// walls the player and items can't pass
constexpr uint32_t DEBUG_OBSTACLE_COLOR = DebugColor(230, 80, 230);		// what blocks walking
constexpr uint32_t DEBUG_SURFACE_COLOR = DebugColor(90, 220, 110);		// tops things can be set down on
constexpr uint32_t DEBUG_STATION_COLOR = DebugColor(80, 200, 255);		// equipment
constexpr uint32_t DEBUG_PACKAGE_COLOR = DebugColor(255, 150, 50);		// deliveries
constexpr uint32_t DEBUG_ITEM_COLOR = DebugColor(255, 230, 80);			// parts and tools

struct ShopBox
{
	glm::vec3 min = {};
	glm::vec3 max = {};
};

struct Station
{
	StationType type = StationType::WORKBENCH;
	std::string name = "";
	pObject object = nullptr;
	int machineIndex = -1;
	int engineIndex = -1;					// engine stands: which engine (Workshop's numbering) it holds
	int furnitureIndex = -1;				// furniture: which piece (FurnitureCatalog index)
	glm::vec3 position = {};
	float yaw = 0.f;
	glm::vec3 localAnchor = {};				// where a part set up here rests, in the station's space
	float partYaw = 0.f;
	std::vector<ShopBox> localSurfaces = {};	// tops things can be set down on, in the station's space
	bool movable = false;
	bool present = true;					// unbought machines and stands aren't in the shop
	glm::vec3 homePosition = {};
	float homeYaw = 0.f;

	glm::quat Rotation() const;
	glm::vec3 Anchor() const;
};

// Where an item physically is. The simulation doesn't care; this is purely the 3D world.
enum class ItemPlace
{
	STORAGE,	// tools hung on the tool board (a part sent here is set down loose on the shelf instead)
	BENCH,		// on the workbench, being inspected
	MACHINE,	// set up in a machine
	HAND,		// held: the equipped item
	STAND,		// fitted to the engine
	WORLD,		// put down or dropped somewhere, under physics
	PACKAGED,	// still in its delivery box
	INVENTORY	// in the backpack and not in hand
};

// Just enough physics for things to fall, land and stack: no tumbling, no friction model.
struct Body
{
	glm::vec3 velocity = {};
	bool resting = true;
};

struct ItemVisual
{
	ItemRef ref = {};
	pPart part = nullptr;		// parts only
	pObject object = nullptr;
	ItemPlace place = ItemPlace::STORAGE;
	int machineIndex = -1;
	int engineIndex = -1;		// fitted to this engine (place STAND)
	Body body = {};
	float yaw = 0.f;
	std::vector<std::pair<SurfaceOverlay, pObject>> overlays = {};
	// Tools: the sockets of a socket set, or the socket on a ratchet, breaker bar or torque wrench.
	std::vector<pObject> sockets = {};
	// Fitted to an engine: the position it was last laid out in, so it's only re-posed when that or the
	// engine moves.
	std::pair<AssemblySlot, int> standSlot = { AssemblySlot::COUNT, -1 };
};

// A delivery: parts and tools come out where it's opened; a machine or engine stand inside it is
// set up in the shop.
struct Package
{
	uint32_t id = 0;
	pObject object = nullptr;
	std::vector<ItemRef> items = {};
	std::vector<size_t> stations = {};	// m_stations indices of machines and stands still crated
	std::string label = "";
	Body body = {};
};

// Asks the game to open the workshop UI at a tab, optionally focused on a part and machine.
struct UIRequest
{
	WorkshopTab tab = WorkshopTab::BENCH;
	uint32_t partId = 0;
	int machineIndex = -1;
	std::optional<StoreCategory> storeCategory = std::nullopt;
};

// What the backpack and hotbar need to show an item.
struct ItemInfo
{
	std::string name = "";
	std::string description = "";
	glm::ivec2 size = {};
	bool isTool = false;
	PartType partType = PartType::BLOCK;
	int toolIndex = -1;
	const Part* part = nullptr;		// parts only
};

// The walkable shop: stations, every part and tool as a 3D item, delivery packages, simple
// physics, the animated engine, the backpack and hotbar, measuring by aiming a tool, and
// moving equipment around.
class ShopWorld
{
	friend class SaveGame;
public:
	// Creates the static scene. Call from Game::OnLoadAssets after the assets are loaded.
	void Build(const ShopAssets& assets);

	glm::vec3 GetSpawnPosition() const;
	float GetSpawnYaw() const;
	const std::vector<FloorObstacle>& GetObstacles() const;
	FloorObstacle GetRoomBounds() const;

	std::optional<UIRequest> Update(float deltaTime, Workshop& workshop, const FirstPersonController& player,
		const InputManager& input, bool playerHasControl);
	void DrawHud(const Workshop& workshop, const FirstPersonController& player, bool showControls) const;
	// Developer view: wireframes of everything that collides or can be picked up, colour coded.
	void DrawDebugBounds(const FirstPersonController& player) const;

	bool IsPackaged(uint32_t partId) const;
	// Deliveries sitting unopened in the shop.
	int GetUnopenedPackages() const;
	// The dyno pull in progress, if any, and which engine is on it.
	void SetEngineRpm(float rpm, int engine);
	void SetUnits(Units::System units);
	void SetFieldOfView(float degrees);

	// Sounds: what has happened since the last call that makes a noise, and where. And the things that
	// hum or rumble while they run: whether they're going and where they are.
	std::vector<SoundCue> TakeSoundCues();
	// The player got into bed this frame: the game decides whether it's late enough to sleep.
	bool TakeSleepRequest();
	// The player opened the service manual in their hand this frame.
	bool TakeReadRequest();
	// A message along the bottom of the screen, like the shop's own.
	void Notify(const std::string& message);
	// What a footstep at feet sounds like: the shop's concrete, upstairs boards, the drive and street, grass.
	Sound FootstepAt(const glm::vec3& feet) const;
	bool IsDoorMoving() const;
	glm::vec3 GetDoorPosition() const;
	bool IsLiftMoving() const;
	glm::vec3 GetLiftPosition() const;
	float GetEngineRpm() const;
	glm::vec3 GetDynoPosition() const;

	// Hotbar, backpack and hands. What's in hand is whatever is in the selected hotbar slot.
	PlayerInventory& GetInventory();
	ItemInfo DescribeItem(const ItemRef& item, const Workshop& workshop) const;
	ItemRef GetEquipped() const;
	// Takes the item in hand: selects its hotbar slot, or brings it out of the backpack into the
	// selected slot (what was there goes in the backpack). If it's in hand already, it goes in the
	// backpack.
	void ToggleEquip(const ItemRef& item);
	// Puts an item in a hotbar slot. What was there goes where the item came from: its old slot, or
	// the backpack if it fits there. False (with a message) if it can't be done.
	bool MoveToHotbar(const ItemRef& item, int slot);
	// Puts an item from the hotbar in the backpack. False (with a message) if there's no room.
	bool MoveToBackpack(const ItemRef& item);
	void SelectHotbarSlot(int slot);
	// The selected slot's item was put away with its number: nothing in hand and no slot picked.
	bool IsHandEmptied() const { return m_handEmpty; }
	// Puts the selected slot's item in hand, and anything else that was there away: call after
	// changing the hotbar directly.
	void SyncHand();
	// Drops an item from the backpack onto the floor in front of the player.
	void DropFromInventory(const ItemRef& item, const FirstPersonController& player);

	// Parts on the workbench, then parts set up in machines: what the Bench tab works on.
	std::vector<BenchItem> GetBenchItems() const;

	// Moving equipment
	bool IsMovingStation() const;
	void CancelMove();

	// The roll-up door and the lot outside
	void SetDoorOpen(bool open, bool instant = false);
	// 0 shut to 1 fully rolled up.
	float GetDoorOpenAmount() const;
	// Street and porch lamps, so the lighting can make them glow at night.
	const std::vector<pObject>& GetLampHeads() const;
	// Stairs, landing and the upstairs floor, once there's a second story.
	const std::vector<WalkSurface>& GetWalkSurfaces() const;
	// The inside of the shop, side bay and second story included, and its lamps.
	ShopInterior GetInterior() const;
private:
	enum class TargetKind { ITEM, STATION, PACKAGE };
	struct Target
	{
		TargetKind kind = TargetKind::STATION;
		ItemRef item = {};
		uint32_t packageId = 0;
		size_t stationIndex = 0;
	};
	// Plastigauge, the way it's really done: a strip laid across the bare journal, squashed when the
	// cap (and its lower shell) is torqued down over it, and read against the card once the cap is off.
	enum class StripStep { NONE, LAY, READ };
	struct PlastigaugeStrip
	{
		int engine = 0;
		AssemblyCheck check = AssemblyCheck::OIL_CLEARANCE;	// or CAM_CLEARANCE
		bool rod = false;
		int index = 0;				// journal (or cylinder, for a rod)
		bool squashed = false;
		double clearance = 0.0;		// the true gap it was squashed to, read later
		float crankAngle = 0.f;		// where the crank was when it went on: turning it smears the strip
	};
	struct MeasureTarget
	{
		bool assembly = false;
		ItemRef part = {};
		std::string feature = "";
		bool finish = false;
		AssemblyCheck check = AssemblyCheck::NONE;
		int index = 0;
		bool rod = false;
		int engine = 0;
		StripStep strip = StripStep::NONE;	// oil clearance: which Plastigauge step this is
		glm::vec3 position = {};
		std::string label = "";
	};
	struct Readout
	{
		std::string title = "";
		std::vector<std::string> lines = {};
		bool good = true;
		float time = 0.f;
	};
	struct MoveState
	{
		size_t stationIndex = 0;
		glm::vec3 originalPosition = {};
		float originalYaw = 0.f;
		float yaw = 0.f;
		bool valid = false;
		// Loose parts, tools and deliveries sitting on the station, carried along with it.
		struct Rider
		{
			ItemRef item = {};
			uint32_t packageId = 0;
			glm::vec3 localPosition = {};
			glm::quat localRotation = glm::quat(1.f, 0.f, 0.f, 0.f);
			float yaw = 0.f;	// the item's own yaw when the move started
		};
		std::vector<Rider> riders = {};
	};

	// Scene
	pObject Spawn(const std::string& model, const std::string& texture, const glm::vec3& position, float yaw = 0.f);
	size_t AddStation(StationType type, const std::string& name, const pObject& object, bool movable,
		const glm::vec3& localAnchor = {}, float partYaw = 0.f, int machineIndex = -1);
	void RebuildCollision();
	static ShopBox WorldBounds(const pObject& object);
	static ShopBox TransformBox(const ShopBox& box, const glm::vec3& position, const glm::quat& rotation);
	const Station* FindStation(StationType type) const;
	// The stand holding this engine.
	const Station* FindStand(int engine) const;
	// A parts shelf (adds its boards as surfaces).
	size_t AddShelf(const std::string& name, const glm::vec3& position);
	// A machine's station, hidden until it's bought.
	size_t AddMachineStation(size_t machineIndex, const glm::vec3& home);
	// Furniture: a station for each piece the workshop has bought, after the shop's own (the first
	// m_fixedStations), crated like a machine until it's opened.
	size_t AddFurnitureStation(size_t furnitureIndex);
	void SyncFurniture(const Workshop& workshop);
	// Takes every piece of furniture out of the shop, for loading another save.
	void RemoveFurniture();
	// Which shelf a part is kept on: internals on the first, bolt-ons on the second.
	const Station* ShelfFor(const Part& part) const;

	// Outdoors (ShopWorldOutdoors.cpp)
	void BuildOutdoors();
	// Shop upgrades: the side bay and the second story are built (or taken away again, loading a save
	// without them) to match the workshop. The lift is a station like a machine.
	void SyncUpgrades(const Workshop& workshop);
	void BuildUpgrades(bool bay, bool upper);
	// Whether equipment with this footprint stands on the shop floor (side bay included).
	bool OnShopFloor(const ShopBox& footprint) const;
	void UpdateLift(float deltaTime);
	void UpdateDoor(float deltaTime);
	// Whether a door station is the extension's roll-up door rather than the shop's, and which way it's going.
	bool IsBayDoor(const Station& station) const;
	bool& DoorOpenFor(const Station& station);
	// Garage walls (and the door while it's down) as solid boxes.
	std::vector<ShopBox> SolidWalls() const;
	std::optional<float> WallHit(const glm::vec3& origin, const glm::vec3& direction) const;
	bool InsideWalls(const ShopBox& box) const;

	// Sync and layout
	void SyncItems(Workshop& workshop);
	ItemVisual& CreateItem(const ItemRef& ref, const pPart& part, uint32_t modelIndex, uint32_t textureIndex);
	void RemoveItem(const ItemRef& ref);
	// Bought machines and engine stands appear in the shop.
	// Which parts are fitted to which engine (they can be fitted or taken off from the UI too).
	void SyncStands(const Workshop& workshop);
	void SyncMachines(const Workshop& workshop);
	// Dropped on the pile by the roll-up door.
	// title, if given, heads the label ("Timing drive: ...").
	void CreatePackage(const std::vector<ItemRef>& items, const std::vector<size_t>& stations, const std::string& title = "");
	bool IsCrated(size_t stationIndex) const;
	// Where the next delivery lands: on top of the shortest pile by the door.
	glm::vec3 NextDeliverySpot() const;
	// Sets a part down loose on the parts shelf, in the first gap from the left on the lowest board
	// it fits (or in front of it when it's full). Where the core starts, in part order, and where
	// parts that are put away go.
	void PutOnShelf(ItemVisual& visual);
	// The order parts are shelved in: big ones first, then by system and kind.
	void SortForShelves(std::vector<ItemRef>& parts) const;
	// Takes every part off the shelves and puts each back on its own shelf, in order. For saves from
	// before there were two.
	void ReorganizeShelves();
	// Little icon labels over the deliveries, showing what's in each.
	void DrawPackageLabels(const Workshop& workshop, const FirstPersonController& player) const;
	void OpenPackage(Package& package);
	void UpdateStations(const Workshop& workshop);
	void UpdateEngineMotion(float deltaTime, const Workshop& workshop, const InputManager& input, bool playerHasControl);
	void LayoutItems(const Workshop& workshop, const FirstPersonController& player);
	void PlaceResting(ItemVisual& visual, const glm::vec3& anchor, float yaw) const;
	void PlaceOnStand(ItemVisual& visual, AssemblySlot slot, int index) const;
	// Where a part in that position sits in engine space (before the stand's own turn and roll).
	struct StandPose
	{
		glm::vec3 offset = {};
		glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
		glm::vec3 scale = { 1.f, 1.f, 1.f };
	};
	StandPose PoseOnStand(int engine, AssemblySlot slot, int index) const;
	// A nut or bolt's place: one of its part's holes.
	StandPose FastenerPose(int engine, AssemblySlot slot, int index) const;
	// The middle of where a part in that position would be, in the world: what you aim at to fit it.
	glm::vec3 SlotWorldCenter(int engine, AssemblySlot slot, int index) const;
	// A part type's model bounds (min, max), cached.
	const std::pair<glm::vec3, glm::vec3>& PartBounds(PartType type) const;
	// Engine space (relative to the mount) to world, including the stand's roll. The engine sits
	// with its flywheel end (cylinder 4) against the stand's mounting plate.
	glm::vec3 StandToWorld(int engine, const glm::vec3& offset) const;
	glm::quat StandRotation(int engine) const;
	// The engine the player is looking at (its stand or a part on it), or -1.
	int LookedAtEngine() const;
	int LookedAtStandEngine() const;
	void PlaceInHand(ItemVisual& visual, const FirstPersonController& player) const;
	void UpdateOverlays(ItemVisual& visual) const;

	// Physics
	float SupportHeight(const ShopBox& bounds, float previousBottom, const Object* self) const;
	// Calls visit(object, box) for every item and delivery something could rest on.
	template <typename Visit> void ForEachSupport(Visit&& visit) const;
	void StepBody(const pObject& object, Body& body, float deltaTime);
	std::optional<glm::vec3> AimPoint(const FirstPersonController& player) const;

	// Interaction
	void UpdateHover(const Workshop& workshop, const FirstPersonController& player);
	// Marks every empty position the held part could go in on the engines in reach, and picks the
	// one nearest the crosshair (if it's close enough). Returns that engine's stand.
	std::optional<size_t> FindFitTarget(const Workshop& workshop, const FirstPersonController& player, const ItemVisual& held);
	// Marks the empty bolt and nut holes close by on parts fitted to an engine, whatever is in hand,
	// with the size the one nearest the crosshair takes.
	void MarkEmptyHoles(const Workshop& workshop, const FirstPersonController& player);
	// A card on the right about the nut or bolt being looked at (or the empty hole): its size, the
	// spanner it takes, and where it goes and how tight, or how tight it is.
	void DrawHardwarePanel(const Workshop& workshop) const;
	// A ratchet, breaker bar, torque wrench or the wrench set in hand: sockets go on from a set within
	// reach, and nuts and bolts are done up or undone. True if it used this frame's E.
	bool UpdateSpanner(Workshop& workshop, const FirstPersonController& player, const InputManager& input, size_t toolIndex);
	struct SocketTarget
	{
		int size = 0;
		glm::vec3 position = {};
		bool putBack = false;	// the empty place of the socket on the tool in hand
	};
	// A socket in a set within reach, or the empty place of heldSize, the socket on the tool in hand.
	std::optional<SocketTarget> FindSocketTarget(const FirstPersonController& player, int heldSize);
	// Whether a socket of that size is out of its set, on a ratchet, breaker bar or torque wrench.
	bool SocketInUse(int sizeMm) const;
	// Puts a tool's sockets where they belong: in the set's case, or on the tool's square drive.
	void UpdateSockets(ItemVisual& visual) const;
	struct FastenerTarget
	{
		int engine = 0;
		AssemblySlot slot = AssemblySlot::BLOCK;
		int index = 0;
		glm::vec3 position = {};
	};
	// The fitted nut or bolt nearest the crosshair; only ones of sizeMm get markers (all of them if 0).
	std::optional<FastenerTarget> FindFastenerTarget(const Workshop& workshop, const FirstPersonController& player, int sizeMm);
	// "13 mm socket", "no socket", "13 mm wrench", or "" for anything that isn't a spanner.
	std::string SpannerLabel(size_t toolIndex) const;
	std::optional<UIRequest> Interact(Workshop& workshop);
	std::optional<UIRequest> UseStation(Workshop& workshop, const Station& station);
	void PickUp(Workshop& workshop, ItemVisual& visual);
	void PlaceInWorld(ItemVisual& visual, const glm::vec3& position, const glm::vec3& velocity);
	void SetPlace(ItemVisual& visual, ItemPlace place, int machineIndex = -1);
	ItemVisual* FindItemAt(ItemPlace place, int machineIndex = -1);
	ItemVisual* Find(const ItemRef& ref);
	const ItemVisual* Find(const ItemRef& ref) const;
	ItemVisual* GetHeld();
	const ItemVisual* GetHeld() const;
	glm::ivec2 GridSize(const ItemVisual& visual) const;
	bool EngineTurnsFreely(const Workshop& workshop, int engine, std::string& reason) const;
	void Toast(const std::string& message);
	void Cue(Sound sound, const glm::vec3& position, float volume = 1.f);

	// Measuring
	// Also lists every point the tool could measure within reach, for the HUD markers.
	// Why a feature of a part can't be measured where it is (something fitted over it, or the part is
	// fitted to the engine itself), or nothing if it can.
	std::optional<std::string> MeasureBlockedBy(const Workshop& workshop, const ItemVisual& visual, const std::string& feature) const;
	// The next Plastigauge step on a main, rod or cam journal, or NONE with why it can't be done now.
	StripStep PlastigaugeStep(const Workshop& workshop, int engine, AssemblyCheck check, bool rod, int index, std::string& why) const;
	// Squashes strips whose cap has just been torqued down, and loses ones the crank has turned on
	// (or come out from under).
	void UpdatePlastigauge(const Workshop& workshop);
	std::optional<MeasureTarget> FindMeasureTarget(const Workshop& workshop, const FirstPersonController& player, int toolIndex,
		std::vector<glm::vec3>& reachablePoints) const;
	void Measure(Workshop& workshop, const MeasureTarget& target, int toolIndex);
	Range AssemblyTarget(const EngineSpec& spec, AssemblyCheck check, bool rod) const;

	// Moving equipment
	void StartMove(size_t stationIndex);
	// Deliveries are carried in both hands: picked up with E, set down with RMB or dropped with G.
	void PickUpPackage(uint32_t packageId);
	void UpdateCarriedPackage(const FirstPersonController& player, const InputManager& input);
	// Puts everything riding on the station being moved back on it, wherever the station now is.
	void CarryRiders(const Station& station);
	bool IsRiding(const ItemRef& item, uint32_t packageId) const;
	void UpdateMove(const FirstPersonController& player, const InputManager& input);
	bool StationFits(const Station& station, const glm::vec3& position, float yaw, const FirstPersonController* player) const;
	void PlaceNewMachine(Station& station);

	// HUD
	std::optional<glm::vec2> ProjectToScreen(const glm::vec3& point, const FirstPersonController& player) const;
	void DrawHotbar(const Workshop& workshop) const;

	const ShopAssets*								m_assets = nullptr;
	std::vector<Station>							m_stations = {};
	size_t										m_fixedStations = 0;	// the shop's own equipment; furniture comes after
	std::vector<FloorObstacle>						m_obstacles = {};
	std::vector<ShopBox>							m_surfaces = {};
	std::vector<FloorObstacle>						m_staticObstacles = {};	// walls, trees, the car...
	std::vector<ShopBox>							m_walls = {};
	std::vector<pObject>							m_lampHeads = {};
	// The garage's own building, swapped for the version that goes with the upgrades.
	pObject											m_garageWalls = nullptr;
	pObject											m_garageSiding = nullptr;
	pObject											m_garageRoof = nullptr;
	pObject											m_garageFrames = nullptr;
	pObject											m_garageGlass = nullptr;
	std::vector<pObject>							m_sideDrums = {};		// outside the left wall, where the side bay goes
	std::vector<FloorObstacle>						m_sceneryObstacles = {};	// trees, poles, cars, the dumpster
	std::vector<FloorObstacle>						m_drumObstacles = {};
	std::vector<pObject>							m_upgradeObjects = {};		// what the upgrades added
	std::vector<WalkSurface>						m_walkSurfaces = {};
	std::vector<ShopBox>							m_upgradeSurfaces = {};	// the upstairs floor, for things set down there
	std::vector<glm::vec3>							m_shopLamps = {};
	bool											m_bayBuilt = false;
	bool											m_upperBuilt = false;
	bool											m_upgradesApplied = false;
	uint64_t										m_syncedStock = 0;		// the parts and tools last synced (see Update)
	size_t											m_liftStation = SIZE_MAX;
	bool											m_sleepRequested = false;
	bool											m_readRequested = false;
	pObject											m_liftArms = nullptr;
	float											m_liftHeight = 0.08f;
	bool											m_liftRaised = false;
	size_t										m_doorStation = 0;
	bool											m_doorOpen = true;
	float											m_doorAmount = 1.f;
	// The extension's own roll-up door: there only once it's built, shut until it's opened.
	size_t										m_bayDoorStation = SIZE_MAX;
	bool											m_bayDoorOpen = false;
	float											m_bayDoorAmount = 0.f;
	std::unordered_map<ItemRef, ItemVisual, ItemRefHash>	m_items = {};
	std::unordered_map<uint32_t, Package>			m_packages = {};
	std::unordered_map<const Object*, Target>		m_targets = {};
	PlayerInventory									m_inventory = {};
	ItemRef											m_equipped = {};
	bool											m_initialSyncDone = false;
	uint32_t										m_nextPackageId = 1;
	uint32_t										m_deliveries = 0;

	// Per engine stand
	std::array<float, Workshop::MAX_ENGINES>		m_crankAngles = {};
	std::array<float, Workshop::MAX_ENGINES>		m_engineRolls = {};	// each stand's rotation about the crank axis
	// Where each engine was when its parts were last laid out, and whether it has moved since.
	struct StandLayout
	{
		glm::vec3 position = {};
		glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
		float crank = 0.f;
	};
	std::array<StandLayout, Workshop::MAX_ENGINES>	m_laidOutStands = {};
	std::array<bool, Workshop::MAX_ENGINES>			m_standMoved = {};
	float											m_engineRpm = 0.f;		// on the dyno
	std::vector<SoundCue>							m_soundCues = {};
	bool											m_doorMoving = false;
	bool											m_liftMoving = false;
	int												m_dynoEngine = -1;
	float											m_handTurnRpm = 0.f;
	float											m_time = 0.f;

	Units::System									m_units = Units::System::METRIC;
	float											m_fieldOfView = 70.f;

	std::optional<Target>							m_hover = std::nullopt;
	std::optional<MeasureTarget>					m_measureTarget = std::nullopt;
	// What's in the way of the measuring spot nearest the crosshair, when it can't be measured.
	mutable std::string								m_measureBlocked = "";
	std::vector<PlastigaugeStrip>					m_plastigauge = {};
	std::vector<pObject>							m_stripObjects = {};		// one green strip on the journal per strip laid
	std::vector<glm::vec3>							m_measurePoints = {};
	std::optional<MoveState>						m_move = std::nullopt;
	// Carrying a part near an engine: the empty positions it could go in, and the one aimed at.
	struct FitPoint
	{
		glm::vec3 position = {};
		bool ready = false;		// everything it needs is already fitted
	};
	struct FitTarget
	{
		int engine = 0;
		AssemblySlot slot = AssemblySlot::BLOCK;
		int index = 0;
		glm::vec3 position = {};
		bool ready = false;
	};
	std::vector<FitPoint>							m_fitPoints = {};
	// Empty bolt and nut holes near the player (see MarkEmptyHoles).
	struct HolePoint
	{
		glm::vec3 position = {};
		std::string label = "";		// the size it takes, on the one nearest the crosshair
	};
	std::vector<HolePoint>							m_holePoints = {};
	// For the hardware card: the empty hole nearest the crosshair, and the fitted nut or bolt the
	// spanner in hand is on.
	std::optional<FastenerTarget>					m_aimedHole = std::nullopt;
	std::optional<FastenerTarget>					m_spannerFastener = std::nullopt;
	std::optional<FitTarget>						m_fitTarget = std::nullopt;
	// A spanner in hand: the sockets it can take from a set within reach, and the nuts and bolts it can
	// reach (coloured by how tight they are), each with a label.
	struct MarkedPoint
	{
		glm::vec3 position = {};
		uint32_t color = 0;
		std::string label = "";
	};
	std::vector<MarkedPoint>						m_spannerPoints = {};
	// The view and projection ProjectToScreen last used, kept while the view doesn't change.
	struct ScreenProjection
	{
		bool valid = false;
		glm::vec3 eye = {};
		glm::vec3 forward = {};
		glm::vec2 display = {};
		float fieldOfView = 0.f;
		glm::mat4 matrix = glm::mat4(1.f);
	};
	mutable ScreenProjection						m_screenProjection = {};
	// Each part model's bounds, looked up once: every fastener's pose needs its parent's, every frame.
	mutable std::unordered_map<PartType, std::pair<glm::vec3, glm::vec3>> m_partBounds = {};
	std::optional<glm::vec3>						m_spannerTarget = std::nullopt;
	// mm: the socket on each ratchet, breaker bar and torque wrench, and the wrench picked from the set.
	std::map<size_t, int>							m_toolSizes = {};
	bool											m_supportChanged = true;	// something was lifted, removed or moved
	bool											m_recheckSupport = true;	// this frame, resting things check what's under them
	// While the bodies are stepped: everything that can be stood on, with its box, gathered once for
	// the frame rather than once per body (hundreds of loose bolts made that a stall).
	mutable std::vector<std::pair<const Object*, ShopBox>>	m_supportBoxes = {};
	mutable bool									m_supportBoxesReady = false;
	bool											m_steppingBodies = false;
	bool											m_wasMoving = false;		// carrying equipment or a delivery last frame
	uint32_t										m_carriedPackage = 0;		// the delivery in your arms, or 0
	bool											m_handEmpty = false;		// the selected slot's item put away with its number
	int												m_engineOnDyno = -1;		// the Workshop's, kept here for posing it
	bool											m_fillAllHoles = false;		// Shift held: fit every matching nut or bolt carried
	Readout											m_readout = {};
	std::string										m_prompt = "";
	std::string										m_secondaryPrompt = "";
	std::string										m_heldHint = "";	// what the item in hand can do, by the hotbar
	std::string										m_toast = "";
	float											m_toastTime = 0.f;
	size_t											m_seenLogSize = 0;
};
