# check-all: every gate of the PC port in one command, from the repository root:
#
#   make -f port/check-all.mk               # the default set (about 7 minutes from a fresh worktree)
#   make -f port/check-all.mk CHECK_LONG=1  # also the 60,000-tick scripted Gate 0, the ch0-ch5
#                                           # routes and the ch5x-ch7 routes from chapter saves
#                                           # through the lag replay, and the full m4a difftest
#
# A fresh worktree needs baserom.gba and tools/agbcc (both untracked; copy them
# from another worktree, or install agbcc with .deps/agbcc/install.sh).
#
# Each check runs in order, one make at a time, with its output in
# build/check-all/<check>.log. A failing check does not stop the run: every
# check runs (a check whose prerequisite failed is SKIPped and says so), then a
# one-screen PASS/FAIL table is printed (also build/check-all/summary.txt) and
# make exits non-zero if any check failed. When rows were skipped and none
# failed the verdict reads "PASS (N skipped)". The rows and what each one runs:
#
#   gba            make -f make_tools.mk; make (fireemblem8.gba: OK, SHA-1 c25b145e...)
#   pc-core        make -f port/Makefile                     (headless core build, build/pc)
#   pc-full        make -f port/Makefile FULL=1              (headless full build, build/pc-full)
#   sdl            make -f port/Makefile.sdl                 (SDL3 window build, build/pc-sdl)
#   mingw          make -f port/Makefile.mingw               (Windows build; SKIP without i686-w64-mingw32-gcc)
#   win-sdl        make -f port/Makefile.mingw SDL=1         (Windows SDL3 window build, build/win32-sdl:
#                  fe8pc-sdl.exe with SDL3.dll beside it; SKIP without i686-w64-mingw32-gcc or
#                  without SDL3 for MinGW in SDL3_MINGW_DIR, default ~/third-party/SDL3-3.4.2-mingw/
#                  extracted/SDL3-3.4.2/i686-w64-mingw32, from the official SDL3-devel-3.4.2-mingw)
#                  The five build rows (and gate0-300 for build/pc-gate0) are also the
#                  compiler warnings gate: port/Makefile keeps every object's compiler
#                  messages in OBJ.warn, so a re-run that compiles nothing still sees the
#                  warnings of earlier compiles; any warning ("file:line:col: warning:",
#                  and also "cc1: warning:" and the like), or an object without its log,
#                  fails the row (port/tools/check_all.py --warn-dir). It covers the port
#                  code only: the game objects (HOST_CFLAGS), romreloc.o and autostubs*.o
#                  are compiled with -w, so their logs stay empty and "0 compiler warnings
#                  in 375 object logs" means port/ had none.
#   ram-check      make -f port/Makefile [FULL=1] ram-check  (B-12, core and full builds)
#   layout-check   make -f port/Makefile FULL=1 layout-check (D-18)
#   abi            python3 port/win32/abi_check.py build/pc-full/fe8pc build/win32/fe8pc.exe
#   abi-sdl        python3 port/win32/abi_check.py build/pc-sdl/fe8pc-sdl build/win32-sdl/fe8pc-sdl.debug
#                  (the two window builds, SDL's own structs as the port code sees them included; the
#                  Windows release exe's debug information is in fe8pc-sdl.debug, port/Makefile.mingw)
#   render-tests   make -f port/Makefile render-tests
#   psg            make -f port/test/audio/psg.mk
#   bios           make -f port/test/bios/Makefile
#   text-difftest  python3 port/test/difftest/difftest_text.py
#   argorder       python3 port/tools/argorder_sweep.py --view gba|pc --accept port/tools/argorder_accept.txt --check
#                  (D-52: call arguments and operators agbcc and the host GCC take in different
#                  orders; every flagged site must be judged in the accept list, every fixed one gone
#                  from the PC view)
#   seqpoint       python3 port/tools/seqpoint_sweep.py --accept port/tools/seqpoint_accept.txt --check
#                  (D-72: GCC -Wsequence-point over every game file of the FULL build, PC view, -w
#                  removed: an expression that changes a variable and reads it with no sequence
#                  point between, which agbcc and the host GCC may order differently; every site
#                  judged in the accept list, every fixed one gone; a few seconds)
#   declsweep      python3 port/tools/decl_sweep.py --objs-dir build/pc-full --accept port/tools/decl_accept.txt
#                  --check --lto (function declarations whose type differs from the definition across
#                  files, from the FULL build's DWARF, cross-checked by an -flto build's
#                  -Wlto-type-mismatch in build/declsweep; every site judged in the accept list, every
#                  fixed one gone; about half a minute)
#   m4a-difftest   python3 port/test/audio/m4a_difftest.py --quick (full run with CHECK_LONG=1)
#   attract-60000  sh port/tools/run_spike.sh full 60000     (attract mode, 60,000 frames, no keys:
#                  exit 0, no fault, and the end state merge-audio recorded on 2026-10-19)
#   win-sdl-60000  build/win32-sdl/fe8pc-sdl.exe --no-save baserom.gba, through WSL's Windows interop,
#                  with SDL's offscreen video and dummy audio drivers (no window, no sound), SDL's
#                  joystick drivers for real devices off (no gamepad of the machine's can press a
#                  key) and FE8PC_PACE=0: attract mode, 60,000 frames, every GBA region reserved,
#                  no gamepad connected, exit 0, no fault, and the same end state as attract-60000.
#                  SKIP when Windows programs cannot run from here (not WSL, or interop off)
#   snd-title      make -f port/test/Makefile snd SND=title  (sound vs mGBA; also needs the PC run complete)
#   snd-map        make -f port/test/Makefile snd SND=map
#   gate0-300      make -f port/test/Makefile gate0          (boot300.keys, 300 ticks)
# With CHECK_LONG=1:
#   gate0-60000    make -f port/test/Makefile scripted       (scripted_a90_long.keys, 60,000 ticks)
#   route-chN-lag  make -f port/test/Makefile route ROUTE_KEYS=port/test/inputs/route_chN.keys
#                  ROUTE_TICKS=<the file's ticks>            (port/test/inputs/README-routes.md)
#                  The route through the lag replay, compared with the GBA: the plain
#                  lag-free PC cannot follow the bot routes (the GBA's lag shifts the
#                  windows the keys were recorded for; the PC leaves every route at tick
#                  5,666), so the PC run with the GBA's lag VBlanks where they came (the
#                  harness run) is the one compared, on its own, outright clock included.
#                  SKIP when the PC already stopped early on the shorter route before it.
#   route-ch5x-lag, route-ch6-lag, route-ch7-lag, route-ch8-lag, route-e9-lag, route-e10-lag,
#   route-e11-lag, route-e12-lag, route-e13-lag, route-e14-lag, route-e15-lag, route-e16-lag, route-e17-lag, route-e18-lag,
#   route-e19-lag, route-b20-lag, route-b21-lag, route-b22-lag, route-bend-lag, route-bpost-lag (boosted
#   starts, README-routes.md; route-b21-lag fails at tick 31,795 until the Crimson Eye spell animation is
#   built (MB1), route-b22-lag at 25,809 (the ending's CG face, c10-e4 F-E4-2; the Demon King's summon
#   positions, F-E4-1 at 3,135, follow the GBA since port-r10-summon)),
#   route-ch8i-lag, route-i9-lag, route-i10-lag,
#   route-i11-lag, route-i12-lag, route-i13-lag, route-i14-lag, route-i15-lag, route-i16-lag,
#   route-i17-lag, route-i18-lag, route-i19-lag, route-i20-lag, route-i21-lag (the final chapter's part 1
#   on Ephraim's route), route-ib22-lag, route-ibend-lag, route-ibpost-lag (part 2, his ending and his
#   post-game from a boosted start: README-routes.md, "Boosted starts"), route-i22-lag, route-iend-lag
#   (part 2 and his ending with the real army, coverage 12: README-routes.md, "Ephraim's final chapter,
#   part 2, with the real army")
#                  make -f port/test/Makefile chain-route CHAIN=ch5x|ch6|ch7|ch8|e9|e10|e11|e12|e13|e14|e15|e16|e17|e18|e19|b20|b21|b22|bend|bpost
#                  make -f port/test/Makefile chain-route CHAIN=ch8i|i9|i10|i11|i12|i13|i14|i15|i16|i17|i18|i19|i20|i21|ib22|ibend|ibpost|i22|iend
#   route-bt1-lag ... route-f_promochoice_i14-lag (CHECK_COVER_ROUTES, while CHECK_COVER_LONG=1, the
#   default): coverage 11's and 12's route files (port-cover11, port-cover12; README-routes.md): the
#   Tower of Valni floors 1-8 and the Lagdou Ruins floors 2-9 (boosted, refitted), the Tower again and
#   the Ruins floors 1-3 with Eirika's real army (route_et1-et8, er1-er3), nine world-map skirmishes
#   (nodes 4, 6, 10, 12, 15, 17 twice, 21, 28: Melkaen Coast), 18 feature routes (arenas, a support,
#   chests, doors, Suspend, houses and villages, shops and the secret shop, promotions, the post-game
#   Tower's supports), each from its start save as the chain rows below, about 2.8 million ticks in all;
#   and the two cheat-assisted files (integration 12: port-cheat12's route_br10, Ruins floor 10, boosted,
#   GATE0_CHEAT=durability, and route_gallery, the item gallery, edited start, GATE0_CHEAT=durability+funds+convoy;
#   their rows take the cheat from the key file and say so in the title, CHEAT_TITLE below), 214,702 ticks.
#   Left out as not cheap enough (their chain-route targets stay): route_er4 (Ruins floor 4, not won:
#   the bot ends its turns to turn 99) and route_f_lordpromo_e16 (route_e16's first 95,729 ticks, the
#   route-e16-lag row's, then Chapter 16's ending).
#                  Each row's title names its start: "chapter save", "chapter save (real army)",
#                  "boosted start (test coverage only)" or "edited start (test coverage only)"
#                  (BOOSTED_CHAIN_ROUTES, REALARMY_CHAIN_ROUTES, EDITED_CHAIN_ROUTES). A row whose
#                  only differences are a recorded limit of the test tool, listed with its exact
#                  fields and ticks in port/test/known_limits.json (route_br9: N-C11-1, the lag
#                  replay's limit), is KNOWN, not FAIL, and its note names the limit
#                  (port/tools/check_all.py).
#                  The routes that start from a chapter save instead of power-on
#                  (port/test/inputs/README-routes.md, "Routes from chapter saves"), the same
#                  check from that save. The start save is made in port/test/saves/ when it
#                  is missing (sram-capture, back to route_ch5.keys: about 4 minutes for
#                  ch5x_start, 1.5 to 3 more per later save) and checked against the key
#                  file's header. About 3 minutes per route for ch5x-ch7 once the save is
#                  there; ch8 to e15 (82,082 to 157,669 ticks) take about twice that,
#                  and their compares (over 60,000 ticks) take turns under the lock.
#                  ch8i and i9 to i19 are Ephraim's route (Chapter 8 again with the
#                  split's second entry, his Chapters 9 to 19: 42,492 to 181,401 ticks).
#   path-pe14, path-pc7b, path-ch2g, path-ch8r, path-i14b, path-ps14, path-pd14
#                  make -f port/test/Makefile path-route PATH_NAME=pe14|pc7b|ch2g|ch8r|i14b|ps14|pd14
#                  The round-10 "paths" key files (port/test/inputs/paths/README.md: a Bishop on
#                  the UI screens, the staff target selections, a Devil Axe backfire on a wall, the
#                  ballista back-out, the one-unit heal with its NULL target on either side) and
#                  integration 11's (a Summon phantom killed by the counter to its own attack; the
#                  wall backfire by a unit with US_DROP_ITEM), 2,249 to 6,254 ticks each, from a
#                  chapter save, a save taken mid-route or an edited start (chain-save makes it),
#                  through the lag replay with every frame of the path's window compared. About 1
#                  to 3 minutes each once the save is there. pe14, pc7b, ps14 and pd14 start from
#                  edited saves (EDITED_PATH_ROUTES): their rows are titled "edited start (test
#                  coverage only)", like the boosted story routes.
#   snd-route-ch5, snd-route-ch5x, snd-route-ch6, snd-route-ch7, snd-route-ch8, snd-route-e9 to snd-route-e13
#                  make -f port/test/Makefile snd-route ROUTE_KEYS=port/test/inputs/route_ch5.keys
#                  make -f port/test/Makefile chain-snd-route CHAIN=ch5x|ch6|ch7|ch8|e9|e10|e11|e12|e13
#                  Sound against mGBA on the story route (port/test/README.md, "Sound on any
#                  route"): the GBA capture with every VBlank's sound record and the m4a calls,
#                  the PC lag replay with its own, then audio/snd_route.py (the snd rows'
#                  replay, the PC self-check, every PC sound record against the GBA's byte for
#                  byte, the PC run complete): the row passes on "SND ROUTE: PASS". route_ch5
#                  covers ch0-ch4 (their key files are its prefixes). About 1.5 minutes each
#                  for ch5x-ch7, 4 for ch8 and e9-e13, 6 for ch5 (one more GBA capture and PC
#                  run per route). They are in CHECK_LONG while CHECK_SND_LONG=1 (the default):
#                  CHECK_SND_LONG=0 leaves them out.
#   A route row whose key file was made with the test cheat (its "# Generated" line names
#                  ROUTE_CHEAT; README-routes.md, "Cheat-assisted routes") is titled "cheat-assisted
#                  (GATE0_CHEAT=..., test coverage only)" and passes GATE0_CHEAT=... to its make, so
#                  both sides run with it (port/test/README.md, "Test cheats"); every other row, and
#                  check-all itself, runs without it (a GATE0_CHEAT or ROUTE_CHEAT in the environment
#                  stops check-all).
#   The route, chain and path rows also compare the route's end SRAM, written on both sides (the
#                  GBA capture and the PC lag replay), with tools/sram_blockdiff.py --cross
#                  (port/test/Makefile route-sram; README.md, "Save checks"; the user's SAVE-1 B of
#                  2026-10-30): the note ends with "end SRAM vs GBA EQUAL apart from the known bytes
#                  (...)" or IDENTICAL, and a byte that is not a known one (or no compare) fails
#                  the row, a KNOWN row too (check_all.py --gate0-sram). The two saves and the
#                  report are kept in build/check-all/<row>/.
# With CHECK_SAVES=1 (opt-in, in addition to the set chosen above; port-savechk2, the user's SAVE-3 B
# of 2026-10-30):
#   saves          make -f port/test/Makefile savechk        (tools/savechk_all.py: P5.3 (all three
#                  slots), the save-menu actions (Copy Data, Erase Data, a chapter end saved to slot 2
#                  while slot 3 is full), P5.4 (a suspend from the PC to mGBA and back, Gate 0 and the
#                  played builds) and Reforged Options on (casual mode with a test fall, unbreakable
#                  weapons, infinite gold: the side file's marks and fallen lists, and the .sav still
#                  plays in mGBA), on the played builds FULL, Linux SDL and Windows SDL (the Windows
#                  one only where Windows programs run from here; the options chain on the Linux
#                  builds) against mGBA. The row passes on "SAVE CHECKS: PASS".
# A Gate 0 row passes when the compare's verdict is MATCH or LAG-ONLY (DIFFERENT
# fails, and so does INCOMPLETE: a compared run that stopped early or logged a
# fault), every PC run of the row (the judged one, the lag replay, the affine
# replay) completed every tick with no fault, and the test build reported no
# unknown PROC_CALL_ARG target (GATE0_REJECT below); its note gives the ticks the
# PC completed, any run's first fault (resolved with addr2line), the verdict with
# the lag check's counts (lag-only, not lag-only, replay-only) and the first tick
# where a game-logic field differs. Its compare.json and PC logs are kept in
# build/check-all/<row>/ (the next Gate 0 run overwrites build/gate0).
#
# Settings: CHECK_JOBS (-j for each build, default 6, at most 8), CHECK_VERBOSE=1
# (show every check's output as it runs), CHECK_ROUTES (default "ch0 ch1 ch2 ch3
# ch4 ch5", shortest first), CHECK_CHAIN_ROUTES (default "ch5x ch6 ch7 ch8 e9
# e10 e11 e12 e13 e14 e15 e16 e17 e18 e19 e20 e21 e22 eend b20 b21 b22 bend bpost ch8i i9 i10 i11 i12 i13 i14 i15 i16 i17 i18 i19 i20
# i21 ib22 ibend ibpost i22 iend",
# the routes from chapter saves), CHECK_ROUTES_ALL=1 (run every route even after the
# PC stopped on a shorter one), ROUTE_PC_TIMEOUT (seconds per PC run of a route,
# default 3600), CHECK_SND_LONG (1, the default: CHECK_LONG=1 also runs the
# snd-route rows; 0: it does not), CHECK_SND_ROUTES (default "ch5 ch5x ch6 ch7 ch8
# e9 e10 e11 e12 e13", the routes of those rows), CHECK_DIR (default build/check-all).
#
# Long compares take turns: port/test/Makefile runs every Gate 0 compare of
# 60,000 ticks or more under ~/.fe8-compare.lock (port/tools/check_all.py lock),
# while the emulator and PC runs do not lock (port-plan/DECISIONS.md, test
# throughput). Running the whole check under that lock (flock
# ~/.fe8-compare.lock make -f port/check-all.mk CHECK_LONG=1) also works: the
# compare steps see the inherited lock and do not wait on it.
#
# The FULL=1 build rewrites the tracked port/tools/fixptr_unresolved.txt, as it
# always does; `git status` shows it if the list changed.

