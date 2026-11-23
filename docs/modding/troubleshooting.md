# Troubleshooting

> Early version: the format can still change.

**What this page covers:** the messages the game and the mod tools show when a mod has a problem, what each one means and what to do.

When something is wrong with a mod, the game tells you. At start-up it can show a window headed "Pokemon XD - mods"
that says "Please check your mods:" and lists problems. The same lines are in the log (newest file in the `logs`
folder of the data folder), each starting with `[mods]`. The game starts anyway, with the mods that can be used.

With the command-line tools, `xd_mods_check <disc>` and `xd_data_mods <disc> --mods` print the same lines without
starting the game. See [change-data.md](change-data.md#tools-you-can-run). To see everything wrong with a mod before
you share it, with no game and no disc, use `xd_mod_check <mod folder>` ([sharing.md](sharing.md)); its messages are
explained in "The mod checker and packer" at the end of this page.

Messages come in three levels. Info is just news. A warning means something is odd, but the mod mostly works. A problem
means the part named in the message is not used. The words below are the real messages (names in quotes are filled
in by the game).

## Script errors in the game log (Luau)

The game names the **mod, file and line** and switches only that mod's scripts off: `[script my-mod] switched off: in the "battle.damage" handler: my-mod/scripts/damage.luau:3: attempt to index nil with 'hp'`. `ran too long (more than 200000 steps ...)` = a loop or recursion without end (see [luau.md](luau.md)); `not enough memory` = over the 16 MB a mod's scripts may use.

## The mod is not found, or not usable

| Message (shortened) | What it means | What to do |
|---|---|---|
| `The folder "X" in the mods folder has no mod.json, so it isn't a mod; ignored.` | The folder has no `mod.json` in it. Often the mod is one folder too deep after unpacking. | Move the files up so `mod.json` is directly in the mod folder. |
| `"X" in the mods folder is a packed file: unpack it into its own folder` | A `.zip` was dropped in. | Unpack it into its own folder. |
| `The mod in "X" can't be used: its mod.json ...` followed by a reason | `mod.json` is wrong. See the next table. | Fix the reason. |
| `The folders "A" and "B" both hold a mod with the id "X"; only "A" is used.` | Two copies of one mod. | Delete one. |
| `New mod found: "X" (...). It is off; to use it, set "id = on" in mods.ini.` | Normal. New mods start off. | Switch it on. |
| `The mod "X" is made for another game or game version (...); this disc is .... It is not used.` | `game` in `mod.json` does not match your disc. | Use `"game": "GXXE01"` for the USA disc. |
| `The mod "X" is on but has no files in "files", "fsys" or "data".` | The mod folder is empty of content. | Add content. For a script mod check `"scripts": true`. |

### mod.json reasons

| Reason | Fix |
|---|---|
| `not valid JSON (...)` | A comma, quote or bracket is wrong. Use a JSON checker. |
| `must be one { ... } block` | The file must start with `{` and end with `}`. |
| `has no "id"` / `"id" may only have letters, digits, '-', '_' and '.'` | Fix the `id`. |
| `has no "game" (the game ID it is made for, e.g. "GXXE01")` | Add `"game": "GXXE01"`. |
| `"applies" must be "any_time", "restart" or "new_playthrough"` | Use one of those three. |
| `"scripts" must be true or false` | No quotes around `true`. |
| `made for a newer script interface than this port has (script_api N)` | Remove `script_api` or use `1`. |
| `made for a newer version of the port (mod.json format N)` | Update the port. |
| `"requires" has "X": write a version condition like "other-mod >= 1.2"` | Write `"other-mod"` or `"other-mod >= 1.2"`. |
| `"requires" / "after" / "conflicts" has "X", which isn't a mod id` | Use a mod's id (letters, digits, `-`, `_`, `.`). |
| `"X" must be a text in quotes` / `a whole number` / `a text or a list of texts` | The field has the wrong kind of value. |

## Needs, conflicts and order

| Message | Meaning |
|---|---|
| `The mod "A" needs the mod "B" in version >= 1.0, but ...` | A mod you named in `requires` is missing, too old, or switched off. |
| `The mod "A" needs the mod "B", which is not on (or can't be used). "A" is not used.` | Switch B on. |
| `The mods "A" and "B" can't be used together (A says so). "B", further down the list, is not used.` | One mod names the other in `conflicts`. |
| `The mod "A" should come after "B": move the line "A = on" below "B = on" in mods.ini` | Use the Mods page's move buttons, or edit `mods.ini`. The game never reorders the list by itself. |
| `Conflict: the mods "A" and "B" both replace <file>. "B" (further down the list) is used.` | Two mods replace the same file. Not an error. Order decides. |
| `Conflict: the mods A and B both change X of Y. B is used.` | Two data mods set the same field. Same thing. |
| `The mod A sets X of Y twice (...); the second is used.` | Two lines in one mod set one field. |
| `This playthrough was set up with the mod "X", which is not in the mods folder any more.` | A save wants a mod that is gone. Put it back. |

## Files in the mod

| Message | Meaning |
|---|---|
| `The mod "X" has "Y": disc file names only have English letters, digits and ...` | A file name has characters the disc cannot hold. Rename it. |
| `The mod "X" has "files/Y": the game's program and disc header can't be replaced.` | Those two files cannot be a mod. |
| `... has "fsys/Y": files there go in a folder named after the archive` | Use `fsys/<archive>.fsys/<entry>`. |
| `... changes A inside B, but the disc has no B; ignored.` | The archive name is wrong. |
| `B has entries #0 to #N, no #M` / `B has no entry called X` | The entry number or name does not exist. |
| `several entries of B are called X; name the one meant by its full name or number` | Use the number `#N`. |
| `B is not an FSYS archive, so X can't be replaced inside it` | You can only replace entries in FSYS archives. |
| `... is bigger than the original (N bytes, was M). If the game runs out of memory ...` | Info. A larger file may use more memory. |
| `The mod "X": #N.bin in B is not usable (it is not a valid script: ...). That file is switched off; the game's own is used.` | A story mod put something that is not a room script where the script goes (or the script is damaged). The game ignores that file and keeps the original script; the mod stays on. Make it again with `xdscript.py mod` and run `xdscript.py check <mod folder>`. See [story-scripts.md](story-scripts.md). |
| `The mod "X" has "data/Y": only .json files in "data" are read; ignored.` | A wrong file type in `data`. |

## Data mods

Each data problem names the mod and the file and line: `The mod "X", file line N: ...`. The thing named is left out,
and the rest of the mod still works.

| Message (shortened) | Meaning |
|---|---|
| `is not valid JSON (...); none of it is used` | A broken file. All of that file is skipped. |
| `must be one { ... } block with tables in it` | The file must be `{ "table": { ... } }`. |
| `names the table "X", which the game doesn't have` | Wrong table name. Use `--tables`. The message may suggest a name. |
| `T has no field "X"` | Wrong field. Use `--fields T`. |
| `X in T can't be changed by mods` | The field is an address or is filled in by the game. |
| `X has N values: write a list like [1, 2] (null for ...)` | The field is a list. |
| `X is one value, not a list` | Write a number, not a list. |
| `no species is called "X" on this disc` (move, item, ...) | Wrong name. Use `--names`. |
| `several ... numbers are called "X" (...)` | Use the number. |
| `T has entries #0 to #N, no #M` | The entry number is too high. |
| `the entries of T are named by number, like "#3"` | This table has no names. |
| `X is species #999, but the game's species numbers end at #N` | A number that does not exist. Also for moves and items. |
| `X must be a whole number from 0 to N` | The value does not fit the field. |
| `X can only be given as a number (no names for it on this disc)` | For example abilities have no names yet. Use the number. |
| `messages are named by number, like "#1234"` | In a `text` block. |
| `the game has no message #N that can be changed` | That message is not in a table mods can reach. |
| `its message texts can't be used (...)` / sizes named | The new texts of a table are longer than the old ones together. Shorten. |
| `the new row "X" needs "copy": the row it starts from` | Add `"copy"` to an added row. |
| `the new row "X" has the name of ...` | The name is taken. Choose another. |
| `mods can add at most 4096 rows to T` | Too many rows. |
| `adds rows to T, which can't get new rows` | Only species, moves and items can grow. |
| `a row can only copy rows above it` | Move the copied row above. |
| `changes the new row "X" before the file that adds it; put the "add" first` | File names are read in order. Name the adding file so it comes first (for example `00-add.json`). |
| `Data mods (...) are not supported for this game yet` / `The game's tables could not be found on this disc` | Your disc is not the supported USA disc. |

## Script mods

| Message | Meaning |
|---|---|
| `The mod "X" has a "scripts" folder but its mod.json does not say "scripts": true.` | Add `"scripts": true`. |
| `The mod "X" has scripts, but running scripts is switched off for it (Mods page); ...` | You (or whoever set up the list) unticked **Allow** under "Runs scripts (Luau)" on the launcher's Mods page, or wrote `, scripts off` in `mods.ini`. Tick it again to run the scripts. The rest of the mod is used either way. |
| `TYPE has no name "X"` (or `SPECIES`, `MOVE`, ...) | A misspelt name in a Luau script, or one the disc does not have. The disc's own words are short in places (`TYPE.ELECTR`). See [lua-names.md](lua-names.md); `find("type", "x")` returns `nil` instead of failing. |
| `TYPE is read-only` | A script tried to change a name table. They cannot be changed. |
| `The mod "X" says "scripts": true but has no scripts/*.luauu.` | The folder or file is missing or misnamed. |
| `has "scripts/Y": only .luau (or older .lua) files are read; ignored.` | A wrong file type in `scripts`. |
| `has more than 32 script files; its scripts are not used.` | Too many files. |
| `scripts/Y could not be read or is bigger than 256 KB` | A file is too big. |
| `[script X] not used: ...` (in the log) | The script failed to load. The reason follows (a syntax error, for example). |
| `Script mod X switched off: ...` (on screen, and in the log) | A handler failed. Read the reason below. |

Reasons inside a "switched off" message:

- `there is no hook called "X" (hooks: ...)`: wrong hook name. The real ones are listed.
- `ran too long (more than N instructions in one call)`: a loop that does too much, or never ends.
- `not enough memory (a mod's scripts may use 16 MB)`.
- `attempt to index a nil value (field 'X')`: you read a field the event does not have.
- `too many handlers for "X" (at most 32)`.
- `string.format: '%p' is not available to mod scripts`.
- an error in your own code, with the file and line.

After a switch-off the game carries on with its own numbers. Fix the script and restart.

## Saves and added rows

| Message | Meaning |
|---|---|
| `[playthrough] CONTINUE refused, the card is not read: ... the mod "X" is not on: this save has its Pokemon "Y" (number N) ...` | The save was made with a mod that is off now (or another mod changed the numbers). Switch the same mods on, in the same order, or play without the memory card this time. Nothing on the card is changed. |
| `[playthrough] labels: ... was number N in the save, is number M now` | Not a problem: another mod before yours moved a row's number. The port changed the numbers in the save to the new ones (a copy of the old file is in the save backups). See [labels.md](labels.md). |
| `the new row "X" has the label ..., which the row ... has already` / `"label" of the new row ...` | Two rows have one label, or a `"label"` breaks the rules (lower case letters, digits, `-`, `_`; whole label starts with your namespace). The row keeps its number and has no label until fixed. |
| `no row has the label "species:..."` | A label in a data file names a row that is not there (the mod that adds it is off or comes later, or the official name is spelled differently on this disc: `xd_data_mods <disc> --labels species`). |
| `Pokemon XD - this save needs its mods` / `This save holds Pokemon, moves or items that this game does not have` | The last guard. The game stops (exit code 6 in test runs). Start with the same mods on. |

## Mod updates, requirements and lists (the launcher's Mods page)

| What you see | Meaning | What to do |
|---|---|---|
| `The update file isn't signed ... Refused.` | The author did not sign the update file. | Install the new zip by hand if you trust the author. Authors: pack with `--key`. |
| `This update is signed with a DIFFERENT key than the one this mod was first installed with ...` | The author's key changed, the mod changed hands, or someone pretends to be the author. | Don't install it. Ask the author. To accept a new key on purpose, delete the mod's line in `mod_updates\mod_keys.json` (in the data folder), or install the new version by hand. |
| `The update file's signature is wrong` | The file was changed after signing, or signed by someone else. | Don't install it. |
| `The download's sha256 fingerprint doesn't match` | The zip is not the file the author published (damaged or swapped). | Try again later; if it keeps happening, tell the author. |
| `The safety scan or the checker found problems in the download` | The details follow (a program in the zip, a hostile file, a broken file). | Nothing was installed. Tell the author. |
| `The update file leaves out permissions the mod really needs` | The zip does more than its update file says. | Refused. Tell the author. |
| `The system's virus scanner refused the download` | Windows Security (or your antivirus) objected. | Don't install it. |
| `only https:// addresses are used for mod updates` | The update link is `http://`. | Authors: use an https address. |
| `Needs "x", which isn't installed.` / `Needs "x" in version >= 1.2; you have 1.0.` | A requirement is missing or too old. Turning the mod on offers to download it when the mod has a `links` entry. | Install or update it. |
| `A compatibility patch: it is only used when "x" is on too` | The mod has `only_with`. | Turn x on, or leave the patch off. |
| `This one and "B" both change the same table value (pokemon / #1 / hp). "B", further down the list, wins.` | A finer clash than a whole file. Not an error. | Move the mod you want to win further down, or turn one off. |
| `Some mods each want to come after the other` | A cycle in `after` / `before` / `requires`. | The order is yours: use the arrows. Mod authors: remove one of the two. |
| `That file can't be used as a mod list` | The file isn't a list the launcher saved. | Use "Save this list to a file..." from the launcher. |

Files the launcher keeps (in the data folder, `mod_updates\`): `modupdates.ini` (your yes / always / skipped versions; delete it to be
asked again), `mod_keys.json` (pinned author keys), `previous\<id>\` (the version kept for Roll back), `tmp\` (downloads in
progress; emptied at start).

## The mod checker and packer

`xd_mod_check` and `xd_mod_pack` ([sharing.md](sharing.md)) print one line per finding:
`PROBLEM  <file> line <n>: <what> [<code>]`. The code in brackets is stable; this table says what each means. Lines
in the **SAFETY** section are about what could harm your PC. Exit code `0` = nothing wrong, `1` = a problem, `2` =
warnings with `--strict`, `3` = the tool could not run.

**Safety** (any of these is a PROBLEM, and `xd_mod_pack` will not pack the mod):

| Code | Meaning | What to do |
|---|---|---|
| `program-file` | The file is a program or script type (`.exe .dll .bat .ps1 .sh .js .jar ...`) or a Mac application bundle. | Take it out. A mod never needs a program. |
| `program-content` | Whatever the file is called, its first bytes are a program (Windows, Linux, Mac, or a `#!` script). | Take it out, and find out how it got in. |
| `nested-archive` | A `.zip`, `.7z`, `.rar` ... is inside the mod (or inside the zip). | Put the files in the mod folder itself. |
| `symlink`, `special-file` | A link or a device in the mod folder. | Replace it with a real copy of the file, or remove it. |
| `bad-name`, `long-path` | A name Windows can't hold (`CON`, `NUL`, a trailing dot, `<>:"|?*`), or a path over 240 bytes. | Rename it. |
| `file-too-big`, `mod-too-big`, `too-many-files` | Over the limits: 512 MB a file, 1 GB and 5000 files a mod. | Make the mod smaller. |
| `json-too-big`, `json-too-deep`, `json-binary` | A JSON file over 8 MB, nested over 64 deep, or not text. | Split or simplify it. |
| `png-too-big`, `dds-too-big`, `png-bomb` | A picture over 8192 pixels on a side, or picture data that unpacks to more than its header says. | Make the picture smaller, or save it again. |
| `lua-bytecode` | A compiled script file. | Only text scripts are read. Ship the source. |
| `zip-bad-path`, `zip-bad-name` | The zip has a path with `..`, an absolute path, a drive letter, a network path, or a Windows device name. | Do not use the zip. Ask for a new one from `xd_mod_pack`. |
| `zip-symlink`, `zip-special`, `zip-setuid`, `zip-encrypted` | A link, a device file, special permission bits, or a password in the zip. | Same. |
| `zip-case-duplicate`, `zip-name-mismatch`, `zip-damaged`, `zip-method`, `zip-unreadable` | The zip is damaged, or two entries have one name on Windows and Mac. | Same. |
| `zip-too-many-files`, `zip-too-big`, `zip-file-too-big`, `zip-bomb` | The zip is bigger when unpacked than the limits allow, or unpacks to far more than it weighs. | Same. |
| `zip-extract` | Unpacking stopped (an entry would land outside the folder, is larger than it said, or has a wrong checksum). | Same. |

**The mod**:

| Code | Meaning | What to do |
|---|---|---|
| `no-manifest` | No `mod.json` directly in the folder (it may be one folder further in; the message says). | Move the files up. |
| `manifest-json`, `manifest-shape`, `manifest-type`, `manifest-missing`, `manifest-id`, `manifest-applies`, `manifest-requires`, `manifest-conflicts`, `manifest-after`, `manifest-engine` | `mod.json` is wrong in the way the message says. The same reasons as the "mod.json reasons" table above, but all of them at once, each with its line. | Fix each. |
| `manifest-self`, `manifest-contradiction` | The mod requires, conflicts with or comes after itself, or the same id is both required and in conflict. | Remove it. |
| `manifest-unknown-field` | A field the port does not know (ignored). The message says what you probably meant. | Fix the spelling or remove it. |
| `manifest-game` | The disc ID is not one the port knows for this game. | Use `GXXE01` (USA), `GXXP01` or `GXXJ01`. |
| `version-format` | `version` is not dotted numbers. | Write `1.2.0`. Other forms compare as plain text. |
| `manifest-relation` | `before`, `works_with`, `recommends` or `only_with` names something that isn't a mod id, or the mod itself. | Fix the id. |
| `manifest-links`, `manifest-links-not-https` | `links` is not `{"id": "https://..."}`, or an address is not https. | Fix it; the launcher refuses non-https links. |
| `manifest-author-key` | `author_key` is not 64 lower case hex digits. | Paste your public key (`xd_mod_pack --make-key` prints it). |
| `updates-no-version`, `updates-not-https`, `updates-url` | `updates` is set without `version`, is not https, or is not a web address. | See "Updates" in [sharing.md](sharing.md). |
| `applies-claim` | `applies` says `any_time` but the mod changes game data or runs scripts. The port uses `new_playthrough`. | Say what the mod really needs. |
| `requires-missing`, `requires-version`, `requires-unchecked`, `conflicts-present` | A needed mod is not in the mods folder, is too old, was not checked (no `--mods-folder`), or a conflicting mod is present. | Install or update it, or change `requires`. |
| `case-duplicate` | Two files differ only by letter case. Windows and Mac can't keep both. | Rename one. |
| `unknown-kind` | The port does not read this kind of file in this folder; it is ignored. | Move it or remove it. |
| `junk` | An editor or system leftover (note only). The packer leaves it out. | None. |
| `empty-mod`, `scripts-none`, `scripts-not-enabled` | The mod changes nothing, says `"scripts": true` with no scripts, or has scripts but no `"scripts": true`. | Add the content or fix `mod.json`. |
| `data-json` | A data file is not valid JSON; the message has the line. | Fix the JSON. |
| `data-shape`, `data-table`, `data-entry`, `data-value`, `data-number`, `data-add`, `data-format` | A data file is the wrong shape: a table must be a `{ }` block of entries, an entry a `{ }` block of fields, a field a whole number from 0 up, a name or a list. | Fix it at the line shown. See [change-data.md](change-data.md). |
| `data-text`, `data-text-key`, `data-text-long` | A message text is not a text, has a control code the game can't write (`{` with no `}`, `{07}` without its argument byte), or a key is not `"#number"`. | See "Message text" in [change-data.md](change-data.md). |
| `lua-syntax` | A script does not compile; the message has the compiler's own words and the line. Lua 5.4-only syntax (`goto`, `<const>`, `& \| ~ << >>`) lands here. | Fix the line; see the table in [luau.md](luau.md). |
| `lua-old-name`, `flat-layout` | Notes only. A script still has the older `.lua` name (it runs, as Luau), or the mod uses the older flat layout (no `games/` folder; it still loads, for XD only: not on XG unless `mod.json` names `"xg"`). | Rename to `.luau` / move a game's own files into `games/xd/` when you like. See [luau.md](luau.md), [two-games.md](two-games.md). |
| `lua-unknown-hook` | `on("name", ...)` names a hook the game does not have. The message lists them. | Use a real hook. |
| `lua-sandbox` | The script uses `os`, `io`, `debug`, `coroutine`, `require`, `pcall`, `getfenv` ... which the sandbox leaves out. It will fail when it reaches that line. | Remove it. |
| `lua-too-big`, `lua-too-many` | A script over 256 KB, or more than 32 scripts: the game switches the mod's scripts off. | Split or shrink them. |
| `script-invalid` | A story script is not laid out as the game reads scripts; the game would keep its own. | Make it again with `xdscript.py mod`. |
| `png-damaged`, `dds-damaged` | A picture has wrong checksums, is cut off, or too short for its header. | Save it again. |
| `json-invalid` | A JSON file outside `data` does not parse. | Fix the JSON. |
| `game-loader` | With `--disc`: a message the game's own loader gave for this mod. Read it as in the tables above. | As the message says. |

**The packer** says what it refused and why, under **REFUSED**. Codes: `game-archive` (a disc image or disc-format
file), `same-as-disc` (a file under `files/` or `fsys/` equal to the disc's own), `texture-dump` (a picture named like a
dumped texture and the same size as the game's own), `known-hash` (on your `--refuse-hashes` list), `save`, `log`,
`game-data` (a folder the port fills, a sound chip dump). **Nothing is written** while any of these (or a PROBLEM) is
left. If your same-size texture is really your own, pack with `--allow-same-size-textures`.

The checker lowers the risk but cannot prove a mod is harmless. Only install mods from people you trust.

## People, zones, chests, music and doors in rooms

Messages of `objects/<room>.json` ([story-objects.md](story-objects.md)). Each names the mod and the file; the rest of the file still loads.

| Message (part) | Meaning |
|---|---|
| `no room of that name` | The file name is neither a room your mod adds nor the archive name of a disc room (`xd_data_mods <disc> --names floor`). |
| `the room's script has no function "x"` | A `talk`, `move` or `run` names a function that is not in the room's script. The person or zone is left out. |
| `needs a "name"` / `needs a "model"` / `needs a "flag"` / `is not a field (a typo?)` | A mistake in the file; the line number is given. The person, chest or zone is left out (a stray field is only ignored). |
| `the mod has no story flag "x"` | A chest, door or the music names a flag your mod does not declare. `xdscript.py mod --objects` declares them; by hand, add them under `add: flags`. |
| `is not an item of the game` | The item name is not one the game writes (use `xd_data_mods <disc> --names item`) or its number. |
| `another mod already adds people to this room` | A room takes people from one mod only. |
| `a room already has N people; a mod's people start at 128` | The disc room has too many people for the numbering (not expected). |
| `the room has no readable script, so its people can not be listed` | People need the room's script (the `person @name` lines): give the room's `.xds` to `mod`. |

## Other problems

- **A mod made no change.** Is it switched on? Is it a `new_playthrough` mod and the save an old one? Did you restart?
- **The list file says `mods.ini line N: expected "<mod id> = on" or "= off"`.** A hand edit has a wrong line. Use
  `id = on`.
- **Could not write the mod list.** The data folder is read only. Check its permissions.
- **Texture packs.** See [textures.md](textures.md). A broken picture gives a line `texture_replacement:` in the log.

If you ask for help, say which message you got, and include your `mod.json`. Never attach game files.

## A mod is "Made for XD, not XG"

On the Pokémon XG disc a mod that does not name `"xg"` in its `mod.json` `"game"` list is shown with that message and is not loaded: XG renumbers moves and changes
data, so an XD mod can break there. If you made the mod and know it is safe on XG, add `"xg"` (`"game": ["xd", "xg"]`); if XG needs different files, put them in `games/xg/`.
The checker notes `xd-only`, `xg-folder` and `xg-xd-data` say the same before you share the mod. Tags in every list: XD, XG, XD + XG.

## See also

- [sharing.md](sharing.md): the checker and the packer
- [change-data.md](change-data.md): tools you can run
