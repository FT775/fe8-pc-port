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
    /* --- Display submenu (port/reforged/reforged_display_menu.c), display-only --- */
    RFO_WINDOW_SCALE,       /* "Window size": 0 auto [0] (FE8PC_SCALE, or 3x if it fits), 1-8 times 240x160 */
    RFO_SCALE_MODE,         /* "Scaling": 0 pixel-perfect [0] (whole numbers, black bars), 1 fill (keeps the
                               shape), 2 stretch */
    RFO_SCALE_SMOOTH,       /* "Smoothing": 0 sharp [0], 1 smooth (filtered; only for fill and stretch) */
    RFO_FULLSCREEN,         /* "Fullscreen": 0 [0], 1 (the window starts fullscreen; F11 still toggles) */
    /* "Keep game Options" (reforged_gameoptions.c; Display screen and the in-chapter screen):
       0 [0]; 1: each New Game starts with the game's Options last chosen. Display-only. */
    RFO_KEEP_GAME_OPTIONS,
    /* "Enemy phase" (reforged_enemyphase.c): the enemy and NPC phases' speed, 1 (normal), 2 or
       4 [4]; Start skips the rest of the phase. Display-only. */
    RFO_ENEMY_SPEED,
    /* "True hit" (reforged_items.c Reforged_ShownHit): the battle forecast and the battle
       scene show the real chance to hit (the game rolls two numbers and averages them), 1
       [1], or the game's own number, 0. Display-only. */
    RFO_TRUE_HIT,
    /* "Arena odds" (reforged_arena.c): the arena shows its opponent before the wager, with both sides'
       hit, damage and crit, 1 [1], or the original's window after paying, 0. Display-only. */
    RFO_ARENA_ODDS,
    /* "Skill banners" (reforged_skillbanner.c, v1.14.0): a battle skill that activates shows its name, a
       banner beside the unit's HP box in battle animations, a short popup by the unit in map animations,
       1 [1], or nothing, 0. Display-only. */
    RFO_SKILL_BANNERS,
    /* "Myrrh look" (reforged_fe7.c, v1.20.0): Myrrh's dragon form in battle animations is FE6's Divine Dragon
       (needs the player's own FE6 ROM), 1, or FE8's, 0 [0]. Display-only. */
    RFO_MYRRH_LOOK,
    /* "Helpers >" (reforged_helpers_menu.c), each 0 [0] or 1; they change play (a save is marked) */
    RFO_CONVOY_ANYWHERE,    /* "Convoy anywhere": every unit has Supply where the chapter has the convoy */
    RFO_SKIRMISH_PASS,      /* "Pass skirmishes": the world-map army walks through monster nodes */
    RFO_REWIND,             /* "Rewind" (reforged_rewind.c): hold the Rewind key to go back a turn */
    RFO_RUINS_SKIP,         /* "Ruin floor select" (reforged_ruins.c): entering the Lagdou Ruins picks the floor to start on */
    RFO_FAST_SUPPORTS,      /* "Faster supports" (reforged_supports.c): 0 off, 1 x2, 2 x3, and units nearby count */
    /* --- Game fixes submenu (port/reforged/reforged_gamefixes*.c): changes to the original's rules,
       each OFF (0) by default. Like the Randomizer's, these are the settings for the NEXT new game:
       a new game copies them into the run (Reforged_RunFix), each save records the run's values
       (the save mark's block line) and loading a save puts back its own; a save that records none
       plays with every fix off. --- */
    RFO_FIX_EARLY_EXP,      /* "Early kill EXP": a stronger kill never gives less (Prologue-Ch8) */
    RFO_FIX_PROMO_LEVELS,   /* "Promoted enemies": pre-promotion levels in the base class (harder) */
    RFO_FIX_AI_HEAL,        /* "AI healing": enemy/NPC healers skip full-HP units */
    RFO_FIX_CANTO,          /* "Canto after Trade": kept after backing out of Trade/Supply */
    RFO_FIX_DEVIL,          /* "Devil at high Luck": Luck 32+ never backfires (not 100%) */
    RFO_FIX_CH15_DROP,      /* "Ch15 staff drop": the Troubadour drops its status staff, not Mend */
    RFO_FIX_PAIR_UP,        /* "Pair Up" (reforged_pairup.c): Rescue pairs units, Fates style (v1.5.0) */
    RFO_FIX_SKILLS,         /* "Skills" (reforged_skills*.c): 0 off, 1 class, 2 class + personal (v1.6.0) */
    RFO_FIX_ENEMY_SKILLS,   /* "Enemy skills": 0 off, 1 promoted enemies and bosses, 2 all (v1.6.0) */
    RFO_FIX_SMART_AI,       /* "Smarter enemies" (reforged_gamefixes.c): regular enemies skip hopeless attacks (v1.7.0) */
    RFO_FIX_MYRRH,          /* "Myrrh buff" (reforged_gamefixes.c): her Dragonstone never breaks, Speed +2 (v1.10.0) */
    RFO_FIX_JOIN_RANKS,     /* "Late joiners' ranks" (reforged_gamefixes.c): +1 rank from Ch9, +2 from Ch13 (v1.13.0) */
    RFO_FIX_ARMED_DANCERS,  /* "Armed dancers" (reforged_gamefixes.c): Dancer light E, the FE7 Bard anima E (v1.18.0) */
    RFO_FIX_PROMO3,         /* "Third promotion" (reforged_rando_classes.c): every promotion offers three classes (v1.23.0) */
    RFO_FIX_FIRST = RFO_FIX_EARLY_EXP,
    RFO_FIX_LAST = RFO_FIX_PROMO3,
    /* --- end of the Game fixes block --- */

    /* --- Cheats submenu (port/reforged/reforged_cheats*.c), each OFF (0) by default --- */
    RFO_CHEAT_GOLD,         /* "Infinite gold": 0 off, 1 on */
    RFO_CHEAT_GROWTHS,      /* "Growths": 0 off, 1 fixed, 2 boosted +25%, 3 boosted +50% */
    RFO_CHEAT_EXP,          /* "EXP gained": 0 x1, 1 x2, 2 x3 */
    /* --- end of the Cheats block --- */

    /* --- Randomizer submenu (port/reforged/reforged_rando*.c, reforged_randomizer_menu.c) ---
       Modelled on the Universal FE Randomizer (UFER, MIT; ideas only, no code). These are the
       settings for the NEXT new game: a new game copies them (and the seed, Reforged_GetRandoSeed)
       into the run, and the run's copy is saved with each save block in the save mark
       (<blk>_rando), so a run keeps its own randomization whatever the menu says later.
       Every value is a u8. Defaults in brackets; "chance" rows are percents, 0 = off. */
    RFO_RANDO_FIRST,
    RFO_RANDO_ENABLE = RFO_RANDO_FIRST, /* "Randomizer": 0 off [0], 1 on */
    /* Stats */
    RFO_RANDO_GROWTHS,      /* "Growths": 0 off [0], 1 redistribute, 2 delta, 3 full random */
    RFO_RANDO_GROWTH_VAR,   /* "Growth variance": redistribute's variance / delta's max delta, 0-100 step 5 [30] */
    RFO_RANDO_GROWTH_MIN,   /* "Growth min": 0-100 step 5 [5] */
    RFO_RANDO_GROWTH_MAX,   /* "Growth max": 0-255 step 5 [80] */
    RFO_RANDO_GROWTH_HP,    /* "Adjust HP growths": 0 [0], 1 */
    RFO_RANDO_BASES,        /* "Bases": 0 off [0], 1 redistribute, 2 delta */
    RFO_RANDO_BASE_VAR,     /* "Base variance": 1-10 [5] (delta: max delta) */
    RFO_RANDO_MOV,          /* "Class movement": 0 [0], 1 (4-8, promoted never lower) */
    RFO_RANDO_CON,          /* "Constitution": 0 [0], 1 (variance 3, min 3) */
    RFO_RANDO_AFFINITY,     /* "Affinity": 0 [0], 1 */
    /* Classes */
    RFO_RANDO_CLASSES,      /* "Player classes": 0 [0], 1 */
    RFO_RANDO_LORDS,        /* "Include lords": 0 [0], 1 (Eirika keeps swords, Ephraim lances) */
    RFO_RANDO_THIEVES,      /* "Include thieves": 0 [0], 1 */
    RFO_RANDO_SPECIAL,      /* "Include dancer": 0 [0], 1 (Tethys; Myrrh never changes) */
    RFO_RANDO_MONSTERS,     /* "Monster classes": 0 [0], 1 (players may become monsters) */
    RFO_RANDO_EVENLY,       /* "Assign evenly": 0 [0], 1 */
    RFO_RANDO_GENDER,       /* "Match gender": 0, 1 [1] (male/female class twins) */
    RFO_RANDO_CLASS_GROWTHS,/* "Class growths": 0 keep [0], 1 transfer personal growths */
    RFO_RANDO_CLASS_BASES,  /* "Class bases": 0 retain final [0], 1 retain personal */
    RFO_RANDO_PALETTES,     /* "Keep colours": 0, 1 [1] (a character's battle colours on a new class) */
    /* Recruitment */
    RFO_RANDO_RECRUIT,      /* "Recruitment": 0 [0], 1 (who joins where is shuffled) */
    RFO_RANDO_RECRUIT_CLASS,/* "Recruit class": 0 their own [0], 1 the slot's */
    RFO_RANDO_RECRUIT_STATS,/* "Recruit stats": 0 autolevel [0], 1 the slot's */
    RFO_RANDO_RECRUIT_GENDER,/* "Cross-gender": 0 [0], 1 */
    /* Enemies */
    RFO_RANDO_BOSSES,       /* "Boss classes": 0 [0], 1 */
    RFO_RANDO_ENEMIES,      /* "Enemy classes": 0 [0], 1 (generic enemies) */
    RFO_RANDO_ENEMY_GROWTH, /* "Enemy growths": 0 off [0], 1 flat buff, 2 scaling buff */
    RFO_RANDO_ENEMY_GROWTH_AMT, /* "Enemy buff": 0-100 step 5 [10] */
    RFO_RANDO_BOSS_BUFF,    /* "Boss stats": 0 off [0], 1 linear, 2 ease in/out */
    RFO_RANDO_BOSS_BUFF_AMT,/* "Boss max boost": 1-20 [5] */
    RFO_RANDO_ENEMY_WEAPONS,/* "Better enemy weapons": chance 0-100 step 5 [0] */
    /* Items */
    RFO_RANDO_W_MIGHT,      /* "Weapon might": 0 [0], 1 (+/-3, 0-23) */
    RFO_RANDO_W_HIT,        /* "Weapon hit": 0 [0], 1 (+/-20, 55-100) */
    RFO_RANDO_W_WEIGHT,     /* "Weapon weight": 0 [0], 1 (+/-5, 2-20) */
    RFO_RANDO_W_USES,       /* "Weapon uses": 0 [0], 1 (+/-20, 15-60) */
    RFO_RANDO_W_EFFECTS,    /* "Weapon traits": chance 0-100 step 5 [0] */
    RFO_RANDO_REWARDS,      /* "Rewards": 0 [0], 1 (chests, villages, houses, gifts) */
    RFO_RANDO_DROPS,        /* "Enemy drops": chance 0-100 step 5 [0] */
    RFO_RANDO_SHOPS,        /* "Shops": 0 [0], 1 */
    RFO_RANDO_MASTER_SEALS, /* "Master Seals only": 0 [0], 1 (every promotion item a Master Seal; v1.1.4) */
    RFO_RANDO_PROMOTIONS,   /* "Random promotions": 0 [0], 1 (each class's two promotion choices; v1.4.0) */
    RFO_RANDO_MUSIC,        /* "Random music": 0 [0], 1 (map and battle themes per chapter; v1.4.0) */
    RFO_RANDO_FLOOR_MONSTERS,/* "Tower & Ruins": 0 [0], 1 (generated monsters, same tier; v1.4.0) */
    RFO_RANDO_PROMO_MONSTERS,/* "Monster promos": 0 separate [0], 1 mixed (Random promotions: monsters and others promote into each other; v1.4.0) */
    RFO_RANDO_FE7,          /* "FE7/FE6 classes" ("FE7 classes" v1.17.0-v1.20.0): 0 [0], 1 (with the player's own FE7 and/or
                               FE6 ROM, v1.21.0: rare foreign classes as promotion choices, players' classes and late
                               bosses and enemies; New Game records it and the ROMs loaded as the run's randomizer
                               version, 11 + the set: 12 FE7, 13 FE6, 14 both; 11 off or no ROM (reforged_rando.h
                               RANDO_VERSION_THREE); version 10 runs: FE7's Archsage / Dark Druid only) */
    /* Challenge */
    RFO_RANDO_NUZLOCKE,     /* "Nuzlocke": 0 [0], 1 (no casual retreat; a death stays through any reset) */
    RFO_RANDO_LAST = RFO_RANDO_NUZLOCKE,
    /* --- end of the Randomizer block --- */

    RFO_COUNT
};
#define RFO_RANDO_COUNT (RFO_RANDO_LAST - RFO_RANDO_FIRST + 1)

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

