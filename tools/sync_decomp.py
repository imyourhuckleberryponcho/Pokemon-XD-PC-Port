"""Take a snapshot of the decomp's source code for the port to build from.

Why a snapshot instead of building straight from the decomp folder:
  - the decomp is changed all day and night by matching runs; a half-edited file would
    break the port build at random
  - a snapshot is tied to one decomp COMMIT, so a port bug can always be traced back to
    exactly which decomp code it was built from

This only READS the decomp (`git archive` of a commit). It never changes files there, and
uncommitted work in the decomp is not included.

Usage (from the port folder):
    python tools/sync_decomp.py              # snapshot the decomp's latest commit
    python tools/sync_decomp.py --commit 54bc2e35
    python tools/sync_decomp.py --list       # show existing snapshots

Result: snapshot/<commit>/{src,include,configure.py,tools/project.py,tools/ninja_syntax.py}, snapshot/<commit>/objects.json
(the per-file build list, see gen_build_list.py) and snapshot/CURRENT.txt naming the one in use.
"""
import argparse
import io
import os
import subprocess
import sys
import tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SNAP = os.path.join(ROOT, "snapshot")
DEFAULT_DECOMP = os.path.join(os.path.dirname(ROOT), "Pokemon-XD-Decomp")
# configure.py + tools/project.py describe how the decomp compiles each file (C or C++, extra
# include folders); gen_build_list.py turns them into the port's build list.
PATHS = ["src", "include", "configure.py", "tools/project.py", "tools/ninja_syntax.py"]
# Sub-projects the decomp builds from (MusyX, the audio engine), and which of their folders to copy.
SUBMODULES = ["extern/musyx"]
SUBMODULE_PATHS = ["src", "include", "LICENSE"]


def git(decomp, *args, binary=False):
    cmd = ["git", "-c", "safe.directory=*", "-C", decomp, *args]
    out = subprocess.run(cmd, check=True, capture_output=True)
    return out.stdout if binary else out.stdout.decode("utf-8", "replace").strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", default=DEFAULT_DECOMP)
    ap.add_argument("--commit", default="HEAD")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--no-current", action="store_true",
                    help="take the snapshot but leave CURRENT.txt as it is (a check build of a newer commit with "
                         "-DXD_SNAPSHOT_OVERRIDE=<folder> while other builds go on from the current one)")
    args = ap.parse_args()

    os.makedirs(SNAP, exist_ok=True)
    current_file = os.path.join(SNAP, "CURRENT.txt")

    if args.list:
        cur = open(current_file).read().split()[0] if os.path.exists(current_file) else None
        for d in sorted(os.listdir(SNAP)):
            if os.path.isdir(os.path.join(SNAP, d)):
                print(("* " if d == cur else "  ") + d)
        return

    sha = git(args.decomp, "rev-parse", "--short=10", args.commit)
    subject = git(args.decomp, "log", "-1", "--format=%cs %s", sha)
    dest = os.path.join(SNAP, sha)
    if os.path.exists(dest):
        print(f"Snapshot {sha} already exists; marking it current.")
    else:
        print(f"Snapshotting decomp commit {sha}: {subject}")
        data = git(args.decomp, "archive", "--format=tar", sha, *PATHS, binary=True)
        tmp = dest + ".partial"
        if os.path.exists(tmp):
            sys.exit(f"Leftover {tmp} from an interrupted run; rename or remove it first.")
        with tarfile.open(fileobj=io.BytesIO(data)) as tf:
            try:
                tf.extractall(tmp, filter="data")
            except TypeError:  # Python older than 3.12
                tf.extractall(tmp)
        os.rename(tmp, dest)
        n = sum(len(f) for _, _, f in os.walk(dest))
        print(f"Extracted {n} files to {dest}")

    # Sub-projects (git submodules) aren't in `git archive`: take each at the exact commit the
    # decomp commit records, from the sub-project's own repository (read-only).
    for sub in SUBMODULES:
        sub_dest = os.path.join(dest, sub)
        if os.path.exists(sub_dest):
            continue
        entry = git(args.decomp, "ls-tree", sha, sub).split()
        if len(entry) >= 3 and entry[1] == "tree":
            # A public copy of the decomp holds the sub-project as plain files of its own: take them from there.
            sub_sha = entry[2]
            data = git(args.decomp, "archive", "--format=tar", sub_sha, *SUBMODULE_PATHS, binary=True)
        elif len(entry) < 3 or entry[1] != "commit":
            print(f"Note: {sub} is not a sub-project in this decomp commit; skipped.")
            continue
        else:
            sub_sha = entry[2]
            data = git(os.path.join(args.decomp, sub), "archive", "--format=tar", sub_sha, *SUBMODULE_PATHS, binary=True)
        with tarfile.open(fileobj=io.BytesIO(data)) as tf:
            try:
                tf.extractall(sub_dest, filter="data")
            except TypeError:
                tf.extractall(sub_dest)
        print(f"Added {sub} at {sub_sha[:10]}")

    subprocess.run([sys.executable, os.path.join(HERE, "gen_build_list.py"), dest], check=True)

    if args.no_current:
        print(f"Snapshot {sha} ready at {dest}; CURRENT.txt left as it is")
        return
    with open(current_file, "w", encoding="utf-8") as f:
        f.write(f"{sha}\n{subject}\n")
    print(f"Current snapshot: {sha}")


if __name__ == "__main__":
    main()
