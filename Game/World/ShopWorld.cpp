#include "ShopWorld.h"

#include <algorithm>
#include <format>
#include <map>
#include <unordered_set>

#include <glm/gtc/constants.hpp>

#include "LV/Engine/Graphics/Models/ModelManager.h"
#include "LV/Engine/Objects/ObjectManager.h"
#include "LV/Engine/Utility/InputManager.h"
#include "../Simulation/Random.h"
#include "../UI/PartIcons.h"
#include "GarageLayout.h"
#include "ShopWorldShared.h"
#include "ToolShapes.h"

using namespace ShopLayout;

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

	Spawn(ShopModels::FLOOR, ShopTextures::CONCRETE, {});
	Spawn(ShopModels::WALLS, ShopTextures::PLASTER, {});
	for (const auto& lamp : GarageLayout::LAMPS)
	{
		auto object = Spawn(ShopModels::LAMP, ShopTextures::PAINT, lamp);
		object->SetEmissive(1.4f);
		object->SetCastsShadows(false);
	}

	auto shelf = AddStation(StationType::SHELF, "Parts shelf", Spawn(ShopModels::SHELF, ShopTextures::PAINT, SHELF_POSITION), true);
	for (float board : SHELF_BOARDS)
		m_stations[shelf].localSurfaces.push_back({ { -1.6f, board - 0.03f, -0.25f }, { 1.6f, board, 0.25f } });


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
		auto fixture = ShopAssets::FixtureFor(machines[i]);
		glm::vec3 home = { MACHINE_ROW_X, 0.f, MACHINE_ROW_START_Z + MACHINE_SPACING * i };
		auto object = Spawn(fixture.model, ShopTextures::PAINT, home);
		auto index = AddStation(StationType::MACHINE, machines[i].name, object, true, fixture.partOffset, fixture.partYaw, static_cast<int>(i));
		auto& station = m_stations[index];
		glm::vec3 offset = fixture.partOffset;
		glm::vec2 table = { fixture.tableSize.x * 0.6f, (std::min)(fixture.tableSize.y, MACHINE_SPACING) * 0.9f };
		station.localSurfaces.push_back({ { offset.x - table.x * 0.5f, 0.f, offset.z - table.y * 0.5f }, { offset.x + table.x * 0.5f, offset.y, offset.z + table.y * 0.5f } });
		station.present = false;
		object->SetVisible(false);
		object->SetPickable(false);
	}

	auto dyno = AddStation(StationType::DYNO, "Dyno cell", Spawn(ShopModels::DYNO, ShopTextures::PAINT, DYNO_POSITION), true);
	m_stations[dyno].localSurfaces.push_back({ { -0.8f, 0.f, -0.45f }, { 0.8f, 0.4f, 0.45f } });

	auto desk = AddStation(StationType::COMPUTER, "Parts computer", Spawn(ShopModels::COMPUTER_DESK, ShopTextures::PAINT, DESK_POSITION, glm::pi<float>()), true);
	m_stations[desk].yaw = glm::pi<float>();
	m_stations[desk].homeYaw = glm::pi<float>();
	m_stations[desk].localSurfaces.push_back({ { -0.7f, 0.f, -0.35f }, { 0.7f, 0.76f, 0.35f } });

	BuildOutdoors();

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
		if (!station.present || (m_move && !m_move->packageId && m_move->stationIndex == i))
			continue;

		for (const auto& surface : station.localSurfaces)
			m_surfaces.push_back(TransformBox(surface, station.position, station.Rotation()));

		// The door's footprint depends on how far it's rolled up, so it's added below.
		if (station.type != StationType::TOOL_BOARD && station.type != StationType::DOOR)
		{
			auto bounds = WorldBounds(station.object);
			m_obstacles.push_back({ { bounds.min.x, bounds.min.z }, { bounds.max.x, bounds.max.z } });
		}
	}

	m_obstacles.insert(m_obstacles.end(), m_staticObstacles.begin(), m_staticObstacles.end());
	if (m_doorAmount < 0.85f)
		m_obstacles.push_back({ { GarageLayout::DOOR_MIN_X, GarageLayout::HALF_DEPTH }, { GarageLayout::DOOR_MAX_X, GarageLayout::HALF_DEPTH + GarageLayout::WALL } });
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
	return { { -LOT_HALF_WIDTH, LOT_BACK_Z }, { LOT_HALF_WIDTH, WALK_LIMIT_Z } };
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
		const auto& tool = ToolCatalog::All()[item.id];
		info.name = tool.name;
		info.description = tool.description;
		info.isTool = true;
		info.toolIndex = static_cast<int>(item.id);
		info.size = ToolGridSize(ToolShapeFor(tool));
	}
	else if (auto visual = Find(item); visual && visual->part)
	{
		const auto& part = *visual->part;
		info.name = std::format("#{} {}", part.GetId(), part.GetName());
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
		return ToolGridSize(ToolShapeFor(ToolCatalog::All()[visual.ref.id]));
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

std::optional<UIRequest> ShopWorld::Update(float deltaTime, Workshop& workshop, const FirstPersonController& player,
	const InputManager& input, bool playerHasControl)
{
	deltaTime = (std::min)(deltaTime, 0.05f);
	m_time += deltaTime;

	UpdateDoor(deltaTime);
	SyncItems(workshop);
	SyncMachines(workshop);
	RebuildCollision();
	UpdateStations(workshop);
	UpdateEngineMotion(deltaTime, workshop, input, playerHasControl);

	for (auto& [ref, visual] : m_items)
		if (visual.place == ItemPlace::WORLD && !IsRiding(ref, 0))
			StepBody(visual.object, visual.body, deltaTime);
	for (auto& [id, package] : m_packages)
		if ((!m_move || m_move->packageId != id) && !IsRiding({}, id))
			StepBody(package.object, package.body, deltaTime);

	LayoutItems(workshop, player);

	// Anything the simulation logs (a cut, a purchase, a refusal) shows up briefly on screen.
	const auto& log = workshop.GetLog();
	if (log.size() > m_seenLogSize)
	{
		Toast(StripTimestamp(log.back()));
		m_seenLogSize = log.size();
	}
	m_toastTime = (std::max)(0.f, m_toastTime - deltaTime);
	m_readout.time = (std::max)(0.f, m_readout.time - deltaTime);

	m_hover.reset();
	m_measureTarget.reset();
	m_measurePoints.clear();
	m_prompt.clear();
	m_secondaryPrompt.clear();
	m_heldHint.clear();
	if (!playerHasControl)
		return std::nullopt;

	if (m_move)
	{
		UpdateMove(player, input);
		return std::nullopt;
	}

	UpdateHover(workshop, player);

	// Hotbar: 1-9 and 0 take an item in hand, or put it back if it's already there.
	for (int slot = 0; slot < PlayerInventory::HOTBAR_SLOTS; slot++)
	{
		int key = slot == 9 ? GLFW_KEY_0 : GLFW_KEY_1 + slot;
		if (!input.WasKeyPressed(key))
			continue;
		auto item = m_inventory.GetHotbar(slot);
		if (!item.IsNone())
			ToggleEquip(item);
		else if (auto held = GetHeld(); held && m_inventory.Contains(held->ref))
			Stow(*held);
	}

	if (input.WasKeyPressed(GLFW_KEY_F) && m_hover && m_hover->kind == TargetKind::STATION && m_stations[m_hover->stationIndex].movable)
	{
		StartMove(m_hover->stationIndex);
		return std::nullopt;
	}
	if (input.WasKeyPressed(GLFW_KEY_F) && m_hover && m_hover->kind == TargetKind::PACKAGE)
	{
		StartMovePackage(m_hover->packageId);
		return std::nullopt;
	}

	auto held = GetHeld();

	// A measuring tool in hand: left click takes a reading of whatever it's aimed at.
	if (held && held->ref.IsTool())
	{
		int toolIndex = static_cast<int>(held->ref.id);
		m_measureTarget = FindMeasureTarget(workshop, player, toolIndex, m_measurePoints);
		if (m_measureTarget)
		{
			m_secondaryPrompt = m_prompt;
			m_prompt = std::format("[LMB] Measure {}", m_measureTarget->label);
			if (input.WasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT))
				Measure(workshop, *m_measureTarget, toolIndex);
		}
		else if (m_prompt.empty())
			m_prompt = std::format("Aim the {} at something it can measure", ToolCatalog::All()[toolIndex].name);
	}

	// What can be done with the item in hand, shown beside its name above the hotbar. Where the
	// prompt already offers right-click (setting it down on the bench), it isn't repeated.
	if (held)
	{
		bool promptHasRightClick = m_prompt.find("[RMB]") != std::string::npos;
		m_heldHint = std::format("{}[G] Drop   [Q] Put away", promptHasRightClick ? "" : "[RMB] Set down   ");
	}

	if (held)
	{
		if (input.WasKeyPressed(GLFW_KEY_Q))
			Stow(*held);
		else if (input.WasKeyPressed(GLFW_KEY_G))
		{
			glm::vec3 toss = player.GetForward() * 1.8f + glm::vec3(0.f, 0.8f, 0.f);
			PlaceInWorld(*held, held->object->GetPosition(), toss);
		}
		else if (input.WasMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT))
		{
			if (auto point = AimPoint(player))
			{
				// Set it just above the spot so it settles onto whatever is there.
				auto model = ModelOf(held->object);
				glm::vec3 center = (model->GetBoundsMin() + model->GetBoundsMax()) * 0.5f;
				held->yaw = player.GetYaw();
				glm::vec3 position = *point - Yaw(held->yaw) * glm::vec3(center.x, model->GetBoundsMin().y - 0.03f, center.z);
				PlaceInWorld(*held, position, {});
			}
			else
				Toast("Nowhere to put it there.");
		}
	}

	bool clickInteracts = !held || !held->ref.IsTool();
	if (input.WasKeyPressed(GLFW_KEY_E) || (clickInteracts && input.WasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)))
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
	return m_items[ref] = visual;
}

