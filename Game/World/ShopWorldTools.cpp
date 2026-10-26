// Measuring with hand tools, moving equipment, and the HUD.

#include "ShopWorld.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "LV/Engine/Objects/ObjectManager.h"
#include "LV/Engine/Utility/InputManager.h"
#include "../UI/ItemIcons.h"
#include "ShopWorldShared.h"

using namespace ShopLayout;

namespace
{
	std::string FormatFeatureValue(const Feature& feature, double value, Units::System units)
	{
		switch (feature.kind)
		{
			case FeatureKind::FLATNESS:
			case FeatureKind::RUNOUT:	return Units::FormatDeviation(value, units);
			case FeatureKind::VOLUME:	return Units::FormatVolume(value);
			case FeatureKind::FORCE:	return Units::FormatForce(value);
			default:					return Units::FormatSize(value, units);
		}
	}
}

std::optional<ShopWorld::MeasureTarget> ShopWorld::FindMeasureTarget(const Workshop& workshop, const FirstPersonController& player, int toolIndex,
	std::vector<glm::vec3>& reachablePoints) const
{
	reachablePoints.clear();
	const auto& tool = ToolCatalog::All()[toolIndex];
	glm::vec3 eye = player.GetEyePosition();
	glm::vec3 forward = player.GetForward();

	// Points buried deep behind whatever the crosshair is on (a main housing under a bore, seen
	// from above) can't be reached from here. Everything is judged against the first box hit.
	const Object* heldObject = GetHeld() ? GetHeld()->object.get() : nullptr;
	auto surface = ObjectManager::Get()->Raycast(eye, forward, MEASURE_DISTANCE + 1.f,
		[&](const pObject& object) { return object.get() != heldObject; });
	constexpr float REACH_INTO_PART = 0.12f;

	// Pick the measuring point closest to the crosshair. The slop is about the radius of a bore,
	// so aiming anywhere into one counts, while neighbours 95 mm apart stay distinct.
	std::optional<MeasureTarget> best;
	float bestAngle = FLT_MAX;
	auto consider = [&](const glm::vec3& point, const MeasureTarget& target) {
		glm::vec3 toPoint = point - eye;
		float distance = glm::length(toPoint);
		if (distance > MEASURE_DISTANCE || distance < 0.05f)
			return;
		if (surface && distance > surface->distance + REACH_INTO_PART)
			return;
		reachablePoints.push_back(point);
		float angle = std::acos(std::clamp(glm::dot(toPoint / distance, forward), -1.f, 1.f));
		float tolerance = std::atan2(0.042f, distance) + 0.01f;
		if (angle < tolerance && angle < bestAngle)
		{
			bestAngle = angle;
			best = target;
			best->position = point;
		}
	};

	for (const auto& [ref, visual] : m_items)
	{
		if (!visual.part || !visual.object->IsVisible() || visual.place == ItemPlace::HAND)
			continue;
		if (glm::length(visual.object->GetPosition() - eye) > MEASURE_DISTANCE + 0.5f)
			continue;

		glm::mat4 transform = visual.object->GetModelMatrix();
		for (const auto& anchor : ShopAssets::FeatureAnchorsFor(visual.part->GetType()))
		{
			const Feature* feature = visual.part->FindFeature(anchor.featureName);
			if (!feature)
				continue;
			bool canMeasure = tool.measuresFinish ? feature->HasFinishSpec() : feature->directlyMeasurable && tool.CanMeasure(*feature);
			if (!canMeasure)
				continue;

			MeasureTarget target;
			target.part = ref;
			target.feature = anchor.featureName;
			target.finish = tool.measuresFinish;
			target.label = std::format("{}{} on the {}", anchor.featureName, tool.measuresFinish ? " finish" : "", visual.part->GetName());
			consider(glm::vec3(transform * glm::vec4(anchor.position, 1.f)), target);
		}
	}

	// Assembly checks on the engine stand.
	if (!tool.assemblyChecks.empty())
	{
		for (const auto& anchor : ShopAssets::AssemblyAnchors(workshop.GetSpec()))
		{
			if (!tool.CanCheck(anchor.check))
				continue;
			MeasureTarget target;
			target.assembly = true;
			target.check = anchor.check;
			target.index = anchor.index;
			target.rod = anchor.rod;
			target.label = Workshop::AssemblyReadingLabel(anchor.check, anchor.index, anchor.rod);
			consider(StandToWorld(anchor.position), target);
		}
	}

	return best;
}

