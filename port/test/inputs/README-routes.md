# Gate 0 routes: new game to the ending on Eirika's route and to the final chapter on Ephraim's route

The first six key files start a new game on a blank save. Together they play
the Prologue and Chapters 1 to 5. Each file is a prefix of the next, so a
checkpoint can take the shortest one that reaches what it needs. Fifteen more
files carry on from chapter saves instead of power-on: Chapters 5x, 6, 7 and
8, then Eirika's Chapters 9 to 15 and Chapters 16 to 19 (the bot takes Eirika's route at the route
split), each from the save that ends the one before ("Routes from chapter
saves", below). Five more start from a **boosted** save (the Chapter 20
army with its stats raised to the class caps: test coverage only, not story
play) and play Chapter 20, the final chapter, the ending and the post-game
("Boosted starts", below); `route_e20.keys` plays Chapter 20 with the real army,
`route_e21.keys` and `route_e22.keys` the final chapter and the ending, `route_eend.keys`
the ending from its clear save
("Eirika's Chapter 20 and the final chapter with the real army", below). Fourteen more take Ephraim's route instead: Chapter 8 again
with the split's second entry, then Ephraim's Chapters 9 to 20 and his final
chapter's part 1 ("Ephraim's route", below); three more play his part 2, his
ending and his post-game from a boosted start (`route_ib22`, `route_ibend`,
`route_ibpost`, "Boosted starts"). Twenty-two more (coverage 11) play the post-game floor by floor (the Tower
of Valni's eight floors and the Lagdou Ruins' floors 2 to 9, boosted), five game features
(an arena fight, a support conversation, a chest, Suspend and Resume, a trainee's promotion)
and four world-map skirmishes ("The post-game floors" and "Feature routes and world-map
skirmishes", below); one more (coverage 12), `route_br10.keys`, plays the Ruins' last floor,
boosted and **cheat-assisted** (a test cheat: test coverage only, "Cheat-assisted routes",
below). More short feature files (coverage 12) visit villages and houses, use map shops,
Door and Chest Keys, lose and yield in arenas, take support conversations up to rank A, the lords'
promotion and a secret shop ("Feature routes (coverage 12)", below). All are made by the one bot, `oracle/route_bot.lua`
("One bot, two chains", below). The key file format, the tick definition and the capture
script are in `port/test/README.md`.

| File | Ticks | GBA frames | Clears | Ends at (first free cursor, turn 1) |
|---|---|---|---|---|
| `route_ch0_prologue.keys` | 5,803 | 6,164 | Prologue | Chapter 1 (tick 5,802) |
| `route_ch1.keys` | 21,273 | 22,314 | + Chapter 1 | Chapter 2 (tick 21,272) |
| `route_ch2.keys` | 38,391 | 40,325 | + Chapter 2 | Chapter 3 (tick 38,390) |
| `route_ch3.keys` | 67,834 | 71,524 | + Chapter 3 | Chapter 4 (tick 67,833) |
| `route_ch4.keys` | 108,024 | 113,523 | + Chapter 4 | Chapter 5 (tick 108,023) |
| `route_ch5.keys` | 135,505 | 142,340 | + Chapter 5 | Chapter 5x (tick 135,504) |

For comparison, the older "A every 90 frames from frame 4300" route reaches the
end-of-Prologue save at about frame 57,160. These routes write that save at
tick 5,008 (frame 5,314).

**Chapter numbers and `chapterIndex`** (`include/constants/chapters.h`):
indices 0 to 4 are the Prologue to Chapter 4. Index 5 is Chapter 5x
(Ephraim's "Unbroken Heart"), index 6 is Chapter 5, index 7 is Chapter 6,
index 8 is Chapter 7 and index 9 is Chapter 8. On Eirika's route, index 10
is Chapter 9, 11 is Chapter 10, 61 (0x3D) is Chapter 11, and 12 to 16
are Chapters 12 to 16. In play order,
Chapter 5 (index 6) comes before Chapter 5x (index 5). Milestone lines in
the key file headers say "chapter N", which means `chapterIndex` N.

## What the routes do

All of the routes share these steps:

- **Boot and menus.** A on the health and safety screen, the title and New
  Game. Difficulty is **Normal** (the second entry, `ROUTE_DIFF=1`). Normal
  sets `gPlaySt.config.controller` = 1, so the chapter tutorials never run.
  `chapterModeIndex` is 1. The old A-only route took the first entry, Easy,
  which plays the tutorials.
- **Cutscenes.** The opening story slides (`gProcScr_OpSubtitle`) and every
  skippable event are skipped with Start. Talk boxes that cannot be skipped
  (battle quotes) are advanced with A. Some scenes borrow another chapter's
  map, so `chapterIndex` briefly reads other values:
  - 16 and 64 before the Prologue (ticks 1,624 to 1,687).
  - 56 after Chapter 1 (ticks 19,913 to 20,277).
  - 63 and 27 after Chapter 3 (ticks 66,616 to 66,634).
  - 8, 9 and 7 at the start of Chapter 5x (ticks 135,367 to 135,394).
- **Options.** Defaults are left alone. Battle animations are on, so every
  fight goes through the battle animation screen (`gMainCallback`
  `MainUpdate_8055C68` / `InBattleMainRoutine`).
- **Map turns.** Each unit is moved with single d-pad presses, waiting until the
  cursor sprite is aligned to the tile. The sequence is:
  1. A on the unit.
  2. D-pad presses to the destination, then A.
  3. Up/Down in the menu to Attack, Wait or Seize, then A.
  4. A to take the default weapon.
  5. Right to cycle targets, then A to fight.

  Phases end on their own (auto-end) once every unit has acted.
- **World map.** Between chapters, L jumps the world map cursor between nodes
  until it is on the node whose chapter is in `gPlaySt.chapterIndex`. A there
  starts the chapter.
- **Battle preparations** (Chapters 4 and 5). Start on the preparations menu
  (`ProcScr_AtMenu`) begins the chapter with the default units and items
  (ticks 67,656 to 67,731 and 107,840 to 107,915, while
  `PLAY_FLAG_PREPSCREEN` is set).

Chapter by chapter (battle counts are from the `gate0_capture.lua` trace):

- **Prologue** (Defeat Boss, 3 battles).
  - Turn 1: Seth attacks the fighter at (8,6) from (8,5), and Eirika waits.
  - Turn 2: Seth kills O'Neill at (10,8) from (10,7) (tick 3,935).
  - `chapterIndex` becomes 1 at tick 4,944.
- **Chapter 1** (Seize (2,2), 16 battles).
  - Seth attacks whatever he can beat safely. Franz and Gilliam join him from
    turn 2.
  - Turn 3: Seth leaves Breguet (on the seize tile) at 2 HP. Breguet dies
    attacking Seth in that enemy phase (the battle at tick 12,216).
  - Turn 6: Eirika walks over and seizes (2,2) (her action at tick 19,853).
- **Chapter 2** (Defeat All, 20 battles).
  - Seth, Franz, Gilliam, Vanessa and Moulder clear the map.
  - Turn 6: Franz, then Gilliam, attack Bone (the battles at ticks 35,882 and
    36,465). The last enemy dies that turn, and `chapterIndex` becomes 3 at
    tick 37,431.
  - Ross and Garcia start as green units and are blue by Chapter 3. Neimi is
    blue at Chapter 3's start. The bot never uses Talk, so these joins come
    from the chapters' own events.
- **Chapter 3** (Seize (14,1), 20 battles).
  - Turn 12: Seth kills Bazba on the seize tile from (13,1) (the battle at
    tick 63,936).
  - Turn 13: Eirika seizes (14,1) (her action at tick 66,545).
  - The bot does not open doors or chests.
- **Chapter 4** (Defeat All, 39 battles). Cleared on turn 7. `chapterIndex`
  becomes 6 (Chapter 5) at tick 106,855.
- **Chapter 5** (`chapterIndex` 6, Defeat Boss, 30 battles). Cleared on turn 6.
  `chapterIndex` becomes 5 (Chapter 5x) at tick 134,701.

Eirika never attacks in these chapters and is still level 1 at the end.

Unit losses: no blue unit has the dead flag at the start of Chapters 1 to 5.
At the end of Chapter 5, Artur, Vanessa and Joshua do. Joshua joins only
through Talk, which the bot never uses.

## Milestones

A tick is one completed `OnMain` (`port/test/README.md`, "Trace"), counted from
0. Every milestone below was produced by `route_check.lua` replaying
`route_ch5.keys` open-loop. It is the same in the shorter files, up to their
end. Frames are from the `gate0_capture.lua` trace of the same file. RNG is
`gRNSeeds` (three u16 at 0x03000000) and `gLCGRNValue` (u32 at 0x03000008),
from `fireemblem8.elf`, read at the end of that tick. Ch is `chapterIndex`.

| Tick | Frame | Milestone | Ch, turn, phase | gRNSeeds | gLCGRNValue |
|---|---|---|---|---|---|
| 754 | 803 | New game set up, new-game save written to SRAM | 0, 0, 00 | 3671 90ea 1496 | 2fa650df |
| 1,080 | 1,155 | Prologue map loaded (beginning scene) | 0, 0, 40 | 3671 90ea 1496 | 1227be1f |
| 1,908 | 2,061 | Prologue turn 1 begins | 0, 1, 00 | f4a1 e163 66d8 | 1227be1f |
| 1,984 | 2,140 | Prologue first player phase, cursor free | 0, 1, 00 | f4a1 e163 66d8 | 1227be1f |
| 2,083 | 2,244 | Prologue first battle (Seth attacks) | 0, 1, 00 | 265b dc77 1ac0 | 31c0bc21 |
| 2,924 | 3,120 | Prologue first enemy phase | 0, 1, 80 | 265b dc77 1ac0 | 31c0bc21 |
| 4,944 | 5,237 | `chapterIndex` 0 → 1 (Prologue cleared) | 1, 2, 00 | ab08 88af b9a7 | 15e47d25 |
| 5,008 | 5,314 | End-of-Prologue game save written to SRAM | 1, 2, 00 | ab08 88af b9a7 | 14ddb4b0 |
| 5,064 | 5,380 | Chapter 1 map loaded | 1, 0, 40 | ab08 88af b9a7 | 101e73eb |
| 5,723 | 6,077 | Chapter 1 turn 1 begins | 1, 1, 00 | 17cf c361 7a64 | 101e73eb |
| 5,802 | 6,164 | Chapter 1 first player phase, cursor free | 1, 1, 00 | 17cf c361 7a64 | 101e73eb |
| 5,914 | 6,281 | Chapter 1 first battle | 1, 1, 00 | 9056 f921 fc77 | 3b0084ad |
| 6,510 | 6,900 | Chapter 1 first enemy phase | 1, 1, 80 | 9056 f921 fc77 | 3b0084ad |
| 19,913 | 20,865 | `chapterIndex` 1 → 56 (Chapter 1 seized) | 56, 6, 00 | 6e06 f389 3f24 | 36218bd6 |
| 20,278 | 21,252 | `chapterIndex` 56 → 2 | 2, 0, 40 | 12b9 d6bb 3539 | 36218bd6 |
| 20,342 | 21,327 | End-of-Chapter-1 game save written to SRAM | 2, 0, 40 | 12b9 d6bb 3539 | 13677ef5 |
| 20,581 | 21,581 | Chapter 2 map loaded | 2, 0, 40 | 12b9 d6bb 3539 | 3aa10c95 |
| 21,193 | 22,227 | Chapter 2 turn 1 begins | 2, 1, 00 | ceda b047 0480 | 3aa10c95 |
| 21,272 | 22,314 | Chapter 2 first player phase, cursor free | 2, 1, 00 | ceda b047 0480 | 3aa10c95 |
| 21,893 | 22,973 | Chapter 2 first enemy phase | 2, 1, 80 | 3777 ceda b047 | 3aa10c95 |
| 22,115 | 23,217 | Chapter 2 first battle (enemy phase) | 2, 1, 80 | 5fc4 773d a0ca | 3aa10c95 |
| 37,431 | 39,296 | `chapterIndex` 2 → 3 (Chapter 2 cleared) | 3, 6, 00 | 3d6a 31b2 f77a | 3964c2a9 |
| 37,495 | 39,373 | End-of-Chapter-2 game save written to SRAM | 3, 6, 00 | 3d6a 31b2 f77a | 10f02f94 |
| 37,729 | 39,622 | Chapter 3 map loaded | 3, 0, 40 | 3d6a 31b2 f77a | 02ebd752 |
| 38,314 | 40,245 | Chapter 3 turn 1 begins | 3, 1, 00 | 093c 2ba8 9b3a | 02ebd752 |
| 38,390 | 40,325 | Chapter 3 first player phase, cursor free | 3, 1, 00 | 093c 2ba8 9b3a | 02ebd752 |
| 39,053 | 41,039 | Chapter 3 first enemy phase | 3, 1, 80 | 093c 2ba8 9b3a | 02ebd752 |
| 40,553 | 42,648 | Chapter 3 first battle (enemy phase) | 3, 2, 80 | cc22 e352 7dcc | 02ebd752 |
| 66,616 | 70,194 | `chapterIndex` 3 → 63 (Chapter 3 seized; then 27, 3) | 63, 13, 00 | 5af8 2674 fb92 | 2a7fe174 |
| 66,641 | 70,245 | `chapterIndex` 3 → 4 | 4, 13, 00 | 2982 8e47 55f3 | 2a7fe174 |
| 66,705 | 70,320 | End-of-Chapter-3 game save written to SRAM | 4, 13, 00 | 2982 8e47 55f3 | 058943ed |
| 66,993 | 70,624 | Chapter 4 map loaded | 4, 0, 40 | 2982 8e47 55f3 | 09792188 |
| 67,757 | 71,444 | Chapter 4 turn 1 begins | 4, 1, 00 | a519 9ac5 d349 | 09792188 |
| 67,833 | 71,524 | Chapter 4 first player phase, cursor free | 4, 1, 00 | a519 9ac5 d349 | 09792188 |
| 67,954 | 71,651 | Chapter 4 first battle | 4, 1, 00 | 9440 4ba0 d8e8 | 094f2992 |
| 71,869 | 75,734 | Chapter 4 first enemy phase | 4, 1, 80 | 84c6 bb64 f52a | 2826a1f0 |
| 106,855 | 112,239 | `chapterIndex` 4 → 6 (Chapter 4 cleared) | 6, 7, 00 | 092e cefe c4b1 | 1fa2a790 |
| 106,919 | 112,314 | End-of-Chapter-4 game save written to SRAM | 6, 7, 00 | 092e cefe c4b1 | 28ffe5bb |
| 107,212 | 112,625 | Chapter 5 (index 6) map loaded | 6, 0, 40 | 092e cefe c4b1 | 3b026300 |
| 107,947 | 113,443 | Chapter 5 turn 1 begins | 6, 1, 00 | c12b 6a71 231a | 3b026300 |
| 108,023 | 113,523 | Chapter 5 first player phase, cursor free | 6, 1, 00 | c12b 6a71 231a | 3b026300 |
| 108,485 | 114,009 | Chapter 5 first battle | 6, 1, 00 | 0645 c833 2199 | 36f3230a |
| 110,156 | 115,749 | Chapter 5 first enemy phase | 6, 1, 80 | 0645 c833 2199 | 36f3230a |
| 134,701 | 141,415 | `chapterIndex` 6 → 5 (Chapter 5 cleared) | 5, 6, 00 | 39ff 8290 c5b5 | 18cc00aa |
| 134,765 | 141,492 | End-of-Chapter-5 game save written to SRAM | 5, 6, 00 | 39ff 8290 c5b5 | 214ab51d |
| 134,821 | 141,558 | Chapter 5x (index 5) map loaded | 5, 0, 40 | 39ff 8290 c5b5 | 02652cda |
| 135,428 | 142,260 | Chapter 5x turn 1 begins | 5, 1, 00 | b0a3 6269 0808 | 02652cda |
| 135,504 | 142,340 | Chapter 5x first player phase, cursor free | 5, 1, 00 | b0a3 6269 0808 | 02652cda |

How each milestone is detected (`port/test/oracle/route_lib.lua`):

- **Map loaded**: `gProc_BMapMain` exists, `chapterIndex` is N and the turn
  is 0. Scenes that borrow a map with turn 0 are reported too: chapter 16 at
  tick 1,624, and chapters 8, 9 and 7 at ticks 135,367 to 135,379.
- **Which chapters**: the story chapters and the Tower of Valni (indices 0
  to 0x2B), the Lagdou Ruins (0x2E to 0x37), Melkaen Coast (0x39) and both
  Chapter 11s (Eirika's 0x3D = 61, Ephraim's 0x3E = 62)
  (`route_lib.lua`, `L.milestoneChapter`). Maps that only scenes borrow
  (Castle Frelia 56, 63, 64, 68 and the rest) are left out. Until
  2026-10-21 the rule was "index 0 to 40", which hid both Chapter 11s, Tower
  6 to 8 and the Ruins; the older routes visit no index the change adds, so
  their headers are the same. Scenes now reported because of it: Chapter 8's
  ending borrows the Tower 8 map ("chapter 43 map loaded", `route_ch8.keys`).
- **Turn 1 begins**: after that, `chapterTurnNumber` ≥ 1.
- **First player phase**: `gProcScr_PlayerPhase` is idling in
  `PlayerPhase_MainIdle` with phase 00.
- **First battle**: `gMainCallback` is `MainUpdate_8055C68` or
  `InBattleMainRoutine` (battle animation screen).
- **Game save written**: the SaveBlockInfo entries of the three game slots
  change (SRAM 0x0E000064, 0x30 bytes, `include/bmsave.h`).
  - The suspend slot changes after every action and is not watched.
  - Between chapters the save menu opens (the slot list, then Save or
    Cancel), after the next chapter's index is already in `gPlaySt`. The
    bot's A presses pick slot 0 and Save (checked on the frames of
    `route_ch1.keys`, ticks 20,285-20,355). Corrected 2026-10-20: this line
    said no save menu appears.
  - These saves can start a Gate 0 run near the spot instead of from
    power-on (`port/test/README.md`, "Chapter saves").
  - The header lines say "end of chapter N" with N = `chapterIndex`.

Each file's header also lists the milestones up to its own end.

## Running

From the repository root, with `fireemblem8.gba` built (`OK`) and
`make -f port/test/Makefile oracle` done:

```
# Replay open-loop and print milestones, then the final chapter, RNG and units
GATE0_KEYS=port/test/inputs/route_ch1.keys \
  build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/route_check.lua 3000000
# (a file whose "# Generated" line names ROUTE_TICK=capture: add ROUTE_TICK=capture,
#  MGBA_RUN_BP=<GBA_BP of port/test/Makefile> and its GATE0_SRAM_IN; one that names
#  ROUTE_CHEAT=..., a cheat-assisted route: also that ROUTE_CHEAT, e.g. ROUTE_CHEAT=durability)

# Gate 0 trace of a route (TICKS = the file's tick count from the table)
make -f port/test/Makefile gba-trace KEYS=port/test/inputs/route_ch1.keys TICKS=21273
# (a cheat-assisted route: add GATE0_CHEAT=<its ROUTE_CHEAT>, e.g. durability, as `route`
#  and `chain-route` do by themselves: "Cheat-assisted routes" below)
```

`gba-trace` also writes raw buffers and a PNG every 60 ticks. The raw buffers
are 0x2869 bytes a tick: about 220 MB for `route_ch1.keys` and 1.4 GB for
`route_ch5.keys`. To get only the JSONL, run the capture script directly:

```
GATE0_KEYS=port/test/inputs/route_ch5.keys GATE0_TICKS=135505 GATE0_OUT=build/gate0/route_ch5.jsonl \
  build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/gate0_capture.lua 3000000
```

For `route_ch2.keys`, the capture took 33 seconds and `route_check.lua` took
18.

### PC check: through the lag replay

```
# The route through the lag replay, compared with the GBA (what check-all runs)
make -f port/test/Makefile route ROUTE_KEYS=port/test/inputs/route_ch1.keys ROUTE_TICKS=21273
make -f port/check-all.mk CHECK_LONG=1     # rows route-ch0-lag ... route-ch5-lag
```

The plain PC run (lag-free by design, PLAN.md 3.4) cannot follow these
routes. The keys were recorded against the GBA, whose lag frames shift game
state against the logic ticks: at tick 5,666 the GBA sets `gBmSt.lock` and the
PC two ticks later, so the Start press at 5,667 that skips Chapter 1's opening
scene comes too early on the PC, and from 5,669 the RNG differs (NULL probe,
`port-plan/pending/merged/nullprobe.md`). So the PC route check runs the route
through the lag replay (`port/test/README.md`, "Lag replay"): the PC run with
each GBA lag VBlank run where it came on the GBA (a harness run, never the
shipped game), compared on its own with the GBA trace of the same keys. It
passes only when that run equals the GBA on every tick (RESULT, with the clock
compared outright), ran the GBA's lag positions and main-loop events on every
tick, and completed all of the route's ticks. The route rows of
`port/check-all.mk` (`route-chN-lag`) run exactly this, shortest route first,
and skip the longer routes once the PC stops early on one
(`CHECK_ROUTES_ALL=1` runs them all). Compares of 60,000 ticks or more (ch3,
ch4, ch5) take turns under `~/.fe8-compare.lock`.

## Routes from chapter saves (Chapters 5x to 8, Eirika's 9 to 18)

These fifteen files do not start from a blank save. Each one boots both sides
with a chapter save in SRAM (`GATE0_SRAM_IN`, `port/test/README.md`, "Chapter
saves"), takes the title menu to Continue, plays one chapter, and stops 300
ticks after the game's own end-of-chapter save (the save menu, slot 0). That
end save is the next file's start save, so each step of the chain starts where
the last one ended instead of replaying everything from power-on.

| File | Start save (made from) | Ticks | GBA frames | Clears | End save (tick written) |
|---|---|---|---|---|---|
| `route_ch5x.keys` | `ch5x_start`: `route_ch5.keys`, `save:5` | 43,540 | 46,197 | Chapter 5x (index 5, Seize (13,7)), turn 19 | `ch6_start`: `save:7` (43,239) |
| `route_ch6.keys` | `ch6_start`: `route_ch5x.keys` from `ch5x_start`, `save:7` | 40,273 | 42,239 | Chapter 6 (index 7, Defeat Boss), turn 5 | `ch7_start`: `save:8` (39,972) |
| `route_ch7.keys` | `ch7_start`: `route_ch6.keys` from `ch6_start`, `save:8` | 41,952 | 44,187 | Chapter 7 (index 8, Seize (9,4)), turn 10 | `ch8_start`: `save:9` (41,651) |
| `route_ch8.keys` | `ch8_start`: `route_ch7.keys` from `ch7_start`, `save:9` | 87,077 | 91,368 | Chapter 8 (index 9, Seize (10,2)), turn 17; then the route split (Eirika) | `e9_start`: `save:10` (86,776) |
| `route_e9.keys` | `e9_start`: `route_ch8.keys` from `ch8_start`, `save:10` | 91,373 | 96,716 | Eirika's Chapter 9 (index 10, Defeat All), turn 21 | `e10_start`: `save:11` (91,072) |
| `route_e10.keys` | `e10_start`: `route_e9.keys` from `e9_start`, `save:11` | 106,799 | 113,048 | Eirika's Chapter 10 (index 11, Seize (15,1)), turn 17 | `e11_start`: `save:61` (106,498) |
| `route_e11.keys` | `e11_start`: `route_e10.keys` from `e10_start`, `save:61` | 82,082 | 85,650 | Eirika's Chapter 11 (index 61, Defeat All), turn 13 (after shopping on the world map) | `e12_start`: `save:12` (81,781) |
| `route_e12.keys` | `e12_start`: `route_e11.keys` from `e11_start`, `save:12` | 87,055 | 91,684 | Eirika's Chapter 12 (index 12, Defeat All), turn 16 | `e13_start`: `save:13` (86,754) |
| `route_e13.keys` | `e13_start`: `route_e12.keys` from `e12_start`, `save:13` | 89,876 | 94,366 | Eirika's Chapter 13 (index 13, hold out to turn 12), turn 12 (after shopping) | `e14_start`: `save:14` (89,575) |
| `route_e14.keys` | `e14_start`: `route_e13.keys` from `e13_start`, `save:14` | 108,382 | 115,374 | Eirika's Chapter 14 (index 14, Seize (9,5)), turn 31 (no shopping: the way to the armory is cut off) | `e15_start`: `save:15` (108,081) |
| `route_e15.keys` | `e15_start`: `route_e14.keys` from `e14_start`, `save:15` | 157,669 | 165,832 | Eirika's Chapter 15 (index 15, Rout), turn 28 (tactics v6: the turn search) | `e16_start`: `save:16` (157,368) |
| `route_e16.keys` | `e16_start`: `route_e15.keys` from `e15_start`, `save:16` | 99,184 | 105,218 | Chapter 16 (index 16, Seize (13,3)), turn 26 (after shopping) | `e17_start`: `save:17` (98,883) |
| `route_e17.keys` | `e17_start`: `route_e16.keys` from `e16_start`, `save:17` | 101,059 | 106,938 | Chapter 17 (index 17, Defeat Boss), turn 17 (after shopping) | `e18_start`: `save:18` (100,758) |
| `route_e18.keys` | `e18_start`: `route_e17.keys` from `e17_start`, `save:18` | 92,763 | 98,629 | Chapter 18 (index 18, Rout), turn 33 (after shopping) | `e19_start`: `save:19` (92,462) |
| `route_e19.keys` | `e19_start`: `route_e18.keys` from `e18_start`, `save:19` | 154,502 | 162,909 | Chapter 19 (index 19, Last Hope: hold out to turn 14, or the boss), turn 14 (no shopping: no armory the lord can walk to) | `e20_start`: `save:20` (154,201) |

The saves are game data and are never committed. Their first 32 KiB, as the
trace headers name them (`sram_fnv`): `ch5x_start` 742bd89e, `ch6_start`
a3361b16, `ch7_start` c6729800, `ch8_start` 6b39183a, `e9_start` c0403e36,
`e10_start` 996ac19b, `e11_start` 0865e05d, `e12_start` 40dea610,
`e13_start` 7d5c4020, `e14_start` e97d186b, `e15_start` 0f77c9a5, `e16_start` 95dd0bb9,
`e17_start` fca65c79, `e18_start` dcabfb26, `e19_start` d303fb6f, `e20_start` 0eb1f0e1. Each key file's `# Start:` header
line names its own start save and that value. Each save opens the title menu
on Continue, and each loads the same on both sides (`make chapsave` with
`resume.keys`, 2,600 ticks: `ch6_start` MATCH, `ch7_start` and `ch8_start`
LAG-ONLY; `ch5x_start` in `port/test/README.md`; `e9_start` and `e10_start`
LAG-ONLY, `e11_start` MATCH; `e12_start`, `e13_start`, `e14_start` and
`e15_start` LAG-ONLY; `e16_start` MATCH, `e17_start`, `e18_start`,
`e19_start` and `e20_start` LAG-ONLY, with the NULL probe on and no probe site).

```
make -f port/test/Makefile chain-save CHAIN_SAVE=e14_start   # every save of the chain, in port/test/saves/
make -f port/test/Makefile chain-route CHAIN=ch6             # route_ch6.keys from ch6_start, lag replay vs GBA
make -f port/check-all.mk CHECK_LONG=1                      # rows route-ch5x-lag ... route-e13-lag
```

`chain-save` makes a missing save with `sram-capture` from the route before it,
back to `route_ch5.keys` from a blank save (about 4 minutes for `ch5x_start`,
1.5 for each of the next three, about 3 for each of `e9_start` to
`e14_start`: about 26 minutes for all ten from an empty
`port/test/saves/`). The bytes are the same every time: all ten came out
equal to the saves the routes were made from (`e12_start` to `e14_start`
checked 2026-10-21 from a `port/test/saves/` holding up to `e11_start`).
`chain-route` checks the save against
the key file's `# Start:` line (a wrong save stops it, checked), then runs
`route` from it (about 3 minutes for ch5x to ch7; 5 to 6.5 for ch8 to e13,
whose compares also take about 3.4 to 4 GB with the raw dumps).

What these routes do, on top of "What the routes do" above:

- **Continue.** The bot's A presses go through the health and safety screen,
  the title and the title menu's first entry (Continue); the save loads at
  tick 708. The milestone line there says "new game started": the check sees
  `chapterModeIndex` become non-zero, here from the save.
- **Chapter 5x** (Ephraim, Forde, Kyle and Orson). The opening scene borrows
  maps 8, 9 and 7 (ticks 1,255 to 1,283), and the ending map 8 again (the
  "chapter 8 turn 1 begins" line at 43,155 is that scene, turn 19). Orson does
  most of the fighting; on turn 15 he attacks the boss, Zonta, on the throne
  (13,7). Ephraim walks round the walls and seizes on turn 19 (his action at
  tick 43,067). Nobody dies.
- **Chapter 6** (index 7, Eirika's army). The boss dies on turn 5, in the
  enemy phase (tick 39,908 is the chapter change). Ross dies in the turn 2
  enemy phase. The game records a loss in the current game save at once
  (`PidStatsRecordLoseData`, `src/bmsave-bwl.c`), so the "game save written
  to SRAM" line at tick 10,352 is that write, not a chapter end. Chapter 7's
  map follows with no world map.
- **Chapter 7** (index 8). On turn 7, in the enemy phase, a dropped item
  (a Knight Crest) reaches Seth, whose five slots are full. The bot sends the
  new item to the supply (ticks 32,931 to 32,951). Eirika seizes (9,4) on
  turn 10 (tick 41,471). The ending scene changes to chapter 68's 15x10 map
  (`Event25_ChangeMap`, tick 41,543), then the save, then Chapter 8's map
  (index 9) at 41,744.
- **Losses.** Dead at Chapter 8's start: Artur, Vanessa and Joshua (as at the
  end of Chapter 5) and Ross. Up to Chapter 7 the bot never talks, visits,
  opens doors or chests, shops or heals; the gold stays 5,000. Chapters 5x, 6
  and 7 have no Talk recruits (`characterBasedEvents` is empty in their event
  info).
- **Chapter 8** (index 9, tactics v3; Eirika's and Ephraim's armies). Turn 2:
  Seth opens the door at (20,7) from (20,8) (the way into the castle) and
  Natasha heals with Mend; Colm uses a Vulnerary (the first item use, tick
  9,938). Turn 3: Seth, his Iron Blade broken, takes Colm's Iron Lance by
  Trade. Kyle dies in the turn 3 enemy phase, Forde in turn 9. Healers act
  4 times and Vulneraries are used 8 times. Eirika seizes (10,2) on turn 17
  (her order at tick 85,736). The ending: `chapterIndex` 9 → 56 at 85,808,
  the save menu (game save with index 56 at 85,873), scenes on maps 6, 78,
  56 and 43 (Tower 8: "chapter 43 map loaded" at 86,313), the route split
  (Eirika's route, `chapterModeIndex` 2 from tick 86,633), 56 → 10 at 86,715
  and the save menu again (index 10 at 86,776: `e9_start`).
- **Eirika's Chapter 9** (index 10, Defeat All). The opening borrows maps
  63, 27 and 69 (ticks 1,760 to 1,788). Lute dies in the turn 6 enemy phase.
  Amelia (a Talk recruit for Franz or Eirika) comes in as an enemy around
  turn 8; no talker reached her, and she was defeated (not recruited). The
  last enemy dies in the turn 21 enemy phase (index 10 → 11 at 91,008).
- **Eirika's Chapter 10** (index 11, Seize). Tana talks to Innes (turn 5,
  tick 32,798), Innes to Gerik and Gerik to Tethys (turn 6), Gerik to Marisa
  (turn 12): all four join. Gilliam opens the doors at (10,9)/(11,9) from
  (10,10) on turn 8. Tana dies in the turn 9 enemy phase, Neimi in turn 13,
  Garcia in turn 16. Eirika seizes (15,1) on turn 17 (index 11 → 61 at
  106,434; the save at 106,498: `e11_start`; "chapter 61 map loaded" at
  106,796).
- **Losses and army.** Dead at Eirika's Chapter 11 start: Artur, Vanessa,
  Joshua, Ross, Kyle, Forde, Lute, Tana, Neimi and Garcia (Ephraim's party
  left at the split). The army: Eirika (still level 1), Seth, Franz,
  Gilliam, Moulder, Natasha, Colm, Innes, Gerik, Tethys and Marisa. Gold
  15,000 (the split's 10,000 for the leader).
- **Eirika's Chapter 11** (index 61, Defeat All; tactics v4). The save
  loads on the world map (tick 708). The bot shops first (ticks 878 to
  8,262, "Tactics v4" below): L to node 9 (Port Kiris), A walks Eirika there
  (1,130), A opens the node menu, Enter Armory, then six units in turn on the
  item screen; then L to node 11 and A (8,304). The chapter has a
  preparations screen (Start at 9,129). Nobody dies; L'Arachel and Dozla
  (green units) join at the end. The last enemy dies on turn 13 (61 → 65 →
  61 → 12 at 81,667 to 81,717; the save at 81,781: `e12_start`; Chapter
  12's map at 81,837, with no world map between).
- **Eirika's Chapter 12** (index 12, Defeat All). Saleh joins in the
  opening. Seth dies in the turn 9 enemy phase (the loss record rewrites the
  game save at 57,583, see Chapter 6) and Marisa in turn 11. The last enemy
  dies on turn 16 (12 → 13 at 86,690; the save at 86,754: `e13_start`).
  The world map follows (86,977), with no shopping: this run already wrote
  its end save.
- **Eirika's Chapter 13** (index 13, Hamill Canyon: defeat the boss or hold
  out until turn 12). The save loads on the world map; the bot shops at node
  9 again (Franz, Colm and Dozla), then enters the chapter (map at 4,142;
  the opening borrows maps 63, 27 and 73 at 4,685 to 4,712). Hold-out mode
  ("Tactics v4"): Eirika moves first and keeps away from the enemy, the
  others stay by her. Tethys dies in the turn 9 enemy phase. Turn 12's event
  wins the chapter (13 → 15 → 13 → 14 at 89,493 to 89,511; the save at
  89,575: `e14_start`). Amelia and Cormag (Talk recruits) are not recruited.
- **Losses and army at Eirika's Chapter 14.** Dead since Chapter 11: Seth,
  Marisa, Tethys. The army at the end of `route_e13.keys`: Eirika (still
  level 1), Saleh, Natasha, Franz, Gilliam, Moulder, Colm, Innes, Gerik,
  L'Arachel, Dozla and Ewan (who joins in Chapter 13's ending). Gold 13,355
  (8,355 after the shopping, then the ending's 5,000).
- **Eirika's Chapter 14** (index 14, Jehanna Hall: Seize (9,5); tactics v5).
  The save loads on the world map with the lord on node 12 (Hamill Canyon).
  The plan wants one weapon (a Steel Lance for Franz), but the only armories
  are behind the monsters on node 10 (Teraz Plateau), so the bot shops
  nowhere and walks to node 13 (map at 958; the opening borrows Chapter 15's
  map, index 15, at 1,506 to 1,518). The army starts in the sand at the
  bottom right; the castle's rooms are closed (roofs) and the only way to the
  throne is through the doors at (17,2) and (18,2). Turn 1: Saleh kills the
  soldier at (17,20) and gets its Door Key; turn 2 he opens the door at
  (22,17). Saleh dies in the turn 5 enemy phase. Franz, with the key of the
  soldier at (9,25), opens (17,2) on turn 15. From turn 25 Innes, Dozla,
  Gerik and Gilliam attack Carlyle on the throne; Dozla dies in the turn 27
  enemy phase; Gilliam's attack on turn 31 kills Carlyle and Eirika seizes
  (9,5) that turn (her order at 107,519). The ending borrows map 74
  (14 → 74 → 14 at 107,604 to 107,656); the save at 108,081 (index 15:
  `e15_start`). Rennac (a Talk recruit for L'Arachel) is not recruited: once
  his room is open he walks off and escapes by the stairs at (0,13) in the
  turn 11 enemy phase (tick 52,297; his unit slot is cleared then, see the PC
  check below).
- **Losses and army at Eirika's Chapter 15.** Dead since Chapter 14: Saleh,
  Dozla. The army: Eirika (still level 1), Natasha, Franz, Gilliam, Moulder,
  Colm, Innes, Gerik, L'Arachel and Ewan. Gold 13,355.
- **Eirika's Chapter 15** (index 15, Scorched Sand: Rout; tactics v6, the
  turn search, "Tactics v6" below; made with `ROUTE_SEARCH=1`). 46 enemies
  at the start, more from turns 3 to 9 (nine pegasus knights from the north
  on turns 3 and 4). Turn 1: Gerik uses his Hero Crest (Hero, tick 2,044);
  Eirika is not carried yet. Turn 2: Natasha takes Colm's Mend (Trade);
  Franz takes the Knight Crest and an Axereaver from the supply next to
  Eirika, Innes the Steel Bow; Gerik rescues Eirika (carried from here to
  the end); Ephraim, Duessel and Knoll arrive in the south-west in the enemy
  phase. Turn 3: Franz becomes a Paladin (10,168); Gerik buys two
  Vulneraries at the vendor (8,4). Ewan dies in the turn 3 enemy phase,
  Natasha in turn 4 (every policy of turn 4 lost her and Moulder; the
  look-back to turn 3 found no better way and the bot kept the old one),
  Colm in turn 5, Knoll in turn 7, Innes in turn 16. From turn 8 on no
  turn lost a unit; the enemies left fell from 40 (turn 8) to 1 (turn 27);
  the attack trials took on the bosses (the Hero at (1,1), the wyvern knight
  on the fort at (21,19)). The last enemy dies on turn 28 (15 → 16 at
  157,304; the save at 157,368: `e16_start`). The search played 157 policies
  beyond the first of each turn, 218 attack trials and one look-back to make
  the file's 157,669 ticks (about 40 minutes).
- **Losses and army at Chapter 16.** Dead in Chapter 15: Ewan, Natasha, Colm,
  Knoll and Innes. The army: Eirika (level 1), Franz (Paladin), Gilliam,
  Moulder, Gerik (Hero), L'Arachel, Ephraim, Duessel. Gold 11,555.
- **Chapter 16** (index 16, Ruled by Madness: Seize (13,3); tactics v6). The
  save loads on the world map; the bot shops at node 5's armory (ticks 878
  to 4,252: Franz sells the Dragon Axe and buys a Steel Lance, Gerik and
  Duessel buy Steel Axes), then enters the chapter (map at 5,081; the
  opening borrows maps 64 and 66). Myrrh joins in the opening. Reinforcements
  come on turn 8 (20 → 29 enemies); nobody dies. The last enemy dies on turn
  22, Eirika seizes on turn 26; the ending (maps 66, 67 and 71) makes Eirika
  and Ephraim Master Lords and gives Ephraim Siegmund (Eirika's Sieglinde
  goes to the supply: her five slots are full). 16 → 17 at 98,819; the save
  at 98,883: `e17_start`. 25 turns, all kept with no death; 75 extra
  policies and 56 attack trials.
- **Chapter 17** (index 17, River of Regrets: Defeat Boss). World map: Gerik
  sells a Divine (a drop) and buys a Steel Axe at node 5 (to 2,891, gold
  13,495). Turn 1 at 4,274 (25 enemies, reinforcements every second turn to
  turn 12); nobody dies. The boss dies on turn 17 (17 → 18 at 100,696);
  Syrene joins in the ending (her Talk recruiters, Tana, Vanessa and Innes,
  are dead); the save at 100,758: `e18_start`. 60 extra policies, 24 attack
  trials.
- **Army at Chapter 18:** Eirika and Ephraim (Master Lords, level 1), Franz
  (Paladin), Gilliam, Moulder, Gerik (Hero), L'Arachel, Duessel (with Garm, a
  drop), Myrrh, Syrene. Gold 13,495.
- **Chapter 18** (index 18, Two Faces of Evil: Rout). World map: Syrene buys a
  Killing Edge at node 21 (to 1,773, gold 12,195). Map at 1,901, turn 1 at
  2,665 (28 enemies). Syrene dies in the turn 20 enemy phase (the loss record
  rewrites the game save at 70,243). From turn 23 the last two enemies, a
  gorgon that never leaves its tile and a gargoyle that waits (AI2 3), stand
  in the south-east corner, far from the army: every policy tied, and the
  first try (the bot of `route_e17.keys`) waited there to turn 60. With the
  chapter-18 stall rules ("Tactics v6") the army walks down to them; the
  last enemy dies on turn 33 (18 → 19 at 92,398; the save at 92,462:
  `e19_start`). Franz picks up Vidofnir (a drop, turn 22). 215 extra
  policies, 194 attack trials.
- **Army at Chapter 19:** Eirika, Ephraim, Franz, Gilliam, Moulder, Gerik,
  L'Arachel, Duessel, Myrrh. Gold 12,195.
- **Chapter 19** (index 19, Last Hope: hold out to turn 14 or defeat Riev;
  Mansel, green on the throne (16,10), must live: his defeat quote is a game
  over, `src/data_battlequotes.c`). The save loads on the world map; no
  shopping (no armory the lord can walk to). Map at 951, turn 1 at 1,802 (28
  enemies; Ephraim, Gerik, L'Arachel and Duessel start in the north-west
  room). On turn 1 Franz rescues Mansel ("Tactics v6", PROTECT) and carries
  him to the end. Reinforcements on every turn to turn 9 fill the 50 red
  slots by turn 5. Gilliam dies in the turn 6 enemy phase (the loss record
  rewrites the game save at 61,908), L'Arachel in turn 8's, Duessel in turn
  10's, Gerik in turn 12's and Moulder in turn 13's. Turn 14's event wins:
  19 → 72 → 19 at 153,123 to 153,136 (the ending's map), 19 → 20 at
  154,137, the save at 154,201: `e20_start`. 127 extra policies, 1
  look-back, no attack trial.
- **Army at Chapter 20:** Eirika (Master Lord, level 1, 20 HP), Ephraim,
  Franz (Paladin), Myrrh. Gold 22,195.

**PC check** (2026-10-20, branch `port-bots9`): `make route` from each save
with `FE8PC_NULLPROBE=1` (the NULL probe, which also gets past integer division
traps), the lag replay compared with the GBA. Frames: frame15 every 60 ticks,
and every frame of the busiest windows (the most main-loop events and lag),
the load, the chapter end and any tick whose 60-tick frame differed.

| Route | PC ticks | Traced fields (every tick) | Frames that differ | NULL probe sites |
|---|---|---|---|---|
| `route_ch5x` | 43,540 of 43,540 | all equal | 57 of 5,350: all phase-banner fades (P-17) | 3, all H-1 write drops (`AnimDelete`) |
| `route_ch6` | 40,273 of 40,273 | all equal | 18 of 6,545: all banners | 3, all H-1 |
| `route_ch7` | 41,952 of 41,952 with the probe; 41,543 without | all equal | 41 of 5,689: 38 banners, 2 unit-window portrait frames, 1 whole-screen dark frame (32,960) | 5: 3 H-1 drops, 2 new reads |

Without the probe the PC stops with SIGSEGV at tick 41,543 of `route_ch7`:
the ending's map change leaves units below the 15x10 map, and
`RefreshUnitSprites` (`src/bmudisp.c:843`) reads `gBmMapUnit` rows that are
NULL. On the GBA that read is open bus. So the `route-ch7-lag` row of
check-all is INCOMPLETE until that is fixed. All three rows read DIFFERENT
while the phase-banner frames are not labelled (P-17, decision 9). Details and
the other new frame: `port-plan/pending/cov9-bot-step1.md`.

**PC check of Chapter 8 and Eirika's 9 and 10** (2026-10-21, the same way;
the compares ran without the raw dumps, see `port-plan/pending/cov9-bot-step2.md`):

| Route | PC ticks | Lag replay | Traced fields that differ | Frames that differ | NULL probe sites |
|---|---|---|---|---|---|
| `route_ch8` | 87,077 of 87,077 with the probe | 4,288 lag VBlanks, all at the GBA's positions; event counts equal | `gOam` 24 ticks (the 4 staff target selections, subtitle text); `gBG3TilemapBuffer` 6 ticks (86,709-86,714); `gPlaySt` 362 ticks (86,715 to the end: the chapter ranks) | 70 of 7,197: 60 banners, 6 subtitle text, 1 on a lag tick, 3 world-map name box | 6: 3 H-1 drops, 3 new reads (the Vulnerary) |
| `route_e9` | 91,373 of 91,373 with the probe | 5,340, all at the GBA's positions | `gOam` 55 ticks (8 staff target selections) | 93 of 7,129: 74 banners, 6 subtitle text, 1 on a lag tick, 7 world-map name box, 4 portrait tops, 1 battle-forecast frame | 6: 3 H-1, 3 new (the Vulnerary) |
| `route_e10` | 106,799 of 106,799 with the probe | 6,246, all at the GBA's positions | `gOam` 73 ticks (9 staff target selections) | 66 of 7,587: 49 banners, 11 subtitle text, 1 on a lag tick, 5 portrait tops | 6: 2 H-1, 3 new (the Vulnerary), 1 new (`ChangeUnitAi`) |

Without the probe the PC stops with SIGSEGV at the first Vulnerary a player
unit uses (battle animations on): tick 9,938 of `route_ch8`, 24,459 of
`route_e9`, 27,288 of `route_e10`. `PrepareBattleGraphicsMaybe` reads the
empty left battle unit's character and class (`src/banim-ekrbattleintro.c`
lines 1022-1023 and 1374-1392). `route_e10` also reads NULL at 35,554
(`ChangeUnitAi`, `src/eventscr3.c:35`, from an area event that changes the AI
of units by position when one of the positions is empty). The subtitle text,
the chapter ranks and the BG3 ticks are new findings too; all are in
`port-plan/pending/cov9-bot-step2.md`.

**PC check of Eirika's Chapters 11 to 13** (2026-10-21, the same way: the
whole route through the lag replay with `FE8PC_NULLPROBE=1`, compared without
the raw dumps (0.72-0.79 GB peak), frame15 every 60 ticks, then every frame
of the four busiest 600-tick windows, the shopping, the load, the chapter end
and around each 60-tick frame that differed; `port-plan/pending/cov9-bot-step3.md`):

