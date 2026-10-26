#include "FirstPersonController.h"

#include <algorithm>

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

void FirstPersonController::Update(float deltaTime, const InputManager& input, const std::vector<FloorObstacle>& obstacles, const FloorObstacle& roomBounds)
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

	if (glm::length(move) > 0.f)
	{
		float speed = input.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ? m_runSpeed : m_walkSpeed;
		m_position += glm::normalize(move) * speed * deltaTime;
	}

	// Jumping: a fixed take-off speed and gravity, landing back on the shop floor.
	bool grounded = m_position.y <= 0.f;
	if (grounded && input.WasKeyPressed(GLFW_KEY_SPACE))
		m_verticalVelocity = m_jumpSpeed;
	m_verticalVelocity -= GRAVITY * deltaTime;
	m_position.y += m_verticalVelocity * deltaTime;
	if (m_position.y <= 0.f)
	{
		m_position.y = 0.f;
		m_verticalVelocity = 0.f;
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

glm::vec3 FirstPersonController::GetRight() const
{
	return GetRotation() * glm::vec3(1.f, 0.f, 0.f);
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
