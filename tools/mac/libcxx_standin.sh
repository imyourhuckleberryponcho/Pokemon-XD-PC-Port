#!/bin/bash
# The Mac stand-in build on Linux: the whole port compiled and linked against LLVM's libc++ (the Mac's C++ library
# family) with the Mac-only replacement pieces switched on (PORT_TEST_MAC_HALVES: the Mac's from_chars, the
# folder claim's Mac half, the low-memory calls), and -idirafter platform/compat/mac (the Mac's <malloc.h>).
# docs/MAC.md, "Stand-in builds and tests" and "Mac pass for 0.6.0".
#
#   tools/mac/libcxx_standin.sh <build root> [ libcxx folder ] [ targets... ]
#
# <build root>   a folder OUTSIDE the repository (Aurora gets its own clone there, as build.sh does; never the shared
#                third_party/aurora: "one configure at a time"). Run it inside tools/build_lock.py.
# libcxx folder  Ubuntu's libc++-18-dev / libc++abi-18-dev .deb files unpacked with `dpkg -x` (nothing installed);
#                default ~/mac2/libcxx.
# targets        default: the unit tests that need no graphics library and no disc.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
B="$1"; LX="${2:-$HOME/mac2/libcxx}"; shift 2 2>/dev/null || shift $# || true
TARGETS="${*:-settings_test controls_test memcard_test save_backups_test gen3_save_test randomizer_test xd_save_test
  xd_save_convert_test xd_save_region_test xd_save_edition_test xd_settings_test xd_legality_test card_sources_test
  i18n_test hdtex_test colo_save_test colo_pkx_test xd_colosseum_test net_test net_trade_test save_thumbnail_test
  online_room_test online_commit_test online_draft_test online_lobby_test online_team_test trainer_card_test
  xd_extra_sources_test mods_test data_mods_test playthrough_test update_test
  low_arena_test low_malloc_mac_test mac_compat_mmap_test folder_claim_posix_test folder_claim_mac_test
  macho_symbols_test disc_check_test button_art_test
  xd_languages colo_models}"
INC="$LX/usr/lib/llvm-18/include/c++/v1"; LIB="$LX/usr/lib/llvm-18/lib"
[ -d "$INC" ] || { echo "no libc++ headers at $INC"; exit 2; }
mkdir -p "$B"
# the table generator needs the pip package libclang (Ubuntu's python3-clang does not work): pip install --target ~/mac2/py libclang
[ -d "$HOME/mac2/py/clang" ] && export PYTHONPATH="$HOME/mac2/py${PYTHONPATH:+:$PYTHONPATH}"
AURORA_SHARED="$ROOT/third_party/aurora"; AURORA_CLONE="$B/aurora"
WANT="$(git -c safe.directory='*' -C "$AURORA_SHARED" rev-parse HEAD)"
if [ ! -d "$AURORA_CLONE/.git" ]; then
  git -c safe.directory='*' -c core.autocrlf=true clone -q --no-hardlinks "$AURORA_SHARED" "$AURORA_CLONE"
  git -C "$AURORA_CLONE" config core.autocrlf true
fi
if [ "$(git -C "$AURORA_CLONE" rev-parse HEAD)" != "$WANT" ]; then
  git -c safe.directory='*' -C "$AURORA_CLONE" fetch -q "$AURORA_SHARED" "$WANT"
  git -C "$AURORA_CLONE" checkout -q -f "$WANT"
fi
if [ ! -f "$B/linux-release/build.ninja" ] || [ "${XD_RECONFIGURE:-0}" = 1 ]; then
  cmake --preset linux-release -B "$B/linux-release" -S "$ROOT" -DXD_LINUX_HEADLESS=ON -DXD_AURORA_DIR="$AURORA_CLONE" \
    -DCMAKE_CXX_FLAGS="-stdlib=libc++ -nostdinc++ -isystem $INC -DPORT_TEST_MAC_HALVES -idirafter $ROOT/platform/compat/mac" \
    -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++ -L$LIB -Wl,-rpath,$LIB" \
    -DCMAKE_MODULE_LINKER_FLAGS="-stdlib=libc++ -L$LIB -Wl,-rpath,$LIB" \
    -DCMAKE_SHARED_LINKER_FLAGS="-stdlib=libc++ -L$LIB -Wl,-rpath,$LIB"
fi
# (known not to build here: log_zip_test and user_folder_test are Windows-only; mac_from_chars_test compares against the
#  std::from_chars that libc++ 18 deletes; gc_memory_test, keyboard_defaults_test, controls_apply_test, pbr_models link Dawn,
#  prebuilt with GNU's library; launcher_reports uses clock_cast on every system but the Mac's own #if __APPLE__ branch)
# -k 0: every target that can be built is built; the ones that need Dawn (prebuilt with GNU's library) are listed
cmake --build "$B/linux-release" -j "${XD_BUILD_JOBS:-8}" --target $TARGETS -- -k 0 || echo "libcxx_standin.sh: some targets failed (listed above)"
echo "built into $B/linux-release/bin"
