#include "SaveGame.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>

#include "LV/Engine/Objects/ObjectManager.h"
#include "../Simulation/Workshop.h"
#include "../World/FirstPersonController.h"
#include "../World/ShopWorld.h"
#include "../World/ShopWorldShared.h"

namespace
{
	constexpr int VERSION = 1;
	constexpr const char* MAGIC = "LVSAVE";

	// Writes whitespace-separated tokens; strings are quoted so they can hold spaces.
	class Writer
	{
	public:
		Writer& Word(const std::string& word) { m_out << word << ' '; return *this; }
		Writer& Num(double value) { m_out << std::format("{}", value) << ' '; return *this; }
		Writer& Int(long long value) { m_out << value << ' '; return *this; }
		Writer& Str(const std::string& text)
		{
			m_out << '"';
			for (char c : text)
			{
				if (c == '"' || c == '\\')
					m_out << '\\' << c;
				else if (c == '\n')
					m_out << "\\n";
				else
					m_out << c;
			}
			m_out << "\" ";
			return *this;
		}
		Writer& Line() { m_out << '\n'; return *this; }
		std::string Text() const { return m_out.str(); }
	private:
		std::ostringstream m_out;
	};

	// Reads the tokens back. Any mismatch marks the reader failed and every later read returns zeros,
	// so parsing code can read straight through and check once at the end.
	class Reader
	{
	public:
		explicit Reader(const std::string& text)
		{
			size_t i = 0;
			while (i < text.size())
			{
				if (std::isspace(static_cast<unsigned char>(text[i])))
				{
					i++;
					continue;
				}
				std::string token;
				if (text[i] == '"')
				{
					i++;
					while (i < text.size() && text[i] != '"')
					{
						if (text[i] == '\\' && i + 1 < text.size())
						{
							i++;
							token += text[i] == 'n' ? '\n' : text[i];
						}
						else
							token += text[i];
						i++;
					}
					i++;
				}
				else
					while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
						token += text[i++];
				m_tokens.push_back(token);
			}
		}

		std::string Next()
		{
			if (m_failed || m_position >= m_tokens.size())
			{
				Fail("the file ends early");
				return "";
			}
			return m_tokens[m_position++];
		}
		void Expect(const std::string& word)
		{
			auto token = Next();
			if (token != word)
				Fail(std::format("expected '{}' but found '{}'", word, token));
		}
		double Num()
		{
			auto token = Next();
			try { return m_failed ? 0.0 : std::stod(token); }
			catch (...) { Fail(std::format("'{}' isn't a number", token)); return 0.0; }
		}
		long long Int()
		{
			auto token = Next();
			try { return m_failed ? 0 : std::stoll(token); }
			catch (...) { Fail(std::format("'{}' isn't a whole number", token)); return 0; }
		}
		// A count of following records, bounded so a damaged file can't ask for billions.
		size_t Count()
		{
			auto count = Int();
			if (count < 0 || count > 100000)
				Fail("a record count is out of range");
			return m_failed ? 0 : static_cast<size_t>(count);
		}
		template <typename Enum>
		Enum Enumerated(int count)
		{
			auto value = Int();
			if (value < 0 || value >= count)
				Fail("a value is out of range");
			return static_cast<Enum>(m_failed ? 0 : value);
		}
		void Fail(const std::string& why)
		{
			if (!m_failed)
				m_error = why;
			m_failed = true;
		}
		bool Failed() const { return m_failed; }
		const std::string& Error() const { return m_error; }
	private:
		std::vector<std::string>	m_tokens = {};
		size_t						m_position = 0;
		bool						m_failed = false;
		std::string					m_error = "";
	};

	constexpr int PART_TYPES = static_cast<int>(PartType::VALVE_SPRING) + 1;
	constexpr int MACHINE_OPERATIONS = static_cast<int>(MachineOperation::FACE_VALVE) + 1;
	constexpr int ITEM_PLACES = static_cast<int>(ItemPlace::INVENTORY) + 1;

