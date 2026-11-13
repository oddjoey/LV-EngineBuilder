#include "ShopAssets.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "LV/Engine/Graphics/Models/MeshBuilder.h"
#include "LV/Engine/Graphics/Models/ModelManager.h"
#include "LV/Engine/Graphics/Models/TextureManager.h"
#include "../Simulation/EngineAssembly.h"
#include "../Simulation/PartCatalog.h"
#include "GarageLayout.h"
#include "ProceduralTextures.h"
#include "ToolShapes.h"

namespace
{
	const std::string MODEL_FOLDER = "Resources/Models/";

	// Models are built up of pieces laid against each other, and a detail (a stripe, a label, trim) often
	// ends flush with what it's on: two faces of different colours in one plane, which flicker as the
	// depth test picks one then the other. Each later one of those is lifted off the face under it by a
	// hair, scaled to the model so it holds from as far away as it'll be seen. Where three meet, the two lifted
	// meet again, so it goes round again.
	void SeparateCoplanarFaces(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, int rounds = 4)
	{
		struct Face { glm::vec3 normal; float distance; size_t first; };
		std::vector<Face> faces;
		glm::vec3 low(1e9f), high(-1e9f);
		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			const glm::vec3& a = vertices[indices[i]].position;
			glm::vec3 cross = glm::cross(vertices[indices[i + 1]].position - a, vertices[indices[i + 2]].position - a);
			for (int k = 0; k < 3; k++)
			{
				low = glm::min(low, vertices[indices[i + k]].position);
				high = glm::max(high, vertices[indices[i + k]].position);
			}
			float area = glm::length(cross);
			if (area > 1e-10f)
				faces.push_back({ cross / area, glm::dot(cross / area, a), i });
		}
		if (faces.empty())
			return;
		const float lift = std::clamp(glm::length(high - low) * 0.0002f, 0.0002f, 0.003f);

		// Faces that look the same way, in the same plane, overlapping by more than an edge.
		auto flat = [&](const Face& face, const glm::vec3& point) {
			glm::vec3 u = glm::normalize(glm::cross(face.normal, std::abs(face.normal.x) < 0.9f ? glm::vec3(1.f, 0.f, 0.f) : glm::vec3(0.f, 1.f, 0.f)));
			return glm::vec2(glm::dot(point, u), glm::dot(point, glm::cross(face.normal, u)));
		};
		auto overlap = [&](const Face& a, const Face& b) {
			glm::vec2 one[3], two[3];
			for (int k = 0; k < 3; k++)
			{
				one[k] = flat(a, vertices[indices[a.first + k]].position);
				two[k] = flat(a, vertices[indices[b.first + k]].position);
			}
			// Shrunk a little, so ones that only share an edge don't count.
			for (auto* triangle : { one, two })
			{
				glm::vec2 middle = (triangle[0] + triangle[1] + triangle[2]) / 3.f;
				for (int k = 0; k < 3; k++)
					triangle[k] = middle + (triangle[k] - middle) * 0.97f;
			}
			auto apart = [](const glm::vec2* edges, const glm::vec2* other) {
				for (int k = 0; k < 3; k++)
				{
					glm::vec2 along = edges[(k + 1) % 3] - edges[k], axis = { -along.y, along.x };
					float minA = 1e9f, maxA = -1e9f, minB = 1e9f, maxB = -1e9f;
					for (int m = 0; m < 3; m++)
					{
						minA = (std::min)(minA, glm::dot(axis, edges[m]));
						maxA = (std::max)(maxA, glm::dot(axis, edges[m]));
						minB = (std::min)(minB, glm::dot(axis, other[m]));
						maxB = (std::max)(maxB, glm::dot(axis, other[m]));
					}
					if (maxA <= minB || maxB <= minA)
						return true;
				}
				return false;
			};
			return !apart(one, two) && !apart(two, one);
		};

