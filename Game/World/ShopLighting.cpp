#include "ShopLighting.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <glm/gtc/constants.hpp>

#include "LV/Engine/Graphics/Renderer.h"
#include "LV/Engine/Objects/ObjectManager.h"
#include "GarageLayout.h"
#include "ShopAssets.h"

namespace
{
	constexpr float SUNRISE = 6.f;
	constexpr float SUNSET = 20.f;

	const glm::vec3 NOON_SUN = { 2.15f, 2.02f, 1.82f };
	const glm::vec3 LOW_SUN = { 2.2f, 1.15f, 0.5f };
	const glm::vec3 MOONLIGHT = { 0.03f, 0.04f, 0.07f };
	const glm::vec3 MOON_DIRECTION = glm::normalize(glm::vec3(-0.4f, 0.8f, 0.45f));

	const glm::vec3 DAY_ZENITH = { 0.20f, 0.38f, 0.75f };
	const glm::vec3 DUSK_ZENITH = { 0.22f, 0.24f, 0.42f };
	const glm::vec3 NIGHT_ZENITH = { 0.008f, 0.012f, 0.03f };
	const glm::vec3 DAY_HORIZON = { 0.62f, 0.72f, 0.85f };
	const glm::vec3 DUSK_HORIZON = { 0.95f, 0.55f, 0.32f };
	const glm::vec3 NIGHT_HORIZON = { 0.025f, 0.03f, 0.05f };
	const glm::vec3 DAY_GROUND = { 0.20f, 0.19f, 0.15f };
	const glm::vec3 NIGHT_GROUND = { 0.01f, 0.012f, 0.015f };

	const glm::vec3 SHOP_LAMP = glm::vec3(1.f, 0.96f, 0.88f) * 4.f;
	const glm::vec3 STREET_LAMP = glm::vec3(1.f, 0.78f, 0.5f) * 14.f;
	const glm::vec3 PORCH_LAMP = glm::vec3(1.f, 0.85f, 0.6f) * 5.f;
	// Warmer and dimmer than the shop's, and not shadowed: the house's rooms are small.
	const glm::vec3 HOUSE_LAMP = glm::vec3(1.f, 0.88f, 0.7f) * 2.6f;
}

void ShopLighting::Build(const ShopAssets& assets)
{
	Object info({}, glm::quat(1.f, 0.f, 0.f, 0.f), { 1.f, 1.f, 1.f }, assets.Model(ShopModels::SKY_DOME), 0);
	m_sky = ObjectManager::Get()->CreateObject(info);
	m_sky->SetShading(ObjectShading::SKY);
	m_sky->SetCastsShadows(false);
}