	void WriteItemRef(Writer& out, const ItemRef& ref)
	{
		out.Int(static_cast<int>(ref.kind)).Int(ref.id);
	}

	ItemRef ReadItemRef(Reader& in)
	{
		ItemRef ref;
		ref.kind = in.Enumerated<ItemRef::Kind>(3);
		ref.id = static_cast<uint32_t>(in.Int());
		return ref;
	}

	// Everything about the 3D shop, read in full before any of it is applied.
	struct SavedStation { glm::vec3 position; float yaw; bool present; };
	struct SavedItem { ItemRef ref; ItemPlace place; int machineIndex; glm::vec3 position; float yaw; };
	struct SavedPackage { uint32_t id; bool big; glm::vec3 position; float yaw; std::string label; std::vector<uint32_t> partIds; };
	struct SavedWorld
	{
		std::vector<SavedStation> stations;
		std::vector<SavedItem> items;
		std::vector<SavedPackage> packages;
		uint32_t nextPackageId = 1;
		uint32_t deliveries = 0;
		std::vector<GridItem> grid;
		std::array<ItemRef, PlayerInventory::HOTBAR_SLOTS> hotbar = {};
		ItemRef equipped;
		float engineRoll = 0.f;
		float crankAngle = 0.f;
		bool doorOpen = true;
		glm::vec3 playerPosition = {};
		float playerYaw = 0.f;
		float playerPitch = 0.f;
	};

	glm::vec3 ReadVec3(Reader& in)
	{
		float x = static_cast<float>(in.Num());
		float y = static_cast<float>(in.Num());
		float z = static_cast<float>(in.Num());
		return { x, y, z };
	}
}

bool SaveGame::Exists()
{
	std::error_code error;
	return std::filesystem::exists(FILE_NAME, error);
}

std::string SaveGame::LastSavedText()
{
	std::error_code error;
	auto written = std::filesystem::last_write_time(FILE_NAME, error);
	if (error)
		return "";
	auto system = std::chrono::clock_cast<std::chrono::system_clock>(written);
	auto local = std::chrono::current_zone()->to_local(std::chrono::floor<std::chrono::minutes>(system));
	return std::format("{:%b %d, %H:%M}", local);
}

