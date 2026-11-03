# Accuracy: how the PC port is checked

> Figures are as of 2026-10-28 unless a later date is given.

## 1. What "identical" means here

The goal is that the PC port makes the same decisions, in the same order, as the GBA build, given the same button presses. It is checked on game state, pictures, sound and saves. It is not a claim about every hardware detail beyond the limits listed in section 5.

"Identical" is a claim about the game itself. The tick-by-tick comparisons with the GBA run on test builds (the save checks and the attract-mode runs use the builds players run; section 6), and test builds leave out Reforged Options (section 6) completely: `PORT_PLAYED_BUILD` is 0 when `FE8PC_TESTBUILD` or `FE8PC_DEBUG` is set (`port/include/port.h`), the Gate 0 binary is built with `FE8PC_DEBUG`, and every Reforged hook and file is compiled only when `PORT_PLAYED_BUILD` is 1. The builds players run have Reforged Options. With its defaults they play as the GBA's, apart from the two original-game freezes that its default bug fixes stop (and the Pierce fix's pacing); section 6 says how that was checked.

## 2. How it is checked

The GBA build and the PC build run from the same starting point (power-on, a chapter save, or a saved state) with the same button presses. The GBA build runs in mGBA 0.10.5.

| What is compared | How |
|---|---|
| Button presses | One 10-bit mask per logic tick, fed to both sides. |
| Game state, every tick | The game clock, the battle and play-state structures, and hashes of the shadow video state (the LCD control buffer, palettes, tilemaps and sprite table). |
| Pictures | Each frame in 15-bit colour, with mGBA's 8-bit blend switched on for checks. A 15-bit frame hash every 60 ticks in long runs; every frame in the frame checks. |
| Sound | The sound driver's state and its output, record by record and byte by byte, including the sample lanes. |
| Saves | The 32 KB SRAM image at the start, at chapter saves, and at the end of every long-test route (both sides write it; it must be equal apart from the play clock and a few unused bytes the original game fills from leftover memory). |
| Timing | Logic runs once per frame. Pacing is at the GBA's rate, 59.7275 Hz. |

### Lag

The GBA sometimes runs out of time within a frame. When that happens it skips a screen update, which the logic then sees as a lag frame. The PC never runs out of time, so it never lags.

Comparisons are made per logic tick. Where the only difference between the two sides is the GBA's lag, the compare reports `LAG-ONLY`. A replay of the PC run, with the GBA's lag frames placed at the GBA's positions, must reproduce the difference exactly for the verdict to stand. That replay is a test tool. The player never sees it.

### Raster timing

Some screen effects change the picture in the middle of a frame. On the GBA the timing of those changes depends on the processor, and the PC cannot reproduce it exactly. A frame that differs only because of that timing is labelled "raster-timing frame" and counted apart. The label is given only when a replay shows that the difference comes from that timing alone.

## 3. Coverage and results

