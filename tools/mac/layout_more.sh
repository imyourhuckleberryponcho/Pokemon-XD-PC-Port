#!/bin/bash
# The rest of the Mac layout check (after tools/mac/layout_check.sh, which must have run into the same folder):
#   A. games/xd/mac/first_run_report.sh on the rightly laid-out program and on the deliberately wrong one, with an
#      "otool" from the ld64 folder on PATH (LLVM's, then Apple's own cctools one)
#   B. every link option alone, and all options but one
#   C. the link through Clang's driver (what CMake does on a Mac), -v to see what the driver adds
#   D. bigger programs: does -no_huge matter (a program of the game's size; and Wine's case, where it does)
# Apple's open-source linker ld64 on Linux (tools/mac/get_ld64.sh). No Apple SDK. docs/MAC.md, "Layout check, second
# part". Usage: layout_more.sh <repo folder> <layout folder of layout_check.sh> [ld64 folder, default ~/mac_ld64]
W="$1"; O="$2"; D="${3:-$HOME/mac_ld64}"
LD="$(ls "$D"/env/bin/x86_64-apple-darwin*-ld | head -1)"; OBJDUMP="$D/env/bin/llvm-objdump"; NM="$D/env/bin/llvm-nm"
cd "$O" || exit 1
[ -f xd_layout ] && [ -f xd_no_fixup_chains_off ] && [ -f main_free.o ] || { echo "run layout_check.sh first"; exit 1; }
BASE="-arch x86_64 -platform_version macos 13.3 13.3 -e _main libSystem.tbd"
"$LD" -v 2>&1 | head -1

echo; echo "################ A. first_run_report.sh on the two programs"
bash -n "$W/games/xd/mac/first_run_report.sh" && echo "bash -n: OK"
for tool in llvm-otool "$(basename "$(ls "$D"/env/bin/x86_64-apple-darwin*-otool | head -1)")"; do
  rm -rf "$O/otoolbin"; mkdir -p "$O/otoolbin"; ln -s "$D/env/bin/$tool" "$O/otoolbin/otool"
  for prog in xd_layout xd_no_fixup_chains_off; do
    echo "=================== report on $prog   (otool = $tool)"
    PATH="$O/otoolbin:$PATH" XD_REPORT_FILE="$O/report_${prog}_${tool%%-*}.txt" bash "$W/games/xd/mac/first_run_report.sh" "$O/$prog" > "$O/report_run.txt" 2>&1
    sed -n '/^In short:/,/^Please send/p' "$O/report_run.txt"
    echo "-- section 2 of the file, the segment lines:"
    sed -n '/^segments (name/,/^$/p' "$O/report_${prog}_${tool%%-*}.txt"
  done
done
rm -rf "$O/otoolbin"

summary() { # <program>: the linker's words and the layout in a few lines
  grep -v "directory not found for option" "$1.link.txt" | sed 's/^/    linker: /'
  [ -f "$1" ] || { echo "    NO PROGRAM WRITTEN"; return; }
  "$OBJDUMP" --macho --private-headers "$1" > "$1.headers.txt" 2>&1
  "$NM" -n "$1" > "$1.nm.txt" 2>&1
  python3 "$W/tools/mac/layout_read.py" "$1" "$1.headers.txt" "$1.nm.txt" > "$1.layout.txt" 2>&1
  grep -E "^__(PAGEZERO|TEXT|XDLOW|XDMEM1|XDMEMU|XDLC|XDSTK) " "$1.layout.txt" | sed -E 's/^(__[A-Z0-9]+) +vmaddr ([^ ]+) vmsize ([^ ]+) .*initprot [^ ]+ +(.*)$/    \1 \2 size \3  \4/'
  grep -E "^(PIE flag|MISSING|LAYOUT)|markers WRONG" "$1.layout.txt" | sed 's/^/    /'
}
link() { # <name> <objects> <options>
  rm -f "$1"
  "$LD" $BASE $3 $2 -o "$1" > "$1.link.txt" 2>&1; echo "    linker exit code: $?"
  summary "$1"
}

