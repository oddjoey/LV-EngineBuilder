#include "FirstPersonController.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "LV/Engine/Objects/Camera.h"
#include "LV/Engine/Utility/InputManager.h"

void FirstPersonController::SetPosition(const glm::vec3& feetPosition)
{
	m_position = feetPosition;
}

void FirstPersonController::SetYaw(float yaw)
{
	m_yaw = yaw;
}

void FirstPersonController::SetMouseSensitivity(float multiplier)
{
	m_mouseSensitivity = BASE_SENSITIVITY * multiplier;
}

void FirstPersonController::SetInvertY(bool invert)
{
	m_invertY = invert;
}

void FirstPersonController::Update(float deltaTime, const InputManager& input, const std::vector<FloorObstacle>& obstacles, const FloorObstacle& roomBounds,
	const std::vector<WalkSurface>& surfaces)
{
	// A long hitch (window drag, breakpoint) shouldn't fling the player through a wall.
	deltaTime = (std::min)(deltaTime, 0.1f);

	glm::vec2 mouse = input.GetMouseDelta();
	m_yaw -= mouse.x * m_mouseSensitivity;
	m_pitch -= mouse.y * m_mouseSensitivity * (m_invertY ? -1.f : 1.f);
	m_pitch = std::clamp(m_pitch, -glm::radians(85.f), glm::radians(85.f));

	glm::vec3 forward = { -std::sin(m_yaw), 0.f, -std::cos(m_yaw) };
	glm::vec3 right = { std::cos(m_yaw), 0.f, -std::sin(m_yaw) };
	glm::vec3 move = {};
	if (input.IsKeyPressed(GLFW_KEY_W)) move += forward;
	if (input.IsKeyPressed(GLFW_KEY_S)) move -= forward;
	if (input.IsKeyPressed(GLFW_KEY_D)) move += right;
	if (input.IsKeyPressed(GLFW_KEY_A)) move -= right;

	// Crouching (held Ctrl): lower and slower, no running or jumping. Letting go stands back up, unless
	// something low overhead is in the way, and the eye eases between the two heights.
	if (input.IsKeyPressed(GLFW_KEY_LEFT_CONTROL))
		m_crouching = true;
	else if (m_crouching && HasHeadroom(obstacles))
		m_crouching = false;
	float targetEye = m_crouching ? CROUCH_EYE_HEIGHT : STAND_EYE_HEIGHT;
	float easing = 1.f - std::exp(-CROUCH_EASE * deltaTime);
	m_eyeHeight += (targetEye - m_eyeHeight) * easing;

	if (glm::length(move) > 0.f)
	{
		float speed = m_crouching ? m_crouchSpeed : input.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ? m_runSpeed : m_walkSpeed;
		m_position += glm::normalize(move) * speed * deltaTime;
	}

	// What's underfoot: the ground, or the highest stair, landing or upstairs floor under the player that
	// isn't more than a step above their feet (so they walk up stairs, but not through a ceiling).
	float ground = 0.f;
	for (const auto& surface : surfaces)
		if (m_position.x >= surface.min.x && m_position.x <= surface.max.x && m_position.z >= surface.min.y && m_position.z <= surface.max.y
			&& surface.height <= m_position.y + STEP_UP && surface.height > ground)
			ground = surface.height;
	// The top of anything solid is somewhere to stand too (a bench, a crate, a table jumped onto), as
	// long as enough of the player is over it; walls and trees have no top to reach.
	glm::vec2 feet = { m_position.x, m_position.z };
	for (const auto& obstacle : obstacles)
		if (obstacle.top <= m_position.y + STEP_UP && obstacle.top > ground
			&& glm::length(feet - glm::clamp(feet, obstacle.min, obstacle.max)) < m_radius * SUPPORT_FRACTION)
			ground = obstacle.top;

	// Jumping: a fixed take-off speed and gravity. Walking, the feet stay on the ground: up a step, and
	// down one rather than falling off it.
	if (m_grounded && !m_crouching && input.WasKeyPressed(GLFW_KEY_SPACE))
	{
		m_verticalVelocity = m_jumpSpeed;
		m_grounded = false;
	}
	if (m_grounded && m_position.y - ground <= STEP_UP)
	{
		m_position.y = ground;
		m_verticalVelocity = 0.f;
	}
	else
	{
		m_verticalVelocity -= GRAVITY * deltaTime;
		m_position.y += m_verticalVelocity * deltaTime;
		m_grounded = m_position.y <= ground;
		if (m_grounded)
		{
			m_position.y = ground;
			m_verticalVelocity = 0.f;
		}
	}

	ResolveCollisions(obstacles, roomBounds);
}