void ShopWorld::RemoveItem(const ItemRef& ref)
{
	auto it = m_items.find(ref);
	if (it == m_items.end())
		return;

	auto objectManager = ObjectManager::Get();
	m_targets.erase(it->second.object.get());
	objectManager->RemoveObject(it->second.object);
	for (auto& [overlay, object] : it->second.overlays)
		objectManager->RemoveObject(object);

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
		if (m_initialSyncDone)
		{
			visual.place = ItemPlace::PACKAGED;
			delivered.push_back(ref);
		}
		else
			starting.push_back(ref);
	}

	// Laid out on the shelf in order: the big parts first, then by kind.
	std::sort(starting.begin(), starting.end(), [&](const ItemRef& a, const ItemRef& b) {
		auto typeA = Find(a)->part->GetType(), typeB = Find(b)->part->GetType();
		if (IsBigPart(typeA) != IsBigPart(typeB))
			return IsBigPart(typeA);
		if (typeA != typeB)
			return typeA < typeB;
		return a.id < b.id;
	});
	for (const auto& ref : starting)
		PutOnShelf(*Find(ref));

	// Tools bought later come in the post too, then hang on the tool board once put away.
	const auto& tools = ToolCatalog::All();
	for (size_t i = 0; i < tools.size(); i++)
	{
		auto ref = ItemRef::Tool(static_cast<uint32_t>(i));
		if (!workshop.OwnsTool(i) || m_items.contains(ref))
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

	if (!delivered.empty())
		CreatePackage(delivered, {});

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

	// The engines can also be changed from the UI, so the assemblies decide what's on each stand.
	for (auto& [ref, visual] : m_items)
	{
		if (!visual.part)
			continue;
		int engine = workshop.EngineOf(*visual.part);
		if (engine >= 0)
		{
			if (visual.place != ItemPlace::STAND || visual.engineIndex != engine)
				SetPlace(visual, ItemPlace::STAND, engine);
		}
		else if (visual.place == ItemPlace::STAND)
			SetPlace(visual, ItemPlace::STORAGE);
	}
}

