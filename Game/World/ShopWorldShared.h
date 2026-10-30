#pragma once

// Layout constants and small helpers shared by the ShopWorld source files.

#include <format>
#include <optional>
#include <string>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "LV/Engine/Graphics/Models/ModelManager.h"
#include "LV/Engine/Objects/Object.h"
#include "../Simulation/EngineAssembly.h"
#include "../Simulation/Machine.h"
#include "../Simulation/PartCatalog.h"
#include "FirstPersonController.h"

namespace ShopLayout
{
	constexpr float INTERACT_DISTANCE = 3.f;
	constexpr float PLACE_DISTANCE = 3.5f;
	constexpr float MEASURE_DISTANCE = 2.5f;
	constexpr float PART_GLOW = 0.35f;
	constexpr float STATION_GLOW = 0.12f;
	constexpr float TOAST_SECONDS = 3.5f;
	constexpr float READOUT_SECONDS = 8.f;
	constexpr float GRAVITY = 9.81f;
	// The engine on screen turns at a fraction of its real speed so the motion stays readable.
	constexpr float VISUAL_RPM_SCALE = 0.09f;
	constexpr float HAND_TURN_RPM = 40.f;

	inline const glm::vec3 SPAWN_POSITION = { 0.f, 0.f, 3.f };
	inline const FloorObstacle ROOM_BOUNDS = { { -7.f, -5.f }, { 7.f, 5.f } };

	inline const glm::vec3 SHELF_POSITION = { 0.f, 0.f, -4.6f };
	// The second shelf, for the bolt-on parts, along the back wall to the right of the first.
	inline const glm::vec3 SECOND_SHELF_POSITION = { 3.45f, 0.f, -4.6f };
	// The line borer is too long for the machine row; it stands along the left wall.
	inline const glm::vec3 LINE_BORER_POSITION = { -6.1f, 0.f, -0.6f };
	inline const glm::vec3 SANDING_BENCH_POSITION = { -6.1f, 0.f, 0.9f };
	// Where new furniture is set up, or the first free floor from there (see PlaceNewMachine).
	inline const glm::vec3 FURNITURE_HOME = { 1.5f, 0.f, 1.5f };
	// The lift goes in the middle of the side bay (or the first free floor, without one).
	inline const glm::vec3 LIFT_HOME = { -10.f, 0.f, 0.f };
	inline const float SHELF_BOARDS[] = { 0.35f, 0.85f, 1.35f, 1.85f };
	constexpr float SHELF_HALF_WIDTH = 1.5f;

	inline const glm::vec3 RACK_POSITION = { -6.3f, 0.f, -0.2f };

	inline const glm::vec3 BENCH_POSITION = { -3.4f, 0.f, -4.45f };
	inline const glm::vec3 TOOL_BOARD_POSITION = { -3.4f, 1.65f, -4.97f };
	inline const glm::vec3 STAND_POSITION = { 0.f, 0.f, -1.2f };
	// Where extra engine stands go when bought (stands 2 to 4), either side of the first and in front.
	inline const glm::vec3 EXTRA_STAND_POSITIONS[] = { { 2.6f, 0.f, -1.2f }, { -2.4f, 0.f, -1.2f }, { 2.6f, 0.f, 1.2f } };
	constexpr float STAND_MOUNT_HEIGHT = 1.05f;
	constexpr float STAND_ROLL_PIVOT_Y = -0.13f;	// below the deck, about the block's middle
	constexpr float STAND_ROLL_SPEED = 1.6f;		// radians per second
	inline const glm::vec3 DYNO_POSITION = { -4.2f, 0.f, 3.2f };
	// Where an engine on the dyno sits, in the dyno's space: its crank on the absorber's axis, its
	// flywheel end (engine +X) towards the absorber.
	inline const glm::vec3 DYNO_MOUNT = { -0.35f, 0.90f, 0.f };
	inline const glm::vec3 DESK_POSITION = { 2.6f, 0.f, 4.5f };
	constexpr float MACHINE_ROW_X = 6.3f;
	constexpr float MACHINE_ROW_START_Z = -4.72f;	// clear of the door into the house at the front end
	constexpr float MACHINE_SPACING = 0.94f;

