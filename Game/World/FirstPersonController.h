#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class InputManager;
class Camera;

// Something the player can't walk through, as a box on the floor plane.
struct FloorObstacle
{
	glm::vec2 min = {};
	glm::vec2 max = {};
};

// Walk-and-look controller: WASD, Shift to hurry, mouse to look. Collides with the room
// walls and with obstacles as circles against boxes on the floor plane.
class FirstPersonController
{
public:
	void SetPosition(const glm::vec3& feetPosition);
	void SetYaw(float yaw);
	void SetPitch(float pitch);
	// multiplier on the base look speed
	void SetMouseSensitivity(float multiplier);
	void SetInvertY(bool invert);

	void Update(float deltaTime, const InputManager& input, const std::vector<FloorObstacle>& obstacles, const FloorObstacle& roomBounds);
	void ApplyToCamera(Camera& camera) const;

	glm::vec3 GetEyePosition() const;
	glm::vec3 GetFeetPosition() const;
	float GetRadius() const;
	glm::vec3 GetForward() const;
	glm::vec3 GetRight() const;
	float GetYaw() const;
	float GetPitch() const;
	glm::quat GetRotation() const;
private:
	void ResolveCollisions(const std::vector<FloorObstacle>& obstacles, const FloorObstacle& roomBounds);

	glm::vec3		m_position = {};
	float			m_yaw = 0.f;
	float			m_pitch = 0.f;
	float			m_eyeHeight = 1.68f;
	float			m_radius = 0.28f;
	float			m_walkSpeed = 2.6f;
	float			m_runSpeed = 4.8f;
	float			m_mouseSensitivity = BASE_SENSITIVITY;
	bool			m_invertY = false;
	float			m_verticalVelocity = 0.f;
	float			m_jumpSpeed = 3.6f;		// about a 0.65 m jump

	static constexpr float BASE_SENSITIVITY = 0.0022f;	// radians per pixel
	static constexpr float GRAVITY = 9.81f;
};