void ShopWorld::SyncMachines(const Workshop& workshop)
{
	for (size_t i = 0; i < m_stations.size(); i++)
	{
		auto& station = m_stations[i];
		bool bought = (station.type == StationType::MACHINE && workshop.OwnsMachine(station.machineIndex))
			|| (station.type == StationType::ENGINE_STAND && station.engineIndex < workshop.GetEngineCount());
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

void ShopWorld::CreatePackage(const std::vector<ItemRef>& items, const std::vector<size_t>& stations)
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
			counts[ToolCatalog::All()[ref.id].name]++;
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

	// Machines and stands come in a crate as big as they need, everything else in a box or crate.
	glm::vec3 position = NextDeliverySpot();
	auto object = big ? Spawn(ShopModels::PACKAGE_CRATE, ShopTextures::WOOD, position, static_cast<float>(Random::Uniform(-0.1, 0.1)))
		: Spawn(ShopModels::PACKAGE_BOX, ShopTextures::CARDBOARD, position, static_cast<float>(Random::Uniform(-0.25, 0.25)));
	object->SetPickable(true);

	Package package = { m_nextPackageId++, object, items, stations, label, { {}, false } };
	m_targets[object.get()] = { TargetKind::PACKAGE, {}, package.id, 0 };
	m_packages[package.id] = package;
	m_deliveries++;
	Toast(std::format("Delivery at the door: {}", label));
}

void ShopWorld::OpenPackage(Package& package)
{
	auto bounds = WorldBounds(package.object);
	glm::vec3 center = (bounds.min + bounds.max) * 0.5f;

	// Everything is laid out on the floor beside the pile, on the shop side, away from the door
	// (a box opened at the top of a stack would otherwise rain parts out onto the driveway).
	constexpr int PER_ROW = 4;
	constexpr float SPACING = 0.45f;
	int index = 0;
	for (const auto& ref : package.items)
	{
		auto visual = Find(ref);
		if (!visual)
			continue;
		glm::vec2 jitter = { static_cast<float>(Random::Uniform(-0.08, 0.08)), static_cast<float>(Random::Uniform(-0.08, 0.08)) };
		glm::vec3 position = { center.x + (index % PER_ROW - (PER_ROW - 1) * 0.5f) * SPACING + jitter.x, 0.f,
			bounds.min.z - 0.5f - (index / PER_ROW) * SPACING + jitter.y };
		position.y = 0.05f - ModelOf(visual->object)->GetBoundsMin().y;
		visual->yaw = static_cast<float>(Random::Uniform(-glm::pi<float>(), glm::pi<float>()));
		PlaceInWorld(*visual, position, {});
		index++;
	}

	// Equipment is set up where its crate stood if it fits there, otherwise at its usual spot.
	// Copied out first: package refers into m_packages, which is about to lose it.
	glm::vec3 floorSpot = { center.x, 0.f, center.z };
	uint32_t packageId = package.id;
	auto stations = package.stations;
	std::string label = package.label;
	m_targets.erase(package.object.get());
	ObjectManager::Get()->RemoveObject(package.object);
	m_packages.erase(packageId);

	for (size_t stationIndex : stations)
	{
		auto& station = m_stations[stationIndex];
		station.present = true;
		station.yaw = 0.f;
		if (StationFits(station, floorSpot, 0.f, nullptr))
			station.position = floorSpot;
		else
			PlaceNewMachine(station);
		station.object->SetVisible(true);
		station.object->SetPickable(true);
	}
	if (!stations.empty())
		RebuildCollision();

	Toast(stations.empty() ? std::format("Unpacked {}.", label)
		: std::format("Set up the {}. Press F on it to move it.", label));
}

void ShopWorld::PutOnShelf(ItemVisual& visual)
{
	// In order: the first gap from the left on the lowest board with headroom for it, lengthways
	// along the board and centred on it. It's then a loose part like any other, settled by the
	// physics. (Put away in part order, the shelf fills up sorted.)
	const Station* shelf = FindStation(StationType::SHELF);
	auto model = ModelOf(visual.object);
	ShopBox local = { model->GetBoundsMin(), model->GetBoundsMax() };
	glm::vec3 size = local.max - local.min;
	constexpr size_t BOARDS = std::size(SHELF_BOARDS);
	constexpr float GAP = 0.02f;

	auto clear = [&](const ShopBox& box) {
		for (const auto& [ref, other] : m_items)
		{
			if (&other == &visual || other.place != ItemPlace::WORLD)
				continue;
			auto bounds = WorldBounds(other.object);
			if (box.min.x < bounds.max.x + GAP && bounds.min.x < box.max.x + GAP && box.min.y < bounds.max.y && bounds.min.y < box.max.y
				&& box.min.z < bounds.max.z + GAP && bounds.min.z < box.max.z + GAP)
				return false;
		}
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
		if (m_move && !m_move->packageId && m_move->stationIndex == i)
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
	if (!assembly.Get(AssemblySlot::CRANKSHAFT, 0))
	{
		reason = "There's no crank in it to turn.";
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
		if (input.IsKeyPressed(GLFW_KEY_Z))
			roll -= STAND_ROLL_SPEED * deltaTime;
		if (input.IsKeyPressed(GLFW_KEY_X))
			roll += STAND_ROLL_SPEED * deltaTime;
		roll = std::remainder(roll, glm::two_pi<float>());
	}

	m_handTurnRpm = 0.f;
	if (playerHasControl && !m_move && looked >= 0 && input.IsKeyPressed(GLFW_KEY_R))
	{
		std::string reason;
		if (EngineTurnsFreely(workshop, looked, reason))
			m_handTurnRpm = HAND_TURN_RPM;
		else if (input.WasKeyPressed(GLFW_KEY_R))
			Toast(reason);
	}

	// The engine on the dyno spins with the pull; the one being turned by hand turns slowly.
	for (int engine = 0; engine < Workshop::MAX_ENGINES; engine++)
	{
		float rpm = engine == m_dynoEngine && m_engineRpm > 0.f ? m_engineRpm * VISUAL_RPM_SCALE : engine == looked ? m_handTurnRpm : 0.f;
		m_crankAngles[engine] = std::fmod(m_crankAngles[engine] + rpm / 60.f * glm::two_pi<float>() * deltaTime, 4.f * glm::pi<float>());
	}
}

int ShopWorld::LookedAtEngine() const
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
	// First person: held low and to the right of the view, locked to the camera so it turns and
	// tilts with your head, clear of the crosshair. Bigger parts sit further out so they don't fill
	// the screen. Tools are held in the right hand, turned in a little; parts are carried more
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
	// Models lie flat the way they rest on a bench; in hand, small things are stood up on end, their
	// longest side turned upright. The big parts (block, head, crank, cam) are carried flat.
	glm::vec3 size = boundsMax - boundsMin;
	float longest = (std::max)({ size.x, size.y, size.z });
	constexpr float CARRIED_FLAT_FROM = 0.3f;
	bool standsUp = longest < CARRIED_FLAT_FROM;
	glm::quat upright = glm::quat(1.f, 0.f, 0.f, 0.f);
	if (standsUp && size.x >= size.y && size.x >= size.z)
		upright = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));
	else if (standsUp && size.z >= size.y && size.z >= size.x)
		upright = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.f, 0.f, 0.f));
	glm::quat rotation = camera * Yaw(tool ? 0.35f : 0.15f) * glm::angleAxis(tool ? -0.15f : -0.1f, glm::vec3(1.f, 0.f, 0.f)) * upright;
	glm::vec3 holdPoint = player.GetEyePosition()
		+ forward * ((tool ? 0.28f : 0.32f) + radius * 1.1f)
		- up * ((tool ? 0.13f : 0.15f) + radius * 0.55f)
		+ right * ((tool ? 0.18f : 0.14f) + radius * 0.5f);
	visual.object->SetPosition(holdPoint - rotation * center);
	visual.object->SetRotation(rotation);
	visual.object->SetScale({ 1.f, 1.f, 1.f });
}

