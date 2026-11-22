# One mod for more than one game
> Early version: the format can still change.

**What this page covers:** how to lay out a mod folder so that it can work in Pokémon XD and in Pokémon Colosseum, what the port does with the
folder today, and which parts of a mod are XD only. Today the port plays only XD; Colosseum comes after XD 1.0. A mod that works in one game only needs nothing
from this page except the XD-only notes.

## The idea

A mod folder has **shared parts** at the top and an optional **games** folder with one folder per game:

```
my-mod/
  mod.json                 "game": ["xd", "colosseum"]
  textures/                shared: pictures that fit every game
  scripts/                 shared: Luau that only uses what every game has
  data/                    shared: tables that mean the same in every game (rare)
  games/
    xd/                    only for Pokemon XD (every XD disc; NOT the XG romhack: see "XD and XG" below)
      data/ scripts/ objects/ textures/ files/ fsys/ ...
    xg/                    only for the Pokemon XG romhack (optional; the port plays it too)
      data/ ...
    colosseum/             only for Pokemon Colosseum (not played by the port yet)
      data/ ...
```

Each folder under `games/` has the same folders a mod has at the top (`data`, `scripts`, `textures`, `files`, `fsys`,
`objects`, `rooms`). The running game reads the shared part plus **its own** `games/` folder. The other games' folders are
ignored.

## Folder names: `xd`, `xg`, `colosseum`, or a disc ID

The plain names are the normal way (0.7.0):

| Folder | Used for |
|---|---|
| `games/xd/` | every XD disc (USA, Japan, Europe). **Not** the XG romhack |
| `games/xg/` | the Pokémon XG romhack (it has the same disc ID as XD, so only the name tells them apart) |
| `games/colosseum/` | every Colosseum disc |

In `mod.json`, `"game": ["xd"]`, `"game": ["xg"]`, `"game": ["colosseum"]` (or several: `["xd", "xg"]`) name the games the same way. Small letters.

The older, finer names still work, for a mod that must tell the regions apart. A folder name is then the **first 3, 4 or 6
letters and digits** of a disc's game ID, in capitals:

| Folder | Used for |
|---|---|
| `games/GXX/` | the same as `games/xd/` |
| `games/GXXE/` | XD discs of one region (every `GXXE..` disc) |
| `games/GXXE01/` | exactly that disc |
| `games/GC6/` | the same as `games/colosseum/` |

If both `games/xd/` and a finer folder apply, the finer one comes later and wins (rule 1 below). Any other name (two letters, five
letters, a hyphen) is not read, and the checker says so.

## XD and XG

Pokémon XG is a romhack of XD. It has the same disc ID (`GXXE01`), but it renumbers moves and changes data, so a mod written for XD can
break on XG. The port therefore treats XG as a game of its own for mods:

- A mod that names only `"xd"` (or a bare disc ID such as `GXXE01`, or says nothing about XG) is **never loaded on XG**. The launcher and the game
  show it as *Made for XD, not XG* and do not use it. It is never loaded silently.
- A mod that names `"xg"` is loaded on XG. It reads the shared parts and `games/xg/`.
- A mod that names **both** (`["xd", "xg"]`) and has **no** `games/xg/` folder reads `games/xd/` on XG as well. That is the author saying
  "this is safe on XG". With both folders, each game reads its own.
