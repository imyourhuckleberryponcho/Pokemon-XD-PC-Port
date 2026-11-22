# Your first mod in ten minutes

**What you will make:** a mod that gives the move Tackle a power of 120 and 30 PP, so you can see it work in the
first battle. It changes two numbers. No programming.

**You need:** the port installed, your own disc image set up in the launcher, and a text editor (Notepad is fine).

## Steps

1. **Open the mods folder.** Launcher, **Mods** page, **Open the mods folder**.

2. **Make a folder** for your mod inside it. Call it `strong-tackle`. Use only letters, digits, `-`, `_` and `.`.

3. **Make `mod.json`** inside `strong-tackle`. This is the mod's name card:

   ```json
   {
     "id": "strong-tackle",
     "name": "Strong Tackle",
     "version": "1.0",
     "author": "Your name",
     "description": "Tackle has 120 power and 30 PP.",
     "game": "GXXE01",
     "applies": "new_playthrough"
   }
   ```

   `game` is the disc's game ID. `GXXE01` is the USA disc, the one the port uses.

4. **Make a `data` folder** inside `strong-tackle`, and a file `tackle.json` in it:

   ```json
   {
     "moves": {
       "Tackle": { "iryoku": 120, "pp": 30 }
     }
   }
   ```

   `moves` is the table of moves. `Tackle` is the entry. `iryoku` is the game's own word for power. `pp` is PP.

   Your folder now looks like this:

   ```
   strong-tackle/
     mod.json
     data/
       tackle.json
   ```

5. **Let the launcher find it.** Open (or restart) the launcher. On the **Mods** page you will see "Strong Tackle",
   switched off. Switch it on.

6. **Start a New Game.** Because the mod is `new_playthrough`, it is chosen on the New Game setup screen. Start the
   game with it on.

## How to test it

- At start-up the game may show a message window. A good start has none. If there is one, it names your mod and the
  problem. See [troubleshooting.md](troubleshooting.md).
- The newest log file in the `logs` folder of the data folder (`play_<date>.log`) has a line like `Data mods: 2 number(s) set in 1 table(s)`. That means
  both of your numbers were applied.
- In the first battle, use Tackle and look at the damage and at the PP in the move list.

If you have the command-line tools (see [change-data.md](change-data.md#tools-you-can-run)), you can check without
starting the game: `xd_data_mods <disc> --mods` prints what every mod that is on changes.

## Common mistakes

- **The mod is there but nothing changes.** It is switched off. Check the Mods page.
- **A comma problem.** JSON wants commas between items and no comma after the last one. A broken file gives the
  message "not valid JSON".
- **A wrong name.** `"Tackle"` must match the game's name for the move. Capital letters, spaces and signs do not
  matter, but spelling does. The game's start-up message lists names it does not know.
- **A folder inside a folder.** `mod.json` must be directly in `strong-tackle`, not one level deeper. If you unpack a
  zip, check this.
- **You changed the mod but the game did not notice.** Mods are read when the game starts. Restart the game.
- **An old save.** A `new_playthrough` mod changes the world, so it belongs to a New Game. Saves remember their mods.

## See also

- Change more things: [change-data.md](change-data.md)
- Add a brand-new move: [add-pokemon-moves-items.md](add-pokemon-moves-items.md)
- Change how battles play: [battle-scripts.md](battle-scripts.md)
