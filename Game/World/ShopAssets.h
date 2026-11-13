#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "LV/Engine/Graphics/Models/Model.h"
#include "../Simulation/EngineSpec.h"
#include "../Simulation/Furniture.h"
#include "../Simulation/Machine.h"
#include "../Simulation/MeasuringTool.h"
#include "../Simulation/Part.h"

class MeshBuilder;

// Where things sit inside an assembled LV-16, in metres, relative to the centre of the block deck.
// The engine's crank axis runs along X.
namespace EngineLayout
{
	constexpr float BORE_SPACING = 0.095f;
	constexpr float BORE_RADIUS = 0.0405f;
	constexpr float BLOCK_LENGTH = 4 * BORE_SPACING + 0.03f;
	constexpr float BLOCK_WIDTH = 0.18f;		// across the deck
	constexpr float CRANKCASE_WIDTH = 0.20f;	// across the skirts
	constexpr float CYLINDER_DEPTH = 0.16f;
	constexpr float CRANK_Y = -0.25f;
	constexpr float CRANK_THROW = 0.0385f;
	constexpr float ROD_LENGTH = 0.14825f;
	constexpr float PISTON_PIN_DEPTH = 0.03f;	// crown to pin centre
	constexpr float GASKET_THICKNESS = 0.0015f;
	constexpr float GASKET_LIP = 0.005f;			// how far the gasket stands out past the block deck all round

	// Head, measured from its deck face.
	constexpr float HEAD_DECK_Y = GASKET_THICKNESS + 0.001f;	// head deck above the block deck
	constexpr float HEAD_FLOOR_Y = 0.07f;		// spring seats
	constexpr float HEAD_RAIL_TOP = 0.13f;
	constexpr float CAM_Y = 0.13f;
	constexpr float CAM_BASE_RADIUS = 0.016f;
	constexpr float VALVE_Z = 0.022f;			// intake on -Z, exhaust on +Z
	constexpr float VALVE_FACE_Y = 0.006f;		// valve face above the head deck when shut
	constexpr float VALVE_STEM_LENGTH = 0.105f;
	constexpr float SPRING_FREE_HEIGHT = 0.044f;
	constexpr float SPRING_INSTALLED_HEIGHT = 0.038f;
	constexpr float MAX_VALVE_LIFT = 0.0088f;
	// A rocker arm's pivot on its shaft, from the valve tip it pushes on: up a little and out to the side.
	constexpr float ROCKER_PIVOT_Y = 0.006f;
	constexpr float ROCKER_PIVOT_Z = 0.025f;

