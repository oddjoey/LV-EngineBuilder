#include "NetState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <set>
#include <unordered_set>

#include "LV/Engine/Objects/ObjectManager.h"
#include "NetProtocol.h"
#include "../Save/SaveText.h"
#include "../Simulation/PartFactory.h"
#include "../Simulation/Workshop.h"
#include "../World/ShopWorld.h"
#include "../World/ShopWorldShared.h"
#include "../World/ToolShapes.h"

namespace
{
	constexpr int PART_TYPES = static_cast<int>(PartType::COUNT);
	constexpr int SLOTS = static_cast<int>(AssemblySlot::COUNT);
	constexpr int PLACES = static_cast<int>(ItemPlace::CARRIED) + 1;
	constexpr int MACHINE_OPERATIONS = static_cast<int>(MachineOperation::LINE_BORE) + 1;
	using NetProtocol::ID_BAND;

	std::string PartKey(uint32_t id) { return std::format("p:{}", id); }
	std::string EngineKey(size_t engine) { return std::format("e:{}", engine); }
	std::string StationKey(size_t station) { return std::format("s:{}", station); }
	std::string ItemKey(const ItemRef& ref) { return std::format("i:{}:{}", static_cast<int>(ref.kind), ref.id); }
	std::string PackageKey(uint32_t id) { return std::format("k:{}", id); }
	// The paint sprayed over a part or tool ("o:kind:id"); paint on the walls is WallPaint's ("d:...").
	std::string CoatKey(const ItemRef& ref) { return std::format("o:{}:{}", static_cast<int>(ref.kind), ref.id); }
	// Which way the shop's stations are numbered ("s:N"), so a garage kept from before a change can be
	// renumbered. 1: no cot (the station after the lift went). 2: the engine hoist, after the extension's
	// door. 3: the project car, after the hoist. Garages without it are older.
	constexpr const char* LAYOUT_KEY = "g:layout";
	constexpr long long STATION_LAYOUT = 3;
	// The project car, what's on its loose struts and wheels, and the fluids in stock (Workshop::CarState).
	constexpr const char* CAR_KEY = "g:car";

	// "p:12" is part 12: the number after the prefix.
	std::optional<uint32_t> KeyNumber(const std::string& key, const char* prefix)
	{
		size_t length = std::char_traits<char>::length(prefix);
		if (key.compare(0, length, prefix) != 0)
			return std::nullopt;
		try { return static_cast<uint32_t>(std::stoul(key.substr(length))); }
		catch (...) { return std::nullopt; }
	}
	std::optional<ItemRef> KeyItem(const std::string& key)
	{
		if (key.compare(0, 2, "i:") != 0)
			return std::nullopt;
		size_t split = key.find(':', 2);
		if (split == std::string::npos)
			return std::nullopt;
		try
		{
			int kind = std::stoi(key.substr(2, split - 2));
			if (kind < 1 || kind > 2)
				return std::nullopt;
			return ItemRef{ static_cast<ItemRef::Kind>(kind), static_cast<uint32_t>(std::stoul(key.substr(split + 1))) };
		}
		catch (...) { return std::nullopt; }
	}

	template <typename Bools>
	void WriteBools(Writer& out, const Bools& values)
	{
		out.Int(values.size());
		for (bool value : values)
			out.Int(value);
	}
	void ReadBools(Reader& in, std::vector<bool>& values)
	{
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			bool value = in.Int() != 0;
			if (i < values.size())
				values[i] = value;
		}
	}
	// Counts (the tools: how many of each). Ones sent as 0 or 1 before there could be several read the same.
	void WriteCounts(Writer& out, const std::vector<int>& values)
	{
		out.Int(values.size());
		for (int value : values)
			out.Int(value);
	}
	void ReadCounts(Reader& in, std::vector<int>& values, int most)
	{
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			int value = std::clamp(static_cast<int>(in.Int()), 0, most);
			if (i < values.size())
				values[i] = value;
		}
	}

	void WriteVec3(Writer& out, const glm::vec3& v) { out.Num(v.x).Num(v.y).Num(v.z); }
	glm::vec3 ReadVec3(Reader& in)
	{
		float x = static_cast<float>(in.Num());
		float y = static_cast<float>(in.Num());
		float z = static_cast<float>(in.Num());
		return { x, y, z };
	}
	void WriteQuat(Writer& out, const glm::quat& q) { out.Num(q.w).Num(q.x).Num(q.y).Num(q.z); }
	glm::quat ReadQuat(Reader& in)
	{
		float w = static_cast<float>(in.Num());
		float x = static_cast<float>(in.Num());
		float y = static_cast<float>(in.Num());
		float z = static_cast<float>(in.Num());
		return { w, x, y, z };
	}
}

// The tallies that add up: money and the career stats that count.
struct NetState::Counter
{
	const char* key;
	std::function<double(const Workshop&, const ShopWorld&)> get;
	std::function<void(Workshop&, ShopWorld&, double)> set;
};

