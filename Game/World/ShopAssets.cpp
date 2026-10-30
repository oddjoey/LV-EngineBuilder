#include "ShopAssets.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "LV/Engine/Graphics/Models/MeshBuilder.h"
#include "LV/Engine/Graphics/Models/ModelManager.h"
#include "LV/Engine/Graphics/Models/TextureManager.h"
#include "../Simulation/EngineAssembly.h"
#include "../Simulation/PartCatalog.h"
#include "ProceduralTextures.h"
#include "ToolShapes.h"

namespace
{
	const std::string MODEL_FOLDER = "Resources/Models/";
}

float EngineLayout::ValveLift(int cylinder, bool exhaust, float crankAngle)
{
	// Angle between the lobe nose and straight down, wrapped to [-pi, pi].
	float delta = LobeAngle(cylinder, exhaust) + crankAngle * 0.5f - glm::pi<float>();
	delta = std::remainder(delta, glm::two_pi<float>());

	// About 110 degrees of cam rotation (220 at the crank) from opening to closing.
	constexpr float halfDuration = 0.96f;
	if (std::abs(delta) >= halfDuration)
		return 0.f;
	float shape = std::cos(delta / halfDuration * glm::half_pi<float>());
	return MAX_VALVE_LIFT * shape * shape;
}

namespace
{
	const glm::vec3 WHITE = { 1.f, 1.f, 1.f };
	const glm::vec3 DARK = { 0.18f, 0.18f, 0.19f };
	const glm::vec3 STEEL_GREY = { 0.55f, 0.56f, 0.58f };
	const glm::vec3 RUBBER_BLACK = { 0.10f, 0.10f, 0.10f };

	glm::mat4 At(const glm::vec3& position)
	{
		return glm::translate(glm::mat4(1.f), position);
	}

	// Primitives are built along Y; these turn them to run along X or Z.
	glm::mat4 AlongX(const glm::vec3& position)
	{
		return glm::rotate(At(position), glm::half_pi<float>(), { 0.f, 0.f, 1.f });
	}

	glm::mat4 AlongZ(const glm::vec3& position)
	{
		return glm::rotate(At(position), glm::half_pi<float>(), { 1.f, 0.f, 0.f });
	}

	void Box(MeshBuilder& builder, const glm::vec3& center, const glm::vec3& size, const glm::vec3& color = WHITE)
	{
		builder.ResetTransform().SetColor(color).AddBox(center, size);
	}

	void Cylinder(MeshBuilder& builder, const glm::mat4& transform, float radius, float length, const glm::vec3& color = WHITE, int segments = 20)
	{
		builder.SetTransform(transform).SetColor(color).AddCylinder({}, radius, length, segments);
		builder.ResetTransform();
	}

	void Tube(MeshBuilder& builder, const glm::mat4& transform, float outer, float inner, float length, const glm::vec3& color = WHITE,
		float arcStart = 0.f, float arcEnd = glm::two_pi<float>(), int segments = 24)
	{
		builder.SetTransform(transform).SetColor(color).AddTube({}, outer, inner, length, segments, arcStart, arcEnd);
		builder.ResetTransform();
	}

	// A plain work stand or cabinet that a machine sits on, top surface at `height`.
	void Stand(MeshBuilder& builder, const glm::vec2& footprint, float height, const glm::vec3& color)
	{
		Box(builder, { 0.f, height * 0.5f, 0.f }, { footprint.x, height, footprint.y }, color);
		Box(builder, { -footprint.x * 0.5f - 0.002f, height * 0.55f, 0.f }, { 0.004f, height * 0.6f, footprint.y * 0.8f }, color * 0.8f);
	}
}

void ShopAssets::Load()
{
	LoadOverrides();
	LoadTextures();
	LoadEnvironment();
	LoadOutdoors();
	LoadMachines();
	LoadParts();
	LoadOverlays();
	LoadTools();
	LoadFurniture();
}

const std::vector<FeatureAnchor>& ShopAssets::FeatureAnchorsFor(PartType type)
{
	static std::unordered_map<PartType, std::vector<FeatureAnchor>> cache;
	auto it = cache.find(type);
	if (it == cache.end())
		it = cache.emplace(type, BuildFeatureAnchors(type)).first;
	return it->second;
}

std::vector<FeatureAnchor> ShopAssets::BuildFeatureAnchors(PartType type)
{
	using namespace EngineLayout;
	std::vector<FeatureAnchor> anchors;
	auto add = [&](const std::string& name, glm::vec3 position) { anchors.push_back({ name, position }); };

	switch (type)
	{
		case PartType::BLOCK:
			for (int cylinder = 0; cylinder < 4; cylinder++)
				add(FeatureNames::Bore(cylinder), { BoreX(cylinder), -0.04f, 0.f });
			for (int journal = 0; journal < 5; journal++)
				add(FeatureNames::MainHousing(journal), { MainX(journal), CRANK_Y + 0.02f, 0.f });
			add(FeatureNames::DECK_FLATNESS, { 0.f, 0.f, 0.085f });
			add(FeatureNames::DECK_HEIGHT, { BLOCK_LENGTH * 0.5f, -0.13f, 0.f });
			break;
		case PartType::CRANKSHAFT:
			for (int journal = 0; journal < 5; journal++)
				add(FeatureNames::MainJournal(journal), { MainX(journal), 0.f, 0.f });
			for (int cylinder = 0; cylinder < 4; cylinder++)
				add(FeatureNames::RodJournal(cylinder), { BoreX(cylinder), ThrowSign(cylinder) * CRANK_THROW, 0.f });
			add(FeatureNames::STROKE, { -0.23f, 0.f, 0.f });
			break;
		case PartType::CONNECTING_ROD:
			add(FeatureNames::BIG_END, { 0.f, -ROD_LENGTH, 0.f });
			add(FeatureNames::CENTER_LENGTH, { 0.f, -ROD_LENGTH * 0.5f, 0.f });
			break;
		case PartType::PISTON:
			add(FeatureNames::SKIRT, { 0.f, -0.045f, 0.0395f });
			add(FeatureNames::COMPRESSION_HEIGHT, { 0.0395f, -0.03f, 0.f });
			add(FeatureNames::DISH_VOLUME, { 0.f, 0.f, 0.f });
			break;
		case PartType::MAIN_BEARING:		add(FeatureNames::WALL_THICKNESS, { 0.f, 0.0285f, 0.f }); break;
		case PartType::MAIN_BEARING_LOWER:	add(FeatureNames::WALL_THICKNESS, { 0.f, -0.0285f, 0.f }); break;
		case PartType::ROD_BEARING:			add(FeatureNames::WALL_THICKNESS, { 0.f, 0.0248f, 0.f }); break;
		case PartType::ROD_BEARING_LOWER:	add(FeatureNames::WALL_THICKNESS, { 0.f, -0.0248f, 0.f }); break;
		case PartType::CYLINDER_HEAD:
			add(FeatureNames::DECK_FLATNESS, { 0.f, 0.f, 0.085f });
			for (int cylinder = 0; cylinder < 4; cylinder++)
			{
				add(FeatureNames::Chamber(cylinder), { BoreX(cylinder), 0.f, 0.f });
				for (bool exhaust : { false, true })
				{
					add(FeatureNames::Guide(cylinder, exhaust), { BoreX(cylinder), HEAD_FLOOR_Y, ValveZ(exhaust) });
					add(FeatureNames::SeatWidth(cylinder, exhaust), { BoreX(cylinder), 0.002f, ValveZ(exhaust) });
					add(FeatureNames::SeatRunout(cylinder, exhaust), { BoreX(cylinder), 0.002f, ValveZ(exhaust) });
				}
			}
			for (int journal = 0; journal < 5; journal++)
				add(FeatureNames::CamBore(journal), { MainX(journal), CAM_Y, 0.f });
			break;
		case PartType::HEAD_GASKET:
			add(FeatureNames::COMPRESSED_THICKNESS, { BLOCK_LENGTH * 0.45f, 0.001f, 0.09f });
			break;
		case PartType::CAMSHAFT:
			for (int journal = 0; journal < 5; journal++)
				add(FeatureNames::CamJournal(journal), { MainX(journal), 0.f, 0.f });
			for (int cylinder = 0; cylinder < 4; cylinder++)
				for (bool exhaust : { false, true })
					add(FeatureNames::LobeLift(cylinder, exhaust), { BoreX(cylinder) + (exhaust ? 0.016f : -0.016f), 0.f, 0.f });
			break;
		case PartType::INTAKE_VALVE:
		case PartType::EXHAUST_VALVE:
			add(FeatureNames::STEM, { 0.f, 0.06f, 0.f });
			add(FeatureNames::MARGIN, { 0.f, 0.001f, 0.015f });
			add(FeatureNames::FACE_RUNOUT, { 0.f, 0.002f, 0.f });
			add(FeatureNames::VALVE_LENGTH, { 0.f, VALVE_STEM_LENGTH, 0.f });
			break;
		case PartType::VALVE_SPRING:
			add(FeatureNames::FREE_LENGTH, { 0.f, 0.022f, 0.012f });
			add(FeatureNames::SEAT_PRESSURE, { 0.f, 0.022f, 0.f });
			break;
		case PartType::OIL_PUMP:
			add(FeatureNames::ROTOR_TIP_CLEARANCE, { 0.f, 0.016f, 0.026f });
			add(FeatureNames::ROTOR_SIDE_CLEARANCE, { 0.026f, 0.016f, 0.f });
			break;
		case PartType::OIL_PAN:				add(FeatureNames::RAIL_FLATNESS, { 0.15f, 0.f, 0.112f }); break;
		case PartType::LIFTER:				add(FeatureNames::BUCKET_DIAMETER, { 0.f, -0.006f, 0.0135f }); break;
		case PartType::CRANK_SPROCKET:		add(FeatureNames::ROOT_DIAMETER, { 0.f, 0.022f, 0.f }); break;
		case PartType::CAM_SPROCKET:		add(FeatureNames::ROOT_DIAMETER, { 0.f, 0.044f, 0.f }); break;
		case PartType::TIMING_BELT:		add(FeatureNames::BELT_TOOTH_HEIGHT, { 0.f, 0.004f, 0.0355f }); break;
		case PartType::BELT_IDLER:			add(FeatureNames::IDLER_PLAY, { 0.f, 0.011f, 0.f }); break;
		case PartType::BELT_TENSIONER:		add(FeatureNames::TENSIONER_TRAVEL, { 0.033f, 0.012f, 0.f }); break;
		case PartType::TIMING_COVER:		add(FeatureNames::FACE_FLATNESS, { 0.2f, -0.022f, 0.1f }); break;
		case PartType::CRANK_PULLEY:		add(FeatureNames::RING_RUNOUT, { -0.008f, 0.066f, 0.f }); break;
		case PartType::VALVE_COVER:			add(FeatureNames::RAIL_FLATNESS, { 0.18f, 0.f, 0.1f }); break;
		case PartType::SPARK_PLUG:			add(FeatureNames::ELECTRODE_GAP, { 0.003f, -0.001f, 0.f }); break;
		case PartType::INTAKE_MANIFOLD:		add(FeatureNames::FLANGE_FLATNESS, { 0.15f, 0.f, 0.f }); break;
		case PartType::THROTTLE_BODY:		add(FeatureNames::THROTTLE_BORE, { -0.02f, 0.f, 0.f }); break;
		case PartType::FUEL_INJECTOR:		add(FeatureNames::INJECTOR_FLOW, { 0.f, 0.003f, 0.f }); break;
		case PartType::EXHAUST_MANIFOLD:	add(FeatureNames::FLANGE_FLATNESS, { 0.15f, 0.f, 0.f }); break;
		case PartType::WATER_PUMP:			add(FeatureNames::SHAFT_PLAY, { -0.045f, 0.012f, 0.f }); break;
		default:
			break;
	}
	return anchors;
}

std::vector<AssemblyAnchor> ShopAssets::AssemblyAnchors(const EngineSpec& spec)
{
	using namespace EngineLayout;
	std::vector<AssemblyAnchor> anchors;
	for (int journal = 0; journal < spec.mainJournals; journal++)
		anchors.push_back({ AssemblyCheck::OIL_CLEARANCE, journal, false, { MainX(journal), CRANK_Y, 0.f } });
	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
	{
		anchors.push_back({ AssemblyCheck::OIL_CLEARANCE, cylinder, true, { BoreX(cylinder), PinY(cylinder), 0.f } });
		anchors.push_back({ AssemblyCheck::RING_GAP, cylinder, false, { BoreX(cylinder), -0.02f, 0.f } });
		anchors.push_back({ AssemblyCheck::DECK_CLEARANCE, cylinder, false, { BoreX(cylinder), 0.f, 0.03f } });
		for (bool exhaust : { false, true })
			anchors.push_back({ AssemblyCheck::INSTALLED_HEIGHT, EngineAssembly::SpringIndex(cylinder, exhaust), false,
				{ BoreX(cylinder), HEAD_DECK_Y + HEAD_FLOOR_Y + 0.02f, ValveZ(exhaust) } });
	}
	for (int journal = 0; journal < spec.camJournals; journal++)
		anchors.push_back({ AssemblyCheck::CAM_CLEARANCE, journal, false, { CamTowerX(journal), HEAD_DECK_Y + CAM_Y, 0.f } });
	return anchors;
}

glm::ivec2 ShopAssets::PartGridSize(PartType type)
{
	const auto& info = PartCatalog::Get(type);
	return { info.gridWidth, info.gridHeight };
}