	inline float BoreX(int cylinder) { return (cylinder - 1.5f) * BORE_SPACING; }
	inline float MainX(int journal) { return (journal - 2.f) * BORE_SPACING; }
	// The cam runs in four journals, as the 4A-C's does: one at each end, and one between the lobes of each of
	// the middle two cylinders.
	constexpr int CAM_JOURNALS = 4;
	inline float CamJournalX(int journal) { return journal == 0 ? MainX(0) : journal == CAM_JOURNALS - 1 ? MainX(4) : BoreX(journal); }
	// The head's cam towers (and the caps on them) sit under the journals, pulled in at the ends to stay on
	// the head.
	inline float CamTowerX(int journal) { return glm::clamp(CamJournalX(journal), -BLOCK_LENGTH * 0.5f + 0.02f, BLOCK_LENGTH * 0.5f - 0.02f); }
	// The rocker shafts' five bolts, through both shafts: one at each end and one between each pair of cylinders.
	inline float RockerBoltX(int hole) { return glm::clamp(MainX(hole), -BLOCK_LENGTH * 0.5f + 0.035f, BLOCK_LENGTH * 0.5f - 0.035f); }
	inline float ValveZ(bool exhaust) { return exhaust ? VALVE_Z : -VALVE_Z; }
	// The spark plugs, as the 4A-C has them: the cam runs down the middle of the head, so each plug comes in from the side
	// (PLUG_SIDE: +1 the exhaust side, -1 the intake), down a tube from just under the valve cover's rail, angled into the
	// chamber beside the valves. In the head's own space (its deck at Y = 0): the firing tip, the line up the plug's
	// axis, and how far along it its seat and terminal are.
	constexpr float PLUG_SIDE = 1.f;
	constexpr float PLUG_ALONG = 0.016f;		// along the crank from the bore's centre: in close, between the valves' heads
	constexpr float PLUG_TILT = 0.873f;			// radians from upright (50 degrees), leaning out to the side
	constexpr float PLUG_SEAT = 0.019f, PLUG_TERMINAL = 0.078f;
	inline glm::vec3 PlugTip(int cylinder) { return { BoreX(cylinder) + PLUG_ALONG, 0.006f, PLUG_SIDE * 0.004f }; }
	constexpr float PLUG_TUBE = 0.0125f;		// the tube's outer radius (its bore, 0.0095)
	inline glm::vec3 PlugAxis() { return { 0.f, std::cos(PLUG_TILT), PLUG_SIDE * std::sin(PLUG_TILT) }; }
	inline glm::quat PlugTurn() { return glm::angleAxis(PLUG_SIDE * PLUG_TILT, glm::vec3(1.f, 0.f, 0.f)); }
	// Firing order 1-3-4-2 puts throws 1 and 4 up together, 2 and 3 down.
	inline float ThrowSign(int cylinder) { return cylinder == 0 || cylinder == 3 ? 1.f : -1.f; }
	inline float PinY(int cylinder) { return CRANK_Y + ThrowSign(cylinder) * CRANK_THROW; }

	// Crank angle at which each cylinder is at top dead centre on its firing stroke (1-3-4-2).
	inline float FiringAngle(int cylinder)
	{
		constexpr float pi = glm::pi<float>();
		constexpr float angles[] = { 0.f, 3.f * pi, pi, 2.f * pi };
		return angles[cylinder];
	}
	// Peak lift sits about 110 degrees either side of the overlap TDC.
	inline float LobePeakCrankAngle(int cylinder, bool exhaust)
	{
		return FiringAngle(cylinder) + glm::two_pi<float>() + (exhaust ? -1.92f : 1.92f);
	}
	// Where a lobe's nose points around the cam axis (0 = up) when the crank is at zero, such that
	// it points straight down at the valve at its peak crank angle. The cam turns at half speed.
	inline float LobeAngle(int cylinder, bool exhaust)
	{
		return glm::pi<float>() - LobePeakCrankAngle(cylinder, exhaust) * 0.5f;
	}
	float ValveLift(int cylinder, bool exhaust, float crankAngle);

