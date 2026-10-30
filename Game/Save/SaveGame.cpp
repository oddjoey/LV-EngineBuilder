#include "SaveGame.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <utility>
#include <sstream>

#include "LV/Engine/Objects/ObjectManager.h"
#include "../Simulation/Furniture.h"
#include "../Simulation/PartCatalog.h"
#include "../Simulation/Workshop.h"
#include "../World/FirstPersonController.h"
#include "../World/ShopWorld.h"
#include "../World/ShopWorldShared.h"

namespace
{
	// 2: several engines, one per engine stand. 3: tools and equipment delivered, no pallet rack.
	// 4: the player's career stats. 5: rod and main caps, lower bearing shells, and which part a
	// cap was machined with. 6: a second parts shelf. 7: the line boring machine. 8: nuts and bolts,
	// with how tight each one is, and hand tools. 13: shop upgrades and the lift. 14: the hotbar holds
	// items itself (not the backpack), and which slot is selected. 15: cam caps, a slot of their own ahead of
	// the nuts and bolts (which moved up one). 16: no more M9 or M11 sizes (rod nuts are M8, head
	// bolts M12). 17: the 1.6 litre LV-16 (shorter stroke, longer rods, smaller chambers), and which
	// engine is on the dyno. 18: the LV-16 is 81 x 77 mm (bores, pistons, rings, stroke and rods resized),
	// and its timing drive is a belt. 19: the house, and its bed (a station after the lift). 20: the
	// extension's roll-up door (a station after the bed).
	constexpr int VERSION = 20;
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

	constexpr int PART_TYPES = static_cast<int>(PartType::COUNT);

	// Before version 9 every nut and bolt had a part type (and a slot) of its own job, and the head
	// bolt sat among the ancillaries. These map the old numbers to the sizes that replaced them.
	constexpr int OLD_HEAD_BOLTS = static_cast<int>(PartType::VALVE_STEM_SEAL);
	constexpr int OLD_MAIN_CAP = static_cast<int>(PartType::MAIN_CAP) + 1;

	PartType OldPartType(long long old, Reader& in)
	{
		static const PartType OLD_FASTENERS[] = {
			PartType::BOLT_M10, PartType::NUT_M8, PartType::BOLT_M6, PartType::BOLT_M6, PartType::BOLT_M8, PartType::NUT_M8,
			PartType::NUT_M8, PartType::BOLT_M8, PartType::BOLT_M8, PartType::BOLT_M6, PartType::BOLT_M6, PartType::BOLT_M14,
			PartType::BOLT_M12, PartType::BOLT_M6, PartType::BOLT_M6 };
		if (old >= 0 && old < OLD_HEAD_BOLTS)
			return static_cast<PartType>(old);
		if (old == OLD_HEAD_BOLTS)
			return PartType::BOLT_M12;
		if (old > OLD_HEAD_BOLTS && old <= OLD_MAIN_CAP)
			return static_cast<PartType>(old - 1);
		if (old > OLD_MAIN_CAP && old - OLD_MAIN_CAP - 1 < static_cast<long long>(std::size(OLD_FASTENERS)))
			return OLD_FASTENERS[old - OLD_MAIN_CAP - 1];
		in.Fail("a value is out of range");
		return PartType::BLOCK;
	}

	// Before version 16 there were two more sizes: M9 rod nuts and M11 torque-to-yield head bolts.
	// Rod caps take M8 nuts now and the head M12 bolts; the numbers after them closed up.
	constexpr int FIRST_SIZE = static_cast<int>(PartType::BOLT_M6);
	constexpr int OLD_M9 = FIRST_SIZE + 3;
	constexpr int OLD_M11 = FIRST_SIZE + 5;
	PartType PartTypeBefore16(long long old, Reader& in)
	{
		static const PartType FROM_FIRST_SIZE[] = {
			PartType::BOLT_M6, PartType::BOLT_M8, PartType::NUT_M8, PartType::NUT_M8, PartType::BOLT_M10, PartType::BOLT_M12,
			PartType::BOLT_M12, PartType::BOLT_M14, PartType::CAM_CAP };
		if (old >= 0 && old < FIRST_SIZE)
			return static_cast<PartType>(old);
		if (old >= FIRST_SIZE && old - FIRST_SIZE < static_cast<long long>(std::size(FROM_FIRST_SIZE)))
			return FROM_FIRST_SIZE[old - FIRST_SIZE];
		in.Fail("a value is out of range");
		return PartType::BLOCK;
	}
	// The head bolts' spec went from 40 Nm (and an angle) to 90 Nm with them: what was done up keeps
	// its share of the spec.
	constexpr double OLD_HEAD_BOLT_NM = 40.0;

