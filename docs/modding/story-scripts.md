# Story and event scripts
> Early version: the format can still change.

**What this page covers:** how to change the game's story: what people say and do, cutscenes, doors, flags, battles, gifts and new rooms. It
describes the story tool, the text format of a room script, the functions a Luau script can call from the story, and the limits.
Story scripts, new rooms and the story functions belong to XD's script engine. In a mod that also names Colosseum, keep them in `games/xd/`
(see [two-games.md](two-games.md)). They are not available on Pokémon XG.

## What a room script is

Every room in the game has a **script**: a small program that runs what NPCs say, cutscenes, doors, flags and
battles in that room. The game calls the format "Tiga" bytecode. It is not human-readable. The story tool turns it
into a text file (`.xds`) you can edit, and turns your text back into the game's bytes.

Scripts are game data. The tool reads them from **your** disc and writes them to a scratch folder. Never put an
extracted or decompiled script in a mod you share, and never commit one to a repository. See
[sharing.md](sharing.md) for how to share a story mod safely.

## What is available

| Step | Command | State |
|---|---|---|
| Extract every room script from your disc | `extract` | Available |
| Turn a script into text | `decompile` | Available |
| Turn text back into a script | `compile` | Available |
| Prove the round trip on the whole disc | `selftest` | Available. 235 of 235 scripts come back byte for byte |
| Edit one script and check it parses back | `edittest` | Available |
| Write a mod folder from edited text | `mod` | Available |
| Read a mod's scripts back and report mistakes | `check` | Available |
| Run an edited script in the game | | Available |
| New message text in a room | `text @name` | Available. Only in rooms that have a text bank |
| Change the "common script" (shared by all rooms) | `mod` (a `script common_script` text) | Experimental. Installed from `common_script/` in the mod: see "The common script" below |
| Flags of your own, with names, saved in the normal save | `flag @name` | Experimental. Tested in the game: set, saved, continued |
| New rooms (a copy of an existing room with a script of its own) | `newroom`, `floor @name` | Experimental. Tested in the game: warp in, the room loads, warp back, saved |
| Lua called from a script | `lua "name" a b c` | Experimental. Tested in the game: the script asks a mod's Lua function and uses the answer |
| Trainer battles, items and Pokémon gifts from a script (and from Luau) | `trainer_battle`, `give_item`, `give_pokemon`, `battle_won` | Experimental. See "Battles, items and gifts" below |
| Wild battles and Shadow Pokémon gifts from a script (and from Luau) | `wild_battle`, `give_shadow_pokemon` | Experimental. See "Wild battles and Shadow Pokémon" below |
| Text in rooms that have no text bank (77 rooms) | `text @name` | Experimental. The mod ships `texts/<room>.json`, the game adds a bank to the room |
| People, zones, chests, music and doors in a room | `person @name`, `zone @name`, `mod --objects` | Experimental. Works in the game (a person, an area zone and a chest): [story-objects.md](story-objects.md) |
| Names for the game's own flags and variables | `flag game.NAME` | Experimental. A names file that can grow; in `.xds` only the code-named flags so far, in Luau about a thousand (see [lua-names.md](lua-names.md)) |

`person @name` and `zone @name` are two more words of the script text, for people and area zones added by an `objects/<room>.json` file: see [story-objects.md](story-objects.md).

`song @name` is one more word: it stands for the number of a **song your mod adds** (`sounds/songs.json`, see [music-and-sound.md](music-and-sound.md) "Adding a NEW song"),
for the command that starts a sound (`Sound.openPlay <number> ...`). The number is filled in when the game starts. A room's own music is set
in `objects/<room>.json` (`"music": { "bgm": "<song name>" }`) and needs no script. Without the mod, the number points at nothing and the song is not played.

The tool is a Python program in the repository, `tools/story/xdscript.py`. Run it with `python -I`.

```
python -I tools/story/xdscript.py extract   <disc> <outdir>
python -I tools/story/xdscript.py decompile <script.bin> <out.xds> [--disc <disc>]
python -I tools/story/xdscript.py compile   <in.xds> <out.bin>
python -I tools/story/xdscript.py mod       <in.xds>... --out <mod folder> --name <mod id> --disc <disc>
python -I tools/story/xdscript.py check     <mod folder>
python -I tools/story/xdscript.py check     <mod folder>
python -I tools/story/xdscript.py selftest  <disc> [workdir]
python -I tools/story/xdscript.py edittest  <disc> [workdir]
```

- `extract` writes one `<room>.bin` per room script, plus `common_script.bin` and a `manifest.tsv`.
- `decompile` with `--disc` shows message text as comments next to the message numbers, and also on the call lines (`Menu.winMsgOpenField(preset40, 50405, 1, 0)  text 50405: "..."`). With `--tool <xd_data_mods.exe>` too it shows the names of items and Pokémon next to their numbers (`Hero.scriptAddItem(4, 1)  = item POKé BALL`).
- `mod` writes a ready mod folder (`mod.json` and `fsys/<room>.fsys/#N.bin`) from your edited `.xds` files. Each room can
  be changed by one `.xds` file per mod.
- Write the extracted files under a scratch folder, **never** inside the repository.

## The `.xds` text format

One instruction per line. `;` starts a comment. A file has a header, tables, and functions.

```
xds 1
script D1_labo_B1            ; the room this script belongs to
ftbl_id 0x30042306           ; keep as is

func preprocess              ; a function; it runs until the next func
    line 97
    push.str "hello"         ; strings are written inline
    method G.fncPrintf       ; a command: Class.method
    pop 1
  L1:                        ; a label (any name)
    jz L1
    call modify_floor        ; call another function by name
    return
```

- The script is a **stack machine**: push the arguments, then call a method. The argument pushed *last* is the first
  argument (for class methods, the object).
- After each `method` line the decompiler adds a comment showing the call with its arguments when they are plain
  pushes, for example `method People.setDisp  ; People.setDisp(gvar1, 0)`.
- Instructions you will meet: `push.int`, `push.float`, `push.str`, `push.vec`, `push.people`, `load` / `loadref` /
  `store`, `pop`, `reserve`, `call`, `jmp`, `jz`, `jnz`, `method`, `return`, `end`.
- Method names (`People.setDisp`, `Menu.winMsgOpenField`, ...) are hints taken from the decompilation's function
  names. There are 24 classes and about 500 methods. A name like `Class.m0x1f` always works for a raw method id.
- Global variables of a script have no names on the disc; `decompile --gvar-names` makes guessed ones (`gvarname`, see "Names for a script's own variables" below). Without it they stay `gvar N`.
- Story flags: about a thousand have names in Luau ([lua-names.md](lua-names.md)); in a `.xds` file only the ones named by the game's
  code can be written by name so far (964 is story progress: `flag game.STORY_PROGRESS` where a flag number goes). Those names are in
  `games/xd/data/story_flag_names.txt`; the decompile shows `= game.NAME` next to a flag number it knows. The rest: write the number.

