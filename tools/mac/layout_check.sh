#!/bin/bash
# The Mac program's memory layout, linked by Apple's open-source linker (ld64, a Linux program from conda-forge:
# tools/mac/get_ld64.sh) with the port's link options read out of the CMake files (tools/mac/mac_link_options.py).
# No Apple SDK: freestanding objects, an empty hand-written libSystem.tbd. docs/MAC.md, "Layout check with Apple's
# ld64 on Linux". Usage: layout_check.sh <repo folder> <out folder> [ld64 folder, default ~/mac_ld64]
W="$1"; O="$2"; D="${3:-$HOME/mac_ld64}"
LD="$(ls "$D"/env/bin/x86_64-apple-darwin*-ld | head -1)"; OBJDUMP="$D/env/bin/llvm-objdump"; NM="$D/env/bin/llvm-nm"
mkdir -p "$O" && cd "$O" || exit 1
T="-target x86_64-apple-macos13.3 -ffreestanding -nostdinc -O1"
cat > main_free.c <<'EOF'
/* freestanding stand-in for the program: no system header */
#pragma clang section text = "__TEXT,xdg_text"
int game_fn(int a) { return a + 1; }
#pragma clang section text = ""
#pragma clang section rodata = "__DATA,xdg_const"
const int game_const[4] = {1, 2, 3, 4};
#pragma clang section rodata = ""
extern char text_start[] __asm__("section$start$__TEXT$xdg_text");
extern char text_end[] __asm__("section$end$__TEXT$xdg_text");
extern char const_start[] __asm__("section$start$__DATA$xdg_const");
extern char const_end[] __asm__("section$end$__DATA$xdg_const");
extern char mx_start[] __asm__("section$start$__DATA$mxstate");
extern char mx_end[] __asm__("section$end$__DATA$mxstate");
void *section_marks[] = {text_start, text_end, const_start, const_end, mx_start, mx_end};
/* the three names the -alias lines give a second name to, and callers of the second names */
void c2a(void) __asm__("__ZN5GSvecC2Ev"); void c2a(void) {}
void c2b(void) __asm__("__ZN5GSmtxC2Ev"); void c2b(void) {}
void d2c(void) __asm__("__ZN16GSBinWriteStreamD2Ev"); void d2c(void) {}
void c1a(void) __asm__("__ZN5GSvecC1Ev"); void c1b(void) __asm__("__ZN5GSmtxC1Ev");
void d1c(void) __asm__("__ZN16GSBinWriteStreamD1Ev");
char big_zero[0x4000000]; /* 64 MB of ordinary zero-filled data, as a real program has */
int some_global = 5;
int main(void) { c1a(); c1b(); d1c(); big_zero[5] = 1; return game_fn(some_global) + game_const[1]; }
EOF
cat > musyx.c <<'EOF'
#pragma clang section data = "__DATA,mxstate"
int musyx_zero[16];
int musyx_set = 3;
#pragma clang section data = ""
EOF
printf -- '--- !tapi-tbd\ntbd-version: 4\ntargets: [ x86_64-macos ]\ninstall-name: /usr/lib/libSystem.B.dylib\n...\n' > libSystem.tbd
clang $T -c "$W/games/xd/src/mac_low_reserve.c" -o mac_low_reserve.o || exit 2
clang $T -c main_free.c -o main_free.o || exit 2
clang $T -fno-zero-initialized-in-bss -c musyx.c -o musyx.o || exit 2
BASE="-arch x86_64 -platform_version macos 13.3 13.3 -e _main libSystem.tbd"
OPTS="$(python3 "$W/tools/mac/mac_link_options.py" "$W" --ld | tr '\n' ' ')"
"$LD" -v 2>&1 | head -1
link() { # <name> <options>
  echo "=================== $1"; echo "options: $2"
  rm -f "$1"
  "$LD" $BASE $2 main_free.o musyx.o mac_low_reserve.o -o "$1" > "$1.link.txt" 2>&1; echo "linker exit code: $?"; cat "$1.link.txt"
  [ -f "$1" ] || return
  "$OBJDUMP" --macho --private-headers "$1" > "$1.headers.txt" 2>&1
  "$NM" -n "$1" > "$1.nm.txt" 2>&1
  python3 "$W/tools/mac/layout_read.py" "$1" "$1.headers.txt" "$1.nm.txt"
}
link xd_layout "$OPTS"
link xd_no_huge "${OPTS/-no_huge /}"
link xd_no_fixup_chains_off "${OPTS/-no_fixup_chains /}"
