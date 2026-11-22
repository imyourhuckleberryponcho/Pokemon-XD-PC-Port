# Names instead of numbers in Luau
> Early version: the format can still change.

**What this page covers:** writing `TYPE.GRASS` or `SPECIES.EEVEE` in a script instead of `12` or `133`.

Every Luau script (battle scripts and story functions) can use names for the game's types, Pokémon, moves, items,
abilities, natures and story flags. The names are read from **your own disc** when the game starts. Nothing from the game is
stored in the program or in the mod. If a mod adds a new Pokémon, move or item, its name is there too.

## The tables

| Table | What | Example |
|---|---|---|
| `TYPE` | types | `TYPE.GRASS`, `TYPE.WATER`, `TYPE.FAIRY` (only with "Modern types") |
| `SPECIES` | Pokémon | `SPECIES.EEVEE`, `SPECIES.MR_MIME` |
| `MOVE` | moves | `MOVE.TACKLE` |
| `ITEM` | items | `ITEM.POTION` |
| `ABILITY` | abilities | `ABILITY.STATIC` |
| `NATURE` | natures | `NATURE.ADAMANT` |
| `FLAG` | the game's story flags that have a name (about 1000 sure ones and about 610 auto-made guesses: see "Story flags" below) | `FLAG.STORY_PROGRESS`, `FLAG.SEEN_EEVEE`, `FLAG.CAUGHT_EEVEE` |
| `BATTLE` | the battles your own mods add (the game's battles have no names) | `BATTLE.SIDE_RIVAL` |
| `STORY` | the steps of the story: the values the story flag takes | `STORY.BEAT_CHOBIN` (= 40) |

Each entry is a whole number, so `e.move.type == TYPE.WATER` and `flag(FLAG.STORY_PROGRESS)` work as they are.

```lua
on("battle.damage", function(e)
  if e.move.type == TYPE.GRASS and e.defender.species == SPECIES.EEVEE then
    return e.damage * 2
  end
end)
```

## How a name is made from the game's text

The name is the game's own word, in capitals, with every other sign turned into `_`:

- `Mr. Mime` is `SPECIES.MR_MIME`. `Poké Ball` is `ITEM.POKE_BALL`. The female and male signs become `_F` and `_M`.
- A name that starts with a digit gets `_` in front (`_1ST`).
- **The game's own words are short in places.** The disc calls Electric `ELECTR` and Psychic `PSYCHC`, so the names are
  `TYPE.ELECTR` and `TYPE.PSYCHC`. Use `name_of` (below) or the list tool to see exactly what exists.
- Two entries with the same name: the first keeps the plain name, later ones end in `_2`, `_3`. (The log says so.)
- Something with no letters or digits in its name (the game's "?" type) has no name. Use its number.
- A Pokémon, move or item **your mod adds** is named by the name you gave it in the data file (`"Zorblat"` is `SPECIES.ZORBLAT`).

To see the list for your disc, start the game once with your script mod on and look in the log: it prints
`[script] names for numbers: type 17, species 4xx, ...`. For the full lists use `xd_data_mods <disc> --names species`
(and `move`, `item`, `type`, `nature`, `ability`).

## Looking things up

- `find("species", "Mr. Mime")` gives the number, or `nil` when there is none. Capital letters, spaces and signs do not
  count: `"mr mime"` works too. Use it when a name may not exist (for example `find("type", "Fairy")`).
- `name_of("species", 133)` gives the table name (`"EEVEE"`), or `nil` when the number has no name.

The first word is the table's name in small letters: `type`, `species`, `move`, `item`, `ability`, `nature`, `flag`.
A wrong first word is an error that lists the right ones.

## Rules

- The tables are **read-only**. `TYPE.GRASS = 5` is an error. (`rawset` cannot get round it.)
- **A name that does not exist is an error**, not `nil`: `TYPE.GRASSS` stops with "TYPE has no name GRASSS". The mod's scripts
  are then switched off for the session, as for every script error (see [battle-scripts.md](battle-scripts.md)).
  This catches typing mistakes at once. `find` returns `nil` instead.
- The numbers are the same on every PC and in every replay of a game with the same mods.
- `pairs(TYPE)` lists the names (in no promised order; do not depend on it).

## Story flags

The game has 2324 story flags. About 1600 have names now (a thousand with a sure meaning, about 610 auto-made guesses), and more come as their meaning is found. A flag with no
name is still reached by its number.

- Names from your **disc's own text**, built when the game starts (nothing of it is in the program): `FLAG.SEEN_<species>` and
  `FLAG.CAUGHT_<species>` (the Strategy Memo), `FLAG.CHEST_F<floor>_<n>_OPENED` (a treasure chest's flag; floor in hex),
  `FLAG.DOOR_F<floor>_ROW<n>_OPEN` / `_CLOSE`.
- Names from the game's **code**, listed in `games/xd/data/story_flag_names.txt` (one line per flag, `number NAME what it means`):
  `FLAG.STORY_PROGRESS`, `FLAG.PREMIUM_POINTS` (Poké Coupons) and a few battle flags.
- **Names made from your disc's room scripts: about 610 more.** The game's room scripts switch flags on and off all over the story.
  At start (only when a script mod is on) the game reads every room script of **your disc** and names each flag that nothing else names:
  `BEAT_<room>_<function>_<number>` (set by a function that starts a battle), `GOT_...` (set where a gift is given), `STATE_...` (a number
  that takes several values, not a yes/no switch), `CHECK_...` (a flag the scripts only ever look at) or just `<room>_<function>_<number>`. The number is at the end,
  so two flags never share a name. **These names are guesses, made by a program.** Every one comes with a "check" meaning that says where it is set and
  where it is looked at, and (when the same script function moves the story forward) which story step it belongs to. Read the meaning before you rely on a name:
  `xd_data_mods <disc> --names flag_auto` lists them all, with the meaning after each. Nothing of this is stored in the program or in the mod; it is made again
  from your disc each time (about a second). A flag that already has a better name (the Strategy Memo, chests, doors, the code's names) keeps that name only.
  In a story script write `flag game.NAME` after making the names file once: `python -I tools/story/xdscript.py flagnames <disc> --out names.txt
  --tool <xd_data_mods.exe>`, then `--flag-names names.txt` (or `set XD_FLAG_NAMES=names.txt`) on the tool's commands. The decompiler then shows `= game.NAME` next to
  a flag number it knows. (Which names and meanings come out is the same in the game and in the tool; `python -I tools/story/test_flagnames.py <disc> <xd_data_mods.exe>` compares them.)
- **`STORY_PROGRESS` is a number, not a switch.** Its value is the step of the story. The `STORY` table names the steps,
  from the game's own debug text: `if flag(FLAG.STORY_PROGRESS) >= STORY.BEAT_CHOBIN then ... end`.

In Lua: `flag(FLAG.STORY_PROGRESS)` or `flag("STORY_PROGRESS")`. In a story script (`.xds`): `flag game.STORY_PROGRESS` (only the
names from the code file; the mod's own flags are still `flag @name`). **Scripts can read the game's flags; they can only
change their own mod's flags**, whatever the names.

## Which names are for every game

`TYPE`, `SPECIES`, `MOVE`, `ITEM`, `ABILITY`, `NATURE`, `find` and `name_of` are game-neutral (they are filled from the disc that runs). `FLAG` and
`STORY` are XD's story flags and steps: **XD only**. See [two-games.md](two-games.md).

## For other games

The tables come from the game, through one shared mechanism: a game hands over lists of names and numbers and the scripts see
`TYPE`, `SPECIES` and so on. A second game (Colosseum) will fill the same tables from its own disc.

## See also

- [battle-scripts.md](battle-scripts.md)
- [story-scripts.md](story-scripts.md)
- [luau.md](luau.md)
