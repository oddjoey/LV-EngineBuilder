#include "GameAudio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "../GameSettings.h"
#include "../Simulation/Workshop.h"
#include "../World/FirstPersonController.h"
#include "../World/ShopWorld.h"

namespace
{
	constexpr float RATE = static_cast<float>(AudioManager::SAMPLE_RATE);
	constexpr float TWO_PI = 6.28318530718f;

	// A small, repeatable source of noise, so the same sounds come out every run.
	struct Noise
	{
		uint32_t state;
		float Next()
		{
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			return static_cast<float>(state) / 2147483648.f - 1.f;
		}
	};

	// A buffer of float samples, built up from layers, then turned into 16-bit.
	struct Synth
	{
		std::vector<float> samples;
		explicit Synth(float seconds) : samples(static_cast<size_t>(seconds * RATE), 0.f) {}
		size_t Size() const { return samples.size(); }
		float Time(size_t i) const { return static_cast<float>(i) / RATE; }

		// A tone that dies away: frequency in Hz, decay as the time to fall to about a third.
		void Ring(float start, float frequency, float amplitude, float decay)
		{
			for (size_t i = static_cast<size_t>(start * RATE); i < Size(); i++)
			{
				float t = Time(i) - start;
				samples[i] += amplitude * std::exp(-t / decay) * std::sin(TWO_PI * frequency * t);
			}
		}
		// A burst of noise through a one-pole low-pass (cutoff in Hz), with an attack and a decay.
		void Burst(Noise& noise, float start, float length, float cutoff, float amplitude, float attack = 0.002f)
		{
			float alpha = 1.f - std::exp(-TWO_PI * cutoff / RATE);
			float smoothed = 0.f;
			size_t first = static_cast<size_t>(start * RATE), count = static_cast<size_t>(length * RATE);
			for (size_t i = first; i < first + count && i < Size(); i++)
			{
				float t = Time(i) - start;
				float envelope = (std::min)(1.f, t / attack) * std::exp(-t / (length * 0.3f));
				smoothed += alpha * (noise.Next() - smoothed);
				samples[i] += amplitude * envelope * smoothed;
			}
		}
		// Noise with the low end taken out: hiss, scrape, crunch.
		void Hiss(Noise& noise, float start, float length, float lowCut, float highCut, float amplitude, float attack = 0.003f)
		{
			float low = 1.f - std::exp(-TWO_PI * lowCut / RATE), high = 1.f - std::exp(-TWO_PI * highCut / RATE);
			float bass = 0.f, top = 0.f;
			size_t first = static_cast<size_t>(start * RATE), count = static_cast<size_t>(length * RATE);
			for (size_t i = first; i < first + count && i < Size(); i++)
			{
				float t = Time(i) - start;
				float envelope = (std::min)(1.f, t / attack) * (std::min)(1.f, (length - t) / (length * 0.4f));
				float n = noise.Next();
				top += high * (n - top);
				bass += low * (top - bass);
				samples[i] += amplitude * envelope * (top - bass);
			}
		}
		// A low thump: a falling sine.
		void Thump(float start, float frequency, float amplitude, float decay)
		{
			float phase = 0.f;
			for (size_t i = static_cast<size_t>(start * RATE); i < Size(); i++)
			{
				float t = Time(i) - start;
				phase += TWO_PI * frequency * (1.f + 0.6f * std::exp(-t / 0.01f)) / RATE;
				samples[i] += amplitude * std::exp(-t / decay) * std::sin(phase);
			}
		}

		// The last `overlap` seconds faded into the start, so the clip loops without a click.
		void MakeLoopable(float overlap)
		{
			size_t fade = static_cast<size_t>(overlap * RATE);
			if (fade * 2 >= Size())
				return;
			size_t body = Size() - fade;
			for (size_t i = 0; i < fade; i++)
			{
				float mix = static_cast<float>(i) / fade;
				samples[i] = samples[i] * mix + samples[body + i] * (1.f - mix);
			}
			samples.resize(body);
		}

