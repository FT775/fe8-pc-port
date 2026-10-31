# Coverage 12 "scenes-e": Eirika's route with every story scene watched

Bot output (`port/test/oracle/route_bot.lua` with `ROUTE_WATCH`; never TAS-derived, never hand-edited).
Every bot route before these skipped each skippable scene with Start, so scene-only code (talk codes,
portrait moves, CG scenes, screen shakes, music cues, the chapter title cards, the opening) had barely
been compared; round 10 found a crash there (Ephraim's Chapter 10 `[ClearFace]`, N-R10-STUBS-1).
These files play Eirika's whole route again, from power-on through the Prologue to the ending, from the
same start saves and with the same settings as the `route_*.keys` they repeat (each file's `# Generated`
line), but never press Start or B in a scene:

- **`wf_*` (fast):** A every 4 to 12 ticks in every event, battle quote, talk, CG text and the ending,
  mostly for one tick, sometimes held 2 to 4 or 15 to 45 ticks. A press while a talk prints takes its
  instant-print path (`TalkSkipListener_OnIdle`'s `instantScroll`); A held runs an event's stalls 4 times
  as fast (`Event0E_STAL`) and the ending's records twice as fast (`ending_details.c`).
- **`ws_*` (slow):** never a key while a talk or CG text prints (the print-delay path, `Talk_OnIdle`'s
  `printClock`, and every `[.]` pause); A 20 to 80 ticks after a talk waits for A or a choice opens; with no
  talk on screen (moves, map changes, the ending's screens) A every 90 to 180 ticks.
- Both: the opening (`gProcScr_OpSubtitle`: the intro animation and its slides) and each chapter's title
  card (`gProcScr_ChapterIntro`) run to their end with no key. The pace is a hash of the tick and
  `ROUTE_WATCH_SEED` (default 1), not the game's RNG, so every file is made again byte for byte
  (`route_bot.lua`, "Coverage 12").

Everything else is the bot's normal play (the scenes the routes reach: chapter openings and endings, the
world-map events before each chapter, the Talk recruits the bot makes, boss battle quotes, deaths, the
route split, the ending, the credits). Watching consumes RNs where the skipped scenes did not (the
Prologue's opening scenes call the RNG every frame), so from that point on the battles differ from
`route_*.keys`; the bot plays them closed-loop as always, and every file wins its chapter but in three chapters:

- **Chapter 10** (index 11): watched, the bot loses (Eirika dies on turn 15) with every pace seed (1-6)
  and lord-safety setting tried, since the watched opening leaves the same RNs at turn 1 whatever the
  pace. So it has two files a pass: `*_e10l` (the opening and 15 turns watched, to the loss) and `*_e10t`
  (`route_e10.keys` open-loop to tick 101,100, its last enemy phase, with `ROUTE_PREFIX`, then the bot
  watching the boss quotes, the ending and Chapter 11's opening; frame tick rule, as `route_e10.keys`).
- **Chapter 20**: watched, the turn search (`ROUTE_SEARCH=1`, as `route_e20.keys`) cycled between turns
  46 and 47 for 3 hours (every policy of turn 47 lost a lord) and was stopped. `*_e20l` is the run without
  the search (Ephraim dies on turn 26), `*_e20t` the tail (`route_e20.keys` to tick 180,900, then the bot
  with the search watching the boss, the ending and the final chapter's title).
- **The final chapter's part 1** (index 21): watched, the bot stalls (15 enemies left, it only ends its
  turns until turn 60). `*_e21s` (watched to the stall) and `*_e21t` (`route_e21.keys` to tick 58,310,
  then the bot watching the boss, the ending and part 2's opening to its preparations; the turn search
  starts after the prefix).

**Checked**, plain PC (no NULL probe) through the lag replay, every traced field on every tick, **every
frame of every scene, battle and menu** (`route_check.lua`'s `ROUTE_SCENES_OUT` lists the ticks of each
kind: the event engines, battle quotes, talks, CG text, the title cards, the opening, the credits, the
battle screen and the menus; `tools/scene_shots.py` makes them `GATE0_SHOT_TICKS`, widened by 3 ticks,
joined across gaps under 30 and then joined until at most 64 windows are left, the PC hook's limit: the
ticks between get their frames too) and a frame every 60 ticks of map play:

    make -f port/test/Makefile watch-route WATCH=wf_e10      # or any NAME below

`watch-route` makes the start save with `chain-save` when `port/test/saves/` lacks it, checks it against
the file's `# Start:` line, and hashes the GBA frames in `WATCH_JOBS` processes before the compare
(`tools/frame_prehash.py`, the compare's own hash). Results and findings:
`port-plan/pending/c12-scenes-e.md`.

**The opening's rotated frames** (`wf_ch5`, `ws_ch5`, ticks 1,970-2,016): the intro animation turns its
picture with `BgAffineSet`, where mGBA's float method and the port's integer method can differ by one
(OL-2), and such a frame passes only with the affine replay's proof (P2.2b), which the lag replay does not
run. Their proof is the `scripted` recipe on the file's first 4,721 ticks, every frame of the opening:

    GATE0_SHOT_TICKS=823-4505 make -f port/test/Makefile scripted SCRIPTED_KEYS=port/test/inputs/watch/wf_ch5.keys SCRIPTED_TICKS=4721

(2026-10-23: the 44 frames are oracle-limit frames, "the affine replay draws the GBA's frame"). Without it
`watch-route` reports them.

**Names in the notes** (detail in `port-plan/pending/c12-scenes-e.md`):

- **F1**, the Prologue narration's full stop, over the world map: `DrawSpriteTextGlyph` draws the last
  character of "The continent of Magvel." into OBJ VRAM (0x06014xxx) while the GBA's raster is past line
  131, so the GBA shows it a frame later than the PC (2 pixels, one frame; every traced field equal). A
  raster-timing frame the redraw cannot prove: the world map's line handler (`WorldMapHblank`) writes
  palette entry 0x05000054 on every line of that frame, so the GBA's stores replayed at their lines do not
  lead to the PC's memory ("rollback"), the kind `port/test/README.md` lists as not covered ("the world
  map's palette effects"). Snapshot `~/fe8-snapshots/c12-se-op4710/tick4705` (wf_ch5).
- **F2**, **F3**: one frame on a GBA lag tick (coverage 11's "frame on a GBA lag tick" kind, not a
  raster-timing frame: the GBA stored nothing watched while it drew it, "nostores"), every traced field
  equal, the next frame equal. F2: the first frame of a staff's map animation after a tick whose main loop
  ran over 4 or 5 GBA VBlanks (`wf_e15` 16,930, `wf_e18` 31,258, `ws_e12` 84,347, `ws_e15` 28,694): the GBA's picture is darkened all over,
  the PC's is not. F3: a light effect in a Chapter 16 event (`wf_e16` 107,608, `ws_e16` 137,351): the PC's picture lacks the
  light layer on its top ~80 lines; its line handler (`BraceletSealBreakFx_OnHBlank`, `mapanim_eventcall.c`) runs only while
  `gBraceletSealBreakFxStretchEnabled` (IWRAM, not in the raster log's watched RAM) is set by its VSync proc. Snapshots
  `~/fe8-snapshots/c12-se-e15bat/tick16925`, `c12-se-e16lag/tick107603`.
- **F5**, the same kind in a spell: Lyon's Naglfar against Myrrh in the final chapter's part 1, as coverage 11's `route_i21` 62,520,
  (`wf_e21t` 59,364-59,420, `ws_e21t` 59,959-60,015; `EfxMagicHBlank_805B750` on every line) has a GBA lag VBlank every 4th tick;
  on 6 of them the raster redraw with the GBA's timing is not the GBA's frame ("something besides the
  timing differs"), every traced field equal. Snapshot `~/fe8-snapshots/c12-se-e21spell/tick59355`.
- **F4**, `gPaletteBuffer` palette 9 colours 12-15 (bytes +0x138-0x13F) differ for 14 ticks after a map
  change on GBA lag ticks (`wf_e18` 90,364-90,377): coverage 11's N-C11-1 kind (`route_br9`, the same
  colours), the lag replay's limit. Snapshot `~/fe8-snapshots/c12-se-e18pal/tick90358`.
- **H1**, the PC hook's 64 shot windows: `gate0_hook.c` keeps only the first 64 ranges of
  `GATE0_SHOT_TICKS` (`sShotRanges[64]`) and drops the rest without a word, and the compare compares frames
  only where both traces have one. The first checks of `wf_ch5`, `ws_ch5`, `wf_ch6` to `wf_ch8`, `wf_e9`
  and `wf_e11` had more windows, so their PC lag replays were run again with the windows joined to 64
  (every GBA frame inside them, checked) and compared again; that second compare had the PC's raw dump
  but not the GBA's (deleted after the first MATCH), so a masked field would show unmasked (only `wf_ch8`'s
  Castle Frelia OL-5 ticks did).

## Files

| File | Start | Ticks | CRC-32 | Stop | Ticks with every frame | GBA frames compared | Lag replay vs GBA (every traced field, every tick) | Raster-timing frames labelled | Notes |
|---|---|---|---|---|---|---|---|---|---|
| `wf_ch5.keys` | `power-on` | 220,177 | `14795d4f` | reached pp:5 | 203,481 (92.4%) | 203,759 | DIFFERENT | 902 | The opening's 44 rotated frames 1,970-2,016: OL-2 oracle-limit frames, proved by the affine replay (above). 4,710: one narration frame reported, the full stop of "The continent of Magvel." (F1). Castle Frelia's 6 OL-5 ticks masked. Checked again at 23:55 with the battle and menu windows too (203,759 frames; the first check, scene kinds only, 86,678 frames, the same result). |
| `ws_ch5.keys` | `power-on` | 423,873 | `1939c93a` | reached pp:5 | 407,220 (96.1%) | 407,496 | DIFFERENT | 491 | The same 44 OL-2 frames. 8,240: the same narration frame (F1). Castle Frelia's OL-5 masked. Checked again at 01:14 with the battle and menu windows too (407,496 frames; the first check, scene kinds only, 290,482 frames, the same result). |
| `wf_ch5x.keys` | `ch5x_start.sav` | 52,928 | `cdcf76f0` | passed save:5 | 40,959 (77.4%) | 41,158 | MATCH | 78 | Made in the first sitting; checked again with the battle and menu windows (the first check, scene kinds only, MATCH too). |
| `ws_ch5x.keys` | `ch5x_start.sav` | 84,745 | `305a2e46` | passed save:5 | 72,793 (85.9%) | 72,993 | MATCH | 17 | The first sitting's slow file (an earlier pace, `ws_ch5x_t300`, MATCH, not committed) made again with the committed pace. |
| `wf_ch6.keys` | `ch6_start.sav` | 51,079 | `4707f317` | passed save:7 | 41,564 (81.4%) | 40,879 | MATCH | 92 | Checked again with every window (H1; the second check without gba.raw, so no mask could apply: none was needed). |
| `ws_ch6.keys` | `ch6_start.sav` | 88,085 | `513fbaef` | passed save:7 | 78,576 (89.2%) | 78,732 | MATCH | 12 |  |
| `wf_ch7.keys` | `ch7_start.sav` | 46,031 | `68579d08` | passed save:8 | 34,226 (74.4%) | 30,648 | MATCH | 20 | Checked again with every window (H1), as wf_ch6. |
| `ws_ch7.keys` | `ch7_start.sav` | 57,194 | `55993795` | passed save:8 | 45,390 (79.4%) | 45,584 | MATCH | 12 |  |
| `wf_ch8.keys` | `ch8_start.sav` | 115,900 | `2734b0a8` | passed saveto:10 | 101,808 (87.8%) | 81,589 | DIFFERENT | 197 | First check MATCH with the raw dumps (Castle Frelia's OL-5 masked at 115,526-115,531). Checked again with every window (H1) without gba.raw: those 6 OL-5 ticks show unmasked, nothing else differs. |
| `ws_ch8.keys` | `ch8_start.sav` | 211,624 | `d5627a0c` | passed saveto:10 | 197,543 (93.3%) | 197,781 | MATCH | 110 |  |
| `wf_e9.keys` | `e9_start.sav` | 96,724 | `49bcf62e` | passed saveto:11 | 80,762 (83.5%) | 67,280 | MATCH | 82 | Checked again with every window (H1), as wf_ch6. |
| `ws_e9.keys` | `e9_start.sav` | 137,857 | `27fa2d44` | passed saveto:11 | 121,914 (88.4%) | 122,182 | MATCH | 67 |  |
| `wf_e10l.keys` | `e10_start.sav` | 94,681 | `b7aba715` | game over: lord (char 01) died in chapter 11, turn 15 | 74,735 (78.9%) | 75,067 | MATCH | 136 | Chapter 10 watched to the bot's loss (Eirika dies on turn 15, the same with pace seeds 2-6 and lord settings ROUTE_LORD_TH 3-20, ROUTE_LORD_DD 1): the opening, the world-map scene and 15 turns watched. |
| `ws_e10l.keys` | `e10_start.sav` | 139,021 | `ebb9af35` | game over: lord (char 01) died in chapter 11, turn 15 | 119,096 (85.7%) | 119,431 | MATCH | 50 | Slow Chapter 10 to the same loss (Eirika dies on turn 15). |
| `wf_e10t.keys` | `e10_start.sav` | 113,499 | `c1075f90` | passed saveto:61 | 92,155 (81.2%) | 92,516 | MATCH | 208 | Chapter 10's tail: `route_e10.keys` (frame tick rule) open-loop to tick 101,100 (`ROUTE_PREFIX`, its last enemy phase), then the bot watching the boss's battle quotes, the ending and Chapter 11's opening. |
| `ws_e10t.keys` | `e10_start.sav` | 137,828 | `1a42cc66` | passed saveto:61 | 116,484 (84.5%) | 116,843 | MATCH | 147 | Slow tail of Chapter 10, from the same prefix as wf_e10t. |
| `wf_e11.keys` | `e11_start.sav` | 97,949 | `baa14caf` | passed saveto:12 | 84,622 (86.4%) | 73,043 | MATCH | 94 | Checked again with every window (H1), as wf_ch6. |
| `ws_e11.keys` | `e11_start.sav` | 141,440 | `1dc14173` | passed saveto:12 | 128,074 (90.6%) | 128,296 | MATCH | 67 |  |
| `wf_e12.keys` | `e12_start.sav` | 88,516 | `f97134a8` | passed saveto:13 | 73,641 (83.2%) | 73,889 | MATCH | 78 |  |
| `ws_e12.keys` | `e12_start.sav` | 115,271 | `37ba1493` | passed saveto:13 | 100,423 (87.1%) | 100,672 | DIFFERENT | 33 | 84,347: one frame reported, the F2 kind (the first tick of a map battle after menus, 4 GBA lag VBlanks at events 4, 4, 4, 20; "nostores"); every traced field equal. |
| `wf_e13.keys` | `e13_start.sav` | 104,351 | `2d974244` | passed saveto:14 | 87,001 (83.4%) | 87,294 | MATCH | 172 |  |
| `ws_e13.keys` | `e13_start.sav` | 143,951 | `6e07f6bb` | passed saveto:14 | 126,615 (88.0%) | 126,900 | MATCH | 97 |  |
| `wf_e14.keys` | `e14_start.sav` | 120,333 | `2bb1bce8` | passed saveto:15 | 104,986 (87.2%) | 105,240 | MATCH | 297 |  |
| `ws_e14.keys` | `e14_start.sav` | 171,375 | `8e9d5a48` | passed saveto:15 | 156,051 (91.1%) | 156,306 | MATCH | 194 |  |
| `wf_e15.keys` | `e15_start.sav` | 164,711 | `4cd171fe` | passed saveto:16 | 146,080 (88.7%) | 146,396 | DIFFERENT | 322 | 16,930: one frame reported (F2, coverage 11's "frame on a GBA lag tick" kind): the first frame of Natasha's staff, after a tick whose main loop ran over 5 GBA VBlanks (the lag replay has them at the GBA's positions: events 4, 4, 4, 4, 27; `OnVBlank` flushes nothing while `main_loop_ended` is 0, and the main loop ends at line 16 of the frame it waits for); the GBA's picture is darkened all over (as a darken of about Y 3), the PC's is not; every traced field equal, no watched store in the GBA's frame ("nostores"); the next frame equal. Snapshot `~/fe8-snapshots/c12-se-e15bat/tick16925` (reproduces it). |
| `ws_e15.keys` | `e15_start.sav` | 201,604 | `28bf6d02` | passed saveto:16 | 182,978 (90.8%) | 183,291 | DIFFERENT | 253 | 28,694: one frame reported, the F2 kind (a staff's first frame after 5 GBA lag VBlanks, events 4, 4, 4, 4, 18; the same shadow change); every traced field equal. |
| `wf_e16.keys` | `e16_start.sav` | 116,266 | `e374e3b2` | passed saveto:17 | 94,085 (80.9%) | 94,457 | DIFFERENT | 203 | 107,608: one frame reported (F3, the same "frame on a GBA lag tick" kind): a light effect in an event of Chapter 16 (index 16), on a tick with one GBA lag VBlank (event 5; the lag replay has it there): the PC's picture lacks the light layer on lines 0 to about 80 (the throne room shows through), the GBA's has it on every line (its frame has 227 HBlank handler calls, `BraceletSealBreakFx_OnHBlank` storing BG2VOFS); every traced field equal, "nostores"; the next frame equal. Snapshot `~/fe8-snapshots/c12-se-e16lag/tick107603` (reproduces it). |
| `ws_e16.keys` | `e16_start.sav` | 161,558 | `7050c41c` | passed saveto:17 | 139,395 (86.3%) | 139,769 | DIFFERENT | 130 | 137,351: one frame reported, F3 again (the same Chapter 16 light effect, a GBA lag VBlank at event 5; the GBA's frame repeats the tick before, the PC's does not); every traced field equal. |
| `wf_e17.keys` | `e17_start.sav` | 136,833 | `1bb80f38` | passed saveto:18 | 117,914 (86.2%) | 118,227 | MATCH | 421 |  |
| `ws_e17.keys` | `e17_start.sav` | 181,233 | `8eaeae1d` | passed saveto:18 | 162,324 (89.6%) | 162,640 | MATCH | 325 |  |
| `wf_e18.keys` | `e18_start.sav` | 103,636 | `24384fae` | passed saveto:19 | 80,632 (77.8%) | 81,018 | DIFFERENT | 516 | 31,258: one frame reported, the F2 kind again (a staff's first frame after a tick with 4 GBA lag VBlanks, events 4, 4, 4, 20; the same shadow change, darken Y 4). 90,364-90,377: **gPaletteBuffer differs** (F4, coverage 11's N-C11-1 kind: the same palette 9 colours 12-15, the lag replay's limit): 4 colours of BG palette 9 (entries 0x9C-0x9F, bytes +0x138-0x13F) for 14 ticks, after a 2-tick event and a map change on ticks with GBA lag VBlanks (90,364: 1; 90,365: 4), then equal again; frames 90,365 and 90,366 differ with it ("slots": the line events differ). Snapshot `~/fe8-snapshots/c12-se-e18pal/tick90358`. |
| `ws_e18.keys` | `e18_start.sav` | 129,890 | `46203e6e` | passed saveto:19 | 106,963 (82.3%) | 107,344 | MATCH | 464 |  |
| `wf_e19.keys` | `e19_start.sav` | 165,130 | `9dd7dee3` | passed saveto:20 | 143,983 (87.2%) | 144,336 | MATCH | 251 |  |
| `ws_e19.keys` | `e19_start.sav` | 198,337 | `77946357` | passed saveto:20 | 177,199 (89.3%) | 177,550 | MATCH | 204 |  |
| `wf_e20l.keys` | `e20_start.sav` | 123,901 | `45257619` | game over: lord (char 0f) died in chapter 20, turn 26 | 105,908 (85.5%) | 106,212 | MATCH | 369 | Chapter 20 watched without the turn search (with it, the search cycled between turns 46 and 47 for 3 hours, every policy of turn 47 losing a lord, and was stopped: no key file): Ephraim (char 0f) dies on turn 26; the opening and 26 turns watched. |
| `ws_e20l.keys` | `e20_start.sav` | 146,131 | `5f2d301f` | game over: lord (char 0f) died in chapter 20, turn 26 | 128,146 (87.7%) | 128,444 | MATCH | 343 | Slow Chapter 20 without the turn search, to the same loss (Ephraim dies on turn 26). |
| `wf_e20t.keys` | `e20_start.sav` | 186,971 | `0e265775` | passed saveto:21 | 167,292 (89.5%) | 167,622 | MATCH | 717 | Chapter 20's tail: `route_e20.keys` open-loop to tick 180,900 (`ROUTE_PREFIX`; the search starts after it), then the bot watching the boss's quote and battles, the ending and the final chapter's title. |
| `ws_e20t.keys` | `e20_start.sav` | 196,088 | `082df595` | passed saveto:21 | 176,409 (90.0%) | 176,739 | MATCH | 700 | Slow tail of Chapter 20, from the same prefix as wf_e20t. |
| `wf_e21s.keys` | `e21_start.sav` | 78,787 | `c17c8e16` | stuck: chapter 21 past turn 60 | 63,197 (80.2%) | 63,457 | MATCH | 344 | The final chapter's part 1 (index 21) watched until the bot stalls (15 enemies left; it ends its turns from turn 16 to 60, "stuck: chapter 21 past turn 60"): the opening and 15 turns watched. Watching left the RNs one step behind `route_e21.keys` at turn 1 (d086 12e2 d94a against 12e2 d94a 4008). |
| `ws_e21s.keys` | `e21_start.sav` | 91,294 | `d99fe15c` | stuck: chapter 21 past turn 60 | 75,717 (82.9%) | 75,976 | MATCH | 324 | Slow part 1 to the same stall (turn 60). |
| `wf_e21t.keys` | `e21_start.sav` | 66,798 | `ef05429e` | reached pp:22 | 53,226 (79.7%) | 53,451 | DIFFERENT | 30 | Part 1's tail: `route_e21.keys` open-loop to tick 58,310 (`ROUTE_PREFIX`; the search starts after it), then the bot watching the boss's quotes and battle, the ending and part 2's opening to its preparations. 59,364-59,420: 6 frames reported (F5, the "frame on a GBA lag tick" kind, as coverage 11's `route_i21` 62,520): Lyon's Naglfar against Myrrh (`EfxMagicHBlank_805B750` on every line) has a GBA lag VBlank every 4th tick (event 23, the lag replay equal); on 6 of those 16 the redraw with the GBA's timing is not the GBA's frame ("something besides the timing differs"); every traced field equal. Snapshot `~/fe8-snapshots/c12-se-e21spell/tick59355`. |
| `ws_e21t.keys` | `e21_start.sav` | 79,553 | `aca1b767` | reached pp:22 | 65,981 (82.9%) | 66,206 | DIFFERENT | 10 | Slow tail of part 1, from the same prefix as wf_e21t: the same 6 Naglfar frames (F5) at 59,959-60,015. |
| `wf_e22.keys` | `e22_start.sav` | 69,188 | `64b04b48` | passed title:300 after the ending | 60,374 (87.3%) | 60,524 | MATCH | 212 |  |
| `ws_e22.keys` | `e22_start.sav` | 74,749 | `70f4b5a4` | passed title:300 after the ending | 65,939 (88.2%) | 66,088 | MATCH | 203 |  |
| `wf_eend.keys` | `e_clear.sav` | 36,950 | `8faf5286` | passed title:300 after the ending | 36,096 (97.7%) | 36,110 | MATCH | 2 | Continue on the clear save `e_clear`: the game plays the ending again (its CG text scenes, the credits, the Fin screen) to the title, watched. |
| `ws_eend.keys` | `e_clear.sav` | 39,188 | `84978b67` | passed title:300 after the ending | 38,334 (97.8%) | 38,349 | MATCH | 1 |  |
