# Change the game's data

**What this page covers:** changing the numbers in the game's tables (stats, moves, trainers, Shadow Pokémon, shops, TM moves, item prices)
and the game's message text, and adding new trainers and battles.

A **table** is a list of rows (the game calls them entries). Each entry has **fields**, the numbers that describe it.
A data mod is a `.json` file that says "in this table, in this entry, set this field to this value".

## The shape of a data file

Put one or more `.json` files in your mod's `data` folder. Every file in `data` is read, in name order.

```json
{
  "_note": "Keys that start with an underscore are comments.",
  "moves": {
    "Tackle": { "iryoku": 90, "pp": 25 }
  },
  "pokemon": {
    "Eevee": { "basisStatus[0].maxHp": 80, "basisStatus[0].phyAtk": 70 }
  },
  "shadow_pokemon": {
    "#3": { "snatchRatio": 90 }
  }
}
```

- **Table** (`moves`, `pokemon`, ...): the first level.
- **Entry** (`Tackle`, `Eevee`, `#3`): the second level. Use a name where the table has names, or a number written as
  `"#12"`.
- **Field** (`iryoku`, `pp`, ...): the third level. The field names are the ones the decompilation of the game uses, so
  they mean exactly what the game reads.

Names ignore capital letters, spaces and signs: `"Mr. Mime"` and `"MR MIME"` are the same. If a name is shared by
several entries, the game tells you their numbers and you must use a number instead.

### Values

- A number: `"level": 30`.
- `true` or `false`.
- A hex number or an entry number as text: `"0x1F"`, `"#12"`.
- A **name**, where the field is a list of names (types, species, moves, items, natures): `"kowaza[0]": "Tackle"`.
- A field with several values is a list. Use `null` for the places you do not want to change:
  `"wazaID": [33, null, null, null]`. Or set one place: `"wazaID[2]": 33`.
