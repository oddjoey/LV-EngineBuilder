#pragma once

enum class MaterialType
{
	GREY_IRON,
	ALUMINUM_A356,
	NODULAR_IRON,
	FORGED_4340,
	POWDER_METAL,
	CAST_ALUMINUM,
	HYPEREUTECTIC_ALUMINUM,
	FORGED_2618,
	DUCTILE_IRON,
	BIMETAL,
	MLS_STEEL,
	// Ancillaries
	HARDENED_STEEL,
	STAMPED_STEEL,
	FKM_RUBBER,
	NYLON,
	COMPOSITE_GASKET,
	SPARK_PLUG_ALLOY,
	HNBR_BELT,			// timing belts: appended, so saved materials keep their numbers
};

struct Material
{
	const char* name;
	double density;			// g/cc
	double expansion;		// linear coefficient of thermal expansion, 1/degC
	double hardness;		// Brinell
	double machinability;	// 1.0 = grey iron; higher cuts more predictably
};

const Material& GetMaterial(MaterialType type);
