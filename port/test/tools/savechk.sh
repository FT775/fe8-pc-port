#!/bin/bash
# Save checks P5.3 and P5.4 (port/test/README.md, "Save checks"): play a chain of key files, one
# session each, every session booting from the .sav the one before it left, and keep that .sav
# after each session. Run from the repository root.
#
#   port/test/tools/savechk.sh pc  full|sdl|win OUTDIR START.sav SESSION.keys...
#   port/test/tools/savechk.sh gba OUTDIR START.sav SESSION.keys...
#
# pc:  a played build (build/pc-full/fe8pc headless, build/pc-sdl/fe8pc-sdl or
#      build/win32-sdl/fe8pc-sdl.exe under SDL's offscreen video and dummy audio, the latter run
#      through WSL's Windows interop as check-all's win-sdl-60000 row is), the keys given as
#      FE8PC_KEYSCRIPT (tools/keys_to_keyscript.py), FE8PC_FRAMES = the key file's ticks,
#      --save FE8.sav beside a copy of baserom.gba named FE8.gba. The .sav is the port's own
#      file (savefile_pc.c), written as the game writes SRAM.
#      SAVECHK_RFO="key = value\n..." (full and sdl only) writes reforged-options.txt into the
#      run's own per-user folder first (FE8PC_USER_DIR, or XDG_DATA_HOME/fe8pc), and the sidecar
#      FE8.sav.reforged is kept after each session too. The win build reads its settings from
#      %APPDATA%\fe8pc, which this script does not touch, so SAVECHK_RFO is refused there.
# gba: mGBA 0.10.5 (build/gate0/mgba_lua_run, oracle/sram_capture.lua with SRAM_AT=tick:T)
#      booting from the previous .sav (GATE0_SRAM_IN), the key file as it is.
#
# A session may be given as SESSION.keys+VAR=VALUE[+VAR=VALUE...] (full and sdl only): those
# variables are set for that session's run alone, e.g. +FE8PC_RFO_TEST_FALL=4 (a test switch of the
# played builds, port/reforged/reforged_casual.c; the save check with options on, savechk_all.py).
# A gba or win session with one stops the script (exit 2).
#
# OUTDIR/NN_NAME.sav (and .log, .reforged) per session; OUTDIR/00_start.sav is the start.
# Saves are game data: keep OUTDIR outside git.
#
# A session that fails stops the chain with exit 1: a non-zero exit (a crash, the 900 s timeout,
# a failed sram-capture), or, on pc, a log without "[frame] frame limit T reached" and
# "exit 0: frames=T" for the key file's T ticks. A pc session that dies early leaves its input
# .sav as it was, which would otherwise look like a session that changed nothing. Its file is
# kept as OUTDIR/NN_NAME.failed.sav (not used as the next session's start).
set -u
here=$(pwd)
mode=$1; shift
if [ "$mode" = pc ]; then build=$1; shift; fi
out=$(realpath -m "$1"); start=$2; shift 2
mkdir -p "$out"
cp "$start" "$out/00_start.sav"
rm -f "$out/00_start.sav.reforged"
[ -f "$start.reforged" ] && cp "$start.reforged" "$out/00_start.sav.reforged"
prev="$out/00_start.sav"
n=0
work=$(mktemp -d "${TMPDIR:-/tmp}/savechk.XXXXXX")
for arg in "$@"; do
    n=$((n + 1))
    keys=${arg%%+*}
    senv=()
    [ "$keys" = "$arg" ] || IFS=+ read -r -a senv <<< "${arg#*+}"
    if [ ${#senv[@]} -gt 0 ] && [ "$mode" = gba -o "${build:-}" = win ]; then
        echo "$arg: session variables are for the full and sdl builds" >&2; exit 2
    fi
    name=$(printf '%02d_%s' $n "$(basename "$keys" .keys)")
    ticks=$(python3 -I port/test/tools/keys_to_keyscript.py --ticks "$keys")
    if [ "$mode" = gba ]; then
        tag="savechk_$$_$n"
        make -s -f port/test/Makefile sram-capture SAVE_KEYS="$keys" SAVE_AT=tick:$ticks SAVE_NAME=$tag \
            GATE0_SRAM_IN="$prev" > "$out/$name.log" 2>&1
        rc=$?
        rm -f "$out/$name.sav" "$out/$name.failed.sav"
        mv -f port/test/saves/$tag.sav "$out/$name.sav" 2>/dev/null
        rm -f port/test/saves/$tag.sav.json
        why=""
        [ $rc -ne 0 ] && why="sram-capture exit $rc"
    else
        ks=$(python3 -I port/test/tools/keys_to_keyscript.py "$keys") || exit 2
        rm -rf "$work/run"; mkdir -p "$work/run/user/fe8pc"
        cp baserom.gba "$work/run/FE8.gba"
        cp "$prev" "$work/run/FE8.sav"
        [ -f "$prev.reforged" ] && cp "$prev.reforged" "$work/run/FE8.sav.reforged"
        if [ -n "${SAVECHK_RFO:-}" ]; then
            [ "$build" = win ] && { echo "SAVECHK_RFO: not for the win build" >&2; exit 2; }
            printf '%b\n' "$SAVECHK_RFO" > "$work/run/user/reforged-options.txt"
            cp "$work/run/user/reforged-options.txt" "$work/run/user/fe8pc/"
        fi
        case $build in
            full) (cd "$work/run" && env "${senv[@]}" FE8PC_USER_DIR="$work/run/user/" FE8PC_KEYSCRIPT="$ks" FE8PC_FRAMES=$ticks \
                    timeout 900 "$here/build/pc-full/fe8pc" --save FE8.sav FE8.gba) > "$out/$name.log" 2>&1 ;;
            sdl)  (cd "$work/run" && env "${senv[@]}" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy FE8PC_PACE=0 \
                    XDG_DATA_HOME="$work/run/user" FE8PC_KEYSCRIPT="$ks" FE8PC_FRAMES=$ticks \
                    timeout 900 "$here/build/pc-sdl/fe8pc-sdl" --save FE8.sav FE8.gba) > "$out/$name.log" 2>&1 ;;
            win)  cp "$here"/build/win32-sdl/*.dll "$work/run/" 2>/dev/null
                  cp "$here/build/win32-sdl/fe8pc-sdl.exe" "$work/run/"
                  (cd "$work/run" && SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy FE8PC_PACE=0 \
                    FE8PC_KEYSCRIPT="$ks" FE8PC_FRAMES=$ticks SDL_JOYSTICK_RAWINPUT=0 SDL_JOYSTICK_HIDAPI=0 \
                    SDL_JOYSTICK_WGI=0 SDL_JOYSTICK_GAMEINPUT=0 SDL_XINPUT_ENABLED=0 SDL_JOYSTICK_DIRECTINPUT=0 \
                    WSLENV=SDL_VIDEO_DRIVER:SDL_AUDIO_DRIVER:FE8PC_PACE:FE8PC_FRAMES:FE8PC_KEYSCRIPT:SDL_JOYSTICK_RAWINPUT:SDL_JOYSTICK_HIDAPI:SDL_JOYSTICK_WGI:SDL_JOYSTICK_GAMEINPUT:SDL_XINPUT_ENABLED:SDL_JOYSTICK_DIRECTINPUT \
                    timeout 900 ./fe8pc-sdl.exe --save FE8.sav FE8.gba) > "$out/$name.log" 2>&1 ;;
            *) echo "build: full, sdl or win" >&2; exit 2 ;;
        esac
        rc=$?
        rm -f "$out/$name.sav" "$out/$name.failed.sav"
        cp "$work/run/FE8.sav" "$out/$name.sav"
        rm -f "$out/$name.sav.reforged"
        [ -f "$work/run/FE8.sav.reforged" ] && cp "$work/run/FE8.sav.reforged" "$out/$name.sav.reforged"
        why=""
        if [ $rc -ne 0 ]; then why="exit $rc"
        elif ! grep -q "^\[frame\] frame limit $ticks reached" "$out/$name.log"; then why="no \"frame limit $ticks reached\" in the log"
        elif ! grep -q "exit 0: frames=$ticks " "$out/$name.log"; then why="no \"exit 0: frames=$ticks\" in the log"
        fi
    fi
    onmain=$(grep -o 'OnMain calls=[0-9]*' "$out/$name.log" 2>/dev/null | tail -n1)
    echo "$name: rc $rc, $(stat -c %s "$out/$name.sav" 2>/dev/null || echo no) bytes${onmain:+, $onmain}"
    [ -f "$out/$name.sav" ] || why=${why:-"no .sav written"}
    if [ -n "$why" ]; then
        [ -f "$out/$name.sav" ] && mv -f "$out/$name.sav" "$out/$name.failed.sav"
        echo "$name: FAILED ($why); log $out/$name.log" >&2
        rm -rf "$work"
        exit 1
    fi
    prev="$out/$name.sav"
done
rm -rf "$work"
