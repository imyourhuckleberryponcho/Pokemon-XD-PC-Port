# What is planned

> Plans can change.

**What this page covers:** what modders cannot do yet and what may come. There are no dates. For what already works, see [status.md](status.md).

## Story and places

- New maps and world models. A new room is still a copy of an existing room, and event zones need triggers the map already has.
- Adding, moving and removing people with a tool that places them; building a new place in a 3D program and bringing it in.
- A higher-level language for story scripts, instead of raw instructions.
- Better names for the script's commands. A script's own variables and about 610 story flags have guessed names only.
- Menu entries other than the field menu (the shop, the PC, the PDA).

## Pokémon, moves and items

- A tool that makes a Pokémon model in the game's format. Today a model comes from the game's own files or from a converter.
- Changing the inside of a move animation (particles, camera path, sounds). Mixing the three pieces of existing animations works today.
- The Pokémon's big body picture in menus for a new species. It uses the copy's picture today.
- More than 4096 new Pokémon, moves or items (raised from 1024 in 0.7.1). The limit comes from the spare rows in the game's tables, and the port follows when that number is raised.
- More Shadow Pokémon past row 127, more TMs than the pocket holds, more tutors, more bag slots, and abilities past 255. Each needs small changes in
  the game's own code. See "Bigger lists" in [add-pokemon-moves-items.md](add-pokemon-moves-items.md).
- Choosing a stand-in species in the launcher when taking a Pokémon out of a mod. The command line has `--species-map`.

## Scripts and battles

- Hooks that let a script make the trainer AI choose something, and hooks for the effects of other abilities.
- A Luau route that returns battle effect blocks, `force_double`, bag items banned by a rule set, and field states that change damage by themselves.
- Writing the game's tables from a script at run time. A save and a replay depend on those tables, so only the named, checked writes exist.
- PC and PDA menu entries, and a different mod version per save.
- Starting a song from Luau (a story script can start one).
- Reloading data values while the game runs. The game reads its tables once at start-up.

## Sound and looks

- Songs the game plays from music instructions instead of a recording, and a check that listens (the checker looks at the files, not at the sound).
- Models and animations of your own for new trainers and for world objects. Replacing an existing model works today. Also a model that the port builds
  from the player's disc when the game starts, so that it can be shared.

## Mod management

- Story-object checks in the mod checker.
- More permissions in the launcher, as more kinds of mods can do more.
- The same tools for Colosseum, and one mod for XD and Colosseum. The format is ready, and Colosseum itself comes after XD 1.0. Colosseum's table and field
  names, its hooks, and a check of whether people, chests, doors and memo formats match XD's are still to do.

## How to ask

If something you need is missing from this list, say so in the project's Discord or issue tracker, and say what you want to make. That decides
what gets built first.

## See also

- [status.md](status.md): what works today
- [two-games.md](two-games.md): the shared mod format
