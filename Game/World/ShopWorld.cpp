#include "ShopWorld.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <unordered_set>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "LV/Engine/Graphics/Models/ModelManager.h"
#include "LV/Engine/Objects/ObjectManager.h"
#include "LV/Engine/Utility/InputManager.h"
#include "../Controls.h"
#include "../FrameProfiler.h"
#include "../Simulation/Random.h"
#include "../UI/ItemIcons.h"
#include "../UI/PartIcons.h"
#include "GarageLayout.h"
#include "ShopWorldShared.h"
#include "ToolShapes.h"

using namespace ShopLayout;

namespace
{
	// A part that has others fitted to it off the engine, at places on it: a cylinder head (its valvetrain) and a rocker
	// shaft (its arms).
	bool TakesLooseFittings(PartType type) { return type == PartType::CYLINDER_HEAD || type == PartType::ROCKER_SHAFT; }

	// How a tool sits in the right hand, upright: the turn from lying flat on the bench into the
	// hand's view (X to the right, Y up, Z back towards you), and the point in the model the fist
	// closes round. Only the tools whose models need it; the rest stand up on their longest side,
	// held at the middle.
	struct ToolGrip
	{
		glm::quat turn = {};
		glm::vec3 point = {};
	};
	std::optional<ToolGrip> GripFor(const MeasuringTool& tool)
	{
		// The model's X, Y and Z, as they end up in the hand.
		auto turn = [](const glm::vec3& x, const glm::vec3& y, const glm::vec3& z) { return glm::quat_cast(glm::mat3(x, y, z)); };
		switch (ToolShapeFor(tool))
		{
			case ToolShape::CALIPER:
				// The jaws at the top, reaching out to the right, and the readout facing you; held by the
				// far end of the beam.
				return ToolGrip{ turn({ 0.f, -1.f, 0.f }, { 0.f, 0.f, 1.f }, { -1.f, 0.f, 0.f }), { 0.07f, 0.f, 0.f } };
			case ToolShape::RATCHET:
			case ToolShape::CORDLESS_RATCHET:
			case ToolShape::BREAKER_BAR:
			case ToolShape::TORQUE_WRENCH:
				// The head at the top, its socket facing away from you (and a torque wrench's dial towards
				// you); held round the handle's grip.
				return ToolGrip{ turn({ 0.f, 1.f, 0.f }, { 0.f, 0.f, 1.f }, { 1.f, 0.f, 0.f }), { -DriveToolLength(tool) * 0.3f, 0.008f, 0.f } };
			case ToolShape::WRENCH:
				// Up on end like a ratchet, the ring at the top, held round the shank near its open end.
				return ToolGrip{ turn({ 0.f, 1.f, 0.f }, { 0.f, 0.f, 1.f }, { 1.f, 0.f, 0.f }), { -WrenchLength(tool.sizes.front()) * 0.2f, 0.002f, 0.f } };
			case ToolShape::IMPACT:
				// Like a pistol: the nose pointing away from you, the grip in the fist and the pack under it.
				return ToolGrip{ turn({ 0.f, 0.f, -1.f }, { 1.f, 0.f, 0.f }, { 0.f, -1.f, 0.f }), IMPACT_GRIP };
			default:
				return std::nullopt;
		}
	}

	// What's carried in a whole body's hand (another player's, or your own in third person), from the
	// middle of the hand: a part sits on top of it; a tool is held at its middle.
	glm::vec3 OnAvatarHand(const ItemVisual& visual, const Model& model)
	{
		constexpr float HAND_HALF_HEIGHT = 0.045f;	// PlayerAvatar's hand
		if (!visual.part)
			return {};
		float middle = (model.GetBoundsMin().y + model.GetBoundsMax().y) * 0.5f;
		return { 0.f, middle - model.GetBoundsMin().y + HAND_HALF_HEIGHT, 0.f };
	}
}

glm::quat Station::Rotation() const
{
	return Yaw(yaw);
}

glm::vec3 Station::Anchor() const
{
	return position + Rotation() * localAnchor;
}

pObject ShopWorld::Spawn(const std::string& model, const std::string& texture, const glm::vec3& position, float yaw)
{
	Object info(position, Yaw(yaw), { 1.f, 1.f, 1.f }, m_assets->Model(model), texture.empty() ? 0u : m_assets->TextureFor(model, texture));
	return ObjectManager::Get()->CreateObject(info);
}

size_t ShopWorld::AddStation(StationType type, const std::string& name, const pObject& object, bool movable,
	const glm::vec3& localAnchor, float partYaw, int machineIndex)
{
	object->SetPickable(true);
	size_t index = m_stations.size();
	m_targets[object.get()] = { TargetKind::STATION, {}, 0, index };

	Station station;
	station.type = type;
	station.name = name;
	station.object = object;
	station.machineIndex = machineIndex;
	station.position = object->GetPosition();
	station.homePosition = station.position;
	station.localAnchor = localAnchor;
	station.partYaw = partYaw;
	station.movable = movable;
	m_stations.push_back(station);
	return index;
}

ShopBox ShopWorld::TransformBox(const ShopBox& box, const glm::vec3& position, const glm::quat& rotation)
{
	ShopBox result = { glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX) };
	for (int corner = 0; corner < 8; corner++)
	{
		glm::vec3 local = { corner & 1 ? box.max.x : box.min.x, corner & 2 ? box.max.y : box.min.y, corner & 4 ? box.max.z : box.min.z };
		glm::vec3 world = position + rotation * local;
		result.min = (glm::min)(result.min, world);
		result.max = (glm::max)(result.max, world);
	}
	return result;
}

ShopBox ShopWorld::WorldBounds(const pObject& object)
{
	auto model = ModelOf(object);
	ShopBox local = { model->GetBoundsMin(), model->GetBoundsMax() };
	glm::mat4 transform = object->GetModelMatrix();

	ShopBox box = { glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX) };
	for (int corner = 0; corner < 8; corner++)
	{
		glm::vec3 corner3 = { corner & 1 ? local.max.x : local.min.x, corner & 2 ? local.max.y : local.min.y, corner & 4 ? local.max.z : local.min.z };
		glm::vec3 world = glm::vec3(transform * glm::vec4(corner3, 1.f));
		box.min = (glm::min)(box.min, world);
		box.max = (glm::max)(box.max, world);
	}
	return box;
}

const Station* ShopWorld::FindStation(StationType type) const
{
	for (const auto& station : m_stations)
		if (station.type == type)
			return &station;
	return nullptr;
}

bool ShopWorld::IsWorkbench(const Station& station) const
{
	return station.type == StationType::WORKBENCH || (station.type == StationType::FURNITURE && station.furnitureIndex >= 0
		&& FurnitureCatalog::All()[station.furnitureIndex].kind == FurnitureKind::WORKBENCH);
}

size_t ShopWorld::AddMachineStation(size_t machineIndex, const glm::vec3& home)
{
	// Only set up once bought; until then it waits at its home spot, hidden.
	const auto& machine = MachineCatalog::All()[machineIndex];
	auto fixture = ShopAssets::FixtureFor(machine);
	auto object = Spawn(fixture.model, ShopTextures::PAINT, home);
	auto index = AddStation(StationType::MACHINE, machine.name, object, true, fixture.partOffset, fixture.partYaw, static_cast<int>(machineIndex));
	auto& station = m_stations[index];
	glm::vec3 offset = fixture.partOffset;
	glm::vec2 table = { fixture.tableSize.x * 0.6f, (std::min)(fixture.tableSize.y, MACHINE_SPACING) * 0.9f };
	station.localSurfaces.push_back({ { offset.x - table.x * 0.5f, 0.f, offset.z - table.y * 0.5f }, { offset.x + table.x * 0.5f, offset.y, offset.z + table.y * 0.5f } });
	station.present = false;
	object->SetVisible(false);
	object->SetPickable(false);
	return index;
}

size_t ShopWorld::AddFurnitureStation(size_t furnitureIndex)
{
	// Delivered crated, and set up in the first free patch of floor when the crate's opened.
	const auto& furniture = FurnitureCatalog::All()[furnitureIndex];
	auto object = Spawn(ShopAssets::FurnitureModel(furniture), furniture.kind == FurnitureKind::WORKBENCH ? ShopTextures::WOOD : ShopTextures::PAINT,
		FURNITURE_HOME);
	auto index = AddStation(StationType::FURNITURE, furniture.name, object, true);
	auto& station = m_stations[index];
	station.furnitureIndex = static_cast<int>(furnitureIndex);
	// What things can be set down on.
	switch (furniture.kind)
	{
		case FurnitureKind::SHELVING:
			for (float board : SHELF_BOARDS)
				station.localSurfaces.push_back({ { -1.6f, board - 0.03f, -0.25f }, { 1.6f, board, 0.25f } });
			break;
		case FurnitureKind::WORKBENCH:		station.localSurfaces.push_back({ { -1.f, 0.f, -0.4f }, { 1.f, 0.9f, 0.4f } }); break;
		case FurnitureKind::TOOL_CART:		station.localSurfaces.push_back({ { -0.34f, 0.f, -0.21f }, { 0.34f, 0.9f, 0.21f } }); break;
		case FurnitureKind::OIL_DRUM:		station.localSurfaces.push_back({ { -0.24f, 0.f, -0.24f }, { 0.24f, 0.88f, 0.24f } }); break;
		case FurnitureKind::STOOL:			station.localSurfaces.push_back({ { -0.14f, 0.f, -0.14f }, { 0.14f, 0.665f, 0.14f } }); break;
		default:							break;
	}
	station.present = false;
	object->SetVisible(false);
	object->SetPickable(false);
	return index;
}

void ShopWorld::SyncFurniture(const Workshop& workshop)
{
	const auto& bought = workshop.GetFurniture();
	for (size_t piece = m_stations.size() - m_fixedStations; piece < bought.size(); piece++)
		AddFurnitureStation(bought[piece]);
}

void ShopWorld::RemoveFurniture()
{
	auto objectManager = ObjectManager::Get();
	for (size_t i = m_fixedStations; i < m_stations.size(); i++)
	{
		m_targets.erase(m_stations[i].object.get());
		objectManager->RemoveObject(m_stations[i].object);
	}
	m_stations.resize(m_fixedStations);
}

size_t ShopWorld::AddShelf(const std::string& name, const glm::vec3& position)
{
	auto shelf = AddStation(StationType::SHELF, name, Spawn(ShopModels::SHELF, ShopTextures::PAINT, position), true);
	for (float board : SHELF_BOARDS)
		m_stations[shelf].localSurfaces.push_back({ { -1.6f, board - 0.03f, -0.25f }, { 1.6f, board, 0.25f } });
	return shelf;
}

const Station* ShopWorld::ShelfFor(const Part& part) const
{
	// The engine's internals on the first shelf, everything bolted around them on the second.
	auto system = PartCatalog::Get(part.GetType()).system;
	bool internals = system == PartSystem::BOTTOM_END || system == PartSystem::TOP_END || system == PartSystem::VALVETRAIN;
	const Station* first = nullptr;
	for (const auto& station : m_stations)
		if (station.type == StationType::SHELF)
		{
			if (!first)
			{
				first = &station;
				if (internals)
					return first;
			}
			else
				return &station;
		}
	return first;
}

const Station* ShopWorld::FindStand(int engine) const
{
	for (const auto& station : m_stations)
		if (station.type == StationType::ENGINE_STAND && station.engineIndex == engine)
			return &station;
	return nullptr;
}

void ShopWorld::Build(const ShopAssets& assets)
{
	m_assets = &assets;
	m_paint.Build(assets);

	Spawn(ShopModels::FLOOR, ShopTextures::CONCRETE, {});
	m_garageWalls = Spawn(ShopModels::WALLS, ShopTextures::PLANKS, {});
	for (const auto& lamp : GarageLayout::LAMPS)
	{
		auto object = Spawn(ShopModels::LAMP, ShopTextures::PAINT, lamp);
		object->SetEmissive(1.4f);
		object->SetCastsShadows(false);
	}

	AddShelf("Parts shelf: engine internals", SHELF_POSITION);


	auto bench = AddStation(StationType::WORKBENCH, "Workbench", Spawn(ShopModels::WORKBENCH, ShopTextures::WOOD, BENCH_POSITION), true, { -0.2f, 0.9f, 0.05f });
	m_stations[bench].localSurfaces.push_back({ { -1.f, 0.f, -0.4f }, { 1.f, 0.9f, 0.4f } });

	// The real measuring tools hang on this board (see LayoutItems).
	auto board = Spawn(ShopModels::TOOL_BOARD, ShopTextures::PEGBOARD, TOOL_BOARD_POSITION);
	AddStation(StationType::TOOL_BOARD, "Tool board", board, true);

	auto firstStand = AddStation(StationType::ENGINE_STAND, "Engine stand 1", Spawn(ShopModels::ENGINE_STAND, ShopTextures::PAINT, STAND_POSITION), true,
		{ 0.f, STAND_MOUNT_HEIGHT, 0.f });
	m_stations[firstStand].engineIndex = 0;

	// Machines are only set up once bought; until then they wait at their home spots, hidden.
	const auto& machines = MachineCatalog::All();
	for (size_t i = 0; i < machines.size(); i++)
	{
		// Machines added since the first release go at the end of the station list (see below).
		if (machines[i].operation == MachineOperation::LINE_BORE || machines[i].removalError > 0.0)
			continue;
		AddMachineStation(i, { MACHINE_ROW_X, 0.f, MACHINE_ROW_START_Z + MACHINE_SPACING * i });
	}

	auto dyno = AddStation(StationType::DYNO, "Dyno cell", Spawn(ShopModels::DYNO, ShopTextures::PAINT, DYNO_POSITION), true);
	m_stations[dyno].localSurfaces.push_back({ { -0.8f, 0.f, -0.45f }, { 0.8f, 0.4f, 0.45f } });
	// A new shop has no dyno: it's bought, and crated until then (see SyncMachines).
	m_stations[dyno].present = false;
	m_stations[dyno].object->SetVisible(false);
	m_stations[dyno].object->SetPickable(false);

	auto desk = AddStation(StationType::COMPUTER, "Parts computer", Spawn(ShopModels::COMPUTER_DESK, ShopTextures::PAINT, DESK_POSITION, glm::pi<float>()), true);
	m_stations[desk].yaw = glm::pi<float>();
	m_stations[desk].homeYaw = glm::pi<float>();
	m_stations[desk].localSurfaces.push_back({ { -0.7f, 0.f, -0.35f }, { 0.7f, 0.76f, 0.35f } });

	BuildOutdoors();
	BuildViewModel();

	// More engine stands can be bought; like machines they wait, hidden, until then. They're added
	// last so the stations before them keep their places in older saves.
	for (int engine = 1; engine < Workshop::MAX_ENGINES; engine++)
	{
		glm::vec3 home = EXTRA_STAND_POSITIONS[engine - 1];
		auto index = AddStation(StationType::ENGINE_STAND, std::format("Engine stand {}", engine + 1),
			Spawn(ShopModels::ENGINE_STAND, ShopTextures::PAINT, home), true, { 0.f, STAND_MOUNT_HEIGHT, 0.f });
		auto& stand = m_stations[index];
		stand.engineIndex = engine;
		stand.present = false;
		stand.object->SetVisible(false);
		stand.object->SetPickable(false);
	}

	// The second parts shelf came later still, so it goes after the stands, then the line borer, then
	// the sanding bench.
	AddShelf("Parts shelf: bolt-ons", SECOND_SHELF_POSITION);
	for (size_t i = 0; i < machines.size(); i++)
		if (machines[i].operation == MachineOperation::LINE_BORE)
			AddMachineStation(i, LINE_BORER_POSITION);
	for (size_t i = 0; i < machines.size(); i++)
		if (machines[i].removalError > 0.0)
			AddMachineStation(i, SANDING_BENCH_POSITION);
	// The lift came with the shop upgrades, after the sanding bench: crated until it's bought.
	m_liftStation = AddStation(StationType::LIFT, "Two-post lift", Spawn(ShopModels::POST_LIFT, ShopTextures::PAINT, LIFT_HOME), true);
	m_stations[m_liftStation].present = false;
	m_stations[m_liftStation].object->SetVisible(false);
	m_stations[m_liftStation].object->SetPickable(false);
	m_liftArms = Spawn(ShopModels::POST_LIFT_ARMS, ShopTextures::PAINT, LIFT_HOME);
	m_liftArms->SetVisible(false);
	// Then the extension's roll-up door, there once the extension is (see UpdateDoor).
	{
		using namespace GarageLayout;
		auto bayDoor = Spawn(ShopModels::ROLLUP_DOOR, ShopTextures::PAINT, { (BAY_DOOR_MIN_X + BAY_DOOR_MAX_X) * 0.5f, BAY_DOOR_HEIGHT, BAY_HALF_DEPTH + 0.06f });
		bayDoor->SetCastsLampShadows(false);
		m_bayDoorStation = AddStation(StationType::DOOR, "Extension roll-up door", bayDoor, false);
		m_stations[m_bayDoorStation].present = false;
		bayDoor->SetVisible(false);
		bayDoor->SetPickable(false);
	}
	// The engine hoist came last of all: crated until it's bought, like the dyno.
	m_hoistStation = AddStation(StationType::HOIST, "Engine hoist", Spawn(ShopModels::ENGINE_HOIST, ShopTextures::PAINT, HOIST_HOME), true);
	m_stations[m_hoistStation].present = false;
	m_stations[m_hoistStation].object->SetVisible(false);
	m_stations[m_hoistStation].object->SetPickable(false);
	// And after it the LE86, parked on the drive. It isn't moved.
	m_carStation = AddStation(StationType::CAR, "LE86", Spawn(ShopModels::PROJECT_CAR, ShopTextures::PAINT, CAR_HOME, CAR_YAW), false);
	m_stations[m_carStation].yaw = CAR_YAW;
	m_stations[m_carStation].homeYaw = CAR_YAW;
	m_stations[m_carStation].present = false;
	m_stations[m_carStation].object->SetVisible(false);
	m_stations[m_carStation].object->SetPickable(false);
	m_carStands = Spawn(ShopModels::CAR_JACK_STANDS, ShopTextures::PAINT, CAR_HOME, CAR_YAW);
	m_carStands->SetVisible(false);
	m_fuelFlap = Spawn(ShopModels::FUEL_FLAP, ShopTextures::PAINT, CAR_HOME, CAR_YAW);
	m_fuelFlap->SetVisible(false);
	m_carStands->SetPickable(false);
	m_fixedStations = m_stations.size();

	RebuildCollision();
}

void ShopWorld::RebuildCollision()
{
	m_obstacles.clear();
	m_surfaces.clear();
	for (size_t i = 0; i < m_stations.size(); i++)
	{
		const auto& station = m_stations[i];
		// A machine being carried around doesn't block anything until it's set down.
		if (!station.present || (m_move && m_move->stationIndex == i))
			continue;

		for (const auto& surface : station.localSurfaces)
			m_surfaces.push_back(TransformBox(surface, station.position, station.Rotation()));

		// The door's footprint depends on how far it's rolled up, so it's added below.
		if (station.type != StationType::TOOL_BOARD && station.type != StationType::DOOR)
		{
			auto bounds = WorldBounds(station.object);
			// Only as high as it stands (a mat can be walked over), and the lift only at its posts: you
			// walk in between them.
			if (station.type == StationType::LIFT)
				for (float side : { -1.f, 1.f })
				{
					glm::vec3 post = station.position + station.Rotation() * glm::vec3(side * 1.5f, 0.f, 0.f);
					m_obstacles.push_back({ { post.x - 0.25f, post.z - 0.25f }, { post.x + 0.25f, post.z + 0.25f }, bounds.min.y, bounds.max.y });
				}
			else
				m_obstacles.push_back({ { bounds.min.x, bounds.min.z }, { bounds.max.x, bounds.max.z }, bounds.min.y, bounds.max.y });
		}
	}

	m_obstacles.insert(m_obstacles.end(), m_staticObstacles.begin(), m_staticObstacles.end());
	m_obstacles.insert(m_obstacles.end(), m_neighbourObstacles.begin(), m_neighbourObstacles.end());
	m_surfaces.insert(m_surfaces.end(), m_upgradeSurfaces.begin(), m_upgradeSurfaces.end());
	if (m_doorAmount < 0.85f)
		m_obstacles.push_back({ { GarageLayout::DOOR_MIN_X, GarageLayout::HALF_DEPTH }, { GarageLayout::DOOR_MAX_X, GarageLayout::HALF_DEPTH + GarageLayout::WALL } });
	if (m_bayBuilt && m_bayDoorAmount < 0.85f)
		m_obstacles.push_back({ { GarageLayout::BAY_DOOR_MIN_X, GarageLayout::BAY_HALF_DEPTH }, { GarageLayout::BAY_DOOR_MAX_X, GarageLayout::BAY_HALF_DEPTH + GarageLayout::WALL } });
}

glm::vec3 ShopWorld::GetSpawnPosition() const
{
	return SPAWN_POSITION;
}

float ShopWorld::GetSpawnYaw() const
{
	return 0.f;
}

const std::vector<FloorObstacle>& ShopWorld::GetObstacles() const
{
	return m_obstacles;
}

FloorObstacle ShopWorld::GetRoomBounds() const
{
	using namespace GarageLayout;
	return { { -WORLD_HALF_WIDTH, WORLD_BACK_Z }, { WORLD_HALF_WIDTH, WORLD_FAR_Z } };
}

void ShopWorld::BuildViewModel()
{
	// They move with the camera every frame, so none of them casts a shadow (the lamps would draw
	// theirs again every frame), and none can be aimed at. The arm is drawn in front of the world, so
	// it never goes into a wall or a bench; the body stays in the world, under you.
	auto attached = [&](const std::string& model) {
		auto object = Spawn(model, "", {});
		object->SetPickable(false);
		object->SetCastsShadows(false);
		return object;
	};
	m_viewBody = attached(ShopModels::VIEW_BODY);
	m_viewBodySkin = attached(ShopModels::VIEW_BODY_SKIN);
	m_viewSleeve = attached(ShopModels::VIEW_SLEEVE);
	m_viewHand = attached(ShopModels::VIEW_HAND);
	m_viewSleeve->SetInFront(true);
	m_viewHand->SetInFront(true);
	SetPlayerLook(m_look);
}

void ShopWorld::SetPlayerLook(const MonkeyLook& look)
{
	// The fur and the bare skin in first person; all of it, and the cap, in third.
	m_look = look;
	if (m_viewBody)
	{
		m_viewBody->SetTint(MonkeyLook::Color(look.fur));
		m_viewSleeve->SetTint(MonkeyLook::Color(look.fur));
		m_viewBodySkin->SetTint(MonkeyLook::Color(look.skin));
		m_viewHand->SetTint(MonkeyLook::Color(look.skin));
	}
	m_selfAvatar.SetLook(look);
}

int ShopWorld::ArmsOut() const
{
	if (m_carriedPackage != 0)
		return 2;
	const ItemVisual* held = m_equipped.IsNone() ? nullptr : Find(m_equipped);
	return held && held->place == ItemPlace::HAND ? 1 : 0;
}

void ShopWorld::UpdateSelfAvatar(float deltaTime, const FirstPersonController& player)
{
	bool show = m_viewModelVisible && m_thirdPerson;
	if (show && !m_selfAvatar.IsSpawned())
		m_selfAvatar.Spawn(*this, player.GetFeetPosition(), player.GetYaw(), m_look);
	m_selfAvatar.SetVisible(show);
	if (show)
		m_selfAvatar.Pose(deltaTime, player.GetFeetPosition(), player.GetYaw(), player.GetPitch(), player.IsCrouching(), ArmsOut());
}

void ShopWorld::CycleView()
{
	if (!m_thirdPerson)
		m_thirdPerson = true;
	else if (!m_lookingBack)
		m_lookingBack = true;
	else
		SetThirdPerson(false);
}

ShopWorld::CameraView ShopWorld::UpdateThirdPersonCamera(const FirstPersonController& player)
{
	constexpr float BACK = 1.9f;
	constexpr float RIGHT = 0.45f;
	constexpr float UP = 0.2f;
	constexpr float CLEARANCE = 0.2f;	// kept off whatever it's pulled in by
	// Swung about the monkey's own eyes, which are well below the player's (it's small, with a big head):
	// where they are on it as it was last posed, from where the player stands now so it doesn't lag.
	glm::vec3 eye = player.GetEyePosition();
	if (m_selfAvatar.IsSpawned() && m_selfAvatar.GetEye().y > m_selfAvatar.GetFeet().y)
		eye = player.GetFeetPosition() + (m_selfAvatar.GetEye() - m_selfAvatar.GetFeet());
	glm::quat look = player.GetRotation();
	glm::vec3 forward = look * glm::vec3(0.f, 0.f, -1.f);
	// Behind the right shoulder, or (looking back) out in front of the face.
	glm::vec3 offset = m_lookingBack ? forward * BACK + glm::vec3(0.f, UP, 0.f)
		: -forward * BACK + look * glm::vec3(RIGHT, 0.f, 0.f) + glm::vec3(0.f, UP, 0.f);
	float distance = glm::length(offset);
	glm::vec3 direction = offset / distance;
	if (auto wall = WallHit(eye, direction); wall && *wall < distance)
		distance = *wall;
	auto hit = ObjectManager::Get()->Raycast(eye, direction, distance, [&](const pObject& object) {
		auto target = m_targets.find(object.get());
		return target != m_targets.end() && target->second.kind == TargetKind::STATION;
	});
	if (hit)
		distance = hit->distance;
	glm::vec3 position = eye + direction * (std::max)(distance - CLEARANCE, 0.1f);
	// Aimed at the point where the player looks, as far off as they can reach, so the crosshair is on
	// what they'd use.
	// Looking back, it's on the face.
	glm::vec3 aim = m_lookingBack ? eye - forward * 0.3f : player.GetEyePosition() + forward * INTERACT_DISTANCE;
	m_thirdPersonView = { position, glm::quatLookAt(glm::normalize(aim - position), glm::vec3(0.f, 1.f, 0.f)) };
	return m_thirdPersonView;
}

