#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "LV/Engine/Objects/Object.h"
#include "GarageLayout.h"

class ShopAssets;

// Time of day: where the sun is, the colour of the sky, the shop lamps and the street lights
// coming on at dusk. Hands the result to the renderer every frame.
class ShopLighting
{
public:
	// Spawns the sky dome. Call after the shop assets are loaded.
	void Build(const ShopAssets& assets);

	// hours: 0 to 24. doorOpen: 0 shut to 1 rolled up, which lets more daylight into the shop.
	// brightness multiplies the exposure.
	// interior: the shop as it stands (upgrades and all), lit by its own lamps.
	void Update(const glm::vec3& cameraPosition, float farPlane, float hours, float doorOpen, float brightness,
		const std::vector<pObject>& lampHeads, const ShopInterior& interior);

	// "14:05"
	static std::string ClockText(float hours);
private:
	pObject		m_sky = nullptr;
};
