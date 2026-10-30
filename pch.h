#pragma once

// Precompiled header: the big, stable headers that nearly every game and engine file pulls in,
// parsed once per build instead of once per file. The project forces it into every game and engine
// source (Configuration Properties > C/C++ > Advanced > Forced Include File), so no file includes it
// by hand. Only add headers here that rarely change: changing this file rebuilds everything.

// Windows (through Vulkan's Win32 surface) would otherwise define min and max as macros everywhere.
#define NOMINMAX

// The same setup Graphics.h uses, so vulkan.hpp is only ever parsed one way.
#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