// Parts: the same fields as the save, readings in name order so the same part always reads the same.
std::string NetState::PartText(const Part& part)
{
	Writer out;
	out.Int(static_cast<int>(part.m_type)).Int(static_cast<int>(part.m_quality)).Int(static_cast<int>(part.m_condition))
		.Int(static_cast<int>(part.m_material)).Num(part.m_sizeOffset).Num(part.m_value).Str(part.m_name).Str(part.m_damageReason)
		.Int(part.m_matchedTo).Int(part.m_matchedPosition).Num(part.m_torque).Int(static_cast<int>(part.m_tightness)).Num(part.m_resurfaced);
	out.Int(part.m_features.size());
	for (const auto& feature : part.m_features)
	{
		out.Str(feature.name);
		for (const auto& station : feature.samples)
			for (double sample : station)
				out.Num(sample);
		out.Num(feature.value).Num(feature.surfaceRa).Int(feature.lastOperation ? static_cast<int>(*feature.lastOperation) : -1);
	}
	std::vector<const std::pair<const std::string, FeatureReading>*> readings;
	for (const auto& entry : part.m_readings)
		readings.push_back(&entry);
	std::sort(readings.begin(), readings.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
	out.Int(readings.size());
	for (const auto* entry : readings)
	{
		const auto& [name, reading] = *entry;
		out.Str(name).Str(reading.toolName).Int(reading.hasSize);
		for (const auto& station : reading.samples)
			for (double sample : station)
				out.Num(sample);
		out.Num(reading.value).Int(reading.finishRa.has_value()).Num(reading.finishRa.value_or(0.0)).Int(reading.stale);
	}
	return out.Text();
}

pPart NetState::ReadPart(const std::string& text, const EngineSpec& spec)
{
	Reader in(text);
	auto type = in.Enumerated<PartType>(PART_TYPES);
	auto quality = in.Enumerated<PartQuality>(3);
	auto condition = in.Enumerated<PartCondition>(3);
	auto material = static_cast<MaterialType>(in.Int());
	double sizeOffset = in.Num();
	double value = in.Num();
	std::string name = in.Next();
	std::string damage = in.Next();
	auto matchedTo = static_cast<uint32_t>(in.Int());
	auto matchedPosition = static_cast<int>(in.Int());
	double torque = in.Num();
	auto tightness = in.Enumerated<FastenerTightness>(3);
	double resurfaced = in.Num();
	if (in.Failed())
		return nullptr;

	auto part = PartFactory::Create(type, spec, quality, condition == PartCondition::DAMAGED ? PartCondition::USED : condition, sizeOffset);
	part->m_condition = condition;
	part->m_material = material;
	part->m_sizeOffset = sizeOffset;
	part->m_value = value;
	part->m_name = name;
	part->m_damageReason = damage;
	part->m_matchedTo = matchedTo;
	part->m_matchedPosition = matchedPosition;
	part->m_torque = torque;
	part->m_tightness = tightness;
	part->m_resurfaced = resurfaced;

	size_t featureCount = in.Count();
	for (size_t f = 0; f < featureCount && !in.Failed(); f++)
	{
		std::string featureName = in.Next();
		double samples[Feature::STATIONS][Feature::AXES] = {};
		for (auto& station : samples)
			for (double& sample : station)
				sample = in.Num();
		double featureValue = in.Num();
		double ra = in.Num();
		auto lastOperation = in.Int();
		if (lastOperation < -1 || lastOperation >= MACHINE_OPERATIONS)
			in.Fail("a machining operation is out of range");
		if (auto feature = part->FindFeature(featureName))
		{
			std::copy(&samples[0][0], &samples[0][0] + Feature::STATIONS * Feature::AXES, &feature->samples[0][0]);
			feature->value = featureValue;
			feature->surfaceRa = ra;
			feature->lastOperation = lastOperation >= 0 ? std::optional(static_cast<MachineOperation>(lastOperation)) : std::nullopt;
		}
	}
	part->m_readings.clear();
	size_t readingCount = in.Count();
	for (size_t r = 0; r < readingCount && !in.Failed(); r++)
	{
		std::string featureName = in.Next();
		FeatureReading reading;
		reading.toolName = in.Next();
		reading.hasSize = in.Int() != 0;
		for (auto& station : reading.samples)
			for (double& sample : station)
				sample = in.Num();
		reading.value = in.Num();
		bool hasFinish = in.Int() != 0;
		double finish = in.Num();
		if (hasFinish)
			reading.finishRa = finish;
		reading.stale = in.Int() != 0;
		part->m_readings[featureName] = reading;
	}
	return in.Failed() ? nullptr : part;
}

const std::vector<NetState::Counter>& NetState::Counters()
{
	static const std::vector<Counter> counters = {
		{ "c:money", [](const Workshop& w, const ShopWorld&) { return w.m_money; }, [](Workshop& w, ShopWorld&, double v) { w.m_money = v; } },
		{ "c:spent", [](const Workshop& w, const ShopWorld&) { return w.m_stats.moneySpent; }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.moneySpent = v; } },
		{ "c:earned", [](const Workshop& w, const ShopWorld&) { return w.m_stats.moneyEarned; }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.moneyEarned = v; } },
		{ "c:bought", [](const Workshop& w, const ShopWorld&) { return double(w.m_stats.partsBought); }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.partsBought = int(v); } },
		{ "c:cores", [](const Workshop& w, const ShopWorld&) { return double(w.m_stats.coresBought); }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.coresBought = int(v); } },
		{ "c:sold", [](const Workshop& w, const ShopWorld&) { return double(w.m_stats.partsSold); }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.partsSold = int(v); } },
		{ "c:fitted", [](const Workshop& w, const ShopWorld&) { return double(w.m_stats.partsFitted); }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.partsFitted = int(v); } },
		{ "c:measured", [](const Workshop& w, const ShopWorld&) { return double(w.m_stats.measurements); }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.measurements = int(v); } },
		{ "c:cuts", [](const Workshop& w, const ShopWorld&) { return double(w.m_stats.cuts); }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.cuts = int(v); } },
		{ "c:pulls", [](const Workshop& w, const ShopWorld&) { return double(w.m_stats.dynoPulls); }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.dynoPulls = int(v); } },
		{ "c:failures", [](const Workshop& w, const ShopWorld&) { return double(w.m_stats.dynoFailures); }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.dynoFailures = int(v); } },
		{ "c:engines_sold", [](const Workshop& w, const ShopWorld&) { return double(w.m_stats.enginesSold); }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.enginesSold = int(v); } },
		{ "c:reputation", [](const Workshop& w, const ShopWorld&) { return w.m_stats.reputation; }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.reputation = v; } },
		{ "c:walked", [](const Workshop& w, const ShopWorld&) { return w.m_stats.metresWalked; }, [](Workshop& w, ShopWorld&, double v) { w.m_stats.metresWalked = v; } },
		{ "c:deliveries", [](const Workshop&, const ShopWorld& s) { return double(s.m_deliveries); }, [](Workshop&, ShopWorld& s, double v) { s.m_deliveries = uint32_t(v); } },
	};
	return counters;
}

namespace
{
	std::string EngineText(const EngineBuild& engine)
	{
		Writer out;
		const auto& assembly = engine.assembly;
		std::vector<std::array<long long, 3>> installed;
		for (int slot = 0; slot < SLOTS; slot++)
			for (int index = 0; index < assembly.SlotCount(static_cast<AssemblySlot>(slot)); index++)
				if (auto part = assembly.Get(static_cast<AssemblySlot>(slot), index))
					installed.push_back({ slot, index, part->GetId() });
		// The count goes negative (less one) to say the slots are numbered with the serpentine belt's
		// tensioner in them: a garage kept from before has them one lower from there on.
		out.Int(-static_cast<long long>(installed.size()) - 1);
		for (const auto& [slot, index, id] : installed)
			out.Int(slot).Int(index).Int(id);
		out.Int(engine.readings.size());
		for (const auto& [label, reading] : engine.readings)
			out.Str(label).Num(reading.value).Int(reading.stale);
		const auto& dyno = engine.lastDyno;
		out.Int(dyno.ran).Int(dyno.failed).Num(dyno.failureRpm).Num(dyno.peakTorqueNm).Num(dyno.peakTorqueRpm).Num(dyno.peakPowerKw)
			.Num(dyno.peakPowerRpm).Num(dyno.oilPressureBar).Num(dyno.blowbyPercent).Num(dyno.compressionRatio).Num(dyno.displacementCc)
			.Num(dyno.healthScore).Int(engine.dynoCurrent);
		out.Int(dyno.curve.size());
		for (const auto& point : dyno.curve)
			out.Num(point.rpm).Num(point.torqueNm).Num(point.powerKw);
		out.Int(dyno.findings.size());
		for (const auto& finding : dyno.findings)
			out.Int(static_cast<int>(finding.severity)).Str(finding.message);
		return out.Text();
	}

