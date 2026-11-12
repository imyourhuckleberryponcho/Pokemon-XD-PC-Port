#!/usr/bin/env python3
"""The port's shipped pipeline list: which shaders (graphics pipelines) the game uses.

Every draw needs a pipeline: a shader plus drawing settings (TEV stages, blending, depth test, vertex layout...).
Aurora records each one it meets in <cache folder>\\pipeline_cache.db (one row per pipeline: the settings struct as
bytes; nothing else). The port ships the ones the game uses as games/xd/shaders/pipelines.txt, built into
initial_pipeline_cache.db next to xd.exe, which Aurora merges into the player's cache at start. The launcher then
compiles them all once (xd.exe XD_PREPARE_SHADERS), so first visits don't wait for shaders.

The list holds drawing settings only: no pictures, sounds, text or other game data (the project's "no game data" rule).
`check` makes sure every row is only a known pipeline type with the right size.

    python tools/pipeline_list.py gather <cache folder | pipeline_cache.db>... [--list L]   # add what runs recorded
    python tools/pipeline_list.py gather --find <folder>...   # + every pipeline_cache.db under those folders (earlier
                                                              # runs' caches, %APPDATA%/Pokemon XD); read only
    python tools/pipeline_list.py build [--list L] --out <initial_pipeline_cache.db>       # (the build does this)
    python tools/pipeline_list.py check [--list L]                                          # format + versions
    python tools/pipeline_list.py stats [--list L | --db <pipeline_cache.db>]

Gathering: run the game with XD_CACHE_DIR=<one folder> (check_run recordings, warp/battle/menu sweeps, savepoints
check: anything that shows many places) and `gather` that folder. Rows of an older Aurora pipeline version are
dropped (Aurora would ignore them too); after an Aurora update that changes a version, gather again.
"""
from __future__ import annotations

import argparse
import base64
import re
import shutil
import sqlite3
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LIST = ROOT / "games" / "xd" / "shaders" / "pipelines.txt"
AURORA = ROOT / "third_party" / "aurora"
SCHEMA = 1  # Aurora's PipelineCacheSchema (lib/gfx/pipeline_cache.cpp)
TYPES = {0: "clear", 1: "gx", 2: "rml"}
VERSION_SOURCES = {0: ("lib/gfx/clear.hpp", "ClearPipelineConfigVersion"),
                   1: ("lib/gx/pipeline.hpp", "GXPipelineConfigVersion"),
                   2: ("lib/rmlui/pipeline.hpp", "RmlPipelineConfigVersion")}

HEADER = """# Pokemon XD PC port: the pipelines (shaders + drawing settings) the game uses. Drawing settings only: no
# pictures, sounds, text or other game data. Built into initial_pipeline_cache.db next to xd.exe (Aurora's
# pipeline_cache.cpp merges it at start; the launcher prepares them once per PC). Made by tools/pipeline_list.py.
# One pipeline per line: type (0 clear, 1 gx), config_version, hash, first_frame, config (Aurora's struct: "z:" +
# base64 of its zlib-compressed bytes, or plain hex)
"""


def aurora_versions() -> dict[int, int]:
    out = {}
    for t, (rel, name) in VERSION_SOURCES.items():
        p = AURORA / rel
        if p.exists():
            m = re.search(name + r"\s*=\s*(\d+)", p.read_text(encoding="utf-8", errors="replace"))
            if m:
                out[t] = int(m.group(1))
    return out


Row = tuple  # (type, version, hash, first_frame, config bytes)


def read_list(path: Path) -> dict[tuple[int, int], Row]:
    rows: dict[tuple[int, int], Row] = {}
    if not path.exists():
        return rows
    for n, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 5:
            raise SystemExit(f"{path}:{n}: expected 5 fields")
        t, ver, h, ff = int(parts[0]), int(parts[1]), int(parts[2], 16), int(parts[3])
        cfg = zlib.decompress(base64.b64decode(parts[4][2:])) if parts[4].startswith("z:") else bytes.fromhex(parts[4])
        if h >= 1 << 63:
            h -= 1 << 64
        rows[(t, h)] = (t, ver, h, ff, cfg)
    return rows


