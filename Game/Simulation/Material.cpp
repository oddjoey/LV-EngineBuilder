#include "Material.h"

#include <stdexcept>

const Material& GetMaterial(MaterialType type)
{
	static const Material materials[] = {
		{ "Grey Cast Iron",				7.20, 11.0e-6, 200.0, 1.00 },
		{ "A356-T6 Aluminium",			2.68, 21.5e-6,  80.0, 1.30 },
		{ "Nodular Iron",				7.10, 11.5e-6, 250.0, 0.90 },
		{ "4340 Forged Steel",			7.85, 12.3e-6, 300.0, 0.70 },
		{ "Powder Metal Steel",			7.60, 12.0e-6, 260.0, 0.80 },
		{ "Cast Aluminium (A332)",		2.70, 21.0e-6, 110.0, 1.20 },
		{ "Hypereutectic Aluminium",	2.68, 18.0e-6, 120.0, 1.00 },
		{ "2618 Forged Aluminium",		2.76, 23.0e-6, 115.0, 1.10 },
		{ "Ductile Iron",				7.10, 11.5e-6, 300.0, 0.80 },
		{ "Aluminium-Tin Bimetal",		3.10, 20.0e-6,  40.0, 1.20 },
		{ "MLS Stainless Steel",		7.90, 17.3e-6, 350.0, 0.60 },
	};

	auto index = static_cast<size_t>(type);
	if (index >= std::size(materials))
		throw std::runtime_error("Unknown material!");
	return materials[index];
}
