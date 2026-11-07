#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

#include "LV/Engine/Objects/Object.h"
#include "MonkeyLook.h"

class ShopWorld;

// A player's whole body in the world, a monkey in a cap of their colour, made of pieces that move: the
// torso, the head and its cap (looking up and down with them), the arms (swinging as they walk, or held
// out) and the legs (thigh and shin, bent to crouch). Another player over the network has one; so does
// this player, seen from behind in third person.
class PlayerAvatar
{
public:
	void Spawn(ShopWorld& world, const glm::vec3& feet, float yaw, const MonkeyLook& look);
	void Remove();
	bool IsSpawned() const { return m_torso != nullptr; }
	void SetVisible(bool visible);
	// Its colours, and whether it has a tail.
	void SetLook(const MonkeyLook& look);

	// Stood at feet, facing yaw, looking pitch (up is positive), crouched or not, arms as NetProtocol's
	// presence has them (0 down, 1 the right one out, 2 both). How fast it walks is worked out from
	// how far the feet have come since the last pose.
	void Pose(float deltaTime, const glm::vec3& feet, float yaw, float pitch, bool crouching, int arms);
	// The middle of the right hand, where what's held goes, and the eyes, as last posed.
	glm::vec3 GetHand() const { return m_hand; }
	glm::vec3 GetEye() const { return m_eye; }

	static glm::quat ViewRotation(float yaw, float pitch);
	// The monkey stood still, for a picture of it: each piece's model, where it goes (feet at the origin) and
	// its colour.
	struct StandingPiece
	{
		std::string model = "";
		glm::mat4 transform = glm::mat4(1.f);
		glm::vec3 tint = glm::vec3(1.f);
	};
	static std::vector<StandingPiece> Standing(const MonkeyLook& look);
private:
	// Every piece, to show, hide or remove them all.
	std::vector<pObject*> Pieces();

	// The jointed pieces, and what rides on each: the belly and tail on the torso, the face and cap on
	// the head, a paw (hand) on each arm and a foot on each shin.
	pObject m_torso = nullptr;
	pObject m_belly = nullptr;
	pObject m_tail = nullptr;
	pObject m_head = nullptr;
	pObject m_face = nullptr;
	pObject m_cap = nullptr;
	pObject m_arm[2] = { nullptr, nullptr };		// left, right
	pObject m_paw[2] = { nullptr, nullptr };
	pObject m_thigh[2] = { nullptr, nullptr };
	pObject m_shin[2] = { nullptr, nullptr };
	pObject m_foot[2] = { nullptr, nullptr };
	MonkeyLook m_look = {};
	bool m_visible = true;

	bool m_posed = false;
	glm::vec3 m_lastFeet = {};
	// Walking speed (eased, so a late update doesn't stop them dead), how far through a stride, how far
	// down into a crouch (0 standing, 1 crouched), and how far each arm is held out (0 hanging, 1 out).
	float m_speed = 0.f;
	float m_stride = 0.f;
	float m_crouch = 0.f;
	float m_armOut[2] = { 0.f, 0.f };
	glm::vec3 m_hand = {};
	glm::vec3 m_eye = {};
};
