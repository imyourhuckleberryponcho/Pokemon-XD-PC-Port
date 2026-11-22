#!/usr/bin/env python3
"""Reads a linked Mach-O program's layout (llvm-objdump --macho --private-headers + llvm-nm -n output) and compares
it with what the port needs (docs/MAC.md; games/xd/CMakeLists.txt; mac_low_reserve.c).
Usage: layout_read.py <program> <headers.txt> <nm.txt>      (called by tools/mac/layout_check.sh)"""
import re
import struct
import sys

prog, headers, nmfile = sys.argv[1:4]
t = open(headers).read()
WANT = {'__PAGEZERO': (0, 0x1000), '__TEXT': (0x40000000, None), '__XDLOW': (0x70000000, 0x10000000),
        '__XDMEM1': (0x80000000, 0x01800000), '__XDMEMU': (0xC0000000, 0x01800000), '__XDLC': (0xE0000000, 0x4000),
        '__XDSTK': (0x50000000, 0x20000000)}
ok = True
m = re.search(r'^\s*MH_MAGIC_64.*$', t, re.M)
print('header:', m.group(0).strip() if m else '?')
pie = bool(re.search(r'\bPIE\b', t))
cmds = re.split(r'^Load command \d+\s*$', t, flags=re.M)[1:]
sections = []
for c in cmds:
    f = dict(re.findall(r'^\s*(\w+) (.+?)\s*$', c, re.M))
    if f.get('cmd') == 'LC_SEGMENT_64':
        seg = re.search(r'segname (\S+)\s+vmaddr (\S+)\s+vmsize (\S+)\s+fileoff (\S+)\s+filesize (\S+)\s+maxprot (\S+)\s+initprot (\S+)', c)
        n, a, s, fo, fs, mp, ip = seg.groups()
        a = int(a, 16); s = int(s, 16)
        w = WANT.pop(n, None)
        v = ''
        if w:
            good = a == w[0] and (w[1] is None or s == w[1])
            v = 'AS NEEDED' if good else 'WRONG (needed 0x%x, size %s)' % (w[0], hex(w[1]) if w[1] else 'any')
            ok &= good
        print('%-11s vmaddr 0x%09x vmsize 0x%09x end 0x%09x fileoff %-8s filesize %-8s maxprot %s initprot %s  %s'
              % (n, a, s, a + s, fo, fs, mp, ip, v))
        for sm in re.finditer(r'sectname (\S+)\s+segname (\S+)\s+addr (\S+)\s+size (\S+)\s+offset (\S+)', c):
            sections.append((sm.group(2), sm.group(1), int(sm.group(3), 16), int(sm.group(4), 16), int(sm.group(5))))
            print('      section %-10s addr 0x%09x size 0x%09x offset %s' % (sm.group(1), sections[-1][2], sections[-1][3], sm.group(5)))
if WANT:
    ok = False
    print('MISSING segments:', sorted(WANT))
kinds = re.findall(r'^\s*cmd (LC_\w+)', t, re.M)
print('load commands:', ' '.join(k for k in kinds if k != 'LC_SEGMENT_64'))
huge = any(s[1] == '__huge' for s in sections)
chained = 'LC_DYLD_CHAINED_FIXUPS' in kinds
print('PIE flag: %s   __huge section: %s   chained fixups: %s   dyld info: %s'
      % (pie, huge, chained, 'LC_DYLD_INFO_ONLY' in kinds or 'LC_DYLD_INFO' in kinds))
nm = {}
for line in open(nmfile):
    p = line.split()
    if len(p) == 3:
        nm[p[2]] = int(p[0], 16)
for name in ('__mh_execute_header', '_main', '_game_fn', '_game_const', '_musyx_zero', '_musyx_set', '_big_zero',
             '_xd_reserve_low', '_xd_reserve_mem1', '_xd_reserve_memu', '_xd_reserve_lc', '__ZN5GSvecC2Ev',
             '__ZN5GSvecC1Ev', '__ZN5GSmtxC1Ev', '__ZN16GSBinWriteStreamD1Ev'):
    print('  symbol %-28s %s' % (name, ('0x%09x' % nm[name]) if name in nm else 'not in the symbol table'))

if '_section_marks' in nm:
    a = nm['_section_marks']
    for seg, sec, addr, size, off in sections:
        if addr <= a < addr + size and off:
            data = open(prog, 'rb').read()
            vals = struct.unpack_from('<6Q', data, off + (a - addr))
            names = ['start __TEXT,xdg_text', 'end __TEXT,xdg_text', 'start __DATA,xdg_const', 'end __DATA,xdg_const',
                     'start __DATA,mxstate', 'end __DATA,mxstate']
            for nme, v in zip(names, vals):
                print('  section$%-24s 0x%09x' % (nme, v))
            for i, (sg, sc) in enumerate((('__TEXT', 'xdg_text'), ('__DATA', 'xdg_const'), ('__DATA', 'mxstate'))):
                real = [s for s in sections if s[0] == sg and s[1] == sc]
                good = bool(real) and vals[2 * i] == real[0][2] and vals[2 * i + 1] == real[0][2] + real[0][3]
                ok &= good
                print('    %s,%s markers %s' % (sg, sc, 'AS NEEDED (the section\'s own start and end)' if good else 'WRONG'))
print('LAYOUT', 'AS NEEDED' if ok and not huge and not pie and not chained else 'NOT AS NEEDED')