ifeq ($(wildcard port/check-all.mk),)
$(error port/check-all.mk: run from the repository root: make -f port/check-all.mk)
endif

PYTHON           ?= python3
CHECK_JOBS       ?= 6
CHECK_LONG       ?=
CHECK_VERBOSE    ?=
CHECK_DIR        ?= build/check-all
CHECK_ROUTES     ?= ch0 ch1 ch2 ch3 ch4 ch5
CHECK_CHAIN_ROUTES ?= ch5x ch6 ch7 ch8 e9 e10 e11 e12 e13 e14 e15 e16 e17 e18 e19 e20 e21 e22 eend b20 b21 b22 bend bpost \
                      ch8i i9 i10 i11 i12 i13 i14 i15 i16 i17 i18 i19 i20 i21 ib22 ibend ibpost i22 iend
CHECK_PATH_ROUTES ?= pe14 pc7b ch2g ch8r i14b ps14 pd14
# The path rows whose start save a script edited (port/test/inputs/paths/README.md: units, items,
# positions, the RN state; "edited start, test coverage only", like the boosted saves): their rows
# say so in the table (integration 11, the user said "I trust your judgement": kept as CHECK_LONG
# rows, as the paths reviewer recommended; they are the only runs of those paths).
EDITED_PATH_ROUTES := pe14 pc7b ps14 pd14
# Coverage 11's and 12's route files (port-cover11, port-cover12), chain rows of CHECK_LONG while
# CHECK_COVER_LONG=1 (the default; 0 or empty leaves them out). route_er4 and route_f_lordpromo_e16 are
# left out as not cheap enough (the comment at the top).
CHECK_COVER_LONG ?= 1
CHECK_COVER_ROUTES ?= bt1 bt5 bt6 bt7 bt8 br2 br3 br4 br5 br6 br7 br8 br9 sk4 sk10 sk15 sk17 f_arena_ch5 \
                     f_support_e12 f_chest_ch8 f_suspend_ch6 f_promo_ch6 sk6 sk12 sk17i sk21 sk28 et1 et2 et3 \
                     et4 et5 et6 et7 et8 er1 er2 er3 f_house_ch1 f_village_ch2 f_village_ch4 f_village_ch5 \
                     f_doorkey_ch8 f_chestkey_ch8 f_arena_e10 f_arena_i12 f_supports_tower_e \
                     f_supports_tower_i f_secretshop_e14 f_thirdtier_ch6 f_promochoice_i14 br10 gallery
