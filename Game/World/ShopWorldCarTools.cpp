// The project car's tools in hand: pouring at the fillers, bleeding at the nipples, springs onto struts and tires onto
// wheels, and the gauges and strobe that set the car up.
#include "ShopWorld.h"

#include <algorithm>
#include <cmath>

#include "LV/Engine/Utility/InputManager.h"
#include "../Controls.h"
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
			case FluidCircuit::COOLANT:			return { 1.98f, 0.66f, 0.2f };
			case FluidCircuit::BRAKES:			return { 1.15f, 0.73f, -0.42f };
			case FluidCircuit::CLUTCH:			return { 0.98f, 0.78f, -0.6f };
			case FluidCircuit::TRANSMISSION:	return { 0.55f, 0.22f, 0.17f };
			case FluidCircuit::DIFFERENTIAL:	return { -1.05f, 0.15f, 0.1f };
			case FluidCircuit::FUEL:			return { -1.5f, 0.75f, 0.81f };
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

	// The bleed nipple for each point, and the part it's on.
	CarSlot BleedPart(BleedPoint point, int& index)
	{
		switch (point)
		{
			case BleedPoint::REAR_RIGHT:	index = 1; return CarSlot::WHEEL_CYLINDER;
			case BleedPoint::REAR_LEFT:		index = 0; return CarSlot::WHEEL_CYLINDER;
			case BleedPoint::FRONT_RIGHT:	index = 1; return CarSlot::BRAKE_CALIPER;
			case BleedPoint::FRONT_LEFT:	index = 0; return CarSlot::BRAKE_CALIPER;
			default:						index = 0; return CarSlot::CLUTCH_RELEASE_CYLINDER;
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
	if (kind < HandToolKind::FUNNEL)
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
	if (!build || m_carStation >= m_stations.size() || !m_stations[m_carStation].present)
		return false;
	const auto& car = build->assembly;
	const auto& spec = car.GetSpec();

	if (kind == HandToolKind::FUNNEL)
	{
		std::optional<FluidCircuit> aimed;
		float best = 0.f;
		for (int c = 0; c < FLUID_CIRCUITS; c++)
		{
			glm::vec3 point = CarToWorld(FillPoint(static_cast<FluidCircuit>(c)));
			m_fitPoints.push_back({ point, !car.CircuitOpen(static_cast<FluidCircuit>(c)) });
			if (auto off = offAim(point); off && (!aimed || *off < best))
			{
				aimed = static_cast<FluidCircuit>(c);
				best = *off;
			}
		}
		if (!aimed)
			return false;
		auto circuit = *aimed;
		glm::vec3 point = CarToWorld(FillPoint(circuit));
		m_spannerTarget = point;
		const auto& fs = spec.Fluid(circuit);
		int& picked = m_funnelProduct[static_cast<size_t>(circuit)];
		if (picked < 0)
			picked = static_cast<int>(fs.product);
		// R: the next fluid there's some of.
		if (Controls::Pressed(input, Control::ROTATE))
			for (int step = 1; step <= FLUID_PRODUCTS; step++)
			{
				int next = (picked + step) % FLUID_PRODUCTS;
				if (workshop.FluidStock(static_cast<FluidProduct>(next)) > 0.0)
				{
					picked = next;
					break;
				}
			}
		auto product = static_cast<FluidProduct>(picked);
		double level = car.GetFluid(circuit).Total();
		auto open = car.CircuitOpen(circuit);
		m_prompt = std::format("{}: {:.2f} l (full at {:.2f})   [LMB] Hold to pour {} ({:.1f} l in stock)   [R] Another fluid   [RMB] Hold to drain",
			FluidCircuitName(circuit), level, fs.level.max, FluidProductName(product), workshop.FluidStock(product));
		m_secondaryPrompt = open ? std::format("Open: the {} isn't on, so it would run straight out", *open) : std::string(fs.check);
		if (leftPressed && open)
			Toast(std::format("The {} isn't on: it would run straight out onto the floor.", *open));
		else if (leftPressed && workshop.FluidStock(product) <= 0.0)
			Toast(std::format("There's no {} in stock: buy some at the parts computer.", FluidProductName(product)));
		if (leftHeld && !open)
		{
			double poured = workshop.PourIntoCar(circuit, product, PourRate(circuit) * deltaTime, true);
			if (poured <= 0.0 && leftPressed && level >= fs.level.max * 0.99)
				Toast(std::format("The {} is full.", FluidCircuitName(circuit)));
		}
		// (Held on a circuit, numbered past any part's id.)
		float held = holdOn((uint64_t(1) << 40) + static_cast<uint64_t>(circuit), rightHeld && level > 0.0);
		m_pressAimed = held > 0.f;
		m_pressProgress = held / HOLD_SECONDS;
		if (held >= HOLD_SECONDS)
		{
			m_carHold = 0.f;
			m_carHoldOn = 0;
			workshop.DrainCar(circuit);
			Toast(workshop.GetLog().empty() ? "" : workshop.GetLog().back());
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
			CarSlot slot = BleedPart(static_cast<BleedPoint>(p), index);
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
		CarSlot slot = BleedPart(*aimed, index);
		m_spannerTarget = CarSlotCenter(slot, index);
		auto circuit = *aimed == BleedPoint::CLUTCH ? FluidCircuit::CLUTCH : FluidCircuit::BRAKES;
		const auto& fs = spec.Fluid(circuit);
		m_prompt = std::format("{}   [LMB] Bleed it (the reservoir has {:.2f} l: MIN {:.2f})", BleedPointName(*aimed),
			car.GetFluid(circuit).Total(), fs.level.min);
		m_secondaryPrompt = "Farthest from the master cylinder first: rear right, rear left, front right, front left";
		if (leftPressed && m_carToolPause <= 0.f)
		{
			m_carToolPause = CLICK_PAUSE;
			workshop.BleedCar(*aimed);
			Cue(Sound::WRENCH, m_spannerTarget.value_or(glm::vec3(0.f)), 0.6f);
			// What was seen in the tube, and anything about the reservoir.
			const auto& log = workshop.GetLog();
			std::string seen = log.empty() ? "" : log.back();
			if (log.size() >= 2 && seen.starts_with("The reservoir"))
				seen = log[log.size() - 2] + " " + seen;
			Toast(seen);
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
			std::string reading = feelers ? std::format("the feelers say {:.2f} mm", std::round(lash * 100.0) / 100.0) : "no feeler gauge to read the gap";
			m_prompt = std::format("{} rocker {}: {} (spec {:.2f} to {:.2f}, hot)   [LMB] In (tighter)   [RMB] Out (looser)", exhaust ? "Exhaust" : "Intake",
				aimed->index / 2 + 1, reading, range.min, range.max);
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