		// Grouped by which way they face, then along that by plane.
		std::map<std::array<int, 3>, std::vector<size_t>> byNormal;
		for (size_t f = 0; f < faces.size(); f++)
			byNormal[{ static_cast<int>(std::round(faces[f].normal.x * 1000.f)), static_cast<int>(std::round(faces[f].normal.y * 1000.f)),
				static_cast<int>(std::round(faces[f].normal.z * 1000.f)) }].push_back(f);
		std::vector<bool> lifted(vertices.size(), false);
		std::vector<glm::vec3> offset(vertices.size(), glm::vec3(0.f));
		for (auto& [key, group] : byNormal)
		{
			std::sort(group.begin(), group.end(), [&](size_t a, size_t b) { return faces[a].distance < faces[b].distance; });
			for (size_t i = 0; i < group.size(); i++)
				for (size_t j = i + 1; j < group.size() && faces[group[j]].distance - faces[group[i]].distance < 2e-5f; j++)
				{
					const Face& a = faces[(std::min)(group[i], group[j])];
					const Face& later = faces[(std::max)(group[i], group[j])];
					if (glm::length(vertices[indices[a.first]].color - vertices[indices[later.first]].color) < 1e-3f || !overlap(a, later))
						continue;
					for (int k = 0; k < 3; k++)
					{
						uint32_t vertex = indices[later.first + k];
						lifted[vertex] = true;
						offset[vertex] = later.normal * lift;
					}
				}
		}
		// The rest of each lifted face (the other half of its quad) goes with it.
		bool spread = true;
		while (spread)
		{
			spread = false;
			for (const Face& face : faces)
			{
				uint32_t corner[3] = { indices[face.first], indices[face.first + 1], indices[face.first + 2] };
				bool any = lifted[corner[0]] || lifted[corner[1]] || lifted[corner[2]];
				bool all = lifted[corner[0]] && lifted[corner[1]] && lifted[corner[2]];
				if (!any || all)
					continue;
				glm::vec3 shift = offset[lifted[corner[0]] ? corner[0] : lifted[corner[1]] ? corner[1] : corner[2]];
				if (glm::dot(glm::normalize(shift), face.normal) < 0.999f)
					continue;
				for (uint32_t vertex : corner)
					if (!lifted[vertex])
					{
						lifted[vertex] = true;
						offset[vertex] = shift;
						spread = true;
					}
			}
		}
		for (size_t v = 0; v < vertices.size(); v++)
			vertices[v].position += offset[v];
		if (rounds > 1 && std::find(lifted.begin(), lifted.end(), true) != lifted.end())
			SeparateCoplanarFaces(vertices, indices, rounds - 1);
	}
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

	// A belt laid along a path round its pulleys (EngineLayout::BeltPath, in Z and Y): a box from each point
	// to the next, its width along X, a little long so the pieces round a pulley meet.
	void BeltAlong(MeshBuilder& builder, const std::vector<glm::vec2>& path, float width, float thick, const glm::vec3& color)
	{
		for (size_t i = 0; i < path.size(); i++)
		{
			glm::vec2 from = path[i], to = path[(i + 1) % path.size()];
			glm::vec2 run = to - from, centre = (from + to) * 0.5f;
			builder.SetTransform(glm::rotate(At({ 0.f, centre.y, centre.x }), std::atan2(-run.y, run.x), { 1.f, 0.f, 0.f })).SetColor(color);
			builder.AddBox({}, { width, thick, glm::length(run) + thick * 0.5f });
			builder.ResetTransform();
		}
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


	// The deck is closed: solid round the tops of the bores, the water jacket under it reached only through small
	// coolant holes, a ring of them round each cylinder (and at the ends of the block), matched in the head gasket.
	// Each is x, z and radius.
	std::vector<glm::vec3> DeckCoolantHoles()
	{
		using namespace EngineLayout;
		std::vector<glm::vec3> holes;
		const float ring = BORE_RADIUS + 0.012f;
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			float x = BoreX(cylinder);
			for (float side : { -1.f, 1.f })
			{
				holes.push_back({ x, side * ring, 0.005f });
				for (float lean : { -1.f, 1.f })
				{
					float angle = glm::radians(35.f);
					holes.push_back({ x + lean * ring * std::sin(angle), side * ring * std::cos(angle), 0.0035f });
				}
			}
			// The end cylinders have the end of the block beside them, not another bore.
			if (cylinder == 0 || cylinder == 3)
				holes.push_back({ x + (cylinder == 0 ? -ring : ring), 0.f, 0.004f });
		}
		return holes;
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
			for (int cylinder = 0; cylinder < 4; cylinder++)
				add(FeatureNames::RodJournalWidth(cylinder), { BoreX(cylinder) + 0.011f, ThrowSign(cylinder) * CRANK_THROW, 0.02f });
			add(FeatureNames::STROKE, { -0.23f, 0.f, 0.f });
			break;
		case PartType::CONNECTING_ROD:
			add(FeatureNames::BIG_END, { 0.f, -ROD_LENGTH, 0.f });
			add(FeatureNames::CENTER_LENGTH, { 0.f, -ROD_LENGTH * 0.5f, 0.f });
			add(FeatureNames::BIG_END_WIDTH, { 0.011f, -ROD_LENGTH, 0.025f });
			add(FeatureNames::ROD_BEND, { 0.f, -ROD_LENGTH * 0.3f, 0.008f });
			add(FeatureNames::ROD_TWIST, { 0.008f, -ROD_LENGTH * 0.7f, 0.f });
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
			for (int journal = 0; journal < CAM_JOURNALS; journal++)
				add(FeatureNames::CamBore(journal), { CamJournalX(journal), CAM_Y, 0.f });
			for (bool exhaust : { false, true })
				add(FeatureNames::PortFace(exhaust), { 0.12f, PORT_Y, exhaust ? PORT_FACE_Z : -PORT_FACE_Z });
			break;
		case PartType::HEAD_GASKET:
			add(FeatureNames::COMPRESSED_THICKNESS, { BLOCK_LENGTH * 0.45f, 0.001f, 0.09f });
			break;
		case PartType::CAMSHAFT:
			for (int journal = 0; journal < CAM_JOURNALS; journal++)
				add(FeatureNames::CamJournal(journal), { CamJournalX(journal), 0.f, 0.f });
			for (int cylinder = 0; cylinder < 4; cylinder++)
				for (bool exhaust : { false, true })
					add(FeatureNames::LobeHeight(cylinder, exhaust), { BoreX(cylinder) + (exhaust ? 0.016f : -0.016f), 0.f, 0.f });
			add(FeatureNames::CAM_END_PLAY, { FRONT_X + 0.01f, 0.f, 0.f });
			add(FeatureNames::CAM_RUNOUT, { MainX(2), 0.f, 0.015f });
			add(FeatureNames::FUEL_PUMP_ECCENTRIC, { FUEL_PUMP_X, 0.f, 0.02f });
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
			add(FeatureNames::SPRING_SQUARENESS, { 0.012f, 0.042f, 0.f });
			break;
		case PartType::OIL_PUMP:
			add(FeatureNames::ROTOR_TIP_CLEARANCE, { 0.f, 0.016f, 0.026f });
			add(FeatureNames::ROTOR_SIDE_CLEARANCE, { 0.026f, 0.016f, 0.f });
			break;
		case PartType::OIL_PAN:				add(FeatureNames::RAIL_FLATNESS, { 0.15f, 0.f, 0.112f }); break;
		case PartType::ROCKER_ARM:			add(FeatureNames::ROCKER_BORE, { 0.f, 0.006f, 0.025f }); break;
		case PartType::ROCKER_SHAFT:		add(FeatureNames::ROCKER_SHAFT_DIAMETER, { BoreX(0), 0.f, 0.f }); break;
		case PartType::DISTRIBUTOR:			add(FeatureNames::SHAFT_PLAY, { 0.f, 0.01f, 0.022f }); break;
		case PartType::FLYWHEEL:			add(FeatureNames::FLYWHEEL_RUNOUT, { 0.09f, 0.014f, 0.f }); break;
		case PartType::EXHAUST_PIPE:		add(FeatureNames::FLANGE_FLATNESS, { 0.06f, -0.12f, -0.09f }); break;
		case PartType::CRANK_SPROCKET:		add(FeatureNames::ROOT_DIAMETER, { 0.f, 0.022f, 0.f }); break;
		case PartType::CAM_SPROCKET:		add(FeatureNames::ROOT_DIAMETER, { 0.f, 0.044f, 0.f }); break;
		case PartType::TIMING_BELT:		add(FeatureNames::BELT_TOOTH_HEIGHT, { 0.f, CamAxisY() + CAM_PULLEY_TEETH + 0.004f, 0.f }); break;
		case PartType::BELT_IDLER:			add(FeatureNames::IDLER_PLAY, { 0.f, 0.011f, 0.f }); break;
		case PartType::DRIVE_BELT_TENSIONER:	add(FeatureNames::IDLER_PLAY, { -0.01f, 0.f, 0.f }); break;
		case PartType::BELT_TENSIONER:		add(FeatureNames::TENSIONER_TRAVEL, { 0.018f, 0.028f, 0.008f }); break;
		case PartType::TIMING_COVER:		add(FeatureNames::FACE_FLATNESS, { 0.2f, -0.022f, 0.1f }); break;
		case PartType::CRANK_PULLEY:		add(FeatureNames::RING_RUNOUT, { -0.008f, 0.066f, 0.f }); break;
		case PartType::VALVE_COVER:			add(FeatureNames::RAIL_FLATNESS, { 0.18f, 0.f, 0.1f }); break;
		case PartType::SPARK_PLUG:			add(FeatureNames::ELECTRODE_GAP, { 0.003f, -0.02f, 0.f }); break;
		case PartType::INTAKE_MANIFOLD:		add(FeatureNames::FLANGE_FLATNESS, { 0.15f, 0.f, 0.f }); break;
		case PartType::CARBURETTOR:
			add(FeatureNames::THROTTLE_BORE, { 0.f, 0.076f, -0.015f });
			add(FeatureNames::JET_FLOW, { 0.045f, 0.03f, 0.f });
			break;
		case PartType::FUEL_INJECTOR:		add(FeatureNames::INJECTOR_FLOW, { 0.f, 0.003f, 0.f }); break;
		case PartType::EXHAUST_MANIFOLD:	add(FeatureNames::FLANGE_FLATNESS, { 0.15f, 0.f, 0.f }); break;
		case PartType::WATER_PUMP:			add(FeatureNames::SHAFT_PLAY, { -0.045f, 0.012f, 0.f }); break;
		case PartType::ALTERNATOR:			add(FeatureNames::SHAFT_PLAY, { 0.f, 0.032f, 0.f }); break;
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
		// Between the big end and the crank cheek beside it.
		anchors.push_back({ AssemblyCheck::ROD_SIDE_CLEARANCE, cylinder, true, { BoreX(cylinder) + 0.011f, PinY(cylinder), 0.f } });
		anchors.push_back({ AssemblyCheck::DECK_CLEARANCE, cylinder, false, { BoreX(cylinder), 0.f, 0.03f } });
		for (bool exhaust : { false, true })
			anchors.push_back({ AssemblyCheck::INSTALLED_HEIGHT, EngineAssembly::SpringIndex(cylinder, exhaust), false,
				{ BoreX(cylinder), HEAD_DECK_Y + HEAD_FLOOR_Y + 0.02f, ValveZ(exhaust) } });
	}
	for (int journal = 0; journal < spec.camJournals; journal++)
		anchors.push_back({ AssemblyCheck::CAM_CLEARANCE, journal, false, { CamTowerX(journal), HEAD_DECK_Y + CAM_Y, 0.f } });
	// The belts, pushed in the middle of the run the manual checks: the timing belt's tight side, the fan
	// belt's top run.
	glm::vec2 timing = BeltRunMiddle(TimingPulleys(), TIMING_CHECK_RUN), fan = BeltRunMiddle(DrivePulleys(), DRIVE_CHECK_RUN);
	anchors.push_back({ AssemblyCheck::BELT_DEFLECTION, 0, false, { TIMING_PLANE_X, timing.y, timing.x } });
	anchors.push_back({ AssemblyCheck::BELT_DEFLECTION, 1, false, { ACCESSORY_BELT_X, fan.y, fan.x } });
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
	const glm::vec3 orange = { 0.9f, 0.45f, 0.08f };

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
				// The tray, with a dark recess where each wrench lies (the wrenches are tools of their own:
				// WrenchSlot).
				Box(builder, { 0.f, 0.007f, 0.f }, { 0.3f, 0.014f, 0.22f }, { 0.15f, 0.2f, 0.45f });
				for (size_t i = 0; i < tool.sizes.size(); i++)
					Box(builder, { WrenchSlot(tool, i).position.x, 0.0143f, 0.f }, { 0.012f, 0.001f, WrenchLength(tool.sizes[i]) }, { 0.08f, 0.09f, 0.14f });
				break;
			case ToolShape::WRENCH:
			{
				// The shank along X, the ring at +X, the open jaws at -X.
				int size = tool.sizes.front();
				float length = WrenchLength(size), ring = WrenchRingRadius(size);
				Box(builder, { 0.f, 0.002f, 0.f }, { length - ring * 2.f, 0.004f, 0.006f + size * 0.0002f }, steel);
				Tube(builder, At({ length * 0.5f - ring, 0.003f, 0.f }), ring, size * 0.0005f, 0.006f, steel, 0.f, glm::two_pi<float>(), 16);
				for (float side : { -1.f, 1.f })
					Box(builder, { -length * 0.5f + ring * 0.6f, 0.0025f, side * (size * 0.0005f + ring * 0.25f) }, { ring * 1.2f, 0.005f, ring * 0.45f }, steel);
				break;
			}
			case ToolShape::MANUAL:
			{
				// A thick workshop book lying shut: navy covers, the page block between them, a spine down
				// the left edge and a band across the front cover.
				// The engine's book is navy, the car's chassis book red.
				const glm::vec3 cover = tool.manualBook == 1 ? glm::vec3(0.5f, 0.09f, 0.08f) : glm::vec3(0.12f, 0.18f, 0.36f);
				Box(builder, { 0.f, 0.0015f, 0.f }, { 0.21f, 0.003f, 0.28f }, cover);
				Box(builder, { 0.004f, 0.0155f, 0.f }, { 0.2f, 0.025f, 0.27f }, { 0.93f, 0.9f, 0.82f });
				Box(builder, { 0.f, 0.0295f, 0.f }, { 0.21f, 0.003f, 0.28f }, cover);
				Box(builder, { -0.1035f, 0.0155f, 0.f }, { 0.006f, 0.031f, 0.28f }, cover * 0.8f);
				Box(builder, { 0.005f, 0.0315f, -0.04f }, { 0.17f, 0.001f, 0.05f }, { 0.85f, 0.65f, 0.2f });
				break;
			}
			case ToolShape::STEEL_SQUARE:
				// An engineer's square lying flat: the thick stock and the thin blade at right angles.
				Box(builder, { -0.05f, 0.006f, 0.f }, { 0.016f, 0.012f, 0.12f }, dark);
				Box(builder, { 0.02f, 0.0015f, -0.052f }, { 0.13f, 0.003f, 0.016f }, steel);
				break;
			case ToolShape::ROD_ALIGNER:
				// A base, the upright plate the V-block reads against, and the mandrel the big end hangs on.
				Box(builder, { 0.f, 0.006f, 0.f }, { 0.20f, 0.012f, 0.12f }, dark);
				Box(builder, { -0.08f, 0.09f, 0.f }, { 0.012f, 0.16f, 0.12f }, steel);
				Cylinder(builder, AlongX({ 0.f, 0.05f, 0.f }), 0.02f, 0.14f, steel);
				Box(builder, { 0.06f, 0.05f, 0.f }, { 0.03f, 0.05f, 0.05f }, dark);
				break;
			// Cordless: in the shop's one make, orange and black. The battery pack is its own item,
			// clipped on (BatteryMountOn).
			case ToolShape::CORDLESS_RATCHET:
			{
				// A ratchet with a motor housing over the back of the handle.
				float length = DriveToolLength(tool);
				Box(builder, { 0.f, 0.006f, 0.f }, { length, 0.012f, 0.016f }, steel);
				Box(builder, { -length * 0.29f, 0.02f, 0.f }, { length * 0.42f, 0.04f, 0.036f }, orange);
				Box(builder, { -length * 0.29f, 0.041f, 0.f }, { length * 0.3f, 0.002f, 0.02f }, dark);
				Cylinder(builder, At({ length * 0.5f, 0.012f, 0.f }), 0.016f, 0.024f, steel);
				Box(builder, { length * 0.5f, -0.002f, 0.f }, { 0.0095f, 0.012f, 0.0095f }, steel);
				break;
			}
			case ToolShape::IMPACT:
				// Lying on its side: the body and its nose along X, the anvil out the front, and the grip
				// down from the body (the pack clips on under it).
				Box(builder, { 0.f, 0.03f, -0.04f }, { 0.17f, 0.06f, 0.07f }, orange);
				Box(builder, { -0.06f, 0.03f, -0.04f }, { 0.05f, 0.062f, 0.072f }, dark);
				Cylinder(builder, AlongX({ IMPACT_NOSE.x - 0.02f, IMPACT_NOSE.y, IMPACT_NOSE.z }), 0.024f, 0.02f, steel);
				Box(builder, { IMPACT_NOSE.x - 0.005f, IMPACT_NOSE.y, IMPACT_NOSE.z }, { 0.012f, 0.0127f, 0.0127f }, steel);
				Box(builder, { IMPACT_GRIP.x, IMPACT_GRIP.y, IMPACT_GRIP.z }, { 0.045f, 0.05f, 0.1f }, dark);
				Box(builder, { IMPACT_GRIP.x + 0.03f, IMPACT_GRIP.y, 0.012f }, { 0.014f, 0.02f, 0.024f }, dark);
				break;
			case ToolShape::BATTERY:
			{
				// The pack, a band round it in its size's colour (blue, green, red as it gets bigger), and
				// the rail on top.
				float body = BatteryBodyHeight(tool);
				glm::vec3 band = tool.capacityAh < 3.0 ? glm::vec3(0.2f, 0.4f, 0.8f) : tool.capacityAh < 5.0 ? glm::vec3(0.2f, 0.65f, 0.3f) : glm::vec3(0.8f, 0.2f, 0.15f);
				Box(builder, { 0.f, body * 0.5f, 0.f }, { BATTERY_LENGTH, body, BATTERY_WIDTH }, dark);
				Box(builder, { 0.f, body * 0.35f, 0.f }, { BATTERY_LENGTH + 0.002f, 0.012f, BATTERY_WIDTH + 0.002f }, band);
				Box(builder, { 0.01f, body + BATTERY_RAIL * 0.5f, 0.f }, { BATTERY_LENGTH * 0.6f, BATTERY_RAIL, 0.03f }, orange);
				break;
			}
			case ToolShape::CHARGER:
				// A cradle with a lamp at the front; the rapid one is bigger.
				Box(builder, { 0.f, CHARGER_HEIGHT * 0.5f, 0.f }, { tool.chargeAmps > 3.0 ? 0.16f : 0.14f, CHARGER_HEIGHT, 0.1f }, dark);
				Box(builder, { 0.f, CHARGER_HEIGHT - 0.002f, 0.f }, { BATTERY_LENGTH * 0.7f, 0.004f, 0.034f }, orange);
				Box(builder, { 0.05f, CHARGER_HEIGHT * 0.6f, 0.051f }, { 0.012f, 0.008f, 0.002f }, { 0.3f, 0.9f, 0.35f });
				break;
			case ToolShape::MARKER:
			case ToolShape::SPRAY_CAN:
			{
				// Lying along X: a marker's barrel in its colour with a cap and nib; a can, its body in
				// its colour, the shoulder, and the nozzle.
				glm::vec3 color = { ((tool.paintColor >> 16) & 0xFF) / 255.f, ((tool.paintColor >> 8) & 0xFF) / 255.f, (tool.paintColor & 0xFF) / 255.f };
				if (ToolShapeFor(tool) == ToolShape::MARKER)
				{
					Cylinder(builder, AlongX({ -0.01f, 0.f, 0.f }), 0.007f, 0.11f, color, 12);
					Cylinder(builder, AlongX({ 0.055f, 0.f, 0.f }), 0.0075f, 0.025f, dark, 12);
				}
				else
				{
					Cylinder(builder, AlongX({ -0.015f, 0.f, 0.f }), 0.033f, 0.15f, color, 20);
					Cylinder(builder, AlongX({ 0.07f, 0.f, 0.f }), 0.025f, 0.02f, steel, 20);
					Box(builder, { 0.088f, 0.f, 0.f }, { 0.016f, 0.014f, 0.014f }, face);
				}
				break;
			}
			case ToolShape::RAG:
				// A folded rag on a small tin of thinner.
				Cylinder(builder, At({ 0.f, 0.04f, 0.f }), 0.035f, 0.08f, steel, 20);
				Box(builder, { 0.f, 0.084f, 0.f }, { 0.08f, 0.008f, 0.06f }, { 0.85f, 0.8f, 0.7f });
				break;
			case ToolShape::PIN_PRESS:
			{
				// A C-frame lying on its side: the anvil cup at one jaw, the screw through the other with its
				// pusher and a tommy bar through its head.
				const glm::vec3 frame = { 0.2f, 0.32f, 0.62f };
				Box(builder, { -0.06f, 0.012f, 0.f }, { 0.024f, 0.024f, 0.14f }, frame);
				for (float z : { -0.06f, 0.06f })
					Box(builder, { 0.f, 0.012f, z }, { 0.12f, 0.024f, 0.02f }, frame);
				Cylinder(builder, AlongZ({ 0.045f, 0.012f, -0.045f }), 0.012f, 0.014f, steel, 16);
				Cylinder(builder, AlongZ({ 0.045f, 0.012f, 0.085f }), 0.006f, 0.1f, steel, 12);
				Cylinder(builder, AlongZ({ 0.045f, 0.012f, 0.03f }), 0.009f, 0.012f, steel, 16);
				Cylinder(builder, AlongX({ 0.045f, 0.012f, 0.135f }), 0.003f, 0.07f, steel, 8);
				break;
			}
			case ToolShape::RING_COMPRESSOR:
			{
				// A band of spring steel standing on its edge, its ratchet on one side with the key in it.
				Tube(builder, At({ 0.f, 0.03f, 0.f }), 0.045f, 0.043f, 0.06f, steel, 0.f, glm::two_pi<float>(), 28);
				Box(builder, { 0.054f, 0.03f, 0.f }, { 0.02f, 0.03f, 0.03f }, dark);
				Cylinder(builder, AlongX({ 0.08f, 0.03f, 0.f }), 0.004f, 0.04f, steel, 8);
				Box(builder, { 0.1f, 0.03f, 0.f }, { 0.008f, 0.008f, 0.05f }, dark);
				break;
			}
			// The project car's tools.
			case ToolShape::FUNNEL:
			{
				// A funnel stood on its spout: the wide cone opening upward, its rolled rim, the dark mouth inside, the
				// narrow spout below, and a tab on the rim to hang it by.
				const glm::vec3 plastic = { 0.95f, 0.55f, 0.1f };
				constexpr float SPOUT = 0.06f, CONE = 0.09f, MOUTH = 0.075f, NECK = 0.011f;
				constexpr int SEGMENTS = 24;
				builder.ResetTransform().SetColor(plastic);
				for (int i = 0; i < SEGMENTS; i++)
				{
					float a0 = glm::two_pi<float>() * i / SEGMENTS, a1 = glm::two_pi<float>() * (i + 1) / SEGMENTS;
					auto ring = [&](float angle, float radius, float y) { return glm::vec3(std::cos(angle) * radius, y, std::sin(angle) * radius); };
					glm::vec3 low0 = ring(a0, NECK, SPOUT), low1 = ring(a1, NECK, SPOUT);
					glm::vec3 high0 = ring(a0, MOUTH, SPOUT + CONE), high1 = ring(a1, MOUTH, SPOUT + CONE);
					// The cone's outside and, inside it a touch smaller, its darker inside.
					builder.SetColor(plastic).AddFlatTriangle(low0, high0, high1).AddFlatTriangle(low0, high1, low1);
					glm::vec3 in0 = ring(a0, MOUTH - 0.004f, SPOUT + CONE - 0.002f), in1 = ring(a1, MOUTH - 0.004f, SPOUT + CONE - 0.002f);
					glm::vec3 deep0 = ring(a0, NECK * 0.6f, SPOUT + 0.004f), deep1 = ring(a1, NECK * 0.6f, SPOUT + 0.004f);
					builder.SetColor(plastic * 0.45f).AddFlatTriangle(deep0, in1, in0).AddFlatTriangle(deep0, deep1, in1);
				}
				Tube(builder, At({ 0.f, SPOUT + CONE, 0.f }), MOUTH + 0.004f, MOUTH - 0.004f, 0.008f, plastic, 0.f, glm::two_pi<float>(), SEGMENTS);
				Tube(builder, At({ 0.f, SPOUT * 0.5f, 0.f }), NECK, NECK * 0.6f, SPOUT, plastic, 0.f, glm::two_pi<float>(), 12);
				Box(builder, { MOUTH + 0.018f, SPOUT + CONE - 0.004f, 0.f }, { 0.03f, 0.004f, 0.022f }, plastic);
				break;
			}
			case ToolShape::BLEEDER:
			{
				// A catch bottle and its coiled clear hose.
				Cylinder(builder, At({ 0.f, 0.04f, 0.f }), 0.025f, 0.08f, { 0.85f, 0.85f, 0.8f }, 16);
				Tube(builder, At({ 0.f, 0.085f, 0.f }), 0.03f, 0.026f, 0.008f, { 0.6f, 0.75f, 0.8f }, 0.f, glm::two_pi<float>(), 16);
				break;
			}
			case ToolShape::SPRING_COMPRESSOR:
			{
				// Two long screws with their hooks.
				for (float z : { -0.03f, 0.03f })
				{
					Cylinder(builder, AlongX({ 0.f, 0.008f, z }), 0.008f, 0.34f, steel, 10);
					Box(builder, { -0.13f, 0.02f, z }, { 0.02f, 0.04f, 0.02f }, orange);
					Box(builder, { 0.11f, 0.02f, z }, { 0.02f, 0.04f, 0.02f }, orange);
				}
				break;
			}
			case ToolShape::TIRE_IRONS:
			{
				for (float z : { -0.02f, 0.02f })
					Box(builder, { 0.f, 0.006f, z }, { 0.4f, 0.012f, 0.02f }, steel);
				Box(builder, { 0.2f, 0.012f, 0.f }, { 0.03f, 0.024f, 0.07f }, dark);
				break;
			}
			case ToolShape::TIMING_LIGHT:
			{
				// A pistol-grip strobe and its leads.
				Box(builder, { 0.02f, 0.03f, 0.f }, { 0.12f, 0.05f, 0.05f }, { 0.85f, 0.7f, 0.1f });
				Cylinder(builder, AlongX({ 0.09f, 0.03f, 0.f }), 0.025f, 0.03f, dark, 16);
				Box(builder, { -0.02f, 0.01f, 0.f }, { 0.03f, 0.02f, 0.09f }, dark);
				break;
			}
			case ToolShape::TIRE_GAUGE:
			{
				// A dial gauge on the end of an air chuck and its hose.
				Cylinder(builder, At({ -0.03f, 0.01f, 0.f }), 0.025f, 0.02f, steel, 20);
				Cylinder(builder, At({ -0.03f, 0.021f, 0.f }), 0.021f, 0.002f, face, 20);
				Cylinder(builder, AlongX({ 0.04f, 0.01f, 0.f }), 0.006f, 0.1f, dark, 8);
				break;
			}
			case ToolShape::ALIGNMENT_GAUGE:
			{
				// The bubble gauge on its magnet, and the folded toe bar.
				Box(builder, { -0.04f, 0.02f, 0.f }, { 0.07f, 0.04f, 0.06f }, { 0.1f, 0.3f, 0.7f });
				Cylinder(builder, AlongZ({ -0.04f, 0.04f, 0.f }), 0.015f, 0.062f, face, 16);
				Box(builder, { 0.05f, 0.006f, 0.f }, { 0.12f, 0.012f, 0.03f }, steel);
				break;
			}
			case ToolShape::SCREWDRIVER:
			{
				// A red handle and its flat blade.
				Cylinder(builder, AlongX({ -0.05f, 0.012f, 0.f }), 0.012f, 0.09f, { 0.75f, 0.12f, 0.1f }, 12);
				Cylinder(builder, AlongX({ 0.04f, 0.012f, 0.f }), 0.003f, 0.09f, steel, 8);
				Box(builder, { 0.088f, 0.012f, 0.f }, { 0.008f, 0.002f, 0.007f }, steel);
				break;
			}
			case ToolShape::VALVE_SPRING_COMPRESSOR:
			{
				// Built as it sits over a valve, its origin where the cup bears on the spring's retainer: the screw up through the
				// frame's top arm with a tommy bar through its head, the cup on its end; the frame out to the side (+Z), down past
				// the head and back in under it, its foot's pad pressing up on the valve's face.
				const glm::vec3 frame = { 0.75f, 0.2f, 0.16f };
				constexpr float REACH = 0.1f;		// out past the side of the head from the valve
				constexpr float TOP = 0.075f, FOOT = -0.125f;
				Tube(builder, At({ 0.f, 0.006f, 0.f }), 0.0155f, 0.0125f, 0.012f, steel, 0.f, glm::two_pi<float>(), 20);
				Cylinder(builder, At({ 0.f, 0.013f, 0.f }), 0.0155f, 0.002f, steel, 20);
				Cylinder(builder, At({ 0.f, 0.055f, 0.f }), 0.0045f, 0.085f, steel, 10);
				Cylinder(builder, AlongX({ 0.f, 0.096f, 0.f }), 0.003f, 0.075f, steel, 8);
				Box(builder, { 0.f, TOP, REACH * 0.5f }, { 0.018f, 0.012f, REACH + 0.009f }, frame);
				Box(builder, { 0.f, (TOP + FOOT) * 0.5f, REACH }, { 0.018f, TOP - FOOT + 0.012f, 0.012f }, frame);
				Box(builder, { 0.f, FOOT, REACH * 0.5f }, { 0.018f, 0.012f, REACH + 0.009f }, frame);
				Cylinder(builder, At({ 0.f, FOOT + 0.009f, 0.f }), 0.012f, 0.006f, steel, 16);
				break;
			}
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
	// Each line: <Model Name> = <file> [scale=0.001] [rotate=x,y,z] [offset=x,y,z] [height=0.9] [texture=file.png]
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
				else if (key == "height") entry.height = std::stof(value);
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
			// Fitted to a height: measured as it comes in, then brought in again scaled to it, its base on the
			// floor and its middle over the origin (then moved by any offset given).
			auto options = entry.options;
			if (entry.height > 0.f)
			{
				auto probeOptions = options;
				probeOptions.offset = {};
				::Model probe(name, path, probeOptions);
				glm::vec3 low = probe.GetBoundsMin(), high = probe.GetBoundsMax();
				float fit = high.y > low.y ? entry.height / (high.y - low.y) : 1.f;
				options.scale *= fit;
				options.offset = entry.options.offset - glm::vec3((low.x + high.x) * 0.5f, low.y, (low.z + high.z) * 0.5f) * fit;
			}
			m_models[name] = modelManager->CreateModel(::Model(name, path, options))->GetIndex();

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

	auto vertices = builder.GetVertices();
	SeparateCoplanarFaces(vertices, builder.GetIndices());
	m_models[name] = modelManager->CreateModel(::Model(name, vertices, builder.GetIndices()))->GetIndex();
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
			for (int journal = 0; journal < CAM_JOURNALS; journal++)
				overlays.push_back({ FeatureNames::CamJournal(journal), "Overlay: Cam Journal", { CamJournalX(journal), 0.f, 0.f } });
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
	add(ShopTextures::SAND, ProceduralTextures::Sand());
	add(ShopTextures::ASPHALT, ProceduralTextures::Asphalt());
	add(ShopTextures::SIDING, ProceduralTextures::Siding());
	add(ShopTextures::PLANKS, ProceduralTextures::Planks());
	add(ShopTextures::ROCK, ProceduralTextures::Rock());
	add(ShopTextures::FOLIAGE, ProceduralTextures::Foliage());

	add(ShopTextures::HONED, ProceduralTextures::Honed());
	add(ShopTextures::BORED, ProceduralTextures::Bored());
	add(ShopTextures::GLAZED, ProceduralTextures::Glazed());
	add(ShopTextures::WORN, ProceduralTextures::Worn());
	add(ShopTextures::SCORED, ProceduralTextures::Scored());
	add(ShopTextures::GROUND, ProceduralTextures::Ground());
	add(ShopTextures::POLISHED, ProceduralTextures::Polished());
	add(ShopTextures::SURFACE_GROUND, ProceduralTextures::SurfaceGround());

	// Reserved: a tile's image is only made once WallPaint first paints it, so the pool costs nothing at
	// start up. Clamped at their edges: a tile's paint mustn't blend into its opposite side at the seams.
	for (int slot = 0; slot < ShopTextures::PAINT_TILE_POOL; slot++)
	{
		std::string name = ShopTextures::PaintTile(slot);
		m_textures[name] = textureManager->ReserveTexture(name, ShopTextures::PAINT_TILE_PIXELS, ShopTextures::PAINT_TILE_PIXELS, true)->GetIndex();
	}
}

