#pragma once

// A toleranced dimension: a nominal size plus signed lower/upper deviations.
// e.g. a bore of 86.000 +0.018/-0.000 is { 86.000, 0.000, 0.018 }.
struct Tolerance
{
	double nominal = 0.0;
	double lower = 0.0;
	double upper = 0.0;

	static Tolerance Bilateral(double nominal, double plusMinus) { return { nominal, -plusMinus, plusMinus }; }
	static Tolerance Limits(double min, double max) { return { min, 0.0, max - min }; }

	double Min() const { return nominal + lower; }
	double Max() const { return nominal + upper; }
	double Mid() const { return nominal + (lower + upper) * 0.5; }
	double Width() const { return upper - lower; }

	// offset shifts the whole window, e.g. +0.50 for an overbored cylinder.
	bool Contains(double value, double offset = 0.0) const { return value >= Min() + offset && value <= Max() + offset; }
	Tolerance Shifted(double offset) const { return { nominal + offset, lower, upper }; }
};

// A plain acceptable range, used for assembly targets such as oil clearance.
struct Range
{
	double min = 0.0;
	double max = 0.0;

	bool Contains(double value) const { return value >= min && value <= max; }
	Range Shifted(double offset) const { return { min + offset, max + offset }; }
};
