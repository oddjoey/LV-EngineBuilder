// The desert quarry in the world: boulders out in the desert broken with the pickaxe, and the pad on the plot
// with the crusher and whatever else has been bought for it.
#include "ShopWorld.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "LV/Engine/Utility/InputManager.h"
#include "../Controls.h"
#include "../Simulation/Random.h"
#include "ShopWorldShared.h"

using namespace ShopLayout;

namespace
{
	constexpr float REGROW_SECONDS = 180.f;		// before another boulder's turned up where one was broken

	// The kinds of rock the desert's boulders are, commonest first: the rarer, the more they fetch and the harder
	// they are to break. Each is tinted (against the rock texture's own orange: grey granite, green copper ore, a purple geode,
	// golden quartz) and the precious ones catch the light, so they can be spotted from afar.
	struct BoulderKind
	{
		const char* name;
		float chance;		// of a boulder being this kind (the rest are sandstone)
		double worth;		// against sandstone
		float hardness;		// how much longer it takes to break
		glm::vec4 tint;
		float glint;
	};
	const BoulderKind BOULDER_KINDS[] = {
		{ "Sandstone boulder", 0.f, 1.0, 1.f, { 1.f, 1.f, 1.f, 1.f }, 0.f },
		{ "Granite boulder", 0.15f, 2.5, 1.3f, { 0.55f, 1.3f, 3.f, 1.f }, 0.f },
		{ "Copper ore boulder", 0.06f, 6.0, 1.5f, { 0.32f, 1.15f, 1.3f, 1.f }, 0.05f },
		{ "Geode", 0.02f, 15.0, 1.8f, { 0.62f, 0.72f, 5.f, 1.f }, 0.1f },
		{ "Gold-bearing quartz", 0.007f, 40.0, 2.f, { 1.2f, 1.5f, 0.6f, 1.f }, 0.2f },
	};
	constexpr int KINDS = static_cast<int>(std::size(BOULDER_KINDS));

	// A kind for a roll from 0 to 1: the rarest checked first.
	int KindFor(float roll)
	{
		float below = 0.f;
		for (int kind = KINDS - 1; kind > 0; kind--)
		{
			below += BOULDER_KINDS[kind].chance;
			if (roll < below)
				return kind;
		}
		return 0;
	}

	// The pickaxe's swing, over SWING_SECONDS: drawn back over the shoulder, brought down hard onto the rock,
	// then back to rest. Its tilt in radians against how far through it is (0 to 1).
	constexpr float SWING_SECONDS = 0.62f;
	constexpr float STRIKE_AT = 0.62f;			// where the head meets the rock
	float SwingTilt(float t)
	{
		auto ease = [](float x) { return x * x * (3.f - 2.f * x); };
		constexpr float BACK = 1.15f, DOWN = -1.f;
		if (t < 0.48f)
			return BACK * ease(t / 0.48f);
		if (t < STRIKE_AT)
		{
			float x = (t - 0.48f) / (STRIKE_AT - 0.48f);
			return BACK + (DOWN - BACK) * x * x;		// accelerating down
		}
		return DOWN * (1.f - ease((t - STRIKE_AT) / (1.f - STRIKE_AT)));
	}

	FloorObstacle Around(const glm::vec3& at, glm::vec2 half)
	{
		return { glm::vec2(at.x, at.z) - half, glm::vec2(at.x, at.z) + half };
	}
}

void ShopWorld::DressBoulder(size_t index, int kind)
{
	auto& piece = m_scenery[index];
	piece.kind = std::clamp(kind, 0, KINDS - 1);
	const auto& rock = BOULDER_KINDS[piece.kind];
	piece.object->SetTint(rock.tint);
	piece.object->SetEmissive(rock.glint);
}

