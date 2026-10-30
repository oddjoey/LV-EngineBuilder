#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class InputManager;
class Camera;

// Something the player can't walk through, as a box on the floor plane, between bottom and top: a
// machine downstairs doesn't get in the way upstairs.
struct FloorObstacle
{
	glm::vec2 min = {};
	glm::vec2 max = {};
	float bottom = -1e6f;
	float top = 1e6f;
};

// Somewhere to stand above the floor: a stair tread, a landing, an upstairs floor.
struct WalkSurface
{
	glm::vec2 min = {};
	glm::vec2 max = {};
	float height = 0.f;
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

	void Update(float deltaTime, const InputManager& input, const std::vector<FloorObstacle>& obstacles, const FloorObstacle& roomBounds,
		const std::vector<WalkSurface>& surfaces);
	void ApplyToCamera(Camera& camera) const;

	glm::vec3 GetEyePosition() const;
	glm::vec3 GetFeetPosition() const;
	float GetRadius() const;
	glm::vec3 GetForward() const;
	float GetYaw() const;
	float GetPitch() const;
	glm::quat GetRotation() const;
	// On the floor (not mid-jump or falling), and how fast it's falling: for footsteps and landings.
	bool IsGrounded() const { return m_grounded; }
	float GetVerticalVelocity() const { return m_verticalVelocity; }
	bool IsCrouching() const { return m_crouching; }
private:
	void ResolveCollisions(const std::vector<FloorObstacle>& obstacles, const FloorObstacle& roomBounds);
	// How tall the player is right now, crouched or standing, for what they bump into.
	float HeadHeight() const;
	// Nothing low overhead stops the player standing up where they are.
	bool HasHeadroom(const std::vector<FloorObstacle>& obstacles) const;

	glm::vec3		m_position = {};
	float			m_yaw = 0.f;
	float			m_pitch = 0.f;
	float			m_eyeHeight = STAND_EYE_HEIGHT;
	float			m_radius = 0.28f;
	float			m_walkSpeed = 2.6f;
	float			m_runSpeed = 4.8f;
	float			m_crouchSpeed = 1.3f;
	bool			m_crouching = false;
	float			m_mouseSensitivity = BASE_SENSITIVITY;
	bool			m_invertY = false;
	float			m_verticalVelocity = 0.f;
	float			m_jumpSpeed = 3.6f;		// about a 0.65 m jump
	bool			m_grounded = true;

	static constexpr float BASE_SENSITIVITY = 0.0022f;	// radians per pixel
	static constexpr float GRAVITY = 9.81f;
	static constexpr float STEP_UP = 0.45f;		// the tallest step walked straight onto (or down off)
	static constexpr float HEAD_HEIGHT = 1.8f;
	// How far past the edge of something the player's centre can go and still be standing on it, as a
	// fraction of their radius.
	static constexpr float SUPPORT_FRACTION = 0.6f;
	static constexpr float STAND_EYE_HEIGHT = 1.68f;
	static constexpr float CROUCH_EYE_HEIGHT = 1.0f;
	static constexpr float CROUCH_HEAD_HEIGHT = 1.15f;
	static constexpr float CROUCH_EASE = 12.f;	// how quickly the eye settles at its new height, per second
};