	// Old fastener slots kept their numbers; only the head bolts moved, from among the ancillaries
	// to the first fastener slot. Numbered as they were before version 15.
	long long OldSlot(long long old)
	{
		if (old == OLD_HEAD_BOLTS)
			return static_cast<long long>(AssemblySlot::HEAD_BOLT) - 1;
		if (old > OLD_HEAD_BOLTS && old <= OLD_MAIN_CAP)
			return old - 1;
		return old;
	}
	constexpr int MACHINE_OPERATIONS = static_cast<int>(MachineOperation::LINE_BORE) + 1;
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
	struct SavedPackage { uint32_t id; bool big; glm::vec3 position; float yaw; std::string label; std::vector<ItemRef> items; std::vector<size_t> stations; };
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
		int selectedSlot = 0;
		std::map<size_t, int> toolSizes;
		std::array<float, Workshop::MAX_ENGINES> engineRolls = {};
		std::array<float, Workshop::MAX_ENGINES> crankAngles = {};
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
	const auto& stats = workshop.m_stats;
	out.Word("stats").Num(stats.moneySpent).Num(stats.moneyEarned).Int(stats.partsBought).Int(stats.coresBought).Int(stats.partsSold)
		.Int(stats.partsFitted).Int(stats.measurements).Int(stats.cuts).Int(stats.dynoPulls).Int(stats.dynoFailures).Int(stats.enginesSold)
		.Num(stats.bestPowerKw).Num(stats.bestTorqueNm).Num(stats.bestEngineSale).Num(stats.metresWalked).Line();

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
	out.Word("furniture").Int(workshop.m_furniture.size());
	for (size_t piece : workshop.m_furniture)
		out.Int(piece);
	out.Line();
	out.Word("upgrades").Int(workshop.m_upgrades.size());
	for (bool done : workshop.m_upgrades)
		out.Int(done);
	out.Line();

