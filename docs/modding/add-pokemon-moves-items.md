# Add new Pokémon, moves and items

**What this page covers:** adding new Pokémon, moves and items to the game, and giving a new Pokémon its own face, model, cry and Pokédex
entry. Each new row is a **copy** of one the game already has, with the fields you change. This page also covers how new rows are numbered and saved,
how many can be added, and how to take a Pokémon out of a mod.

A complete working example is in the repository: `tools/examples/data_mods/add-zorblat`.

## How it works

A data file may have an `add` block. You name each new row yourself. The port gives it a number after the end of the
table and tells the game. You never need to know the number: write the **name** wherever a species, move or item is
wanted.

Tables that can grow: `pokemon` (species), `moves` and `items`. You can add at most 4096 new rows to each (all mods
together, the Gen 4 pack included). Other tables cannot grow (the message says so). More Shadow Pokémon (past the free rows),
more HMs, more tutors and more bag slots cannot be added yet: they would not fit the GameCube save, and the game's code would have to
learn to look in the save extension file for them. See "Bigger lists" below for what each one needs.

**Why 4096?** The save is not the limit: it stores these numbers in 16 bits. The limit is in the game's own program. The
decompilation gave the PC version 4096 spare rows in each table that the port cannot make longer (the models and cries
by species, the move animations, the item parameters). Going past them would read memory that is not a table. Also, the game's Pokémon memo (the PDA
list) keeps 500 different species in the save. More than that are kept in a file next to the save, the **save extension**
([save-extension.md](save-extension.md)); the memo's screens show all of them, so Gen 5-7 (800 and more species) fit.
The number of new **rooms** stays at 32; see [story-scripts.md](story-scripts.md).

## Steps

1. Make the mod folder `add-zorblat-mine`, with `mod.json`:

   ```json
   {
     "id": "add-zorblat-mine",
     "name": "Add Zorblat",
     "version": "1.0",
     "author": "Your name",
     "description": "One new move and one new Pokémon that knows it.",
     "game": "GXXE01",
     "applies": "new_playthrough"
   }
   ```

2. Make `data/add.json`:

   ```json
   {
     "add": {
       "moves": {
         "Zap Test": {
           "copy": "Tackle",
           "iryoku": 120,
           "pp": 20,
           "text": { "name": "Zap Test" }
         }
       },
       "pokemon": {
         "Zorblat": {
           "copy": "Eevee",
           "basisStatus[0].maxHp": 150,
           "basisStatus[0].phyAtk": 100,
           "basisStatus[0].phyDef": 90,
           "basisStatus[0].speAtk": 60,
           "basisStatus[0].speDef": 70,
           "basisStatus[0].nimbleness": 80,
           "waza.dataId": ["Zap Test"],
           "text": { "name": "Zorblat" }
         }
       },
       "items": {
         "Test Berry": {
           "copy": "Oran Berry",
           "price": 300,
           "text": { "name": "Test Berry", "doc": "A test item." }
         }
       }
     }
   }
   ```

   - `copy` is the existing row the new one starts as (it can also be an earlier new row of yours).
   - The other keys are the fields to change. List them with `xd_data_mods <disc> --fields pokemon`.
   - `text` gives the words: for a species `name`, `typeName`, `doc`; for moves and items `name`, `doc`. A row with no
     `name` text gets its own name as its name.
   - **Words for other languages**: add the language to the field name: `"name.sp"`, `"doc.fr"`, `"typeName.ge"`. The codes are
     the game's: `sp` Spanish, `fr` French, `ge` German, `it` Italian, `jp` Japanese. A player who runs the game in that language
     (with the language pack) sees those words; everyone else sees the plain `name` / `doc`. Example:
     `"text": { "name": "Zap Test", "name.sp": "ZAP DE PRUEBA", "name.jp": "ザップテスト" }`. Write the words the way the game
     writes them in that language (capitals in Spanish, French, German and Italian; French without accents on capitals).
     The Gen 4 pack is made this way.
   - `category` (moves only) is `"physical"` or `"special"`. It is used when the player has the physical/special split
     setting on; with the split off the move follows its type, like every move in the original game.

3. Give the new Pokémon to yourself some way you can see it. The simplest is to use it in a trainer's team, or a
   Shadow Pokémon slot, by name, from another data file:

   ```json
   { "virtual_pokemon": { "#1": { "pokemonID": "Zorblat", "wazaID": ["Zap Test"] } } }
   ```

   (`virtual_pokemon` is a deck table. Check the real table and entry with `--tables` and `--show`.)

