#include "SaveGame.h"

#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
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
#include "SaveText.h"
#include "../LocalTime.h"

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
	// extension's roll-up door (a station after the bed). 21: the LV-16's figures are the Toyota 4A-C's
	// (crank, cam, chambers, pistons, rings, guides, stems and springs resized) and its hardware: rockers, a
	// distributor and a carburettor, plus a flywheel, downpipe, alternator and V-belt (six part slots before the nuts and bolts); cam
	// lobes measured by height, not lift; and each part keeps count of what has been surfaced off its deck.
	// 22: battery packs for the cordless tools. 23: a serpentine belt tensioner (since retired), a part slot
	// before the nuts and bolts (which moved up one), and its bolt after them.
	// 24: the shop's reputation (its rank). 25: the perks bought with skill points. 26: no shop clock (the
	// hours went), and no Machinist's Eye perk (the eighth): its points go back to the shop.
	// 27: the shack: half the size, no house (the bed is a cot in the shack), the dyno bought (and owned in
	// older saves), and the upgrades are perks: Contractor Rates (the fourth) went, its points back to the shop.
	// 28: the 4A-C's four cam journals (and caps), not five, and its five rocker shaft bolts through both shafts.
	// 29: no cot (the station after the lift). 30: the engine hoist (a station after the extension's door), and
	// which engine hangs from it. 31: bearing shells fitted into loose rods and caps. 32: pistons on loose rods, and
	// the piston pins pressed into them (a part of their own). 33: rings fitted onto loose pistons. 34: the pistons
	// in the top of their bores, not yet pushed home with the ring compressor.
	// 38: the project car (a station after the hoist, its state on a line of its own, and the fluids in stock).
	constexpr int VERSION = 39;
	constexpr const char* MAGIC = "LVSAVE";

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
	// The torques that changed in version 21, as they were before.
	// Before version 28 the cam ran in five journals, where it runs in four now (the middle one went), and the
	// rocker shafts had three bolts each where five go through both now. Where an old journal (or the cap on it)
	// is now, or -1 for the one that went.
	int CamJournalBefore28(int journal)
	{
		constexpr int NOW[] = { 0, 1, -1, 2, 3 };
		return journal >= 0 && journal < 5 ? NOW[journal] : -1;
	}
	// A cam journal's or cam bore's feature, or the cam clearance read on it, renumbered to match; empty for the
	// journal that went. Anything else is left as it is.
	std::string CamNameBefore28(const std::string& name)
	{
		for (const char* prefix : { "Cam Journal ", "Cam Bore ", "Cam journal " })
		{
			std::string start = prefix;
			if (!name.starts_with(start) || name.size() <= start.size() || !std::isdigit(static_cast<unsigned char>(name[start.size()])))
				continue;
			int journal = name[start.size()] - '1';
			int now = CamJournalBefore28(journal);
			return now < 0 ? std::string() : start + std::to_string(now + 1) + name.substr(start.size() + 1);
		}
		return name;
	}

	const std::map<std::string, double> OLD_TORQUES_NM = {
		{ "Head Bolt", 90.0 }, { "Main Cap Bolt", 65.0 }, { "Rod Nut", 45.0 }, { "Intake Manifold Nut", 22.0 },
		{ "Oil Pump Bolt", 10.0 }, { "Crank Pulley Bolt", 145.0 }, { "Cam Sprocket Bolt", 60.0 }, { "Cam Cap Bolt", 10.0 },
	};

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
		// A delivery carried on the hotbar is saved as a package, where it was carried; its slot as empty.
		if (ref.IsPackage())
		{
			out.Int(static_cast<int>(ItemRef::Kind::NONE)).Int(0);
			return;
		}
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
	struct SavedItem { ItemRef ref; ItemPlace place; int machineIndex; glm::vec3 position; float yaw; glm::quat tilt = glm::quat(1.f, 0.f, 0.f, 0.f); };
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
		std::map<size_t, BatteryPack> batteries;
		std::vector<std::pair<std::string, std::string>> paint;	// WallPaint's records
		std::vector<std::pair<ItemRef, PaintCoat>> coats;
		std::array<float, Workshop::MAX_ENGINES> engineRolls = {};
		std::array<float, Workshop::MAX_ENGINES> crankAngles = {};
		bool moveIn = false;	// from the old garage into the shack (see Load)
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
#ifdef _WIN32
	auto system = std::chrono::clock_cast<std::chrono::system_clock>(written);
