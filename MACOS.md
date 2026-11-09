# Building on macOS

The game and both servers build on macOS 13.3 or later (Apple silicon or Intel) with CMake. Windows
keeps using `LV-EngineBuilder.sln`.

## What to install

1. **Xcode command line tools**: `xcode-select --install`
2. **The LunarG Vulkan SDK** for macOS, from <https://vulkan.lunarg.com/sdk/home#mac>. Tick
   *System Global Installation* in its installer, so the Vulkan loader finds MoltenVK (Vulkan on
   Metal) and, for Debug builds, the validation layers.
3. **Homebrew** packages: `brew install cmake glfw openssl@3`
4. **The header-only libraries** in `LV/Libraries/`, as the README's Libraries table says: ImGui, glm
   and magic_enum come with the submodules; put **stb**, **termcolor**, **tinyobjloader** and
   **ufbx** there by hand. (The Vulkan, GLFW and DLSS folders aren't needed on a Mac: the SDK and
   Homebrew provide the first two, and there's no DLSS.)

## Build and run

```sh
git submodule update --init          # the LV engine
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
cmake --build build -j
cd build && ./LV-EngineBuilder
```

Use `-DCMAKE_BUILD_TYPE=Debug` for the dev menu, the console cheats and the Vulkan validation
layers. The servers build to `build/Server/LV-EngineServer` and `build/Server/LV-AccountServer`;
Ctrl+C stops them and saves.

## What's different on a Mac

- **No DLSS.** It's NVIDIA's, on Windows. The setting stays off and the game renders at full
  resolution.
- **Sound** is mixed by the engine and played through Core Audio (XAudio2 on Windows).
- **Networking and encryption** use the BSD sockets and OpenSSL. Keys and signatures go over the
  wire in the same form as on Windows, so a Mac can join a Windows-hosted garage and the other
  way round.
- The shaders are the same compiled SPIR-V files as on Windows; nothing needs recompiling.
