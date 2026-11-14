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
	const glm::vec3 DAY_HORIZON = { 0.78f, 0.75f, 0.68f };	// dusty desert haze
	const glm::vec3 DUSK_HORIZON = { 0.95f, 0.55f, 0.32f };
	const glm::vec3 NIGHT_HORIZON = { 0.025f, 0.03f, 0.05f };
	const glm::vec3 DAY_GROUND = { 0.34f, 0.27f, 0.19f };	// light bounced off the sand
	const glm::vec3 NIGHT_GROUND = { 0.01f, 0.012f, 0.015f };

	const glm::vec3 SHOP_LAMP = glm::vec3(1.f, 0.96f, 0.88f) * 4.f;
	const glm::vec3 STREET_LAMP = glm::vec3(1.f, 0.78f, 0.5f) * 14.f;
	const glm::vec3 PORCH_LAMP = glm::vec3(1.f, 0.85f, 0.6f) * 5.f;
}

void ShopLighting::Build(const ShopAssets& assets)
{
	Object info({}, glm::quat(1.f, 0.f, 0.f, 0.f), { 1.f, 1.f, 1.f }, assets.Model(ShopModels::SKY_DOME), 0);
	m_sky = ObjectManager::Get()->CreateObject(info);
	m_sky->SetShading(ObjectShading::SKY);
	m_sky->SetCastsShadows(false);
	// The mountains round the horizon, kept centred on the camera like the sky.
	Object mountains({}, glm::quat(1.f, 0.f, 0.f, 0.f), { 1.f, 1.f, 1.f }, assets.Model(ShopModels::MOUNTAINS),
		assets.TextureFor(ShopModels::MOUNTAINS, ShopTextures::ROCK));
	m_mountains = ObjectManager::Get()->CreateObject(mountains);
	m_mountains->SetCastsShadows(false);
}

void ShopLighting::Update(const glm::vec3& cameraPosition, float farPlane, float hours, float doorOpen, float brightness,
	const std::vector<pObject>& lampHeads, const ShopInterior& interior, const std::vector<PointLight>& carLights)
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
	lighting.fogDensity = 0.003f;

	// The shack: dim ambient plus whatever daylight gets in through the windows and the door. Its zone reaches into
	// the walls a little, past the inside of the roll-up door when it's down (4 cm out) and short of the window glass
	// (5.5 cm): a face right on the edge of it flickers between lit inside and out.
	constexpr float MARGIN = 0.05f;
	lighting.interiors.push_back({ interior.min - glm::vec3(MARGIN, 0.f, MARGIN), interior.max + glm::vec3(MARGIN) });
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
	// Street lights and the one over the door come on at dusk.
	float lampsOn = 1.f - glm::smoothstep(0.25f, 0.55f, daylight);
	if (lampsOn > 0.f)
	{
		for (const auto& pole : STREET_LIGHTS)
			lighting.pointLights.push_back({ pole + glm::vec3(0.f, STREET_LIGHT_HEIGHT - 0.35f, STREET_LIGHT_REACH), 20.f, STREET_LAMP * lampsOn, LightZone::EXTERIOR });
		lighting.pointLights.push_back({ DOOR_LIGHT + glm::vec3(0.f, -0.2f, 0.25f), 8.f, PORCH_LAMP * lampsOn, LightZone::EXTERIOR });
	}
	for (const auto& head : lampHeads)
		head->SetEmissive(2.5f * lampsOn);
	// The car's headlights: lost in the daylight, bright on the road at night.
	for (auto light : carLights)
		if (lighting.pointLights.size() < static_cast<size_t>(MAX_POINT_LIGHTS))
		{
			light.color *= glm::mix(1.f, 0.12f, daylight);
			lighting.pointLights.push_back(light);
		}

	// Let the eye adjust a little at night so it stays playable.
	lighting.exposure = brightness * glm::mix(1.35f, 1.f, daylight);

	Renderer::Get()->SetSceneLighting(lighting);

	m_sky->SetPosition(cameraPosition);
	m_sky->SetScale(glm::vec3(farPlane * 0.9f));
	// The mountains just inside it, on the ground: always on the horizon however far you go, or can see.
	m_mountains->SetPosition({ cameraPosition.x, 0.f, cameraPosition.z });
	m_mountains->SetScale(glm::vec3(farPlane * 0.85f));
}

std::string ShopLighting::ClockText(float hours)
{
	int minutes = static_cast<int>(std::fmod(hours, 24.f) * 60.f);
	return std::format("{:02}:{:02}", minutes / 60, minutes % 60);
}
