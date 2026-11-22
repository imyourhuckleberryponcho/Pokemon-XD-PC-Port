#!/bin/bash
# Apple's open-source linker (ld64) and cctools as Linux programs, from conda-forge, unpacked into a folder under the
# home folder (default ~/mac_ld64). No sudo, nothing installed into the system, nothing added to PATH.
# Used by tools/mac/layout_check.sh (docs/MAC.md, "Layout check with Apple's ld64 on Linux").
# This download was approved on 2026-11-10. NO Apple SDK (headers / libraries) is fetched: only the tools.
# Usage: get_ld64.sh [folder]
D="${1:-$HOME/mac_ld64}"
mkdir -p "$D/dl" "$D/mm" || exit 1
cd "$D/mm" || exit 1
if [ ! -x bin/micromamba ]; then
  echo "== micromamba (one user-level program, the package unpacker)"
  curl -sSL https://micro.mamba.pm/api/micromamba/linux-64/latest -o "$D/dl/micromamba.tar.bz2" || { echo "DOWNLOAD FAILED: micromamba"; exit 2; }
  tar -xjf "$D/dl/micromamba.tar.bz2" bin/micromamba || { echo "UNPACK FAILED: micromamba"; exit 2; }
fi
export MAMBA_ROOT_PREFIX="$D/mm/root"
if [ ! -d "$D/env" ]; then
  echo "== conda-forge: ld64_osx-64 cctools_osx-64 (and what they need)"
  ./bin/micromamba create -y -p "$D/env" -c conda-forge --override-channels ld64_osx-64 cctools_osx-64 2>&1 | tail -60
fi
echo "== packages (name, version, build, channel)"
./bin/micromamba list -p "$D/env" 2>&1
echo "== package files as downloaded"
ls -1 "$D/mm/root/pkgs" 2> /dev/null | grep -E "\.(conda|tar\.bz2)$"
echo "== tools"
ls "$D/env/bin"
echo "== ld -v"
"$D"/env/bin/x86_64-apple-darwin*-ld -v 2>&1
