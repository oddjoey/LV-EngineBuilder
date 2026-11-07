#include "MonkeyUI.h"

#include <cmath>
#include <format>
#include <iterator>

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

	ImU32 ToImU32(uint32_t rgb)
	{
		return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
	}

	// A row of colour buttons, then the colour picker for anything else. The one in use has a ring.
	void ColorRow(const char* label, uint32_t& color, const Swatch* swatches, size_t count)
	{
		ImGui::SeparatorText(label);
		ImGui::PushID(label);
		for (size_t i = 0; i < count; i++)
		{
			if (i > 0)
				ImGui::SameLine();
			ImGui::PushID(static_cast<int>(i));
			ImVec4 shade = ImGui::ColorConvertU32ToFloat4(ToImU32(swatches[i].color));
			if (ImGui::ColorButton(swatches[i].name, shade, ImGuiColorEditFlags_NoTooltip, { 30.f, 30.f }))
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
		ImGui::SameLine(0.f, 16.f);
		if (ImGui::ColorEdit3("##Custom", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
			color = MonkeyLook::Pack({ rgb[0], rgb[1], rgb[2] });
		ImGui::SameLine();
		ImGui::TextDisabled("Any colour");
		ImGui::PopID();
	}

}

void MonkeyUI::Draw(MonkeyLook& look, const std::function<uint32_t(const std::string&)>& modelLookup)
{
	// Your monkey, turning, built from the same pieces it's made of in the shop.
	constexpr float PORTRAIT_WIDTH = 240.f, PORTRAIT_HEIGHT = 340.f;
	std::vector<ModelPreview::Piece> pieces;
	for (const auto& piece : PlayerAvatar::Standing(look))
		if (uint32_t model = modelLookup ? modelLookup(piece.model) : 0)
			pieces.push_back({ model, piece.transform, piece.tint });
	ModelPreview::DrawScene(pieces, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
	ImGui::SameLine(0.f, 24.f);

	ImGui::BeginGroup();
	ImGui::PushItemWidth(200.f);
	ColorRow("Fur", look.fur, FURS, std::size(FURS));
	ColorRow("Face, hands and feet", look.skin, SKINS, std::size(SKINS));
	ColorRow("Cap", look.cap, CAPS, std::size(CAPS));
	ImGui::SeparatorText("Tail");
	ImGui::Checkbox("Curly tail", &look.tail);
	ImGui::PopItemWidth();
	ImGui::Spacing();
	if (ImGui::Button("Back to the shop's monkey"))
		look = {};
	ImGui::TextDisabled("%s", std::format("Everyone in the garage sees your monkey as you dress it. {} shows you yourself.",
		Controls::NameOf(Control::THIRD_PERSON)).c_str());
	ImGui::EndGroup();
}