Range ShopWorld::AssemblyTarget(const EngineSpec& spec, AssemblyCheck check, bool rod) const
{
	switch (check)
	{
		case AssemblyCheck::OIL_CLEARANCE:		return rod ? spec.rodClearance : spec.mainClearance;
		case AssemblyCheck::CAM_CLEARANCE:		return spec.camClearance;
		case AssemblyCheck::RING_GAP:			return spec.ringGap;
		case AssemblyCheck::DECK_CLEARANCE:		return { spec.quench.min - spec.gasketThickness.nominal, spec.quench.max - spec.gasketThickness.nominal };
		case AssemblyCheck::INSTALLED_HEIGHT:	return spec.installedHeightRange;
		default:								return {};
	}
}

void ShopWorld::Measure(Workshop& workshop, const MeasureTarget& target, int toolIndex)
{
	Readout readout;
	readout.time = READOUT_SECONDS;

	if (target.assembly)
	{
		if (!workshop.MeasureAssembly(target.check, target.index, target.rod, toolIndex))
			return;
		auto reading = workshop.GetAssemblyReading(target.check, target.index, target.rod);
		Range range = AssemblyTarget(workshop.GetSpec(), target.check, target.rod);
		auto format = [&](double value) {
			return target.check == AssemblyCheck::INSTALLED_HEIGHT ? Units::FormatSize(value, m_units) : Units::FormatDeviation(value, m_units);
		};

		readout.title = target.label;
		readout.lines.push_back(format(reading->value));
		readout.lines.push_back(std::format("target {} - {}", format(range.min), format(range.max)));
		readout.good = range.Contains(reading->value);
		m_readout = readout;
		return;
	}

	auto visual = Find(target.part);
	if (!visual)
		return;
	Part& part = *visual->part;
	bool measured = target.finish ? workshop.MeasureFinish(part, target.feature, toolIndex) : workshop.MeasureSize(part, target.feature, toolIndex);
	if (!measured)
		return;

	const Feature* feature = part.FindFeature(target.feature);
	const FeatureReading* reading = part.GetReading(target.feature);

	// Judge the reading, not the truth: copy the feature and swap its geometry for what was read.
	Feature asRead = *feature;
	for (int station = 0; station < Feature::STATIONS; station++)
		for (int axis = 0; axis < Feature::AXES; axis++)
			asRead.samples[station][axis] = reading->samples[station][axis];
	asRead.value = reading->value;
	asRead.surfaceRa = reading->finishRa.value_or(0.0);

	readout.title = std::format("{}  ({})", target.feature, PartLabel(part));
	uint32_t issues = 0;
	if (target.finish)
	{
		readout.lines.push_back(Units::FormatFinish(*reading->finishRa));
		issues = asRead.Evaluate(true) & (FeatureIssue::ROUGH | FeatureIssue::GLAZED);
	}
	else if (asRead.IsRound())
	{
		readout.lines.push_back(std::format("{} average", Units::FormatSize(asRead.Size(), m_units)));
		readout.lines.push_back(std::format("taper {}   out-of-round {}", Units::FormatDeviation(asRead.Taper(), m_units), Units::FormatDeviation(asRead.OutOfRound(), m_units)));
		issues = asRead.Evaluate(false);
	}
	else
	{
		readout.lines.push_back(FormatFeatureValue(asRead, asRead.Size(), m_units));
		issues = asRead.Evaluate(false);
	}

	std::string verdict = DescribeIssues(issues, feature->kind);
	if (!target.finish && issues == FeatureIssue::NONE)
		if (auto sizeClass = asRead.MatchSizeClass(); sizeClass && *sizeClass != 0.0)
			verdict += std::format(" ({:+.3g})", *sizeClass);
	readout.lines.push_back(verdict);
	readout.good = issues == FeatureIssue::NONE;
	m_readout = readout;
}