	// Bolt-on parts. The timing end of the engine is -X, the intake side -Z.
	constexpr float FRONT_X = -BLOCK_LENGTH * 0.5f;
	// The skirts come down past the crank's centre line (a deep-skirt block); the pan rail is their bottom face.
	constexpr float SKIRT_DEPTH = 0.045f;
	constexpr float PAN_RAIL_Y = CRANK_Y - SKIRT_DEPTH;				// the block's bottom face
	constexpr float HEAD_TOP_Y = HEAD_DECK_Y + HEAD_RAIL_TOP;		// where the valve cover sits
	constexpr float OIL_PAN_DEPTH = 0.095f;							// down to the pickup, under the skirts
	constexpr float OIL_PAN_LIP = 0.022f;							// the pan's rail, out past its walls, for the bolts
	constexpr float PORT_Y = 0.035f;								// intake and exhaust port centres
	constexpr float PORT_FACE_Z = 0.1055f;							// the head's port faces, either side
	// The manifolds' head flanges (in their own space, out along +Z for the intake, -Z for the exhaust): how thick, and
	// the eight nuts on them, between the ports, alternately low and high, the first low (a high one at the front end would
	// foul the thermostat housing on the intake side). X along the engine, Y up.
	constexpr float MANIFOLD_FLANGE = 0.012f;
	constexpr float MANIFOLD_NUT_Y = 0.019f;
	inline glm::vec2 ManifoldNut(int nut)
	{
		float x = (static_cast<float>(nut / 2) - 1.5f) * BORE_SPACING + (nut % 2 == 0 ? -0.5f : 0.5f) * BORE_SPACING;
		return { x, nut % 2 == 0 ? -MANIFOLD_NUT_Y : MANIFOLD_NUT_Y };
	}
	// The exhaust manifold's outlet, in its space (the downpipe's flange meets it here), and its three bolts round it.
	constexpr glm::vec3 EXHAUST_OUTLET = { 0.06f, -0.12f, -0.09f };
	constexpr float EXHAUST_BOLT_RING = 0.03f;
	constexpr float TIMING_PLANE_X = FRONT_X - 0.023f;				// sprockets and belt
	constexpr float TIMING_COVER_X = FRONT_X - 0.035f;
	constexpr float TIMING_COVER_Y = -0.06f;						// the middle of the cover
	// In the covers' own space: the top of the valve cover's plate, and the timing cover's face (lying
	// face up), where their bolt heads sit.
	constexpr float VALVE_COVER_TOP = 0.055f;
	constexpr float TIMING_COVER_FACE = 0.012f;
	constexpr float TIMING_COVER_HALF_WIDTH = 0.105f;				// its body, either side of the crank
	// Along the cam, towards the back, between the last cylinder's lobes and the rear journal: the fuel pump's eccentric,
	// and the pump on the side of the head beside it.
	constexpr float FUEL_PUMP_X = 0.166f;
	// How far below the cam's axis the pump sits. The axis is level with the head's top rail (where the valve cover meets
	// it), so the pump, its gasket and its hole sit wholly on the head below that joint, its arm reaching up to the eccentric.
	constexpr float FUEL_PUMP_DROP = 0.026f;
	// The thermostat housing's two bolts, on opposite corners of its flange (in its own space, X and Z; the other one at
	// minus both): clear of its round body and of the outlet's neck running off along +X.
	constexpr glm::vec2 THERMOSTAT_BOLT = { -0.024f, -0.024f };
	// The timing belt (as the 4A-C has it, seen from the front): round the cam pulley at the top and the
	// crank pulley, half its size, below it; the spring-loaded idler pulley leans on the back of the slack
	// side, on the left, nearer the crank.
	constexpr float CRANK_PULLEY_TEETH = 0.0245f;		// the timing pulleys' tooth tips, where the belt rides
	constexpr float CAM_PULLEY_TEETH = 0.0465f;
	constexpr float TIMING_IDLER_Y = -0.15f;
	constexpr float TIMING_IDLER_Z = -0.032f;
	constexpr float TIMING_IDLER_RADIUS = 0.017f;
	// The fan belt, a V-belt in one plane in front of the timing cover: round the crank pulley, up round
	// the water pump (above the crank, out on the left), across to the alternator (out to the right,
	// level with it) and back down. The alternator's pivot sets how tight it is.
	constexpr float ACCESSORY_BELT_X = TIMING_COVER_X - 0.06f;
	constexpr float ALTERNATOR_Y = 0.0f;
	constexpr float ALTERNATOR_Z = 0.17f;
	// In the alternator's own space (origin at its pulley, its body back along +X): the pivot, through the two lugs under
	// it and the bracket's boss between them, and the adjusting lug on top, against the bracket's slotted strap.
	constexpr float ALTERNATOR_PIVOT_X = 0.05f, ALTERNATOR_PIVOT_Y = -0.062f;
	constexpr float ALTERNATOR_ADJUST_X = 0.078f, ALTERNATOR_ADJUST_Y = 0.064f;		// the adjusting lug's front face
	// The bracket's origin, at the pivot (the bracket is built in the engine's turn, about it).
	constexpr float ALTERNATOR_BRACKET_X = ACCESSORY_BELT_X + ALTERNATOR_PIVOT_X;
	constexpr float ALTERNATOR_BRACKET_Y = ALTERNATOR_Y + ALTERNATOR_PIVOT_Y;
	// Its three bolts into the side of the block (back from the front of the block, and up), heads out on its plate.
	constexpr glm::vec2 ALTERNATOR_BRACKET_BOLTS[] = { { 0.038f, -0.045f }, { 0.06f, -0.058f }, { 0.05f, -0.097f } };
	// The water pump sits out on the left, clear of the timing belt and its idler: its nose goes in through an ear on
	// the side of the timing cover (sealed off from the belt) into a coolant boss cast on the block's front corner.
	constexpr float WATER_PUMP_Y = -0.085f;
	constexpr float WATER_PUMP_Z = -0.105f;
	constexpr float WATER_PUMP_NOSE = 0.026f;						// the nose's radius
	constexpr float WATER_PUMP_EAR = 0.142f;						// how far out the cover's ear (and the boss) reach
	constexpr float WATER_PUMP_EAR_HALF = 0.037f;					// the ear's half-height, round the pump's middle
	constexpr float WATER_PUMP_PAD = 0.0015f;						// the machined pad on the ear's face, round the hole
	constexpr float WATER_PUMP_GASKET_X = TIMING_COVER_X - TIMING_COVER_FACE - WATER_PUMP_PAD;	// on the pad
	constexpr float WATER_PUMP_X = WATER_PUMP_GASKET_X - 0.0015f;	// the back of the pump's flange, on its gasket
	constexpr float WATER_PUMP_BOLT_RING = 0.029f;					// its four bolts, on the diagonals
	// A retired part's (the serpentine belt tensioner): only its model uses them now.
	constexpr float DRIVE_TENSIONER_Y = -0.06f;
	constexpr float DRIVE_TENSIONER_Z = -0.07f;
	constexpr float DRIVE_TENSIONER_RADIUS = 0.03f;
	// A belt's pulleys, seen from the front of the engine (centres in Z to the right and Y up), in the
	// order the belt goes round them (clockwise, the way the engine turns): wrap +1 on the belt's working
	// side, -1 on its back.
	struct BeltPulley
	{
		const char* name = "";
		glm::vec2 centre = {};
		float radius = 0.f;
		float wrap = 1.f;
	};
	const std::vector<BeltPulley>& TimingPulleys();
	const std::vector<BeltPulley>& DrivePulleys();
	// A belt all the way round, as points along it (Z, Y): each straight run tangent to the pulleys at its
	// ends, and round each pulley between them, offset metres out from it.
	std::vector<glm::vec2> BeltPath(const std::vector<BeltPulley>& pulleys, float offset);
	// The middle of the run from pulley run to the next, where a belt's deflection is checked (Z, Y): the
	// timing belt's tight side, cam to crank; the fan belt's top run, water pump to alternator.
	glm::vec2 BeltRunMiddle(const std::vector<BeltPulley>& pulleys, size_t run);
	constexpr size_t TIMING_CHECK_RUN = 0;
	constexpr size_t DRIVE_CHECK_RUN = 1;
	inline float CamAxisY() { return HEAD_DECK_Y + CAM_Y; }