/* "Keep game Options" (RFO_KEEP_GAME_OPTIONS): the game's 13 Options rows in the screen's
   order (uiconfig.c gGameOptionsUiOrder), each the screen's value (GetGameOption), kept as
   the settings file's game_options line (reforged_settings.c). Get: 1 and out filled when
   some were remembered. */
#define RFO_GAME_OPTION_COUNT 13
int Reforged_GameOptionsGet(unsigned char *out);
void Reforged_GameOptionsSet(const unsigned char *v);
const char *Reforged_GameOptionKey(int i);
/* reforged_gameoptions.c: remember the loaded game's Options (the option on), and give a
   New Game the remembered ones (bmsave.c, after InitPlayConfig). */
int Reforged_GameOptionsCapture(void); /* -1: the settings file could not be written */
void Reforged_GameOptionsApply(void);

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
/* Select on the free map (PlayerPhase_MainIdle): every enemy's range, the
   retail game's unreached danger-zone view (player phase label 12); 1 when it
   opened (the caller returns). Then, at the top of PlayerPhase_RangeDisplayIdle,
   that view's keys (B, Select, R, the closing A); 1 when handled. */
int Reforged_DangerZoneFreeMap(void *proc);
int Reforged_DangerZoneFreeMapIdle(void *proc);
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
/* After Reforged_FastForwardFrame: 1 once when a muted fast-forward has just been released, the
   stream then to be emptied (what it holds is the muted samples) and refilled with *silence frames
   of silence, so normal sound comes back at once at normal pitch (no glide down from N times). */