	std::unique_ptr<EngineBuild> ReadEngine(const std::string& text, const Workshop& workshop, const std::unordered_map<uint32_t, pPart>& parts)
	{
		Reader in(text);
		auto engine = std::make_unique<EngineBuild>(workshop.GetSpec());
		long long installedCount = in.Int();
		bool beforeTensioner = installedCount >= 0;
		if (!beforeTensioner)
			installedCount = -installedCount - 1;
		for (long long i = 0; i < installedCount && !in.Failed(); i++)
		{
			long long slot = in.Int(), index = in.Int(), id = in.Int();
			if (beforeTensioner && slot >= static_cast<long long>(AssemblySlot::DRIVE_BELT_TENSIONER))
				slot++;
			auto part = parts.find(static_cast<uint32_t>(id));
			if (slot < 0 || slot >= SLOTS || part == parts.end())
				continue;
			std::string error;
			engine->assembly.Install(static_cast<AssemblySlot>(slot), static_cast<int>(index), part->second, error);
		}
		size_t readingCount = in.Count();
		for (size_t i = 0; i < readingCount && !in.Failed(); i++)
		{
			std::string label = in.Next();
			AssemblyReading reading;
			reading.value = in.Num();
			reading.stale = in.Int() != 0;
			engine->readings[label] = reading;
		}
		auto& dyno = engine->lastDyno;
		dyno.ran = in.Int() != 0;
		dyno.failed = in.Int() != 0;
		dyno.failureRpm = static_cast<float>(in.Num());
		dyno.peakTorqueNm = in.Num();
		dyno.peakTorqueRpm = in.Num();
		dyno.peakPowerKw = in.Num();
		dyno.peakPowerRpm = in.Num();
		dyno.oilPressureBar = in.Num();
		dyno.blowbyPercent = in.Num();
		dyno.compressionRatio = in.Num();
		dyno.displacementCc = in.Num();
		dyno.healthScore = in.Num();
		engine->dynoCurrent = in.Int() != 0;
		size_t points = in.Count();
		for (size_t i = 0; i < points && !in.Failed(); i++)
		{
			DynoPoint point;
			point.rpm = static_cast<float>(in.Num());
			point.torqueNm = static_cast<float>(in.Num());
			point.powerKw = static_cast<float>(in.Num());
			dyno.curve.push_back(point);
		}
		size_t findings = in.Count();
		for (size_t i = 0; i < findings && !in.Failed(); i++)
		{
			DynoFinding finding;
			finding.severity = in.Enumerated<FindingSeverity>(3);
			finding.message = in.Next();
			dyno.findings.push_back(finding);
		}
		return in.Failed() ? nullptr : std::move(engine);
	}
}

