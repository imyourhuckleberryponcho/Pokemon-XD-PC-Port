# Making your own rooms
> Early version: the format can still change.

**What this page covers:** how a mod makes a **new room** that looks and plays differently from the game's rooms: walls you add or take
out, furniture you hide or move, pieces borrowed from other rooms of the game, new entrances and your own camera. The room is described in one
small text file (a **recipe**). The mod holds no part of the game: the game builds the room from the player's own disc when it starts.

Rooms belong to Pokémon XD, not XG. A mod with rooms is chosen when a New Game starts (it adds rows the save has to agree with).

## The idea

A new room starts as a **copy of a room of the game** (its "base": the walls, floor, light, camera, text and script). You then change the copy.
The recipe lists the changes:

- **hide** a piece of the room's model (a bed, a shelf) and **move** one (with its collision, if you say so);
- **add a piece** from another room of the game (a counter from a Pokémon Center), with a box round it so the hero can not walk through it;
- **add walls** and boxes (invisible: collision only) or **remove** a wall;
- give the room its own **entrances** (where the hero arrives) and **camera**.

People, chests, music, zones and doors are added as for any room, in `objects/<room>.json` ([story-objects.md](story-objects.md)).
The room's script is written as for any new room ([story-scripts.md](story-scripts.md) "New rooms").

## Steps

1. Pick the base room: `xd_room_parts <your disc> list` shows every room with its size, models and joints. `xd_room_parts <disc> parts M3_houseA_1F --json parts.json`
   lists the pieces of one room: each **joint** has a number, a place and a size. (The numbers are only a name for a piece; the same disc always gives the same numbers.)
2. Make the mod folder with `data/story_rooms.json` (`{ "add": { "floors": { "reading_room": { "copy": "M3_houseA_1F" } } } }`) and
   `rooms/reading_room/room.json`, the recipe. The spec of every field is in [room-format.md](room-format.md).
3. Check it: `xd_room_build <your disc> rooms/reading_room/room.json --out <a folder under build>` tells you every problem found, or what it made, and
   writes `topview.png`: the room's collision from above (red = walls, green = ground, blue = door planes, yellow = event triangles).
4. Test it in the game: the dev menu (F9) warps to any room, new rooms come after the game's own.

If the recipe is wrong, the game says why in its log and the room stays a plain copy of its base. A map or collision file that is broken in
any other way is refused the same way: it never reaches the game's loading code.

## Example recipe

```json
{
  "format": "xd-room", "version": 1,
  "name": "reading_room",
  "base": "M3_houseA_1F",
  "hide": [ { "model": 0, "joint": 28 } ],
  "pieces": [ { "from": "M1_pc_1F", "model": 0, "joint": 12, "at": [-20, 0, 5], "collide": "box" } ],
  "walls": { "boxes": [ { "cx": 0, "cz": 8, "sx": 8, "sz": 8, "height": 20 } ] },
  "entrances": [ { "x": 0, "y": 0, "z": 29, "angle": 0 } ]
}
```

## Things to know

- **The hero is a ball of radius 3.5.** He stops that far from a wall. Walls you add are tall (`height` 22 by default): rooms are seen from the south,
  so a tall wall on the south side hides the hero (the game's own walls there are low).
- **Collision and picture are separate.** Hiding a piece does not remove its collision, and moving one moves it only if you say `collision`.
- **A piece takes the room's light.** Its colours come from the room it comes from, lit by the new room's lights.
- **Size.** A room may not be bigger than the largest room of the game; a piece brings its pictures with it (a Pokémon Center's hall is nearly 1 MB).
- **At most 64 collision records.** The game keeps its state for 64 per room.
- The room's exits are the base room's exits (they take the hero to the neighbours of the base room). To lead somewhere else, use a zone whose script moves the hero.
- A recipe's hashes (`base_hash`, a piece's `hash`) make a different disc version refuse the room instead of building something odd.

## Limits (so far)

- Your own models (`"gltf"` pieces, a `.glb` from Blender) are drawn with vertex colours and one picture per material, **not lit** by the room's lights, **opaque** (no see-through),
  PNG pictures only (docs/modding/room-format.md "Your own models"). Collision from the model's mesh (`"collide": "mesh"`) works for your own models only, not for a piece of the game.
- Animated pieces (doors, flickering lights) are not copied as animated.

## Changing a room of the game
A folder `rooms/<archive name>/room.json` named like a room of the disc (for example `rooms/M3_out/room.json` for Phenac) is a recipe for THAT room:
`pieces`, `hide`, `move` and `walls` work as for a new room; the game builds the room from the player's disc at start-up and uses it in place of
the disc's file. `camera` is not used (the room keeps its own). `entrances` are ADDED after the room's own ones (the room's doors keep working); they
are numbered from 32 up (the room's own list is padded to 32 first, so the numbers are known when the mod is built). An entrance with
`"exit_of": "<a room the mod adds>"` is where that room's exit comes out: the exit rows that lead to this room are changed to it. Example: the Reading
Room's door is a door piece borrowed from another house (`M3_houseD_1F`, model 2, joint 1) placed on the front wall of a Phenac house at
`[-217.5, 40, 17.8]` (the hero's ground level there is 40), with a zone of `objects/M3_out.json` in front of it that sends the hero into the new room,
and the entrance `{ "x": -217.5, "y": 40, "z": 29, "angle": 0, "exit_of": "reading_room" }` where the room's exit brings the hero back out, in front of
that door (without it the exit is that of the room it copies: the house door next to it). Two mods may not change the same room; the second is refused.

## Saves and changed rooms
The playthrough's setup file (next to the save) keeps a number for each room a mod gave a recipe or files (`room_recipes`). When you
CONTINUE and a room's files are not the ones the save was made with, the game says so in plain words and asks: keep playing (the setup
file then holds today's recipes) or quit. Your hero may stand inside what is now a wall, so make a copy of the save before you change a
room that a save is inside of.
