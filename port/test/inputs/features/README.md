# Coverage 12 "features": hand-made key files for menus no route opens

Hand-made key files (never TAS-derived) for game screens that no bot route and no menu
monkey file opens: the map menu's Guide, the title menu's Extras (the Sound Room, the
support conversation viewer). The `.edit` files here are the edits of the edited starts the bot's
feature routes use (`sram_boost.lua` `BOOST_EDIT_FILE`; README-routes.md "Feature routes (coverage 12)"). Each is built from its `.ks` key script with
`port/test/tools/keyscript.py` (round 10's tool, `port/test/inputs/paths/README.md`):

    python3 port/test/tools/keyscript.py port/test/inputs/features/NAME.ks port/test/inputs/features/NAME.keys

(`prefix FILE N` takes a key file's first N entries; `tap KEY GAP`, `rep N KEY GAP`, `wait N`;
`header` lines go into the key file's header). Every press comes at least 20 ticks after the
screen it is meant for was up on the GBA (checked on the GBA's screenshots), so the PC's lag
replay, which runs the GBA's lag VBlanks where the GBA had them, takes the same path.

Each was run on the GBA and on the plain PC (no NULL probe) through the lag replay, every
traced field on every tick and **every frame** of the window below (`GATE0_SHOT_TICKS` on both
sides), as `make route` does:

    GATE0_SHOT_TICKS=<window> make -f port/test/Makefile route ROUTE_KEYS=port/test/inputs/features/NAME.keys \
        ROUTE_TICKS=<ticks> GATE0_SRAM_IN=<start save>

| Key file | Start save (FNV-1a of its first 32 KiB) | Ticks | Frames compared | What it opens | Plain PC (2026-10-23, port-c12-features) |
|---|---|---|---|---|---|
| `guide_e12.keys` | `e12_start` (40dea610) | 5,722 | 1,500-5,721 (every frame) | `route_f_support_e12.keys` to Eirika's Chapter 12 first free cursor (1,544); the cursor to an empty tile, A: the map menu; Guide (1,881): every category down and up again (the list scrolls), Basic Rules' "Viewing Units" and "Game Flow" texts scrolled, Combat's topics and one text, Select (Sort) twice, back to the map (5,522) | MATCH, every traced field on every tick; 4,247 frames compared, none differ |
| `soundroom_eclear.keys` | `e_clear` (44491be0; Eirika's real-army clear save) | 7,947 | 1,650-7,946 (every frame) | `resume.keys` to the title menu, Extras, Sound Room (1,936): song 01 played (the level meter), 02, a song four rows down, two pages down to the list's end (69 songs, the locked "?????" entries), Random (Select) and Stop (Start) twice, back to the Extras menu (7,546) and the title menu | MATCH; 6,325 frames compared, the 92 that differ are all raster-timing labelled (P-17) |
| `support_viewer_sve.keys` | `sv_e_start` (c67cf1cb; below) | 9,802 | 1,900-9,801 (every frame) | `resume.keys` to the title menu (Resume Chapter, Restart Chapter, Copy Data, Erase Data, New Game, Extras), Extras (Link Arena, Sound Room, Support Conversation, Map), Support Conversation (1,998): the character list, Eirika (2,299): her partners, Ephraim's C, B and A lit; A (2,582): the A conversation watched to its end with A (83 presses, the list again at 4,658); B (4,726): the B conversation watched for 170 presses, then skipped with Start (8,698); back to the character list, Extras and the title menu (9,401) | MATCH; 7,934 frames compared, the 45 that differ are all raster-timing labelled (P-17) |

**`sv_e_start`** (never committed, like every save): the SRAM at the end of
`route_f_supports_tower_e.keys` (from `e_clear.sav`; `sram_capture.lua` with `SRAM_AT=tick:120069`).
That route takes Eirika and Ephraim's C, B and A conversations in the post-game Tower, and each one
writes the global support record (`UpdateBestGlobalSupportValue`, src/bmsave-lib.c) at once, so this
SRAM's header has their A (the game save slot is still `e_clear`'s; the title menu also offers the
Tower's suspend). The Extras menu shows Support Conversation for a played-through game that knows a
character (`IsExtraSupportViewerEnabled`); the viewer lists the conversations of that record.

    MGBA_RUN_BP=<GBA_BP of port/test/Makefile> GATE0_SRAM_IN=e_clear.sav GATE0_KEYS=port/test/inputs/route_f_supports_tower_e.keys \
      SRAM_AT=tick:120069 SRAM_OUT=sv_e_start.sav build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/sram_capture.lua 40000000
