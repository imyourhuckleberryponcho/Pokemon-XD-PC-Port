# Battle scripts (Luau)

> Early version: the format can still change.

> **Which game?** The hook names (`battle.damage` ...) and the name tables (`TYPE`, `SPECIES`, `MOVE`, ...) are game-neutral. Today only
> XD offers the battle hooks, and the numbers behind the names come from the disc that runs. `story_function`, `position`, `room`,
> `in_zone`, `FLAG` and `STORY` are **XD only**. If a mod may one day run in Colosseum too, keep XD-only scripts in `games/GXX/scripts`
> (see [two-games.md](two-games.md)).

**What this page covers:** writing a small script that changes how battles play. A script is a text file in the Luau language (see
[luau.md](luau.md)). The game runs it at fixed **hook points**, for example "a hit is about to take damage". The examples below are complete.

Working examples are in the repository, in `tools/examples/script_mods`: `halve-damage`, `water-double`,
`ai-expects-less`, `battle-log` and `names-demo` (`halve-damage` is typed Luau in the new `games/xd/scripts/` layout; `water-double` keeps the older `.lua` name on purpose) (names instead of numbers, see [lua-names.md](lua-names.md)). The language and the limits: [luau.md](luau.md).

## Make a script mod: Water moves do double damage

1. Folder `water-double` with `mod.json`:

   ```json
   {
     "id": "water-double",
     "name": "Water moves double",
     "version": "1.0",
     "author": "Your name",
     "description": "Water-type moves do double damage.",
     "game": "GXXE01",
     "scripts": true,
     "script_api": 1,
     "applies": "new_playthrough"
   }
   ```

   `"scripts": true` is required. Without it the game ignores the `scripts` folder (and says so).

2. File `scripts/damage.luau`:

   ```lua
   on("battle.damage", function(e)
     if e.move.type == TYPE.WATER then
       return e.damage * 2
     end
   end)
   ```

3. Switch the mod on and start a New Game. Script mods are `new_playthrough` mods.

## How a script works

- `on("hook name", function(e) ... end)` says "run this function at that hook". `e` is a table with facts about what is
  happening.
- For hooks that can change a number, **return a whole number** to set it. Return nothing to leave the game's number.
- For the other hooks the return value is ignored.
- Numbers: a script has one number type. A decimal result is cut to a whole number when you return it (`e.damage / 2` gives the same as `e.damage // 2`), so either division works for a return value. Use `math.floor` when you need a whole number in the middle of your own sums.
- `log("text", value)` writes a line to the game's log, like `[script water-double] text value`. (`print` does the same.)
- A mod can have several `.luau` files. A hook can have several handlers (up to 32). They run in order.

## The hooks