# The start kind each chain row's title names (README-routes.md): boosted starts and edited starts are
# test coverage only; the real army is the story chain's own units, never edited.
BOOSTED_CHAIN_ROUTES  := b20 b21 b22 bend bpost ib22 ibend ibpost bt1 bt5 bt6 bt7 bt8 br2 br3 br4 br5 br6 br7 \
                        br8 br9 br10 f_promo_ch6 f_supports_tower_i
REALARMY_CHAIN_ROUTES := e20 e21 e22 eend i21 i22 iend et1 et2 et3 et4 et5 et6 et7 et8 er1 er2 er3 er4 \
                        f_supports_tower_e
EDITED_CHAIN_ROUTES   := f_secretshop_e14 f_thirdtier_ch6 gallery
chain_kind = $(if $(filter $(1),$(EDITED_CHAIN_ROUTES)),edited start (test coverage only),$(if $(filter $(1),$(BOOSTED_CHAIN_ROUTES)),boosted start (test coverage only),$(if $(filter $(1),$(REALARMY_CHAIN_ROUTES)),chapter save (real army),chapter save)))
ROUTE_PC_TIMEOUT ?= 3600
SCRIPTED_TICKS   ?= 60000
# The sound rows of CHECK_LONG (snd-route-<route>): this one switch decides
# whether CHECK_LONG=1 runs them (1, the default, recommended; 0 or empty: not).
CHECK_SND_LONG   ?= 1
CHECK_SND_ROUTES ?= ch5 ch5x ch6 ch7 ch8 e9 e10 e11 e12 e13

