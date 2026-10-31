# Fire Emblem: The Sacred Stones, PC port

This repository is a PC port of *Fire Emblem: The Sacred Stones*, built on the [fireemblem8u](https://github.com/FireEmblemUniverse/fireemblem8u) decompilation. The decompilation's own README, with its instructions for building the GBA ROM, is [README-decomp.md](README-decomp.md). The port's documents are listed under "Documents" below.

## What the project is

The PC port is a native Windows and Linux version of *Fire Emblem: The Sacred Stones*, a Game Boy Advance game. It is built from the community decompilation of the game (the FireEmblemUniverse `fireemblem8u` project).

The game's own C source is compiled for the PC. Parts that belonged to the GBA hardware are replaced by PC code:

- the renderer (the picture);
- the sound output (the GBA sound driver's logic is kept, its output goes to the PC);
- the GBA BIOS functions (maths, decompression, reset, frame wait);
- input and save files.

**The PC port is not an emulator.** It does not emulate the GBA CPU or run the GBA ROM. The game's logic runs as native code.

The game plays as the GBA game. The builds players run also have one optional screen, **Reforged Options** (on the title menu, under Extras), with extra player options and cheats. Everything there is off at first except two things: fast-forward, which acts only while its key is held, and the original-game bug fixes, which stop two of the original game's freezes (the Pierce fix also makes every Pierce hit with battle animations on wait like Sure Shot, with the same results; the owner chose to keep it so on 2026-10-30). [PATCH-NOTES.md](docs/port/PATCH-NOTES.md) lists both. See [PLAYING.md](docs/port/PLAYING.md).

## Status (as of 2026-10-28; saves and Reforged Options 2026-10-30)

| Area | Status |
|---|---|
| Game logic | Both story routes (Eirika and Ephraim) play from power-on to the end credits. The post-game content and the side features (houses, villages, shops, arenas, supports, promotions, keys) are covered by the recorded test routes. |
| Pictures | Checked frame by frame on the recorded routes. Listed exceptions remain (see ACCURACY.md). |
| Sound | Checked record by record on the story routes and monkey runs. Listed exceptions remain. |
| Scenes | Every story scene of both routes was watched (not skipped) and compared tick by tick and frame by frame (coverage round 12: Eirika's story twice, at two text speeds, and Ephraim's). A few turns between lost bot runs and their continuation were not watched. |
| Input and timing | Keyboard and gamepad input. Frame pacing at the GBA's rate (59.7275 Hz), except while fast-forward is held. |
| Saves | Save files are the GBA's 32 KB raw SRAM image (mGBA-compatible `.sav`). All three save slots, Copy Data and Erase Data, and a suspend moved between the PC and mGBA both ways, were checked against mGBA on 2026-10-30 ([ACCURACY.md](docs/port/ACCURACY.md)), and again on the integration branch `port-rfo-int8` (67 checks, mGBA and the Windows build included, all passed). Late-game saves are not yet checked. |
| Reforged Options | Built, in the builds players run: danger zone on Select, fast-forward, unbreakable weapons, casual mode, growth rates on Select, original-game bug fixes (the Pierce freeze, Riev's Chapter 20 turn-1 softlock), HP bars & warnings, and a Cheats screen (infinite gold, growths, EXP). Off at first except fast-forward and the bug fixes ([PATCH-NOTES.md](docs/port/PATCH-NOTES.md)). Checked by scripted runs (DIVERGENCES.md, PB-2 to PB-12). Option sets, more bug fixes, rebindable controls and a randomizer are planned, not built ([MODDING.md](docs/port/MODDING.md)). |
| Windows | Window build (`fe8pc-sdl.exe`). Hand-tested by the owner on several sessions (2026-10-25 to 2026-10-28): reported working very well. |
| Linux | Window build. Tested through automated runs. |
| Link cable | **Not started.** The Link Arena entry is hidden in the builds players run. |

The comparison is against the GBA build of the same game, run in mGBA 0.10.5 with the same button presses. See [ACCURACY.md](docs/port/ACCURACY.md) for what this covers and what it does not. In short:

- Not every path through the game has been checked. A path that no recorded route takes is not compared.
- The project has no GBA BIOS dump, so the reference uses mGBA's built-in BIOS. Its known limits are listed in ACCURACY.md.
- Deliberate differences from the GBA (for example the hidden Link Arena entry and the Reforged Options entry) are listed in [DIVERGENCES.md](docs/port/DIVERGENCES.md) and summarised in ACCURACY.md.
- The tick-by-tick comparisons run on test builds, which leave Reforged Options out; the save checks used the builds players run (67 checks against mGBA, passed on the integration branch `port-rfo-int8` with the Linux and Windows builds players run). The options were checked on their own ([ACCURACY.md](docs/port/ACCURACY.md), sections 1 and 6).

## Bring your own ROM

**No game data is distributed with the PC port.** This includes the ROM, extracted graphics, sound and text.

To play, you need your own copy of the original USA ROM:

- *Fire Emblem: The Sacred Stones* (USA), 16 MiB;
- SHA-1 `c25b145e37456171ada4b0d440bf88a19f4d509f`.

The port checks the SHA-1 before the game starts. It refuses hacks, translations, patched ROMs and cut-short copies, and says which one it found.

The game is by Nintendo and Intelligent Systems. All rights in the game belong to them.

## Documents

| File | For | Contents |
|---|---|---|
| [PLAYING.md](docs/port/PLAYING.md) | Players | Setup, controls, saves, Reforged Options, troubleshooting |
| [PATCH-NOTES.md](docs/port/PATCH-NOTES.md) | Players | What is on by default that the GBA game does not do (fast-forward, the original-game bug fixes), and the planned fixes |
| [BUILDING.md](docs/port/BUILDING.md) | Developers | Requirements, build targets, test harness overview |
| [ACCURACY.md](docs/port/ACCURACY.md) | Developers, modders, players | How fidelity is verified, known limits, the deliberate-difference policy |
| [MODDING.md](docs/port/MODDING.md) | Modders | Where the code is, where PC code lives, Reforged Options (built) and the planned options |
| [CREDITS.md](docs/port/CREDITS.md) | Everyone | Every project and person the port uses, with licence as recorded |
| [LICENSE-PORT.md](docs/port/LICENSE-PORT.md) | Everyone | The scoped MIT licence of the port's own work (in the tree as the root [`LICENSE`](LICENSE)), and what it covers |
| [LICENSE-PORT-FILES.txt](LICENSE-PORT-FILES.txt) | Everyone | The files outside `port/` that the port changes (`M`: the licence covers its changes) or adds (`A`: the licence covers the file), generated from git |

## Credits

The port builds on the FireEmblemUniverse decompilation of the game and on the community work around it: naming and address references, the pret agbcc compiler and build tools, the mGBA emulator (as a test oracle only), SDL3 (the window and sound layer), a sound-driver translation from pokeemerald-expansion-native, and test data. The name and ideas of Reforged Options come from FE8 Reforged and other FEUniverse authors (ideas only, no code). The full list is in [CREDITS.md](docs/port/CREDITS.md).

## Licence

**MIT License (scoped), for the port's own work only** (the owner's decisions of 2026-10-28, D5, and 2026-10-30, LIC-1, LIC-2 and LIC-4). It is the repository root's [`LICENSE`](LICENSE), with its scope written into the licence terms. It covers the files in `port/`, except four that keep their own MIT notices (the sound code from pokeemerald-expansion-native and the mgba-emu/suite test values). It also covers the port's own changes to the decompilation's files (mostly the `PC_PORT` blocks), to the extent they are separable from the decompiled code, and the files the port adds outside `port/` (such as `LICENSE` itself); both kinds are listed in [LICENSE-PORT-FILES.txt](LICENSE-PORT-FILES.txt). It does not cover the game, which belongs to Nintendo and Intelligent Systems, or the decompilation itself, which has no licence statement of its own; nor SDL3 and the other third-party software, which keep their own licences. [LICENSE-PORT.md](docs/port/LICENSE-PORT.md) has the same text and explains it. Copyright holder: somecallmetim_ and the FE8 PC port contributors.
