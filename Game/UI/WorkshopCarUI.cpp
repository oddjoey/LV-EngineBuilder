// The project car's screens: building it up, its fluids, its tune, the road test, and its page of the parts computer.
#include "WorkshopUI.h"

#include "ModelPreview.h"
#include "../Simulation/CarSpec.h"
#include "../World/ShopAssets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

namespace
{
	const ImVec4 GOOD = { 0.45f, 0.85f, 0.45f, 1.f };
	const ImVec4 BAD = { 0.95f, 0.40f, 0.35f, 1.f };
	const ImVec4 WARN = { 0.95f, 0.75f, 0.30f, 1.f };
	const ImVec4 DIM = { 0.6f, 0.6f, 0.6f, 1.f };

	const ImVec4& SeverityColor(FindingSeverity severity)
	{
		return severity == FindingSeverity::FAILURE ? BAD : severity == FindingSeverity::WARNING ? WARN : DIM;
	}

	// The car's systems, in the order the build page lists them.
	constexpr int CAR_SYSTEMS = static_cast<int>(PartSystem::COUNT) - static_cast<int>(FIRST_CAR_SYSTEM);
	PartSystem CarSystem(int index) { return static_cast<PartSystem>(static_cast<int>(FIRST_CAR_SYSTEM) + index); }

	// Which system a car slot is listed under: a part's own, and the engine with the drivetrain.
	PartSystem SystemOf(CarSlot slot)
	{
		if (slot == CarSlot::ENGINE)
			return PartSystem::DRIVETRAIN;
		return PartCatalog::Get(CarAssembly::SlotPartType(slot)).system;
	}

	std::string Tightness(const Part& part)
	{
		switch (part.GetTightness())
		{
			case FastenerTightness::LOOSE:		return "finger tight";
			case FastenerTightness::SNUG:		return "snug";
			case FastenerTightness::TORQUED:	return "torqued";
		}
		return "";
	}
}