StateRecords NetState::Capture(const Workshop& workshop, const ShopWorld& world, int localPlayer)
{
	StateRecords records;

	for (const auto& counter : Counters())
		records[counter.key] = NetProtocol::CounterText(counter.get(workshop, world));
	{
		Writer out;
		out.Num(workshop.m_stats.bestPowerKw).Num(workshop.m_stats.bestTorqueNm).Num(workshop.m_stats.bestEngineSale);
		records["g:best"] = out.Text();
	}
	{
		Writer out;
		WriteCounts(out, workshop.m_toolCounts);
		records["g:tools"] = out.Text();
	}
	{
		// The perks the shop has bought with its skill points.
		std::vector<int> skills(workshop.m_skills.begin(), workshop.m_skills.end());
		Writer out;
		WriteCounts(out, skills);
		records["g:skills"] = out.Text();
	}
	{
		Writer out;
		WriteBools(out, workshop.m_ownedMachines);
		records["g:machines"] = out.Text();
	}
	{
		Writer out;
		WriteBools(out, workshop.m_upgrades);
		records["g:upgrades"] = out.Text();
	}
	records["g:ownsdyno"] = Writer().Int(workshop.m_ownsDyno).Text();
	records["g:ownshoist"] = Writer().Int(workshop.m_ownsHoist).Text();
	{
		Writer out;
		out.Int(workshop.m_furniture.size());
		for (size_t piece : workshop.m_furniture)
			out.Int(piece);
		records["g:furniture"] = out.Text();
	}
	records["g:dyno"] = Writer().Int(workshop.m_engineOnDyno).Text();
	records["g:hoist"] = Writer().Int(workshop.m_engineOnHoist).Text();
	{
		// Pistons in the top of their bores, not yet pushed home: engine and cylinder.
		Writer out;
		std::vector<std::pair<int, int>> unseated;
		for (int engine = 0; engine < workshop.GetEngineCount(); engine++)
			for (int cylinder : workshop.m_engines[engine]->unseated)
				unseated.push_back({ engine, cylinder });
		out.Int(unseated.size());
		for (auto [engine, cylinder] : unseated)
			out.Int(engine).Int(cylinder);
		// Then the rings gapped on their own, pushed down square in their bores.
		std::vector<std::pair<int, int>> squared;
		for (int engine = 0; engine < workshop.GetEngineCount(); engine++)
			for (int cylinder : workshop.m_engines[engine]->squaredRings)
				squared.push_back({ engine, cylinder });
		out.Int(squared.size());
		for (auto [engine, cylinder] : squared)
			out.Int(engine).Int(cylinder);
		records["g:unseated"] = out.Text();
	}
	{
		// Rings set in the bores of blocks off the engine: the ring, the block, the bore, and whether it's squared.
		Writer out;
		out.Int(workshop.m_boreRings.size());
		for (const auto& [ring, at] : workshop.m_boreRings)
			out.Int(ring).Int(at.block).Int(at.cylinder).Int(at.squared ? 1 : 0);
		records["g:borerings"] = out.Text();
	}
	{
		// The valvetrain in heads off the engine: the part, the head, and its position in it.
		Writer out;
		out.Int(workshop.m_headFittings.size());
		for (const auto& [part, at] : workshop.m_headFittings)
			out.Int(part).Int(at.head).Int(static_cast<int>(at.slot)).Int(at.index);
		records["g:headfittings"] = out.Text();
	}
	// What's fitted to what off the engines: shells in rods and caps, pistons on rods, pins in pistons.
	for (auto [key, fittings] : { std::pair{ "g:shells", &workshop.m_shells }, std::pair{ "g:pistons", &workshop.m_pistons }, std::pair{ "g:pins", &workshop.m_pins }, std::pair{ "g:rings", &workshop.m_rings } })
	{
		Writer out;
		out.Int(fittings->size());
		for (const auto& [host, fitted] : *fittings)
			out.Int(host).Int(fitted);
		records[key] = out.Text();
	}
	records[LAYOUT_KEY] = Writer().Int(STATION_LAYOUT).Text();

	for (const auto& part : workshop.m_inventory)
		records[PartKey(part->GetId())] = PartText(*part);
	for (size_t engine = 0; engine < workshop.m_engines.size(); engine++)
		records[EngineKey(engine)] = EngineText(*workshop.m_engines[engine]);
	records[CAR_KEY] = workshop.CarState();

	for (size_t i = 0; i < world.m_stations.size(); i++)
	{
		const auto& station = world.m_stations[i];
		Writer out;
		WriteVec3(out, station.position);
		out.Num(station.yaw).Int(station.present);
		records[StationKey(i)] = out.Text();
	}

	for (const auto& [ref, visual] : world.m_items)
	{
		Writer out;
		// What's in this player's backpack or hand is carried by them.
		bool mine = visual.place == ItemPlace::INVENTORY || visual.place == ItemPlace::HAND;
		ItemPlace place = mine ? ItemPlace::CARRIED : visual.place == ItemPlace::ATTACHED || visual.place == ItemPlace::CAR ? ItemPlace::STORAGE : visual.place;
		int index = place == ItemPlace::STAND ? visual.engineIndex : visual.machineIndex;
		out.Int(static_cast<int>(place)).Int(index).Num(visual.yaw);
		if (place == ItemPlace::WORLD)
		{
			WriteVec3(out, visual.object->GetPosition());
			WriteVec3(out, visual.body.velocity);
			out.Int(visual.body.resting);
			out.Num(visual.tilt.w).Num(visual.tilt.x).Num(visual.tilt.y).Num(visual.tilt.z);
		}
		if (place == ItemPlace::CARRIED)
			out.Int(mine ? localPlayer : visual.carrier).Int(mine ? visual.place == ItemPlace::HAND : visual.carriedInHand);
		records[ItemKey(ref)] = out.Text();
	}

	for (const auto& [id, package] : world.m_packages)
	{
		Writer out;
		bool big = package.object->GetModelIndex() == world.m_assets->Model(ShopModels::PACKAGE_CRATE);
		// Carried on our hotbar: in our hands, or put away out of sight.
		int carrier = world.IsCarried(id) ? localPlayer : -1;
		bool inHand = id == world.m_carriedPackage;
		if (auto remote = world.m_remoteCarriedPackages.find(id); remote != world.m_remoteCarriedPackages.end())
		{
			carrier = remote->second;
			inHand = !world.m_remoteStowedPackages.contains(id);
		}
		out.Int(big).Int(carrier);
		WriteVec3(out, package.object->GetPosition());
		WriteQuat(out, package.object->GetRotation());
		WriteVec3(out, package.body.velocity);
		out.Int(package.body.resting).Str(package.label).Int(package.items.size());
		for (const auto& item : package.items)
			out.Int(static_cast<int>(item.kind)).Int(item.id);
		out.Int(package.stations.size());
		for (size_t station : package.stations)
			out.Int(station);
		out.Int(inHand);
		records[PackageKey(id)] = out.Text();
	}

	records["w:doors"] = Writer().Int(world.m_doorOpen).Int(world.m_bayDoorOpen).Text();
	records["w:lift"] = Writer().Int(world.m_liftRaised).Text();
	{
		Writer out;
		out.Int(world.m_toolSizes.size());
		for (const auto& [tool, size] : world.m_toolSizes)
			out.Int(tool).Int(size);
		records["w:sockets"] = out.Text();
	}
	{
		// To the percent, so a pack charging doesn't change the record every frame.
		Writer out;
		out.Int(world.m_batteries.size());
		for (const auto& [tool, battery] : world.m_batteries)
			out.Int(tool).Int(static_cast<int>(std::round(battery.charge * 100.f))).Int(battery.on);
		records["w:batteries"] = out.Text();
	}
	{
		// Tools hung on the tool board by hand, to the millimetre.
		Writer out;
		out.Int(world.m_toolSpots.size());
		for (const auto& [tool, spot] : world.m_toolSpots)
			out.Int(tool).Int(std::lround(spot.x * 1000.f)).Int(std::lround(spot.y * 1000.f));
		records["w:toolspots"] = out.Text();
	}
	{
		Writer out;
		for (size_t engine = 0; engine < world.m_engineRolls.size(); engine++)
			out.Num(world.m_engineRolls[engine]).Num(world.m_crankAngles[engine]);
		records["w:engines"] = out.Text();
	}
	// Paint: on the walls and floor, and coats on parts and tools (in hundredths, so a coat going on
	// isn't a change every sync).
	for (auto& [key, text] : world.m_paint.Records())
		records[key] = std::move(text);
	for (const auto& [ref, coat] : world.m_coats)
		records[CoatKey(ref)] = Writer().Int(coat.rgb).Int(std::lround(coat.amount * 100.f)).Text();
	return records;
}

