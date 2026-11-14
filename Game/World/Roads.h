#pragma once

#include <vector>

#include <glm/glm.hpp>

// The roads over the desert round the shop, as lines on the ground (x and z, in metres): the highway, which runs
// along in front of the lot and on round behind it as a circuit, and the back roads off it. Their models
// (ShopOutdoors.cpp), what's kept off them, what a tire grips on and the driver's map all come from here.
namespace Roads
{
	// One strip of asphalt: a carriageway of the highway, or a back road.
	struct Strip
	{
		std::vector<glm::vec2> line = {};	// along its middle, a point every tile of its asphalt
		bool loop = false;
		float halfWidth = 3.5f;
		float top = 0.f;					// how high its surface lies: each a little over the ones it crosses
		// A carriageway: two lanes the same way, a yellow line along the side the median is on (to the left of the
		// way its line runs, +1, or the right, -1). Otherwise a lane each way.
		float medianSide = 0.f;
		glm::vec2 min = {}, max = {};		// what it covers, edges and all
	};
	const std::vector<Strip>& Strips();

	// The skid pan: a square of asphalt beside a back road, for going round in circles on.
	struct Pad
	{
		glm::vec2 min = {}, max = {};
		float top = 0.f;
	};
	const Pad& SkidPan();

	// How far a point is from the nearest asphalt (none or less on it).
	float DistanceOff(glm::vec2 point);
	inline bool On(glm::vec2 point, float margin = 0.f) { return DistanceOff(point) <= margin; }

	// The lap's line, across the highway's straight in front of the lot: x here, between these z.
	constexpr float START_LINE_X = 30.f;
	// How far everything reaches, for the map.
	glm::vec2 Min();
	glm::vec2 Max();
}
