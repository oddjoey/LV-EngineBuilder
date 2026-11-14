#include "Roads.h"

#include <algorithm>
#include <cmath>

#include "GarageLayout.h"

namespace
{
	using Points = std::vector<glm::vec2>;

	glm::vec2 CatmullRom(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3, float t)
	{
		float t2 = t * t, t3 = t2 * t;
		return 0.5f * (2.f * p1 + (p2 - p0) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 + (3.f * p1 - p0 - 3.f * p2 + p3) * t3);
	}

	// A smooth line through every one of these points, as a point every `step` metres along it (a loop comes back
	// round to its first point, which isn't given twice).
	Points Spline(const Points& through, bool loop, float step)
	{
		const int count = static_cast<int>(through.size());
		auto at = [&](int i) { return loop ? through[((i % count) + count) % count] : through[std::clamp(i, 0, count - 1)]; };
		Points fine;
		constexpr int PIECES = 48;
		for (int span = 0; span < (loop ? count : count - 1); span++)
			for (int piece = 0; piece < PIECES; piece++)
				fine.push_back(CatmullRom(at(span - 1), at(span), at(span + 1), at(span + 2), static_cast<float>(piece) / PIECES));
		fine.push_back(loop ? through.front() : through.back());

		float length = 0.f;
		for (size_t i = 0; i + 1 < fine.size(); i++)
			length += glm::distance(fine[i], fine[i + 1]);
		const int steps = (std::max)(1, static_cast<int>(std::round(length / step)));
		const float spacing = length / static_cast<float>(steps);
		Points even = { fine.front() };
		float along = 0.f, next = spacing;
		for (size_t i = 0; i + 1 < fine.size() && static_cast<int>(even.size()) < steps; i++)
		{
			float piece = glm::distance(fine[i], fine[i + 1]);
			while (next <= along + piece && static_cast<int>(even.size()) < steps)
			{
				even.push_back(glm::mix(fine[i], fine[i + 1], (next - along) / (std::max)(piece, 1e-6f)));
				next += spacing;
			}
			along += piece;
		}
		if (!loop)
			even.push_back(fine.back());
		return even;
	}

	// Which way a line runs at one of its points, and what's to the left of that.
	glm::vec2 Heading(const Points& line, bool loop, size_t i)
	{
		const size_t count = line.size();
		const glm::vec2& before = line[i > 0 ? i - 1 : loop ? count - 1 : 0];
		const glm::vec2& after = line[i + 1 < count ? i + 1 : loop ? 0 : count - 1];
		return glm::normalize(after - before);
	}

	// The same line, this far to its left (or its right, less than nothing).
	Points Beside(const Points& line, bool loop, float distance)
	{
		Points beside;
		for (size_t i = 0; i < line.size(); i++)
		{
			glm::vec2 heading = Heading(line, loop, i);
			beside.push_back(line[i] + glm::vec2(-heading.y, heading.x) * distance);
		}
		return beside;
	}

	Roads::Strip Make(Points line, bool loop, float halfWidth, float top, float medianSide = 0.f)
	{
		Roads::Strip strip;
		strip.loop = loop;
		strip.halfWidth = halfWidth;
		strip.top = top;
		strip.medianSide = medianSide;
		strip.min = strip.max = line.front();
		for (const auto& point : line)
		{
			strip.min = glm::min(strip.min, point - glm::vec2(halfWidth));
			strip.max = glm::max(strip.max, point + glm::vec2(halfWidth));
		}
		strip.line = std::move(line);
		return strip;
	}

