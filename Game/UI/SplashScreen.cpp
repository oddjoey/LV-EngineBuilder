#include "SplashScreen.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <imgui.h>

namespace
{
	constexpr float FADE_IN = 0.5f;
	constexpr float HOLD = 2.0f;
	constexpr float FADE_OUT = 0.7f;
	constexpr float LENGTH = FADE_IN + HOLD + FADE_OUT;
	// Keys pressed this early are the ones that launched the game, not a request to skip.
	constexpr float SKIPPABLE_AFTER = 0.3f;
	// Only release builds show it: a debug build, run over and over while working on the game, starts
	// straight in the shop.
#ifdef NDEBUG
	constexpr bool SHOWN = true;
#else
	constexpr bool SHOWN = false;
#endif

	ImU32 Faded(int r, int g, int b, float alpha)
	{
		return IM_COL32(r, g, b, static_cast<int>(255.f * alpha));
	}

	// A cartoon monkey sitting up, a spanner in its right hand, centred on center at about size
	// pixels tall.
	void DrawMonkey(ImDrawList* drawList, ImVec2 center, float size, float alpha)
	{
		auto at = [&](float x, float y) { return ImVec2(center.x + x * size, center.y + y * size); };
		float unit = size;
		ImU32 fur = Faded(122, 78, 40, alpha);
		ImU32 furDark = Faded(92, 58, 30, alpha);
		ImU32 face = Faded(232, 196, 150, alpha);
		ImU32 dark = Faded(30, 22, 18, alpha);
		ImU32 steel = Faded(176, 182, 192, alpha);
		ImU32 steelDark = Faded(120, 126, 136, alpha);

		// Tail, curling up behind.
		drawList->AddBezierCubic(at(0.18f, 0.38f), at(0.55f, 0.45f), at(0.6f, 0.05f), at(0.42f, 0.0f), furDark, unit * 0.045f, 32);

		// Body, belly and legs.
		drawList->AddEllipseFilled(at(0.f, 0.25f), { unit * 0.27f, unit * 0.24f }, fur, 0.f, 48);
		drawList->AddEllipseFilled(at(0.f, 0.29f), { unit * 0.17f, unit * 0.16f }, face, 0.f, 40);
		drawList->AddEllipseFilled(at(-0.16f, 0.46f), { unit * 0.12f, unit * 0.06f }, furDark, 0.f, 24);
		drawList->AddEllipseFilled(at(0.16f, 0.46f), { unit * 0.12f, unit * 0.06f }, furDark, 0.f, 24);

		// Left arm hanging down, right arm raised with the spanner.
		drawList->AddLine(at(-0.22f, 0.12f), at(-0.3f, 0.36f), fur, unit * 0.09f);
		drawList->AddCircleFilled(at(-0.3f, 0.37f), unit * 0.055f, face, 20);
		drawList->AddLine(at(0.22f, 0.12f), at(0.4f, -0.02f), fur, unit * 0.09f);

		// The spanner: a handle up through the fist, an open jaw at the top, a ring at the bottom.
		ImVec2 handleTop = at(0.52f, -0.32f), handleBottom = at(0.36f, 0.12f);
		drawList->AddLine(handleBottom, handleTop, steel, unit * 0.05f);
		drawList->AddCircleFilled(handleTop, unit * 0.075f, steel, 24);
		drawList->AddCircleFilled(at(0.54f, -0.38f), unit * 0.035f, Faded(24, 24, 30, alpha), 16);
		drawList->AddRectFilled(at(0.525f, -0.45f), at(0.56f, -0.38f), Faded(24, 24, 30, alpha));
		drawList->AddCircle(handleBottom, unit * 0.045f, steel, 20, unit * 0.025f);
		drawList->AddLine(at(0.39f, 0.04f), at(0.49f, -0.24f), steelDark, unit * 0.012f);
		drawList->AddCircleFilled(at(0.41f, -0.03f), unit * 0.06f, face, 20);

		// Head: ears, the round head, the pale face (two brows and a muzzle), eyes, nose, smile.
		ImVec2 head = at(0.f, -0.2f);
		for (float side : { -1.f, 1.f })
		{
			drawList->AddCircleFilled(at(side * 0.3f, -0.2f), unit * 0.1f, fur, 24);
			drawList->AddCircleFilled(at(side * 0.3f, -0.2f), unit * 0.06f, face, 20);
		}
		drawList->AddCircleFilled(head, unit * 0.28f, fur, 48);
		drawList->AddCircleFilled(at(-0.09f, -0.24f), unit * 0.12f, face, 32);
		drawList->AddCircleFilled(at(0.09f, -0.24f), unit * 0.12f, face, 32);
		drawList->AddEllipseFilled(at(0.f, -0.1f), { unit * 0.19f, unit * 0.13f }, face, 0.f, 40);
		for (float side : { -1.f, 1.f })
		{
			drawList->AddCircleFilled(at(side * 0.085f, -0.24f), unit * 0.035f, dark, 16);
			drawList->AddCircleFilled(at(side * 0.075f, -0.252f), unit * 0.012f, Faded(255, 255, 255, alpha), 10);
			drawList->AddCircleFilled(at(side * 0.03f, -0.13f), unit * 0.012f, dark, 10);
		}
		drawList->PathArcTo(at(0.f, -0.1f), unit * 0.1f, 0.35f, 2.79f, 24);
		drawList->PathStroke(dark, 0, unit * 0.018f);
	}
}

void SplashScreen::Update(float deltaTime, bool skipPressed)
{
	if (m_skipped)
		return;
	m_time += (std::min)(deltaTime, 0.1f);
	if (skipPressed && m_time > SKIPPABLE_AFTER)
		m_skipped = true;
}

bool SplashScreen::IsShowing() const
{
	return SHOWN && !m_skipped && m_time < LENGTH;
}

void SplashScreen::Draw() const
{
	if (!IsShowing())
		return;

	// The card fades in from black and out to the game; the background stays solid until the fade
	// out, so the shop never shows behind a half-drawn card.
	float shown = m_time < FADE_IN ? m_time / FADE_IN : m_time > FADE_IN + HOLD ? 1.f - (m_time - FADE_IN - HOLD) / FADE_OUT : 1.f;
	shown = std::clamp(shown, 0.f, 1.f);
	float background = m_time > FADE_IN + HOLD ? shown : 1.f;

	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	ImVec2 display = ImGui::GetIO().DisplaySize;
	drawList->AddRectFilled({ 0.f, 0.f }, display, Faded(20, 20, 24, background));

	float size = (std::min)(display.x, display.y) * 0.42f;
	ImVec2 center = { display.x * 0.5f, display.y * 0.42f };
	DrawMonkey(drawList, center, size, shown);

	const char* name = "monkeykobo";
	float fontSize = size * 0.2f;
	ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.f, name);
	drawList->AddText(ImGui::GetFont(), fontSize, { center.x - textSize.x * 0.5f, center.y + size * 0.62f }, Faded(235, 225, 205, shown), name);
}
