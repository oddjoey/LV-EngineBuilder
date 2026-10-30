// Measuring with hand tools, moving equipment, and the HUD.

#include "ShopWorld.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "LV/Engine/Objects/ObjectManager.h"
#include "LV/Engine/Utility/InputManager.h"
#include "../UI/ItemIcons.h"
#include "../FrameProfiler.h"
#include "ShopWorldShared.h"
#include "ToolShapes.h"

using namespace ShopLayout;

namespace
{
	std::string FormatFeatureValue(const Feature& feature, double value, Units::System units)
	{
		switch (feature.kind)
		{
			case FeatureKind::FLATNESS:
			case FeatureKind::RUNOUT:	return Units::FormatDeviation(value, units);
			case FeatureKind::VOLUME:	return Units::FormatVolume(value);
			case FeatureKind::FORCE:	return Units::FormatForce(value);
			default:					return Units::FormatSize(value, units);
		}
	}
}

void ShopWorld::Cue(Sound sound, const glm::vec3& position, float volume)
{
	m_soundCues.push_back({ sound, position, volume, true });
}

std::vector<SoundCue> ShopWorld::TakeSoundCues()
{
	return std::exchange(m_soundCues, {});
}

bool ShopWorld::IsDoorMoving() const
{
	return m_doorMoving;
}

glm::vec3 ShopWorld::GetDoorPosition() const
{
	// Whichever is rolling: the extension's door if it is, otherwise the shop's.
	bool bayRolling = m_bayBuilt && m_bayDoorStation < m_stations.size() && m_bayDoorAmount != (m_bayDoorOpen ? 1.f : 0.f);
	return m_stations[bayRolling ? m_bayDoorStation : m_doorStation].position + glm::vec3(0.f, 2.f, 0.f);
}

bool ShopWorld::IsLiftMoving() const
{
	return m_liftMoving;
}

glm::vec3 ShopWorld::GetLiftPosition() const
{
	return m_liftArms ? m_liftArms->GetPosition() : glm::vec3(0.f);
}

float ShopWorld::GetEngineRpm() const
{
	return m_engineRpm;
}

glm::vec3 ShopWorld::GetDynoPosition() const
{
	const Station* dyno = FindStation(StationType::DYNO);
	return dyno ? dyno->position + glm::vec3(0.f, 0.9f, 0.f) : glm::vec3(0.f);
}

std::optional<std::string> ShopWorld::MeasureBlockedBy(const Workshop& workshop, const ItemVisual& visual, const std::string& feature) const
{
	if (visual.place != ItemPlace::STAND || !visual.part)
		return std::nullopt;
	// Only the block is measured on the stand; anything else fitted to the engine comes off first
	// (a crank's journals are inside its bearings, a piston's skirt inside its bore).
	if (visual.part->GetType() != PartType::BLOCK)
		return std::format("Take the {} off the engine to measure it.", visual.part->GetName());

	// The block, where nothing fitted covers the spot.
	const auto& assembly = workshop.GetAssembly(visual.engineIndex);
	const auto& spec = assembly.GetSpec();
	auto inTheWay = [&](std::initializer_list<std::pair<AssemblySlot, int>> covers) -> std::optional<std::string> {
		for (const auto& [slot, index] : covers)
			if (auto part = assembly.Get(slot, index))
				return std::format("The {} is in the way: take it out to measure the {}.", part->GetName(), feature);
		return std::nullopt;
	};
	for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
		if (feature == FeatureNames::Bore(cylinder))
			return inTheWay({ { AssemblySlot::CYLINDER_HEAD, 0 }, { AssemblySlot::PISTON, cylinder } });
	for (int journal = 0; journal < spec.mainJournals; journal++)
		if (feature == FeatureNames::MainHousing(journal))
			return inTheWay({ { AssemblySlot::CRANKSHAFT, 0 }, { AssemblySlot::MAIN_BEARING, journal }, { AssemblySlot::MAIN_BEARING_LOWER, journal } });
	if (feature == FeatureNames::DECK_FLATNESS || feature == FeatureNames::DECK_HEIGHT)
		return inTheWay({ { AssemblySlot::CYLINDER_HEAD, 0 }, { AssemblySlot::HEAD_GASKET, 0 } });
	return std::nullopt;
}

namespace
{
	// What a Plastigauge strip on a journal sits on and under. A cam runs straight in the head, with
	// no shells: COUNT stands for "none".
	struct JournalParts
	{
		AssemblySlot shaft, upper, lower, cap;
		const char* name;		// "main", "rod" or "cam"
	};
	JournalParts PartsOver(AssemblyCheck check, bool rod)
	{
		if (check == AssemblyCheck::CAM_CLEARANCE)
			return { AssemblySlot::CAMSHAFT, AssemblySlot::COUNT, AssemblySlot::COUNT, AssemblySlot::CAM_CAP, "cam" };
		return rod ? JournalParts{ AssemblySlot::CRANKSHAFT, AssemblySlot::ROD_BEARING, AssemblySlot::ROD_BEARING_LOWER, AssemblySlot::ROD_CAP, "rod" }
			: JournalParts{ AssemblySlot::CRANKSHAFT, AssemblySlot::MAIN_BEARING, AssemblySlot::MAIN_BEARING_LOWER, AssemblySlot::MAIN_CAP, "main" };
	}
	bool Fitted(const EngineAssembly& assembly, AssemblySlot slot, int index)
	{
		return slot != AssemblySlot::COUNT && assembly.Get(slot, index);
	}

	// Every bolt or nut holding the cap down, done up to spec.
	bool CapTorqued(const EngineAssembly& assembly, AssemblySlot cap, int index)
	{
		PartType capType = EngineAssembly::SlotPartType(cap);
		for (int slotIndex = 0; slotIndex < static_cast<int>(AssemblySlot::COUNT); slotIndex++)
		{
			auto slot = static_cast<AssemblySlot>(slotIndex);
			const auto* holes = EngineAssembly::Holes(slot);
			if (!holes || holes->parent != capType)
				continue;
			for (int hole = index * holes->perParent; hole < (index + 1) * holes->perParent; hole++)
			{
				auto fastener = assembly.Get(slot, hole);
				if (!fastener || fastener->GetTightness() != FastenerTightness::TORQUED)
					return false;
			}
		}
		return true;
	}
}

ShopWorld::StripStep ShopWorld::PlastigaugeStep(const Workshop& workshop, int engine, AssemblyCheck check, bool rod, int index, std::string& why) const
{
	const auto& assembly = workshop.GetAssembly(engine);
	auto parts = PartsOver(check, rod);
	bool capOn = Fitted(assembly, parts.cap, index) || Fitted(assembly, parts.lower, index);
	auto strip = std::find_if(m_plastigauge.begin(), m_plastigauge.end(),
		[&](const PlastigaugeStrip& s) { return s.engine == engine && s.check == check && s.rod == rod && s.index == index; });
	const char* lowerToo = parts.lower == AssemblySlot::COUNT ? "" : " and its shell";

	if (strip != m_plastigauge.end())
	{
		if (strip->squashed && !capOn)
			return StripStep::READ;
		why = strip->squashed ? std::format("Take {} cap {} off to read the squashed Plastigauge.", parts.name, index + 1)
			: capOn ? std::format("Torque {} cap {} down to spec to squash the Plastigauge.", parts.name, index + 1)
			: std::format("Plastigauge is on {} journal {}: fit the {} and torque it down.", parts.name, index + 1,
				parts.lower == AssemblySlot::COUNT ? "cap" : "lower shell and cap");
		return StripStep::NONE;
	}

	// A fresh strip goes on the bare journal: the crank sitting in its upper shells (a rod's in the
	// rod), or the cam in its bores in the head.
	if (!Fitted(assembly, parts.shaft, 0))
		why = parts.shaft == AssemblySlot::CAMSHAFT ? "The cam goes in before the Plastigauge goes on."
			: "The crank goes in on its upper shells before the Plastigauge goes on.";
	else if (rod && !assembly.Get(AssemblySlot::CONNECTING_ROD, index))
		why = std::format("Fit rod {} on its journal before the Plastigauge goes on.", index + 1);
	else if (parts.upper != AssemblySlot::COUNT && !assembly.Get(parts.upper, index))
		why = std::format("The upper {} shell {} goes in before the Plastigauge.", parts.name, index + 1);
	else if (capOn)
		why = std::format("Take {} cap {}{} off to lay Plastigauge across the journal.", parts.name, index + 1, lowerToo);
	else
		return StripStep::LAY;
	return StripStep::NONE;
}