bool ShopWorld::IsMovingStation() const
{
	return m_move.has_value();
}

void ShopWorld::StartMove(size_t stationIndex)
{
	auto& station = m_stations[stationIndex];
	m_move = MoveState{ stationIndex, station.position, station.yaw, station.yaw, false };
}

void ShopWorld::CancelMove()
{
	if (!m_move)
		return;
	auto& station = m_stations[m_move->stationIndex];
	station.position = m_move->originalPosition;
	station.yaw = m_move->originalYaw;
	m_move.reset();
}

bool ShopWorld::StationFits(const Station& station, const glm::vec3& position, float yaw, const FirstPersonController* player) const
{
	auto model = ModelOf(station.object);
	ShopBox footprint = TransformBox({ model->GetBoundsMin(), model->GetBoundsMax() }, position, Yaw(yaw));

	if (footprint.min.x < ROOM_BOUNDS.min.x || footprint.max.x > ROOM_BOUNDS.max.x || footprint.min.z < ROOM_BOUNDS.min.y || footprint.max.z > ROOM_BOUNDS.max.y)
		return false;

	for (const auto& other : m_stations)
	{
		if (other.object == station.object || !other.present)
			continue;
		auto bounds = WorldBounds(other.object);
		bool overlaps = footprint.min.x < bounds.max.x - 0.01f && bounds.min.x < footprint.max.x - 0.01f
			&& footprint.min.z < bounds.max.z - 0.01f && bounds.min.z < footprint.max.z - 0.01f;
		if (overlaps)
			return false;
	}

	if (player)
	{
		glm::vec2 feet = { player->GetFeetPosition().x, player->GetFeetPosition().z };
		glm::vec2 closest = glm::clamp(feet, glm::vec2(footprint.min.x, footprint.min.z), glm::vec2(footprint.max.x, footprint.max.z));
		if (glm::length(feet - closest) < player->GetRadius())
			return false;
	}
	return true;
}

void ShopWorld::PlaceNewMachine(Station& station)
{
	station.yaw = 0.f;
	if (StationFits(station, station.homePosition, 0.f, nullptr))
	{
		station.position = station.homePosition;
		return;
	}

	// Its usual spot is taken: find the first free patch of floor, working in from the machine wall.
	for (float x = MACHINE_ROW_X; x > ROOM_BOUNDS.min.x; x -= 0.5f)
		for (float z = ROOM_BOUNDS.min.y + 0.6f; z < ROOM_BOUNDS.max.y; z += 0.5f)
			if (StationFits(station, { x, 0.f, z }, 0.f, nullptr))
			{
				station.position = { x, 0.f, z };
				return;
			}
	station.position = station.homePosition;
}

void ShopWorld::UpdateMove(const FirstPersonController& player, const InputManager& input)
{
	auto& station = m_stations[m_move->stationIndex];
	auto model = ModelOf(station.object);
	glm::vec3 extent = (model->GetBoundsMax() - model->GetBoundsMin()) * 0.5f;
	float radius = (std::max)(extent.x, extent.z);

	if (input.WasKeyPressed(GLFW_KEY_R))
		m_move->yaw += glm::half_pi<float>();

	// Hold the equipment out in front, snapped to a 5 cm grid on the floor.
	glm::vec3 forward = player.GetForward();
	forward.y = 0.f;
	forward = glm::length(forward) > 0.001f ? glm::normalize(forward) : glm::vec3(0.f, 0.f, -1.f);
	glm::vec3 target = player.GetFeetPosition() + forward * (radius + 0.9f);
	target = glm::round(target / 0.05f) * 0.05f;
	// Floor-standing equipment stays on the floor; the tool board keeps its height on the wall.
	target.y = station.homePosition.y;

	station.position = target;
	station.yaw = m_move->yaw;
	station.object->SetPosition(station.position);
	station.object->SetRotation(station.Rotation());

	m_move->valid = StationFits(station, target, m_move->yaw, &player);
	station.object->SetTint(m_move->valid ? glm::vec4(0.7f, 1.f, 0.7f, 1.f) : glm::vec4(1.f, 0.45f, 0.45f, 1.f));
	station.object->SetEmissive(0.25f);

	m_prompt = m_move->valid ? std::format("[LMB] Set the {} down here", station.name) : std::format("The {} doesn't fit here", station.name);
	m_secondaryPrompt = "[R] Rotate   [F] Cancel";

	if (m_move->valid && (input.WasKeyPressed(GLFW_KEY_E) || input.WasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)))
	{
		Toast(std::format("Moved the {}.", station.name));
		m_move.reset();
	}
	else if (input.WasKeyPressed(GLFW_KEY_F))
		CancelMove();
}