	// Where a bolt-on part's model sits on the assembled engine (its origin, in engine space) and
	// how it's turned. The models are built around their own origin, lying the way they'd rest on
	// a bench; this stands them up in place. Moving parts (sprockets, pulley, valvetrain) get
	// their motion on top of this.
	struct Mount
	{
		glm::vec3 offset = {};
		glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
	};
	Mount AncillaryMount(PartType type, int index);
}

namespace ShopTextures
{
	// Paint on the walls and floor goes on tiles (see WallPaint), each with a texture of its own out of this
	// pool, made clear and painted on as the game goes.
	constexpr int PAINT_TILE_POOL = 1024;
	constexpr int PAINT_TILE_PIXELS = 128;
	inline std::string PaintTile(int slot) { return "Paint Tile " + std::to_string(slot); }
	inline const std::string CONCRETE = "Concrete";
	inline const std::string CAST_IRON = "Cast Iron";
	inline const std::string ALUMINIUM = "Aluminium";
	inline const std::string STEEL = "Brushed Steel";
	inline const std::string WOOD = "Wood";
	inline const std::string PAINT = "Painted Metal";
	inline const std::string PLASTER = "Plaster";
	inline const std::string PEGBOARD = "Pegboard";
	inline const std::string RUBBER = "Rubber";
	inline const std::string CARDBOARD = "Cardboard";
	inline const std::string SAND = "Sand";
	inline const std::string ASPHALT = "Asphalt";
	inline const std::string SIDING = "Metal Siding";
	inline const std::string PLANKS = "Planks";
	inline const std::string ROCK = "Rock";
	inline const std::string FOLIAGE = "Foliage";

