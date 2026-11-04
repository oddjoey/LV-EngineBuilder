#pragma once

#include <cstdint>

#include <imgui.h>

// A small turntable view of one of the shop's models, drawn straight into the ImGui window: the
// model's triangles are turned, lit and sorted back to front on the CPU. Fine for a handful of items.
namespace ModelPreview
{
	// Takes up a width x height spot in the current window. tint multiplies the model's colours.
	void Draw(uint32_t modelIndex, float width, float height, ImVec4 tint = { 1.f, 1.f, 1.f, 1.f });
	// Draws the model alone (no background) into min - max of any draw list, for pages that lay
	// themselves out. Several can be on screen at once: each keeps its own picture.
	void DrawAt(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, uint32_t modelIndex, ImVec4 tint = { 1.f, 1.f, 1.f, 1.f });
}