std::optional<glm::vec2> ShopWorld::ProjectToScreen(const glm::vec3& point, const FirstPersonController& player) const
{
	ImVec2 display = ImGui::GetIO().DisplaySize;
	if (display.x <= 0.f || display.y <= 0.f)
		return std::nullopt;

	glm::vec3 eye = player.GetEyePosition();
	glm::mat4 view = glm::lookAt(eye, eye + player.GetForward(), glm::vec3(0.f, 1.f, 0.f));
	glm::mat4 projection = glm::perspective(glm::radians(m_fieldOfView), display.x / display.y, 0.03f, 60.f);
	glm::vec4 clip = projection * view * glm::vec4(point, 1.f);
	if (clip.w <= 0.001f)
		return std::nullopt;

	glm::vec2 ndc = glm::vec2(clip) / clip.w;
	return glm::vec2((ndc.x * 0.5f + 0.5f) * display.x, (0.5f - ndc.y * 0.5f) * display.y);
}

void ShopWorld::DrawDebugBounds(const FirstPersonController& player) const
{
	ImVec2 display = ImGui::GetIO().DisplaySize;
	if (display.x <= 0.f || display.y <= 0.f)
		return;

	// Behind the UI, over the 3D view.
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	glm::vec3 eye = player.GetEyePosition();
	glm::mat4 viewProjection = glm::perspective(glm::radians(m_fieldOfView), display.x / display.y, 0.03f, 500.f)
		* glm::lookAt(eye, eye + player.GetForward(), glm::vec3(0.f, 1.f, 0.f));

	auto toScreen = [&](const glm::vec4& clip) {
		glm::vec2 ndc = glm::vec2(clip) / clip.w;
		return ImVec2((ndc.x * 0.5f + 0.5f) * display.x, (0.5f - ndc.y * 0.5f) * display.y);
	};
	// Edges that pass behind the camera are cut where they cross just in front of it.
	auto line = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color) {
		constexpr float NEAR_W = 0.05f;
		glm::vec4 clipA = viewProjection * glm::vec4(a, 1.f);
		glm::vec4 clipB = viewProjection * glm::vec4(b, 1.f);
		if (clipA.w < NEAR_W && clipB.w < NEAR_W)
			return;
		if (clipA.w < NEAR_W)
			clipA = glm::mix(clipA, clipB, (NEAR_W - clipA.w) / (clipB.w - clipA.w));
		else if (clipB.w < NEAR_W)
			clipB = glm::mix(clipB, clipA, (NEAR_W - clipB.w) / (clipA.w - clipB.w));
		drawList->AddLine(toScreen(clipA), toScreen(clipB), color, 1.5f);
	};
	auto box = [&](const ShopBox& bounds, ImU32 color) {
		// Far-off boxes (houses down the street) would only clutter the view.
		glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
		if (glm::distance(center, eye) > 60.f)
			return;
		glm::vec3 corners[8];
		for (int i = 0; i < 8; i++)
			corners[i] = { i & 1 ? bounds.max.x : bounds.min.x, i & 2 ? bounds.max.y : bounds.min.y, i & 4 ? bounds.max.z : bounds.min.z };
		constexpr int EDGES[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
		for (const auto& edge : EDGES)
			line(corners[edge[0]], corners[edge[1]], color);
	};

	for (const auto& wall : SolidWalls())
		box(wall, DEBUG_WALL_COLOR);
	for (const auto& obstacle : m_obstacles)
		box({ { obstacle.min.x, 0.f, obstacle.min.y }, { obstacle.max.x, 1.8f, obstacle.max.y } }, DEBUG_OBSTACLE_COLOR);
	for (const auto& surface : m_surfaces)
		box(surface, DEBUG_SURFACE_COLOR);
	for (const auto& station : m_stations)
		if (station.present && station.object)
			box(WorldBounds(station.object), DEBUG_STATION_COLOR);
	for (const auto& [id, package] : m_packages)
		box(WorldBounds(package.object), DEBUG_PACKAGE_COLOR);
	for (const auto& [ref, visual] : m_items)
		if (visual.object && visual.object->IsVisible() && visual.place != ItemPlace::INVENTORY)
			box(WorldBounds(visual.object), DEBUG_ITEM_COLOR);
}

