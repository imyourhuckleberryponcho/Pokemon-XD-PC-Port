#!/bin/bash
# Pokemon XD PC, Mac build: the first tester's report. NEVER RUN ON A MAC (written on a Windows PC, 2026-11-10;
# syntax checked with bash -n; the program-file checks tried on Linux on real Mac program files made by Apple's
# open-source linker, read by LLVM's and by Apple's own otool: a right one, one with __TEXT at the wrong address,
# one with a 4 GB page zero + PIE + chained fixups: each reported as it is. tools/mac/layout_more.sh).
#
# What it does: looks at this Mac and at the program, starts the game ONCE WITHOUT A DISC (it stops by itself with
# "no disc" after it has set up its memory, which is the part that has never been tried on a Mac) and writes ONE
# text file to send back:   ~/Desktop/pokemon_xd_mac_report.txt
# Nothing is sent anywhere. No game data is read (no disc is used). Your user name and home folder are replaced by
# "~" in the file. Read the file before sending it if you like: it is plain text.
#
# How to run it: open Terminal, then
#     bash "/path/to/Pokemon XD.app/Contents/Resources/first_run_report.sh"
# (drag the file into the Terminal window to get its path), or, for a build folder:
#     bash games/xd/mac/first_run_report.sh build/mac/mac-release/bin/xd
#
# Works with the system's own bash 3.2 and BSD tools; needs no Xcode. Where Xcode's command line tools are missing,
# the checks that need them (otool, codesign) say so and the rest still runs.
HERE="$(cd "$(dirname "$0")" && pwd)"
XD="$1"
if [ -z "$XD" ]; then
  for c in "$HERE/Pokemon XD.app/Contents/MacOS/xd" "$HERE/../MacOS/xd" "$HERE/xd" "$HERE/../../../build/mac/mac-release/bin/xd"; do
    if [ -f "$c" ]; then XD="$c"; break; fi
  done
fi
OUT="${XD_REPORT_FILE:-$HOME/Desktop/pokemon_xd_mac_report.txt}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/xd_report.XXXXXX")"
RAW="$WORK/raw.txt"
: > "$RAW"
say() { printf '%s\n' "$*" >> "$RAW"; }
section() { say ""; say "======== $* ========"; }
run() { # prints the command, then its output (first 400 lines) and exit code
  say "\$ $*"
  "$@" > "$WORK/cmd.txt" 2>&1
  local rc=$?
  head -n 400 "$WORK/cmd.txt" >> "$RAW"
  [ $rc -ne 0 ] && say "(exit code $rc)"
  return $rc
}
have() { command -v "$1" > /dev/null 2>&1; }
# Are Xcode's command line tools really there? On a Mac without them /usr/bin/otool and /usr/bin/ld still exist, as
# stand-ins that only open an "install the developer tools?" window; "xcode-select -p" fails then. (From Apple's
# xcode-select(1); not tried on a Mac. Where xcode-select itself is missing, as on Linux, the tool's presence counts.)
dev_tools() { if have xcode-select; then xcode-select -p > /dev/null 2>&1; else return 0; fi; }
VERDICTS="$WORK/verdicts.txt"
: > "$VERDICTS"
verdict() { printf '%-7s %s\n' "$1" "$2" >> "$VERDICTS"; } # OK / PROBLEM / UNKNOWN

section "1. This Mac"
run sw_vers
run uname -a
run sysctl -n machdep.cpu.brand_string
say "hw.model: $(sysctl -n hw.model 2> /dev/null)   memory: $(( $(sysctl -n hw.memsize 2> /dev/null || echo 0) / 1073741824 )) GB"
ARCH="$(uname -m)"
OSMAJOR="$(sw_vers -productVersion 2> /dev/null | cut -d. -f1)"
if [ "$ARCH" = "arm64" ]; then
  say "Apple silicon Mac: the game is an Intel program and runs through Rosetta 2."
  if arch -x86_64 /usr/bin/true 2> "$WORK/rosetta.txt"; then
    say "Rosetta 2: installed (an Intel program started)."
    verdict OK "Rosetta 2 is installed"
  else
    say "Rosetta 2: NOT working: $(cat "$WORK/rosetta.txt")"
    say "Install it with:   softwareupdate --install-rosetta --agree-to-license"
    verdict PROBLEM "Rosetta 2 is not installed (softwareupdate --install-rosetta --agree-to-license)"
  fi
else
  say "Intel Mac ($ARCH): no Rosetta needed."
