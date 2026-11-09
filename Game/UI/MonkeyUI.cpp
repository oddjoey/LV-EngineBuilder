#include "MonkeyUI.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <vector>

#include <imgui.h>

#include "../Controls.h"
#include "../World/PlayerAvatar.h"
#include "ModelPreview.h"

namespace
{
	struct Swatch
	{
		const char* name;
		uint32_t color;
	};
	const Swatch FURS[] = {
		{ "Brown", 0x5C381F }, { "Dark brown", 0x33200F }, { "Black", 0x1E1C1B }, { "Golden", 0xB07A33 },
		{ "Ginger", 0xB4531C }, { "Grey", 0x77736F }, { "Snow", 0xE6E2DA },
	};
	const Swatch SKINS[] = {
		{ "Tan", 0xC79975 }, { "Pale", 0xE7C3A6 }, { "Pink", 0xE29C93 }, { "Dark", 0x6E4B37 }, { "Grey", 0x8C8681 },
	};
	const Swatch CAPS[] = {
		{ "Blue", 0x2F6FD6 }, { "Red", 0xC8322B }, { "Green", 0x2E9A4A }, { "Yellow", 0xE6B820 }, { "Orange", 0xE7731F },
		{ "Purple", 0x7B45C2 }, { "Black", 0x222222 }, { "White", 0xEDEDED },
	};

	// The Premium Colours perk's.
	const Swatch PREMIUM_FURS[] = { { "Gold", 0xD4A017 }, { "Chrome", 0xC9CED4 }, { "Neon green", 0x39FF6A } };
	const Swatch PREMIUM_CAPS[] = { { "Gold", 0xE8B923 }, { "Chrome", 0xDCE0E6 }, { "Neon pink", 0xFF3DB8 }, { "Neon blue", 0x2FD8FF } };

	ImU32 ToImU32(uint32_t rgb)
	{
		return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
	}

	// Rows of colour buttons, wrapped to the panel, then the colour picker for anything else. The one
	// in use has a ring.
	void ColorRow(const char* label, uint32_t& color, std::vector<Swatch> swatches)
	{
		constexpr float SWATCH = 30.f;
		ImGui::SeparatorText(label);
		ImGui::PushID(label);
		float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
		for (size_t i = 0; i < swatches.size(); i++)
		{
			if (i > 0 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + SWATCH <= right)
				ImGui::SameLine();
			ImGui::PushID(static_cast<int>(i));
			ImVec4 shade = ImGui::ColorConvertU32ToFloat4(ToImU32(swatches[i].color));
			if (ImGui::ColorButton(swatches[i].name, shade, ImGuiColorEditFlags_NoTooltip, { SWATCH, SWATCH }))
				color = swatches[i].color;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", swatches[i].name);
			if (color == swatches[i].color)
			{
				ImVec2 low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
				ImGui::GetWindowDrawList()->AddRect({ low.x - 3.f, low.y - 3.f }, { high.x + 3.f, high.y + 3.f },
					IM_COL32(255, 210, 120, 255), 4.f, 0, 2.f);
			}
			ImGui::PopID();
		}
		float rgb[3] = { ((color >> 16) & 0xFF) / 255.f, ((color >> 8) & 0xFF) / 255.f, (color & 0xFF) / 255.f };
		if (ImGui::ColorEdit3("##Custom", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
			color = MonkeyLook::Pack({ rgb[0], rgb[1], rgb[2] });
		ImGui::SameLine();
		ImGui::TextDisabled("Any colour");
		ImGui::PopID();
	}

}

void MonkeyUI::Draw(MonkeyLook& look, const std::function<uint32_t(const std::string&)>& modelLookup, bool wardrobe, bool premium)
{
	// Your monkey, turning, built from the same pieces it's made of in the shop, as wide as the panel.
	// Without the Wardrobe perk it's the shop's monkey, whatever was picked before.
	float width = ImGui::GetContentRegionAvail().x;
	std::vector<ModelPreview::Piece> pieces;
	for (const auto& piece : PlayerAvatar::Standing(wardrobe ? look : MonkeyLook{}))
		if (uint32_t model = modelLookup ? modelLookup(piece.model) : 0)
			pieces.push_back({ model, piece.transform, piece.tint });
	ModelPreview::DrawScene(pieces, width, (std::min)(width * 1.3f, 320.f));

	ImGui::PushTextWrapPos(0.f);
	if (!wardrobe)
	{
		ImGui::TextDisabled("You're the shop's monkey. Dressing it (fur, face, cap and tail) comes with the Wardrobe perk (Rank & Skills tab).");
		ImGui::PopTextWrapPos();
		return;
	}

	// What to dress it in, underneath.
	ImGui::PushItemWidth((std::min)(width, 200.f));
	// With Premium Colours, gold, chrome and neon on the end of the fur and cap rows.
	auto with = [&](const Swatch* first, size_t count, const Swatch* extra, size_t extras) {
		std::vector<Swatch> swatches(first, first + count);
		if (premium)
			swatches.insert(swatches.end(), extra, extra + extras);
		return swatches;
	};
	ColorRow("Fur", look.fur, with(FURS, std::size(FURS), PREMIUM_FURS, std::size(PREMIUM_FURS)));
	ColorRow("Face, hands and feet", look.skin, { std::begin(SKINS), std::end(SKINS) });
	ColorRow("Cap", look.cap, with(CAPS, std::size(CAPS), PREMIUM_CAPS, std::size(PREMIUM_CAPS)));
	if (!premium)
		ImGui::TextDisabled("Gold, chrome and neon come with the Premium Colours perk (Rank & Skills tab).");
	ImGui::SeparatorText("Tail");
	ImGui::Checkbox("Curly tail", &look.tail);
	ImGui::PopItemWidth();
	ImGui::Spacing();
	if (ImGui::Button("Back to the shop's monkey"))
		look = {};
	ImGui::TextDisabled("%s", std::format("Everyone in the garage sees your monkey as you dress it. {} shows you yourself.",
		Controls::NameOf(Control::THIRD_PERSON)).c_str());
	ImGui::PopTextWrapPos();
}
