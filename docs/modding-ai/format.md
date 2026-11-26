# Mod format

Folder layout, `mod.json`, text in several languages, the data file format and how to look up names. Back to the [front page](README.md).

## Folder layout

The mods folder (open it from the launcher's Mods page): Windows `%APPDATA%\Pokemon XD\mods`, macOS
`~/Library/Application Support/Pokemon XD/mods`, Linux `~/.local/share/Pokemon XD/mods`. One folder per mod, with
`mod.json` directly inside. Zips are not read.

```
<mod-id>/
  mod.json
  data/*.json            numbers, text, added rows (read in file name order)
  scripts/*.luau          battle scripts (needs "scripts": true)
  files/<disc path>      replaces a whole disc file (advanced)
  fsys/<archive>.fsys/<entry>   replaces one entry in a disc archive (advanced; entry by name or "#N" number)
  textures/              HD texture files (Dolphin names)
  sounds/effects.json    single sound effects of the author's own WAV / OGG (sounds/ also holds the sound files; see [content.md](content.md))
  sounds/songs.json      NEW songs (same)
  objects/<room>.json    people, zones, chests, music, doors of rooms (XD only, see story-objects.md)
  games/<ID start>/...   parts made for ONE game (the same folders as above); see "One mod, more than one game"
```

### One mod, more than one game

Human page: [two-games.md](../modding/two-games.md).

Colosseum comes after XD 1.0, but the format is ready now. Shared parts (`data/`, `scripts/`, `textures/`, ...) sit at the top; parts for one game
sit in `games/<name>/`: `games/xd/` = every XD disc (NOT the XG romhack), `games/xg/` = the XG romhack (same disc ID, edition xg), `games/colosseum/` = every Colosseum disc; `mod.json` `"game": ["xd"]`
or `["xd", "colosseum"]`. The older finer names still work (`games/GXX/` = the same as xd, `games/GXXE/` = one region, `games/GXXE01/` = exactly that disc, `games/GC6/` = colosseum).
A mod in the older flat layout (no `games/`) loads as before, for XD only (not XG unless it names `"xg"`); the checker gives a note (`flat-layout`). Draft: the shared mod format can still change. Rules (use them, do not invent others):

- The game reads the shared top folder, then `games/<3>`, `games/<4>`, `games/<6>` that apply to the running disc, **least specific first**.
- Same path (letter case ignored) in a later folder **replaces** the earlier file whole; different names add up. Shared files come first in any
  order-dependent place (data files applied in order, scripts run in order), then the game's, each by name.
- A mod without a `games/` folder is the **old layout** (everything at the top, for every game in `"game"`): unchanged, still valid.
- `"game"` entries may be the start of an ID (`"GXX"`, `"GXXE"`) as well as a whole ID. The mod is used only for a disc that one of them starts.
- Put anything XD-only (see the audit below) under `games/xd/`. Never `if`-test the game inside Luau: there is no such function.
- `xd_mod_check` prints `works with: XD; Colosseum: shared parts only`, warns about `games/` folders no `"game"` entry matches, wrong folder
  names, a game with nothing for it, and XD-only Luau in the shared part of a mod that names another game (`lua-game-only`).
- Code that reads a mod folder uses `engine/mods/include/mod_layout.h` (`ContentRoots`, `LayoutFiles`, `ResolveLayoutFile`), never `<mod>/data` by hand.

Texture packs may also sit in `<data folder>\textures\GXXE01\<pack>\` and appear as mods that start on.

## mod.json

```json
{
  "id": "my-mod",
  "name": "My mod",
  "version": "1.0",
  "author": "Name",
  "description": "One sentence.",
  "game": "GXXE01",
  "applies": "new_playthrough",
  "scripts": true,
  "script_api": 1,
  "requires": ["other-mod >= 1.0"],
  "conflicts": ["other-mod"],
  "after": ["other-mod"],
  "before": ["other-mod"],
  "works_with": ["other-mod"],
  "recommends": ["other-mod"],
  "only_with": ["other-mod"],
  "links": {"other-mod": "https://example.com/other-mod.update.json"},
  "priority": 0,
  "updates": "https://example.com/my-mod/my-mod.update.json",
  "author_key": "<64 hex digits: your public signing key>"
}
```

| Field | Needed | Meaning |
|---|---|---|
| `id` | yes | Letters, digits, `-`, `_`, `.` |
| `game` | yes | `"GXXE01"` or a list. A mod for another game ID is not used |
| `name`, `version`, `author`, `description` | no | Shown in the mod list. Versions compare number by number (1.10 > 1.9) |
| `applies` | no | `any_time` (looks and sound only), `restart` (read at start, not kept in saves, e.g. text), `new_playthrough` (default; anything that changes rules, data or what is saved). The port raises a wrong `any_time` to `new_playthrough` if the mod has `data/` or replaces non-picture/sound/font/movie files |
| `scripts` | for Lua | `true` to run `scripts/*.luau` |
| `script_api` | no | `1` is the only value |
| `requires` | no | Mod ids that must be on, with optional condition: `"x >= 1.0"` (also `>`, `<=`, `<`, `=`) |
| `conflicts` | no | Ids it cannot be used with (the one lower in the list is dropped) |
| `after` | no | Ids this mod must come after in the list |
| `before` | no | The twin of `after`: this mod comes before those ids |
| `works_with`, `recommends` | no | Ids that go well with it. Suggestions only, never forced |
| `only_with` | no | A compatibility patch: used only when ALL of these ids are on (and it goes after them) |
| `links` | no | `{"id": "https://.../id.update.json"}`: where a missing mod named in `requires` / `recommends` can be downloaded (the player is asked) |
| `author_key` | no | The author's public Ed25519 key (64 lower-case hex). Launchers pin it; an update signed with another key is accepted only when the old key announced it (key change) or the player confirms the new fingerprint |
| `label_namespace` | no (set it if the mod adds rows) | DRAFT ([labels.md](../modding/labels.md)): `<author>.<pack>`, lower case letters / digits / `-` / `_`, one dot. The namespace of the stable labels of the rows the mod adds. Never `base`. Without it: `local.<mod id>` |
| `priority` | no | Position of a newly found mod in the list, and the tie-break of the automatic working order (lower first) |
| `updates` | no | An **https** address of a small JSON file the author hosts (written by `xd_mod_pack`, see [tools-and-saves.md](tools-and-saves.md)). Needs `version`. Only the launcher uses it, and only after the player says yes; the game never does |

Files for `mods.ini` (in the data folder): `profile = default`, then `[profile default]` with `mod-id = on|off`. Lower
lines win conflicts. New mods are added as `off`. A player normally uses the launcher Mods page or the in-game MODS page
instead of editing it.

## Words in several languages (`text` of an added row)

In an `"add"` row, `"text"` may hold `"<field>.<lang>"` keys next to the plain ones: `"name.sp"`, `"doc.fr"`, `"typeName.ge"`. Language
codes are the game's `system.language` codes without `us`: `sp`, `fr`, `ge`, `it`, `jp`. The game uses the key of the language it runs in and the plain
key otherwise (a language key for a field the row lacks is ignored). Style: capitals for sp/fr/ge/it names, no accents on French capitals, ß as SS,
Japanese as written (kana). Keep move names under ~150 px in the 18 px font (about 14 characters). The Gen 4 pack uses this for its 113 moves, 13 items,
41 TMs and 105 species (source `tools/pbr_convert/gen4_text_i18n.tsv`, machine-made descriptions); its abilities and battle messages are tables in
`games/xd/src/gen4_text_i18n.inc`.
Box sizes (native 640 x 480 pixels, the dialogue font; measured on screen 2026-11-18): a bag list row (item name, or "No88 " + a machine's move
name) about 240 px; a bag description line about 225 px (3 lines); a summary move description line about 325 px (2 lines); a summary ability
name about 180 px and its description about 350 px (one line, XD's own longest is 322). Letters are counted when the Gen 4 pack builds the
text (the builder has no fonts), so a language with wide letters gets short wording; `python tools/pbr_convert/gen4_text_i18n_test.py --fonts <a
language pack folder of the data folder> [--jp <Japanese pack>] [--pack <Gen 4 pack>]` measures every text with the game's real fonts and fails when one
is too wide. XD shows no Pokedex entry text for any species (the Strategy Memo shows the category line, height, weight, type, ability), so a
species row has no description to translate.

## Data file format (`data/*.json`)

One JSON object. Keys starting with `_` are comments.

```json
{
  "moves":   { "Tackle": { "iryoku": 90, "pp": 25 } },
  "pokemon": { "Eevee":  { "basisStatus[0].maxHp": 80 } },
  "trainer_pokemon": { "#25": { "level": 30, "wazaID": ["Tackle", null, null, null] } },
  "text":    { "#10200": "New words{00}second line" },
  "add":     { "moves": { "New Move": { "copy": "Tackle", "iryoku": 120, "text": { "name": "New Move" } } } }
}
```

- Level 1 is a table (`pokemon`, `moves`, `items`, `types`, `natures`, `trainers`, `trainer_pokemon`, `shadow_pokemon`,
  or any long table name such as `pokemon_data`, `waza_data`, `story_pokemon`, deck tables like `story_trainers`).
- Level 2 is an entry: a name (case, spaces and signs ignored) where the table has names, or `"#N"` by number.
- Level 3 is a field, using decompilation names (`iryoku` = power, `pp`, `basisStatus[0].maxHp`, `.phyAtk`, `.phyDef`,
  `.speAtk`, `.speDef`, `.nimbleness`).
- Values: number, `true`/`false`, `"0x1F"`, `"#12"`, or a name where the field takes a name (types, species, moves,
  items, natures). Multi-value fields: a list with `null` for "leave", or `"field[2]": value`.
- Bit-packed fields (a trainer Pokémon's `seikakuID`, `sex`, `tokuseiFlag`) can be set alone.
- Addresses and engine-filled values cannot be changed.
- Trainers: deck tables (`story_*`, `colosseum_*`, `hundred_*`, `virtual_*`, `imasugu_*`, `bingo_*`, `sample_*`, each with
  `_trainers`, `_pokemon`, `_ai`). Team = `pokemonDataID` (six entries). Bit i of `darkPokemonFlags` makes slot i a
  Shadow Pokémon (entry of `shadow_pokemon`; its `deckPokemonID` points to the `_pokemon` entry).
- Changing a Shadow's species also needs `shadow_species` (`darkPokemonList`, field `pokemonID`, entry = shadow number).
- Program tables: `shadow_species` and `tm_moves` (`item_waza_machine_no`, entry = TM number minus 1, field `wazaID`).
  Shop tables: `shopData`, `shopGoods`, `shopSalesman`, `shopExchanger`.
- `text`: messages by number (`"#N"`). `{00}` line break, `{XX}` or `{XX aa bb}` control codes, `{{` a brace. The sum of
  the new texts of one table cannot exceed the sum of the old ones (2 bytes per character). Common message tables and
  menu tables only; no map or story dialogue.
- `add`: tables `pokemon`, `moves`, `items` (max 4096 each), `flags`, `floors` (rooms), `battles`, `trainers`, `trainer_pokemon` (and the other
  decks' `_trainers` / `_pokemon`), see [story-scripts.md](../modding/story-scripts.md) and [change-data.md](../modding/change-data.md) for the last ones. Each new row: a unique new name as key, `"copy"`
  (existing row or an earlier new row), fields to change, and `"text"` (`name`, `doc`; species also `typeName`). Species
  numbers start at 415, moves at 375, items after the last item. Write names, never numbers, for new rows
  (`"mod-id:Name"` to be exact). New rows share their copy's model, cry, learn-set, animation and Pokédex seen/caught
  flags unless the row says otherwise (keys below), and its FACE unless the species row has `"face"`: `"face": "Umbreon"` (a species name or `"#197"`: that species' face) or
  `"face": "textures/faces/x.png"` (the mod's own PNG, 8-bit RGB/RGBA, not interlaced; any size, shrunk to 42x84 and 256 colours; twice as high
  as wide = normal over shiny, any other shape = one face for both; path inside the mod with `/`, may sit under `games/<id>/`). The picture becomes a new
  texture entry of `poke_face.fsys` and two new rows of the `poke_face` table (PC-only longer copy, like the other grown tables); the species row's
  `cgData.spriteId[0..1]` point at them. A missing or bad file leaves the copy's face and says why at start. XD only.
  **Own box / party icon** (`"icon": "textures/icons/x.png"`, species row, XD only; xd_faces.cpp `BuildBoxIcons`): the small wiggling picture of the PC box grid and the Party list (`cgData.pokebodyId[0..1]` = `poke_dance.fsys` id << 16 | 0x3200, ant.cpp). PNG 8-bit RGB/RGBA not interlaced; 54 x 108 = normal over shiny (the game's size), any 1:2 picture is read that way, any other shape = one for both; shrunk to fit 54 x 54 keeping the shape (never enlarged), standing on the bottom edge, put into the 3 frames of a copy of the ant `unknown_a` (64 x 192 C8, 256 colours) and added to `poke_dance.fsys` as two new entries per picture. Missing or bad file: the copy's icon stays, said at start. (0.7.5) `"face"` and `"icon"` are also read in an EDIT of a row of the disc (`"pokemon": { "#414": { "icon": ... } }`; `DataTable::editAssetKeys`, `DataApplyResult::editedAssets`): xd_faces.cpp `BuildPictures` looks at those rows as added ones (`AddedRow::existing`), runs `BuildFaces` / `BuildBoxIcons`, and hands the new row bytes back in `RowsJob::editedRows`, which mod_load.cpp writes into the table's entries. A row without the key is never touched; off in XG.
  **Own model, cry, Pokédex marks, move animation** (XD only, `games/xd/src/data/xd_species.cpp`; all optional keys of the species row, a bad one is
  reported at start and leaves what the row copies):
  `"model": "models/x.pkx"` (a model file of the mod: the game's pkx format, header type 0xC, at most 614,400 bytes; or a species name / `"#n"` = that species' model) -> the port
  makes a one-member archive `pkx_mNNNN.fsys` (group number above every group in GSfsys.toc, at least 5000), a toc row and a row of `pokemon_pkx_data`
  `{group, 0x00011E00}`, and writes the new model number into `pkxDataId`. `"cry": "sounds/x.wav"` (8/16/24/32-bit PCM or float WAV, any rate and channels; or a species
  name) -> encoded to the game's DSP-ADPCM cry (32 kHz, 2 channels, max 3.5 s, longer is cut), two more members of `stm_se_nakigoe_archive.fsys`, one row each of `snd_file`,
  `snd_data` and the PDA waveform table `pokemon_nakigoe_data`; sets `voice`, `shout`, `nakigoeId`. `"dex": "own"` (default `"copy"`; not default because it adds rows and a
  playthrough is bound to its rows; NOTE the game's own marking (`pokemonSetOnZukanFlag`) writes the flag of species 0 whatever the species, so these flags are only for scripts: the Strategy Memo, keyed by species number, already treats a new species as its own entry) -> two new 1-bit level-3 rows at the end of `flag_data`, written into `mitaFlag` (seen) and `tukamaetaFlag` (caught); room for ~9,368 bits
  (all 4,096 species fit). A **move** row may have `"animation": "<move>"` = copies that move's `wazawzxdataId`; or `"animation_attack"` / `"animation_damage"` / `"animation_status"` = a NEW row of `waza_wzx_data` (spare rows from 373) whose three resource ids are taken one each from the named moves' rows (`"none"` = no piece; `"animation"` names the move for the parts not named; filled at start-up by `species_tables.cpp`) (which animation it plays; the insides of a piece cannot be changed). Same file given twice is stored once. No game data goes into the mod; the files are made when the game starts. A species has its own Pokédex entry through `"height"` (decimetres), `"weight"` (hectograms) and
  `"text": {"typeName": "...", "doc": "..."}` (the category line; `doc` is kept in the row but XD's PDA shows no
  description). A move has `"category": "physical"|"special"` (used only when the physical/special split setting is on;
  without it the move follows its type). Every move a mod adds can be picked by Metronome unless it has `"metronome": false` (needs the game build with the decomp's never-pick list; older builds ignore it) (the first one added by a mod
  and everything after it in the table; the Gen 4 pack's moves are skipped when they come first). The `"add"` file must be
  read before files that use the new names (name it so it sorts first).
  Why 4096 (1024 before 0.7.1): the decomp's PC-only spare rows (`include/game/pcspare.h`, `PC_SPARE_SPECIES/MOVES/ITEMS` = 4096) in the
  compiled tables the port cannot grow (model, cry, move-animation, item-parameter tables). The Gen 4 pack and all
  mods share the 4096 (models and cries too: the pack's rows come first). Rooms stay at 32 (see story-scripts.md).
- Any field naming a species, move or item is checked against the table's end. A non-existent number is refused.

Problems (unknown table, entry, field or name, a value that does not fit, a broken file) are reported at start-up and
skipped. The rest applies. Setting a field to the disc's own value changes nothing.

## Finding names (do not use a stale copy)

These are command-line programs built with the port (not in the player download). `<disc>` is the player's disc image:

```
xd_data_mods <disc> --tables
xd_data_mods <disc> --fields <table>
xd_data_mods <disc> --show <table> <entry>
xd_data_mods <disc> --names <list>        species | move | item | type | nature | story_trainer | ...
xd_data_mods <disc> --text <from> [to]
xd_data_mods <disc> --mods [mods folder] [mods.ini]    check the mods that are on
xd_data_mods <disc> --ids  [mods folder] [mods.ini]    numbers (and labels) given to added rows (JSON)
xd_data_mods <disc> --labels species|move|item         the official rows' labels as the player's disc gives them (DRAFT, labels.md)
xd_data_mods <disc> --check-saves <save.gci>...
xd_mods_check <disc> [mods folder] [mods.ini]          load as the game does; exit code 0 = all ok
xd_randomize <disc> --switches                         the randomizer's switches (a generator, not a modding tool)
```

If the tools are not available, tell the player that you cannot verify names and which ones you used.