	inline const std::string HONED = "Finish: Honed";
	inline const std::string BORED = "Finish: Bored";
	inline const std::string GLAZED = "Finish: Glazed";
	inline const std::string WORN = "Finish: Worn";
	inline const std::string SCORED = "Finish: Scored";
	inline const std::string GROUND = "Finish: Ground";
	inline const std::string POLISHED = "Finish: Polished";
	inline const std::string SURFACE_GROUND = "Finish: Surface Ground";
}

namespace ShopModels
{
	inline const std::string FLOOR = "Shop Floor";
	inline const std::string WALLS = "Shop Walls";
	inline const std::string LAMP = "Ceiling Lamp";

	// The garage building
	inline const std::string GARAGE_SIDING = "Garage Siding";
	inline const std::string GARAGE_ROOF = "Garage Roof";
	// The garage walls, siding, frames and glass come in versions for the upgrades: with the opening
	// into the side bay, and without a roof of their own under the second story.
	inline std::string GarageVariant(const std::string& base, bool bay, bool upper)
	{
		return base + (bay ? " (bay)" : "") + (upper ? " (upper)" : "");
	}
	inline const std::string BAY_SHELL = "Side Bay Shell";
	inline const std::string BAY_EXTERIOR = "Side Bay Exterior";
	inline const std::string BAY_FLOOR = "Side Bay Floor";
	inline const std::string BAY_DRIVE = "Side Bay Drive";
	inline const std::string BAY_SAND = "Side Bay Sand";	// where the bay's floor and drive go, till it's built
	inline const std::string UPPER_SHELL = "Upper Story Shell";
	inline const std::string UPPER_EXTERIOR = "Upper Story Exterior";
	inline const std::string UPPER_ROOF = "Upper Story Roof";
	inline const std::string UPPER_FRAMES = "Upper Story Frames";
	inline const std::string UPPER_GLASS = "Upper Story Glass";
	inline const std::string OUTSIDE_STAIRS = "Outside Stairs";
	inline const std::string INSIDE_STAIRS = "Inside Stairs";
	inline const std::string POST_LIFT = "Two-Post Lift";
	inline const std::string POST_LIFT_ARMS = "Two-Post Lift Arms";
	// A strip of Plastigauge laid across a journal.
	inline const std::string PLASTIGAUGE_STRIP = "Plastigauge Strip";
	inline const std::string GARAGE_FRAMES = "Garage Frames";
	inline const std::string WINDOW_GLASS = "Window Glass";
	inline const std::string ROLLUP_DOOR = "Roll-up Door";