| Route | PC ticks | Lag replay | Traced fields that differ | Frames that differ | NULL probe sites |
|---|---|---|---|---|---|
| `route_e11` | 82,082 of 82,082 with the probe; 16,624 without | 3,565 lag VBlanks in 1,526 ticks, all at the GBA's positions; event counts equal | `gOam` 101 ticks (the 4 staff target selections, subtitle text) | 49 of 6,693: 30 banners, 11 world-map name box, 8 shop and item-screen frames (busy-menu kind) | 7: 3 H-1 drops, 3 the Vulnerary, **1 new read** (`AiFindTargetInReachByCharId`, 3 hits at 16,624) |
| `route_e12` | 87,055 of 87,055 with the probe; 60,145 without | 4,626 in 1,980 ticks (3 early flush), all at the GBA's positions | `gOam` 43 ticks (3 staff selections) | 24 of 5,325: all banners | 6: 3 H-1, 3 the Vulnerary |
| `route_e13` | 89,876 of 89,876 with the probe; 10,698 without | 4,487 in 1,946 ticks (2 early flush), all at the GBA's positions | `gOam` 30 ticks (5 staff selections) | 86 of 6,811: 69 banners, 11 world-map name box, 5 shop and item-screen frames, 1 portrait top | 6: 2 H-1, 3 the Vulnerary, **1 new read** (`AiStaffPhysicRescue`, 33 hits from 10,698) |

Without the probe the PC stops with SIGSEGV at tick 16,624 of `route_e11`
(turn 1 enemy phase) and 10,698 of `route_e13` (turn 1 enemy phase), both
reproduced from a snapshot in seconds, and at the first Vulnerary (60,145) of
`route_e12`. Both new reads are AI loops over every unit that look a dead
unit's tile up in a map before checking that it is dead: a unit off the map
has x = -1 and keeps its last y (`src/eventcall.c:163` at each chapter end),
and a y past the map's last row reads a bogus row pointer (`gBmMapRange` row 0xFFFFFFFF in Chapter 11, address
0xFFFFFFFE; `gBmMapUnit` row NULL in Chapter 13, address 0xFFFFFFFF). On the
GBA those reads are open bus; with the probe every traced field stays equal.

**PC check of Eirika's Chapter 14** (2026-10-21, branch `port-bots10e`, the
same way: the whole route through the lag replay with `FE8PC_NULLPROBE=1`,
compared without the raw dumps (0.95 GB peak), frame15 every 60 ticks, then
every frame of the four busiest 600-tick windows, the world map, the chapter
end and around each 60-tick frame that differed; `port-plan/pending/c10-e1.md`):

| Route | PC ticks | Lag replay | Traced fields that differ | Frames that differ | NULL probe sites |
|---|---|---|---|---|---|
| `route_e14` | 108,382 of 108,382 with the probe; 42,721 without | 6,990 lag VBlanks in 2,807 ticks (no early flush), all at the GBA's positions; event counts equal | `gOam` 87 ticks (the 12 staff target selections) | 219 of 6,710: 188 raster-timing labelled, 31 in three of the staff selections (the help-box sprites, and the 2 frames after) | 7: 3 H-1 drops, 3 the Vulnerary, **1 new read** (`HandlePostActionTraps`, 1 hit at 52,297) |

Without the probe the PC stops at the first Vulnerary (tick 42,721, SIGSEGV,
the known heal-animation read). The new read: in the turn 11 enemy phase
Rennac (red, thief AI) escapes by the stairs at (0,13); his unit slot is
cleared (`pCharacterData` NULL) while he is still `gActiveUnit`, and
`HandlePostActionTraps` (`src/bmtrap.c:194`) then reads his character's
attributes through NULL (address 0x28). On the GBA that is a BIOS read; with
the probe every traced field stays equal.

**PC check of Eirika's Chapters 15 to 18** (2026-10-22, branch
`port-bots10e`, the same way; compare peaks 0.67-1.35 GB;
`port-plan/pending/c10-e2.md`):

| Route | PC ticks | Lag replay | Traced fields that differ | Frames that differ | NULL probe sites |
|---|---|---|---|---|---|
| `route_e15` | 157,669 of 157,669 with the probe | 8,162 lag VBlanks in 3,664 ticks (2 early flush), all at the GBA's positions; event counts equal | `gOam` 71 ticks (the 5 staff target selections) | 163 of 6,577: 155 raster-timing labelled, 7 in a staff selection, 1 on a GBA lag tick (2,413, the end of the Hero Crest promotion's lightning; not labelled) | 7: 4 H-1 drops, 3 the Vulnerary |
| `route_e16` | 99,184 of 99,184 with the probe | 6,032 in 2,536 ticks (1 early flush), all at the GBA's positions | `gOam` 69 ticks (6 staff selections); `gBG3TilemapBuffer` 7 ticks (98,806-98,812, the ending's tile change) | 182 of 6,855: all raster-timing labelled | 6: 3 H-1, 3 the Vulnerary |
| `route_e17` | 101,059 of 101,059 with the probe | 5,877 in 2,558 ticks, all at the GBA's positions | `gOam` 35 ticks (5 staff selections) | 105 of 5,113: all raster-timing labelled | 20: 3 H-1, 3 the Vulnerary, 14 the snag group (`BATTLE_HandleCombatDeaths`, 23,458) |
| `route_e18` | **62,369 of 92,763 with the probe** (SIGSEGV at 62,369); 6,479 without | 4,006 in 1,866 ticks up to the crash, all at the GBA's positions | `gOam` 62 ticks (8 staff selections); `gLCDControlBuffer` 32 ticks (two critical-hit map shakes, BG2/BG3 x and y swapped: the `BG0Shaker_Loop` argument order) | 149 of 4,927: 126 raster-timing labelled, 16 in the shake, 7 in a staff selection | 34: 5 H-1, 5 the Vulnerary and Myrrh's item, **24 in the three new groups below** (8 of them marked related to D-10, 1 an H-1 drop) |

`route_e18.keys` has three new NULL-page reads, none of them seen in an
older route:

- **6,479** (turn 1 enemy phase): the AI looks for targets in reach of a
  Gorgon Egg (class 0x62), whose movement-cost tables are all NULL, so
  `SetWorkingMoveCosts` copies the BIOS area as move costs and the flood fill
  reads NULL rows (`AiFindTargetInReachByFunc`, `src/cp_utility.c:194`).
  The plain PC stops here (SIGSEGV, address 0).
- **6,499** (each turn's trap update): every empty fire tile becomes a trap
  target with unit 0, and `PidStatsRecordTargetListDefeats`
  (`src/bmtarget.c:1256`) reads its character number through NULL. The GBA's
  open-bus reads give 0x42 and 0x9D; the probe's latch gives 0x04, so on the
  PC Franz gets a trap death and a loss (and SRAM writes) each time. Not in
  any traced field.
- **62,369** (turn 18 enemy phase, a gorgon's Stone as a map animation):
  `MapAnimMonsterStone_Loop1` decompresses `TsaSet_MonsterStoneMapAnimfx[10]`,
  one past the table, on its last frame (the ROM word there is 0x0001000E).
  On the GBA the open-bus type byte is 0x08 (a raw copy from the BIOS area,
  refused); with the probe's 0xA0 `Decompress` calls entry 21 of its 8-entry
  function table and the PC jumps to 0x02032F8C: SIGSEGV. So the PC check of
  Chapter 18 stops there (INCOMPLETE), with or without the probe.

**PC check of Chapter 19** (2026-10-22, branch `port-bots10e`, the same way;
`port-plan/pending/c10-e3.md`): `route_e19.keys` 154,502 of 154,502 ticks with
the probe; 8,406 lag VBlanks in 3,555 ticks, all at the GBA's positions;
**every traced field equal (MATCH)**. Frames: 6,352 compared in the second
pass, 52 differ, 51 of them raster-timing labelled; one is not (tick 153,334,
in the ending's "Sent Ivaldi" box: the PC's picture is darkened for that one
tick, the GBA's line events start at line 161; F-E3-1). NULL probe: 6 sites,
H-1 drops, the Vulnerary (43,429) and `IsUnitSlotAvailable`
(`src/bmunit.c:1504`) from `ShouldUNITBeLoaded`: the reinforcements fill all
50 red slots by turn 5 (first at 54,324; the scout's N2, here in story play).

## Boosted starts (test coverage only, not story play)

The story chain stops at Chapter 20 (`e20_start.sav`, not won: "Limits"). To
check the last chapters, the ending, the credits, the clear save and the
post-game on the PC anyway, these routes start from a **boosted** save: the
same army, but with every living unit's stats raised to its class caps. No
player of the real game has that army, so these files are labelled "boosted"
everywhere and never count as story play; the game code they run (the maps,
the bosses, the ending, the save menu) is the real one, and both sides boot
from the same save, so a Gate 0 compare from them is a fair check.

`port/test/oracle/sram_boost.lua` makes the save. It plays a key file like
`sram_capture.lua`, and at the end of tick `BOOST_AT` it writes in RAM, for
every blue unit that is not dead: max HP and HP = the class's max HP, power,
skill, speed, defence and resistance = the class's caps (`struct ClassData`
+0x13 to +0x18), luck 30, and with `BOOST_USES=1` every item's uses = a new
item's (unbreakable items left alone). Then the game goes on and writes its
own saves (with its own checksums); the SRAM is taken at `SRAM_AT=tick:T`.

| File | Start save (made from) | Ticks | GBA frames | Clears | End save (tick written) |
|---|---|---|---|---|---|
| `route_b20.keys` | `b20_start`: `boost_e20.keys` from `e20_start`, `BOOST_AT=1900`, `BOOST_USES=1`, `SRAM_AT=tick:2721` (the turn-1 suspend save of Chapter 20; title menu: Resume Chapter) | 164,619 | 172,837 | Chapter 20 (index 20, Seize (11,11)), turn 21, boosted (no loss) | `b21_start`: `save:21` (164,318) |
| `route_b21.keys` | `b21_start`: `route_b20.keys` from `b20_start`, `save:21` | 170,079 | 176,755 | the final chapter: part 1 (index 21, Defeat Boss: Lyon), turn 24; part 2 (index 22, Defeat Boss: the Demon King), turn 3; then the save menu, the ending and the credits to the title screen | `b_clear`: `tick:170079` (the clear save, slot 0, written at 133,972; the SRAM at the route's end) |
| `route_b22.keys` | `b22_start`: `route_b21.keys` from `b21_start`, `tick:123230` (the turn-1 suspend save of part 2; title menu: Resume Chapter) | 43,505 | 43,995 | part 2 again from its turn 1 (the Demon King, turn 2), the save menu, the ending, the credits, the title | (the SRAM at its end holds a clear save too, not kept) |
| `route_bend.keys` | `b_clear` (above) | 36,759 | 36,911 | Continue on the clear save: the game plays the ending again (`GameCtrl_CheckGameCompleteAndBranch`), the credits, the Fin screen, the title | (the SRAM is unchanged) |
| `route_bpost.keys` | `b_clear` (above) | 116,095 | 121,927 | the post-game: Extras, Map; the world map; Tower of Valni floor 1 (index 36), turn 3, then the retreat (a game save, 19,956); Lagdou Ruins floor 1 (index 46), turn 33; stops at floor 2's first free cursor (index 47) | `b_r2_start`: `tick:116095` (Ruins floor 2's turn-1 suspend; slot 0 the Tower save) |
| `route_ib22.keys` | `ib22_start`: `route_i21.keys` (Ephraim's part 1, won with the real army) from `i21_start`, `BOOST_AT=80205` (part 2's map loaded, before its turn-1 suspend save), `BOOST_USES=1`, `SRAM_AT=tick:80292` (title menu: Resume Chapter) | 59,907 | 61,315 | Ephraim's final chapter, part 2 (index 35, Defeat Boss: the Demon King), turn 7, boosted, no loss; the save menu (slot 0, the clear save, 22,948), Ephraim's ending (scenes on maps 19, 12, 64 and 16 from 23,009), the credits, the Fin screen (59,419), the title (59,606) | `ib_clear`: `tick:59906` |
| `route_ibend.keys` | `ib_clear` (above) | 37,611 | 37,765 | Continue on the clear save: Ephraim's ending again (from 716), the credits, the Fin screen (37,123), the title (37,310) | (the SRAM is unchanged) |
| `route_ibpost.keys` | `ib_clear` (above) | 81,060 | 84,866 | Ephraim's post-game: Extras, Map, the slot; the world map; Tower of Valni floor 1 (index 36) won on turn 2, the retreat (a game save, 19,472); Lagdou Ruins floor 1 (index 46) won on turn 14; stops at floor 2's first free cursor (index 47, 81,059) | `ib_r2_start`: `tick:81059` (Ruins floor 2's turn-1 suspend; slot 0 the Tower save) |

- `boost_e20.keys` is the bot's own world-map walk from `e20_start.sav` to
  Chapter 20's first free cursor (`ROUTE_STOP=pp:20`, 2,721 ticks: Franz buys
  a Silver Lance at the armory of node 23 at 1,313, the map loads at 1,946).
  At tick 1,900 the lord is walking to node 25. The boost (`b20_start.sav.json`
  lists it unit by unit): Eirika (Great Lord 1) 20 HP to 60, power 6 to 24,
  defence 6 to 22; Franz (Paladin 9) 41 to 60; Ephraim (Master Lord 6) 39 to
  60; Myrrh (Manakete 20) 38 to 60 (her other caps are 20); Rapier 32 to 40
  uses, Vidofnir 19 to 30, Reginleif 3 to 45, Divinestone 24 to 50.
- First 32 KiB (`sram_fnv`): `b20_start` 158b331e, `b21_start` cf224bb1.
  `b20_start` loads the same on both sides (`make chapsave`, `resume.keys`,
  2,600 ticks, release, NULL probe on: LAG-ONLY, no probe site; end SRAM 9
  bytes, the P-12 kind).
- `route_b20.keys`: made with `ROUTE_SEARCH=1 ROUTE_SEARCH_BASE=0` (the
  plain bot every turn unless a unit dies or ends under 40% HP; nobody did),
  `ROUTE_STOP=saveto:21`. Morva falls on turn 6; Ephraim picks Eirika up that
  turn (54,617), drops her on the seize tile on turn 20 (153,933), and she
  seizes on turn 21 (164,200). The game save for
  the final chapter is written at 164,318 and its map loads at 164,374 (no
  world map in between).
- `route_b21.keys` (`ROUTE_SEARCH=1 ROUTE_SEARCH_BASE=0`, `ROUTE_STOP=title:300`):
  the final chapter's part 1 (Lyon on (11,2); 34 enemies left after turn 1)
  is won on turn 24; `MNC3` takes the game straight to part 2 (index 22 at
  123,077, no save, no preparations); the Demon King (character 0xBE, 120 HP)
  falls on turn 3 (the rule "the final boss" below). Then
  `PLAY_FLAG_COMPLETE` (`GameCtrl_DeclareCompletedChapter`), the save menu
  (slot 0, the clear save, 133,972), the ending (`EventScr_EirikaModeGameEnd`:
  scenes on the maps of indices 19, 15, 64 and 16 from 134,033, the credits,
  the turn records and the character endings, 31,000 ticks on index 16), the
  Fin screen (169,587) and the title screen (169,778). The ending is played
  with A only (never Start), so nothing in it is skipped.
- `b_clear.sav` is the SRAM at the route's end (`sram-capture`
  `SAVE_AT=tick:170079`): the clear save in slot 0 (chapterIndex 22,
  `PLAY_FLAG_COMPLETE`) and the play-through record the ending writes
  (`SavePlayThroughData`). Continue on it plays the ending again
  (`GameCtrl_CheckGameCompleteAndBranch`: complete and not post-game); the
  post-game's world map is Extras, then Map (`EXTRA_MENU_OPTION_MAP`, which
  sets `PLAY_FLAG_POSTGAME`), then the slot.
- First 32 KiB: `b_clear` 4fd2d0fb, `b22_start` 38c44684, `b_r2_start` 015f3833.
- `route_b22.keys` exists for the PC check: the PC leaves `route_b21.keys`
  at tick 31,795 in part 1 (an Arch Mogall's Crimson Eye: its spell
  animation is a generated stub on the PC, `StartSpellAnimCrimsonEye`, the
  round's MB1), so the part 2, the ending and the credits are checked from
  `b22_start` (no Crimson Eye on the way). Its part 2 differs a little from
  `route_b21.keys`' (the Demon King falls on turn 2, not 3: the resumed game
  takes other cursor paths), the ending is the same script. The PC leaves
  it too, at tick 3,135 (the Demon King's summon in the turn-1 enemy phase,
  "PC check of the boosted routes" below), so `route_bend.keys` checks the
  ending: Continue on the clear save plays the whole ending again, with no
  battle before it.
- `route_bpost.keys` (`ROUTE_SEARCH=1 ROUTE_SEARCH_BASE=0 ROUTE_POSTGAME=1
  ROUTE_WM_NODE=26,27`, `ROUTE_STOP=pp:47`): Extras, Map, the slot (the
  post-game world map, `PLAY_FLAG_POSTGAME`), the lord walks to the Tower of
  Valni (node 26) and enters it (the preparations: Start, the four units);
  floor 1 (index 36) is won on turn 3; "Proceed to the next floor?" gets B
  (the retreat, `StartRetreatProcessing`; a game save at 19,956); the world
  map again, Lagdou Ruins (node 27), floor 1 (index 46, 38 enemies) won on
  turn 33; "Proceed?" gets A (yes), floor 2's map (index 47) and its first
  free cursor (116,094). The Tower's floor 4 (the scout's N1) is not on the
  way. (A try that answered every "Proceed?" with yes went through Tower
  floors 1 to 6 on the GBA, floor 4 included, and stalled on floor 6.)

**PC check of the boosted routes** (2026-10-22, `port-plan/pending/c10-e4.md`;
the lag replay with `FE8PC_NULLPROBE=1`, release build, two passes: frame15
every 60 ticks, then every frame of the busiest windows and fixed ones):

