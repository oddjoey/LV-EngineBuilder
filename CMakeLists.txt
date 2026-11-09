# LV: Engine Builder on macOS (Apple silicon or Intel), with the garage and account servers.
# Windows builds with LV-EngineBuilder.sln instead. See MACOS.md for what to install first.
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ./build/LV-EngineBuilder
cmake_minimum_required(VERSION 3.24)
project(LV-EngineBuilder LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()
# The engine and game test _DEBUG for validation layers, the dev menu and cheats, as MSVC defines it.
add_compile_definitions($<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)

set(LORDI ${CMAKE_SOURCE_DIR}/LV)
set(LIBRARIES ${LORDI}/Libraries)

# The LunarG Vulkan SDK (it brings MoltenVK, the loader and the validation layers), and from
# Homebrew: glfw and openssl@3.
find_package(Vulkan REQUIRED)
find_package(glfw3 3.4 REQUIRED)
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)

# ---- The game

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Game/*.cpp)
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS ${LORDI}/Engine/*.cpp)
set(IMGUI_SOURCES
	${LIBRARIES}/imgui/imgui.cpp
	${LIBRARIES}/imgui/imgui_demo.cpp
	${LIBRARIES}/imgui/imgui_draw.cpp
	${LIBRARIES}/imgui/imgui_tables.cpp
	${LIBRARIES}/imgui/imgui_widgets.cpp
	${LIBRARIES}/imgui/misc/cpp/imgui_stdlib.cpp
	${LIBRARIES}/imgui/backends/imgui_impl_glfw.cpp
	${LIBRARIES}/imgui/backends/imgui_impl_vulkan.cpp)

add_executable(LV-EngineBuilder
	${CMAKE_SOURCE_DIR}/main.cpp
	${GAME_SOURCES}
	${ENGINE_SOURCES}
	${IMGUI_SOURCES}
	${LIBRARIES}/ufbx/ufbx.c)

target_include_directories(LV-EngineBuilder PRIVATE
	${CMAKE_SOURCE_DIR}
	${LIBRARIES}/imgui
	${LIBRARIES}/glm
	${LIBRARIES}/magic_enum/include/magic_enum
	${LIBRARIES}/stb
	${LIBRARIES}/termcolor/include/termcolor
	${LIBRARIES}/tinyobjloader
	${LIBRARIES}/ufbx
	${LIBRARIES}/DLSS/include)

# Like the Visual Studio project: pch.h goes into every game and engine source, except the ones built
# without it there (the networking, which sets its own platform headers up, and the libraries).
set(WITHOUT_PCH
	${LORDI}/Engine/Networking/Socket.cpp
	${LORDI}/Engine/Networking/Crypto.cpp
	${IMGUI_SOURCES})
set(WITH_PCH ${CMAKE_SOURCE_DIR}/main.cpp ${GAME_SOURCES} ${ENGINE_SOURCES})
list(REMOVE_ITEM WITH_PCH ${WITHOUT_PCH})
set_source_files_properties(${WITH_PCH} PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_SOURCE_DIR}/pch.h")

target_link_libraries(LV-EngineBuilder PRIVATE
	Vulkan::Vulkan
	glfw
	OpenSSL::Crypto
	"-framework AudioToolbox"
	"-framework CoreFoundation")

# It runs from its own folder: the shaders and Resources go next to it.
add_custom_command(TARGET LV-EngineBuilder POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:LV-EngineBuilder>/LV/Engine/Graphics/Shaders/Compiled
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${LORDI}/Engine/Graphics/Shaders/Compiled $<TARGET_FILE_DIR:LV-EngineBuilder>/LV/Engine/Graphics/Shaders/Compiled
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Resources $<TARGET_FILE_DIR:LV-EngineBuilder>/Resources
	COMMENT "Copying the shaders and Resources next to the game")

# ---- The servers

set(NETWORKING_SOURCES ${LORDI}/Engine/Networking/Socket.cpp ${LORDI}/Engine/Networking/Crypto.cpp)

add_executable(LV-EngineServer
	${CMAKE_SOURCE_DIR}/Server/main.cpp
	${CMAKE_SOURCE_DIR}/Server/GarageServer.cpp
	${CMAKE_SOURCE_DIR}/Server/Profiles.cpp
	${NETWORKING_SOURCES})
target_link_libraries(LV-EngineServer PRIVATE OpenSSL::Crypto)
set_target_properties(LV-EngineServer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Server)

add_executable(LV-AccountServer
	${CMAKE_SOURCE_DIR}/AccountServer/main.cpp
	${CMAKE_SOURCE_DIR}/AccountServer/AccountServer.cpp
	${CMAKE_SOURCE_DIR}/AccountServer/Accounts.cpp
	${NETWORKING_SOURCES})
target_link_libraries(LV-AccountServer PRIVATE OpenSSL::Crypto)
set_target_properties(LV-AccountServer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Server)
