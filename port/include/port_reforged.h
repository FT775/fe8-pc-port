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
    RFO_WIDESCREEN,         /* "Widescreen" (v1.25.0): 0 [0], 1 (a 284x160 picture, the game centred; render.h) */
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
    /* "Mouse" (reforged_mouse.c, v1.29.0; the Controls screen's row): 0 [0], 1 (clicks, the wheel and menu
       hover become key presses, the same ones a pad would make). Input only: never marks a save, never in
       an option set. */
    RFO_MOUSE,
    /* "Update notice" (reforged_update.c, v1.31.0; the Display screen's row): 0 [0], 1 (once per run the game
       asks GitHub for the newest release; a newer one is named on the title screen). Never marks a save,
       never in an option set. */
    RFO_UPDATE_NOTICE,
    /* "Sound" (port/audio/hq_pc.c, v1.32.0; the Display screen's row): 0 Original [0], 1 Enhanced (the music
       and sound effects mixed again at 4 x the GBA's rate for the speaker). Never marks a save, never in an
       option set. */
    RFO_AUDIO_HQ,
    /* "Helpers >" (reforged_helpers_menu.c), each 0 [0] or 1; they change play (a save is marked) */
    RFO_CONVOY_ANYWHERE,    /* "Convoy anywhere": every unit has Supply where the chapter has the convoy */
    RFO_SKIRMISH_PASS,      /* "Pass skirmishes": the world-map army walks through monster nodes */
    RFO_REWIND,             /* "Rewind" (reforged_rewind.c): hold the Rewind key to go back a turn */
    RFO_RUINS_SKIP,         /* "Ruin floor select" (reforged_ruins.c): entering the Lagdou Ruins picks the floor to start on */
    RFO_FAST_SUPPORTS,      /* "Faster supports" (reforged_supports.c): 0 off, 1 x2, 2 x3, and units nearby count */
    RFO_SAVE_STATES,        /* "Save states" (reforged_savestate.c, v1.24.0): Ctrl+S saves the moment, Ctrl+L loads it */
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
/* After a save state's load (reforged_savestate.c): writes the settings file when it differs from the
   options and controls in use (a Reforged screen's "changed" flag went back with the load). */
void Reforged_SettingsAfterStateLoad(void);

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
/* After a save state's load: the next release's log line gives no frame count (the counter is the photo's). */
void Reforged_FastForwardAfterStateLoad(void);
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

/* --- Widescreen on the battle map (v1.26.0, milestone M1; port/reforged/reforged_wide.c, port/render/wide.c) ---
   With the Display screen's "Widescreen" row on (render.h gRenderWideOn), the 22-pixel sides of the 284x160
   picture show more of the battle map: its terrain and the standing units and their icons. Display only:
   nothing here writes GBA memory (EWRAM, IWRAM, VRAM, OAM, I/O, gBmSt); the game, the 240x160 frame and the
   saves are the same with the row on or off.
   - src/bmudisp.c calls PortWide_PushObj before each of its 240-pixel culls, with the arguments of the
     CallARM_PushToSecondaryOAM call that follows but with whole x and y: a sprite reaching into a side is
     kept in a host list (never in OAM).
   - src/bm.c OnVBlank calls PortWide_VBlankLatch right after SyncHiOam (beside Reforged_MapHpBarsVBlank):
     the list becomes the shown one with the camera, the map's ring origin and the side squares' BG3 words
     (the tile rule of src/bmmap.c DisplayBmTile, through PortWide_BmTileWords, inside the map only) for the
     frame drawn next, and whether the battle map is up (its main proc, no BMapDispSuspend).
   - port/render/wide.c (played builds, PORT_RFO_WIDE) checks at the start of each frame that BG3 in VRAM
     still holds that map at the latched ring origin (else, and on every other screen, the sides are black)
     and draws the sides line by line. Session data (PORT_STATE_SESSION): rebuilt at every VBlank. */
