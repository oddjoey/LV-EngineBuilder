#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Feature.h"
#include "Material.h"

enum class PartType
{
	BLOCK,
	CRANKSHAFT,
	CONNECTING_ROD,
	PISTON,
	PISTON_RING,
	MAIN_BEARING,
	ROD_BEARING,
	CYLINDER_HEAD,
	HEAD_GASKET,
	CAMSHAFT,
	INTAKE_VALVE,
	EXHAUST_VALVE,
	VALVE_SPRING,

	// Everything else that makes a running engine. Appended so saved part types keep their numbers;
	// each one up to MAIN_CAP also has an assembly slot of the same number (see AssemblySlot). PartCatalog
	// describes them all.
	CORE_PLUG,
	REAR_MAIN_SEAL,
	OIL_PUMP,
	OIL_PICKUP,
	OIL_PAN_GASKET,
	OIL_PAN,
	OIL_DRAIN_PLUG,
	OIL_FILTER,
	VALVE_STEM_SEAL,
	VALVE_RETAINER,
	LIFTER,
	CRANK_SPROCKET,
	CAM_SPROCKET,
	TIMING_BELT,
	BELT_IDLER,
	BELT_TENSIONER,
	TIMING_COVER_GASKET,
	TIMING_COVER,
	FRONT_CRANK_SEAL,
	CRANK_PULLEY,
	VALVE_COVER_GASKET,
	VALVE_COVER,
	SPARK_PLUG,
	IGNITION_COIL,
	INTAKE_MANIFOLD_GASKET,
	INTAKE_MANIFOLD,
	THROTTLE_BODY,
	FUEL_INJECTOR,
	FUEL_RAIL,
	EXHAUST_MANIFOLD_GASKET,
	EXHAUST_MANIFOLD,
	WATER_PUMP_GASKET,
	WATER_PUMP,
	THERMOSTAT,
	THERMOSTAT_HOUSING,
	// The other halves: bearings are fitted as an upper and a lower shell, and the rods and main
	// journals are closed by caps machined together with their rod or block.
	MAIN_BEARING_LOWER,
	ROD_BEARING_LOWER,
	ROD_CAP,
	MAIN_CAP,
	// Nuts and bolts by size. One size fits every hole it threads into; each hole has its own torque
	// (see PartCatalog::Holes).
	BOLT_M6,
	BOLT_M8,
	NUT_M8,
	BOLT_M10,
	BOLT_M12,
	BOLT_M14,
	// Bolted over the cam journals, line-bored with their head (appended later, so not with the
	// other caps).
	CAM_CAP,

	COUNT
};

enum class PartQuality { BUDGET, OEM, PERFORMANCE };
// A nut or bolt: in its hole finger tight, snugged by feel, or done up with a torque wrench.
enum class FastenerTightness { LOOSE, SNUG, TORQUED };
enum class PartCondition { NEW, USED, DAMAGED };

const char* PartTypeName(PartType type);
const char* PartQualityName(PartQuality quality);
const char* PartConditionName(PartCondition condition);

namespace FeatureNames
{
	inline std::string Bore(int cylinder) { return "Bore " + std::to_string(cylinder + 1); }
	inline std::string MainHousing(int journal) { return "Main Housing " + std::to_string(journal + 1); }
	inline std::string MainJournal(int journal) { return "Main Journal " + std::to_string(journal + 1); }
	inline std::string RodJournal(int cylinder) { return "Rod Journal " + std::to_string(cylinder + 1); }
	inline std::string Chamber(int cylinder) { return "Chamber " + std::to_string(cylinder + 1) + " Volume"; }

	// Valvetrain: one intake and one exhaust valve per cylinder.
	inline std::string ValveSide(bool exhaust) { return exhaust ? "Exhaust" : "Intake"; }
	inline std::string Guide(int cylinder, bool exhaust) { return ValveSide(exhaust) + " Guide " + std::to_string(cylinder + 1); }
	inline std::string SeatWidth(int cylinder, bool exhaust) { return ValveSide(exhaust) + " Seat " + std::to_string(cylinder + 1) + " Width"; }
	inline std::string SeatRunout(int cylinder, bool exhaust) { return ValveSide(exhaust) + " Seat " + std::to_string(cylinder + 1) + " Runout"; }
	inline std::string SeatRecession(int cylinder, bool exhaust) { return ValveSide(exhaust) + " Seat " + std::to_string(cylinder + 1) + " Recession"; }
	inline std::string CamBore(int journal) { return "Cam Bore " + std::to_string(journal + 1); }
	inline std::string CamJournal(int journal) { return "Cam Journal " + std::to_string(journal + 1); }
	inline std::string LobeLift(int cylinder, bool exhaust) { return ValveSide(exhaust) + " Lobe " + std::to_string(cylinder + 1) + " Lift"; }