void ShopWorld::UpdatePlastigauge(const Workshop& workshop)
{
	for (auto it = m_plastigauge.begin(); it != m_plastigauge.end();)
	{
		const auto& assembly = workshop.GetAssembly(it->engine);
		auto parts = PartsOver(it->check, it->rod);
		// Shaft out from under it, or turned with the strip on it (the cam turns with the crank):
		// it's gone, or smeared and no use.
		if (!Fitted(assembly, parts.shaft, 0) || (it->rod && !assembly.Get(AssemblySlot::CONNECTING_ROD, it->index)))
		{
			it = m_plastigauge.erase(it);
			continue;
		}
		if (std::abs(m_crankAngles[it->engine] - it->crankAngle) > 0.02f)
		{
			Toast(std::format("Turning the engine over smeared the Plastigauge on {} journal {}: it'll have to be done again.", parts.name, it->index + 1));
			it = m_plastigauge.erase(it);
			continue;
		}
		// Squashed once the cap (on its lower shell) is torqued down: that's the gap it reads.
		bool lowerIn = parts.lower == AssemblySlot::COUNT || assembly.Get(parts.lower, it->index);
		if (!it->squashed && lowerIn && assembly.Get(parts.cap, it->index) && CapTorqued(assembly, parts.cap, it->index))
		{
			auto clearance = it->check == AssemblyCheck::CAM_CLEARANCE ? assembly.CamClearance(it->index)
				: it->rod ? assembly.RodClearance(it->index) : assembly.MainClearance(it->index);
			if (clearance)
			{
				it->squashed = true;
				it->clearance = *clearance;
				Toast(std::format("The Plastigauge on {} journal {} is squashed: take the cap off to read it.", parts.name, it->index + 1));
			}
		}
		++it;
	}

	// Each strip shows as a thin green thread lying along its journal on the side its cap goes (under
	// a main or rod journal, on top of a cam journal), flattened and wider once the cap has squashed
	// it. Under the cap it's hidden, as it should be.
	using namespace EngineLayout;
	auto objectManager = ObjectManager::Get();
	while (m_stripObjects.size() > m_plastigauge.size())
	{
		objectManager->RemoveObject(m_stripObjects.back());
		m_stripObjects.pop_back();
	}
	while (m_stripObjects.size() < m_plastigauge.size())
	{
		auto object = Spawn(ShopModels::PLASTIGAUGE_STRIP, "", {});
		object->SetPickable(false);
		object->SetCastsShadows(false);
		m_stripObjects.push_back(object);
	}
	for (size_t i = 0; i < m_plastigauge.size(); i++)
	{
		const auto& strip = m_plastigauge[i];
		// The journal: the cam's in the head, a rod's pin wherever the crank has it, a main on the crank
		// axis. Radius out to the cap's side: up for the cam, down for the crank.
		glm::vec3 centre;
		float radius;
		if (strip.check == AssemblyCheck::CAM_CLEARANCE)
		{
			centre = { CamTowerX(strip.index), HEAD_DECK_Y + CAM_Y, 0.f };
			radius = 0.013f;
		}
		else if (strip.rod)
		{
			centre = PoseOnStand(strip.engine, AssemblySlot::ROD_BEARING, strip.index).offset;
			radius = -0.024f;
		}
		else
		{
			centre = { MainX(strip.index), CRANK_Y, 0.f };
			radius = -0.0275f;
		}
		auto& object = m_stripObjects[i];
		object->SetPosition(StandToWorld(strip.engine, centre + glm::vec3(0.f, radius, 0.f)));
		// Upside down under the crank, so its thickness builds away from the journal.
		object->SetRotation(StandRotation(strip.engine) * (radius < 0.f ? glm::angleAxis(glm::pi<float>(), glm::vec3(1.f, 0.f, 0.f)) : glm::quat(1.f, 0.f, 0.f, 0.f)));
		object->SetScale(strip.squashed ? glm::vec3(1.f, 0.4f, 2.5f) : glm::vec3(1.f));
	}
}

std::optional<ShopWorld::MeasureTarget> ShopWorld::FindMeasureTarget(const Workshop& workshop, const FirstPersonController& player, int toolIndex,
	std::vector<glm::vec3>& reachablePoints) const
{
	reachablePoints.clear();
	m_measureBlocked.clear();
	const auto& tool = ToolCatalog::All()[toolIndex];
	// Spanners and the like measure nothing.
	if (tool.kinds.empty() && !tool.measuresFinish && tool.assemblyChecks.empty())
		return std::nullopt;
	PROFILE_SCOPE("tools: measure target");
	glm::vec3 eye = player.GetEyePosition();
	glm::vec3 forward = player.GetForward();

	// Points buried deep behind whatever the crosshair is on (a main housing under a bore, seen
	// from above) can't be reached from here. Everything is judged against the first box hit.
	const Object* heldObject = GetHeld() ? GetHeld()->object.get() : nullptr;
	auto surface = ObjectManager::Get()->Raycast(eye, forward, MEASURE_DISTANCE + 1.f,
		[&](const pObject& object) { return object.get() != heldObject; });
	constexpr float REACH_INTO_PART = 0.12f;

	// Pick the measuring point closest to the crosshair. The slop is about the radius of a bore,
	// so aiming anywhere into one counts, while neighbours 95 mm apart stay distinct.
	// The target (and its label) is only made for a point that becomes the best: this runs every
	// frame for every point in reach.
	std::optional<MeasureTarget> best;
	float bestAngle = FLT_MAX;
	float blockedAngle = FLT_MAX;
	auto consider = [&](const glm::vec3& point, const auto& makeTarget) {
		glm::vec3 toPoint = point - eye;
		float distance = glm::length(toPoint);
		if (distance > MEASURE_DISTANCE || distance < 0.05f)
			return;
		if (surface && distance > surface->distance + REACH_INTO_PART)
			return;
		reachablePoints.push_back(point);
		// The widest tolerance (right up close) is under 0.75 rad, so anything further off is out.
		float cosine = glm::dot(toPoint / distance, forward);
		if (cosine < 0.73f)
			return;
		float angle = std::acos(std::clamp(cosine, -1.f, 1.f));
		float tolerance = std::atan2(0.042f, distance) + 0.01f;
		if (angle < tolerance && angle < bestAngle)
		{
			bestAngle = angle;
			best = makeTarget();
			best->position = point;
		}
	};
	// A point that can't be measured right now: no dot and no reading, but aiming at it says why
	// (what has to come off first, or what to do next).
	auto noteBlocked = [&](const glm::vec3& point, const std::string& why) {
		glm::vec3 toPoint = point - eye;
		float distance = glm::length(toPoint);
		if (distance <= 0.05f || distance > MEASURE_DISTANCE)
			return;
		float angle = std::acos(std::clamp(glm::dot(toPoint / distance, forward), -1.f, 1.f));
		if (angle < std::atan2(0.042f, distance) + 0.01f && angle < blockedAngle)
		{
			blockedAngle = angle;
			m_measureBlocked = why;
		}
	};

	for (const auto& [ref, visual] : m_items)
	{
		if (!visual.part || !visual.object->IsVisible() || visual.place == ItemPlace::HAND)
			continue;
		if (glm::length(visual.object->GetPosition() - eye) > MEASURE_DISTANCE + 0.5f)
			continue;

		glm::mat4 transform = visual.object->GetModelMatrix();
		for (const auto& anchor : ShopAssets::FeatureAnchorsFor(visual.part->GetType()))
		{
			const Feature* feature = visual.part->FindFeature(anchor.featureName);
			if (!feature)
				continue;
			bool canMeasure = tool.measuresFinish ? feature->HasFinishSpec() : feature->directlyMeasurable && tool.CanMeasure(*feature);
			if (!canMeasure)
				continue;

			glm::vec3 point = glm::vec3(transform * glm::vec4(anchor.position, 1.f));
			// Something fitted over it, or it's fitted itself.
			if (auto blocked = MeasureBlockedBy(workshop, visual, anchor.featureName))
			{
				noteBlocked(point, *blocked);
				continue;
			}

			consider(point, [&] {
				MeasureTarget target;
				target.part = ref;
				target.feature = anchor.featureName;
				target.finish = tool.measuresFinish;
				target.label = std::format("{}{} on the {}", anchor.featureName, tool.measuresFinish ? " finish" : "", visual.part->GetName());
				return target;
			});
		}
	}

	// Assembly checks on whichever engine stand it's aimed at.
	if (!tool.assemblyChecks.empty())
	{
		bool severalEngines = workshop.GetEngineCount() > 1;
		for (int engine = 0; engine < workshop.GetEngineCount(); engine++)
		{
			const Station* stand = FindStand(engine);
			if (!stand || !stand->present || engine == m_engineOnDyno || glm::length(stand->position - eye) > MEASURE_DISTANCE + 1.5f)
				continue;
			for (const auto& anchor : ShopAssets::AssemblyAnchors(workshop.GetSpec()))
			{
				if (!tool.CanCheck(anchor.check))
					continue;
				glm::vec3 point = StandToWorld(engine, anchor.position);
				// Bearing clearance is Plastigauge: a strip laid on the journal, squashed under the
				// torqued cap, then read with the cap off. Which step is next depends on what's fitted.
				auto step = StripStep::NONE;
				if (anchor.check == AssemblyCheck::OIL_CLEARANCE || anchor.check == AssemblyCheck::CAM_CLEARANCE)
				{
					std::string why;
					step = PlastigaugeStep(workshop, engine, anchor.check, anchor.rod, anchor.index, why);
					if (step == StripStep::NONE)
					{
						noteBlocked(point, why);
						continue;
					}
				}
				consider(point, [&] {
					MeasureTarget target;
					target.assembly = true;
					target.check = anchor.check;
					target.index = anchor.index;
					target.rod = anchor.rod;
					target.engine = engine;
					target.strip = step;
					target.label = Workshop::AssemblyReadingLabel(anchor.check, anchor.index, anchor.rod);
					if (step == StripStep::LAY)
						target.label = std::format("Lay a Plastigauge strip across the journal ({})", target.label);
					else if (step == StripStep::READ)
						target.label = std::format("Read the squashed Plastigauge ({})", target.label);
					if (severalEngines)
						target.label = std::format("Engine {}: {}", engine + 1, target.label);
					return target;
				});
			}
		}
	}

	return best;
}

