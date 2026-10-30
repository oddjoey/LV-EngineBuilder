#pragma once

#include <chrono>
#include <map>
#include <string>

// Developer timing: how long each named part of a frame takes, averaged and written to the console
// every couple of seconds while it's switched on (dev menu). Costs nothing while it's off.
class FrameProfiler
{
public:
	static FrameProfiler& Get();

	bool IsEnabled() const;
	void SetEnabled(bool enabled);

	void Add(const char* section, float milliseconds);
	// Call once a frame: counts the frame and reports when it's time.
	void EndFrame();

	// Times its own lifetime into a section.
	class Scope
	{
	public:
		explicit Scope(const char* section);
		~Scope();
	private:
		const char* m_section;
		std::chrono::steady_clock::time_point m_start;
		bool m_enabled;
	};
private:
	bool m_enabled = false;
	std::map<std::string, float> m_totals = {};
	int m_frames = 0;
	std::chrono::steady_clock::time_point m_lastReport = {};
};

// Two steps so __LINE__ becomes the number, letting one function hold several scopes.
#define PROFILE_SCOPE_JOIN2(a, b) a##b
#define PROFILE_SCOPE_JOIN(a, b) PROFILE_SCOPE_JOIN2(a, b)
#define PROFILE_SCOPE(name) FrameProfiler::Scope PROFILE_SCOPE_JOIN(profileScope, __LINE__)(name)
