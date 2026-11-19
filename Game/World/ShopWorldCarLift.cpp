// The car off the ground: floor jacks and jack stands (shop fittings, moved about with F) under its lifting points, the
// two-post lift, and each corner coming down onto whatever's under it (or its brake, with its wheel off) when what held
// it up is taken away.
#include "ShopWorld.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "../Controls.h"
#include "../Simulation/Furniture.h"
#include "CarBody.h"
#include "ShopWorldShared.h"
#include "ToolShapes.h"

using namespace ShopLayout;

namespace
{
	constexpr float PUMP_SPEED = 0.1f;		// m/s a jack's saddle goes up, pumped
	constexpr float LOWER_SPEED = 0.16f;	// and down, let down
	constexpr float JACK_REACH = 0.42f;		// how high it lifts the car, from where it sits on its wheels
	constexpr float STAND_REACH = 0.4f;		// how high a stand's post comes up
	constexpr float CORNER_RISE = 0.6f;		// m/s a corner follows what lifts it
	// How far below the car's sill the jack's saddle sits let right down; and how far the car has to be lifted for a stand
	// (its post right in) to go under.
	constexpr float JACK_GAP = CarBody::JACK_POINT[1] - FLOOR_JACK_LOW;
	constexpr float STAND_LIFT = JACK_STAND_LOW - CarBody::JACK_POINT[1];
	// The lift's arms' pads, above its carriages.
	constexpr float LIFT_PAD = 0.14f;
	// How near a jack or stand being moved has to come to the floor under a lifting point to go under it.
	constexpr float SNAP_REACH = 0.45f;
}

bool ShopWorld::IsCarSupport(const Station& station, bool* jack) const
{
	if (station.type != StationType::FURNITURE || station.furnitureIndex < 0)
		return false;
	auto kind = FurnitureCatalog::All()[station.furnitureIndex].kind;
	if (jack)
		*jack = kind == FurnitureKind::FLOOR_JACK;
	return kind == FurnitureKind::FLOOR_JACK || kind == FurnitureKind::JACK_STAND;
}

glm::vec3 ShopWorld::CarLiftPoint(int point)
{
	using namespace CarBody;
	// The two in the middle of each end: under the front crossmember, and under the differential.
	if (point == FRONT_LIFT_POINT)
		return { FRONT_JACK_POINT[0], FRONT_JACK_POINT[1], FRONT_JACK_POINT[2] };
	if (point == REAR_LIFT_POINT)
		return { REAR_JACK_POINT[0], REAR_JACK_POINT[1], REAR_JACK_POINT[2] };
	const int corner = std::clamp(point / 2, 0, CAR_CORNERS - 1);
	const float* at = point % 2 == 0 ? JACK_POINT : STAND_POINT;
	const float x = corner < 2 ? at[0] : -at[0], z = corner % 2 == 0 ? -at[2] : at[2];
	return { x, at[1], z };
}

std::pair<int, int> ShopWorld::LiftPointCorners(int point)
{
	if (point == FRONT_LIFT_POINT)
		return { 0, 1 };
	if (point == REAR_LIFT_POINT)
		return { 2, 3 };
	return { point / 2, point / 2 };
}

float ShopWorld::LiftPointHeight(int point) const
{
	// Where what's under it would meet the car: the lower of the corners it holds.
	auto [first, second] = LiftPointCorners(point);
	return (std::min)(m_rig.cornerHeights[first], m_rig.cornerHeights[second]);
}

std::string ShopWorld::LiftPointName(int point)
{
	if (point == FRONT_LIFT_POINT)
		return "the front jacking point, under the crossmember";
	if (point == REAR_LIFT_POINT)
		return "the rear jacking point, under the differential";
	const char* CORNERS[] = { "front left", "front right", "rear left", "rear right" };
	return std::format("the {} {}", CORNERS[std::clamp(point / 2, 0, CAR_CORNERS - 1)], point % 2 == 0 ? "jacking point" : "stand point");
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
	if (m_liftStation >= m_stations.size() || m_rig.carStation >= m_stations.size())
		return false;
	const auto& lift = m_stations[m_liftStation];
	const auto& car = m_stations[m_rig.carStation];
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
	for (float height : m_rig.cornerHeights)
		if (height > 0.01f)
			return true;
	return false;
}

