#!/bin/sh
# Builds and runs every probe variant and writes ONE file, probe_report.txt, to send back (the workflow's "probe"
# stage runs exactly this). Usage:  sh run_probe.sh [output folder]
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-"$here/out"}
sh "$here/build_probe.sh" "$out" || exit 1
cd "$out" || exit 1
R=probe_report.txt
: > "$R"
{ echo "window probe report $(date -u +%Y-%m-%dT%H:%MZ)"; uname -a; sw_vers 2>/dev/null; echo; cat build_result.txt; } >> "$R"
for f in window_probe window_probe_seg512 window_probe_seg3g window_probe_segfix; do
  echo >> "$R"
  if [ -x "./$f" ]; then
    echo "######## $f" >> "$R"
    "./$f" >> "$R" 2>&1
    echo "(exit code $?)" >> "$R"
  else
    echo "######## $f: not built (see the link lines at the top)" >> "$R"
  fi
done
echo
echo "Done. Send this file back: $out/$R"
echo
grep -E "^FACT|^link |^######## |^macOS|^this program" "$R"
