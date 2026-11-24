#!/bin/bash
# Widescreen invariance check (v1.26.0): the identity routes of quickcheck.sh replayed on this build twice,
# the Display screen's "Widescreen" row off and on, offscreen (SDL_VIDEO_DRIVER=offscreen, no window).
#
#   port/tools/widecheck.sh [--routes NAME[,NAME...]] [--sheets DIR] [--keep]
#
#   --routes   the routes to run (default: every quickcheck route whose data is there; the FE7/FE6 ones
#              only with FE7ROM and FE6ROM set; plus e16map, lordpromo's first player and enemy phases
#              on the Chapter 16 map, whose camera reaches both map edges; and, v1.27.0, e16wide:
#              port/test/inputs/features/wide_e16.keys with HP bars and the danger zone on: a range, the
#              movement arrow and a unit's move across the seam with the camera at the map's left edge, the
#              danger zone, the enemy phase; e16bars: lordpromo's frames 65000-66000 with HP bars on, hurt
#              units' bars crossing the left seam, staff target squares; v1.28.0: e16battle, lordpromo's
#              first battle with the camera at the map's right edge (the strip slides to the middle for the
#              battle animation and the level-up screen, and back); e16late, lordpromo's frames 34950-36400
#              with HP bars on (units walking across the seam at the map's left edge, hurt units' bars in
#              a side, a tile change's cross-fade, the phase banner, ranges); e16heal, frames 22400-22700 (a
#              staff's effect on BG2 left out of the sides); e11fog: Eirika's Chapter 11 (fog;
#              route_e11.keys from $TESTDATA/saves/e11_start.sav, skipped without it), frames 11400-12750:
#              enemies walking in the fog, one hidden at a side, a battle); e11cam: the same route's frames 73850-73965
#              (a staff's target selection scrolling the camera 4 pixels past the map's bottom: the sides
#              must show the map on every frame)
#   --sheets   also write a PNG contact sheet of frames whose sides show the map, one per route, to DIR
#   --keep     keep the dumps (in ~/wc)
#
# For each route: (a) every 240x160 frame (FE8PC_DUMP_FRAMES), the .sav and the .reforged side file are
# identical with the row off and on: the row changes only the picture, never the game; (b) every 284x160
# picture (FE8PC_DUMP_WIDE) holds the 240x160 frame at its x = off (wide.log) and black sides whenever its
# sides do not show the map, and (v1.28.0) the strip only slides between the map and a battle over it and
# is in the middle for the battle; (c) how many frames show the map at the sides and how many are black,
# and (v1.28.0) what the sides covered (moving units, the fog, HP bars, ...): a route's last field names
# what it must cover (widecheck.py --expect).
# Prints "WIDECHECK: PASS" only when every route passed. Data paths: ~/.config/fe8pc-release.conf
# (TESTDATA, FE7ROM, FE6ROM), as quickcheck.sh.
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1
REPO=$PWD
CONF=${FE8PC_RELEASE_CONF:-$HOME/.config/fe8pc-release.conf}
[ -f "$CONF" ] && . "$CONF"
: "${TESTDATA:?set TESTDATA in $CONF}"
FE7ROM=${FE7ROM:-/nonexistent-fe7.gba}; FE6ROM=${FE6ROM:-/nonexistent-fe6.gba}
HAVE_ROMS=0; [ -f "$FE7ROM" ] && [ -f "$FE6ROM" ] && HAVE_ROMS=1
ONLY=""; SHEETS=""; KEEP=0
while [ $# -gt 0 ]; do
  case $1 in
    --routes) ONLY=",$2,"; shift;;
    --sheets) SHEETS=$2; shift;;
    --keep) KEEP=1;;
    *) echo "unknown argument $1"; exit 2;;
  esac; shift
done
J=${QC_JOBS:-8}
OUT=$HOME/wc; rm -rf "$OUT"; mkdir -p "$OUT"
# v1.28.0: the FE7/FE6 routes read local copies of the ROMs, read once here: 30-odd runs reading them from a
# slow mount (/mnt/d) at once on a loaded machine once got a short header in one run (a false FAIL)
if [ $HAVE_ROMS = 1 ] && mkdir -p "$OUT/roms" && cp "$FE7ROM" "$OUT/roms/fe7.gba" && cp "$FE6ROM" "$OUT/roms/fe6.gba" &&
   cmp -s "$FE7ROM" "$OUT/roms/fe7.gba" && cmp -s "$FE6ROM" "$OUT/roms/fe6.gba"; then
  FE7ROM=$OUT/roms/fe7.gba; FE6ROM=$OUT/roms/fe6.gba
