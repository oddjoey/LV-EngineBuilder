#include "RankUI.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

#include <imgui.h>

#include "../Simulation/Workshop.h"

namespace
{
	const ImVec4 REACHED = { 0.45f, 0.82f, 0.48f, 1.f };
	const ImVec4 NEXT = { 1.f, 0.82f, 0.35f, 1.f };

	const ImU32 OWNED_FILL = IM_COL32(38, 66, 46, 255);
	const ImU32 OWNED_LINE = IM_COL32(115, 210, 122, 255);
	const ImU32 OPEN_FILL = IM_COL32(46, 49, 57, 255);
	const ImU32 BUYABLE_FILL = IM_COL32(70, 66, 44, 255);
	const ImU32 BUYABLE_LINE = IM_COL32(255, 210, 90, 255);
	const ImU32 OPEN_LINE = IM_COL32(95, 100, 110, 255);
	const ImU32 LOCKED_FILL = IM_COL32(30, 32, 36, 255);
	const ImU32 LOCKED_LINE = IM_COL32(58, 61, 68, 255);
	const ImU32 TEXT = IM_COL32(230, 230, 230, 255);
	const ImU32 DIM = IM_COL32(140, 142, 148, 255);

	constexpr std::array<Skills::Group, 5> GROUPS = { Skills::Group::TOOLS, Skills::Group::BUILDING, Skills::Group::SHOP, Skills::Group::STATS, Skills::Group::COSMETICS };

	// The tools a rank opens up in the store, by name.
	std::string ToolsAt(int rank)
	{
		std::string tools;
		for (const auto& tool : ToolCatalog::All())
			if (tool.rank == rank && tool.partOf < 0)
				tools += (tools.empty() ? "" : ", ") + tool.name;
		return tools;
	}

	std::string Points(int points)
	{
		return std::format("{} pt{}", points, points == 1 ? "" : "s");
	}

	// How wide a perk's part of the tree is: one for each perk at the bottom of it.
	int Leaves(SkillKind kind)
	{
		int leaves = 0;
		for (const auto& skill : Skills::All())
			if (skill.after == kind)
				leaves += Leaves(skill.kind);
		return (std::max)(leaves, 1);
	}

	struct Node
	{
		float x = 0.f;		// its middle, from the tree's left
		float span = 0.f;	// how wide its share of the column is
		int depth = 0;
	};

	// A perk gets the middle of its share of the column, and shares that out among the perks under it.
	void Place(SkillKind kind, float from, float to, int depth, std::array<Node, Skills::COUNT>& nodes)
	{
		nodes[static_cast<size_t>(kind)] = { (from + to) * 0.5f, to - from, depth };
		float total = static_cast<float>(Leaves(kind));
		float x = from;
		for (const auto& skill : Skills::All())
			if (skill.after == kind)
			{
				float share = (to - from) * Leaves(skill.kind) / total;
				Place(skill.kind, x, x + share, depth + 1, nodes);
				x += share;
			}
	}

