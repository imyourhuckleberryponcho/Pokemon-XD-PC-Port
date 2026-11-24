# New move effects, new abilities and battle rules

> Early version: the format can still change.

**What this page covers:** new move effects (Flame Charge raising Speed, Volt Switch swapping out, Shell Smash changing five stats), new
abilities with real effects (Moxie, Sap Sipper, Prankster), terrains, and rules for one battle (a level cap, inverse types, weather at the start).
They are written as plain text files made of JSON blocks; no programming is needed.

Two small examples to copy are in the repository: `tools/examples/battle_mods/gen57-moves` (twelve moves) and
`tools/examples/battle_mods/gen57-abilities` (thirteen abilities and three rule sets).

## Where the files go

A mod folder gets a `battle` folder. Every `.json` file in it is read (in the order of their names):

```
my-mod/
  mod.json
  data/add.json         the new move rows (see add-pokemon-moves-items.md)
  battle/moves.json     what the new moves DO
  battle/abilities.json what the new abilities DO
```

The move rows say the number part (type, power, accuracy, PP, priority, which move's picture is shown: `"copy": "Ember"`). The files in
`battle` say what the move does **besides** that. A file has up to three parts:

```json
{
  "moves":     { "Flame Charge": { ... } },
  "abilities": { "Moxie": { ... } },
  "rule_sets": { "Level 40": { ... } }
}
```

A mistake in one entry leaves that entry out and says why in the log (`[mods] problem: battle effects: ...`, with the file and line);
everything else still works. The checker `xd_mod_check` is not taught about the folder yet.

## Moves

```json
"Scald": { "effects": [ { "status": "burn", "target": "target", "chance": 30 } ] }
```

A move that has `effects` gets the port's own effect script. A damaging move (power above 0) is "the plain hit, then your effects". A
move with power 0 is "the picture, then your effects"; it first has to hit if one of the effects is aimed at the target. A move with
**only** `power_rules` or `accuracy_rules` keeps the effect of the move it copies.

### The effect blocks

Each block does one thing. The ones that act on a Pokémon take `"target"`: `"user"` (default for stats, heal, hurt), `"target"` (the one
the move hit; default for status, remove_item) or `"foes"` (every foe on the field). `"chance": 30` rolls once, with the game's own
random numbers, when the block runs. `"when"` is a test (see the list below): the block runs only if it is true. A `"message"` is shown
after the block (`{name}` is the Pokémon's name).

| Block | What it does |
|---|---|
| `{"stat": "atk", "stages": 1}` | A stat stage changes by that much (-6 to 6). Stats: `atk def spe spa spd acc eva`. The game's own message, and the game's own protection (Mist, Clear Body ...) work |
| `{"status": "burn"}` | `sleep poison toxic burn freeze paralysis confusion flinch`; the game's own rules apply (a Pokémon with a status problem is not given another) |
| `{"heal": 50}` | The Pokémon gets 50 percent of its maximum HP back (the game's own heal picture) |
| `{"hurt": 12.5}` | It loses 12.5 percent of its maximum HP |
| `{"recoil": 25}` | **Moves only.** The user loses 25 percent of the damage the move dealt |
| `{"drain": 50}` | **Moves only.** The user gets half the damage the move dealt as HP |
| `{"switch_out": "user"}` | **Moves only.** After the hit the user goes back and you choose the next Pokémon (Volt Switch, U-turn) |
| `{"force_switch": true}` | **Moves only.** After the hit the target is forced out and the foe's next Pokémon comes in (Dragon Tail, Circle Throw). It uses the game's own Roar scene, so a foe with nobody left stays in |
| `{"weather": "rain"}` | `sun rain sand hail`. Only if no weather is on |
| `{"remove_item": true}` | The target's held item is lost (Knock Off) |
| `{"set_type": "move"}` | The user's type becomes the move's type (or a named one: `"Water"`) until it goes back |
| `{"field": "my_terrain", "turns": 5, "end_message": "..."}` | A field state you name, for some turns. Tests can ask for it with `"field:my_terrain"` |
| `{"message": "words"}` | Only a message |
| `{"terrain": "grassy"}` | `grassy electric psychic misty`: a terrain for five turns (see "Terrains" below) |
| `{"side": "aurora_veil"}` / `{"side": "sticky_web"}` | Aurora Veil (damage x2/3 for the user's side for five turns) / Sticky Web (a foe that comes in on the ground loses a Speed stage). Put `"when": "weather:hail"` on Aurora Veil |
| `{"set_ability": 152}` | The Pokémon's ability becomes that one (a number, or the name of one of the game's own) |
| `{"cure_status": true}` | The Pokémon's sleep / poison / burn / freeze / paralysis is cured |
| `{"hurt_dealt": 100}` | The Pokémon loses that percent of the damage the hit just dealt (Innards Out) |
| `{"moody": true}` | One random stat +2 and another one -1 |
| `{"restore_berry": true}` | The berry the Pokémon ate comes back (Harvest: put it in `end_of_turn`, once for 100 percent in sun, once with `"chance": 50` and `"when": "!weather:sun"`) |
| `{"transform": true}` | The Pokémon turns into the foe (the game's own Transform: its stats, types, moves and ability). `{foe}` in a `"message"` is the foe's name (Imposter, in `switch_in`) |

`{"stat": "best", "stages": 1}` raises the Pokémon's best stat (Beast Boost). `"set_type"` also takes `"target": "target"` (Soak). A move that has a
switch-out block **and** a status effect (Parting Shot) works too: the stats fall first, then the user goes back.

### More things a move can say

Next to `effects`, a move entry may hold (all optional):

| Key | What it does |
|---|---|
| `"crit": "always"` | every hit is a critical hit (Storm Throw), unless the target has Battle Armor / Shell Armor |
| `"ignore_boosts": true` | the target's Defense and Sp. Def changes do not count (Chip Away, Sacred Sword) |
| `"use_target_attack": true` | the damage uses the TARGET's Attack and its changes (Foul Play) |
| `"defend_stat": "def"` | a special move that meets the target's Defense (Psyshock) |
| `"first_turn_only": true` | fails unless the user came in this turn (First Impression) |
| `"power_formula": "weight"` | `weight` (Heavy Slam: the user's weight against the target's), `speed` (Electro Ball), `boosts` (Stored Power: 20 + 20 for each raised stage) |
| `"needs_physical_hit": true` | fails unless a physical move hit the user earlier in the same turn (Shell Trap; give it a late priority in the move's row). In plain words: the move waits for a hit |
| `"heats_up": true` | from the start of the turn until the move is used, a foe's contact move that hits the user burns that foe (Beak Blast; give it a late priority) |
| `"type_from_user": true` | the move's type is the user's own (first) type (Multi-Attack: a Silvally holding a Memory) |
| `"protect": "kings_shield"` | a Protect move (copy Protect as the donor) with a punishment for a contact move it stops: `kings_shield` (Attack -2), `spiky_shield` (1/8 HP), `baneful_bunker` (poison) |

More tests for `"when"`: `target_status:poison` / `user_status:burn` (`poison paralysis burn freeze sleep`), `terrain:electric` (`grassy electric psychic misty any`),
`target_grounded`, `user_grounded`, `hp_crossed_half`, `slower_than_target`, `target_first_turn`, and `{"move_in": ["Bite", "Crunch"]}` (the move is one of these;
a name the game does not have is left out).

### Changing the power or the accuracy

```json
"Hex": { "power_rules": [ { "when": "target_has_status", "mult": 2 } ] }
"Hurricane": { "accuracy_rules": [ { "when": "weather:rain", "accuracy": 255 }, { "when": "weather:sun", "accuracy": 50 } ] }
```

Every rule whose test is true applies, one after the other. `accuracy` is 1 to 100; 255 = never misses. Power rules go into the game's
own damage question, so **the trainer AI sees them too** when it picks a move.

### The tests (`"when"`)

One text or a list (all must be true). `"!"` in front turns it round (`"!contact"`).

`full_hp not_full_hp hp_below_half hp_above_half hp_below_third` (the user's), `target_full_hp target_hp_below_half`,
`status_move damaging_move contact physical special sound`, `type:fire` (the move's type), `user_type:ghost`, `target_type:grass`,
`weather:rain` (`sun rain sand hail none any`), `has_status no_status target_has_status target_no_status` (a status problem),
`user_has_item user_no_item target_has_item target_no_item`, `first_turn`, `move_has_secondary`, `super_effective not_very_effective`,
`move:Hex`, `field:name`.

## Abilities

```json
"Moxie": { "id": 153, "text": "Attack rises after a KO.", "ko": [ { "stat": "atk", "stages": 1 } ] }
```

- `"id"`: the ability's number, **124 to 255** (the game's own use 1 to 76; the Gen 4 pack 77 to 123). `0` or no `"id"`: the next free
  number from 124. A Pokémon has the ability when its row's `tokuseiDataId` holds that number.
- `"text"`: the line the summary shows. The name shown is the entry's name in capitals.

| Key | When it works |
|---|---|
| `switch_in` | the Pokémon comes in. `target: "foes"` reaches the foes (Intimidate) |
| `end_of_turn` | at the end of every turn, after the game's own checks (weather damage, Leftovers ...) |
| `ko` | its move knocked out the target |
| `contact` | a move that makes contact hit it (the owner is `user`, the attacker is `target`) |
| `switch_out` | it goes back (the heal has no picture) |
| `before_move` | its move is about to start (`set_type`: Protean) |
| `damage_dealt`, `damage_taken` | `{"when": ..., "mult": 1.3}` rules on the damage it deals / takes |
| `stat_mods` | `{"stat": "atk", "mult": 2}`: atk spa (its damage), def spd (damage it takes), spe (its speed). Attack and Defense are done as damage factors, not as the stat itself |
| `absorb` | `{"type": "Grass", "stat": "atk", "stages": 1}` or `{"type": "Water", "heal": 25}`: a damaging move of that type does nothing to it, and it gains |
| `acts_as` | `"Clear Body"`: for every check the **game itself** makes (Clear Body, Levitate, Static, Intimidate, Shed Skin ...) this ability counts as that one of the game's. This is the way to copy a game ability and add to it |
| `stat_drop_block` | stats foes cannot lower (`["all"]`, or `["atk"]`). Works for the **extra effects** of moves (the 10 percent Attack drop); a move or ability that lowers a stat for certain (Leer, Intimidate) is stopped by `"acts_as": "Clear Body"` instead |
| `status_block` | statuses foes cannot give it by a move's extra effect (the same limit) |
| `priority` | `{"when": "status_move", "add": 1}`: the moves it uses go first (Prankster) |
| `reflect_status` | `true`: a move the game lets Magic Coat reflect goes back at its user (Magic Bounce) |
| `remove_secondary` | `true`: its damaging moves lose their extra-effect rolls (Sheer Force; add a `damage_dealt` rule with `"move_has_secondary"` for the bonus) |
| `hit` | a damaging move hit it and it is still up (`when` can test the move: `type:dark`, `physical`, `hp_crossed_half`). The owner is `user`, the attacker is `target` |
| `hit_dealt` | its own damaging move hit (`when: contact`): Poison Touch |
| `fainted` | it was knocked out by a hit (Innards Out: `hurt_dealt`) |
| `stat_dropped` | a foe lowered one of its stats (Defiant, Competitive). It runs a moment later: at the next hit or the end of the turn |
| `any_faint` | another Pokémon on the field was knocked out by a hit (Soul-Heart: Sp. Atk +1). The owner is `user`. Not by poison, weather or recoil |
| `no_contact` | `true`: its own moves never make contact (Long Reach): the foe's `contact` effects (Mummy, Pickpocket ...) do not run after its hits. The game's own contact abilities (Rough Skin, Static) still do |
| `contrary` | `true`: stat changes it gets are turned round (only the ones that come through a move's extra effect or an ability) |
| `convert` | `[{"from": "Normal", "to": "Ice", "mult": 1.2, "when": "sound"}]`: its moves of that type become another type, and are stronger by `mult` (Aerilate family, Liquid Voice). `from` missing = any type. A move that becomes Fairy needs "Modern types" |
| `aura` | `{"type": "Dark", "mult": 1.33}`: every move of that type on the field is stronger while this Pokémon is out (Dark Aura); `breaks_auras: true` makes them weaker instead (Aura Break) |
| `immune_moves` | `["Shadow Ball", ...]`: these moves do nothing to it (Bulletproof) |
| `accuracy_against` | `[{"when": "status_move", "accuracy": 50}]`: foes' moves can be at most this accurate against it (Wonder Skin) |
| `accuracy_dealt` | `[{"mult": 1.1}]`: its own moves are this much more accurate (Victory Star) |
| `crit_when` | tests: its damaging moves are critical hits when they are true (Merciless: `target_status:poison`) |
| `unnerve` | `true`: its foes' berries have no effect while it is out (Unnerve) |
| `disable_on_hit` | `30`: a percent. A damaging move that hits it is disabled for its user (the game's own Disable) (Cursed Body) |
| `poison_any_type` | `true`: poison from its moves (the poison status moves, Toxic) works on Steel and Poison types (Corrosion). Seen working: the Steel Metagross was badly poisoned and lost HP at the turn end |
| `infiltrator` | `true`: its moves ignore the foe's Reflect, Light Screen, Aurora Veil, Safeguard and Mist, and its hits go past a Substitute (Infiltrator) |
| `strong_wind` | `true`: Delta Stream. While it is out the game's 6th weather (strong winds) is on: no other weather can start (a weather move fails) and Flying Pokémon take no extra damage from Rock, Ice and Electric moves. It ends when the last Pokémon with it has left or fainted (0.7.3) |
| `illusion` | `true`: Illusion. Sent out, the Pokémon shows the name, model and status face of the last Pokémon of its party that has not fainted. Its stats, types and moves stay its own. The first damaging hit from another Pokémon ends it (a Substitute that takes the hit, recoil and its own damage do not) and says "... Illusion wore off!" (0.7.3) |
| `berry_eaten` | a list of blocks that run when its owner has just eaten a berry (Cheek Pouch: `{"heal": 33.3}`). It runs right after the move in which the berry was eaten (round 3; it used to wait for the end of the turn) |
| `half_hp` | a list of blocks that run when a hit takes its owner to half HP or less (it did not faint). `{"switch_out": "user"}` sends the owner back at the END of the move that hurt it (Baton Pass's own switch-out script, where U-turn's runs; Wimp Out / Emergency Exit). Seen to trigger and never to stop the battle; a wild Pokémon with it runs away from the battle (the game's own Teleport ending) |
| `dancer` | `["Swords Dance", "Dragon Dance"]`: right after another Pokemon uses one of these moves, the first owner of this ability among the others uses the same move at once (its own message, PP line and effects; no turn is used) (Dancer) |
| `type_by_item` | `[{"item": "Fire Memory", "type": "Fire"}]`: its type follows the item it holds (RKS System). An item the game does not have is left out |

## Two-turn moves

```json
"Freeze Shock": {
  "charge": { "donor": "Razor Wind", "message": "{name} became cloaked in a freezing light!" },
  "effects": [ { "status": "paralysis", "target": "target", "chance": 30 } ]
}
```

`"charge"` makes the move a two-turn move (0.7.3: `"untouchable": true` on a vanish move (donor Fly) makes the vanished turn out of reach of every move, as Phantom Force; `"carry": true` makes it Sky Drop: the target is lifted with the user, hidden, loses its turn, and is let go after the hit; a target that is a Flying type, weighs 200 kg or more or stands behind a Substitute makes it fail): the first time it is used the user only charges up (the words in `message`), the next turn it is locked into the move and the
move happens. `"donor"` is one of the game's own two-turn moves (Razor Wind, Skull Bash, Sky Attack, SolarBeam): the game's own script of that move is used (the lock, the PP, the
messages of the hit), only the words of the first turn are yours. The move's `effects` run after its hit (a move with no damage, such as Geomancy: with the second turn). Moves that
vanish for the first turn: use `"donor": "Fly"` (or Dig, Dive, Bounce: one script for all four). The script is copied, its "which move is this" tests and its two state commands answer for your move,
so the user is out of reach exactly as the donor is (the game's own in-the-air / underground / underwater state; the few moves that hit a Fly user also hit yours). The words of the vanish turn are the donor's ("flew up high"), `message` is not used for them. Up to eight such moves in all the loaded mods.

## Trapping moves

`{"status": "trap", "target": "target"}` (with `"when": "!target_type:ghost"` to leave Ghost-types out) gives the target the game's own "cannot switch / cannot escape" state, the one Mean Look, Block and Spider Web set. The game's menu and its trainer AI refuse the switch, and the game ends the state when the user leaves or faints. Example: Thousand Waves, Spirit Shackle, Anchor Shot in the Gen 5-7 pack.

## Hidden abilities (a placeholder rule)

A species can have a third ability slot, the "hidden ability". It only exists in the Gen 5, 6 and 7 rule sets, and only for Pokémon that have it: a wild Pokémon at a Poké Spot has it 1 time in 20, and the Ability Patch (sold at the
Mt. Battle prize desk) gives any Pokémon its hidden ability. A mod says which species have one, in any `battle/*.json`:

```json
{ "hidden_abilities": { "Eevee": "Adaptability", "558": 161 } }
```

The key is a species name or a National Dex number, the value an ability name or number. The Gen 5-7 pack already has them for every species of the Ultra Sun / Ultra Moon list. The rule itself is in one place
in the game's code (`games/xd/src/hidden_ability.cpp`, "THE RULE").

## Terrains

`{"terrain": "electric"}` in a move or in `switch_in` of an ability starts a terrain for five turns (a new one replaces the old). Numbers of the 7th generation:
the matching move type of a Pokémon on the ground is x1.5 (Grassy: Grass, Electric: Electric, Psychic: Psychic); Misty halves Dragon moves against a Pokémon on the ground;
Grassy halves Earthquake / Magnitude / Bulldoze against it and gives every Pokémon on the ground 1/16 of its HP back at the end of the turn; Electric keeps a Pokémon
on the ground awake; Misty keeps status problems and confusion off it. Psychic Terrain does not stop priority moves yet. "On the ground" = not a Flying type and not Levitate.

The ability's effect is looked up when the game asks, so it works for a Pokémon whose ability a script changed (`battle.ability`), too.

## Rules for one battle

```json
"rule_sets": { "Level 40": { "level_cap": 40, "inverse": false, "weather": "rain", "banned_items": ["Leftovers"] } }
```

`level_cap` (Pokémon above it are lowered **for the battle**), `level_all`, `banned_items` (held items of these have no effect),
`inverse` (the type chart turned round: super effective and not very effective swap, immune becomes super effective), `weather`
(rain, sun, sand, hail when the first Pokémon comes out), `force_double` (accepted, **not built yet**).

A Pokémon whose level a rule changed gets no experience and no level-up from that battle; EVs, PP and status carry over.

A script starts a rule set for the next battle with `battle_rules("Level 40")`, for example from `on_battle_start`. It ends by itself
when the battle ends. `battle_rules()` ends it at once.

## What the trainer AI knows

New damaging moves are asked through the same damage question as every move, so the AI knows their power rules and the abilities'
damage factors. It does not know that Sap Sipper takes a Grass move, that Magic Bounce reflects a move, or what a status move's
blocks do (it uses its usual rules for a move with no power).

## Online battles and replays

The text of the `battle` files is part of what both PCs must have: the room refuses a PC with other definitions (the digest is in the
room fingerprint and in the recording's header). Blocks only use the game's own random numbers at the moment they run, in the order of
the blocks, so the same mods and the same choices give the same battle on both PCs and in a replay.

## Not built yet

`force_double`, the Luau route for effects (a script that returns blocks), field states that
change damage by themselves (`field` only starts the state; tests can read it), bag items banned by a rule set.

## See also

- [battle-scripts.md](battle-scripts.md): Luau scripts for battles
- [add-pokemon-moves-items.md](add-pokemon-moves-items.md): the move and ability rows these effects belong to
- [story-scripts.md](story-scripts.md): `battle_rules("name")` starts a rule set from a script
