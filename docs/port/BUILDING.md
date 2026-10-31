# Building the PC port

> Commands are from the port's Makefiles and the port's own READMEs.

## Requirements

| Need | For | Notes |
|---|---|---|
| WSL2 with Ubuntu (tested on Ubuntu 26.04) or native Linux | Everything | The development tree is built in WSL. Native Linux should work with the same packages; it is untested. |
| gcc with 32-bit (`-m32`) support, make, python3 | The PC builds | The PC build is 32-bit. See "Packages" below. |
| Python modules numpy and Pillow (`python3-numpy`, `python3-pil`) | The GBA build, which every PC build needs first | See "Python" below. Every other build script uses only Python's standard library. |
| The decomp's build tools, including agbcc | The GBA build | Run `./build_tools.sh` once in a fresh tree. agbcc is pret/agbcc at commit `da598c1d`. |
| SDL3 3.4.2, i386 (`libsdl3-dev:i386` on Ubuntu) | The Linux window build | Linked, not shipped. |
| `gcc-mingw-w64-i686` (with `windres` and `dlltool`) | The Windows builds | Cross-compiled in WSL. |
| SDL3 3.4.2, MinGW development files (the `i686-w64-mingw32` folder of the official `SDL3-devel-3.4.2-mingw.tar.gz`) | The Windows window build | Set `SDL3_MINGW_DIR` if the folder is not in the default place. |
| mGBA 0.10.5 library and headers (`libmgba0.10t64`, `libmgba-dev`), Lua 5.4 | The test harness | Built with scripting enabled. |
| python3-unicorn 2.1.4 | Two check-all rows: `text-difftest` and `m4a-difftest` | Test tool. Not shipped. Without it those two rows fail (they are not skipped). |
| Your ROM, named `baserom.gba` in the tree | Tests | Never committed. |

### Packages

**Ubuntu 26.04 (tested: the development machine has exactly these).** It needs the 32-bit SDL3, not the 64-bit one.

```sh
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install -y build-essential gcc-multilib pkg-config python3 \
  python3-numpy python3-pil \
  libsdl3-dev:i386 \
  gcc-mingw-w64-i686 binutils-mingw-w64-i686 mingw-w64-i686-dev \
  binutils-arm-none-eabi
```

`port/Makefile.sdl` finds the 32-bit SDL3 by itself (it sets `PKG_CONFIG_LIBDIR` to the i386 folder). If it says "no 32-bit SDL3 found", `libsdl3-dev:i386` is missing.

For the Windows build, unpack the official `SDL3-devel-3.4.2-mingw.tar.gz` from SDL's GitHub releases. The default place is `~/third-party/SDL3-3.4.2-mingw/extracted/`; or set `SDL3_MINGW_DIR` to its `i686-w64-mingw32` folder. Use **3.4.2**: the port's compile-time checks hold SDL's struct layouts to that version.

**Ubuntu 24.04, Debian 12 and older (untested).** No SDL3 package exists there, so SDL3 3.4.2 has to be built for i386 from source. The rest of the package list is the same. A CMake build with `-DCMAKE_C_FLAGS=-m32` and the `:i386` X11/Wayland/audio dev packages may work; nobody has tried it yet. Only Ubuntu 26.04 is tested.

### Python

The Makefiles run Python as `PYTHON ?= python3` (the development machine has Python 3.14.4). Checked on 2026-10-30 (`port-rfo-int5` `935e2576`) by listing every `.py` script a Makefile or `.mk` file runs, with the scripts those import, 45 in all, and reading their `import` lines:

- **The GBA build** (the decomp's own scripts in `scripts/`) needs **numpy** and **Pillow** (`PIL`): `scripts/gfxtools/tsa_generator.py` and `tsa2.py` import both, and the root `Makefile` runs them (`FETSATOOL`) to make the `.feimg`/`.fetsa` graphics files the data files include. Without them the GBA build stops, and with it every PC build.
- **The PC builds** (`port/tools/`, `port/win32/`): the standard library only.
- **The test harness** (`port/test/`, `port/check-all.mk`): the standard library, plus:
  - **unicorn** for `text-difftest` and `m4a-difftest` (`port/test/difftest/difftest_text.py`, `port/test/audio/m4a_difftest.py`);
  - **numpy** for the monkey runs' frame checks (`port/test/tools/monkey_check.py`);
  - numpy and Pillow, if present, for a faster frame hash in `port/test/tools/frame_prehash.py`; without them it takes a slower path.

The versions on the development machine: numpy 2.3.5, Pillow 12.1.1, unicorn 2.1.4 (Ubuntu 26.04 packages). The standard-library modules used are `argparse`, `ast`, `bisect`, `collections`, `concurrent`, `csv`, `difflib`, `enum`, `errno`, `fcntl`, `getopt`, `glob`, `hashlib`, `importlib`, `json`, `math`, `mmap`, `multiprocessing`, `operator`, `os`, `pathlib`, `pickle`, `random`, `re`, `runpy`, `shlex`, `shutil`, `struct`, `subprocess`, `sys`, `textwrap`, `time`, `typing`, `wave` and `zlib`. (`fcntl` exists only on Unix-like systems, which is one more reason to build in WSL or on Linux.)

### Submodules

None. The tree has no `.gitmodules` file and no submodule entries (checked with `git ls-files -s` on `935e2576`). The `mgfembp` submodule belongs to a different fork of the decomp (laqieer's), where it builds the FE6 link-cable program; the FireEmblemUniverse tree the port builds on has none, and no PC target uses it (the only mention in the tree is a comment in `src/sio_core.c`).

**Common errors** (not yet reproduced here):
- `skipping incompatible ... libSDL3.so` or `cannot find -lSDL3`: only the 64-bit SDL3 is installed. Install `libsdl3-dev:i386`.
- `gnu/stubs-32.h: No such file`: install `gcc-multilib`.
- `gcc-multilib` conflicts with `gcc-i686-linux-gnu` / `crossbuild-essential-i386`: keep `gcc-multilib`.
- `windres` not found: install `binutils-mingw-w64-i686`.
- `:amd64` and `:i386` versions disagree: run `sudo apt full-upgrade` first.

No ROM, ROM-derived file, trace, frame image, save or snapshot belongs in git. The test harness reads the ROM from the build tree and writes only to `build/gate0/`.

## 1. The GBA gate

The decomp's own build still produces the byte-identical GBA ROM. Every change to a shared source file must keep it working.

```sh
make -f make_tools.mk -j6 && make -j6
```

The output must end with `fireemblem8.gba: OK`, checked against the decomp's `checksum.sha1`.

## 2. PC build targets

| Target | Command | Output | Use |
|---|---|---|---|
| Headless, Linux | `make -f port/Makefile` | `build/pc/fe8pc` | No window or sound. Tests. Run as `./build/pc/fe8pc baserom.gba`. |
| Window, Linux | `make -f port/Makefile.sdl` | `build/pc-sdl/` | The Linux build players run. |
| Headless, Windows | `make -f port/Makefile.mingw` | `build/win32/fe8pc.exe` | No window or sound. Tests. |
| Window, Windows | `make -f port/Makefile.mingw SDL=1` | `build/win32-sdl/` (`fe8pc-sdl.exe`, `SDL3.dll`, `LICENSE-SDL3.txt`, `fe8pc-sdl.debug`) | The Windows build players run. It is linked at a fixed address (`--image-base=0x10000000 --disable-dynamicbase`), so Windows' address randomisation (ASLR) is off for it, on purpose: the game's own pointer checks need the program's addresses at or above 0x02000000, as the Linux build gets with `-Ttext-segment=0x10000000` (DIVERGENCES.md, B-11). The game has no network code. |
| Test build | Add `FE8PC_TESTBUILD=1` to any target | A build directory whose name ends in `-test` | Adds test hooks and test-only tools. Never for players. |

Build notes:

- Shared game code compiles as GNU C89 (`gnu89`), so it stays close to what agbcc accepts. Port-only code compiles as GNU C11. Do not build for C23.
- Do not define `MODERN`. It removes `STRUCT_PAD`, and save-file layouts depend on it.
- `-DPC_PORT=1` marks the PC build. Shared files use it to switch to PC code, with the GBA line kept beside it (see ACCURACY.md).
- The played builds (every target above without `FE8PC_TESTBUILD`) include Reforged Options (`port/reforged/`; PLAYING.md, MODDING.md). Its fast-forward is in the two window builds only. Test builds and the Gate 0 binary have none of it: `PORT_PLAYED_BUILD` is 0 when `FE8PC_TESTBUILD` or `FE8PC_DEBUG` is set (`port/include/port.h`), and `port/reforged/` is compiled with the game's flags, so its files are empty there (`port/Makefile`).
- Compiler safety checks (stack protector and `_FORTIFY_SOURCE`) are **on in test builds and off in the played builds**, on both Linux and Windows. This keeps played builds close to the GBA's behaviour.
- A `.debug` file is written beside the Windows window exe. Keep each build's `.debug` with its exe, because the exe and the `.debug` share one build stamp.

### The picture: SDL's renderer

The window builds (Linux and Windows, the same file `port/sdl3/platform_sdl3.c`) draw through SDL3's 2D renderer: `SDL_CreateWindowAndRenderer` with no renderer named, one 240 x 160 streaming texture, nearest-neighbour scaling by a whole number (`SDL_LOGICAL_PRESENTATION_INTEGER_SCALE`), VSync off (the port's own pacer sets the rate). The game's pixels come from the port's software renderer (`port/render/`); SDL only scales and shows them. The port has no OpenGL, Vulkan or SDL GPU code of its own, so it needs no particular graphics driver: SDL picks the first renderer that works.

- **Windows:** SDL picks Direct3D 11. The owner's log says `video driver windows, renderer direct3d11` (`PC-test/fe8pc-sdl.log`, 2026-10-29).
- **Linux:** SDL's own choice for the desktop. The log's `[sdl] SDL 3.4.2, video driver ..., renderer ...` line names it.
- `SDL_RENDER_DRIVER` (for example `software` or `opengl`) and `SDL_VIDEO_DRIVER` (for example `x11` or `wayland`) override SDL's choice; these are SDL's own settings.

### Windows specifics

- `SDL3.dll` is loaded late (delay-loaded), after the GBA memory map has been reserved. Without this, Windows' start-up mappings take the low address space the port needs.
- The exe imports only `KERNEL32.dll` and `msvcrt.dll`. The build fails if it imports anything else (`IMPORT_CHECK`).
- The window exe is a GUI-subsystem program, so it opens no console. Its log and message boxes are described in PLAYING.md.
- A manifest sets the UTF-8 code page, so file names that are not ASCII work with `fopen` and SDL.

## 3. Test harness (overview)

The harness compares the GBA build of the game, running in mGBA, with the PC build. Both get the same button presses. The PC side is a test build (the Gate 0 binary, built with `FE8PC_DEBUG`), so it has no Reforged Options.

- **The oracle.** `make -f port/test/Makefile oracle` builds `build/gate0/mgba_lua_run`, a headless runner for mGBA 0.10.5 with Lua scripting. It uses mGBA's built-in high-level BIOS (see ACCURACY.md, section 5).
- **Gate 0.** One 10-bit button mask per logic tick goes to both sides. Each tick, both sides record game state (the clock, the battle and play-state structures, and hashes of the video and sprite memory). The compare reports one of four verdicts: `MATCH`, `LAG-ONLY` (the difference is the GBA's lag, reproduced by a replay), `DIFFERENT`, or `INCOMPLETE` (a run was cut short).
- **check-all.** `make -f port/check-all.mk` runs every gate: the GBA build, the PC builds, unit tests, sound, Gate 0 and the route checks. The default set has **23 rows**, all passing on 2026-10-28.
- **CHECK_LONG.** `make -f port/check-all.mk CHECK_LONG=1` runs **143 rows**, including the full story routes and the sound rows. On 2026-10-31 it gave 138 pass, 5 known limits and 0 failures (two of the known limits are the P-44 save bytes, ACCURACY.md section 5). It takes about 7 hours on the development machine. Long compares take turns on a lock, so they do not all run at once.
- **Known-limit rows.** `port/test/known_limits.json` lists the exact fields and tick windows of each recorded limit of the test tool. A row that differs only there is reported as KNOWN, not FAIL.
- **Route files.** Button-press files (`.keys`) live in `port/test/inputs/`:
  - *routes* play a chapter or a whole story;
  - *watch* files play scenes;
  - *monkey* files are seeded, structured sequences that open menus and screens (`make monkey`).
  Each route starts from a chapter save or from a saved state.
- **Sound.** `make -f port/test/Makefile snd-route ROUTE_KEYS=...` compares the sound driver's state and output on a route, record by record.
- **Test-only cheats.** The test builds have cheats for durability, funds and the item convoy, so that long routes can finish. Any route that uses them is labelled "cheat-assisted". They are never in played builds, and the GBA reference runs the same cheats on the same ticks.

Route and monkey counts change as coverage grows. The current figures are in ACCURACY.md.