	// The perks as a tree, a column per group, each under the one that opens it up. Click one to buy it.
	void DrawTree(Workshop& workshop)
	{
		int points = workshop.SkillPoints();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		float lineHeight = ImGui::GetTextLineHeight();
		const float NODE_HEIGHT = lineHeight * 2.f + 26.f;
		const float ROW_GAP = 30.f;
		const float HEADER = lineHeight + 10.f;

		float width = ImGui::GetContentRegionAvail().x;
		float column = width / GROUPS.size();
		std::array<Node, Skills::COUNT> nodes = {};
		for (size_t g = 0; g < GROUPS.size(); g++)
		{
			auto isRoot = [&](const Skills::Info& skill) { return skill.group == GROUPS[g] && skill.after == SkillKind::COUNT; };
			float total = 0.f;
			for (const auto& skill : Skills::All())
				if (isRoot(skill))
					total += Leaves(skill.kind);
			float x = column * g;
			for (const auto& skill : Skills::All())
				if (isRoot(skill))
				{
					float share = column * Leaves(skill.kind) / total;
					Place(skill.kind, x, x + share, 0, nodes);
					x += share;
				}
		}
		int deepest = 0;
		for (const auto& node : nodes)
			deepest = (std::max)(deepest, node.depth);

		ImVec2 origin = ImGui::GetCursorScreenPos();
		auto top = [&](int depth) { return origin.y + HEADER + depth * (NODE_HEIGHT + ROW_GAP); };
		float bottom = top(deepest) + NODE_HEIGHT;

		for (size_t g = 0; g < GROUPS.size(); g++)
		{
			const char* name = Skills::GroupName(GROUPS[g]);
			float textWidth = ImGui::CalcTextSize(name).x;
			drawList->AddText({ origin.x + column * (g + 0.5f) - textWidth * 0.5f, origin.y }, DIM, name);
			if (g > 0)
				drawList->AddLine({ origin.x + column * g, origin.y }, { origin.x + column * g, bottom }, LOCKED_LINE);
		}

		// The links first, under the perks: lit once the perk above is bought.
		for (const auto& skill : Skills::All())
		{
			if (skill.after == SkillKind::COUNT)
				continue;
			const auto& node = nodes[static_cast<size_t>(skill.kind)];
			const auto& parent = nodes[static_cast<size_t>(skill.after)];
			ImU32 color = workshop.SkillLevel(skill.after) > 0 ? OWNED_LINE : LOCKED_LINE;
			float from = top(parent.depth) + NODE_HEIGHT, to = top(node.depth), middle = (from + to) * 0.5f;
			float parentX = origin.x + parent.x, childX = origin.x + node.x;
			drawList->AddLine({ parentX, from }, { parentX, middle }, color, 2.f);
			drawList->AddLine({ parentX, middle }, { childX, middle }, color, 2.f);
			drawList->AddLine({ childX, middle }, { childX, to }, color, 2.f);
		}

		for (const auto& skill : Skills::All())
		{
			const auto& node = nodes[static_cast<size_t>(skill.kind)];
			int level = workshop.SkillLevel(skill.kind);
			bool maxed = level >= skill.maxLevel;
			bool unlocked = workshop.SkillUnlocked(skill.kind);
			int cost = maxed ? 0 : Skills::CostOfLevel(skill.kind, level + 1);
			bool buyable = unlocked && !maxed && cost <= points;

			float nodeWidth = (std::min)(node.span - 10.f, 200.f);
			ImVec2 min = { origin.x + node.x - nodeWidth * 0.5f, top(node.depth) };
			ImVec2 max = { min.x + nodeWidth, min.y + NODE_HEIGHT };
			ImGui::PushID(static_cast<int>(skill.kind));
			ImGui::SetCursorScreenPos(min);
			if (ImGui::InvisibleButton("perk", { nodeWidth, NODE_HEIGHT }) && buyable)
				workshop.BuySkill(skill.kind);
			bool hovered = ImGui::IsItemHovered();
			ImGui::PopID();

			ImU32 fill = hovered && buyable ? BUYABLE_FILL : level > 0 ? OWNED_FILL : unlocked ? OPEN_FILL : LOCKED_FILL;
			ImU32 border = buyable ? BUYABLE_LINE : level > 0 ? OWNED_LINE : unlocked ? OPEN_LINE : LOCKED_LINE;
			drawList->AddRectFilled(min, max, fill, 6.f);
			drawList->AddRect(min, max, border, 6.f, 0, buyable ? 2.f : 1.f);

			// Its name, its levels as pips, and what the next one costs.
			drawList->PushClipRect(min, max, true);
			drawList->AddText({ min.x + 8.f, min.y + 6.f }, unlocked ? TEXT : DIM, skill.name);
			const float PIP = 4.f;
			float pipY = min.y + 6.f + lineHeight + 8.f + PIP;
			for (int i = 0; i < skill.maxLevel; i++)
			{
				ImVec2 center = { min.x + 8.f + PIP + i * (PIP * 2.f + 5.f), pipY };
				if (i < level)
					drawList->AddCircleFilled(center, PIP, OWNED_LINE);
				else
					drawList->AddCircle(center, PIP, unlocked ? OPEN_LINE : LOCKED_LINE, 0, 1.5f);
			}
			std::string state = maxed ? "Maxed" : !unlocked ? "Locked" : Points(cost);
			ImU32 stateColor = maxed ? OWNED_LINE : buyable ? BUYABLE_LINE : DIM;
			ImVec2 stateSize = ImGui::CalcTextSize(state.c_str());
			drawList->AddText({ max.x - 8.f - stateSize.x, max.y - 6.f - stateSize.y }, stateColor, state.c_str());
			drawList->PopClipRect();

			if (hovered)
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.f);
				ImGui::TextUnformatted(skill.name);
				ImGui::TextDisabled("%s", skill.description);
				ImGui::Text("Level %d of %d", level, skill.maxLevel);
				if (maxed)
					ImGui::TextColored(REACHED, "Fully levelled.");
				else if (!unlocked)
					ImGui::TextColored(NEXT, "Opens up once %s is bought.", Skills::Of(skill.after).name);
				else if (cost > points)
					ImGui::TextColored(NEXT, "The next level costs %s; the shop has %s.", Points(cost).c_str(), Points(points).c_str());
				else
					ImGui::TextColored(NEXT, "Click to %s for %s.", level == 0 ? "buy it" : "level it up", Points(cost).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}

		ImGui::SetCursorScreenPos({ origin.x, bottom + 6.f });
		ImGui::Dummy({ width, 0.f });
	}
}

