#!/usr/bin/env python3
"""Checks the sound replay itself (m4a_replay.py): a driver change that alters
only player or track state, never the PCM lanes, must fail the replay.

    python3 port/test/audio/mutate_replay.py [build/gate0/snd/title.gba.snd]   # about a minute

Needs a capture and its call record (make -f port/test/Makefile snd-gba
SND=title). Each mutant is an altered copy of port/audio/m4a_1_pc.c or
src/m4a.c in build/difftest/replay-mutants (ignored by git; the sources are
never edited). The unaltered driver must pass. Each mutant must fail the
current rule with every PCM lane still equal (so only player or track bytes
show it). The rule before 2026-10-19 (m4a_replay.py --legacy) runs too, for
comparison: it excused any VBlank whose players or tracks differ as "a
main-thread sound call" and never ran the game's calls, so it passes both
mutants. That is the hole the call record closes.
"""

import os
import re
import subprocess
import sys

# (name, file, old text, new text, what changes)
MUTANTS = [
    ('mplaymain-clock-2', 'port/audio/m4a_1_pc.c', 'mplayInfo->clock++;', 'mplayInfo->clock += 2;',
     'player clock, VBlank handler path (MPlayMain)'),
    ('imminit-lfospeed-23', 'src/m4a.c', 'track->lfoSpeed = 22;', 'track->lfoSpeed = 23;',
     'track lfoSpeed, game call path (m4aMPlayImmInit)'),
]


def replay(root, snd, outdir, legacy, c_source=None, m4a_source=None):
    cmd = [sys.executable, 'port/test/audio/m4a_replay.py', snd, '--out', outdir, '--show', '2']
    if legacy:
        cmd.append('--legacy')
    if c_source:
        cmd += ['--c-source', c_source]
    if m4a_source:
        cmd += ['--m4a-source', m4a_source]
    r = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
    text = r.stdout + r.stderr
    if legacy:
        m = re.search(r'legacy rule: replayed (\d+) VBlanks.*?(\d+) fully equal, (\d+) differ.*?PCM lanes equal on (\d+)',
                      text, re.S)
    else:
        m = re.search(r'replayed (\d+) VBlanks.*?(\d+) fully equal.*?(\d+) differ.*?PCM lanes \(the FIFO bytes\) equal'
                      r' on (\d+)', text, re.S)
    if not m:
        return None, text
    total, equal, differ, lanes = (int(x) for x in m.groups())
    passed = re.search(r'RESULT[^:]*: PASS', text) is not None
    first = [l.strip() for l in text.splitlines() if l.strip().startswith('record ')][:1]
    return dict(total=total, equal=equal, differ=differ, lanes=lanes, passed=passed,
                rc=r.returncode, first=first[0] if first else ''), text


def main():
    root = os.getcwd()
    snd = sys.argv[1] if len(sys.argv) > 1 else 'build/gate0/snd/title.gba.snd'
    outdir = os.path.join(root, 'build', 'difftest', 'replay-mutants')
    os.makedirs(outdir, exist_ok=True)
    ok = True

    base, text = replay(root, snd, os.path.join(outdir, 'baseline'), False)
    if base is None or not base['passed'] or base['rc'] != 0:
        print(text)
        print('BAD baseline: the unaltered driver does not pass')
        return 1
    print(f'OK  baseline                 current rule PASS ({base["equal"]} of {base["total"]} VBlanks fully equal)')

    for name, path, old, new, what in MUTANTS:
        src = open(os.path.join(root, path)).read()
        if src.count(old) != 1:
            print(f'BAD {name}: pattern found {src.count(old)} times in {path}')
            ok = False
            continue
        mdir = os.path.join(outdir, name)
        os.makedirs(mdir, exist_ok=True)
        mpath = os.path.join(mdir, os.path.basename(path))
        with open(mpath, 'w') as f:
            f.write(src.replace(old, new))
        kw = {'c_source': mpath} if path.startswith('port/') else {'m4a_source': mpath}
        cur, ctext = replay(root, snd, mdir, False, **kw)
        leg, ltext = replay(root, snd, mdir, True, **kw)
        if cur is None or leg is None:
            print(ctext if cur is None else ltext)
            print(f'BAD {name}: no result')
            ok = False
            continue
        good = not cur['passed'] and cur['rc'] != 0 and cur['lanes'] == cur['total']
        ok &= good
        print(f'{"OK " if good else "BAD"} {name:24s} ({what})')
        print(f'      current rule: {"PASS" if cur["passed"] else "FAIL"}, {cur["differ"]} VBlanks differ,'
              f' PCM lanes equal on {cur["lanes"]} of {cur["total"]}; {cur["first"]}')
        print(f'      legacy rule:  {"PASS" if leg["passed"] else "FAIL"}, {leg["differ"]} VBlanks differ,'
              f' PCM lanes equal on {leg["lanes"]} of {leg["total"]}')
    print('RESULT:', 'PASS' if ok else 'FAIL', '(every mutant caught by the current rule, PCM lanes unchanged)')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
