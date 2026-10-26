#include "ProceduralTextures.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace ProceduralTextures
{
	namespace
	{
		constexpr int SIZE = 256;

		float Hash(int x, int y, int seed)
		{
			uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + static_cast<uint32_t>(seed) * 2246822519u;
			h = (h ^ (h >> 13)) * 1274126177u;
			h ^= h >> 16;
			return (h & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
		}

		// Fractal value noise in [0, 1] for u and v in [0, 1). Every octave repeats a whole number
		// of times across the texture so it tiles; stretchU > 1 gives brushed-metal streaks.
		float Fbm(float u, float v, int basePeriod, int octaves, int seed, float stretchU = 1.f)
		{
			float sum = 0.f, amplitude = 0.5f, total = 0.f;
			int period = basePeriod;
			for (int octave = 0; octave < octaves; octave++)
			{
				int periodU = std::max(1, static_cast<int>(period * stretchU));
				float x = u * periodU, y = v * period;
				int seedOffset = seed + octave * 101;
				int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
				float fx = x - x0, fy = y - y0;
				fx = fx * fx * (3.f - 2.f * fx);
				fy = fy * fy * (3.f - 2.f * fy);
				auto corner = [&](int cx, int cy) { return Hash(((cx % periodU) + periodU) % periodU, ((cy % period) + period) % period, seedOffset); };
				float value = glm::mix(glm::mix(corner(x0, y0), corner(x0 + 1, y0), fx), glm::mix(corner(x0, y0 + 1), corner(x0 + 1, y0 + 1), fx), fy);

				sum += value * amplitude;
				total += amplitude;
				amplitude *= 0.5f;
				period *= 2;
			}
			return sum / total;
		}

		Image Generate(const std::function<glm::vec3(float u, float v, int x, int y)>& shader)
		{
			Image image;
			image.width = SIZE;
			image.height = SIZE;
			image.pixels.resize(SIZE * SIZE * 4);
			for (int y = 0; y < SIZE; y++)
				for (int x = 0; x < SIZE; x++)
				{
					glm::vec3 color = glm::clamp(shader(static_cast<float>(x) / SIZE, static_cast<float>(y) / SIZE, x, y), 0.f, 1.f);
					size_t i = (static_cast<size_t>(y) * SIZE + x) * 4;
					image.pixels[i + 0] = static_cast<uint8_t>(color.r * 255.f);
					image.pixels[i + 1] = static_cast<uint8_t>(color.g * 255.f);
					image.pixels[i + 2] = static_cast<uint8_t>(color.b * 255.f);
					image.pixels[i + 3] = 255;
				}
			return image;
		}
	}

	Image Concrete()
	{
		return Generate([](float u, float v, int x, int y) {
			float stains = Fbm(u, v, 4, 5, 11);
			float grain = Hash(x, y, 12);
			float tone = 0.50f + (stains - 0.5f) * 0.22f + (grain - 0.5f) * 0.06f;
			// Saw-cut control joint along two edges, so tiles read as slabs.
			if (x < 2 || y < 2)
				tone *= 0.55f;
			return glm::vec3(tone, tone * 0.98f, tone * 0.95f);
		});
	}

	Image CastIron()
	{
		return Generate([](float u, float v, int x, int y) {
			float casting = Fbm(u, v, 8, 4, 21);
			float grain = Hash(x, y, 22);
			float tone = 0.36f + (casting - 0.5f) * 0.10f + (grain - 0.5f) * 0.10f;
			return glm::vec3(tone, tone, tone * 1.02f);
		});
	}

	Image Aluminium()
	{
		return Generate([](float u, float v, int x, int y) {
			float casting = Fbm(u, v, 6, 4, 31);
			float grain = Hash(x, y, 32);
			float tone = 0.74f + (casting - 0.5f) * 0.08f + (grain - 0.5f) * 0.04f;
			return glm::vec3(tone, tone * 1.0f, tone * 1.03f);
		});
	}

	Image BrushedSteel()
	{
		return Generate([](float u, float v, int, int) {
			float streaks = Fbm(u, v, 4, 5, 41, 16.f);
			float tone = 0.62f + (streaks - 0.5f) * 0.16f;
			return glm::vec3(tone * 0.98f, tone, tone * 1.03f);
		});
	}

	Image Wood()
	{
		return Generate([](float u, float v, int x, int y) {
			float warp = Fbm(u, v, 4, 4, 51);
			float rings = 0.5f + 0.5f * std::sin((v * 24.f + warp * 4.f) * glm::two_pi<float>() / 4.f);
			float grain = Hash(x / 4, y, 52);
			glm::vec3 light = { 0.66f, 0.48f, 0.30f };
			glm::vec3 dark = { 0.45f, 0.30f, 0.17f };
			return glm::mix(dark, light, rings * 0.7f + grain * 0.3f);
		});
	}

	Image PaintedMetal()
	{
		return Generate([](float u, float v, int x, int y) {
			float wear = Fbm(u, v, 8, 4, 61);
			float grain = Hash(x, y, 62);
			float tone = 0.88f + (wear - 0.5f) * 0.08f + (grain - 0.5f) * 0.03f;
			return glm::vec3(tone);
		});
	}

	Image Plaster()
	{
		return Generate([](float u, float v, int x, int y) {
			float mottling = Fbm(u, v, 4, 5, 71);
			float grain = Hash(x, y, 72);
			float tone = 0.80f + (mottling - 0.5f) * 0.08f + (grain - 0.5f) * 0.03f;
			return glm::vec3(tone, tone, tone * 0.97f);
		});
	}

	Image Pegboard()
	{
		return Generate([](float u, float v, int x, int y) {
			float grain = Fbm(u, v, 8, 3, 81);
			glm::vec3 board = glm::vec3(0.70f, 0.58f, 0.42f) * (0.92f + grain * 0.16f);
			// A hole every 32 pixels.
			int cx = (x % 32) - 16, cy = (y % 32) - 16;
			if (cx * cx + cy * cy < 16)
				return glm::vec3(0.08f);
			return board;
		});
	}

	Image Rubber()
	{
		return Generate([](float, float, int x, int y) {
			float tone = 0.13f + (Hash(x, y, 91) - 0.5f) * 0.03f;
			return glm::vec3(tone);
		});
	}

	Image Cardboard()
	{
		return Generate([](float u, float v, int x, int y) {
			float fibres = Fbm(u, v, 8, 4, 101, 4.f);
			float speck = Hash(x, y, 102);
			glm::vec3 board = glm::vec3(0.66f, 0.50f, 0.32f) * (0.9f + fibres * 0.2f + (speck - 0.5f) * 0.05f);
			// Packing tape across the middle of each face.
			if (v > 0.44f && v < 0.56f)
				return glm::mix(board, glm::vec3(0.80f, 0.70f, 0.48f), 0.75f) * (0.97f + Hash(x, 0, 103) * 0.03f);
			// A shipping label in one corner.
			if (u > 0.08f && u < 0.32f && v > 0.08f && v < 0.26f)
				return (y % 6 < 2 && u > 0.1f && u < 0.28f) ? glm::vec3(0.25f) : glm::vec3(0.93f);
			return board;
		});
	}

	Image Honed()
	{
		// Plateau-honed crosshatch: two families of fine scratches at about 45 degrees to each other.
		return Generate([](float u, float v, int x, int y) {
			float base = 0.58f + (Fbm(u, v, 8, 3, 111) - 0.5f) * 0.04f;
			auto scratches = [&](int line, int seed) {
				int period = 7;
				int phase = ((line % period) + period) % period;
				float strength = Hash(line / period, 0, seed);
				return phase == 0 && strength > 0.35f ? strength : 0.f;
			};
			float cut = (std::max)(scratches(x + y, 112), scratches(x - y + SIZE, 113));
			return glm::vec3(base - cut * 0.22f);
		});
	}

	Image Bored()
	{
		// The boring bar's single point leaves a fine spiral thread around the bore.
		return Generate([](float u, float v, int x, int y) {
			float thread = 0.5f + 0.5f * std::sin(glm::two_pi<float>() * (v * 48.f + u * 2.f));
			float tone = 0.50f + thread * 0.16f + (Hash(x, y, 121) - 0.5f) * 0.04f;
			return glm::vec3(tone, tone, tone * 1.02f);
		});
	}

	Image Glazed()
	{
		return Generate([](float u, float v, int x, int y) {
			float sheen = Fbm(u, v, 2, 3, 131, 8.f);
			float tone = 0.64f + (sheen - 0.5f) * 0.10f + (Hash(x, y, 132) - 0.5f) * 0.01f;
			return glm::vec3(tone * 0.97f, tone * 0.95f, tone * 0.90f);
		});
	}

	Image Worn()
	{
		// Ring travel polishes vertical streaks into the bore and leaves a ridge-line look.
		return Generate([](float u, float v, int x, int y) {
			float streaks = Fbm(u, v, 4, 5, 141, 16.f);
			float tone = 0.48f + (streaks - 0.5f) * 0.22f + (Hash(x, y, 142) - 0.5f) * 0.04f;
			return glm::vec3(tone, tone * 0.98f, tone * 0.95f);
		});
	}

	Image Scored()
	{
		return Generate([](float u, float v, int x, int y) {
			float base = 0.40f + (Fbm(u, v, 4, 4, 151) - 0.5f) * 0.12f;
			glm::vec3 color = glm::vec3(base, base * 0.92f, base * 0.85f);
			// Deep gouges along the direction of travel, some with a burnt brown edge.
			float gouge = Hash(x / 3, 0, 152);
			if (gouge > 0.9f)
				color *= 0.45f + Hash(x, y / 8, 153) * 0.2f;
			else if (gouge > 0.84f)
				color = glm::mix(color, glm::vec3(0.45f, 0.30f, 0.18f), 0.5f);
			return color;
		});
	}

	Image Ground()
	{
		// Fine circumferential grinding lines.
		return Generate([](float u, float v, int x, int y) {
			float lines = Hash(0, y, 161);
			float tone = 0.66f + (lines - 0.5f) * 0.10f + (Fbm(u, v, 4, 3, 162) - 0.5f) * 0.05f;
			return glm::vec3(tone, tone, tone * 1.03f);
		});
	}

	Image Polished()
	{
		return Generate([](float u, float v, int x, int y) {
			float tone = 0.86f + (Fbm(u, v, 2, 3, 171) - 0.5f) * 0.05f + (Hash(x, y, 172) - 0.5f) * 0.01f;
			return glm::vec3(tone, tone, tone * 1.02f);
		});
	}

	Image SurfaceGround()
	{
		// Straight passes of the grinding wheel across the deck, slightly overlapping.
		return Generate([](float u, float v, int x, int y) {
			float pass = 0.5f + 0.5f * std::sin(glm::two_pi<float>() * v * 16.f);
			float scratch = Hash(0, y, 181);
			float tone = 0.60f + pass * 0.06f + (scratch - 0.5f) * 0.08f + (Hash(x, y, 182) - 0.5f) * 0.02f;
			return glm::vec3(tone, tone, tone * 1.02f);
		});
	}

	Image Grass()
	{
		return Generate([](float u, float v, int x, int y) {
			float patches = Fbm(u, v, 3, 5, 201);
			float blades = Hash(x, y, 202);
			float clumps = Fbm(u, v, 24, 2, 203);
			glm::vec3 lush = { 0.30f, 0.46f, 0.18f };
			glm::vec3 dry = { 0.47f, 0.50f, 0.26f };
			glm::vec3 color = glm::mix(lush, dry, glm::smoothstep(0.35f, 0.75f, patches));
			return color * (0.78f + blades * 0.30f + (clumps - 0.5f) * 0.25f);
		});
	}

	Image Asphalt()
	{
		return Generate([](float u, float v, int x, int y) {
			float wear = Fbm(u, v, 4, 5, 211);
			float aggregate = Hash(x, y, 212);
			float tone = 0.20f + (wear - 0.5f) * 0.08f + (aggregate > 0.92f ? 0.10f : (aggregate - 0.5f) * 0.05f);
			return glm::vec3(tone, tone, tone * 1.04f);
		});
	}

	Image Siding()
	{
		return Generate([](float u, float v, int, int) {
			// Eight ribs across the tile, lit as if the light comes from one side.
			float rib = std::sin(u * 8.f * glm::two_pi<float>());
			float weather = Fbm(u, v, 4, 4, 221);
			float streaks = Fbm(u, v, 2, 4, 222, 8.f);
			float tone = 0.60f + rib * 0.07f + (weather - 0.5f) * 0.10f - streaks * 0.06f;
			return glm::vec3(tone * 0.90f, tone * 0.95f, tone);
		});
	}

	Image Brick()
	{
		return Generate([](float u, float v, int x, int y) {
			// Running bond: 4 courses of 2 bricks, alternate courses offset by half a brick.
			int course = y / 64;
			int column = (x + (course % 2) * 64) / 128;
			bool mortar = (y % 64) < 5 || ((x + (course % 2) * 64) % 128) < 5;
			if (mortar)
				return glm::vec3(0.62f, 0.60f, 0.56f) * (0.9f + Hash(x, y, 231) * 0.1f);
			float brickTone = Hash(column, course, 232);
			float speckle = Hash(x, y, 233);
			float dirt = Fbm(u, v, 4, 3, 234);
			glm::vec3 color = glm::mix(glm::vec3(0.55f, 0.24f, 0.17f), glm::vec3(0.66f, 0.34f, 0.22f), brickTone);
			return color * (0.85f + speckle * 0.15f) * (0.9f + dirt * 0.15f);
		});
	}

	Image Shingles()
	{
		return Generate([](float u, float v, int x, int y) {
			int row = y / 32;
			int tab = (x + (row % 2) * 16) / 32;
			float tone = 0.26f + Hash(tab, row, 241) * 0.10f + (Hash(x, y, 242) - 0.5f) * 0.06f;
			if (y % 32 > 28)
				tone *= 0.6f;	// the shadow line under each row
			if ((x + (row % 2) * 16) % 32 < 2)
				tone *= 0.75f;
			return glm::vec3(tone * 1.05f, tone * 0.95f, tone * 0.9f);
		});
	}

	Image Bark()
	{
		return Generate([](float u, float v, int x, int y) {
			float furrows = Fbm(u, v, 3, 4, 251, 6.f);
			float grain = Hash(x, y, 252);
			float tone = 0.32f + (furrows - 0.5f) * 0.35f + (grain - 0.5f) * 0.06f;
			return glm::vec3(tone, tone * 0.82f, tone * 0.65f);
		});
	}

	Image Foliage()
	{
		return Generate([](float u, float v, int x, int y) {
			float clusters = Fbm(u, v, 6, 4, 261);
			float leaves = Hash(x, y, 262);
			float light = glm::smoothstep(0.3f, 0.8f, clusters);
			glm::vec3 shade = { 0.13f, 0.26f, 0.10f };
			glm::vec3 sunlit = { 0.33f, 0.50f, 0.20f };
			return glm::mix(shade, sunlit, light) * (0.8f + leaves * 0.35f);
		});
	}
}