void ShopAssets::LoadTools()
{
	const glm::vec3 steel = { 0.72f, 0.73f, 0.76f };
	const glm::vec3 dark = { 0.2f, 0.2f, 0.22f };
	const glm::vec3 face = { 0.95f, 0.95f, 0.9f };

	// Tools lie flat with their origin in the middle of their footprint.
	for (const auto& tool : ToolCatalog::All())
	{
		MeshBuilder builder;
		builder.SetUVScale(12.f);
		switch (ToolShapeFor(tool))
		{
			case ToolShape::CALIPER:
				Box(builder, { 0.f, 0.f, 0.f }, { 0.20f, 0.004f, 0.016f }, steel);
				Box(builder, { -0.092f, 0.f, -0.022f }, { 0.008f, 0.004f, 0.03f }, steel);
				Box(builder, { -0.03f, 0.f, -0.02f }, { 0.008f, 0.004f, 0.026f }, steel);
				Box(builder, { -0.02f, 0.004f, 0.004f }, { 0.04f, 0.006f, 0.022f }, dark);
				break;
			case ToolShape::MICROMETER:
				Box(builder, { -0.03f, 0.f, 0.f }, { 0.012f, 0.012f, 0.05f }, { 0.75f, 0.25f, 0.2f });
				Box(builder, { 0.f, 0.f, -0.025f }, { 0.06f, 0.012f, 0.012f }, { 0.75f, 0.25f, 0.2f });
				Box(builder, { 0.f, 0.f, 0.025f }, { 0.06f, 0.012f, 0.012f }, { 0.75f, 0.25f, 0.2f });
				Cylinder(builder, AlongX({ 0.055f, 0.f, 0.025f }), 0.007f, 0.05f, steel);
				Cylinder(builder, AlongX({ 0.085f, 0.f, 0.025f }), 0.010f, 0.025f, steel);
				break;
			case ToolShape::HEIGHT_MICROMETER:
				Cylinder(builder, At({ 0.f, 0.005f, 0.f }), 0.016f, 0.01f, dark);
				Cylinder(builder, At({ 0.f, 0.04f, 0.f }), 0.007f, 0.06f, steel);
				Cylinder(builder, At({ 0.f, 0.06f, 0.f }), 0.010f, 0.02f, steel);
				break;
			case ToolShape::TELESCOPING_GAUGE:
				Cylinder(builder, AlongX({ 0.f, 0.f, 0.f }), 0.005f, 0.12f, steel);
				Cylinder(builder, AlongZ({ 0.06f, 0.f, 0.f }), 0.004f, 0.05f, steel);
				break;
			case ToolShape::BORE_GAUGE:
			case ToolShape::SMALL_BORE_GAUGE:
			{
				float length = ToolShapeFor(tool) == ToolShape::BORE_GAUGE ? 0.25f : 0.14f;
				Cylinder(builder, AlongX({ 0.f, 0.f, 0.f }), 0.006f, length, steel);
				Cylinder(builder, At({ -length * 0.5f - 0.02f, 0.f, 0.f }), 0.028f, 0.012f, dark);
				Cylinder(builder, At({ -length * 0.5f - 0.02f, 0.0062f, 0.f }), 0.024f, 0.001f, face);
				Box(builder, { length * 0.5f, 0.f, 0.f }, { 0.01f, 0.01f, 0.03f }, steel);
				break;
			}
			case ToolShape::SMALL_HOLE_GAUGE:
				Box(builder, { 0.f, 0.f, 0.f }, { 0.08f, 0.015f, 0.05f }, dark);
				for (int i = 0; i < 4; i++)
					Cylinder(builder, AlongX({ 0.f, 0.009f, -0.018f + i * 0.012f }), 0.002f + i * 0.0006f, 0.07f, steel);
				break;
			case ToolShape::STRAIGHTEDGE:
				Box(builder, { 0.f, 0.f, 0.f }, { 0.60f, 0.008f, 0.04f }, steel);
				Box(builder, { 0.2f, 0.007f, 0.f }, { 0.08f, 0.006f, 0.02f }, { 0.75f, 0.6f, 0.2f });
				break;
			case ToolShape::BURETTE:
				Box(builder, { 0.f, 0.005f, 0.f }, { 0.10f, 0.01f, 0.10f }, dark);
				Cylinder(builder, At({ 0.f, 0.16f, 0.f }), 0.006f, 0.3f, { 0.75f, 0.85f, 0.95f });
				Cylinder(builder, At({ 0.03f, 0.12f, 0.f }), 0.003f, 0.22f, steel);
				break;
			case ToolShape::PROFILOMETER:
				Box(builder, { 0.f, 0.025f, 0.f }, { 0.12f, 0.05f, 0.08f }, { 0.85f, 0.85f, 0.82f });
				Box(builder, { -0.01f, 0.051f, 0.f }, { 0.07f, 0.002f, 0.05f }, { 0.2f, 0.45f, 0.35f });
				Cylinder(builder, AlongX({ 0.08f, 0.01f, 0.f }), 0.003f, 0.05f, steel);
				break;
			case ToolShape::PLASTIGAUGE:
				Box(builder, { 0.f, 0.f, 0.f }, { 0.08f, 0.003f, 0.03f }, { 0.3f, 0.6f, 0.3f });
				break;
			case ToolShape::FEELERS:
				for (int blade = 0; blade < 5; blade++)
				{
					builder.SetTransform(glm::rotate(glm::mat4(1.f), -0.5f + blade * 0.25f, { 0.f, 1.f, 0.f }));
					builder.SetColor(steel).AddBox({ 0.045f, blade * 0.0008f, 0.f }, { 0.09f, 0.0007f, 0.012f });
				}
				builder.ResetTransform();
				Cylinder(builder, At({ 0.f, 0.002f, 0.f }), 0.008f, 0.006f, dark);
				break;
			case ToolShape::DIAL_INDICATOR:
			case ToolShape::RUNOUT_GAUGE:
				if (ToolShapeFor(tool) == ToolShape::RUNOUT_GAUGE)
				{
					Box(builder, { -0.05f, 0.015f, 0.04f }, { 0.04f, 0.03f, 0.04f }, steel);
					Box(builder, { 0.05f, 0.015f, 0.04f }, { 0.04f, 0.03f, 0.04f }, steel);
				}
				Box(builder, { 0.f, 0.02f, -0.02f }, { 0.05f, 0.04f, 0.04f }, dark);
				Cylinder(builder, At({ 0.f, 0.09f, -0.02f }), 0.005f, 0.1f, steel);
				Cylinder(builder, AlongZ({ 0.f, 0.14f, -0.005f }), 0.025f, 0.012f, dark);
				Cylinder(builder, AlongZ({ 0.f, 0.14f, 0.0015f }), 0.021f, 0.001f, face);
				break;
			case ToolShape::SPRING_TESTER:
				Box(builder, { 0.f, 0.015f, 0.f }, { 0.18f, 0.03f, 0.12f }, { 0.25f, 0.3f, 0.5f });
				Box(builder, { 0.07f, 0.15f, 0.f }, { 0.03f, 0.25f, 0.03f }, steel);
				Box(builder, { 0.f, 0.26f, 0.f }, { 0.18f, 0.015f, 0.02f }, steel);
				Cylinder(builder, AlongZ({ -0.03f, 0.12f, 0.035f }), 0.03f, 0.012f, dark);
				break;
			// Hand tools: lying flat, the handle along X.
			case ToolShape::RATCHET:
			case ToolShape::BREAKER_BAR:
			case ToolShape::TORQUE_WRENCH:
			{
				float length = DriveToolLength(tool);
				Box(builder, { 0.f, 0.006f, 0.f }, { length, 0.012f, 0.016f }, steel);
				Box(builder, { -length * 0.3f, 0.008f, 0.f }, { length * 0.4f, 0.016f, 0.02f }, ToolShapeFor(tool) == ToolShape::TORQUE_WRENCH ? glm::vec3(0.75f, 0.2f, 0.15f) : dark);
				Cylinder(builder, At({ length * 0.5f, 0.012f, 0.f }), 0.016f, 0.024f, steel);
				Box(builder, { length * 0.5f, -0.002f, 0.f }, { 0.0095f, 0.012f, 0.0095f }, steel);
				if (ToolShapeFor(tool) == ToolShape::TORQUE_WRENCH)
					Cylinder(builder, AlongZ({ 0.f, 0.014f, 0.f }), 0.012f, 0.004f, face);
				break;
			}
			case ToolShape::SOCKET_SET:
			{
				// The open case, with a hole for each socket. The sockets are objects of their own (see
				// SocketSlot), so one can be out on a ratchet.
				Box(builder, { 0.f, 0.008f, 0.f }, { 0.26f, 0.016f, 0.12f }, { 0.75f, 0.12f, 0.1f });
				for (size_t i = 0; i < tool.sizes.size(); i++)
					Cylinder(builder, At(SocketSlot(tool, i) * glm::vec3(1.f, 0.f, 1.f) + glm::vec3(0.f, 0.0165f, 0.f)), SocketRadius(tool.sizes[i]) + 0.001f, 0.001f, { 0.12f, 0.1f, 0.1f }, 12);
				break;
			}
			case ToolShape::WRENCH_SET:
				Box(builder, { 0.f, 0.008f, 0.f }, { 0.26f, 0.016f, 0.14f }, { 0.15f, 0.2f, 0.45f });
				for (int i = 0; i < 6; i++)
					Box(builder, { 0.f, 0.019f, -0.055f + i * 0.022f }, { 0.12f + i * 0.02f, 0.004f, 0.012f }, steel);
				break;
		}
		AddModel(ToolModelName(tool), builder);
	}
	// One socket, scaled to each size (SocketRadius) wherever it is: in its set or on a ratchet.
	{
		MeshBuilder socket;
		socket.SetUVScale(20.f);
		Cylinder(socket, At({}), SOCKET_MODEL_RADIUS, SOCKET_HEIGHT, { 0.72f, 0.73f, 0.76f }, 16);
		AddModel(SOCKET_MODEL, socket);
	}
}

void ShopAssets::LoadOverrides()
{
	// Each line: <Model Name> = <file> [scale=0.001] [rotate=x,y,z] [offset=x,y,z] [texture=file.png]
	std::ifstream file(MODEL_FOLDER + "models.cfg");
	if (!file)
		return;

	auto parseVector = [](const std::string& text) {
		glm::vec3 result = {};
		std::stringstream stream(text);
		std::string component;
		for (int i = 0; i < 3 && std::getline(stream, component, ','); i++)
			result[i] = std::stof(component);
		return result;
	};

	std::string line;
	while (std::getline(file, line))
	{
		auto comment = line.find('#');
		if (comment != std::string::npos)
			line = line.substr(0, comment);
		auto equals = line.find('=');
		if (equals == std::string::npos)
			continue;

		auto trim = [](std::string text) {
			text.erase(0, text.find_first_not_of(" \t\r"));
			text.erase(text.find_last_not_of(" \t\r") + 1);
			return text;
		};
		std::string name = trim(line.substr(0, equals));
		std::stringstream tokens(line.substr(equals + 1));

		ModelOverride entry;
		tokens >> entry.file;
		if (name.empty() || entry.file.empty())
			continue;

		std::string token;
		try
		{
			while (tokens >> token)
			{
				auto separator = token.find('=');
				if (separator == std::string::npos)
					continue;
				std::string key = token.substr(0, separator), value = token.substr(separator + 1);
				if (key == "scale") entry.options.scale = std::stof(value);
				else if (key == "rotate") entry.options.rotationDegrees = parseVector(value);
				else if (key == "offset") entry.options.offset = parseVector(value);
				else if (key == "texture") entry.texture = value;
			}
		}
		catch (const std::exception&)
		{
			LOG_WARNING("models.cfg: couldn't read the options for " + name);
			continue;
		}
		m_overrides[name] = entry;
	}
}

void ShopAssets::AddModel(const std::string& name, const MeshBuilder& builder)
{
	auto modelManager = ModelManager::Get();

	auto it = m_overrides.find(name);
	if (it != m_overrides.end())
	{
		const auto& entry = it->second;
		std::string path = MODEL_FOLDER + entry.file;
		try
		{
			if (!std::filesystem::exists(path))
				throw std::runtime_error("file not found");
			m_models[name] = modelManager->CreateModel(::Model(name, path, entry.options))->GetIndex();

			if (!entry.texture.empty())
				m_modelTextures[name] = TextureManager::Get()->CreateTexture({ name, MODEL_FOLDER + entry.texture })->GetIndex();

			LOG_INFO("Loaded custom model for " + name + " from " + path);
			return;
		}
		catch (const std::exception& error)
		{
			LOG_WARNING("Couldn't load " + path + " for " + name + " (" + error.what() + "); using the built-in model.");
		}
		catch (...)
		{
			LOG_WARNING("Couldn't load " + path + " for " + name + "; using the built-in model.");
		}
	}

	m_models[name] = modelManager->CreateModel(builder.Build(name))->GetIndex();
}

uint32_t ShopAssets::TextureFor(const std::string& modelName, const std::string& fallbackTexture) const
{
	auto it = m_modelTextures.find(modelName);
	return it != m_modelTextures.end() ? it->second : Texture(fallbackTexture);
}

uint32_t ShopAssets::Model(const std::string& name) const
{
	auto it = m_models.find(name);
	if (it == m_models.end())
		throw std::runtime_error("Shop model not found: " + name);
	return it->second;
}

uint32_t ShopAssets::Texture(const std::string& name) const
{
	auto it = m_textures.find(name);
	if (it == m_textures.end())
		throw std::runtime_error("Shop texture not found: " + name);
	return it->second;
}

uint32_t ShopAssets::PartModel(PartType type) const
{
	return Model(PartTypeName(type));
}

uint32_t ShopAssets::PartTexture(const Part& part) const
{
	auto custom = m_modelTextures.find(PartTypeName(part.GetType()));
	if (custom != m_modelTextures.end())
		return custom->second;

	switch (part.GetMaterial())
	{
		case MaterialType::GREY_IRON:
		case MaterialType::NODULAR_IRON:			return Texture(ShopTextures::CAST_IRON);
		case MaterialType::ALUMINUM_A356:
		case MaterialType::CAST_ALUMINUM:
		case MaterialType::HYPEREUTECTIC_ALUMINUM:
		case MaterialType::FORGED_2618:				return Texture(ShopTextures::ALUMINIUM);
		case MaterialType::FKM_RUBBER:
		case MaterialType::NYLON:
		case MaterialType::COMPOSITE_GASKET:		return Texture(ShopTextures::RUBBER);
		case MaterialType::STAMPED_STEEL:			return Texture(ShopTextures::PAINT);
		default:									return Texture(ShopTextures::STEEL);
	}
}

glm::vec4 ShopAssets::PartTint(MaterialType material) const
{
	switch (material)
	{
		case MaterialType::NODULAR_IRON:			return { 0.85f, 0.85f, 0.88f, 1.f };
		case MaterialType::FORGED_4340:				return { 1.10f, 1.10f, 1.12f, 1.f };
		case MaterialType::POWDER_METAL:			return { 0.75f, 0.75f, 0.78f, 1.f };
		case MaterialType::FORGED_2618:				return { 1.05f, 1.05f, 1.08f, 1.f };
		case MaterialType::HYPEREUTECTIC_ALUMINUM:	return { 0.95f, 0.95f, 0.92f, 1.f };
		case MaterialType::DUCTILE_IRON:			return { 0.45f, 0.45f, 0.47f, 1.f };
		case MaterialType::BIMETAL:					return { 1.05f, 0.88f, 0.62f, 1.f };
		case MaterialType::HARDENED_STEEL:			return { 0.70f, 0.70f, 0.74f, 1.f };
		// The rest carry their colour in the model.
		case MaterialType::FKM_RUBBER:
		case MaterialType::NYLON:
		case MaterialType::COMPOSITE_GASKET:		return { 1.6f, 1.6f, 1.6f, 1.f };
		default:									return { 1.f, 1.f, 1.f, 1.f };
	}
}