void ShopAssets::LoadEnvironment()
{
	auto add = [&](const std::string& name, const MeshBuilder& builder) { AddModel(name, builder); };

	{
		MeshBuilder floor;
		floor.SetUVScale(0.5f);
		// Out to the walls' outside faces and no further, so it meets the drive and apron without overlapping them.
		using namespace GarageLayout;
		Box(floor, { 0.f, -0.05f, 0.f }, { (HALF_WIDTH + WALL) * 2.f, 0.1f, (HALF_DEPTH + WALL) * 2.f });
		add(ShopModels::FLOOR, floor);
	}
	{
		// Only its front face is seen: the back is against the wall.
		MeshBuilder tile;
		Box(tile, { 0.5f, 0.5f, 0.f }, { 1.f, 1.f, 0.0001f });
		add(ShopModels::PAINT_TILE, tile);
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
				Box(shelf, { x, 0.85f, z }, { 0.04f, 1.7f, 0.04f }, upright);
		// The boards at ShopLayout::SHELF_BOARDS.
		for (float y : { 0.3f, 0.7f, 1.1f, 1.5f })
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
		// A shop crane: two legs on casters, a mast, and a boom reaching out over the legs on a hydraulic
		// ram, with a chain, the hook, and a leveller bar with a chain to each end of an engine. Its boom
		// runs along +Z (see ShopLayout::HOIST_HOOK).
		MeshBuilder hoist;
		hoist.SetUVScale(3.f);
		const glm::vec3 red = { 0.72f, 0.12f, 0.10f };
		const glm::vec3 chrome = { 0.82f, 0.83f, 0.86f };
		const glm::vec3 chain = { 0.38f, 0.38f, 0.40f };
		// From a to b, both at x = 0: a square bar thick across.
		auto beam = [&](const glm::vec3& a, const glm::vec3& b, float thick, const glm::vec3& color) {
			glm::vec3 run = b - a;
			hoist.SetTransform(glm::rotate(At((a + b) * 0.5f), std::atan2(-run.y, run.z), { 1.f, 0.f, 0.f })).SetColor(color);
			hoist.AddBox({}, { thick, thick, glm::length(run) });
			hoist.ResetTransform();
		};
		for (float x : { -0.36f, 0.36f })
		{
			Box(hoist, { x, 0.11f, 0.535f }, { 0.08f, 0.07f, 1.17f }, red);
			for (float z : { -0.02f, 1.08f })
				Cylinder(hoist, AlongX({ x, 0.04f, z }), 0.04f, 0.035f, RUBBER_BLACK, 12);
		}
		Box(hoist, { 0.f, 0.11f, -0.05f }, { 0.8f, 0.07f, 0.1f }, red);
		Box(hoist, { 0.f, 0.8f, 0.f }, { 0.09f, 1.4f, 0.09f }, red);
		Box(hoist, { 0.f, 0.24f, 0.09f }, { 0.1f, 0.2f, 0.16f }, red * 0.85f);
		beam({ 0.f, 1.48f, -0.12f }, { 0.f, 1.80f, 1.02f }, 0.08f, red);
		beam({ 0.f, 0.24f, 0.14f }, { 0.f, 0.95f, 0.28f }, 0.075f, DARK);
		beam({ 0.f, 0.95f, 0.28f }, { 0.f, 1.56f, 0.40f }, 0.035f, chrome);
		beam({ 0.f, 0.5f, 0.06f }, { 0.f, 0.85f, -0.45f }, 0.025f, STEEL_GREY);
		Box(hoist, { 0.f, 1.71f, 1.0f }, { 0.02f, 0.14f, 0.02f }, chain);
		Box(hoist, { 0.f, 1.625f, 1.0f }, { 0.045f, 0.035f, 0.025f }, DARK);
		Box(hoist, { 0.f, 1.585f, 1.0f }, { 0.04f, 0.03f, 0.42f }, red);
		for (float z : { 0.82f, 1.18f })
			Box(hoist, { 0.f, 1.525f, z }, { 0.015f, 0.1f, 0.015f }, chain);
		add(ShopModels::ENGINE_HOIST, hoist);
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
	{
		// Another player: a monkey in a cap, in pieces so it can walk and look about. The torso has its
		// feet at the origin, facing -Z; the head turns about the neck, and the arms and legs swing from
		// the shoulder and hip, hanging -Y. The fur, the bare skin (belly, face, hands, feet) and the cap
		// are pieces of their own, all white, so each player's colours (MonkeyLook) are tinted on.
		// Built cute: a big head on a small body with short legs, a big cap and a big curly tail. The
		// joints match PlayerAvatar's.
		const glm::vec3 detail = { 0.08f, 0.06f, 0.05f };	// eyes, nostrils and mouth: dark whatever the skin
		MeshBuilder player;
		player.SetUVScale(2.f);
		Box(player, { 0.f, 0.73f, 0.01f }, { 0.34f, 0.42f, 0.24f });
		Box(player, { 0.f, 0.96f, 0.f }, { 0.12f, 0.05f, 0.12f });
		add(ShopModels::PLAYER, player);

		MeshBuilder belly;
		Box(belly, { 0.f, 0.71f, -0.115f }, { 0.22f, 0.28f, 0.03f });
		add(ShopModels::PLAYER_BELLY, belly);

		// The tail, thick and long, curling up from the base of the back and over.
		MeshBuilder tail;
		Box(tail, { 0.f, 0.58f, 0.2f }, { 0.07f, 0.07f, 0.16f });
		Box(tail, { 0.f, 0.69f, 0.3f }, { 0.07f, 0.24f, 0.07f });
		Box(tail, { 0.f, 0.8f, 0.25f }, { 0.07f, 0.07f, 0.17f });
		Box(tail, { 0.f, 0.75f, 0.19f }, { 0.07f, 0.1f, 0.07f });
		add(ShopModels::PLAYER_TAIL, tail);

		// The head, face and cap are drawn at a normal monkey's size about the neck and scaled up
		// together, so the face stays where it belongs on the bigger head.
		constexpr float HEAD = 1.9f;
		auto headBox = [&](MeshBuilder& mesh, const glm::vec3& center, const glm::vec3& size, const glm::vec3& color = glm::vec3(1.f)) {
			Box(mesh, center * HEAD, size * HEAD, color);
		};

		// The head: a furry skull with a heavy brow; the face is its own piece.
		MeshBuilder head;
		headBox(head, { 0.f, 0.13f, 0.01f }, { 0.24f, 0.24f, 0.24f });
		headBox(head, { 0.f, 0.176f, -0.122f }, { 0.19f, 0.026f, 0.03f });
		add(ShopModels::PLAYER_HEAD, head);

		// The face: pale, a muzzle with its nostrils and mouth, and the ears. The eyes go on it.
		MeshBuilder face;
		headBox(face, { 0.f, 0.11f, -0.115f }, { 0.18f, 0.16f, 0.02f });
		for (float side : { -1.f, 1.f })
		{
			headBox(face, { side * 0.016f, 0.075f, -0.177f }, { 0.012f, 0.01f, 0.004f }, detail);
			headBox(face, { side * 0.135f, 0.13f, 0.f }, { 0.04f, 0.07f, 0.05f });
		}
		headBox(face, { 0.f, 0.06f, -0.14f }, { 0.13f, 0.08f, 0.07f });
		headBox(face, { 0.f, 0.04f, -0.176f }, { 0.08f, 0.006f, 0.004f }, detail);
		add(ShopModels::PLAYER_FACE, face);

		// Big round eyes under the brow: the whites, dark pupils a little in and down, and a glint on each.
		MeshBuilder eyes;
		for (float side : { -1.f, 1.f })
		{
			headBox(eyes, { side * 0.048f, 0.135f, -0.1265f }, { 0.056f, 0.054f, 0.004f }, { 0.97f, 0.97f, 0.95f });
			headBox(eyes, { side * 0.044f, 0.131f, -0.1292f }, { 0.032f, 0.038f, 0.002f }, detail);
			headBox(eyes, { side * 0.05f, 0.142f, -0.1306f }, { 0.01f, 0.01f, 0.001f }, { 1.f, 1.f, 1.f });
		}
		add(ShopModels::PLAYER_EYES, eyes);

		// The cap, about the neck like the head and bigger again than it, pulled down over the top of the head
		// to just above the brow, and worn backwards: its peak out over the back of the neck.
		MeshBuilder cap;
		headBox(cap, { 0.f, 0.245f, 0.01f }, { 0.29f, 0.09f, 0.29f });
		headBox(cap, { 0.f, 0.211f, 0.19f }, { 0.25f, 0.022f, 0.14f }, { 0.8f, 0.8f, 0.8f });
		add(ShopModels::PLAYER_CAP, cap);

		// Short furry arms from the shoulder, and the hands at the end of them.
		MeshBuilder arm;
		arm.SetUVScale(2.f);
		Box(arm, { 0.f, -0.21f, 0.f }, { 0.09f, 0.42f, 0.1f });
		add(ShopModels::PLAYER_ARM, arm);

		MeshBuilder hand;
		Box(hand, { 0.f, -0.46f, 0.f }, { 0.09f, 0.09f, 0.09f });
		add(ShopModels::PLAYER_HAND, hand);

		// Short legs bend at the knee: the thigh from the hip, the shin (and its bare foot) from the knee.
		MeshBuilder thigh;
		thigh.SetUVScale(2.f);
		Box(thigh, { 0.f, -0.13f, 0.f }, { 0.13f, 0.28f, 0.15f });
		add(ShopModels::PLAYER_THIGH, thigh);

		MeshBuilder shin;
		shin.SetUVScale(2.f);
		Box(shin, { 0.f, -0.12f, 0.f }, { 0.12f, 0.24f, 0.14f });
		add(ShopModels::PLAYER_SHIN, shin);

		MeshBuilder foot;
		Box(foot, { 0.f, -0.24f, -0.04f }, { 0.12f, 0.04f, 0.2f });
		add(ShopModels::PLAYER_FOOT, foot);
	}
	{
		// First person, the same monkey: the right arm's fur (stretched to reach the hand), the hand,
		// and the body below the view, its fur and its bare skin apart so each takes its colour.
		MeshBuilder sleeve;
		sleeve.SetUVScale(2.f);
		Box(sleeve, { 0.f, 0.f, -0.5f }, { 0.0425f, 0.085f, 1.f });
		add(ShopModels::VIEW_SLEEVE, sleeve);

		MeshBuilder hand;
		Box(hand, { 0.f, 0.f, -ShopModels::VIEW_HAND_LENGTH * 0.5f }, { 0.0425f, ShopModels::VIEW_HAND_THICKNESS, ShopModels::VIEW_HAND_LENGTH });
		add(ShopModels::VIEW_HAND, hand);

		// The legs, the torso up to the shoulders, and the left arm hanging (the right one is the sleeve
		// and hand); then the feet, belly and left hand.
		MeshBuilder body;
		body.SetUVScale(2.f);
		for (float x : { -0.1f, 0.1f })
			Box(body, { x, 0.26f, 0.f }, { 0.13f, 0.52f, 0.15f });
		Box(body, { 0.f, 0.73f, 0.f }, { 0.34f, 0.42f, 0.24f });
		Box(body, { 0.f, 0.96f, 0.f }, { 0.3f, 0.05f, 0.18f });
		Box(body, { -0.22f, 0.69f, 0.f }, { 0.09f, 0.42f, 0.1f });
		add(ShopModels::VIEW_BODY, body);

		MeshBuilder bodySkin;
		for (float x : { -0.1f, 0.1f })
			Box(bodySkin, { x, 0.02f, -0.04f }, { 0.12f, 0.04f, 0.2f });
		Box(bodySkin, { 0.f, 0.71f, -0.115f }, { 0.22f, 0.28f, 0.03f });
		Box(bodySkin, { -0.22f, 0.44f, 0.f }, { 0.09f, 0.09f, 0.09f });
		add(ShopModels::VIEW_BODY_SKIN, bodySkin);
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
		// Origin at the centre of the deck; the block hangs below it, its skirts past the crank. Real bores, so pistons
		// show through, in a closed deck with its coolant holes.
		MeshBuilder block;
		block.SetUVScale(4.f);
		float halfLength = BLOCK_LENGTH * 0.5f;
		float crankcaseHeight = -PAN_RAIL_Y - CYLINDER_DEPTH;

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
		const float CRANKCASE_HALF_WIDTH = CRANKCASE_WIDTH * 0.5f;
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
		Cylinder(block, AlongZ({ 0.12f, -0.2f, CRANKCASE_HALF_WIDTH + 0.01f }), 0.035f, 0.03f);	// on the side the filter went to (see AncillaryMount)
		for (const auto& hole : DeckCoolantHoles())
			Cylinder(block, At({ hole.x, -0.0004f, hole.y }), hole.z, 0.0012f, DARK, 12);
		// The water pump's coolant boss, cast out of the front corner on the intake side: its face flush with the front of
		// the block (the timing cover's ear seals on it), the pump's inlet and bolt holes in it, and the passage from it
		// running back along the side into the water jacket.
		{
			constexpr float INNER = BLOCK_WIDTH * 0.5f - 0.004f, DEPTH = 0.034f;
			Box(block, { FRONT_X + DEPTH * 0.5f, WATER_PUMP_Y, -(INNER + WATER_PUMP_EAR) * 0.5f },
				{ DEPTH, 2.f * WATER_PUMP_EAR_HALF, WATER_PUMP_EAR - INNER });
			Cylinder(block, AlongX({ FRONT_X + DEPTH + 0.03f, WATER_PUMP_Y, WATER_PUMP_Z + 0.006f }), 0.019f, 0.06f, WHITE, 20);
			Cylinder(block, AlongX({ FRONT_X + DEPTH + 0.0625f, WATER_PUMP_Y, WATER_PUMP_Z + 0.006f }), 0.013f, 0.005f, WHITE, 18);
			Cylinder(block, AlongX({ FRONT_X - 0.0003f, WATER_PUMP_Y, WATER_PUMP_Z }), WATER_PUMP_NOSE + 0.0005f, 0.0008f, DARK, 24);
			for (int bolt = 0; bolt < 4; bolt++)
			{
				float angle = glm::quarter_pi<float>() + bolt * glm::half_pi<float>();
				Cylinder(block, AlongX({ FRONT_X - 0.0003f, WATER_PUMP_Y + std::sin(angle) * WATER_PUMP_BOLT_RING,
					WATER_PUMP_Z + std::cos(angle) * WATER_PUMP_BOLT_RING }), 0.0035f, 0.0008f, DARK, 10);
			}
		}
		add(PartType::BLOCK, block);
	}
	{
		// Origin at the centre of the head's deck face. Open on top (no cam cover) so the springs,
		// valve tips and camshaft can be seen working. Cast aluminium: its machined faces (the deck, the rail
		// the valve cover sits on, the port faces, the tops of the rocker pedestals) a shade brighter than the
		// casting round them.
		MeshBuilder head;
		head.SetUVScale(4.f);
		const glm::vec3 CAST = { 0.86f, 0.86f, 0.88f };
		const glm::vec3 CHAMBER = { 0.60f, 0.58f, 0.56f };	// a little sooted
		const glm::vec3 SEAT = { 0.74f, 0.74f, 0.76f };		// hardened valve seats, cut bright
		const float halfLength = BLOCK_LENGTH * 0.5f;
		const float headWidth = 0.21f, halfWidth = headWidth * 0.5f;
		const float boresEnd = 2.f * BORE_SPACING;
		const float CHAMBER_DEPTH = 0.008f;
		const float chamberRadius = BORE_RADIUS - 0.001f;
		const float LIP = 0.004f;									// the machined rail under the valve cover
		const float wallTop = HEAD_RAIL_TOP - LIP;
		const float wallHeight = wallTop - HEAD_FLOOR_Y;
		const float CAM_BORE = 0.0146f;
		const float SHAFT_UNDER = HEAD_FLOOR_Y + 0.039f;				// the underside of the rocker shafts
		const float HEAD_BOLT_Z = 0.085f, ROCKER_SHAFT_Z = VALVE_Z + ROCKER_PIVOT_Z;
		auto headBoltX = [](int pair) { return std::clamp(MainX(pair), -0.19f, 0.19f); };

		// The deck: machined flat, a combustion chamber cast up into it over each bore (its plate faces down).
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			float x = BoreX(cylinder);
			head.ResetTransform().SetColor(WHITE);
			head.SetTransform(glm::rotate(glm::mat4(1.f), glm::pi<float>(), { 1.f, 0.f, 0.f })).AddPlateWithHole({ x, 0.f, 0.f }, { BORE_SPACING, headWidth }, chamberRadius, 32);
			head.ResetTransform();
			// Its wall and roof; in the roof, the two valve seats round their ports' throats and the plug hole between them.
			Tube(head, At({ x, (CHAMBER_DEPTH + 0.0002f) * 0.5f, 0.f }), chamberRadius + 0.002f, chamberRadius, CHAMBER_DEPTH - 0.0002f, CHAMBER,
				0.f, glm::two_pi<float>(), 32);
			Cylinder(head, At({ x, CHAMBER_DEPTH - 0.0002f, 0.f }), chamberRadius, 0.0004f, CHAMBER, 32);
			for (bool exhaust : { false, true })
			{
				float throat = exhaust ? 0.0115f : 0.013f;
				Tube(head, At({ x, CHAMBER_DEPTH - 0.0007f, ValveZ(exhaust) }), throat + 0.003f, throat, 0.0006f, SEAT, 0.f, glm::two_pi<float>(), 24);
				Cylinder(head, At({ x, CHAMBER_DEPTH - 0.0006f, ValveZ(exhaust) }), throat, 0.0004f, DARK, 24);
			}
			// The plug's hole, beside the valves where its tube comes in from the side.
			Cylinder(head, At({ PlugTip(cylinder).x, CHAMBER_DEPTH - 0.0006f, PlugTip(cylinder).z }), 0.0055f, 0.0004f, DARK, 16);
		}
		// The deck past the end bores, and its edges along the sides.
		for (float side : { -1.f, 1.f })
		{
			Box(head, { side * (boresEnd + halfLength) * 0.5f, CHAMBER_DEPTH * 0.5f, 0.f }, { halfLength - boresEnd, CHAMBER_DEPTH, headWidth });
			Box(head, { 0.f, CHAMBER_DEPTH * 0.5f, side * (halfWidth - 0.0005f) }, { 2.f * boresEnd, CHAMBER_DEPTH, 0.001f });
		}
		// Through the deck: the coolant holes the gasket and block match, and the head bolts' holes.
		for (const auto& hole : DeckCoolantHoles())
			Cylinder(head, At({ hole.x, -0.0004f, hole.y }), hole.z, 0.0012f, DARK, 12);
		for (int pair = 0; pair < 5; pair++)
			for (float side : { -1.f, 1.f })
				Cylinder(head, At({ headBoltX(pair), -0.0004f, side * HEAD_BOLT_Z }), 0.0065f, 0.0012f, DARK, 16);

		// The body up to the floor the springs sit on: the ports run through it, out to the faces either side. Each spark
		// plug's tube runs through it too, from the chamber's roof up and out through the wall on the plug side: round the
		// tube the body's cast away (a pocket under the floor), so it's open all the way down to the plug.
		// A box from (x0, y0, u0) to (x1, y1, u1), u being across the head towards the plugs' side.
		auto span = [&](float x0, float x1, float y0, float y1, float u0, float u1) {
			if (x1 > x0 && y1 > y0 && u1 > u0)
				Box(head, { (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, PLUG_SIDE * (u0 + u1) * 0.5f }, { x1 - x0, y1 - y0, u1 - u0 }, CAST);
		};
		{
			const float tanTilt = std::tan(PLUG_TILT);
			float from = -halfLength;
			for (int cylinder = 0; cylinder < 4; cylinder++)
			{
				glm::vec3 tip = PlugTip(cylinder);
				float b0 = tip.x - PLUG_TUBE, b1 = tip.x + PLUG_TUBE, tipU = std::abs(tip.z);
				span(from, b0, CHAMBER_DEPTH, HEAD_FLOOR_Y, -halfWidth, halfWidth);
				// Along the tube: the body short of it, the pocket's bottom over the chamber, its face on the side, and the
				// floor over it except where the tube comes up through.
				float pocket = tipU - PLUG_TUBE;
				float up = tipU + (HEAD_FLOOR_Y - tip.y) * tanTilt, across = PLUG_TUBE / std::cos(PLUG_TILT);
				span(b0, b1, CHAMBER_DEPTH, HEAD_FLOOR_Y, -halfWidth, pocket);
				span(b0, b1, CHAMBER_DEPTH, CHAMBER_DEPTH + 0.0005f, pocket, halfWidth);
				span(b0, b1, CHAMBER_DEPTH, HEAD_FLOOR_Y, halfWidth - 0.001f, halfWidth);
				span(b0, b1, HEAD_FLOOR_Y - 0.002f, HEAD_FLOOR_Y, pocket, up - across);
				span(b0, b1, HEAD_FLOOR_Y - 0.002f, HEAD_FLOOR_Y, up + across, halfWidth - 0.001f);
				from = b1;
			}
			span(from, halfLength, CHAMBER_DEPTH, HEAD_FLOOR_Y, -halfWidth, halfWidth);
		}
		for (float side : { -1.f, 1.f })
		{
			bool exhaust = side > 0.f;
			// The port face, machined for the manifold's gasket, standing a whisker proud of the casting.
			Box(head, { 0.f, PORT_Y, side * (halfWidth + 0.00025f) }, { 2.f * boresEnd - 0.01f, 0.036f, 0.0005f }, WHITE);
			for (int cylinder = 0; cylinder < 4; cylinder++)
			{
				float x = BoreX(cylinder);
				glm::vec3 at = { x, PORT_Y, side * (halfWidth + 0.0006f) };
				if (exhaust)
					Cylinder(head, AlongZ(at), 0.0125f, 0.0004f, DARK, 20);
				else
				{
					// The intakes are wider than they're tall: a rounded slot.
					Box(head, at, { 0.012f, 0.026f, 0.0004f }, DARK);
					for (float end : { -1.f, 1.f })
						Cylinder(head, AlongZ(at + glm::vec3(end * 0.006f, 0.f, 0.f)), 0.013f, 0.0004f, DARK, 20);
				}
				// The manifold's studs above and below each port.
				for (float up : { -1.f, 1.f })
					Cylinder(head, AlongZ({ x + 0.03f, PORT_Y + up * 0.012f, side * (halfWidth + 0.0006f) }), 0.003f, 0.0004f, DARK, 10);
			}
		}

		// The walls up round the valvetrain, and the machined rail on top of them the valve cover's gasket sits on.
		for (float side : { -1.f, 1.f })
		{
			// On the plugs' side, a slot through the wall for each plug's tube.
			if (side == PLUG_SIDE)
			{
				const float tanTilt = std::tan(PLUG_TILT);
				float from = -halfLength;
				for (int cylinder = 0; cylinder < 4; cylinder++)
				{
					glm::vec3 tip = PlugTip(cylinder);
					float b0 = tip.x - PLUG_TUBE, b1 = tip.x + PLUG_TUBE;
					float slotTop = tip.y + (halfWidth - std::abs(tip.z)) / tanTilt + PLUG_TUBE / std::sin(PLUG_TILT) + 0.001f;
					span(from, b0, HEAD_FLOOR_Y, wallTop, halfWidth - 0.01f, halfWidth);
					span(b0, b1, slotTop, wallTop, halfWidth - 0.01f, halfWidth);
					from = b1;
				}
				span(from, halfLength, HEAD_FLOOR_Y, wallTop, halfWidth - 0.01f, halfWidth);
			}
			else
				Box(head, { 0.f, HEAD_FLOOR_Y + wallHeight * 0.5f, side * (halfWidth - 0.005f) }, { BLOCK_LENGTH, wallHeight, 0.01f }, CAST);
			Box(head, { side * (halfLength - 0.005f), HEAD_FLOOR_Y + wallHeight * 0.5f, 0.f }, { 0.01f, wallHeight, headWidth }, CAST);
			Box(head, { 0.f, wallTop + LIP * 0.5f, side * (halfWidth - 0.0055f) }, { BLOCK_LENGTH + 0.002f, LIP, 0.013f });
			Box(head, { side * (halfLength - 0.0055f), wallTop + LIP * 0.5f, 0.f }, { 0.013f, LIP, headWidth + 0.002f });
		}
		// Ribs cast down the exhaust side's wall, stiffening it between the head bolts.
		for (int pair = 1; pair < 4; pair++)
			Box(head, { headBoltX(pair), HEAD_FLOOR_Y + wallHeight * 0.5f, halfWidth + 0.0015f }, { 0.006f, wallHeight, 0.003f }, CAST);
		// The coolant outlet's pad, at the front of the intake side, the thermostat and its housing over it.
		Cylinder(head, AlongZ({ -0.17f, 0.095f, -(halfWidth + 0.00025f) }), 0.036f, 0.0005f, WHITE, 28);
		Cylinder(head, AlongZ({ -0.17f, 0.095f, -(halfWidth + 0.0006f) }), 0.011f, 0.0004f, DARK, 20);
		// The fuel pump's mounting pad on the intake side, just under the cam (all of it below the valve cover's joint), the
		// hole its arm goes in through up onto the cam's eccentric, and its two bolt holes either side.
		Box(head, { FUEL_PUMP_X, CAM_Y - FUEL_PUMP_DROP, -(halfWidth + 0.00025f) }, { 0.07f, 0.032f, 0.0005f }, WHITE);
		Cylinder(head, AlongZ({ FUEL_PUMP_X, CAM_Y - FUEL_PUMP_DROP, -(halfWidth + 0.0006f) }), 0.009f, 0.0004f, DARK, 18);
		for (float side : { -1.f, 1.f })
			Cylinder(head, AlongZ({ FUEL_PUMP_X + side * 0.027f, CAM_Y - FUEL_PUMP_DROP, -(halfWidth + 0.0006f) }), 0.0035f, 0.0004f, DARK, 10);
		// The cam's seal boss out the front, under the cam, and a lifting eye at each end on opposite sides.
		Box(head, { -(halfLength + 0.002f), CAM_Y - 0.013f, 0.f }, { 0.004f, 0.026f, 0.05f }, CAST);
		for (float end : { -1.f, 1.f })
		{
			glm::vec3 eye = { end * (halfLength + 0.003f), 0.098f, end * -0.07f };
			Box(head, eye, { 0.006f, 0.034f, 0.022f }, CAST);
			Cylinder(head, AlongX(eye + glm::vec3(0.f, 0.006f, 0.f)), 0.0045f, 0.0066f, DARK, 14);
		}

		// On the floor: the head bolts' bosses (their heads seat on top), the oil's drain-backs to the sump at the
		// corners, and the rocker shafts' pedestals, one under each bolt, either side.
		for (int pair = 0; pair < 5; pair++)
			for (float side : { -1.f, 1.f })
				Cylinder(head, At({ headBoltX(pair), HEAD_FLOOR_Y + 0.005f, side * HEAD_BOLT_Z }), 0.0095f, 0.01f, CAST, 20);
		for (float end : { -1.f, 1.f })
			for (float side : { -1.f, 1.f })
				Cylinder(head, At({ end * 0.165f, HEAD_FLOOR_Y + 0.0002f, side * 0.068f }), 0.006f, 0.0006f, DARK, 14);
		for (int hole = 0; hole < 5; hole++)
			for (float side : { -1.f, 1.f })
			{
				float x = RockerBoltX(hole), z = side * ROCKER_SHAFT_Z;
				Box(head, { x, (HEAD_FLOOR_Y + SHAFT_UNDER - 0.001f) * 0.5f, z }, { 0.014f, SHAFT_UNDER - 0.001f - HEAD_FLOOR_Y, 0.016f }, CAST);
				Box(head, { x, SHAFT_UNDER - 0.0005f, z }, { 0.0142f, 0.001f, 0.0162f });
			}
		// Cam towers under the cam's journals, each with its half of the bearing bore cut in its top; the caps
		// over the cam are parts of their own.
		for (int journal = 0; journal < CAM_JOURNALS; journal++)
		{
			float x = CamTowerX(journal);
			float saddle = CAM_Y - CAM_BORE;
			Box(head, { x, (HEAD_FLOOR_Y + saddle) * 0.5f, 0.f }, { 0.018f, saddle - HEAD_FLOOR_Y, 0.05f }, CAST);
			for (float side : { -1.f, 1.f })
				Box(head, { x, (saddle + CAM_Y) * 0.5f, side * (CAM_BORE + 0.025f) * 0.5f }, { 0.018f, CAM_BORE, 0.025f - CAM_BORE }, CAST);
			Tube(head, AlongX({ x, CAM_Y, 0.f }), CAM_BORE * 1.45f, CAM_BORE, 0.0178f, WHITE, glm::half_pi<float>(), glm::pi<float>() * 1.5f, 20);
		}
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			float x = BoreX(cylinder);
			// The plug's tube, in from the side of the head: from the chamber's roof, where the plug screws in, up through the
			// pocket in the body and the slot in the wall, out under the rail as a boss. It's open all the way down: the plug
			// can be seen at the bottom of it, and its lead's boot goes down it onto the plug.
			{
				glm::vec3 tip = PlugTip(cylinder), axis = PlugAxis();
				float fromRoof = (CHAMBER_DEPTH + 0.001f - tip.y) / axis.y;
				float toFace = (halfWidth - std::abs(tip.z)) / std::abs(axis.z);
				float outEnd = toFace + 0.006f;
				glm::mat4 along = At(tip + axis * ((fromRoof + outEnd) * 0.5f)) * glm::mat4_cast(PlugTurn());
				Tube(head, along, PLUG_TUBE, 0.0095f, outEnd - fromRoof, CAST, 0.f, glm::two_pi<float>(), 20);
				// The thread at the bottom, where the plug screws into the chamber's roof.
				Cylinder(head, At(tip + axis * (fromRoof + 0.0005f)) * glm::mat4_cast(PlugTurn()), 0.0095f, 0.0008f, DARK, 16);
			}
			// Each valve's spring seat, and its guide standing up through it for the stem seal to push onto.
			for (bool exhaust : { false, true })
			{
				Cylinder(head, At({ x, HEAD_FLOOR_Y + 0.0005f, ValveZ(exhaust) }), 0.0135f, 0.001f, SEAT, 24);
				Tube(head, At({ x, HEAD_FLOOR_Y + 0.004f, ValveZ(exhaust) }), 0.0055f, 0.0035f, 0.007f, CAST, 0.f, glm::two_pi<float>(), 14);
			}
		}
		add(PartType::CYLINDER_HEAD, head);
	}
	{
		// Origin on the cam axis, running along X like the crank.
		MeshBuilder cam;
		cam.SetUVScale(10.f);
		Cylinder(cam, AlongX({ 0.f, 0.f, 0.f }), 0.010f, 0.42f, { 0.8f, 0.8f, 0.82f });
		for (int journal = 0; journal < CAM_JOURNALS; journal++)
			Cylinder(cam, AlongX({ CamJournalX(journal), 0.f, 0.f }), 0.013f, 0.016f, WHITE, 24);
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
		// The fuel pump's eccentric, off-centre so it rocks the pump's arm once a turn.
		Cylinder(cam, AlongX({ FUEL_PUMP_X, 0.003f, 0.f }), 0.017f, 0.008f, { 0.85f, 0.85f, 0.88f }, 24);
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
		// A little bigger than the deck all round, so it shows as a lip between the block and the head.
		MeshBuilder gasket;
		gasket.SetUVScale(6.f);
		const float width = BLOCK_WIDTH + 2.f * GASKET_LIP;
		const glm::vec3 GASKET_GREY = { 0.55f, 0.57f, 0.6f };	// coated steel layers
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			gasket.ResetTransform().SetColor(GASKET_GREY).AddPlateWithHole({ BoreX(cylinder), GASKET_THICKNESS, 0.f }, { BORE_SPACING, width }, BORE_RADIUS + 0.0015f, 32);
			gasket.SetTransform(glm::rotate(glm::mat4(1.f), glm::pi<float>(), { 1.f, 0.f, 0.f }));
			gasket.AddPlateWithHole({ BoreX(cylinder), 0.f, 0.f }, { BORE_SPACING, width }, BORE_RADIUS + 0.0015f, 32);
		}
		gasket.ResetTransform();
		// The ends past the outer bores, out to the lip; and the edge all round.
		const float boresEnd = 2.f * BORE_SPACING, end = BLOCK_LENGTH * 0.5f + GASKET_LIP;
		for (float side : { -1.f, 1.f })
		{
			Box(gasket, { side * (boresEnd + end) * 0.5f, GASKET_THICKNESS * 0.5f, 0.f }, { end - boresEnd, GASKET_THICKNESS, width }, GASKET_GREY);
			Box(gasket, { 0.f, GASKET_THICKNESS * 0.5f, side * (width * 0.5f - 0.0002f) }, { 2.f * boresEnd, GASKET_THICKNESS, 0.0004f }, GASKET_GREY);
		}
		for (const auto& hole : DeckCoolantHoles())
			Cylinder(gasket, At({ hole.x, GASKET_THICKNESS - 0.0003f, hole.y }), hole.z, 0.0008f, DARK, 12);
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
		// Origin on the cam axis: a dome over the journal, a low flat foot either side of it out to the edge of its tower,
		// where the bolts go down beside the dome. Its underside is the top half of the journal's bore. A shade darker
		// than the head, so it shows on its tower.
		MeshBuilder cap;
		cap.SetUVScale(6.f);
		const glm::vec3 color = { 0.74f, 0.75f, 0.78f };
		Tube(cap, AlongX({}), 0.0195f, 0.0146f, 0.018f, color, -glm::half_pi<float>(), glm::half_pi<float>(), 20);
		for (float side : { -1.f, 1.f })
			Box(cap, { 0.f, 0.004f, side * 0.0198f }, { 0.018f, 0.008f, 0.0104f }, color);
		add(PartType::CAM_CAP, cap);
	}
	{
		// Origin at the centre of the crown.
		MeshBuilder piston;
		piston.SetUVScale(8.f);
		Cylinder(piston, At({ 0.f, -0.0275f, 0.f }), 0.0395f, 0.055f, WHITE, 32);
		for (int groove = 0; groove < 3; groove++)
			Tube(piston, At({ 0.f, -0.006f - groove * 0.005f, 0.f }), 0.0396f, 0.0367f, 0.0015f, DARK, 0.f, glm::two_pi<float>(), 32);
		add(PartType::PISTON, piston);
	}
	{
		// Its pin, a part of its own: origin at its middle, along X through the piston's bosses (PISTON_PIN_DEPTH under
		// the crown), its ends just showing.
		MeshBuilder pin;
		pin.SetUVScale(8.f);
		Cylinder(pin, AlongX({}), 0.0105f, 0.087f, STEEL_GREY);
		add(PartType::PISTON_PIN, pin);
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
	LoadCarParts();
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
		case PartType::CORE_PLUG:				return { { (index - 1) * 0.12f, -0.09f, BLOCK_WIDTH * 0.5f + 0.0005f }, FACING_INTAKE };	// in the block's holes
		case PartType::REAR_MAIN_SEAL:			return { { -FRONT_X + 0.001f, CRANK_Y, 0.f }, FACING_REAR };
		case PartType::OIL_PUMP:				return { { FRONT_X - 0.001f, CRANK_Y, 0.f }, FACING_FRONT };
		case PartType::OIL_PICKUP:				return { { FRONT_X - 0.009f, CRANK_Y - 0.075f, 0.f } };	// its flange under the pump's lug
		case PartType::OIL_PAN_GASKET:			return { { 0.f, PAN_RAIL_Y - 0.001f, 0.f } };
		case PartType::OIL_PAN:					return { { 0.f, PAN_RAIL_Y - 0.002f, 0.f } };
		// Under the drain plug's head, against the bottom of the pan (the plug itself is a fastener, on it: see ShopWorld).
		case PartType::DRAIN_PLUG_WASHER:		return { { 0.12f, PAN_RAIL_Y - 0.002f - OIL_PAN_DEPTH - 0.002f, 0.f } };
		case PartType::OIL_FILTER:				return crossOver({ { 0.12f, -0.2f, -(CRANKCASE_WIDTH * 0.5f + 0.025f) }, FACING_EXHAUST });
		case PartType::VALVE_STEM_SEAL:			return { { BoreX(cylinder), HEAD_DECK_Y + HEAD_FLOOR_Y, ValveZ(exhaust) } };
		case PartType::VALVE_RETAINER:			return { { BoreX(cylinder), HEAD_DECK_Y + HEAD_FLOOR_Y + SPRING_INSTALLED_HEIGHT, ValveZ(exhaust) } };
		// Each rocker arm sits on its valve tip, its pivot out on the shaft to the side: the intakes' shaft
		// on the intake side, the exhausts' on the exhaust side.
		case PartType::ROCKER_ARM:				return { { BoreX(cylinder), HEAD_DECK_Y + VALVE_FACE_Y + VALVE_STEM_LENGTH, ValveZ(exhaust) }, exhaust ? glm::quat(1.f, 0.f, 0.f, 0.f) : END_FOR_END };
		case PartType::ROCKER_SHAFT:			return { { 0.f, HEAD_DECK_Y + VALVE_FACE_Y + VALVE_STEM_LENGTH + ROCKER_PIVOT_Y, index == 1 ? VALVE_Z + ROCKER_PIVOT_Z : -VALVE_Z - ROCKER_PIVOT_Z } };
		case PartType::CRANK_SPROCKET:			return { { TIMING_PLANE_X, CRANK_Y, 0.f } };
		case PartType::CAM_SPROCKET:			return { { TIMING_PLANE_X, CamAxisY(), 0.f } };
		case PartType::TIMING_BELT:			return { { TIMING_PLANE_X, 0.f, 0.f } };
		case PartType::BELT_IDLER:				return { { TIMING_PLANE_X + 0.012f, -0.05f, index == 0 ? 0.062f : -0.062f }, FACING_FRONT };
		case PartType::BELT_TENSIONER:			return { { TIMING_PLANE_X, TIMING_IDLER_Y, TIMING_IDLER_Z } };
		case PartType::TIMING_COVER_GASKET:		return { { FRONT_X, TIMING_COVER_Y, 0.f }, FACING_FRONT };	// against the block, under the cover's rim
		case PartType::TIMING_COVER:			return { { TIMING_COVER_X, TIMING_COVER_Y, 0.f }, FACING_FRONT };
		case PartType::FRONT_CRANK_SEAL:		return { { FRONT_X - 0.009f, CRANK_Y, 0.f }, FACING_FRONT };	// in the oil pump's housing
		case PartType::CRANK_PULLEY:			return { { TIMING_COVER_X - 0.03f, CRANK_Y, 0.f } };
		case PartType::VALVE_COVER_GASKET:		return { { 0.f, HEAD_TOP_Y, 0.f } };
		case PartType::VALVE_COVER:				return { { 0.f, HEAD_TOP_Y + 0.003f, 0.f } };
		case PartType::SPARK_PLUG:				return { { BoreX(index), HEAD_DECK_Y + 0.012f, 0.f } };
		case PartType::PLUG_LEAD:				return { glm::vec3(0.f, HEAD_DECK_Y, 0.f) + PlugTip(index) + PlugAxis() * PLUG_TERMINAL };	// on its plug's terminal
		// Gear-driven off the back of the cam, sticking out of the back of the head.
		case PartType::DISTRIBUTOR:				return { { -FRONT_X + 0.001f, CamAxisY(), 0.f }, FACING_REAR };
		case PartType::FLYWHEEL:				return { { -FRONT_X + 0.002f, CRANK_Y, 0.f }, FACING_REAR };
		case PartType::INTAKE_MANIFOLD_GASKET:	return otherSide({ { 0.f, PORT_Y, PORT_FACE_Z + 0.001f }, FACING_INTAKE });
		case PartType::INTAKE_MANIFOLD:			return otherSide({ { 0.f, PORT_Y, PORT_FACE_Z + 0.002f } });
		// On top of the plenum.
		case PartType::CARBURETTOR:				return otherSide({ { 0.f, PORT_Y + 0.08f, PORT_FACE_Z + 0.137f } });
		case PartType::FUEL_INJECTOR:			return crossOver({ { BoreX(index), PORT_Y + 0.012f, PORT_FACE_Z + 0.04f } });
		// On the intake side of the head at the cam's height, its arm in through the head onto the eccentric.
		// Its gasket on the side of the head, then the pump on that (both standing out from the head, -Z).
		case PartType::FUEL_PUMP_GASKET:		return { { FUEL_PUMP_X, CamAxisY() - FUEL_PUMP_DROP, -PORT_FACE_Z }, FACING_EXHAUST };
		case PartType::FUEL_PUMP:				return { { FUEL_PUMP_X, CamAxisY() - FUEL_PUMP_DROP, -PORT_FACE_Z - 0.001f }, FACING_EXHAUST };
		// At the front, beside the water pump, its pulley in the belt's plane.
		case PartType::ALTERNATOR:				return { { ACCESSORY_BELT_X, ALTERNATOR_Y, ALTERNATOR_Z } };
		case PartType::ALTERNATOR_BRACKET:		return { { ALTERNATOR_BRACKET_X, ALTERNATOR_BRACKET_Y, ALTERNATOR_Z } };
		case PartType::DRIVE_BELT:				return { { ACCESSORY_BELT_X, 0.f, 0.f } };
		case PartType::DRIVE_BELT_TENSIONER:		return { { ACCESSORY_BELT_X, DRIVE_TENSIONER_Y, DRIVE_TENSIONER_Z } };
		case PartType::EXHAUST_MANIFOLD_GASKET:	return otherSide({ { 0.f, PORT_Y, -PORT_FACE_Z - 0.001f }, FACING_EXHAUST });
		case PartType::EXHAUST_MANIFOLD:
		case PartType::EXHAUST_PIPE:			return otherSide({ { 0.f, PORT_Y, -PORT_FACE_Z - 0.002f } });
		case PartType::WATER_PUMP_GASKET:		return { { WATER_PUMP_GASKET_X, WATER_PUMP_Y, WATER_PUMP_Z }, FACING_FRONT };	// under the pump, on the pad round its hole in the cover's ear
		case PartType::WATER_PUMP:				return { { WATER_PUMP_X, WATER_PUMP_Y, WATER_PUMP_Z } };
		// The thermostat's flange between its two gaskets, the housing over them.
		case PartType::THERMOSTAT_GASKET:		return crossOver({ { -0.17f, 0.095f, PORT_FACE_Z }, FACING_INTAKE });
		case PartType::THERMOSTAT:				return crossOver({ { -0.17f, 0.095f, PORT_FACE_Z + 0.001f }, FACING_INTAKE });
		case PartType::THERMOSTAT_HOUSING_GASKET:	return crossOver({ { -0.17f, 0.095f, PORT_FACE_Z + 0.003f }, FACING_INTAKE });
		case PartType::THERMOSTAT_HOUSING:		return crossOver({ { -0.17f, 0.095f, PORT_FACE_Z + 0.004f }, FACING_INTAKE });
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
		// Its bore round the crank snout is the front crank seal's housing: the seal's pressed in from the front.
		Tube(pump, At({ 0.f, 0.008f, 0.f }), 0.055f, 0.0305f, 0.016f, WHITE, 0.f, glm::two_pi<float>(), 32);
		Tube(pump, At({ 0.f, 0.0163f, 0.f }), 0.034f, 0.0305f, 0.0008f, DARK, 0.f, glm::two_pi<float>(), 24);
		Box(pump, { -0.06f, 0.008f, 0.f }, { 0.03f, 0.016f, 0.026f });
		add(PartType::OIL_PUMP, pump);
	}
	{
		// The flange on top bolts up under the oil pump's lug; the tube runs down from it and back along the pan to
		// the strainer.
		MeshBuilder pickup;
		Box(pickup, { 0.f, -0.003f, 0.f }, { 0.026f, 0.006f, 0.026f }, STEEL_GREY);
		Cylinder(pickup, At({ 0.f, -0.0325f, 0.f }), 0.008f, 0.055f, STEEL_GREY, 12);
		Cylinder(pickup, AlongX({ 0.07f, -0.06f, 0.f }), 0.008f, 0.14f, STEEL_GREY, 12);
		Cylinder(pickup, At({ 0.17f, -0.063f, 0.f }), 0.04f, 0.012f, DARK, 24);
		add(PartType::OIL_PICKUP, pickup);
	}
	{
		MeshBuilder gasket;
		frame(gasket, BLOCK_LENGTH, CRANKCASE_WIDTH, OIL_PAN_LIP, 0.0015f, RUBBER);
		add(PartType::OIL_PAN_GASKET, gasket);
	}
	{
		// Rail on top at y = 0, the sump hanging below from its inner edge, so the rail stands out round it as a
		// lip for the bolts.
		MeshBuilder pan;
		pan.SetUVScale(3.f);
		float depth = OIL_PAN_DEPTH;
		frame(pan, BLOCK_LENGTH, CRANKCASE_WIDTH, OIL_PAN_LIP, 0.004f, PAN_PAINT, -0.002f);
		float wallLength = BLOCK_LENGTH - 2.f * OIL_PAN_LIP, wallWidth = CRANKCASE_WIDTH - 2.f * OIL_PAN_LIP;
		for (float side : { -1.f, 1.f })
		{
			Box(pan, { 0.f, -depth * 0.5f, side * (wallWidth * 0.5f - 0.002f) }, { wallLength, depth, 0.004f }, PAN_PAINT);
			Box(pan, { side * (wallLength * 0.5f - 0.002f), -depth * 0.5f, 0.f }, { 0.004f, depth, wallWidth }, PAN_PAINT);
		}
		Box(pan, { 0.f, -depth, 0.f }, { wallLength, 0.005f, wallWidth }, PAN_PAINT);
		add(PartType::OIL_PAN, pan);
	}
	{
		// Built as the nuts and bolts are (origin under its head, the shank down -Y): a 14 mm hex head with a flange,
		// the threaded shank and its magnet on the end.
		MeshBuilder plug;
		Cylinder(plug, At({ 0.f, 0.0045f, 0.f }), 0.0081f, 0.007f, STEEL_GREY, 6);
		Cylinder(plug, At({ 0.f, 0.0006f, 0.f }), 0.0095f, 0.0012f, STEEL_GREY, 20);
		Cylinder(plug, At({ 0.f, -0.006f, 0.f }), 0.006f, 0.012f, STEEL_GREY, 12);
		Cylinder(plug, At({ 0.f, -0.0125f, 0.f }), 0.0045f, 0.002f, DARK, 12);
		add(PartType::OIL_DRAIN_PLUG, plug);
	}
	{
		// The fuel pump's gasket: a thin fibre flange outline with the hole the pump's arm goes in through.
		MeshBuilder gasket;
		Tube(gasket, At({ 0.f, 0.0005f, 0.f }), 0.022f, 0.009f, 0.001f, GASKET, 0.f, glm::two_pi<float>(), 24);
		for (float side : { -1.f, 1.f })
			Box(gasket, { side * 0.027f, 0.0005f, 0.f }, { 0.012f, 0.001f, 0.014f }, GASKET);
		add(PartType::FUEL_PUMP_GASKET, gasket);
	}
	{
		// A thin copper ring, lying flat.
		MeshBuilder washer;
		Tube(washer, At({ 0.f, 0.001f, 0.f }), 0.0095f, 0.0063f, 0.002f, { 0.85f, 0.48f, 0.3f }, 0.f, glm::two_pi<float>(), 20);
		add(PartType::DRAIN_PLUG_WASHER, washer);
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
		// (The drain plug and the spark plugs have models of their own.)
		if (!fastener.IsFastener() || static_cast<PartType>(type) == PartType::OIL_DRAIN_PLUG || static_cast<PartType>(type) == PartType::SPARK_PLUG)
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
		// Origin at the pad on the valve tip. The arm runs in under the cam lobe one way and out to its
		// pivot boss on the shaft the other (+Z), with the lash adjuster screw over the valve.
		MeshBuilder rocker;
		rocker.SetUVScale(10.f);
		Box(rocker, { 0.f, 0.006f, 0.0065f }, { 0.009f, 0.006f, 0.037f }, WHITE);
		Box(rocker, { 0.f, 0.002f, 0.f }, { 0.007f, 0.004f, 0.007f }, STEEL_GREY);
		Box(rocker, { 0.f, 0.0095f, -0.009f }, { 0.009f, 0.003f, 0.01f }, STEEL_GREY);
		Cylinder(rocker, AlongX({ 0.f, ROCKER_PIVOT_Y, ROCKER_PIVOT_Z }), 0.0085f, 0.012f, WHITE, 16);
		Cylinder(rocker, At({ 0.f, 0.012f, 0.f }), 0.0025f, 0.008f, STEEL_GREY, 8);
		add(PartType::ROCKER_ARM, rocker);
	}
	{
		// Along X, on its own axis; plugged at both ends.
		MeshBuilder shaft;
		shaft.SetUVScale(10.f);
		Cylinder(shaft, AlongX({}), 0.008f, BLOCK_LENGTH - 0.04f, WHITE, 16);
		for (float end : { -1.f, 1.f })
			Cylinder(shaft, AlongX({ end * (BLOCK_LENGTH * 0.5f - 0.02f), 0.f, 0.f }), 0.0085f, 0.003f, DARK, 16);
		add(PartType::ROCKER_SHAFT, shaft);
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
		// In its plane (X = 0, as it sits on the engine) round the cam and crank pulleys and over the back
		// of the idler: a wide black toothed belt.
		MeshBuilder belt;
		belt.SetUVScale(20.f);
		constexpr float WIDTH = 0.022f, THICK = 0.004f;
		BeltAlong(belt, BeltPath(TimingPulleys(), THICK * 0.5f), WIDTH, THICK, { 0.09f, 0.09f, 0.10f });
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
		// The timing idler pulley: origin at its pulley, in the belt's plane, a smooth pulley on its
		// bearing; behind it the bracket up to its pivot bolt, and the spring that pulls it onto the belt.
		MeshBuilder tensioner;
		Cylinder(tensioner, AlongX({}), TIMING_IDLER_RADIUS, 0.024f, { 0.70f, 0.71f, 0.74f }, 28);
		Cylinder(tensioner, AlongX({ -0.0125f, 0.f, 0.f }), 0.007f, 0.002f, BLACK, 16);
		Box(tensioner, { 0.016f, 0.014f, 0.004f }, { 0.006f, 0.04f, 0.022f }, { 0.45f, 0.45f, 0.48f });
		Cylinder(tensioner, AlongX({ 0.018f, 0.028f, 0.008f }), 0.008f, 0.01f, STEEL_GREY, 16);
		Cylinder(tensioner, At({ 0.018f, 0.045f, -0.006f }), 0.003f, 0.03f, STEEL_GREY, 8);
		add(PartType::BELT_TENSIONER, tensioner);
	}
	// The timing cover and its gasket run from under the crank up to the top of the head (where the valve cover's rail is),
	// not past it: in their own space (lying face up, +X up the engine), from BOTTOM to TOP about the cover's middle.
	constexpr float COVER_BOTTOM = -0.25f, COVER_TOP = HEAD_TOP_Y - TIMING_COVER_Y;
	constexpr float COVER_LENGTH = COVER_TOP - COVER_BOTTOM, COVER_MIDDLE = (COVER_TOP + COVER_BOTTOM) * 0.5f;
	// Its rim reaches all the way back to the block, the gasket between: no gap.
	constexpr float COVER_RIM = -TIMING_COVER_X + FRONT_X - 0.0015f;
	// The ear out the left side for the water pump, from EAR_LOW to EAR_HIGH up the cover, out to -WATER_PUMP_EAR.
	constexpr float EAR_LOW = WATER_PUMP_Y - TIMING_COVER_Y - WATER_PUMP_EAR_HALF, EAR_HIGH = EAR_LOW + 2.f * WATER_PUMP_EAR_HALF;
	{
		// Its outline: a band round the rim's edge, lying against the front of the block, out round the pump's ear on the
		// left (and round the ear's passage, which the pump's coolant goes through, sealing it off from the belt).
		MeshBuilder gasket;
		constexpr float BAND = 0.012f, HALF = TIMING_COVER_HALF_WIDTH, THICK = 0.0015f;
		auto band = [&](float x0, float x1, float z0, float z1) {
			Box(gasket, { (x0 + x1) * 0.5f, THICK * 0.5f, (z0 + z1) * 0.5f }, { x1 - x0, THICK, z1 - z0 }, GASKET);
		};
		band(COVER_BOTTOM, COVER_TOP, HALF - BAND, HALF);
		band(COVER_BOTTOM, EAR_LOW + BAND, -HALF, -HALF + BAND);
		band(EAR_HIGH - BAND, COVER_TOP, -HALF, -HALF + BAND);
		band(COVER_BOTTOM, COVER_BOTTOM + BAND, -HALF + BAND, HALF - BAND);
		band(COVER_TOP - BAND, COVER_TOP, -HALF + BAND, HALF - BAND);
		band(EAR_LOW, EAR_LOW + BAND, -WATER_PUMP_EAR, -HALF);
		band(EAR_HIGH - BAND, EAR_HIGH, -WATER_PUMP_EAR, -HALF);
		band(EAR_LOW + BAND, EAR_HIGH - BAND, -WATER_PUMP_EAR, -WATER_PUMP_EAR + BAND);
		Tube(gasket, At({ WATER_PUMP_Y - TIMING_COVER_Y, THICK * 0.5f, WATER_PUMP_Z }), 0.034f, 0.028f, THICK, GASKET, 0.f, glm::two_pi<float>(), 28);
		add(PartType::TIMING_COVER_GASKET, gasket);
	}
	{
		// Lying face up. Stood on the front of the engine, +X points up and the rim faces the block.
		// The crank seal boss sits 0.19 m below the cover's middle, the water pump boss just above it.
		MeshBuilder cover;
		cover.SetUVScale(4.f);
		// The face, with a hole through it for the crank's snout (the crank pulley's hub goes in through it onto the snout) and
		// one for the water pump (its nose goes in through it): a ring of boss round each. In strips up the cover: plain,
		// then one with a hole in it, built from a plate each side and the hole's bore through it.
		{
			constexpr float HALF_WIDTH = TIMING_COVER_HALF_WIDTH;
			const glm::vec2 crankHole = { CRANK_Y - TIMING_COVER_Y, 0.f }, pumpHole = { WATER_PUMP_Y - TIMING_COVER_Y, WATER_PUMP_Z };
			auto solid = [&](float x0, float x1, float z0, float z1) {
				if (x1 > x0 && z1 > z0)
					Box(cover, { (x0 + x1) * 0.5f, TIMING_COVER_FACE * 0.5f, (z0 + z1) * 0.5f }, { x1 - x0, TIMING_COVER_FACE, z1 - z0 });
			};
			auto holed = [&](const glm::vec2& at, float hole, float strip, float boss, float height) {
				// The square round the hole, front and back, the hole's bore, and the solid either side of the square.
				cover.ResetTransform().SetColor(WHITE).AddPlateWithHole({ at.x, TIMING_COVER_FACE, at.y }, { strip, strip }, hole, 28);
				cover.SetTransform(glm::rotate(glm::mat4(1.f), glm::pi<float>(), { 1.f, 0.f, 0.f })).AddPlateWithHole({ at.x, 0.f, -at.y }, { strip, strip }, hole, 28);
				cover.ResetTransform();
				Tube(cover, At({ at.x, TIMING_COVER_FACE * 0.5f, at.y }), hole + 0.001f, hole, TIMING_COVER_FACE, WHITE, 0.f, glm::two_pi<float>(), 28);
				solid(at.x - strip * 0.5f, at.x + strip * 0.5f, -HALF_WIDTH, at.y - strip * 0.5f);
				solid(at.x - strip * 0.5f, at.x + strip * 0.5f, at.y + strip * 0.5f, HALF_WIDTH);
				// Its boss, standing out of the face round it (the pump's, a low pad its flange sits flat on).
				Tube(cover, At({ at.x, TIMING_COVER_FACE + height * 0.5f, at.y }), hole + boss, hole, height, WHITE, 0.f, glm::two_pi<float>(), 28);
			};
			// The crank's hole; the pump's, in its ear, the strip round it reaching out to the ear's edge.
			constexpr float CRANK_STRIP = 0.1f, PUMP_STRIP = 2.f * WATER_PUMP_EAR_HALF, PUMP_HOLE = WATER_PUMP_NOSE + 0.002f;
			solid(COVER_BOTTOM, crankHole.x - CRANK_STRIP * 0.5f, -HALF_WIDTH, HALF_WIDTH);
			holed(crankHole, 0.03f, CRANK_STRIP, 0.01f, 0.006f);
			solid(crankHole.x + CRANK_STRIP * 0.5f, EAR_LOW, -HALF_WIDTH, HALF_WIDTH);
			holed(pumpHole, PUMP_HOLE, PUMP_STRIP, 0.0095f, WATER_PUMP_PAD);
			solid(EAR_HIGH, COVER_TOP, -HALF_WIDTH, HALF_WIDTH);
			// Behind the pump's hole, a sleeve back to the block: the coolant goes through it, the belt runs dry outside it.
			Tube(cover, At({ pumpHole.x, -COVER_RIM * 0.5f, pumpHole.y }), PUMP_HOLE + 0.004f, PUMP_HOLE, COVER_RIM, WHITE, 0.f, glm::two_pi<float>(), 28);
		}
		// The rim: the right side straight, the left stepping out round the pump's ear; and the bottom end.
		Box(cover, { COVER_MIDDLE, -COVER_RIM * 0.5f, TIMING_COVER_HALF_WIDTH - 0.003f }, { COVER_LENGTH, COVER_RIM, 0.006f });
		Box(cover, { (COVER_BOTTOM + EAR_LOW) * 0.5f, -COVER_RIM * 0.5f, -TIMING_COVER_HALF_WIDTH + 0.003f }, { EAR_LOW - COVER_BOTTOM, COVER_RIM, 0.006f });
		Box(cover, { (EAR_HIGH + COVER_TOP) * 0.5f, -COVER_RIM * 0.5f, -TIMING_COVER_HALF_WIDTH + 0.003f }, { COVER_TOP - EAR_HIGH, COVER_RIM, 0.006f });
		Box(cover, { (EAR_LOW + EAR_HIGH) * 0.5f, -COVER_RIM * 0.5f, -WATER_PUMP_EAR + 0.003f }, { EAR_HIGH - EAR_LOW, COVER_RIM, 0.006f });
		for (float end : { EAR_LOW + 0.003f, EAR_HIGH - 0.003f })
			Box(cover, { end, -COVER_RIM * 0.5f, -(WATER_PUMP_EAR + TIMING_COVER_HALF_WIDTH) * 0.5f }, { 0.006f, COVER_RIM, WATER_PUMP_EAR - TIMING_COVER_HALF_WIDTH + 0.006f });
		Box(cover, { COVER_BOTTOM + 0.003f, -COVER_RIM * 0.5f, 0.f }, { 0.006f, COVER_RIM, 2.f * TIMING_COVER_HALF_WIDTH });
		// The top edge is at the cam's axis: a half-round cutout there for the cam's pulley (its lower half shrouded, its top
		// out above the cover), the top wall either side of it.
		constexpr float CAM_CUTOUT = 0.052f;
		for (float side : { -1.f, 1.f })
		{
			float width = 0.105f - CAM_CUTOUT;
			Box(cover, { COVER_TOP - 0.003f, -COVER_RIM * 0.5f, side * (CAM_CUTOUT + width * 0.5f) }, { 0.006f, COVER_RIM, width });
		}
		Tube(cover, At({ COVER_TOP, -COVER_RIM * 0.5f, 0.f }), CAM_CUTOUT + 0.004f, CAM_CUTOUT, COVER_RIM, WHITE, glm::half_pi<float>(),
			glm::pi<float>() * 1.5f, 24);
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
		// Its hub's nose, back through the timing cover's hole onto the crank's snout, up against the crank's timing pulley.
		constexpr float NOSE_END = (TIMING_PLANE_X - 0.005f) - (TIMING_COVER_X - 0.03f);
		Cylinder(pulley, AlongX({ (0.015f + NOSE_END) * 0.5f, 0.f, 0.f }), 0.022f, NOSE_END - 0.015f, STEEL_GREY, 20);
		Tube(pulley, AlongX({ -0.004f, 0.f, 0.f }), 0.052f, 0.028f, 0.006f, WHITE, 0.f, glm::two_pi<float>(), 32);
		Tube(pulley, AlongX({ -0.008f, 0.f, 0.f }), 0.055f, 0.052f, 0.018f, RUBBER, 0.f, glm::two_pi<float>(), 32);
		Tube(pulley, AlongX({ -0.008f, 0.f, 0.f }), 0.066f, 0.055f, 0.018f, WHITE, 0.f, glm::two_pi<float>(), 32);
		for (float x : { -0.004f, -0.008f, -0.012f })
			Tube(pulley, AlongX({ x, 0.f, 0.f }), 0.0665f, 0.062f, 0.0015f, DARK, 0.f, glm::two_pi<float>(), 32);
		// The belt groove out in front, in the accessory belt's plane.
		Cylinder(pulley, AlongX({ -0.022f, 0.f, 0.f }), 0.02f, 0.01f, STEEL_GREY, 20);
		Tube(pulley, AlongX({ ACCESSORY_BELT_X - (TIMING_COVER_X - 0.03f), 0.f, 0.f }), 0.058f, 0.02f, 0.01f, STEEL_GREY, 0.f, glm::two_pi<float>(), 32);
		add(PartType::CRANK_PULLEY, pulley);
	}
	{
		MeshBuilder gasket;
		frame(gasket, BLOCK_LENGTH, 0.21f, 0.01f, 0.003f, RUBBER);
		add(PartType::VALVE_COVER_GASKET, gasket);
	}
	{
		// Origin at its rail, on the head's. Its top plate with ribs cast across it over each cylinder, the oil filler cap,
		// and a baffle plate inside. (The spark plugs come in from the side of the head, under it: see PlugTip.)
		MeshBuilder cover;
		cover.SetUVScale(4.f);
		constexpr float WIDTH = 0.21f, PLATE = 0.006f;
		for (float side : { -1.f, 1.f })
		{
			Box(cover, { 0.f, 0.025f, side * 0.1025f }, { BLOCK_LENGTH, 0.05f, 0.005f }, PAINT_RED);
			Box(cover, { side * (BLOCK_LENGTH * 0.5f - 0.0025f), 0.025f, 0.f }, { 0.005f, 0.05f, WIDTH }, PAINT_RED);
		}
		Box(cover, { 0.f, VALVE_COVER_TOP - PLATE * 0.5f, 0.f }, { BLOCK_LENGTH, PLATE, WIDTH }, PAINT_RED);
		for (int cylinder = 0; cylinder < 4; cylinder++)
			for (float side : { -1.f, 1.f })
				Box(cover, { BoreX(cylinder), VALVE_COVER_TOP + 0.003f, side * 0.06f }, { 0.006f, 0.006f, 0.06f }, PAINT_RED);
		// The baffle under the top, keeping the oil thrown off the cam out of the breather: a plate along one side.
		Box(cover, { 0.f, VALVE_COVER_TOP - 0.015f, -0.07f }, { BLOCK_LENGTH - 0.04f, 0.002f, 0.05f }, { 0.6f, 0.6f, 0.62f });
		Cylinder(cover, At({ 0.12f, 0.059f, 0.055f }), 0.018f, 0.008f, BLACK, 20);
		add(PartType::VALVE_COVER, cover);
	}
	{
		// Built as the nuts and bolts are: origin at its seat, under the hex, the threads (and the firing tip on their end) down
		// -Y, the hex, the insulator and the terminal up.
		MeshBuilder plug;
		Box(plug, { 0.003f, -0.02f, 0.f }, { 0.006f, 0.0015f, 0.002f }, STEEL_GREY);
		Cylinder(plug, At({ 0.f, -0.0095f, 0.f }), 0.007f, 0.019f, STEEL_GREY, 12);
		Cylinder(plug, At({ 0.f, 0.007f, 0.f }), 0.009f, 0.014f, { 0.7f, 0.7f, 0.72f }, 6);
		Cylinder(plug, At({ 0.f, 0.035f, 0.f }), 0.0055f, 0.042f, { 0.95f, 0.95f, 0.92f }, 16);
		Cylinder(plug, At({ 0.f, 0.059f, 0.f }), 0.003f, 0.006f, STEEL_GREY, 8);
		add(PartType::SPARK_PLUG, plug);
	}
	{
		// Origin on the plug's terminal: a long boot out of its tube in the side of the head, then the lead off
		// towards the distributor at the back.
		MeshBuilder lead;
		Cylinder(lead, At({ 0.f, 0.05f, 0.f }), 0.008f, 0.1f, BLACK, 12);
		Box(lead, { 0.05f, 0.103f, 0.f }, { 0.1f, 0.005f, 0.005f }, BLACK);
		add(PartType::PLUG_LEAD, lead);

		// Fitted, each runs from its plug's boot back over the valve cover, side by side with the others,
		// and down into its tower on the distributor cap: the towers in firing order (1-3-4-2) round the
		// cap. Built from the plug terminal, where a lead is mounted (see the PLUG_LEAD mount).
		constexpr int TOWER_OF_CYLINDER[4] = { 0, 3, 1, 2 };
		constexpr float CAP_TOWER_TOP = 0.098f;		// the distributor's towers, out from its flange
		constexpr float CAP_TOWER_RADIUS = 0.02f;
		const glm::vec3 distributor = { -FRONT_X + 0.001f, CamAxisY(), 0.f };
		auto run = [&](MeshBuilder& builder, const glm::vec3& from, const glm::vec3& to, float radius) {
			glm::vec3 along = to - from;
			float length = glm::length(along);
			if (length < 1e-5f)
				return;
			glm::quat turn = glm::quat(glm::vec3(0.f, 1.f, 0.f), along / length);
			// A little long, so the pieces meet at the bends.
			builder.SetTransform(At((from + to) * 0.5f) * glm::mat4_cast(turn)).SetColor(BLACK).AddCylinder({}, radius, length + radius, 8);
			builder.ResetTransform();
		};
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			// The plug's in from the side of the head (PlugTip): the lead's long boot runs down its tube onto the terminal, and
			// the lead comes out of the tube's end, up along that side of the valve cover and back to the distributor.
			const glm::vec3 deck = { 0.f, HEAD_DECK_Y, 0.f };
			const glm::vec3 axis = PlugAxis();
			glm::vec3 terminal = deck + PlugTip(cylinder) + axis * PLUG_TERMINAL;
			float tubeEnd = (0.105f - std::abs(PlugTip(cylinder).z)) / std::abs(axis.z) + 0.006f;
			glm::vec3 bootEnd = deck + PlugTip(cylinder) + axis * (tubeEnd + 0.014f);
			// The distributor stands out of the back of the head, +Y turned to +X: a tower's (x, z) round the
			// cap comes out as (-y, z) behind it.
			float angle = TOWER_OF_CYLINDER[cylinder] * glm::half_pi<float>();
			glm::vec3 tower = distributor + glm::vec3(CAP_TOWER_TOP, -std::cos(angle) * CAP_TOWER_RADIUS, std::sin(angle) * CAP_TOWER_RADIUS);
			// Along the side of the valve cover, each in its own lane.
			float lane = PLUG_SIDE * (0.122f + (3 - cylinder) * 0.007f);
			float alongSide = HEAD_TOP_Y + 0.03f + cylinder * 0.004f;
			std::vector<glm::vec3> path = {
				bootEnd,
				bootEnd + axis * 0.012f + glm::vec3(0.f, 0.01f, 0.f),
				{ bootEnd.x, alongSide, lane },
				{ tower.x - 0.04f, alongSide, lane },
				{ tower.x + 0.02f, (alongSide + tower.y) * 0.5f + 0.01f, (lane + tower.z) * 0.5f },
				{ tower.x + 0.03f, tower.y, tower.z },
				{ tower.x + 0.004f, tower.y, tower.z },
			};
			for (auto& point : path)
				point -= terminal;
			tower -= terminal;
			MeshBuilder fitted;
			run(fitted, { 0.f, 0.f, 0.f }, bootEnd - terminal, 0.008f);
			for (size_t i = 0; i + 1 < path.size(); i++)
				run(fitted, path[i], path[i + 1], 0.0035f);
			// The boot over the tower.
			run(fitted, { tower.x + 0.018f, tower.y, tower.z }, { tower.x, tower.y, tower.z }, 0.0065f);
			AddModel(ShopModels::PlugLeadFitted(cylinder), fitted);
		}
	}
	{
		// Built standing up from its drive flange (+Y out of the back of the head): the aluminium body,
		// then the cap with a tower for the coil and one for each lead.
		MeshBuilder distributor;
		distributor.SetUVScale(6.f);
		Cylinder(distributor, At({ 0.f, 0.002f, 0.f }), 0.03f, 0.004f, WHITE, 20);
		Cylinder(distributor, At({ 0.f, 0.032f, 0.f }), 0.022f, 0.056f, WHITE, 20);
		Cylinder(distributor, At({ 0.f, 0.075f, 0.f }), 0.032f, 0.03f, BLACK, 24);
		for (int tower = 0; tower < 5; tower++)
		{
			float angle = tower * glm::two_pi<float>() / 4.f;
			glm::vec3 at = tower == 4 ? glm::vec3(0.f, 0.094f, 0.f) : glm::vec3(std::cos(angle) * 0.02f, 0.094f, std::sin(angle) * 0.02f);
			Cylinder(distributor, At(at), 0.005f, 0.008f, BLACK, 8);
		}
		add(PartType::DISTRIBUTOR, distributor);
	}
	{
		// Lying flat, crank side down (+Y faces back, away from the crank), the ring gear round its rim and
		// the hub its bolts go through.
		MeshBuilder flywheel;
		flywheel.SetUVScale(4.f);
		Cylinder(flywheel, At({ 0.f, 0.007f, 0.f }), 0.13f, 0.014f, WHITE, 40);
		Tube(flywheel, At({ 0.f, 0.007f, 0.f }), 0.136f, 0.129f, 0.012f, STEEL_GREY, 0.f, glm::two_pi<float>(), 40);
		// Kept thin: the stand's mounting plate is only a couple of millimetres behind it.
		Cylinder(flywheel, At({ 0.f, 0.0155f, 0.f }), 0.035f, 0.003f, STEEL_GREY, 20);
		add(PartType::FLYWHEEL, flywheel);
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
	// A cast pipe along a curve (a cubic Bézier through four points): short straight lengths, their joints rounded, its
	// radius going from one to the other along it.
	auto bend = [&](MeshBuilder& builder, const std::array<glm::vec3, 4>& p, float r0, float r1, const glm::vec3& color, int pieces = 8) {
		auto at = [&](float t) {
			float u = 1.f - t;
			return u * u * u * p[0] + 3.f * u * u * t * p[1] + 3.f * u * t * t * p[2] + t * t * t * p[3];
		};
		for (int i = 0; i < pieces; i++)
		{
			float t0 = static_cast<float>(i) / pieces, t1 = static_cast<float>(i + 1) / pieces;
			glm::vec3 a = at(t0), b = at(t1), run = b - a;
			float length = glm::length(run), radius = glm::mix(r0, r1, (t0 + t1) * 0.5f);
			glm::vec3 dir = run / length, up = { 0.f, 1.f, 0.f }, axis = glm::cross(up, dir);
			glm::mat4 turn = glm::length(axis) < 1e-5f ? (dir.y < 0.f ? glm::rotate(glm::mat4(1.f), glm::pi<float>(), glm::vec3(1.f, 0.f, 0.f)) : glm::mat4(1.f))
				: glm::rotate(glm::mat4(1.f), std::acos(glm::clamp(glm::dot(up, dir), -1.f, 1.f)), glm::normalize(axis));
			Cylinder(builder, At((a + b) * 0.5f) * turn, radius, length, color, 14);
			if (i > 0)
				builder.ResetTransform().SetColor(color).AddSphere(a, radius, 4, 14);
		}
	};
	auto flangeNut = [](int nut) { return ManifoldNut(nut); };
	// A head flange: a pad round each port, a web joining them, and a boss round each stud, its face at FLANGE out
	// along +Z (or -Z, out).
	auto headFlange = [&](MeshBuilder& builder, float out, const glm::vec3& color) {
		for (int cylinder = 0; cylinder < 4; cylinder++)
			Box(builder, { BoreX(cylinder), 0.f, out * MANIFOLD_FLANGE * 0.5f }, { 0.062f, 0.05f, MANIFOLD_FLANGE }, color);
		Box(builder, { 0.f, 0.f, out * MANIFOLD_FLANGE * 0.4f }, { 0.40f, 0.03f, MANIFOLD_FLANGE * 0.8f }, color);
		for (int nut = 0; nut < 8; nut++)
		{
			glm::vec2 at = flangeNut(nut);
			Cylinder(builder, AlongZ({ at.x, at.y, out * MANIFOLD_FLANGE * 0.5f }), 0.009f, MANIFOLD_FLANGE, color, 16);
		}
	};
	{
		// Origin at the middle of the head flange; the runners out along +Z, rising as they go, into the plenum: a round log
		// along the engine with the carburettor's pad on top, a vacuum take-off and the brake servo's on it, and the
		// coolant's hose nipples at the front (it's warmed by the coolant, so the mixture doesn't ice up).
		MeshBuilder manifold;
		manifold.SetUVScale(4.f);
		headFlange(manifold, 1.f, WHITE);
		constexpr glm::vec3 PLENUM = { 0.f, 0.038f, 0.135f };
		constexpr float PLENUM_RADIUS = 0.042f, PLENUM_LENGTH = 0.40f;
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			float x = BoreX(cylinder);
			bend(manifold, { glm::vec3(x, 0.f, MANIFOLD_FLANGE - 0.002f), glm::vec3(x, 0.f, 0.065f), glm::vec3(x, 0.004f, 0.095f), glm::vec3(x, 0.022f, 0.122f) },
				0.018f, 0.016f, WHITE);
		}
		Cylinder(manifold, AlongX(PLENUM), PLENUM_RADIUS, PLENUM_LENGTH, WHITE, 28);
		for (float end : { -1.f, 1.f })
			Cylinder(manifold, AlongX(PLENUM + glm::vec3(end * PLENUM_LENGTH * 0.5f, 0.f, 0.f)), PLENUM_RADIUS + 0.003f, 0.008f, WHITE, 28);
		// The carburettor's pad, its top at the carburettor's seat.
		Box(manifold, { 0.f, 0.071f, PLENUM.z }, { 0.09f, 0.018f, 0.08f });
		// Its ribs between the runners, underneath.
		for (int rib = 0; rib < 3; rib++)
			Box(manifold, { (rib - 1.f) * BORE_SPACING, 0.004f, 0.075f }, { 0.006f, 0.03f, 0.11f });
		// The servo's take-off out the back end, the vacuum nipple on top, and the coolant's two nipples at the front.
		Cylinder(manifold, AlongX(PLENUM + glm::vec3(PLENUM_LENGTH * 0.5f + 0.014f, 0.f, 0.f)), 0.009f, 0.022f, WHITE, 14);
		Tube(manifold, AlongX(PLENUM + glm::vec3(PLENUM_LENGTH * 0.5f + 0.022f, 0.f, 0.f)), 0.011f, 0.006f, 0.004f, WHITE, 0.f, glm::two_pi<float>(), 14);
		Cylinder(manifold, At(PLENUM + glm::vec3(0.12f, PLENUM_RADIUS + 0.006f, 0.02f)), 0.004f, 0.016f, STEEL_GREY, 10);
		for (float y : { -0.012f, 0.014f })
			Cylinder(manifold, AlongX(PLENUM + glm::vec3(-PLENUM_LENGTH * 0.5f - 0.012f, y, 0.01f)), 0.006f, 0.018f, WHITE, 12);
		add(PartType::INTAKE_MANIFOLD, manifold);
	}
	{
		// Origin under its base flange, sitting on the plenum: the body, the float bowl on the side, and
		// the two barrels opening up through the air horn.
		// A two-barrel downdraft in bare zinc alloy, the barrels side by side across it (along Z): primary at -Z, secondary at +Z.
		MeshBuilder carburettor;
		carburettor.SetUVScale(6.f);
		const glm::vec3 ZINC = { 0.80f, 0.79f, 0.74f }, ZINC_DARK = { 0.66f, 0.65f, 0.60f }, BORE = { 0.12f, 0.12f, 0.13f };
		constexpr float BARREL_Z = 0.016f, BARREL = 0.0125f;
		// The base flange on the manifold's pad, a boss round each of its four bolts, its top where their heads sit.
		Box(carburettor, { 0.f, 0.003f, 0.f }, { 0.08f, 0.006f, 0.07f }, ZINC_DARK);
		for (float x : { -0.036f, 0.036f })
			for (float z : { -0.03f, 0.03f })
				Cylinder(carburettor, At({ x, 0.003f, z }), 0.0065f, 0.006f, ZINC_DARK, 12);
		// The throttle body, the throttle shaft across both barrels, and on its end the throttle lever, the cable's arm
		// and the return spring.
		Box(carburettor, { 0.f, 0.017f, 0.f }, { 0.062f, 0.022f, 0.052f }, ZINC);
		Cylinder(carburettor, AlongZ({ -0.012f, 0.016f, 0.f }), 0.003f, 0.074f, STEEL_GREY, 10);
		Box(carburettor, { -0.012f, 0.016f, 0.0375f }, { 0.03f, 0.008f, 0.002f }, STEEL_GREY);
		Cylinder(carburettor, AlongZ({ -0.026f, 0.02f, 0.039f }), 0.0025f, 0.006f, STEEL_GREY, 8);
		Cylinder(carburettor, At({ 0.004f, 0.022f, 0.035f }), 0.003f, 0.014f, { 0.35f, 0.35f, 0.38f }, 8);
		// The main body, and the float bowl on its +X side with its sight glass and the fuel's inlet.
		Box(carburettor, { 0.f, 0.045f, 0.f }, { 0.064f, 0.034f, 0.058f }, ZINC);
		Box(carburettor, { 0.043f, 0.042f, 0.f }, { 0.024f, 0.042f, 0.05f }, ZINC);
		Cylinder(carburettor, AlongX({ 0.0555f, 0.04f, -0.008f }), 0.007f, 0.001f, { 0.45f, 0.55f, 0.62f }, 14);
		Cylinder(carburettor, AlongX({ 0.06f, 0.054f, 0.014f }), 0.004f, 0.018f, STEEL_GREY, 10);
		Cylinder(carburettor, AlongX({ 0.066f, 0.054f, 0.014f }), 0.0048f, 0.003f, STEEL_GREY, 10);
		// The accelerator pump's housing on the primary side, and the secondary's vacuum pod on the -X side with its rod.
		Cylinder(carburettor, AlongZ({ 0.026f, 0.05f, -0.032f }), 0.008f, 0.01f, ZINC, 12);
		Cylinder(carburettor, AlongX({ -0.039f, 0.032f, 0.016f }), 0.012f, 0.008f, ZINC_DARK, 16);
		Cylinder(carburettor, AlongX({ -0.0435f, 0.032f, 0.016f }), 0.0125f, 0.002f, ZINC, 16);
		Box(carburettor, { -0.038f, 0.022f, 0.016f }, { 0.002f, 0.018f, 0.002f }, STEEL_GREY);
		// The automatic choke's housing on the -X side by the primary, its black bimetal cover held by three screws.
		Cylinder(carburettor, AlongX({ -0.036f, 0.056f, -0.014f }), 0.013f, 0.008f, ZINC, 18);
		Cylinder(carburettor, AlongX({ -0.042f, 0.056f, -0.014f }), 0.014f, 0.006f, BLACK, 18);
		for (int screw = 0; screw < 3; screw++)
		{
			float angle = screw * glm::two_pi<float>() / 3.f + 0.4f;
			Cylinder(carburettor, AlongX({ -0.0455f, 0.056f + std::sin(angle) * 0.011f, -0.014f + std::cos(angle) * 0.011f }), 0.0018f, 0.001f, STEEL_GREY, 6);
		}
		// The air horn on top: its plate with the two barrels open through it (dark down inside, the venturis showing),
		// the choke's plate across the primary, the air cleaner's sealing ring round both and its stud between them, and
		// the cover's screws.
		constexpr float HORN_TOP = 0.072f;
		Box(carburettor, { 0.f, 0.064f, 0.f }, { 0.07f, 0.004f, 0.064f }, ZINC);
		for (float side : { -1.f, 1.f })
		{
			carburettor.ResetTransform().SetColor(ZINC).AddPlateWithHole({ 0.f, HORN_TOP, side * BARREL_Z }, { 0.07f, 0.032f }, BARREL, 20);
			Tube(carburettor, At({ 0.f, (0.066f + HORN_TOP) * 0.5f, side * BARREL_Z }), BARREL + 0.0005f, BARREL, HORN_TOP - 0.066f, ZINC, 0.f, glm::two_pi<float>(), 20);
			Box(carburettor, { 0.f, 0.069f, side * (0.032f - 0.0015f) }, { 0.07f, 0.006f, 0.003f }, ZINC);
			Cylinder(carburettor, At({ 0.f, 0.0665f, side * BARREL_Z }), BARREL, 0.001f, BORE, 20);
			Tube(carburettor, At({ 0.f, 0.067f, side * BARREL_Z }), BARREL, 0.007f, 0.002f, ZINC_DARK, 0.f, glm::two_pi<float>(), 20);
		}
		for (float x : { -0.035f, 0.035f })
			Box(carburettor, { x - glm::sign(x) * 0.0015f, 0.069f, 0.f }, { 0.003f, 0.006f, 0.064f }, ZINC);
		carburettor.SetTransform(glm::rotate(At({ 0.f, 0.0705f, -BARREL_Z }), 0.25f, glm::vec3(0.f, 0.f, 1.f))).SetColor(STEEL_GREY);
		carburettor.AddCylinder({}, BARREL - 0.0008f, 0.0008f, 18);
		carburettor.ResetTransform();
		Tube(carburettor, At({ 0.f, HORN_TOP + 0.003f, 0.f }), 0.034f, 0.0315f, 0.006f, ZINC_DARK, 0.f, glm::two_pi<float>(), 32);
		Cylinder(carburettor, At({ 0.f, HORN_TOP + 0.012f, 0.f }), 0.0022f, 0.024f, STEEL_GREY, 8);
		for (float x : { -0.03f, 0.03f })
			for (float z : { -0.027f, 0.027f })
				Cylinder(carburettor, At({ x, HORN_TOP + 0.0006f, z }), 0.002f, 0.0012f, STEEL_GREY, 6);
		add(PartType::CARBURETTOR, carburettor);
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
		// Built standing up from its mounting flange (+Y out from the head): the diaphragm housing,
		// with the inlet and outlet pipes across its top.
		MeshBuilder pump;
		pump.SetUVScale(6.f);
		Box(pump, { 0.f, 0.002f, 0.f }, { 0.06f, 0.004f, 0.025f });
		Cylinder(pump, At({ 0.f, 0.016f, 0.f }), 0.022f, 0.024f, WHITE, 20);
		Cylinder(pump, AlongX({ 0.f, 0.022f, 0.f }), 0.004f, 0.064f, STEEL_GREY, 8);
		// The rocker arm, reaching in through the head under the cam, then up (+Z here is up the engine) to ride on the
		// underside of its eccentric.
		constexpr float REACH = PORT_FACE_Z - 0.006f, RISE = FUEL_PUMP_DROP - 0.012f;
		Box(pump, { 0.f, -REACH * 0.5f, 0.f }, { 0.006f, REACH, 0.008f }, STEEL_GREY);
		Box(pump, { 0.f, -REACH + 0.003f, RISE * 0.5f }, { 0.006f, 0.006f, RISE + 0.008f }, STEEL_GREY);
		add(PartType::FUEL_PUMP, pump);
	}
	{
		// Origin at the middle of the head flange; the primaries run out along -Z into a collector.
		// Cast iron, four into one: the primaries out from the ports, curving down and in round each other into the
		// collector, which turns down to the outlet's flange (the downpipe's bolts up into it).
		MeshBuilder manifold;
		manifold.SetUVScale(3.f);
		headFlange(manifold, -1.f, RUST);
		for (int cylinder = 0; cylinder < 4; cylinder++)
		{
			float x = BoreX(cylinder);
			// Where each comes into the top of the collector: a quarter of it each.
			glm::vec2 into = glm::vec2(cylinder < 2 ? -1.f : 1.f, cylinder % 2 == 0 ? -1.f : 1.f) * 0.011f;
			bend(manifold, { glm::vec3(x, 0.f, -MANIFOLD_FLANGE + 0.002f), glm::vec3(x, 0.f, -0.055f),
				glm::vec3(glm::mix(x, EXHAUST_OUTLET.x, 0.5f), -0.02f, EXHAUST_OUTLET.z + 0.01f),
				glm::vec3(EXHAUST_OUTLET.x + into.x, -0.04f, EXHAUST_OUTLET.z + into.y) }, 0.017f, 0.015f, RUST, 10);
		}
		// The collector, necking down to the outlet, and the outlet's flange with its three bolt bosses.
		Cylinder(manifold, At({ EXHAUST_OUTLET.x, -0.058f, EXHAUST_OUTLET.z }), 0.029f, 0.04f, RUST, 20);
		Cylinder(manifold, At({ EXHAUST_OUTLET.x, -0.097f, EXHAUST_OUTLET.z }), 0.023f, 0.034f, RUST, 20);
		Cylinder(manifold, At({ EXHAUST_OUTLET.x, EXHAUST_OUTLET.y + 0.003f, EXHAUST_OUTLET.z }), 0.034f, 0.006f, RUST, 24);
		for (int bolt = 0; bolt < 3; bolt++)
		{
			float angle = bolt * glm::two_pi<float>() / 3.f;
			Cylinder(manifold, At({ EXHAUST_OUTLET.x + std::cos(angle) * EXHAUST_BOLT_RING, EXHAUST_OUTLET.y + 0.003f,
				EXHAUST_OUTLET.z + std::sin(angle) * EXHAUST_BOLT_RING }), 0.009f, 0.006f, RUST, 14);
		}
		add(PartType::EXHAUST_MANIFOLD, manifold);
	}
	{
		// In the manifold's own space (they share a mount): the flange under its collector, and the
		// pipe on down from it.
		MeshBuilder pipe;
		pipe.SetUVScale(3.f);
		Cylinder(pipe, At(EXHAUST_OUTLET - glm::vec3(0.f, 0.003f, 0.f)), 0.034f, 0.006f, RUST, 24);
		for (int bolt = 0; bolt < 3; bolt++)
		{
			float angle = bolt * glm::two_pi<float>() / 3.f;
			Cylinder(pipe, At(EXHAUST_OUTLET + glm::vec3(std::cos(angle) * EXHAUST_BOLT_RING, -0.003f, std::sin(angle) * EXHAUST_BOLT_RING)), 0.009f, 0.006f, RUST, 14);
		}
		Cylinder(pipe, At(EXHAUST_OUTLET - glm::vec3(0.f, 0.106f, 0.f)), 0.022f, 0.2f, STEEL_GREY, 18);
		add(PartType::EXHAUST_PIPE, pipe);
	}
	{
		MeshBuilder gasket;
		// A ring the size of the pump's flange, round its nose.
		Tube(gasket, At({ 0.f, 0.00075f, 0.f }), 0.0355f, WATER_PUMP_NOSE + 0.0015f, 0.0015f, GASKET, 0.f, glm::two_pi<float>(), 28);
		add(PartType::WATER_PUMP_GASKET, gasket);
	}
	{
		// Origin at the back of its flange (on its gasket on the timing cover's ear), the pulley out along -X. Cast aluminium.
		MeshBuilder pump;
		pump.SetUVScale(6.f);
		const glm::vec3 CAST = { 0.74f, 0.75f, 0.77f };
		// The nose, back through the cover's ear to the block's coolant boss, an O-ring near its end, and the impeller on it.
		constexpr float NOSE_LENGTH = FRONT_X - WATER_PUMP_X - 0.002f;
		Cylinder(pump, AlongX({ NOSE_LENGTH * 0.5f, 0.f, 0.f }), WATER_PUMP_NOSE, NOSE_LENGTH, CAST, 24);
		Tube(pump, AlongX({ NOSE_LENGTH - 0.006f, 0.f, 0.f }), WATER_PUMP_NOSE + 0.0012f, WATER_PUMP_NOSE - 0.001f, 0.0028f, RUBBER, 0.f, glm::two_pi<float>(), 24);
		Cylinder(pump, AlongX({ NOSE_LENGTH + 0.003f, 0.f, 0.f }), 0.009f, 0.006f, STEEL_GREY, 14);
		for (int vane = 0; vane < 6; vane++)
		{
			pump.SetTransform(glm::rotate(At({ NOSE_LENGTH + 0.003f, 0.f, 0.f }), vane * glm::pi<float>() / 3.f, { 1.f, 0.f, 0.f })).SetColor(STEEL_GREY);
			pump.AddBox({ 0.f, 0.016f, 0.f }, { 0.006f, 0.016f, 0.0018f });
		}
		pump.ResetTransform();
		// The flange, with a boss at each bolt hole on its front.
		Cylinder(pump, AlongX({ -0.003f, 0.f, 0.f }), 0.0355f, 0.006f, CAST, 28);
		for (int bolt = 0; bolt < 4; bolt++)
		{
			float angle = glm::quarter_pi<float>() + bolt * glm::half_pi<float>();
			Cylinder(pump, AlongX({ -0.0075f, std::sin(angle) * WATER_PUMP_BOLT_RING, std::cos(angle) * WATER_PUMP_BOLT_RING }), 0.0065f, 0.003f, CAST, 14);
		}
		// The volute, its ribs, and the inlet neck out the left side for the bottom hose, a bead round its end.
		Cylinder(pump, AlongX({ -0.016f, 0.f, 0.f }), 0.027f, 0.02f, CAST, 26);
		for (int rib = 0; rib < 3; rib++)
		{
			pump.SetTransform(glm::rotate(At({ -0.016f, 0.f, 0.f }), glm::half_pi<float>() + rib * glm::two_pi<float>() / 3.f, { 1.f, 0.f, 0.f })).SetColor(CAST);
			pump.AddBox({ 0.f, 0.027f, 0.f }, { 0.018f, 0.006f, 0.003f });
		}
		pump.ResetTransform();
		Cylinder(pump, AlongZ({ -0.016f, -0.006f, -0.041f }), 0.0105f, 0.03f, CAST, 16);
		Cylinder(pump, AlongZ({ -0.016f, -0.006f, -0.051f }), 0.012f, 0.003f, CAST, 16);
		Cylinder(pump, AlongZ({ -0.016f, -0.006f, -0.0562f }), 0.0075f, 0.0008f, DARK, 14);
		// The bearing's snout, its weep hole underneath, and the hub flange the pulley bolts to.
		Cylinder(pump, AlongX({ -0.031f, 0.f, 0.f }), 0.015f, 0.01f, CAST, 20);
		Cylinder(pump, At({ -0.031f, -0.015f, 0.f }), 0.0016f, 0.0008f, DARK, 8);
		Cylinder(pump, AlongX({ -0.0375f, 0.f, 0.f }), 0.02f, 0.003f, STEEL_GREY, 20);
		// The pulley in the fan belt's plane: a grooved rim, its web with lightening holes, and the nut on the hub.
		constexpr float PULLEY_X = ACCESSORY_BELT_X - WATER_PUMP_X;
		const glm::vec3 PULLEY = { 0.6f, 0.6f, 0.62f };
		Tube(pump, AlongX({ PULLEY_X, 0.f, 0.f }), 0.05f, 0.043f, 0.012f, PULLEY, 0.f, glm::two_pi<float>(), 32);
		for (float side : { -1.f, 1.f })
			Tube(pump, AlongX({ PULLEY_X + side * 0.0068f, 0.f, 0.f }), 0.0555f, 0.046f, 0.0016f, PULLEY, 0.f, glm::two_pi<float>(), 32);
		Cylinder(pump, AlongX({ PULLEY_X + 0.004f, 0.f, 0.f }), 0.043f, 0.003f, PULLEY, 28);
		for (int hole = 0; hole < 4; hole++)
		{
			float angle = hole * glm::half_pi<float>();
			Cylinder(pump, AlongX({ PULLEY_X + 0.004f, std::sin(angle) * 0.029f, std::cos(angle) * 0.029f }), 0.0075f, 0.0034f, DARK, 14);
		}
		Cylinder(pump, AlongX({ (PULLEY_X + 0.004f - 0.0375f) * 0.5f, 0.f, 0.f }), 0.011f, -0.0375f - PULLEY_X - 0.004f, STEEL_GREY, 16);
		Cylinder(pump, AlongX({ PULLEY_X + 0.001f, 0.f, 0.f }), 0.0085f, 0.004f, DARK, 6);
		add(PartType::WATER_PUMP, pump);
	}
	{
		// Origin at its pulley, the body running back along +X towards the block: the grooved pulley and its nut, the fan
		// behind it, the cast drive-end housing with its vents, the stator's band, the rear housing and its cover with the
		// output stud and the plug; the pivot's two lugs underneath and the adjusting lug on top.
		MeshBuilder alternator;
		alternator.SetUVScale(6.f);
		const glm::vec3 CAST = { 0.80f, 0.80f, 0.78f }, STATOR = { 0.36f, 0.36f, 0.38f }, PULLEY = { 0.42f, 0.42f, 0.45f };
		// The pulley: its V between two lips, the belt riding in it; the nut and washer on the shaft.
		Tube(alternator, AlongX({}), 0.032f, 0.008f, 0.01f, PULLEY, 0.f, glm::two_pi<float>(), 28);
		for (float side : { -1.f, 1.f })
			Tube(alternator, AlongX({ side * 0.006f, 0.f, 0.f }), 0.037f, 0.008f, 0.002f, PULLEY, 0.f, glm::two_pi<float>(), 28);
		Cylinder(alternator, AlongX({ -0.008f, 0.f, 0.f }), 0.009f, 0.002f, STEEL_GREY, 16);
		Cylinder(alternator, AlongX({ -0.0105f, 0.f, 0.f }), 0.0075f, 0.004f, DARK, 6);
		// The fan: a hub and its blades, swept.
		Cylinder(alternator, AlongX({ 0.0115f, 0.f, 0.f }), 0.018f, 0.004f, STEEL_GREY, 18);
		for (int blade = 0; blade < 12; blade++)
		{
			float angle = blade * glm::two_pi<float>() / 12.f;
			alternator.SetTransform(glm::rotate(glm::rotate(At({ 0.0115f, 0.f, 0.f }), angle, glm::vec3(1.f, 0.f, 0.f)), 0.35f, glm::vec3(0.f, 1.f, 0.f)))
				.SetColor(STEEL_GREY).AddBox({ 0.f, 0.03f, 0.f }, { 0.008f, 0.024f, 0.0015f });
		}
		alternator.ResetTransform();
		// The drive-end housing, its vents round it, and a through-bolt boss at each corner the length of the body.
		Cylinder(alternator, AlongX({ 0.0305f, 0.f, 0.f }), 0.054f, 0.029f, CAST, 32);
		for (int vent = 0; vent < 10; vent++)
		{
			float angle = (vent + 0.5f) * glm::two_pi<float>() / 10.f;
			alternator.SetTransform(glm::rotate(At({ 0.026f, 0.f, 0.f }), angle, glm::vec3(1.f, 0.f, 0.f))).SetColor(DARK)
				.AddBox({ 0.f, 0.0542f, 0.f }, { 0.012f, 0.001f, 0.014f });
		}
		alternator.ResetTransform();
		Cylinder(alternator, AlongX({ 0.0505f, 0.f, 0.f }), 0.057f, 0.011f, STATOR, 32);
		Cylinder(alternator, AlongX({ 0.071f, 0.f, 0.f }), 0.052f, 0.03f, CAST, 32);
		for (int bolt = 0; bolt < 4; bolt++)
		{
			float angle = glm::quarter_pi<float>() + bolt * glm::half_pi<float>();
			Cylinder(alternator, AlongX({ 0.055f, std::sin(angle) * 0.055f, std::cos(angle) * 0.055f }), 0.004f, 0.07f, CAST, 10);
			Cylinder(alternator, AlongX({ 0.0905f, std::sin(angle) * 0.055f, std::cos(angle) * 0.055f }), 0.0035f, 0.001f, STEEL_GREY, 6);
		}
		Cylinder(alternator, AlongX({ 0.088f, 0.f, 0.f }), 0.044f, 0.006f, CAST, 28);
		// The output (B+) stud on the rear cover, its nut, and the regulator's plug in its socket.
		Cylinder(alternator, AlongX({ 0.095f, 0.022f, 0.02f }), 0.0025f, 0.01f, { 0.75f, 0.6f, 0.3f }, 8);
		Cylinder(alternator, AlongX({ 0.0925f, 0.022f, 0.02f }), 0.005f, 0.003f, STEEL_GREY, 6);
		Box(alternator, { 0.08f, 0.032f, -0.038f }, { 0.016f, 0.012f, 0.018f }, BLACK);
		Box(alternator, { 0.08f, 0.042f, -0.042f }, { 0.012f, 0.01f, 0.012f }, BLACK);
		// The pivot's lugs (front and back of the bracket's boss), and the adjusting lug on top, each on a web off the body.
		for (float x : { ALTERNATOR_PIVOT_X - 0.028f, ALTERNATOR_PIVOT_X + 0.028f })
		{
			Box(alternator, { x, (ALTERNATOR_PIVOT_Y - 0.04f) * 0.5f, 0.f }, { 0.012f, -ALTERNATOR_PIVOT_Y - 0.04f, 0.016f }, CAST);
			Cylinder(alternator, AlongX({ x, ALTERNATOR_PIVOT_Y, 0.f }), 0.009f, 0.012f, CAST, 16);
		}
		Box(alternator, { ALTERNATOR_ADJUST_X + 0.006f, (ALTERNATOR_ADJUST_Y + 0.04f) * 0.5f, 0.f }, { 0.012f, ALTERNATOR_ADJUST_Y - 0.04f, 0.014f }, CAST);
		Cylinder(alternator, AlongX({ ALTERNATOR_ADJUST_X + 0.006f, ALTERNATOR_ADJUST_Y, 0.f }), 0.008f, 0.012f, CAST, 16);
		add(PartType::ALTERNATOR, alternator);
	}
	{
		// Cast iron, built round the alternator's pivot (its origin), in the engine's turn: the pivot's boss, an arm back
		// under the alternator, a web across to the side of the block, the plate that bolts to it (three bolts), and a post
		// up behind the alternator to the slotted strap its adjusting lug locks to.
		MeshBuilder bracket;
		bracket.SetUVScale(5.f);
		const glm::vec3 IRON = { 0.30f, 0.30f, 0.32f };
		const glm::vec3 origin = { ALTERNATOR_BRACKET_X, ALTERNATOR_BRACKET_Y, ALTERNATOR_Z };
		auto block = [&](const glm::vec3& lo, const glm::vec3& hi, const glm::vec3& color) {
			Box(bracket, (lo + hi) * 0.5f - origin, hi - lo, color);
		};
		constexpr float SIDE = BLOCK_WIDTH * 0.5f, PLATE = 0.007f;
		// The pivot's boss, between the alternator's lugs.
		Cylinder(bracket, AlongX({}), 0.011f, 0.042f, IRON, 18);
		// The arm under it, back to the web behind the alternator.
		block({ FRONT_X - 0.07f, -0.094f, ALTERNATOR_Z - 0.012f }, { FRONT_X + 0.025f, -0.073f, ALTERNATOR_Z + 0.012f }, IRON);
		// The web across to the block, the plate on it, and a gusset between them.
		block({ FRONT_X + 0.005f, -0.097f, SIDE + PLATE }, { FRONT_X + 0.025f, -0.055f, ALTERNATOR_Z + 0.012f }, IRON);
		block({ FRONT_X + 0.004f, -0.107f, SIDE }, { FRONT_X + 0.068f, -0.033f, SIDE + PLATE }, IRON);
		block({ FRONT_X + 0.025f, -0.08f, SIDE + PLATE }, { FRONT_X + 0.068f, -0.072f, SIDE + 0.03f }, IRON);
		for (const auto& bolt : ALTERNATOR_BRACKET_BOLTS)
			Cylinder(bracket, AlongZ(glm::vec3(FRONT_X + bolt.x, bolt.y, SIDE + PLATE + 0.0015f) - origin), 0.009f, 0.003f, IRON, 14);
		// The post up behind the alternator, and the strap across its top, slotted for the adjusting bolt to slide in.
		block({ FRONT_X + 0.001f, -0.066f, ALTERNATOR_Z - 0.01f }, { FRONT_X + 0.015f, 0.05f, ALTERNATOR_Z + 0.01f }, IRON);
		constexpr float STRAP_X = ACCESSORY_BELT_X + ALTERNATOR_ADJUST_X + 0.012f;		// behind the adjusting lug
		block({ STRAP_X, 0.048f, ALTERNATOR_Z - 0.026f }, { FRONT_X + 0.015f, 0.08f, ALTERNATOR_Z + 0.026f }, IRON);
		block({ STRAP_X - 0.0005f, ALTERNATOR_ADJUST_Y - 0.0045f, ALTERNATOR_Z - 0.018f }, { STRAP_X, ALTERNATOR_ADJUST_Y + 0.0045f, ALTERNATOR_Z + 0.018f }, BLACK);
		add(PartType::ALTERNATOR_BRACKET, bracket);
	}
	{
		// The fan belt, an 11 mm V-belt, in its plane (X = 0) round its pulleys.
		MeshBuilder belt;
		belt.SetUVScale(20.f);
		constexpr float WIDTH = 0.011f, THICK = 0.008f;
		BeltAlong(belt, BeltPath(DrivePulleys(), THICK * 0.5f), WIDTH, THICK, { 0.09f, 0.09f, 0.10f });
		add(PartType::DRIVE_BELT, belt);
	}
	{
		// Origin at its pulley, in the belt's plane: a smooth pulley on its spindle, and the arm back up
		// to the sprung pivot that bolts to the timing cover.
		MeshBuilder tensioner;
		tensioner.SetUVScale(6.f);
		constexpr float COVER_FACE = (TIMING_COVER_X - TIMING_COVER_FACE) - ACCESSORY_BELT_X;	// along +X, where it meets the cover
		const glm::vec2 pivot = { 0.02f, 0.035f };	// (Z, Y) from the pulley
		Cylinder(tensioner, AlongX({}), DRIVE_TENSIONER_RADIUS, 0.014f, STEEL_GREY, 28);
		Cylinder(tensioner, AlongX({ -0.0085f, 0.f, 0.f }), 0.011f, 0.003f, DARK, 18);
		Cylinder(tensioner, AlongX({ 0.013f, 0.f, 0.f }), 0.009f, 0.012f, WHITE, 16);
		float armLength = glm::length(pivot);
		tensioner.SetTransform(glm::rotate(At({ 0.022f, pivot.y * 0.5f, pivot.x * 0.5f }), std::atan2(-pivot.y, pivot.x), { 1.f, 0.f, 0.f })).SetColor(WHITE);
		tensioner.AddBox({}, { 0.008f, 0.018f, armLength });
		tensioner.ResetTransform();
		Cylinder(tensioner, AlongX({ (0.018f + COVER_FACE) * 0.5f, pivot.y, pivot.x }), 0.018f, COVER_FACE - 0.018f, WHITE, 20);
		Cylinder(tensioner, AlongX({ 0.02f, pivot.y, pivot.x }), 0.008f, 0.006f, DARK, 14);
		add(PartType::DRIVE_BELT_TENSIONER, tensioner);
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
		// A bolt ear on two opposite corners (clear of the body and of the outlet's neck), the bolts' heads on them.
		for (float side : { -1.f, 1.f })
			Cylinder(housing, At({ side * THERMOSTAT_BOLT.x, 0.003f, -side * THERMOSTAT_BOLT.y }), 0.009f, 0.006f, WHITE, 16);
		Cylinder(housing, At({ 0.f, 0.016f, 0.f }), 0.022f, 0.022f, WHITE, 20);
		Cylinder(housing, AlongX({ 0.035f, 0.022f, 0.f }), 0.015f, 0.05f, WHITE, 16);
		add(PartType::THERMOSTAT_HOUSING, housing);
	}
	{
		// The thermostat's: a rubber ring a little bigger than its flange (a lip of it shows round it), round its valve.
		MeshBuilder gasket;
		Tube(gasket, At({ 0.f, 0.0005f, 0.f }), 0.028f, 0.0185f, 0.001f, RUBBER, 0.f, glm::two_pi<float>(), 24);
		add(PartType::THERMOSTAT_GASKET, gasket);
	}
	{
		// The housing's: a fibre sheet the shape of the housing's base, the outlet's hole through it (front and back).
		MeshBuilder gasket;
		gasket.SetColor(GASKET).AddPlateWithHole({ 0.f, 0.001f, 0.f }, { 0.06f, 0.05f }, 0.019f, 24);
		gasket.SetTransform(glm::rotate(glm::mat4(1.f), glm::pi<float>(), { 1.f, 0.f, 0.f })).AddPlateWithHole({ 0.f, 0.f, 0.f }, { 0.06f, 0.05f }, 0.019f, 24);
		gasket.ResetTransform();
		Tube(gasket, At({ 0.f, 0.0005f, 0.f }), 0.0192f, 0.019f, 0.001f, GASKET, 0.f, glm::two_pi<float>(), 24);
		// Its ears under the housing's, the bolts' holes through them.
		for (float side : { -1.f, 1.f })
		{
			Tube(gasket, At({ side * THERMOSTAT_BOLT.x, 0.0005f, -side * THERMOSTAT_BOLT.y }), 0.009f, 0.0045f, 0.001f, GASKET, 0.f, glm::two_pi<float>(), 16);
			Cylinder(gasket, At({ side * THERMOSTAT_BOLT.x, 0.0005f, -side * THERMOSTAT_BOLT.y }), 0.0045f, 0.0009f, DARK, 12);
		}
		add(PartType::THERMOSTAT_HOUSING_GASKET, gasket);
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
			case FurnitureKind::MONCHU:
			{
				// Only if monchu.fbx won't load (models.cfg): a monkey sat on a plinth.
				const glm::vec3 fur = { 0.36f, 0.22f, 0.12f }, face = { 0.78f, 0.6f, 0.46f };
				Box(builder, { 0.f, 0.1f, 0.f }, { 0.5f, 0.2f, 0.5f }, { 0.3f, 0.3f, 0.32f });
				Box(builder, { 0.f, 0.45f, 0.f }, { 0.34f, 0.5f, 0.28f }, fur);
				Box(builder, { 0.f, 0.82f, 0.f }, { 0.26f, 0.24f, 0.24f }, fur);
				Box(builder, { 0.f, 0.8f, -0.125f }, { 0.18f, 0.16f, 0.02f }, face);
				for (float side : { -1.f, 1.f })
					Box(builder, { side * 0.15f, 0.84f, 0.f }, { 0.05f, 0.08f, 0.05f }, face);
				break;
			}
		}
		AddModel(FurnitureModel(furniture), builder);
	}
}

