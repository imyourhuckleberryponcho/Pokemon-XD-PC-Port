# third_party/

Outside libraries (Aurora, SDL, Dear ImGui) and the launcher's font, each with its license. Nothing from the game goes here.

| Library | Source | Pinned version | License |
|---|---|---|---|
| Aurora | https://github.com/encounter/aurora | commit `5794daa2ccbd3d2ddf01af788e51a8612850ffd1` (2026-11-03) | MIT |
| SDL3 | https://github.com/libsdl-org/SDL | tag `release-3.4.20` (commit `f00dc919ca35`) — the version Aurora pins | zlib |
| Borealis | https://github.com/encounter/borealis | commit `8e9427fab92c1935a5d6bacadc6053c687f10c1e` (2026-10-28) | MIT |
| RenderDoc in-app API header | `renderdoc/renderdoc_app.h`, from RenderDoc 1.47 (https://renderdoc.org) | 1.47.0 | MIT |
| Inter (font) | https://github.com/rsms/inter, release `Inter-4.1.zip` (`extras/ttf`) | 4.1 | SIL Open Font License 1.1 (`fonts/inter/OFL.txt`) |
| Lua | https://www.lua.org, `lua-5.4.8.tar.gz` (sha256 4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae) | 5.4.8 | MIT (`lua/LICENCE.md`) |
| Monocypher | https://monocypher.org, `monocypher-4.0.2.tar.gz` (SHA-512 checked against the site's `.sha512`) | 4.0.2 | BSD 2-clause or CC0 (`monocypher/LICENCE.md`) |

`monocypher/` holds four unchanged files of Monocypher (`src/monocypher.c/.h`, `src/optional/monocypher-ed25519.c/.h`)
and its licence. The launcher's updater uses only its Ed25519 signature check (with SHA-512), to make sure an update
was published by the port's publisher (docs/UPDATER.md; engine/update). Small, audited, widely used, no other
dependencies. To update: replace the four files from a newer release, rebuild, run `update_test.exe` (it checks
RFC 8032's test vectors).

`lua/src` holds the 25 C files and their headers of Lua 5.4.8 that the script sandbox needs, **unchanged** (the interpreter
lua.c, the compiler `luac.c` and the libraries for files, the OS, DLLs, debug, coroutines and UTF-8 are left out on purpose:
the mods' scripts must have no way to reach the PC). `engine/script` builds them as `lua54` and decides the two things that
would make a script's result differ from run to run (the string-hash seed and `table.sort`'s random pivot) in two small
wrapper files that `#include` Lua's `lstate.c` / `ltablib.c`. To update: replace the files from a newer 5.4.x tarball,
rebuild, run `script_test`. See docs/MODDING_NEXT.md part B.


`fonts/inter/` holds three files of the Inter font family (Inter-Regular, Inter-SemiBold, InterDisplay-Bold), the
launcher's font. The build copies them and the licence into a `fonts` folder next to the launcher
(launcher/CMakeLists.txt, `launcher_copy_fonts`); tools/package_release.py puts them in the player's folder. The
OFL allows bundling and redistributing them with a program as long as the licence goes with them and they aren't
sold on their own. Without them the launcher falls back to Windows' Segoe UI.

Borealis is Aurora's companion library. The port builds only two parts of it: `borealis::data` (where the
player's files live: `%APPDATA%\Pokemon XD`, or a portable folder; platform/os/user_folder.cpp) and
`borealis::file_select` (the system's "open file" window for the disc image; platform/os/disc_dialog.cpp). They use
the SDL, fmt and JSON libraries Aurora already has. The rest of Borealis (HTTP, updates, crash reports, Discord,
UI, ...) is switched off in CMakeLists.txt.
Thanks to **Count-Sheep** (github.com/Count-Sheep), who brought Borealis into the port with the data folder,
the portable mode and xd.exe's disc picker (pull request #1 on the public repository).

`renderdoc_app.h` is only the header for the debugging switch `XD_RENDERDOC` (games/xd/src/renderdoc_capture.c);
RenderDoc itself is not part of the port and is only loaded when that switch is set and RenderDoc is installed.

Aurora, SDL and Borealis are plain git clones. To update one: `git -C third_party/<name> fetch` and check out the new
commit, update this table, rebuild. When updating Aurora, keep SDL on the version listed in
`aurora/cmake/AuroraDependencyVersions.cmake` (`AURORA_SDL3_REF`).

## Downloaded at build time (not stored here)

Aurora's build downloads its own helper libraries into `build/<preset>/_deps` on the first build:
Dawn (prebuilt, the WebGPU graphics backend — D3D12/Vulkan), nod (prebuilt, disc images), abseil,
fmt, zlib-ng, libpng, FreeType, Dear ImGui, SQLite, zstd, xxHash, Tracy, nlohmann/json.
Their exact versions are pinned inside Aurora (`aurora/extern/CMakeLists.txt`,
`aurora/cmake/AuroraDependencyVersions.cmake`). Deleting `build/` means they download again.

## Patches

`patches/aurora/*.patch` are the port's changes to Aurora. The build applies them to the Aurora checkout
automatically (CMakeLists.txt, at configure time), and stops if one no longer applies after an update.
Each is small and could go upstream:
- `0020-gamecube-rumble-off.patch`: a GameCube controller (adapter) whose profile has no rumble strength keeps its
  motor off. Its motor is only on/off, so Aurora ignored the strength; the port's "Rumble off" setting
  (`gamepadN.rumble`, platform/input/controls_apply.cpp) sets the strength to 0, which already stops every other
  controller.
- `0021-frame-replay-view-override.patch`: for the photo mode (platform/video/photo_mode.cpp). `AuroraSetCommandTap`
  shows every processed command chunk, chunks ending at each display copy, so a whole picture's commands can be kept
  and written again; `AuroraSetViewOverride` draws perspective scenes over the whole picture through another camera
  (a matrix after each position matrix, its rotation on normals and lights, the field of view scaled) and can skip
  2D draws (orthographic, sampling a texture that isn't an EFB copy); `AuroraSaveGXState`/`AuroraRestoreGXState`
  put the decoder's state back afterwards. Nothing changes unless they are used.
- `0001-unsized-vertex-arrays.patch`: `GX_ARRAY_SIZE_UNKNOWN` for `GXSetArray`. GameCube games don't
  pass array sizes; each draw then uploads up to the highest index it uses, which is what the hardware
  reads.
- `0002-viewport-depth-clamp.patch`: viewport depth -0.0 clamped to 0.
- `0003-raw-efb-copy-registers.patch`: EFB copies set up with raw BP registers (games with their own GX
  layer write these instead of calling GXSetTexCopySrc/Dst and GXCopyTex).
- `0004-raw-texture-address.patch`: the texture address register (image3) works as on the hardware:
  GXInitTexObj fills it, and a raw write points the texture slot at that address in main memory.
- `0005-raw-tlut-registers.patch`: palettes (TLUTs) loaded with raw BP registers, as on the hardware:
  GXInitTlutObj fills the address in loadTlut0; a load (0x65) with a TMEM offset reads the palette from
  that address, and a texture's TLUT register (0x98-0x9B, 0xB8-0xBB) picks the palette and its format.
  Without it XD's own GX layer left every paletted texture on whatever palette was loaded last.
- `0006-call-display-list-by-address.patch`: `GXCallDisplayListAt` writes the hardware's "call display
  list" command (0x40, address, size) for a list in main memory, and the decoder runs that list when it
  reaches the command, as the chip reads it from memory then. The port uses it when recording into a
  game FIFO (platform/sdk/gx_fifo.cpp): copying whole lists in overfilled XD's layer FIFOs.
- `0007-channel-color-merge.patch`: `GXSetChanAmbColor`/`GXSetChanMatColor` as in the SDK: `GX_COLORn` changes
  only the register's RGB bytes, `GX_ALPHAn` only its alpha byte. Aurora wrote the whole word for either, so
  HSD's "material RGB, then alpha 0" left every lit model's material colour black (characters drawn black).
- `0008-missing-texcoord-reads-zero.patch`: a texgen reading a texture coordinate the vertex format doesn't
  send reads (0, 0), as the hardware and Dolphin do (XD's particle display lists); was a fatal error.
- `0009-efb-kept-between-frames.patch`: the EFB keeps its picture from one Aurora frame to the next, as the
  hardware's does (only a copy with clear, or a drawn clear, empties it). Aurora cleared it at every frame
  start; the port's frames end on video timing (platform/sdk/vi_clock.cpp), which can fall between the game's
  last draw and its GXCopyDisp, so the copy took a cleared EFB: the opening cutscene was black in optimised
  builds.
- `0010-arb-mip-check-mip-count.patch`: `arb_mip_check` skips textures with more mip levels than its arrays
  hold (11 for max_lod 10). Its size `CHECK` is compiled out in release, so it wrote past the arrays: a
  silent stack-overrun crash on the M2_earth_colo map.
- `0011-gxbool-one-byte-in-c.patch`: `GXBool` is one byte in C files too, as on the GameCube (`u8`). Aurora
  used `bool`, and the HSD library's C files get the decomp's own `stdbool.h` (`typedef int bool`, as Melee's
  CodeWarrior C had), so there `GXBool` was 4 bytes: the texture LOD records read from the disc
  (`HSD_TexLODDesc`: bias clamp, edge LOD, anisotropy) came out shifted, and textures lost edge LOD and
  anisotropic filtering (ship cutscene, frame 7050: 76 draws).
- `0012-viewport-origin-342.patch`: the viewport registers use the hardware's origin offset, 342, in both
  `GXSetViewport` and the register decoder. Aurora used 340 in both, which only works for its own calls: XD's
  GSgfx layer writes the viewport registers itself with 342 (as the SDK does), so every 3D scene was drawn
  2 pixels right and down of where the GameCube draws it (battle: RenderDoc viewport 4,4 at 2x; Dolphin and
  the FIFO log say 0,0).
- `0013-efb-528-lines.patch`: the EFB has the hardware's 528 lines, also when the display shows 480: games
  render textures into the lines below the picture. Aurora's EFB was only as tall as the display, so XD's
  512x512 battle shadow map (drawn with scissor 2..509, copied out whole) lost its bottom rows: the copy
  repeated the last row (long dark stripes behind every Pokemon's shadow) and HSD's white border was gone.
  The EFB textures get the extra lines at the same scale (`efb.cpp`); viewport, scissor and copy sizes are
  scaled by the displayed part only (`get_render_target_display_size`), so the display copy and the window
  are unchanged; when no display copy was made yet, the frame's end copies out the displayed lines.

- `0014-card-time-source.patch`: `CARDSetTimeSource(fn)`: where memory-card timestamps come from (a new file's
  modified time in its GCI header, a format's time). Aurora stamped them from the PC's clock (`getGCTime`), so a
  save made in two deterministic runs (XD_DETERMINISTIC) of one recording differed in its header. The port sets
  it to the virtual GameCube clock in deterministic runs only (platform/sdk/async_callbacks.cpp); otherwise
  nothing changes. (Still unlike the hardware: the SDK also restamps on CARDWrite/CARDSetStatus; Aurora doesn't.)
- `0015-texobj-zeroed-padding.patch`: `GXInitTexObj*` zero the whole texture object, padding included. `obj = {}`
  copied a temporary whose 3 padding bytes after `flags` were stack leftovers; texture objects live in game
  memory (GStexture), which then differed between two runs of one recording (found by the determinism check).
- `0016-widescreen.patch`: `AuroraSetWidescreen(aspect)` (setting video.widescreen, platform/video). Off (0, the
  default) changes nothing. On: the EFB gets the wide aspect; a viewport across the whole logical 640 width is
  stretched over it with its projection's x row scaled by 4:3 / wide (3D sees more at the sides, the game's 2D
  stays centred at its own shape); everything narrower (sub-viewports, scissor and copy rectangles) keeps its
  exact 4:3 place inside the centred 4:3 area, so pictures drawn for copying (XD's battle shadow map) line up
  with their copies; a full-width copy holds the wide picture. Orthographic draws that cover the whole width and
  are untextured or sample only EFB copies (fades, colour filters, a captured screen drawn back) are stretched
  instead (command_processor.cpp `wide_stretch_draw`). The 4:3 area starts on a whole pixel: a copy whose
  rectangle is a pixel wider than its texture came out wrong (dark battle floor).
- `0017-present-hooks.patch`: `aurora_set_present_hooks(before, after)`, called on the render worker around the
  primary surface's Present: frame pacing waits in `before` to hand pictures over on an even 59.94 Hz rhythm,
  and XD_PRESENT_STATS measures in `after` (platform/video/presentation.cpp).
- `0018-dawn-threads.patch`: Dawn's device used safely from Aurora's threads. Aurora calls Dawn from the GX command
  processor (game thread or its FIFO thread: textures, views, samplers, bind groups), the render worker (encode,
  submit, frees) and the pipeline thread, but never asked Dawn for `ImplicitDeviceSynchronization`, without which a
  Dawn device is not thread-safe: a rare crash inside `wgpuDeviceCreateBindGroup` (`bind_group_ref`, reading a
  small bad address) when several game copies ran at once. The patch requests that feature (logged at start-up:
  "implicit device synchronization on"), and moves the freeing of finished frames (frame packets: EFB and copy
  textures, views; the render worker's finished work items; expired bind groups) from the render worker to the
  recording thread's next `end_frame`, where the GX processor is idle: a stand-alone Dawn test showed that freeing
  bind groups on one thread while another creates them still corrupts the heap even with the feature on.
- `0019-fifo-color-capture.patch`: `AuroraReadbackCaptureColorInStream`: a full-resolution colour capture of the
  current EFB or offscreen pass written into the command stream (`GX_AURORA_READBACK_CAPTURE_COLOR`), so it is
  taken when the GPU side gets there, not when the CPU asks (`AuroraReadbackCaptureColor`). For commands a game
  records into a FIFO that is drawn later: xd.exe's icon renders (games/xd/src/render_icon.cpp).

- `0022-widescreen-efb-copies.patch`: widescreen (patch 0016) and copies of the screen. A copy of the whole width
  holds the wide picture and remembers it (`CopyTextureRef::wideUvScale`). An orthographic full-width draw that
  samples such a copy is stretched over the wide EFB whatever its shape (gx.cpp `wide_copy_stretch_draw`): screen
  effects that draw the captured screen back in pieces, strips, moved, turned or zoomed (battle transitions, the
  "lens" pieces of the trainer-battle swirl) put each piece where it came from, across the whole wide picture; 0016
  only stretched draws reaching both edges, so the rest showed the wide picture squeezed into the 4:3 middle. Any
  other draw (3D, sub-viewports, other 2D) reads a wide copy as the GameCube's 4:3 copy: its centred part
  (`texN_size_bias.w` = that part's width, shader `wide_copy_uv`). Off (no widescreen) changes nothing: the
  shader's coordinates go through unchanged (`select`, scale 0).

- `0023-gpu-wait-watch.patch`: waits for the render worker / GPU can't last for ever silently. A frame slot is only
  freed when the render worker has finished (submitted and presented) an older frame, and the game thread polled
  for one without any limit: a worker stuck in the driver, or a GPU device lost while Dawn never got to notice,
  froze the game for good (seen once in ~220 battle-sweep runs, 4+ copies on one GPU; reproduced once in ~90
  single-copy turbo runs: the worker was inside Dawn's `CreateBindGroup` in `webgpu::resample` for 20+ s while the
  pipeline thread compiled shaders with DXC; the cause was found later: patch 0025). Now: every wait for a frame
  slot, a staging buffer or `render_worker::synchronize` checks the device from 1 s on without Dawn's lock
  (`device_watch.cpp`: Dawn's `IsDeviceLost` and D3D12/D3D11 `GetDeviceRemovedReason`) and, when it is gone, ends
  with the same "Device lost" FATAL as Dawn's own callback (at once when only the driver knows; after 10 s when
  Dawn knows, whose callback is then reporting it). From 5 s on, every 5 s it logs what the worker is doing
  ("Waiting N s for a frame slot ...: render worker: end frame (submit, present) for N ms (frame F), pending,
  queued, done; staging map states"; `render_worker::state`). Nothing changes while frames flow (the checks start
  after 1 s of waiting). Testing: `aurora_debug_lose_device(removeDevice)` (D3D12 `RemoveDevice`, as a GPU reset,
  or Dawn `ForceLoss`) and `aurora_debug_stall_render_worker(seconds)` (xd.exe: `XD_TEST_CRASH=<frame>:gpu_*`).
- `0025-sampler-set-keeper.patch`: works around a Dawn bug (D3D12 only) that froze a tester's 0.2.0 game (HQ Lab,
  ~10 min in: the "Aurora FIFO processor" thread at 100% CPU inside `wgpuDeviceCreateBindGroup`, the game waiting
  for it). Dawn's D3D12 backend shares the sampler descriptors of bind groups that use the same samplers in the
  same order (`SamplerHeapCache`, an absl hash set with no lock of its own). Creating a bind group uses it under
  Dawn's device lock, but releasing one (`wgpu*Release`) takes no lock, and when the last bind group with a sampler
  set goes, its entry is erased from the set on the releasing thread. Aurora creates bind groups on the GX
  processor thread and the render worker and releases them on those and the recording thread, so an erase could
  run while another thread looked up or inserted: the set broke, and a later lookup probed for ever (with Dawn
  built from the same commit with symbols, the tester's spin is `raw_hash_set<SamplerHeapCacheEntry*>::find_large`
  in `SamplerHeapCache::GetOrCreate`), or the heap broke (the crash patch 0018 met, and the hang in
  `webgpu::resample` of patch 0023). Same code in upstream Dawn. Fix without changing Dawn
  (`lib/webgpu/sampler_keeper.cpp`): for every distinct sampler set, one extra small bind group (only those
  samplers, no textures) is kept until the device shuts down, so no entry for Aurora's bind groups is ever erased
  and the set only changes under Dawn's lock. All of Aurora's bind groups with samplers go through it
  (`bind_group_ref`, `create_bind_group`: resample, copy, EFB copy and palette conversion). Cost: one small bind
  group per sampler combination ever used. Check: `platform/tests/dawn_bind_group_race_test` (`plain`: bare Dawn,
  2 threads creating and 2 releasing: crashes within a second on D3D12, Vulkan and D3D11 pass; `keeper`: through
  the workaround, millions of bind groups without a fault). If `plain` passes on D3D12 after a Dawn update, the
  bug is fixed there and this patch can go.

- `0024-texture-source-observer.patch`: `aurora::texture::set_source_observer({wants, decoded, userData})`, for the PC
  button pictures (setting controls.button_icons, games/xd/src/button_icons.cpp). While an observer is set, a source
  key (Aurora's content hash, as for texture packs) is built for every texture resolved and `wants` is asked about
  it; for a texture it wants, `decoded` gets its base level as RGBA8 (decoded as the texture dump does) and may
  register a replacement for that key on the spot, which the lookup right after finds. So a program can make a
  replacement from the texture's own picture (repaint part of it) without keeping any of it on disk. No observer
  (the default): nothing changes, no keys are built.

- `0028-gpu-timing.patch`: Aurora's GPU profiler (timestamp queries per pass, `lib/webgpu/gpu_prof.cpp`) also works
  without Tracy: `aurora_set_gpu_timing(seconds)` (new header `aurora/gpu_timing.h`, before `aurora_initialize`)
  asks the device for timestamp queries and logs every `seconds` the GPU time per frame (average, p95, max) and per
  kind of pass ("EFB", "Staging copies", "Present blit"...), the bytes uploaded per frame (AuroraStats) and the
  render worker's CPU time to encode + submit a frame ("[gpu timing]" lines). Backends without timestamp queries
  (Dawn's D3D11) still log the CPU and upload lines. Zones per frame 127 -> 511. `aurora_set_gpu_fallback_adapter`
  (testing): the system's software adapter (WARP) as a very slow GPU. xd.exe: `XD_GPU_TIMING`, `XD_GPU_BACKEND`
  (docs/PERFORMANCE.md). Nothing changes when neither is called.
- `0029-texture-cache-sweep.patch`: `gx::texture::end_frame` swept its texture-object cache (every texture object
  used in the last 600 frames: tens of thousands of entries in XD, which sets its texture objects up anew each
  frame) on the game's thread at every frame end, ~8 % of that thread's time (docs/PERFORMANCE.md). Now every
  64th frame: unused entries live up to 64 frames longer, nothing else changes.
- `0030-shared-shader-cache.patch`: two programs on one cache folder (two copies of the game, a helper run). Aurora's
  SQLite caches (`dawn_cache.db`, `pipeline_cache.db`) had no busy timeout, so a write failed at once with "database
  is locked" while another program was writing: the compiled shader was not stored (compiled again next time), a
  busy database at start-up switched the cache off for the whole run, and the pipeline list's writer stopped for
  good. Now: busy timeouts (5 s at start-up and for the list's writer, 25 ms for a shader store while the game
  draws); a store that finds the database busy keeps its entry in memory and a helper thread stores it when the
  database is free (once more at shutdown); the writer keeps its batch and retries; the prune of unused entries
  only touches rows that existed when this program opened the database (never another running copy's new ones) and
  is skipped while the database is busy. `AuroraCacheWritesPending()` says how many entries still wait
  (xd.exe's shader preparation waits for 0 before it ends). docs/SHADERS.md "Two programs on one cache".
- `0031-frame-interpolation.patch`: for in-between pictures (setting `video.frame_rate`, platform/video/frame_rate.cpp,
  docs/FRAME_RATE.md). `AuroraInterpSetMode(RECORD)`: the decoder notes, for every draw whose transforms go to the
  GPU, what the draw is (primitive, vertex layout, vertex arrays, its indices) and its projection and position/normal
  matrices, one list per picture (ending at the display copy), the last two kept, and compares them
  (`AuroraInterpGetInfo`: how many draws were found again, moved, jumped). `AuroraInterpSetMode(REPLAY, blend)`: a
  kept picture's commands written again (patch 0021's tap) get each perspective draw's matrices blended with the
  same draw's in the picture before; draws that are new, orthographic or jumped are drawn as they are. Nothing
  changes while it is off.
  Also `AuroraGetFrameBufferUse` (how full the frame's fixed-size buffers are: a replay in the same frame needs
  room), and `AuroraRestoreGXState` (patch 0021) now keeps the vertex arrays uploaded in the frame known and
  uploads the restored state's arrays again when next used.
- `0032-frame-interpolation-steady-pieces.patch` (on top of 0031; docs/FRAME_RATE.md "Tests 2", "2D"): the pairing
  and the decision "blend or not" are made once, when a picture is whole, per set of transforms with all the draws
  that used it; a replay only looks the decision up (and blends nothing if the transforms it is about to send are
  not the recorded ones: `slipped` in `AuroraInterpInfo`). A draw's matrices are compared only in the slots its
  vertices really use (unused slots made skinned models "jump"); a cut is "more than half of the models found
  again jumped"; models whose vertex arrays change address every picture are found by their indices; sprites,
  letters and particles (positions in the command) are told apart by their corners and texture coordinates and
  blended only in steady motion (or when they hardly moved). New `AuroraInterpInfo` fields: fresh, unknown,
  unsteady, sprites, slipped.

`patches/aurora-32bit-experiment.patch`: the three small fixes Aurora needed to compile as 32-bit
(2026-11-04 experiment, ARCHITECTURE.md "hard parts" 2). Not applied (the port is 64-bit).
Apply with `git -C third_party/aurora apply ../patches/aurora-32bit-experiment.patch`.
