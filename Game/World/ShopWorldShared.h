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
#include "CarBody.h"
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

	inline const glm::vec3 SPAWN_POSITION = { 0.4f, 0.f, 1.9f };
	inline const FloorObstacle ROOM_BOUNDS = { { -5.f, -3.5f }, { 5.f, 3.5f } };
	// The shack's outside boards, greyed by the weather (the lining inside is the same planks, new).
	inline const glm::vec4 WEATHERED_BOARDS = { 0.78f, 0.76f, 0.72f, 1.f };

	inline const glm::vec3 SHELF_POSITION = { -0.85f, 0.f, -3.1f };
	// The second shelf, for the bolt-on parts, along the back wall to the right of the first.
	inline const glm::vec3 SECOND_SHELF_POSITION = { 2.5f, 0.f, -3.1f };
	// The line borer is too long for the machine row; it stands along the left wall.
	inline const glm::vec3 LINE_BORER_POSITION = { -4.4f, 0.f, -1.2f };
	inline const glm::vec3 SANDING_BENCH_POSITION = { -4.4f, 0.f, 0.2f };
	// Where new furniture is set up, or the first free floor from there (see PlaceNewMachine).
	inline const glm::vec3 FURNITURE_HOME = { -0.6f, 0.f, 1.6f };
	// The lift goes in the middle of the side bay (or the first free floor, without one).
	inline const glm::vec3 LIFT_HOME = { -7.5f, 0.f, 0.f };
	inline const float SHELF_BOARDS[] = { 0.3f, 0.7f, 1.1f, 1.5f };	// the top one below the eyes, in reach
	constexpr float SHELF_HALF_WIDTH = 1.5f;

	inline const glm::vec3 RACK_POSITION = { -6.3f, 0.f, -0.2f };

	inline const glm::vec3 BENCH_POSITION = { -3.6f, 0.f, -2.95f };
	inline const glm::vec3 TOOL_BOARD_POSITION = { -3.6f, 1.65f, -3.47f };
	inline const glm::vec3 STAND_POSITION = { 0.f, 0.f, -1.0f };
	// Where extra engine stands go when bought (stands 2 to 4), either side of the first and in front.
	inline const glm::vec3 EXTRA_STAND_POSITIONS[] = { { 2.4f, 0.f, -1.0f }, { -2.4f, 0.f, -1.0f }, { 1.4f, 0.f, 1.3f } };
	constexpr float STAND_MOUNT_HEIGHT = 1.05f;
	constexpr float STAND_ROLL_PIVOT_Y = -0.13f;	// below the deck, about the block's middle
	constexpr float STAND_ROLL_SPEED = 1.6f;		// radians per second
	// A rod and piston before the ring compressor lets them go home: the ring, sprung out of its groove bigger than the bore
	// (this much), rests on the deck (its middle this high), and the piston sits on it, its crown above the deck.
	constexpr float UNSEATED_RING_SPREAD = 1.06f;
	constexpr float UNSEATED_RING_Y = 0.0013f;
	// A ring gapped on its own: set just in the top of the bore, then pushed down square to where it's read.
	constexpr float RING_IN_BORE_Y = -0.003f;
	constexpr float RING_SQUARED_Y = -0.02f;
	inline const glm::vec3 DYNO_POSITION = { -2.6f, 0.f, 1.3f };
	// Where an engine on the dyno sits, in the dyno's space: its crank on the absorber's axis, its
	// flywheel end (engine +X) towards the absorber.
	inline const glm::vec3 DYNO_MOUNT = { -0.35f, 0.90f, 0.f };
	inline const glm::vec3 DESK_POSITION = { 3.8f, 0.f, 3.1f };
	// The engine hoist: home by the door. In its own space its boom reaches out along +Z; the hook hangs
	// over the ends of its legs, and an engine on it hangs from the hook with its crank along the boom.
	inline const glm::vec3 HOIST_HOME = { 2.6f, 0.f, 2.3f };
	// The LE86, parked on the drive in front of the roll-up door, its nose toward the shop with room in between for the
	// hoist to roll out to its engine bay (its model's +X is its front, so a quarter turn points it at -Z); and where the
	// hoist's hook goes to reach into its engine bay, in its own space.
	inline const glm::vec3 CAR_HOME = { (GarageLayout::DOOR_MIN_X + GarageLayout::DOOR_MAX_X) * 0.5f, 0.f, GarageLayout::HALF_DEPTH + GarageLayout::WALL + 5.4f };
	constexpr float CAR_YAW = glm::half_pi<float>();
	inline const glm::vec3 CAR_ENGINE_BAY = { 1.35f, 0.75f, 0.f };
	// Where the engine's deck sits, its middle, in the car's space: the block between the strut towers, the crank
	// about 0.35 m off the ground and the flywheel end at the firewall.
	inline const glm::vec3 CAR_ENGINE_MOUNT = { CarBody::ENGINE_X, CarBody::ENGINE_DECK_Y, 0.f };
	inline const glm::vec3 HOIST_HOOK = { 0.f, 1.62f, 1.0f };
	inline const glm::vec3 HOIST_HANG = { 0.f, 1.27f, 1.0f };	// the engine's deck, under the hook
	constexpr float HOIST_REACH = 1.2f;			// how far from the hook a stand or the dyno can be, across the floor
	constexpr float HOIST_SWING_SECONDS = 1.4f;	// for an engine to go up onto it or down off it
	// Down the right wall, from in front of the cot; those that don't fit there find free floor (PlaceNewMachine).
	constexpr float MACHINE_ROW_X = 4.45f;
	constexpr float MACHINE_ROW_START_Z = -1.0f;
	constexpr float MACHINE_SPACING = 0.94f;

	// Deliveries land inside the roll-up door.
	inline const glm::vec3 DELIVERY_POINT = { -0.2f, 1.6f, 2.5f };

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

	inline std::string PartLabel(const Part& part)
	{
		return part.GetName();
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
	// A bearing shell or piston isn't fitted to an engine by itself, but in or on its rod or cap: why not, or nullopt
	// for anything else. (A piston pin never goes on by itself either: it's pressed into a piston on its rod.)
	inline std::optional<std::string> ShellGoesInFirst(PartType type)
	{
		if (type == PartType::PISTON_PIN)
			return std::string("A piston pin is pressed into a piston on its rod, off the engine, with the Piston Pin Press");
		auto host = EngineAssembly::CarriedBy(EngineAssembly::SlotFor(type));
		if (!host)
			return std::nullopt;
		if (type == PartType::PISTON)
			return std::string("The piston goes onto a connecting rod off the engine, pinned, and on with it");
		if (type == PartType::PISTON_RING)
			return std::string("The ring goes onto its piston off the engine, and in with it (or on its own into an empty bore, the head off, to gap it)");
		return std::format("The {} goes into a {} off the engine, and on with it", PartTypeName(type), PartTypeName(EngineAssembly::SlotPartType(*host)));
	}
	inline FitSpot FindFitSpot(const EngineAssembly& assembly, PartType type)
	{
		FitSpot spot;
		for (size_t slot = 0; slot < static_cast<size_t>(AssemblySlot::COUNT); slot++)
		{
			auto slotType = static_cast<AssemblySlot>(slot);
			if (EngineAssembly::SlotPartType(slotType) != type)
				continue;
			// Bearing shells go on in their rods and caps, never by themselves.
			if (EngineAssembly::CarriedBy(slotType))
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
