#include "Part.h"

#include "PartCatalog.h"

uint32_t Part::s_nextId = 1;

const char* PartTypeName(PartType type)
{
	return PartCatalog::Get(type).name;
}

const char* PartQualityName(PartQuality quality)
{
	switch (quality)
	{
		case PartQuality::BUDGET:		return "Budget";
		case PartQuality::OEM:			return "OEM";
		case PartQuality::PERFORMANCE:	return "Performance";
	}
	return "Unknown";
}

const char* PartConditionName(PartCondition condition)
{
	switch (condition)
	{
		case PartCondition::NEW:		return "New";
		case PartCondition::USED:		return "Used";
		case PartCondition::DAMAGED:	return "Damaged";
	}
	return "Unknown";
}

Part::Part(PartType type, const std::string& name, MaterialType material, PartQuality quality, PartCondition condition)
	: m_id(s_nextId++), m_type(type), m_name(name), m_material(material), m_quality(quality), m_condition(condition)
{
}

Part::~Part()
{
}

uint32_t Part::GetId() const
{
	return m_id;
}

PartType Part::GetType() const
{
	return m_type;
}

const std::string& Part::GetName() const
{
	return m_name;
}

MaterialType Part::GetMaterial() const
{
	return m_material;
}

PartQuality Part::GetQuality() const
{
	return m_quality;
}

PartCondition Part::GetCondition() const
{
	return m_condition;
}

double Part::GetSizeOffset() const
{
	return m_sizeOffset;
}

double Part::GetValue() const
{
	return m_condition == PartCondition::DAMAGED ? m_value * 0.1 : m_value;
}

void Part::MarkDamaged(const std::string& reason)
{
	m_condition = PartCondition::DAMAGED;
	m_damageReason = reason;
}

const std::string& Part::GetDamageReason() const
{
	return m_damageReason;
}

double Part::GetTorque() const
{
	return m_torque;
}

void Part::SetTorque(double newtonMetres)
{
	m_torque = newtonMetres;
}

FastenerTightness Part::GetTightness() const
{
	return m_tightness;
}

void Part::SetTightness(FastenerTightness tightness)
{
	m_tightness = tightness;
}

uint32_t Part::GetMatchedTo() const
{
	return m_matchedTo;
}

int Part::GetMatchedPosition() const
{
	return m_matchedPosition;
}

void Part::MatchTo(uint32_t partId, int position)
{
	m_matchedTo = partId;
	m_matchedPosition = position;
}

std::vector<Feature>& Part::GetFeatures()
{
	return m_features;
}

const std::vector<Feature>& Part::GetFeatures() const
{
	return m_features;
}

Feature* Part::FindFeature(const std::string& name)
{
	for (auto& feature : m_features)
		if (feature.name == name)
			return &feature;
	return nullptr;
}

const Feature* Part::FindFeature(const std::string& name) const
{
	for (const auto& feature : m_features)
		if (feature.name == name)
			return &feature;
	return nullptr;
}

void Part::RecordReading(const std::string& featureName, const FeatureReading& reading)
{
	m_readings[featureName] = reading;
}

const FeatureReading* Part::GetReading(const std::string& featureName) const
{
	auto it = m_readings.find(featureName);
	return it == m_readings.end() ? nullptr : &it->second;
}

void Part::MarkReadingStale(const std::string& featureName)
{
	auto it = m_readings.find(featureName);
	if (it != m_readings.end())
		it->second.stale = true;
}