#define PORT_WIDE_PW      32  /* the latched squares: 32 columns ... */
#define PORT_WIDE_PH      16  /* ... by 16 rows around the camera */
#define PORT_WIDE_OBJ_MAX 128 /* pushes kept per frame (as OAM's 128 entries) */
#define PORT_WIDE_HP_MAX  48  /* v1.27.0: HP bar pictures kept per frame for units reaching into a side */
struct PortWideObj {
    short x, y;                 /* the push's whole x and y, on the game's 240x160 screen */
    unsigned short oam2;        /* its oam2 */
    unsigned short a0or;        /* v1.27.0: attr0 bits the push adds to each entry (OAM0_BLEND of a dying MU) */
    const unsigned short *list; /* its object list (count, then attr0-2 triples) */
};
/* v1.27.0: one unit's HP bar / "!" picture (port/reforged/reforged_hpbars.c) for a unit whose square reaches
   into a side: drawn there over its own sprite's pixels and pixels no sprite drew (as obj_line.c does in the
   middle). The owner is the side sprite pushed with the same x, y, list and oam2. */
struct PortWideHp {
    short sqx, sqy;             /* the unit's square on the game's screen (whole x and y) */
    short ox, oy;               /* the owner sprite's push x and y */
    unsigned short oam2;        /* ... its oam2 */
    short owner;                /* the latched obj index of the owner, -1 if none (set by the latch) */
    const unsigned short *list; /* ... its object list */
    unsigned short pix[16][16]; /* RFO_OVERLAY_OPAQUE | BGR555, 0 = not drawn */
};
struct PortWideLatch {
    unsigned frame;            /* gPortFrameCount at the latch; the renderer drops one more than 2 frame steps old */
    int ok;                    /* the battle map is up and the latch is usable */
    int suspended;             /* ... but BMapDispSuspend is in force (gameGfxSemaphore): the battle animation
                                  over the map (v1.28.0: black sides, the strip slid to the middle) */
    int reason;                /* why not (PORT_WIDE_NO_*), for the log */
    short camX, camY;          /* gBmSt.camera */
    unsigned short ox, oy;     /* gBmSt.mapRenderOrigin: the BG3 ring's square 0 */
    short mapW, mapH;          /* gBmMapSize (squares) */
    int off;                   /* where the game's x = 0 sits in the wide picture, 0 .. 2 x margin */
    int rangeView;             /* gBmSt.gameStateBits BM_FLAG_0: BG2 is the range layer following the camera
                                  (move/attack ranges, the danger zone), drawn in the sides from rangeClass
                                  (v1.27.0) when VRAM's squares on screen are of the same classes */
    short sx0, sy0;            /* the map square of words[0][0] */
    unsigned short words[PORT_WIDE_PH][PORT_WIDE_PW][4]; /* each square's four BG3 screen entries */
    unsigned char inMap[PORT_WIDE_PH][PORT_WIDE_PW];     /* 0: outside the map (drawn black) */
    /* v1.27.0, only while rangeView: each square's class by src/bmmap.c DisplayMovementViewTile's rule from
       gMapMovementSigned / gMapRangeSigned (inside the map only): PORT_WIDE_RANGE_* */
    unsigned char rangeClass[PORT_WIDE_PH][PORT_WIDE_PW];
    int objCount;
    struct PortWideObj obj[PORT_WIDE_OBJ_MAX];
    int hpCount;               /* v1.27.0: HP bar pictures of units reaching into a side */
    struct PortWideHp hp[PORT_WIDE_HP_MAX];
    int muCount;               /* v1.28.0: pushes of moving units among obj (wide.log, port/tools/widecheck.sh) */
    int muFogCount;            /* v1.28.0: moving enemies reaching a side that the fog hid (src/mu.c PutMu) */
};
enum { PORT_WIDE_RANGE_NONE, PORT_WIDE_RANGE_MOVE, PORT_WIDE_RANGE_ATTACK };
/* (v1.28.0 PORT_WIDE_NO_WEATHER: a weather of particle sprites, which the retail chapters never set: the map
   is shown with black sides rather than with particles in the middle only) */
enum { PORT_WIDE_NO_NONE, PORT_WIDE_NO_PROC, PORT_WIDE_NO_SUSPEND, PORT_WIDE_NO_SIZE, PORT_WIDE_NO_CAMERA,
       PORT_WIDE_NO_WEATHER, PORT_WIDE_NO_COUNT };
