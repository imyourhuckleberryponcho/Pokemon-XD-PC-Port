# Stable labels for Pokémon, moves and items

> Early version: the format can still change.

**What this page covers:** stable labels, the permanent names that let a save find a mod's Pokémon, moves and items again however the
mods are numbered, and what the game does when a label is missing.

## The problem

The game identifies a Pokémon, a move and an item by a **number**. A mod that adds rows gets its numbers handed out when the
game starts: after the end of the table, in the order the mods load. So the same mod can be number 415 in one setup and 416 in
another (a second mod was switched on before it). The save stores numbers, so a save made in one setup would show the wrong
Pokémon in the other.

A **label** is a permanent name for a row. Numbers are still handed out at load, but the save records **both** the label and
the number, and a label never changes. "The same Turtwig" is the same Turtwig in every setup.

## What a label looks like

```
species:base/turtwig
move:base/wake-up-slap
item:base/ice-stone
species:ada.zorblat/zorblat
move:ada.zorblat/zap-test
```

`<kind>:<namespace>/<name>`

| Part | Rules |
|---|---|
| kind | lower case letters, 1 to 16. Today: `species`, `move`, `item`. (Other kinds may come; the number of a kind is separate from the others.) |
| namespace | `base` for official content, or `<author>.<pack>` for fan content. Lower case letters, digits, `-` and `_`; exactly one `.` between author and pack (a single word such as `ada` is allowed). At most 40 characters. |
| name | lower case letters, digits, `-` and `_`; starts with a letter or a digit; at most 48 characters. No dots, no spaces. |
| whole label | at most 96 characters. Always lower case. Compared exactly, never "close enough". |

A label that breaks a rule is refused with a message that names the rule. Nothing is guessed or repaired.

## `base/`: the official rows

`base/` is for rows the game itself has, and for the **Gen 4 pack** (the Pokémon and moves made from the player's own Battle
Revolution disc), which count as official: `species:base/turtwig`, `move:base/roost`, `item:base/ice-stone`.

**No game text is stored in the repository.** The name part of a `base/` label is made on the player's computer from the names
on the player's own disc, by one fixed rule (the *slug* rule below). The port keeps no table of names. What is fixed and shared is
the rule, and that every kind's `base/` names come from the game's English names.

### The slug rule (name to name part)

1. Letters and digits become lower case. Accented letters lose the accent (`é` is `e`).
2. The gender signs become `-f` and `-m` (`Nidoran♀` is `nidoran-f`).
3. Apostrophes and dots are dropped (`Farfetch'd` is `farfetchd`, `Mr. Mime` is `mr-mime`).
4. Every other run of characters (spaces, `-`, `!`, `?` ...) becomes one `-`. No `-` at either end.
5. Longer than 48 characters: cut.
6. Nothing usable left (a placeholder such as `?????`): the row has **no label**. It is found only by its number.
7. Two rows that end up with the same name part: both get `-<number>` added, so each is still found
   (`twin-3`, `twin-4`).

Examples: `Wake-Up Slap` is `wake-up-slap`; `Porygon2` is `porygon2`; `Poké Ball` is `poke-ball`; `Mime Jr.` is `mime-jr`.

`xd_data_mods <disc> --labels species|move|item` prints the labels the player's disc gives, one per line
(`species:base/turtwig 415`). It reads the disc and writes nothing.

**Open point in the shared mod format.** The rule needs English names. A disc with no English text (the Japanese one) gives names the rule
cannot read; its rows then have no `base/` label and are found by number only. The shared format may replace this with a fixed
number-to-slug table (shared between games, made once from a disc by a person and checked in) replaces this. Until then
nothing in a save depends on a `base/` label of a row the game already numbers itself: those numbers are fixed by the disc.

## Labels for fan content

A mod adds rows with `"add"` (see [add-pokemon-moves-items.md](add-pokemon-moves-items.md)). Each new species, move or item gets
a label:

- The **namespace** comes from `mod.json`: `"label_namespace": "ada.zorblat"` (author and pack). Choose it once and never change
  it: changing it makes every row a new row. The namespace `base` is not allowed for a mod. Without `label_namespace` the port
  uses `local.<mod id>`: it works, but it names the row after the mod's folder, so two people who rename the folder no longer
  agree. Set it before you share a mod. `xd_mod_check` complains about a namespace that breaks the rules.
- The **name** comes from the row's name in the `add` block (`"Zap Test"` becomes `zap-test`), or from the row's own `"label"`:

  ```json
  "Zap Test": { "copy": "Tackle", "label": "zap-test", "iryoku": 120 }
  ```

  `"label"` is the name part only (`"zap-test"`) or the whole label when its namespace is the mod's
  (`"move:ada.zorblat/zap-test"`). Give it when the display name might change (a rename of the text must not rename the row).
