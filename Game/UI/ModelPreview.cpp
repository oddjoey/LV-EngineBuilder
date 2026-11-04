#include "ModelPreview.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "LV/Engine/Graphics/Models/ModelManager.h"

namespace ModelPreview
{
	namespace
	{
		// A model's triangles, ready to turn: centred on its bounds, with a unit normal and a colour.
		struct Facet
		{
			glm::vec3 corners[3];
			glm::vec3 normal;
			glm::vec3 color;
		};

		// A run of same-coloured pixels on one row of the picture.
		struct Span
		{
			int x0, x1, y;
			ImU32 color;
		};

		// A model's triangles, prepared once.
		struct Shape
		{
			std::vector<Facet> facets = {};
			float radius = 1.f;
			double usedAt = 0.0;
		};

		// One view of a model at one size: the last picture, kept until it's time to turn the model again.
		struct Picture
		{
			std::vector<Span> spans = {};
			double renderedAt = -1.0;
			ImVec4 renderedTint = {};
			double usedAt = 0.0;
		};

		// Turning it 30 times a second looks the same as every frame, at a fraction of the cost.
		constexpr double TURN_INTERVAL = 1.0 / 30.0;
		// Samples per pixel along each axis: 2 x 2 smooths the edges.
		constexpr int SUPERSAMPLE = 2;
		// Shapes and pictures not drawn for this long are let go.
		constexpr double PICTURE_LIFETIME = 2.0;
		constexpr double SHAPE_LIFETIME = 10.0;

		std::map<uint32_t, Shape> shapes;
		std::map<std::tuple<uint32_t, int, int>, Picture> pictures;	// by model, width and height

		Shape& Prepare(uint32_t modelIndex, const Model& model)
		{
			auto [it, added] = shapes.try_emplace(modelIndex);
			Shape& cache = it->second;
			if (!added)
				return cache;

			const auto& vertices = model.GetVertices();
			const auto& indices = model.GetIndices();
			glm::vec3 boundsMin = model.GetBoundsMin(), boundsMax = model.GetBoundsMax();
			glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
			cache.radius = (std::max)(glm::length(boundsMax - boundsMin) * 0.5f, 1e-4f);
			cache.facets.reserve(indices.size() / 3);
			for (size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				const auto& a = vertices[indices[i]];
				const auto& b = vertices[indices[i + 1]];
				const auto& c = vertices[indices[i + 2]];
				glm::vec3 normal = glm::cross(b.position - a.position, c.position - a.position);
				float area = glm::length(normal);
				if (area < 1e-12f)
					continue;
				cache.facets.push_back({ { a.position - center, b.position - center, c.position - center }, normal / area, (a.color + b.color + c.color) / 3.f });
			}
			return cache;
		}

		// Draws the model into a depth-tested picture (so nothing behind shows through, however the
		// triangles overlap), then turns each row into runs of one colour for ImGui to fill.
		void Render(const Shape& cache, Picture& picture, int width, int height, ImVec4 tint, double time)
		{
			int sampleWidth = width * SUPERSAMPLE, sampleHeight = height * SUPERSAMPLE;
			static std::vector<float> depth;
			static std::vector<uint32_t> samples;	// 0x00BBGGRR plus 1 << 24 where covered
			depth.assign(static_cast<size_t>(sampleWidth) * sampleHeight, -FLT_MAX);
			samples.assign(static_cast<size_t>(sampleWidth) * sampleHeight, 0u);

			// Turning slowly about the vertical axis, tipped toward the viewer a little so its top shows.
			float angle = static_cast<float>(time) * 0.6f;
			glm::mat3 rotation = glm::mat3(glm::rotate(glm::mat4(1.f), 0.45f, { 1.f, 0.f, 0.f }) * glm::rotate(glm::mat4(1.f), angle, { 0.f, 1.f, 0.f }));
			float scale = (std::min)(sampleWidth, sampleHeight) * 0.46f / cache.radius;
			glm::vec3 light = glm::normalize(glm::vec3(0.4f, 0.8f, 0.6f));
			glm::vec3 tinted = glm::vec3(tint.x, tint.y, tint.z) * 0.85f * 255.f;

			for (const auto& facet : cache.facets)
			{
				glm::vec3 p[3];
				for (int corner = 0; corner < 3; corner++)
				{
					glm::vec3 turned = rotation * facet.corners[corner];
					p[corner] = { sampleWidth * 0.5f + turned.x * scale, sampleHeight * 0.5f - turned.y * scale, turned.z };
				}
				// Back faces are skipped (screen-space winding), so only the outside shows.
				float area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x);
				if (area >= 0.f)
					continue;

				float shade = 0.35f + 0.65f * (std::max)(0.f, glm::dot(rotation * facet.normal, light));
				glm::vec3 color = glm::clamp(facet.color * shade * tinted, glm::vec3(0.f), glm::vec3(255.f));
				uint32_t packed = (1u << 24) | (static_cast<uint32_t>(color.b) << 16) | (static_cast<uint32_t>(color.g) << 8) | static_cast<uint32_t>(color.r);

				int minX = (std::max)(0, static_cast<int>(std::floor((std::min)({ p[0].x, p[1].x, p[2].x }))));
				int maxX = (std::min)(sampleWidth - 1, static_cast<int>(std::ceil((std::max)({ p[0].x, p[1].x, p[2].x }))));
				int minY = (std::max)(0, static_cast<int>(std::floor((std::min)({ p[0].y, p[1].y, p[2].y }))));
				int maxY = (std::min)(sampleHeight - 1, static_cast<int>(std::ceil((std::max)({ p[0].y, p[1].y, p[2].y }))));
				if (minX > maxX || minY > maxY)
					continue;

				// Barycentric weights at each sample centre, stepped along the row.
				float inverse = 1.f / area;
				auto edge = [](const glm::vec3& a, const glm::vec3& b, float x, float y) { return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x); };
				float step0 = -(p[2].y - p[1].y) * inverse, step1 = -(p[0].y - p[2].y) * inverse, step2 = -(p[1].y - p[0].y) * inverse;
				for (int y = minY; y <= maxY; y++)
				{
					float centerY = y + 0.5f, startX = minX + 0.5f;
					float w0 = edge(p[1], p[2], startX, centerY) * inverse;
					float w1 = edge(p[2], p[0], startX, centerY) * inverse;
					float w2 = edge(p[0], p[1], startX, centerY) * inverse;
					size_t row = static_cast<size_t>(y) * sampleWidth;
					for (int x = minX; x <= maxX; x++, w0 += step0, w1 += step1, w2 += step2)
					{
						if (w0 < 0.f || w1 < 0.f || w2 < 0.f)
							continue;
						float z = w0 * p[0].z + w1 * p[1].z + w2 * p[2].z;
						if (z <= depth[row + x])
							continue;
						depth[row + x] = z;
						samples[row + x] = packed;
					}
				}
			}

