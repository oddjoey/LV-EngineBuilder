#include "EngineBuilderGame.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>

#include "FrameProfiler.h"
#include "Simulation/Furniture.h"
#include "Simulation/Machine.h"
#include "Simulation/MeasuringTool.h"
#include "Simulation/PartCatalog.h"
#include "Simulation/Upgrades.h"

namespace
{
	// Names compare without case, spaces or punctuation: "main cap bolt" finds "Main Cap Bolt".
	std::string Normalize(const std::string& text)
	{
		std::string out;
		for (unsigned char c : text)
			if (std::isalnum(c))
				out += static_cast<char>(std::tolower(c));
		return out;
	}

	std::optional<double> Number(const std::string& text)
	{
		double value = 0.0;
		auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		if (error != std::errc() || end != text.data() + text.size())
			return std::nullopt;
		return value;
	}

	// The one name that matches: exactly, or else the only one containing what was typed. Otherwise
	// an error saying what it could have been.
	std::optional<size_t> FindName(const std::vector<std::string>& names, const std::string& typed, std::string& error)
	{
		std::string wanted = Normalize(typed);
		if (wanted.empty())
		{
			error = "Which one? Give a name.";
			return std::nullopt;
		}
		std::vector<size_t> partial;
		for (size_t i = 0; i < names.size(); i++)
		{
			std::string name = Normalize(names[i]);
			if (name == wanted)
				return i;
			if (name.find(wanted) != std::string::npos)
				partial.push_back(i);
		}
		if (partial.size() == 1)
			return partial[0];
		if (partial.empty())
			error = std::format("Nothing called \"{}\".", typed);
		else
		{
			error = std::format("\"{}\" could be:", typed);
			for (size_t i : partial)
				error += "\n  " + names[i];
		}
		return std::nullopt;
	}

	std::string Joined(const Console::Arguments& args)
	{
		std::string text;
		for (const auto& arg : args)
			text += (text.empty() ? "" : " ") + arg;
		return text;
	}

	// "on", "off", or nothing to flip it.
	bool Switch(const Console::Arguments& args, bool current)
	{
		if (args.empty())
			return !current;
		std::string word = Normalize(args[0]);
		return word == "on" || word == "1" || word == "true" || word == "yes";
	}
}

