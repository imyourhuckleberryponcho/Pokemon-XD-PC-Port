#!/usr/bin/env python3
"""arm64 cross-compile check (docs/MAC_ARM.md, round 2): every C / C++ file of an existing LINUX build tree is compiled with
clang for aarch64 (-fsyntax-only, no link, nothing run), against an arm64 C / C++ library that is downloaded and unpacked
into a folder of its own (no root needed). It finds what a native arm64 build would stop at: x86-only code (intrinsics,
register names), 32-bit-only system flags (MAP_32BIT), x86 assembly. It cannot find Apple-only problems (those need the
macOS SDK: the workflow stage arm64-probe).

In WSL (Ubuntu 24.04), from the port folder:
    python3 tools/mac/arm64_syntax_check.py --build <linux build tree, e.g. build/linux/linux-window> --work <scratch folder>
        [--old-root <path the tree was configured under> --new-root <path to use now>] [--jobs 4]
Steps: `apt-get download` of libc6-dev-arm64-cross, linux-libc-dev-arm64-cross, libstdc++-13-dev-arm64-cross,
libgcc-13-dev-arm64-cross, libc6-arm64-cross (once), `dpkg -x` into <work>/sysroot, then `ninja -t compdb` of the tree.
Result: <work>/arm64_errors.txt (every failing file with its clang messages) and a summary line. Libraries the Linux
build links (zlib, X11 ...) have no arm64 headers here, so files that include them fail with "file not found": ignore those.
"""
import argparse, json, os, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ap = argparse.ArgumentParser()
ap.add_argument("--build", required=True)
ap.add_argument("--work", required=True)
ap.add_argument("--old-root", default="")
ap.add_argument("--new-root", default="")
ap.add_argument("--jobs", type=int, default=4)
ap.add_argument("--target", default="aarch64-linux-gnu")
a = ap.parse_args()
work = os.path.abspath(a.work)
sysroot = os.path.join(work, "sysroot")
os.makedirs(work, exist_ok=True)
if not os.path.isdir(os.path.join(sysroot, "usr", "aarch64-linux-gnu", "include")):
    debs = os.path.join(work, "debs")
    os.makedirs(debs, exist_ok=True)
    subprocess.run(["apt-get", "download", "libc6-dev-arm64-cross", "linux-libc-dev-arm64-cross", "libc6-arm64-cross",
                    "libstdc++-13-dev-arm64-cross", "libgcc-13-dev-arm64-cross"], cwd=debs, check=True)
    for f in sorted(os.listdir(debs)):
        if f.endswith(".deb"):
            subprocess.run(["dpkg", "-x", os.path.join(debs, f), sysroot], check=True)
inc = os.path.join(sysroot, "usr", "aarch64-linux-gnu", "include")
db = json.loads(subprocess.run(["ninja", "-t", "compdb"], cwd=a.build, capture_output=True, text=True, check=True).stdout)
jobs = []
for e in db:
    f = e["file"]
    if not f.endswith((".c", ".cpp")) or " -c " not in e["command"]:
        continue
    if "_deps" in f or "third_party" in f or "/aurora" in f:
        continue
    cmd = e["command"]
    if a.old_root:
        cmd = cmd.replace(a.old_root, a.new_root)
    args = shlex.split(cmd)
    out, i = [], 0
    while i < len(args):
        x = args[i]
        if x in ("-o", "-MF", "-MT", "-MQ"):
            i += 2
            continue
        if x in ("-MD", "-MMD", "-c"):
            i += 1
            continue
        out.append(x)
        i += 1
    out[0] = "clang++" if f.endswith(".cpp") else "clang"
    out += ["--target=" + a.target, "-fsyntax-only", "-nostdlibinc", "-isystem", inc + "/c++/13", "-isystem", inc + "/c++/13/aarch64-linux-gnu",
            "-isystem", inc, "-Wno-everything", "-ferror-limit=0"]
    jobs.append((f.replace(a.old_root, a.new_root) if a.old_root else f, out, e["directory"].replace(a.old_root, a.new_root) if a.old_root else e["directory"]))
print(len(jobs), "translation units", flush=True)


def run(j):
    f, args, d = j
    try:
        r = subprocess.run(args, cwd=d, capture_output=True, text=True, timeout=300)
    except Exception as ex:
        return f, -1, str(ex)
    return f, r.returncode, r.stderr


bad = []
with ThreadPoolExecutor(a.jobs) as ex:
    for f, rc, err in ex.map(run, jobs):
        if rc != 0:
            bad.append((f, rc, err))
open(os.path.join(work, "arm64_errors.txt"), "w").write("\n".join("=== %s (rc %s)\n%s" % (f, rc, err[:4000]) for f, rc, err in bad))
print("compiled for %s: %d ok, %d failed (details: %s)" % (a.target, len(jobs) - len(bad), len(bad), os.path.join(work, "arm64_errors.txt")))
sys.exit(1 if bad else 0)
