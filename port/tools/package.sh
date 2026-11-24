#!/bin/bash
# Package the Windows release from the committed tree's build: the program (exe, SDL3.dll, its
# licence) and the player docs. Never a ROM, a save, a log or a backup. Usage: package.sh VERSION
# The zip goes to PKG_OUTDIR, or WIN_ROOT/releases from ~/.config/fe8pc-release.conf (never in git).
set -e
V=${1:?version}
cd "$(git rev-parse --show-toplevel)"
CONF=${FE8PC_RELEASE_CONF:-$HOME/.config/fe8pc-release.conf}
[ -f "$CONF" ] && . "$CONF"
# tracked files must be committed (PKG_TEST=1: a trial package of the working tree); untracked files are not packaged
[ -n "$PKG_TEST" ] || [ -z "$(git status --short --untracked-files=no)" ] || { echo "tree not clean"; exit 1; }
OUTDIR="${PKG_OUTDIR:-${WIN_ROOT:?set WIN_ROOT in $CONF or PKG_OUTDIR}/releases}"
NAME="FE8-PC-Port-$V-Windows"
STAGE=$(mktemp -d)/$NAME
mkdir -p "$STAGE/docs" "$STAGE/roms"
cp build/win32-sdl/fe8pc-sdl.exe build/win32-sdl/SDL3.dll "$STAGE/"
cp build/win32-sdl/LICENSE-SDL3.txt "$STAGE/" 2>/dev/null || cp "$(ls -d /usr/*/*SDL3*/share/licenses/SDL3/LICENSE.txt 2>/dev/null | head -1)" "$STAGE/LICENSE-SDL3.txt"
for f in PLAYING.md CHANGELOG.md PATCH-NOTES.md ACCURACY.md CREDITS.md LICENSE-PORT.md; do cp docs/port/$f "$STAGE/docs/"; done
cp LICENSE "$STAGE/LICENSE.txt"; cp LICENSE-PORT-FILES.txt "$STAGE/"
cat > "$STAGE/README.txt" <<EOF
Fire Emblem: The Sacred Stones - PC port, $V (Windows)
=========================================================

This is a native Windows version of the GBA game, built from the community
decompilation. The game itself is not included: you need your own USA ROM.

  Fire Emblem: The Sacred Stones (USA), a .gba file, 16 MB
  SHA-1 c25b145e37456171ada4b0d440bf88a19f4d509f
  (hacks, translations and other versions are refused)

To play
-------
1. Unzip this folder anywhere. Keep fe8pc-sdl.exe and SDL3.dll together.
2. Put your .gba file in the "roms" folder (see roms\PUT YOUR ROMS HERE.txt),
   or next to fe8pc-sdl.exe, or drop it onto fe8pc-sdl.exe.
3. Double-click fe8pc-sdl.exe. If it finds no ROM, it asks you to choose
   the file and remembers where it is (the ROM is never copied).
Saves are in the same format as mGBA's .sav. With the ROM in "roms" or chosen
in the window, the save stays next to fe8pc-sdl.exe; with the ROM next to the
exe or dropped onto it, the save is next to the ROM, as before.

Controls (default): arrows move, Z = A, X = B, Enter = Start, Backspace =
Select, Q = L, E = R, hold Space = fast-forward, F11 = fullscreen. Gamepads
work too. Change them in Reforged Options > Controls.

Reforged Options (title screen: Extras) holds the PC extras: fast-forward,
danger zone, HP bars, casual mode, cheats, a seeded randomizer, optional rule
fixes, controls and display settings. During a chapter, the map menu's
"Reforged" entry changes the options that are safe mid-run.

What's new in $V: docs/CHANGELOG.md
Everything else (options, saves, troubleshooting): docs/PLAYING.md

Fire Emblem: The Sacred Stones is by Nintendo and Intelligent Systems.
The licences of this port and of SDL are in LICENSE*.txt and docs/.
EOF
cat > "$STAGE/roms/PUT YOUR ROMS HERE.txt" <<'EOF'
Put your Fire Emblem ROM files in this folder
=============================================

The game looks here for your ROMs (and also next to fe8pc-sdl.exe, as before).
Any file names work: the game recognises each ROM by its contents.

Required
--------
Fire Emblem: The Sacred Stones (USA), a .gba file of 16 MB.
  SHA-1 c25b145e37456171ada4b0d440bf88a19f4d509f
  Hacks, translations, other regions and cut-short copies are refused.

Optional
--------
Fire Emblem (USA), the GBA game known as FE7: adds its Nomad, Blade Lord,
  Armor Lord, Archsage, Dark Druid and Bard classes (see docs\PLAYING.md,
  "FE7 classes").
Fire Emblem: The Binding Blade (the Japanese GBA game known as FE6, or a fan
  translation of it): adds its female Thief, Trooper, King, Lion Lord, Master
  Lord and Divine Dragon classes, and the "Myrrh look" option (see
  docs\PLAYING.md, "FE6 classes").
Without them the game is complete; those classes are simply absent.

Use your own copies, made from your own cartridges. ROMs are never included
with this port and must never be shared.

If no Sacred Stones ROM is here or next to the exe, the game asks you to
choose the file when it starts and remembers where it is. Saves go next to
the ROM (a save already next to fe8pc-sdl.exe keeps being used).
EOF
( cd "$STAGE" && sha256sum fe8pc-sdl.exe SDL3.dll > SHA256SUMS.txt )
# nothing but the intended files
find "$STAGE" -type f | sed "s|$STAGE/||" | sort
if find "$STAGE" -iname '*.gba' -o -iname '*.sav*' -o -iname '*.log' | grep -q .; then echo "REFUSED: game data in the package"; exit 1; fi
mkdir -p "$OUTDIR"
rm -f "$OUTDIR/$NAME.zip"
python3 -I - "$STAGE" "$OUTDIR/$NAME.zip" <<'PYEOF'
import os, sys, zipfile
stage, out = sys.argv[1], sys.argv[2]
base = os.path.dirname(stage)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _, files in os.walk(stage):
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, os.path.relpath(p, base))
PYEOF
ls -la "$OUTDIR/$NAME.zip"; sha256sum "$OUTDIR/$NAME.zip" | cut -c1-64
echo "zip contents:"; python3 -I -c 'import sys, zipfile; [print("  %8d  %s" % (i.file_size, i.filename)) for i in zipfile.ZipFile(sys.argv[1]).infolist()]' "$OUTDIR/$NAME.zip"