def write_list(path: Path, rows: dict[tuple[int, int], Row]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [HEADER.rstrip("\n")]
    for t, ver, h, ff, cfg in sorted(rows.values(), key=lambda r: (r[0], r[3], r[2])):
        packed = base64.b64encode(zlib.compress(cfg, 9)).decode("ascii")
        lines.append(f"{t} {ver} {h & 0xFFFFFFFFFFFFFFFF:016x} {ff} z:{packed}")
    path.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")


def read_db(path: Path) -> list[Row]:
    """The rows of an Aurora pipeline cache, READ ONLY: the database and its -wal/-shm files are copied to a temporary
    folder first and the copy is read (a cache in use, e.g. a running game's, keeps recent rows in its -wal file,
    which a plain read-only open would miss or would have to touch). The original files are never opened for writing."""
    with tempfile.TemporaryDirectory(prefix="pipeline_list_") as tmp:
        copy = Path(tmp) / "pipeline_cache.db"
        for suffix in ("", "-wal", "-shm"):
            src = Path(str(path) + suffix)
            if src.exists():
                shutil.copyfile(src, str(copy) + suffix)
        con = sqlite3.connect(copy)
        try:
            cur = con.execute("SELECT type, config_version, hash, first_frame_used, config FROM pipeline_cache")
            return [(int(t), int(v), int(h), int(ff), bytes(c)) for t, v, h, ff, c in cur]
        except sqlite3.DatabaseError:
            return []
        finally:
            con.close()


def find_caches(roots: list[str]) -> list[Path]:
    """Every pipeline_cache.db under these folders (earlier runs' XD_CACHE_DIR folders, sweeps, worktrees' builds,
    %APPDATA%/Pokemon XD), biggest first; empty ones (4 KB: no rows) left out."""
    found: list[Path] = []
    for root in roots:
        r = Path(root)
        if r.is_file():
            found.append(r)
            continue
        for db in r.rglob("pipeline_cache.db"):
            try:
                if db.stat().st_size > 4096 or Path(str(db) + "-wal").exists():
                    found.append(db)
            except OSError:
                pass
    return sorted(set(found), key=lambda d: -d.stat().st_size)


def problems(rows: dict[tuple[int, int], Row], versions: dict[int, int]) -> list[str]:
    out = []
    sizes: dict[tuple[int, int], int] = {}
    for t, ver, h, ff, cfg in rows.values():
        if t not in TYPES:
            out.append(f"unknown pipeline type {t} (hash {h & (2**64 - 1):016x})")
            continue
        if versions.get(t) is not None and ver != versions[t]:
            out.append(f"{TYPES[t]} pipeline of version {ver}, Aurora is at {versions[t]} (gather again)")
        size = sizes.setdefault((t, ver), len(cfg))
        if size != len(cfg):
            out.append(f"{TYPES[t]} pipeline of {len(cfg)} bytes, others of that version have {size}")
        if len(cfg) > 4096:
            out.append(f"{TYPES[t]} pipeline of {len(cfg)} bytes: far bigger than drawing settings")
    return out


def cmd_gather(args) -> int:
    path = Path(args.list)
    rows = read_list(path)
    versions = aurora_versions()
    before = len(rows)
    sources: list[Path] = []
    for src in args.sources:
        p = Path(src)
        db = p / "pipeline_cache.db" if p.is_dir() else p
        if not db.exists():
            print(f"  {src}: no pipeline_cache.db, skipped")
            continue
        sources.append(db)
    if args.find:
        sources += [d for d in find_caches(args.find) if d not in sources]
    outdated_all: dict[tuple[int, int], int] = {}
    for db in sources:
        new = old = 0
        for t, ver, h, ff, cfg in read_db(db):
            if t not in (0, 1):  # clear + GX; Aurora's own UI pipelines (rml) are not the game's
                continue
            if versions.get(t) is not None and ver != versions[t]:
                old += 1
                outdated_all[(t, ver)] = outdated_all.get((t, ver), 0) + 1
                continue
            have = rows.get((t, h))
            if have is None:
                rows[(t, h)] = (t, ver, h, ff, cfg)
                new += 1
            elif ff < have[3]:
                rows[(t, h)] = (t, ver, h, ff, cfg)
        print(f"  {db}: {new} new" + (f", {old} of an older Aurora version skipped" if old else ""))
    if outdated_all:
        print("  outdated (another Aurora pipeline version, dropped; the same pipeline may be counted in several caches): "
              + ", ".join(f"{TYPES.get(t, t)} v{v}: {n}" for (t, v), n in sorted(outdated_all.items())))
    # Rows of an older version in the list itself go too.
    stale = [k for k, r in rows.items() if versions.get(r[0]) is not None and r[1] != versions[r[0]]]
    for k in stale:
        del rows[k]
    bad = problems(rows, versions)
    if bad:
        print("problems:\n  " + "\n  ".join(bad[:20]))
        return 1
    write_list(path, rows)
    print(f"{path}: {len(rows)} pipelines ({len(rows) - before + len(stale):+d} new, {len(stale)} old dropped)")
    return 0


def cmd_build(args) -> int:
    rows = read_list(Path(args.list))
    versions = aurora_versions()
    bad = problems(rows, versions)
    for b in bad[:10]:
        print(f"pipeline_list: warning: {b}", file=sys.stderr)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(".tmp")
    if tmp.exists():
        tmp.unlink()
    con = sqlite3.connect(tmp)
    con.executescript(f"""
CREATE TABLE aurora_schema(value INTEGER);
INSERT INTO aurora_schema VALUES ({SCHEMA});
CREATE TABLE pipeline_cache (
  type INTEGER NOT NULL, hash INTEGER NOT NULL, config_version INTEGER NOT NULL, config_size INTEGER NOT NULL,
  config BLOB NOT NULL, first_frame_used INTEGER NOT NULL, PRIMARY KEY (type, hash));
""")
    con.executemany("INSERT INTO pipeline_cache VALUES (?, ?, ?, ?, ?, ?)",
                    [(t, h, ver, len(cfg), cfg, ff) for t, ver, h, ff, cfg in rows.values()])
    con.commit()
    con.close()
    tmp.replace(out)
    print(f"pipeline_list: {out.name}: {len(rows)} pipelines")
    return 0


def cmd_check(args) -> int:
    rows = read_list(Path(args.list))
    bad = problems(rows, aurora_versions())
    for b in bad:
        print(b)
    print(f"{args.list}: {len(rows)} pipelines, {len(bad)} problems")
    return 1 if bad else 0


def cmd_stats(args) -> int:
    rows = list(read_db(Path(args.db))) if args.db else list(read_list(Path(args.list)).values())
    by: dict[tuple[int, int], int] = {}
    for t, ver, *_ in rows:
        by[(t, ver)] = by.get((t, ver), 0) + 1
    for (t, ver), n in sorted(by.items()):
        print(f"{TYPES.get(t, t)} v{ver}: {n}")
    print(f"total {len(rows)}; Aurora versions here: {aurora_versions()}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gather")
    g.add_argument("sources", nargs="*")
    g.add_argument("--find", nargs="+", metavar="FOLDER",
                   help="also every pipeline_cache.db under these folders (read only: copies are read)")
    g.add_argument("--list", default=str(DEFAULT_LIST))
    b = sub.add_parser("build")
    b.add_argument("--list", default=str(DEFAULT_LIST))
    b.add_argument("--out", required=True)
    c = sub.add_parser("check")
    c.add_argument("--list", default=str(DEFAULT_LIST))
    s = sub.add_parser("stats")
    s.add_argument("--list", default=str(DEFAULT_LIST))
    s.add_argument("--db")
    args = ap.parse_args()
    return {"gather": cmd_gather, "build": cmd_build, "check": cmd_check, "stats": cmd_stats}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
