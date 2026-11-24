#!/bin/bash
# Mouse controls check (v1.29.0; port/reforged/reforged_mouse.c): scripted mouse sessions run offscreen on the
# played Linux window build (SDL_VIDEO_DRIVER=offscreen, no window), the Controls screen's "Mouse" row on.
#
#   port/tools/mousecheck.sh [--only NAME[,NAME...]] [--keep]
#
# 1. the planner's pure helpers (FE8PC_MOUSE_SELFTEST=exit: the picture -> game -> square mapping, Widescreen's
#    offset, the menu hit test, the shorter way round a list);
# 2. each scenario: a start save and the first ticks of a recorded route (to a free map), then mouse events
#    injected as real SDL events (FE8PC_TEST_MOUSE, port/test/inputs/mouse/*.mouse: their own comments say what
#    each does), the log (FE8PC_MOUSE_LOG=1) checked against the file's "#expect" lines (the screens the hooks
#    report, the clicks' squares, the keys pressed, the outcome: a unit selected and moved, Attack and a weapon
#    chosen by clicks, a target chosen with one click and confirmed with a second, the battle and the target's
#    HP, right-click cancels, the wheel in lists, menu hover, middle-click R, a side button doing nothing, the
#    focus click dropped, a key cutting a plan short, a target at the screen's edge (the camera scrolls) still
#    taking two clicks, a click with an enemy's range shown closing it and selecting the unit clicked, a
#    double-clicked unit selected once, a click in a Widescreen side bar, a click under Pixel / Fill / Stretch in
#    a 1200x500 window and fullscreen);
#    and the KEYREC equivalence: the keys recorded during the session (FE8PC_KEYREC) replayed with the row OFF
#    and no mouse (FE8PC_KEYFILE) give the same 240x160 frames (FE8PC_DUMP_FRAMES) and the same .sav and
#    .reforged: the mouse only ever pressed keys a pad could press;
# 3. invariance: Chapter 16's route (frames 5600-9000) with the row on and no mouse event, against the row off:
#    the same frames and saves.
# Prints "MOUSECHECK: PASS" only when every step passed. Data paths: ~/.config/fe8pc-release.conf (TESTDATA), as
# quickcheck.sh.
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1
REPO=$PWD
CONF=${FE8PC_RELEASE_CONF:-$HOME/.config/fe8pc-release.conf}
[ -f "$CONF" ] && . "$CONF"
: "${TESTDATA:?set TESTDATA in $CONF}"
ONLY=""; KEEP=0
while [ $# -gt 0 ]; do
  case $1 in
    --only) ONLY=",$2,"; shift;;
    --keep) KEEP=1;;
    *) echo "unknown argument $1"; exit 2;;
  esac; shift
done
J=${QC_JOBS:-8}
OUT=$HOME/mc; rm -rf "$OUT"; mkdir -p "$OUT"
START=$(date +%s)
bad=0
fail() { echo "FAIL: $*"; bad=1; }
step() { echo "[$(date +%H:%M:%S)] $*"; }
PY="python3 -I $REPO/port/tools/mousecheck.py"