	// Deliveries land inside the roll-up door.
	inline const glm::vec3 DELIVERY_POINT = { -2.6f, 1.6f, 4.1f };

	inline const glm::vec4 DAMAGED_TINT = { 1.f, 0.55f, 0.5f, 1.f };

	inline glm::quat Yaw(float yaw)
	{
		return glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f));
	}

	inline glm::quat AboutX(float angle)
	{
		return glm::angleAxis(angle, glm::vec3(1.f, 0.f, 0.f));
	}

	inline bool IsBigPart(PartType type)
	{
		return PartCatalog::Get(type).big;
	}

	// Called for every item every frame: no reference counting, on the manager or the model.
	inline const Model* ModelOf(const pObject& object)
	{
		static const auto manager = ModelManager::Get();
		return manager->PeekModel(object->GetModelIndex());
	}

	inline std::string StripTimestamp(const std::string& line)
	{
		auto end = line.find("] ");
		return end == std::string::npos ? line : line.substr(end + 2);
	}

	inline std::string PartLabel(const Part& part)
	{
		return std::format("#{} {}", part.GetId(), part.GetName());
	}

	inline bool MachineCanWork(const Machine& machine, const Part& part)
	{
		for (const auto& feature : part.GetFeatures())
			if (Machining::CanMachine(machine, feature))
				return true;
		return false;
	}

	inline std::optional<std::pair<AssemblySlot, int>> FindInstalledSlot(const EngineAssembly& assembly, const pPart& part)
	{
		return assembly.Find(part.get());
	}

	// Where a part of this type can go on the engine right now: the first empty position whose
	// prerequisites are fitted. Otherwise why not (no empty position, or what has to go on first).
	struct FitSpot
	{
		std::optional<std::pair<AssemblySlot, int>> slot = std::nullopt;
		bool anyEmpty = false;
		std::string notYet = "";
	};
	inline FitSpot FindFitSpot(const EngineAssembly& assembly, PartType type)
	{
		FitSpot spot;
		for (size_t slot = 0; slot < static_cast<size_t>(AssemblySlot::COUNT); slot++)
		{
			auto slotType = static_cast<AssemblySlot>(slot);
			if (EngineAssembly::SlotPartType(slotType) != type)
				continue;
			for (int index = 0; index < assembly.SlotCount(slotType); index++)
			{
				if (assembly.Get(slotType, index))
					continue;
				auto missing = assembly.MissingPrerequisite(slotType, index);
				if (!missing)
				{
					spot.slot = std::make_pair(slotType, index);
					return spot;
				}
				if (!spot.anyEmpty)
					spot.notYet = *missing;
				spot.anyEmpty = true;
			}
		}
		return spot;
	}

	inline std::optional<float> RayBox(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& boxMin, const glm::vec3& boxMax)
	{
		float tMin = 0.f, tMax = FLT_MAX;
		for (int axis = 0; axis < 3; axis++)
		{
			if (std::abs(direction[axis]) < 1e-8f)
			{
				if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis])
					return std::nullopt;
				continue;
			}
			float t0 = (boxMin[axis] - origin[axis]) / direction[axis];
			float t1 = (boxMax[axis] - origin[axis]) / direction[axis];
			if (t0 > t1)
				std::swap(t0, t1);
			tMin = (std::max)(tMin, t0);
			tMax = (std::min)(tMax, t1);
			if (tMin > tMax)
				return std::nullopt;
		}
		return tMin;
	}

	inline void ShadowText(ImDrawList* drawList, ImVec2 position, ImU32 color, const std::string& text, bool centered)
	{
		ImVec2 size = ImGui::CalcTextSize(text.c_str());
		if (centered)
			position.x -= size.x * 0.5f;
		drawList->AddText({ position.x + 1.f, position.y + 1.f }, IM_COL32(0, 0, 0, (color >> IM_COL32_A_SHIFT) & 0xFF), text.c_str());
		drawList->AddText(position, color, text.c_str());
	}
}