Range ShopWorld::AssemblyTarget(const EngineSpec& spec, AssemblyCheck check, bool rod) const
{
	switch (check)
	{
		case AssemblyCheck::OIL_CLEARANCE:		return rod ? spec.rodClearance : spec.mainClearance;
		case AssemblyCheck::CAM_CLEARANCE:		return spec.camClearance;
		case AssemblyCheck::RING_GAP:			return spec.ringGap;
		case AssemblyCheck::DECK_CLEARANCE:		return { spec.quench.min - spec.gasketThickness.nominal, spec.quench.max - spec.gasketThickness.nominal };
		case AssemblyCheck::INSTALLED_HEIGHT:	return spec.installedHeightRange;
		default:								return {};
	}
}

void ShopWorld::Measure(Workshop& workshop, const MeasureTarget& target, int toolIndex)
{
	Readout readout;
	readout.time = READOUT_SECONDS;

	if (target.assembly)
	{
		// Plastigauge: the strip goes on now and is read later, once the cap's been torqued over it
		// and taken off again.
		if (target.strip == StripStep::LAY)
		{
			m_plastigauge.push_back({ target.engine, target.check, target.rod, target.index, false, 0.0, m_crankAngles[target.engine] });
			Toast("Plastigauge laid across the journal: now the cap (and its shell), torqued to spec. Don't turn the engine over.");
			return;
		}
		std::optional<double> squashedTo;
		if (target.strip == StripStep::READ)
		{
			auto strip = std::find_if(m_plastigauge.begin(), m_plastigauge.end(), [&](const PlastigaugeStrip& s) {
				return s.engine == target.engine && s.check == target.check && s.rod == target.rod && s.index == target.index && s.squashed;
			});
			if (strip == m_plastigauge.end())
				return;
			squashedTo = strip->clearance;
			m_plastigauge.erase(strip);
		}

		workshop.SetActiveEngine(target.engine);
		bool measured = squashedTo ? workshop.RecordAssemblyReading(target.check, target.index, target.rod, toolIndex, squashedTo)
			: workshop.MeasureAssembly(target.check, target.index, target.rod, toolIndex);
		if (!measured)
			return;
		auto reading = workshop.GetAssemblyReading(target.check, target.index, target.rod);
		Range range = AssemblyTarget(workshop.GetSpec(), target.check, target.rod);
		auto format = [&](double value) {
			return target.check == AssemblyCheck::INSTALLED_HEIGHT ? Units::FormatSize(value, m_units) : Units::FormatDeviation(value, m_units);
		};

		readout.title = Workshop::AssemblyReadingLabel(target.check, target.index, target.rod);
		readout.lines.push_back(format(reading->value));
		readout.lines.push_back(std::format("target {} - {}", format(range.min), format(range.max)));
		readout.good = range.Contains(reading->value);
		m_readout = readout;
		return;
	}

	auto visual = Find(target.part);
	if (!visual)
		return;
	Part& part = *visual->part;
	bool measured = target.finish ? workshop.MeasureFinish(part, target.feature, toolIndex) : workshop.MeasureSize(part, target.feature, toolIndex);
	if (!measured)
		return;

	const Feature* feature = part.FindFeature(target.feature);
	const FeatureReading* reading = part.GetReading(target.feature);

	// Judge the reading, not the truth: copy the feature and swap its geometry for what was read.
	Feature asRead = *feature;
	for (int station = 0; station < Feature::STATIONS; station++)
		for (int axis = 0; axis < Feature::AXES; axis++)
			asRead.samples[station][axis] = reading->samples[station][axis];
	asRead.value = reading->value;
	asRead.surfaceRa = reading->finishRa.value_or(0.0);

	readout.title = std::format("{}  ({})", target.feature, PartLabel(part));
	uint32_t issues = 0;
	if (target.finish)
	{
		readout.lines.push_back(Units::FormatFinish(*reading->finishRa));
		issues = asRead.Evaluate(true) & (FeatureIssue::ROUGH | FeatureIssue::GLAZED);
	}
	else if (asRead.IsRound())
	{
		readout.lines.push_back(std::format("{} average", Units::FormatSize(asRead.Size(), m_units)));
		readout.lines.push_back(std::format("taper {}   out-of-round {}", Units::FormatDeviation(asRead.Taper(), m_units), Units::FormatDeviation(asRead.OutOfRound(), m_units)));
		issues = asRead.Evaluate(false);
	}
	else
	{
		readout.lines.push_back(FormatFeatureValue(asRead, asRead.Size(), m_units));
		issues = asRead.Evaluate(false);
	}

	std::string verdict = DescribeIssues(issues, feature->kind);
	if (!target.finish && issues == FeatureIssue::NONE)
		if (auto sizeClass = asRead.MatchSizeClass(); sizeClass && *sizeClass != 0.0)
			verdict += std::format(" ({:+.3g})", *sizeClass);
	readout.lines.push_back(verdict);
	readout.good = issues == FeatureIssue::NONE;
	m_readout = readout;
}

std::optional<size_t> ShopWorld::FindFitTarget(const Workshop& workshop, const FirstPersonController& player, const ItemVisual& held)
{
	glm::vec3 eye = player.GetEyePosition();
	glm::vec3 forward = player.GetForward();
	// How close to the crosshair a spot has to be: a block is easy to aim at, a valve seal isn't,
	// but neighbouring positions (a bore spacing apart) must stay apart.
	auto model = ModelOf(held.object);
	float radius = glm::length(model->GetBoundsMax() - model->GetBoundsMin()) * 0.5f;
	float tolerance = std::clamp(radius * 0.45f, 0.03f, 0.15f);

	std::optional<size_t> stand;
	float best = tolerance;
	for (size_t i = 0; i < m_stations.size(); i++)
	{
		const auto& station = m_stations[i];
		if (station.type != StationType::ENGINE_STAND || !station.present || station.engineIndex == m_engineOnDyno)
			continue;
		int engine = station.engineIndex;
		if (glm::distance(StandToWorld(engine, {}), eye) > INTERACT_DISTANCE + 1.f)
			continue;

		const auto& assembly = workshop.GetAssembly(engine);
		// A size of nut or bolt fits every set of holes of its size.
		for (int slotIndex = 0; slotIndex < static_cast<int>(AssemblySlot::COUNT); slotIndex++)
		{
			auto slot = static_cast<AssemblySlot>(slotIndex);
			if (EngineAssembly::SlotPartType(slot) != held.part->GetType())
				continue;
			bool fastener = EngineAssembly::Holes(slot) != nullptr;
			for (int index = 0; index < assembly.SlotCount(slot); index++)
			{
				if (assembly.Get(slot, index))
					continue;
				bool ready = !assembly.MissingPrerequisite(slot, index);
				// Holes only exist once the part they're in is on.
				if (fastener && !ready)
					continue;
				glm::vec3 position = SlotWorldCenter(engine, slot, index);
				m_fitPoints.push_back({ position, ready });

				float along = glm::dot(position - eye, forward);
				if (along <= 0.f || along > INTERACT_DISTANCE + 0.5f)
					continue;
				float off = glm::length(position - (eye + forward * along));
				if (off < best)
				{
					best = off;
					m_fitTarget = FitTarget{ engine, slot, index, position, ready };
					stand = i;
				}
			}
		}
	}
	return stand;
}

