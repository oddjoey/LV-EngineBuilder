#include "DevMenu.h"

#include <imgui.h>

#include "../World/ShopWorld.h"

void DevMenu::Toggle()
{
	m_open = !m_open;
}

void DevMenu::Close()
{
	m_open = false;
}

bool DevMenu::IsOpen() const
{
	return m_open;
}

const DevSettings& DevMenu::GetSettings() const
{
	return m_settings;
}

void DevMenu::Draw()
{
	if (!m_open)
		return;

	ImGui::SetNextWindowPos({ 20.f, 60.f }, ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Developer (F3)", &m_open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Checkbox("X-ray part geometry", &m_settings.xray);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("The workshop screens show each part's true geometry next to your measurements.");

		ImGui::Checkbox("Bounding boxes", &m_settings.showBounds);
		if (m_settings.showBounds)
		{
			// What each colour means.
			const std::pair<uint32_t, const char*> legend[] = {
				{ DEBUG_WALL_COLOR, "Walls" },
				{ DEBUG_OBSTACLE_COLOR, "Blocks walking" },
				{ DEBUG_SURFACE_COLOR, "Surfaces to set things on" },
				{ DEBUG_STATION_COLOR, "Equipment" },
				{ DEBUG_PACKAGE_COLOR, "Deliveries" },
				{ DEBUG_ITEM_COLOR, "Parts and tools" },
			};
			ImGui::Indent();
			for (const auto& [color, label] : legend)
			{
				ImVec2 min = ImGui::GetCursorScreenPos();
				float size = ImGui::GetTextLineHeight();
				ImGui::GetWindowDrawList()->AddRectFilled({ min.x, min.y + 2.f }, { min.x + size - 4.f, min.y + size - 2.f }, color);
				ImGui::Dummy({ size, size });
				ImGui::SameLine();
				ImGui::TextUnformatted(label);
			}
			ImGui::Unindent();
		}
	}
	ImGui::End();
}
