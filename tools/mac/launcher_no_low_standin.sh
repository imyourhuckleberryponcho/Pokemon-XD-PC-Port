#!/bin/bash
# The Mac launcher's save-page abort (0.5.4 -> 0.5.5) reproduced on Linux, and the fix shown (docs/MAC.md, "Launcher
# stopped on a Mac with a save"). A Mac program without reserved memory has NO free address below 4 GB; here
# tools/mac/no_low_preload.c (LD_PRELOAD) takes every free piece of the low 4 GB before the launcher starts.
#   tools/mac/launcher_no_low_standin.sh <launcher> <disc image> <save .gci> <work folder> [<other launcher to compare>]
# Runs the launcher on the Save page (scripted, picture taken) with the save and the disc, twice: normally, and with
# no low memory. Expected with the fixed launcher: both end with exit code 0, the picture shows the save with names,
# the second run's log has the one "[platform/sdk] ... ordinary memory" line. Expected with the 0.5.4 launcher (give
# it as the fifth argument): the no-low run prints "no free 256 MB range below 4 GB" and ends in an abort.
# Every run uses its own scratch folders (settings, card, cache, data folder): nothing of a player's is touched.
set -u
L="$1"; DISC="$2"; GCI="$3"; W="$4"; OLD="${5:-}"
R="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$W"; cd "$W" || exit 1
cc -shared -fPIC -O2 "$R/tools/mac/no_low_preload.c" -o no_low_preload.so || exit 1
rc_all=0
run() { # <label> <launcher> <preload 0|1>
  local label="$1" prog="$2" pre="$3" d="$W/$1"
  rm -rf "$d"; mkdir -p "$d/settings" "$d/cache" "$d/card/USA/Card A" "$d/home/.local/share" "$d/config"
  cp "$GCI" "$d/card/USA/Card A/01-GXXE-PokemonXD.gci"
  local env_pre=""
  [ "$pre" = 1 ] && env_pre="LD_PRELOAD=$W/no_low_preload.so"
  ( cd "$(dirname "$prog")" && env $env_pre HOME="$d/home" XDG_DATA_HOME="$d/home/.local/share" XDG_CONFIG_HOME="$d/config" \
      XD_SETTINGS_DIR="$d/settings" XD_CARD_DIR="$d/card" XD_CACHE_DIR="$d/cache" XD_LAUNCHER_RENDER=0 XD_LAUNCHER_SHADERS=0 \
      XD_NO_MESSAGE_BOXES=1 SDL_VIDEODRIVER=offscreen SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy XD_WINDOW_OFFSCREEN=1 \
      XD_LAUNCHER_SCRIPT="size:1100x900|set:system.update_check=off|disc:$DISC|wait:disc|page:save|wait:save|shot:$d/save_page.png|quit" \
      timeout 240 "$prog" > "$d/out.txt" 2>&1; echo "exit code $?" >> "$d/out.txt" )
  echo "== $label: $(tail -n 1 "$d/out.txt")  (picture: $( [ -s "$d/save_page.png" ] && echo yes || echo NO ))"
  grep -E "no_low_preload|no free 256 MB|ordinary memory|Stopped|without Pokemon" "$d/out.txt" | head -5 | sed 's/^/     /'
}
run normal_fixed "$L" 0
run nolow_fixed "$L" 1
[ "$(tail -n 1 "$W/nolow_fixed/out.txt")" = "exit code 0" ] && [ -s "$W/nolow_fixed/save_page.png" ] || { echo "FAIL: the launcher did not survive without low memory"; rc_all=1; }
grep -q "Stopped (abort)" "$W/nolow_fixed/out.txt" && { echo "FAIL: abort"; rc_all=1; }
if [ -n "$OLD" ]; then
  run normal_old "$OLD" 0
  cmp -s "$W/normal_old/save_page.png" "$W/normal_fixed/save_page.png" && echo "(save page of the 0.5.4 launcher and of the fixed one: identical pictures, with low memory free)" || echo "NOTE: the save page pictures differ"
  cmp -s "$W/nolow_fixed/save_page.png" "$W/normal_fixed/save_page.png" && echo "(fixed launcher, no low memory: the same picture as with low memory: names read correctly)" || { echo "FAIL: pictures differ without low memory"; rc_all=1; }
  run nolow_old "$OLD" 1
  grep -q "no free 256 MB range below 4 GB" "$W/nolow_old/out.txt" && echo "(the 0.5.4 launcher aborts the same way, as on Mark's Mac)" || echo "NOTE: the old launcher did not stop"
fi
exit $rc_all
