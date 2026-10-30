#include "WorkshopUI.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

#include <imgui.h>

#include "ItemIcons.h"
#include "Ratings.h"
#include "ModelPreview.h"
#include "../FrameProfiler.h"
#include "PartIcons.h"
#include "../Simulation/PartCatalog.h"
#include "../World/ShopAssets.h"
#include "../World/ToolShapes.h"

namespace
{
	const ImVec4 COLOR_GOOD = { 0.45f, 0.90f, 0.45f, 1.f };
	const ImVec4 COLOR_WARN = { 1.00f, 0.80f, 0.30f, 1.f };
	const ImVec4 COLOR_BAD = { 1.00f, 0.40f, 0.40f, 1.f };
	const ImVec4 COLOR_DIM = { 0.55f, 0.55f, 0.55f, 1.f };
	const ImVec4 COLOR_XRAY = { 0.55f, 0.75f, 1.00f, 1.f };

	const char* STATION_NAMES[Feature::STATIONS] = { "Top", "Middle", "Bottom" };
	const char* AXIS_NAMES[Feature::AXES] = { "Thrust", "Pin" };

	std::string SizeClassLabel(double sizeClass)
	{
		return std::abs(sizeClass) < 1e-6 ? "STD" : std::format("{:+.2f}", sizeClass);
	}

	void ColoredRangeText(double value, const Range& range, const std::string& text)
	{
		ImGui::TextColored(range.Contains(value) ? COLOR_GOOD : COLOR_BAD, "%s", text.c_str());
	}

	constexpr const char* XRAY_TIP = "The part's real geometry (debug x-ray). The player never gets to see this.";

	// TableHeadersRow, but each column header explains itself on hover. Tips past the column count are ignored.
	void HeadersRow(std::initializer_list<const char*> tips)
	{
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		for (int column = 0; column < ImGui::TableGetColumnCount(); column++)
		{
			if (!ImGui::TableSetColumnIndex(column))
				continue;
			ImGui::PushID(column);
			ImGui::TableHeader(ImGui::TableGetColumnName(column));
			if (column < static_cast<int>(tips.size()) && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tips.begin()[column]);
			ImGui::PopID();
		}
	}
}

WorkshopUI::WorkshopUI()
{
}

WorkshopUI::~WorkshopUI()
{
}

std::string WorkshopUI::Size(double mm) const
{
	return Units::FormatSize(mm, m_units);
}

std::string WorkshopUI::Deviation(double mm) const
{
	return Units::FormatDeviation(mm, m_units);
}

std::string WorkshopUI::FeatureValue(const Feature& feature, double value) const
{
	switch (feature.kind)
	{
		case FeatureKind::FLATNESS:
		case FeatureKind::RUNOUT:	return Deviation(value);
		case FeatureKind::VOLUME:	return Units::FormatVolume(value);
		case FeatureKind::FORCE:	return Units::FormatForce(value);
		default:					return Size(value);
	}
}

std::string WorkshopUI::SpecText(const Feature& feature) const
{
	switch (feature.kind)
	{
		case FeatureKind::FLATNESS:
		case FeatureKind::RUNOUT:	return "<= " + Deviation(feature.spec.Max());
		case FeatureKind::VOLUME:	return std::format("{:.1f} - {:.1f} cc", feature.spec.Min(), feature.spec.Max());
		case FeatureKind::FORCE:	return std::format("{:.0f} - {:.0f} N", feature.spec.Min(), feature.spec.Max());
		default:
		{
			auto text = Size(feature.spec.Min()) + " - " + Size(feature.spec.Max());
			if (feature.sizeClasses.size() > 1)
				text += " STD";
			return text;
		}
	}
}

std::string WorkshopUI::RangeText(const Range& range) const
{
	return Deviation(range.min) + " - " + Deviation(range.max);
}

Feature WorkshopUI::FromReading(const Feature& feature, const FeatureReading& reading)
{
	Feature measured = feature;
	for (int station = 0; station < Feature::STATIONS; station++)
		for (int axis = 0; axis < Feature::AXES; axis++)
			measured.samples[station][axis] = reading.samples[station][axis];
	measured.value = reading.value;
	measured.surfaceRa = reading.finishRa.value_or(0.0);
	return measured;
}

std::optional<double> WorkshopUI::MeasuredSize(const pPart& part, const std::string& featureName)
{
	if (!part)
		return std::nullopt;

	auto feature = part->FindFeature(featureName);
	auto reading = part->GetReading(featureName);
	if (!feature || !reading || !reading->hasSize || reading->stale)
		return std::nullopt;
	return FromReading(*feature, *reading).Size();
}

void WorkshopUI::Open(WorkshopTab tab)
{
	m_open = true;
	m_requestedTab = tab;
	m_currentTab = tab;
	if (tab != WorkshopTab::MACHINE)
		m_activeMachine = -1;
}

void WorkshopUI::OpenStore(StoreCategory category)
{
	// The parts computer replaces whatever workshop screen was open.
	m_open = false;
	m_activeMachine = -1;
	m_storeOpen = true;
	m_requestedCategory = category;
}

void WorkshopUI::OpenMachine(int machineIndex)
{
	if (machineIndex < 0 || machineIndex >= static_cast<int>(MachineCatalog::All().size()))
		return;
	m_activeMachine = machineIndex;
	Open(WorkshopTab::MACHINE);
}

void WorkshopUI::Close()
{
	m_open = false;
	m_storeOpen = false;
	m_activeMachine = -1;
}

void WorkshopUI::SetXray(bool xray)
{
	m_xray = xray;
}

bool WorkshopUI::IsOpen() const
{
	return m_open || m_storeOpen;
}

void WorkshopUI::SelectPart(const Part& part, int machineIndex)
{
	if (m_selectedPartId != part.GetId())
		m_lastMachiningMessage.clear();
	m_selectedPartId = part.GetId();
	m_selectedFeature.clear();
	m_selectedMachine = -1;

	const auto& machines = MachineCatalog::All();
	if (machineIndex < 0 || machineIndex >= static_cast<int>(machines.size()))
		return;

	for (const auto& feature : part.GetFeatures())
		if (Machining::CanMachine(machines[machineIndex], feature))
		{
			m_selectedFeature = feature.name;
			m_selectedMachine = machineIndex;
			m_machineParameter = machines[machineIndex].operation == MachineOperation::SURFACE ? Units::ToDisplay(0.05, m_units) : 0.0;
			return;
		}
}

void WorkshopUI::Update(float deltaTime)
{
	constexpr float SPIN_DOWN_SECONDS = 1.5f;
	if (m_pullActive)
	{
		m_pullTime += deltaTime;
		if (m_pullTime >= PULL_SECONDS)
		{
			m_pullActive = false;
			m_spinDownTime = m_pullFailed ? 0.f : SPIN_DOWN_SECONDS;
		}
	}
	else if (m_spinDownTime > 0.f)
		m_spinDownTime = (std::max)(0.f, m_spinDownTime - deltaTime);
}

int WorkshopUI::GetPullEngine() const
{
	return m_pullEngine;
}

float WorkshopUI::GetLiveRpm() const
{
	if (m_pullActive)
		return m_pullStartRpm + (m_pullEndRpm - m_pullStartRpm) * (std::min)(1.f, m_pullTime / PULL_SECONDS);
	// A failed engine stops dead; a healthy one coasts down off the dyno.
	if (m_spinDownTime > 0.f && m_pullEndRpm > 0.f)
		return m_pullEndRpm * m_spinDownTime / 1.5f;
	return 0.f;
}

void WorkshopUI::SetPartAvailability(const std::function<bool(uint32_t)>& isUsable)
{
	m_isUsable = isUsable;
}

void WorkshopUI::SetBackpackContents(const std::function<void()>& drawContents)
{
	m_backpackContents = drawContents;
}

void WorkshopUI::SetUnits(Units::System units)
{
	m_units = units;
}

Units::System WorkshopUI::GetUnits() const
{
	return m_units;
}

bool WorkshopUI::IsUsable(const Part& part) const
{
	return !m_isUsable || m_isUsable(part.GetId());
}

void WorkshopUI::Draw(Workshop& workshop)
{
	if (m_storeOpen)
		DrawStoreWindow(workshop);

	if (!m_open)
	{
		// Closed with the window's X: the machine tab goes with it.
		m_activeMachine = -1;
		return;
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize({ 1150.f, 780.f }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });
	// Opening from the shop floor should always show the window, even if it was left collapsed.
	if (m_requestedTab)
	{
		ImGui::SetNextWindowFocus();
		ImGui::SetNextWindowCollapsed(false);
	}

	// One screen at a time: Tab is the inventory, and each station's screen only opens from that
	// station. The window is named after what it shows.
	if (m_currentTab == WorkshopTab::BACKPACK)
	{
		// Never so small that the grid and hotbar crowd the item and stats panel beside them off
		// the window (a size saved from an older, narrower layout included).
		ImGui::SetNextWindowSizeConstraints({ 1000.f, 600.f }, { FLT_MAX, FLT_MAX });
		if (ImGui::Begin("Inventory###Inventory", &m_open) && m_backpackContents)
			m_backpackContents();
		ImGui::End();
		m_requestedTab = std::nullopt;
		return;
	}

	std::string title = "Workbench";
	if (m_currentTab == WorkshopTab::MACHINE && m_activeMachine >= 0)
		title = MachineCatalog::All()[m_activeMachine].name;
	else if (m_currentTab == WorkshopTab::ENGINE_STAND)
		title = "Engine Stand";
	else if (m_currentTab == WorkshopTab::DYNO)
		title = "Dyno Cell";

	if (ImGui::Begin(std::format("{}###Workshop", title).c_str(), &m_open))
	{
		DrawStatusBar(workshop);
		ImGui::Separator();

		float logHeight = ImGui::GetTextLineHeightWithSpacing() * 7.f;
		if (ImGui::BeginChild("Main", { 0.f, -logHeight }))
		{
			switch (m_currentTab)
			{
				case WorkshopTab::MACHINE:
					if (m_activeMachine >= 0)
						DrawMachineTab(workshop);
					break;
				case WorkshopTab::ENGINE_STAND:	DrawAssemblyTab(workshop);	break;
				case WorkshopTab::DYNO:			DrawDynoTab(workshop);		break;
				default:						DrawInventoryTab(workshop);	break;
			}
		}
		ImGui::EndChild();
		m_requestedTab = std::nullopt;

		DrawLog(workshop);
	}
	ImGui::End();
}

void WorkshopUI::DrawStatusBar(Workshop& workshop)
{
	// Not the engine's name: there'll be more than one kind of engine.
	ImGui::TextColored(COLOR_GOOD, "$%.0f", workshop.GetMoney());
	ImGui::SameLine(0.f, 30.f);
	ImGui::Text("Shop clock: %.1f h", workshop.GetHours());

	ImGui::SameLine(0.f, 30.f);
	int units = static_cast<int>(m_units);
	ImGui::RadioButton("Metric", &units, static_cast<int>(Units::System::METRIC));
	ImGui::SameLine();
	ImGui::RadioButton("Imperial", &units, static_cast<int>(Units::System::IMPERIAL));
	m_units = static_cast<Units::System>(units);

	if (m_xray)
	{
		ImGui::SameLine(0.f, 30.f);
		ImGui::TextColored(COLOR_XRAY, "X-ray on (F3)");
	}

	ImGui::SameLine(0.f, 30.f);
	ImGui::TextDisabled("Tab / Esc: back to the shop floor");
}

std::string WorkshopUI::FeatureSection(PartType type, const std::string& featureName)
{
	auto has = [&](const char* text) { return featureName.find(text) != std::string::npos; };
	switch (type)
	{
		case PartType::BLOCK:
			if (has("Bore")) return "Cylinder bores";
			if (has("Main Housing")) return "Main bearing housings";
			return "Deck";
		case PartType::CYLINDER_HEAD:
			if (has("Guide")) return "Valve guides";
			if (has("Seat")) return "Valve seats";
			if (has("Cam Bore")) return "Cam bores";
			if (has("Chamber")) return "Combustion chambers";
			return "Deck";
		case PartType::CRANKSHAFT:
			if (has("Main Journal")) return "Main journals";
			if (has("Rod Journal")) return "Rod journals";
			return "Stroke";
		case PartType::CAMSHAFT:
			if (has("Journal")) return "Journals";
			return "Lobes";
		default:
			return "Measurements";
	}
}

