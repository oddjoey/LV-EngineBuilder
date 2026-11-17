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
	constexpr float NOSE_Y = 0.70f;				// the hood line at the nose: it falls from the cowl to a low wedge
	// The pop-up headlights' lids, let into the hood's front corners: how far back from the nose, and from where across.
	constexpr float LAMP_LID_LENGTH = 0.31f;
	constexpr float LAMP_LID_INNER = 0.37f;
	constexpr float LAMP_BASE_Y = 0.56f;		// the bottom of a headlight's bucket
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
	constexpr float FULL_LOCK = 0.6f;		// the front wheels turned as far as they go, radians
	// The pop-up headlights: each pod tilts up about the back edge of its lid by this much, radians (which lifts the
	// lid's front edge near a quarter of a metre), and the lamp's face under it is this tall.
	constexpr float LAMP_POP = 0.7f;
	constexpr float LAMP_FACE = 0.22f;

	// The engine in its bay: the middle of its block's deck (its crank a quarter of a metre under that, along the car),
	// forward of the firewall far enough for the distributor on the back of its head; and the rubber mounts it sits on,
	// either side of its crankcase on perches off the chassis rails.
	constexpr float ENGINE_X = 1.26f;
	constexpr float ENGINE_DECK_Y = 0.59f;
	constexpr float CRANK_AXIS_Y = ENGINE_DECK_Y - 0.25f;
	constexpr float BLOCK_REAR_X = ENGINE_X - 0.205f;	// the block's back face, the gearbox's bellhousing against it
	constexpr float ENGINE_MOUNT_Y = ENGINE_DECK_Y - 0.29f;	// the perches' tops
	constexpr float ENGINE_MOUNT_Z = 0.24f;
	constexpr float FIREWALL_X = 0.88f;
	// The plumbing in the bay: the clutch's master cylinder on the firewall at the driver's feet; the fuel filter in the
	// other back corner (its base: it stands this tall, a stub out of each end), and under it the end of the fuel line,
	// up through the top of the wheel housing; and the brake pipes' junction block on the driver's wheel housing, under
	// the master cylinder.
	constexpr float CLUTCH_MASTER[3] = { 0.93f, 0.66f, -0.56f };
	constexpr float FUEL_FILTER[3] = { 0.93f, 0.67f, 0.5f };
	constexpr float FUEL_FILTER_HEIGHT = 0.125f;
	constexpr float IGNITION_COIL[3] = { 1.63f, 0.55f, 0.61f };	// on the inner wing, ahead of the wheel housing
	constexpr float FUEL_RISER[3] = { 0.905f, 0.585f, 0.43f };
	constexpr float BRAKE_BLOCK[3] = { 1.15f, 0.52f, -0.385f };
	// The front wheels' housings in the engine bay: a tub this radius about the axle, walled off from the bay this far out.
	constexpr float FRONT_TUB = 0.40f;
	constexpr float FRONT_TUB_WALL_Z = 0.40f;
	// The instrument cluster's dials, in its own space (it faces -z, the driver's left +x): the two big ones either
	// side of its middle, the two little ones between them, one over the other; how big each is, and how far its
	// needle swings from its lowest mark to its highest (about straight up).
	constexpr float GAUGE_Y = 0.072f;
	constexpr float GAUGE_BIG_X = 0.075f, GAUGE_BIG = 0.058f, GAUGE_SWEEP = 4.19f;
	// The three little ones are bars along the bottom, a needle swinging under each from a pivot below it: the fuel
	// gauge out on the driver's left, the oil pressure and temperature gauges on the right.
	constexpr float GAUGE_FUEL_X = 0.165f, GAUGE_OIL_X = -0.147f, GAUGE_TEMP_X = -0.176f;
	constexpr float GAUGE_SMALL_Y = 0.03f, GAUGE_SMALL = 0.026f, GAUGE_SMALL_SWEEP = 1.0f;
	constexpr float SPEEDO_TOP = 180.f, TACH_TOP = 8000.f;
	// What's worked by hand from the seats, in the car's space: the headlights' knob on the dash by the driver's door,
	// the hood's release under the dash beside it, and the ignition's barrel on the dash right of the column.
	constexpr float DASH_X = 0.6f, DASH_Y = 0.55f;		// where the dashboard sits (it's built across the car)
	constexpr float HEADLIGHT_KNOB[3] = { 0.33f, 0.745f, -0.628f };
	constexpr float HOOD_RELEASE[3] = { 0.4f, 0.5f, -0.66f };
	constexpr float IGNITION_BARREL[3] = { 0.33f, 0.665f, -0.12f };
	// Where the steering's tie rods run, under the engine's sump.
	constexpr float TIE_ROD_Y = 0.165f;
	// The doors' inside handles, on the door's own model (from its hinge line: how far back along it, how far up from
	// its bottom edge, and how far in from its outer skin the lever stands): on the cloth of the card behind the
	// dashboard's end, where they show with the door shut.
	constexpr float DOOR_HANDLE_X = -0.6f, DOOR_HANDLE_Y = 0.47f, DOOR_HANDLE_IN = 0.094f;
	// A front seat, in its own space (it faces +x, its frame on y = 0). Its back hinges on the cushion's back edge:
	// leant back by SEAT_RECLINE as it comes (the recliner's knob takes it from nearly upright to well back), and
	// tipped forward to SEAT_FOLD to let someone into the back. The whole seat slides this far either way on its
	// rails. What's worked by hand: the slide's bar under the front of the cushion, the recliner's knob by the hinge
	// (both on the seat's base), and the fold's catch on the shoulder of the back (on the back, from its hinge).
	constexpr float SEAT_HINGE_X = -0.2f, SEAT_HINGE_Y = 0.13f;
	constexpr float SEAT_RECLINE = 0.26f, SEAT_RECLINE_MIN = 0.06f, SEAT_RECLINE_MAX = 0.8f, SEAT_RECLINE_STEP = 0.06f;
	constexpr float SEAT_FOLD = -0.72f;
	constexpr float SEAT_SLIDE = 0.1f, SEAT_SLIDE_STEP = 0.02f;
	constexpr float SEAT_SLIDE_BAR[3] = { 0.26f, 0.05f, 0.f };
	constexpr float SEAT_RECLINER[3] = { -0.18f, 0.1f, -0.265f };
	constexpr float SEAT_FOLD_CATCH[3] = { 0.f, 0.47f, -0.245f };
	constexpr float SEAT_X = -0.3f, SEAT_Y = 0.22f, SEAT_Z = 0.37f;		// where a front seat sits in the car, its slide in the middle
	// The back seat: a bench across the car, each half of it a place to sit (a box round it, for aiming at it).
	constexpr float REAR_SEAT_FRONT_X = -0.67f, REAR_SEAT_BACK_X = -1.27f, REAR_SEAT_BASE_Y = 0.24f, REAR_SEAT_TOP_Y = 0.87f, REAR_SEAT_HALF = 0.57f;
	// The exhaust, down the car's left (the side the engine's manifold is on). The engine's own downpipe drops from the
	// manifold forward of the left engine mount and ends at a flange low in the bay (EXHAUST_JOINT, in the car's space).
	// The front pipe bolts on there, runs back over the front crossmember and down under the floor along EXHAUST_Z,
	// clear of the starter, to the catalytic converter; the pipe on from that swings out to the silencer behind the back
	// axle (MUFFLER_Z), and the tailpipe ends under the bumper, where the smoke comes out.
	constexpr float EXHAUST_JOINT[3] = { ENGINE_X + 0.12f, 0.30f, -0.24f };
	constexpr float EXHAUST_Z = -0.28f, EXHAUST_Y = 0.12f, EXHAUST_PIPE_RADIUS = 0.022f;
	constexpr float CATALYST[3] = { 0.15f, 0.f, EXHAUST_Z };		// the converter's place (its model is built in the car's height)
	constexpr float CATALYST_HALF = 0.2f;							// half its length
	constexpr float MUFFLER_Z = -0.42f, MUFFLER_X = -1.55f, MUFFLER_Y = 0.04f, MUFFLER_RADIUS = 0.06f, MUFFLER_HALF = 0.25f;
	constexpr float TAILPIPE[3] = { -2.26f, MUFFLER_Y + MUFFLER_RADIUS, MUFFLER_Z };
	// The handbrake's lever between the seats: its pivot at the back, on the tunnel, and how far it comes up, radians.
	constexpr float HANDBRAKE_PIVOT[3] = { -0.36f, 0.46f, 0.03f };
	constexpr float HANDBRAKE_PULL = 0.42f;
	// Where the car's lifted from, either side at each end: the jacking point on the sill behind the front wheel (or
	// ahead of the back one), and beside it, further in under the floor, the place a stand goes. (x and z are mirrored
	// for the other three corners.) And how far a corner with no wheel on it sinks, onto its brake.
	constexpr float JACK_POINT[3] = { 0.66f, 0.18f, 0.72f };
	constexpr float STAND_POINT[3] = { 0.42f, 0.18f, 0.56f };
	constexpr float NO_WHEEL_DROP = 0.15f;
	// A tail light's stop and tail lamp: where its middle is on the lamp's own model (toward the car's middle from
	// the lamp's own, by this), and its size.
	constexpr float STOP_LAMP_IN = 0.0425f, STOP_LAMP_Y = 0.0925f, STOP_LAMP_WIDE = 0.185f, STOP_LAMP_HIGH = 0.155f;
}