		std::vector<int16_t> ToClip(float gain = 1.f) const
		{
			std::vector<int16_t> clip(Size());
			for (size_t i = 0; i < Size(); i++)
				clip[i] = static_cast<int16_t>(std::clamp(samples[i] * gain, -1.f, 1.f) * 32000.f);
			return clip;
		}
	};

	std::vector<int16_t> Footstep(Sound surface, uint32_t seed)
	{
		Noise noise{ seed * 2654435761u + 1u };
		Synth s(0.28f);
		float jitter = 0.9f + 0.2f * std::abs(noise.Next());
		switch (surface)
		{
			case Sound::FOOTSTEP_GRASS:
				// Soft: a muffled brush of noise, barely a crackle.
				s.Hiss(noise, 0.f, 0.16f * jitter, 250.f, 2500.f, 0.35f, 0.02f);
				for (int crackle = 0; crackle < 3; crackle++)
					s.Burst(noise, 0.02f + 0.03f * crackle * jitter, 0.01f, 3000.f, 0.06f, 0.001f);
				s.Thump(0.f, 70.f, 0.2f, 0.03f);
				break;
			case Sound::FOOTSTEP_WOOD:
				// Heel then toe on boards: a low, dull knock, hardly ringing.
				s.Thump(0.f, 110.f, 0.55f, 0.05f);
				s.Ring(0.f, 300.f * jitter, 0.05f, 0.03f);
				s.Burst(noise, 0.f, 0.025f, 1200.f, 0.12f);
				s.Thump(0.07f * jitter, 140.f, 0.3f, 0.04f);
				break;
			default:
				// Shoes on concrete: a dull, soft scuff, heel then toe, nothing sharp.
				s.Burst(noise, 0.f, 0.05f, 900.f * jitter, 0.35f);
				s.Thump(0.f, 85.f, 0.35f, 0.025f);
				s.Burst(noise, 0.06f * jitter, 0.04f, 1100.f, 0.18f);
				s.Hiss(noise, 0.f, 0.08f, 800.f, 3000.f, 0.03f);
				break;
		}
		return s.ToClip(0.9f);
	}