void ShopWorld::UpdateViewModel(const FirstPersonController& player)
{
	bool firstPerson = m_viewModelVisible && !m_thirdPerson;
	bool showBody = firstPerson && m_viewSettings.showBody;
	bool showArm = firstPerson && m_viewSettings.showArm;
	m_viewBody->SetVisible(showBody);
	m_viewBodySkin->SetVisible(showBody);
	m_viewSleeve->SetVisible(showArm);
	m_viewHand->SetVisible(showArm);

	// What's in hand (and a tool's sockets) is drawn in front with the arm; whatever was in hand
	// before goes back into the world. In third person it's in the body's hand, in the world.
	const ItemVisual* held = m_equipped.IsNone() ? nullptr : Find(m_equipped);
	for (const auto& object : m_heldInFront)
		object->SetInFront(false);
	m_heldInFront.clear();
	if (held && held->place == ItemPlace::HAND && !m_thirdPerson)
	{
		m_heldInFront.push_back(held->object);
		m_heldInFront.insert(m_heldInFront.end(), held->sockets.begin(), held->sockets.end());
		// And what's fitted to it (a pin, to the piston on the rod in hand).
		std::unordered_set<uint32_t> carried;
		if (held->ref.IsPart())
			carried.insert(held->ref.id);
		for (int pass = 0; pass < 2; pass++)
			for (const auto& [ref, visual] : m_items)
				if (visual.place == ItemPlace::ATTACHED && carried.contains(visual.hostPart) && carried.insert(ref.id).second)
					m_heldInFront.push_back(visual.object);
		auto battery = held->ref.IsTool() ? BatteryOn(held->ref.id) : std::nullopt;
		if (const ItemVisual* pack = battery ? Find(ItemRef::Tool(static_cast<uint32_t>(*battery))) : nullptr)
			m_heldInFront.push_back(pack->object);
		for (const auto& object : m_heldInFront)
			object->SetInFront(true);
	}

	// The body stands behind the eyes (how far is the player's to choose), so looking down shows a
	// little of the boots and legs below the view rather than filling it, and never the inside of the
	// head. Crouched, it's squashed down to the lower eyes.
	constexpr float STANDING_EYE_HEIGHT = 1.25f;	// FirstPersonController's
	glm::vec3 feet = player.GetFeetPosition();
	glm::vec3 eye = player.GetEyePosition();
	if (showBody)
	{
		glm::quat facing = ShopLayout::Yaw(player.GetYaw());
		m_viewBody->SetPosition(feet + facing * glm::vec3(0.f, 0.f, m_viewSettings.bodyBack));
		m_viewBody->SetRotation(facing);
		m_viewBody->SetScale({ 1.f, std::clamp((eye.y - feet.y) / STANDING_EYE_HEIGHT, 0.5f, 1.f), 1.f });
		m_viewBodySkin->SetPosition(m_viewBody->GetPosition());
		m_viewBodySkin->SetRotation(facing);
		m_viewBodySkin->SetScale(m_viewBody->GetScale());
	}
	if (!showArm)
		return;

	// The right arm: in from the bottom right corner of the view to the wrist, the hand low and right of
	// the middle. It stays there whatever it holds: what's taken in hand comes to it (HoldPoseOf).
	glm::vec3 up = player.GetRotation() * glm::vec3(0.f, 1.f, 0.f);
	glm::vec3 shoulder = ViewShoulder(player);
	glm::vec3 wrist = ViewRestWrist(player);
	glm::vec3 reach = wrist - shoulder;
	float length = (std::max)(glm::length(reach), 0.05f);
	glm::quat along = glm::quatLookAt(reach / length, up);
	// The sleeve carries on back past the corner of the view to the shoulder behind it, so its end
	// is never seen hanging in the air.
	constexpr float BACK_TO_SHOULDER = 0.5f;
	m_viewSleeve->SetPosition(shoulder - reach / length * BACK_TO_SHOULDER);
	m_viewSleeve->SetRotation(along);
	m_viewSleeve->SetScale({ 1.f, 1.f, length + BACK_TO_SHOULDER });
	m_viewHand->SetPosition(wrist);
	m_viewHand->SetRotation(along);
}

bool ShopWorld::IsPackaged(uint32_t partId) const
{
	auto visual = Find(ItemRef::Part(partId));
	return visual && visual->place == ItemPlace::PACKAGED;
}

int ShopWorld::GetUnopenedPackages() const
{
	return static_cast<int>(m_packages.size());
}

void ShopWorld::SetEngineRpm(float rpm, int engine)
{
	m_engineRpm = rpm;
	m_dynoEngine = engine;
}

void ShopWorld::SetUnits(Units::System units)
{
	m_units = units;
}

void ShopWorld::SetFieldOfView(float degrees)
{
	m_fieldOfView = degrees;
}

PlayerInventory& ShopWorld::GetInventory()
{
	return m_inventory;
}

ItemRef ShopWorld::GetEquipped() const
{
	return m_equipped;
}

ItemInfo ShopWorld::DescribeItem(const ItemRef& item, const Workshop& workshop) const
{
	(void)workshop;
	ItemInfo info;
	if (item.IsTool())
	{
		const auto& tool = ToolCatalog::Of(item.id);
		info.name = tool.name;
		info.description = tool.description;
		info.isTool = true;
		info.toolIndex = static_cast<int>(item.id);
		info.size = ToolGridSize(ToolShapeFor(tool));
	}
	else if (item.IsPackage())
	{
		auto it = m_packages.find(item.id);
		info.name = it != m_packages.end() ? "Delivery: " + it->second.label : "Delivery";
		info.description = "Carried in both arms. Set it down and open it to unpack it. Too big for the backpack.";
		info.toolIndex = ItemIcons::PACKAGE;
		info.size = { PlayerInventory::WIDTH + 1, PlayerInventory::HEIGHT + 1 };
	}
	else if (auto visual = Find(item); visual && visual->part)
	{
		const auto& part = *visual->part;
		info.name = part.GetName();
		info.description = PartIcons::Description(part.GetType());
		if (part.GetCondition() == PartCondition::DAMAGED)
			info.description = std::format("Damaged: {}. {}", part.GetDamageReason(), info.description);
		info.partType = part.GetType();
		info.size = ShopAssets::PartGridSize(part.GetType());
		info.part = &part;
	}
	return info;
}

glm::ivec2 ShopWorld::GridSize(const ItemVisual& visual) const
{
	if (visual.ref.IsTool())
		return ToolGridSize(ToolShapeFor(ToolCatalog::Of(visual.ref.id)));
	return ShopAssets::PartGridSize(visual.part->GetType());
}

ItemVisual* ShopWorld::Find(const ItemRef& ref)
{
	auto it = m_items.find(ref);
	return it == m_items.end() ? nullptr : &it->second;
}

const ItemVisual* ShopWorld::Find(const ItemRef& ref) const
{
	auto it = m_items.find(ref);
	return it == m_items.end() ? nullptr : &it->second;
}

ItemVisual* ShopWorld::GetHeld()
{
	return m_equipped.IsNone() ? nullptr : Find(m_equipped);
}

const ItemVisual* ShopWorld::GetHeld() const
{
	return m_equipped.IsNone() ? nullptr : Find(m_equipped);
}

ItemVisual* ShopWorld::FindItemAt(ItemPlace place, int machineIndex)
{
	for (auto& [ref, visual] : m_items)
		if (visual.place == place && (place != ItemPlace::MACHINE || visual.machineIndex == machineIndex))
			return &visual;
	return nullptr;
}

void ShopWorld::Toast(const std::string& message)
{
	m_toast = message;
	m_toastTime = TOAST_SECONDS;
}

void ShopWorld::Notify(const std::string& message)
{
	Toast(message);
}

std::optional<int> ShopWorld::TakeReadRequest()
{
	return std::exchange(m_readRequested, std::nullopt);
}

std::optional<UIRequest> ShopWorld::Update(float deltaTime, Workshop& workshop, const FirstPersonController& player,
	const InputManager& input, bool playerHasControl)
{
	deltaTime = (std::min)(deltaTime, 0.05f);
	m_time += deltaTime;

	UpdateDoor(deltaTime);
	UpdateLift(deltaTime);
	AnimateCarPanels(workshop, deltaTime);
	{
		PROFILE_SCOPE("world: sync");
		// Only when the workshop's parts or tools have changed: building the lookups every frame cost more
		// than the rest of the sync put together.
		uint64_t stock = 1469598103934665603ull;
		for (const auto& part : workshop.GetInventory())
			stock = (stock ^ part->GetId()) * 1099511628211ull;
		for (size_t tool = 0; tool < ToolCatalog::All().size(); tool++)
			stock = (stock ^ (tool + 1) * static_cast<uint64_t>(workshop.ToolCount(tool) + 1)) * 1099511628211ull;
		if (stock != m_syncedStock)
		{
			SyncItems(workshop);
			m_syncedStock = stock;
		}
		SyncStands(workshop);
		SyncEngineMoves(workshop);
		m_swing.progress = (std::min)(1.f, m_swing.progress + deltaTime / HOIST_SWING_SECONDS);
		SyncUpgrades(workshop);
		SyncFurniture(workshop);
		SyncMachines(workshop);
	}
	{
		PROFILE_SCOPE("world: collision+stations");
		RebuildCollision();
		UpdateStations(workshop);
		UpdateEngineMotion(deltaTime, workshop, input, playerHasControl);
	}

	// Resting things only look again at what's under them when something has moved (or equipment
	// is being carried about); otherwise every part would test every other part every frame.
	// Picking something up or setting it down counts; carrying it about doesn't (what rides on it
	// is carried with it, not left to settle).
	if (m_move.has_value() != m_wasMoving)
		m_supportChanged = true;
	m_wasMoving = m_move.has_value();
	m_recheckSupport = m_supportChanged;
	m_supportChanged = false;
	{
		PROFILE_SCOPE("world: physics");
		m_steppingBodies = true;
		m_supportBoxesReady = false;
		for (auto& [ref, visual] : m_items)
			if (visual.place == ItemPlace::WORLD && !IsRiding(ref, 0))
			{
				// Toppling over onto its flat side, then settled again from there.
				if (auto tip = m_tips.find(ref); tip != m_tips.end())
				{
					if (StepTip(visual, tip->second, deltaTime))
						m_tips.erase(tip);
					continue;
				}
				bool falling = !visual.body.resting;
				StepBody(visual.object, visual.body, deltaTime);
				// Landed tipped up on an edge or corner: it starts to topple.
				if (falling && visual.body.resting)
					LevelOut(visual);
			}
		for (auto& [id, package] : m_packages)
			if (!IsCarried(id) && !m_remoteCarriedPackages.contains(id) && !IsRiding({}, id))
				StepBody(package.object, package.body, deltaTime);
		m_steppingBodies = false;
	}
	PROFILE_SCOPE("world: hover+interact");
	{
		PROFILE_SCOPE("world: layout");
		UpdateSelfAvatar(deltaTime, player);
		LayoutItems(workshop, player);
		PoseCarriedPackages(player);
		m_paint.Update(deltaTime);
		UpdateViewModel(player);
	}

	// Anything the simulation logs (a cut, a purchase, a refusal) shows up briefly on screen.
	const auto& log = workshop.GetLog();
	if (log.size() > m_seenLogSize)
	{
		Toast(log.back());
		m_seenLogSize = log.size();
	}
	m_toastTime = (std::max)(0.f, m_toastTime - deltaTime);
	m_readout.time = (std::max)(0.f, m_readout.time - deltaTime);
	UpdatePlastigauge(workshop);

	// Packs on chargers charge whatever the player is doing.
	ChargeBatteries(deltaTime);
	m_hover.reset();
	m_measureTarget.reset();
	m_aimedBore.reset();
	m_aimedHeadSpot.reset();
	// The Valve Spring Compressor squeezes a spring only while it's clamped over that valve: moved off it any other way (put
	// away, sold, the game reloaded), the spring comes back up; and with no spring held, it's not left sitting on the head.
	if (ItemVisual* compressor = ValveCompressorVisual())
	{
		const auto& clamp = workshop.GetValveCompressor();
		bool onHead = compressor->place == ItemPlace::ATTACHED;
		if (clamp && (!onHead || compressor->hostPart != clamp->head))
			workshop.ReleaseValveSpring();
		else if (!clamp && onHead)
			SetPlace(*compressor, ItemPlace::STORAGE);
	}
	else if (workshop.GetValveCompressor())
		workshop.ReleaseValveSpring();
	m_aimedHeadPart.reset();
	m_measurePoints.clear();
	m_fitPoints.clear();
	m_fitTarget.reset();
	m_carFitTarget.reset();
	m_boreFitTarget.reset();
	m_holePoints.clear();
	m_aimedHole.reset();
	m_spannerFastener.reset();
	m_spannerPoints.clear();
	m_spannerTarget.reset();
	m_prompt.clear();
	m_secondaryPrompt.clear();
	m_heldHint.clear();
	m_paintCursor.reset();
	if (!playerHasControl)
		return std::nullopt;

	if (m_move)
	{
		UpdateMove(player, input);
		return std::nullopt;
	}

	// Hotbar: 1-9 and 0 pick a slot, and the wheel steps through them (down to the right, up to the
	// left); whatever is in the picked slot is in hand. The number of the slot already picked puts
	// it away, leaving your hand empty and no slot picked; the next press, or the wheel, brings that
	// same slot back.
	for (int slot = 0; slot < PlayerInventory::HOTBAR_SLOTS; slot++)
		if (input.WasKeyPressed(slot == 9 ? GLFW_KEY_0 : GLFW_KEY_1 + slot))
		{
			if (slot == m_inventory.GetSelected())
			{
				m_handEmpty = !m_handEmpty;
				SyncHand();
			}
			else
				SelectHotbarSlot(slot);
		}
	// Paint in hand: the wheel sizes it instead (see UpdatePaint); a torque wrench, it sets it (UpdateSpanner).
	bool sizingPaint = GetHeld() && GetHeld()->ref.IsTool() && (ToolCatalog::Of(GetHeld()->ref.id).Paints()
		|| ToolCatalog::Of(GetHeld()->ref.id).handKind == HandToolKind::TORQUE_WRENCH);
	if (float scroll = input.GetScrollDelta(); std::abs(scroll) >= 0.5f && !sizingPaint)
		SelectHotbarSlot(m_handEmpty ? m_inventory.GetSelected() : m_inventory.GetSelected() + (scroll > 0.f ? -1 : 1));

	// A delivery in hand fills both arms: it's set down or dropped (or another slot picked) before
	// anything else is done.
	if (m_carriedPackage)
	{
		UpdateCarriedPackage(player, input);
		return std::nullopt;
	}

	{
		PROFILE_SCOPE("world: hover");
		UpdateHover(workshop, player);
	}
	{
		PROFILE_SCOPE("world: empty holes");
		MarkEmptyHoles(workshop, player);
	}

	if (Controls::Pressed(input, Control::MOVE) && m_hover && m_hover->kind == TargetKind::STATION && m_stations[m_hover->stationIndex].movable)
	{
		StartMove(m_hover->stationIndex);
		return std::nullopt;
	}
	if (Controls::Pressed(input, Control::MOVE) && m_hover && m_hover->kind == TargetKind::PACKAGE)
	{
		OpenPackage(m_packages.at(m_hover->packageId));
		return std::nullopt;
	}

	auto held = GetHeld();

	// A measuring tool in hand: left click takes a reading of whatever it's aimed at.
	if (held && held->ref.IsTool())
	{
		int toolIndex = static_cast<int>(held->ref.id);
		m_measureTarget = FindMeasureTarget(workshop, player, toolIndex, m_measurePoints);
		// The service manual: left click opens it.
		if (ToolCatalog::Of(toolIndex).manual && Controls::Pressed(input, Control::PRIMARY))
			m_readRequested = ToolCatalog::Of(toolIndex).manualBook;
		if (m_measureTarget)
		{
			m_secondaryPrompt = m_prompt;
			m_prompt = m_measureTarget->strip != StripStep::NONE ? std::format("[LMB] {}", m_measureTarget->label)
				: std::format("[LMB] Measure {}", m_measureTarget->label);
			if (Controls::Pressed(input, Control::PRIMARY))
				Measure(workshop, *m_measureTarget, toolIndex);
		}
		else if (!m_measureBlocked.empty())
		{
			m_secondaryPrompt = m_prompt;
			m_prompt = m_measureBlocked;
		}
		else if (m_prompt.empty() && !ToolCatalog::Of(toolIndex).IsHandTool() && !ToolCatalog::Of(toolIndex).manual)
			m_prompt = std::format("Aim the {} at something it can measure", ToolCatalog::Of(toolIndex).name);
	}

	// A spanner in hand: a socket goes on from a set, and nuts and bolts are done up or undone.
	if (UpdateBatteries(workshop, input, held) || UpdateSetMembers(input, held))
		return std::nullopt;
	if (!held || !held->ref.IsTool())
		m_turning.reset();
	if (held && held->ref.IsTool() && UpdatePaint(player, input, held->ref.id, deltaTime))
		return std::nullopt;
	if (held && held->ref.IsTool() && UpdateSpanner(workshop, player, input, held->ref.id, deltaTime))
		return std::nullopt;
	if (held && held->ref.IsTool() && UpdatePinPress(workshop, player, input, held->ref.id, deltaTime))
		return std::nullopt;
	if (held && held->ref.IsTool() && UpdateCarTool(workshop, player, input, held->ref.id, deltaTime))
		return std::nullopt;
	if (!held || !held->ref.IsTool())
		m_pressAimed = false;

	// What can be done with the item in hand, shown beside its name above the hotbar. Where the
	// prompt already offers right-click (setting it down on the bench), it isn't repeated.
	if (held)
	{
		bool promptHasRightClick = m_prompt.find("[RMB]") != std::string::npos;
		std::string spanner = held->ref.IsTool() ? SpannerLabel(held->ref.id) : "";
		if (held->ref.IsTool() && ToolCatalog::Of(held->ref.id).manual)
			spanner = "[LMB] Read";
		m_heldHint = std::format("{}{}[G] Drop   [Q] Hold to turn ([Shift] snaps)", spanner.empty() ? "" : spanner + "   ", promptHasRightClick ? "" : "[RMB] Set down   ");
	}

	TurnHeld(input, player);

	if (held)
	{
		if (Controls::Pressed(input, Control::DROP))
		{
			ApplyHeldTurn(*held, player);
			glm::vec3 toss = player.GetForward() * 1.8f + glm::vec3(0.f, 0.8f, 0.f);
			PlaceInWorld(*held, held->object->GetPosition(), toss);
			Cue(Sound::PICK_UP, held->object->GetPosition(), 0.6f);
		}
		else if (Controls::Pressed(input, Control::SET_DOWN))
		{
			// A tool aimed at the tool board hangs right where it's looked at.
			std::optional<glm::vec2> boardSpot = held->ref.IsTool()
				? AimedBoardSpot(player) : std::nullopt;
			if (boardSpot)
				HangToolAt(*held, *boardSpot);
			else if (auto point = AimPoint(player))
			{
				// Set it just above the spot so it settles onto whatever is there, the way it rests (a spray can stood up).
				auto model = ModelOf(held->object);
				ApplyHeldTurn(*held, player);
				ShopBox resting = TransformBox({ model->GetBoundsMin(), model->GetBoundsMax() }, {}, RestingRotation(*held));
				glm::vec3 center = (resting.min + resting.max) * 0.5f;
				glm::vec3 position = *point - glm::vec3(center.x, resting.min.y - 0.03f, center.z);
				PlaceInWorld(*held, position, {});
				Cue(Sound::SET_DOWN, position);
			}
			else
				Toast("Nowhere to put it there.");
		}
	}

	// R takes the bearing shell out of the rod or cap aimed at, or the ring or piston off it (never off what's in hand: an
	// assembled rod and piston stays together while carried). A ring comes out of a loose block's bore with E, see RingInAimedBore.
	if (!m_move && Controls::Pressed(input, Control::ROTATE))
	{
		ItemVisual* host = m_hover && m_hover->kind == TargetKind::ITEM ? Find(m_hover->item) : nullptr;
		auto hasFitted = [&](const ItemVisual* visual) { return visual && visual->part && visual->place != ItemPlace::STAND && visual->part->GetType() != PartType::BLOCK && (TakeOffWhat(workshop, *visual->part) || workshop.PistonOn(*visual->part)); };
		if (hasFitted(host))
			TakeOff(workshop, *host);
	}

	// The left button on one of the car's doors, its hood, trunk lid or fuel flap opens or shuts it.
	if (!m_move && UpdateCarPanels(workshop, player, input))
		return std::nullopt;

	// A click works as E with empty hands or a part in hand, and with the Valve Spring Compressor aimed at a valve (it clamps on).
	bool clickInteracts = !held || !held->ref.IsTool() || (m_hover && m_hover->kind == TargetKind::ITEM && CanCompressAt(*Find(m_hover->item)));
	if (Controls::Pressed(input, Control::USE) || (clickInteracts && Controls::Pressed(input, Control::PRIMARY)))
		return Interact(workshop);
	return std::nullopt;
}

ItemVisual& ShopWorld::CreateItem(const ItemRef& ref, const pPart& part, uint32_t modelIndex, uint32_t textureIndex)
{
	auto objectManager = ObjectManager::Get();
	Object info({}, glm::quat(1.f, 0.f, 0.f, 0.f), { 1.f, 1.f, 1.f }, modelIndex, textureIndex);
	auto object = objectManager->CreateObject(info);
	object->SetPickable(true);
	m_targets[object.get()] = { TargetKind::ITEM, ref, 0, 0 };

	ItemVisual visual;
	visual.ref = ref;
	visual.part = part;
	visual.object = object;
	if (part)
		for (const auto& overlay : ShopAssets::OverlaysFor(part->GetType()))
		{
			Object overlayInfo({}, glm::quat(1.f, 0.f, 0.f, 0.f), { 1.f, 1.f, 1.f }, m_assets->Model(overlay.model), 0);
			visual.overlays.push_back({ overlay, objectManager->CreateObject(overlayInfo) });
		}
	if (ref.IsTool())
	{
		// A socket set's sockets, or the one a drive tool can carry (shown once one's put on).
		const auto& tool = ToolCatalog::Of(ref.id);
		size_t count = tool.handKind == HandToolKind::SOCKETS ? tool.sizes.size() : IsDriveTool(tool) ? 1 : 0;
		for (size_t i = 0; i < count; i++)
		{
			Object socketInfo({}, glm::quat(1.f, 0.f, 0.f, 0.f), { 1.f, 1.f, 1.f }, m_assets->Model(SOCKET_MODEL), 0);
			auto socket = objectManager->CreateObject(socketInfo);
			socket->SetPickable(false);
			socket->SetVisible(false);
			visual.sockets.push_back(socket);
		}
	}
	return m_items[ref] = visual;
}

void ShopWorld::RemoveItem(const ItemRef& ref)
{
	m_supportChanged = true;
	auto it = m_items.find(ref);
	if (it == m_items.end())
		return;

	auto objectManager = ObjectManager::Get();
	m_targets.erase(it->second.object.get());
	objectManager->RemoveObject(it->second.object);
	for (auto& [overlay, object] : it->second.overlays)
		objectManager->RemoveObject(object);
	for (auto& socket : it->second.sockets)
		objectManager->RemoveObject(socket);

	m_inventory.Remove(ref);
	if (m_equipped == ref)
		m_equipped = {};
	m_items.erase(it);
}

void ShopWorld::SyncItems(Workshop& workshop)
{
	std::unordered_set<uint32_t> owned;
	std::vector<ItemRef> delivered;
	std::vector<ItemRef> starting;

	for (const auto& part : workshop.GetInventory())
	{
		owned.insert(part->GetId());
		auto ref = ItemRef::Part(part->GetId());
		if (m_items.contains(ref))
			continue;

		auto& visual = CreateItem(ref, part, m_assets->PartModel(part->GetType()), m_assets->PartTexture(*part));
		// The core you start with is already on the parts shelf; everything after that gets
		// delivered. (A loaded game puts each part back where it was afterwards.)
		// (Parts that come already fitted to an engine, from the dev menu, go straight onto it.)
		if (m_initialSyncDone && workshop.EngineOf(*part) < 0 && !workshop.IsOnCar(*part))
		{
			visual.place = ItemPlace::PACKAGED;
			delivered.push_back(ref);
		}
		// What's loose at the start goes on the shelves; what's fitted (the car, and the engine in it) is posed by SyncStands.
		else if (!m_initialSyncDone && workshop.EngineOf(*part) < 0 && !workshop.IsOnCar(*part))
			starting.push_back(ref);
	}

	SortForShelves(starting);
	for (const auto& ref : starting)
		PutOnShelf(*Find(ref));

	// Tools bought later come in the post too, then hang on the tool board once put away: each copy of
	// one is an item of its own.
	const auto& tools = ToolCatalog::All();
	for (size_t i = 0; i < tools.size(); i++)
		for (size_t copy = 0; copy < static_cast<size_t>(Workshop::MAX_TOOL_COPIES); copy++)
		{
			auto ref = ItemRef::Tool(static_cast<uint32_t>(ToolCatalog::IdOf(i, copy)));
			if (!workshop.OwnsTool(ref.id) || m_items.contains(ref))
				continue;
			auto model = ToolModelName(tools[i]);
			auto& visual = CreateItem(ref, nullptr, m_assets->Model(model), m_assets->TextureFor(model, ShopTextures::STEEL));
			if (m_initialSyncDone)
			{
				visual.place = ItemPlace::PACKAGED;
				delivered.push_back(ref);
			}
		}
	m_initialSyncDone = true;

	// One order arrives packed sensibly: a box per engine system (all the timing parts together, all
	// the cooling parts together, and so on), and the tools in a box of their own.
	if (!delivered.empty())
	{
		std::map<int, std::vector<ItemRef>> boxes;	// by PartSystem; tools after the last system
		constexpr int TOOLS = static_cast<int>(PartSystem::COUNT);
		for (const auto& ref : delivered)
		{
			const auto* visual = Find(ref);
			int box = visual && visual->part ? static_cast<int>(PartCatalog::Get(visual->part->GetType()).system) : TOOLS;
			boxes[box].push_back(ref);
		}
		for (const auto& [box, items] : boxes)
			CreatePackage(items, {}, box == TOOLS ? "Tools" : PartSystemName(static_cast<PartSystem>(box)));
		if (boxes.size() > 1)
			Toast(std::format("A delivery at the door: {} boxes.", boxes.size()));
	}

	// Parts sold, or gone with a sold engine.
	std::vector<ItemRef> gone;
	for (const auto& [ref, visual] : m_items)
		if (ref.IsPart() && !owned.contains(ref.id))
			gone.push_back(ref);
	for (const auto& ref : gone)
		RemoveItem(ref);

	auto objectManager = ObjectManager::Get();
	for (auto it = m_packages.begin(); it != m_packages.end();)
	{
		auto& items = it->second.items;
		items.erase(std::remove_if(items.begin(), items.end(), [&](const ItemRef& ref) { return ref.IsPart() && !owned.contains(ref.id); }), items.end());
		if (!items.empty() || !it->second.stations.empty())
		{
			++it;
			continue;
		}
		m_targets.erase(it->second.object.get());
		objectManager->RemoveObject(it->second.object);
		it = m_packages.erase(it);
	}

}

void ShopWorld::SyncStands(const Workshop& workshop)
{
	for (int engine = 0; engine < Workshop::MAX_ENGINES; engine++)
	{
		m_unseated[engine] = workshop.UnseatedPistons(engine);
		m_ringsInBore[engine] = workshop.RingsInBore(engine);
	}
	// The engines can also be changed from the UI, so the assemblies decide what's on each stand.
	for (auto& [ref, visual] : m_items)
	{
		if (!visual.part)
			continue;
		int engine = workshop.EngineOf(*visual.part);
		// On the car (an engine in it stays on its stand's numbering, posed in the bay).
		if (workshop.IsOnCar(*visual.part))
		{
			if (visual.place != ItemPlace::CAR)
				SetPlace(visual, ItemPlace::CAR);
		}
		else if (engine >= 0)
		{
			if (visual.place != ItemPlace::STAND || visual.engineIndex != engine)
				SetPlace(visual, ItemPlace::STAND, engine);
		}
		// A spring on its loose strut, a tire on its wheel, go about on it (still boxed, they stay in the box).
		else if (auto carHost = workshop.CarHostOf(*visual.part))
		{
			if (visual.place == ItemPlace::PACKAGED)
				continue;
			if (visual.place != ItemPlace::ATTACHED)
				SetPlace(visual, ItemPlace::ATTACHED);
			visual.hostPart = carHost->GetId();
		}
		else if (auto host = workshop.HostOf(*visual.part))
		{
			// A bearing shell fitted into a loose rod or cap goes about in it.
			if (visual.place != ItemPlace::ATTACHED)
				SetPlace(visual, ItemPlace::ATTACHED);
			visual.hostPart = host->GetId();
		}
		else if (visual.place == ItemPlace::STAND || visual.place == ItemPlace::ATTACHED || visual.place == ItemPlace::CAR)
			SetPlace(visual, ItemPlace::STORAGE);
	}
}

