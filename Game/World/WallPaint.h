#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "LV/Engine/Objects/Object.h"

class ShopAssets;

// Marker and spray paint on the shop's walls and floor. Any flat, axis-aligned face can take paint:
// it's laid in TILE-metre squares, each with a texture of its own out of a fixed pool (made only once
// something's painted there), on a thin sheet over the face, see-through where it's bare. Each texel is
// a colour out of the palette, or bare; it's kept as text (run length coded) to save and share.
class WallPaint
{
public:
	static constexpr float TILE = 0.5f;		// metres
	// What the paint can be: 0 is bare (a rag and thinner takes it back to that).
	struct Color
	{
		const char* name;
		uint32_t rgb;
	};
	static const std::vector<Color>& Palette();
	// The palette entry nearest an RGB colour (never 0).
	static uint8_t NearestColor(uint32_t rgb);

	// A face: the one axis it's square to (0 X, 1 Y, 2 Z), which way it faces along it, and where it is.
	struct Plane
	{
		int axis = 2;
		int facing = 1;		// +1 or -1
		float offset = 0.f;
		glm::vec3 Normal() const;
		// Across the face: U to the right and V up, as you face it.
		glm::vec3 U() const;
		glm::vec3 V() const;
		bool operator==(const Plane& other) const;
	};

	void Build(const ShopAssets& assets);
	// Paints a round dab on a face at point (anywhere on its plane), radius metres across: solid, or
	// (spray) count speckles scattered over it, thickest in the middle. color 0 wipes it off.
	void Dab(const Plane& plane, const glm::vec3& point, float radius, uint8_t color, int speckles = 0);
	// A stroke: dabs from one point to the next, close enough together to make a line.
	void Stroke(const Plane& plane, const glm::vec3& from, const glm::vec3& to, float radius, uint8_t color);
	// Sends what's been painted to the GPU, every tile changed since in one go, a few times a second.
	void Update(float deltaTime);

	// Saving and sharing: a record per painted tile, keyed "d:axis:facing:offset_mm:i:j".
	static bool IsKey(const std::string& key) { return key.starts_with("d:"); }
	std::map<std::string, std::string> Records() const;
	// A tile's paint as recorded; nullopt (or bare paint) clears it.
	void Apply(const std::string& key, const std::optional<std::string>& text);
	void Clear();
	std::vector<std::string> Keys() const;
	// True when the pool has run out and a dab found no tile for itself.
	bool IsFull() const;
private:
	static constexpr int PIXELS = 128;
	// Where a tile is: its face (the offset in millimetres) and its square on it.
	struct TileId
	{
		int axis = 0, facing = 0, offsetMm = 0, i = 0, j = 0;
		auto operator<=>(const TileId&) const = default;
	};
	struct Tile
	{
		Plane plane = {};
		TileId id = {};
		int slot = -1;
		std::vector<uint8_t> texels = std::vector<uint8_t>(PIXELS * PIXELS, 0);
		std::vector<uint8_t> rgba = {};		// what was last sent to the GPU
		pObject object = nullptr;
		bool upload = false;
		bool touched = false;	// by the dab going on now
		mutable std::string encoded = "";
		mutable bool encode = true;
		int painted = 0;		// texels that aren't bare
	};

	static TileId IdOf(const Plane& plane, int i, int j);
	static std::string KeyOf(const TileId& id);
	Tile* TileAt(const Plane& plane, int i, int j, bool create);
	void Release(const TileId& id);
	// Lays a tile's sheet over its face.
	void Place(Tile& tile);
	// Sets one texel of a face, given in metres along U and V.
	void Set(const Plane& plane, float u, float v, uint8_t color);
	// After a dab: the tiles it changed are marked to go to the GPU and be recorded, and any it wiped
	// clean go back to the pool.
	void FinishDab();
	void BuildPixels(Tile& tile);

	uint32_t						m_model = 0;
	std::vector<uint32_t>			m_textures = {};	// the pool
	std::vector<int>				m_freeSlots = {};
	std::map<TileId, Tile>			m_tiles = {};
	std::vector<Tile*>				m_touched = {};
	Tile*							m_lastTile = nullptr;	// the tile Set last wrote to, to find it again quickly
	float							m_uploadTimer = 0.f;
	bool							m_full = false;
	uint32_t						m_random = 12345u;
};
