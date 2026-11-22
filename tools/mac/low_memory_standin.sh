#!/bin/bash
# The Mac build's "memory below 4 GB" pieces built and run on Linux (docs/MAC.md, "Memory below 4 GB").
#   tools/mac/low_memory_standin.sh <repo folder> <output folder>
# Builds, with Clang: the block keeper's own test (platform/tests/low_arena_test.cpp), the same with the
# undefined-behaviour checker, and "low_malloc_mac_test": the Mac's malloc replacement
# (platform/os/low_malloc_mac.cpp) with a stand-in for Apple's zone calls, where EVERY malloc of the test
# process (the C and C++ libraries' own too) is answered by the keeper. Runs all three.
set -u
R="$1"; O="$2"; mkdir -p "$O"; cd "$O" || exit 1
CC="${CC:-clang}"; CXX="${CXX:-clang++}"
W="-Wall -Wextra -Wno-unused-parameter"
I="-I$R/platform/os/include"
$CC -O2 -g $W -c "$R/platform/os/low_arena.c" $I -o low_arena.o || exit 1
$CC -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined $W -c "$R/platform/os/low_arena.c" $I -o low_arena_ub.o || exit 1
$CXX -O2 -g $W -std=c++20 "$R/platform/tests/low_arena_test.cpp" low_arena.o $I -lpthread -o low_arena_test || exit 1
$CXX -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined $W -std=c++20 "$R/platform/tests/low_arena_test.cpp" low_arena_ub.o $I -lpthread -o low_arena_test_ub || exit 1
$CXX -O2 -g $W -std=c++20 -DPORT_TEST_MAC_HALVES -DLOW_TEST_WITH_MALLOC "$R/platform/tests/low_arena_test.cpp" \
  "$R/platform/os/low_malloc_mac.cpp" "$R/platform/os/low_malloc_selftest.cpp" low_arena.o $I -lpthread -o low_malloc_mac_test || exit 1
RESULT=0
for t in low_arena_test low_arena_test_ub low_malloc_mac_test; do
  echo "== $t"
  /usr/bin/time -f "%e s, %M KB" ./$t
  RC=$?
  echo "exit code $RC"
  [ "$RC" = 0 ] || RESULT=1
done

# Real programs on the keeper: the same code as a library put in front of the C library (LD_PRELOAD), so every
# malloc of Python and of the compiler goes through it. (Two more names Linux programs call are added here.)
cat > preload_extra.c <<'EOT'
#include "port_low_arena.h"
size_t malloc_usable_size(void* p) { return PortLow_Owns(p) ? PortLow_Size(p) : 0; }
void* memalign(size_t a, size_t n) { return PortLow_Memalign(a, n); }
EOT
$CC -O2 -g -fPIC $W -c "$R/platform/os/low_arena.c" $I -o low_arena_pic.o || exit 1
$CC -O2 -g -fPIC $W -c preload_extra.c $I -o preload_extra.o || exit 1
$CXX -O2 -g -fPIC $W -std=c++20 -DPORT_TEST_MAC_HALVES -shared "$R/platform/os/low_malloc_mac.cpp" low_arena_pic.o preload_extra.o \
  $I -lpthread -o liblow_preload.so || exit 1
echo "== python3 on the keeper"
LD_PRELOAD="$PWD/liblow_preload.so" python3 -c '
import json, random, threading
random.seed(5)
def work(k):
    d = {}
    for i in range(200000):
        d[i % 5000] = [random.random() for _ in range(i % 50)]
    s = json.dumps([list(range(1000)) for _ in range(300)])
    assert len(json.loads(s)) == 300
    b = bytearray(64 << 20); b[-1] = 1
t = [threading.Thread(target=work, args=(k,)) for k in range(4)]
[x.start() for x in t]; [x.join() for x in t]
maps = [l for l in open("/proc/self/maps") if "[heap]" in l]
print("python3: done; the C library has a heap of its own:", "yes (NOT on the keeper)" if maps else "no (every malloc came from the keeper)")
'
RC=$?; echo "exit code $RC"; [ "$RC" = 0 ] || RESULT=1
echo "== the compiler on the keeper"
LD_PRELOAD="$PWD/liblow_preload.so" $CXX -O2 -std=c++20 -c "$R/platform/tests/low_arena_test.cpp" $I -o preload_compiled.o
RC=$?; echo "exit code $RC"; [ "$RC" = 0 ] || RESULT=1
$CXX -O2 -std=c++20 -c "$R/platform/tests/low_arena_test.cpp" $I -o plain_compiled.o
cmp preload_compiled.o plain_compiled.o && echo "the object file is the same as without the keeper" || RESULT=1
exit $RESULT