	// Outdoors
	inline const std::string SKY_DOME = "Sky Dome";
	inline const std::string GROUND = "Ground";
	inline const std::string PAVING = "Paving";
	inline const std::string ROAD = "Road";
	inline const std::string ROAD_MARKINGS = "Road Markings";
	// The desert: saguaros and scrub scattered over the sand, and the mountains round the horizon.
	inline const std::string CACTUS = "Cactus";
	inline const std::string SCRUB = "Scrub";
	inline const std::string MOUNTAINS = "Mountains";
	inline const std::string STREET_LIGHT = "Street Light";
	inline const std::string LAMP_HEAD = "Lamp Head";
	inline const std::string DUMPSTER = "Dumpster";
	inline const std::string OIL_DRUM = "Oil Drum";
	inline const std::string WORKBENCH = "Workbench";
	inline const std::string SHELF = "Parts Shelf";
	inline const std::string PALLET_RACK = "Pallet Rack";
	inline const std::string TOOL_BOARD = "Tool Board";
	inline const std::string TOOL_BOARD_TOOLS = "Tool Board Tools";
	inline const std::string ENGINE_STAND = "Engine Stand";
	inline const std::string DYNO = "Dyno";
	inline const std::string ENGINE_HOIST = "Engine Hoist";
	inline const std::string PROJECT_CAR = "LE86 Project Car";
	inline const std::string CAR_JACK_STANDS = "Car Jack Stands";
	// The right-hand fender and door (the parts' own models are the left-hand ones: see LoadCarParts), and the fuel filler's flap.
	inline const std::string FENDER_RIGHT = "Front Fender (Right)";
	inline const std::string DOOR_RIGHT = "Door (Right)";
	inline const std::string FUEL_FLAP = "Fuel Flap";
	inline const std::string KNUCKLE_RIGHT = "Steering Knuckle Right";
	inline const std::string DOOR_GLASS = "Door Glass";
	inline const std::string DOOR_GLASS_RIGHT = "Door Glass Right";
	inline const std::string CAR_QUARTER_GLASS = "Car Quarter Glass";
	inline const std::string COMPUTER_DESK = "Computer Desk";
	inline const std::string PACKAGE_BOX = "Package: Box";
	inline const std::string PACKAGE_CRATE = "Package: Crate";
	// A paint tile: one metre square in X and Y, facing +Z, its texture across it once (scaled to the tile).
	inline const std::string PAINT_TILE = "Paint Tile";
	// A spark plug lead fitted to its cylinder (0-3): routed from the plug over the valve cover to its
	// tower on the distributor cap. Off the engine, a lead is the plain Spark Plug Lead.
	inline std::string PlugLeadFitted(int cylinder) { return "Spark Plug Lead, fitted " + std::to_string(cylinder + 1); }
	// Another player, in a game over the network: the torso, and the parts that move on it. The fur, the
	// bare skin and the cap are pieces of their own, each taking the player's colour for it (MonkeyLook).
	inline const std::string PLAYER = "Player";
	inline const std::string PLAYER_HEAD = "Player Head";
	inline const std::string PLAYER_BELLY = "Player Belly";
	inline const std::string PLAYER_TAIL = "Player Tail";
	inline const std::string PLAYER_FACE = "Player Face";
	// The eyes: whites, pupils and a glint, in their own colours (never tinted), on the face.
	inline const std::string PLAYER_EYES = "Player Eyes";
	inline const std::string PLAYER_CAP = "Player Cap";
	inline const std::string PLAYER_ARM = "Player Arm";
	inline const std::string PLAYER_HAND = "Player Hand";
	inline const std::string PLAYER_THIGH = "Player Thigh";
	inline const std::string PLAYER_SHIN = "Player Shin";
	inline const std::string PLAYER_FOOT = "Player Foot";
	// First person: the player's own sleeve (one metre long, from the shoulder at the origin down -Z:
	// stretched to reach the hand), right hand (the wrist at the origin, fingers to -Z), and body
	// (feet at the origin, facing -Z, no head: the camera is there).
	inline const std::string VIEW_SLEEVE = "View Sleeve";
	inline const std::string VIEW_HAND = "View Hand";
	// The hand's size: how far it reaches past the wrist, and how thick it is (what's carried sits on it).
	constexpr float VIEW_HAND_LENGTH = 0.09f;
	constexpr float VIEW_HAND_THICKNESS = 0.045f;
	inline const std::string VIEW_BODY = "View Body";
	inline const std::string VIEW_BODY_SKIN = "View Body Skin";
}

