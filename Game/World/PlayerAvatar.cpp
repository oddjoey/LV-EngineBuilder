#include "PlayerAvatar.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "LV/Engine/Objects/ObjectManager.h"
#include "ShopAssets.h"
#include "ShopWorld.h"
#include "ShopWorldShared.h"

namespace
{
	// Where the joints are on the torso model (feet at the origin, facing -Z), left side first.
	constexpr glm::vec3 PELVIS = { 0.f, 0.52f, 0.f };
	constexpr glm::vec3 NECK = { 0.f, 0.97f, 0.f };
	constexpr glm::vec3 SHOULDER[2] = { { -0.22f, 0.9f, 0.f }, { 0.22f, 0.9f, 0.f } };
	constexpr float HIP_X[2] = { -0.1f, 0.1f };
	constexpr float THIGH_LENGTH = 0.26f;		// hip to knee, and the shin knee to the floor
	constexpr float EYE_ABOVE_NECK = 0.28f;
	constexpr float HAND_REACH = 0.46f;			// shoulder to the middle of the hand
	constexpr float STEP_LENGTH = 0.6f;			// metres a foot moves on, each step
	constexpr float WALK_SPEED = 2.6f;			// FirstPersonController's
	// Crouched, the hips come down this far and the back leans forward this far (radians). The camera sits
	// at these eyes (FirstPersonController), standing
	// or crouched.
	constexpr float CROUCH_DROP = 0.3f;
	constexpr float CROUCH_LEAN = 0.35f;
	const glm::vec3 X_AXIS = { 1.f, 0.f, 0.f };

	float Ease(float rate, float deltaTime) { return 1.f - std::exp(-rate * deltaTime); }
}

glm::quat PlayerAvatar::ViewRotation(float yaw, float pitch)
{
	return glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f)) * glm::angleAxis(pitch, X_AXIS);
}

void PlayerAvatar::Spawn(ShopWorld& world, const glm::vec3& feet, float yaw, const MonkeyLook& look)
{
	auto spawn = [&](const std::string& model)
	{
		pObject object = world.Spawn(model, "", feet, yaw);
		object->SetPickable(false);
		return object;
	};
	m_torso = spawn(ShopModels::PLAYER);
	m_belly = spawn(ShopModels::PLAYER_BELLY);
	m_tail = spawn(ShopModels::PLAYER_TAIL);
	m_head = spawn(ShopModels::PLAYER_HEAD);
	m_face = spawn(ShopModels::PLAYER_FACE);
	m_eyes = spawn(ShopModels::PLAYER_EYES);
	m_cap = spawn(ShopModels::PLAYER_CAP);
	for (int side = 0; side < 2; side++)
	{
		m_arm[side] = spawn(ShopModels::PLAYER_ARM);
		m_paw[side] = spawn(ShopModels::PLAYER_HAND);
		m_thigh[side] = spawn(ShopModels::PLAYER_THIGH);
		m_shin[side] = spawn(ShopModels::PLAYER_SHIN);
		m_foot[side] = spawn(ShopModels::PLAYER_FOOT);
	}
	m_posed = false;
	m_visible = true;
	SetLook(look);
}

std::vector<pObject*> PlayerAvatar::Pieces()
{
	return { &m_torso, &m_belly, &m_tail, &m_head, &m_face, &m_eyes, &m_cap, &m_arm[0], &m_arm[1], &m_paw[0], &m_paw[1],
		&m_thigh[0], &m_thigh[1], &m_shin[0], &m_shin[1], &m_foot[0], &m_foot[1] };
}

void PlayerAvatar::Remove()
{
	for (auto* object : Pieces())
	{
		if (*object)
			ObjectManager::Get()->RemoveObject(*object);
		*object = nullptr;
	}
}