- Two rows with one label: the second has **no label** and a message says so. The row keeps its number.
- A row whose name makes no usable name part (`"???"`) needs a `"label"`; without one it has no label and a message says so.

### Using a label inside a mod

Wherever a mod writes a species, move or item, it may write a label instead of a name or a number:

```json
"copy": "species:base/eevee",
"waza.dataId": ["move:ada.zorblat/zap-test"]
```

- The kind must be the one the place wants (a move label where a species goes is refused).
- `base/` labels find official rows by the rule above. Other labels find rows that mods add (the mod must be on, and its rows
  must be declared; a label may be used before its row's file, like a name).
- A label never matches "almost": `species:base/Eevee` is refused (upper case).

Names and `#numbers` keep working as before. A label is the form that does not change when mods are added or reordered.

## What the save records

Next to every save the port keeps a small setup file (`playthroughs/Card A/pt_...json`). It lists every row a mod added that
the save may hold, with both the label and the number:

```json
"added_rows": [
  {"mod": "zorb", "table": "pokemon_data", "name": "Zorblat", "id": 415, "label": "species:ada.zorblat/zorblat"},
  {"mod": "zorb", "table": "waza_data",    "name": "Zap Test", "id": 375, "label": "move:ada.zorblat/zap-test"}
]
```

It is rewritten at every save (so it follows the numbers as they are now). The save file itself stays the console's own format
and is not changed by this.

The same list is written by the port as `added_ids.json` next to the settings (`xd_data_mods <disc> --ids` prints it): the
mod → number map, now with `label`.

## Continue: remap or refuse

When the player continues a save, the port compares the recorded rows with the rows the loaded mods give **by label**:

| Case | What happens |
|---|---|
| Every label is there with the same number | Nothing to do. |
| A label is there with **another number** (another mod was switched on before it, another order) | The numbers in the save are **changed to the new numbers**, then the game reads the save. A copy of the old file goes to the save backups first (the launcher's Save page restores it). The setup file is rewritten. |
| A label is **not there** (the mod is off, or no longer adds that row) | CONTINUE is **refused** and the message names the row and the mod. Nothing on the card is read or changed. The player can quit and switch the mod back on, or play without the memory card. |
| Two recorded rows with one number | Refused (never guess). |
| A row of the other kind with the same name | Another row: refused if it is the one the save needs. |

What is changed in the save: every Pokémon's species, held item and four moves (party, PC boxes, day care, Purification
Chamber), the Shadow Pokémon list's species, and the item numbers in the bag and the PC. All numbers are changed at once, so two
rows swapping places works. Both save slots are changed, each with its own encryption key; the save counter and everything else
stay as they are. What is **not** changed: nothing else in the save is known to hold these numbers; if the game ever stores one
elsewhere the list above gets it first.

The save guard that stops a save holding numbers the game has no rows for stays as it was: it is the last check, after this one.

### Saves made before labels existed

A setup file written before this change has `mod`, `table`, `name` and `id` but no `label`. It keeps loading:

- If the mods give the rows the same numbers, it loads as it always did.
- If the numbers moved and the mods have labels, the port matches each recorded row to the row of the same **mod, table and name**
  and remaps through that row's label. (Same rule: a missing row refuses.)
- If the numbers moved and the mods have no labels at all, it is refused as before.

The first save after Continue rewrites the file with labels.

## Mod updates

When a mod update **renames or reorders** rows that saves hold, the launcher used to warn that saves would break. With a stable
`"label"` on each row (kept when you rename the display name), reordering is safe: Continue remaps by label. A removed row, or a
row whose label changed, still breaks saves that hold it, and the launcher still says so.

## Writing a tool that reads this

A tool needs only: the grammar above, the slug rule, and the `added_rows` list of the setup file. It must treat a label it does not
know as "not there" (refuse), never as a different row.

## Open points in the shared mod format

These points are not settled yet in the shared mod format (also used for Colosseum). Do not rely on them yet.

1. Whether `base/` names come from the player's disc (this draft) or from one shared number-to-name table.
2. The kinds beyond `species`, `move`, `item` (abilities? trainers?).
3. Whether a namespace of one word is allowed (this draft: yes).
4. How a mod says "my labels are the same row as that other pack's" (an alias list), for a pack that is renamed.
5. Whether the save file itself should carry the record (this draft keeps it in the port's setup file next to the save, so the
   save stays what the console wrote).

## See also

- [add-pokemon-moves-items.md](add-pokemon-moves-items.md): adding rows
- [sharing.md](sharing.md): updates and what a save depends on
- [two-games.md](two-games.md): the shared mod format
