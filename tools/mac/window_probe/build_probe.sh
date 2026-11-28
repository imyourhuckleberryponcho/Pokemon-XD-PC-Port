#!/bin/sh
# Builds the three-facts probe (docs/MAC_ARM.md step 4; tools/mac/window_probe/README.txt). Needs only Apple's
# command line tools (clang). Usage:  sh build_probe.sh [output folder]
# Fact 3 is a LINK question: each variant below links a bigger zero-fill segment; "link ...: FAILED" lines are answers.
set -u
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-"$here/out"}
mkdir -p "$out"
cc=${CC:-clang}
os=$(uname -s)
echo "build_probe: $os $(uname -m), compiler: $($cc --version 2>&1 | head -1)"
LOG="$out/build_result.txt"; : > "$LOG"
say() { echo "$*" | tee -a "$LOG"; }

build() { # <name> <extra compile flags> <extra link flags>
  name=$1; cflags=$2; lflags=$3
  # shellcheck disable=SC2086
  if $cc -O1 -g -Wall $cflags "$here/window_probe.c" -o "$out/$name" $lflags >"$out/$name.build.txt" 2>&1; then
    say "link $name: OK"
  else
    say "link $name: FAILED (the linker's words are in $out/$name.build.txt):"
    head -5 "$out/$name.build.txt" | sed 's/^/    /' | tee -a "$LOG"
  fi
}

build window_probe "" ""
if [ "$os" = Darwin ]; then
  # arm64 programs are position independent (the system insists); the answer says whether these segments still link
  build window_probe_seg512 "-DPROBE_SEG_BYTES=0x20000000" "-Wl,-segprot,__XDBIG,rw,rw"
  build window_probe_seg3g "-DPROBE_SEG_BYTES=0xC0000000" "-Wl,-segprot,__XDBIG,rw,rw"
  # a segment at a fixed high address (the "-segaddr" of the Intel build); a PIE arm64 program may refuse this
  build window_probe_segfix "-DPROBE_SEG_BYTES=0x20000000" "-Wl,-segprot,__XDBIG,rw,rw -Wl,-segaddr,__XDBIG,0x2000000000"
  for f in window_probe window_probe_seg512 window_probe_seg3g window_probe_segfix; do
    if [ -x "$out/$f" ]; then say "$f: $(file "$out/$f" | sed 's/^[^:]*: //')"; fi
  done
fi
say "built in $out. Run each program from that folder, or use run_probe.sh which does everything."