void ShopWorld::SyncMachines(const Workshop& workshop)
{
	for (size_t i = 0; i < m_stations.size(); i++)
	{
		auto& station = m_stations[i];
		// The LE86 is the shop's from the start, parked on the drive.
		if (station.type == StationType::CAR)
		{
			// Up on stands till all four wheels are on.
			bool onWheels = workshop.GetCar() && workshop.GetCar()->assembly.IsSlotComplete(CarSlot::WHEEL)
				&& workshop.GetCar()->assembly.IsSlotComplete(CarSlot::TIRE);
			if (m_carStands)
			{
				m_carStands->SetVisible(workshop.OwnsCar() && !onWheels);
				m_carStands->SetPosition(station.position);
				m_carStands->SetRotation(station.Rotation());
			}
			// The car never moves from its spot on the drive.
			if (station.position != station.homePosition || station.yaw != station.homeYaw)
			{
				station.position = station.homePosition;
				station.yaw = station.homeYaw;
				station.object->SetPosition(station.position);
				station.object->SetRotation(station.Rotation());
				RebuildCollision();
			}
			if (station.present != workshop.OwnsCar())
			{
				station.present = workshop.OwnsCar();
				station.position = station.homePosition;
				station.yaw = station.homeYaw;
				station.object->SetPosition(station.position);
				station.object->SetRotation(station.Rotation());
				station.object->SetVisible(station.present);
				station.object->SetPickable(station.present);
				RebuildCollision();
			}
			continue;
		}
		bool bought = (station.type == StationType::MACHINE && workshop.OwnsMachine(station.machineIndex))
			|| (station.type == StationType::ENGINE_STAND && station.engineIndex < workshop.GetEngineCount())
			|| station.type == StationType::FURNITURE
			|| (station.type == StationType::LIFT && workshop.OwnsUpgrade(UpgradeKind::POST_LIFT))
			|| (station.type == StationType::DYNO && workshop.OwnsDyno())
			|| (station.type == StationType::HOIST && workshop.OwnsHoist());
		if (station.present || !bought || IsCrated(i))
			continue;

		// Bought equipment is delivered in a crate, and set up when the crate is opened.
		if (m_initialSyncDone)
		{
			CreatePackage({}, { i });
			continue;
		}
		station.present = true;
		PlaceNewMachine(station);
		station.object->SetVisible(true);
		station.object->SetPickable(true);
	}
}

bool ShopWorld::IsCrated(size_t stationIndex) const
{
	for (const auto& [id, package] : m_packages)
		if (std::find(package.stations.begin(), package.stations.end(), stationIndex) != package.stations.end())
			return true;
	return false;
}

glm::vec3 ShopWorld::NextDeliverySpot() const
{
	// Deliveries are stacked inside the door: each goes on top of the first pile that isn't yet
	// three high (or the lowest, once they all are), dropped from just above its top so it lands
	// squarely on what's already there.
	constexpr int PILES = 3;
	constexpr int PILE_HEIGHT = 3;
	constexpr float PILE_SPACING = 1.1f;
	glm::vec3 best = DELIVERY_POINT;
	float bestTop = FLT_MAX;
	for (int pile = 0; pile < PILES; pile++)
	{
		glm::vec3 spot = { DELIVERY_POINT.x + pile * PILE_SPACING, 0.f, DELIVERY_POINT.z };
		float top = 0.f;
		int count = 0;
		for (const auto& [id, package] : m_packages)
		{
			auto box = WorldBounds(package.object);
			glm::vec3 center = (box.min + box.max) * 0.5f;
			if (std::abs(center.x - spot.x) < 0.5f && std::abs(center.z - spot.z) < 0.5f)
			{
				top = (std::max)(top, box.max.y);
				count++;
			}
		}
		if (count < PILE_HEIGHT)
		{
			best = spot;
			bestTop = top;
			break;
		}
		if (top < bestTop)
		{
			bestTop = top;
			best = spot;
		}
	}
	return { best.x, (std::max)(bestTop + 0.15f, 0.3f), best.z };
}

void ShopWorld::CreatePackage(const std::vector<ItemRef>& items, const std::vector<size_t>& stations, const std::string& title,
	std::optional<glm::vec3> at)
{
	bool big = !stations.empty();
	std::map<std::string, int> counts;
	for (const auto& ref : items)
	{
		if (const auto* visual = Find(ref); visual && visual->part)
		{
			big |= IsBigPart(visual->part->GetType());
			counts[visual->part->GetName()]++;
		}
		else if (ref.IsTool())
			counts[ToolCatalog::Of(ref.id).name]++;
	}
	for (size_t station : stations)
		counts[m_stations[station].name]++;

	std::string label;
	for (const auto& [name, count] : counts)
	{
		if (!label.empty())
			label += ", ";
		label += count > 1 ? std::format("{}x {}", count, name) : name;
	}
	if (!title.empty())
		label = title + ": " + label;

	// Machines and stands come in a crate as big as they need, everything else in a box or crate.
	glm::vec3 position = at ? *at : NextDeliverySpot();
	auto object = big ? Spawn(ShopModels::PACKAGE_CRATE, ShopTextures::WOOD, position, static_cast<float>(Random::Uniform(-0.1, 0.1)))
		: Spawn(ShopModels::PACKAGE_BOX, ShopTextures::CARDBOARD, position, static_cast<float>(Random::Uniform(-0.25, 0.25)));
	object->SetPickable(true);

	Package package = { m_nextPackageId++, object, items, stations, label, { {}, false } };
	m_targets[object.get()] = { TargetKind::PACKAGE, {}, package.id, 0 };
	m_packages[package.id] = package;
	m_deliveries++;
	Toast(std::format("{}: {}", at ? "Delivered" : "Delivery at the door", label));
}

void ShopWorld::OpenPackage(Package& package, const FirstPersonController* player)
{
	m_supportChanged = true;
	auto bounds = WorldBounds(package.object);
	glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
	Cue(Sound::CRATE_OPEN, center);

	// Copied out first: package refers into m_packages, which is about to lose it. The box goes before
	// anything's laid out, so it isn't in the way (or something to stack on).
	uint32_t packageId = package.id;
	auto items = package.items;
	auto stations = package.stations;
	std::string label = package.label;
	m_targets.erase(package.object.get());
	ObjectManager::Get()->RemoveObject(package.object);
	m_packages.erase(packageId);
	if (player)
	{
		m_inventory.Remove(ItemRef::Package(packageId));
		m_carriedPackage = 0;
	}

	// Opened where it stood, it's laid out on the floor around it; opened in hand, it fills the hotbar, then
	// the backpack, and what doesn't fit goes at your feet.
	glm::vec3 origin = player ? player->GetFeetPosition() : glm::vec3(center.x, bounds.min.y, center.z);
	std::vector<std::pair<glm::vec2, float>> taken;
	int onFloor = 0;
	for (const auto& ref : items)
	{
		auto visual = Find(ref);
		if (!visual)
			continue;
		// What comes in a set comes in it: a wrench goes straight into its tray (see LayOutSetMembers).
		if (ref.IsTool() && ToolCatalog::Of(ref.id).partOf >= 0)
		{
			SetPlace(*visual, ItemPlace::STORAGE);
			continue;
		}
		if (player)
		{
			bool stowed = false;
			if (auto slot = m_inventory.FreeHotbarSlot())
				stowed = m_inventory.PutInHotbar(*slot, ref);
			if (!stowed)
				stowed = m_inventory.AddToBackpack(ref, GridSize(*visual));
			if (stowed)
			{
				SetPlace(*visual, ItemPlace::INVENTORY);
				continue;
			}
		}
		auto model = ModelOf(visual->object);
		auto spot = FloorSpotNear(origin, { model->GetBoundsMin(), model->GetBoundsMax() }, taken);
		glm::vec3 position = spot.value_or(origin);
		position.y += 0.05f - model->GetBoundsMin().y;
		visual->yaw = static_cast<float>(Random::Uniform(-glm::pi<float>(), glm::pi<float>()));
		PlaceInWorld(*visual, position, {});
		onFloor++;
	}
	if (player)
		SyncHand();

	// Equipment is set up where its crate stood if it fits there, otherwise at its usual spot.
	glm::vec3 floorSpot = { center.x, 0.f, center.z };

	for (size_t stationIndex : stations)
	{
		auto& station = m_stations[stationIndex];
		station.present = true;
		station.yaw = 0.f;
		if (StationFits(station, floorSpot, 0.f, player))
			station.position = floorSpot;
		else
			PlaceNewMachine(station);
		station.object->SetVisible(true);
		station.object->SetPickable(true);
	}
	if (!stations.empty())
		RebuildCollision();

	if (!stations.empty())
		Toast(std::format("Set up the {}. Press F on it to move it.", label));
	else if (!player)
		Toast(std::format("Unpacked {}.", label));
	else if (onFloor > 0)
		Toast(std::format("Unpacked {}: no room for {} of it, so it's at your feet.", label, onFloor));
	else
		Toast(std::format("Unpacked {} into your inventory.", label));
}

std::optional<glm::vec3> ShopWorld::FloorSpotNear(const glm::vec3& origin, const ShopBox& localBounds, std::vector<std::pair<glm::vec2, float>>& taken) const
{
	// Spots on a grid around origin, nearest first. Each is as wide as the item turned any way.
	constexpr float SPACING = 0.3f;
	constexpr int REACH = 8;
	static const std::vector<glm::vec2> offsets = [] {
		std::vector<glm::vec2> grid;
		for (int x = -REACH; x <= REACH; x++)
			for (int z = -REACH; z <= REACH; z++)
				grid.push_back(glm::vec2(x, z) * SPACING);
		std::stable_sort(grid.begin(), grid.end(), [](const glm::vec2& a, const glm::vec2& b) { return glm::length(a) < glm::length(b); });
		return grid;
	}();
	float radius = (std::max)({ -localBounds.min.x, localBounds.max.x, -localBounds.min.z, localBounds.max.z, 0.05f });
	float height = localBounds.max.y - localBounds.min.y;

	FloorObstacle lot = GetRoomBounds();
	ShopInterior interior = GetInterior();
	glm::vec3 probe = origin + glm::vec3(0.f, 0.1f, 0.f);
	for (const auto& offset : offsets)
	{
		glm::vec2 spot = glm::vec2(origin.x, origin.z) + offset;
		if (spot.x - radius < lot.min.x || spot.x + radius > lot.max.x || spot.y - radius < lot.min.y || spot.y + radius > lot.max.y)
			continue;
		bool clear = true;
		for (const auto& [other, otherRadius] : taken)
			if (glm::length(spot - other) < radius + otherRadius + 0.02f)
				clear = false;
		if (!clear)
			continue;

		// Out of the walls, and the same side of them: in the shack if origin is, outside if it isn't.
		glm::vec3 point = { spot.x, probe.y, spot.y };
		if (interior.InGarage(point) != interior.InGarage(probe))
			continue;
		ShopBox box = { { spot.x - radius, origin.y + 0.05f, spot.y - radius }, { spot.x + radius, origin.y + 0.05f + height, spot.y + radius } };
		if (InsideWalls(box))
			continue;
		glm::vec3 from = { origin.x, origin.y + 0.05f + (std::min)(height * 0.5f, 0.3f), origin.z };
		glm::vec3 to = { spot.x, from.y, spot.y };
		if (float distance = glm::length(to - from); distance > 1e-3f)
			if (auto hit = WallHit(from, (to - from) / distance); hit && *hit < distance + radius)
				continue;

		// Not under a bench, shelf or stairs, where it would land out of reach.
		if (SupportHeight(box, origin.y + 1.5f, nullptr) > origin.y + 0.3f)
			continue;

		taken.push_back({ spot, radius });
		return glm::vec3(spot.x, origin.y, spot.y);
	}
	return std::nullopt;
}

void ShopWorld::SortForShelves(std::vector<ItemRef>& parts) const
{
	// The big parts first, then kind by kind, grouped by system (each shelf takes its own systems;
	// see ShelfFor).
	const auto& order = PartCatalog::All();
	auto rank = [&](PartType type) { return std::find(order.begin(), order.end(), type) - order.begin(); };
	std::sort(parts.begin(), parts.end(), [&](const ItemRef& a, const ItemRef& b) {
		auto typeA = Find(a)->part->GetType(), typeB = Find(b)->part->GetType();
		if (IsBigPart(typeA) != IsBigPart(typeB))
			return IsBigPart(typeA);
		if (typeA != typeB)
			return rank(typeA) < rank(typeB);
		return a.id < b.id;
	});
}

void ShopWorld::ReorganizeShelves()
{
	// Every part sitting on a shelf comes off and goes back on in order, each on its own shelf.
	std::vector<ItemRef> shelved;
	for (const auto& station : m_stations)
	{
		if (station.type != StationType::SHELF || !station.present)
			continue;
		auto footprint = WorldBounds(station.object);
		for (auto& [ref, visual] : m_items)
		{
			if (!visual.part || visual.place != ItemPlace::WORLD)
				continue;
			auto box = WorldBounds(visual.object);
			glm::vec3 middle = (box.min + box.max) * 0.5f;
			if (box.min.y > 0.1f && middle.x > footprint.min.x && middle.x < footprint.max.x && middle.z > footprint.min.z && middle.z < footprint.max.z)
				shelved.push_back(ref);
		}
	}
	// Out of the way first, so they don't count as taking up room while they're put back.
	for (const auto& ref : shelved)
		Find(ref)->place = ItemPlace::PACKAGED;
	SortForShelves(shelved);
	for (const auto& ref : shelved)
		PutOnShelf(*Find(ref));
	m_supportChanged = true;
}

void ShopWorld::LayOutToolBoard()
{
	const auto& tools = ToolCatalog::All();
	if (m_toolHooks.size() == tools.size())
		return;
	static const auto manager = ModelManager::Get();
	// The board is 1.8 m wide and 1.25 m tall about its middle. Tools hang inside a margin, just
	// clear of its face.
	constexpr float LEFT = -0.84f;
	constexpr float RIGHT = 0.84f;
	constexpr float TOP = 0.57f;
	constexpr float BOTTOM = -0.57f;
	constexpr float GAP = 0.035f;
	constexpr float FACE = 0.017f;

	struct Hung
	{
		size_t tool = 0;
		ShopBox box = {};	// in the board's space, before it's moved to its spot
		glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
		float Width() const { return box.max.x - box.min.x; }
		float Height() const { return box.max.y - box.min.y; }
	};
	std::vector<Hung> hung;
	for (size_t i = 0; i < tools.size(); i++)
	{
		// What comes in a set is kept in it, not on the board.
		if (tools[i].partOf >= 0)
			continue;
		const Model* model = manager->PeekModel(m_assets->Model(ToolModelName(tools[i])));
		ShopBox bounds = { model->GetBoundsMin(), model->GetBoundsMax() };
		// Tools are made lying flat. Stood against the board their top faces out, and they're turned
		// so their long side runs up and down.
		glm::quat rotation = AboutX(glm::half_pi<float>());
		if (bounds.max.x - bounds.min.x > bounds.max.z - bounds.min.z)
			rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f)) * rotation;
		hung.push_back({ i, TransformBox(bounds, glm::vec3(0.f), rotation), rotation });
	}

	m_toolHooks.assign(tools.size(), {});
	auto place = [&](const Hung& tool, float left, float top)
	{
		m_toolHooks[tool.tool] = { { left - tool.box.min.x, top - tool.box.max.y, FACE - tool.box.min.z }, tool.rotation,
			(glm::vec2(tool.box.min) + glm::vec2(tool.box.max)) * 0.5f, (glm::vec2(tool.box.max) - glm::vec2(tool.box.min)) * 0.5f, FACE - tool.box.min.z };
	};
	// Every tool's size, for hanging it by hand where it's aimed (the manual and those placed below get theirs again).
	for (const auto& tool : hung)
		place(tool, 0.f, 0.f);

	// The manual has the bottom right corner to itself, so it's always in the same place.
	float manualLeft = RIGHT;
	float manualTop = BOTTOM - 1.f;
	auto manual = std::find_if(hung.begin(), hung.end(), [&](const Hung& tool) { return tools[tool.tool].manual; });
	if (manual != hung.end())
	{
		manualLeft = RIGHT - manual->Width();
		manualTop = BOTTOM + manual->Height();
		place(*manual, manualLeft, manualTop);
		manualLeft -= GAP;
		manualTop += GAP;
		hung.erase(manual);
	}

	// Everything else in rows from the top left, the longest first, so each row's tools are about
	// the same length.
	std::stable_sort(hung.begin(), hung.end(), [](const Hung& a, const Hung& b) { return a.Height() > b.Height(); });
	float x = LEFT;
	float top = TOP;
	float rowHeight = 0.f;
	for (const auto& tool : hung)
	{
		// A row low enough to reach the manual's corner stops short of it.
		auto rightEdge = [&] { return top - std::max(rowHeight, tool.Height()) < manualTop ? manualLeft : RIGHT; };
		if (x > LEFT && x + tool.Width() > rightEdge())
		{
			top -= rowHeight + GAP;
			x = LEFT;
			rowHeight = 0.f;
		}
		place(tool, x, top);
		x += tool.Width() + GAP;
		rowHeight = std::max(rowHeight, tool.Height());
	}
}

std::optional<glm::vec2> ShopWorld::AimedBoardSpot(const FirstPersonController& player) const
{
	const Station* board = FindStation(StationType::TOOL_BOARD);
	if (!board || !m_hover)
		return std::nullopt;
	// Looking at the board itself, or at a tool hung on it (in front of the board, so the board's face behind it).
	bool onBoard = m_hover->kind == TargetKind::STATION && m_hover->stationIndex < m_stations.size() && &m_stations[m_hover->stationIndex] == board;
	if (m_hover->kind == TargetKind::ITEM)
		if (auto it = m_items.find(m_hover->item); it != m_items.end())
			onBoard = it->second.ref.IsTool() && it->second.place == ItemPlace::STORAGE;
	if (!onBoard)
		return std::nullopt;
	// Where the view meets the board's face (its front, +Z in its space), on the board.
	constexpr float FACE = 0.015f, HALF_WIDTH = 0.9f, HALF_HEIGHT = 0.625f;
	glm::quat toBoard = glm::inverse(board->Rotation());
	glm::vec3 origin = toBoard * (player.GetEyePosition() - board->position), direction = toBoard * player.GetForward();
	if (origin.z <= FACE || direction.z > -1e-4f)
		return std::nullopt;
	glm::vec3 hit = origin + direction * ((FACE - origin.z) / direction.z);
	if (std::abs(hit.x) > HALF_WIDTH || std::abs(hit.y) > HALF_HEIGHT)
		return std::nullopt;
	return glm::vec2(hit);
}

void ShopWorld::HangToolAt(ItemVisual& held, const glm::vec2& spot)
{
	LayOutToolBoard();
	const ToolHook& hook = m_toolHooks[ToolCatalog::IndexOf(held.ref.id)];
	// Kept inside the board's edge, a little in from it.
	constexpr float HALF_WIDTH = 0.88f, HALF_HEIGHT = 0.605f;
	glm::vec2 limit = glm::max(glm::vec2(HALF_WIDTH, HALF_HEIGHT) - hook.half, glm::vec2(0.f));
	m_toolSpots[held.ref.id] = glm::clamp(spot, -limit, limit);
	SetPlace(held, ItemPlace::STORAGE);
	if (const Station* board = FindStation(StationType::TOOL_BOARD))
		Cue(Sound::SET_DOWN, board->position + board->Rotation() * glm::vec3(m_toolSpots[held.ref.id], 0.05f));
}

void ShopWorld::PutOnShelf(ItemVisual& visual)
{
	// In order: the first gap from the left on the lowest board with headroom for it, lengthways
	// along the board and centred on it. It's then a loose part like any other, settled by the
	// physics. (Put away in part order, the shelf fills up sorted.) Tools have no shelf.
	const Station* shelf = visual.part ? ShelfFor(*visual.part) : FindStation(StationType::SHELF);
	auto model = ModelOf(visual.object);
	ShopBox local = { model->GetBoundsMin(), model->GetBoundsMax() };
	glm::vec3 size = local.max - local.min;
	constexpr size_t BOARDS = std::size(SHELF_BOARDS);
	constexpr float GAP = 0.02f;

	// Everything loose that could be in the way, measured once rather than at every spot tried.
	std::vector<ShopBox> others;
	for (const auto& [ref, other] : m_items)
		if (&other != &visual && other.place == ItemPlace::WORLD)
			others.push_back(WorldBounds(other.object));
	std::vector<ShopBox> nearby;
	auto clear = [&](const ShopBox& box) {
		for (const auto& bounds : nearby)
			if (box.min.x < bounds.max.x + GAP && bounds.min.x < box.max.x + GAP && box.min.y < bounds.max.y && bounds.min.y < box.max.y
				&& box.min.z < bounds.max.z + GAP && bounds.min.z < box.max.z + GAP)
				return false;
		return true;
	};

	// Lengthways along the board, so long parts don't hang off the front. In the shelf's own space,
	// how far the part reaches either side of its origin along the board and front to back.
	float yaw = size.x >= size.z ? 0.f : glm::half_pi<float>();
	ShopBox along = TransformBox(local, {}, Yaw(yaw));
	float length = along.max.x - along.min.x;
	float middleZ = (along.min.z + along.max.z) * 0.5f;

	for (size_t board = 0; board < BOARDS; board++)
	{
		float headroom = board + 1 < BOARDS ? SHELF_BOARDS[board + 1] - SHELF_BOARDS[board] - 0.04f : 0.6f;
		if (size.y > headroom || length > SHELF_HALF_WIDTH * 2.f)
			continue;
		// Only what sits at this board's height can be in the way.
		float floorY = shelf->position.y + SHELF_BOARDS[board];
		nearby.clear();
		for (const auto& bounds : others)
			if (bounds.max.y > floorY && bounds.min.y < floorY + size.y + 0.05f)
				nearby.push_back(bounds);
		for (float left = -SHELF_HALF_WIDTH + GAP; left + length <= SHELF_HALF_WIDTH; left += 0.02f)
		{
			glm::vec3 spot = { left - along.min.x, SHELF_BOARDS[board] + 0.01f - local.min.y, -middleZ };
			glm::vec3 position = shelf->position + shelf->Rotation() * spot;
			float worldYaw = shelf->yaw + yaw;
			if (!clear(TransformBox(local, position, Yaw(worldYaw))))
				continue;
			visual.yaw = worldYaw;
			PlaceInWorld(visual, position, {});
			return;
		}
	}

	// No room left: on the floor in front of it.
	glm::vec3 spot = { static_cast<float>(Random::Uniform(-SHELF_HALF_WIDTH + 0.3, SHELF_HALF_WIDTH - 0.3)), 0.f, static_cast<float>(Random::Uniform(0.6, 1.1)) };
	glm::vec3 position = shelf->position + shelf->Rotation() * spot;
	position.y = 0.05f - local.min.y;
	visual.yaw = shelf->yaw + static_cast<float>(Random::Uniform(-glm::pi<float>(), glm::pi<float>()));
	PlaceInWorld(visual, position, {});
}

void ShopWorld::UpdateStations(const Workshop& workshop)
{
	(void)workshop;
	for (size_t i = 0; i < m_stations.size(); i++)
	{
		auto& station = m_stations[i];
		station.object->SetPosition(station.position);
		station.object->SetRotation(station.Rotation());
		if (m_move && m_move->stationIndex == i)
			continue;
		station.object->SetEmissive(0.f);
		station.object->SetTint(glm::vec4(1.f));
	}
	for (auto& [id, package] : m_packages)
	{
		package.object->SetEmissive(0.f);
		package.object->SetTint(glm::vec4(1.f));
	}
}

bool ShopWorld::EngineTurnsFreely(const Workshop& workshop, int engine, std::string& reason) const
{
	const auto& assembly = workshop.GetAssembly(engine);
	const auto& spec = assembly.GetSpec();
	if (!workshop.UnseatedPistons(engine).empty())
	{
		reason = std::format("Piston {} isn't home: its rod isn't on the crank pin yet. The ring compressor pushes it home.", *workshop.UnseatedPistons(engine).begin() + 1);
		return false;
	}
	if (!assembly.Get(AssemblySlot::CRANKSHAFT, 0))
	{
		reason = "There's no crank in it to turn.";
		return false;
	}
	// The crank only turns in its bearings, held down by the caps: not on bare saddles, or loose.
	for (auto [slot, what] : { std::pair{ AssemblySlot::MAIN_BEARING, "upper main bearing" },
		std::pair{ AssemblySlot::MAIN_BEARING_LOWER, "lower main bearing" }, std::pair{ AssemblySlot::MAIN_CAP, "main cap" } })
		for (int journal = 0; journal < assembly.SlotCount(slot); journal++)
			if (!assembly.Get(slot, journal))
			{
				reason = std::format("It won't turn: the {} {} isn't on.", what, journal + 1);
				return false;
			}
	// A bearing with no clearance or a piston bigger than its bore locks the engine solid.
	for (int journal = 0; journal < spec.mainJournals; journal++)
		if (auto clearance = assembly.MainClearance(journal); clearance && *clearance < 0.003)
		{
			reason = std::format("It won't turn: main {} is binding.", journal + 1);
			return false;
		}
	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
	{
		if (auto clearance = assembly.RodClearance(cylinder); clearance && *clearance < 0.003)
		{
			reason = std::format("It won't turn: rod {} is binding.", cylinder + 1);
			return false;
		}
		if (auto clearance = assembly.PistonToWall(cylinder); clearance && *clearance < 0.0)
		{
			reason = std::format("It won't turn: piston {} is tighter than its bore.", cylinder + 1);
			return false;
		}
	}
	return true;
}

void ShopWorld::UpdateEngineMotion(float deltaTime, const Workshop& workshop, const InputManager& input, bool playerHasControl)
{
	// Turning an engine over by hand, or rolling it, works on the one the player is looking at.
	int looked = LookedAtEngine();

	// Z and X roll the engine over on the stand so you can get at it from underneath.
	if (playerHasControl && !m_move && looked >= 0)
	{
		float& roll = m_engineRolls[looked];
		if (Controls::Held(input, Control::ROLL_BACK))
			roll -= STAND_ROLL_SPEED * deltaTime;
		if (Controls::Held(input, Control::ROLL_FORWARD))
			roll += STAND_ROLL_SPEED * deltaTime;
		roll = std::remainder(roll, glm::two_pi<float>());
	}
	if (m_devSpin.second != 0.f)
		m_engineRolls[m_devSpin.first] = std::remainder(m_engineRolls[m_devSpin.first] + m_devSpin.second * deltaTime, glm::two_pi<float>());

	m_handTurnRpm = 0.f;
	if (playerHasControl && !m_move && looked >= 0 && Controls::Held(input, Control::ROTATE))
	{
		std::string reason;
		if (EngineTurnsFreely(workshop, looked, reason))
			m_handTurnRpm = HAND_TURN_RPM;
		else if (Controls::Pressed(input, Control::ROTATE))
			Toast(reason);
	}

	// The engine on the dyno spins with the pull; the one being turned by hand turns slowly.
	for (int engine = 0; engine < Workshop::MAX_ENGINES; engine++)
	{
		float rpm = engine == m_dynoEngine && m_engineRpm > 0.f ? m_engineRpm * VISUAL_RPM_SCALE : engine == looked ? m_handTurnRpm : 0.f;
		TurnCrank(workshop, engine, rpm / 60.f * glm::two_pi<float>() * deltaTime);
	}
}

void ShopWorld::TurnCrank(const Workshop& workshop, int engine, float by)
{
	if (by == 0.f || engine < 0 || engine >= Workshop::MAX_ENGINES)
		return;
	m_crankAngles[engine] = std::fmod(m_crankAngles[engine] + by, 4.f * glm::pi<float>());
	// The cam (and the valves off it) only turns with the crank with the timing belt round them both.
	if (engine < workshop.GetEngineCount() && workshop.GetAssembly(engine).Get(AssemblySlot::TIMING_BELT, 0))
		m_camCranks[engine] = std::fmod(m_camCranks[engine] + by, 4.f * glm::pi<float>());
}

int ShopWorld::LookedAtEngine() const
{
	// Not the one on the dyno or the hoist: it can't be rolled or turned by hand there.
	int engine = LookedAtStandEngine();
	return engine == m_engineOnDyno || engine == m_engineOnHoist ? -1 : engine;
}

int ShopWorld::LookedAtStandEngine() const
{
	if (m_hover && m_hover->kind == TargetKind::STATION)
	{
		const auto& station = m_stations[m_hover->stationIndex];
		return station.type == StationType::ENGINE_STAND ? station.engineIndex : -1;
	}
	if (m_hover && m_hover->kind == TargetKind::ITEM)
		if (auto visual = Find(m_hover->item); visual && visual->place == ItemPlace::STAND)
			return visual->engineIndex;
	return -1;
}

