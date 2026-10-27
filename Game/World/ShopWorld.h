#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "LV/Engine/Objects/Object.h"
#include "../Inventory/PlayerInventory.h"
#include "../Simulation/Units.h"
#include "../Simulation/Workshop.h"
#include "../UI/WorkshopUI.h"
#include "FirstPersonController.h"
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
	DOOR		// the roll-up door: opens and closes, never moves
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

	// Backpack and hands
	PlayerInventory& GetInventory();
	ItemInfo DescribeItem(const ItemRef& item, const Workshop& workshop) const;
	ItemRef GetEquipped() const;
	// Takes the item in hand (or puts it back if it's already there).
	void ToggleEquip(const ItemRef& item);
	// Drops an item from the backpack onto the floor in front of the player.
	void DropFromInventory(const ItemRef& item, const FirstPersonController& player);

	// Parts on the workbench, then parts set up in machines: what the Bench tab works on.
	std::vector<BenchItem> GetBenchItems() const;

	// Moving equipment
	bool IsMovingStation() const;
	void CancelMove();

	// The roll-up door and the lot outside
	bool IsDoorOpen() const;
	void SetDoorOpen(bool open, bool instant = false);
	// 0 shut to 1 fully rolled up.
	float GetDoorOpenAmount() const;
	// Street and porch lamps, so the lighting can make them glow at night.
	const std::vector<pObject>& GetLampHeads() const;