void WorkshopUI::DrawInventoryTab(Workshop& workshop)
{
	// Only what's actually on the workbench. Parts set up in a machine have that machine's tab.
	std::vector<BenchItem> items = m_benchContents ? m_benchContents() : std::vector<BenchItem>{};
	std::erase_if(items, [](const BenchItem& item) { return item.machineIndex >= 0; });
	pPart selected = nullptr;
	m_selectedPartMachine = -1;
	for (const auto& item : items)
		if (item.part->GetId() == m_selectedPartId)
		{
			selected = item.part;
			m_selectedPartMachine = item.machineIndex;
		}
	if (!selected && !items.empty())
	{
		selected = items.front().part;
		m_selectedPartId = selected->GetId();
		m_selectedPartMachine = items.front().machineIndex;
		m_selectedFeature.clear();
	}

	if (ImGui::BeginChild("OnTheBench", { 300.f, 0.f }, ImGuiChildFlags_Borders))
	{
		if (items.empty())
		{
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("The workbench is empty. Set a part down on top of it (right-click to place it, or G to drop it).");
			ImGui::PopTextWrapPos();
		}

		std::string currentLocation;
		for (const auto& item : items)
		{
			if (item.location != currentLocation)
			{
				ImGui::SeparatorText(item.location.c_str());
				currentLocation = item.location;
			}

			const auto& part = item.part;
			ImGui::PushID(static_cast<int>(part->GetId()));
			PartIcons::Icon(part->GetType(), ImGui::GetTextLineHeight() * 1.3f);
			ImGui::SameLine();
			bool damaged = part->GetCondition() == PartCondition::DAMAGED;
			if (damaged)
				ImGui::PushStyleColor(ImGuiCol_Text, COLOR_BAD);
			if (ImGui::Selectable(std::format("#{} {}", part->GetId(), part->GetName()).c_str(), part == selected))
			{
				m_selectedPartId = part->GetId();
				m_selectedFeature.clear();
				m_lastMachiningMessage.clear();
				if (item.machineIndex >= 0)
					SelectPart(*part, item.machineIndex);
			}
			if (damaged)
				ImGui::PopStyleColor();
			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	if (ImGui::BeginChild("Inspector", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		if (selected)
			DrawPartInspector(workshop, selected);
		else
			ImGui::TextDisabled("Nothing on the bench.");
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawMachineTab(Workshop& workshop)
{
	const auto& machine = MachineCatalog::All()[m_activeMachine];

	// The machine itself: what it does, how long it takes.
	ItemIcons::MachineIcon(machine, 56.f);
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::Text("%s", machine.name.c_str());
	std::string timeText = machine.hoursPerMm > 0.0
		? std::format("{:.1f} h setup + {:.0f} h per mm removed", machine.hoursPerUse, machine.hoursPerMm)
		: std::format("{:.1f} h per job", machine.hoursPerUse);
	ImGui::TextDisabled("%s   |   leaves %.2f um Ra   |   %s", Machining::OperationName(machine.operation), machine.finishRa, timeText.c_str());
	ImGui::PushTextWrapPos(0.f);
	ImGui::TextDisabled("%s", machine.description.c_str());
	ImGui::PopTextWrapPos();
	ImGui::EndGroup();
	ImGui::Separator();

	// The part set up in it.
	pPart part = nullptr;
	if (m_benchContents)
		for (const auto& item : m_benchContents())
			if (item.machineIndex == m_activeMachine)
				part = item.part;
	if (!part)
	{
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("Nothing is set up in the %s. Hold a part it can work on and press E at the machine.", machine.name.c_str());
		ImGui::PopTextWrapPos();
		return;
	}

	if (m_selectedPartId != part->GetId())
		m_lastMachiningMessage.clear();
	m_selectedPartId = part->GetId();
	m_selectedPartMachine = m_activeMachine;

	// Only the features this machine can cut; keep the selection on one of them.
	std::vector<const Feature*> cuttable;
	for (const auto& feature : part->GetFeatures())
		if (Machining::CanMachine(machine, feature))
			cuttable.push_back(&feature);
	bool selectionCuttable = std::any_of(cuttable.begin(), cuttable.end(), [&](const Feature* feature) { return feature->name == m_selectedFeature; });
	if (!selectionCuttable)
		m_selectedFeature = cuttable.empty() ? "" : cuttable.front()->name;

	if (ImGui::BeginChild("MachineFeatures", { 280.f, 0.f }, ImGuiChildFlags_Borders))
	{
		PartIcons::Icon(part->GetType(), ImGui::GetTextLineHeight() * 2.f);
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::PushTextWrapPos(0.f);
		ImGui::Text("#%u %s", part->GetId(), part->GetName().c_str());
		ImGui::PopTextWrapPos();
		ImGui::TextDisabled("%s  |  %s", GetMaterial(part->GetMaterial()).name, PartConditionName(part->GetCondition()));
		ImGui::EndGroup();
		if (part->GetCondition() == PartCondition::DAMAGED)
			ImGui::TextColored(COLOR_BAD, "Damaged: %s", part->GetDamageReason().c_str());

		ImGui::SeparatorText("What it can cut");
		if (cuttable.empty())
			ImGui::TextDisabled("Nothing on this part.");
		// Each with a coloured mark: green in spec, red out, grey not measured (or out of date).
		for (const Feature* feature : cuttable)
		{
			auto reading = part->GetReading(feature->name);
			ImVec4 mark = COLOR_DIM;
			if (reading && !reading->stale && reading->hasSize)
				mark = FromReading(*feature, *reading).Evaluate(false) == FeatureIssue::NONE ? COLOR_GOOD : COLOR_BAD;
			ImGui::TextColored(mark, "o");
			ImGui::SameLine();
			if (ImGui::Selectable(feature->name.c_str(), m_selectedFeature == feature->name) && m_selectedFeature != feature->name)
			{
				m_selectedFeature = feature->name;
				m_lastMachiningMessage.clear();
			}
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("MachineWork", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		if (auto feature = part->FindFeature(m_selectedFeature))
		{
			ImGui::Text("%s", feature->name.c_str());
			DrawFeatureDetail(workshop, *part, *feature);
			DrawMachiningPanel(workshop, *part, *feature);
		}
		else
			ImGui::TextDisabled("Pick a feature on the left.");
	}
	ImGui::EndChild();
}

void WorkshopUI::SetBenchContents(const std::function<std::vector<BenchItem>()>& benchContents)
{
	m_benchContents = benchContents;
}

void WorkshopUI::DrawPartInspector(Workshop& workshop, const pPart& part)
{
	PartIcons::Icon(part->GetType(), 44.f);
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::Text("%s", part->GetName().c_str());
	ImGui::TextDisabled("#%u  |  %s  |  %s  |  %s", part->GetId(), GetMaterial(part->GetMaterial()).name,
		PartQualityName(part->GetQuality()), PartConditionName(part->GetCondition()));
	ImGui::EndGroup();

	if (part->GetCondition() == PartCondition::DAMAGED)
		ImGui::TextColored(COLOR_BAD, "Damaged: %s", part->GetDamageReason().c_str());

	// Nuts and bolts: the size, the hole it's in and how it was done up (as far as the wrench
	// could tell), or every kind of hole it fits.
	if (const auto& fastener = PartCatalog::Get(part->GetType()).fastener; fastener.IsFastener())
	{
		ImGui::Text("%s %s, %d mm", fastener.thread, fastener.nut ? "nut" : "bolt", fastener.headMm);
		int engine = workshop.EngineOf(*part);
		auto position = engine >= 0 ? workshop.GetAssembly(engine).Find(part.get()) : std::nullopt;
		if (const auto* holes = position ? EngineAssembly::Holes(position->first) : nullptr)
		{
			ImGui::Text("In: %s, %.0f Nm", workshop.GetAssembly(engine).PositionName(position->first, position->second).c_str(), holes->torqueNm);
			switch (part->GetTightness())
			{
				case FastenerTightness::TORQUED:	ImGui::TextColored(COLOR_GOOD, "Torqued to spec with a torque wrench."); break;
				case FastenerTightness::SNUG:		ImGui::TextColored(COLOR_WARN, "Snugged up by feel, not torqued: go over it with a torque wrench."); break;
				case FastenerTightness::LOOSE:		ImGui::TextColored(COLOR_BAD, "Finger tight: do it up with a %d mm socket or wrench.", fastener.headMm); break;
			}
			if (part->GetTorque() > 0.0)
			{
				if (m_xray)
					ImGui::TextColored(COLOR_XRAY, "X-ray: really %.1f Nm (%.0f%% of spec)", part->GetTorque(), part->GetTorque() / holes->torqueNm * 100.0);
			}
		}
		else
			for (const auto& holes : PartCatalog::Holes())
				if (holes.size == part->GetType())
					ImGui::BulletText("Fits the %ss: %.0f Nm", holes.name, holes.torqueNm);
	}

	if (!IsUsable(*part))
	{
		ImGui::Spacing();
		ImGui::TextColored(COLOR_WARN, "Still in its delivery box. Open the delivery by the roll-up door first.");
		return;
	}

	bool installed = workshop.EngineOf(*part) >= 0;
	ImGui::BeginDisabled(installed);
	if (ImGui::Button(std::format("Sell for ${:.0f}", std::round(part->GetValue() * 0.5)).c_str()))
	{
		workshop.SellPart(part);
		ImGui::EndDisabled();
		return;
	}
	ImGui::EndDisabled();
	if (installed)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(on the engine stand)");
	}

	ImGui::Spacing();

	int columns = m_xray ? 8 : 7;
	ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

	// Say plainly that the rows open up.
	ImGui::TextColored(COLOR_XRAY, "Click any feature for its full readings and how to bring it back into spec.");

	// Big parts have dozens of features, so they're grouped by area, each in its own section.
	std::vector<std::string> sections;
	for (const auto& feature : part->GetFeatures())
	{
		auto section = FeatureSection(part->GetType(), feature.name);
		if (std::find(sections.begin(), sections.end(), section) == sections.end())
			sections.push_back(section);
	}

	for (const auto& section : sections)
	{
		if (sections.size() > 1 && !ImGui::CollapsingHeader(section.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			continue;
		if (!ImGui::BeginTable(("Features " + section).c_str(), columns, flags))
			continue;

		ImGui::TableSetupColumn("Feature");
		ImGui::TableSetupColumn("Spec");
		ImGui::TableSetupColumn("Measured");
		ImGui::TableSetupColumn("Finish");
		ImGui::TableSetupColumn("Verdict");
		ImGui::TableSetupColumn("Measure with", ImGuiTableColumnFlags_WidthFixed, 170.f);
		if (m_xray)
			ImGui::TableSetupColumn("True");
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 64.f);
		HeadersRow({
			"The surface or dimension. Click a row for its readings, how to fix it and the machining.",
			"The size it has to be, with its tolerance band.",
			"Your last reading of its size (the average of all stations). Stale once the part's been cut or swapped since.",
			"Your last surface finish reading, in Ra (lower is smoother).",
			"How your last reading compares to spec: in spec, or what's wrong with it.",
			"The tool for the job. Hold it and aim at the feature in the shop.",
			m_xray ? XRAY_TIP : "Click a row to open its details.",
			"Click a row to open its details." });

		for (const auto& feature : part->GetFeatures())
		{
			if (FeatureSection(part->GetType(), feature.name) != section)
				continue;
			ImGui::PushID(feature.name.c_str());
			ImGui::TableNextRow();
			auto reading = part->GetReading(feature.name);

			ImGui::TableSetColumnIndex(0);
			bool selected = m_selectedFeature == feature.name;
			if (ImGui::Selectable(feature.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
			{
				if (!selected)
				{
					m_selectedFeature = feature.name;
					m_selectedMachine = -1;
					m_lastMachiningMessage.clear();
				}
				m_scrollToDetail = true;
			}
			bool hovered = ImGui::IsItemHovered();
			if (hovered)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				ImGui::SetTooltip("Click for %s's readings and how to fix it.", feature.name.c_str());
			}

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(SpecText(feature).c_str());

			ImGui::TableSetColumnIndex(2);
			if (!feature.directlyMeasurable)
				ImGui::TextDisabled("Check on stand");
			else if (reading && reading->hasSize)
			{
				auto measured = FromReading(feature, *reading);
				auto text = FeatureValue(feature, measured.Size());
				if (reading->stale)
					ImGui::TextColored(COLOR_DIM, "%s (stale)", text.c_str());
				else
					ImGui::TextUnformatted(text.c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", reading->toolName.c_str());
			}
			else
				ImGui::TextDisabled("-");

			ImGui::TableSetColumnIndex(3);
			if (!feature.HasFinishSpec())
				ImGui::TextDisabled("n/a");
			else if (reading && reading->finishRa)
				ImGui::TextColored(reading->stale ? COLOR_DIM : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Units::FormatFinish(*reading->finishRa).c_str());
			else
				ImGui::TextDisabled("-");

			ImGui::TableSetColumnIndex(4);
			if (reading && !reading->stale && (reading->hasSize || reading->finishRa))
			{
				auto measured = FromReading(feature, *reading);
				uint32_t issues = 0;
				if (reading->hasSize)
					issues |= measured.Evaluate(false);
				if (reading->finishRa)
					issues |= measured.Evaluate(true) & (FeatureIssue::ROUGH | FeatureIssue::GLAZED);

				std::string verdict = DescribeIssues(issues, feature.kind);
				if (reading->hasSize && issues == FeatureIssue::NONE)
					if (auto sizeClass = measured.MatchSizeClass(); sizeClass && *sizeClass != 0.0)
						verdict += " (" + SizeClassLabel(*sizeClass) + ")";
				ImGui::TextColored(issues == FeatureIssue::NONE ? COLOR_GOOD : COLOR_BAD, "%s", verdict.c_str());
			}
			else
				ImGui::TextDisabled("Not measured");

			// Measuring happens in the shop: these say which tool to pick up.
			ImGui::TableSetColumnIndex(5);
			if (feature.directlyMeasurable)
			{
				ImGui::TextDisabled("%s", ToolHint(workshop, workshop.BestToolFor(feature)).c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Hold the tool and aim at this feature in the shop.");
			}
			if (feature.HasFinishSpec())
				ImGui::TextDisabled("Ra: %s", ToolHint(workshop, workshop.FinishTool()).c_str());

			if (m_xray)
			{
				ImGui::TableSetColumnIndex(6);
				std::string truth = FeatureValue(feature, feature.Size());
				if (feature.IsRound())
					truth += std::format("  T{:.3f} O{:.3f}", feature.Taper(), feature.OutOfRound());
				if (feature.HasFinishSpec())
					truth += std::format("  {:.2f}Ra", feature.surfaceRa);
				ImGui::TextColored(COLOR_XRAY, "%s", truth.c_str());
			}

			// The open-me cue, brighter on the row under the mouse and on the open one.
			ImGui::TableSetColumnIndex(m_xray ? 7 : 6);
			if (selected)
				ImGui::TextColored(COLOR_GOOD, "Open v");
			else
				ImGui::TextColored(hovered ? COLOR_XRAY : COLOR_DIM, "Details >");
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	if (auto feature = part->FindFeature(m_selectedFeature))
	{
		ImGui::Spacing();
		ImGui::SeparatorText(feature->name.c_str());
		// The details open below the whole table, which can be off screen on a big part.
		if (m_scrollToDetail)
		{
			ImGui::SetScrollHereY(0.f);
			m_scrollToDetail = false;
		}
		DrawFeatureDetail(workshop, *part, *feature);
		DrawFixAdvice(workshop, *part, *feature);
		DrawMachiningPanel(workshop, *part, *feature);
	}
	else
	{
		ImGui::Spacing();
		ImGui::TextDisabled("No feature open. Click one above to see its readings and how to fix it.");
	}
}

void WorkshopUI::DrawFeatureDetail(Workshop& workshop, Part& part, const Feature& feature)
{
	auto reading = part.GetReading(feature.name);

	// One label per value, in two side-by-side cards: what it should be, and what you found.
	auto row = [](const char* label) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextDisabled("%s", label);
		ImGui::TableSetColumnIndex(1);
	};
	ImGuiTableFlags cardFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg;

	if (ImGui::BeginTable("SpecAndReading", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableNextRow();

		// Spec
		ImGui::TableSetColumnIndex(0);
		ImGui::SeparatorText("Spec");
		if (ImGui::BeginTable("Spec", 2, cardFlags))
		{
			row(feature.IsRound() ? "Diameter" : "Size");
			ImGui::TextUnformatted(SpecText(feature).c_str());
			if (feature.sizeClasses.size() > 1)
				for (double sizeClass : feature.sizeClasses)
				{
					if (std::abs(sizeClass) < 1e-6)
						continue;
					row(std::format("{} oversize", SizeClassLabel(sizeClass)).c_str());
					ImGui::TextUnformatted(Size(feature.spec.Mid() + sizeClass).c_str());
				}
			if (feature.maxTaper > 0.0)
			{
				row("Max taper");
				ImGui::Text("%s", Deviation(feature.maxTaper).c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Largest allowed difference between top and bottom.");
			}
			if (feature.maxOutOfRound > 0.0)
			{
				row("Max out-of-round");
				ImGui::Text("%s", Deviation(feature.maxOutOfRound).c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Largest allowed difference between the thrust and pin directions.");
			}
			if (feature.HasFinishSpec())
			{
				row("Finish");
				if (feature.maxRa > 0.0)
					ImGui::Text("%.2f - %.2f um Ra", feature.minRa, feature.maxRa);
				else
					ImGui::Text("at least %.2f um Ra", feature.minRa);
			}
			ImGui::EndTable();
		}

		// Your readings
		ImGui::TableSetColumnIndex(1);
		ImGui::SeparatorText("Your last reading");
		bool hasReading = reading && (reading->hasSize || reading->finishRa);
		if (!hasReading)
		{
			ImGui::PushTextWrapPos(0.f);
			if (feature.directlyMeasurable)
				ImGui::TextDisabled("Not measured yet. Use the %s on it.", ToolHint(workshop, workshop.BestToolFor(feature)).c_str());
			else
				ImGui::TextDisabled("Not measured yet. This one is checked with the part on the engine stand.");
			ImGui::PopTextWrapPos();
		}
		else
		{
			if (reading->stale)
				ImGui::TextColored(COLOR_WARN, "Out of date: it's been cut since.");
			auto measured = FromReading(feature, *reading);
			uint32_t issues = reading->hasSize ? measured.Evaluate(false) : FeatureIssue::NONE;
			ImVec4 valueColor = reading->stale ? COLOR_DIM : ImGui::GetStyleColorVec4(ImGuiCol_Text);
			auto judged = [&](bool bad) { return reading->stale ? COLOR_DIM : bad ? COLOR_BAD : COLOR_GOOD; };

			if (ImGui::BeginTable("Reading", 2, cardFlags))
			{
				if (reading->hasSize)
				{
					row(feature.IsRound() ? "Diameter" : "Size");
					ImGui::TextColored(judged(issues & (FeatureIssue::UNDERSIZE | FeatureIssue::OVERSIZE | FeatureIssue::BEYOND_LIMITS)),
						"%s", FeatureValue(feature, measured.Size()).c_str());
					if (auto sizeClass = measured.MatchSizeClass(); sizeClass && *sizeClass != 0.0)
					{
						ImGui::SameLine();
						ImGui::TextDisabled("(%s)", SizeClassLabel(*sizeClass).c_str());
					}
					if (feature.IsRound())
					{
						row("Taper");
						ImGui::TextColored(judged(issues & FeatureIssue::TAPER), "%s", Deviation(measured.Taper()).c_str());
						row("Out-of-round");
						ImGui::TextColored(judged(issues & FeatureIssue::OUT_OF_ROUND), "%s", Deviation(measured.OutOfRound()).c_str());
					}
				}
				if (reading->finishRa)
				{
					row("Finish");
					uint32_t finishIssues = measured.Evaluate(true) & (FeatureIssue::ROUGH | FeatureIssue::GLAZED);
					ImGui::TextColored(judged(finishIssues != 0), "%s", Units::FormatFinish(*reading->finishRa).c_str());
					issues |= finishIssues;
				}
				row("Verdict");
				ImGui::TextColored(judged(issues != FeatureIssue::NONE), "%s", DescribeIssues(issues, feature.kind).c_str());
				row("Measured with");
				ImGui::TextColored(valueColor, "%s", reading->toolName.c_str());
				ImGui::EndTable();
			}
		}
		ImGui::EndTable();
	}

	// Round features: every gauge reading, so the taper and out-of-round can be seen where they are.
	if (feature.IsRound() && reading && reading->hasSize)
	{
		if (ImGui::TreeNodeEx("Gauge readings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("Readings", Feature::AXES + 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("Depth");
				for (auto axisName : AXIS_NAMES)
					ImGui::TableSetupColumn(axisName);
				ImGui::TableSetupColumn("Out-of-round");
				HeadersRow({
					"Where along the bore or journal the reading was taken. Top vs. bottom shows taper.",
					"Across the thrust axis, where the piston pushes hardest.",
					"Along the pin axis, at right angles to thrust.",
					"Thrust minus pin at this depth." });

				for (int station = 0; station < Feature::STATIONS; station++)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextDisabled("%s", STATION_NAMES[station]);
					for (int axis = 0; axis < Feature::AXES; axis++)
					{
						ImGui::TableSetColumnIndex(axis + 1);
						ImGui::TextUnformatted(Size(reading->samples[station][axis]).c_str());
					}
					ImGui::TableSetColumnIndex(Feature::AXES + 1);
					ImGui::TextDisabled("%s", Deviation(std::abs(reading->samples[station][0] - reading->samples[station][1])).c_str());
				}
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}
	}

	if (m_xray)
	{
		std::string truth = FeatureValue(feature, feature.Size());
		if (feature.IsRound())
			truth += std::format("   taper {}   out-of-round {}", Deviation(feature.Taper()), Deviation(feature.OutOfRound()));
		if (feature.HasFinishSpec())
			truth += std::format("   {:.2f} um Ra", feature.surfaceRa);
		ImGui::TextColored(COLOR_XRAY, "X-ray: %s", truth.c_str());
	}

	if (part.GetType() == PartType::PISTON_RING)
		ImGui::TextWrapped("A ring's gap depends on the bore it lives in. Square it in its cylinder on the Engine Stand and check the gap with feelers, then set it up in the ring filer to open the gap.");
	if (feature.name == FeatureNames::SKIRT)
		ImGui::TextWrapped("Piston-to-wall clearance for this alloy: %s (cold). Subtract this skirt from the measured bore.",
			RangeText(workshop.GetSpec().PistonToWallFor(part.GetMaterial())).c_str());
}

void WorkshopUI::DrawFixAdvice(Workshop& workshop, const Part& part, const Feature& feature)
{
	ImGui::SeparatorText("Bringing it back into spec");
	ImGui::PushTextWrapPos(0.f);

	auto step = [](const std::string& text) { ImGui::Bullet(); ImGui::TextUnformatted(text.c_str()); };
	// Which machine does an operation, and whether the shop has one yet.
	auto machineFor = [&](MachineOperation operation) {
		const auto& machines = MachineCatalog::All();
		std::string text;
		for (size_t i = 0; i < machines.size(); i++)
		{
			if (machines[i].operation != operation)
				continue;
			if (!text.empty())
				text += " or ";
			text += workshop.OwnsMachine(i) ? std::format("the {} (yours)", machines[i].name)
				: std::format("the {} (${:.0f} at the parts computer)", machines[i].name, machines[i].price);
		}
		return text.empty() ? std::string("a machine shop") : text;
	};
	auto replace = [&](const std::string& why) {
		step(std::format("{} There's no machine for it here: replace the {} (Parts at the parts computer).", why, PartTypeName(part.GetType())));
	};

	// Only ever what the player has measured.
	auto reading = part.GetReading(feature.name);
	bool measured = reading && !reading->stale && (reading->hasSize || reading->finishRa);
	if (!feature.directlyMeasurable)
	{
		if (part.GetType() == PartType::PISTON_RING)
		{
			step("Fit the ring in its bore on the engine stand and check the gap with feelers.");
			step(std::format("Too tight: set the ring up in {} and open the gap a little at a time, checking as you go.", machineFor(MachineOperation::FILE_RING)));
			step("Too wide: filing only opens it. Fit a new ring, or the next oversize ring with an overbored cylinder.");
		}
		else
			step("This one only shows up once the part is on the engine (installed height, spring rate): check it on the stand.");
		ImGui::PopTextWrapPos();
		return;
	}
	if (!measured)
	{
		ImGui::TextDisabled(reading && reading->stale ? "Your reading is out of date: measure it again to see what it needs."
			: "Measure it first: the advice comes from your own readings.");
		ImGui::PopTextWrapPos();
		return;
	}

	auto found = FromReading(feature, *reading);
	uint32_t issues = reading->hasSize ? found.Evaluate(false) : FeatureIssue::NONE;
	if (reading->finishRa)
		issues |= found.Evaluate(true) & (FeatureIssue::ROUGH | FeatureIssue::GLAZED);
	if (issues == FeatureIssue::NONE)
	{
		ImGui::TextColored(COLOR_GOOD, "In spec by your reading: nothing to do.");
		if (auto sizeClass = found.MatchSizeClass(); sizeClass && *sizeClass != 0.0)
			step(std::format("It's at the {} size class, so it needs matching {} parts.", SizeClassLabel(*sizeClass),
				part.GetType() == PartType::BLOCK ? "oversize pistons and rings" : part.GetType() == PartType::CRANKSHAFT ? "undersize bearings"
				: part.GetType() == PartType::CYLINDER_HEAD ? "oversize-stem valves" : ""));
		ImGui::PopTextWrapPos();
		return;
	}

	bool sizeWrong = issues & (FeatureIssue::OVERSIZE | FeatureIssue::UNDERSIZE | FeatureIssue::TAPER | FeatureIssue::OUT_OF_ROUND | FeatureIssue::BEYOND_LIMITS);
	bool finishWrong = issues & (FeatureIssue::ROUGH | FeatureIssue::GLAZED);
	auto supports = [&](MachineOperation operation) { return feature.SupportsOperation(operation); };

	// The size class to machine to: the first one that cleans up the worst of the wear, leaving a
	// little stock for the finishing pass.
	auto cleanupClass = [&](bool growing) -> std::optional<double> {
		constexpr double FINISH_STOCK = 0.02;
		for (double sizeClass : feature.sizeClasses)
		{
			if (growing && feature.spec.Min() + sizeClass >= found.MaxSample() + FINISH_STOCK)
				return sizeClass;
			if (!growing && feature.spec.Max() + sizeClass <= found.MinSample() - FINISH_STOCK)
				return sizeClass;
		}
		return std::nullopt;
	};

	if (feature.kind == FeatureKind::BORE && supports(MachineOperation::BORE))
	{
		// A cylinder bore.
		if (sizeWrong)
		{
			if (auto sizeClass = cleanupClass(true))
			{
				step(std::format("Bore it out to the {} oversize ({}) with {}.", SizeClassLabel(*sizeClass),
					Size(feature.spec.Mid() + *sizeClass), machineFor(MachineOperation::BORE)));
				step(std::format("Then hone it to the finish ({:.2f} - {:.2f} um Ra) with {}.", feature.minRa, feature.maxRa, machineFor(MachineOperation::HONE)));
				step(std::format("Fit a {} piston and top ring to it.", SizeClassLabel(*sizeClass)));
			}
			else
				step("It's worn past the last oversize: the block needs sleeving at a machine shop, or replacing.");
		}
		else if (finishWrong)
			step(std::format("The size is fine but the finish isn't: hone it to {:.2f} - {:.2f} um Ra with {} to break the glaze and lay down a crosshatch.",
				feature.minRa, feature.maxRa, machineFor(MachineOperation::HONE)));
	}
	else if (feature.kind == FeatureKind::BORE && supports(MachineOperation::REAM))
	{
		// A valve guide.
		if (auto sizeClass = cleanupClass(true); sizeClass && sizeWrong)
		{
			step(std::format("Ream it to the {} oversize with {}.", SizeClassLabel(*sizeClass), machineFor(MachineOperation::REAM)));
			step(std::format("Fit a {} oversize-stem valve to it.", SizeClassLabel(*sizeClass)));
		}
		else
			step("It's worn past the largest oversize: the guide needs replacing at a machine shop, or a new head.");
	}
	else if (feature.kind == FeatureKind::JOURNAL && supports(MachineOperation::GRIND))
	{
		// A crank journal.
		if (sizeWrong)
		{
			if (auto sizeClass = cleanupClass(false))
			{
				step(std::format("Grind it down to the {} undersize ({}) with {}.", SizeClassLabel(*sizeClass),
					Size(feature.spec.Mid() + *sizeClass), machineFor(MachineOperation::GRIND)));
				step(std::format("Polish it afterwards with {}.", machineFor(MachineOperation::POLISH)));
				step(std::format("Fit {} bearings to it (thicker shells make up the difference).", SizeClassLabel(*sizeClass)));
			}
			else
				step("It's worn past the last undersize: the crank needs replacing.");
		}
		else if (finishWrong)
			step(std::format("Polish it with {}. If the scoring's deep, polishing won't take it out: grind it to the next undersize instead.",
				machineFor(MachineOperation::POLISH)));
	}
	else if (feature.kind == FeatureKind::JOURNAL && supports(MachineOperation::POLISH))
	{
		// A cam journal: it can be polished, not ground.
		if (finishWrong && !sizeWrong)
			step(std::format("Polish it with {}.", machineFor(MachineOperation::POLISH)));
		else
			replace("Worn undersize.");
	}
	else if (feature.kind == FeatureKind::FLATNESS && supports(MachineOperation::SURFACE))
	{
		step(std::format("Skim it flat with {}. Take off at least your reading ({}) so the cut cleans it up.", machineFor(MachineOperation::SURFACE),
			Deviation(found.Size())));
		if (part.GetType() == PartType::BLOCK)
			step("Decking the block lowers the deck height: the pistons sit closer to the head, so recheck deck clearance and quench.");
		else if (part.GetType() == PartType::CYLINDER_HEAD)
			step("Skimming the head shrinks the chambers and raises compression: CC them again afterwards.");
	}
	else if (feature.kind == FeatureKind::RUNOUT && supports(MachineOperation::CUT_SEAT))
		step(std::format("Recut the seat true with {}. Each cut sinks the seat a little, which raises the installed height.", machineFor(MachineOperation::CUT_SEAT)));
	else if (feature.kind == FeatureKind::RUNOUT && supports(MachineOperation::FACE_VALVE))
		step(std::format("Reface the valve with {}. It takes off margin, so check the margin is still thick enough afterwards.", machineFor(MachineOperation::FACE_VALVE)));
	else if (supports(MachineOperation::CUT_SEAT))
		step(std::format("Cut the seat to width with {}: a top cut narrows it, a bottom cut moves it down.", machineFor(MachineOperation::CUT_SEAT)));
	else if (feature.name == FeatureNames::MARGIN)
		replace("The margin can't be built back up.");
	else if (feature.name == FeatureNames::WALL_THICKNESS)
		replace("Bearing shells are never machined.");
	else if (feature.name == FeatureNames::FREE_LENGTH || feature.name == FeatureNames::SEAT_PRESSURE)
		replace("A tired spring has sagged.");
	else if (feature.kind == FeatureKind::BORE && supports(MachineOperation::LINE_BORE))
	{
		// A main housing: the whole line is done together.
		if (found.MaxSample() > feature.spec.Max() + 0.05)
			step("It's too far oversize to bring back by line boring: the block needs oversize-OD main bearings at a machine shop, or replacing.");
		else
		{
			step(std::format("Line bore the block with {}: it grinds the main caps and bores all {} housings back to {}, round and in line, in one setup.",
				machineFor(MachineOperation::LINE_BORE), workshop.GetSpec().mainJournals, Size(feature.spec.Mid())));
			step("Measure every housing again afterwards, then Plastigauge the mains once the crank and caps are in.");
		}
	}
	else
		replace("Wear like this can't be machined back.");

	ImGui::PopTextWrapPos();
}

void WorkshopUI::DrawMachiningPanel(Workshop& workshop, Part& part, const Feature& feature)
{
	if (feature.operations.empty())
		return;

	// Cutting only happens with the part set up in a machine, and only that machine can work on it.
	const auto& machines = MachineCatalog::All();
	ImGui::Spacing();
	ImGui::SeparatorText("Machining");

	if (m_selectedPartMachine < 0 || m_selectedPartMachine >= static_cast<int>(machines.size()))
	{
		std::string machinesFor;
		for (const auto& candidate : machines)
			if (Machining::CanMachine(candidate, feature))
				machinesFor += (machinesFor.empty() ? "" : ", ") + candidate.name;
		ImGui::PushTextWrapPos(0.f);
		if (machinesFor.empty())
			ImGui::TextDisabled("Set this part up in a machine to cut it.");
		else
			ImGui::TextDisabled("Set this part up in a machine to cut it (%s).", machinesFor.c_str());
		ImGui::PopTextWrapPos();
		return;
	}

	const auto& machine = machines[m_selectedPartMachine];
	if (!Machining::CanMachine(machine, feature))
	{
		ImGui::TextDisabled("The %s can't work on this feature.", machine.name.c_str());
		return;
	}
	if (m_selectedMachine != m_selectedPartMachine)
	{
		m_selectedMachine = m_selectedPartMachine;
		m_machineParameter = machine.operation == MachineOperation::SURFACE ? Units::ToDisplay(0.05, m_units) : 0.0;
	}

	if (Machining::UsesParameter(machine.operation))
	{
		ImGui::SetNextItemWidth(180.f);
		const char* format = m_units == Units::System::IMPERIAL ? "%.5f" : "%.4f";
		ImGui::InputDouble(Machining::ParameterName(machine.operation), &m_machineParameter, 0.0, 0.0, format);
		ImGui::SameLine();
		ImGui::TextDisabled(m_units == Units::System::IMPERIAL ? "in" : "mm");

		// Quick targets: hone/grind to the middle of a size class; bore leaves stock for the hone.
		if (machine.operation == MachineOperation::BORE || machine.operation == MachineOperation::HONE || machine.operation == MachineOperation::GRIND)
		{
			constexpr double HONE_STOCK = 0.06;
			for (double sizeClass : feature.sizeClasses)
			{
				double target = feature.spec.Mid() + sizeClass - (machine.operation == MachineOperation::BORE ? HONE_STOCK : 0.0);
				ImGui::SameLine();
				if (ImGui::SmallButton(SizeClassLabel(sizeClass).c_str()))
					m_machineParameter = Units::ToDisplay(target, m_units);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(machine.operation == MachineOperation::BORE ? "%s, leaving 0.06 mm for the hone" : "%s", Size(target).c_str());
			}
		}
	}

	// Hones charge time by the stock taken off, so estimate it from the player's own reading.
	if (machine.hoursPerMm > 0.0)
	{
		auto reading = part.GetReading(feature.name);
		if (reading && reading->hasSize && !reading->stale)
		{
			double stock = Units::FromDisplay(m_machineParameter, m_units) - FromReading(feature, *reading).Size();
			if (stock > 0.0)
				ImGui::Text("About %s off: roughly %.1f h", Deviation(stock).c_str(), machine.hoursPerUse + stock * machine.hoursPerMm);
			else
				ImGui::TextDisabled("That target is smaller than your last reading.");
		}
		else
			ImGui::TextDisabled("Measure it first to estimate how long this will take.");
	}

	if (machine.operation == MachineOperation::LINE_BORE)
	{
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("Does all %d main housings at once, whichever one is open: the caps are ground and the housings bored back to "
			"%s, round and in line. A housing more than %s oversize can't be brought back.", workshop.GetSpec().mainJournals,
			Size(workshop.GetSpec().mainHousing.Mid()).c_str(), Deviation(machine.maxRemoval).c_str());
		ImGui::PopTextWrapPos();
	}
	ImGui::TextColored(COLOR_WARN, "Material only comes off. This can't be undone.");
	std::string timeText = machine.hoursPerMm > 0.0 ? std::format("{:.1f} h + {:.0f} h/mm", machine.hoursPerUse, machine.hoursPerMm) : std::format("{:.1f} h", machine.hoursPerUse);
	if (ImGui::Button(std::format("{} ({})", Machining::OperationName(machine.operation), timeText).c_str()))
	{
		auto result = workshop.MachineFeature(m_selectedMachine, part, feature.name, Units::FromDisplay(m_machineParameter, m_units));
		m_lastMachiningMessage = result.message;
	}
	if (!m_lastMachiningMessage.empty())
		ImGui::TextWrapped("%s", m_lastMachiningMessage.c_str());
}

void WorkshopUI::DrawPartsStore(Workshop& workshop, bool hardware)
{
	// Each tab keeps to its own kind: switching tabs picks the first of that kind.
	auto belongs = [&](PartType type) { return (PartCatalog::Get(type).system == PartSystem::FASTENERS) == hardware; };
	if (!belongs(m_order.type))
		for (auto type : PartCatalog::All())
			if (belongs(type) && PartCatalog::Get(type).listed)
			{
				m_order = {};
				m_order.type = type;
				if (!Workshop::SoldUsed(type))
					m_order.condition = PartCondition::NEW;
				break;
			}

	// Catalogue cards on the left, the order form for the selected part on the right.
	constexpr float CARD_WIDTH = 118.f;
	constexpr float CARD_HEIGHT = 112.f;
	constexpr float ICON_SIZE = 64.f;

	if (ImGui::BeginChild("Catalogue", { ImGui::GetContentRegionAvail().x * 0.55f, 0.f }, ImGuiChildFlags_Borders))
	{
		if (hardware)
		{
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("Nuts and bolts by size: any of a size fits every hole of that size. They go in finger tight; do them up "
				"with a socket on a ratchet (or a wrench) of their size, and to spec with a torque wrench.");
			ImGui::PopTextWrapPos();
		}
		else
			ImGui::TextDisabled("Parts ship to the roll-up door. Budget parts scatter wider; measure everything.");
		ImGui::Spacing();

		float available = ImGui::GetContentRegionAvail().x;
		int columns = (std::max)(1, static_cast<int>(available / (CARD_WIDTH + ImGui::GetStyle().ItemSpacing.x)));
		int column = 0;
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// One section per engine system.
		std::optional<PartSystem> section;
		for (auto type : PartCatalog::All())
		{
			if (!belongs(type))
				continue;
			// The other halves of a set come with it.
			if (!PartCatalog::Get(type).listed)
				continue;
			auto system = PartCatalog::Get(type).system;
			if (system != section)
			{
				if (column != 0)
					ImGui::NewLine();
				column = 0;
				ImGui::SeparatorText(PartSystemName(system));
				section = system;
			}
			ImGui::PushID(static_cast<int>(type));
			ImVec2 cardMin = ImGui::GetCursorScreenPos();
			bool selected = m_order.type == type;
			if (ImGui::InvisibleButton("card", { CARD_WIDTH, CARD_HEIGHT }))
			{
				m_order.type = type;
				m_order.sizeOffset = 0.0;
				m_order.quantity = 1;
				if (!Workshop::SoldUsed(type))
					m_order.condition = PartCondition::NEW;
			}
			bool hovered = ImGui::IsItemHovered();
			ImVec2 cardMax = { cardMin.x + CARD_WIDTH, cardMin.y + CARD_HEIGHT };

			ImU32 background = selected ? IM_COL32(60, 85, 120, 255) : hovered ? IM_COL32(55, 58, 66, 255) : IM_COL32(40, 42, 48, 255);
			drawList->AddRectFilled(cardMin, cardMax, background, 6.f);
			if (selected)
				drawList->AddRect(cardMin, cardMax, IM_COL32(120, 170, 230, 255), 6.f, 0, 2.f);

			PartIcons::Draw(drawList, type, { cardMin.x + (CARD_WIDTH - ICON_SIZE) * 0.5f, cardMin.y + 8.f }, ICON_SIZE);

			// Long names are clipped to the card, with the full name on hover.
			const char* name = PartCatalog::StoreName(type);
			ImVec2 textSize = ImGui::CalcTextSize(name);
			drawList->PushClipRect(cardMin, cardMax, true);
			drawList->AddText({ cardMin.x + (std::max)(4.f, (CARD_WIDTH - textSize.x) * 0.5f), cardMin.y + ICON_SIZE + 14.f }, IM_COL32(230, 230, 230, 255), name);
			drawList->PopClipRect();
			if (hovered && textSize.x > CARD_WIDTH - 8.f)
				ImGui::SetTooltip("%s", name);
			double cheapest = PartFactory::PriceWithCompanions(type, PartQuality::BUDGET,
				Workshop::SoldUsed(type) ? PartCondition::USED : PartCondition::NEW, workshop.GetSpec());
			// Cents for the small stuff, so a bolt isn't "from $0".
			std::string from = cheapest < 10.0 ? std::format("from ${:.2f}", cheapest) : std::format("from ${:.0f}", cheapest);
			ImVec2 fromSize = ImGui::CalcTextSize(from.c_str());
			drawList->AddText({ cardMin.x + (CARD_WIDTH - fromSize.x) * 0.5f, cardMin.y + ICON_SIZE + 30.f }, IM_COL32(140, 200, 140, 255), from.c_str());

			if (++column < columns)
				ImGui::SameLine();
			else
				column = 0;
			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	if (!ImGui::BeginChild("Order", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}

	ImGui::Text("%s", PartCatalog::StoreName(m_order.type));
	DrawPreview(PartTypeName(m_order.type));
	ImGui::PushTextWrapPos(0.f);
	ImGui::TextDisabled("%s", PartIcons::Description(m_order.type));
	ImGui::PopTextWrapPos();
	ImGui::Separator();

	// Each quality with what one costs; hovering one (or picking it) says what it gets you for this part.
	int quality = static_cast<int>(m_order.quality);
	for (auto option : { PartQuality::BUDGET, PartQuality::OEM, PartQuality::PERFORMANCE })
	{
		if (option != PartQuality::BUDGET)
			ImGui::SameLine();
		double each = PartFactory::PriceWithCompanions(m_order.type, option, m_order.condition, workshop.GetSpec());
		std::string label = std::format("{} ({})", PartQualityName(option), each < 10.0 ? std::format("${:.2f}", each) : std::format("${:.0f}", each));
		ImGui::RadioButton(label.c_str(), &quality, static_cast<int>(option));
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
			for (const auto& note : PartFactory::QualityNotes(m_order.type, option))
				ImGui::BulletText("%s", note.c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}
	m_order.quality = static_cast<PartQuality>(quality);
	ImGui::PushTextWrapPos(0.f);
	for (const auto& note : PartFactory::QualityNotes(m_order.type, m_order.quality))
		ImGui::BulletText("%s", note.c_str());
	ImGui::PopTextWrapPos();

	if (Workshop::SoldUsed(m_order.type))
	{
		int condition = static_cast<int>(m_order.condition);
		ImGui::RadioButton("New", &condition, static_cast<int>(PartCondition::NEW));
		ImGui::SameLine();
		ImGui::RadioButton("Used core", &condition, static_cast<int>(PartCondition::USED));
		m_order.condition = static_cast<PartCondition>(condition);
	}

	auto sizes = Workshop::SizeOptions(m_order.type);
	if (sizes.size() > 1)
	{
		ImGui::SetNextItemWidth(220.f);
		if (ImGui::BeginCombo("Size", SizeClassLabel(m_order.sizeOffset).c_str()))
		{
			for (double size : sizes)
				if (ImGui::Selectable(SizeClassLabel(size).c_str(), m_order.sizeOffset == size))
					m_order.sizeOffset = size;
			ImGui::EndCombo();
		}
		ImGui::PushTextWrapPos(0.f);
		if (m_order.type == PartType::PISTON || m_order.type == PartType::PISTON_RING)
			ImGui::TextDisabled("Oversize parts suit cylinders bored to the matching size class.");
		else if (m_order.type == PartType::INTAKE_VALVE || m_order.type == PartType::EXHAUST_VALVE)
			ImGui::TextDisabled("Oversize stems suit guides reamed to the matching size class.");
		else
			ImGui::TextDisabled("Undersize bearings have thicker walls to suit journals ground to that size class.");
		ImGui::PopTextWrapPos();
	}

	ImGui::SetNextItemWidth(220.f);
	ImGui::InputInt("Quantity", &m_order.quantity);
	m_order.quantity = std::clamp(m_order.quantity, 1, 16);

	ImGui::PushTextWrapPos(0.f);
	if (m_order.type == PartType::PISTON_RING && m_order.quality == PartQuality::PERFORMANCE)
		ImGui::TextColored(COLOR_WARN, "Performance rings are file-fit: they arrive tight and must be gapped by hand.");
	if (m_order.type == PartType::PISTON && m_order.quality == PartQuality::PERFORMANCE)
		ImGui::TextColored(COLOR_WARN, "Forged 2618 pistons grow more when hot and need more cold clearance.");
	if (m_order.type == PartType::CAMSHAFT && m_order.quality == PartQuality::PERFORMANCE)
		ImGui::TextColored(COLOR_WARN, "Performance cam: more lift. Pair it with springs that can keep up.");
	if (m_order.type == PartType::VALVE_SPRING && m_order.quality == PartQuality::PERFORMANCE)
		ImGui::TextColored(COLOR_WARN, "Stiffer springs: higher seat pressure, so check installed height.");
	ImGui::PopTextWrapPos();

	ImGui::Spacing();
	CartLine line;
	line.kind = CartLine::Kind::PART;
	line.order = m_order;
	double quote = workshop.Quote(m_order);
	AddToCartButton(workshop, line, (quote < 10.0 ? std::format("Add to cart (${:.2f})", quote) : std::format("Add to cart (${:.0f})", quote)).c_str());

	ImGui::EndChild();
}

void WorkshopUI::DrawStoreTab(Workshop& workshop)
{
	if (!ImGui::BeginTabBar("StoreCategories"))
		return;

	auto category = [&](const char* label, StoreCategory which) {
		ImGuiTabItemFlags flags = m_requestedCategory == which ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
		return ImGui::BeginTabItem(label, nullptr, flags);
	};

	if (category("Parts", StoreCategory::PARTS))
	{
		DrawPartsStore(workshop, false);
		ImGui::EndTabItem();
	}
	if (category("Hardware", StoreCategory::HARDWARE))
	{
		DrawPartsStore(workshop, true);
		ImGui::EndTabItem();
	}
	if (category("Tools", StoreCategory::TOOLS))
	{
		DrawToolStore(workshop);
		ImGui::EndTabItem();
	}
	if (category("Machines", StoreCategory::MACHINES))
	{
		DrawMachineStore(workshop);
		ImGui::EndTabItem();
	}
	if (category("Furniture", StoreCategory::FURNITURE))
	{
		DrawFurnitureStore(workshop);
		ImGui::EndTabItem();
	}
	if (category("Upgrades", StoreCategory::UPGRADES))
	{
		DrawUpgradeStore(workshop);
		ImGui::EndTabItem();
	}
	m_requestedCategory = std::nullopt;
	ImGui::EndTabBar();
}

void WorkshopUI::DrawStoreWindow(Workshop& workshop)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize({ 1150.f, 760.f }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });
	if (m_requestedCategory)
	{
		ImGui::SetNextWindowFocus();
		ImGui::SetNextWindowCollapsed(false);
	}

	if (ImGui::Begin("Parts Computer", &m_storeOpen))
	{
		ImGui::TextColored(COLOR_GOOD, "$%.0f", workshop.GetMoney());
		ImGui::SameLine(0.f, 30.f);
		ImGui::TextDisabled("Everything is delivered to the roll-up door: open it with E. Machines and stands are set up when their crate is opened.");
		ImGui::SameLine(0.f, 30.f);
		ImGui::TextDisabled("Tab / Esc: close");
		ImGui::Separator();

		// The catalogue on the left, the cart down the right.
		float logHeight = ImGui::GetTextLineHeightWithSpacing() * 6.f;
		constexpr float CART_WIDTH = 330.f;
		if (ImGui::BeginChild("StoreMain", { -CART_WIDTH - ImGui::GetStyle().ItemSpacing.x, -logHeight }))
			DrawStoreTab(workshop);
		ImGui::EndChild();
		ImGui::SameLine();
		if (ImGui::BeginChild("Cart", { 0.f, -logHeight }, ImGuiChildFlags_Borders))
			DrawCart(workshop);
		ImGui::EndChild();
		DrawLog(workshop);
	}
	ImGui::End();
}

void WorkshopUI::SetModelLookup(const std::function<uint32_t(const std::string&)>& lookup)
{
	m_modelLookup = lookup;
}

void WorkshopUI::DrawPreview(const std::string& modelName)
{
	// A turntable view of the item, as wide as the panel.
	uint32_t model = m_modelLookup ? m_modelLookup(modelName) : 0;
	if (!model)
		return;
	PROFILE_SCOPE("ui: model preview");
	float width = ImGui::GetContentRegionAvail().x;
	ModelPreview::Draw(model, width, (std::min)(width * 0.6f, 220.f));
}

std::string WorkshopUI::CartLineName(const CartLine& line) const
{
	switch (line.kind)
	{
		case CartLine::Kind::PART:
		{
			std::string name = std::format("{} {}", line.order.condition == PartCondition::USED ? "Used" : PartQualityName(line.order.quality),
				PartCatalog::StoreName(line.order.type));
			if (Workshop::SizeOptions(line.order.type).size() > 1)
				name += " " + SizeClassLabel(line.order.sizeOffset);
			return name;
		}
		case CartLine::Kind::TOOL:			return ToolCatalog::All()[line.index].name;
		case CartLine::Kind::MACHINE:		return MachineCatalog::All()[line.index].name;
		case CartLine::Kind::ENGINE_STAND:	return "Engine stand";
		case CartLine::Kind::FURNITURE:		return FurnitureCatalog::All()[line.index].name;
		case CartLine::Kind::UPGRADE:		return UpgradeCatalog::All()[line.index].name;
	}
	return "";
}

int WorkshopUI::InCart(CartLine::Kind kind, size_t index) const
{
	int count = 0;
	for (const auto& line : m_cart)
		if (line.kind == kind && (kind == CartLine::Kind::ENGINE_STAND || line.index == index))
			count += kind == CartLine::Kind::PART ? line.order.quantity : line.quantity;
	return count;
}

void WorkshopUI::AddToCart(const CartLine& line)
{
	for (auto& existing : m_cart)
	{
		if (existing.kind != line.kind)
			continue;
		if (line.kind == CartLine::Kind::PART && existing.order.type == line.order.type && existing.order.quality == line.order.quality
			&& existing.order.condition == line.order.condition && existing.order.sizeOffset == line.order.sizeOffset)
		{
			existing.order.quantity = (std::min)(existing.order.quantity + line.order.quantity, 99);
			return;
		}
		if (line.kind == CartLine::Kind::ENGINE_STAND || (line.kind == CartLine::Kind::FURNITURE && existing.index == line.index))
		{
			existing.quantity += line.quantity;
			return;
		}
	}
	m_cart.push_back(line);
}

void WorkshopUI::AddToCartButton(Workshop& workshop, const CartLine& line, const char* label)
{
	// One of a kind (a tool, a machine) can only be in the cart once; stands only up to the room left.
	std::optional<std::string> problem;
	if ((line.kind == CartLine::Kind::TOOL || line.kind == CartLine::Kind::MACHINE || line.kind == CartLine::Kind::UPGRADE) && InCart(line.kind, line.index) > 0)
		problem = "In the cart.";
	else
	{
		auto withIt = m_cart;
		withIt.push_back(line);
		problem = workshop.CartLineProblem(line, withIt);
	}

	ImGui::BeginDisabled(problem.has_value());
	if (ImGui::Button(label, { 280.f, 34.f }))
	{
		AddToCart(line);
		m_cartMessage.clear();
	}
	ImGui::EndDisabled();
	if (problem)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("%s", problem->c_str());
	}
}

void WorkshopUI::DrawCart(Workshop& workshop)
{
	ImGui::SeparatorText(std::format("Cart ({})", m_cart.size()).c_str());
	if (m_cart.empty())
	{
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("Empty. Add parts, tools, machines and engine stands from the catalogue, then check out once: "
			"the order arrives together, with the parts boxed by system and each machine in its own crate.");
		ImGui::PopTextWrapPos();
	}

	std::optional<size_t> remove;
	for (size_t i = 0; i < m_cart.size(); i++)
	{
		auto& line = m_cart[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::TextUnformatted(CartLineName(line).c_str());
		ImGui::TextColored(COLOR_GOOD, "$%.0f", workshop.LineTotal(line));

		// Parts, stands and cores come in quantities; tools and machines are one each.
		int* quantity = line.kind == CartLine::Kind::PART ? &line.order.quantity
			: line.kind == CartLine::Kind::ENGINE_STAND || line.kind == CartLine::Kind::FURNITURE ? &line.quantity : nullptr;
		if (quantity)
		{
			ImGui::SameLine(110.f);
			if (ImGui::SmallButton("-") && *quantity > 1)
				(*quantity)--;
			ImGui::SameLine();
			ImGui::Text("%d", *quantity);
			ImGui::SameLine();
			if (ImGui::SmallButton("+") && *quantity < 99)
				(*quantity)++;
		}
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.f);
		if (ImGui::SmallButton("Remove"))
			remove = i;
		if (auto problem = workshop.CartLineProblem(line, m_cart))
			ImGui::TextColored(COLOR_BAD, "%s", problem->c_str());
		ImGui::Separator();
		ImGui::PopID();
	}
	if (remove)
		m_cart.erase(m_cart.begin() + *remove);

	double total = workshop.CartTotal(m_cart);
	ImGui::Spacing();
	ImGui::Text("Total");
	ImGui::SameLine(110.f);
	ImGui::TextColored(total > workshop.GetMoney() ? COLOR_BAD : COLOR_GOOD, "$%.0f", total);
	ImGui::Text("After");
	ImGui::SameLine(110.f);
	ImGui::TextDisabled("$%.0f", workshop.GetMoney() - total);

	bool problems = std::any_of(m_cart.begin(), m_cart.end(), [&](const CartLine& line) { return workshop.CartLineProblem(line, m_cart).has_value(); });
	ImGui::Spacing();
	ImGui::BeginDisabled(m_cart.empty() || total > workshop.GetMoney() || problems);
	if (ImGui::Button("Check out", { -1.f, 36.f }))
	{
		if (workshop.Checkout(m_cart))
		{
			m_cartMessage = std::format("Ordered {} line{} for ${:.0f}. It's on its way to the roll-up door.", m_cart.size(), m_cart.size() == 1 ? "" : "s", total);
			m_cart.clear();
		}
	}
	ImGui::EndDisabled();
	if (!m_cart.empty() && total > workshop.GetMoney())
		ImGui::TextColored(COLOR_BAD, "Not enough money for all of it.");
	ImGui::BeginDisabled(m_cart.empty());
	if (ImGui::Button("Empty the cart", { -1.f, 0.f }))
		m_cart.clear();
	ImGui::EndDisabled();
	if (!m_cartMessage.empty())
	{
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextColored(COLOR_GOOD, "%s", m_cartMessage.c_str());
		ImGui::PopTextWrapPos();
	}
}

void WorkshopUI::DrawEnginePicker(Workshop& workshop)
{
	int engines = workshop.GetEngineCount();
	if (engines <= 1)
		return;

	// Switching engines mid-pull would swap the sheet out from under the run.
	ImGui::BeginDisabled(m_pullActive);
	int active = workshop.GetActiveEngine();
	ImGui::TextUnformatted("Engine:");
	for (int engine = 0; engine < engines; engine++)
	{
		ImGui::SameLine();
		if (ImGui::RadioButton(std::format("{}##Engine{}", engine + 1, engine).c_str(), active == engine))
			workshop.SetActiveEngine(engine);
	}
	ImGui::EndDisabled();
	ImGui::SameLine(0.f, 20.f);
	ImGui::TextDisabled("(on engine stand %d; E at a stand also picks its engine)", workshop.GetActiveEngine() + 1);
	ImGui::Separator();
}

bool WorkshopUI::StoreCard(const char* id, const char* name, const std::string& price, bool selected, bool owned,
	const std::function<void(ImDrawList*, ImVec2, float)>& drawIcon, const std::vector<float>& ratings)
{
	constexpr float CARD_WIDTH = 132.f;
	constexpr float CARD_HEIGHT = 132.f;
	constexpr float ICON_SIZE = 62.f;

	ImGui::PushID(id);
	ImVec2 cardMin = ImGui::GetCursorScreenPos();
	bool clicked = ImGui::InvisibleButton("card", { CARD_WIDTH, CARD_HEIGHT });
	bool hovered = ImGui::IsItemHovered();
	ImVec2 cardMax = { cardMin.x + CARD_WIDTH, cardMin.y + CARD_HEIGHT };
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	ImU32 background = selected ? IM_COL32(60, 85, 120, 255) : hovered ? IM_COL32(55, 58, 66, 255) : IM_COL32(40, 42, 48, 255);
	drawList->AddRectFilled(cardMin, cardMax, background, 6.f);
	if (selected)
		drawList->AddRect(cardMin, cardMax, IM_COL32(120, 170, 230, 255), 6.f, 0, 2.f);

	drawIcon(drawList, { cardMin.x + (CARD_WIDTH - ICON_SIZE) * 0.5f, cardMin.y + 8.f }, ICON_SIZE);

	// Long names are clipped to the card rather than spilling into the next one.
	ImVec2 textSize = ImGui::CalcTextSize(name);
	float textX = cardMin.x + (std::max)(4.f, (CARD_WIDTH - textSize.x) * 0.5f);
	drawList->PushClipRect(cardMin, cardMax, true);
	drawList->AddText({ textX, cardMin.y + ICON_SIZE + 16.f }, IM_COL32(230, 230, 230, 255), name);
	drawList->PopClipRect();

	std::string footer = owned ? "Owned" : price;
	ImVec2 footerSize = ImGui::CalcTextSize(footer.c_str());
	drawList->AddText({ cardMin.x + (CARD_WIDTH - footerSize.x) * 0.5f, cardMin.y + ICON_SIZE + 34.f },
		owned ? IM_COL32(150, 150, 150, 255) : IM_COL32(140, 200, 140, 255), footer.c_str());

	// Efficiency at a glance: one thin bar per rating along the bottom of the card.
	for (size_t i = 0; i < ratings.size(); i++)
		Ratings::MiniBar(drawList, { cardMin.x + 10.f, cardMax.y - 16.f + i * 6.f - (ratings.size() - 1) * 6.f }, CARD_WIDTH - 20.f, ratings[i]);

	if (hovered && textSize.x > CARD_WIDTH - 8.f)
		ImGui::SetTooltip("%s", name);

	ImGui::PopID();

	// Flow the cards into rows.
	float nextX = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + CARD_WIDTH;
	if (nextX < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
		ImGui::SameLine();
	return clicked;
}

std::string WorkshopUI::ToolPrecision(const MeasuringTool& tool, double value) const
{
	if (tool.measuresFinish)
		return Units::FormatFinish(value);
	if (tool.kinds.size() == 1 && tool.kinds[0] == FeatureKind::VOLUME)
		return Units::FormatVolume(value);
	if (tool.kinds.size() == 1 && tool.kinds[0] == FeatureKind::FORCE)
		return Units::FormatForce(value);
	return Deviation(value);
}

std::string WorkshopUI::ToolHint(Workshop& workshop, int toolIndex) const
{
	(void)workshop;
	if (toolIndex < 0)
		return "no tool owned";
	return ToolCatalog::All()[toolIndex].name;
}

void WorkshopUI::DrawToolStore(Workshop& workshop)
{
	const auto& tools = ToolCatalog::All();
	m_selectedTool = std::clamp(m_selectedTool, 0, static_cast<int>(tools.size()) - 1);

	if (ImGui::BeginChild("ToolCatalogue", { ImGui::GetContentRegionAvail().x * 0.55f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::TextDisabled("Tools are delivered to the roll-up door. Once unpacked, put them away on the tool board or in your backpack.");
		ImGui::Spacing();

		// Grouped by the job they do, cheapest first within each: the measuring tools by what they
		// measure, then the spanners.
		static const char* const SHELVES[] = {
			"Outside sizes: journals, stems, lengths",
			"Bores",
			"Flatness and runout",
			"On the engine: clearances, gaps, heights",
			"Springs, volumes and surface finish",
			"Sockets and drives",
			"Wrenches",
			"Torque",
			"Manuals",
		};
		auto shelfOf = [](const MeasuringTool& tool) {
			if (tool.manual)
				return 8;
			auto measures = [&](FeatureKind kind) { return std::find(tool.kinds.begin(), tool.kinds.end(), kind) != tool.kinds.end(); };
			switch (tool.handKind)
			{
				case HandToolKind::SOCKETS:
				case HandToolKind::RATCHET:
				case HandToolKind::BREAKER_BAR:		return 5;
				case HandToolKind::WRENCHES:		return 6;
				case HandToolKind::TORQUE_WRENCH:	return 7;
				default:							break;
			}
			if (!tool.assemblyChecks.empty())
				return 3;
			if (tool.measuresFinish || measures(FeatureKind::FORCE) || measures(FeatureKind::VOLUME))
				return 4;
			if (measures(FeatureKind::BORE))
				return 1;
			if (measures(FeatureKind::FLATNESS) || measures(FeatureKind::RUNOUT))
				return 2;
			return 0;
		};
		std::vector<size_t> order(tools.size());
		for (size_t i = 0; i < tools.size(); i++)
			order[i] = i;
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			int shelfA = shelfOf(tools[a]), shelfB = shelfOf(tools[b]);
			return shelfA != shelfB ? shelfA < shelfB : tools[a].price < tools[b].price;
		});

		int shelf = -1;
		for (size_t i : order)
		{
			const auto& tool = tools[i];
			if (int thisShelf = shelfOf(tool); thisShelf != shelf)
			{
				if (shelf >= 0)
					ImGui::NewLine();
				ImGui::SeparatorText(SHELVES[thisShelf]);
				shelf = thisShelf;
			}
			if (StoreCard(tool.name.c_str(), tool.name.c_str(), std::format("${:.0f}", tool.price), m_selectedTool == static_cast<int>(i), workshop.OwnsTool(i),
				[&](ImDrawList* drawList, ImVec2 min, float size) { ItemIcons::DrawTool(drawList, tool, min, size); },
				tool.manual ? std::vector<float>{} : std::vector<float>{ Ratings::ToolPrecision(tool), Ratings::ToolSpeed(tool) }))
				m_selectedTool = static_cast<int>(i);
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("ToolDetail", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		const auto& tool = tools[m_selectedTool];
		ImGui::Text("%s", tool.name.c_str());
		DrawPreview(ToolModelName(tool));
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("%s", tool.description.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Separator();

		if (tool.manual)
		{
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("Chapters: general information, tools, teardown, the block, crankshaft, rods, pistons and rings, bearings and "
				"Plastigauge, the head and valves, the camshaft, the assembly sequence, torque specifications, final checks and the dyno, "
				"and troubleshooting. Dimensions are shown in the units you play in.");
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			CartLine line;
			line.kind = CartLine::Kind::TOOL;
			line.index = static_cast<size_t>(m_selectedTool);
			if (workshop.OwnsTool(m_selectedTool))
				ImGui::TextColored(COLOR_GOOD, "Owned.");
			else
				AddToCartButton(workshop, line, std::format("Add to cart (${:.0f})", tool.price).c_str());
			ImGui::EndChild();
			return;
		}
		if (tool.IsHandTool())
		{
			// What it does for the engine's nuts and bolts.
			std::string sizes;
			for (int size : tool.sizes)
				sizes += (sizes.empty() ? "" : ", ") + std::to_string(size);
			if (!sizes.empty())
				ImGui::Text("Sizes: %s mm", sizes.c_str());
			if (tool.handKind == HandToolKind::TORQUE_WRENCH)
			{
				Ratings::Bar("Precision", Ratings::ToolPrecision(tool));
				ImGui::Text("Accurate to about +/- %.0f%%, up to %.0f Nm. Drives a socket.", tool.torqueError * 100.0, tool.maxTorque);
			}
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("Every nut and bolt on the engine needs a socket (driven by a ratchet or a torque wrench) or a wrench of its size, "
				"and a torque wrench to be done up to spec. How close the torque wrench gets "
				"is how tight they really are, and the dyno finds out.");
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			CartLine line;
			line.kind = CartLine::Kind::TOOL;
			line.index = static_cast<size_t>(m_selectedTool);
			if (workshop.OwnsTool(m_selectedTool))
				ImGui::TextColored(COLOR_GOOD, "Owned.");
			else
				AddToCartButton(workshop, line, std::format("Add to cart (${:.0f})", tool.price).c_str());
			ImGui::EndChild();
			return;
		}

		Ratings::Bar("Precision", Ratings::ToolPrecision(tool));
		Ratings::Bar("Speed", Ratings::ToolSpeed(tool));
		ImGui::Spacing();
		ImGui::Text("Reads to   %s", ToolPrecision(tool, tool.resolution).c_str());
		ImGui::Text("Repeats to +/- %s", ToolPrecision(tool, tool.sigma * 2.0).c_str());
		if (tool.minSize > 0.0 || tool.maxSize > 0.0)
			ImGui::Text("Fits bores %s", tool.maxSize > 0.0 ? std::format("up to {:.0f} mm", tool.maxSize).c_str() : std::format("from {:.0f} mm", tool.minSize).c_str());
		ImGui::Text("Time per check %.2f h", tool.hoursPerUse);

		std::string uses;
		for (auto kind : tool.kinds)
		{
			static const char* kindNames[] = { "bores", "journals", "flatness", "lengths", "thicknesses", "volumes", "runout", "spring force" };
			uses += (uses.empty() ? "" : ", ") + std::string(kindNames[static_cast<int>(kind)]);
		}
		if (tool.measuresFinish)
			uses += (uses.empty() ? "" : ", ") + std::string("surface finish");
		for (auto check : tool.assemblyChecks)
		{
			static const char* checkNames[] = { "", "bearing oil clearance", "cam oil clearance", "ring gap", "deck clearance", "installed height" };
			uses += (uses.empty() ? "" : ", ") + std::string(checkNames[static_cast<int>(check)]);
		}
		ImGui::PushTextWrapPos(0.f);
		ImGui::Text("Measures   %s", uses.c_str());
		ImGui::PopTextWrapPos();

		ImGui::Spacing();
		if (workshop.OwnsTool(m_selectedTool))
			ImGui::TextColored(COLOR_GOOD, "Owned. Unpack it at the door if it hasn't been yet.");
		else
		{
			CartLine line;
			line.kind = CartLine::Kind::TOOL;
			line.index = static_cast<size_t>(m_selectedTool);
			AddToCartButton(workshop, line, std::format("Add to cart (${:.0f})", tool.price).c_str());
		}
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawMachineStore(Workshop& workshop)
{
	const auto& machines = MachineCatalog::All();
	m_selectedStoreMachine = std::clamp(m_selectedStoreMachine, 0, static_cast<int>(machines.size()) - 1);

	if (ImGui::BeginChild("MachineCatalogue", { ImGui::GetContentRegionAvail().x * 0.55f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::TextDisabled("Machines are delivered in a crate and set up when it's opened. Press F on one to move it.");
		ImGui::Spacing();
		// Grouped by the job they do, cheapest first within each, so the budget way and the proper
		// way to do a job sit side by side.
		auto group = [](MachineOperation operation) {
			switch (operation)
			{
				case MachineOperation::BORE:
				case MachineOperation::HONE:		return 0;
				case MachineOperation::SURFACE:		return 1;
				case MachineOperation::LINE_BORE:	return 2;
				case MachineOperation::GRIND:
				case MachineOperation::POLISH:		return 3;
				case MachineOperation::REAM:
				case MachineOperation::CUT_SEAT:
				case MachineOperation::FACE_VALVE:	return 4;
				case MachineOperation::FILE_RING:	return 5;
			}
			return 6;
		};
		constexpr const char* GROUP_NAMES[] = { "Cylinder bores", "Block and head decks", "Main bearing housings", "Crankshaft",
			"Cylinder head and valves", "Piston rings", "Other" };
		std::vector<size_t> order(machines.size());
		for (size_t i = 0; i < order.size(); i++)
			order[i] = i;
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			int groupA = group(machines[a].operation), groupB = group(machines[b].operation);
			return groupA != groupB ? groupA < groupB : machines[a].price < machines[b].price;
		});

		int section = -1;
		for (size_t i : order)
		{
			const auto& machine = machines[i];
			if (group(machine.operation) != section)
			{
				// The last card may have left the cursor on its row.
				if (ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x + 1.f)
					ImGui::NewLine();
				section = group(machine.operation);
				ImGui::SeparatorText(GROUP_NAMES[section]);
			}
			if (StoreCard(machine.name.c_str(), machine.name.c_str(), std::format("${:.0f}", machine.price), m_selectedStoreMachine == static_cast<int>(i),
				workshop.OwnsMachine(i), [&](ImDrawList* drawList, ImVec2 min, float size) { ItemIcons::DrawMachine(drawList, machine, min, size); },
				{ Ratings::MachineAccuracy(machine), Ratings::MachineSpeed(machine) }))
				m_selectedStoreMachine = static_cast<int>(i);
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("MachineDetail", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		const auto& machine = machines[m_selectedStoreMachine];
		ImGui::Text("%s", machine.name.c_str());
		DrawPreview(ShopAssets::FixtureFor(machine).model);
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("%s", machine.description.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Separator();

		Ratings::Bar("Accuracy", Ratings::MachineAccuracy(machine));
		Ratings::Bar("Speed", Ratings::MachineSpeed(machine));
		if (float finish = Ratings::MachineFinish(machine); finish >= 0.f)
			Ratings::Bar("Finish", finish);
		ImGui::Spacing();
		ImGui::Text("Operation   %s", Machining::OperationName(machine.operation));
		ImGui::Text("Accuracy    +/- %s", Deviation(machine.sigma * 2.0).c_str());
		if (machine.finishRa > 0.0)
			ImGui::Text("Finish      %s", Units::FormatFinish(machine.finishRa).c_str());
		if (machine.maxRemoval > 0.0)
			ImGui::Text("Max per cut %s", Deviation(machine.maxRemoval).c_str());
		else
			ImGui::Text("Max per cut no limit");
		if (machine.hoursPerMm > 0.0)
			ImGui::Text("Time        %.1f h setup + %.0f h per mm removed", machine.hoursPerUse, machine.hoursPerMm);
		else
			ImGui::Text("Time per cut %.2f h", machine.hoursPerUse);

		ImGui::Spacing();
		if (workshop.OwnsMachine(m_selectedStoreMachine))
			ImGui::TextColored(COLOR_GOOD, "Owned. If it isn't set up yet, its crate is waiting by the roll-up door.");
		else
		{
			CartLine line;
			line.kind = CartLine::Kind::MACHINE;
			line.index = static_cast<size_t>(m_selectedStoreMachine);
			AddToCartButton(workshop, line, std::format("Add to cart (${:.0f})", machine.price).c_str());
		}
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawFurnitureStore(Workshop& workshop)
{
	const auto& furniture = FurnitureCatalog::All();
	// After the furniture, one more card: an engine stand, for building another engine alongside.
	const int STAND = static_cast<int>(furniture.size());
	m_selectedStoreFurniture = std::clamp(m_selectedStoreFurniture, 0, STAND);
	auto drawStand = [](ImDrawList* drawList, ImVec2 at, float s) {
		ImU32 color = IM_COL32(220, 90, 70, 255);
		drawList->AddLine({ at.x + s * 0.3f, at.y + s * 0.9f }, { at.x + s * 0.3f, at.y + s * 0.25f }, color, 2.5f);
		drawList->AddLine({ at.x + s * 0.3f, at.y + s * 0.3f }, { at.x + s * 0.75f, at.y + s * 0.3f }, color, 2.5f);
		drawList->AddRectFilled({ at.x + s * 0.72f, at.y + s * 0.12f }, { at.x + s * 0.82f, at.y + s * 0.5f }, color);
		drawList->AddLine({ at.x + s * 0.1f, at.y + s * 0.9f }, { at.x + s * 0.9f, at.y + s * 0.9f }, color, 2.5f);
	};

	if (ImGui::BeginChild("FurnitureCatalogue", { ImGui::GetContentRegionAvail().x * 0.55f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::TextDisabled("Delivered in a crate and set up when it's opened. Press F on a piece to move it.");
		ImGui::Spacing();
		for (size_t i = 0; i < furniture.size(); i++)
		{
			const auto& piece = furniture[i];
			if (StoreCard(piece.name.c_str(), piece.name.c_str(), std::format("${:.0f}", piece.price), m_selectedStoreFurniture == static_cast<int>(i),
				false, [&](ImDrawList* drawList, ImVec2 min, float size) { ItemIcons::DrawFurniture(drawList, piece, min, size); }, {}))
				m_selectedStoreFurniture = static_cast<int>(i);
		}
		if (StoreCard("Engine stand", "Engine stand", std::format("${:.0f}", Workshop::ENGINE_STAND_PRICE), m_selectedStoreFurniture == STAND,
			false, drawStand, {}))
			m_selectedStoreFurniture = STAND;
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("FurnitureDetail", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		if (m_selectedStoreFurniture == STAND)
		{
			int engines = workshop.GetEngineCount();
			ImGui::Text("Engine stand");
			DrawPreview(ShopModels::ENGINE_STAND);
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("Holds one engine while it's built, checked and run on the dyno: one more stand, one more engine on the go at once. Set up when its crate is opened; press F on it to move it.");
			ImGui::PopTextWrapPos();
			ImGui::Separator();
			ImGui::TextDisabled("You have %d of %d.", engines, Workshop::MAX_ENGINES);
			CartLine stand;
			stand.kind = CartLine::Kind::ENGINE_STAND;
			AddToCartButton(workshop, stand, std::format("Add to cart (${:.0f})", Workshop::ENGINE_STAND_PRICE).c_str());
			if (engines >= Workshop::MAX_ENGINES)
				ImGui::TextDisabled("There's no room for more.");
			ImGui::EndChild();
			return;
		}
		const auto& piece = furniture[m_selectedStoreFurniture];
		ImGui::Text("%s", piece.name.c_str());
		DrawPreview(ShopAssets::FurnitureModel(piece));
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("%s", piece.description.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Separator();
		int owned = static_cast<int>(std::count(workshop.GetFurniture().begin(), workshop.GetFurniture().end(), static_cast<size_t>(m_selectedStoreFurniture)));
		if (owned > 0)
			ImGui::TextDisabled("You have %d.", owned);
		CartLine line;
		line.kind = CartLine::Kind::FURNITURE;
		line.index = static_cast<size_t>(m_selectedStoreFurniture);
		AddToCartButton(workshop, line, std::format("Add to cart (${:.0f})", piece.price).c_str());
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawUpgradeStore(Workshop& workshop)
{
	const auto& upgrades = UpgradeCatalog::All();
	m_selectedStoreUpgrade = std::clamp(m_selectedStoreUpgrade, 0, static_cast<int>(upgrades.size()) - 1);

	// A sketch of each: the bay off the side of the garage, a second floor on top, the lift's posts.
	auto icon = [](ImDrawList* drawList, UpgradeKind kind, ImVec2 min, float size) {
		auto p = [&](float x, float y) { return ImVec2(min.x + x * size, min.y + y * size); };
		ImU32 color = PartIcons::DEFAULT_COLOR;
		ImU32 dim = (color & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT);
		float line = (std::max)(1.f, size * 0.045f);
		auto house = [&](float x0, float x1, float floorY, float wallY, float ridgeY, bool filled) {
			if (filled)
				drawList->AddRectFilled(p(x0, wallY), p(x1, floorY), dim);
			drawList->AddRect(p(x0, wallY), p(x1, floorY), color, 0.f, 0, line);
			drawList->AddLine(p(x0 - 0.04f, wallY), p((x0 + x1) * 0.5f, ridgeY), color, line);
			drawList->AddLine(p((x0 + x1) * 0.5f, ridgeY), p(x1 + 0.04f, wallY), color, line);
		};
		switch (kind)
		{
			case UpgradeKind::SIDE_BAY:
				house(0.38f, 0.9f, 0.85f, 0.45f, 0.25f, false);
				drawList->AddRectFilled(p(0.08f, 0.55f), p(0.38f, 0.85f), dim);
				drawList->AddRect(p(0.08f, 0.55f), p(0.38f, 0.85f), color, 0.f, 0, line);
				break;
			case UpgradeKind::SECOND_STORY:
				house(0.18f, 0.82f, 0.88f, 0.58f, 0.58f, false);
				house(0.18f, 0.82f, 0.58f, 0.32f, 0.12f, true);
				for (int step = 0; step < 4; step++)
					drawList->AddLine(p(0.84f + step * 0.03f, 0.58f + step * 0.075f), p(0.9f + step * 0.03f, 0.58f + step * 0.075f), color, line);
				break;
			case UpgradeKind::POST_LIFT:
				drawList->AddRectFilled(p(0.16f, 0.15f), p(0.26f, 0.88f), color);
				drawList->AddRectFilled(p(0.74f, 0.15f), p(0.84f, 0.88f), color);
				drawList->AddLine(p(0.16f, 0.12f), p(0.84f, 0.12f), color, line * 1.5f);
				drawList->AddLine(p(0.26f, 0.55f), p(0.42f, 0.55f), color, line * 1.5f);
				drawList->AddLine(p(0.58f, 0.55f), p(0.74f, 0.55f), color, line * 1.5f);
				break;
		}
	};

	if (ImGui::BeginChild("UpgradeCatalogue", { ImGui::GetContentRegionAvail().x * 0.55f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::TextDisabled("Building work is done as soon as it's paid for; the lift comes crated.");
		ImGui::Spacing();
		for (size_t i = 0; i < upgrades.size(); i++)
		{
			const auto& upgrade = upgrades[i];
			if (StoreCard(upgrade.name.c_str(), upgrade.name.c_str(), std::format("${:.0f}", upgrade.price), m_selectedStoreUpgrade == static_cast<int>(i),
				workshop.OwnsUpgrade(upgrade.kind), [&](ImDrawList* drawList, ImVec2 min, float size) { icon(drawList, upgrade.kind, min, size); }, {}))
				m_selectedStoreUpgrade = static_cast<int>(i);
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("UpgradeDetail", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		const auto& upgrade = upgrades[m_selectedStoreUpgrade];
		ImGui::Text("%s", upgrade.name.c_str());
		if (upgrade.kind == UpgradeKind::POST_LIFT)
			DrawPreview(ShopModels::POST_LIFT);
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("%s", upgrade.description.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Separator();
		if (workshop.OwnsUpgrade(upgrade.kind))
			ImGui::TextColored(COLOR_GOOD, "Done.");
		else
		{
			CartLine line;
			line.kind = CartLine::Kind::UPGRADE;
			line.index = static_cast<size_t>(m_selectedStoreUpgrade);
			AddToCartButton(workshop, line, std::format("Add to cart (${:.0f})", upgrade.price).c_str());
		}
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawAssemblyTab(Workshop& workshop)
{
	// Read-only, like the workbench: parts are fitted and taken off at the stand itself, by hand.
	const auto& assembly = workshop.GetAssembly();
	ImGui::Text("Engine %d", workshop.GetActiveEngine() + 1);
	ImGui::SameLine(0.f, 20.f);
	auto missing = assembly.MissingParts();
	if (missing.empty())
		ImGui::TextColored(COLOR_GOOD, "Long block complete. Check your clearances, then take it to the dyno.");
	else
		ImGui::TextDisabled("%zu positions still empty. Carry a part to the stand and press E to fit it.", missing.size());

	// What can go on next, in build order.
	std::vector<std::string> ready;
	for (size_t slotIndex = 0; slotIndex < static_cast<size_t>(AssemblySlot::COUNT); slotIndex++)
	{
		auto slot = static_cast<AssemblySlot>(slotIndex);
		int count = 0;
		for (int index = 0; index < assembly.SlotCount(slot); index++)
			if (!assembly.Get(slot, index) && !assembly.MissingPrerequisite(slot, index))
				count++;
		if (count > 0)
			ready.push_back(count > 1 ? std::format("{} x{}", AssemblySlotName(slot), count) : AssemblySlotName(slot));
	}
	if (!ready.empty())
	{
		std::string list;
		for (const auto& entry : ready)
			list += (list.empty() ? "" : ", ") + entry;
		ImGui::TextColored(COLOR_WARN, "Next to fit: %s", list.c_str());
	}
	ImGui::Separator();

	pPart selected = nullptr;
	if (ImGui::BeginChild("Fitted", { 300.f, 0.f }, ImGuiChildFlags_Borders))
		selected = DrawAssemblySlots(workshop);
	ImGui::EndChild();

	ImGui::SameLine();

	if (ImGui::BeginChild("Inspector", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		// Nothing is machined on the engine.
		m_selectedPartMachine = -1;
		if (selected)
			DrawPartInspector(workshop, selected);
		else
			DrawAssemblyChecks(workshop);
	}
	ImGui::EndChild();
}

pPart WorkshopUI::DrawAssemblySlots(Workshop& workshop)
{
	const auto& assembly = workshop.GetAssembly();
	pPart selected = nullptr;

	if (ImGui::Selectable("Engine checks", m_standPartId == 0))
	{
		m_standPartId = 0;
		m_selectedFeature.clear();
	}

	bool any = false;
	for (size_t slotIndex = 0; slotIndex < static_cast<size_t>(AssemblySlot::COUNT); slotIndex++)
	{
		auto slot = static_cast<AssemblySlot>(slotIndex);
		bool header = false;
		for (int index = 0; index < assembly.SlotCount(slot); index++)
		{
			auto part = assembly.Get(slot, index);
			if (!part)
				continue;
			if (!header)
			{
				ImGui::SeparatorText(AssemblySlotName(slot));
				header = true;
			}
			any = true;

			ImGui::PushID(static_cast<int>(part->GetId()));
			PartIcons::Icon(part->GetType(), ImGui::GetTextLineHeight() * 1.3f);
			ImGui::SameLine();
			std::string position = assembly.SlotCount(slot) > 1 ? assembly.PositionName(slot, index) + ": " : "";
			if (ImGui::Selectable(std::format("{}#{} {}", position, part->GetId(), part->GetName()).c_str(), m_standPartId == part->GetId()))
			{
				m_standPartId = part->GetId();
				m_selectedFeature.clear();
			}
			if (m_standPartId == part->GetId())
				selected = part;
			ImGui::PopID();
		}
	}

	if (!any)
	{
		ImGui::Spacing();
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("Nothing fitted yet. Start with the block: carry it to the stand and press E.");
		ImGui::PopTextWrapPos();
	}
	// The part that was being looked at came off.
	if (!selected)
		m_standPartId = 0;
	return selected;
}

void WorkshopUI::DrawAssemblyChecks(Workshop& workshop)
{
	const auto& spec = workshop.GetSpec();
	auto& assembly = workshop.GetAssembly();
	ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

	// One row of a clearance table: the player's reading against the target, a button to take
	// it, and the true value in X-ray mode.
	auto checkRow = [&](const std::string& label, AssemblyCheck check, int index, bool rod, const Range& target, std::optional<double> truth)
	{
		// Installed height is a size; everything else here is a small clearance.
		auto format = [&](double value) { return check == AssemblyCheck::INSTALLED_HEIGHT ? Size(value) : Deviation(value); };

		ImGui::PushID(label.c_str());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(label.c_str());

		ImGui::TableSetColumnIndex(1);
		ImGui::TextDisabled("%s - %s", format(target.min).c_str(), format(target.max).c_str());

		ImGui::TableSetColumnIndex(2);
		if (auto reading = workshop.GetAssemblyReading(check, index, rod))
		{
			if (reading->stale)
				ImGui::TextColored(COLOR_DIM, "%s (stale)", format(reading->value).c_str());
			else
				ColoredRangeText(reading->value, target, format(reading->value));
		}
		else
			ImGui::TextDisabled("-");

		ImGui::TableSetColumnIndex(3);
		if (truth)
			ImGui::TextDisabled("%s", ToolHint(workshop, workshop.BestToolFor(check)).c_str());
		else
			ImGui::TextDisabled("fit parts first");

		if (m_xray)
		{
			ImGui::TableSetColumnIndex(4);
			if (truth)
				ImGui::TextColored(COLOR_XRAY, "%s", format(*truth).c_str());
		}
		ImGui::PopID();
	};

	int columns = m_xray ? 5 : 4;
	auto beginTable = [&](const char* id)
	{
		if (!ImGui::BeginTable(id, columns, flags))
			return false;
		ImGui::TableSetupColumn("Check");
		ImGui::TableSetupColumn("Target");
		ImGui::TableSetupColumn("Reading");
		ImGui::TableSetupColumn("Measure with", ImGuiTableColumnFlags_WidthFixed, 170.f);
		if (m_xray)
			ImGui::TableSetupColumn("True");
		HeadersRow({
			"What's being checked, and where on the engine.",
			"The range the engine spec calls for.",
			"Your last reading on the assembled engine. Stale once something it depends on has changed.",
			"The tool for the check. Hold it and aim at the spot on the engine.",
			XRAY_TIP });
		return true;
	};

	ImGui::SeparatorText("Bearing oil clearance (Plastigauge)");
	if (beginTable("Bearings"))
	{
		for (int journal = 0; journal < spec.mainJournals; journal++)
			checkRow(std::format("Main {}", journal + 1), AssemblyCheck::OIL_CLEARANCE, journal, false, spec.mainClearance, assembly.MainClearance(journal));
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			checkRow(std::format("Rod {}", cylinder + 1), AssemblyCheck::OIL_CLEARANCE, cylinder, true, spec.rodClearance, assembly.RodClearance(cylinder));
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Top ring end gap (feelers)");
	if (beginTable("Rings"))
	{
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			checkRow(std::format("Cylinder {}", cylinder + 1), AssemblyCheck::RING_GAP, cylinder, false, spec.ringGap, assembly.RingGap(cylinder));
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Deck clearance (dial indicator, + = below deck)");
	if (beginTable("Deck"))
	{
		Range deckTarget = { spec.quench.min - spec.gasketThickness.nominal, spec.quench.max - spec.gasketThickness.nominal };
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			checkRow(std::format("Cylinder {}", cylinder + 1), AssemblyCheck::DECK_CLEARANCE, cylinder, false, deckTarget, assembly.DeckClearance(cylinder));
		ImGui::EndTable();
	}

	// Numbers the builder has to work out from their own measurements.
	ImGui::SeparatorText("Your calculations");
	auto block = assembly.Get(AssemblySlot::BLOCK, 0);
	auto crankshaft = assembly.Get(AssemblySlot::CRANKSHAFT, 0);
	auto head = assembly.Get(AssemblySlot::CYLINDER_HEAD, 0);
	auto gasket = assembly.Get(AssemblySlot::HEAD_GASKET, 0);
	auto gasketThickness = MeasuredSize(gasket, FeatureNames::COMPRESSED_THICKNESS);
	auto stroke = MeasuredSize(crankshaft, FeatureNames::STROKE);

	if (ImGui::BeginTable("Calculations", m_xray ? 4 : 3, flags))
	{
		ImGui::TableSetupColumn("Cylinder");
		ImGui::TableSetupColumn("Piston-to-wall");
		ImGui::TableSetupColumn("Quench / Compression");
		if (m_xray)
			ImGui::TableSetupColumn("True");
		HeadersRow({
			"Which cylinder.",
			"Bore minus piston skirt, from your own readings. Too tight scuffs when hot; too loose rocks and slaps.",
			"Quench: piston-to-head gap at top dead centre (deck clearance plus gasket). Compression ratio comes from the swept and chamber volumes.",
			XRAY_TIP });

		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
		{
			auto piston = assembly.Get(AssemblySlot::PISTON, cylinder);
			auto bore = MeasuredSize(block, FeatureNames::Bore(cylinder));
			auto skirt = MeasuredSize(piston, FeatureNames::SKIRT);
			auto chamber = MeasuredSize(head, FeatureNames::Chamber(cylinder));
			auto dish = MeasuredSize(piston, FeatureNames::DISH_VOLUME);
			auto deckReading = workshop.GetAssemblyReading(AssemblyCheck::DECK_CLEARANCE, cylinder);
			std::optional<double> deck = deckReading && !deckReading->stale ? std::optional<double>(deckReading->value) : std::nullopt;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%d", cylinder + 1);

			ImGui::TableSetColumnIndex(1);
			if (bore && skirt && piston)
				ColoredRangeText(*bore - *skirt, spec.PistonToWallFor(piston->GetMaterial()), Deviation(*bore - *skirt));
			else
				ImGui::TextDisabled("measure bore + skirt");

			ImGui::TableSetColumnIndex(2);
			if (deck && gasketThickness)
			{
				double quench = *deck + *gasketThickness;
				ColoredRangeText(quench, spec.quench, Deviation(quench));
				if (bore && stroke && chamber && dish)
					if (auto ratio = EngineAssembly::CompressionRatioFrom(*bore, *stroke, *chamber, *dish, spec.gasketBore, *gasketThickness, *deck))
					{
						ImGui::SameLine();
						ImGui::TextColored(spec.compressionRatio.Contains(*ratio) ? COLOR_GOOD : COLOR_BAD, " / %.2f:1", *ratio);
					}
			}
			else
				ImGui::TextDisabled("deck + gasket, then cc chamber/dish");

			if (m_xray)
			{
				ImGui::TableSetColumnIndex(3);
				auto pistonToWall = assembly.PistonToWall(cylinder);
				auto quench = assembly.Quench(cylinder);
				auto ratio = assembly.CompressionRatio(cylinder);
				ImGui::TextColored(COLOR_XRAY, "%s  %s  %.2f:1", pistonToWall ? Deviation(*pistonToWall).c_str() : "-",
					quench ? Deviation(*quench).c_str() : "-", ratio.value_or(0.0));
			}
		}
		ImGui::EndTable();
	}

	// Valvetrain
	ImGui::SeparatorText("Cam journal oil clearance (Plastigauge)");
	if (beginTable("Cam"))
	{
		for (int journal = 0; journal < spec.camJournals; journal++)
			checkRow(std::format("Cam journal {}", journal + 1), AssemblyCheck::CAM_CLEARANCE, journal, false, spec.camClearance, assembly.CamClearance(journal));
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Valve spring installed height (height mic)");
	if (beginTable("InstalledHeight"))
	{
		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			for (bool exhaust : { false, true })
				checkRow(std::format("{} {}", FeatureNames::ValveSide(exhaust), cylinder + 1), AssemblyCheck::INSTALLED_HEIGHT,
					EngineAssembly::SpringIndex(cylinder, exhaust), false, spec.installedHeightRange, assembly.InstalledHeight(cylinder, exhaust));
		ImGui::EndTable();
	}

	// Stem clearance from the guide and stem readings; seat pressure from the spring tester's
	// reading, corrected for this valve's actual installed height using the spring's nominal rate.
	if (ImGui::BeginTable("ValveCalculations", m_xray ? 4 : 3, flags))
	{
		ImGui::TableSetupColumn("Valve");
		ImGui::TableSetupColumn("Stem-to-guide");
		ImGui::TableSetupColumn("Seat pressure");
		if (m_xray)
			ImGui::TableSetupColumn("True");
		HeadersRow({
			"Which valve, by cylinder.",
			"Guide bore minus valve stem, from your own readings. Too tight seizes; too loose wears and burns oil.",
			"Spring force holding the valve shut, corrected for this valve's actual installed height.",
			XRAY_TIP });

		for (int cylinder = 0; cylinder < spec.cylinders; cylinder++)
			for (bool exhaust : { false, true })
			{
				auto valve = assembly.Get(exhaust ? AssemblySlot::EXHAUST_VALVE : AssemblySlot::INTAKE_VALVE, cylinder);
				auto spring = assembly.Get(AssemblySlot::VALVE_SPRING, EngineAssembly::SpringIndex(cylinder, exhaust));
				auto guide = MeasuredSize(head, FeatureNames::Guide(cylinder, exhaust));
				auto stem = MeasuredSize(valve, FeatureNames::STEM);
				auto testedPressure = MeasuredSize(spring, FeatureNames::SEAT_PRESSURE);
				auto heightReading = workshop.GetAssemblyReading(AssemblyCheck::INSTALLED_HEIGHT, EngineAssembly::SpringIndex(cylinder, exhaust));

				ImGui::PushID(cylinder * 2 + (exhaust ? 1 : 0));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s %d", FeatureNames::ValveSide(exhaust).c_str(), cylinder + 1);

				ImGui::TableSetColumnIndex(1);
				if (guide && stem)
					ColoredRangeText(*guide - *stem, exhaust ? spec.exhaustStemClearance : spec.intakeStemClearance, Deviation(*guide - *stem));
				else
					ImGui::TextDisabled("measure guide + stem");

				ImGui::TableSetColumnIndex(2);
				if (testedPressure && heightReading && !heightReading->stale)
				{
					double pressure = *testedPressure - spec.springRate * (heightReading->value - spec.installedHeight);
					ColoredRangeText(pressure, spec.seatPressure, Units::FormatForce(pressure));
				}
				else
					ImGui::TextDisabled("test spring + installed height");

				if (m_xray)
				{
					ImGui::TableSetColumnIndex(3);
					auto clearance = assembly.StemClearance(cylinder, exhaust);
					auto pressure = assembly.SeatPressure(cylinder, exhaust);
					ImGui::TextColored(COLOR_XRAY, "%s  %s", clearance ? Deviation(*clearance).c_str() : "-", pressure ? Units::FormatForce(*pressure).c_str() : "-");
				}
				ImGui::PopID();
			}
		ImGui::EndTable();
	}
}
namespace
{
	// A step for axis ticks that gives about the wanted number of them: 1, 2, 2.5 or 5 times a power of ten.
	float NiceStep(float range, int ticks)
	{
		float raw = range / (std::max)(ticks, 1);
		float power = std::pow(10.f, std::floor(std::log10((std::max)(raw, 1e-6f))));
		for (float multiple : { 1.f, 2.f, 2.5f, 5.f, 10.f })
			if (raw <= multiple * power)
				return multiple * power;
		return 10.f * power;
	}

	// The pull as a dyno sheet draws it: torque (left scale) and power (right scale) against engine
	// speed, with the peaks marked, the redline, where it let go, and the needle while it runs. Only
	// the first shownPoints of the curve are drawn, so a live pull grows from left to right.
	void DrawDynoChart(const DynoResult& result, int shownPoints, float liveRpm, bool live, float redline)
	{
		const ImU32 TORQUE = IM_COL32(255, 170, 60, 255);
		const ImU32 POWER = IM_COL32(90, 170, 255, 255);
		const ImU32 GRID = IM_COL32(255, 255, 255, 28);
		const ImU32 AXIS = IM_COL32(200, 200, 200, 160);
		const ImU32 LABEL = IM_COL32(190, 190, 190, 255);
		const ImU32 REDLINE = IM_COL32(230, 70, 60, 200);
		constexpr float HP_PER_KW = 1.341f;
		constexpr float LEFT = 58.f, RIGHT = 58.f, TOP = 26.f, BOTTOM = 40.f;

		ImVec2 origin = ImGui::GetCursorScreenPos();
		ImVec2 size = { ImGui::GetContentRegionAvail().x, 300.f };
		ImGui::InvisibleButton("DynoChart", size);
		bool hovered = ImGui::IsItemHovered();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 plotMin = { origin.x + LEFT, origin.y + TOP };
		ImVec2 plotMax = { origin.x + size.x - RIGHT, origin.y + size.y - BOTTOM };
		drawList->AddRectFilled(origin, { origin.x + size.x, origin.y + size.y }, IM_COL32(18, 20, 24, 255), 4.f);

		// Scales: whole thousands of rpm across, and round numbers up both sides.
		float minRpm = std::floor(result.curve.front().rpm / 1000.f) * 1000.f;
		float maxRpm = std::ceil((std::max)(result.curve.back().rpm, redline) / 1000.f) * 1000.f;
		float maxTorque = 0.f, maxPower = 0.f;
		for (const auto& point : result.curve)
		{
			maxTorque = (std::max)(maxTorque, point.torqueNm);
			maxPower = (std::max)(maxPower, point.powerKw * HP_PER_KW);
		}
		float torqueStep = NiceStep(maxTorque * 1.15f, 5), powerStep = NiceStep(maxPower * 1.15f, 5);
		float torqueTop = std::ceil(maxTorque * 1.15f / torqueStep) * torqueStep;
		float powerTop = std::ceil(maxPower * 1.15f / powerStep) * powerStep;
		auto xOf = [&](float rpm) { return plotMin.x + (rpm - minRpm) / (maxRpm - minRpm) * (plotMax.x - plotMin.x); };
		auto yOf = [&](float value, float top) { return plotMax.y - value / top * (plotMax.y - plotMin.y); };

		// Grid and the rpm axis.
		for (float rpm = minRpm; rpm <= maxRpm + 1.f; rpm += 1000.f)
		{
			float x = xOf(rpm);
			drawList->AddLine({ x, plotMin.y }, { x, plotMax.y }, GRID);
			std::string text = std::format("{:.0f}", rpm);
			drawList->AddText({ x - ImGui::CalcTextSize(text.c_str()).x * 0.5f, plotMax.y + 4.f }, LABEL, text.c_str());
		}
		// Torque on the left, power on the right, each against its own scale.
		for (int tick = 0; tick * torqueStep <= torqueTop + 0.01f; tick++)
		{
			float y = yOf(tick * torqueStep, torqueTop);
			drawList->AddLine({ plotMin.x, y }, { plotMax.x, y }, GRID);
			std::string text = std::format("{:.0f}", tick * torqueStep);
			drawList->AddText({ plotMin.x - 6.f - ImGui::CalcTextSize(text.c_str()).x, y - 7.f }, TORQUE, text.c_str());
		}
		for (int tick = 0; tick * powerStep <= powerTop + 0.01f; tick++)
		{
			float y = yOf(tick * powerStep, powerTop);
			drawList->AddText({ plotMax.x + 6.f, y - 7.f }, POWER, std::format("{:.0f}", tick * powerStep).c_str());
		}
		drawList->AddRect(plotMin, plotMax, AXIS);

		// Axis titles and the key.
		const char* xTitle = "Engine speed (rpm)";
		drawList->AddText({ (plotMin.x + plotMax.x - ImGui::CalcTextSize(xTitle).x) * 0.5f, plotMax.y + 20.f }, LABEL, xTitle);
		drawList->AddText({ origin.x + 6.f, origin.y + 5.f }, TORQUE, "Torque (Nm)");
		const char* powerTitle = "Power (hp)";
		drawList->AddText({ origin.x + size.x - 6.f - ImGui::CalcTextSize(powerTitle).x, origin.y + 5.f }, POWER, powerTitle);

		// The redline.
		if (redline > minRpm && redline < maxRpm)
			for (float y = plotMin.y; y < plotMax.y; y += 8.f)
				drawList->AddLine({ xOf(redline), y }, { xOf(redline), (std::min)(y + 4.f, plotMax.y) }, REDLINE, 1.5f);

		// The curves, as far as the pull has got.
		std::vector<ImVec2> torqueLine, powerLine;
		for (int i = 0; i < shownPoints && i < static_cast<int>(result.curve.size()); i++)
		{
			const auto& point = result.curve[i];
			torqueLine.push_back({ xOf(point.rpm), yOf(point.torqueNm, torqueTop) });
			powerLine.push_back({ xOf(point.rpm), yOf(point.powerKw * HP_PER_KW, powerTop) });
		}
		drawList->PushClipRect(plotMin, plotMax, true);
		if (torqueLine.size() > 1)
		{
			drawList->AddPolyline(torqueLine.data(), static_cast<int>(torqueLine.size()), TORQUE, 0, 2.5f);
			drawList->AddPolyline(powerLine.data(), static_cast<int>(powerLine.size()), POWER, 0, 2.5f);
		}
		drawList->PopClipRect();

		// Peaks, once the pull has passed them, and where it broke.
		bool finished = !live || shownPoints >= static_cast<int>(result.curve.size());
		auto mark = [&](float rpm, float value, float top, ImU32 color, const std::string& text) {
			ImVec2 at = { xOf(rpm), yOf(value, top) };
			drawList->AddCircleFilled(at, 4.5f, color);
			ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
			float x = (std::min)(at.x + 6.f, plotMax.x - textSize.x - 2.f);
			drawList->AddText({ x, at.y - textSize.y - 4.f }, color, text.c_str());
		};
		if (finished)
		{
			mark(static_cast<float>(result.peakTorqueRpm), static_cast<float>(result.peakTorqueNm), torqueTop, TORQUE,
				std::format("{:.0f} Nm @ {:.0f}", result.peakTorqueNm, result.peakTorqueRpm));
			mark(static_cast<float>(result.peakPowerRpm), static_cast<float>(result.peakPowerKw) * HP_PER_KW, powerTop, POWER,
				std::format("{:.0f} hp @ {:.0f}", result.peakPowerKw * HP_PER_KW, result.peakPowerRpm));
			if (result.failed)
			{
				float x = xOf(result.failureRpm);
				drawList->AddLine({ x - 8.f, plotMin.y + 8.f }, { x + 8.f, plotMin.y + 24.f }, REDLINE, 3.f);
				drawList->AddLine({ x + 8.f, plotMin.y + 8.f }, { x - 8.f, plotMin.y + 24.f }, REDLINE, 3.f);
				drawList->AddText({ (std::min)(x + 12.f, plotMax.x - 120.f), plotMin.y + 8.f }, REDLINE, std::format("Failed at {:.0f}", result.failureRpm).c_str());
			}
		}

		// The needle, while it pulls.
		if (live && liveRpm >= minRpm && liveRpm <= maxRpm)
			drawList->AddLine({ xOf(liveRpm), plotMin.y }, { xOf(liveRpm), plotMax.y }, IM_COL32(255, 255, 255, 200), 1.5f);

		// Pointing at the chart reads it off at that speed.
		if (hovered && finished && !result.curve.empty())
		{
			float mouseX = ImGui::GetIO().MousePos.x;
			const DynoPoint* nearest = &result.curve.front();
			for (const auto& point : result.curve)
				if (std::abs(xOf(point.rpm) - mouseX) < std::abs(xOf(nearest->rpm) - mouseX))
					nearest = &point;
			float x = xOf(nearest->rpm);
			drawList->AddLine({ x, plotMin.y }, { x, plotMax.y }, IM_COL32(255, 255, 255, 90));
			drawList->AddCircleFilled({ x, yOf(nearest->torqueNm, torqueTop) }, 3.5f, TORQUE);
			drawList->AddCircleFilled({ x, yOf(nearest->powerKw * HP_PER_KW, powerTop) }, 3.5f, POWER);
			ImGui::SetTooltip("%.0f rpm\nTorque %.1f Nm (%.1f lb-ft)\nPower %.1f hp (%.1f kW)", nearest->rpm, nearest->torqueNm,
				nearest->torqueNm * 0.7376f, nearest->powerKw * HP_PER_KW, nearest->powerKw);
		}
	}
}

void WorkshopUI::DrawDynoTab(Workshop& workshop)
{
	int onDyno = workshop.GetEngineOnDyno();

	// Nothing on the dyno: pick a finished engine to move over from its stand.
	if (onDyno < 0)
	{
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("The dyno is empty. A finished engine comes off its stand and is strapped down here to be run; afterwards it goes back onto a free stand, or is sold from here.");
		ImGui::PopTextWrapPos();
		ImGui::SeparatorText("Engines");
		for (int engine = 0; engine < workshop.GetEngineCount(); engine++)
		{
			const auto& assembly = workshop.GetAssembly(engine);
			ImGui::PushID(engine);
			int needed = 0;
			for (int slot = 0; slot < static_cast<int>(AssemblySlot::COUNT); slot++)
				needed += assembly.SlotCount(static_cast<AssemblySlot>(slot));
			int fitted = static_cast<int>(assembly.GetAllParts().size());
			ImGui::Text("Engine %d (stand %d)", engine + 1, engine + 1);
			ImGui::SameLine(200.f);
			ImGui::TextColored(fitted == needed ? COLOR_GOOD : fitted > 0 ? COLOR_WARN : COLOR_DIM, "%d / %d parts", fitted, needed);
			ImGui::SameLine(330.f);
			ImGui::BeginDisabled(!assembly.IsComplete());
			if (ImGui::Button("Move onto the dyno"))
				workshop.MoveEngineToDyno(engine);
			ImGui::EndDisabled();
			if (!assembly.IsComplete())
			{
				ImGui::SameLine();
				ImGui::TextDisabled(fitted == 0 ? "No engine" : "Not finished");
			}
			ImGui::PopID();
		}
		return;
	}

	// The engine on the dyno is the one the sheet is about.
	workshop.SetActiveEngine(onDyno);
	const auto& spec = workshop.GetSpec();
	ImGui::Text("Engine %d is on the dyno.", onDyno + 1);

	ImGui::BeginDisabled(workshop.GetMoney() < 50.0 || m_pullActive);
	if (ImGui::Button("Pull to redline ($50)"))
	{
		const auto& pull = workshop.RunDyno();
		if (pull.ran && workshop.IsDynoResultCurrent() && !pull.curve.empty())
		{
			m_pullEngine = onDyno;
			m_pullActive = true;
			m_pullTime = 0.f;
			m_spinDownTime = 0.f;
			m_pullStartRpm = pull.curve.front().rpm;
			m_pullEndRpm = pull.failed ? pull.failureRpm : pull.curve.back().rpm;
			m_pullFailed = pull.failed;
		}
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("A wide-open-throttle pull from %d rpm to the %d rpm redline, recording torque and power all the way.", 1000, spec.redline);

	const auto& result = workshop.GetLastDynoResult();
	if (result.ran)
	{
		if (!workshop.IsDynoResultCurrent())
			ImGui::TextColored(COLOR_WARN, "The engine has changed since this pull.");

		// While the pull runs, the needle sweeps and the curves draw as it goes.
		float liveRpm = m_pullActive ? GetLiveRpm() : 1e9f;
		int shownPoints = 0;
		for (const auto& point : result.curve)
			if (point.rpm <= liveRpm)
				shownPoints++;

		if (m_pullActive)
		{
			const DynoPoint* now = shownPoints > 0 ? &result.curve[shownPoints - 1] : nullptr;
			ImGui::SetWindowFontScale(1.6f);
			ImGui::TextColored(COLOR_WARN, "%5.0f rpm", liveRpm);
			ImGui::SetWindowFontScale(1.f);
			ImGui::SameLine(0.f, 30.f);
			ImGui::Text("Torque %.1f Nm   Power %.1f hp", now ? now->torqueNm : 0.f, now ? now->powerKw * 1.341f : 0.f);
		}
		else if (result.failed)
			ImGui::TextColored(COLOR_BAD, "FAILED at %.0f rpm", result.failureRpm);

		if (!result.curve.empty())
			DrawDynoChart(result, shownPoints, liveRpm, m_pullActive, static_cast<float>(spec.redline));

		if (!m_pullActive)
		{
			// The numbers off the sheet.
			double litres = result.displacementCc / 1000.0;
			if (ImGui::BeginTable("DynoNumbers", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame))
			{
				auto cell = [](const char* label, const std::string& value, std::optional<ImVec4> color = std::nullopt) {
					ImGui::TableNextColumn();
					ImGui::TextDisabled("%s", label);
					ImGui::TableNextColumn();
					if (color)
						ImGui::TextColored(*color, "%s", value.c_str());
					else
						ImGui::TextUnformatted(value.c_str());
				};
				cell("Peak power", std::format("{:.1f} hp ({:.1f} kW) @ {:.0f}", result.peakPowerKw * 1.341, result.peakPowerKw, result.peakPowerRpm));
				cell("Peak torque", std::format("{:.1f} Nm ({:.1f} lb-ft) @ {:.0f}", result.peakTorqueNm, result.peakTorqueNm * 0.7376, result.peakTorqueRpm));
				cell("Specific output", litres > 0.0 ? std::format("{:.1f} hp per litre", result.peakPowerKw * 1.341 / litres) : "-");
				cell("Displacement", std::format("{:.0f} cc ({:.1f} L)", result.displacementCc, litres));
				cell("Compression", std::format("{:.2f}:1", result.compressionRatio),
					spec.compressionRatio.Contains(result.compressionRatio) ? COLOR_GOOD : COLOR_WARN);
				cell("Oil pressure", std::format("{:.1f} bar", result.oilPressureBar));
				cell("Blow-by", std::format("{:.1f}%", result.blowbyPercent));
				ImGui::TableNextColumn();
				ImGui::TextDisabled("Health");
				ImGui::TableNextColumn();
				ImGui::ProgressBar(static_cast<float>(result.healthScore / 100.0), { -1.f, 0.f }, std::format("{:.0f}%", result.healthScore).c_str());
				ImGui::EndTable();
			}

			ImGui::SeparatorText("Dyno sheet");
			for (const auto& finding : result.findings)
			{
				ImVec4 color = finding.severity == FindingSeverity::FAILURE ? COLOR_BAD : finding.severity == FindingSeverity::WARNING ? COLOR_WARN : COLOR_DIM;
				ImGui::TextColored(color, "%s", finding.message.c_str());
			}
		}
	}
	if (m_pullActive)
		return;

	// Done with it: sell it from here, or put it back on a stand to work on.
	ImGui::SeparatorText("Off the dyno");
	double value = workshop.EstimateEngineValue();
	ImGui::BeginDisabled(value <= 0.0);
	if (ImGui::Button(std::format("Sell the engine (${:.0f})", value).c_str()))
		workshop.SellEngine();
	ImGui::EndDisabled();
	if (value <= 0.0)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("Buyers want a clean pull of the engine as it sits now.");
	}
	if (workshop.GetEngineOnDyno() < 0)
		return;
	ImGui::TextUnformatted("Put it back on stand:");
	for (int stand = 0; stand < workshop.GetEngineCount(); stand++)
	{
		if (!workshop.IsStandFree(stand))
			continue;
		ImGui::SameLine();
		ImGui::PushID(stand);
		if (ImGui::Button(std::format("Stand {}{}", stand + 1, stand == onDyno ? " (its own)" : "").c_str()))
			workshop.ReturnEngineFromDyno(stand);
		ImGui::PopID();
	}
}


void WorkshopUI::DrawLog(Workshop& workshop)
{
	if (ImGui::BeginChild("Log", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		for (const auto& line : workshop.GetLog())
			ImGui::TextUnformatted(line.c_str());

		if (workshop.GetLog().size() != m_lastLogSize)
		{
			ImGui::SetScrollHereY(1.f);
			m_lastLogSize = workshop.GetLog().size();
		}
	}
	ImGui::EndChild();
}
