#pragma once

#include <array>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LV/Engine/Graphics/Lighting.h"
#include "LV/Engine/Objects/Object.h"
#include "../Audio/Sounds.h"
#include "../Inventory/PlayerInventory.h"
#include "../Simulation/Units.h"
#include "../Simulation/Workshop.h"
#include "../UI/WorkshopUI.h"
#include "FirstPersonController.h"
#include "GarageLayout.h"
#include "PlayerAvatar.h"
#include "ShopAssets.h"
#include "WallPaint.h"

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
	HOIST,		// the engine hoist: lifts an engine off a stand or the dyno, and lowers it onto one
	CAR			// the project car on the drive: E works on it; the hoist lifts its engine in and out
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
	INVENTORY,	// in the backpack and not in hand
	CARRIED,	// another player has it, in a game over the network (never saved)
	ATTACHED,	// a bearing shell fitted into a loose rod or cap (ItemVisual::hostPart), going about in it (saved as STORAGE);
				// also a spring on a loose strut, a tire on a loose wheel
	CAR			// on the project car, or in an engine that is (hidden till the car is modelled; saved as STORAGE)
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
	// Put down turned some other way than flat (see TurnHeld): its turn, after the yaw. Only kept while it's down.
	glm::quat tilt = glm::quat(1.f, 0.f, 0.f, 0.f);
	std::vector<std::pair<SurfaceOverlay, pObject>> overlays = {};
	// Tools: the sockets of a socket set, or the socket on a ratchet, breaker bar or torque wrench.
	std::vector<pObject> sockets = {};
	// Fitted to an engine: the position it was last laid out in, so it's only re-posed when that or the
	// engine moves.
	std::pair<AssemblySlot, int> standSlot = { AssemblySlot::COUNT, -1 };
	// A nut or bolt: how far out of its hole it was laid out (see FastenerRise).
	float standRise = -1.f;
	// Fitted to an engine: where it sits in the engine's own frame as last laid out, so when only the engine moves (rolled
	// on its stand, carried on the hoist) it's just carried along (see LayoutItems).
	glm::vec3 standOffset = {};
	glm::quat standTurn = glm::quat(1.f, 0.f, 0.f, 0.f);
	// In a loose cylinder head: its place there (a valve, or the seal, spring or retainer on one), as last laid out, so the
	// bench can list each valve with what's on it. COUNT when it isn't in a head.
	std::pair<AssemblySlot, int> headSpot = { AssemblySlot::COUNT, -1 };
	// On the project car: the position it was last laid out in, and whether it stood out of its hole (a loose nut or
	// bolt), so it's only re-posed when one of those changes (or the car does: see m_carLaidOut). -1: not laid out.
	int carSlot = -1;
	int carIndex = -1;
	bool carRaised = false;
	// CARRIED: which player has it (see NetSession), and whether it's in their hand.
	uint32_t hostPart = 0;		// ATTACHED: the rod or cap it's fitted into
	int carrier = -1;
	bool carriedInHand = false;
};

// Spray paint over a part or tool: its colour (RGB) and how thick it's gone on, 0 to 1.
struct PaintCoat
{
	uint32_t rgb = 0;
	float amount = 0.f;
};

// A battery pack for the cordless tools: how full (0 to 1), and what it's on: a cordless tool or a
// charger (a tool index), or nothing (-1).
struct BatteryPack
{
	float charge = 1.f;
	int on = -1;
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
	friend class NetState;
	friend class NetSession;
	friend class PlayerAvatar;
public:
	// Creates the static scene. Call from Game::OnLoadAssets after the assets are loaded.
	void Build(const ShopAssets& assets);

	glm::vec3 GetSpawnPosition() const;
	float GetSpawnYaw() const;
	// The turn-held key is down with something in hand: the mouse turns it, not the view.
	bool TurningHeld(const InputManager& input) const;
	// The other garages on this server, each on its own plot this far along X from this one: their buildings,
	// shut up, with their drives, solid to walk into, and the desert cleared round them. Empty for none.
	void SetNeighbours(const std::vector<float>& offsets);
	const std::vector<FloorObstacle>& GetObstacles() const;
	FloorObstacle GetRoomBounds() const;

