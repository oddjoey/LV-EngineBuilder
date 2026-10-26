#pragma once

#include <cstdint>
#include <random>

// Shared source of manufacturing and measurement variance. Seed it to make a run reproducible.
class Random
{
public:
	static void Seed(uint64_t seed);
	static double Normal(double mean, double sigma);
	static double Uniform(double min, double max);
	static bool Chance(double probability);
private:
	static std::mt19937_64& Generator();
};
