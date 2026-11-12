# engine/

Code shared by XD and (later) Colosseum: settings and presets, mod loader, save converters (GBA, memory card, Colosseum), dev console, editors. Must never check which game is running.

- `settings/`: settings registry (every setting declared once: name, description, type, category, games,
  works yet or not, Original/Enhanced values), presets (Original / Enhanced / Custom), settings.ini (versioned
  `format`, migration, unknown keys kept), run-only overrides (environment switches), the faithfulness check
  (`IsOriginal`, `Summary`; C: settings_c.h), the rules every setting keeps (`CheckRules`: every enhancement off
  in Original) and the docs list (`SettingsMarkdown` -> docs/SETTINGS.md). Test: `settings_test`.
- `controls/`: which keyboard keys and PC-controller buttons press each GameCube button, for all 4
  controllers (by position or by label, walk key, deadzones, L/R click points), the defaults (exactly
  the port's behaviour before remapping), button names and icon names per kind of controller, and the
  `[keyboardN]` / `[gamepadN]` sections of settings.ini. platform/input hands it to Aurora. Test: `controls_test`.
- `formats/`: file-format readers (LZSS, FSYS, REL) and described tables; writers for LZSS and for FSYS
  archives with replaced entries (`fsys_build.c`, for mods).
- `mods/`: mods layer 1 (include/mods.h has the folder layout and mod.json): mod folders, the player's mod
  list (mods.ini, profiles; new mods are added switched off), game ID / requires / conflicts checks,
  whole disc files or single FSYS entries replaced (later in the list wins, overlaps reported), handed to
  platform/disc's overlay. No mod on: the disc as it is. Test: `mods_test`.
- `gs/`: GSthread task switching (fibers). Test: `gsthread_test`.
- `disc_check/`: hash the player's disc image once (through platform/disc) and remember the result per file
  (full path + size + last-modified time); the game decides which hash is right. Test: `disc_check_test`.
