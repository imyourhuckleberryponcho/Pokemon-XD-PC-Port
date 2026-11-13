# launcher/

The launcher is the small window you start first: **`Pokemon XD.exe`** in the player's folder (below), which
starts **`xd_launcher.exe`** (in `build\release\bin`, next to `xd.exe`; starting that directly works too).
Choose your disc, how you want to play, your controls and mods, then press **Play**.

## The pages

- **Disc**: "Choose..." opens a normal Windows file picker; pick your Pokemon XD disc image (.iso, .rvz...).
  It is checked the same way the game checks it (right version, complete, unchanged). The first check of a
  new file reads it all (a few seconds, with a progress bar); after that it's remembered. The launcher
  remembers the disc for next time (`system.disc_path` in settings.ini). Below: "Icon and shortcuts".
- **Extra assets**: discs of OTHER games the port can borrow assets from (below, "Extra assets"). Today one row, Pokemon Battle Revolution (USA): "Battle Revolution battle models".
- **Preset**: Original (the GameCube game, the default), Enhanced (the recommended improvements) or Custom
  (your own mix). Below it: everything that is different from the GameCube game right now.
- **Settings**: every setting, in tabs (Video, Audio, Gameplay, System). Each says what it does; a purple
  "Changed" line means it's not as on the GameCube. Greyed-out settings don't do anything yet. "Back to
  Original on this page" undoes the page.
  - *Photo mode* (Video tab, on in Enhanced, off in Original; "Photo mode key", F8 at first): in the game, the key
    pauses the game and opens a small window over it. Move a free camera with W A S D (E/Q up and down, Shift
    faster, Ctrl slower), look around with the right mouse button held (wheel: speed), tilt with Z/C; or a
    controller (sticks, triggers up/down, shoulders tilt, A photo, Y reset, X hide 2D, B/Start back). The window
    has speed, field of view and tilt sliders, "Hide menus and text (2D)", the photo size (as the game draws, or
    640x480 up to 5120x3840) and **Take photo** (or P). Photos are PNG files of the picture itself (no window
    border, no black bars), in `%APPDATA%\Pokemon XD\photos`. H hides the window, R resets the camera, the key
    again (or Esc) goes back: the game carries on exactly where it was, as if no time had passed. The camera only
    sees what the game drew for that moment, so things outside the game's own view may be missing.
  - *Full screen* / *Borderless window* (Video tab, off in every preset: they are yours, like the volumes): full
    screen covers the monitor without changing its resolution or refresh rate (desktop full screen); borderless is
    a window without frame over the whole monitor. Both also on the game's SETTINGS page, and **Alt+Enter** in the
    game switches full screen; every change works at once and is kept.
  - *Pause in the background* (System tab, on in Enhanced, off in Original): the game and its sound stop while its
    window isn't in front and carry on when you come back, as if no time had passed.