	std::vector<int16_t> Make(Sound sound, uint32_t seed)
	{
		Noise noise{ seed * 2246822519u + 7u };
		switch (sound)
		{
			case Sound::LAND:
			{
				Synth s(0.35f);
				s.Thump(0.f, 60.f, 0.9f, 0.07f);
				s.Burst(noise, 0.f, 0.08f, 1500.f, 0.6f);
				return s.ToClip();
			}
			case Sound::PICK_UP:
			{
				// A grab and a swish of cloth.
				Synth s(0.3f);
				s.Hiss(noise, 0.f, 0.22f, 200.f, 2500.f, 0.35f, 0.06f);
				s.Burst(noise, 0.f, 0.03f, 3000.f, 0.3f);
				return s.ToClip();
			}
			case Sound::SET_DOWN:
			{
				Synth s(0.3f);
				s.Thump(0.f, 95.f, 0.6f, 0.04f);
				s.Burst(noise, 0.f, 0.04f, 3500.f, 0.45f, 0.0008f);
				s.Ring(0.f, 1200.f, 0.05f, 0.03f);
				return s.ToClip();
			}
			case Sound::DROP:
			{
				// Something heavy on concrete: a thud, a clang and a rattle as it settles.
				Synth s(0.7f);
				s.Thump(0.f, 55.f, 1.f, 0.09f);
				s.Burst(noise, 0.f, 0.12f, 2500.f, 0.7f, 0.0008f);
				s.Ring(0.f, 610.f, 0.12f, 0.12f);
				s.Ring(0.f, 1530.f, 0.07f, 0.08f);
				for (int bounce = 0; bounce < 3; bounce++)
					s.Burst(noise, 0.14f + bounce * 0.07f, 0.03f, 3000.f, 0.25f / (bounce + 1), 0.0008f);
				return s.ToClip();
			}
			case Sound::FIT_PART:
			{
				// Metal seating onto metal: a solid clunk with a short ring.
				Synth s(0.45f);
				s.Thump(0.f, 120.f, 0.8f, 0.05f);
				s.Burst(noise, 0.f, 0.03f, 4000.f, 0.5f, 0.0005f);
				s.Ring(0.f, 880.f, 0.18f, 0.09f);
				s.Ring(0.f, 2210.f, 0.08f, 0.05f);
				return s.ToClip();
			}
			case Sound::RATCHET:
			{
				// A run of pawl clicks, quick at first then slowing as the bolt tightens.
				Synth s(0.75f);
				float t = 0.f;
				for (int click = 0; click < 10; click++)
				{
					s.Burst(noise, t, 0.004f, 9000.f, 0.6f, 0.0002f);
					s.Ring(t, 3300.f + 120.f * noise.Next(), 0.22f, 0.008f);
					s.Ring(t, 5200.f, 0.1f, 0.005f);
					t += 0.045f + click * 0.005f;
				}
				return s.ToClip();
			}
			case Sound::WRENCH:
			{
				// A spanner pulled round: the bolt creaks in its thread, then a tink as it's lifted off.
				Synth s(0.6f);
				s.Hiss(noise, 0.f, 0.35f, 800.f, 3500.f, 0.3f, 0.04f);
				for (int creak = 0; creak < 4; creak++)
					s.Ring(0.05f + creak * 0.07f, 420.f + creak * 30.f, 0.1f, 0.03f);
				s.Ring(0.4f, 2600.f, 0.3f, 0.06f);
				s.Ring(0.4f, 4100.f, 0.12f, 0.04f);
				return s.ToClip();
			}
			case Sound::BREAKER_BAR:
			{
				// A long strain, then the bolt lets go with a crack.
				Synth s(0.8f);
				s.Hiss(noise, 0.f, 0.45f, 300.f, 1800.f, 0.25f, 0.1f);
				s.Ring(0.f, 160.f, 0.12f, 0.3f);
				s.Burst(noise, 0.45f, 0.05f, 6000.f, 1.f, 0.0003f);
				s.Thump(0.45f, 140.f, 0.5f, 0.05f);
				s.Ring(0.45f, 1900.f, 0.2f, 0.07f);
				return s.ToClip();
			}
			case Sound::TORQUE_CLICK:
			{
				// The wrench breaking over at its setting: one sharp, solid click.
				Synth s(0.25f);
				s.Burst(noise, 0.f, 0.006f, 8000.f, 1.f, 0.0002f);
				s.Thump(0.f, 300.f, 0.4f, 0.015f);
				s.Ring(0.f, 2200.f, 0.25f, 0.035f);
				s.Burst(noise, 0.012f, 0.004f, 8000.f, 0.4f, 0.0002f);
				return s.ToClip();
			}
			case Sound::SOCKET_CLINK:
			{
				// A socket snapping onto the drive: a bright, inharmonic chink.
				Synth s(0.35f);
				s.Burst(noise, 0.f, 0.004f, 9000.f, 0.5f, 0.0002f);
				s.Ring(0.f, 2870.f, 0.3f, 0.07f);
				s.Ring(0.f, 4610.f, 0.2f, 0.05f);
				s.Ring(0.f, 6130.f, 0.1f, 0.03f);
				return s.ToClip();
			}
			case Sound::MEASURE:
			{
				// A gauge's double beep.
				Synth s(0.22f);
				for (float start : { 0.f, 0.1f })
					for (size_t i = static_cast<size_t>(start * RATE); i < static_cast<size_t>((start + 0.07f) * RATE) && i < s.Size(); i++)
					{
						float t = s.Time(i) - start;
						s.samples[i] += 0.3f * (std::min)(1.f, t / 0.003f) * (std::min)(1.f, (0.07f - t) / 0.005f) * std::sin(TWO_PI * 2400.f * t);
					}
				return s.ToClip();
			}
			case Sound::CRATE_OPEN:
			{
				// Prising the lid: wood groans, a nail squeaks out, the lid clatters down.
				Synth s(0.9f);
				s.Hiss(noise, 0.f, 0.35f, 200.f, 1200.f, 0.4f, 0.05f);
				for (size_t i = 0; i < static_cast<size_t>(0.3f * RATE); i++)
				{
					float t = s.Time(i);
					s.samples[i] += 0.12f * std::sin(TWO_PI * (900.f + 600.f * t) * t) * std::sin(3.14159f * t / 0.3f);
				}
				s.Thump(0.45f, 90.f, 0.8f, 0.06f);
				s.Burst(noise, 0.45f, 0.1f, 2000.f, 0.6f, 0.0008f);
				s.Burst(noise, 0.6f, 0.06f, 2000.f, 0.3f, 0.0008f);
				return s.ToClip();
			}
			case Sound::CASH:
			{
				// The till: a drawer's thunk and two bell notes.
				Synth s(0.7f);
				s.Thump(0.f, 180.f, 0.4f, 0.03f);
				s.Burst(noise, 0.f, 0.03f, 5000.f, 0.3f, 0.0005f);
				s.Ring(0.08f, 1318.f, 0.3f, 0.18f);
				s.Ring(0.08f, 2636.f, 0.08f, 0.1f);
				s.Ring(0.2f, 1975.f, 0.3f, 0.22f);
				s.Ring(0.2f, 3950.f, 0.07f, 0.12f);
				return s.ToClip();
			}
			case Sound::MACHINE:
			{
				// A cut: the motor spinning up, the tool grinding, spinning down.
				Synth s(2.f);
				float phase = 0.f;
				for (size_t i = 0; i < s.Size(); i++)
				{
					float t = s.Time(i);
					float speed = (std::min)(1.f, t / 0.4f) * (std::min)(1.f, (2.f - t) / 0.5f);
					phase += TWO_PI * (60.f + 360.f * speed) / RATE;
					s.samples[i] += 0.18f * speed * (std::sin(phase) + 0.4f * std::sin(phase * 2.f) + 0.2f * std::sin(phase * 3.f));
				}
				s.Hiss(noise, 0.4f, 1.1f, 1500.f, 6000.f, 0.25f, 0.05f);
				return s.ToClip();
			}
			case Sound::CLICK:
			{
				Synth s(0.05f);
				s.Ring(0.f, 1600.f, 0.25f, 0.008f);
				return s.ToClip();
			}
			case Sound::BIRD:
			{
				// A few quick chirps, each a little sweep up.
				Synth s(0.6f);
				int notes = 2 + static_cast<int>(std::abs(noise.Next()) * 3.f);
				float base = 2800.f + 1500.f * std::abs(noise.Next());
				for (int note = 0; note < notes; note++)
				{
					float start = note * 0.11f, length = 0.06f;
					float phase = 0.f;
					for (size_t i = static_cast<size_t>(start * RATE); i < static_cast<size_t>((start + length) * RATE) && i < s.Size(); i++)
					{
						float t = s.Time(i) - start;
						phase += TWO_PI * base * (1.f + 0.5f * t / length) / RATE;
						s.samples[i] += 0.25f * std::sin(3.14159f * t / length) * std::sin(phase);
					}
				}
				return s.ToClip();
			}
			default:
				return {};
		}
	}