| Hook | When it runs | What is in `e` | What to return |
|---|---|---|---|
| `battle.damage` | A hit's damage is final (crits, types, the random roll, Endure) and about to be subtracted from the defender. Only hits on the defender with damage above 0. Not recoil. | `attacker`, `defender` (each: `species`, `level`, `hp`, `max_hp`), `move` (`id`, `power`, `type`), `game_damage` (the game's own number), `damage` (the number so far) | The new damage. The port keeps it between 1 and 9999. |
| `battle.damage_predict` | The trainer AI asks how much a move would do. It asks many times a turn. The AI never sees changes made in `battle.damage`. | `attacker`, `defender`, `move.id`, `game_damage`, `damage` | What the AI will believe (1 to 9999). Real hits are not changed. |
| `battle.move_used` | A move was used. Runs after the whole move, so `hp` is the value after it. | `attacker`, `defender`, `move.id`, `turn` | Ignored |
| `battle.turn_start` | The actions of a turn are about to run. | `turn` (1 in the first turn) | Ignored |
| `battle.turn_end` | A turn is over. `turn` is already the next number. | `turn` | Ignored |
| `battle.end` | The battle is over. | `result` (the game's number: 2 or 3 means a side won), `won` (1 = your side), `turn` | Ignored |
| `battle.ability` | **Every time the game asks what ability a battler has**: all the ability checks (Intimidate on switch-in, Static on contact, Levitate against Ground moves...) ask the same question, and so does the trainer AI. Asked a lot, so keep the handler short (see "The budget" below). | `species`, `level`, `hp`, `max_hp` of the battler, `game_ability` (the game's own number) | An **ability number** (`ABILITY.INTIMIDATE`) the game should use for this battler for this question. Return nothing = the game's own. Return 0 = no ability at all. Only what the game *sees* changes: the Pokémon's real ability in the save is never touched, and the ability names on screen stay the game's |
| `battle.ai_switch` | The trainer AI asks "shall I swap this Pokémon out?" (it asks once for every Pokémon that has not got an action yet, when it may switch at all) | `species`, `level`, `hp`, `max_hp` of that Pokémon | `AI.VETO` (1): the AI does **not** pick a switch for this Pokémon this turn (it goes on to choose a move). Nothing or 0 (`AI.ALLOW`): the game's own choice |
| `battle.ai_item` | The trainer AI asks "shall I use an item (Full Restore, X Speed...) on this Pokémon?", when the battle allows items | the same | `AI.VETO`: no item for this Pokémon this turn. Nothing or 0: the game's own choice |

Two examples of the new hooks:

```lua
-- Every Pikachu fights as if it had Intimidate (the game's checks see it; the save does not change)
on("battle.ability", function(e)
  if e.species == SPECIES.PIKACHU then return ABILITY.INTIMIDATE end
end)

-- The trainer AI never uses items (it still switches and attacks)
on("battle.ai_item", function(e) return AI.VETO end)
```

The ability hook is asked far more often than the damage hook (a few dozen times a turn in a double battle), and the trainer AI's own calculations ask too, so a
handler should not do more than a few lines. The frame budget (below) protects the game if it does. The two AI hooks are **vetoes** only: they can stop the AI from doing
something, not make it do something it did not choose. (A veto asks no random number; the game's own AI code that would have run is skipped, so a battle with a vetoing mod
is a different battle from the plain one, by design.)

**Tested in a real trainer battle.** The AI asks `battle.ai_switch` in battles whose trainer is allowed to switch (the Colosseum battles of the story, for example battle 55, first opponent;
asks four times in a few turns; `tools/examples/script_mods/ai-neutral` logs each question and lets the game decide, and with it on the whole battle is exactly the battle without it, random numbers
included: `tests/checks/mods_later_ai_neutral.txt`). `ai-no-items` (the veto) answers the same questions with `AI.VETO`; the battle then plays differently from the plain one (the AI's own switch
code was skipped) and the same twice (`mods_later_ai_veto.txt`). Good to know: the story trainers' AI data say "never switch, never use items" for most battles (the data: `pokeChgRate` and
`itemUseRate` of the trainer's AI row, `trainerItem` of the trainer), so for them the questions are never asked at all, with or without a script; a mod can change that with a data
file (the AI table) or with `set_trainer_team(..., {ai_items = {...}})` plus an AI row that uses items. The item question was **not** seen asked in any battle of the plain game's data, so
the example `ai-items-demo` makes the game ask it (a data file gives the foe's AI row an item rate and `set_trainer_team` gives it items): the question is asked and answered in a real battle (`mods_later_ai_item.txt`, `mods_later_ai_item_veto.txt`). A veto that visibly stops the AI from using an item or switching is **not** shown yet: in that battle the game's own answer was "no" anyway.

`species` and `move.id` are the game's numbers. You do not have to write numbers: `SPECIES.EEVEE`, `MOVE.TACKLE`,
`TYPE.GRASS` and similar names work in every script (read from your disc). See [lua-names.md](lua-names.md).

`move.type` is the game's type number: compare it with `TYPE.WATER`, `TYPE.GRASS` and so on. List the exact names
with `xd_data_mods <disc> --names type`.

## More examples

Halve all damage (`halve-damage`):

```lua
on("battle.damage", function(e)
  return e.damage // 2
end)
```

Make the trainer AI think moves hit half as hard (`ai-expects-less`). The AI then chooses moves and "can I KO?"
decisions on those numbers, while hits are untouched:

```lua
on("battle.damage_predict", function(e)
  return e.damage // 2
end)
```

Write the battle to the log (`battle-log`, changes nothing):

```lua
on("battle.turn_start", function(e)
  log("turn_start " .. e.turn)
end)

on("battle.move_used", function(e)
  log("move_used turn " .. e.turn .. ": species " .. e.attacker.species ..
      " (hp " .. e.attacker.hp .. "/" .. e.attacker.max_hp .. ") uses move " .. e.move.id)
end)

on("battle.turn_end", function(e)
  log("turn_end " .. e.turn)
end)

on("battle.end", function(e)
  log("battle_end result " .. e.result .. " won " .. e.won .. " after turn " .. e.turn)
end)
```

A low-level-Pokémon helper: a level 5 or lower attacker does 10 extra damage:

```lua
on("battle.damage", function(e)
  if e.attacker.level <= 5 then
    return e.damage + 10
  end
end)
```

## Scripts in the field
The same Luau scripts can also listen to the field: a room being set up, a battle starting or ending, an item used from the bag, a step of the
hero. They are written `on_room_ready(function(floor, mode) ... end)`, `on_battle_start`, `on_battle_end`, `on_item_use` and `on_step`, and can
give items and Pokémon, start battles and show notes. All of it is in [story-scripts.md](story-scripts.md) ("Hooks for the field" and
"The same from Luau"). The rules above (names, sandbox, no clock, no random numbers) are the same.

## The sandbox: what a script cannot do

Scripts run in a locked box so that one script cannot hurt the game, the save or another mod.

- Each mod has its own Luau world. One mod's failure does not touch another.
- Allowed: `assert`, `error`, `ipairs`, `pairs`, `next`, `select`, `type`, `typeof`, `tonumber`, `tostring`, `unpack`, `setmetatable`,
  `getmetatable`, `rawget`, `rawset`, `rawequal`, `rawlen`; `math` (abs, ceil, clamp, floor, fmod, huge, max, min, modf, pi, round,
  sign, sqrt); `string` (byte, char, format, len, lower, rep, reverse, sub, upper); `table` (clear, clone, concat, find, insert,
  move, pack, remove, sort, unpack); `bit32`; and the port's `on`, `log`, `print`, `API_VERSION` (1), `SCRIPT_LANGUAGE` ("Luau"),
  the name tables (`TYPE`, `SPECIES`, `MOVE`, `ITEM`, `ABILITY`, `NATURE`, `FLAG`) and `name_of` / `find`
  ([lua-names.md](lua-names.md)). The library tables are read-only.
- Not allowed: files, the clock, random numbers (a script cannot touch the game's random sequence), `pcall`,
  `load`, `require`, `os`, `io`, `debug`, `coroutine`, `utf8`, `buffer`. `string` has no pattern functions (`find`, `match`, `gsub`).
  `string.format` refuses `%p` and `%a`.
- Limits: one handler call may run at most 200,000 steps (function calls and loop turns). A file's top level may run 2,000,000. A mod may use
  16 MB. At most 32 script files per mod, each up to 256 KB, and 32 handlers per hook.
- **The budget for a whole frame.** On top of the limit for one call, **all scripts of all mods together** may run at most 1,000,000 steps in one game
  frame (a frame is what the game draws about 30 times a second). It is counted in steps, not seconds, so it is the same on every PC and in every replay.
  Normal scripts use a few dozen steps. When the frame's steps are used up, a handler that is running is **cut** (its answer is not used) and the handlers that
  would have run after it **wait for the next frame**. A line in the log says so once a frame (`the scripts used up their 1000000 steps for this game frame`).
  This does **not** switch a mod off (running past the budget is a crowded frame, not a mistake in your script), but a handler that was cut did not finish: do not
  keep half-finished work in your own variables. A handler that cannot finish within the per-call limit is still switched off, as before. A damage hook that is
  skipped for a frame gives the game's own damage for that hit.
- `log` writes at most 16 lines per call and 2000 per mod; each line is cut at 200 characters.
- **Any error switches that mod's scripts off for the rest of the session.** A message appears on screen
  ("Script mod X switched off: ...") and in the log, and the game goes on with its own numbers. An error includes a
  wrong return value, a missing field, running too long, and using too much memory.
- Scripts keep nothing between runs. Work from the event `e` each time. Variables you keep at the top of a script only
  last until the game closes, and are not saved. Do not depend on them after CONTINUE.

### Why so strict: replays

The port records games and replays them to check that nothing changed. A script must give the same answer every time
it gets the same event. That is why there is no clock, no random, and why limits count instructions, not seconds.

## Test it

1. Switch the mod on, start a New Game, go to a battle.
2. Look in the log for `[script <your id>] ... file(s) loaded`. If it says "not used", the reason follows.
3. Add a `log(...)` line to see the values you get.
4. Break it on purpose once. `error("test")` inside a handler must give the "switched off" message and a normal battle.
5. **Faster tries (developers):** with the setting **Reload mods while playing (developers)** on, saving a changed script (or pressing **F6**) loads
   the scripts again while the game runs, at a quiet moment (not in a battle). The script starts fresh, as at the start of the game. Never in Original.
   See [reload.md](reload.md).
6. Examples to copy for tests: `tools/examples/script_mods/broken-script` (an error) and `endless-loop` (stopped after
   200,000 instructions).

## Common mistakes

- Forgetting `"scripts": true` in `mod.json`.
- Using `/` and getting a decimal number. Use `//`.
- Returning nothing when you meant to change a number, or returning a number from a hook that ignores it.
- Using a hook name that does not exist. The error lists the real ones.
- Using `math.random`. It is not there.
- Expecting `battle.damage` to change the trainer AI. That is `battle.damage_predict`.
- Reading a field that does not exist (for example `e.attacker.badge_count`). That is an error and switches the mod off.

## Rules and table changes for one battle

`battle_rules{bag = false, gen4 = true}` and `battle_tables{...}` arm rules and a few table values (a move's power, accuracy, priority, type; a species' types and abilities) for the NEXT battle only; the game's own
numbers are put back when it ends. They are explained, with what is refused and why, in [story-scripts.md](story-scripts.md) "More script power". The hooks above see the changed numbers while the battle runs (`move_info` reads the live table).

## See also

- [luau.md](luau.md): the script language and its limits
- [lua-names.md](lua-names.md): names instead of numbers
- [battle-effects.md](battle-effects.md): effects made of JSON blocks, without a script
- [story-scripts.md](story-scripts.md): scripts for the field
- [reload.md](reload.md): try a changed script without restarting
