# Credits

> Every source the port used, read or took an idea from, with its licence as the source records it.
>
> **Licence** is the licence as recorded in the sources. It is not legal advice. **Status** records how each source is used, and what was checked (2026-10-31).

## 1. The base

| Who | What we use | Licence (as recorded) | Status |
|---|---|---|---|
| FireEmblemUniverse, `fireemblem8u` decompilation ([link](https://github.com/FireEmblemUniverse/fireemblem8u)) | The base of the port. The game's C source compiles on the PC. Its symbol list and headers confirmed addresses and struct offsets. Its build path made the GBA check possible. | No licence file or statement | No licence given. Used as is (owner's decision, 2026-10-31): no ROM is shipped; graphics, maps and music are read from the player's own ROM |
| StanHash, mgfembp ([link](https://github.com/StanHash/mgfembp)) | Builds the FE6 link payload in another fork's tree. **Not used by the port.** The port's tree has its own copy of that data. | None (no LICENSE; no licence text in the README) | Not used. Nothing to settle for the port |

## 2. Names and address references

Names, addresses and symbol tables were used as reference. No code was copied from these sources. Where a name is used, it is credited here.

| Who | What we use | Licence (as recorded) | Status |
|---|---|---|---|
| laqieer, fork of the decompilation ([link](https://github.com/laqieer/fireemblem8u)) | Names and the fork's history (the naming branches). The fork is archived. | No licence file or statement | Used as is (owner's decision, 2026-10-31) |
| laqieer, `document-all-symbols` branch | Placeholder names, used as hints. 289 of our names match the branch. | No licence file | Not confirmed as a source. Listed for completeness |
| StanHash, FE6 decompilation ([link](https://github.com/StanHash/fe6)) | Names for FE6 twins of FE8 functions, weather graphics and classes. | None (no LICENSE; none detected by GitHub) | Used as is (owner's decision, 2026-10-31) |
| StanHash, FE-CLib ([link](https://github.com/StanHash/FE-CLib)) | Names and addresses; a reference table for naming. | No licence file | Used as is (owner's decision, 2026-10-31) |
| MokhaLeee, FE-CLib-Mokha ([link](https://github.com/MokhaLeee/FE-CLib-Mokha)) | Cross-check only. No applied name cites it. | No licence file | Not confirmed as a source. Listed for completeness |
| StanHash, EAStandardLibrary ([link](https://github.com/StanHash/EAStandardLibrary)) | Address names, reference only. One unapplied fill row depends on it. | No licence file | Read, not used (owner's decision, 2026-10-31) |
| FEBuilderGBA ([link](https://github.com/FEBuilderGBA/FEBuilderGBA)) | FE8 address-name tables, and a few identifiers taken as written. | README: "GPL3", followed by "use it freely with no limitations". No LICENSE file. | Used as is (owner's decision, 2026-10-31): a few names only, no code |
| FireEmblemUniverse, FE6 decompilation (fe6j) ([link](https://github.com/FireEmblemUniverse/fireemblem6j)) | Names for FE6 twins (the `FadeWithCallBack_Speed` steps and others). | None (no LICENSE) | Used as is (owner's decision, 2026-10-31) |
| MokhaLeee, FE7 decompilation (fe7j) ([link](https://github.com/MokhaLeee/FireEmblem7J)) | Names for FE7 twins (face and blink routines, stat-screen palettes). | None (no LICENSE) | Used as is (owner's decision, 2026-10-31) |
| Benst1996 (previously Huichelaar), FE8U_FogDR ([link](https://github.com/Benst1996/FE8U_FogDR)) | Notes that agreed with the decompilation on the `gBmSt` layout. | No licence file visible | Not confirmed as a source. Listed for completeness |

## 3. Code and notices in the port

| Who | What we use | Licence (as recorded) | Status |
|---|---|---|---|
| uehlbran and the pokeemerald-expansion-native contributors ([link](https://github.com/uehlbran/pokeemerald-expansion-native)) | Copied and adapted: the sound driver's ARM half in C (`port/audio/m4a_1_pc.c`) and the PSG sound model (`port/audio/psg.c`, `psg.h`). The frame pacer's method (`port/hw/pacer.c`), with no code copied. | MIT, scoped: "Copyright (c) 2026 pokeemerald-expansion-native contributors". The grant covers that project's own platform layer. Its translation of decompiled code is covered only "to the extent separable". The notice is in each file's header. | Settled for the MIT part: its notices are kept (LICENSE-PORT.md) |
| pret, pokeemerald, and rh-hideout, pokeemerald-expansion ([pret](https://github.com/pret/pokeemerald), [rh-hideout](https://github.com/rh-hideout/pokeemerald-expansion)) | Their `src/m4a_1.s`, the MP2K sound driver. The translation above is based on it. | None (no LICENSE; none detected) | Used as is, as for the decompilation (section 1) |
| mgba-emu/suite ([link](https://github.com/mgba-emu/suite)) | 123 hardware test vectors for the BIOS maths, as data (`port/test/bios/suite_vectors.h`). | MIT, "Copyright (c) 2015 Jeffrey Pfau". The full notice is in the header. | Settled |
| Dave Murphy, libgba (devkitPro) | `include/gba_sprites.h`, in the decompilation tree and compiled into the PC game. Its layouts and macros are used by the shop, save-menu, world-map and event code. | LGPL-2.0-or-later. "Copyright 2003-2006 by Dave Murphy" (the file's header). | Its notice is kept in the file, and the full source is published. Link: https://github.com/devkitPro/libgba |
| SDL3, libsdl-org ([link](https://github.com/libsdl-org/SDL)) | The window, the picture scaling, keyboard, gamepads, the sound stream, the frame timer and the save folder in both window builds. Version 3.4.2. | zlib. "Copyright (C) 1997-2026 Sam Lantinga". | Settled. On Windows, `SDL3.dll` ships unmodified with `LICENSE-SDL3.txt` |
| mingw-w64, runtime 13.0.0 | The C runtime linked into the Windows exes (`fe8pc.exe`, `fe8pc-sdl.exe`). | Mostly ZPL-2.1 or public domain. Some parts have other permissive notices (gdtoa, BSD-3, Sun). The GCC runtime library is under the GCC Runtime Library Exception and needs no notice. | Its runtime is in the Windows exe. Link: https://www.mingw-w64.org |

## 4. Decompilation build tools

These build the GBA ROM for the check. They are not in the PC game. Each MIT notice ships with its tool if the tools folder ships.

| Who | What we use | Licence (as recorded) | Status |
|---|---|---|---|
| pret, agbcc ([link](https://github.com/pret/agbcc), commit `da598c1d`) | The compiler that builds the GBA ROM for the check. The PC port uses the host gcc. | `gcc/`, `gcc_arm/`: GPL-2 (and LGPL-2 `COPYING.LIB`). `libgcc/`: GPL-2 with the unlimited-link exception. `libc/`: newlib, with BSD-style notices. | Never shipped |
| YamaArashi: `gbagfx`, `bin2c`, `mid2agb`, `scaninc` | Tools in the decompilation tree. | MIT, "Copyright (c) 2015 YamaArashi" (`gbagfx`, `scaninc`) and "Copyright (c) 2016 YamaArashi" (`bin2c`, `mid2agb`). | Ships with the tools |
| huderlem and Marco Trillo: `aif2pcm` | Tool in the decompilation tree. | MIT, "Copyright (c) 2016 huderlem" and "Copyright (c) 2005, 2006 by Marco Trillo". | Ships with the tools |
| camthesaxman: `textencode` | Text encoder for the GBA build. | MIT, "Copyright(c) 2018 camthesaxman" | Ships with the tools |
| lbersch, FIRST, Martin Moene: `inja.hpp` in `jsonproc` | Template library that generates `src/data/chapter_settings.h`. | MIT (lbersch, 2018). Also a BSD-style notice (FIRST, 2009-2018) and Boost 1.0 (Martin Moene, 2017-2019). Ship all three. | Ships with the tools |
| Niels Lohmann: `nlohmann/json.hpp` in `jsonproc` | JSON library for `jsonproc`. | MIT, "Copyright (c) 2013-2019 Niels Lohmann". It has MIT parts by Florian Loitsch and Bjoern Hoehrmann. | Ships with the tools |
| magical: `scripts/lzss3.py` | A script in the tree. | MIT, "Copyright (c) magical" | In the decompilation's tree; the build does not run it. Its MIT notice is kept in the file |

## 5. Test tools (never shipped)

| Who | What we use | Licence (as recorded) | Status |
|---|---|---|---|
| mGBA ([link](https://github.com/mgba-emu/mgba)), Jeffrey Pfau and contributors | The reference emulator, through libmgba 0.10.5 and its Lua 5.4 scripting. Facts read from its source. No renderer code copied. | MPL-2.0. Lua is MIT. | Never shipped. The old `ArcTan2` version was only in the development history, which is not published |
| Unicorn engine ([link](https://github.com/unicorn-engine/unicorn)) | Runs the decompilation's ARM text code next to the port's C, in a test script. Version 2.1.4. | GPL-2.0 for the engine. The Python binding is BSD-3. | Never bundled. The test script imports it |
| GCC's UndefinedBehaviorSanitizer and LLVM compiler-rt | The sanitizer's interface, followed in the port's test-only logging runtime. Messages worded as libubsan does. Nothing copied. | libsanitizer: University of Illinois (NCSA). GCC: GPL-3.0 with the runtime exception (not checked). | Test builds only. Never shipped. Links: https://gcc.gnu.org, https://github.com/llvm/llvm-project |

## 6. Facts and references (no code copied)

| Who | What we use | Licence (as recorded) | Status |
|---|---|---|---|
| Martin Korth, GBATEK ([link](https://problemkaputt.de/gbatek.htm)) | Hardware facts: BIOS behaviour, open bus, Div, Sqrt, the CpuSet rules, the video-memory byte rules. No text pasted. | None stated. Part of no$gba's help. | Facts only. Cite by section |
| VBA-M ([link](https://github.com/visualboyadvance-m/visualboyadvance-m)) | The affine formula and the sine table values, as facts. | GPL-2.0 | Facts only. No code |
| Cult-of-GBA BIOS ([link](https://github.com/Cult-of-GBA/BIOS)), DenSinH and fleroviux | A cross-check of the sine table and the affine formula. The generated table matches it in all 256 entries. | MIT, "Copyright 2020 - 2021 DenSinH and fleroviux" | Settled. Nothing copied |
| FireEmblemUniverse forum threads S15 to S22 (feuniverse.us), posts by Eebit, jjl2357 and others | Paraphrased facts in our notes. No code or text copied. | CC BY-NC-SA 3.0 (the forum's terms) | Background reading only. No port file cites them |
| TASVideos Sacred Stones thread (S24) | Paraphrased facts. | CC BY 2.0 (site content) | Background reading only. The thread's link was not recorded |
| mGBA blog post (endrift, 2020) and mGBA issue 1531 | Background facts on open bus after DMA, and on `CpuSet` side effects. | Not checked | Background reading only |

## 7. Test data (local only)

| Who | What we use | Licence (as recorded) | Status |
|---|---|---|---|
| Vykan12, Sacred Stones TAS ([TASVideos 1843M](https://tasvideos.org/1843M)) | The input movie, as a whole-game test route. Inputs only. | CC BY 2.0. Attribute the authors, link the publication page, and label the run as tool-assisted. | Local only. Never shipped |
| alyosha, GBA_replay_files ([link](https://github.com/alyosha-tas/GBA_replay_files)) | The frame-by-frame input list of the same run, the backup test route. Inputs only. | No licence file | Not shipped: kept locally for testing, not in the source or the release |

## 8. Ideas that shaped decisions (no code taken)

| Who | What we use | Licence (as recorded) | Status |
|---|---|---|---|
| Sacred Stones Reforged by Struedelmuffin ([FEUniverse thread](https://feuniverse.us/t/fe8-sacred-stones-reforged-v1-0-4/28486); a romhack for the US FE8 ROM) | The idea of an unlimited-durability option, which led to a test-only cheat. It also led to the player option "Unbreakable weapons". The name "Reforged Options" for the Extras entry that holds the PC port's options (built 2026-10-28 to 10-30), credited to FE8 Reforged for the name and the inspiration. No code, data, patch or text was read or used. | None. An idea only. | Thread checked 2026-10-31 (posted by Struedelmuffin) |
| Circles (circleseverywhere), Tequila and Alusq, FE8 map HP bars with effectiveness and crit warnings (FEUniverse threads; StanHash fixed bugs in the HP bars; Gamma, Vesly and Sme also worked on them) | The idea and the rules of the "HP bars & warnings" option (Off / Bars / Warnings / Both): HP bars on hurt units' map squares (11 steps, four colours), and one white or orange "!" on an enemy while you move a unit (white: effective against it; orange: a weapon whose own crit is above 24). No code, patch, graphics or text of theirs was read or used. The rules come from the owner's research notes. The pixels are our own, and the bar's fill and empty colours are the game's own map palette colours (its dark frame colour is our own). | None. An idea only. | Handles checked on FEUniverse, 2026-10-31 |
| Tequila, FE8 growth display (FEUniverse, "Tequila's Minor Assembly Shenanigans") | The idea of the "Growth rates on Select" option: Select on the stat screen swaps the stats for growth rates, Metis' +5 after the number, and stat names coloured by growth tier. No code, patch, graphics or text of the patch was read or used; the drawing uses the game's own number font and text colours. | None. An idea only. | Handle checked on FEUniverse, 2026-10-31 |
| TR143, FE6/FE7/FE8 fixed growths mode (a FEBuilderGBA patch, [FEUniverse thread](https://feuniverse.us/t/fe6-fe7-fe8-fixed-growths-mode/4482)) | The rule of the Cheats screen's "Growths: Fixed". No code, patch or text of the patch was read or used. The rule is our own code, written from its description. | None. An idea only. | Thread checked 2026-10-31 (posted by TR143) |
| circleseverywhere and Vesly, the FE8 CasualMode patch (FEUniverse threads) | The convention of the "Casual mode" option: a fallen unit retreats and returns in the next chapter, with its supports kept. No code, patch or text of theirs was read or used. | None. An idea only. | Handles checked on FEUniverse, 2026-10-31 |
| The FE8 Skill System (SkillSystem_FE8, [link](https://github.com/FireEmblemUniverse/SkillSystem_FE8)) | Its ModularEXP convention for the Cheats screen's "EXP gained" (multiply after the special cases, cap at 100), and its HpBars module's rules behind the HP bars row above. Ideas only, read by the owner's research helper, not by us. No code, data or text of the Skill System was read or used. | None. An idea only. | Its contributors are listed in its own repository (link) |
| The FEUniverse "Pierce Fix" thread ([link](https://feuniverse.us/t/pierce-fix/699); posted by Brendor), with the bug's descriptions on Serenes Forest ([link](https://serenesforest.net/the-sacred-stones/miscellaneous/hints-and-secrets/)) and the Fire Emblem Wiki ("Glitch", "Pierce") | The idea of the "Original-game bug fixes" option's Pierce fix (Pierce in a battle animation acts as Sure Shot, the FEU fix's description), and the description of Riev's Chapter 20 softlock (Fire Emblem Wiki). No patch bytes, code or text of theirs were read or used; both fixes are our own code, written from the decompilation (PATCH-NOTES.md). Found through the owner's research helper. | None. An idea only. | Thread checked 2026-10-31 (posted by Brendor, 2015) |
| lushen124, Universal FE Randomizer ([link](https://github.com/lushen124/Universal-FE-Randomizer)) | The option list of its FE8 set, as the frame for the planned randomizer. No code taken. | MIT | **Planned.** Credit when the randomizer is built, and keep the MIT notice if any code is reused |

## 9. Read, not used

These were read during research. Nothing from them is in the port.

- StanHash, EAStandardLibrary (see section 2, one unapplied row).
- laqieer, FE decomp portal (a link hub).
- Kurausukun's pokeemerald `pc_port`, NTx86/pokeemerald-sdl2pc, fuddlesworth/pokeemerald-native (no licence; ideas noted only).
- SAT-R/sa2 (no root licence; GPL renderer).
- 999sian/tmc (Project Picori), MatheoVignaud/tmc, VirtuaPPU and VirtuaAPU, zeldaret/tmc (GPL-3.0, or no licence).
- mstan's gbarecomp and FireRedLeafGreenRecomp (PolyForm Noncommercial), Lordingard's SacredStonesRecomp (no licence).
- arilsonbc's Fire Emblem 8 web port (no licence).
- ipatix/agbplay, huderlem/poryaaaa, ahigerd/mp2k-clef, Kermalis/VGMusicStudio (audio players; not driver-exact).
- nba-emu/NanoBoyAdvance (GPL-3.0). A possible second reference for disputes; not run.
- simonlindholm/asm-differ, encounter/objdiff (metadata only).
- Almamu's CrashBandicootXS-decomp, toucans' pokeemerald_SDL3.
- ez-me/gba-bios (GPL-2.0) and Normmatt/gba_bios (a disassembly of Nintendo's BIOS): rejected.
- mGBA's CInema test logs: local tests only.
- Other repositories read for background only.

## Links (checked 2026-10-31)

| Project | Link |
|---|---|
| libgba (devkitPro) | https://github.com/devkitPro/libgba |
| mingw-w64 | https://www.mingw-w64.org (source: https://sourceforge.net/projects/mingw-w64/) |
| SDL3 | https://www.libsdl.org (repo: https://github.com/libsdl-org/SDL) |
| mGBA | https://mgba.io/ (repo: https://github.com/mgba-emu/mgba) |
| TASVideos 1843M (Vykan12's Sacred Stones run; an "M" number is a published movie) | https://tasvideos.org/1843M |
| Universal FE Randomizer (lushen124, MIT) | https://github.com/lushen124/Universal-FE-Randomizer |
| Sacred Stones Reforged (Struedelmuffin) | https://feuniverse.us/t/fe8-sacred-stones-reforged-v1-0-4/28486 |
