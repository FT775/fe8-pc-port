#ifndef GUARD_PORT_REFORGED_H
#define GUARD_PORT_REFORGED_H

/*
 * Reforged Options (port-plan/reforged-options-plan.md): the PC-only Extras
 * entry of the played builds (PORT_PLAYED_BUILD, port/include/port.h). The
 * name and the idea come from FE8 Reforged (THANK-YOU.md row 30).
 *
 * Every option is OFF by default, and all off is the GBA game. Two exceptions
 * are ON by default: fast-forward (DECISIONS.md 2026-10-28): it only acts
 * while its key is held and never changes the game; and "Original-game bug
 * fixes" (DECISIONS.md 2026-10-30, U10; the block at the end): the Riev fix acts only where the
 * original game hangs for good, but the Pierce fix also changes the pacing of every Pierce hit
 * with battle animations on (results the same), not only the hits that would hang. The values live
 * in a plain-text settings file in Platform_UserDir (port/reforged/reforged_settings.c),
 * never in SRAM, so the .sav stays mGBA's. Test builds never call any of this:
 * every caller in the game code is under #if defined(PC_PORT) && PORT_PLAYED_BUILD.
 * With an option OFF, its hooks do nothing, so the game is the GBA's. The options in
 * sDisplayOnly (reforged_settings.c) never mark a save: the display-only ones change only the
 * picture or the speed, never a game value; the bug fixes are listed there too (the reason is there).
 */

enum ReforgedOption {
    RFO_DANGER_ZONE_SELECT, /* "Danger zone on Select" */
    RFO_FAST_FORWARD,       /* "Fast-forward" */
    RFO_UNBREAKABLE,        /* "Unbreakable weapons" */
    RFO_CASUAL_MODE,        /* "Casual mode" */
    /* fast-forward block (port/reforged/reforged_fastforward.c) */
    RFO_FF_SPEED,           /* "Fast-forward speed": 2, 3, 4 (default) or 8 */
    RFO_FF_SOUND,           /* "Fast-forward sound": 0 mute (default), 1 sped up */
    /* growth-rates block (port/reforged/reforged_growths.c), display only */
    RFO_GROWTH_SELECT,      /* "Growth rates on Select": the stat screen's Select shows growths */
    /* original-game bug fixes block (port/reforged/reforged_settings.c), ON (1) by default */
    RFO_BUG_FIXES,          /* "Original-game bug fixes": 1 on (the default), 0 the original's hangs */
    /* map HP bars block (port/reforged/reforged_hpbars.c) */
    RFO_MAP_HPBARS,         /* "HP bars & warnings": RFO_HPBARS_* (0 off, the default); display-only */

    /* --- Cheats submenu (port/reforged/reforged_cheats*.c), each OFF (0) by default --- */
    RFO_CHEAT_GOLD,         /* "Infinite gold": 0 off, 1 on */
    RFO_CHEAT_GROWTHS,      /* "Growths": 0 off, 1 fixed, 2 boosted +25%, 3 boosted +50% */
    RFO_CHEAT_EXP,          /* "EXP gained": 0 x1, 1 x2, 2 x3 */
    /* --- end of the Cheats block --- */

    RFO_COUNT
};

/* A switch is 0 (off) or 1 (on); RFO_UNBREAKABLE: 0 off, 1 your units (blue),
   2 everyone; RFO_FF_SPEED 2, 3, 4 or 8; RFO_MAP_HPBARS 0 to 3 (RFO_HPBARS_*,
   below); the cheats as listed above.
   An id out of range reads 0. Reforged_OptionMax: the largest value. */
int Reforged_OptionMax(int id);
int Reforged_GetOption(int id);
/* Sets a value in memory; Reforged_SaveSettings writes the file. */
void Reforged_SetOption(int id, int value);
/* The settings file's key for an option ("danger_zone_select"), or NULL. */
const char *Reforged_OptionKey(int id);

