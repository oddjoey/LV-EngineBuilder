#include "Random.h"

std::mt19937_64& Random::Generator()
{
	static std::mt19937_64 generator{ std::random_device{}() };
	return generator;
}

void Random::Seed(uint64_t seed)
{
	Generator().seed(seed);
}

double Random::Normal(double mean, double sigma)
{
	if (sigma <= 0.0)
		return mean;
	return std::normal_distribution<double>(mean, sigma)(Generator());
}

double Random::Uniform(double min, double max)
{
	if (max <= min)
		return min;
	return std::uniform_real_distribution<double>(min, max)(Generator());
}

bool Random::Chance(double probability)
{
	return Uniform(0.0, 1.0) < probability;
}