| Route | Traced fields | Frames (pass 2) | NULL probe |
|---|---|---|---|
| `route_b20.keys` | MATCH, 164,619 ticks | 6,549 compared: 83 raster-timing labelled; 2 not (17,006 and 108,278: the map darker for one frame under a "Sent a ..." box, the known G7 / F-E3-1 kind) | H-1 drops; the item heal group at 46,706 (P-21) |
| `route_b21.keys` | equal to 31,794; the PC leaves the GBA at 31,795 (an Arch Mogall's Crimson Eye: `[stub] StartSpellAnimCrimsonEye`, MB1) | 2,186 to 31,799: 42 labelled, 3 at the stub | H-1; the item heal group at 29,106 |
| `route_b22.keys` | equal to 3,134; the PC leaves the GBA at 3,135 (the Demon King's summon, turn 1 enemy phase: the fourth area tried, round (11,2), is all wall, terrain 0x3B; the GBA summons nothing there, the PC puts a monster in the wall; F-E4-1: `src/mapanim_summon.c` declares `CanUnitCrossTerrain` `int`, it is `s8`, and on the PC only its low byte is set) | 9 labelled before it | H-1, and a new H-1 write (`EkrDragonBodyAnimeSet54`, the Demon King's battle animation, 1,382; the plain PC passes it too) |
| `route_bend.keys` | MATCH, 36,759 ticks: the whole ending, the credits and the Fin screen | 1 labelled, 0 other | 7 sites from 19,063: `GetFaceDisplayBitsById` (`src/face.c:471`) reads a face that is not there in the ending's CG text (F-E4-2); the plain PC stops there (SIGSEGV) |
| `route_bpost.keys` | MATCH, 116,095 ticks | 6,682 compared: 182 labelled, 0 other | H-1 only |

```
GATE0_SRAM_IN=port/test/saves/e20_start.sav GATE0_KEYS=port/test/inputs/boost_e20.keys BOOST_AT=1900 BOOST_USES=1 \
  SRAM_AT=tick:2721 SRAM_OUT=port/test/saves/b20_start.sav MGBA_RUN_BP=<GBA_BP of port/test/Makefile> \
  build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/sram_boost.lua 3000000
```

**Ephraim's boosted part 2** (2026-10-22, coverage 11, `port-plan/pending/c11-i.md`).
Ephraim's chain wins the final chapter's part 1 with the real army
(`route_i21.keys`, "Ephraim's route" below), but not part 2: in four tries
from `i22_start` (Tactics I11) the bot cleared the Demon King's eight
summoned monsters by turn 6 or 7 with no loss but could not bring him down.
What is left for him (120 HP; Siegmund does 24 a hit, Audhulma 19 with 2 uses
left after Lyon, nothing else more than 9; his Demonlight does 37 to Ephraim,
39 HP, and 27 to Seth) is healing: 2 Elixir uses and 5 Vulneraries (10 HP
each) in the whole army, the supply empty, no healer, no healing tile. Part 1
spent 6 of the army's 8 Elixir uses (Seth's bait heals against Lyon). The best
try (`p2c`) took him to 92 by turn 10; from turn 13 Ephraim (10 of 42 HP)
and Seth (8 of 48) had nothing left to heal with, and turn 60 came with the
king at 92. So part 2 and everything after it start from a boosted save made
the same way as Eirika's: `sram_boost.lua` plays `route_i21.keys` and boosts
the four units at the end of tick 80,205 (part 2's map is loaded at 80,154,
its turn 1 begins at 80,216 and the game writes the turn-1 suspend then):
Ephraim 39 to 60 HP, Eirika 28 to 60, Seth 48 to 60, Franz 54 to 60, the caps
of their classes; Audhulma 2 to 30 uses, Siegmund 23 to 30, Reginleif 18 to
45, the Brave Lance 1 to 30 (`ib22_start.sav.json`). First 32 KiB: `i22_start`
fac3f343, `ib22_start` a1d7ce75, `ib_clear` 05d7fd4a, `ib_r2_start` 24803ec0.

- `route_ib22.keys` (`ROUTE_SEARCH=1`, `ROUTE_STOP=title:300`): the turn-1
  summon (eight monsters), the army holds its tiles; the summons fall by turn
  4; the Demon King falls on turn 7 (`PLAY_FLAG_COMPLETE`, the save menu:
  slot 0, the clear save, 22,948); the ending (`EventScr_EphraimModeGameEnd`,
  A only: scenes on maps 19, 12, 64, 16, the credits, the character endings),
  the Fin screen and the title.
- `route_ibend.keys` (`ROUTE_SEARCH=1`, `ROUTE_STOP=title:300`): Continue on
  `ib_clear.sav` replays Ephraim's ending (`GameCtrl_CheckGameCompleteAndBranch`),
  the credits and the Fin screen.
- `route_ibpost.keys` (`ROUTE_SEARCH=1 ROUTE_POSTGAME=1 ROUTE_WM_NODE=26,27`,
  `ROUTE_STOP=pp:47`): Extras, Map, the slot (`PLAY_FLAG_POSTGAME`), the world
  map, the Tower of Valni (node 26) floor 1 won on turn 2, "Proceed to the
  next floor?" B (the retreat; a game save at 19,472), Lagdou Ruins (node 27)
  floor 1 won on turn 14, "Proceed?" A, floor 2's first free cursor.

## Eirika's Chapter 20 and the final chapter with the real army (tactics E7)

Coverage 11 (`port-bots11e`, `port-plan/pending/c11-e.md`): the four units
`route_e19.keys` brings to Chapter 20 (Eirika, Great Lord level 1 with 20 HP;
Ephraim; Franz; Myrrh), not boosted, in Chapter 20, both parts of the final chapter and the
ending after it, and the ending again from the real clear save. Step E3 lost this chapter in 22 tries with
the rules of "The boss on the seize tile" above. The bot wins it with the rules
below, all only with `ROUTE_E7=1` and only in chapter indices 0x14 to 0x16 (the
world map before Chapter 20 has index 0x14 too), so every route made without it
(all the older ones, the boosted five included) comes out as before.

| File | Start | Ticks | GBA frames | Clears | End save |
|---|---|---|---|---|---|
| `route_e20.keys` | `e20_start` (fnv1a 0eb1f0e1) | 183,170 | 193,997 | Chapter 20 (index 20, Seize (11,11)), turn 49, no loss | `e21_start` (`save:21`, 182,869; fnv1a 163eb0bd) |
| `route_e21.keys` | `e21_start` (`route_e20.keys`, `save:21`; fnv1a 163eb0bd) | 60,394 | 63,944 | the final chapter's part 1 (index 21, Defeat Boss: Lyon), turn 15, no loss; it stops at part 2's first free cursor (index 22 at 60,251: `MNC3`, no save, no preparations) | `e22_start` (`tick:60394`: the turn-1 suspend save of part 2, title menu Resume Chapter; fnv1a 1096622f) |
| `route_e22.keys` | `e22_start` (above) | 67,933 | 69,765 | part 2 (index 22, Defeat Boss: the Demon King), turn 9, no loss; then the save menu (the clear save, slot 0, 31,826), the ending, the credits, the Fin screen (67,441) and the title screen (67,632) | `e_clear` (`tick:67933`: the SRAM at the route's end, the clear save in slot 0; fnv1a 44491be0) |
| `route_eend.keys` | `e_clear` (above) | 36,759 | 36,911 | Continue on the clear save: the game plays the ending again (`GameCtrl_CheckGameCompleteAndBranch`), the credits, the Fin screen, the title | (the SRAM is unchanged) |

- Made with `ROUTE_TICK=capture ROUTE_SEARCH=1 ROUTE_SEARCH_BASE=0 ROUTE_E7=1`,
  `ROUTE_STOP=saveto:21` (and `MGBA_RUN_BP` = the Makefile's `GBA_BP`): about
  10 minutes. 11 extra policies, no turn gone back, 151 attack trials.
- The world map (node 23): Franz buys a Silver Lance (the armory, as in
  `boost_e20.keys`); at the vendor Eirika sells the Orion's Bolt and buys an
  Elixir, Ephraim an Elixir, Franz sells a Chest Key and buys a second Elixir,
  Myrrh a second Elixir (22,195 gold to 14,145).
- Turn 1: Eirika takes Sieglinde from the supply (giving Audhulma, which needs S
  in swords) and waits; Franz picks her up and carries her to the end.
  Ephraim leads, Franz follows him, Myrrh keeps out of reach. The army takes the
  southern way: the south-east corner's exit (x 24-29, rows 9-12, turns 1-24,
  where the monsters and the north-east area's Cyclops waves come to it), west by
  rows 12-14, down by x 16-18, along rows 17-19 to the mountain at (9,19)
  (turns 26-38), up the west side (x 4-8) to the basin round (11,12)
  (turns 39-47). Turn 48: Franz drops Eirika at (9,13) from (9,12); turn 49: Myrrh, at full
  HP, kills Morva from (11,12) (two Divinestone hits through his 36-HP counter;
  "strike: char 1e on 41 (hp 62) ... 62 of 62") and Eirika, moving last, seizes
  (182,792). 
- The search kept no death; Eirika was never attacked.
- `route_e21.keys`: made with `ROUTE_TICK=capture ROUTE_SEARCH=1 ROUTE_SEARCH_BASE=0
  ROUTE_E7=1`, `ROUTE_STOP=pp:22`, about 2 minutes; 24 extra policies, no turn gone
  back. Turn 1: Ephraim takes the Spear from the supply (Siegmund's 19 uses are the
  bosses'; he gives the Steel Sword and the Killing Edge he cannot use), Franz picks
  Eirika up (and puts her down only when the chapter ends). The chargers come to the
  start (AI2 0 and 0x12: four Revenants, two Wights), then the army goes up the west
  side (x 2-5): Franz's attack from (3,11) on turn 6 calls the five Arch Mogalls of the
  hall's west half (they load at (22,5) on turn 7 and fly west; four are dead by turn
  15). Ephraim enters the throne room by its west wing (turns 8-12: the Cyclops on
  (4,2), the Gorgons on (5,2) and (10,1)). Turn 15: Myrrh (38 of 38 HP, 8 Divinestone
  uses) and Ephraim (47 of 47) can both reach a tile next to Lyon, the strike is on;
  Myrrh, from (11,1), kills him in her one fight (two hits, one critical: 22 a hit
  otherwise). The stairs (10,5)-(12,6) are never stepped on, so the ten turns of
  Revenants by Lyon never come. The army comes to part 2 with full HP (the game
  heals it), Myrrh with 6 Divinestone uses, Ephraim with Siegmund's 9 and no heal item
  (the Spear broke).
- `route_e22.keys` (`ROUTE_STOP=title:300`, otherwise as `route_e21.keys`): about 3
  minutes, 45 extra policies, no turn gone back. Turn 1: Franz picks Eirika up again;
  the Demon King summons eight monsters in the enemy phase (Arch Mogalls; on the PC
  this is C10-6, below). Ephraim kills them with Siegmund (his own attacks on
  them stop at 4 uses, his counters spend more: 3 are left on turn 7); Myrrh hits
  the Demon King alone on turns 5 and 8 (45 a hit; 120 to 75 to 30; she heals in
  between), and on turn 9 the strike is on (Myrrh
  from (10,8), Ephraim from (11,9)): her hit kills him. Then the game's own end: the
  save menu (slot 0, 31,826), the ending's scenes (indices 19, 15, 64, 16), the
  credits, the Fin screen and the title screen, played with A only, as in
  `route_b22.keys`.
- `route_eend.keys` (`ROUTE_STOP=title:300`, no turn to search): Continue on `e_clear`
  plays the whole ending again, as `route_bend.keys` does on the boosted clear save;
  its key masks are the same as `route_bend.keys`' (A on the same ticks: the scenes
  wait for the same keys), the game data behind them are not (the turn counts, the
  stats and the times in the ending's records).

**Tactics E7** (`ROUTE_E7=1`; `L.E7` in `route_bot.lua`):

- **Shopping and the supply.** The vendor plan of tactics v10 (an Elixir for
  each unit that has none), plus a second Elixir for each unit that is not a lord
  while 6,000 gold or more are left (never into the supply, which is out of
  reach once Eirika is carried; never selling the Hoplon Shield). v10's supply
  rule on turn 1 (Eirika takes Sieglinde, her one sacred weapon: Audhulma needs S).
- **Eirika is carried** by the toughest mounted fighter that is not a lord
  (Franz), from turn 1 after her own supply, for the whole chapter, and in the
  final chapter too; not again once she stands on a staging tile (below). The
  carrier moves last, keeps a tile whose threat reaches two thirds of its HP out,
  keeps no closer than 2 points behind the most advanced escort on her way,
  and attacks only an enemy it kills at a blow where the threat after stays
  under half its HP. Its death counts as a lord's in the search.
- **Her way** (`L.E7.way`, Chapter 20): rows 9 and down, the southern way. The
  northern one passes 17 monsters and the spawn points of two area waves (Cyclops
  at (11,1), Deathgoyles at (1,1)); in step E3 every carrier that went north
  stalled there. Her distance field (`L.E7.field`) gives a tile off the way 30
  points more; the carrier and the vanguard stand only on it.
- **The vanguard** (`L.E7.front`): Ephraim, and Myrrh while she has more than
  14 Divinestone uses, walk 3 to 9 points ahead of the most advanced of Eirika
  and the escorts (at most 16 ahead of Eirika), fight first the enemies within 2
  tiles of her way's next 12 points (+300), attack where the threat after stays
  under the HP left (not half, the lords' rule), and keep off tiles whose threat
  could kill them, a tile closer counting 10.
- **Myrrh's Divinestone** (24 uses, nothing replaces it, Lyon and the Demon King
  need about 10): she fights only bosses other than the seize tile's holder, or,
  as the vanguard, enemies on the way; otherwise she keeps back, safety first.
  In a first try her rush to Morva (E6's "rusher") and the counters round him
  spent 17 uses by turn 7.
- **Nobody stands in Morva's reach** (2 tiles: Wretched Air, 36 through any
  defence) but for the strike; nobody attacks him but by the strike.
- **The end** (v10's strike): when a unit that kills the holder alone (Myrrh,
  56 a hit, twice, at full HP) is within its move of the holder's open side,
  the carrier drops Eirika on a staging tile (one move from the seize tile with
  the holder gone, 3 or more from it, threat under her HP); next turn that unit
  strikes and Eirika, moving last, seizes. The turn after Morva's fall Mogalls
  would load on the free tile (`src/events/ch20a-eventscript.h`), so the seize
  comes in the same turn.
- **The threat** counts each enemy's worst weapon (v10's `bestStrike`: a
  Gorgon's Demon Surge, not its Stone); a unit heals under 60% of its HP (an
  Elixir heals it all), on her way when it can, and one with no heal item takes
  an Elixir by Trade from an ally with two.
- The search's turn score counts Eirika's (or her carrier's) distance on her
  way, not in a straight line.

**Tactics E7: the final chapter** (indices 0x15 and 0x16; `L.E7.fin` and the functions
before it in `route_bot.lua`; only with `ROUTE_E7=1`, so `route_b21.keys`,
`route_b22.keys` and `route_bend.keys` come out as before, checked byte for byte, as
are `route_e20.keys` and the three files above). In the bot's earlier tries (coverage
11, not committed) the worst-case threat (every enemy hit lands) kept Ephraim off
every tile near the throne room (two Shadowshots, 19 a hit at 8%), and the army stood
still from turn 21 to 60 with 17 enemies left. The final chapter has its own rules:

- **Chances, not worst cases.** Each enemy's real hit rate (FE8 rolls every hit with
  two random numbers, `Roll2RN`: (rn1 + rn2) / 2 under the rate, so a 20% hit lands
  8.4% of the time), its crit rate, its damage with each of its weapons (the stat
  bonuses, the triangle and the reaver weapons, effectiveness x2 for the sacred
  weapons and x3 for the others, `IA_NEGATE_DEFENSE`, Stone's petrification, which
  ends a fight) and the counter's, over the strikes of the fight (`BattleUnwind`: a
  round each, then one more for the side 4 or more faster; a brave weapon twice a
  round; `IA_UNCOUNTERABLE`) give each fight's outcomes (`L.E7.fight`). Every enemy
  whose reach covers a tile attacks it with the weapon and from the distance it likes
  best (the most damage, a kill or Stone above all, less half the counter's); the
  fights are taken as independent, and a tile gets its chance of death, of Stone, its
  expected damage and the expected hits of its counters (`L.E7.risk`).
- **The death chance a unit may take** (`L.E7.theta`): 3% a move or attack; with the
  search's policies 0.5% ("safe" for that unit), 1.2% ("cautious"), 8% ("press", a
  stalled turn), 15% ("bold"). A death makes the search play the turn again.
- **Ephraim** takes his best attack (a kill 1,000, 8 a point of damage, more for an
  enemy that could reach Myrrh or the carrier and for a Stone holder, less for
  Siegmund's uses: his attacks on enemies other than the boss stop at 4) or moves
  to the tile nearest the boss along the ways whose death chance stays under the
  limit; he heals under 60% of
  his HP (an Elixir, or one by Trade from Myrrh or the carrier) or, near Lyon, under
  what Lyon's counter takes (43); when he only moves he equips a weapon other than
  Siegmund for the enemy phase's counters.
- **Myrrh** keeps out of every reach that would cost her a Divinestone use (her
  counters) or HP and out of every Stone's (a tile's cost: 1,000,000 a Stone chance,
  4,000 a use, 100,000 a death chance), as near the boss along her own way as that
  allows (she crosses the dark tiles), near Ephraim (20 a tile past 7), and heals
  below full HP. In part 2 she also attacks the Demon King alone from full HP where her death
  chance in his counter and the enemy phase after stays under 20% (`L.E7.SOLO`; his
  Ravager crits 15% of the time, 54 to her): the strike needs a first hit (120 HP).
- **The strike** (`L.E7.strikePlan`): when Myrrh and Ephraim, neither yet moved, can
  both reach a free tile next to the boss, Myrrh's fight and then Ephraim's (each
  weapon, each pair of tiles; exact chances over both fights) kill it at 85% or more,
  Ephraim's death chance in his fight is at most 3% and Myrrh's 1% (8% against the
  Demon King, whose Demonlight crits 6%), the turn's order is Myrrh, then Ephraim
  (`L.E7.finOrder`; else Ephraim, Myrrh, Eirika, the carrier last). Lyon (73 HP, def
  26, attack speed 4) takes 22 a hit from her, twice, and 16 twice from Siegmund (76);
  their hit rates are 100, so he dies before any enemy phase and the two Stone
  Gorgons next to him never act.
- **Eirika's carrier** (Franz) picks her up on turn 1 once Ephraim has taken from the
  supply (she has to be on the map for it), keeps to the tiles of the least risk (3 a
  tile past 4 from Myrrh), heals under 70% and attacks only a kill at a blow where
  nothing can then hurt it. Eirika on the map (turn 1 of each part) waits on the safest tile.
- **The ways** (part 1, `L.E7.corr`): the stairs into the throne room, (10,5) to
  (12,6), are walls (a unit ending its move there calls three Revenants by Lyon each
  turn for ten turns: `AREA` (10,5)-(12,6), `src/events/ch21a-eventscript.h`); the
  hall's west half (x 0-11, rows 7-12) costs 20 a tile more and 5,000 to end a move in
  until the Arch Mogalls are there (it calls five, Crimson Eye; the east half calls four
  Deathgoyles); an attack does not count that cost (in `route_e21.keys` Franz's attack
  from (3,11) called them). An enemy's tile costs 5 more (12 for one that never leaves
  it).
- **The search's turn score** (`L.E7.finScore`) adds 3,000 a Divinestone use spent,
  80,000 a petrified unit, 300 a point of the boss's HP taken, and 40 and 15 a point of
  Ephraim's and Myrrh's ways to the boss.

**PC check of the real army's routes** (2026-10-23, `port-plan/pending/c11-e.md`): the lag
replay with `FE8PC_NULLPROBE=1`, release build `build/pc-gate0` (the game code of
`port-integrate10`), compared without the raw dumps, one pass (the traced fields and
frame15, no per-frame windows). The groups are round 10's
(`port-plan/pending/c10-findings.md`), not fixed on this base.

| Route | Traced fields | NULL probe |
|---|---|---|
| `route_e20.keys` | MATCH on 0-27,109; the PC stops at 27,110 (SIGSEGV, EIP 0x02032F8C), a Gorgon's Stone map animation in the turn-4 enemy phase: C10-4 (a) | the H-1 drop (`AnimDelete`, 9,515); `Decompress` (`src/bmlib.c:139`) reading 0x0001000E from `MapAnimMonsterStone_Loop1` at 27,110, C10-4 (a)'s site |
| `route_e21.keys` | equal to 28,238; the PC leaves the GBA at 28,239 (`gOam` from 28,242): `[stub] StartSpellAnimCrimsonEye`, an Arch Mogall's Crimson Eye in the turn-9 enemy phase, C10-1 | H-1 drops only (`AnimDelete`, 2,769 and 2,853) |
| `route_e22.keys` | equal to 1,376; the PC leaves the GBA at 1,377 (16 main-loop events on the GBA, 20 on the PC, the turn-1 enemy phase): the Demon King's summon, C10-6, as in `route_b22.keys` at 3,135 | no site |
| `route_eend.keys` | MATCH, 36,759 ticks (150 lag VBlanks, all at the GBA's positions) | 7 sites from 19,063 (`GetFaceDisplayBitsById`, `src/face.c:471`; `SetFaceBlinkControl`; `FaceBlinkProc_GenBlinkInterval`): C10-8, where the plain PC stops, as in `route_bend.keys` |

Snapshots (all four parts, `savestate.py check` exit 0): `~/fe8-snapshots/route_e21/tick1480`
(part 1's first free cursor), `route_e22/tick729` (part 2's, after Resume Chapter),
`route_eend/tick716` (the ending's start); `route_e20/tick7440` (Chapter 20's).

## Ephraim's final chapter, part 2, with the real army (tactics E7 on map 0x23)

Coverage 12 (`port-c12-iend-real`, `port-plan/pending/c12-iend-real.md`): the four units
`route_i21.keys` brings to part 2 (Ephraim, Master Lord level 5 with 39 HP; Seth; Franz;
Eirika), not boosted, from the real `i22_start.sav` (made again from `route_i21.keys` by
`chain-save`: byte for byte the saved one), win part 2, then the game's own end, and the ending
again from the real clear save. Coverage 11's bot lost part 2 four times (README above,
"Ephraim's route"); the boosted `route_ib22.keys` stays as it was.

| File | Start | Ticks | GBA frames | Clears | End save |
|---|---|---|---|---|---|
| `route_i22.keys` | `i22_start` (`route_i21.keys`, `tick:80292`; fnv1a fac3f343) | 65,624 | 67,707 | part 2 (index 35, Defeat Boss: the Demon King), turn 21, no loss; then the save menu (the clear save, slot 0, 28,665), the ending's scenes (indices 19, 12, 64, 16), the credits and the title (65,132) | `i_clear` (`tick:65624`: the SRAM at the route's end, the clear save in slot 0; fnv1a 5e8e92c8) |
| `route_iend.keys` | `i_clear` (above) | 37,611 | 37,763 | Continue on the clear save: the ending again, the credits, the Fin screen, the title (37,119) | (none written) |

- Both made with `ROUTE_TICK=capture ROUTE_SEARCH=1 ROUTE_SEARCH_BASE=0 ROUTE_SEARCH_K=16
  ROUTE_SEARCH_LOOKBACK=1 ROUTE_E7=1 ROUTE_MAX_TURN=99`, `ROUTE_STOP=title:300` (and
  `MGBA_RUN_BP` = the Makefile's `GBA_BP`); `route_i22.keys` in 148 s (91 extra policies, no turn
  gone back, no death kept), `route_iend.keys` in 26 s. `route_check.lua` gives the headers'
  milestones exactly (12 and 11 lines), and the committed bot makes `route_i22.keys` again byte for
  byte.
- How part 2 went: turn 1, Ephraim takes Franz's Elixir by Trade, Seth equips his Iron Sword (the
  Audhulma kept), Franz picks Eirika up; the army holds its start tiles while the Demon King
  summons eight monsters (this time two Gorgons, two Entombed, a Wight, an Elder Bael and two
  Draco Zombies; the turn's moves decide which). Turns 2 to 4: the army kills the six that come
  (Ephraim heals on turns 4 and 5 with Vulneraries); the two Draco Zombies, which wait (AI2
  NeverMove) on (14,7) and (7,6), are left alone to the end, out of their reach. Turn 5: Franz
  drops Eirika in the north-west corner (1,3). Then the chips, each from (10,6), Seth rescuing
  Ephraim from (10,7) and cantoing to (10,11): turn 8 (120 to 95; the counter missed), turn 10
  (to 70; Ephraim 5 HP), turn 15 (to 45), turn 19 (to 20); he heals with the two Elixir uses in
  between. Turn 21, the strike, Seth first (97%): the Audhulma's two hits from (11,5) kill him.
  Left at the end: Siegmund 18 uses, Ephraim's last Vulnerary, Eirika's two.
- `route_iend.keys`' key masks are the same as `route_ibend.keys`' (the scenes wait for A on the
  same ticks); the game data behind them are the real army's.

**Tactics E7 on map 0x23** (`ROUTE_E7=1`; `L.E7.MAP[0x23]` and `L.E7.fin23` with the functions
before it in `route_bot.lua`; only in chapterIndex 0x23, so every older route, Eirika's E7 ones
and `route_i21.keys` and `route_ib22.keys` among them, comes out as before). E7's exact chances
(`L.E7.fight`, `L.E7.risk`, `L.E7.attack`) with this map's own rules:

- **Out of his reach.** The Demon King (120 HP; DemonKingAI: after turn 1's summon, Nightmare and
  more summons need five of our units in range, so he only attacks; AI2 NeverMove) acts only on
  a unit in his reach (2 moves, range 3) at an enemy phase. Nobody ends a move there (20,000) but
  for a chip, a rescue or the strike, so he never acts and keeps Demonlight equipped (32 to
  Ephraim at 96%, a 4% crit); an attack with Ravager (37 at 100%, a 16% crit) would equip it for
  his counters (`EquipUnitItemSlot`, `src/cp_perform.c`).
- **The chip** (`L.E7.chipPlan`): Ephraim, with more HP than the counter takes and no monster left
  that comes, attacks him with Siegmund from a tile next to him (death chance at most 5%); a
  Paladin rescues him from a tile next to that one and cantos (Canto after Rescue:
  `TryMakeCantoUnit`, UNIT_MOV less the move's cost) to the tile of the least risk out of every
  reach; next turn it drops him near the launch tiles (`L.E7.launch`: out of every reach, within
  his move of a tile next to the boss; a dropped unit is greyed, `UnitDrop`), and he heals (an
  Elixir from 25 HP missing, else a Vulnerary; one by Trade from Franz or Eirika when he has none).
- **The strike** (`L.E7.eStrike`, E7's same-turn strike): Seth (Audhulma, 19 twice) and Ephraim
  in the better order when the two kill him at 85% or more.
- **The monsters** (`L.E7.movers`): those that come (AI1 0, ActionInRange; AI2 0, or AI2 3 with a
  blue unit in reach) are met out of his reach by E7's attack (death chance 3%, the search's
  policies 0.5% to 15%), Siegmund kept at 8 uses, Seth never with the Audhulma (his Iron Sword
  equipped from turn 1 for the counters); Franz carries Eirika by Ephraim (`L.E7.cohesion`, 25 a
  tile past 2) and drops her 8 or more from the boss once none is left. The ones that wait
  (AI2 3, nobody in their reach: `L.E7.waiting`) are left alone: nobody ends a move in their reach
  (15,000). (In a try a Draco Zombie woken by a drop took Eirika to 1 HP.)
- **The search's turn score** (`L.E7.score23`): 300 a point of his HP taken, 1,200 an Elixir use
  spent, 350 a Vulnerary use, 25 a point of Ephraim's HP, 80,000 a petrified unit, 20,000 if he has
  Ravager equipped. The policies' moves change the random numbers the fights get (a unit's path
  with ties takes them: `GenerateBestMovementScript`, `src/bmidoten.c`), so a chip that missed is
  played again: 16 policies on each chip turn.

**On the PC** (coverage 12; this branch's game code = integration 11's, the release build, no NULL
probe): `gba-trace`, `pc-lag-replay` and `compare-lag-replay` from the route's start save, with
`GATE0_SHOT_TICKS` covering every tick, so the frame of every tick was compared too.

| File | Traced fields | Frames | Lag replay | SRAM at the end (first 32 KiB) |
|---|---|---|---|---|
| `route_i22.keys` | **MATCH** on 0-65,623: every traced field equal on every tick (`gBmSt`'s `prevVCount` left out by the rules) | 65,624 compared; 681 differ, all raster-timing labelled (P-17): 497 on the map (41 phase banners, 10 to 13 each, and 8 single frames), 184 in the battle animations (16 each on turns 10, 15 and 19, 136 on turn 21: 27,404-27,446 and the Demon King's death, 28,228-28,349) | 2,079 GBA lag VBlanks in 1,041 ticks, all at the GBA's positions; event counts equal on every tick | 27 bytes differ, all P-12's known kinds (`DIVERGENCES.md`): the stack words and pad bytes of block infos 0, 3 and 4 and their checksum, the suspend blocks' and game block 0's `supplyItems` byte 0xAF, `GMapSaveInfo` padding and `gmUnit` bytes 6-7; the play times are equal. The GBA's is `i_clear.sav` byte for byte |
| `route_iend.keys` | **MATCH** on 0-37,610 | 37,611 compared, all equal (none labelled) | 150 in 48 ticks, all at the GBA's positions | 8 bytes differ: block infos 3 and 4 `magic32` (GBA 0x080A3121, PC 0x00040624; `InvalidateSuspendSave` never sets it, P-12) |

Snapshots (all four parts, `savestate.py check` exit 0), each resumed on both sides for 3,500
ticks (`make route GATE0_STATE_IN=...`, the release lag replay): MATCH, and the resumed GBA trace
equal to the full pass's on every game field: `~/fe8-snapshots/route_i22/tick720` (part 2 resumed,
before turn 1's phase banner; 1 labelled frame), `route_i22/tick26580` (turn 21, 17 ticks before
Seth's strike's battle animation, the Demon King's death and the clear save at 28,665; 2 labelled
frames), `route_iend/tick716` (the ending's start).

## The post-game floors (boosted, coverage 11)

The Tower of Valni and the Lagdou Ruins floor by floor, with the boosted army of `b_clear.sav`
(the four units of "Boosted starts" above: Eirika, Ephraim, Franz, Myrrh; Caellach, character
0x29, joins in the Tower). Each file starts from its floor's turn-1 suspend save (the title menu's Resume
Chapter; the first one from the clear save) and stops at the next floor's first free cursor
(`ROUTE_STOP=pp:N`); the SRAM at its last tick (`sram-capture SAVE_AT=tick:T`) is the next
file's start. Made with `ROUTE_TICK=capture ROUTE_SEARCH=1 ROUTE_SEARCH_BASE=0` (and a
`ROUTE_MAX_TURN` of 40 to 60, which only stops a stalled try; `port-plan/pending/c11-p.md`);
the committed bot makes every one of them byte for byte. Tower floors 2 to 4 were only in a lost try before
(`c10-e4.md`), floors 5 to 8 and Ruins 2 to 10 never. Ruins floor 10 is coverage 12's, made
cheat-assisted (`route_br10.keys`, below).