void ShopWorld::PlaceResting(ItemVisual& visual, const glm::vec3& anchor, float yaw) const
{
	// Sit the item's bounding box on the anchor, centred, with its underside touching.
	auto model = ModelOf(visual.object);
	glm::vec3 boundsMin = model->GetBoundsMin(), boundsMax = model->GetBoundsMax();
	glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
	glm::quat rotation = Yaw(yaw);

	visual.object->SetPosition(anchor - rotation * glm::vec3(center.x, boundsMin.y, center.z));
	visual.object->SetRotation(rotation);
	visual.object->SetScale({ 1.f, 1.f, 1.f });
}

void ShopWorld::PlaceInHand(ItemVisual& visual, const FirstPersonController& player) const
{
	// In third person it's in the body's right hand, the way other players see it (NetSession).
	if (m_thirdPerson && m_selfAvatar.IsSpawned())
	{
		auto model = ModelOf(visual.object);
		glm::vec3 center = (model->GetBoundsMin() + model->GetBoundsMax()) * 0.5f;
		glm::quat facing = Yaw(player.GetYaw());
		visual.object->SetPosition(m_selfAvatar.GetHand() + OnAvatarHand(visual, *model) - facing * center);
		visual.object->SetRotation(facing);
		visual.object->SetScale({ 1.f, 1.f, 1.f });
		return;
	}
	auto pose = HoldPoseOf(visual, player);
	visual.object->SetPosition(pose.point - pose.rotation * pose.center);
	visual.object->SetRotation(pose.rotation);
	visual.object->SetScale({ 1.f, 1.f, 1.f });
}


ShopWorld::HoldPose ShopWorld::HoldPoseOf(const ItemVisual& visual, const FirstPersonController& player) const
{
	// First person: held low and to the right of the view, locked to the camera so it turns and
	// tilts with your head, clear of the crosshair, in the hand where it rests. Tools are held in the right hand, turned in a little; parts are carried more
	// squarely.
	auto model = ModelOf(visual.object);
	glm::vec3 boundsMin = model->GetBoundsMin(), boundsMax = model->GetBoundsMax();
	glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
	float radius = glm::length(boundsMax - boundsMin) * 0.5f;
	glm::quat camera = player.GetRotation();
	glm::vec3 forward = camera * glm::vec3(0.f, 0.f, -1.f);
	glm::vec3 right = camera * glm::vec3(1.f, 0.f, 0.f);
	glm::vec3 up = camera * glm::vec3(0.f, 1.f, 0.f);

	bool tool = visual.ref.IsTool();
	auto grip = tool ? GripFor(ToolCatalog::Of(visual.ref.id)) : std::nullopt;
	// Models lie flat the way they rest on a bench; in hand, tools and small parts are stood up on end,
	// their longest side turned upright (the tools that need it the right way up, see GripFor). The
	// big parts (block, head, crank, cam) are carried flat.
	glm::vec3 size = boundsMax - boundsMin;
	float longest = (std::max)({ size.x, size.y, size.z });
	constexpr float CARRIED_FLAT_FROM = 0.3f;
	bool standsUp = tool || longest < CARRIED_FLAT_FROM;
	glm::quat upright = glm::quat(1.f, 0.f, 0.f, 0.f);
	if (grip)
		upright = grip->turn;
	else if (standsUp && size.x >= size.y && size.x >= size.z)
		upright = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));
	else if (standsUp && size.z >= size.y && size.z >= size.x)
		upright = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.f, 0.f, 0.f));
	glm::quat rotation = camera * Yaw(tool ? 0.35f : 0.15f) * glm::angleAxis(tool ? -0.15f : -0.1f, glm::vec3(1.f, 0.f, 0.f)) * upright;
	// Turned with the turn-held key: on from the way it was held (Tilt carries that), shown the way it will be put
	// down, still in the same grip.
	if (m_heldTurn.turned && m_heldTurn.item == visual.ref)
		rotation = Yaw(player.GetYaw()) * m_heldTurn.Tilt();
	// The hand stays where it is, whatever it holds. A tool with a
	// grip is held there, in the fist just past the wrist; any other tool is gripped by its lower
	// end, and a part sits on the palm, carried from underneath (the hand under its near right side).
	glm::vec3 wrist = ViewRestWrist(player);
	glm::vec3 fist = wrist + glm::normalize(wrist - ViewShoulder(player)) * ShopModels::VIEW_HAND_LENGTH * 0.5f;
	// How far the item, turned as it's held, reaches from its middle along a direction.
	glm::vec3 half = (boundsMax - boundsMin) * 0.5f;
	auto reach = [&](const glm::vec3& direction) {
		return std::abs(glm::dot(rotation * glm::vec3(1.f, 0.f, 0.f), direction)) * half.x
			+ std::abs(glm::dot(rotation * glm::vec3(0.f, 1.f, 0.f), direction)) * half.y
			+ std::abs(glm::dot(rotation * glm::vec3(0.f, 0.f, 1.f), direction)) * half.z;
	};
	glm::vec3 holdPoint = {};
	if (grip)
		holdPoint = fist + rotation * (center - grip->point);
	else if (tool)
		holdPoint = fist + up * (std::max)(reach(up) - 0.04f, 0.f);
	else
		holdPoint = fist + up * (ShopModels::VIEW_HAND_THICKNESS * 0.5f + reach(up)) - right * (reach(right) * 0.4f) + forward * (reach(forward) * 0.2f);
	return { holdPoint, rotation, center, radius, tool };
}

glm::vec3 ShopWorld::ViewPoint(const FirstPersonController& player, float right, float down, float depth) const
{
	float halfHeight = depth * std::tan(glm::radians(m_fieldOfView) * 0.5f);
	float halfWidth = halfHeight * 16.f / 9.f;
	return player.GetEyePosition() + player.GetRotation() * glm::vec3(right * halfWidth, -down * halfHeight, -depth);
}

glm::vec3 ShopWorld::ViewRestWrist(const FirstPersonController& player) const
{
	glm::vec3 wrist = ViewPoint(player, 0.55f, 0.6f, 0.68f) + HandOffset(player.GetRotation());
	return wrist;
}

glm::vec3 ShopWorld::HandOffset(const glm::quat& camera) const
{
	// Forward is out from you: the view looks down -Z.
	const glm::vec3& offset = m_viewSettings.handOffset;
	return camera * glm::vec3(offset.x, offset.y, -offset.z);
}

void ShopWorld::PlaceOnStand(ItemVisual& visual, AssemblySlot slot, int index) const
{
	int engine = std::clamp(visual.engineIndex, 0, Workshop::MAX_ENGINES - 1);
	auto pose = PoseOnStand(engine, slot, index);

	// A running engine shakes on its stand.
	if (engine == m_dynoEngine && m_engineRpm > 0.f)
	{
		float amount = 0.0015f * (std::min)(m_engineRpm / 5500.f, 1.f);
		pose.offset += glm::vec3(std::sin(m_time * 91.f), std::sin(m_time * 67.f), std::sin(m_time * 83.f)) * amount;
	}

	// A nut or bolt stands out of its hole while it's loose, and screws down into it as it's done up
	// (and back out as it's undone): along its own axis, turning clockwise (seen from its head) on the
	// way in. Loose, it's a few turns out, as far as half its length will go.
	if (EngineAssembly::Holes(slot) && visual.part)
	{
		constexpr float TURNS = 6.f;
		const auto& bounds = PartBounds(visual.part->GetType());
		float travel = std::clamp((bounds.second.y - bounds.first.y) * 0.5f, 0.006f, 0.03f);
		float rise = FastenerRise(visual, slot, index);
		glm::vec3 axis = { 0.f, 1.f, 0.f };
		pose.offset += pose.rotation * axis * (rise * travel);
		pose.rotation = pose.rotation * glm::angleAxis(rise * TURNS * glm::two_pi<float>(), axis);
	}

	// The engine moves with its stand and rolls on its mount.
	visual.standOffset = pose.offset;
	visual.standTurn = pose.rotation;
	EnginePose engineNow = CurrentPose(engine);
	visual.object->SetPosition(engineNow.position + engineNow.rotation * pose.offset);
	visual.object->SetRotation(engineNow.rotation * pose.rotation);
	visual.object->SetScale(pose.scale);
}

float ShopWorld::FastenerRise(const ItemVisual& visual, AssemblySlot slot, int index) const
{
	if (!visual.part)
		return 0.f;
	float now = visual.part->GetTightness() == FastenerTightness::LOOSE ? 1.f : 0.f;
	// Part way done up (or undone), it sits as far in as it's been turned, being turned now or left there and
	// looked away from.
	if (now > 0.f)
		if (auto turned = m_fastenerTurns.find(visual.part->GetId()); turned != m_fastenerTurns.end())
			return std::clamp(1.f - turned->second, 0.f, 1.f);
	if (m_turning && m_turning->fastener.engine == visual.engineIndex && m_turning->fastener.slot == slot
		&& std::find(m_turning->indices.begin(), m_turning->indices.end(), index) != m_turning->indices.end())
	{
		// From where it is now: down to the bottom done up, all the way out undone (a snug one being
		// torqued stays down).
		float progress = std::clamp(m_turning->progress, 0.f, 1.f);
		return m_turning->tightening ? now * (1.f - progress) : now + (1.f - now) * progress;
	}
	return now;
}

glm::vec3 ShopWorld::SlotWorldCenter(int engine, AssemblySlot slot, int index) const
{
	// A spark plug lead: where its boot goes in, the mouth of its plug's tube in the side of the head (on the plug's axis,
	// where it comes out of the head's side face). Its loose model, the boot straight up and the wire off along X, is
	// nothing like the shape it takes fitted.
	if (slot == AssemblySlot::PLUG_LEAD)
	{
		using namespace EngineLayout;
		constexpr float HEAD_SIDE = 0.105f;
		glm::vec3 tip = PlugTip(index), axis = PlugAxis();
		float out = (HEAD_SIDE - std::abs(tip.z)) / std::abs(axis.z);
		// From its mount, on the plug's terminal, on along the axis.
		auto lead = PoseOnStand(engine, slot, index);
		return StandToWorld(engine, lead.offset + lead.rotation * (axis * (out - PLUG_TERMINAL)));
	}
	// The middle of the part that goes there, where it would sit.
	auto pose = PoseOnStand(engine, slot, index);
	const auto& bounds = PartBounds(EngineAssembly::SlotPartType(slot));
	glm::vec3 center = (bounds.first + bounds.second) * 0.5f * pose.scale;
	return StandToWorld(engine, pose.offset + pose.rotation * center);
}

const std::pair<glm::vec3, glm::vec3>& ShopWorld::PartBounds(PartType type) const
{
	auto bounds = m_partBounds.find(type);
	if (bounds == m_partBounds.end())
	{
		const Model* model = ModelManager::Get()->PeekModel(m_assets->PartModel(type));
		bounds = m_partBounds.emplace(type, std::pair{ model->GetBoundsMin(), model->GetBoundsMax() }).first;
	}
	return bounds->second;
}

ShopWorld::StandPose ShopWorld::PoseOnStand(int engine, AssemblySlot slot, int index) const
{
	using namespace EngineLayout;

	// Nuts and bolts sit on whatever they hold.
	if (EngineAssembly::Holes(slot))
		return FastenerPose(engine, slot, index);

	glm::vec3 offset = {};
	glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
	glm::vec3 scale = { 1.f, 1.f, 1.f };
	engine = std::clamp(engine, 0, Workshop::MAX_ENGINES - 1);
	float crankAngle = m_crankAngles[engine];
	// The cam turns at half the crank's speed with the timing belt on; without it, it stays where it was (TurnCrank).
	float camCrank = m_camCranks[engine];

	// Slider-crank: each throw sits at the crank angle plus its own phase, and each rod leans to
	// reach it from a piston that can only move straight up and down.
	auto pin = [&](int cylinder) {
		float angle = crankAngle + (ThrowSign(cylinder) > 0.f ? 0.f : glm::pi<float>());
		return glm::vec2(CRANK_Y + CRANK_THROW * std::cos(angle), CRANK_THROW * std::sin(angle));
	};
	auto smallEndY = [&](int cylinder) {
		glm::vec2 p = pin(cylinder);
		return p.x + std::sqrt(ROD_LENGTH * ROD_LENGTH - p.y * p.y);
	};
	auto rodLean = [&](int cylinder) {
		return -std::asin(pin(cylinder).y / ROD_LENGTH);
	};

	switch (slot)
	{
		case AssemblySlot::BLOCK:
			break;
		case AssemblySlot::HEAD_GASKET:
			offset = { 0.f, 0.0005f, 0.f };
			break;
		case AssemblySlot::CYLINDER_HEAD:
			offset = { 0.f, HEAD_DECK_Y, 0.f };
			break;
		case AssemblySlot::CRANKSHAFT:
			offset = { 0.f, CRANK_Y, 0.f };
			rotation = AboutX(crankAngle);
			break;
		case AssemblySlot::MAIN_BEARING:
		case AssemblySlot::MAIN_BEARING_LOWER:
		case AssemblySlot::MAIN_CAP:
			offset = { MainX(index), CRANK_Y, 0.f };
			break;
		case AssemblySlot::ROD_BEARING:
		case AssemblySlot::ROD_BEARING_LOWER:
		case AssemblySlot::ROD_CAP:
			offset = { BoreX(index), pin(index).x, pin(index).y };
			rotation = AboutX(rodLean(index));
			break;
		case AssemblySlot::CONNECTING_ROD:
			offset = { BoreX(index), smallEndY(index), 0.f };
			rotation = AboutX(rodLean(index));
			break;
		case AssemblySlot::PISTON:
			offset = { BoreX(index), smallEndY(index) + PISTON_PIN_DEPTH, 0.f };
			break;
		case AssemblySlot::PISTON_RING:
			// On its own in the bore, to be gapped: in the top of it, or pushed down square.
			if (auto ring = m_ringsInBore[engine].find(index); ring != m_ringsInBore[engine].end())
			{
				offset = { BoreX(index), ring->second ? RING_SQUARED_Y : RING_IN_BORE_Y, 0.f };
				break;
			}
			offset = { BoreX(index), smallEndY(index) + PISTON_PIN_DEPTH - 0.006f, 0.f };
			break;
		case AssemblySlot::CAMSHAFT:
			offset = { 0.f, HEAD_DECK_Y + CAM_Y, 0.f };
			rotation = AboutX(camCrank * 0.5f);
			break;
		case AssemblySlot::CAM_CAP:
			offset = { CamTowerX(index), HEAD_DECK_Y + CAM_Y, 0.f };
			break;
		case AssemblySlot::INTAKE_VALVE:
		case AssemblySlot::EXHAUST_VALVE:
		{
			bool exhaust = slot == AssemblySlot::EXHAUST_VALVE;
			float lift = ValveLift(index, exhaust, camCrank);
			offset = { BoreX(index), HEAD_DECK_Y + VALVE_FACE_Y - lift, ValveZ(exhaust) };
			break;
		}
		case AssemblySlot::VALVE_SPRING:
		{
			int cylinder = index / 2;
			bool exhaust = index % 2 == 1;
			float lift = ValveLift(cylinder, exhaust, camCrank);
			offset = { BoreX(cylinder), HEAD_DECK_Y + HEAD_FLOOR_Y, ValveZ(exhaust) };
			scale.y = (SPRING_INSTALLED_HEIGHT - lift) / SPRING_FREE_HEIGHT;
			break;
		}
		default:
		{
			// Bolt-ons sit where the layout says; the timing drive turns and the valvetrain lifts.
			auto mount = AncillaryMount(EngineAssembly::SlotPartType(slot), index);
			offset = mount.offset;
			rotation = mount.rotation;
			if (slot == AssemblySlot::CRANK_SPROCKET || slot == AssemblySlot::CRANK_PULLEY)
				rotation = AboutX(crankAngle);
			else if (slot == AssemblySlot::FLYWHEEL)
				rotation = AboutX(crankAngle) * mount.rotation;
			else if (slot == AssemblySlot::CAM_SPROCKET)
				rotation = AboutX(camCrank * 0.5f);
			else if (slot == AssemblySlot::VALVE_RETAINER || slot == AssemblySlot::ROCKER_ARM)
				offset.y -= ValveLift(index / 2, index % 2 == 1, camCrank);
			break;
		}
	}
	// A rod and piston not yet pushed home sit up in the top of the bore, wherever the crank is: the ring, too big for the
	// bore out of its groove, rests on the deck, and the piston (its rod and shell with it) on the ring, its crown sticking up.
	bool cylinderPart = slot == AssemblySlot::CONNECTING_ROD || slot == AssemblySlot::PISTON || slot == AssemblySlot::PISTON_RING || slot == AssemblySlot::ROD_BEARING;
	if (cylinderPart && m_unseated[engine].contains(index))
	{
		float ringY = smallEndY(index) + PISTON_PIN_DEPTH - 0.006f;
		offset.y += UNSEATED_RING_Y - ringY;
		if (slot == AssemblySlot::PISTON_RING)
			scale = { UNSEATED_RING_SPREAD, 1.f, UNSEATED_RING_SPREAD };
	}
	return { offset, rotation, scale };
}

ShopWorld::StandPose ShopWorld::FastenerPose(int engine, AssemblySlot slot, int index) const
{
	using namespace EngineLayout;
	const auto& holes = *EngineAssembly::Holes(slot);
	// Holes through every parent are placed on the first.
	int parentIndex = holes.throughAll ? 0 : index / holes.perParent;
	int hole = holes.throughAll ? index : index % holes.perParent;
	auto parent = PoseOnStand(engine, EngineAssembly::SlotFor(holes.parent), parentIndex);
	// Turned over: the head under the part, the shank pointing up into it.
	const glm::quat FLIPPED = glm::angleAxis(glm::pi<float>(), glm::vec3(1.f, 0.f, 0.f));
	// Head out the front of the engine (-X), the shank pointing back into it.
	const glm::quat FACING_FRONT = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));

	glm::vec3 local = {};
	glm::quat turn = glm::quat(1.f, 0.f, 0.f, 0.f);
	switch (slot)
	{
		case AssemblySlot::MAIN_CAP_BOLT:
			// Heads up against the underside of the cap's bosses.
			local = { 0.f, -0.02f, hole == 0 ? -0.058f : 0.058f };
			turn = FLIPPED;
			break;
		case AssemblySlot::CAM_CAP_BOLT:
			// Down through the cap's feet, either side of the dome over the cam.
			local = { 0.f, 0.008f, hole == 0 ? -0.021f : 0.021f };
			break;
		case AssemblySlot::ROD_NUT:
			// Up against the underside of the cap's bosses, on the rod bolts poking through them.
			local = { 0.f, -0.008f, hole == 0 ? -0.038f : 0.038f };
			turn = FLIPPED;
			break;
		case AssemblySlot::HEAD_BOLT:
			// Either side of each cam tower.
			local = { std::clamp(MainX(hole / 2), -0.19f, 0.19f), 0.08f, hole % 2 == 0 ? -0.085f : 0.085f };
			break;
		case AssemblySlot::SPARK_PLUG:
			// In from the side of the head down its tube, angled into the chamber beside the valves (EngineLayout::PlugTip).
			local = PlugTip(hole) + PlugAxis() * PLUG_SEAT;
			turn = PlugTurn();
			break;
		case AssemblySlot::DRAIN_PLUG:
			// Up into the bottom of the pan, its head under it on the crush washer.
			local = { 0.12f, -OIL_PAN_DEPTH - 0.002f, 0.f };
			turn = FLIPPED;
			break;
		case AssemblySlot::FLYWHEEL_BOLT:
		{
			// A ring round the hub, into the crank flange.
			float angle = hole * glm::two_pi<float>() / holes.perParent;
			local = { std::cos(angle) * 0.024f, 0.017f, std::sin(angle) * 0.024f };
			break;
		}
		case AssemblySlot::CARBURETTOR_BOLT:
			// The corners of its base flange.
			local = { hole % 2 == 0 ? -0.036f : 0.036f, 0.006f, hole / 2 == 0 ? -0.03f : 0.03f };
			break;
		case AssemblySlot::DISTRIBUTOR_BOLT:
			local = { 0.025f, 0.004f, 0.f };
			break;
		// The timing end: the bolt in the middle of the cam sprocket's face, and the crank pulley's
		// in the nose of its hub (inside the belt groove's rim), heads out the front.
		case AssemblySlot::CAM_SPROCKET_BOLT:
			local = { -0.004f, 0.f, 0.f };
			turn = FACING_FRONT;
			break;
		case AssemblySlot::CRANK_PULLEY_BOLT:
			local = { -0.027f, 0.f, 0.f };
			turn = FACING_FRONT;
			break;
		// The serpentine belt tensioner's, through the middle of its pulley.
		case AssemblySlot::DRIVE_BELT_TENSIONER_BOLT:
			local = { -0.01f, 0.f, 0.f };
			turn = FACING_FRONT;
			break;
		// The timing idler pulley's, through the middle of its pulley.
		case AssemblySlot::BELT_IDLER_BOLT:
			local = { -0.0135f, 0.f, 0.f };
			turn = FACING_FRONT;
			break;
		case AssemblySlot::ROCKER_SHAFT_BOLT:
			// Down through the shafts at each end and between the cylinders.
			local = { RockerBoltX(hole), 0.008f, 0.f };
			break;
		// Round the pump's body, through it into the block, heads seated on its face.
		case AssemblySlot::OIL_PUMP_BOLT:
		{
			float angle = glm::two_pi<float>() * (static_cast<float>(hole) + 0.5f) / static_cast<float>(holes.perParent);
			local = { 0.045f * std::cos(angle), 0.016f, 0.045f * std::sin(angle) };
			break;
		}
		// The alternator's: the pivot bolt in from the front through its lugs and the bracket's boss, and the adjusting bolt
		// through its top lug into the bracket's strap; heads out the front.
		case AssemblySlot::ALTERNATOR_BOLT:
			local = hole == 0 ? glm::vec3(ALTERNATOR_PIVOT_X - 0.034f, ALTERNATOR_PIVOT_Y, 0.f) : glm::vec3(ALTERNATOR_ADJUST_X, ALTERNATOR_ADJUST_Y, 0.f);
			turn = FACING_FRONT;
			break;
		// The bracket's, into the side of the block through the bosses on its plate, heads out.
		case AssemblySlot::ALTERNATOR_BRACKET_BOLT:
		{
			glm::vec2 at = ALTERNATOR_BRACKET_BOLTS[std::clamp(hole, 0, 2)];
			local = glm::vec3(FRONT_X + at.x, at.y, BLOCK_WIDTH * 0.5f + 0.01f) - glm::vec3(ALTERNATOR_BRACKET_X, ALTERNATOR_BRACKET_Y, ALTERNATOR_Z);
			turn = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.f, 0.f, 0.f));
			break;
		}
		// The manifolds' nuts, on the studs through the bosses on their head flanges, out from the head.
		case AssemblySlot::INTAKE_MANIFOLD_NUT:
		case AssemblySlot::EXHAUST_MANIFOLD_NUT:
		{
			float out = slot == AssemblySlot::INTAKE_MANIFOLD_NUT ? 1.f : -1.f;
			glm::vec2 at = ManifoldNut(hole);
			local = { at.x, at.y, out * MANIFOLD_FLANGE };
			turn = glm::angleAxis(out * glm::half_pi<float>(), glm::vec3(1.f, 0.f, 0.f));
			break;
		}
		// The downpipe's, up through the ears of its flange into the manifold's outlet, heads underneath.
		case AssemblySlot::EXHAUST_PIPE_BOLT:
		{
			float angle = static_cast<float>(hole) * glm::two_pi<float>() / 3.f;
			local = EXHAUST_OUTLET + glm::vec3(std::cos(angle) * EXHAUST_BOLT_RING, -0.006f, std::sin(angle) * EXHAUST_BOLT_RING);
			turn = FLIPPED;
			break;
		}
		// The thermostat housing's, heads down on the ears on opposite corners of its flange.
		case AssemblySlot::THERMOSTAT_HOUSING_BOLT:
		{
			float side = hole == 0 ? -1.f : 1.f;
			local = { side * THERMOSTAT_BOLT.x, 0.006f, -side * THERMOSTAT_BOLT.y };
			break;
		}
		// The fuel pump's, heads down on its flange either side of the body, over the holes in its gasket and the head.
		case AssemblySlot::FUEL_PUMP_BOLT:
			local = { hole == 0 ? -0.027f : 0.027f, 0.004f, 0.f };
			break;
		// The water pump's, on the bosses on the front of its flange, on the diagonals, heads out the front.
		case AssemblySlot::WATER_PUMP_BOLT:
		{
			float angle = glm::quarter_pi<float>() + static_cast<float>(hole) * glm::half_pi<float>();
			local = { -0.009f, std::sin(angle) * WATER_PUMP_BOLT_RING, std::cos(angle) * WATER_PUMP_BOLT_RING };
			turn = FACING_FRONT;
			break;
		}
		// The pickup's, up through either side of its flange into the pump's lug, heads under the flange.
		case AssemblySlot::OIL_PICKUP_BOLT:
			local = { 0.f, -0.006f, hole == 0 ? -0.008f : 0.008f };
			turn = FLIPPED;
			break;
		default:
		{
			// Spread evenly round the edge of the part's top face, a little in from the edge.
			const auto& bounds = PartBounds(holes.parent);
			glm::vec3 lo = bounds.first, hi = bounds.second;
			// The timing cover's go round its body, not out round the water pump's ear.
			if (slot == AssemblySlot::TIMING_COVER_BOLT)
			{
				lo.z = -TIMING_COVER_HALF_WIDTH;
				hi.z = TIMING_COVER_HALF_WIDTH;
			}
			glm::vec2 inset = (glm::vec2(hi.x, hi.z) - glm::vec2(lo.x, lo.z)) * 0.08f;
			// The pan's go round the middle of its lip, outside the sump's walls.
			if (slot == AssemblySlot::OIL_PAN_BOLT)
				inset = { OIL_PAN_LIP * 0.5f, OIL_PAN_LIP * 0.5f };
			glm::vec2 a = glm::vec2(lo.x, lo.z) + inset, b = glm::vec2(hi.x, hi.z) - inset;
			// The timing cover's are ten spots round its rim less two: the 2nd (by the water pump's hole) and the 10th (by
			// the crank's).
			int spot = hole, spots = holes.perParent;
			if (slot == AssemblySlot::TIMING_COVER_BOLT)
			{
				static constexpr int KEPT[] = { 0, 2, 3, 4, 5, 6, 7, 8 };
				spot = KEPT[std::clamp(hole, 0, 7)];
				spots = 10;
			}
			if (spots == 1)
				local = { (a.x + b.x) * 0.5f, hi.y, (a.y + b.y) * 0.5f };
			else
			{
				float width = b.x - a.x, depth = b.y - a.y;
				float along = (static_cast<float>(spot) + 0.5f) / spots * 2.f * (width + depth);
				glm::vec2 point = along < width ? glm::vec2(a.x + along, a.y)
					: along < width + depth ? glm::vec2(b.x, a.y + along - width)
					: along < 2.f * width + depth ? glm::vec2(b.x - (along - width - depth), b.y)
					: glm::vec2(a.x, b.y - (along - 2.f * width - depth));
				local = { point.x, hi.y, point.y };
			}
			// The valve cover's go down through its top plate, and the timing cover's through its face: on
			// those, under the caps and bosses that stand higher, so a done-up one shows only its head.
			if (slot == AssemblySlot::VALVE_COVER_BOLT)
				local.y = VALVE_COVER_TOP;
			else if (slot == AssemblySlot::TIMING_COVER_BOLT)
				local.y = TIMING_COVER_FACE;
			// The pan's bolts go up through its rail into the block.
			if (slot == AssemblySlot::OIL_PAN_BOLT)
			{
				local.y = -0.006f;
				turn = FLIPPED;
			}
			break;
		}
	}
	return { parent.offset + parent.rotation * local, parent.rotation * turn, { 1.f, 1.f, 1.f } };
}