MachineFixture ShopAssets::FixtureFor(const Machine& machine)
{
	switch (machine.operation)
	{
		case MachineOperation::FILE_RING:	return { "Machine: Ring Filer", { -0.12f, 0.90f, 0.12f }, 0.f, { 0.5f, 0.5f } };
		case MachineOperation::HONE:
			// The power hone is the one that can take more stock per setup.
			if (machine.maxRemoval > 0.1)
				return { "Machine: Power Hone", { -0.18f, 0.90f, 0.f }, 0.f, { 1.0f, 1.0f } };
			return { "Machine: Drill Hone", { -0.10f, 0.90f, 0.f }, 0.f, { 0.6f, 0.6f } };
		case MachineOperation::BORE:		return { "Machine: Boring Bar", { -0.18f, 0.85f, 0.f }, 0.f, { 0.9f, 0.9f } };
		case MachineOperation::GRIND:		return { "Machine: Crank Grinder", { -0.12f, 1.00f, 0.f }, glm::half_pi<float>(), { 0.8f, 1.0f } };
		case MachineOperation::POLISH:		return { "Machine: Journal Polisher", { -0.10f, 0.90f, 0.f }, 0.f, { 0.6f, 0.6f } };
		case MachineOperation::SURFACE:
			// By hand, on a stout wooden bench.
			if (machine.removalError > 0.0)
				return { "Machine: Sanding Bench", { 0.f, 0.78f, 0.f }, glm::half_pi<float>(), { 0.7f, 1.3f } };
			return { "Machine: Surface Grinder", { -0.10f, 0.96f, 0.f }, 0.f, { 0.55f, 0.82f } };
		case MachineOperation::REAM:		return { "Machine: Guide Reamer", { -0.10f, 0.90f, 0.f }, 0.f, { 0.7f, 0.6f } };
		case MachineOperation::CUT_SEAT:	return { "Machine: Seat Cutter", { -0.10f, 0.90f, 0.f }, 0.f, { 0.9f, 0.7f } };
		case MachineOperation::FACE_VALVE:	return { "Machine: Valve Refacer", { -0.12f, 0.90f, 0.1f }, 0.f, { 0.6f, 0.6f } };
		case MachineOperation::LINE_BORE:	return { "Machine: Line Borer", { 0.f, 0.88f, 0.f }, 0.f, { 1.1f, 0.5f } };
	}
	return {};
}

std::vector<SurfaceOverlay> ShopAssets::OverlaysFor(PartType type)
{
	using namespace EngineLayout;
	std::vector<SurfaceOverlay> overlays;
	switch (type)
	{
		case PartType::BLOCK:
			for (int cylinder = 0; cylinder < 4; cylinder++)
				overlays.push_back({ FeatureNames::Bore(cylinder), "Overlay: Bore", { BoreX(cylinder), -CYLINDER_DEPTH * 0.5f, 0.f } });
			overlays.push_back({ FeatureNames::DECK_FLATNESS, "Overlay: Block Deck", {} });
			break;
		case PartType::CRANKSHAFT:
			for (int journal = 0; journal < 5; journal++)
				overlays.push_back({ FeatureNames::MainJournal(journal), "Overlay: Main Journal", { MainX(journal), 0.f, 0.f } });
			for (int cylinder = 0; cylinder < 4; cylinder++)
				overlays.push_back({ FeatureNames::RodJournal(cylinder), "Overlay: Rod Journal", { BoreX(cylinder), ThrowSign(cylinder) * CRANK_THROW, 0.f } });
			break;
		case PartType::CYLINDER_HEAD:
			overlays.push_back({ FeatureNames::DECK_FLATNESS, "Overlay: Head Deck", {} });
			break;
		case PartType::CAMSHAFT:
			for (int journal = 0; journal < 5; journal++)
				overlays.push_back({ FeatureNames::CamJournal(journal), "Overlay: Cam Journal", { MainX(journal), 0.f, 0.f } });
			break;
		default:
			break;
	}
	return overlays;
}

uint32_t ShopAssets::SurfaceTexture(const Feature& feature) const
{
	auto op = feature.lastOperation;

	if (feature.kind == FeatureKind::FLATNESS)
		return op == MachineOperation::SURFACE ? Texture(ShopTextures::SURFACE_GROUND) : 0u;

	if (feature.kind == FeatureKind::BORE)
	{
		if (op == MachineOperation::HONE)
			return Texture(feature.surfaceRa > 1.0 ? ShopTextures::SCORED : ShopTextures::HONED);
		if (op == MachineOperation::BORE)
			return Texture(ShopTextures::BORED);
		if (feature.surfaceRa > 1.0)
			return Texture(ShopTextures::SCORED);
		if (feature.surfaceRa < 0.16)
			return Texture(ShopTextures::GLAZED);
		return Texture(ShopTextures::WORN);
	}

	// Journals
	if (feature.surfaceRa > 0.6)
		return Texture(ShopTextures::SCORED);
	if (op == MachineOperation::POLISH || feature.surfaceRa < 0.15)
		return Texture(ShopTextures::POLISHED);
	if (op == MachineOperation::GRIND)
		return Texture(ShopTextures::GROUND);
	return Texture(ShopTextures::WORN);
}

void ShopAssets::LoadTextures()
{
	auto textureManager = TextureManager::Get();
	auto add = [&](const std::string& name, const ProceduralTextures::Image& image)
	{
		m_textures[name] = textureManager->CreateTexture(name, image.width, image.height, image.pixels)->GetIndex();
	};

	add(ShopTextures::CONCRETE, ProceduralTextures::Concrete());
	add(ShopTextures::CAST_IRON, ProceduralTextures::CastIron());
	add(ShopTextures::ALUMINIUM, ProceduralTextures::Aluminium());
	add(ShopTextures::STEEL, ProceduralTextures::BrushedSteel());
	add(ShopTextures::WOOD, ProceduralTextures::Wood());
	add(ShopTextures::PAINT, ProceduralTextures::PaintedMetal());
	add(ShopTextures::PLASTER, ProceduralTextures::Plaster());
	add(ShopTextures::PEGBOARD, ProceduralTextures::Pegboard());
	add(ShopTextures::RUBBER, ProceduralTextures::Rubber());
	add(ShopTextures::CARDBOARD, ProceduralTextures::Cardboard());
	add(ShopTextures::GRASS, ProceduralTextures::Grass());
	add(ShopTextures::ASPHALT, ProceduralTextures::Asphalt());
	add(ShopTextures::SIDING, ProceduralTextures::Siding());
	add(ShopTextures::BRICK, ProceduralTextures::Brick());
	add(ShopTextures::SHINGLES, ProceduralTextures::Shingles());
	add(ShopTextures::BARK, ProceduralTextures::Bark());
	add(ShopTextures::FOLIAGE, ProceduralTextures::Foliage());

	add(ShopTextures::HONED, ProceduralTextures::Honed());
	add(ShopTextures::BORED, ProceduralTextures::Bored());
	add(ShopTextures::GLAZED, ProceduralTextures::Glazed());
	add(ShopTextures::WORN, ProceduralTextures::Worn());
	add(ShopTextures::SCORED, ProceduralTextures::Scored());
	add(ShopTextures::GROUND, ProceduralTextures::Ground());
	add(ShopTextures::POLISHED, ProceduralTextures::Polished());
	add(ShopTextures::SURFACE_GROUND, ProceduralTextures::SurfaceGround());
}

void ShopAssets::LoadEnvironment()
{
	auto add = [&](const std::string& name, const MeshBuilder& builder) { AddModel(name, builder); };

	{
		MeshBuilder floor;
		floor.SetUVScale(0.5f);
		Box(floor, { 0.f, -0.05f, 0.f }, { 14.f, 0.1f, 10.f });
		add(ShopModels::FLOOR, floor);
	}
	{
		MeshBuilder lamp;
		Box(lamp, { 0.f, 0.f, 0.f }, { 1.2f, 0.04f, 0.2f }, { 1.f, 0.98f, 0.92f });
		add(ShopModels::LAMP, lamp);
	}
	{
		MeshBuilder bench;
		bench.SetUVScale(1.5f);
		Box(bench, { 0.f, 0.87f, 0.f }, { 2.0f, 0.06f, 0.8f });
		for (float x : { -0.92f, 0.92f })
			for (float z : { -0.34f, 0.34f })
				Box(bench, { x, 0.42f, z }, { 0.06f, 0.84f, 0.06f }, { 0.45f, 0.42f, 0.40f });
		Box(bench, { 0.f, 0.15f, 0.f }, { 1.9f, 0.03f, 0.7f }, { 0.8f, 0.8f, 0.8f });
		// Bench vise.
		Box(bench, { 0.8f, 0.94f, 0.32f }, { 0.12f, 0.08f, 0.16f }, { 0.25f, 0.3f, 0.45f });
		Box(bench, { 0.8f, 0.99f, 0.25f }, { 0.14f, 0.06f, 0.02f }, { 0.25f, 0.3f, 0.45f });
		add(ShopModels::WORKBENCH, bench);
	}
	{
		MeshBuilder shelf;
		shelf.SetUVScale(2.f);
		const glm::vec3 upright = { 0.30f, 0.40f, 0.58f };
		const glm::vec3 board = { 0.70f, 0.70f, 0.68f };
		for (float x : { -1.58f, 1.58f })
			for (float z : { -0.23f, 0.23f })
				Box(shelf, { x, 1.1f, z }, { 0.04f, 2.2f, 0.04f }, upright);
		for (float y : { 0.35f, 0.85f, 1.35f, 1.85f })
			Box(shelf, { 0.f, y - 0.015f, 0.f }, { 3.2f, 0.03f, 0.5f }, board);
		add(ShopModels::SHELF, shelf);
	}
	{
		MeshBuilder rack;
		rack.SetUVScale(1.5f);
		Box(rack, { 0.f, 0.13f, 0.f }, { 1.1f, 0.03f, 8.0f });
		for (float x : { -0.5f, 0.f, 0.5f })
			Box(rack, { x, 0.06f, 0.f }, { 0.08f, 0.12f, 8.0f }, { 0.75f, 0.75f, 0.75f });
		add(ShopModels::PALLET_RACK, rack);
	}
	{
		MeshBuilder board;
		board.SetUVScale(5.f);
		Box(board, { 0.f, 0.f, 0.f }, { 1.8f, 1.25f, 0.03f });
		add(ShopModels::TOOL_BOARD, board);

		MeshBuilder tools;
		tools.SetUVScale(8.f);
		// Spanners in a row, a pair of micrometers and a caliper.
		for (int i = 0; i < 8; i++)
			Box(tools, { -0.75f + i * 0.05f, 0.05f + i * 0.012f, 0.025f }, { 0.018f, 0.16f + i * 0.025f, 0.008f }, STEEL_GREY);
		for (int i = 0; i < 2; i++)
		{
			float x = 0.05f + i * 0.22f;
			Box(tools, { x, 0.12f, 0.025f }, { 0.14f, 0.02f, 0.01f }, { 0.3f, 0.3f, 0.32f });
			Box(tools, { x - 0.06f, 0.07f, 0.025f }, { 0.02f, 0.10f, 0.01f }, { 0.3f, 0.3f, 0.32f });
			Box(tools, { x + 0.07f, 0.04f, 0.025f }, { 0.07f, 0.025f, 0.012f }, STEEL_GREY);
		}
		Box(tools, { 0.6f, 0.1f, 0.025f }, { 0.025f, 0.3f, 0.006f }, STEEL_GREY);
		Box(tools, { 0.6f, 0.23f, 0.025f }, { 0.08f, 0.02f, 0.006f }, STEEL_GREY);
		Box(tools, { 0.6f, -0.2f, 0.025f }, { 0.4f, 0.12f, 0.02f }, { 0.65f, 0.12f, 0.1f });
		add(ShopModels::TOOL_BOARD_TOOLS, tools);
	}
	{
		// Mount point at (0, 1.05, 0): the centre of the block deck when an engine is bolted on.
		MeshBuilder stand;
		stand.SetUVScale(3.f);
		const glm::vec3 red = { 0.70f, 0.12f, 0.10f };
		Box(stand, { -0.55f, 0.06f, 0.f }, { 0.08f, 0.06f, 0.9f }, red);
		Box(stand, { -0.15f, 0.06f, 0.f }, { 0.8f, 0.06f, 0.08f }, red);
		Box(stand, { -0.55f, 0.55f, 0.f }, { 0.08f, 1.0f, 0.08f }, red);
		Box(stand, { -0.40f, 0.93f, 0.f }, { 0.24f, 0.06f, 0.06f }, red);
		Box(stand, { -0.235f, 0.93f, 0.f }, { 0.02f, 0.26f, 0.26f }, { 0.35f, 0.35f, 0.38f });
		for (float z : { -0.42f, 0.42f })
			Cylinder(stand, AlongZ({ -0.55f, 0.04f, z }), 0.04f, 0.04f, RUBBER_BLACK);
		Cylinder(stand, AlongZ({ 0.23f, 0.04f, 0.f }), 0.04f, 0.04f, RUBBER_BLACK);
		add(ShopModels::ENGINE_STAND, stand);
	}
	{
		MeshBuilder dyno;
		dyno.SetUVScale(3.f);
		const glm::vec3 blue = { 0.15f, 0.22f, 0.40f };
		Box(dyno, { 0.f, 0.2f, 0.f }, { 1.6f, 0.4f, 0.9f }, blue);
		Cylinder(dyno, AlongX({ 0.55f, 0.65f, 0.f }), 0.25f, 0.45f, { 0.30f, 0.32f, 0.36f }, 28);
		Cylinder(dyno, AlongX({ 0.2f, 0.65f, 0.f }), 0.04f, 0.3f, STEEL_GREY);
		for (float z : { -0.3f, 0.3f })
			Box(dyno, { -0.25f, 0.55f, z }, { 0.5f, 0.3f, 0.06f }, blue * 1.3f);
		Box(dyno, { -0.95f, 0.6f, 0.65f }, { 0.3f, 1.2f, 0.3f }, { 0.2f, 0.2f, 0.22f });
		Box(dyno, { -0.95f, 1.3f, 0.65f }, { 0.04f, 0.3f, 0.45f }, { 0.05f, 0.05f, 0.06f });
		Box(dyno, { -0.97f, 1.3f, 0.65f }, { 0.01f, 0.25f, 0.4f }, { 0.25f, 0.7f, 0.45f });
		Cylinder(dyno, At({ -0.3f, 2.5f, -0.35f }), 0.15f, 2.3f, { 0.6f, 0.6f, 0.62f });
		add(ShopModels::DYNO, dyno);
	}
	{
		MeshBuilder desk;
		desk.SetUVScale(2.f);
		Box(desk, { 0.f, 0.74f, 0.f }, { 1.4f, 0.04f, 0.7f }, { 0.55f, 0.55f, 0.57f });
		for (float x : { -0.66f, 0.66f })
			Box(desk, { x, 0.36f, 0.f }, { 0.04f, 0.72f, 0.64f }, { 0.25f, 0.25f, 0.27f });
		Box(desk, { 0.f, 0.80f, -0.15f }, { 0.08f, 0.08f, 0.08f }, DARK);
		Box(desk, { 0.f, 1.03f, -0.15f }, { 0.58f, 0.36f, 0.03f }, DARK);
		Box(desk, { 0.f, 1.03f, -0.133f }, { 0.54f, 0.32f, 0.005f }, { 0.35f, 0.6f, 0.9f });
		Box(desk, { 0.f, 0.77f, 0.12f }, { 0.45f, 0.02f, 0.15f }, { 0.12f, 0.12f, 0.13f });
		add(ShopModels::COMPUTER_DESK, desk);
	}
}

