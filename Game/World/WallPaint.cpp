#include "WallPaint.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

#include <glm/gtc/quaternion.hpp>

#include "LV/Engine/Graphics/Models/TextureManager.h"
#include "LV/Engine/Objects/ObjectManager.h"
#include "ShopAssets.h"

namespace
{
	constexpr float SHEET_OFF = 0.003f;		// how far the paint's sheet stands off its face
	constexpr float UPLOAD_INTERVAL = 0.05f;
	// Tile textures, painted, are one character per run of texels: the colour ('A' bare, 'B' the first
	// colour...), then how many if more than one.
	constexpr char BARE_CHAR = 'A';
}

const std::vector<WallPaint::Color>& WallPaint::Palette()
{
	static const std::vector<Color> palette = {
		{ "Bare", 0x000000 },
		{ "Black", 0x1A1A1A }, { "White", 0xF2F2EE }, { "Red", 0xC8231E }, { "Blue", 0x1E5AC8 }, { "Yellow", 0xF0C814 },
		{ "Green", 0x28962D }, { "Orange", 0xE66E14 }, { "Silver", 0xB4B8BC }, { "Purple", 0x7832AA }, { "Pink", 0xE65A9B },
		// Showroom Paint (added after the rest, so painted walls keep their colours).
		{ "Gold", 0xD4A52A }, { "Chrome", 0xDCE0E6 }, { "Candy Teal", 0x12A89A },
	};
	return palette;
}

uint8_t WallPaint::NearestColor(uint32_t rgb)
{
	auto channel = [](uint32_t c, int shift) { return static_cast<int>((c >> shift) & 0xFF); };
	uint8_t best = 1;
	int bestDistance = INT32_MAX;
	const auto& palette = Palette();
	for (size_t i = 1; i < palette.size(); i++)
	{
		int distance = 0;
		for (int shift : { 16, 8, 0 })
		{
			int d = channel(rgb, shift) - channel(palette[i].rgb, shift);
			distance += d * d;
		}
		if (distance < bestDistance)
		{
			bestDistance = distance;
			best = static_cast<uint8_t>(i);
		}
	}
	return best;
}

glm::vec3 WallPaint::Plane::Normal() const
{
	glm::vec3 normal(0.f);
	normal[axis] = static_cast<float>(facing);
	return normal;
}

glm::vec3 WallPaint::Plane::U() const
{
	// U x V = the normal, so the sheet's front faces out of the face.
	switch (axis)
	{
		case 0:		return { 0.f, 0.f, -static_cast<float>(facing) };
		case 1:		return { 1.f, 0.f, 0.f };
		default:	return { static_cast<float>(facing), 0.f, 0.f };
	}
}

glm::vec3 WallPaint::Plane::V() const
{
	return axis == 1 ? glm::vec3(0.f, 0.f, -static_cast<float>(facing)) : glm::vec3(0.f, 1.f, 0.f);
}

bool WallPaint::Plane::operator==(const Plane& other) const
{
	return axis == other.axis && facing == other.facing && std::abs(offset - other.offset) < 0.001f;
}

void WallPaint::Build(const ShopAssets& assets)
{
	Clear();
	m_model = assets.Model(ShopModels::PAINT_TILE);
	m_textures.clear();
	m_freeSlots.clear();
	for (int slot = 0; slot < ShopTextures::PAINT_TILE_POOL; slot++)
	{
		m_textures.push_back(assets.Texture(ShopTextures::PaintTile(slot)));
		m_freeSlots.push_back(ShopTextures::PAINT_TILE_POOL - 1 - slot);
	}
}

WallPaint::TileId WallPaint::IdOf(const Plane& plane, int i, int j)
{
	return { plane.axis, plane.facing, static_cast<int>(std::lround(plane.offset * 1000.f)), i, j };
}

std::string WallPaint::KeyOf(const TileId& id)
{
	return std::format("d:{}:{}:{}:{}:{}", id.axis, id.facing, id.offsetMm, id.i, id.j);
}

WallPaint::Tile* WallPaint::TileAt(const Plane& plane, int i, int j, bool create)
{
	TileId id = IdOf(plane, i, j);
	if (m_lastTile && m_lastTile->id == id)
		return m_lastTile;
	if (auto it = m_tiles.find(id); it != m_tiles.end())
		return m_lastTile = &it->second;
	if (!create)
		return nullptr;
	if (m_freeSlots.empty())
	{
		m_full = true;
		return nullptr;
	}
	Tile tile;
	tile.plane = plane;
	tile.id = id;
	tile.slot = m_freeSlots.back();
	m_freeSlots.pop_back();
	auto& placed = m_tiles.emplace(id, std::move(tile)).first->second;
	Place(placed);
	return m_lastTile = &placed;
}

