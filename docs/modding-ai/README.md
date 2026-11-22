# Modding guide for AI helpers

> Early version: the format can still change.

This guide is for an AI helper that is asked to make a mod for the Pokémon XD: Gale of Darkness PC port (disc `GXXE01`, USA).
It is a compact, exact reference: folder layout, `mod.json`, the data format, the Luau and story APIs, the tools, and a checklist.
Read this page first, then only the topic file for the job. The human guide, written for players, starts at
[../modding/README.md](../modding/README.md); the helper may link to its pages when the player needs a longer explanation.

## Topic files

| File | Covers |
|---|---|
| [format.md](format.md) | Folder layout, one mod for several games, `mod.json`, languages, data file format, finding names |
| [scripting.md](scripting.md) | Luau battle scripts: hooks, sandbox, names, the player's permission, which game offers what |
| [story.md](story.md) | Story scripts (`.xds`), rooms, people and chests, field hooks, script power, taking a Pokémon out of a mod |
| [content.md](content.md) | Music and sound, battle effects (JSON blocks), characters and models |
| [tools-and-saves.md](tools-and-saves.md) | Checker and packer, signing and updates, saves and playthroughs, save extension file, reload while playing |
| [xg.md](xg.md) | What a mod can do on Pokémon XG |
| [writing-docs.md](writing-docs.md) | How these guides are kept up to date |

## Hard rules

1. **Never include game files or game data in a mod or in a reply.** No extracted files, textures, sounds, models,
   text banks, decompiled scripts, disc images, save files. The player supplies their own disc.
2. A mod holds only: `mod.json`, `data/*.json`, `scripts/*.luau`, the author's own new pictures, optional readme.
3. **Mods must not break saves.** New rows (species, moves, items) are bound to the save. Never rename or reorder rows
   of a mod already in use. Append new rows.
4. **Do not invent names, tables, fields or numbers.** Look them up (see [format.md](format.md), "Finding names"). If you cannot run the tools,
   ask the player to run them, or tell them which names you could not verify.
5. **Scripts must be deterministic.** Replays must repeat exactly. No clock, no random.
6. Only claim what the human guide says exists ([../modding/README.md](../modding/README.md)). A feature listed under "What does not exist yet" below is not available. Everything in a draft page may change.
7. Never edit the player's save or `mods.ini` without being asked.
8. Say plainly what the mod does and what could not be done.
9. **Never put a program or a script the PC could run in a mod** (`.exe .dll .so .dylib .bat .cmd .ps1 .vbs .js .jar .sh`,
   a Mac `.app`, or any file that starts like one), no archive inside the mod, no links. A mod is JSON, Luau source
   text, story text, PNG/DDS pictures and readmes. `xd_mod_check` refuses the rest (see [tools-and-saves.md](tools-and-saves.md)).
10. Be honest: the checker lowers the risk, it cannot prove a mod harmless. Tell the player to install mods only from
   people they trust.


## What does not exist yet

New maps (a new room is a COPY of an existing one), new models for trainers and the world, music that is not a sound stream (sequenced songs), new map triggers for zones, abilities as Luau scripts (abilities made of blocks exist: see [content.md](content.md)), table writes that last (only `battle_tables` for one battle; the read side is `species_info`, `move_info`), PC / PDA menu entries (the field menu has `add_menu_entry`), setting a trainer's name / AI / class from a script (only the team: `set_trainer_team`), a stand-in-species chooser in the launcher's "Take a Pokémon out of a mod" (the command line has `--species-map`), a different mod version per save (the game runs one version of a mod at a time; a save that keeps a mod's numbers says plainly when the version changed, and the launcher holds an update back for it), hot reload of data values (only scripts and textures reload live: see [tools-and-saves.md](tools-and-saves.md)). Do not promise
these. See [roadmap.md](../modding/roadmap.md).


## Checklist before you hand a mod to the player

- [ ] `mod.json` is directly in the mod folder, with `id`, `game` (`GXXE01`), `version`, `description`, and a honest `applies`.
- [ ] `"scripts": true` if there is a `scripts` folder.
- [ ] Every table, entry, field and name was looked up with the tools, or listed as unverified.
- [ ] JSON is valid (commas, quotes, no trailing comma). Luau is plain whole-number code, no random, no clock.
- [ ] New rows use names, have `copy`, and are in a file that sorts first.
- [ ] No game files, no extracted scripts, no dumped textures, no saves in the folder.
- [ ] If rows were added: `label_namespace` is set in `mod.json` and never changed; give a row a `"label"` before you rename its text; never remove
      a row or change its label in an update (saves holding it would be refused). Reordering is safe with labels.
- [ ] Text is no longer than what it replaces (per table).
- [ ] You told the player: where to put the folder, to switch it on, whether to start a New Game, and any other mods
      needed and the order.
- [ ] If possible: `xd_data_mods <disc> --mods` shows no problems, and a test battle or New Game works.
- [ ] You listed anything you could not test.
- [ ] `xd_mod_check <mod folder>` (no disc needed) shows no PROBLEM and no safety finding; with a disc, `--disc <disc>` too.
- [ ] No program or script the PC could run, no archive inside, no links, no game files (`xd_mod_pack` refuses them).
- [ ] `version` is dotted numbers and was raised; `updates` (if any) is https and `version` is set.
- [ ] You told the player the mod's permissions in plain words, and that mods should come from people they trust.

## Battle replays and mods

A replay's header has `battle_mods` (mods that change a battle: data mods, script mods, mods with `battle/` effect files (their digest is in the header), mods whose "any time" claim fails CheckApplies) and `mods` (all). Only
`battle_mods` must match to play a replay (`BlocksReplay()` in games/xd/src/mods_boot.cpp is the one rule). To keep a mod from blocking replays: only replace
looks/sound files (textures, models, music, cries), set `applies: any_time`, add no `data/`, no `battle/`, no `scripts`.