	// Parts: who they are, then every feature's true geometry, then what the player measured.
	out.Word("parts").Int(workshop.m_inventory.size()).Line();
	for (const auto& part : workshop.m_inventory)
	{
		out.Word("part").Int(part->m_id).Int(static_cast<int>(part->m_type)).Int(static_cast<int>(part->m_quality))
			.Int(static_cast<int>(part->m_condition)).Int(static_cast<int>(part->m_material))
			.Num(part->m_sizeOffset).Num(part->m_value).Str(part->m_name).Str(part->m_damageReason)
			.Int(part->m_matchedTo).Int(part->m_matchedPosition).Num(part->m_torque)
			.Int(static_cast<int>(part->m_tightness)).Line();

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

	// Each engine on its stand: its parts by id, its assembly checks and its last dyno pull.
	out.Word("engines").Int(workshop.m_engines.size()).Int(workshop.m_activeEngine).Line();
	for (const auto& engine : workshop.m_engines)
	{
		const auto& assembly = engine->assembly;
		std::vector<std::array<long long, 3>> installed;
		for (int slot = 0; slot < static_cast<int>(AssemblySlot::COUNT); slot++)
			for (int index = 0; index < assembly.SlotCount(static_cast<AssemblySlot>(slot)); index++)
				if (auto part = assembly.Get(static_cast<AssemblySlot>(slot), index))
					installed.push_back({ slot, index, part->GetId() });
		out.Word("assembly").Int(installed.size()).Line();
		for (const auto& [slot, index, id] : installed)
			out.Int(slot).Int(index).Int(id).Line();

		out.Word("assembly_readings").Int(engine->readings.size()).Line();
		for (const auto& [label, reading] : engine->readings)
			out.Str(label).Num(reading.value).Int(reading.stale).Line();

		const auto& dyno = engine->lastDyno;
		out.Word("dyno").Int(dyno.ran).Int(dyno.failed).Num(dyno.failureRpm).Num(dyno.peakTorqueNm).Num(dyno.peakTorqueRpm)
			.Num(dyno.peakPowerKw).Num(dyno.peakPowerRpm).Num(dyno.oilPressureBar).Num(dyno.blowbyPercent).Num(dyno.compressionRatio)
			.Num(dyno.displacementCc).Num(dyno.healthScore).Int(engine->dynoCurrent).Line();
		out.Word("curve").Int(dyno.curve.size());
		for (const auto& point : dyno.curve)
			out.Num(point.rpm).Num(point.torqueNm).Num(point.powerKw);
		out.Line();
		out.Word("findings").Int(dyno.findings.size()).Line();
		for (const auto& finding : dyno.findings)
			out.Int(static_cast<int>(finding.severity)).Str(finding.message).Line();
	}
	out.Word("dyno_engine").Int(workshop.m_engineOnDyno).Line();

	// The 3D shop
	out.Word("stations").Int(world.m_stations.size()).Line();
	for (const auto& station : world.m_stations)
		out.Num(station.position.x).Num(station.position.y).Num(station.position.z).Num(station.yaw).Int(station.present).Line();

	out.Word("items").Int(world.m_items.size()).Line();
	for (const auto& [ref, visual] : world.m_items)
	{
		WriteItemRef(out, ref);
		glm::vec3 position = visual.object->GetPosition();
		// The index is the machine for a part set up in one, or the engine for a part fitted to one.
		int placeIndex = visual.place == ItemPlace::STAND ? visual.engineIndex : visual.machineIndex;
		out.Int(static_cast<int>(visual.place)).Int(placeIndex).Num(position.x).Num(position.y).Num(position.z).Num(visual.yaw).Line();
	}

	out.Word("packages").Int(world.m_packages.size()).Int(world.m_nextPackageId).Int(world.m_deliveries).Line();
	for (const auto& [id, package] : world.m_packages)
	{
		glm::vec3 position = package.object->GetPosition();
		bool big = package.object->GetModelIndex() == world.m_assets->Model(ShopModels::PACKAGE_CRATE);
		glm::quat rotation = package.object->GetRotation();
		float yaw = 2.f * std::atan2(rotation.y, rotation.w);	// packages only ever turn about Y
		out.Int(id).Int(big).Num(position.x).Num(position.y).Num(position.z).Num(yaw).Str(package.label).Int(package.items.size());
		for (const auto& item : package.items)
			WriteItemRef(out, item);
		out.Int(package.stations.size());
		for (size_t station : package.stations)
			out.Int(station);
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
	out.Word("selected").Int(world.m_inventory.GetSelected()).Line();
	// The socket on each ratchet, breaker bar and torque wrench, and the wrench picked from the set.
	out.Word("tool_sizes").Int(world.m_toolSizes.size());
	for (const auto& [tool, size] : world.m_toolSizes)
		out.Int(tool).Int(size);
	out.Line();

	out.Word("engine").Int(world.m_engineRolls.size());
	for (size_t engine = 0; engine < world.m_engineRolls.size(); engine++)
		out.Num(world.m_engineRolls[engine]).Num(world.m_crankAngles[engine]);
	out.Line();
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

	// Version 1 saves predate multiple engines: they hold one engine and one stand.
	in.Expect(MAGIC);
	long long version = in.Int();
	if ((version < 1 || version > VERSION) && !in.Failed())
		in.Fail("it was saved by a different version of the game");

	in.Expect("time");
	double savedTime = in.Num();
	in.Expect("money");
	double money = in.Num();
	in.Expect("hours");
	double hours = in.Num();

	PlayerStats stats;
	if (version >= 4)
	{
		in.Expect("stats");
		stats.moneySpent = in.Num();
		stats.moneyEarned = in.Num();
		stats.partsBought = static_cast<int>(in.Int());
		stats.coresBought = static_cast<int>(in.Int());
		stats.partsSold = static_cast<int>(in.Int());
		stats.partsFitted = static_cast<int>(in.Int());
		stats.measurements = static_cast<int>(in.Int());
		stats.cuts = static_cast<int>(in.Int());
		stats.dynoPulls = static_cast<int>(in.Int());
		stats.dynoFailures = static_cast<int>(in.Int());
		stats.enginesSold = static_cast<int>(in.Int());
		stats.bestPowerKw = in.Num();
		stats.bestTorqueNm = in.Num();
		stats.bestEngineSale = in.Num();
		stats.metresWalked = in.Num();
	}

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
	// Furniture came in version 12.
	std::vector<size_t> furniture;
	if (version >= 12)
	{
		in.Expect("furniture");
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			auto piece = static_cast<size_t>(in.Int());
			if (piece >= FurnitureCatalog::All().size())
				in.Fail("the shop has furniture this game doesn't know");
			furniture.push_back(piece);
		}
	}
	// Shop upgrades came in version 13.
	std::vector<bool> upgrades(UpgradeCatalog::All().size(), false);
	if (version >= 13)
	{
		in.Expect("upgrades");
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			bool done = in.Int() != 0;
			if (i < upgrades.size())
				upgrades[i] = done;
		}
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
		PartType type = PartType::BLOCK;
		bool resized = false;	// an M9 or M11, now another size
		bool oldHeadBolt = false;
		if (version >= 16)
			type = in.Enumerated<PartType>(PART_TYPES);
		else
		{
			long long old = in.Int();
			type = version >= 9 ? PartTypeBefore16(old, in) : OldPartType(old, in);
			resized = version >= 9 && (old == OLD_M9 || old == OLD_M11);
			oldHeadBolt = version >= 9 ? old == OLD_M11 : old == OLD_HEAD_BOLTS;
		}
		auto quality = in.Enumerated<PartQuality>(3);
		auto condition = in.Enumerated<PartCondition>(3);
		auto material = static_cast<MaterialType>(in.Int());
		double sizeOffset = in.Num();
		double value = in.Num();
		std::string name = in.Next();
		std::string damage = in.Next();
		uint32_t matchedTo = version >= 5 ? static_cast<uint32_t>(in.Int()) : 0;
		int matchedPosition = version >= 5 ? static_cast<int>(in.Int()) : -1;
		double torque = version >= 8 ? in.Num() : 0.0;
		// Before version 11 a nut or bolt was always torqued as it went on.
		auto tightness = version >= 11 ? in.Enumerated<FastenerTightness>(3) : torque > 0.0 ? FastenerTightness::TORQUED : FastenerTightness::LOOSE;
		if (in.Failed())
			break;

		auto part = PartFactory::Create(type, spec, quality, condition == PartCondition::DAMAGED ? PartCondition::USED : condition, sizeOffset);
		part->m_id = id;
		part->m_condition = condition;
		part->m_material = material;
		part->m_sizeOffset = sizeOffset;
		part->m_value = value;
		// Old nuts and bolts were named for their job; they take their size's name now.
		if ((version >= 9 && !resized) || !PartCatalog::Get(type).fastener.IsFastener())
			part->m_name = name;
		part->m_damageReason = damage;
		part->m_matchedTo = matchedTo;
		part->m_matchedPosition = matchedPosition;
		part->m_torque = oldHeadBolt ? torque / OLD_HEAD_BOLT_NM * EngineAssembly::Holes(AssemblySlot::HEAD_BOLT)->torqueNm : torque;
		part->m_tightness = tightness;

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

		// The engine has changed size. Before version 17 it was the 2.0 litre LV-20 (86 x 86 mm, 143.75 mm
		// rods, 46 cc chambers); version 17 had an 86 x 68.9 mm 1.6 (152.30 mm rods, 35 cc). It's now an
		// 81 x 77 mm 1.6 with 148.25 mm rods. What was made for the old sizes keeps how far off spec it
		// was, readings included.
		if (version < 18)
		{
			auto shift = [&](const std::string& featureName, double change) {
				if (auto feature = part->FindFeature(featureName))
				{
					feature->value += change;
					// A round feature's size is its samples.
					if (feature->IsRound())
						for (auto& station : feature->samples)
							for (double& sample : station)
								sample += change;
				}
				if (auto reading = part->m_readings.find(featureName); reading != part->m_readings.end())
				{
					reading->second.value += change;
					for (auto& station : reading->second.samples)
						for (double& sample : station)
							if (sample != 0.0)
								sample += change;
				}
			};
			bool lv20 = version < 17;
			shift(FeatureNames::STROKE, 77.00 - (lv20 ? 86.00 : 68.90));
			shift(FeatureNames::CENTER_LENGTH, 148.25 - (lv20 ? 143.75 : 152.30));
			for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			{
				if (lv20)
					shift(FeatureNames::Chamber(cylinder), 35.0 - 46.0);
				shift(FeatureNames::Bore(cylinder), 81.0 - 86.0);
			}
			shift(FeatureNames::SKIRT, 81.0 - 86.0);
			shift(FeatureNames::CLOSED_DIAMETER, 81.0 - 86.0);
			if (auto at = part->m_name.find("LV-20"); at != std::string::npos)
				part->m_name.replace(at, 5, "LV-16");
			// The timing chain became a belt: the old chain, guides and tensioner are the belt, idlers and
			// tensioner now (their measurements were the chain's, so they're fresh).
			for (auto [from, to] : { std::pair{ "Timing Chain", "Timing Belt" }, std::pair{ "Chain Guide", "Belt Idler" }, std::pair{ "Chain Tensioner", "Belt Tensioner" } })
				if (auto at = part->m_name.find(from); at != std::string::npos)
					part->m_name.replace(at, std::string(from).size(), to);
			if (type == PartType::TIMING_BELT)
				part->m_material = MaterialType::HNBR_BELT;
		}

		if (partsById.contains(id))
			in.Fail("two parts share an id");
		parts.push_back(part);
		partsById[id] = part;
		highestId = (std::max)(highestId, id);
	}

