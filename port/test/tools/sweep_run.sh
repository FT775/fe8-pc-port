#!/bin/bash
# One PC run of a bug-sweep binary (port/test/sweep.mk; port/test/README.md,
# "Bug sweeps"), timed. Run from the repository root.
#
#   sweep_run.sh NAME EXE OUT KEYS TICKS [GBA_DIR]
#
# OUT is a path prefix: the run writes OUT.jsonl (its Gate 0 trace), OUT.raw,
# OUT.log and OUT.cost.json, and the UBSan binary (NAME ubsan...) also its site
# table OUT.ubsan.jsonl and OUT.ubsan.txt (port/test/pc/ubsan_log.c); with
# FE8PC_NULLPROBE=1 in the environment, the NULL probe's table OUT.sites.jsonl.
# GBA_DIR: a GBA capture of the same keys (gba.jsonl; gba_raster.bin when it is
# there): the run is then the lag replay (README.md, "Lag replay"), which keeps
# a bot route on the GBA's path; without it, the plain run.
# GATE0_SRAM_IN and GATE0_STATE_IN from the environment start the run from a
# save or a save state (README.md, "Chapter saves" and "Save states"); for a
# save state, its part (pclag for the lag replay, pc for the plain run) is
# relinked for EXE first. SWEEP_TIMEOUT: seconds (default 3600). GBA_DIR from
# the environment is the GBA build tree (default .).
set -u
if [ $# -lt 5 ]; then
    echo "usage: $0 NAME EXE OUT KEYS TICKS [GBA_DIR]" >&2
    exit 2
fi
name=$1 exe=$2 out=$3 keys=$4 ticks=$5 gba=${6:-}
tree=${GBA_DIR:-.}
mkdir -p "$(dirname "$out")"
rm -f "$out.jsonl" "$out.raw" "$out.log" "$out.cost.json" "$out.ubsan.jsonl" "$out.ubsan.txt" "$out.time"

# The same renderer settings as port/test/Makefile's PC runs (FE8PC_MGBA_BGCLIP=1 since
# b3a86d7d, README.md "mGBA BG clip"), so a sweep binary's frames compare with the GBA's
# as the Gate 0 binary's do.
envs=(GATE0_KEYS="$keys" GATE0_TICKS="$ticks" GATE0_OUT="$out.jsonl" GATE0_RAW="$out.raw"
      FE8PC_BLEND=mgba8 FE8PC_MGBA_BGCLIP=1 FE8PC_FRAMES=0)
mode="plain run" part=pc
if [ -n "$gba" ]; then
    [ -f "$gba/gba.jsonl" ] || { echo "sweep_run: no GBA capture $gba/gba.jsonl" >&2; exit 2; }
    python3 - "$gba/gba.jsonl" "$keys" "$ticks" <<'EOF' || exit 2
import json, sys
path, keys, ticks = sys.argv[1:]
head = json.loads(open(path).readline())
if head.get("keys") != keys or int(head.get("ticks", 0)) < int(ticks):
    print(f"sweep_run: {path} is a GBA capture of {head.get('keys')} to {head.get('ticks')} ticks, not of {keys} "
          f"to {ticks}")
    sys.exit(2)
EOF
    envs+=(GATE0_LAG_FROM="$gba/gba.jsonl")
    [ -f "$gba/gba_raster.bin" ] && envs+=(GATE0_RASTER_FROM="$gba/gba_raster.bin")
    mode="lag replay" part=pclag
fi
case "$name" in ubsan*) envs+=(FE8PC_UBSAN_OUT="$out.ubsan.jsonl");; esac
# The NULL probe (README.md, "NULL probe"), when on: each run its own table.
if [ "${FE8PC_NULLPROBE:-}" = 1 ]; then
    rm -f "$out.sites.jsonl"
    envs+=(FE8PC_NULLPROBE_OUT="$out.sites.jsonl" FE8PC_NULLPROBE_ROUTE="$(basename "$keys" .keys)")
fi
start="power-on"
[ -n "${GATE0_SRAM_IN:-}" ] && start="save $GATE0_SRAM_IN"
if [ -n "${GATE0_STATE_IN:-}" ]; then
    start="save state $GATE0_STATE_IN ($part)"
    python3 port/test/tools/savestate.py relink --part "$GATE0_STATE_IN/$part" --exe "$exe" \
        --gba-elf "$tree/fireemblem8.elf" > "$out.relink.log" 2>&1 \
        || { echo "sweep_run: $name: relink of $GATE0_STATE_IN/$part for $exe failed:"; tail -5 "$out.relink.log"; exit 2; }
fi

t0=$(date +%s.%N)
env "${envs[@]}" /usr/bin/time -o "$out.time" -f "%e %U %S %M" timeout "${SWEEP_TIMEOUT:-3600}" \
    "$exe" "$tree/baserom.gba" > "$out.log" 2>&1
status=$?
t1=$(date +%s.%N)

# Ticks this run sampled: its trace lines but the header (and a resumed
# trace's prologue line, tick T-1, from the snapshot's run).
lines=$(($(wc -l < "$out.jsonl" 2>/dev/null || echo 1) - 1))
first=0
if [ -n "${GATE0_STATE_IN:-}" ] && [ "$lines" -gt 0 ]; then
    lines=$((lines - 1))
    first=$(sed -n '2s/^{"tick":\([0-9]*\).*/\1/p' "$out.jsonl")
    first=$((first + 1))
fi
python3 - "$name" "$exe" "$out" "$mode" "$start" "$status" "$t0" "$t1" "$lines" "$first" "$ticks" <<'EOF'
import json, sys
name, exe, out, mode, start, status, t0, t1, ran, first, ticks = sys.argv[1:]
wall = float(t1) - float(t0)
ran = int(ran)
try:
    e, u, s, m = open(out + ".time").read().split()[-4:]
    user, sys_s, rss = float(u), float(s), int(m)
except (OSError, ValueError):
    user = sys_s = rss = None
fault = next((l.strip() for l in open(out + ".log", errors="replace") if l.startswith("[fault]")), None)
cost = {"name": name, "exe": exe, "mode": mode, "start": start, "status": int(status), "fault": fault,
        "first_tick": int(first), "ticks_run": ran, "ticks_wanted": int(ticks) - int(first), "wall_s": round(wall, 2),
        "user_s": user, "sys_s": sys_s, "max_rss_kb": rss,
        "wall_s_per_10k_ticks": round(wall / ran * 10000, 2) if ran else None,
        "user_s_per_10k_ticks": round(user / ran * 10000, 2) if ran and user is not None else None}
json.dump(cost, open(out + ".cost.json", "w"), indent=1)
per = f"{cost['wall_s_per_10k_ticks']} s" if ran else "-"
print(f"[sweep] {name}: {ran} of {cost['ticks_wanted']} ticks ({mode}, from {start}) in {wall:.1f} s wall, "
      f"{user if user is not None else '?'} s user; {per} wall per 10,000 ticks; exit {status}"
      + (f"; {fault}" if fault else ""))
EOF
exit $status
