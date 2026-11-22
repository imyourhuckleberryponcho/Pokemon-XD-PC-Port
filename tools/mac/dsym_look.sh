#!/bin/bash
# What a Mac build's type information would look like (docs/MAC.md, "Members and arrays by name"): a small C++
# program compiled for the Apple target with -g, linked by Apple's open-source ld64 at the port's addresses, its
# debug information collected by LLVM's dsymutil into a .dSYM, and read back. Nothing of this is used by the build;
# it settles what the future Mac reader has to read. Usage: dsym_look.sh <repo folder> <out folder> [ld64 folder]
W="$1"; O="$2"; D="${3:-$HOME/mac_ld64}"
LD="$(ls "$D"/env/bin/x86_64-apple-darwin*-ld | head -1)"
mkdir -p "$O" && cd "$O" || exit 1
cat > members.cpp <<'EOF'
struct Log { int a; short b[3]; };
struct HeroMove { char pad[5]; Log leaderLog; float pos[3]; };
HeroMove heroMove = {{0}, {7, {1, 2, 3}}, {1.0f, 2.0f, 3.0f}};
int _inputData[4] = {10, 11, 12, 13};
static int file_static = 5;
namespace { int hidden = 6; }
extern "C" int main(void) { return heroMove.leaderLog.b[1] + _inputData[1] + file_static + hidden; }
EOF
printf -- '--- !tapi-tbd\ntbd-version: 4\ntargets: [ x86_64-macos ]\ninstall-name: /usr/lib/libSystem.B.dylib\n...\n' > libSystem.tbd
clang++ -target x86_64-apple-macos13.3 -ffreestanding -nostdinc -nostdinc++ -O1 -g -c members.cpp -o members.o || exit 2
OPTS="$(python3 "$W/tools/mac/mac_link_options.py" "$W" --ld --group low | tr '\n' ' ')"
"$LD" -arch x86_64 -platform_version macos 13.3 13.3 -e _main libSystem.tbd $OPTS members.o -o members 2>&1 | grep -v "directory not found"
echo "== debug sections left in the PROGRAM file (expected: none; Mach-O keeps them in the .o files):"
"$D/env/bin/llvm-objdump" --macho --section-headers members | grep -i -E "debug|dwarf" || echo "   none"
echo "== the program's notes of where the .o files are (N_OSO):"
"$D/env/bin/llvm-nm" -a members | grep -c " - " | sed 's/^/   debug notes in the symbol table: /'
rm -rf members.dSYM
"$D/env/bin/dsymutil" members -o members.dSYM 2>&1 | sed 's/^/   dsymutil: /'
F=members.dSYM/Contents/Resources/DWARF/members
ls -l "$F" | awk '{print "== dSYM file: " $5 " bytes"}'
echo "== its sections:"
"$D/env/bin/llvm-objdump" --macho --section-headers "$F" | grep -E "__debug|__apple" | awk '{print "   " $2 " size 0x" $3}'
echo "== DWARF version and unit header:"
"$D/env/bin/llvm-dwarfdump" --debug-info "$F" | grep -m1 "Compile Unit" | sed 's/^/   /'
echo "== the forms used (which decide what the reader must know):"
"$D/env/bin/llvm-dwarfdump" --debug-abbrev "$F" | grep -o "DW_FORM_[a-z0-9_]*" | sort | uniq -c | sed 's/^/   /'
echo "== heroMove, leaderLog, _inputData as the dSYM has them:"
"$D/env/bin/llvm-dwarfdump" --debug-info "$F" | grep -E -A8 'DW_AT_name\s+\("(heroMove|leaderLog|_inputData|file_static|hidden|b)"\)' | grep -E "DW_AT_name|DW_AT_location|DW_AT_data_member_location" | sed 's/^ */   /'
echo "== the same variables in the program's symbol table:"
"$D/env/bin/llvm-nm" members | grep -E "heroMove|_inputData|file_static|hidden" | sed 's/^/   /'