int Reforged_FastForwardTakeFlush(unsigned *silence);
/* reforged_enemyphase.c: the enemy phase's speed now (1, 2, 4; 8 while skipped with Start),
   once per frame step. */
int Reforged_EnemyPhaseSpeed(void);

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
/* "Ruin floor select" (reforged_ruins.c): the best Lagdou floor ever cleared (0-10, + 0x10: the run
   being played started part-way), kept per save block by the save mark (reforged_savemark.c "<block>_ruins"). */
/* "Myrrh buff" (RFO_FIX_MYRRH, reforged_gamefixes.c): owner is Myrrh and item her Dragonstone, with the
   run's fix on (reforged_items.c: never used up, its uses hidden); her Speed bonus (0 or 2). */
int Reforged_MyrrhStone(int item, const struct Unit *owner);
int Reforged_MyrrhSpeed(const struct Unit *unit);
int Reforged_RuinsGet(void);
void Reforged_RuinsSet(int value);
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
/* The .sav the game writes (the save mark's own writes between .sav writes go beside it). */
void Reforged_SaveMarkSetWritePath(const char *savPath);

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
     strike, so it also paces hits that never hang (results, RNG and save the same apart from the play clock).
   - Reforged_FixDancerWeaponBanim (port/reforged/reforged_gamefixes.c; include/anime.h
     BANIM_DANCER_WEAPON_MAPANIM): a Dancer fighting with a weapon gets the map animation, as her dance
     animation hangs when she has two rounds in a row (results, RNG and save the same). */