void ShopWorld::BuildQuarry()
{
	using namespace GarageLayout;
	auto place = [&](const std::string& model, const std::string& texture, const glm::vec3& at, float yaw = 0.f) {
		auto object = Spawn(model, texture, at, yaw);
		return object;
	};
	m_quarry.pad = place(ShopModels::QUARRY_PAD, ShopTextures::ASPHALT, {});
	m_quarry.sign = place(ShopModels::QUARRY_SIGN, ShopTextures::WOOD, QUARRY_SIGN, 2.07f);	// facing the drive
	m_quarry.crusher = place(ShopModels::QUARRY_CRUSHER, ShopTextures::PAINT, QUARRY_CRUSHER);
	// The conveyor runs from its foot at the back of the pad up to the crusher's mouth.
	glm::vec3 conveyorStart = QUARRY_CONVEYOR_FOOT;
	m_quarry.conveyor = place(ShopModels::QUARRY_CONVEYOR, ShopTextures::PAINT, conveyorStart);
	m_quarry.hopper = place(ShopModels::QUARRY_HOPPER, ShopTextures::PAINT, QUARRY_HOPPER);
	m_quarry.gravel = place(ShopModels::QUARRY_GRAVEL, ShopTextures::ROCK, QUARRY_HOPPER);
	m_quarry.shed = place(ShopModels::QUARRY_SHED, ShopTextures::SIDING, QUARRY_SHED);
	// The sign and the machines are what you use the quarry by.
	for (const auto& object : { m_quarry.sign, m_quarry.crusher, m_quarry.hopper, m_quarry.gravel, m_quarry.shed })
	{
		object->SetPickable(true);
		m_targets[object.get()] = { TargetKind::QUARRY, {}, 0, 0 };
	}
	m_sceneryObstacles.push_back(Around(QUARRY_SIGN, { 0.8f, 0.4f }));
	// Each of the desert's boulders is some kind of rock, the same in every game till it's broken.
	for (size_t i = 0; i < m_scenery.size(); i++)
		if (m_scenery[i].boulder)
		{
			uint32_t h = static_cast<uint32_t>(i + 1) * 2654435761u;
			h = (h ^ (h >> 15)) * 2246822519u;
			DressBoulder(i, KindFor(static_cast<float>((h ^ (h >> 13)) & 0xFFFF) / 65535.f));
		}
	m_quarryShown.fill(-1);
}

