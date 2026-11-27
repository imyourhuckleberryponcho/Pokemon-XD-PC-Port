# What is available

> Early version: the mod format can still change.

**What this page covers:** what a mod can do in version 0.7.0, and how settled each part is. "Available" means it works and is not expected to
change. "Experimental" means it works but is new, so it may change or have rough edges. "Not available yet" means it is only planned (see
[roadmap.md](roadmap.md)).

| Area | Status | Page |
|---|---|---|
| Change numbers, trainers, Shadow Pokémon, shops, TM moves, item prices | Available | [change-data.md](change-data.md) |
| Change message text (the new text must fit the old length) | Available | [change-data.md](change-data.md) |
| Replace whole files, or files inside the game's archives (for experts) | Available | [sharing.md](sharing.md) |
| HD texture packs | Available | [textures.md](textures.md) |
| Other languages made from the European disc; own text changes | Available | [translations.md](translations.md) |
| Add Pokémon, moves and items, with their own Pokédex height, weight and category; Metronome and the physical/special split for new moves | Available | [add-pokemon-moves-items.md](add-pokemon-moves-items.md) |
| Own face, model, cry and Pokédex marks for a new Pokémon; another move's animation for a new move | Experimental | [add-pokemon-moves-items.md](add-pokemon-moves-items.md) |
| Take a Pokémon out of a mod into a plain save (command line and the launcher's Save page) | Experimental | [add-pokemon-moves-items.md](add-pokemon-moves-items.md) |
| Stable labels for new rows | Experimental | [labels.md](labels.md) |
| A Strategy Memo with more than 500 species | Experimental | [save-extension.md](save-extension.md) |
| Luau battle scripts: damage, what the trainer AI expects, watching a battle | Available | [battle-scripts.md](battle-scripts.md) |
| Luau hooks `battle.ability`, `battle.ai_switch`, `battle.ai_item` and the per-frame step budget | Experimental | [battle-scripts.md](battle-scripts.md) |
| Names instead of numbers in scripts | Experimental | [lua-names.md](lua-names.md) |
| New move effects, abilities, terrains and rules for one battle (JSON blocks) | Experimental | [battle-effects.md](battle-effects.md) |
| Story and event scripts: text, new lines and functions, flags of your own, new rooms, a changed shared script | Experimental | [story-scripts.md](story-scripts.md) |
| From scripts: trainer and wild battles, items, Pokémon and Shadow Pokémon gifts, a foe's team, a rental team, saved numbers and currencies, pages, picks and shops, a field-menu entry, saving, numbers in messages, rules for one battle | Experimental | [story-scripts.md](story-scripts.md) |
| Field hooks (room ready, battle start and end, item use, steps, a flag or money changed, a Pokémon caught, the field menu opened) | Experimental | [story-scripts.md](story-scripts.md) |
| People, zones, chests, music and doors in rooms | Experimental | [story-objects.md](story-objects.md) |
| A mod that says it needs (or likes) the player's own extra assets: Battle Revolution or Ultra Sun / Ultra Moon models, the Gen 4 or Gen 5-7 pack (`needs_assets`, `recommends_assets`) | Experimental | [sharing.md](sharing.md) |
| Own 3D model for a trainer or a person (replacing an existing one) | Experimental | [characters-and-models.md](characters-and-models.md) |
| Own songs, cries, single sound effects and new songs | Experimental | [music-and-sound.md](music-and-sound.md) |
| Check a mod, pack it, and check a downloaded mod from the launcher | Experimental | [sharing.md](sharing.md) |
| Switch a mod's scripts off while keeping the rest of the mod | Experimental | [sharing.md](sharing.md) |
| Mod updates in the launcher, with signing and roll back | Available | [sharing.md](sharing.md) |
| Reload scripts and textures while the game runs (developer setting) | Experimental | [reload.md](reload.md) |
| One mod folder for more than one game | Experimental | [two-games.md](two-games.md) |
| Mods on Pokémon XG (change rows; add Pokémon, moves and items) | Experimental | [two-games.md](two-games.md) |
| New maps; models and animations of your own for new trainers and the world | Not available yet | [roadmap.md](roadmap.md) |
| Other new game mechanics beyond the hooks | Not available yet | [roadmap.md](roadmap.md) |

## Known rough edges

- About 1000 of the game's story flags have sure names. About 610 more have names made by a program from the room scripts, and those are guesses. A
  script's own variables have guessed names only.
- Colosseum does not run in the port yet. The mod format for it is ready, and the shared parts of a mod will work in both games once it does.
- The **Reload mods** row on the in-game MODS page and the reload of texture packs were built and tested in code, but not yet seen working in a live
  game. Reloading scripts with the shortcut was.

## See also

- [using-mods.md](using-mods.md): switch a mod on
- [roadmap.md](roadmap.md): what is planned