int Reforged_FixEventUnitGone(int cmd, int pid);
int Reforged_FixPierceFreeze(void);
/* --- end of the original-game bug fixes block --- */

/* --- Game fixes (port/reforged/reforged_gamefixes.c; RFO_FIX_*, each off by default) ---
   The game calls them through same-line macros (include/bmbattle.h, bmunit.h, cp_common.h,
   playerphase.h; their prototypes are there). The Game fixes screen
   (reforged_gamefixes_menu.c) is a blocking child of the options screen. With FE8PC_RFO_CHECK,
   Reforged_GameFixesSelfCheck checks each on scratch units (called with the cheats' check). */
void *StartReforgedGameFixesScreen(void *parent);
void Reforged_GameFixesSelfCheck(void);
/* The run's game fixes: Reforged_RunFix(RFO_FIX_*) is the value the game being played was started
   with (0 before any game is loaded). Reforged_GameFixesNewGame (src/bmsave.c WriteNewGameSave,
   through Reforged_RandoNewGame): the screen's values become the run's. Reforged_SetRunFix: a loaded
   save block's recorded value (reforged_savemark.c), or a test's. */
int Reforged_RunFix(int id);
void Reforged_SetRunFix(int id, int value);
void Reforged_GameFixesNewGame(void);
/* Pair Up (reforged_pairup.c; the game's hooks: include/bmunit.h RFO_PAIR_*, bmbattle.h,
   bmreliance.h, src/menu_def.c's Swap row). */
