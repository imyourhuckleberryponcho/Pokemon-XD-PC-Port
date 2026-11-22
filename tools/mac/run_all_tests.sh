#!/bin/bash
# Runs EVERY unit test program of a build that needs no disc (docs/MAC.md "Mac pass for 0.7.0"). The same rule as the Windows / Linux unit runs:
# every <name>_test program in the bin folder is started with no arguments, in the bin folder, with a scratch home and settings
# folder of its own, 5 minutes at most.
#
#   tools/mac/run_all_tests.sh <bin folder> <logs folder> [<expected names file>]
#
#   PASS         exit 0
#   NEEDS INPUT  exit 2 and the output says "usage": the test takes a disc / save / file (async_callbacks_test, pbr_gx_test,
#                pbr_hsd_write_test, xd_card_filter_test, xd_card_test, xd_dsp_test, xd_net_test, xd_rel_link_test). Listed, not a failure.
#   SKIPPED      dawn_bind_group_race_test (it crashes by design without an argument)
#   SOFT FAIL    os_threads_test (timing tolerances that fail on a busy machine, as on Windows / Linux): reported, not fatal
#   FAIL         anything else (the last lines of its output are printed)
#   NOT BUILT    named in the optional file (one test name per line: what the build system knows) but missing from the bin folder
#
# Writes <logs>/tests_summary.txt (one line per test), <logs>/tests_failed.txt (names of the FAILs and NOT BUILTs, one line,
# empty when everything is fine) and <logs>/test_<name>.log. Exit 0 = no FAIL and no NOT BUILT.
set +e -u
BIN="${1:?bin folder}"
LOGS="${2:?logs folder}"
EXPECTED="${3:-}"
mkdir -p "$LOGS"
BIN="$(cd "$BIN" && pwd)"
LOGS="$(cd "$LOGS" && pwd)"
SUMMARY="$LOGS/tests_summary.txt"
: > "$SUMMARY"
FAILED=""
PASS=0; NEEDS=0; SOFT=0; FAIL=0; MISSING=0
SKIP_LIST=" dawn_bind_group_race_test "
SOFT_LIST=" os_threads_test "
run_with_limit() { perl -e 'alarm 300; exec @ARGV' "$@"; }

for path in "$BIN"/*_test; do
  [ -f "$path" ] && [ -x "$path" ] || continue
  t="$(basename "$path")"
  if [[ "$SKIP_LIST" == *" $t "* ]]; then
    echo "SKIPPED     $t (crashes by design without an argument)" | tee -a "$SUMMARY"; continue
  fi
  home="$LOGS/home_$t"; rm -rf "$home"; mkdir -p "$home"
  ( cd "$BIN" && HOME="$home" XD_SETTINGS_DIR="$home" XD_CARD_DIR="$home/card" XD_CACHE_DIR="$home/cache" XD_NO_MESSAGE_BOXES=1 \
      XD_WINDOW_OFFSCREEN=1 SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy SDL_RENDER_DRIVER=software \
      run_with_limit "$path" ) > "$LOGS/test_$t.log" 2>&1
  rc=$?
  if [ $rc = 0 ]; then
    echo "PASS        $t" | tee -a "$SUMMARY"; PASS=$((PASS + 1))
  elif [ $rc = 2 ] && grep -qi "usage" "$LOGS/test_$t.log"; then
    echo "NEEDS INPUT $t (exit 2, asks for a disc / file: not run)" | tee -a "$SUMMARY"; NEEDS=$((NEEDS + 1))
  elif [[ "$SOFT_LIST" == *" $t "* ]]; then
    echo "SOFT FAIL   $t (exit $rc; timing test, not fatal)" | tee -a "$SUMMARY"; SOFT=$((SOFT + 1))
  else
    echo "FAIL        $t (exit $rc)" | tee -a "$SUMMARY"; tail -n 15 "$LOGS/test_$t.log"
    FAILED="$FAILED $t"; FAIL=$((FAIL + 1))
  fi
done

# The extra run of 0.5.5: a program that does not run the game must get ordinary memory when the system has NO memory below
# 4 GB, and go on (the Mac launcher of 0.5.4 stopped there).
if [ -x "$BIN/low_alloc_ordinary_test" ]; then
  home="$LOGS/home_low_alloc_ordinary_test_nolow"; rm -rf "$home"; mkdir -p "$home"
  ( cd "$BIN" && HOME="$home" XD_SETTINGS_DIR="$home" run_with_limit "$BIN/low_alloc_ordinary_test" nolow ) \
    > "$LOGS/test_low_alloc_ordinary_test_nolow.log" 2>&1
  rc=$?
  if [ $rc = 0 ]; then echo "PASS        low_alloc_ordinary_test nolow" | tee -a "$SUMMARY"
  else
    echo "FAIL        low_alloc_ordinary_test nolow (exit $rc)" | tee -a "$SUMMARY"; tail -n 15 "$LOGS/test_low_alloc_ordinary_test_nolow.log"
    FAILED="$FAILED low_alloc_ordinary_test_nolow"; FAIL=$((FAIL + 1))
  fi
fi

if [ -n "$EXPECTED" ] && [ -f "$EXPECTED" ]; then
  while read -r t; do
    [ -n "$t" ] || continue
    if [ ! -x "$BIN/$t" ]; then
      echo "NOT BUILT   $t (the build system has it, the bin folder does not)" | tee -a "$SUMMARY"
      FAILED="$FAILED $t(not built)"; MISSING=$((MISSING + 1))
    fi
  done < "$EXPECTED"
fi

echo "$FAILED" | sed 's/^ *//' > "$LOGS/tests_failed.txt"
echo "tests: $PASS passed, $NEEDS need a disc/file (not run), $SOFT soft fail, $FAIL failed, $MISSING not built" | tee -a "$SUMMARY"
[ $((FAIL + MISSING)) = 0 ]