- **Controls**: a tab for each of the four GameCube controllers, each in cards:
  - *Controller N*: which PC controller plays as it (Automatic = the first free one, None = keyboard only, or one
    of those plugged in, listed by name; plugging one in or out shows at once). Controller presets: Auto (the
    standard buttons of whatever it reports itself as), GameCube controller (adapter: Nintendo's WUP-028, or a
    Mayflash in Wii U mode), Xbox, PlayStation, Switch Pro (by the letters printed on it). Keyboard default
    (Dolphin's keys for controller 1; none for 2-4, or "Copy controller 1's keys"). Rumble on/off and "Test rumble".
  - *Try it*: what the game reads from this controller right now, drawn as a GameCube controller: both sticks
    (with the deadzone ring), L and R (analog value, click point, click), every button, and the numbers PADRead
    returns. "Try the keyboard" lets the keys play it (meanwhile keys don't work on the page; Esc ends it).
  - *Buttons*: every GameCube button and stick direction with its keys and PC-controller buttons. Click one,
    then press the new key or button (Esc cancels); "..." adds a second one, sets none, or the standard one.
    Violet = changed. "Reset this controller to the defaults" (keeps which PC controller plays). Below them the
    port's own actions, which are not GameCube buttons: *Fast-forward* (Tab on controller 1's keyboard, the right
    stick pressed in on every PC controller; works only while Settings > Gameplay > Fast-forward key is on).
    Script: `key:fast_forward=43`, `padbutton:fast_forward=back`.
  - *Sticks and triggers*: deadzones, L/R click points (for triggers without a click), button layout, walk tilt.
  Remapping is a preference, not a gameplay setting: it is allowed in every preset, Original too, and never
  makes a run "not Original" (it decides how you press the GameCube's buttons, not what the game does). The
  defaults are the GameCube layout. xd.exe reads the same settings.ini (`[keyboardN]`, `[gamepadN]`, docs/SETTINGS.md)
  and follows changes and plugged-in controllers while it runs (platform/input/controls_apply.cpp).
- **Mods**: the mods in your mods folder, with on/off and order (lower in the list wins). "Mod list" switches
  between lists (for example one for a randomizer run); "Open the mods folder" shows where mods go.
  Each mod shows its kind (looks and sound: any time; read at start: restart; changes the world or the rules: new
  playthrough). New-playthrough mods are chosen for a new game on the game's New Game setup screen (after NEW GAME;
  setting "New Game setup screen", off in Original) and then stay with that save (docs/PLAY_SETUP.md).
- **Save**: **Saves**: the list of your saves (README section "Several saves" below). The circle at the left of
  a row marks the save the game loads. A click on a row opens that save right under it: trainer, play time,
  money, party (only shown), its playthrough setup, and **Backups**: one line ("4 kept, newest <when>") with
  "Show backups" for the copies the game keeps by itself (README section "Save backups" below), newest first,
  each with when it was taken, which save (its own comment line and save time), the card and the size;
  "Restore..." (asks first: "this replaces your current save; the current one is backed up first"; not while the
  game runs), "Open folder". The buttons for that one save (Rename, Duplicate, Export, Delete) are in the same box.
- **Reports**: the bug reports the game made (README section "Reports" below): F10 ones and the ones it makes by
  itself after a crash, a stop or a hang. Newest first, each with its kind, where in the game it was and its size;
  the selected one shows its screenshot, what's inside (every file, in plain words) and its info.txt. "Export..."
  saves a copy as a zip, with a name and in a place the player picks; "Open folder" and "Delete..." (asks first).
  "Save my last play session..." saves the last time the game was played as one zip (log, button presses, card
  copies, settings) when the game made no report, e.g. it froze and was closed.

The Disc page ends with **Version and updates**: the version (from `version.txt` in the game folder, else the
build's own), and, in a build with an update address, the updater's switch, "Check now", what was found and "Go
back to the previous version" (below, "Updates").

At the bottom: the disc, the preset and the mods in use, "Keep this window open" (otherwise the launcher
closes when the game starts) and **Play**.

Everything is saved as you change it, into `%APPDATA%\Pokemon XD\settings.ini` and `mods.ini` (the same files
the game reads). You can still start `xd.exe` directly (drag the disc onto it); it uses the same settings.

## Reports

Reports stay in their folder (`%APPDATA%\Pokemon XD\reports`, or the portable folder): nothing is sent anywhere,
there is no upload, no email and no network code. To share one, the player exports it and sends the file themselves.

- The list: every `report_<date>_<time>[_crash|_stop|_stuck]` folder (games/xd/src/bug_report.cpp writes them),
  newest first: date and time, the kind in words (F10 report / Crash / Stopped / Stuck), the moment from info.txt
  (`map Phenac City (0x064)` and the game frame; the map line is new, from `PortXD_ReportMap` in warp.cpp, so
  older reports show only the frame) and the size. The page looks at the folder again every two seconds while it
  is open, so a report made meanwhile appears. Arrow keys move through the list (it follows the keyboard).
- **Export...** opens Windows' Save window (`SDL_ShowSaveFileDialog`) in Documents with the name
  `report_<date>_<time>.zip`. The zip is made fresh from the folder (`launcher::reports::Export`, compressed like the
  game's own zip: deflate for every file it makes smaller, pictures stored; `port_zip::Store` in platform/os), with
  two choices:
  - *Hide my Windows user name* (on): in every text file (info.txt, log.txt, settings.ini, mods.ini,
    recording.txt...) the profile folder (`C:\Users\<name>`, also with `/` or doubled `\\`) becomes
    `%USERPROFILE%`, a path segment that is exactly the name (and its short form `NAME~1`) becomes `%USERNAME%`,
    and so does a value word that is exactly the name in `key=value` / `key: value` lines. The name inside other
    words is left alone ("sam" in "sampling" or in `C:\Users\samuel`). Pictures and the crash dump are not text
    and are copied as they are.
  - *Include the crash dump* (off): crash.dmp / stop.dmp / stuck.dmp, a snapshot of the program's memory; it helps
    find a crash but may hold bits of anything that was in memory, so it is left out unless ticked.
  The screenshot and the recording are always included (the page says the screenshot shows only the game window).
  info.txt in the export ends with a line saying which choices were made. The report folder itself is never changed.
- **Delete...** removes the folder and the zip the game made next to it (exported copies stay).
- **Save my last play session...** (top of the page, there even when there are no reports): for a game that
  stalled, froze or was closed without a report. A window lists the play sessions, newest first (when it
  started, how long it ran, size, how it ended: closed normally / not closed normally / never started playing /
  very short, and how many reports it made); the newest is chosen, any older one can be picked. Below: "Hide my
  Windows user name" (the same switch and the same scrubbing as Export), what's inside (every file, its size, in
  plain words), "Save..." (Windows' Save window in Documents, name `session_<date>_<time>.zip`).
  - A **session** is one run of xd.exe started as play (not test runs): its log `logs\play_<date>_<time>_p<pid>.log`
    with the recording `recordings\play_<date>_<time>_p<pid>.txt` of the same process started up to 10 minutes after
    it (and before the next log), plus that recording's `.cards\` folder (the card at the start and after each save),
    and every report folder whose time falls between its start and two minutes after its files were last written.
    A recording without a log is a session of its own. Length = last write of its files minus its start, so a
    crashed or killed run (half-written log, no END line in the recording) works, and so does one still running
    (the zip is made from what is on disk at the moment of saving).
  - **Which files**: info.txt (made fresh: launcher version, the recording's port commit and decomp snapshot lines,
    start, length, last game frame, whether it ended normally, the reports, the choice below, every file with what
    it is), the log (its last 16 MB if longer), the recording, the card copies, each report's files (crash dumps
    left out: the window says to use Export on the report for one), settings.ini and mods.ini as they are now.
  - **Short sessions**: when the chosen session recorded under 600 game frames (10 s) or has no recording at all
    (a start that never got to play, or the game opened again after the problem only to close it), the session
    before it is put in too, under `previous session/`, and info.txt and the window say so.
  - **Kept small** (0.2.2; tester exports were 28 MB and got re-compressed by hand): every file deflated where that
    helps; each report's screenshot saved as JPEG (quality 90; Windows' image codecs, `port_zip::AsJpeg`; a report's
    own Export keeps the PNG); a file byte for byte the same as another left out (the copy nearest the top stays:
    settings.ini, mods.ini, identical screenshots/recordings); a report's recording.txt, which is the session's
    recording up to the report plus a few lines, kept only as those lines (`recording.rest.txt`). `shortened.txt`
    in the zip lists each (tab-separated `jpeg` / `same` / `prefix` lines) and info.txt says so;
    `python tools/unpack_export.py <zip> <folder>` unpacks and puts every left-out file back, byte for byte (the
    PNGs stay JPEG). Tester 1's session (23 reports): 27.9 MB -> 3.6 MB, exported in 0.7 s; all 123 other files came
    back identical. Measure one: `launcher_reports_test.exe --export-session <data folder> <out.zip>`.
  - Nothing is sent: the player saves the file and gives it to the developer. The game's own files are only read.
- Code: `launcher/launcher_reports.cpp` (game-neutral: folders, info.txt lines, scrubbing, the export zip, play
  sessions: `ListSessions`, `BundleFor`, `ExportSession`) and `DrawReportsPage` / `DrawSessionWindow` in
  launcher_app.cpp; the folders come from `GameInfo::reportsFolder`, `logsFolder`, `recordingsFolder` (XD: the
  user folder's `reports`, `logs`, `recordings`). Test: `launcher_reports_test.exe [temp folder]` (scrubbing
  cases, listing, export read back, delete; sessions: matching log/recording/cards/reports, a killed run's
  half-written recording, the short-session rule, the session zip read back and scrubbed; a shortened session zip:
  JPEG screenshots, copies left out, recording rests, put back from shortened.txt).

## Extra assets

A page for the discs of other games (not the game the port plays) whose pictures or models the port can use. The list
of sources is a small table (`GameInfo::ExtraSource`, XD's rows in `games/xd/launcher/xd_extra_sources.cpp`): id, the
disc's name, the game IDs it accepts, what it provides, how it is made, the setting that remembers the disc's path, the
description file's id, and the converter program. One row today; Pokemon Colosseum and others get a row each.

Extra assets are a **built-in feature of the port, not mods** (mods are things other people make and add through the
Mods page). What is made goes in `<data folder>\extra_assets\<source id>\` (e.g. `extra_assets\pbr`), never in the
mods folder, and it never appears in the Mods page, in mods.ini, in the bottom bar's MODS line or in a playthrough's
mod list.

- **Pokemon Battle Revolution (USA, RPBE01)** gives "Battle Revolution battle models": the Pokemon models of that disc
  (a Wii disc) turned into XD's model format, in `extra_assets\pbr`. XD keeps its own model for any Pokemon the
  converter couldn't convert.
- Per source: **Choose disc...** (the Disc page's file picker; .iso .rvz .wbfs .ciso .gcz), a quick check that the file
  really is that game (its header, through the same disc reader as the Disc page; a wrong game says which game it is
  and which one is needed), the path remembered in settings.ini as `system.extra_disc.pbr` (hidden from the Settings
  page), and **Make models**.
- **Make models** runs the converter program shipped next to the game, `<game folder>\pbr_models\pbr_models.exe`, on a
  thread (the window never freezes): `pbr_models.exe --pbr-disc <that disc> --xd-disc <your XD disc> --out <data
  folder>\extra_assets\pbr` (`--out` is just a folder to make). It prints `progress <done> <total> <model name>` lines (the progress bar), then
  `done <converted> <kept_xd> <total_bytes>` (exit 0) or `error <message>` (exit 1; a wrong disc exits 2). The converter
  writes `<out>.tmp` and renames it at the end. **Cancel** ends the program and everything it started (a job object on
  Windows) and removes a leftover `<out>.tmp`: nothing half-made stays. Errors are shown in the converter's own words.
  The folder holds `files\`/`fsys\` in the layout the game loads, `conversion_log.txt` and `mod.json`, which is only the
  folder's description file (its version is what the status compares); it stays named `mod.json` because mods code
  only ever looks inside the mods folder, never in `extra_assets`.
- **Where the program is, per system** (always `<program folder>/pbr_models/pbr_models[.exe]`, `<program folder>` =
  the folder of the launcher's own file): Windows `game\pbr_models\pbr_models.exe` (tools/package_release.py); Linux
  `game/pbr_models/pbr_models` + `libnod.so` (tools/package_linux.py, built in WSL with
  `python3 tools/pbr_convert/package_pbr_models.py`); Mac: the launcher's folder is `Pokemon XD.app/Contents/MacOS`, where
  `pbr_models` is a link to `Contents/Resources/pbr_models` (games/xd/mac/package_mac.sh; built by the Mac workflow;
  the Mac launcher is built and packaged by the workflow since 2026-11-12, docs/MAC.md "Launcher on the Mac"; none of it has run on a Mac). The Linux run of the real program on the real discs
  (docs/LINUX.md "pbr_models on Linux") also needs the POSIX half of `launcher_sys` for Cancel: the converter leads a
  process group of its own (`killTree`), so its worker processes end with it.
- **Status** on the page: "Not set up" / "Ready: N models" / "Out of date: make again" (the description file's version
  is older than `pbr_models.exe --version`, which prints e.g. `version 1.0.0`; unknown version: never out of date) /
  "Converter missing" (no pbr_models.exe next to the game; a folder that is already there still counts as Ready).
  It reads `extra_assets\pbr`.
- **Using the result**: the setting `video.battle_revolution_models` ("Battle Revolution models", Enhanced, off unless
  turned on; an improvement, so Original/Faithful holds it off). When it is on and the folder is there, the game puts
  the folder's files in the disc overlay as a built-in layer: **above the disc, below every real mod** (a mod that
  replaces the same file wins, quietly), through the same overlay code mods use (`engine::mods::LoadOptions::extraLayers`,
  set in `games/xd/src/mods_boot.cpp`). Off, or no folder: nothing. The start-up log says
  `[extra assets] Battle Revolution models: on, 388 files` (or `off`, or why not), and the same sentence is the
  `extra assets` line of recordings and F10's info.txt; the setting itself is also in their `settings` line like every
  other change from the GameCube game. The Extra assets page has the tick, the Settings page (Video) the row, and the
  game's own SETTINGS page shows the row when the folder is there. It is read when the game starts, so it acts from the
  next start (or soft reset); the row says so. It only changes how the Pokemon look, so it works for every save.
- **An old test build's mod folder**: `<data folder>\mods\battle-revolution-models` (made when this was a mod) is moved to
  `extra_assets\pbr` by the launcher when it starts, if `extra_assets\pbr` isn't there yet, and taken out of mods.ini;
  if both exist the old one is left alone (delete it).
- Testing: `XD_PBR_MODELS_EXE=<program>` names another converter (it must keep to the command line above);
  `pbr_models_standin.exe` (launcher/tests/pbr_models_standin.cpp; XD_STANDIN_SOURCE=<work folder with pairs.tsv and
  out\*.pkx>, XD_STANDIN_COUNT, XD_STANDIN_DELAY_MS, XD_STANDIN_VERSION, XD_STANDIN_FAIL) does. Script steps
  `page:extra`, `extradisc:pbr=<disc>`, `wait:extradisc`, `extramake`, `extracancel`, `wait:extra`. Unit tests:
  `launcher_extra_test.exe` (the converter's lines, status words, disc check, the stand-in run for real: done, wrong disc,
  error, Cancel, leftover .tmp), `xd_extra_sources_test.exe` (the table agrees with the settings list), `mods_test.exe`
  (the switch in the loader).

## The playthrough on the Save page

Under the save's facts and party, "Playthrough" shows the setup the playthrough was started with (docs/PLAY_SETUP.md
"Saves"): the file `<card root>\playthroughs\Card A\pt_*.json`, matched to the save by its trainer's name and ID
numbers (games/xd/src/xd_playthrough_files.cpp, the same file xd.exe uses; read in games/xd/launcher/xd_launcher.cpp
ReadSetup, drawn by App::DrawSetup):
- Game engine (Faithful / Enhanced), Mods (the ones that change the world; looks-and-sound ones on their own row),
  Randomizer (off, or its style Light / Classic / Chaos / Custom, read from the code) and the Randomizer code, Started.
- **Copy code** puts the code on the clipboard, to give to a friend: on the New Game setup screen they choose "Use a
  friend's code" (it reads the clipboard) and get the same changes.
- **Show spoilers...** (only when the spoiler log is there: `<card root>\playthroughs\randomizer\<code>\spoiler.txt`)
  first asks "Show spoilers?" (it tells everything the randomizer changed), then opens the file.
- No setup file: "Faithful, no mods (no setup file)" and why (started before the port kept setups, or copied in from
  Dolphin or a GameCube). A setup written quietly because no setup screen ran (Original preset, tests; the file says
  `"chosen_by": "run"`): a line says nobody chose it. "Carried on without its full setup" lines for each change.
  A playthrough whose engine was switched at Continue (the "Faithful or Enhanced?" question): "Game engine: Enhanced
  (started as Faithful)", a line per switch with its date, and that it is not Faithful from start to end.
Script steps (tests): `wait:save`, `copycode` (prints what the clipboard holds), `spoilers` / `spoilers:close` (the
question only; never opens the file). Note: `copycode` really sets the PC's clipboard.

## Several saves

The game itself knows one memory card with one save. The launcher lets a player keep several saves side by side
and choose which one the game loads; the game is not changed.

- **For the player** (Save page, "Saves"): a list with one row per save: its name, the trainer, the play time and
  the playthrough (Faithful / Enhanced and "was ..." when it was switched, randomizer style and code, challenge
  rules, world mods; "no setup file" for a save from Dolphin or from before setups).
  - **The circle at the left of a row = the save the game loads** (CONTINUE, or New Game when it is empty), also
    when the game is started without the launcher. Clicking another row's circle ticks it and unticks the other;
    exactly one is always ticked. Only the circle changes which save loads. The bottom bar shows that save's name
    next to disc, preset and mods.
  - **A click anywhere else on a row opens that save in place**: a box right under the row (the small arrow points
    down). A second click closes it; opening another save closes the first. Opening a save never changes which
    one the game loads, so a save can be looked at without choosing it. When the launcher starts, the save the
    game loads is the open one.
  - **The box** holds everything about that one save: its name, whether the game loads it, trainer, play time,
    money, the party, the Playthrough block (engine and its history, randomizer style and code with Copy code /
    Show spoilers, challenge rules, mods), where its memory card file is, and its backups: one line ("4 kept,
    newest <when>") with **Show backups**, which shows the list with Restore... and Open folder (README section
    "Save backups"). An empty save's box says "No save yet: the game offers New Game" and its folder.
    Not shown: the place in the game where the save is (the save reader only gives the map's number, not its
    name).
  - **Above the list**: **New...**: a new, empty save (asks for a name; "Save 2" is offered). It is chosen and
    opened, and the game offers New Game. **Import...**: a `.gci` file (from Dolphin's "GCI folder", a GameCube
    memory-card tool, or another PC). Only a file the game's own save reader accepts (a USA Pokemon XD save). It
    becomes a new save named after its trainer, chosen and opened; the file picked is only copied.
  - **In the box** (they act on the open save, whether the game loads it or not):
    **Rename...**: another name. Only the name in the list changes.
    **Duplicate**: a copy called "<name> (copy)" (the save and its setup, not its backups). The copy is not chosen.
    **Export...**: a copy of the save's `.gci` file where the player says (Windows' Save window, Documents).
    **Delete...**: asks first, then moves the save's folder (save, setup, backups) to Windows' recycle bin, so it
    can be taken back out. The first save's place stays in the list, empty. Deleting the save the game loads
    makes the first save the one it loads; deleting another one changes nothing about that.
    An empty save's box has Rename... and Delete... only.
  - While this launcher runs the game, ticking another circle and every change are refused ("Close the game
    first: it has the memory card open"); opening saves and Export still work.
  - Script steps (tests; the list at the top of include/launcher.h): `slot:<n|name>` ticks a circle,
    `openslot:<n|name|none>` is the click on a row, `renameslot` / `dupslot` / `deleteslot` / `exportslot` act on
    the open save, `showbackups`, `backup:<n>` (the open save's list).
- **The player's existing save is "Save 1" and nothing of it moves**: the first save's folder is the data folder
  itself, where the save has always been.
- **Where things are**:
  - `%APPDATA%\Pokemon XD\` = the first save ("main"): `USA\Card A\*.gci`, `playthroughs\`, `save_backups\`;
  - `%APPDATA%\Pokemon XD\saves\s2\`, `s3\`... = every other save, each with its own `USA\Card A`, `playthroughs\`
    and `save_backups\` (so each save has its own setup files and backups);
  - `%APPDATA%\Pokemon XD\saves\slots.json` = the list: `{"version": 1, "active": "s2", "slots": [{"id": "main",
    "name": "Save 1"}, {"id": "s2", "name": "Nuzlocke run"}]}`. Folder names are made by the program (`s2`,
    `s3`...), never from the name the player types. No file = only Save 1. A file that can't be read = Save 1 plus
    every `saves\s<number>` folder found ("Save 3 (found again)"), and the unreadable file is kept as
    `slots.json.damaged` when the list is next written.
- **The game program**: `xd::CardRoot()` (games/xd/src/xd_user_folder.h, xd_card_root.cpp) is `XD_CARD_DIR` when a
  test sets it (then there is no list at all, in the launcher too), else the chosen save's folder. xd.exe prints
  `[card] save: <name> (<folder>)` at start. For a save other than the first only the memory card goes to its
  folder (`PortCARD_SetRoot`, platform/sdk/card_root.cpp, Aurora's `CARDSetBasePath`): Aurora's own files
  (`input.json`: which controller is in which port; `imgui.ini`) stay in the data folder for all saves. After the
  card is opened the game checks it really is in that folder; if not it plays without a memory card rather than
  on another save's.
- **Code**: `engine/memcard/save_slots.cpp` (game-neutral: list, chosen one, create, rename, duplicate, delete with
  the caller's "bin", import/export of one `.gci`), `DrawSlots` (the list), `DrawSlotBox` (the open save's box) and
  the `...Slot` functions in launcher_app.cpp, `GameInfo::Saves` (XD's rows, import check, save file, and `read` /
  `cardFolder` for the box of a save that is not the chosen one: games/xd/launcher/xd_launcher.cpp; saves that are
  not the chosen one are read through `xd::CardRootScope`, the open one's contents on a thread of its own,
  `App::OpenSummary`).
- **Script steps** (tests): `slot:<n|name>`, `newslot:<name>`, `renameslot:<name>`, `dupslot`, `deleteslot`,
  `importslot:<file.gci>`, `exportslot:<file.gci>`. `XD_SAVE_BIN_DIR=<folder>`: deleted saves are moved there
  instead of into the recycle bin (tests). A scripted launcher without `XD_SETTINGS_DIR` (a test on the player's own
  files) refuses every change to the saves.
- **Tests**: `save_slots_test.exe` (the engine part: 72 checks, also damaged lists and names that aren't ASCII);
  `python tools/test_save_slots.py --build <bin>` (a scratch data folder: new / import / rename / duplicate /
  export / delete in the scripted launcher, xd.exe loads the chosen save, changes refused while the game runs, and
  Save 1's files byte for byte unchanged at the end; about 2 minutes).
- **Not built**: choosing between saves inside the game (a far-future idea); the map a save is on in its row.

## Save backups

- **Made by the game program**, not the launcher: just before the game writes to a memory-card file,
  `platform/sdk/card_backup.cpp` (called by `PortCARD_WriteAsync`) copies the file as it is on disk to
  `<card root>\save_backups\<card>\<date>_<time>\<file>.gci` (card root = the data folder, or `XD_CARD_DIR` in
  tests, so a test card keeps its own). Only when it differs from the newest backup of that file; the newest 10
  per card are kept (`engine::memcard::backups::kKeep`, a constant). One save can be several writes (a new file,
  XD's GBA part): only the first write of a burst counts (writes of one file less than 600 video fields apart),
  and a file still all zeros (just created) isn't kept. The copies are real `.gci` files (GameCube format, as the
  card holds them), so they also work in Dolphin.
- **Never changes the game**: the card operation's result and the frame it completes on (`system.card_timing`) are
  decided as before; on the game's thread only the file is read into memory (well under a millisecond), the
  comparing, writing and deleting happen on a worker thread (finished at exit). `XD_SAVE_BACKUPS=0` turns it off.
- **Restore** (`engine::memcard::backups::Restore`): reads the backup, backs up the current file (the same rule, so
  nothing is lost), writes the backup next to the card file and renames it over it. The launcher refuses while it
  runs the game (the game has the card open); then reads the save again for the page.
- xd_save_convert `--install`'s copies (`save_backups\<name>.<stamp>.gci`, loose files at the top) are left alone.
  The recorder's card copies and the launcher's card search skip the `save_backups` folder.
- Code: `engine/memcard/save_backups.cpp` (game-neutral: list, add, prune, restore), `DrawBackups` in launcher_app.cpp,
  the folder from `GameInfo::saveBackupsFolder` / `cardFolder` (XD: `xd::CardRoot()\save_backups`). Script steps
  `backup:<n>` (1 = newest), `restorebackup`. Tests: `save_backups_test.exe` (rotation, only-when-changed, restore),
  `python tools/test_save_backups.py --build <bin>` (two saves in the game, restore in the launcher, CONTINUE loads it).

## Icon and shortcuts

- **The program icon** (`games/xd/resources/xd_icon.ico`, `.png`): original art, a violet "XD" monogram, drawn by
  `python tools/make_icon.py` (Pillow; `--preview sheet.png` shows every size). Each size is drawn on its own, so
  16x16 still reads. It is built into every XD program (`resources/xd.rc`), so Explorer, the taskbar and the
  windows show it. No game art in it. Four other styles of it (`--style shadow|minimal|swirl|light`,
  `xd_icon_<style>.ico`; `--gallery sheet.png` shows all) are built into the launcher only (`resources/xd_launcher.rc`)
  as icon choices.
- **Icons from the player's disc, and the choice.** After a disc passes the check, the launcher makes a set of
  icons from the disc's own pictures, once per game ID, into the user folder (`%APPDATA%\Pokemon XD\icons\`, or
  the portable folder): `GXXE01_<choice>.ico/.png` for each, `GXXE01_icons_v2.txt` listing them,
  `GXXE01_disc_banner.png`. XD offers (games/xd/launcher/xd_launcher.cpp, `ReadDiscPictures`): the game's logo
  from the end credits (staffroll.fsys, 640x480, sharp): "Full logo" (the default; its "XD" letters for the 32/24/16 sizes) and "XD logo" (its XD on black);
  the disc banner (opening.bnr; its "XD" letters alone for the 32/24/16 sizes, as the full logo); the icon the game
  puts on the memory card with its save (main.dol, 32x32, an animated Eevee; frame 1), Shadow Lugia's face and
  party icon, Lugia, Eevee's face and party icon, Espeon, Umbreon, the hero's face (poke_face / poke_dance /
  poke_body / chara_small.fsys, by entry name). Each is enlarged with an edge-aware Scale3x/Scale2x (own code
  after the published rules; colours compared with a tolerance) and then shrunk a little to fit, inside a dark
  violet rounded tile like the program icon's, so it reads as an app icon. Any PNG in `icons\rendered\` (e.g.
  pictures the game renders from its own models later) is offered too, as `rendered_<name>`. The Disc page shows
  them all as tiles, the program's own styles first; the choice is the setting `system.app_icon` (an id, or
  empty = the disc's first, before a disc the program's own). Styles other than the program's own are written
  out of the launcher to `icons\<style>.ico` when chosen. The chosen one is copied to
  `GXXE01.ico` (removed for "original"): the launcher's window, xd.exe's window (read at start; `[window] icon:`
  in its log) and the shortcuts use that file, else the program icon. Nothing from the disc is ever in the
  repository, the build or the player's folder: it is made on the player's PC. Delete the `icons` folder to make
  them again.
- **The taskbar button** follows the chosen icon too, also when the launcher or the game was started from a
  shortcut (that would otherwise show the shortcut's icon): both give their window the app id `PokemonXD.PCPort`
  (`PortWindowIcon_SetAppId`), and the launcher sets the icon chosen last time before its window is shown.
- **Launcher background** (setting `system.launcher_background`: "none", an id, or empty = the first): pictures
  behind the pages, scaled to cover the window, darkened by 40%; the panels are 76% opaque and the margins wider,
  so the picture shows around and through them. Any PNG in `%APPDATA%\Pokemon XD\backgrounds\rendered\` (e.g. the
  scenes rendered from the game's models) comes first, so a render is the default when there is one. From the disc
  XD offers the map of Orre (world_map.fsys menu_039; the default without a render), the menus' Poke Ball pattern
  (title.fsys menu_042), kept as PNGs in `backgrounds\`
  (`GXXE01_backgrounds.txt` lists them). Chosen on the Disc page.
- **Icons and backgrounds drawn by the game itself (3D renders).** After the disc's icons, the launcher has the
  game draw sharper ones from its own 3D models: it starts `xd.exe` hidden with `XD_RENDER_ICON=all` (no window,
  no sound, about a minute, below-normal priority; the Disc page says so meanwhile and stays usable). xd.exe boots
  to the title screen and writes, from the player's disc:
  `icons\rendered\shadow_lugia.png`, `eevee.png`, `michael.png` (1024x1024, transparent, 3/4 view, drawn at
  2048x2048 and shrunk: smooth edges), `logo.png` (the title screen's logo taken from the game's own picture at
  2560 pixels wide) and `backgrounds\rendered\title_scene.png` (the title's sky without logo and menu, 2560x1440),
  `lugia_sky.png` (Shadow Lugia in that sky), `duel_sky.png` (Shadow Lugia and Eevee facing each other). The icon
  choices offer them as "... (3D render)"; their 32/24/16 sizes show the top of the figure closer
  (`icon::FocusTop`: a head and shoulders), so they still read. Once per game ID: `icons\rendered\render_GXXE01.done`
  (with xd.exe's exit code; `render_icon.txt` lists what was made); delete it to draw them again. Testing:
  `XD_LAUNCHER_RENDER=0` leaves this out; script step `wait:pictures`.
  How xd.exe draws them (games/xd/src/render_icon.cpp): a `GSeffect` of the game's graphics engine (its render
  module slots are all taken) records, inside the title screen's 3D layer, an offscreen pass (Aurora's
  `GXCreateFrameBuffer`) with the model drawn by the game's own `GSmodelDrawModel` through a camera and lights of
  ours, then a colour capture at that point of the command stream (Aurora patch 0019); once over black and once
  over white, which gives the transparency. Models are loaded the way the summary screen does
  (`Sequence_LoadPokemon` 417 = XD001, 133; `Sequence_LoadTrainer` 1) and posed by their sequence's idle
  animation. A first picture finds the model, a second is framed tightly. Normal play never runs any of it.
  Michael uses his model's animation 2 (battle stance; the sequence's idle is a bare T pose). Limits: no Shadow aura
  (particles aren't drawn); models are drawn early on the title screen (later the title turns them grey: not
  understood yet); the logo is the title's 2D texture art, so it is only as sharp as that texture.
- **Shaders prepared for this PC** (`GameInfo::prepareShaders`, since 0.2.2). Every draw needs a compiled shader
  (pipeline); compiling one the first time takes a moment. The game waits for it (never draws a place with a piece
  missing, as 0.2.1 did), so the launcher compiles all the game's shaders beforehand: after the disc check and the
  icons, the Disc page shows "Preparing the game's shaders for this PC..." with a progress bar; the page stays usable,
  and Play pressed meanwhile starts the game as soon as it is done ("Starting..."). Once per PC: the fingerprint
  (version, the shipped list's hash, every graphics adapter with its driver version) is kept in
  `<shader cache>\shaders_prepared.txt`; a driver or game update or a new list prepares again (quick when most are
  still in the cache). The shader cache folder is Aurora's: `XD_CACHE_DIR`, else `%APPDATA%\Pokemon XD`.
  How: xd.exe with `XD_PREPARE_SHADERS=<stamp file>` (games/xd/src/shader_prep.cpp) starts hidden, lets Aurora
  compile every pipeline it knows (the shipped list `initial_pipeline_cache.db` next to xd.exe + what this PC met
  before), writes `<built> <total>` to `<stamp>.progress` and exits; no game code runs. The list is made from test
  runs with `tools/pipeline_list.py` (games/xd/shaders/pipelines.txt: drawing settings only, no game data).
  Testing: `XD_LAUNCHER_SHADERS=0` leaves it out (so does `XD_LAUNCHER_RENDER=0`, unless `XD_LAUNCHER_SHADERS=1`);
  script step `wait:shaders`; `python tools/test_shader_prep.py --build <bin>` (below).
- **Shortcuts**: the Disc page's "Create Start menu shortcut" / "Create desktop shortcut" make a `Pokemon XD.lnk`
  to `Pokemon XD.exe` (the top of the player's folder) with the disc's icon. If Windows refuses (a desktop kept
  in OneDrive, folder protection), the page says so and how to make one by hand.
- Shared code: `launcher/launcher_icon.cpp` (GameCube picture formats, Scale2x, .ico writing),
  `platform/os/window_icon.cpp` (a window's icon from a file or the program's own). XD's part: which pictures
  (`ReadDiscPictures` in games/xd/launcher/xd_launcher.cpp).

## The player's folder

`python tools/package_release.py` (after `build.bat release`) makes `dist\Pokemon XD PC\`:

    Pokemon XD PC\
      Pokemon XD.exe   the only program on top: starts game\xd_launcher.exe (games/xd/launcher/starter.cpp;
                       C library built in, needs no DLL next to it), or the folder game_folder.txt names
      Read me.txt      for players
      game\            xd_launcher.exe, xd.exe, SDL3, Dawn, nod, DXC, zlib, libpng, the Microsoft C++
                       runtime DLLs, fonts\ (Inter + licence), licenses\, version.txt (--version; default the
                       top CMakeLists.txt's project() VERSION)
      game-<version>\  later, an update installed by the launcher (with game_folder.txt; docs/UPDATER.md)

No test tools, no .pdb/.ilk, nothing from the user folder or a disc (the script stops if anything else is in it).
`--build <bin folder>` / `--out <folder>` for other builds. It doesn't make shortcuts.

Why a small starter instead of the launcher itself on top: the launcher needs SDL3.dll, webgpu_dawn.dll, nod.dll,
libpng and the C++ runtime next to it, which would put a dozen files beside it. The starter needs nothing.

The launcher finds the game next to itself (`xd.exe`), else in `game\` (a launcher copied to the top). The data
folder is unchanged: `%APPDATA%\Pokemon XD`, or portable mode with Borealis's `data_location.json`
(`{"version": 1, "mode": "portable"}`). That file works in either place: next to `Pokemon XD.exe` (data in
`Pokemon XD PC\portable`) or in `game\` next to the programs (data in `game\portable`); the one in `game\` wins
when both exist (platform/os/user_folder.cpp). Read me.txt tells players the first.

## Updates

Plain-language guide, signing and publishing: `docs/UPDATER.md`. In short: completely opt-in, and even when on it
always asks before downloading.

- **Inactive until published**: `games/xd/updates.txt` (the feed address and the public key, compiled into the
  launcher) is empty, so there is no updater at all: nothing shown, asked or sent.
- **With an address**: the first start asks once, "Check for updates when the launcher starts?" (no network before
  the answer; remembered in `<data folder>\updates\updater.ini`). Setting `system.update_check` (off in every preset,
  a preference). "Check now" on the Disc page works with it off.
- **A newer version**: a window with the version, date, notes and size: "Update now" / "Not now" / "Skip this
  version" (that version is never asked about again at start; "Check now" still shows it).
- **Update now**: downloaded into `<data folder>\updates`, size and SHA-256 checked against the Ed25519-signed
  manifest, unpacked into `game-<version>.partial` next to `game\`, renamed to `game-<version>` when complete, then
  `game_folder.txt` (next to "Pokemon XD.exe") says to start it. The starter opens it the next time ("Restart now").
  Any failure: nothing changes, a plain message. The data folder is never touched.
- **Go back**: switches `game_folder.txt` back to the previous folder (kept); the version left is then skipped.
- Only in the player's folder (a launcher in `game\` or `game-<version>\` under "Pokemon XD.exe"); a build folder
  can check but not install. Data kept inside the game folder (portable mode in `game\`) is refused with a message.
- Code: `engine/update` (manifest, Monocypher Ed25519, WinHTTP, BCrypt SHA-256, zip via zlib, folders),
  `launcher_app.cpp` (`DrawUpdateCard`, `DrawUpdateWindows`), XD's names in `xd_launcher.cpp` (`SetUpUpdates`), the
  starter (`starter.cpp`). Tests: `update_test.exe`, `python tools/test_updater.py --build <bin>` (local feed).

## Original means nothing extra

With the Original preset the game runs exactly as on the GameCube, and nothing extra appears in the game.
An in-game settings overlay is planned only for the other presets; it will use
the same settings code as the launcher (`engine/settings_ui`).

## On Linux

The launcher builds and runs on Linux since 2026-11-11 (`xd_launcher` next to `xd`; docs/LINUX.md "Ninth pass").
**It has only ever run inside WSL, driven by its test script, and been looked at in the screenshots it takes of
itself: never on a real Linux desktop, never with a mouse.** The Linux package's start script opens it
(tools/package_linux.py); with a disc image on the command line, or `XD_NO_LAUNCHER=1`, the game starts
without it as before.

What is the same as on Windows: the Disc page (file window through SDL: the desktop's file chooser service, else
zenity), Play (starts `xd` with the disc, waits for it, reads the save again afterwards), Preset, Settings,
Controls, Mods, Save (several saves, backups, import / export), Reports (list, Export, play sessions, Delete).

What is different, and what the player sees:

| Windows | Linux |
|---|---|
| Updates card: check, install, go back | only "Version x.y.z": no updater (no signed Linux update exists; the launcher has no update address there and never goes online) |
| Icon chooser (program's styles, pictures from the disc) | one line: choosing another icon "isn't there on this system yet"; window and shortcuts show `icon.png` next to the program (the program's own icon) |
| Backgrounds: from the disc's pictures + drawn by the game | from the disc's pictures only (the game has no hidden render mode on Linux) |
| Shaders prepared before the first Play | not done (the game compiles them as it meets them, as the Linux game always did); `XD_LAUNCHER_SHADERS=1` tries it, untested |
| "Open folder" opens Explorer | `xdg-open`; a desktop without it gets a small window with the folder's path and "Copy the path" |
| Deleted saves go to the recycle bin | to the desktop's trash (freedesktop.org: `~/.local/share/Trash`, with its info file so the file manager can put it back); on another drive than the saves: refused with a message, never deleted |
| Start menu / desktop shortcut (.lnk) | "applications menu" (`~/.local/share/applications`) / desktop shortcut: a `.desktop` file starting `Pokemon XD.sh`; some desktops ask to "Allow launching" first |
| "Hide my Windows user name" in exports | "Hide my user name": `$USER` and `$HOME` are taken out the same way |
| Screenshots in exports as JPEG | kept as PNG (no JPEG writer on Linux yet) |

Scripted runs: `tools/linux/launcher_run.sh <run folder> <script file>` (own HOME, data folder, settings; the game
started by `play` is muted and off screen).

## On the Mac

Built (x86_64, Rosetta 2) and packaged as the main program of `Pokemon XD.app`, which starts the game
(`Contents/MacOS/xd`) when Play is pressed; never run on a Mac yet. Differences from Linux: `open` for folders, the
Finder (osascript, else `~/.Trash`) for deleted saves, Arial as the fall-back font, no shortcut buttons, no updater.
`XD_NO_LAUNCHER=1` starts the game directly. docs/MAC.md "Launcher on the Mac": what the workflow checks, what is
untested.

## How it is built (for developers)

- Everything the launcher asks of the system it runs on is behind `include/launcher_sys.h` (the program's folder,
  starting a program and waiting for it, opening a folder, Documents / desktop, the recycle bin, shortcuts, .ico
  reading): `launcher_sys_win.cpp` holds the Win32 calls, `launcher_sys_posix.cpp` the Linux half (plain POSIX
  where there is a choice: a Mac half can start from it). launcher_app.cpp itself includes no system header.

- `launcher/` is shared by every game (it never names one): the window (SDL3 with SDL's own 2D renderer)
  and the pages (Dear ImGui, the same version Aurora uses). The look (colours, spacing, the header, the page
  list's line-drawn glyphs, the Play button) is `ApplyStyle`, `DrawHeader`, `DrawNav`, `PlayButton` in
  launcher_app.cpp; the font is Inter (third_party/fonts, copied as `fonts\` next to the program), sized by the
  display's scale. The game fills in a `launcher::GameInfo`
  (`include/launcher.h`): names, folders, its settings, its disc check and save reader.
- `engine/settings_ui/` draws presets, settings and controls from the settings registry
  (engine/settings). It only needs Dear ImGui, so the in-game overlay can reuse it.
- `games/xd/launcher/xd_launcher.cpp` is XD's part: `xd_settings::Register`, `PortXD_JudgeDisc`
  (src/disc_verify.c, the game's own check), the folders (src/xd_user_folder.h), the save reader (save/).
- Settings are written only through `engine::settings::Registry` and the mod list through
  `engine::mods` (`ReadModListFile`, `AddNewMods`, `WriteModListFile`), never by hand.

A separate program rather than a mode of xd.exe: xd.exe reserves the GameCube's memory and starts the
graphics engine before anything else, which is heavy and can fail; the launcher must open at once, also when
the game can't start (no disc yet, a wrong disc, a graphics problem), and never touch game state.

## Testing

    set XD_SETTINGS_DIR=%TEMP%\launcher_test\settings   (settings.ini, mods.ini and mods\ from there)
    set XD_CARD_DIR=%TEMP%\launcher_test\card           (the memory card from there)
    set XD_WINDOW_OFFSCREEN=1
    set APPDATA=F:\some\test\appdata                    (the user folder, so the disc's icon goes there)
    set XD_SHORTCUT_DIR=F:\some\test\shortcuts          (the shortcut buttons write there, not to the Start menu)
    set XD_LAUNCHER_SCRIPT=disc:<iso>|wait:disc|page:settings|set:system.disc_timing=fast|shot:<png>|play|wait:game|quit
    xd_launcher.exe

**A test never writes the player's own settings.ini / mods.ini.** Give every test run its own `XD_SETTINGS_DIR`
(Python tools: `savepoints.private_settings(env, folder)`: an Original settings.ini of the run's own, no mods.ini;
not a copy of the player's files, so a test's result never changes with his settings). If one is forgotten,
the programs refuse by themselves: a scripted launcher (`XD_LAUNCHER_SCRIPT`), the game it starts, and any xd.exe
test run (scripted, replayed, off screen...; the one definition is `games/xd/src/xd_test_run.h`) read the player's
files but skip every write and print `test run: not saved to the player's settings (XD_SETTINGS_DIR not set)`
(`GameInfo::playerFilesReadOnly` here). A launcher a player starts saves as always. The proof, on a scratch APPDATA:
`python tools/test_settings_guard.py`.

The steps are listed at the top of `include/launcher.h`. Reports page: `page:reports`, `report:<n>` (1 = newest),
`exportoption:name=on|off`, `exportoption:dump=on|off`, `export:<file.zip>` (as if chosen in the Save window),
`deletereport`, `session:<n>` (the play-session window on session n, 1 = newest), `savesession:<file.zip>`. Make test reports with xd.exe's `XD_BUG_REPORT_AT` / `XD_TEST_CRASH` under a test APPDATA. xd.exe started by Play inherits the environment
(so the same test settings and card, and XD_INPUT_SCRIPT if set).