namespace
{
	// Engine space runs cylinder 1 to 4 along +X, which would leave cylinder 4 out on the far side
	// of the stand. The engine is turned end for end so its flywheel end (cylinder 4) is against the
	// mounting plate and cylinder 1 hangs out at the front.
	const glm::quat ENGINE_ON_STAND = glm::angleAxis(glm::pi<float>(), glm::vec3(0.f, 1.f, 0.f));
	// On the hoist it hangs level, its crank along the boom.
	const glm::quat ENGINE_ON_HOIST = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f));
	// In the car, lengthways: the timing end (-X in engine space) to the car's nose (+X in its space).
	const glm::quat ENGINE_IN_CAR = glm::angleAxis(glm::pi<float>(), glm::vec3(0.f, 1.f, 0.f));
}

ShopWorld::EnginePose ShopWorld::RestingPose(int engine) const
{
	if (engine == m_engineOnHoist && m_hoistStation < m_stations.size())
	{
		const Station& hoist = m_stations[m_hoistStation];
		return { hoist.position + hoist.Rotation() * HOIST_HANG, hoist.Rotation() * ENGINE_ON_HOIST };
	}
	// In the car it sits on its mounts in the bay, its timing end to the car's nose.
	if (engine == m_engineInCar && m_carStation < m_stations.size() && m_stations[m_carStation].present)
		return { CarToWorld(CAR_ENGINE_MOUNT), CarRotation() * ENGINE_IN_CAR };
	// On the dyno it's held level, the right way round.
	if (engine == m_engineOnDyno)
		if (const Station* dyno = FindStation(StationType::DYNO))
			return { dyno->position + dyno->Rotation() * DYNO_MOUNT, dyno->Rotation() };
	const Station* stand = FindStand(engine);
	if (!stand)
		return {};
	// The stand's head rolls about an axis through the middle of the block, not its deck.
	const glm::vec3 pivot = { 0.f, STAND_ROLL_PIVOT_Y, 0.f };
	glm::quat roll = AboutX(m_engineRolls[std::clamp(engine, 0, Workshop::MAX_ENGINES - 1)]);
	return { stand->Anchor() + stand->Rotation() * (ENGINE_ON_STAND * (pivot - roll * pivot)), stand->Rotation() * ENGINE_ON_STAND * roll };
}

ShopWorld::EnginePose ShopWorld::CurrentPose(int engine) const
{
	EnginePose resting = RestingPose(engine);
	if (engine != m_swing.engine || m_swing.progress >= 1.f)
		return resting;
	// Lifted a little clear of everything on the way.
	float t = glm::smoothstep(0.f, 1.f, m_swing.progress);
	glm::vec3 position = glm::mix(m_swing.from.position, resting.position, t);
	position.y += 0.12f * std::sin(t * glm::pi<float>());
	return { position, glm::slerp(m_swing.from.rotation, resting.rotation, t) };
}

glm::quat ShopWorld::StandRotation(int engine) const
{
	return CurrentPose(engine).rotation;
}

glm::vec3 ShopWorld::StandToWorld(int engine, const glm::vec3& offset) const
{
	EnginePose pose = CurrentPose(engine);
	return pose.position + pose.rotation * offset;
}

void ShopWorld::SyncEngineMoves(const Workshop& workshop)
{
	int nowOnDyno = workshop.GetEngineOnDyno(), nowOnHoist = workshop.GetEngineOnHoist(), nowInCar = workshop.GetEngineInCar();
	if (nowOnDyno == m_engineOnDyno && nowOnHoist == m_engineOnHoist && nowInCar == m_engineInCar)
		return;
	// Into or out of the car it comes off or goes onto the hoist, swinging as it does (below); the car's pose is
	// only taken up once the swing has started from where it was.
	struct InCarLater
	{
		int& at;
		int now;
		~InCarLater() { at = now; }
	} inCarLater{ m_engineInCar, nowInCar };
	// Onto or off the hoist, it swings from where it was: off its stand or the dyno, or down from the
	// hook. One lowered onto another stand is that stand's engine now (the workshop's active one).
	std::optional<EngineSwing> swing;
	if (nowOnHoist != m_engineOnHoist)
	{
		int before = nowOnHoist >= 0 ? nowOnHoist : m_engineOnHoist;
		int after = nowOnHoist >= 0 || nowOnDyno == m_engineOnHoist ? before : workshop.GetActiveEngine();
		swing = EngineSwing{ after, CurrentPose(before), 0.f };
	}
	// One back onto a stand, off the dyno or the hoist, takes its crank position with it, and sits
	// upright there.
	int off = m_engineOnDyno >= 0 && nowOnDyno < 0 && nowOnHoist != m_engineOnDyno ? m_engineOnDyno
		: m_engineOnHoist >= 0 && nowOnHoist < 0 && nowOnDyno != m_engineOnHoist ? m_engineOnHoist : -1;
	if (off >= 0)
	{
		int stand = std::clamp(workshop.GetActiveEngine(), 0, Workshop::MAX_ENGINES - 1);
		m_crankAngles[stand] = m_crankAngles[std::clamp(off, 0, Workshop::MAX_ENGINES - 1)];
		m_camCranks[stand] = m_camCranks[std::clamp(off, 0, Workshop::MAX_ENGINES - 1)];
		m_engineRolls[stand] = 0.f;
	}
	m_engineOnDyno = nowOnDyno;
	m_engineOnHoist = nowOnHoist;
	if (swing)
		m_swing = *swing;
}

glm::vec3 ShopWorld::HoistHook() const
{
	if (m_hoistStation >= m_stations.size())
		return {};
	const Station& hoist = m_stations[m_hoistStation];
	return hoist.position + hoist.Rotation() * HOIST_HOOK;
}

std::optional<int> ShopWorld::HoistTarget(const Workshop& workshop) const
{
	if (m_hoistStation >= m_stations.size() || !m_stations[m_hoistStation].present)
		return std::nullopt;
	// The nearest stand or dyno under the hook's reach: with an engine on, one it can go onto; without,
	// one with an engine to lift.
	glm::vec3 hook = HoistHook();
	bool hanging = workshop.GetEngineOnHoist() >= 0;
	std::optional<int> best;
	float nearest = HOIST_REACH;
	auto consider = [&](int target, const glm::vec3& mount) {
		float distance = glm::length(glm::vec2(mount.x - hook.x, mount.z - hook.z));
		if (distance < nearest)
		{
			nearest = distance;
			best = target;
		}
	};
	for (const auto& station : m_stations)
	{
		if (station.type != StationType::ENGINE_STAND || !station.present || station.engineIndex >= workshop.GetEngineCount())
			continue;
		int engine = station.engineIndex;
		if (hanging ? workshop.IsStandFreeForHoist(engine)
			: engine != workshop.GetEngineOnDyno() && !workshop.GetAssembly(engine).GetAllParts().empty())
			consider(engine, station.Anchor());
	}
	if (const Station* dyno = FindStation(StationType::DYNO); dyno && dyno->present && (workshop.GetEngineOnDyno() >= 0) != hanging)
		consider(DYNO_TARGET, dyno->position + dyno->Rotation() * DYNO_MOUNT);
	// The car's engine bay: an engine to lift out, or room for the one on the hook.
	if (const Station* car = FindStation(StationType::CAR); car && car->present && (workshop.GetEngineInCar() >= 0) != hanging)
		consider(CAR_TARGET, car->position + car->Rotation() * CAR_ENGINE_BAY);
	return best;
}

std::vector<BenchItem> ShopWorld::GetBenchItems() const
{
	std::vector<BenchItem> items;

	// Anything put down on the top of the bench last opened counts as on the bench, however it got
	// there: the shop's own, or one bought from the store.
	const Station* bench = FindStation(StationType::WORKBENCH);
	if (m_activeBench < m_stations.size() && m_stations[m_activeBench].present && IsWorkbench(m_stations[m_activeBench]))
		bench = &m_stations[m_activeBench];
	bool ownBench = bench->type == StationType::WORKBENCH;
	std::string location = ownBench ? "On the workbench" : "On the " + bench->name;
	ShopBox top = TransformBox(bench->localSurfaces.front(), bench->position, bench->Rotation());
	// Each part there, and under it what's fitted into or onto it (and onto that), so all of an assembly can be looked at.
	auto add = [&](const pPart& part) {
		items.push_back({ part, location, -1 });
		auto addFitted = [&](auto& self, uint32_t host, int depth) -> void {
			std::vector<const ItemVisual*> fitted;
			for (const auto& [fittedRef, fittedVisual] : m_items)
				if (fittedVisual.place == ItemPlace::ATTACHED && fittedVisual.hostPart == host && fittedVisual.part)
					fitted.push_back(&fittedVisual);
			// In a head: each valve, in order along the head, with its stem seal, spring and retainer under it.
			auto isValve = [](const ItemVisual* visual) {
				return visual->headSpot.first == AssemblySlot::INTAKE_VALVE || visual->headSpot.first == AssemblySlot::EXHAUST_VALVE;
			};
			auto valveOf = [&](const ItemVisual* visual) {
				const auto& [slot, index] = visual->headSpot;
				return isValve(visual) ? EngineAssembly::SpringIndex(index, slot == AssemblySlot::EXHAUST_VALVE) : index;
			};
			bool head = std::any_of(fitted.begin(), fitted.end(), [](const ItemVisual* visual) { return visual->headSpot.first != AssemblySlot::COUNT; });
			if (head)
			{
				// What's on a valve: seal, spring, retainer (in build order).
				auto stackOrder = [](AssemblySlot slot) {
					return slot == AssemblySlot::VALVE_STEM_SEAL ? 0 : slot == AssemblySlot::VALVE_SPRING ? 1 : 2;
				};
				std::sort(fitted.begin(), fitted.end(), [&](const ItemVisual* a, const ItemVisual* b) { return valveOf(a) < valveOf(b); });
				for (const auto* valve : fitted)
				{
					if (!isValve(valve))
						continue;
					items.push_back({ valve->part, location, -1, depth });
					std::vector<const ItemVisual*> onIt;
					for (const auto* other : fitted)
						if (!isValve(other) && other->headSpot.first != AssemblySlot::COUNT && valveOf(other) == valveOf(valve))
							onIt.push_back(other);
					std::sort(onIt.begin(), onIt.end(), [&](const ItemVisual* a, const ItemVisual* b) { return stackOrder(a->headSpot.first) < stackOrder(b->headSpot.first); });
					for (const auto* other : onIt)
						items.push_back({ other->part, location, -1, depth + 1 });
				}
				// A seal or spring with its valve gone is listed on its own.
				for (const auto* other : fitted)
					if (!isValve(other) && std::none_of(fitted.begin(), fitted.end(), [&](const ItemVisual* valve) { return isValve(valve) && valveOf(valve) == valveOf(other); }))
						items.push_back({ other->part, location, -1, depth });
				return;
			}
			std::sort(fitted.begin(), fitted.end(), [](const ItemVisual* a, const ItemVisual* b) { return a->part->GetType() < b->part->GetType(); });
			for (const auto* fittedVisual : fitted)
			{
				items.push_back({ fittedVisual->part, location, -1, depth });
				self(self, fittedVisual->part->GetId(), depth + 1);
			}
		};
		addFitted(addFitted, part->GetId(), 1);
	};
	for (const auto& [ref, visual] : m_items)
	{
		if (!visual.part)
			continue;
		if (visual.place == ItemPlace::BENCH)
		{
			if (ownBench)
				add(visual.part);
			continue;
		}
		if (visual.place != ItemPlace::WORLD)
			continue;

		auto bounds = WorldBounds(visual.object);
		glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
		bool overTop = center.x >= top.min.x && center.x <= top.max.x && center.z >= top.min.z && center.z <= top.max.z;
		// Still settling counts too, so the bench screen can open the moment it's set down.
		bool onTop = bounds.min.y >= top.max.y - 0.02f && bounds.min.y <= top.max.y + 0.3f;
		if (overTop && onTop)
			add(visual.part);
	}

	const auto& machines = MachineCatalog::All();
	for (size_t i = 0; i < machines.size(); i++)
		for (const auto& [ref, visual] : m_items)
			if (visual.part && visual.place == ItemPlace::MACHINE && visual.machineIndex == static_cast<int>(i))
				items.push_back({ visual.part, "In the " + machines[i].name, static_cast<int>(i) });
	return items;
}

void ShopWorld::UpdateOverlays(ItemVisual& visual) const
{
	glm::vec3 position = visual.object->GetPosition();
	glm::quat rotation = visual.object->GetRotation();
	bool visible = visual.object->IsVisible();

	for (auto& [overlay, object] : visual.overlays)
	{
		const Feature* feature = visual.part->FindFeature(overlay.featureName);
		uint32_t texture = feature ? m_assets->SurfaceTexture(*feature) : 0;
		object->SetVisible(visible && texture != 0);
		if (texture == 0)
			continue;

		object->SetTexture(texture);
		object->SetPosition(position + rotation * overlay.offset);
		object->SetRotation(rotation);
		object->SetTint(visual.object->GetTint());
		object->SetEmissive(visual.object->GetEmissive());
	}
}

bool ShopWorld::SocketInUse(int sizeMm, size_t setId) const
{
	// However many drives have a socket of this size on, that many sets are each missing one: the first
	// copies of the set first.
	size_t out = 0;
	for (const auto& [tool, size] : m_toolSizes)
		if (size == sizeMm && IsDriveTool(ToolCatalog::Of(tool)))
			out++;
	return ToolCatalog::CopyOf(setId) < out;
}

void ShopWorld::UpdateSockets(ItemVisual& visual) const
{
	if (visual.sockets.empty())
		return;
	const auto& tool = ToolCatalog::Of(visual.ref.id);
	glm::vec3 position = visual.object->GetPosition();
	glm::quat rotation = visual.object->GetRotation();
	glm::vec3 scale = visual.object->GetScale();
	bool visible = visual.object->IsVisible();
	auto place = [&](const pObject& socket, const glm::vec3& local, int size, bool there, const glm::quat& turn = glm::quat(1.f, 0.f, 0.f, 0.f)) {
		socket->SetVisible(visible && there);
		if (!visible || !there)
			return;
		float across = SocketRadius(size) / SOCKET_MODEL_RADIUS;
		socket->SetPosition(position + rotation * (local * scale));
		socket->SetRotation(rotation * turn);
		socket->SetScale(scale * glm::vec3(across, 1.f, across));
		socket->SetCastsShadows(visual.object->CastsShadows());
		socket->SetTint(visual.object->GetTint());
		socket->SetEmissive(visual.object->GetEmissive());
	};

	if (tool.handKind == HandToolKind::SOCKETS)
		for (size_t i = 0; i < visual.sockets.size(); i++)
			place(visual.sockets[i], SocketSlot(tool, i), tool.sizes[i], !SocketInUse(tool.sizes[i], visual.ref.id));
	else
	{
		auto it = m_toolSizes.find(visual.ref.id);
		int size = it == m_toolSizes.end() ? 0 : it->second;
		place(visual.sockets[0], DriveSocketPosition(tool), size, size > 0, DriveSocketTurn(tool));
	}
}

glm::vec4 ShopWorld::TightnessTint(const Part& fastener) const
{
	// Red finger tight, orange part way done up, yellow snugged up by feel, green torqued.
	switch (fastener.GetTightness())
	{
		case FastenerTightness::TORQUED:	return { 0.45f, 1.7f, 0.55f, 1.f };
		case FastenerTightness::SNUG:		return { 1.7f, 1.45f, 0.35f, 1.f };
		default:
			return m_fastenerTurns.contains(fastener.GetId()) ? glm::vec4(1.8f, 0.85f, 0.25f, 1.f) : glm::vec4(1.8f, 0.4f, 0.35f, 1.f);
	}
}

void ShopWorld::LayoutItems(const Workshop& workshop, const FirstPersonController& player)
{
	for (int engine = 0; engine < Workshop::MAX_ENGINES; engine++)
	{
		EnginePose pose = CurrentPose(engine);
		StandLayout now = { pose.position, pose.rotation, m_crankAngles[engine], m_unseated[engine], m_ringsInBore[engine] };
		const auto& before = m_laidOutStands[engine];
		bool shaking = engine == m_dynoEngine && m_engineRpm > 0.f;
		m_standPartsMoved[engine] = shaking || now.crank != before.crank || now.unseated != before.unseated || now.ringsInBore != before.ringsInBore;
		m_standMoved[engine] = m_standPartsMoved[engine] || now.position != before.position || now.rotation != before.rotation;
		m_standPoses[engine] = pose;
		m_laidOutStands[engine] = now;
	}
	const Station* bench = FindStation(StationType::WORKBENCH);
	const Station* toolBoard = FindStation(StationType::TOOL_BOARD);
	LayOutToolBoard();
	// Something in hand that turns nuts and bolts.
	const ItemVisual* inHand = GetHeld();
	const MeasuringTool* handTool = inHand && inHand->ref.IsTool() ? &ToolCatalog::Of(inHand->ref.id) : nullptr;
	bool spannerInHand = handTool && (IsDriveTool(*handTool) || handTool->handKind == HandToolKind::COMBINATION_WRENCH
		|| handTool->handKind == HandToolKind::WRENCHES);
	// Whether the car's been put somewhere else since its parts were last laid out on it.
	bool carMoved = false;
	if (m_carStation < m_stations.size())
	{
		const auto& carStation = m_stations[m_carStation];
		carMoved = carStation.position != m_carLaidOutAt || carStation.yaw != m_carLaidOutYaw;
		m_carLaidOutAt = carStation.position;
		m_carLaidOutYaw = carStation.yaw;
	}

	auto layout = [&](ItemVisual& visual)
	{
		// A spark plug lead on the engine runs all the way to its tower on the distributor cap.
		if (visual.part && visual.part->GetType() == PartType::PLUG_LEAD)
		{
			uint32_t wanted = m_assets->PartModel(PartType::PLUG_LEAD);
			if (visual.place == ItemPlace::STAND)
				if (auto slot = FindInstalledSlot(workshop.GetAssembly(visual.engineIndex), visual.part))
					wanted = m_assets->Model(ShopModels::PlugLeadFitted(std::clamp(slot->second, 0, 3)));
			if (visual.object->GetModelIndex() != wanted)
			{
				ObjectManager::Get()->ChangeObjectModel(visual.object, wanted);
				visual.standSlot = { AssemblySlot::COUNT, -1 };
			}
		}
		// A fender or door on the car's right side is the mirror of the left-hand one (it keeps that hand once it's off).
		if (visual.part && visual.place == ItemPlace::CAR
			&& (visual.part->GetType() == PartType::FRONT_FENDER || visual.part->GetType() == PartType::DOOR) && workshop.GetCar())
			if (auto at = workshop.GetCar()->assembly.Find(visual.part.get()))
			{
				bool fender = visual.part->GetType() == PartType::FRONT_FENDER;
				uint32_t wanted = at->second == 1 ? m_assets->Model(fender ? ShopModels::FENDER_RIGHT : ShopModels::DOOR_RIGHT)
					: m_assets->PartModel(visual.part->GetType());
				if (visual.object->GetModelIndex() != wanted)
				{
					ObjectManager::Get()->ChangeObjectModel(visual.object, wanted);
					visual.carSlot = -1;
				}
			}
		auto model = ModelOf(visual.object);
		glm::vec4 tint = glm::vec4(1.f);
		if (visual.part)
		{
			tint = m_assets->PartTint(visual.part->GetMaterial());
			if (visual.part->GetCondition() == PartCondition::DAMAGED)
				tint *= DAMAGED_TINT;
		}
		// Sprayed: the paint over it, as thick as it's gone on.
		if (auto coat = m_coats.find(visual.ref); coat != m_coats.end())
		{
			glm::vec3 paint = { ((coat->second.rgb >> 16) & 0xFF) / 255.f, ((coat->second.rgb >> 8) & 0xFF) / 255.f, (coat->second.rgb & 0xFF) / 255.f };
			tint = glm::vec4(glm::mix(glm::vec3(tint), paint * 1.6f, std::clamp(coat->second.amount, 0.f, 1.f)), tint.a);
		}
		// A spanner in hand: the nuts and bolts on the engines and the car show how done up they are.
		if (spannerInHand && visual.part && (visual.place == ItemPlace::STAND || visual.place == ItemPlace::CAR)
			&& PartCatalog::Get(visual.part->GetType()).fastener.IsFastener())
			tint = TightnessTint(*visual.part);
		visual.object->SetTint(tint);
		visual.object->SetEmissive(0.f);
		// What another player carries shows only in their hand.
		bool remoteHeld = visual.place == ItemPlace::CARRIED && visual.carriedInHand && m_remoteHands.contains(visual.carrier);
		// On the car, all but the shell itself (that's the car's own model).
		bool visible = visual.place != ItemPlace::PACKAGED && visual.place != ItemPlace::INVENTORY
			&& !(visual.place == ItemPlace::CAR && visual.part && visual.part->GetType() == PartType::BODY_SHELL)
			&& (visual.place != ItemPlace::CARRIED || remoteHeld);
		visual.object->SetVisible(visible);
		visual.object->SetPickable(visible && visual.place != ItemPlace::HAND && visual.place != ItemPlace::CARRIED);
		// What's in your hand moves with the camera: a shadow from it would make the lamps redraw
		// their shadow maps every frame.
		visual.object->SetCastsShadows(visual.place != ItemPlace::HAND && visual.place != ItemPlace::CARRIED);

		switch (visual.place)
		{
			case ItemPlace::STORAGE:
				// Hooks are laid out in the board's own space, so hung tools go wherever the board is
				// moved to.
				if (visual.ref.IsTool())
				{
					// Hung upright on the tool board, each tool in its own spot; another copy of it hangs on
					// the same hook, out in front of the one before.
					// One hung by hand goes where it was put, its middle on the spot.
					const ToolHook& hook = m_toolHooks[ToolCatalog::IndexOf(visual.ref.id)];
					glm::vec3 position = hook.position + glm::vec3(0.f, 0.f, 0.03f * static_cast<float>(ToolCatalog::CopyOf(visual.ref.id)));
					if (auto spot = m_toolSpots.find(visual.ref.id); spot != m_toolSpots.end())
						position = glm::vec3(spot->second - hook.middle, hook.out);
					visual.object->SetRotation(toolBoard->Rotation() * hook.rotation);
					visual.object->SetPosition(toolBoard->position + toolBoard->Rotation() * position);
					visual.object->SetScale({ 1.f, 1.f, 1.f });
				}
				else
				{
					// Parts put away (or knocked off the bench, a machine or a stand) are set down
					// loose on the shelf.
					PutOnShelf(visual);
				}
				break;
			case ItemPlace::BENCH:
				PlaceResting(visual, bench->Anchor(), bench->yaw);
				break;
			case ItemPlace::MACHINE:
				for (const auto& station : m_stations)
					if (station.type == StationType::MACHINE && station.machineIndex == visual.machineIndex)
						PlaceResting(visual, station.Anchor(), station.yaw + station.partYaw);
				break;
			case ItemPlace::HAND:
				PlaceInHand(visual, player);
				break;
			case ItemPlace::STAND:
				// Re-posed only when the engine has moved (turned, rolled, carried, shaking on the dyno) or
				// the part has: a whole engine's worth of poses every frame was a good part of the frame.
				// A nut or bolt, also as it's turned in or out.
				if (auto slot = FindInstalledSlot(workshop.GetAssembly(visual.engineIndex), visual.part))
				{
					float rise = EngineAssembly::Holes(slot->first) ? FastenerRise(visual, slot->first, slot->second) : 0.f;
					int engine = std::clamp(visual.engineIndex, 0, Workshop::MAX_ENGINES - 1);
					if (m_standPartsMoved[engine] || visual.standSlot != *slot || visual.standRise != rise)
					{
						PlaceOnStand(visual, slot->first, slot->second);
						visual.standSlot = *slot;
						visual.standRise = rise;
					}
					// Only the engine as a whole has moved (rolled, or on the hoist): its parts go with it as they sit.
					else if (m_standMoved[engine])
					{
						const EnginePose& pose = m_standPoses[engine];
						visual.object->SetPosition(pose.position + pose.rotation * visual.standOffset);
						visual.object->SetRotation(pose.rotation * visual.standTurn);
					}
				}
				break;
			case ItemPlace::WORLD:
				visual.object->SetRotation(RestingRotation(visual));
				visual.object->SetScale({ 1.f, 1.f, 1.f });
				break;
			case ItemPlace::CARRIED:
				// Held out in front of the other player, centred on their hand, turned with them.
				if (remoteHeld)
				{
					const auto& [hand, yaw] = m_remoteHands.at(visual.carrier);
					glm::vec3 center = (model->GetBoundsMin() + model->GetBoundsMax()) * 0.5f;
					visual.object->SetRotation(Yaw(yaw));
					visual.object->SetPosition(hand + OnAvatarHand(visual, *model) - Yaw(yaw) * center);
					visual.object->SetScale({ 1.f, 1.f, 1.f });
				}
				break;
			case ItemPlace::CAR:
				// Re-posed only when it's gone into another position, a nut or bolt has gone in or come out of its hole,
				// or the car itself has moved: a whole car's worth of poses every frame was most of the layout.
				if (const auto* car = workshop.GetCar())
					if (auto at = car->assembly.Find(visual.part.get()))
					{
						bool raised = CarAssembly::IsFastenerSlot(at->first) && visual.part->GetTightness() == FastenerTightness::LOOSE
							&& !PartWayDoneUp(*visual.part);
						if (carMoved || m_carPanelsMoving || visual.carSlot != static_cast<int>(at->first) || visual.carIndex != at->second || visual.carRaised != raised)
						{
							PlaceOnCar(visual, at->first, at->second);
							visual.carSlot = static_cast<int>(at->first);
							visual.carIndex = at->second;
							visual.carRaised = raised;
						}
					}
				break;
			case ItemPlace::PACKAGED:
			case ItemPlace::INVENTORY:
			case ItemPlace::ATTACHED:	// after its rod or cap, below
				break;
		}

		if (visual.part)
			UpdateOverlays(visual);
		else
			UpdateSockets(visual);
	};

	// Parts in inventory order so storage spots stay put from frame to frame, then tools.
	for (const auto& part : workshop.GetInventory())
		if (auto visual = Find(ItemRef::Part(part->GetId())))
			layout(*visual);
	for (auto& [ref, visual] : m_items)
		if (ref.IsTool())
			layout(visual);
	// What's fitted into or onto another part sits in it, wherever it is, and is seen when it is: an upper rod shell
	// in the big end, a rod's length from the small end; a lower shell where its cap is; a piston over the small
	// end, its ring in its top groove and its pin through both. Twice round, so a pin follows its piston once that's followed its rod.
	for (int pass = 0; pass < 2; pass++)
	for (auto& [ref, visual] : m_items)
	{
		if (visual.place != ItemPlace::ATTACHED)
			continue;
		const ItemVisual* host = Find(ItemRef::Part(visual.hostPart));
		visual.object->SetVisible(host && host->object->IsVisible());
		visual.object->SetPickable(false);
		if (!host)
			continue;
		visual.object->SetCastsShadows(host->object->CastsShadows());
		PartType hostType = host->part ? host->part->GetType() : PartType::BLOCK;
		PartType type = visual.part ? visual.part->GetType() : PartType::BLOCK;
		// How far the Valve Spring Compressor squeezes a spring down.
		constexpr float SQUEEZE = 0.009f;
		// The compressor clamped over a valve in a loose head: its cup on the spring's top, its frame out over the side
		// the valve's on and its foot under the valve. It can be aimed at (to take it off) unless a part's in hand.
		if (!visual.part && visual.ref.IsTool() && ToolCatalog::Of(visual.ref.id).handKind == HandToolKind::VALVE_SPRING_COMPRESSOR)
		{
			const auto& clamp = workshop.GetValveCompressor();
			if (clamp && clamp->head == visual.hostPart)
			{
				using namespace EngineLayout;
				bool exhaust = clamp->index % 2 == 1;
				glm::vec3 at = { BoreX(clamp->index / 2), HEAD_FLOOR_Y + SPRING_INSTALLED_HEIGHT - SQUEEZE, ValveZ(exhaust) };
				glm::quat turn = exhaust ? glm::quat(1.f, 0.f, 0.f, 0.f) : glm::angleAxis(glm::pi<float>(), glm::vec3(0.f, 1.f, 0.f));
				visual.object->SetRotation(host->object->GetRotation() * turn);
				visual.object->SetScale({ 1.f, 1.f, 1.f });
				visual.object->SetPosition(host->object->GetPosition() + host->object->GetRotation() * at);
				const ItemVisual* inHand = GetHeld();
				visual.object->SetPickable(!(inHand && inHand->part));
			}
			continue;
		}
		// A rocker arm on a loose shaft: over its cylinder, its pivot on the shaft and the arm reaching out to where the valve
		// would be (as an exhaust arm sits on the engine, the shaft lying along X).
		if (hostType == PartType::ROCKER_SHAFT && visual.part)
			if (const HeadFitting* at = workshop.LooseHeadFittingOf(*visual.part))
			{
				using namespace EngineLayout;
				glm::vec3 local = { BoreX(at->index), -ROCKER_PIVOT_Y, -ROCKER_PIVOT_Z };
				visual.object->SetRotation(host->object->GetRotation());
				visual.object->SetScale(host->object->GetScale());
				visual.object->SetPosition(host->object->GetPosition() + host->object->GetRotation() * (local * host->object->GetScale()));
				continue;
			}
		// A valve, seal, spring or retainer in a loose head: in its place there, shut (a spring under the compressor
		// squeezed down, its retainer with it).
		if (hostType == PartType::CYLINDER_HEAD && visual.part)
			if (const HeadFitting* at = workshop.LooseHeadFittingOf(*visual.part))
			{
				visual.headSpot = { at->slot, at->index };
				auto pose = HeadSpotPose(at->slot, at->index);
				if ((at->slot == AssemblySlot::VALVE_SPRING || at->slot == AssemblySlot::VALVE_RETAINER) && workshop.CompressedAt(*host->part, at->index))
				{
					if (at->slot == AssemblySlot::VALVE_SPRING)
						pose.scale.y = (EngineLayout::SPRING_INSTALLED_HEIGHT - SQUEEZE) / EngineLayout::SPRING_FREE_HEIGHT;
					else
						pose.offset.y -= SQUEEZE;
				}
				glm::vec3 hostScale = host->object->GetScale();
				visual.object->SetRotation(host->object->GetRotation() * pose.rotation);
				visual.object->SetScale(hostScale * pose.scale);
				visual.object->SetPosition(host->object->GetPosition() + host->object->GetRotation() * (pose.offset * hostScale));
				continue;
			}
		// A ring in the bore of a loose block: in the top of it, or pushed down square.
		const BoreRing* bore = type == PartType::PISTON_RING && hostType == PartType::BLOCK ? workshop.LooseBoreOf(*visual.part) : nullptr;
		glm::vec3 local = bore ? glm::vec3(EngineLayout::BoreX(bore->cylinder), bore->squared ? RING_SQUARED_Y : RING_IN_BORE_Y, 0.f)
			: type == PartType::PISTON ? glm::vec3(0.f, EngineLayout::PISTON_PIN_DEPTH, 0.f)
			: type == PartType::PISTON_RING ? glm::vec3(0.f, -0.006f, 0.f)
			: hostType == PartType::PISTON ? glm::vec3(0.f, -EngineLayout::PISTON_PIN_DEPTH, 0.f)
			: hostType == PartType::CONNECTING_ROD ? glm::vec3(0.f, -EngineLayout::ROD_LENGTH, 0.f) : glm::vec3(0.f);
		visual.object->SetRotation(host->object->GetRotation());
		visual.object->SetScale(host->object->GetScale());
		visual.object->SetPosition(host->object->GetPosition() + host->object->GetRotation() * (local * host->object->GetScale()));
	}
	LayOutBatteries();
	LayOutSetMembers();
}