	// Each engine: parts fitted by id, assembly checks, last dyno pull.
	struct SavedEngine
	{
		std::vector<std::array<long long, 3>> installed;
		std::map<std::string, AssemblyReading> readings;
		DynoResult dyno;
		bool dynoCurrent = false;
	};
	auto readEngine = [&](SavedEngine& engine) {
		in.Expect("assembly");
		size_t installedCount = in.Count();
		for (size_t i = 0; i < installedCount && !in.Failed(); i++)
		{
			long long slot = in.Int(), index = in.Int(), id = in.Int();
			if (version < 9)
				slot = OldSlot(slot);
			// Cam caps took the slot before the first nut or bolt.
			if (version < 15 && slot >= static_cast<long long>(AssemblySlot::CAM_CAP))
				slot++;
			if (slot < 0 || slot >= static_cast<long long>(AssemblySlot::COUNT) || !partsById.contains(static_cast<uint32_t>(id)))
				in.Fail("an engine on a stand refers to a missing part");
			engine.installed.push_back({ slot, index, id });
		}

		in.Expect("assembly_readings");
		size_t assemblyReadingCount = in.Count();
		for (size_t i = 0; i < assemblyReadingCount && !in.Failed(); i++)
		{
			std::string label = in.Next();
			AssemblyReading reading;
			reading.value = in.Num();
			reading.stale = in.Int() != 0;
			engine.readings[label] = reading;
		}

		auto& dyno = engine.dyno;
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
		engine.dynoCurrent = in.Int() != 0;
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
	};