void PlayerAvatar::SetVisible(bool visible)
{
	// Shown again after a while hidden, it picks up from where they are now and stands still to start with,
	// rather than walking off everything that happened meanwhile in a rush.
	if (visible && !m_visible)
	{
		m_posed = false;
		m_speed = 0.f;
	}
	m_visible = visible;
	for (auto* object : Pieces())
		if (*object)
			(*object)->SetVisible(visible && (*object != m_tail || m_look.tail));
}

void PlayerAvatar::SetLook(const MonkeyLook& look)
{
	m_look = look;
	if (!m_torso)
		return;
	glm::vec4 fur = MonkeyLook::Color(look.fur), skin = MonkeyLook::Color(look.skin);
	for (const auto& object : { m_torso, m_tail, m_head, m_arm[0], m_arm[1], m_thigh[0], m_thigh[1], m_shin[0], m_shin[1] })
		object->SetTint(fur);
	for (const auto& object : { m_belly, m_face, m_paw[0], m_paw[1], m_foot[0], m_foot[1] })
		object->SetTint(skin);
	m_cap->SetTint(MonkeyLook::Color(look.cap));
	SetVisible(m_visible);
}

void PlayerAvatar::Pose(float deltaTime, const glm::vec3& feet, float yaw, float pitch, bool crouching, int arms)
{
	if (!m_torso)
		return;
	if (!m_posed)
	{
		m_lastFeet = feet;
		m_crouch = crouching ? 1.f : 0.f;
		m_posed = true;
	}

	// Walking: how fast they're going over the floor sets how far the legs (and the arms, the other
	// way) swing, and moves them on through the stride.
	if (deltaTime > 0.f)
	{
		float speed = glm::length(glm::vec2(feet.x - m_lastFeet.x, feet.z - m_lastFeet.z)) / deltaTime;
		m_speed += (speed - m_speed) * Ease(8.f, deltaTime);
	}
	m_lastFeet = feet;
	m_stride = std::fmod(m_stride + m_speed * deltaTime * glm::pi<float>() / STEP_LENGTH, glm::two_pi<float>());
	float walk = std::clamp(m_speed / WALK_SPEED, 0.f, 1.4f);
	float swing = std::sin(m_stride) * walk * 0.45f;
	// Up a little as the legs pass each other, down as they're furthest apart.
	float bob = std::abs(std::cos(m_stride)) * (std::min)(walk, 1.f) * 0.03f;
	m_crouch += ((crouching ? 1.f : 0.f) - m_crouch) * Ease(12.f, deltaTime);

	// The torso hangs from the hips: down and leaning forward into a crouch.
	glm::quat facing = ShopLayout::Yaw(yaw);
	glm::quat torso = facing * glm::angleAxis(-CROUCH_LEAN * m_crouch, X_AXIS);
	glm::vec3 pelvis = feet + glm::vec3(0.f, PELVIS.y - CROUCH_DROP * m_crouch + bob, 0.f);
	auto on = [&](const glm::vec3& joint) { return pelvis + torso * (joint - PELVIS); };
	m_torso->SetPosition(on({ 0.f, 0.f, 0.f }));
	m_torso->SetRotation(torso);

	// The head looks up and down with them, as far as a neck goes.
	glm::vec3 neck = on(NECK);
	m_head->SetPosition(neck);
	m_head->SetRotation(facing * glm::angleAxis(std::clamp(pitch, -0.8f, 0.8f), X_AXIS));
	m_eye = neck + glm::vec3(0.f, EYE_ABOVE_NECK, 0.f);

	// The legs: the knees bend so the feet stay on the floor under the lowered hips, the thigh forward
	// and the shin back, and swing together as they walk.
	float hipHeight = pelvis.y - feet.y;
	float bend = std::acos(std::clamp(hipHeight / (2.f * THIGH_LENGTH), 0.f, 1.f));
	for (int side = 0; side < 2; side++)
	{
		float legSwing = side == 0 ? swing : -swing;
		glm::vec3 hip = pelvis + facing * glm::vec3(HIP_X[side], 0.f, 0.f);
		glm::quat thigh = facing * glm::angleAxis(legSwing + bend, X_AXIS);
		m_thigh[side]->SetPosition(hip);
		m_thigh[side]->SetRotation(thigh);
		m_shin[side]->SetPosition(hip + thigh * glm::vec3(0.f, -THIGH_LENGTH, 0.f));
		m_shin[side]->SetRotation(facing * glm::angleAxis(legSwing - bend, X_AXIS));
	}

	// The arms swing as they walk; held out, they reach for a point in front of the eyes, raised and
	// lowered as they look about. The right one holds what's in hand; both carry a delivery.
	glm::quat view = ViewRotation(yaw, pitch);
	for (int side = 0; side < 2; side++)
	{
		bool out = side == 1 ? arms >= 1 : arms == 2;
		m_armOut[side] += ((out ? 1.f : 0.f) - m_armOut[side]) * Ease(10.f, deltaTime);

		glm::vec3 shoulder = on(SHOULDER[side]);
		glm::vec3 target = m_eye + view * glm::vec3(side == 1 ? 0.22f : -0.22f, -0.3f, -0.5f);
		glm::vec3 reach = glm::inverse(facing) * (target - shoulder);
		float raise = std::atan2(-reach.z, -reach.y);
		float armSwing = (side == 0 ? -swing : swing) * 0.8f;
		glm::quat pose = facing * glm::angleAxis(glm::mix(armSwing, raise, m_armOut[side]), X_AXIS);
		m_arm[side]->SetPosition(shoulder);
		m_arm[side]->SetRotation(pose);
		if (side == 1)
			m_hand = shoulder + pose * glm::vec3(0.f, -HAND_REACH, 0.f);
	}

	// What rides on the jointed pieces goes with them.
	auto ride = [](const pObject& rider, const pObject& on) {
		rider->SetPosition(on->GetPosition());
		rider->SetRotation(on->GetRotation());
	};
	ride(m_belly, m_torso);
	ride(m_tail, m_torso);
	ride(m_face, m_head);
	ride(m_eyes, m_head);
	ride(m_cap, m_head);
	for (int side = 0; side < 2; side++)
	{
		ride(m_paw[side], m_arm[side]);
		ride(m_foot[side], m_shin[side]);
	}
}

