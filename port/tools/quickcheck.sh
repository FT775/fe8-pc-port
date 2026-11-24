#!/bin/bash
# Quick release check (about 10-20 minutes): the three PC builds, the randomizer and Third promotion
# self-tests, and the identity routes against the previous release, plus the GBA build byte-identical.
#
#   port/tools/quickcheck.sh [--base TAG] [--full] [--allow-diff NAME[,NAME...]]
#
#   --base TAG     the release to compare with (default: the newest v* tag before HEAD's tree)
#   --full         also the 229 save checks against mGBA (port/test/tools/savechk_all.py, ~20 min more)
#   --allow-diff   identity routes that may differ on purpose (a change meant to alter play or the
#                  picture); they are still run and reported, but do not fail the check.
#                  Routes: thirdtier promochoice trainee arena lordpromo lordpromo7 thirdtier7 rndnone
#                  rndboth v18; "all" allows every route.
#
# The identity routes replay a recorded route with the default settings on this build and on the
# previous release and compare every dumped frame, the .sav and the .reforged side file: 0 differences
# means a player who does not touch a new option sees exactly the previous release.
#
# Local paths come from ~/.config/fe8pc-release.conf (never in git): TESTDATA (start saves, the v18
# game), FE7ROM, FE6ROM (optional; routes needing them are skipped without). The previous release's
# binaries are kept by release.sh in ~/fe8-base/TAG/bin; when missing they are built once from the tag.
#
# Prints "QUICKCHECK: PASS" and writes build/quickcheck.stamp (HEAD, the working tree's diff) only when
# every step passed; release.sh requires that stamp to match.
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1
REPO=$PWD
CONF=${FE8PC_RELEASE_CONF:-$HOME/.config/fe8pc-release.conf}
[ -f "$CONF" ] && . "$CONF"
: "${TESTDATA:?set TESTDATA in $CONF}"
FE7ROM=${FE7ROM:-/nonexistent-fe7.gba}; FE6ROM=${FE6ROM:-/nonexistent-fe6.gba}
HAVE_ROMS=0; [ -f "$FE7ROM" ] && [ -f "$FE6ROM" ] && HAVE_ROMS=1
BASE=""; FULL=0; ALLOW=","
while [ $# -gt 0 ]; do
  case $1 in
    --base) BASE=$2; shift;;
    --full) FULL=1;;
    --allow-diff) ALLOW=",$2,";;
    *) echo "unknown argument $1"; exit 2;;
  esac; shift
done
[ -n "$BASE" ] || BASE=$(git describe --tags --abbrev=0 --match 'v[0-9]*' HEAD)
J=${QC_JOBS:-8}
OUT=$HOME/qc; rm -rf "$OUT"; mkdir -p "$OUT"
START=$(date +%s)
bad=0
fail() { echo "FAIL: $*"; bad=1; }
step() { echo "[$(date +%H:%M:%S)] $*"; }
allowed() { case $ALLOW in *,all,*|*,$1,*) return 0;; esac; return 1; }

step "quickcheck against $BASE ($( [ $FULL = 1 ] && echo full || echo quick ))"

