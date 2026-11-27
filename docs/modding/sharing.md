# Sharing your mod

**What this page covers:** checking a mod, packing it as a zip that other players can drop into their mods folder, signing and updating it, and
what must never be in a mod.

## The one rule

**A mod must never contain anything copied from the game.** Every player supplies their own disc. A mod holds only your
own work and small instructions ("make this number 90"). That is why data mods and scripts are ideal: they are just
numbers and text.

Do **not** include:

- the disc image, the main program file, or any file extracted from the disc;
- the game's pictures, textures (including the port's texture dump), sounds, music, models, fonts or movies, even if you
  changed them a little;
- extracted or decompiled **story scripts**, or message text banks made from the game's own (story mods are the hard
  case: see below);
- your save files or memory card images;
- the language pack or any "extra assets" the port made on your PC;
- the outputs of the randomizer tool (they hold game data);
- notes with game text copied from the disc;
- **programs or scripts the PC could run** (see "Safety" below): `.exe`, `.dll`, `.bat`, `.ps1`, `.sh`, `.js`, `.jar`, Mac
  apps and the like. A mod never needs one.

Do include: `mod.json`, your `data/*.json`, your `scripts/*.luau`, your own new pictures, and a short readme.

## Steps

1. Make the folder. Check that `mod.json` is directly in it:

   ```
   my-mod/
     mod.json
     README.txt          what it does, who made it, what version, what it needs
     data/
       changes.json
     scripts/
       damage.luau
   ```

2. Fill in `mod.json` properly:

   ```json
   {
     "id": "my-mod",
     "name": "My mod",
     "version": "1.0.0",
     "author": "Your name",
     "description": "One sentence: what the mod does.",
     "game": "GXXE01",
     "applies": "new_playthrough",
     "requires": ["other-mod >= 1.0"],
     "conflicts": ["other-mod"],
     "after": ["other-mod"],
     "priority": 0,
     "updates": "https://example.com/my-mod/my-mod.update.json"
   }
   ```

   Only `id` and `game` are required. `requires`, `conflicts`, `after`, `priority` and `updates` are optional. Remove
   the lines you do not need.

   - Choose `applies` honestly: `any_time` (only looks and sound), `restart`, or `new_playthrough` (anything that goes
     in a save or changes the rules). If you leave it out, it is `new_playthrough`.
   - Raise `version` each time you change the mod. Versions compare number by number: 1.10 is newer than 1.9.

3. Test on a clean setup: only your mod on, a New Game. See the test steps on the page for what you used.
4. **Check it, then pack it** with the two tools below. `xd_mod_check` lists everything wrong. `xd_mod_pack` makes the
   zip: it holds the folder, so the zip has `my-mod/mod.json`. Players unpack it into the mods folder. (The launcher
   will do the unpacking and checking itself; see "Updates".)
5. Write what the player must do: which mods to switch on, whether to start a New Game, and the order if it matters.

## Check and pack your mod