J        := -j$(CHECK_JOBS)
GBA_SHA1 := c25b145e37456171ada4b0d440bf88a19f4d509f
CA       := $(PYTHON) port/tools/check_all.py --dir $(CHECK_DIR)
RUN      := $(CA) run $(if $(filter 1,$(CHECK_VERBOSE)),--verbose)
# Compiles run (0 when the build was up to date), and the compiler warnings
# gate over the build directory's per-object logs.
COMPILED := --count '^\S*cc .* -c =files compiled'
WARN      = $(COMPILED) --warn-dir $(1)
RAMNOTE  := --note-all 'check_ram_addrs: (\S+): ([0-9]+) RAM symbols checked: .*; ([0-9]+) at another address=>{2} RAM symbols, {3} misplaced'
# port/test/Makefile's own tick counts for the sound runs, read back with make -p.
SND_TICKS = $(shell MAKEFLAGS= make -f port/test/Makefile -pn .check-all-none 2>/dev/null \
              | sed -n 's/^SND_TICKS_$(1) := //p')
ROUTE_KEYS = port/test/inputs/$(if $(filter ch0,$(1)),route_ch0_prologue,route_$(1)).keys
# The test cheat (port/test/README.md, "Test cheats"; README-routes.md, "Cheat-assisted routes"): a
# route row whose key file names ROUTE_CHEAT in its "# Generated" line is titled cheat-assisted and
# passes GATE0_CHEAT to its make, so both sides run with it; every other row runs as before. The
# rest of check-all never has it: one set in the environment stops check-all.
KEYS_CHEAT  = $(shell sed -n 's/^\# Generated.*ROUTE_CHEAT=\([a-z+]*\).*/\1/p' $(1) 2>/dev/null)
CHEAT_TITLE = $(if $(call KEYS_CHEAT,$(1)),$(comma) cheat-assisted (GATE0_CHEAT=$(call KEYS_CHEAT,$(1))$(comma) test coverage only))
CHEAT_ARG   = $(if $(call KEYS_CHEAT,$(1)), GATE0_CHEAT=$(call KEYS_CHEAT,$(1)))
comma := ,
ifneq ($(GATE0_CHEAT)$(ROUTE_CHEAT),)
$(error check-all: GATE0_CHEAT=$(GATE0_CHEAT) ROUTE_CHEAT=$(ROUTE_CHEAT) is set: check-all runs without the test cheats; a cheat-assisted route's row passes its own (port/test/README.md, "Test cheats"))
endif

# SDL3 for MinGW (the window build for Windows, port/Makefile.mingw SDL=1, which
# has the same default and takes this one from the environment).
SDL3_MINGW_DIR ?= $(HOME)/third-party/SDL3-3.4.2-mingw/extracted/SDL3-3.4.2/i686-w64-mingw32
export SDL3_MINGW_DIR

ROWS := gba pc-core pc-full sdl mingw win-sdl ram-check layout-check abi abi-sdl render-tests psg bios \
        text-difftest argorder seqpoint declsweep m4a-difftest attract-60000 win-sdl-60000 snd-title snd-map gate0-300
ifeq ($(CHECK_LONG),1)
ROWS += gate0-60000 $(addsuffix -lag,$(addprefix route-,$(CHECK_ROUTES) $(CHECK_CHAIN_ROUTES))) \
        $(addprefix path-,$(CHECK_PATH_ROUTES))
ifeq ($(CHECK_COVER_LONG),1)
ROWS += $(addsuffix -lag,$(addprefix route-,$(CHECK_COVER_ROUTES)))
endif
ifeq ($(CHECK_SND_LONG),1)
ROWS += $(addprefix snd-route-,$(CHECK_SND_ROUTES))
endif
endif
# The save checks (opt-in, CHECK_SAVES=1; the comment at the top).
CHECK_SAVES ?=
ifeq ($(CHECK_SAVES),1)
ROWS += saves
endif

.DEFAULT_GOAL := check-all
.NOTPARALLEL:
.PHONY: check-all check-all-start $(addprefix ca-,$(ROWS))

check-all: check-all-start $(addprefix ca-,$(ROWS))
	@$(CA) table $(ROWS)

check-all-start:
	@rm -rf $(CHECK_DIR); mkdir -p $(CHECK_DIR)
	@echo "check-all: $(words $(ROWS)) checks ($(if $(filter 1,$(CHECK_LONG)),CHECK_LONG=1,default set)), $(J), logs in $(CHECK_DIR)/"

ca-gba:
	@$(RUN) gba "GBA build + SHA-1" --require '^fireemblem8\.gba: OK$$' --require '^$(GBA_SHA1)  fireemblem8\.gba$$' \
	    --note '^(fireemblem8\.gba: OK)$$' --note '^([0-9a-f]{40})  fireemblem8\.gba$$=>SHA-1 {1}' \
	    -- sh -c 'test -x tools/agbcc/bin/agbcc || { echo "tools/agbcc is missing: install agbcc into tools/agbcc (.deps/agbcc/install.sh) or copy it from another worktree"; exit 1; }; \
	              make -f make_tools.mk $(J) && make $(J) && sha1sum fireemblem8.gba'

ca-pc-core:
	@$(RUN) pc-core "PC headless core build" --needs gba $(call WARN,build/pc) $(RAMNOTE) -- make $(J) -f port/Makefile

ca-pc-full:
	@$(RUN) pc-full "PC headless FULL build" --needs gba $(call WARN,build/pc-full) $(RAMNOTE) \
	    -- make $(J) -f port/Makefile FULL=1

ca-sdl:
	@$(RUN) sdl "SDL3 window build" --needs gba $(call WARN,build/pc-sdl) $(RAMNOTE) -- make $(J) -f port/Makefile.sdl

ca-mingw:
	@$(RUN) mingw "MinGW Windows build" --skip-unless-cmd i686-w64-mingw32-gcc --needs gba $(call WARN,build/win32) \
	    --note-all 'check_ram_addrs: (\S+): ([0-9]+) RAM symbols checked: .*; ([0-9]+) at another address=>{2} RAM symbols, {3} misplaced' \
	    -- make $(J) -f port/Makefile.mingw

# The Windows window build (port/win32/README.md): the build checks its own RAM
# addresses (B-12), that no SDL function became a stub and that the exe
# imports only KERNEL32.dll and msvcrt.dll (port/Makefile.mingw).
ca-win-sdl:
	@$(RUN) win-sdl "Windows SDL3 window build" --skip-unless-cmd i686-w64-mingw32-gcc \
	    --skip-unless-path $(SDL3_MINGW_DIR)/bin/SDL3.dll --needs gba $(call WARN,build/win32-sdl) \
	    --note-all 'check_ram_addrs: (\S+): ([0-9]+) RAM symbols checked: .*; ([0-9]+) at another address=>{2} RAM symbols, {3} misplaced' \
	    -- sh -c 'make $(J) -f port/Makefile.mingw SDL=1 && ls -l build/win32-sdl/fe8pc-sdl.exe build/win32-sdl/fe8pc-sdl.debug build/win32-sdl/SDL3.dll'

ca-ram-check:
	@$(RUN) ram-check "RAM addresses (B-12), core + FULL" --needs pc-core --needs pc-full $(RAMNOTE) \
	    -- sh -c 'make -f port/Makefile ram-check && make -f port/Makefile FULL=1 ram-check'

ca-layout-check:
	@$(RUN) layout-check "Struct layouts vs agbcc (D-18)" --needs pc-full \
	    --note '^layout_probe: ([0-9]+) files, ([0-9]+) errors, ([0-9]+) distinct types.*; ([0-9]+) types differ=>{1} files, {2} errors, {3} types, {4} differ' \
	    -- make -f port/Makefile FULL=1 layout-check

ca-abi:
	@$(RUN) abi "Windows ABI (struct layouts)" --needs pc-full --needs mingw \
	    --note '^abi_check: ([0-9]+) struct/union layouts compared.*; ([0-9]+) differ=>{1} layouts, {2} differ' \
	    -- $(PYTHON) port/win32/abi_check.py build/pc-full/fe8pc build/win32/fe8pc.exe

ca-abi-sdl:
	@$(RUN) abi-sdl "Windows ABI, window builds (struct layouts)" --needs sdl --needs win-sdl \
	    --note '^abi_check: ([0-9]+) struct/union layouts compared.*; ([0-9]+) differ=>{1} layouts, {2} differ' \
	    -- $(PYTHON) port/win32/abi_check.py build/pc-sdl/fe8pc-sdl build/win32-sdl/fe8pc-sdl.debug

ca-render-tests:
	@$(RUN) render-tests "Renderer unit tests" \
	    --note '^([0-9]+) passed, ([0-9]+) failed, ([0-9]+) checks=>BG {1} passed, {2} failed' \
	    --note '^passed ([0-9]+), failed ([0-9]+)=>OBJ {1} passed, {2} failed' \
	    -- make -f port/Makefile render-tests

ca-psg:
	@$(RUN) psg "PSG model tests" --require '^warnings: psg\.c is clean' --require '^no-float: psg\.o has no' \
	    --note-all '^test_psg: ([0-9]+ checks, [0-9]+ failed)' -- make -f port/test/audio/psg.mk

ca-bios:
	@$(RUN) bios "BIOS math tests" --require '^sine-table: .* matches' --require '^no-float: bios_math\.o has no' \
	    --note-all '^(PASS|FAIL): ([0-9]+) checks, ([0-9]+) failed=>{2} checks, {3} failed' \
	    -- make -f port/test/bios/Makefile

ca-text-difftest:
	@$(RUN) text-difftest "Text difftest (DrawGlyph, DecodeString)" --needs gba --require '^RESULT: PASS' \
	    --note '^Cases: ([0-9]+) calls=>{1} calls' --note '^(RESULT: [A-Z]+)' \
	    -- $(PYTHON) port/test/difftest/difftest_text.py

# The argument-order sweep (port/tools/argorder_sweep.py; port-plan/DIVERGENCES.md D-52): every call
# argument list, operator and assignment that agbcc and the host GCC evaluate in different orders,
# judged against port/tools/argorder_accept.txt, once as the GBA build preprocesses the game (the
# retail lines) and once as the PC builds do (a site fixed under PC_PORT must be gone there). A new
# flagged site, a fix that went missing or an accept entry that no longer matches fails the row.
ca-argorder:
	@$(RUN) argorder "Argument-order sweep vs the accept list (GBA and PC views)" --needs gba \
	    --require '^selftest: [0-9]+ cases, 0 failed$$' \
	    --require '^argorder_sweep: view gba, ' --require '^argorder_sweep: view pc, ' \
	    --note '^selftest: ([0-9]+) cases, ([0-9]+) failed=>model {1} cases, {2} failed' \
	    --note-all '^  flagged: ([0-9]+) \(([0-9]+) in the accept list\), not accepted: ([0-9]+)=>{1} flagged, {3} open' \
	    -- sh -c '$(PYTHON) port/tools/argorder_sweep.py --selftest \
	              && $(PYTHON) port/tools/argorder_sweep.py -j $(CHECK_JOBS) --view gba --accept port/tools/argorder_accept.txt --check \
	              && $(PYTHON) port/tools/argorder_sweep.py -j $(CHECK_JOBS) --view pc --accept port/tools/argorder_accept.txt --check'

# The sequence-point sweep (port/tools/seqpoint_sweep.py; round 10's C10-10 (a), RemoveTrap's
# `*trap++ = *(trap + 1)`, D-72): GCC's -Wsequence-point over every game C file with the FULL build's
# flags (PC view, -w removed, -fsyntax-only), judged against port/tools/seqpoint_accept.txt. A new
# site, a fixed one reported again, a stale benign entry or a file the compiler refuses fails the
# row. The port's own files are compiled with -Wall (which has -Wsequence-point), so the build
# rows' warnings gate covers them. Needs pc-full (its lvcast copies and the generated sources).
ca-seqpoint:
	@$(RUN) seqpoint "Sequence-point sweep (GCC -Wsequence-point, PC view) vs the accept list" --needs pc-full \
	    --require '^SEQPOINT SWEEP: PASS ' \
	    --note '^seqpoint_sweep: ([0-9]+) game files .*, ([0-9]+) -Wsequence-point warnings at ([0-9]+) sites=>{1} files, {3} sites' \
	    --note '^SEQPOINT SWEEP: [A-Z]+ \(([0-9]+) sites, ([0-9]+) new, ([0-9]+) fix missing, ([0-9]+) stale=>{2} new, {3} fix missing, {4} stale' \
	    -- $(PYTHON) port/tools/seqpoint_sweep.py -j $(CHECK_JOBS) --accept port/tools/seqpoint_accept.txt --check

# The declaration sweep (port/tools/decl_sweep.py; round 10's C10-6, the Demon King's summon): every
# function declaration whose return or parameter types differ from the definition in another file,
# read from the FULL build's DWARF and judged against port/tools/decl_accept.txt (a narrow return
# read through a wider declaration gets the definition's type under PC_PORT; the GBA's callee
# extends its value, the host's does not). --lto builds the objects again with -flto in
# build/declsweep and fails when -Wlto-type-mismatch reports a name the scan missed.
ca-declsweep:
	@$(RUN) declsweep "Declaration types vs definitions (DWARF, LTO) vs the accept list" --needs pc-full \
	    --require '^DECL SWEEP: PASS ' \
	    --note '^decl_sweep: ([0-9]+) objects, .*, ([0-9]+) sites differ=>{1} objects, {2} sites' \
	    --note '^decl_sweep --lto: link exit ([0-9]+); -Wlto-type-mismatch: ([0-9]+) reports .*, ([0-9]+) not in the lists=>LTO {2} reports, {3} missed' \
	    -- $(PYTHON) port/tools/decl_sweep.py -j $(CHECK_JOBS) --objs-dir build/pc-full \
	       --accept port/tools/decl_accept.txt --check --lto

ca-m4a-difftest:
	@$(RUN) m4a-difftest "m4a difftest ($(if $(filter 1,$(CHECK_LONG)),full,quick))" --needs gba \
	    --note '^\s+total\s+([0-9]+)\s+([0-9]+)$$=>{1} cases, {2} failures' \
	    --note '^ARM writes outside the driver image.*: ([0-9]+) granules=>{1} ARM writes outside' \
	    --note '^(NOT COVERED: .*)' \
	    -- $(PYTHON) port/test/audio/m4a_difftest.py $(if $(filter 1,$(CHECK_LONG)),,--quick)

# Attract mode, as merge-audio ran it (port-plan/pending/merged/merge-audio.md):
# the FULL headless build, 60,000 frames with no keys. run_spike.sh reports a
# non-zero exit on a line of its own and exits 0 itself, so that line fails the
# row. The end state must be the one recorded then (deterministic run): any
# change there is a regression to explain, or a new value to record here.
ATTRACT_END := --require '^\[port\] exit 0: frames=60000 OnMain calls=60000 ' \
               --require '^\[audio\] 60000 VBlanks, 13440000 frames made' \
               --require '^\[state\] gGameClock=60000 DISPCNT=0x3F00 ' \
               --require '^\[state\] VRAM non-zero bytes=63604 \(FNV-1a 0xE8A769C8\), PLTT non-zero entries=264\r?$$' \
               --reject '^\[fault\]' \
               --note '^\[port\] exit 0: (frames=[0-9]+)' --note '^\[state\] (gGameClock=[0-9]+ DISPCNT=0x[0-9A-F]+)' \
               --note '^\[state\] VRAM non-zero bytes=([0-9]+) \(FNV-1a (0x[0-9A-F]+)\)=>VRAM {2}'
ca-attract-60000:
	@$(RUN) attract-60000 "Attract mode, 60,000 frames (FULL build)" --needs pc-full $(ATTRACT_END) \
	    --reject '^fe8pc exit status' \
	    -- env FE8PC_TIMEOUT=600 JOBS=$(CHECK_JOBS) sh port/tools/run_spike.sh full 60000

# The Windows window build run on Windows, from here (WSL's interop runs the
# exe; the variables reach it through WSLENV): no window (SDL's offscreen video
# driver), no sound (its dummy audio driver), unpaced, no save file. The game
# must take the attract-mode path the headless build takes, to the same end
# state, with every GBA region reserved at its address.
# The exe reads live gamepads even with the offscreen driver, so a pad touched
# while check-all runs could press keys in this fixed run: SDL's joystick
# drivers for real devices are turned off (SDL reads each hint from the
# environment variable of its name; Windows has these six: RawInput, HIDAPI,
# Windows.Gaming.Input, GameInput, XInput, DirectInput), and a gamepad that
# connects anyway fails the row. SDL's virtual gamepads (the test hooks') are
# not affected. Also for other automated runs of the window build on Windows
# (port/win32/README.md, "Checks").
WIN_SDL_NOPADS := SDL_JOYSTICK_RAWINPUT=0 SDL_JOYSTICK_HIDAPI=0 SDL_JOYSTICK_WGI=0 SDL_JOYSTICK_GAMEINPUT=0 \
                  SDL_XINPUT_ENABLED=0 SDL_JOYSTICK_DIRECTINPUT=0
WIN_SDL_ENV := SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy FE8PC_PACE=0 FE8PC_FRAMES=60000 $(WIN_SDL_NOPADS) \
               WSLENV=SDL_VIDEO_DRIVER:SDL_AUDIO_DRIVER:FE8PC_PACE:FE8PC_FRAMES:SDL_JOYSTICK_RAWINPUT:SDL_JOYSTICK_HIDAPI:SDL_JOYSTICK_WGI:SDL_JOYSTICK_GAMEINPUT:SDL_XINPUT_ENABLED:SDL_JOYSTICK_DIRECTINPUT
ca-win-sdl-60000:
	@$(RUN) win-sdl-60000 "Windows window build on Windows, attract mode 60,000 frames (offscreen)" \
	    --skip-unless-windows --needs win-sdl $(ATTRACT_END) \
	    --require '^\[sdl\] SDL 3\.[0-9]+\.[0-9]+, video driver offscreen' \
	    --require '^\[mem\] SRAM  0x0E000000 \+0x0010000 reserved' --reject 'RESERVATION FAILED' \
	    --reject '^\[sdl\] gamepad' \
	    --note '^\[sdl\] (SDL 3\.[0-9]+\.[0-9]+), video driver ([^,\s]+), renderer ([^,\s]+)=>{1} {2}/{3}' \
	    -- env $(WIN_SDL_ENV) timeout 900 build/win32-sdl/fe8pc-sdl.exe --no-save baserom.gba

# Sound replay vs mGBA (port/test/README.md, "Sound"). The replay (RESULT) is
# the pass/fail of `snd`; check-all also requires the PC self-check to pass
# and the PC run to complete its ticks, as it does on this line since D-13.
define snd_row
ca-snd-$(1):
	@$$(RUN) snd-$(1) "Sound vs mGBA, SND=$(1)" --needs gba --require '^RESULT: PASS' \
	    --require '^PC \.fifo8 against the slot rule on the PC records: [0-9]+ VBlanks, 0 differ' \
	    --trace-ticks build/gate0/snd/$(1).pc.jsonl=$$(call SND_TICKS,$(1)) \
	    --note '^replayed ([0-9]+) VBlanks \(records [0-9]+-[0-9]+; ([0-9]+) taken before m4aSoundInit returned skipped\): ([0-9]+) fully equal, ([0-9]+) equal outside .*, ([0-9]+) differ=>replay {1} VBlanks: {3} equal, {4} excused, {5} differ ({2} boot records skipped)' \
	    --note '^  PCM lanes \(the FIFO bytes\) equal on ([0-9]+);=>PCM lanes equal on {1}' \
	    --note '^PC \.fifo8 against the slot rule on the PC records: ([0-9]+) VBlanks, ([0-9]+) differ=>self-check {2}/{1} differ' \
	    -- make $$(J) -f port/test/Makefile snd SND=$(1)
endef
$(eval $(call snd_row,title))
$(eval $(call snd_row,map))

# The Gate 0 test build reports a PROC_CALL_ARG target it does not know, which
# could be a void function given host eax (port/test/pc/proc_call_arg_check.c);
# such a report fails the row.
GATE0_REJECT := --reject '^\[gate0\] PROC_CALL_ARG: '

ca-gate0-300:
	@$(RUN) gate0-300 "Gate 0, boot300 (300 ticks)" --needs gba --gate0 300 --warn-dir build/pc-gate0 \
	    $(GATE0_REJECT) -- make $(J) -f port/test/Makefile gate0

ca-saves:
	@$(RUN) saves "Save checks vs mGBA (P5.3, P5.4, Copy/Erase, options on; played builds)" \
	    --needs gba --needs pc-full --needs sdl --require '^SAVE CHECKS: PASS' \
	    --note-all '^NOTE (.*)' --note '^SAVE CHECKS: (.*)' \
	    -- make -f port/test/Makefile savechk

ca-gate0-60000:
	@$(RUN) gate0-60000 "Gate 0, scripted ($(SCRIPTED_TICKS) ticks)" --needs gba --gate0 $(SCRIPTED_TICKS) \
	    $(GATE0_REJECT) -- make $(J) -f port/test/Makefile scripted SCRIPTED_TICKS=$(SCRIPTED_TICKS)

# One row per route (phony targets take no pattern rules, hence the eval).
# The tick count is the key file's own (port/test/README.md, "Input file").
# Each route's key file is a prefix of the next one's (README-routes.md), and
# the PC replay is deterministic, so when the PC stops early on one route
# (a fault), the longer routes are SKIPped with that tick rather than run to
# the same stop; CHECK_ROUTES_ALL=1 runs them anyway.
CHECK_ROUTES_ALL ?=
route_prev :=
$(foreach r,$(CHECK_ROUTES),$(eval ROUTE_PREV_$(r) := $(route_prev))$(eval route_prev := $(r)))
define route_row
ca-route-$(1)-lag:
	@ticks=$$$$($$(CA) keyticks $(call ROUTE_KEYS,$(1))) || exit 1; \
	$$(RUN) route-$(1)-lag "Route $(1) through the lag replay vs GBA ($$$$ticks ticks)$(call CHEAT_TITLE,$(call ROUTE_KEYS,$(1)))" --needs gba \
	    --gate0 $$$$ticks --gate0-pc pc_lagreplay --gate0-sram $$(GATE0_REJECT) \
	    $(if $(filter 1,$(CHECK_ROUTES_ALL)),,$(if $(ROUTE_PREV_$(1)),--skip-if-pc-stopped route-$(ROUTE_PREV_$(1))-lag)) \
	    -- make $$(J) -f port/test/Makefile route ROUTE_KEYS=$(call ROUTE_KEYS,$(1)) ROUTE_TICKS=$$$$ticks \
	       PC_TIMEOUT=$$(ROUTE_PC_TIMEOUT)$(call CHEAT_ARG,$(call ROUTE_KEYS,$(1)))
endef
$(foreach r,$(CHECK_ROUTES),$(eval $(call route_row,$(r))))

# The routes from chapter saves (port/test/Makefile chain-route). Each has its
# own start save, so none is skipped because another stopped.
define chain_row
ca-route-$(1)-lag:
	@ticks=$$$$($$(CA) keyticks port/test/inputs/route_$(1).keys) || exit 1; \
	$$(RUN) route-$(1)-lag "Route $(1) from its $(call chain_kind,$(1)), lag replay vs GBA ($$$$ticks ticks)$(call CHEAT_TITLE,port/test/inputs/route_$(1).keys)" --needs gba \
	    --gate0 $$$$ticks --gate0-pc pc_lagreplay --gate0-sram $$(GATE0_REJECT) \
	    -- make $$(J) -f port/test/Makefile chain-route CHAIN=$(1) PC_TIMEOUT=$$(ROUTE_PC_TIMEOUT)$(call CHEAT_ARG,port/test/inputs/route_$(1).keys)
endef
$(foreach r,$(CHECK_CHAIN_ROUTES) $(filter-out $(CHECK_CHAIN_ROUTES),$(CHECK_COVER_ROUTES)),$(eval $(call chain_row,$(r))))

# The round-10 "paths" key files (port/test/inputs/paths/README.md; port/test/Makefile path-route):
# each from its start save, through the lag replay, every frame of its window compared too.
define path_row
ca-path-$(1):
	@ticks=$$$$($$(CA) keyticks port/test/inputs/paths/$(1).keys) || exit 1; \
	$$(RUN) path-$(1) "Path $(1) from its $(if $(filter $(1),$(EDITED_PATH_ROUTES)),edited start (test coverage only),start save), lag replay vs GBA ($$$$ticks ticks)" --needs gba \
	    --gate0 $$$$ticks --gate0-pc pc_lagreplay --gate0-sram $$(GATE0_REJECT) \
	    -- make $$(J) -f port/test/Makefile path-route PATH_NAME=$(1) PC_TIMEOUT=$$(ROUTE_PC_TIMEOUT)
endef
$(foreach r,$(CHECK_PATH_ROUTES),$(eval $(call path_row,$(r))))

# Sound on the story routes (CHECK_LONG=1 with CHECK_SND_LONG=1; port/test/README.md,
# "Sound on any route"): snd-route on the route's whole key file, from power-on
# for the CHECK_ROUTES ones, from its chapter save (chain-snd-route) for the
# others. SND ROUTE: PASS needs the replay, the PC self-check, every PC sound
# record equal to its GBA record and the PC run complete.
define snd_route_row
ca-snd-route-$(1):
	@$$(RUN) snd-route-$(1) "Sound vs mGBA on route $(1) (snd-route)" --needs gba --require '^SND ROUTE: PASS' \
	    --note '^frame clock: D = GBA record - PC record = (-?[0-9]+) on ([0-9]+) ticks=>D {1} on {2} ticks' \
	    --note '^PC records [0-9]+.*: ([0-9]+) compared with GBA records .*, ([0-9]+) fully equal, ([0-9]+) equal outside .*, ([0-9]+) differ=>{2} of {1} PC sound records equal, {3} excused, {4} differ' \
	    --note '^(SND ROUTE: [A-Z]+)' \
	    -- make $$(J) -f port/test/Makefile $(if $(filter $(CHECK_ROUTES),$(1)),snd-route ROUTE_KEYS=$(call ROUTE_KEYS,$(1)),chain-snd-route CHAIN=$(1)) \
	       SND_JOBS=$$(CHECK_JOBS)
endef
$(foreach r,$(CHECK_SND_ROUTES),$(eval $(call snd_route_row,$(r))))