### Editing

You can change numbers, strings and jumps, add or remove lines, and add new functions (anything after a new
`func name` line). Add new `global`, `people` and `array` lines at the end of their lists. The compiler rebuilds the
tables and recomputes every size. It reports the line of an error, and unknown labels, functions and methods.

### New message text

A story mod can declare its own message texts in the `.xds` file and use them by name (in **every** room: rooms that have no text bank
get one from the mod, see "Rooms with no text bank" below):

```
text @machine_greeting "{07 00}Story mod says hello!{00}Want to use the HEALING MACHINE?"
...
    push.int @machine_greeting
```

The `mod` command gives them message numbers and adds them to the room's text bank.

### Rooms with no text bank
77 rooms of the game have no text of their own. A mod can still give them words: when the tool finds `text @name` in the script of such
a room, it writes **`texts/<room>.json`** instead of a changed copy of a bank (so the mod holds only your own words, no game data):

```json
{ "texts": { "900000": "{07 00}Hello from a room with no bank!{02}" } }
```

When the mods load, the game makes a text bank of those words and adds it to the room's archive (a new file inside it, the way the game's
own rooms with text have one). The numbers start at 900000: no bank of the game uses numbers that high, so yours never clash with the
game's. The bank takes its font and language from a bank of the game's rooms. One language per mod today (the one the words are in).
Rooms your mod adds with `newroom` work the same. The log says `Text of the room "..." ... a new text bank`.

## Make your first story mod

You will change the healing machine in the lab's first floor (room `D1_labo_1F`; the machine is in the room left of the
main hall). Two things change: the machine's question gets new words, and a new function you write shows a new line
before it. Your own disc gives the script; nothing of the game goes into the repository. `<disc>` is your own ISO and
`<work>` is any scratch folder outside the repository.

**1. Get the script as text.**

```
python -I tools/story/xdscript.py extract   <disc> <work>/bins
python -I tools/story/xdscript.py decompile <work>/bins/D1_labo_1F.bin <work>/D1_labo_1F.xds --disc <disc>
```

Open `D1_labo_1F.xds`. The function `tako_machine` is the machine. Its first `push.int 50405` has a comment with the
question the machine asks today.

**2. Declare two new lines of text.** Put them before `func preprocess`, then use the first one in place of `50405`:

```
text @machine_greeting "{07 00}Story mod says hello!{00}Want to use the HEALING MACHINE?"
text @bonus_line "{07 00}This line is new: a function the{00}mod added to the room's script.{02}"
...
    push.int @machine_greeting          ; was: push.int 50405
```

`{07 00}` starts a message window, `{00}` is a line break, `{02}` waits for a button. (More control codes:
[change-data.md](change-data.md).)

**3. Add a function** at the very end of the file. A message window opens with `Menu.winMsgOpenField` and closes with
`Menu.winMsgCloseField`, as in the machine's own code:

```
func bonus_line
    reserve 0
    push.int 0
    push.int 1
    push.int @bonus_line
    loadref preset[40]
    method Menu.winMsgOpenField
    pop 4
    push.int 1
    loadref preset[40]
    method Menu.winMsgCloseField
    pop 2
    cleartop 0
    return
```

Then call it as the first line of `tako_machine`, right after `reserve 2`:

```
    call bonus_line
```

(`tools/story/examples/healing_machine.py` makes exactly these edits to your decompiled file, for a repeatable test:
`python -I tools/story/examples/healing_machine.py <work>/D1_labo_1F.xds <work>/D1_labo_1F.edited.xds`. All its words are
its own.)

**4. Make the mod and check it.**

```
python -I tools/story/xdscript.py mod   <work>/D1_labo_1F.edited.xds --out <mods folder>/story-healing-machine --name story-healing-machine --disc <disc>
python -I tools/story/xdscript.py check <mods folder>/story-healing-machine
```

`<mods folder>` is the game's mods folder (see [getting-started.md](getting-started.md)). It is a new-playthrough mod:
switch it on in the launcher's Mods page and pick it on the New Game setup screen.

**5. Play.** Start a New Game, go to the lab (to skip the walk, the test switch `XD_WARP=0x7` starts you in that room),
and talk to the machine. First your new line appears, then the machine's question with your words.

### Mistakes the compiler stops

A mistake in the `.xds` is stopped by the compiler, with the line, before any mod is written:

```
error: bad.xds: line 180: call bonus_lne: no function or label of that name in this script
error: bad.xds: line 264: unknown method Menu.winMsgOpenFeild (use Menu.m0xNN for a raw id)
error: bad.xds: line 262: unknown text @bonus_lin (declare it: text @bonus_lin "words")
error: bad.xds: line 18: text @bonus_line: bad control code {zz}
error: bad.xds: line 155: expected num, found 'four'
```

### What the game does with a bad script

The tool is the first line of defence: `check` reads every script and text entry of a mod back and reports (exit code 1
when something is bad; unknown command numbers are only warnings).

```
BAD: D1_labo_1F.fsys/#1.bin: neither a script (TCOD) nor a text bank; the game switches this file off and keeps its own script
warning: D1_labo_1F.fsys/#1.bin: method Menu.m0x7fff is not a command the tool knows
ok: D1_labo_1F.fsys/#1.bin: script, 1776 bytes
```

A script that is not a valid script at all is switched off by the game when mods load, with a message naming the mod
and the file; the original script stays. A script that is well formed but calls a command the game does not know is
**not** caught: the game skips that call without a message and goes on. Use `check`.

## Battles, items and gifts
Four more words of the script text. They are written as one line and the tool turns them into the game's own commands, so a
battle, an item or a gift goes through exactly the code the game's own story uses. Every one leaves its answer in `reg[0]`.

```
    trainer_battle @side_rival          ; or a battle number: trainer_battle 268
    battle_won                          ; reg[0] = 1 when the player's side won the last battle
    give_item @side_gel 2 on_full too_full
    give_pokemon @side_pal 8 33 0 0 0 party_only on_full no_room on_pc sent_to_pc
```

