#pragma once

#include <cmath>

// The LE86's body, in the car's own space (+X its nose, +Y up from the ground, +Z its right side): the lines its
// panels are cut to. The models (ShopAssetsCar.cpp) and where they sit on the car (ShopWorldCar.cpp) both come from
// these, so the panels meet: a shut line is GAP wide, and what's either side of it is flush.
namespace CarBody
{
	constexpr float GAP = 0.004f;				// a shut line
	constexpr float SIDE = 0.80f;				// the side skins' outer faces, from the belt down
	constexpr float SKIN = 0.035f;				// how thick a side skin is
	constexpr float SILL_Y = 0.20f;				// the bottom of the body between the wheels
	constexpr float ROCKER_TOP = 0.30f;			// the top of the sill: the door shuts down onto it
	constexpr float BELT_Y = 0.86f;				// the beltline: the doors' and quarters' tops, the cowl and the deck
	constexpr float ROOF_Y = 1.25f;				// the roof's edge
	constexpr float NOSE_X = 2.07f;
	constexpr float TAIL_X = -2.07f;
	constexpr float NOSE_Y = 0.79f;				// the hood line at the nose: it falls from the cowl
	constexpr float DOOR_FRONT_X = 0.85f;		// the door's shut lines: against the fender, and against the quarter
	constexpr float DOOR_REAR_X = -0.45f;
	constexpr float HOOD_REAR_X = 0.97f;		// the cowl is between the door's line and this
	constexpr float HOOD_HALF = 0.70f;			// the fenders' tops come in to here; the hood is between them
	constexpr float TRUNK_FRONT_X = -1.56f;		// the deck under the rear window ends here
	constexpr float TRUNK_HALF = 0.66f;			// the quarters' tops come in to here; the lid is between them
	constexpr float FRONT_AXLE_X = 1.2f;
	constexpr float REAR_AXLE_X = -1.2f;
	constexpr float AXLE_Y = 0.29f;
	constexpr float ARCH_RADIUS = 0.34f;
	// The glass and pillars lean in from the belt to the roof (tumblehome): the side plane's z at each.
	constexpr float GLASS_Z = 0.775f;
	constexpr float ROOF_Z = 0.665f;
	// The windshield from the cowl up to the roof, and the rear window from the roof down to the deck (side on).
	constexpr float WINDSHIELD_BASE_X = 0.88f;
	constexpr float WINDSHIELD_TOP_X = 0.44f;
	constexpr float REAR_WINDOW_TOP_X = -0.98f;
	constexpr float REAR_WINDOW_BASE_X = -1.50f;
	// The fuel filler's flap in the right quarter: its middle, and half its size.
	constexpr float FLAP_X = -1.68f;
	constexpr float FLAP_Y = 0.70f;
	constexpr float FLAP_HALF = 0.065f;
	// Body mouldings along the sides, the same height on every panel.
	constexpr float RUB_STRIP_Y = 0.52f;

	// The top of the fenders and the hood between them at x: level along the cowl, then falling to the nose.
	inline float HoodY(float x)
	{
		return x <= HOOD_REAR_X ? BELT_Y : BELT_Y + (NOSE_Y - BELT_Y) * (x - HOOD_REAR_X) / (NOSE_X - HOOD_REAR_X);
	}
	// How far the hood line is tipped nose down, radians.
	inline float HoodPitch() { return -std::atan2(BELT_Y - NOSE_Y, NOSE_X - HOOD_REAR_X); }
	// The side plane of the glass and pillars at height y (from the belt up to the roof).
	inline float SideZ(float y) { return GLASS_Z + (ROOF_Z - GLASS_Z) * (y - BELT_Y) / (ROOF_Y - BELT_Y); }

	// How far each opens, radians: the doors about their front edges, the hood about its back edge, the trunk lid about
	// its front edge, the fuel flap about its front edge.
	constexpr float DOOR_OPEN = 0.96f;
	constexpr float HOOD_OPEN = 0.9f;
	constexpr float TRUNK_OPEN = 1.05f;
	constexpr float FLAP_OPEN = 1.45f;
}
