#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "LV/Engine/Graphics/Models/Model.h"
#include "../Simulation/EngineSpec.h"
#include "../Simulation/Machine.h"
#include "../Simulation/MeasuringTool.h"
#include "../Simulation/Part.h"

class MeshBuilder;

// Where things sit inside an assembled LV-20, in metres, relative to the centre of the block deck.
// The engine's crank axis runs along X.
namespace EngineLayout
{
	constexpr float BORE_SPACING = 0.095f;
	constexpr float BORE_RADIUS = 0.043f;
	constexpr float BLOCK_LENGTH = 4 * BORE_SPACING + 0.03f;
	constexpr float BLOCK_WIDTH = 0.20f;
	constexpr float CYLINDER_DEPTH = 0.16f;
	constexpr float CRANK_Y = -0.25f;
	constexpr float CRANK_THROW = 0.043f;
	constexpr float ROD_LENGTH = 0.144f;
	constexpr float PISTON_PIN_DEPTH = 0.03f;	// crown to pin centre
	constexpr float GASKET_THICKNESS = 0.0015f;

	// Head, measured from its deck face.
	constexpr float HEAD_DECK_Y = GASKET_THICKNESS + 0.001f;	// head deck above the block deck
	constexpr float HEAD_FLOOR_Y = 0.07f;		// spring seats
	constexpr float HEAD_RAIL_TOP = 0.13f;
	constexpr float CAM_Y = 0.13f;
	constexpr float CAM_BASE_RADIUS = 0.016f;
	constexpr float VALVE_Z = 0.022f;			// intake on +Z, exhaust on -Z
	constexpr float VALVE_FACE_Y = 0.006f;		// valve face above the head deck when shut
	constexpr float VALVE_STEM_LENGTH = 0.105f;
	constexpr float SPRING_FREE_HEIGHT = 0.044f;
	constexpr float SPRING_INSTALLED_HEIGHT = 0.038f;
	constexpr float MAX_VALVE_LIFT = 0.0088f;

	inline float BoreX(int cylinder) { return (cylinder - 1.5f) * BORE_SPACING; }
	inline float MainX(int journal) { return (journal - 2.f) * BORE_SPACING; }
	inline float ValveZ(bool exhaust) { return exhaust ? -VALVE_Z : VALVE_Z; }
	// Firing order 1-3-4-2 puts throws 1 and 4 up together, 2 and 3 down.
	inline float ThrowSign(int cylinder) { return cylinder == 0 || cylinder == 3 ? 1.f : -1.f; }
	inline float PinY(int cylinder) { return CRANK_Y + ThrowSign(cylinder) * CRANK_THROW; }
	inline float PistonCrownY(int cylinder) { return PinY(cylinder) + ROD_LENGTH + PISTON_PIN_DEPTH; }

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
}

namespace ShopTextures
{
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
	inline const std::string GRASS = "Grass";
	inline const std::string ASPHALT = "Asphalt";
	inline const std::string SIDING = "Metal Siding";
	inline const std::string BRICK = "Brick";
	inline const std::string SHINGLES = "Shingles";
	inline const std::string BARK = "Bark";
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
	inline const std::string GARAGE_FRAMES = "Garage Frames";
	inline const std::string WINDOW_GLASS = "Window Glass";
	inline const std::string ROLLUP_DOOR = "Roll-up Door";

	// Outdoors
	inline const std::string SKY_DOME = "Sky Dome";
	inline const std::string GROUND = "Ground";
	inline const std::string PAVING = "Paving";
	inline const std::string ROAD = "Road";
	inline const std::string ROAD_MARKINGS = "Road Markings";
	inline const std::string FENCE = "Fence";
	inline const std::string TREE_TRUNK = "Tree Trunk";
	inline const std::string TREE_CROWN = "Tree Crown";
	inline const std::string PINE_CROWN = "Pine Crown";
	inline const std::string HOUSE_WALLS = "House Walls";
	inline const std::string HOUSE_ROOF = "House Roof";
	inline const std::string HOUSE_TRIM = "House Trim";
	inline const std::string STREET_LIGHT = "Street Light";
	inline const std::string LAMP_HEAD = "Lamp Head";
	inline const std::string CAR_BODY = "Car Body";
	inline const std::string CAR_TRIM = "Car Trim";
	inline const std::string DUMPSTER = "Dumpster";
	inline const std::string OIL_DRUM = "Oil Drum";
	inline const std::string WORKBENCH = "Workbench";
	inline const std::string SHELF = "Parts Shelf";
	inline const std::string PALLET_RACK = "Pallet Rack";
	inline const std::string TOOL_BOARD = "Tool Board";
	inline const std::string TOOL_BOARD_TOOLS = "Tool Board Tools";
	inline const std::string ENGINE_STAND = "Engine Stand";
	inline const std::string DYNO = "Dyno";
	inline const std::string COMPUTER_DESK = "Computer Desk";
	inline const std::string PACKAGE_BOX = "Package: Box";
	inline const std::string PACKAGE_CRATE = "Package: Crate";
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
	static std::vector<SurfaceOverlay> OverlaysFor(PartType type);
	static std::vector<FeatureAnchor> FeatureAnchorsFor(PartType type);
	static std::vector<AssemblyAnchor> AssemblyAnchors(const EngineSpec& spec);
	// Backpack cells, width x height; {0, 0} for parts too big to carry in it.
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
	};

	void LoadOverrides();
	void AddModel(const std::string& name, const MeshBuilder& builder);
	void LoadTextures();
	void LoadEnvironment();
	void LoadOutdoors();
	void LoadMachines();
	void LoadParts();
	void LoadOverlays();
	void LoadTools();

	std::unordered_map<std::string, uint32_t>		m_models = {};
	std::unordered_map<std::string, uint32_t>		m_textures = {};
	std::unordered_map<std::string, ModelOverride>	m_overrides = {};
	std::unordered_map<std::string, uint32_t>		m_modelTextures = {};
};
