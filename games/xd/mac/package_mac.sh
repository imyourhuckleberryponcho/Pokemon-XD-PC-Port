#!/bin/bash
# Makes the Mac player folder and zip from a finished build (docs/MAC.md, "Packaging"). Runs on a Mac only.
# NEVER RUN BY ITS AUTHOR (no Mac): GitHub's Mac machines are the only place it runs so far.
#
#   games/xd/mac/package_mac.sh [build bin folder] [output folder]
#   defaults: build/mac/mac-release/bin   dist
#
# Result:  <out>/Pokemon XD PC <version> - Mac (test)/Pokemon XD.app + Read me.txt
#          <out>/Pokemon XD PC <version> - Mac (test).zip
# The app holds the launcher (xd_launcher: the app's main program, what a double-click starts), the game program
# (xd, started by the launcher; x86_64, both run through Rosetta 2 on Apple silicon), the libraries they load and
# the licences. No game data: the player picks his own disc image in the launcher.
# The game stays startable without the launcher (tests, scripts): Contents/MacOS/xd, or the launcher with
# XD_NO_LAUNCHER=1 (docs/MAC.md, "Launcher on the Mac"). A build without xd_launcher gives the old app, whose main
# program is xd (the log says so).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-$ROOT/build/mac/mac-release/bin}"
OUT="${2:-$ROOT/dist}"
VERSION="$(sed -n 's/^project(xd_port VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "package_mac.sh: no version in CMakeLists.txt"; exit 1; }
[ -x "$BIN/xd" ] || { echo "package_mac.sh: $BIN/xd is missing (build first: ./build_mac.sh)"; exit 1; }
NAME="Pokemon XD PC $VERSION - Mac (test)"
DEST="$OUT/$NAME"
APP="$DEST/Pokemon XD.app"
rm -rf "$DEST" "$OUT/$NAME.zip"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/licenses"

cp "$BIN/xd" "$APP/Contents/MacOS/xd"
MAIN=xd
if [ -x "$BIN/xd_launcher" ]; then
  cp "$BIN/xd_launcher" "$APP/Contents/MacOS/xd_launcher"
  MAIN=xd_launcher
  echo "  launcher: included, the app's main program"
else
  echo "  launcher: NOT included ($BIN/xd_launcher is missing): the app starts the game directly, as before"
fi
sed -e "s/@VERSION@/$VERSION/g" -e "s/@MAIN@/$MAIN/g" "$HERE/Info.plist.in" > "$APP/Contents/Info.plist"
# The app's picture (original art, games/xd/resources/xd_icon.png): sips and iconutil are Apple's own tools.
# Best effort: without the picture the app just shows the system's plain icon.
ICON_PNG="$ROOT/games/xd/resources/xd_icon.png"
if [ -f "$ICON_PNG" ] && command -v sips >/dev/null 2>&1 && command -v iconutil >/dev/null 2>&1; then
  ICONSET="$OUT/.AppIcon.iconset"
  rm -rf "$ICONSET"; mkdir -p "$ICONSET"
  for sz in 16 32 64 128 256; do
    sips -z $sz $sz "$ICON_PNG" --out "$ICONSET/icon_${sz}x${sz}.png" >/dev/null 2>&1 || true
  done
  # (the 256 picture is the largest we have: it also serves as the 128 @2x and the 16/32 @2x of the smaller ones)
  cp "$ICONSET/icon_32x32.png" "$ICONSET/icon_16x16@2x.png" 2>/dev/null || true
  cp "$ICONSET/icon_64x64.png" "$ICONSET/icon_32x32@2x.png" 2>/dev/null || true
  cp "$ICONSET/icon_256x256.png" "$ICONSET/icon_128x128@2x.png" 2>/dev/null || true
  rm -f "$ICONSET/icon_64x64.png"
  if iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns" 2>&1; then
    echo "  icon: AppIcon.icns made"
  else
    echo "  icon: iconutil failed (the app has the plain icon)"
  fi
  rm -rf "$ICONSET"
else
  echo "  icon: not made (xd_icon.png, sips or iconutil missing)"
fi

# Where things go inside the app (run 9 on GitHub's Mac, 2026-11-11: signing failed with everything in
# Contents/MacOS. Apple's rule: that folder holds programs only; anything else there is taken for code that
# must carry a signature of its own, and a folder there is refused):
#   Contents/MacOS/xd            the program
#   Contents/Frameworks/*.dylib  the libraries it loads
#   Contents/Resources/...       data files (shader list, fonts, version.txt), with a link to each in
#                                Contents/MacOS, because the program looks for them next to itself
FW="$APP/Contents/Frameworks"
mkdir -p "$FW"
BUILD_ROOT="$(cd "$BIN/.." && pwd)"
TODO="$OUT/.package_mac_todo"

