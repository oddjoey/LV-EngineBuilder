#include "Units.h"

#include <format>

namespace Units
{
	std::string FormatSize(double mm, System system)
	{
		if (system == System::IMPERIAL)
			return std::format("{:.4f} in", ToInches(mm));
		return std::format("{:.3f} mm", mm);
	}

	std::string FormatDeviation(double mm, System system)
	{
		if (system == System::IMPERIAL)
			return std::format("{:.2f} thou", ToThou(mm));
		return std::format("{:.3f} mm", mm);
	}

	std::string FormatVolume(double cc)
	{
		return std::format("{:.1f} cc", cc);
	}

	std::string FormatFinish(double ra)
	{
		return std::format("{:.2f} um Ra", ra);
	}

	std::string FormatForce(double newtons)
	{
		return std::format("{:.0f} N", newtons);
	}

	double ToDisplay(double mm, System system)
	{
		return system == System::IMPERIAL ? ToInches(mm) : mm;
	}

	double FromDisplay(double value, System system)
	{
		return system == System::IMPERIAL ? Inches(value) : value;
	}
}