void NetState::Apply(Workshop& workshop, ShopWorld& world, const std::vector<StateChange>& changes, int localPlayer, bool full)
{
	// Sorted by what they are, so everything a record refers to is in place before it.
	std::map<std::string, std::optional<std::string>> partChanges, engineChanges, stationChanges, itemChanges, packageChanges, other;
	for (const auto& change : changes)
	{
		auto& into = change.key.starts_with("p:") ? partChanges : change.key.starts_with("e:") ? engineChanges
			: change.key.starts_with("s:") ? stationChanges : change.key.starts_with("i:") ? itemChanges
			: change.key.starts_with("k:") ? packageChanges : other;
		into[change.key] = change.text;
	}
	// A garage from before a change to the shop's stations is renumbered. Before the cot went it was the
	// station after the lift, and those after it move down one; before the hoist, those after the
	// extension's door move up one to make room for it; before the car, those after the hoist (the furniture) do.
	const size_t carStation = world.m_fixedStations - 1;
	const size_t hoistStation = carStation - 1;
	const size_t cotStation = carStation - 2;
	long long layout = STATION_LAYOUT;
	if (full)
	{
		auto record = other.find(LAYOUT_KEY);
		layout = record == other.end() || !record->second ? 0 : Reader(*record->second).Int();
	}
	auto renumber = [&](size_t index) -> std::optional<size_t> {
		if (layout < 1)
		{
			if (index == cotStation)
				return std::nullopt;
			if (index > cotStation)
				index--;
		}
		if (layout < 2 && index >= hoistStation)
			index++;
		if (layout < 3 && index >= carStation)
			index++;
		return index;
	};
	if (layout < STATION_LAYOUT)
	{
		std::map<std::string, std::optional<std::string>> renumbered;
		for (auto& [key, text] : stationChanges)
			if (auto index = KeyNumber(key, "s:"))
				if (auto now = renumber(*index))
					renumbered[StationKey(*now)] = std::move(text);
		stationChanges = std::move(renumbered);
	}
	// The whole shop: whatever isn't in it goes.
	if (full)
	{
		for (const auto& part : workshop.m_inventory)
			partChanges.try_emplace(PartKey(part->GetId()), std::nullopt);
		for (size_t engine = 0; engine < workshop.m_engines.size(); engine++)
			engineChanges.try_emplace(EngineKey(engine), std::nullopt);
		for (const auto& [ref, visual] : world.m_items)
			itemChanges.try_emplace(ItemKey(ref), std::nullopt);
		for (const auto& [id, package] : world.m_packages)
			packageChanges.try_emplace(PackageKey(id), std::nullopt);
		for (const auto& key : world.m_paint.Keys())
			other.try_emplace(key, std::nullopt);
		for (const auto& [ref, coat] : world.m_coats)
			other.try_emplace(CoatKey(ref), std::nullopt);
	}

	// Paint, on the walls and on what's been sprayed. Taken out of the rest, which only reads what's set.
	for (auto it = other.begin(); it != other.end();)
	{
		const auto& [key, text] = *it;
		if (WallPaint::IsKey(key))
			world.m_paint.Apply(key, text);
		else if (key.starts_with("o:"))
		{
			std::string fields = key.substr(2);
			std::replace(fields.begin(), fields.end(), ':', ' ');
			Reader keyIn(fields);
			ItemRef ref;
			ref.kind = keyIn.Enumerated<ItemRef::Kind>(3);
			ref.id = static_cast<uint32_t>(keyIn.Int());
			Reader in(text ? *text : "");
			uint32_t rgb = text ? static_cast<uint32_t>(in.Int()) : 0;
			float amount = text ? static_cast<float>(in.Int()) / 100.f : 0.f;
			if (!keyIn.Failed())
			{
				if (!text || in.Failed() || amount <= 0.f)
					world.m_coats.erase(ref);
				else
					world.m_coats[ref] = { rgb, amount };
			}
		}
		else
		{
			++it;
			continue;
		}
		it = other.erase(it);
	}

	// The money, tallies and what the shop owns.
	std::optional<std::string> unseatedText;
	for (const auto& [key, text] : other)
	{
		if (!text)
			continue;
		Reader in(*text);
		if (NetProtocol::IsCounter(key))
		{
			for (const auto& counter : Counters())
				if (key == counter.key)
					counter.set(workshop, world, NetProtocol::CounterValue(*text));
		}
		else if (key == "g:best")
		{
			workshop.m_stats.bestPowerKw = in.Num();
			workshop.m_stats.bestTorqueNm = in.Num();
			workshop.m_stats.bestEngineSale = in.Num();
		}
		else if (key == "g:tools")
			ReadCounts(in, workshop.m_toolCounts, Workshop::MAX_TOOL_COPIES);
		else if (key == "g:skills")
		{
			std::vector<int> skills(workshop.m_skills.begin(), workshop.m_skills.end());
			ReadCounts(in, skills, 10);
			for (size_t i = 0; i < workshop.m_skills.size() && i < skills.size(); i++)
				workshop.m_skills[i] = (std::min)(skills[i], Skills::All()[i].maxLevel);
		}
		else if (key == "g:machines")
			ReadBools(in, workshop.m_ownedMachines);
		else if (key == "g:upgrades")
			ReadBools(in, workshop.m_upgrades);
		else if (key == "g:ownsdyno")
			workshop.m_ownsDyno = in.Int() != 0;
		else if (key == "g:ownshoist")
			workshop.m_ownsHoist = in.Int() != 0;
		else if (key == "g:furniture")
		{
			std::vector<size_t> furniture;
			size_t count = in.Count();
			for (size_t i = 0; i < count && !in.Failed(); i++)
				furniture.push_back(static_cast<size_t>(in.Int()));
			if (!in.Failed())
				workshop.m_furniture = furniture;
		}
		else if (key == "g:dyno")
			workshop.m_engineOnDyno = static_cast<int>(in.Int());
		else if (key == "g:hoist")
			workshop.m_engineOnHoist = static_cast<int>(in.Int());
		else if (key == "g:unseated")
			unseatedText = *text;
		else if (key == "g:borerings")
		{
			workshop.m_boreRings.clear();
			size_t count = in.Count();
			for (size_t i = 0; i < count && !in.Failed(); i++)
			{
				auto ring = static_cast<uint32_t>(in.Int());
				BoreRing at;
				at.block = static_cast<uint32_t>(in.Int());
				at.cylinder = static_cast<int>(in.Int());
				at.squared = in.Int() != 0;
				if (!in.Failed())
					workshop.m_boreRings[ring] = at;
			}
		}
		else if (key == "g:headfittings")
		{
			workshop.m_headFittings.clear();
			size_t count = in.Count();
			for (size_t i = 0; i < count && !in.Failed(); i++)
			{
				auto part = static_cast<uint32_t>(in.Int());
				HeadFitting at;
				at.head = static_cast<uint32_t>(in.Int());
				int slot = static_cast<int>(in.Int());
				at.index = static_cast<int>(in.Int());
				at.slot = static_cast<AssemblySlot>(slot);
				if (!in.Failed() && slot >= 0 && slot < static_cast<int>(AssemblySlot::COUNT) && (EngineAssembly::InHead(at.slot) || at.slot == AssemblySlot::ROCKER_ARM))
					workshop.m_headFittings[part] = at;
			}
		}
		else if (key == "g:shells" || key == "g:pistons" || key == "g:pins" || key == "g:rings")
		{
			auto& fittings = key == "g:shells" ? workshop.m_shells : key == "g:pistons" ? workshop.m_pistons : key == "g:pins" ? workshop.m_pins : workshop.m_rings;
			fittings.clear();
			size_t count = in.Count();
			for (size_t i = 0; i < count && !in.Failed(); i++)
			{
				auto host = static_cast<uint32_t>(in.Int());
				fittings[host] = static_cast<uint32_t>(in.Int());
			}
		}
	}

	// Parts, changed where they are so the UI and the 3D shop keep hold of the same ones.
	if (!partChanges.empty())
	{
		std::unordered_map<uint32_t, size_t> at;
		for (size_t i = 0; i < workshop.m_inventory.size(); i++)
			at[workshop.m_inventory[i]->GetId()] = i;
		std::unordered_set<uint32_t> removed;
		for (const auto& [key, text] : partChanges)
		{
			auto id = KeyNumber(key, "p:");
			if (!id)
				continue;
			auto existing = at.find(*id);
			if (!text)
			{
				if (existing != at.end())
					removed.insert(*id);
				continue;
			}
			auto part = ReadPart(*text, workshop.GetSpec());
			if (!part)
				continue;
			part->m_id = *id;
			if (existing == at.end())
			{
				at[*id] = workshop.m_inventory.size();
				workshop.m_inventory.push_back(part);
				continue;
			}
			auto& current = workshop.m_inventory[existing->second];
			// A different kind of part under the same number needs a different model.
			if (current->GetType() != part->GetType())
				world.RemoveItem(ItemRef::Part(*id));
			*current = *part;
		}
		if (!removed.empty())
			std::erase_if(workshop.m_inventory, [&](const pPart& part) { return removed.contains(part->GetId()); });
	}

	// Engines are rebuilt from their parts.
	if (!engineChanges.empty() || !partChanges.empty())
	{
		std::unordered_map<uint32_t, pPart> parts;
		for (const auto& part : workshop.m_inventory)
			parts[part->GetId()] = part;
		for (const auto& [key, text] : engineChanges)
		{
			auto engine = KeyNumber(key, "e:");
			if (!engine || *engine >= Workshop::MAX_ENGINES)
				continue;
			if (!text)
			{
				if (*engine < workshop.m_engines.size())
					workshop.m_engines.resize(*engine);
				continue;
			}
			auto build = ReadEngine(*text, workshop, parts);
			if (!build)
				continue;
			while (workshop.m_engines.size() <= *engine)
				workshop.m_engines.push_back(std::make_unique<EngineBuild>(workshop.GetSpec()));
			workshop.m_engines[*engine] = std::move(build);
		}
		// A part taken away with no change to its engine still comes off it.
		for (auto& engine : workshop.m_engines)
			for (int slot = 0; slot < SLOTS; slot++)
				for (int index = 0; index < engine->assembly.SlotCount(static_cast<AssemblySlot>(slot)); index++)
					if (auto part = engine->assembly.Get(static_cast<AssemblySlot>(slot), index); part && !parts.contains(part->GetId()))
						engine->assembly.Remove(static_cast<AssemblySlot>(slot), index);
		workshop.m_activeEngine = std::clamp(workshop.m_activeEngine, 0, (std::max)(workshop.GetEngineCount() - 1, 0));
		if (workshop.m_engineOnDyno >= workshop.GetEngineCount())
			workshop.m_engineOnDyno = -1;
		if (workshop.m_engineOnHoist >= workshop.GetEngineCount() || workshop.m_engineOnHoist == workshop.m_engineOnDyno)
			workshop.m_engineOnHoist = -1;
	}
	// The project car, rebuilt from its parts once they and the engines are in place. A part that's gone with no
	// change to the car still comes off it (rebuilt from its own state, which drops what isn't the shop's).
	if (auto car = other.find(CAR_KEY); car != other.end())
	{
		if (car->second)
			workshop.RestoreCarState(*car->second);
		else
		{
			workshop.m_car.reset();
			workshop.m_carCarried.clear();
		}
		other.erase(car);
	}
	else if (full)
	{
		workshop.m_car.reset();
		workshop.m_carCarried.clear();
	}
	else if (!partChanges.empty() && workshop.m_car)
		workshop.RestoreCarState(workshop.CarState());
	// Once the engines are what they are: which of their pistons are still in the top of the bore, and which rings
	// gapped on their own have been squared in theirs.
	if (unseatedText)
	{
		for (auto& engine : workshop.m_engines)
		{
			engine->unseated.clear();
			engine->squaredRings.clear();
		}
		Reader in(*unseatedText);
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			int engine = static_cast<int>(in.Int());
			int cylinder = static_cast<int>(in.Int());
			if (!in.Failed() && engine >= 0 && engine < workshop.GetEngineCount())
				workshop.m_engines[engine]->unseated.insert(cylinder);
		}
		count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			int engine = static_cast<int>(in.Int());
			int cylinder = static_cast<int>(in.Int());
			if (!in.Failed() && engine >= 0 && engine < workshop.GetEngineCount())
				workshop.m_engines[engine]->squaredRings.insert(cylinder);
		}
	}

	// The building and furniture first: furniture brings stations with it.
	world.SyncUpgrades(workshop);
	world.SyncFurniture(workshop);

	for (const auto& [key, text] : stationChanges)
	{
		auto index = KeyNumber(key, "s:");
		if (!index || !text || *index >= world.m_stations.size())
			continue;
		// Not the one being carried about here.
		if (world.m_move && world.m_move->stationIndex == *index)
			continue;
		// Nor the car while it's being driven here.
		if (world.m_drive.active && *index == world.m_carStation)
			continue;
		Reader in(*text);
		glm::vec3 position = ReadVec3(in);
		float yaw = static_cast<float>(in.Num());
		bool present = in.Int() != 0;
		if (in.Failed())
			continue;
		auto& station = world.m_stations[*index];
		station.position = position;
		station.yaw = yaw;
		station.present = present;
		station.object->SetPosition(position);
		station.object->SetRotation(ShopLayout::Yaw(yaw));
		station.object->SetVisible(present);
		station.object->SetPickable(present);
	}

	// Every part and tool the shop has needs its 3D item before it can be put anywhere.
	std::unordered_set<uint32_t> owned;
	for (const auto& part : workshop.m_inventory)
	{
		owned.insert(part->GetId());
		auto ref = ItemRef::Part(part->GetId());
		if (!world.m_items.contains(ref))
			world.CreateItem(ref, part, world.m_assets->PartModel(part->GetType()), world.m_assets->PartTexture(*part));
	}
	const auto& tools = ToolCatalog::All();
	for (size_t i = 0; i < tools.size(); i++)
		for (size_t copy = 0; copy < static_cast<size_t>(Workshop::MAX_TOOL_COPIES); copy++)
		{
			auto ref = ItemRef::Tool(static_cast<uint32_t>(ToolCatalog::IdOf(i, copy)));
			if (!workshop.OwnsTool(ref.id) || world.m_items.contains(ref))
				continue;
			auto model = ToolModelName(tools[i]);
			world.CreateItem(ref, nullptr, world.m_assets->Model(model), world.m_assets->TextureFor(model, ShopTextures::STEEL));
		}
	std::vector<ItemRef> gone;
	for (const auto& [ref, visual] : world.m_items)
		if ((ref.IsPart() && !owned.contains(ref.id)) || (ref.IsTool() && !workshop.OwnsTool(ref.id)))
			gone.push_back(ref);
	for (const auto& ref : gone)
		world.RemoveItem(ref);

	for (const auto& [key, text] : itemChanges)
	{
		auto ref = KeyItem(key);
		if (!ref || !text)
			continue;
		auto visual = world.Find(*ref);
		if (!visual)
			continue;
		Reader in(*text);
		auto place = in.Enumerated<ItemPlace>(PLACES);
		int index = static_cast<int>(in.Int());
		float yaw = static_cast<float>(in.Num());
		glm::vec3 position = {}, velocity = {};
		bool resting = true;
		glm::quat tilt = glm::quat(1.f, 0.f, 0.f, 0.f);
		if (place == ItemPlace::WORLD)
		{
			position = ReadVec3(in);
			velocity = ReadVec3(in);
			resting = in.Int() != 0;
			float w = static_cast<float>(in.Num()), x = static_cast<float>(in.Num()), y = static_cast<float>(in.Num()), z = static_cast<float>(in.Num());
			if (w * w + x * x + y * y + z * z > 0.5f)
				tilt = glm::normalize(glm::quat(w, x, y, z));
		}
		int carrier = -1;
		bool inHand = false;
		if (place == ItemPlace::CARRIED)
		{
			carrier = static_cast<int>(in.Int());
			inHand = in.Int() != 0;
		}
		if (in.Failed())
			continue;

		bool mine = visual->place == ItemPlace::INVENTORY || visual->place == ItemPlace::HAND;
		if (place == ItemPlace::CARRIED && carrier == localPlayer)
		{
			// Ours: it stays where it is in our backpack or hand. (Said to be ours but not here, we
			// had it when we last left: it's back in the backpack.)
			if (!mine)
				Stow(world, *visual);
			continue;
		}
		visual->yaw = yaw;
		if (place == ItemPlace::CARRIED)
		{
			if (visual->place != ItemPlace::CARRIED)
				world.SetPlace(*visual, ItemPlace::CARRIED);
			visual->carrier = carrier;
			visual->carriedInHand = inHand;
			continue;
		}
		visual->carrier = -1;
		visual->carriedInHand = false;
		if (place == ItemPlace::INVENTORY || place == ItemPlace::HAND)
			place = ItemPlace::STORAGE;
		// A bearing shell fitted into a rod or cap goes about in it (sent as stored).
		if (place == ItemPlace::STORAGE && visual->part && workshop.HostOf(*visual->part))
			place = ItemPlace::ATTACHED;
		bool samePlace = visual->place == place && (place != ItemPlace::MACHINE || visual->machineIndex == index)
			&& (place != ItemPlace::STAND || visual->engineIndex == index);
		if (!samePlace)
			world.SetPlace(*visual, place, index);
		if (place == ItemPlace::WORLD)
		{
			visual->object->SetPosition(position);
			visual->tilt = tilt;
			visual->object->SetRotation(world.RestingRotation(*visual));
			visual->object->SetScale({ 1.f, 1.f, 1.f });
			visual->body.velocity = velocity;
			visual->body.resting = resting;
		}
	}

	auto objectManager = ObjectManager::Get();
	auto removePackage = [&](uint32_t id) {
		auto it = world.m_packages.find(id);
		if (it == world.m_packages.end())
			return;
		world.m_targets.erase(it->second.object.get());
		objectManager->RemoveObject(it->second.object);
		world.m_packages.erase(it);
		world.m_remoteCarriedPackages.erase(id);
		world.m_remoteStowedPackages.erase(id);
		if (world.IsCarried(id))
		{
			world.m_inventory.Remove(ItemRef::Package(id));
			world.SyncHand();
		}
	};
	for (const auto& [key, text] : packageChanges)
	{
		auto id = KeyNumber(key, "k:");
		if (!id)
			continue;
		if (!text)
		{
			removePackage(*id);
			continue;
		}
		Reader in(*text);
		bool big = in.Int() != 0;
		int carrier = static_cast<int>(in.Int());
		glm::vec3 position = ReadVec3(in);
		glm::quat rotation = ReadQuat(in);
		glm::vec3 velocity = ReadVec3(in);
		bool resting = in.Int() != 0;
		std::string label = in.Next();
		std::vector<ItemRef> items;
		size_t itemCount = in.Count();
		for (size_t i = 0; i < itemCount && !in.Failed(); i++)
		{
			ItemRef ref;
			ref.kind = in.Enumerated<ItemRef::Kind>(3);
			ref.id = static_cast<uint32_t>(in.Int());
			items.push_back(ref);
		}
		std::vector<size_t> stations;
		size_t stationCount = in.Count();
		for (size_t i = 0; i < stationCount && !in.Failed(); i++)
		{
			size_t station = static_cast<size_t>(in.Int());
			if (auto now = renumber(station))
				stations.push_back(*now);
		}
		// Older records don't say: carried meant in their arms.
		bool inHand = in.AtEnd() || in.Int() != 0;
		if (in.Failed())
			continue;

		// Our own delivery, in our arms or on our hotbar, stays there.
		if (carrier == localPlayer && world.IsCarried(*id))
			continue;
		auto it = world.m_packages.find(*id);
		if (it == world.m_packages.end())
		{
			auto object = world.Spawn(big ? ShopModels::PACKAGE_CRATE : ShopModels::PACKAGE_BOX, big ? ShopTextures::WOOD : ShopTextures::CARDBOARD, position);
			Package package = { *id, object, {}, {}, label, {} };
			world.m_targets[object.get()] = { ShopWorld::TargetKind::PACKAGE, {}, *id, 0 };
			it = world.m_packages.emplace(*id, package).first;
		}
		auto& package = it->second;
		// Someone else took the one we were carrying.
		if (world.IsCarried(*id))
		{
			world.m_inventory.Remove(ItemRef::Package(*id));
			world.SyncHand();
			package.object->SetVisible(true);
		}
		package.label = label;
		package.items = items;
		package.stations = stations;
		package.object->SetPosition(position);
		package.object->SetRotation(rotation);
		package.body.velocity = velocity;
		package.body.resting = resting;
		bool remote = carrier >= 0 && carrier != localPlayer;
		if (remote)
			world.m_remoteCarriedPackages[*id] = carrier;
		else
			world.m_remoteCarriedPackages.erase(*id);
		// One they've put away on their hotbar isn't seen.
		if (remote && !inHand)
			world.m_remoteStowedPackages.insert(*id);
		else
			world.m_remoteStowedPackages.erase(*id);
		package.object->SetVisible(!remote || inHand);
		package.object->SetPickable(!remote);
		package.object->SetCastsShadows(!remote);
		// Crated equipment stays out of the shop until it's unpacked.
		for (size_t station : stations)
			if (station < world.m_stations.size())
			{
				auto& crated = world.m_stations[station];
				crated.present = false;
				crated.object->SetVisible(false);
				crated.object->SetPickable(false);
			}
	}

	for (const auto& [key, text] : other)
	{
		if (!text)
			continue;
		Reader in(*text);
		if (key == "w:doors")
		{
			bool open = in.Int() != 0;
			bool bayOpen = in.Int() != 0;
			if (in.Failed())
				continue;
			if (open != world.m_doorOpen)
				world.SetDoorOpen(open);
			world.m_bayDoorOpen = bayOpen;
		}
		else if (key == "w:lift")
			world.m_liftRaised = in.Int() != 0;
		else if (key == "w:sockets")
		{
			std::map<size_t, int> sizes;
			size_t count = in.Count();
			for (size_t i = 0; i < count && !in.Failed(); i++)
			{
				auto tool = static_cast<size_t>(in.Int());
				sizes[tool] = static_cast<int>(in.Int());
			}
			if (!in.Failed())
				world.m_toolSizes = sizes;
		}
		else if (key == "w:batteries")
		{
			std::map<size_t, BatteryPack> batteries;
			size_t count = in.Count();
			for (size_t i = 0; i < count && !in.Failed(); i++)
			{
				auto tool = static_cast<size_t>(in.Int());
				int percent = static_cast<int>(in.Int());
				auto& battery = batteries[tool];
				battery.on = static_cast<int>(in.Int());
				// Kept as it is here when it's the same to the percent, so charging carries on smoothly.
				auto here = world.m_batteries.find(tool);
				bool same = here != world.m_batteries.end() && static_cast<int>(std::round(here->second.charge * 100.f)) == percent;
				battery.charge = same ? here->second.charge : std::clamp(percent / 100.f, 0.f, 1.f);
			}
			if (!in.Failed())
				world.m_batteries = batteries;
		}
		else if (key == "w:toolspots")
		{
			std::map<size_t, glm::vec2> spots;
			size_t count = in.Count();
			for (size_t i = 0; i < count && !in.Failed(); i++)
			{
				auto tool = static_cast<size_t>(in.Int());
				float x = static_cast<float>(in.Int()) / 1000.f;
				spots[tool] = { x, static_cast<float>(in.Int()) / 1000.f };
			}
			if (!in.Failed())
				world.m_toolSpots = spots;
		}
		else if (key == "w:engines")
		{
			for (size_t engine = 0; engine < world.m_engineRolls.size() && !in.Failed(); engine++)
			{
				float roll = static_cast<float>(in.Num());
				float crank = static_cast<float>(in.Num());
				if (in.Failed())
					break;
				world.m_engineRolls[engine] = roll;
				// The cam follows however far the crank's been turned, with the timing belt on.
				float turned = std::remainder(crank - world.m_crankAngles[engine], 4.f * glm::pi<float>());
				world.TurnCrank(workshop, static_cast<int>(engine), turned);
				world.m_crankAngles[engine] = crank;
			}
		}
	}

	// Whatever someone else took out of this player's hands, or off the hotbar, is gone from it.
	world.SyncHand();
	world.m_supportChanged = true;
}