float ShopWorld::SupportHeight(const ShopBox& bounds, float previousBottom, const Object* self) const
{
	// The highest surface under the body's footprint that it was above last step. The footprint
	// is shrunk a little so an item hanging off an edge by a whisker still falls.
	glm::vec2 footprintMin = glm::vec2(bounds.min.x, bounds.min.z);
	glm::vec2 footprintMax = glm::vec2(bounds.max.x, bounds.max.z);
	glm::vec2 inset = (footprintMax - footprintMin) * 0.15f;
	footprintMin += inset;
	footprintMax -= inset;

	float support = 0.f;
	auto consider = [&](const glm::vec3& boxMin, const glm::vec3& boxMax) {
		if (boxMax.x < footprintMin.x || boxMin.x > footprintMax.x || boxMax.z < footprintMin.y || boxMin.z > footprintMax.y)
			return;
		if (boxMax.y <= previousBottom + 0.02f)
			support = (std::max)(support, boxMax.y);
	};

	for (const auto& surface : m_surfaces)
		consider(surface.min, surface.max);

	// Other items and packages can be stacked on.
	if (m_steppingBodies)
	{
		if (!m_supportBoxesReady)
		{
			m_supportBoxes.clear();
			ForEachSupport([&](const Object* object, const ShopBox& box) { m_supportBoxes.push_back({ object, box }); });
			m_supportBoxesReady = true;
		}
		for (const auto& [object, box] : m_supportBoxes)
			if (object != self)
				consider(box.min, box.max);
	}
	else
		ForEachSupport([&](const Object* object, const ShopBox& box) {
			if (object != self)
				consider(box.min, box.max);
		});
	return support;
}

template <typename Visit>
void ShopWorld::ForEachSupport(Visit&& visit) const
{
	for (const auto& [ref, visual] : m_items)
	{
		if (visual.place == ItemPlace::HAND || visual.place == ItemPlace::PACKAGED || visual.place == ItemPlace::INVENTORY || visual.place == ItemPlace::CARRIED)
			continue;
		if (visual.place == ItemPlace::WORLD && !visual.body.resting)
			continue;
		if (ref.IsTool() && visual.place == ItemPlace::STORAGE)
			continue;
		visit(visual.object.get(), WorldBounds(visual.object));
	}
	for (const auto& [id, package] : m_packages)
		if (package.body.resting)
			visit(package.object.get(), WorldBounds(package.object));
}

void ShopWorld::StepBody(const pObject& object, Body& body, float deltaTime)
{
	// Something may have been taken out from underneath, but only if anything moved this frame.
	if (body.resting && !m_recheckSupport)
		return;

	auto bounds = WorldBounds(object);
	if (body.resting)
	{
		if (bounds.min.y - SupportHeight(bounds, bounds.min.y, object.get()) < 0.005f)
			return;
		body.resting = false;
		// It was holding something up, maybe.
		m_supportChanged = true;
	}

	float previousBottom = bounds.min.y;
	body.velocity.y -= GRAVITY * deltaTime;
	glm::vec3 position = object->GetPosition() + body.velocity * deltaTime;

	// Walls stop it (unless it started out overlapping one), and it stays on the lot.
	glm::vec3 move = body.velocity * deltaTime;
	ShopBox moved = { bounds.min + glm::vec3(move.x, 0.f, move.z), bounds.max + glm::vec3(move.x, 0.f, move.z) };
	if (InsideWalls(moved) && !InsideWalls(bounds))
	{
		position.x -= move.x;
		position.z -= move.z;
		body.velocity.x = body.velocity.z = 0.f;
	}

	FloorObstacle lot = GetRoomBounds();
	glm::vec3 halfSize = (bounds.max - bounds.min) * 0.5f;
	glm::vec3 center = (bounds.min + bounds.max) * 0.5f + (position - object->GetPosition());
	glm::vec2 clampedCenter = glm::clamp(glm::vec2(center.x, center.z), lot.min + glm::vec2(halfSize.x, halfSize.z), lot.max - glm::vec2(halfSize.x, halfSize.z));
	if (clampedCenter != glm::vec2(center.x, center.z))
	{
		position.x += clampedCenter.x - center.x;
		position.z += clampedCenter.y - center.z;
		body.velocity.x = body.velocity.z = 0.f;
	}

	object->SetPosition(position);
	bounds = WorldBounds(object);

	float support = SupportHeight(bounds, previousBottom, object.get());
	if (bounds.min.y <= support)
	{
		object->SetPosition(position + glm::vec3(0.f, support - bounds.min.y, 0.f));
		// Heard as it hits: a thump for a hard landing, a tap for a gentle one.
		if (body.velocity.y < -2.5f)
			Cue(Sound::DROP, object->GetPosition(), (std::min)(1.f, -body.velocity.y / 6.f));
		else if (body.velocity.y < -0.8f)
			Cue(Sound::SET_DOWN, object->GetPosition(), 0.6f);
		// A hard landing gets a small bounce; anything gentler just settles.
		if (body.velocity.y < -2.5f)
		{
			body.velocity.y *= -0.2f;
			body.velocity.x *= 0.4f;
			body.velocity.z *= 0.4f;
		}
		else
		{
			body.velocity = {};
			body.resting = true;
		}
	}
}

std::optional<glm::vec3> ShopWorld::AimPoint(const FirstPersonController& player) const
{
	glm::vec3 origin = player.GetEyePosition();
	glm::vec3 direction = player.GetForward();
	const Object* held = GetHeld() ? GetHeld()->object.get() : nullptr;

	std::optional<float> best;
	auto consider = [&](const glm::vec3& boxMin, const glm::vec3& boxMax) {
		auto t = RayBox(origin, direction, boxMin, boxMax);
		if (t && *t <= PLACE_DISTANCE && (!best || *t < *best))
			best = t;
	};

	for (const auto& surface : m_surfaces)
		consider(surface.min, surface.max);
	for (const auto& [ref, visual] : m_items)
		if (visual.object.get() != held && visual.object->IsVisible() && visual.place != ItemPlace::STAND && !(ref.IsTool() && visual.place == ItemPlace::STORAGE))
		{
			auto box = WorldBounds(visual.object);
			consider(box.min, box.max);
		}
	for (const auto& [id, package] : m_packages)
	{
		auto box = WorldBounds(package.object);
		consider(box.min, box.max);
	}
	if (direction.y < -1e-4f)
	{
		float t = -origin.y / direction.y;
		if (t <= PLACE_DISTANCE && (!best || t < *best))
			best = t;
	}

	// Aiming at a wall: there's nowhere to put it (and nothing should end up outside through it).
	if (auto wall = WallHit(origin, direction); wall && (!best || *wall < *best))
		return std::nullopt;

	if (!best)
		return std::nullopt;
	return origin + direction * *best;
}

const ItemVisual* ShopWorld::RemovalHost(const Workshop& workshop, const ItemVisual& visual) const
{
	// On the car, a spring comes off with its strut and a tire with its wheel.
	if (visual.place == ItemPlace::CAR && visual.part && workshop.GetCar())
	{
		const auto& car = workshop.GetCar()->assembly;
		if (auto at = car.Find(visual.part.get()))
			if (auto carrier = CarAssembly::CarriedBy(at->first))
				if (auto part = car.Get(*carrier, at->second))
					if (const ItemVisual* host = Find(ItemRef::Part(part->GetId())))
						return host;
		return &visual;
	}
	if (visual.place != ItemPlace::STAND || !visual.part)
		return &visual;
	const auto& assembly = workshop.GetAssembly(visual.engineIndex);
	const ItemVisual* host = &visual;
	auto slot = FindInstalledSlot(assembly, visual.part);
	while (slot)
	{
		auto carrier = EngineAssembly::CarriedBy(slot->first);
		int carrierIndex = EngineAssembly::CarrierIndex(slot->first, slot->second);
		auto part = carrier ? assembly.Get(*carrier, carrierIndex) : nullptr;
		const ItemVisual* carrierVisual = part ? Find(ItemRef::Part(part->GetId())) : nullptr;
		if (!carrierVisual)
			break;
		host = carrierVisual;
		slot = std::pair{ *carrier, carrierIndex };
	}
	return host;
}

const ItemVisual* ShopWorld::FastenerNearCrosshair(int engine, const FirstPersonController& player, float hitDistance) const
{
	glm::vec3 eye = player.GetEyePosition(), forward = player.GetForward();
	const ItemVisual* best = nullptr;
	float bestOff = 0.f;
	for (const auto& [ref, visual] : m_items)
	{
		if (visual.place != ItemPlace::STAND || visual.engineIndex != engine || !visual.part || !visual.object->IsVisible()
			|| !PartCatalog::Get(visual.part->GetType()).fastener.IsFastener())
			continue;
		// The middle of its head (or nut): its origin is at the face it bears on, the head standing off it.
		float head = ModelOf(visual.object)->GetBoundsMax().y * 0.5f;
		glm::vec3 point = visual.object->GetPosition() + visual.object->GetRotation() * glm::vec3(0.f, head, 0.f);
		float along = glm::dot(point - eye, forward);
		if (along <= 0.f || along > hitDistance + 0.04f)
			continue;
		float off = glm::length(point - (eye + forward * along));
		if (off < (std::max)(0.012f, along * 0.02f) && (!best || off < bestOff))
		{
			best = &visual;
			bestOff = off;
		}
	}
	return best;
}

void ShopWorld::UpdateHover(const Workshop& workshop, const FirstPersonController& player)
{
	auto held = GetHeld();
	const Object* heldObject = held ? held->object.get() : nullptr;
	bool holdingPart = held && held->part;

	// Stations' boxes enclose the items resting on them (a shelf, a machine table, the stand), so
	// a plain closest hit would always find the station. Items and packages are tried first, with or
	// without something in hand: what you're carrying goes on the hotbar to free the hand. Items are
	// tested by their triangles, so the one really in front wins; stations by their boxes, so aiming
	// at the open space of a shelf or bench still means that station.
	auto objectManager = ObjectManager::Get();
	auto raycast = [&](bool stations) {
		return objectManager->Raycast(player.GetEyePosition(), player.GetForward(), INTERACT_DISTANCE, [&](const pObject& object) {
			auto target = m_targets.find(object.get());
			if (target == m_targets.end() || object.get() == heldObject)
				return false;
			return (target->second.kind == TargetKind::STATION) == stations;
		}, !stations);
	};

	// Carrying a part to an engine: it goes where you're looking, so aiming at an empty position
	// (even into the gap where a crank or a bearing goes) picks it, whatever the ray hits.
	std::optional<size_t> fitStand;
	if (holdingPart)
	{
		PROFILE_SCOPE("world: fit targets");
		fitStand = FindFitTarget(workshop, player, *held);
		// Or the project car, the same way.
		if (!fitStand)
			fitStand = FindCarFitTarget(workshop, player, *held);
	}

	auto hit = raycast(false);
	if (!hit)
		hit = raycast(true);
	if (!hit && !fitStand)
		return;
	// Nothing on the far side of a wall, through a window, is in reach.
	if (auto wall = WallHit(player.GetEyePosition(), player.GetForward()); hit && !fitStand && wall && *wall < hit->distance
		&& m_targets.at(hit->object.get()).stationIndex != m_doorStation && m_targets.at(hit->object.get()).stationIndex != m_bayDoorStation)
		return;

	// Holding a nut or bolt and looking right at a loose one (finger tight on the engine, or lying
	// about): that's the one you're after, not the empty hole next to it. It's picked up.
	bool looseHardware = false;
	if (holdingPart && hit && PartCatalog::Get(held->part->GetType()).fastener.IsFastener())
		if (const auto& target = m_targets.at(hit->object.get()); target.kind == TargetKind::ITEM)
			if (const auto* visual = Find(target.item); visual && visual->part && PartCatalog::Get(visual->part->GetType()).fastener.IsFastener())
				looseHardware = visual->place != ItemPlace::MACHINE
					&& ((visual->place != ItemPlace::STAND && visual->place != ItemPlace::CAR) || visual->part->GetTightness() == FastenerTightness::LOOSE);

	m_hover = fitStand && !looseHardware ? Target{ TargetKind::STATION, {}, 0, *fitStand } : m_targets.at(hit->object.get());
	// Aimed at a nut or bolt on the engine, it's that, though the ray went on down its hole into the part it holds.
	if (m_hover->kind == TargetKind::ITEM)
		if (const ItemVisual* aimed = Find(m_hover->item); aimed && aimed->place == ItemPlace::STAND && aimed->part
			&& !PartCatalog::Get(aimed->part->GetType()).fastener.IsFastener())
			if (const ItemVisual* fastener = FastenerNearCrosshair(aimed->engineIndex, player, hit->distance))
				m_hover = m_targets.at(fastener->object.get());
	if (looseHardware)
	{
		m_fitTarget.reset();
		m_carFitTarget.reset();
	}

	// Carrying a part and looking at one fitted to an engine or set up in a machine means "put it
	// there". One on a shelf, the bench or the floor is picked up, and so is loose hardware.
	if (holdingPart && !looseHardware && m_hover->kind == TargetKind::ITEM)
	{
		const auto& onto = *Find(m_hover->item);
		for (size_t i = 0; i < m_stations.size(); i++)
		{
			const auto& station = m_stations[i];
			bool holdsIt = (onto.place == ItemPlace::STAND && station.type == StationType::ENGINE_STAND && station.engineIndex == onto.engineIndex)
				|| (onto.place == ItemPlace::MACHINE && station.type == StationType::MACHINE && station.machineIndex == onto.machineIndex)
				|| (onto.place == ItemPlace::CAR && station.type == StationType::CAR);
			if (holdsIt)
				m_hover = Target{ TargetKind::STATION, {}, 0, i };
		}
	}



	if (m_hover->kind == TargetKind::PACKAGE)
	{
		const auto& package = m_packages.at(m_hover->packageId);
		package.object->SetEmissive(PART_GLOW);
		m_prompt = std::format("[E] Pick up   [F] Open   {}", package.label);
		return;
	}

	if (m_hover->kind == TargetKind::ITEM)
	{
		const auto& visual = *Find(m_hover->item);
		visual.object->SetEmissive(PART_GLOW);
		FindAimedBore(workshop, visual, player);
		FindAimedHeadSpot(workshop, visual, player);
		std::string name = visual.part ? PartLabel(*visual.part) : ToolCatalog::Of(visual.ref.id).name;

		if (visual.place == ItemPlace::CAR)
		{
			// A spring or tire comes off with the strut or wheel it's on: that's what's taken off, lit up with it.
			const auto* car = workshop.GetCar();
			const ItemVisual& removal = *RemovalHost(workshop, visual);
			if (&removal != &visual)
				removal.object->SetEmissive(PART_GLOW);
			auto at = car ? car->assembly.Find(removal.part.get()) : std::nullopt;
			if (at && at->first == CarSlot::BODY_SHELL)
				m_prompt = "[E] Work on the LE86";
			else if (at)
			{
				std::string position = car->assembly.PositionName(at->first, at->second);
				std::optional<std::string> blocked = car->assembly.RemovalBlockedBy(at->first, at->second);
				if (blocked)
					blocked = "the " + *blocked + " has to come off first";
				if (!blocked && PartWayDoneUp(*removal.part))
					blocked = "part way done up: hold [RMB] with a spanner on it to undo it";
				if (!blocked && CarAssembly::IsFastenerSlot(at->first) && removal.part->GetTightness() != FastenerTightness::LOOSE)
					blocked = std::format("done up: undo it with a {} mm spanner first", PartCatalog::Get(removal.part->GetType()).fastener.headMm);
				m_prompt = blocked ? std::format("{}: {}", position, *blocked) : std::format("[E] Take the {} off the car", position);
			}
		}
		else if (auto slot = visual.place == ItemPlace::STAND ? FindInstalledSlot(workshop.GetAssembly(visual.engineIndex), visual.part) : std::nullopt;
			visual.place == ItemPlace::STAND && workshop.EngineWorkProblem(visual.engineIndex, slot ? slot->first : AssemblySlot::COUNT))
		{
			// Out of reach where the engine is.
			bool flywheel = slot && Workshop::AtFlywheel(slot->first);
			m_prompt = visual.engineIndex == m_engineOnDyno ? std::format("{}: on the dyno", name)
				: visual.engineIndex == m_engineOnHoist ? std::format("{}: hanging from the hoist (only the flywheel comes off there)", name)
				: visual.engineIndex == m_engineInCar ? (flywheel ? std::format("{}: the clutch and gearbox are in the way", name)
					: std::format("{}: in the car (lift the engine out with the hoist to work on it)", name))
				: std::format("{}: no room on the stand (hang the engine from the hoist to get at the flywheel)", name);
		}
		else if (visual.place == ItemPlace::STAND)
		{
			const auto& assembly = workshop.GetAssembly(visual.engineIndex);
			// A piston (or its ring, or a shell) comes off with the rod or cap it's fitted to: that's what's taken off,
			// lit up with it.
			const ItemVisual& removal = *RemovalHost(workshop, visual);
			std::string removalName = &removal == &visual ? name : PartLabel(*removal.part);
			if (&removal != &visual)
				removal.object->SetEmissive(PART_GLOW);
			auto slot = FindInstalledSlot(assembly, removal.part);
			auto blocked = slot ? assembly.RemovalBlockedBy(slot->first, slot->second) : std::nullopt;
			if (!blocked && PartWayDoneUp(*removal.part))
				blocked = "part way done up: hold [RMB] with a spanner on it to undo it";
			m_prompt = blocked ? std::format("{}: {}", removalName, *blocked)
				: &removal == &visual ? std::format("[E] Take {} off the engine", name)
				: std::format("[E] Take {} off the engine, with the {}", removalName, visual.part->GetName());
			m_secondaryPrompt = "[R] Turn it over   [Z]/[X] Roll it on the stand";
			// A ring on its own in a bore, to be gapped: pushed down square with a piston, then read with the feelers.
			if (slot && slot->first == AssemblySlot::PISTON_RING)
				if (auto rings = workshop.RingsInBore(visual.engineIndex); rings.contains(slot->second))
				{
					if (RingToSquare(workshop, visual))
						m_prompt = std::format("[E] Push the ring in bore {} down square with the piston", slot->second + 1);
					else
						m_secondaryPrompt = rings.at(slot->second) ? "Squared in the bore: the feeler gauge reads its gap"
							: "Push it down square with a piston (hold one, [E] on the ring) to gap it";
				}
		}
		else
		{
			// A bearing shell in hand goes into a loose rod or cap, a piston onto a rod; R takes one off again.
			auto fitted = visual.part && visual.place != ItemPlace::STAND && visual.part->GetType() != PartType::BLOCK ? TakeOffWhat(workshop, *visual.part) : nullptr;
			auto bore = AimedBore(visual);
			if (CanFitOnto(workshop, visual) && visual.part->GetType() == PartType::BLOCK)
				m_boreFitTarget = glm::vec3(visual.object->GetModelMatrix() * glm::vec4(EngineLayout::BoreX(*bore), RING_IN_BORE_Y, 0.f, 1.f));
			if (CanFitOnto(workshop, visual) && visual.part->GetType() == PartType::BLOCK)
				m_prompt = held->part->GetType() == PartType::PISTON ? std::format("[E] Push the ring in bore {} down square with the piston", *bore + 1)
					: std::format("[E] Set the {} in bore {} of the {} on its own, to gap it", held->part->GetName(), *bore + 1, name);
			else if (CanCompressAt(visual))
				m_prompt = std::format("[E]/[LMB] Clamp the Valve Spring Compressor over the {} and squeeze its spring down",
					workshop.GetAssembly().PositionName(AssemblySlot::VALVE_SPRING, m_aimedHeadSpot->index));
			else if (CanFitOnto(workshop, visual) && TakesLooseFittings(visual.part->GetType()))
				m_prompt = std::format("[E] Fit the {} {} the {}: {}", held->part->GetName(), m_aimedHeadSpot->slot == AssemblySlot::ROCKER_ARM ? "onto" : "into", name,
					workshop.LooseFittingName(m_aimedHeadSpot->slot, m_aimedHeadSpot->index));
			else if (CanFitOnto(workshop, visual))
				m_prompt = std::format("[E] Fit the {} {} the {}", held->part->GetName(), held->part->GetType() == PartType::PISTON || held->part->GetType() == PartType::PISTON_RING ? "onto" : "into",
					held->part->GetType() == PartType::PISTON_RING && visual.part->GetType() == PartType::CONNECTING_ROD ? "piston on the " + name : name);
			else if (auto ring = RingInAimedBore(workshop, visual))
			{
				m_prompt = std::format("[E] Take the {} out of bore {}", ring->GetName(), *bore + 1);
				// The ring lights up, not the block it's in: tinted gold and glowing, as it's thin and down in a bore where a plain
				// glow barely shows. (LayoutItems puts its own tint back each frame.)
				if (const ItemVisual* ringVisual = Find(ItemRef::Part(ring->GetId())))
				{
					visual.object->SetEmissive(0.f);
					ringVisual->object->SetTint(glm::vec4(1.f, 0.72f, 0.25f, 1.f));
					ringVisual->object->SetEmissive(PART_GLOW * 2.f);
				}
			}
			else
				m_prompt = std::format("[E] Pick up {}", name);
			auto piston = visual.part ? workshop.PistonOn(*visual.part) : nullptr;
			if (piston && !workshop.PinIn(*piston))
				m_prompt += "   (its piston needs its pin pressed in)";
			// The valvetrain in hand over a loose head: where it can go, or what has to go in first.
			bool headPart = held && held->part && visual.part && visual.part->GetType() == PartType::CYLINDER_HEAD
				&& EngineAssembly::InHead(EngineAssembly::SlotFor(held->part->GetType()));
			if (headPart && !CanFitOnto(workshop, visual) && visual.place != ItemPlace::STAND && visual.place != ItemPlace::CAR)
			{
				// A valve goes in from the deck side, the rest from the top: which face is towards the player.
				glm::mat4 transform = visual.object->GetModelMatrix();
				glm::vec3 up = glm::vec3(transform * glm::vec4(0.f, 1.f, 0.f, 0.f));
				glm::vec3 centre = glm::vec3(transform * glm::vec4(0.f, EngineLayout::HEAD_FLOOR_Y * 0.5f, 0.f, 1.f));
				bool deckSeen = glm::dot(player.GetEyePosition() - centre, up) < 0.f;
				bool valve = held->part->GetType() == PartType::INTAKE_VALVE || held->part->GetType() == PartType::EXHAUST_VALVE;
				const auto& clamp = workshop.GetValveCompressor();
				bool retainer = held->part->GetType() == PartType::VALVE_RETAINER && !(clamp && clamp->head == visual.part->GetId());
				m_secondaryPrompt = retainer && !deckSeen ? "A retainer goes on with its spring squeezed: put the Valve Spring Compressor over the valve first"
					: valve && !deckSeen ? "A valve goes in from underneath: turn the head over (in hand, hold [Q] and move the mouse to tip it)"
					: !valve && deckSeen ? "The seals, springs and retainers go on from the top: turn the head right way up"
					: "Aim at a green dot: a valve from underneath, then its seal, spring and retainer from the top";
			}
			if (fitted && TakesLooseFittings(visual.part->GetType()))
			{
				const HeadFitting* at = workshop.LooseHeadFittingOf(*fitted);
				m_secondaryPrompt = std::format("[R] Take the {} out ({})", fitted->GetName(), at ? workshop.LooseFittingName(at->slot, at->index) : "");
			}
			else if (fitted)
				m_secondaryPrompt = std::format("[R] Take the {} {}", fitted->GetName(), fitted->GetType() == PartType::ROD_BEARING || fitted->GetType() == PartType::ROD_BEARING_LOWER || fitted->GetType() == PartType::MAIN_BEARING_LOWER ? "out" : "off");
		}

		if (visual.part && visual.part->GetCondition() == PartCondition::DAMAGED)
			m_prompt += std::format("  (damaged: {})", visual.part->GetDamageReason());
		return;
	}

	const auto& station = m_stations[m_hover->stationIndex];
	station.object->SetEmissive(STATION_GLOW);
	std::string moveHint = station.movable ? "   [F] Move" : "";

	switch (station.type)
	{
		case StationType::WORKBENCH:
			m_prompt = holdingPart ? std::format("[RMB] Set the {} down on the workbench", held->part->GetName()) : "[E] Work at the bench" + moveHint;
			break;
		case StationType::MACHINE:
		{
			const auto& machine = MachineCatalog::All()[station.machineIndex];
			if (!holdingPart)
				m_prompt = std::format("[E] Use the {}{}", machine.name, moveHint);
			else if (MachineCanWork(machine, *held->part))
				m_prompt = std::format("[E] Set the {} up on the {}", held->part->GetName(), machine.name);
			else
				m_prompt = std::format("The {} can't work on a {}", machine.name, PartTypeName(held->part->GetType()));
			break;
		}
		case StationType::ENGINE_STAND:
			if (!holdingPart)
			{
				m_prompt = std::format("[E] {} (engine {}){}", station.name, station.engineIndex + 1, moveHint);
				m_secondaryPrompt = "[R] Turn it over   [Z]/[X] Roll it on the stand";
			}
			else
			{
				const auto& assembly = workshop.GetAssembly(station.engineIndex);
				if (auto why = ShellGoesInFirst(held->part->GetType()); why && !(m_fitTarget && m_fitTarget->engine == station.engineIndex && m_fitTarget->slot == AssemblySlot::PISTON_RING))
					m_prompt = *why;
				else if (m_fitTarget && m_fitTarget->engine == station.engineIndex)
				{
					auto position = assembly.PositionName(m_fitTarget->slot, m_fitTarget->index);
					if (held->part->GetType() == PartType::PISTON)
						m_prompt = std::format("[E] Push the ring in bore {} down square with the piston", m_fitTarget->index + 1);
					else if (auto missing = FitMissing(workshop, station.engineIndex, m_fitTarget->slot, m_fitTarget->index))
						m_prompt = std::format("{}: not yet. {}", position, *missing);
					else if (m_fitTarget->slot == AssemblySlot::PISTON_RING)
						m_prompt = std::format("[E] Set the {} in the top of bore {} on its own, to gap it", held->part->GetName(), m_fitTarget->index + 1);
					else
					{
						m_prompt = std::format("[E] Fit the {} as {}", held->part->GetName(), position);
						// Nuts and bolts go in finger tight, then done up: how tight is in the service manual.
						const auto& fastener = PartCatalog::Get(held->part->GetType()).fastener;
						if (EngineAssembly::Holes(m_fitTarget->slot) && fastener.IsFastener())
							m_secondaryPrompt = std::format("Goes in finger tight, then a {} mm socket or wrench (the service manual has the torque)",
								fastener.headMm);
					}
				}
				else if (FindFitSpot(assembly, held->part->GetType()).anyEmpty || FindFitSpot(assembly, held->part->GetType()).slot)
					m_prompt = std::format("Aim at the marked spot where the {} goes", PartTypeName(held->part->GetType()));
				else
					m_prompt = std::format("No free place on engine {} for a {}", station.engineIndex + 1, PartTypeName(held->part->GetType()));
			}
			break;
		case StationType::DYNO:
			m_prompt = "[E] Dyno cell" + moveHint;
			break;
		case StationType::COMPUTER:
			m_prompt = "[E] Parts computer: buy parts, tools, machines and engine stands" + moveHint;
			break;
		case StationType::TOOL_BOARD:
			m_prompt = held && held->ref.IsTool() ? "[RMB] Hang the tool here" : station.name + moveHint;
			break;
		case StationType::FURNITURE:
			if (IsWorkbench(station))
			{
				m_prompt = holdingPart ? std::format("[RMB] Set the {} down on the bench", held->part->GetName()) : "[E] Work at the bench" + moveHint;
				break;
			}
			m_prompt = station.name + moveHint;
			break;
		case StationType::SHELF:
		case StationType::PALLET_RACK:
			m_prompt = station.name + moveHint;
			break;
		case StationType::LIFT:
			m_prompt = std::format("[E] {} the lift{}", m_liftRaised ? "Lower" : "Raise", moveHint);
			break;
		case StationType::HOIST:
		{
			int hanging = workshop.GetEngineOnHoist();
			auto target = HoistTarget(workshop);
			auto where = [&](int at) { return at == DYNO_TARGET ? std::string("the dyno") : at == CAR_TARGET ? std::string("into the car")
				: std::format("stand {}", at + 1); };
			if (m_swing.progress < 1.f)
				m_prompt = station.name;
			else if (hanging >= 0 && target && *target == CAR_TARGET)
				m_prompt = std::format("[E] Lower engine {} into the car's bay{}", hanging + 1, moveHint);
			else if (hanging >= 0 && target)
				m_prompt = std::format("[E] Lower engine {} onto {}{}", hanging + 1, where(*target), moveHint);
			else if (target && *target == CAR_TARGET)
				m_prompt = std::format("[E] Lift engine {} out of the car{}", workshop.GetEngineInCar() + 1, moveHint);
			else if (target)
				m_prompt = std::format("[E] Lift engine {} off {}{}", (*target == DYNO_TARGET ? workshop.GetEngineOnDyno() : *target) + 1,
					*target == DYNO_TARGET ? "the dyno" : "its stand", moveHint);
			else
			{
				m_prompt = (hanging >= 0 ? std::format("Engine {} hangs from the hoist", hanging + 1) : station.name) + moveHint;
				m_secondaryPrompt = hanging >= 0 ? "Push it up to a free engine stand or the dyno to lower the engine onto it"
					: "Push it up to an engine stand or the dyno to lift the engine off";
			}
			break;
		}
		case StationType::DOOR:
			m_prompt = DoorOpenFor(station) ? "[E] Pull the roll-up door down" : "[E] Roll the door up";
			break;
		case StationType::CAR:
			if (holdingPart && m_carFitTarget)
			{
				m_prompt = CarFitPrompt(workshop, *held);
				break;
			}
			m_prompt = holdingPart ? std::format("The {} doesn't go on the car (or there's nowhere left for it)", held->part->GetName())
				: "[E] Work on the LE86";
			m_secondaryPrompt = workshop.GetEngineInCar() >= 0 ? "Roll the engine hoist out to it to lift the engine out"
				: "Roll the engine hoist out to it with an engine on the hook to lower one in";
			break;
	}
}