void ShopWorld::MarkEmptyHoles(const Workshop& workshop, const FirstPersonController& player)
{
	// Close enough to see which holes are empty; further off the dots would just be clutter.
	constexpr float MARK_DISTANCE = 1.6f;
	glm::vec3 eye = player.GetEyePosition();
	glm::vec3 forward = player.GetForward();

	// Carrying a nut or bolt already marks the holes it fits (green), so those aren't marked twice.
	auto held = GetHeld();
	std::optional<PartType> carried = held && held->part ? std::optional(held->part->GetType()) : std::nullopt;

	size_t nearest = SIZE_MAX;
	float nearestOff = 0.06f;
	std::vector<FastenerTarget> found;
	for (const auto& station : m_stations)
	{
		if (station.type != StationType::ENGINE_STAND || !station.present || station.engineIndex == m_engineOnDyno)
			continue;
		int engine = station.engineIndex;
		if (glm::distance(StandToWorld(engine, {}), eye) > MARK_DISTANCE + 1.f)
			continue;
		const auto& assembly = workshop.GetAssembly(engine);
		for (int slotIndex = 0; slotIndex < static_cast<int>(AssemblySlot::COUNT); slotIndex++)
		{
			auto slot = static_cast<AssemblySlot>(slotIndex);
			const auto* holes = EngineAssembly::Holes(slot);
			if (!holes || (carried && *carried == holes->size))
				continue;
			for (int index = 0; index < assembly.SlotCount(slot); index++)
			{
				// Holes only exist once the part they're in is on.
				if (assembly.Get(slot, index) || assembly.MissingPrerequisite(slot, index))
					continue;
				// Where the bolt's head would be, and which way it would point: out of the hole.
				auto pose = PoseOnStand(engine, slot, index);
				glm::vec3 head = StandToWorld(engine, pose.offset);
				glm::vec3 toHole = head - eye;
				if (glm::length(toHole) > MARK_DISTANCE)
					continue;
				// Only holes facing this way: one pointing away is on the far side of the part (or
				// under it), out of sight.
				glm::vec3 outward = StandToWorld(engine, pose.offset + pose.rotation * glm::vec3(0.f, 0.05f, 0.f)) - head;
				if (glm::dot(outward, toHole) > 0.f)
					continue;
				const auto& bounds = PartBounds(holes->size);
				glm::vec3 position = StandToWorld(engine, pose.offset + pose.rotation * ((bounds.first + bounds.second) * 0.5f * pose.scale));
				toHole = position - eye;
				float along = glm::dot(toHole, forward);
				if (along > 0.f)
				{
					float off = glm::length(toHole - forward * along) / along;
					if (off < nearestOff)
					{
						nearestOff = off;
						nearest = m_holePoints.size();
					}
				}
				m_holePoints.push_back({ position, "" });
				found.push_back({ engine, slot, index, position });
			}
		}
	}
	if (nearest != SIZE_MAX)
	{
		m_aimedHole = found[nearest];
		m_holePoints[nearest].label = std::format("{} needed", PartCatalog::Get(EngineAssembly::Holes(m_aimedHole->slot)->size).name);
	}
}

void ShopWorld::DrawHardwarePanel(const Workshop& workshop) const
{
	// What's looked at: the nut or bolt the spanner in hand is on, else one under the crosshair
	// (fitted or loose), else the empty hole nearest the crosshair.
	const Part* part = nullptr;
	std::optional<FastenerTarget> hole = m_spannerFastener;
	if (!hole && m_hover && m_hover->kind == TargetKind::ITEM)
		if (const auto* visual = Find(m_hover->item); visual && visual->part && PartCatalog::Get(visual->part->GetType()).fastener.IsFastener())
		{
			part = visual->part.get();
			if (visual->place == ItemPlace::STAND)
				if (auto slot = FindInstalledSlot(workshop.GetAssembly(visual->engineIndex), visual->part))
					hole = FastenerTarget{ visual->engineIndex, slot->first, slot->second, {} };
		}
	if (!part && !hole)
		hole = m_aimedHole;
	if (!part && !hole)
		return;

	const EngineAssembly* assembly = hole ? &workshop.GetAssembly(hole->engine) : nullptr;
	const FastenerHoles* holes = hole ? EngineAssembly::Holes(hole->slot) : nullptr;
	if (hole && !part)
		part = assembly->Get(hole->slot, hole->index).get();
	PartType type = part ? part->GetType() : holes->size;
	const auto& info = PartCatalog::Get(type);
	const auto& spec = info.fastener;

	const ImU32 text = IM_COL32(240, 240, 235, 255);
	const ImU32 dim = IM_COL32(175, 182, 192, 255);
	const ImU32 green = IM_COL32(120, 230, 130, 255);
	const ImU32 amber = IM_COL32(255, 190, 90, 255);
	const ImU32 red = IM_COL32(250, 120, 110, 255);
	std::vector<std::pair<std::string, ImU32>> lines;

	std::string title = part ? PartLabel(*part) : std::format("Empty hole: takes an {}", info.name);
	lines.push_back({ std::format("{} thread, {} mm {}", spec.thread, spec.headMm, spec.nut ? "nut" : "head"), text });
	if (part)
	{
		bool damaged = part->GetCondition() == PartCondition::DAMAGED;
		lines.push_back({ std::format("{}, {}", PartQualityName(part->GetQuality()), PartConditionName(part->GetCondition())), damaged ? red : dim });
		if (damaged)
			lines.push_back({ part->GetDamageReason(), red });
	}

	if (holes)
	{
		lines.push_back({ assembly->PositionName(hole->slot, hole->index), text });
		lines.push_back({ std::format("Spec: {:.0f} Nm", holes->torqueNm), text });
		if (part)
		{
			auto tightness = part->GetTightness();
			lines.push_back(tightness == FastenerTightness::TORQUED ? std::pair{ std::string("Torqued"), green }
				: tightness == FastenerTightness::SNUG ? std::pair{ std::string("Snugged up by feel, not torqued"), amber }
				: std::pair{ std::string("Finger tight"), amber });
		}
		// The tools for the job.
		lines.push_back({ std::format("Snug or loosen: {} mm socket on a ratchet or breaker bar, or the {} mm wrench", spec.headMm, spec.headMm), dim });
		lines.push_back({ std::format("Torque: {} mm socket on a torque wrench", spec.headMm), dim });
	}
	else
	{
		// A loose one: where it goes on an engine.
		std::vector<std::string> uses;
		for (const auto& candidate : PartCatalog::Holes())
			if (candidate.size == type)
				uses.push_back(std::format("  {} ({:.0f} Nm)", candidate.name, candidate.torqueNm));
		lines.push_back({ uses.empty() ? "Not used on this engine" : "Goes in:", text });
		for (const auto& use : uses)
			lines.push_back({ use, dim });
	}

	// On the right, under where the gauge readout goes.
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	ImVec2 display = ImGui::GetIO().DisplaySize;
	float lineHeight = ImGui::GetTextLineHeightWithSpacing();
	float width = ImGui::CalcTextSize(title.c_str()).x + 32.f;
	for (const auto& line : lines)
		width = (std::max)(width, ImGui::CalcTextSize(line.first.c_str()).x + 32.f);
	width = (std::min)(width, display.x * 0.45f);
	ImVec2 min = { display.x - width - 20.f, display.y * 0.32f };
	ImVec2 max = { display.x - 20.f, min.y + 22.f + lineHeight * (lines.size() + 1) };
	drawList->AddRectFilled(min, max, IM_COL32(15, 18, 22, 200), 6.f);
	drawList->AddRect(min, max, IM_COL32(255, 170, 60, 220), 6.f, 0, 2.f);
	drawList->AddText({ min.x + 12.f, min.y + 8.f }, IM_COL32(255, 200, 120, 255), title.c_str());
	for (size_t i = 0; i < lines.size(); i++)
		drawList->AddText({ min.x + 12.f, min.y + 12.f + lineHeight * (i + 1) }, lines[i].second, lines[i].first.c_str());
}

bool ShopWorld::IsMovingStation() const
{
	return m_move.has_value();
}

void ShopWorld::StartMove(size_t stationIndex)
{
	auto& station = m_stations[stationIndex];
	m_move = MoveState{ stationIndex, station.position, station.yaw, station.yaw, false };

	// Whatever is sitting on it (or stacked on something that is) goes with it: anything off the
	// floor whose middle is over the station's footprint.
	auto bounds = WorldBounds(station.object);
	glm::quat inverse = glm::inverse(station.Rotation());
	auto onTop = [&](const pObject& object) {
		auto box = WorldBounds(object);
		glm::vec3 middle = (box.min + box.max) * 0.5f;
		return box.min.y > 0.02f && box.min.y < bounds.max.y + 0.5f && middle.x > bounds.min.x && middle.x < bounds.max.x
			&& middle.z > bounds.min.z && middle.z < bounds.max.z;
	};
	auto ride = [&](const pObject& object, const ItemRef& item, uint32_t packageId, float yaw) {
		m_move->riders.push_back({ item, packageId, inverse * (object->GetPosition() - station.position), inverse * object->GetRotation(), yaw });
	};
	for (const auto& [ref, visual] : m_items)
		if (visual.place == ItemPlace::WORLD && onTop(visual.object))
			ride(visual.object, ref, 0, visual.yaw);
	for (const auto& [id, package] : m_packages)
		if (onTop(package.object))
			ride(package.object, {}, id, 0.f);
}

void ShopWorld::CarryRiders(const Station& station)
{
	if (!m_move)
		return;
	glm::quat rotation = station.Rotation();
	float turned = station.yaw - m_move->originalYaw;
	for (const auto& rider : m_move->riders)
	{
		pObject object = nullptr;
		Body* body = nullptr;
		if (rider.packageId)
		{
			auto package = m_packages.find(rider.packageId);
			if (package == m_packages.end())
				continue;
			object = package->second.object;
			body = &package->second.body;
		}
		else if (auto visual = Find(rider.item); visual && visual->place == ItemPlace::WORLD)
		{
			object = visual->object;
			body = &visual->body;
			visual->yaw = rider.yaw + turned;
		}
		if (!object)
			continue;
		object->SetPosition(station.position + rotation * rider.localPosition);
		object->SetRotation(rotation * rider.localRotation);
		// Settles again once the station is set down.
		*body = {};
		body->resting = false;
	}
}