	// First person: the player's own right arm, holding what's in hand, and their body below the view.
	// Hidden while the camera is somewhere else (the menu's view).
	void SetViewModelVisible(bool visible) { m_viewModelVisible = visible; }
	// How the player's monkey looks, in first person and in third.
	void SetPlayerLook(const MonkeyLook& look);
	const MonkeyLook& GetPlayerLook() const { return m_look; }
	// How the player likes it (see GameSettings): which of it shows, where the hand is (it and what it
	// holds moved from where they'd be, in the view's own right, up and forward), and how far behind
	// the eyes the body stands.
	struct ViewModelSettings
	{
		bool showArm = true;
		bool showBody = true;
		glm::vec3 handOffset = {};
		float bodyBack = 0.3f;
	};
	void SetViewModelSettings(const ViewModelSettings& settings) { m_viewSettings = settings; }
	// How the player's arms are held, for other players to see: 0 down, 1 the right one out (something
	// in hand), 2 both (carrying a delivery).
	int ArmsOut() const;
	// Third person: the camera over the player's right shoulder, looking where they look, and their
	// whole body in view (holding what's in hand) in place of the first person arm.
	void SetThirdPerson(bool thirdPerson) { m_thirdPerson = thirdPerson; m_lookingBack = false; }
	// The view key: first person, then from behind, then from in front looking back at you, then first
	// person again.
	void CycleView();
	bool IsLookingBack() const { return m_thirdPerson && m_lookingBack; }
	bool IsThirdPerson() const { return m_thirdPerson; }
	// Where the camera goes in third person: behind the shoulder, pulled in when a wall or a station is
	// in the way, aimed so the middle of the view is where the player is looking. Kept for the HUD.
	struct CameraView
	{
		glm::vec3 position = {};
		glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
	};
	CameraView UpdateThirdPersonCamera(const FirstPersonController& player);
	// Sitting in the car, and driving it (ShopWorldCar.cpp). [F] at the car gets into the seat nearest what's aimed at:
	// the driver's turns the key, and it then goes where it's driven, about the lot, down the drive and round the roads,
	// and stays where it's left. The other seats are for the ride.
	bool IsSeated() const { return m_seat >= 0; }
	bool IsDriving() const { return m_drive.active; }
	// Each frame in a seat, in place of walking: at the wheel the pedals and the wheel and the car moved; in any seat the
	// player kept in it (turned with the car, free to look about), and put out beside its door when they get out.
	void Drive(float deltaTime, Workshop& workshop, FirstPersonController& player, const InputManager& input, bool playerHasControl);
	// The headlights' beams on the road, when they're up and on, for the lighting.
	std::vector<PointLight> GetCarLights() const;
	// From the driver's seat, or (third person) from behind and above the car, swung round it with the mouse.
	CameraView DriveCamera(const FirstPersonController& player);
	// The engine's note at the wheel, and where the car is, for the sound of it.
	float GetDriveRpm() const { return m_drive.active ? m_drive.rpm : 0.f; }
	glm::vec3 GetCarPosition() const { return CarToWorld({ 1.2f, 0.6f, 0.f }); }
	// Developer shortcuts: gets in (true if it started), and holds the throttle down for a while with the wheel turned.
	bool DevStartDrive(Workshop& workshop);
	void DevDrive(float seconds, float steer) { m_drive.devSeconds = seconds; m_drive.devSteer = steer; }
	// Into a seat (0 the driver's, 1 the passenger's, 2 and 3 the back), out of it, and the headlights up or down.
	void DevSit(int seat, Workshop& workshop, FirstPersonController& player) { LeaveSeat(&player); Sit(seat, workshop); }
	void DevHeadlights() { m_headlights.on = !m_headlights.on; }
	// Puts the car down somewhere else, pointing along a heading (degrees: 0 toward +x, 90 toward -z).
	void DevPlaceCar(float x, float z, float degrees);
	// Turns the parked car's front wheels (-1 full right lock to 1 full left), to look at them from outside.
	void DevSteer(float steer);

