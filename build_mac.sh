#!/bin/bash
# Builds the Mac version (docs/MAC.md). NEVER RUN BY ITS AUTHOR: written on a Windows PC without a Mac; the only
# place it runs so far is GitHub's Mac machines (.github/workflows/mac.yml).
#
#   ./build_mac.sh              configure (preset mac-release) + every target of the list below
#   ./build_mac.sh xd           only these targets
#   XD_KEEP_GOING=1 ./build_mac.sh    go on after errors and list all of them (first builds on a new system)
#   XD_COMPILER_CACHE=1         compile through ccache when it is installed: a later build only compiles again
#                               what changed (GitHub's Mac minutes: .github/workflows/mac.yml keeps the cache)
#   XD_GAME_FIRST=1             compile the port's and the game's own files for xd before the libraries: the
#                               code that has never met Apple's compiler shows its errors in the first minutes
#
# Needs: Xcode's command line tools (clang), cmake, ninja, python3 with "pip install libclang", and for Apple
# silicon Macs Rosetta 2 (softwareupdate --install-rosetta). The program is x86_64 on purpose (docs/MAC.md).
# Output: build/mac/mac-release/bin; then games/xd/mac/package_mac.sh makes the app and the zip.
set -u
cd "$(dirname "$0")" || exit 1
PRESET=mac-release
# The Linux list (build.sh) plus the game and the launcher (xd_launcher: the app's main program, docs/MAC.md
# "Launcher on the Mac"). Not on the Mac: the updater (as on Linux).
MAC_TARGETS="settings_test controls_test memcard_test save_backups_test gen3_save_test randomizer_test
  gsthread_test xd_save_test xd_save_convert_test xd_gba_test xd_settings_test
  dsp_decode_test dsp_core_test
  disc_check_test mods_test data_mods_test playthrough_test disc_overlay_test gc_fused_test disc_drive_test
  xd_disc_list xd_dsp_test xd_dsp_run
  platform_compat platform_sdk platform_os platform_audio platform_input platform_video
  xd_disc_check xd_data_dump xd_save_dump xd_randomize xd_gba_trade xd_rel_link_test
  xd_settings xd_data_mods xd_save_convert xd_mods_check
  os_threads_test gc_memory_test gx_fifo_test card_timing_test input_record_test game_clock_test
  keyboard_defaults_test controls_apply_test button_art_test low_arena_test dsp_equiv_test
  xd_launcher pbr_models xd"
EXTRA_ARGS=""
[ -n "${XD_SNAPSHOT_OVERRIDE:-}" ] && EXTRA_ARGS="$EXTRA_ARGS -DXD_SNAPSHOT_OVERRIDE=$XD_SNAPSHOT_OVERRIDE"
[ "${XD_BUILD_GAME:-1}" = 0 ] && EXTRA_ARGS="$EXTRA_ARGS -DXD_BUILD_GAME=OFF"
# (the Python that has the libclang package, when it is not the first one CMake finds)
[ -n "${XD_PYTHON:-}" ] && EXTRA_ARGS="$EXTRA_ARGS -DPython3_EXECUTABLE=$XD_PYTHON"
if [ "${XD_COMPILER_CACHE:-0}" = 1 ]; then
  if command -v ccache >/dev/null 2>&1; then
    EXTRA_ARGS="$EXTRA_ARGS -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
    echo "build_mac.sh: compiling through ccache"
  else
    echo "build_mac.sh: XD_COMPILER_CACHE=1 but ccache is not installed: compiling without it"
  fi
fi
# shellcheck disable=SC2086
cmake --preset "$PRESET" $EXTRA_ARGS || { echo "build_mac.sh: configuring failed"; exit 1; }
if [ $# -gt 0 ]; then TARGETS="$*"; else TARGETS="$MAC_TARGETS"; fi
if [ "${XD_BUILD_GAME:-1}" = 0 ]; then TARGETS="${TARGETS%xd}"; fi
KEEP=""
[ "${XD_KEEP_GOING:-0}" = 1 ] && KEEP="-- -k 0"
if [ "${XD_GAME_FIRST:-0}" = 1 ] && [ "${XD_BUILD_GAME:-1}" != 0 ]; then
  # Every object file xd is made of that is ours (not under third_party/ or the downloaded libraries), asked of
  # ninja by name. What they need first (generated headers) ninja builds by itself.
  OWN="$(ninja -C "build/mac/$PRESET" -t inputs xd 2>/dev/null | grep '\.o$' | grep -v -e '^third_party/' -e '^_deps/' -e '/third_party/' -e '/_deps/')"
  COUNT="$(printf '%s\n' "$OWN" | grep -c . || true)"
  echo "build_mac.sh: the port's and the game's own files first ($COUNT files)"
  if [ "$COUNT" -gt 0 ]; then
    FIRST_KEEP=""
    [ "${XD_KEEP_GOING:-0}" = 1 ] && FIRST_KEEP="-k 0"
    # shellcheck disable=SC2086
    if printf '%s\n' "$OWN" | xargs ninja -C "build/mac/$PRESET" -j "${XD_BUILD_JOBS:-4}" $FIRST_KEEP; then
      echo "build_mac.sh: own files: all compiled"
    else
      echo "build_mac.sh: own files: ERRORS (listed above)"
      # Without "keep going" this is where the build ends; with it the libraries are still built (and cached).
      [ "${XD_KEEP_GOING:-0}" = 1 ] || { echo "build_mac.sh: the build failed"; exit 1; }
    fi
  fi
fi
# shellcheck disable=SC2086
cmake --build "build/mac/$PRESET" -j "${XD_BUILD_JOBS:-4}" --target $TARGETS $KEEP || { echo "build_mac.sh: the build failed"; exit 1; }
echo
echo "Build finished: build/mac/$PRESET/bin"