bool ShopWorld::IsRiding(const ItemRef& item, uint32_t packageId) const
{
	if (!m_move)
		return false;
	return std::any_of(m_move->riders.begin(), m_move->riders.end(), [&](const MoveState::Rider& rider) {
		return packageId ? rider.packageId == packageId : (!rider.packageId && rider.item == item);
	});
}

void ShopWorld::CancelMove()
{
	if (!m_move)
		return;
	auto& station = m_stations[m_move->stationIndex];
	station.position = m_move->originalPosition;
	station.yaw = m_move->originalYaw;
	CarryRiders(station);
	m_move.reset();
}

bool ShopWorld::StationFits(const Station& station, const glm::vec3& position, float yaw, const FirstPersonController* player) const
{
	auto model = ModelOf(station.object);
	ShopBox footprint = TransformBox({ model->GetBoundsMin(), model->GetBoundsMax() }, position, Yaw(yaw));

	if (!OnShopFloor(footprint))
		return false;

	for (const auto& other : m_stations)
	{
		if (other.object == station.object || !other.present)
			continue;
		auto bounds = WorldBounds(other.object);
		bool overlaps = footprint.min.x < bounds.max.x - 0.01f && bounds.min.x < footprint.max.x - 0.01f
			&& footprint.min.z < bounds.max.z - 0.01f && bounds.min.z < footprint.max.z - 0.01f;
		if (overlaps)
			return false;
	}

	if (player)
	{
		glm::vec2 feet = { player->GetFeetPosition().x, player->GetFeetPosition().z };
		glm::vec2 closest = glm::clamp(feet, glm::vec2(footprint.min.x, footprint.min.z), glm::vec2(footprint.max.x, footprint.max.z));
		if (glm::length(feet - closest) < player->GetRadius())
			return false;
	}
	return true;
}

void ShopWorld::PlaceNewMachine(Station& station)
{
	station.yaw = 0.f;
	if (StationFits(station, station.homePosition, 0.f, nullptr))
	{
		station.position = station.homePosition;
		return;
	}

	// Its usual spot is taken: find the first free patch of floor, working in from the machine wall.
	float leftmost = m_bayBuilt ? GarageLayout::BAY_MIN_X : ROOM_BOUNDS.min.x;
	for (float x = MACHINE_ROW_X; x > leftmost; x -= 0.5f)
		for (float z = ROOM_BOUNDS.min.y + 0.6f; z < ROOM_BOUNDS.max.y; z += 0.5f)
			if (StationFits(station, { x, 0.f, z }, 0.f, nullptr))
			{
				station.position = { x, 0.f, z };
				return;
			}
	station.position = station.homePosition;
}

void ShopWorld::UpdateMove(const FirstPersonController& player, const InputManager& input)
{
	auto& station = m_stations[m_move->stationIndex];
	auto model = ModelOf(station.object);
	glm::vec3 extent = (model->GetBoundsMax() - model->GetBoundsMin()) * 0.5f;
	float radius = (std::max)(extent.x, extent.z);

	if (input.WasKeyPressed(GLFW_KEY_R))
		m_move->yaw += glm::half_pi<float>();

	// Hold the equipment out in front, snapped to a 5 cm grid on the floor.
	glm::vec3 forward = player.GetForward();
	forward.y = 0.f;
	forward = glm::length(forward) > 0.001f ? glm::normalize(forward) : glm::vec3(0.f, 0.f, -1.f);
	glm::vec3 target = player.GetFeetPosition() + forward * (radius + 0.9f);
	target = glm::round(target / 0.05f) * 0.05f;
	// Floor-standing equipment stays on the floor; the tool board keeps its height on the wall.
	target.y = station.homePosition.y;

	station.position = target;
	station.yaw = m_move->yaw;
	station.object->SetPosition(station.position);
	station.object->SetRotation(station.Rotation());
	CarryRiders(station);

	m_move->valid = StationFits(station, target, m_move->yaw, &player);
	station.object->SetTint(m_move->valid ? glm::vec4(0.7f, 1.f, 0.7f, 1.f) : glm::vec4(1.f, 0.45f, 0.45f, 1.f));
	station.object->SetEmissive(0.25f);

	m_prompt = m_move->valid ? std::format("[LMB] Set the {} down here", station.name) : std::format("The {} doesn't fit here", station.name);
	m_secondaryPrompt = "[R] Rotate   [F] Cancel";

	if (m_move->valid && (input.WasKeyPressed(GLFW_KEY_E) || input.WasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)))
	{
		Toast(std::format("Moved the {}.", station.name));
		m_move.reset();
	}
	else if (input.WasKeyPressed(GLFW_KEY_F))
		CancelMove();
}

std::optional<glm::vec2> ShopWorld::ProjectToScreen(const glm::vec3& point, const FirstPersonController& player) const
{
	ImVec2 display = ImGui::GetIO().DisplaySize;
	if (display.x <= 0.f || display.y <= 0.f)
		return std::nullopt;

	// Made once and reused while the view stays the same: the HUD projects hundreds of marker dots.
	glm::vec3 eye = player.GetEyePosition();
	glm::vec3 forward = player.GetForward();
	auto& cache = m_screenProjection;
	if (!cache.valid || cache.eye != eye || cache.forward != forward || cache.display != glm::vec2(display.x, display.y) || cache.fieldOfView != m_fieldOfView)
	{
		glm::mat4 view = glm::lookAt(eye, eye + forward, glm::vec3(0.f, 1.f, 0.f));
		glm::mat4 projection = glm::perspective(glm::radians(m_fieldOfView), display.x / display.y, 0.03f, 60.f);
		cache = { true, eye, forward, { display.x, display.y }, m_fieldOfView, projection * view };
	}
	glm::vec4 clip = cache.matrix * glm::vec4(point, 1.f);
	if (clip.w <= 0.001f)
		return std::nullopt;

	glm::vec2 ndc = glm::vec2(clip) / clip.w;
	return glm::vec2((ndc.x * 0.5f + 0.5f) * display.x, (0.5f - ndc.y * 0.5f) * display.y);
}

void ShopWorld::DrawDebugBounds(const FirstPersonController& player) const
{
	ImVec2 display = ImGui::GetIO().DisplaySize;
	if (display.x <= 0.f || display.y <= 0.f)
		return;

	// Behind the UI, over the 3D view.
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	glm::vec3 eye = player.GetEyePosition();
	glm::mat4 viewProjection = glm::perspective(glm::radians(m_fieldOfView), display.x / display.y, 0.03f, 500.f)
		* glm::lookAt(eye, eye + player.GetForward(), glm::vec3(0.f, 1.f, 0.f));

	auto toScreen = [&](const glm::vec4& clip) {
		glm::vec2 ndc = glm::vec2(clip) / clip.w;
		return ImVec2((ndc.x * 0.5f + 0.5f) * display.x, (0.5f - ndc.y * 0.5f) * display.y);
	};
	// Edges that pass behind the camera are cut where they cross just in front of it.
	auto line = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color) {
		constexpr float NEAR_W = 0.05f;
		glm::vec4 clipA = viewProjection * glm::vec4(a, 1.f);
		glm::vec4 clipB = viewProjection * glm::vec4(b, 1.f);
		if (clipA.w < NEAR_W && clipB.w < NEAR_W)
			return;
		if (clipA.w < NEAR_W)
			clipA = glm::mix(clipA, clipB, (NEAR_W - clipA.w) / (clipB.w - clipA.w));
		else if (clipB.w < NEAR_W)
			clipB = glm::mix(clipB, clipA, (NEAR_W - clipB.w) / (clipA.w - clipB.w));
		drawList->AddLine(toScreen(clipA), toScreen(clipB), color, 1.5f);
	};
	auto box = [&](const ShopBox& bounds, ImU32 color) {
		// Far-off boxes (houses down the street) would only clutter the view.
		glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
		if (glm::distance(center, eye) > 60.f)
			return;
		glm::vec3 corners[8];
		for (int i = 0; i < 8; i++)
			corners[i] = { i & 1 ? bounds.max.x : bounds.min.x, i & 2 ? bounds.max.y : bounds.min.y, i & 4 ? bounds.max.z : bounds.min.z };
		constexpr int EDGES[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
		for (const auto& edge : EDGES)
			line(corners[edge[0]], corners[edge[1]], color);
	};

	for (const auto& wall : SolidWalls())
		box(wall, DEBUG_WALL_COLOR);
	for (const auto& obstacle : m_obstacles)
		box({ { obstacle.min.x, 0.f, obstacle.min.y }, { obstacle.max.x, 1.8f, obstacle.max.y } }, DEBUG_OBSTACLE_COLOR);
	for (const auto& surface : m_surfaces)
		box(surface, DEBUG_SURFACE_COLOR);
	for (const auto& station : m_stations)
		if (station.present && station.object)
			box(WorldBounds(station.object), DEBUG_STATION_COLOR);
	for (const auto& [id, package] : m_packages)
		box(WorldBounds(package.object), DEBUG_PACKAGE_COLOR);
	for (const auto& [ref, visual] : m_items)
		if (visual.object && visual.object->IsVisible() && visual.place != ItemPlace::INVENTORY)
			box(WorldBounds(visual.object), DEBUG_ITEM_COLOR);
}