	std::optional<UIRequest> Update(float deltaTime, Workshop& workshop, const FirstPersonController& player,
		const InputManager& input, bool playerHasControl);
	void DrawHud(const Workshop& workshop, const FirstPersonController& player) const;
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
	// The player opened the service manual in their hand this frame.
	// The book the player opened with a manual in hand this frame (MeasuringTool::manualBook), if they did.
	std::optional<int> TakeReadRequest();
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
	// Developer shortcut: takes an item straight into the hand from wherever it is (out of its delivery, too).
	bool DevHold(const ItemRef& item);
	// Dev: sets what's in hand down at a spot, turned to a heading, upside down if asked. False with nothing in hand.
	bool DevDropHeld(const glm::vec3& position, float yaw, bool upsideDown);
	// Dev: clamps the Valve Spring Compressor over a spring in a loose head, as E with it in hand does.
	bool DevClampCompressor(Workshop& workshop, const pPart& head, int index);
	// Dev: every door, the hood, the trunk lid and the fuel flap open (or all shut).
	void DevOpenCar(bool open) { for (auto& panel : m_carPanels) panel.open = open; }
	void DevRollStand(int stand, float radians) { m_engineRolls[std::clamp(stand, 0, Workshop::MAX_ENGINES - 1)] = radians; }
	// Dev: keeps a stand rolling at this many radians a second (0 stops it), to profile a moving engine.
	void DevSpinStand(int stand, float radiansPerSecond) { m_devSpin = { std::clamp(stand, 0, Workshop::MAX_ENGINES - 1), radiansPerSecond }; }

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
		bool looseRing = false;			// a ring's end gap, in the bore of a block off the engine (part is the ring)
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
	// The shop's own bench, or a wooden one bought from the store: parts set on either are read there.
	bool IsWorkbench(const Station& station) const;
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
	// Whether it's out on the drive in front of the roll-up door (or in the open doorway): where the hoist can be
	// rolled to reach the project car.
	bool OnDrive(const ShopBox& footprint) const;
	// Out on the shop's own land, round the building and down the drive, clear of what stands there: where the shop's
	// equipment can be moved to outside.
	bool OnPlot(const ShopBox& footprint) const;
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
	// Delivered at the door (NextDeliverySpot), or wherever at says.
	void CreatePackage(const std::vector<ItemRef>& items, const std::vector<size_t>& stations, const std::string& title = "",
		std::optional<glm::vec3> at = std::nullopt);
	bool IsCrated(size_t stationIndex) const;
	// Where the next delivery lands: on top of the shortest pile by the door.
	glm::vec3 NextDeliverySpot() const;
	// Where each tool hangs on the tool board, in the board's own space: worked out once from the
	// tool models, the long ones first, with the manual in the bottom right corner.
	void LayOutToolBoard();
	// Aiming at the tool board (or a tool on it): the spot on its face looked at, in the board's space (across and up).
	std::optional<glm::vec2> AimedBoardSpot(const FirstPersonController& player) const;
	// Hangs the tool in hand on the tool board, its middle at that spot (kept on the board).
	void HangToolAt(ItemVisual& held, const glm::vec2& spot);
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
	// Opens a delivery where it stands or, given the player carrying it, into their backpack and hotbar,
	// with whatever doesn't fit going on the floor at their feet.
	void OpenPackage(Package& package, const FirstPersonController* player = nullptr);
	// A clear patch of floor for a loose item (its model's bounds) near origin, at origin's height: out of
	// the walls and on origin's side of them, not under a bench or shelf, and clear of the spots taken.
	std::optional<glm::vec3> FloorSpotNear(const glm::vec3& origin, const ShopBox& localBounds, std::vector<std::pair<glm::vec2, float>>& taken) const;
	void UpdateStations(const Workshop& workshop);
	void UpdateEngineMotion(float deltaTime, const Workshop& workshop, const InputManager& input, bool playerHasControl);
	// Turns an engine's crank on by an angle, and its cam with it if the timing belt's on.
	void TurnCrank(const Workshop& workshop, int engine, float by);
	void LayoutItems(const Workshop& workshop, const FirstPersonController& player);
	// A nut or bolt's colour while a spanner is in hand: how done up it is.
	glm::vec4 TightnessTint(const Part& fastener) const;
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
	// How far a fitted nut or bolt stands out of its hole, 0 (done up) to 1 (loose, a few turns out):
	// part way while it's being turned.
	float FastenerRise(const ItemVisual& visual, AssemblySlot slot, int index) const;
	// The middle of where a part in that position would be, in the world: what you aim at to fit it.
	glm::vec3 SlotWorldCenter(int engine, AssemblySlot slot, int index) const;
	// A part type's model bounds (min, max), cached.
	const std::pair<glm::vec3, glm::vec3>& PartBounds(PartType type) const;
	// Engine space (relative to the mount) to world, including the stand's roll. The engine sits
	// with its flywheel end (cylinder 4) against the stand's mounting plate.
	glm::vec3 StandToWorld(int engine, const glm::vec3& offset) const;
	glm::quat StandRotation(int engine) const;
	// Where an engine is: its deck's middle, and which way it's turned (StandToWorld is position +
	// rotation * offset). Resting is where it sits, on its stand, the dyno or the hoist; CurrentPose eases
	// it there from where it was, while it swings onto or off the hoist.
	struct EnginePose
	{
		glm::vec3 position = {};
		glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
	};
	EnginePose RestingPose(int engine) const;
	EnginePose CurrentPose(int engine) const;
	// What the hoist would lift or lower onto with E: a stand (its engine number), DYNO_TARGET, or nothing.
	static constexpr int DYNO_TARGET = -2;
	static constexpr int CAR_TARGET = -3;	// the project car's engine bay
	std::optional<int> HoistTarget(const Workshop& workshop) const;
	glm::vec3 HoistHook() const;
	// Follows the workshop's engines onto and off the dyno and the hoist (see m_engineOnDyno).
	void SyncEngineMoves(const Workshop& workshop);
	// The engine the player is looking at (its stand or a part on it), or -1.
	int LookedAtEngine() const;
	int LookedAtStandEngine() const;
	void PlaceInHand(ItemVisual& visual, const FirstPersonController& player) const;
	// Where something held in hand goes: its middle at point, turned by rotation, and the wrist of the
	// hand holding it.
	struct HoldPose
	{
		glm::vec3 point = {};
		glm::quat rotation = {};
		glm::vec3 center = {};		// the model's middle, in its own space
		float radius = 0.f;
		bool tool = false;
	};
	HoldPose HoldPoseOf(const ItemVisual& visual, const FirstPersonController& player) const;
	// A point depth metres ahead of the eye, right and down as fractions of the way from the middle of
	// the view to its edge (taking the screen as 16:9).
	glm::vec3 ViewPoint(const FirstPersonController& player, float right, float down, float depth) const;
	// The first person arm: it comes in from just below the bottom of the view, a tenth of the screen
	// in from the right corner, and the hand rests (with nothing in it) low and right of the middle,
	// an arm's length of about 0.4 m from there.
	glm::vec3 ViewShoulder(const FirstPersonController& player) const { return ViewPoint(player, 0.85f, 1.05f, 0.3f); }
	glm::vec3 ViewRestWrist(const FirstPersonController& player) const;
	// The player's chosen move of the hand (ViewModelSettings::handOffset), in the world.
	glm::vec3 HandOffset(const glm::quat& camera) const;
	void BuildViewModel();
	void UpdateViewModel(const FirstPersonController& player);
	// Third person: the player's own body, posed where they stand.
	void UpdateSelfAvatar(float deltaTime, const FirstPersonController& player);
	void UpdateOverlays(ItemVisual& visual) const;

	// Physics
	float SupportHeight(const ShopBox& bounds, float previousBottom, const Object* self) const;
	// Calls visit(object, box) for every item and delivery something could rest on.
	template <typename Visit> void ForEachSupport(Visit&& visit) const;
	void StepBody(const pObject& object, Body& body, float deltaTime);
	std::optional<glm::vec3> AimPoint(const FirstPersonController& player) const;