void NetState::ClaimIds(const Workshop& workshop, ShopWorld& world, int slot)
{
	uint32_t low = static_cast<uint32_t>(slot) * ID_BAND;
	uint32_t high = low + ID_BAND;
	uint32_t nextPart = (std::max)(low, 1u);
	for (const auto& part : workshop.m_inventory)
		if (part->GetId() >= low && part->GetId() < high)
			nextPart = (std::max)(nextPart, part->GetId() + 1);
	Part::s_nextId = nextPart;

	uint32_t nextPackage = (std::max)(low, 1u);
	for (const auto& [id, package] : world.m_packages)
		if (id >= low && id < high)
			nextPackage = (std::max)(nextPackage, id + 1);
	world.m_nextPackageId = nextPackage;
}

void NetState::Stow(ShopWorld& world, ItemVisual& visual)
{
	if (visual.place != ItemPlace::INVENTORY && visual.place != ItemPlace::HAND)
		world.SetPlace(visual, ItemPlace::INVENTORY);
	visual.carrier = -1;
	visual.carriedInHand = false;
	if (world.m_inventory.Contains(visual.ref))
		return;
	glm::ivec2 size = world.GridSize(visual);
	if (PlayerInventory::CanEverFit(size) && world.m_inventory.AddToBackpack(visual.ref, size))
		return;
	if (auto slot = world.m_inventory.FreeHotbarSlot())
	{
		world.m_inventory.PutInHotbar(*slot, visual.ref);
		return;
	}
	world.SetPlace(visual, ItemPlace::STORAGE);
}