void ShopWorld::DrawHotbar(const Workshop& workshop) const
{
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	ImVec2 display = ImGui::GetIO().DisplaySize;
	constexpr float SLOT = 54.f;
	constexpr float GAP = 6.f;
	float width = PlayerInventory::HOTBAR_SLOTS * SLOT + (PlayerInventory::HOTBAR_SLOTS - 1) * GAP;
	ImVec2 origin = { (display.x - width) * 0.5f, display.y - SLOT - 46.f };

	for (int slot = 0; slot < PlayerInventory::HOTBAR_SLOTS; slot++)
	{
		ImVec2 min = { origin.x + slot * (SLOT + GAP), origin.y };
		ImVec2 max = { min.x + SLOT, min.y + SLOT };
		auto item = m_inventory.GetHotbar(slot);
		// The selected slot, empty or not, is the hand: a bright frame a little bigger than the rest.
		// With the hand emptied by its number, no slot is.
		bool selected = slot == m_inventory.GetSelected() && !m_handEmpty;

		drawList->AddRectFilled(min, max, selected ? IM_COL32(45, 48, 56, 210) : IM_COL32(20, 22, 26, 170), 5.f);
		if (selected)
			drawList->AddRect({ min.x - 3.f, min.y - 3.f }, { max.x + 3.f, max.y + 3.f }, IM_COL32(255, 210, 90, 255), 6.f, 0, 3.f);
		else
			drawList->AddRect(min, max, IM_COL32(120, 120, 130, 160), 5.f);
		if (!item.IsNone())
		{
			auto info = DescribeItem(item, workshop);
			ItemIcons::DrawItem(drawList, info.isTool, info.toolIndex, info.partType, { min.x + 6.f, min.y + 6.f }, SLOT - 12.f);
		}
		drawList->AddText({ min.x + 4.f, min.y + 2.f }, IM_COL32(220, 220, 220, 220), std::to_string((slot + 1) % 10).c_str());
	}

	// The item in hand, with what can be done with it beside it, centred over the hotbar.
	std::string carried = m_carriedPackage && m_packages.contains(m_carriedPackage) ? "Delivery" : "";
	if (auto held = GetHeld(); held || !carried.empty())
	{
		auto name = held ? DescribeItem(held->ref, workshop).name : carried;
		std::string hint = m_heldHint.empty() ? "" : "     " + m_heldHint;
		float nameWidth = ImGui::CalcTextSize(name.c_str()).x;
		float lineWidth = nameWidth + ImGui::CalcTextSize(hint.c_str()).x;
		ImVec2 start = { (display.x - lineWidth) * 0.5f, origin.y - 22.f };
		ShadowText(drawList, start, IM_COL32(240, 240, 235, 235), name, false);
		if (!hint.empty())
			ShadowText(drawList, { start.x + nameWidth, start.y }, IM_COL32(200, 200, 200, 200), hint, false);
	}
}

void ShopWorld::DrawPackageLabels(const Workshop& workshop, const FirstPersonController& player) const
{
	// Behind the UI, over the 3D view: a little tag over each delivery with an icon per kind of
	// thing inside it and how many.
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	glm::vec3 eye = player.GetEyePosition();
	constexpr float ICON = 30.f;
	constexpr float PAD = 5.f;
	constexpr int MAX_ICONS = 5;
	constexpr float SHOWN_WITHIN = 9.f;

	struct Entry
	{
		bool isTool = false;
		int toolIndex = -1;
		PartType partType = PartType::BLOCK;
		int station = -1;
		int count = 1;
	};

	for (const auto& [id, package] : m_packages)
	{
		// The one being aimed at is named by the prompt instead.
		if ((m_hover && m_hover->kind == TargetKind::PACKAGE && m_hover->packageId == id) || id == m_carriedPackage)
			continue;

		// On the middle of the package, so the tags on a stack don't overlap.
		auto box = WorldBounds(package.object);
		glm::vec3 middle = (box.min + box.max) * 0.5f;
		float distance = glm::distance(middle, eye);
		if (distance > SHOWN_WITHIN)
			continue;
		// Not through the garage walls.
		if (auto wall = WallHit(eye, glm::normalize(middle - eye)); wall && *wall < distance)
			continue;
		auto screen = ProjectToScreen(middle, player);
		if (!screen)
			continue;

		std::vector<Entry> entries;
		for (const auto& ref : package.items)
		{
			auto info = DescribeItem(ref, workshop);
			auto same = std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) {
				return entry.station < 0 && entry.isTool == info.isTool && (info.isTool ? entry.toolIndex == info.toolIndex : entry.partType == info.partType);
			});
			if (same != entries.end())
				same->count++;
			else
				entries.push_back({ info.isTool, info.toolIndex, info.partType, -1, 1 });
		}
		for (size_t station : package.stations)
			entries.push_back({ false, -1, PartType::BLOCK, static_cast<int>(station), 1 });
		if (entries.empty())
			continue;

		int shown = (std::min)(static_cast<int>(entries.size()), MAX_ICONS);
		int hidden = static_cast<int>(entries.size()) - shown;
		std::string more = hidden > 0 ? std::format("+{}", hidden) : "";
		float width = PAD + shown * (ICON + PAD) + (hidden > 0 ? ImGui::CalcTextSize(more.c_str()).x + PAD : 0.f);
		float alpha = std::clamp((SHOWN_WITHIN - distance) / 2.f, 0.f, 1.f);
		auto fade = [&](int a) { return static_cast<int>(a * alpha); };

		ImVec2 min = { screen->x - width * 0.5f, screen->y - (ICON + PAD * 2.f) * 0.5f };
		ImVec2 max = { min.x + width, min.y + ICON + PAD * 2.f };
		drawList->AddRectFilled(min, max, IM_COL32(20, 22, 26, fade(190)), 5.f);
		drawList->AddRect(min, max, IM_COL32(230, 190, 120, fade(200)), 5.f);
		ImU32 iconColor = IM_COL32(225, 225, 220, fade(255));
		for (int i = 0; i < shown; i++)
		{
			const auto& entry = entries[i];
			ImVec2 at = { min.x + PAD + i * (ICON + PAD), min.y + PAD };
			if (entry.station >= 0)
			{
				const auto& station = m_stations[entry.station];
				if (station.type == StationType::MACHINE)
					ItemIcons::DrawMachine(drawList, MachineCatalog::All()[station.machineIndex], at, ICON, iconColor);
				else
				{
					// An engine stand: upright, arm and mounting plate, on its feet.
					float s = ICON;
					drawList->AddLine({ at.x + s * 0.3f, at.y + s * 0.9f }, { at.x + s * 0.3f, at.y + s * 0.25f }, iconColor, 2.5f);
					drawList->AddLine({ at.x + s * 0.3f, at.y + s * 0.3f }, { at.x + s * 0.75f, at.y + s * 0.3f }, iconColor, 2.5f);
					drawList->AddRectFilled({ at.x + s * 0.72f, at.y + s * 0.12f }, { at.x + s * 0.82f, at.y + s * 0.5f }, iconColor);
					drawList->AddLine({ at.x + s * 0.1f, at.y + s * 0.9f }, { at.x + s * 0.9f, at.y + s * 0.9f }, iconColor, 2.5f);
				}
			}
			else
				ItemIcons::DrawItem(drawList, entry.isTool, entry.toolIndex, entry.partType, at, ICON, iconColor);
			if (entry.count > 1)
			{
				std::string count = std::format("{}", entry.count);
				ImVec2 size = ImGui::CalcTextSize(count.c_str());
				drawList->AddText({ at.x + ICON - size.x, at.y + ICON - size.y + 3.f }, IM_COL32(255, 220, 140, fade(255)), count.c_str());
			}
		}
		if (hidden > 0)
			drawList->AddText({ max.x - PAD - ImGui::CalcTextSize(more.c_str()).x, min.y + PAD + ICON * 0.3f }, IM_COL32(220, 220, 220, fade(230)), more.c_str());
	}
}

