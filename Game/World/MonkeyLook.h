#pragma once

#include <cstdint>

#include <glm/glm.hpp>

// How a player's monkey looks: its fur, its bare face, hands and feet, its cap, and whether it has a
// tail. Chosen in the backpack's Monkey tab, kept with the settings, and sent to the other players.
// Colours are 0xRRGGBB, tinted onto the white parts of the model.
struct MonkeyLook
{
	uint32_t fur = 0x5C381F;
	uint32_t skin = 0xC79975;
	uint32_t cap = 0x2F6FD6;
	bool tail = true;

	static glm::vec4 Color(uint32_t rgb)
	{
		return { ((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, 1.f };
	}
	static uint32_t Pack(const glm::vec3& color)
	{
		auto channel = [](float value) { return static_cast<uint32_t>(glm::clamp(value, 0.f, 1.f) * 255.f + 0.5f); };
		return (channel(color.r) << 16) | (channel(color.g) << 8) | channel(color.b);
	}

	bool operator==(const MonkeyLook& other) const = default;
};
