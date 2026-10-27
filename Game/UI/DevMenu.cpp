#include "DevMenu.h"

#include <cfloat>
#include <cmath>
#include <utility>

#include <imgui.h>

#include "LV/Engine/Graphics/IMGUI/IMGUIManager.h"
#include "../World/ShopLighting.h"
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

void DevMenu::Draw(float& timeOfDay)
{
	if (!m_open)
		return;

	ImGui::SetNextWindowPos({ 20.f, 60.f }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({ 460.f, 560.f }, ImGuiCond_FirstUseEver);
	// Lordi's panels need room, even if an older, smaller size was saved.
	ImGui::SetNextWindowSizeConstraints({ 400.f, 440.f }, { FLT_MAX, FLT_MAX });
	if (ImGui::Begin("Developer (F3)", &m_open))
	{
		if (ImGui::BeginTabBar("DevTabs"))
		{
			if (ImGui::BeginTabItem("Game"))
			{
				DrawGameTab(timeOfDay);
				ImGui::EndTabItem();
			}
			// Lordi's own debug panels: performance, cameras and every object in the scene.
			if (ImGui::BeginTabItem("Lordi"))
			{
				if (ImGui::BeginChild("Lordi"))
					IMGUIManager::Get()->DrawDebugContents();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void DevMenu::DrawGameTab(float& timeOfDay)
{
	ImGui::SeparatorText("Workshop");
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

	// Jump to any time, or let the day run faster to watch the light change.
	ImGui::SeparatorText("Time of day");
	ImGui::Text("It's %s", ShopLighting::ClockText(timeOfDay).c_str());
	ImGui::SliderFloat("Time", &timeOfDay, 0.f, 23.99f, "%.2f h");
	const std::pair<const char*, float> presets[] = { { "Dawn", 6.f }, { "Morning", 9.f }, { "Noon", 12.f }, { "Dusk", 19.5f }, { "Night", 23.f } };
	for (size_t i = 0; i < std::size(presets); i++)
	{
		if (i > 0)
			ImGui::SameLine();
		if (ImGui::Button(presets[i].first))
			timeOfDay = presets[i].second;
	}
	ImGui::SliderFloat("Time speed", &m_settings.timeScale, 0.f, 200.f, "%.0fx", ImGuiSliderFlags_Logarithmic);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Multiplies how fast the day goes by (when the day and night cycle is on). 0 stops the clock.");
	ImGui::SameLine();
	if (ImGui::SmallButton("1x"))
		m_settings.timeScale = 1.f;
}