int Reforged_PairOn(const struct Unit *unit);
int Reforged_PairGauge(const struct Unit *unit);
void Reforged_PairTest(void *proc); /* FE8PC_PAIR_TEST, a test switch */
/* --- end of the Game fixes block --- */

/* --- Randomizer (port/reforged/reforged_rando*.c; the options are RFO_RANDO_*, above) ---
   The menu's settings are for the next new game. The seed is a u32 of its own in the settings
   file (randomizer_seed); 0 means "pick one at New Game" (from the host clock, never the game's
   RNG), which is then written back so the menu shows it. The game hooks are the RANDO_* macros
   at the end of include/bmunit.h; the other entry points are here. */
unsigned Reforged_GetRandoSeed(void);
void Reforged_SetRandoSeed(unsigned seed);
/* A seed from the host clock (never 0, never the game's RNG). */
unsigned Reforged_NewRandoSeed(void);
/* The Randomizer screen, a blocking child of the options screen (as the Cheats screen is). */
void *StartReforgedRandomizerScreen(void *parent);
/* The Controls screen (port/reforged/reforged_controls_menu.c; the bindings:
   port/include/port_controls.h), a blocking child of the options screen. */
void *StartReforgedControlsScreen(void *parent);
/* The Display screen (port/reforged/reforged_display_menu.c), and the display options
   applied to the window (option: the one that changed; -1 all, after the settings file). */
void *StartReforgedDisplayScreen(void *parent);
void *StartReforgedHelpersScreen(void *parent);

/* "Rewind" (reforged_rewind.c): the snapshot after the player phase's suspend save (bm.c), the
   key from the platform each frame step, and the free map's check (playerphase.c MainIdle; 1:
   rewinding, the idle loop returns). The save mark's blocks as a blob for it
   (reforged_savemark.c). */
