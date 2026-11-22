#!/usr/bin/env python3
"""The Mac program's link options, read out of the CMake files (never retyped).

Prints, one per line, what the Apple branches give the game program `xd`:
  XD_MAC_LOW_LINK_OPTIONS      (top CMakeLists.txt)
  XD_PROGRAM_LINK_OPTIONS      (games/xd/CMakeLists.txt, the APPLE branch)
  -rdynamic and the -alias lines (games/xd/CMakeLists.txt, the non-MSVC block's APPLE branch)
as compiler-driver options (-Wl,a,b), the form CMake hands to clang. --ld prints them as the linker itself gets
them (-Wl,a,b -> a b; -rdynamic -> -export_dynamic, which is what Clang's Darwin driver passes).
--group NAME prints one group only: low | program | extra.

Used by tools/mac/layout_check.sh (docs/MAC.md, "Layout check with Apple's ld64 on Linux").
Usage: mac_link_options.py <repo folder> [--ld] [--group low|program|extra]
"""
import re
import sys


def strip_comments(text):
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def set_args(text, name, must_contain=None):
    """Arguments of every set(<name> ...) in the text."""
    found = []
    for m in re.finditer(r"\bset\s*\(\s*" + re.escape(name) + r"\b([^)]*)\)", text):
        args = m.group(1).split()
        if must_contain is None or any(must_contain in a for a in args):
            found.append(args)
    return found


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    repo = args[0]
    as_ld = "--ld" in sys.argv
    group = None
    if "--group" in sys.argv:
        group = sys.argv[sys.argv.index("--group") + 1]
        args = [a for a in args if a != group]

    top = strip_comments(open(repo + "/CMakeLists.txt", encoding="utf-8", errors="replace").read())
    xd = strip_comments(open(repo + "/games/xd/CMakeLists.txt", encoding="utf-8", errors="replace").read())

    low = set_args(top, "XD_MAC_LOW_LINK_OPTIONS")
    if len(low) != 1:
        sys.exit("mac_link_options: expected one set(XD_MAC_LOW_LINK_OPTIONS ...) in CMakeLists.txt, found %d" % len(low))
    low = low[0]

    prog = set_args(xd, "XD_PROGRAM_LINK_OPTIONS", "XD_MAC_LOW_LINK_OPTIONS")
    if len(prog) != 1:
        sys.exit("mac_link_options: expected one Apple set(XD_PROGRAM_LINK_OPTIONS ${XD_MAC_LOW_LINK_OPTIONS} ...), "
                 "found %d" % len(prog))
    prog = [a for a in prog[0] if a != "${XD_MAC_LOW_LINK_OPTIONS}"]

    extra = []
    if re.search(r"target_link_options\s*\(\s*xd\s+PRIVATE\s+-rdynamic\s*\)", xd):
        extra.append("-rdynamic")
    for m in re.finditer(r"target_link_options\s*\(\s*xd\s+PRIVATE([^)]*)\)", xd):
        extra += [a for a in m.group(1).split() if a.startswith("-Wl,-alias,")]

    groups = {"low": low, "program": prog, "extra": extra}
    chosen = groups[group] if group else low + prog + extra
    for opt in chosen:
        if not as_ld:
            print(opt)
        elif opt.startswith("-Wl,"):
            print(" ".join(opt[4:].split(",")))
        elif opt == "-rdynamic":
            print("-export_dynamic")
        else:
            print(opt)
    return 0


if __name__ == "__main__":
    sys.exit(main())