	// A four-cylinder at 2400 rpm: 80 firings a second, each a decaying pulse, over the exhaust's roar.
	// Pitched up and down from there to follow the dyno. One second long, so every part repeats evenly.
	std::vector<int16_t> EngineLoop()
	{
		Noise noise{ 99u };
		Synth s(1.f);
		constexpr int FIRINGS = 80;
		for (int firing = 0; firing < FIRINGS; firing++)
		{
			float start = static_cast<float>(firing) / FIRINGS;
			float strength = 0.75f + 0.25f * std::abs(noise.Next());
			s.Thump(start, 75.f, 0.5f * strength, 0.008f);
			s.Burst(noise, start, 0.01f, 1500.f, 0.35f * strength, 0.0005f);
		}
		for (size_t i = 0; i < s.Size(); i++)
		{
			float t = s.Time(i);
			s.samples[i] += 0.15f * std::sin(TWO_PI * 40.f * t) + 0.1f * std::sin(TWO_PI * 160.f * t) + 0.05f * std::sin(TWO_PI * 240.f * t);
		}
		return s.ToClip(0.8f);
	}

	// Steady sounds made to loop: a rolling door, a lift motor, the wind outside, the shop's hum.
	std::vector<int16_t> DoorLoop()
	{
		Noise noise{ 5u };
		Synth s(1.2f);
		s.Hiss(noise, 0.f, 1.2f, 60.f, 400.f, 0.8f, 0.001f);
		for (int clack = 0; clack < 5; clack++)
			s.Burst(noise, 0.04f + clack * 0.22f, 0.03f, 2500.f, 0.35f, 0.0008f);
		s.MakeLoopable(0.2f);
		return s.ToClip();
	}
	std::vector<int16_t> LiftLoop()
	{
		Noise noise{ 6u };
		Synth s(1.2f);
		for (size_t i = 0; i < s.Size(); i++)
		{
			float t = s.Time(i);
			s.samples[i] += 0.25f * std::sin(TWO_PI * 120.f * t) + 0.12f * std::sin(TWO_PI * 240.f * t) + 0.06f * std::sin(TWO_PI * 360.f * t);
		}
		s.Hiss(noise, 0.f, 1.2f, 500.f, 3000.f, 0.08f, 0.001f);
		s.MakeLoopable(0.2f);
		return s.ToClip();
	}
	std::vector<int16_t> WindLoop()
	{
		Noise noise{ 7u };
		Synth s(6.f);
		float alpha = 1.f - std::exp(-TWO_PI * 350.f / RATE), smoothed = 0.f;
		for (size_t i = 0; i < s.Size(); i++)
		{
			float t = s.Time(i);
			smoothed += alpha * (noise.Next() - smoothed);
			float gust = 0.6f + 0.25f * std::sin(TWO_PI * 0.17f * t) + 0.15f * std::sin(TWO_PI * 0.43f * t + 1.f);
			s.samples[i] = 0.9f * gust * smoothed;
		}
		s.MakeLoopable(1.f);
		return s.ToClip();
	}
	std::vector<int16_t> HumLoop()
	{
		Noise noise{ 8u };
		Synth s(1.f);
		for (size_t i = 0; i < s.Size(); i++)
		{
			float t = s.Time(i);
			s.samples[i] += 0.12f * std::sin(TWO_PI * 120.f * t) + 0.05f * std::sin(TWO_PI * 240.f * t);
		}
		s.Hiss(noise, 0.f, 1.f, 2000.f, 8000.f, 0.015f, 0.001f);
		return s.ToClip();
	}
}