4. Switch the mod on and start a New Game.

## Labels: names that never change

Every new Pokémon, move and item also gets a **label**, a permanent name made of your namespace and the row's name:
`species:ada.zorblat/zorblat`. The number is still handed out when the game starts and can differ between setups; the label is what
saves remember, so adding another mod before yours does not scramble anyone's Pokémon. Full rules: [labels.md](labels.md).

- In `mod.json`: `"label_namespace": "ada.zorblat"` (your name, a dot, the pack's name; lower case). Set it once and never change it.
- In a row: the label's name part is made from the row's name (`"Zap Test"` is `zap-test`). To keep it when you change the display
  name, write it down: `"label": "zap-test"`.
- Anywhere a species, move or item is written, a label works: `"copy": "species:base/eevee"`, `"waza.dataId": ["move:ada.zorblat/zap-test"]`.
  `xd_data_mods <disc> --labels species` lists the official labels on your disc.

## What a new row is, and what it shares

A new Pokémon fights, levels up and evolves like any other. By default it **shares with the row it copied**:

- its 3D model and cry (its face picture too, unless you give it one: see "Its own face" below);
- its two Pokédex flag numbers (the "seen" and "caught" flags in the row; see "Its own Pokédex marks": the game itself does not write them);
- its learn-set and move animation, unless you point those fields at something else that exists.

So Zorblat looks and sounds like Eevee. A new move reuses an existing move effect, animation and AI row.
Each of these can be made its own, one by one, with the keys below. A key you leave out changes nothing.

| Key in the species row | What it gives | Section |
|---|---|---|
| `"face"` | its own face picture (party list, summary, PC) | Its own face |
| `"icon"` | its own small picture in the PC box grid and the party list | Its own small picture |
| `"model"` | its own 3D model in battle, in the Memo's Size picture and in scenes | Its own model |
| `"cry"` | its own cry | Its own cry |
| `"dex": "own"` | its own Pokédex "seen" and "caught" flag numbers (what they do today is explained in the section) | Its own Pokédex marks |
| `"animation"` (on a **move**) | the animation of another move | A new move's animation |
| `"animation_attack"`, `"animation_damage"`, `"animation_status"` (on a **move**) | a piece of another move's animation, one each | A new animation put together from the pieces of several moves (see below) |

## Its own Pokédex entry

The PDA's Pokémon list (the entry you open for a Pokémon you have) reads the species row, so a new species has its own
entry as soon as you set these fields:

```json
"height": 17,
"weight": 333,
"text": { "typeName": "Static Test", "doc": "A made-up Pokémon." }
```

- `height` is in tenths of a metre (17 = 1.7 m), `weight` in tenths of a kilogram (333 = 33.3 kg).
- `typeName` is the category line under the name ("Static Test").
- `doc` is the description. XD's PDA does not show a description on the entry, so it is stored in the row for mods and
  tools that read it, nothing more. (The original game's Eevee has none either.)
- Leave them out and the entry shows the copied Pokémon's height, weight and category.

## Its own face
The small face in the party list and on the summary page comes from the face table. A new species copies its source's face. Give it
its own with `"face"` in the species row:

```json
"Zorblat": {
  "copy": "Eevee",
  "face": "textures/faces/zorblat.png"
}
```

- `"face": "Umbreon"` (a species name, or a number like `"#197"`) shows that species' face. No picture needed. A species your mod
  adds earlier in the same file works too.
- `"face": "textures/faces/zorblat.png"` is **your own picture**, a PNG file inside your mod (8-bit, with or without transparency,
  not interlaced). The path is written with `/` and starts inside the mod folder. It can also sit in a `games/<game>/` folder.
- A picture twice as high as wide is read as the **normal face on top and the shiny face below** (the game's own layout, 42 x 84
  pixels). Any other shape is one face used for both. Draw it bigger if you like (84 x 168 is nice): the port shrinks it to the game's
  size and **256 colours**, like the game's own faces.
- Put your pictures in `textures/faces/`. The texture folder is also read as an HD texture pack, but only files with Dolphin's names
  count there, so your face files are not mistaken for one.
- Two species may use the same picture file; it is stored once.
- Nothing from the game is in your mod. The game's own faces stay as they are.
- If the file is missing or not a PNG, the species keeps the face it copies and the game says why when it starts.

The face is shown in the party list, the summary page, the PC boxes and everywhere else the game draws a Pokémon's face.
The big picture used by some menus (the Pokémon "body" picture) is still the copy's.

## Its own small picture (PC box and party list)

The PC box grid and the Party Pokémon list draw each Pokémon as a small picture that wiggles a little. A species that copies another
one shows that one's small picture (a copy of Umbreon shows Umbreon there, even with its own face). Give it its own with `"icon"`:

```json
"Quillfox": {
  "copy": "Eevee",
  "icon": "textures/icons/quillfox.png"
}
```

- A PNG file inside your mod (8-bit, with or without transparency, not interlaced; path with `/`, may sit under `games/<game>/`).
- **54 x 108 pixels** is the game's own size: the normal picture on top (54 x 54) and the shiny one below. Any picture twice as high as wide is read that way; any other shape
  is one picture used for both. A bigger picture is shrunk to fit 54 x 54 (the shape is kept), a smaller one is not stretched. It stands on the bottom edge. The picture is put into the game's
  three wiggle frames, so draw it standing, three-quarter view, with a thin dark outline like the game's own ones.
- 256 colours at most (the game's format); the port reduces them for you.
- Nothing from the game is in your mod. If the file is missing or not a PNG the species keeps the small picture it copies, and the game says why when it starts.

The packs the launcher makes from an Ultra Sun / Ultra Moon dump set this key for every Gen 4 and Gen 5-7 species (pictures drawn from the models).

### A Pokémon the game already has

`"face"` and `"icon"` also work when you **change** a Pokémon the game already has, not only in an `"add"` block. This is for a row that has no
picture of its own (Munchlax's row on the disc is an empty one: it has no face and no small picture, so the PC box showed a blank cell):

```json
{ "pokemon": { "Munchlax": { "face": "textures/faces/446.png", "icon": "textures/icons/446.png" } } }
```

- The pictures are read exactly as for a new Pokémon (see above). A name works for `"face"` too (`"face": "Snorlax"`).
- It replaces what the row had, so give it only to a row that needs it: leave the key out and the Pokémon keeps the picture the game gave it.
- Only these two keys work on a changed row; a model, a cry and the Pokédex marks are still for new Pokémon.
- Not in the Pokémon XG edition (the same limit as for new Pokémon there).

## Its own model
```json
"Quillfox": {
  "copy": "Eevee",
  "model": "models/quillfox.pkx"
}
```

- `"model": "models/quillfox.pkx"` is a **model file of your mod**: a `.pkx` as the game stores them (the game's own Pokémon models are
  this kind of file; the converted Battle Revolution models are too). The path starts inside the mod and uses `/`. It can also sit under
  `games/<game>/`.
- `"model": "Umbreon"` (a species name, `"#197"`, or a species your mod adds earlier) uses that species' model. No file needed.
- The port makes a small archive of its own out of your file when the game starts and adds one row to the game's model table. The game's own
  files stay as they are; your mod holds only your file, never a copy of the game's.
- The model must be at most 614,400 bytes (the game's model memory). A file that is not a Pokémon model, too big or missing is **not used**:
  the species keeps the copy's model and the game tells you why when it starts (also in `xd_mod_check`, for a missing file).
- The model animates with the game's own animation slots, so it needs the same layout as the game's models (a converted model from
  the Battle Revolution tool already has it). Move animations of the copied move still play, with your model as the Pokémon.
- Two species may use the same file; it is stored once.
- A shiny of a new species uses the same model with the game's usual shiny colours.

## Its own cry
```json
"cry": "sounds/quillfox.wav"
```

- A **WAV file** of your mod: 8, 16, 24 or 32-bit sound (or 32-bit float), mono or stereo, any rate. The port turns it into the game's cry format
  (32 000 samples a second, two channels, 4-bit packed sound) when the game starts. A mono file is played from both sides.
- A cry can be at most **3.5 seconds**. A longer file is cut at 3.5 s (the game says so when it starts). The game's own cries are 0.5 to 2.5 s.
- `"cry": "Umbreon"` (a species name or `"#197"`) uses that species' cry.
- To change the cry of a Pokémon the game already has (not one your mod adds), see [music-and-sound.md](music-and-sound.md) (`xd_audio_tool encode`).
- The cry is used everywhere the game plays the species' voice: when it is sent out, hurt and in the evolution scene. The little wave picture
  on the PDA's cry page is drawn from your sound too.
- Packing 4 bits a sample costs a little quality, as it does in the game's own cries. A short, clean sound works best.
- Nothing from the game is in your mod. The game's own cries stay as they are.

## Its own Pokédex marks
First, what the game does by itself. **The Strategy Memo (the PDA's Pokémon list) already gives every new species its own entry**: the
Memo records the Pokémon you have met by species number, so Zorblat shows as "Zorblat" next to Eevee, with its own model, height, weight and
category, and catching it never marks Eevee. You need no key for that. (The Memo holds 500 different species, see the top of this page.)

Each species row also holds two **flag numbers**, "seen" and "caught", that point into the game's list of save flags. A species that copies
Eevee points at Eevee's two flags. The original game's own marking function **does not use them** (it always writes the flag of species number 0;
this is how the game's code is, checked in the running game), so on the original game these flags do nothing visible. They matter to
**scripts** (`flag(n)` reads any flag) and to anything that wants to know "has this species been met" without the Memo.

```json
"dex": "own"
```

With `"dex": "own"` the species gets **two flags of its own** instead of its copy's two, so a script (or a later game change that writes them)
can tell it apart from the copy. `"dex": "copy"` (or no key) leaves it pointing at its copy's flags, as before.

- **It is not the default on purpose.** Own flags add rows, and a playthrough is tied to the rows its mods gave (see "Saves and added rows").
  Changing a mod that people already play from "shared" to `"own"` would make their saves be refused. Decide when you first publish the species.
- The two flags are put at the very **end** of the game's flag list. No existing flag moves, so old saves, the game's own flags and other mods
  are not touched. The numbers are in `added_ids.json` (the rows are called "seen mark of X" and "caught mark of X").
- **How many species fit.** The save has room for about **9,368** more one-bit flags after the game's own (measured on the disc: the game uses
  2,888 of the 12,256 bits that fit; the Gen 4 pack takes 214; a mod's story flags 1 to 16 bits each, at most 256). Every species with own
  flags needs 2. So all **4,096** species a mod set can add fit (8,192 of those bits); the limit is the 4,096 spare rows of the
  game's tables, not the save. If the room ever ran out, the port says so and the species keeps its copy's flags.
- Species the Gen 4 pack adds already have flags of their own.

## A new move's animation
```json
"Quill Zap": {
  "copy": "Tackle",
  "animation": "Thunderbolt"
}
```

A move row says which of the game's **animations** it plays. `"animation"` takes a move's name (or `"#85"`, or a move your mod adds earlier) and
plays **that move's animation** instead of the copied move's. That is every animation the game has (the beams, bites, punches, balls, winds...).
### Putting a new animation together from the game's pieces
A move's animation has **three pieces** in the game's table: what the **user** does (`attack`: the run-up, the beam leaving, the camera), what
happens to the **target** (`damage`: the hit, the burst, the flash on it) and a third **special / status** piece (moves that only change stats
or conditions, like Growl, use only this one). Each piece is one of the game's effect files (camera moves, particle effects, model moves and sounds all live inside it). You can make a
new animation by taking each piece from a different move:

```json
"Quill Zap": {
  "copy": "Tackle",
  "animation_attack": "Thunderbolt",
  "animation_damage": "Flamethrower",
  "animation_status": "none"
}
```

| Key | Piece taken from the named move |
|---|---|
| `"animation_attack"` | the user's part |
| `"animation_damage"` | the part on the target |
| `"animation_status"` | the third (special / status) part |
| `"animation"` | with the keys above: the move to take every part from that you did not name (nothing is played for a part named `"none"`) |

A move's name, `"#85"`, or a move your mod adds earlier works for each. The new animation is a **new row of the game's animation table** (made when
the game starts; up to 4,096 of them), so the other moves keep theirs, and a save does not depend on it.

What this does **not** do: it does not change the insides of a piece (the particles, the camera path, the sounds in it). That needs the
game's effect file format, which no tool writes yet; see [roadmap.md](roadmap.md). Mixing the pieces of two moves is allowed even when they were
not made for each other: for example a beam on the user that ends on a burst can look odd, but it does not break the game.

A Pokémon with a model of its own uses the same per-move animation as every other model; the game's few hand-made per-species variants
(for example Eevee's own versions of some moves) belong to the game's own species numbers.

## Metronome and the physical/special split

- **Metronome** can pick every move a mod adds. Nothing else changes: a game with no added moves picks exactly as before.
  It picks from the first move a mod added up to the last one in the table. The moves of the Gen 4 pack are left out
  when they come first in the table. Write `"metronome": false` on a move Metronome must not pick. (A copy of a move the game keeps out of Metronome, like Struggle,
  is kept out too.) The game build that honours `false` needs the decompilation update that adds the never-pick list; with an older
  game build the key is accepted and Metronome may still pick the move.
- **Physical/special split**: with the player's setting on, a move with `"category"` is physical or special as you wrote.
  A move without it follows its type.

(This page is about XD. The same idea for Colosseum is future work: [two-games.md](two-games.md). Own models, cries, faces and
Pokédex marks work on XD only for now; on XG only new Pokémon, moves and items (without those extras) can be added, see [two-games.md](two-games.md).)

## Bigger lists: how far each list can go, and what stops it

This is the measured list (the numbers are in the game's own program and save, read from the decompilation). "Port" means this program;
"game" means the decompiled game code the port is built from.

| List | Plain game | Mods can reach | What stops it | What it would take |
|---|---|---|---|---|
| New Pokémon, moves, items (rows) | 415 / 375 / 594 | **+4096 each** | the game's compiled tables have 4096 spare rows (`PC_SPARE_*`) | the decomp raises the number; the port reads it from the snapshot by itself (no change here) |
| Strategy Memo entries | 500 saved | **unlimited** (tested with 602) | the save holds 500 | **built**: the extra entries are in the save extension file ([save-extension.md](save-extension.md)) |
| Shadow Pokémon | 48 in the story, 83 rows in use | **up to 127 rows now (44 free)** | the save's Shadow list has 128 places; the game checks `< 0x80` in five places | rows 84-127 are plain data (see below); past 127 needs the decomp |
| TMs and HMs | 58 (50 + 8) | **6 more** in the bag; more moves per TM list | the bag's TM pocket has 64 slots; a species has 58 bytes of "can learn" | a bigger pocket and a wider row: decomp (see below) |
| Move tutors | 12 | 12 | a species row has 12 tutor flags (`teachWaza[12]`), the tutor table 12 rows | decomp: a wider row |
| Bag slots | items 30, key 43, balls 16, TMs 64, berries 46, colognes 3, discs 60 | same | the save's bag arrays have these sizes | extension file + a few lines in the game's one bag function (decomp) |
| Abilities | 78 + 178 spare = 256 | **255** | an ability number is one byte in a species row | see "Abilities beyond 255" below |

### Shadow Pokémon: what a mod can do today

The save has a Shadow list of **128** records and the disc's Shadow table has **128** rows (`shadow_pokemon`), of which the game uses rows 1 to 83.
Rows **84 to 127 are empty** (species 0, level 0). A data mod can fill them: set the row in `shadow_pokemon` (which deck Pokémon it is, its
level, the heart gauge, the catch rate) and the same number in `shadow_species` (`darkPokemonList`). Each Shadow row points at a row of
`story_pokemon` for its species, level and moves; point it at one that exists. No new file, no change in the game. Past 127 needs the
decomp: the number 128 is written into the game in five places , and the save has no room for more
records, so the extra records would live in the save extension file.

### TMs and HMs, tutors, bag slots: why they are not just more rows

These three are **fixed-size lists inside bigger records**: a species row has exactly 58 "can learn this machine" bytes and 12 tutor flags;
the bag in the save has exactly 64 TM slots. Extra rows cannot make those longer, and the GameCube save cannot change. The Gen 4 pack already
gets around the first one for its extra machines (`gen4_machines.cpp`: it keeps the extra bits in a table of its own and answers the game's
two "which move / which species" questions itself). A general version for mods and a bigger bag pocket need the decomp to widen the rows
and to route the bag through one function. The list of changes is part of the decompilation work, not of a mod.

### Abilities beyond 255

A species row names its two abilities with one byte each, the ability table is indexed by that byte, and about 600 places in 31 game files
use the number. The Pokémon in the save stores only *which of the two* abilities it has (one bit), **not** the ability number, so a bigger
number never has to enter the save. The plan that keeps the save as it is: the species row keeps a byte; a byte value of 255 means "ask the
port's side table", which holds the real 16-bit ability number for that species and slot; the game's one question "which ability does this
battler have" (already routed through the port for `battle.ability`) and the ability name / description lookup answer from the side table. It
needs the decomp to widen those two answers to 16 bits. Not built: it is not cheap, and nothing in the plain game or the Gen 4 pack needs
it. Gen 1 to 7 together have 233 abilities, so the table has room for about 22 more before Gen 8's abilities (the 267th and up) need this.

## How a new Pokémon evolves

A species row has five evolution places: `"sinka.kind"`, `"sinka.buff"` (the number that goes with the kind) and
`"sinka.pokemonDataId"` (what it becomes), each a list of five. These are the kinds the game itself knows:

| `sinka.kind` | The Pokémon evolves... | `sinka.buff` |
|---|---|---|
| 1 | by friendship | 0 |
| 2, 3 | by friendship by day / at night (XD's clock never moves, so these never happen) | 0 |
| 4 | at that level | the level |
| 5 | when traded (the Linking Cord from the Gen 4 Pokémon does this too) | 0 |
| 6 | when traded while holding an item | the item |
| 7 | when that item is used on it (a stone) | the item |
| 8, 9, 10 | at that level if Attack is higher than / equal to / lower than Defense (Tyrogue) | the level |
| 11, 12 | at that level, by the Pokémon's personality (Wurmple) | the level |
| 14, 15 | Shedinja's and the beauty rules | see the game |
| 16 | by friendship while the item is in the bag (Espeon, Umbreon) | the item |

A mod can use exactly these. The longer lists of the Gen 4 and Gen 5-7 Pokémon (more than five evolutions, "level up
holding an item", "only female", "with a Dark Pokémon in the party", "knowing a move") are made by the port from the
packs' own `evolutions.txt` and are not something a mod can add.

## Numbers

- Species continue after the last one on the disc (415, 416, ...). Moves start at 375 (355 to 374 are special marks the
  game uses). Items continue after the last item number.
- The numbers follow the mods' load order and each file's order. The same mods in the same order always give the same
  numbers.
- A new row cannot have the same name as a row the game already has.
- To be exact about another mod's row, write `"mod-id:Zap Test"`.
- To see the numbers: `xd_data_mods <disc> --ids` prints them as JSON. When rows were added, the game also writes
  `added_ids.json` in its settings folder, and the log has a line `[mods] added: ...` for each row.
- A number that does not exist is refused. For example `"sinka.pokemonDataId": 999` is a message and is not used.

## Saves and added rows

The numbers of new rows end up in the save (a Pokémon's species, a bag slot's item). So the port ties each playthrough
to the rows it was made with. The setup file next to the save (`playthroughs\Card A\...json`) lists every added row.

At start-up the port checks that the same mods give the same rows the same numbers. If not (a mod switched off,
removed, another mod put before it, a row renamed) the save is **refused**:

- A message names what is missing.
- You can quit and switch the mods back on, or play without the memory card this time.
- Nothing is read from the card and nothing is written to it.

As a last guard, the game never reads a save with a species, move or item number past the end of the game's tables. It
stops with the message "Pokemon XD - this save needs its mods" before the game sees it. That happens for a save copied
to another PC without its mods, or a damaged save.

The own model, cry, face and flag rows are rows like the species and moves, and are listed in the same setup file. So a save made with a species that has
its own model and cry behaves exactly as described above when the mod is switched off: the game says which mod is missing, reads nothing from the card and writes
nothing to it. Switch the mod back on and the save loads as before, with the same model, cry and face. (Tested in the running game: made with the mod, loaded with the
mod, refused without it with the card untouched, and a copy of the save without its setup file stopped by the last guard.)

So: **do not rename or reorder the rows of a mod that people already use in saves.** Add new rows at the end, in
a new file, if you can.

## Taking a Pokémon out of a mod
A save that was played with your mod holds your new Pokémon, moves and items **by number**. A plain game (no mod) refuses that save, because it does not know
those numbers. The tool `xd_mod_extract` takes **one Pokémon** out of a modded save and puts it into a **plain** save, changing or refusing whatever the plain game
does not have. It never touches the source save, and never writes anything unless you say where (a dry run is the default). Nothing of the game is stored: it
reads your disc, like the other tools.

```
xd_mod_extract <your disc> <modded save.gci> list
xd_mod_extract <your disc> <modded save.gci> take <N> <plain save.gci> [options]
```

`list` shows every Pokémon of the source save (party first, then the PC boxes) with its number `N`, and says either `fits a plain save as it is` or
`NOT plain:` with what the plain game lacks, for example `[species #415 (only in a mod)]` or `[move #380 (only in a mod)]`.

`take` makes the Pokémon fit and puts it in the plain save's **first free place of the PC boxes** (`--party`: the party's first free place). Options:

| Option | What it does |
|---|---|
| `--species-map 415=133` | A Pokémon only your mod has comes out as this species of the plain game (here Eevee). Its level and moves stay; the experience, stats and HP are made again for the new species |
| `--move-map 380=33` | A move only your mod has becomes this move of the plain game |
| `--drop-moves` | A mod move with no map is forgotten (at least one move must be left) |
| `--item-map 600=17` or `--drop-item` | A held item only your mod has becomes that item, or is taken away (it is **not** put in the bag) |
| `--party` | into the party instead of the PC |
| `--out new.gci` | write the changed plain save to a **new** file (the plain save itself stays as it was) |
| `--in-place` | change the plain save itself (a backup of it goes to `save_backups` next to it first) |

Without `--out` or `--in-place` nothing is written: you get `DRY RUN` and the plan.

**What it refuses, with a plain reason** (exit code 1):

- a **Shadow Pokémon**: its record (status, heart gauge, order of meeting) belongs to the source save's Shadow list and cannot move;
- a species, move or held item the plain game does not have, when you gave no map for it (the message names the number and the option to use);
- a map that points at something the plain game also does not have;
- a Pokémon the plain game's own legality check would not accept after the changes (the same rules online trading uses for a save that plays the original game);
- no free place in the plain save, or no move left.

What it also does: a Strategy Memo (Pokédex) entry for the species if there is none, and it reads the new save back (checks and the Pokémon in place) before it says
it is done. Friendship, nature, IVs/EVs, the original trainer and the nickname stay as they were. **Things to know:** the place where it was met
(a room only your mod has) may show a wrong name in the summary, and the tool tells you when that may be the case; it does not change the game's Pokédex flags of your mod (the
plain game never had them).

**In the launcher**: the Save page's open save has a button **Take a Pokémon out of a mod...** next to "Colosseum import...". Open the plain save (the one that
should receive the Pokémon), press the button, choose the modded save's `.gci` file: every Pokémon of it is listed with what it loses on the way, or why it stays behind. Tick
the ones you want and press "Copy the ticked Pokémon into the PC boxes": they go into the first free PC places, the launcher keeps a backup of your save first
(`save_backups\modextract_<time>`), reads the new save back, and never changes the modded save. It works with the controller too. The button is the same work as the command line (one shared
library), with one difference: **it has no way to name a plain stand-in species.** A move or a held item that only your mod has is left out (listed as "loses"); a Pokémon of a
species only your mod has, and a Shadow Pokémon, stay behind with a note that says to use the command line (`--species-map`). So a modded Pokémon of a plain species that
knows a mod move comes out with one click; a Pokémon of your new species needs `xd_mod_extract` and a map.

## Test it

- `xd_data_mods <disc> --mods` and `--ids`: you should see "Data mods add 2 row(s)" and the numbers.
- In the log, `[mods] added: ...` for each row.
- In the game, check the name in the battle HUD ("Zorblat Lv 50") and in the message ("Zorblat used Zap Test!").
- Save, quit, start again with the mod on: CONTINUE must work. Start again with the mod off: the card must be refused,
  not read.

## Common mistakes

- Writing a number where you can write the name. Names keep working when the numbers change.
- Giving a row the name of an existing Pokémon or move.
- Copying a row that is below your new row in the same file. A row can only copy a new row that is above it.
- Expecting a new look or cry without `"model"` and `"cry"`. It copies its source's.
- Adding `"dex": "own"` to a mod that saves already use. The saves are refused (the rows changed); decide before you publish.
- Writing `"category"` and expecting it to work with the split setting off. It only counts with the split on.
- Removing the mod from a playthrough that has its Pokémon in the party.

## See also

- [labels.md](labels.md): stable labels for new rows
- [change-data.md](change-data.md): change the rows that already exist
- [battle-effects.md](battle-effects.md): give a new move or ability an effect
- [save-extension.md](save-extension.md): limits and what a save keeps
