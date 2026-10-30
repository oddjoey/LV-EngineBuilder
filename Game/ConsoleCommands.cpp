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

	m_console.AddCommand("give", "give <part> [count] [new|used] [budget|oem|performance]",
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
			else if (word == "budget")
				order.quality = PartQuality::BUDGET;
			else if (word == "oem")
				order.quality = PartQuality::OEM;
			else if (word == "performance")
				order.quality = PartQuality::PERFORMANCE;
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

	m_console.AddCommand("door", "door [open|close]", "Rolls the garage door up or down (or the other way to how it is).", [this](const Console::Arguments& args) {
		bool open = args.empty() ? m_world.GetDoorOpenAmount() < 0.5f : Normalize(args[0]) == "open";
		m_world.SetDoorOpen(open);
		m_console.Print(open ? "Rolling the door up." : "Pulling the door down.");
	});

	m_console.AddCommand("assemble",

 "assemble", "Fits new OEM parts to every empty position on the active engine.", [this](const Console::Arguments&) {
		m_workshop->DevAssembleEngine();
		m_console.Print(std::format("Engine {} assembled.", m_workshop->GetActiveEngine() + 1));
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

	m_console.AddCommand("profile", "profile [on|off]", "Logs per-section frame timings to the debug output every 2 s.", [this](const Console::Arguments& args) {
		auto& profiler = FrameProfiler::Get();
		profiler.SetEnabled(Switch(args, profiler.IsEnabled()));
		m_console.Print(std::format("Frame profiler {}.", profiler.IsEnabled() ? "on" : "off"));
	});

	m_console.AddCommand("pos", "pos", "Prints where you're standing.", [this](const Console::Arguments&) {
		glm::vec3 feet = m_player.GetFeetPosition();
		m_console.Print(std::format("x {:.2f}  y {:.2f}  z {:.2f}  facing {:.0f} deg", feet.x, feet.y, feet.z, glm::degrees(m_player.GetYaw())));
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

	m_console.AddCommand("save", "save", "Saves the game.", [this](const Console::Arguments&) {
		if (SaveCareer())
			m_console.Print("Saved.");
		else
			m_console.Error("Couldn't save.");
	});

	m_console.AddCommand("load", "load", "Loads the last save, losing anything since.", [this](const Console::Arguments&) {
		if (LoadCareer())
			m_console.Print("Loaded.");
		else
			m_console.Error("Couldn't load the save.");
	});
}