extern struct PortWideLatch gPortWide; /* the shown one (read by port/render/wide.c) */
int PortWide_On(void);
void PortWide_PushObj(int x, int y, const unsigned short *list, int oam2);
/* v1.28.0: src/mu.c, after a moving unit's side push (kind 0, kept 1/0) or when the fog hid one (kind 1):
   counts for wide.log only */
void PortWide_NoteMu(int kind, int kept);
/* v1.28.0: src/phasechangefx.c PhaseIntroUnk_Loop, after the phase banner's band sprite (PutSpriteExt at y,
   oam2, affine group 0): copies of the band's outer 8-pixel columns across the sides, for the side list only */
void PortWide_PushPhaseBand(int y, int oam2);
/* v1.27.0: the same with attr0 bits added to every entry (src/mu.c: OAM0_BLEND of a dying unit). The movement
   arrow (src/bmpatharrowdisp.c DrawPathArrow) and the moving units (src/mu.c PutMu / PutMuSMS, before their
   cull, with the AP handle's current frame, which the AP display step would push; no AP state is advanced)
   push here too. sideOnly: the game culls this push (draws none of it in its 240 strip), so it is kept only
   if none of its on-screen objects falls on the strip (no half sprite at a seam). */
int PortWide_PushObjEx(int x, int y, const unsigned short *list, int oam2, int a0or, int sideOnly); /* 1 = kept */
/* v1.27.0: reforged_hpbars.c, for a unit whose square (sqx, sqy) reaches into a side: its picture, and the
   push its sprite makes (ox, oy, list, oam2). */
void PortWide_PushHpBar(int sqx, int sqy, int ox, int oy, const unsigned short *list, int oam2,
    const unsigned short pix[16][16]);
/* v1.27.0: whether a box at the game's x, w wide, reaches into a side (x < 0 or x + w > 240, within the
   sides' reach); 0 with the row off. */
int PortWide_ReachesSide(int x, int w);
/* v1.27.0: src/bmudisp.c, PutStandingMuSprite's push for the sides (src/mu.c PutMuSMS; sideOnly as above) */
int PortWide_PutStandingMuSprite(int x, int y, int oam2, int jid, int idx, int sideOnly); /* v1.28.0: 1 = kept */
void PortWide_VBlankLatch(void);
/* src/bmmap.c: the four BG3 screen entries DisplayBmTile writes for map square (x, y), for a square inside
   the map; 0 (nothing written) outside it. */
int PortWide_BmTileWords(int x, int y, unsigned short *out);
/* --- end of the Widescreen block --- */

/* --- Mouse controls (v1.29.0; port/reforged/reforged_mouse.c; RFO_MOUSE, off by default) ---
   The mouse never touches game memory: clicks, the wheel and menu hover are turned into GBA key presses
   (D-pad taps, A, B, R), ORed into the live keys where the keyboard's and gamepads' go (the window
   builds' Platform_ReadKeys, port/sdl3/platform_sdl3.c), so every mouse session is a key sequence a pad
   could have pressed (FE8PC_KEYREC records it as a key file; port/tools/mousecheck.sh). To know what to
   press, the planner reads game state read-only and four hooks in the game code (each under
   #if defined(PC_PORT) && PORT_PLAYED_BUILD) say which input screen ran its idle loop in this OnMain,
   with a host copy of its geometry. With the row off the hooks return at once and Reforged_MouseKeys
   gives 0. Session data (PORT_STATE_SESSION).
   - The platform, once per frame step: Reforged_MouseEvent for each mouse event (picture pixels of the
     presented picture, presentW wide; kind REFORGED_MOUSE_*; button 1 left, 2 middle, 3 right; steps:
     the wheel's whole notches, + up / right), then keys |= Reforged_MouseKeys(keys) (keys: the real
     keyboard and gamepad keys; any of them cancels the mouse's plan). Reforged_MouseReset: after a save
     state's load (Platform_ReleaseKeys). Reforged_MouseOn: the row.
   - src/hardware.c UpdateKeyStatus: Reforged_MouseKeyRead, the planner's clock (one per key read).
   - src/playerphase.c PlayerPhase_MainIdle (0) / PlayerPhase_RangeDisplayIdle (1): Reforged_MouseMapIdle;
     src/uimenu.c Menu_OnIdle (after its frozen/doomed returns): Reforged_MouseMenu; src/uiselecttarget.c
     TargetSelection_Loop (after its frozen return): Reforged_MouseTargets; v1.30.0: the trade screen and the
     shops' lists: Reforged_MouseGrid (below). */
