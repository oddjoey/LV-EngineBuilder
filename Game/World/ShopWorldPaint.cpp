#include "ShopWorld.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "LV/Engine/Utility/InputManager.h"
#include "../Controls.h"
#include "GarageLayout.h"
#include "ShopWorldShared.h"

using namespace ShopLayout;

namespace
{
	// Paint reaches any wall in sight, at the size set with the scroll wheel.
	constexpr float PAINT_REACH = 80.f;
	// The sizes each can be set to (radius, metres), smallest to biggest, and how much a notch of the
	// wheel changes it.
	constexpr float PAINT_SIZE_MIN[3] = { 0.0015f, 0.02f, 0.015f };
	constexpr float PAINT_SIZE_MAX[3] = { 0.04f, 0.6f, 0.3f };
	constexpr float PAINT_SIZE_STEP = 1.18f;
	// How quickly spray fills its circle in, and how fast a coat goes on a part (or thinner takes it off).
	constexpr float SPRAY_FILL_PER_SECOND = 7.f;
	constexpr int SPRAY_SPECKLES_MAX = 6000;
	constexpr float COAT_PER_SECOND = 1.2f;
	constexpr float STRIP_PER_SECOND = 1.2f;

	float WallPlaneCoordinate(GarageLayout::Wall wall)
	{
		switch (wall)
		{
			case GarageLayout::Wall::BACK:	return -GarageLayout::HALF_DEPTH;
			case GarageLayout::Wall::FRONT:	return GarageLayout::HALF_DEPTH;
			case GarageLayout::Wall::LEFT:	return -GarageLayout::HALF_WIDTH;
			default:						return GarageLayout::HALF_WIDTH;
		}
	}
}

std::optional<ShopWorld::PaintHit> ShopWorld::FindPaintHit(const FirstPersonController& player, float reach) const
{
	glm::vec3 origin = player.GetEyePosition();
	glm::vec3 direction = player.GetForward();

	// The nearest wall face, or the floor.
	std::optional<PaintHit> hit;
	auto consider = [&](float t, const WallPaint::Plane& plane) {
		if (t > 0.f && t <= reach && (!hit || t < hit->distance))
			hit = PaintHit{ plane, origin + direction * t, t };
	};
	for (const auto& wall : m_walls)
	{
		auto t = RayBox(origin, direction, wall.min, wall.max);
		if (!t)
			continue;
		// Which face it went in through: the one the point is on, facing back along the ray.
		glm::vec3 point = origin + direction * *t;
		WallPaint::Plane plane;
		float closest = 1e9f;
		for (int axis = 0; axis < 3; axis++)
			for (int facing : { -1, 1 })
			{
				float face = facing > 0 ? wall.max[axis] : wall.min[axis];
				float gap = std::abs(point[axis] - face);
				if (gap < closest && direction[axis] * facing < 0.f)
				{
					closest = gap;
					plane = { axis, facing, face };
				}
			}
		if (closest < 0.01f)
			consider(*t, plane);
	}
	if (direction.y < -1e-4f)
	{
		float t = -origin.y / direction.y;
		glm::vec3 point = origin + direction * t;
		ShopBox spot = { point - glm::vec3(0.01f, 0.f, 0.01f), point + glm::vec3(0.01f, 0.1f, 0.01f) };
		if (InsideWalls(spot))
			consider(t, { 1, 1, 0.f });
	}
	if (!hit)
		return std::nullopt;

	// Not on the glass of a window.
	for (const auto& window : GarageLayout::WINDOWS)
	{
		if (m_bayBuilt && window.wall == GarageLayout::Wall::LEFT)
			continue;
		bool alongX = window.wall == GarageLayout::Wall::BACK || window.wall == GarageLayout::Wall::FRONT;
		if (hit->plane.axis != (alongX ? 2 : 0) || std::abs(hit->plane.offset - WallPlaneCoordinate(window.wall)) > 0.01f)
			continue;
		float u = alongX ? hit->point.x : hit->point.z;
		if (u > window.u0 && u < window.u1 && hit->point.y > window.v0 && hit->point.y < window.v1)
			return std::nullopt;
	}

	// Nor through anything standing in the way: the equipment, what's lying about, deliveries.
	float limit = hit->distance - 0.01f;
	auto blocks = [&](const ShopBox& box) {
		auto t = RayBox(origin, direction, box.min, box.max);
		return t && *t < limit;
	};
	for (const auto& station : m_stations)
		if (station.present && station.object && station.object->IsVisible() && blocks(WorldBounds(station.object)))
			return std::nullopt;
	const Object* held = GetHeld() ? GetHeld()->object.get() : nullptr;
	for (const auto& [ref, visual] : m_items)
		if (visual.object.get() != held && visual.object->IsVisible() && visual.place != ItemPlace::HAND && blocks(WorldBounds(visual.object)))
			return std::nullopt;
	for (const auto& [id, package] : m_packages)
		if (package.object->IsVisible() && blocks(WorldBounds(package.object)))
			return std::nullopt;
	return hit;
}

int ShopWorld::PaintSizeIndex(const MeasuringTool& tool)
{
	return tool.handKind == HandToolKind::MARKER ? 0 : tool.handKind == HandToolKind::SPRAY_CAN ? 1 : 2;
}