	// Interaction
	void UpdateHover(const Workshop& workshop, const FirstPersonController& player);
	// A nut or bolt on that engine near the crosshair, its head's face not behind something the ray hit this far
	// off: the one meant, though the ray caught the part it holds (a rod cap's nut, round the cap's bolt).
	const ItemVisual* FastenerNearCrosshair(int engine, const FirstPersonController& player, float hitDistance) const;
	// A part on an engine that only comes off with the one it's fitted to (a piston on its rod, the piston's ring, a
	// shell in a rod or cap) is taken off by taking that: the outermost one it's carried by. Itself otherwise.
	const ItemVisual* RemovalHost(const Workshop& workshop, const ItemVisual& visual) const;
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
	bool UpdateSpanner(Workshop& workshop, const FirstPersonController& player, const InputManager& input, size_t toolIndex, float deltaTime);
	// The piston pin press in hand, aimed at a piston on its loose rod: LMB held presses a pin in, RMB held presses it
	// out. Or the ring compressor, aimed at a piston in the top of its bore: LMB held pushes it home.
	bool UpdatePinPress(Workshop& workshop, const FirstPersonController& player, const InputManager& input, size_t toolIndex, float deltaTime);
	// A marker, spray can or rag in hand: held down, it paints (or wipes) the wall or floor it's at, or (spray
	// and rag) coats the part or tool. True while it's at work.
	bool UpdatePaint(const FirstPersonController& player, const InputManager& input, size_t toolId, float deltaTime);
	// The wall or floor face aimed at, within reach, if nothing's in the way.
	struct PaintHit
	{
		WallPaint::Plane plane = {};
		glm::vec3 point = {};
		float distance = 0.f;
	};
	std::optional<PaintHit> FindPaintHit(const FirstPersonController& player, float reach) const;
	// The marker's, spray can's or rag's size (0, 1, 2), for a paint tool.
	static int PaintSizeIndex(const MeasuringTool& tool);
	// Battery packs: clipped onto a cordless tool in hand (E on a pack, C takes it off), set on a
	// charger (E on the charger with a pack in hand, E again takes it back), and charging there. True
	// when the key was used.
	bool UpdateBatteries(Workshop& workshop, const InputManager& input, ItemVisual* held);
	void ChargeBatteries(float deltaTime);
	// Packs on a tool or a charger go with it: laid out after everything else.
	void LayOutBatteries();
	// Wrenches put away lie in their set; one in hand goes back with E on the set.
	void LayOutSetMembers();
	bool UpdateSetMembers(const InputManager& input, ItemVisual* held);
	// The pack on a cordless tool or charger (a tool index), if there is one.
	std::optional<size_t> BatteryOn(size_t host) const;
	float BatteryCharge(size_t battery) const;
	// "4.0 Ah, 62%".
	std::string BatteryLabel(size_t battery) const;
	struct SocketTarget
	{
		int size = 0;
		glm::vec3 position = {};
		bool putBack = false;	// the empty place of the socket on the tool in hand
	};
	// A socket in a set within reach, or the empty place of heldSize, the socket on the tool in hand.
	std::optional<SocketTarget> FindSocketTarget(const FirstPersonController& player, int heldSize);
	// Whether a socket of that size is out of its set, on a ratchet, breaker bar or torque wrench.
	// A socket of this size is out of this set (by its id) on a drive.
	bool SocketInUse(int sizeMm, size_t setId) const;
	// Puts a tool's sockets where they belong: in the set's case, or on the tool's square drive.
	void UpdateSockets(ItemVisual& visual) const;
	struct FastenerTarget
	{
		int engine = 0;
		AssemblySlot slot = AssemblySlot::BLOCK;
		int index = 0;
		glm::vec3 position = {};
		// On the project car instead: engine is -1, slot COUNT, and carSlot and index say which.
		bool car = false;
		CarSlot carSlot = CarSlot::COUNT;
	};
	// The project car (ShopWorldCar.cpp). Where each part sits on it, in the car's own space (+X its nose, +Z its
	// right side, y = 0 the ground under its wheels), and the car in the world.
	struct CarPose
	{
		glm::vec3 offset = {};
		glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
	};
	CarPose PoseOnCar(CarSlot slot, int index) const;
	// What opens on the car, with the left button aimed at it: the doors, the hood, the trunk lid and the fuel filler's flap.
	enum class CarPanel { DOOR_LEFT, DOOR_RIGHT, HOOD, TRUNK, FUEL_FLAP, COUNT };
	struct CarPanelState
	{
		float amount = 0.f;		// 0 shut to 1 open, on its way to where it's going
		bool open = false;
	};
	// How far open one is, eased (0 shut to 1 open).
	float CarPanelOpen(CarPanel panel) const;
	bool IsCarPanelOpen(CarPanel panel) const { return m_carPanels[static_cast<size_t>(panel)].open; }
	void ToggleCarPanel(CarPanel panel);
	// The one being aimed at, if any (a door, the hood or the lid on the car under the crosshair, or the fuel flap).
	std::optional<CarPanel> AimedCarPanel(const Workshop& workshop, const FirstPersonController& player) const;
	// Swings them toward open or shut (one that's been taken off the car is shut), and keeps the fuel flap on the car.
	void AnimateCarPanels(const Workshop& workshop, float deltaTime);
	// The left button on one opens or shuts it; says so in the prompt. True when it took the click.
	bool UpdateCarPanels(const Workshop& workshop, const FirstPersonController& player, const InputManager& input);
	glm::vec3 CarToWorld(const glm::vec3& offset) const;
	glm::quat CarRotation() const;
	// The middle of the part that goes in a position, where it would sit, in the world.
	glm::vec3 CarSlotCenter(CarSlot slot, int index) const;
	void PlaceOnCar(ItemVisual& visual, CarSlot slot, int index) const;
	// Carrying a part to the car: marks every empty position it could go in and picks the one nearest the crosshair
	// (m_carFitTarget). Returns the car's station if it found one.
	std::optional<size_t> FindCarFitTarget(const Workshop& workshop, const FirstPersonController& player, const ItemVisual& held);
	// A fitted nut or bolt on the car near the crosshair, those of sizeMm first.
	std::optional<FastenerTarget> FindCarFastenerTarget(const Workshop& workshop, const FirstPersonController& player, int sizeMm);
	// The prompt for the car station with a part in hand, aimed at a position on it.
	std::string CarFitPrompt(const Workshop& workshop, const ItemVisual& held) const;
	// Fits the part in hand where m_carFitTarget says (E at the car).
	void FitToCar(Workshop& workshop, ItemVisual& held);
	// The car's tools in hand (ShopWorldCarTools.cpp): the funnel at a filler, the bleeder on a nipple, the spring
	// compressor and tire irons on a loose strut or wheel, the timing light, tire gauge and alignment gauge. True while
	// it's aimed at something it works on (the prompt is its).
	bool UpdateCarTool(Workshop& workshop, const FirstPersonController& player, const InputManager& input, size_t toolIndex, float deltaTime);
	// The fitted nut or bolt nearest the crosshair, those of sizeMm first (any size if 0).
	std::optional<FastenerTarget> FindFastenerTarget(const Workshop& workshop, const FirstPersonController& player, int sizeMm);
	// "13 mm socket", "no socket", "13 mm wrench", or "" for anything that isn't a spanner.
	std::string SpannerLabel(size_t toolIndex) const;
	// What a torque wrench is set to, Nm (from its lowest up to the most it goes to).
	float TorqueSetting(size_t toolIndex) const;
	static constexpr float TORQUE_SETTING_MIN = 5.f;
	std::optional<UIRequest> Interact(Workshop& workshop);
	std::optional<UIRequest> UseStation(Workshop& workshop, const Station& station);
	void PickUp(Workshop& workshop, ItemVisual& visual);
	// A nut or bolt turned in part way (m_fastenerTurns): it has to be undone before it comes off by hand.
	bool PartWayDoneUp(const Part& part) const;
	// Put together off the engine (Workshop::FitShell, FitPiston): the shell or piston in hand into or onto a loose
	// rod or cap, or off one again (TakeOffWhat: its piston once unpinned, otherwise its shell).
	bool CanFitOnto(const Workshop& workshop, const ItemVisual& host) const;
	void FitOnto(Workshop& workshop, ItemVisual& host);
	pPart TakeOffWhat(const Workshop& workshop, const Part& host) const;
	pPart RingTarget(const Workshop& workshop, const Part& host) const;
	// What has to happen before a part goes in that position: its prerequisites, or for a ring set in a bore on its own
	// to be gapped, an open bore (Workshop::RingInBoreBlockedBy).
	std::optional<std::string> FitMissing(const Workshop& workshop, int engine, AssemblySlot slot, int index) const;
	// The ring set in a bore on its own, aimed at with a piston in hand: the cylinder it's in, to push it down square.
	std::optional<int> RingToSquare(const Workshop& workshop, const ItemVisual& ring) const;
	// The bore of a loose block the crosshair is in (found by UpdateHover).
	std::optional<int> AimedBore(const ItemVisual& block) const;
	// The ring in the bore aimed into, which E takes out (as on the stand) when there's nothing to fit there.
	pPart RingInAimedBore(const Workshop& workshop, const ItemVisual& block) const;
	void FindAimedBore(const Workshop& workshop, const ItemVisual& block, const FirstPersonController& player);
	// A cylinder head off the engine: where each valve, stem seal, spring and retainer sits in it (shut, in the head's own
	// frame); the place in it the part in hand would go, aimed at; and the part in it nearest the crosshair that can come out.
	StandPose HeadSpotPose(AssemblySlot slot, int index) const;
	void FindAimedHeadSpot(const Workshop& workshop, const ItemVisual& head, const FirstPersonController& player);
	// The Valve Spring Compressor in hand, aimed at a valve in this loose head with its spring in: clamp it over it.
	bool CanCompressAt(const ItemVisual& head) const;
	void CompressAt(Workshop& workshop, const ItemVisual& head);
	// The compressor's visual, if the shop has one (there's only the one).
	ItemVisual* ValveCompressorVisual();
	void TakeOff(Workshop& workshop, ItemVisual& host);
	// How an item lies when it's put down loose: turned to its yaw, and a spray can stood on its base.
	glm::quat RestingRotation(const ItemVisual& visual) const;
	// Come to rest turned up on an edge or a corner, it tips over onto the side nearest the floor (still facing the way
	// it was), rolling over the edge it landed on, faster as it goes. LevelOut starts that (whether it has to); StepTip
	// moves it on, and says when it's down.
	struct Tip
	{
		glm::vec3 pivot = {};			// the edge or corner it rolls over
		glm::vec3 axis = {};
		float total = 0.f;				// how far over it goes
		float angle = 0.f;
		float speed = 0.f;
		glm::quat start = glm::quat(1.f, 0.f, 0.f, 0.f);
		glm::vec3 startPosition = {};
	};
	std::unordered_map<ItemRef, Tip, ItemRefHash>		m_tips = {};
	bool LevelOut(ItemVisual& visual);
	bool StepTip(ItemVisual& visual, Tip& tip, float deltaTime);
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
	Range AssemblyTarget(const EngineAssembly& assembly, AssemblyCheck check, int index, bool rod) const;