bool SaveGame::Save(const Workshop& workshop, const ShopWorld& world, const FirstPersonController& player, float timeOfDay, std::string& error)
{
	Writer out;
	out.Word(MAGIC).Int(VERSION).Line();
	out.Word("time").Num(timeOfDay).Line();
	out.Word("money").Num(workshop.m_money).Word("hours").Num(workshop.m_hours).Line();

	out.Word("log").Int(workshop.m_log.size()).Line();
	for (const auto& line : workshop.m_log)
		out.Str(line).Line();

	out.Word("tools").Int(workshop.m_ownedTools.size());
	for (bool owned : workshop.m_ownedTools)
		out.Int(owned);
	out.Line();
	out.Word("machines").Int(workshop.m_ownedMachines.size());
	for (bool owned : workshop.m_ownedMachines)
		out.Int(owned);
	out.Line();

	// Parts: who they are, then every feature's true geometry, then what the player measured.
	out.Word("parts").Int(workshop.m_inventory.size()).Line();
	for (const auto& part : workshop.m_inventory)
	{
		out.Word("part").Int(part->m_id).Int(static_cast<int>(part->m_type)).Int(static_cast<int>(part->m_quality))
			.Int(static_cast<int>(part->m_condition)).Int(static_cast<int>(part->m_material))
			.Num(part->m_sizeOffset).Num(part->m_value).Str(part->m_name).Str(part->m_damageReason).Line();

		out.Word("features").Int(part->m_features.size()).Line();
		for (const auto& feature : part->m_features)
		{
			out.Str(feature.name);
			for (const auto& station : feature.samples)
				for (double sample : station)
					out.Num(sample);
			out.Num(feature.value).Num(feature.surfaceRa).Int(feature.lastOperation ? static_cast<int>(*feature.lastOperation) : -1).Line();
		}

		out.Word("readings").Int(part->m_readings.size()).Line();
		for (const auto& [name, reading] : part->m_readings)
		{
			out.Str(name).Str(reading.toolName).Int(reading.hasSize);
			for (const auto& station : reading.samples)
				for (double sample : station)
					out.Num(sample);
			out.Num(reading.value).Int(reading.finishRa.has_value()).Num(reading.finishRa.value_or(0.0)).Int(reading.stale).Line();
		}
	}

	// The engine on the stand, by part id.
	std::vector<std::array<long long, 3>> installed;
	for (int slot = 0; slot < static_cast<int>(AssemblySlot::COUNT); slot++)
		for (int index = 0; index < workshop.m_assembly.SlotCount(static_cast<AssemblySlot>(slot)); index++)
			if (auto part = workshop.m_assembly.Get(static_cast<AssemblySlot>(slot), index))
				installed.push_back({ slot, index, part->GetId() });
	out.Word("assembly").Int(installed.size()).Line();
	for (const auto& [slot, index, id] : installed)
		out.Int(slot).Int(index).Int(id).Line();

	out.Word("assembly_readings").Int(workshop.m_assemblyReadings.size()).Line();
	for (const auto& [label, reading] : workshop.m_assemblyReadings)
		out.Str(label).Num(reading.value).Int(reading.stale).Line();

	const auto& dyno = workshop.m_lastDyno;
	out.Word("dyno").Int(dyno.ran).Int(dyno.failed).Num(dyno.failureRpm).Num(dyno.peakTorqueNm).Num(dyno.peakTorqueRpm)
		.Num(dyno.peakPowerKw).Num(dyno.peakPowerRpm).Num(dyno.oilPressureBar).Num(dyno.blowbyPercent).Num(dyno.compressionRatio)
		.Num(dyno.displacementCc).Num(dyno.healthScore).Int(workshop.m_dynoCurrent).Line();
	out.Word("curve").Int(dyno.curve.size());
	for (const auto& point : dyno.curve)
		out.Num(point.rpm).Num(point.torqueNm).Num(point.powerKw);
	out.Line();
	out.Word("findings").Int(dyno.findings.size()).Line();
	for (const auto& finding : dyno.findings)
		out.Int(static_cast<int>(finding.severity)).Str(finding.message).Line();

	// The 3D shop
	out.Word("stations").Int(world.m_stations.size()).Line();
	for (const auto& station : world.m_stations)
		out.Num(station.position.x).Num(station.position.y).Num(station.position.z).Num(station.yaw).Int(station.present).Line();

	out.Word("items").Int(world.m_items.size()).Line();
	for (const auto& [ref, visual] : world.m_items)
	{
		WriteItemRef(out, ref);
		glm::vec3 position = visual.object->GetPosition();
		out.Int(static_cast<int>(visual.place)).Int(visual.machineIndex).Num(position.x).Num(position.y).Num(position.z).Num(visual.yaw).Line();
	}

	out.Word("packages").Int(world.m_packages.size()).Int(world.m_nextPackageId).Int(world.m_deliveries).Line();
	for (const auto& [id, package] : world.m_packages)
	{
		glm::vec3 position = package.object->GetPosition();
		bool big = package.object->GetModelIndex() == world.m_assets->Model(ShopModels::PACKAGE_CRATE);
		glm::quat rotation = package.object->GetRotation();
		float yaw = 2.f * std::atan2(rotation.y, rotation.w);	// packages only ever turn about Y
		out.Int(id).Int(big).Num(position.x).Num(position.y).Num(position.z).Num(yaw).Str(package.label).Int(package.partIds.size());
		for (uint32_t partId : package.partIds)
			out.Int(partId);
		out.Line();
	}

	const auto& grid = world.m_inventory.GetItems();
	out.Word("backpack").Int(grid.size()).Line();
	for (const auto& entry : grid)
	{
		WriteItemRef(out, entry.item);
		out.Int(entry.position.x).Int(entry.position.y).Int(entry.size.x).Int(entry.size.y).Line();
	}
	out.Word("hotbar");
	for (int slot = 0; slot < PlayerInventory::HOTBAR_SLOTS; slot++)
		WriteItemRef(out, world.m_inventory.GetHotbar(slot));
	out.Line();
	out.Word("equipped");
	WriteItemRef(out, world.m_equipped);
	out.Line();

	out.Word("engine").Num(world.m_engineRoll).Num(world.m_crankAngle).Line();
	out.Word("door").Int(world.m_doorOpen).Line();
	glm::vec3 feet = player.GetFeetPosition();
	out.Word("player").Num(feet.x).Num(feet.y).Num(feet.z).Num(player.GetYaw()).Num(player.GetPitch()).Line();
	out.Word("end").Line();

	// Write beside the old save and swap it in, so a crash mid-write can't lose both.
	std::string temporary = std::string(FILE_NAME) + ".tmp";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			error = "Couldn't write the save file.";
			return false;
		}
		file << out.Text();
		if (!file)
		{
			error = "Couldn't finish writing the save file.";
			return false;
		}
	}
	std::error_code fileError;
	std::filesystem::rename(temporary, FILE_NAME, fileError);
	if (fileError)
	{
		error = "Couldn't replace the old save: " + fileError.message();
		return false;
	}
	return true;
}

