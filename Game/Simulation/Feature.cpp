#include "Feature.h"

#include <algorithm>
#include <cmath>

std::string DescribeIssues(uint32_t issues, FeatureKind kind)
{
	if (issues == FeatureIssue::NONE)
		return "In spec";

	bool force = kind == FeatureKind::FORCE;
	bool length = kind == FeatureKind::LENGTH || kind == FeatureKind::THICKNESS || kind == FeatureKind::VOLUME;
	const std::pair<uint32_t, const char*> names[] = {
		{ FeatureIssue::UNDERSIZE, force ? "Weak" : length ? "Under spec" : "Undersize" },
		{ FeatureIssue::OVERSIZE, force ? "Too stiff" : length ? "Over spec" : "Oversize" },
		{ FeatureIssue::TAPER, "Taper" },
		{ FeatureIssue::OUT_OF_ROUND, "Out of round" },
		{ FeatureIssue::ROUGH, "Rough finish" },
		{ FeatureIssue::GLAZED, "Glazed" },
		{ FeatureIssue::WARPED, "Warped" },
		{ FeatureIssue::BEYOND_LIMITS, "Beyond limits" },
		{ FeatureIssue::RUNOUT, "Runout" },
	};

	std::string description;
	for (const auto& [flag, name] : names)
		if (issues & flag)
		{
			if (!description.empty())
				description += ", ";
			description += name;
		}
	return description;
}

bool Feature::IsRound() const
{
	return kind == FeatureKind::BORE || kind == FeatureKind::JOURNAL;
}

bool Feature::HasFinishSpec() const
{
	return minRa > 0.0 || maxRa > 0.0;
}

bool Feature::SupportsOperation(MachineOperation operation) const
{
	return std::find(operations.begin(), operations.end(), operation) != operations.end();
}

double Feature::Size() const
{
	if (!IsRound())
		return value;

	double sum = 0.0;
	for (const auto& station : samples)
		for (double sample : station)
			sum += sample;
	return sum / (STATIONS * AXES);
}

double Feature::MinSample() const
{
	if (!IsRound())
		return value;

	double result = samples[0][0];
	for (const auto& station : samples)
		for (double sample : station)
			result = std::min(result, sample);
	return result;
}

double Feature::MaxSample() const
{
	if (!IsRound())
		return value;

	double result = samples[0][0];
	for (const auto& station : samples)
		for (double sample : station)
			result = std::max(result, sample);
	return result;
}

double Feature::Taper() const
{
	if (!IsRound())
		return 0.0;

	double taper = 0.0;
	for (int axis = 0; axis < AXES; axis++)
	{
		double low = samples[0][axis], high = samples[0][axis];
		for (int station = 1; station < STATIONS; station++)
		{
			low = std::min(low, samples[station][axis]);
			high = std::max(high, samples[station][axis]);
		}
		taper = std::max(taper, high - low);
	}
	return taper;
}

double Feature::OutOfRound() const
{
	if (!IsRound())
		return 0.0;

	double outOfRound = 0.0;
	for (const auto& station : samples)
		outOfRound = std::max(outOfRound, std::abs(station[0] - station[1]));
	return outOfRound;
}

void Feature::SetUniform(double size)
{
	for (auto& station : samples)
		for (double& sample : station)
			sample = size;
	value = size;
}

void Feature::Offset(double delta)
{
	for (auto& station : samples)
		for (double& sample : station)
			sample += delta;
	value += delta;
}

std::optional<double> Feature::MatchSizeClass() const
{
	double size = Size();
	for (double sizeClass : sizeClasses)
		if (spec.Contains(size, sizeClass))
			return sizeClass;
	return std::nullopt;
}

uint32_t Feature::Evaluate(bool includeFinish) const
{
	uint32_t issues = FeatureIssue::NONE;

	if (kind == FeatureKind::FLATNESS)
	{
		if (value > spec.Max())
			issues |= FeatureIssue::WARPED;
	}
	else if (kind == FeatureKind::RUNOUT)
	{
		if (value > spec.Max())
			issues |= FeatureIssue::RUNOUT;
	}
	else if (!MatchSizeClass())
	{
		double size = Size();
		auto [smallestClass, largestClass] = std::minmax_element(sizeClasses.begin(), sizeClasses.end());

		// Judge against the nearest class so a bore halfway between +0.25 and +0.50 reads as
		// oversize for +0.25 rather than undersize for +0.50.
		double nearest = *smallestClass;
		for (double sizeClass : sizeClasses)
			if (std::abs(size - spec.Mid() - sizeClass) < std::abs(size - spec.Mid() - nearest))
				nearest = sizeClass;

		issues |= size < spec.Min() + nearest ? FeatureIssue::UNDERSIZE : FeatureIssue::OVERSIZE;

		if ((*largestClass > 0.0 && size > spec.Max() + *largestClass) ||
			(*smallestClass < 0.0 && size < spec.Min() + *smallestClass))
			issues |= FeatureIssue::BEYOND_LIMITS;
	}

	if (IsRound())
	{
		if (maxTaper > 0.0 && Taper() > maxTaper)
			issues |= FeatureIssue::TAPER;
		if (maxOutOfRound > 0.0 && OutOfRound() > maxOutOfRound)
			issues |= FeatureIssue::OUT_OF_ROUND;
	}

	if (includeFinish)
	{
		if (maxRa > 0.0 && surfaceRa > maxRa)
			issues |= FeatureIssue::ROUGH;
		if (minRa > 0.0 && surfaceRa < minRa)
			issues |= FeatureIssue::GLAZED;
	}

	return issues;
}