void ShopWorld::SyncQuarry(const Workshop& workshop, float deltaTime)
{
	using namespace GarageLayout;
	const auto& quarry = workshop.GetQuarry();
	auto level = [&](QuarryUpgrade upgrade) { return quarry.levels[static_cast<int>(upgrade)]; };
	if (!m_quarry.pad)
		return;

	// What's been bought is there, bigger as it's upgraded.
	if (quarry.levels != m_quarryShown)
	{
		m_quarryShown = quarry.levels;
		int crusher = level(QuarryUpgrade::CRUSHER), conveyor = level(QuarryUpgrade::CONVEYOR), hopper = level(QuarryUpgrade::HOPPER);
		bool hand = level(QuarryUpgrade::HAND) > 0;
		m_quarry.crusher->SetVisible(crusher > 0)->SetPickable(crusher > 0);
		m_quarry.crusher->SetScale(glm::vec3(0.85f + 0.12f * crusher));
		m_quarry.conveyor->SetVisible(conveyor > 0);
		m_quarry.conveyor->SetScale({ 1.f, 1.f, 0.8f + 0.2f * conveyor });
		m_quarry.hopper->SetVisible(crusher > 0 && hopper > 0)->SetPickable(crusher > 0 && hopper > 0);
		m_quarry.hopper->SetScale(glm::vec3(0.8f + 0.1f * hopper));
		m_quarry.shed->SetVisible(hand)->SetPickable(hand);
		m_quarryObstacles.clear();
		if (crusher > 0)
			m_quarryObstacles.push_back(Around(QUARRY_CRUSHER, glm::vec2(1.1f, 0.75f) * (0.85f + 0.12f * crusher)));
		if (crusher > 0 && hopper > 0)
			m_quarryObstacles.push_back(Around(QUARRY_HOPPER, glm::vec2(0.8f) * (0.8f + 0.1f * hopper)));
		if (hand)
			m_quarryObstacles.push_back(Around(QUARRY_SHED, { 1.6f, 0.8f }));
		RebuildCollision();
	}
	// The crushed stone piles up as the hopper fills: in the bin, or on the ground under the chute without one.
	float fill = quarry.levels[static_cast<int>(QuarryUpgrade::CRUSHER)] > 0 ? static_cast<float>(quarry.hopper / workshop.QuarryCapacity()) : 0.f;
	m_quarry.gravel->SetVisible(fill > 0.01f);
	bool binned = level(QuarryUpgrade::HOPPER) > 0;
	glm::vec3 heap = binned ? QUARRY_HOPPER + glm::vec3(0.f, 2.1f - 0.75f * (1.f - fill), 0.f) : QUARRY_HOPPER;
	m_quarry.gravel->SetPosition(heap);
	m_quarry.gravel->SetScale(binned ? glm::vec3(0.8f, (std::max)(0.05f, fill) * 0.9f, 0.8f) : glm::vec3(0.6f + 0.6f * fill, (std::max)(0.05f, fill), 0.6f + 0.6f * fill));

	// Broken boulders grow back (another rolls down off the hills, say) after a while.
	bool regrown = false;
	for (size_t i = 0; i < m_scenery.size(); i++)
	{
		auto& piece = m_scenery[i];
		if (!piece.boulder || piece.regrow <= 0.f)
			continue;
		piece.regrow -= deltaTime;
		if (piece.regrow > 0.f)
			continue;
		piece.regrow = 0.f;
		// The one that turns up may be any kind of rock.
		DressBoulder(i, KindFor(static_cast<float>(Random::Uniform(0.0, 1.0))));
		piece.object->SetVisible(true);
		if (piece.footprint)
			for (auto* list : { &m_sceneryObstacles, &m_staticObstacles })
				for (auto& obstacle : *list)
					if (obstacle.min == piece.footprint->min && obstacle.max == piece.footprint->max)
						obstacle.top = piece.footprint->top;
		regrown = true;
	}
	if (regrown)
		RebuildCollision();
}

