#!/bin/bash
# "Enhanced sound" check (v1.32.0; port/audio/hq_pc.c): the played Linux window build, offscreen, on the
# thirdtier route (Chapter 6, 3,014 frames, map music and battles):
#
#   port/tools/soundcheck.sh [--keep] [--wav DIR]
#
# 1. the Display screen's "Sound" row Original (nothing mixed again) against Enhanced with FE8PC_AUDIO_DUMP_HQ:
#    the same 240x160 frames, the same .sav, the same original sound stream (FE8PC_AUDIO_DUMP's .lr16 and the
#    FIFO bytes): the game and its own sound do not change;
# 2. the enhanced stream against the original (port/tools/soundcheck.py): each second's level within 4 dB and,
#    low-passed and taken every 4th frame, correlated above 0.8 with the original (the same notes, in time),
#    no clipped samples; with --wav, both streams as WAV files in DIR.
# Prints "SOUNDCHECK: PASS" only when both passed. Data paths: ~/.config/fe8pc-release.conf (TESTDATA).
cd "$(git rev-parse --show-toplevel)" || exit 1
REPO=$PWD
CONF=${FE8PC_RELEASE_CONF:-$HOME/.config/fe8pc-release.conf}
[ -f "$CONF" ] && . "$CONF"
TESTDATA=${TESTDATA:-$HOME/fe8-testdata}
KEEP=0; WAV=""
while [ $# -gt 0 ]; do
  case $1 in
    --keep) KEEP=1;;
    --wav) WAV=$2; shift;;
    *) echo "usage: soundcheck.sh [--keep] [--wav DIR]"; exit 2;;
  esac
  shift
done
OUT=$HOME/sc; rm -rf "$OUT"; mkdir -p "$OUT"
START=$(date +%s)
J=$(nproc)
echo "[$(date +%H:%M:%S)] building linux sdl"
nice make -f port/Makefile.sdl -j$J > "$OUT/sdl.log" 2>&1 || { echo "FAIL: linux sdl build ($OUT/sdl.log)"; echo "SOUNDCHECK: FAIL"; exit 1; }
BIN=$REPO/build/pc-sdl/fe8pc-sdl
SAV=$TESTDATA/saves/f_tt_start.sav KEYS=$REPO/port/test/inputs/route_f_thirdtier_ch6.keys FR=3014
[ -f "$SAV" ] || { echo "FAIL: no $SAV"; echo "SOUNDCHECK: FAIL"; exit 1; }
run() { # NAME ROW HQDUMP
  local T=$OUT/$1
  mkdir -p "$T/u" "$T/d"; cp "$SAV" "$T/g.sav"
  printf "enhanced_sound = %s\n" "$2" > "$T/u/reforged-options.txt"
  ( cd "$T" && env -u FE8PC_TEST_UNIT_CLASS -u FE8PC_FIXES_FORCE -u FE8PC_RANDO_FORCE ${3:+FE8PC_AUDIO_DUMP_HQ=$T/hq} \
      SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy FE8PC_PACE=0 FE8PC_USER_DIR="$T/u" FE8PC_FE7_ROM=/nonexistent \
      FE8PC_FE6_ROM=/nonexistent FE8PC_KEYFILE="$KEYS" FE8PC_FRAMES=$FR FE8PC_DUMP_FRAMES=0-$FR FE8PC_DUMP_DIR="$T/d" \
      FE8PC_AUDIO_DUMP="$T/orig" timeout 1200 "$BIN" --save "$T/g.sav" "$REPO/baserom.gba" > out.log 2>&1 )
}
echo "[$(date +%H:%M:%S)] thirdtier, Sound Original and Enhanced"
run off 0 & run on 1 1 & wait
bad=0
nf=$(ls "$OUT/off/d" | wc -l); df=$(diff -rq "$OUT/off/d" "$OUT/on/d" | wc -l)
ss=$(cmp -s "$OUT/off/g.sav" "$OUT/on/g.sav" && echo same || echo DIFFER)
so=$(cmp -s "$OUT/off/orig.lr16" "$OUT/on/orig.lr16" && cmp -s "$OUT/off/orig.fifo8" "$OUT/on/orig.fifo8" && echo same || echo DIFFER)
echo "  $df differing frames of $nf; saves $ss; the original sound $so"
[ "$nf" -ge 3000 ] && [ "$df" -eq 0 ] && [ "$ss" = same ] && [ "$so" = same ] || { echo "FAIL: the game or its own sound changed"; bad=1; }
grep -q 'stream now 53516 Hz' "$OUT/on/out.log" || { echo "FAIL: the audio stream did not take 53,516 Hz"; bad=1; }
[ -n "$WAV" ] && mkdir -p "$WAV"
python3 port/tools/soundcheck.py "$OUT/on/orig.lr16" "$OUT/on/hq.lr16" $WAV | tail -2 | sed 's/^/  /'
[ ${PIPESTATUS[0]} -eq 0 ] || { echo "FAIL: the enhanced stream does not match the original"; bad=1; }
[ $KEEP = 1 ] || rm -rf "$OUT"/off/d "$OUT"/on/d
if [ $bad = 0 ]; then echo "SOUNDCHECK: PASS ($(( ($(date +%s) - START + 59) / 60 )) min)"; else echo "SOUNDCHECK: FAIL (logs in $OUT)"; exit 1; fi