	std::vector<SavedEngine> engines;
	int activeEngine = 0;
	if (version == 1)
	{
		engines.resize(1);
		readEngine(engines[0]);
	}
	else
	{
		in.Expect("engines");
		size_t engineCount = in.Count();
		activeEngine = static_cast<int>(in.Int());
		if (!in.Failed() && (engineCount < 1 || engineCount > static_cast<size_t>(Workshop::MAX_ENGINES)))
			in.Fail("it has an impossible number of engines");
		engines.resize((std::min)(engineCount, static_cast<size_t>(Workshop::MAX_ENGINES)));
		for (auto& engine : engines)
			readEngine(engine);
	}
	int engineOnDyno = -1;
	if (version >= 17)
	{
		in.Expect("dyno_engine");
		engineOnDyno = static_cast<int>(in.Int());
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
	// Before version 3 the pallet rack was the second station; it's gone. Version 1 had no extra
	// engine stands either, which come last and keep their defaults.
	if (version < 3 && saved.stations.size() > 1)
		saved.stations.erase(saved.stations.begin() + 1);
	// Version 1 had no extra engine stands, and before version 6 there was one parts shelf. Both
	// come last, so the stations before them keep their places and the new ones their defaults.
	// The shop's own equipment, then a station for each piece of furniture.
	size_t expectedStations = world.m_fixedStations + furniture.size();
	if (version < 20)
		expectedStations -= 1;	// the extension's roll-up door
	if (version < 19)
		expectedStations -= 1;	// the bed
	if (version < 13)
		expectedStations -= 1;	// the post lift
	if (version < 10)
		expectedStations -= 1;	// the sanding bench
	if (version < 7)
		expectedStations -= 1;	// the line borer
	if (version < 6)
		expectedStations -= 1;	// the second shelf
	if (version == 1)
		expectedStations -= Workshop::MAX_ENGINES - 1;
	if (!in.Failed() && saved.stations.size() != expectedStations)
		in.Fail("the shop's equipment list has changed since this was saved");
	// The bed, then the extension's roll-up door, are the last of the shop's own stations: in older saves
	// the furniture comes straight after whichever was last then, so it moves up to make room.
	const size_t bedStation = world.m_fixedStations - 2;
	const size_t bayDoorStation = world.m_fixedStations - 1;
	for (auto [station, since] : { std::pair{ bedStation, 19 }, std::pair{ bayDoorStation, 20 } })
		if (!in.Failed() && version < since && saved.stations.size() >= station)
			saved.stations.insert(saved.stations.begin() + station, { world.m_stations[station].homePosition, world.m_stations[station].homeYaw, station == bedStation });

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
		// Before version 3 packages only ever held parts, listed by id, and no equipment.
		package.items.resize(in.Count());
		for (auto& item : package.items)
			item = version < 3 ? ItemRef::Part(static_cast<uint32_t>(in.Int())) : ReadItemRef(in);
		if (version >= 3)
		{
			package.stations.resize(in.Count());
			for (auto& station : package.stations)
			{
				station = static_cast<size_t>(in.Int());
				if (version < 19 && station >= bedStation)
					station++;
				if (version < 20 && station >= bayDoorStation)
					station++;
				if (station >= world.m_stations.size())
					in.Fail("a delivery holds equipment the shop doesn't have");
			}
		}
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
	if (version >= 14)
	{
		in.Expect("selected");
		saved.selectedSlot = static_cast<int>(in.Int());
	}
	if (version >= 11)
	{
		in.Expect("tool_sizes");
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			auto tool = static_cast<size_t>(in.Int());
			saved.toolSizes[tool] = static_cast<int>(in.Int());
		}
	}

