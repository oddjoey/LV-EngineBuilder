# 🔧 LV: Engine Builder

A first-person engine building game, made on **[Lordi](https://github.com/oddjoey/LV)**, a custom C++ / Vulkan game engine. Lordi is included as a git submodule in `LV/`.

You run a small garage. You start with a worn-out **LV-20 Inline-4** core and **$4000**. Measure every part down to the micron, machine what's out of spec, buy what's missing, put the engine together with the right clearances, and run it on the dyno. Nothing is shown to you that you haven't measured yourself: the parts have real (hidden) geometry, and your tools have real resolution and error.

---

## 📑 Contents

- [Getting started](#-getting-started)
- [How to play](#-how-to-play)
- [Controls](#-controls)
- [The workshop screens](#-the-workshop-screens)
- [Parts, tolerances and measuring](#-parts-tolerances-and-measuring)
- [Machines](#-machines)
- [Tools](#-tools)
- [The shop and the world](#-the-shop-and-the-world)
- [Settings](#-settings)
- [Saving](#-saving)
- [Developer tools](#-developer-tools)
- [How the game uses Lordi](#-how-the-game-uses-lordi)
- [Project layout](#-project-layout)
- [Known limitations](#-known-limitations)
- [License](#-license)

---

## 🚀 Getting started

### Requirements

- **Windows 10/11, x64**
- **Visual Studio 2026** (platform toolset v145) with the C++ desktop workload
- A discrete GPU with **Vulkan 1.4** support
- An **NVIDIA RTX** GPU for DLSS (optional: the game runs without it)

### Libraries

The game builds against Lordi's libraries in `LV/Libraries/`. ImGui, glm and magic_enum are submodules of Lordi and arrive with `--recurse-submodules`; the others have to be put there by hand (that folder is git-ignored in Lordi apart from its submodules).

| Library | Folder | Used for |
|---|---|---|
| [Vulkan SDK](https://vulkan.lunarg.com/) | `LV/Libraries/vulkan` | Graphics API, validation layers, `glslc` shader compiler |
| [GLFW 3.4](https://www.glfw.org/) | `LV/Libraries/glfw-3.4` | Window, input, fullscreen |
| [Dear ImGui](https://github.com/ocornut/imgui) | `LV/Libraries/imgui` (submodule) | All UI: HUD, menus, workshop screens |
| [glm](https://github.com/g-truc/glm) | `LV/Libraries/glm` (submodule) | Vector and matrix maths |
| [magic_enum](https://github.com/Neargye/magic_enum) | `LV/Libraries/magic_enum` (submodule) | Enum names in logs |
| [stb](https://github.com/nothings/stb) | `LV/Libraries/stb` | Image loading |
| [termcolor](https://github.com/ikalnytskyi/termcolor) | `LV/Libraries/termcolor` | Coloured console logs |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) | `LV/Libraries/tinyobjloader` | `.obj` models |
| [ufbx](https://github.com/ufbx/ufbx) | `LV/Libraries/ufbx` | `.fbx` models |
| [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS) | `LV/Libraries/DLSS` | DLSS Super Resolution (310.9.16) |

### Setup

1. Clone with the submodules (this brings Lordi into `LV/`, and Lordi's own submodules with it):
   ```bash
   git clone --recurse-submodules https://github.com/oddjoey/LV-EngineBuilder.git
   cd LV-EngineBuilder
   ```
   Already cloned without them? Run `git submodule update --init --recursive`.
2. Put the Vulkan SDK in `LV/Libraries/vulkan` and GLFW 3.4 in `LV/Libraries/glfw-3.4` (with `lib-vc2026`), plus stb, termcolor, tinyobjloader and ufbx. Set `VULKAN_SDK` to the Vulkan folder.
3. Get the DLSS SDK. Only the headers and Windows libraries are needed:
   ```bash
   cd LV/Libraries
   git clone --depth 1 --filter=blob:none --sparse https://github.com/NVIDIA/DLSS DLSS
   cd DLSS
   git sparse-checkout set --no-cone /include /lib/Windows_x86_64 /LICENSE.txt /README.md
   ```
   Already have these in another Lordi checkout? Link them instead of copying. In `cmd`, from `LV\Libraries`: `mklink /J vulkan "<that checkout>\Libraries\vulkan"`, and the same for the others.
4. Open `LV-EngineBuilder.sln`, pick **Debug | x64** (or Release), and build. A post-build step copies `nvngx_dlss.dll` next to the executable.
5. Run it from Visual Studio (the debugger starts in the repository root), or start `x64\Debug\LV-EngineBuilder.exe` with the repository root as the working directory. Lordi's shaders are loaded from `LV/` (`main.cpp` sets `GraphicsSettings::sEngineRoot = "LV/"`); the game's own files are read and written in the root.

### Updating Lordi

The game is pinned to one Lordi commit. To move to a newer one:

```bash
cd LV
git pull origin master
cd ..
git add LV
git commit -m "Update Lordi"
```

Engine work (rendering, shaders, DLSS) belongs in the [Lordi repository](https://github.com/oddjoey/LV). Shaders are compiled there, with `Engine/Graphics/Shaders/compile.bat`.

---

## 🎮 How to play

The loop:

1. **Look around the garage.** Your core's parts are laid out in order on the parts shelf: block, crank, head and cam on the bottom board, then the rods, valves and springs. The tool board is bare and there are no machines: everything is bought at the parts computer (E at the desk), so spend the $4000 carefully.
2. **Measure.** Take a tool in hand, aim at a part, and left-click. Bores, journals, decks, valves, springs: everything has a spec.
3. **Inspect on the bench.** Set a part on the workbench and press E there. The Bench screen lists every feature, its spec, your reading and the verdict.
4. **Machine** what's worn: set the part up in the right machine with E and cut it from that machine's screen. Material only comes off.
5. **Buy** what's missing or scrap at the parts computer, the only place to buy anything. Everything you buy (parts, tools, machines, engine stands) arrives in a box or crate by the roll-up door; open it with E.
6. **Assemble** on an engine stand by carrying each part over and pressing E, in the real [build order](#build-order), then check the clearances that only exist once parts are together (bearing oil clearance, ring gap, deck clearance, installed spring height).
7. **Run it on the dyno.** You get a power curve, oil pressure, blow-by, compression and a list of findings. A bad build fails on the pull.
8. **Sell** the finished engine (priced on its dyno pull), or keep improving it.

### Several engines at once

Each engine is built on its own engine stand. You start with one; buy more (up to four, $250 each) and whole used cores to fill them under **Engines** at the parts computer. New stands are set up next to the first and can be moved with F.

- **E at a stand** makes its engine the one you're working on: the Engine Stand screen shows that stand's engine, and the Dyno screen has an engine picker when you have more than one.
- Parts are shared: any part can go into any engine, but only one at a time, and a part fitted to one engine isn't offered for another.
- Assembly checks (Plastigauge, feelers, dial indicator) work on whichever stand you aim at. Each engine keeps its own readings and its own dyno sheet.
- The **Backpack** checklist has a picker too: per engine, how many of each part are fitted, how many spares are loose in the shop, and what you still need to buy.

### Build order

Parts go on in the order they would in a real shop, and come off in reverse: nothing can be removed while something is fitted on top of it. Aiming at the stand with a part in hand says where it will go, or what has to go on first ("Not yet: The Main Bearing 1 has to go on first.").

1. **Block.**
2. **Main bearings**, then the **crankshaft** (all the mains first).
3. Per cylinder, once the crank is in: the **piston**, then its **ring** and **connecting rod**, then the **rod bearing**.
4. **Head gasket**, once the whole bottom end is done, then the **cylinder head**.
5. **Valves** into the head, each **valve spring** over its valve.
6. **Camshaft** last, once the head, every valve and every spring are in.

Older saves load whatever was fitted, even if out of order.

---

## ⌨️ Controls

| Key | Action |
|---|---|
| W A S D | Walk |
| Shift | Run |
| Space | Jump |
| Mouse | Look around |
| E | Pick up, open deliveries, use stations, fit parts, open/close the roll-up door |
| Left click | Measure with the tool in your hand (otherwise the same as E) |
| Right click | Set what you're holding down where you're aiming |
| G | Drop what you're holding |
| Q | Stow what you're holding in your backpack (or, when it won't fit, on the parts shelf or tool board) |
| 1 – 0 | Take a hotbar item in hand, or put it back |
| Tab | Open/close your inventory (closes any workshop screen) |
| F | Move equipment (machines, bench, stands, shelf, tool board) or an unopened delivery. R rotates, F cancels |
| R (hold) | Turn over the engine you're looking at, by hand |
| Z / X (hold) | Roll the engine you're looking at on its stand to get at it from underneath |
| Esc | Close the screen, or open the pause menu |
| F3 | Developer menu (with Lordi's debug panels) |

In the backpack: drag to move, **R** while dragging to turn an item, drag out of the window to drop, right-click for options.

---

## 🖥️ The workshop screens

One screen at a time, with no tabs to switch between: Tab opens the inventory, and each station's screen opens only with E at that station. The window is named after what it shows. Tab and Esc go back to the shop floor. The store is separate: see [The parts computer](#the-parts-computer).

| Screen | Opened by | What it shows |
|---|---|---|
| **Inventory** | Tab | A Tarkov-style grid (10 × 6 cells) and the hotbar. Beside it: details of the item under the cursor (material, quality, condition, size class, value, how much of it you've measured; for tools, precision, speed and resolution), a **Tidy up** button, and the engine checklist with a "still to buy" list. |
| **Workbench** | E at the workbench | Only parts actually sitting on the bench. Every feature grouped by area, with spec, your reading, finish, verdict and which tool to measure it with. Click a feature for the full spec and readings. |
| **Machine** (named after it) | E at a machine | The machine (operation, finish it leaves, time), the part set up in it, and only the features that machine can cut, each marked green, red or grey by your last reading. Then the cut itself: target size (with one-click size classes), time estimate from your own reading, and the button. |
| **Engine Stand** | E at a stand (without a part in hand) | Read-only, like the workbench: that stand's engine, what can be fitted next, and every fitted part by position. Click a part for its features and your readings, or **Engine checks** for the assembled checks: oil clearances, ring gaps, piston-to-wall, deck clearance, compression, valve installed height and spring pressure. Parts are fitted and removed only at the stand itself (see [Build order](#build-order)). |
| **Dyno Cell** | E at the dyno | The pull for the active engine: a live RPM sweep, torque and power curves, oil pressure, blow-by, compression, findings and the sell button. |

Station screens have the shop status along the top and a log at the bottom that records everything that happens, with the shop clock. The inventory shows only the backpack.

### The parts computer

E at the computer desk opens the store in a window of its own; it can't be reached any other way (the tool board only takes tools back). Four categories:

- **Parts**: every part, by quality, condition (new, or used for blocks, cranks, heads, rods and cams) and size class.
- **Engines**: engine stands, whole used cores, and a table of where each of your engines is up to.
- **Measuring Tools** and **Machines**, each with a description and ratings.

### Feature details

Click a feature (on the Workbench or a machine's screen) for two side-by-side panels:

- **Spec**: diameter or size range, each oversize class with its exact size, max taper, max out-of-round, and the finish range.
- **Your last reading**: diameter (with the size class it falls in), taper, out-of-round, finish, the verdict and the tool used. Each value is green or red against spec, and greyed out if the part has been cut since.
- **Gauge readings** (round features): every reading by depth (top, middle, bottom) on the thrust and pin axes, with out-of-round at each depth.

---

## 📏 Parts, tolerances and measuring

Every part is generated (`Game/Simulation/PartFactory`) with true geometry: sizes, taper, out-of-round, flatness, runout, surface finish and wear, according to its **quality** (budget, OEM or performance), **condition** (new, used, damaged) and **size class** (standard, or oversize/undersize). Used parts are worn; damaged ones carry a reason.

A **feature** (`Game/Simulation/Feature.h`) is one measurable thing on a part: a bore, a journal, a deck, a valve stem, a spring force. Each has:

- a **spec** (tolerance band), plus allowed **size classes** for things that can be machined oversize/undersize;
- limits for **taper**, **out-of-round** and **surface finish** (Ra) where they matter;
- the **operations** that can cut it.

Round features are read at three depths on two axes (thrust and pin). The verdict lists every issue: undersize, oversize, taper, out-of-round, rough, glazed, warped, runout, or beyond the last size class (scrap or sleeve).

**Measuring is physical.** Take the tool in hand and aim at the part; markers show everything that tool can measure from where you stand. Each reading has the tool's resolution and repeatability error, so a $40 caliper and a $450 bore gauge give genuinely different answers. Readings go stale when the part is machined.

Some clearances only exist once parts are assembled. These are checked on the engine stand: oil clearance (Plastigauge), ring gap (ring squared in its bore, feeler gauge), deck clearance (dial indicator at TDC) and installed spring height.

The engine's true clearances are what the **dyno** runs on, whether or not you measured them.

Units can be switched between metric and imperial in the workshop window or in settings.

---

## ⚙️ Machines

Parts are only machined while set up in the right machine: hold the part and press E at the machine. Each cut costs shop time and can't be undone.

| Machine | Price | Does |
|---|---|---|
| Hand Ring Filer | $150 | Opens piston ring end gaps |
| Drill-Driven Cylinder Hone | $300 | Hones bores; cheap, but geometry depends on your hands |
| Torque-Plate Power Hone | $4500 | Hones with a torque plate, so the bore is shaped as it will be when assembled |
| Portable Boring Bar | $3500 | Bores oversize in one pass; leaves a rough finish, so hone afterwards |
| Crankshaft Grinder | $12000 | Regrinds journals undersize (you'll need undersize bearings) |
| Belt Journal Polisher | $600 | Takes a few microns off and leaves a mirror finish |
| Surface Grinder | $6000 | Decks blocks and heads flat (every cut changes deck height or chamber volume) |
| Valve Guide Reamer Set | $180 | Reams worn guides straight and round for oversize-stem valves |
| Valve Seat Cutter | $900 | Three-angle cutters; trues the seat but sinks it a little deeper each cut |
| Valve Refacer | $1400 | Regrinds valve faces true; eats margin and the valve sits deeper |

Hones charge time by the amount of stock removed, so measure first to get a time estimate. The shop starts with no machines. Bought ones arrive crated by the door; opening the crate sets the machine up there, and F moves it.

---

## 📐 Tools

| Tool | Price |
|---|---|
| Digital Caliper | $40 |
| Telescoping Gauge | $60 |
| Small Hole Gauge Set | $45 |
| Straightedge & Feelers | $80 |
| Plastigauge | $15 |
| Feeler Gauge Set | $25 |
| Outside Micrometer Set | $350 |
| Dial Bore Gauge | $450 |
| Dial Small-Bore Gauge | $380 |
| Runout Gauge & V-Blocks | $140 |
| Valve Spring Tester | $250 |
| Burette & CC Plate | $90 |
| Surface Profilometer | $1500 |
| Dial Indicator & Deck Bridge | $120 |
| Installed Height Micrometer | $90 |

Tools are rated for **precision** and **speed**. The bench always suggests the best tool you own for each feature. The shop starts with none: buy them at the parts computer. Bought tools are delivered in a box by the door; once unpacked they can hang on the tool board, and E at the board hangs the tool in your hand back up.

---

## 🏠 The shop and the world

- **The garage** (`Game/World/GarageLayout.h` holds every dimension): metal siding, a pitched shingle roof, ten windows, six ceiling lamps and a roll-up door (E to open/close; it's open at the start).
- **Equipment**: workbench, parts shelf (no E action: its boards are surfaces like any other, so any part is set down on them with RMB and stays where it lands; parts that won't fit in the backpack or get bumped off the bench, a machine or a stand go in the first gap from the left on the lowest board they fit), tool board, engine stand, machines, dyno cell and parts computer. All of it can be moved with F, and whatever is sitting on it (parts, tools, deliveries, even things stacked on those) goes along.
- **Shadows**: the sun casts shadows outside and through the windows, and the six ceiling lamps each cast their own, so machines, parts, tools and equipment throw shadows inside the shop too.
- **Outside**: a fenced yard with trees, a driveway with a parked car, bins and oil drums, a sidewalk, a road with lane markings, street lights and houses across the street.
- **Physics**: items fall, land and stack, and can be set down anywhere. Walls stop you and anything you throw. You can't reach through walls or windows.
- **Holding things**: whatever is in your hand is held first person, low and to the right of your view, small parts and tools stood upright on their longest side and big parts (block, head, crank, cam) carried flat, and moves with the camera.
- **Deliveries** arrive as boxes and crates by the door, stacked up to three high in piles. Each one has a tag with an icon for every kind of thing inside it and how many. Open one with E: parts and tools are laid out on the floor beside the pile, and a machine or engine stand is set up where its crate was. Before opening, F picks a delivery up to carry it somewhere else: set it on the floor, a bench or another delivery (anything stacked on it drops into its place).
- **Day and night**: the sun crosses the sky (sunrise 06:00, sunset 20:00), the sky and fog change colour, street and porch lights come on at dusk, and there's moonlight at night. A full day takes 30 real minutes by default. The clock is in the top right.
- **Custom models**: any built-in model can be replaced with your own `.obj` or `.fbx` through `Resources/Models/models.cfg` (see `Resources/Models/README.md` for the names and options).

---

## 🎛️ Settings

Esc → **Settings**. Changes apply immediately and are saved to `game_settings.cfg` when you leave the menu.

**Game:** mouse sensitivity, invert Y, field of view, metric/imperial, control hints, FPS counter.

**Video:**

| Setting | Notes |
|---|---|
| Display mode | Windowed, borderless fullscreen or exclusive fullscreen |
| Resolution | Any mode your monitor supports (borderless always uses the desktop's) |
| V-Sync, frame rate limit | Limit 0 = unlimited |
| NVIDIA DLSS | Off, DLAA, Quality, Balanced, Performance, Ultra Performance. Greyed out (with the reason) on GPUs that can't run it. Shows the resolution it renders at. |
| Anti-aliasing (MSAA) | 1×/2×/4×/8×, takes effect on restart, not used while DLSS is on |
| Shadows | Off / Low / Medium / High (sun: 1024 / 2048 / 4096 shadow maps; lamps: half that, 512 to 2048). Off turns the lamps' shadows off too |
| Shadow distance, view distance, brightness | |

**Time of day:** set the time, turn the cycle on/off, and set the length of a day.

---

## 💾 Saving

Pause menu → **Save Game** / **Load Game** / **New Game**, and **Save and Quit** on the quit screen. The game loads your save automatically when it starts.

The save is `savegame.lvsave`, a versioned text file (`LVSAVE 3`; older saves still load: version 1 as one engine, and the pallet rack from versions 1 and 2 is dropped) holding:

- money, shop hours and the log;
- every part's true geometry, condition, value, name, your readings, and any machining done to it;
- every engine: what's fitted to it, its assembly checks and its last dyno pull, and which one you were working on;
- where every part, tool and delivery is, the backpack layout and hotbar;
- equipment positions, the door, the time of day, and where you're standing.

The whole file is read and checked before anything is replaced, so a damaged save shows an error and leaves your game alone. Saves are written to a temporary file and renamed, so a crash can't leave half a save. `savegame.lvsave` and `game_settings.cfg` are git-ignored.

---

## 🛠️ Developer tools

**F3** opens the developer menu (nothing in it is saved). It has two tabs.

**Game**
- **X-ray part geometry**: the workshop screens show each part's true geometry (in blue) next to your readings.
- **Bounding boxes**: colour-coded wireframes: walls (red), what blocks walking (magenta), surfaces things can be set on (green), equipment (cyan), deliveries (orange), parts and tools (yellow). The menu shows the legend.
- **Time of day**: a slider to jump to any time, presets (Dawn, Morning, Noon, Dusk, Night), and a time speed from 0x (clock stopped) to 200x.

**Lordi**: the engine's own debug panels (performance, the world and its objects), which used to be a separate window on Home.

Debug builds run with the Vulkan validation layers on and log everything to the console.

---

## 🧱 How the game uses Lordi

The game is a `Game` (`LV/Engine/Game.h`): `EngineBuilderGame` implements `OnLoadAssets`, `OnStart`, `OnUpdate`, `OnGUI` and `OnShutdown`, and `main.cpp` hands it to Lordi. Game code includes engine headers as `"LV/Engine/..."`. [Lordi's README](https://github.com/oddjoey/LV) covers the engine itself; this is what the game relies on:

| Engine feature | Used for |
|---|---|
| `MeshBuilder` and the model manager | Every model in the game is built in code (`Game/World/ShopAssets.cpp`, `ShopOutdoors.cpp`), unless replaced through `models.cfg` |
| Textures generated in code | Concrete, cast iron, aluminium, steel, wood, paint, plaster, pegboard, cardboard, siding, brick, shingles, grass, asphalt, bark, foliage, and machining finishes (honed, bored, glazed, worn, scored, ground, polished) that show on a feature's surface (`ProceduralTextures.cpp`) |
| Objects | Every part, tool, machine and piece of scenery; tint, emissive glow (lamps), glass, shadow casting |
| Scene lighting | `ShopLighting` drives the sun path, sky colours, fog, exposure, the garage's interior zone and all the lamps every frame; the shop lamps are shadowed lights |
| Shadows, DLSS, display mode, V-Sync, frame cap | The Video settings |
| ImGui | The HUD, the workshop screens, the backpack, the pause and dev menus |
| Input | First-person movement, hotkeys, cursor lock for menus |
| `ResetTemporalHistory` | Called after loading a save or starting a new game |

---

## 🗂️ Project layout

```
LV-EngineBuilder/
├── LV/                         Lordi, the engine (git submodule), with its Libraries/ folder
├── main.cpp                    Window/video settings from game_settings.cfg, then starts Lordi with the game
├── Game/
│   ├── EngineBuilderGame.*     Ties it all together: input modes, menus, saving, time of day
│   ├── GameSettings.*          The player's settings file
│   ├── Simulation/             The engine-building model: parts, features, tolerances, materials,
│   │                           tools, machines, assembly, dyno, the workshop (money, clock, store)
│   ├── World/                  The 3D shop: layout, models, procedural textures, lighting,
│   │                           physics, measuring, moving equipment, the HUD, the outdoors
│   ├── Inventory/              The backpack grid and hotbar
│   ├── UI/                     Workshop screens, backpack, pause menu, dev menu, icons, ratings
│   └── Save/                   Save and load
├── Resources/Models/           Custom model overrides (models.cfg)
└── LV-EngineBuilder.sln, .vcxproj
```

Files written while playing (both git-ignored): `game_settings.cfg` and `savegame.lvsave`.

---

## ⚠️ Known limitations

- MSAA changes need a restart.
- Held items can poke through a wall if you stand right against it.
- Only the shop lamps cast shadows; the street and porch lights don't (the inside/outside light zones keep them where they belong).
- DLSS needs an NVIDIA RTX GPU and a recent driver; on anything else the option is greyed out.

---

## 🧑‍💻 License

This project is released under the **MIT License**. The NVIDIA DLSS SDK used through Lordi is under NVIDIA's own license (`LV/Libraries/DLSS/LICENSE.txt`).