bool ShopWorld::UpdatePickaxe(Workshop& workshop, const FirstPersonController& player, const InputManager& input, size_t toolIndex, float deltaTime)
{
	if (ToolCatalog::Of(toolIndex).handKind != HandToolKind::PICKAXE)
	{
		m_boulderAimed.reset();
		m_swingTime = 0.f;
		m_swingAngle = 0.f;
		return false;
	}
	// The swing: started with a click, kept going while it's held, and always seen through to the end. The head
	// meets whatever's in front partway through each one.
	bool swinging = Controls::Held(input, Control::PRIMARY);
	bool struck = false;
	if (swinging || m_swingTime > 0.f)
	{
		float before = m_swingTime;
		m_swingTime += deltaTime;
		struck = before < STRIKE_AT * SWING_SECONDS && m_swingTime >= STRIKE_AT * SWING_SECONDS;
		if (m_swingTime >= SWING_SECONDS)
			m_swingTime = swinging ? m_swingTime - SWING_SECONDS : 0.f;
	}
	m_swingAngle = m_swingTime > 0.f ? SwingTilt(m_swingTime / SWING_SECONDS) : 0.f;
	if (m_devSwing >= 0.f)
		m_swingAngle = SwingTilt(m_devSwing);

	// The boulder in front, close enough to swing at.
	glm::vec3 eye = player.GetEyePosition(), forward = player.GetForward();
	std::optional<size_t> aimed;
	float nearest = 0.f;
	for (size_t i = 0; i < m_scenery.size(); i++)
	{
		const auto& piece = m_scenery[i];
		if (!piece.boulder || !piece.object->IsVisible())
			continue;
		auto bounds = WorldBounds(piece.object);
		if (auto hit = RayBox(eye, forward, bounds.min, bounds.max); hit && *hit <= INTERACT_DISTANCE && (!aimed || *hit < nearest))
		{
			aimed = i;
			nearest = *hit;
		}
	}
	if (aimed != m_boulderAimed)
		m_boulderHold = 0.f;
	m_boulderAimed = aimed;
	if (!aimed)
	{
		m_pressAimed = false;
		return false;
	}

	auto& piece = m_scenery[*aimed];
	const auto& rock = BOULDER_KINDS[piece.kind];
	float size = piece.object->GetScale().x;
	float seconds = workshop.BoulderSeconds() * (0.6f + 0.5f * size) * rock.hardness;
	m_boulderHold = swinging ? m_boulderHold + deltaTime : 0.f;
	m_pressAimed = true;
	m_pressProgress = (std::min)(1.f, m_boulderHold / seconds);
	glm::vec3 strike = eye + forward * nearest;
	// Each blow lands with a thud, harder rock ringing a little louder.
	bool breaking = m_boulderHold >= seconds;
	if (struck && !breaking)
		Cue(Sound::DROP, strike, (std::min)(1.f, 0.5f + 0.1f * rock.hardness));
	m_prompt = std::format("{}: about ${:.0f}   [LMB] Hold to break it up", rock.name, workshop.BoulderPay(size, rock.worth));

	if (breaking)
	{
		// Broken up, and the stone's sold (the money coming in rings the till by itself): it's gone till another turns up.
		double pay = workshop.BreakBoulder(size, rock.worth);
		Cue(Sound::DROP, strike, 1.f);
		Toast(piece.kind >= 2 ? std::format("+${:.0f}: a {}! ({} broken so far)", pay, rock.name, workshop.GetQuarry().bouldersBroken)
			: std::format("+${:.0f}: the {}'s stone, sold ({} broken so far)", pay, rock.name, workshop.GetQuarry().bouldersBroken));
		piece.object->SetVisible(false);
		piece.regrow = REGROW_SECONDS;
		if (piece.footprint)
			for (auto* list : { &m_sceneryObstacles, &m_staticObstacles })
				for (auto& obstacle : *list)
					if (obstacle.min == piece.footprint->min && obstacle.max == piece.footprint->max)
						obstacle.top = -1e5f;
		RebuildCollision();
		m_boulderAimed.reset();
		m_boulderHold = 0.f;
		m_pressAimed = false;
	}
	return true;
}

std::string ShopWorld::QuarryPrompt(const Workshop& workshop) const
{
	const auto& quarry = workshop.GetQuarry();
	int crusher = quarry.levels[static_cast<int>(QuarryUpgrade::CRUSHER)];
	if (crusher == 0)
		return std::format("Desert quarry: rock to be had, but no crusher yet (${:.0f} from boulders so far)   [E] The quarry", quarry.earned);
	bool hand = quarry.levels[static_cast<int>(QuarryUpgrade::HAND)] > 0;
	std::string hopper = hand ? "the hand hauls it away" : std::format("${:.0f} of ${:.0f} in the hopper{}", quarry.hopper, workshop.QuarryCapacity(),
		quarry.hopper >= workshop.QuarryCapacity() - 0.01 ? " (full: it's stopped)" : "");
	return std::format("{}: ${:.1f} a minute, {}   [E] The quarry", Workshop::QuarryUpgradeName(QuarryUpgrade::CRUSHER, crusher),
		workshop.QuarryRate(), hopper);
}

std::optional<glm::vec3> ShopWorld::DevNearestBoulder(const glm::vec3& from, int kind) const
{
	std::optional<glm::vec3> nearest;
	for (const auto& piece : m_scenery)
		if (piece.boulder && piece.object->IsVisible() && piece.kind >= kind)
		{
			glm::vec3 at = piece.object->GetPosition();
			if (!nearest || glm::distance(at, from) < glm::distance(*nearest, from))
				nearest = at;
		}
	return nearest;
}
