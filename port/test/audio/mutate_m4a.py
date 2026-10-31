#!/usr/bin/env python3
"""Checks the m4a difftest itself: runs it on altered copies of
port/audio/m4a_1_pc.c and expects each behaviour change to be caught.

    python3 port/test/audio/mutate_m4a.py        # about 3 minutes, 4 at a time

The source file is never edited. Mutants and their builds go to
build/difftest/audio-mutants (ignored by git). Each mutant runs
m4a_difftest.py --quick with fewer VBlanks per scenario.
"""

import concurrent.futures
import os
import subprocess
import sys

SRC = 'port/audio/m4a_1_pc.c'

# (name, old text, new text, expected: 'caught' or 'equivalent')
MUTANTS = [
    ('interp-shift-22', 'r9 = r0 + Asr(lr * r1, 23);', 'r9 = r0 + Asr(lr * r1, 22);', 'caught'),
    ('reverb-no-round', 'if (v & 0x80)\n                v++;', 'if (v & 0x100)\n                v++;', 'caught'),
    ('master-volume-no-plus1', '((soundInfo->masterVolume + 1u) * r5) >> 4', '(soundInfo->masterVolume * r5) >> 4', 'caught'),
    ('mix-no-bic', 'return ((vol16 * sample) & ~0xFF0000u) + Ror(word, 8);', 'return (vol16 * sample) + Ror(word, 8);', 'caught'),
    ('fixed-no-loop', '    r2 = m->loopLen;\n    if (r2 != 0)', '    r2 = 0;\n    if (r2 != 0)', 'caught'),
    ('end-no-rotate', 'u32 rot = (3u - (r5 >> 30)) << 3;', 'u32 rot = 0;', 'caught'),
    ('lfo-fall-7f', 'w = 0x80u - c;', 'w = 0x7Fu - c;', 'caught'),
    ('tempo-151', 'if (tempo < 150)', 'if (tempo < 151)', 'caught'),
    ('note-tiebreak', 'if (tr > bestTrack)', 'if (tr < bestTrack)', 'caught'),
    ('chk-adr-24', 'if ((addr >> 25) != 0)', 'if ((addr >> 24) != 0)', 'caught'),
    ('trackstop-no-oscoff', 'SOUND_INFO_PTR->CgbOscOff((u8)cgb);', '(void)cgb;', 'caught'),
    ('vsync-period', 'if (GtAfterSub(c, 1))\n        return;', 'if (GtAfterSub(c, 2))\n        return;', 'caught'),
    ('chnvol-left-80', 'v = Asr((0x7Fu - rp) * ve * track->volML, 14);', 'v = Asr((0x80u - rp) * ve * track->volML, 14);', 'caught'),
    ('endtie-no-key-check', '&& Ld8(chan + CH_MK) == key)', ')', 'caught'),
    ('maxlines-gt', 'if (deadline != 0 && CurrentLine() >= deadline)', 'if (deadline != 0 && CurrentLine() > deadline)', 'caught'),
    ('fixed-fast-path-8', 'if (S(r2) <= 4)\n        goto L0EC;', 'if (S(r2) <= 8)\n        goto L0EC;', 'equivalent'),
]


def run_one(root, outdir, m):
    name, old, new, expect = m
    src = open(os.path.join(root, SRC)).read()
    if src.count(old) != 1:
        return name, expect, f'pattern found {src.count(old)} times'
    mdir = os.path.join(outdir, name)
    os.makedirs(mdir, exist_ok=True)
    path = os.path.join(mdir, 'm4a_1_pc.c')
    with open(path, 'w') as f:
        f.write(src.replace(old, new))
    r = subprocess.run([sys.executable, 'port/test/audio/m4a_difftest.py', '--quick', '--frames', '60',
                        '--c-source', path, '--out', mdir], cwd=root, capture_output=True, text=True)
    total = [l for l in r.stdout.splitlines() if l.strip().startswith('total')]
    return name, expect, ('passed' if r.returncode == 0 else 'failed') + (' ' + total[0].strip() if total else '')


def main():
    root = os.getcwd()
    outdir = os.path.join(root, 'build', 'difftest', 'audio-mutants')
    os.makedirs(outdir, exist_ok=True)
    ok = True
    with concurrent.futures.ThreadPoolExecutor(4) as ex:
        for name, expect, result in ex.map(lambda m: run_one(root, outdir, m), MUTANTS):
            good = (expect == 'caught' and result.startswith('failed')) or \
                   (expect == 'equivalent' and result.startswith('passed'))
            ok &= good
            print(f'{"OK " if good else "BAD"} {name:26s} expected {expect:10s} -> {result}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