const std::vector<EngineLayout::BeltPulley>& EngineLayout::TimingPulleys()
{
	// Down the tight side from the cam to the crank, and back up the slack side over the idler.
	static const std::vector<BeltPulley> pulleys = {
		{ "Cam pulley", { 0.f, CamAxisY() }, CAM_PULLEY_TEETH, 1.f },
		{ "Crank pulley", { 0.f, CRANK_Y }, CRANK_PULLEY_TEETH, 1.f },
		{ "Idler pulley", { TIMING_IDLER_Z, TIMING_IDLER_Y }, TIMING_IDLER_RADIUS, -1.f },
	};
	return pulleys;
}

const std::vector<EngineLayout::BeltPulley>& EngineLayout::DrivePulleys()
{
	// Up the left from the crank to the water pump, across the top to the alternator, and back down.
	static const std::vector<BeltPulley> pulleys = {
		{ "Crank", { 0.f, CRANK_Y }, 0.058f, 1.f },
		{ "Water pump", { WATER_PUMP_Z, WATER_PUMP_Y }, 0.05f, 1.f },
		{ "Alternator", { ALTERNATOR_Z, ALTERNATOR_Y }, 0.032f, 1.f },
	};
	return pulleys;
}

namespace
{
	// The straight run from each pulley to the next: on the same side of both for two wrapped the same
	// way, crossing between them otherwise. Its ends, on each pulley, offset out from them.
	std::vector<std::pair<glm::vec2, glm::vec2>> BeltRuns(const std::vector<EngineLayout::BeltPulley>& pulleys, float offset)
	{
		size_t count = pulleys.size();
		std::vector<std::pair<glm::vec2, glm::vec2>> runs(count);
		for (size_t i = 0; i < count; i++)
		{
			const auto& a = pulleys[i];
			const auto& b = pulleys[(i + 1) % count];
			glm::vec2 between = b.centre - a.centre;
			float distance = glm::length(between), heading = std::atan2(between.y, between.x);
			float ra = a.wrap * (a.radius + offset), rb = b.wrap * (b.radius + offset);
			for (float side : { 1.f, -1.f })
			{
				float angle = heading + side * std::acos(std::clamp((rb - ra) / distance, -1.f, 1.f));
				glm::vec2 normal = { std::cos(angle), std::sin(angle) };
				if (glm::dot(glm::vec2(-normal.y, normal.x), between) <= 0.f)
					continue;
				runs[i] = { a.centre - normal * ra, b.centre - normal * rb };
				break;
			}
		}
		return runs;
	}
}