std::string NetState::CaptureLayout(const ShopWorld& world)
{
	// A delivery on the hotbar is put down by the server when this player leaves: its slot comes back empty.
	auto writeRef = [](Writer& out, const ItemRef& ref) {
		ItemRef kept = ref.IsPackage() ? ItemRef{} : ref;
		out.Int(static_cast<int>(kept.kind)).Int(kept.id);
	};
	Writer out;
	const auto& grid = world.m_inventory.GetItems();
	out.Int(grid.size());
	for (const auto& entry : grid)
	{
		writeRef(out, entry.item);
		out.Int(entry.position.x).Int(entry.position.y).Int(entry.size.x).Int(entry.size.y);
	}
	for (int slot = 0; slot < PlayerInventory::HOTBAR_SLOTS; slot++)
		writeRef(out, world.m_inventory.GetHotbar(slot));
	out.Int(world.m_inventory.GetSelected());
	return out.Text();
}

void NetState::RestoreLayout(ShopWorld& world, const std::string& layout)
{
	auto readRef = [](Reader& in) {
		ItemRef ref;
		ref.kind = in.Enumerated<ItemRef::Kind>(3);
		ref.id = static_cast<uint32_t>(in.Int());
		return ref;
	};
	std::vector<GridItem> grid;
	std::array<ItemRef, PlayerInventory::HOTBAR_SLOTS> hotbar = {};
	int selected = 0;
	if (!layout.empty())
	{
		Reader in(layout);
		grid.resize(in.Count());
		for (auto& entry : grid)
		{
			entry.item = readRef(in);
			entry.position = { static_cast<int>(in.Int()), static_cast<int>(in.Int()) };
			entry.size = { static_cast<int>(in.Int()), static_cast<int>(in.Int()) };
		}
		for (auto& slot : hotbar)
			slot = readRef(in);
		selected = static_cast<int>(in.Int());
		if (in.Failed())
		{
			grid.clear();
			hotbar = {};
			selected = 0;
		}
	}

	// Only what this player actually has: anything since taken away is left out.
	auto carried = [&](const ItemRef& ref) {
		auto visual = world.Find(ref);
		return visual && (visual->place == ItemPlace::INVENTORY || visual->place == ItemPlace::HAND);
	};
	std::erase_if(grid, [&](const GridItem& entry) { return !carried(entry.item); });
	for (auto& slot : hotbar)
		if (!carried(slot))
			slot = {};
	world.m_inventory.Restore(grid, hotbar, std::clamp(selected, 0, PlayerInventory::HOTBAR_SLOTS - 1));

	// Everything carried put away (wherever it fits, if the layout didn't place it), then the
	// selected slot's item taken in hand.
	for (auto& [ref, visual] : world.m_items)
		if (visual.place == ItemPlace::INVENTORY || visual.place == ItemPlace::HAND)
		{
			Stow(world, visual);
			if (visual.place == ItemPlace::HAND)
				world.SetPlace(visual, ItemPlace::INVENTORY);
		}
	world.m_equipped = {};
	world.SyncHand();
	world.m_supportChanged = true;
}
