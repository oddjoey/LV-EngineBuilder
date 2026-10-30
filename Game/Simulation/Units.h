#pragma once

#include <string>

// Every length in the simulation is stored in millimetres, volumes in cubic centimetres,
// surface finish in micrometres Ra and temperatures in degrees Celsius. Conversion only
// happens at the UI boundary.
namespace Units
{
	constexpr double MM_PER_INCH = 25.4;

	constexpr double Inches(double inches) { return inches * MM_PER_INCH; }

	constexpr double ToInches(double mm) { return mm / MM_PER_INCH; }
	constexpr double ToThou(double mm) { return mm / MM_PER_INCH * 1000.0; }

	enum class System { METRIC, IMPERIAL };

	// A size such as a bore diameter, at the precision a machinist would quote it.
	std::string FormatSize(double mm, System system);
	// A small difference such as a clearance, taper or ring gap.
	std::string FormatDeviation(double mm, System system);
	std::string FormatVolume(double cc);
	std::string FormatFinish(double ra);
	std::string FormatForce(double newtons);

	// Converts between millimetres and the unit the player is typing in.
	double ToDisplay(double mm, System system);
	double FromDisplay(double value, System system);
}