void ShopAssets::LoadMachines()
{
	auto add = [&](const std::string& name, const MeshBuilder& builder) { AddModel(name, builder); };

	// Machines face -X, toward the middle of the shop, with their origin on the floor.
	{
		MeshBuilder filer;
		filer.SetUVScale(3.f);
		const glm::vec3 red = { 0.70f, 0.15f, 0.12f };
		Stand(filer, { 0.5f, 0.5f }, 0.9f, { 0.35f, 0.36f, 0.40f });
		Box(filer, { 0.05f, 0.96f, -0.05f }, { 0.22f, 0.12f, 0.16f }, red);
		Cylinder(filer, AlongZ({ 0.05f, 1.0f, 0.04f }), 0.06f, 0.015f, STEEL_GREY);
		Box(filer, { 0.05f, 1.0f, 0.06f }, { 0.012f, 0.012f, 0.03f }, STEEL_GREY);
		Box(filer, { 0.05f, 1.06f, 0.08f }, { 0.012f, 0.1f, 0.012f }, STEEL_GREY);
		add("Machine: Ring Filer", filer);
	}
	{
		MeshBuilder hone;
		hone.SetUVScale(3.f);
		const glm::vec3 yellow = { 0.85f, 0.68f, 0.12f };
		Stand(hone, { 0.6f, 0.6f }, 0.9f, { 0.35f, 0.36f, 0.40f });
		Cylinder(hone, At({ 0.18f, 1.3f, 0.f }), 0.03f, 0.8f, STEEL_GREY);
		Box(hone, { 0.08f, 1.6f, 0.f }, { 0.3f, 0.16f, 0.14f }, yellow);
		Cylinder(hone, At({ -0.02f, 1.42f, 0.f }), 0.012f, 0.22f, STEEL_GREY);
		Tube(hone, At({ -0.02f, 1.28f, 0.f }), 0.04f, 0.012f, 0.08f, { 0.55f, 0.45f, 0.35f });
		add("Machine: Drill Hone", hone);
	}
	{
		MeshBuilder hone;
		hone.SetUVScale(3.f);
		const glm::vec3 green = { 0.22f, 0.42f, 0.30f };
		Stand(hone, { 1.0f, 1.0f }, 0.9f, green);
		Box(hone, { 0.36f, 1.55f, 0.f }, { 0.25f, 1.3f, 0.4f }, green);
		Box(hone, { 0.12f, 2.05f, 0.f }, { 0.6f, 0.3f, 0.4f }, green * 1.15f);
		Cylinder(hone, At({ -0.08f, 1.55f, 0.f }), 0.03f, 0.7f, STEEL_GREY);
		Box(hone, { 0.36f, 1.4f, -0.21f }, { 0.2f, 0.25f, 0.02f }, { 0.08f, 0.08f, 0.09f });
		add("Machine: Power Hone", hone);
	}
	{
		MeshBuilder bar;
		bar.SetUVScale(3.f);
		const glm::vec3 blue = { 0.28f, 0.36f, 0.48f };
		Stand(bar, { 0.9f, 0.9f }, 0.85f, blue);
		Box(bar, { 0.3f, 1.4f, 0.f }, { 0.22f, 1.1f, 0.3f }, blue);
		Box(bar, { 0.08f, 1.85f, 0.f }, { 0.5f, 0.22f, 0.3f }, blue * 1.2f);
		Cylinder(bar, At({ -0.08f, 1.45f, 0.f }), 0.035f, 0.6f, STEEL_GREY);
		Box(bar, { -0.08f, 1.14f, 0.f }, { 0.07f, 0.03f, 0.03f }, { 0.8f, 0.7f, 0.2f });
		add("Machine: Boring Bar", bar);
	}
	{
		MeshBuilder grinder;
		grinder.SetUVScale(3.f);
		const glm::vec3 green = { 0.30f, 0.45f, 0.35f };
		Box(grinder, { 0.f, 0.45f, 0.f }, { 0.8f, 0.9f, 1.0f }, green);
		Box(grinder, { -0.12f, 0.95f, -0.42f }, { 0.22f, 0.2f, 0.18f }, green * 1.15f);
		Box(grinder, { -0.12f, 0.95f, 0.42f }, { 0.22f, 0.2f, 0.18f }, green * 1.15f);
		Box(grinder, { 0.25f, 1.1f, 0.f }, { 0.3f, 0.4f, 0.5f }, green * 0.9f);
		Cylinder(grinder, AlongX({ 0.12f, 1.0f, 0.f }), 0.22f, 0.04f, { 0.75f, 0.72f, 0.65f }, 32);
		Box(grinder, { 0.12f, 1.16f, 0.f }, { 0.08f, 0.12f, 0.5f }, green * 1.1f);
		add("Machine: Crank Grinder", grinder);
	}
	{
		MeshBuilder polisher;
		polisher.SetUVScale(3.f);
		const glm::vec3 orange = { 0.85f, 0.42f, 0.12f };
		Stand(polisher, { 0.6f, 0.6f }, 0.9f, { 0.35f, 0.36f, 0.40f });
		Box(polisher, { 0.18f, 1.05f, 0.f }, { 0.2f, 0.3f, 0.2f }, orange);
		Box(polisher, { 0.02f, 1.12f, 0.f }, { 0.32f, 0.04f, 0.05f }, { 0.25f, 0.2f, 0.18f });
		Cylinder(polisher, AlongZ({ -0.12f, 1.12f, 0.f }), 0.03f, 0.05f, STEEL_GREY);
		add("Machine: Journal Polisher", polisher);
	}
	{
		MeshBuilder grinder;
		grinder.SetUVScale(3.f);
		const glm::vec3 grey = { 0.38f, 0.42f, 0.48f };
		Box(grinder, { 0.f, 0.45f, 0.f }, { 0.9f, 0.9f, 0.92f }, grey);
		Box(grinder, { -0.1f, 0.93f, 0.f }, { 0.55f, 0.06f, 0.82f }, STEEL_GREY);
		Box(grinder, { 0.36f, 1.5f, 0.f }, { 0.2f, 1.2f, 0.35f }, grey);
		Box(grinder, { 0.1f, 1.85f, 0.f }, { 0.5f, 0.25f, 0.3f }, grey * 1.15f);
		Cylinder(grinder, AlongZ({ -0.1f, 1.62f, 0.f }), 0.15f, 0.05f, { 0.75f, 0.72f, 0.65f }, 28);
		add("Machine: Surface Grinder", grinder);
	}
	{
		MeshBuilder reamer;
		reamer.SetUVScale(3.f);
		const glm::vec3 teal = { 0.15f, 0.45f, 0.48f };
		Stand(reamer, { 0.7f, 0.6f }, 0.9f, { 0.35f, 0.36f, 0.40f });
		// A head-holding fixture with a tilting bar and a T-handle reamer in it.
		Box(reamer, { 0.12f, 0.96f, 0.f }, { 0.08f, 0.12f, 0.5f }, teal);
		Box(reamer, { 0.12f, 1.12f, 0.f }, { 0.04f, 0.22f, 0.04f }, teal);
		Cylinder(reamer, At({ 0.f, 1.15f, 0.f }), 0.004f, 0.2f, STEEL_GREY);
		Box(reamer, { 0.f, 1.26f, 0.f }, { 0.14f, 0.012f, 0.012f }, DARK);
		add("Machine: Guide Reamer", reamer);
	}
	{
		MeshBuilder seat;
		seat.SetUVScale(3.f);
		const glm::vec3 blue = { 0.20f, 0.30f, 0.62f };
		Stand(seat, { 0.9f, 0.7f }, 0.9f, blue * 0.7f);
		Box(seat, { 0.3f, 1.4f, 0.f }, { 0.18f, 1.0f, 0.25f }, blue);
		Box(seat, { 0.1f, 1.8f, 0.f }, { 0.45f, 0.2f, 0.25f }, blue * 1.15f);
		Cylinder(seat, At({ -0.05f, 1.55f, 0.f }), 0.012f, 0.3f, STEEL_GREY);
		Cylinder(seat, At({ -0.05f, 1.39f, 0.f }), 0.022f, 0.03f, { 0.75f, 0.65f, 0.3f });
		add("Machine: Seat Cutter", seat);
	}
	{
		MeshBuilder refacer;
		refacer.SetUVScale(3.f);
		const glm::vec3 grey = { 0.55f, 0.56f, 0.52f };
		Stand(refacer, { 0.6f, 0.6f }, 0.9f, { 0.35f, 0.36f, 0.40f });
		Box(refacer, { 0.08f, 1.0f, -0.05f }, { 0.35f, 0.18f, 0.3f }, grey);
		Cylinder(refacer, AlongX({ -0.12f, 1.02f, -0.1f }), 0.06f, 0.025f, { 0.75f, 0.72f, 0.65f }, 24);
		Box(refacer, { 0.05f, 1.12f, 0.1f }, { 0.25f, 0.05f, 0.06f }, DARK);
		add("Machine: Valve Refacer", refacer);
	}
	{
		// A long bed with the block clamped upside down on it, a head and tail stock at either end,
		// and the boring bar running through where the crank goes.
		MeshBuilder borer;
		borer.SetUVScale(3.f);
		const glm::vec3 maroon = { 0.50f, 0.16f, 0.14f };
		Box(borer, { 0.f, 0.42f, 0.f }, { 1.5f, 0.84f, 0.62f }, maroon * 0.75f);
		Box(borer, { 0.f, 0.86f, 0.f }, { 1.4f, 0.04f, 0.5f }, STEEL_GREY);
		for (float side : { -1.f, 1.f })
		{
			Box(borer, { side * 0.66f, 1.05f, 0.f }, { 0.16f, 0.4f, 0.4f }, maroon);
			Cylinder(borer, AlongX({ side * 0.66f, 1.04f, 0.f }), 0.05f, 0.18f, DARK);
		}
		Cylinder(borer, AlongX({ 0.f, 1.04f, 0.f }), 0.014f, 1.32f, STEEL_GREY);
		Box(borer, { -0.66f, 1.32f, 0.f }, { 0.22f, 0.14f, 0.3f }, maroon * 1.15f);
		add("Machine: Line Borer", borer);
	}
	{
		// A heavy wooden bench, with the sanding beam lying along it: a long hardwood beam with
		// sandpaper on its face and handles at either end.
		MeshBuilder bench;
		bench.SetUVScale(3.f);
		const glm::vec3 wood = { 0.55f, 0.38f, 0.22f };
		const glm::vec3 beam = { 0.68f, 0.50f, 0.30f };
		Box(bench, { 0.f, 0.74f, 0.f }, { 0.75f, 0.08f, 1.4f }, wood);
		for (float x : { -0.3f, 0.3f })
			for (float z : { -0.62f, 0.62f })
				Box(bench, { x, 0.35f, z }, { 0.09f, 0.7f, 0.09f }, wood * 0.8f);
		Box(bench, { 0.f, 0.18f, 0.f }, { 0.62f, 0.04f, 1.3f }, wood * 0.8f);
		Box(bench, { 0.28f, 0.82f, 0.f }, { 0.09f, 0.07f, 1.25f }, beam);
		Box(bench, { 0.28f, 0.784f, 0.f }, { 0.095f, 0.004f, 1.2f }, { 0.78f, 0.70f, 0.52f });
		for (float z : { -0.5f, 0.5f })
			Box(bench, { 0.28f, 0.88f, z }, { 0.035f, 0.05f, 0.12f }, DARK);
		add("Machine: Sanding Bench", bench);
	}
}

void ShopAssets::LoadOverlays()
{
	using namespace EngineLayout;

	// Each overlay sits a hair proud of the surface it skins, so it's what you see.
	{
		MeshBuilder bore;
		bore.SetUVScale(10.f);
		bore.AddTube({}, BORE_RADIUS + 0.0004f, BORE_RADIUS - 0.0003f, CYLINDER_DEPTH - 0.004f, 40);
		AddModel("Overlay: Bore", bore);
	}
	{
		MeshBuilder deck;
		deck.SetUVScale(6.f);
		for (int cylinder = 0; cylinder < 4; cylinder++)
			deck.AddPlateWithHole({ BoreX(cylinder), 0.0007f, 0.f }, { BORE_SPACING, BLOCK_WIDTH }, BORE_RADIUS + 0.0005f, 32);
		AddModel("Overlay: Block Deck", deck);
	}
	{
		MeshBuilder deck;
		deck.SetUVScale(6.f);
		deck.SetTransform(glm::rotate(glm::mat4(1.f), glm::pi<float>(), { 1.f, 0.f, 0.f }));
		for (int cylinder = 0; cylinder < 4; cylinder++)
			deck.AddPlateWithHole({ BoreX(cylinder), 0.0007f, 0.f }, { BORE_SPACING, BLOCK_WIDTH }, BORE_RADIUS + 0.0005f, 32);
		AddModel("Overlay: Head Deck", deck);
	}
	auto journal = [&](const std::string& name, float radius, float length)
	{
		MeshBuilder builder;
		builder.SetUVScale(12.f);
		builder.SetTransform(glm::rotate(glm::mat4(1.f), glm::half_pi<float>(), { 0.f, 0.f, 1.f }));
		builder.AddCylinder({}, radius, length, 28);
		AddModel(name, builder);
	};
	journal("Overlay: Main Journal", 0.0279f, 0.0236f);
	journal("Overlay: Rod Journal", 0.0244f, 0.0216f);
	journal("Overlay: Cam Journal", 0.0132f, 0.0156f);
}