void Reforged_RewindSnapshot(void);
void Reforged_RewindKey(int held);
int Reforged_RewindIdle(void *proc);
int Reforged_RandoNuzlocke(void);
#define REFORGED_SAVEMARK_STATE_MAX 8192 /* five blocks; 1024 before Skills kept skills (v1.6.0), 4096 before 48 FE7-class marks a block (v1.16.0) */
void Reforged_SaveMarkStateGet(void *out);
void Reforged_SaveMarkStateSet(const void *in);
void Reforged_SaveMarkStateSetSuspend(const void *in); /* only the two suspend blocks' entries */
void Reforged_RewindForget(void);
void Reforged_ApplyDisplay(int option);
/* Option sets (reforged_settings.c; their screen: reforged_display_menu.c): the options
   (not the controls or the Display options) saved to / loaded from option-set-N.txt
   in the per-user folder, N 1-9. Load: 0 when there is no such set (nothing changes);
   a loaded set is written to the settings file at once. */
void *StartReforgedOptionSetsScreen(void *parent);
int Reforged_OptionSetExists(int n);
int Reforged_SaveOptionSet(int n);
int Reforged_LoadOptionSet(int n);
/* src/bmsave.c WriteNewGameSave, before the save mark: starts the run's randomization from the
   menu's settings (or none), from the vanilla tables. */
void Reforged_RandoNewGame(void);
/* src/main.c, right after Reforged_LoadSettings: keeps the vanilla tables (the snapshot every
   apply starts from). With FE8PC_RANDO_CHECK set it also runs the self-test (many seeds, every
   option on, the results checked; logged), and exits when it is "exit". */
void Reforged_RandoBoot(void);
/* The run being played: 1 when it is randomized; its seed (0 when not). */
int Reforged_RandoActive(void);
unsigned Reforged_RandoActiveSeed(void);
/* "Nuzlocke" (port/reforged/reforged_rando_nuzlocke.c): 1 when the run being played has
   it (casual mode's retreat is then off). Reforged_RandoUnitDied: a unit is about to
   die (casual mode's hooks, the arena); a blue one is put in the run's graveyard, written
   to the save mark at once. Reforged_RandoBlockLoaded (Reforged_SaveBlockRead, after a
   block's units are in): the run's dead are dead again. */
int Reforged_RandoNuzlocke(void);
void Reforged_RandoUnitDied(struct Unit *unit);
void Reforged_RandoBlockLoaded(void);
/* The save mark's Nuzlocke graveyards (reforged_savemark.c), one per run (seed and the
   save's playthrough id): the dead pids (how many, in out); add one (the mark is
   written again at once); clear one (a new game). */
int Reforged_GraveGet(unsigned seed, int play, unsigned char *out, int max);
void Reforged_GraveAdd(unsigned seed, int play, int pid);
void Reforged_GraveClear(unsigned seed, int play);
/* --- end of the Randomizer block --- */

/* --- Skill banners (port/reforged/reforged_skillbanner.c; RFO_SKILL_BANNERS) ---
   A whole-screen picture the software renderer draws over each finished line, after windows and
   blending (port/render/obj_line.c, played builds only, PORT_RFO_MAPOVERLAY): nothing is written
   to GBA memory. pix: 0 nothing; RFO_SCREEN_OPAQUE | BGR555 drawn as it is; any other value is a
   BGR555 colour mixed half and half with the game's pixel. Rows whose rowUsed is 0 are skipped,
   and the whole picture is dropped once frame is more than 2 frame steps old (whoever draws it
   stamps it each frame: the battle animations' loop, or OnVBlank on the map). Plain types only. */
#define RFO_SCREEN_W 240
#define RFO_SCREEN_H 160
#define RFO_SCREEN_OPAQUE 0x8000
struct ReforgedScreenOverlay {
    int active;
    unsigned frame;
    unsigned char rowUsed[RFO_SCREEN_H];
    unsigned short pix[RFO_SCREEN_H][RFO_SCREEN_W];
};
extern struct ReforgedScreenOverlay gReforgedScreenOvl;
void Reforged_SkillBannerMapTick(void); /* OnVBlank, through Reforged_MapHpBarsVBlank */
/* --- end of the skill banners block --- */

