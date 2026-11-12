"""Turn a snapshot's configure.py into the port's build list: snapshot/<commit>/objects.json.

The decomp's configure.py says, for every source file, which compiler settings it uses. Most of
those settings only matter for byte-matching the GameCube compiler, but a few matter for the PC:
  - language: some .cpp files are compiled as C ("-lang=c")
  - extra include folders ("-i include/hsd_ref", ...)
  - preprocessor defines ("-DSDK_REVISION=...")
This script runs the snapshot's own copy of configure.py (never the live decomp), stopping just
before it would write any build files, and records those settings per file.

Usage:  python tools/gen_build_list.py snapshot/<commit>
"""
import json
import os
import sys


def parse_cflags(flags):
    lang, includes, defines = None, [], []
    it = iter(flags)
    for f in it:
        if f in ("-lang=c", "-lang c"):
            lang = "c"
        elif f in ("-lang=c++", "-lang c++", "-lang=cplus", "-lang cplus"):
            lang = "c++"
        elif f.startswith(("-i ", "-ir ", "-I ")):
            includes.append(f.split(" ", 1)[1].strip())
        elif f in ("-i", "-ir", "-I"):
            includes.append(next(it))
        elif f.startswith("-D"):
            defines.append(f[2:])
    return lang, includes, defines


def load_config(snap):
    src = open(os.path.join(snap, "configure.py"), encoding="utf-8").read()
    # Cut the script off where it would generate build.ninja / progress reports.
    cut = src.rfind('\nif args.mode == "configure":')
    if cut < 0:
        sys.exit("configure.py layout changed: could not find the 'args.mode' block")
    src = src[:cut]
    old_cwd, old_argv, old_path = os.getcwd(), sys.argv, list(sys.path)
    os.chdir(snap)
    sys.argv = ["configure.py"]
    sys.path.insert(0, snap)
    try:
        ns = {"__name__": "configure_snapshot", "__file__": os.path.join(snap, "configure.py")}
        exec(compile(src, "configure.py", "exec"), ns)
    finally:
        os.chdir(old_cwd)
        sys.argv, sys.path[:] = old_argv, old_path
    return ns["config"]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    snap = os.path.abspath(sys.argv[1])
    config = load_config(snap)

    objects = []
    for lib in config.libs:
        lib_cflags = lib.get("cflags", [])
        for obj in lib["objects"]:
            o = obj.options
            cflags = list(o["cflags"] if o["cflags"] is not None else lib_cflags) + list(o["extra_cflags"])
            lang, includes, defines = parse_cflags(cflags)
            name = obj.name
            if lang is None:
                lang = "c" if name.endswith(".c") else "c++"
            objects.append({
                "lib": lib["lib"],
                "source": o["source"],
                "lang": lang,
                "includes": includes,
                "defines": [d for d in defines if not d.startswith(("BUILD_VERSION", "VERSION_", "DEBUG", "NDEBUG"))],
                "matching": bool(obj.completed),
            })

    out = os.path.join(snap, "objects.json")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"version": config.version, "objects": objects}, f, indent=1)
    print(f"Wrote {len(objects)} objects to {out}")


if __name__ == "__main__":
    main()