void ShopAssets::LoadParts()
{
	using namespace EngineLayout;
	auto add = [&](PartType type, const MeshBuilder& builder) { AddModel(PartTypeName(type), builder); };

	{
		// Origin at the centre of the deck; the block hangs below it. Real bores, so pistons show through.
		MeshBuilder block;
		block.SetUVScale(4.f);
		float halfLength = BLOCK_LENGTH * 0.5f;
		float crankcaseHeight = -CRANK_Y - CYLINDER_DEPTH;

		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			block.ResetTransform().SetColor(WHITE).AddPlateWithHole({ BoreX(cylinder), 0.f, 0.f }, { BORE_SPACING, BLOCK_WIDTH }, BORE_RADIUS, 32);
			Tube(block, At({ BoreX(cylinder), -CYLINDER_DEPTH * 0.5f - 0.0005f, 0.f }), BORE_RADIUS + 0.004f, BORE_RADIUS, CYLINDER_DEPTH - 0.001f, { 0.85f, 0.85f, 0.87f }, 0.f, glm::two_pi<float>(), 32);
		}
		for (float side : { -1.f, 1.f })
		{
			Box(block, { side * (halfLength - 0.0075f), -0.005f, 0.f }, { 0.015f, 0.01f, BLOCK_WIDTH });
			Box(block, { side * (halfLength - 0.005f), -CYLINDER_DEPTH * 0.5f, 0.f }, { 0.01f, CYLINDER_DEPTH, BLOCK_WIDTH });
			Box(block, { 0.f, -CYLINDER_DEPTH * 0.5f, side * (BLOCK_WIDTH * 0.5f - 0.005f) }, { BLOCK_LENGTH, CYLINDER_DEPTH, 0.01f });
		}
		// The crankcase: open underneath for the crank, closed in by the skirts down each side, with
		// the cylinders opening into it from above. A bulkhead at each main journal (and at each end)
		// carries the upper half of that main's bearing saddle, cut into its bottom edge.
		float crankcaseMiddle = -CYLINDER_DEPTH - crankcaseHeight * 0.5f;
		const float CRANKCASE_HALF_WIDTH = 0.12f;
		const float SADDLE_RADIUS = 0.0295f;
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			block.ResetTransform().SetColor(WHITE).AddPlateWithHole({ BoreX(cylinder), -CYLINDER_DEPTH, 0.f }, { BORE_SPACING, CRANKCASE_HALF_WIDTH * 2.f }, BORE_RADIUS, 32);
			block.SetTransform(glm::rotate(glm::mat4(1.f), glm::pi<float>(), { 1.f, 0.f, 0.f }));
			block.AddPlateWithHole({ BoreX(cylinder), CYLINDER_DEPTH + 0.004f, 0.f }, { BORE_SPACING, CRANKCASE_HALF_WIDTH * 2.f }, BORE_RADIUS, 32);
		}
		block.ResetTransform();
		for (float side : { -1.f, 1.f })
			Box(block, { 0.f, crankcaseMiddle, side * (CRANKCASE_HALF_WIDTH - 0.005f) }, { BLOCK_LENGTH, crankcaseHeight, 0.01f });
		auto bulkhead = [&](float x, float thickness) {
			float saddleTop = CRANK_Y + SADDLE_RADIUS;
			float upperHeight = saddleTop - (-CYLINDER_DEPTH);
			Box(block, { x, (-CYLINDER_DEPTH + saddleTop) * 0.5f, 0.f }, { thickness, -upperHeight, CRANKCASE_HALF_WIDTH * 2.f - 0.02f });
			for (float side : { -1.f, 1.f })
			{
				float inner = SADDLE_RADIUS, outer = CRANKCASE_HALF_WIDTH - 0.01f;
				Box(block, { x, (saddleTop + CRANK_Y) * 0.5f, side * (inner + outer) * 0.5f }, { thickness, SADDLE_RADIUS, outer - inner });
			}
			Tube(block, AlongX({ x, CRANK_Y, 0.f }), SADDLE_RADIUS + 0.006f, SADDLE_RADIUS, thickness, { 0.85f, 0.85f, 0.87f },
				-glm::half_pi<float>(), glm::half_pi<float>(), 20);
		};
		for (int journal = 0; journal < 5; journal++)
			bulkhead(std::clamp(MainX(journal), -BLOCK_LENGTH * 0.5f + 0.012f, BLOCK_LENGTH * 0.5f - 0.012f), 0.018f);
		for (float side : { -1.f, 1.f })
			bulkhead(side * (BLOCK_LENGTH * 0.5f - 0.004f), 0.008f);
		// Core plug holes (the plugs are parts of their own) and the oil filter boss.
		for (int plug = 0; plug < 3; plug++)
			Cylinder(block, AlongZ({ (plug - 1) * 0.12f, -0.09f, BLOCK_WIDTH * 0.5f + 0.0005f }), 0.015f, 0.001f, DARK);
		Cylinder(block, AlongZ({ 0.12f, -0.2f, -0.13f }), 0.035f, 0.03f);
		add(PartType::BLOCK, block);
	}
	{
		// Origin at the centre of the head's deck face. Open on top (no cam cover) so the springs,
		// valve tips and camshaft can be seen working.
		MeshBuilder head;
		head.SetUVScale(4.f);
		float halfLength = BLOCK_LENGTH * 0.5f;
		float railHeight = HEAD_RAIL_TOP - HEAD_FLOOR_Y;
		Box(head, { 0.f, HEAD_FLOOR_Y * 0.5f, 0.f }, { BLOCK_LENGTH, HEAD_FLOOR_Y, 0.21f });
		for (float side : { -1.f, 1.f })
		{
			Box(head, { 0.f, HEAD_FLOOR_Y + railHeight * 0.5f, side * 0.1f }, { BLOCK_LENGTH, railHeight, 0.01f });
			Box(head, { side * (halfLength - 0.005f), HEAD_FLOOR_Y + railHeight * 0.5f, 0.f }, { 0.01f, railHeight, 0.21f });
		}
		// Cam towers between the cylinders; the caps over the cam bores are parts of their own.
		for (int journal = 0; journal < 5; journal++)
			Box(head, { CamTowerX(journal), (HEAD_FLOOR_Y + CAM_Y) * 0.5f, 0.f }, { 0.018f, CAM_Y - HEAD_FLOOR_Y, 0.05f });
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			float x = BoreX(cylinder);
			Cylinder(head, At({ x, HEAD_FLOOR_Y + 0.02f, 0.f }), 0.011f, 0.04f, { 0.8f, 0.8f, 0.82f });
			// Intake ports on -Z, exhaust on +Z.
			Cylinder(head, AlongZ({ x, 0.035f, -0.125f }), 0.018f, 0.04f);
			Cylinder(head, AlongZ({ x, 0.035f, 0.125f }), 0.016f, 0.04f, { 0.6f, 0.55f, 0.5f });
			for (bool exhaust : { false, true })
				Cylinder(head, At({ x, HEAD_FLOOR_Y + 0.0005f, ValveZ(exhaust) }), 0.0135f, 0.001f, { 0.5f, 0.5f, 0.52f });
		}
		add(PartType::CYLINDER_HEAD, head);
	}
	{
		// Origin on the cam axis, running along X like the crank.
		MeshBuilder cam;
		cam.SetUVScale(10.f);
		Cylinder(cam, AlongX({ 0.f, 0.f, 0.f }), 0.010f, 0.42f, { 0.8f, 0.8f, 0.82f });
		for (int journal = 0; journal < 5; journal++)
			Cylinder(cam, AlongX({ MainX(journal), 0.f, 0.f }), 0.013f, 0.016f, WHITE, 24);
		for (int cylinder = 0; cylinder < 4; cylinder++)
			for (bool exhaust : { false, true })
			{
				float x = BoreX(cylinder) + (exhaust ? 0.016f : -0.016f);
				Cylinder(cam, AlongX({ x, 0.f, 0.f }), CAM_BASE_RADIUS, 0.012f, { 0.85f, 0.85f, 0.88f }, 24);
				// The nose, turned to this lobe's phase so the valves open in firing order.
				cam.SetTransform(glm::rotate(glm::mat4(1.f), LobeAngle(cylinder, exhaust), { 1.f, 0.f, 0.f })).SetColor({ 0.85f, 0.85f, 0.88f });
				cam.AddBox({ x, CAM_BASE_RADIUS * 0.6f, 0.f }, { 0.012f, (CAM_BASE_RADIUS + MAX_VALVE_LIFT) * 1.2f, 0.022f });
				cam.ResetTransform();
			}
		// The nose the cam sprocket bolts to.
		Cylinder(cam, AlongX({ -0.218f, 0.f, 0.f }), 0.011f, 0.022f, { 0.45f, 0.45f, 0.48f }, 20);
		add(PartType::CAMSHAFT, cam);
	}
	for (bool exhaust : { false, true })
	{
		// Origin at the centre of the valve face; the stem points up.
		MeshBuilder valve;
		valve.SetUVScale(12.f);
		float headRadius = exhaust ? 0.0145f : 0.0165f;
		Cylinder(valve, At({ 0.f, 0.00125f, 0.f }), headRadius, 0.0025f, exhaust ? glm::vec3(0.75f, 0.65f, 0.55f) : WHITE, 24);
		Cylinder(valve, At({ 0.f, 0.0085f, 0.f }), 0.007f, 0.012f, WHITE, 16);
		Cylinder(valve, At({ 0.f, VALVE_STEM_LENGTH * 0.5f, 0.f }), 0.0035f, VALVE_STEM_LENGTH, WHITE, 12);
		add(exhaust ? PartType::EXHAUST_VALVE : PartType::INTAKE_VALVE, valve);
	}
	{
		// Origin at the bottom of the spring.
		MeshBuilder spring;
		spring.SetColor({ 0.35f, 0.38f, 0.45f });
		spring.AddHelix({}, 0.0115f, 0.0018f, SPRING_FREE_HEIGHT, 6.f, 14, 6);
		add(PartType::VALVE_SPRING, spring);
	}
	{
		MeshBuilder box;
		box.SetUVScale(2.2f);
		Box(box, { 0.f, 0.17f, 0.f }, { 0.48f, 0.34f, 0.38f });
		AddModel(ShopModels::PACKAGE_BOX, box);

		MeshBuilder crate;
		crate.SetUVScale(1.5f);
		Box(crate, { 0.f, 0.3f, 0.f }, { 0.78f, 0.56f, 0.58f }, { 0.85f, 0.85f, 0.85f });
		// Corner posts and slats standing proud of the panels.
		for (float x : { -0.39f, 0.39f })
			for (float z : { -0.29f, 0.29f })
				Box(crate, { x, 0.3f, z }, { 0.05f, 0.6f, 0.05f });
		for (float y : { 0.08f, 0.52f })
		{
			Box(crate, { 0.f, y, 0.295f }, { 0.8f, 0.06f, 0.02f });
			Box(crate, { 0.f, y, -0.295f }, { 0.8f, 0.06f, 0.02f });
		}
		Box(crate, { 0.f, 0.015f, 0.f }, { 0.8f, 0.03f, 0.6f }, { 0.6f, 0.6f, 0.6f });
		AddModel(ShopModels::PACKAGE_CRATE, crate);
	}
	{
		MeshBuilder gasket;
		gasket.SetUVScale(6.f);
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			gasket.ResetTransform().SetColor(WHITE).AddPlateWithHole({ BoreX(cylinder), GASKET_THICKNESS, 0.f }, { BORE_SPACING, BLOCK_WIDTH }, BORE_RADIUS + 0.0015f, 32);
			gasket.SetTransform(glm::rotate(glm::mat4(1.f), glm::pi<float>(), { 1.f, 0.f, 0.f }));
			gasket.AddPlateWithHole({ BoreX(cylinder), 0.f, 0.f }, { BORE_SPACING, BLOCK_WIDTH }, BORE_RADIUS + 0.0015f, 32);
		}
		add(PartType::HEAD_GASKET, gasket);
	}
	{
		// Origin on the crank axis.
		MeshBuilder crank;
		crank.SetUVScale(6.f);
		for (int journal = 0; journal < 5; journal++)
			Cylinder(crank, AlongX({ MainX(journal), 0.f, 0.f }), 0.0275f, 0.024f, WHITE, 24);
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			float x = BoreX(cylinder);
			float sign = ThrowSign(cylinder);
			Cylinder(crank, AlongX({ x, sign * CRANK_THROW, 0.f }), 0.024f, 0.022f, WHITE, 24);
			for (float side : { -1.f, 1.f })
			{
				float webX = x + side * 0.0215f;
				Box(crank, { webX, sign * CRANK_THROW * 0.5f, 0.f }, { 0.021f, CRANK_THROW + 0.05f, 0.06f }, { 0.75f, 0.75f, 0.78f });
				Box(crank, { webX, -sign * 0.045f, 0.f }, { 0.021f, 0.05f, 0.1f }, { 0.75f, 0.75f, 0.78f });
			}
		}
		Cylinder(crank, AlongX({ -0.23f, 0.f, 0.f }), 0.018f, 0.05f);
		Cylinder(crank, AlongX({ 0.225f, 0.f, 0.f }), 0.045f, 0.016f);
		add(PartType::CRANKSHAFT, crank);
	}
	{
		// Origin at the small-end (piston pin) centre; the big end hangs ROD_LENGTH below.
		MeshBuilder rod;
		rod.SetUVScale(8.f);
		// Just the upper half of the big end: the cap is a part of its own.
		Tube(rod, AlongX({ 0.f, 0.f, 0.f }), 0.014f, 0.0105f, 0.022f);
		Tube(rod, AlongX({ 0.f, -ROD_LENGTH, 0.f }), 0.032f, 0.0255f, 0.022f, WHITE, -glm::half_pi<float>(), glm::half_pi<float>(), 16);
		Box(rod, { 0.f, -ROD_LENGTH * 0.5f, 0.f }, { 0.016f, ROD_LENGTH - 0.05f, 0.022f });
		for (float side : { -1.f, 1.f })
			Box(rod, { 0.f, -ROD_LENGTH + 0.004f, side * 0.038f }, { 0.022f, 0.008f, 0.014f });
		add(PartType::CONNECTING_ROD, rod);
	}
	{
		// Origin at the big end's centre, like the bearing shells; the cap closes the lower half.
		MeshBuilder cap;
		cap.SetUVScale(8.f);
		Tube(cap, AlongX({}), 0.032f, 0.0255f, 0.022f, WHITE, glm::half_pi<float>(), glm::pi<float>() * 1.5f, 16);
		for (float side : { -1.f, 1.f })
		{
			Box(cap, { 0.f, -0.004f, side * 0.038f }, { 0.022f, 0.008f, 0.014f });
			Cylinder(cap, At({ 0.f, -0.002f, side * 0.038f }), 0.004f, 0.03f, STEEL_GREY, 10);
		}
		Box(cap, { 0.f, -0.03f, 0.f }, { 0.02f, 0.012f, 0.075f });
		add(PartType::ROD_CAP, cap);
	}
	{
		// Origin on the crank axis; the cap closes the bottom of the journal's saddle.
		MeshBuilder cap;
		cap.SetUVScale(6.f);
		Tube(cap, AlongX({}), 0.045f, 0.0295f, 0.018f, WHITE, glm::half_pi<float>(), glm::pi<float>() * 1.5f, 20);
		for (float side : { -1.f, 1.f })
		{
			Box(cap, { 0.f, -0.01f, side * 0.058f }, { 0.018f, 0.02f, 0.03f });
			Cylinder(cap, At({ 0.f, -0.005f, side * 0.058f }), 0.0055f, 0.04f, STEEL_GREY, 10);
		}
		add(PartType::MAIN_CAP, cap);
	}
	{
		// Origin on the cam axis: a bridge over the journal, bolted down either side of the cam.
		MeshBuilder cap;
		cap.SetUVScale(6.f);
		Box(cap, { 0.f, 0.014f, 0.f }, { 0.02f, 0.012f, 0.05f }, { 0.85f, 0.85f, 0.88f });
		add(PartType::CAM_CAP, cap);
	}
	{
		// Origin at the centre of the crown.
		MeshBuilder piston;
		piston.SetUVScale(8.f);
		Cylinder(piston, At({ 0.f, -0.0275f, 0.f }), 0.0395f, 0.055f, WHITE, 32);
		for (int groove = 0; groove < 3; groove++)
			Tube(piston, At({ 0.f, -0.006f - groove * 0.005f, 0.f }), 0.0396f, 0.0367f, 0.0015f, DARK, 0.f, glm::two_pi<float>(), 32);
		Cylinder(piston, AlongX({ 0.f, -PISTON_PIN_DEPTH, 0.f }), 0.0105f, 0.087f, STEEL_GREY);
		add(PartType::PISTON, piston);
	}
	{
		MeshBuilder ring;
		ring.SetUVScale(8.f);
		// Leave the end gap open, so you can see what you're filing.
		Tube(ring, At({}), 0.0403f, 0.0372f, 0.0025f, WHITE, 0.06f, glm::two_pi<float>() - 0.06f, 40);
		add(PartType::PISTON_RING, ring);
	}
	{
		// One half-shell each, origin on the journal's axis: the upper halves arch over it, the
		// lower ones cup under it.
		auto shell = [&](PartType type, float outer, float inner, float width, bool upper) {
			MeshBuilder builder;
			builder.SetUVScale(8.f);
			float start = upper ? -glm::half_pi<float>() + 0.01f : glm::half_pi<float>() + 0.01f;
			Tube(builder, AlongX({}), outer, inner, width, WHITE, start, start + glm::pi<float>() - 0.02f, 16);
			add(type, builder);
		};
		shell(PartType::MAIN_BEARING, 0.0295f, 0.0275f, 0.02f, true);
		shell(PartType::MAIN_BEARING_LOWER, 0.0295f, 0.0275f, 0.02f, false);
		shell(PartType::ROD_BEARING, 0.0255f, 0.024f, 0.018f, true);
		shell(PartType::ROD_BEARING_LOWER, 0.0255f, 0.024f, 0.018f, false);
	}
	LoadAncillaries();
}

