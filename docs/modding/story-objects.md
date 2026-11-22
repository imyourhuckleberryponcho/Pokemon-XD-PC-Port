# People, zones, chests, music and doors in rooms
> Early version: the format can still change.

**What this page covers:** how to put people, zones, chests, music and doors into a room: a person who talks or starts a battle, a spot that
starts a script, a chest the game remembers, a room's song, and a changed door. The room can be one of the game's own rooms or a new room
of your mod (see [story-scripts.md](story-scripts.md)).

Rooms and their objects belong to XD. In a mod that also names Colosseum, keep `objects/` in `games/xd/` (see [two-games.md](two-games.md)).
They are not available on Pokémon XG.

## What you can add

A room (one of the game's rooms, or a new room of your mod, see [story-scripts.md](story-scripts.md)) can get:

- **People**: a person standing or walking in the room, who says something when you talk to them.
- **Zones**: a spot that starts a script when the hero walks into it. Either a spot the room's map already has ("event zone"), or a
  round area you place yourself ("area zone").
- **Chests**: something the hero can pick up. The game remembers it was taken, also after saving.
- **Music**: what plays in the room.
- **Doors**: an opening and closing door, with a story flag that says if it is open.

You write one small file per room, `objects/<room>.json`, plus the room's `.xds` script if you use people or zones.

## Where the file goes and what it is called

```
my-mod/
  mod.json
  objects/side_room.json      <- the room's name: a room your mod adds (newroom side_room), or a room of the disc
  objects/D1_labo_1F.json        by the name of its archive (see below)
```

The name of a disc room is the name of its archive: `xd_data_mods <your disc> --names floor` lists them. A mod with an `objects` folder
counts as a "new playthrough" mod (it is chosen when you start a New Game), because it adds table rows the save has to agree with.

## The file

Every list is optional. Names are letters, digits and `_` (up to 40), and are used once per kind.

```json
{
  "people": [
    { "name": "guide", "model": 10, "x": 0, "y": 0, "z": 40, "angle": 180,
      "talk": "guide_talk", "behavior": "stand", "talk_start": 2, "talk_end": 1 }
  ],
  "zones": [
    { "name": "hall", "area": { "x": 0, "y": 0, "z": 0, "radius": 60 } },
    { "name": "gate", "event": 3, "on": "touch", "run": "gate_event", "args": [1, 0] }
  ],
  "chests": [
    { "name": "gift", "x": 30, "y": 0, "z": 30, "angle": 0, "item": "SUPER POTION", "count": 2, "flag": "gift_taken" }
  ],
  "doors": [ { "name": "side", "like": 3, "type": 1, "open_flag": "side_open" } ],
  "music": { "like": "D1_labo_1F" }
}
```

### People

| Field | Meaning |
|---|---|
| `name` | the name you use in the room's script: `person @guide` |
| `model` | a row of the game's `peopleInfoData` table: the person's model and how close you must be to talk. `xd_data_mods <disc> --show peopleInfoData 10` |
| `x`, `y`, `z`, `angle` | where the person stands and which way they face (degrees) |
| `talk` | a function in the room's script that runs when the hero talks to the person |
| `move` | a function that runs when the room loads (the person's own behaviour); optional |
| `behavior` | `stand` (default), `wander` (walks about) or `look` (turns about) |
| `solid`, `visible` | default `true` |
| `look_at_hero` | the person's head follows the hero (default `false`) |
| `talk_through_walls` | you can talk across a counter (default `false`) |
| `talk_start` | what the person does when you talk to them: `2` (default) the whole body turns to face you, `1` only the head turns, `0` nothing, `3` cannot be talked to at all |
| `talk_end` | `1` (default): after the talk a body-turning person turns back to the old direction; `0`: stays facing you (only matters with `talk_start` 2) |
| `blend` | default `true`: smooth changes between the person's motions (the game's "flag2") |
| `name_text` | the number of a message that is the person's name (0: none) |

To talk to a person the hero must stand within about 10 units (the model row's talk distance plus both collision balls) AND face her: the game only looks 40 degrees to each side of where the hero looks. Pressing A beside her with your back turned does nothing (the same as any person in the game).

`"replace_people": true` (at the top of the file) leaves out the room's own people. Otherwise yours are added to them. Only one mod
can add people to a room.

In the room's `.xds` script, declare each person once and use it like the game's own people:

```
person @guide                 ; the room's people table lists this person (talk scripts only run for people in it)
...
func guide_talk
    reserve 1
    ...                       ; the person is the function's first argument: loadref arg[-1]
    push.people @guide        ; (or use it by name anywhere a people entry is wanted)
```

The tool and the game take care of the numbers: your people are numbered from 128 on in the room, so the room's own people keep theirs.

### Zones

An **event zone** uses an event number that the room's map already has (the map's walkable triggers). `on` is `walk` (the hero walks
across it), `touch` (the hero touches it) or `check` (the hero presses A facing it). `run` is a function of the room's script, `args`
are up to four numbers it gets. You can only use event numbers that the map has: a new map is not possible yet.

An **area zone** needs no map support: you give a centre and a radius. The room's script asks the game whether the hero stands inside:

```
func hall_watch
    reserve 0
    zone @hall                ; asks: is the hero inside the zone "hall" of this room?
    load reg[0]               ; 1 = inside, 0 = outside
    ...
```

Ask often enough (for example once a frame, from the room's `hero_main` loop). In Lua there is `in_zone("hall")` too.

### Chests

`item` is an item name as the game writes it (`xd_data_mods <disc> --names item`) or a number; `count` how many. `flag` is the name of a
story flag of your mod (the tool creates it for you): the game sets it when the chest is taken and does not offer the chest again. Options:

- `look`: `chest` (opens, stays open), `pickup` or `pickup2` (a thing on the floor that vanishes).
- `kind`: `item` (the "found" message shows), `item_quiet` (straight into the bag), or `money` (`count` is the amount).

### Music

`"music": { "like": "D1_labo_1F" }` plays what that room of the disc plays. For your own numbers, `fade` is the time in milliseconds the new
song takes to fade IN (the old song always fades out over a fixed half second first), `env_fade` is the time in milliseconds the old ambient sound
(wind, machines) takes to fade out before the new one starts (only for ambient sounds); `0` = start at once, no fade. Or `{ "bgm": 1067, "volume": 90, "fade": 500 }` with a sound
number (`xd_data_mods <disc> --show floorsoundTbl 1` shows what the disc's rooms use). `"music": "none"` or `null` plays nothing. New songs
or sounds are not possible yet. Without a `music` entry a new room stays as it was (silent).

### Doors

A door is an animation of a part of the room's model. You cannot make a new model, so a door row starts from a row of the disc's
`floor_door_list` (`like`, `xd_data_mods <disc> --show floor_door_list 3`) and changes what you name: `type`, `part`, `collision`,
`open_anim`, `close_anim`, `resource`, `open_flag`, `close_flag` (story flags of your mod). The door is open when its `open_flag` is
set. Walking through a door into another room is not a door: it is a zone whose script moves the hero (`Floor.link_16`, see the side
quest example).

## Making the mod

```
python -I tools/story/xdscript.py mod lab.xds side_room.xds --out <mods folder>/my-mod --name my-mod --disc <disc> --objects <folder with the .json files>
python -I tools/story/xdscript.py check <mods folder>/my-mod
```

`mod --objects` checks each file against the room's script (every person declared, every function there), adds the story flags your
chests, doors and music use, and copies the files to `objects/`. The game reads them again every time it starts.

The complete example is the side quest, `tools/story/examples/side_quest.py` with `tools/story/examples/side_quest/objects/side_room.json`.

Scripts: a person's `talk` and `move` numbers and an event zone's `run` are `0x01000000 | the function's number`: the tool and the game write
them for you. A bare function number never runs.

## Common mistakes

- A person without `person @name` in the script: the person stands there but never talks.
- A `talk` or `run` function that is not in the script: the game says so and leaves the person or zone out.
- A chest without a `flag`: refused (the game could not remember it).
- Positions: a room's size is not written down anywhere. Put the hero where you want to see things, read their place with
  Luau's `position()` (it logs `x y z`) and write those numbers.
- Two mods adding people to the same room: the second is left out with a message.
- An event number the map does not have: nothing happens.

## Limits

At most 48 people, 48 chests, 64 zones and 16 doors per room in the file, but the GAME has one pool of 48 people for everything at once: the hero, up
to three party members who are out, every person of the room that loads on entry (the room's own and yours), EVERY chest (each takes one) and
anyone a script opens. A full pool does not crash: the person or chest that does not fit is silently missing. Keep the room's own people + your
people + your chests at 44 or fewer (the game and the check warn at 44 and over, and say what they counted).

Models: a person's model and the three chest models must be in the game's shared people archive (`people_archive.fsys`) or in the room's own archive.
A model that is only in some other room's archive does not load outside that room: the person is missing and the log says "person setup
failed" (no crash). The game and the check warn when a model is not in either place. No new maps, models, songs or collision. Event zones need event numbers the
map already has. Not in Pokémon XG.

## See also

- [story-scripts.md](story-scripts.md): the scripts that people and zones run
- [music-and-sound.md](music-and-sound.md): songs for rooms
- [two-games.md](two-games.md): which parts are XD only