void ShopWorld::DrawHotbar(const Workshop& workshop) const
{
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	ImVec2 display = ImGui::GetIO().DisplaySize;
	constexpr float SLOT = 54.f;
	constexpr float GAP = 6.f;
	float width = PlayerInventory::HOTBAR_SLOTS * SLOT + (PlayerInventory::HOTBAR_SLOTS - 1) * GAP;
	ImVec2 origin = { (display.x - width) * 0.5f, display.y - SLOT - 46.f };

	for (int slot = 0; slot < PlayerInventory::HOTBAR_SLOTS; slot++)
	{
		ImVec2 min = { origin.x + slot * (SLOT + GAP), origin.y };
		ImVec2 max = { min.x + SLOT, min.y + SLOT };
		auto item = m_inventory.GetHotbar(slot);
		bool equipped = !item.IsNone() && item == m_equipped;

		drawList->AddRectFilled(min, max, IM_COL32(20, 22, 26, 170), 5.f);
		drawList->AddRect(min, max, equipped ? IM_COL32(255, 200, 80, 255) : IM_COL32(120, 120, 130, 160), 5.f, 0, equipped ? 2.5f : 1.f);
		if (!item.IsNone())
		{
			auto info = DescribeItem(item, workshop);
			ItemIcons::DrawItem(drawList, info.isTool, info.toolIndex, info.partType, { min.x + 6.f, min.y + 6.f }, SLOT - 12.f);
		}
		drawList->AddText({ min.x + 4.f, min.y + 2.f }, IM_COL32(220, 220, 220, 220), std::to_string((slot + 1) % 10).c_str());
	}

	if (auto held = GetHeld())
	{
		auto info = DescribeItem(held->ref, workshop);
		ShadowText(drawList, { display.x * 0.5f, origin.y - 22.f }, IM_COL32(240, 240, 235, 230), std::format("In hand: {}", info.name), true);
	}
}