| Check | Result (2026-10-28) |
|---|---|
| Default check set (23 rows) | 23 of 23 pass. |
| Long check set (143 rows) | 139 pass, 4 known limits, 0 fail. The known limits are listed in section 5. The Reforged Options branch (`b99518e6`) gave the same on 2026-10-30. |
| Eirika's story | Played from power-on to the end credits, including her final chapter with her real army. |
| Ephraim's story | Played from power-on to the ending on the test routes. The last part of his final chapter ran from a boosted save. His real post-game is not yet covered (section 4). |
| Post-game | Tower of Valni floors 1 to 8. Lagdou Ruins floors 2 to 10. Floor 10 was won with a test-only durability cheat. |
| Skirmishes | Every skirmish node that a story save offers (nine nodes). |
| Side features | Houses, villages, shops, the secret shop, arenas, supports, keys and promotions. |
| Item gallery | Every item used once, with test cheats. The run differs only at listed limits of the test tool. |
| Watched scenes | Eirika's whole story was watched twice (about 5.8 million ticks, 5.0 million frames), including every scene. Every game value was equal. |
| Sound (coverage round 12 sweep) | 86 of 90 runs equal record by record. The other four are at a recorded limit of the lag replay (section 5). |
| Pictures | Every frame that differs from mGBA on the checked runs is either a labelled raster-timing frame or a listed item. |
| Same-buttons TAS run | A run of 249,800 ticks of a published whole-game input file matched on 2026-10-23. The TAS's own timing matches the console only up to tick 767 (section 5), so later ticks are a same-buttons test, not a same-timing test. |
| Windows and Linux window builds | Compared frame by frame and sample by sample on test runs. The Windows build also ran 60,000 frames of attract mode with no window and no sound, and matched the end state. |
| Saves | A fresh boot with no save, and mid-chapter saves made in mGBA, continue on the PC. Saves written by the PC reload in mGBA. All three save slots: a short scripted story put a different game in each slot, suspended in each, quit and reloaded; it also copied a slot (Copy Data, including into slot 2 while slot 3 was full), erased one (Erase Data) and saved a chapter end into slot 2 while slot 3 was full. After every session the save file matched mGBA's (same keys, same start) except the play clock and a few unused bytes the original game fills from leftover memory, and each session changed only the slot it wrote. A suspend written by the PC resumed in mGBA, and one written by mGBA resumed on the PC, play on the same (4,000 and 5,500 ticks, with enemy-phase battles): the chapter and turn data, the random numbers and the map and screen the test harness records each tick matched (on lag frames through its lag replay), and the save written at the end, units included, matched byte for byte apart from the play clock and unused bytes. Checked on the Linux and Windows builds players run (aff1f2d4, 2026-10-30), and again on the integration branch `port-rfo-int8` (`make -f port/check-all.mk CHECK_SAVES=1`, run at `57a2d601`, which `1a411bc0` equals apart from the two root licence files): 67 checks passed, with mGBA and the Windows build both run. That command reruns these checks. The long check (CHECK_LONG, 2026-10-31) also compares the save memory at the end of every route that reaches one, through both stories and the post-game: 107 of 109 equal; the other two differ only in a few death and loss records that mGBA's open-bus reads change (OL-7, P-44). **Late game (2026-11-03):** the same scripted checks from six late-game saves with the real army (Eirika's Chapter 20 and final chapter, Ephraim's final chapter part 1, his clear save on the post-game world map, Tower of Valni floor 8, Lagdou Ruins floor 10): a full game save into a second slot from the preparations or the world map, a suspend, then an enemy phase (on Lagdou Ruins floor 10 with two deaths and their records), and each late-game suspend moved between the PC and mGBA both ways. Every save matched mGBA's apart from the known bytes, on the Linux, Linux window and Windows builds: `savechk_all.py --parts p53,p53b,p54,rfo,late`, 229 checks passed. |

Each route checks only the ticks it plays. Routes and monkey runs change as coverage grows. Their current list is in `port/test/inputs/` and in the check-all file.

## 4. Not yet verified

- **Saves, wider:** the extra-map and link-arena save blocks are not compared with mGBA (no session writes them). The late-game save checks (section 3) cover six saves, not every chapter.
- **Link cable:** not started. The Link Arena is hidden in the builds players run.
- **Hardware:** no GBA BIOS dump has been used, so there is no comparison with real hardware (section 5).
- **Coverage still to add**, from the project's own list:
  - a real army past Lagdou Ruins floor 4;
  - Lagdou Ruins floor 10 without the test cheat;
  - Ephraim's real post-game;
  - watched turns between lost battles and at the tails of routes;
  - sound and undefined-behaviour sweeps on the newest routes;
  - the Windows build on the story routes;
  - more side features.
- **Hand testing on Windows:** passed. The owner played the window build by hand in several sessions (2026-10-25 to 2026-10-28), including the gamepad, full screen (F11), speakers and a real battle, and reported it working very well, and played the release build again on 2026-10-31 ("looks and runs great"). A hand test is not a frame-by-frame comparison; running the Windows build on the story routes is still in the coverage list above.
- **Mac:** not tested.