/* --- FE7 classes (port/reforged/reforged_fe7.c) ---
   Classes 0x80-0x88 built from the user's FE7 (USA) ROM when it is found at boot: 1 when jid is
   one of them and it was built (never without the ROM). Their names and descriptions are virtual
   text ids 0x7F14-0x7F25 (Reforged_VirtualMsg; k = id - 0x7F14); the FE6 classes' (v1.19.0) follow. */
#define REFORGED_FE7_LAST_JID 0x88 /* the last FE7 class id (v1.15.0: 0x83, v1.16.0: 0x87) */
#define REFORGED_FE7_JID_ARCHSAGE   0x86
#define REFORGED_FE7_JID_DARK_DRUID 0x87
#define REFORGED_FE7_JID_BARD       0x88 /* v1.18.0: FE7's Bard (Nils), with Dance; test switches only */
/* v1.19.0: FE6 classes from the user's own FE6 (Japan, or a translation of it) ROM, built by the same loader
   (reforged_fe7.c) right after the FE7 ones: 0x89 Thief F, 0x8A/0x8B Nomad Trooper (M/F), 0x8C King. Their
   names and descriptions continue the virtual text ids (0x7F26-0x7F2D). The Reforged_Fe7* functions below
   cover every foreign class, FE7's and FE6's (0x80 to REFORGED_FOREIGN_LAST_JID). */
#define REFORGED_FE6_JID_THIEF_F    0x89
#define REFORGED_FE6_JID_TROOPER    0x8A
#define REFORGED_FE6_JID_TROOPER_F  0x8B
#define REFORGED_FE6_JID_KING       0x8C
/* v1.20.0 (FE6 batch B): 0x8D Lion Lord and 0x8E Master Lord (Roy's line), 0x8F Divine Dragon; names and
   descriptions 0x7F2E-0x7F33. Test switches only. */
#define REFORGED_FE6_JID_LION_LORD     0x8D
#define REFORGED_FE6_JID_MASTER_LORD   0x8E
#define REFORGED_FE6_JID_DIVINE_DRAGON 0x8F
#define REFORGED_FOREIGN_LAST_JID   0x8F /* the last foreign (FE7 or FE6) class id (v1.19.0: 0x8C) */
/* "Myrrh look" (RFO_MYRRH_LOOK, v1.20.0; display-only): Myrrh's dragon form in battle animations is FE6's Divine
   Dragon (its transform in, dragon and transform out: godd_he1, godd_mg1, godd_hk1), when the option is on and
   the FE6 ROM gave those three animations (reforged_fe7.c; include/anime.h RFO_MYRRH_*). Available: 1 when they
   loaded (the option row is grey without them). */
int Reforged_MyrrhLookAvailable(void);
/* "Armed dancers" (RFO_FIX_ARMED_DANCERS, reforged_gamefixes.c): a Dancer gets light E, the Bard anima E (a
   floor; nothing when the fix is off or for any other class). Called with the late joiners' ranks
   (include/bmunit.h RFO_JOIN_RANKS: LoadUnit, UnitChangeFaction), by the unit class test switch, and after
   a save block's FE7 marks are restored. Reforged_ArmedDancerClass: 1 for the Dancer and a loaded Bard. */
void Reforged_FixArmedDancer(struct Unit *unit);
int Reforged_ArmedDancerClass(const struct Unit *unit);
/* The randomizer's FE7 extras (reforged_rando_classes.c, "FE7 classes", randomizer version 10): the run's
   rare third promotion choice for class jid (Archsage or Dark Druid), 0 for none (or the class not loaded).
   Reforged_Fe7ThirdPromo includes it. */