step "building linux sdl"
b=build/pc-sdl/fe8pc-sdl
if [ ! -f "$b" ] || [ -n "$(find port src include -name '*.h' -newer "$b" -print -quit)" ]; then
  echo "  build/pc-sdl: a header changed, clean build"; rm -rf build/pc-sdl/src build/pc-sdl/port build/pc-sdl/*.o
fi
nice make -f port/Makefile.sdl -j$J > "$OUT/sdl.log" 2>&1 || { echo "FAIL: linux sdl build ($OUT/sdl.log)"; echo "MOUSECHECK: FAIL"; exit 1; }
BIN=$REPO/$b
SV=$TESTDATA/saves
INP=$REPO/port/test/inputs
ROUTE=$INP/route_f_lordpromo_e16.keys
[ -f "$SV/e16_start.sav" ] && [ -f "$ROUTE" ] || { echo "FAIL: missing $SV/e16_start.sav or $ROUTE"; echo "MOUSECHECK: FAIL"; exit 1; }

step "self-test"
mkdir -p "$OUT/st/u"; printf "mouse_controls = 1\n" > "$OUT/st/u/reforged-options.txt"
( cd "$OUT/st" && SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy FE8PC_PACE=0 FE8PC_USER_DIR="$OUT/st/u" FE8PC_FRAMES=600 \
    FE8PC_MOUSE_SELFTEST=exit FE8PC_FE7_ROM=/nonexistent FE8PC_FE6_ROM=/nonexistent timeout 300 "$BIN" --no-save "$REPO/baserom.gba" > out.log 2>&1 )
l=$(grep '\[mouse\] self-test:' "$OUT/st/out.log" | tail -1)
if [ "$l" = "[mouse] self-test: 0 failures" ]; then echo "  $l"; else fail "self-test: '${l:-no result}'"; grep 'self-test FAIL' "$OUT/st/out.log" | head; fi

# run NAME MOUSE(0/1) SAVE KEYS FRAMES DUMP OPTS [ENV...]   (OPTS: settings lines joined by ';', or -)
run() {
  local n=$1 mouse=$2 sav=$3 keys=$4 fr=$5 dump=$6 opts=$7; shift 7
  local T=$OUT/$n
  mkdir -p "$T/u" "$T/d"; cp "$sav" "$T/g.sav"; [ -f "$sav.reforged" ] && cp "$sav.reforged" "$T/g.sav.reforged"
  printf "arena_odds = 1\nmouse_controls = %s\n" "$mouse" > "$T/u/reforged-options.txt"
  [ "$opts" != - ] && echo "$opts" | tr ';' '\n' >> "$T/u/reforged-options.txt"
  ( cd "$T" && env -u FE8PC_TEST_UNIT_CLASS -u FE8PC_FIXES_FORCE -u FE8PC_RANDO_FORCE -u FE8PC_SCALE SDL_VIDEO_DRIVER=offscreen \
      SDL_AUDIO_DRIVER=dummy FE8PC_PACE=0 FE8PC_USER_DIR="$T/u" FE8PC_FE7_ROM=/nonexistent FE8PC_FE6_ROM=/nonexistent \
      FE8PC_KEYFILE="$keys" FE8PC_FRAMES=$fr FE8PC_DUMP_FRAMES=$dump FE8PC_DUMP_DIR="$T/d" "$@" \
      timeout 1800 "$BIN" --save "$T/g.sav" "$REPO/baserom.gba" > out.log 2>&1 )
}
same() { # A B: frames, .sav and .reforged of two runs
  local a=$OUT/$1 b=$OUT/$2 nf df ss sc
  nf=$(ls "$a/d" | wc -l); df=$(diff -rq "$a/d" "$b/d" | wc -l)
  ss=$(cmp -s "$a/g.sav" "$b/g.sav" && echo same || echo DIFFER)
  if [ -f "$a/g.sav.reforged" ] || [ -f "$b/g.sav.reforged" ]; then
    sc=$(cmp -s "$a/g.sav.reforged" "$b/g.sav.reforged" && echo same || echo DIFFER); else sc=none; fi
  SAME_LINE="$df differing frames of $nf; saves $ss; side files $sc"
  [ "$nf" -ge 100 ] && [ "$df" -eq 0 ] && [ "$ss" = same ] && [ "$sc" != DIFFER ]
}

$PY prefix "$ROUTE" 5885 "$OUT/turn1.keys"
$PY prefix "$ROUTE" 7925 "$OUT/turn2.keys"
M=$INP/mouse
# NAME|MOUSE FILE|MODE|START KEYS|FRAMES|DUMP|SETTINGS|ENV (picture pixels: FE8PC_TEST_MOUSE_PICTURE=1)
SCEN="attack|$M/attack_e16.mouse|-|$OUT/turn2.keys|9600|7900-9600|-|FE8PC_TEST_MOUSE_PICTURE=1
cancel|$M/cancel_e16.mouse|-|$OUT/turn1.keys|6700|5880-6700|-|FE8PC_TEST_MOUSE_PICTURE=1 FE8PC_TEST_HOLD_KEYS=6566:Tab:1
edge|$M/edge_e16.mouse|-|$OUT/turn1.keys|6200|5880-6200|-|FE8PC_TEST_MOUSE_PICTURE=1
range|$M/range_e16.mouse|-|$OUT/turn2.keys|8250|7900-8250|-|FE8PC_TEST_MOUSE_PICTURE=1
wide|$M/wide_e16.mouse|-|$OUT/turn2.keys|8150|7900-8150|widescreen = 1|FE8PC_TEST_MOUSE_PICTURE=1
pixel|$M/scale_e16.mouse|pixel|$OUT/turn2.keys|8020|7900-8020|scaling = 0|
fill|$M/scale_e16.mouse|fill|$OUT/turn2.keys|8020|7900-8020|scaling = 1|
stretch|$M/scale_e16.mouse|stretch|$OUT/turn2.keys|8020|7900-8020|scaling = 2|
fullscreen|$M/full_e16.mouse|-|$OUT/turn2.keys|8020|7900-8020|-|FE8PC_TEST_MOUSE_PICTURE=1"

step "mouse sessions (row on, events injected, keys recorded)"
RUN=""
declare -A MF MODE FRAMES DUMP OPTS
while IFS='|' read -r n mf mode keys fr dump opts env; do
  [ -n "$n" ] || continue
  if [ -n "$ONLY" ]; then case $ONLY in *,$n,*) ;; *) continue;; esac; fi
  $PY events "$mf" "$mode" "$OUT/$n.events"
  run ${n}_mouse 1 "$SV/e16_start.sav" "$keys" $fr $dump "$opts" FE8PC_MOUSE_LOG=1 FE8PC_TEST_MOUSE="$OUT/$n.events" \
      FE8PC_KEYREC="$OUT/$n.rec.keys" $env &
  # attack: also the same start with no event (its .sav must differ from the session's)
  [ $n = attack ] && run ${n}_none 1 "$SV/e16_start.sav" "$keys" $fr $dump "$opts" &
  RUN="$RUN $n"; MF[$n]=$mf; MODE[$n]=$mode; FRAMES[$n]=$fr; DUMP[$n]=$dump; OPTS[$n]=$opts
done <<< "$SCEN"
# 3. invariance: the row on, no mouse event, against the row off
inv=0
if [ -z "$ONLY" ] || case $ONLY in *,idle,*) true;; *) false;; esac; then
  run idle_on 1 "$SV/e16_start.sav" "$ROUTE" 9000 5600-9000 - FE8PC_MOUSE_LOG=1 &
  run idle_off 0 "$SV/e16_start.sav" "$ROUTE" 9000 5600-9000 - &
  inv=1
fi
wait

step "replays of the recorded keys (row off, no mouse)"
for n in $RUN; do
  if [ ! -s "$OUT/$n.rec.keys" ]; then continue; fi
  run ${n}_replay 0 "$SV/e16_start.sav" "$OUT/$n.rec.keys" ${FRAMES[$n]} ${DUMP[$n]} "${OPTS[$n]}" &
done
wait

step "results"
for n in $RUN; do
  echo "  $n:"
  log=$OUT/${n}_mouse/out.log
  res=$($PY check "$log" "${MF[$n]}" "${MODE[$n]}"); rc=$?
  echo "$res"
  [ $rc = 0 ] || fail "$n: the session's log ($log)"
  if [ ! -s "$OUT/$n.rec.keys" ]; then fail "$n: no keys recorded"; continue; fi
  presses=$(grep -c '^\[mouse\] frame [0-9]*: press ' "$log")
  wrote=""
  if [ -f "$OUT/${n}_none/g.sav" ]; then
    # the same start with no mouse event: the session's .sav must differ (the save the game wrote holds what
    # the mouse did), so its comparison above means something
    if cmp -s "$OUT/${n}_none/g.sav" "$OUT/${n}_mouse/g.sav"; then fail "$n: the .sav is the same as with no mouse event"
    else wrote="; its .sav differs from a run with no mouse event"; fi
  fi
  if same ${n}_mouse ${n}_replay; then echo "    keys recorded ($presses mouse presses), replayed with the mouse off: $SAME_LINE$wrote"
  else fail "$n: KEYREC replay: $SAME_LINE"; fi
done
if [ $inv = 1 ]; then
  if same idle_on idle_off; then echo "  idle (row on, no mouse event, against off): $SAME_LINE"
  else fail "idle: row on against off: $SAME_LINE"; fi
  grep -q '^\[mouse\] frame [0-9]*: press' "$OUT/idle_on/out.log" && fail "idle: the mouse pressed a key with no event"
fi

[ $KEEP = 1 ] || rm -rf "$OUT"/*/d
MIN=$(( ($(date +%s) - START + 59) / 60 ))
if [ $bad = 0 ]; then echo "MOUSECHECK: PASS ($MIN min)"; else echo "MOUSECHECK: FAIL ($MIN min; logs in $OUT)"; fi
exit $bad