			// Each pixel is the average of its samples; partly covered ones (the edges) let the
			// background through in proportion.
			picture.spans.clear();
			constexpr int SAMPLES = SUPERSAMPLE * SUPERSAMPLE;
			for (int y = 0; y < height; y++)
			{
				Span run = { 0, -1, y, 0 };
				for (int x = 0; x < width; x++)
				{
					uint32_t r = 0, g = 0, b = 0, covered = 0;
					for (int sy = 0; sy < SUPERSAMPLE; sy++)
						for (int sx = 0; sx < SUPERSAMPLE; sx++)
						{
							uint32_t sample = samples[static_cast<size_t>(y * SUPERSAMPLE + sy) * sampleWidth + x * SUPERSAMPLE + sx];
							if (!(sample >> 24))
								continue;
							r += sample & 0xFF;
							g += (sample >> 8) & 0xFF;
							b += (sample >> 16) & 0xFF;
							covered++;
						}
					ImU32 pixel = covered ? IM_COL32(r / covered, g / covered, b / covered, covered * 255 / SAMPLES) : 0;
					if (pixel == run.color && run.x1 == x - 1)
					{
						run.x1 = x;
						continue;
					}
					if (run.color && run.x1 >= run.x0)
						picture.spans.push_back(run);
					run = { x, x, y, pixel };
				}
				if (run.color && run.x1 >= run.x0)
					picture.spans.push_back(run);
			}
		}

		// Lets go of what hasn't been drawn lately (the shop window closed, the book turned on), at most once a second.
		void Forget(double time)
		{
			static double sweptAt = 0.0;
			if (time - sweptAt < 1.0 && time >= sweptAt)
				return;
			sweptAt = time;
			std::erase_if(pictures, [&](const auto& entry) { return time - entry.second.usedAt > PICTURE_LIFETIME; });
			std::erase_if(shapes, [&](const auto& entry) { return time - entry.second.usedAt > SHAPE_LIFETIME; });
		}
	}

	void Draw(uint32_t modelIndex, float width, float height, ImVec4 tint)
	{
		ImVec2 min = ImGui::GetCursorScreenPos();
		ImGui::Dummy({ width, height });
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 max = { min.x + width, min.y + height };
		drawList->AddRectFilledMultiColor(min, max, IM_COL32(52, 56, 64, 255), IM_COL32(52, 56, 64, 255), IM_COL32(28, 30, 34, 255), IM_COL32(28, 30, 34, 255));
		DrawAt(drawList, min, max, modelIndex, tint);
	}

	void DrawAt(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, uint32_t modelIndex, ImVec4 tint)
	{
		auto model = ModelManager::Get()->GetModel(modelIndex);
		if (!model || model->GetVertices().empty() || model->GetIndices().size() < 3)
			return;

		double time = ImGui::GetTime();
		Forget(time);
		auto& shape = Prepare(modelIndex, *model);
		shape.usedAt = time;
		int pixelsWide = (std::max)(1, static_cast<int>(max.x - min.x)), pixelsHigh = (std::max)(1, static_cast<int>(max.y - min.y));
		auto& picture = pictures[{ modelIndex, pixelsWide, pixelsHigh }];
		picture.usedAt = time;
		bool retinted = picture.renderedTint.x != tint.x || picture.renderedTint.y != tint.y || picture.renderedTint.z != tint.z;
		if (retinted || time - picture.renderedAt >= TURN_INTERVAL || time < picture.renderedAt)
		{
			Render(shape, picture, pixelsWide, pixelsHigh, tint, time);
			picture.renderedAt = time;
			picture.renderedTint = tint;
		}

		// One flat rectangle per run, straight into the draw list.
		ImVec2 origin = { std::floor(min.x), std::floor(min.y) };
		drawList->PushClipRect(min, max, true);
		constexpr size_t BATCH = 4096;
		for (size_t first = 0; first < picture.spans.size(); first += BATCH)
		{
			size_t count = (std::min)(BATCH, picture.spans.size() - first);
			drawList->PrimReserve(static_cast<int>(count * 6), static_cast<int>(count * 4));
			for (size_t i = first; i < first + count; i++)
			{
				const auto& span = picture.spans[i];
				drawList->PrimRect({ origin.x + span.x0, origin.y + span.y }, { origin.x + span.x1 + 1, origin.y + span.y + 1 }, span.color);
			}
		}
		drawList->PopClipRect();
	}
}
