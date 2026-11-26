// A car no one's working on, drawn as a few merged models instead of its hundreds of parts one by one.

#include "ShopWorld.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <future>
#include <map>
#include <tuple>

#include <glm/glm.hpp>

#include "LV/Engine/Graphics/Models/ModelManager.h"
#include "LV/Engine/Objects/ObjectManager.h"

#include "ShopWorldShared.h"

using namespace ShopLayout;

namespace
{
	// Closer than this to the middle of a car (its half length, and then some past reach) and it's drawn part by part, to be
	// looked at, reached into and worked on.
	constexpr float MERGE_DISTANCE = 2.2f + INTERACT_DISTANCE + 1.5f;
	// It stands untouched this long first: a car being let down off its stands, say, isn't merged at every step.
	constexpr float MERGE_SETTLE_SECONDS = 1.f;
	// How often a merged car's parts are looked at again, in case one's changed in some way nothing else noticed.
	constexpr float MERGE_CHECK_SECONDS = 1.f;

	uint64_t Mix(uint64_t hash, const void* data, size_t size)
	{
		const auto* bytes = static_cast<const unsigned char*>(data);
		for (size_t i = 0; i < size; i++)
			hash = (hash ^ bytes[i]) * 1099511628211ull;
		return hash;
	}

	// One way of being drawn (texture, shading, what shadows it casts): one merged model.
	using BatchKey = std::tuple<uint32_t, int, bool, bool>;
	struct Batch
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
	};

	// A member as it was when the merge began: what's needed to put its geometry where it is, on another thread.
	struct MemberShot
	{
		const Model* model = nullptr;	// (the game's own models, loaded once and never changed: safe to read from there)
		glm::mat4 matrix = glm::mat4(1.f);
		glm::vec3 tint = glm::vec3(1.f);
		BatchKey key = {};
	};

	// The parts' geometry where they are in the world, their tints in their colours (the shader multiplies the two), their
	// normals turned with them, sorted by how each is drawn.
	std::map<BatchKey, Batch> BuildBatches(const std::vector<MemberShot>& shots)
	{
		std::map<BatchKey, Batch> batches;
		std::map<BatchKey, std::pair<size_t, size_t>> sizes;
		for (const auto& shot : shots)
		{
			auto& [vertices, indices] = sizes[shot.key];
			vertices += shot.model->GetVertices().size();
			indices += shot.model->GetIndices().size();
		}
		for (const auto& [key, size] : sizes)
		{
			batches[key].vertices.reserve(size.first);
			batches[key].indices.reserve(size.second);
		}
		for (const auto& shot : shots)
		{
			Batch& batch = batches[shot.key];
			const glm::mat3 turn = glm::mat3(shot.matrix);
			const auto start = static_cast<uint32_t>(batch.vertices.size());
			for (Vertex vertex : shot.model->GetVertices())
			{
				vertex.position = glm::vec3(shot.matrix * glm::vec4(vertex.position, 1.f));
				vertex.color *= shot.tint;
				const glm::vec3 normal = turn * vertex.normal;
				const float length = glm::length(normal);
				if (length > 0.f)
					vertex.normal = normal / length;
				batch.vertices.push_back(vertex);
			}
			// (Mirrored, its triangles wind the other way round: turned back, so they still face out.)
			const bool mirrored = glm::determinant(turn) < 0.f;
			const auto& indices = shot.model->GetIndices();
			for (size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				batch.indices.push_back(start + indices[i]);
				batch.indices.push_back(start + indices[mirrored ? i + 2 : i + 1]);
				batch.indices.push_back(start + indices[mirrored ? i + 1 : i + 2]);
			}
		}
		return batches;
	}
}

struct PendingCarMerge
{
	std::vector<pObject> members;
	uint64_t membersHash = 0;
	std::future<std::map<BatchKey, Batch>> batches;
};