void GameAudio::Init()
{
	m_audio = AudioManager::Get().get();
	if (!m_audio->IsAvailable())
	{
		std::cout << "[WARNING] No audio device: the game will be silent\n";
		return;
	}
	// A few takes of each, so a run of footsteps or ratchet clicks never sounds like a loop.
	for (int sound = 0; sound < static_cast<int>(Sound::COUNT); sound++)
	{
		auto kind = static_cast<Sound>(sound);
		bool step = kind == Sound::FOOTSTEP_CONCRETE || kind == Sound::FOOTSTEP_GRASS || kind == Sound::FOOTSTEP_WOOD;
		int takes = step ? 6 : kind == Sound::BIRD ? 5 : 2;
		for (int take = 0; take < takes; take++)
			m_clips[sound].push_back(m_audio->AddClip(step ? Footstep(kind, take + sound * 31) : Make(kind, take + sound * 17)));
	}
	m_engineClip = m_audio->AddClip(EngineLoop());
	m_doorClip = m_audio->AddClip(DoorLoop());
	m_liftClip = m_audio->AddClip(LiftLoop());
	m_windClip = m_audio->AddClip(WindLoop());
	m_humClip = m_audio->AddClip(HumLoop());
	std::cout << "[INFO] Audio ready: " << m_humClip << " sounds made\n";
}