bool SaveGame::Load(Workshop& workshop, ShopWorld& world, FirstPersonController& player, float& timeOfDay, std::string& error)
{
	std::ifstream file(FILE_NAME, std::ios::binary);
	if (!file)
	{
		error = "There's no saved game.";
		return false;
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	Reader in(buffer.str());

	in.Expect(MAGIC);
	if (in.Int() != VERSION && !in.Failed())
		in.Fail("it was saved by a different version of the game");

	in.Expect("time");
	double savedTime = in.Num();
	in.Expect("money");
	double money = in.Num();
	in.Expect("hours");
	double hours = in.Num();

	in.Expect("log");
	std::vector<std::string> log(in.Count());
	for (auto& line : log)
		line = in.Next();

	std::vector<bool> tools(ToolCatalog::All().size(), false);
	in.Expect("tools");
	size_t toolCount = in.Count();
	for (size_t i = 0; i < toolCount; i++)
	{
		bool owned = in.Int() != 0;
		if (i < tools.size())
			tools[i] = owned;
	}
	std::vector<bool> machines(MachineCatalog::All().size(), false);
	in.Expect("machines");
	size_t machineCount = in.Count();
	for (size_t i = 0; i < machineCount; i++)
	{
		bool owned = in.Int() != 0;
		if (i < machines.size())
			machines[i] = owned;
	}

	// Each part is rebuilt by the factory (which knows its specs and tolerances) and then given
	// back its saved geometry, history and readings.
	const auto& spec = workshop.GetSpec();
	std::vector<pPart> parts;
	std::map<uint32_t, pPart> partsById;
	uint32_t highestId = 0;
	in.Expect("parts");
	size_t partCount = in.Count();
	for (size_t i = 0; i < partCount && !in.Failed(); i++)
	{
		in.Expect("part");
		auto id = static_cast<uint32_t>(in.Int());
		auto type = in.Enumerated<PartType>(PART_TYPES);
		auto quality = in.Enumerated<PartQuality>(3);
		auto condition = in.Enumerated<PartCondition>(3);
		auto material = static_cast<MaterialType>(in.Int());
		double sizeOffset = in.Num();
		double value = in.Num();
		std::string name = in.Next();
		std::string damage = in.Next();
		if (in.Failed())
			break;

		auto part = PartFactory::Create(type, spec, quality, condition == PartCondition::DAMAGED ? PartCondition::USED : condition, sizeOffset);
		part->m_id = id;
		part->m_condition = condition;
		part->m_material = material;
		part->m_sizeOffset = sizeOffset;
		part->m_value = value;
		part->m_name = name;
		part->m_damageReason = damage;

		in.Expect("features");
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

			// A feature the game no longer has is skipped rather than failing the whole load.
			if (auto feature = part->FindFeature(featureName))
			{
				std::copy(&samples[0][0], &samples[0][0] + Feature::STATIONS * Feature::AXES, &feature->samples[0][0]);
				feature->value = featureValue;
				feature->surfaceRa = ra;
				feature->lastOperation = lastOperation >= 0 ? std::optional(static_cast<MachineOperation>(lastOperation)) : std::nullopt;
			}
		}

		in.Expect("readings");
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

		if (partsById.contains(id))
			in.Fail("two parts share an id");
		parts.push_back(part);
		partsById[id] = part;
		highestId = (std::max)(highestId, id);
	}

	std::vector<std::array<long long, 3>> installed;
	in.Expect("assembly");
	size_t installedCount = in.Count();
	for (size_t i = 0; i < installedCount && !in.Failed(); i++)
	{
		long long slot = in.Int(), index = in.Int(), id = in.Int();
		if (slot < 0 || slot >= static_cast<long long>(AssemblySlot::COUNT) || !partsById.contains(static_cast<uint32_t>(id)))
			in.Fail("the engine on the stand refers to a missing part");
		installed.push_back({ slot, index, id });
	}

	std::map<std::string, AssemblyReading> assemblyReadings;
	in.Expect("assembly_readings");
	size_t assemblyReadingCount = in.Count();
	for (size_t i = 0; i < assemblyReadingCount && !in.Failed(); i++)
	{
		std::string label = in.Next();
		AssemblyReading reading;
		reading.value = in.Num();
		reading.stale = in.Int() != 0;
		assemblyReadings[label] = reading;
	}

	DynoResult dyno;
	in.Expect("dyno");
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
	bool dynoCurrent = in.Int() != 0;
	in.Expect("curve");
	dyno.curve.resize(in.Count());
	for (auto& point : dyno.curve)
	{
		point.rpm = static_cast<float>(in.Num());
		point.torqueNm = static_cast<float>(in.Num());
		point.powerKw = static_cast<float>(in.Num());
	}
	in.Expect("findings");
	dyno.findings.resize(in.Count());
	for (auto& finding : dyno.findings)
	{
		finding.severity = in.Enumerated<FindingSeverity>(3);
		finding.message = in.Next();
	}

	SavedWorld saved;
	in.Expect("stations");
	saved.stations.resize(in.Count());
	for (auto& station : saved.stations)
	{
		station.position = ReadVec3(in);
		station.yaw = static_cast<float>(in.Num());
		station.present = in.Int() != 0;
	}
	if (!in.Failed() && saved.stations.size() != world.m_stations.size())
		in.Fail("the shop's equipment list has changed since this was saved");

	in.Expect("items");
	saved.items.resize(in.Count());
	for (auto& item : saved.items)
	{
		item.ref = ReadItemRef(in);
		item.place = in.Enumerated<ItemPlace>(ITEM_PLACES);
		item.machineIndex = static_cast<int>(in.Int());
		item.position = ReadVec3(in);
		item.yaw = static_cast<float>(in.Num());
	}

	in.Expect("packages");
	saved.packages.resize(in.Count());
	saved.nextPackageId = static_cast<uint32_t>(in.Int());
	saved.deliveries = static_cast<uint32_t>(in.Int());
	for (auto& package : saved.packages)
	{
		package.id = static_cast<uint32_t>(in.Int());
		package.big = in.Int() != 0;
		package.position = ReadVec3(in);
		package.yaw = static_cast<float>(in.Num());
		package.label = in.Next();
		package.partIds.resize(in.Count());
		for (auto& partId : package.partIds)
			partId = static_cast<uint32_t>(in.Int());
	}

	in.Expect("backpack");
	saved.grid.resize(in.Count());
	for (auto& entry : saved.grid)
	{
		entry.item = ReadItemRef(in);
		entry.position = { static_cast<int>(in.Int()), static_cast<int>(in.Int()) };
		entry.size = { static_cast<int>(in.Int()), static_cast<int>(in.Int()) };
	}
	in.Expect("hotbar");
	for (auto& slot : saved.hotbar)
		slot = ReadItemRef(in);
	in.Expect("equipped");
	saved.equipped = ReadItemRef(in);

	in.Expect("engine");
	saved.engineRoll = static_cast<float>(in.Num());
	saved.crankAngle = static_cast<float>(in.Num());
	in.Expect("door");
	saved.doorOpen = in.Int() != 0;
	in.Expect("player");
	saved.playerPosition = ReadVec3(in);
	saved.playerYaw = static_cast<float>(in.Num());
	saved.playerPitch = static_cast<float>(in.Num());
	in.Expect("end");

	if (in.Failed())
	{
		error = "The save couldn't be read: " + in.Error() + ".";
		return false;
	}

	// Everything parsed: now replace the career.
	workshop.m_money = money;
	workshop.m_hours = hours;
	workshop.m_log = log;
	workshop.m_ownedTools = tools;
	workshop.m_ownedMachines = machines;
	workshop.m_inventory = parts;
	workshop.m_assembly.Clear();
	for (const auto& [slot, index, id] : installed)
	{
		std::string installError;
		workshop.m_assembly.Install(static_cast<AssemblySlot>(slot), static_cast<int>(index), partsById.at(static_cast<uint32_t>(id)), installError);
	}
	workshop.m_assemblyReadings = assemblyReadings;
	workshop.m_lastDyno = dyno;
	workshop.m_dynoCurrent = dynoCurrent;
	Part::s_nextId = (std::max)(Part::s_nextId, highestId + 1);

	ResetWorld(workshop, world);

	for (size_t i = 0; i < saved.stations.size(); i++)
	{
		auto& station = world.m_stations[i];
		station.position = saved.stations[i].position;
		station.yaw = saved.stations[i].yaw;
		station.present = saved.stations[i].present;
		station.object->SetVisible(station.present);
		station.object->SetPickable(station.present);
	}

	for (const auto& item : saved.items)
	{
		auto visual = world.Find(item.ref);
		if (!visual)
			continue;
		visual->place = item.place;
		visual->machineIndex = item.place == ItemPlace::MACHINE ? item.machineIndex : -1;
		visual->yaw = item.yaw;
		if (item.place == ItemPlace::WORLD)
		{
			// Let it settle again from where it was.
			visual->object->SetPosition(item.position + glm::vec3(0.f, 0.01f, 0.f));
			visual->object->SetRotation(ShopLayout::Yaw(item.yaw));
			visual->body = { {}, false };
		}
		// The engine stand decides what's on it.
		if (item.place == ItemPlace::STAND && !workshop.m_assembly.Contains(visual->part))
			visual->place = ItemPlace::STORAGE;
	}

	for (const auto& saved_package : saved.packages)
	{
		std::vector<uint32_t> ids;
		for (uint32_t id : saved_package.partIds)
			if (auto visual = world.Find(ItemRef::Part(id)))
			{
				visual->place = ItemPlace::PACKAGED;
				ids.push_back(id);
			}
		if (ids.empty())
			continue;
		auto object = saved_package.big ? world.Spawn(ShopModels::PACKAGE_CRATE, ShopTextures::WOOD, saved_package.position, saved_package.yaw)
			: world.Spawn(ShopModels::PACKAGE_BOX, ShopTextures::CARDBOARD, saved_package.position, saved_package.yaw);
		object->SetPickable(true);
		Package package = { saved_package.id, object, ids, saved_package.label, { {}, false } };
		world.m_targets[object.get()] = { ShopWorld::TargetKind::PACKAGE, {}, package.id, 0 };
		world.m_packages[package.id] = package;
	}
	world.m_nextPackageId = (std::max)(saved.nextPackageId, 1u);
	world.m_deliveries = saved.deliveries;

	// Only keep backpack entries for items that exist and are actually carried.
	std::vector<GridItem> grid;
	for (const auto& entry : saved.grid)
		if (auto visual = world.Find(entry.item); visual && (visual->place == ItemPlace::INVENTORY || visual->place == ItemPlace::HAND))
			grid.push_back(entry);
	auto hotbar = saved.hotbar;
	for (auto& slot : hotbar)
		if (std::none_of(grid.begin(), grid.end(), [&](const GridItem& entry) { return entry.item == slot; }))
			slot = {};
	world.m_inventory.Restore(grid, hotbar);
	for (auto& [ref, visual] : world.m_items)
		if ((visual.place == ItemPlace::INVENTORY || visual.place == ItemPlace::HAND) && !world.m_inventory.Contains(ref))
			visual.place = ItemPlace::STORAGE;

	auto equipped = world.Find(saved.equipped);
	world.m_equipped = equipped && equipped->place == ItemPlace::HAND ? saved.equipped : ItemRef{};
	for (auto& [ref, visual] : world.m_items)
		if (visual.place == ItemPlace::HAND && ref != world.m_equipped)
			visual.place = ItemPlace::INVENTORY;

	world.m_engineRoll = saved.engineRoll;
	world.m_crankAngle = saved.crankAngle;
	world.SetDoorOpen(saved.doorOpen, true);
	world.RebuildCollision();

	player.SetPosition(saved.playerPosition);
	player.SetYaw(saved.playerYaw);
	player.SetPitch(saved.playerPitch);
	timeOfDay = static_cast<float>(savedTime);
	return true;
}

