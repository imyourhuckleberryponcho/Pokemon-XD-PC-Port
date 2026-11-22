# Reload mods while the game runs

> Early version: the format can still change.

**What this page covers:** the developer setting that reloads script mods and texture packs without restarting the game, what it
reloads, and what still needs a restart. It is meant for people who make mods and want to try a change quickly.

## Turn it on

1. Open the game's **Settings**.
2. Find **Reload mods while playing (developers)** (setting `system.mods_hot_reload`) and switch it on. It is off in every preset.

## Reload

With the setting on, a reload starts in any of three ways:

- Press **F6**.
- Choose **Reload mods** on the in-game MODS page.
- Save a changed file in the mods folder. The game checks the folder about every two and a half seconds.

The reload waits for a quiet moment: no battle running and a map loaded. Until then the request waits, and the log says
`waiting for a quiet moment`. A short notice on the screen tells you what was reloaded.

## What is reloaded

| Change | Reloaded while playing? |
|---|---|
| `scripts/*.luau` of a mod that is switched on (and whose scripts are allowed) | Yes. The scripts load again, including names and hooks, and a script's own variables start over. A script that fails to load is reported as `not used` with the reason |
| `textures/` of a mod, and texture packs | Yes, when texture packs are in use in this run (not in Original, not with `XD_HD_TEXTURES=0`). A picture changes the next time it is drawn |
| Data values and text (`data/*.json`), replaced files (`files/`, `fsys/`, rooms, objects), new rows, which mods are on, and their order | No. The game reads these tables once at start-up. The reload notices the change and shows `Restart the game to use changes to ...`, but applies nothing of it |

## Safety

- The setting does nothing in the Original preset, with `XD_REQUIRE_ORIGINAL=1`, on Pokémon XG, and in test runs and replays.
- A reload never changes the game's own variables and never writes `mods.ini`. A mod found by the reload stays switched off until you switch it on.
- Every reload is noted in the log (`[mods reload] ...`) and in the recording of the run. A recording with a reload note is a run whose mods
  changed part way, so it cannot be played back as a normal replay.

## See also

- [battle-scripts.md](battle-scripts.md) for the scripts that can be reloaded
- [textures.md](textures.md) for texture packs
- [sharing.md](sharing.md) for checking a mod before you share it
