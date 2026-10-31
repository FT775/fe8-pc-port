# The Windows builds

Two Windows programs are built from this tree, both 32-bit, both cross-compiled
in WSL with MinGW-w64, both the D1-a layout of the Linux builds (GBA memory at
GBA addresses, code at 0x10000000):

- `fe8pc-sdl.exe`, **the game in a window** (`make -f port/Makefile.mingw SDL=1`):
  the Windows twin of the Linux window build (`port/Makefile.sdl`), with
  keyboard, gamepads, sound and save files. This is the one to play.
- `fe8pc.exe`, headless (`make -f port/Makefile.mingw`): no window, no sound,
  no live keys; for tests (`pending/merged/windows-build.md`).

## Playing (fe8pc-sdl.exe)

1. Copy these three files of `build/win32-sdl/` into a folder of your own, for
   example `C:\Games\FE8\`: `fe8pc-sdl.exe`, `SDL3.dll` (the exe needs it beside
   it) and `LICENSE-SDL3.txt`. (`fe8pc-sdl.debug` is the exe's debug
   information, for developers; playing does not need it.)
2. Put your Fire Emblem: The Sacred Stones (USA) ROM file in the same folder.
   Any file name ending in `.gba` works (`baserom.gba` is taken first when there
   are several). The game is not included: the port runs the ROM of your own
   cartridge. It must be the original ROM, 16 MiB with SHA-1
   `c25b145e37456171ada4b0d440bf88a19f4d509f`: a copy cut short or changed (a
   hack, a translation, a patch) is refused before the game starts, and the
   message box says which it is. Of several `.gba` files, the original is taken.
3. Double-click `fe8pc-sdl.exe`. Or drop a ROM file onto it, or start it from a
   terminal: `fe8pc-sdl.exe [--save PATH | --no-save] [--console] [ROM]`.

Keys (by key position, so the keyboard layout does not matter):

| GBA | Keyboard | Gamepad (any SDL3 gamepad, plugged in at any time) |
|---|---|---|
| A | Z | the button labelled A (cross on PlayStation pads; on Nintendo pads the right one) |
| B | X | the button labelled B (circle; on Nintendo pads the bottom one) |
| L | Q | left shoulder or left trigger |
| R | E | right shoulder or right trigger |
| Start | Enter | Start (Menu, Options, Plus) |
| Select | Backspace | Back (View, Share or Create, Minus) |
| D-pad | arrow keys | D-pad, or the left stick past half way |
| (fast-forward, held) | Space | the right stick's click (pressed in) |

F11 switches full screen. Closing the window quits (and writes the save).
Opposite directions held together count as neither, as on a real D-pad. The
picture is scaled by a whole number with sharp pixels; the window starts at
3 times the GBA's size (720 x 480, smaller if the screen is) and can be resized
(`FE8PC_SCALE=1` to `10` sets the start size).

Fast-forward (Reforged Options): hold Space, or press in the right stick, and
the game runs faster for as long as it is held: 4 times by default, muted. In
Extras > Reforged Options, "Fast-forward" turns it off, "Fast-forward speed"
picks 2x, 3x, 4x or 8x, and "Fast-forward sound" picks Mute or Sped up (8x is
always muted). It changes only the speed, never the game, and neither key is a
GBA button. The played exe only: a test build (`FE8PC_TESTBUILD=1`) has no
fast-forward (`port/sdl3/platform_sdl3.c`, `port/reforged/reforged_fastforward.c`).

Saves (port-plan/DECISIONS.md, 2026-10-20 afternoon):

- **Next to the ROM**, with the ROM's name and `.sav` (`FE8.gba` -> `FE8.sav`), the
  raw 32 KiB SRAM image mGBA uses, so a `.sav` from mGBA loads and the other way
  round. An empty `FE8.sav.lock` stays beside it.
- **When that folder cannot be written** (a read-only folder, a CD): in
  `%APPDATA%\fe8pc\` (`C:\Users\<you>\AppData\Roaming\fe8pc\`), and the window
  title says so. That folder is made only then: a game that saves next to the
  ROM leaves no empty folder there. A save in both places, a read-only `.sav`,
  a second copy of the game on the same save, or a save changed by another
  program while the game runs are all named in the window title (and the log)
  and never overwritten silently.
- `--save PATH` uses PATH instead; `--no-save` keeps none.

The log: the exe has no console window. It writes its log to `fe8pc-sdl.log`
next to the exe (or in `%APPDATA%\fe8pc\` when that folder cannot be written; a
second copy running at the same time writes `fe8pc-sdl-<process id>.log`); the
first line says where. `--console` prints it in the terminal it was started
from instead. When something stops the game (no ROM found, a ROM that is not
the original FE8 USA ROM, `SDL3.dll` missing, a bad command line, an internal
error), a message box says what, with the last lines of the log and where the
whole log is.

## Building (in WSL, from the repository root)

```sh
make -f make_tools.mk -j6 && make -j6       # the GBA build (fireemblem8.gba: OK), the address oracle
make -f port/Makefile.mingw -j6 SDL=1       # build/win32-sdl/fe8pc-sdl.exe + SDL3.dll + LICENSE-SDL3.txt
                                            # (+ fe8pc-sdl.debug, its debug information)