std::optional<UIRequest> ShopWorld::Interact(Workshop& workshop)
{
	if (!m_hover)
		return std::nullopt;

	switch (m_hover->kind)
	{
		case TargetKind::PACKAGE:
			PickUpPackage(m_hover->packageId);
			return std::nullopt;
		case TargetKind::ITEM:
			if (auto cylinder = RingToSquare(workshop, *Find(m_hover->item)))
			{
				const auto& ring = *Find(m_hover->item);
				workshop.SetActiveEngine(ring.engineIndex);
				if (workshop.SquareRing(*cylinder))
					Cue(Sound::FIT_PART, ring.object->GetPosition());
			}
			else if (CanCompressAt(*Find(m_hover->item)))
				CompressAt(workshop, *Find(m_hover->item));
			else if (CanFitOnto(workshop, *Find(m_hover->item)))
				FitOnto(workshop, *Find(m_hover->item));
			// A ring in the bore of a loose block aimed into comes out, as on the stand.
			else if (RingInAimedBore(workshop, *Find(m_hover->item)))
				TakeOff(workshop, *Find(m_hover->item));
			else
				PickUp(workshop, *Find(RemovalHost(workshop, *Find(m_hover->item))->ref));
			return std::nullopt;
		case TargetKind::STATION:
			return UseStation(workshop, m_stations[m_hover->stationIndex]);
	}
	return std::nullopt;
}

std::optional<UIRequest> ShopWorld::UseStation(Workshop& workshop, const Station& station)
{
	auto held = GetHeld();
	ItemVisual* heldPart = held && held->part ? held : nullptr;

	switch (station.type)
	{
		case StationType::WORKBENCH:
		{
			// Parts only get onto the bench by being set down or dropped on it.
			if (heldPart)
			{
				Toast("Right-click to set it down on the bench, or G to drop it.");
				return std::nullopt;
			}
			m_activeBench = SIZE_MAX;
			return UIRequest{ WorkshopTab::BENCH };
		}
		case StationType::MACHINE:
		{
			const auto& machine = MachineCatalog::All()[station.machineIndex];
			if (heldPart)
			{
				if (!MachineCanWork(machine, *heldPart->part))
				{
					Toast(std::format("The {} can't work on a {}.", machine.name, PartTypeName(heldPart->part->GetType())));
					return std::nullopt;
				}
				SetPlace(*heldPart, ItemPlace::MACHINE, station.machineIndex);
				return UIRequest{ WorkshopTab::MACHINE, heldPart->ref.id, station.machineIndex };
			}

			if (auto loaded = FindItemAt(ItemPlace::MACHINE, station.machineIndex))
				return UIRequest{ WorkshopTab::MACHINE, loaded->ref.id, station.machineIndex };
			Toast(std::format("Bring a part over to set up on the {}.", machine.name));
			return std::nullopt;
		}
		case StationType::ENGINE_STAND:
		{
			// Working at a stand makes its engine the one the workshop screens and dyno act on.
			int engine = station.engineIndex;
			workshop.SetActiveEngine(engine);
			if (!heldPart)
				return UIRequest{ WorkshopTab::ENGINE_STAND };

			// It goes exactly where you're aiming.
			const auto& assembly = workshop.GetAssembly(engine);
			if (auto why = ShellGoesInFirst(heldPart->part->GetType()); why && !(m_fitTarget && m_fitTarget->engine == engine && m_fitTarget->slot == AssemblySlot::PISTON_RING))
			{
				Toast(*why + ".");
				return std::nullopt;
			}
			if (!m_fitTarget || m_fitTarget->engine != engine)
			{
				auto spot = FindFitSpot(assembly, heldPart->part->GetType());
				Toast(spot.slot || spot.anyEmpty ? std::format("Aim at the marked spot where the {} goes.", PartTypeName(heldPart->part->GetType()))
					: std::format("No free place on engine {} for a {}.", engine + 1, PartTypeName(heldPart->part->GetType())));
				return std::nullopt;
			}
			// A piston in hand pushes a ring set in the bore on its own down square.
			if (heldPart->part->GetType() == PartType::PISTON)
			{
				workshop.SetActiveEngine(engine);
				if (workshop.SquareRing(m_fitTarget->index))
					Cue(Sound::FIT_PART, m_fitTarget->position);
				return std::nullopt;
			}
			if (auto missing = FitMissing(workshop, engine, m_fitTarget->slot, m_fitTarget->index))
			{
				Toast(std::format("Not yet: {}", *missing));
				return std::nullopt;
			}
			auto slot = m_fitTarget->slot;
			if (!workshop.InstallPart(slot, m_fitTarget->index, heldPart->part))
				return std::nullopt;
			Cue(Sound::FIT_PART, m_fitTarget->position);
			SetPlace(*heldPart, ItemPlace::STAND, engine);

			return std::nullopt;
		}
		case StationType::DOOR:
			DoorOpenFor(station) = !DoorOpenFor(station);
			return std::nullopt;
		case StationType::DYNO:			return UIRequest{ WorkshopTab::DYNO };
		// Everything is bought at the parts computer, and only there.
		case StationType::COMPUTER:		return UIRequest{ WorkshopTab::STORE, 0, -1, StoreCategory::PARTS };
		case StationType::TOOL_BOARD:
			// A tool in hand is hung with [RMB], where it's aimed (see Update); E does nothing with it here.
			if (held && held->ref.IsTool())
				return std::nullopt;
			Toast("Measuring tools are bought at the parts computer.");
			return std::nullopt;
		case StationType::LIFT:
			m_liftRaised = !m_liftRaised;
			return std::nullopt;
		// The hoist lifts the engine off whatever's in its reach, or lowers the one on it onto it.
		case StationType::HOIST:
		{
			if (m_swing.progress < 1.f)
				return std::nullopt;
			int hanging = workshop.GetEngineOnHoist();
			auto target = HoistTarget(workshop);
			if (!target)
			{
				Toast(hanging >= 0 ? "Push the hoist up to a free engine stand or the dyno to lower the engine onto it."
					: "Push the hoist up to an engine stand or the dyno to lift its engine off.");
				return std::nullopt;
			}
			if (*target == CAR_TARGET)
			{
				if (hanging < 0)
					workshop.HoistEngineFromCar();
				else
					workshop.LowerEngineIntoCar();
				return std::nullopt;
			}
			if (hanging < 0)
			{
				int engine = *target == DYNO_TARGET ? workshop.GetEngineOnDyno() : *target;
				if (engine == m_dynoEngine && m_engineRpm > 0.f)
				{
					Toast("Not while it's running on the dyno.");
					return std::nullopt;
				}
				workshop.HoistEngine(engine);
			}
			else if (*target == DYNO_TARGET)
			{
				if (!workshop.GetAssembly(hanging).IsComplete())
					Toast(std::format("Engine {} isn't finished: only a complete engine goes on the dyno.", hanging + 1));
				else
					workshop.LowerEngineOntoDyno();
			}
			else
				workshop.LowerEngineOntoStand(*target);
			return std::nullopt;
		}
		// A bought workbench reads what's on it, like the shop's own.
		case StationType::FURNITURE:
			if (IsWorkbench(station))
			{
				if (heldPart)
				{
					Toast("Right-click to set it down on the bench, or G to drop it.");
					return std::nullopt;
				}
				m_activeBench = static_cast<size_t>(&station - m_stations.data());
				return UIRequest{ WorkshopTab::BENCH };
			}
			return std::nullopt;
		// Parts are set down on the shelf like on any other surface ([RMB]).
		case StationType::SHELF:
		case StationType::PALLET_RACK:
			return std::nullopt;
		case StationType::CAR:
			// With a part in hand it goes where it's aimed; otherwise the car's screen.
			if (heldPart)
			{
				FitToCar(workshop, *heldPart);
				return std::nullopt;
			}
			return UIRequest{ WorkshopTab::CAR };
	}
	return std::nullopt;
}

void ShopWorld::PickUpPackage(uint32_t packageId)
{
	// Onto the hotbar like anything else, and into your hands (both of them).
	auto slot = m_inventory.FreeHotbarSlot();
	if (!slot)
	{
		Toast("The hotbar's full: free a slot to carry the delivery.");
		return;
	}
	Cue(Sound::PICK_UP, m_packages.at(packageId).object->GetPosition(), 1.2f);
	m_inventory.PutInHotbar(*slot, ItemRef::Package(packageId));
	SelectHotbarSlot(*slot);
	m_packages.at(packageId).object->SetPickable(false);
	// Something moving with the camera every frame would make the lamps redraw their shadows every
	// frame; what you carry doesn't cast one.
	m_packages.at(packageId).object->SetCastsShadows(false);
	// Whatever was stacked on it drops.
	m_supportChanged = true;
}

bool ShopWorld::IsCarried(uint32_t packageId) const
{
	return m_inventory.HotbarSlotOf(ItemRef::Package(packageId)).has_value();
}

void ShopWorld::PoseCarriedPackages(const FirstPersonController& player)
{
	glm::vec3 forward = player.GetForward();
	forward.y = 0.f;
	forward = glm::length(forward) > 0.001f ? glm::normalize(forward) : glm::vec3(0.f, 0.f, -1.f);
	glm::quat rotation = Yaw(player.GetYaw());
	for (int slot = 0; slot < PlayerInventory::HOTBAR_SLOTS; slot++)
	{
		ItemRef ref = m_inventory.GetHotbar(slot);
		if (!ref.IsPackage())
			continue;
		auto it = m_packages.find(ref.id);
		if (it == m_packages.end())
		{
			// Opened or taken by someone else meanwhile.
			m_inventory.Remove(ref);
			SyncHand();
			continue;
		}
		auto& package = it->second;
		auto model = ModelOf(package.object);
		glm::vec3 center = (model->GetBoundsMin() + model->GetBoundsMax()) * 0.5f;
		float radius = glm::length(model->GetBoundsMax() - model->GetBoundsMin()) * 0.5f;
		bool inHand = ref.id == m_carriedPackage;
		// In hand: held out in front at waist height, kept level, turning with you. Put away: with
		// you, out of sight, so it's where you are if it's ever put down for you (leaving the garage).
		glm::vec3 holdPoint = inHand
			? player.GetEyePosition() + forward * (0.3f + radius * 0.8f) - glm::vec3(0.f, 0.7f + radius * 0.4f, 0.f)
			: player.GetFeetPosition() + glm::vec3(0.f, 1.f, 0.f);
		package.object->SetPosition(holdPoint - rotation * center);
		package.object->SetRotation(rotation);
		package.object->SetVisible(inHand);
		package.object->SetPickable(false);
		package.object->SetCastsShadows(false);
		package.body = {};
	}
}

void ShopWorld::ReleasePackage(uint32_t packageId, const glm::vec3& position, const glm::vec3& velocity)
{
	m_inventory.Remove(ItemRef::Package(packageId));
	SyncHand();
	auto it = m_packages.find(packageId);
	if (it == m_packages.end())
		return;
	auto& package = it->second;
	package.object->SetPosition(position);
	package.body.velocity = velocity;
	package.body.resting = false;
	package.object->SetVisible(true);
	package.object->SetPickable(true);
	package.object->SetCastsShadows(true);
	m_supportChanged = true;
}

void ShopWorld::UpdateCarriedPackage(const FirstPersonController& player, const InputManager& input)
{
	auto it = m_packages.find(m_carriedPackage);
	if (it == m_packages.end())
		return;
	auto& package = it->second;
	auto model = ModelOf(package.object);
	glm::vec3 center = (model->GetBoundsMin() + model->GetBoundsMax()) * 0.5f;
	glm::quat rotation = Yaw(player.GetYaw());

	m_secondaryPrompt = package.label;
	m_heldHint = "[F] Open   [RMB] Set down   [G] Drop";

	if (Controls::Pressed(input, Control::MOVE))
		OpenPackage(package, &player);
	else if (Controls::Pressed(input, Control::DROP))
		ReleasePackage(package.id, package.object->GetPosition(), player.GetForward() * 1.2f + glm::vec3(0.f, 0.5f, 0.f));
	else if (Controls::Pressed(input, Control::SET_DOWN))
	{
		if (auto point = AimPoint(player))
			ReleasePackage(package.id, *point - rotation * glm::vec3(center.x, model->GetBoundsMin().y - 0.03f, center.z), {});
		else
			Toast("Nowhere to put it there.");
	}
}

bool ShopWorld::PartWayDoneUp(const Part& part) const
{
	auto turned = m_fastenerTurns.find(part.GetId());
	return turned != m_fastenerTurns.end() && turned->second > 0.f && part.GetTightness() == FastenerTightness::LOOSE;
}

pPart ShopWorld::RingTarget(const Workshop& workshop, const Part& host) const
{
	// A ring goes onto a loose piston, or the piston on a loose rod.
	if (host.GetType() == PartType::PISTON)
		return workshop.FindPart(host.GetId());
	return host.GetType() == PartType::CONNECTING_ROD ? workshop.PistonOn(host) : nullptr;
}

std::optional<std::string> ShopWorld::FitMissing(const Workshop& workshop, int engine, AssemblySlot slot, int index) const
{
	const auto& assembly = workshop.GetAssembly(engine);
	// Out of reach where the engine is (the flywheel on a stand, most things on the hoist or in the car).
	if (auto problem = workshop.EngineWorkProblem(engine, slot))
		return problem;
	if (slot == AssemblySlot::PISTON_RING && !assembly.Get(AssemblySlot::PISTON, index))
		return workshop.RingInBoreBlockedBy(engine, index);
	return assembly.MissingPrerequisite(slot, index);
}

std::optional<int> ShopWorld::RingToSquare(const Workshop& workshop, const ItemVisual& ring) const
{
	auto held = GetHeld();
	if (!held || !held->part || held->part->GetType() != PartType::PISTON || ring.place != ItemPlace::STAND || !ring.part
		|| ring.part->GetType() != PartType::PISTON_RING)
		return std::nullopt;
	auto slot = FindInstalledSlot(workshop.GetAssembly(ring.engineIndex), ring.part);
	if (!slot)
		return std::nullopt;
	auto rings = workshop.RingsInBore(ring.engineIndex);
	auto it = rings.find(slot->second);
	return it != rings.end() && !it->second ? std::optional(slot->second) : std::nullopt;
}

std::optional<int> ShopWorld::AimedBore(const ItemVisual& block) const
{
	return block.part && m_aimedBore && m_aimedBore->first == block.part->GetId() ? std::optional(m_aimedBore->second) : std::nullopt;
}

pPart ShopWorld::RingInAimedBore(const Workshop& workshop, const ItemVisual& block) const
{
	auto bore = AimedBore(block);
	return bore ? workshop.RingInLooseBore(*block.part, *bore) : nullptr;
}

void ShopWorld::FindAimedBore(const Workshop& workshop, const ItemVisual& block, const FirstPersonController& player)
{
	// Only a block off the engine: on a stand, its bores are the assembly's.
	bool loose = block.place == ItemPlace::WORLD || block.place == ItemPlace::BENCH || block.place == ItemPlace::STORAGE;
	if (!loose || !block.part || block.part->GetType() != PartType::BLOCK)
		return;
	glm::vec3 eye = player.GetEyePosition(), forward = player.GetForward();
	glm::mat4 transform = block.object->GetModelMatrix();
	float best = 0.05f;
	for (int cylinder = 0; cylinder < workshop.GetSpec().cylinders; cylinder++)
	{
		glm::vec3 top = glm::vec3(transform * glm::vec4(EngineLayout::BoreX(cylinder), RING_IN_BORE_Y, 0.f, 1.f));
		float along = glm::dot(top - eye, forward);
		if (along <= 0.f)
			continue;
		float off = glm::length(top - (eye + forward * along));
		if (off < best)
		{
			best = off;
			m_aimedBore = { block.part->GetId(), cylinder };
		}
	}
}

ShopWorld::StandPose ShopWorld::HeadSpotPose(AssemblySlot slot, int index) const
{
	using namespace EngineLayout;
	// As on the engine with the valve shut, less the head's height off the block (its origin is its deck face).
	StandPose pose;
	switch (slot)
	{
		case AssemblySlot::INTAKE_VALVE:
		case AssemblySlot::EXHAUST_VALVE:
			pose.offset = { BoreX(index), VALVE_FACE_Y, ValveZ(slot == AssemblySlot::EXHAUST_VALVE) };
			break;
		case AssemblySlot::VALVE_SPRING:
			pose.offset = { BoreX(index / 2), HEAD_FLOOR_Y, ValveZ(index % 2 == 1) };
			pose.scale.y = SPRING_INSTALLED_HEIGHT / SPRING_FREE_HEIGHT;
			break;
		default:
		{
			auto mount = AncillaryMount(EngineAssembly::SlotPartType(slot), index);
			pose.offset = mount.offset - glm::vec3(0.f, HEAD_DECK_Y, 0.f);
			pose.rotation = mount.rotation;
			break;
		}
	}
	return pose;
}

void ShopWorld::FindAimedHeadSpot(const Workshop& workshop, const ItemVisual& head, const FirstPersonController& player)
{
	using namespace EngineLayout;
	bool loose = head.place == ItemPlace::WORLD || head.place == ItemPlace::BENCH || head.place == ItemPlace::STORAGE;
	if (!loose || !head.part || !TakesLooseFittings(head.part->GetType()))
		return;
	glm::vec3 eye = player.GetEyePosition(), forward = player.GetForward();
	glm::mat4 transform = head.object->GetModelMatrix();

	// A loose rocker shaft: a place for an arm over each cylinder along it; the nearest to the crosshair aimed at, to fit
	// the arm in hand there or to take the arm that's there off.
	if (head.part->GetType() == PartType::ROCKER_SHAFT)
	{
		constexpr float REACH = 0.03f;
		const ItemVisual* inHand = GetHeld();
		bool arm = inHand && inHand->part && inHand->part->GetType() == PartType::ROCKER_ARM;
		float bestFit = REACH, bestOff = REACH;
		for (int cylinder = 0; cylinder < workshop.GetSpec().cylinders; cylinder++)
		{
			glm::vec3 point = glm::vec3(transform * glm::vec4(BoreX(cylinder), 0.f, 0.f, 1.f));
			float along = glm::dot(point - eye, forward);
			float off = along <= 0.f ? 1e9f : glm::length(point - (eye + forward * along));
			auto there = workshop.InLooseHead(*head.part, AssemblySlot::ROCKER_ARM, cylinder);
			if (arm && !there)
			{
				m_fitPoints.push_back({ point, true });
				if (off < bestFit)
				{
					bestFit = off;
					m_aimedHeadSpot = HeadSpot{ head.part->GetId(), AssemblySlot::ROCKER_ARM, cylinder };
					m_boreFitTarget = point;
				}
			}
			if (there && off < bestOff)
			{
				bestOff = off;
				m_aimedHeadPart = there->GetId();
			}
		}
		return;
	}
	const auto& assembly = workshop.GetAssembly();
	// Each valve's place: a valve itself goes in from underneath, up through its seat in the chamber; the rest of the stack
	// (seal, spring, retainer) on top, on its stem up through its spring seat.
	auto valveAt = [&](int cylinder, bool exhaust, AssemblySlot slot) {
		bool valve = slot == AssemblySlot::INTAKE_VALVE || slot == AssemblySlot::EXHAUST_VALVE;
		float y = valve ? VALVE_FACE_Y : HEAD_FLOOR_Y + 0.015f;
		return glm::vec3(transform * glm::vec4(BoreX(cylinder), y, ValveZ(exhaust), 1.f));
	};
	auto offRay = [&](const glm::vec3& point) {
		float along = glm::dot(point - eye, forward);
		return along <= 0.f ? 1e9f : glm::length(point - (eye + forward * along));
	};
	auto valveOf = [](AssemblySlot slot, int index) {
		bool valve = slot == AssemblySlot::INTAKE_VALVE || slot == AssemblySlot::EXHAUST_VALVE;
		return valve ? std::pair{ index, slot == AssemblySlot::EXHAUST_VALVE } : std::pair{ index / 2, index % 2 == 1 };
	};
	constexpr float REACH = 0.03f;

	// Which face of the head can be seen from here: the deck (underneath, as it's built) or the top.
	glm::vec3 headUp = glm::normalize(glm::vec3(transform * glm::vec4(0.f, 1.f, 0.f, 0.f)));
	glm::vec3 headCentre = glm::vec3(transform * glm::vec4(0.f, HEAD_FLOOR_Y * 0.5f, 0.f, 1.f));
	bool deckSeen = glm::dot(eye - headCentre, headUp) < 0.f;
	auto faceSeen = [&](AssemblySlot slot) {
		bool valve = slot == AssemblySlot::INTAKE_VALVE || slot == AssemblySlot::EXHAUST_VALVE;
		return valve == deckSeen;
	};

	// The part in hand: a dot on each place in the head it can go (green once what's under it is in), the nearest aimed at:
	// a valve's from the deck side, the rest from the top, only on the face that can be seen.
	const ItemVisual* held = GetHeld();
	float best = REACH;
	if (held && held->part)
		for (auto slot : EngineAssembly::HEAD_VALVETRAIN)
		{
			if (EngineAssembly::SlotPartType(slot) != held->part->GetType() || !faceSeen(slot))
				continue;
			for (int index = 0; index < assembly.SlotCount(slot); index++)
			{
				if (workshop.InLooseHead(*head.part, slot, index))
					continue;
				bool ready = !workshop.HeadFitBlockedBy(*head.part, slot, index);
				auto [cylinder, exhaust] = valveOf(slot, index);
				glm::vec3 point = valveAt(cylinder, exhaust, slot);
				m_fitPoints.push_back({ point, ready });
				if (float off = offRay(point); ready && off < best)
				{
					best = off;
					m_aimedHeadSpot = HeadSpot{ head.part->GetId(), slot, index };
					m_boreFitTarget = point;
				}
			}
		}

	// The Valve Spring Compressor in hand: a dot on each valve with its spring in (from the top), the nearest aimed at.
	if (held && held->ref.IsTool() && ToolCatalog::Of(held->ref.id).handKind == HandToolKind::VALVE_SPRING_COMPRESSOR && !deckSeen)
		for (int index = 0; index < assembly.SlotCount(AssemblySlot::VALVE_SPRING); index++)
		{
			if (!workshop.InLooseHead(*head.part, AssemblySlot::VALVE_SPRING, index))
				continue;
			bool ready = !workshop.CompressValveSpringBlockedBy(*head.part, index);
			glm::vec3 point = valveAt(index / 2, index % 2 == 1, AssemblySlot::VALVE_SPRING);
			m_fitPoints.push_back({ point, ready });
			if (float off = offRay(point); ready && off < best)
			{
				best = off;
				m_aimedHeadSpot = HeadSpot{ head.part->GetId(), AssemblySlot::VALVE_SPRING, index };
				m_boreFitTarget = point;
			}
		}

	// What R takes out: the top of the stack at the valve aimed at (a retainer, then its spring, its seal; the valve, once
	// it's on its own, from the deck side).
	best = REACH;
	for (auto slot : EngineAssembly::HEAD_VALVETRAIN)
		for (int index = 0; index < assembly.SlotCount(slot); index++)
		{
			auto part = workshop.InLooseHead(*head.part, slot, index);
			if (!part || !faceSeen(slot) || workshop.HeadTakeOutBlockedBy(*part))
				continue;
			auto [cylinder, exhaust] = valveOf(slot, index);
			if (float off = offRay(valveAt(cylinder, exhaust, slot)); off < best)
			{
				best = off;
				m_aimedHeadPart = part->GetId();
			}
		}
}

