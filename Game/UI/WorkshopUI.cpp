#include "WorkshopUI.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <imgui.h>

#include "ItemIcons.h"
#include "Ratings.h"
#include "PartIcons.h"

namespace
{
	const ImVec4 COLOR_GOOD = { 0.45f, 0.90f, 0.45f, 1.f };
	const ImVec4 COLOR_WARN = { 1.00f, 0.80f, 0.30f, 1.f };
	const ImVec4 COLOR_BAD = { 1.00f, 0.40f, 0.40f, 1.f };
	const ImVec4 COLOR_DIM = { 0.55f, 0.55f, 0.55f, 1.f };
	const ImVec4 COLOR_XRAY = { 0.55f, 0.75f, 1.00f, 1.f };

	const PartType ALL_PART_TYPES[] = {
		PartType::BLOCK, PartType::CRANKSHAFT, PartType::CYLINDER_HEAD, PartType::HEAD_GASKET, PartType::CONNECTING_ROD,
		PartType::PISTON, PartType::PISTON_RING, PartType::MAIN_BEARING, PartType::ROD_BEARING,
		PartType::CAMSHAFT, PartType::INTAKE_VALVE, PartType::EXHAUST_VALVE, PartType::VALVE_SPRING
	};

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
}

void WorkshopUI::OpenStore(StoreCategory category)
{
	Open(WorkshopTab::STORE);
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
	m_activeMachine = -1;
}

void WorkshopUI::SetXray(bool xray)
{
	m_xray = xray;
}

bool WorkshopUI::IsOpen() const
{
	return m_open;
}