#else
	auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(std::chrono::file_clock::to_sys(written));
#endif
	return LocalTimeText(system, "%b %d, %H:%M");
}

bool SaveGame::Save(const Workshop& workshop, const ShopWorld& world, const FirstPersonController& player, float timeOfDay, std::string& error)
{
	Writer out;
	out.Word(MAGIC).Int(VERSION).Line();
	out.Word("time").Num(timeOfDay).Line();
	out.Word("money").Num(workshop.m_money).Line();
	const auto& stats = workshop.m_stats;
	out.Word("stats").Num(stats.moneySpent).Num(stats.moneyEarned).Int(stats.partsBought).Int(stats.coresBought).Int(stats.partsSold)
		.Int(stats.partsFitted).Int(stats.measurements).Int(stats.cuts).Int(stats.dynoPulls).Int(stats.dynoFailures).Int(stats.enginesSold)
		.Num(stats.bestPowerKw).Num(stats.bestTorqueNm).Num(stats.bestEngineSale).Num(stats.metresWalked).Num(stats.reputation).Line();

	out.Word("log").Int(workshop.m_log.size()).Line();
	for (const auto& line : workshop.m_log)
		out.Str(line).Line();

	// How many of each tool (saves before copies wrote 0 or 1, which reads the same).
	out.Word("tools").Int(workshop.m_toolCounts.size());
	for (int count : workshop.m_toolCounts)
		out.Int(count);
	out.Line();
	out.Word("skills").Int(workshop.m_skills.size());
	for (int level : workshop.m_skills)
		out.Int(level);
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
	out.Word("dyno").Int(workshop.m_ownsDyno).Line();
	out.Word("hoist").Int(workshop.m_ownsHoist).Line();

	// Parts: who they are, then every feature's true geometry, then what the player measured.
	out.Word("parts").Int(workshop.m_inventory.size()).Line();
	for (const auto& part : workshop.m_inventory)
	{
		out.Word("part").Int(part->m_id).Int(static_cast<int>(part->m_type)).Int(static_cast<int>(part->m_quality))
			.Int(static_cast<int>(part->m_condition)).Int(static_cast<int>(part->m_material))
			.Num(part->m_sizeOffset).Num(part->m_value).Str(part->m_name).Str(part->m_damageReason)
			.Int(part->m_matchedTo).Int(part->m_matchedPosition).Num(part->m_torque)
			.Int(static_cast<int>(part->m_tightness)).Num(part->m_resurfaced).Line();

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
	out.Word("hoist_engine").Int(workshop.m_engineOnHoist).Line();
	out.Word("shells").Int(workshop.m_shells.size());
	for (const auto& [host, shell] : workshop.m_shells)
		out.Int(host).Int(shell);
	out.Line();
	for (auto [word, fittings] : { std::pair{ "pistons", &workshop.m_pistons }, std::pair{ "pins", &workshop.m_pins }, std::pair{ "rings", &workshop.m_rings } })
	{
		out.Word(word).Int(fittings->size());
		for (const auto& [host, fitted] : *fittings)
			out.Int(host).Int(fitted);
		out.Line();
	}
	{
		std::vector<std::pair<int, int>> unseated;
		for (int engine = 0; engine < workshop.GetEngineCount(); engine++)
			for (int cylinder : workshop.m_engines[engine]->unseated)
				unseated.push_back({ engine, cylinder });
		out.Word("unseated").Int(unseated.size());
		for (auto [engine, cylinder] : unseated)
			out.Int(engine).Int(cylinder);
		out.Line();
	}
	{
		// Rings gapped on their own, pushed down square in their bores.
		std::vector<std::pair<int, int>> squared;
		for (int engine = 0; engine < workshop.GetEngineCount(); engine++)
			for (int cylinder : workshop.m_engines[engine]->squaredRings)
				squared.push_back({ engine, cylinder });
		out.Word("squaredrings").Int(squared.size());
		for (auto [engine, cylinder] : squared)
			out.Int(engine).Int(cylinder);
		out.Line();
	}
	// Rings set in the bores of blocks off the engine: the ring, the block, the bore, and whether it's squared.
	out.Word("borerings").Int(workshop.m_boreRings.size());
	for (const auto& [ring, at] : workshop.m_boreRings)
		out.Int(ring).Int(at.block).Int(at.cylinder).Int(at.squared ? 1 : 0);
	out.Line();
	// The project car, what's on its loose struts and wheels, and the fluids on the shelf.
	out.Word("car").Str(workshop.CarState()).Line();
	// The desert quarry: its upgrades, the crusher's hopper and what it's made.
	out.Word("quarry").Str(workshop.QuarryText()).Line();

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
		// What another player is carrying goes back on the shelf or the tool board: only the host saves. A shell
		// in a rod or cap is saved as stored, and goes back in it (see the shells line).
		// A part on the car goes back on it from the car's own line, likewise.
		ItemPlace place = visual.place == ItemPlace::CARRIED || visual.place == ItemPlace::ATTACHED || visual.place == ItemPlace::CAR
			? ItemPlace::STORAGE : visual.place;
		out.Int(static_cast<int>(place)).Int(placeIndex).Num(position.x).Num(position.y).Num(position.z).Num(visual.yaw)
			.Num(visual.tilt.w).Num(visual.tilt.x).Num(visual.tilt.y).Num(visual.tilt.z).Line();
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
	// Each battery pack: how full, and the cordless tool or charger it's on (or -1).
	out.Word("batteries").Int(world.m_batteries.size());
	for (const auto& [tool, battery] : world.m_batteries)
		out.Int(tool).Num(battery.charge).Int(battery.on);
	out.Line();
	// Paint on the walls and floor, a tile to a line, and the coats sprayed on parts and tools.
	auto paint = world.m_paint.Records();
	out.Word("paint").Int(paint.size()).Line();
	for (const auto& [key, text] : paint)
		out.Str(key).Str(text).Line();
	out.Word("coats").Int(world.m_coats.size());
	for (const auto& [ref, coat] : world.m_coats)
		out.Int(static_cast<int>(ref.kind)).Int(ref.id).Int(coat.rgb).Num(coat.amount);
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
	if (version < 26)
	{
		in.Expect("hours");
		in.Num();
	}

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
		if (version >= 24)
			stats.reputation = in.Num();
	}

	in.Expect("log");
	std::vector<std::string> log(in.Count());
	for (auto& line : log)
		line = in.Next();

	std::vector<int> tools(ToolCatalog::All().size(), 0);
	in.Expect("tools");
	size_t toolCount = in.Count();
	for (size_t i = 0; i < toolCount; i++)
	{
		int count = std::clamp(static_cast<int>(in.Int()), 0, Workshop::MAX_TOOL_COPIES);
		if (i < tools.size())
			tools[i] = count;
	}
	// Tools added since it was saved that every shop starts with (the piston pin press), it has too.
	for (size_t i = toolCount; i < tools.size(); i++)
		if (ToolCatalog::Of(i).ownedAtStart)
			tools[i] = 1;
	std::array<int, Skills::COUNT> skills = {};
	if (version >= 25)
	{
		in.Expect("skills");
		size_t skillCount = in.Count();
		constexpr size_t RETIRED_MACHINIST = 7;
		for (size_t i = 0; i < skillCount; i++)
		{
			int level = static_cast<int>(in.Int());
			size_t skill = i;
			if (version < 26 && i == RETIRED_MACHINIST)
				continue;
			if (version < 26 && i > RETIRED_MACHINIST)
				skill--;
			constexpr size_t RETIRED_CONTRACTOR = 3;
			if (version < 27 && skill == RETIRED_CONTRACTOR)
				continue;
			if (skill < skills.size())
				skills[skill] = std::clamp(level, 0, Skills::All()[skill].maxLevel);
		}
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
	// Before version 27 every shop had a dyno.
	bool ownsDyno = true;
	if (version >= 27)
	{
		in.Expect("dyno");
		ownsDyno = in.Int() != 0;
	}
	bool ownsHoist = false;
	if (version >= 30)
	{
		in.Expect("hoist");
		ownsHoist = in.Int() != 0;
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
		if (version < 28 && type == PartType::CAM_CAP)
			matchedPosition = CamJournalBefore28(matchedPosition);
		double torque = version >= 8 ? in.Num() : 0.0;
		// Before version 11 a nut or bolt was always torqued as it went on.
		auto tightness = version >= 11 ? in.Enumerated<FastenerTightness>(3) : torque > 0.0 ? FastenerTightness::TORQUED : FastenerTightness::LOOSE;
		// Before version 21 nothing kept count of what had been surfaced off a deck.
		double resurfaced = version >= 21 ? in.Num() : 0.0;
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
		// (To the 90 Nm spec of the time: version 21 scales it on to today's once it's on its engine.)
		part->m_torque = oldHeadBolt ? torque / OLD_HEAD_BOLT_NM * OLD_TORQUES_NM.at("Head Bolt") : torque;
		part->m_tightness = tightness;
		part->m_resurfaced = resurfaced;

		// Before version 21 a cam's lobes were measured by their lift; now it's their height, which is the
		// lift on top of the base circle. Renames the feature (true if it did); the value wants the base adding.
		auto lobeLiftToHeight = [&](std::string& featureName) {
			if (version >= 21 || type != PartType::CAMSHAFT)
				return false;
			for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
				for (bool exhaust : { false, true })
					if (featureName == FeatureNames::OldLobeLift(cylinder, exhaust))
					{
						featureName = FeatureNames::LobeHeight(cylinder, exhaust);
						return true;
					}
			return false;
		};

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
			if (lobeLiftToHeight(featureName))
				featureValue += spec.CamBaseCircle();
			if (version < 28)
				featureName = CamNameBefore28(featureName);

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
			if (lobeLiftToHeight(featureName))
				reading.value += spec.CamBaseCircle();
			if (version < 28)
				featureName = CamNameBefore28(featureName);
			if (!featureName.empty())
				part->m_readings[featureName] = reading;
		}

		// Resizing a feature to a new spec: what was made for the old size keeps how far off spec it was,
		// readings included. map takes the old size (or sample, or reading) to the new one.
		auto resize = [&](const std::string& featureName, const std::function<double(double)>& map) {
			if (auto feature = part->FindFeature(featureName))
			{
				feature->value = map(feature->value);
				// A round feature's size is its samples.
				if (feature->IsRound())
					for (auto& station : feature->samples)
						for (double& sample : station)
							sample = map(sample);
			}
			if (auto reading = part->m_readings.find(featureName); reading != part->m_readings.end())
			{
				reading->second.value = map(reading->second.value);
				for (auto& station : reading->second.samples)
					for (double& sample : station)
						if (sample != 0.0)
							sample = map(sample);
			}
		};
		auto shift = [&](const std::string& featureName, double change) { resize(featureName, [=](double size) { return size + change; }); };
		auto scale = [&](const std::string& featureName, double factor) { resize(featureName, [=](double size) { return size * factor; }); };

		// The engine has changed size. Before version 17 it was the 2.0 litre LV-20 (86 x 86 mm, 143.75 mm
		// rods, 46 cc chambers); version 17 had an 86 x 68.9 mm 1.6 (152.30 mm rods, 35 cc). It's now an
		// 81 x 77 mm 1.6 with 148.25 mm rods.
		if (version < 18)
		{
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

		// Version 21 took the LV-16's figures from the Toyota 4A-C: a lower deck and shorter rods, smaller
		// crank journals and housings, a bigger cam, bigger chambers for 9.0:1, looser pistons, a tighter ring
		// gap, and softer springs. Each shift is the new mid-spec size less the old one.
		if (version < 21)
		{
			shift(FeatureNames::DECK_HEIGHT, 191.00 - 216.95);
			shift(FeatureNames::CENTER_LENGTH, 122.00 - 148.25);
			shift(FeatureNames::COMPRESSION_HEIGHT, 30.25 - 30.00);
			for (int journal = 0; journal < spec.mainJournals; journal++)
			{
				shift(FeatureNames::MainJournal(journal), 47.9925 - 54.985);
				shift(FeatureNames::MainHousing(journal), 52.008 - 59.0095);
			}
			for (int journal = 0; journal < spec.camJournals; journal++)
			{
				shift(FeatureNames::CamJournal(journal), 28.940 - 25.9695);
				shift(FeatureNames::CamBore(journal), 28.9955 - 26.0105);
			}
			for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			{
				shift(FeatureNames::RodJournal(cylinder), 39.9925 - 47.984);
				shift(FeatureNames::Chamber(cylinder), 40.6 - 35.0);
				for (bool exhaust : { false, true })
					shift(FeatureNames::Guide(cylinder, exhaust), 7.020 - 7.0075);
			}
			shift(FeatureNames::BIG_END, 43.008 - 51.008);
			shift(FeatureNames::SKIRT, 80.899 - 80.973);
			shift(FeatureNames::CLOSED_DIAMETER, 80.914 - 80.888);
			if (type == PartType::INTAKE_VALVE)
				shift(FeatureNames::STEM, 6.9775 - 6.9825);
			else if (type == PartType::EXHAUST_VALVE)
				shift(FeatureNames::STEM, 6.9725 - 6.9625);
			shift(FeatureNames::FREE_LENGTH, 44.60 - 44.0);
			scale(FeatureNames::SPRING_RATE, 38.5 / 45.0);
			scale(FeatureNames::SEAT_PRESSURE, 231.0 / 270.0);
			// And its hardware: buckets became rocker arms, coils plug leads, the throttle body a
			// carburettor and the fuel rail a fuel pump (each measured fresh: what was measured was the old part).
			for (auto [from, to] : { std::pair{ "Bucket Lifter", "Rocker Arm" }, std::pair{ "Ignition Coil", "Spark Plug Lead" },
				std::pair{ "Throttle Body", "Carburettor" }, std::pair{ "Fuel Rail", "Fuel Pump" } })
				if (auto at = part->m_name.find(from); at != std::string::npos)
					part->m_name.replace(at, std::string(from).size(), to);
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
			// The rocker shafts, distributor, flywheel, downpipe, alternator and V-belt took the six slots
			// before the first nut or bolt in version 21.
			if (version < 21 && slot >= static_cast<long long>(AssemblySlot::ROCKER_SHAFT))
				slot += 6;
			// And the serpentine belt's tensioner the one before them in version 23.
			if (version < 23 && slot >= static_cast<long long>(AssemblySlot::DRIVE_BELT_TENSIONER))
				slot++;
			if (slot < 0 || slot >= static_cast<long long>(AssemblySlot::COUNT) || !partsById.contains(static_cast<uint32_t>(id)))
				in.Fail("an engine on a stand refers to a missing part");
			// Version 28: four cam caps (two bolts each), and five rocker shaft bolts through both shafts at the ends and
			// between the cylinders (the first shaft's three are the middle ones). What has no place now comes off, loose.
			if (version < 28 && slot == static_cast<long long>(AssemblySlot::CAM_CAP))
				index = CamJournalBefore28(static_cast<int>(index));
			else if (version < 28 && slot == static_cast<long long>(AssemblySlot::CAM_CAP_BOLT))
			{
				int cap = CamJournalBefore28(static_cast<int>(index / 2));
				index = cap < 0 ? -1 : cap * 2 + index % 2;
			}
			else if (version < 28 && slot == static_cast<long long>(AssemblySlot::ROCKER_SHAFT_BOLT))
				index = index / 3 == 0 ? index % 3 + 1 : -1;
			if (index < 0)
				continue;
			engine.installed.push_back({ slot, index, id });
			// Version 21 took the torques from the 4A-C: what was done up keeps its share of the spec.
			if (version < 21 && !in.Failed())
				if (const auto* holes = EngineAssembly::Holes(static_cast<AssemblySlot>(slot)))
					if (auto old = OLD_TORQUES_NM.find(holes->name); old != OLD_TORQUES_NM.end())
					{
						auto& fastener = partsById[static_cast<uint32_t>(id)];
						fastener->m_torque *= holes->torqueNm / old->second;
					}
		}

		in.Expect("assembly_readings");
		size_t assemblyReadingCount = in.Count();
		for (size_t i = 0; i < assemblyReadingCount && !in.Failed(); i++)
		{
			std::string label = in.Next();
			AssemblyReading reading;
			reading.value = in.Num();
			reading.stale = in.Int() != 0;
			if (version < 28)
				label = CamNameBefore28(label);
			if (!label.empty())
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
	int engineOnHoist = -1;
	if (version >= 30)
	{
		in.Expect("hoist_engine");
		engineOnHoist = static_cast<int>(in.Int());
	}
	std::map<uint32_t, uint32_t> shells;
	if (version >= 31)
	{
		in.Expect("shells");
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			auto host = static_cast<uint32_t>(in.Int());
			shells[host] = static_cast<uint32_t>(in.Int());
		}
	}
	std::map<uint32_t, uint32_t> pistons, pins, rings;
	if (version >= 32)
		for (auto [word, fittings] : { std::pair{ "pistons", &pistons }, std::pair{ "pins", &pins }, std::pair{ "rings", &rings } })
		{
			if (word == std::string("rings") && version < 33)
				continue;
			in.Expect(word);
			size_t count = in.Count();
			for (size_t i = 0; i < count && !in.Failed(); i++)
			{
				auto host = static_cast<uint32_t>(in.Int());
				(*fittings)[host] = static_cast<uint32_t>(in.Int());
			}
		}
	std::vector<std::pair<int, int>> unseated;
	if (version >= 34)
	{
		in.Expect("unseated");
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			int engine = static_cast<int>(in.Int());
			unseated.push_back({ engine, static_cast<int>(in.Int()) });
		}
	}
	std::vector<std::pair<int, int>> squaredRings;
	if (version >= 35)
	{
		in.Expect("squaredrings");
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			int engine = static_cast<int>(in.Int());
			squaredRings.push_back({ engine, static_cast<int>(in.Int()) });
		}
	}
	std::map<uint32_t, BoreRing> boreRings;
	if (version >= 36)
	{
		in.Expect("borerings");
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			auto ring = static_cast<uint32_t>(in.Int());
			BoreRing at;
			at.block = static_cast<uint32_t>(in.Int());
			at.cylinder = static_cast<int>(in.Int());
			at.squared = in.Int() != 0;
			boreRings[ring] = at;
		}
	}
	std::string carText;
	if (version >= 38)
	{
		in.Expect("car");
		carText = in.Next();
	}
	std::string quarryText;
	if (version >= 39)
	{
		in.Expect("quarry");
		quarryText = in.Next();
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
	if (version < 38)
		expectedStations -= 1;	// the project car
	if (version < 30)
		expectedStations -= 1;	// the engine hoist
	if (version < 20)
		expectedStations -= 1;	// the extension's roll-up door
	if (version >= 19 && version < 29)
		expectedStations += 1;	// the bed, later the cot, since gone
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
	// The extension's roll-up door, then the engine hoist, are the last of the shop's own stations. From
	// version 19 to 28 the bed (then the cot) came just before the door: it's gone, so what came after it
	// moves down one. Before 20 there was no door, and before 30 no hoist: the furniture moves up to make
	// room for them.
	// Since version 38 the project car comes after the hoist: the stations before it are numbered as they were.
	const size_t carStation = world.m_fixedStations - 1;
	const size_t hoistStation = carStation - 1;
	const size_t bayDoorStation = carStation - 2;
	const size_t cotStation = bayDoorStation;	// where the bed was, in those saves
	if (!in.Failed() && version >= 19 && version < 29 && saved.stations.size() > cotStation)
		saved.stations.erase(saved.stations.begin() + cotStation);
	if (!in.Failed() && version < 20 && saved.stations.size() >= bayDoorStation)
		saved.stations.insert(saved.stations.begin() + bayDoorStation, { world.m_stations[bayDoorStation].homePosition, world.m_stations[bayDoorStation].homeYaw, false });
	if (!in.Failed() && version < 30 && saved.stations.size() >= hoistStation)
		saved.stations.insert(saved.stations.begin() + hoistStation, { world.m_stations[hoistStation].homePosition, world.m_stations[hoistStation].homeYaw, false });
	if (!in.Failed() && version < 38 && saved.stations.size() >= carStation)
		saved.stations.insert(saved.stations.begin() + carStation, { world.m_stations[carStation].homePosition, world.m_stations[carStation].homeYaw, false });
	// Before version 27 the shop was a garage twice the size, with the house beside it: everything moves into
	// the shack, the equipment to its usual spots (furniture to where new furniture goes).
	saved.moveIn = version < 27;
	if (saved.moveIn)
		for (size_t i = 0; i < saved.stations.size(); i++)
		{
			saved.stations[i].position = i < world.m_stations.size() ? world.m_stations[i].homePosition : ShopLayout::FURNITURE_HOME;
			saved.stations[i].yaw = i < world.m_stations.size() ? world.m_stations[i].homeYaw : 0.f;
		}

	in.Expect("items");
	saved.items.resize(in.Count());
	for (auto& item : saved.items)
	{
		item.ref = ReadItemRef(in);
		item.place = in.Enumerated<ItemPlace>(ITEM_PLACES);
		item.machineIndex = static_cast<int>(in.Int());
		item.position = ReadVec3(in);
		item.yaw = static_cast<float>(in.Num());
		// Since version 37: how it was turned as it was put down.
		if (version >= 37)
		{
			float w = static_cast<float>(in.Num()), x = static_cast<float>(in.Num()), y = static_cast<float>(in.Num()), z = static_cast<float>(in.Num());
			if (w * w + x * x + y * y + z * z > 0.5f)
				item.tilt = glm::normalize(glm::quat(w, x, y, z));
		}
		// Whatever was lying about in the old garage goes back on the shelves.
		if (saved.moveIn && item.place == ItemPlace::WORLD)
			item.place = ItemPlace::STORAGE;
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
				if (version >= 19 && version < 29 && station > cotStation)
					station--;
				if (version < 20 && station >= bayDoorStation)
					station++;
				if (version < 30 && station >= hoistStation)
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
	if (version >= 22)
	{
		in.Expect("batteries");
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			auto tool = static_cast<size_t>(in.Int());
			auto& battery = saved.batteries[tool];
			battery.charge = std::clamp(static_cast<float>(in.Num()), 0.f, 1.f);
			battery.on = static_cast<int>(in.Int());
		}
	}
	if (version >= 24)
	{
		in.Expect("paint");
		saved.paint.resize(in.Count());
		for (auto& [key, text] : saved.paint)
		{
			key = in.Next();
			text = in.Next();
		}
		in.Expect("coats");
		saved.coats.resize(in.Count());
		for (auto& [ref, coat] : saved.coats)
		{
			ref = ReadItemRef(in);
			coat.rgb = static_cast<uint32_t>(in.Int());
			coat.amount = std::clamp(static_cast<float>(in.Num()), 0.f, 1.f);
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
	workshop.m_stats = stats;
	workshop.m_log = log;
	workshop.m_toolCounts = tools;
	workshop.m_skills = skills;
	workshop.m_ownedMachines = machines;
	workshop.m_furniture = furniture;
	workshop.m_upgrades = upgrades;
	workshop.m_ownsDyno = ownsDyno;
	workshop.m_ownsHoist = ownsHoist;
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
	workshop.m_engineOnHoist = workshop.m_ownsHoist && engineOnHoist >= 0 && engineOnHoist < workshop.GetEngineCount() && engineOnHoist != workshop.m_engineOnDyno
		&& !workshop.GetAssembly(engineOnHoist).GetAllParts().empty() ? engineOnHoist : -1;
	// Shells in loose rods and caps, as long as both are still the shop's and off the engines.
	workshop.m_shells.clear();
	for (const auto& [host, shell] : shells)
		if (partsById.contains(host) && partsById.contains(shell) && workshop.EngineOf(*partsById.at(host)) < 0 && workshop.EngineOf(*partsById.at(shell)) < 0
			&& Workshop::ShellFits(partsById.at(host)->GetType(), partsById.at(shell)->GetType()))
			workshop.m_shells[host] = shell;
	Part::s_nextId = (std::max)(Part::s_nextId, highestId + 1);
	// Pistons on loose rods, and the pins in pistons (on the engines too).
	workshop.m_pistons.clear();
	for (const auto& [rod, piston] : pistons)
		if (partsById.contains(rod) && partsById.contains(piston) && partsById.at(rod)->GetType() == PartType::CONNECTING_ROD
			&& partsById.at(piston)->GetType() == PartType::PISTON && workshop.EngineOf(*partsById.at(rod)) < 0 && workshop.EngineOf(*partsById.at(piston)) < 0)
			workshop.m_pistons[rod] = piston;
	workshop.m_pins.clear();
	for (const auto& [piston, pin] : pins)
		if (partsById.contains(piston) && partsById.contains(pin) && partsById.at(piston)->GetType() == PartType::PISTON
			&& partsById.at(pin)->GetType() == PartType::PISTON_PIN)
			workshop.m_pins[piston] = pin;
	workshop.m_rings.clear();
	for (const auto& [piston, ring] : rings)
		if (partsById.contains(piston) && partsById.contains(ring) && partsById.at(piston)->GetType() == PartType::PISTON
			&& partsById.at(ring)->GetType() == PartType::PISTON_RING && workshop.EngineOf(*partsById.at(piston)) < 0 && workshop.EngineOf(*partsById.at(ring)) < 0)
			workshop.m_rings[piston] = ring;
	// Pistons in the top of their bores, as long as their rods are in.
	for (auto [engine, cylinder] : unseated)
		if (engine >= 0 && engine < workshop.GetEngineCount() && workshop.GetAssembly(engine).Get(AssemblySlot::CONNECTING_ROD, cylinder))
			workshop.m_engines[engine]->unseated.insert(cylinder);
	// Rings squared in their bores, as long as they're there on their own.
	for (auto [engine, cylinder] : squaredRings)
		if (engine >= 0 && engine < workshop.GetEngineCount() && workshop.RingsInBore(engine).contains(cylinder))
			workshop.m_engines[engine]->squaredRings.insert(cylinder);
	// Rings in loose blocks, as long as both are still loose.
	workshop.m_boreRings.clear();
	for (const auto& [ring, at] : boreRings)
		if (partsById.contains(ring) && partsById.contains(at.block) && partsById.at(ring)->GetType() == PartType::PISTON_RING
			&& partsById.at(at.block)->GetType() == PartType::BLOCK && workshop.EngineOf(*partsById.at(ring)) < 0 && workshop.EngineOf(*partsById.at(at.block)) < 0)
			workshop.m_boreRings[ring] = at;
	// The project car, once its parts and the engines are in place.
	workshop.m_car.reset();
	workshop.m_carCarried.clear();
	workshop.m_fluidStock = {};
	if (!carText.empty() && !workshop.RestoreCarState(carText))
		workshop.Log("The project car in the save couldn't be read: it's gone from the salvage yard.");
	workshop.m_quarry = {};
	if (!quarryText.empty() && !workshop.RestoreQuarry(quarryText))
		workshop.Log("The quarry in the save couldn't be read: it starts again from nothing.");
	// Before version 32 the pins were part of the pistons: each piston gets one, pressed in if it's on an engine.
	if (version < 32)
	{
		std::vector<pPart> pistonsOwned;
		for (const auto& part : workshop.m_inventory)
			if (part->GetType() == PartType::PISTON)
				pistonsOwned.push_back(part);
		for (const auto& piston : pistonsOwned)
		{
			auto pin = PartFactory::Create(PartType::PISTON_PIN, spec, piston->GetQuality(), PartCondition::USED);
			workshop.m_inventory.push_back(pin);
			if (workshop.EngineOf(*piston) >= 0)
				workshop.m_pins[piston->GetId()] = pin->GetId();
		}
	}

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
					: station.type == StationType::LIFT ? workshop.OwnsUpgrade(UpgradeKind::POST_LIFT)
					: station.type == StationType::DYNO ? workshop.OwnsDyno()
					: station.type == StationType::HOIST ? workshop.OwnsHoist()
					: station.type == StationType::CAR ? workshop.OwnsCar() : true;
			}
			station.object->SetVisible(station.present);
			station.object->SetPickable(station.present);
		}
	};
	placeStations();
	ResetWorld(workshop, world);
	placeStations();
	// The engines are already where they were: none swings onto the hoist as the game opens.
	world.m_engineOnDyno = workshop.m_engineOnDyno;
	world.m_engineOnHoist = workshop.m_engineOnHoist;

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
			visual->tilt = item.tilt;
			visual->object->SetRotation(world.RestingRotation(*visual));
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
		glm::vec3 position = saved.moveIn ? world.NextDeliverySpot() : saved_package.position;
		auto object = saved_package.big ? world.Spawn(ShopModels::PACKAGE_CRATE, ShopTextures::WOOD, position, saved_package.yaw)
			: world.Spawn(ShopModels::PACKAGE_BOX, ShopTextures::CARDBOARD, position, saved_package.yaw);
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
	world.m_batteries = saved.batteries;
	// Paint on the old garage's walls and floor doesn't fit the shack: it's left behind.
	if (!saved.moveIn)
		for (const auto& [key, text] : saved.paint)
			if (WallPaint::IsKey(key))
				world.m_paint.Apply(key, text);
	for (const auto& [ref, coat] : saved.coats)
		world.m_coats[ref] = coat;
	world.m_engineRolls = saved.engineRolls;
	world.m_crankAngles = saved.crankAngles;
	world.SetDoorOpen(saved.doorOpen, true);
	world.RebuildCollision();

	player.SetPosition(saved.moveIn ? world.GetSpawnPosition() : saved.playerPosition);
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
	if (version < 21)
		workshop.AddMissing4ACParts();
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
			else if (station.type == StationType::DYNO)
				station.present = workshop.OwnsDyno();
			else if (station.type == StationType::HOIST)
				station.present = workshop.OwnsHoist();
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
	world.m_engineOnHoist = -1;
	world.m_swing = {};
	world.m_toolSizes.clear();
	world.m_batteries.clear();
	world.m_move.reset();
	world.m_paint.Clear();
	world.m_coats.clear();
	world.m_lastPaint.reset();
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