int ShopWorld::SupportPointOf(size_t station) const
{
	for (int point = 0; point < CAR_LIFT_POINTS; point++)
		if (m_rig.carSupports[point].station == station)
			return point;
	return -1;
}

void ShopWorld::ReleaseCarSupports()
{
	for (auto& support : m_rig.carSupports)
	{
		support.station = NO_STATION;
		support.height = 0.f;
		if (support.post)
			support.post->SetVisible(false);
	}
}

glm::vec3 ShopWorld::LiftPointFloor(int point) const
{
	// The floor under a lifting point, as the car stands on its wheels (where something goes to be under it).
	if (m_rig.carStation >= m_stations.size())
		return {};
	const auto& car = m_stations[m_rig.carStation];
	glm::vec3 at = car.position + car.Rotation() * CarLiftPoint(point);
	return { at.x, car.position.y, at.z };
}

float ShopWorld::SupportYaw(int point, bool jack) const
{
	// A jack is rolled in from the side its point is on, its handle left out past the body: from the side at a corner,
	// from the end in the middle of one. A stand any way round, square to the car.
	const float carYaw = m_rig.carStation < m_stations.size() ? m_stations[m_rig.carStation].yaw : 0.f;
	if (!jack || point == FRONT_LIFT_POINT)
		return carYaw;
	if (point == REAR_LIFT_POINT)
		return carYaw + glm::pi<float>();
	return carYaw + ((point / 2) % 2 == 0 ? glm::half_pi<float>() : -glm::half_pi<float>());
}

std::optional<int> ShopWorld::SnapSupport(const Station& station, glm::vec3& position, float& yaw) const
{
	bool jack = false;
	if (!IsCarSupport(station, &jack) || m_rig.carStation >= m_stations.size() || !m_stations[m_rig.carStation].present || CarBeingDriven())
		return std::nullopt;
	// The free lifting point whose floor it's nearest, if it's near enough.
	std::optional<int> nearest;
	float best = SNAP_REACH;
	for (int point = 0; point < CAR_LIFT_POINTS; point++)
	{
		if (m_rig.carSupports[point].station != NO_STATION)
			continue;
		glm::vec3 floor = LiftPointFloor(point);
		float distance = glm::length(glm::vec2(floor.x - position.x, floor.z - position.z));
		if (distance < best)
		{
			best = distance;
			nearest = point;
		}
	}
	if (nearest)
	{
		position = LiftPointFloor(*nearest);
		yaw = SupportYaw(*nearest, jack);
	}
	return nearest;
}

bool ShopWorld::SupportFitsUnder(int point, bool jack) const
{
	// A jack goes under anywhere (let right down, its saddle's under the sill). A stand goes in anywhere the car's up enough for
	// its post right in, and its post is run up to the car (as far as it reaches: higher than that, the car rests on it once
	// it's let down that far).
	return jack || LiftPointHeight(point) >= STAND_LIFT - 0.005f;
}

