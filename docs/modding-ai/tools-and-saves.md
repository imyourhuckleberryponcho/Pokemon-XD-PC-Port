# Tools, saves and reload

The checker and packer, signing, how saves and mods fit together, and reloading while the game runs. Back to the [front page](README.md).

## Checking and packing

Command-line tools built with the port (the build's `bin` folder). They need no game and no disc.
Full text: [sharing.md](../modding/sharing.md). Messages: [troubleshooting.md](../modding/troubleshooting.md).

| Command | What it does |
|---|---|
| `xd_mod_check <folder>` | Lists every problem with file and line: `mod.json`, names, `data/*.json` shape, `scripts/*.luau` (compiled in the sandbox, never run), story files, `version`/`updates`, then a SAFETY section and the mod's permissions |
| `xd_mod_check <mod.zip>` | The same checks the launcher will run on a download: zip scan (paths, links, bombs), unpack into a scratch folder, then the folder checks |
| `xd_mod_check ... --disc <disc>` | Also runs the game's own loader on the mod (names exist, numbers in range) |
| `xd_mod_check ... --mods-folder <dir>` | Checks `requires` / `conflicts` against the mods there |
| `xd_mod_check ... --strict --json` | Warnings give exit 2; findings as JSON |
| `xd_mod_pack <folder> [--out d] [--disc <disc>] [--url-base https://...] [--notes "..."]` | Checks, refuses game data and anything unsafe, leaves out junk, writes `<id>-<version>.zip` (one top folder, reproducible bytes) and, when `mod.json` has `updates`, `<id>.update.json` |

The launcher's Mods page runs the same library in-process (0.7.0): per mod **Check this mod** (verdict, then safety / permissions / problems /
warnings / notes with file and line, the XD / XG tags, and the disc pass's findings (`game-loader`) or the plain line that no disc was
used) and **Pack for sharing...** (save window, then the packer's result; no signing there). The first switch-on of a mod in a launcher
session is checked first; the window opens only for a problem, a warning or a safety finding (default answer: Keep it off). Controller
buttons work everywhere (focus rows scroll the report; B closes). Launcher script steps: `modcheck:<id>`, `modpack:<id>=<zip>`, `modon:<id>`,
`modtools:on|close`, `wait:modtools` (launcher.h).

Exit codes: `0` fine, `1` a PROBLEM (the packer then writes nothing), `2` warnings with `--strict`, `3` could not run.
Finding codes are stable (`lua-syntax`, `data-number`, `program-file` ...); the list with meanings is in troubleshooting.md.

**Safety rules the checker enforces** (a PROBLEM): no programs or scripts the PC could run, by name or by content (PE, ELF,
Mach-O, `#!`); no archives inside a mod; no symbolic links; JSON at most 8 MB and 64 deep; PNG/DDS headers and sizes sane
(at most 8192 on a side, data not larger than the header says); Luau text only; at most 5000 files and 1 GB; zips without
`..`, absolute or drive paths, device names, case duplicates, links, and with limited unpacked size and ratio. Unknown file
kinds are a warning. Known folders: `data/*.json`, `scripts/*.luau`, `textures/*.png|dds`, `files/`, `fsys/`, `rooms/`,
`objects/*.json`, `*.xds`, readmes.

**Permissions** it reports (codes): `scripts`, `replaces-files`, `changes-tables`, `text-changes`, `adds-rows`,
`adds-story`, `textures`, `update-link`. The launcher will show them before install and ask again when an update adds one.

**Game data the packer refuses**: disc images and disc-format files (also by header, whatever the name); with `--disc`, any
`files/` or `fsys/` file byte-equal to the disc's; files whose SHA-256 is in `--refuse-hashes`; dumped textures
(`tex1_<w>x<h>_<hash>_*.png|dds` the same size as the game's own; `--allow-same-size-textures` overrides); saves, logs,
recordings, the port's own `settings.ini`/`mods.ini`, folders it fills. There is no override for the safety refusals.

**Update file** (`<id>.update.json`, next to the zip; the author uploads both): `{"format": 1, "id", "version", "url" (https,
where the zip will be; `--url-base`), "sha256" and "size" (of the zip), "notes" (`--notes` or the mod's CHANGES file), "games",
"permissions" (codes)}`. `mod.json` `"updates"` is the address of that file. Versions are dotted numbers and compare number by number.

**Signing (0.6.0).** `xd_mod_pack --make-key <file>` makes an Ed25519 key file (`private:` / `public:` lines; keep it outside the mod and
any repository, back it up, never share it). `xd_mod_pack <mod> --key <file> --url-base https://... ` writes the update file
signed: `{"signature", "public_key", "manifest": {...the fields above...}}`; the signature covers `mod-update-1\n` + the exact bytes
of the manifest block. The launcher installs an update only if it is signed, the signature is valid, and the key equals the one it
pinned the first time (trust on first use; also `author_key` in mod.json when set). Unsigned update files and other keys are
refused with a plain message; a mod without signing still works when unpacked by hand but cannot update itself. The launcher's
flow: ask first (the player says yes; "always check" may be remembered), https only, show version / notes / permissions (an
added permission is named), download to a temp folder, check size + sha256, Windows' antivirus (IAttachmentExecute) where
available, the full safety scan + checker, a second yes, install with the old version kept (Roll back), saves started with the
mod protected (the update is held back unless the player chooses). Never tell a player a mod or a signature is "safe": the checks
lower the risk, they cannot prove a mod harmless.

Changing the signing key (DRAFT, [sharing.md](../modding/sharing.md) "Changing your signing key"): planned = `xd_mod_pack --rotate-key <old key> <new key>`
(makes the new key file if missing; writes `change: <old>:<new>:<sig>` lines into it, the old key signs `mod-key-change-1\n<old>\n<new>`), then
`--key <new key>` puts `"key_changes": [{"old_key","new_key","signature"}]` next to the signature; launchers that pinned the old key follow the chain by
themselves (at most 8 steps). Lost key = no statement: the launcher shows both fingerprints and installs nothing until the player presses the explicit
"I checked this fingerprint with the author" button. Never suggest a way to skip that question.

Always say: the checker lowers the risk but cannot prove a mod is harmless (the game's own file readers were never built for
hostile files). Mods should come from people the player trusts.

## Save guard and playthroughs

- A playthrough's setup file lists the rows its mods added, with the number AND the stable label ([labels.md](../modding/labels.md), DRAFT):
  `{"mod","table","name","id","label"}`. At start the port matches the rows by label. Same numbers: nothing. A label with another
  number (another mod before it, another order): the numbers in the save are changed to the new ones (party, boxes, day care,
  purify chambers, Shadow list, bag, PC items; both slots; a backup of the file goes to the save backups first). A label that is not
  there: the save is refused (nothing read, nothing written). A setup file from before labels is matched by mod + table + name.
- The last guard: a save with a species, move or item number past the tables' ends makes the game stop with "this save
  needs its mods".
- `new_playthrough` mods are chosen on the New Game setup screen and stay with that save. `restart` and `any_time` mods can be
  switched later.

## Save extension file (0.7.0): extra data next to the save

Full page: [save-extension.md](../modding/save-extension.md). Facts an AI needs:
- The GameCube save layout NEVER changes. Data that does not fit it goes in `<card root>/playthroughs/Card A/<key>.ext`, next to the setup
  file `<key>.json` (same key). Binary, versioned (`"XDEX"`, format 1), named sections, a CRC-32, and a 64-bit fingerprint of the exact save
  file it was written for (`engine/saveext/include/saveext.h`). No file is made for a plain game or a playthrough with nothing extra.
- Written in two steps with the card write (`<key>.ext.new`, then renamed after the card took the write); read when the game loads
  (a read of a whole slot, 0x28000 bytes). A file whose fingerprint does not match the save is stale and not used. A section marked
  **required** (its names are also in the setup file as `"ext_required"`) that is missing, stale, damaged or too new makes the game refuse
  the save before it reads anything (log `[save guard] STOP`, exit code 6); an optional section is dropped with a log line.
- Backups: the save backups hold the extension file in the same folder as the save (`Backup::companions`); restoring a backup puts both
  back. Copying a save slot copies the whole `playthroughs` folder.
- Built section: `memo` (the Strategy Memo's entries past the 500th, 10 bytes each). The memo's own four questions (count, species at an
  index, the two random numbers of a species) are renamed in `memo.cpp` and answered by `games/xd/src/memo_overflow.cpp` once the memo is full.
  Dev action `memofill <n>` puts species 1..n into the memo (tests). Registering another section: `xd::saveext::Register` (`xd_save_ext.h`).
- The number of rows a mod can add per table is the decomp's `PC_SPARE_*` (`include/game/pcspare.h`), read at configure time into
  `xd_spare.h`; raise it there and the port follows. Not built: Shadow Pokémon past 127, more TMs than the 64-slot pocket, more tutors than 12,
  more bag slots, abilities past 255: see "Bigger lists" in [add-pokemon-moves-items.md](../modding/add-pokemon-moves-items.md). Do not promise these.

## Reload while playing (0.7.0, developers)

Setting `system.mods_hot_reload` ("Reload mods while playing (developers)", off in every preset). With it on, **F6**, **Reload mods** on the
in-game MODS page, or a change to a file in the mods folder (watched every ~2.5 s, off the game's thread) reloads, at a quiet moment
(`fightIsFight()==0` and a map loaded and running; otherwise the request waits, log `waiting for a quiet moment`):

| Kind of change | Reloaded live? |
|---|---|
| `scripts/*.luau` of a mod that is on (and not switched off by the player's Allow box) | **Yes.** The script host is built again (names, hooks); a script's own variables start again. A script that fails to load: `[script] <id>: not used: ...` and the notice says so |
| `textures/` of a mod and texture packs | **Yes**, when texture packs are in use in this run (not Original, not `XD_HD_TEXTURES=0`): the registered replacements are swapped; a picture shows from the next time it is drawn |
| data values and text (`data/*.json`), replaced files (`files/`, `fsys/`, rooms, objects), new rows, which mods are on, their order | **No, restart.** The game reads those tables once at start-up. The reload notices the change (a fingerprint of what the start-up load made) and shows `Restart the game to use changes to ...`; it applies nothing of it |

Safety: ignored in Original, with `XD_REQUIRE_ORIGINAL=1`, in XG mode, and in every test run (replays, scripted runs: `xd_test_run.h`) unless a
test asks (`XD_MODS_RELOAD_TEST=1`, `XD_MODS_RELOAD_AT=<frame>[,<frame>]` reloads at those game frames). It never writes a game variable and never
writes `mods.ini` (new mods stay off). A short notice shows in the game (`PortVideo_Notice`); every reload writes a recording note
`MODS frame N field M reload <why> scripts=yes|no textures=yes|no restart=needed|no` (read back as a note: a replay does not reload, so a
recording with a `MODS` line is a run whose mods changed mid-way) and the log line `[mods reload] ...` (`mods_reload.cpp`, `xd_mods_reload.h`).