void ShopWorld::PlaceOnStand(ItemVisual& visual, AssemblySlot slot, int index) const
{
	using namespace EngineLayout;

	glm::vec3 offset = {};
	glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
	glm::vec3 scale = { 1.f, 1.f, 1.f };
	int engine = std::clamp(visual.engineIndex, 0, Workshop::MAX_ENGINES - 1);
	float crankAngle = m_crankAngles[engine];

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
			offset = { MainX(index), CRANK_Y, 0.f };
			break;
		case AssemblySlot::ROD_BEARING:
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
			offset = { BoreX(index), smallEndY(index) + PISTON_PIN_DEPTH - 0.006f, 0.f };
			break;
		case AssemblySlot::CAMSHAFT:
			offset = { 0.f, HEAD_DECK_Y + CAM_Y, 0.f };
			rotation = AboutX(crankAngle * 0.5f);
			break;
		case AssemblySlot::INTAKE_VALVE:
		case AssemblySlot::EXHAUST_VALVE:
		{
			bool exhaust = slot == AssemblySlot::EXHAUST_VALVE;
			float lift = ValveLift(index, exhaust, crankAngle);
			offset = { BoreX(index), HEAD_DECK_Y + VALVE_FACE_Y - lift, ValveZ(exhaust) };
			break;
		}
		case AssemblySlot::VALVE_SPRING:
		{
			int cylinder = index / 2;
			bool exhaust = index % 2 == 1;
			float lift = ValveLift(cylinder, exhaust, crankAngle);
			offset = { BoreX(cylinder), HEAD_DECK_Y + HEAD_FLOOR_Y, ValveZ(exhaust) };
			scale.y = (SPRING_INSTALLED_HEIGHT - lift) / SPRING_FREE_HEIGHT;
			break;
		}
		default:
			break;
	}

	// A running engine shakes on its stand.
	if (engine == m_dynoEngine && m_engineRpm > 0.f)
	{
		float amount = 0.0015f * (std::min)(m_engineRpm / 7000.f, 1.f);
		offset += glm::vec3(std::sin(m_time * 91.f), std::sin(m_time * 67.f), std::sin(m_time * 83.f)) * amount;
	}

	// The engine moves with its stand and rolls on its mount.
	visual.object->SetPosition(StandToWorld(engine, offset));
	visual.object->SetRotation(StandRotation(engine) * rotation);
	visual.object->SetScale(scale);
}

