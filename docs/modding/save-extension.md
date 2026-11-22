# The save extension file: room for more than the GameCube save can hold

> Early version: the format can still change.

**What this page covers:** the second file that sits next to a save when a mod needs more room than the GameCube save has, what is kept
in it, and what is not available yet.

## Overview

The game's save file has a fixed shape. It was made for the GameCube, it has room for exactly so many Pokémon in the Strategy Memo, so many
Shadow Pokémon records, so many bag slots. **The port never changes that shape**: a save the port writes can still be opened by the plain
game, by older versions of the port and by Dolphin, and a real GameCube save keeps working.

When a mod needs more room than that (more than 500 species in the Strategy Memo, for example), the extra part goes in a **second file
next to the save**, called the *save extension*. It is the same idea as the playthrough's setup file, which already sits next to the save.

```
Pokemon XD\playthroughs\Card A\pt_0000abcd_1234abcd.json     the playthrough's setup (already existed)
Pokemon XD\playthroughs\Card A\pt_0000abcd_1234abcd.ext      the save extension (new; only when there is something to keep)
```

A plain game with no mods never makes this file. A save from before this existed has none and loads exactly as before.

## What happens when

- **When the game saves:** the port first writes the new extension under a temporary name, then lets the game write the memory card, and
  only when the card write worked does it rename the extension into place. If the computer dies half way, the next start notices (see "A
  save and its extension must belong together").
- **When the game loads (CONTINUE):** the port reads the extension that belongs to exactly that save and hands each part to the feature that
  owns it.
- **Backups:** the launcher's automatic save backups keep the extension next to the save in the same backup folder. Restoring a backup
  puts the save and its own extension back together. The Save page still lists one row per save; the extension is not a save of its own.
- **Copying a save slot** (the launcher's save slots, "Move my data"): the whole `playthroughs` folder is copied, so the extension goes along.

## A save and its extension must belong together

The extension remembers a fingerprint of the exact save file it was written for. If the save was changed by anything else since (a backup
that was put back without its extension, a save copied in from Dolphin, a version of the port that does not know the extension), the two no
longer belong together and the extension is **not used for that save**.

What then happens depends on what the extension holds:

| The extension holds | Missing, from another save, or damaged |
|---|---|
| only **optional** parts (the Strategy Memo's extra entries) | the game plays on; the extra entries are simply not there; a line in the log says so |
| a **required** part (data the save cannot be played without) | the save is **refused** with a message, before anything is read; the memory card is not touched |
| a part written by a **newer** version of the port | refused, with a message (so an older version never overwrites newer data) |

Which parts a save requires is written into its setup file (`"ext_required"`), with every save, so a lost extension file is noticed.

## Opening the save without the mods that made it

Nothing changed here: a save made with mods that add Pokémon, moves or items is already refused by the port when those mods are off
(the setup file names the added rows, and the "save guard" stops numbers the game does not have). The extension adds no new way to get
this wrong: it is only read for a save the port has already accepted.

A save made with mods and then opened in the **plain game or an older port**: the GameCube save itself is valid, so it opens. The memo
simply has its first 500 entries. Features that REQUIRE their extension (more Shadow Pokémon, extra bag slots, once they exist) also put
numbers in the GameCube save that the plain game does not have, and the older port's guard refuses those saves.

## The Strategy Memo past 500 species (built)

The memo in the save holds a count and 500 entries. The 412 Pokémon of the game never fill it. With the Gen 4 pack (about 100 more) or the
Gen 5-7 plan (800 and more) it can.

- The first 500 entries live in the save as always. The 501st and later live in the extension's `memo` section (10 bytes each: species with
  the "new" flag, the two random numbers the memo shows with a Pokémon).
- The memo's own screens (list, sorting, the page of one Pokémon) only ask four questions of the memo: how many entries, which species is at
  place N, and the two random numbers of a species. The port answers those four from the save for the first 500 and from the extension for
  the rest. The screens themselves are the game's, unchanged.
- While the memo has fewer than 500 entries, the game's own functions answer, byte for byte as before. A plain game cannot even reach the
  extension's code.
- Tested in the running game with 602 species (a test mod adds 190 to the game's 415, and the first 602 are put into the memo):
  the game saves, CONTINUE shows all 602, a second save after two more species keeps 604, and CONTINUE again shows 604. The save backups hold the
  extension next to the save they belong to. With the extension file deleted, the game plays on with the memo's first 500; if the setup file
  says the extension is required and it is gone, the game refuses the save (exit code 6) and the card is not touched. Without the mod the save is
  refused by the setup file as before, card untouched.

## For people who write code (a feature with extra data)

A feature registers a *handler* with a section name, a version, whether it is required, and three small functions: "give me your state as
bytes", "here are your bytes again", "forget everything". See `games/xd/src/xd_save_ext.h`; the memo is the model
(`games/xd/src/memo_overflow.cpp`). The file format and the rules are in `engine/saveext/include/saveext.h`: named sections, a checksum, the
save's fingerprint, unknown sections kept as they are. The Pokédex design keeps its own "caught" list next to the save; it is meant to be
one more section of this file (`dex`), not a second mechanism.

## What is not built yet, and why

See the "Bigger lists" section of [add-pokemon-moves-items.md](add-pokemon-moves-items.md) and [roadmap.md](roadmap.md). In short: more
Shadow Pokémon (past 128), more bag slots and more TMs than the pocket holds need small changes in the game's own code (the decomp), not
only a place to keep the data.

## See also

- [add-pokemon-moves-items.md](add-pokemon-moves-items.md): how far each list can go
- [labels.md](labels.md): what a save records about mods
- [roadmap.md](roadmap.md): what is planned
