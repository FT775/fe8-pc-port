#!/usr/bin/env python3
"""Save states (v1.24.0; port/include/port_state.h): check the link map of a played build.

A save state restores every byte of the host .data and .bss between the marker objects
port/statemark/begin.o and end.o (first and last on the link line). This checks, in the GNU ld map
(ELF, or with --pe the MinGW one), that
  - every .data / .bss input section of the build's own objects (BUILD/...o, not an archive member)
    lies inside the bracket: nothing of the game or the port is silently left out of a state;
  - no other input section (the C runtime, crtbegin, libgcc, mingw-w64, glibc's copy relocations in
    .dynbss, the SDL delay-load library) lies inside it: a state never writes the runtime's data;
  - no input section straddles a bracket's edge.
The brackets: [gPortStateDataBegin, gPortStateDataEnd + 4) and [gPortStateBssBegin, gPortStateBssEnd + 4),
and on Windows [gPortStateData2Begin, gPortStateData2End + 4) for the sorted .data$* sections. A map
without the markers (a test build: the marker objects are empty there) is not checked.
Exit 0 when it holds, 1 (each problem listed) when not.
"""
import argparse
import re
import sys

MARKERS = ('gPortStateDataBegin', 'gPortStateDataEnd', 'gPortStateBssBegin', 'gPortStateBssEnd',
           'gPortStateData2Begin', 'gPortStateData2End')


def parse(path, pe):
    lines = open(path, errors='replace').read().split('\n')
    out = None
    secs = []
    syms = {}
    for i, line in enumerate(lines):
        m = re.match(r'^(\.\S+|COMMON)(\s|$)', line)
        if m:
            out = m.group(1)
        m = re.match(r'^\s+0x([0-9a-fA-F]+)\s+(_?)(gPortState\w+)\s*$', line)
        if m and m.group(3) in MARKERS:
            syms[m.group(3)] = int(m.group(1), 16)
        if out not in ('.data', '.bss'):
            continue
        m = re.match(r'^ (\.data\S*|\.bss\S*|\.dynbss|COMMON)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$', line)
        if m:
            secs.append((int(m.group(2), 16), int(m.group(3), 16), m.group(1), m.group(4).strip(), out))
            continue
        m = re.match(r'^ (\.data\S*|\.bss\S*|\.dynbss|COMMON)\s*$', line)
        if m and i + 1 < len(lines):
            n = re.match(r'^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$', lines[i + 1])
            if n:
                secs.append((int(n.group(1), 16), int(n.group(2), 16), m.group(1), n.group(3).strip(), out))
    return secs, syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', required=True)
    ap.add_argument('--build', required=True, help='the build directory (its objects are the own ones)')
    ap.add_argument('--pe', action='store_true', help='a MinGW (PE) map: also the sorted .data$* bracket')
    a = ap.parse_args()
    secs, syms = parse(a.map, a.pe)
    want = MARKERS if a.pe else MARKERS[:4]
    if not any(k in syms for k in want):
        print('check_state_ranges: %s has no save-state markers (a test build): not checked' % a.map)
        return 0
    missing = [k for k in want if k not in syms]
    if missing:
        print('check_state_ranges: %s: markers missing: %s' % (a.map, ' '.join(missing)))
        return 1
    ranges = [(syms['gPortStateDataBegin'], syms['gPortStateDataEnd'] + 4, 'data'),
              (syms['gPortStateBssBegin'], syms['gPortStateBssEnd'] + 4, 'bss')]
    if a.pe:
        ranges.append((syms['gPortStateData2Begin'], syms['gPortStateData2End'] + 4, 'data$'))
    prefix = a.build.rstrip('/') + '/'
    problems = []
    own_bytes = 0
    for addr, size, kind, obj, out in secs:
        if size == 0:
            continue
        own = obj.startswith(prefix) and '.a(' not in obj
        inside = [r for r in ranges if r[0] <= addr and addr + size <= r[1]]
        touch = [r for r in ranges if addr < r[1] and r[0] < addr + size]
        if touch and not inside:
            problems.append('straddles the %s bracket: %s 0x%08X +0x%X %s' % (touch[0][2], kind, addr, size, obj))
        elif own and not inside:
            problems.append('own data outside every bracket (never saved): %s 0x%08X +0x%X %s' % (kind, addr, size, obj))
        elif not own and inside:
            problems.append('foreign data inside the %s bracket (would be restored): %s 0x%08X +0x%X %s'
                            % (inside[0][2], kind, addr, size, obj))
        elif own:
            own_bytes += size
    for p in problems:
        print('check_state_ranges: ' + p)
    if problems:
        print('check_state_ranges: %s: %d problem(s)' % (a.map, len(problems)))
        return 1
    print('check_state_ranges: %s: %s; %d bytes of own .data/.bss, all inside, nothing foreign'
          % (a.map, ', '.join('%s 0x%08X-0x%08X' % (r[2], r[0], r[1]) for r in ranges), own_bytes))
    return 0


if __name__ == '__main__':
    sys.exit(main())
