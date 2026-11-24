#!/usr/bin/env python3
"""Mouse controls (v1.29.0): port/tools/mousecheck.sh's helper.

    mousecheck.py prefix KEYS N OUT          the first N ticks of key file KEYS, written to OUT
    mousecheck.py events MOUSE MODE OUT      the events of a port/test/inputs/mouse/*.mouse file for MODE
                                             (lines "@MODE ..." only for that mode; "#..." left out) to OUT,
                                             the FE8PC_TEST_MOUSE file
    mousecheck.py check LOG MOUSE MODE       the file's "#expect REGEX" lines (for MODE) found in LOG's lines in
                                             that order, and its "#count N REGEX" lines matching exactly N lines;
                                             prints each failure, exit 0 when all hold
"""
import re
import sys


def read_keys(path):
    ticks = []
    for line in open(path):
        line = line.split('#')[0].strip()
        if not line:
            continue
        m, _, c = line.partition('*')
        try:
            mask = int(m, 16)
        except ValueError:
            continue
        ticks += [mask] * (int(c) if c.strip() else 1)
    return ticks


def mode_lines(path, mode):
    """The file's lines for mode, the @mode prefix taken off."""
    out = []
    for line in open(path):
        line = line.rstrip('\n')
        m = re.match(r'^@(\w+)\s+(.*)$', line)
        if m:
            if m.group(1) != mode:
                continue
            line = m.group(2)
        out.append(line)
    return out


def main():
    cmd = sys.argv[1]
    if cmd == 'prefix':
        ticks = read_keys(sys.argv[2])[:int(sys.argv[3])]
        with open(sys.argv[4], 'w') as f:
            f.write('# the first %d ticks of %s\n' % (len(ticks), sys.argv[2]))
            for t in ticks:
                f.write('%03X\n' % t)
        return 0
    if cmd == 'events':
        with open(sys.argv[4], 'w') as f:
            for line in mode_lines(sys.argv[2], sys.argv[3]):
                if line.strip() and not line.lstrip().startswith('#'):
                    f.write(line + '\n')
        return 0
    if cmd == 'check':
        log = [l.rstrip('\n') for l in open(sys.argv[2], errors='replace')]
        fails = 0
        pos = 0
        expects = 0
        for line in mode_lines(sys.argv[3], sys.argv[4]):
            m = re.match(r'^#expect (.*)$', line)
            if m:
                expects += 1
                rx = re.compile(m.group(1))
                while pos < len(log) and not rx.search(log[pos]):
                    pos += 1
                if pos == len(log):
                    print('    missing (in order): %s' % m.group(1))
                    fails += 1
                    pos = 0 if fails > 20 else pos
                    break
                pos += 1
                continue
            m = re.match(r'^#count (\d+) (.*)$', line)
            if m:
                rx = re.compile(m.group(2))
                n = sum(1 for l in log if rx.search(l))
                if n != int(m.group(1)):
                    print('    count %d, not %s: %s' % (n, m.group(1), m.group(2)))
                    fails += 1
        print('    %d expectations: %s' % (expects, 'all found in order' if fails == 0 else '%d failures' % fails))
        return 1 if fails else 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
