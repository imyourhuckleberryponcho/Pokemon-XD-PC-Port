# Luau battle scripts

The Luau API for battle scripts, the sandbox, names, and the player's permission to run scripts. Back to the [front page](README.md).

## Luau API (battle scripts)

Needs `"scripts": true`. Files `scripts/*.luau` (the older `.lua` name is read as Luau too). Luau (typed Lua) in a sandbox; Draft: the shared mod format can still change.
Write Lua 5.1-style code (`//` floor division works): `bit32.band(a, b)` not `a & b`, no `goto`, no `<const>`, no `pcall`, no `string` patterns, no `coroutine`.
Type notes (`local x: number`, `function(e: Hit): number`), `continue`, `+=` and `` `text {x}` `` are allowed. One number type (a double: whole numbers exact up to 2^53).
Limits per handler call: 200,000 steps (a step = function call / return / loop turn), 16 MB memory; an error names mod, file and line. **All scripts of all mods together share 1,000,000 steps per game frame** (counted in steps, so the same on every PC and replay); a handler that is cut by it, or that would run after it is used up, is skipped for that frame, logged once a frame, and the mod stays on (only a handler that passes its own 200,000 is switched off). A handler cut half way did not finish: keep no half-done work in script variables. Page: [luau.md](../modding/luau.md). Register with `on("hook", function(e) ... end)`.

| Hook | `e` fields | Return |
|---|---|---|
| `battle.damage` | `attacker`, `defender` (`species`, `level`, `hp`, `max_hp`), `move` (`id`, `power`, `type`), `game_damage`, `damage` | whole number: new damage (clamped 1..9999); nothing = keep. Only hits on the defender with damage > 0, not recoil |
| `battle.damage_predict` | `attacker`, `defender`, `move.id`, `game_damage`, `damage` | whole number the trainer AI will believe (1..9999); real hits unchanged |
| `battle.move_used` | `attacker`, `defender`, `move.id`, `turn` | ignored (runs after the move) |
| `battle.turn_start` | `turn` | ignored |
| `battle.turn_end` | `turn` (already the next number) | ignored |
| `battle.end` | `result` (2 or 3 = a side won), `won` (1 = player's side), `turn` | ignored |
| `battle.ability` | `species`, `level`, `hp`, `max_hp` of the battler, `game_ability` | an ability number (`ABILITY.INTIMIDATE`) the game's checks should SEE for that battler (nothing = the game's own, 0 = none). Asked on every ability check, the AI's too: keep handlers tiny. The save's own ability is untouched |
| `battle.ai_switch` / `battle.ai_item` | `species`, `level`, `hp`, `max_hp` of the Pokémon the AI is about to give a switch / an item | `AI.VETO` (1) = no switch / no item for that Pokémon this turn (the AI goes on to its move); nothing or 0 = the game's own choice. Vetoes only |

Globals: `on`, `log` (and `print`, same thing; writes `[script <mod id>] ...` to the log), `API_VERSION` (1), the read-only name
tables `TYPE`, `SPECIES`, `MOVE`, `ITEM`, `ABILITY`, `NATURE`, `FLAG` (whole numbers), and `name_of(category, number)` /
`find(category, text)`. `species` and `move.id` are the game's numbers.

### Which game? Luau and format audit (for mods that may one day run in Colosseum too)

