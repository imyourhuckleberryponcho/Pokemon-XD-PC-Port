# Using mods

> Early version: the mod format can still change.

**What this page covers:** where the mods folder is, how to switch a mod on, when a mod takes effect and what it does to saves, and the rules every
mod follows.

## Where mods live

Each mod is one folder inside the **mods folder**:

- Windows: `%APPDATA%\Pokemon XD\mods`
- macOS: `~/Library/Application Support/Pokemon XD/mods`
- Linux: `~/.local/share/Pokemon XD/mods`
- With the "portable" setup: the `portable\mods` folder next to the program.

To find it, open the launcher, go to the **Mods** page and press **Open the mods folder**.

Texture packs live in a separate folder, `textures\GXXE01\<pack name>`, in the same data folder (see [textures.md](textures.md)).

## Switch a mod on

A mod in the folder is found the next time the launcher or the game starts. It is added to the list **switched off**, and a mod is never used until it
is turned on. (Texture packs are the one exception: they come in switched on, because they only change pictures.)

To turn a mod on, use one of three ways:

1. **The launcher's Mods page.** Switch the mod on. When two mods change the same thing, the one lower in the list wins; the page has buttons to
   move a mod up or down.
2. **The game's own MODS page** on the title screen (left of SETTINGS). Change the list, then choose Apply and restart.
3. **By hand.** Open `mods.ini` in the data folder (next to the mods folder) and write `your-mod-id = on` under the profile in use.

## When a mod takes effect

Every mod says how it works with saves in the `applies` field of `mod.json`:

| Value | Meaning |
|---|---|
| `any_time` | Looks and sound only (textures, fonts). Can be switched at any time |
| `restart` | Read when the game starts and not kept in the save (for example text). Switch it on the title menu; the game restarts. Saves are not touched |
| `new_playthrough` | Anything that changes the world, the rules or what is stored in a save (stats, trainers, new Pokémon, scripts). Chosen on the New Game setup screen; the save then remembers which mods it was made with. This is the default and the safe choice |

A save made with new Pokémon, moves or items belongs to the mods that added them. If it is loaded without those mods, the game refuses to read the
memory card instead of damaging it. See [change-data.md](change-data.md#saves-and-mods) and [troubleshooting.md](troubleshooting.md).

## Rules for every mod

- Never put game files in a mod: no pictures, sounds, models or text copied from the disc. Every player supplies their own disc. A mod holds only
  the author's own work and small instructions ("make this number 90"). See [sharing.md](sharing.md).
- A mod must never break saves.
- Mods are read when the game starts. After a change, restart the game. The exception is the developer setting in [reload.md](reload.md).

## See also

- [getting-started.md](getting-started.md): make a first mod
- [sharing.md](sharing.md): check, pack and share a mod
- [troubleshooting.md](troubleshooting.md): what the messages mean