- Every mod list (the launcher, the game's MODS page, the checker, the update and download windows) shows a tag: **XD**, **XG** or
  **XD + XG**.

The checker adds notes for this (`xd-only`, `xg-folder`) and a warning (`xg-xd-data`) when a mod names `xg` but its data files are the
XD ones. Saves and online rooms already keep XD and XG apart (a save and a room belong to one edition), so an XD mod list never matches an XG room.

## Which file wins (the precedence rules)

For one folder, say `data`, the game builds the **merged view** like this:

1. Take the shared folder (`data/`), then `games/<3 letters>/data`, then `games/<4 letters>/data`, then
   `games/<6 letters>/data`. The more exact folder comes **later and wins**.
2. A file with the **same path** (capital letters do not count) in a later folder **replaces** the earlier one **whole**.
   The port never mixes two files into one. Files with different names simply add up.
3. Order matters where files are used one after the other (data files are applied in order, scripts run in order). Shared
   files come first, then the game's own, and inside each group by name. So the game's own files act **after** the shared ones.
4. Only the folders that apply to the running game are read.

Example for XD (USA): `scripts/a.luau`, `scripts/c.luau` (shared), `games/xd/scripts/b.luau`, `games/GXXE01/scripts/a.luau`.
The game runs `c.luau` (shared), `b.luau` (the `xd` folder), `a.luau` (the `GXXE01` one: it replaced the shared `a.luau`).

**An old mod** (the flat layout: everything at the top, no `games` folder) keeps working exactly as before, for the games its `mod.json` names (for an XD mod, XD only: it is not loaded on XG unless `mod.json` also names `"xg"`). The checker adds a gentle note: *this mod uses the older flat layout; move a game's own files into `games/xd/` when you want it to grow to another game.* Nothing is required.

## `mod.json`: which games the mod names

`"game"` is still a list of disc IDs. A mod is used only when the disc's ID is named. New: an entry may be the **start** of an ID,
so `"game": ["xd", "colosseum"]` means every XD and every Colosseum disc. A whole ID (`"GXXE01"`) still means only that disc. (Two,
five or other lengths are not IDs; the checker warns.)

What a mod does for a game it names:

- **Works** with its own parts: a `games/` folder for it matches (or the mod has no `games` folder at all: an old-layout mod).
- **Shared parts only**: the mod names the game, has `games` folders, but none for this game. Only the shared parts apply.
- **Nothing there**: no folder for the game and no shared parts. The mod changes nothing in that game.

## What the tools say

- **Checker** (`xd_mod_check`): a line `works with: XD; Colosseum: shared parts only`, then which `games/` folders each game uses.
  Warnings for a `games/` folder no `game` entry matches (nobody reads it), a folder with a wrong name, a file straight under `games/`,
  a game with nothing for it, and shared Luau that uses a function only one game has (below).
- **Permissions** (what the launcher shows before you install or update): a permission that only a `games/` folder uses carries
  `[only for GXXE01]`, so you can see that a Colosseum player does not get it.
- **Packer, signing, updates, requirements:** all work on the whole folder, `games/` included. A mod update that changes a
  `games/` folder counts as a change of the world, like a change in `data/`.
- **The launcher's Mods page and the game's own MODS page** show `Works with: XD; Colosseum: shared parts only` for each mod,
  the same on Windows, Linux and Mac. A mod that does not name the game on your disc says so and is not used.

## Which parts are for both games, and which are XD only

This is the audit of what exists today. "Both" means the idea is the same in every game and the names are game-neutral; the
**numbers and names come from the disc that runs**, so a species number from XD means nothing in Colosseum. Use the names.

| Part | Both games | XD only |
|---|---|---|
| `mod.json`, `mods.ini`, profiles, requirements, updates, permissions, signing | yes | |
| Textures (`textures/`, HD texture packs) | yes (the pictures a game asks for differ, the format does not) | |
| Data tables `data/*.json`: the file format (`pokemon`, `moves`, "add" rows) | yes (the same JSON) | table and field **names** are the game's own: a table XD has may be missing in Colosseum |
| Luau: `on(hook, f)`, `log`, `find`, `name_of`, the name tables `TYPE`, `SPECIES`, `MOVE`, `ITEM`, `ABILITY`, `NATURE` | yes (filled from the disc that runs) | |
| Luau hooks `battle.damage`, `battle.damage_predict`, `battle.move_used`, `battle.turn_start`, `battle.turn_end`, `battle.end` | same names, offered by **XD today**; Colosseum's battle code is another engine, so they are not promised there | |
| Luau `party()`, `flag(n)`, `set_flag(name, v)` | same idea | the flag **numbers** are XD's |
| Luau `FLAG`, `STORY` name tables (including the auto-made flag names) | | XD's story flags and steps |
| Luau hooks `battle.ability`, `battle.ai_switch`, `battle.ai_item` | same names, offered by **XD today** (they ask the battle engine's own questions, so another engine must offer them itself) | |
| Luau hooks `on_flag_changed`, `on_money_changed`, `on_pokemon_caught`, `on_menu_open` | same names, offered by **XD today**; the flag **numbers** are XD's | |
| Luau `wild_battle`, `give_shadow_pokemon`, `shadow_species` and the words `wild_battle`, `give_shadow_pokemon` in a story script | | XD only (Poké Spots and the Shadow list are XD's) |
| Luau `story_function`, `position()`, `room(name)`, `in_zone(name)` | | XD only (they belong to XD's story scripts and rooms) |
| Luau `set_trainer_team`, `lend_team` | same names and member fields wherever the game builds a trainer's team from rows | **XD only today** (they write XD's deck rows for one battle; the checker lists them as XD-only) |
| Luau `value_*`, `currency_*`, `define_currency`, `mod_page`, `mod_pick`, `mod_shop`, `add_menu_entry`, `save_game`, `fill`, `battle_rules` | same names in every game | **XD today** (the checker does not list them as XD-only: nothing in their shape is XD's; a later game gives the same functions) |
| Luau `message_number` / `message_text` / `message_species` / `message_item` / `message_move` | same names; the message codes they fill (`{2F}` ...) are the game's own | **XD today**: the codes are XD's, a later game documents its own |
| Luau `battle_tables` | rows keyed by `move` / `species` | **XD only today** (it writes XD's table layout for one battle; the checker lists it as XD-only) |
| Luau `random_int(n)` / `random_int(lo, hi)` | yes (it reads the running game's random state and never advances it) | the numbers it gives are the same on every PC for the same game moment |
| The mod folder `common_script/` (a changed shared script) | | XD only (Colosseum's scripts are another format) |
| Story scripts (`.xds`, Tiga / TCOD) | | XD only. Colosseum uses a different script engine ("cms") |
| New rooms, `rooms/` | | XD only |
| `objects/<room>.json`: people, zones, chests, music, doors | | XD only **for now**. The decomp says these tables are the same family in Colosseum, but the row layouts have not been compared |
| Adding species, moves, items (`add`), own Pokédex text, faces, `"model"`, `"cry"`, `"dex"`, a move's `"animation"` | the idea is the same | XD's table layouts, model table and sound tables; Colosseum's have not been compared. Put the `"model"` / `"cry"` / `"dex"` keys in a `games/xd/` data file. The WAV encoder and the archive writer in the port know the GameCube formats, not which game uses them |
| `sounds/effects.json` (single sound effects) and `sounds/songs.json` (new songs) | the sound library (MusyX), the WAV / OGG reader and the encoders are shared code that knows no game | the **bank names and file names** are XD's (`menu`, `battle`, `atmos`; `common.fsys`; `sound/snd_se.samp`). Colosseum uses the same library, but its banks have not been looked at: put these files in `games/xd/sounds/` until they have |
| A move's `"animation_attack"` / `"animation_damage"` / `"animation_status"` | the idea is the same | XD's animation table (`waza_wzx_data`, three pieces per animation). Colosseum's has not been compared |
| `tools/model_convert/gltf_to_pkx.py` (your model on a character's skeleton) | the tool reads and writes the HSD model format that both games use | the **characters** you replace are XD's (a trainer's `pkx_<name>.fsys`, a person's model). Colosseum's trainer models are not looked at |
| Battle starts from scripts | | XD only (Colosseum starts battles differently) |

The checker helps with the Luau part: if a mod names more than one game and a **shared** script uses `story_function`, `position`,
`room`, `in_zone`, `FLAG`, `STORY`, `set_trainer_team`, `lend_team` or `battle_tables`, it warns. Move that script into `games/xd/scripts`.

## A small example

`mod.json`:

```json
{
  "id": "nicer-damage",
  "name": "Nicer damage",
  "version": "1.0",
  "game": ["xd", "colosseum"],
  "scripts": true
}
```

`scripts/damage.luau` (shared, uses only game-neutral names):

```lua
on("battle.damage", function(e)
  if e.move.type == TYPE.WATER then return e.damage * 2 end
end)
```

`games/xd/data/stats.json`: numbers for XD's tables only. In Colosseum the mod then does the shared part only (and, since
Colosseum does not offer `battle.damage` yet, nothing at all). The mod page says: `Works with: XD; Colosseum: shared parts only`.

## Adding Pokémon, moves and items on XG
Until now a mod's `add` block was dropped on XG, with the reason "its tables have their own layout". That reason was checked against the real XG
disc (read only) and **is not true for these three tables**: XG's Pokémon table has the same 415 rows of 292 bytes as XD's, its move table the
same 375 rows of 56 bytes, its item table the same 594 (the numbers were read from the running game: `pokemon_data grown: 415 -> 416 rows`,
`waza_data grown: 375 -> 377 rows`). What differs in XG are the *values* (renumbered moves, its own abilities, two starters) and the program
code, and that code is the game's own, already written to read these tables. The block was only a precaution.

So on XG a data mod (that names `"xg"` in `mod.json`) can now add **new Pokémon, moves and items**. Tried on the real XG disc, with an own
settings, card and cache folder (the disc is never written):

| What | Result on XG |
|---|---|
| The example mod (a new Pokémon and two new moves) loads and gets numbers 415, 375 and 376 | works |
| A new Pokémon given to the party, the game saves, CONTINUE with the mod | the Pokémon is in the party, the save loads, the card is untouched by CONTINUE |
| CONTINUE without the mod | refused with the message that names the mod; the card is untouched |
| A save without its setup file, without the mod | stopped by the last guard (`this save needs its mods`), card untouched |
| The opening battle with the new Pokémon in front and the new move as its first move | the Pokémon has its own (higher) HP and the new move takes the foe's HP down |
| A new item (a copy of Oran Berry) given three times, saved, CONTINUE with the mod; without the mod; with no setup file | gets number 594, loads with the mod, refused without it, and the last guard stops a save with no setup file (the item is in the bag); the card is untouched each time |

What stays **off on XG** because it was not tried there: new story flags, rooms, battles and trainer decks, and a new Pokémon's own face, model,
cry and Pokédex marks, and a move's animation. A mod with those still loads; those parts are skipped and the log says so.

A mod written for XD needs `"game": ["xd", "xg"]` (or `"xg"`) to load on XG at all, and its copies (`"copy": "Eevee"`) must name rows XG has;
XG's own moves are renumbered, so a mod that copies by *number* means something else on XG. Copy by name.

## For tool makers

The code is `engine/mods/include/mod_layout.h` (`ContentRoots`, `LayoutFiles`, `ResolveLayoutFile`, `SupportOf`). Anything that
reads a mod folder should go through it, never `mod/<folder>` by hand.

## See also

- [sharing.md](sharing.md): packing a mod
- [battle-scripts.md](battle-scripts.md): scripts and the XD-only functions
- [labels.md](labels.md): stable labels
