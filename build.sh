#!/bin/bash
# Builds the port on Linux (docs/LINUX.md).  Usage:  ./build.sh [release|debug] [target ...]
# Output goes to build/linux/linux-<preset>/bin. Without targets: everything that builds on Linux so far
# (the list below); "all" asks for every target (stops at the first part that is Windows-only still).
#
# The compiler is Clang (the game code needs it). The game itself: `./build.sh release xd` (needs a decomp snapshot
# from fe525e8aa1 on; docs/LINUX.md, "First link and first run").
# Needs: the packages of docs/LINUX.md ("What has to be run once"): gcc/g++, clang, cmake >= 3.25, ninja, the
# X11/Wayland/audio development files. If cmake/ninja are missing, a copy from pip in build/linux/tools/py is used
# (python3 -m pip install --target build/linux/tools/py cmake ninja libclang).
# Environment: XD_BUILD_JOBS (default 6), XD_LINUX_HEADLESS=0|1 (default: 1 when the X11 and Wayland development
# files are missing: SDL is then built without a window system, enough for the tools and tests).
set -e
cd "$(dirname "$0")"
PRESET="linux-${1:-release}"
[ $# -gt 0 ] && shift

# Everything that compiles, links and runs on Linux today. Keep in step with docs/LINUX.md ("What builds").
LINUX_TARGETS="settings_test controls_test memcard_test save_backups_test gen3_save_test randomizer_test
  gsthread_test xd_save_test xd_save_convert_test xd_gba_test xd_settings_test
  dsp_decode_test dsp_core_test
  disc_check_test mods_test data_mods_test playthrough_test disc_overlay_test gc_fused_test disc_drive_test
  xd_disc_list xd_dsp_test xd_dsp_run
  platform_compat platform_sdk platform_os platform_audio platform_input platform_video
  xd_disc_check xd_data_dump xd_save_dump xd_randomize xd_gba_trade xd_rel_link_test
  xd_settings xd_data_mods xd_save_convert xd_mods_check
  os_threads_test gc_memory_test gx_fifo_test card_timing_test input_record_test game_clock_test
  keyboard_defaults_test controls_apply_test button_art_test dsp_equiv_test"
if [ -f "${XD_LINUX_BUILD_ROOT:-build/linux}/targets_extra.txt" ]; then # (local experiments)
  LINUX_TARGETS="$LINUX_TARGETS $(cat "${XD_LINUX_BUILD_ROOT:-build/linux}/targets_extra.txt")"
fi

# XD_LINUX_BUILD_ROOT=<folder>: build tree and pip tools there instead of build/linux (for example a second source
# tree's build kept outside that tree).
BUILD_ROOT="${XD_LINUX_BUILD_ROOT:-$PWD/build/linux}"
TOOLS="$BUILD_ROOT/tools/py"
if ! command -v cmake >/dev/null || ! command -v ninja >/dev/null; then
  if [ -x "$TOOLS/bin/cmake" ]; then
    export PYTHONPATH="$TOOLS${PYTHONPATH:+:$PYTHONPATH}"
    export PATH="$TOOLS/bin:$PATH"
  else
    echo "cmake and ninja were not found (see the top of build.sh)"; exit 1
  fi
fi

# The game's tables (tools/gen_disc_tables.py, run when configuring) need the pip package libclang; Ubuntu's own
# python3-clang doesn't work (docs/LINUX.md): python3 -m pip install --target build/linux/tools/py libclang
if [ -d "$TOOLS/clang" ]; then
  export PYTHONPATH="$TOOLS${PYTHONPATH:+:$PYTHONPATH}"
fi
# XD_SNAPSHOT_OVERRIDE=<folder>: build the game code from that decomp snapshot instead of snapshot/CURRENT.txt
# (XD_BUILD_GAME=0: only the libraries, tools and tests).
EXTRA_ARGS=""
[ -n "${XD_SNAPSHOT_OVERRIDE:-}" ] && EXTRA_ARGS="$EXTRA_ARGS -DXD_SNAPSHOT_OVERRIDE=$XD_SNAPSHOT_OVERRIDE"
[ "${XD_BUILD_GAME:-1}" = 0 ] && EXTRA_ARGS="$EXTRA_ARGS -DXD_BUILD_GAME=OFF"

HEADLESS="${XD_LINUX_HEADLESS:-}"
if [ -z "$HEADLESS" ]; then
  if [ -f /usr/include/X11/Xlib.h ] || [ -f /usr/include/wayland-client.h ]; then HEADLESS=0; else HEADLESS=1; fi
fi
if [ "$HEADLESS" = 1 ]; then
  echo "build.sh: no X11/Wayland development files: SDL without a window system (tools and tests only)"
  HEADLESS_ARG=-DXD_LINUX_HEADLESS=ON
else
  HEADLESS_ARG=-DXD_LINUX_HEADLESS=OFF
fi

if [ "${XD_LINUX_NO_CONFIGURE:-0}" != 1 ] || [ ! -f "$BUILD_ROOT/$PRESET/build.ninja" ]; then
  cmake --preset "$PRESET" -B "$BUILD_ROOT/$PRESET" $HEADLESS_ARG $EXTRA_ARGS
fi
if [ $# -gt 0 ]; then
  if [ "$1" = all ]; then TARGETS=all; else TARGETS="$*"; fi
else
  TARGETS="$LINUX_TARGETS"
fi
cmake --build "$BUILD_ROOT/$PRESET" -j "${XD_BUILD_JOBS:-6}" --target $TARGETS
echo
echo "Build finished: $BUILD_ROOT/$PRESET/bin"