EngineLayout::Mount EngineLayout::AncillaryMount(PartType type, int index)
{
	// Turns that stand a part built lying flat (its thin side along Y) up in place.
	const glm::quat FACING_REAR = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));		// +Y to +X
	const glm::quat FACING_FRONT = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));	// +Y to -X, +X to +Y
	const glm::quat FACING_INTAKE = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.f, 0.f, 0.f));	// +Y to +Z
	const glm::quat FACING_EXHAUST = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1.f, 0.f, 0.f));	// +Y to -Z

	// The intake and exhaust sides were swapped after the bolt-ons were laid out: what was laid out
	// on one side is turned end for end onto the other. A per-cylinder part only crosses over, so it
	// stays over its own cylinder.
	const glm::quat END_FOR_END = glm::angleAxis(glm::pi<float>(), glm::vec3(0.f, 1.f, 0.f));
	auto otherSide = [&](Mount mount) { return Mount{ { -mount.offset.x, mount.offset.y, -mount.offset.z }, END_FOR_END * mount.rotation }; };
	auto crossOver = [&](Mount mount) { return Mount{ { mount.offset.x, mount.offset.y, -mount.offset.z }, END_FOR_END * mount.rotation }; };

	int cylinder = index / 2;
	bool exhaust = index % 2 == 1;
	switch (type)
	{
		case PartType::CORE_PLUG:				return crossOver({ { (index - 1) * 0.12f, -0.09f, BLOCK_WIDTH * 0.5f + 0.0005f }, FACING_INTAKE });
		case PartType::REAR_MAIN_SEAL:			return { { -FRONT_X + 0.001f, CRANK_Y, 0.f }, FACING_REAR };
		case PartType::OIL_PUMP:				return { { FRONT_X - 0.001f, CRANK_Y, 0.f }, FACING_FRONT };
		case PartType::OIL_PICKUP:				return { { FRONT_X - 0.009f, CRANK_Y - 0.05f, 0.f } };
		case PartType::OIL_PAN_GASKET:			return { { 0.f, PAN_RAIL_Y - 0.001f, 0.f } };
		case PartType::OIL_PAN:					return { { 0.f, PAN_RAIL_Y - 0.002f, 0.f } };
		case PartType::OIL_DRAIN_PLUG:			return { { 0.12f, PAN_RAIL_Y - 0.002f - OIL_PAN_DEPTH - 0.008f, 0.f } };
		case PartType::OIL_FILTER:				return crossOver({ { 0.12f, -0.2f, -0.145f }, FACING_EXHAUST });
		case PartType::VALVE_STEM_SEAL:			return { { BoreX(cylinder), HEAD_DECK_Y + HEAD_FLOOR_Y, ValveZ(exhaust) } };
		case PartType::VALVE_RETAINER:			return { { BoreX(cylinder), HEAD_DECK_Y + HEAD_FLOOR_Y + SPRING_INSTALLED_HEIGHT, ValveZ(exhaust) } };
		case PartType::LIFTER:					return { { BoreX(cylinder), HEAD_DECK_Y + VALVE_FACE_Y + VALVE_STEM_LENGTH, ValveZ(exhaust) } };
		case PartType::CRANK_SPROCKET:			return { { TIMING_PLANE_X, CRANK_Y, 0.f } };
		case PartType::CAM_SPROCKET:			return { { TIMING_PLANE_X, CamAxisY(), 0.f } };
		case PartType::TIMING_BELT:			return { { TIMING_PLANE_X, (CRANK_Y + CamAxisY()) * 0.5f, 0.f }, FACING_FRONT };
		case PartType::BELT_IDLER:				return { { TIMING_PLANE_X + 0.012f, -0.05f, index == 0 ? 0.062f : -0.062f }, FACING_FRONT };
		case PartType::BELT_TENSIONER:			return { { TIMING_PLANE_X + 0.004f, -0.13f, -0.072f } };
		case PartType::TIMING_COVER_GASKET:		return { { TIMING_COVER_X + 0.0055f, TIMING_COVER_Y, 0.f }, FACING_FRONT };
		case PartType::TIMING_COVER:			return { { TIMING_COVER_X, TIMING_COVER_Y, 0.f }, FACING_FRONT };
		case PartType::FRONT_CRANK_SEAL:		return { { TIMING_COVER_X - 0.012f, CRANK_Y, 0.f }, FACING_FRONT };
		case PartType::CRANK_PULLEY:			return { { TIMING_COVER_X - 0.03f, CRANK_Y, 0.f } };
		case PartType::VALVE_COVER_GASKET:		return { { 0.f, HEAD_TOP_Y, 0.f } };
		case PartType::VALVE_COVER:				return { { 0.f, HEAD_TOP_Y + 0.003f, 0.f } };
		case PartType::SPARK_PLUG:				return { { BoreX(index), HEAD_DECK_Y + 0.012f, 0.f } };
		case PartType::IGNITION_COIL:			return { { BoreX(index), HEAD_DECK_Y + 0.088f, 0.f } };
		case PartType::INTAKE_MANIFOLD_GASKET:	return otherSide({ { 0.f, PORT_Y, PORT_FACE_Z + 0.001f }, FACING_INTAKE });
		case PartType::INTAKE_MANIFOLD:			return otherSide({ { 0.f, PORT_Y, PORT_FACE_Z + 0.002f } });
		case PartType::THROTTLE_BODY:			return otherSide({ { -0.212f, PORT_Y + 0.03f, PORT_FACE_Z + 0.137f } });
		case PartType::FUEL_INJECTOR:			return crossOver({ { BoreX(index), PORT_Y + 0.012f, PORT_FACE_Z + 0.04f } });
		case PartType::FUEL_RAIL:				return otherSide({ { 0.f, PORT_Y + 0.073f, PORT_FACE_Z + 0.04f } });
		case PartType::EXHAUST_MANIFOLD_GASKET:	return otherSide({ { 0.f, PORT_Y, -PORT_FACE_Z - 0.001f }, FACING_EXHAUST });
		case PartType::EXHAUST_MANIFOLD:		return otherSide({ { 0.f, PORT_Y, -PORT_FACE_Z - 0.002f } });
		case PartType::WATER_PUMP_GASKET:		return { { TIMING_COVER_X - 0.0125f, -0.04f, 0.06f }, FACING_FRONT };
		case PartType::WATER_PUMP:				return { { TIMING_COVER_X - 0.014f, -0.04f, 0.06f } };
		case PartType::THERMOSTAT:				return crossOver({ { -0.17f, 0.10f, PORT_FACE_Z + 0.001f }, FACING_INTAKE });
		case PartType::THERMOSTAT_HOUSING:		return crossOver({ { -0.17f, 0.10f, PORT_FACE_Z + 0.004f }, FACING_INTAKE });
		default:								return {};
	}
}