void FirstPersonController::ResolveCollisions(const std::vector<FloorObstacle>& obstacles, const FloorObstacle& roomBounds)
{
	glm::vec2 position = { m_position.x, m_position.z };

	// Two passes settle the player into corners where two boxes push in different directions.
	for (int pass = 0; pass < 2; pass++)
		for (const auto& obstacle : obstacles)
		{
			// Only what's level with the player: not a machine downstairs, nor a step they're walking up,
			// nor something they can duck under.
			if (m_position.y + STEP_UP >= obstacle.top || m_position.y + HeadHeight() <= obstacle.bottom)
				continue;
			glm::vec2 closest = glm::clamp(position, obstacle.min, obstacle.max);
			glm::vec2 offset = position - closest;
			float distance = glm::length(offset);

			if (distance >= m_radius)
				continue;

			if (distance > 1e-5f)
				position = closest + offset / distance * m_radius;
			else
			{
				// Centre is inside the box: leave by the nearest side.
				float left = position.x - obstacle.min.x, right = obstacle.max.x - position.x;
				float back = position.y - obstacle.min.y, front = obstacle.max.y - position.y;
				float nearest = (std::min)({ left, right, back, front });
				if (nearest == left) position.x = obstacle.min.x - m_radius;
				else if (nearest == right) position.x = obstacle.max.x + m_radius;
				else if (nearest == back) position.y = obstacle.min.y - m_radius;
				else position.y = obstacle.max.y + m_radius;
			}
		}

	position = glm::clamp(position, roomBounds.min + m_radius, roomBounds.max - m_radius);
	m_position.x = position.x;
	m_position.z = position.y;
}

float FirstPersonController::HeadHeight() const
{
	return m_crouching ? CROUCH_HEAD_HEIGHT : HEAD_HEIGHT;
}

bool FirstPersonController::HasHeadroom(const std::vector<FloorObstacle>& obstacles) const
{
	// Anything over the crouched head and below the standing one, where the player stands.
	glm::vec2 position = { m_position.x, m_position.z };
	for (const auto& obstacle : obstacles)
	{
		if (obstacle.bottom < m_position.y + CROUCH_HEAD_HEIGHT || obstacle.bottom >= m_position.y + HEAD_HEIGHT)
			continue;
		if (glm::length(position - glm::clamp(position, obstacle.min, obstacle.max)) < m_radius)
			return false;
	}
	return true;
}

void FirstPersonController::ApplyToCamera(Camera& camera) const
{
	camera.SetPosition(GetEyePosition());
	camera.SetRotation(GetRotation());
}

glm::vec3 FirstPersonController::GetEyePosition() const
{
	return m_position + glm::vec3(0.f, m_eyeHeight, 0.f);
}

glm::vec3 FirstPersonController::GetFeetPosition() const
{
	return m_position;
}

float FirstPersonController::GetRadius() const
{
	return m_radius;
}

glm::quat FirstPersonController::GetRotation() const
{
	return glm::angleAxis(m_yaw, glm::vec3(0.f, 1.f, 0.f)) * glm::angleAxis(m_pitch, glm::vec3(1.f, 0.f, 0.f));
}

glm::vec3 FirstPersonController::GetForward() const
{
	return GetRotation() * glm::vec3(0.f, 0.f, -1.f);
}

float FirstPersonController::GetYaw() const
{
	return m_yaw;
}

void FirstPersonController::SetPitch(float pitch)
{
	m_pitch = pitch;
}

float FirstPersonController::GetPitch() const
{
	return m_pitch;
}