enum { REFORGED_MOUSE_MOTION, REFORGED_MOUSE_DOWN, REFORGED_MOUSE_WHEEL, REFORGED_MOUSE_WHEEL_X };
struct MenuProc;
struct SelectTargetProc;
int Reforged_MouseOn(void);
void Reforged_MouseEvent(int kind, int button, int px, int py, int steps, int presentW);
unsigned short Reforged_MouseKeys(unsigned short realKeys);
void Reforged_MouseReset(void);
void Reforged_MouseKeyRead(void);
void Reforged_MouseMapIdle(int move);
void Reforged_MouseMenu(struct MenuProc *proc);
void Reforged_MouseTargets(struct SelectTargetProc *proc);
/* port/reforged/reforged_controls_menu.c Controls_Loop: a left click there is A only on the hand's row (game-screen
   pixel rows y0 <= y < y1; y0 >= y1: nowhere), so a stray click neither switches the Mouse row nor starts a capture. */
void Reforged_MouseHandRow(int y0, int y1);
/* v1.30.0: a list or grid of items the hand moves over with the D-pad (src/bmtrade.c TradeMenu_OnLoopUnselected /
   _OnLoopSelected, src/bmshop.c Shop_Loop_BuyKeyHandler / _SellKeyHandler; each before its input is read): a click
   on an item moves the hand there (Left / Right for the column, Up / Down for the row), then A; hover moves the
   hand. Game-screen pixels. */
struct ReforgedMouseGrid
{
    int id;              /* which screen (1 trade, 2 shop buy, 3 shop sell): another id is another list */
    int cols;            /* 1 or 2 */
    int x0[2], x1[2];    /* a column's items: x0 <= x < x1 */
    int y0[2];           /* the top of a column's first row on screen */
    int rowH, lines;     /* a row's height; rows on screen */
    int top[2];          /* the index of a column's first row on screen */
    int count[2];        /* the items 0 .. count-1 the hand can stand on */
    int curCol, cur;     /* the hand */
    int wrap;            /* Up on the first item / Down on the last goes round */
    int busy;            /* A is not taken now (a page scrolling, a help box): A waits */
};
void Reforged_MouseGrid(const struct ReforgedMouseGrid *g);
/* --- end of the Mouse controls block --- */

/* --- Update notice (v1.31.0; port/reforged/reforged_update.c; RFO_UPDATE_NOTICE, off by default) ---
   Reforged_UpdateCheck: the row on (Reforged_ApplyDisplay at start, or the row switched on): asks GitHub in the
   background, once per run. Reforged_UpdateFrame: each frame step (port/hw/frame.c, before the picture is
   shown): on the title screen, a newer release named once (the save states' toast). */
void Reforged_UpdateCheck(void);
void Reforged_UpdateFrame(void);

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
int Reforged_RewindRestarting(void); /* 1 while a rewind's restart reads its suspend (v1.24.1) */
/* "Save states" (v1.24.1): a state was loaded (1, reforged_savestate.c) or a new game started (0); a block
   written then is marked save_states=1 only after a load (reforged_savemark.c). */
void Reforged_SaveMarkStateLoaded(int loaded);
/* "Save states" (reforged_savestate.c, v1.24.0; port/include/port_state.h): the .sav the game writes
   ("" when there is none), beside which the state file goes (reforged_savemark.c); the FE7 / FE6 ROM
   loaded at boot (game 0 FE7, 1 FE6): 1 and its SHA-1 in hex, or 0 (reforged_fe7.c). */
const char *Reforged_SaveMarkWritePath(void);
int Reforged_ForeignRomSha1(int game, char out[41]);
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