	std::vector<Roads::Strip> Build()
	{
		using namespace GarageLayout;
		std::vector<Roads::Strip> strips;
		// The highway: along in front of the lot where it always ran (dead straight past the shops' drives), then round
		// to the right at each end and back behind the lot in long sweeping bends. A point every tile of its asphalt.
		const float middle = HIGHWAY_NEAR_Z + HIGHWAY_CARRIAGEWAY + HIGHWAY_MEDIAN * 0.5f;
		const Points ring = {
			{ -350.f, middle }, { -250.f, middle }, { -150.f, middle }, { -50.f, middle }, { 50.f, middle }, { 150.f, middle }, { 250.f, middle },
			{ 350.f, middle }, { 445.f, 28.f }, { 492.f, -50.f }, { 482.f, -150.f }, { 415.f, -240.f }, { 300.f, -292.f }, { 180.f, -262.f },
			{ 60.f, -300.f }, { -60.f, -362.f }, { -190.f, -352.f }, { -300.f, -292.f }, { -390.f, -190.f }, { -450.f, -90.f }, { -455.f, -5.f },
			{ -415.f, 40.f },
		};
		const Points centre = Spline(ring, true, HIGHWAY_CARRIAGEWAY / 3.f);
		// (It runs toward +x along the front: to its left there is +z, the far side.)
		const float out = (HIGHWAY_MEDIAN + HIGHWAY_CARRIAGEWAY) * 0.5f;
		strips.push_back(Make(Beside(centre, true, -out), true, HIGHWAY_CARRIAGEWAY * 0.5f, 0.016f, 1.f));
		strips.push_back(Make(Beside(centre, true, out), true, HIGHWAY_CARRIAGEWAY * 0.5f, 0.016f, -1.f));

		// The back roads, a lane each way, each ending on the highway's middle (or on another of them).
		constexpr float LANE = 3.5f;
		// Across behind the lot from the highway's west side to its east: esses, a pair of hairpins, and a fast run out.
		strips.push_back(Make(Spline({
			{ -450.f, -90.f }, { -390.f, -88.f }, { -330.f, -70.f }, { -280.f, -95.f }, { -230.f, -135.f }, { -170.f, -150.f }, { -120.f, -120.f },
			{ -60.f, -82.f }, { -10.f, -76.f }, { 16.f, -102.f }, { -6.f, -130.f }, { -60.f, -136.f }, { -96.f, -162.f }, { -80.f, -196.f },
			{ -30.f, -206.f }, { 60.f, -190.f }, { 150.f, -150.f }, { 230.f, -100.f }, { 300.f, -110.f }, { 380.f, -72.f }, { 440.f, -55.f },
			{ 492.f, -50.f } }, false, LANE), false, LANE, 0.006f));
		// A link from the highway's back straight up to it.
		strips.push_back(Make(Spline({ { 60.f, -300.f }, { 72.f, -262.f }, { 50.f, -226.f }, { 60.f, -190.f } }, false, LANE), false, LANE, 0.009f));
		// And a loop out over the far side of the front straight.
		strips.push_back(Make(Spline({ { 150.f, middle }, { 150.f, 95.f }, { 175.f, 140.f }, { 230.f, 165.f }, { 290.f, 150.f }, { 335.f, 110.f },
			{ 350.f, middle } }, false, LANE), false, LANE, 0.012f));
		return strips;
	}

	float DistanceToSegment(glm::vec2 point, glm::vec2 a, glm::vec2 b)
	{
		glm::vec2 along = b - a;
		float t = std::clamp(glm::dot(point - a, along) / (std::max)(glm::dot(along, along), 1e-8f), 0.f, 1.f);
		return glm::distance(point, a + along * t);
	}

	float DistanceToBox(glm::vec2 point, glm::vec2 boxMin, glm::vec2 boxMax)
	{
		glm::vec2 outside = glm::max(glm::max(boxMin - point, point - boxMax), glm::vec2(0.f));
		return glm::length(outside);
	}
}

const std::vector<Roads::Strip>& Roads::Strips()
{
	static const std::vector<Strip> strips = Build();
	return strips;
}

const Roads::Pad& Roads::SkidPan()
{
	static const Pad pan = { { 236.f, -182.f }, { 300.f, -113.f }, 0.003f };
	return pan;
}

float Roads::DistanceOff(glm::vec2 point)
{
	const Pad& pan = SkidPan();
	float nearest = DistanceToBox(point, pan.min, pan.max);
	for (const auto& strip : Strips())
	{
		// Not one that's nowhere near.
		if (DistanceToBox(point, strip.min, strip.max) >= nearest)
			continue;
		const size_t count = strip.line.size();
		for (size_t i = 0; i + 1 < count || (strip.loop && i < count); i++)
			nearest = (std::min)(nearest, DistanceToSegment(point, strip.line[i], strip.line[(i + 1) % count]) - strip.halfWidth);
	}
	return (std::max)(nearest, 0.f);
}

glm::vec2 Roads::Min()
{
	glm::vec2 low = SkidPan().min;
	for (const auto& strip : Strips())
		low = glm::min(low, strip.min);
	return low;
}

glm::vec2 Roads::Max()
{
	glm::vec2 high = SkidPan().max;
	for (const auto& strip : Strips())
		high = glm::max(high, strip.max);
	return high;
}