fi
case "$OSMAJOR" in
  ''|*[!0-9]*) verdict UNKNOWN "macOS version not read" ;;
  *)
    if [ "$OSMAJOR" -lt 13 ]; then verdict PROBLEM "macOS $OSMAJOR is older than 13.3, which the program needs"
    elif [ "$OSMAJOR" -ge 28 ]; then verdict PROBLEM "macOS $OSMAJOR no longer runs Intel programs (macOS 27 was the last)"
    else verdict OK "macOS $OSMAJOR is in the supported range (13.3 to 27)"; fi ;;
esac
run system_profiler SPDisplaysDataType
# Memory page size: 16 KB on Apple silicon; an Intel program is expected to see 4 KB (never measured by us).
say "page size of this Mac: $(sysctl -n hw.pagesize 2> /dev/null || echo '?')   as an Intel program sees it: $(arch -x86_64 /usr/bin/pagesize 2> /dev/null || echo '?')"
# Only for people who BUILT the program: which Xcode and linker. (Xcode 27 removed the older linker "ld64", the one
# the memory layout was checked with; Xcode 15.3 to 26 still have it as -ld_classic. docs/MAC.md.)
if have xcode-select && dev_tools; then
  run xcode-select -p
  run xcodebuild -version
  run pkgutil --pkg-info=com.apple.pkg.CLTools_Executables
  run ld -v
  run clang --version
else
  say "Xcode / command line tools: not installed (fine for playing; only needed to build)."
fi

section "2. The program file"
if [ -z "$XD" ] || [ ! -f "$XD" ]; then
  say "The game program was not found. Give its path:  bash first_run_report.sh \"/path/to/Pokemon XD.app/Contents/MacOS/xd\""
  verdict PROBLEM "the game program (xd) was not found"
else
  say "program: $XD"
  run ls -l "$XD"
  run file "$XD"
  run xattr -l "$XD"
  if xattr "$XD" 2> /dev/null | grep -q com.apple.quarantine; then
    say "The file is still marked as downloaded (quarantine). See the Read me: System Settings > Privacy & Security > Open Anyway."
    verdict PROBLEM "the program is still in quarantine (Read me: Open Anyway)"
  fi
  if have otool && dev_tools; then
    run otool -hv "$XD"
    if otool -hv "$XD" 2> /dev/null | grep -qw PIE; then
      verdict PROBLEM "the program is position independent (PIE): the linker ignored -no_pie; the game's memory layout cannot work"
    else
      verdict OK "the program is not position independent (no PIE flag)"
    fi
    otool -l "$XD" > "$WORK/load.txt" 2>&1
    # Each segment: name, address, size, bytes in the file.
    awk '
      $1 == "cmd" { insegment = ($2 == "LC_SEGMENT_64"); if ($2 == "LC_DYLD_CHAINED_FIXUPS") print "CHAINED_FIXUPS" }
      insegment && $1 == "segname" { name = $2 }
      insegment && $1 == "vmaddr" { addr = $2 }
      insegment && $1 == "vmsize" { size = $2 }
      insegment && $1 == "filesize" { print "SEGMENT", name, addr, size, $2; insegment = 0 }
      $1 == "sectname" && $2 == "__huge" { print "HUGE_SECTION" }
    ' "$WORK/load.txt" > "$WORK/segments.txt"
    say "segments (name, address, size, bytes in the file):"
    grep '^SEGMENT' "$WORK/segments.txt" | sed 's/^SEGMENT/   /' >> "$RAW"
    check_segment() { # <name> <address> <size or -> : compares as numbers
      local line addr size
      line="$(grep "^SEGMENT $1 " "$WORK/segments.txt" | head -n 1)"
      if [ -z "$line" ]; then verdict PROBLEM "segment $1 is missing from the program"; return; fi
      addr="$(echo "$line" | awk '{print $3}')"; size="$(echo "$line" | awk '{print $4}')"
      if [ $((addr)) -ne $(($2)) ]; then verdict PROBLEM "segment $1 is at $addr, must be at $2"; return; fi
      if [ "$3" != "-" ] && [ $((size)) -ne $(($3)) ]; then verdict PROBLEM "segment $1 has size $size, must be $3"; return; fi
      verdict OK "segment $1 at $2"
    }
    check_segment __PAGEZERO 0x0 0x1000
    check_segment __TEXT 0x40000000 -
    check_segment __XDLOW 0x70000000 0x10000000
    check_segment __XDMEM1 0x80000000 0x1800000
    check_segment __XDMEMU 0xC0000000 0x1800000
    check_segment __XDLC 0xE0000000 0x4000
    if grep -q HUGE_SECTION "$WORK/segments.txt"; then verdict PROBLEM "the linker moved the reserved memory into a __huge section (-no_huge was ignored)"; fi
    if grep -q CHAINED_FIXUPS "$WORK/segments.txt"; then verdict PROBLEM "the program uses chained fixups (-no_fixup_chains was ignored): its fixed address may be lost"; fi
    say ""
    say "libraries the program asks for:"
    otool -L "$XD" 2>&1 | tail -n +2 >> "$RAW"
    say ""
    say "linker / build version commands:"
    grep -A 6 -E "LC_BUILD_VERSION|LC_VERSION_MIN_MACOSX|LC_MAIN" "$WORK/load.txt" | head -n 40 >> "$RAW"
  else
    say "otool is not on this Mac (it comes with Xcode's command line tools:  xcode-select --install ). The program's layout was not checked."
    verdict UNKNOWN "program layout not checked (no otool; install with xcode-select --install)"
  fi
  if have codesign; then
    run codesign -dv --verbose=2 "$XD"
    APP="$(cd "$(dirname "$XD")/../.." 2> /dev/null && pwd)"
    case "$APP" in *.app) run codesign --verify --deep --strict --verbose=2 "$APP"; run spctl -a -vv "$APP" ;; esac
  fi