void WorkshopUI::DrawCarTab(Workshop& workshop)
{
	if (!workshop.OwnsCar())
	{
		ImGui::TextWrapped("There's no project car in the salvage yard. Buy one at the parts computer, under Project Car.");
		DrawCarRoadTest(workshop);	// the last sale, if there's been one
		return;
	}
	if (ImGui::BeginTabBar("CarPages"))
	{
		// A page asked for (OpenCarPage) is turned to once.
		auto flags = [&](int page) { return m_carPageRequest == page ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
		if (ImGui::BeginTabItem("Build", nullptr, flags(0)))
		{
			DrawCarBuild(workshop);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Fluids", nullptr, flags(1)))
		{
			DrawCarFluids(workshop);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Tune", nullptr, flags(2)))
		{
			DrawCarTune(workshop);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Road Test", nullptr, flags(3)))
		{
			DrawCarRoadTest(workshop);
			ImGui::EndTabItem();
		}
		m_carPageRequest = -1;
		ImGui::EndTabBar();
	}
}

void WorkshopUI::OpenCarPage(int page)
{
	Open(WorkshopTab::CAR);
	m_carPageRequest = page;
}

void WorkshopUI::DrawCarBuild(Workshop& workshop)
{
	const auto& car = workshop.GetCar()->assembly;
	m_carSystem = std::clamp(m_carSystem, 0, CAR_SYSTEMS - 1);

	// The systems down the left, each with how much of it is on.
	if (ImGui::BeginChild("CarSystems", { 230.f, 0.f }, ImGuiChildFlags_Borders))
	{
		for (int s = 0; s < CAR_SYSTEMS; s++)
		{
			int filled = 0, total = 0;
			for (int slot = 0; slot < static_cast<int>(CarSlot::COUNT); slot++)
			{
				auto carSlot = static_cast<CarSlot>(slot);
				PartSystem system = CarAssembly::IsFastenerSlot(carSlot) ? SystemOf(CarAssembly::SlotFor(CarAssembly::Holes(carSlot)->parent)) : SystemOf(carSlot);
				if (system != CarSystem(s))
					continue;
				for (int index = 0; index < car.SlotCount(carSlot); index++, total++)
					filled += car.IsFilled(carSlot, index) ? 1 : 0;
			}
			std::string label = std::format("{}  {}/{}###system{}", PartSystemName(CarSystem(s)), filled, total, s);
			ImGui::PushStyleColor(ImGuiCol_Text, filled == total ? GOOD : ImGui::GetStyleColorVec4(ImGuiCol_Text));
			if (ImGui::Selectable(label.c_str(), m_carSystem == s))
				m_carSystem = s;
			ImGui::PopStyleColor();
		}
		ImGui::Separator();
		auto missing = car.MissingParts();
		if (missing.empty())
			ImGui::TextColored(GOOD, "Every part is on.");
		else
			ImGui::TextDisabled("%zu positions still empty.", missing.size());
		if (ImGui::Button("Dev: build it all", { -1.f, 0.f }))
			workshop.DevAssembleCar();
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (!ImGui::BeginChild("CarSlots", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}
	PartSystem system = CarSystem(m_carSystem);
	ImGui::TextDisabled("Parts go on in the factory's order: whatever a part bolts to, and that part's own nuts and bolts, first. "
		"Nuts and bolts go in finger tight; do them up with the best tool the shop has.");

	// Loose parts of a type the shop has: off every engine and the car, and not on a strut or wheel.
	auto looseOf = [&](PartType type) {
		std::vector<pPart> loose;
		for (const auto& part : workshop.GetInventory())
			if (part->GetType() == type && workshop.EngineOf(*part) < 0 && !workshop.IsOnCar(*part) && !workshop.CarHostOf(*part))
				loose.push_back(part);
		return loose;
	};

	// A part's nuts and bolts, a row for each set of them under it: how many are in and how tight.
	auto drawBolts = [&](CarSlot slot, int index) {
		for (int h = static_cast<int>(CarSlot::STRUT_MOUNT_NUT); h < static_cast<int>(CarSlot::COUNT); h++)
		{
			auto holeSlot = static_cast<CarSlot>(h);
			const auto* holes = CarAssembly::Holes(holeSlot);
			if (holes->parent != CarAssembly::SlotPartType(slot))
				continue;
			int first = index * holes->perParent, count = holes->perParent;
			int in = 0, loose = 0, snug = 0, torqued = 0;
			for (int i = first; i < first + count; i++)
				if (auto bolt = car.Get(holeSlot, i))
				{
					in++;
					(bolt->GetTightness() == FastenerTightness::LOOSE ? loose : bolt->GetTightness() == FastenerTightness::SNUG ? snug : torqued)++;
				}
			ImGui::PushID(h * 1000 + index);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("   %s x%d, %.0f Nm", holes->name, count, holes->torqueNm);
			ImGui::TableNextColumn();
			ImGui::TextColored(in < count ? DIM : torqued == count ? GOOD : WARN, "%d in: %d finger tight, %d snug, %d torqued", in, loose, snug, torqued);
			ImGui::TableNextColumn();
			auto looseBolts = looseOf(holes->size);
			ImGui::BeginDisabled(in == count || looseBolts.empty());
			if (ImGui::SmallButton("Fit all"))
			{
				size_t next = 0;
				for (int i = first; i < first + count && next < looseBolts.size(); i++)
					if (!car.Get(holeSlot, i) && workshop.InstallCarPart(holeSlot, i, looseBolts[next]))
						next++;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			auto tool = workshop.BestTurningTool(holes->size, holes->torqueNm, true);
			ImGui::BeginDisabled(in == 0 || !tool);
			if (ImGui::SmallButton("Do up"))
				for (int i = first; i < first + count; i++)
					if (car.Get(holeSlot, i))
						workshop.TightenCarFastener(holeSlot, i, tool->first, tool->second);
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("%s", tool ? std::format("With the {} ({} mm).", ToolCatalog::Of(tool->first).name, tool->second).c_str()
					: std::format("The shop has nothing to turn a {} mm {}.", PartCatalog::Get(holes->size).fastener.headMm,
						PartCatalog::Get(holes->size).fastener.nut ? "nut" : "bolt").c_str());
			ImGui::SameLine();
			auto undo = workshop.BestTurningTool(holes->size, holes->torqueNm, false);
			ImGui::BeginDisabled(snug + torqued == 0 || !undo);
			if (ImGui::SmallButton("Undo"))
				for (int i = first; i < first + count; i++)
					if (auto bolt = car.Get(holeSlot, i); bolt && bolt->GetTightness() != FastenerTightness::LOOSE)
						workshop.LoosenCarFastener(holeSlot, i, undo->first, undo->second);
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(in == 0 || loose < in);
			if (ImGui::SmallButton("Take out"))
				for (int i = first; i < first + count; i++)
					if (car.Get(holeSlot, i))
						workshop.RemoveCarPart(holeSlot, i);
			ImGui::EndDisabled();
			ImGui::PopID();
		}
	};

	if (ImGui::BeginTable("Positions", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch, 1.2f);
		ImGui::TableSetupColumn("On it", ImGuiTableColumnFlags_WidthStretch, 1.4f);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.6f);
		ImGui::TableHeadersRow();

		for (int s = 0; s < CAR_PART_SLOTS + 1; s++)
		{
			auto slot = static_cast<CarSlot>(s);
			if (SystemOf(slot) != system || CarAssembly::CarriedBy(slot))
				continue;
			for (int index = 0; index < car.SlotCount(slot); index++)
			{
				ImGui::PushID(s * 100 + index);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(car.PositionName(slot, index).c_str());

				// The engine: lowered in and lifted out with the hoist, out in the salvage yard.
				if (slot == CarSlot::ENGINE)
				{
					ImGui::TableNextColumn();
					int engine = workshop.GetEngineInCar();
					if (engine >= 0)
						ImGui::Text("Engine %d%s", engine + 1, workshop.GetAssembly(engine).IsComplete() ? "" : " (unfinished)");
					else
						ImGui::TextDisabled("Empty bay");
					ImGui::TableNextColumn();
					if (auto missing = car.MissingPrerequisite(slot, index); missing && engine < 0)
						ImGui::TextDisabled("The %s first.", missing->c_str());
					else
						ImGui::TextDisabled(engine >= 0 ? "Lift it out with the engine hoist." : "Lower one in with the engine hoist.");
					ImGui::PopID();
					if (engine >= 0)
						drawBolts(slot, index);
					continue;
				}

				auto part = car.Get(slot, index);
				ImGui::TableNextColumn();
				if (part)
				{
					ImGui::TextColored(part->GetCondition() == PartCondition::DAMAGED ? BAD : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", part->GetName().c_str());
					if (part->GetCondition() == PartCondition::DAMAGED && ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", part->GetDamageReason().c_str());
					if (auto carried = slot == CarSlot::FRONT_STRUT ? car.Get(CarSlot::FRONT_COIL_SPRING, index) : slot == CarSlot::WHEEL ? car.Get(CarSlot::TIRE, index) : nullptr)
						ImGui::TextDisabled("with %s", carried->GetName().c_str());
				}
				else
					ImGui::TextDisabled("-");

				ImGui::TableNextColumn();
				if (part)
				{
					auto blocked = car.RemovalBlockedBy(slot, index);
					if (slot == CarSlot::BODY_SHELL)
						ImGui::TextDisabled("The car itself.");
					else
					{
						ImGui::BeginDisabled(blocked.has_value());
						if (ImGui::SmallButton("Take off"))
							workshop.RemoveCarPart(slot, index);
						ImGui::EndDisabled();
						if (blocked)
						{
							ImGui::SameLine();
							ImGui::TextDisabled("(%s first)", blocked->c_str());
						}
					}
				}
				else
				{
					auto type = CarAssembly::SlotPartType(slot);
					auto loose = looseOf(type);
					// A strut or wheel only goes on with its spring or tire on it.
					if (slot == CarSlot::FRONT_STRUT || slot == CarSlot::WHEEL)
						std::erase_if(loose, [&](const pPart& candidate) { return !workshop.CarriedOn(*candidate); });
					std::string key = car.PositionName(slot, index);
					uint32_t& pick = m_carPick[key];
					if (std::none_of(loose.begin(), loose.end(), [&](const pPart& candidate) { return candidate->GetId() == pick; }))
						pick = loose.empty() ? 0 : loose.front()->GetId();
					auto missing = car.MissingPrerequisite(slot, index);
					if (missing && !(slot == CarSlot::FRONT_STRUT || slot == CarSlot::WHEEL))
						ImGui::TextDisabled("The %s first.", missing->c_str());
					else if (loose.empty())
						ImGui::TextDisabled(slot == CarSlot::FRONT_STRUT ? "No strut with a spring on it."
							: slot == CarSlot::WHEEL ? "No wheel with a tire on it." : "None in the shop.");
					else
					{
						ImGui::SetNextItemWidth(220.f);
						pPart picked = workshop.FindPart(pick);
						if (ImGui::BeginCombo("##pick", picked ? picked->GetName().c_str() : ""))
						{
							for (const auto& candidate : loose)
							{
								std::string name = std::format("{}{}###{}", candidate->GetName(), candidate->GetCondition() == PartCondition::DAMAGED
									? " (damaged)" : "", candidate->GetId());
								if (ImGui::Selectable(name.c_str(), candidate->GetId() == pick))
									pick = candidate->GetId();
							}
							ImGui::EndCombo();
						}
						ImGui::SameLine();
						if (ImGui::SmallButton("Fit") && picked)
							workshop.InstallCarPart(slot, index, picked);
					}
				}
				ImGui::PopID();
				if (part)
					drawBolts(slot, index);
			}
		}
		ImGui::EndTable();
	}

	// Springs onto struts and tires onto wheels, off the car.
	if (system == PartSystem::FRONT_SUSPENSION || system == PartSystem::WHEELS)
	{
		bool struts = system == PartSystem::FRONT_SUSPENSION;
		auto hostType = struts ? PartType::FRONT_STRUT : PartType::WHEEL;
		auto riderType = struts ? PartType::FRONT_COIL_SPRING : PartType::TIRE;
		ImGui::SeparatorText(struts ? "Springs on struts (off the car, in the spring compressor)" : "Tires on wheels (off the car, on the tire machine)");
		auto riders = looseOf(riderType);
		for (const auto& host : looseOf(hostType))
		{
			ImGui::PushID(static_cast<int>(host->GetId()));
			ImGui::TextUnformatted(host->GetName().c_str());
			ImGui::SameLine(260.f);
			if (auto rider = workshop.CarriedOn(*host))
			{
				ImGui::Text("with %s", rider->GetName().c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Take off"))
					workshop.TakeOffCarPart(host);
			}
			else if (riders.empty())
				ImGui::TextDisabled("No loose %s.", PartTypeName(riderType));
			else
			{
				uint32_t& pick = m_carPick[std::format("rider{}", host->GetId())];
				if (std::none_of(riders.begin(), riders.end(), [&](const pPart& candidate) { return candidate->GetId() == pick; }))
					pick = riders.front()->GetId();
				pPart picked = workshop.FindPart(pick);
				ImGui::SetNextItemWidth(220.f);
				if (ImGui::BeginCombo("##rider", picked ? picked->GetName().c_str() : ""))
				{
					for (const auto& candidate : riders)
						if (ImGui::Selectable(std::format("{}###{}", candidate->GetName(), candidate->GetId()).c_str(), candidate->GetId() == pick))
							pick = candidate->GetId();
					ImGui::EndCombo();
				}
				ImGui::SameLine();
				if (ImGui::SmallButton(struts ? "Compress onto it" : "Mount it") && picked)
					workshop.FitOntoCarPart(host, picked);
			}
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawCarFluids(Workshop& workshop)
{
	const auto& car = workshop.GetCar()->assembly;
	const auto& spec = car.GetSpec();

	if (ImGui::BeginTable("Fluids", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Circuit", ImGuiTableColumnFlags_WidthStretch, 0.9f);
		ImGui::TableSetupColumn("In it", ImGuiTableColumnFlags_WidthStretch, 1.4f);
		ImGui::TableSetupColumn("The manual", ImGuiTableColumnFlags_WidthStretch, 1.4f);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.6f);
		ImGui::TableHeadersRow();
		for (int c = 0; c < FLUID_CIRCUITS; c++)
		{
			auto circuit = static_cast<FluidCircuit>(c);
			const auto& fs = spec.Fluid(circuit);
			const auto& fluid = car.GetFluid(circuit);
			ImGui::PushID(c);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(FluidCircuitName(circuit));
			ImGui::TableNextColumn();
			double litres = fluid.Total();
			ImGui::TextColored(fs.level.Contains(litres) ? GOOD : litres <= 0.0 ? DIM : WARN, "%.2f l", litres);
			if (auto main = fluid.Main(); main != FluidProduct::COUNT)
			{
				auto fit = FluidFitFor(circuit, main);
				ImGui::TextColored(fit == FluidFit::RIGHT || fit == FluidFit::ACCEPTABLE ? DIM : BAD, "%s%s", FluidProductName(main),
					fluid.litres.size() > 1 ? ", mixed" : "");
			}
			if (circuit == FluidCircuit::COOLANT && fluid.airPocket > 0.05 && litres > 0.0)
				ImGui::TextColored(WARN, "Air in it: burp it");
			if (auto open = car.CircuitOpen(circuit))
				ImGui::TextColored(DIM, "Open: no %s", open->c_str());
			ImGui::TableNextColumn();
			ImGui::TextWrapped("%.2f to %.2f l of %s", fs.level.min, fs.level.max, fs.grade);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", fs.check);
			ImGui::TableNextColumn();
			// What to pour: the manual's by default.
			int& product = m_pourProduct[c];
			if (product <= 0 || product > FLUID_PRODUCTS)
				product = static_cast<int>(fs.product) + 1;
			ImGui::SetNextItemWidth(190.f);
			auto chosen = static_cast<FluidProduct>(product - 1);
			if (ImGui::BeginCombo("##product", std::format("{} ({:.1f} l)", FluidProductName(chosen), workshop.FluidStock(chosen)).c_str()))
			{
				for (int p = 0; p < FLUID_PRODUCTS; p++)
				{
					auto option = static_cast<FluidProduct>(p);
					if (ImGui::Selectable(std::format("{} ({:.1f} l in stock)", FluidProductName(option), workshop.FluidStock(option)).c_str(), product == p + 1))
						product = p + 1;
				}
				ImGui::EndCombo();
			}
			float& amount = m_pourLitres[c];
			if (amount <= 0.f)
				amount = static_cast<float>((std::max)(0.05, fs.level.max - litres));
			ImGui::SetNextItemWidth(90.f);
			ImGui::InputFloat("l##amount", &amount, 0.05f, 0.5f, "%.2f");
			amount = std::clamp(amount, 0.f, 60.f);
			ImGui::SameLine();
			if (ImGui::SmallButton("Pour"))
			{
				workshop.PourIntoCar(circuit, chosen, amount);
				amount = 0.f;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(litres <= 0.0);
			if (ImGui::SmallButton("Drain"))
				workshop.DrainCar(circuit);
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Bleeding");
	ImGui::TextDisabled("The brakes from the wheel farthest from the master cylinder in: rear right, rear left, front right, front left. Watch the "
		"tube on the nipple, and keep the reservoir topped up: let it run dry and it draws air.");
	for (int p = 0; p < BLEED_POINTS; p++)
	{
		if (p > 0)
			ImGui::SameLine();
		if (ImGui::Button(BleedPointName(static_cast<BleedPoint>(p))))
			workshop.BleedCar(static_cast<BleedPoint>(p));
	}
	ImGui::SeparatorText("Cooling system");
	if (ImGui::Button("Run it with the radiator cap off"))
		workshop.BurpCarCoolant();
	ImGui::SameLine();
	ImGui::TextDisabled("Lets the air out of the jackets once it's warm: top it up after.");
}

void WorkshopUI::DrawCarTune(Workshop& workshop)
{
	const auto& car = workshop.GetCar()->assembly;
	const auto& ts = car.GetSpec().tune;
	const CarTune& now = car.GetTune();
	if (!m_tuneDraft)
		m_tuneDraft = now;
	auto& tune = *m_tuneDraft;

	// What's out of the book's range in the draft, and what differs from the car.
	struct Row
	{
		const char* label;
		double* value;
		double current;
		double step;
		const char* format;
		const Range* range;
		double nominal;
		const char* how;
	};
	std::vector<Row> engine = {
		{ "Ignition timing, deg BTDC", &tune.ignitionTiming, now.ignitionTiming, 0.5, "%.1f", &ts.ignitionTiming, ts.ignitionTimingNominal, "Distributor; read it with the timing light" },
		{ "Idle speed, rpm", &tune.idleSpeed, now.idleSpeed, 25.0, "%.0f", &ts.idleSpeed, ts.idleSpeedNominal, "Idle speed screw, warm" },
		{ "Idle mixture, turns out", &tune.idleMixture, now.idleMixture, 0.125, "%.3f", &ts.idleMixture, ts.idleMixtureNominal, "Mixture screw, from lightly seated" },
		{ "Primary main jet", &tune.mainJet, now.mainJet, 1.0, "%.0f", &ts.mainJet, ts.mainJetNominal, "Swapped with the carburettor's top off" },
		{ "Intake valve lash, mm", &tune.intakeLash, now.intakeLash, 0.01, "%.2f", &ts.intakeLash, ts.intakeLashNominal, "Hot; screwdriver and feelers, cover off" },
		{ "Exhaust valve lash, mm", &tune.exhaustLash, now.exhaustLash, 0.01, "%.2f", &ts.exhaustLash, ts.exhaustLashNominal, "Hot; screwdriver and feelers, cover off" },
	};
	std::vector<Row> chassis = {
		{ "Caster, deg", &tune.caster, now.caster, 0.25, "%.2f", &ts.caster, ts.casterNominal, "The strut rods' nuts" },
		{ "Front toe, mm in", &tune.frontToe, now.frontToe, 0.5, "%.1f", &ts.frontToe, ts.frontToeNominal, "Tie rod ends; the camber and toe gauge" },
	};
	std::vector<std::string> tireLabels;
	for (int corner = 0; corner < 4; corner++)
		tireLabels.push_back(std::format("{} tire, kPa", CornerName(static_cast<Corner>(corner))));
	for (int corner = 0; corner < 4; corner++)
		chassis.push_back({ tireLabels[corner].c_str(), &tune.tirePressure[corner], now.tirePressure[corner], 5.0, "%.0f", &ts.tirePressure,
			ts.tirePressureNominal, "Cold; the tire gauge and inflator" });

	int outOfSpec = 0, changed = 0;
	for (const auto* rows : { &engine, &chassis })
		for (const auto& row : *rows)
		{
			outOfSpec += !row.range->Contains(*row.value);
			changed += std::abs(*row.value - row.current) > 1e-9;
		}
	bool camberOut = !ts.frontCamber.Contains(tune.frontCamber);

	ImGui::TextWrapped("How the car is set up. Every setting can also be made at the car with its tool, reading it as you go; here it's "
		"set from the book's figures. Nothing changes on the car till it's applied, and only a drive shows what it did.");
	if (outOfSpec + camberOut == 0)
		ImGui::TextColored(GOOD, "Everything is inside the factory's range.");
	else
		ImGui::TextColored(WARN, "%d setting%s outside the factory's range.", outOfSpec + camberOut, outOfSpec + camberOut == 1 ? "" : "s");
	if (changed > 0)
	{
		ImGui::SameLine();
		ImGui::TextColored(WARN, "  %d change%s not applied yet.", changed, changed == 1 ? "" : "s");
	}

	ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
	if (ImGui::BeginTable("TuneTable", 5, flags))
	{
		ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 2.2f);
		ImGui::TableSetupColumn("On the car", ImGuiTableColumnFlags_WidthStretch, 1.f);
		ImGui::TableSetupColumn("Set to", ImGuiTableColumnFlags_WidthStretch, 1.6f);
		ImGui::TableSetupColumn("Factory range", ImGuiTableColumnFlags_WidthStretch, 1.6f);
		ImGui::TableSetupColumn("How it's set", ImGuiTableColumnFlags_WidthStretch, 3.f);
		ImGui::TableHeadersRow();
		auto section = [&](const char* title) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::SeparatorText(title);
		};
		auto draw = [&](Row& row, int id) {
			ImGui::TableNextRow();
			bool differs = std::abs(*row.value - row.current) > 1e-9;
			if (differs)
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.95f, 0.75f, 0.3f, 0.12f)));
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(row.label);
			ImGui::TableSetColumnIndex(1);
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(row.range->Contains(row.current) ? ImVec4(1.f, 1.f, 1.f, 0.85f) : WARN, row.format, row.current);
			ImGui::TableSetColumnIndex(2);
			ImGui::PushID(id);
			float shown = static_cast<float>(*row.value);
			ImGui::SetNextItemWidth(-1.f);
			if (ImGui::InputFloat("##set", &shown, static_cast<float>(row.step), static_cast<float>(row.step * 5.0), row.format))
				*row.value = shown;
			ImGui::PopID();
			ImGui::TableSetColumnIndex(3);
			ImGui::AlignTextToFramePadding();
			std::string fmt = std::string(row.format) + " to " + row.format;
			char range[64];
			std::snprintf(range, sizeof(range), fmt.c_str(), row.range->min, row.range->max);
			ImGui::TextColored(row.range->Contains(*row.value) ? GOOD : WARN, "%s", range);
			ImGui::TableSetColumnIndex(4);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", row.how);
		};
		section("Engine");
		int id = 0;
		for (auto& row : engine)
			draw(row, id++);
		section("Chassis");
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Front camber, deg");
			ImGui::TableSetColumnIndex(1);
			ImGui::TextColored(camberOut ? WARN : ImVec4(1.f, 1.f, 1.f, 0.85f), "%.2f", now.frontCamber);
			ImGui::TableSetColumnIndex(2);
			ImGui::TextDisabled("not adjustable");
			ImGui::TableSetColumnIndex(3);
			ImGui::TextColored(camberOut ? WARN : GOOD, "%.2f to %.2f", ts.frontCamber.min, ts.frontCamber.max);
			ImGui::TableSetColumnIndex(4);
			ImGui::TextDisabled("Set by the parts: out of range means a bent knuckle or strut");
		}
		for (auto& row : chassis)
			draw(row, id++);
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Final drive");
			ImGui::TableSetColumnIndex(1);
			if (now.finalDrive > 0.0)
				ImGui::Text("%.3f : 1", now.finalDrive);
			else
				ImGui::TextDisabled("no diff");
			ImGui::TableSetColumnIndex(2);
			ImGui::TextDisabled("fit another diff");
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%.3f factory", car.GetSpec().finalDriveNominal);
			ImGui::TableSetColumnIndex(4);
			ImGui::TextDisabled("Shorter (higher) pulls harder, tops out sooner");
		}
		ImGui::EndTable();
	}

	ImGui::Spacing();
	ImGui::BeginDisabled(changed == 0);
	if (ImGui::Button(changed > 0 ? std::format("Apply {} change{}", changed, changed == 1 ? "" : "s").c_str() : "Nothing to apply", { 200.f, 0.f }))
	{
		workshop.SetCarTune(tune);
		m_tuneDraft = workshop.GetCar()->assembly.GetTune();
	}
	ImGui::SameLine();
	if (ImGui::Button("Undo changes"))
		m_tuneDraft = now;
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Factory settings"))
	{
		auto factory = CarTune::Factory(car.GetSpec());
		factory.frontCamber = tune.frontCamber;
		factory.finalDrive = tune.finalDrive;
		m_tuneDraft = factory;
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Puts every setting back to the book's middle figure (not applied till you apply it).");
}

namespace
{
	// The run down the strip: speed against time, the gears shaded behind it, the engine's revs faintly over it,
	// and where it passed 100 km/h and the quarter mile. Hovering reads off a moment.
	void DrawRunGraph(const RoadTestResult& r, float width, float height)
	{
		ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##run", { width, height });
		bool hovered = ImGui::IsItemHovered();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImU32 frame = ImGui::GetColorU32(ImGuiCol_Border);
		const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
		const ImU32 faint = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const ImU32 speedInk = ImGui::GetColorU32(ImVec4(0.45f, 0.75f, 1.f, 1.f));
		const ImU32 rpmInk = ImGui::GetColorU32(ImVec4(0.95f, 0.6f, 0.3f, 0.55f));
		const ImU32 mark = ImGui::GetColorU32(WARN);
		float fontSize = ImGui::GetFontSize();
		ImVec2 min = { origin.x + fontSize * 2.6f, origin.y + fontSize * 0.5f }, max = { origin.x + width - fontSize * 2.6f, origin.y + height - fontSize * 1.6f };
		draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.f);
		if (r.trace.size() < 2)
		{
			const char* none = "Drive it again to see the run.";
			ImVec2 size = ImGui::CalcTextSize(none);
			draw->AddText({ (min.x + max.x - size.x) * 0.5f, (min.y + max.y - size.y) * 0.5f }, faint, none);
			return;
		}
		// The first half minute or so, past the quarter mile.
		float endTime = (std::min)(r.trace.back().time, (std::max)(20.f, static_cast<float>(r.quarterMile) * 1.4f));
		float topSpeed = 20.f, topRpm = 1000.f;
		for (const auto& s : r.trace)
			if (s.time <= endTime)
			{
				topSpeed = (std::max)(topSpeed, s.speed);
				topRpm = (std::max)(topRpm, s.rpm);
			}
		topSpeed = std::ceil(topSpeed / 20.f) * 20.f;
		topRpm = std::ceil(topRpm / 1000.f) * 1000.f;
		auto at = [&](float time, float value, float top) {
			return ImVec2(min.x + (max.x - min.x) * time / endTime, max.y - (max.y - min.y) * value / top);
		};
		// Gear bands.
		size_t start = 0;
		for (size_t i = 1; i <= r.trace.size(); i++)
			if (i == r.trace.size() || r.trace[i].gear != r.trace[start].gear || r.trace[i].time > endTime)
			{
				float t0 = r.trace[start].time, t1 = r.trace[(std::min)(i, r.trace.size() - 1)].time;
				t1 = (std::min)(t1, endTime);
				if (r.trace[start].gear % 2 == 1)
					draw->AddRectFilled({ at(t0, 0, 1).x, min.y }, { at(t1, 0, 1).x, max.y }, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.04f)));
				std::string gear = std::to_string(r.trace[start].gear + 1);
				if (at(t1, 0, 1).x - at(t0, 0, 1).x > fontSize)
					draw->AddText({ (at(t0, 0, 1).x + at(t1, 0, 1).x) * 0.5f - fontSize * 0.25f, min.y + 2.f }, faint, gear.c_str());
				if (i == r.trace.size() || r.trace[i].time > endTime)
					break;
				start = i;
			}
		// Grid and scales: speed on the left, revs on the right, seconds along the bottom.
		for (float v = 0.f; v <= topSpeed + 0.1f; v += 40.f)
		{
			ImVec2 p = at(0.f, v, topSpeed);
			draw->AddLine(p, { max.x, p.y }, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.06f)));
			std::string label = std::format("{:.0f}", v);
			draw->AddText({ min.x - ImGui::CalcTextSize(label.c_str()).x - 4.f, p.y - fontSize * 0.5f }, faint, label.c_str());
		}
		for (float v = 0.f; v <= topRpm + 0.1f; v += 2000.f)
		{
			std::string label = std::format("{:.0f}k", v / 1000.f);
			draw->AddText({ max.x + 4.f, at(0.f, v, topRpm).y - fontSize * 0.5f }, rpmInk, label.c_str());
		}
		for (float t = 0.f; t <= endTime + 0.01f; t += 5.f)
		{
			std::string label = std::format("{:.0f}", t);
			draw->AddText({ at(t, 0, 1).x - ImGui::CalcTextSize(label.c_str()).x * 0.5f, max.y + 2.f }, faint, label.c_str());
		}
		draw->AddText({ min.x, max.y + fontSize * 0.75f }, faint, "km/h against seconds; revs in orange");
		// 100 km/h and the quarter mile.
		if (topSpeed >= 100.f)
		{
			ImVec2 p = at(0.f, 100.f, topSpeed);
			for (float x = min.x; x < max.x; x += 8.f)
				draw->AddLine({ x, p.y }, { (std::min)(x + 4.f, max.x), p.y }, faint);
		}
		auto marker = [&](double time, const std::string& label) {
			if (time <= 0.0 || time > endTime)
				return;
			float x = at(static_cast<float>(time), 0, 1).x;
			draw->AddLine({ x, min.y }, { x, max.y }, mark, 1.f);
			draw->AddText({ x + 3.f, min.y + fontSize * 1.1f }, mark, label.c_str());
		};
		marker(r.zeroToHundred, std::format("100 km/h, {:.1f} s", r.zeroToHundred));
		marker(r.quarterMile, std::format("1/4 mile, {:.1f} s", r.quarterMile));
		// The revs, then the speed over them.
		std::vector<ImVec2> rpm, speed;
		for (const auto& s : r.trace)
			if (s.time <= endTime)
			{
				rpm.push_back(at(s.time, s.rpm, topRpm));
				speed.push_back(at(s.time, s.speed, topSpeed));
			}
		draw->AddPolyline(rpm.data(), static_cast<int>(rpm.size()), rpmInk, 0, 1.5f);
		draw->AddPolyline(speed.data(), static_cast<int>(speed.size()), speedInk, 0, 2.5f);
		draw->AddRect(min, max, frame, 4.f);
		// Reading off a moment.
		if (hovered)
		{
			float time = (ImGui::GetIO().MousePos.x - min.x) / (max.x - min.x) * endTime;
			const RoadTestSample* nearest = &r.trace.front();
			for (const auto& s : r.trace)
				if (std::abs(s.time - time) < std::abs(nearest->time - time))
					nearest = &s;
			float x = at(nearest->time, 0, 1).x;
			draw->AddLine({ x, min.y }, { x, max.y }, text, 1.f);
			draw->AddCircleFilled(at(nearest->time, nearest->speed, topSpeed), 4.f, speedInk);
			ImGui::SetTooltip("%.1f s: %.0f km/h, %.0f rpm in %s", nearest->time, nearest->speed, nearest->rpm,
				std::format("{}", nearest->gear + 1 == 1 ? "first" : nearest->gear + 1 == 2 ? "second" : nearest->gear + 1 == 3 ? "third"
					: nearest->gear + 1 == 4 ? "fourth" : "fifth").c_str());
		}
	}
}

void WorkshopUI::DrawCarRoadTest(Workshop& workshop)
{
	// One drive: what it did, against the drive before it if there was one.
	auto showResult = [&](const RoadTestResult& r, const RoadTestResult* before, const char* id) {
		ImGui::PushID(id);
		bool compare = before && before->ran && before->drives && r.drives;
		auto check = [&](const char* label, bool ok) {
			ImGui::TextColored(ok ? GOOD : BAD, "%s %s", ok ? "[x]" : "[ ]", label);
			ImGui::SameLine(0.f, 18.f);
		};
		check("Cranks", r.cranks);
		check("Starts", r.starts);
		check("Drives", r.drives);
		check("Steers", r.steers);
		check("Stops", r.stops);
		ImGui::NewLine();
		// Health, the buyer's figure, as a bar.
		float health = static_cast<float>(r.healthScore / 100.0);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, health > 0.85f ? GOOD : health > 0.6f ? WARN : BAD);
		ImGui::ProgressBar(std::clamp(health, 0.f, 1.f), { 320.f, 0.f }, std::format("{:.0f}% healthy", r.healthScore).c_str());
		ImGui::PopStyleColor();
		if (compare && std::abs(r.healthScore - before->healthScore) >= 0.5)
		{
			ImGui::SameLine();
			bool better = r.healthScore > before->healthScore;
			ImGui::TextColored(better ? GOOD : BAD, "%+.0f since the last drive", r.healthScore - before->healthScore);
		}

		if (r.drives && r.steers && r.stops)
		{
			// The time slip on the left, the run on the right.
			if (ImGui::BeginTable("RunLayout", 2, ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("slip", ImGuiTableColumnFlags_WidthFixed, 380.f);
				ImGui::TableSetupColumn("graph", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				if (ImGui::BeginTable("Slip", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
				{
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.6f);
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.4f);
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.f);
					// lowerBetter: +1 when less is better (times, distances), -1 when more is, 0 when neither.
					auto row = [&](const char* label, double value, double previous, const char* unit, int places, int lowerBetter) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(label);
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("%s", std::vformat(std::format("{{:.{}f}} {}", places, unit), std::make_format_args(value)).c_str());
						ImGui::TableSetColumnIndex(2);
						if (!compare || previous <= 0.0 || value <= 0.0)
							return;
						double change = value - previous;
						if (std::abs(change) < std::pow(10.0, -places) * 0.5)
						{
							ImGui::TextDisabled("same");
							return;
						}
						bool better = lowerBetter == 0 ? true : (change < 0.0) == (lowerBetter > 0);
						std::string shown = std::vformat(std::format("{{:+.{}f}}", places), std::make_format_args(change));
						ImGui::TextColored(lowerBetter == 0 ? DIM : better ? GOOD : BAD, "%s", shown.c_str());
					};
					const RoadTestResult none = {};
					const RoadTestResult& p = compare ? *before : none;
					row("0-100 km/h", r.zeroToHundred, p.zeroToHundred, "s", 1, 1);
					row("Quarter mile", r.quarterMile, p.quarterMile, "s", 1, 1);
					row("  trap speed", r.quarterMileSpeed, p.quarterMileSpeed, "km/h", 0, -1);
					row("Top speed", r.topSpeed, p.topSpeed, "km/h", 0, -1);
					row("100-0 km/h", r.brakingDistance, p.brakingDistance, "m", 1, 1);
					row("Skidpad", r.lateralGrip, p.lateralGrip, "g", 2, -1);
					row("Fuel at 90 km/h", r.fuelEconomy, p.fuelEconomy, "l/100 km", 1, 1);
					if (r.idleRpm > 0.0)
						row("Idle", r.idleRpm, p.idleRpm, "rpm", 0, 0);
					ImGui::EndTable();
				}
				if (compare)
					ImGui::TextDisabled("Changes are against the drive before.");
				ImGui::TableSetColumnIndex(1);
				DrawRunGraph(r, ImGui::GetContentRegionAvail().x, 230.f);
				ImGui::EndTable();
			}
		}
		else if (r.idleRpm > 0.0)
			ImGui::Text("It idles at %.0f rpm, but it never got going.", r.idleRpm);

		// What the driver noticed, the worst first.
		struct Group { FindingSeverity severity; const char* title; };
		for (const auto& group : { Group{ FindingSeverity::FAILURE, "Must be put right" }, Group{ FindingSeverity::WARNING, "Should be put right" },
				Group{ FindingSeverity::INFO, "Noticed" } })
		{
			int count = 0;
			for (const auto& finding : r.findings)
				count += finding.severity == group.severity;
			if (count == 0)
				continue;
			ImGui::PushStyleColor(ImGuiCol_Text, SeverityColor(group.severity));
			ImGui::SeparatorText(std::format("{} ({})", group.title, count).c_str());
			for (const auto& finding : r.findings)
				if (finding.severity == group.severity)
				{
					ImGui::Bullet();
					ImGui::TextWrapped("%s", finding.message.c_str());
				}
			ImGui::PopStyleColor();
		}
		if (r.findings.empty() && r.drives)
			ImGui::TextColored(GOOD, "The driver had nothing to say about it.");
		ImGui::PopID();
	};

	if (const auto* build = workshop.GetCar())
	{
		ImGui::TextWrapped("A drive round the block, down the strip, a stop from 100 km/h and a lap of the skidpad, with whatever engine is in "
			"it as it is. Nothing about how it goes is known till it's been driven.");
		if (ImGui::Button("Take it for a drive", { 220.f, 34.f }))
			workshop.RoadTestCar();
		ImGui::SameLine();
		ImGui::BeginDisabled(workshop.GetEngineInCar() < 0);
		if (ImGui::Button("Sell it", { 140.f, 34.f }))
			workshop.SellCar();
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("The buyer drives it, engine and all, and pays for what it does and how healthy it is.");
		if (workshop.GetCar() && build->lastRoadTest.ran)
		{
			ImGui::SeparatorText(build->roadTestCurrent ? "The last drive" : "The last drive (it's been worked on since)");
			showResult(build->lastRoadTest, &build->previousRoadTest, "last");
		}
	}
	const auto& sale = workshop.GetLastCarSale();
	if (sale.sold)
	{
		ImGui::SeparatorText(std::format("The last car sold: ${:.0f}, +{:.0f} reputation", sale.price, sale.reputation).c_str());
		showResult(sale.test, nullptr, "sale");
	}
}

namespace
{
	// What a fluid comes in: a jerry can for fuel, a jug for the rest, each tinted the colour of what's in it.
	const char* FluidModel(FluidProduct product)
	{
		return product == FluidProduct::PETROL_87 || product == FluidProduct::PETROL_91 || product == FluidProduct::DIESEL ? "Fuel Can" : "Fluid Jug";
	}

	ImVec4 FluidTint(FluidProduct product)
	{
		switch (product)
		{
			case FluidProduct::OIL_5W30:
			case FluidProduct::OIL_10W30:
			case FluidProduct::OIL_20W50:			return { 0.95f, 0.75f, 0.25f, 1.f };
			case FluidProduct::COOLANT_50_50:		return { 0.45f, 0.9f, 0.45f, 1.f };
			case FluidProduct::WATER:				return { 0.55f, 0.75f, 1.f, 1.f };
			case FluidProduct::DOT3:
			case FluidProduct::DOT4:				return { 1.f, 0.95f, 0.75f, 1.f };
			case FluidProduct::DOT5_SILICONE:		return { 0.75f, 0.55f, 0.95f, 1.f };
			case FluidProduct::GEAR_OIL_75W90_GL4:
			case FluidProduct::GEAR_OIL_80W90_GL5:	return { 0.7f, 0.5f, 0.3f, 1.f };
			case FluidProduct::ATF:					return { 0.95f, 0.35f, 0.35f, 1.f };
			case FluidProduct::DIESEL:				return { 0.95f, 0.85f, 0.3f, 1.f };
			default:								return { 0.9f, 0.25f, 0.2f, 1.f };	// petrol: the red can
		}
	}
}

void WorkshopUI::DrawCarStore(Workshop& workshop)
{
	// Like the parts store: catalogue cards on the left, each with a turning view of what it is, and the order
	// form for the one picked on the right, with a bigger view of it.
	auto model = [&](const std::string& name) { return m_modelLookup ? m_modelLookup(name) : 0u; };
	// A card's picture: the model turning in it (only drawn while the card's in view).
	auto turning = [&](const std::string& name, ImVec4 tint = { 1.f, 1.f, 1.f, 1.f }) {
		return [&, name, tint](ImDrawList* drawList, ImVec2 min, float size) {
			ImVec2 max = { min.x + size, min.y + size };
			if (uint32_t index = model(name); index && ImGui::IsRectVisible(min, max))
				ModelPreview::DrawAt(drawList, min, max, index, tint);
		};
	};
	auto price = [](double dollars) { return dollars < 10.0 ? std::format("${:.2f}", dollars) : std::format("${:.0f}", dollars); };
	auto pickPart = [&](PartType type) {
		m_carStoreKind = 1;
		m_carStoreOrder = {};
		m_carStoreOrder.type = type;
		m_carStoreOrder.condition = PartCondition::NEW;
		m_carStoreOrder.quantity = type == PartType::LUG_NUT || type == PartType::NUT_M10 || type == PartType::NUT_M12 ? 4 : 1;
		auto sizes = Workshop::SizeOptions(type);
		m_carStoreOrder.sizeOffset = sizes.empty() ? 0.0 : sizes.front();
	};

	if (ImGui::BeginChild("CarCatalogue", { ImGui::GetContentRegionAvail().x * 0.58f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::SeparatorText("The project");
		if (StoreCard("project", "LE86 project shell", price(Workshop::PROJECT_CAR_PRICE), m_carStoreKind == 0, workshop.OwnsCar(),
				turning(ShopModels::PROJECT_CAR)))
			m_carStoreKind = 0;
		ImGui::NewLine();

		// A section per system of the car, and its hardware after them.
		std::optional<PartSystem> section;
		for (auto type : PartCatalog::All())
		{
			const auto& info = PartCatalog::Get(type);
			// Its nuts are on the Nuts & Bolts page with the rest of the hardware.
			if (!IsCarPart(type) || !info.listed || type == PartType::BODY_SHELL || info.system == PartSystem::FASTENERS)
				continue;
			if (info.system != section)
			{
				if (section)
					ImGui::NewLine();
				ImGui::SeparatorText(PartSystemName(info.system));
				section = info.system;
			}
			PartOrder cheapest;
			cheapest.type = type;
			cheapest.condition = info.soldUsed ? PartCondition::USED : PartCondition::NEW;
			if (StoreCard(PartTypeName(type), PartCatalog::StoreName(type), "from " + price(workshop.Quote(cheapest)),
					m_carStoreKind == 1 && m_carStoreOrder.type == type, false, turning(PartTypeName(type))))
				pickPart(type);
		}
		ImGui::NewLine();
		ImGui::SeparatorText("Fluids");
		for (int p = 0; p < FLUID_PRODUCTS; p++)
		{
			auto product = static_cast<FluidProduct>(p);
			std::string label = std::format("{} ({:g} l)", FluidProductName(product), Workshop::FluidUnitLitres(product));
			if (StoreCard(FluidProductName(product), label.c_str(), price(Workshop::FluidUnitPrice(product) * workshop.PartPriceFactor()),
					m_carStoreKind == 2 && m_carStoreFluid == p, false, turning(FluidModel(product), FluidTint(product))))
			{
				m_carStoreKind = 2;
				m_carStoreFluid = p;
				m_carStoreBottles = 1;
			}
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();
	if (!ImGui::BeginChild("CarOrder", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}
	if (m_carStoreKind == 0)
	{
		ImGui::Text("LE86 project shell");
		DrawPreview(ShopModels::PROJECT_CAR);
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("A 1985 MonkeyKobo Comet GT coupe, as it came out of a barn, sitting in the salvage yard beside the shop: the bare shell on "
			"jack stands, with whatever came off it lying round it and its nuts and bolts in a box by the gate. No engine (build one on "
			"a stand and hoist it in), none of the parts only sold new, and some of what's there past saving. Build it up, fill it, "
			"tune it, drive it and sell it.");
		ImGui::PopTextWrapPos();
		ImGui::Separator();
		CartLine project;
		project.kind = CartLine::Kind::PROJECT_CAR;
		AddToCartButton(workshop, project, std::format("Add to cart (${:.0f})", Workshop::PROJECT_CAR_PRICE).c_str());
	}
	else if (m_carStoreKind == 1)
	{
		PartType type = m_carStoreOrder.type;
		const auto& info = PartCatalog::Get(type);
		ImGui::Text("%s", PartCatalog::StoreName(type));
		DrawPreview(PartTypeName(type));
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("%s", info.description);
		ImGui::PopTextWrapPos();
		ImGui::Separator();
		if (info.soldUsed)
		{
			int condition = static_cast<int>(m_carStoreOrder.condition);
			ImGui::RadioButton("New", &condition, static_cast<int>(PartCondition::NEW));
			ImGui::SameLine();
			ImGui::RadioButton("Used", &condition, static_cast<int>(PartCondition::USED));
			m_carStoreOrder.condition = static_cast<PartCondition>(condition);
			ImGui::SameLine();
			ImGui::TextDisabled("(used is cheaper, and may be worn)");
		}
		else
			m_carStoreOrder.condition = PartCondition::NEW;
		auto sizes = Workshop::SizeOptions(type);
		if (sizes.size() > 1)
		{
			ImGui::SetNextItemWidth(220.f);
			if (ImGui::BeginCombo("Final drive", std::format("{:.3f} : 1", m_carStoreOrder.sizeOffset).c_str()))
			{
				for (double size : sizes)
					if (ImGui::Selectable(std::format("{:.3f} : 1{}", size, size == sizes.front() ? " (factory)" : "").c_str(), m_carStoreOrder.sizeOffset == size))
						m_carStoreOrder.sizeOffset = size;
				ImGui::EndCombo();
			}
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("A shorter (higher) ratio pulls harder in every gear and tops out sooner.");
			ImGui::PopTextWrapPos();
		}
		ImGui::SetNextItemWidth(220.f);
		ImGui::InputInt("Quantity", &m_carStoreOrder.quantity);
		m_carStoreOrder.quantity = std::clamp(m_carStoreOrder.quantity, 1, 64);
		ImGui::Spacing();
		CartLine line;
		line.kind = CartLine::Kind::PART;
		line.order = m_carStoreOrder;
		AddToCartButton(workshop, line, std::format("Add to cart ({})", price(workshop.Quote(m_carStoreOrder))).c_str());
	}
	else
	{
		auto product = static_cast<FluidProduct>(m_carStoreFluid);
		ImGui::Text("%s", FluidProductName(product));
		if (uint32_t index = model(FluidModel(product)))
		{
			float width = ImGui::GetContentRegionAvail().x;
			ModelPreview::Draw(index, width, (std::min)(width * 0.6f, 220.f), FluidTint(product));
		}
		ImGui::Text("%g l a %s, %s", Workshop::FluidUnitLitres(product), FluidModel(product) == std::string("Fuel Can") ? "can" : "bottle",
			price(Workshop::FluidUnitPrice(product) * workshop.PartPriceFactor()).c_str());
		ImGui::TextDisabled("%.1f l in stock. What's bought is kept on the shelf till it's poured with the funnel.", workshop.FluidStock(product));
		// What the book calls for it in, if anything.
		std::string uses;
		for (const auto& fluid : CarSpec::LE86Coupe().fluids)
			if (FluidFitFor(fluid.circuit, product) == FluidFit::RIGHT)
				uses += (uses.empty() ? "" : ", ") + std::string(FluidCircuitName(fluid.circuit));
		ImGui::PushTextWrapPos(0.f);
		if (!uses.empty())
			ImGui::TextColored(GOOD, "The book calls for it in: %s.", uses.c_str());
		else
			ImGui::TextColored(WARN, "The book doesn't call for it anywhere in this car.");
		ImGui::PopTextWrapPos();
		ImGui::Separator();
		ImGui::SetNextItemWidth(220.f);
		ImGui::InputInt("Bottles", &m_carStoreBottles);
		m_carStoreBottles = std::clamp(m_carStoreBottles, 1, 20);
		CartLine line;
		line.kind = CartLine::Kind::FLUID;
		line.index = static_cast<size_t>(product);
		line.quantity = m_carStoreBottles;
		AddToCartButton(workshop, line, std::format("Add to cart ({})",
			price(Workshop::FluidUnitPrice(product) * workshop.PartPriceFactor() * m_carStoreBottles)).c_str());
	}
	ImGui::EndChild();
}