- Some fields share one byte with others (a trainer's Pokémon: `seikakuID`, `sex`, `tokuseiFlag`). You can set them
  alone; the bits beside them are kept.
- Addresses and values the game program fills in by itself cannot be changed. The game says so.

If you set a field to the number the disc already has, nothing changes. Numbers that cannot exist (a species number
past the end of the table, a value too big for its field) are refused with a message. The rest of your mod still works.

## Finding table, entry and field names

Do not guess and do not trust a copy of a list on a web page. Ask the game's own tables, from your own disc. See
[Tools you can run](#tools-you-can-run) below. The most useful commands:

```
xd_data_mods <disc> --tables                  every table, its entry count and short name
xd_data_mods <disc> --fields <table>          the fields of one table
xd_data_mods <disc> --show <table> <entry>    what one entry holds now
xd_data_mods <disc> --names <list>            names: species, move, item, type, nature, story_trainer, ...
```

Common short table names: `pokemon`, `moves`, `items`, `types`, `natures`, `trainers`, `trainer_pokemon`,
`shadow_pokemon`. The long names (`pokemon_data`, `waza_data`, `story_pokemon`, ...) work too.

Some useful fields: for `pokemon`, the base stats are `basisStatus[0].maxHp`, `.phyAtk`, `.phyDef`, `.speAtk`,
`.speDef`, `.nimbleness` (speed). For `moves`: `iryoku` (power), `pp`. Run `--fields` for the rest.

## Trainers and Shadow Pokémon

The trainer data is kept in "deck" files. Their names start with `story_` (the story), `colosseum_`, `hundred_` (Mt.
Battle), `virtual_` (Battle CDs), `imasugu_`, `bingo_` and `sample_`. Each has `_trainers`, `_pokemon` (the
trainers' Pokémon) and `_ai`.

- A trainer's team is `pokemonDataID`: six entry numbers in the same deck's `_pokemon` table.
- Bit *i* of the trainer's `darkPokemonFlags` (values 1, 2, 4, 8, 16, 32 for slots 0 to 5) makes that slot a Shadow
  Pokémon. The number in that slot is then an entry of `shadow_pokemon`.
- A `shadow_pokemon` entry has `deckPokemonID` (which `_pokemon` entry gives species, level and moves), `snatchRatio`
  (the catch rate), `darkWaza` (Shadow moves), `initDarkPoint` (the heart gauge), and more.

**Changing which species a Shadow Pokémon is.** The game keeps a second list of that, in its program file:
`darkPokemonList` (short name `shadow_species`; the entry is the `shadow_pokemon` number, the field is `pokemonID`).
If you give a Shadow's `_pokemon` entry another species, set the same species in `shadow_species`. If you do not, the
P★DA still shows the disc's species. It only counts for a New Game.

### New trainers and battles
A mod can **add** a trainer, the Pokémon it brings and a battle that uses them. Three "add" blocks in one data file (the same `add` form as
new Pokémon, moves and items: [add-pokemon-moves-items.md](add-pokemon-moves-items.md)). A story script then starts the battle with
`trainer_battle @name` (see [story-scripts.md](story-scripts.md)).

```json
{
  "add": {
    "trainer_pokemon": {
      "rival_eevee": { "copy": 25, "pokemonID": "Eevee", "level": 8, "wazaID": ["Tackle", "Tail Whip", 0, 0] }
    },
    "trainers": {
      "rival": { "copy": 15, "pokemonDataID": ["rival_eevee"] }
    },
    "battles": {
      "rival": { "copy": 222, "zenmetuFlag": 0, "trainerId": [null, "rival"] }
    }
  }
}
```

- `trainer_pokemon` (= `story_pokemon`): a new team member, a copy of a row of the story deck. A new row has a number after the deck's last
  one, so it can be named in the team of a new trainer: `"pokemonDataID": ["rival_eevee"]`. The decks of Colosseum, Mt. Battle and so on have
  their own tables (`colosseum_pokemon`...). At most 256 new Pokémon rows and 64 new trainers per deck.
- `trainers` (= `story_trainers`): copy a story trainer to keep its look, kind, AI and name. **Pick a copy with `darkPokemonFlags` 0**
  (a trainer whose slot is a Shadow Pokémon would make your new Pokémon a Shadow). The trainer's name shown in the fight is the copy's (the
  name is a message number of 16 bits: a new text would not fit).
- `battles` (= `fight_encount_data`, the table a story script's battle number is a row of): copy a story battle that looks like what you want
  (a single trainer against you: `#222`; `xd_data_mods <disc> --show battles <n>` shows a row). `trainerId[1]` is the trainer on the other
  side, `deckGroup[1]` which deck it is in (1 = the story deck: leave it when you copy a story battle). `zenmetuFlag` 1 = "lose and the game
  sends you back to the last Pokémon Center"; 0 = losing is allowed. The battle floor (`fightFloorDataId`), the music (`bgmSndId`) and
  the screen wipe (`wipeId`) stay the copy's. At most 256 new battles.
- All three go into the game's data files for you; the mod holds only these few lines. The game's own rows are unchanged.
- The new rows are part of the playthrough's number record like new Pokémon: CONTINUE with other mods is refused.
- On Pokémon XG, **new battles, story flags and rooms are not available** (see [two-games.md](two-games.md)); new Pokémon, moves and items are. The battles, trainers and Shadow slots of the game are changed with the ordinary data files as before.

## Tables in the game's program file

Two tables live in the game program instead of a disc archive. You change them in the same way:

| Table (short name) | Entry | Field |
|---|---|---|
| `darkPokemonList` (`shadow_species`) | Shadow Pokémon number | `pokemonID` |
| `item_waza_machine_no` (`tm_moves`) | TM number minus 1 (`#0` is TM01) | `wazaID` |

The shop tables (`shopData`, `shopGoods`, `shopSalesman`, `shopExchanger`) are available too. Other menu tables, map
files and other tables of the game program are not offered yet.

## Message text

A data file may have a `text` block that rewrites messages by number:

```json
{ "text": { "#10200": "Sharply raises{00}the ATTACK stat." } }
```

(The number is only an example. Find the real one with `xd_data_mods <disc> --text <from> [to]`, which lists messages
by number, written the way a `text` block writes them.)

- `{00}` is a line break. Other control codes are `{XX}` or `{XX aa bb}` (hex bytes, as `--text` shows them). `{{` is a
  plain `{`.
- You can change the texts in the game's common message tables and in the menus' own tables (item descriptions, menu
  words). You cannot change map or story dialogue this way. For that, see [story-scripts.md](story-scripts.md).
- **A table keeps its size.** All the new texts of one table together can only be as long as the texts they replace
  (2 bytes per character). If they are too long, the game refuses that table's texts and tells you both sizes.
- A mod with a `text` block should say `"applies": "restart"` or `"new_playthrough"`. The port decides if your
  `any_time` claim is wrong and treats the mod as `new_playthrough`.

## When two mods change the same thing

The mod lower in the list wins, and the player gets a message: "Conflict: the mods A and B both change X of Y." This is
normal, not an error. Order the list to choose.

The port's own options (Gen 4 pack, Modern types, the randomizer) are built to stack and never report conflicts with
each other. Their order is fixed:
base disc, then user mods, then the Gen 4 pack, Poké Spot set, Modern types, and last the Randomizer. Later wins.

## Saves and mods

A save belongs to the mods it was made with. Changed numbers in existing rows do not break a save. They only change
how the game plays. New rows (new Pokémon, moves, items) are tied to the save. See
[add-pokemon-moves-items.md](add-pokemon-moves-items.md#saves-and-added-rows).

## Tools you can run

These are small command-line programs built together with the game (in the build's `bin` folder). The download for
players contains the game and the launcher only. You do not need these tools to make a mod, but they find names and
check your work without starting the game.

| Program | What it does |
|---|---|
| `xd_data_mods <disc> --tables` | List the tables |
| `xd_data_mods <disc> --fields <table>` | List a table's fields |
| `xd_data_mods <disc> --show <table> <entry>` | Show one entry |
| `xd_data_mods <disc> --names <list>` | List names (species, move, item, type, nature, story_trainer, ...) |
| `xd_data_mods <disc> --text <from> [to]` | List messages by number |
| `xd_data_mods <disc> --mods [mods folder] [mods.ini]` | Load the mods that are on and print every number they change, and every problem |
| `xd_data_mods <disc> --ids [mods folder] [mods.ini]` | Print the numbers given to rows your mods added (JSON) |
| `xd_data_mods <disc> --check-saves <save.gci>...` | Check whether the game, as the disc has it, accepts a save (no mods) |
| `xd_mods_check <disc> [mods folder] [mods.ini]` | Load mods exactly as the game does, then read every replaced file back and compare. Prints `ok` or `FAIL` for each check. Exit code 0 means all passed. |
| `xd_audio_tool list, info, decode, preview, encode ...` | Your own songs and cries in the game's sound format, and the game's own back to WAV for listening: [music-and-sound.md](music-and-sound.md) |

`<disc>` is the path to your disc image. Without folder arguments the tools use the player's mods folder and
`mods.ini`. They only read. They never change a list or a save.

## Test it

1. `xd_data_mods <disc> --mods` and read the output: every problem is listed, then "Data mods: N number(s) set in M
   table(s)".
2. Start a New Game with the mod on. At start-up the game shows a message window if anything was wrong.
3. Look at the thing you changed in game: the move list, the stats screen, the shop.

## Common mistakes

- Using a name from a different game or from memory. Ask `--names`.
- Making a text longer than the one it replaces. Shorten it.
- Changing a trainer's Pokémon in the wrong deck. Each deck has its own `_pokemon` table.
- Changing a Shadow Pokémon's species but not `shadow_species`.
- Forgetting commas, or leaving one after the last item.
- Expecting a change in an old save. Use a New Game.

## Mods on XG

If the disc you play is Pokémon XG (the fan game made from XD), a mod only loads when its `mod.json` `"game"` list names `"xg"`
(for example `["xd", "xg"]`); a mod made only for XD is shown as *Made for XD, not XG* and is not used. Put XG's own files in `games/xg/`
(see [two-games.md](two-games.md)). For a mod that names XG, the game reads its tables
from your disc. What works: texture packs, data mods that **change** rows (stats, moves, trainers, shops, text),
Luau battle scripts, story-script mods that only use the game's own flags (not yet tested on XG), the Battle Revolution models and the
randomizer, and **new Pokémon, moves and items** (the `add` block: see [two-games.md](two-games.md)). What is **off** on XG: adding
story flags, rooms, battles and trainer decks, a new Pokémon's own model, cry and face, the Gen 4 rules, the Fairy type (the Gen 4 Pokémon have XG's own Fairy type), and language packs.
The Gen 4 Pokémon themselves can be switched on for XG on the New Game screen. A data mod that tries to add those still loads on XG; those added rows are skipped and the log says so.
XD's numbers may not mean the same thing on XG, so test a mod on the disc it is for.

## See also

- [add-pokemon-moves-items.md](add-pokemon-moves-items.md): add rows instead of changing them
- [translations.md](translations.md): message text and languages
- [troubleshooting.md](troubleshooting.md): what the start-up messages mean
