#!/usr/bin/env python3
"""Stand-in until the decomp has its reverb C in XD's own MusyX copy.

Pokemon XD's reverb file is the decomp's EDITED copy src/musyx/runtime/StdReverb/reverb.c (configure.py: lib musyx_xd,
src_dir "src"; it differs from the MusyX sub-project's file by the powf wrapper at the top). The decomp wrote the C
versions of DoCrossTalk / HandleReverb (2026-11-25, musyx 9308a0d) into the sub-project's file only, so the file XD
really builds still has the two "TODO: Reimplement in C" stubs and the port played no reverb at all.

This script writes build/<..>/reverb.c = XD's copy with the two stubs replaced by the sub-project's C blocks.
It does nothing (exit 3: "use the snapshot's file as it is") once XD's copy has the C itself (looks for rv_madd).
Exit 0 = written, 3 = not needed, 1 = the files are not what this script expects (the configure stops).

  python tools/musyx_reverb_stand_in.py <XD copy reverb.c> <sub-project reverb.c> <out reverb.c>
"""
import sys

STUB_CROSS = "static void DoCrossTalk(s32* a, s32* b, f32 start, f32 end) {\n  // TODO: Reimplement in C\n}\n"
STUB_HANDLE = "static void HandleReverb(s32* sptr, SND_AUX_REVERBHI* rv, s32 k) {\n  // TODO: Reimplement in C\n}\n"


def read(path):
    with open(path, "rb") as f:
        raw = f.read().decode("utf-8")
    crlf = "\r\n" in raw
    return raw.replace("\r\n", "\n"), crlf


def between(text, start, end):
    a = text.index(start)
    b = text.index(end, a)
    return text[a:b]


def main():
    xd_path, sub_path, out_path = sys.argv[1:4]
    xd, crlf = read(xd_path)
    sub, _ = read(sub_path)
    if "rv_madd" in xd:
        return 3
    try:
        if xd.count(STUB_CROSS) != 1 or xd.count(STUB_HANDLE) != 1:
            raise ValueError("XD's reverb.c no longer has the two TODO stubs as expected")

        cross = between(sub, "/* XD PC port (2026-11-25): C versions of DoCrossTalk", "#endif\n\n#ifdef __MWERKS__\n/* clang-format off */\nstatic asm void HandleReverb")
        handle = between(sub, "/* One delay line's sample at a byte offset", "void ReverbHICallback(")
        if not handle.endswith("#endif\n\n"):
            raise ValueError("the sub-project's HandleReverb block ends differently than expected")
        handle = handle[:-len("#endif\n\n")]
    except ValueError as e:
        print("musyx_reverb_stand_in: " + str(e), file=sys.stderr)
        return 1
    out = xd.replace(STUB_CROSS, cross.rstrip("\n") + "\n")
    out = out.replace(STUB_HANDLE, handle.rstrip("\n") + "\n")
    if len(sys.argv) > 4 and sys.argv[4] == "dolphin-madd":


        a = out.index("static f32 rv_madd(f32 a, f32 c, f32 b) {")
        b = out.index("static f32 rv_nmsub")
        out = (out[:a] + "static f32 rv_madd(f32 a, f32 c, f32 b) { return (f32)fma((f64)a, (f64)c, (f64)b); }\n\n" +
               out[b:])
    if crlf:
        out = out.replace("\n", "\r\n")
    with open(out_path, "wb") as f:
        f.write(out.encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