void GameAudio::Place(const glm::vec3& position, float& volume, float& pan) const
{
	// Fades with distance (barely across a room, mostly gone across the lot) and pans to the side it's on.
	glm::vec3 offset = position - m_listener;
	float distance = glm::length(offset);
	volume *= 1.f / (1.f + 0.04f * distance * distance);
	pan = distance > 0.3f ? std::clamp(glm::dot(offset / distance, m_listenerRight), -1.f, 1.f) * 0.8f : 0.f;
}

void GameAudio::Play(const SoundCue& cue)
{
	const auto& takes = m_clips[static_cast<size_t>(cue.sound)];
	if (takes.empty())
		return;
	float volume = cue.volume, pan = 0.f;
	if (cue.positional)
		Place(cue.position, volume, pan);
	// A slightly different take and pitch each time.
	m_variant = m_variant * 1664525u + 1013904223u;
	float pitch = 0.95f + 0.1f * static_cast<float>((m_variant >> 8) & 0xFF) / 255.f;
	bool onInterface = cue.sound == Sound::CLICK || cue.sound == Sound::CASH || (!cue.positional && cue.sound == Sound::MEASURE);
	m_audio->Play(takes[(m_variant >> 16) % takes.size()], onInterface ? AudioManager::Bus::INTERFACE
		: cue.sound == Sound::BIRD ? AudioManager::Bus::AMBIENCE : AudioManager::Bus::EFFECTS, volume, pitch, pan);
}

void GameAudio::PlayInterface(Sound sound, float volume)
{
	if (m_audio && m_audio->IsAvailable())
		Play({ sound, {}, volume, false });
}

void GameAudio::Footsteps(float deltaTime, const FirstPersonController& player, const ShopWorld& world)
{
	// One step every stride's length walked on the ground, quicker strides when running; a thump on
	// landing from a jump or a drop, harder the faster it was falling.
	glm::vec3 feet = player.GetFeetPosition();
	glm::vec3 moved = feet - m_lastFeet;
	m_lastFeet = feet;
	bool grounded = player.IsGrounded();
	if (!grounded)
		m_fallSpeed = (std::max)(m_fallSpeed, -player.GetVerticalVelocity());
	if (grounded && !m_wasGrounded && m_fallSpeed > 2.f)
		Play({ Sound::LAND, feet, std::clamp(m_fallSpeed / 5.f, 0.3f, 1.f), true });
	if (grounded)
		m_fallSpeed = 0.f;
	m_wasGrounded = grounded;

	float horizontal = glm::length(glm::vec2(moved.x, moved.z));
	if (!grounded || horizontal > 2.f || deltaTime <= 0.f)
		return;		// mid-air, or a teleport
	float speed = horizontal / deltaTime;
	m_stride += horizontal;
	float strideLength = speed > 3.5f ? 0.95f : 0.7f;
	if (m_stride >= strideLength)
	{
		m_stride = 0.f;
		// Kept well under everything else, each a little louder or softer than the last so they
		// don't tick like a metronome.
		m_variant = m_variant * 1664525u + 1013904223u;
		float vary = 0.85f + 0.3f * static_cast<float>((m_variant >> 16) & 0xFF) / 255.f;
		float loudness = player.IsCrouching() ? 0.08f : speed > 3.5f ? 0.28f : 0.18f;
		Play({ world.FootstepAt(feet), feet, loudness * vary, true });
	}
	if (speed < 0.2f)
		m_stride = strideLength * 0.6f;	// the first step comes soon after setting off
}

