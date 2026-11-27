// The project car's tools in hand: pouring at the fillers, bleeding at the nipples, springs onto struts and tires onto
// wheels, and the gauges and strobe that set the car up.
#include "ShopWorld.h"
#include "LV/Engine/Lordi.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <tuple>

#include "LV/Engine/Graphics/Models/ModelManager.h"
#include "LV/Engine/Utility/InputManager.h"
#include "../Controls.h"
#include "CarBody.h"
#include "ShopWorldShared.h"

using namespace ShopLayout;

namespace
{
	// Where each circuit is filled, in the car's own space: the oil cap on the valve cover, the radiator cap, the brake and
	// clutch reservoirs on the firewall, the gearbox's and diff's fill plugs, and the fuel filler in the right rear quarter.
	glm::vec3 FillPoint(FluidCircuit circuit)
	{
		switch (circuit)
		{
			case FluidCircuit::ENGINE_OIL:		return { CAR_ENGINE_MOUNT.x, CAR_ENGINE_MOUNT.y + 0.19f, 0.05f };
			case FluidCircuit::COOLANT:			return { CarBody::NOSE_X - 0.09f, 0.67f, 0.2f };	// the radiator's cap
			case FluidCircuit::BRAKES:			return { 1.15f, 0.78f, -0.33f };
			case FluidCircuit::CLUTCH:			return { 0.93f, 0.82f, -0.56f };
			case FluidCircuit::TRANSMISSION:	return { 0.68f, 0.3f, 0.1f };
			case FluidCircuit::DIFFERENTIAL:	return { -1.05f, 0.15f, 0.1f };
			case FluidCircuit::FUEL:			return { CarBody::FLAP_X, CarBody::FLAP_Y, CarBody::SIDE - CarBody::SKIN + 0.012f };	// the neck's mouth behind the flap
			case FluidCircuit::COUNT:			break;
		}
		return {};
	}

	// How fast it goes in, litres a second, from a jug through the funnel (the reservoirs a trickle, the tank from a can).
	double PourRate(FluidCircuit circuit)
	{
		switch (circuit)
		{
			case FluidCircuit::BRAKES:
			case FluidCircuit::CLUTCH:		return 0.05;
			case FluidCircuit::FUEL:		return 1.5;
			case FluidCircuit::TRANSMISSION:
			case FluidCircuit::DIFFERENTIAL: return 0.25;
			default:						return 0.5;
		}
	}

	constexpr float HOLD_SECONDS = 1.5f;	// a spring compressed on or off, a tire levered on or off, a circuit drained
	constexpr float CLICK_PAUSE = 0.6f;		// between goes with the bleeder, clicks of the distributor or the tie rods
	constexpr double PUMP_KPA = 25.0;		// a second, in or out of a tire
}

