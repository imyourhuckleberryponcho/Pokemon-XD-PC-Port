# Story scripts, rooms and script power

The story tool, the `.xds` format, new rooms, people, zones and chests, field hooks and the extra script functions. Back to the [front page](README.md).

## Story scripts

Tool: `python -I tools/story/xdscript.py <command>` (see [story-scripts.md](../modding/story-scripts.md)). Commands:
- `extract <disc> <outdir>`: every room script as `<room>.bin`, plus `common_script.bin` and `manifest.tsv`. A script whose first line says `script common_script` is written by `mod` to `common_script/common_script.bin` in the mod: the game then uses it as the shared script of all rooms (checked like a room script; a failing file is ignored with a log line; the lower mod in the list wins). `decompile ... --gvar-names` adds guessed names for the script's own variables (`gvarname 0 GV0_...` lines; `gvar[GV0_...]`; every meaning starts with "check:"); `python -I tools/story/gvarnames.py <script.bin>... [--out f]` lists them.
- `decompile <script.bin> <out.xds> [--disc <disc>]`: text; `--disc` adds message text as comments.
- `compile <in.xds> <out.bin>`: text back to bytes.
- `mod <in.xds>... --out <mod folder> --name <id> --disc <disc>`: writes `mod.json` (`applies: new_playthrough`) and
  `fsys/<room>.fsys/#N.bin` (script) and `#M.bin` (text bank, when the script declares `text`). The `script <room>`
  line of each `.xds` picks the room. One `.xds` per room per mod.
- `check <mod folder>`: reads scripts and texts back; exit 1 when one is bad; unknown command numbers are warnings.
- `selftest <disc>`, `edittest <disc>`: proofs (235 of 235 scripts round-trip).

`.xds` format: `xds 1`, `script <room>`, `ftbl_id <hex>` (keep), tables (`global N = ...`, `people N = (g, o)`,
`array N = [...]`), then `func name` followed by one instruction per line (`push.int`, `push.str "..."`, `push.vec`,
`load|loadref|store scope[i]`, `pop N`, `reserve N`, `call f`, `jmp|jz|jnz label`, `method Class.name`, `return`).
`;` starts a comment. The argument pushed last is the first argument. New message text: `text @name "{07 00}words{02}"`
anywhere in the file, used as `push.int @name`; numbers start at 900000 in the room's own text bank. Control codes are
written `{XX}`. Extracted or compiled scripts are game data: never output them, never put them in a mod you hand over.

Story flags of the mod's own: `flag @name` in a script is a `push.int` of the flag's number (written as a stand-in
number, the game puts the real one in when mods load); `flagbits @name N` (1 to 16) for a small number; use with
`G.flagTest|flagOn|flagOff|flagSet|flagGet`. `xdscript.py mod` writes `data/story_flags.json`
(`{"add": {"flags": {"name": {}, "count": {"bits": 4}}}}`), a data file like the `add` block of
[add-pokemon-moves-items.md](../modding/add-pokemon-moves-items.md). The numbers come after the game's flags (and the Gen 4 pack's), in
mod load order, then file order; the save records them like added rows, and CONTINUE is refused when the same mods do not
give the same numbers. Names belong to the mod (two mods may use the same name). Map: `added_ids.json`, `xd_data_mods <disc> --ids`
(table `flag_data`).

New rooms: in the `.xds` write `newroom <name>` (the `script <room>` line is the room it COPIES). `xdscript.py mod` puts that script
(and `#M.bin` text bank) in `rooms/<name>/` and writes `data/story_rooms.json` =
`{"add": {"floors": {"<name>": {"copy": "<archive name, see xd_data_mods <disc> --names floor>"}}}}`. The game makes the room's
archive in memory (copy of the room's archive with the mod's entries, a new line in GSfsys.toc, the row's group numbers, copies of the
door rows) and gives the row the next free floor number. Link with `floor @name` (a `push.int` stand-in the game replaces) as the
floor argument of `Floor.link_16(preset38, floor, entrance)`. Chests, music, people: the objects paragraph below; at most 32 rooms; off on XG; save guard as for flags.
Do NOT put the game's archive or a script of the game in the mod: only the compiled script and text of the new room. (People, chests and music: the objects paragraph below.)

