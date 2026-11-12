"""Game data the decomp compiles in, taken from the player's main.dol instead (docs/port-readiness/COMPILED_DATA.md).

Some of the decomp's files hold nothing but the game's own tables (initialised arrays, no functions): menu layouts,
move/item/Pokemon number tables, callback tables. Compiled as they are, their values would be inside xd.exe, and a
released program must contain no game data. Every one of those tables is also in main.dol, which the port loads into
GameCube memory at its real addresses at start-up (games/xd/src/dol_tables.c). So the port compiles these files
WITHOUT their initialisers (the tables are then zero) and fills each table from main.dol's bytes before the game's
code runs, converted to the PC layout: numbers byte-swapped, 4-byte GameCube addresses turned into PC pointers
(games/xd/src/disc_tables.cpp).

This script does the build-time half, from the snapshot's sources (CMake runs it when configuring; it needs the pip
package libclang):
  - finds the data-only files (no function bodies, at least one initialised variable);
  - writes, per file, a copy with the initialisers removed (and `[]` sizes filled in), and a file that includes that
    copy and registers each table: its PC address and size, its main.dol address and size, and its layout (every
    number, pointer and raw field with its GameCube and PC offset, worked out by clang for the GameCube's PowerPC
    and for the PC);
  - registers the functions and variables the tables' initialisers name, with their main.dol addresses, so a
    GameCube address in a table can become the PC program's own function or variable.
No game data goes into the generated files: names, sizes, offsets and main.dol addresses only.

With --verify the copies keep their initialisers (the old build) and xd.exe compares, at start-up, every table it
converts from main.dol with the one the compiler made from the decomp's source (XD_DISC_TABLES_VERIFY build).

main.dol addresses come from games/xd/src/data/disc_table_addresses.tsv (names and addresses from the decomp's
symbol list, made with --addresses; main.dol is fixed by its SHA-1, so they only change when the decomp renames
something, and then configuring stops with a message saying to rerun --addresses).

Usage:
    python tools/gen_disc_tables.py --snapshot DIR --flags FILE --out DIR [--verify]   # (CMake)
    python tools/gen_disc_tables.py --snapshot DIR --flags FILE --addresses            # rewrite the .tsv (reads the
                                                                                        # decomp's symbols/splits)
    python tools/gen_disc_tables.py --snapshot DIR --flags FILE --report               # COMPILED_DATA.md tables
The flags file (JSON: defines, includes, force_includes, system_includes) is written by games/xd/CMakeLists.txt.
"""
import argparse
import json
import os
import re
import sys
from concurrent.futures import ProcessPoolExecutor
from concurrent.futures.process import BrokenProcessPool
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECOMP_CONFIG = ROOT.parent / "Pokemon-XD-Decomp" / "config" / "GXXE01"  # read only
ADDR_TSV = ROOT / "games" / "xd" / "src" / "data" / "disc_table_addresses.tsv"
MANUAL_TSV = ROOT / "games" / "xd" / "src" / "data" / "disc_table_manual.tsv"
GENERATOR_VERSION = 1

PC_TARGET = "x86_64-pc-windows-msvc"
# The GameCube layout: the PC's headers only parse for a Windows target, and 32-bit Windows lays out everything this
# script accepts as the GameCube's compiler does (4-byte pointers, every number aligned to its size, double and
# long long to 8). Unions, bit fields, wchar_t and long double, where they could differ, are refused, and every
# table's GameCube size is checked against the decomp's symbol list (--addresses) and main.dol at start-up.
GC_TARGET = "i686-pc-windows-msvc"
# Linux build (docs/LINUX.md): the flags file names the targets itself ("pc_target": "x86_64-pc-linux-gnu",
# "gc_target": "i686-pc-linux-gnu") and clang reads the game's files as the Linux build compiles them (Linux
# headers; Microsoft's bit-field layout, -mms-bitfields, as the game code is compiled with there). The GameCube
# side: 32-bit Linux aligns double and long long to 4 inside structs where the GameCube aligns them to 8, hence
# -malign-double. Without these keys (the Windows build) nothing changes.
LINUX_STUBS = ROOT / "tools" / "linux_gc_headers"  # gnu/stubs-32.h for machines without the 32-bit C library

# Files never compiled from the snapshot anyway (games/xd/CMakeLists.txt).
SKIPPED = [r"/dsp_import\.c$", r"/GSmovie/THP(Dec|Audio)\.cpp$", r"/GSmemman/memOverride\.cpp$"]

# Leaf kinds (games/xd/src/disc_tables.h).
K_NUM, K_RAW, K_FUNC, K_DATA, K_STR = 0, 1, 2, 3, 4

# Classes the decomp gives GameCube byte order on PC (include/game/gcdata.h): kept as they are.
RAW_RECORD = re.compile(r"^(gc_be|gc_ptr32|gc_u|gc_s|gc_f)")

# Tables that must stay compiled from the decomp's source although main.dol has them: (file, table) -> why.
# (none at the moment; the verify build, XD_DISC_TABLES_VERIFY, is how one would be found)
KEEP_COMPILED = {}

# Tables the decomp defines twice, `#ifdef __MWERKS__` the GameCube's and `#else` a PC copy that keeps the GameCube's
# BYTES (numbers written byte-swapped so the bytes in memory are the same): from main.dol they are copied as bytes.
# (file, table) -> why. The verify build checks each against its PC copy.
GC_BYTES_TABLES = {
    ("game/menuReliveHall.cpp", "relivehallAisyou2ColorTbl"):
        "GSgfxColAu8 colours read byte by byte: the PC copy's u32s are written byte-swapped",
}


class GenError(Exception):
    pass


# ---------------------------------------------------------------------------------------------------------- clang
LIBCLANG_HELP = ("Building the port needs Python's libclang package (clang reads the decomp's tables for the PC "
                 "build): run  python -m pip install libclang  (the same Python CMake uses), then configure again.")


def cindex():
    try:
        import clang.cindex as ci
        ci.Index.create()
    except Exception as e:  # (not installed, or its library doesn't load)
        raise GenError("%s (%s: %s)" % (LIBCLANG_HELP, type(e).__name__, e))
    return ci


def game_files(snapshot):
    objs = json.loads((snapshot / "objects.json").read_text(encoding="utf-8"))["objects"]
    out = []
    for o in objs:
        src = o["source"]
        if not src.startswith("game/"):
            continue
        if any(re.search(p, src) for p in SKIPPED):
            continue
        # (the extra include folder some files get, as games/xd/CMakeLists.txt gives it)
        extra = [str(snapshot / i) for i in o.get("includes", []) if re.match(r"^include/(hsd_ref)$", i)]
        out.append((src, "c" if o.get("lang", "c") == "c" else "c++", extra))
    return out


def clang_args(flags, lang, target, extra=()):
    if "linux" in target or "apple" in target:  # (apple: docs/MAC.md, never run)
        args = ["-target", target, "-x", "c" if lang == "c" else "c++",
                "-std=gnu11" if lang == "c" else "-std=gnu++20", "-fms-extensions", "-mms-bitfields",
                "-Wno-everything", "-ferror-limit=0"]
        if lang == "c":  # (MSVC's C has the C++ spelling built in; games/xd/CMakeLists.txt gives Clang the same)
            args += ["-Dstatic_assert=_Static_assert"]
        if target.startswith(("i686", "i386")):
            # (hsd_ref's platform.h declares uintptr_t as `unsigned long` unless this is set: right for 64-bit Linux,
            #  a clash with the C library's 32-bit one, which is read first instead)
            args += ["-malign-double", "-isystem", str(LINUX_STUBS), "-D_UINTPTR_T_DEFINED", "-include", "stdint.h"]
            # (the 64-bit C library's own folder, e.g. /usr/include/x86_64-linux-gnu: its headers serve both sizes)
            for i in flags.get("gc_system_includes", []):
                args += ["-isystem", i]
    else:
        args = ["-target", target, "-x", "c" if lang == "c" else "c++",
                "-std=c11" if lang == "c" else "-std=c++20", "-fms-extensions", "-fms-compatibility",
                "-Wno-everything", "-ferror-limit=0", "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH"]
        args += ["-fms-compatibility-version=19.44"]
    for d in flags["defines"]:
        args.append("-D" + d)
    # ("first_includes": Linux only, the folder whose sys/types.h passes on the system's own; games/xd/CMakeLists.txt)
    for i in flags.get("first_includes", []) + list(extra) + flags["includes"]:
        args += ["-I", i]
    for f in flags["force_includes"]:
        args += ["-include", f]
    # The PC's C library headers (Visual Studio's). Both targets use them: the decomp's code is compiled against
    # them on PC, and only the layouts of the game's own types matter here.
    for i in flags.get("system_includes") or ([] if ("linux" in target or "apple" in target) else os.environ.get("INCLUDE", "").split(";")):
        if i:
            args += ["-isystem", i]
    return args