echo; echo "################ B. each option alone, and all but one"
mapfile -t LINES < <(python3 "$W/tools/mac/mac_link_options.py" "$W" --ld)
ALIASES=""; OPTS=()
for l in "${LINES[@]}"; do case "$l" in -alias*) ALIASES="$ALIASES $l" ;; *) OPTS+=("$l") ;; esac; done
OBJS="main_free.o musyx.o mac_low_reserve.o"
echo "=================== none of the options (only the three -alias lines, without which it cannot link)"
link b_none "$OBJS" "$ALIASES"
i=0
for o in "${OPTS[@]}"; do
  i=$((i + 1))
  echo "=================== alone: $o"
  link "b_alone_$i" "$OBJS" "$ALIASES $o"
done
i=0
for o in "${OPTS[@]}"; do
  i=$((i + 1)); rest=""
  for p in "${OPTS[@]}"; do [ "$p" = "$o" ] || rest="$rest $p"; done
  echo "=================== all but: $o"
  link "b_without_$i" "$OBJS" "$ALIASES $rest"
done
echo "=================== without the -alias lines (expected: cannot link, the test program calls the second names)"
link b_noalias "$OBJS" "${OPTS[*]}"

echo; echo "################ A2. first_run_report.sh on the program linked with none of the options (4 GB page zero, PIE, chained fixups)"
mkdir -p "$O/otoolbin"; ln -sf "$D/env/bin/llvm-otool" "$O/otoolbin/otool"
PATH="$O/otoolbin:$PATH" XD_REPORT_FILE="$O/report_b_none.txt" bash "$W/games/xd/mac/first_run_report.sh" "$O/b_none" > "$O/report_run.txt" 2>&1
sed -n '/^In short:/,/^Please send/p' "$O/report_run.txt"
rm -rf "$O/otoolbin"

echo; echo "################ C. through Clang's driver (the options in CMake's -Wl, form)"
DRV="$(python3 "$W/tools/mac/mac_link_options.py" "$W" | tr '\n' ' ')"
rm -f c_driver
clang -v -target x86_64-apple-macos13.3 -fuse-ld="$LD" -mlinker-version=956.6 -nostdlib -L. -lSystem $DRV \
  main_free.o musyx.o mac_low_reserve.o -o c_driver > c_driver.link.txt 2>&1
echo "    driver exit code: $?"
echo "    the linker line the driver wrote:"; grep -E '^ *"[^"]*-ld"' c_driver.link.txt | sed 's/^/    /'
grep -v -E '^ *"|^clang version|^Ubuntu clang|^Target:|^Thread model|^InstalledDir|^Found candidate|^Selected|^Candidate' c_driver.link.txt > c_driver.link2.txt; mv c_driver.link2.txt c_driver.link.txt
summary c_driver
echo "=================== the driver with libSystem.tbd as the only library, WITHOUT -nostdlib (it adds its own libraries; none exist here)"
rm -f c_driver2
clang -v -target x86_64-apple-macos13.3 -fuse-ld="$LD" -mlinker-version=956.6 -L. $DRV main_free.o musyx.o mac_low_reserve.o -o c_driver2 > c_driver2.all.txt 2>&1
echo "    driver exit code: $?"
grep -E '^ *"[^"]*-ld"' c_driver2.all.txt | sed 's/^/    /'
grep -E "^ld: |error" c_driver2.all.txt | head -5 | sed 's/^/    /'

echo; echo "################ D. bigger programs: does -no_huge matter"
T="-target x86_64-apple-macos13.3 -ffreestanding -nostdinc -O1"
# D1: a program of the game's order of size: 96 MB of code in the game code section, 400 MB of zero-filled data
cat > big_code.s <<'EOF'
        .section __TEXT,xdg_text,regular,pure_instructions
        .globl _big_code
_big_code:
        .space 0x6000000, 0x90
        ret