| Word | What it does | `reg[0]` afterwards |
|---|---|---|
| `trainer_battle <battle>` | Starts a trainer battle with the fade the game's own story battles use (`Fight.scriptEncount`). A battle is a **row of the battle table**: the game's rows, or a row your mod adds (see [change-data.md](change-data.md) "New trainers and battles"). `@name` is a battle your mod adds; a number is any row of the game. | The game's result number of the battle (2 or 3 mean a side won) |
| `battle_won` | Asks whether the player's side won the last battle. | 1 or 0 |
| `give_item <item> <count> [on_full <label>]` | Gives items (`Hero.scriptAddItem`). The item is a number (`xd_data_mods <disc> --names item`) or `@name` for an item your mod adds. The count must be 1 or more: the game's own command gives back garbage for 0, so the tool refuses it. `on_full` jumps to the label when something did not fit. | How many did **not** fit: 0 = all given, 1 or more = bag (or that stack) full, -1 = no such item |
| `give_pokemon <species> <level> [move1 move2 move3 move4] [party_only] [on_full <label>] [on_pc <label>]` | Gives a Pokémon the way the game's own gifts do (you are its trainer, friendship 70, a Poké Ball, the Strategy Memo gets its entry). Moves left out (or 0) keep the level-up moves. `party_only`: never the PC. `on_full` jumps when there is no room at all, `on_pc` when the party was full and it went to the PC box. | 0 to 5 = the party slot, -1 = sent to the PC box, -2 = no room anywhere, -3 = not a Pokémon or a level outside 1 to 100 |

Use `reg[0]` the usual way to say "your bag is full" yourself:

```
    give_item @side_gel 2
    load reg[0]
    push.int 0
    expr ne
    jz all_given                         ; 0 left over: it all fit
    ...                                  ; show your "your bag is full" message here
```

Notes that come from the game:

- A battle needs a **battle row**, a trainer in a deck and a battle floor that exists. Your mod can add the row and the trainer; the
  battle floor is one of the game's (copy it from a battle you like). A new battle that is not "must win" does not send the player
  back when lost.
- The game remembers nothing about a gift or a battle but what the gift or battle itself changes: a script that runs again gives again.
  Keep a flag of your own (`flag @name`) for "already given".
- `trainer_battle` must be run from a script that can wait (a talk or event script). Not from `preprocess`.

### The same from Luau

A story function can do all of this too (names without game words, so a later port of the game keeps them):

```lua
story_function("reward", function()
  local r = give_item(ITEM.SUPER_POTION, 2)          -- {status = "ok" | "bag_full" | "no_such_item" | "refused", given, left}
  if r.status == "bag_full" then notice("Your bag is full.") end
  local g = give_pokemon(SPECIES.EEVEE, 8, MOVE.TACKLE) -- {status = "party" | "pc" | "no_room" | "bad", slot}
  start_battle("side_rival")                         -- starts when the function returns
  return 1
end)
```

