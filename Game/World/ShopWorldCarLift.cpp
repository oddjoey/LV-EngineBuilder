// The car off the ground: a floor jack and jack stands under its lifting points, the two-post lift, and each corner
// coming down onto whatever's under it (or its brake, with its wheel off) when what held it up is taken away.
#include "ShopWorld.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "../Controls.h"
#include "CarBody.h"
#include "ShopWorldShared.h"
#include "ToolShapes.h"

using namespace ShopLayout;

namespace
{
	constexpr float PUMP_SPEED = 0.1f;		// m/s a jack's saddle goes up, pumped
	constexpr float LOWER_SPEED = 0.16f;	// and down, let down
	constexpr float JACK_REACH = 0.42f;		// how high it lifts a corner, from where the car sits on its wheels
	constexpr float STAND_REACH = 0.4f;		// how high a stand's post comes up
	constexpr float CORNER_RISE = 0.6f;		// m/s a corner follows what lifts it
	// How far below the car's lifting point the saddle of each sits, at the car's own height: the jack's let right down,
	// the stand's with its post right in.
	constexpr float JACK_GAP = CarBody::JACK_POINT[1] - FLOOR_JACK_LOW;
	constexpr float STAND_GAP = CarBody::JACK_POINT[1] - JACK_STAND_LOW;
	// The lift's arms' pads, above its carriages.
	constexpr float LIFT_PAD = 0.14f;

	bool IsSupportTool(size_t toolId, HandToolKind& kind)
	{
		kind = ToolCatalog::Of(toolId).handKind;
		return kind == HandToolKind::FLOOR_JACK || kind == HandToolKind::JACK_STAND;
	}
}

glm::vec3 ShopWorld::CarLiftPoint(int point)
{
	using namespace CarBody;
	const int corner = std::clamp(point / 2, 0, CAR_CORNERS - 1);
	const float* at = point % 2 == 0 ? JACK_POINT : STAND_POINT;
	const float x = corner < 2 ? at[0] : -at[0], z = corner % 2 == 0 ? -at[2] : at[2];
	return { x, at[1], z };
}

float ShopWorld::CornerFloor(const Workshop& workshop, int corner) const
{
	const auto* build = workshop.GetCar();
	if (!build)
		return 0.f;
	const auto& car = build->assembly;
	return car.IsFilled(CarSlot::WHEEL, corner) && car.IsFilled(CarSlot::TIRE, corner) ? 0.f : -CarBody::NO_WHEEL_DROP;
}

bool ShopWorld::CarOnLift() const
{
	if (m_liftStation >= m_stations.size() || m_carStation >= m_stations.size())
		return false;
	const auto& lift = m_stations[m_liftStation];
	const auto& car = m_stations[m_carStation];
	if (!lift.present || !car.present)
		return false;
	// Parked between the posts (which stand either side along the lift's own x), its length along the lift's z: either
	// way round, and near enough square to it.
	glm::vec3 local = glm::inverse(lift.Rotation()) * (car.position - lift.position);
	float across = std::remainder(car.yaw - lift.yaw - glm::half_pi<float>(), glm::pi<float>());
	return std::abs(local.x) < 0.35f && std::abs(local.z) < 0.6f && std::abs(across) < 0.35f;
}

float ShopWorld::LiftUnderCar() const
{
	if (!CarOnLift())
		return 0.f;
	return (std::max)(0.f, m_liftHeight + LIFT_PAD - CarBody::JACK_POINT[1]);
}

bool ShopWorld::CarOffGround() const
{
	for (float height : m_cornerHeights)
		if (height > 0.01f)
			return true;
	return false;
}

int ShopWorld::SupportPointOf(size_t tool) const
{
	for (int point = 0; point < CAR_LIFT_POINTS; point++)
		if (m_carSupports[point].tool == tool)
			return point;
	return -1;
}

void ShopWorld::ReleaseCarSupports()
{
	for (auto& support : m_carSupports)
	{
		support.tool = NO_TOOL;
		support.height = 0.f;
		if (support.post)
			support.post->SetVisible(false);
	}
}