bool ShopWorld::UpdateCarTool(Workshop& workshop, const FirstPersonController& player, const InputManager& input, size_t toolIndex, float deltaTime)
{
	const auto& tool = ToolCatalog::Of(toolIndex);
	auto kind = tool.handKind;
	if (kind < HandToolKind::FUNNEL || kind == HandToolKind::CAR_KEY)
		return false;
	m_carToolPause = (std::max)(0.f, m_carToolPause - deltaTime);
	glm::vec3 eye = player.GetEyePosition();
	glm::vec3 forward = player.GetForward();
	bool leftHeld = Controls::Held(input, Control::PRIMARY), rightHeld = Controls::Held(input, Control::SET_DOWN);
	bool leftPressed = Controls::Pressed(input, Control::PRIMARY), rightPressed = Controls::Pressed(input, Control::SET_DOWN);
	// How near the crosshair a point has to be to be the one aimed at.
	auto offAim = [&](const glm::vec3& point) -> std::optional<float> {
		float along = glm::dot(point - eye, forward);
		if (along <= 0.f || along > INTERACT_DISTANCE)
			return std::nullopt;
		float off = glm::length(point - (eye + forward * along));
		if (off > (std::max)(0.07f, along * 0.05f))
			return std::nullopt;
		return off;
	};
	// Held on one thing: how long, starting again when it's something else or let go.
	auto holdOn = [&](uint64_t what, bool held) {
		if (!held || m_carHoldOn != what)
			m_carHold = 0.f;
		m_carHoldOn = held ? what : 0;
		if (held)
			m_carHold += deltaTime;
		return m_carHold;
	};

	// Spring onto strut, tire onto wheel: aimed at a loose strut or wheel.
	if (kind == HandToolKind::SPRING_COMPRESSOR || kind == HandToolKind::TIRE_IRONS)
	{
		bool springs = kind == HandToolKind::SPRING_COMPRESSOR;
		PartType hostType = springs ? PartType::FRONT_STRUT : PartType::WHEEL;
		PartType riderType = springs ? PartType::FRONT_COIL_SPRING : PartType::TIRE;
		auto loose = [&](const ItemVisual& visual) {
			return visual.part && visual.object->IsVisible() && (visual.place == ItemPlace::WORLD || visual.place == ItemPlace::BENCH
				|| visual.place == ItemPlace::STORAGE);
		};
		ItemVisual* host = nullptr;
		float best = 0.f;
		for (auto& [ref, visual] : m_items)
			if (loose(visual) && visual.part->GetType() == hostType)
			{
				auto model = ModelOf(visual.object);
				glm::vec3 middle = glm::vec3(visual.object->GetModelMatrix() * glm::vec4((model->GetBoundsMin() + model->GetBoundsMax()) * 0.5f, 1.f));
				if (auto off = offAim(middle); off && (!host || *off < best))
				{
					host = &visual;
					best = *off;
				}
			}
		if (!host)
			return false;
		m_spannerTarget = host->object->GetPosition();
		std::string name = host->part->GetName();
		if (auto rider = workshop.CarriedOn(*host->part))
		{
			m_prompt = std::format("{} with its {}   [RMB] Hold to take the {} off", name, rider->GetName(), springs ? "spring" : "tire");
			float held = holdOn(host->part->GetId(), rightHeld);
			m_pressAimed = held > 0.f;
			m_pressProgress = held / HOLD_SECONDS;
			if (held >= HOLD_SECONDS)
			{
				m_carHold = 0.f;
				m_carHoldOn = 0;
				if (workshop.TakeOffCarPart(host->part))
				{
					// It comes off beside it.
					if (auto* visual = Find(ItemRef::Part(rider->GetId())))
						PlaceInWorld(*visual, host->object->GetPosition() + glm::vec3(0.f, 0.35f, 0.f) + glm::cross(forward, glm::vec3(0.f, 1.f, 0.f)) * 0.4f, {});
					Cue(Sound::FIT_PART, host->object->GetPosition());
				}
				Toast(workshop.GetLog().empty() ? "" : workshop.GetLog().back());
			}
			return true;
		}
		// A spring or tire lying about near it.
		ItemVisual* rider = nullptr;
		float nearest = 2.5f;
		for (auto& [ref, visual] : m_items)
			if (loose(visual) && visual.part->GetType() == riderType && !workshop.CarHostOf(*visual.part))
				if (float distance = glm::distance(visual.object->GetPosition(), host->object->GetPosition()); distance < nearest)
				{
					nearest = distance;
					rider = &visual;
				}
		if (!rider)
		{
			m_prompt = std::format("{}: set a {} down by it to {} it", name, PartTypeName(riderType), springs ? "compress onto" : "lever onto");
			return true;
		}
		m_prompt = std::format("{}   [LMB] Hold to {} the {} onto it", name, springs ? "compress" : "lever", rider->part->GetName());
		float held = holdOn(host->part->GetId(), leftHeld);
		m_pressAimed = held > 0.f;
		m_pressProgress = held / HOLD_SECONDS;
		if (held >= HOLD_SECONDS)
		{
			m_carHold = 0.f;
			m_carHoldOn = 0;
			if (workshop.FitOntoCarPart(host->part, rider->part))
				Cue(Sound::FIT_PART, host->object->GetPosition());
			Toast(workshop.GetLog().empty() ? "" : workshop.GetLog().back());
		}
		return true;
	}

	// Everything else works on the car itself.
	const auto* build = workshop.GetCar();
	if (!build || m_rig.carStation >= m_stations.size() || !m_stations[m_rig.carStation].present)
		return false;
	const auto& car = build->assembly;
	const auto& spec = car.GetSpec();

	// The funnel and its drain-plug spanner, at where each circuit is drained (CarDrainPoints): undone, it comes out into the
	// drain pan set down under it, or with none there, onto the floor.
	if (kind == HandToolKind::FUNNEL)
	{
		const std::vector<DrainPoint> drains = CarDrainPoints(workshop);
		const DrainPoint* aimed = nullptr;
		float best = 0.f;
		for (const auto& drain : drains)
		{
			m_fitPoints.push_back({ drain.at, car.GetFluid(drain.circuit).Total() > 0.0 });
			if (auto off = offAim(drain.at); off && (!aimed || *off < best))
			{
				aimed = &drain;
				best = *off;
			}
		}
		if (!aimed)
			return false;
		const auto circuit = aimed->circuit;
		m_spannerTarget = aimed->at;
		const double level = car.GetFluid(circuit).Total();
		if (circuit == FluidCircuit::ENGINE_OIL && workshop.CarOilDrainPlugIn())
		{
			m_prompt = std::format("{}: {:.2f} l of oil in the engine", aimed->where, level);
			m_secondaryPrompt = std::format("Set the drain pan under it, then undo the plug with a {} mm spanner and take it out: the oil comes straight out",
				PartCatalog::Get(PartType::OIL_DRAIN_PLUG).fastener.headMm);
			return true;
		}
		m_prompt = std::format("{}: {:.2f} l of {}   [RMB] Hold to undo it and let it out", aimed->where, level, FluidCircuitLower(circuit));
		// (And whether the drain pan's under it: without, it's all over the floor.)
		m_secondaryPrompt = std::string(DrainPanUnder(aimed->at) ? "The drain pan's under it" : "No drain pan under it: set one down first, or it'll be all over the floor")
			+ (circuit == FluidCircuit::BRAKES || circuit == FluidCircuit::CLUTCH ? ". It leaves the lines full of air: bleed them after filling" : "");
		// (Held on a drain point: a circuit, numbered past any part's id.)
		float holding = holdOn((uint64_t(1) << 40) + static_cast<uint64_t>(circuit) * 16 + static_cast<uint64_t>(aimed - drains.data()), rightHeld && level > 0.0);
		m_pressAimed = holding > 0.f;
		m_pressProgress = holding / HOLD_SECONDS;
		if (holding >= HOLD_SECONDS)
		{
			m_carHold = 0.f;
			m_carHoldOn = 0;
			DrainInto(workshop, circuit, aimed->at);
			Cue(Sound::CLICK, aimed->at, 0.5f);
			Toast(workshop.GetLog().empty() ? "" : workshop.GetLog().back());
		}
		return true;
	}

	// At the fillers: a jug poured in with the cap off, a cap put back on its own.
	if (kind == HandToolKind::FLUID_CONTAINER || kind == HandToolKind::FILLER_CAP)
	{
		std::optional<FluidCircuit> aimed;
		float best = 0.f;
		for (int c = 0; c < FLUID_CIRCUITS; c++)
		{
			const auto circuit = static_cast<FluidCircuit>(c);
			// (A cap goes on its own filler only.)
			if (kind == HandToolKind::FILLER_CAP && tool.capCircuit != c)
				continue;
			glm::vec3 point = FillerSeat(workshop, circuit).at;
			m_fitPoints.push_back({ point, kind == HandToolKind::FILLER_CAP ? !car.CapOn(circuit) : !car.CircuitOpen(circuit) && !car.CapOn(circuit) });
			if (auto off = offAim(point); off && (!aimed || *off < best))
			{
				aimed = circuit;
				best = *off;
			}
		}
		if (!aimed)
			return false;
		const auto circuit = *aimed;
		m_spannerTarget = FillerSeat(workshop, circuit).at;
		// The fuel's filler is behind its flap.
		if (circuit == FluidCircuit::FUEL && !IsCarPanelOpen(CarPanel::FUEL_FLAP))
		{
			m_prompt = "[LMB] Open the fuel flap";
			if (leftPressed)
				ToggleCarPanel(CarPanel::FUEL_FLAP);
			return true;
		}
		const auto& fs = spec.Fluid(circuit);
		const double level = car.GetFluid(circuit).Total();
		const auto open = car.CircuitOpen(circuit);
		const bool capped = car.CapOn(circuit).has_value();
		ItemVisual* held = GetHeld();

		if (kind == HandToolKind::FILLER_CAP)
		{
			if (capped)
			{
				m_prompt = std::format("The {}'s cap is on already", FluidCircuitLower(circuit));
				return true;
			}
			m_prompt = std::format("{}: {:.2f} l (full at {:.2f})   [LMB] Put the {} on", FluidCircuitName(circuit), level, fs.level.max, tool.name);
			if (leftPressed && m_carToolPause <= 0.f && held && workshop.PutCarCap(circuit, held->ref.id))
			{
				m_carToolPause = CLICK_PAUSE;
				// On the filler, out of the hand (see UpdateCarFittings).
				SetPlace(*held, ItemPlace::ATTACHED);
				SyncHand();
				Cue(Sound::FIT_PART, m_spannerTarget.value_or(glm::vec3(0.f)), 0.5f);
				Toast(workshop.GetLog().back());
			}
			return true;
		}

		if (kind == HandToolKind::FLUID_CONTAINER)
		{
			const auto product = static_cast<FluidProduct>(tool.fluidProduct);
			const double left = held ? workshop.ContainerLitres(held->ref.id) : 0.0;
			if (capped)
			{
				m_prompt = std::format("{}: {:.2f} l (full at {:.2f})   Its cap is on: take it off first (E on it)", FluidCircuitName(circuit), level,
					fs.level.max);
				m_secondaryPrompt = fs.check;
				return true;
			}
			m_prompt = std::format("{}: {:.2f} l (full at {:.2f})   [LMB] Hold to pour the {} ({:.2f} l left in it)", FluidCircuitName(circuit), level,
				fs.level.max, FluidProductName(product), left);
			m_secondaryPrompt = open ? std::format("Open: the {} isn't on, so it would run straight out", *open)
				: FluidFitFor(circuit, product) == FluidFit::RIGHT ? std::string(fs.check)
				: std::format("The book calls for {} here", fs.grade);
			if (leftPressed && open)
				Toast(std::format("The {} isn't on: it would run straight out onto the floor.", *open));
			if (leftHeld && !open && held)
			{
				const double poured = workshop.PourFromContainer(circuit, held->ref.id, PourRate(circuit) * deltaTime, true);
				if (poured <= 0.0 && leftPressed && level >= fs.level.max * 0.99)
					Toast(std::format("The {} is full.", FluidCircuitName(circuit)));
				// Emptied: it's gone from the hand (and the shop: SyncItems).
				if (!workshop.OwnsTool(held->ref.id))
					Toast(std::format("The {} is empty: it goes in the bin.", FluidProductName(product)));
			}
			return true;
		}

		return true;
	}

	if (kind == HandToolKind::BRAKE_BLEEDER)
	{
		std::optional<BleedPoint> aimed;
		float best = 0.f;
		for (int p = 0; p < BLEED_POINTS; p++)
		{
			int index = 0;
			CarSlot slot = CarAssembly::BleedNipplePart(static_cast<BleedPoint>(p), index);
			if (!car.IsFilled(slot, index))
				continue;
			glm::vec3 point = CarSlotCenter(slot, index);
			m_fitPoints.push_back({ point, true });
			if (auto off = offAim(point); off && (!aimed || *off < best))
			{
				aimed = static_cast<BleedPoint>(p);
				best = *off;
			}
		}
		if (!aimed)
			return false;
		int index = 0;
		CarSlot slot = CarAssembly::BleedNipplePart(*aimed, index);
		m_spannerTarget = CarSlotCenter(slot, index);
		auto circuit = *aimed == BleedPoint::CLUTCH ? FluidCircuit::CLUTCH : FluidCircuit::BRAKES;
		const auto& fs = spec.Fluid(circuit);
		// The bleeder goes on the nipple and cracks it open; then the pedal's pumped from the driver's seat, and it's taken off
		// again (closing the nipple) once the tube runs clear.
		if (car.BleederOn(*aimed))
		{
			m_prompt = std::format("{}: there's already a bleeder on it", BleedPointName(*aimed));
			return true;
		}
		m_prompt = std::format("{}   [LMB] Put the bleeder on and crack the nipple open (the reservoir has {:.2f} l: MIN {:.2f})",
			BleedPointName(*aimed), car.GetFluid(circuit).Total(), fs.level.min);
		m_secondaryPrompt = *aimed == BleedPoint::CLUTCH ? "Then pump the clutch pedal from the driver's seat"
			: "Then pump the brake pedal from the driver's seat. Farthest from the master cylinder first: rear right, rear left, front right, front left";
		ItemVisual* held = GetHeld();
		if (leftPressed && m_carToolPause <= 0.f && held && held->ref.IsTool())
		{
			m_carToolPause = CLICK_PAUSE;
			if (workshop.AttachBleeder(*aimed, held->ref.id))
			{
				// It stays on the nipple, out of the hand, till it's taken off (see UpdateBleeders).
				if (auto host = car.Get(slot, index))
					held->hostPart = host->GetId();
				SetPlace(*held, ItemPlace::ATTACHED);
				SyncHand();
				Cue(Sound::WRENCH, m_spannerTarget.value_or(glm::vec3(0.f)), 0.6f);
			}
			if (!workshop.GetLog().empty())
				Toast(workshop.GetLog().back());
		}
		return true;
	}

	if (kind == HandToolKind::TIMING_LIGHT)
	{
		int engine = workshop.GetEngineInCar();
		if (engine < 0)
			return false;
		// The crank pulley's marks, at the front of the engine.
		glm::vec3 pulley = CarToWorld({ CAR_ENGINE_MOUNT.x + 0.29f, CAR_ENGINE_MOUNT.y - 0.25f, 0.f });
		if (!offAim(pulley) && !offAim(pulley + CarRotation() * glm::vec3(0.f, 0.15f, 0.f)))
			return false;
		m_spannerTarget = pulley;
		if (!workshop.CarStarts())
		{
			m_prompt = "The crank pulley's timing marks: the engine has to be running to strobe them";
			return true;
		}
		auto tune = car.GetTune();
		double shown = std::round(tune.ignitionTiming * 2.0) / 2.0;
		m_prompt = std::format("Timing {:.1f} deg BTDC at {:.0f} rpm (spec {:.0f} +/- 1)   [LMB] Advance   [RMB] Retard", shown, tune.idleSpeed,
			spec.tune.ignitionTimingNominal);
		if ((leftPressed || rightPressed) && m_carToolPause <= 0.f)
		{
			m_carToolPause = CLICK_PAUSE * 0.5f;
			tune.ignitionTiming += leftPressed ? 0.5 : -0.5;
			workshop.SetCarTune(tune);
			Cue(Sound::WRENCH, pulley, 0.4f);
		}
		return true;
	}

	if (kind == HandToolKind::SCREWDRIVER)
	{
		int engine = workshop.GetEngineInCar();
		if (engine < 0)
			return false;
		const auto& assembly = workshop.GetAssembly(engine);
		glm::quat turn = StandRotation(engine);
		// The carburettor's two screws, either side of its base, and with the valve cover off each rocker's adjuster.
		struct Screw
		{
			glm::vec3 position = {};
			int kind = 0;		// 0 idle speed, 1 mixture, 2 intake lash, 3 exhaust lash
			int index = 0;
		};
		std::vector<Screw> screws;
		if (assembly.Get(AssemblySlot::CARBURETTOR, 0))
		{
			glm::vec3 carb = SlotWorldCenter(engine, AssemblySlot::CARBURETTOR, 0);
			screws.push_back({ carb + turn * glm::vec3(0.035f, 0.f, 0.03f), 0, 0 });
			screws.push_back({ carb + turn * glm::vec3(-0.035f, -0.02f, 0.03f), 1, 0 });
		}
		if (!assembly.Get(AssemblySlot::VALVE_COVER, 0))
			for (int i = 0; i < assembly.SlotCount(AssemblySlot::ROCKER_ARM); i++)
				if (assembly.Get(AssemblySlot::ROCKER_ARM, i))
					screws.push_back({ SlotWorldCenter(engine, AssemblySlot::ROCKER_ARM, i), i % 2 == 1 ? 3 : 2, i });
		const Screw* aimed = nullptr;
		float best = 0.f;
		for (const auto& screw : screws)
		{
			m_fitPoints.push_back({ screw.position, true });
			if (auto off = offAim(screw.position); off && (!aimed || *off < best))
			{
				aimed = &screw;
				best = *off;
			}
		}
		if (!aimed)
			return false;
		m_spannerTarget = aimed->position;
		auto tune = car.GetTune();
		const auto& ts = spec.tune;
		bool click = (leftPressed || rightPressed) && m_carToolPause <= 0.f;
		double direction = leftPressed ? 1.0 : -1.0;	// in, or out
		if (aimed->kind <= 1)
		{
			if (!workshop.CarStarts())
			{
				m_prompt = std::format("The carburettor's {} screw: the engine has to be running to set it", aimed->kind == 0 ? "idle speed" : "mixture");
				return true;
			}
			// How it idles, by ear.
			FluidProduct fuel = car.GetFluid(FluidCircuit::FUEL).Main();
			auto effect = RoadTest::TuneEffect(tune, spec, workshop.GetEngine(engine).lastDyno.compressionRatio, fuel);
			std::string sound = !effect.idles ? "it hunts and wants to stall" : !ts.idleMixture.Contains(tune.idleMixture) ? "it's a little lumpy"
				: !ts.idleSpeed.Contains(tune.idleSpeed) ? "smooth, but not at the speed the manual wants" : "it idles sweetly";
			if (aimed->kind == 0)
				m_prompt = std::format("Idle speed screw: {:.0f} rpm on the tach (spec {:.0f} to {:.0f}), {}   [LMB] In (faster)   [RMB] Out (slower)",
					tune.idleSpeed, ts.idleSpeed.min, ts.idleSpeed.max, sound);
			else
				m_prompt = std::format("Mixture screw: {:.2f} turns out (spec {:.1f} to {:.1f}), {}   [LMB] In (leaner)   [RMB] Out (richer)",
					tune.idleMixture, ts.idleMixture.min, ts.idleMixture.max, sound);
			if (click)
			{
				if (aimed->kind == 0)
					tune.idleSpeed += direction * 25.0;
				else
					tune.idleMixture -= direction * 0.125;
				workshop.SetCarTune(tune);
			}
		}
		else
		{
			bool exhaust = aimed->kind == 3;
			const Range& range = exhaust ? ts.exhaustLash : ts.intakeLash;
			double& lash = exhaust ? tune.exhaustLash : tune.intakeLash;
			// The gap's only known with a feeler gauge in it.
			bool feelers = false;
			const auto& tools = ToolCatalog::All();
			for (size_t i = 0; i < tools.size(); i++)
				if (tools[i].name.find("Feeler") != std::string::npos && workshop.OwnsTool(i))
					feelers = true;
			// The lash is set by what it is hot (the tune's figure); cold, the valve's shorter in the head and it reads tighter.
			// The spec's given both ways: hot just after a drive, cold once it's sat, and in between, for how warm it is.
			double heat = workshop.CarEngineHeat();
			double tighter = Workshop::LASH_HOT_GROWTH * (1.0 - heat);
			double now = (std::max)(0.0, lash - tighter);
			std::string reading = feelers ? std::format("the feelers say {:.2f} mm", std::round(now * 100.0) / 100.0) : "no feeler gauge to read the gap";
			std::string warmth = heat > 0.95 ? "hot" : heat < 0.05 ? "cold" : std::format("warm, {:.0f}% of the way to hot", heat * 100.0);
			m_prompt = std::format("{} rocker {}, engine {}: {} (spec {:.2f} to {:.2f} cold, {:.2f} to {:.2f} hot)   [LMB] In (tighter)   [RMB] Out (looser)",
				exhaust ? "Exhaust" : "Intake", aimed->index / 2 + 1, warmth, reading, range.min - Workshop::LASH_HOT_GROWTH,
				range.max - Workshop::LASH_HOT_GROWTH, range.min, range.max);
			m_secondaryPrompt = "The rockers on one side are set alike: set one, and do the rest to match";
			if (click)
			{
				lash = (std::max)(0.0, lash - direction * 0.01);
				workshop.SetCarTune(tune);
			}
		}
		if (click)
		{
			m_carToolPause = CLICK_PAUSE * 0.4f;
			Cue(Sound::WRENCH, aimed->position, 0.3f);
		}
		return true;
	}

	if (kind == HandToolKind::TIRE_GAUGE || kind == HandToolKind::ALIGNMENT_GAUGE)
	{
		bool gauge = kind == HandToolKind::TIRE_GAUGE;
		std::optional<int> aimed;
		float best = 0.f;
		for (int corner = 0; corner < (gauge ? 4 : 2); corner++)
		{
			if (!car.IsFilled(CarSlot::WHEEL, corner) || !car.IsFilled(CarSlot::TIRE, corner))
				continue;
			glm::vec3 point = CarSlotCenter(CarSlot::WHEEL, corner);
			if (auto off = offAim(point); off && (!aimed || *off < best))
			{
				aimed = corner;
				best = *off;
			}
		}
		// The gauges are looked at the wheels with, so aiming anywhere on one will do.
		if (!aimed)
			for (int corner = 0; corner < (gauge ? 4 : 2); corner++)
				if (car.IsFilled(CarSlot::WHEEL, corner) && car.IsFilled(CarSlot::TIRE, corner))
				{
					glm::vec3 point = CarSlotCenter(CarSlot::WHEEL, corner);
					float along = glm::dot(point - eye, forward);
					if (along > 0.f && along < INTERACT_DISTANCE && glm::length(point - (eye + forward * along)) < 0.3f)
						aimed = corner;
				}
		if (!aimed)
			return false;
		m_spannerTarget = CarSlotCenter(CarSlot::WHEEL, *aimed);
		auto tune = car.GetTune();
		std::string corner = CornerName(static_cast<Corner>(*aimed));
		if (gauge)
		{
			double& pressure = tune.tirePressure[*aimed];
			m_prompt = std::format("{} tire: {:.0f} kPa ({:.0f} psi), cold spec {:.0f}   [LMB] Hold to pump up   [RMB] Hold to let some out", corner,
				pressure, pressure * 0.145038, spec.tune.tirePressureNominal);
			if (leftHeld != rightHeld)
			{
				pressure = (std::max)(0.0, pressure + (leftHeld ? PUMP_KPA : -PUMP_KPA) * deltaTime);
				workshop.SetCarTune(tune);
			}
			return true;
		}
		m_prompt = std::format("{} wheel: camber {:.2f} deg, caster {:.2f} deg, toe {:+.1f} mm in (spec {:.0f} to {:.0f})   [LMB] More toe in   [RMB] Less",
			corner, tune.frontCamber, tune.caster, tune.frontToe, spec.tune.frontToe.min, spec.tune.frontToe.max);
		if (!car.IsFilled(CarSlot::TIE_ROD_END, *aimed))
			m_secondaryPrompt = "There's no tie rod end on this side to set the toe with";
		else if ((leftPressed || rightPressed) && m_carToolPause <= 0.f)
		{
			m_carToolPause = CLICK_PAUSE * 0.5f;
			tune.frontToe += leftPressed ? 0.5 : -0.5;
			workshop.SetCarTune(tune);
			Cue(Sound::WRENCH, m_spannerTarget.value_or(glm::vec3(0.f)), 0.4f);
		}
		return true;
	}
	return false;
}