# Every library the program loads that is not the system's, and every such library those load in turn: copied
# into Frameworks, and each user told to look there. Looked for at the path named, in the build's bin folder,
# then anywhere in the build.
find_library() { # <name as its user spells it>: prints the file
  base="$(basename "$1")"
  if [ -f "$1" ]; then echo "$1"; return; fi
  if [ -f "$BIN/$base" ]; then echo "$BIN/$base"; return; fi
  find "$BUILD_ROOT" -name "$base" -not -path "*/dist/*" 2>/dev/null | head -n 1
}
needs_of() { otool -L "$1" | tail -n +2 | awk '{print $1}'; }
bundle_needs() { # <file inside the app> <the prefix its libraries get>
  user="$1"
  prefix="$2"
  needs_of "$user" > "$TODO.needs"
  while read -r lib; do
    case "$lib" in /usr/lib/*|/System/*|@executable_path/*|@loader_path/*) continue ;; esac
    base="$(basename "$lib")"
    if [ "$base" = "$(basename "$user")" ]; then continue; fi # (a library's own name)
    if [ ! -f "$FW/$base" ]; then
      src="$(find_library "$lib")"
      if [ -z "$src" ]; then echo "package_mac.sh: library $lib (needed by $(basename "$user")) not found"; exit 1; fi
      cp -L "$src" "$FW/$base"
      chmod u+w "$FW/$base"
      install_name_tool -id "@executable_path/../Frameworks/$base" "$FW/$base" 2>&1 || true
      echo "  library: $base  (from $src, for $(basename "$user"))"
      echo "$base" >> "$TODO"
    fi
    install_name_tool -change "$lib" "$prefix/$base" "$user" 2>&1
  done < "$TODO.needs"
}
: > "$TODO"
bundle_needs "$APP/Contents/MacOS/xd" "@executable_path/../Frameworks"
if [ -f "$APP/Contents/MacOS/xd_launcher" ]; then
  bundle_needs "$APP/Contents/MacOS/xd_launcher" "@executable_path/../Frameworks"
fi
# (libraries of libraries: the list grows while it is worked through)
DONE=0
while [ "$DONE" -lt "$(wc -l < "$TODO" | tr -d ' ')" ]; do
  DONE=$((DONE + 1))
  bundle_needs "$FW/$(sed -n "${DONE}p" "$TODO")" "@loader_path"
done
rm -f "$TODO" "$TODO.needs"
# Nothing may still point into a package manager's folders (a player's Mac has none).
LEFT=0
for f in "$APP/Contents/MacOS/xd" "$APP/Contents/MacOS/xd_launcher" "$FW"/*.dylib; do
  [ -f "$f" ] || continue
  echo "  $(basename "$f") asks for:"
  needs_of "$f" | sed 's/^/      /'
  if needs_of "$f" | grep -q -E "^(/usr/local|/opt/|@rpath)"; then
    echo "package_mac.sh: $(basename "$f") still asks for a library outside the app and the system"
    LEFT=1
  fi
done
[ "$LEFT" = 0 ] || exit 1

# Files the build puts next to the program and the programs read at run time (the shipped shader list, fonts: the
# launcher's own Inter font files are in the build's fonts folder too, from the xd_launcher target).
for f in initial_pipeline_cache.db; do
  if [ -f "$BIN/$f" ]; then
    cp "$BIN/$f" "$APP/Contents/Resources/$f"
    ln -s "../Resources/$f" "$APP/Contents/MacOS/$f"
  fi
done
if [ -d "$BIN/fonts" ]; then
  cp -R "$BIN/fonts" "$APP/Contents/Resources/fonts"
  ln -s "../Resources/fonts" "$APP/Contents/MacOS/fonts"
fi
# The Battle Revolution models helper (tools/pbr_convert/package_pbr_models.py, built by the workflow's step before
# this one; PBR_MODELS_DIR names its folder): the launcher's Extra assets page looks for
# <program folder>/pbr_models/pbr_models, and the program folder is Contents/MacOS. Same rule as the fonts: the files
# live in Resources, Contents/MacOS gets a link (a real folder there is refused by the signing).
# UNTESTED on a Mac. It is left out, with a note in the log, when it was not built.
PBR_MODELS_DIR="${PBR_MODELS_DIR:-$ROOT/build/pbr_models_dist_mac/pbr_models}"
if [ -x "$PBR_MODELS_DIR/pbr_models" ]; then
  cp -R "$PBR_MODELS_DIR" "$APP/Contents/Resources/pbr_models"
  ln -s "../Resources/pbr_models" "$APP/Contents/MacOS/pbr_models"
  echo "  pbr_models: included (from $PBR_MODELS_DIR)"
else
  echo "  pbr_models: NOT included ($PBR_MODELS_DIR/pbr_models is not there)"
fi
echo "$VERSION" > "$APP/Contents/Resources/version.txt"
ln -s "../Resources/version.txt" "$APP/Contents/MacOS/version.txt"
# The first tester's report script (checks this Mac and the program, starts the game once without a disc, writes one
# text file to send back): next to the Read me and inside the app.
cp "$HERE/first_run_report.sh" "$APP/Contents/Resources/first_run_report.sh"
cp "$HERE/first_run_report.sh" "$DEST/first_run_report.sh"
chmod +x "$APP/Contents/Resources/first_run_report.sh" "$DEST/first_run_report.sh"

# Licences of what is inside (the same texts the Windows package carries).
for f in "$ROOT/LICENSE" "$ROOT/third_party/aurora/LICENSE" "$ROOT/third_party/SDL/LICENSE.txt" \
         "$ROOT/third_party/borealis/LICENSE" "$ROOT/third_party/monocypher/LICENCE.md"; do
  if [ -f "$f" ]; then cp "$f" "$APP/Contents/Resources/licenses/$(basename "$(dirname "$f")")-$(basename "$f")"; fi
done

sed "s/@VERSION@/$VERSION/g" "$HERE/Read me (Mac).txt" > "$DEST/Read me.txt"

# No game data may be inside (the same rule as every package of the port).
if find "$DEST" \( -iname "*.iso" -o -iname "*.gcm" -o -iname "*.rvz" -o -iname "*.dol" -o -iname "*.gci" -o -iname "*.fsys" \) | grep -q .; then
  echo "package_mac.sh: game data found in the package"; exit 1
fi

# Signed "ad hoc" (no developer account, no cost): it does not make the app trusted, but a changed program file
# with no signature at all is refused outright on some systems.
# Inside out, each library first and the app last, without --deep (Apple, "Creating distribution-signed code for
# macOS": "Don't pass the --deep option to codesign when you sign code"). install_name_tool above changed the
# program and the libraries after the linker signed them, so --force replaces those signatures.
SIGNED=1
sign() { # <file or app>: codesign's own words go into the log
  echo "  signing: ${1#"$DEST"/}"
  if ! codesign --force --sign - "$1" 2>&1; then
    SIGNED=0
    echo "  SIGNING FAILED: ${1#"$DEST"/}"
  fi
}
for lib in "$FW"/*.dylib; do
  [ -f "$lib" ] || continue
  sign "$lib"
done
# The helper's own programs and libraries (PyInstaller's; they were changed by being copied and its libnod is ours):
# each one ad hoc, as the libraries above. Their failure does not fail the app's signature.
if [ -d "$APP/Contents/Resources/pbr_models" ]; then
  find "$APP/Contents/Resources/pbr_models" -type f | while read -r f; do
    if file -b "$f" | grep -q "Mach-O"; then codesign --force --sign - "$f" 2>&1 || echo "  (could not sign ${f#"$DEST"/})"; fi
  done
fi
# (the programs in Contents/MacOS: the main one is signed with the app, but the other one is a second program that
# the app's seal only records as a file: ad hoc signed by itself first, so it carries a signature of its own)
for prog in xd xd_launcher; do
  if [ -f "$APP/Contents/MacOS/$prog" ]; then sign "$APP/Contents/MacOS/$prog"; fi
done
sign "$APP"
[ "$SIGNED" = 1 ] || echo "package_mac.sh: ad hoc signing failed (the app is unsigned or half signed)"
echo "  checking the signature:"
codesign --verify --strict --verbose=4 "$APP" 2>&1 || echo "package_mac.sh: the app's signature does NOT verify"
echo "  the app's files:"
(cd "$DEST" && find "Pokemon XD.app" | sed 's/^/    /')

(cd "$OUT" && ditto -c -k --sequesterRsrc --keepParent "$NAME" "$NAME.zip")
echo "package_mac.sh: $OUT/$NAME.zip"