void GameAudio::Loops(float deltaTime, ShopWorld& world, float timeOfDay)
{
	using Bus = AudioManager::Bus;
	auto keep = [&](AudioManager::Loop& loop, AudioManager::Clip clip, Bus bus, bool on, const glm::vec3* position, float volume, float pitch) {
		float pan = 0.f;
		if (position)
			Place(*position, volume, pan);
		if (on && !loop)
			loop = m_audio->StartLoop(clip, bus, volume, pitch, pan);
		else if (on)
			m_audio->SetLoop(loop, volume, pitch, pan);
		else if (loop)
		{
			m_audio->StopLoop(loop);
			loop = 0;
		}
	};

	// The dyno engine, its note following the rpm; the door rolling; the lift's motor.
	float rpm = world.GetEngineRpm();
	glm::vec3 dyno = world.GetDynoPosition(), door = world.GetDoorPosition(), lift = world.GetLiftPosition();
	keep(m_engineLoop, m_engineClip, Bus::EFFECTS, rpm > 0.f, &dyno, 0.5f + 0.5f * (std::min)(rpm / 7000.f, 1.f), rpm / 2400.f);
	keep(m_doorLoop, m_doorClip, Bus::EFFECTS, world.IsDoorMoving(), &door, 0.6f, 1.f);
	keep(m_liftLoop, m_liftClip, Bus::EFFECTS, world.IsLiftMoving(), &lift, 0.5f, 1.f);

	// Ambience: wind outside (heard faintly through the walls, louder with the door up), the shop
	// lamps' hum in the garage, and birds through the day when you're out.
	ShopInterior interior = world.GetInterior();
	bool inGarage = interior.InGarage(m_listener);
	bool inside = inGarage || interior.InHouse(m_listener);
	float doorOpen = world.GetDoorOpenAmount();
	keep(m_windLoop, m_windClip, Bus::AMBIENCE, true, nullptr, inside ? 0.06f + 0.12f * doorOpen : 0.35f, 1.f);
	keep(m_humLoop, m_humClip, Bus::AMBIENCE, inGarage, nullptr, 0.12f, 1.f);
	bool day = timeOfDay > 5.5f && timeOfDay < 20.5f;
	if (!inside && day)
	{
		m_variant = m_variant * 1664525u + 1013904223u;
		m_birdTimer -= deltaTime;
		if (m_birdTimer <= 0.f)
		{
			// Somewhere off in the trees round the lot.
			float angle = static_cast<float>((m_variant >> 8) & 0xFFFF) / 65535.f * TWO_PI;
			glm::vec3 tree = m_listener + glm::vec3(std::cos(angle) * 14.f, 6.f, std::sin(angle) * 14.f);
			Play({ Sound::BIRD, tree, 3.f, true });
			m_birdTimer = 2.f + static_cast<float>((m_variant >> 20) & 0x3FF) / 1023.f * 6.f;
		}
	}
}

void GameAudio::Bookkeeping(const Workshop& workshop)
{
	// What happens in the workshop screens: money spent or earned, a reading taken, a cut made.
	const auto& stats = workshop.GetStats();
	double money = workshop.GetMoney();
	if (m_lastMoney >= 0.0 && std::abs(money - m_lastMoney) > 0.5)
		PlayInterface(Sound::CASH, 0.7f);
	if (m_lastMeasurements >= 0 && stats.measurements > m_lastMeasurements)
		PlayInterface(Sound::MEASURE, 0.6f);
	if (m_lastCuts >= 0 && stats.cuts > m_lastCuts)
		PlayInterface(Sound::MACHINE, 0.6f);
	m_lastMoney = money;
	m_lastMeasurements = stats.measurements;
	m_lastCuts = stats.cuts;
}

void GameAudio::Update(float deltaTime, const FirstPersonController& player, ShopWorld& world, const Workshop& workshop,
	const GameSettings& settings, float timeOfDay)
{
	if (!m_audio || !m_audio->IsAvailable())
		return;
	m_audio->SetMasterVolume(settings.masterVolume);
	m_audio->SetBusVolume(AudioManager::Bus::EFFECTS, settings.effectsVolume);
	m_audio->SetBusVolume(AudioManager::Bus::AMBIENCE, settings.ambienceVolume);
	m_audio->SetBusVolume(AudioManager::Bus::INTERFACE, settings.interfaceVolume);

	m_listener = player.GetEyePosition();
	glm::vec3 forward = player.GetForward();
	glm::vec3 right = glm::cross(forward, glm::vec3(0.f, 1.f, 0.f));
	if (glm::length(right) > 0.001f)
		m_listenerRight = glm::normalize(right);

	for (const auto& cue : world.TakeSoundCues())
		Play(cue);
	Footsteps(deltaTime, player, world);
	Loops(deltaTime, world, timeOfDay);
	Bookkeeping(workshop);
	m_audio->Update();
}
