#include "Ratings.h"

#include <algorithm>
#include <cmath>

namespace Ratings
{
	namespace
	{
		// 1 at `best`, 0 at `worst`, logarithmic in between.
		float LogScore(double value, double best, double worst)
		{
			double score = 1.0 - std::log(value / best) / std::log(worst / best);
			return static_cast<float>(std::clamp(score, 0.05, 1.0));
		}

		float LinearScore(double value, double best, double worst)
		{
			double score = 1.0 - (value - best) / (worst - best);
			return static_cast<float>(std::clamp(score, 0.05, 1.0));
		}

		ImU32 ScoreColor(float value)
		{
			if (value > 0.66f)
				return IM_COL32(110, 210, 120, 255);
			if (value > 0.33f)
				return IM_COL32(230, 190, 80, 255);
			return IM_COL32(230, 100, 90, 255);
		}
	}

	float ToolPrecision(const MeasuringTool& tool)
	{
		// Hand tools: only a torque wrench has a precision worth rating.
		if (tool.IsHandTool())
			return tool.handKind == HandToolKind::TORQUE_WRENCH ? LogScore(tool.torqueError, 0.015, 0.15) : 0.5f;
		// Each kind of measurement has its own idea of good: a micron, a newton, a tenth of a cc.
		if (tool.measuresFinish)
			return LogScore(tool.sigma, 0.01, 0.12);
		if (!tool.kinds.empty() && tool.kinds[0] == FeatureKind::FORCE)
			return LogScore(tool.sigma, 1.0, 12.0);
		if (!tool.kinds.empty() && tool.kinds[0] == FeatureKind::VOLUME)
			return LogScore(tool.sigma, 0.03, 0.36);
		return LogScore(tool.sigma, 0.001, 0.012);
	}

	float ToolSpeed(const MeasuringTool& tool)
	{
		return LinearScore(tool.hoursPerUse, 0.05, 0.4);
	}

	float MachineAccuracy(const Machine& machine)
	{
		switch (machine.operation)
		{
			case MachineOperation::CUT_SEAT:	return LogScore(machine.sigma, 0.02, 0.2);
			case MachineOperation::FILE_RING:	return LogScore(machine.sigma, 0.002, 0.02);
			default:							return LogScore(machine.sigma, 0.001, 0.012);
		}
	}

	float MachineSpeed(const Machine& machine)
	{
		// Rate-based machines are judged on a typical 0.05 mm job.
		double hours = machine.hoursPerUse + machine.hoursPerMm * 0.05;
		return LinearScore(hours, 0.1, 1.2);
	}

	float MachineFinish(const Machine& machine)
	{
		if (machine.finishRa <= 0.0)
			return -1.f;
		return LogScore(machine.finishRa, 0.1, 3.0);
	}

	void Bar(const char* label, float value, float width)
	{
		ImGui::TextUnformatted(label);
		ImGui::SameLine(110.f);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ScoreColor(value));
		ImGui::ProgressBar(value, { width, ImGui::GetTextLineHeight() }, "");
		ImGui::PopStyleColor();
	}

	void MiniBar(ImDrawList* drawList, ImVec2 min, float width, float value)
	{
		constexpr float HEIGHT = 4.f;
		drawList->AddRectFilled(min, { min.x + width, min.y + HEIGHT }, IM_COL32(25, 25, 30, 255), 2.f);
		drawList->AddRectFilled(min, { min.x + width * value, min.y + HEIGHT }, ScoreColor(value), 2.f);
	}
}