void RankUI::Draw(Workshop& workshop)
{
	if (!ImGui::BeginChild("RankAndSkills"))
	{
		ImGui::EndChild();
		return;
	}

	int rank = workshop.Rank();
	double reputation = workshop.GetStats().reputation;
	const auto& here = ShopRank::Info(rank);

	// Where the shop stands.
	ImGui::SeparatorText("Your shop");
	ImGui::TextColored(REACHED, "Rank %d of %d: %s", rank, ShopRank::MaxRank(), here.name);
	ImGui::Text("Reputation: %.0f", reputation);
	if (rank < ShopRank::MaxRank())
	{
		const auto& next = ShopRank::Info(rank + 1);
		float progress = static_cast<float>((reputation - here.reputation) / (next.reputation - here.reputation));
		ImGui::ProgressBar(progress, { 360.f, 0.f }, std::format("{:.0f} / {:.0f}", reputation, next.reputation).c_str());
		ImGui::TextDisabled("%.0f more to rank %d, %s: a $%.0f bonus.", next.reputation - reputation, rank + 1, next.name, next.bonus);
	}
	else
		ImGui::TextDisabled("The top rank: every tool in the store is yours to buy.");

	// The shop's points, shared by everyone in it, and the tree to spend them in.
	ImGui::SeparatorText("Skills");
	int points = workshop.SkillPoints();
	ImGui::TextColored(points > 0 ? NEXT : ImGui::GetStyleColorVec4(ImGuiCol_Text), "Skill points to spend: %d", points);
	double toNext = Skills::REPUTATION_PER_POINT - std::fmod(reputation, Skills::REPUTATION_PER_POINT);
	ImGui::PushTextWrapPos(0.f);
	ImGui::TextDisabled("%d earned, %d spent: one for every %.0f reputation, the next with %.0f more. They're the shop's: everyone in the garage shares them. "
		"Click a perk to buy it; each opens up the ones under it.", workshop.SkillPointsEarned(), workshop.SkillPointsSpent(), Skills::REPUTATION_PER_POINT, toNext);
	ImGui::PopTextWrapPos();
	ImGui::Spacing();
	DrawTree(workshop);

	// How it's earned.
	ImGui::SeparatorText("Earning reputation");
	ImGui::BulletText("Sell an engine: %.0f, plus up to %.0f for how healthy it ran on the dyno, plus %.1f per horsepower.",
		ShopRank::ForSale(0.0, 0.0), ShopRank::ForSale(100.0, 0.0) - ShopRank::ForSale(0.0, 0.0), ShopRank::ForSale(0.0, 1.0) - ShopRank::ForSale(0.0, 0.0));
	ImGui::BulletText("A dyno pull that doesn't fail: %.0f.", ShopRank::FOR_CLEAN_PULL);
	ImGui::TextDisabled("A good 90 hp engine sold with a clean bill of health earns about %.0f.", ShopRank::ForSale(90.0, 90.0));

	// Every rank, its bonus and what it unlocks.
	ImGui::SeparatorText("Ranks and rewards");
	ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("Ranks", 5, flags))
	{
		ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 44.f);
		ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthFixed, 150.f);
		ImGui::TableSetupColumn("Reputation", ImGuiTableColumnFlags_WidthFixed, 80.f);
		ImGui::TableSetupColumn("Bonus", ImGuiTableColumnFlags_WidthFixed, 64.f);
		ImGui::TableSetupColumn("Unlocks in the store");
		ImGui::TableHeadersRow();
		for (int r = 1; r <= ShopRank::MaxRank(); r++)
		{
			const auto& info = ShopRank::Info(r);
			ImVec4 color = r <= rank ? REACHED : r == rank + 1 ? NEXT : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(color, "%s %d", r <= rank ? "v" : " ", r);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextColored(color, "%s", info.name);
			ImGui::TableSetColumnIndex(2);
			ImGui::TextColored(color, "%.0f", info.reputation);
			ImGui::TableSetColumnIndex(3);
			if (info.bonus > 0.0)
				ImGui::TextColored(color, "$%.0f", info.bonus);
			else
				ImGui::TextDisabled("-");
			ImGui::TableSetColumnIndex(4);
			std::string tools = r == 1 ? "Everything not listed below" : ToolsAt(r);
			ImGui::PushTextWrapPos(0.f);
			if (tools.empty())
				ImGui::TextDisabled("No new tools: the bonus");
			else
				ImGui::TextColored(color, "%s", tools.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::EndTable();
	}
	ImGui::EndChild();
}
