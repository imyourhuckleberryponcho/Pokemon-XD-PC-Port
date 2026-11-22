# HD textures

**What this page covers:** making and using a texture pack: new pictures that replace the game's pictures. Packs made for Pokémon XD in
the Dolphin emulator (game ID `GXXE01`) work as they are.

Only pictures change. The game's memory, logic, timing and random numbers are not touched. The "Original" preset never
uses a pack.

## Use a pack

1. Put the pack in the data folder (the folder the launcher's **Open the data folder** shows):

   ```
   <data folder>\textures\GXXE01\<pack name>\<pack files>
   ```

   A pack's own instructions may say "copy to `User\Load\Textures\GXXE01`". Put that folder's contents in
   `<data folder>\textures\GXXE01`.

2. Start the launcher. The pack shows up on the **Mods** page as "Textures: <pack name>", **already on**. (Texture
   packs are the one kind of mod that starts switched on.)

3. Restart the game. Packs are read when the game starts.

Other places that work, as in Dolphin: pictures loose in `textures\GXXE01` (one entry named "Textures: GXXE01"),
`textures\GXX`, and any folder under `textures` that holds a file called `GXXE01.txt`, `GXX.txt` or `all.txt`.

A mod can carry textures too: a `textures` folder inside the mod folder is used when the mod is on.

**Order is priority.** When two packs have a picture with the same name, the one lower in the list wins.

If a picture cannot be read, the game keeps its own picture, writes a line starting `texture_replacement:` in the log
and goes on. While a new picture loads, the game shows a small blurry version for a moment.

## Make a pack

1. In the game's settings, Video: switch on **Save the game's textures (for pack makers)** (`video.hd_textures_dump`).
2. Play the parts of the game you want pictures for. Each picture the game uses is written once as a PNG to:

   ```
   <data folder>\texture_dump\GXXE01\
   ```

   with the name Dolphin would give it, for example `tex1_64x64_0123456789abcdef_5.png`.

3. Switch the setting off.
4. Make your new pictures in your own art program. Keep the **same file name**. Put them in
   `<data folder>\textures\GXXE01\my-pack\`.
5. Add the pack to a mod folder if you want to share it. See [sharing.md](sharing.md).

The dump is the game's own picture, so keep it for yourself. **Do not share dumped pictures.** Share only your new
pictures.

## What the file name means

`tex1_<width>x<height>[_m]_<texture hash>[_<palette hash>]_<format>`

- `<width>x<height>`: the picture's size in the game.
- `_m`: the picture has mipmaps (smaller copies for distance). The port finds a file with or without it.
- `<texture hash>`: a fingerprint of the picture's bytes, 16 hex digits. You never need to calculate it: take it from
  the dump.
- `<palette hash>`: only for palette pictures.
- `<format>`: the GameCube picture format number.

Rules that help:

- File types: `.png` and `.dds`, any folder depth. A name that does not start with `tex1_` is ignored, so notes and
  thumbnails can sit in the pack.
- A new picture can be any size, best a whole multiple of the original (2x, 4x) with the same shape.
- Wildcards: `$` can stand for the palette hash (any palette) or for the texture hash.
- Extra mipmap levels: `<name>_mip1`, `_mip2`, ... next to the base file.
- DDS formats that work: uncompressed RGBA8 or BGRA8, BC1, BC3, BC5, BC7. Compressed files use much less graphics
  memory than PNG.
- Dolphin's newer "custom pipeline" files (shaders, materials, `.json`) are not read. Only the pictures.
- Two files with the same name: the pack with higher priority wins.

## Test it

- Start the game. In the newest file of the `logs` folder, lines starting `[hd textures]` say which packs are on.
- Look for the picture in the game.
- Test switch: set the environment variable `XD_HD_TEXTURES=0` to run once without any pack.
- **Faster tries (developers):** turn on the setting **Reload mods while playing (developers)** and press **F6** in the game (or just save a changed
  picture: the game notices within a few seconds). The packs are read again without restarting; a picture shows the next time it is drawn.
  Never in Original. See [reload.md](reload.md).

## Common mistakes

- A new name for a replacement. The name has to match the dump exactly.
- A pack in the wrong folder (it must be in `textures\GXXE01`, not `textures\GXXP01`).
- Forgetting to restart the game.
- Sharing the game's own dumped pictures.
- A very large pack on a weak PC. If it needs more graphics memory than the PC has, the game reloads old pictures as
  needed.

## See also

- [reload.md](reload.md): try a changed pack without restarting
- [sharing.md](sharing.md): what may and may not be shared