| File | Start save (FNV-1a) | Ticks | GBA frames | Plays | End save |
|---|---|---|---|---|---|
| `route_bt1.keys` | `b_clear` (4fd2d0fb) | 118,458 | 123,894 | with `ROUTE_POSTGAME=1 ROUTE_WM_NODE=26`: the title's Extras, Map, the post-game world map, the Tower (node 26); floor 1 (index 36) won turn 3, "Proceed to the next floor?" yes; floor 2 (37) turn 15; floor 3 (38) turn 12; floor 4 (39) turn 9; floor 5's first free cursor (118,457) | `bt5_start` (`tick:118458`, b1e31764) |
| `route_bt5.keys` | `bt5_start` (b1e31764) | 61,654 | 64,002 | floor 5 (index 40), won turn 7 | `bt6_start` (`tick:61654`, 2d99a9b4) |
| `route_bt6.keys` | `bt6_start` (2d99a9b4) | 63,020 | 66,965 | floor 6 (index 41), won turn 29: its rooms are shut by cracked walls, which the army breaks (the walls rule below) | `bt7_start` (`tick:63020`, 9278e525) |
| `route_bt7.keys` | `bt7_start` (9278e525) | 55,743 | 59,150 | floor 7 (index 42), won turn 14 | `bt8_start` (`tick:55743`, 54b3d7d5) |
| `route_bt8.keys` | `bt8_start` (54b3d7d5) | 47,947 | 50,464 | floor 8 (index 43), the last, won turn 11 (47,409); the dungeon record screen (A, below); a game save (47,474); the world map's free cursor; `ROUTE_STOP=wm:300` | (not kept) |
| `route_br2.keys` | `br2r_start` (0821a27e; **refitted**, below) | 64,304 | 67,289 | Ruins floor 2 (index 47), won turn 12 | `br3_start` (`tick:64304`, 19656a81) |
| `route_br3.keys` | `br3_start` (19656a81) | 107,697 | 113,383 | floor 3 (48), won turn 28 | `br4_start` (`tick:107697`, 05e3380f) |
| `route_br4.keys` | `br4_start` (05e3380f) | 66,193 | 70,705 | floor 4 (49), won turn 34 | `br5_start` (`tick:66193`, 8f60d184) |
| `route_br5.keys` | `br5_start` (8f60d184) | 54,229 | 56,400 | floor 5 (50), won turn 12 | `br6_start` (`tick:54229`, 602efee7) |
| `route_br6.keys` | `br6_start` (602efee7) | 87,737 | 92,679 | floor 6 (51), won turn 21 (a game save at 38,124 on the way) | `br7_start` (`tick:87737`, 4d44fb6e) |
| `route_br7.keys` | `br7_start` (4d44fb6e) | 98,493 | 105,255 | floor 7 (52), won turn 40 | `br8_start` (`tick:98493`, 07a2cab4) |
| `route_br8.keys` | `br8_start` (07a2cab4) | 90,018 | 95,936 | floor 8 (53), won turn 28 | `br9_start` (`tick:90018`, eddf1d75) |
| `route_br9.keys` | `br9_start` (eddf1d75) | 97,417 | 103,248 | floor 9 (54), won turn 35 (a game save at 94,748 on the way) | `br10_start` (`tick:97417`, b92ebad3) |
| `route_br10.keys` | `br10r_start` (08035d5c; **refitted**, below; **boosted, cheat-assisted**: made with `ROUTE_CHEAT=durability`, test coverage only, not fair play) | 54,996 | 58,156 | floor 10 (55), the last: won turn 23 (the tenth Draco Zombie dies at 53,497; nobody lost); the dungeon record screen (A, 53,676 to 54,432); a game save (54,523); the world map's free cursor (54,695); `ROUTE_STOP=wm:300` (the lord's node takes the bot's A: floor 1's map loads at 54,756, as in `route_bt8.keys`) | (not kept) |

**The Ruins refit** (test coverage, as the boosted starts). `b_r2_start` (the end of
`route_bpost.keys`: Ruins floor 2's turn-1 suspend) carries the boosted army after Ruins
floor 1's 33 turns, and its weapons are worn out: in a try from it (scratch `br2a`, 215,223
ticks) Ephraim (an Iron Bow and a Swordslayer), Franz (none) and Myrrh (none: her
Divinestone broke) had nothing they could attack with, Eirika only Audhulma, and the army
stood in a corner from turn 7 to turn 49 with 28 enemies left. `br2r_start` gives the four
units new weapons and nothing else: `sram_boost.lua` with `BOOST_ITEMS` (`boost_r2.boost`:
Eirika Sieglinde, Audhulma, a Silver Sword, a Silver Lance, an Elixir; Ephraim Siegmund,
Reginleif, a Silver Lance, a Silver Sword, an Elixir; Franz Vidofnir, a Silver Lance, a
Silver Sword, a Brave Lance, an Elixir; Myrrh three Divinestones and two Elixirs; each with a
new item's uses) at the end of tick 735 of `boost_r2.keys` (the bot's own `ROUTE_SUSPEND=1`
run from `b_r2_start`: Resume, the first free cursor, the map menu's Suspend, [Yes], the
title), so the game's own suspend save (written at the Suspend) holds the refit; the SRAM at
tick 936. Resume Chapter on it is floor 2's turn 1 again (suspend point PLAYERIDLE).
`make -f port/test/Makefile chain-save CHAIN_SAVE=br2r_start` makes it.

**Ruins floor 10** (index 55, the last: ten Draco Zombies, the boss on (9,3)) is won by
`route_br10.keys` (coverage 12, `port-plan/pending/c12-br10.md`), **boosted, refitted and
cheat-assisted** (test coverage only, not fair play; its header says so: "Cheat-assisted
routes" below). Before it, coverage 11 lost it twice: from `br10_start` (`route_br9.keys`'s end;
the weapons worn out again, Eirika with a Silver Sword of 1 use) a try lost Franz on turn 4,
Valter (character 0x27, who joins in the Ruins) on turn 6 and Myrrh on turn 8; from the refit
below a try lost Franz on turn 4 and stalled from turn 9 to 13 with 8 enemies left (16 policies
a turn). The cheat alone does not win it: with it and the plain rules (coverage 12, scratch `b0`)
Franz died on turn 3 again and 9 enemies were still there on turn 7. A Draco Zombie's Wretched
Air takes 30 to 32 whatever the defence, so two hits kill any of the army; the plain rules count
every enemy hit as landing, and the army either stood out of every reach or walked into three.
`route_br10.keys` is tactics R10 (below): nobody lost, every turn kept on the search's first
policy (no extra policy played), about 70 seconds to make.

**The floor-10 refit** (test coverage, as the Ruins refit above): `br10r_start` is `br10_start`
with the same kit (`boost_r10.boost`, a copy of `boost_r2.boost`) poked at the end of tick 735 of
`boost_r10.keys` (the bot's own `ROUTE_SUSPEND=1` run from `br10_start`, coverage 11's scratch
`boost_r10a`), the SRAM at tick 936: Resume Chapter on it is floor 10's turn 1. Valter keeps his
Delphi Shield and Steel Lance. `make -f port/test/Makefile chain-save CHAIN_SAVE=br10r_start`
makes it, byte for byte the save the route was made from; `chain-route CHAIN=br10` runs the route.