- `give_item(item, count)`: a count of 0 or less is **refused** (status `"refused"`, a line in the log), not passed to the game.
- `have_item(item)`: 1 when the player has the item, else 0 (the game's own question; it does not count).
- `give_pokemon(species, level, [move1..move4], [party_only])`.
- `start_battle(battle)`: a battle number or the name of a battle your mod adds. The battle begins when the Lua function that called it
  **returns** (the script command that called the function runs it). Afterwards `battle_result()` says how it went:
  `{battle, result, won, lost}` (`won` = 1 when the player's side won, `lost` = 1 when a must-win battle was lost and the player was
  sent back). `battle_id("name")` is the row number of one of your battles. A battle cannot be started from a field hook (below).
- `notice("text")`: a short note at the top of the screen. Hooks use it; a story script shows its own message boxes.

## Wild battles and Shadow Pokémon
Two more words of the script text, and the same two in Luau. They were traced in the game first, so a gift or a battle goes through the code the game's
own story uses.

```
    wild_battle 2 @pal 9                       ; spot 1-3, a Pokémon (number or @name of your own), a level
    give_shadow_pokemon 81 on_full no_room on_have got_it     ; the Shadow's row; optional: level 30
```

| Word | What it does | `reg[0]` afterwards |
|---|---|---|
| `wild_battle <spot> <species> <level>` | Starts a wild battle **like the Poké Spots'**: the wild Pokémon is that species at that level (spot 1 = the Rock spot's battle, 2 = Oasis, 3 = Cave; the spot only chooses the battle's background and music). Balls, catching, the Pokédex and the end of the battle are the game's own. Nothing about the real spots changes: they keep their own species. | The battle row it started, 0 when it did not start (a bad species or level) |
| `give_shadow_pokemon <row> [level <n>] [on_full <label>] [on_have <label>]` | Gives a Shadow Pokémon **with its whole Shadow record**, the way the game gives its own Shadow gift: its Shadow moves, its Dark Point gauge, its place in the Shadow list and the Strategy Memo. `<row>` is the Shadow's row number in the game's Shadow list (1 and up; `shadow_species(row)` in Luau says which Pokémon it is). `level` left out = the Shadow's own level. | the party slot 0-5, `-2` the party is full, `-3` no such Shadow row or level, `-4` the player already has that Shadow |

How the game does these (so you know what to expect):

- **A wild battle.** A Poké Spot keeps one species as a story flag. A wild battle reads it, looks it up in the spot's list for the level range and makes the
  Pokémon. A `wild_battle` puts your species and level in place of that for **one** Pokémon of **one** battle: it is put away when the Pokémon is made, and again
  when the battle ends. If the battle never makes its Pokémon (a cancelled battle), the end of the battle still puts it away. A battle that starts from
  somewhere else never sees it.
- **A Shadow gift.** A Shadow Pokémon is a normal Pokémon plus a **Shadow record** in the save (its status, the order it was met, its place, the six stat
  randoms). The gift fills in the record exactly as the game's own gift does. It refuses what would damage a save: a row that is not in the game's Shadow list;
  a Shadow the player **already has** (snagged or purified: two Pokémon on one record), and a full party (the record's place can only be a party slot).
  Nothing else is changed; if the player had met that Shadow in a battle before, it keeps its place in the order of meeting.
- Saves: a Shadow gift writes the Shadow's record the way the game's own gift does (checked: after a save, the save reader shows the Pokémon as `[SHADOW #1, heart gauge 3000]` and the Shadow list with 1 snagged). A `wild_battle` writes nothing of its own: what the player catches is a normal Pokémon of a species the game has (or a species your mod adds, with the usual rules for added rows).
  species the game has, in a Shadow slot the game has).
- Like `trainer_battle`, `wild_battle` must run from a script that can wait (a talk or event script).

From Luau (story functions; names without game words, so a later port keeps them):

```lua
story_function("wild", function(spot, species, level)
  return wild_battle(spot, species, level) and 1 or 0     -- queued: it starts when the function returns, like start_battle
end)

story_function("shadow", function(row, level)
  local r = give_shadow_pokemon(row, level)             -- {status = "party" | "no_room" | "already_have" | "bad", slot}
  return r.slot
end)
```

`shadow_species(row)` is the Pokémon species number of a Shadow row (nothing when there is no such row). `tools/examples/script_mods/wild-and-shadow` has both.
Test them in the game with the dev menu action `lua wild 2 133 7` / `lua shadow 81 0` (`XD_DEV_ACTIONS=<frame>:lua wild 2 133 7`).

## Hooks for the field
Lua can also be told about things that happen while the player walks about. They are written like this (the same words work in every
Lua file of the mod):

| Hook | When | Your function gets |
|---|---|---|
| `on_room_ready(function(floor, mode) ... end)` | A room is set up: the hero stands in it, people, doors and chests are ready. The room's own script may still be starting: wait a frame if you need it. | `floor` = the room number, `mode` = 0 normal entry, 1 pushed room, 2 back after a battle, 3 CONTINUE |
| `on_battle_start(function(battle) ... end)` | Any battle (story, Colosseum, Bingo, Battle CD, Mt. Battle) was chosen, before the screen wipe | the battle row number |
| `on_battle_end(function(battle, result, lost) ... end)` | Back in the field after a battle | the row, the game's result number, `lost` = 1 for a must-win battle that was lost |
| `on_item_use(function(item) ... end)` | The player uses an item from the bag outside battle. Return `ITEM_USE.GAME` (or nothing) to leave it to the game, `ITEM_USE.REFUSE` ("it won't have any effect": stays in the bag), `ITEM_USE.CONSUME` (used, one is taken) or `ITEM_USE.KEEP` (used, kept, like a key item). To only watch, return nothing. | the item number |
| `on_step(function() ... end)` | Once per step of the hero (every 7 units walked in the story, not while the hero is locked) | nothing: use `position()` |
| `on_flag_changed(function(flag, old, new) ... end)` | A story flag **changed value** (a flag set to the value it already has does not count). Fires for every flag, the game's and your own (a handler that sets a flag of its own does not start itself again). | the flag's number (`FLAG.NAME` names it), the old and the new value |
| `on_money_changed(function(old, new) ... end)` | The player's money changed (shops, prizes, scripts, the dev menu) | the old and the new amount |
| `on_pokemon_caught(function(species, level, slot, shadow) ... end)` | A Pokémon was caught in a battle (a ball; a Shadow Pokémon snag too). Not gifts (they are yours: you know about them) | species, level, `slot` 1-6 = the party place, 0 = it went to the PC box; `shadow` = 1 for a Shadow snag |
| `on_menu_open(function(menu) ... end)` | A menu is about to open. Today only the field menu (the Start button): `MENU.FIELD`. More menus come with their own names | `MENU.FIELD` |

Rules: no clock, no random numbers, the same instruction limits as battle scripts. Everything in this step is for the US game (XD); the Pokémon XG disc does not take added rows, so new battles, trainers and room text are not used there (the log says so). A hook only exists when a mod has a function for it:
the plain game is not touched. A custom item use must do its own work (`give_item`, `set_flag`, ...) and say what it did with `notice`.
The four new hooks only **tell** your function; they cannot change or stop what happens (use `on_item_use` for that). They are wired into the game only while a mod has a handler for them, so the plain game never calls them. All of them are in the same sandbox as the other hooks; scripts of all mods together share a **step budget per game frame** (see [battle-scripts.md](battle-scripts.md) "The budget").

Two mods may both answer `on_item_use`: the one lower in the mod list that answers something other than `ITEM_USE.GAME` wins.

## Data API: read the game's tables from Luau
```lua
local s = species_info(SPECIES.EEVEE)   -- {hp, attack, defense, sp_attack, sp_defense, speed, base_exp, types = {a, b}, abilities = {a, b}} or nil
local m = move_info(MOVE.TACKLE)        -- {power, type, pp, priority, accuracy, range} or nil
```

These read the game's **live tables**, so a number a data file changed and the rows your mod adds are what you see. A number that does not exist gives `nil`. Together with `party()`,
`flag(n)`, `have_item(item)` and `position()` (see "The same from Luau" above) this is what a script can **read**. It is the same on every PC and in every replay: the tables
do not change while the game runs. `tools/examples/script_mods/data-api-demo` prints a few.

**Writing tables from a script: only for one battle, and only what no save keeps** (`battle_tables`, see "More script power" below). A table is part of what a save and a replay depend on, so a change that
lasts is a data file's job (see [change-data.md](change-data.md)): it changes a table the same way for every player and every replay, and the game checks it when it starts. The writes a script can do are named and
checked, one by one: `give_item`, `give_pokemon`, `give_shadow_pokemon`, `set_flag` (your own flags), `wild_battle`, `start_battle`, `value_set` (your own saved numbers), `save_game`, and for one battle
`set_trainer_team`, `lend_team`, `battle_rules` and `battle_tables`. If you need another one, say what you want to make (see [roadmap.md](roadmap.md)): each new write is built the same way (the game's own function, a plain
answer, a log line).

**A menu entry for a mod:** `add_menu_entry` puts an action on the MODS page inside the field menu's SETTINGS (see "Pages, picks and shops" below). The PC and PDA screens are the game's own and have no entry for mods.

## The common script
Every room's script can call functions of one shared script: doors, elevators, fades, the TVs, the shops' flag checks. The game does not keep it in a file of an archive (it is a resource inside
its program module), so a mod could not replace it. Now it can, from a folder of its own:

```
mods/my-mod/
  mod.json
  common_script/common_script.bin     the changed shared script (one file; any name)
```

How to make one (nothing of the game goes into the mod except your changes):

```
python -I tools/story/xdscript.py extract   <disc> <work>/bins
python -I tools/story/xdscript.py decompile <work>/bins/common_script.bin <work>/common.xds
(edit common.xds)
python -I tools/story/xdscript.py mod <work>/common.xds --out <mods folder>/my-mod --name my-mod --disc <disc>
```

`mod` sees `script common_script` at the top of the text and writes `common_script/common_script.bin` (it can be given together with room scripts, flags and Lua in one run). The game checks
the file exactly as it checks a room script (the layout; your own `flag @name`, `battle @name`, `item @name` words get their numbers), and a file that fails is **not used**: the game keeps its
own common script and says why in the log. Two mods with a common script: the lower one in the list wins (a message says so). `xd_mod_check` reports a broken one without a disc.
The mod needs "applies": "new_playthrough" (the default), like every story mod.

Good to know: the shared script is big (about 15 KB, 25 functions); change the one function you need. `tools/story/examples/common_script.py` is a small example (it adds a call to your Luau
function in `fade_in`; the proof is `tests/checks/mods_later_common_script.txt`: the game installs the mod's bytes and runs the function). Of the 25 functions the game itself only runs some
(`fade_in` and the like, not `preprocess` / `postprocess`): look at which a room really calls before you change one.

## Names for a script's own variables
A room script has a few variables of its own (`gvar 0`, `gvar 3`). They have no names on the disc. The decompiler can make a name for each one from how the script uses it:

```
python -I tools/story/xdscript.py decompile <script.bin> <out.xds> --gvar-names
python -I tools/story/gvarnames.py <script.bin>... [--out names.tsv]       a list: room, number, name, meaning
```

A name looks like `GV0_EVENTCTRLDOOR` (the variable is mostly handed to `Floor.eventCtrlDoor`) or `GV1_USE_PCBOX` (it is only set, in `use_pcbox`). Every name comes with a meaning line that
starts with `check:` and says where it starts, is set, is read and what it is handed to. **They are guesses made from the script itself**: read the meaning before you rely on a name. The text
then has `gvarname 0 GV0_EVENTCTRLDOOR` lines under the header and writes `gvar[GV0_EVENTCTRLDOOR]`; the compiler reads both and the bytes come out the same as with the numbers. Nothing is stored
anywhere: the names are made every time from your own script, like the flag names.

## Teams for one battle
Two Luau calls change who fights in the NEXT battle. Both touch only that battle's own copy of the trainer data, never your save, and neither draws a random number.

```lua
on_battle_start(function(battle)
  if battle == 225 then
    set_trainer_team(1, {                                   -- slot 1 = the first opponent
      { species = SPECIES.EEVEE, level = 10, item = ITEM.LEFTOVERS, moves = { MOVE.TACKLE, MOVE.QUICK_ATTACK },
        nature = NATURE.ADAMANT, ability = 1, ivs = 31, evs = { 252, 0, 0, 0, 0, 252 } },
      { species = SPECIES.PIKACHU, level = 11, item = ITEM.ORAN_BERRY, moves = { MOVE.THUNDERBOLT, MOVE.PROTECT } },
    }, { ai_items = { ITEM.FULL_RESTORE } })                -- (the third argument is optional: items the AI may use)
  end
end)

local ok = lend_team({ { species = SPECIES.EEVEE, level = 50, moves = { MOVE.TACKLE } } })   -- the PLAYER's side, for the next battle
```

- A **member** is a table: `species` (needed), `level` (1-100, default 50), `item`, `moves` (a list of 1 to 4; at least one), `nature` (0-24), `ability` (0 or 1: which of the species' two abilities),
  `friendship`, `ivs` and `evs` (a list of 6 in the order HP, Atk, Def, SpA, SpD, Spe, or one number for all six; defaults: IVs 15, EVs 0). A field the game does not know is an error with its name, so a typo cannot pass quietly.
  Teams have 1 to 6 members. The calls return `true`, or `false` with the reason in the log (not a real species, move or item, a level out of range...).
- **When**: call them before the battle starts (a story function before `start_battle`, or in `on_battle_start`, which runs before the game builds the trainers). The team lasts for **that one battle**:
  when it ends the call is forgotten, and the game's data was never changed for the next one.
- **Slot**: the place in the battle row's list of trainers (the log line `[teams] set_trainer_team: trainer N (place P of battle B)` tells which place was used). The player's own place is 0; the opponent of a one-on-one or a plain double battle is 1 (proven in battle 225, a double battle against one trainer); battles with more trainers continue the list (2 and 3), not tried yet. The Pokémon the foe had beyond your team are left out. A battle with a Shadow Pokémon on the foe's side loses
  it when you replace that slot (a plain Pokémon is made). If the game has no free place in its data for a team that long, the team is shorter and the log says so.
- **How it stays honest** (for the curious): the game builds a trainer's team from rows of the battle's trainer data (the Pokémon table of the trainer's deck). Before it does, the port writes your team into those rows
  in memory (empty places get rows nobody uses), the game builds the team exactly as for any other trainer (every random draw is the game's own, in the game's own order), and when the battle is over the game loads its data
  fresh (or the port puts the rows back). Your script draws nothing. With no mod using these calls, the game's own function runs and nothing else.
- **`lend_team` is a rental**: the player's side is built from your list in a new, empty Hero (no bag), the way the game's own Battle Bingo does it. The player's **real party and bag are never touched**:
  at the end of the battle the game copies the battle's player back into the save (HP, experience, evolutions), and that one copy is skipped for a lent team, so there is nothing to give back, even after a reset.
  Experience and level-ups of the lent Pokémon are lost with them. (Online rooms and the VS menu have their own rules and are not touched.)
- `tools/examples/script_mods/trainer-team` and `rental-team` are the examples; the proofs are `tests/checks/mods_later_trainer_team.txt` (battle 225 with a three-Pokémon team: species, level,
  item, nature read back from the running battle) and `mods_later_rental_team.txt` (the player fights with the lent two, the real party is the same before and after).
- XD only for now (the names are neutral; another game's port would give the same calls). `xd_mod_check` lists `set_trainer_team` and `lend_team` as XD-only.
- Not built: setting a trainer's *other* data (name, AI strength, trainer class). Pick a trainer row that already has what you want and change only the team.

## Random numbers for scripts
Luau has no random numbers of its own on purpose (two runs of one game must stay equal). `random_int` gives you some **without touching the game's**:

```lua
random_int(6)         -- 1 to 6
random_int(10, 20)    -- 10 to 20
```

It is worked out from the game's current random state (only READ, never advanced), the name of your mod and a counter of your own calls, with a fixed formula. So the same game moment gives the same
answers on every PC and in every replay (a recording puts the game's state back at each event), different moments give different ones, and everything the game draws is exactly as without your script.
Because it only reads, a player cannot get "another roll" by closing and reopening the game *at the same moment*, but loading the save again changes the game's own state and with it your numbers; if
a choice must survive a reload, make it once and keep it in a flag of your own (`flag @name`). `tools/examples/script_mods/script-random` draws six different numbers out of ten (a shuffle in Luau); the proof is
`tests/checks/mods_later_random.txt` (the game's random numbers equal the run without the mod, twice).

## More script power
Everything in this part is **deterministic** (no random numbers, no clock; a page reads the controller the game gets, so a recording and its replay make the same choices at the same game frames)
and **safe** (a call that cannot be done safely right now is refused with a plain line in the log, and your script goes on with a plain answer). `tools/examples/script_mods/script-power` uses all of it.

### Saved numbers and currencies

A story flag holds a small number. For a streak of 48, a point total or "which rentals are mine", a mod keeps **saved numbers**:

```lua
value_set("streak", 12)        -- true, or false + a log line when the rules below are broken
value_add("streak", 1)         -- the new number (it stops at the ends of its range)
value_get("streak")            -- 0 when nothing was saved under that name
saved_version()                -- the version of your mod that last saved numbers with this save (nil: none yet)
```

- A name is 1 to 40 characters: small letters, digits, `_` and `.`, starting with a letter. A mod keeps at most 256 names. A number is a whole number from -999999999999 to 999999999999.
- Only your mod sees its own numbers. Where they live: in the playthrough's **setup file next to the save** (`"mod_data"`), never inside the GameCube save. The file is written when the game saves, together with the save,
  and read back at CONTINUE; a New Game starts with none. So a number changed after the last save is gone when the player resets: exactly what a flag does.
- A **currency** is a saved number with a name to show and a cap:

```lua
define_currency{ id = "bp", name = "Battle Points", max = 9999 }   -- at the top of your script: declared every time the game starts (only the amount is saved)
currency_add("bp", 7)       -- the new amount, never over the cap
currency_spend("bp", 5)     -- true, or false (and nothing changes) when there is not enough
currency_get("bp")
```

A mod has at most 8 currencies. The currency lives only in the save-side data: nothing of it is in the GameCube save layout.

### Pages, picks and shops

A page is shown in the game's own menu style over the field, and **waits for the player** while the story script waits. A `mod_page` looks like a person talking: its text (the `title` first, as the first line) is in the game's message window at the bottom of the screen with the world behind it as it is, and the `choices` are in the game's choice box above the window (a long text turns its two-line pages with A; the choices come with the last one). A page does not block your Luau (a script cannot wait); it is *queued*, the story command shows it when your
function returns, and the answer comes to another story function whose name you give in `on_choice`:

```lua
story_function("ask", function()
  mod_page{ title = "Tower", text = "Streak {value:streak}", choices = { "Start", "Rules", "Leave" }, on_choice = "asked" }
end)
story_function("asked", function(index)      -- 1, 2, 3; 0 when the player pressed B
  if index == 1 then start_battle("tower_1") end
end)

mod_pick{ title = "Choose three", choices = { "Eevee", "Pikachu", ... }, pick = 3, on_choice = "picked" }   -- picked({1, 4, 5}); an empty list when cancelled
mod_shop{ currency = "bp", title = "Prize counter", items = { { item = ITEM.LEFTOVERS, price = 48 } }, on_choice = "shopped" }   -- shopped(purchases, last item)
```

- A page answers by a story function of **your** mod (`story_function`). A callback may queue the next page: a chain (a list, then a pick, then a shop) is how a menu is built. At most 32 pages in a row.
- `cancel = false` takes the B button away. A list with no `choices` is a text page with "OK". A pick has an extra "Done" row that works when exactly `pick` are ticked (START also confirms).
- The **shop** sells one item at a time for the currency, checks the money and the bag (a full bag sells nothing and takes no money), and logs each sale. It never touches the player's money.
- Where a page can be used: from a **story function** (a story script's `lua "name"` command, a hook is not a place that can wait). The input is the game's own controller stream, so replays repeat the choices.
- **A question from a person (the normal way).** A page looks like a person talking, so the natural place for it is a person's talk. Put `lua "ask_person" 0` in the person's talk script (a person of `objects/<room>.json`
  whose `"talk"` names the script; the story-objects guide shows the whole thing); the Luau function queues the page and its answer comes back to a second story function:

  ```lua
  story_function("ask_person", function()
    mod_page{ title = "Guide", text = "Which snack do you like best?", choices = { "Sweet", "Salty", "Sour" }, on_choice = "ask_answer" }
  end)
  story_function("ask_answer", function(index)        -- 1, 2, 3; 0 when the player pressed B
    mod_page{ title = "Guide", text = "Ha! I will remember that." }   -- a text-only page: the person's answer, then "OK"
  end)
  ```

  The player walks up to the person, presses A, hears the person's line, then sees the question in the person's own message window with the choice box above it, answers, and the person goes on. Nothing forces you to use a person:
  a mod may also show a page from any other story function (a room script, a menu action), and it looks the same.
- **An action on the MODS page:** `add_menu_entry{ id = "tower", label = "BATTLE TOWER", run = "tower_menu" }`. With at least one entry, the field menu's **SETTINGS** page has a **MODS** button (the first row of its MEMO page). It opens a page
  in the same bag style that lists the actions (at most 8 in all: DEX HELPER, QUICK HEAL, BATTLE TOWER...); the button is not there when no mod adds one. Choosing an action runs its story function while the game is frozen (the pages it queues are shown over that page). Not from there: `save_game`, battles (they need a game thread that can wait).
  Like SETTINGS itself this is off in replays and scripted runs, so its effects are not part of a recording: for anything that must replay (a tower run) start it from a story script (a person in a room), not from the menu.
  A PC or PDA screen entry is **not built**: those are the game's own screens with their own layout (a mod can add the field-menu entry only; `where = "field"` is the only value).

### Saving from a script

```lua
local r = save_game()      -- r.status: "saved", "not_here", "in_battle", "other_save", "card_error", "not_allowed"
```

The game's own save job, with the checks a normal save has: not in a battle, only where the game allows saving (its own "may save here" flag), a real memory-card result. One more: a card that holds **another
trainer's save is never written over** (the normal save asks the player; a script cannot ask). Only from a story function (not from a hook, which cannot wait for the card). The numbers your mod keeps are written
with the save, so a "save before every battle" lands both together.
The game's own "Saving..." and "Saved" messages wait for the player's A button, as in the save menu, and `save_game()` returns when the save is done. A test that saves from a script has to press A a few times after the call
(`tests/checks/mods_script_save.txt` does).

### Numbers and names inside messages

The game's messages already have slots for numbers and names; a script fills them, and the codes in any `text @name` line show them:

| Luau | Message code | Shows |
|---|---|---|
| `message_number(1, n)` / `message_number(2, n)` | `{2F}` / `{30}` | a number 0 to 999999999 |
| `message_text(1, "Ash")` / `message_text(2, ...)` | `{4D}` / `{57}` | a text of up to 46 characters |
| `message_species(species)` | `{4E}` | a species' name |
| `message_item(1, item)` / `message_item(2, item)` | `{2D}` / `{2E}` | an item's name |
| `message_move(move)` | `{39}` | a move's name |

```
text @streak "{07 00}Your streak: {2F}{00}Best partner: {4E}{02}"
```

Call the `message_...` functions in the story function that runs just before the message. In **your own pages** and in `notice()` the words are filled by `fill("text")`: `{value:streak}`, `{currency:bp}`,
`{currency_name:bp}`, `{number1}`, `{number2}`, `{text1}`, `{text2}` (mod_page text and choices are filled for you). Anything else in braces stays as written.

### Writing the game's tables: for ONE battle, with rules

A table change at run time would change what a save and a replay depend on, so it is allowed only where nothing is kept:

```lua
battle_tables{ moves = { { move = MOVE.TACKLE, power = 90, accuracy = 100, priority = 1, type = TYPE.FIRE } },
               species = { { species = SPECIES.EEVEE, types = { TYPE.NORMAL, TYPE.WATER }, abilities = { ABILITY.X, ABILITY.Y } } } }
```

- **When:** call it *before* the battle (a story function, or in `on_battle_start`, which runs before the battle's teams are built). A battle that already runs refuses it. It is armed for the next battle only.
- **What:** a move's `power`, `accuracy`, `priority`, `type`; a species' `types` and `abilities` (two numbers each). Nothing else: base stats and PP are **refused** with a message (a Pokémon's maximum HP, its stats and its PP
  are written into the save from them; use a data file, see [change-data.md](change-data.md), for those). At most 40 rows.
- **Rollback:** the game's own numbers are put back when the battle ends, when the scripts are reloaded and when another battle starts without an end being reported. A mod that is off never writes anything.
- XD only (the table layout is XD's; `xd_mod_check` lists `battle_tables` as XD only).

`battle_rules{ bag = false, gen4 = true }` is the same idea for the rules: no items for the player in the next battle (the "no battle items" rule), the Gen 4 mechanics on or off (needs the Gen 4 pack). They go
into force when the battle starts and the player's own rules come back when it ends. In the table form `level = 50` is refused (use a rule set with `level_all` for that); the members of `lend_team` and `set_trainer_team` have
their level in their table. The same name takes a **word** too: `battle_rules("name")` starts a rule set from the mod's `battle/*.json` (level cap, all at one level, banned items, inverse types, weather): see [battle-effects.md](battle-effects.md).

### Which function for which job

| Job | What to use |
|---|---|
| Set a foe's team | `set_trainer_team` |
| Lend the player a team | `lend_team` |
| Rules for one battle | `battle_rules`, `battle_tables` (above); a level rule is a rule set of [battle-effects.md](battle-effects.md), started with `battle_rules("name")` |
| Random choices | `random_int` |
| Numbers bigger than a flag | `value_get` / `value_set` / `value_add` |
| A pick screen | `mod_page`, `mod_pick` |
| Save from a script | `save_game()` |
| Numbers in messages | `message_number` and the codes above, `fill` |
| A shop with a currency | `define_currency`, `mod_shop` |
| Know which replay was kept | `on_replay_kept(function(label, battle, won) ... end)`: called when a battle's replay is kept (an automatic one, F7, or an online one). `label` is the replay file's name, `battle` the encounter number (0 online), `won` 1 or 0 |

## Story flags of your own

A story flag is a switch (or a small number) the game keeps in the save: "this door is open", "the quest has started". The
game's own flags are only numbers. Your mod can make **flags of its own**, with names, that are saved in the normal save.

In your `.xds` file write `flag @name` where a flag number goes (it is a `push.int` of the flag's number), and use the game's
flag commands on it:

```
func memory_line
    reserve 0
    flag @machine_seen              ; the flag's number
    method G.flagTest               ; is it on? (the answer is in reg[0])
    pop 1
    load reg[0]
    push.int 0
    expr eq                         ; 1 = still off: the first visit
    jz seen_before
    ...                             ; first visit: say something
    flag @machine_seen
    method G.flagOn                 ; switch it on: this goes into the save
    pop 1
    jmp done
  seen_before:
    ...                             ; every visit after that
  done:
    cleartop 0
    return
```

(`tools/story/examples/machine_memory.py` makes exactly this on the lab's healing machine: the first talk says one line, every
later talk, also after saving and continuing, another.)

- A flag is one bit by default. For a small number write `flagbits @counter 4` anywhere in the file (1 to 16 bits), then use
  `G.flagSet` and `G.flagGet`.
- `xdscript.py mod` finds every `flag @name` in all your scripts and writes the list for you as `data/story_flags.json`
  (`{"add": {"flags": {"machine_seen": {}, "counter": {"bits": 4}}}}`). You can write that file by hand too.
- **The number is not fixed.** The game gives each flag its number when the mods load, after the game's own flags (and after
  the Gen 4 pack's, when "Gen 4 Pokemon" is on), in the order of the mods and then the order in the file. It puts the numbers into
  your scripts at that moment, so a flag works whichever mods are on with yours. The names belong to your mod: two mods can
  both have a flag `quest`.
- **Saves.** The flags are saved in the normal save; nothing extra is stored. The save remembers which mods gave which numbers
  and **CONTINUE refuses to load it** if the same mods do not give the same numbers (another mod before yours, another order).
  The message names the mod and the flag. Same rule as for added Pokémon, moves and items
  ([add-pokemon-moves-items.md](add-pokemon-moves-items.md)).
- **The map name to number** is written to `added_ids.json` in the settings folder (and `xd_data_mods <disc> --ids` prints
  it): rows with the table `flag_data`.
- Room for 256 flags per mod set, 1 to 16 bits each. The game has about 9000 free bits for them.
- `xdscript.py check` tells you when a script uses a flag your mod does not declare.

## New rooms
A story mod can add a **new room**. It is a copy of a room the game already has: same walls, floor, camera, entrances and
people, but with its own floor number and **its own script**. You link to it from a room that already exists (a warp in
that room's script) and the new room's script decides what happens inside. Nothing is added to the game's files: the game
makes the new room's archive in memory when it starts, from your disc.

**1. Declare the room.** In the `.xds` of the room's script, next to the `script` line, write `newroom <name>`:

```
script D1_garage_B1            ; the room it copies (a plain room: the garage's basement)
newroom side_room              ; this script is for a NEW room of the mod, called side_room
```

`xdscript.py mod` then writes the script into `rooms/side_room/` (not into `fsys/`) and a data file
`data/story_rooms.json` that adds the room:

```json
{ "add": { "floors": { "side_room": { "copy": "D1_garage_B1" } } } }
```

`"copy"` is the name of a room on your disc (see them with `xd_data_mods <disc> --names floor`). You can write the file by
hand too. The new room's script is yours to change like any other (new text, flags, Lua calls).

**2. Link to it** from a script of an existing room (or from another new room) with the game's own warp command and the
room's name, written `floor @name`:

```
    push.int 1                  ; the entrance of the new room to arrive at (see below)
    floor @side_room            ; the new room's floor number; the game fills it in when mods load
    loadref preset[38]
    method Floor.link_16        ; the warp
    pop 3
```

The floor number is not fixed: the game gives each new room the next free number when the mods load (the first one gets a
number above all the game's own), and puts it into your scripts. Names belong to your mod.

**3. What the new room has, and has not.**

- It has the map, the collision, the camera, the entrances (numbered like the copied room's), the doors of the model (the
  game's door rows are copied for the new number) and the characters the copied room has.
- It has **no treasure chests** (the copied room's chests would hand out their items a second time) and no background-music row
  of its own unless the mod adds them in `objects/<room>.json` ([story-objects.md](story-objects.md)), and no places in the game's menus (about 36 places in the game compare against particular room numbers; a new number matches
  none of them, which is harmless).
- Leaving the room: the exits of the copied room work as they did, so they take the player to the neighbours of the copied room.
  To bring the player back where they came from, use a warp (`Floor.link_16`) with the real room's number as the example does.
- **Saving:** the game saves the room number in the normal save. Like added Pokémon and flags, a save from a playthrough with
  new rooms is only loaded with the same mods in the same order. The new-playthrough rule applies (the mod is chosen at New Game).
- Off on XG (new rows are not available there), like the other added rows.

`xdscript.py check` tells you when a `floor @name` has no room declared, and `rooms/<name>` that no data file adds.
`xd_data_mods <disc> --ids` prints the room's floor number; the game's log has a line `New room "side_room" (mod ...): floor N, archive ...`.

## Lua from scripts
A story script can ask a Lua function of the same mod a question and use the answer. That lets you write the logic in Luau
(count the party's types, compare numbers) and keep the script for what the game shows.

**In the script**, one line:

```
    lua "quest_check" 0           ; call the Lua function quest_check(0); the answer is in reg[0]
    load reg[0]
    push.int 0
    expr gt
    jz none_found
```

`lua "name" a b c` gives the function up to three whole numbers (the rest are 0). The tool turns that line into the game's
global command 0x100 (`G.luaCall`), which always takes the name and three numbers.

**In `scripts/*.luau`** (the mod needs `"scripts": true` in mod.json; `xdscript.py mod --lua <folder>` does it):

```lua
story_function("quest_check", function(wanted)
  local n = 0
  for _, p in ipairs(party()) do
    if p.types[1] == wanted or p.types[2] == wanted then n = n + 1 end
  end
  set_flag("quest_result", n)
  return n            -- a whole number (or true / false / nothing = 0) is the script's answer
end)
```

A story function can **read**: `flag(number)` (a game flag's value), `flag("name")` (one of the mod's flags, or a game flag
by its name: `flag("STORY_PROGRESS")` or `flag(FLAG.STORY_PROGRESS)`), `party()` (a list
of `{slot, species, level, hp, max_hp, types = {a, b}}`), `position()` (`{floor, x, y, z}`; x, y, z are 0 for now) and `room("name")` (the
floor number of a room of the mod). It can **change** only the mod's own flags: `set_flag("name", value)`. It cannot change the
game's flags, give items, start battles or move anyone: the story script does that. Scripts can also write `TYPE.GRASS`, `SPECIES.EEVEE`, `MOVE.TACKLE` and so on ([lua-names.md](lua-names.md)). The rules of the Lua sandbox are the same as
for battle scripts (see [battle-scripts.md](battle-scripts.md)): no files, no clock, no random numbers, an instruction limit
for each call. An error in a story function switches that mod's scripts off with a message and the script gets 0.
Two mods may use the same function name: write `lua "other-mod:name"` to pick one (the mod's id before the colon).
`xdscript.py check` warns when a script calls a Lua function that no `scripts/*.luau` of the mod registers.

**The example** `tools/story/examples/side_quest.py` (with `side_quest/side_quest.lua`) does all of this: the healing machine in the lab
sends you through a warp into a new room (a copy of the garage basement), the new room's script asks Lua how many Normal-type
Pokémon you have, shows one line or another, and sends you back. Lua writes the result into two flags of the mod; the machine
remembers it after saving and CONTINUE.

## How a story mod is packaged

A story mod is a normal mod:

```
story-healing-machine/
  mod.json                          "applies": "new_playthrough"
  fsys/D1_labo_1F.fsys/#N.bin       the compiled script (N is the script's entry number)
  fsys/D1_labo_1F.fsys/#M.bin       the room's text bank, when the mod has new texts
  rooms/<name>/#N.bin               the script (and #M.bin the text bank) of a NEW room of the mod
  data/story_flags.json, data/story_rooms.json   the mod's flags and new rooms
  scripts/*.luau                     the mod's Lua functions (mod.json "scripts": true)
  texts/<room>.json                 the mod's own text for a room that has no text bank
  data/battles.json (any name)      new battles, trainers and their Pokémon ("add" blocks)
```

It replaces the room script's entry inside the room's archive (the loader's "layer 1"). A script of another length
works: the game reads the sizes from the script's own header. The log says `bigger than the original`: that is
information, nothing is wrong.

**This kind of mod contains compiled game scripts and edited game text banks.** They are derived from the disc.
Sharing them needs care: see [sharing.md](sharing.md).

## Limits

- Every room can get new `text @name` lines. A room with a bank of its own gets them in a changed copy of that bank (game data: do not
  share it, see [sharing.md](sharing.md)); a room without one gets `texts/<room>.json` (only your words).
- The **common script** is one script for the whole game: a mod that changes it changes it for every room, and only one mod can (the lower one in the list wins). A change to a function that rooms of other mods call can break them; change as little as you can.
- A new room is a copy of an existing room: you cannot give it a new map yet (people, chests, zones, music and doors: [story-objects.md](story-objects.md)). No more than 32 new rooms in all. Lua called from scripts needs the port's build with the script hook (every release of this
  version); an older game skips the call and the script gets 0.
- Two story mods that change the same room replace the same files: the lower one in the list wins.
- A script's own **global variables** (`gvar 3`) have guessed names only (`--gvar-names`, each with a "check" meaning); the game's flags have names (about 1000 sure, about 610 auto-made guesses: [lua-names.md](lua-names.md)).
- Method names are hints from the decompilation, not official.
- A command the game does not know is skipped silently. Run `check`.
- Pages (`mod_page`, `mod_pick`, `mod_shop`) and `save_game` work from a story function only. A mod's menu entry is not part of a recording (like SETTINGS), so anything that must replay starts from a room script.
- Saved numbers are written with the game's own save; a number changed after the last save is gone at the next reset.

## Common mistakes

- Leaving extracted scripts inside the repository. They are game data.
- Changing the `ftbl_id` line.
- Two `.xds` files for the same room in one mod. One mod changes a room once.
- A message number that does not exist in that room's text bank. Use new `text @name` lines or existing numbers.
- Trusting a made-up name. Flag and variable names the tool makes are guesses: read the "check" meaning.
- If it fails, the game's own debug switch `XD_TIGA_TRACE=1` prints every script function and method call.

For what is coming (a higher-level language, new maps, new songs), see [roadmap.md](roadmap.md).

## See also

- [story-objects.md](story-objects.md): people, zones, chests, music and doors
- [lua-names.md](lua-names.md): names for flags and numbers
- [battle-scripts.md](battle-scripts.md): Luau hooks for battles
- [roadmap.md](roadmap.md): what is planned