bool ShopWorld::CanCompressAt(const ItemVisual& head) const
{
	const ItemVisual* held = GetHeld();
	return held && held->ref.IsTool() && ToolCatalog::Of(held->ref.id).handKind == HandToolKind::VALVE_SPRING_COMPRESSOR && head.part
		&& m_aimedHeadSpot && m_aimedHeadSpot->head == head.part->GetId() && m_aimedHeadSpot->slot == AssemblySlot::VALVE_SPRING;
}

void ShopWorld::CompressAt(Workshop& workshop, const ItemVisual& head)
{
	ItemVisual* held = GetHeld();
	if (!held || !CanCompressAt(head) || !workshop.CompressValveSpring(head.part, m_aimedHeadSpot->index))
		return;
	// It stays clamped over the valve, out of the hand, till it's taken off.
	held->hostPart = head.part->GetId();
	SetPlace(*held, ItemPlace::ATTACHED);
	Cue(Sound::FIT_PART, head.object->GetPosition());
	SyncHand();
}

ItemVisual* ShopWorld::ValveCompressorVisual()
{
	for (auto& [ref, visual] : m_items)
		if (ref.IsTool() && ToolCatalog::Of(ref.id).handKind == HandToolKind::VALVE_SPRING_COMPRESSOR)
			return &visual;
	return nullptr;
}

bool ShopWorld::CanFitOnto(const Workshop& workshop, const ItemVisual& host) const
{
	// The part in hand into or onto a loose rod, cap or piston (put down, on the bench or the shelf): a bearing
	// shell with none in it yet, a piston onto a rod with none on it, a ring onto a piston (or a rod's) without one.
	auto held = GetHeld();
	if (!held || !held->part || !host.part || &host == held)
		return false;
	bool loose = host.place == ItemPlace::WORLD || host.place == ItemPlace::BENCH || host.place == ItemPlace::STORAGE;
	if (!loose)
		return false;
	// A ring on its own into a bore of a loose block, to be gapped; a piston pushes it down square.
	if (host.part->GetType() == PartType::BLOCK)
	{
		auto bore = AimedBore(host);
		if (!bore)
			return false;
		if (held->part->GetType() == PartType::PISTON_RING)
			return !workshop.LooseBoreBlockedBy(*host.part, *bore);
		auto ring = workshop.RingInLooseBore(*host.part, *bore);
		return held->part->GetType() == PartType::PISTON && ring && !workshop.LooseBoreOf(*ring)->squared;
	}
	// A valve, stem seal, spring or retainer into its place in a loose head, aimed at.
	if (TakesLooseFittings(host.part->GetType()))
		return m_aimedHeadSpot && m_aimedHeadSpot->head == host.part->GetId()
			&& EngineAssembly::SlotPartType(m_aimedHeadSpot->slot) == held->part->GetType();
	if (held->part->GetType() == PartType::PISTON)
		return host.part->GetType() == PartType::CONNECTING_ROD && !workshop.PistonOn(*host.part);
	if (held->part->GetType() == PartType::PISTON_RING)
	{
		auto piston = RingTarget(workshop, *host.part);
		return piston && !workshop.RingOn(*piston);
	}
	return Workshop::ShellFits(host.part->GetType(), held->part->GetType()) && !workshop.ShellIn(*host.part);
}

void ShopWorld::FitOnto(Workshop& workshop, ItemVisual& host)
{
	auto held = GetHeld();
	if (!held)
		return;
	auto type = held->part->GetType();
	if (host.part->GetType() == PartType::BLOCK)
	{
		auto bore = AimedBore(host);
		if (!bore)
			return;
		if (type == PartType::PISTON)
		{
			if (auto ring = workshop.RingInLooseBore(*host.part, *bore); ring && workshop.SquareLooseRing(*ring))
				Cue(Sound::FIT_PART, host.object->GetPosition());
			return;
		}
		if (!workshop.SetRingInLooseBore(host.part, *bore, held->part))
			return;
		held->hostPart = host.part->GetId();
		SetPlace(*held, ItemPlace::ATTACHED);
		Cue(Sound::FIT_PART, host.object->GetPosition());
		SyncHand();
		return;
	}
	if (TakesLooseFittings(host.part->GetType()))
	{
		if (!m_aimedHeadSpot || !workshop.FitIntoHead(host.part, m_aimedHeadSpot->slot, m_aimedHeadSpot->index, held->part))
			return;
		held->hostPart = host.part->GetId();
		SetPlace(*held, ItemPlace::ATTACHED);
		Cue(Sound::FIT_PART, host.object->GetPosition());
		SyncHand();
		return;
	}
	pPart onto = type == PartType::PISTON_RING ? RingTarget(workshop, *host.part) : host.part;
	bool fitted = type == PartType::PISTON ? workshop.FitPiston(host.part, held->part)
		: type == PartType::PISTON_RING ? workshop.FitRing(onto, held->part)
		: workshop.FitShell(host.part, held->part);
	if (!fitted)
		return;
	held->hostPart = onto->GetId();
	SetPlace(*held, ItemPlace::ATTACHED);
	Cue(Sound::FIT_PART, host.object->GetPosition());
	SyncHand();
}

pPart ShopWorld::TakeOffWhat(const Workshop& workshop, const Part& host) const
{
	// A loose block: the ring in the bore aimed into, else any ring in it.
	if (host.GetType() == PartType::BLOCK)
	{
		if (m_aimedBore && m_aimedBore->first == host.GetId())
			if (auto ring = workshop.RingInLooseBore(host, m_aimedBore->second))
				return ring;
		for (int cylinder = 0; cylinder < workshop.GetSpec().cylinders; cylinder++)
			if (auto ring = workshop.RingInLooseBore(host, cylinder))
				return ring;
		return nullptr;
	}
	// A loose head: the part at the valve aimed at, on the face that can be seen (see FindAimedHeadSpot).
	if (TakesLooseFittings(host.GetType()))
	{
		if (m_aimedHeadPart)
			if (auto aimed = workshop.FindPart(*m_aimedHeadPart); aimed && workshop.HostOf(*aimed).get() == &host)
				return aimed;
		return nullptr;
	}
	// The last thing fitted: a piston's ring, then a rod's piston once its pin is out, then the shell in it.
	if (auto piston = RingTarget(workshop, host))
		if (auto ring = workshop.RingOn(*piston))
			return ring;
	if (auto piston = workshop.PistonOn(host); piston && !workshop.PinIn(*piston))
		return piston;
	return workshop.ShellIn(host);
}

void ShopWorld::TakeOff(Workshop& workshop, ItemVisual& host)
{
	auto what = TakeOffWhat(workshop, *host.part);
	if (!what)
	{
		if (workshop.PistonOn(*host.part))
			Toast("Press the piston's pin out first: hold the right button on it with the Piston Pin Press.");
		return;
	}
	auto part = host.part->GetType() == PartType::BLOCK ? workshop.TakeRingOutOfLooseBore(*what)
		: TakesLooseFittings(host.part->GetType()) ? workshop.TakeOutOfHead(*what)
		: what->GetType() == PartType::PISTON_RING ? workshop.TakeRingOff(RingTarget(workshop, *host.part))
		: what->GetType() == PartType::PISTON ? workshop.TakePistonOff(host.part) : workshop.TakeShellOut(host.part);
	ItemVisual* visual = part ? Find(ItemRef::Part(part->GetId())) : nullptr;
	if (!visual)
		return;
	// Off where it was, then into the hand (or the backpack); with no room, it's left lying there.
	visual->yaw = host.yaw;
	PlaceInWorld(*visual, visual->object->GetPosition() + glm::vec3(0.f, 0.03f, 0.f), {});
	PickUp(workshop, *visual);
}

void ShopWorld::PickUp(Workshop& workshop, ItemVisual& visual)
{
	// Where the engine is decides what can be got at: the dyno nothing, the hoist the flywheel, the car its service parts
	// (and the flywheel with the gearbox out), a stand all but the flywheel.
	if (visual.place == ItemPlace::STAND)
	{
		auto slot = FindInstalledSlot(workshop.GetAssembly(visual.engineIndex), visual.part);
		if (auto problem = workshop.EngineWorkProblem(visual.engineIndex, slot ? slot->first : AssemblySlot::COUNT))
		{
			Toast(*problem);
			return;
		}
	}
	// Off the car in reverse build order, by hand once its nuts and bolts are undone.
	if (visual.place == ItemPlace::CAR)
	{
		const auto* car = workshop.GetCar();
		auto at = car ? car->assembly.Find(visual.part.get()) : std::nullopt;
		if (!at || at->first == CarSlot::BODY_SHELL || CarAssembly::CarriedBy(at->first))
			return;
		if (auto blocked = car->assembly.RemovalBlockedBy(at->first, at->second))
		{
			Toast(std::format("The {} has to come off first.", *blocked));
			return;
		}
		if (PartWayDoneUp(*visual.part))
		{
			Toast("It's part way done up: hold [RMB] with a spanner on it to undo it the rest of the way.");
			return;
		}
	}
	// Off the engine in reverse build order.
	if (visual.place == ItemPlace::STAND)
	{
		const auto& assembly = workshop.GetAssembly(visual.engineIndex);
		if (auto slot = FindInstalledSlot(assembly, visual.part))
			if (auto blocked = assembly.RemovalBlockedBy(slot->first, slot->second))
			{
				Toast(*blocked);
				return;
			}
		if (PartWayDoneUp(*visual.part))
		{
			Toast("It's part way done up: hold [RMB] with a spanner on it to undo it the rest of the way.");
			return;
		}
	}

	// Onto the hotbar, in hand if the selected slot is free; with the hotbar full, into the backpack
	// if it fits there. Anything goes on the hotbar, however big.
	std::string name = visual.part ? visual.part->GetName() : ToolCatalog::Of(visual.ref.id).name;
	auto slot = m_inventory.FreeHotbarSlot();
	if (!slot)
	{
		glm::ivec2 size = GridSize(visual);
		if (!PlayerInventory::CanEverFit(size))
		{
			Toast(std::format("Your hotbar's full: free a slot to carry the {}.", name));
			return;
		}
		if (!m_inventory.AddToBackpack(visual.ref, size))
		{
			Toast("Your hotbar and backpack are full.");
			return;
		}
	}

	if (visual.place == ItemPlace::STAND)
	{
		workshop.SetActiveEngine(visual.engineIndex);
		if (auto installed = FindInstalledSlot(workshop.GetAssembly(), visual.part))
			workshop.RemovePart(installed->first, installed->second);
	}
	// The Valve Spring Compressor taken off its valve lets the spring back up.
	if (visual.place == ItemPlace::ATTACHED && visual.ref.IsTool() && ToolCatalog::Of(visual.ref.id).handKind == HandToolKind::VALVE_SPRING_COMPRESSOR)
	{
		workshop.ReleaseValveSpring();
		Cue(Sound::FIT_PART, visual.object->GetPosition());
	}
	if (visual.place == ItemPlace::CAR)
		if (auto at = workshop.GetCar()->assembly.Find(visual.part.get()); !at || !workshop.RemoveCarPart(at->first, at->second))
		{
			if (!slot)
				m_inventory.Remove(visual.ref);
			if (!workshop.GetLog().empty())
				Toast(workshop.GetLog().back());
			return;
		}

	if (slot)
	{
		// Into the leftmost free slot, and into the hand: that slot's taken up.
		m_inventory.PutInHotbar(*slot, visual.ref);
		SelectHotbarSlot(*slot);
		m_handEmpty = false;
	}
	SetPlace(visual, ItemPlace::INVENTORY);
	Cue(Sound::PICK_UP, visual.object->GetPosition());
	SyncHand();
	if (!slot)
		Toast(std::format("Hotbar's full: the {} went in your backpack.", name));
	else if (*slot != m_inventory.GetSelected())
		Toast(std::format("{} on the hotbar [{}]", name, (*slot + 1) % 10));
}

bool ShopWorld::DevHold(const ItemRef& item)
{
	auto* visual = Find(item);
	if (!visual)
		return false;
	for (auto& [id, package] : m_packages)
		std::erase(package.items, item);
	m_inventory.Remove(item);
	int slot = m_inventory.GetSelected();
	if (!m_inventory.GetHotbar(slot).IsNone())
		if (auto* occupant = Find(m_inventory.GetHotbar(slot)))
			SetPlace(*occupant, ItemPlace::STORAGE);
	m_inventory.PutInHotbar(slot, item);
	SetPlace(*visual, ItemPlace::INVENTORY);
	m_handEmpty = false;
	SyncHand();
	return true;
}

bool ShopWorld::DevClampCompressor(Workshop& workshop, const pPart& head, int index)
{
	ItemVisual* compressor = ValveCompressorVisual();
	if (!compressor || !head || !workshop.CompressValveSpring(head, index))
		return false;
	for (auto& [id, package] : m_packages)
		std::erase(package.items, compressor->ref);
	m_inventory.Remove(compressor->ref);
	compressor->hostPart = head->GetId();
	SetPlace(*compressor, ItemPlace::ATTACHED);
	SyncHand();
	return true;
}

bool ShopWorld::DevDropHeld(const glm::vec3& position, float yaw, bool upsideDown)
{
	auto* held = GetHeld();
	if (!held)
		return false;
	held->yaw = yaw;
	held->tilt = upsideDown ? glm::angleAxis(glm::pi<float>(), glm::vec3(1.f, 0.f, 0.f)) : glm::quat(1.f, 0.f, 0.f, 0.f);
	PlaceInWorld(*held, position, {});
	SyncHand();
	return true;
}

void ShopWorld::ToggleEquip(const ItemRef& item)
{
	if (!Find(item) && !item.IsPackage())
		return;
	if (m_equipped == item)
		MoveToBackpack(item);
	else if (auto slot = m_inventory.HotbarSlotOf(item))
		SelectHotbarSlot(*slot);
	else
	{
		m_handEmpty = false;
		MoveToHotbar(item, m_inventory.GetSelected());
	}
}

bool ShopWorld::MoveToHotbar(const ItemRef& item, int slot)
{
	if ((!Find(item) && !item.IsPackage()) || !m_inventory.Contains(item) || slot < 0 || slot >= PlayerInventory::HOTBAR_SLOTS)
		return false;

	if (auto from = m_inventory.HotbarSlotOf(item))
		m_inventory.SwapHotbar(*from, slot);
	else
	{
		// From the backpack: whatever's in the slot takes its place there (a delivery can't).
		ItemRef occupant = m_inventory.GetHotbar(slot);
		if (occupant.IsPackage())
		{
			Toast("The delivery in that slot won't go in the backpack: pick another slot.");
			return false;
		}
		const ItemVisual* occupantVisual = occupant.IsNone() ? nullptr : Find(occupant);
		if (!m_inventory.ExchangeWithBackpack(slot, item, occupantVisual ? GridSize(*occupantVisual) : glm::ivec2(0)))
		{
			std::string name = occupantVisual && occupantVisual->part ? occupantVisual->part->GetName() : ToolCatalog::Of(occupant.id).name;
			Toast(std::format("No room in the backpack for the {} in that slot.", name));
			return false;
		}
	}
	SyncHand();
	return true;
}

bool ShopWorld::MoveToBackpack(const ItemRef& item)
{
	if (item.IsPackage())
	{
		Toast("A delivery is too big for the backpack.");
		return false;
	}
	auto visual = Find(item);
	if (!visual || !m_inventory.Contains(item))
		return false;
	if (!m_inventory.InBackpack(item))
	{
		glm::ivec2 size = GridSize(*visual);
		if (!m_inventory.AddToBackpack(item, size))
		{
			std::string name = visual->part ? visual->part->GetName() : ToolCatalog::Of(item.id).name;
			Toast(PlayerInventory::CanEverFit(size) ? std::format("No room in the backpack for the {}.", name)
				: std::format("The {} is too big for the backpack.", name));
			return false;
		}
	}
	SyncHand();
	return true;
}

void ShopWorld::SelectHotbarSlot(int slot)
{
	m_handEmpty = false;
	m_inventory.SetSelected(slot);
	SyncHand();
}

void ShopWorld::SyncHand()
{
	// The picked slot's item in hand, unless it's been put away with its own number. A delivery
	// there is carried in both arms, with nothing else in hand.
	ItemRef selected = m_handEmpty ? ItemRef{} : m_inventory.GetSelectedItem();
	m_carriedPackage = selected.IsPackage() && m_packages.contains(selected.id) ? selected.id : 0;
	ItemRef wanted = selected.IsPackage() ? ItemRef{} : selected;
	if (!m_equipped.IsNone() && m_equipped != wanted)
	{
		if (auto previous = Find(m_equipped); previous && previous->place == ItemPlace::HAND)
			SetPlace(*previous, ItemPlace::INVENTORY);
		m_equipped = {};
	}
	if (!wanted.IsNone() && m_equipped != wanted)
		if (auto visual = Find(wanted))
			SetPlace(*visual, ItemPlace::HAND);
}

void ShopWorld::DropFromInventory(const ItemRef& item, const FirstPersonController& player)
{
	if (item.IsPackage())
	{
		// Out in front of you, its middle at the height you'd carry it.
		if (auto it = m_packages.find(item.id); it != m_packages.end())
		{
			auto model = ModelOf(it->second.object);
			glm::vec3 center = (model->GetBoundsMin() + model->GetBoundsMax()) * 0.5f;
			glm::vec3 forward = glm::normalize(glm::vec3(player.GetForward().x, 0.f, player.GetForward().z) + glm::vec3(0.f, 0.f, 1e-4f));
			glm::vec3 middle = player.GetEyePosition() + forward * 0.8f - glm::vec3(0.f, 0.6f, 0.f);
			ReleasePackage(item.id, middle - Yaw(player.GetYaw()) * center, forward * 0.5f);
		}
		return;
	}
	auto visual = Find(item);
	if (!visual)
		return;
	visual->yaw = player.GetYaw();
	PlaceInWorld(*visual, player.GetEyePosition() + player.GetForward() * 0.6f - glm::vec3(0.f, 0.4f, 0.f), player.GetForward() * 0.5f);
}

bool ShopWorld::TurningHeld(const InputManager& input) const
{
	return !m_move && !m_carriedPackage && GetHeld() && Controls::Held(input, Control::TURN_HELD);
}

void ShopWorld::TurnHeld(const InputManager& input, const FirstPersonController& player)
{
	const ItemVisual* held = GetHeld();
	if (!held || m_heldTurn.item != held->ref)
		m_heldTurn = { held ? held->ref : ItemRef{} };
	// With Shift it goes in steps; let go, it carries on smoothly from the step it's on.
	constexpr float STEP = glm::radians(15.f);
	bool snap = Controls::Held(input, Control::RUN);
	if (!snap)
	{
		m_heldTurn.rawSpin = m_heldTurn.spin;
		m_heldTurn.rawTip = m_heldTurn.tip;
	}
	if (!TurningHeld(input))
		return;
	// Side to side spins it about its own up; up and down tips it toward or away from you.
	constexpr float TURN_PER_PIXEL = 0.004f;
	glm::vec2 mouse = input.GetMouseDelta();
	m_heldTurn.rawSpin -= mouse.x * TURN_PER_PIXEL;
	m_heldTurn.rawTip += mouse.y * TURN_PER_PIXEL;
	float spin = snap ? std::round(m_heldTurn.rawSpin / STEP) * STEP : m_heldTurn.rawSpin;
	float tip = snap ? std::round(m_heldTurn.rawTip / STEP) * STEP : m_heldTurn.rawTip;
	if ((spin != m_heldTurn.spin || tip != m_heldTurn.tip) && !m_heldTurn.turned)
	{
		// The turn starts from the way it's held now.
		m_heldTurn.base = glm::inverse(Yaw(player.GetYaw())) * HoldPoseOf(*held, player).rotation;
		m_heldTurn.turned = true;
	}
	m_heldTurn.spin = spin;
	m_heldTurn.tip = tip;
}

void ShopWorld::ApplyHeldTurn(ItemVisual& held, const FirstPersonController& player)
{
	bool turned = m_heldTurn.turned && m_heldTurn.item == held.ref;
	held.yaw = player.GetYaw();
	// Kept as it goes into the world (SetPlace clears it anywhere else).
	held.tilt = turned ? m_heldTurn.Tilt() : glm::quat(1.f, 0.f, 0.f, 0.f);
	// A spray can is stood up on its base when it's put down (RestingRotation): take that back off, so it goes down as shown.
	if (turned && held.ref.IsTool() && ToolShapeFor(ToolCatalog::Of(held.ref.id)) == ToolShape::SPRAY_CAN)
		held.tilt = held.tilt * glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));
}

glm::quat ShopWorld::RestingRotation(const ItemVisual& visual) const
{
	// Turned to face the way it was put down (and turned in hand, see TurnHeld). A spray can (built lying along X, its
	// nozzle at +X) stands up on its base.
	glm::quat rotation = Yaw(visual.yaw) * visual.tilt;
	if (visual.ref.IsTool() && ToolShapeFor(ToolCatalog::Of(visual.ref.id)) == ToolShape::SPRAY_CAN)
		rotation = rotation * glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));
	return rotation;
}

bool ShopWorld::LevelOut(ItemVisual& visual)
{
	// The side of its box that faces most nearly down goes flat on the floor.
	glm::quat rotation = RestingRotation(visual);
	const glm::vec3 down = { 0.f, -1.f, 0.f };
	glm::vec3 lowest = down;
	float best = -2.f;
	for (const glm::vec3& axis : { glm::vec3(1.f, 0.f, 0.f), glm::vec3(-1.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, -1.f, 0.f),
		glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, 0.f, -1.f) })
	{
		glm::vec3 side = rotation * axis;
		if (float facing = glm::dot(side, down); facing > best)
		{
			best = facing;
			lowest = side;
		}
	}
	// Flat already (to half a degree).
	if (best > std::cos(glm::radians(0.5f)))
		return false;
	glm::vec3 axis = glm::cross(lowest, down);
	if (glm::length(axis) < 1e-6f)
		return false;

	// It rolls over what it's resting on: the lowest corner of its box, or the middle of the lowest edge.
	auto model = ModelOf(visual.object);
	glm::vec3 boundsMin = model->GetBoundsMin(), boundsMax = model->GetBoundsMax();
	glm::mat4 transform = visual.object->GetModelMatrix();
	std::vector<glm::vec3> corners;
	for (int i = 0; i < 8; i++)
		corners.push_back(glm::vec3(transform * glm::vec4(i & 1 ? boundsMax.x : boundsMin.x, i & 2 ? boundsMax.y : boundsMin.y, i & 4 ? boundsMax.z : boundsMin.z, 1.f)));
	float bottom = std::min_element(corners.begin(), corners.end(), [](const glm::vec3& a, const glm::vec3& b) { return a.y < b.y; })->y;
	glm::vec3 pivot = {};
	int touching = 0;
	for (const auto& corner : corners)
		if (corner.y < bottom + 0.002f)
		{
			pivot += corner;
			touching++;
		}

	Tip tip;
	tip.pivot = pivot / static_cast<float>(touching);
	tip.axis = glm::normalize(axis);
	tip.total = std::acos(std::clamp(best, -1.f, 1.f));
	tip.start = rotation;
	tip.startPosition = visual.object->GetPosition();
	m_tips[visual.ref] = tip;
	return true;
}

bool ShopWorld::StepTip(ItemVisual& visual, Tip& tip, float deltaTime)
{
	// Over it goes, slowly at first and faster as it falls, the way something toppling does.
	constexpr float TIP_ACCELERATION = 22.f;	// radians per second per second
	tip.speed += TIP_ACCELERATION * (std::min)(deltaTime, 0.05f);
	tip.angle = (std::min)(tip.angle + tip.speed * (std::min)(deltaTime, 0.05f), tip.total);
	glm::quat turn = glm::angleAxis(tip.angle, tip.axis);
	glm::quat rotation = turn * tip.start;
	// Back into a turn after its yaw (what's under it, a spray can's standing up, stays as it was).
	glm::quat facing = Yaw(visual.yaw);
	glm::quat under = glm::inverse(facing * visual.tilt) * RestingRotation(visual);
	visual.tilt = glm::normalize(glm::inverse(facing) * rotation * glm::inverse(under));
	visual.object->SetRotation(RestingRotation(visual));
	visual.object->SetPosition(tip.pivot + turn * (tip.startPosition - tip.pivot));
	if (tip.angle < tip.total)
		return false;
	// Down flat: heard as it lands, then settled from there.
	Cue(Sound::SET_DOWN, visual.object->GetPosition(), (std::min)(1.f, 0.3f + tip.speed * 0.05f));
	visual.body.resting = false;
	m_supportChanged = true;
	return true;
}

void ShopWorld::PlaceInWorld(ItemVisual& visual, const glm::vec3& position, const glm::vec3& velocity)
{
	SetPlace(visual, ItemPlace::WORLD);
	visual.object->SetPosition(position);
	visual.object->SetRotation(RestingRotation(visual));
	visual.object->SetScale({ 1.f, 1.f, 1.f });
	visual.body.velocity = velocity;
	visual.body.resting = false;
}


void ShopWorld::SetPlace(ItemVisual& visual, ItemPlace place, int machineIndex)
{
	m_supportChanged = true;
	// The bench and each machine hold one part; whatever was there goes back to storage.
	if (place == ItemPlace::BENCH || place == ItemPlace::MACHINE)
		if (auto occupant = FindItemAt(place, machineIndex); occupant && occupant != &visual)
			SetPlace(*occupant, ItemPlace::STORAGE);

	// Only the backpack and hands keep an item in the inventory.
	if (place != ItemPlace::INVENTORY && place != ItemPlace::HAND)
		m_inventory.Remove(visual.ref);
	if (place == ItemPlace::HAND)
		m_equipped = visual.ref;
	else if (m_equipped == visual.ref)
		m_equipped = {};

	visual.place = place;
	visual.standSlot = { AssemblySlot::COUNT, -1 };
	visual.carSlot = -1;
	if (place != ItemPlace::WORLD)
	{
		visual.tilt = glm::quat(1.f, 0.f, 0.f, 0.f);
		m_tips.erase(visual.ref);
	}
	visual.machineIndex = place == ItemPlace::MACHINE ? machineIndex : -1;
	// For a stand, the index is the engine it's fitted to.
	visual.engineIndex = place == ItemPlace::STAND ? (std::max)(machineIndex, 0) : -1;
}