**Tactics R10** (`L.R10` in `route_bot.lua`; only in chapter index 0x37, where no other route
plays a turn: `route_br9.keys` stops at its first free cursor, and the committed bot makes it
byte for byte). A director for the floor, on E7's exact model ("Tactics E7: the final chapter"
above: `L.E7.fight`, `L.E7.risk`, FE8's 2RN hit roll). Nine zombies wait (AI2 3) until one of
ours stands in their reach; the boss (AI1 3) never leaves (9,3). Myrrh's Divinestone (x3, 40 a hit,
twice) and Ephraim's Siegmund (x2, 38 twice) kill one each a turn; Eirika's and Franz's sacred
weapons take 66 and 54.
- An attack is taken when the unit's death chance, in the fight and in the enemy phase after
  it, over every outcome of the fight (its HP left, its target dead or not), is at most theta
  (`L.E7.theta`: 3%, the search's policies 0.5% to 15%; half that for a lord). A target the
  fight leaves alive counts as dead when a unit yet to move can finish it at 90% or more within
  its own theta (the unit order then has it do so: Eirika 66, Franz the rest). A kill scores
  1000, a point of damage 8.
- The zombies awake (whose reach covers one of our units' tiles) stay at most 2 (1 with
  "cautious", 3 with "press" or "bold"), about what the army kills in a turn.
- With no attack, a unit that one more hit would kill heals (its Elixir, which the cheat keeps
  full) on its safest tile; a unit that can take a hit walks toward the nearest zombie on tiles
  whose death chance is at most theta (25 a point of the way, 5 a point of the expected damage),
  and a tile that wakes one zombie more within the cap counts 300 less (the bait: the last three
  zombies stand within 2 of each other, and without it the army stood at the edge of their reach
  from turn 15 to 30 of a try, scratch `r1`); a hurt unit keeps off (300 a point).
- Valter, whose Steel Lance does 1 a hit, keeps to his safest tile and heals under 70% (by
  Trade, an Elixir from an ally with two heal items; not needed in the route).
- The order: Myrrh, Ephraim, Eirika, Franz, Valter.

**PC check of `route_br10.keys`** (coverage 12, on this branch's game code: integration 11's):
the plain PC (release build `build/pc-gate0`, no NULL probe), `make -f port/test/Makefile
chain-route CHAIN=br10` with `GATE0_SHOT_TICKS=755-1870`, the cheat on both sides from the file's
header. **MATCH on every traced field of every tick, 0 to 54,995**, the cheat's `cheat_items` and
`cheat_refills` included (68 item words refilled, on 42 ticks); the lag replay's 3,156 GBA lag
VBlanks (in 1,344 ticks) at the GBA's positions, main-loop events equal on every tick. Frames:
2,014 compared, every tick of the first battle (Myrrh's Divinestone on the Draco Zombie at
(13,15), which it kills, ticks 755 to 1,870) and every 60th tick elsewhere; 4 differ, all labelled
raster-timing frames (P-17: 15,660, 27,660, 44,160, 51,360), none in the battle.

**The walls rule** (`L.F.WALLS`: Tower floors 6 to 8 and Ruins floors 2 to 10, the floors no
older route plays). Tower floor 6 splits its rooms with cracked walls (`TERRAIN_WALL_DAMAGED`
0x1B: a `TRAP_OBSTACLE` with HP, which the Attack command lists, `TryAddTrapsToTargetList`).
The enemies behind them (AI2 3: they wait until someone comes into reach) never come out; a
try without the rule (scratch `bt6a`, and `c10-e4.md`'s `bpg1`) waited from turn 13 to the
turn limit with 22 enemies left. A unit with no way to any enemy (its distance field to them
has none) attacks the cracked wall nearest to them on the way (the distance with the walls
passable at cost 6) from a tile it outlives, or walks toward such a wall; the target selection
picks the wall by its position (its target has no unit). **The dungeon's end:** after a
dungeon's last floor the record screen (`sProcScr_DisplayDungeonRecord_AfterDungeonClear`)
takes A, not the event's Start (`DungeonRecordUi_KeyListener`); a try that pressed Start
stopped there for 200,000 ticks (scratch `bt8a`).

## Feature routes and world-map skirmishes (coverage 11)

Short files for game features no chapter route uses, each from a story save (one from a
level-boosted one) and made by the bot with a feature setting (`route_bot.lua`'s header
comment, "Feature routes"; `ROUTE_TICK=capture`; each setting is named in the file's
`# Generated` line). Each stops a few hundred ticks after its feature (`ROUTE_STOP=arena:N`,
`support:N`, `chest:N`, `resume:N`, `promo:N`, `skirmish:N`). The GBA's state after each was
read back from a replay of the file (`port-plan/pending/c11-p.md`, "Checks").

| File | Start save (FNV-1a) | Ticks | GBA frames | Setting | Plays |
|---|---|---|---|---|---|
| `route_f_arena_ch5.keys` | `ch5_start` (f7d234d4) | 29,660 | 30,893 | `ROUTE_ARENA=12,6,2 ROUTE_STOP=arena:600` | Chapter 5 (Serafew): Seth walks to the arena (12,6) on turns 1 to 5 and takes Arena (26,727); the wager [Yes], the fight, a win, the prize; the screens close at 29,059. The keys are r9-uiarena's scratch `route_arena_ch5.keys` exactly (its arena rules are now the bot's) |
| `route_f_support_e12.keys` | `e12_start` (40dea610) | 2,986 | 3,122 | `ROUTE_SUPPORT=2,1 ROUTE_STOP=support:300` | Eirika's Chapter 12 (Village of Silva), turn 1: Seth (80 support points with Eirika: the most before rank C, so their C conversation is ready) moves next to her and takes Support (1,656); the C conversation plays with A to its end (2,685); Seth's points 80 to 81 (rank C) |
| `route_f_chest_ch8.keys` | `ch8_start` (6b39183a) | 31,622 | 33,123 | `ROUTE_CHEST=9 ROUTE_STOP=chest:600` | Chapter 8: Colm (a thief with a Lockpick) walks to the chest at (19,4) and opens it (31,021) |
| `route_f_suspend_ch6.keys` | `ch6_start` (a3361b16) | 2,604 | 2,801 | `ROUTE_SUSPEND=1 ROUTE_STOP=resume:600` | Chapter 6, turn 1's first free cursor: the map menu's Suspend (1,704), [Yes], the save, the title screen (1,863); Resume Chapter; the chapter again, free cursor at 2,003 (integrate9's hand-made `k7.keys` idea, made by the bot) |
| `route_f_promo_ch6.keys` | `f_promo_start` (82f177b4; **level-boosted**, below) | 41,795 | 43,907 | `ROUTE_PROMO=1 ROUTE_STOP=promo:300` | Chapter 6 with Ross (Journeyman) at level 9 and 99 experience: his next battle takes him to level 10; Chapter 6 is won on turn 6 (39,185); at Chapter 7's start the game's trainee promotion runs (`StartTraineePromoScreen`: a trainee at level 10 promotes when the next chapter starts): its talk and the class choice with A (40,072 to 41,494); Ross becomes a Fighter (class 0x3F), level 1 |

**The level-boosted start** (test coverage, as "Boosted starts" above). No story save has a
trainee near level 10: Ross died in Chapter 6 (before the split); Ewan stayed at level 1 on
Eirika's route and died in her Chapter 15; on Ephraim's route Amelia died in his Chapter 9
and Ewan (level 5) in his Chapter 14. `f_promo_start` is `ch6_start` with only Ross
changed: `sram_boost.lua` with `BOOST_LEVEL=07:9:99 BOOST_CAPS=1` (level 9, 99 experience,
his stats to the Journeyman's caps; at level 1 he died in the first enemy phase that
reached him) at the end of tick 800 (the world map) of `boost_ch6.keys` (the bot's own walk
from `ch6_start` to Chapter 6's first free cursor), the SRAM at tick 1,669 (the turn-1
suspend). `make -f port/test/Makefile chain-save CHAIN_SAVE=f_promo_start` makes it
(`boost_ch6.boost` holds the settings).

World-map skirmishes, from story saves where monsters stand on the world map (made with
`ROUTE_SEARCH=1 ROUTE_SEARCH_BASE=0`): the lord walks to the monsters' node (no other
monsters on the way), the skirmish's battle map is that node's story chapter's map, and the
bot plays it with chapter index 0x39's rules (Melkaen Coast's: no seize tile, boss, talk
or hold-out rule of a story chapter; the milestones keep the game's own index). Each stops
300 ticks after the world map's free cursor is back. Before this round only debug-jump
skirmishes on the maps of Chapters 6, 17 and 18, Hamill Canyon and Melkaen Coast were played
(menu monkey, the Prologue army).

| File | Start save (FNV-1a) | Ticks | GBA frames | Plays |
|---|---|---|---|---|
| `route_sk4.keys` | `e10_start` (996ac19b) | 39,836 | 41,633 | `ROUTE_SKIRMISH=4`: Eirika from node 9 to node 4 (Za'ha Woods); the skirmish on Chapter 4's map (index 4) is won on turn 8 (39,372); a game save (39,437); the world map (39,535) |
| `route_sk10.keys` | `e14_start` (e97d186b) | 59,058 | 61,844 | `ROUTE_SKIRMISH=10`: Eirika from node 12 to node 10; the skirmish on Chapter 10's map (index 11) is won on turn 12 (58,594); a game save (58,659); the world map (58,757) |
| `route_sk15.keys` | `i13_start` (c65824de) | 38,517 | 40,198 | `ROUTE_SKIRMISH=15`: Ephraim from node 16 to node 15; the skirmish on his Chapter 10's map (index 24) is won on turn 7 (38,053); a game save (38,118); the world map (38,216) |
| `route_sk17.keys` | `e16_start` (95dd0bb9) | 51,315 | 53,930 | `ROUTE_SKIRMISH=17`: Eirika from node 19 to node 17; the skirmish on Ephraim's Chapter 13's map (index 26) is won on turn 12 (50,851); a game save (50,916); the world map (51,014). (From `i15_start`, Ephraim's army, scratch `sk17_i15b`: not won by turn 80; most of its units had no weapon left and the last 9 enemies, Gorgon eggs and AI2-3 monsters, never came) |

**PC check of the coverage-11 routes** (2026-10-22, `port-plan/pending/c11-p.md`; the lag replay
with `FE8PC_NULLPROBE=1`, release build, frame15 every 60 ticks; where the probe saw a site the
plain PC faults on, the plain PC's lag replay too). MATCH: `route_f_arena_ch5`,
`route_f_support_e12`, `route_f_chest_ch8`, `route_f_suspend_ch6`, `route_f_promo_ch6`,
`route_sk4`, `route_sk15`, `route_bt1` (118,458 ticks), `route_bt5` to `route_bt8`, `route_br2`,
`route_br4`, `route_br5` (every traced field on every tick; frames: only raster-timing labelled
ones differ; NULL probe: H-1 drops only, but in the feature and skirmish routes named below).
Where the others stop, and the sites the plain PC faults on:

| Route | With the NULL probe | The plain PC |
|---|---|---|
| `route_f_promo_ch6` | MATCH (sites from 40,119: `SetFaceBlinkControl`, a face that is not there, in the trainee promotion's talk) | SIGSEGV at 40,119 (`face.c:1282`): the ending's C10-8 kind, a new caller |
| `route_sk4`, `route_sk10`, `route_sk15`, `route_sk17` | sites at the skirmish's start (1,478, 1,472, 1,414, 1,365): `HideWmFace` with no world-map face proc | SIGSEGV there (`worldmap_face.c:435`, address 0x2C): every world-map skirmish (F-P-1, new) |
| `route_sk10`, `route_sk17`, `route_br7`, `route_br8` | equal to 13,678, 42,050, 3,788 and 8,192, then `[stub] StartSpellAnimCrimsonEye` (C10-1) | (as left) |
| `route_br3` | equal to 24,302, SIGSEGV at 24,303: the Gorgon's Stone map animation (C10-4 (a)) | the same (snapshot `route_br3/tick24290`) |
| `route_br6` | equal to 33,702, C10-4 (a) at 33,703; a site at 32,948: `EfxPalModifyPetrifyEffect` from `UpdateBanimFrame` (`banim-ekrmain.c:240`, `(int)PAL_OBJ(8)` as a palette line) | SIGSEGV at 32,948 (address 0x42479DA8): a petrified unit in a battle animation (F-P-2, new) |
| `route_br9` | sites from 5,153: `PidStatsRecordTargetListDefeats` from `RecordTrapDamageDefeats` (`bmtrick.c:660`), a trap's target with no unit (C10-3's kind); `gPaletteBuffer` alone differs from 62,549 (not identified); C10-1 at 73,043 | not run (C10-3's kind faults at 5,153) |

## Cheat-assisted routes (test coverage only, not fair play)

Some stretches are too long or too hard for the bot's fair play to cover (Lagdou Ruins floor
10 above: the army's weapons wear out faster than the floors give new ones). For those the
bot can play with the **test cheats** (port/test/README.md, "Test cheats"): `durability`
(the user's decision of 2026-10-22, the idea from FE8 Reforged's unlimited-durability option,
only the idea: at every tick boundary every unit's items and the convoy's get their uses back
to full), `funds` (2026-10-23: the party's gold at the game's maximum, 999,999) and `convoy`
(2026-10-23: a rolling convoy, every empty slot of the 100 filled with the next of the game's
177 items, going round the list; the 100 slots themselves are the ROM's limit, which a
compared run cannot raise), one or several joined with `+`. Both sides run them at the same
point, so a Gate 0 compare of such a route is as exact as any other. Such a route is labelled
**cheat-assisted** everywhere, as a boosted start is labelled boosted, and never counts as
story play. **Not for Eirika's Chapter 20 and final chapter** (the user's decision: they are
played fair). The refusal goes by chapter, whatever the settings and whichever cheat: the
cheats stop the bot, `route_check.lua`, every Gate 0 tool and the PC hook at the first tick
sample with `gPlaySt.chapterIndex` 0x14 to 0x16 (the indices `L.E7.chapter` plays) before the
game is complete, before the bot's first decision there, with or without `ROUTE_E7` (with
`ROUTE_E7=1` the bot stops at once), the real army or a boosted start (port/test/README.md,
"Test cheats"). So no cheat-assisted route starts from `e20_start`, `e21_start`, `e22_start`
or `b20_start` to `b22_start` (it stops at tick 708, where Continue loads the save), and none
crosses into Chapter 20 (a cheat-assisted Chapter 19 of Eirika's cannot end with `saveto:20`:
the index moves to 0x14 at tick 154,137 of `route_e19.keys`, before the end save at 154,201).
**The post-game of Eirika's clear saves is allowed** (the user's decision of 2026-10-23): a
clear save (`e_clear`, `b_clear`) loads with 0x16 but with `PLAY_FLAG_COMPLETE`
(`chapterStateBits` 0x20), the post-game's mark, so a cheat-assisted post-game route can start
from it, as from a floor's save (`br10_start`) or from Ephraim's `i_clear` or `ib_clear` (index
0x23). On Ephraim's route (`chapterModeIndex` 3) the indices are only a scene's map (the scene
at the end of his Chapter 20, `route_i20.keys` ticks 163,673 to 163,689), and the cheats go on
there.

- **Making one:** the bot with `ROUTE_CHEAT=<cheats>` (and `ROUTE_TICK=capture`, which it
  needs), for example
  `GATE0_SRAM_IN=port/test/saves/br10_start.sav ROUTE_OUT=... ROUTE_STOP=... ROUTE_TICK=capture ROUTE_SEARCH=1 ... ROUTE_CHEAT=durability MGBA_RUN_BP=$(GBA_BP) build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/route_bot.lua 3000000`,
  or `ROUTE_CHEAT=durability+funds+convoy` for all three (any order; the file names them in
  the order durability, funds, convoy).
- **Its label:** the key file's `# Generated` line names `ROUTE_CHEAT=<cheats>`, and a line
  `# Cheat-assisted (test coverage only, not fair play): made with the test cheat
  ROUTE_CHEAT=<cheats> (what they do); replay it with GATE0_CHEAT=<cheats> on both sides`
  follows it (after the `# Start:` line; for `durability` alone the words of every older
  file: "every item's uses back to full at each tick"). From a boosted start (the start
  save's `.sav.json` says `"what":"boosted start ..."`) a line `# Boosted, cheat-assisted (test
  coverage only, not story play): the start save is a boosted start ...` comes before it
  (coverage 12; only with a cheat, so no older header changes).
- **Replaying it:** `make -f port/test/Makefile chain-route CHAIN=...` (or `route`,
  `snd-route`) reads the `# Generated` line and runs both sides with `GATE0_CHEAT=<cheats>`;
  a check-all row for it is titled "cheat-assisted (GATE0_CHEAT=..., test coverage
  only)" and passes the cheats on its make line. `route_check.lua` needs
  `ROUTE_CHEAT=<cheats>` (with `ROUTE_TICK=capture`) to follow it; without them a replay
  leaves the route at the first menu that shows a refilled item's uses, the gold or a convoy
  item.
- **Its saves:** `chain-save` plays each save's key file with that file's own cheats, so the
  save at a cheat-assisted route's end is made with them (its `.sav.json` says
  `"cheat":"<cheats>"`) and is the start of the next route; the game wrote it, so it is a
  normal save, but its army was helped at every tick. **The mark goes on through every later
  save:** `sram_capture.lua` and `sram_boost.lua` (chain-save's tools) copy the start save's
  `"cheat"` into the `.sav.json` of the save they make, joined with their own run's cheats,
  so a save any number of hops after a cheat-assisted route still says it (coverage 12's
  cheat13; before, the mark lasted one save). A route from such a save is labelled with it
  ("Start save" column), and the bot says so in the key file too: when the `.sav.json` next
  to `GATE0_SRAM_IN` names `"cheat"`, a line `# Start save made with the test cheat (its
  .sav.json: "cheat":"durability"): the army this route starts with comes from a run with the
  test cheat durability, at some save before this one ...` follows the `# Start:` line. That
  line does not make the route itself cheat-assisted (only `ROUTE_CHEAT` in the `# Generated`
  line does), so the route is replayed without a cheat.
- **The cheat-assisted routes:** `route_br10.keys` (Lagdou Ruins floor 10 from the refit
  `br10r_start`: boosted, cheat-assisted with `durability`; "The post-game floors" above,
  tactics R10). Before it the cheat was checked on existing routes
  (port/test/README.md, "Test cheats", the results there): with it on both sides,
  `route_br9` (5,000 ticks from `br9_start`, and all 97,417) and `route_ch6` (10,000 ticks
  from `ch6_start`) leave the plain route (the GBA traces part at ticks 1,723 and 7,411, in
  the weapon menu's uses: "Spear 15" where the plain game shows "Spear 6"; no weapon breaks,
  where the plain `route_br9` breaks two; on the whole of `route_br9` the cheat game is
  still on floor 9's turn 6 at the file's end, where the plain one won it on turn 35) and
  still MATCH the GBA on every traced field, the refill's own fields included, on every
  tick. A single-use item (a key, a booster) is still used up. `funds` and `convoy` were
  checked the same way (cheat13: `route_e17`'s shop visit, `route_e15`'s supply, and a
  cheat-assisted post-game run from `e_clear` with all three).
  `route_gallery.keys` (coverage 12, **the item gallery**: boosted, edited and cheat-assisted with
  `durability+funds+convoy`, from `gal_start`, Ephraim's final chapter part 1 edited by
  `gallery/gal_prep.edit`; port/test/inputs/gallery/README.md): every item of the convoy cheat's list
  taken from the supply once and used once (161 of the 177; the 16 others not tried by rule), 159,706
  ticks; `make -f port/test/Makefile chain-route CHAIN=gallery` (every frame:
  `GATE0_SHOT_TICKS=0-159705`).

## World-map skirmishes, coverage 12

Five more skirmish files (branch `port-c12-skirmish`, 2026-10-23) on the nodes no route had
played from a story save: 6, 12, 17 (from Ephraim's save this time: coverage 11 won it only
from Eirika's), 21, and 28, **Melkaen Coast**. Node 28's chapter is Melkaen Coast's own
(index 0x39 = 57, its own map), so `route_sk28` is Melkaen Coast in real play: the walk to the
node on the world map, the monsters' map, the world map again (round 10 had it only by debug
jump, `dj_melkaen`, menu monkey). Each starts from a story save with monsters on that node
(coverage 11's scan of every story save's `gGMData.units[4..6]`, `port-plan/pending/c11-p.md`),
on a way with no other monsters, and was made as coverage 11's skirmishes: `ROUTE_SKIRMISH=N`
with the committed bot unchanged (SHA-1 `ecb67b50`, `route_lib.lua` `ca63892e`),
`ROUTE_TICK=capture ROUTE_SEARCH=1 ROUTE_SEARCH_BASE=0` and `ROUTE_MAX_TURN=40` (a cap none
reached; not in the `# Generated` line). Each skirmish is won with no unit lost (the search
kept no turn with a death) and stops 300 ticks after the world map's free cursor is back.
`make -f port/test/Makefile chain-route CHAIN=sk6|sk12|sk17i|sk21|sk28` runs each.

| File | Start save (FNV-1a) | Ticks | GBA frames | Plays |
|---|---|---|---|---|
| `route_sk6.keys` | `e11_start` (0865e05d) | 46,961 | 49,145 | `ROUTE_SKIRMISH=6`: Eirika from node 10 to node 6 (the world map shows monster class 0x53); the skirmish on Chapter 6's map (index 7, loaded at 1,660) is won on turn 7 (46,497); a game save (46,562); the world map (46,660) |
| `route_sk12.keys` | `i17_start` (4117ac5f) | 37,249 | 38,771 | `ROUTE_SKIRMISH=12`: Ephraim from node 20 to node 12, Hamill Canyon (monster class 0x5C); the skirmish on Eirika's Chapter 13's map (index 13, 1,402) is won on turn 6 (36,785); a game save (36,850); the world map (36,948) |
| `route_sk17i.keys` | `i18_start` (6fb1dc12) | 35,434 | 37,177 | `ROUTE_SKIRMISH=17`: Ephraim from node 21 to node 17 (monster class 0x5A; coverage 11's try from `i15_start` met class 0x60 there and was not won); the skirmish on his Chapter 13's map (index 26, 1,730) is won on turn 9 (34,970); a game save (35,035); the world map (35,133) |
| `route_sk21.keys` | `e19_start` (d303fb6f) | 44,699 | 46,784 | `ROUTE_SKIRMISH=21`: Eirika from node 22 to node 21 (monster class 0x64); the skirmish on Chapter 17's map (index 17, 1,641) is won on turn 7 (44,235); a game save (44,300); the world map (44,398); in the last 300 ticks the bot walks on to the story's node and Chapter 19's map loads (44,582) |
| `route_sk28.keys` | `e20_start` (0eb1f0e1) | 110,900 | 116,973 | `ROUTE_SKIRMISH=28`: Eirika from node 23 to node 28, **Melkaen Coast** (monster class 0x61); its own map (index 57, 1,222), four units fight (characters 0x01, 0x04, 0x0F, 0x1E), won on turn 28 (110,436); a game save (110,501); the world map (110,599); Chapter 20's map loads in the last 300 ticks (110,766) |

**Byte for byte:** each file made again with the committed bot and the same settings comes out
identical (all five).

**PC check (the plain PC, no NULL probe; 2026-10-23, `port-plan/pending/c12-skirmish.md`):** each
file through the lag replay on 21189271's game code (main 9ef060ea + port-cover11), every traced
field on every tick, frame15 every 60 ticks and on **every tick of each route's busy windows**
(battle animations, map animations, events, menus, the world map, and the first 1,200 and last 600
ticks: 72-85% of each route's ticks, at most 64 windows a route, see the note below):

| File | Ticks compared | GBA lag VBlanks (ticks) | Frames compared (GBA = PC) | Result |
|---|---|---|---|---|
| `route_sk6` | 0-46,960 | 2,182 (1,056) | 36,501 (78% of the ticks) | **MATCH**: every traced field on every tick; 22 frames differ, all raster-timing (P-17, labelled) |
| `route_sk12` | 0-37,248 | 1,520 (841) | 28,995 (78%) | **MATCH**; 17 raster-timing frames |
| `route_sk17i` | 0-35,433 | 1,741 (857) | 25,657 (72%) | **MATCH**; 26 raster-timing frames |
| `route_sk21` | 0-44,698 | 2,083 (925; 1 early flush) | 34,171 (76%) | **MATCH**; 31 raster-timing frames |
| `route_sk28` (Melkaen Coast) | 0-110,899 | 6,076 (3,201) | 94,740 (85%) | **MATCH**; 74 raster-timing frames |

Every lag VBlank ran at the GBA's position, with the main-loop events equal on every tick; no
fault. So every skirmish start of the five (the world map's walk onto the monsters, D-83's
`HideWmFace` with no face controller) plays as on the GBA too.

**Note, `GATE0_SHOT_TICKS` (harness, not the game):** the PC hook keeps only the first 64 ranges
(`port/test/pc/gate0_hook.c`, `sShotRanges[64]`; `ReadShotTicks` stops there without a word) while
the GBA capture takes them all, and the compare then compares the frames both sides have and still
says MATCH (only its line "GBA has N frames, PC has M" shows the gap). A first `route_sk17i` run
with 70 ranges compared 23,536 of 25,219 GBA frames that way; the windows above are capped at 64
ranges (the closest ones merged). No committed rule passes more than 8 ranges.

## The post-game with the real army (coverage 12)

The Tower of Valni and the Lagdou Ruins played floor by floor with the **real army** of
`e_clear.sav` (the clear save of `route_e22.keys`, Eirika's whole story with the real army,
tactics E7): no boost and no refit by a tool. Its living units at the post-game's start are
Eirika (Great Lord, level 1, 20 HP), Ephraim (Great Lord, level 16), Franz (Paladin, level 12)
and Myrrh (Manakete, level 20); Ephraim's one weapon is a Siegmund with 3 uses, Myrrh's a
Divinestone with 3, Franz has a Steel Blade (21) and Vidofnir (3); 14,145 gold. The
post-game's recruits join on the way (the bot first moves Caellach, character 0x29, on Tower
floor 4, Orson, 0x24, on floor 7, Riev, 0x28, on floor 8, Ismaire, 0x2B, on Ruins floor 1).
The only help is what any player may do: the world map's armory and vendor before each
dungeon (the bot's rules "PR", below). Each file starts from its floor's turn-1 suspend save
(the title menu's Resume Chapter; the first one of each dungeon from a game save) and stops
at the next floor's first free cursor (`ROUTE_STOP=pp:N`); the SRAM at its last tick
(`sram-capture SAVE_AT=tick:T`) is the next file's start. Made with `ROUTE_TICK=capture
ROUTE_SEARCH=1 ROUTE_SEARCH_BASE=0 ROUTE_PG_REAL=1`; the `# Generated` line names every
setting, the post-game's too (`port-plan/pending/c12-postgame-real.md`). Keys made by the
bot only (no TAS input).

| File | Start save (FNV-1a) | Ticks | GBA frames | Plays (real army) | End save |
|---|---|---|---|---|---|
| `route_et1.keys` | `e_clear` (44491be0) | 26,429 | 27,202 | with `ROUTE_POSTGAME=1 ROUTE_WM_NODE=26`: the title's Extras, Map, the post-game world map; the armory of node 23 and its vendor (PR); the Tower (node 26); floor 1 (index 36) won turn 3, "Proceed to the next floor?" yes; floor 2's first free cursor | `et2_start` (`tick:26429`, a63a8dfb) |
| `route_et2.keys` | `et2_start` (a63a8dfb) | 46,582 | 49,135 | floor 2 (index 37), won turn 18 | `et3_start` (`tick:46582`, 1e70173f) |
| `route_et3.keys` | `et3_start` (1e70173f) | 36,542 | 38,335 | floor 3 (index 38), won turn 14 | `et4_start` (`tick:36542`, f1748238) |
| `route_et4.keys` | `et4_start` (f1748238) | 23,413 | 24,502 | floor 4 (index 39), won turn 6 | `et5_start` (`tick:23413`, 1bbd69ce) |
| `route_et5.keys` | `et5_start` (1bbd69ce) | 64,847 | 67,296 | floor 5 (index 40), won turn 10 | `et6_start` (`tick:64847`, 0c1430e8) |
| `route_et6.keys` | `et6_start` (0c1430e8) | 71,178 | 75,494 | floor 6 (index 41, the cracked walls), won turn 31 | `et7_start` (`tick:71178`, 57df7715) |
| `route_et7.keys` | `et7_start` (57df7715) | 59,936 | 63,847 | floor 7 (index 42), won turn 17 | `et8_start` (`tick:59936`, 665d7199) |
| `route_et8.keys` | `et8_start` (665d7199) | 60,388 | 63,786 | floor 8 (index 43), the last, won turn 15 (59,850; the last enemy, a Cyclops that never leaves (11,2), falls to the PR siege); the dungeon record (A); a game save (59,915); the world map's free cursor; `ROUTE_STOP=wm:300` | `er1_start` (`tick:60388`, db85f593: slot 0 the game save after the Tower) |


The Lagdou Ruins (node 27, floors 1 to 10, chapter indices 46 to 55) with the same army, from
the SRAM at `route_et8`'s end (the game save after the Tower): the same settings, the first
file with `ROUTE_POSTGAME=1 ROUTE_WM_NODE=27`.

| File | Start save (FNV-1a) | Ticks | GBA frames | Plays (real army) | End save |
|---|---|---|---|---|---|
| `route_er1.keys` | `er1_start` (db85f593) | 88,602 | 92,823 | the post-game world map; the armory of node 9 and its vendor (PR: 4,680 gold to spend; Eirika buys a Steel Sword, Ephraim sells two Chest Keys and buys a Steel Lance and an Iron Lance, Franz sells an Iron Bow and buys a Steel Lance, Caellach sells a Chest Key and buys a Steel Axe, an Iron Blade and a Hammer, Orson a Steel Lance and an Iron Blade; at the vendor Orson sells a Red Gem for a Vulnerary, Riev buys one; 3,280 gold left); the Ruins (node 27); floor 1 (index 46) won turn 13 (88,163); floor 2's first free cursor | `er2_start` (`tick:88602`, 5b8115e9) |
| `route_er2.keys` | `er2_start` (5b8115e9) | 94,878 | 100,218 | floor 2 (index 47), won turn 17 (94,439); Caellach (0x29) falls on turn 6 (the search kept that turn: every policy lost a unit) | `er3_start` (`tick:94878`, c79f287e) |
| `route_er3.keys` | `er3_start` (c79f287e) | 136,724 | 145,051 | floor 3 (index 48, 42 enemies, the cracked walls), won turn 36 (136,285); Orson (0x24) and Ismaire (0x2B) fall; the next floor has no battle preparations (the game deploys the units itself: 5 on floor 4) | `er4_start` (`tick:136724`, f40f4124) |
| `route_er4.keys` | `er4_start` (f40f4124) | 128,554 | 138,001 | floor 4 (index 49, 36 enemies, AI2 3 behind cracked walls), **not won**: `ROUTE_MAX_TURN=99`, the bot stops at turn 99 with 5 of the 36 enemies left (from turn 49 on), no unit lost. The five deployed: Eirika, Ephraim, Franz, and Myrrh and Riev with no weapon they can use (Myrrh's Divinestone broke on Tower floor 1); by turn 55 Ephraim's and Franz's weapons are worn out (Ephraim keeps a Swordslayer he cannot use), the supply holds none they can use, and the five left (AI2 3) never come | none |

On the plain PC (no NULL probe; `make route`'s three steps, every traced field on every tick,
frame15 every 60 ticks and on every tick of each route's battle, map-animation, event, menu
and world-map windows, merged to the PC hook's 64 ranges): `route_et1` to `et8`, `route_er1`
and `er2` MATCH the GBA (raster-timing frames labelled, no oracle-limit frame, no fault).
`route_er3` and `route_er4` are DIFFERENT on 20 and 4 ticks, every game value equal on every
tick: in a battle animation the lag replay runs a GBA lag VBlank (where `efxFlashBG`, tree 0,
ends) after a `Proc_Start` that the GBA made after it (`efxExcalibur_Loop_Main`'s
`efxExcaliburBG3` then `efxExcaliburBGCOL3`; `route_er4` tick 2,198, `route_er3` 46,646), so
the proc slot history parts from the GBA's, and later retail reads of recycled proc memory see
other leftovers (the map windows' `windowQuadrant`: BG0/BG1 for 3 ticks after a phase or a
Wait; battle frames the raster redraw cannot label): H-c12-pr-1 in
`port-plan/pending/c12-postgame-real.md`.

**PR, the post-game with the real army** (`L.PR` in `route_bot.lua`, on only with
`ROUTE_PG_REAL=1`, which no older file has; the older files are unchanged, and every file
above regenerates byte for byte with the committed bot):
- the world map's shopping first: v4's shop plan (made at the run's first free world-map
  cursor) is walked to before `ROUTE_WM_NODE`'s dungeon. With `ROUTE_POSTGAME` alone the
  dungeon's node won and the plan was never used (scratch `et1a`: Ephraim's Siegmund and
  Myrrh's Divinestone broke on floor 1, and the army went on with two units unarmed).
- a bigger refit: a fighter whose usable weapons have fewer than 60 uses in all buys up to 3
  weapons (about 80 uses), none dearer than 2,000 gold, keeping 500 gold; a Door Key or a
  Chest Key is sold when a slot is needed; then the vendor's heal items (v10's rule: an
  Elixir for each unit that has none, lords first, else a Vulnerary). What the armory sells
  is not chosen again at the vendor (scratch `et1b` sold Ephraim's one Door Key at both, and
  the full unit's Elixir went to the supply again and again). From `e_clear.sav` (node 23):
  Ephraim sells the Door Key and buys a Silver Lance, a Killer Lance and an Axereaver, Franz
  a Silver Lance; at the vendor Ephraim sells the Chest Key and buys an Elixir (14,145 gold
  to 5,770).
- the siege (v9's rule, think "siege") on the post-game floors (indices 0x24 to 0x37) once
  every enemy left stands on its tile (AI1 3 to 5), boss or not: the damage exact, and a unit
  at 3/4 of its HP or more (not a lord) attacks it wherever it outlives the counter. Floor 8
  without it (scratch `et8a`): the Cyclops (60 HP, a Killer Axe, AI1 3) alone on (11,2) from
  turn 10, and the army waited out of its reach to turn 50.
- Lagdou Ruins floor 4 on (chapter indices 0x31 to 0x37, `L.PR.BACK`): a weak lord (fewer
  than `L.PR.BACK_HP` = 30 max HP: Eirika, level 1) keeps out of the cracked-walls rule, and
  her threat counts every cracked wall as open floor and the allies yet to move as out of the
  way (scratch `er4b`: every policy lost her on turn 1 or 2, walked next to the wall at (4,5)
  that Ephraim then broke); a strong lord (Ephraim) leads (`L.PR.lead`): he walks to the
  enemies as any fighter does, and only a tile whose threat could kill him is ruled out
  (scratch `er4c`, `er4d`: with no goal for a lord on these floors he stood still once Franz
  fell, and the army waited to turn 50).

## Feature routes (coverage 12)

Game features that no route played more than once (coverage 12, shard "features",
`port-plan/pending/c12-features.md`), each made by the bot from a story save (or a clear save)
with the coverage-12 settings of `route_bot.lua`'s header comment (`ROUTE_VISIT`, `ROUTE_DOOR`,
`ROUTE_SHOP`, `ROUTE_SUPPORTS`, `ROUTE_WATCH`, `ROUTE_PREFIX`, `ROUTE_USEITEM`, `ROUTE_PROMO_CHOICE`,
`ROUTE_TRAINEE`, `ROUTE_TALKRUSH`, `ROUTE_WMSHOP`, `ROUTE_LORDPROMO`; each file's `# Generated` line names
the ones it was made with, `ROUTE_TICK=capture`). With `ROUTE_WATCH=1` every event after the route's first
free cursor gets A, never Start: the visit, recruit, support, arena and promotion scenes are watched to
their end. The hand-made menu files (the Guide, the Sound Room, the Extras support viewer) are in
`features/` (`features/README.md`). Every file below regenerates byte for byte with this branch's bot
(2026-10-23: the settings of its row, `ROUTE_TICK=capture`, `ROUTE_VERBOSE=1` and, with `ROUTE_SEARCH=1`,
`ROUTE_SEARCH_BASE=0`, from its start save).

| File | Start save (FNV-1a) | Ticks | GBA frames | Setting | Plays |
|---|---|---|---|---|---|
| `route_f_house_ch1.keys` | `ch1_start` (f2c17758) | 2,535 | 2,656 | `ROUTE_VISIT=1,2 ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_STOP=visit:300` | Chapter 1, turn 1: Seth visits the house at (10,4) (1,468, the talk to 1,811), Eirika the house at (13,6) (1,861 to 2,234): the two talk-only houses |
| `route_f_village_ch2.keys` | `ch2_start` (a953b552) | 6,547 | 6,816 | `ROUTE_VISIT=6,2 ROUTE_VISIT_N=2 ROUTE_SHOP=4,5,7,14,6c ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_STOP=shop:300` | Chapter 2: Vanessa visits the village at (4,2) (a Red Gem, 1,921 to 2,487), Seth the one at (7,2) (an Elixir, 4,267 to 4,749); Franz walks onto the armory at (5,7), sells a Vulnerary (5,297) and buys an Iron Lance (5,782); the shop closes at 6,246 |
| `route_f_village_ch4.keys` | `ch4_start` (6fcf3532) | 42,016 | 43,892 | `ROUTE_VISIT=2,4 ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_STOP=visit:300` | Chapter 4: Seth visits the village at (8,2) (an Iron Axe, 7,213 to 7,737) and later the one at (1,11), whose event makes Lute join (41,178 to 41,715): an item village and a recruit village |
| `route_f_village_ch5.keys` | `ch5_start` (f7d234d4) | 35,221 | 36,867 | `ROUTE_VISIT=2,4,6 ROUTE_SHOP=1,6,10,6c,- ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_STOP=visit:300` | Chapter 5 (index 6): all four villages, (12,19) (a Dracoshield, by Vanessa, 2,299), (12,10) (an Armorslayer, by Seth, 16,675), (5,1) (a Torch, by Seth, 33,579), (5,6) (a Secret Book, by Franz, 34,264); the vendor was not reached before the last visit (the stop) |
| `route_f_doorkey_ch8.keys` | `ch8_start` (6b39183a) | 8,391 | 8,793 | `ROUTE_DOOR=2 ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_STOP=door:300` | Chapter 8 (index 9): Seth walks next to the door at (20,7) and opens it with his Door Key (Door, 8,014) |
| `route_f_chestkey_ch8.keys` | `ch8_start` (6b39183a) | 37,507 | 39,240 | `ROUTE_CHEST=8 ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_STOP=chest:300` | Chapter 8 (index 9): Neimi walks to the chest at (19,4) and opens it with a Chest Key (37,190; `route_f_chest_ch8` opens it with Colm's Lockpick) |
| `route_f_arena_e10.keys` | `e10_start` (996ac19b) | 85,050 | 90,000 | `ROUTE_ARENA=12,6,8 ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_STOP=arena:600` | Eirika's Chapter 10 (index 11), turn 12: Neimi (7 HP) takes Arena at (12,6) (82,325), the wager [Yes], the fight, and she dies in it: her death quote, the arena master's "you lost" (the screens close at 84,449): an arena loss |
| `route_f_arena_i12.keys` | `i12_start` (248af190) | 53,732 | 56,331 | `ROUTE_ARENA=7,1,2 ROUTE_ARENA_YIELD=150 ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_MAX_TURN=40 ROUTE_STOP=arena:600` | Ephraim's Chapter 12 (index 25): Seth takes Arena at (7,1) (51,534), the wager [Yes], and yields mid-fight (B held 90 ticks at 52,396); the screens close at 53,131: an arena yield |
| `route_f_supports_tower_e.keys` | `e_clear` (44491be0; Eirika's real-army clear save) | 120,070 | 126,137 | `ROUTE_POSTGAME=1 ROUTE_WM_NODE=26 ROUTE_SUPPORTS=0x0f,1 ROUTE_SUPPORTS_N=3 ROUTE_WATCH=1 ROUTE_SEARCH=1 ROUTE_STOP=support:300` | The post-game: the world map's armory at node 23 (shopping, 903), the Tower of Valni's floors 1 to 3 with Ephraim next to Eirika every turn: their C (2,801 to 4,124, floor 1), B (67,151 to 69,627, floor 2) and A (118,494 to 119,769, floor 3) conversations, watched with A |
| `route_f_supports_tower_i.keys` | `ib_clear` (05d7fd4a; Ephraim's **boosted** clear save) | 65,066 | 70,100 | `ROUTE_POSTGAME=1 ROUTE_WM_NODE=26 "ROUTE_SUPPORTS=2,4;1,2;0x0f,1" ROUTE_SUPPORTS_N=6 ROUTE_WATCH=1 ROUTE_SEARCH=1 ROUTE_STOP=support:300` | Ephraim's post-game: the world map's vendor and armory at node 23 (903), the Tower's floors 1 and 2 with Seth next to Franz, Eirika next to Seth and Ephraim next to Eirika every turn: on floor 1's first turn the C conversations of Seth and Franz (3,308), Eirika and Seth (4,246), Ephraim and Eirika (5,372); on floor 2 the B conversations of Ephraim and Eirika (49,457), Eirika and Seth (56,218), Seth and Franz (63,579 to 64,765), watched with A |
| `route_f_lordpromo_e16.keys` | `e16_start` (95dd0bb9) | 107,773 | 113,935 | `ROUTE_PREFIX=port/test/inputs/route_e16.keys:95729 ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_STOP=saveto:17` | `route_e16.keys`' first 95,729 ticks as they are (Chapter 16 to its last turn), then the bot ends the chapter (turn 27) and watches Chapter 16's ending with A (`route_e16` skips it with Start): the scenes (96,627 to 106,365), the "promote now?" questions at their first answer, and both Lord promotions (`PROM` in `EventScr_089F73F0`/`EventScr_089F7470`): Eirika with the Lunar Brace to Great Lord (about 101,660), Ephraim with the Solar Brace (about 102,810 to 103,730), the stat screens; the game save (107,472) and 300 ticks |
| `route_f_secretshop_e14.keys` | `f_mc_start` (105a9dbd; **edited start**, below) | 21,713 | 22,821 | `ROUTE_TALKRUSH=0x19,0x1c ROUTE_SHOP=0x1c,4,4,6b,03 ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_STOP=shop:300` | Eirika's Chapter 14 with Rennac (red, holding the Member Card) moved next to the secret shop and L'Arachel next to him: the Talk command is not offered on turns 1 and 2 (the bot's Talk plan falls back to Wait); on turn 3 L'Arachel walks after him and talks (14,506): their recruit conversation, Rennac joins; she is lost in that enemy phase; Rennac walks onto the secret shop at (4,4) (19,948): "Shhh... This is a secret shop!", he sells his Steel Sword (300 gold, 20,277) and buys a Lockpick (1,200 gold, 20,860); the shop closes at 21,412 |
| `route_f_thirdtier_ch6.keys` | `f_tt_start` (97500f29; **edited start**, below) | 3,014 | 3,156 | `ROUTE_USEITEM=7:88 ROUTE_PROMO=1 ROUTE_PROMO_CHOICE=2 ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_STOP=promo:300` | Chapter 6, turn 1: Ross (a Fighter at level 10 by the edit, once a Journeyman) uses a Master Seal (Item, Use, 1,424): the class choice (Hero, Warrior) takes the second, Warrior (1,516); the promotion animation, the stat screen (Warrior level 1), "You can now use" bows; the screens close at 2,713: a trainee's third tier |
| `route_f_promochoice_i14.keys` | `i14_start` (83286ce8) | 6,074 | 6,379 | `ROUTE_PROMO=1 ROUTE_PROMO_CHOICE=2 ROUTE_WATCH=1 ROUTE_SEARCH=0 ROUTE_STOP=promo:300` | Ephraim's Chapter 14, turn 1: Gerik (Mercenary, level 14) uses his Hero Crest (v7's rule, Item, Use, 4,431): the class choice (Hero, Ranger) takes the second, Ranger; the mounted promotion animation, the stat screen (Ranger level 1), "You can now use" bows; the screens close at 5,773: the branched choice in story play (the story routes take the first class) |

**The edited starts** (test coverage only, not story play; `sram_boost.lua` with `BOOST_STATS=0` and
`BOOST_EDIT_FILE`, the edit files in `features/`, as coverage 11's `f_promo_start`): `f_mc_start` is
`e14_start` played with `route_e14.keys`, `features/mc_e14.edit` at the end of tick 1,600 (Rennac to (3,5),
L'Arachel to (3,6)) and the SRAM at tick 1,725 (the turn-1 suspend). Every secret shop, the world map's
too, lets only a unit holding a Member Card in (`src/eventinfo.c`, `src/prep_itemscreen.c`); only Rennac
holds one, and in the story saves he runs off with it (bot tries to reach him failed: in Ephraim's
Chapter 14 closed doors kept L'Arachel from him; in Eirika's he went over the roofs, and with
`ROUTE_TALKRUSH` she was lost on turn 8). `f_tt_start` is `ch6_start`
played with `boost_ch6.keys`, `features/tt_ch6.edit` at the end of tick 800 (Ross a Fighter at level 10
with a Master Seal in his third slot) and the SRAM at tick 1,669.

    MGBA_RUN_BP=<GBA_BP> GATE0_SRAM_IN=e14_start.sav GATE0_KEYS=port/test/inputs/route_e14.keys BOOST_AT=1600 BOOST_STATS=0 \
      BOOST_EDIT_FILE=port/test/inputs/features/mc_e14.edit SRAM_AT=tick:1725 SRAM_OUT=f_mc_start.sav \
      build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/sram_boost.lua 40000000

**PC check (2026-10-23, `port-c12-features`; the plain PC, no NULL probe, release build, through the lag
replay as `make route` does; every frame of the windows that matter, the scenes, menus, battles, shops and
conversations, with `GATE0_SHOT_TICKS` on both sides, frame15 every 60 ticks elsewhere).** All but
`route_f_lordpromo_e16` (below) MATCH: every traced field on every tick; the frames that differ are only the
raster-timing labelled ones (P-17):

| File | Frames compared (windows of every frame) | Raster-timing labelled |
|---|---|---|
| `route_f_house_ch1` | 1,159 (1,400-2,534) | 21 |
| `route_f_village_ch2` | 5,072 (1,500-6,546) | 34 |
| `route_f_village_ch4` | 3,029 (1,800-2,500, 7,150-7,900, 41,100-42,015) | 16 |
| `route_f_village_ch5` | 5,182 (2,250-4,400, 16,600-17,400, 33,500-35,220) | 19 |
| `route_f_doorkey_ch8` | 6,867 (1,550-8,390) | 34 |
| `route_f_chestkey_ch8` | 2,257 (1,650-2,300, 7,600-8,200, 37,100-37,506) | 15 |
| `route_f_arena_e10` | 5,205 (1,450-2,500, 82,250-85,049) | 19 |
| `route_f_arena_i12` | 4,075 (700-1,600, 51,400-53,731) | 18 |
| `route_f_supports_tower_e` | 10,580 (1,550-4,400, 25,000-25,600, 67,050-69,900, 90,550-91,300, 118,400-120,069) | 119 |
| `route_f_supports_tower_i` | 13,588 (700-6,995, 25,300-26,000, 49,400-52,000, 56,150-57,700, 63,500-65,065) | 117 |
| `route_f_secretshop_e14` | 4,901 (700-1,900, 5,600-5,900, 14,400-15,700, 19,900-21,712) | 16 |
| `route_f_thirdtier_ch6` | 2,326 (700-3,013) | 1 |
| `route_f_promochoice_i14` | 2,830 (700-1,800, 4,400-6,073) | 20 |

**`route_f_lordpromo_e16`: DIFFERENT, one frame (finding N-C12-F1, `port-plan/pending/c12-features.md`).**
Every traced field is equal on every tick; 12,882 frames compared (every frame of 96,500-107,772), 21
raster-timing labelled, and one more frame differs: tick **99,952**, Chapter 16's ending (chapter index 16
between scenes 67 and 71), the light effect of `MapEventcallFx2` (`ProcScr_089A4434`,
`src/mapanim_eventcall.c`). Tick 99,952 is a GBA lag tick (two VBlanks, `lag_at` [5] on both sides): the
GBA's picture is the previous one again (the light over the whole screen), the PC's has lines 0 to about
78 without it; 99,953 is equal again. A GBA probe from the snapshot (diagnostic only) puts the GBA's lag
VBlank inside the step's `LZ77UnCompWram` of the effect's tilemap to `gGenericBuffer` (EWRAM; interrupted
BIOS address 0x34C), that is before the step's last stores to the VSync-tree proc `ProcScr_089A448C`
(`unk29 = 1`, `unk2A = 0`), so the GBA's lag `OnVBlank` leaves `gBraceletSealBreakFxStretchEnabled` (the switch of the BG2
line-scroll HBlank handler `BraceletSealBreakFx_OnHBlank`) at 1 and the next VBlank sets it to 0. A decompression into
other memory is no lag-position event and the capture did not list it as `lag_in_other_copy`, so the lag
replay runs that VBlank before the next event, after those stores (README.md "Lag position"): probably the
lag replay's limit, as N-C11-1; the PC side was not probed. Snapshot: `~/fe8-snapshots/c12-feat-lord/tick99940`
(parts `gba`, `pclag`, set `f730edcc44da`); resumed to 99,980 it shows the same frame at 99,952.

## Ephraim's route (Chapter 8 again, Ephraim's Chapters 9 to 20)

These thirteen files take the route split's second entry, Ephraim. They work as
the chain above: each boots both sides with its start save, takes the title
menu to Continue (the save loads at tick 708), plays one chapter and stops 300
ticks after the game's own end save. All thirteen were made with
`ROUTE_TICK=capture` ("Tactics v4", tick rule) and tactics v5 for Ephraim's
chapters ("Tactics v5" below; from his Chapter 12 on, v6; from his Chapter
14 on, v7; from Scorched Sand on, v8; from River of Regrets on, v9; from
Darkling Woods on, v10).

| File | Start save (made from) | Ticks | GBA frames | Clears | End save (tick written) |
|---|---|---|---|---|---|
| `route_ch8i.keys` | `ch8_start` (as `route_ch8.keys`) | 87,115 | 91,404 | Chapter 8 (index 9, Seize (10,2)), turn 17; then the route split, Ephraim (`ROUTE_SPLIT=ephraim`) | `i9_start`: `save:23` (86,814) |
| `route_i9.keys` | `i9_start`: `route_ch8i.keys` from `ch8_start`, `save:23` | 117,128 | 124,078 | Ephraim's Chapter 9 (index 23, Fort Rigwald, Seize (22,7)), turn 30 (after shopping) | `i10_start`: `save:24` (116,827) |
| `route_i10.keys` | `i10_start`: `route_i9.keys` from `i9_start`, `save:24` | 71,983 | 76,077 | Ephraim's Chapter 10 (index 24, Turning Traitor, hold out to turn 11), turn 11 (after shopping) | `i11_start`: `save:62` (71,682) |
| `route_i11.keys` | `i11_start`: `route_i10.keys` from `i10_start`, `save:62` | 80,653 | 84,512 | Ephraim's Chapter 11 (index 62, Phantom Ship, Rout), turn 11 (after shopping) | `i12_start`: `save:25` (80,352) |
| `route_i12.keys` | `i12_start`: `route_i11.keys` from `i11_start`, `save:25` | 76,501 | 80,561 | Ephraim's Chapter 12 (index 25, Landing at Taizel, Defeat Boss), turn 12 | `i13_start`: `save:26` (76,200) |
| `route_i13.keys` | `i13_start`: `route_i12.keys` from `i12_start`, `save:26` | 121,631 | 127,669 | Ephraim's Chapter 13 (index 26, Fluorspar's Oath, Rout), turn 14 | `i14_start`: `save:27` (121,330) |
| `route_i14.keys` | `i14_start`: `route_i13.keys` from `i13_start`, `save:27` | 181,401 | 192,028 | Ephraim's Chapter 14 (index 27, Father and Son, Seize (14,2)), turn 45 | `i15_start`: `save:28` (181,100) |
| `route_i15.keys` | `i15_start`: `route_i14.keys` from `i14_start`, `save:28` | 140,981 | 147,846 | Chapter 15 on his route (index 28, Scorched Sand, Rout), turn 21 (after shopping) | `i16_start`: `save:29` (140,680) |
| `route_i16.keys` | `i16_start`: `route_i15.keys` from `i15_start`, `save:29` | 122,918 | 129,609 | Chapter 16 on his route (index 29, Ruled by Madness, Seize (13,3)), turn 21 (after shopping) | `i17_start`: `save:30` (122,617) |
| `route_i17.keys` | `i17_start`: `route_i16.keys` from `i16_start`, `save:30` | 127,656 | 134,236 | Chapter 17 on his route (index 30, River of Regrets, Defeat Boss), turn 16 (after shopping) | `i18_start`: `save:31` (127,355) |
| `route_i18.keys` | `i18_start`: `route_i17.keys` from `i17_start`, `save:31` | 42,492 | 44,577 | Chapter 18 on his route (index 31, Two Faces of Evil, Rout), turn 10 (after shopping) | `i19_start`: `save:32` (42,191) |
| `route_i19.keys` | `i19_start`: `route_i18.keys` from `i18_start`, `save:32` | 141,800 | 150,049 | Chapter 19 on his route (index 32, Last Hope: turn 14 or the boss), turn 14 (after shopping) | `i20_start`: `save:33` (141,499) |
| `route_i20.keys` | `i20_start`: `route_i19.keys` from `i19_start`, `save:33` | 164,061 | 173,876 | Chapter 20 on his route (index 33, Darkling Woods, Seize (11,11)), turn 38 (after shopping) | `i21_start`: `saveto:34` (163,760) |
| `route_i21.keys` | `i21_start`: `route_i20.keys` from `i20_start`, `saveto:34` | 80,293 | 85,150 | the final chapter on his route, part 1 (index 34, Sacred Stone, Defeat Boss: Lyon), turn 29, the real army, no loss (`ROUTE_SEARCH=1`, Tactics I11); stops at part 2's first free cursor (index 35) | `i22_start`: `tick:80292` (part 2's turn-1 suspend save; title menu: Resume Chapter) |

The saves (never committed; first 32 KiB, `sram_fnv`): `i9_start` 47a0668a,
`i10_start` 7753e267, `i11_start` 63d6cfbb (Ephraim's Chapter 11, Phantom
Ship, index 62), `i12_start` 248af190, `i13_start` c65824de, `i14_start`
83286ce8 (Ephraim's Chapter 14, Father and Son, index 27), `i15_start`
80a97b37 (Scorched Sand, index 28), `i16_start` 86c72e1b (Ruled by Madness,
index 29), `i17_start` 4117ac5f (River of Regrets, index 30), `i18_start`
6fb1dc12 (Two Faces of Evil, index 31), `i19_start` c11d85bb (Last Hope,
index 32), `i20_start` 3f60ce37 (Darkling Woods, index 33), `i21_start`
99fb8d28 (the final chapter on his route, Sacred Stone part 1, index 34).
`chain-save CHAIN_SAVE=i21_start` makes them from `ch8_start`, and
`chain-route CHAIN=ch8i|i9|i10|i11|i12|i13|i14|i15|i16|i17|i18|i19|i20` runs a
route from its own save, as for the chain above.

- **Chapter 8 again** (`route_ch8i.keys`). The same play as
  `route_ch8.keys`: the tick rule only moves its masks one tick later from
  tick 18,362, and three by the seize (the frame rule's late ticks at early
  flushes). Kyle dies in the turn 3 enemy phase and Forde in turn 9; Eirika
  seizes (10,2) on turn 17 (index 9 → 56 at 85,811; the game save with index
  56 at 85,876). In the split scene the bot takes "Go with Ephraim to Grado"
  (Down, then A at 86,639) and Yes; `chapterModeIndex` 3, 56 → 23 at 86,750,
  the save menu (index 23 at 86,814: `i9_start`). `AutolevelSecondaryLord`
  levels Eirika (to level 10) before she leaves.
- **Ephraim's Chapter 9** (index 23, Fort Rigwald). The whole army goes with
  Ephraim (Seth, Franz, Gilliam, Moulder, Natasha, Garcia, Neimi, Colm, Lute;
  Ephraim is level 4). The save loads on the world map; the bot shops at node 2,
  Ide (ticks 883 to 6,466): Seth sells an Iron Axe and buys an Iron Lance and
  a Javelin; Franz and Gilliam an Iron Lance and a Javelin each; Garcia a
  Steel Axe; Ephraim a Javelin (15,000 gold to 12,095). Then node 14; the map
  loads at 6,814 (the opening borrows maps 63 and 27 at 7,359 to 7,382), the
  first free cursor at 7,645. Tana joins unarmed in her cell (11,1) and stays
  there. Franz talks to Amelia on turn 8 (tick 35,451) and she joins; Neimi
  and Amelia die in the turn 10 enemy phase (the loss record rewrites the game
  save at 51,043). The boss, Gheb, sits on the throne (22,7) with
  `GuardTileAI` and can be reached only from (22,8): Gilliam's Javelin from
  two tiles away on turns 22 to 29, Seth (turn 22) and Franz's Axereaver
  (turn 29) next to him, and Garcia's Hand Axe. Gheb dies on turn 29 and
  Ephraim seizes on turn 30 (tick 116,657); 23 → 24 at 116,763, the save at
  116,827 (`i10_start`), Chapter 10's map at 117,125.
- **Ephraim's Chapter 10** (index 24, Turning Traitor: hold out until turn 11
  or defeat the boss). The save loads on the world map; the bot shops at
  node 2 again: a Javelin for Seth, an Iron Lance, a Slim Lance and a Javelin
  for Tana (12,095 to 10,485). The map loads at 3,785 (map 76 borrowed at
  4,330 to 4,345), the first free cursor at 4,584. Hold-out mode: the army
  keeps round Ephraim in the top corner; Seth fights his way south. Moulder
  dies in the turn 6 enemy phase, Natasha in turn 7, Lute in turn 8. Turn
  11's event wins: 24 → 62 at 71,618, the save at 71,682 (`i11_start`),
  Ephraim's Chapter 11's map at 71,980. Duessel and Cormag (Talk recruits)
  are not talked to; both join in the ending (`EventScr_LoadUniqueAlly`,
  `EventScr_StrictLoadUniqueAlly`).
- **Losses and army at Ephraim's Chapter 11** (`route_check.lua`'s last
  tick): Ephraim (level 6), Seth, Franz, Gilliam, Garcia, Colm, Tana, Cormag
  and Duessel. Dead on this branch since the split: Neimi and Amelia
  (Chapter 9), Moulder, Natasha and Lute (Chapter 10); both healers are
  gone.
- **Ephraim's Chapter 11** (index 62, Phantom Ship: Rout; monsters on a
  ship). The save loads on the world map; the bot shops at node 15 (ticks
  878 to 2,641): a Javelin for Cormag, a Hand Axe for Duessel (10,485 gold
  to 9,785). The map loads at 2,770 (maps 63 and 27 borrowed at 3,317 to
  3,328), the first free cursor at 3,543. L'Arachel and Dozla arrive as
  green units on turn 5; Ephraim talks to L'Arachel (tick 73,427, turn
  9) and she talks to Dozla (73,559); both join (they would join in the
  ending anyway, `EventScr_LoadUniqueAlly`). Cormag dies in the turn 4 enemy
  phase (the loss record rewrites the game save at 27,460), Colm in turn 6.
  The last enemy falls on turn 11: 62 → 25 at 80,288, the save at 80,352
  (`i12_start`), Chapter 12's map at 80,408.
- **Ephraim's Chapter 12** (index 25, Landing at Taizel: Defeat Boss). The
  save loads straight onto the map (tick 711; no world map), the first free
  cursor at 1,486. Caellach leaves on turn 2 and the boss, a Cyclops
  (character 0x5C), arrives at (17,0). Franz visits Ewan's village (5,5)
  (tick 49,765, turn 7; "recruit villages", Tactics v6) and Ewan joins;
  he walks toward Marisa (his Talk recruit, spared meanwhile) but the boss
  falls first, on turn 12: 25 → 26 at 76,136, the save at 76,200
  (`i13_start`). Marisa, still red, joins in the ending
  (`EventScr_LoadUniqueAlly`, Ewan alive). No losses.
- **Ephraim's Chapter 13** (index 26, Fluorspar's Oath: Rout). The save loads
  on the world map; no armory can be reached (the only way back passes node
  15, held by a monster skirmish), so the bot goes straight to node 17. The
  map loads at 949, the first free cursor at 1,798; Gerik and Tethys are in
  the army from the start (the chapter's Talk events are conversations).
  Tana kills an archer with her Javelin on turn 1 and dies to the other
  archer in that enemy phase (the loss record at 10,640). The last enemy
  falls on turn 14: 26 → 27 at 121,266, the save at 121,330 (`i14_start`).
- **Army at Ephraim's Chapter 14** (`route_check.lua`'s last tick of
  `route_i13.keys`): Ephraim (level 8, 27 HP), Seth, Franz, Gilliam, Garcia,
  Duessel, L'Arachel, Dozla, Ewan, Marisa, Tethys and Gerik. Dead since
  `i11_start`: Cormag, Colm (Chapter 11), Tana (Chapter 13).
- **Ephraim's Chapter 14** (index 27, Father and Son: Seize (14,2)). The
  save loads on the world map; no armory can be reached (node 15 still holds
  the monsters), so the bot goes straight to the chapter. The map loads at
  957 (map 63 borrowed at 1,502 to 1,523), the first free cursor at 1,735;
  the army starts in two groups at the bottom corners of Grado Keep. Gerik
  uses his Hero Crest on turn 1 (Tactics v7: the class menu's first entry,
  Hero; his Hand Axe is then usable). Druids carry Berserk staves (a first
  try lost Ephraim to a berserked Dozla, which v7's threat now counts),
  Bishops Sleep and Silence. Rennac (red, a looting thief from turn 2) is
  spared for L'Arachel's Talk, which never happens; he does not join. Vigarde
  (a General, 52 HP, with the Spear) holds the throne, which has one tile
  next to it, (14,3): the army attacks him there and his Spear wears out (15
  uses); then the others make way and Ephraim's Reginleif (effective against
  armour) finishes him (v7, "a boss that cannot fight back"). Ewan dies in
  the turn 5 enemy phase (the loss record rewrites the save at 52,288),
  Garcia in turn 11, Dozla in turn 13, Gerik in turn 18. Ephraim seizes on
  turn 45 (27 → 70 at 180,081; the ending passes maps 70, 75 and 77), 27 →
  28 at 181,036, the save at 181,100 (`i15_start`). Knoll joins in the
  ending.
- **Scorched Sand** (index 28, Rout). The save loads on the world map: a
  node both routes share, whose Ephraim chapter is at `chapteridx_ephram`
  (+5; "World-map walk" below). The bot shops at node 5 (ticks 878 to
  5,581): Steel Lances and Steel Swords for Seth and Franz, a Steel Axe and a
  Steel Lance for Duessel, an Iron Sword and a Steel Sword for Marisa (9,785
  gold to 5,725). The map loads at 6,090, the first free cursor at 6,859;
  Eirika, Innes and Saleh join in the opening. On turn 1 Franz takes the
  Knight Crest Vigarde left with Ephraim (Trade, Tactics v8) and gives back
  the Guiding Ring, which Knoll takes; both promote on turn 2 (Franz a
  Paladin at 21,493, Knoll a Druid at 24,827). Valter (a Wyvern Knight) and
  Caellach (a Hero on the fort at (1,1)) hold their tiles. Knoll dies in the
  turn 5 enemy phase, Marisa in turn 9, Tethys in turn 12. The last enemy
  falls on turn 21: 28 → 29 at 140,616, the save at 140,680 (`i16_start`).
- **Ruled by Madness** (index 29, Seize (13,3)). The save loads on the world
  map; the bot shops (ticks 878 to 4,159: a Steel Axe for Duessel, a Steel
  Bow and an Iron Bow for Innes; 5,725 gold to 4,985). The map loads at
  4,960 (maps 64 and 66 borrowed at 5,505 to 5,560), the first free cursor
  at 5,758. Gilliam dies in the turn 10 enemy phase (the loss record at
  64,044), Saleh in turn 14. Orson (a Paladin, the boss) sits on the seize
  tile; on turn 21 Seth and Duessel (at 10 HP; Orson's counter kills him)
  bring him to 8 HP, Innes's bow kills him, and Ephraim seizes (tick
  119,423; 29 →
  66 at 119,489, the ending's maps, 29 → 30 at 122,553), the save at
  122,617 (`i17_start`). In the ending the twins promote (Master Lord and
  Master Lady) and Myrrh joins.
- **Army at Chapter 17 on Ephraim's route** (`route_check.lua`'s last tick
  of `route_i16.keys`): Ephraim (Master Lord, level 1, 35 HP), Seth (level
  17), Franz (Paladin, level 4), L'Arachel (level 5), Eirika (Master Lady,
  level 1, 26 HP), Innes (Sniper, level 6) and Myrrh (level 3). Dead since
  `i14_start`: Ewan, Garcia, Dozla, Gerik (Chapter 14), Knoll, Marisa,
  Tethys (Scorched Sand), Gilliam, Saleh, Duessel (Ruled by Madness).
- **River of Regrets** (index 30, Defeat Boss). The save loads on the world
  map; the bot shops at node 5 (ticks 878 to 4,692): Seth sells a staff and a
  light tome he cannot use and buys a Steel Lance and a Steel Sword, Franz
  sells a Mend staff and buys a Steel Lance (4,985 gold to 8,925, the sales
  being worth more). The map loads at 5,473, the first free cursor at 6,263.
  L'Arachel dies in the turn 5 enemy phase (the loss record rewrites the save
  at 43,010). Syrene (green, a Talk recruit for Innes) is never talked to;
  she and her four civilians die. The boss, Lyon (a Necromancer, 44 HP,
  Fenrir and Nosferatu), holds the gate (20,6), which only (20,7) touches,
  with `GuardTileAI`. On turn 16 Franz kills the last other enemy (tick
  122,201); then Tactics v9's siege ("Tactics v9") sends the healthy units at
  Lyon: Innes's bow from (18,6) (123,189) and Myrrh from (20,7) (124,384) bring
  him down: 30 → 31 at 127,293, the save at 127,355 (`i18_start`). The ending gives Ephraim two sacred weapons (items 0x94
  and 0x8E).
- **Two Faces of Evil** (index 31, Rout; monsters and Gorgon eggs). The save
  loads on the world map; the bot shops at node 21 (ticks 878 to 4,233): a
  Javelin for Seth, a Killer Lance and a Javelin for Franz (who sells an axe
  he cannot use), a Steel Bow for Innes (8,925 gold to 6,655). The map loads
  at 4,361, the first free cursor at 5,125. Myrrh's Dragonstone and Eirika's
  Sieglinde are effective against monsters; the last enemy falls on turn 10
  with no loss: 31 → 32 at 42,127, the save at 42,191 (`i19_start`).
- **Last Hope** (index 32: hold out to turn 14 or defeat Riev; Mansel, green
  on the throne (16,10), must live: his defeat quote is a game over). The save
  loads on the world map; the bot shops at node 21 (ticks 878 to 2,345: a
  Killer Lance for Ephraim; 6,655 gold to 5,455). The map loads at 2,577, the
  first free cursor at 3,426; Innes and Myrrh start alone in the north-west
  room. On turn 1 Seth rescues Mansel (tick 3,449; Tactics v9) and carries him
  to the end, out of the fighting; the others fight on (not in hold-out mode:
  every hold-out try lost Mansel or a lord by turn 12, "Tactics v9"). Innes
  dies in the turn 5 enemy phase (the loss record at 45,490), Myrrh in turn
  13's. Turn 14's event wins: 32 → 72 → 32 at 140,421 to 140,434 (the
  ending's map), 32 → 33 at 141,435, the save at 141,499 (`i20_start`).
- **Army at Chapter 20 on Ephraim's route** (`route_check.lua`'s last tick
  of `route_i19.keys`): Ephraim (Master Lord, level 2, 36 HP), Seth (level
  20), Franz (Paladin, level 14) and Eirika (Master Lady, level 1, 26 HP).
  Dead since `i17_start`: L'Arachel (River of Regrets), Innes and Myrrh
  (Last Hope).
- **Darkling Woods** (index 33, Seize (11,11); Tactics v10). The save loads
  on the world map at node 23; the bot shops there (ticks 878 to 6,511): at
  the armory a Silver Lance and a Silver Sword for Franz, then at the vendor
  an Elixir for each unit (Ephraim's, his hands full, goes to the supply:
  "Send it to storage?"; Eirika sells the Orion's Bolt for the room), 15,455
  gold to 5,755. The map loads at 6,684, the first free cursor at 7,458. On
  turn 1 the units take the sacred weapons from the supply (Tactics v10):
  Ephraim gives Audhulma and Nidhogg (he cannot use them) and takes Siegmund
  and the Elixir, Seth gives the Red Gem and takes Audhulma, Franz takes
  Vidofnir; on turn 2 Ephraim uses his Energy Ring. Morva (a Manakete, 62 HP,
  Wretched Air: def 24 and 36 a hit through any defence) holds the gate
  (11,11), which only (11,12) touches (the floor tiles round it are walls);
  Riev (a Bishop, `CA_BOSS`) falls before turn 33. On turn 37 Franz waits on
  (11,12) with Vidofnir equipped (the bait): Morva attacks him in the enemy
  phase and the counter takes him to 16 HP. On turn 38 Franz heals and makes
  way, Eirika's Sieglinde kills Morva from (11,12) (the strike, 162,542) and
  Ephraim, moving last, seizes (163,599): 33 → 21 → 33 at 163,673 to 163,690
  (the ending borrows map 21), 33 → 34 at 163,696, the save at 163,760
  (`i21_start`; the final chapter's map loads at 163,816, with no world map
  between). No loss.
- **Army at the final chapter** (`route_check.lua`'s last tick of
  `route_i20.keys`): Ephraim (Master Lord, level 4, 38 HP), Seth (level 20,
  48 HP), Franz (Paladin, level 17, 54 HP) and Eirika (Master Lady, level 2,
  27 HP).
- **The final chapter, part 1** (`route_i21.keys`, index 34, Sacred Stone:
  Defeat Boss, Lyon on (11,2), 73 HP, def 26, res 29, Naglfar; Tactics I11,
  `ROUTE_SEARCH=1`, coverage 11). Won with the real army on turn 29, no
  loss. The save loads on the map (tick 708), the first free cursor at
  1,477. On turn 1 Seth takes an Elixir from the supply. The army fights
  north by the west passage (4,13)-(5,16) (Ephraim's Siegmund kills the
  Draco Zombie that holds it), never stops on the corridor below the throne
  room or in the east middle, so neither the Wights by Lyon nor the Arch
  Mogalls load, and goes round by the west room and the stairs (5,1)-(5,2)
  into the throne room: 30 of the 36 enemies are down by turn 14 (the
  search: 12 policies a turn at most, 172 played in all). Turns 15 to 20
  stand still before the throne room's last guards until the stalled turn's
  "press"; from turn 21 Seth waits next to Lyon at (11,1) with Audhulma (the
  bait: Lyon's Naglfar does 33 to him, his counter 20) and heals there with
  his Elixir after each hit (the bait's heal); on turn 29 Lyon has 17 HP and
  Seth's Audhulma strikes from (10,2): 34 → 35 at 80,151 (`MNC3`: straight to
  part 2, no save, no preparations), part 2's map at 80,154, its turn 1 at
  80,216 (the turn-1 suspend save), its first free cursor at 80,292, where the
  route stops. The game slot still holds `i21_start`'s save (index 34); the
  suspend holds part 2's turn 1 (`i22_start`). Army at part 2: Ephraim
  (level 5, 39 HP), Eirika (level 3, 28), Seth (48), Franz (level 18, 54);
  Audhulma 2 uses left, Elixirs 2 uses (Franz), Vulneraries 5, the supply
  without heal items. Part 2 is not won with this army ("Boosted starts",
  "Ephraim's boosted part 2").

The thirteen end saves load the same on both sides (`make chapsave`,
`resume.keys`, 2,600 ticks, release build, NULL probe on): `i9_start` to
`i13_start` LAG-ONLY, `i14_start` MATCH, `i15_start` to `i21_start`
LAG-ONLY, no probe site. The bot at this commit regenerates all twenty-eight
route files byte for byte, and `route_check.lua` (`ROUTE_TICK=capture`)
re-derives each new header's milestones (20, 16, 13, 15, 10, 10, 17, 10, 20,
10, 9, 12 and 12 lines).

**PC check** (2026-10-21, branch `port-bots10i`; as the chain above: the
whole route through the lag replay with `FE8PC_NULLPROBE=1`, compared without
the raw dumps (0.64 to 1.02 GB peak), frame15 every 60 ticks, then every frame
of the four busiest 600-tick windows, the shopping, the load, the chapter end
and around each 60-tick frame that differed; `port-plan/pending/c10-i1.md`).
The frame counts now take round 8's raster-timing labels (P-17), which these
runs carry:

| Route | PC ticks | Lag replay | Traced fields that differ | Frames compared / differ | NULL probe sites |
|---|---|---|---|---|---|
| `route_ch8i` | 87,115 of 87,115 with the probe | 4,287 lag VBlanks in 1,985 ticks (5 early flush), all at the GBA's positions | `gOam` 24 ticks (4 staff selections, the subtitle text); `gBG3TilemapBuffer` 6 ticks (86,744-86,749, the same 6 cells as `route_ch8`); `gPlaySt` 365 ticks (86,750 to the end: the chapter ranks) | 6,668 / 89: 86 labelled raster-timing, 3 world-map name box | 6: 3 H-1 drops, 3 the Vulnerary |
| `route_i9` | 117,128 of 117,128 with the probe | 6,948 in 3,035 ticks, all at the GBA's positions | `gOam` 71 ticks (9 staff selections) | 12,534 / 185: 160 labelled, 24 subtitle text and staff animation's first frame, 1 darker map (`slots`) | 6: 3 H-1, 3 the Vulnerary |
| `route_i10` | 71,983 of 71,983 with the probe | 4,092 in 1,837 ticks, all at the GBA's positions | none: MATCH | 7,521 / 81, all labelled raster-timing | 5: 2 H-1, 3 the Vulnerary |

Every difference is a known kind (`port-plan/pending/cov9-findings.md`
groups G1, G5, G6, G7 and the busy-menu kind); nothing new. Without the
probe (the release lag replay resumed from each chapter-start snapshot,
`~/fe8-snapshots/route_ch8i/tick1587`, `route_i9/tick7645`,
`route_i10/tick4584`) every tick matches up to the first Vulnerary, where the
PC stops with SIGSEGV (EIP 0x10023D2D, address 4: G1): tick 9,938 of
`route_ch8i`, 36,469 of `route_i9`, 61,261 of `route_i10`.

**PC check of Ephraim's Chapters 11 to 13** (2026-10-21, step I2,
`port-plan/pending/c10-i2.md`; the same two passes):

| Route | PC ticks | Lag replay | Traced fields that differ | Frames compared / differ | NULL probe sites |
|---|---|---|---|---|---|
| `route_i11` | 80,653 of 80,653 with the probe | 3,857 lag VBlanks in 1,849 ticks (1 early flush), all at the GBA's positions | `gBG2TilemapBuffer` 67 ticks (2,702-2,768: **new**, the world-map route row below the map, see below); `gOam` 6 ticks (1 staff selection) | 6,947 / 57, all labelled raster-timing | 7: 3 H-1, 1 `AiFindTargetInReachByCharId` (round 9's out-of-map reads), 3 the Vulnerary |
| `route_i12` | 76,501 of 76,501 with the probe | 4,058 in 1,985 ticks, all at the GBA's positions | `gOam` 19 ticks (2 staff selections) | 6,156 / 26, all labelled | 6: 3 H-1, 3 the Vulnerary |
| `route_i13` | 121,631 of 121,631 with the probe | 6,036 in 2,859 ticks, all at the GBA's positions | `gOam` 25 ticks (3 staff selections) | 6,510 / 85, all labelled | 5: 2 H-1, 3 the Vulnerary |