EOF
cat > big_data.c <<'EOF'
char big_zero2[0x19000000]; /* 400 MB */
extern void big_code(void);
void *keep_big[] = {big_zero2, (void *)big_code};
EOF
clang $T -c big_code.s -o big_code.o && clang $T -c big_data.c -o big_data.o || exit 2
ALL="$ALIASES ${OPTS[*]}"
echo "=================== D1 game-sized program (96 MB code, 464 MB zero data), the port's options"
link d1_with "$OBJS big_code.o big_data.o" "$ALL"
grep -E "^__(DATA|LINKEDIT) " d1_with.layout.txt | awk '{printf "    %-11s %s size %s end %s\n", $1, $3, $5, $7}'
echo "=================== D1 the same without -no_huge"
link d1_without "$OBJS big_code.o big_data.o" "${ALL/-no_huge/}"
grep -E "^__(DATA|LINKEDIT) " d1_without.layout.txt | awk '{printf "    %-11s %s size %s end %s\n", $1, $3, $5, $7}'
cmp -s d1_with d1_without && echo "    the two program files are byte for byte the same" || echo "    the two program files differ ($(cmp d1_with d1_without | head -1))"
ls -l d1_with | awk '{print "    program file size: " $5 " bytes"}'
rm -f d1_with d1_without big_code.o
# D2: Wine's case (loader/main.c + configure.ac): one 8 GB reserve from 0x1000, image at 0x200000000
cat > wine_like.c <<'EOF'
__asm__(".zerofill WINE_RESERVE,WINE_RESERVE,___wine_reserve,0x1fffff000");
int main(void) { return 0; }
EOF
cat > wine_like2.c <<'EOF'
char two_mb[0x200000]; int set_data = 7; const int const_data[4] = {1, 2, 3, 4};
int more_code(void) { two_mb[1] = 1; return set_data + const_data[1]; }
EOF
clang $T -c wine_like.c -o wine_like.o && clang $T -fno-common -c wine_like2.c -o wine_like2.o || exit 2
WINE="-pagezero_size 0x1000 -no_pie -image_base 0x200000000 -no_fixup_chains -segalign 0x1000 -segaddr WINE_RESERVE 0x1000"
for v in "-no_huge" ""; do
  echo "=================== D2 Wine's layout (8 GB reserve), ${v:-WITHOUT -no_huge}"
  rm -f d2
  "$LD" $BASE $WINE $v wine_like.o wine_like2.o -o d2> d2.link.txt 2>&1; echo "    linker exit code: $?"
  grep -v "directory not found for option" d2.link.txt | sed 's/^/    linker: /'
  [ -f d2 ] && "$OBJDUMP" --macho --private-headers d2 | awk '
    $1 == "segname" { seg = $2 } $1 == "sectname" { sect = $2 }
    $1 == "cmd" { incmd = $2 }
    incmd == "LC_SEGMENT_64" && $1 == "vmaddr" { va = $2 } incmd == "LC_SEGMENT_64" && $1 == "vmsize" { print "    segment " seg " at " va " size " $2 }
    $1 == "addr" { a = $2 } $1 == "size" { print "       section " sect " at " a " size " $2 }'
done
rm -f d2
# D3: the case the linker's source (ld64 passes/huge.cpp) describes: more than 2 GB of sizes counted before a later
# block. No fixed addresses; three 1 GB zero blocks and a 2 MB one.
cat > over2gb.c <<'EOF'
char gb1[0x40000000]; char gb2[0x40000000]; char gb3[0x40000000]; char mb2[0x200000]; char small[0x1000];
int main(void) { gb1[1] = 1; gb2[1] = 1; gb3[1] = 1; mb2[1] = 1; small[1] = 1; return 0; }
EOF
clang $T -fno-common -mcmodel=large -c over2gb.c -o over2gb.o || exit 2
for v in "-no_huge" ""; do
  echo "=================== D3 3 GB of ordinary zero data, no fixed addresses, ${v:-WITHOUT -no_huge}"
  rm -f d3
  "$LD" $BASE $v over2gb.o -o d3 > d3.link.txt 2>&1; echo "    linker exit code: $?"
  grep -v "directory not found for option" d3.link.txt | sed 's/^/    linker: /'
  [ -f d3 ] && "$OBJDUMP" --macho --private-headers d3 | awk '
    $1 == "segname" { seg = $2 } $1 == "sectname" { sect = $2 }
    $1 == "addr" { a = $2 } $1 == "size" { print "       section " seg "," sect " at " a " size " $2 }'
done
rm -f d3
echo; echo "layout_more.sh: finished"