ShopWorld::FillerSeatAt ShopWorld::FillerSeat(const Workshop& workshop, FluidCircuit circuit) const
{
	// The neck on its part, in that part's own space (where its model's neck is: ShopAssets).
	pPart host;
	glm::vec3 neck = {};
	if (const auto* build = workshop.GetCar())
		switch (circuit)
		{
			case FluidCircuit::ENGINE_OIL:
				if (const int engine = build->assembly.GetEngine(); engine >= 0 && engine < workshop.GetEngineCount())
					host = workshop.GetAssembly(engine).Get(AssemblySlot::VALVE_COVER, 0);
				neck = { 0.12f, 0.059f, 0.055f };
				break;
			case FluidCircuit::COOLANT:
				host = build->assembly.Get(CarSlot::RADIATOR, 0);
				neck = { 0.12f, 0.441f, 0.f };
				break;
			case FluidCircuit::BRAKES:
				host = build->assembly.Get(CarSlot::BRAKE_MASTER_CYLINDER, 0);
				neck = { 0.03f, 0.122f, 0.f };
				break;
			case FluidCircuit::CLUTCH:
				host = build->assembly.Get(CarSlot::CLUTCH_MASTER_CYLINDER, 0);
				neck = { 0.f, 0.086f, 0.f };
				break;
			default:
				break;
		}
	if (const ItemVisual* visual = host ? Find(ItemRef::Part(host->GetId())) : nullptr)
		return { glm::vec3(visual->object->GetModelMatrix() * glm::vec4(neck, 1.f)), visual->object->GetRotation() };
	// The fuel filler faces out of the right rear quarter: its cap's up (+y) turned to the car's +z.
	if (circuit == FluidCircuit::FUEL)
		return { CarToWorld(FillPoint(circuit)), CarRotation() * glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.f, 0.f, 0.f)) };
	return { CarToWorld(FillPoint(circuit)), CarRotation() };
}