make -f port/Makefile.mingw -j6 SDL=1 FE8PC_TESTBUILD=1   # the test build, build/win32-sdl-test
build/win32-sdl/fe8pc-sdl.exe baserom.gba   # from WSL (Windows interop); it opens the window
```

Needs: `gcc-mingw-w64-i686` (with its `windres` and `dlltool`), and SDL3 for
MinGW: the `i686-w64-mingw32` folder of the official `SDL3-devel-3.4.2-mingw.tar.gz`
from the libsdl-org GitHub release (SDL 3.4.2, the version of Ubuntu's
`libsdl3-dev:i386`, which the Linux window build uses), by default
`~/third-party/SDL3-3.4.2-mingw/extracted/SDL3-3.4.2/i686-w64-mingw32`;
`SDL3_MINGW_DIR=...` names another one. SDL's files are never committed.

From WSL, settings reach the exe through WSLENV, for example
`SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy FE8PC_FRAMES=600 WSLENV=SDL_VIDEO_DRIVER:SDL_AUDIO_DRIVER:FE8PC_FRAMES build/win32-sdl/fe8pc-sdl.exe --no-save baserom.gba`
runs 600 frames with no window and no sound. The exe still reads the
machine's gamepads then; a run that must not see them also turns off SDL's
joystick drivers for real devices: `SDL_JOYSTICK_RAWINPUT=0
SDL_JOYSTICK_HIDAPI=0 SDL_JOYSTICK_WGI=0 SDL_JOYSTICK_GAMEINPUT=0
SDL_XINPUT_ENABLED=0 SDL_JOYSTICK_DIRECTINPUT=0`, each also named in WSLENV
(`WIN_SDL_NOPADS` in `port/check-all.mk`; SDL's virtual gamepads still work).

## What is Windows-specific, and why

`port/Makefile.mingw` (SDL=1) builds the same game and port objects as the
Linux window build, with the same ABI flags, linker script, RAM-address check
(B-12, 617 symbols) and PE step as `fe8pc.exe`. On top of that:

- **SDL3.dll is loaded late.** A plain import of SDL3.dll loads it, and the
  `user32.dll` and `gdi32.dll` it imports, before `main()`. Their start-up
  mappings (about 20 MB at the bottom of the address space) took
  0x02000000-0x04000FFF, so EWRAM, IWRAM and IO could not be reserved (the first
  build failed every start with `IO RESERVATION FAILED`). The exe is therefore
  linked against a delay-load import library made from SDL's own (`dlltool`):
  SDL3.dll loads at the first SDL call, after `port/hw/memory.c` has reserved the
  GBA map. The exe itself imports only `KERNEL32.dll` and `msvcrt.dll`; the
  build fails if it imports anything else (`IMPORT_CHECK`), and
  `port/win32/gui/gui_win32.c` and the test hooks load `user32` by name. A
  missing or too old SDL3.dll is then reported by a message box (the
  delay-load exception), not by Windows' own dialog.
- **A GUI-subsystem exe** (`-mwindows`), so a double-click opens no console;
  `port/win32/gui/gui_win32.c` routes the log (above) and shows the message
  boxes (`--wrap=exit`, `--wrap=_exit`, and calls from `posix_win32.c`'s
  `__wrap_main`, all under `FE8PC_WINGUI`; the headless exe has none of it).
  Started with its output going somewhere (WSL, a redirect), it shows no box,
  so a test never waits on one.
- **A manifest** (`port/win32/gui/fe8pc-sdl.manifest.xml`; the repository's
  `.gitignore` ignores `*.manifest`) sets the UTF-8 code page
  (Windows 10 1903 and later), so `fopen` (the ROM, the save checks) and SDL's
  file calls (the save writes) agree on file names that are not ASCII.
- **The fault handler and SDL's threads.** `posix_win32.c`'s vectored exception
  handler now takes only the game's faults: in the exe, a jump to NULL or into
  one of the GBA regions `memory.c` reserved (not the gaps between them, where
  host memory can be, executable code included), or to an address that is not
  mapped or not executable. A
  fault in a DLL (SDL3.dll, a graphics driver) or a data fault in code outside
  every module (a driver's generated code, an overlay's hook) goes to that
  code's own exception handling first, as before for system DLLs; anything
  left unhandled is still reported. The BIOS-area write drop (H-1) works as on
  Linux (same site, frame 8,349, in the window build).
- **Debug information beside the exe.** The release `fe8pc-sdl.exe` keeps only
  its COFF symbol table (function names); its DWARF debug information (about
  9 MB) is in `fe8pc-sdl.debug`, which the exe names for gdb. A fault report
  in the log gives addresses at the exe's fixed base and the build's link time
  stamp. With the exe alone, `i686-w64-mingw32-nm -n fe8pc-sdl.exe` names the
  function (the last function symbol at or below ADDR). `addr2line` reads only
  the DWARF information, so it needs the `.debug` of the same build:
  `i686-w64-mingw32-addr2line -f -e fe8pc-sdl.exe ADDR` with `fe8pc-sdl.debug`
  beside the exe (it follows the exe's debug link), or `-e fe8pc-sdl.debug`,
  gives the function and the source line; with the exe alone it prints
  `?? ??:?`. So keep each build's `.debug` with its exe. The exe and its
  `.debug` carry the same stamp, the link's (`objdump -p`, "Time/Date";
  `pe_gba.py stamp`), so a report's stamp names its `.debug`; the build checks
  it (before 2026-10-22 the two could be 1 s apart). Not stripped further: an
  exe without the symbol table too was quarantined by Malwarebytes' machine
  learning as soon as it was copied to a Windows folder ("Anomalous").
- **The same file names twice.** `savefile_pc.c` tells whether two save paths
  are one file by the volume serial number and file index on Windows
  (`PortWin32SameFile`), so letter case or `/` against `\` no longer counts as
  two files.
- **Timing**: `SDL_DelayPrecise` (a high-resolution waitable timer, then a short
  spin) and `SDL_GetTicksNS` (QueryPerformanceCounter). SDL makes the window
  per-monitor DPI aware, so Windows never stretches the picture.

### SDL's structs

SDL3.dll is built with MinGW's default struct layout (`-mms-bitfields`,
`-malign-double`); the port's objects use the Linux i386 layout (`ABI_FLAGS`),
which the game needs. Compiling all of SDL's headers both ways and comparing
them with `abi_check.py` gives 145 structs, 30 of which differ: most event
structs only in their trailing padding (inside the 128-byte `SDL_Event`), and
`SDL_PathInfo`, `SDL_InitState` and the sensor events' `sensor_timestamp` in
member offsets. The members the window build shares with SDL (`SDL_Event`'s
type, key and gamepad-device fields, `SDL_AudioSpec`, `SDL_Rect`, `SDL_Surface`,
`SDL_VirtualJoystickDesc`) have the same offsets both ways; compile-time checks
in `port/sdl3/platform_sdl3.c` hold them there. A new SDL struct needs the same
check before the Windows build uses it.

## Checks

- check-all (`port/check-all.mk`): `win-sdl` (the build: 0 warnings, RAM
  addresses, no SDL stub, the import check, the loader layout again after the
  debug information is taken out), `abi-sdl` (every struct layout of
  the Linux and Windows window builds, SDL's included, read from
  `fe8pc-sdl.debug`) and `win-sdl-60000` (the
  exe run on Windows through WSL's interop with SDL's offscreen video and dummy
  audio drivers and no real gamepads: 60,000 frames of attract mode to the end state of
  `attract-60000`). Each is skipped, with a note, without MinGW, without SDL3 for
  MinGW, or without Windows interop.
- Test builds (`FE8PC_TESTBUILD=1`, Linux and Windows) have the window build's
  test hooks, `port/sdl3/test/sdl_testhook.c` (a release build has none of them):
  `FE8PC_TEST_KEYS` (a Gate 0 key file), `FE8PC_TEST_FRAMEHASH` (a hash of every
  frame given to SDL and of what SDL drew), `FE8PC_TEST_FRAMESTATS` (frame
  times), `FE8PC_TEST_VPAD` (an SDL virtual gamepad with a script),
  `FE8PC_TEST_WINKEYS` (Windows key messages posted to a hidden window),
  `FE8PC_TEST_HIDDEN`, `FE8PC_TEST_AUDIO_GAIN`, `FE8PC_TEST_INFO`. With them the
  Linux and Windows window builds were compared frame by frame and sample by
  sample (port-plan/pending/folded/win9.md).

## Known limits

- Dragging or resizing the window holds the game while the mouse button is down
  (Windows runs its own message loop then, inside SDL's event poll); the pacer
  starts a new schedule afterwards rather than catching up, and the sound
  refills. Not checked by hand yet.
- On a screen scaled to 150%, the 720 x 480 start window looks smaller than
  other windows (it is 720 x 480 real pixels); resize it, use F11, or set
  `FE8PC_SCALE`.
- The first start of an audio device can take seconds (2.7 s for the Oculus
  virtual audio device here); the pacer then logs one schedule restart at
  frame 1.