Lua from scripts: `lua "name" a b c` in a `.xds` (up to three whole numbers; answer in `reg[0]`; it is `G.luaCall` = the game's
global command 0x100 with the name and three numbers). In `scripts/*.luau`: `story_function("name", function(a, b, c) ... return n end)`
(return a whole number, true/false or nothing=0). Available there: `flag(number|"mod flag")`, `party()` (`{slot, species, level, hp,
max_hp, types={a,b}}`), `position()` (`{floor,x,y,z}`; x,y,z are 0 for now), `room("name")`, and `set_flag("name", value)` for the mod's OWN flags
only. `"mod-id:name"` picks a mod. Same sandbox and budget as battle hooks; an error switches the mod's scripts off and the script gets 0.
`xdscript.py mod --lua <folder>` copies the `.luau` files and sets `"scripts": true`. Example: `tools/story/examples/side_quest.py`.

People, zones, chests, music, doors in rooms (human page: [story-objects.md](../modding/story-objects.md)):
`mod --objects <folder of <room>.json>` copies them to `objects/<room>.json` (room = a `newroom` name of this mod, or a disc room's archive name,
`xd_data_mods <disc> --names floor`) and declares the flags they use (1 bit each) in `data/story_flags.json`. A mod with an `objects` folder is
`new_playthrough` (engine CheckApplies). Top-level keys: `people`, `replace_people`, `zones`, `chests`, `doors`, `music`; unknown keys are told.
- people `{name, model (row of peopleInfoData), x, y, z, angle (deg), talk, move (function names in the room's script), behavior stand|wander|look,
  solid, visible, look_at_hero, talk_through_walls, talk_start 0..3 (default 2 = body turns to the hero; 1 head only; 3 cannot be talked to), talk_end 0..1 (default 1 = turns back), blend (default true), name_text}` (the hero must be within ~10 units AND look at the person, 40 degrees either side, or A does nothing); in the `.xds`: `person @name` (a people-table entry
  with stand-ins the game fills: group = the room's, object = the person's number, 128 + its place in the list), `push.people @name`. Talk scripts
  run only for people in that table. Appended to the room's own people (`replace_people` drops them); one mod per room.
- zones `{name, event, on walk|touch|check, run, args[<=4]}` = a floor_event_list row (an event number the map already has; new triangles are not
  possible) or `{name, area:{x,y,z,radius}}` answered by the port: `zone @name` in a script (answer in reg[0]), `in_zone("name")` in Luau.
- chests `{name, x,y,z, angle, item (name or number), count, flag (required: the mod's story flag set when taken), look chest|pickup|pickup2,
  kind item|money|item_quiet}` = floor_tresure_list rows.
- music `{like: <disc room>}` | `{bgm, volume, fade, env_fade, flag}` | `"none"` = floorsoundTbl rows (fade = ms the new sound fades IN, the old music always fades out over 500 ms first; env_fade = ms the old ambient sound fades out; 0 = no fade). One pool of 48 people for the whole game (hero, party out, every loadInit person of the room, EVERY chest, script-opened people): room's own + mod people + chests <= 44 or some are silently missing. Models (people and the chests 0x2F80400/0x2F90400/0x2FA0400) must be in people_archive.fsys or the room's own archive, else the person is missing (log "person setup failed"). doors `{name, like (row of floor_door_list), type, part,
  collision, open_anim, close_anim, resource, open_flag, close_flag}` = a changed copy of that row.
- Limits 48 people, 48 chests, 64 zones, 16 doors per room; no new maps, models or songs; off on XG (added rows are dropped). Loader: games/xd/src/data/xd_rooms.cpp
  BuildObjects + xd_objects.cpp; people lists are built in common_rel.cpp (apply_floor_chars). Check: `xdscript.py check`, `xd_objects_test`.

