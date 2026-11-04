#include "ServiceManual.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <tuple>
#include <utility>
#include <vector>

#include <imgui.h>

#include "../Simulation/EngineSpec.h"
#include "../Simulation/MeasuringTool.h"
#include "../Simulation/PartCatalog.h"

// The service manual's diagrams and graphs. They're drawn, not pictures: shapes and lines in the
// book's inks, and every graph plotted from the engine's own spec, so a figure never disagrees with
// the tables around it.

namespace
{
	constexpr float PI = 3.14159265f;

	ImU32 Shade(int r, int g, int b, int a = 255)
	{
		auto linear = [](int value) { return static_cast<int>(std::pow(value / 255.0, 2.2) * 255.0 + 0.5); };
		return IM_COL32(linear(r), linear(g), linear(b), a);
	}
	const ImU32 INK = Shade(30, 27, 24);
	const ImU32 INK_FAINT = Shade(120, 110, 96);
	const ImU32 ACCENT = Shade(28, 48, 100);
	const ImU32 METAL = Shade(178, 182, 188);
	const ImU32 METAL_DARK = Shade(120, 126, 134);
	const ImU32 FILL = Shade(222, 228, 238);
	const ImU32 OIL = Shade(214, 166, 50);
	const ImU32 GOOD = Shade(70, 140, 80);
	const ImU32 GOOD_FILL = Shade(70, 140, 80, 60);
	const ImU32 BAD = Shade(170, 52, 40);
	const ImU32 BAD_FILL = Shade(170, 52, 40, 45);
	const ImU32 GAUGE = Shade(60, 160, 80);		// Plastigauge green

	ImVec2 operator+(ImVec2 a, ImVec2 b) { return { a.x + b.x, a.y + b.y }; }
	ImVec2 operator-(ImVec2 a, ImVec2 b) { return { a.x - b.x, a.y - b.y }; }
	ImVec2 operator*(ImVec2 a, float s) { return { a.x * s, a.y * s }; }