/* Reads the settings file. A missing or unreadable file, a missing key or a
   bad value leaves that option at its default (Reforged_OptionDefault: OFF,
   but the fast-forward block's and RFO_BUG_FIXES). Never writes. */
void Reforged_LoadSettings(void);
/* Writes every option to the settings file, atomically (a temporary file
   renamed over it, as the save does). 0 on success, -1 (logged) on failure. */
int Reforged_SaveSettings(void);

/* The options screen, a blocking child of the save menu's proc (as the Sound
   Room is). port/reforged/reforged_menu.c. */
void *StartReforgedOptionsScreen(void *parent);

/* "Danger zone on Select" (port/reforged/reforged_dangerzone.c), called from
   src/playerphase.c: the Select key of the unit movement screen, whether the
   zone is shown now, and a reset when the screen starts. */
void Reforged_DangerZoneSelect(void);
/* "Unbreakable weapons" (port/reforged/reforged_items.c): GetItemAfterUse for
   an item whose owner is known; blue units' weapons and staves keep max uses. */
struct Unit;
unsigned short Reforged_ItemAfterUse(int item, struct Unit *owner);
/* The uses the battle forecast counts hits from (src/bksel.c): a weapon the
   option refills counts as unlimited; anything else, GetItemUses. */
int Reforged_ForecastUses(int item, struct Unit *owner);

/* The same in map target selection (src/uiselecttarget.c TargetSelection_Loop). */
void Reforged_DangerZoneTargetSelect(void);
int Reforged_DangerZoneShown(void);
void Reforged_DangerZoneReset(void);
/* While the zone is shown in the movement screen: the path arrow follows the
   cursor on the unit's own maps (not drawn), in place of DrawUpdatedPathArrow. */
void Reforged_DangerZonePathArrow(void);
/* src/bmmap.c: after a RenderBmMap with the camera between squares, draws the
   16th column / 11th row the retail scroll step drew (RenderBmMapColumn(15) /
   RenderBmMapLine(10)). */
void Reforged_RenderBmMapEdges(void);

/* Fast-forward (port/reforged/reforged_fastforward.c), called only by the
   window builds' platform layer (port/sdl3/platform_sdl3.c, played builds).
   Frame: once per frame step, after the pacer, with the key's state, the
   stream's queued input frames and the real clock; 1 when the stream's multiplier moved (the platform then sets
   the stream's ratio again). Clock / RealTime: the platform clock the pacer
   sees (N times real time while held) and back. Present: 0 for a frame not
   to show. AudioRatio: the stream ratio for audio_out's own (0: the last one
   again). Queued: the stream's queued frames as audio_out counts them (divided
   by the multiplier). Gain: the mute ramp; 1 when it wrote `out`, 0 to queue
   `in` as it is. */
int Reforged_FastForwardFrame(int keyHeld, unsigned queued, unsigned long long realNs);
unsigned long long Reforged_FastForwardClock(unsigned long long realNs);
unsigned long long Reforged_FastForwardRealTime(unsigned long long clockNs);
int Reforged_FastForwardPresent(void);
double Reforged_FastForwardAudioRatio(double base);
unsigned Reforged_FastForwardQueued(unsigned frames);
/* FE8PC_FF_CHECK only: the device's request (SDL's get callback, on the
   audio thread) found the stream `additional` bytes short of `total`. */
void Reforged_FastForwardStreamShort(int additional, int total);
int Reforged_FastForwardGain(const short *in, short *out, unsigned frames);

/* --- Casual mode and saves marked (port-rfo-casual) --- */
/* 1 when an option changes the game (a save made with it on is marked):
   every option but the display-only ones (reforged_settings.c). */
int Reforged_OptionIsGameplay(int id);
/* An option's default value (0 for all but the fast-forward block and RFO_BUG_FIXES); a save is
   marked for a gameplay option not at its default. */