void ShopLighting::Update(const glm::vec3& cameraPosition, float farPlane, float hours, float doorOpen, float brightness,
	const std::vector<pObject>& lampHeads, const ShopInterior& interior)
{
	using namespace GarageLayout;

	// The sun rises in the east (+X), crosses the sky to the south (over the street, +Z) and
	// sets in the west.
	float dayAngle = (hours - SUNRISE) / (SUNSET - SUNRISE) * glm::pi<float>();
	// The shadows follow the sun in steps of about a tenth of a degree: too small to see, but between
	// steps the shadow maps stay as they are and needn't be drawn again (see Renderer).
	constexpr float SHADOW_STEP = 0.002f;
	float shadowAngle = std::round(dayAngle / SHADOW_STEP) * SHADOW_STEP;
	glm::vec3 sunDirection = glm::normalize(glm::vec3(std::cos(shadowAngle), std::sin(shadowAngle), 0.6f));
	float elevation = sunDirection.y;
	float daylight = glm::smoothstep(-0.12f, 0.25f, elevation);
	float warmth = 1.f - glm::smoothstep(0.f, 0.45f, elevation);
	float sunUp = glm::smoothstep(-0.02f, 0.08f, elevation);

	SceneLighting lighting;
	glm::vec3 sunColor = glm::mix(NOON_SUN, LOW_SUN, warmth) * sunUp;
	glm::vec3 moonColor = MOONLIGHT * (1.f - daylight);
	// Whichever is brighter casts the shadows.
	if (sunUp > 0.05f)
	{
		lighting.sunDirection = sunDirection;
		lighting.sunColor = sunColor;
	}
	else
	{
		lighting.sunDirection = MOON_DIRECTION;
		lighting.sunColor = moonColor;
	}

	lighting.skyZenith = glm::mix(NIGHT_ZENITH, glm::mix(DAY_ZENITH, DUSK_ZENITH, warmth), daylight);
	lighting.skyHorizon = glm::mix(NIGHT_HORIZON, glm::mix(DAY_HORIZON, DUSK_HORIZON, warmth), daylight);
	lighting.groundColor = glm::mix(NIGHT_GROUND, DAY_GROUND, daylight);
	lighting.fogColor = lighting.skyHorizon;
	lighting.fogDensity = 0.004f;

	// The shop and the house: dim ambient plus whatever daylight gets in through the windows and the door.
	constexpr float MARGIN = 0.04f;
	lighting.interiors.push_back({ interior.min - glm::vec3(MARGIN, 0.f, MARGIN), interior.max + glm::vec3(MARGIN) });
	lighting.interiors.push_back({ interior.houseMin - glm::vec3(MARGIN, 0.f, MARGIN), interior.houseMax + glm::vec3(MARGIN) });
	// Kept modest so the (shadowed) lamps do most of the lighting and things look grounded.
	lighting.interiorAmbient = glm::vec3(0.05f, 0.05f, 0.055f) + glm::vec3(0.9f, 0.95f, 1.f) * daylight * (0.06f + 0.05f * doorOpen);

	// The ceiling lamps shine down on everything in the shop, so they cast shadows: machines,
	// parts and tools sit on the floor and benches instead of floating.
	for (const auto& lamp : interior.lamps)
	{
		PointLight light = { lamp - glm::vec3(0.f, 0.1f, 0.f), 9.f, SHOP_LAMP, LightZone::INTERIOR };
		light.castsShadows = true;
		lighting.pointLights.push_back(light);
	}
	for (const auto& lamp : interior.houseLamps)
		lighting.pointLights.push_back({ lamp - glm::vec3(0.f, 0.08f, 0.f), 5.5f, HOUSE_LAMP, LightZone::INTERIOR });

	// Street and porch lights come on at dusk.
	float lampsOn = 1.f - glm::smoothstep(0.25f, 0.55f, daylight);
	if (lampsOn > 0.f)
	{
		for (const auto& pole : STREET_LIGHTS)
			lighting.pointLights.push_back({ pole + glm::vec3(0.f, STREET_LIGHT_HEIGHT - 0.35f, 1.15f), 16.f, STREET_LAMP * lampsOn, LightZone::EXTERIOR });
		lighting.pointLights.push_back({ DOOR_LIGHT + glm::vec3(0.f, -0.2f, 0.25f), 8.f, PORCH_LAMP * lampsOn, LightZone::EXTERIOR });
		lighting.pointLights.push_back({ PORCH_LIGHT + glm::vec3(0.f, -0.15f, 0.f), 7.f, PORCH_LAMP * lampsOn, LightZone::EXTERIOR });
	}
	for (const auto& head : lampHeads)
		head->SetEmissive(2.5f * lampsOn);

	// Let the eye adjust a little at night so it stays playable.
	lighting.exposure = brightness * glm::mix(1.35f, 1.f, daylight);

	Renderer::Get()->SetSceneLighting(lighting);

	m_sky->SetPosition(cameraPosition);
	m_sky->SetScale(glm::vec3(farPlane * 0.9f));
}

std::string ShopLighting::ClockText(float hours)
{
	int minutes = static_cast<int>(std::fmod(hours, 24.f) * 60.f);
	return std::format("{:02}:{:02}", minutes / 60, minutes % 60);
}