int Reforged_RandoFe7Promo(int jid);
int Reforged_Fe7ClassAvailable(int jid);
int Reforged_Fe7VirtualMsg(int k, char *buf);
/* Promotions: the promotion screen's third choice for class jid (Myrmidon: Blade Lord; Knight: Great
   Lord; in a randomized run with "FE7 classes", Reforged_RandoFe7Promo's), 0 for none. */
int Reforged_Fe7ThirdPromo(int jid);
/* Randomizer versions 11-14 (reforged_rando_classes.c, v1.21.0), and v1.23.0's "Third promotion" in a game without the
   randomizer: every promotion screen offers three classes.
   Reforged_PromoList: the unit's three (3), else 0 (another run: the screen's own choices); Reforged_PromoSlot:
   choice slot 0-2 of them, else jid; Reforged_PromoThree: 1 when the promotion opens the choice screen (logged);
   Reforged_RandoEventPromoMenu: 1 when a twin's Chapter 16 promotion opens it (Reforged_Fe7EventPromoMenu). */
struct Unit;
int Reforged_PromoList(const struct Unit *unit, unsigned char out[3]);
int Reforged_PromoSlot(struct Unit *unit, int slot, int jid);
int Reforged_PromoThree(struct Unit *unit);
int Reforged_RandoEventPromoMenu(struct Unit *unit);
/* "Third promotion" (RFO_FIX_PROMO3, reforged_rando_classes.c, v1.23.0): Reforged_Promo3Active: 1 in a game without
   the randomizer started with the fix on (Reforged_PromoList and the rest then give the owner's three); Reforged_Promo3Tables:
   the twins' prf locks on their characters while it is on (after Rando_Start and a save block's fixes are read). */
int Reforged_Promo3Active(void);
void Reforged_Promo3Tables(void);
/* Saves never hold an id past 0x7F: the FE8 class written for an FE7 one (others: jid). */
int Reforged_Fe7Fallback(int jid, int female);
/* A save's unit in an FE7 class as the save mark's "<block>_fe7" keeps it: the unit's index, its FE7
   class, and the ranks the save wrote as 0 (mask: their weapon kinds, bit ITYPE_*) because its stand-in
   class cannot use them. */
#define REFORGED_FE7_MARKS 48 /* per save block (v1.15.0: 16; a suspend holds the blue, red and green units) */
struct Reforged_Fe7Mark {
    unsigned char index, jid, mask, ranks[8];
};
void Reforged_Fe7PackUnit(const struct Unit *unit, unsigned *jid, unsigned *flag, unsigned char *ranks); /* chapter save */
void Reforged_Fe7PackReset(void);
void Reforged_Fe7UnpackUnit(struct Unit *unit, unsigned flag);                     /* chapter save read */
int Reforged_Fe7SuspendJid(const struct Unit *unit);                                /* suspend save */
void Reforged_Fe7StripRanks(const struct Unit *unit, unsigned char *ranks);         /* both saves */
int Reforged_Fe7TakePacked(struct Reforged_Fe7Mark *out, int max);   /* the save mark's "game<n>_fe7" */
int Reforged_Fe7SuspendUnits(struct Reforged_Fe7Mark *out, int max); /* the save mark's "suspend*_fe7" */
void Reforged_Fe7MarkRestore(const struct Reforged_Fe7Mark *list, int n, int suspend);
/* Test switch FE8PC_TEST_ARENA_CLASS (reforged_arena.c): around a suspend save written during the
   arena fight (src/bmsave.c WriteSuspendSave), the fighter carries its own class and rank (1: before,
   0: after), so a resumed suspend never brings the test class back. */
void Reforged_ArenaTestSuspend(int before);
/* Skills: the FE8 class whose skills FE7 class jid has (others: jid). */
int Reforged_Fe7SkillClass(int jid);
/* Test switch FE8PC_TEST_UNIT_CLASS=pid:jid (src/bm.c BmMain_SuspendBeforePhase). */
void Reforged_TestUnitClass(void);
/* --- end of the FE7 classes block --- */

#endif /* GUARD_PORT_REFORGED_H */
