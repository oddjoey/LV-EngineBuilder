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
	VALVE_SPRING
};

enum class PartQuality { BUDGET, OEM, PERFORMANCE };
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
	std::vector<Feature>					m_features = {};
	std::unordered_map<std::string, FeatureReading> m_readings = {};
};

typedef std::shared_ptr<Part> pPart;