void ShopWorld::UpdateCarLift(const Workshop& workshop, float deltaTime)
{
	const auto* build = workshop.GetCar();
	bool present = build && m_rig.carStation < m_stations.size() && m_stations[m_rig.carStation].present;
	// What's under it stays under it only while it's still there, where it was put: moved away (or gone), it's let go.
	for (int point = 0; point < CAR_LIFT_POINTS; point++)
	{
		auto& support = m_rig.carSupports[point];
		if (support.station == NO_STATION)
			continue;
		bool still = present && support.station < m_stations.size() && m_stations[support.station].present
			&& !(m_move && m_move->stationIndex == support.station);
		if (still)
		{
			glm::vec3 floor = LiftPointFloor(point), at = m_stations[support.station].position;
			still = glm::length(glm::vec2(floor.x - at.x, floor.z - at.z)) < 0.1f;
		}
		if (!still)
		{
			support.station = NO_STATION;
			support.height = 0.f;
		}
	}
	// And a jack or stand standing under a free point (set down there, or there already when the game was loaded) goes
	// under it: a jack with its saddle up to the car where it is, a stand ratcheted up to it (the car's lifted onto it, if
	// it was sat lower).
	if (present && !CarBeingDriven())
		for (size_t s = m_fixedStations; s < m_stations.size(); s++)
		{
			const auto& station = m_stations[s];
			bool jack = false;
			if (!station.present || !IsCarSupport(station, &jack) || (m_move && m_move->stationIndex == s) || SupportPointOf(s) >= 0)
				continue;
			for (int point = 0; point < CAR_LIFT_POINTS; point++)
			{
				if (m_rig.carSupports[point].station != NO_STATION)
					continue;
				glm::vec3 floor = LiftPointFloor(point);
				if (glm::length(glm::vec2(floor.x - station.position.x, floor.z - station.position.z)) > 0.05f)
					continue;
				m_rig.carSupports[point].station = s;
				m_rig.carSupports[point].height = jack ? LiftPointHeight(point) : std::clamp(LiftPointHeight(point), STAND_LIFT, STAND_REACH);
				break;
			}
		}

	// Each corner goes to the highest of what holds it: its wheel (or its brake, with that off), the lift's arms, and
	// a jack or stand under either of its points or the middle of its end. Lifted, it follows; let go, it drops.
	const float lift = LiftUnderCar();
	bool changed = false;
	for (int corner = 0; corner < CAR_CORNERS; corner++)
	{
		float held = present ? CornerFloor(workshop, corner) : 0.f;
		held = (std::max)(held, lift);
		for (int point : { corner * 2, corner * 2 + 1, corner < 2 ? FRONT_LIFT_POINT : REAR_LIFT_POINT })
			if (m_rig.carSupports[point].station != NO_STATION)
				held = (std::max)(held, m_rig.carSupports[point].height);
		float& height = m_rig.cornerHeights[corner];
		float& speed = m_rig.cornerSpeeds[corner];
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
		m_rig.carLaidOutAt = glm::vec3(-1e9f);
		m_supportChanged = true;
		if (present)
		{
			m_stations[m_rig.carStation].object->SetPosition(CarOrigin());
			m_stations[m_rig.carStation].object->SetRotation(CarRotation());
		}
	}

	// The jacks' rams and the stands' posts, from the jack or stand up to the point they hold (or as far as they've come up).
	for (int point = 0; point < CAR_LIFT_POINTS; point++)
	{
		auto& support = m_rig.carSupports[point];
		if (!support.post)
			continue;
		const Station* station = support.station != NO_STATION ? &m_stations[support.station] : nullptr;
		support.post->SetVisible(station != nullptr);
		if (!station)
			continue;
		// (A jack lifts with its arm: see PoseJacks. Only a stand has a post.)
		bool jack = false;
		IsCarSupport(*station, &jack);
		if (jack)
		{
			support.post->SetVisible(false);
			continue;
		}
		const glm::vec3 base = station->position + glm::vec3(0.f, JACK_STAND_LOW - 0.03f, 0.f);
		const glm::vec3 point3 = CarToWorld(CarLiftPoint(point));
		const float top = (std::min)(point3.y, station->position.y + CarLiftPoint(point).y + support.height);
		support.post->SetPosition(base);
		support.post->SetRotation(glm::quat(1.f, 0.f, 0.f, 0.f));
		support.post->SetScale({ 1.f, (std::max)(0.01f, top - base.y), 1.f });
	}
}

bool ShopWorld::DevSupport(Workshop& workshop, int point, float height, bool stand)
{
	(void)workshop;
	if (point < 0 || point >= CAR_LIFT_POINTS || m_rig.carStation >= m_stations.size() || !m_stations[m_rig.carStation].present)
		return false;
	// The first of the shop's jacks (or stands) that isn't under the car already: out of its crate if it's still in it.
	for (size_t s = m_fixedStations; s < m_stations.size(); s++)
	{
		bool jack = false;
		if (!IsCarSupport(m_stations[s], &jack) || jack == stand || SupportPointOf(s) >= 0)
			continue;
		for (auto& [id, package] : m_packages)
			if (std::find(package.stations.begin(), package.stations.end(), s) != package.stations.end())
			{
				OpenPackage(package);
				break;
			}
		auto& station = m_stations[s];
		station.present = true;
		station.object->SetVisible(true);
		station.object->SetPickable(true);
		station.position = LiftPointFloor(point);
		station.yaw = SupportYaw(point, !stand);
		m_rig.carSupports[point].station = s;
		m_rig.carSupports[point].height = height;
		RebuildCollision();
		return true;
	}
	return false;
}