def parse(path, flags, lang, target, extra=()):
    ci = cindex()
    return ci.Index.create().parse(str(path), args=clang_args(flags, lang, target, extra))


def same_file(f, path):
    return f is not None and os.path.normcase(os.path.abspath(f.name)) == os.path.normcase(os.path.abspath(str(path)))


def walk_file_scope(tu, path):
    """File-scope declarations of the main file (through extern "C" blocks and namespaces)."""
    ci = cindex()
    stack = list(tu.cursor.get_children())[::-1]
    while stack:
        c = stack.pop()
        if c.kind in (ci.CursorKind.LINKAGE_SPEC, ci.CursorKind.NAMESPACE, ci.CursorKind.UNEXPOSED_DECL):
            stack.extend(list(c.get_children())[::-1])
            continue
        if same_file(c.location.file, path):
            yield c


def init_token(var):
    """The `=` (or C++ `{`) token that starts the variable's initialiser, or None."""
    depth = 0
    seen_name = False
    for t in var.get_tokens():
        s = t.spelling
        if not seen_name:
            if s == var.spelling:
                seen_name = True
            continue
        if s in ("(", "["):
            depth += 1
        elif s in (")", "]"):
            depth -= 1
        elif depth == 0 and s in ("=", "{"):
            return t
        elif depth == 0 and s in (",", ";"):
            return None
    return None


def file_scope_vars(tu, path):
    """(functions defined, [initialised variable cursors]) of the main file."""
    ci = cindex()
    funcs = 0
    out = []
    fn_kinds = (ci.CursorKind.FUNCTION_DECL, ci.CursorKind.CXX_METHOD, ci.CursorKind.CONSTRUCTOR,
                ci.CursorKind.DESTRUCTOR, ci.CursorKind.FUNCTION_TEMPLATE, ci.CursorKind.CONVERSION_FUNCTION)
    for c in walk_file_scope(tu, path):
        if c.kind in fn_kinds:
            if c.is_definition():
                funcs += 1
        elif c.kind == ci.CursorKind.VAR_DECL:
            if c.is_definition() and init_token(c) is not None:
                out.append(c)
        elif c.kind in (ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL, ci.CursorKind.UNION_DECL):
            for m in c.walk_preorder():
                if m.kind in fn_kinds and m.is_definition():
                    funcs += 1
    return funcs, out


def errors_of(tu):
    ci = cindex()
    return [str(d) for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]


# --------------------------------------------------------------------------------------------------------- survey
def survey_one(args):
    src, lang, extra, path, flags = args
    tu = parse(path, flags, lang, flags.get("pc_target", PC_TARGET), extra)
    funcs, vars_ = file_scope_vars(tu, path)
    return {"src": src, "lang": lang, "funcs": funcs, "errors": errors_of(tu)[:5],
            "vars": [{"name": v.spelling, "size": v.type.get_size(), "type": v.type.spelling,
                      "line": v.location.line} for v in vars_]}


def survey(snapshot, flags, jobs):
    work = [(s, l, e, snapshot / "src" / s, flags) for s, l, e in game_files(snapshot)]
    with ProcessPoolExecutor(jobs) as ex:
        return list(ex.map(survey_one, work))


# --------------------------------------------------------------------------------------------------------- layout
def field_list(record_type):
    """The record's fields (through the type, so template instances like PocketVec<short> have theirs too)."""
    return list(record_type.get_fields())


def pointee_kind(ptr_type):
    ci = cindex()
    p = ptr_type.get_pointee().get_canonical()
    if p.kind in (ci.TypeKind.FUNCTIONPROTO, ci.TypeKind.FUNCTIONNOPROTO):
        return K_FUNC
    if p.kind in (ci.TypeKind.CHAR_S, ci.TypeKind.CHAR_U, ci.TypeKind.SCHAR, ci.TypeKind.UCHAR):
        return K_STR
    return K_DATA