fi

section "3. Start without a disc"
if [ -n "$XD" ] && [ -f "$XD" ]; then
  say "The game is started with no disc. Expected: it sets up its memory, prints where things are, says that no"
  say "disc was given and ends by itself with exit code 1. No window should stay open. Limit: 90 seconds."
  mkdir -p "$WORK/settings" "$WORK/card" "$WORK/cache"
  # start_once <name> [VAR=value]: one start without a disc; what it printed in $WORK/<name>.txt, its exit code
  # in $WORK/<name>_exit.txt
  start_once() {
    (
    name="$1"
    cd "$(dirname "$XD")" || exit 97
    env XD_TEST_RUN=1 XD_NO_MESSAGE_BOXES=1 XD_SETTINGS_DIR="$WORK/settings" XD_CARD_DIR="$WORK/card" \
      XD_CACHE_DIR="$WORK/cache" XD_CRASH_REPORT=0 "${2:-XD_REPORT_START=1}" "$XD" > "$WORK/$name.txt" 2>&1 &
    pid=$!
    waited=0
    while kill -0 $pid 2> /dev/null; do
      sleep 1
      waited=$((waited + 1))
      if [ $waited -ge 90 ]; then
        echo "(still running after 90 seconds: stopped by the report script)" >> "$WORK/$name.txt"
        if have sample; then sample $pid 2 -file "$WORK/sample.txt" > /dev/null 2>&1; fi
        if have vmmap; then vmmap $pid > "$WORK/vmmap.txt" 2>&1; fi
        kill -9 $pid 2> /dev/null
        break
      fi
    done
    wait $pid 2> /dev/null
    echo $? > "$WORK/${name}_exit.txt"
    )
  }
  start_once start
  RC="$(cat "$WORK/start_exit.txt" 2> /dev/null)"
  say "exit code: $RC   (1 = stopped by itself as expected; 137 = had to be stopped; 132/134/138/139 = crashed)"
  say "---- everything it printed ----"
  head -n 600 "$WORK/start.txt" >> "$RAW"
  say "---- end ----"
  for f in sample.txt vmmap.txt; do
    if [ -f "$WORK/$f" ]; then say "---- $f (first 300 lines) ----"; head -n 300 "$WORK/$f" >> "$RAW"; fi
  done
  S="$WORK/start.txt"
  if [ "$RC" = "1" ]; then verdict OK "the start without a disc ended by itself (exit code 1)"
  else verdict PROBLEM "the start without a disc ended with exit code $RC (expected 1)"; fi
  if grep -q "^\[user folder\]" "$S"; then verdict OK "data folder line printed"; else verdict PROBLEM "no \"[user folder]\" line: the program did not get as far as its data folder"; fi
  if grep -q "mac: C library blocks at" "$S"; then
    if grep -q "all three blocks below 4 GB: yes" "$S"; then verdict OK "the C library's memory blocks are below 4 GB"
    else verdict PROBLEM "the C library's memory blocks are ABOVE 4 GB (known risk 1 in docs/MAC.md: the game may stop later with \"address above 4 GB\")"; fi
  else verdict PROBLEM "no \"mac: C library blocks at\" line: the program stopped before its first line"; fi
  # The program's own memory keeper (platform/os/low_malloc_mac.cpp): its proof lines.
  if grep -q "mac: low memory self-test: OK" "$S"; then
    verdict OK "the program's own memory (new, containers, a second thread, realloc) is below 4 GB"
  elif grep -q "mac: low memory self-test:" "$S"; then
    verdict PROBLEM "the low memory self-test found a problem: see the \"low memory:\" lines of the start"
  else
    verdict PROBLEM "no \"low memory self-test\" line: the program stopped inside its memory test, or is an older build"
  fi
  if grep -q "low memory: stress: .*verdict: OK" "$S"; then verdict OK "the mixed memory load on four threads ended well"
  else verdict PROBLEM "the mixed memory load (\"low memory: stress\") did not end with verdict OK"; fi
  if grep -q "^.low memory. " "$S"; then verdict PROBLEM "the memory keeper stopped the program or refused a request: see its \"[low memory]\" line"; fi
  if grep -q "mac: game memory in place at 0x80000000 (second view at 0xC0000000: same memory)" "$S"; then
    verdict OK "the game's memory is at 0x80000000 and mirrored at 0xC0000000"
  elif grep -q "mac: game memory in place" "$S"; then
    verdict PROBLEM "the game's memory is in place but the second view is NOT the same memory"
  else
    verdict PROBLEM "the game's memory could not be placed at its addresses (the central question of this build)"
  fi
  if grep -q "could not make the table" "$S"; then verdict PROBLEM "a game table could not be made writable"; fi
  if grep -qi "dyld\[" "$S"; then verdict PROBLEM "the system's loader (dyld) refused the program or a library: see its lines"; fi
  # A second start, for information only: the system's libraries allocate below 4 GB as well (XD_LOW_MALLOC=all,
  # a testing switch; not how the game runs). Its result is never a PROBLEM line.
  say ""
  say "---- second start, with XD_LOW_MALLOC=all (a testing switch; for information only) ----"
  start_once start_all XD_LOW_MALLOC=all
  RC2="$(cat "$WORK/start_all_exit.txt" 2> /dev/null)"
  say "exit code: $RC2"
  head -n 60 "$WORK/start_all.txt" >> "$RAW"
  say "---- end ----"
  if [ "$RC2" = "1" ] && grep -q "mac: low memory self-test: OK" "$WORK/start_all.txt"; then
    verdict INFO "with XD_LOW_MALLOC=all (the system's libraries low as well) the start also ended well"
  else
    verdict INFO "with XD_LOW_MALLOC=all (a testing switch, not used by the game) the start did NOT end well: exit code $RC2"
  fi