Game-neutral by design (same name, same meaning in any game; the contents come from the disc that runs): `on`, `log`, `print`, `find`, `name_of`,
`API_VERSION`, the name tables `TYPE SPECIES MOVE ITEM ABILITY NATURE`, `party()`, `flag(n)`, `set_flag("own-name", v)` (the NUMBERS are the game's).
The six `battle.*` hooks have game-neutral names but only **XD offers them today**; do not promise them for Colosseum.

**XD only** (mark every use; keep them in `games/xd/`): `story_function`, `position()`, `room(name)`, `in_zone(name)`, the `FLAG` and `STORY` tables,
story scripts (`.xds`, Tiga bytecode; Colosseum's engine is "cms"), `rooms/`, `objects/<room>.json` (same table family in Colosseum, row layouts not
compared yet: treat as XD only), battle starts and gifts from scripts, the table and field names in `data/*.json` (read them from `--tables` of the
game that runs). Game-neutral file formats: `mod.json`, `mods.ini`, the `data/*.json` JSON shape (`add`, `text`, `copy`), PNG/DDS textures,
packing, signing, updates. Nothing was renamed; no old name changed.

### Names in Luau (read from the player's disc at start; never invent a number when a name exists)

- Names are the game's own words in capitals, other signs `_`: `SPECIES.MR_MIME`, `ITEM.POKE_BALL`, `MOVE.TACKLE`, `TYPE.GRASS`. Some
  disc words are short: `TYPE.ELECTR`, `TYPE.PSYCHC` (check with `xd_data_mods <disc> --names type|species|move|item|nature|ability`). Duplicate
  names: first plain, later `_2`, `_3`. Names with no letters (the "?" type) have none: use the number. Rows a mod adds are named by the mod's
  own key (`SPECIES.ZORBLAT`); `TYPE.FAIRY` exists only with "Modern types"; the Gen 4 abilities (and their names) only with the Gen 4 Pokemon; their effects only with the Gen 4 rules.
- Tables are read-only (assigning, `rawset` = error). A name that does not exist is an ERROR (it switches the mod's scripts off):
  use `find("species", "Zorblat")` (number or `nil`; case, spaces and signs ignored) when the name may be missing. `name_of("move", 33)` = the
  table name or `nil`. Categories: `type species move item ability nature flag`; another word is an error listing the right ones.
- `FLAG` = story flags with a name (about 1000 sure names plus about 610 AUTO-MADE guesses, see below): from the player's disc at start (`SEEN_<species>`, `CAUGHT_<species>`,
  `CHEST_F<floor hex>_<n>_OPENED`, `DOOR_F<floor hex>_ROW<n>_OPEN|CLOSE`) plus names from the game's code (`games/xd/data/story_flag_names.txt`,
  `number NAME meaning`, grows: `STORY_PROGRESS`, `PREMIUM_POINTS`...). `flag(FLAG.STORY_PROGRESS)`, `flag("STORY_PROGRESS")`. In `.xds`:
  `flag game.STORY_PROGRESS` (code names; the others after `xdscript.py flagnames <disc> --out names.txt --tool <xd_data_mods.exe>` and `--flag-names names.txt`). No name = write the number.
- **Auto-made flag names (new, , GUESSES).** At start (script mods only) the game reads every room script of the player's disc and names each flag nothing else names (about 610):
  `BEAT_<room>_<function>_<n>` (the function that sets it starts a battle), `GOT_...` (gives a Pokémon), `STATE_...` (several values: a counter, not a switch), `CHECK_...` (only ever tested) or `<room>_<function>_<n>`; the flag number is always the end of the name, so names never clash. Each comes with a meaning (where it is set and tested, which story step the function sets): `xd_data_mods <disc> --names flag_auto` prints `number NAME (check: meaning)`. **Never rely on one without reading its meaning, and tell the player a name is a guess.** Nothing of it is in the program or the mod (made from the disc each time, ~0.3 s). The story tool produces the same names (`tools/story/flagnames.py`; `test_flagnames.py` compares the two). Scripts READ game flags, only SET their own mod's flags.
- `STORY` = the VALUES of the story flag 964 (not flag ids), named from the disc's debug text: `flag(FLAG.STORY_PROGRESS) >= STORY.BEAT_CHOBIN`
  (= 40). STORY_PROGRESS is a number, not a switch. Never commit disc-derived name lists (the game builds them at start).
- Plain page: [lua-names.md](../modding/lua-names.md).

### The player's permission to run scripts

The launcher's Mods page shows "Runs scripts (Luau)" with an Allow box for each mod with `"scripts": true`. Off = `mods.ini` line
`id = on, scripts off` (per profile): that mod's `scripts/*.luau` are skipped (an Info line in the log; `LoadResult::scriptsSwitchedOff`),
everything else of the mod is used. A mod must not need its scripts to be valid; do not tell players to edit `mods.ini` by hand when the
Mods page can do it.

Sandbox: libraries `base` (assert, error, ipairs, pairs, next, select, type, tonumber, tostring, set/getmetatable,
raw*), `math` (no random, no trigonometry), `string` (byte, char, format, len, lower, rep, reverse, sub, upper; no
patterns; format has no `%p`/`%a`), `table` (concat, insert, move, pack, remove, sort, unpack). No io, os, require,
load, pcall, debug, coroutine. Use whole-number arithmetic (`//` for division). Limits: 200,000 steps per handler
call, 2,000,000 for a file's top level, 16 MB per mod, 32 files (256 KB each), 32 handlers per hook, `log` 16 lines per call,
2000 per mod, 200 characters per line. **Any error (including a bad return value, a missing field, a limit) switches that mod's
scripts off for the session** with an on-screen message. Scripts must not keep state across calls that matters for
correctness (nothing is saved).

Complete examples (copy these): `tools/examples/script_mods/` (`halve-damage`, `water-double`, `ai-expects-less`,
`battle-log`); `broken-script` and `endless-loop` are failure tests. A data example: `tools/examples/data_mods/add-zorblat`.
