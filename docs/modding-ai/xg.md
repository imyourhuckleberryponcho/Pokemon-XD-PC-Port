# Mods on XG

What a mod can do on Pokémon XG, the romhack of XD that the same program plays. Back to the [front page](README.md).

## Mods on XG
XG is its own mod target (0.7.0 DRAFT): `"game"` must name `"xg"` for a mod to load there; a mod naming only `"xd"` (or a bare ID, or a flat layout) is refused on XG
with "Made for XD, not XG" (never loaded silently). Folders: `games/xg/` on XG; `["xd","xg"]` without `games/xg/` reads `games/xd/` on XG too (the author says it is safe);
both folders: each edition reads its own. Tags everywhere mods are listed: `XD`, `XG`, `XD + XG`. Checker: notes `xd-only`, `xg-folder`, warning `xg-xd-data` (names xg, data is XD's).
The same program plays Pokemon XG (a romhack of XD, same game id). `xd_xg.h` decides what is on. ON in XG: texture packs, data
mods that CHANGE existing rows, Luau battle scripts, story scripts using existing flags (untested on XG), Battle Revolution models, randomizer.
The Gen 4 POKEMON (not the Gen 4 rules) also work on XG since 0.6.1: the pack is translated by name to XG's moves and abilities (docs/GEN4.md 0c).
Mods can add rows on XG too (0.7.0, DRAFT, tried on the real XG disc: add, save, CONTINUE, a battle with the new species and move, refused without the mod):
`add` blocks for `pokemon`, `moves` and `items` (xd_xg.h `DataAddRows`; mods_boot.cpp leaves only those three tables addable on XG).
OFF in XG: `add` rows for story flags (`flags`), rooms, battles and trainer decks, the species asset keys `model`, `cry`, `dex` and a move's `animation` (a species' `face` and `icon` work on XG since 0.7.5, also in an edit of a disc row such as Munchlax's; tried on the XG disc: party list and PC box),
the Gen 4 rules, Fairy / modern types, language packs. A data mod with those add rows still loads; they are dropped and the log line is
`[mods] data mods: in this edition only new Pokemon, moves and items can be added`. Never assume XD's species/move/item numbers are XG's: say in the mod's
description which disc it is for.