void ShopAssets::LoadAncillaries()
{
	using namespace EngineLayout;
	auto add = [&](PartType type, const MeshBuilder& builder) { AddModel(PartTypeName(type), builder); };

	const glm::vec3 GASKET = { 0.42f, 0.46f, 0.40f };
	const glm::vec3 RUBBER = { 0.12f, 0.11f, 0.10f };
	const glm::vec3 VITON = { 0.42f, 0.24f, 0.14f };
	const glm::vec3 BLACK = { 0.12f, 0.12f, 0.13f };
	const glm::vec3 CHAIN = { 0.32f, 0.32f, 0.34f };
	const glm::vec3 RUST = { 0.55f, 0.36f, 0.26f };
	const glm::vec3 PAINT_RED = { 0.78f, 0.18f, 0.15f };
	const glm::vec3 PAN_PAINT = { 0.30f, 0.30f, 0.33f };

	// A flat rectangular frame in the XZ plane, centred on the origin.
	auto frame = [&](MeshBuilder& builder, float length, float width, float band, float thickness, const glm::vec3& color, float y = 0.f) {
		Box(builder, { 0.f, y, (width - band) * 0.5f }, { length, thickness, band }, color);
		Box(builder, { 0.f, y, -(width - band) * 0.5f }, { length, thickness, band }, color);
		Box(builder, { (length - band) * 0.5f, y, 0.f }, { band, thickness, width - band * 2.f }, color);
		Box(builder, { -(length - band) * 0.5f, y, 0.f }, { band, thickness, width - band * 2.f }, color);
	};
	// A toothed wheel turning about X.
	auto sprocket = [&](MeshBuilder& builder, float radius, float width, int teeth) {
		Cylinder(builder, AlongX({}), radius - 0.002f, width, WHITE, 32);
		for (int tooth = 0; tooth < teeth; tooth++)
		{
			float angle = tooth * glm::two_pi<float>() / teeth;
			builder.SetTransform(glm::rotate(glm::mat4(1.f), angle, { 1.f, 0.f, 0.f })).SetColor(WHITE);
			builder.AddBox({ 0.f, radius, 0.f }, { width, 0.005f, 0.0035f });
			builder.ResetTransform();
		}
	};
	// A gasket with a port hole per cylinder, seen from both sides.
	auto portGasket = [&](MeshBuilder& builder, float holeRadius, const glm::vec3& color) {
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			builder.ResetTransform().SetColor(color).AddPlateWithHole({ BoreX(cylinder), 0.0008f, 0.f }, { BORE_SPACING, 0.05f }, holeRadius, 24);
			builder.SetTransform(glm::rotate(glm::mat4(1.f), glm::pi<float>(), { 1.f, 0.f, 0.f }));
			builder.AddPlateWithHole({ BoreX(cylinder), 0.0008f, 0.f }, { BORE_SPACING, 0.05f }, holeRadius, 24);
		}
		builder.ResetTransform();
	};

	{
		MeshBuilder plug;
		Cylinder(plug, At({ 0.f, 0.003f, 0.f }), 0.016f, 0.006f, STEEL_GREY, 24);
		add(PartType::CORE_PLUG, plug);
	}
	{
		MeshBuilder seal;
		Tube(seal, At({ 0.f, 0.004f, 0.f }), 0.048f, 0.036f, 0.008f, RUBBER, 0.f, glm::two_pi<float>(), 32);
		add(PartType::REAR_MAIN_SEAL, seal);
	}
	{
		// Lies face down; on the engine its back is against the block, round the crank snout.
		MeshBuilder pump;
		pump.SetUVScale(8.f);
		Tube(pump, At({ 0.f, 0.008f, 0.f }), 0.055f, 0.019f, 0.016f, WHITE, 0.f, glm::two_pi<float>(), 32);
		Cylinder(pump, At({ 0.f, 0.0163f, 0.f }), 0.034f, 0.0008f, DARK, 24);
		Box(pump, { -0.06f, 0.008f, 0.f }, { 0.03f, 0.016f, 0.026f });
		add(PartType::OIL_PUMP, pump);
	}
	{
		MeshBuilder pickup;
		Box(pickup, { 0.f, -0.003f, 0.f }, { 0.026f, 0.006f, 0.026f }, STEEL_GREY);
		Cylinder(pickup, At({ 0.f, -0.045f, 0.f }), 0.008f, 0.08f, STEEL_GREY, 12);
		Cylinder(pickup, AlongX({ 0.07f, -0.085f, 0.f }), 0.008f, 0.14f, STEEL_GREY, 12);
		Cylinder(pickup, At({ 0.17f, -0.088f, 0.f }), 0.04f, 0.012f, DARK, 24);
		add(PartType::OIL_PICKUP, pickup);
	}
	{
		MeshBuilder gasket;
		frame(gasket, BLOCK_LENGTH, 0.24f, 0.016f, 0.0015f, RUBBER);
		add(PartType::OIL_PAN_GASKET, gasket);
	}
	{
		// Rail on top at y = 0, the sump hanging below.
		MeshBuilder pan;
		pan.SetUVScale(3.f);
		float depth = OIL_PAN_DEPTH;
		frame(pan, BLOCK_LENGTH, 0.24f, 0.016f, 0.004f, PAN_PAINT, -0.002f);
		for (float side : { -1.f, 1.f })
		{
			Box(pan, { 0.f, -depth * 0.5f, side * 0.113f }, { BLOCK_LENGTH - 0.02f, depth, 0.004f }, PAN_PAINT);
			Box(pan, { side * (BLOCK_LENGTH * 0.5f - 0.012f), -depth * 0.5f, 0.f }, { 0.004f, depth, 0.226f }, PAN_PAINT);
		}
		Box(pan, { 0.f, -depth, 0.f }, { BLOCK_LENGTH - 0.02f, 0.005f, 0.226f }, PAN_PAINT);
		add(PartType::OIL_PAN, pan);
	}
	{
		MeshBuilder plug;
		Cylinder(plug, At({ 0.f, 0.004f, 0.f }), 0.0095f, 0.008f, STEEL_GREY, 6);
		Cylinder(plug, At({ 0.f, 0.014f, 0.f }), 0.006f, 0.012f, STEEL_GREY, 12);
		add(PartType::OIL_DRAIN_PLUG, plug);
	}
	{
		MeshBuilder filter;
		Cylinder(filter, At({ 0.f, 0.0425f, 0.f }), 0.038f, 0.085f, { 0.15f, 0.25f, 0.62f }, 28);
		Tube(filter, At({ 0.f, 0.003f, 0.f }), 0.034f, 0.026f, 0.006f, RUBBER, 0.f, glm::two_pi<float>(), 24);
		add(PartType::OIL_FILTER, filter);
	}
	// Every nut and bolt: origin under the head (or at the nut's bearing face), the shank down -Y.
	for (int type = 0; type < static_cast<int>(PartType::COUNT); type++)
	{
		const auto& fastener = PartCatalog::Get(static_cast<PartType>(type)).fastener;
		if (!fastener.IsFastener())
			continue;
		MeshBuilder builder;
		builder.SetUVScale(20.f);
		float across = fastener.headMm * 0.001f;
		float diameter = across * 0.7f;
		glm::vec3 color = CHAIN;
		if (fastener.nut)
			Tube(builder, At({ 0.f, across * 0.4f, 0.f }), across * 0.58f, diameter * 0.5f, across * 0.8f, color, 0.f, glm::two_pi<float>(), 6);
		else
		{
			auto size = static_cast<PartType>(type);
			float length = size == PartType::BOLT_M12 ? 0.08f : size == PartType::BOLT_M10 ? 0.06f : size == PartType::BOLT_M14 ? 0.05f
				: size == PartType::BOLT_M8 ? 0.03f : 0.022f;
			Cylinder(builder, At({ 0.f, across * 0.33f, 0.f }), across * 0.58f, across * 0.66f, color, 6);
			Cylinder(builder, At({ 0.f, -length * 0.5f, 0.f }), diameter * 0.5f, length, color, 10);
		}
		add(static_cast<PartType>(type), builder);
	}
	{
		MeshBuilder seal;
		Tube(seal, At({ 0.f, 0.0045f, 0.f }), 0.0065f, 0.0035f, 0.009f, VITON, 0.f, glm::two_pi<float>(), 16);
		add(PartType::VALVE_STEM_SEAL, seal);
	}
	{
		// Bottom on the spring, keepers poking down round the stem.
		MeshBuilder retainer;
		Cylinder(retainer, At({ 0.f, 0.0015f, 0.f }), 0.0105f, 0.003f, { 0.45f, 0.45f, 0.48f }, 16);
		Cylinder(retainer, At({ 0.f, -0.002f, 0.f }), 0.005f, 0.006f, { 0.6f, 0.6f, 0.62f }, 12);
		add(PartType::VALVE_RETAINER, retainer);
	}
	{
		// Origin under the crown, where the valve tip pushes; the skirt hangs down round the spring.
		MeshBuilder lifter;
		lifter.SetUVScale(10.f);
		Cylinder(lifter, At({ 0.f, 0.0015f, 0.f }), 0.0135f, 0.003f, WHITE, 24);
		Tube(lifter, At({ 0.f, -0.007f, 0.f }), 0.0135f, 0.0125f, 0.014f, WHITE, 0.f, glm::two_pi<float>(), 24);
		add(PartType::LIFTER, lifter);
	}
	{
		MeshBuilder crank;
		crank.SetUVScale(10.f);
		sprocket(crank, 0.022f, 0.010f, 18);
		Cylinder(crank, AlongX({ 0.f, 0.f, 0.f }), 0.012f, 0.016f, STEEL_GREY, 16);
		add(PartType::CRANK_SPROCKET, crank);
	}
	{
		MeshBuilder cam;
		cam.SetUVScale(10.f);
		sprocket(cam, 0.044f, 0.008f, 36);
		for (int hole = 0; hole < 4; hole++)
		{
			float angle = hole * glm::half_pi<float>() + 0.4f;
			Cylinder(cam, AlongX({ 0.f, std::cos(angle) * 0.025f, std::sin(angle) * 0.025f }), 0.008f, 0.0085f, DARK, 14);
		}
		add(PartType::CAM_SPROCKET, cam);
	}
	{
		// Lying flat: the crank end at -X, the cam end at +X. Stood up, -X points down. A wide black
		// toothed belt wrapped round both sprockets.
		MeshBuilder belt;
		belt.SetUVScale(20.f);
		const glm::vec3 BELT = { 0.09f, 0.09f, 0.10f };
		constexpr float WIDTH = 0.022f, THICK = 0.004f;
		float halfSpan = (CamAxisY() - CRANK_Y) * 0.5f;
		Tube(belt, At({ -halfSpan, 0.f, 0.f }), 0.0245f + THICK, 0.0245f, WIDTH, BELT, 0.f, glm::two_pi<float>(), 28);
		Tube(belt, At({ halfSpan, 0.f, 0.f }), 0.0465f + THICK, 0.0465f, WIDTH, BELT, 0.f, glm::two_pi<float>(), 40);
		for (float side : { -1.f, 1.f })
		{
			float dz = (0.0465f - 0.0245f) * side;
			float length = std::sqrt(halfSpan * halfSpan * 4.f + dz * dz);
			float angle = -std::atan2(dz, halfSpan * 2.f);
			belt.SetTransform(glm::rotate(At({ 0.f, 0.f, side * (0.0355f + THICK * 0.5f) }), angle, { 0.f, 1.f, 0.f })).SetColor(BELT);
			belt.AddBox({}, { length, WIDTH, THICK });
			belt.ResetTransform();
		}
		add(PartType::TIMING_BELT, belt);
	}
	{
		// A smooth pulley on its bearing, the belt running over its rim.
		MeshBuilder idler;
		Cylinder(idler, At({ 0.f, 0.012f, 0.f }), 0.022f, 0.024f, { 0.70f, 0.71f, 0.74f }, 28);
		Cylinder(idler, At({ 0.f, 0.025f, 0.f }), 0.009f, 0.003f, BLACK, 16);
		add(PartType::BELT_IDLER, idler);
	}
	{
		// A sprung arm on its pivot with a pulley on the end, pressing on the belt.
		MeshBuilder tensioner;
		Box(tensioner, { 0.f, 0.012f, 0.f }, { 0.035f, 0.024f, 0.025f }, { 0.45f, 0.45f, 0.48f });
		Box(tensioner, { 0.03f, 0.006f, 0.f }, { 0.04f, 0.008f, 0.014f }, STEEL_GREY);
		Cylinder(tensioner, At({ 0.05f, 0.012f, 0.f }), 0.018f, 0.022f, { 0.70f, 0.71f, 0.74f }, 24);
		add(PartType::BELT_TENSIONER, tensioner);
	}
	{
		MeshBuilder gasket;
		frame(gasket, 0.50f, 0.21f, 0.012f, 0.0015f, GASKET);
		add(PartType::TIMING_COVER_GASKET, gasket);
	}
	{
		// Lying face up. Stood on the front of the engine, +X points up and the rim faces the block.
		// The crank seal boss sits 0.19 m below the cover's middle, the water pump boss just above it.
		MeshBuilder cover;
		cover.SetUVScale(4.f);
		Box(cover, { 0.f, 0.006f, 0.f }, { 0.50f, 0.012f, 0.21f });
		for (float side : { -1.f, 1.f })
		{
			Box(cover, { 0.f, -0.011f, side * 0.102f }, { 0.50f, 0.022f, 0.006f });
			Box(cover, { side * 0.247f, -0.011f, 0.f }, { 0.006f, 0.022f, 0.21f });
		}
		Cylinder(cover, At({ CRANK_Y - TIMING_COVER_Y, 0.016f, 0.f }), 0.034f, 0.008f, WHITE, 28);
		Cylinder(cover, At({ -0.04f - TIMING_COVER_Y, 0.015f, 0.06f }), 0.046f, 0.006f, WHITE, 28);
		add(PartType::TIMING_COVER, cover);
	}
	{
		MeshBuilder seal;
		Tube(seal, At({ 0.f, 0.0035f, 0.f }), 0.030f, 0.021f, 0.007f, RUBBER, 0.f, glm::two_pi<float>(), 28);
		add(PartType::FRONT_CRANK_SEAL, seal);
	}
	{
		// Turns about X like the crank: hub on the snout, the inertia ring on its rubber layer.
		MeshBuilder pulley;
		pulley.SetUVScale(6.f);
		Cylinder(pulley, AlongX({ 0.f, 0.f, 0.f }), 0.028f, 0.03f, STEEL_GREY, 24);
		Tube(pulley, AlongX({ -0.004f, 0.f, 0.f }), 0.052f, 0.028f, 0.006f, WHITE, 0.f, glm::two_pi<float>(), 32);
		Tube(pulley, AlongX({ -0.008f, 0.f, 0.f }), 0.055f, 0.052f, 0.018f, RUBBER, 0.f, glm::two_pi<float>(), 32);
		Tube(pulley, AlongX({ -0.008f, 0.f, 0.f }), 0.066f, 0.055f, 0.018f, WHITE, 0.f, glm::two_pi<float>(), 32);
		for (float x : { -0.004f, -0.008f, -0.012f })
			Tube(pulley, AlongX({ x, 0.f, 0.f }), 0.0665f, 0.062f, 0.0015f, DARK, 0.f, glm::two_pi<float>(), 32);
		add(PartType::CRANK_PULLEY, pulley);
	}
	{
		MeshBuilder gasket;
		frame(gasket, BLOCK_LENGTH, 0.21f, 0.01f, 0.003f, RUBBER);
		add(PartType::VALVE_COVER_GASKET, gasket);
	}
	{
		MeshBuilder cover;
		cover.SetUVScale(4.f);
		for (float side : { -1.f, 1.f })
		{
			Box(cover, { 0.f, 0.025f, side * 0.1025f }, { BLOCK_LENGTH, 0.05f, 0.005f }, PAINT_RED);
			Box(cover, { side * (BLOCK_LENGTH * 0.5f - 0.0025f), 0.025f, 0.f }, { 0.005f, 0.05f, 0.21f }, PAINT_RED);
		}
		Box(cover, { 0.f, 0.052f, 0.f }, { BLOCK_LENGTH, 0.006f, 0.21f }, PAINT_RED);
		for (int cylinder = 0; cylinder < 4; cylinder++)
			Cylinder(cover, At({ BoreX(cylinder), 0.0555f, 0.f }), 0.014f, 0.001f, DARK, 20);
		Cylinder(cover, At({ 0.12f, 0.059f, 0.055f }), 0.018f, 0.008f, BLACK, 20);
		add(PartType::VALVE_COVER, cover);
	}
	{
		// Origin at the firing tip.
		MeshBuilder plug;
		Box(plug, { 0.003f, -0.001f, 0.f }, { 0.006f, 0.0015f, 0.002f }, STEEL_GREY);
		Cylinder(plug, At({ 0.f, 0.0095f, 0.f }), 0.007f, 0.019f, STEEL_GREY, 12);
		Cylinder(plug, At({ 0.f, 0.026f, 0.f }), 0.009f, 0.014f, { 0.7f, 0.7f, 0.72f }, 6);
		Cylinder(plug, At({ 0.f, 0.054f, 0.f }), 0.0055f, 0.042f, { 0.95f, 0.95f, 0.92f }, 16);
		Cylinder(plug, At({ 0.f, 0.078f, 0.f }), 0.003f, 0.006f, STEEL_GREY, 8);
		add(PartType::SPARK_PLUG, plug);
	}
	{
		MeshBuilder coil;
		Cylinder(coil, At({ 0.f, 0.0425f, 0.f }), 0.012f, 0.085f, BLACK, 16);
		Box(coil, { 0.f, 0.096f, 0.008f }, { 0.036f, 0.022f, 0.05f }, BLACK);
		Box(coil, { 0.f, 0.098f, 0.04f }, { 0.016f, 0.012f, 0.016f }, { 0.6f, 0.6f, 0.62f });
		add(PartType::IGNITION_COIL, coil);
	}
	{
		MeshBuilder gasket;
		portGasket(gasket, 0.019f, GASKET);
		add(PartType::INTAKE_MANIFOLD_GASKET, gasket);
	}
	{
		MeshBuilder gasket;
		portGasket(gasket, 0.017f, STEEL_GREY);
		add(PartType::EXHAUST_MANIFOLD_GASKET, gasket);
	}
	{
		// Origin at the middle of the head flange; runners out along +Z into the plenum.
		MeshBuilder manifold;
		manifold.SetUVScale(4.f);
		Box(manifold, { 0.f, 0.f, 0.006f }, { 0.40f, 0.06f, 0.012f });
		for (int cylinder = 0; cylinder < 4; cylinder++)
			Cylinder(manifold, AlongZ({ BoreX(cylinder), 0.f, 0.06f }), 0.018f, 0.10f, WHITE, 18);
		Box(manifold, { 0.f, 0.03f, 0.135f }, { 0.42f, 0.10f, 0.07f });
		add(PartType::INTAKE_MANIFOLD, manifold);
	}
	{
		// Bolts to the front of the plenum, bore along -X.
		MeshBuilder throttle;
		throttle.SetUVScale(6.f);
		Box(throttle, { -0.002f, 0.f, 0.f }, { 0.004f, 0.075f, 0.075f });
		Tube(throttle, AlongX({ -0.02f, 0.f, 0.f }), 0.033f, 0.025f, 0.035f, WHITE, 0.f, glm::two_pi<float>(), 28);
		Box(throttle, { -0.02f, 0.f, 0.f }, { 0.002f, 0.048f, 0.002f }, STEEL_GREY);
		Box(throttle, { -0.02f, 0.038f, 0.02f }, { 0.012f, 0.012f, 0.03f }, BLACK);
		add(PartType::THROTTLE_BODY, throttle);
	}
	{
		// Origin at the nozzle.
		MeshBuilder injector;
		Cylinder(injector, At({ 0.f, 0.005f, 0.f }), 0.004f, 0.01f, STEEL_GREY, 10);
		Cylinder(injector, At({ 0.f, 0.03f, 0.f }), 0.0075f, 0.04f, { 0.25f, 0.25f, 0.27f }, 14);
		Box(injector, { 0.f, 0.052f, 0.006f }, { 0.012f, 0.008f, 0.016f }, BLACK);
		add(PartType::FUEL_INJECTOR, injector);
	}
	{
		MeshBuilder rail;
		Cylinder(rail, AlongX({ 0.f, 0.f, 0.f }), 0.009f, 0.37f, STEEL_GREY, 14);
		for (int cylinder = 0; cylinder < 4; cylinder++)
			Cylinder(rail, At({ BoreX(cylinder), -0.01f, 0.f }), 0.009f, 0.012f, STEEL_GREY, 12);
		Cylinder(rail, AlongX({ -0.195f, 0.f, 0.f }), 0.006f, 0.02f, { 0.7f, 0.6f, 0.3f }, 10);
		add(PartType::FUEL_RAIL, rail);
	}
	{
		// Origin at the middle of the head flange; the primaries run out along -Z into a collector.
		MeshBuilder manifold;
		manifold.SetUVScale(3.f);
		Box(manifold, { 0.f, 0.f, -0.006f }, { 0.40f, 0.06f, 0.012f }, RUST);
		for (int cylinder = 0; cylinder < 4; cylinder++)
			Cylinder(manifold, AlongZ({ BoreX(cylinder), 0.f, -0.045f }), 0.017f, 0.066f, RUST, 16);
		Box(manifold, { 0.f, -0.02f, -0.09f }, { 0.32f, 0.05f, 0.05f }, RUST);
		Cylinder(manifold, At({ 0.06f, -0.08f, -0.09f }), 0.022f, 0.08f, RUST, 18);
		add(PartType::EXHAUST_MANIFOLD, manifold);
	}
	{
		MeshBuilder gasket;
		Tube(gasket, At({ 0.f, 0.00075f, 0.f }), 0.044f, 0.022f, 0.0015f, GASKET, 0.f, glm::two_pi<float>(), 28);
		add(PartType::WATER_PUMP_GASKET, gasket);
	}
	{
		// Body against the timing cover, the pulley out along -X.
		MeshBuilder pump;
		pump.SetUVScale(6.f);
		Cylinder(pump, AlongX({ -0.015f, 0.f, 0.f }), 0.04f, 0.03f, WHITE, 24);
		Cylinder(pump, AlongX({ -0.045f, 0.f, 0.f }), 0.012f, 0.03f, STEEL_GREY, 14);
		Tube(pump, AlongX({ -0.06f, 0.f, 0.f }), 0.05f, 0.012f, 0.012f, { 0.6f, 0.6f, 0.62f }, 0.f, glm::two_pi<float>(), 28);
		Cylinder(pump, At({ -0.012f, 0.045f, 0.f }), 0.012f, 0.03f, WHITE, 14);
		add(PartType::WATER_PUMP, pump);
	}
	{
		MeshBuilder thermostat;
		Tube(thermostat, At({ 0.f, 0.001f, 0.f }), 0.026f, 0.012f, 0.002f, STEEL_GREY, 0.f, glm::two_pi<float>(), 24);
		Cylinder(thermostat, At({ 0.f, -0.008f, 0.f }), 0.008f, 0.018f, { 0.75f, 0.62f, 0.3f }, 12);
		Box(thermostat, { 0.f, 0.006f, 0.f }, { 0.04f, 0.003f, 0.006f }, STEEL_GREY);
		add(PartType::THERMOSTAT, thermostat);
	}
	{
		MeshBuilder housing;
		housing.SetUVScale(6.f);
		Box(housing, { 0.f, 0.003f, 0.f }, { 0.06f, 0.006f, 0.05f });
		Cylinder(housing, At({ 0.f, 0.016f, 0.f }), 0.022f, 0.022f, WHITE, 20);
		Cylinder(housing, AlongX({ 0.035f, 0.022f, 0.f }), 0.015f, 0.05f, WHITE, 16);
		add(PartType::THERMOSTAT_HOUSING, housing);
	}
}