void ShopWorld::UpdateCarLift(const Workshop& workshop, float deltaTime)
{
	const auto* build = workshop.GetCar();
	bool present = build && m_carStation < m_stations.size() && m_stations[m_carStation].present;
	// What's under it stays under it only while it's still there, where it was put: picked up (or gone), it's let go.
	for (auto& support : m_carSupports)
	{
		if (support.tool == NO_TOOL)
			continue;
		const ItemVisual* visual = Find(ItemRef::Tool(static_cast<uint32_t>(support.tool)));
		if (!present || !visual || visual->place != ItemPlace::WORLD)
		{
			support.tool = NO_TOOL;
			support.height = 0.f;
		}
	}
	// Each corner goes to the highest of what holds it: its wheel (or its brake, with that off), the lift's arms, and
	// a jack or stand under either of its points. Lifted, it follows; let go, it drops.
	const float lift = LiftUnderCar();
	bool changed = false;
	for (int corner = 0; corner < CAR_CORNERS; corner++)
	{
		float held = present ? CornerFloor(workshop, corner) : 0.f;
		held = (std::max)(held, lift);
		for (int point : { corner * 2, corner * 2 + 1 })
			if (m_carSupports[point].tool != NO_TOOL)
				held = (std::max)(held, m_carSupports[point].height);
		float& height = m_cornerHeights[corner];
		float& speed = m_cornerSpeeds[corner];
		const float before = height;
		if (held > height)
		{
			height = (std::min)(held, height + CORNER_RISE * deltaTime);
			speed = 0.f;
		}
		else if (held < height)
		{
			speed += GRAVITY * deltaTime;
			height = (std::max)(held, height - speed * deltaTime);
			if (height == held)
			{
				if (speed > 1.5f)
					Cue(Sound::DROP, CarToWorld(CarLiftPoint(corner * 2)), (std::min)(1.f, speed / 4.f));
				speed = 0.f;
			}
		}
		else
			speed = 0.f;
		if (height != before)
			changed = true;
	}
	if (changed)
	{
		// Everything on it is laid out again where it's gone, and what's set down on it or walked into moves with it.
		m_carLaidOutAt = glm::vec3(-1e9f);
		m_supportChanged = true;
		if (present)
		{
			m_stations[m_carStation].object->SetPosition(CarOrigin());
			m_stations[m_carStation].object->SetRotation(CarRotation());
		}
	}

	// The jacks' rams and the stands' posts, from the tool up to the point they hold (or as far as they've come up).
	for (int point = 0; point < CAR_LIFT_POINTS; point++)
	{
		auto& support = m_carSupports[point];
		if (!support.post)
			continue;
		const ItemVisual* visual = support.tool != NO_TOOL ? Find(ItemRef::Tool(static_cast<uint32_t>(support.tool))) : nullptr;
		support.post->SetVisible(visual != nullptr);
		if (!visual)
			continue;
		HandToolKind kind;
		IsSupportTool(support.tool, kind);
		const glm::vec3 base = visual->object->GetPosition() + glm::vec3(0.f, kind == HandToolKind::FLOOR_JACK ? FLOOR_JACK_LOW : JACK_STAND_LOW - 0.03f, 0.f);
		// To the car's point if it's resting on this, or as high as it's been run up.
		const glm::vec3 point3 = CarToWorld(CarLiftPoint(point));
		const float top = (std::min)(point3.y, visual->object->GetPosition().y + CarBody::JACK_POINT[1] + support.height);
		support.post->SetPosition(base);
		support.post->SetRotation(glm::quat(1.f, 0.f, 0.f, 0.f));
		support.post->SetScale({ 1.f, (std::max)(0.01f, top - base.y), 1.f });
	}
}