void WallPaint::Place(Tile& tile)
{
	const Plane& plane = tile.plane;
	glm::vec3 u = plane.U(), v = plane.V(), normal = plane.Normal();
	glm::vec3 along(0.f);
	along[plane.axis] = plane.offset;
	glm::vec3 position = along + u * (tile.id.i * TILE) + v * (tile.id.j * TILE) + normal * SHEET_OFF;
	glm::quat rotation = glm::quat_cast(glm::mat3(u, v, normal));
	Object info(position, rotation, { TILE, TILE, 1.f }, m_model, m_textures[tile.slot]);
	tile.object = ObjectManager::Get()->CreateObject(info);
	tile.object->SetShading(ObjectShading::CUTOUT);
	tile.object->SetCastsShadows(false);
	tile.object->SetPickable(false);
	// Hidden until its pixels are sent: its slot holds another tile's paint, or no image yet at all.
	tile.object->SetVisible(false);
	tile.upload = true;
}

void WallPaint::Release(const TileId& id)
{
	auto it = m_tiles.find(id);
	if (it == m_tiles.end())
		return;
	if (m_lastTile == &it->second)
		m_lastTile = nullptr;
	if (it->second.object)
		ObjectManager::Get()->RemoveObject(it->second.object);
	if (it->second.slot >= 0)
		m_freeSlots.push_back(it->second.slot);
	m_tiles.erase(it);
	m_full = false;
}

void WallPaint::Set(const Plane& plane, float u, float v, uint8_t color)
{
	float tileU = u / TILE, tileV = v / TILE;
	int i = static_cast<int>(std::floor(tileU));
	int j = static_cast<int>(std::floor(tileV));
	Tile* tile = TileAt(plane, i, j, color != 0);
	if (!tile)
		return;
	int x = std::clamp(static_cast<int>((tileU - i) * PIXELS), 0, PIXELS - 1);
	int y = std::clamp(static_cast<int>((tileV - j) * PIXELS), 0, PIXELS - 1);
	uint8_t& texel = tile->texels[y * PIXELS + x];
	if (texel == color)
		return;
	tile->painted += (color != 0 ? 1 : 0) - (texel != 0 ? 1 : 0);
	texel = color;
	if (!tile->touched)
	{
		tile->touched = true;
		m_touched.push_back(tile);
	}
}

void WallPaint::FinishDab()
{
	std::vector<TileId> wiped;
	for (Tile* tile : m_touched)
	{
		tile->touched = false;
		tile->upload = true;
		tile->encode = true;
		if (tile->painted <= 0)
			wiped.push_back(tile->id);
	}
	m_touched.clear();
	for (const auto& id : wiped)
		Release(id);
}

void WallPaint::Dab(const Plane& plane, const glm::vec3& point, float radius, uint8_t color, int speckles)
{
	float cu = glm::dot(point, plane.U()), cv = glm::dot(point, plane.V());
	constexpr float TEXEL = TILE / PIXELS;
	radius = (std::max)(radius, TEXEL * 0.6f);
	if (speckles > 0)
	{
		// Scattered over the circle, thicker towards the middle (a wide bell curve, cut off at the edge).
		auto next = [&]() {
			m_random ^= m_random << 13;
			m_random ^= m_random >> 17;
			m_random ^= m_random << 5;
			return (m_random & 0xFFFFFF) / static_cast<float>(0x1000000);
		};
		for (int k = 0; k < speckles; k++)
		{
			float r = std::sqrt(-2.f * std::log((std::max)(next(), 1e-6f))) * radius * 0.6f;
			if (r > radius)
				r = next() * radius;
			float angle = next() * 6.2831853f;
			Set(plane, cu + std::cos(angle) * r, cv + std::sin(angle) * r, color);
		}
	}
	else
	{
		for (float du = -radius; du <= radius; du += TEXEL)
			for (float dv = -radius; dv <= radius; dv += TEXEL)
				if (du * du + dv * dv <= radius * radius)
					Set(plane, cu + du, cv + dv, color);
	}
	FinishDab();
}

void WallPaint::Stroke(const Plane& plane, const glm::vec3& from, const glm::vec3& to, float radius, uint8_t color)
{
	float length = glm::length(to - from);
	int steps = (std::max)(1, static_cast<int>(std::ceil(length / (std::max)(radius * 0.5f, TILE / PIXELS))));
	for (int step = 1; step <= steps; step++)
		Dab(plane, from + (to - from) * (static_cast<float>(step) / steps), radius, color);
}