Battles, items, gifts: one-line words in a `.xds` (all answer in `reg[0]`; `xds.py story_statement`):
`trainer_battle <@battle|N>` (`Fight.scriptEncount`, fade on), `battle_won` (global command 0x102: 1 if the player's side won), `give_item <@item|N> <count>
[on_full <label>]` (`Hero.scriptAddItem`; count >= 1 or the tool refuses; reg[0] = how many did NOT fit, -1 no such item; `on_full` jumps if not 0),
`give_pokemon <@species|N> <level> [m1..m4] [party_only] [on_full <label>] [on_pc <label>]` (global command 0x101 = the decomp's PortXD_GiftPokemon:
reg[0] = party slot 0-5, -1 sent to the PC, -2 no room, -3 bad). `battle @name` / `item @name` / `species @name` push a stand-in number (0x7A / 0x79 /
0x78 + the name's hash) for a battle / item / Pokémon the mod adds in a data file; the game replaces them when mods load (`Tiga_BattlePlaceholder`...).
`xdscript.py mod --data <folder>` copies the mod's own data JSON files (new rows) to `data/`; `check` warns about a name no data file adds.
New battle rows (data `add`, tables `battles` = fight_encount_data, `trainers` = story_trainers, `trainer_pokemon` = story_pokemon, also the other decks'
`<deck>_trainers` / `<deck>_pokemon`): see change-data.md "New trainers and battles"; trainer/Pokémon rows are appended inside the deck file
(`DataTable::addInChunk`, `AddedRow::inFile`: the game has nothing to grow), battle rows grow the PC table (`common_rel.cpp apply_added_rows`). A copy of a
trainer must not have Shadow slots (`darkPokemonFlags` 0); the trainer name text is the copy's. Names for Lua: `BATTLE.NAME` (the mod's own battles).
Lua (story functions and hooks): `give_item(item, count)` -> `{status = "ok"|"bag_full"|"no_such_item"|"refused", given, left}` (count < 1 refused, logged),
`have_item(item)` (1 or 0: the game's yes/no, not a count), `give_pokemon(species, level[, m1..m4[, party_only]])` -> `{status = "party"|"pc"|"no_room"|"bad", slot}`, `start_battle(n|"name")`
(runs when the calling story function returns; not from hooks), `battle_id("name")`, `battle_result()` -> `{battle, result, won, lost}` or nil,
`notice("text")`; new: **`set_trainer_team(slot, members [, {ai_items = {...}}])`** (XD only; slot 1 = the first opponent; the foe's team of the NEXT battle, from a list of member tables `{species=, level=, item=, moves={..}, nature=, ability=0|1, friendship=, ivs=, evs=}`; true / false + a log line; armed before the battle's trainers are built, e.g. in `on_battle_start`, lasts for that one battle; the game's data is never left changed; draws no random number), **`lend_team(members)`** (XD only; the player's side of the NEXT battle is these members, no bag, the real party and bag are never touched), **`random_int(n)` / `random_int(lo, hi)`** (game-neutral; script random numbers made from a READ of the game's random state, the mod's name and a call counter: same moment = same answer in every replay, the game's own random numbers untouched), `wild_battle(spot 1-3, species, level)` (queued like start_battle; the Poké Spot style wild battle with that species and level; true / false), `give_shadow_pokemon(shadow_row [, level])` -> `{status = "party"|"no_room"|"already_have"|"bad", slot}` (the whole Shadow record is made as the game's own gift does it; party only; refuses a Shadow the player already has), `shadow_species(row)`, and the read-only data API `species_info(n)` / `move_info(n)` (live tables; nil when missing). In `.xds`: `wild_battle <spot> <@species|N> <level>` (global command 0x104; reg[0] = battle row, 0 = not started) and `give_shadow_pokemon <row> [level N] [on_full <label>] [on_have <label>]` (0x103; reg[0] = party slot 0-5, -2 full, -3 bad, -4 already have it). Dev menu action for tests: `lua <function> [a b c]`. Hooks (registered with functions, wired in the game only when a mod has one): `on_room_ready(function(floor, mode))` (mode 0 normal,
1 push, 2 back by pop, 3 CONTINUE), `on_battle_start(function(battle))`, `on_battle_end(function(battle, result, lost))`,
`on_item_use(function(item))` returning `ITEM_USE.GAME|REFUSE|CONSUME|KEEP` (0..3; the handler runs when the player chooses USE in the bag outside battle,
once per use), `on_step(function())`; new: `on_flag_changed(function(flag, old, new))` (only real changes; a handler's own set_flag does not re-enter), `on_money_changed(function(old, new))`, `on_pokemon_caught(function(species, level, slot, shadow))` (a catch in battle: slot 1-6 party, 0 PC box; not gifts), `on_menu_open(function(menu))` (`MENU.FIELD` only today). They only tell; they cannot change or stop anything. Raw names: `field.flag_changed|money_changed|pokemon_caught|menu_open` with `e.flag, e.old, e.new / e.old, e.new / e.species, e.level, e.slot, e.shadow / e.menu`. The same through `on("field.room_ready"|"field.battle_start"|"field.battle_end"|"field.item_use"|"field.step", fn(e))`
with `e.floor, e.mode / e.battle / e.battle, e.result, e.lost, e.won / e.item`. Deterministic (no clock, no random). These are the decomp's PC-only pointers
(`PortXD_OnFloorReady` ...) set only when used (`script_hooks.cpp WireFieldHooks`; log line `[script] field hooks wired: ...`).

Text for rooms with no text bank: when `text @name` is used in a script of such a room (`xdscript.py mod` checks the disc), the tool writes
`texts/<room>.json` = `{"texts": {"900000": "words", ...}}` (numbers from 900000, the same as in rooms with a bank). The game makes a bank (group 0,
sorted, font and language bytes copied from a room bank of the disc) and ADDS it to the room's archive as a new type-5 entry
(`Fsys_AddEntry`, `xd_rooms.cpp BuildRoomTexts`). Works for disc rooms (by archive name) and `newroom` rooms. A mod with a `texts` folder is `new_playthrough`.
One language per mod (the language of the disc's room banks); off on XG.

Limits to state honestly: script variables have only guessed names (`--gvar-names`) and about 610 flags have only auto-made guesses; method names are
hints; a command the game does not know is skipped without a message (`check` warns); a script entry that is not a
valid script is switched off by the game with a message and the original is kept.

### More script power

Names (all neutral except where marked): saved numbers `value_get(name)`, `value_set(name, n)` (true / false), `value_add(name, n)` (new number), `saved_version()` (the mod version that wrote the save's numbers, nil: none);
names are `[a-z][a-z0-9_.]{0,39}`, at most 256 per mod, numbers within +-999999999999; kept in the setup file's `mod_data` next to the save, written with each game save, read at CONTINUE, empty at New Game.
Currencies: `define_currency{id, name, max}` at the top of a script (declared each start; only the amount is saved), `currency_get(id)`, `currency_add(id, n)` (capped, returns the amount), `currency_spend(id, n)` (true / false, nothing changes when short). At most 8 per mod.
Pages (queued, shown when the story function returns, the answer goes to a `story_function` of the same mod named in `on_choice`): `mod_page{title, text, choices, on_choice, cancel}` -> callback(index, 0 = B),
`mod_pick{title, text, choices, pick, on_choice}` -> callback({indices}), `mod_shop{currency, title, items = {{item, price}}, on_choice}` -> callback(purchases, last item). Max 32 pages in a row; story functions only. A page looks like a person talking: ask it from a PERSON's talk script (`lua "ask_person" 0` in a person's `"talk"` script; the player walks up, presses A, hears the line, sees the question, answers). Not enforced: any story function may show a page.
`add_menu_entry{id, label, run, where = "field"}`: the field menu's SETTINGS page gets a MODS button (first row of MEMO; hidden when no mod has an entry) that lists the entries; the entry's function runs with the game frozen; not recorded; no `save_game` or battles from there; PC / PDA entries do not exist.
`save_game()` -> `{status = "saved" | "not_here" | "in_battle" | "other_save" | "card_error" | "not_allowed"}` (story functions only; the game's own checks; never over another trainer's save).
Message slots: `message_number(1|2, n)` ({2F} / {30}), `message_text(1|2, s)` ({4D} / {57}), `message_species(n)` ({4E}), `message_item(1|2, n)` ({2D} / {2E}), `message_move(n)` ({39}); `fill(text)` replaces `{value:name}`, `{currency:id}`, `{currency_name:id}`, `{number1}`, `{number2}`, `{text1}`, `{text2}`.
One battle: `battle_rules{bag = false, gen4 = true|false}` (level is refused: not built), `battle_tables{moves = {{move = MOVE.X, power, accuracy, priority, type}}, species = {{species = SPECIES.X, types = {a, b}, abilities = {a, b}}}}` (XD only; before the battle or in `on_battle_start`; put back at the battle's end; stats and PP are refused: they are kept in the save).
Hook `on_replay_kept(function(label, battle, won))` (called when a replay is kept; M12). `battle_rules` takes a TABLE (`{bag = false, gen4 = true}`: this script power) or a NAME (`battle_rules("name")`: a rule set of the mod's `battle/*.json`, which has the level rules `level_cap` / `level_all`). Do not promise a PC / PDA menu entry or table writes that last.

## Taking a Pokémon out of a mod

`xd_mod_extract <disc> <modded save.gci> list` / `take <N> <plain save.gci> [--species-map a=b] [--move-map a=b] [--drop-moves] [--item-map a=b | --drop-item] [--party] [--out f | --in-place]`.
Without `--out` / `--in-place` it is a dry run. Refuses (exit 1, plain words): Shadow Pokémon, a mod species / move / item with no map, a Pokémon the plain game's legality check
rejects after the changes, no free place. A mapped species gets experience, stats and HP made again; friendship, nature, IVs, EVs, OT, nickname stay. The new save is read back and checked before success.
The source save is never changed. The launcher's Save page has the same thing as a button (**Take a Pokémon out of a mod...**, next to Colosseum import): it drops mod-only moves / items and leaves mod-only species and Shadow Pokémon behind (no species map there). Page: add-pokemon-moves-items.md "Taking a Pokémon out of a mod".
