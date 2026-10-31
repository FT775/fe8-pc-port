#!/bin/sh
# Run the reservation probe N times per host to check that ASLR never lands
# something inside the GBA ranges. Run from the repo root after building:
#   gcc -m32 -O1 -o build/pc/reserve_probe port/tools/reserve_probe.c
#   i686-w64-mingw32-gcc -O1 -o build/pc/reserve_probe.exe port/tools/reserve_probe.c
#   sh port/tools/probe_repeat.sh 40
n=${1:-40}
for exe in build/pc/reserve_probe build/pc/reserve_probe.exe; do
    ok=0
    bad=0
    i=0
    while [ "$i" -lt "$n" ]; do
        if "./$exe" > build/pc/probe_last.txt 2>&1; then
            ok=$((ok + 1))
        else
            bad=$((bad + 1))
            grep FAIL build/pc/probe_last.txt
        fi
        i=$((i + 1))
    done
    echo "$exe: $ok of $n runs reserved every region, $bad failed"
done
