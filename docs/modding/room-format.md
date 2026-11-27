# Room recipe format (version 1)
> Early version: the format can still change. For people who write a program that makes rooms (an editor); modders: see [rooms.md](rooms.md).

A **room recipe** is one JSON file, `rooms/<name>/room.json` in a mod. It says how a new room is made from rooms the player's own disc
already has. A recipe holds **no game data**: only names of rooms of the disc, joint numbers and the author's own numbers. The game builds
the room in memory when it starts, from the player's disc. A program can write and read this file; nothing else is needed to make a room.

Rooms belong to Pokémon XD. Not available on Pokémon XG.

## Coordinates, units, directions (for a program that draws or writes rooms)

- Right-handed x, y, z. **x** east (right on the top view), **y** up, **z** south (towards the player's camera in the usual room view).
- Distances are the game's own units. A small house is about 80 wide; the hero is a ball of radius **3.5** (he stops 3.5 from a wall).
- Angles in the recipe are **degrees**. The game stores an entrance's angle in whole degrees (`0.0174533 * angle` radians).
- **Triangles: front faces are clockwise** (looking at the face, the vertices go clockwise) in these right-handed coordinates. Counter-clockwise
  faces are invisible.
- **A wall blocks from one side only.** A wall line from (x0, z0) to (x1, z1) has the normal (dz, 0, -dx); it stops a hero who stands on the side the
  normal points to (walking from the first point to the second in the top view, x to the right and z down, that is the LEFT side). A hero on the other
  side walks through it. `"flip": true` swaps the side. (Proven in the game: a wall at z = 20 stops a hero coming from the south only with `"flip": true`.)
  The walls of a `boxes` entry face outward: the outside is blocked, the hero stops 3.5 from the box.
- Joint numbers: every model of a room's map is a tree of joints; the number of a joint is its place in a walk that visits a joint, then
  everything below it, then its next sibling (depth first, children before the next sibling). The numbers are the same on every run for the
  same disc. `xd_room_parts <disc> parts <room> --json f.json` lists them with their places and sizes.

## The file

```json
{
  "format": "xd-room", "version": 1,
  "name": "reading_room",
  "base": "M3_houseA_1F",
  "base_hash": "bd26d5010d8a8532d9c8874a849d82b6f5bfe06c",
  "hide":   [ { "model": 0, "joint": 28, "children": true } ],
  "move":   [ { "model": 0, "joint": 4, "by": [10, 0, -5], "turn": 90, "collision": { "record": 2 } } ],
  "pieces": [ { "from": "M1_pc_1F", "model": 0, "joint": 12, "at": [-20, 0, 5], "turn": 180, "collide": "box", "hash": "..." } ],
  "walls":  { "add": [ { "line": [0, 20, 30, 20], "y": -2, "height": 22, "flip": false } ],
              "boxes": [ { "cx": 0, "cz": 8, "sx": 8, "sz": 8, "y": -2, "height": 20 } ],
              "remove": [ { "area": [2, -9, 39, -9] } ] },
  "entrances": [ { "x": 0, "y": 0, "z": 29, "angle": 0 } ],
  "camera":    [ { "kind": 0, "x": 0, "y": 0, "z": 0, "distance": 60, "height": 40, "rotY": 0, "radius": 80, "ratio": 1, "angle": 0, "pow": 1, "blend": 30, "fov": 30 } ]
}
```

Unknown fields are reported, never silently ignored. Every list is optional.

| Field | Meaning |
|---|---|
| `format`, `version` | `"xd-room"` and `1`. Another version is refused. |
| `name` | the room's name (letters, digits, `_`, `-`; the folder `rooms/<name>/` and the mod's `data/story_rooms.json` use it) |
| `base` | the archive name of the room of the disc that is the **shell**: its model, lights, camera, text and script. Must be the room the mod's `story_rooms.json` says to copy. |
| `base_hash` | optional: SHA-1 of the base room's map as the author saw it. A different disc version is refused with a message. |
| `hide` | stops a joint (and with `children`, true by default, everything below) from drawing. Its collision stays (see `walls.remove`). |
| `move` | moves a joint by `by` (x, y, z) and turns it about y by `turn` degrees (exact only for a joint that has no x / z turn). `collision` moves collision with it: `{"record": n}` moves the whole collision record n (turning about its middle), `{"area": [x0, z0, x1, z1]}` moves the wall and ground triangles that lie wholly inside the rectangle. Door planes and event triangles never move. |
| `pieces` | copies a joint tree of **another room** of the disc (with its meshes and textures) into the room as a new model. `at` is where the piece's **bottom centre** stands (the middle of its meshes' box, its lowest point at height `at[1]`); `turn` turns it about y first. The first piece from a room brings that room's big shared blobs (vertex arrays and pictures: about 0.5 MB from the Pokémon Center hall); more pieces from the same room share them. `hash`: that room's map SHA-1 (optional). `collide`: `"none"`, `"box"` (a box round the piece's meshes, standing at `at`; height from the mesh or `height`) or `"mesh"` (only for your own models, below). Lights stay the room's, so the piece takes the room's light. |
| `pieces` with `gltf` | your **own model**: `{ "gltf": "parts/ramp.glb", "at": [x, y, z], "turn": 90, "scale": 1, "collide": "mesh" }`. The file is in the room's folder in the mod (`rooms/<name>/parts/ramp.glb`) and is part of the recipe (a changed model file counts as a changed recipe for the save question). Not together with `from`, `model`, `joint`. See "Your own models" below. |
| `walls.add` | a vertical wall (2 triangles) from `line` = [x0, z0, x1, z1], from `y` up by `height` |
| `walls.boxes` | a free-standing box with its outside walkable: centre `cx`, `cz`, size `sx` by `sz`, from `y` up by `height` |
| `walls.remove` | takes out the wall triangles that lie wholly inside `area` |
| `entrances` | replaces the room's entrance list. Entrance 0 is where the hero arrives by default. `angle` in degrees. In a recipe for a room of the game they are added after the room's own, from number 32 up; `"exit_of": "<new room>"` makes that new room's exit come out there (docs/modding/rooms.md). |
| `camera` | replaces the room's camera zones. The first is the default. Fields as the game's camera zone: `kind` 0 = circle, `distance` and `height` from the hero, `rotY`, `radius` (the zone's reach), `ratio` (ellipse), `fov` (degrees). |

People, chests, area zones, music and doors are **not** in the recipe: they are in `objects/<name>.json` of the same mod (see
[story-objects.md](story-objects.md)), which already does them. A program that writes rooms writes both files.

## Your own models (`"gltf"` pieces)

A piece can be a model you made yourself, in any program that writes glTF 2.0 (Blender, ...). **Nothing of the game is in it**, and nothing
of it is kept after the room is built: the game makes the room's map from the player's disc and your file when it starts.

What is read: one **.glb** (or a .gltf with its data inside as `data:` addresses), meshes of **triangle lists** (not strips or fans), the node
transforms, `POSITION`, `TEXCOORD_0`, `COLOR_0` (optional), indices of any size, and per material the **base colour factor** and **base colour
texture**. The pictures must be **PNG** (8 bits per colour: grey, RGB, palette, with or without alpha; not interlaced), inside the .glb. What is **not**
read yet (said in words when a file needs it): other picture types, a second set of texture coordinates, normals (the model is lit by its vertex
colours only: its colour is the base colour times `COLOR_0`; the room's lights do not shade it), animations, skins, morph targets, transparency
(the alpha of the colour is ignored; a picture's alpha is kept in the picture but the model is drawn opaque), extensions (a file that *requires*
one, such as compressed meshes, is refused).

How it is written into the room: one joint at `at`, one mesh per material, vertex colours, the picture as a texture (RGB565, or RGB5A3 when
it has see-through pixels; sizes are made a multiple of 4, at most 1024 x 1024; `repeat` wrapping only when the size is a power of 2).
The model has the room's own render settings: **no lighting from the room's lights**. About 2 MB of room size are used by a 512 x 512 picture.
Limits: 65,535 vertices per material.

**Units and directions** are the room's (see above): your numbers times `scale` are the game's units; x east, y up, z south. `at` is where
the model's **own origin** stands (not its bottom centre as with a piece of the game: your model decides where its origin is). `turn` turns the model about y
(degrees, as everywhere in the recipe).

**Which faces are drawn.** glTF's front faces are counter-clockwise seen from outside; the game's are clockwise. The importer turns every triangle
round, so you write the model the way every glTF tool does and the outside is what you see. With `"doubleSided": true` on the material both sides
are drawn.

### Collision of your own model (`"collide": "mesh"`)

- `"none"`: no collision (the hero walks through it). `"box"`: a box round the model's bounds (like a piece of the game). `"mesh"`: the model's
  own triangles:
  - **walls**: every triangle whose normal does not point up by more than `0.5` (its y part): vertical faces, steep slopes, overhangs and undersides. They
    are put into the room's wall list ("hit") with the triangle's **outward normal** (from its counter-clockwise order). A wall **blocks from one side only,
    the side its normal points to** (the outside of a closed model). Edges are tested for the hero's ball when they stand free or meet a neighbour at
    a convex angle (a table's corners stop the hero round, a flat wall's inner seam is not tested twice).
  - **ground**: every triangle whose normal points up by more than 0.5 (slopes up to 60 degrees): put into the ground list ("walk"). The hero's feet
    follow the height of the ground triangle under them, so a **ramp or a stair of ground triangles can be walked up** and a platform stood on. A place
    can have several ground heights (up to 8): the hero follows the one nearest his feet.
  - Triangles with no area are left out (the report says how many).
- Make closed models whose side walls reach the floor: the ground list is not a wall, so the hero walks off a ramp's side edge unless the model has a wall there.
- The collision comes with the room's own collision record (the walls record that has most triangles), so it counts in the 64 records / size limits as before.

## Piece previews (for an editor)

`tools/rooms/piece_previews.py` makes one PNG per piece of the player's disc, so an editor can show what a piece looks like before it is placed:

```
python -I tools/rooms/piece_previews.py --disc <iso> --exe <xd.exe> --room-build <xd_room_build.exe> --out <folder> M1_pc_1F:0:66 M1_pc_1F:0:19 ...
```

For each `ROOM:MODEL:JOINT` it builds a tiny mod whose one new room (a copy of a small house with its own furniture hidden) holds only that piece, starts the game
off screen and muted (own card, settings and cache folders), and takes a picture with the photo mode (free camera, no menus), pulled back by the
piece's size; the hero stands in front of the piece (a size to compare with), and the small house's own people may show at the edges. The result is `<folder>/<ROOM>_m<MODEL>_j<JOINT>.png` and `previews.json` (`{ "format": "xd-room-previews", "version": 1, "pieces": [ { "from", "model", "joint", "png", "size": [x, y, z] } ] }`,
sizes in game units; a piece that could not be placed has a `problem` instead of a `png`). One run of the game per piece (a few minutes each). The pictures
are the game's art made from the player's own disc: they stay on the player's PC (never commit or publish them).

## Limits and what is checked (the same checks run in the game and in the tools)

- The room's map is checked as the game loads one (container sizes, every pointer inside the data, joint trees without loops, display lists
  and pictures inside the data, a camera and a light present). The collision is checked as the game reads one (at most **64 records**, every pointer,
  grid and triangle number inside the file, no NaN). A recipe that fails is refused **whole**: the room stays a plain copy of its base and the log says why.
- The same checks refuse a broken `#N.bin` map or collision entry of any mod.
- A room may not be bigger than the **largest floor archive of the USA disc** (5,059,483 bytes unpacked, S1_shop_1F): the game's memory. (A
  placeholder rule, not final.)
- A mod with a room is a new-playthrough mod (it adds table rows the save has to agree with).

## The tools (the same code the game uses)

```
xd_room_parts <disc> check [room]          every map and collision of the disc checked (every original passes)
xd_room_parts <disc> list [--json f]       the catalogue: every floor, sizes, models, joints, textures, lights, collision records, map SHA-1
xd_room_parts <disc> parts <room> [--json f]   one room's models and joints (number, parent, place, meshes, size)
xd_room_build <disc> room.json [--base <room>] [--out <folder>] [--json f]
                                           the recipe checked and built; --out writes topview.png (the collision from above) and, because
                                           they hold the game's data, map.bin / collision.bin (keep under build\)
```

The library behind them is `engine/rooms` (C++: `room_recipe.h`, `room_hsd.h`, `room_ccd.h`, `room_preview.h`) and, for the game's side,
`games/xd/src/data/xd_room_lib.h`. A program that is not C++ calls `xd_room_build --json`.

## A C interface for editors

`engine/rooms/include/room_c_api.h` (`xdr_*` functions, version `XDR_API_VERSION`) is the same library as plain C, for a program in any language. It
works on byte buffers **you** give it (the player's own disc data, read by your program) and holds no game data itself:

- `xdr_recipe_check(json)`: reads a recipe, answers `{"ok", "problems"}`.
- `xdr_map_parts(map, size, &error)`: a room's models and joints as JSON (number, parent, place, meshes, size): the **piece catalogue**.
- `xdr_build(recipe, base_map, base_collision, source_map_callback, ...)`: the recipe applied; gives the new map, the new collision and a JSON report. `source_map_callback` hands over another room's map by archive name (for `pieces`).
- `xdr_collision_png(collision, size, pixels, &png_size)`: the top view of a collision file.
- `xdr_free`, `xdr_result_free`: free what the library returned.

An editor shows the catalogue (`xd_room_parts <disc> parts <room> --json`, or `xdr_map_parts`), lets the author place pieces, writes `room.json`
(and the mod's `objects/<name>.json`), and may call `xdr_build` for a check and a top view before the game ever runs it.