bool ShopWorld::DrainPanUnder(const glm::vec3& at) const
{
	// Lying on the floor (or anything under the car), below the point and close enough under it to catch it: its rim's
	// 0.24 across, and a stream wanders.
	// (Set down: not while it's being slid somewhere.)
	constexpr float CATCHES = 0.26f;
	for (size_t s = m_fixedStations; s < m_stations.size(); s++)
	{
		const auto& station = m_stations[s];
		if (!IsDrainPan(station) || !station.present || (m_move && m_move->stationIndex == s))
			continue;
		const glm::vec3 pan = station.position;
		const glm::vec2 off = { pan.x - at.x, pan.z - at.z };
		if (pan.y < at.y && glm::dot(off, off) <= CATCHES * CATCHES)
			return true;
	}
	return false;
}

double ShopWorld::DrainInto(Workshop& workshop, FluidCircuit circuit, const glm::vec3& at)
{
	const auto* build = workshop.GetCar();
	if (!build)
		return 0.0;
	const FluidProduct main = build->assembly.GetFluid(circuit).Main();
	const bool caught = DrainPanUnder(at);
	const double drained = workshop.DrainCar(circuit, caught);
	if (drained > 0.0 && !caught && main != FluidProduct::COUNT)
		SpillOnFloor(at, main, drained);
	return drained;
}

