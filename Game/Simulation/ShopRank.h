#pragma once

#include <array>

// The shop's standing: reputation earned building engines (mostly by selling good ones), which
// raises its rank. Each rank opens up better tools in the store and pays a bonus on the way in.
namespace ShopRank
{
	struct Rank
	{
		const char* name;
		double reputation;	// reputation needed to reach it
		double bonus;		// paid on reaching it
	};

	inline constexpr std::array<Rank, 8> RANKS = { {
		{ "Backyard Tinkerer", 0.0, 0.0 },
		{ "Weekend Wrench", 50.0, 300.0 },
		{ "Garage Hand", 150.0, 500.0 },
		{ "Engine Builder", 300.0, 800.0 },
		{ "Machinist", 500.0, 1200.0 },
		{ "Master Builder", 800.0, 1800.0 },
		{ "Race Shop", 1200.0, 2500.0 },
		{ "Legend of the Trade", 1800.0, 4000.0 },
	} };

	// 1-based, as the store and the HUD show it.
	inline int RankFor(double reputation)
	{
		int rank = 1;
		for (size_t i = 1; i < RANKS.size(); i++)
			if (reputation >= RANKS[i].reputation)
				rank = static_cast<int>(i) + 1;
		return rank;
	}
	inline const Rank& Info(int rank) { return RANKS[(rank < 1 ? 1 : rank > static_cast<int>(RANKS.size()) ? static_cast<int>(RANKS.size()) : rank) - 1]; }
	inline int MaxRank() { return static_cast<int>(RANKS.size()); }

	// What's earned: an engine sold earns for how healthy it ran and how strong it is; a clean dyno
	// pull earns a little.
	inline double ForSale(double healthScore, double horsepower) { return 15.0 + healthScore * 0.6 + horsepower * 0.3; }
	constexpr double FOR_CLEAN_PULL = 3.0;
}
