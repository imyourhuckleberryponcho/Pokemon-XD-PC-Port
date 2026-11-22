# Your own characters and models

> Early version: the format can still change.

**What this page covers:** putting a 3D model of your own on a trainer or a person in the game. You make or find a model in a common 3D format
(**glTF**, `.glb` or `.gltf`; Blender and most 3D programs export it), and a tool turns it into the game's own model file. The character keeps
every animation the game gives it (standing, walking, throwing the ball, the win pose), because the tool puts your model on the game's skeleton.

You need: the player's own disc (the tool reads the character you replace), Python 3 with `numpy` and `Pillow`, and your model.
No programming. Use only a model you made, or one whose licence lets you use it.

## What works and what does not

| | |
|---|---|
| A person's **model in a room** (a villager, a guide) | **Works** (seen in the game: a block robot stands where the guide did) |
| A **world object** that has its own small model file (the treasure chest) | **Works** with the same tool (seen in the game: the chest became a robot) |
| A trainer's **battle model** (the person who stands there during a trainer battle) | **Works** (seen in the game: in the first battle the player's trainer is a block robot, standing in the right place on the ground) |
| Your own animations (a walk you animated yourself) | **Not yet.** The game's own animations are used. Your model has no animation of its own |
| A new object that is part of a room's own model (a door, a wall, a statue in the room) | **Not yet.** A room's model is one big file; it cannot be replaced piece by piece |
| Giving a **new** trainer or person (not replacing one) | Not yet. You replace a trainer or a person that exists |

## How your model is put on the game's skeleton

The game's characters are a skeleton (about 60 joints) with animations, and a body made of pieces that follow the joints. The tool keeps the
**skeleton and the animations** of the character you replace (so you do not draw any animation) and swaps the **body** for yours:

1. **Automatic fit (the default).** Your model is made as big as the character and set on the same ground. Every triangle follows the bone
   of the skeleton that is nearest to it. The limbs move with the game's animations without any work on your side. The price: where two
   bones meet (an elbow, a hip) there is a hard seam, because every triangle follows exactly one bone. It looks fine on a block character or a
   robot and rough on a smooth one.
2. **Your own skin weights (`--joint-map`).** If your model is skinned in Blender, give a small file that says which of the game's joints each
   of your bones is (`{"upper_arm.L": 31, ...}`), and the model follows the game's joints smoothly.
3. **One joint (`--attach N`).** The whole body follows one joint: a prop, or a character that only moves with the body.

`python tools/model_convert/gltf_to_pkx.py skeleton <character.pkx> skeleton.json` writes the skeleton (the joints, where they are, which ones the
animations move), so you can pose your model the same way before you convert it. Put your model in the same stance as the game's character (arms down
or out, as the skeleton shows) for the best fit.

## Steps

1. **Take the original character out of your disc, into a folder of your own** (this is the game's data: keep it out of anything you share).
   The tool does it from your disc image:

   ```
   python tools/model_convert/gltf_to_pkx.py list <disc.iso> field_common.fsys
   python tools/model_convert/gltf_to_pkx.py extract <disc.iso> field_common.fsys 6 build/mana.hsd
   ```

   - A **trainer's battle model** is the only entry (number 0, type 15) of an archive called `pkx_<name>.fsys` (for example `pkx_rinto_0101.fsys` is the player's trainer).
   - A **person's model** is an entry of type 2 in `field_common.fsys` (the shared people) or in the room's own archive. A person in a story mod's
     `objects/<room>.json` has `"model": N`: `xd_data_mods <disc> --show peopleInfoData N` shows `modelResID`; divide it by 65536 to get the entry's **id**.
   - A **chest** is `t_box_00` in `field_common.fsys` (it opens with its own small animations).
   Use a copy under `build/`.
2. **Convert:**

   ```
   python tools/model_convert/gltf_to_pkx.py swap original.pkx my-character.glb my-model.pkx
   ```

   Options: `--height H` (the height in the game's units; the default is the original character's), `--turn DEGREES` (if yours faces the wrong
   way), `--flip-winding` (if you see the inside of your model), `--no-texture`, `--texture-size 128` (the picture is made a small square;
   the game's models use small pictures), `--attach N`, `--joint-map map.json`.
3. **Put it in a mod** in the place of the original: `fsys/pkx_<name>.fsys/#0.bin` for a trainer, `fsys/field_common.fsys/#6.bin` for the person above (the entry's number
   from `list`; see [sharing.md](sharing.md) for how a mod replaces a part of an archive). Check it with `xd_mod_check`, switch it on, start the battle.

## Things to know

- **The file you make holds the game's skeleton and animations** (the game's data, copied from your disc). It is for your own game and for
  testing; **do not share it**. A way to share the model itself (the port building the game's part from the player's own disc when the game
  starts, as it does for sound) is planned, not built. What you can share is your model (the glTF) and these instructions.
- The game's model memory is limited: the model file must stay under 600 KB. Keep the model low-poly (a few thousand triangles) and the picture
  small. The tool tells you the size.
- The tool makes **one picture** per material, as a compressed GameCube picture, so colours are a little coarser than in your file.
- A model has no lights or cameras of its own; the game lights it.

## See also

- [sharing.md](sharing.md): what may and may not be shared
- [add-pokemon-moves-items.md](add-pokemon-moves-items.md): the model of a new Pokémon
- [story-objects.md](story-objects.md): people in rooms
