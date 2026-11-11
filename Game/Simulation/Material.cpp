#include "Material.h"

#include <stdexcept>

const Material& GetMaterial(MaterialType type)
{
	static const Material materials[] = {
		{ "Grey Cast Iron",				7.20, 11.0e-6, 200.0, 1.00, MaterialTier::STANDARD },
		{ "A356-T6 Aluminium",			2.68, 21.5e-6,  80.0, 1.30, MaterialTier::STANDARD },
		{ "Nodular Iron",				7.10, 11.5e-6, 250.0, 0.90, MaterialTier::UPRATED },
		{ "4340 Forged Steel",			7.85, 12.3e-6, 300.0, 0.70, MaterialTier::PERFORMANCE },
		{ "Powder Metal Steel",			7.60, 12.0e-6, 260.0, 0.80, MaterialTier::STANDARD },
		{ "Cast Aluminium (A332)",		2.70, 21.0e-6, 110.0, 1.20, MaterialTier::STANDARD },
		{ "Hypereutectic Aluminium",	2.68, 18.0e-6, 120.0, 1.00, MaterialTier::UPRATED },
		{ "2618 Forged Aluminium",		2.76, 23.0e-6, 115.0, 1.10, MaterialTier::PERFORMANCE },
		{ "Ductile Iron",				7.10, 11.5e-6, 300.0, 0.80, MaterialTier::UPRATED },
		{ "Aluminium-Tin Bimetal",		3.10, 20.0e-6,  40.0, 1.20, MaterialTier::UPRATED },
		{ "MLS Stainless Steel",		7.90, 17.3e-6, 350.0, 0.60, MaterialTier::UPRATED },
		{ "Hardened Steel",				7.85, 11.5e-6, 550.0, 0.40, MaterialTier::UPRATED },
		{ "Stamped Steel",				7.85, 12.0e-6, 120.0, 1.00, MaterialTier::STANDARD },
		{ "FKM Rubber",					1.85, 160.0e-6,  0.0, 0.00, MaterialTier::STANDARD },
		{ "Glass-filled Nylon",			1.40, 30.0e-6,  15.0, 0.00, MaterialTier::STANDARD },
		{ "Rubber-coated Composite",	1.60, 40.0e-6,  10.0, 0.00, MaterialTier::STANDARD },
		{ "Nickel Alloy & Ceramic",		4.50, 8.0e-6,  400.0, 0.00, MaterialTier::STANDARD },
		{ "HNBR Rubber, Glass Cord",	1.30, 120.0e-6,  0.0, 0.00, MaterialTier::STANDARD },
		{ "Laminated Glass",			2.50, 9.0e-6,    0.0, 0.00, MaterialTier::STANDARD },
		{ "Semi-metallic Friction",		2.60, 15.0e-6,  20.0, 0.00, MaterialTier::STANDARD },
		{ "ABS Plastic",				1.05, 90.0e-6,   0.0, 0.00, MaterialTier::STANDARD },
		{ "Lead-acid",					3.00, 29.0e-6,   0.0, 0.00, MaterialTier::STANDARD },
		{ "Steel-belted Rubber",		1.20, 150.0e-6,  0.0, 0.00, MaterialTier::STANDARD },
		{ "PVC-insulated Copper",		4.00, 17.0e-6,   0.0, 0.00, MaterialTier::STANDARD },
	};

	auto index = static_cast<size_t>(type);
	if (index >= std::size(materials))
		throw std::runtime_error("Unknown material!");
	return materials[index];
}

const char* MaterialTierName(MaterialTier tier)
{
	switch (tier)
	{
		case MaterialTier::STANDARD:	return "Standard";
		case MaterialTier::UPRATED:		return "Uprated";
		case MaterialTier::PERFORMANCE:	return "Performance";
	}
	return "";
}

const char* MaterialTierMeaning(MaterialTier tier)
{
	switch (tier)
	{
		case MaterialTier::STANDARD:	return "the usual for this part: fine in a stock engine at stock power.";
		case MaterialTier::UPRATED:		return "stronger and harder-wearing than the basic stuff: a better grade for the job.";
		case MaterialTier::PERFORMANCE:	return "forged: the strength to take more revs and more power, and the price to match.";
	}
	return "";
}
