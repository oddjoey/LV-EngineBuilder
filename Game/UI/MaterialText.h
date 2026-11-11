#pragma once

#include <imgui.h>

#include "../Simulation/Material.h"

// A material's name in its tier's colour (standard plain, uprated green, performance blue), with its tier and what that
// means on hover. The colours are deeper on a light window.
inline ImVec4 MaterialTierColor(MaterialTier tier)
{
	const ImVec4& background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
	bool light = background.x * 0.3f + background.y * 0.59f + background.z * 0.11f > 0.5f;
	switch (tier)
	{
		case MaterialTier::UPRATED:		return light ? ImVec4(0.10f, 0.55f, 0.20f, 1.f) : ImVec4(0.45f, 0.88f, 0.45f, 1.f);
		case MaterialTier::PERFORMANCE:	return light ? ImVec4(0.12f, 0.38f, 0.85f, 1.f) : ImVec4(0.45f, 0.68f, 1.f, 1.f);
		case MaterialTier::STANDARD:	break;
	}
	return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

inline void MaterialText(MaterialType type)
{
	const Material& material = GetMaterial(type);
	ImGui::TextColored(MaterialTierColor(material.tier), "%s", material.name);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s material: %s", MaterialTierName(material.tier), MaterialTierMeaning(material.tier));
}