	in.Expect("engine");
	size_t poses = version == 1 ? 1 : in.Count();
	for (size_t engine = 0; engine < poses && !in.Failed(); engine++)
	{
		float roll = static_cast<float>(in.Num());
		float crank = static_cast<float>(in.Num());
		if (engine < saved.engineRolls.size())
		{
			saved.engineRolls[engine] = roll;
			saved.crankAngles[engine] = crank;
		}
	}
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
	workshop.m_stats = stats;
	workshop.m_log = log;
	workshop.m_ownedTools = tools;
	workshop.m_ownedMachines = machines;
	workshop.m_furniture = furniture;
	workshop.m_upgrades = upgrades;
	workshop.m_inventory = parts;
	workshop.m_engines.clear();
	for (const auto& saved_engine : engines)
	{
		auto engine = std::make_unique<EngineBuild>(spec);
		for (const auto& [slot, index, id] : saved_engine.installed)
		{
			std::string installError;
			engine->assembly.Install(static_cast<AssemblySlot>(slot), static_cast<int>(index), partsById.at(static_cast<uint32_t>(id)), installError);
		}
		engine->readings = saved_engine.readings;
		engine->lastDyno = saved_engine.dyno;
		engine->dynoCurrent = saved_engine.dynoCurrent;
		workshop.m_engines.push_back(std::move(engine));
	}
	workshop.m_activeEngine = std::clamp(activeEngine, 0, workshop.GetEngineCount() - 1);
	// Only a finished engine can be on the dyno.
	workshop.m_engineOnDyno = engineOnDyno >= 0 && engineOnDyno < workshop.GetEngineCount() && workshop.GetAssembly(engineOnDyno).IsComplete() ? engineOnDyno : -1;
	Part::s_nextId = (std::max)(Part::s_nextId, highestId + 1);