The one new difference: at tick 2,702 of `route_i11` the bot's L moves the
world-map cursor from node 15 down to node 16 and the camera scrolls to the
map's bottom edge. `MapRoute_BlitScrollEdge` (`src/worldmap_path.c:607-618`)
then copies route-layer row 40 of `gUnknown_02019D00` (60 entries a row) into
`gBG2TilemapBuffer` row 8, one row past the 40 the layer has, so it copies
what follows in RAM: `gFont_0201AFC0`'s `draw_glyph` and `get_draw_dest`
pointers (cells (4,8) to (7,8): GBA 0x08004219 `DrawTextGlyph` and
0x080041E9 `GetTextDrawDest`, PC 0x100F9B94 and 0x100F9B5C, the same two
functions). The row is below the screen (from 600 to 3,600, compared every
frame, no frame differs but the labelled raster-timing ones) and the
chapter's map load clears it at 2,769.
Snapshot `~/fe8-snapshots/c10-i2-wmrow/tick2690` (resumes on both sides and
shows it at 2,702 in 4 seconds). The three chapter-start snapshots
(`~/fe8-snapshots/route_i11/tick3543`, `route_i12/tick1486`,
`route_i13/tick1798`) resume on both sides without the probe: MATCH for
3,000 ticks each.

**PC check of Ephraim's Chapters 14 to 16** (2026-10-21, step I3,
`port-plan/pending/c10-i3.md`; the same two passes; pass 2 stops before the
tick where the PC leaves the GBA's path):

| Route | PC ticks | Lag replay | Traced fields that differ | Frames compared / differ | NULL probe sites |
|---|---|---|---|---|---|
| `route_i14` | 181,401 of 181,401 with the probe, but in one battle from 76,095 to the end (**new**, see below) | 10,625 lag VBlanks; at the GBA's positions up to 76,094, off from 76,095 | up to 76,094: `gOam` 36 ticks (6 staff selections); from 76,096 every field | pass 2 (0 to 76,094): 5,443 / 54: 46 labelled raster-timing, 7 the staff subtitle, 1 darker frame (`slots`) | 4: H-1 only |
| `route_i15` | 140,981 of 140,981 with the probe | 6,863 in 3,371 ticks, all at the GBA's positions | `gBG2TilemapBuffer` 280 ticks (5,747-6,026: the world-map route row of `route_i11` again) | 11,033 / 133: 129 labelled, 3 world-map name box, 1 darker frame | 7: 4 H-1, 3 the Vulnerary |
| `route_i16` | 122,918 of 122,918 with the probe | 6,689 in 3,251 ticks (3 early flush), all at the GBA's positions | `gBG2TilemapBuffer` 169 ticks (4,325-4,493: the world-map row again); `gBG3TilemapBuffer` 7 ticks (122,540-122,546: seven cells at a scene's map change, black screen) | 11,381 / 155: 151 labelled, 3 name box, 1 darker frame | 6: 3 H-1, 3 the Vulnerary |

The new difference is a hang. On turn 9 of Ephraim's Chapter 14, Seth
attacks a Bishop (Shine and a Silence staff) from (20,12), and the Bishop
counters with Shine. On the GBA, `ProcScr_efxShine` and the spell-cast
flash start at tick 76,095 (the casting animation's C05 command,
`StartSpellAnimation`, `gEkrSpellAnimLut[51]`), and the battle ends at
76,588. On the PC no proc starts (main-loop events stay 21, the GBA's 25),
the casting glow cycles on, and `gMainCallback` stays in the battle until
the route's last tick: the spell never starts and the game never leaves the
battle. Snapshot `~/fe8-snapshots/c10-i3-i14bat/tick75800` (resumes on both
sides with the probe and shows it at 76,095 in 34 seconds). The world-map row
(cells (4,8) to (7,8), the font's two function pointers) comes back before
Scorched Sand and Ruled by Madness; the seven BG3 cells at the end of
`route_i16` (GBA 727C 727D 828A 80F3 8055 8011 A1CE, PC 807E 807F 809E 809F
80BE 80BF A0B6, map 16 of the ending, screen black) look like the map-change
kind of `route_ch8` (cov9 G5 (b)) with other values. The three
chapter-start snapshots (`~/fe8-snapshots/route_i14/tick1735`,
`route_i15/tick6859`, `route_i16/tick5758`) resume on both sides without the
probe: MATCH for 3,000 ticks each.

**PC check of Ephraim's Chapters 17 to 19** (2026-10-22, step I4,
`port-plan/pending/c10-i4.md`; the same two passes; pass 2 stops before the
tick where the PC stops):

| Route | PC ticks | Lag replay | Traced fields that differ | Frames compared / differ | NULL probe sites |
|---|---|---|---|---|---|
| `route_i17` | 127,656 of 127,656 with the probe | 6,578 lag VBlanks in 3,389 ticks, all at the GBA's positions | `gBG2TilemapBuffer` 231 ticks (4,858-5,088: the world-map route row again) | 9,955 / 89: 83 labelled raster-timing, 5 world-map name box, 1 darker frame (`slots`) | 22: 3 H-1, 14 the wall/snag deaths at 10,066 (round 9's NULL-unit group), 5 the Vulnerary |
| `route_i18` | 38,559 of 42,492: **the PC stops at 38,559 even with the probe** (new, below) | 1,853 in 962 ticks up to there, all at the GBA's positions | `gLCDControlBuffer` 30 ticks (19,536-19,551 and 28,455-28,470: the critical-hit shake's x/y swap, round 9's argument-order group) | 6,117 / 81, all labelled | 32: 10 the Gorgon egg's move costs at 6,375 (**new**, below), 13 the fire traps at 6,395 (the monkey's MB2), 5 H-1, 3 the Vulnerary, 1 the Stone animation at 38,559 (**new**, below) |
| `route_i19` | 141,800 of 141,800 with the probe | 8,247 in 3,818 ticks, all at the GBA's positions | none: every traced field equal | 8,110 / 75: 73 labelled, 2 darker frames (`slots`, the ending's "Sent ... to the supply" boxes) | 8: 2 H-1, 1 `IsUnitSlotAvailable` at 68,694 (65 reads: the scout's N2, here in story play), 5 the Vulnerary |

Two new crashes, both on Two Faces of Evil's monster map:

- **The Gorgon egg's AI** (tick 6,375, the first enemy phase). A Gorgon
  egg's class has no move-cost tables (`pMovCostTable` all 0,
  `src/data_classes.c`), and when its AI builds a movement map
  (`GenerateExtendedMovementMapOnRange`, `src/bmidoten.c:50`)
  `SetWorkingMoveCosts` reads 65 costs at address 0, then `MapFloodCore`
  floods with them. The GBA reads the BIOS latch there; the plain PC stops
  with SIGSEGV (EIP 0x1003E880, `SetWorkingMoveCosts`, address 0; resumed
  from `~/fe8-snapshots/c10-i4-egg/tick6370` in 3 seconds). With the probe
  every traced field stays equal.
- **The Gorgon's Stone as a map animation** (tick 38,559: a Gorgon's Stone
  against Myrrh from three tiles away, a map battle). `MapAnimMonsterStone_Loop1`
  (`src/mapanim_staffeffect.c:1086-1112`) calls `Proc_Break` at frame 10 but
  goes on to `Decompress(TsaSet_MonsterStoneMapAnimfx[10], ...)`, one past
  the 10-entry table: the next ROM word, `0x0001000E` (the following
  `ProcCmd`), on both sides. `Decompress` (`src/bmlib.c:139`) indexes its
  8-entry `func_lut` with that address's type byte. The GBA reads open bus
  `0x08` there and calls `UnpackRaw` (measured with a breakpoint at
  `0x08012F76`); the PC, even with the probe (its stand-in byte `0xA0`),
  takes entry 21, past the table, and jumps to `0x02032F8C`
  (`sBmBaseTilesPool`): SIGSEGV. GBA-only snapshot
  `~/fe8-snapshots/c10-i4-stone/tick38550` (the plain PC stops at the egg
  first, and probe runs cannot be snapshotted); the whole route with the
  probe shows it in 2 minutes.

Known kinds seen again: the fire-trap targets (`PidStatsRecordTargetListDefeats`,
the monkey shard's MB2, here in the story chapter at its first trap turn);
`IsUnitSlotAvailable` with all 50 red slots full (the scout's N2, here when
Last Hope's turn 8 reinforcements load; plain PC SIGSEGV at 68,694, EIP
0x1005D30A, from `~/fe8-snapshots/c10-i4-n2/tick68690`); the world-map row
and name box; the critical-hit shake; the darker frames; the Vulnerary; H-1.
The three chapter-start snapshots (`~/fe8-snapshots/route_i17/tick6263`,
`route_i18/tick5125`, `route_i19/tick3426`) resume on both sides without
the probe: `route_i17` and `route_i19` MATCH for 3,000 ticks, `route_i18`
MATCH up to the egg's crash at 6,375.

**PC check of Darkling Woods** (2026-10-22, step I5,
`port-plan/pending/c10-i5.md`; the same two passes):

| Route | PC ticks | Lag replay | Traced fields that differ | Frames compared / differ | NULL probe sites |
|---|---|---|---|---|---|
| `route_i20` | 164,061 of 164,061 with the probe | 9,813 lag VBlanks in 4,861 ticks (3 early flush), all at the GBA's positions | none: every traced field equal (MATCH on 0-164,060) | 13,715 / 202, all labelled raster-timing | 8: 5 H-1, 3 the item use's battle graphics (`PrepareBattleGraphicsMaybe` at 32,157, Franz's Elixir: round 9's heal-animation group) |

Nothing new: the supply screen, the vendor, the Equip menu, Morva's Wretched
Air, the Gorgons' Stone and Demon Surge and the sacred weapons' battles all
follow the GBA. The chapter-start snapshot (`~/fe8-snapshots/route_i20/tick7458`)
resumes on both sides without the probe: MATCH for 3,000 ticks.

**PC check of the final chapter, the ending and the post-game** (2026-10-22,
coverage 11, `port-plan/pending/c11-i.md`; the same two passes; pass 2 stops
before the tick where the PC leaves the GBA's path):

| Route | PC ticks | Lag replay | Traced fields that differ | Frames compared / differ | NULL probe sites |
|---|---|---|---|---|---|
| `route_i21` | 80,293 of 80,293 with the probe | 4,855 lag VBlanks (1 early flush), all at the GBA's positions | none: every traced field equal on every tick | 5,629 / 122: 108 labelled raster-timing, 14 in Lyon's Naglfar spell animation on GBA lag ticks (2 or 3 VBlanks in the tick: 62,520, 72,094-72,150, 79,357-79,413) | 4: H-1 only (`AnimDelete`: battle intro, hit quake, Stone, Demon Surge) |
| `route_ib22` (boosted) | the PC leaves the GBA at 1,405: **the Demon King's summon** (C10-6, here on Ephraim's map) | at the GBA's positions up to 1,404 | none on 0-1,404 (MATCH); from 1,405 the main-loop events, `gLCDControlBuffer`, `gBG3TilemapBuffer`, `gOam`, from 1,410 `gRNSeeds`, then everything | pass 2 (0-1,404) 1,404 / 27, all labelled | none |
| `route_ibend` (boosted) | 37,611 of 37,611 with the probe | 152 lag VBlanks in 48 ticks, all at the GBA's positions | none: MATCH on 0-37,610 (the whole ending, the credits, the Fin screen) | pass 1: 627 / 0; pass 2: 4,276 / 0 |
| `route_ibpost` (boosted) | 81,060 of 81,060 with the probe | 3,804 lag VBlanks in 1,877 ticks, all at the GBA's positions | none: MATCH on 0-81,059 | pass 2: 6,048 / 113, all labelled raster-timing | H-1 only (`AnimDelete` from the battle intro) |

The part-1 frames (sheet `i21_frames.png`, a local scratch file):
in Lyon's Naglfar animation the GBA's frame shows a soft purple glow where
the PC's shows the spell's ring pattern, on ticks with 2 or 3 GBA VBlanks;
the next frames are equal and every traced field stays equal, so this looks
like the known "frame on a GBA lag tick" kind (not labelled by the raster
replay), in a spell animation the PC had not shown before. The ending's
probe sites are C10-8's: without the probe the PC's lag replay stops there
(SIGSEGV at 19,549, EIP 0x10081E61, `GetFaceDisplayBitsById`, `src/face.c:471`,
address 0x30). Snapshots: `~/fe8-snapshots/route_i21/tick1477`,
`route_ib22/tick805` and `tick1390` (15 ticks before the summon),
`route_ibend/tick716` and `tick19540` (9 before the face read),
`route_ibpost/tick2362` (all four parts each). The new saves load the same
on both sides (`make chapsave`, `resume.keys`, 2,600 ticks, release, probe
on): `i22_start`, `ib22_start`, `ib_clear` and `ib_r2_start` LAG-ONLY, no
probe site, end SRAM 8, 8, 8 and 9 bytes (P-12). 7 from 19,549: `GetFaceDisplayBitsById` (`src/face.c:471`) reads a face that is not loaded in the ending's CG text scene "Lyon: Oh, are you..." (the same scene and sites as Eirika's ending: C10-8, so both endings), then `SetFaceBlinkControl` and `FaceBlinkProc_GenBlinkInterval` through it, again from `CgText_InitFadeOut` (21,338) and `CgText_OnEnd` (21,355) |

## Watching the scenes (coverage 12)