private:
	enum class TargetKind { ITEM, STATION, PACKAGE };
	struct Target
	{
		TargetKind kind = TargetKind::STATION;
		ItemRef item = {};
		uint32_t packageId = 0;
		size_t stationIndex = 0;
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
		uint32_t packageId = 0;	// a delivery being carried instead of a station (0: none)
		glm::quat originalRotation = glm::quat(1.f, 0.f, 0.f, 0.f);
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

	// Outdoors (ShopWorldOutdoors.cpp)
	void BuildOutdoors();
	void UpdateDoor(float deltaTime);
	// Garage walls (and the door while it's down) as solid boxes.
	std::vector<ShopBox> SolidWalls() const;
	std::optional<float> WallHit(const glm::vec3& origin, const glm::vec3& direction) const;
	bool InsideWalls(const ShopBox& box) const;

	// Sync and layout
	void SyncItems(Workshop& workshop);
	ItemVisual& CreateItem(const ItemRef& ref, const pPart& part, uint32_t modelIndex, uint32_t textureIndex);
	void RemoveItem(const ItemRef& ref);
	// Bought machines and engine stands appear in the shop.
	void SyncMachines(const Workshop& workshop);
	// Dropped on the pile by the roll-up door.
	void CreatePackage(const std::vector<ItemRef>& items, const std::vector<size_t>& stations);
	bool IsCrated(size_t stationIndex) const;
	// Where the next delivery lands: on top of the shortest pile by the door.
	glm::vec3 NextDeliverySpot() const;
	// Sets a part down loose on the parts shelf, in the first gap from the left on the lowest board
	// it fits (or in front of it when it's full). Where the core starts, in part order, and where
	// parts that are put away go.
	void PutOnShelf(ItemVisual& visual);
	// Little icon labels over the deliveries, showing what's in each.
	void DrawPackageLabels(const Workshop& workshop, const FirstPersonController& player) const;
	void OpenPackage(Package& package);
	void UpdateStations(const Workshop& workshop);
	void UpdateEngineMotion(float deltaTime, const Workshop& workshop, const InputManager& input, bool playerHasControl);
	void LayoutItems(const Workshop& workshop, const FirstPersonController& player);
	void PlaceResting(ItemVisual& visual, const glm::vec3& anchor, float yaw) const;
	void PlaceOnStand(ItemVisual& visual, AssemblySlot slot, int index) const;
	// Engine space (relative to the mount) to world, including the stand's roll. The engine sits
	// with its flywheel end (cylinder 4) against the stand's mounting plate.
	glm::vec3 StandToWorld(int engine, const glm::vec3& offset) const;
	glm::quat StandRotation(int engine) const;
	// The engine the player is looking at (its stand or a part on it), or -1.
	int LookedAtEngine() const;
	void PlaceInHand(ItemVisual& visual, const FirstPersonController& player) const;
	void UpdateOverlays(ItemVisual& visual) const;

	// Physics
	float SupportHeight(const ShopBox& bounds, float previousBottom, const Object* self) const;
	void StepBody(const pObject& object, Body& body, float deltaTime);
	std::optional<glm::vec3> AimPoint(const FirstPersonController& player) const;

	// Interaction
	void UpdateHover(const Workshop& workshop, const FirstPersonController& player);
	std::optional<UIRequest> Interact(Workshop& workshop);
	std::optional<UIRequest> UseStation(Workshop& workshop, const Station& station);
	void PickUp(Workshop& workshop, ItemVisual& visual);
	void PlaceInWorld(ItemVisual& visual, const glm::vec3& position, const glm::vec3& velocity);
	void SetPlace(ItemVisual& visual, ItemPlace place, int machineIndex = -1);
	void Stow(ItemVisual& visual);
	ItemVisual* FindItemAt(ItemPlace place, int machineIndex = -1);
	ItemVisual* Find(const ItemRef& ref);
	const ItemVisual* Find(const ItemRef& ref) const;
	ItemVisual* GetHeld();
	const ItemVisual* GetHeld() const;
	glm::ivec2 GridSize(const ItemVisual& visual) const;
	bool EngineTurnsFreely(const Workshop& workshop, int engine, std::string& reason) const;
	void Toast(const std::string& message);

	// Measuring
	// Also lists every point the tool could measure within reach, for the HUD markers.
	std::optional<MeasureTarget> FindMeasureTarget(const Workshop& workshop, const FirstPersonController& player, int toolIndex,
		std::vector<glm::vec3>& reachablePoints) const;
	void Measure(Workshop& workshop, const MeasureTarget& target, int toolIndex);
	Range AssemblyTarget(const EngineSpec& spec, AssemblyCheck check, bool rod) const;

	// Moving equipment
	void StartMove(size_t stationIndex);
	void StartMovePackage(uint32_t packageId);
	// Puts everything riding on the station being moved back on it, wherever the station now is.
	void CarryRiders(const Station& station);
	bool IsRiding(const ItemRef& item, uint32_t packageId) const;
	void UpdateMove(const FirstPersonController& player, const InputManager& input);
	void UpdatePackageMove(const FirstPersonController& player, const InputManager& input);
	bool StationFits(const Station& station, const glm::vec3& position, float yaw, const FirstPersonController* player) const;
	void PlaceNewMachine(Station& station);

	// HUD
	std::optional<glm::vec2> ProjectToScreen(const glm::vec3& point, const FirstPersonController& player) const;
	void DrawHotbar(const Workshop& workshop) const;

	const ShopAssets*								m_assets = nullptr;
	std::vector<Station>							m_stations = {};
	std::vector<FloorObstacle>						m_obstacles = {};
	std::vector<ShopBox>							m_staticSurfaces = {};
	std::vector<ShopBox>							m_surfaces = {};
	std::vector<FloorObstacle>						m_staticObstacles = {};	// walls, trees, the car...
	std::vector<ShopBox>							m_walls = {};
	std::vector<pObject>							m_lampHeads = {};
	size_t										m_doorStation = 0;
	bool											m_doorOpen = true;
	float											m_doorAmount = 1.f;
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
	float											m_engineRpm = 0.f;		// on the dyno
	int												m_dynoEngine = -1;
	float											m_handTurnRpm = 0.f;
	float											m_time = 0.f;

	Units::System									m_units = Units::System::METRIC;
	float											m_fieldOfView = 70.f;

	std::optional<Target>							m_hover = std::nullopt;
	std::optional<MeasureTarget>					m_measureTarget = std::nullopt;
	std::vector<glm::vec3>							m_measurePoints = {};
	std::optional<MoveState>						m_move = std::nullopt;
	Readout											m_readout = {};
	std::string										m_prompt = "";
	std::string										m_secondaryPrompt = "";
	std::string										m_heldHint = "";	// what the item in hand can do, by the hotbar
	std::string										m_toast = "";
	float											m_toastTime = 0.f;
	size_t											m_seenLogSize = 0;
};