	// The stations go where the save has them before anything is laid out on them (or the parts would
	// land on the shelves wherever they stood in the last game), and again after: the furniture's
	// stations are rebuilt in between.
	auto placeStations = [&]() {
		for (size_t i = 0; i < world.m_stations.size(); i++)
		{
			auto& station = world.m_stations[i];
			if (i < saved.stations.size())
			{
				station.position = saved.stations[i].position;
				station.yaw = saved.stations[i].yaw;
				station.present = saved.stations[i].present;
			}
			else
			{
				// Stations the save didn't know about start at home, there only if owned.
				station.position = station.homePosition;
				station.yaw = station.homeYaw;
				station.present = station.type == StationType::ENGINE_STAND ? station.engineIndex < workshop.GetEngineCount()
					: station.type == StationType::MACHINE ? workshop.OwnsMachine(static_cast<size_t>(station.machineIndex))
					: station.type == StationType::LIFT ? workshop.OwnsUpgrade(UpgradeKind::POST_LIFT) : true;
			}
			station.object->SetVisible(station.present);
			station.object->SetPickable(station.present);
		}
	};
	placeStations();
	ResetWorld(workshop, world);
	placeStations();

	for (const auto& item : saved.items)
	{
		auto visual = world.Find(item.ref);
		if (!visual)
			continue;
		visual->place = item.place;
		visual->machineIndex = item.place == ItemPlace::MACHINE ? item.machineIndex : -1;
		visual->engineIndex = item.place == ItemPlace::STAND ? (std::max)(item.machineIndex, 0) : -1;
		visual->yaw = item.yaw;
		if (item.place == ItemPlace::WORLD)
		{
			// Let it settle again from where it was.
			visual->object->SetPosition(item.position + glm::vec3(0.f, 0.01f, 0.f));
			visual->object->SetRotation(ShopLayout::Yaw(item.yaw));
			visual->body = { {}, false };
		}
		// The engines decide what's on the stands.
		if (item.place == ItemPlace::STAND && (!visual->part || workshop.EngineOf(*visual->part) < 0))
		{
			visual->place = ItemPlace::STORAGE;
			visual->engineIndex = -1;
		}
	}

	for (const auto& saved_package : saved.packages)
	{
		std::vector<ItemRef> items;
		for (const auto& ref : saved_package.items)
			if (auto visual = world.Find(ref))
			{
				visual->place = ItemPlace::PACKAGED;
				items.push_back(ref);
			}
		// Crated equipment stays out of the shop until it's unpacked.
		for (size_t station : saved_package.stations)
		{
			auto& crated = world.m_stations[station];
			crated.present = false;
			crated.object->SetVisible(false);
			crated.object->SetPickable(false);
		}
		if (items.empty() && saved_package.stations.empty())
			continue;
		auto object = saved_package.big ? world.Spawn(ShopModels::PACKAGE_CRATE, ShopTextures::WOOD, saved_package.position, saved_package.yaw)
			: world.Spawn(ShopModels::PACKAGE_BOX, ShopTextures::CARDBOARD, saved_package.position, saved_package.yaw);
		object->SetPickable(true);
		Package package = { saved_package.id, object, items, saved_package.stations, saved_package.label, { {}, false } };
		world.m_targets[object.get()] = { ShopWorld::TargetKind::PACKAGE, {}, package.id, 0 };
		world.m_packages[package.id] = package;
	}
	world.m_nextPackageId = (std::max)(saved.nextPackageId, 1u);
	world.m_deliveries = saved.deliveries;

	// Only keep backpack and hotbar entries for items that exist and are actually carried.
	auto carried = [&](const ItemRef& ref) {
		auto visual = world.Find(ref);
		return visual && (visual->place == ItemPlace::INVENTORY || visual->place == ItemPlace::HAND);
	};
	std::vector<GridItem> grid;
	for (const auto& entry : saved.grid)
		if (carried(entry.item))
			grid.push_back(entry);
	auto hotbar = saved.hotbar;
	for (auto& slot : hotbar)
		if (!carried(slot))
			slot = {};
	world.m_inventory.Restore(grid, hotbar, saved.selectedSlot);