std::vector<PlayerAvatar::StandingPiece> PlayerAvatar::Standing(const MonkeyLook& look)
{
	glm::vec3 fur = MonkeyLook::Color(look.fur), skin = MonkeyLook::Color(look.skin), cap = MonkeyLook::Color(look.cap);
	auto at = [](const glm::vec3& joint) { return glm::translate(glm::mat4(1.f), joint); };
	std::vector<StandingPiece> pieces = {
		{ ShopModels::PLAYER, glm::mat4(1.f), fur },
		{ ShopModels::PLAYER_BELLY, glm::mat4(1.f), skin },
		{ ShopModels::PLAYER_HEAD, at(NECK), fur },
		{ ShopModels::PLAYER_FACE, at(NECK), skin },
		{ ShopModels::PLAYER_EYES, at(NECK), glm::vec3(1.f) },
		{ ShopModels::PLAYER_CAP, at(NECK), cap },
	};
	if (look.tail)
		pieces.push_back({ ShopModels::PLAYER_TAIL, glm::mat4(1.f), fur });
	for (int side = 0; side < 2; side++)
	{
		glm::vec3 hip = { HIP_X[side], PELVIS.y, 0.f };
		glm::vec3 knee = hip - glm::vec3(0.f, THIGH_LENGTH, 0.f);
		pieces.push_back({ ShopModels::PLAYER_ARM, at(SHOULDER[side]), fur });
		pieces.push_back({ ShopModels::PLAYER_HAND, at(SHOULDER[side]), skin });
		pieces.push_back({ ShopModels::PLAYER_THIGH, at(hip), fur });
		pieces.push_back({ ShopModels::PLAYER_SHIN, at(knee), fur });
		pieces.push_back({ ShopModels::PLAYER_FOOT, at(knee), skin });
	}
	return pieces;
}