bool ShopWorld::UpdatePaint(const FirstPersonController& player, const InputManager& input, size_t toolId, float deltaTime)
{
	m_paintCursor.reset();
	const auto& tool = ToolCatalog::Of(toolId);
	if (!tool.Paints())
	{
		m_lastPaint.reset();
		return false;
	}
	bool marker = tool.handKind == HandToolKind::MARKER;
	bool spray = tool.handKind == HandToolKind::SPRAY_CAN;
	// The scroll wheel sizes it (rather than stepping through the hotbar; see Update).
	int sizeIndex = PaintSizeIndex(tool);
	float& radius = m_paintRadius[sizeIndex];
	if (float scroll = input.GetScrollDelta(); std::abs(scroll) >= 0.5f)
		radius = std::clamp(radius * (scroll > 0.f ? PAINT_SIZE_STEP : 1.f / PAINT_SIZE_STEP), PAINT_SIZE_MIN[sizeIndex], PAINT_SIZE_MAX[sizeIndex]);
	radius = std::clamp(radius, PAINT_SIZE_MIN[sizeIndex], PAINT_SIZE_MAX[sizeIndex]);
	auto hit = FindPaintHit(player, PAINT_REACH);
	if (hit)
		m_paintCursor = PaintCursor{ hit->plane, hit->point, radius };

	// A part or tool aimed at takes a coat from a spray can, or has it wiped off with thinner.
	ItemVisual* item = nullptr;
	if (!marker && m_hover && m_hover->kind == TargetKind::ITEM)
		if (auto visual = Find(m_hover->item); visual && (!hit || glm::distance(player.GetEyePosition(), visual->object->GetPosition()) < hit->distance + 0.1f))
			item = visual;
	std::string itemName = item ? (item->part ? item->part->GetName() : ToolCatalog::Of(item->ref.id).name) : "";

	std::string size = radius < 0.01f ? std::format("{:.0f} mm", radius * 2000.f) : std::format("{:.0f} cm", radius * 200.f);
	if (item)
		m_prompt = spray ? std::format("[LMB] Spray the {}", itemName) : std::format("[LMB] Wipe the paint off the {}", itemName);
	else if (hit)
		m_prompt = std::format("{} ({} across)   [Wheel] Size", marker ? "[LMB] Draw" : spray ? "[LMB] Spray" : "[LMB] Wipe the paint off", size);
	else if (m_prompt.empty())
		m_prompt = marker ? "Aim at a wall or the floor to draw on it" : spray ? "Point it at a wall, the floor, or a part or tool"
			: "Aim at paint on a wall, the floor, or a part or tool";

	if (!Controls::Held(input, Control::PRIMARY))
	{
		m_lastPaint.reset();
		return false;
	}

	if (item)
	{
		m_lastPaint.reset();
		auto& coat = m_coats[item->ref];
		if (spray)
		{
			// The new colour takes over as much of the coat as is going on.
			float added = COAT_PER_SECOND * deltaTime;
			float after = (std::min)(coat.amount + added, 1.f);
			float share = coat.amount <= 0.f ? 1.f : (std::min)(added / after, 1.f);
			auto mix = [&](int shift) {
				float from = static_cast<float>((coat.rgb >> shift) & 0xFF), to = static_cast<float>((tool.paintColor >> shift) & 0xFF);
				return static_cast<uint32_t>(std::lround(from + (to - from) * share)) << shift;
			};
			coat.rgb = mix(16) | mix(8) | mix(0);
			coat.amount = after;
		}
		else
		{
			coat.amount -= STRIP_PER_SECOND * deltaTime;
			if (coat.amount <= 0.f)
				m_coats.erase(item->ref);
		}
		return true;
	}
	if (!hit)
	{
		m_lastPaint.reset();
		return true;
	}

	if (marker)
	{
		uint8_t color = WallPaint::NearestColor(tool.paintColor);
		// On from where it was, if it's still on the same face and hasn't jumped.
		// Far off, a small turn of the head is a long way along the wall: a jump that big is a new line.
		float jump = (std::max)(0.15f, hit->distance * 0.2f);
		if (m_lastPaint && m_lastPaint->first == hit->plane && glm::distance(m_lastPaint->second, hit->point) < jump)
			m_paint.Stroke(hit->plane, m_lastPaint->second, hit->point, radius, color);
		else
			m_paint.Dab(hit->plane, hit->point, radius, color);
		m_lastPaint = { hit->plane, hit->point };
	}
	else if (spray)
	{
		float texel = WallPaint::TILE / ShopTextures::PAINT_TILE_PIXELS;
		float texels = 3.14159f * radius * radius / (texel * texel);
		int speckles = (std::min)(static_cast<int>(texels * SPRAY_FILL_PER_SECOND * deltaTime) + 1, SPRAY_SPECKLES_MAX);
		m_paint.Dab(hit->plane, hit->point, radius, WallPaint::NearestColor(tool.paintColor), speckles);
	}
	else
		m_paint.Dab(hit->plane, hit->point, radius, 0);

	if (m_paint.IsFull())
		Toast("There's no room left for more paint: wipe some off with thinner first.");
	return true;
}
