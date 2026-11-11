// The desert quarry's screen: what it's making, the hopper to empty, and the upgrades, each with a turning view of it.
#include "WorkshopUI.h"

#include <algorithm>
#include <format>

#include "ModelPreview.h"
#include "../World/ShopAssets.h"
#include "../World/ToolShapes.h"

namespace
{
	const ImVec4 GOOD = { 0.45f, 0.85f, 0.45f, 1.f };
	const ImVec4 WARN = { 0.95f, 0.75f, 0.30f, 1.f };

	// The model that shows each upgrade.
	std::string UpgradeModel(QuarryUpgrade upgrade)
	{
		switch (upgrade)
		{
			case QuarryUpgrade::CRUSHER:	return ShopModels::QUARRY_CRUSHER;
			case QuarryUpgrade::CONVEYOR:	return ShopModels::QUARRY_CONVEYOR;
			case QuarryUpgrade::HOPPER:		return ShopModels::QUARRY_HOPPER;
			case QuarryUpgrade::HAMMER:
				for (const auto& tool : ToolCatalog::All())
					if (tool.handKind == HandToolKind::PICKAXE)
						return ToolModelName(tool);
				return "";
			default:						return ShopModels::QUARRY_SHED;
		}
	}
}

void WorkshopUI::DrawQuarryTab(Workshop& workshop)
{
	const auto& quarry = workshop.GetQuarry();
	auto level = [&](QuarryUpgrade upgrade) { return quarry.levels[static_cast<int>(upgrade)]; };
	bool crusher = level(QuarryUpgrade::CRUSHER) > 0;
	bool hand = level(QuarryUpgrade::HAND) > 0;

	ImGui::TextWrapped("The shop's sideline. Break the boulders out in the desert with the pickaxe for what the stone fetches (they turn "
		"up again after a while), and set up a crusher on the pad to make money while you work on engines. Everything it makes buys it bigger.");
	ImGui::Spacing();

	// What it's doing now.
	if (ImGui::BeginTable("QuarryNow", 4, ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextRow();
		auto figure = [&](const char* label, const std::string& value) {
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", label);
			ImGui::Text("%s", value.c_str());
		};
		figure("Crusher", crusher ? std::format("${:.1f} a minute", workshop.QuarryRate()) : "none yet");
		figure("A boulder by hand", std::format("about ${:.0f}, {:.1f} s", workshop.BoulderPay(1.0), workshop.BoulderSeconds()));
		figure("Boulders broken", std::to_string(quarry.bouldersBroken));
		figure("The quarry has made", std::format("${:.0f}", quarry.earned));
		ImGui::EndTable();
	}
	if (crusher)
	{
		ImGui::SeparatorText("The hopper");
		float fill = static_cast<float>(quarry.hopper / workshop.QuarryCapacity());
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, fill >= 0.999f ? WARN : GOOD);
		ImGui::ProgressBar(fill, { 360.f, 0.f }, std::format("${:.0f} of ${:.0f}", quarry.hopper, workshop.QuarryCapacity()).c_str());
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::BeginDisabled(quarry.hopper < 1.0);
		if (ImGui::Button(std::format("Sell it (${:.0f})", quarry.hopper).c_str()))
			workshop.CollectQuarry();
		ImGui::EndDisabled();
		if (hand)
			ImGui::TextDisabled("The hand hauls the gravel away as it comes, for a tenth of it.");
		else if (fill >= 0.999f)
			ImGui::TextColored(WARN, "It's full: the crusher's stopped till it's emptied.");
		else
		{
			double minutes = (workshop.QuarryCapacity() - quarry.hopper) / (std::max)(workshop.QuarryRate(), 0.01);
			ImGui::TextDisabled("Full in about %.0f minute%s.", minutes, minutes >= 0.5 && minutes < 1.5 ? "" : "s");
		}
	}

	// The upgrades: what's there, what's next, and what it does.
	ImGui::SeparatorText("Upgrades");
	if (ImGui::BeginTable("QuarryUpgrades", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("view", ImGuiTableColumnFlags_WidthFixed, 140.f);
		ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch, 3.f);
		ImGui::TableSetupColumn("buy", ImGuiTableColumnFlags_WidthStretch, 1.2f);
		for (int u = 0; u < QUARRY_UPGRADES; u++)
		{
			auto upgrade = static_cast<QuarryUpgrade>(u);
			int now = level(upgrade), max = Workshop::QuarryMaxLevel(upgrade);
			ImGui::PushID(u);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			// Its view: what's there now, or (with nothing yet) what's to come.
			ImVec2 min = ImGui::GetCursorScreenPos(), max2 = { min.x + 130.f, min.y + 100.f };
			ImGui::Dummy({ 130.f, 100.f });
			if (uint32_t model = m_modelLookup ? m_modelLookup(UpgradeModel(upgrade)) : 0; model && ImGui::IsRectVisible(min, max2))
			{
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				drawList->AddRectFilled(min, max2, ImGui::GetColorU32(ImGuiCol_FrameBg), 6.f);
				bool owned = now > 0 || upgrade == QuarryUpgrade::HAMMER;
				ModelPreview::DrawAt(drawList, min, max2, model, owned ? ImVec4(1.f, 1.f, 1.f, 1.f) : ImVec4(0.45f, 0.45f, 0.45f, 1.f));
			}
			ImGui::TableNextColumn();
			ImGui::Text("%s", Workshop::QuarryUpgradeName(upgrade, now));
			ImGui::SameLine();
			ImGui::TextDisabled("(%d of %d)", now, max);
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("%s", Workshop::QuarryUpgradeBlurb(upgrade));
			ImGui::PopTextWrapPos();
			if (now < max)
				ImGui::Text("Next: %s", Workshop::QuarryUpgradeName(upgrade, now + 1));
			ImGui::TableNextColumn();
			if (now >= max)
				ImGui::TextColored(GOOD, "The best there is");
			else
			{
				auto problem = workshop.QuarryUpgradeProblem(upgrade);
				ImGui::BeginDisabled(problem.has_value());
				if (ImGui::Button(std::format("Buy (${:.0f})", workshop.QuarryUpgradePrice(upgrade)).c_str(), { -1.f, 0.f }))
					workshop.BuyQuarryUpgrade(upgrade);
				ImGui::EndDisabled();
				if (problem)
					ImGui::TextDisabled("%s", problem->c_str());
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
}
