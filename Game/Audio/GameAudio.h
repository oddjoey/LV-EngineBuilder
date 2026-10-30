#pragma once

#include <array>
#include <vector>

#include "LV/Engine/Audio/AudioManager.h"
#include "Sounds.h"

class FirstPersonController;
class ShopWorld;
class Workshop;
struct GameSettings;

// Everything the game sounds like. There are no sound files: every sound is synthesised when the game
// starts, from noise and tones shaped to match (a footstep on concrete is a short, dull burst; a
// ratchet is a run of bright ticks), with a few variants each so repeats don't sound identical. Each
// frame it plays what the shop reports, times footsteps to the player's stride, keeps the motors and
// the dyno engine running while they run, and lays the outdoor and shop ambience underneath.
class GameAudio
{
public:
	void Init();
	void Update(float deltaTime, const FirstPersonController& player, ShopWorld& world, const Workshop& workshop,
		const GameSettings& settings, float timeOfDay);
	// Heard the same anywhere: a menu click.
	void PlayInterface(Sound sound, float volume = 1.f);
private:
	void Play(const SoundCue& cue);
	// How loud and how far to one side something at position sounds from where the player is looking.
	void Place(const glm::vec3& position, float& volume, float& pan) const;
	void Footsteps(float deltaTime, const FirstPersonController& player, const ShopWorld& world);
	void Loops(float deltaTime, ShopWorld& world, float timeOfDay);
	void Bookkeeping(const Workshop& workshop);

	AudioManager*						m_audio = nullptr;
	std::array<std::vector<AudioManager::Clip>, static_cast<size_t>(Sound::COUNT)> m_clips = {};
	AudioManager::Clip					m_engineClip = 0, m_doorClip = 0, m_liftClip = 0, m_windClip = 0, m_humClip = 0;
	AudioManager::Loop					m_engineLoop = 0, m_doorLoop = 0, m_liftLoop = 0, m_windLoop = 0, m_humLoop = 0;

	glm::vec3							m_listener = {};
	glm::vec3							m_listenerRight = { 1.f, 0.f, 0.f };
	glm::vec3							m_lastFeet = {};
	float								m_stride = 0.f;
	bool								m_wasGrounded = true;
	float								m_fallSpeed = 0.f;
	float								m_birdTimer = 3.f;
	unsigned							m_variant = 0;

	// The workshop's tallies last frame: a change is a purchase, a sale, a reading or a cut.
	double								m_lastMoney = -1.0;
	int									m_lastMeasurements = -1;
	int									m_lastCuts = -1;
};
