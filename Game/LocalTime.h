#pragma once

#include <chrono>
#include <ctime>
#include <string>

// A moment as the local clock shows it, in strftime's format ("%Y-%m-%d %H:%M"). Through the C
// library, since not every standard library has C++20's time zones (Apple's hasn't).
inline std::string LocalTimeText(std::chrono::system_clock::time_point when, const char* format)
{
	std::time_t seconds = std::chrono::system_clock::to_time_t(when);
	std::tm local = {};
#ifdef _WIN32
	localtime_s(&local, &seconds);
#else
	localtime_r(&seconds, &local);
#endif
	char text[64] = {};
	std::strftime(text, sizeof(text), format, &local);
	return text;
}