// Where a part rests on a machine, relative to the machine's origin, and how it's turned.
struct MachineFixture
{
	std::string model = "";
	glm::vec3 partOffset = {};
	float partYaw = 0.f;
	glm::vec2 tableSize = { 0.5f, 0.5f };	// the surface parts can be set on
};

// A thin skin over one machinable surface, re-textured to show its finish: honed crosshatch,
// fresh grinding, glazing, scoring. It follows its part around.
struct SurfaceOverlay
{
	std::string featureName = "";
	std::string model = "";
	glm::vec3 offset = {};	// in the part's own space
};

// A spot on a part where one of its features is measured, in the part's own space.
struct FeatureAnchor
{
	std::string featureName = "";
	glm::vec3 position = {};
};

// A spot on the assembled engine where an assembly check is taken, relative to the stand's mount.
struct AssemblyAnchor
{
	AssemblyCheck check = AssemblyCheck::NONE;
	int index = 0;
	bool rod = false;
	glm::vec3 position = {};
};

// Builds and registers every model and texture the shop uses. Must run inside
// Game::OnLoadAssets, before the renderer uploads geometry.
//
// Any model can be replaced with a real one: list it in Resources/Models/models.cfg
// (see Resources/Models/README.md) and that file is loaded instead of the built-in shape.
class ShopAssets
{
public:
	void Load();

	uint32_t Model(const std::string& name) const;
	uint32_t Texture(const std::string& name) const;
	// The texture to use with a model: its override from models.cfg if it has one, else fallback.
	uint32_t TextureFor(const std::string& modelName, const std::string& fallbackTexture) const;

	uint32_t PartModel(PartType type) const;
	uint32_t PartTexture(const Part& part) const;
	// Colour multiplier so different alloys read differently under the same texture.
	glm::vec4 PartTint(MaterialType material) const;

	static MachineFixture FixtureFor(const Machine& machine);
	// The model a piece of furniture is drawn with (shelving and the workbench share the shop's own).
	static std::string FurnitureModel(const FurnitureInfo& furniture);
	static std::vector<SurfaceOverlay> OverlaysFor(PartType type);
	// Built once per part type: the measuring tools look them up for every part in reach, every frame.
	static const std::vector<FeatureAnchor>& FeatureAnchorsFor(PartType type);
	static std::vector<FeatureAnchor> BuildFeatureAnchors(PartType type);
	static std::vector<AssemblyAnchor> AssemblyAnchors(const EngineSpec& spec);
	// Backpack cells, width x height; {0, 0} for parts that can't go in it.
	static glm::ivec2 PartGridSize(PartType type);
	// Which finish texture a surface should wear, given how it was last worked and how rough it is.
	// 0 means the part's own texture already looks right, so the overlay can hide.
	uint32_t SurfaceTexture(const Feature& feature) const;
private:
	struct ModelOverride
	{
		std::string file = "";
		ModelImportOptions options = {};
		std::string texture = "";
		// Fitted to this height (metres) and stood on its base, centred, if more than 0.
		float height = 0.f;
	};

	void LoadOverrides();
	void AddModel(const std::string& name, const MeshBuilder& builder);
	void LoadTextures();
	void LoadEnvironment();
	void LoadOutdoors();
	void LoadMachines();
	void LoadParts();
	// Seals, gaskets and everything bolted around the long block.
	void LoadAncillaries();
	// The project car's parts (stand-ins, till the car is modelled properly) and the car itself.
	void LoadCarParts();
	void BuildProjectCarShell();
	void LoadOverlays();
	void LoadTools();
	void LoadFurniture();

	std::unordered_map<std::string, uint32_t>		m_models = {};
	std::unordered_map<std::string, uint32_t>		m_textures = {};
	std::unordered_map<std::string, ModelOverride>	m_overrides = {};
	std::unordered_map<std::string, uint32_t>		m_modelTextures = {};
};
