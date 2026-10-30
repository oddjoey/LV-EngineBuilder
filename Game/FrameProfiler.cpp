#include "FrameProfiler.h"

#include <format>
#include <iostream>

FrameProfiler& FrameProfiler::Get()
{
	static FrameProfiler profiler;
	return profiler;
}

bool FrameProfiler::IsEnabled() const
{
	return m_enabled;
}

void FrameProfiler::SetEnabled(bool enabled)
{
	m_enabled = enabled;
	m_totals.clear();
	m_frames = 0;
	m_lastReport = std::chrono::steady_clock::now();
}

void FrameProfiler::Add(const char* section, float milliseconds)
{
	if (m_enabled)
		m_totals[section] += milliseconds;
}

void FrameProfiler::EndFrame()
{
	if (!m_enabled)
		return;
	m_frames++;
	auto now = std::chrono::steady_clock::now();
	float elapsed = std::chrono::duration<float>(now - m_lastReport).count();
	if (elapsed < 2.f)
		return;

	std::string report = std::format("[profile] {:.0f} fps, per frame:", m_frames / elapsed);
	for (const auto& [section, total] : m_totals)
		report += std::format("  {} {:.3f} ms", section, total / m_frames);
	std::cout << report << std::endl;

	m_totals.clear();
	m_frames = 0;
	m_lastReport = now;
}

FrameProfiler::Scope::Scope(const char* section)
	: m_section(section), m_start(std::chrono::steady_clock::now()), m_enabled(FrameProfiler::Get().IsEnabled())
{
}

FrameProfiler::Scope::~Scope()
{
	if (m_enabled)
		FrameProfiler::Get().Add(m_section, std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - m_start).count());
}