# 1. builds (incremental; the Makefiles do not follow header changes, so a build older than any header is
#    rebuilt from scratch)
step "building windows, linux sdl, linux full"
for b in build/win32-sdl/fe8pc-sdl.exe build/pc-sdl/fe8pc-sdl build/pc-full/fe8pc; do
  d=${b%/*}
  if [ ! -f "$b" ] || [ -n "$(find port src include -name '*.h' -newer "$b" -print -quit)" ]; then
    echo "  $d: a header changed, clean build"; rm -rf "$d/src" "$d/port" "$d"/*.o
  fi
done
make -f port/Makefile.mingw SDL=1 -j$J > "$OUT/win.log" 2>&1 || fail "windows build ($OUT/win.log)"
nice make -f port/Makefile.sdl -j$J > "$OUT/sdl.log" 2>&1 || fail "linux sdl build ($OUT/sdl.log)"
make -f port/Makefile FULL=1 BUILD=build/pc-full -j$J > "$OUT/full.log" 2>&1 || fail "linux full build ($OUT/full.log)"
[ $bad = 0 ] || { grep -h -m5 'error' "$OUT"/*.log; echo "QUICKCHECK: FAIL"; exit 1; }
NEW_SDL=$REPO/build/pc-sdl/fe8pc-sdl; NEW_FULL=$REPO/build/pc-full/fe8pc

# 2. the previous release's binaries
B=$HOME/fe8-base/$BASE
if [ ! -x "$B/bin/fe8pc-sdl" ] || [ ! -x "$B/bin/fe8pc" ]; then
  step "building $BASE once (kept in $B)"
  mkdir -p "$B"
  [ -d "$B/src" ] || git worktree add -f --detach "$B/src" "$BASE" > "$B/build.log" 2>&1
  cp baserom.gba "$B/src/" 2>/dev/null
  ( cd "$B/src" && nice make -j$J compare && nice make -f port/Makefile.sdl -j$J \
      && make -f port/Makefile FULL=1 BUILD=build/pc-full -j$J ) >> "$B/build.log" 2>&1 \
    && mkdir -p "$B/bin" && cp "$B/src/build/pc-sdl/fe8pc-sdl" "$B/src/build/pc-full/fe8pc" "$B/bin/" \
    || { echo "FAIL: building $BASE ($B/build.log)"; echo "QUICKCHECK: FAIL"; exit 1; }
fi
OLD_SDL=$B/bin/fe8pc-sdl; OLD_FULL=$B/bin/fe8pc

# 3. self-tests: 0 failures, and the randomizer's lines equal the previous release's (a run started in an
#    older version gives the same game)
step "self-tests"
selftest() { # NAME BIN SET ENVVAR
  local T=$OUT/st/$1 F7=/nonexistent-fe7.gba F6=/nonexistent-fe6.gba
  case $3 in fe7) F7=$FE7ROM;; fe6) F6=$FE6ROM;; both) F7=$FE7ROM; F6=$FE6ROM;; esac
  mkdir -p "$T/xdg"
  ( cd "$T" && env -u FE8PC_TEST_UNIT_CLASS FE8PC_FE7_ROM="$F7" FE8PC_FE6_ROM="$F6" XDG_DATA_HOME="$T/xdg" FE8PC_USER_DIR="$T/xdg/" \
      $4=exit timeout 900 "$2" "$REPO/baserom.gba" > out.log 2>&1; echo $? > rc )
}
SETS="none"; [ -f "$FE7ROM" ] && SETS="$SETS fe7"; [ $HAVE_ROMS = 1 ] && SETS="$SETS both"
for s in $SETS; do
  selftest rando_new_$s "$NEW_FULL" $s FE8PC_RANDO_CHECK &
  selftest rando_old_$s "$OLD_FULL" $s FE8PC_RANDO_CHECK &
done
P3SETS="none"; [ $HAVE_ROMS = 1 ] && P3SETS="none fe7 fe6 both"
for s in $P3SETS; do selftest promo3_$s "$NEW_FULL" $s FE8PC_PROMO3_CHECK & done
wait
for s in $SETS; do
  n=$OUT/st/rando_new_$s; o=$OUT/st/rando_old_$s
  grep '^\[rando\] self-test' "$n/out.log" > "$n/lines"; grep '^\[rando\] self-test' "$o/out.log" > "$o/lines"
  l=$(grep '\[rando\] self-test: [0-9]* configs' "$n/out.log" | tail -1)
  if [ "$(cat "$n/rc")" != 0 ] || ! echo "$l" | grep -q ', 0 failures$'; then fail "randomizer self-test ($s): rc $(cat "$n/rc"), '$l'"
  elif ! cmp -s "$n/lines" "$o/lines"; then fail "randomizer self-test ($s) differs from $BASE"; diff "$o/lines" "$n/lines" | head -4
  else echo "  randomizer self-test ($s): 0 failures, $(wc -l < "$n/lines") lines equal to $BASE"; fi
done
for s in $P3SETS; do
  n=$OUT/st/promo3_$s; l=$(grep '\[promo3\] self-test: [0-9]* ROM sets' "$n/out.log" | tail -1)
  if [ "$(cat "$n/rc")" != 0 ] || ! echo "$l" | grep -q ', 0 failures$'; then fail "third promotion self-test ($s): '$l'"
  else echo "  third promotion self-test ($s): 0 failures"; fi
done

# 4. identity routes, this build against the previous release, default settings
step "identity routes against $BASE"
INP=$REPO/port/test/inputs
route() { # NAME BIN SAVE KEYS FRAMES DUMP ROMS [ENV...]
  local n=$1 bin=$2 sav=$3 keys=$4 fr=$5 dump=$6 roms=$7; shift 7
  local T=$OUT/id/$n F7=/nonexistent-fe7.gba F6=/nonexistent-fe6.gba
  mkdir -p "$T/u" "$T/d"; cp "$sav" "$T/g.sav"; [ -f "$sav.reforged" ] && cp "$sav.reforged" "$T/g.sav.reforged"
  printf "arena_odds = 1\n" > "$T/u/reforged-options.txt"
  [ "$roms" = both ] && { F7=$FE7ROM; F6=$FE6ROM; }
  ( cd "$T" && env -u FE8PC_TEST_UNIT_CLASS -u FE8PC_FIXES_FORCE -u FE8PC_RANDO_FORCE SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
      FE8PC_PACE=0 FE8PC_USER_DIR="$T/u" FE8PC_FE7_ROM="$F7" FE8PC_FE6_ROM="$F6" FE8PC_KEYFILE="$keys" FE8PC_FRAMES=$fr \
      FE8PC_DUMP_FRAMES=$dump FE8PC_DUMP_DIR="$T/d" "$@" timeout 1800 "$bin" --save "$T/g.sav" "$REPO/baserom.gba" > out.log 2>&1 )
}
SV=$TESTDATA/saves
ROUTES="thirdtier|$SV/f_tt_start.sav|$INP/route_f_thirdtier_ch6.keys|3014|700-3014|none|
promochoice|$SV/i14_start.sav|$INP/route_f_promochoice_i14.keys|4080|1600-4080|none|
trainee|$SV/f_promo_start.sav|$INP/route_f_promo_ch6.keys|43200|39000-43200|none|
arena|$SV/ch5_start.sav|$INP/route_f_arena_ch5.keys|29100|26900-29100|none|
lordpromo|$SV/e16_start.sav|$INP/route_f_lordpromo_e16.keys|107773|101500-103500|none|
lordpromo7|$SV/e16_start.sav|$INP/route_f_lordpromo_e16_fe7.keys|109273|101500-103500|both|
thirdtier7|$SV/f_tt_start.sav|$INP/route_f_thirdtier_ch6.keys|3014|700-3014|both|
rndnone|$SV/f_tt_start.sav|$INP/route_f_thirdtier_ch6.keys|3014|700-3014|none|FE8PC_RANDO_FORCE=0x1234
rndboth|$SV/f_tt_start.sav|$INP/route_f_thirdtier_ch6.keys|3014|700-3014|both|FE8PC_RANDO_FORCE=0x1234
v18|$TESTDATA/v18/g.sav|$TESTDATA/v18/res3.keys|2352|816-2352|both|"
RUN=""
while IFS='|' read -r n sav keys fr dump roms env; do
  [ -n "$n" ] || continue
  if [ "$roms" = both ] && [ $HAVE_ROMS = 0 ]; then echo "  $n: skipped (FE7/FE6 ROMs not set)"; continue; fi
  if [ ! -f "$sav" ] || [ ! -f "$keys" ]; then fail "$n: missing $sav or $keys"; continue; fi
  route ${n}_new "$NEW_SDL" "$sav" "$keys" $fr $dump $roms $env &
  route ${n}_old "$OLD_SDL" "$sav" "$keys" $fr $dump $roms $env &
  RUN="$RUN $n"
done <<< "$ROUTES"
wait
for n in $RUN; do
  o=$OUT/id/${n}_old; w=$OUT/id/${n}_new
  nf=$(ls "$w/d" | wc -l); df=$(diff -rq "$o/d" "$w/d" | wc -l)
  ss=$(cmp -s "$o/g.sav" "$w/g.sav" && echo same || echo DIFFER)
  if [ -f "$o/g.sav.reforged" ] || [ -f "$w/g.sav.reforged" ]; then
    sc=$(cmp -s "$o/g.sav.reforged" "$w/g.sav.reforged" && echo same || echo DIFFER); else sc=none; fi
  line="$n: $df differing frames of $nf; saves $ss; side files $sc"
  if [ "$nf" -lt 100 ] || [ "$df" -ne 0 ] || [ "$ss" != same ] || [ "$sc" = DIFFER ]; then
    if allowed $n && [ "$nf" -ge 100 ]; then echo "  $line (allowed to differ)"; else fail "$line"; fi
  else echo "  $line"; fi
done

# 5. the GBA build byte-identical
step "GBA build"
nice make -j$J compare > "$OUT/gba.log" 2>&1
if tail -1 "$OUT/gba.log" | grep -q 'fireemblem8.gba: OK'; then echo "  fireemblem8.gba: OK"; else fail "GBA build ($OUT/gba.log): $(tail -1 "$OUT/gba.log")"; fi

# 6. optional: the save checks against mGBA
if [ $FULL = 1 ]; then
  step "save checks (229, against mGBA)"
  python3 port/test/tools/savechk_all.py --parts p53,p53b,p54,rfo,late --out "$OUT/savechk" > "$OUT/savechk.log" 2>&1 < /dev/null
  rc=$?; tail -1 "$OUT/savechk.log" | sed 's/^/  /'
  [ $rc = 0 ] || { grep '^FAIL' "$OUT/savechk.log" | head; fail "save checks"; }
fi

MIN=$(( ($(date +%s) - START + 59) / 60 ))
if [ $bad = 0 ]; then
  echo "$(git rev-parse HEAD) $(git diff HEAD -- . ':!build' | sha1sum | cut -c1-40) $BASE $FULL" > build/quickcheck.stamp
  echo "QUICKCHECK: PASS ($MIN min)"
else
  rm -f build/quickcheck.stamp
  echo "QUICKCHECK: FAIL ($MIN min; logs in $OUT)"
fi
exit $bad