	inline const std::string STEM = "Stem Diameter";
	inline const std::string MARGIN = "Margin";
	inline const std::string FACE_RUNOUT = "Face Runout";
	inline const std::string VALVE_LENGTH = "Overall Length";
	inline const std::string SEAT_SINK = "Seat Sink";
	inline const std::string FREE_LENGTH = "Free Length";
	inline const std::string SEAT_PRESSURE = "Seat Pressure";
	inline const std::string SPRING_RATE = "Spring Rate";

	inline const std::string DECK_FLATNESS = "Deck Flatness";
	inline const std::string DECK_HEIGHT = "Deck Height";
	inline const std::string STROKE = "Stroke";
	inline const std::string BIG_END = "Big End Bore";
	inline const std::string CENTER_LENGTH = "Center Length";
	inline const std::string SKIRT = "Skirt Diameter";
	inline const std::string COMPRESSION_HEIGHT = "Compression Height";
	inline const std::string DISH_VOLUME = "Dish Volume";
	inline const std::string CLOSED_DIAMETER = "Closed Diameter";
	inline const std::string WALL_THICKNESS = "Wall Thickness";
	inline const std::string COMPRESSED_THICKNESS = "Compressed Thickness";

	// Ancillaries
	inline const std::string ROTOR_TIP_CLEARANCE = "Rotor Tip Clearance";
	inline const std::string ROTOR_SIDE_CLEARANCE = "Rotor Side Clearance";
	inline const std::string RAIL_FLATNESS = "Sealing Rail Flatness";
	inline const std::string FLANGE_FLATNESS = "Flange Flatness";
	inline const std::string FACE_FLATNESS = "Sealing Face Flatness";
	inline const std::string BUCKET_DIAMETER = "Bucket Diameter";
	inline const std::string ROOT_DIAMETER = "Tooth Root Diameter";
	inline const std::string BELT_TOOTH_HEIGHT = "Tooth Height";
	inline const std::string IDLER_PLAY = "Bearing Play";
	inline const std::string TENSIONER_TRAVEL = "Tensioner Travel";
	inline const std::string RING_RUNOUT = "Damper Ring Runout";
	inline const std::string ELECTRODE_GAP = "Electrode Gap";
	inline const std::string THROTTLE_BORE = "Throttle Bore";
	inline const std::string INJECTOR_FLOW = "Flow in 15 s";
	inline const std::string SHAFT_PLAY = "Bearing Play";
}

// What the player has learned about a feature. Kept separate from the true geometry so the
// UI can only ever show what was actually measured, with the tool's resolution and error.
struct FeatureReading
{
	std::string toolName = "";
	bool hasSize = false;
	double samples[Feature::STATIONS][Feature::AXES] = {};
	double value = 0.0;
	std::optional<double> finishRa = std::nullopt;
	bool stale = false; // the feature has been machined since this reading was taken
};

class Part
{
	friend class PartFactory;
	friend class SaveGame;
public:
	Part(PartType type, const std::string& name, MaterialType material, PartQuality quality, PartCondition condition);
	~Part();

	uint32_t GetId() const;
	PartType GetType() const;
	const std::string& GetName() const;
	MaterialType GetMaterial() const;
	PartQuality GetQuality() const;
	PartCondition GetCondition() const;

	// Oversize for pistons and rings, journal undersize for bearings. 0 = standard.
	double GetSizeOffset() const;
	double GetValue() const;

	void MarkDamaged(const std::string& reason);
	const std::string& GetDamageReason() const;

	// Caps are machined in place with their rod or block and only ever fit that one (and a main
	// cap only its own journal). 0 / -1 when the part isn't one of a matched set.
	// Fasteners: how tight it was done up (Nm), 0
	// while it's loose. The player only knows what the wrench said, not this.
	double GetTorque() const;
	void SetTorque(double newtonMetres);
	// How it was done up: finger tight, snugged with a ratchet or wrench, or torqued to spec.
	FastenerTightness GetTightness() const;
	void SetTightness(FastenerTightness tightness);

	uint32_t GetMatchedTo() const;
	int GetMatchedPosition() const;
	void MatchTo(uint32_t partId, int position = -1);

	std::vector<Feature>& GetFeatures();
	const std::vector<Feature>& GetFeatures() const;
	Feature* FindFeature(const std::string& name);
	const Feature* FindFeature(const std::string& name) const;

	void RecordReading(const std::string& featureName, const FeatureReading& reading);
	const FeatureReading* GetReading(const std::string& featureName) const;
	void MarkReadingStale(const std::string& featureName);
private:
	static uint32_t s_nextId;

	uint32_t								m_id = 0;
	PartType								m_type;
	std::string								m_name = "";
	MaterialType							m_material;
	PartQuality								m_quality;
	PartCondition							m_condition;
	double									m_sizeOffset = 0.0;
	double									m_value = 0.0;
	std::string								m_damageReason = "";
	uint32_t								m_matchedTo = 0;
	int										m_matchedPosition = -1;
	double									m_torque = 0.0;
	FastenerTightness						m_tightness = FastenerTightness::LOOSE;
	std::vector<Feature>					m_features = {};
	std::unordered_map<std::string, FeatureReading> m_readings = {};
};

typedef std::shared_ptr<Part> pPart;