void ShopWorld::SpillOnFloor(const glm::vec3& at, FluidProduct product, double litres)
{
	// Only the shop's floor takes it (it's painted on, and wiped off with thinner and a rag like paint); outside, it soaks
	// into the ground. A puddle the size of what came out, a darker, dirtier shade of it.
	const glm::vec3 point = { at.x, 0.f, at.z };
	if (!GetInterior().InGarage(point + glm::vec3(0.f, 0.1f, 0.f)))
		return;
	const auto colour = FluidColour(product);
	auto channel = [&](float c) { return static_cast<uint32_t>(std::clamp(c * 0.55f, 0.f, 1.f) * 255.f); };
	const uint32_t rgb = (channel(colour[0]) << 16) | (channel(colour[1]) << 8) | channel(colour[2]);
	const float radius = std::clamp(0.1f + 0.12f * std::sqrt(static_cast<float>(litres)), 0.1f, 0.55f);
	m_paint.Dab({ 1, 1, 0.f }, point, radius, WallPaint::NearestColor(rgb));
}

std::vector<ShopWorld::DrainPoint> ShopWorld::CarDrainPoints(const Workshop& workshop) const
{
	std::vector<DrainPoint> drains;
	const auto* build = workshop.GetCar();
	if (!build)
		return drains;
	const auto& car = build->assembly;
	// The middle of a part's underside, where its plug is, as the part is.
	auto bottomOf = [&](const pPart& part) -> std::optional<glm::vec3> {
		const ItemVisual* visual = part ? Find(ItemRef::Part(part->GetId())) : nullptr;
		const Model* model = visual ? Lordi::Get()->GetModelManager()->PeekModel(visual->object->GetModelIndex()) : nullptr;
		if (!model)
			return std::nullopt;
		const glm::vec3 low = model->GetBoundsMin(), high = model->GetBoundsMax();
		return glm::vec3(visual->object->GetModelMatrix() * glm::vec4((low.x + high.x) * 0.5f, low.y, (low.z + high.z) * 0.5f, 1.f));
	};
	if (const int engine = workshop.GetEngineInCar(); engine >= 0)
	{
		const auto& assembly = workshop.GetAssembly(engine);
		if (const ItemVisual* plug = assembly.Get(AssemblySlot::DRAIN_PLUG, 0) ? Find(ItemRef::Part(assembly.Get(AssemblySlot::DRAIN_PLUG, 0)->GetId())) : nullptr)
			drains.push_back({ FluidCircuit::ENGINE_OIL, plug->object->GetPosition(), "The oil pan's drain plug" });
		else if (auto at = bottomOf(assembly.Get(AssemblySlot::OIL_PAN, 0)))
			drains.push_back({ FluidCircuit::ENGINE_OIL, *at, "The oil pan's drain hole" });
	}
	for (auto [circuit, slot, where] : std::initializer_list<std::tuple<FluidCircuit, CarSlot, const char*>>{
		{ FluidCircuit::COOLANT, CarSlot::RADIATOR, "The radiator's drain plug" },
		{ FluidCircuit::TRANSMISSION, CarSlot::TRANSMISSION, "The gearbox's drain plug" },
		{ FluidCircuit::DIFFERENTIAL, CarSlot::DIFFERENTIAL, "The differential's drain plug" },
		{ FluidCircuit::FUEL, CarSlot::FUEL_TANK, "The fuel tank's drain plug" } })
		if (auto at = bottomOf(car.Get(slot, 0)))
			drains.push_back({ circuit, *at, where });
	for (int p = 0; p < BLEED_POINTS; p++)
	{
		const auto point = static_cast<BleedPoint>(p);
		int index = 0;
		const CarSlot slot = CarAssembly::BleedNipplePart(point, index);
		if (car.IsFilled(slot, index))
			drains.push_back({ point == BleedPoint::CLUTCH ? FluidCircuit::CLUTCH : FluidCircuit::BRAKES, CarSlotCenter(slot, index),
				std::format("The {}'s bleed nipple", BleedPointLower(point)) });
	}
	return drains;
}

double ShopWorld::DevDrain(Workshop& workshop, FluidCircuit circuit)
{
	for (const auto& drain : CarDrainPoints(workshop))
		if (drain.circuit == circuit)
			return DrainInto(workshop, circuit, drain.at);
	return 0.0;
}

bool ShopWorld::DevPanUnder(const Workshop& workshop, FluidCircuit circuit)
{
	for (const auto& drain : CarDrainPoints(workshop))
		if (drain.circuit == circuit)
			for (size_t s = m_fixedStations; s < m_stations.size(); s++)
				if (IsDrainPan(m_stations[s]))
				{
					// (Out of its crate, if it's still in it.)
					for (auto& [id, package] : m_packages)
						if (std::find(package.stations.begin(), package.stations.end(), s) != package.stations.end())
						{
							OpenPackage(package);
							break;
						}
					auto& station = m_stations[s];
					station.present = true;
					station.object->SetVisible(true);
					station.object->SetPickable(true);
					station.position = { drain.at.x, 0.f, drain.at.z };
					station.object->SetPosition(station.position);
					RebuildCollision();
					return true;
				}
	return false;
}