else
  say "(skipped: no program)"
fi

section "4. The data folder"
DATA="$HOME/Library/Application Support/Pokemon XD"
if [ -d "$DATA" ]; then
  run ls -la "$DATA"
  if [ -f "$DATA/start_failure.txt" ]; then say "---- start_failure.txt ----"; tail -n 40 "$DATA/start_failure.txt" >> "$RAW"; fi
  NEWEST="$(ls -t "$DATA/logs" 2> /dev/null | head -n 1)"
  if [ -n "$NEWEST" ]; then
    say "---- newest log: logs/$NEWEST (last 300 lines) ----"
    tail -n 300 "$DATA/logs/$NEWEST" >> "$RAW"
  else
    say "No log files yet in $DATA/logs."
  fi
else
  say "The data folder does not exist yet: $DATA"
fi
CRASHES="$HOME/Library/Logs/DiagnosticReports"
if [ -d "$CRASHES" ]; then
  LAST="$(ls -t "$CRASHES" 2> /dev/null | grep -i '^xd[-_.]' | head -n 1)"
  if [ -n "$LAST" ]; then
    say "---- the system's newest crash report for xd: $LAST (first 150 lines) ----"
    head -n 150 "$CRASHES/$LAST" >> "$RAW"
  fi
fi

# The file to send: verdicts first, then the details; the home folder and the user name replaced.
{
  echo "Pokemon XD PC, Mac build: first run report ($(date '+%Y-%m-%d %H:%M'))"
  echo "Made by first_run_report.sh. No game data inside. Please send this file back as it is."
  echo
  echo "======== In short ========"
  if grep -q '^PROBLEM' "$VERDICTS"; then echo "PROBLEMS FOUND: $(grep -c '^PROBLEM' "$VERDICTS")"; else echo "No problem found by these checks."; fi
  cat "$VERDICTS"
  cat "$RAW"
} > "$WORK/report.txt"
ME="$(id -un 2> /dev/null)"
ESC_HOME="$(printf '%s' "$HOME" | sed 's/[][\\.*^$|]/\\&/g')"
ESC_ME="$(printf '%s' "$ME" | sed 's/[][\\.*^$|]/\\&/g')"
if [ -n "$ESC_ME" ] && [ ${#ME} -ge 3 ]; then
  sed -e "s|$ESC_HOME|~|g" -e "s|/Users/$ESC_ME|~|g" -e "s|$ESC_ME|<user>|g" "$WORK/report.txt" > "$OUT"
else
  sed -e "s|$ESC_HOME|~|g" "$WORK/report.txt" > "$OUT"
fi
rm -rf "$WORK"
echo "Report written: $OUT"
echo "In short:"
sed -n '/^======== In short/,/^======== 1\./p' "$OUT" | sed '1d;$d'
echo "Please send that file back."