	// Before 14 the hotbar only pointed into the backpack, and what was in hand was whatever was
	// equipped, maybe a part too big for the backpack: now it's whatever is in the selected slot, so
	// that goes on the hotbar (and is selected).
	if (version < 14 && carried(saved.equipped))
	{
		auto slot = world.m_inventory.HotbarSlotOf(saved.equipped);
		if (!slot && (slot = world.m_inventory.FreeHotbarSlot()))
			world.m_inventory.PutInHotbar(*slot, saved.equipped);
		if (slot)
			world.m_inventory.SetSelected(*slot);
	}
	for (auto& [ref, visual] : world.m_items)
		if ((visual.place == ItemPlace::INVENTORY || visual.place == ItemPlace::HAND) && !world.m_inventory.Contains(ref))
			visual.place = ItemPlace::STORAGE;

	// Everything carried put away, then the selected slot's item taken in hand.
	for (auto& [ref, visual] : world.m_items)
		if (visual.place == ItemPlace::HAND)
			visual.place = ItemPlace::INVENTORY;
	world.m_equipped = {};
	world.SyncHand();

	world.m_toolSizes = saved.toolSizes;
	world.m_engineRolls = saved.engineRolls;
	world.m_crankAngles = saved.crankAngles;
	world.SetDoorOpen(saved.doorOpen, true);
	world.RebuildCollision();

	player.SetPosition(saved.playerPosition);
	player.SetYaw(saved.playerYaw);
	player.SetPitch(saved.playerPitch);
	timeOfDay = static_cast<float>(savedTime);

	// Older saves predate caps and lower bearing shells as parts of their own: they arrive as a
	// delivery.
	if (version < 5)
		workshop.AddMissingHalves();
	// One shelf's worth of parts gets shared out across the two.
	if (version < 6)
		world.ReorganizeShelves();
	if (version < 8)
		workshop.AddMissingFasteners();
	if (version < 15)
		workshop.AddMissingCamCaps();
	return true;
}

void SaveGame::StartNew(Workshop& workshop, ShopWorld& world, FirstPersonController& player)
{
	// Everything back at home before the parts are laid out on the shelves (or they'd land wherever the
	// shelves stood in the last game), and again after: the furniture's stations are rebuilt in between.
	auto sendHome = [&]() {
		for (auto& station : world.m_stations)
		{
			station.position = station.homePosition;
			station.yaw = station.homeYaw;
			if (station.type == StationType::MACHINE)
				station.present = workshop.OwnsMachine(station.machineIndex);
			else if (station.type == StationType::ENGINE_STAND)
				station.present = station.engineIndex < workshop.GetEngineCount();
			else if (station.type == StationType::LIFT)
				station.present = workshop.OwnsUpgrade(UpgradeKind::POST_LIFT);
			else
				station.present = true;
			station.object->SetVisible(station.present);
			station.object->SetPickable(station.present);
		}
	};
	sendHome();
	ResetWorld(workshop, world);
	sendHome();
	world.m_engineRolls = {};
	world.m_crankAngles = {};
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
	world.m_inventory.Restore({}, {}, 0);
	world.m_equipped = {};
	world.m_handEmpty = false;
	world.m_engineOnDyno = -1;
	world.m_toolSizes.clear();
	world.m_move.reset();
	world.m_carriedPackage = 0;
	world.m_nextPackageId = 1;
	// Furniture is the workshop's: the pieces it has get stations of their own again.
	world.RemoveFurniture();
	world.SyncFurniture(workshop);
	// The building as this workshop has it: bay and second story built, or taken away again.
	world.SyncUpgrades(workshop);
	world.m_liftRaised = false;
	// Plastigauge strips aren't saved: a check in progress starts again.
	world.m_plastigauge.clear();

	// Lay out the workshop's parts and tools as if the shop had just opened: nothing is delivered.
	world.m_initialSyncDone = false;
	world.SyncItems(workshop);
	world.SyncStands(workshop);
	world.m_seenLogSize = workshop.GetLog().size();
}