	// Text placed by an anchor: align 0 puts pos at its left, 0.5 its middle, 1 its right.
	void Text(ImDrawList* drawList, ImVec2 pos, ImU32 color, const std::string& text, float size, float align = 0.f, float alignY = 0.f)
	{
		ImFont* font = ImGui::GetFont();
		ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.f, text.c_str());
		drawList->AddText(font, size, { pos.x - extent.x * align, pos.y - extent.y * alignY }, color, text.c_str());
	}

	void Arrowhead(ImDrawList* drawList, ImVec2 tip, ImVec2 from, ImU32 color, float size)
	{
		ImVec2 direction = tip - from;
		float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
		if (length < 1e-3f)
			return;
		direction = direction * (1.f / length);
		ImVec2 side = { -direction.y, direction.x };
		ImVec2 back = tip - direction * size;
		drawList->AddTriangleFilled(tip, back + side * (size * 0.45f), back - side * (size * 0.45f), color);
	}

	void Arrow(ImDrawList* drawList, ImVec2 from, ImVec2 to, ImU32 color, float size, float thickness = 1.2f)
	{
		drawList->AddLine(from, to, color, thickness);
		Arrowhead(drawList, to, from, color, size);
	}

	// A dimension: a line with arrows both ends.
	void Dimension(ImDrawList* drawList, ImVec2 a, ImVec2 b, ImU32 color, float size)
	{
		drawList->AddLine(a, b, color, 1.f);
		Arrowhead(drawList, a, b, color, size);
		Arrowhead(drawList, b, a, color, size);
	}

	void Dashed(ImDrawList* drawList, ImVec2 a, ImVec2 b, ImU32 color, float dash = 6.f, float thickness = 1.f)
	{
		ImVec2 delta = b - a;
		float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
		int pieces = (std::max)(1, static_cast<int>(length / dash));
		for (int i = 0; i < pieces; i += 2)
			drawList->AddLine(a + delta * (static_cast<float>(i) / pieces), a + delta * (static_cast<float>((std::min)(i + 1, pieces)) / pieces), color, thickness);
	}

	// A graph's frame: axes with ticks and labels, mapping its values onto the rectangle.
	struct Plot
	{
		ImVec2 min, max;
		float x0, x1, y0, y1;
		ImVec2 At(float x, float y) const
		{
			return { min.x + (x - x0) / (x1 - x0) * (max.x - min.x), max.y - (y - y0) / (y1 - y0) * (max.y - min.y) };
		}
	};

	Plot Axes(ImDrawList* drawList, ImVec2 min, ImVec2 max, float fontSize, float x0, float x1, float y0, float y1,
		const std::string& xLabel, const std::string& yLabel, float xStep, float yStep, const char* xFormat = "{:g}", const char* yFormat = "{:g}")
	{
		float label = fontSize * 0.8f;
		Plot plot = { { min.x + fontSize * 3.2f, min.y + fontSize * 0.6f }, { max.x - fontSize * 0.6f, max.y - fontSize * 2.4f }, x0, x1, y0, y1 };
		// Grid, then axes over it.
		for (float x = x0; x <= x1 + xStep * 0.01f; x += xStep)
		{
			ImVec2 at = plot.At(x, y0);
			drawList->AddLine({ at.x, plot.min.y }, at, Shade(214, 205, 182), 1.f);
			Text(drawList, { at.x, at.y + 3.f }, INK_FAINT, std::vformat(xFormat, std::make_format_args(x)), label, 0.5f);
		}
		for (float y = y0; y <= y1 + yStep * 0.01f; y += yStep)
		{
			ImVec2 at = plot.At(x0, y);
			drawList->AddLine(at, { plot.max.x, at.y }, Shade(214, 205, 182), 1.f);
			Text(drawList, { at.x - 4.f, at.y }, INK_FAINT, std::vformat(yFormat, std::make_format_args(y)), label, 1.f, 0.5f);
		}
		drawList->AddLine(plot.At(x0, y0), plot.At(x1, y0), INK, 1.5f);
		drawList->AddLine(plot.At(x0, y0), plot.At(x0, y1), INK, 1.5f);
		Text(drawList, { (plot.min.x + plot.max.x) * 0.5f, plot.max.y + fontSize * 1.15f }, INK, xLabel, label, 0.5f);
		Text(drawList, { plot.min.x + 4.f, plot.min.y }, INK, yLabel, label, 0.f);
		return plot;
	}

	// Smallest convex outline round a set of points (for a belt round its pulleys).
	std::vector<ImVec2> Hull(std::vector<ImVec2> points)
	{
		std::sort(points.begin(), points.end(), [](ImVec2 a, ImVec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
		auto cross = [](ImVec2 o, ImVec2 a, ImVec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
		std::vector<ImVec2> hull(points.size() * 2);
		size_t k = 0;
		for (size_t i = 0; i < points.size(); i++)
		{
			while (k >= 2 && cross(hull[k - 2], hull[k - 1], points[i]) <= 0.f)
				k--;
			hull[k++] = points[i];
		}
		for (size_t i = points.size() - 1, t = k + 1; i > 0; i--)
		{
			while (k >= t && cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 0.f)
				k--;
			hull[k++] = points[i - 1];
		}
		hull.resize(k > 0 ? k - 1 : 0);
		return hull;
	}
}

float ServiceManual::FigureLines(Figure figure)
{
	switch (figure)
	{
		case Figure::TOOL_RESOLUTION:	return 15.f;
		case Figure::SPRING_CURVE:
		case Figure::CAM_PROFILE:
		case Figure::COMPRESSION_GRAPH:
		case Figure::TORQUE_ACCURACY:	return 13.f;
		case Figure::MAIN_CAP_SEQUENCE:	return 7.f;
		case Figure::ASSEMBLY_FLOW:		return 12.f;
		default:						return 11.f;
	}
}

void ServiceManual::DrawFigure(ImDrawList* drawList, Figure figure, const ImVec2& min, const ImVec2& max, float fontSize) const
{
	const EngineSpec& spec = EngineSpec::InlineFour();
	bool imperial = m_builtImperial;
	float width = max.x - min.x, height = max.y - min.y;
	ImVec2 centre = { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f };
	float label = fontSize * 0.85f;
	float arrow = fontSize * 0.45f;
	// Small differences as the book prints them: millimetres, or thousandths of an inch.
	auto fine = [&](double mm) { return imperial ? std::format("{:.1f} thou", mm / 0.0254) : std::format("{:.3f} mm", mm); };
	auto fineRange = [&](const Range& r) { return imperial ? std::format("{:.1f} - {:.1f} thou", r.min / 0.0254, r.max / 0.0254)
		: std::format("{:.3f} - {:.3f} mm", r.min, r.max); };

	switch (figure)
	{
		case Figure::TOOL_RESOLUTION:
		{
			// How finely each measuring tool reads, on a log scale, against the tightest tolerance in the
			// book: anything to the right of the line can't tell a good bore from a bad one.
			std::vector<const MeasuringTool*> tools;
			for (const auto& tool : ToolCatalog::All())
				if (!tool.IsHandTool() && !tool.manual && tool.resolution > 0.0 && !tool.measuresFinish
						&& std::find(tool.kinds.begin(), tool.kinds.end(), FeatureKind::FORCE) == tool.kinds.end()
						&& std::find(tool.kinds.begin(), tool.kinds.end(), FeatureKind::VOLUME) == tool.kinds.end())
					tools.push_back(&tool);
			std::sort(tools.begin(), tools.end(), [](const MeasuringTool* a, const MeasuringTool* b) { return a->resolution < b->resolution; });
			float names = fontSize * 13.f;
			float rowHeight = (height - fontSize * 2.6f) / static_cast<float>(tools.size());
			float left = min.x + names, right = max.x - fontSize;
			auto x = [&](double mm) { return left + std::clamp(static_cast<float>((std::log10(mm) + 3.5) / 2.5), 0.f, 1.f) * (right - left); };	// 0.0003 to 0.1 mm
			for (size_t i = 0; i < tools.size(); i++)
			{
				float y = min.y + rowHeight * i;
				Text(drawList, { left - 6.f, y + rowHeight * 0.5f }, INK, tools[i]->name, label * 0.92f, 1.f, 0.5f);
				bool fineEnough = tools[i]->resolution < spec.bore.Width();
				drawList->AddRectFilled({ left, y + rowHeight * 0.18f }, { x(tools[i]->resolution), y + rowHeight * 0.82f }, fineEnough ? GOOD : BAD, 2.f);
			}
			float limit = x(spec.bore.Width());
			float bottom = min.y + rowHeight * tools.size();
			Dashed(drawList, { limit, min.y }, { limit, bottom }, ACCENT, 5.f, 1.5f);
			Text(drawList, { limit + 4.f, min.y }, ACCENT, "Bore tolerance " + fine(spec.bore.Width()), label);
			for (double tick : { 0.001, 0.01, 0.1 })
			{
				Text(drawList, { x(tick), bottom + 3.f }, INK_FAINT, fine(tick), label * 0.9f, 0.5f);
				drawList->AddLine({ x(tick), bottom }, { x(tick), bottom + 3.f }, INK_FAINT);
			}
			Text(drawList, { (left + right) * 0.5f, bottom + fontSize * 1.3f }, INK, "Resolution (shorter is finer)", label, 0.5f);
			break;
		}
		case Figure::BORE_POINTS:
		{
			// Left: the bore cut through, the ring travel shaded, the three heights measured at. Right: from
			// above, the two directions.
			float side = width * 0.45f;
			ImVec2 wallTop = { min.x + side * 0.25f, min.y + fontSize * 0.5f };
			float boreWidth = side * 0.5f, boreHeight = height - fontSize * 1.5f;
			drawList->AddRectFilled({ wallTop.x - 14.f, wallTop.y }, { wallTop.x, wallTop.y + boreHeight }, METAL);
			drawList->AddRectFilled({ wallTop.x + boreWidth, wallTop.y }, { wallTop.x + boreWidth + 14.f, wallTop.y + boreHeight }, METAL);
			drawList->AddRectFilled({ wallTop.x, wallTop.y + boreHeight * 0.06f }, { wallTop.x + boreWidth, wallTop.y + boreHeight * 0.7f }, FILL);
			Text(drawList, { wallTop.x + boreWidth * 0.5f, wallTop.y + boreHeight * 0.38f }, INK_FAINT, "ring travel", label, 0.5f, 0.5f);
			const char* labels[] = { "A  top", "B  middle", "C  bottom" };
			float heights[] = { 0.1f, 0.38f, 0.66f };
			for (int i = 0; i < 3; i++)
			{
				float y = wallTop.y + boreHeight * heights[i];
				Dimension(drawList, { wallTop.x + 2.f, y }, { wallTop.x + boreWidth - 2.f, y }, ACCENT, arrow);
				Text(drawList, { wallTop.x + boreWidth + 20.f, y }, INK, labels[i], label, 0.f, 0.5f);
			}
			ImVec2 top = { min.x + width * 0.75f, centre.y - fontSize * 0.5f };
			float radius = (std::min)(width * 0.16f, height * 0.36f);
			drawList->AddCircleFilled(top, radius + 12.f, METAL, 48);
			drawList->AddCircleFilled(top, radius, Shade(244, 238, 222), 48);
			Dimension(drawList, { top.x - radius + 2.f, top.y }, { top.x + radius - 2.f, top.y }, ACCENT, arrow);
			Dimension(drawList, { top.x, top.y - radius + 2.f }, { top.x, top.y + radius - 2.f }, ACCENT, arrow);
			Text(drawList, { top.x + radius * 0.5f, top.y - 4.f }, INK, "X", label, 0.5f, 1.f);
			Text(drawList, { top.x + 5.f, top.y - radius * 0.5f }, INK, "Y", label, 0.f, 0.5f);
			Text(drawList, { top.x, top.y + radius + 16.f }, INK_FAINT, "Taper: A - C.  Out of round: X - Y.", label, 0.5f);
			break;
		}
		case Figure::JOURNAL_POINTS:
		{
			// The crank side on, a journal between two webs, measured at both ends; and end on, at two angles.
			float y = centre.y - fontSize * 0.5f;
			float shaftLeft = min.x + width * 0.04f, shaftRight = min.x + width * 0.6f;
			drawList->AddRectFilled({ shaftLeft, y - height * 0.08f }, { shaftRight, y + height * 0.08f }, METAL_DARK);
			float journalLeft = shaftLeft + (shaftRight - shaftLeft) * 0.3f, journalRight = shaftLeft + (shaftRight - shaftLeft) * 0.7f;
			drawList->AddRectFilled({ journalLeft - 16.f, y - height * 0.38f }, { journalLeft, y + height * 0.38f }, METAL_DARK);
			drawList->AddRectFilled({ journalRight, y - height * 0.38f }, { journalRight + 16.f, y + height * 0.38f }, METAL_DARK);
			drawList->AddRectFilled({ journalLeft, y - height * 0.2f }, { journalRight, y + height * 0.2f }, METAL);
			float planes[] = { journalLeft + (journalRight - journalLeft) * 0.2f, journalLeft + (journalRight - journalLeft) * 0.8f };
			for (int i = 0; i < 2; i++)
			{
				Dashed(drawList, { planes[i], y - height * 0.3f }, { planes[i], y + height * 0.3f }, ACCENT, 4.f, 1.5f);
				Text(drawList, { planes[i], y - height * 0.32f }, INK, i == 0 ? "1" : "2", label, 0.5f, 1.f);
			}
			ImVec2 end = { min.x + width * 0.82f, y };
			float radius = height * 0.25f;
			drawList->AddCircleFilled(end, radius, METAL, 40);
			Dimension(drawList, { end.x - radius, end.y }, { end.x + radius, end.y }, ACCENT, arrow);
			Dimension(drawList, { end.x, end.y - radius }, { end.x, end.y + radius }, ACCENT, arrow);
			Text(drawList, { end.x + radius + 4.f, end.y }, INK, "A", label, 0.f, 0.5f);
			Text(drawList, { end.x, end.y + radius * 1.05f }, INK, "B", label);
			Text(drawList, { centre.x, max.y - fontSize * 0.2f }, INK_FAINT, "Taper: 1 - 2.  Out of round: A - B, at each end.", label, 0.5f, 1.f);
			break;
		}
		case Figure::PISTON_CLEARANCE:
		{
			// The bore and a piston in it, the gap down each side exaggerated, with the two diameters.
			float boreLeft = min.x + width * 0.08f, boreRight = min.x + width * 0.5f;
			float top = min.y + fontSize * 1.6f, bottom = max.y - fontSize * 0.4f;
			drawList->AddRectFilled({ boreLeft - 16.f, top }, { boreLeft, bottom }, METAL);
			drawList->AddRectFilled({ boreRight, top }, { boreRight + 16.f, bottom }, METAL);
			float gap = 9.f;
			ImVec2 pistonMin = { boreLeft + gap, top + height * 0.25f }, pistonMax = { boreRight - gap, bottom - height * 0.05f };
			drawList->AddRectFilled(pistonMin, pistonMax, FILL, 4.f);
			drawList->AddRect(pistonMin, pistonMax, ACCENT, 4.f, 0, 1.5f);
			for (int ring = 0; ring < 3; ring++)
				drawList->AddLine({ pistonMin.x, pistonMin.y + 8.f + ring * 7.f }, { pistonMax.x, pistonMin.y + 8.f + ring * 7.f }, ACCENT);
			drawList->AddRectFilled({ boreLeft, pistonMin.y }, { pistonMin.x, pistonMax.y }, OIL);
			drawList->AddRectFilled({ pistonMax.x, pistonMin.y }, { boreRight, pistonMax.y }, OIL);
			Dimension(drawList, { boreLeft, top - fontSize * 0.6f }, { boreRight, top - fontSize * 0.6f }, ACCENT, arrow);
			Text(drawList, { (boreLeft + boreRight) * 0.5f, top - fontSize * 0.8f }, INK, "Bore", label, 0.5f, 1.f);
			float skirt = pistonMax.y - height * 0.15f;
			Dimension(drawList, { pistonMin.x, skirt }, { pistonMax.x, skirt }, ACCENT, arrow);
			Text(drawList, { (boreLeft + boreRight) * 0.5f, skirt - 3.f }, INK, "Skirt", label, 0.5f, 1.f);
			Arrow(drawList, { boreRight + 40.f, pistonMin.y + height * 0.2f }, { boreRight - gap * 0.5f, pistonMin.y + height * 0.25f }, INK, arrow);
			Text(drawList, { boreRight + 42.f, pistonMin.y + height * 0.2f }, INK, "Clearance", label, 0.f, 1.f);
			Text(drawList, { boreRight + 42.f, pistonMin.y + height * 0.2f + 2.f }, INK_FAINT, "(half each side)", label);
			Text(drawList, { boreRight + 42.f, pistonMin.y + height * 0.2f + fontSize * 1.1f }, GOOD, fineRange(spec.PistonToWallFor(MaterialType::CAST_ALUMINUM)) + " (cast)", label);
			break;
		}
		case Figure::RING_GAP:
		{
			// From above: a ring squared in its bore, its ends a feeler blade apart.
			ImVec2 middle = { min.x + width * 0.36f, centre.y };
			float radius = height * 0.42f;
			drawList->AddCircleFilled(middle, radius + 14.f, METAL, 64);
			drawList->AddCircleFilled(middle, radius, Shade(244, 238, 222), 64);
			float gapAngle = 0.12f;
			drawList->PathArcTo(middle, radius - 5.f, -PI * 0.5f + gapAngle, PI * 1.5f - gapAngle, 64);
			drawList->PathStroke(ACCENT, 0, 7.f);
			ImVec2 blade = { middle.x, middle.y - radius + 5.f };
			drawList->AddRectFilled({ blade.x - 3.f, blade.y - 22.f }, { blade.x + 3.f, blade.y + 12.f }, GOOD);
			Arrow(drawList, { middle.x + radius * 0.9f, min.y + fontSize * 0.4f }, { blade.x + 5.f, blade.y - 6.f }, INK, arrow);
			Text(drawList, { middle.x + radius * 0.92f, min.y + fontSize * 0.4f }, INK, "Feeler blade in the gap", label, 0.f, 0.5f);
			float note = min.x + width * 0.66f;
			Text(drawList, { note, centre.y - fontSize * 0.8f }, INK, "End gap", label);
			Text(drawList, { note, centre.y + fontSize * 0.3f }, GOOD, fineRange(spec.ringGap), label);
			Text(drawList, { note, centre.y + fontSize * 1.6f }, INK_FAINT, "Too tight: file it open.", label);
			Text(drawList, { note, centre.y + fontSize * 2.6f }, INK_FAINT, "Too wide: a new ring.", label);
			break;
		}
		case Figure::OIL_CLEARANCE:
		{
			// End on: the housing, the two shells, the oil film (exaggerated) and the journal.
			ImVec2 middle = { min.x + width * 0.32f, centre.y };
			float radius = height * 0.44f;
			drawList->AddCircleFilled(middle, radius, METAL_DARK, 64);
			drawList->AddCircleFilled(middle, radius * 0.86f, OIL, 64);
			drawList->AddCircleFilled(middle, radius * 0.8f, Shade(244, 238, 222), 64);
			drawList->AddCircleFilled(middle, radius * 0.72f, METAL, 64);
			drawList->AddLine({ middle.x - radius, middle.y }, { middle.x + radius, middle.y }, INK_FAINT, 1.f);
			struct Callout { float radius; float angle; const char* text; };
			const Callout callouts[] = {
				{ 0.93f, -0.6f, "Housing bore (block and cap)" },
				{ 0.83f, -0.2f, "Bearing shells (upper and lower)" },
				{ 0.76f, 0.2f, "Oil clearance" },
				{ 0.4f, 0.6f, "Journal" },
			};
			float textX = min.x + width * 0.62f;
			for (int i = 0; i < 4; i++)
			{
				const auto& callout = callouts[i];
				ImVec2 point = { middle.x + std::cos(callout.angle) * radius * callout.radius, middle.y + std::sin(callout.angle) * radius * callout.radius };
				ImVec2 anchor = { textX - 6.f, min.y + height * (0.15f + i * 0.22f) };
				drawList->AddLine(point, anchor, INK, 1.f);
				drawList->AddCircleFilled(point, 2.5f, INK);
				Text(drawList, { textX, anchor.y }, INK, callout.text, label, 0.f, 0.5f);
			}
			Text(drawList, { textX, max.y - fontSize * 0.1f }, GOOD, "Target " + fineRange(spec.mainClearance), label, 0.f, 1.f);
			break;
		}
		case Figure::PLASTIGAUGE_SCALE:
		{
			// The scale printed on the packet: the wider the squashed thread, the tighter the clearance.
			double marks[] = { 0.025, 0.038, 0.051, 0.064, 0.076 };
			float column = width / 7.f;
			float barTop = min.y + fontSize * 0.6f, barBottom = max.y - fontSize * 3.f;
			for (int i = 0; i < 5; i++)
			{
				float x = min.x + column * (i + 1);
				float barWidth = column * 0.55f * static_cast<float>(0.025 / marks[i]);
				drawList->AddRectFilled({ x - barWidth * 0.5f, barTop }, { x + barWidth * 0.5f, barBottom }, GAUGE, 2.f);
				bool inSpec = spec.mainClearance.Contains(marks[i]);
				Text(drawList, { x, barBottom + 4.f }, inSpec ? INK : BAD, fine(marks[i]), label * 0.9f, 0.5f);
			}
			// A strip off a journal, laid against the scale.
			float stripX = min.x + column * 6.2f;
			float stripWidth = column * 0.55f * static_cast<float>(0.025 / 0.038);
			drawList->AddRectFilled({ stripX - stripWidth * 0.5f, barTop + 10.f }, { stripX + stripWidth * 0.5f, barBottom - 10.f }, GAUGE, 4.f);
			Text(drawList, { stripX, barBottom + 4.f }, ACCENT, "Your strip", label * 0.9f, 0.5f);
			//Dashed(drawList, { min.x + column * 2.f + stripWidth * 0.5f, barTop + 4.f }, { stripX - stripWidth * 0.5f, barTop + 4.f }, ACCENT);
			Text(drawList, { centre.x, max.y - fontSize * 0.2f }, INK_FAINT, "Match the widest point of the strip to the scale: this one reads " + fine(0.038) + ".", label, 0.5f, 1.f);
			break;
		}
		case Figure::VALVE_DIMENSIONS:
		{
			// A valve in its guide, cut through the head, with its spring and retainer; the dimensions
			// that matter called out.
			float x = min.x + width * 0.3f;
			float seatY = max.y - fontSize * 2.2f, retainerY = min.y + fontSize * 1.2f;
			float head = width * 0.11f, stem = 5.f;
			// The head casting round the port and the seat.
			drawList->AddRectFilled({ min.x + width * 0.04f, seatY - 6.f }, { x - head - 6.f, max.y - fontSize * 0.3f }, METAL);
			drawList->AddRectFilled({ x + head + 6.f, seatY - 6.f }, { min.x + width * 0.56f, max.y - fontSize * 0.3f }, METAL);
			drawList->AddRectFilled({ x - stem - 7.f, retainerY + height * 0.3f }, { x + stem + 7.f, seatY - 18.f }, METAL_DARK);	// the guide
			// The valve: stem, then the head with its 45 degree face and margin.
			drawList->AddRectFilled({ x - stem, retainerY - 8.f }, { x + stem, seatY - 8.f }, INK_FAINT);
			ImVec2 face[] = { { x - stem, seatY - 18.f }, { x + stem, seatY - 18.f }, { x + head, seatY - 6.f }, { x + head, seatY }, { x - head, seatY }, { x - head, seatY - 6.f } };
			drawList->AddConvexPolyFilled(face, 6, INK_FAINT);
			drawList->AddLine({ x + head - 8.f, seatY - 8.f }, { x + head + 4.f, seatY - 8.f }, GOOD, 3.f);	// the seat contact
			// Spring between the head's spring seat and the retainer.
			float springTop = retainerY + 6.f, springBottom = retainerY + height * 0.3f - 2.f;
			for (int turn = 0; turn < 7; turn++)
			{
				float y0 = springTop + (springBottom - springTop) * turn / 7.f, y1 = springTop + (springBottom - springTop) * (turn + 1) / 7.f;
				drawList->AddLine({ x - head * 0.7f, y0 }, { x + head * 0.7f, (y0 + y1) * 0.5f }, ACCENT, 2.f);
				drawList->AddLine({ x + head * 0.7f, (y0 + y1) * 0.5f }, { x - head * 0.7f, y1 }, ACCENT, 2.f);
			}
			drawList->AddRectFilled({ x - head * 0.85f, retainerY }, { x + head * 0.85f, retainerY + 6.f }, METAL_DARK);
			// Installed height: spring seat to the underside of the retainer.
			float dimX = x - head - 18.f;
			Dimension(drawList, { dimX, retainerY + 6.f }, { dimX, springBottom }, ACCENT, arrow);
			Text(drawList, { dimX - 4.f, (retainerY + springBottom) * 0.5f }, INK, "Installed", label, 1.f, 1.f);
			Text(drawList, { dimX - 4.f, (retainerY + springBottom) * 0.5f }, INK, "height", label, 1.f, 0.f);
			// Callouts down the right.
			float textX = min.x + width * 0.6f;
			struct Callout { ImVec2 point; std::string text; };
			const Callout callouts[] = {
				{ { x + stem, retainerY + height * 0.2f }, "Stem diameter (to the guide)" },
				{ { x + stem + 7.f, retainerY + height * 0.45f }, "Guide bore" },
				{ { x + head, seatY - 3.f }, "Margin: min " + fine(spec.minValveMargin) },
				{ { x + head - 2.f, seatY - 8.f }, "Seat width, 45 degrees" },
			};
			for (int i = 0; i < 4; i++)
			{
				ImVec2 anchor = { textX - 6.f, min.y + height * (0.12f + i * 0.24f) };
				drawList->AddLine(callouts[i].point, anchor, INK, 1.f);
				Text(drawList, { textX, anchor.y }, INK, callouts[i].text, label, 0.f, 0.5f);
			}
			break;
		}
		case Figure::SPRING_CURVE:
		{
			// Force against height: a straight line from nothing at the free length, rising by the rate.
			// The installed height window and the seat pressure window cross in the box a good spring
			// passes through.
			float free = static_cast<float>(spec.springFreeLength.nominal);
			float rate = static_cast<float>(spec.springRate);
			float scale = imperial ? 1.f / 25.4f : 1.f;
			float x0 = (free - 10.f) * scale, x1 = (free + 0.5f) * scale;
			Plot plot = Axes(drawList, min, max, fontSize, x0, x1, 0.f, 500.f, imperial ? "Spring height (in)" : "Spring height (mm)", "Force (N)",
				imperial ? 0.1f : 2.f, 100.f, imperial ? "{:.1f}" : "{:g}");
			ImVec2 windowMin = plot.At(static_cast<float>(spec.installedHeightRange.min) * scale, static_cast<float>(spec.seatPressure.max));
			ImVec2 windowMax = plot.At(static_cast<float>(spec.installedHeightRange.max) * scale, static_cast<float>(spec.seatPressure.min));
			drawList->AddRectFilled(windowMin, windowMax, GOOD_FILL);
			drawList->AddRect(windowMin, windowMax, GOOD, 0.f, 0, 1.5f);
			drawList->AddLine(plot.At(free * scale, 0.f), plot.At((free - 10.f) * scale, rate * 10.f), ACCENT, 2.5f);
			// A tired spring: shorter free length, same rate.
			Dashed(drawList, plot.At((free - 1.5f) * scale, 0.f), plot.At((free - 10.f) * scale, rate * 8.5f), BAD, 6.f, 2.f);
			Text(drawList, { windowMax.x + 6.f, windowMin.y }, GOOD, "Installed: in spec", label);
			Text(drawList, plot.At((free - 9.5f) * scale, rate * 8.f) + ImVec2(8.f, 0.f), BAD, "Tired spring", label);
			Text(drawList, plot.At(free * scale, 0.f) + ImVec2(-4.f, -fontSize * 1.3f), ACCENT, "Free length", label, 1.f);
			break;
		}
		case Figure::CAM_PROFILE:
		{
			// Valve lift through a turn of the cam: the lobe's flank, nose and flank, with a worn lobe
			// under it.
			float lift = static_cast<float>(spec.lobeLift.nominal) * (imperial ? 1.f / 25.4f : 1.f);
			Plot plot = Axes(drawList, min, max, fontSize, 0.f, 360.f, 0.f, lift * 1.25f, "Cam angle (degrees)", imperial ? "Lift (in)" : "Lift (mm)",
				60.f, imperial ? 0.1f : 2.f, "{:g}", imperial ? "{:.1f}" : "{:g}");
			auto curve = [&](float peak, ImU32 color, bool dashed) {
				std::vector<ImVec2> points;
				for (int i = 0; i <= 120; i++)
				{
					float angle = i * 3.f;
					float t = (angle - 180.f) / 115.f;
					float y = std::abs(t) < 1.f ? peak * std::pow(0.5f * (1.f + std::cos(PI * t)), 1.3f) : 0.f;
					points.push_back(plot.At(angle, y));
				}
				if (dashed)
					for (size_t i = 0; i + 1 < points.size(); i += 2)
						drawList->AddLine(points[i], points[i + 1], color, 2.f);
				else
					drawList->AddPolyline(points.data(), static_cast<int>(points.size()), color, 0, 2.5f);
			};
			curve(lift, ACCENT, false);
			curve(lift * 0.9f, BAD, true);
			ImVec2 nose = plot.At(180.f, lift);
			Text(drawList, nose + ImVec2(8.f, -fontSize * 0.2f), ACCENT, "Lobe lift " + (imperial ? std::format("{:.3f} in", lift) : std::format("{:.2f} mm", lift)), label);
			Text(drawList, plot.At(250.f, lift * 0.55f), BAD, "Worn lobe: less air", label);
			break;
		}
		case Figure::ASSEMBLY_FLOW:
		{
			// The build in eight stages, snaking down the page.
			const char* stages[] = { "Block, core plugs,\nupper main shells", "Crank, lower shells,\nmain caps torqued", "Pistons, rings,\nrods and rod caps",
				"Head gasket,\nhead, head bolts", "Valves, springs,\ncamshaft, rockers", "Sprockets, belt,\ntensioner",
				"Covers, pan, intake,\nexhaust, cooling", "Dyno pull" };
			int columns = 4;
			float boxWidth = width / columns - fontSize * 0.9f, boxHeight = height / 2.f - fontSize * 2.f;
			ImVec2 centres[8];
			for (int i = 0; i < 8; i++)
			{
				int row = i / columns, column = row == 0 ? i % columns : columns - 1 - i % columns;
				centres[i] = { min.x + (column + 0.5f) * width / columns, min.y + (row + 0.5f) * height / 2.f };
			}
			for (int i = 0; i + 1 < 8; i++)
			{
				ImVec2 from = centres[i], to = centres[i + 1];
				if (from.y == to.y)
				{
					float direction = to.x > from.x ? 1.f : -1.f;
					Arrow(drawList, { from.x + direction * boxWidth * 0.5f, from.y }, { to.x - direction * boxWidth * 0.5f, to.y }, ACCENT, arrow, 2.f);
				}
				else
					Arrow(drawList, { from.x, from.y + boxHeight * 0.5f }, { to.x, to.y - boxHeight * 0.5f }, ACCENT, arrow, 2.f);
			}
			for (int i = 0; i < 8; i++)
			{
				ImVec2 boxMin = { centres[i].x - boxWidth * 0.5f, centres[i].y - boxHeight * 0.5f }, boxMax = { centres[i].x + boxWidth * 0.5f, centres[i].y + boxHeight * 0.5f };
				drawList->AddRectFilled(boxMin, boxMax, i == 7 ? GOOD_FILL : FILL, 5.f);
				drawList->AddRect(boxMin, boxMax, i == 7 ? GOOD : ACCENT, 5.f, 0, 1.5f);
				Text(drawList, { boxMin.x + 6.f, boxMin.y + 4.f }, ACCENT, std::to_string(i + 1), label);
				Text(drawList, centres[i] + ImVec2(0.f, 4.f), INK, stages[i], label * 0.82f, 0.5f, 0.5f);
			}
			break;
		}
		case Figure::BELT_ROUTING:
		{
			// From the front: the crank sprocket at the bottom, the cam sprocket (twice its size) at the
			// top, the idler and tensioner, and the belt round the lot. The marks line up at TDC.
			float unit = (std::min)(width * 0.5f, height) / 10.f;
			ImVec2 crank = { centre.x - unit * 2.f, max.y - unit * 2.f }, cam = { centre.x - unit * 1.f, min.y + unit * 3.f };
			ImVec2 idler = { centre.x + unit * 2.f, centre.y + unit * 0.5f }, tensioner = { centre.x - unit * 3.6f, centre.y + unit * 0.5f };
			float crankRadius = unit * 1.3f, camRadius = unit * 2.6f, idlerRadius = unit * 0.8f;
			std::vector<ImVec2> rim;
			auto sample = [&](ImVec2 c, float r) { for (int i = 0; i < 36; i++) rim.push_back(c + ImVec2(std::cos(i * PI / 18.f), std::sin(i * PI / 18.f)) * (r + 3.f)); };
			sample(crank, crankRadius);
			sample(cam, camRadius);
			sample(idler, idlerRadius);
			sample(tensioner, idlerRadius);
			auto belt = Hull(rim);
			drawList->AddPolyline(belt.data(), static_cast<int>(belt.size()), Shade(40, 40, 44), ImDrawFlags_Closed, 5.f);
			for (auto [where, r, name] : { std::tuple{ crank, crankRadius, "Crank" }, std::tuple{ cam, camRadius, "Cam" }, std::tuple{ idler, idlerRadius, "Idler" },
				std::tuple{ tensioner, idlerRadius, "Tensioner" } })
			{
				drawList->AddCircleFilled(where, r, METAL, 40);
				drawList->AddCircle(where, r, METAL_DARK, 40, 1.5f);
				drawList->AddCircleFilled(where, r * 0.2f, METAL_DARK, 16);
				bool leftSide = where.x < centre.x - unit * 2.5f;
				Text(drawList, where + ImVec2(leftSide ? -(r + 8.f) : r + 8.f, 0.f), INK, name, label, leftSide ? 1.f : 0.f, 0.5f);
			}
			// Timing marks: a notch on each sprocket against a pointer on the casting.
			for (auto [where, r] : { std::pair{ crank, crankRadius }, std::pair{ cam, camRadius } })
			{
				ImVec2 notch = { where.x, where.y - r };
				drawList->AddTriangleFilled(notch + ImVec2(-5.f, 0.f), notch + ImVec2(5.f, 0.f), notch + ImVec2(0.f, 8.f), BAD);
				drawList->AddTriangleFilled(notch + ImVec2(-5.f, -14.f), notch + ImVec2(5.f, -14.f), notch + ImVec2(0.f, -6.f), BAD);
			}
			Text(drawList, { max.x - fontSize * 0.5f, min.y + fontSize * 0.3f }, BAD, "Marks lined up:", label, 1.f);
			Text(drawList, { max.x - fontSize * 0.5f, min.y + fontSize * 1.3f }, BAD, "No. 1 at TDC", label, 1.f);
			Text(drawList, { max.x - fontSize * 0.5f, max.y - fontSize * 1.1f }, INK_FAINT, "Cam turns once", label, 1.f, 1.f);
			Text(drawList, { max.x - fontSize * 0.5f, max.y - fontSize * 0.1f }, INK_FAINT, "for two of the crank", label, 1.f, 1.f);
			break;
		}
		case Figure::TORQUE_ACCURACY:
		{
			// Where a head bolt really ends up with each torque wrench: nineteen pulls in twenty land in its bar.
			double target = 0.0;
			for (const auto& holes : PartCatalog::Holes())
				if (std::string(holes.name) == "Head Bolt")
					target = holes.torqueNm;
			std::vector<const MeasuringTool*> wrenches;
			for (const auto& tool : ToolCatalog::All())
				if (tool.handKind == HandToolKind::TORQUE_WRENCH)
					wrenches.push_back(&tool);
			float names = fontSize * 11.f;
			Plot plot = Axes(drawList, { min.x + names - fontSize * 3.2f, min.y }, max, fontSize, std::floor(static_cast<float>(target) * 0.07f) * 10.f, std::ceil(static_cast<float>(target) * 0.13f) * 10.f,
				0.f, static_cast<float>(wrenches.size()), "Torque reached on a " + std::format("{:.0f}", target) + " Nm head bolt (Nm)", "", 10.f, 1.f, "{:.0f}", "");
			for (size_t i = 0; i < wrenches.size(); i++)
			{
				float row = static_cast<float>(wrenches.size() - i) - 0.5f;
				double spread = 2.0 * wrenches[i]->torqueError;
				ImVec2 a = plot.At(static_cast<float>(target * (1.0 - spread)), row + 0.25f), b = plot.At(static_cast<float>(target * (1.0 + spread)), row - 0.25f);
				drawList->AddRectFilled(a, b, wrenches[i]->torqueError <= 0.04 ? GOOD : OIL, 3.f);
				Text(drawList, { plot.min.x - 8.f, (a.y + b.y) * 0.5f }, INK, wrenches[i]->name, label, 1.f, 0.5f);
				Text(drawList, { b.x + 6.f, (a.y + b.y) * 0.5f }, INK_FAINT, std::format("+/- {:.0f}%", spread * 100.0), label, 0.f, 0.5f);
			}
			ImVec2 specTop = plot.At(static_cast<float>(target), static_cast<float>(wrenches.size())), specBottom = plot.At(static_cast<float>(target), 0.f);
			drawList->AddLine(specTop, specBottom, ACCENT, 2.f);
			Text(drawList, specTop + ImVec2(4.f, 0.f), ACCENT, "Spec", label);
			break;
		}
		case Figure::HEAD_BOLT_SEQUENCE:
		{
			// The head from above, its four chambers, and the ten bolts numbered in the order they're
			// tightened: from the middle outward, alternating sides.
			ImVec2 headMin = { min.x + width * 0.08f, min.y + fontSize * 0.5f }, headMax = { max.x - width * 0.08f, max.y - fontSize * 1.8f };
			drawList->AddRectFilled(headMin, headMax, FILL, 6.f);
			drawList->AddRect(headMin, headMax, ACCENT, 6.f, 0, 2.f);
			float pitch = (headMax.x - headMin.x) / 4.f;
			for (int cylinder = 0; cylinder < 4; cylinder++)
				drawList->AddCircle({ headMin.x + pitch * (cylinder + 0.5f), (headMin.y + headMax.y) * 0.5f }, (headMax.y - headMin.y) * 0.28f, INK_FAINT, 32, 1.f);
			const int columnOrder[] = { 2, 1, 3, 0, 4 };
			int number = 1;
			for (int pair = 0; pair < 5; pair++)
				for (int row : { 0, 1 })
				{
					// Each pair of bolts across the head, alternating which side goes first, so it's pulled down evenly.
					int column = columnOrder[pair];
					int side = pair % 2 == 0 ? row : 1 - row;
					ImVec2 at = { headMin.x + (headMax.x - headMin.x) * column / 4.f, side == 0 ? headMin.y + 16.f : headMax.y - 16.f };
					at.x = std::clamp(at.x, headMin.x + 16.f, headMax.x - 16.f);
					drawList->AddCircleFilled(at, 12.f, ACCENT, 20);
					Text(drawList, at, IM_COL32(255, 255, 255, 255), std::to_string(number++), label, 0.5f, 0.5f);
				}
			Text(drawList, { centre.x, max.y - fontSize * 0.2f }, INK_FAINT, "Front of engine: the timing belt end is on the left.", label, 0.5f, 1.f);
			break;
		}
		case Figure::MAIN_CAP_SEQUENCE:
		{
			// The five main caps along the block's underside, tightened from the middle cap outward.
			const int order[] = { 5, 3, 1, 2, 4 };
			float pitch = width / 5.f;
			for (int cap = 0; cap < 5; cap++)
			{
				ImVec2 capMin = { min.x + pitch * cap + pitch * 0.15f, min.y + fontSize * 0.5f }, capMax = { min.x + pitch * (cap + 1) - pitch * 0.15f, max.y - fontSize * 1.8f };
				drawList->AddRectFilled(capMin, capMax, METAL, 4.f);
				drawList->AddRect(capMin, capMax, METAL_DARK, 4.f, 0, 1.5f);
				ImVec2 at = { (capMin.x + capMax.x) * 0.5f, (capMin.y + capMax.y) * 0.5f };
				drawList->AddCircleFilled(at, 13.f, ACCENT, 20);
				Text(drawList, at, IM_COL32(255, 255, 255, 255), std::to_string(order[cap]), label, 0.5f, 0.5f);
				Text(drawList, { at.x, capMax.y + 3.f }, INK_FAINT, std::format("Cap {}", cap + 1), label * 0.9f, 0.5f);
			}
			break;
		}
		case Figure::COMPRESSION_GRAPH:
		{
			// Compression ratio against metal taken off the block deck, worked out from the bore, stroke,
			// chamber, dish, gasket and deck clearance. The target band shaded; above it, detonation.
			double boreArea = PI * 0.25 * spec.bore.nominal * spec.bore.nominal;
			double swept = boreArea * spec.stroke.nominal / 1000.0;
			double deckClearance = spec.deckHeight.nominal - (spec.stroke.nominal * 0.5 + spec.rodLength.nominal + spec.compressionHeight.nominal);
			double gasket = PI * 0.25 * spec.gasketBore * spec.gasketBore * spec.gasketThickness.nominal / 1000.0;
			auto ratio = [&](double removed) {
				double clearance = spec.chamberVolume.nominal + spec.dishVolume.nominal + gasket + boreArea * (deckClearance - removed) / 1000.0;
				return static_cast<float>((swept + clearance) / clearance);
			};
			float scale = imperial ? 1.f / 0.0254f : 1.f;
			float x1 = 0.5f * scale;
			Plot plot = Axes(drawList, min, max, fontSize, 0.f, x1, 8.f, 11.f, imperial ? "Taken off the deck (thou)" : "Taken off the deck (mm)", "Compression ratio",
				imperial ? 5.f : 0.1f, 0.5f, imperial ? "{:.0f}" : "{:.1f}", "{:.1f}");
			ImVec2 bandMin = plot.At(0.f, static_cast<float>(spec.compressionRatio.max)), bandMax = plot.At(x1, static_cast<float>(spec.compressionRatio.min));
			drawList->AddRectFilled(bandMin, bandMax, GOOD_FILL);
			drawList->AddRectFilled(plot.At(0.f, 11.f), plot.At(x1, static_cast<float>(spec.compressionRatio.max)), BAD_FILL);
			Text(drawList, plot.At(x1 * 0.98f, 10.9f), BAD, "Detonation", label, 1.f);
			Text(drawList, plot.At(x1 * 0.02f, static_cast<float>(spec.compressionRatio.min) + 0.05f), GOOD, "Target", label);
			std::vector<ImVec2> points;
			for (int i = 0; i <= 50; i++)
			{
				double removed = 0.5 * i / 50.0;
				points.push_back(plot.At(static_cast<float>(removed) * scale, (std::min)(ratio(removed), 11.f)));
			}
			drawList->AddPolyline(points.data(), static_cast<int>(points.size()), ACCENT, 0, 2.5f);
			ImVec2 stock = plot.At(0.f, ratio(0.0));
			drawList->AddCircleFilled(stock, 4.f, ACCENT);
			Text(drawList, stock + ImVec2(8.f, 2.f), ACCENT, std::format("As built: {:.1f}:1", ratio(0.0)), label);
			break;
		}
	}
}