Every route above skips each skippable scene with Start (and a talk with B). `ROUTE_WATCH=fast` or
`slow` (`route_bot.lua`, "Coverage 12") makes the bot watch them instead: no Start or B in a scene,
the opening and each chapter's title card left to run, and A at a human's pace, fast (A every 4 to 12
ticks, sometimes held) or slow (never while a talk prints, A 20 to 80 ticks after it waits). Unset,
nothing changes (`route_ch5x.keys` made again byte for byte). The files for Eirika's route, from power-on
to the ending, are in `inputs/watch/` with their own README and table; `route_check.lua`'s
`ROUTE_SCENES_OUT` lists the ticks of each scene kind (`route_lib.lua`'s `L.sceneKind`), battle and menu,
and `make -f port/test/Makefile watch-route WATCH=NAME` compares every frame of them.
`ROUTE_PREFIX=FILE` (with `ROUTE_PREFIX_TICKS=N`, default all of it) plays FILE's first N ticks open-loop
before the bot takes over, as `MONKEY_PREFIX` does for the menu monkey; the key file written holds both.
Unset, nothing changes (`wf_ch5x.keys` made again byte for byte). It makes the tail of a chapter whose
watched play the bot cannot win (`inputs/watch/README.md`, Chapter 10).

## How the key files were made

`port/test/oracle/route_bot.lua` plays the game in mGBA from power-on. It reads:

- the proc array;
- `gPlaySt` and `gBmSt`;
- the unit arrays;
- `gBmMapMovement`, `gBmMapUnit` and `gBmMapTerrain`;
- the world map data.

From these it chooses each tick's keys and writes them out as a key file. Its
tick and key timing are the same as `gate0_capture.lua`. mGBA is deterministic
from reset, so replaying the file open-loop gives the same run. You do not need
the bot to use the routes.

```
ROUTE_OUT=port/test/inputs/route_ch2.keys ROUTE_STOP=pp:3 \
  build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/route_bot.lua 3000000
```

Settings:

- `ROUTE_STOP` sets where the bot stops:
  - `pp:N`: chapter index N's first free cursor.
  - `save:N`: 300 ticks after the game save that ends chapter index N (the
    first game save written with a higher `chapterIndex`).
  - `saveto:K`: 300 ticks after the first game save written while
    `chapterIndex` is K. Chapter 8 needs it: its ending writes a game save
    during the Castle Frelia scene (index 56, tick 85,873 of `route_ch8.keys`)
    before the route split, so `save:9` would stop there; the save that
    starts Eirika's Chapter 9 is the one written with index 10 (86,776).
  - `tick:T`: tick T.
- `ROUTE_DIFF` picks the difficulty entry (default 1, Normal).
- `ROUTE_VERBOSE=1` logs each unit order and the units lost since the last
  player phase; `ROUTE_VERBOSE=2` also logs each change of input handler.
- `ROUTE_MAX_TURN` (default 60): stop, writing the keys so far, when a
  chapter's turn passes this, so a stalled try leaves a file to look at.
- `ROUTE_SPLIT=ephraim`: at the route split after Chapter 8 the bot takes the
  menu's second entry, Ephraim (`MenuDef_RouteSplit`: Down, A, then the
  event's Yes). Unset (every file but `route_ch8i.keys`), the A presses take
  the first entry, Eirika. The `# Generated` header line names it when set.
- In tactics v3 the bot also stops when a lord dies ("game over: lord ...
  died"). Without that it would loop for ever: the game-over screen goes back
  to the title, the bot's A presses take Resume Chapter, and the suspend save
  replays the same enemy phase with the same RNG, so the same death.

The files were made with `pp:1`, `pp:2`, `pp:3`, `pp:4`, `pp:6` and `pp:5`, in
table order. The routes from chapter saves were made with `GATE0_SRAM_IN` set
to their start save and `ROUTE_STOP=save:5`, `save:7`, `save:8`, then
`saveto:10`, `saveto:11` and `saveto:61`, then (with `ROUTE_TICK=capture`)
`saveto:12`, `saveto:13`, `saveto:14` and `saveto:15`, then (with
`ROUTE_TICK=capture ROUTE_SEARCH=1`, "Tactics v6") `saveto:16` to `saveto:19`; with `GATE0_SRAM_IN` set, the bot
writes the `# Start:` header line. Ephraim's thirteen were made with
`ROUTE_TICK=capture` and `saveto:23` (with `ROUTE_SPLIT=ephraim`),
`saveto:24`, `saveto:62`, `saveto:25` and on to `saveto:34`.
On port-bots10e the bot regenerated its twenty-six files byte for byte
(2026-10-22, SHA-1 b3f3dc85: `route_ch5.keys` from power-on, `route_ch0` to
`route_ch4` its prefixes, `route_ch5x` to `route_e19` and the five boosted
routes, with the settings in their `# Generated` lines, 4,005 s, two lanes),
and on port-bots10i its twenty-eight (SHA-1 c1c25a15). The merged bot
of port-cover10 regenerates every one of them; "One bot, two chains" below
has the check. `route_check.lua` (with `ROUTE_TICK=capture`) re-derives the
headers' milestones of the files made with that rule.

```
GATE0_SRAM_IN=port/test/saves/ch6_start.sav ROUTE_OUT=port/test/inputs/route_ch6.keys ROUTE_STOP=save:7 \
  build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/route_bot.lua 3000000

# the routes from route_e11.keys on: the capture's tick rule, which needs its breakpoints
# (MGBA_RUN_BP = GBA_BP of port/test/Makefile, as the files were made)
GATE0_SRAM_IN=port/test/saves/e13_start.sav ROUTE_OUT=port/test/inputs/route_e13.keys ROUTE_STOP=saveto:14 \
  ROUTE_TICK=capture MGBA_RUN_BP=$(make -s -f port/test/Makefile -pn .x 2>/dev/null | sed -n 's/^GBA_BP := //p') \
  build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/route_bot.lua 3000000
```

(About 2.5 minutes a file with the breakpoints, against about 45 seconds
with the frame rule.)

The tactics are simple:

- The planner takes Seth first and the lord (Eirika, or Ephraim) last.
- A unit attacks the best target it can reach.
  - The score favors kills and the boss.
  - It assumes every hit lands, with no crits, terrain or effectiveness.
  - A unit never attacks when the counter could kill it.
- A unit that cannot attack moves toward the nearest enemy, using its class
  movement costs.
- Only the lord may stop on the seize tile.
- The lord only attacks when the estimated enemy-phase damage on its tile is
  below its HP. Otherwise it stays next to Seth, and after the boss dies it
  heads for the seize tile.
- The chapter data the bot uses (seize tiles, bosses) is in `route_bot.lua`
  under "Chapter knowledge".

**Tactics v2** (2026-10-20). The six files above were made with the rules
above (v1), and the bot keeps v1 for their chapters (`chapterIndex` 0 to 4
and 6), so it still makes them byte for byte. Every other chapter (5x, then 6
on) uses v2:

- **Real enemy reach.** The enemy-phase threat on a tile counts an enemy only
  if it can really attack that tile: a search over the map with the enemy's
  class move costs and its move, with blue and green units as walls (it may
  pass red units but not stop on them), then the ranges of every weapon it
  carries. Its AI is read too: AI1 3 to 5 ("action standing") attack only
  from their own tile, and AI1 6 does nothing. v1 counted every enemy within
  move plus range in a straight line, through walls; that is why Chapter 5x
  stalled.
- **Bosses** are the red units whose character or class has `CA_BOSS`
  (Zonta in 5x, the bosses of Chapters 6 and 7), not a list of names.
- **Goals held by an enemy** (the boss on the seize tile) still lead the way:
  v1's distance search treated that tile as a wall, so the lord's distance to
  the seize tile was "unreachable" everywhere and it stayed in the safest
  corner.
- **The lord** rules out only tiles whose threat could kill it, and otherwise
  weighs threat and progress about equally (`ROUTE_LORD_TH`, `ROUTE_LORD_DD`,
  default 5 and 3). The 5x route is the same with 2:4, 3:3, 4:6, 5:3 and
  8:2; 1:5 clears it one turn sooner. The files use the defaults.
- **Full inventory.** When a unit with five items gets one more (an enemy's
  drop), the bot sends the new item to the supply, so the unit keeps its
  weapons (`gProcCmd_ConvoyMenu`, in either phase).
- **Seize tile** of Chapter 7 (index 8): (9,4).

**Tactics v3** (2026-10-21). The three v2 files (Chapters 5x, 6 and 7) must
regenerate byte for byte too, so v2 stays for `chapterIndex` 5, 7 and 8, and
Chapter 8 (index 9) and every later chapter use v3: v2 plus the rules below.
The bot at this commit still regenerates all nine older files byte for byte
(checked 2026-10-21).

- **Weapons by the game's rules.** A unit's weapon is the first item it can
  use (`CanUnitUseWeapon`: the weapon rank and the item locks), as the game
  equips it. v1 and v2 took the first weapon in the inventory: in Chapter 8,
  after Seth's Iron Blade broke, they planned attacks with his Iron Axe, which
  a Paladin cannot use, and the menu offered no Attack.
- **Damage**: effectiveness (might and triangle bonus x3, as
  `ComputeBattleUnitAttack`; the Rapier against knights, bows against fliers)
  and the defender's terrain bonus (defence, or resistance against magic).
- **Support moves**, tried before attacks:
  - a unit with Heal, Mend, Recover or Physic heals the most hurt ally (7 HP
    or more missing) it can stand next to, where the threat is below its HP.
    Healers act after the fighters;
  - a unit under half HP with a Vulnerary or Elixir moves to its safest tile
    and uses it (Item, the item, Use);
  - a unit left with no usable weapon takes one from an ally next to a tile in
    reach, by Trade, when the ally cannot use it or has another (in Chapter 8
    Seth took Colm's Iron Lance);
  - a unit with a Door Key (or a thief's Lockpick) opens a closed door that
    leads toward the enemy (the distance to them, doors counted as open, drops
    through it). Doors count as open in everyone's distances when someone in
    the army can open them, and units without a key keep off the tiles next
    to a closed door. Chapter 8's way into the castle is the door at (20,7);
    Eirika's Chapter 10 has the doors at (10,9) and (11,9);
  - recruits by Talk (the chapters' character events that make a unit join):
    Eirika's Chapter 9, Amelia (by Franz or Eirika); Chapter 10, Innes (by
    Eirika or Tana), then Gerik (by Innes or Tethys), Tethys (by Innes or
    Gerik) and Marisa (by Gerik). A talker walks to its recruit; a red recruit
    is not attacked while one of its talkers is in the army, until turn 15.
- **Keeping back**: a hurt unit (under 40% HP) heads for the healers, a unit
  with no usable weapon for the fighters, both safety first; a hurt unit
  attacks only where the threat cannot kill it.
- **The lords**: a lord's death ends the game, so a lord rules out tiles whose
  threat reaches half its HP, and attacks only under that margin. Only the
  unit `CanUnitSeize` allows (Eirika until the route split; Ephraim on his
  route and in Chapter 5x) goes to the seize tile; in Chapter 8 Ephraim stays
  back like a lord.
- **Seize tiles**: Chapter 8 (index 9) (10,2); Eirika's Chapter 10 (index 11)
  (15,1). Eirika's Chapter 9 is Defeat All.
- **The route split** after Chapter 8 (`EventScr_SplitMenu`,
  `CallRouteSplitMenu`, `src/events/ch8-eventscript.h`) needs no rule: the
  bot's A presses take the menu's first entry, Eirika, and Yes to confirm
  (`chapterModeIndex` 2 from tick 86,633 of `route_ch8.keys`).

**Tactics v4** (2026-10-21). The twelve older files must regenerate byte for
byte, so v4 is only for Eirika's Chapter 11 (index 61) and every chapter from
index 12: v3 plus the rules below. The bot at this commit regenerates all
fifteen files byte for byte (checked 2026-10-21).

- **World-map shopping.** The army v3 brings to Eirika's Chapter 11 has worn
  its weapons out (Franz has none, Gilliam an 8-use Iron Lance, Seth a 17-use
  Killer Edge), and a first Chapter 12 try with that army lost Eirika on
  turn 12 with half the army unarmed. At the first free world-map cursor of a
  run that starts from a chapter save, before the run writes any game save
  (so a route's last 300 ticks, after its own end save, stay as they were),
  the bot:
  - plans: every unit that has a weapon rank (not only staves) and whose
    usable weapons have fewer than 40 uses in all gets the best weapon it can
    use from the armory (might, less 6 points per point of weight over its
    constitution), two when it has fewer than 15 uses; it sells weapons and
    staves it cannot use when it needs the room; it keeps 1,500 gold;
  - walks the lord to the nearest cleared node with an armory and no skirmish
    on it (L to the node, A), opens the node menu (A on the lord's node) and
    takes Enter Armory (`gMenu_WMNodeMenu`, `src/worldmap_path.c`);
  - on the item screen's unit grid (`sub_8099120`, `src/prep_itemscreen.c`)
    picks each unit with work left (columns first, then rows) and in the shop
    (`gProcScr_Shop`, `src/bmshop.c`) answers the Buy/Sell choice
    (`TalkChoice_OnIdle`, `selectedChoice` +0x2A), moves the hand to the item
    (`head_loc` +0x5C) and takes the price's Yes; B leaves the shop, then B on
    the grid when every unit is done;
  - then goes on to the chapter's node as before.
  Before Chapter 11 (`route_e11.keys`, node 9, Port Kiris, ticks 878 to
  8,262): Seth sells an Iron Axe and buys a Steel Lance; Franz a Steel Lance
  and a Steel Sword; Gilliam a Steel Lance and an Iron Lance; Innes sells a
  Torch staff and buys a Steel Bow and an Iron Bow; Gerik an Iron Blade and a
  Steel Sword; Marisa a Steel Sword (15,000 gold to 9,795). Before Chapter 13
  (`route_e13.keys`, node 9 again): Franz a Steel Lance, Colm a Steel Sword,
  Dozla a Steel Axe (to 8,355). Chapter 11 then took 13 turns and lost nobody
  (29 turns without the shopping, in a trial).
- **Hold-out mode** for a chapter won by holding out until a turn (a
  `TurnEventPlayer` with `EVFLAG_WIN`; Eirika's Chapter 13, index 13, turn
  12, or the boss): the lord moves first each turn, to the tile with the
  lowest score: tiles whose threat reaches half its HP are ruled out first
  (as in v3), then threat x 20, plus 2 for each tile each enemy within 15
  tiles is short of 15, minus 5 for each ally within 2 tiles. The others
  stand by the lord instead of walking to the enemy, and attack only where
  the threat cannot kill them. Without it Eirika (still level 1, 16 HP) died
  on turn 10 in a trial. The goal window reads "Survive", 11 turns: turn
  12's event wins.
- **Talk recruits of Chapter 13** (Amelia by Eirika or Franz, Cormag by
  Eirika) are left out: in a trial with them, sparing them cost six units and
  then Eirika on turn 11.
- **Tick rule.** The three v4 files are made with `ROUTE_TICK=capture` (and
  `MGBA_RUN_BP` set to `port/test/Makefile`'s `GBA_BP`): the bot samples each
  tick exactly as `gate0_capture.lua` does (`route_lib.lua`, "Tick rules";
  `port/test/README.md`, "Early flush VBlank"). With the old frame rule the
  first `route_e13.keys` had its key masks a frame late around an early flush
  VBlank in a battle animation at tick 19,196 (ticks 19,196 to 19,828 and from
  61,228 on), and the Gate 0 capture of that file played a different game:
  it never reached turn 12, and the end save was never written. The capture
  rule gives the same play two ticks later (the file has 89,876 ticks, not
  89,874); `route_e11.keys` and `route_e12.keys` are the same under both rules
  (only the header line naming `ROUTE_TICK=capture` differs). The older
  files keep the frame rule (`ROUTE_TICK` unset), which still makes them byte
  for byte. `route_check.lua` takes the same setting.

**One bot, two chains** (port-cover10, 2026-10-22). Coverage round 10 grew
the v4 bot in two branches at once: port-bots10e (Eirika's Chapters 14 to 19,
then the boosted starts and the post-game) and port-bots10i (Ephraim's route).
Both called their new rules v5 and v6, so in the merged `route_bot.lua`:

- Ephraim's chain keeps its names: `V5`, `V7` to `V10` and `tacticsV5` to
  `tacticsV10` (chapterIndex 0x17 to 0x23 and 0x3E, his Chapter 11).
- Eirika's chain's v5 and v6 are `E5` and `E6` (`tacticsE5`, `tacticsE6`;
  also `PROTECT_E`, `protectedUnitE`, `handleSupplyE`, `PROMO_LISTS_E`):
  Eirika's Chapter 14 on (index 14 and up but 61), the boosted starts and the
  post-game, and never Ephraim's chapters.
- Where both chains solved the same snag, each keeps its own answer in its own
  chapters: the world-map walk (`gmPathClear` for E5's chapters,
  `reachableNodes` for every other), the protected green unit (`PROTECT_E` for
  index 19, `PROTECT` for 32), the supply screen (`handleSupplyE`,
  `handleSupply`), the promotion menus (E6's for every chapter but Ephraim's,
  v7's for his).
- Lua allows 200 local variables in a chunk and the merged bot has more names,
  so its ALL_CAPS constants and per-decision flags (`V5`, `E6`, `SIEGE`,
  `TERRAIN_DOOR`, ...) are globals: the same values and the same uses (no name is
  used before it is set).

Neither chain's rules run in the other's chapters, so every route file of both
regenerates byte for byte. Checked on port-cover10 (2026-10-22: the merged
bot SHA-1 `30c20a01`, lib `3f6559d1`, frozen copies, two lanes, 3,485 s):
`route_ch5.keys` from power-on (`route_ch0` to `route_ch4` its prefixes),
`route_ch5x` to `route_e19`, the five boosted routes, `route_ch8i` and
`route_i9` to `route_i20`, each with the settings of its `# Generated` line:
all 39 files byte for byte (34 runs). A first try caught one slip of the merge
(the unit menu's Talk entry, lost in a hunk: `route_i9` left its path at tick
35,544), fixed before the merge commit. A sample on a trial merge with main
(`port-integrate9`, whose `GBA_BP` adds `gate0_rpscmd`) also comes out byte
for byte: `route_ch5x`, `route_e11`, `route_e14`, `route_b20`, `route_i9`.

The sections below keep each chain's own names ("v5" of Eirika's chain is E5
in the code).

Coverage 11 (port-bots11i) adds Ephraim's final-chapter rules as the global
table `I11` (`I11.on`, `I11.search`, `I11.noStop`, `I11.baitHeal`,
`I11.lordBait`, `I11.lure`, `I11.supplyHasHeal`; no new top-level local),
every one gated by chapterIndex 0x22 or 0x23 ("Tactics I11"). The search's
gate is `tacticsE6(ch) or I11.search(ch)`, and v10's `supplyTried` goes into
the search's save states only in those two chapters (nil elsewhere).
Checked (2026-10-22, the committed bot frozen, SHA-1 `38c2cede`, lib
`3f6559d1`, one lane, 1,830 s): the four new files (`route_i21`, made by an
earlier copy of the bot, `route_ib22`, `route_ibend`, `route_ibpost`),
`route_i20` (the one older file with ticks in chapterIndex 0x22: its last
245 ticks) and the five boosted routes (the search, the final boss, the
ending and the post-game code they share): all ten byte for byte. Then
`route_e15` to `route_e19` (Eirika's search routes, `ROUTE_SEARCH=1`; 4,364 s):
all five byte for byte too.

**Three chains, one bot** (port-cover11, 2026-10-23). Coverage 11 grew the
bot in three branches at once from port-integrate10's: port-bots11e (Tactics
E7, "Eirika's Chapter 20 and the final chapter with the real army": the table
`L.E7`, on only with `ROUTE_E7=1` and in chapter indices 0x14 to 0x16),
port-bots11i (Tactics I11, above: the global table `I11`, chapter indices 0x22
and 0x23) and port-bots11p (the feature routes, the post-game walls and the
dungeon record: the table `L.F`, on only with a feature setting, in the
post-game floors `L.F.WALLS` lists, or on the dungeon record screen). No name
clashed: none of the three adds a top-level local (the main chunk keeps 184),
each new global (`I11`, `ARENA_MAIN`, `ARENA_RESULTS`) and `L` field (`L.E7`,
`L.F`) is used by its own chain only, and no two chains declare one name in
one scope; so nothing was renamed. Where two chains edited the same line the
merged line keeps both edits: `support()`'s heal-item Trade
(`fbOn or i11Trade or L.E7.on`), the bait's test
(`V10 and (not isLord or lordBait)`: V10 is on in 0x21 to 0x23, so I11 sees
the same) and the header's `# Generated` line (`, ROUTE_E7=1` then the feature
settings; never both). Every other merged hunk is two blocks added at one
place, both kept. The merged bot is the base plus every changed line of the
three chains and nothing else but those three combined lines (a line-set
check). Checked on port-cover11 (2026-10-23, the merged bot frozen, SHA-1
`ecb67b50`, lib `ca63892e`; one heavy run at a time): all 69 route files byte for byte, in 64 runs: the 30
coverage-11 files, `route_ch5.keys` from power-on (`route_ch0` to `route_ch4`
checked as its prefixes) and the other 33 older files, each with the settings
of its `# Generated` line (and `ROUTE_POSTGAME`/`ROUTE_WM_NODE` where its row
names them); 9,436 s of runs, in two parts (2,642 s and 7,723 s of wall time:
the queue was split once so the collate's other heavy runs could take the one
slot). Scripts `wf-fe8\c11col_regen.sh` (c10's lanes and job list, extended)
and `c11col_qsplit.sh`; outputs `~/gate0-scratch/c11-col/regen-full*/`.

**Tactics v5 of Eirika's chain (E5)** (2026-10-21, `route_e14.keys` on). The fifteen older files
must regenerate byte for byte, so v5 is only for the chapters from index 14
(Eirika's Chapter 14) on, Chapter 11's index 61 left out (`tacticsV5`): v4
plus the rules below. The bot at this commit still regenerates all fifteen
byte for byte, and `route_e14.keys` too. Files are made with
`ROUTE_TICK=capture`.

- **World map: only an armory the lord can walk to.** The lord's walk stops on
  the node before one with a skirmish on it (`WorldMap_MuWalkLoop`,
  `src/worldmap_main.c`: an encounter, then the free cursor again). From
  `e14_start.sav` the monsters on node 10 (Teraz Plateau) stand between
  Hamill Canyon and every armory, so v4 walked Eirika to node 11, then A on
  node 9 opened the world-map menu (no path), B, and round again for ever.
  v5 plans the shopping only at an armory reached by a path over
  `gGmNodeLinks` with no skirmish node on the way (`GmFindPath` with flag 0,
  `src/worldmap_path.c`); with none it does not shop.
- **The lord keeps by the toughest fighter** (the most max HP + 2 x defence
  among the units that can fight and are not hurt) where v3 kept it by Seth,
  dead since Chapter 12. Without it Eirika stayed in Chapter 14's starting
  sand with the healers, and the turn-6 reinforcements there (three
  cavaliers) killed her on turn 7.
- **Units under a roof are not targets.** A red unit with `US_HIDDEN` (and
  `US_UNDER_A_ROOF`, Chapter 14's closed rooms) is not on the map until its
  room is opened: it is neither attacked nor walked to (v3 planned bow and
  magic attacks at such units through the wall every turn, and the Attack
  menu never offered them). Talk targets too.
- **Doors on the way to the seize tile.** In a seize chapter a unit with a
  Door Key opens only a door on the shortest way to the seize tile, with
  doors counted as open (cost 4), enemies left out and roofs counted as
  floor (the closed rooms become floor when opened); and it walks that way
  when it has nothing to attack. v3 opened the doors toward the nearest
  enemy, spent Chapter 14's few keys (enemy drops) on side rooms and left
  the only way to the throne shut (turn 60 in a trial).
- **A boss that never leaves its tile** (AI1 3 to 5: Carlyle on the throne)
  is attacked by any unit that survives the fight itself, not only where the
  enemy phase is safe; the next attackers of the turn finish it (a kill
  leaves its threat out). With v3's safe-only rule nobody ever attacked
  Carlyle.
- **Seize tile** of Eirika's Chapter 14 (index 14): (9,5). **Talk recruit**:
  Rennac, by L'Arachel (her talk event makes him join; Eirika's is an offer
  of gold, left out). He stayed in his closed room in `route_e14.keys`.
- Logging only: a key holder with no door on the way logs its distance to
  the seize tile.

**Tactics v5 of Eirika's chain for her Chapter 15 (not won with v5; won with v6 below).** Chapter 15
(index 15, Rout: a desert of 24x22 tiles, 46 enemies at the start, wyvern
riders among them, reinforcements from the south-west; Ephraim, Duessel and
Knoll join there in the turn 2 enemy phase) beat every try from
`e15_start.sav` (ten, `port-plan/pending/c10-e1.md`). The rules below are in
the bot for index 15 only (`RESCUE_CHAPTERS`); no route uses them yet:

- **Lords carried.** The army's toughest fighter (max HP + 2 x defence) that
  can reach a lord and carry it (aid: con - 1 on foot, 25 - con mounted, 20 -
  con for women: `GetUnitAid`) rescues it (Rescue, `overrideId` 0x63); the
  carrier then plays as a lord: safety first, last to move, never attacks.
  Eirika (Gerik) and Ephraim (Duessel) were carried from turns 2 and 3; both
  lords were alive on turn 60. Without it Eirika died on turn 3, 4 or 5 in
  every try: from turn 3 no tile she could reach was safe.
- **A healer with no staff takes one an ally cannot use** (Natasha's Mend
  broke in Chapter 14; Colm carried one).
- **Rout hold-out** (`ROUTE_ROUT_DEFEND=N`, off by default): the hold-out
  mode while more than N enemies with AI1 0 and an AI2 other than 3 are left
  (with AI2 3 an enemy waits until one of ours comes into its reach). In it
  the lord moves last (so its tile is judged with the others in place:
  moving first, Eirika was boxed in by allies who then walked away) and each
  unit stands by the nearest lord or lord's carrier. In trials it kept the
  army waiting, and Ephraim's group, alone in the south-west, was worn down
  (Ephraim died on turns 8, 15 and 18).
- **Why it is not won:** on turn 60 of the best try (both lords carried) 30
  enemies were left, 7 of the 13 units had died, and of the four fighters
  left Franz and Innes had worn out every weapon. The wyvern riders in the
  south-east (AI1 3) and the groups with AI2 3 wait to be attacked. There is
  no armory on the way (Chapter 14 to 15 has no world map; the map's vendor at
  (8,4) sells tomes, staves and potions).

**Tactics v6 of Eirika's chain (E6)** (2026-10-21, `route_e15.keys` on; `ROUTE_SEARCH=1`). Chapter
15 was won with a search over whole turns on top of the v5 bot. Everything
in v6 is off without `ROUTE_SEARCH=1` and only for chapter indices 15 on
(`tacticsV6`), so the sixteen older files regenerate byte for byte as
before (checked); `route_e15.keys` regenerates byte for byte with
`ROUTE_SEARCH=1` and the search's default settings (its `# Generated` line
names them: `ROUTE_SEARCH_K=12`, `ROUTE_SEARCH_BACK=3`, `ROUTE_SEARCH_BASE=4`,
`ROUTE_SEARCH_LOOKBACK=1.5`). The search needs `ROUTE_TICK=capture`.

- **The turn search.** At the first free cursor of each player phase (a
  "branch point") the bot saves the emulator (`emu:saveStateBuffer`, save
  data included) and its own state, then plays the turn and the enemy phase
  after it to the next branch point. Every turn plays four policies (the
  "base" set: zone 3; zone 3 and cautious; the plain v5 bot; zone 3 and
  guard; knobs below), loading the save state between them, and keeps the
  best end: fewest deaths (a healer counts 1.5), then the score 3 x the
  army's HP - 300 x the enemies left - 2 x their HP - up to 3,000 for each
  unit left under 40% of its HP (4,500 for a healer). If even the best of the
  four loses a unit or leaves one that low, it goes on with policies in which
  those units play safe, then with more knobs and noise seeds, up to
  `ROUTE_SEARCH_K` = 12 a turn; a try that has already lost more than the
  best so far is cut short. When every policy loses a lord the bot goes back
  a turn (up to `ROUTE_SEARCH_BACK` turns) and tries that turn's next policy;
  when every policy loses 1.5 or more (`ROUTE_SEARCH_LOOKBACK`), it plays the
  turn before once more with this turn's victims playing safe there too, and
  keeps whichever of the two ways ends better (once per turn). The key file
  holds only the kept turns' keys and replays open-loop like any other
  (mGBA is deterministic from a save state; the save states are taken and
  loaded only at a tick sample, the capture rule's own state put back with
  them, `route_lib.lua` `L.captureState`).
- **Policy knobs** (`P` in `route_bot.lua`): *safe* (these units attack only
  where safe and move to the least threatened tile on the way; healers heal
  only where the threat is under half their HP), *cautious* (no unsafe
  attack; 70% of its HP counts as deadly), *hold* (stand by the nearest lord
  or carrier), *zone N* (units other than lords end their moves within N
  tiles of the nearest lord or carrier), *guard* (healers move first, then
  fighters with nothing to attack stand on the open tiles next to healers and
  carriers that an enemy can reach), *press* (closer to the enemy counts
  most; 90% of its HP counts as deadly: the base set of a "stalled" turn, one
  where the enemies left have not gone down for three turns), noise seeds
  (a small hash noise on the attack and move scores and the unit order).
- **Attack trials.** For an attack on a boss, or in a stalled turn, the unit's
  best four attacks and its plan without one are each played out from a save
  state to the end of the unit's action, and the best real outcome is kept
  (the target's HP lost, a kill, the unit's HP lost, the estimated
  enemy-phase threat on its tile against the HP it has left). The battle RNG
  is part of the state, so the kept outcome is what the key file replays.
  From chapter index 16 on, a stalled turn also plays its base policies
  with *bold* trials, in which the threat after an attack is left to the
  turn's search (`route_e15.keys` was made before this rule).
- **Stall rules from chapter index 18 on** (`route_e18.keys`; the routes
  before it were made without them and still regenerate byte for byte): in
  a stalled turn the lords too walk toward the enemies left ("press", safety
  first as ever, so a zone round them moves with them), and the turn's score
  counts each unit's distance to its nearest enemy (3 a tile), so where
  every policy ties the one that gets closer is kept.
- **Rules for these chapters** (`V6`): a promotion item the unit can use
  (level 10 or more, its class in the item's list, `gItemUseJidList_*`) is
  used at once on the safest tile in reach (Item, Use, then the promotion
  screens: the first class, Change); a unit takes from the supply, from a
  tile next to Eirika while she is on the map, a promotion item it can use
  and up to two weapons it can use when its own have fewer than 40 uses (the
  supply screen: Take, Right to the item's page, the line, A); a map vendor in
  reach sells a healing staff to a healer without one and Vulneraries to a
  unit with fewer than two healing items, 3,000 gold kept (`VENDORS`); only a
  weak lord is carried (max HP + 2 x defence under 45: Eirika, still level 1;
  Ephraim fights), and not on turn 1 while she is out of danger, so the army
  can use the supply next to her first; a lord's carrier attacks where a lord
  may, its speed halved in the estimate; an HP-halving weapon (Eclipse: might
  255 in the item data) counts as half the defender's HP, once (with 255 the
  last six enemies of a try, near a shaman with Eclipse, stayed out of reach
  for 40 turns); seize tiles of Chapter 16 (13,3) and Chapter 20 (11,11).
- **A green unit to protect, from chapter index 19 on** (`route_e19.keys`;
  `PROTECT`: a unit whose defeat quote sets `EVFLAG_GAMEOVER`,
  `src/data_battlequotes.c`; Last Hope's Mansel, the rule of `port-bots10i`'s
  "Tactics v9" for Ephraim's Last Hope). The toughest mounted unit that is
  not a lord (`CA_MOUNTEDAID`, aid 25 - con at least the green unit's con:
  `GetUnitAid`, `CanUnitRescue`) and can get next to it rescues it from the
  safest tile there (the unit menu's Rescue; the canto takes it to the
  safest tile). Carrying it, that unit plays as a lord that never attacks:
  it moves last, to a tile away from the enemy and near allies (the hold-out
  lord's tile score), and the zones form round it too. A carried unit
  cannot be attacked. The game over (`ProcScr_BmGameOver`,
  `ProcScr_GameOverScreen`) counts as a lost lord in the search (the
  title's Resume Chapter would replay the same enemy phase). Not a
  hold-out: the usual tactics, as on Ephraim's route, where every hold-out
  try lost Mansel or a lord.
- **The boss on the seize tile, from chapter index 20 on** (Chapter 20,
  Darkling Woods: Morva, a boss that never moves, stands on the seize tile
  (11,11); the only tile next to it a unit can stand on is (11,12), the
  others are walls even for fliers). Experimental: Chapter 20 is not won
  yet ("Limits"). The rules: the seize waits only for that boss (other bosses
  may live); the sacred weapons count as effective x2 and the others x3
  (`ComputeBattleUnitAttack`), both sides' weapon stat bonuses count
  (`pStatBonuses`: Morva's Wretched Air gives pow +10, def +20, res +10) and
  `IA_NEGATE_DEFENSE` leaves the defence out (Morva's counter takes 36 from
  anyone); magic by the weapon's attributes only (a Divinestone hits
  defence). A unit attacks the boss with the weapon that hurts it most and
  anything else with the first one not effective against it (the weapon
  menu's entry 0x49 + slot); a unit whose every weapon is effective against
  the boss (Myrrh, the Divinestone) fights nothing else, except a unit within
  2 tiles of the boss's open side, heals (Vulnerary, Elixir) whenever it is
  below full HP, walks to the boss outside any zone policy, and keeps out of
  the reach of enemies with Stone. A kill of the boss counts 300,000 in an
  attack trial and 150,000 in the turn's score; each unit's distance to the
  seize tile costs 20 a tile (100 for that unit and for Eirika's carrier
  while the boss stands, 300 for Eirika or her carrier after). Walks to the
  boss or the seize tile ignore the red units on the way (the attacks clear
  it). Ephraim, the lord who cannot seize, carries Eirika (Rescue; he never
  attacks while carrying, presses on to (11,12)) and drops her on the seize
  tile once the boss is gone (Drop, overrideId 0x64, the target by its
  position); she seizes on the next turn. The others walk to the tiles round
  (11,12) once the boss is gone. A turn with no fewer enemies than two turns
  before counts as stalled (reinforcements).
- **The final chapter and the ending** (`route_b21.keys`; no older route gets
  there, so they all regenerate as before):
  - the Demon King (part 2, index 22, `L.FINALBOSS`): any unit, a lord too,
    attacks him with the weapon that hurts him most (the weapon menu) where it
    survives the fight and then one more attack of his (exact damage; the
    rest of the enemy phase is left to the turn's search), and the attack
    trials judge such an attack the same way. A unit 15 HP or more down heals
    with an item while he stands, and one with no heal item takes one by Trade
    from an ally that has two. With the lords' usual rule (threat under half
    the HP left) nobody ever attacked him: a first try stood from turn 14 to
    turn 43 with only him and three monsters left, the army's Divinestone and
    Vidofnir spent in part 1, Eirika's first weapon a Rapier;
  - once `PLAY_FLAG_COMPLETE` is set (and not `PLAY_FLAG_POSTGAME`) the event
    engine gets A only, never Start, and the turn search ends (the ending's
    scenes clear and reload the units);
  - `ROUTE_STOP=title:N` stops N ticks after the title screen that follows
    the ending's Fin screen (`gProcScr_FinScreen`, `gProcScr_TitleScreen`).
- **The post-game** (`route_bpost.keys`; only with `ROUTE_POSTGAME=1` and
  `ROUTE_WM_NODE`): on the title menu (`ProcScr_SaveMenu`) the bot takes
  Extras (`main_select` +0x2B over the set bits of `main_options` +0x30,
  `SameMenu_CtrlLoop`), then Map (`extra_select` +0x34 over `extra_options`
  +0x32, `SaveMenu_ExtraMenuLoop`), then A on the slot; on the post-game's
  world map the target is the next node of `ROUTE_WM_NODE` (the lord's walk
  ends on a dungeon node and enters it, `WorldMap_MuWalkLoop`); in a dungeon
  floor (index 0x24 to 0x37), while more nodes are wanted, the floor-clear
  choice (`YesNoChoice_Loop_KeyHandler` or `TalkChoice_OnIdle`) gets B: the
  retreat to the world map.
- `ROUTE_PARTIAL=1` writes the keys kept so far to `ROUTE_OUT.partial` at
  every kept turn (for a look at a long run); `ROUTE_DEBUG=1` logs why each
  unit moves where it does; `ROUTE_TRIAL_ALL=1` (not used by a route) runs
  attack trials for every attack.

**Tactics v5** (2026-10-21, Ephraim's route: `chapterIndex` 0x17 to 0x23 and
his Chapter 11, 0x3E). No older file visits these chapters, so the eighteen
files stay as they were. v4 plus:

- **The weapon per attack.** Every usable weapon is tried for every target
  and tile, and the weapon menu (`gWeaponSelectMenuDef`, entries 0x49 + slot)
  takes the one chosen; before, the bot fought with the first usable weapon.
  The triangle counts the reaver weapons (`IA_REVERTTRIANGLE`: each one
  reverses the bonus and doubles it, as `BattleApplyWeaponTriangleEffect`).
  In Ephraim's Chapter 9 that is Franz's Axereaver against the boss's axes.
- **Enemies are costly tiles, not walls, in the distance search** (8 more
  than the terrain). Ephraim's Chapter 9 fort has one-tile columns full of
  enemies; with walls no tile had a distance to anything, and the whole army
  waited in its start corner for eight turns.
- **A 1-2 range weapon from the armory** (Javelin, Hand Axe) for every fighter
  with none, besides v4's shopping. A boss that only attacks next to itself
  (`GuardTileAI` on a throne, AI1 3) can then be hit from two tiles away with
  no counter. Before Ephraim's Chapter 9 (node 2, Ide): Javelins for Seth,
  Franz, Gilliam and Ephraim.
- **Hold-out** (Ephraim's Chapter 10, index 24: turn 11's `TurnEventPlayer`
  with `EVFLAG_WIN`, or the boss): as v4, but nobody walks off to a Talk
  recruit (a recruit next to a safe tile is still talked to), and the others
  weigh threat as much as the distance to the lord (v4: distance ten times
  more). In a trial without this, Tana walked to Cormag alone and five units
  died; with it, three.
- **Chapter knowledge:** the seize tile of index 23 (22,7); Talk recruits:
  index 23 Amelia (by Ephraim or Franz), index 24 Duessel (green, by Ephraim)
  and Cormag (by Duessel or Tana); index 62 L'Arachel (green, by Ephraim)
  and Dozla (green, by L'Arachel); index 25 Marisa (by Ewan).

**Tactics v6** (2026-10-21, Ephraim's Chapter 12 on: `chapterIndex` 0x19 to
0x23). `route_i9.keys` to `route_i11.keys` (0x17, 0x18, 0x3E) were made
before it and stay as they were. v5 plus:

- **Recruit villages.** A village whose visit makes a unit join
  (`RECRUIT_VILLAGES`: index 25, (5,5), Ewan, `EventScr_089FB934`): while it
  is still open (`TERRAIN_VILLAGE_REGULAR`) and the unit has not joined, the
  fighter nearest it (not a lord, not under 40% HP) walks to it, and any unit
  that can stand on it with the threat under its HP (a lord: under half)
  takes Visit (the unit menu's overrideId 0x5C). In `route_i12.keys` that is Franz, and Ewan
  joins.
- **A red unit spared for a Talk counts double in the threat.** Nobody
  attacks it, so it keeps its full strength; in a first Chapter 12 try Dozla
  stood next to the spared Marisa and died to her Shamshir's critical hit.

**Tactics v7** (2026-10-21, Ephraim's Chapter 14 on: `chapterIndex` 0x1B to
0x23). `route_i9.keys` to `route_i13.keys` were made before it and stay as
they were. v6 plus:

- **Berserked allies count in the threat.** A blue unit under Berserk
  (`statusIndex` 4, the low nibble of +0x30) is moved by the game's AI at the
  end of the player phase (`CpOrderBerserkInit`, `src/cp_order.c`) and
  attacks anyone in reach; its reach (passing blue units, stopped by red and
  green ones) and its damage count like an enemy's. A first Chapter 14 try
  lost Ephraim on turn 12 to Dozla, berserked by a Druid's staff.
- **Promotion items.** A unit of level 10 or more whose class is in the
  item's list (`CanUnitUsePromotionItem`, `src/bmitemuse.c`; the 0-ended
  `gItemUseJidList_*` of the Hero Crest, Knight Crest, Orion's Bolt, Elysian
  Whip, Guiding Ring, Master Seal and Ocean Seal) goes to its safest tile and
  uses it (Item, the slot, Use); the class menu (`gMenuDef_PromoSel`) takes
  its first class and the confirm menu (`Menu_PromoSubConfirm`) "Change".
- **A hurt unit may attack where it is safe and the target cannot hit
  back** (before, a unit under 40% HP attacked only to kill).
- **A boss that cannot fight back is the lord's.** When a boss has no usable
  weapon left, the units next to it make way first, the lord moves next and
  walks to it, and no other unit ends a move next to it. Vigarde's throne has
  one tile next to it; once his Spear wore out, a hurt Seth stood there and
  waited every turn, nobody else could reach him, and a try stopped at turn
  60.
- **Chapter knowledge:** the seize tiles of index 27 (14,2) and 29 (13,3);
  Talk recruit Rennac (index 27) by L'Arachel (Ephraim's own Talk asks for
  gold and is left out).

**Tactics v8** (2026-10-21, from Scorched Sand on: `chapterIndex` 0x1C to
0x23). `route_i14.keys` was made before it. v7 plus:

- **Promotion items by Trade.** A unit that can use a promotion item an ally
  holds (and the ally cannot) takes it by Trade from a safe tile next to the
  ally, giving back its first item that is neither a usable weapon nor a heal
  item when it is full. Franz takes the Knight Crest Vigarde gave Ephraim,
  and Knoll the Guiding Ring Franz gives back.
- **HP-halving and defence-negating weapons.** A weapon with
  `WPN_EFFECT_HPHALVE` (Eclipse: might 255 in the item table) does (HP + 1) /
  2 whatever the defence (`ComputeBattleUnitSpecialWeaponStats`,
  `src/bmbattle.c`); `IA_NEGATE_DEFENSE` (Luna) leaves out the defence and its
  terrain bonus. With Eclipse at 255, a Shaman's threat covered the whole map
  of Scorched Sand and the army stood still from turn 22 to turn 60.

**Tactics v9** (2026-10-22, from River of Regrets on Ephraim's route:
`chapterIndex` 0x1E to 0x23). `route_i14.keys` to `route_i16.keys` were made
before it. v8 plus:

- **Magic by the weapon's attributes only** (`IA_MAGIC` or
  `IA_MAGICDAMAGE`), as `ComputeBattleUnitDefense` (`src/bmbattle.c`): a
  Dragonstone (weapon type 11, no such bit) hits defence, not resistance.
- **The siege of a lone standing boss.** When every enemy left is a boss
  whose AI never leaves its tile (AI1 3 to 5), damage is counted exactly (no
  +1 margins), a healthy unit (at least 3/4 of its HP, not a lord) attacks
  the boss wherever it outlives the counter, hurt units heal (Vulneraries and
  Elixirs from 3/4 of their HP down, the healing tiles of
  `TerrainTable_HealAmount`) out of its reach, and everyone else keeps out of
  it. In River of Regrets Lyon sits on a gate that only (20,7) touches, and
  his counter plus his enemy-phase spell, each with the +1 margin, topped every
  unit's HP: a first try waited from turn 14 to turn 60.
- **A green unit to protect** (`PROTECT`: a unit whose defeat quote sets
  `EVFLAG_GAMEOVER`, `src/data_battlequotes.c`; Last Hope's Mansel). A
  mounted unit (`CA_MOUNTEDAID`, aid 25 - con at least its con: `GetUnitAid`,
  `CanUnitRescue`) rescues it from the safest tile next to it (the unit
  menu's Rescue, overrideId 0x63; Rescue allows the canto, which carries it
  to the safest tile). The carrier does not fight and keeps back; a carried
  unit cannot be attacked.
- **The game over stops a try** (`ProcScr_BmGameOver`,
  `ProcScr_GameOverScreen`). The lord check misses a green unit's death, and
  the title's Resume Chapter replays the same enemy phase with the same RNG:
  a first Last Hope try did that for 20 minutes.
- **Last Hope is not played in hold-out mode.** It can be won at turn 14, but
  40 or more enemies come from every side. Every hold-out try lost Mansel or a
  lord by turn 12: around the lord (Mansel killed on turn 12), guards on the
  tiles one and two from Mansel or on the throne room's three entries (turns
  5 to 8), a walled refuge in the north-west room (the carriers never got
  through the top corridor), Mansel or a lord carried (a lord left alone died
  on turn 11 or 12). With the usual tactics and Mansel carried, the army
  thins the enemy out and lasts to turn 14.
- **Chapter knowledge:** Talk recruit Syrene (index 30, green) by Innes, Tana
  or Vanessa.

**Tactics v10** (2026-10-22, from Darkling Woods on Ephraim's route:
`chapterIndex` 0x21 to 0x23). `route_i17.keys` to `route_i19.keys` were made
before it. Four units are left, and the first tries lost a lord by turn 3
(the threat model) or stood still to turn 60 (worn-out weapons, no healer,
Morva out of reach). v9 plus:

- **The damage model.** The threat counts each enemy's worst weapon, not its
  first (a Gorgon's first is Stone, might 0; its Demon Surge took 16 of
  Ephraim's 28 HP on a tile counted safe); Stone itself counts 8. The
  equipped weapon's stat bonuses count (`pStatBonuses`: Wretched Air pow
  +10, skl +10, def +20, res +10; Siegmund and Sieglinde pow +5; Audhulma res
  +5), the sacred weapons' effectiveness is x2 and every other weapon's x3
  (`ComputeBattleUnitAttack`), and a brave weapon strikes twice. Before,
  Seth's Audhulma was counted 72 against Morva's def 4 (one hit kills); it
  did 54 against 24, and Morva's counter took 36, not 26.
- **The supply** (the unit menu's Supply, overrideId 0x69, for the lord and
  the units next to him; `ProcScr_BmSupplyScreen`, `src/prep_itemsupply.c`).
  A unit takes the best weapon it can use when that beats its own by 20
  points (might x10, less the weight it cannot carry; +100 for a weapon
  effective against monsters, +60 for a brave one), and a heal item when it
  holds none, giving the supply what it has no use for when it needs the room
  (weapons and staves it cannot use, gems, promotion items that do not fit
  it); once per chapter, again when its weapons are nearly worn out. The
  page is the item's weapon type, the line its place in
  `gPrepScreenItemList`. The lord with a plan moves first.
- **The vendor** (`gWMNodeData` +0x10, the node menu's Shop, after the
  armory): an Elixir for each unit that has none (lords first), else a
  Vulnerary; a full unit sells what nobody in the army can use, or buys into
  the supply (the shop's "Send it to storage?", Yes).
- **Stat boosters** are used (the safest tile, Item, Use), and **every unit
  heals under 3/4 of its HP** (a lord's threat also counts the blue units yet
  to move as no wall: Ephraim, moving after Eirika, opened the way for the
  Deathgoyle that killed her).
- **The holder of the seize tile.** Morva holds (11,11), the only tile next
  to it is (11,12), he does 36 a hit to anything within two tiles, and no two
  units could kill him in one turn. *The strike*: when the units yet to move
  can kill the holder this turn (exact damage, every hit landing, each one
  outliving the counter) and the seizer can then reach the tile, they attack
  it whatever the threat after, and the seizer, moving last, seizes: the
  chapter ends before an enemy phase. *The bait*: otherwise a fighter at 3/4
  of its HP or more that outlives the holder's attack (with the margins and
  every other enemy's threat on top) waits in its reach with the weapon whose
  counter hurts it most equipped (Item, the weapon, Equip, B, Wait), once the
  seizer stands on (or can reach) a staging tile, one move from the seize
  tile, without a deadly threat; the holder, which never leaves its tile,
  attacks it in the enemy phase and the counter wears it down. The seizer
  waits on the safest staging tile it can reach. The weapon that hurts the
  holder most is kept for it from 6 uses on (Seth's Audhulma was worn out on
  Wights by turn 25). The seizer may seize past any boss but the holder
  (Riev, `CA_BOSS`, was another), and once the tile is free only a threat that
  could kill it rules a tile out.
- **The leader** (Seth, else the armed non-lord with the lowest id) makes for
  the holder, the other fighters keep with it (Franz took the southern way
  alone and died to Riev); a lord's kill in one blow counts its target gone
  (Ephraim never used Siegmund); a weapon that is hard to replace (effective
  against monsters, S rank, brave) costs 40 points, so a plain one makes the
  kills it can.
- **Chapter knowledge:** the seize tile of index 33, (11,11).

**Tactics I11** (2026-10-22, coverage 11, branch `port-bots11i`: Ephraim's
final chapter, `chapterIndex` 0x22 (part 1) and 0x23 (part 2) only; every
rule is gated by those two indices, so no older route changes: `route_i20.keys`
ends 245 ticks after part 1's map loads, before any turn). v10 plus:

- **The turn search** (Eirika's E6 search, "Tactics v6") plays these two
  chapters too when `ROUTE_SEARCH=1`; v10's supply visits are saved with its
  save states there. Without the search, a lord's death stops the try as
  before.
- **The final boss is the holder** (v10, with no seize tile): Lyon (char
  0x6C) in part 1, the Demon King (0xBE) in part 2. *The bait*: a healthy
  fighter that outlives his attack (exact) and every other threat (with the
  margins) waits in his reach with the weapon whose counter hurts him most
  equipped; Seth's Audhulma does 10 a hit to Lyon (def 26) and doubles him:
  20 a round. *The bait's heal*: a hurt bait uses its Elixir on its bait tile
  (Item, Use) when the heal lets it outlive the next attack, so it counters
  every enemy phase (`I11.baitHeal`). *The strike*: when the units yet to
  move can kill him this turn (exact damage, each one outliving the counter;
  Ephraim's Siegmund, 16, kills Lyon at 16 HP or less in one blow) they
  attack, with no seizer to wait for. The weapon that hurts him most is kept
  for him whatever its uses (a non-lord's, doing 10 or more a round).
- **Part 1's event areas** (`src/events/ch21b-eventinfo.h`): no unit ends its
  move on the corridor (10,5)-(12,6) below the throne room (`AREA`
  `EventScr_089FDAD4`: three Wights by Lyon every turn for ten turns) or in
  the east middle (12,7)-(22,12) (`EventScr_089FDA2C`: five Arch Mogalls, whose
  Crimson Eye the PC build does not have yet, C10-1; `ROUTE_I11_EAST=1` allows
  it); both count as walls in the distance fields, so the army goes round by
  the west room and the stairs (5,1)-(5,2) into the throne room.
- **A lord's kill** (by the whole fight, the counter between the hits
  counted) leaves its target out of the threat, and is safe when the threat
  left is under the HP the lord keeps: Ephraim's Siegmund kills a Draco
  Zombie (68 HP, 34 a hit twice); the old rule counted the dead zombie's 27
  and nobody attacked it while it held the only west passage (first try,
  turns 5 to 24).
- **Heal items**: a fighter (not a lord) with none takes one from the supply
  (also on a later turn) or by Trade from an ally with two (part 1); the
  stalled turn's "press" policy sends the lord on as well.
- **Part 2** (the Demon King, `AiTryDKSummon`: on turn 1 he summons on four
  sides of himself and attacks nobody; later summons and Nightmare need five
  of our units in reach, and four are left): on turn 1 the army holds its
  start tiles (out of his reach) and only uses the supply and Trade; Ephraim
  may be his bait (Siegmund, 24 a hit; his Demonlight leaves Ephraim 2 of 39)
  and takes an Elixir by Trade from an ally that holds one; a bait's counter
  must do 10 or more; routine heals take a Vulnerary first (more than 20 HP
  missing: the Elixir); no heal-item Trade for the others (Franz and Ephraim
  traded the Elixir back and forth). *The lure*: a healthy fighter with no
  attack waits out of the Demon King's reach and in the reach of the monsters
  that stay by him (AttackInRange), where it outlives them all, and counters
  there (a try stood still from turn 5 to 17 with an Arch Mogall and a Draco
  Zombie next to him).

**World-map walk** (2026-10-21, every v4 chapter). The armory for shopping is
now the nearest one the lord can walk to: over `gGMData.openPaths` (each a
`gWMPathData` index, its two nodes at +4), never through a node that holds a
skirmish (the route search `GmSearchPath` skips them, and
`WorldMap_MuWalkLoop` stops the walk in front of one with the blocked-walk
animation, `GmapSogu`), at most 16 steps. From `i13_start` the only way back
to the armories passes node 15, held by monsters: the old rule picked node 5,
A there played the animation and the bot tried again for ever. Where the old
pick was reachable (every committed route) the pick is the same.
The chapter's node: a node both routes share (Scorched Sand) holds Ephraim's
chapter at `chapteridx_ephram` (+5 of `struct GMapNodeData`; +4 is
Eirika's). When no node has the chapter at +4, the bot takes the node with it
at +5 (v7 chapters only); before, it pressed L on the world map for ever.

## Verification (2026-10-19, `fireemblem8.gba` SHA-1 c25b145e37456171ada4b0d440bf88a19f4d509f)

- **Replay matches generation.** `route_check.lua` replayed each file
  open-loop. For all six, its milestone list is identical to the one the
  generator wrote in the file header.
- **Harness agrees.** `gate0_capture.lua` (the Gate 0 harness) replayed
  `route_ch5.keys` for 135,505 ticks in 142,340 frames, the same as
  `route_check.lua`. Its `gPlaySt` bytes show `chapterIndex` changing at these
  ticks:
  - 0 → 1 at 4,944.
  - 1 → 56 at 19,913, then 56 → 2 at 20,278.
  - 2 → 3 at 37,431.
  - 3 → 63 → 27 → 3 → 4 at 66,616 to 66,641.
  - 4 → 6 at 106,855.
  - 6 → 5 at 134,701.

  The final tick reads chapterIndex 5, turn 1, phase 00.
- **Prefixes.** Each file is a prefix of the next, entry for entry.
- **Addresses.** Every address in `route_lib.lua` was checked against
  `arm-none-eabi-nm fireemblem8.elf`.

## Limits

- **Tick-exact.** The routes are timed to the tick against the GBA's own state,
  with gaps of 1 to 20 ticks between presses. If a port differs in game logic,
  RNG or key handling, it drifts off the route; it does not just play a little
  later. The PC hook replays key masks per logic tick, but that is not enough:
  the GBA's lag frames move game state against the ticks (a lock set two ticks
  later on the PC at 5,666), so the plain PC leaves every route there. The PC
  check runs the routes through the lag replay instead ("PC check" above).
- **RNG.** Battle results depend on the RNG. The RNG values above are where a
  port should agree with the GBA at each milestone.
- **Save data.** The routes assume a blank save (mGBA's in-memory SRAM, as in
  the harness). With an existing save, the title and New Game menus differ.
- **Chapter 5x from power-on.** The first two tries played on from
  `route_ch5.keys` and stalled (Orson on the seize tile; then Ephraim at
  (1,14) every turn, past tick 630,000, because v1's threat ignored walls and
  the boss's tile blocked the way to the seize tile). Tactics v2 fixed both;
  Chapter 5x is now routed from `ch5x_start.sav` (`route_ch5x.keys`), not as a
  seventh prefix file.
- **Chapters after 15 on Eirika's route.** `route_e15.keys` to `route_e19.keys` need
  `ROUTE_SEARCH=1` to regenerate (tactics v6 of Eirika's chain, E6), and the search is slow: about
  40 minutes for Chapter 15, 15 to 25 for each of 16 to 18, 50 for 19.
  `e20_start.sav` (the end save of `route_e19.keys`, `chain-save
  CHAIN_SAVE=e20_start`) is the start for Chapter 20 (index 20), won with the real
  army since coverage 11 (tactics E7, `route_e20.keys`, above, and the final chapter
  and the ending after it, `route_e21.keys`, `route_e22.keys`, `route_eend.keys`);
  before that, not won: in
  twenty-one tries (2026-10-22, `port-plan/pending/c10-e3.md`) the bot killed
  Morva (Myrrh, at full HP, from (11,12): turns 6 to 13) but never got Eirika
  (level 1, 20 HP, mounted: the thickets are walls, so the way to (11,12)
  is 30 to 50 move points through the monsters of the north or south-west)
  onto the seize tile: walking, she found no safe tile on the way; carried,
  the carrier stalled in the north-west with 30 or more enemies left
  (reinforcements every turn from turn 15). So until coverage 11 Chapter 20
  on, the final chapter and the ending were played only from the boosted starts
  ("Boosted starts" above: test coverage, not story play); the post-game still is. The PC check of
  `route_e18.keys` ends at tick 62,369 (a crash, "PC check of Eirika's
  Chapters 15 to 18" above).
- **Ephraim's route is routed to its final chapter:** `i21_start.sav` (the end save of `route_i20.keys`,
  `chain-save CHAIN_SAVE=i21_start`) starts Sacred Stone part 1 (index 34),
  with four units left (Ephraim, Seth, Franz, Eirika).
- **The final chapter on Ephraim's route: part 1 is won with the real army
  (coverage 11, `route_i21.keys`, Tactics I11 with the turn search), part 2
  only from a boosted start** (`route_ib22.keys`: in four tries the army
  cleared the summons but ran out of healing against the Demon King, "Boosted
  starts" above). Before (step I5, three tries without the search, the last
  two with a trial bot that knew Lyon as a holder: the bait and the strike,
  Audhulma kept for him, the corridor that loads Wights by Lyon never stopped
  on) part 1 was lost; the notes below are from then.
  Lyon (73 HP, def 26, res 29; Naglfar does 54 against resistance: 33 to
  Seth, 43 to Ephraim, 46 to Franz) takes more than 2 a hit only from Seth's
  Audhulma (10 a hit, 8 uses left: 80 at most) and Ephraim's Siegmund (16,
  but Lyon's counter kills Ephraim), so the win needs every Audhulma hit on
  Lyon over four bait rounds, with Seth healed between them and the throne
  room's ten guards cleared first. The best try killed 29 of the 36 enemies
  by turn 24 but lost Franz (turn 17, no heal item left) and Seth (turn 23, in
  the throne room); the lords alone cannot hurt Lyon. Part 2's Demon King
  (read from the scout's `dj_e21x.sav`: 120 HP, pow 23, def and res 25,
  spd 18, with Demonlight's pow +10, def +10, res +15 or Ravager's
  +15/+15/+10) takes 24 a hit from Siegmund, 19 from Audhulma and 9 from
  Sieglinde, and his Demonlight (range 1 to 3) does 32 to Ephraim (39 HP),
  so Ephraim cannot take his counter and his next attack in one round: 120
  HP to bring down with Audhulma mostly spent on Lyon. A stronger army
  (fewer of the 21 units lost earlier on this route) or a debug-jump start
  is the way on.
- **Losses.** The bot keeps losing units; later chapters may need
  stronger tactics. It shops for weapons (v4); on Ephraim's route it visits the one recruit
  village it knows (Ewan's, v6) and uses promotion items (v7), and on Eirika's
  route from Chapter 15 on (E6) it promotes, takes from the supply and buys at
  map vendors. It does not
  visit other villages, open chests or use the arena, and it missed the Talk
  recruits Amelia (Chapters 9 and 13), Cormag (13) and Rennac (14) on Eirika's route, and
  Rennac (Ephraim's 14) and Syrene (17 on his route).
  On Eirika's route: fifteen dead by Chapter 15 (Seth, Saleh and Dozla among
  them; Eirika still level 1, 16 HP); Chapter 15 cost five more units (Ewan,
  Natasha, Colm, Knoll, Innes) and Chapter 18 Syrene; Chapters 16 and 17
  none. Chapter 19 cost five (Gilliam, L'Arachel, Duessel, Gerik, Moulder):
  four units are left for Chapter 20 (Eirika, Ephraim, Franz, Myrrh).
  On Ephraim's route it
  had lost eight units by his Chapter 14 (Neimi, Amelia, Moulder, Natasha,
  Lute, Cormag, Colm, Tana),
  eighteen by Chapter 17 (also Ewan, Garcia, Dozla, Gerik, Knoll, Marisa,
  Tethys, Gilliam, Saleh, Duessel) and twenty-one by Chapter 20 (also
  L'Arachel, Innes, Myrrh); four units are left. Its battle model
  lets every hit land, so a unit at low HP still attacks where the kill looks
  sure (Duessel at 10 HP against Orson).
- **Tick rule.** A file made with the frame rule can play differently in the
  Gate 0 capture when an early flush VBlank falls where a key press matters
  (`route_lib.lua`, "Tick rules"); new files should use `ROUTE_TICK=capture`.

## Coverage 12, merged (`port-cover12`)

The eleven coverage-12 shards merged into one test branch, `port-cover12` (from `21189271`: main
`9ef060ea` + `port-cover11`; `port-plan/pending/c12-findings.md`). Five of them changed the bot: scenes-e
and scenes-i (`L.W`: `ROUTE_WATCH=fast|slow`, `ROUTE_WATCH_SEED`, `ROUTE_WATCH_FROM`, `ROUTE_PREFIX=FILE`
with `ROUTE_PREFIX_TICKS`), features (`L.F`'s coverage-12 settings, among them `ROUTE_WATCH=1` and
`ROUTE_PREFIX=FILE:N`), postgame-real (`L.PR`, on with `ROUTE_PG_REAL=1`) and iend-real (tactics E7 on map
0x23). Each kept its rules in its own table, but two settings had two meanings: `ROUTE_WATCH` (features
`1`, scenes `fast` or `slow`) and `ROUTE_PREFIX` (features `FILE:N`, scenes `FILE` with
`ROUTE_PREFIX_TICKS=N`). Merged as they were, every features file would have stopped at `L.W`'s check of
`ROUTE_WATCH` and every watched tail at `L.F`'s check of `ROUTE_PREFIX`, and `L.F.header()` would have
named the scenes' values a second time. The merged bot keeps them apart by value: `L.W` takes only `fast`
and `slow` and a prefix with no `:N`, `L.F` only `1` and `FILE:N`, and each one's part of the `# Generated`
line names only its own, so every file's header stays as it was. The `# Generated` line itself ends
`L.F.header() .. L.PR.header() .. L.W.header()`; the other conflicts were sections added at the same
place (both kept).

**Byte for byte:** the merged bot (frozen copy `~/gate0-scratch/c12-col/frozen-full/`, `route_bot.lua`
SHA-1 `06b57e16`, `route_lib.lua` `94f7bdaf`) makes **177 of the 178 bot-made key files again byte for
byte**, each from its start save with its `# Generated` line's settings plus those its README row names
and the header leaves out (`ROUTE_MAX_TURN`, `ROUTE_POSTGAME` and `ROUTE_WM_NODE`): coverage 11's 64 jobs
(its 69 files; `route_ch0_prologue` to `route_ch4` checked as prefixes of the regenerated `route_ch5`),
coverage 12's 33 route files, monkey-post's 8 bot prefixes (`monkey/pg_*`) and 72 of the 73 bot-made
watched files. The one that is not, `watch/rp_ch5x_bmvsync.keys`, says why in its own header: F-c12-si-1's
repro, made by scenes-i's first attempt's bot (never committed). 29,865 s of runs (8.3 hours, `ws_e19` the
longest at 1,407 s), 4.2 hours on two lanes, every run exit 0. The menu monkey's shared files changed too
(`menu_monkey.lua`, `route_lib.lua`): a sample of five monkey files comes out byte for byte
(`mkD_e14_start_s4`, `mk10A_route_ch8_tick1587_s8`, `mk12e_e18_start_s1`, `mk12i_i19_start_s2`,
`mk12P_i_wm_s4` with `MONKEY_WM=post`).

**New chain entries** (`port/test/Makefile`): coverage 12's feature routes (`chain-route
CHAIN=f_house_ch1|f_village_ch2|f_village_ch4|f_village_ch5|f_doorkey_ch8|f_chestkey_ch8|f_arena_e10|
f_arena_i12|f_supports_tower_e|f_supports_tower_i|f_lordpromo_e16|f_secretshop_e14|f_thirdtier_ch6|
f_promochoice_i14`), with chain saves for `ch1_start` (`route_ch0_prologue`, `save:1`), `ch4_start`
(`route_ch3`, `save:4`) and the two edited starts `f_mc_start` (`route_e14`, the edit at the end of tick
1,600, the SRAM at 1,725) and `f_tt_start` (`boost_ch6`, 800 and 1,669); `CHAIN_EDITS` names their edit
files (`features/mc_e14.edit`, `features/tt_ch6.edit`). `chain-save` makes all four byte for byte (the
saves in `~/fe8-chapsaves/`).

**CHECK_LONG rows** (`port/check-all.mk`, `CHECK_COVER_ROUTES` while `CHECK_COVER_LONG=1`, the default):
51 more route rows, coverage 11's 22 chain-p files (the Tower and the Ruins boosted, four skirmishes, five
features) and 29 of coverage 12's (five skirmishes, the Tower and the Ruins floors 1-3 with the real army,
13 features), about 2.8 million ticks; coverage 11's e and i rows and coverage 12's `i22`/`iend` were
already chain rows. Left out as not cheap enough: `route_er4` (Ruins floor 4, not won: its second half is
the bot ending its turns to turn 99) and `route_f_lordpromo_e16` (its first 95,729 ticks are
`route_e16`'s, the `route-e16-lag` row's); their `chain-route` targets stay. Each row's title names its
start ("chapter save", "chapter save (real army)", "boosted start (test coverage only)", "edited start
(test coverage only)"). Three rows carry a recorded limit and are KNOWN, not FAIL (`port/test/known_limits.json`,
port/test/README.md "Known limits of check-all rows"): `route-br9-lag` (N-C11-1, `gPaletteBuffer`
62,549-62,563 and 96,999-97,215), `route-i21-lag` (a frame on a GBA lag tick, 62,520) and `route-er3-lag`
(H-c12-pr-1's knock-on, the side windows at four phase changes). Run through check-all's own row targets
on this branch (`build/check-c12rows-*`): those three KNOWN, and `route-f_house_ch1-lag`,
`route-f_thirdtier_ch6-lag`, `route-f_secretshop_e14-lag`, `route-f_village_ch4-lag` PASS (their saves made
by the new chain rules). The other rows' files were checked on the same game code by their shards (more
frames than a row compares) and by integration 11.