int Reforged_OptionDefault(int id);
/* "Casual mode" (port/reforged/reforged_casual.c). Reforged_CasualRetreat: in
   place of UnitKill for a combat or hazard death (bmmind.c, bmusailment.c);
   1 when the unit retreated (dead for this chapter, supports kept, on the
   fallen list), 0 when the caller's UnitKill must run. ChapterChangeUnitCleanup
   (bmio.c) calls Reforged_CasualReviveFallen first. */
int Reforged_CasualRetreat(struct Unit *unit);
void Reforged_CasualReviveFallen(void);
int Reforged_CasualGetFallen(unsigned char *out, int max);
void Reforged_CasualSetFallen(const unsigned char *pids, int count);
void Reforged_CasualTestFall(void);
/* The save mark sidecar <sav>.reforged (port/reforged/reforged_savemark.c).
   src/bmsave.c reports each save block (SAVE_ID_*) it writes, reads, copies
   or clears; port/save/savefile_pc.c loads the mark at start (loadedOther: 1
   when the save was loaded from the other place, so the first write writes
   the sidecar beside the new .sav) and rewrites it just before each .sav
   write (PORT_RFO_SAVEMARK, played builds only). */
void Reforged_SaveBlockWritten(int block);
void Reforged_SaveBlockRead(int block);
void Reforged_SaveBlockCopied(int from, int to);
void Reforged_SaveBlockCleared(int block);
void Reforged_SaveMarkLoad(const char *savPath, int loadedOther);
void Reforged_SaveMarkWritten(const char *savPath);

/* --- Growth rates on Select (port/reforged/reforged_growths.c), called from
   src/statscreen.c: StartStatScreen (Reset), DisplayPage after the page is
   drawn (PageDrawn) and before (PageWillDraw), StatScreen_OnIdle's Select
   when no retail key acted (Select), StatScreen_OnClose (Close). Display
   only. --- */
void Reforged_GrowthReset(void);
void Reforged_GrowthPageWillDraw(int pageid);
void Reforged_GrowthPageDrawn(int pageid);
void Reforged_GrowthSelect(void);
void Reforged_GrowthClose(void);
/* src/bm.c: OnMain after its procs (MainEnd) and OnVBlank right after
   FlushBackgrounds (AfterBgSync): the view's VRAM glyphs and bar tiles change
   with the tilemap that uses them, one VBlank after the OnMain that drew them. */
void Reforged_GrowthMainEnd(void);
void Reforged_GrowthAfterBgSync(void);

/* --- Cheats submenu (port/reforged/reforged_cheats.c, reforged_cheats_menu.c) --- */
/* (Reforged_OptionIsGameplay: the casual block's, above; the cheats are gameplay options.) */
/* The Cheats screen, a blocking child of the options screen's proc; on its
   way out it calls Reforged_MenuRedraw(parent) to draw the options screen again. */
void *StartReforgedCheatsScreen(void *parent);
void Reforged_MenuRedraw(void *menuProc);
/* src/bmgold.c GetPartyGoldAmount (not in chapter index 5): "Infinite gold"
   tops the party's gold back up to 999,999. */
void Reforged_CheatGoldRefill(void);
/* src/bmbattle.c CheckBattleUnitLevelUp: 1 when the "Growths" cheat is on and
   this did the level-up check in place of the retail code, else 0. */
struct BattleUnit;
int Reforged_CheatLevelUp(struct BattleUnit *bu);
/* src/bmbattle.c: "EXP gained" x2/x3 on a battle's EXP (after
   ModifyUnitSpecialExp), on staff EXP (a hit, not a miss), on dance and other
   action EXP: 0 stays 0, at most 100. */
int Reforged_CheatBattleExp(int exp);
int Reforged_CheatStaffExp(int exp);
int Reforged_CheatActionExp(int exp);
/* --- end of the Cheats block --- */

