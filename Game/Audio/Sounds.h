#pragma once

#include <glm/glm.hpp>

// Every sound the game makes. The shop reports what happened and where (a SoundCue); GameAudio
// plays it, fainter and to one side the further off and round it is.
enum class Sound
{
	FOOTSTEP_CONCRETE,
	FOOTSTEP_GRASS,
	FOOTSTEP_WOOD,
	LAND,			// coming down off a jump or a step
	PICK_UP,
	SET_DOWN,
	DROP,			// something heavy hitting the floor
	FIT_PART,		// a part going onto the engine
	RATCHET,		// a run of clicks
	WRENCH,			// a spanner pulled round
	BREAKER_BAR,	// a long pull, then the bolt cracks free
	TORQUE_CLICK,	// the torque wrench letting go at its setting
	SOCKET_CLINK,	// a socket on or off a drive
	MEASURE,		// a gauge's beep
	CRATE_OPEN,
	CASH,			// money changing hands
	MACHINE,		// a machining cut
	CLICK,			// the interface
	BIRD,
	COUNT
};

struct SoundCue
{
	Sound sound = Sound::CLICK;
	glm::vec3 position = {};
	float volume = 1.f;
	bool positional = true;		// false: heard the same wherever you are (the interface)
};
