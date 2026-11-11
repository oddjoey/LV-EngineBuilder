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
	// The car's (appended in turn)
	LAMINATED_GLASS,
	FRICTION_LINING,
	ABS_PLASTIC,
	LEAD_ACID,
	TYRE_RUBBER,
	COPPER_WIRING,
};

// How good a material is for its job: standard (cast, stamped, the usual), uprated (stronger alloys and irons, or
// a better grade of the usual) and performance (forged, for more revs and power).
enum class MaterialTier { STANDARD, UPRATED, PERFORMANCE };
const char* MaterialTierName(MaterialTier tier);
// What it means for a part made of it, in a sentence.
const char* MaterialTierMeaning(MaterialTier tier);

struct Material
{
	const char* name;
	double density;			// g/cc
	double expansion;		// linear coefficient of thermal expansion, 1/degC
	double hardness;		// Brinell
	double machinability;	// 1.0 = grey iron; higher cuts more predictably
	MaterialTier tier;
};

const Material& GetMaterial(MaterialType type);