void ShopWorld::PoseJacks(float deltaTime)
{
	using namespace JackLayout;
	for (size_t s = m_fixedStations; s < m_stations.size(); s++)
	{
		auto& station = m_stations[s];
		bool jack = false;
		if (!IsCarSupport(station, &jack) || !jack)
			continue;
		auto& pieces = m_jacks[s];
		if (!pieces.arm)
		{
			auto piece = [&](const std::string& model) {
				auto object = Spawn(model, ShopTextures::PAINT, station.position);
				object->SetPickable(false);
				return object;
			};
			pieces.arm = piece(ShopModels::JACK_ARM);
			pieces.saddle = piece(ShopModels::JACK_SADDLE);
			pieces.handle = piece(ShopModels::JACK_HANDLE);
		}
		const bool shown = station.present && station.object->IsVisible();
		for (const auto& object : { pieces.arm, pieces.saddle, pieces.handle })
			object->SetVisible(shown);
		if (!shown)
			continue;

		// Worked: each stroke of the handle, on its way down, pushes the arm up (quickly, till the saddle's up against the
		// car, then with its weight on it); the handle twisted, the valve opens and it comes down.
		const int point = SupportPointOf(s);
		auto* support = point >= 0 ? &m_rig.carSupports[point] : nullptr;
		constexpr float STROKES = 1.4f;		// a second
		// A click is one whole stroke (up, then down, and back to rest), seen through to the end even once the button's
		// let go; held, the next follows on straight away.
		if (pieces.pumping && support && !pieces.stroking)
		{
			pieces.stroking = true;
			pieces.pumpPhase = 0.f;
		}
		if (pieces.stroking && support)
		{
			const float before = pieces.pumpPhase;
			pieces.pumpPhase = (std::min)(glm::two_pi<float>(), pieces.pumpPhase + deltaTime * glm::two_pi<float>() * STROKES);
			pieces.handleUp = 0.38f * std::sin(pieces.pumpPhase);
			// It lifts on the way down, from the top of the stroke to the bottom.
			const float down = -std::cos(pieces.pumpPhase);
			if (down > 0.f)
			{
				const bool touching = support->height >= LiftPointHeight(point) - 0.005f;
				support->height = (std::min)(JACK_REACH, support->height + PUMP_SPEED * glm::pi<float>() * down * (touching ? 1.f : 3.f) * deltaTime);
			}
			constexpr float BOTTOM = 1.5f * glm::pi<float>();
			if (before < BOTTOM && pieces.pumpPhase >= BOTTOM)
				Cue(Sound::RATCHET, pieces.grip, 0.25f);
			if (pieces.pumpPhase >= glm::two_pi<float>())
			{
				pieces.stroking = false;
				pieces.handleUp = 0.f;
			}
		}
		else
		{
			pieces.stroking = false;
			pieces.handleUp += (0.f - pieces.handleUp) * (1.f - std::exp(-deltaTime * 8.f));
		}
		// Asked to be moved while it's up (F): its valve's opened and it's let right down first, then it's picked up.
		if (pieces.releasing && (!support || pieces.pumping))
			pieces.releasing = false;
		pieces.lowering = pieces.lowering || pieces.releasing;
		pieces.twist += ((pieces.lowering ? 1.6f : 0.f) - pieces.twist) * (1.f - std::exp(-deltaTime * 6.f));
		if (pieces.lowering && support && pieces.twist > 1.f)
			support->height = (std::max)(-JACK_GAP, support->height - LOWER_SPEED * deltaTime);
		if (pieces.releasing && support->height <= -JACK_GAP + 0.001f)
		{
			pieces.releasing = false;
			if (!m_move)
				StartMove(s);
		}
		pieces.pumping = pieces.lowering = false;

		// The saddle's top: up under its point as far as it's been pumped, or (not under the car) let right down.
		const float target = support ? CarLiftPoint(point).y + support->height : FLOOR_JACK_SADDLE;
		pieces.saddleTop = support ? target : pieces.saddleTop + (target - pieces.saddleTop) * (1.f - std::exp(-deltaTime * 3.f));
		pieces.saddleTop = std::clamp(pieces.saddleTop, FLOOR_JACK_SADDLE, ARM_PIVOT_Y + ARM_LENGTH * 0.95f + SADDLE_HEIGHT);

		// The arm swung up to it about its pivot; the body rolled under it so the saddle stays where it was put.
		const float tipY = pieces.saddleTop - SADDLE_HEIGHT;
		const float swing = std::asin(std::clamp((tipY - ARM_PIVOT_Y) / ARM_LENGTH, -1.f, 1.f));
		const float tipX = ARM_PIVOT_X - ARM_LENGTH * std::cos(swing);
		const glm::quat turn = station.Rotation();
		const glm::vec3 origin = station.position + turn * glm::vec3(-tipX, 0.f, 0.f);
		if (!(m_move && m_move->stationIndex == s))
			station.object->SetPosition(origin);
		const glm::vec3 z = { 0.f, 0.f, 1.f };
		pieces.arm->SetPosition(origin + turn * glm::vec3(ARM_PIVOT_X, ARM_PIVOT_Y, 0.f));
		pieces.arm->SetRotation(turn * glm::angleAxis(-swing, z));
		pieces.saddle->SetPosition(origin + turn * glm::vec3(tipX, tipY, 0.f));
		pieces.saddle->SetRotation(turn);
		const glm::quat handle = turn * glm::angleAxis(HANDLE_REST + pieces.handleUp, z) * glm::angleAxis(pieces.twist, glm::vec3(1.f, 0.f, 0.f));
		const glm::vec3 socket = origin + turn * glm::vec3(HANDLE_X, HANDLE_Y, 0.f);
		pieces.handle->SetPosition(socket);
		pieces.handle->SetRotation(handle);
		pieces.grip = socket + handle * glm::vec3(HANDLE_LENGTH - 0.07f, 0.f, 0.f);
		for (const auto& object : { pieces.arm, pieces.saddle, pieces.handle })
		{
			object->SetTint(station.object->GetTint());
			object->SetEmissive(station.object->GetEmissive());
		}
	}
}