void ShopWorld::DrawHud(const Workshop& workshop, const FirstPersonController& player, bool showControls) const
{
	DrawPackageLabels(workshop, player);

	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	ImVec2 display = ImGui::GetIO().DisplaySize;
	ImVec2 center = { display.x * 0.5f, display.y * 0.5f };
	const ImU32 white = IM_COL32(240, 240, 235, 230);
	const ImU32 grey = IM_COL32(200, 200, 200, 200);

	drawList->AddCircleFilled(center, 3.5f, IM_COL32(0, 0, 0, 160));
	drawList->AddCircleFilled(center, 2.f, m_hover || m_measureTarget ? IM_COL32(255, 210, 90, 255) : white);

	// Faint dots on everything the tool in hand can measure from here...
	for (const auto& point : m_measurePoints)
		if (auto screen = ProjectToScreen(point, player))
			drawList->AddCircleFilled({ screen->x, screen->y }, 3.f, IM_COL32(120, 220, 255, 110), 8);

	// ...and a ring on the one it will take a reading of.
	if (m_measureTarget)
		if (auto screen = ProjectToScreen(m_measureTarget->position, player))
		{
			drawList->AddCircle({ screen->x, screen->y }, 9.f, IM_COL32(120, 220, 255, 230), 20, 2.f);
			drawList->AddCircleFilled({ screen->x, screen->y }, 2.5f, IM_COL32(120, 220, 255, 230));
		}

	// Empty bolt and nut holes close by, in amber, with the size wanted on the one aimed at.
	for (const auto& point : m_holePoints)
		if (auto screen = ProjectToScreen(point.position, player))
		{
			drawList->AddCircleFilled({ screen->x, screen->y }, 3.5f, IM_COL32(255, 170, 60, 170), 8);
			if (!point.label.empty())
				ShadowText(drawList, { screen->x, screen->y - 16.f }, IM_COL32(255, 200, 120, 230), point.label, true);
		}

	// Carrying a part to an engine: a dot on every empty place it can go (green when it can go on
	// now, grey when something else has to go first) and a ring on the one being aimed at.
	for (const auto& point : m_fitPoints)
		if (auto screen = ProjectToScreen(point.position, player))
			drawList->AddCircleFilled({ screen->x, screen->y }, 4.f, point.ready ? IM_COL32(120, 255, 150, 150) : IM_COL32(170, 170, 170, 110));
	if (m_fitTarget)
		if (auto screen = ProjectToScreen(m_fitTarget->position, player))
		{
			ImU32 ring = m_fitTarget->ready ? IM_COL32(120, 255, 150, 240) : IM_COL32(255, 190, 90, 240);
			drawList->AddCircle({ screen->x, screen->y }, 11.f, ring, 24, 2.f);
			drawList->AddCircleFilled({ screen->x, screen->y }, 3.f, ring);
		}
	for (const auto& point : m_spannerPoints)
		if (auto screen = ProjectToScreen(point.position, player))
		{
			drawList->AddCircleFilled({ screen->x, screen->y }, 3.5f, point.color);
			if (!point.label.empty())
				ShadowText(drawList, { screen->x, screen->y - 14.f }, point.color, point.label, true);
		}
	if (m_spannerTarget)
		if (auto screen = ProjectToScreen(*m_spannerTarget, player))
			drawList->AddCircle({ screen->x, screen->y }, 10.f, IM_COL32(255, 255, 255, 230), 24, 2.f);

	if (!m_prompt.empty())
		ShadowText(drawList, { center.x, center.y + 24.f }, white, m_prompt, true);
	if (!m_secondaryPrompt.empty())
		ShadowText(drawList, { center.x, center.y + 44.f }, grey, m_secondaryPrompt, true);

	if (m_toastTime > 0.f)
	{
		auto alpha = static_cast<int>(255.f * (std::min)(1.f, m_toastTime));
		ShadowText(drawList, { center.x, display.y - 160.f }, IM_COL32(255, 220, 140, alpha), m_toast, true);
	}

	DrawHardwarePanel(workshop);

	// The last reading, like the face of the gauge, in the top right.
	if (m_readout.time > 0.f)
	{
		float alpha = (std::min)(1.f, m_readout.time);
		float lineHeight = ImGui::GetTextLineHeightWithSpacing();
		float width = 360.f;
		for (const auto& line : m_readout.lines)
			width = (std::max)(width, ImGui::CalcTextSize(line.c_str()).x + 32.f);
		width = (std::max)(width, ImGui::CalcTextSize(m_readout.title.c_str()).x + 32.f);
		ImVec2 min = { display.x - width - 20.f, 60.f };
		ImVec2 max = { display.x - 20.f, min.y + 22.f + lineHeight * (m_readout.lines.size() + 1) };
		drawList->AddRectFilled(min, max, IM_COL32(15, 18, 22, static_cast<int>(200 * alpha)), 6.f);
		drawList->AddRect(min, max, m_readout.good ? IM_COL32(110, 220, 120, static_cast<int>(255 * alpha)) : IM_COL32(240, 110, 100, static_cast<int>(255 * alpha)), 6.f, 0, 2.f);
		drawList->AddText({ min.x + 12.f, min.y + 8.f }, IM_COL32(170, 200, 230, static_cast<int>(255 * alpha)), m_readout.title.c_str());
		for (size_t i = 0; i < m_readout.lines.size(); i++)
		{
			bool verdict = i + 1 == m_readout.lines.size() && m_readout.lines.size() > 1;
			ImU32 color = verdict ? (m_readout.good ? IM_COL32(120, 230, 130, 255) : IM_COL32(250, 120, 110, 255)) : IM_COL32(240, 240, 235, 255);
			color = (color & 0x00FFFFFF) | (static_cast<ImU32>(255 * alpha) << IM_COL32_A_SHIFT);
			drawList->AddText({ min.x + 12.f, min.y + 12.f + lineHeight * (i + 1) }, color, m_readout.lines[i].c_str());
		}
	}

	if (m_engineRpm > 0.f)
		ShadowText(drawList, { center.x, 40.f }, IM_COL32(255, 120, 90, 255), std::format("{:.0f} rpm", m_engineRpm), true);

	ShadowText(drawList, { 16.f, 12.f }, IM_COL32(140, 230, 140, 255), std::format("${:.0f}", workshop.GetMoney()), false);
	ShadowText(drawList, { 90.f, 12.f }, white, std::format("{:.1f} h on the clock", workshop.GetHours()), false);
	if (!m_packages.empty())
		ShadowText(drawList, { 16.f, 32.f }, IM_COL32(230, 190, 120, 255), std::format("{} deliver{} waiting by the door", m_packages.size(), m_packages.size() == 1 ? "y" : "ies"), false);

	DrawHotbar(workshop);

	if (showControls)
		ShadowText(drawList, { 16.f, display.y - 30.f }, grey,
			"WASD move  Space jump  Ctrl crouch  E use  LMB measure  RMB set down  G drop  1-0/wheel hotbar  Tab backpack  F move  R turn engine  Z/X roll engine  Esc menu", false);
}

std::string ShopWorld::SpannerLabel(size_t toolIndex) const
{
	const auto& tool = ToolCatalog::All()[toolIndex];
	auto it = m_toolSizes.find(toolIndex);
	int size = it == m_toolSizes.end() ? 0 : it->second;
	switch (tool.handKind)
	{
		case HandToolKind::RATCHET:
		case HandToolKind::BREAKER_BAR:
		case HandToolKind::TORQUE_WRENCH:	return size ? std::format("{} mm socket", size) : "no socket";
		case HandToolKind::WRENCHES:		return std::format("{} mm wrench   [C] Size", size ? size : tool.sizes.front());
		default:							return "";
	}
}

std::optional<ShopWorld::SocketTarget> ShopWorld::FindSocketTarget(const FirstPersonController& player, int heldSize)
{
	glm::vec3 eye = player.GetEyePosition();
	glm::vec3 forward = player.GetForward();
	const auto& tools = ToolCatalog::All();
	std::optional<SocketTarget> best;
	float bestOff = 0.014f;
	for (const auto& [ref, visual] : m_items)
	{
		if (!ref.IsTool() || tools[ref.id].handKind != HandToolKind::SOCKETS)
			continue;
		if (visual.place == ItemPlace::HAND || visual.place == ItemPlace::INVENTORY || visual.place == ItemPlace::PACKAGED)
			continue;
		const auto& sizes = tools[ref.id].sizes;
		int count = static_cast<int>(sizes.size());
		glm::vec3 position = visual.object->GetPosition();
		glm::quat rotation = visual.object->GetRotation();
		glm::vec3 scale = visual.object->GetScale();
		for (int i = 0; i < count; i++)
		{
			// A socket out on another tool isn't there to take; the one on the tool in hand leaves a gap to put it back in.
			bool putBack = sizes[i] == heldSize;
			if (SocketInUse(sizes[i]) && !putBack)
				continue;
			glm::vec3 local = SocketSlot(tools[ref.id], i) + glm::vec3(0.f, SOCKET_HEIGHT * 0.5f, 0.f);
			glm::vec3 world = position + rotation * (local * scale);
			float along = glm::dot(world - eye, forward);
			if (along <= 0.f || along > INTERACT_DISTANCE)
				continue;
			// Sizes are labelled up close; further off they'd run together.
			m_spannerPoints.push_back({ world, putBack ? IM_COL32(120, 200, 255, 220) : IM_COL32(200, 200, 210, 200), along < 0.9f ? std::to_string(sizes[i]) : "" });
			float off = glm::length(world - (eye + forward * along));
			if (off < bestOff)
			{
				bestOff = off;
				best = SocketTarget{ sizes[i], world, putBack };
			}
		}
	}
	return best;
}