These are small command-line programs that are built with the port (in the build's `bin` folder, like the other tools
in [change-data.md](change-data.md#tools-you-can-run)). They work **without the game and without a disc**. The launcher has
buttons for them too (next section).

### In the launcher (Mods page)

Under every mod in the list there are two buttons. They run the same checks as the programs below, inside the launcher, and show the
result in plain words. You can use them with a mouse or only with a controller (D-pad to the button, **A** to press, **B** to close).

- **Check this mod.** A window with a verdict ("Nothing wrong found", or how many problems and warnings), then the findings: safety first,
  then what the mod can do (its permissions), then problems, warnings and notes, each with the file and line. The tags `XD`, `XG`, `XD + XG`
  show which game(s) it is made for. If you chose your disc on the Disc page, the game's own loader also reads the mod (table and field
  names, number ranges), and the window says what it found; without a disc it says that part was skipped. **Copy the report** puts the text on
  the clipboard, for a bug report or for the mod's author.
- **Pack for sharing...** Asks where to save, then makes the zip (and the update file, when the mod has `updates`) the way `xd_mod_pack`
  does: it refuses game data and anything unsafe, and the window says what was left out and why. It never writes a zip with a problem in it.
  Signing an update file with your own key stays on the command line (`xd_mod_pack --key`).
- **Before a mod is switched on.** The first time you tick **On** for a mod in a launcher session, the launcher checks it first. If nothing
  needs reading it goes on at once (one line says it was checked). If there is a problem, a warning or a safety finding, a window
  opens and you choose: **Keep it off** (the safe answer, already selected; **B** does the same) or **Switch it on anyway**. This is also
  the place to look at a mod you just downloaded by hand.

A check lowers the risk; it cannot prove that a mod is harmless. Only use mods from people you trust.

```
xd_mod_check my-mod                        check a folder
xd_mod_check my-mod.zip                    check a zip the way the launcher will check a download
xd_mod_check my-mod --disc <disc image>    deeper: also run the game's own loader on it
xd_mod_pack  my-mod                        check, then make my-mod-1.0.0.zip next to the folder
```

### What `xd_mod_check` reads

It prints every problem it finds, not just the first, each with the file and the line. Three levels: **PROBLEM** (the
game would not use that part, or it is not safe), **warning** (odd, but it works), **note** (news).

- `mod.json`: valid JSON, every field the right kind, the id, `game`, `requires` and `conflicts` (a mod requiring itself,
  or both requiring and conflicting with the same mod), `applies` against what the mod really changes, unknown fields
  (with "did you mean ...?"), `version` and `updates`.
- File names: only letters and signs the disc and Windows can hold, no two names that differ only by letter case,
  nothing that climbs out of the folder.
- `data/*.json`: valid JSON, the right shape (tables, entries, fields; `text` and `add` blocks), whole numbers in range,
  message texts the game can hold (a `{` with no `}`, a control code with the wrong number of bytes).
- `scripts/*.luau`: they compile in the game's sandbox (the same compiler the game uses; the script is **never run**),
  at most 32 files of 256 KB, only hooks the game really has, and a warning for names the sandbox does not have
  (`os`, `io`, `require` ...).
- Story files (`rooms/`, `fsys/`): a script is laid out as the game reads scripts.
- With `--mods-folder <folder>`: whether the mods you `require` are there, in the right version.
- With `--disc`: the game's own loader runs on the mod, so table and field names exist and numbers are in range. What
  it says is the game's own message. Without a disc the checker says so and does not guess.

Exit codes, for scripts and CI: `0` nothing wrong (notes allowed), `1` a problem, `2` only warnings and you gave
`--strict`, `3` the tool could not run. `--json` prints the findings as JSON.

### Safety

The report has its own **SAFETY** section. It also lists what the mod **can do** in plain words, for example "Runs Luau
scripts", "Replaces files inside the game's archives", "Adds new entries to the game's tables", "Adds or changes story
rooms", "Has an update link". The launcher will show these before you install a mod, and ask again when an update adds a
new one.

What is refused (a PROBLEM, and `xd_mod_pack` will not pack it):

- **Programs and scripts the PC could run**: by name (`.exe .dll .so .dylib .bat .cmd .ps1 .vbs .js .jar .sh .app
  .msi .scr .lnk .com` and similar, Mac application bundles) **and by content**: a file that starts like a Windows
  program, a Linux program, a Mac program or a `#!` script is refused whatever it is called. A picture that is
  really a program is caught.
- **Archives inside a mod** (a `.zip`, `.7z`, `.rar` ... in the folder or inside a zip): nobody can look inside them.
- **Links** (symbolic links, shortcuts) and special files.
- **Too big or damaged files**: JSON over 8 MB or nested more than 64 deep; PNG or DDS pictures with wrong headers,
  wrong checksums, more than 8192 pixels on a side, or picture data that unpacks to more than the header says;
  compiled code (only text scripts are read); more than 5000 files or 1 GB.
- **Hostile zip files** (checked on a zip, and by the packer on its own output): paths with `..`, absolute paths, drive
  letters or network paths, Windows device names (`CON`, `NUL` ...), names that differ only by case, symbolic links,
  password protected entries, and zip bombs (a huge unpacked size or compression ratio, or an entry that unpacks to more
  than it says).
- Only known kinds of files per folder are quiet: `mod.json`, `data/*.json`, `scripts/*.luau`, `*.xds` story text,
  `objects/*.json`, `texts/*.json` (words for rooms with no text bank), PNG and DDS pictures in `textures`, game-format files under `files`, `fsys` and `rooms`, and
  plain text readmes. Anything else is a warning ("the port does not read this kind of file here").

> **Be honest about what this is.** The checker lowers the risk. It cannot prove that a mod is harmless. The game's own
> readers for models, scripts and other game files were never built to face files made to hurt, so a cleverly broken
> file might still crash the game, or worse. **Only install mods from people you trust.**

### What `xd_mod_pack` does

1. Runs the checker first. A PROBLEM (any safety problem included) stops it. With `--strict`, a warning stops it too.
2. **Refuses game data** and says what and why. It never writes a zip when it finds some:
   - disc images and disc-format files (`.iso .gcm .ciso .wbfs .rvz .fsys .dol .rel ...`) and any file that *starts* like
     one, whatever its name;
   - with `--disc <your disc>`: any file under `files/` or `fsys/` that is a **byte for byte copy of the disc's own
     file** or archive entry ("share only what you changed");
   - `--refuse-hashes <file>`: files whose SHA-256 is on your list of known game files (one per line);
   - textures dumped from the game: a picture named like a dumped texture (`tex1_64x64_<hash>_5.png`) that is the **same
     size as the game's own**. A replacement is bigger. If it is your own redrawing at the same size, add
     `--allow-same-size-textures`;
   - saves and memory cards (`.gci .sav .raw`), logs and crash dumps, recordings of play sessions, the port's own
     `settings.ini` / `mods.ini`, and folders the port fills (`texture_dump`, `Card A`, `reports`, `pbr_models` ...).
3. Leaves out junk (editor backups `x~ x.bak x.swp`, `.DS_Store`, `Thumbs.db`, `.git`, `__MACOSX`, `desktop.ini`) and lists
   what it left out.
4. Makes `<id>-<version>.zip` (next to the folder, or in `--out`). The zip has one top folder named after the id. Dates
   are fixed and names sorted, so packing the same mod twice gives the same bytes and the same SHA-256.
5. Scans its own zip with the same safety scan the launcher will use on a download.

```
xd_mod_pack my-mod --out dist --url-base https://example.com/releases/v1.0.0/
```

## Updates

Players should get a new version of your mod without hunting for it. These optional `mod.json` fields:

- `"version"`: dotted numbers, like `1.2.0`. Compared number by number (1.10 is newer than 1.9). Raise it every release.
- `"updates"`: an **https** address of a small JSON file *you* host (for example in a GitHub release). Setting
  `updates` without `version` is a warning (nobody can tell if it is newer), and so is an address that is not
  `https://`. The game itself never contacts it; only the launcher does, and only when the player says yes.
- `"author_key"` (optional): your public signing key (64 hex digits, below). Players' launchers pin it when they first see
  the mod.

### Signing your updates (so only you can update your mod)

The launcher only installs an update that is **signed with your key** and is the same key the mod was first installed with.
Without that, anyone who got hold of your download address (or your web hosting) could push a changed mod to everyone who
has it. The key is an Ed25519 key, the same kind the port's own updater uses ([../UPDATER.md](../UPDATER.md)).

```
xd_mod_pack --make-key my-mod-author.key             # once: a key file. KEEP IT SECRET and make a backup copy
xd_mod_pack my-mod --url-base https://example.com/releases/v1.2.0/ --notes "What is new" --key my-mod-author.key
```

- The key file holds a `private:` line (never share it) and a `public:` line. Keep it **outside the mod folder and outside any
  repository**. The packer refuses to sign when the key file is inside the mod's folder, and a zip never holds it.
- If you lose the key, launchers that pinned it can no longer tell your next update from someone else's. They do **not** accept it
  by themselves: they show the new key's fingerprint and ask the player to confirm (see "Changing your signing key" below). If
  someone else gets your key, they can publish "your" updates. Treat it like a password.
- `--key` signs `<id>.update.json`. If `mod.json` has `"author_key"`, it must be your key's public half or the packer stops.
- Keep one key for a mod. When you do have to change it, **announce it with the old key first** (next section), so players are not asked.
- A mod without signing still works when a player unpacks it by hand. It just **can't update itself**: the launcher says the
  update file isn't signed and refuses it.

### Changing your signing key

> Early version: the format can still change.

There are two cases, and the launcher treats them very differently.

**1. A planned change** (you still have the old key: a better key, a new computer, a key you fear was seen). The old key signs
the sentence *"the new key is X"*. A launcher that trusts the old key checks that signature and accepts the new key **by itself**.

```
xd_mod_pack --rotate-key my-mod-author.key my-mod-author-2.key
xd_mod_pack my-mod --url-base https://example.com/releases/v1.3.0/ --key my-mod-author-2.key
```

- `--rotate-key <old> <new>` makes the new key file if it is missing, signs "the new key is X" with the old key, and writes the
  statement (a line `change: <old public>:<new public>:<signature>`) into the **new** key file. It also copies the statements the
  old key file already held, so a launcher that is several keys behind can follow the whole chain (at most 8 steps). The old key
  file is never changed. Running it again does not write the same statement twice.
- From then on pack with the new key. `xd_mod_pack --key <new key file>` puts the statements into the update file as a `key_changes`
  list next to the signature. Put the new public key in `mod.json`'s `author_key`.
- Release at least one update signed with the new key (carrying the statement) **before** you stop using the old one, and keep
  the old key file until your players have updated once. A player who has not updated for a long time follows the chain.
- Each statement is checked on its own: `{"old_key", "new_key", "signature"}`, the signature being the old key's over
  `mod-key-change-1` + a line break + the old public key + a line break + the new public key. A statement does not name a mod:
  it speaks for the key (every mod signed with it). Nobody else can sign one for your key.

The launcher then says, in the update's details, *"The author changed their signing key as planned: the old key a1b2-... signed
'the new key is c3d4-...'. This update is signed with the new key, which will be kept from now on."* and keeps the new key after the install.

**2. A lost key** (the old key is gone: nothing can announce the change). The launcher cannot know whether the author lost the key
or someone else took over the mod. So it never switches silently:

- the update is shown with both fingerprints (*"Key you had: a1b2-... New key: c3d4-..."*) and a plain warning;
- **nothing is downloaded or installed** until the player presses *"I checked this fingerprint with the author: trust the new key"*.
  The button is the only way on; "Skip this version" is the other;
- only after that explicit yes is the update downloaded, checked as any other, installed, and the new key kept.

What you can do as the author after losing the key: make a new key, pack with it, and tell your players the **new fingerprint**
somewhere they can check (your page, the mod's description). The launcher shows the fingerprint to compare with.

What the launcher does not do: it never accepts a different key because the file says so, never forgets the old key when the
player says no, and never installs anything signed with a key it has not been told to trust. An update signed with a key nobody
announced and nobody confirmed stays unused.

Fingerprints are the first 16 hex digits of the SHA-256 of the public key, in four groups (`a1b2-c3d4-e5f6-7890`).

### The update file

`xd_mod_pack` writes `<id>.update.json` next to the zip when `updates` is set. Signed, it looks like this (the signature covers
`mod-update-1` + a line break + the exact bytes of the `manifest` block):

```json
{
  "signature": "3f9a...(128 hex digits)",
  "public_key": "35ea557ea69fa5ed...(64 hex digits)",
  "manifest": {
    "format": 1,
    "id": "my-mod",
    "version": "1.2.0",
    "url": "https://example.com/releases/v1.2.0/my-mod-1.2.0.zip",
    "sha256": "ba7816bf...",
    "size": 48213,
    "notes": "1.2.0: made the numbers rounder.",
    "games": ["GXXE01"],
    "permissions": ["changes-tables", "update-link"]
  }
}
```

- `url` is where the zip will be once you upload it. Pack with `--url-base <https folder>` and it is filled in. It must be `https://`.
- `sha256` and `size` are the zip's own, so the launcher can tell that a download was not changed on the way. They are inside the
  signed block, so changing even one letter of the file makes the signature wrong.
- `notes` come from `--notes "..."`, or else from a `CHANGES.md` / `CHANGES.txt` / `CHANGELOG.md` in your mod folder.
- `permissions` are the codes the checker found (`scripts`, `replaces-files`, `changes-tables`, `text-changes`,
  `adds-rows`, `adds-story`, `textures`, `update-link`). The launcher shows them in plain words. When a new version **adds**
  one the old one did not have, the launcher asks again and names it. If the mod really needs more than the file says,
  the launcher refuses the update.

After a key change the file also has a `key_changes` list next to `signature` and `public_key` (not inside `manifest`):

```json
"key_changes": [
  {"old_key": "<64 hex>", "new_key": "<64 hex>", "signature": "<128 hex>"}
]
```

Upload the zip and the `.update.json` file, and point `updates` at the update file's address.

### What the launcher does with it (Mods page, "Check for mod updates")

1. **It asks first.** The first time, the launcher says what it will contact (the hosts of your mods' update links) and
   waits for a yes. "Yes, check now", "Yes, and always check when the launcher starts" or "No". Nothing goes online before that.
   A remembered "always" checks when the launcher starts. Only `https://` addresses are used, with size limits, time limits,
   and redirects only to other https addresses.
2. **It shows the update.** The version, your notes, the permissions in plain words, and a permission the update ADDS is marked
   NEW and named. The author's key is shown the first time ("trust on first use": the launcher pins it). A new key the old key
   announced is accepted and said so; a new key nobody announced waits for the player's explicit confirmation
   ("Changing your signing key" above).
3. **Download and check.** Into a temporary folder: the size and sha256 are compared with the signed file, Windows' own virus
   scan is asked (Windows Security or the player's antivirus, through the same interface browsers use; where it isn't available
   the launcher says so and goes on with its own checks), then the **full safety scan and the checker** (the ones in `xd_mod_check`
   zip mode) run on the unpacked files. Any problem and nothing is installed.
4. **Install** only after a second yes. The old version is kept; **Roll back** on the mod's row swaps them (and again to go forward).
5. **Playthroughs are protected.** If the update changes what a playthrough depends on (table numbers or rows, story rooms or
   objects, scripts, game files) and one of the player's saves was started with the mod, the launcher says which saves and why,
   and suggests **Keep my current version**. "Install anyway" keeps the old version for Roll back; the game itself refuses to
   CONTINUE a save whose added rows moved. The save files are never touched by an update.

The honest limits: **these checks lower the risk; they cannot prove a mod harmless.** The game's file readers were not built
for hostile files. A signature proves the update came from the same key as before, not that the author is trustworthy. A virus
scan only knows programs, and mods are not programs. Only install mods from people you trust.

## Dependencies and compatibility

More optional `mod.json` fields (all work with `requires`, `conflicts`, `after`, `priority`, which already existed):

| Field | Meaning |
|---|---|
| `"requires": ["core >= 1.2"]` | Needed. The launcher offers to turn them on with the mod; one that is not installed is named, with the version it asks for. |
| `"after": ["x"]`, `"before": ["x"]` | Order: this mod comes after / before x in the list. |
| `"works_with": ["x"]`, `"recommends": ["x"]` | Suggestions only, shown under the mod. Never forced. |
| `"only_with": ["a", "b"]` | A compatibility patch: used only when **all** of these are on. Otherwise it is skipped quietly. It also goes after them. |
| `"links": {"x": "https://.../x.update.json"}` | Where a missing mod named above can be downloaded (through the same asking and checks as an update). |
| `"needs_assets": ["gen57_pack"]`, `"recommends_assets": ["usum_models"]` | The player's own extra assets the mod needs, or only works best with. See "Mods that need the player's own assets" below. |

When a player turns a mod on, the launcher turns on its requirements too, **puts the whole list in a working order** (a topological
sort: requirements and `after` first, `before` honoured, `only_with` patches after what they patch, then `priority`, then the
list's own order; a cycle is named and left to the player) and says why a mod can't be used: a missing requirement, a version
that is too old, a conflict, another game. "Put in working order" does the sort on demand. The Mods page also names clashes finer
than whole files: two mods setting the **same table value** (`pokemon / #1 / hp`), touching the **same story room**
(`rooms/<name>`, `objects/<room>.json`) or hooking the **same script event** (`on("battle.damage")`) get a warning saying which
wins (the one further down the list). The check is by name; two mods addressing one row by number and by name are not seen.

### Mods that need the player's own assets

Some content only exists on the player's PC, made on the launcher's **Extra assets** page from his own discs and dumps. A mod can say
that it needs one (or likes to have one) without carrying any of it:

```json
"needs_assets": ["gen57_pack"],
"recommends_assets": ["usum_models"]
```

| Id | What the player must have made |
|---|---|
| `br_models` | The Battle Revolution models (from his own Pokemon Battle Revolution disc) |
| `usum_models` | The Ultra Sun / Ultra Moon models (from his own 3DS dump) |
| `gen4_pack` | The Gen 4 pack (from the Battle Revolution disc or the 3DS dump) |
| `gen57_pack` | The Gen 5-7 pack (from the 3DS dump) |

- **`needs_assets`:** while one of them is not ready, the mod is **not used**. The game starts anyway, with one message that names the mod,
  what is missing and why (not made yet, made by another version of the game, or it does not fit the Gen 4 files that are installed),
  and where to make it: the launcher's Extra assets page. The Mods page and the in-game MODS page show the same sentence on the mod's
  row. For `gen4_pack` and `gen57_pack` the playthrough must also have that choice on in its New Game setup (the Gen 5-7 choice brings the
  Gen 4 pack with it); a playthrough set up without it does not use the mod, and the message says so.
- **`recommends_assets`:** only a note under the mod ("Works best with the ... of your own"). The mod is used either way.
- Models made by an older converter still count as ready (they work); a pack made for another version of the game does not.
- An id this port does not know is a problem in `xd_mod_check` (`manifest-assets`) and the mod is refused. Ids are lower case.
- **Nothing from those games may ever be inside your mod.** The mod only names the asset; the player makes it from his own copy. If your
  mod gives new species their own models, say in its description which assets they use.
- **Online rooms:** a mod that is used in a room is part of the room's list. A friend whose copy of that mod is switched off because the
  assets are not made is told so ("the mod is on your PC but needs extra assets of your own"), not that he lacks the mod; the host's message
  names what the mod needs. Nothing changed on the wire.
- **Battle replays:** a replay file records the mods it was made with; one that needs a pack has the same refusal as a missing mod, and the
  assets are named there too.

### Mods for more than one game

A mod may name more than one game in `"game"` and keep the parts made for one game in `games/<ID start>/` (see [two-games.md](two-games.md)).
The checker prints `works with: XD; Colosseum: shared parts only`; the permissions the launcher shows say `[only for GXXE01]` when
only a `games/` folder uses them; the packer, the signing, the updater and the requirements check all see the whole folder. The
launcher's Mods page and the game's MODS page show `Works with: ...` for every mod; a mod that does not name the game of your disc says
so and is not used. A mod update that changes something inside a `games/` folder holds back like a change in `data/`.

### Sharing a mod list

"Save this list to a file..." writes "these mods, these versions, this order" (only the mods that are on) as a small JSON: ids,
names, versions and update links; no paths. "Use a list from a file..." shows what you have, what is in another version and what
is missing, offers to download the missing ones (each through the update flow above, with its own asking), and applies the order
only after you press the button. Nothing in a list is run.

## Saves and updates

- Saves remember the mods they were made with, row by row, **by label** ([labels.md](labels.md), draft). Give every new Pokémon,
  move and item a stable `"label"` and set `"label_namespace"` in `mod.json`; then reordering rows or switching on another mod
  before yours does not break saves (Continue changes the numbers in the save to the new ones). Removing a row, or changing its
  label, still breaks the saves that hold it.
- Changing the numbers of existing rows is safe for saves. It only changes how the game plays.
- Script mods can keep **numbers** with a save (streaks, points, a currency: `value_set`, `define_currency`). They are written in the playthrough's setup file next to the save, never inside the GameCube save,
  together with the version of your mod that wrote them. When the version is another one at CONTINUE, the game says so plainly (it does not stop), and your script can ask `saved_version()` and convert. The launcher's update
  window lists the saves that keep numbers of the mod and suggests **Keep my current version** for them. The game still runs one version of a mod at a time: for a mod people are mid-run in, make an update read the old numbers.
- A new version that adds a permission (say, a first script) is asked about again by the launcher. Say in your notes
  why you added it.
- An update that changes numbers, rows, rooms or scripts of a mod that a save was started with is held back by the launcher
  (the player can still choose it). The game only ever runs one version of a mod, so for a mod people are mid-playthrough in, keep
  changes small and say so in the notes.

## Mods that run scripts: the player's permission

A mod with `"scripts": true` runs Luau. The scripts are locked in a box (no files, no clock, no random numbers), but they
can change how the game plays, so the **player decides**. On the launcher's **Mods** page a mod that runs scripts shows
"Runs scripts (Luau)" and an **Allow** box. Untick it and the game skips that mod's scripts and says so in its log
("... running scripts is switched off for it ..."). **The rest of the mod stays**: its data, files and texts are used as
always. Other mods' scripts are not affected. The in-game **MODS** page has the same permission: on a mod that runs scripts, press **Z** (the description box says whether its scripts are allowed or skipped), then Apply. The choice is saved in `mods.ini` as `my-mod = on, scripts off`, per profile.

For authors: a player may have switched your scripts off, so make a mod whose data part still makes sense without them, or
say clearly in your readme that the scripts are needed. A mod whose story script calls a Lua function (`lua "name"`) finds
no function when scripts are off; the story script then simply gets 0.

## Story mods and file replacement

A mod that replaces whole files (`files/...` or `fsys/...`) with your own work is fine. A mod that holds files *made
from* the game's files (a compiled room script, an edited text bank) contains game data. Do not publish it as a file.
Publish the instructions instead: the `.xds` edit as a patch or a small script that edits a player's own decompiled
copy, as `tools/story/examples/healing_machine.py` does. The player runs the tool on their own disc. See
[story-scripts.md](story-scripts.md).

## Common mistakes

- Zipping a folder that contains another folder, so `mod.json` is two levels down. `xd_mod_pack` does it right.
- Leaving a test save, a dump or a screenshot with game text in the zip.
- An `id` with spaces or other signs. Allowed: letters, digits, `-`, `_`, `.`.
- `"game"` missing or wrong.
- Saying a mod is `any_time` when it changes data. The port treats it as `new_playthrough` anyway, and the mod list
  says why.
- Calling a mod "faithful" when it changes the rules. Players use the "Original" preset to play the plain game, and mods
  are never used there.

## Mods and battle replays

A battle replay (a small file the game can keep of a battle) only plays back the same way if the battle is computed the same way. So a replay remembers which of your
mods change what a battle computes, and the game refuses to play it when those differ. It tells you which mod and why.

- Mods that **change the battle** and so must match: data mods (species, moves, items, trainers, rules, new rows) and script mods (battle scripts).
- Mods that **only change looks or sound** never matter: texture packs, model and looks packs, music and cry replacements. Say `"applies": "any_time"` in
  the manifest and keep to looks and sound; if such a mod also touches game data the game treats it as a battle-changing mod.

The game never downloads a mod for a replay: it refuses and names the mod.

## See also

- [troubleshooting.md](troubleshooting.md): what the checker's messages mean
- [two-games.md](two-games.md): one mod for more than one game
- [labels.md](labels.md): stable labels