/* --- Map HP bars & warnings (port/reforged/reforged_hpbars.c) ---
   Display only: drawn by the software renderer as extra OBJ pixels; nothing
   is written to GBA memory (no OAM, VRAM or palette entry is used).
   src/bmudisp.c PutUnitSpritesOam calls Reforged_MapHpBarsCollect first (it
   reads the unit sprites it is about to push and works out the pictures);
   src/bm.c OnVBlank calls Reforged_MapHpBarsVBlank right after SyncHiOam, so
   the pictures shown are those of the sprites that OAM copy shows.
   port/render/obj_line.c (played builds only, PORT_RFO_MAPOVERLAY) draws
   gReforgedOverlay: each owner is one unit's map sprite, found in OAM by its
   three attribute words; its 16x16 picture covers the unit's square and is
   drawn over that sprite's own pixels and over pixels no sprite drew, never
   over another sprite's, with the sprite's priority (so BG0/BG1 windows hide
   it as they hide the sprite). Plain types only: obj_line.c is built with
   the port layer's flags. */
/* RFO_MAP_HPBARS's values (port-rfo-int6; settings key map_hp_bars_warnings):
   bit 0 the bars, bit 1 the "!" warnings, chosen apart (the community's
   convention, port-plan/reforged-qol-research.md "Map HP bars and '!'
   warnings"). */
#define RFO_HPBARS_OFF      0
#define RFO_HPBARS_BARS     1
#define RFO_HPBARS_WARNINGS 2
#define RFO_HPBARS_BOTH     3
#define RFO_OVERLAY_MAX 100 /* gSMSHandleArray's size (src/bmudisp.c) */
#define RFO_OVERLAY_OPAQUE 0x8000 /* in a picture pixel: drawn (bits 0-14: BGR555) */
struct ReforgedOverlayOwner {
    unsigned short a0, a1, a2; /* the sprite's OAM attributes (as PutOamHi writes them) */
    signed char dx, dy;        /* the unit's square inside the sprite's box */
    unsigned short pix[16][16]; /* [row][column] of the square; 0 = not drawn */
};
struct ReforgedOverlay {
    int count;
    unsigned frame; /* gPortFrameCount when it was made the shown one: the renderer
                       drops a list more than 2 frame steps old (OnVBlank no
                       longer runs, e.g. in a battle animation) */
    struct ReforgedOverlayOwner owner[RFO_OVERLAY_MAX];
};
extern struct ReforgedOverlay gReforgedOverlay; /* the one shown now */
void Reforged_MapHpBarsCollect(void);
void Reforged_MapHpBarsVBlank(void);
/* --- end of the map HP bars block --- */

/* --- Original-game bug fixes (port/reforged/reforged_settings.c; RFO_BUG_FIXES, on by default) ---
   Fixes for retail bugs that stop the game for good (a freeze or a softlock), each reproduced on the GBA
   (mGBA, the retail-built ROM) and on the PC first (port-plan/pending/retailfix.md). Each returns 1 when
   the fix is to act (the option on; logged), 0 for the original game's code. The game code calls them
   through include/event.h's EVC_ERROR_UNIT_GONE and include/anime.h's BANIM_PIERCE_KEEP_RIGHT_BEGIN/END,
   which are the original code in every other build.
   - Reforged_FixEventUnitGone: an event command (0x26 camera, 0x3B cursor) points at character pid, who
     is not on the map (Riev killed on Chapter 20's turn 1): skip the command instead of running it again
     for ever.
   - Reforged_FixPierceFreeze: Pierce's C07 branch in a battle animation (src/banim-main.c): act as Sure Shot's
     (the right-hand anims' state3 and state put back after the original's four statements), on every Pierce
     strike, so it also paces hits that never hang (results, RNG and save the same apart from the play clock). */
int Reforged_FixEventUnitGone(int cmd, int pid);
int Reforged_FixPierceFreeze(void);
/* --- end of the original-game bug fixes block --- */

#endif /* GUARD_PORT_REFORGED_H */