	// Moving equipment
	void StartMove(size_t stationIndex);
	// Deliveries are carried in both hands: picked up with E, set down with RMB or dropped with G.
	void PickUpPackage(uint32_t packageId);
	// The delivery in hand: [F] opens it, [RMB] sets it down, [G] drops it.
	void UpdateCarriedPackage(const FirstPersonController& player, const InputManager& input);
	// Every delivery on the hotbar goes with you: the one in hand held out in front, the rest out of sight.
	void PoseCarriedPackages(const FirstPersonController& player);
	// Off the hotbar and into the shop, at position (moving at velocity).
	void ReleasePackage(uint32_t packageId, const glm::vec3& position, const glm::vec3& velocity);
	// On this player's hotbar, in hand or not.
	bool IsCarried(uint32_t packageId) const;
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
	size_t										m_activeBench = SIZE_MAX;	// the workbench last opened (in m_stations); SIZE_MAX, the shop's own
	std::vector<FloorObstacle>						m_obstacles = {};
	std::vector<ShopBox>							m_surfaces = {};
	std::vector<FloorObstacle>						m_staticObstacles = {};	// walls, trees...
	std::vector<ShopBox>							m_walls = {};
	std::vector<pObject>							m_lampHeads = {};
	// The garage's own building, swapped for the version that goes with the upgrades.
	pObject											m_garageWalls = nullptr;
	pObject											m_viewBody = nullptr;
	pObject											m_viewBodySkin = nullptr;
	pObject											m_viewSleeve = nullptr;
	pObject											m_viewHand = nullptr;
	std::vector<pObject>							m_heldInFront = {};	// what's in hand, drawn in front with the arm
	bool											m_thirdPerson = false;
	bool											m_lookingBack = false;	// third person, from in front
	PlayerAvatar									m_selfAvatar = {};
	CameraView										m_thirdPersonView = {};
	bool											m_viewModelVisible = true;
	MonkeyLook										m_look = {};
	ViewModelSettings								m_viewSettings = {};
	pObject											m_garageSiding = nullptr;
	pObject											m_garageRoof = nullptr;
	pObject											m_garageFrames = nullptr;
	pObject											m_garageGlass = nullptr;
	std::vector<pObject>							m_sideDrums = {};		// outside the left wall, where the side bay goes
	std::vector<FloorObstacle>						m_sceneryObstacles = {};	// trees, poles, the dumpster
	// The cacti and scrub scattered over the desert, so those on a neighbour's plot can be cleared away.
	struct Scenery
	{
		pObject object = nullptr;
		glm::vec2 at = {};
		std::vector<FloorObstacle> footprints = {};	// what of it is in the way
	};
	std::vector<Scenery>							m_scenery = {};
	glm::vec3										m_carLaidOutAt = glm::vec3(-1e9f);	// where the car stood when its parts were last laid out on it
	float											m_carLaidOutYaw = 0.f;
	// The other garages' buildings (SetNeighbours), and what can't be walked through of them.
	std::vector<pObject>							m_neighbourObjects = {};
	std::vector<FloorObstacle>						m_neighbourObstacles = {};
	std::vector<FloorObstacle>						m_drumObstacles = {};
	std::vector<pObject>							m_upgradeObjects = {};		// what the upgrades added
	std::vector<WalkSurface>						m_walkSurfaces = {};
	std::vector<ShopBox>							m_upgradeSurfaces = {};	// the upstairs floor, for things set down there
	std::vector<glm::vec3>							m_shopLamps = {};
	bool											m_bayBuilt = false;
	// Paint on the walls and floor, and the coats sprayed over parts and tools: colour (RGB) and how
	// thick, 0 to 1.
	WallPaint										m_paint = {};
	std::unordered_map<ItemRef, PaintCoat, ItemRefHash>	m_coats = {};
	std::optional<std::pair<WallPaint::Plane, glm::vec3>>	m_lastPaint = std::nullopt;	// where the marker was last frame, to draw on from
	// How big a marker, spray can and rag go on, in metres (the scroll wheel changes them), and where the
	// paint would go now, drawn as a ring on the wall round the crosshair.
	std::array<float, 3>							m_paintRadius = { 0.004f, 0.12f, 0.05f };
	struct PaintCursor
	{
		WallPaint::Plane plane = {};
		glm::vec3 point = {};
		float radius = 0.f;
	};
	std::optional<PaintCursor>						m_paintCursor = std::nullopt;
	bool											m_upperBuilt = false;
	bool											m_upgradesApplied = false;
	uint64_t										m_syncedStock = 0;		// the parts and tools last synced (see Update)
	size_t											m_liftStation = SIZE_MAX;
	std::optional<int>								m_readRequested = std::nullopt;
	pObject											m_liftArms = nullptr;
	float											m_liftHeight = 0.08f;
	bool											m_liftRaised = false;
	size_t										m_doorStation = 0;
	bool											m_doorOpen = true;
	float											m_doorAmount = 1.f;
	// The extension's own roll-up door: there only once it's built, shut until it's opened.
	size_t										m_bayDoorStation = SIZE_MAX;
	size_t									m_hoistStation = SIZE_MAX;
	size_t									m_carStation = SIZE_MAX;
	pObject									m_carStands = nullptr;	// under the car till it's on its wheels
	pObject									m_fuelFlap = nullptr;	// the fuel filler's flap, in the right rear quarter
	// The car's side glass, see-through: the fixed quarter windows, and each door's (kept on the door while it's on the car).
	pObject									m_quarterGlass = nullptr;
	std::array<pObject, 2>					m_doorGlass = {};
	std::array<CarPanelState, static_cast<size_t>(CarPanel::COUNT)>	m_carPanels = {};
	bool									m_carPanelsMoving = false;	// one swung this frame: what's on the car is posed again
	// The pop-up headlights: on (up and lit) or off, and how far up they've swung.
	struct Headlights
	{
		bool on = false;
		float amount = 0.f;
	};
	Headlights								m_headlights = {};
	std::array<pObject, 2>					m_headlightLenses = {};	// the lamps in their pods, lit when they're on
	static constexpr int GAUGES = 4;		// the speedo, the tach, the fuel gauge and the temperature gauge
	std::array<pObject, GAUGES>				m_gaugeNeedles = {};
	std::array<float, GAUGES>				m_gaugeReads = {};		// where each needle is, 0 to 1 of its sweep
	// Swings the headlights and turns the gauges' needles, each frame.
	void PoseCarInstruments(const Workshop& workshop, float deltaTime);
	void ToggleHeadlights(const Workshop& workshop);
	// Which of the car's seats the player is in (0 the driver's, 1 the front passenger's, 2 and 3 the back seat's two
	// places), or -1.
	static constexpr int SEATS = 4;
	int										m_seat = -1;
	bool									m_seatJustIn = false;	// got in this frame: the player is turned to face forward
	float									m_seatLastYaw = 0.f;	// the car's, last frame: whoever's in it is turned by what it's turned since
	float									m_bestLap = 0.f;		// seconds round the highway, the best yet (0: none)
	// The seat [F] would put the player in, aimed at the car from where they stand.
	std::optional<int> AimedSeat(const Workshop& workshop, const FirstPersonController& player) const;
	void Sit(int seat, Workshop& workshop);
	void LeaveSeat(FirstPersonController* player);
	std::string SeatPrompt() const;
	struct DriveState
	{
		bool active = false;			// the engine's running, the driver at the wheel
		float speed = 0.f;				// m/s along the car's nose (backward when negative)
		float steer = 0.f;				// the front wheels' angle, radians, left positive
		float spin = 0.f;				// how far the wheels have turned, radians
		float rpm = 0.f;
		int gear = 1;					// 0 reverse
		float throttle = 0.f;
		float topSpeed = 40.f;			// m/s
		float pull = 3.f;				// m/s2 off the line
		float braking = 8.f;			// m/s2
		float grip = 7.f;				// m/s2 sideways
		float idleRpm = 850.f;
		float bumped = 0.f;				// seconds since it last hit something, so it doesn't clatter
		bool offRoad = false;			// in the sand
		// The lap being timed: which way the line was last crossed (0: not yet), how long ago, and the lap before.
		int lapWay = 0;
		float lap = 0.f;
		float lastLap = 0.f;
		float devSeconds = 0.f;
		float devSteer = 0.f;
	};
	DriveState								m_drive = {};
	// The key turned (from the driver's seat), and the engine stopped.
	bool StartDrive(Workshop& workshop);
	void EndDrive();
	// A frame of driving: the pedals and the wheel, and the car moved.
	void MoveCar(float deltaTime, Workshop& workshop, const InputManager& input, bool playerHasControl);
	// Whether the car, standing there turned that way, would be in something solid.
	bool CarBlocked(const glm::vec3& position, float yaw) const;
	// Whether there's something hard to drive on there (the lot's concrete or a road), not sand.
	bool OnPaving(glm::vec2 point) const;
	// The map and the lap, and from outside the car its gauges.
	void DrawDriveHud(const Workshop& workshop) const;
	bool											m_bayDoorOpen = false;
	float											m_bayDoorAmount = 0.f;
	std::unordered_map<ItemRef, ItemVisual, ItemRefHash>	m_items = {};
	std::unordered_map<uint32_t, Package>			m_packages = {};
	std::unordered_map<const Object*, Target>		m_targets = {};
	PlayerInventory									m_inventory = {};
	ItemRef											m_equipped = {};
	// How the item in hand has been turned with the turn-held key, to be put down that way: spun about its own up,
	// then tipped toward or away from the player (who faces the way it goes down). With Shift they go in steps; the raw
	// angles are where the mouse has got to. Reset whenever another item is taken in hand.
	struct HeldTurn
	{
		ItemRef item = {};
		bool turned = false;
		float spin = 0.f;
		float tip = 0.f;
		float rawSpin = 0.f;
		float rawTip = 0.f;
		// How it sat in the hand when the turning started (relative to the way the player faces), so the turn carries on
		// from there rather than jumping to it lying flat.
		glm::quat base = glm::quat(1.f, 0.f, 0.f, 0.f);
		glm::quat Tilt() const { return glm::angleAxis(tip, glm::vec3(1.f, 0.f, 0.f)) * glm::angleAxis(spin, glm::vec3(0.f, 1.f, 0.f)) * base; }
	};
	HeldTurn										m_heldTurn = {};
	void TurnHeld(const InputManager& input, const FirstPersonController& player);
	// The way the item in hand would lie if put down now: turned by the player, or flat facing them.
	void ApplyHeldTurn(ItemVisual& held, const FirstPersonController& player);
	bool											m_initialSyncDone = false;
	uint32_t										m_nextPackageId = 1;
	uint32_t										m_deliveries = 0;