def layout(pc_t, gc_t, pc_off, gc_off, leaves, where):
    """Appends the leaves of one object of type pc_t (PC) / gc_t (GameCube) at those offsets."""
    ci = cindex()
    pc_t = pc_t.get_canonical()
    gc_t = gc_t.get_canonical()
    k = pc_t.kind
    if k == ci.TypeKind.CONSTANTARRAY:
        n = pc_t.get_array_size()
        if gc_t.kind != k or gc_t.get_array_size() != n:
            raise GenError("%s: array differs between PC and GameCube" % where)
        pe, ge = pc_t.get_array_element_type(), gc_t.get_array_element_type()
        ps, gs = pe.get_size(), ge.get_size()
        for i in range(n):
            layout(pe, ge, pc_off + i * ps, gc_off + i * gs, leaves, "%s[%d]" % (where, i))
        return
    if k == ci.TypeKind.RECORD:
        decl = pc_t.get_declaration()
        name = decl.spelling or ""
        if RAW_RECORD.match(name):
            if pc_t.get_size() != gc_t.get_size():
                raise GenError("%s: %s has different sizes" % (where, name))
            leaves.append([K_RAW, pc_t.get_size(), gc_off, pc_off, 1])
            return
        if decl.kind == ci.CursorKind.UNION_DECL:
            # An initialiser fills a union through its first member (C and C++), so main.dol's bytes are that
            # member's; the rest of a larger union is zero, as the compiler makes it.
            pf, gf = field_list(pc_t), field_list(gc_t)
            if not pf or not gf or pf[0].spelling != gf[0].spelling:
                raise GenError("%s: union without a first member (%s)" % (where, pc_t.spelling))
            layout(pf[0].type, gf[0].type, pc_off, gc_off, leaves, "%s.%s" % (where, pf[0].spelling))
            return
        pf, gf = field_list(pc_t), field_list(gc_t)
        if not pf and pc_t.get_size() > 0:
            raise GenError("%s: no fields found in %s" % (where, pc_t.spelling))
        if any(c.kind == ci.CursorKind.CXX_BASE_SPECIFIER for c in decl.get_children()):
            raise GenError("%s: %s has a base class" % (where, pc_t.spelling))
        if [f.spelling for f in pf] != [f.spelling for f in gf]:
            raise GenError("%s: fields differ between PC and GameCube (%s)" % (where, pc_t.spelling))
        units = {}  # bit-field storage units: offset -> [size, bits used]
        for p, g in zip(pf, gf):
            if p.is_bitfield():
                # The decomp declares game-data bit fields with GC_BITSn (include/game/bitfield.h): on PC each
                # name keeps the same bits of its storage unit's VALUE, so the unit is converted as one number.
                usize = p.type.get_canonical().get_size()
                po, go = p.get_field_offsetof(), g.get_field_offsetof()
                pu, gu = (po // (8 * usize)) * usize, (go // (8 * usize)) * usize
                u = units.setdefault((pu, gu), [usize, 0])
                if u[0] != usize:
                    raise GenError("%s.%s: bit fields of different sizes in one unit" % (where, p.spelling))
                u[1] += p.get_bitfield_width()
                continue
            po, go = p.get_field_offsetof(), g.get_field_offsetof()
            if po < 0 or go < 0 or po % 8 or go % 8:
                raise GenError("%s.%s: no byte offset" % (where, p.spelling))
            layout(p.type, g.type, pc_off + po // 8, gc_off + go // 8, leaves, "%s.%s" % (where, p.spelling))
        for (pu, gu), (usize, bits) in units.items():
            if bits != usize * 8:
                raise GenError("%s: a bit-field unit at 0x%X isn't filled exactly (%d of %d bits: GC_BITSn needs "
                               "every bit named)" % (where, gu, bits, usize * 8))
            leaves.append([K_NUM, usize, gc_off + gu, pc_off + pu, 1])
        leaves.sort(key=lambda l: l[2])
        return
    if k == ci.TypeKind.POINTER:
        if pc_t.get_size() != 8 or gc_t.get_size() != 4:
            raise GenError("%s: unexpected pointer sizes" % where)
        leaves.append([pointee_kind(pc_t), 4, gc_off, pc_off, 1])
        return
    if k in (ci.TypeKind.MEMBERPOINTER, ci.TypeKind.LVALUEREFERENCE, ci.TypeKind.RVALUEREFERENCE,
             ci.TypeKind.INCOMPLETEARRAY, ci.TypeKind.VARIABLEARRAY, ci.TypeKind.LONGDOUBLE, ci.TypeKind.WCHAR):
        raise GenError("%s: type %s isn't handled" % (where, pc_t.spelling))
    size = pc_t.get_size()
    if size == 8 and gc_t.get_size() == 4 and k in (ci.TypeKind.ULONGLONG, ci.TypeKind.LONGLONG,
                                                    ci.TypeKind.ULONG, ci.TypeKind.LONG):
        # A pointer-sized number (the decomp's GCuptr: u32 on the GameCube, uintptr_t on PC): an address or a number,
        # converted as a pointer field is (a GameCube address -> the PC object, a small number stays a number).
        # (ULONG/LONG: uintptr_t on 64-bit Linux is `unsigned long`; on Windows `long` is 4 bytes and never gets here)
        leaves.append([K_DATA, 4, gc_off, pc_off, 1])
        return
    if size != gc_t.get_size() or size not in (1, 2, 4, 8):
        raise GenError("%s: %s is %d bytes on PC, %d on GameCube" % (where, pc_t.spelling, size, gc_t.get_size()))
    leaves.append([K_NUM, size, gc_off, pc_off, 1])


def compress(leaves):
    """Merges runs of equal leaves that follow each other on both sides into one leaf with a count."""
    out = []
    for kind, size, go, po, n in leaves:
        if out:
            k2, s2, g2, p2, n2 = out[-1]
            pc_step = 8 if kind in (K_FUNC, K_DATA, K_STR) else size
            gc_step = 4 if kind in (K_FUNC, K_DATA, K_STR) else size
            if k2 == kind and s2 == size and go == g2 + n2 * gc_step and po == p2 + n2 * pc_step and kind != K_RAW:
                out[-1][4] += n
                continue
        out.append([kind, size, go, po, n])
    # Single bytes need no conversion: a whole run of them, in the same place on both sides, is one raw copy.
    return out


def var_layout(pc_var, gc_var):
    """(count, pcStride, gcStride, leaves of one element) for a table (an array is its elements)."""
    ci = cindex()
    pc_t, gc_t = pc_var.type.get_canonical(), gc_var.type.get_canonical()
    if pc_t.kind == ci.TypeKind.CONSTANTARRAY:
        n = pc_t.get_array_size()
        pe, ge = pc_t.get_array_element_type(), gc_t.get_array_element_type()
        leaves = []
        layout(pe, ge, 0, 0, leaves, pc_var.spelling + "[]")
        return n, pe.get_size(), ge.get_size(), compress(leaves)
    leaves = []
    layout(pc_t, gc_t, 0, 0, leaves, pc_var.spelling)
    return 1, pc_t.get_size(), gc_t.get_size(), compress(leaves)


# ------------------------------------------------------------------------------------------------------- references
def references(var):
    """Functions and variables the initialiser of `var` names: [(name, cursor of the declaration)]."""
    ci = cindex()
    seen = {}
    for c in var.walk_preorder():
        if c.kind == ci.CursorKind.DECL_REF_EXPR:
            d = c.referenced
            if d is None:
                continue
            if d.kind == ci.CursorKind.FUNCTION_DECL or d.kind == ci.CursorKind.VAR_DECL:
                seen.setdefault(d.spelling, d)
    return seen


def is_c_linkage(decl):
    ci = cindex()
    try:
        return decl.linkage in (ci.LinkageKind.EXTERNAL, ci.LinkageKind.UNIQUE_EXTERNAL) and \
            (decl.mangled_name == decl.spelling or decl.mangled_name == "_" + decl.spelling)
    except Exception:
        return False


# ------------------------------------------------------------------------------------------------------- one file
# clang errors that don't change the layout of any table (the PC's compiler is the judge of the code; the layouts
# are checked against MSVC's sizes, the decomp's symbol list and, in the verify build, the compiled tables).
BENIGN_ERRORS = ("static assertion expression is not an integral constant expression",
                 "cannot combine with previous 'extern' declaration specifier", "conflicting types for",
                 "has a different language linkage", "must return 'int'",
                 "type name does not allow storage class to be specified")


def is_table_type(t):
    ci = cindex()
    return t.get_canonical().kind in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.RECORD)


def static_locals(tu, path):
    """Initialised `static` tables inside functions of the main file: [(function cursor, variable cursor)]."""
    ci = cindex()
    out = []
    for c in tu.cursor.walk_preorder():
        if c.kind != ci.CursorKind.VAR_DECL or not same_file(c.location.file, path):
            continue
        f = c.semantic_parent
        if f is None or f.kind not in (ci.CursorKind.FUNCTION_DECL, ci.CursorKind.CXX_METHOD):
            continue
        if c.storage_class == ci.StorageClass.STATIC and init_token(c) is not None and is_table_type(c.type):
            out.append((f, c))
    return out


def conditional_lines(data):
    """Lines (1-based) inside an #if/#ifdef/#ifndef block, and lines holding a conditional directive."""
    inside, directive = set(), set()
    depth = 0
    for n, line in enumerate(data.split(b"\n"), 1):
        s = line.strip()
        if s.startswith(b"#"):
            word = s[1:].strip().split(b" ")[0].split(b"(")[0]
            if word in (b"if", b"ifdef", b"ifndef"):
                depth += 1
                directive.add(n)
                continue
            if word in (b"else", b"elif", b"endif"):
                directive.add(n)
                if word == b"endif":
                    depth -= 1
                continue
        if depth > 0:
            inside.add(n)
    return inside, directive


def has_call(var):
    """Does the initialiser run code (a function call, a constructor)? Then it isn't plain data."""
    ci = cindex()
    for c in var.walk_preorder():
        if c.kind in (ci.CursorKind.CALL_EXPR, ci.CursorKind.CXX_NEW_EXPR, ci.CursorKind.LAMBDA_EXPR):
            return True
    return False


def evaluate_int(c):
    """The integer value of a constant expression cursor (clang's evaluator), or None."""
    import ctypes
    ci = cindex()
    lib = ci.conf.lib
    if not getattr(lib, "_xddt_eval_ready", False):
        lib.clang_Cursor_Evaluate.argtypes = [ci.Cursor]
        lib.clang_Cursor_Evaluate.restype = ctypes.c_void_p
        lib.clang_EvalResult_getKind.argtypes = [ctypes.c_void_p]
        lib.clang_EvalResult_getKind.restype = ctypes.c_int
        lib.clang_EvalResult_getAsLongLong.argtypes = [ctypes.c_void_p]
        lib.clang_EvalResult_getAsLongLong.restype = ctypes.c_longlong
        lib.clang_EvalResult_dispose.argtypes = [ctypes.c_void_p]
        lib._xddt_eval_ready = True
    r = lib.clang_Cursor_Evaluate(c)
    if not r:
        return None
    try:
        return lib.clang_EvalResult_getAsLongLong(r) if lib.clang_EvalResult_getKind(r) == 1 else None
    finally:
        lib.clang_EvalResult_dispose(r)


def unwrap(c):
    ci = cindex()
    while c.kind in (ci.CursorKind.UNEXPOSED_EXPR, ci.CursorKind.PAREN_EXPR):
        kids = list(c.get_children())
        if len(kids) != 1:
            break
        c = kids[0]
    return c


def value_source(c):
    """C text for where a value-reading initialiser takes its value: `name` or `name[<n>]`; None otherwise."""
    ci = cindex()
    c = unwrap(c)
    if c.kind == ci.CursorKind.DECL_REF_EXPR and c.referenced is not None and c.referenced.kind == ci.CursorKind.VAR_DECL:
        return qualified_name(c.referenced)
    if c.kind == ci.CursorKind.ARRAY_SUBSCRIPT_EXPR:
        base, index = list(c.get_children())
        b = unwrap(base)
        n = evaluate_int(index)
        if b.kind == ci.CursorKind.DECL_REF_EXPR and b.referenced is not None and n is not None:
            q = qualified_name(b.referenced)
            return "%s[%d]" % (q, n) if q else None
    return None


def init_fixups(var):
    """[(PC offset, size, C text of the value's source)] for the initialiser's fields that read a variable."""
    ci = cindex()
    init = [c for c in var.get_children() if c.kind == ci.CursorKind.INIT_LIST_EXPR]
    if len(init) != 1:
        raise GenError("its initialiser reads a variable and isn't a plain {...} list")
    out = []

    def walk(c, t, off, where):
        t = t.get_canonical()
        if c.kind == ci.CursorKind.INIT_LIST_EXPR:
            kids = list(c.get_children())
            if t.kind == ci.TypeKind.CONSTANTARRAY:
                e = t.get_array_element_type()
                if len(kids) > t.get_array_size():
                    raise GenError("%s: more values than elements" % where)
                for i, k in enumerate(kids):
                    walk(k, e, off + i * e.get_size(), "%s[%d]" % (where, i))
                return
            if t.kind == ci.TypeKind.RECORD:
                fields = field_list(t)
                if t.get_declaration().kind == ci.CursorKind.UNION_DECL:
                    fields = fields[:1]
                if len(kids) > len(fields) or any(f.is_bitfield() for f in fields[:len(kids)]):
                    raise GenError("%s: its {...} doesn't follow the fields one by one" % where)
                for f, k in zip(fields, kids):
                    walk(k, f.type, off + f.get_field_offsetof() // 8, "%s.%s" % (where, f.spelling))
                return
            if len(kids) == 1:
                walk(kids[0], t, off, where)
                return
            raise GenError("%s: braces around a value" % where)
        if reads_variable_expr(c):
            src = value_source(c)
            if src is None:
                raise GenError("%s: its value is computed from variables in a way the port can't repeat" % where)
            out.append((off, t.get_size(), src))

    walk(init[0], var.type, 0, var.spelling)
    if not out:
        raise GenError("its initialiser reads a variable, but no field could be found for it")
    return out


def reads_variable_expr(c):
    """reads_variable for one expression."""
    return _reads(c)


def reads_variable(var):
    tok = init_token(var)
    return any(ch.extent.start.offset >= tok.extent.start.offset and _reads(ch) for ch in var.get_children())


def _reads(expr):
    """Does the expression use a variable's VALUE (`names[3]`, `count`), not just its address (`&x`, an array or a
    function by name)? CodeWarrior computes such an initialiser at start-up (__sinit), so main.dol holds 0 there
    (floorLayerConfig.cpp's g_layer_names[id], found by the verify build)."""
    ci = cindex()

    def walk(c, under_addr):
        k = c.kind
        if k == ci.CursorKind.CXX_UNARY_EXPR:  # (sizeof / alignof, C and C++)
            toks = [t.spelling for t in c.get_tokens()]
            if toks and toks[0] in ("sizeof", "alignof", "_Alignof", "__alignof"):
                return False  # (not evaluated)
        if k == ci.CursorKind.UNARY_OPERATOR:
            toks = [t.spelling for t in c.get_tokens()]
            if toks and toks[0] == "&":
                return any(walk(ch, True) for ch in c.get_children())
        if k == ci.CursorKind.ARRAY_SUBSCRIPT_EXPR and not under_addr:
            return True
        if k == ci.CursorKind.MEMBER_REF_EXPR and not under_addr:
            if c.type.get_canonical().kind != ci.TypeKind.CONSTANTARRAY:
                return True
            return any(walk(ch, True) for ch in c.get_children())  # (an array member: its address)
        if k == ci.CursorKind.DECL_REF_EXPR:
            d = c.referenced
            if d is not None and d.kind == ci.CursorKind.VAR_DECL and not under_addr:
                t = d.type.get_canonical()
                if t.kind not in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY):
                    return True
            return False
        # (inside &x[i] / &s.f the address is still a constant)
        keep = under_addr and k in (ci.CursorKind.ARRAY_SUBSCRIPT_EXPR, ci.CursorKind.MEMBER_REF_EXPR,
                                    ci.CursorKind.PAREN_EXPR, ci.CursorKind.UNEXPOSED_EXPR)
        return any(walk(ch, keep) for ch in c.get_children())

    return walk(expr, False)


def object_const_tokens(var):
    """The `const` tokens that make the object itself const (after the last `*` before the name, or all of the
    declaration's `const`s if there is no `*`), as byte ranges."""
    toks = list(var.get_tokens())
    name_at = next((i for i, t in enumerate(toks) if t.spelling == var.spelling), None)
    if name_at is None:
        return None
    last_star = max((i for i in range(name_at) if toks[i].spelling == "*"), default=-1)
    return [(t.extent.start.offset, t.extent.end.offset) for t in toks[last_star + 1:name_at] if t.spelling == "const"]


def qualified_name(c):
    """`ns::name` for a variable in a named namespace (anonymous namespaces need no name)."""
    ci = cindex()
    parts = [c.spelling]
    p = c.semantic_parent
    while p is not None and p.kind != ci.CursorKind.TRANSLATION_UNIT:
        if p.kind == ci.CursorKind.NAMESPACE:
            if p.spelling:
                parts.insert(0, p.spelling)
        elif p.kind in (ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL):
            if not p.spelling:
                return None
            # (a static member: Class::member, with a template's arguments; the compiler says if it isn't public)
            ts = p.type.spelling if p.type is not None and p.type.spelling else p.spelling
            cut = ts.find("<")
            head = ts if cut < 0 else ts[:cut]
            parts.insert(0, ts[head.rfind("::") + 2:] if "::" in head else ts)  # (namespaces: added by the loop)
        elif p.kind != ci.CursorKind.LINKAGE_SPEC:
            return None
        p = p.semantic_parent
    return "::".join(parts)


def analyse_file(args):
    """One game file: its tables (layouts and the edits that remove their initialisers) and what they point at.
    Data-only files: every initialised variable. Files with code: initialised arrays and records, at file scope or
    `static` in a function. A table that can't be handled safely keeps its initialiser ("kept", with the reason)."""
    src, lang, extra, path, flags = args
    ci = cindex()
    pc_tu = parse(path, flags, lang, flags.get("pc_target", PC_TARGET), extra)
    pc_funcs, pc_vars = file_scope_vars(pc_tu, path)
    data_only = pc_funcs == 0
    errs = errors_of(pc_tu)
    fatal = [e for e in errs if not any(b in e for b in BENIGN_ERRORS)]
    # A file clang could not read (a header not found) may show no table at all: it is listed as unread instead of
    # passing as "no tables", so main() can stop (2026-11-10; Windows and Linux have no such file).
    header_missing = [e for e in fatal if "file not found" in e]
    if data_only and not pc_vars:
        if header_missing:
            return {"src": src, "kept_file": "clang can't read it for the PC: " + header_missing[0]}
        return {"src": src, "skip": True}
    pc_locals = [] if data_only else static_locals(pc_tu, path)
    if not data_only:
        pc_vars = [v for v in pc_vars if is_table_type(v.type)]
        if not pc_vars and not pc_locals:
            if header_missing:
                return {"src": src, "kept_file": "clang can't read it for the PC: " + header_missing[0]}
            return {"src": src, "skip": True}
    if fatal or (data_only and errs):
        return {"src": src, "kept_file": "clang can't read it for the PC: " + (fatal or errs)[0]}
    gc_tu = parse(path, flags, lang, flags.get("gc_target", GC_TARGET), extra)
    gerrs = errors_of(gc_tu)
    # (the GameCube reading also trips the headers' checks of PC sizes: `static_assert(sizeof(X) == <PC size>)`,
    # or the negative-array-size form of it)
    gfatal = [e for e in gerrs if not any(b in e for b in BENIGN_ERRORS + (
        "static assertion failed", "array size is negative", "declared as an array with a negative size"))]
    if gfatal or (data_only and gerrs):
        return {"src": src, "kept_file": "clang can't read it for the GameCube: " + (gfatal or gerrs)[0]}
    _, gc_vars = file_scope_vars(gc_tu, path)
    gc_locals = [] if data_only else static_locals(gc_tu, path)
    if not data_only:
        gc_vars = [v for v in gc_vars if is_table_type(v.type)]
    if [v.spelling for v in pc_vars] != [v.spelling for v in gc_vars] or \
            [(f.spelling, v.spelling) for f, v in pc_locals] != [(f.spelling, v.spelling) for f, v in gc_locals]:
        return {"src": src, "kept_file": "the PC and GameCube readings have different variables"}

    data = path.read_bytes()
    cond_inside, cond_directive = conditional_lines(data)
    # variables declared more than once in the file (a header's `extern` declaration and the definition)
    decl_count = {}
    for c in pc_tu.cursor.walk_preorder():
        if c.kind == ci.CursorKind.VAR_DECL:
            u = c.get_usr()
            if u:
                decl_count[u] = decl_count.get(u, 0) + 1
    starts = {}
    for v in pc_vars:
        starts[v.extent.start.offset] = starts.get(v.extent.start.offset, 0) + 1

    tables, kept, refs = [], [], {}
    candidates = [(None, pv, gv) for pv, gv in zip(pc_vars, gc_vars)] + \
                 [(pf, pv, gv) for (pf, pv), (gf, gv) in zip(pc_locals, gc_locals)]
    local_no = 0
    seen_labels = {}
    for func, pv, gv in candidates:
        label = pv.spelling if func is None else "%s::%s" % (func.spelling, pv.spelling)
        seen_labels[label] = seen_labels.get(label, 0) + 1
        if seen_labels[label] > 1:  # (overloads, or two blocks of one function, with a table of the same name)
            label += "#%d" % seen_labels[label]
        try:
            tok = init_token(pv)
            ext = pv.extent
            if not same_file(tok.location.file, path) or not same_file(ext.end.file, path) or \
                    not same_file(ext.start.file, path):
                raise GenError("the definition comes from a macro")
            lines = range(ext.start.line, ext.end.line + 1)
            if (src, label) in KEEP_COMPILED:
                raise GenError(KEEP_COMPILED[(src, label)])
            gc_bytes = (src, label) in GC_BYTES_TABLES
            if any(n in cond_inside or n in cond_directive for n in lines) and not gc_bytes:
                raise GenError("defined under #if (it may be the PC's own version)")
            if has_call(pv):
                raise GenError("its initialiser runs code")
            fixups = []
            if reads_variable(pv):
                # CodeWarrior fills those fields at start-up (__sinit) from other variables, so main.dol has 0
                # there: the port does the same after filling the table (each field and where its value is read).
                fixups = init_fixups(pv)
            count, pc_stride, gc_stride, leaves = var_layout(pv, gv)
            if gc_bytes:
                if any(l[0] not in (K_NUM, K_RAW) for l in leaves):
                    raise GenError("a GameCube-bytes table with pointers")
                leaves = [[K_RAW, l[1], l[2], l[3], l[4]] for l in leaves]
            is_const = is_const_object(pv.type)
            is_static = pv.storage_class == ci.StorageClass.STATIC
            toks = list(pv.get_tokens())
            empty_bracket = None
            for i, t in enumerate(toks):
                if t.spelling == pv.spelling and i + 2 < len(toks) and toks[i + 1].spelling == "[" and \
                        toks[i + 2].spelling == "]":
                    empty_bracket = toks[i + 1].extent.end.offset
                    break
            pc_t = pv.type.get_canonical()
            outer = pc_t.get_array_size() if pc_t.kind == ci.TypeKind.CONSTANTARRAY else None
            edits = []
            reg_name = qualified_name(pv) if func is None else None
            if func is None:
                if reg_name is None:
                    raise GenError("a class member")
                # Linux (Clang): `template <> T C<X>::member;` without an initialiser only DECLARES the member
                # (Microsoft's compiler takes it as the definition), so there it keeps an empty one (zero).
                spec_member = ("linux" in flags.get("pc_target", PC_TARGET) or "apple" in flags.get("pc_target", PC_TARGET)) and lang != "c" and \
                    (re.search(rb"template\s*<\s*>\s*$", data[:ext.start.offset]) is not None or
                     re.match(rb"template\s*<\s*>", data[ext.start.offset:ext.end.offset]) is not None)
                if data_only:
                    # No code in the file can read the empty table, so it may stay const. (C++ wants `= {}` on a
                    # const object; C doesn't, and `{}` is C23.)
                    edits.append((tok.extent.start.offset, ext.end.offset,
                                  b" = {}" if (is_const or spec_member) and lang != "c" else b""))
                else:
                    if is_const:
                        # The file's own code could read a const table at compile time (all zero once empty): it
                        # becomes a plain variable, which only the start-up filling writes.
                        if decl_count.get(pv.get_usr(), 0) > 1:
                            raise GenError("const and declared elsewhere too (a header): needs GC_CONST in the decomp")
                        if starts.get(ext.start.offset, 0) > 1:
                            raise GenError("const, several variables in one declaration")
                        ranges = object_const_tokens(pv)
                        if not ranges:
                            raise GenError("const through a typedef")
                        for a, b in ranges:
                            edits.append((a, b, b""))
                        # (C++: a namespace-scope const has internal linkage unless extern / extern "C"; without
                        # its const it would become external, so it gets `static`)
                        if lang != "c" and not is_static and pv.linkage == ci.LinkageKind.INTERNAL:
                            edits.append((ext.start.offset, ext.start.offset, b"static "))  # (const meant internal)
                    # `extern "C" T x = {...};` (no braces) or `extern T x = {...};` is a definition only through
                    # its initialiser: it keeps an empty one (zero; the table isn't const, so nothing is folded).
                    lp = pv.lexical_parent
                    single_spec = lp is not None and lp.kind == ci.CursorKind.LINKAGE_SPEC and \
                        [t.spelling for t in list(lp.get_tokens())[:3]][2:3] != ["{"]
                    needs_init = single_spec or any(t.spelling == "extern" for t in toks[:next(
                        i for i, t in enumerate(toks) if t.spelling == pv.spelling)])
                    if spec_member:
                        needs_init = True
                    edits.append((tok.extent.start.offset, ext.end.offset,
                                  (b" = {}" if lang != "c" else b" = {0}") if needs_init else b""))
                if empty_bracket is not None:
                    edits.append((empty_bracket, empty_bracket, str(outer).encode()))
            else:
                # A static table inside a function moves to file scope, right before the function, under a name
                # of its own; inside the function, a macro gives the old name that variable (#undef after it).
                # (where the function's text is: at file scope or in namespaces, not inside a class body)
                ns = []
                lp = func.lexical_parent
                while lp is not None and lp.kind != ci.CursorKind.TRANSLATION_UNIT:
                    if lp.kind == ci.CursorKind.NAMESPACE:
                        if lp.spelling:
                            ns.insert(0, lp.spelling)
                    elif lp.kind != ci.CursorKind.LINKAGE_SPEC:
                        raise GenError("in a function defined inside a class")
                    lp = lp.lexical_parent
                base = _array_base(pc_t)
                bd = base.get_declaration()
                # (a type declared inside the function can't be named at file scope, unless it is written in this
                # very declaration, `static struct {...} x[] = ...`, and so moves along with it)
                # (clang then starts the variable at its name: the declaration starts at the `static` before the
                # struct)
                decl_start = ext.start.offset
                inline_type = False
                if bd is not None and same_file(bd.extent.start.file, path) and \
                        bd.extent.end.offset <= ext.start.offset and not data[bd.extent.end.offset:ext.start.offset].strip():
                    st = data.rfind(b"static", 0, bd.extent.start.offset)
                    if st >= 0 and not data[st + 6:bd.extent.start.offset].strip():
                        inline_type = True
                        decl_start = st
                if bd is not None and bd.semantic_parent is not None and bd.semantic_parent.kind in (
                        ci.CursorKind.FUNCTION_DECL, ci.CursorKind.CXX_METHOD) and not inline_type:
                    raise GenError("its type is declared inside the function")
                if not same_file(func.extent.start.file, path):
                    raise GenError("the function comes from a macro")
                # Every use of the variable in the function (clang's references to this very declaration, so a member
                # or another variable of the same name is left alone) is renamed to the moved table.
                uses = []
                for c in func.walk_preorder():
                    if c.kind == ci.CursorKind.DECL_REF_EXPR and c.referenced is not None and c.referenced == pv:
                        if not same_file(c.location.file, path):
                            raise GenError("used through a macro")
                        a = c.location.offset
                        if data[a:a + len(pv.spelling)] != pv.spelling.encode():
                            raise GenError("used through a macro")
                        uses.append(a)
                local_no += 1
                local_name = "xddt_local%d_%s" % (local_no, pv.spelling)
                reg_name = "::".join(ns + [local_name])
                name_at = next(i for i, t in enumerate(toks) if t.spelling == pv.spelling)
                init_at = next(i for i, t in enumerate(toks) if t.extent.start.offset == tok.extent.start.offset)
                const_ranges = set(object_const_tokens(pv) or [])
                if is_const and not const_ranges:
                    raise GenError("const through a typedef")
                parts = []
                for i, t in enumerate(toks[:init_at]):
                    if t.spelling == "static" or (t.extent.start.offset, t.extent.end.offset) in const_ranges:
                        continue
                    if i == name_at:
                        parts.append(local_name)
                    elif i == name_at + 1 and t.spelling == "[" and empty_bracket is not None:
                        parts.append("[%d" % outer)
                    else:
                        parts.append(t.spelling)
                hoisted = ("static " + " ".join(parts) + ";\n").encode()
                if inline_type:
                    if is_const:
                        raise GenError("const, with its type written in the declaration")
                    hoisted = b"static " + data[bd.extent.start.offset:ext.start.offset] + hoisted[len(b"static "):]
                fstart = func.extent.start.offset
                # (`extern "C" void f() {...}` without braces: before the `extern "C"`, which belongs to f)
                lp = func.lexical_parent
                if lp is not None and lp.kind == ci.CursorKind.LINKAGE_SPEC:
                    lt = [t.spelling for t in list(lp.get_tokens())[:3]]
                    if len(lt) < 3 or lt[2] != "{":
                        fstart = lp.extent.start.offset
                renames = [(a, a + len(pv.spelling), local_name.encode()) for a in sorted(set(uses))]
                gone = (decl_start, ext.end.offset, b"")  # (the `;` stays: an empty statement)
                edits += [(fstart, fstart, hoisted), gone] + renames
                # (the verify build: the same move with the initialiser kept)
                no = toks[name_at].extent.start.offset
                whole = data[decl_start:no] + local_name.encode() + data[no + len(pv.spelling):ext.end.offset]
                verify_edits = [(fstart, fstart, whole + b";\n"), gone] + renames
            tables.append({
                "name": reg_name, "sym": pv.spelling, "label": label, "local": func is not None,
                "static": is_static or func is not None, "const": is_const,
                "pc_size": pv.type.get_size(), "gc_size": gv.type.get_size(),
                "count": count, "pc_stride": pc_stride, "gc_stride": gc_stride, "leaves": leaves,
                "edits": edits, "verify_edits": verify_edits if func is not None else [], "line": pv.location.line,
                "fixups": fixups,
            })
            for name, d in references(pv).items():
                refs.setdefault(name, {"kind": "func" if d.kind == ci.CursorKind.FUNCTION_DECL else "var",
                                       "c_linkage": is_c_linkage(d),
                                       # (declared without a size, e.g. `extern u8 x[];`: pointers to its start only)
                                       "sized": d.kind != ci.CursorKind.VAR_DECL or d.type.get_size() > 0})
        except GenError as e:
            kept.append({"label": label, "reason": str(e), "pc_size": pv.type.get_size(), "line": pv.location.line,
                         "gc_size": gv.type.get_size()})
    # (a pointer to another table of this file goes through that table; one to a kept table, as to any other
    # variable, through its address in the list)
    own = {t["sym"] for t in tables if not t["local"]}
    refs = {k: v for k, v in refs.items() if k not in own}
    return {"src": src, "lang": lang, "extra": extra, "data_only": data_only, "tables": tables, "kept": kept,
            "refs": refs}


def is_const_object(t):
    """Is the object itself const (`const T x[]`, `T* const x`), not just what it points to?"""
    # (clang puts an array's const on the canonical array type: `const unsigned int[5]` is const-qualified)
    return t.is_const_qualified() or t.get_canonical().is_const_qualified()


def _array_base(t):
    ci = cindex()
    while t.kind == ci.TypeKind.CONSTANTARRAY:
        t = t.get_array_element_type().get_canonical()
    return t


def analyse_all(snapshot, flags, jobs):
    files = game_files(snapshot)
    work = [(s, l, e, snapshot / "src" / s, flags) for s, l, e in files]
    try:
        with ProcessPoolExecutor(jobs) as ex:
            res = list(ex.map(analyse_file, work))
    except BrokenProcessPool:
        # (a worker can die when the PC is short of memory, e.g. while a build runs: then one at a time)
        print("gen_disc_tables: worker processes failed; reading the files one at a time", file=sys.stderr)
        res = [analyse_file(w) for w in work]
    res = [r for r in res if not r.get("skip")]
    # A reference to another file's table is found through that table (converted too), not as a reference.
    tables = {t["sym"] for r in res if "tables" in r for t in r["tables"] if not t["static"]}
    for r in res:
        if "tables" in r:
            r["refs"] = {k: v for k, v in r["refs"].items() if k not in tables}
    return res


# ------------------------------------------------------------------------------------------------------- addresses
def read_decomp_symbols():
    """symbols.txt and splits.txt of the decomp (read only) -> (symbols [(name, section, addr, size, scope, type)],
    file ranges {src: [(start, end)]})."""
    syms = []
    for line in (DECOMP_CONFIG / "symbols.txt").read_text(encoding="utf-8").splitlines():
        m = re.match(r"(\S+) = (\.\w+):0x([0-9A-Fa-f]+); // (.*)", line)
        if not m:
            continue
        size = re.search(r"size:0x([0-9A-Fa-f]+)", m.group(4))
        scope = re.search(r"scope:(\w+)", m.group(4))
        typ = re.search(r"type:(\w+)", m.group(4))
        syms.append((m.group(1), m.group(2), int(m.group(3), 16), int(size.group(1), 16) if size else 0,
                     scope.group(1) if scope else "", typ.group(1) if typ else ""))
    ranges = {}
    cur = None
    for line in (DECOMP_CONFIG / "splits.txt").read_text(encoding="utf-8").splitlines():
        if line and not line[0].isspace() and line.endswith(":"):
            cur = line[:-1]
            ranges[cur] = []
            continue
        m = re.match(r"\s+\.\w+\s+start:0x([0-9A-Fa-f]+) end:0x([0-9A-Fa-f]+)", line)
        if m and cur:
            ranges[cur].append((int(m.group(1), 16), int(m.group(2), 16)))
    return syms, ranges


def name_matches(sym, name, local=False):
    """CodeWarrior's names: `name`, `name__<mangling>` (C++, anonymous namespaces), `name$<n>` (static locals)."""
    if local:
        return sym.startswith(name + "$")
    return sym == name or sym.startswith(name + "__")


def size_fits(gc_size, sym_size):
    # (a few symbols are a word longer than their table: the padding up to the next 8-byte-aligned one)
    return gc_size <= sym_size < gc_size + 8


def write_addresses(results):
    syms, ranges = read_decomp_symbols()
    lines = ["# Generated by tools/gen_disc_tables.py --addresses from the decomp's config/GXXE01/symbols.txt and",
             "# splits.txt: where each table the port takes from main.dol is (and the functions/variables their",
             "# initialisers name). Names and addresses only, no game data. kind<TAB>file<TAB>name<TAB>address<TAB>size",
             "# (absent: no symbol of that name in main.dol; differs: the decomp's table has another size than main.dol's;",
             "#  zero: the GameCube's object is in .bss, i.e. made by code;",
             "#  Both stay compiled from the decomp's source.)"]
    problems = []
    refs = {}
    manual = {}
    if MANUAL_TSV.exists():
        for line in MANUAL_TSV.read_text(encoding="utf-8").splitlines():
            if line and not line.startswith("#"):
                f = line.split("\t")
                manual.setdefault((f[0], f[1]), []).append((int(f[2], 16), int(f[3], 16), f[5] if len(f) > 5 else ""))
    for r in sorted(results, key=lambda r: r["src"]):
        if "tables" not in r:
            continue
        rng = ranges.get(r["src"], [])
        in_file = [s for s in syms if any(a <= s[2] < b for a, b in rng)]
        # static locals of the same name in one file: paired in the order of the source and of their addresses
        local_seen = {}
        for t in r["tables"]:
            # the decompilation's own placements first (games/xd/src/data/disc_table_manual.tsv)
            base, _, nth = t["label"].partition("#")
            placed = manual.get((r["src"], base), [])
            k = int(nth) - 1 if nth else 0
            if k < len(placed):
                addr, size, section = placed[k]
                if section in (".bss", ".sbss"):
                    lines.append("zero\t%s\t%s\t0x%08X\t0x%X" % (r["src"], t["label"], addr, size))
                elif size_fits(t["gc_size"], size):
                    lines.append("table\t%s\t%s\t0x%08X\t0x%X" % (r["src"], t["label"], addr, size))
                else:
                    lines.append("differs\t%s\t%s\t0x%08X\t0x%X" % (r["src"], t["label"], addr, size))
                if t["local"]:
                    local_seen[t["sym"]] = local_seen.get(t["sym"], 0) + 1
                continue
            if t["local"]:
                # CodeWarrior numbers a file's static locals in source order (`name$<n>`): the k-th table of that
                # name in the source is the k-th by number, if the counts and every size agree (the verify build
                # checks the values).
                def dollar(sym):
                    m = re.search(r"\$(\d+)$", sym[0])
                    return int(m.group(1)) if m else -1
                cands = sorted((s for s in in_file if name_matches(s[0], t["sym"], True)), key=dollar)
                k = local_seen.get(t["sym"], 0)
                local_seen[t["sym"]] = k + 1
                same = [u for u in r["tables"] if u["local"] and u["sym"] == t["sym"]]
                if cands and (len(cands) != len(same) or any(dollar(c) < 0 for c in cands) or
                              not all(size_fits(u["gc_size"], c[3]) for u, c in zip(same, cands))):
                    lines.append("ambiguous\t%s\t%s\t0x0\t0x0" % (r["src"], t["label"]))
                    continue
                cands = [cands[k]] if cands else []
            else:
                cands = [s for s in in_file if name_matches(s[0], t["sym"])]
                if not cands:  # (a static local on the GameCube that the decomp writes at file scope: `name$<n>`)
                    cands = [s for s in in_file if name_matches(s[0], t["sym"], True)]
            if len(cands) != 1:
                if not cands:
                    lines.append("absent\t%s\t%s\t0x0\t0x0" % (r["src"], t["label"]))
                else:
                    problems.append("%s %s: %d symbols in the file's ranges" % (r["src"], t["label"], len(cands)))
                continue
            s = cands[0]
            if not size_fits(t["gc_size"], s[3]):
                # The decomp's table isn't the game's yet (another size): it stays compiled until the decomp fixes it.
                lines.append("differs\t%s\t%s\t0x%08X\t0x%X" % (r["src"], t["label"], s[2], s[3]))
                continue
            if s[1] in (".bss", ".sbss"):
                lines.append("zero\t%s\t%s\t0x%08X\t0x%X" % (r["src"], t["label"], s[2], s[3]))
                continue
            lines.append("table\t%s\t%s\t0x%08X\t0x%X" % (r["src"], t["label"], s[2], s[3]))
        for name, info in r["refs"].items():
            refs.setdefault(name, dict(info, src=r["src"], in_file=in_file))
    for name, info in sorted(refs.items()):
        want_type = "function" if info["kind"] == "func" else "object"
        cands = [s for s in syms if s[5] == want_type and s[4] != "local" and
                 (s[0] == name if info["c_linkage"] else name_matches(s[0], name))]
        if not cands:  # (a static function or variable of the same file)
            cands = [s for s in info["in_file"] if s[5] == want_type and name_matches(s[0], name)]
        if len(cands) != 1:
            problems.append("reference %s (%s): %d symbols" % (name, info["src"], len(cands)))
            continue
        lines.append("%s\t\t%s\t0x%08X\t0x%X" % (info["kind"], name, cands[0][2], cands[0][3]))
    ADDR_TSV.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return problems


def read_addresses():
    tables, refs = {}, {}
    if not ADDR_TSV.exists():
        return tables, refs
    for line in ADDR_TSV.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        kind, src, name, addr, size = line.split("\t")
        if kind in ("table", "absent", "differs", "ambiguous", "zero"):
            tables[(src, name)] = (int(addr, 16), int(size, 16)) if kind == "table" else kind
        else:
            refs[name] = (kind, int(addr, 16), int(size, 16))
    return tables, refs


# ------------------------------------------------------------------------------------------------------- generate
def c_string(s):
    return '"' + s.replace("\\", "/").replace('"', '\\"') + '"'


def stripped_source(path, tables, verify):
    """The file's bytes with each table's initialiser removed (the edits analyse_file worked out)."""
    data = path.read_bytes()
    if verify:
        # The verify build keeps the initialisers; static tables in functions still move to file scope, so the
        # start-up check can reach them (the move alone, with the initialiser kept, changes nothing).
        edits = []
        for t in tables:
            if t["local"]:
                edits += t["verify_edits"]
    else:
        edits = [e for t in tables for e in t["edits"]]
    for start, end, repl in sorted(edits, key=lambda e: e[0], reverse=True):
        data = data[:start] + repl + data[end:]
    return data


def generate(snapshot, flags, out_dir, verify, jobs):
    results = analyse_all(snapshot, flags, jobs)
    tables_addr, refs_addr = read_addresses()
    missing = []
    out_dir.mkdir(parents=True, exist_ok=True)
    files, kept = [], []
    for idx, r in enumerate(sorted(results, key=lambda r: r["src"])):
        if "kept_file" in r:
            kept.append({"src": r["src"], "label": "(whole file)", "reason": r["kept_file"], "pc_size": 0})
            continue
        path = snapshot / "src" / r["src"]
        for k in r["kept"]:
            kept.append(dict(k, src=r["src"]))
        live = []
        for t in r["tables"]:
            key = (r["src"], t["label"])
            if isinstance(tables_addr.get(key), str):
                why = {"absent": "no symbol of that name in main.dol (decomp's symbols.txt)",
                       "ambiguous": "several static tables of that name in the file (which is which isn't known)",
                       "zero": "zero-initialised (.bss) on the GameCube: its values are made by code there, not data "
                               "on the disc",
                       "differs": "a different size than main.dol's (the decomp's table isn't the game's yet)"}
                kept.append({"src": r["src"], "label": t["label"], "reason": why[tables_addr[key]],
                             "pc_size": t["pc_size"], "gc_size": t["gc_size"], "line": t.get("line")})
                continue
            a = tables_addr.get(key)
            if a is None:
                missing.append("%s %s" % key)
            elif not size_fits(t["gc_size"], a[1]):
                missing.append("%s %s: size 0x%X in the address list, 0x%X now" % (key + (a[1], t["gc_size"])))
            t["gc_address"] = a[0] if a else 0
            live.append(t)
        r["tables"] = live
        if not live:
            continue
        for name in r["refs"]:
            if name not in refs_addr:
                missing.append("reference %s (%s)" % (name, r["src"]))
        # The copies keep the snapshot's paths (src/game/...), so the build's per-file settings, chosen by path in
        # games/xd/CMakeLists.txt, apply to them as to the original.
        stripped = out_dir / "stripped" / r["src"]
        wrapper = out_dir / "src" / r["src"]
        stripped.parent.mkdir(parents=True, exist_ok=True)
        wrapper.parent.mkdir(parents=True, exist_ok=True)
        write_if_changed(stripped, stripped_source(path, live, verify))
        write_if_changed(wrapper, wrapper_source(r, idx, stripped).encode("utf-8"))
        files.append({"src": r["src"], "wrapper": str(wrapper).replace("\\", "/"), "lang": r["lang"],
                      "includes": [str(i).replace("\\", "/") for i in r["extra"]] + [str(path.parent).replace("\\", "/")],
                      "id": idx})
    if missing:
        raise GenError("main.dol addresses missing for (run: python tools/gen_disc_tables.py --snapshot %s --flags "
                       "<build>/disc_tables/flags.json --addresses):\n  " % snapshot + "\n  ".join(missing))
    write_if_changed(out_dir / "disc_tables_list.c", list_source(files, verify).encode("utf-8"))
    live_tables = [(r, t) for r in results if "tables" in r for t in r["tables"]]
    manifest = {"version": GENERATOR_VERSION, "verify": verify, "files": files,
                # (for tools/scan_exe_tables.py: each table's place in main.dol and layout)
                "tables": [{"src": r["src"], "name": t["label"], "gc_address": t["gc_address"],
                            "gc_size": t["gc_size"], "pc_size": t["pc_size"], "count": t["count"],
                            "gc_stride": t["gc_stride"], "pc_stride": t["pc_stride"], "leaves": t["leaves"],
                            "data_only": r["data_only"]} for r, t in live_tables],
                "kept": kept}
    write_if_changed(out_dir / "manifest.json", json.dumps(manifest, indent=1).encode("utf-8"))
    # For CMake: the snapshot's files replaced by the generated ones, and those, with their settings.
    cm = ["set(XD_DISC_TABLE_SOURCES"] + ['  "%s"' % f["src"] for f in files] + [")", "set(XD_DISC_TABLE_WRAPPERS"]
    cm += ['  "%s"' % f["wrapper"] for f in files] + [")"]
    for f in files:
        cm.append('set_source_files_properties("%s" PROPERTIES LANGUAGE %s INCLUDE_DIRECTORIES "%s")' % (
            f["wrapper"], "C" if f["lang"] == "c" else "CXX",
            ";".join(flags.get("first_includes", []) + f["includes"])))  # ("first_includes": Linux only)
    cm.append("set(XD_DISC_TABLES_KEPT %d)" % len(kept))
    write_if_changed(out_dir / "files.cmake", ("\n".join(cm) + "\n").encode("utf-8"))
    return results, kept


def write_if_changed(path, data):
    if path.exists() and path.read_bytes() == data:
        return
    path.write_bytes(data)


def wrapper_source(r, idx, stripped):
    lines = ["/* Generated by tools/gen_disc_tables.py from the snapshot's %s: the file compiled without its" % r["src"],
             " * tables' initialisers (they are filled from the player's main.dol at start-up, games/xd/src/disc_tables.cpp),",
             " * and where each table is on PC and in main.dol, with its layout. No game data. */",
             "#include %s" % c_string(str(stripped)),
             '#include "disc_tables.h"', ""]
    for ti, t in enumerate(r["tables"]):
        lines.append("XDDT_STATIC_ASSERT(sizeof(%s) == %d, xddt_size_%d);" % (t["name"], t["pc_size"], ti))
    lines.append("")
    for ti, t in enumerate(r["tables"]):
        lines.append("static const XDDiscLeaf xddt_leaves_%d[] = {" % ti)
        for kind, size, go, po, n in t["leaves"]:
            lines.append("    {%d, %d, 0x%X, 0x%X, %d}," % (kind, size, go, po, n))
        lines.append("};")
    lines.append("static XDDiscTable xddt_tables[] = {")
    for ti, t in enumerate(r["tables"]):
        lines.append("    {%s, (void*)&%s, %d, 0x%08Xu, %d, %d, %d, %d, xddt_leaves_%d, %d}," % (
            c_string(t["label"]), t["name"], t["pc_size"], t["gc_address"], t["gc_size"], t["count"],
            t["gc_stride"], t["pc_stride"], ti, len(t["leaves"])))
    lines.append("};")
    tables_addr, refs_addr = read_addresses()
    refs = sorted(r["refs"].items())
    if refs:
        lines.append("static const XDDiscRef xddt_refs[] = {")
        for name, info in refs:
            kind, addr, size = refs_addr[name]
            if kind == "func":
                lines.append("    {0x%08Xu, 0, (void*)&%s, 0}," % (addr, name))
            else:
                lines.append("    {0x%08Xu, 0x%X, (void*)&%s, %s}," % (
                    addr, size, name, "(unsigned)sizeof(%s)" % name if info.get("sized", True) else "0"))
        lines.append("};")
    fix = [(ti, f) for ti, t in enumerate(r["tables"]) for f in t.get("fixups", [])]
    if fix:
        lines.append("/* Fields CodeWarrior fills at start-up (__sinit) from other variables: the same, after the filling. */")
        lines.append("static const XDDiscFixup xddt_fixups[] = {")
        for ti, (off, size, src) in fix:
            lines.append("    {%d, 0x%X, %d, (const void*)&(%s)}," % (ti, off, size, src))
        lines.append("};")
    lines.append("XDDT_EXTERN_C const XDDiscTableFile xddt_file_%d = {%s, xddt_tables, %d, %s, %d, %s, %d};" % (
        idx, c_string(r["src"]), len(r["tables"]), "xddt_refs" if refs else "0", len(refs),
        "xddt_fixups" if fix else "0", len(fix)))
    return "\n".join(lines) + "\n"


def list_source(files, verify):
    lines = ["/* Generated by tools/gen_disc_tables.py: every file whose tables come from main.dol. */",
             '#include "disc_tables.h"', ""]
    for f in files:
        lines.append("extern const XDDiscTableFile xddt_file_%d;" % f["id"])
    lines.append("const XDDiscTableFile* const kXDDiscTableFiles[] = {")
    for f in files:
        lines.append("    &xddt_file_%d," % f["id"])
    lines.append("};")
    lines.append("const int kXDDiscTableFileCount = %d;" % len(files))
    lines.append("/* 1: the tables were also compiled from the decomp's source (XD_DISC_TABLES_VERIFY), to compare. */")
    lines.append("const int kXDDiscTablesCompiledIn = %d;" % (1 if verify else 0))
    return "\n".join(lines) + "\n"


# ------------------------------------------------------------------------------------------------------- report
def report(results):
    """Markdown for COMPILED_DATA.md: per file, what comes from main.dol and what is still compiled in."""
    tables_addr, _ = read_addresses()
    K = {K_FUNC: "functions", K_DATA: "data", K_STR: "text"}
    out = ["| file | tables from main.dol | GameCube bytes | PC bytes | pointers to | biggest (entries) |",
           "|---|---|---|---|---|---|"]
    kept = []
    for r in sorted(results, key=lambda r: r["src"]):
        if "kept_file" in r:
            kept.append((r["src"], "(whole file)", r["kept_file"], 0))
            continue
        live = [t for t in r["tables"] if not isinstance(tables_addr.get((r["src"], t["label"])), str)]
        for t in r["tables"]:
            if t not in live:
                kept.append((r["src"], t["label"], {"absent": "no symbol in main.dol", "zero": "in .bss on the "
                             "GameCube (made by code there)", "ambiguous": "several static "
                             "tables of that name in the file", "differs": "size differs from "
                             "main.dol's"}[tables_addr[(r["src"], t["label"])]], t["pc_size"]))
        for k in r["kept"]:
            kept.append((r["src"], k["label"], k["reason"], k["pc_size"]))
        if not live:
            continue
        ptr = sorted({K[l[0]] for t in live for l in t["leaves"] if l[0] in K})
        big = max(live, key=lambda t: t["gc_size"])
        out.append("| %s%s | %d | %d | %d | %s | `%s` (%d) |" % (
            r["src"].replace("game/", ""), "" if r["data_only"] else " (code)", len(live),
            sum(t["gc_size"] for t in live), sum(t["pc_size"] for t in live), ", ".join(ptr) or "-",
            big["label"], big["count"]))
    allt = [t for r in results if "tables" in r for t in r["tables"]
            if not isinstance(tables_addr.get((r["src"], t["label"])), str)]
    out.append("")
    out.append("Total from main.dol: %d tables, %d GameCube bytes, %d PC bytes." % (
        len(allt), sum(t["gc_size"] for t in allt), sum(t["pc_size"] for t in allt)))
    out.append("")
    out.append("Still compiled in:")
    out.append("")
    out.append("| file | table | why | PC bytes |")
    out.append("|---|---|---|---|")
    for src, label, why, size in sorted(kept):
        out.append("| %s | `%s` | %s | %d |" % (src.replace("game/", ""), label, why, size))
    out.append("")
    out.append("Still compiled in: %d tables, %d PC bytes." % (len(kept), sum(k[3] for k in kept)))
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--snapshot", required=True)
    ap.add_argument("--flags", required=True)
    ap.add_argument("--out")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--addresses", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--survey", action="store_true")
    ap.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 4))
    args = ap.parse_args()
    snapshot = Path(args.snapshot)
    flags = json.loads(Path(args.flags).read_text(encoding="utf-8-sig"))
    try:
        cindex()
        if args.survey:
            print(json.dumps(survey(snapshot, flags, args.jobs), indent=1))
            return 0
        if args.addresses:
            results = analyse_all(snapshot, flags, args.jobs)
            problems = write_addresses(results)
            for p in problems:
                print("PROBLEM " + p)
            print("Wrote %s" % ADDR_TSV)
            return 1 if problems else 0
        if args.report:
            print(report(analyse_all(snapshot, flags, args.jobs)))
            return 0
        if not args.out:
            ap.error("--out, --addresses, --report or --survey")
        results, kept = generate(snapshot, flags, Path(args.out), args.verify, args.jobs)
        # A file clang cannot read keeps its tables compiled in, so the program would hold the game's data: that
        # stops the build instead of passing with a count nobody reads (found 2026-11-10 by running the Apple
        # targets without Apple's headers: "0 tables taken from main.dol", exit 0). Windows and Linux have no such
        # file. XD_DISC_TABLES_ALLOW_UNREAD=1 goes on anyway (to see how far a new platform's build gets; never
        # for a build that is given to anyone).
        unread = ["%s: %s" % (k["src"], k["reason"]) for k in kept if k["reason"].startswith("clang can't read it")]
        if unread:
            print("gen_disc_tables: clang could not read %d game file(s) with these targets and header folders "
                  "(flags file %s: pc_target %s, gc_target %s):" % (
                      len(unread), args.flags, flags.get("pc_target", PC_TARGET), flags.get("gc_target", GC_TARGET)),
                  file=sys.stderr)
            for u in unread[:40]:
                print("  " + u, file=sys.stderr)
            if len(unread) > 40:
                print("  ... and %d more" % (len(unread) - 40), file=sys.stderr)
            # The first of them in full: every error clang gives and the files it read on the way there (a new
            # system's first build: the one line above is rarely enough to see the cause).
            try:
                first = next(k["src"] for k in kept if k["reason"].startswith("clang can't read it"))
                for src, lang, extra in game_files(snapshot):
                    if src != first:
                        continue
                    tu = parse(snapshot / "src" / src, flags, lang, flags.get("pc_target", PC_TARGET), extra)
                    print("  In full, %s (PC reading):" % src, file=sys.stderr)
                    for d in list(tu.diagnostics)[:25]:
                        print("    " + str(d), file=sys.stderr)
                    print("  Files read, in order (depth, file, included from):", file=sys.stderr)
                    incs = list(tu.get_includes())
                    firsts = [str(Path(f)).replace("\\", "/") for f in flags.get("first_includes", [])]
                    at = next((i for i, inc in enumerate(incs)
                               if any(inc.include.name.replace("\\", "/").startswith(f) for f in firsts)), 0)
                    for inc in incs[max(0, at - 15):at + 60]:  # (around the pass-on header, where the 16 files part)
                        print("    %d %s  <- %s:%d" % (inc.depth, inc.include.name, inc.location.file.name if
                              inc.location.file else "?", inc.location.line), file=sys.stderr)
                for folder in flags.get("first_includes", []):
                    wrapper = Path(folder) / "sys" / "types.h"
                    if wrapper.is_file():
                        text = wrapper.read_text(errors="replace")
                        print("  %s holds %r" % (wrapper, text), file=sys.stderr)
                        for m in re.finditer(r'#include "([^"]+)"', text):
                            print("  %s: %s" % (m.group(1), "is there" if os.path.isfile(m.group(1)) else "NOT THERE"),
                                  file=sys.stderr)
            except Exception as e:  # (diagnosis only: never the reason a build stops)
                print("  (details not available: %s)" % e, file=sys.stderr)
            if os.environ.get("XD_DISC_TABLES_ALLOW_UNREAD") != "1":
                print("Their tables would stay compiled into the program (the game's data inside it), so this is an "
                      "error. XD_DISC_TABLES_ALLOW_UNREAD=1 goes on anyway (test builds only).", file=sys.stderr)
                return 1
        n = sum(len(r.get("tables", [])) for r in results)
        nf = sum(1 for r in results if r.get("tables"))
        print("disc tables: %d tables in %d files taken from main.dol, %d still compiled in (%s)%s" % (
            n, nf, len(kept), "docs/port-readiness/COMPILED_DATA.md", " (verify build)" if args.verify else ""))
        return 0
    except GenError as e:
        print("gen_disc_tables: " + str(e), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