## 5. Known limits of the reference

The reference is mGBA 0.10.5, which is accurate but not perfect. The project has no GBA BIOS dump, so the reference uses mGBA's built-in high-level BIOS. Where a trace difference comes from a known limit of the reference, the difference is judged against hardware facts (from GBATEK and mGBA's own hardware test vectors), not against the reference. The limit is recorded, and it is masked only where its scope is exact.

| ID | Limit of the reference | Effect on the comparison | Handling |
|---|---|---|---|
| OL-1 | Reads from the BIOS area can return bytes from mGBA's BIOS image, not the real latch values. | None seen in any traced field. | Listed. Never masked. |
| OL-2 | mGBA's BgAffineSet uses floating point. The port uses the hardware's integer method. | Affine matrices differ on a few ticks of one title-screen effect and one map. | Masked only where the values differ by at most 1 (matrix) or 201 (position). A rotated frame passes only with a replay that proves the rest. |
| OL-3 | The same as OL-2 for ObjAffineSet. | Differences of 1 on a few ticks. | Masked at that size. |
| OL-4 | mGBA records a DMA control bit that nothing reads back. | Sound capture only. | Listed, not masked. |
| OL-5 | mGBA's debug print window sign-extends some cartridge reads past the 16 MiB image. The GBA zero-extends them. | Six background cells on one chapter's map. | Masked, only in that chapter, only for those exact values. |
| OL-6 | mGBA cuts horizontally flipped text tiles at window edges. | Frames with a minimap window. | Check runs use a switch that copies the cut. The shipped build draws as the hardware does. |
| OL-7 | After a DMA, mGBA can return a stale word for an open-bus read. | Rare: a few loads on one route, one saved rank value on one ending path, and in two lag routes a few death and loss records in the end save (the game reads a missing unit there, and mGBA returns the last DMA's value). | Listed. The rank value is masked narrowly. The two routes' save bytes are known limits, byte by byte (P-44); any other byte still fails. |

Other limits:

- mGBA's BIOS decompresses LZ77 data in a different time from a real BIOS. Lag frames that land inside a long decompression can therefore fall at different points. The lag positions around those calls are mGBA's, so they may differ from a console's.
- The published TAS input stays in step with the console's timing only up to tick 767 on mGBA. After that it is a same-buttons test.

## 6. Deliberate differences from the GBA

**The policy:**

1. PC-only code sits behind a `PC_PORT` guard in the shared source, with the original GBA line kept in the `#else` branch. The GBA build stays byte-identical.
2. Port-only code lives in `port/`.
3. Where the GBA reads a value it never set (an uninitialised local, or open bus), the PC gives the value the GBA would give. A stop of the crash alone is not enough.
4. The game itself is the GBA's. The builds players run add one optional screen, **Reforged Options** (title menu, Extras), with player options and cheats (PLAYING.md). Every option is off by default except two. Fast-forward acts only while Space or the right stick's click is held, and changes only the speed, never the game. The original-game bug fixes (the second exception, decided by the owner on 2026-10-30) stop two of the original's freezes, each reproduced first on mGBA and on the PC: Riev's Chapter 20 turn-1 softlock (that fix acts only at the freeze) and the Pierce freeze (that fix makes every Pierce hit with battle animations on wait like Sure Shot, freezing or not: the same results and RNG, and the same save apart from the play clock; a different pace, kept so by the owner's choice of 2026-10-30). With the defaults the game plays as the GBA's (faster while that key is held) but for those two fixes; the other change that stays is the Extras entry itself. PATCH-NOTES.md lists what is on by default, and the further fixes that are planned, not built. The port has no widescreen, filters, higher frame rates or scaling other than whole-number scaling. (Fast-forward runs the game faster but shows no more pictures a second: only every 2nd, 3rd, 4th or 8th frame.) Modders may add those later (see MODDING.md).
5. Played builds contain the differences the owner has chosen. They also hold a few test switches for scripted runs of the played builds, read only from environment variables; unset, they do nothing (PB-9, section 7). Test builds contain extra test code, under `FE8PC_TESTBUILD`. Release builds contain none of that test code.
6. Every difference is written in `DIVERGENCES.md`, with an ID, the GBA behaviour, the PC behaviour and the reason. A limit of the reference is written there too, as a limit, not as a difference.

**The differences a player or modder may notice:**

| Area | GBA | PC port | Entry |
|---|---|---|---|
| Extras menu | Shows Link Arena when its conditions are met. | Does not show Link Arena in played builds. The cable mode is not in the first release. | PB-1 |
| Extras menu | Shows Extras only when one of its entries is there. | Always lists Reforged Options (after Sound Room and Support, before Map), so the title menu always has Extras. | PB-2 |
| Display options: Danger zone on Select, Growth rates on Select, HP bars & warnings | Not there. | Off by default. When on, they only draw on the screen. Scripted runs gave the same game values on or off (danger zone: at every check line; growth rates: the game values at every check line, and RAM, video memory and sound on every frame once the stat screen is closed; HP bars: RAM, video memory, sound and I/O on every frame; both: the same `.sav`). | PB-3, PB-10, PB-11 |
| Fast-forward | Runs at the GBA's rate. | On by default, but acts only while Space or the right stick's click is held: the same frames run 2, 3, 4 or 8 times as fast, only every 2nd, 3rd, 4th or 8th is shown, and the sound is muted or sped up. Scripted runs with the key held gave the same RAM, I/O, video memory and sound on every frame. Window builds only. | PB-5 |
| Original-game bug fixes | Riev killed on Chapter 20's turn 1: the game freezes at the enemy phase. Pierce (animations on), a big hit that does not kill and another hit: the battle freezes. | On by default. Riev: the scene's camera and cursor steps are skipped (the same fix skips any camera or cursor command that points at a unit that is gone, in any scene; each such case freezes the GBA game). Pierce: waits for its effect and HP bar, as Sure Shot; Pierce battles with animations on run longer (the same results). Off: both freeze as on the GBA (checked: Riev, the same picture at the freeze; Pierce, the same hang, with 340 pixels of the name plate's gradient different, the kind the GBA checks label `LAG-ONLY`, section 2). Scripted runs with no Pierce and no missing unit gave the same frames and .sav on and off. Not tested: an enemy Wyvern Knight's Pierce (attacker on the left), and a played freeze with stats inside the class caps (the reproduction used a save edit with Pow 31). Riev's death was made by a save edit (what a kill leaves in the suspend), not a played kill. | PB-12 |
| Gameplay options and cheats: Unbreakable weapons, Casual mode, Infinite gold, Growths, EXP gained | Not there. | Off by default. Off, the game's own code runs. On, they change the game. | PB-4, PB-6, PB-7 |
| Save mark | Nothing beside the save. | A save made with a gameplay option or cheat on is marked in a side file, `<save>.reforged`. The `.sav` stays the raw image mGBA reads. | PB-8 |
| Settings file | None. | `reforged-options.txt` in the per-user folder. Never in the `.sav`. | PB-2 |
| Test switches in played builds | None. | Environment variables only, for scripted runs of the played builds. Unset, nothing changes. | PB-9 |
| Save file | In the cartridge's save memory. | A `.sav` file next to the ROM, with a fallback folder (PLAYING.md). | Port layer |
| Lag frames | Skips a screen update when the logic overruns a frame. | Never skips. | Section 2 |
| Compiler safety checks | Not applicable. | Off in played builds, on in test builds. | Build |
| Save-code typo | As written. | Corrected on the PC path only. | DIVERGENCES.md |
| Colour blend | 5-bit hardware maths. | The same 5-bit maths. The 8-bit blend is used only in checks against mGBA. | Port layer |

Other differences are internal and do not change what a player sees. They are all in `DIVERGENCES.md`.

The save checks of section 3 and check-all's two 60,000-frame attract-mode rows run the builds players run, with Reforged Options in them. The other checks of sections 2 and 3 run test builds, which have none of it (section 1). The Reforged notes record as not run: a real keyboard or gamepad press, and a Windows run of the options.

## 7. Test-only code

The test builds have extra code: the test cheats, the debug-menu shortcut and the harness hooks. Release builds contain none of it. The loaded code and data of the Linux builds and the Windows exe were compared section by section with a tree that has no test code. The only differences were the build ID and the debug information. (That comparison is recorded for `port-integrate4`, before Reforged Options came in. `DIVERGENCES.md` records no later run of it.)

It works the other way round for Reforged Options. Test builds have none of its code. On the Reforged branch the Gate 0 test build's objects were compared with an independent build of `b99518e6` (the branch before growth rates and HP bars were added): 454 the same, 0 different, and 2 new files with no code in them; the binary has no `Reforged_` symbol. The hooks that the Extras label (`port-rfo-label`) and the original-game bug fixes (`port-retailfix`) add to the game's own files are macros placed on existing lines (appended, or in place of the original return value) that expand to the original code in test builds and on the GBA: on both branches the Gate 0 test build's 456 objects and its binary were byte-for-byte those of `aff1f2d4`, and the GBA build stayed `fireemblem8.gba: OK`. The builds players run have Reforged Options, and with it a few test switches read only from environment variables: `FE8PC_RFO_CHECK`, `FE8PC_RFO_TEST_FALL`, `FE8PC_FF_HOLD`, `FE8PC_FF_CHECK` and `FE8PC_FF_DUMP`, and `FE8PC_KEYSCRIPT` (one-off key presses for scripted runs, the save checks included; it is in every build, test builds and the Gate 0 binary too). Unset, each is one look-up and changes nothing. Set, they write check logs or hold fast-forward; `FE8PC_RFO_TEST_FALL` makes a chosen unit fall and `FE8PC_KEYSCRIPT` presses keys, so they change the game. They are listed in `DIVERGENCES.md` (PB-9).

Every played build also has the older developer switches of the port layer, which came before Reforged Options. They are kept on purpose (the owner's choice U9, 2026-10-30): the scripted checks of the played builds use them. They work the same way: environment variables only, which no key, menu or file of the game sets, and unset each is one look-up that changes nothing. Some of them can change the game or end it: `FE8PC_AUTOPRESS` presses keys, `FE8PC_DROP_BIOS_WRITES=0` lets a write the GBA ignores stop the program, and `FE8PC_FRAMES` and `FE8PC_SECONDS` quit, and `FE8PC_NULLPROBE` turns on a diagnostic mode that keeps a run going past faults (outside the Linux builds the program quits at once). The others change only the picture, the sound, the speed, the save file used or the log. Lines are on `aff1f2d4`. "All" means every played build: the headless core and FULL builds, the Linux window build, the headless Windows exe and the Windows window build.

| Switch | Builds | When set | Code |
|---|---|---|---|
| `FE8PC_AUTOPRESS="mask,period,start[,hold]"` | All | **Presses keys:** the keys in `mask` every `period` frames from frame `start`, held `hold` frames (2 if left out), joined with `FE8PC_KEYSCRIPT`'s presses and the live keys. How the headless builds are driven. | `port/hw/frame.c:322`, `:385-409` (`UpdateKeys`), `:430` |
| `FE8PC_KEYLOG=1` | All | Logs each change of the keys and each frame in which the game read a new press. | `port/hw/frame.c:375-383` |
| `FE8PC_FRAMES=N` | All | **Quits** after N frames (a window otherwise runs until it is closed; a headless run stops at 60). | `port/hw/main_pc.c:329`, `:339-342`; `port/hw/frame.c:468-470` |
| `FE8PC_SECONDS=S` | All | **Quits** after S seconds of wall-clock time. | `port/hw/pacer.c:49-50`, `:104-108` |
| `FE8PC_PACE=0` or `1` | All | Turns the pacing at the GBA's rate off (as fast as the PC can) or on (a headless run). Speed only. | `port/hw/pacer.c:46-47` |
| `FE8PC_DROP_BIOS_WRITES=0` | All | Turns off the fault handler that drops the game's stray writes to the BIOS area, as the GBA ignores them; the program then **stops with a fault** where the GBA goes on. | `port/hw/main_pc.c:128-133`, `:200`, `:343-344` |
| `FE8PC_NULLPROBE=1`, with `FE8PC_NULLPROBE_ROUTE`, `FE8PC_NULLPROBE_OUT`, `GATE0_KEYS` | Linux builds (elsewhere the program logs that it is not available and **quits** at start) | A diagnostic mode for open-bus research: BIOS-area and NULL-page reads and integer divisions trap and are given the GBA's values, and every site is recorded. On Linux the program also **quits** at start if the probe cannot reserve its shadow area. | `port/hw/nullprobe.c:913-978`, `:980-990`; `port/hw/main_pc.c:359-360`; `port/include/port_hw.h:53-56` |
| `FE8PC_RENDER=0` | All | Draws no picture. | `port/render/render.c:50-52` |
| `FE8PC_BLEND=mgba8` | All | Uses mGBA's 8-bit colour blend instead of the hardware's 5-bit maths (for checks against mGBA). Picture only. | `port/render/render.c:55-59` |
| `FE8PC_MGBA_BGCLIP=1` | All | Copies mGBA's cut of flipped text tiles at window edges (OL-6, section 5). Picture only. | `port/render/render.c:60-61`; `port/render/bg_line.c:231`, `:461` |
| `FE8PC_RENDER_LAYERS=mask` | All | Draws only the background layers in `mask`. Picture only. | `port/render/bg_line.c:66-75` |
| `FE8PC_DUMP_FRAMES=a[-b]`, `FE8PC_DUMP_DIR` | All | Writes those frames as picture files. | `port/render/render.c:64-78` |
| `FE8PC_AUDIO=0` | All | Opens no sound device. | `port/audio/audio_out.c:46-52` |
| `FE8PC_AUDIO_DUMP=prefix` | All | Writes the sound output to `prefix.lr16` and `prefix.fifo8`. | `port/audio/fifo_pc.c:102-106` |
| `FE8PC_SND_RECORD=path`, `FE8PC_SND_FROM=n` | All | Writes the sound record that the sound compare reads. | `port/audio/fifo_pc.c:107-113` |
| `FE8PC_AUDIO_LOG=1` | All | Logs the songs started. | `port/audio/fifo_pc.c:114-115`, `:261` |
| `FE8PC_SAVE=path` or `none` | The window builds, when neither `--save` nor `--no-save` is given | Uses that save file, or none. | `port/save/savefile_pc.c:38`, `:804`, `:817-822` |
| `FE8PC_USER_DIR` | The headless builds | Their per-user folder; nothing headless uses it. | `port/hw/platform_headless.c:160-163` |
| `FE8PC_SCALE=1` to `10` | The window builds | The window's starting size (a player setting, PLAYING.md). | `port/sdl3/platform_sdl3.c:223` |

The window builds also read the system's own folder variables (`XDG_DATA_HOME` and `HOME` on Linux, `APPDATA` on Windows) to find the per-user folder (PLAYING.md), and SDL3 reads its own `SDL_*` variables (for example `SDL_VIDEO_DRIVER`, which the scripted window runs set). Nothing in the game's own code (`src/`, `include/`) reads the environment. The test builds have more switches of their own (`FE8PC_TEST_*`, the Gate 0 hooks), which played builds do not have.