	// Per engine stand
	std::array<float, Workshop::MAX_ENGINES>		m_crankAngles = {};
	// Where the crank is as far as the cam knows: it follows the crank only with the timing belt on, so without it the cam and
	// valves stay put as the crank turns (see TurnCrank). Not saved: it's taken from the crank on loading.
	std::array<float, Workshop::MAX_ENGINES>		m_camCranks = {};
	std::array<float, Workshop::MAX_ENGINES>		m_engineRolls = {};	// each stand's rotation about the crank axis
	std::pair<int, float>							m_devSpin = { 0, 0.f };
	// Where each engine was when its parts were last laid out, and whether it has moved since.
	struct StandLayout
	{
		glm::vec3 position = {};
		glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
		float crank = 0.f;
		std::set<int> unseated = {};
		std::map<int, bool> ringsInBore = {};
	};
	std::array<StandLayout, Workshop::MAX_ENGINES>	m_laidOutStands = {};
	// The workshop's pistons in the top of their bores (Workshop::UnseatedPistons), kept here for posing them.
	std::array<std::set<int>, Workshop::MAX_ENGINES>	m_unseated = {};
	// And its rings set in bores on their own to be gapped, and whether each is squared (Workshop::RingsInBore).
	std::array<std::map<int, bool>, Workshop::MAX_ENGINES>	m_ringsInBore = {};
	// One per tool in the catalogue: the model's position and turn on the board (see LayOutToolBoard).
	// Hung upright, its turned bounds' middle (across and up the board), half its size each way, and how far out from
	// the board its origin sits so its back is just clear of the face.
	struct ToolHook
	{
		glm::vec3 position = {};
		glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
		glm::vec2 middle = {};
		glm::vec2 half = {};
		float out = 0.f;
	};
	std::vector<ToolHook>							m_toolHooks = {};
	// Where each tool hung by hand ([RMB] aimed at the board) went, by its id: the middle of it, in the board's space
	// (across and up). One not listed goes on its hook from LayOutToolBoard.
	std::map<size_t, glm::vec2>						m_toolSpots = {};
	std::array<bool, Workshop::MAX_ENGINES>			m_standMoved = {};
	// Whether the engine's parts have moved within it this frame (the crank turned, a piston pushed home, shaking on the
	// dyno), not just the engine as a whole; and where each engine is this frame.
	std::array<bool, Workshop::MAX_ENGINES>			m_standPartsMoved = {};
	std::array<EnginePose, Workshop::MAX_ENGINES>	m_standPoses = {};
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
	std::optional<std::pair<uint32_t, int>>			m_aimedBore = std::nullopt;	// a loose block's id, and the bore aimed into
	struct HeadSpot
	{
		uint32_t head = 0;
		AssemblySlot slot = AssemblySlot::COUNT;
		int index = 0;
	};
	std::optional<HeadSpot>							m_aimedHeadSpot = std::nullopt;	// where in a loose head the part in hand would go
	std::optional<uint32_t>							m_aimedHeadPart = std::nullopt;	// the part in a loose head that R would take out
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
	// The bore of a loose block aimed at with a ring or piston in hand, where it goes (marked like m_fitTarget).
	std::optional<glm::vec3>						m_boreFitTarget = std::nullopt;
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
	// The same on the project car.
	struct CarFitTarget
	{
		CarSlot slot = CarSlot::BODY_SHELL;
		int index = 0;
		glm::vec3 position = {};
		bool ready = false;
	};
	std::optional<CarFitTarget>						m_carFitTarget = std::nullopt;
	// The car's tools: what the funnel pours into each circuit (a FluidProduct, or -1 for what the manual says), how
	// long the button's been held on what (for the spring compressor, tire irons and draining), and the pause after
	// each go with the bleeder or a click of the distributor.
	std::array<int, FLUID_CIRCUITS>					m_funnelProduct = { -1, -1, -1, -1, -1, -1, -1 };
	float											m_carHold = 0.f;
	uint64_t										m_carHoldOn = 0;
	float											m_carToolPause = 0.f;
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
	// Nm: what each torque wrench is set to, with the wheel while it's in hand. One not listed is at its lowest.
	std::map<size_t, float>							m_torqueSettings = {};
	// Battery packs, by tool index. One not listed is full and on nothing.
	std::map<size_t, BatteryPack>					m_batteries = {};
	// Holding the button on a nut or bolt: which, how far through
	// (0 to 1), and when the tool is next heard. Once it's done the button has to be let go before the
	// next, so a bolt just done up isn't straight away undone.
	struct Turning
	{
		FastenerTarget fastener = {};
		bool tightening = false;
		std::vector<int> indices = {};	// the one being turned
		float progress = 0.f;
		float soundTimer = 0.f;
	};
	std::optional<Turning>							m_turning = std::nullopt;
	// Nuts and bolts left turned part way (by part id): how far done up, 0 finger tight to 1 done up.
	std::unordered_map<uint32_t, float>				m_fastenerTurns = {};
	bool											m_turnDone = false;
	float											m_pressProgress = 0.f;	// the pin press, as far as it went on m_pressPiston (0 to 1)
	uint32_t									m_pressPiston = 0;
	HandToolKind									m_pressTool = HandToolKind::NONE;
	int											m_pressDirection = 0;	// 1 in, -1 out
	bool											m_pressAimed = false;	// at a piston now: the ring shows how far
	bool											m_pressDone = false;
	bool											m_supportChanged = true;	// something was lifted, removed or moved
	bool											m_recheckSupport = true;	// this frame, resting things check what's under them
	// While the bodies are stepped: everything that can be stood on, with its box, gathered once for
	// the frame rather than once per body (hundreds of loose bolts made that a stall).
	mutable std::vector<std::pair<const Object*, ShopBox>>	m_supportBoxes = {};
	mutable bool									m_supportBoxesReady = false;
	bool											m_steppingBodies = false;
	bool											m_wasMoving = false;		// carrying equipment or a delivery last frame
	uint32_t										m_carriedPackage = 0;		// the delivery in your arms (its hotbar slot is picked), or 0
	// In a game over the network: where each other player's hand is (what they hold is drawn there), and
	// the deliveries they're carrying (moved by them, not by physics here). Kept by NetSession.
	std::map<int, std::pair<glm::vec3, float>>		m_remoteHands = {};		// player: hand point and yaw
	std::unordered_map<uint32_t, int>				m_remoteCarriedPackages = {};	// package: player
	std::unordered_set<uint32_t>					m_remoteStowedPackages = {};	// carried by someone else, but on their hotbar, not in hand
	bool											m_handEmpty = false;		// the selected slot's item put away with its number
	int												m_engineOnDyno = -1;		// the Workshop's, kept here for posing it
	int												m_engineOnHoist = -1;		// the same, for the hoist
	int												m_engineInCar = -1;			// and for the project car's bay
	// An engine going up onto the hoist or down off it: from where it was, over HOIST_SWING_SECONDS.
	struct EngineSwing
	{
		int engine = -1;
		EnginePose from = {};
		float progress = 1.f;
	};
	EngineSwing									m_swing = {};
	Readout											m_readout = {};
	std::string										m_prompt = "";
	std::string										m_secondaryPrompt = "";
	std::string										m_heldHint = "";	// what the item in hand can do, by the hotbar
	std::string										m_toast = "";
	float											m_toastTime = 0.f;
	size_t											m_seenLogSize = 0;
};
