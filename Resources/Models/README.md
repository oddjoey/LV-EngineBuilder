# Custom models

Every part, machine and prop in the shop is built from simple shapes in code (`Game/World/ShopAssets.cpp`).
Any of them can be swapped for a real model without touching code:

1. Drop the `.obj` or `.fbx` (and an optional texture image) into this folder.
2. Add a line to `models.cfg` naming the model to replace and the file to use.
3. Start the game. The console prints `Loaded custom model for ...`, or a warning and falls back to
   the built-in shape if something's wrong.

## How a model has to sit

Each model is placed by its **origin**, in **metres**, **Y up**. The engine's crankshaft axis runs along
**+X**. Parts are positioned on the engine stand, shelves and machines from their origin, so get the
origin right and everything lines up. (Shelf and bench placement uses the model's bounding box, so only the
engine stand cares about the exact origin.)

| Model name | Origin | Orientation / size (built-in) |
|---|---|---|
| `Block` | Centre of the deck face (top of the block) | Block hangs below the origin; bores along Y at X = -0.1425, -0.0475, 0.0475, 0.1425. 0.41 x 0.25 x 0.24 m |
| `Cylinder Head` | Centre of the head's deck face (bottom) | Head sits above the origin. Intake ports on -Z, exhaust on +Z. Spring seats at Y = 0.07, cam axis at Y = 0.13 |
| `Head Gasket` | Centre of its bottom face | 1.5 mm thick |
| `Crankshaft` | On the crank axis, centred | Runs along X. Throws 1 and 4 point +Y, 2 and 3 point -Y. Stroke 77 mm (38.5 mm throw) |
| `Connecting Rod` | Centre of the small end (piston pin) | Big end hangs 148 mm below in -Y; both eyes' axes along X |
| `Piston` | Centre of the crown (top face) | Skirt hangs below; pin axis along X, 30 mm below the crown. 81 mm diameter |
| `Piston Ring` | Centre, mid-thickness | Lies flat in the XZ plane |
| `Main Bearing` / `Rod Bearing` | On the journal axis | Shell pair wrapped around X |
| `Camshaft` | On the cam axis, centred | Runs along X. Each lobe's nose must point the way it does in the built-in model to stay in time with the valve animation |
| `Intake Valve` / `Exhaust Valve` | Centre of the valve face (bottom of the head) | Stem points +Y, 105 mm long |
| `Valve Spring` | Centre of the bottom coil | 44 mm free height along +Y. It's squashed along Y as the valve opens |
| `Engine Stand` | On the floor | Engine mounts with the block deck centre at (0, 1.05, 0) |
| `Workbench`, `Parts Shelf`, `Pallet Rack`, `Dyno`, `Computer Desk`, `Tool Board`, `Tool Board Tools` | On the floor (tool board: its centre) | Keep roughly the built-in footprint so collisions and set-down surfaces still match |
| `Machine: Ring Filer`, `Machine: Drill Hone`, `Machine: Power Hone`, `Machine: Boring Bar`, `Machine: Crank Grinder`, `Machine: Journal Polisher`, `Machine: Surface Grinder`, `Machine: Guide Reamer`, `Machine: Seat Cutter`, `Machine: Valve Refacer` | On the floor | Facing -X (towards the middle of the shop); parts are set up about 0.9 m high, 0.1-0.2 m in front of the origin |
| `Package: Box`, `Package: Crate`, `Ceiling Lamp`, `Shop Floor`, `Shop Walls` | On the floor (lamp: its centre) | |

Machined-surface overlays (`Overlay: Bore`, `Overlay: Main Journal`, ...) can be replaced too, but they
need to match your part's surfaces exactly, so it's usually easier to leave them alone.

## Where to get models

- **Sketchfab** (sketchfab.com): search "engine block", "crankshaft", "piston", "valve spring". Filter by
  *Downloadable* and check the licence: CC0 or CC-BY are fine for a game (CC-BY needs credit).
  Download as FBX or glTF (convert glTF to FBX/OBJ in Blender).
- **GrabCAD** (grabcad.com/library): engineering CAD, very accurate engine parts. Usually STEP/STL.
  Import STEP into FreeCAD (or Fusion 360), export as OBJ. Check each model's licence before shipping.
- **Thingiverse / Printables**: 3D-print models (STL) of pistons, rods, cranks; import to Blender and
  export OBJ. Many are CC licensed.
- **Poly Haven** (polyhaven.com): CC0 shop props and PBR textures (concrete, metal, wood) that can
  replace the generated ones.
- **Make your own**: the built-in shapes are dimensioned from the LV-16 spec, so modelling over them
  in Blender keeps everything lined up.

## Exporting from Blender

1. Model in metres (Scene > Units > Unit Scale 1.0), or set `scale=` in `models.cfg` to convert.
2. Put the origin where the table above says: Object > Set Origin, or move the mesh so the point sits at
   the world origin and apply (Ctrl+A > All Transforms).
3. Point the part the right way: for engine parts the crank axis is X; Blender is Z-up, so export with
   **Forward: -Z, Up: Y** (the OBJ and FBX exporters both have these options), or fix it with `rotate=`.
4. Triangulate isn't needed; the loaders handle quads and n-gons.
5. Export UVs and normals. Bake the material's base colour into a single image if you want it textured
   and pass it with `texture=`. Each model uses one texture.
6. Keep it reasonable: a few thousand triangles per part is plenty, as a full engine has 30+ parts.