fi
[ -n "$SHEETS" ] && mkdir -p "$SHEETS"
START=$(date +%s)
bad=0
fail() { echo "FAIL: $*"; bad=1; }
step() { echo "[$(date +%H:%M:%S)] $*"; }

step "building linux sdl"
b=build/pc-sdl/fe8pc-sdl
if [ ! -f "$b" ] || [ -n "$(find port src include -name '*.h' -newer "$b" -print -quit)" ]; then
  echo "  build/pc-sdl: a header changed, clean build"; rm -rf build/pc-sdl/src build/pc-sdl/port build/pc-sdl/*.o
fi
nice make -f port/Makefile.sdl -j$J > "$OUT/sdl.log" 2>&1 || { echo "FAIL: linux sdl build ($OUT/sdl.log)"; echo "WIDECHECK: FAIL"; exit 1; }
BIN=$REPO/$b

INP=$REPO/port/test/inputs
run() { # NAME WIDE SAVE KEYS FRAMES DUMP ROMS OPTS [ENV...]  (OPTS: settings lines joined by ';', or -)
  local n=$1 wide=$2 sav=$3 keys=$4 fr=$5 dump=$6 roms=$7 opts=$8; shift 8
  local T=$OUT/$n F7=/nonexistent-fe7.gba F6=/nonexistent-fe6.gba
  mkdir -p "$T/u" "$T/d" "$T/w"; cp "$sav" "$T/g.sav"; [ -f "$sav.reforged" ] && cp "$sav.reforged" "$T/g.sav.reforged"
  printf "arena_odds = 1\nwidescreen = %s\n" "$wide" > "$T/u/reforged-options.txt"
  [ "$opts" != - ] && echo "$opts" | tr ';' '\n' >> "$T/u/reforged-options.txt"
  [ "$roms" = both ] && { F7=$FE7ROM; F6=$FE6ROM; }
  ( cd "$T" && env -u FE8PC_TEST_UNIT_CLASS -u FE8PC_FIXES_FORCE -u FE8PC_RANDO_FORCE SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
      FE8PC_PACE=0 FE8PC_USER_DIR="$T/u" FE8PC_FE7_ROM="$F7" FE8PC_FE6_ROM="$F6" FE8PC_KEYFILE="$keys" FE8PC_FRAMES=$fr \
      FE8PC_DUMP_FRAMES=$dump FE8PC_DUMP_DIR="$T/d" FE8PC_DUMP_WIDE="$T/w" "$@" timeout 1800 "$BIN" --save "$T/g.sav" "$REPO/baserom.gba" > out.log 2>&1 )
}
SV=$TESTDATA/saves
ROUTES="thirdtier|$SV/f_tt_start.sav|$INP/route_f_thirdtier_ch6.keys|3014|700-3014|none|
promochoice|$SV/i14_start.sav|$INP/route_f_promochoice_i14.keys|4080|1600-4080|none|
trainee|$SV/f_promo_start.sav|$INP/route_f_promo_ch6.keys|43200|39000-43200|none|
arena|$SV/ch5_start.sav|$INP/route_f_arena_ch5.keys|29100|26900-29100|none|
lordpromo|$SV/e16_start.sav|$INP/route_f_lordpromo_e16.keys|107773|101500-103500|none|
e16map|$SV/e16_start.sav|$INP/route_f_lordpromo_e16.keys|8830|5600-8830|none|
e16wide|$SV/e16_start.sav|$INP/features/wide_e16.keys|8495|5880-8495|none||map_hp_bars_warnings = 3;danger_zone_select = 1|sprites,mus,range,squares
e16bars|$SV/e16_start.sav|$INP/route_f_lordpromo_e16.keys|66000|65000-66000|none||map_hp_bars_warnings = 3|hps
e16battle|$SV/e16_start.sav|$INP/route_f_lordpromo_e16.keys|9900|8800-9900|none|||held,slide
e16late|$SV/e16_start.sav|$INP/route_f_lordpromo_e16.keys|36400|34950-36400|none||map_hp_bars_warnings = 3|mus,hps,squares,range
e16heal|$SV/e16_start.sav|$INP/route_f_lordpromo_e16.keys|22700|22400-22700|none|||local
e11fog|$SV/e11_start.sav|$INP/route_e11.keys|12750|11400-12750|opt|||mus,fogs,local,held
e11cam|$SV/e11_start.sav|$INP/route_e11.keys|73965|73850-73965|opt|||range,allmap
lordpromo7|$SV/e16_start.sav|$INP/route_f_lordpromo_e16_fe7.keys|109273|101500-103500|both|
thirdtier7|$SV/f_tt_start.sav|$INP/route_f_thirdtier_ch6.keys|3014|700-3014|both|
rndnone|$SV/f_tt_start.sav|$INP/route_f_thirdtier_ch6.keys|3014|700-3014|none|FE8PC_RANDO_FORCE=0x1234
rndboth|$SV/f_tt_start.sav|$INP/route_f_thirdtier_ch6.keys|3014|700-3014|both|FE8PC_RANDO_FORCE=0x1234
v18|$TESTDATA/v18/g.sav|$TESTDATA/v18/res3.keys|2352|816-2352|both|"
RUN=""
declare -A EXPECT
step "routes, Widescreen off and on"
while IFS='|' read -r n sav keys fr dump roms env opts expect; do
  [ -n "$n" ] || continue
  if [ -n "$ONLY" ]; then case $ONLY in *,$n,*) ;; *) continue;; esac; fi
  if [ "$roms" = both ] && [ $HAVE_ROMS = 0 ]; then echo "  $n: skipped (FE7/FE6 ROMs not set)"; continue; fi
  # roms "opt": a start save that is not in every test data set (skipped without it)
  if [ "$roms" = opt ] && [ ! -f "$sav" ]; then echo "  $n: skipped (no $sav)"; continue; fi
  if [ ! -f "$sav" ] || [ ! -f "$keys" ]; then fail "$n: missing $sav or $keys"; continue; fi
  run ${n}_off 0 "$sav" "$keys" $fr $dump $roms "${opts:--}" $env &
  run ${n}_on 1 "$sav" "$keys" $fr $dump $roms "${opts:--}" $env &
  RUN="$RUN $n"
  EXPECT[$n]=$expect
done <<< "$ROUTES"
wait

for n in $RUN; do
  o=$OUT/${n}_off; w=$OUT/${n}_on
  nf=$(ls "$o/d" | wc -l); df=$(diff -rq "$o/d" "$w/d" | wc -l)
  ss=$(cmp -s "$o/g.sav" "$w/g.sav" && echo same || echo DIFFER)
  if [ -f "$o/g.sav.reforged" ] || [ -f "$w/g.sav.reforged" ]; then
    sc=$(cmp -s "$o/g.sav.reforged" "$w/g.sav.reforged" && echo same || echo DIFFER); else sc=none; fi
  nw=$(ls "$o/w" | wc -l)
  line="$n: 240 frames $df differing of $nf; saves $ss; side files $sc"
  if [ "$nf" -lt 100 ] || [ "$df" -ne 0 ] || [ "$ss" != same ] || [ "$sc" = DIFFER ] || [ "$nw" -ne 0 ]; then
    fail "$line (wide pictures with the row off: $nw)"
  else echo "  $line"; fi
  sheet=""; [ -n "$SHEETS" ] && sheet="--sheet $SHEETS/$n.png --title $n"
  exp=""; [ -n "${EXPECT[$n]}" ] && exp="--expect ${EXPECT[$n]}"
  res=$(python3 -I port/tools/widecheck.py "$w/d" "$w/w" $sheet $exp 2>&1); rc=$?
  echo "$res" | sed 's/^/    /'
  [ $rc = 0 ] || fail "$n: wide pictures"
done

[ $KEEP = 1 ] || rm -rf "$OUT"/*/d "$OUT"/*/w
rm -rf "$OUT/roms"
MIN=$(( ($(date +%s) - START + 59) / 60 ))
if [ $bad = 0 ]; then echo "WIDECHECK: PASS ($MIN min)"; else echo "WIDECHECK: FAIL ($MIN min; logs in $OUT)"; fi
exit $bad
