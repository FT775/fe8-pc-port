# Coverage 12 "scenes-i": Ephraim's route with every story scene watched, and the Extras support viewer

Bot output (never TAS-derived, never hand-edited). The Eirika side of this round, and the tools these
files are made and checked with, are `README.md` in this folder (port-c12-scenes-e: `route_bot.lua`'s
`ROUTE_WATCH`, `route_check.lua`'s `ROUTE_SCENES_OUT`, `tools/scene_shots.py`, `tools/frame_prehash.py`,
`make watch-route`). Results and findings: `port-plan/pending/c12-scenes-i.md`.

## Ephraim's route watched (`wf_*` fast, `ws_*` slow)

Each file plays the route file it repeats (`inputs/route_<name>.keys`) again from the same start save and
with the same settings (its `# Generated` line), plus `ROUTE_WATCH=fast` or `slow` (seed 1 unless the
header says otherwise), so no scene is skipped: Chapter 8's ending with the route split to Ephraim
(`wf_ch8i`), Ephraim's Chapters 9 to 21, the final chapter
(`wf_ib22`, the boosted army) and the ending from the cleared save (`wf_ibend`, `ib_clear`); `wf2_ch5x`
is Ephraim's Chapter 5x with `ROUTE_WATCH_SEED=2` (port-c12-scenes-e has seed 1). Three chapters are not
won when watched from the start (watching consumes random numbers, and Ephraim's chapters have no turn
search): `wf_i10`, `wf_i17` (a lord dies) and `wf_i20` (stuck past turn 60) are kept as the bot made them,
their openings watched, and `wf_i10m`, `wf_i17m` (`ROUTE_WATCH_FROM` = the route file's first free cursor,
the opening skipped as there) and `wf_i20e` (`ROUTE_WATCH_FROM=163640`, the chapter's ending) play those
chapters through. `ROUTE_WATCH_FROM=T`: the bot as without `ROUTE_WATCH` until tick T, then watching
(`route_bot.lua`, "Coverage 12"); unset, nothing changes.

    make -f port/test/Makefile watch-route WATCH=wf_i9     # every frame of every scene, frames every 60 ticks elsewhere

This round's checks compared more frames than `watch-route` does: the `wf_*` files every frame of every
scene, battle animation and menu (`route_check.lua`'s kinds opening, intro, quote, event, talk, ending,
battle, menu), the `ws_*` files every frame of every scene and menu; at most 64 ranges (the PC side takes
no more, `port/test/pc/gate0_hook.c` `sShotRanges[64]`: the closest ranges joined until 64 are left).
To run one that way by hand:

    make -f port/test/Makefile chain-save CHAIN_SAVE=i9_start
    GATE0_KEYS=port/test/inputs/watch/wf_i9.keys GATE0_SRAM_IN=port/test/saves/i9_start.sav ROUTE_TICK=capture \
      MGBA_RUN_BP="<GBA_BP of port/test/Makefile>" ROUTE_SCENES_OUT=scenes.txt \
      build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/route_check.lua 900000000
    shots=...   # the ranges of scenes.txt of those kinds, widened by 3 ticks, joined across gaps under 30, at most 64
    GATE0_SRAM_IN=port/test/saves/i9_start.sav GATE0_SHOT_TICKS=$shots make -f port/test/Makefile gba-trace KEYS=... TICKS=...
    GATE0_SRAM_IN=... GATE0_SHOT_TICKS=$shots make -f port/test/Makefile pc-lag-replay KEYS=... TICKS=... PC_FAULT_OK=1
    python3 port/test/tools/frame_prehash.py build/gate0 --jobs 4
    GATE0_SRAM_IN=... make -f port/test/Makefile compare-lag-replay KEYS=... TICKS=...

| File | Start (FNV-1a) | Ticks | Stop (notable settings besides ROUTE_WATCH) | Every-frame share | PC check (plain PC, lag replay) |
|---|---|---|---|---|---|
| `wf_ch8i.keys` | `ch8_start` (6b39183a) | 115,862 | passed saveto:23 (ROUTE_SPLIT=ephraim) | 87.8% | **MATCH**; 102,006 frames on both sides, 284 raster-timing labelled |
| `wf_i9.keys` | `i9_start` (47a0668a) | 138,590 | passed saveto:24 | 89.0% | **MATCH**; 123,646 frames on both sides, 429 raster-timing labelled |
| `wf_i10.keys` | `i10_start` (7753e267) | 75,511 | game over: Ephraim (0x0F) dies in chapter 24, turn 9 | 77.0% | **MATCH**; 58,456 frames on both sides, 156 raster-timing labelled |
| `wf_i10m.keys` | `i10_start` (7753e267) | 82,760 | passed saveto:62 (ROUTE_WATCH_FROM=4584) | 78.2% | **MATCH**; 65,013 frames on both sides, 205 raster-timing labelled |
| `wf_i11.keys` | `i11_start` (63d6cfbb) | 78,281 | passed saveto:25 | 84.4% | **MATCH**; 66,252 frames on both sides, 68 raster-timing labelled |
| `wf_i12.keys` | `i12_start` (248af190) | 86,207 | passed saveto:26 | 84.0% | **DIFFERENT** (first 57,306; F-c12-si-2); 72,652 frames on both sides, 105 raster-timing labelled |
| `wf_i13.keys` | `i13_start` (c65824de) | 141,607 | passed saveto:27 | 88.1% | **DIFFERENT** (first 40,062; F-c12-si-4); 125,064 frames on both sides, 248 raster-timing labelled |
| `wf_i14.keys` | `i14_start` (83286ce8) | 168,922 | passed saveto:28 | 87.7% | **MATCH**; 148,573 frames on both sides, 458 raster-timing labelled |
| `wf_i15.keys` | `i15_start` (80a97b37) | 159,886 | passed saveto:29 | 89.8% | **MATCH**; 143,911 frames on both sides, 307 raster-timing labelled |
| `wf_i16.keys` | `i16_start` (86c72e1b) | 133,229 | passed saveto:30 | 84.2% | **DIFFERENT** (first 5,437; F-c12-si-5, F-c12-si-4); 112,565 frames on both sides, 271 raster-timing labelled |
| `wf_i17.keys` | `i17_start` (4117ac5f) | 105,631 | game over: Eirika (0x01) dies in chapter 30, turn 10 | 80.3% | **DIFFERENT** (first 6,855; F-c12-si-3); 85,204 frames on both sides, 117 raster-timing labelled |
| `wf_i17m.keys` | `i17_start` (4117ac5f) | 130,140 | passed saveto:31 (ROUTE_WATCH_FROM=6263) | 81.7% | **MATCH**; 106,716 frames on both sides, 83 raster-timing labelled |
| `wf_i18.keys` | `i18_start` (6fb1dc12) | 56,667 | passed saveto:32 | 66.3% | **MATCH**; 37,879 frames on both sides, 99 raster-timing labelled |
| `wf_i19.keys` | `i19_start` (c11d85bb) | 152,223 | passed saveto:33 | 85.0% | **MATCH**; 129,784 frames on both sides, 264 raster-timing labelled |
| `wf_i20.keys` | `i20_start` (3f60ce37) | 179,269 | stuck: chapter 33 past turn 60 | 90.7% | **MATCH**; 162,872 frames on both sides, 787 raster-timing labelled |
| `wf_i20e.keys` | `i20_start` (3f60ce37) | 171,961 | passed saveto:34 (ROUTE_WATCH_FROM=163640) | 90.5% | **MATCH**; 155,901 frames on both sides, 592 raster-timing labelled |
| `wf_i21.keys` | `i21_start` (99fb8d28) | 86,790 | reached pp:35 (ROUTE_SEARCH=1) | 81.2% | **DIFFERENT** (first 63,129; F-c12-si-4); 70,782 frames on both sides, 159 raster-timing labelled |
| `wf_ib22.keys` | `ib22_start` (a1d7ce75) | 61,058 | passed title:300 after the ending (ROUTE_SEARCH=1) | 90.0% | **MATCH**; 55,067 frames on both sides, 193 raster-timing labelled |
| `wf_ibend.keys` | `ib_clear` (05d7fd4a) | 37,855 | passed title:300 after the ending (ROUTE_SEARCH=1) | 97.7% | **MATCH**; 37,015 frames on both sides, 1 raster-timing labelled |
| `wf2_ch5x.keys` | `ch5x_start` (742bd89e) | 53,353 | passed save:5 (ROUTE_WATCH_SEED=2) | 77.6% | **MATCH**; 41,600 frames on both sides, 90 raster-timing labelled |
| `ws_ch8i.keys` | `ch8_start` (6b39183a) | 211,396 | passed saveto:23 (ROUTE_SPLIT=ephraim) | 69.7% | **MATCH**; 148,503 frames on both sides, 87 raster-timing labelled |
| `ws_i9.keys` | `i9_start` (47a0668a) | 199,767 | passed saveto:24 | 65.6% | **MATCH**; 132,124 frames on both sides, 391 raster-timing labelled |
| `ws_i15.keys` | `i15_start` (80a97b37) | 200,658 | passed saveto:29 | 51.6% | **MATCH**; 105,136 frames on both sides, 326 raster-timing labelled |
| `ws_i16.keys` | `i16_start` (86c72e1b) | 178,201 | passed saveto:30 | 51.5% | **DIFFERENT** (first 155,705; F-c12-si-4); 93,277 frames on both sides, 196 raster-timing labelled |
| `ws_ib22.keys` | `ib22_start` (a1d7ce75) | 66,949 | passed title:300 after the ending (ROUTE_SEARCH=1) | 66.5% | **MATCH**; 44,906 frames on both sides, 10 raster-timing labelled |
| `ws_ibend.keys` | `ib_clear` (05d7fd4a) | 40,376 | passed title:300 after the ending (ROUTE_SEARCH=1) | 97.9% | **MATCH**; 39,536 frames on both sides, 1 raster-timing labelled |

## The Extras menu's support viewer (`vf_support`, `vs_support`)

FE8's Extras menu has one scene viewer: **Support Conversation** (`src/uisupport.c`), which lists the
characters a save's global block knows and plays every support conversation whose rank the block
records. No bot route plays a support conversation into a cleared game, so the start is an edited copy
of the cleared save `ib_clear` (test coverage only): `port/test/tools/sram_support_edit.py` writes rank
A for every conversation into the global block's `SuppordRecord` and its checksum, nothing else.
`port/test/oracle/scene_viewer.lua` then opens Extras > Support Conversation from the title screen and
plays seven conversations on three characters' pages (`VIEW_PLAN`, default
`0.1,0.2,0.3,1.1,R,0.1,2.1,R,0.2`: partner row . rank, `R` the next character's page), A at a human's
pace (`VIEW_PACE=fast`: held 2-4 ticks, 4-14 between; `slow`: held 3-7, 40-120 between).

    make -f port/test/Makefile chain-save CHAIN_SAVE=ib_clear
    python3 port/test/tools/sram_support_edit.py port/test/saves/ib_clear.sav port/test/saves/sv_clear.sav
    GATE0_SRAM_IN=port/test/saves/sv_clear.sav VIEW_OUT=vf_support.keys VIEW_PACE=fast VIEW_SEED=301 \
      MGBA_RUN_BP="<GBA_BP of port/test/Makefile>" \
      build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/scene_viewer.lua 1000000   # slow: VIEW_SEED=302
    GATE0_SRAM_IN=port/test/saves/sv_clear.sav GATE0_SHOT_TICKS=0-11449 make -f port/test/Makefile gba-trace KEYS=port/test/inputs/watch/vf_support.keys TICKS=11450
    (then pc-lag-replay and compare-lag-replay the same way; vs_support: 37,994 ticks)

| File | Start | Ticks | Conversations | PC check (plain PC, lag replay, every frame of the file) |
|---|---|---|---|---|
| `vf_support.keys` | `sv_clear` (FNV-1a e7eaaa7a) | 11,450 | 7: the first page's partner 1 at C, B and A and partner 2 at C, the second page's partners 1 and 3 at C, the third page's partner 1 at B | MATCH on ticks 0-11,449; 11,450 frames compared, 105 raster-timing frames labelled |
| `vs_support.keys` | `sv_clear` | 37,994 | the same 7, slow | MATCH on ticks 0-37,993; 37,994 frames, 44 raster-timing |

Both files were made again with the committed `scene_viewer.lua` and `sram_support_edit.py` (the edited
save byte for byte): the same keys and header but its README line (line 4, changed when the files moved
here). The PC checks are of the first attempt's files (the same keys), every frame of the whole file.

## Repro of finding F-c12-si-1 (`rp_ch5x_bmvsync`)

`rp_ch5x_bmvsync.keys` is Chapter 5x (Ephraim's) from `ch5x_start` with every scene watched, made by the
first attempt of this shard with its own `ROUTE_WATCH` (replaced by port-c12-scenes-e's before any file
was committed, so the committed bot does not make it again; it replays open-loop like any key file).
Its opening's return to the map (tick 6,024) has six GBA lag VBlanks, the last inside
`BMapVSync_InitMapAnimations`; the lag replay runs it after that function, so the map's animated water
tiles differ on frames 6,026-6,064 (`port-plan/pending/c12-scenes-i.md`, F-c12-si-1). Snapshot 6,020:
`~/fe8-snapshots/c12-si-1/`.

    make -f port/test/Makefile chain-save CHAIN_SAVE=ch5x_start
    export GATE0_SRAM_IN=port/test/saves/ch5x_start.sav GATE0_SHOT_TICKS=6000-6100
    make -f port/test/Makefile gba-trace KEYS=port/test/inputs/watch/rp_ch5x_bmvsync.keys TICKS=6200
    make -f port/test/Makefile pc-lag-replay KEYS=port/test/inputs/watch/rp_ch5x_bmvsync.keys TICKS=6200 PC_FAULT_OK=1
    make -f port/test/Makefile compare-lag-replay KEYS=port/test/inputs/watch/rp_ch5x_bmvsync.keys TICKS=6200
    # DIFFERENT: first difference at tick 6026 (frame15, "not a raster-timing frame"), 39 ticks, 6,026-6,064

From the snapshot instead (both sides resume at 6,020; the snapshot records the save by the path it was
made with): add `GATE0_STATE_IN=$HOME/fe8-snapshots/c12-si-1/tick6020 SAVESTATE_NEW_GAME_DATA=initial` to
the three steps, `GATE0_SHOT_TICKS=6020-6099`, `TICKS=6100` (checked: the same 39 frames).
