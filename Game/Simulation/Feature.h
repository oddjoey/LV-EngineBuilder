#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Tolerance.h"

enum class FeatureKind
{
	BORE,			// internal diameter, sampled at several stations
	JOURNAL,		// external diameter, sampled at several stations
	FLATNESS,		// deviation from a true plane
	LENGTH,
	THICKNESS,
	VOLUME,			// cc
	RUNOUT,			// total indicated runout of a face or seat
	FORCE			// newtons
};

enum class MachineOperation
{
	BORE,
	HONE,
	GRIND,
	POLISH,
	SURFACE,
	FILE_RING,
	REAM,			// valve guides
	CUT_SEAT,		// valve seats
	FACE_VALVE		// valve faces
};

namespace FeatureIssue
{
	constexpr uint32_t NONE				= 0;
	constexpr uint32_t UNDERSIZE		= 1 << 0;
	constexpr uint32_t OVERSIZE			= 1 << 1;
	constexpr uint32_t TAPER			= 1 << 2;
	constexpr uint32_t OUT_OF_ROUND		= 1 << 3;
	constexpr uint32_t ROUGH			= 1 << 4;
	constexpr uint32_t GLAZED			= 1 << 5;
	constexpr uint32_t WARPED			= 1 << 6;
	constexpr uint32_t BEYOND_LIMITS	= 1 << 7; // past the last oversize/undersize class: scrap or sleeve
	constexpr uint32_t RUNOUT			= 1 << 8;
}

// kind tunes the wording: an undersize spring force reads as "Weak", not "Undersize".
std::string DescribeIssues(uint32_t issues, FeatureKind kind = FeatureKind::LENGTH);

// One measurable, machinable surface of a part. Round features are sampled the way an
// engine builder checks them: top/middle/bottom, each on two axes 90 degrees apart, so
// taper and out-of-round fall out of the samples.
struct Feature
{
	static constexpr int STATIONS = 3;
	static constexpr int AXES = 2;

	std::string name;
	FeatureKind kind = FeatureKind::LENGTH;
	Tolerance spec = {};
	// Offsets a machinist may legitimately cut to (e.g. +0.25 bores, -0.25 journals).
	std::vector<double> sizeClasses = { 0.0 };
	double maxTaper = 0.0;			// 0 = not checked
	double maxOutOfRound = 0.0;		// 0 = not checked
	double minRa = 0.0;				// 0 = not checked
	double maxRa = 0.0;				// 0 = not checked
	bool directlyMeasurable = true;
	std::vector<MachineOperation> operations = {};

	// True geometry. The player only ever sees it through a measuring tool.
	double samples[STATIONS][AXES] = {};
	double value = 0.0;
	double surfaceRa = 0.0;
	// The last machine to cut this surface, which decides what it looks like in the shop.
	std::optional<MachineOperation> lastOperation = std::nullopt;

	bool IsRound() const;
	bool HasFinishSpec() const;
	bool SupportsOperation(MachineOperation operation) const;

	double Size() const;
	double MinSample() const;
	double MaxSample() const;
	double Taper() const;
	double OutOfRound() const;

	void SetUniform(double size);
	void Offset(double delta);

	// The size class whose tolerance window contains the current size, if any.
	std::optional<double> MatchSizeClass() const;
	uint32_t Evaluate(bool includeFinish = true) const;
};