namespace
{
	// Engine space runs cylinder 1 to 4 along +X, which would leave cylinder 4 out on the far side
	// of the stand. The engine is turned end for end so its flywheel end (cylinder 4) is against the
	// mounting plate and cylinder 1 hangs out at the front.
	const glm::quat ENGINE_ON_STAND = glm::angleAxis(glm::pi<float>(), glm::vec3(0.f, 1.f, 0.f));
}

glm::quat ShopWorld::StandRotation(int engine) const
{
	const Station* stand = FindStand(engine);
	glm::quat standRotation = stand ? stand->Rotation() : glm::quat(1.f, 0.f, 0.f, 0.f);
	return standRotation * ENGINE_ON_STAND * AboutX(m_engineRolls[std::clamp(engine, 0, Workshop::MAX_ENGINES - 1)]);
}

glm::vec3 ShopWorld::StandToWorld(int engine, const glm::vec3& offset) const
{
	// The stand's head rolls about an axis through the middle of the block, not its deck.
	const glm::vec3 pivot = { 0.f, STAND_ROLL_PIVOT_Y, 0.f };
	const Station* stand = FindStand(engine);
	if (!stand)
		return offset;
	glm::vec3 rolled = pivot + AboutX(m_engineRolls[std::clamp(engine, 0, Workshop::MAX_ENGINES - 1)]) * (offset - pivot);
	return stand->Anchor() + stand->Rotation() * (ENGINE_ON_STAND * rolled);
}