bool ShopWorld::UpdateSupportTool(Workshop& workshop, const FirstPersonController& player, const InputManager& input, size_t toolId)
{
	HandToolKind kind;
	if (!IsSupportTool(toolId, kind))
		return false;
	const auto* build = workshop.GetCar();
	if (!build || m_carStation >= m_stations.size() || !m_stations[m_carStation].present)
		return false;
	const bool jack = kind == HandToolKind::FLOOR_JACK;
	glm::vec3 eye = player.GetEyePosition(), forward = player.GetForward();
	// Every lifting point marked: ready if this can go under it now.
	std::optional<int> aimed;
	float best = 0.f;
	for (int point = 0; point < CAR_LIFT_POINTS; point++)
	{
		const int corner = point / 2;
		bool free = m_carSupports[point].tool == NO_TOOL;
		bool ready = free && (jack || m_cornerHeights[corner] >= STAND_GAP);
		glm::vec3 at = CarToWorld(CarLiftPoint(point));
		m_fitPoints.push_back({ at, ready });
		float along = glm::dot(at - eye, forward);
		if (along <= 0.f || along > INTERACT_DISTANCE + 0.5f)
			continue;
		float off = glm::length(at - (eye + forward * along));
		if (off < (std::max)(0.1f, along * 0.06f) && (!aimed || off < best))
		{
			aimed = point;
			best = off;
		}
	}
	if (!aimed)
	{
		m_prompt = jack ? "Aim at one of the car's lifting points (marked) to slide the jack under it"
			: "Aim at one of the car's lifting points (marked) to put the stand under it: jack that corner up first";
		return true;
	}
	const int point = *aimed, corner = point / 2;
	const char* CORNERS[] = { "front left", "front right", "rear left", "rear right" };
	const std::string where = std::format("the {} {}", CORNERS[corner], point % 2 == 0 ? "jacking point" : "stand point");
	m_spannerTarget = CarToWorld(CarLiftPoint(point));
	if (m_carSupports[point].tool != NO_TOOL)
	{
		m_prompt = std::format("There's already something under {}", where);
		return true;
	}
	if (!jack && m_cornerHeights[corner] < STAND_GAP)
	{
		m_prompt = std::format("{}: jack that corner up first, for the stand to go under it", where);
		return true;
	}
	if (!jack && m_cornerHeights[corner] > STAND_REACH)
	{
		m_prompt = std::format("{}: it's higher than the stand reaches; let it down a little", where);
		return true;
	}
	m_prompt = std::format("[LMB] / [E] Put the {} under {}", jack ? "jack" : "stand", where);
	if (!Controls::Pressed(input, Control::PRIMARY) && !Controls::Pressed(input, Control::USE))
		return true;
	ItemVisual* visual = Find(ItemRef::Tool(static_cast<uint32_t>(toolId)));
	if (!visual)
		return true;
	// Set down on the floor under the point, the jack's handle out from the car's side.
	const auto& station = m_stations[m_carStation];
	glm::vec3 at = CarToWorld(CarLiftPoint(point));
	visual->yaw = jack ? station.yaw + (corner % 2 == 0 ? glm::half_pi<float>() : -glm::half_pi<float>()) : station.yaw;
	PlaceInWorld(*visual, { at.x, station.position.y + 0.01f, at.z }, {});
	visual->body.resting = true;
	auto& support = m_carSupports[point];
	support.tool = toolId;
	// A jack goes in with its saddle up against the point (holding the corner where it is); a stand is ratcheted up to it.
	support.height = m_cornerHeights[corner];
	Cue(Sound::SET_DOWN, at);
	Toast(jack ? std::format("The jack's under {}: aim at it and hold [LMB] to pump it up, [RMB] to let it down.", where)
		: std::format("The stand's under {}: let the jack down and the corner rests on it.", where));
	return true;
}