std::vector<pObject> ShopWorld::CarMergeMembers(const Workshop& workshop, int car)
{
	const CarRig& rig = RigOf(car);
	std::vector<pObject> members;
	auto add = [&](const pObject& object) {
		// Only what's solid and drawn as it is: not glass, nothing lit up or glowing, nothing drawn over the world.
		if (!object || !object->IsVisible() || object->IsMerged() || object->IsTransparent() || object->GetEmissive() != 0.f
			|| object->IsInFront() || object->IsShadowOnly() || !object->GetModelIndex())
			return;
		members.push_back(object);
	};
	for (auto& [ref, visual] : m_items)
	{
		bool ours = false;
		if (visual.place == ItemPlace::CAR)
			ours = (visual.part ? workshop.CarOf(*visual.part) : CarWithKey(ref)) == car;
		else if (visual.place == ItemPlace::STAND)
			ours = rig.engineInCar >= 0 && visual.engineIndex == rig.engineInCar;
		if (!ours)
			continue;
		add(visual.object);
		for (const auto& [overlay, object] : visual.overlays)
			add(object);
	}
	// The car's own fittings that are objects of their own.
	for (const pObject& object : { rig.fuelFlap, rig.handbrakeLever, rig.gearLever, rig.clusterMarks })
		add(object);
	for (const auto* objects : { &rig.doorGlass, &rig.headlightLenses, &rig.stopLamps, &rig.seatBacks, &rig.strutTops, &rig.swayBarEnds, &rig.tieRodStuds, &rig.loomTrunks, &rig.lineFixtures })
		for (const pObject& object : *objects)
			add(object);
	for (const pObject& object : rig.gaugeNeedles)
		add(object);
	return members;
}

uint64_t ShopWorld::HashMembers(const std::vector<pObject>& members)
{
	uint64_t hash = 1469598103934665603ull;
	for (const pObject& object : members)
	{
		const glm::mat4& matrix = object->GetModelMatrix();
		const glm::vec4 tint = object->GetTint();
		const uint32_t ids[3] = { object->GetModelIndex(), object->GetTextureIndex(), static_cast<uint32_t>(object->GetShading()) };
		const uint8_t flags[3] = { object->IsVisible(), object->CastsShadows(), object->CastsLampShadows() };
		const float emissive = object->GetEmissive();
		hash = Mix(hash, &matrix, sizeof(matrix));
		hash = Mix(hash, &tint, sizeof(tint));
		hash = Mix(hash, ids, sizeof(ids));
		hash = Mix(hash, flags, sizeof(flags));
		hash = Mix(hash, &emissive, sizeof(emissive));
	}
	return hash;
}

void ShopWorld::StartCarMerge(const Workshop& workshop, int car)
{
	CarRig& rig = RigAt(car);
	auto pending = std::make_shared<PendingCarMerge>();
	pending->members = CarMergeMembers(workshop, car);
	std::vector<MemberShot> shots;
	shots.reserve(pending->members.size());
	auto models = ModelManager::Get();
	for (const pObject& object : pending->members)
	{
		const Model* model = models->PeekModel(object->GetModelIndex());
		if (!model || model->GetVertices().empty() || model->GetIndices().empty())
			continue;
		shots.push_back({ model, object->GetModelMatrix(), glm::vec3(object->GetTint()),
			{ object->GetTextureIndex(), static_cast<int>(object->GetShading()), object->CastsShadows(), object->CastsLampShadows() } });
	}
	if (shots.empty())
		return;
	pending->membersHash = HashMembers(pending->members);
	pending->batches = std::async(std::launch::async, [shots = std::move(shots)]() { return BuildBatches(shots); });
	rig.merge.pending = std::move(pending);
}

void ShopWorld::FinishCarMerge(int car)
{
	CarRig& rig = RigAt(car);
	auto pending = std::move(rig.merge.pending);
	auto batches = pending->batches.get();
	// Something about it changed while it was being put together: it's left as it is, to be merged again once it's settled.
	if (HashMembers(pending->members) != pending->membersHash)
		return;
	auto models = ModelManager::Get();
	auto objectManager = ObjectManager::Get();
	for (auto& [key, batch] : batches)
	{
		if (batch.vertices.empty() || batch.indices.empty())
			continue;
		const auto& [texture, shading, castsShadows, castsLampShadows] = key;
		const uint32_t model = models->CreateDynamicModel(std::format("merged car {}", car), std::move(batch.vertices), std::move(batch.indices));
		auto object = objectManager->CreateObject(Object({}, glm::quat(1.f, 0.f, 0.f, 0.f), { 1.f, 1.f, 1.f }, model, texture));
		object->SetShading(static_cast<ObjectShading>(shading));
		object->SetCastsShadows(castsShadows);
		object->SetCastsLampShadows(castsLampShadows);
		object->SetPickable(false);
		rig.merge.models.push_back(model);
		rig.merge.batches.push_back(object);
	}
	for (const pObject& object : pending->members)
		object->SetMerged(true);
	rig.merge.membersHash = pending->membersHash;
	rig.merge.members = std::move(pending->members);
	rig.merge.checkedAt = m_time;
	rig.merge.merged = true;
}