void WallPaint::BuildPixels(Tile& tile)
{
	// Bare texels take the tile's average colour (still clear), so the colour doesn't darken at the
	// edges of the paint in the smaller mip levels.
	const auto& palette = Palette();
	glm::vec3 sum(0.f);
	int count = 0;
	for (uint8_t texel : tile.texels)
		if (texel != 0 && texel < palette.size())
		{
			uint32_t rgb = palette[texel].rgb;
			sum += glm::vec3((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
			count++;
		}
	glm::vec3 average = count > 0 ? sum / static_cast<float>(count) : glm::vec3(0.f);
	tile.rgba.resize(PIXELS * PIXELS * 4);
	for (size_t i = 0; i < tile.texels.size(); i++)
	{
		uint8_t texel = tile.texels[i];
		uint8_t* out = &tile.rgba[i * 4];
		if (texel != 0 && texel < palette.size())
		{
			uint32_t rgb = palette[texel].rgb;
			out[0] = static_cast<uint8_t>((rgb >> 16) & 0xFF);
			out[1] = static_cast<uint8_t>((rgb >> 8) & 0xFF);
			out[2] = static_cast<uint8_t>(rgb & 0xFF);
			out[3] = 255;
		}
		else
		{
			out[0] = static_cast<uint8_t>(average.x);
			out[1] = static_cast<uint8_t>(average.y);
			out[2] = static_cast<uint8_t>(average.z);
			out[3] = 0;
		}
	}
	tile.upload = false;
}

void WallPaint::Update(float deltaTime)
{
	m_uploadTimer -= deltaTime;
	if (m_uploadTimer > 0.f)
		return;
	m_uploadTimer = UPLOAD_INTERVAL;
	// Every tile changed since, in one batch (which also lets the last batch's staging memory go).
	std::vector<std::pair<uint32_t, const std::vector<uint8_t>*>> updates;
	std::vector<Tile*> sent;
	for (auto& [id, tile] : m_tiles)
		if (tile.upload)
		{
			BuildPixels(tile);
			updates.push_back({ m_textures[tile.slot], &tile.rgba });
			sent.push_back(&tile);
		}
	TextureManager::Get()->UpdateTextures(updates);
	// The upload's ahead of the next frame on the GPU, so they show with their own paint.
	for (Tile* tile : sent)
		if (tile->object)
			tile->object->SetVisible(true);
}

std::map<std::string, std::string> WallPaint::Records() const
{
	std::map<std::string, std::string> records;
	for (const auto& [id, tile] : m_tiles)
	{
		if (tile.encode)
		{
			std::string text;
			for (size_t i = 0; i < tile.texels.size();)
			{
				size_t run = 1;
				while (i + run < tile.texels.size() && tile.texels[i + run] == tile.texels[i])
					run++;
				text += static_cast<char>(BARE_CHAR + tile.texels[i]);
				if (run > 1)
					text += std::to_string(run);
				i += run;
			}
			tile.encoded = std::move(text);
			tile.encode = false;
		}
		records[KeyOf(id)] = tile.encoded;
	}
	return records;
}

void WallPaint::Apply(const std::string& key, const std::optional<std::string>& text)
{
	// d:axis:facing:offset_mm:i:j
	Plane plane;
	int offsetMm = 0, i = 0, j = 0;
	std::string fields = key.substr(2);
	std::replace(fields.begin(), fields.end(), ':', ' ');
	std::istringstream in(fields);
	if (!(in >> plane.axis >> plane.facing >> offsetMm >> i >> j) || plane.axis < 0 || plane.axis > 2 || (plane.facing != 1 && plane.facing != -1))
		return;
	plane.offset = offsetMm / 1000.f;

	std::vector<uint8_t> texels;
	if (text)
	{
		const size_t colors = Palette().size();
		for (size_t at = 0; at < text->size();)
		{
			int color = (*text)[at++] - BARE_CHAR;
			size_t run = 0;
			while (at < text->size() && std::isdigit(static_cast<unsigned char>((*text)[at])))
				run = run * 10 + static_cast<size_t>((*text)[at++] - '0');
			run = (std::max)(run, size_t(1));
			if (color < 0 || static_cast<size_t>(color) >= colors || texels.size() + run > PIXELS * PIXELS)
				return;
			texels.insert(texels.end(), run, static_cast<uint8_t>(color));
		}
		if (texels.size() != PIXELS * PIXELS)
			return;
	}
	int painted = static_cast<int>(std::count_if(texels.begin(), texels.end(), [](uint8_t texel) { return texel != 0; }));
	if (painted == 0)
	{
		Release(IdOf(plane, i, j));
		return;
	}
	Tile* tile = TileAt(plane, i, j, true);
	if (!tile)
		return;
	tile->texels = std::move(texels);
	tile->painted = painted;
	tile->upload = true;
	tile->encode = true;
}

void WallPaint::Clear()
{
	std::vector<TileId> ids;
	for (const auto& [id, tile] : m_tiles)
		ids.push_back(id);
	for (const auto& id : ids)
		Release(id);
	m_touched.clear();
	m_full = false;
}

std::vector<std::string> WallPaint::Keys() const
{
	std::vector<std::string> keys;
	for (const auto& [id, tile] : m_tiles)
		keys.push_back(KeyOf(id));
	return keys;
}

bool WallPaint::IsFull() const
{
	return m_full;
}