std::optional<ShopWorld::FastenerTarget> ShopWorld::FindFastenerTarget(const Workshop& workshop, const FirstPersonController& player, int sizeMm)
{
	glm::vec3 eye = player.GetEyePosition();
	glm::vec3 forward = player.GetForward();
	PROFILE_SCOPE("tools: fastener target");
	// Everything near the crosshair, best first: the ones this spanner fits, then the nearest.
	struct Candidate
	{
		FastenerTarget target;
		bool fits = false;
		float off = 0.f;
		const Object* object = nullptr;
	};
	std::vector<Candidate> candidates;
	for (const auto& station : m_stations)
	{
		if (station.type != StationType::ENGINE_STAND || !station.present || station.engineIndex == m_engineOnDyno)
			continue;
		int engine = station.engineIndex;
		if (glm::distance(StandToWorld(engine, {}), eye) > INTERACT_DISTANCE + 1.f)
			continue;
		const auto& assembly = workshop.GetAssembly(engine);
		for (int slotIndex = static_cast<int>(AssemblySlot::HEAD_BOLT); slotIndex < static_cast<int>(AssemblySlot::COUNT); slotIndex++)
		{
			auto slot = static_cast<AssemblySlot>(slotIndex);
			for (int index = 0; index < assembly.SlotCount(slot); index++)
			{
				auto part = assembly.Get(slot, index);
				if (!part)
					continue;
				// Aimed at by its head (its origin is just under the head), not its middle, which is in the part.
				auto pose = PoseOnStand(engine, slot, index);
				glm::vec3 position = StandToWorld(engine, pose.offset + pose.rotation * glm::vec3(0.f, 0.005f, 0.f));
				float along = glm::dot(position - eye, forward);
				if (along <= 0.f || along > INTERACT_DISTANCE + 0.5f)
					continue;
				// Markers for the ones this spanner fits: orange finger tight, yellow snugged, green torqued.
				bool fits = !sizeMm || PartCatalog::Get(part->GetType()).fastener.headMm == sizeMm;
				if (fits)
				{
					auto tightness = part->GetTightness();
					m_spannerPoints.push_back({ position, tightness == FastenerTightness::TORQUED ? IM_COL32(120, 255, 150, 170)
						: tightness == FastenerTightness::SNUG ? IM_COL32(240, 220, 90, 170) : IM_COL32(255, 150, 70, 170), "" });
				}
				float off = glm::length(position - (eye + forward * along));
				// Within about 1.5 degrees of the crosshair, however far away.
				if (off < along * 0.026f)
				{
					const auto* visual = Find(ItemRef::Part(part->GetId()));
					candidates.push_back({ FastenerTarget{ engine, slot, index, position }, fits, off, visual ? visual->object.get() : nullptr });
				}
			}
		}
	}
	std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.fits != b.fits ? a.fits : a.off < b.off; });

	// Only one you can see: a rod nut inside the block can't be reached through the wall. Only the
	// best few are tried; a ray each, every frame, adds up behind a row of hidden bolts.
	constexpr size_t MAX_TRIED = 6;
	PROFILE_SCOPE("tools: fastener sight lines");
	auto held = GetHeld();
	const Object* heldObject = held ? held->object.get() : nullptr;
	for (size_t i = 0; i < candidates.size() && i < MAX_TRIED; i++)
	{
		const auto& candidate = candidates[i];
		glm::vec3 toward = candidate.target.position - eye;
		float distance = glm::length(toward);
		auto hit = ObjectManager::Get()->Raycast(eye, toward / distance, distance, [&](const pObject& object) {
			return object.get() != heldObject && object.get() != candidate.object && m_targets.contains(object.get());
		});
		if (!hit || hit->distance > distance - 0.03f)
			return candidate.target;
	}
	return std::nullopt;
}

bool ShopWorld::UpdateSpanner(Workshop& workshop, const FirstPersonController& player, const InputManager& input, size_t toolIndex)
{
	const auto& tool = ToolCatalog::All()[toolIndex];
	bool drive = IsDriveTool(tool);
	bool wrenches = tool.handKind == HandToolKind::WRENCHES;
	if (!drive && !wrenches)
		return false;
	bool pressedE = input.WasKeyPressed(GLFW_KEY_E);

	// The wrench set: C takes out the next size.
	if (wrenches)
	{
		if (!m_toolSizes.contains(toolIndex))
			m_toolSizes[toolIndex] = tool.sizes.front();
		if (input.WasKeyPressed(GLFW_KEY_C))
		{
			auto it = std::find(tool.sizes.begin(), tool.sizes.end(), m_toolSizes[toolIndex]);
			m_toolSizes[toolIndex] = it == tool.sizes.end() || it + 1 == tool.sizes.end() ? tool.sizes.front() : *(it + 1);
			Toast(std::format("{} mm wrench", m_toolSizes[toolIndex]));
		}
	}
	int size = m_toolSizes.contains(toolIndex) ? m_toolSizes[toolIndex] : 0;

	PROFILE_SCOPE("tools: spanner");
	// A ratchet, breaker bar or torque wrench takes the socket you look at in a socket set.
	if (drive)
		if (auto socket = FindSocketTarget(player, size))
		{
			m_spannerTarget = socket->position;
			if (socket->putBack)
			{
				m_prompt = std::format("[E] Put the {} mm socket back in the set", size);
				if (pressedE)
				{
					m_toolSizes.erase(toolIndex);
					Cue(Sound::SOCKET_CLINK, socket->position);
					Toast(std::format("{} mm socket back in the set.", size));
				}
			}
			else
			{
				// Swapping: the one on now goes back in its set.
				m_prompt = size ? std::format("[E] Swap the {} mm socket for the {} mm", size, socket->size)
					: std::format("[E] Put the {} mm socket on the {}", socket->size, tool.name);
				if (pressedE)
				{
					m_toolSizes[toolIndex] = socket->size;
					Cue(Sound::SOCKET_CLINK, socket->position);
					Toast(std::format("{} mm socket on the {}.", socket->size, tool.name));
				}
			}
			return pressedE;
		}

	auto target = FindFastenerTarget(workshop, player, size);
	if (!target)
		return false;
	m_spannerTarget = target->position;
	m_spannerFastener = *target;
	workshop.SetActiveEngine(target->engine);
	const auto& assembly = workshop.GetAssembly();
	auto part = assembly.Get(target->slot, target->index);
	const auto* holes = EngineAssembly::Holes(target->slot);
	auto tightness = part->GetTightness();
	std::string position = assembly.PositionName(target->slot, target->index);

	// A torque wrench only ever does up; anything else snugs a loose one and undoes a tight one.
	bool torqueWrench = tool.handKind == HandToolKind::TORQUE_WRENCH;
	bool tightening = torqueWrench || tightness == FastenerTightness::LOOSE;
	std::string state = tightness == FastenerTightness::TORQUED ? "torqued" : tightness == FastenerTightness::SNUG ? "snugged up by feel, not torqued" : "finger tight";
	if (torqueWrench && tightness == FastenerTightness::TORQUED)
	{
		m_prompt = std::format("{}: already torqued", position);
		return pressedE;
	}
	if (auto problem = workshop.TurningProblem(target->slot, target->index, toolIndex, size, tightening))
	{
		m_prompt = std::format("{} ({})", position, state);
		m_secondaryPrompt = *problem;
		// E says why rather than opening the stand behind it.
		if (pressedE)
			Toast(*problem);
		return pressedE;
	}
	std::string spec = std::format("{:.0f} Nm", holes->torqueNm);
	m_prompt = torqueWrench ? std::format("[E] Torque {} to {}", position, spec)
		: tightening ? std::format("[E] Snug up {} (spec {}: a torque wrench does it properly)", position, spec)
		: std::format("[E] Loosen {}", position);
	m_secondaryPrompt = std::format("It's {}.   [Shift+E] Every one on that part", state);
	if (!pressedE)
		return false;

	// Shift: the rest of that part's holes of the same kind, wherever they need the same job.
	std::vector<int> indices = { target->index };
	if (m_fillAllHoles)
	{
		int first = target->index / holes->perParent * holes->perParent;
		for (int index = first; index < first + holes->perParent; index++)
		{
			auto other = index != target->index ? assembly.Get(target->slot, index) : nullptr;
			if (!other)
				continue;
			bool needsIt = tightening ? other->GetTightness() != FastenerTightness::TORQUED && (torqueWrench || other->GetTightness() == FastenerTightness::LOOSE)
				: other->GetTightness() != FastenerTightness::LOOSE;
			if (needsIt)
				indices.push_back(index);
		}
	}
	int done = 0;
	for (int index : indices)
		if (tightening ? workshop.TightenFastener(target->slot, index, toolIndex, size) : workshop.LoosenFastener(target->slot, index, toolIndex, size))
			done++;
	// Heard at the bolt: a ratchet's run of clicks, a spanner pulled round, a breaker bar's crack, and
	// a torque wrench's click as it lets go at its setting.
	if (done > 0)
	{
		Sound turning = tool.handKind == HandToolKind::BREAKER_BAR ? Sound::BREAKER_BAR
			: tool.handKind == HandToolKind::WRENCHES ? Sound::WRENCH : Sound::RATCHET;
		Cue(turning, target->position);
		if (torqueWrench)
			Cue(Sound::TORQUE_CLICK, target->position);
	}
	std::string verb = torqueWrench ? "Torqued" : tightening ? "Snugged up" : "Loosened";
	if (done == 1)
		Toast(std::format("{} {}.", verb, position));
	else if (done > 1)
		Toast(std::format("{} {} {}s.", verb, done, holes->name));
	return true;
}