void ShopWorld::UnmergeCar(int car)
{
	CarRig& rig = RigAt(car);
	// (One being put together is let go: what it makes is thrown away once it's done, not waited for now.)
	if (rig.merge.pending)
		m_abandonedMerges.push_back(std::move(rig.merge.pending));
	if (!rig.merge.merged)
		return;
	for (const pObject& object : rig.merge.members)
		object->SetMerged(false);
	auto objectManager = ObjectManager::Get();
	for (const pObject& object : rig.merge.batches)
		objectManager->RemoveObject(object);
	for (uint32_t model : rig.merge.models)
		ModelManager::Get()->ReleaseDynamicModel(model);
	rig.merge.members.clear();
	rig.merge.batches.clear();
	rig.merge.models.clear();
	rig.merge.merged = false;
	rig.merge.settledSince = m_time;
}

void ShopWorld::UpdateCarMerges(const Workshop& workshop, const FirstPersonController& player)
{
	std::erase_if(m_abandonedMerges, [](const auto& pending) { return pending->batches.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
	const glm::vec3 feet = player.GetFeetPosition();
	bool madeOne = false;
	for (int car = 0; car < static_cast<int>(m_rigs.size()); car++)
	{
		CarRig& rig = RigAt(car);
		const bool there = car < workshop.GetCarCount() && rig.carStation < m_stations.size() && m_stations[rig.carStation].present;
		if (!there)
		{
			UnmergeCar(car);
			continue;
		}
		const Station& station = m_stations[rig.carStation];
		// Anything about it that changes what its parts look like, or where they are: what's on it and where it's all been put
		// (as laid out, SyncStands), where the car is, its engine moving.
		uint64_t key = rig.laidOutKey ^ (m_standsSignature * 31u);
		key = Mix(key, &station.position, sizeof(station.position));
		key = Mix(key, &station.yaw, sizeof(station.yaw));
		const bool engineMoving = rig.engineInCar >= 0 && rig.engineInCar < Workshop::MAX_ENGINES && m_standMoved[rig.engineInCar];
		const bool changed = key != rig.merge.key || engineMoving || rig.carPanelsMoving || rig.drive.active;
		rig.merge.key = key;
		if (changed)
			rig.merge.settledSince = m_time;

		// No one's working on it: it's not the one this player's at, and no one's near it.
		auto distant = [&](const glm::vec3& at) {
			const glm::vec2 away = glm::vec2(at.x - station.position.x, at.z - station.position.z);
			return glm::dot(away, away) > MERGE_DISTANCE * MERGE_DISTANCE;
		};
		const bool alone = car != m_activeRig && distant(feet) && std::all_of(m_othersAt.begin(), m_othersAt.end(), distant);

		if ((rig.merge.merged || rig.merge.pending) && (changed || !alone))
		{
			UnmergeCar(car);
			continue;
		}
		// Put together on its thread: made its models (one car a frame, to spread the uploads out).
		if (rig.merge.pending)
		{
			if (!madeOne && rig.merge.pending->batches.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			{
				FinishCarMerge(car);
				madeOne = true;
			}
			continue;
		}
		if (rig.merge.merged && m_time - rig.merge.checkedAt >= MERGE_CHECK_SECONDS)
		{
			rig.merge.checkedAt = m_time;
			if (HashMembers(rig.merge.members) != rig.merge.membersHash)
				UnmergeCar(car);
			continue;
		}
		if (!rig.merge.merged && alone && m_time - rig.merge.settledSince >= MERGE_SETTLE_SECONDS)
			StartCarMerge(workshop, car);
	}
}
