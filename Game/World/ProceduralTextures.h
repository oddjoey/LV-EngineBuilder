#pragma once

#include <cstdint>
#include <vector>

// Tileable surface textures generated at startup, so the shop needs no image files.
// Colours are authored in sRGB, matching the engine's sRGB texture format.
namespace ProceduralTextures
{
	struct Image
	{
		int width = 0;
		int height = 0;
		std::vector<uint8_t> pixels = {};
	};

	Image Concrete();
	Image CastIron();
	Image Aluminium();
	Image BrushedSteel();
	Image Wood();
	Image PaintedMetal();
	Image Plaster();
	Image Pegboard();
	Image Rubber();
	Image Cardboard();

	// Outdoors
	Image Grass();
	Image Asphalt();
	Image Siding();		// corrugated metal, ribs running along V
	Image Brick();
	Image Shingles();
	Image Bark();
	Image Foliage();

	// Machined and worn surface finishes. U runs around a bore or journal, V along it.
	Image Honed();
	Image Bored();
	Image Glazed();
	Image Worn();
	Image Scored();
	Image Ground();
	Image Polished();
	Image SurfaceGround();
}