std::string ShopWorld::DevCheckSupport(int point, bool stand)
{
	// As UpdateMove would find it: the shop's first free jack (or stand) brought to the floor under that point.
	for (size_t s = m_fixedStations; s < m_stations.size(); s++)
	{
		bool jack = false;
		if (!IsCarSupport(m_stations[s], &jack) || jack == stand || SupportPointOf(s) >= 0)
			continue;
		// (Out of its crate first, if it's still in one: set up wherever the crate stood.)
		for (auto& [id, package] : m_packages)
			if (std::find(package.stations.begin(), package.stations.end(), s) != package.stations.end())
			{
				OpenPackage(package);
				break;
			}
		const auto& station = m_stations[s];
		glm::vec3 position = LiftPointFloor(point);
		float yaw = station.yaw;
		auto snapped = SnapSupport(station, position, yaw);
		if (!snapped)
			return "It didn't snap to any free point there.";
		m_moveSnap = *snapped;
		std::string why;
		const bool fitsUnder = SupportFitsUnder(*snapped, jack);
		const bool fits = StationFits(station, position, yaw, nullptr, &why);
		const float height = LiftPointHeight(*snapped);
		m_moveSnap = -1;
		return std::format("Snapped to {}; the car's {:.0f} mm up there; {}{}", LiftPointName(*snapped), height * 1000.f,
			!fitsUnder ? "too low for the stand" : fits ? "it fits" : "blocked: ", fits ? "" : why);
	}
	return "No free one, out of its crate.";
}