std::vector<glm::vec2> EngineLayout::BeltPath(const std::vector<BeltPulley>& pulleys, float offset)
{
	// Round each pulley from where the last run meets it to where the next leaves it; the runs are the
	// straight joins from one pulley's last point to the next one's first.
	size_t count = pulleys.size();
	auto runs = BeltRuns(pulleys, offset);
	std::vector<glm::vec2> path;
	for (size_t i = 0; i < count; i++)
	{
		const auto& pulley = pulleys[i];
		glm::vec2 arrive = runs[(i + count - 1) % count].second - pulley.centre;
		glm::vec2 leave = runs[i].first - pulley.centre;
		float start = std::atan2(arrive.y, arrive.x);
		float sweep = pulley.wrap > 0.f ? start - std::atan2(leave.y, leave.x) : std::atan2(leave.y, leave.x) - start;
		sweep = std::fmod(sweep + glm::two_pi<float>() * 2.f, glm::two_pi<float>());
		int segments = (std::max)(2, static_cast<int>(sweep / glm::two_pi<float>() * 40.f));
		float radius = pulley.radius + offset;
		for (int segment = 0; segment <= segments; segment++)
		{
			float angle = start - pulley.wrap * sweep * segment / segments;
			path.push_back(pulley.centre + radius * glm::vec2(std::cos(angle), std::sin(angle)));
		}
	}
	return path;
}

glm::vec2 EngineLayout::BeltRunMiddle(const std::vector<BeltPulley>& pulleys, size_t run)
{
	auto runs = BeltRuns(pulleys, 0.f);
	return (runs[run % runs.size()].first + runs[run % runs.size()].second) * 0.5f;
}