std::string ShopAssets::FurnitureModel(const FurnitureInfo& furniture)
{
	switch (furniture.kind)
	{
		case FurnitureKind::SHELVING:	return ShopModels::SHELF;
		case FurnitureKind::WORKBENCH:	return ShopModels::WORKBENCH;
		default:						return "Furniture: " + furniture.name;
	}
}

void ShopAssets::LoadFurniture()
{
	// Origins on the floor, fronts facing -Z like the shop's own furniture.
	const glm::vec3 black = { 0.1f, 0.1f, 0.11f };
	const glm::vec3 chrome = { 0.75f, 0.76f, 0.78f };
	for (const auto& furniture : FurnitureCatalog::All())
	{
		MeshBuilder builder;
		builder.SetUVScale(3.f);
		switch (furniture.kind)
		{
			case FurnitureKind::SHELVING:
			case FurnitureKind::WORKBENCH:
				continue;
			case FurnitureKind::TOOL_CART:
			{
				const glm::vec3 red = { 0.72f, 0.12f, 0.1f };
				Box(builder, { 0.f, 0.52f, 0.f }, { 0.7f, 0.64f, 0.44f }, red);
				for (int drawer = 0; drawer < 4; drawer++)
				{
					float y = 0.3f + drawer * 0.14f;
					Box(builder, { 0.f, y, -0.222f }, { 0.64f, 0.12f, 0.006f }, red * 1.1f);
					Box(builder, { 0.f, y + 0.03f, -0.228f }, { 0.3f, 0.015f, 0.01f }, chrome);
				}
				Box(builder, { 0.f, 0.86f, 0.f }, { 0.72f, 0.04f, 0.46f }, red * 0.8f);
				Box(builder, { 0.f, 0.89f, 0.f }, { 0.68f, 0.02f, 0.42f }, black);
				for (float x : { -0.3f, 0.3f })
					for (float z : { -0.17f, 0.17f })
						Cylinder(builder, AlongX({ x, 0.1f, z }), 0.05f, 0.04f, black, 14);
				Box(builder, { 0.f, 0.18f, 0.f }, { 0.7f, 0.04f, 0.44f }, red * 0.8f);
				Box(builder, { 0.42f, 0.8f, 0.f }, { 0.03f, 0.03f, 0.36f }, chrome);
				break;
			}
			case FurnitureKind::STOOL:
				Cylinder(builder, At({ 0.f, 0.64f, 0.f }), 0.17f, 0.05f, black, 24);
				for (int leg = 0; leg < 4; leg++)
				{
					float angle = leg * glm::half_pi<float>() + glm::quarter_pi<float>();
					Box(builder, { std::cos(angle) * 0.13f, 0.31f, std::sin(angle) * 0.13f }, { 0.025f, 0.62f, 0.025f }, chrome);
				}
				Tube(builder, At({ 0.f, 0.22f, 0.f }), 0.15f, 0.135f, 0.02f, chrome, 0.f, glm::two_pi<float>(), 24);
				break;
			case FurnitureKind::OFFICE_CHAIR:
				for (int spoke = 0; spoke < 5; spoke++)
				{
					float angle = spoke * glm::two_pi<float>() / 5.f;
					Box(builder, { std::cos(angle) * 0.14f, 0.06f, std::sin(angle) * 0.14f }, { 0.04f, 0.03f, 0.04f }, black);
					Box(builder, { std::cos(angle) * 0.07f, 0.08f, std::sin(angle) * 0.07f }, { 0.14f, 0.025f, 0.03f }, black);
					Cylinder(builder, AlongX({ std::cos(angle) * 0.26f, 0.03f, std::sin(angle) * 0.26f }), 0.03f, 0.03f, black, 10);
				}
				Cylinder(builder, At({ 0.f, 0.27f, 0.f }), 0.025f, 0.36f, chrome, 12);
				Box(builder, { 0.f, 0.47f, 0.f }, { 0.48f, 0.08f, 0.46f }, { 0.15f, 0.17f, 0.22f });
				Box(builder, { 0.f, 0.82f, 0.21f }, { 0.44f, 0.5f, 0.06f }, { 0.15f, 0.17f, 0.22f });
				Box(builder, { 0.f, 0.6f, 0.23f }, { 0.05f, 0.2f, 0.03f }, black);
				break;
			case FurnitureKind::OIL_DRUM:
			{
				const glm::vec3 blue = { 0.12f, 0.3f, 0.62f };
				Cylinder(builder, At({ 0.f, 0.44f, 0.f }), 0.29f, 0.88f, blue, 28);
				for (float y : { 0.29f, 0.59f })
					Cylinder(builder, At({ 0.f, y, 0.f }), 0.296f, 0.02f, blue * 0.8f, 28);
				Cylinder(builder, At({ 0.1f, 0.885f, 0.1f }), 0.03f, 0.012f, chrome, 12);
				break;
			}
			case FurnitureKind::TRASH_CAN:
				Cylinder(builder, At({ 0.f, 0.33f, 0.f }), 0.22f, 0.66f, { 0.3f, 0.32f, 0.3f }, 24);
				Tube(builder, At({ 0.f, 0.665f, 0.f }), 0.23f, 0.2f, 0.03f, { 0.25f, 0.27f, 0.25f }, 0.f, glm::two_pi<float>(), 24);
				Cylinder(builder, At({ 0.f, 0.6f, 0.f }), 0.2f, 0.01f, black, 24);
				break;
			case FurnitureKind::FLOOR_MAT:
				Box(builder, { 0.f, 0.007f, 0.f }, { 1.5f, 0.014f, 0.9f }, black);
				for (int ridge = 0; ridge < 9; ridge++)
					Box(builder, { 0.f, 0.015f, -0.36f + ridge * 0.09f }, { 1.44f, 0.004f, 0.02f }, black * 1.6f);
				break;
			case FurnitureKind::POTTED_PLANT:
			{
				const glm::vec3 green = { 0.2f, 0.48f, 0.18f };
				Cylinder(builder, At({ 0.f, 0.17f, 0.f }), 0.16f, 0.34f, { 0.68f, 0.36f, 0.22f }, 20);
				Cylinder(builder, At({ 0.f, 0.335f, 0.f }), 0.145f, 0.01f, { 0.25f, 0.17f, 0.1f }, 20);
				Cylinder(builder, At({ 0.f, 0.55f, 0.f }), 0.02f, 0.44f, { 0.35f, 0.25f, 0.12f }, 8);
				// Leaves: flat blades fanning out from the stem.
				for (int leaf = 0; leaf < 9; leaf++)
				{
					float angle = leaf * 2.4f;
					float height = 0.45f + (leaf % 3) * 0.13f;
					glm::vec3 out = { std::cos(angle), 0.f, std::sin(angle) };
					Box(builder, glm::vec3(0.f, height, 0.f) + out * 0.13f, glm::abs(out) * 0.22f + glm::vec3(0.06f, 0.012f, 0.06f), green * (0.85f + 0.05f * (leaf % 4)));
				}
				break;
			}
			case FurnitureKind::SHOP_FAN:
				Cylinder(builder, At({ 0.f, 0.02f, 0.f }), 0.2f, 0.04f, black, 20);
				Cylinder(builder, At({ 0.f, 0.6f, 0.f }), 0.02f, 1.16f, chrome, 10);
				Cylinder(builder, AlongZ({ 0.f, 1.2f, 0.04f }), 0.07f, 0.12f, black, 16);
				Tube(builder, AlongZ({ 0.f, 1.2f, -0.06f }), 0.27f, 0.255f, 0.1f, chrome, 0.f, glm::two_pi<float>(), 32);
				for (int blade = 0; blade < 3; blade++)
				{
					float angle = blade * glm::two_pi<float>() / 3.f;
					Box(builder, { std::cos(angle) * 0.13f, 1.2f + std::sin(angle) * 0.13f, -0.06f }, { 0.12f + std::abs(std::cos(angle)) * 0.08f, 0.12f + std::abs(std::sin(angle)) * 0.08f, 0.01f },
						{ 0.6f, 0.75f, 0.85f });
				}
				break;
			case FurnitureKind::GAS_PUMP:
			{
				const glm::vec3 red = { 0.78f, 0.12f, 0.1f };
				const glm::vec3 cream = { 0.93f, 0.9f, 0.82f };
				Box(builder, { 0.f, 0.05f, 0.f }, { 0.6f, 0.1f, 0.5f }, black);
				Box(builder, { 0.f, 0.85f, 0.f }, { 0.5f, 1.5f, 0.4f }, red);
				Box(builder, { 0.f, 1.2f, -0.205f }, { 0.36f, 0.36f, 0.01f }, cream);
				Box(builder, { 0.f, 1.2f, -0.212f }, { 0.02f, 0.14f, 0.004f }, black);
				Box(builder, { 0.f, 0.75f, -0.205f }, { 0.36f, 0.12f, 0.01f }, cream);
				Cylinder(builder, At({ 0.f, 1.75f, 0.f }), 0.06f, 0.2f, red * 0.8f, 16);
				Cylinder(builder, At({ 0.f, 1.95f, 0.f }), 0.17f, 0.24f, cream, 24);
				Cylinder(builder, At({ 0.f, 1.95f, 0.f }), 0.175f, 0.06f, red, 24);
				Box(builder, { 0.27f, 0.95f, 0.f }, { 0.06f, 0.16f, 0.08f }, chrome);
				Cylinder(builder, At({ 0.3f, 0.55f, 0.f }), 0.015f, 0.6f, black, 8);
				break;
			}
		}
		AddModel(FurnitureModel(furniture), builder);
	}
}