void EngineBuilderGame::RegisterConsoleCommands()
{
	// Anyone's: the door, and where you are and how the frame is going.
	m_console.AddCommand("door", "door [open|close]", "Rolls the garage door up or down (or the other way to how it is).", [this](const Console::Arguments& args) {
		bool open = args.empty() ? m_world.GetDoorOpenAmount() < 0.5f : Normalize(args[0]) == "open";
		m_world.SetDoorOpen(open);
		m_console.Print(open ? "Rolling the door up." : "Pulling the door down.");
	});

	m_console.AddCommand("profile", "profile [on|off]", "Logs per-section frame timings to the debug output every 2 s.", [this](const Console::Arguments& args) {
		auto& profiler = FrameProfiler::Get();
		profiler.SetEnabled(Switch(args, profiler.IsEnabled()));
		m_console.Print(std::format("Frame profiler {}.", profiler.IsEnabled() ? "on" : "off"));
	});

	m_console.AddCommand("pos", "pos", "Prints where you're standing.", [this](const Console::Arguments&) {
		glm::vec3 feet = m_player.GetFeetPosition();
		m_console.Print(std::format("x {:.2f}  y {:.2f}  z {:.2f}  facing {:.0f} deg", feet.x, feet.y, feet.z, glm::degrees(m_player.GetYaw())));
	});

	// Cheats, and the developer's views: debug builds only.
#ifdef _DEBUG
	auto partNames = [] {
		std::vector<std::string> names;
		for (int type = 0; type < static_cast<int>(PartType::COUNT); type++)
			names.push_back(PartCatalog::Get(static_cast<PartType>(type)).name);
		return names;
	};
	auto toolNames = [] {
		std::vector<std::string> names;
		for (const auto& tool : ToolCatalog::All())
			names.push_back(tool.name);
		return names;
	};
	auto machineNames = [] {
		std::vector<std::string> names;
		for (const auto& machine : MachineCatalog::All())
			names.push_back(machine.name);
		return names;
	};
	auto furnitureNames = [] {
		std::vector<std::string> names;
		for (const auto& piece : FurnitureCatalog::All())
			names.push_back(piece.name);
		return names;
	};
	auto upgradeNames = [] {
		std::vector<std::string> names;
		for (const auto& upgrade : UpgradeCatalog::All())
			names.push_back(upgrade.name);
		return names;
	};

	m_console.AddCommand("money", "money <amount>", "Adds money (negative takes it away).", [this](const Console::Arguments& args) {
		auto amount = args.empty() ? std::nullopt : Number(args[0]);
		if (!amount)
			return m_console.Error("Usage: money <amount>");
		m_workshop->DevAddMoney(*amount);
		m_console.Print(std::format("Money: ${:.0f}", m_workshop->GetMoney()));
	});

	m_console.AddCommand("rep", "rep <amount>", "Adds reputation (ranks up, with the bonuses, and earns skill points).", [this](const Console::Arguments& args) {
		auto amount = args.empty() ? std::nullopt : Number(args[0]);
		if (!amount || *amount <= 0.0)
			return m_console.Error("Usage: rep <amount>");
		m_workshop->DevAddReputation(*amount);
		m_console.Print(std::format("Reputation: {:.0f} (rank {}, {} skill points to spend)", m_workshop->GetStats().reputation,
			m_workshop->Rank(), m_workshop->SkillPoints()));
	});

	m_console.AddCommand("give", "give <part> [count] [new|used]",
		"Delivers parts to the door for free. Names can be partial: give head bolt 10.", [this, partNames](const Console::Arguments& args) {
		PartOrder order;
		Console::Arguments nameWords;
		for (const auto& arg : args)
		{
			std::string word = Normalize(arg);
			if (auto count = Number(arg))
				order.quantity = (std::max)(1, static_cast<int>(*count));
			else if (word == "new")
				order.condition = PartCondition::NEW;
			else if (word == "used")
				order.condition = PartCondition::USED;
			else
				nameWords.push_back(arg);
		}
		std::string error;
		auto index = FindName(partNames(), Joined(nameWords), error);
		if (!index)
			return m_console.Error(error);
		order.type = static_cast<PartType>(*index);
		m_workshop->DevGivePart(order);
		m_console.Print(std::format("{}x {} on its way to the door.", order.quantity, PartTypeName(order.type)));
	});

	m_console.AddCommand("tool", "tool <name>|all", "Gives you a tool (or every tool) for free.", [this, toolNames](const Console::Arguments& args) {
		const auto& tools = ToolCatalog::All();
		if (!args.empty() && Normalize(args[0]) == "all")
		{
			int given = 0;
			for (size_t i = 0; i < tools.size(); i++)
				given += m_workshop->DevGiveTool(i) ? 1 : 0;
			return m_console.Print(std::format("{} tools added.", given));
		}
		std::string error;
		auto index = FindName(toolNames(), Joined(args), error);
		if (!index)
			return m_console.Error(error);
		if (!m_workshop->DevGiveTool(*index))
			return m_console.Print(std::format("You already have the {}.", tools[*index].name));
		m_console.Print(std::format("Added the {}.", tools[*index].name));
	});

	m_console.AddCommand("machine", "machine <name>|all", "Gives you a machine (or every machine), crated at the door.",
		[this, machineNames](const Console::Arguments& args) {
		const auto& machines = MachineCatalog::All();
		if (!args.empty() && Normalize(args[0]) == "all")
		{
			int given = 0;
			for (size_t i = 0; i < machines.size(); i++)
				given += m_workshop->DevGiveMachine(i) ? 1 : 0;
			return m_console.Print(std::format("{} machines delivered.", given));
		}
		std::string error;
		auto index = FindName(machineNames(), Joined(args), error);
		if (!index)
			return m_console.Error(error);
		if (!m_workshop->DevGiveMachine(*index))
			return m_console.Print(std::format("You already have the {}.", machines[*index].name));
		m_console.Print(std::format("The {} is at the door in its crate.", machines[*index].name));
	});

	m_console.AddCommand("list", "list parts|tools|machines|furniture|upgrades [filter]", "Lists the names the other commands take.",
		[this, partNames, toolNames, machineNames, furnitureNames, upgradeNames](const Console::Arguments& args) {
		std::string what = args.empty() ? "" : Normalize(args[0]);
		std::vector<std::string> names = what == "parts" ? partNames() : what == "tools" ? toolNames() : what == "machines" ? machineNames() : what == "furniture" ? furnitureNames() : what == "upgrades" ? upgradeNames()
			: std::vector<std::string>{};
		if (names.empty())
			return m_console.Error("Usage: list parts|tools|machines|furniture [filter]");
		std::string filter = args.size() > 1 ? Normalize(Joined({ args.begin() + 1, args.end() })) : "";
		for (const auto& name : names)
			if (filter.empty() || Normalize(name).find(filter) != std::string::npos)
				m_console.Print("  " + name);
	});

	m_console.AddCommand("furniture", "furniture <name> [count]", "Delivers furniture, crated at the door, for free.", [this, furnitureNames](const Console::Arguments& args) {
		auto names = furnitureNames();
		int count = 1;
		Console::Arguments nameWords;
		for (const auto& arg : args)
			if (auto number = Number(arg))
				count = (std::max)(1, static_cast<int>(*number));
			else
				nameWords.push_back(arg);
		std::string error;
		auto index = FindName(names, Joined(nameWords), error);
		if (!index)
			return m_console.Error(error);
		for (int i = 0; i < count; i++)
			m_workshop->DevGiveFurniture(*index);
		m_console.Print(std::format("{}x {} on its way to the door.", count, names[*index]));
	});

	m_console.AddCommand("upgrade", "upgrade <name>|all", "Does a shop upgrade for free (list upgrades for the names).", [this, upgradeNames](const Console::Arguments& args) {
		auto names = upgradeNames();
		std::vector<size_t> chosen;
		if (!args.empty() && Normalize(args[0]) == "all")
			for (size_t i = 0; i < names.size(); i++)
				chosen.push_back(i);
		else
		{
			std::string error;
			auto index = FindName(names, Joined(args), error);
			if (!index)
				return m_console.Error(error);
			chosen.push_back(*index);
		}
		for (size_t index : chosen)
		{
			m_workshop->DevGiveUpgrade(static_cast<UpgradeKind>(index));
			m_console.Print(std::format("{}: done.", names[index]));
		}
	});

	m_console.AddCommand("assemble",

 "assemble", "Fits new OEM parts to every empty position on the active engine.", [this](const Console::Arguments&) {
		m_workshop->DevAssembleEngine();
		m_console.Print(std::format("Engine {} assembled.", m_workshop->GetActiveEngine() + 1));
	});

	m_console.AddCommand("pullhead", "pullhead", "Strips the active engine down to the head gasket and takes the head off, its valvetrain in it, into your hand.",
		[this](const Console::Arguments&) {
		auto head = m_workshop->DevPullHead();
		if (!head)
			return m_console.Error("The head didn't come off (is there one on the active engine?).");
		m_world.DevHold(ItemRef::Part(head->GetId()));
		m_console.Print(std::format("Took the {} off engine {}.", head->GetName(), m_workshop->GetActiveEngine() + 1));
	});

	m_console.AddCommand("strip", "strip <part>|<bolts>", "Takes that part (or a set of nuts and bolts, by its name: Exhaust Pipe Bolt) off the active "
		"engine, and everything fitted over it first. Names can be partial.",
		[this, partNames](const Console::Arguments& args) {
		std::string wanted = Normalize(Joined(args));
		// A set of nuts and bolts by its own name, exactly.
		for (int slot = static_cast<int>(AssemblySlot::HEAD_BOLT); slot < static_cast<int>(AssemblySlot::COUNT); slot++)
			if (Normalize(AssemblySlotName(static_cast<AssemblySlot>(slot))) == wanted)
			{
				int removed = m_workshop->DevStrip(static_cast<AssemblySlot>(slot));
				return m_console.Print(std::format("Took {} part(s) off engine {}.", removed, m_workshop->GetActiveEngine() + 1));
			}
		std::string error;
		auto index = FindName(partNames(), Joined(args), error);
		if (!index)
			return m_console.Error(error);
		auto slot = EngineAssembly::SlotFor(static_cast<PartType>(*index));
		if (slot == AssemblySlot::COUNT)
			return m_console.Error("That part doesn't go on an engine.");
		int removed = m_workshop->DevStrip(slot);
		m_console.Print(std::format("Took {} part(s) off engine {}.", removed, m_workshop->GetActiveEngine() + 1));
	});

	m_console.AddCommand("holdpart","holdpart <part>", "Takes a loose part of that kind (not fitted to anything) straight into your hand. Names can be partial.",
		[this, partNames](const Console::Arguments& args) {
		std::string error;
		auto index = FindName(partNames(), Joined(args), error);
		if (!index)
			return m_console.Error(error);
		auto type = static_cast<PartType>(*index);
		// The newest first (one just taken off an engine, say).
		const auto& inventory = m_workshop->GetInventory();
		for (auto it = inventory.rbegin(); it != inventory.rend(); ++it)
			if (const auto& part = *it; part->GetType() == type && m_workshop->EngineOf(*part) < 0 && !m_workshop->IsOnCar(*part) && !m_workshop->HostOf(*part)
				&& !m_workshop->CarHostOf(*part) && m_world.DevHold(ItemRef::Part(part->GetId())))
			{
				m_console.Print(std::format("Holding the {}.", part->GetName()));
				return;
			}
		m_console.Error(std::format("There's no loose {} (give one, and wait a moment for it to arrive).", PartCatalog::Get(type).name));
	});

	m_console.AddCommand("compress", "compress <spring>", "Clamps the Valve Spring Compressor over a spring (1-8, intake then exhaust per cylinder) in a loose head with its valvetrain in.",
		[this](const Console::Arguments& args) {
		auto spring = args.empty() ? std::nullopt : Number(args[0]);
		if (!spring)
			return m_console.Error("Usage: compress <spring>");
		int index = static_cast<int>(*spring) - 1;
		// The newest head first: the one just taken off an engine.
		const auto& inventory = m_workshop->GetInventory();
		for (auto it = inventory.rbegin(); it != inventory.rend(); ++it)
			if (const auto& part = *it; part->GetType() == PartType::CYLINDER_HEAD && m_workshop->EngineOf(*part) < 0
				&& m_workshop->InLooseHead(*part, AssemblySlot::VALVE_SPRING, index) && m_world.DevClampCompressor(*m_workshop, part, index))
			{
				m_console.Print(std::format("Clamped the compressor over spring {}.", index + 1));
				return;
			}
		m_console.Error("No loose head with that spring in, or no compressor (tool valve spring compressor), or it's on another valve.");
	});

	m_console.AddCommand("drop", "drop [flip]", "Sets what's in your hand down on the floor a step in front of you (upside down with flip).",
		[this](const Console::Arguments& args) {
		glm::vec3 forward = m_player.GetForward();
		glm::vec3 flat = glm::length(glm::vec2(forward.x, forward.z)) > 1e-4f ? glm::normalize(glm::vec3(forward.x, 0.f, forward.z)) : glm::vec3(1.f, 0.f, 0.f);
		glm::vec3 spot = m_player.GetEyePosition() + flat * 0.8f;
		spot.y = m_player.GetEyePosition().y - 0.2f;	// it falls onto whatever's under it
		if (!m_world.DevDropHeld(spot, m_player.GetYaw(), !args.empty() && args[0] == "flip"))
			return m_console.Error("Nothing in hand.");
		m_console.Print("Set it down.");
	});

	m_console.AddCommand("hold", "hold <tool>", "Takes a tool you own straight into your hand, out of its box if it's still in one.",
		[this, toolNames](const Console::Arguments& args) {
		std::string error;
		auto index = FindName(toolNames(), Joined(args), error);
		if (!index)
			return m_console.Error(error);
		if (!m_world.DevHold(ItemRef::Tool(static_cast<uint32_t>(*index))))
			return m_console.Error(std::format("You haven't a {} (give it with tool, and wait a moment for it to arrive).", ToolCatalog::All()[*index].name));
		m_console.Print(std::format("Holding the {}.", ToolCatalog::All()[*index].name));
	});

	m_console.AddCommand("look", "look <yaw> [pitch]", "Turns the view to a heading and tilt, in degrees.", [this](const Console::Arguments& args) {
		auto yaw = args.empty() ? std::nullopt : Number(args[0]);
		if (!yaw)
			return m_console.Error("Usage: look <yaw> [pitch]");
		auto pitch = args.size() > 1 ? Number(args[1]) : std::optional<double>(0.0);
		m_player.SetYaw(static_cast<float>(glm::radians(*yaw)));
		m_player.SetPitch(static_cast<float>(glm::radians(pitch.value_or(0.0))));
		m_console.Print(std::format("Looking at {:.0f}, {:.0f} degrees.", *yaw, pitch.value_or(0.0)));
	});

	m_console.AddCommand("car", "car build|test|strip <part>",
		"The LE86: build (new OEM parts wherever one's missing, the active engine in if it's finished, filled and bled), "
		"test (a road test), or strip a kind of part off it, bolts and all (car strip hood).", [this, partNames](const Console::Arguments& args) {
		std::string what = args.empty() ? "" : args[0];
		if (what == "strip")
		{
			std::string error;
			auto index = FindName(partNames(), Joined(Console::Arguments(args.begin() + 1, args.end())), error);
			if (!index)
				return m_console.Error(error);
			int removed = m_workshop->DevStripCar(static_cast<PartType>(*index));
			m_console.Print(std::format("Took {} {} off the car.", removed, PartTypeName(static_cast<PartType>(*index))));
			return;
		}
		if (what == "build")
		{
			m_workshop->DevAssembleCar();
			m_console.Print(m_workshop->GetEngineInCar() >= 0 ? "Built, with an engine in." : "Built, but with no engine: finish the active one and build again.");
		}
		else if (what == "test")
		{
			if (!m_workshop->OwnsCar())
				return m_console.Error("There's no car.");
			const auto& result = m_workshop->RoadTestCar();
			m_console.Print(std::format("Cranks {}, starts {}, drives {}, steers {}, stops {}; 0-100 km/h {:.1f} s, {:.0f}% healthy.", result.cranks,
				result.starts, result.drives, result.steers, result.stops, result.zeroToHundred, result.healthScore));
		}
		else
			m_console.Error("Usage: car build|test|strip <part>");
	});

	m_console.AddCommand("screen", "screen car [build|fluids|tune|test] | screen store", "Opens the parts computer's Car tab on one of its pages, or the store's LE86 Parts page.",
		[this](const Console::Arguments& args) {
		const char* pages[] = { "build", "fluids", "tune", "test" };
		if (!args.empty() && (args[0] == "backpack" || args[0] == "bench"))
		{
			m_ui.Open(args[0] == "bench" ? WorkshopTab::BENCH : WorkshopTab::BACKPACK);
			return m_console.Print(std::format("The {} screen.", args[0]));
		}
		if (!args.empty() && args[0] == "store")
		{
			m_ui.OpenStore(args.size() > 1 && args[1] == "hardware" ? StoreCategory::HARDWARE : args.size() > 1 && args[1] == "parts" ? StoreCategory::PARTS : StoreCategory::CAR);
			return m_console.Print("The store's LE86 Parts page.");
		}
		std::string which = args.size() > 1 ? args[1] : "build";
		for (int page = 0; page < 4; page++)
			if (which == pages[page] && !args.empty() && args[0] == "car")
			{
				m_ui.OpenCarPage(page);
				return m_console.Print(std::format("The Car tab, {} page.", which));
			}
		m_console.Error("Usage: screen car [build|fluids|tune|test]");
	});

	m_console.AddCommand("read","read engine|car [spread]", "Opens a service manual (the engine's or the car's), turned to a pair of pages.",
		[this](const Console::Arguments& args) {
		std::string which = args.empty() ? "engine" : args[0];
		if (which != "engine" && which != "car")
			return m_console.Error("Usage: read engine|car [spread]");
		m_manual.Open(which == "car" ? ServiceManual::Book::CHASSIS : ServiceManual::Book::ENGINE);
		auto spread = args.size() > 1 ? Number(args[1]) : std::nullopt;
		if (spread)
			m_manual.Turn(static_cast<int>(*spread));
		m_console.Print(std::format("Reading the {} manual.", which));
	});

	m_console.AddCommand("time", "time <hour>", "Sets the time of day, 0 to 24 (e.g. time 13.5).", [this](const Console::Arguments& args) {
		auto hour = args.empty() ? std::nullopt : Number(args[0]);
		if (!hour || *hour < 0.0 || *hour > 24.0)
			return m_console.Error("Usage: time <hour>, 0 to 24");
		m_timeOfDay = static_cast<float>(std::fmod(*hour, 24.0));
		m_console.Print(std::format("Time set to {:.2f} h.", m_timeOfDay));
	});

	m_console.AddCommand("timescale", "timescale <x>", "How many times faster than normal the day goes by.", [this](const Console::Arguments& args) {
		auto scale = args.empty() ? std::nullopt : Number(args[0]);
		if (!scale || *scale < 0.0)
			return m_console.Error("Usage: timescale <x>");
		m_devMenu.EditSettings().timeScale = static_cast<float>(*scale);
		m_console.Print(std::format("The day goes {:g}x as fast.", *scale));
	});

	m_console.AddCommand("xray", "xray [on|off]", "Shows parts' true geometry next to your measurements.", [this](const Console::Arguments& args) {
		auto& settings = m_devMenu.EditSettings();
		settings.xray = Switch(args, settings.xray);
		m_console.Print(std::format("X-ray {}.", settings.xray ? "on" : "off"));
	});

	m_console.AddCommand("bounds", "bounds [on|off]", "Draws the boxes everything collides and is picked with.", [this](const Console::Arguments& args) {
		auto& settings = m_devMenu.EditSettings();
		settings.showBounds = Switch(args, settings.showBounds);
		m_console.Print(std::format("Bounding boxes {}.", settings.showBounds ? "on" : "off"));
	});

	m_console.AddCommand("view", "view first|third|front", "Looks out of your own eyes, from behind you, or from in front of you.",
		[this](const Console::Arguments& args) {
		std::string which = args.empty() ? "first" : args[0];
		m_world.SetThirdPerson(which != "first");
		if (which == "front")
			m_world.CycleView();
		m_console.Print(std::format("The {} view.", which));
	});

	m_console.AddCommand("roll", "roll <degrees> [stand]", "Rolls the engine on a stand (the first unless given) about its crank.",
		[this](const Console::Arguments& args) {
		auto degrees = args.empty() ? std::nullopt : Number(args[0]);
		auto stand = args.size() > 1 ? Number(args[1]) : std::optional<double>(1.0);
		if (!degrees || !stand)
			return m_console.Error("Usage: roll <degrees> [stand]");
		m_world.DevRollStand(static_cast<int>(*stand) - 1, glm::radians(static_cast<float>(*degrees)));
		m_console.Print(std::format("Stand {} rolled to {} degrees.", *stand, *degrees));
	});

	m_console.AddCommand("spin", "spin <degrees per second> [stand]", "Keeps the engine on a stand rolling (0 stops it), to profile a moving engine.",
		[this](const Console::Arguments& args) {
		auto speed = args.empty() ? std::nullopt : Number(args[0]);
		auto stand = args.size() > 1 ? Number(args[1]) : std::optional<double>(1.0);
		if (!speed || !stand)
			return m_console.Error("Usage: spin <degrees per second> [stand]");
		m_world.DevSpinStand(static_cast<int>(*stand) - 1, glm::radians(static_cast<float>(*speed)));
		m_console.Print(std::format("Stand {} spinning at {} degrees a second.", *stand, *speed));
	});

	m_console.AddCommand("tp", "tp <x> <z> [y]", "Moves you to a spot on the floor (pos shows where you are).", [this](const Console::Arguments& args) {
		auto x = args.size() > 1 ? Number(args[0]) : std::nullopt;
		auto z = args.size() > 1 ? Number(args[1]) : std::nullopt;
		auto y = args.size() > 2 ? Number(args[2]) : std::optional<double>(0.0);
		if (!x || !z || !y)
			return m_console.Error("Usage: tp <x> <z> [y]");
		m_player.SetPosition({ static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z) });
		m_console.Print(std::format("Moved to {:.2f}, {:.2f}.", *x, *z));
	});
#endif
}