std::vector<BenchItem> ShopWorld::GetBenchItems() const
{
	std::vector<BenchItem> items;

	// Anything put down on the bench top counts as on the bench, however it got there.
	const Station* bench = FindStation(StationType::WORKBENCH);
	ShopBox top = TransformBox(bench->localSurfaces.front(), bench->position, bench->Rotation());
	for (const auto& [ref, visual] : m_items)
	{
		if (!visual.part)
			continue;
		if (visual.place == ItemPlace::BENCH)
		{
			items.push_back({ visual.part, "On the workbench", -1 });
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
			items.push_back({ visual.part, "On the workbench", -1 });
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

void ShopWorld::LayoutItems(const Workshop& workshop, const FirstPersonController& player)
{
	const Station* bench = FindStation(StationType::WORKBENCH);
	const Station* toolBoard = FindStation(StationType::TOOL_BOARD);

	auto layout = [&](ItemVisual& visual)
	{
		auto model = ModelOf(visual.object);
		glm::vec4 tint = glm::vec4(1.f);
		if (visual.part)
		{
			tint = m_assets->PartTint(visual.part->GetMaterial());
			if (visual.part->GetCondition() == PartCondition::DAMAGED)
				tint *= DAMAGED_TINT;
		}
		visual.object->SetTint(tint);
		visual.object->SetEmissive(0.f);
		bool visible = visual.place != ItemPlace::PACKAGED && visual.place != ItemPlace::INVENTORY;
		visual.object->SetVisible(visible);
		visual.object->SetPickable(visible && visual.place != ItemPlace::HAND);

		switch (visual.place)
		{
			case ItemPlace::STORAGE:
				// Hooks are laid out in the board's own space, so hung tools go wherever the board is
				// moved to.
				if (visual.ref.IsTool())
				{
					// Hung flat on the tool board, each tool on its own hook.
					int slot = static_cast<int>(visual.ref.id);
					glm::vec3 hook = toolBoard->position + toolBoard->Rotation() * glm::vec3(-0.72f + (slot % 5) * 0.36f, 0.3f - (slot / 5) * 0.3f, 0.035f);
					auto bounds = ShopBox{ model->GetBoundsMin(), model->GetBoundsMax() };
					glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
					glm::quat rotation = toolBoard->Rotation() * AboutX(glm::half_pi<float>());
					visual.object->SetRotation(rotation);
					visual.object->SetPosition(hook - rotation * glm::vec3(center.x, bounds.min.y, center.z));
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
				if (auto slot = FindInstalledSlot(workshop.GetAssembly(visual.engineIndex), visual.part))
					PlaceOnStand(visual, slot->first, slot->second);
				break;
			case ItemPlace::WORLD:
				visual.object->SetRotation(Yaw(visual.yaw));
				visual.object->SetScale({ 1.f, 1.f, 1.f });
				break;
			case ItemPlace::PACKAGED:
			case ItemPlace::INVENTORY:
				break;
		}

		if (visual.part)
			UpdateOverlays(visual);
	};

	// Parts in inventory order so storage spots stay put from frame to frame, then tools.
	for (const auto& part : workshop.GetInventory())
		if (auto visual = Find(ItemRef::Part(part->GetId())))
			layout(*visual);
	for (auto& [ref, visual] : m_items)
		if (ref.IsTool())
			layout(visual);
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
	for (const auto& [ref, visual] : m_items)
	{
		if (visual.object.get() == self || visual.place == ItemPlace::HAND || visual.place == ItemPlace::PACKAGED || visual.place == ItemPlace::INVENTORY)
			continue;
		if (visual.place == ItemPlace::WORLD && !visual.body.resting)
			continue;
		if (ref.IsTool() && visual.place == ItemPlace::STORAGE)
			continue;
		auto box = WorldBounds(visual.object);
		consider(box.min, box.max);
	}
	for (const auto& [id, package] : m_packages)
	{
		if (package.object.get() == self || !package.body.resting)
			continue;
		auto box = WorldBounds(package.object);
		consider(box.min, box.max);
	}
	return support;
}

void ShopWorld::StepBody(const pObject& object, Body& body, float deltaTime)
{
	auto bounds = WorldBounds(object);

	if (body.resting)
	{
		// Something may have been taken out from underneath.
		if (bounds.min.y - SupportHeight(bounds, bounds.min.y, object.get()) < 0.005f)
			return;
		body.resting = false;
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

void ShopWorld::UpdateHover(const Workshop& workshop, const FirstPersonController& player)
{
	auto held = GetHeld();
	const Object* heldObject = held ? held->object.get() : nullptr;
	bool holdingPart = held && held->part;

	// Stations' boxes enclose the items resting on them (a shelf, a machine table, the stand), so
	// a plain closest hit would always find the station. Without a part in hand you're after items
	// and packages, so try those first; carrying a part you're after a place to put it.
	auto objectManager = ObjectManager::Get();
	auto raycast = [&](bool stations) {
		return objectManager->Raycast(player.GetEyePosition(), player.GetForward(), INTERACT_DISTANCE, [&](const pObject& object) {
			auto target = m_targets.find(object.get());
			if (target == m_targets.end() || object.get() == heldObject)
				return false;
			return (target->second.kind == TargetKind::STATION) == stations;
		});
	};

	auto hit = raycast(holdingPart);
	if (!hit)
		hit = raycast(!holdingPart);
	if (!hit)
		return;
	// Nothing on the far side of a wall, through a window, is in reach.
	if (auto wall = WallHit(player.GetEyePosition(), player.GetForward()); wall && *wall < hit->distance
		&& m_targets.at(hit->object.get()).stationIndex != m_doorStation)
		return;

	m_hover = m_targets.at(hit->object.get());

	// Carrying a part and looking at one already fitted somewhere means "put it there".
	if (holdingPart && m_hover->kind == TargetKind::ITEM)
	{
		const auto& onto = *Find(m_hover->item);
		for (size_t i = 0; i < m_stations.size(); i++)
		{
			const auto& station = m_stations[i];
			bool holdsIt = (onto.place == ItemPlace::STAND && station.type == StationType::ENGINE_STAND && station.engineIndex == onto.engineIndex)
				|| (onto.place == ItemPlace::BENCH && station.type == StationType::WORKBENCH)
				|| (onto.place == ItemPlace::MACHINE && station.type == StationType::MACHINE && station.machineIndex == onto.machineIndex);
			if (holdsIt)
				m_hover = Target{ TargetKind::STATION, {}, 0, i };
		}
	}


	if (m_hover->kind == TargetKind::PACKAGE)
	{
		const auto& package = m_packages.at(m_hover->packageId);
		package.object->SetEmissive(PART_GLOW);
		m_prompt = std::format("[E] Open delivery: {}   [F] Move", package.label);
		return;
	}

	if (m_hover->kind == TargetKind::ITEM)
	{
		const auto& visual = *Find(m_hover->item);
		visual.object->SetEmissive(PART_GLOW);
		std::string name = visual.part ? PartLabel(*visual.part) : ToolCatalog::All()[visual.ref.id].name;

		if (holdingPart)
			m_prompt = name;
		else if (visual.place == ItemPlace::STAND)
		{
			const auto& assembly = workshop.GetAssembly(visual.engineIndex);
			auto slot = FindInstalledSlot(assembly, visual.part);
			auto blocked = slot ? assembly.RemovalBlockedBy(slot->first, slot->second) : std::nullopt;
			m_prompt = blocked ? std::format("{}: {}", name, *blocked) : std::format("[E] Take {} off the engine", name);
			m_secondaryPrompt = "[R] Turn it over   [Z]/[X] Roll it on the stand";
		}
		else
			m_prompt = std::format("[E] Pick up {}", name);

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
				auto spot = FindFitSpot(assembly, held->part->GetType());
				if (spot.slot)
					m_prompt = std::format("[E] Fit the {} as {} on engine {}", held->part->GetName(),
						assembly.PositionName(spot.slot->first, spot.slot->second), station.engineIndex + 1);
				else if (spot.anyEmpty)
					m_prompt = std::format("Not yet: {}", spot.notYet);
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
			m_prompt = held && held->ref.IsTool() ? "[E] Hang the tool back up" : station.name + moveHint;
			break;
		case StationType::SHELF:
		case StationType::PALLET_RACK:
			m_prompt = station.name + moveHint;
			break;
		case StationType::DOOR:
			m_prompt = m_doorOpen ? "[E] Pull the roll-up door down" : "[E] Roll the door up";
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
			OpenPackage(m_packages.at(m_hover->packageId));
			return std::nullopt;
		case TargetKind::ITEM:
			PickUp(workshop, *Find(m_hover->item));
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

			auto spot = FindFitSpot(workshop.GetAssembly(engine), heldPart->part->GetType());
			if (!spot.slot)
			{
				Toast(spot.anyEmpty ? std::format("Not yet: {}", spot.notYet)
					: std::format("No free place on engine {} for a {}.", engine + 1, PartTypeName(heldPart->part->GetType())));
				return std::nullopt;
			}
			auto slot = spot.slot;
			if (workshop.InstallPart(slot->first, slot->second, heldPart->part))
				SetPlace(*heldPart, ItemPlace::STAND, engine);
			return std::nullopt;
		}
		case StationType::DOOR:
			m_doorOpen = !m_doorOpen;
			return std::nullopt;
		case StationType::DYNO:			return UIRequest{ WorkshopTab::DYNO };
		// Everything is bought at the parts computer, and only there.
		case StationType::COMPUTER:		return UIRequest{ WorkshopTab::STORE, 0, -1, StoreCategory::PARTS };
		case StationType::TOOL_BOARD:
			if (held && held->ref.IsTool())
			{
				SetPlace(*held, ItemPlace::STORAGE);
				return std::nullopt;
			}
			Toast("Measuring tools are bought at the parts computer.");
			return std::nullopt;
		// Parts are set down on the shelf like on any other surface ([RMB]).
		case StationType::SHELF:
		case StationType::PALLET_RACK:
			return std::nullopt;
	}
	return std::nullopt;
}

void ShopWorld::PickUp(Workshop& workshop, ItemVisual& visual)
{
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
	}

	glm::ivec2 size = GridSize(visual);
	bool fitsBackpack = PlayerInventory::CanEverFit(size);
	auto held = GetHeld();

	// Big parts go in your hands, and your hands have to be free (or holding something that can
	// go back in the backpack).
	bool needsHands = !fitsBackpack;
	if (fitsBackpack && !m_inventory.Add(visual.ref, size))
		needsHands = true;
	if (needsHands && held && held != &visual && !m_inventory.Contains(held->ref))
	{
		Toast(std::format("Your hands are full: put the {} down first.", held->part ? held->part->GetName() : "tool"));
		return;
	}

	if (visual.place == ItemPlace::STAND)
	{
		workshop.SetActiveEngine(visual.engineIndex);
		if (auto slot = FindInstalledSlot(workshop.GetAssembly(), visual.part))
			workshop.RemovePart(slot->first, slot->second);
	}

	std::string name = visual.part ? visual.part->GetName() : ToolCatalog::All()[visual.ref.id].name;
	if (!needsHands)
	{
		SetPlace(visual, ItemPlace::INVENTORY);
		auto slot = m_inventory.HotbarSlotOf(visual.ref);
		Toast(slot ? std::format("{} into your backpack [{}]", name, (*slot + 1) % 10) : std::format("{} into your backpack", name));
		return;
	}

	if (held && held != &visual)
		SetPlace(*held, ItemPlace::INVENTORY);
	SetPlace(visual, ItemPlace::HAND);
	if (fitsBackpack)
		Toast(std::format("Backpack's full: carrying the {}.", name));
}

void ShopWorld::ToggleEquip(const ItemRef& item)
{
	auto visual = Find(item);
	if (!visual)
		return;

	if (m_equipped == item)
	{
		Stow(*visual);
		return;
	}

	if (auto held = GetHeld())
	{
		if (!m_inventory.Contains(held->ref))
		{
			Toast(std::format("Put the {} down first.", held->part ? held->part->GetName() : "tool"));
			return;
		}
		SetPlace(*held, ItemPlace::INVENTORY);
	}
	SetPlace(*visual, ItemPlace::HAND);
}

void ShopWorld::DropFromInventory(const ItemRef& item, const FirstPersonController& player)
{
	auto visual = Find(item);
	if (!visual)
		return;
	visual->yaw = player.GetYaw();
	PlaceInWorld(*visual, player.GetEyePosition() + player.GetForward() * 0.6f - glm::vec3(0.f, 0.4f, 0.f), player.GetForward() * 0.5f);
}

void ShopWorld::PlaceInWorld(ItemVisual& visual, const glm::vec3& position, const glm::vec3& velocity)
{
	SetPlace(visual, ItemPlace::WORLD);
	visual.object->SetPosition(position);
	visual.object->SetRotation(Yaw(visual.yaw));
	visual.object->SetScale({ 1.f, 1.f, 1.f });
	visual.body.velocity = velocity;
	visual.body.resting = false;
}

void ShopWorld::Stow(ItemVisual& visual)
{
	std::string name = visual.part ? visual.part->GetName() : ToolCatalog::All()[visual.ref.id].name;
	if (m_inventory.Contains(visual.ref) || m_inventory.Add(visual.ref, GridSize(visual)))
	{
		SetPlace(visual, ItemPlace::INVENTORY);
		return;
	}
	// Too big for the backpack, or no room left: onto the shelf, or back on its hook.
	if (visual.part)
		PutOnShelf(visual);
	else
		SetPlace(visual, ItemPlace::STORAGE);
	Toast(std::format("No room in the backpack: put the {} {}.", name, visual.part ? "on the shelf" : "back on the tool board"));
}

void ShopWorld::SetPlace(ItemVisual& visual, ItemPlace place, int machineIndex)
{
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
	visual.machineIndex = place == ItemPlace::MACHINE ? machineIndex : -1;
	// For a stand, the index is the engine it's fitted to.
	visual.engineIndex = place == ItemPlace::STAND ? (std::max)(machineIndex, 0) : -1;
}