bool ShopWorld::UpdatePlacedJack(Workshop& workshop, const FirstPersonController& player, const InputManager& input, float deltaTime)
{
	(void)workshop;
	(void)deltaTime;
	// A jack under the car is worked from the end of its handle (out past the body); a stand, aimed at low down.
	glm::vec3 eye = player.GetEyePosition(), forward = player.GetForward();
	int aimed = -1;
	float best = 0.f;
	// A jack's handle taken hold of stays in hand while the button's held, however far it swings from under the crosshair
	// as it's pumped (or twisted): let go of the button, or walk off, and it's let go.
	const bool holding = Controls::Held(input, Control::PRIMARY) || Controls::Held(input, Control::SET_DOWN);
	if (m_workingJack != NO_STATION)
	{
		int point = SupportPointOf(m_workingJack);
		auto jack = m_jacks.find(m_workingJack);
		if (holding && point >= 0 && jack != m_jacks.end() && glm::distance(eye, jack->second.grip) < INTERACT_DISTANCE + 0.5f)
			aimed = point;
		else
			m_workingJack = NO_STATION;
	}
	for (int point = 0; point < CAR_LIFT_POINTS && aimed < 0; point++)
	{
		const auto& support = m_rig.carSupports[point];
		if (support.station == NO_STATION)
			continue;
		auto jack = m_jacks.find(support.station);
		std::vector<glm::vec3> spots;
		if (jack != m_jacks.end())
		{
			// Anywhere along it from the middle of the handle to the end of its grip, at rest or swung.
			const glm::vec3 socket = jack->second.handle->GetPosition(), grip = jack->second.grip;
			spots = { grip, glm::mix(socket, grip, 0.75f), glm::mix(socket, grip, 0.5f) };
		}
		else
			spots = { m_stations[support.station].position + glm::vec3(0.f, 0.08f, 0.f), m_stations[support.station].position + glm::vec3(0.f, 0.2f, 0.f) };
		for (const auto& at : spots)
		{
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
	auto& support = m_rig.carSupports[aimed];
	const size_t stationIndex = support.station;
	auto& station = m_stations[stationIndex];
	bool jack = false;
	IsCarSupport(station, &jack);
	station.object->SetEmissive(STATION_GLOW);
	if (jack)
	{
		// From its handle: pumped up and down (left), or twisted to open the release valve (right). See PoseJacks.
		auto& pieces = m_jacks[stationIndex];
		m_prompt = pieces.releasing
			? std::format("Letting the floor jack down ({:.0f} cm up), to move it   [LMB] Pump it back up", (std::max)(0.f, support.height + JACK_GAP) * 100.f)
			: std::format("Floor jack's handle, under {} ({:.0f} cm up)   [LMB] Hold to pump   [RMB] Hold to twist it and let it down   [F] Let it down and move it",
				LiftPointName(aimed), (std::max)(0.f, support.height) * 100.f);
		bool up = Controls::Held(input, Control::PRIMARY), down = Controls::Held(input, Control::SET_DOWN);
		pieces.pumping = up && !down;
		pieces.lowering = down && !up;
		if (up || down)
			m_workingJack = stationIndex;
		if (pieces.handle)
			pieces.handle->SetEmissive(STATION_GLOW * 2.f);
		if (up && support.height >= JACK_REACH)
			m_secondaryPrompt = "That's as high as it goes";
	}
	else
		m_prompt = std::format("Jack stand under {} ({:.0f} cm)   [F] Move it", LiftPointName(aimed), support.height * 100.f);
	// Moved away (F), it's let go: the car comes down onto whatever else is under it. A jack that's up is let right down
	// first, by its valve (see PoseJacks), and picked up once it's down.
	if (Controls::Pressed(input, Control::MOVE))
	{
		if (jack && support.height > -JACK_GAP + 0.001f)
			m_jacks[stationIndex].releasing = !m_jacks[stationIndex].releasing;
		else
			StartMove(stationIndex);
		return true;
	}
	// (The buttons are the jack's while it's aimed at, not a click on whatever's behind it.)
	return Controls::Held(input, Control::PRIMARY) || Controls::Held(input, Control::SET_DOWN) || Controls::Pressed(input, Control::PRIMARY);
}
