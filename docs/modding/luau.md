# Script mods are written in Luau

> Early version: the format can still change.

**What this page covers:** the script language of mods (Luau): how it differs from Lua 5.4, which parts are allowed, and the limits that keep a script
from freezing the game.

Scripts in a mod (the `scripts/` folder, `"scripts": true` in `mod.json`) are written in **Luau**. Luau is Lua with a few
extras and a stricter, safer engine. If you have written Lua 5.1 before, you already know almost all of it. Before version 0.7.0 the
port used Lua 5.4; the hooks, the names (`TYPE`, `SPECIES`, ...), `log`, `on`, `story_function` and the permission
("this mod runs scripts") are **the same**. Only the language underneath changed.

## File names

- New name: `scripts/damage.luau`.
- The older `.lua` name still works and is read as Luau. The mod checker says so in a gentle note. Rename when you like.
- A script is plain text. Compiled code is never accepted.

## What Luau adds (all optional)

```lua
--!strict                                  -- a note for editors; the game does not care
type Hit = {damage: number, game_damage: number}

on("battle.damage", function(e: Hit): number   -- ": Hit" and ": number" are type notes, removed before the script runs
  local total = 0
  for i = 1, 5 do
    if i == 3 then continue end            -- `continue` skips to the next turn of the loop
    total += i                             -- += -= *= /= .= work
  end
  local note = if total > 5 then `big {total}` else "small"   -- if as a value, and `text {like this}`
  return math.floor(e.damage / 2)
end)
```

Types are only notes. A wrong type note never stops the mod. (An editor that knows Luau will point it out to you.)

## What is different from the old Lua 5.4 scripts

| Lua 5.4 (before 0.7.0) | Luau (now) |
|---|---|
| `7 // 2` (whole division) | works the same (`//` is floor division in Luau too) |
| `a & b`, `a \| b`, `a ~ b`, `a << 2`, `~a` | `bit32.band(a, b)`, `bit32.bor`, `bit32.bxor`, `bit32.lshift(a, 2)`, `bit32.bnot` |
| `goto`, `::label::` | not there; use `continue` or a flag |
| `local x <const> = 5` | `local x = 5` |
| whole numbers and decimals are different types | one number type (a double): whole numbers up to 9,007,199,254,740,992 are exact. `math.type`, `math.tointeger`, `math.maxinteger` are gone (the script still compiles; the name is empty at run time) |
| `math.ult`, `utf8.*`, `table.move` of strings | `utf8` is not offered; `table.move` still is |
| `_ENV` | not there |

Everything that worked in both (loops, tables, `ipairs`, `pairs`, `#`, closures, `string.sub`, `table.insert`, ...) works as before.
A script that only used these runs unchanged. If an old script uses one of the left-hand forms, the mod checker says
"this script does not compile" with the **file and line**, and points here.

## What a script can and cannot touch (the sandbox)

Same rules as before, now enforced by Luau:

- **There:** `assert error getmetatable ipairs next pairs rawequal rawget rawlen rawset select setmetatable tonumber tostring type typeof unpack`,
  `string` (`byte char format len lower rep reverse sub upper`; no patterns), `table` (`clear clone concat find insert move pack remove sort unpack`),
  `math` (`abs ceil clamp floor fmod huge max min modf pi round sign sqrt`; no random numbers, no sin/cos/log: they differ between PCs),
  `bit32`, and the game's own functions (`on`, `log`, `story_function`, the names, `position()` and so on).
- **Not there:** `io`, `os`, `debug`, `coroutine`, `utf8`, `buffer`, `vector`, `require` (there is no way to load another file), `load*`,
  `pcall`/`xpcall` (a script cannot hide its own failure), `collectgarbage`, `getfenv`/`setfenv`, `newproxy`, string patterns, `math.random`.
- The library tables (`string`, `table`, `math`, ...) are read-only: `string.upper = nil` is an error.
- Your own global variables are kept for as long as the game runs (nothing is saved). They are your mod's alone; another mod
  never sees them.
- Each mod has its own memory cap (16 MB) and its own state.

## The work limit (so a bad script cannot freeze the game)

Every call of a hook handler is allowed **200,000 steps**; a script file's first run (its top level) 2,000,000. A *step* is one
function call, one return or one turn of a loop. It is not seconds, so it is the same on every PC (and a recording of a game with a
script stops it in the same place). An ordinary handler uses a few hundred. When a script goes over:

```
[script endless-loop] switched off: in the "battle.damage" handler: endless-loop/scripts/damage.luau:3: ran too long (more than 200000 steps in one call: a step is a function call or a loop turn)
```

The mod's scripts are switched off for the rest of the session, the game goes on with its own result, and the message
(also shown in the Mods page and the log) names the **mod, file and line**. Memory: `not enough memory (a mod's scripts may use 16 MB)`.
Any other error (a typo in a field name, a wrong answer type, a missing hook name) also switches that mod's scripts off with the
mod, file and line.

## One script file for XD and Colosseum

Put what both games can use in `scripts/` at the top of the mod, and what only XD has in `games/xd/scripts/`
(see [two-games.md](two-games.md)). The checker warns when shared scripts call something only XD offers.

## Names for numbers

Unchanged: [lua-names.md](lua-names.md) (the page keeps its old file name for now).

## See also

- [battle-scripts.md](battle-scripts.md): the hooks and examples
- [lua-names.md](lua-names.md): names for numbers
- [sharing.md](sharing.md): the player's permission to run scripts