void SaveGame::StartNew(Workshop& workshop, ShopWorld& world, FirstPersonController& player)
{
	ResetWorld(workshop, world);
	for (auto& station : world.m_stations)
	{
		station.position = station.homePosition;
		station.yaw = station.homeYaw;
		station.present = station.type != StationType::MACHINE || workshop.OwnsMachine(station.machineIndex);
		station.object->SetVisible(station.present);
		station.object->SetPickable(station.present);
	}
	world.m_engineRoll = 0.f;
	world.m_crankAngle = 0.f;
	world.m_deliveries = 0;
	world.SetDoorOpen(true, true);
	world.RebuildCollision();

	player.SetPosition(world.GetSpawnPosition());
	player.SetYaw(world.GetSpawnYaw());
	player.SetPitch(0.f);
}

void SaveGame::ResetWorld(Workshop& workshop, ShopWorld& world)
{
	auto objectManager = ObjectManager::Get();
	std::vector<ItemRef> refs;
	for (const auto& [ref, visual] : world.m_items)
		refs.push_back(ref);
	for (const auto& ref : refs)
		world.RemoveItem(ref);
	for (auto& [id, package] : world.m_packages)
	{
		world.m_targets.erase(package.object.get());
		objectManager->RemoveObject(package.object);
	}
	world.m_packages.clear();
	world.m_inventory.Restore({}, {});
	world.m_equipped = {};
	world.m_move.reset();
	world.m_nextPackageId = 1;

	// Lay out the workshop's parts and tools as if the shop had just opened: nothing is delivered.
	world.m_initialSyncDone = false;
	world.SyncItems(workshop);
	world.m_seenLogSize = workshop.GetLog().size();
}