WorkshopTab WorkshopUI::GetCurrentTab() const
{
	return m_currentTab;
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

bool WorkshopUI::BeginTab(const char* label, WorkshopTab tab)
{
	ImGuiTabItemFlags flags = m_requestedTab == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
	if (!ImGui::BeginTabItem(label, nullptr, flags))
		return false;
	m_currentTab = tab;
	return true;
}

void WorkshopUI::Draw(Workshop& workshop)
{
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

	if (ImGui::Begin("Engine Builder", &m_open))
	{
		DrawStatusBar(workshop);
		ImGui::Separator();

		float logHeight = ImGui::GetTextLineHeightWithSpacing() * 7.f;
		if (ImGui::BeginChild("Main", { 0.f, -logHeight }))
		{
			if (ImGui::BeginTabBar("Tabs"))
			{
				if (m_backpackContents && BeginTab("Backpack", WorkshopTab::BACKPACK))
				{
					m_backpackContents();
					ImGui::EndTabItem();
				}
				if (BeginTab("Bench", WorkshopTab::BENCH))
				{
					DrawInventoryTab(workshop);
					ImGui::EndTabItem();
				}
				// Only while working at a machine, named after it.
				if (m_activeMachine >= 0)
				{
					auto label = std::format("{}###Machine", MachineCatalog::All()[m_activeMachine].name);
					if (BeginTab(label.c_str(), WorkshopTab::MACHINE))
					{
						DrawMachineTab(workshop);
						ImGui::EndTabItem();
					}
				}
				if (BeginTab("Engine Stand", WorkshopTab::ENGINE_STAND))
				{
					DrawAssemblyTab(workshop);
					ImGui::EndTabItem();
				}
				if (BeginTab("Dyno Cell", WorkshopTab::DYNO))
				{
					DrawDynoTab(workshop);
					ImGui::EndTabItem();
				}
				if (BeginTab("Store", WorkshopTab::STORE))
				{
					DrawStoreTab(workshop);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			m_requestedTab = std::nullopt;
		}
		ImGui::EndChild();

		DrawLog(workshop);
	}
	ImGui::End();
}

void WorkshopUI::DrawStatusBar(Workshop& workshop)
{
	ImGui::Text("%s", workshop.GetSpec().name.c_str());
	ImGui::SameLine(0.f, 30.f);
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

	if (!IsUsable(*part))
	{
		ImGui::Spacing();
		ImGui::TextColored(COLOR_WARN, "Still in its delivery box. Open the delivery by the roll-up door first.");
		return;
	}

	bool installed = workshop.GetAssembly().Contains(part);
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

	int columns = m_xray ? 7 : 6;
	ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

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
		HeadersRow({
			"The surface or dimension. Click a row for its readings and machining.",
			"The size it has to be, with its tolerance band.",
			"Your last reading of its size (the average of all stations). Stale once the part's been cut or swapped since.",
			"Your last surface finish reading, in Ra (lower is smoother).",
			"How your last reading compares to spec: in spec, or what's wrong with it.",
			"The tool for the job. Hold it and aim at the feature in the shop.",
			XRAY_TIP });

		for (const auto& feature : part->GetFeatures())
		{
			if (FeatureSection(part->GetType(), feature.name) != section)
				continue;
			ImGui::PushID(feature.name.c_str());
			ImGui::TableNextRow();
			auto reading = part->GetReading(feature.name);

			ImGui::TableSetColumnIndex(0);
			if (ImGui::Selectable(feature.name.c_str(), m_selectedFeature == feature.name, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
			{
				if (m_selectedFeature != feature.name)
				{
					m_selectedFeature = feature.name;
					m_selectedMachine = -1;
					m_lastMachiningMessage.clear();
				}
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
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	if (auto feature = part->FindFeature(m_selectedFeature))
	{
		ImGui::Spacing();
		ImGui::SeparatorText(feature->name.c_str());
		DrawFeatureDetail(workshop, *part, *feature);
		DrawMachiningPanel(workshop, *part, *feature);
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

void WorkshopUI::DrawPartsStore(Workshop& workshop)
{
	// Catalogue cards on the left, the order form for the selected part on the right.
	constexpr float CARD_WIDTH = 118.f;
	constexpr float CARD_HEIGHT = 112.f;
	constexpr float ICON_SIZE = 64.f;

	if (ImGui::BeginChild("Catalogue", { ImGui::GetContentRegionAvail().x * 0.55f, 0.f }, ImGuiChildFlags_Borders))
	{
		ImGui::TextDisabled("Parts ship to the roll-up door. Budget parts scatter wider; measure everything.");
		ImGui::Spacing();

		float available = ImGui::GetContentRegionAvail().x;
		int columns = (std::max)(1, static_cast<int>(available / (CARD_WIDTH + ImGui::GetStyle().ItemSpacing.x)));
		int column = 0;
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		for (auto type : ALL_PART_TYPES)
		{
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

			const char* name = PartTypeName(type);
			ImVec2 textSize = ImGui::CalcTextSize(name);
			drawList->AddText({ cardMin.x + (CARD_WIDTH - textSize.x) * 0.5f, cardMin.y + ICON_SIZE + 14.f }, IM_COL32(230, 230, 230, 255), name);
			std::string from = std::format("from ${:.0f}", PartFactory::Price(type, PartQuality::BUDGET, Workshop::SoldUsed(type) ? PartCondition::USED : PartCondition::NEW));
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

	PartIcons::Icon(m_order.type, 96.f);
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::Text("%s", PartTypeName(m_order.type));
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
	ImGui::TextDisabled("%s", PartIcons::Description(m_order.type));
	ImGui::PopTextWrapPos();
	ImGui::EndGroup();
	ImGui::Separator();

	int quality = static_cast<int>(m_order.quality);
	ImGui::RadioButton("Budget", &quality, static_cast<int>(PartQuality::BUDGET));
	ImGui::SameLine();
	ImGui::RadioButton("OEM", &quality, static_cast<int>(PartQuality::OEM));
	ImGui::SameLine();
	ImGui::RadioButton("Performance", &quality, static_cast<int>(PartQuality::PERFORMANCE));
	m_order.quality = static_cast<PartQuality>(quality);

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
	double quote = workshop.Quote(m_order);
	ImGui::BeginDisabled(quote > workshop.GetMoney());
	if (ImGui::Button(std::format("Order for ${:.0f}", quote).c_str(), { 200.f, 34.f }))
		workshop.Buy(m_order);
	ImGui::EndDisabled();
	if (quote > workshop.GetMoney())
	{
		ImGui::SameLine();
		ImGui::TextColored(COLOR_BAD, "Not enough money.");
	}

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
		DrawPartsStore(workshop);
		ImGui::EndTabItem();
	}
	if (category("Measuring Tools", StoreCategory::TOOLS))
	{
		DrawToolStore(workshop);
		ImGui::EndTabItem();
	}
	if (category("Machines", StoreCategory::MACHINES))
	{
		DrawMachineStore(workshop);
		ImGui::EndTabItem();
	}
	m_requestedCategory = std::nullopt;
	ImGui::EndTabBar();
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
		ImGui::TextDisabled("Bought tools hang on the tool board. Take them down and use them on the parts.");
		ImGui::Spacing();
		for (size_t i = 0; i < tools.size(); i++)
		{
			const auto& tool = tools[i];
			if (StoreCard(tool.name.c_str(), tool.name.c_str(), std::format("${:.0f}", tool.price), m_selectedTool == static_cast<int>(i), workshop.OwnsTool(i),
				[&](ImDrawList* drawList, ImVec2 min, float size) { ItemIcons::DrawTool(drawList, tool, min, size); },
				{ Ratings::ToolPrecision(tool), Ratings::ToolSpeed(tool) }))
				m_selectedTool = static_cast<int>(i);
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("ToolDetail", { 0.f, 0.f }, ImGuiChildFlags_Borders))
	{
		const auto& tool = tools[m_selectedTool];
		ItemIcons::ToolIcon(tool, 96.f);
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::Text("%s", tool.name.c_str());
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("%s", tool.description.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
		ImGui::Separator();

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
			ImGui::TextColored(COLOR_GOOD, "Owned. It's on the tool board or in your backpack.");
		else
		{
			ImGui::BeginDisabled(tool.price > workshop.GetMoney());
			if (ImGui::Button(std::format("Buy for ${:.0f}", tool.price).c_str(), { 200.f, 34.f }))
				workshop.BuyTool(m_selectedTool);
			ImGui::EndDisabled();
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
		ImGui::TextDisabled("Machines are set up in the shop when bought. Press F on one to move it.");
		ImGui::Spacing();
		for (size_t i = 0; i < machines.size(); i++)
		{
			const auto& machine = machines[i];
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
		ItemIcons::MachineIcon(machine, 96.f);
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::Text("%s", machine.name.c_str());
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("%s", machine.description.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
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
			ImGui::TextColored(COLOR_GOOD, "Owned and set up in the shop.");
		else
		{
			ImGui::BeginDisabled(machine.price > workshop.GetMoney());
			if (ImGui::Button(std::format("Buy for ${:.0f}", machine.price).c_str(), { 200.f, 34.f }))
				workshop.BuyMachine(m_selectedStoreMachine);
			ImGui::EndDisabled();
		}
	}
	ImGui::EndChild();
}

void WorkshopUI::DrawAssemblyTab(Workshop& workshop)
{
	auto missing = workshop.GetAssembly().MissingParts();
	if (missing.empty())
		ImGui::TextColored(COLOR_GOOD, "Long block complete. Check your clearances, then take it to the dyno.");
	else
		ImGui::TextDisabled("%zu positions still empty.", missing.size());

	if (ImGui::BeginChild("Slots", { 380.f, 0.f }, ImGuiChildFlags_Borders))
		DrawAssemblySlots(workshop);
	ImGui::EndChild();

	ImGui::SameLine();

	if (ImGui::BeginChild("Checks", { 0.f, 0.f }, ImGuiChildFlags_Borders))
		DrawAssemblyChecks(workshop);
	ImGui::EndChild();
}

void WorkshopUI::DrawAssemblySlots(Workshop& workshop)
{
	auto& assembly = workshop.GetAssembly();

	for (size_t slotIndex = 0; slotIndex < static_cast<size_t>(AssemblySlot::COUNT); slotIndex++)
	{
		auto slot = static_cast<AssemblySlot>(slotIndex);
		int count = assembly.SlotCount(slot);
		ImGui::SeparatorText(AssemblySlotName(slot));

		for (int index = 0; index < count; index++)
		{
			ImGui::PushID(static_cast<int>(slotIndex * 100 + index));
			auto installed = assembly.Get(slot, index);
			std::string label = count > 1 ? std::format("{} {}", AssemblySlotName(slot), index + 1) : AssemblySlotName(slot);
			std::string preview = installed ? std::format("#{} {}", installed->GetId(), installed->GetName()) : "(empty)";

			ImGui::SetNextItemWidth(-130.f);
			if (ImGui::BeginCombo(label.c_str(), preview.c_str()))
			{
				if (ImGui::Selectable("(empty)", !installed))
					workshop.RemovePart(slot, index);

				for (const auto& part : workshop.GetInventory(EngineAssembly::SlotPartType(slot)))
				{
					if (part->GetCondition() == PartCondition::DAMAGED || !IsUsable(*part) || (assembly.Contains(part) && part != installed))
						continue;
					if (ImGui::Selectable(std::format("#{} {}", part->GetId(), part->GetName()).c_str(), part == installed))
						workshop.InstallPart(slot, index, part);
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
		}
	}
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

void WorkshopUI::DrawDynoTab(Workshop& workshop)
{
	const auto& assembly = workshop.GetAssembly();
	bool complete = assembly.IsComplete();

	ImGui::BeginDisabled(!complete || workshop.GetMoney() < 50.0 || m_pullActive);
	if (ImGui::Button("Strap it down and pull to redline ($50)"))
	{
		const auto& pull = workshop.RunDyno();
		if (pull.ran && workshop.IsDynoResultCurrent() && !pull.curve.empty())
		{
			m_pullActive = true;
			m_pullTime = 0.f;
			m_spinDownTime = 0.f;
			m_pullStartRpm = pull.curve.front().rpm;
			m_pullEndRpm = pull.failed ? pull.failureRpm : pull.curve.back().rpm;
			m_pullFailed = pull.failed;
		}
	}
	ImGui::EndDisabled();
	if (!complete)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("Finish the long block first.");
	}

	const auto& result = workshop.GetLastDynoResult();
	if (!result.ran)
		return;

	if (!workshop.IsDynoResultCurrent())
		ImGui::TextColored(COLOR_WARN, "The engine has changed since this pull.");

	// While the pull runs, show it live: the needle sweeps and the curves draw as it goes.
	float liveRpm = m_pullActive ? GetLiveRpm() : 1e9f;
	int shownPoints = 0;
	for (const auto& point : result.curve)
		if (point.rpm <= liveRpm)
			shownPoints++;

	ImGui::Spacing();
	if (m_pullActive)
	{
		const DynoPoint* now = shownPoints > 0 ? &result.curve[shownPoints - 1] : nullptr;
		ImGui::SetWindowFontScale(1.6f);
		ImGui::TextColored(COLOR_WARN, "%5.0f rpm", liveRpm);
		ImGui::SetWindowFontScale(1.f);
		ImGui::Text("Torque %.1f Nm   Power %.1f hp", now ? now->torqueNm : 0.f, now ? now->powerKw * 1.341f : 0.f);
		ImGui::ProgressBar((liveRpm - m_pullStartRpm) / (std::max)(1.f, m_pullEndRpm - m_pullStartRpm), { -1.f, 0.f }, "");
	}
	else
	{
		if (result.failed)
			ImGui::TextColored(COLOR_BAD, "FAILED at %.0f rpm", result.failureRpm);

		ImGui::Text("Peak power   %.1f hp (%.1f kW) @ %.0f rpm", result.peakPowerKw * 1.341, result.peakPowerKw, result.peakPowerRpm);
		ImGui::Text("Peak torque  %.1f Nm (%.1f lb-ft) @ %.0f rpm", result.peakTorqueNm, result.peakTorqueNm * 0.7376, result.peakTorqueRpm);
		ImGui::Text("Displacement %.0f cc   Compression %.2f:1   Oil pressure %.1f bar   Blow-by %.1f%%",
			result.displacementCc, result.compressionRatio, result.oilPressureBar, result.blowbyPercent);
		ImGui::Text("Health");
		ImGui::SameLine();
		ImGui::ProgressBar(static_cast<float>(result.healthScore / 100.0), { 200.f, 0.f });
	}

	if (!result.curve.empty())
	{
		float maxTorque = 0.f, maxPower = 0.f;
		for (const auto& point : result.curve)
		{
			maxTorque = (std::max)(maxTorque, point.torqueNm);
			maxPower = (std::max)(maxPower, point.powerKw);
		}
		// Plot against the full rpm range so the trace grows from left to right.
		std::vector<float> torque(result.curve.size(), 0.f), power(result.curve.size(), 0.f);
		for (int i = 0; i < shownPoints; i++)
		{
			torque[i] = result.curve[i].torqueNm;
			power[i] = result.curve[i].powerKw;
		}
		auto count = static_cast<int>(result.curve.size());
		ImGui::PlotLines("Torque (Nm)", torque.data(), count, 0, nullptr, 0.f, maxTorque * 1.1f, { 0.f, 120.f });
		ImGui::PlotLines("Power (kW)", power.data(), count, 0, nullptr, 0.f, maxPower * 1.1f, { 0.f, 120.f });
		ImGui::TextDisabled("%.0f - %.0f rpm", result.curve.front().rpm, result.curve.back().rpm);
	}

	if (m_pullActive)
		return;

	ImGui::SeparatorText("Dyno sheet");
	for (const auto& finding : result.findings)
	{
		ImVec4 color = finding.severity == FindingSeverity::FAILURE ? COLOR_BAD : finding.severity == FindingSeverity::WARNING ? COLOR_WARN : COLOR_DIM;
		ImGui::TextColored(color, "%s", finding.message.c_str());
	}

	double value = workshop.EstimateEngineValue();
	ImGui::Spacing();
	ImGui::BeginDisabled(value <= 0.0);
	if (ImGui::Button(std::format("Sell the engine (${:.0f})", value).c_str()))
		workshop.SellEngine();
	ImGui::EndDisabled();
	if (value <= 0.0)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("Buyers want a clean pull of the engine as it sits now.");
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