bool ShopWorld::DevSupport(Workshop& workshop, int point, float height, bool stand)
{
	if (point < 0 || point >= CAR_LIFT_POINTS || m_carStation >= m_stations.size() || !m_stations[m_carStation].present)
		return false;
	// The first jack (or stand) the shop has that isn't under the car already, given if there isn't one.
	const auto& tools = ToolCatalog::All();
	const HandToolKind want = stand ? HandToolKind::JACK_STAND : HandToolKind::FLOOR_JACK;
	for (size_t index = 0; index < tools.size(); index++)
	{
		if (tools[index].handKind != want)
			continue;
		for (int copy = 0; copy < Workshop::MAX_TOOL_COPIES; copy++)
		{
			size_t id = ToolCatalog::IdOf(index, static_cast<size_t>(copy));
			ItemVisual* visual = Find(ItemRef::Tool(static_cast<uint32_t>(id)));
			if (!visual || SupportPointOf(id) >= 0)
				continue;
			// (Out of its delivery, if it is still in one.)
			if (visual->place == ItemPlace::PACKAGED)
				DevHold(visual->ref);
			const auto& station = m_stations[m_carStation];
			glm::vec3 at = CarToWorld(CarLiftPoint(point));
			visual->yaw = station.yaw;
			PlaceInWorld(*visual, { at.x, station.position.y + 0.01f, at.z }, {});
			m_carSupports[point].tool = id;
			m_carSupports[point].height = height;
			return true;
		}
	}
	return false;
}

bool ShopWorld::UpdatePlacedJack(Workshop& workshop, const FirstPersonController& player, const InputManager& input, float deltaTime)
{
	// A jack or stand under the car, aimed at: its saddle, low down under the car's edge (aimed at past the body).
	glm::vec3 eye = player.GetEyePosition(), forward = player.GetForward();
	int aimed = -1;
	float best = 0.f;
	for (int point = 0; point < CAR_LIFT_POINTS; point++)
	{
		const auto& support = m_carSupports[point];
		if (support.tool == NO_TOOL)
			continue;
		const ItemVisual* visual = Find(ItemRef::Tool(static_cast<uint32_t>(support.tool)));
		if (!visual)
			continue;
		for (float up : { 0.08f, 0.2f })
		{
			glm::vec3 at = visual->object->GetPosition() + glm::vec3(0.f, up, 0.f);
			float along = glm::dot(at - eye, forward);
			if (along <= 0.f || along > INTERACT_DISTANCE)
				continue;
			float off = glm::length(at - (eye + forward * along));
			if (off < (std::max)(0.12f, along * 0.07f) && (aimed < 0 || off < best))
			{
				aimed = point;
				best = off;
			}
		}
	}
	if (aimed < 0)
		return false;
	auto& support = m_carSupports[aimed];
	HandToolKind kind;
	IsSupportTool(support.tool, kind);
	ItemVisual* visual = Find(ItemRef::Tool(static_cast<uint32_t>(support.tool)));
	visual->object->SetEmissive(PART_GLOW);
	const int corner = aimed / 2;
	if (kind == HandToolKind::FLOOR_JACK)
	{
		m_prompt = std::format("Floor jack ({:.0f} cm up)   [LMB] Hold to pump it up   [RMB] Hold to let it down   [E] Take it away",
			(std::max)(0.f, support.height) * 100.f);
		bool up = Controls::Held(input, Control::PRIMARY), down = Controls::Held(input, Control::SET_DOWN);
		if (up != down)
		{
			// Pumped, it lifts the corner from where it is (from under it, the saddle comes up to it first).
			const float from = up ? (std::max)(support.height, m_cornerHeights[corner] - 0.005f) : support.height;
			support.height = std::clamp(from + (up ? PUMP_SPEED : -LOWER_SPEED) * deltaTime, -JACK_GAP, JACK_REACH);
			if (up && support.height >= JACK_REACH)
				m_secondaryPrompt = "That's as high as it goes";
		}
	}
	else
		m_prompt = std::format("Jack stand ({:.0f} cm)   [E] Take it away", support.height * 100.f);
	if (Controls::Pressed(input, Control::USE))
	{
		// Pulled out from under: the corner comes down onto what else is there, if anything.
		support.tool = NO_TOOL;
		support.height = 0.f;
		PickUp(workshop, *visual);
		return true;
	}
	// (The buttons are the jack's while it's aimed at, not a click on whatever's behind it.)
	return Controls::Held(input, Control::PRIMARY) || Controls::Held(input, Control::SET_DOWN) || Controls::Pressed(input, Control::PRIMARY);
}