void ShopWorld::DrawHud(const Workshop& workshop, const FirstPersonController& player, bool showControls) const
{
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	ImVec2 display = ImGui::GetIO().DisplaySize;
	ImVec2 center = { display.x * 0.5f, display.y * 0.5f };
	const ImU32 white = IM_COL32(240, 240, 235, 230);
	const ImU32 grey = IM_COL32(200, 200, 200, 200);

	drawList->AddCircleFilled(center, 3.5f, IM_COL32(0, 0, 0, 160));
	drawList->AddCircleFilled(center, 2.f, m_hover || m_measureTarget ? IM_COL32(255, 210, 90, 255) : white);

	// Faint dots on everything the tool in hand can measure from here...
	for (const auto& point : m_measurePoints)
		if (auto screen = ProjectToScreen(point, player))
			drawList->AddCircleFilled({ screen->x, screen->y }, 3.f, IM_COL32(120, 220, 255, 110));

	// ...and a ring on the one it will take a reading of.
	if (m_measureTarget)
		if (auto screen = ProjectToScreen(m_measureTarget->position, player))
		{
			drawList->AddCircle({ screen->x, screen->y }, 9.f, IM_COL32(120, 220, 255, 230), 20, 2.f);
			drawList->AddCircleFilled({ screen->x, screen->y }, 2.5f, IM_COL32(120, 220, 255, 230));
		}

	if (!m_prompt.empty())
		ShadowText(drawList, { center.x, center.y + 24.f }, white, m_prompt, true);
	if (!m_secondaryPrompt.empty())
		ShadowText(drawList, { center.x, center.y + 44.f }, grey, m_secondaryPrompt, true);

	if (m_toastTime > 0.f)
	{
		auto alpha = static_cast<int>(255.f * (std::min)(1.f, m_toastTime));
		ShadowText(drawList, { center.x, display.y - 160.f }, IM_COL32(255, 220, 140, alpha), m_toast, true);
	}

	// The last reading, like the face of the gauge, in the top right.
	if (m_readout.time > 0.f)
	{
		float alpha = (std::min)(1.f, m_readout.time);
		float lineHeight = ImGui::GetTextLineHeightWithSpacing();
		float width = 360.f;
		for (const auto& line : m_readout.lines)
			width = (std::max)(width, ImGui::CalcTextSize(line.c_str()).x + 32.f);
		width = (std::max)(width, ImGui::CalcTextSize(m_readout.title.c_str()).x + 32.f);
		ImVec2 min = { display.x - width - 20.f, 60.f };
		ImVec2 max = { display.x - 20.f, min.y + 22.f + lineHeight * (m_readout.lines.size() + 1) };
		drawList->AddRectFilled(min, max, IM_COL32(15, 18, 22, static_cast<int>(200 * alpha)), 6.f);
		drawList->AddRect(min, max, m_readout.good ? IM_COL32(110, 220, 120, static_cast<int>(255 * alpha)) : IM_COL32(240, 110, 100, static_cast<int>(255 * alpha)), 6.f, 0, 2.f);
		drawList->AddText({ min.x + 12.f, min.y + 8.f }, IM_COL32(170, 200, 230, static_cast<int>(255 * alpha)), m_readout.title.c_str());
		for (size_t i = 0; i < m_readout.lines.size(); i++)
		{
			bool verdict = i + 1 == m_readout.lines.size() && m_readout.lines.size() > 1;
			ImU32 color = verdict ? (m_readout.good ? IM_COL32(120, 230, 130, 255) : IM_COL32(250, 120, 110, 255)) : IM_COL32(240, 240, 235, 255);
			color = (color & 0x00FFFFFF) | (static_cast<ImU32>(255 * alpha) << IM_COL32_A_SHIFT);
			drawList->AddText({ min.x + 12.f, min.y + 12.f + lineHeight * (i + 1) }, color, m_readout.lines[i].c_str());
		}
	}

	if (m_engineRpm > 0.f)
		ShadowText(drawList, { center.x, 40.f }, IM_COL32(255, 120, 90, 255), std::format("{:.0f} rpm", m_engineRpm), true);

	ShadowText(drawList, { 16.f, 12.f }, IM_COL32(140, 230, 140, 255), std::format("${:.0f}", workshop.GetMoney()), false);
	ShadowText(drawList, { 90.f, 12.f }, white, std::format("{:.1f} h on the clock", workshop.GetHours()), false);
	if (!m_packages.empty())
		ShadowText(drawList, { 16.f, 32.f }, IM_COL32(230, 190, 120, 255), std::format("{} deliver{} waiting by the door", m_packages.size(), m_packages.size() == 1 ? "y" : "ies"), false);

	DrawHotbar(workshop);

	if (showControls)
		ShadowText(drawList, { 16.f, display.y - 30.f }, grey,
			"WASD move  Space jump  E use  LMB measure  RMB set down  G drop  Q stow  1-0 hotbar  Tab backpack  F move  R turn engine  Z/X roll engine  Esc menu", false);
}
