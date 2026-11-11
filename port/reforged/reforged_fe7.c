#include "global.h"

/*
 * FE7 classes (the owner's request of 2026-11-08; plan steps D1-D3): classes taken from the
 * user's own Fire Emblem (USA) ROM, read at run time and never copied anywhere.
 *
 * The ROM. Looked for once at boot, after the FE8 ROM is loaded and checked and before the ROM
 * relocations (port/hw/memory.c gPortAfterRomLoad, which this file's constructor sets in the
 * played builds only): the file named by FE8PC_FE7_ROM, else the first .gba file with the FE7
 * (USA) header (title FIREEMBLEME, game code AE7E) next to the exe, then next to the FE8 ROM.
 * It must be 16,777,216 bytes with SHA-1 c735fdbb9e8abe19e0c6a44708df19acc962e204. It is read
 * into the host's ROM window at 0x09000000, above the FE8 ROM (the window is 32 MiB; LoadRom
 * fills the first 16), and never written. FE7's own pointers (0x08xxxxxx) are rebased by
 * +0x01000000 (Fe7Ptr). romreloc and the guarded loads (gbabus.c) only know the FE8 ROM's
 * 16 MiB, so the FE7 data is read with plain pointers, as the game's decompressors already do.
 * One log line says what was found; without the ROM nothing else changes and the classes stay
 * absent (their table entries zero, unreachable: Reforged_Fe7ClassAvailable).
 *
 * The classes, ids past FE8's own (0x80-0x88), built into the grown host tables:
 *   0x80 Nomad (FE7 0x2E), 0x81 Nomad F (FE7 0x2F), 0x82 Blade Lord (FE7 0x08, for Myrmidons),
 *   0x83 Blade Lord for Eirika (the same data, plus CA_LORD and CA_LOCK_4 for the Rapier and
 *   Sieglinde; promotion field CLASS_EIRIKA_LORD) (v1.15.0); 0x84 Great Lord (FE7 0x09, for Knights
 *   of either sex: FE7's lock bit 29 cleared; shown as "Armor Lord" (FE7 USA calls Eliwood's class Knight Lord), since FE8 (USA)
 *   names the twins' Master Lord "Great Lord"), 0x85 Great Lord for Ephraim (the same, plus CA_LORD;
 *   bit 29 is FE8's CA_LOCK_5, Reginleif and Siegmund), 0x86 Archsage (FE7 0x42, bit 31 cleared) and
 *   0x87 Dark Druid (FE7 0x45) (v1.16.0, plan 2 items A-D), 0x88 Bard (FE7 0x41, Nils's class; v1.18.0: FE7's
 *   Play as FE8's Dance, CA_DANCE for CA_PLAY; only its play animation, so a fight with a weapon (the "Armed
 *   dancers" game fix) plays the map animation; reachable only through the test switches).
 * - Class data (src/data_classes.c, grown to 0x88): FE7's bases, caps, power, growths and
 *   promotion bonuses (+0x0B..+0x27, the same layout), weapon ranks and attributes (the bits mean
 *   the same in both games: Blade Lord's lock bits are FE8's Shamshir and Mani Katti locks);
 *   number, promotion, portrait, sort order and terrain tables are FE8's (movement: Nomad the
 *   AnimalT1 set, Blade Lord CommonT2, Great Lord Armor, Archsage and Dark Druid Magic: FE7 gives
 *   Great Lord the Knight's tables and the two casters the Sage's). A row (sSpecs) may override:
 *   attribute bits cleared, the base ranks (Great Lord sword D, axe D and lance C; Archsage the
 *   Sage's plus dark D; Dark Druid the Druid's plus light D: owner decisions D2/D3, no FE7 S
 *   ranks), the promotion gains (Archsage FE8 Sage's, Dark Druid FE8 Druid's) and the bases (FE8
 *   Sage's / Druid's + 2 on HP, Str/Mag, Skl, Spd, Def and Res, Con and Move theirs: D4; FE7's are
 *   Athos's and Nergal's). Every class's caps are clamped to HP 60 and 30 for the others (the
 *   game's and the save's limits; only Dark Druid's 80 HP is changed).
 * - Battle animations (src/banim_data.c, grown to 0xE8): each FE7 animation gets a slot from
 *   0xD0 (24 slots). Modes, OAM and palette are used in place (rebased); the script is decompressed
 *   once, every frame record's sheet pointer rebased (an FE7 script has only end, command and frame
 *   words, the FE8 format; anything else rejects the animation), two commands made command 0x00
 *   (no case does anything with it: banim-main.c BattleAIS_ExecCommands "case 0: break", animedrv.c
 *   queues it without waiting, banim-ekrmainmini.c ignores it): 0x47 (FE8's cape overlay,
 *   NewEfxMantBatabata, which picks its art by FE8 animation id and draws FE8's default cape for a
 *   pool slot; Archsage and Dark Druid have it) and 0x3D (FE7's Nergal dark aura, Dark Druid's;
 *   FE8's leftover handler starts a screen quake and leaves the animation blocked, so the next C05
 *   never starts the spell: the Dark Druid's attack hung in testing), and
 *   stored as an uncompressed LZ77 stream, which is what the game decompresses. Sizes are checked
 *   against the game's buffers (script 0x2A00, OAM 0x57F0, palette 0xA0, each sheet 0x2000). The
 *   animation list keeps the weapon-type entries and maps item entries: FE7's Sol Katti as FE8's
 *   Mani Katti, the Hand Axe and Tomahawk (the same ids in both games, plus FE8's Hatchet, another
 *   thrown axe, on the same animation); others (Armads: FE8's 0x85 is Sieglinde) are dropped. A
 *   row may borrow an FE8 class's lance entries (Great Lord has no lance animation: the General's
 *   for 0x84, Ephraim's Master Lord's for 0x85, D2), shown as that class for the fight.
 * - Standing sprites (src/unit_icon_wait_data.c, ids 0x6B-0x73) and moving sprites
 *   (src/unit_icon_move_data.c, grown to 0x88): FE7's entries, rebased (the moving sprites'
 *   motion data has only relative offsets).
 * - Names and descriptions: decoded once with FE7's Huffman tree (the algorithm of
 *   port/asm/arm_pc.c DecodeString), FE7's 0x1F byte dropped, served as virtual text ids
 *   0x7F14-0x7F25 (reforged_items.c Reforged_VirtualMsg).
 * A log-only self-test runs after the load: FE7 text 0x493 must decode to "Blade Lord", and the
 * Blade Lord animation 0x11's script must decompress with every sheet pointer in range.
 *
 * Reached through promotions (v1.15.0, plan D4): a Nomad promotes to Ranger (src/classchg-data.c rows
 * 0x80/0x81, Orion's Bolt and Master Seal lists), Eirika's Lunar Brace offers Blade Lord (her Lord row's
 * second choice, set at boot), a Myrmidon gets Blade Lord as the third choice (Reforged_Fe7ThirdPromo);
 * v1.16.0: a Knight (either sex) gets Great Lord as the third choice beside General and Great Knight,
 * and Ephraim's Solar Brace offers Great Lord (his Lord row's second choice, set at boot). Chapter 16's
 * forced promotion (the "yes" answer; v1.16.1, the owner's decision of 2026-11-08, replacing D1 option
 * a) opens the brace's class choice for each twin still in their Lord class: Master Lord or the FE7
 * lord, B does nothing (Reforged_Fe7EventPromoMenu, below); "no" and the brace work as before. Archsage and Dark Druid have no
 * promotion row and are no third tier (the owner's decision of 2026-11-08): v1.17.0's randomizer (version 10, "FE7 classes",
 * reforged_rando_classes.c) offers them as a rare third promotion choice for magic classes with Random promotions, and
 * makes some late magic bosses and enemies FE7 ones; a suspend keeps such enemies as their stand-ins plus the save
 * mark's "suspend_fe7" line, as for the blue units. All of them are reachable through the test
 * switches FE8PC_TEST_ARENA_CLASS (reforged_arena.c) and FE8PC_TEST_UNIT_CLASS (below), which refuse
 * these ids when the classes are absent. Saves (D5) never hold an id past 0x7F (Reforged_Fe7Fallback,
 * below); skills (D6) use FE8 stand-ins (Reforged_Fe7SkillClass). The randomizer's own tables stop at
 * 0x7F; only version 10's FE7 extras (above, their own rolls) name these classes, so no version 8 pick
 * changes; the lord rows it never rewrites.
 *
 * FE6 classes (v1.19.0, the owner's decision of 2026-11-08, "All five", batch A): the same loader with a
 * second profile (struct Game: where the ROM is, its tables, its class row layout, its animation command
 * fixes); every class, FE7's or FE6's, is one sSpecs row and goes through the same BuildClass, animation pool,
 * saves, skills and test switches.
 * - The ROM: FE8PC_FE6_ROM, else the first .gba file with the FE6 header (FIREEMBLEM6, game code AFEJ) next to
 *   the exe, the FE8 ROM or the FE7 ROM. Translations of the Japanese game differ in size and SHA-1, so it is
 *   recognised by its header, three structure probes (the class table's literal at 0x176E0, the animation
 *   count at 0x6A0000, the animation table's literal at 0x4B0F8) and the SHA-1 of the four tables read here
 *   (classes, battle animations, moving sprites, map sprite palettes); its size may be 8 to 32 MiB (the
 *   owner's translated copy is 16 MiB + 316 bytes, and its tail is used data). The whole file is read into
 *   heap memory (the 32 MiB host ROM window is full: FE8 at 0x08000000, FE7 at 0x09000000; 0x0A000000 and up
 *   is a ROM mirror to nullprobe.c), and FE6's pointers are rebased onto that copy (GamePtr). Audit of the
 *   window assumption: the FE7 code read FE7 data only through Fe7Ptr and plain host pointers (romreloc, the
 *   guarded loads of gbabus.c and nullprobe.c never see it: they know the FE8 ROM only); the battle animation
 *   engine reads banim_data's modes, script, OAM, palette and each frame's sheet through plain pointers (the
 *   BIOS LZ77 calls of port/bios/bios.c refuse only the BIOS area), and the map sprite code the
 *   UnitIconWait / UnitIconMove pointers the same way, so a heap address works as the window did. The one
 *   check against the window (SelfTest's FE7_WINDOW) stays FE7-only; FE6's self-test checks its own copy.
 * - Class rows are 0x48 bytes: bases, caps, power and growths at 0x0B-0x21 (the FE8 layout), attributes at
 *   0x24, ranks at 0x28, the animation list at 0x30. FE6 has no promotion gains: each class's gains are its
 *   FE8 template's or gainsFrom's. Ranks are rescaled (FE6 E 1, D 51, C 101, B 151, A 201, S 251 to FE8's
 *   1, 31, 71, 121, 181, 251). Attribute bits 16-20 and 23-31 are cleared (FE6's Roy / Myrmidon / dragon /
 *   Zephiel locks, its bit 20, alternate unit data and major boss; FE8 reads them as other locks,
 *   CA_MAXLEVEL10, CA_UNSELECTABLE, CA_NEGATE_LETHALITY...), caps clamped as for FE7.
 * - Animations: the FE7 checks, plus FE6's command fixes: C3D -> C2B (FE6's C3D only plays sound 0x267 /
 *   0x266, which FE8's C2B plays; FE8's C3D blocks forever: the King's attack would hang), C43 -> C00 (FE6's
 *   sound 0x118; FE8's C43 plays 0x2CF instead and no FE8 command plays 0x118), C47 -> C00 (as FE7). List
 *   items: FE6's Hand Axe 0x20 as FE8's 0x28 (plus the Hatchet), Tomahawk 0x7F as 0x29; other items dropped.
 *   The pool is shared and grown to 40 slots (0xD0-0xF7: indexes stay below 0x100).
 * - Names and descriptions are written here (the translation stores its English in the Japanese font's
 *   codes, and some name slots hold dialogue).
 * - The classes: 0x89 Thief F (FE6 0x35, Cath's class; FE8 Thief's Steal and lock picking, promotes to
 *   Assassin F or Rogue), 0x8A / 0x8B Nomad Trooper (FE6 0x28 / 0x29; with both ROMs the FE7 Nomad / Nomad F's
 *   second promotion choice beside Ranger; FE8 Ranger's gains), 0x8C King (FE6 0x3D, Zephiel's class, sword A;
 *   test switch only). Stand-ins: Thief, Ranger (F), Swordmaster (F); skills the same classes'.
 * - Saves: the stand-in in SRAM, the class in the save mark's "<block>_fe7" line (jid 0x89 and up: every older
 *   build skips such an entry, v1.15.0 reads 0x80-0x83, v1.16.0-v1.17.0 0x80-0x87, v1.18.0 0x80-0x88) and, in a
 *   chapter save, in the unit's flag bits 8-12 as a code no older build reads as one of its classes (bits 8-11
 *   13, 14 or 15: v1.15.0 reads bits 8-10 as slot 5-7, past its 4 classes; v1.16.0-v1.18.0 bits 8-11 as slot
 *   13-15, past their 8 or 9; bit 12 is read by v1.18.0 only when bits 8-11 are 0). Codes: Thief F 0x0D, Nomad
 *   Trooper 0x0E (M and F told apart by the stand-in, Ranger or Ranger F), King 0x1D.
 * Without the FE6 ROM nothing of this exists and the game is v1.18.0's, with or without the FE7 ROM.
 *
 * FE6 batch B (v1.20.0, the owner's decisions of 2026-11-08), the same rows and code:
 * - 0x8D Lion Lord (FE6 0x01, Roy's class; the owner's name) and 0x8E Master Lord (FE6 0x43). Swords only (FE6's
 *   ranks: sword D and A, nothing else, so no borrowed animation is needed). Lion Lord keeps FE6's list (lorm_sw1;
 *   the Binding Blade entry, item 0x0F, is dropped as every unknown FE6 item); Master Lord's list is overridden
 *   (the row's list): roym_sw1, FE6's Binding Blade swing (animation 0x4F), for every sword (the owner's choice;
 *   FE6 binds it to item 0x0F only). roym_sw1's commands all mean the same in FE8: its sounds C1F/C22/C23/C25/
 *   C36/C37/C3E play the same songs in both games' EfxPlaySEwithCmdCtrl, C08 is the critical hit, and C2C is
 *   NewEfxHurtmutEff00 in both (the flame on the blade: an object effect that does not look at the weapon, so an
 *   ordinary sword shows it too); script 0x1BA4 and OAM 0x22D4 bytes, well inside the buffers. Lion Lord
 *   promotes to Master Lord (one choice) with a Master Seal or a Hero Crest (FE8's sword infantry: Myrmidon,
 *   Mercenary), and its gains are FE6's own promotion rule: the promoted class's bases minus the base class's
 *   (gainsFromSrc: HP +4, Str +2, Skl +3, Spd +2, Def +2, Res +5; Con and Move follow the class bases). FE6's lock
 *   bit 16 (the Binding Blade) is cleared with the others; CA_LORD (FE6's bit 13, the same bit) is kept: in FE8 it
 *   only orders the computer's units. Templates Myrmidon / Swordmaster (portrait, sort order, movement and
 *   terrain); stand-ins Myrmidon (F) and Swordmaster (F) by sex, sword only; skills theirs.
 * - 0x8F Divine Dragon (FE6 0x3B, the class of FE6's dragon form): the Divinestone (FE8's dragonstone, Myrrh's lock
 *   CA_LOCK_3) and no weapon both show godd_mg1 (FE6 lists it under its magic kinds, which FE8 has no dragon for).
 *   It fights as a plain animation: no transform (FE8's transform is for Myrrh's class only, src/banim-ekrdragon-
 *   myrrh.c). Map sprites: FE8's Manakete (class 0x3B, its dragon: FE6's own standing sprite 0x36 is a head and
 *   its moving one is the transform's), Myrrh's movement and terrain (flying), stand-in and skills Myrrh's class.
 *   Weak to what Myrrh is weak to: listed with her in src/data_itemuse.c's ItemEffectiveness_Dragon (wyrmslayer)
 *   and ItemEffectiveness_Flier (bows), FE8's rule for a flier, not FE6's (no flier weakness).
 *   HP cap 80 -> 60 (the clamp). Test switch only.
 * - Save codes (Reforged_Fe7PackUnit): Lion Lord 0x0F, Master Lord 0x1E, Divine Dragon 0x0F too (shared by stand-in:
 *   Myrmidon (F) and Myrrh's Manakete differ). v1.15.0 reads bits 8-10 (7 and 6: past its 4 classes),
 *   v1.16.0-v1.18.0 bits 8-11 (15 and 14: past their 8 or 9), v1.19.0 looks the code up with the stand-in (none of
 *   its classes has these codes): every older build keeps the stand-in.
 * - "Myrrh look" (RFO_MYRRH_LOOK, a Helpers row, display-only): FE6's Divine Dragon animations godd_he1, godd_mg1
 *   and godd_hk1 (0x6C, 0x5C, 0x6D) are FE6's transform in, dragon and transform out, the same trio as FE8's
 *   fifd_he1 / fifd_mg1 / fifd_hk1 (BANIM_INDEX_MYRRH_INTRO, _MAIN, _EXIT), and FE6's manakete code runs the same
 *   flow (prepare 0x6B, the dragon 0x5B, back 0x6C; sounds 0xDC, 0xDE, 0xDD). With the option on, Myrrh (character
 *   0x1E; the player's, the only one in Myrrh's class) gets them in FE8's flow (include/anime.h RFO_MYRRH_*): the
 *   transform, idle, attacks, hits, misses, criticals, the way back and her defeat (no way back: FE8's girl) stay
 *   FE8's code, only the indexes and the transform's palettes change. The transforms use two palettes: their own
 *   (the girl, FE6's Fa) and the dragon's (godd_mg1's), each the unit's faction row, as FE6's code loads them.
 *   godd_hk1's mode table is valid for modes 0-1 only (the rest point past its script; FE6 plays mode 0 alone):
 *   those modes are mapped to 0 in a copy (ForeignBanim fixModes). Enemy manaketes keep FE8's look (no enemy is in
 *   Myrrh's class, and the owner's option is Myrrh's); map animations are not battle animations (unchanged).
 */

#if PORT_PLAYED_BUILD

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm.h"
#include "bmbattle.h"
#include "bmunit.h"
#include "bmsave.h"
#include "bmudisp.h"
#include "bmitem.h"
#include "proc.h"
#include "classchg.h"
#include "banim_data.h"
#include "ekrbattle.h"
#include "unit_icon_data.h"
#include "variables.h"
#include "constants/classes.h"
#include "constants/items.h"
#include "constants/characters.h"
#include "constants/banims.h"

#include "port_hw.h"
#include "port_reforged.h"

extern UnitIconWait unit_icon_wait_table[]; /* src/unit_icon_wait_data.c */
extern UnitIconMove unit_icon_move_table[]; /* src/unit_icon_move_data.c (mu.h's gMuInfoTable) */

#ifdef _WIN32
#include <dirent.h>
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void *module, char *name, unsigned long size);
#else
/* A folder is read with getdents64: this is a 32-bit process, and its readdir fails with EOVERFLOW
   on file systems with 64-bit inode numbers (WSL's /mnt drives, for one). */
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#define FE7_SIZE      0x01000000u
#define FE7_WINDOW    0x09000000u
#define FE7_SHA1      "c735fdbb9e8abe19e0c6a44708df19acc962e204"

/* FE7 (USA) tables (GBA addresses) */
#define FE7_CLASSES     0x08BE015Cu /* + jid * 0x54 */
#define FE7_BANIMS      0x08E00008u /* + (id - 1) * 0x20 */
#define FE7_SMS         0x08C99700u /* + sms * 8 */
#define FE7_MMS         0x08C9D174u /* + (jid - 1) * 8 */
#define FE7_HUFF_NODES  0x08B7D71Cu
#define FE7_HUFF_ROOT   0x08B808A8u /* a word: the root node's address */
#define FE7_MSGS        0x08B808ACu /* + id * 4 */
#define FE7_ITEM_SOL_KATTI 0x8C
#define FE7_ITEM_HAND_AXE  0x28 /* FE8's ITEM_AXE_HANDAXE too */
#define FE7_ITEM_TOMAHAWK  0x29 /* FE8's ITEM_AXE_TOMAHAWK too */

#define FE7_FIRST_JID   0x80
#define FE7_CLASS_COUNT (REFORGED_FE7_LAST_JID - FE7_FIRST_JID + 1) /* 9 */
#define FE7_JID_NOMAD        0x80
#define FE7_JID_NOMAD_F      0x81
#define FE7_JID_BLADE_LORD   0x82
#define FE7_JID_BLADE_LORD_E 0x83
#define FE7_JID_GREAT_LORD   0x84
#define FE7_JID_GREAT_LORD_E 0x85
#define FE7_JID_ARCHSAGE     0x86
#define FE7_JID_DARK_DRUID   0x87
#define FE7_JID_BARD         0x88 /* REFORGED_FE7_JID_BARD */
#define FE7_SMS_FIRST   0x6B   /* standing sprites 0x6B-0x73 */
#define FE7_VMSG_FIRST  0x7F14 /* name 0x7F14 + 2k, description + 1 (0x7F14-0x7F25) */

#define POOL_FIRST      0xD0 /* banim_data slots 0xD0-0xF7 (v1.16.0-v1.18.0: 24 slots, 0xD0-0xE7) */
#define POOL_SIZE       40

/* FE6 (Japan; translations keep these tables where they are) */
#define FE6_MIN_SIZE    0x00800000u /* the original cartridge */
#define FE6_MAX_SIZE    0x02000000u /* a GBA pointer reaches 0x09FFFFFF */
#define FE6_CLASSES     0x0860A0E8u /* + jid * 0x48 (the table, 0x0860A130, counts from 1) */
#define FE6_BANIMS      0x086A0008u /* + (id - 1) * 0x20, the FE8 layout */
#define FE6_SMS         0x08662C14u /* + sms * 8 */
#define FE6_MMS         0x086649B4u /* + (jid - 1) * 8 */
#define FE6_ITEM_HAND_AXE  0x20
#define FE6_ITEM_TOMAHAWK  0x7F

/* every foreign class: FE7's 0x80-0x88, then FE6's 0x89-0x8C */
#define FOREIGN_COUNT   (REFORGED_FOREIGN_LAST_JID - FE7_FIRST_JID + 1) /* 13 */
#define FE6_FIRST_JID   (REFORGED_FE7_LAST_JID + 1)
#define FE6_JID_THIEF_F    REFORGED_FE6_JID_THIEF_F
#define FE6_JID_TROOPER    REFORGED_FE6_JID_TROOPER
#define FE6_JID_TROOPER_F  REFORGED_FE6_JID_TROOPER_F
#define FE6_JID_KING       REFORGED_FE6_JID_KING
#define FE6_JID_LION_LORD     REFORGED_FE6_JID_LION_LORD
#define FE6_JID_MASTER_LORD   REFORGED_FE6_JID_MASTER_LORD
#define FE6_JID_DIVINE_DRAGON REFORGED_FE6_JID_DIVINE_DRAGON
/* the Myrrh look (v1.20.0): FE6's Divine Dragon animations */
#define FE6_ANIM_GODD_HE1  0x6C /* the transform in (FE8's BANIM_INDEX_MYRRH_INTRO, fifd_he1) */
#define FE6_ANIM_GODD_MG1  0x5C /* the dragon (BANIM_INDEX_MYRRH_MAIN, fifd_mg1) */
#define FE6_ANIM_GODD_HK1  0x6D /* the transform out (BANIM_INDEX_MYRRH_EXIT, fifd_hk1) */

typedef char Fe7CountIs9[FE7_CLASS_COUNT == 9 ? 1 : -1];
typedef char Fe7BardJid[FE7_JID_BARD == REFORGED_FE7_JID_BARD ? 1 : -1];
typedef char Fe6First[FE6_JID_THIEF_F == FE6_FIRST_JID ? 1 : -1];
typedef char PoolBelow100[POOL_FIRST + POOL_SIZE <= 0xFF ? 1 : -1]; /* a list index (slot + 1) fits a byte */

/* The two source games. base: the host address of the game's 0x08000000 (FE7: the window at 0x09000000;
   FE6: its heap copy), size: the bytes there; then where its tables are and its class row layout. */
enum { GAME_FE7, GAME_FE6, GAME_COUNT };
struct Game {
    const char *tag;
    const u8 *base;
    u32 size;
    u32 classes, rowSize, attrAt, ranksAt, listAt, banims, sms, mms;
    int loaded;
    char path[2048];
    int c47, c3d, c43; /* command words rewritten (ForeignScript), for the log */
};
static struct Game sGames[GAME_COUNT] = {
    { "fe7", NULL, 0, FE7_CLASSES, 0x54, 0x28, 0x2C, 0x34, FE7_BANIMS, FE7_SMS, FE7_MMS, 0, "", 0, 0, 0 },
    { "fe6", NULL, 0, FE6_CLASSES, 0x48, 0x24, 0x28, 0x30, FE6_BANIMS, FE6_SMS, FE6_MMS, 0, "", 0, 0, 0 },
};

/* the game's buffers */
#define SCRIPT_MAX      0x2A00 /* gBanimScrLeft / Right */
#define OAM_MAX         0x57F0 /* gBanimOaml / Oamr2, before the end mark */
#define PAL_MAX         0xA0   /* gBanimPaletteLeft / Right */
#define SHEET_MAX       0x2000 /* RegisterAISSheetGraphics */

static u8 sClassOk[FOREIGN_COUNT];
static char sName[FOREIGN_COUNT][32];
static char sDesc[FOREIGN_COUNT][192];
static struct BattleAnimDef sAnimDefs[FOREIGN_COUNT][8];
static u16 sPoolId[POOL_SIZE];
static u8 sPoolGame[POOL_SIZE];
static int sPoolUsed;
/* the Myrrh look (v1.20.0): banim_data indexes of godd_he1, godd_mg1, godd_hk1 (0: not loaded), and per battle
   position whether the dragon now shown is FE6's (Reforged_MyrrhLookStart) */
static int sMyrrhBanim[3];
static u8 sMyrrhOn[2];

#define MOV_COMMON_T2 { TerrainTable_MovCost_CommonT2Normal, TerrainTable_MovCost_CommonT2Rain, TerrainTable_MovCost_CommonT2Snow }
#define MOV_ANIMAL_T1 { TerrainTable_MovCost_AnimalT1Normal, TerrainTable_MovCost_AnimalT1Rain, TerrainTable_MovCost_AnimalT1Snow }
#define MOV_ARMOR     { TerrainTable_MovCost_ArmorNormal, TerrainTable_MovCost_ArmorRain, TerrainTable_MovCost_ArmorSnow }
#define MOV_MAGIC     { TerrainTable_MovCost_MagicNormal, TerrainTable_MovCost_MagicRain, TerrainTable_MovCost_MagicSnow }
#define MOV_COMMON_T1 { TerrainTable_MovCost_CommonT1Normal, TerrainTable_MovCost_CommonT1Rain, TerrainTable_MovCost_CommonT1Snow }

/* One FE7 class as built here. templ: the FE8 class whose portrait, sort order and terrain bonuses it
   takes; promotion: its ClassData.promotion. attrAdd / attrClear: bits set / cleared in FE7's
   attributes (CA_FEMALE is always cleared). The overrides (0: FE7's own): ranks, the base weapon ranks
   (any non-zero entry: all eight from here); gainsFrom, the FE8 class whose promotion gains it takes;
   basesFrom, the FE8 class whose bases + 2 (HP, Str/Mag, Skl, Spd, Def, Res; Con and Move as they are)
   it takes; lanceFrom, the FE8 class whose lance animation entries it borrows; name, the name shown
   instead of FE7's (NULL: FE7's); danceOnly, keep only the animation list's "no weapon" entry (0x109: the
   dance / play animation), so a fight with a weapon has no battle animation and plays the map one.
   v1.19.0: game, the source game (GAME_FE7 when not given); srcJid, the class's id there; mov NULL: the
   template's movement and terrain tables; desc, the description shown (FE6 rows: name and desc are always
   given); v1.23.0: desc3, the description shown instead in a game with the Game fix "Third promotion" on
   (Reforged_Fe7VirtualMsg; every row has one): two lines of at most 179 talk-font pixels with "Equip:", as FE8's
   own, since the promotion screen's box holds two lines and scrolls a third; any other game shows v1.22.0's;
   portraitFrom, the FE8 class whose generic portrait it shows; saveCode, an FE6 class's code in a
   chapter save's flag bits 8-12 (Reforged_Fe7PackUnit). v1.20.0: list, the animation list used instead of the
   source game's (pairs of an FE8 list entry and a source animation id, ending with 0); gainsFromSrc, the source
   class whose bases are subtracted from this class's for the promotion gains (FE6's promotion rule); smsFrom, the
   FE8 class whose standing and moving map sprites it shows (instead of the source game's). */
static const struct Fe7Spec {
    u8 srcJid, templ, promotion;
    u16 nameId, descId;
    u32 attrAdd, attrClear;
    u8 ranks[8];
    u8 gainsFrom, basesFrom, lanceFrom;
    const s8 *mov[3];
    const char *name;
    u8 danceOnly;
    u8 game, portraitFrom, saveCode;
    const char *desc;
    const char *desc3;
    const u16 *list;
    u8 gainsFromSrc, smsFrom;
} sSpecs[FOREIGN_COUNT] = {
    { .srcJid = 0x2E, .templ = CLASS_RANGER, .promotion = CLASS_RANGER, .nameId = 0x4B9, .descId = 0x23D,
      .mov = MOV_ANIMAL_T1, .desc3 = "Mounted archers of the open plains.\x01" "Equip: Bows." },
    { .srcJid = 0x2F, .templ = CLASS_RANGER_F, .promotion = CLASS_RANGER_F, .nameId = 0x4BA, .descId = 0x23D,
      .mov = MOV_ANIMAL_T1, .desc3 = "Mounted archers of the open plains.\x01" "Equip: Bows." },
    { .srcJid = 0x08, .templ = CLASS_SWORDMASTER_F, .promotion = CLASS_MYRMIDON, .nameId = 0x493, .descId = 0x22F,
      .mov = MOV_COMMON_T2, .desc3 = "Lords who master both blade and bow.\x01" "Equip: Swords, bows." },
    { .srcJid = 0x08, .templ = CLASS_SWORDMASTER_F, .promotion = CLASS_EIRIKA_LORD, .nameId = 0x493, .descId = 0x22F,
      .attrAdd = CA_LORD | CA_LOCK_4, .mov = MOV_COMMON_T2, .desc3 = "Lords who master both blade and bow.\x01" "Equip: Swords, bows." },
    /* Great Lord for Knights: FE7's bit 29 (Hector's Wolf Beil / Armads lock) is FE8's CA_LOCK_5, Ephraim's
       Reginleif and Siegmund: cleared. Shown as "Armor Lord" (FE7 USA's "Knight Lord" is Eliwood's class): FE8 (USA) already
       calls the twins' Master Lord "Great Lord", and Ephraim's Solar Brace menu would list "Great Lord"
       twice. */
    { .srcJid = 0x09, .templ = CLASS_GENERAL, .promotion = CLASS_ARMOR_KNIGHT, .nameId = 0x492, .descId = 0x22E,
      .attrClear = CA_LOCK_5,
      .ranks = { [ITYPE_SWORD] = WPN_EXP_D, [ITYPE_LANCE] = WPN_EXP_C, [ITYPE_AXE] = WPN_EXP_D },
      .lanceFrom = CLASS_GENERAL, .mov = MOV_ARMOR, .name = "Armor Lord", .desc3 = "Heavily armored lords of great might.\x01" "Equip: Swords, lances, axes." },
    /* Great Lord for Ephraim: + CA_LORD, CA_LOCK_5 kept; lance C as his Master Lord's */
    { .srcJid = 0x09, .templ = CLASS_EPHRAIM_MASTER_LORD, .promotion = CLASS_EPHRAIM_LORD, .nameId = 0x492,
      .descId = 0x22E, .attrAdd = CA_LORD,
      .ranks = { [ITYPE_SWORD] = WPN_EXP_D, [ITYPE_LANCE] = WPN_EXP_C, [ITYPE_AXE] = WPN_EXP_D },
      .lanceFrom = CLASS_EPHRAIM_MASTER_LORD, .mov = MOV_ARMOR, .name = "Armor Lord", .desc3 = "Heavily armored lords of great might.\x01" "Equip: Swords, lances, axes." },
    /* Archsage: FE7's bit 31 (CA_LOCK_7 in FE8) cleared; the Sage's ranks plus dark D */
    { .srcJid = 0x42, .templ = CLASS_SAGE, .promotion = CLASS_MAGE, .nameId = 0x4CE, .descId = 0x24F,
      .attrClear = CA_LOCK_7,
      .ranks = { [ITYPE_STAFF] = WPN_EXP_D, [ITYPE_ANIMA] = WPN_EXP_C, [ITYPE_LIGHT] = WPN_EXP_D, [ITYPE_DARK] = WPN_EXP_D },
      .gainsFrom = CLASS_SAGE, .basesFrom = CLASS_SAGE, .mov = MOV_MAGIC, .desc3 = "The wisest and mightiest of sages.\x01" "Equip: Anima, light, dark, staves." },
    /* Dark Druid: the Druid's ranks plus light D */
    { .srcJid = 0x45, .templ = CLASS_DRUID, .promotion = CLASS_SHAMAN, .nameId = 0x4B2, .descId = 0x249,
      .ranks = { [ITYPE_STAFF] = WPN_EXP_E, [ITYPE_ANIMA] = WPN_EXP_D, [ITYPE_LIGHT] = WPN_EXP_D, [ITYPE_DARK] = WPN_EXP_C },
      .gainsFrom = CLASS_DRUID, .basesFrom = CLASS_DRUID, .mov = MOV_MAGIC, .desc3 = "Druids steeped in the deepest dark.\x01" "Equip: Dark, anima, light, staves." },
    /* Bard (v1.18.0; Nils's class): FE7's Play (CA_PLAY) becomes FE8's Dance (CA_DANCE), so the Dancer's
       Dance command, refresh and rings work as they are (FE8's Play passes item 0xCD, which has no battle
       data); no weapon ranks (the "Armed dancers" game fix gives anima E, reforged_gamefixes.c); FE7's play
       animation for the refresh only (danceOnly); the Dancer's portrait, sort order and terrain, FE7's
       bases, caps and growths (no promotion) */
    { .srcJid = 0x41, .templ = CLASS_DANCER, .promotion = 0, .nameId = 0x4CD, .descId = 0x242,
      .attrAdd = CA_DANCE, .attrClear = CA_PLAY, .mov = MOV_COMMON_T1, .danceOnly = 1,
      .desc3 = "Bards whose music revitalizes their\x01" "allies. They cannot attack." },

    /* --- FE6 (v1.19.0): FE6's bases, caps, power and growths, rescaled ranks; names and descriptions written
       here (the descriptions are this port's own words) --- */
    /* Thief F (FE6 0x35, Cath's class): the FE8 Thief's Steal and lock picking (FE6's own bits are the same),
       its movement, terrain and promotions (Assassin F, the female twin of the Thief's first choice, or Rogue,
       which has no female twin), a female generic portrait (Myrmidon F's) */
    { .game = GAME_FE6, .srcJid = 0x35, .templ = CLASS_THIEF, .promotion = CLASS_ASSASSIN_F,
      .attrAdd = CA_STEAL | CA_THIEF, .portraitFrom = CLASS_MYRMIDON_F, .saveCode = 0x0D,
      .name = "Thief", .desc = "A nimble rogue who can\x01" "steal items and pick\x01" "locks.",
      .desc3 = "Nimble thieves who can pick locks.\x01" "Equip: Swords. Skill: Steal." },
    /* Nomad Trooper (FE6 0x28 / 0x29), shown as "Trooper" ("Nomad Trooper" runs into the level-up window's "Lv";
       the description names it in full): FE8 Ranger's gains, movement and terrain (Ranger is the template);
       promotion field Archer (F), the FE8 class nearest the Nomad it comes from (EXP and enemy levels only) */
    { .game = GAME_FE6, .srcJid = 0x28, .templ = CLASS_RANGER, .promotion = CLASS_ARCHER, .gainsFrom = CLASS_RANGER,
      .saveCode = 0x0E, .name = "Trooper", .desc = "A Nomad Trooper: a\x01" "mounted warrior of the\x01" "plains. Swords, bows.",
      .desc3 = "Nomad Troopers: riders of the plains.\x01" "Equip: Bows, swords." },
    { .game = GAME_FE6, .srcJid = 0x29, .templ = CLASS_RANGER_F, .promotion = CLASS_ARCHER_F, .gainsFrom = CLASS_RANGER_F,
      .saveCode = 0x0E, .name = "Trooper", .desc = "A Nomad Trooper: a\x01" "mounted warrior of the\x01" "plains. Swords, bows.",
      .desc3 = "Nomad Troopers: riders of the plains.\x01" "Equip: Bows, swords." },
    /* King (FE6 0x3D, Zephiel's class; FE6's Eckesachs lock is not in its row): sword A, HP cap 80 -> 60;
       Swordmaster's movement, terrain and portrait (a sword-only promoted infantry class) */
    { .game = GAME_FE6, .srcJid = 0x3D, .templ = CLASS_SWORDMASTER, .promotion = CLASS_MYRMIDON, .saveCode = 0x1D,
      .name = "King", .desc = "The sovereign of a\x01" "great nation. Wields\x01" "a mighty sword.",
      .desc3 = "The mighty sovereign of a great\x01" "nation. Equip: Swords." },

    /* --- FE6 batch B (v1.20.0) --- */
    /* Lion Lord (FE6 0x01, Roy's class; the owner's name): FE6's list (lorm_sw1), sword D, promotes to Master Lord;
       the Myrmidon's movement, terrain and portrait */
    { .game = GAME_FE6, .srcJid = 0x01, .templ = CLASS_MYRMIDON, .promotion = FE6_JID_MASTER_LORD, .saveCode = 0x0F,
      .name = "Lion Lord", .desc = "A young lord of noble\x01" "blood who leads with\x01" "the sword.",
      .desc3 = "Young lords of noble blood who lead\x01" "from the front. Equip: Swords." },
    /* Master Lord (FE6 0x43): every sword with roym_sw1 (the Binding Blade swing; the owner's choice), no weapon with
       FE6's own dodge (0x15, lorm_sw1's); sword A; gains FE6's (Master Lord's bases minus Lion Lord's); the
       Swordmaster's movement, terrain and portrait; promotion field Lion Lord (EXP, enemy levels) */
    { .game = GAME_FE6, .srcJid = 0x43, .templ = CLASS_SWORDMASTER, .promotion = FE6_JID_LION_LORD, .saveCode = 0x1E,
      .list = (const u16[]){ 0x100 | ITYPE_SWORD, 0x4F, 0x100 | ITYPE_ITEM, 0x15, 0 }, .gainsFromSrc = 0x01,
      .name = "Master Lord", .desc = "A lord who has come\x01" "into his own. Wields\x01" "the sword masterfully.",
      .desc3 = "Lords who have come into their own.\x01" "Equip: Swords." },
    /* Divine Dragon (FE6 0x3B): the Divinestone (Myrrh's lock) and no weapon with godd_mg1; Myrrh's class's
       movement, terrain, portrait and promotion field; FE8's Manakete (0x3B, a dragon) map sprites; weak to
       dragon and flier weapons as Myrrh (src/data_itemuse.c) */
    { .game = GAME_FE6, .srcJid = 0x3B, .templ = CLASS_MANAKETE_MYRRH, .promotion = CLASS_WYVERN_RIDER_F,
      .attrAdd = CA_LOCK_3, .saveCode = 0x0F, .smsFrom = CLASS_MANAKETE_2,
      .list = (const u16[]){ ITEM_DIVINESTONE, FE6_ANIM_GODD_MG1, 0x100 | ITYPE_ITEM, FE6_ANIM_GODD_MG1, 0 },
      .name = "Divine Dragon", .desc = "A dragon of the divine\x01" "tribe, ancient and\x01" "mighty.",
      .desc3 = "Dragons of the ancient divine tribe.\x01" "Equip: Dragonstone." },
};

/* ------------------------------------------------------------------ the ROM window */

/* Game g's pointer p as a host pointer into its loaded copy (FE7: the window, FE7's 0x08xxxxxx + 0x01000000;
   FE6: its heap copy), or NULL when p is not into that game's ROM. */
static const u8 *GamePtr(const struct Game *g, u32 p)
{
    return g->base != NULL && p - 0x08000000u < g->size ? g->base + (p - 0x08000000u) : NULL;
}

static const u8 *Fe7Ptr(u32 p)
{
    return GamePtr(&sGames[GAME_FE7], p);
}

static u32 Rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u16 Rd16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

/* The word at game g's address gbaAddr (0 outside its ROM). */
static u32 GameWord(const struct Game *g, u32 gbaAddr)
{
    const u8 *p = GamePtr(g, gbaAddr);

    return p != NULL && gbaAddr - 0x08000000u <= g->size - 4 ? Rd32(p) : 0;
}

static u32 Fe7Word(u32 gbaAddr)
{
    return GameWord(&sGames[GAME_FE7], gbaAddr);
}

/* The size an LZ77 stream at game g's address p unpacks to, or 0 when p is not one (or is past max). */
static u32 LzSize(const struct Game *g, u32 p, u32 max)
{
    const u8 *s = GamePtr(g, p);
    u32 n;

    if (s == NULL || p - 0x08000000u > g->size - 4 || s[0] != 0x10)
        return 0;
    n = Rd32(s) >> 8;
    return n != 0 && n <= max ? n : 0;
}

static int HeaderIsFe7(const u8 *h)
{
    return memcmp(h + 0xA0, "FIREEMBLEME", 11) == 0 && memcmp(h + 0xAC, "AE7E", 4) == 0;
}

static int HeaderIsFe6(const u8 *h)
{
    return memcmp(h + 0xA0, "FIREEMBLEM6", 11) == 0 && memcmp(h + 0xAC, "AFEJ", 4) == 0;
}

static int HeaderIs(int game, const u8 *h)
{
    return game == GAME_FE7 ? HeaderIsFe7(h) : HeaderIsFe6(h);
}

/* What a header says it is, for a rejection: "title 'X', game code Y". */
static void HeaderName(const u8 *h, char *out, size_t outSize)
{
    char title[13], code[5];
    int i;

    for (i = 0; i < 12; i++)
        title[i] = h[0xA0 + i] >= 0x20 && h[0xA0 + i] < 0x7F ? (char)h[0xA0 + i] : (h[0xA0 + i] == 0 ? '\0' : '?');
    title[12] = '\0';
    for (i = 0; i < 4; i++)
        code[i] = h[0xAC + i] >= 0x20 && h[0xAC + i] < 0x7F ? (char)h[0xAC + i] : '?';
    code[4] = '\0';
    snprintf(out, outSize, "title '%s', game code %s", title, code);
}

/* FE6's tables as this loader reads them: their SHA-1 must match (a translation that moved or changed one is
   refused). The owner's translated copy and the Japanese original have the same ones (the class rows match
   the decomp, StanHash/fe6, byte for byte). */
static const struct { const char *what; u32 start, end; const char *sha1; } sFe6Regions[] = {
    { "class table", 0x60A130, 0x60A130 + 75 * 0x48, "456ac8e8ce71a205bb17a26d5b1baa467e3cff0f" },
    { "battle animations", 0x6A0000, 0x7FF0A4, "f19a18446d52420181d651ae075813d7bdffb6f7" },
    { "moving map sprites", 0x6649B4, 0x664C0C, "eedff50076768d3f4675d5bd753aa10f1eaa88e1" },
    { "map sprite palettes", 0x100968, 0x100A08, "d1e76801fe33e50e8900d27bb0dfb01a1fee9458" },
};

/* Reads path into heap memory; 1 when it is an FE6 ROM this loader knows (header, structure probes, table
   hashes), else 0 with why set (nothing kept). The whole file is kept: a translation's data may lie past
   16 MiB. */
static int Fe6TryLoad(const char *path, char *why, size_t whySize)
{
    struct Game *g = &sGames[GAME_FE6];
    FILE *fp = fopen(path, "rb");
    u8 head[0xC0], *buf;
    char sha1[41], name[64];
    long size;
    size_t n, r;

    if (fp == NULL)
    {
        snprintf(why, whySize, "cannot open it: %s", strerror(errno));
        return 0;
    }
    n = fread(head, 1, sizeof(head), fp);
    if (n < sizeof(head) || !HeaderIsFe6(head))
    {
        if (n < sizeof(head))
            snprintf(why, whySize, "not FE6 by its header (only %u bytes)", (unsigned)n);
        else
        {
            HeaderName(head, name, sizeof(name));
            snprintf(why, whySize, "not FE6 by its header (%s; FE6 is FIREEMBLEM6, AFEJ)", name);
        }
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0)
    {
        snprintf(why, whySize, "the FE6 header, but its size cannot be read");
        fclose(fp);
        return 0;
    }
    if ((unsigned long)size < FE6_MIN_SIZE || (unsigned long)size > FE6_MAX_SIZE)
    {
        snprintf(why, whySize, "the FE6 header, but %ld bytes (8 to 32 MiB expected)", size);
        fclose(fp);
        return 0;
    }
    buf = malloc((size_t)size);
    if (buf == NULL)
    {
        snprintf(why, whySize, "out of memory for its %ld bytes", size);
        fclose(fp);
        return 0;
    }
    r = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (r != (size_t)size)
    {
        snprintf(why, whySize, "the FE6 header, but only %u of its %ld bytes could be read", (unsigned)r, size);
        free(buf);
        return 0;
    }
    /* structure: the class table's literal (the table minus one row), the animation count, the animation
       table's literal */
    if (Rd32(buf + 0x176E0) != FE6_CLASSES || Rd32(buf + 0x6A0000) != 0x7A || Rd32(buf + 0x4B0F8) != FE6_BANIMS)
    {
        snprintf(why, whySize, "the FE6 header, but its tables are not where FE6 (Japan) has them (0x%08X 0x%X 0x%08X)",
            (unsigned)Rd32(buf + 0x176E0), (unsigned)Rd32(buf + 0x6A0000), (unsigned)Rd32(buf + 0x4B0F8));
        free(buf);
        return 0;
    }
    for (n = 0; n < sizeof(sFe6Regions) / sizeof(sFe6Regions[0]); n++)
    {
        PortSha1Hex(buf + sFe6Regions[n].start, sFe6Regions[n].end - sFe6Regions[n].start, sha1);
        if (strcmp(sha1, sFe6Regions[n].sha1) != 0)
        {
            snprintf(why, whySize, "the FE6 header, but its %s differ (SHA-1 %s): another version or a changed ROM",
                sFe6Regions[n].what, sha1);
            free(buf);
            return 0;
        }
    }
    g->base = buf;
    g->size = (u32)size;
    return 1;
}

/* Loads path into the window; 1 when it is the FE7 (USA) ROM, else 0 with why set (the window is
   cleared again). */
static int TryLoad(const char *path, char *why, size_t whySize)
{
    u8 *dst = (u8 *)(uintptr_t)FE7_WINDOW;
    FILE *fp = fopen(path, "rb");
    char sha1[41];
    size_t n;
    int more;

    if (fp == NULL)
    {
        snprintf(why, whySize, "cannot open it: %s", strerror(errno));
        return 0;
    }
    n = fread(dst, 1, FE7_SIZE, fp);
    more = n == FE7_SIZE && fgetc(fp) != EOF;
    fclose(fp);
    if (n < 0xC0 || !HeaderIsFe7(dst))
        snprintf(why, whySize, "not FE7 (USA) by its header");
    else if (n != FE7_SIZE || more)
        snprintf(why, whySize, "the FE7 (USA) header, but %u bytes%s, not 16,777,216", (unsigned)n, more ? " or more" : "");
    else
    {
        PortSha1Hex(dst, n, sha1);
        if (strcmp(sha1, FE7_SHA1) == 0)
        {
            sGames[GAME_FE7].base = dst;
            sGames[GAME_FE7].size = FE7_SIZE;
            return 1;
        }
        snprintf(why, whySize, "the FE7 (USA) header and size, but SHA-1 %s (a changed or damaged copy)", sha1);
    }
    memset(dst, 0, n);
    return 0;
}

static int EndsWithGba(const char *name)
{
    size_t n = strlen(name);

    return n > 4 && name[n - 4] == '.' && (name[n - 3] | 0x20) == 'g' && (name[n - 2] | 0x20) == 'b' &&
           (name[n - 1] | 0x20) == 'a';
}

/* The names of the .gba files in dir, one after another, each ending in a 0, in at most size bytes
   (names that do not fit are left out); how many. */
static int GbaNames(const char *dir, char *names, size_t size)
{
    size_t used = 0;
    int count = 0;
#ifdef _WIN32
    DIR *d = opendir(dir[0] != '\0' ? dir : ".");
    struct dirent *e;

    if (d == NULL)
        return 0;
    while ((e = readdir(d)) != NULL)
        if (EndsWithGba(e->d_name) && used + strlen(e->d_name) + 1 <= size)
        {
            strcpy(names + used, e->d_name);
            used += strlen(e->d_name) + 1;
            count++;
        }
    closedir(d);
#else
    static char buf[8192];
    int fd = open(dir[0] != '\0' ? dir : ".", O_RDONLY | O_DIRECTORY);
    long n, at;

    if (fd < 0)
        return 0;
    while ((n = syscall(SYS_getdents64, fd, buf, sizeof(buf))) > 0)
        for (at = 0; at < n; at += *(unsigned short *)(buf + at + 16)) /* d_reclen */
        {
            const char *name = buf + at + 19; /* after d_ino (8), d_off (8), d_reclen (2) and d_type (1) */

            if (EndsWithGba(name) && used + strlen(name) + 1 <= size)
            {
                strcpy(names + used, name);
                used += strlen(name) + 1;
                count++;
            }
        }
    close(fd);
#endif
    return count;
}

/* The first .gba file in dir with game's header (FE7 (USA) or FE6) that loads; 1 when one did (its path in
   out). */
static int ScanDir(int game, const char *dir, char *out, size_t outSize, char *why, size_t whySize)
{
    static char names[16384];
    const char *name = names;
    int count = GbaNames(dir, names, sizeof(names)), ok = 0;

    for (; !ok && count > 0; count--, name += strlen(name) + 1)
    {
        u8 head[0xC0];
        FILE *fp;
        size_t n;

        size_t len = strlen(dir);

        /* no separator after "" or after a folder that already ends in one (a root: "/", "D:\") */
        snprintf(out, outSize, "%s%s%s", dir,
            len == 0 || dir[len - 1] == '/' || dir[len - 1] == '\\' ? "" : strchr(dir, '\\') != NULL ? "\\" : "/", name);
        if ((fp = fopen(out, "rb")) == NULL)
            continue;
        n = fread(head, 1, sizeof(head), fp);
        fclose(fp);
        if (n == sizeof(head) && HeaderIs(game, head))
        {
            ok = game == GAME_FE7 ? TryLoad(out, why, whySize) : Fe6TryLoad(out, why, whySize);
            if (!ok)
                PortLog("[%s] '%s' skipped: %s\n", sGames[game].tag, out, why);
        }
    }
    return ok;
}

/* The folder part of path ("" when it has none). A root keeps its slash ("/", "D:\", "D:/"): "" would
   be the current folder, and "D:" is drive D's current folder, not its root. */
static void DirOf(const char *path, char *out, size_t outSize)
{
    const char *a = strrchr(path, '/'), *b = strrchr(path, '\\');
    const char *slash = a > b ? a : b;
    size_t n = slash != NULL ? (size_t)(slash - path) : 0;

    if (slash != NULL && (slash == path || (slash == path + 2 && path[1] == ':')))
        n++; /* the root's own slash */

    if (n >= outSize)
        n = 0;
    memcpy(out, path, n);
    out[n] = '\0';
}

static void ExeDir(char *out, size_t outSize)
{
    char path[1024];
    long n;

#ifdef _WIN32
    n = (long)GetModuleFileNameA(NULL, path, sizeof(path));
    if (n <= 0 || n >= (long)sizeof(path))
        n = 0;
#else
    n = (long)readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n < 0)
        n = 0;
#endif
    path[n] = '\0';
    DirOf(path, out, outSize);
}

/* v1.22.0: the "roms" folder next to the exe (the release zip's place for the player's ROMs), looked in after
   every older place, so a ROM found today is the one found still. */
static void RomsDir(char *out, size_t outSize)
{
    char exe[1024];
    size_t n;

    ExeDir(exe, sizeof(exe));
    n = strlen(exe);
    snprintf(out, outSize, "%s%sroms", exe,
        n == 0 || exe[n - 1] == '/' || exe[n - 1] == '\\' ? "" : strchr(exe, '\\') != NULL ? "\\" : "/");
}

/* ------------------------------------------------------------------ text */

/* FE7 message id into dst (at most max bytes with the 0), 0x1F dropped; its length, or -1. */
static int Fe7Text(int id, char *dst, int max)
{
    const u8 *table = Fe7Ptr(FE7_HUFF_NODES);
    const u8 *root = Fe7Ptr(Fe7Word(FE7_HUFF_ROOT));
    u32 p = Fe7Word(FE7_MSGS + id * 4);
    const u8 *in;
    u32 bits = 0;
    int left = 0, n = 0, steps = 0;

    if (root == NULL)
        return -1;
    if (p & 0x80000000u) /* stored as it is */
    {
        in = Fe7Ptr(0x08000000u | (p & 0x01FFFFFFu));
        if (in == NULL)
            return -1;
        for (; *in != 0 && n < max - 1; in++)
            if (*in != 0x1F)
                dst[n++] = (char)*in;
        dst[n] = '\0';
        return n;
    }
    if ((in = Fe7Ptr(p)) == NULL)
        return -1;
    for (;;)
    {
        const u8 *node = root;
        u32 word;

        do
        {
            if (--left < 0)
            {
                bits = *in++;
                left = 7;
            }
            word = (bits & 1) ? Rd16(node + 2) : Rd16(node);
            node = table + (word << 2);
            bits >>= 1;
            word = Rd32(node);
            if (++steps > 0x10000)
                return -1;
        } while (!(word & 0x80000000u));

        if (word & 0xFF00) /* a two-byte code: kept as it is */
        {
            if (n + 2 >= max)
                return -1;
            dst[n++] = (char)word;
            dst[n++] = (char)(word >> 8);
            continue;
        }
        if ((word & 0xFF) == 0)
            break;
        if ((word & 0xFF) == 0x1F)
            continue;
        if (n + 1 >= max)
            return -1;
        dst[n++] = (char)word;
    }
    dst[n] = '\0';
    return n;
}

/* ------------------------------------------------------------------ battle animations */

/* Game g's animation id's decompressed script, checked: every frame record's sheet pointer rebased onto the
   game's loaded copy, and the commands FE8 does differently rewritten (below). The script (malloc'd, its size
   in *size), or NULL with why set. fixModes (v1.20.0): a mode that starts past the script is no error (the caller
   maps it to mode 0). */
static u32 *ForeignScript(struct Game *g, int id, int fixModes, u32 *size, char *why, size_t whySize)
{
    u32 e = g->banims + (id - 1) * 0x20;
    u32 n = LzSize(g, GameWord(g, e + 16), SCRIPT_MAX);
    const u8 *modes = GamePtr(g, GameWord(g, e + 12));
    u32 *w;
    u32 i, frames = 0;

    if (id < 1 || id > 0x200 || n == 0 || n % 4 != 0 || modes == NULL)
    {
        snprintf(why, whySize, "animation 0x%X: no script, or one past 0x%X bytes", id, SCRIPT_MAX);
        return NULL;
    }
    w = malloc(n);
    if (w == NULL)
    {
        snprintf(why, whySize, "out of memory");
        return NULL;
    }
    LZ77UnCompWram(GamePtr(g, GameWord(g, e + 16)), w);
    for (i = 0; i < 12 && !fixModes; i++)
        if (Rd32(modes + i * 4) >= n || Rd32(modes + i * 4) % 4 != 0)
        {
            snprintf(why, whySize, "animation 0x%X: mode %u starts past the script", id, (unsigned)i);
            goto fail;
        }
    for (i = 0; i < n / 4; i++)
    {
        u32 type = w[i] >> 24, cmd = w[i] & 0xFF;

        if (type == 0x85 && (cmd == 0x47 || cmd == 0x3D || (cmd == 0x43 && g == &sGames[GAME_FE6])))
        {
            /* C47 (FE8's cape overlay, wrong for a pool slot) -> C00, which does nothing. C3D: FE7's dark aura,
               FE6's sound 0x267 / 0x266; FE8 blocks on it -> C00 for FE7, C2B for FE6 (FE8's C2B plays FE6's
               sound). FE6's C43 (sound 0x118; FE8's C43 plays 0x2CF, and no FE8 command plays 0x118) -> C00. */
            if (cmd == 0x47)
                g->c47++;
            else if (cmd == 0x3D)
                g->c3d++;
            else
                g->c43++;
            w[i] &= ~0xFFu;
            if (cmd == 0x3D && g == &sGames[GAME_FE6])
                w[i] |= 0x2B;
            continue;
        }
        if (type == 0x80 || type == 0x85)
            continue;
        if (type != 0x86 || i + 2 >= n / 4)
        {
            snprintf(why, whySize, "animation 0x%X: word %u (0x%08X) is not an end, command or frame", id,
                (unsigned)i, (unsigned)w[i]);
            goto fail;
        }
        if (LzSize(g, w[i + 1], SHEET_MAX) == 0 || w[i + 2] >= OAM_MAX)
        {
            snprintf(why, whySize, "animation 0x%X: frame at word %u: sheet 0x%08X or OAM offset 0x%X out of range", id,
                (unsigned)i, (unsigned)w[i + 1], (unsigned)w[i + 2]);
            goto fail;
        }
        w[i + 1] = (u32)(uintptr_t)GamePtr(g, w[i + 1]); /* FE7: + 0x01000000, into the window */
        frames++;
        i += 2;
    }
    if (frames == 0)
    {
        snprintf(why, whySize, "animation 0x%X: no frames", id);
        goto fail;
    }
    *size = n;
    return w;
fail:
    free(w);
    return NULL;
}

/* The uncompressed LZ77 stream of n bytes (n a multiple of 4): the header, then each 8 bytes
   after a flag byte of 0 (8 literals). */
static u8 *PackLiteral(const u8 *raw, u32 n)
{
    u8 *lz = malloc(4 + n + (n + 7) / 8 + 8);
    u32 i, o = 4;

    if (lz == NULL)
        return NULL;
    lz[0] = 0x10;
    lz[1] = (u8)n;
    lz[2] = (u8)(n >> 8);
    lz[3] = (u8)(n >> 16);
    for (i = 0; i < n; i += 8)
    {
        u32 k = n - i < 8 ? n - i : 8;

        lz[o++] = 0;
        memcpy(lz + o, raw + i, k);
        o += k;
    }
    return lz;
}

/* Game g's animation id as an FE8 animation list index (banim_data slot + 1), converting it the first time
   (the pool is shared by both games: FE7's classes are built first, so their slots are v1.18.0's); 0 when it
   cannot be used (why set). fixModes (v1.20.0, the Myrrh look's godd_hk1): modes that start past the script
   become mode 0, in a copy of the mode table. */
static int ForeignBanim(struct Game *g, int id, int fixModes, char *why, size_t whySize)
{
    u32 e = g->banims + (id - 1) * 0x20;
    struct BattleAnim *b;
    u32 *script, n;
    u8 *lz;
    int k, *modes = NULL;

    for (k = 0; k < sPoolUsed; k++)
        if (sPoolId[k] == id && sPoolGame[k] == (u8)(g - sGames))
            return POOL_FIRST + k + 1;
    if (sPoolUsed == POOL_SIZE)
    {
        snprintf(why, whySize, "animation 0x%X: no free slot", id);
        return 0;
    }
    if (LzSize(g, GameWord(g, e + 20), OAM_MAX) == 0 || LzSize(g, GameWord(g, e + 24), OAM_MAX) == 0 ||
        LzSize(g, GameWord(g, e + 28), PAL_MAX) == 0)
    {
        snprintf(why, whySize, "animation 0x%X: OAM or palette missing or too big", id);
        return 0;
    }
    if ((script = ForeignScript(g, id, fixModes, &n, why, whySize)) == NULL)
        return 0;
    if (fixModes)
    {
        const u8 *m = GamePtr(g, GameWord(g, e + 12));
        int i, fixed = 0;

        if ((modes = malloc(12 * sizeof(int))) == NULL)
        {
            free(script);
            snprintf(why, whySize, "out of memory");
            return 0;
        }
        for (i = 0; i < 12; i++)
        {
            u32 off = Rd32(m + i * 4);

            modes[i] = off < n && off % 4 == 0 ? (int)off : 0;
            fixed += off >= n || off % 4 != 0;
        }
        PortLog("[%s] animation 0x%X: %d of its 12 modes start past its script: played as mode 0\n", g->tag, id, fixed);
    }
    lz = PackLiteral((const u8 *)script, n);
    free(script);
    if (lz == NULL)
    {
        free(modes);
        snprintf(why, whySize, "out of memory");
        return 0;
    }
    k = sPoolUsed++;
    sPoolId[k] = (u16)id;
    sPoolGame[k] = (u8)(g - sGames);
    b = &banim_data[POOL_FIRST + k];
    memcpy(b->abbr, GamePtr(g, e), sizeof(b->abbr) - 1);
    b->abbr[sizeof(b->abbr) - 1] = '\0';
    b->modes = modes != NULL ? modes : (int *)GamePtr(g, GameWord(g, e + 12));
    b->script = (char *)lz;
    b->oam_r = (char *)GamePtr(g, GameWord(g, e + 20));
    b->oam_l = (char *)GamePtr(g, GameWord(g, e + 24));
    b->pal = (char *)GamePtr(g, GameWord(g, e + 28));
    return POOL_FIRST + k + 1;
}

/* ------------------------------------------------------------------ classes */

/* An FE6 weapon rank as FE8's: each FE6 letter's start (E 1, D 51, C 101, B 151, A 201, S 251) to FE8's (1, 31,
   71, 121, 181, 251), linear between them. */
static u8 Fe6Rank(u8 v)
{
    static const u8 sFe6[] = { 1, 51, 101, 151, 201, 251 }, sFe8[] = { 1, 31, 71, 121, 181, 251 };
    int i;

    if (v == 0)
        return 0;
    if (v >= sFe6[5])
        return (u8)(sFe8[5] + (v - sFe6[5]));
    for (i = 4; i > 0 && v < sFe6[i]; i--)
        ;
    return (u8)(sFe8[i] + (v - sFe6[i]) * (sFe8[i + 1] - sFe8[i]) / (sFe6[i + 1] - sFe6[i]));
}

/* FE6's attribute bits FE8 reads otherwise: 16-18 (FE6's Roy, Myrmidon and dragon weapon locks; FE8's locks
   are other weapons'), 19 (Zephiel's lock; FE8's CA_MAXLEVEL10), 20 (FE8's CA_UNSELECTABLE), 23 (alternate
   unit data; FE8's CA_BIT_23), 24 (major boss; FE8's CA_NEGATE_LETHALITY) and 25-31 (unused in FE6) */
#define FE6_ATTR_CLEAR 0xFF9F0000u

static int BuildClass(int k, char *why, size_t whySize)
{
    const struct Fe7Spec *s = &sSpecs[k];
    struct Game *g = &sGames[s->game];
    int jid = FE7_FIRST_JID + k, fe6 = s->game == GAME_FE6;
    const u8 *src = GamePtr(g, g->classes + s->srcJid * g->rowSize);
    struct ClassData *cd = (struct ClassData *)GetClassData(jid);
    const u8 *list = src != NULL ? GamePtr(g, Rd32(src + g->listAt)) : NULL;
    const u8 *sms = src != NULL ? GamePtr(g, g->sms + src[6] * 8) : NULL;
    const u8 *mms = GamePtr(g, g->mms + (s->srcJid - 1) * 8);
    const u16 *own = s->list;
    UnitIconWait *wait = &unit_icon_wait_table[FE7_SMS_FIRST + k];
    UnitIconMove *move = &unit_icon_move_table[jid - 1];
    const char *game = fe6 ? "FE6" : "FE7";
    int n = 0, t;

    if (src == NULL || src[4] != s->srcJid || list == NULL || sms == NULL || mms == NULL)
    {
        snprintf(why, whySize, "%s class 0x%02X: not where it should be", game, s->srcJid);
        return 0;
    }
    /* the animation list: weapon-type entries; items: FE7's Sol Katti as FE8's Mani Katti, the Hand Axe and
       Tomahawk as FE8's (FE7's ids are FE8's; FE6's are 0x20 and 0x7F), plus FE8's Hatchet on the Hand Axe's
       animation; other items dropped */
    for (; own != NULL && own[0] != 0 && n < 7; own += 2) /* v1.20.0: the row's own list */
    {
        int idx = ForeignBanim(g, own[1], 0, why, whySize);

        if (idx == 0)
            return 0;
        sAnimDefs[k][n].wtype = own[0];
        sAnimDefs[k][n].index = (u16)idx;
        n++;
    }
    for (; s->list == NULL && Rd16(list) != 0 && n < 7; list += 4)
    {
        u16 wtype = Rd16(list);
        int idx;

        if (s->danceOnly && wtype != (0x100 | ITYPE_ITEM))
            continue;
        if (wtype < 0x100)
        {
            if (!fe6 && wtype == FE7_ITEM_SOL_KATTI)
                wtype = ITEM_SWORD_MKATTI;
            else if (fe6 && wtype == FE6_ITEM_HAND_AXE)
                wtype = ITEM_AXE_HANDAXE;
            else if (fe6 && wtype == FE6_ITEM_TOMAHAWK)
                wtype = ITEM_AXE_TOMAHAWK;
            else if (fe6 || (wtype != FE7_ITEM_HAND_AXE && wtype != FE7_ITEM_TOMAHAWK))
                continue;
        }
        if ((idx = ForeignBanim(g, Rd16(list + 2), 0, why, whySize)) == 0)
            return 0;
        sAnimDefs[k][n].wtype = wtype;
        sAnimDefs[k][n].index = (u16)idx;
        n++;
        if (wtype == ITEM_AXE_HANDAXE && n < 7)
        {
            sAnimDefs[k][n].wtype = ITEM_AXE_HATCHET;
            sAnimDefs[k][n].index = (u16)idx;
            n++;
        }
    }
    /* borrowed FE8 lance entries (D2: Great Lord keeps a lance rank and has no lance animation) */
    if (s->lanceFrom != 0)
    {
        const struct BattleAnimDef *d = GetClassData(s->lanceFrom)->pBattleAnimDef;

        for (; d != NULL && d->wtype != 0 && n < 7; d++)
            if (d->wtype == (0x100 | ITYPE_LANCE) || (d->wtype < 0x100 && GetItemType(d->wtype) == ITYPE_LANCE))
                sAnimDefs[k][n++] = *d;
    }
    sAnimDefs[k][n].wtype = 0;
    sAnimDefs[k][n].index = 0;
    /* map sprites (the source game's, unless the row shows an FE8 class's) */
    if (s->smsFrom == 0 && (LzSize(g, Rd32(sms + 4), 0x1000) == 0 || Rd16(sms + 2) > UNIT_ICON_SIZE_32x32 ||
                            LzSize(g, Rd32(mms), 0x4000) == 0 || GamePtr(g, Rd32(mms + 4)) == NULL))
    {
        snprintf(why, whySize, "%s class 0x%02X: map sprite missing", game, s->srcJid);
        return 0;
    }
    if (fe6)
    {
        /* names and descriptions written here (the translation stores its English in the Japanese font) */
        snprintf(sName[k], sizeof(sName[k]), "%s", s->name);
        snprintf(sDesc[k], sizeof(sDesc[k]), "%s", s->desc);
    }
    else if (Fe7Text(s->nameId, sName[k], sizeof(sName[k])) <= 0 ||
             Fe7Text(s->descId, sDesc[k], sizeof(sDesc[k])) <= 0)
    {
        snprintf(why, whySize, "FE7 class 0x%02X: name or description does not decode", s->srcJid);
        return 0;
    }
    if (s->name != NULL)
        snprintf(sName[k], sizeof(sName[k]), "%s", s->name);
    if (s->smsFrom != 0)
        *move = unit_icon_move_table[s->smsFrom - 1]; /* v1.20.0: an FE8 class's (the standing one: SMSId below) */
    else
    {
        wait->pattern = Rd16(sms);
        wait->size = Rd16(sms + 2);
        wait->sheet = (char *)GamePtr(g, Rd32(sms + 4));
        move->sheet = (char *)GamePtr(g, Rd32(mms));
        move->motion = (char *)GamePtr(g, Rd32(mms + 4));
    }

    *cd = *GetClassData(s->templ); /* portrait, sort order, terrain bonuses (FE6: and promotion gains) */
    if (fe6)
        memcpy((u8 *)cd + 0x0B, src + 0x0B, 0x21 - 0x0B + 1); /* bases, caps, power, growths (FE6 has no gains) */
    else
        memcpy((u8 *)cd + 0x0B, src + 0x0B, 0x27 - 0x0B + 1); /* bases, caps, power, growths, promotion gains */
    memcpy(cd->baseRanks, src + g->ranksAt, sizeof(cd->baseRanks));
    if (fe6)
        for (t = 0; t < 8; t++)
            cd->baseRanks[t] = Fe6Rank(cd->baseRanks[t]);
    for (t = 0; t < 8 && s->ranks[t] == 0; t++)
        ;
    if (t < 8)
        memcpy(cd->baseRanks, s->ranks, sizeof(cd->baseRanks));
    if (s->gainsFrom != 0)
    {
        const struct ClassData *gc = GetClassData(s->gainsFrom);

        cd->promotionHp = gc->promotionHp;
        cd->promotionPow = gc->promotionPow;
        cd->promotionSkl = gc->promotionSkl;
        cd->promotionSpd = gc->promotionSpd;
        cd->promotionDef = gc->promotionDef;
        cd->promotionRes = gc->promotionRes;
    }
    if (s->gainsFromSrc != 0) /* v1.20.0: FE6's rule, this class's bases minus the base class's (none below 0) */
    {
        const u8 *b = GamePtr(g, g->classes + s->gainsFromSrc * g->rowSize);
        int d[6], i;

        if (b == NULL || b[4] != s->gainsFromSrc)
        {
            snprintf(why, whySize, "%s class 0x%02X: its base class 0x%02X is not where it should be", game, s->srcJid,
                s->gainsFromSrc);
            return 0;
        }
        for (i = 0; i < 6; i++)
            d[i] = (s8)src[0x0B + i] - (s8)b[0x0B + i] > 0 ? (s8)src[0x0B + i] - (s8)b[0x0B + i] : 0;
        cd->promotionHp = d[0];
        cd->promotionPow = d[1];
        cd->promotionSkl = d[2];
        cd->promotionSpd = d[3];
        cd->promotionDef = d[4];
        cd->promotionRes = d[5];
    }
    if (s->basesFrom != 0)
    {
        const struct ClassData *b = GetClassData(s->basesFrom);

        cd->baseHP = b->baseHP + 2;
        cd->basePow = b->basePow + 2;
        cd->baseSkl = b->baseSkl + 2;
        cd->baseSpd = b->baseSpd + 2;
        cd->baseDef = b->baseDef + 2;
        cd->baseRes = b->baseRes + 2;
        cd->baseCon = b->baseCon;
        cd->baseMov = b->baseMov;
    }
    /* caps: the game's and the save's limits (HP 60, others 30) */
    if (cd->maxHP > 60) cd->maxHP = 60;
    if (cd->maxPow > 30) cd->maxPow = 30;
    if (cd->maxSkl > 30) cd->maxSkl = 30;
    if (cd->maxSpd > 30) cd->maxSpd = 30;
    if (cd->maxDef > 30) cd->maxDef = 30;
    if (cd->maxRes > 30) cd->maxRes = 30;
    if (cd->maxCon > 30) cd->maxCon = 30;
    cd->nameTextId = FE7_VMSG_FIRST + k * 2;
    cd->descTextId = FE7_VMSG_FIRST + k * 2 + 1;
    cd->number = jid;
    cd->promotion = s->promotion;
    cd->SMSId = s->smsFrom != 0 ? GetClassData(s->smsFrom)->SMSId : FE7_SMS_FIRST + k;
    cd->slowWalking = src[7];
    cd->attributes = (Rd32(src + g->attrAt) & ~(u32)CA_FEMALE & ~(fe6 ? FE6_ATTR_CLEAR : 0u) & ~s->attrClear) | s->attrAdd;
    if (s->portraitFrom != 0)
        cd->defaultPortraitId = GetClassData(s->portraitFrom)->defaultPortraitId;
    cd->pBattleAnimDef = sAnimDefs[k];
    if (s->mov[0] != NULL) /* else the template's movement and terrain tables */
    {
        cd->pMovCostTable[0] = s->mov[0];
        cd->pMovCostTable[1] = s->mov[1];
        cd->pMovCostTable[2] = s->mov[2];
        cd->pTerrainAvoidLookup = TerrainTable_Avo_Common;
        cd->pTerrainDefenseLookup = TerrainTable_Def_Common;
        cd->pTerrainResistanceLookup = TerrainTable_Res_Common;
    }
    return 1;
}

/* The log-only self-test: FE7 text 0x493 and the Blade Lord animation 0x11's script. */
static void SelfTest(void)
{
    char text[32], why[160];
    u32 *script, n = 0, i, frames = 0;
    int textOk = Fe7Text(0x493, text, sizeof(text)) > 0 && strcmp(text, "Blade Lord") == 0;

    int inRange = 1;

    /* ForeignScript rejects a script with a sheet pointer outside FE7's ROM; checked again here, rebased,
       against the window */
    script = ForeignScript(&sGames[GAME_FE7], 0x11, 0, &n, why, sizeof(why));
    if (script != NULL)
    {
        for (i = 0; i < n / 4; i++)
            if (script[i] >> 24 == 0x86)
            {
                frames++;
                inRange &= script[i + 1] - FE7_WINDOW < FE7_SIZE;
                i += 2;
            }
        free(script);
        if (inRange)
            snprintf(why, sizeof(why), "%u bytes, %u frames, every sheet pointer in range: OK", (unsigned)n,
                (unsigned)frames);
        else
            snprintf(why, sizeof(why), "a sheet pointer out of range: WRONG");
    }
    PortLog("[fe7] self-test: text 0x493 '%s' %s; animation 0x11 script %s\n", textOk ? text : "?",
        textOk ? "OK" : "WRONG", why);
}

/* FE6's log-only self-test: the King's animation 0x76 (the largest FE6 script, and the one with C3D): every
   sheet pointer inside the heap copy, no C3D or C43 left, its C3Ds now C2B. */
static void Fe6SelfTest(void)
{
    struct Game *g = &sGames[GAME_FE6];
    char why[200];
    u32 *script, n = 0, i, frames = 0, c2b = 0, left = 0;
    int inRange = 1, c3d = g->c3d, c43 = g->c43, c47 = g->c47;

    script = ForeignScript(g, 0x76, 0, &n, why, sizeof(why));
    g->c3d = c3d, g->c43 = c43, g->c47 = c47; /* the log line's counts are the classes' */
    if (script != NULL)
    {
        for (i = 0; i < n / 4; i++)
            if (script[i] >> 24 == 0x86)
            {
                frames++;
                inRange &= (uintptr_t)script[i + 1] - (uintptr_t)g->base < g->size;
                i += 2;
            }
            else if (script[i] >> 24 == 0x85)
            {
                c2b += (script[i] & 0xFF) == 0x2B;
                left += (script[i] & 0xFF) == 0x3D || (script[i] & 0xFF) == 0x43;
            }
        free(script);
        snprintf(why, sizeof(why), "%u bytes (buffer 0x%X), %u frames, %s, %u C2B, %u C3D/C43 left: %s", (unsigned)n,
            SCRIPT_MAX, (unsigned)frames, inRange ? "every sheet pointer in the copy" : "a sheet pointer OUTSIDE the copy",
            (unsigned)c2b, (unsigned)left, inRange && left == 0 && c2b > 0 ? "OK" : "WRONG");
    }
    PortLog("[fe6] self-test: the King's animation 0x76 script %s\n", why);
}

static void LogClass(int k)
{
    const struct ClassData *cd = GetClassData(FE7_FIRST_JID + k);
    char list[96];
    int j, len = 0;

    for (j = 0; sAnimDefs[k][j].wtype != 0 && len < (int)sizeof(list) - 12; j++)
        len += snprintf(list + len, sizeof(list) - len, "%s%X:%X", j ? " " : "", sAnimDefs[k][j].wtype,
            sAnimDefs[k][j].index - 1);
    list[len] = '\0';
    PortLog("[%s] 0x%02X %s: bases %d/%d/%d/%d/%d/%d con %d mov %d, caps %d/%d/%d/%d/%d/%d, gains %d/%d/%d/%d/"
            "%d/%d, ranks %d %d %d %d %d %d %d %d, attributes 0x%08X, animations %s\n", sGames[sSpecs[k].game].tag,
        FE7_FIRST_JID + k, sName[k], cd->baseHP, cd->basePow, cd->baseSkl, cd->baseSpd, cd->baseDef, cd->baseRes,
        cd->baseCon, cd->baseMov, cd->maxHP, cd->maxPow, cd->maxSkl, cd->maxSpd, cd->maxDef, cd->maxRes,
        cd->promotionHp, cd->promotionPow, cd->promotionSkl, cd->promotionSpd, cd->promotionDef, cd->promotionRes,
        cd->baseRanks[0], cd->baseRanks[1], cd->baseRanks[2], cd->baseRanks[3], cd->baseRanks[4],
        cd->baseRanks[5], cd->baseRanks[6], cd->baseRanks[7], (unsigned)cd->attributes, list);
}

/* The FE7 (USA) ROM: FE8PC_FE7_ROM, else the first one next to the exe, then next to the FE8 ROM, then (v1.22.0) in
   the "roms" folder next to the exe. */
static int Fe7Find(const char *romPath)
{
    struct Game *g = &sGames[GAME_FE7];
    char dir[1024], why[256];
    const char *env = getenv("FE8PC_FE7_ROM");
    int ok;

    why[0] = '\0';
    if (env != NULL && env[0] != '\0')
    {
        snprintf(g->path, sizeof(g->path), "%s", env);
        ok = TryLoad(g->path, why, sizeof(why));
        if (!ok)
            PortLog("[fe7] FE8PC_FE7_ROM '%s': %s; the FE7 classes are absent\n", g->path, why);
        return ok;
    }
    ExeDir(dir, sizeof(dir));
    ok = ScanDir(GAME_FE7, dir, g->path, sizeof(g->path), why, sizeof(why));
    if (!ok)
    {
        char romDir[1024];

        DirOf(romPath, romDir, sizeof(romDir));
        if (strcmp(romDir, dir) != 0)
            ok = ScanDir(GAME_FE7, romDir, g->path, sizeof(g->path), why, sizeof(why));
        if (!ok)
        {
            char roms[1024];

            RomsDir(roms, sizeof(roms));
            if (strcmp(roms, romDir) != 0)
                ok = ScanDir(GAME_FE7, roms, g->path, sizeof(g->path), why, sizeof(why));
        }
    }
    if (!ok)
        PortLog("[fe7] no FE7 (USA) ROM found (FE8PC_FE7_ROM is not set, none next to the exe, the FE8 ROM or in the "
                "roms folder): the FE7 classes are absent\n");
    return ok;
}

/* The FE6 ROM (v1.19.0): FE8PC_FE6_ROM, else the first one that passes the checks next to the exe, the FE8
   ROM or the FE7 ROM (when one was loaded), then (v1.22.0) in the "roms" folder next to the exe. */
static int Fe6Find(const char *romPath)
{
    struct Game *g = &sGames[GAME_FE6];
    char dirs[4][1024], why[256];
    const char *env = getenv("FE8PC_FE6_ROM");
    int nd = 0, i, j;

    why[0] = '\0';
    if (env != NULL && env[0] != '\0')
    {
        snprintf(g->path, sizeof(g->path), "%s", env);
        if (Fe6TryLoad(g->path, why, sizeof(why)))
            return 1;
        PortLog("[fe6] FE8PC_FE6_ROM '%s' rejected: %s; the FE6 classes are absent\n", g->path, why);
        return 0;
    }
    ExeDir(dirs[nd++], sizeof(dirs[0]));
    DirOf(romPath, dirs[nd++], sizeof(dirs[0]));
    if (sGames[GAME_FE7].loaded)
        DirOf(sGames[GAME_FE7].path, dirs[nd++], sizeof(dirs[0]));
    RomsDir(dirs[nd++], sizeof(dirs[0]));
    for (i = 0; i < nd; i++)
    {
        for (j = 0; j < i && strcmp(dirs[j], dirs[i]) != 0; j++)
            ;
        if (j == i && ScanDir(GAME_FE6, dirs[i], g->path, sizeof(g->path), why, sizeof(why)))
            return 1;
    }
    PortLog("[fe6] no FE6 ROM found (FE8PC_FE6_ROM is not set; no .gba file with the FE6 header that passes the checks "
            "next to the exe, the FE8 ROM, the FE7 ROM or in the roms folder): the FE6 classes are absent\n");
    return 0;
}

static void Fe7AfterRomLoad(const char *romPath)
{
    char why[256];
    int k, built[GAME_COUNT] = { 0 }, poolFe7 = 0;

    why[0] = '\0';
    sGames[GAME_FE7].loaded = Fe7Find(romPath);
    sGames[GAME_FE6].loaded = Fe6Find(romPath);
    /* FE7's classes first: their animation slots stay v1.18.0's */
    for (k = 0; k < FOREIGN_COUNT; k++)
    {
        if (k == FE6_FIRST_JID - FE7_FIRST_JID)
            poolFe7 = sPoolUsed;
        if (!sGames[sSpecs[k].game].loaded)
            continue;
        sClassOk[k] = (u8)BuildClass(k, why, sizeof(why));
        if (sClassOk[k])
            built[sSpecs[k].game]++;
        else
            PortLog("[%s] class 0x%02X left out: %s\n", sGames[sSpecs[k].game].tag, FE7_FIRST_JID + k, why);
    }
    if (sGames[GAME_FE7].loaded)
    {
        PortLog("[fe7] FE7 (USA) ROM '%s' loaded at 0x%08X, SHA-1 OK: %d of %d FE7 classes (0x80-0x88: %s, %s, %s, %s, "
                "%s, %s, %s, %s, %s), %d of %d animation slots, %d cape (0x47) and %d dark aura (0x3D) commands removed\n",
            sGames[GAME_FE7].path, FE7_WINDOW, built[GAME_FE7], FE7_CLASS_COUNT, sName[0], sName[1], sName[2], sName[3],
            sName[4], sName[5], sName[6], sName[7], sName[8], poolFe7, POOL_SIZE, sGames[GAME_FE7].c47,
            sGames[GAME_FE7].c3d);
        for (k = 0; k < FE7_CLASS_COUNT; k++)
            if (sClassOk[k])
                LogClass(k);
        SelfTest();
        /* Eirika's Lunar Brace and Ephraim's Solar Brace: their Lord classes' second choice (the randomizer's
           promotion snapshot, taken at the first game start, keeps it; Random promotions never changes a
           lord's row) */
        if (Reforged_Fe7ClassAvailable(FE7_JID_BLADE_LORD_E))
            ((u8 (*)[2])gPromoJidLut)[CLASS_EIRIKA_LORD][1] = FE7_JID_BLADE_LORD_E;
        if (Reforged_Fe7ClassAvailable(FE7_JID_GREAT_LORD_E))
            ((u8 (*)[2])gPromoJidLut)[CLASS_EPHRAIM_LORD][1] = FE7_JID_GREAT_LORD_E;
    }
    if (sGames[GAME_FE6].loaded)
    {
        static const int sMyrrhAnims[3] = { FE6_ANIM_GODD_HE1, FE6_ANIM_GODD_MG1, FE6_ANIM_GODD_HK1 };
        int i, ok = 1;

        /* the Myrrh look (v1.20.0): FE6's transform in, dragon and transform out, after the classes (godd_mg1 may
           already be the Divine Dragon's); godd_hk1's mode table is valid for modes 0-1 only */
        for (i = 0; i < 3 && ok; i++)
        {
            int idx = ForeignBanim(&sGames[GAME_FE6], sMyrrhAnims[i], sMyrrhAnims[i] == FE6_ANIM_GODD_HK1, why, sizeof(why));

            ok = idx != 0;
            sMyrrhBanim[i] = idx - 1;
        }
        if (!ok)
        {
            sMyrrhBanim[0] = sMyrrhBanim[1] = sMyrrhBanim[2] = 0;
            PortLog("[fe6] the Myrrh look is not available: %s\n", why);
        }
        else
            PortLog("[fe6] the Myrrh look is available (Helpers, \"Myrrh look\"): FE6's godd_he1 / godd_mg1 / godd_hk1 as "
                    "banims 0x%02X / 0x%02X / 0x%02X (FE8's 0x%02X / 0x%02X / 0x%02X)\n", sMyrrhBanim[0], sMyrrhBanim[1],
                sMyrrhBanim[2], BANIM_INDEX_MYRRH_INTRO - 1, BANIM_INDEX_MYRRH_MAIN - 1, BANIM_INDEX_MYRRH_EXIT - 1);
        PortLog("[fe6] FE6 ROM '%s' loaded (%u bytes, header FIREEMBLEM6/AFEJ, structure and table checks OK; read into "
                "the heap at %p): %d of %d FE6 classes (0x89-0x8F: %s, %s, %s, %s, %s, %s, %s), %d of %d animation slots in "
                "use, %d C3D (to C2B), %d C43 and %d C47 (to C00) commands rewritten\n",
            sGames[GAME_FE6].path, (unsigned)sGames[GAME_FE6].size, (const void *)sGames[GAME_FE6].base, built[GAME_FE6],
            FOREIGN_COUNT - FE7_CLASS_COUNT, sName[9], sName[10], sName[11], sName[12], sName[13], sName[14], sName[15],
            sPoolUsed, POOL_SIZE, sGames[GAME_FE6].c3d, sGames[GAME_FE6].c43, sGames[GAME_FE6].c47);
        for (k = FE7_CLASS_COUNT; k < FOREIGN_COUNT; k++)
            if (sClassOk[k])
                LogClass(k);
        Fe6SelfTest();
    }
    /* v1.19.0: with both ROMs, the FE7 Nomad / Nomad F's second promotion choice is the FE6 Nomad Trooper (the
       first stays FE8's Ranger: the owner's Nomad -> Ranger). Rows 0x80+ are never read by the randomizer. */
    if (Reforged_Fe7ClassAvailable(FE7_JID_NOMAD) && Reforged_Fe7ClassAvailable(FE6_JID_TROOPER))
        ((u8 (*)[2])gPromoJidLut)[FE7_JID_NOMAD][1] = FE6_JID_TROOPER;
    if (Reforged_Fe7ClassAvailable(FE7_JID_NOMAD_F) && Reforged_Fe7ClassAvailable(FE6_JID_TROOPER_F))
        ((u8 (*)[2])gPromoJidLut)[FE7_JID_NOMAD_F][1] = FE6_JID_TROOPER_F;
    if (gPromoJidLut[FE7_JID_NOMAD][1] == FE6_JID_TROOPER || gPromoJidLut[FE7_JID_NOMAD_F][1] == FE6_JID_TROOPER_F)
        PortLog("[fe6] FE7 Nomad promotions: Ranger or Nomad Trooper (0x80 -> 0x%02X / 0x%02X, 0x81 -> 0x%02X / 0x%02X)\n",
            gPromoJidLut[FE7_JID_NOMAD][0], gPromoJidLut[FE7_JID_NOMAD][1], gPromoJidLut[FE7_JID_NOMAD_F][0],
            gPromoJidLut[FE7_JID_NOMAD_F][1]);
}

__attribute__((constructor)) static void Fe7Register(void)
{
    gPortAfterRomLoad = Fe7AfterRomLoad;
}

/* 1 when jid is a foreign class (FE7's 0x80-0x88, FE6's 0x89-0x8C) that was built in this run. */
int Reforged_Fe7ClassAvailable(int jid)
{
    return jid >= FE7_FIRST_JID && jid < FE7_FIRST_JID + FOREIGN_COUNT && sClassOk[jid - FE7_FIRST_JID];
}

/* Reforged_VirtualMsg's ids 0x7F14-0x7F2D: k = id - 0x7F14 (even: a name, odd: its description). */
int Reforged_Fe7VirtualMsg(int k, char *buf)
{
    if (k < 0 || k >= FOREIGN_COUNT * 2 || !Reforged_Fe7ClassAvailable(FE7_FIRST_JID + k / 2))
        return 0;
    /* v1.23.0: in a game with the Game fix "Third promotion" on, the row's description (desc3: what the class can
       equip); in any other game v1.22.0's (the FE7 ROM's text, the FE6 rows' three lines) */
    if ((k & 1) && sSpecs[k / 2].desc3 != NULL && Reforged_Promo3Active())
        strcpy(buf, sSpecs[k / 2].desc3);
    else
        strcpy(buf, (k & 1) ? sDesc[k / 2] : sName[k / 2]);
    return 1;
}

/* ------------------------------------------------------------------ the Myrrh look (v1.20.0) */

int Reforged_MyrrhLookAvailable(void)
{
    return sMyrrhBanim[0] != 0 && sMyrrhBanim[1] != 0 && sMyrrhBanim[2] != 0;
}

/* src/banim-ekrdragon-myrrh.c EkrMyr_PrepareBanimfx (RFO_MYRRH_START): whether this dragon is FE6's. Myrrh's (the
   character; FE8 has her alone in the class that transforms) with the option on and the animations loaded. Test
   switch FE8PC_TEST_MYRRH_LOOK_PID=pid: that character counts as Myrrh too (for a test unit put in her class with
   FE8PC_TEST_UNIT_CLASS / FE8PC_TEST_ARENA_CLASS); a replay of that unit's fights desyncs. */
void Reforged_MyrrhLookStart(struct Anim *anim)
{
    int pos = GetAnimPosition(anim) == EKR_POS_L ? 0 : 1;
    const struct BattleUnit *bu = pos == 0 ? gpEkrBattleUnitLeft : gpEkrBattleUnitRight;
    const char *test = getenv("FE8PC_TEST_MYRRH_LOOK_PID");
    int pid = bu != NULL && bu->unit.pCharacterData != NULL ? bu->unit.pCharacterData->number : 0;
    int on = Reforged_GetOption(RFO_MYRRH_LOOK) && Reforged_MyrrhLookAvailable() &&
             (pid == CHARACTER_MYRRH || (test != NULL && pid != 0 && pid == (int)strtol(test, NULL, 0)));

    sMyrrhOn[pos] = (u8)on;
    if (Reforged_MyrrhLookAvailable()) /* (without the FE6 ROM: nothing to say) */
    {
        /* the battle's strikes as the animation will play them (h hit, c critical, m miss; the dragon's in capitals), for
           the log */
        char rounds[2 * BATTLE_HIT_MAX + 1];
        int i, n = 0, actor = bu == &gBattleActor;

        for (i = 0; i < BATTLE_HIT_MAX && n < (int)sizeof(rounds) - 1; i++)
        {
            const struct BattleHit *h = &gBattleHitArray[i];
            int mine = !(h->info & BATTLE_HIT_INFO_RETALIATION) == actor;
            char c = (h->attributes & BATTLE_HIT_ATTR_MISS) ? 'm' : (h->attributes & BATTLE_HIT_ATTR_CRIT) ? 'c' : 'h';

            if (h->info & BATTLE_HIT_INFO_END) /* the entry after the last strike */
                break;
            rounds[n++] = mine ? (char)(c - 'a' + 'A') : c;
        }
        rounds[n] = '\0';
        PortLog("[fe6] frame %u: manakete transform, %s side, pid 0x%02X: %s (strikes %s)\n",
            (unsigned)gPortFrameCount, pos == 0 ? "left" : "right", pid,
            on ? "FE6's Divine Dragon (Myrrh look)" : "FE8's dragon", rounds);
    }
}

/* RFO_MYRRH_BANIM: FE8's transform in / dragon / transform out banim index, or FE6's while this dragon is FE6's. */
int Reforged_MyrrhLookBanim(struct Anim *anim, int index)
{
    int pos = GetAnimPosition(anim) == EKR_POS_L ? 0 : 1;

    if (!sMyrrhOn[pos])
        return index;
    if (index == BANIM_INDEX_MYRRH_INTRO - 1)
        return sMyrrhBanim[0];
    if (index == BANIM_INDEX_MYRRH_MAIN - 1)
        return sMyrrhBanim[1];
    if (index == BANIM_INDEX_MYRRH_EXIT - 1)
        return sMyrrhBanim[2];
    return index;
}

/* RFO_MYRRH_PAL: the transform's two palettes, as FE6 loads them (pal holds FE8's transform palettes, which the
   caller copies to the unit's two object palettes). With FE6's dragon: the transform's own palette (gBanimIdx, just
   prepared: godd_he1 or godd_hk1) and the dragon's (godd_mg1), each the row of the unit's faction. */
void Reforged_MyrrhLookPal(struct Anim *anim, void *pal)
{
    int pos = GetAnimPosition(anim) == EKR_POS_L ? 0 : 1;
    int row = gBanimFactionPal[pos] & 3;
    u16 buf[PAL_MAX / 2];

    if (!sMyrrhOn[pos])
        return;
    LZ77UnCompWram(banim_data[gBanimIdx[pos]].pal, buf);
    memcpy(pal, buf + row * 16, 32);
    LZ77UnCompWram(banim_data[sMyrrhBanim[1]].pal, buf);
    memcpy((u16 *)pal + 16, buf + row * 16, 32);
}

/* ------------------------------------------------------------------ promotions (D4) */

/* The promotion screen's third choice for a class (src/classchg-sel.c, classchg-menuselect.c,
   reforged_skills.c Reforged_SkillPromoChoices): Blade Lord for a Myrmidon of either sex (FE7's art is
   Lyn's; there is no male twin), beside Assassin and Swordmaster; Great Lord for a Knight of either sex
   (Hector's art; no female twin), beside General and Great Knight; v1.17.0, in a randomized run with
   "FE7 classes" and Random promotions, the run's rare Archsage or Dark Druid for a magic class's row
   (reforged_rando_classes.c Reforged_RandoFe7Promo; never a Myrmidon's or Knight's). 0: none. */
int Reforged_Fe7ThirdPromo(int jid)
{
    if ((jid == CLASS_MYRMIDON || jid == CLASS_MYRMIDON_F) && Reforged_Fe7ClassAvailable(FE7_JID_BLADE_LORD))
        return FE7_JID_BLADE_LORD;
    if ((jid == CLASS_ARMOR_KNIGHT || jid == CLASS_ARMOR_KNIGHT_F) && Reforged_Fe7ClassAvailable(FE7_JID_GREAT_LORD))
        return FE7_JID_GREAT_LORD;
    return Reforged_RandoFe7Promo(jid); /* v1.17.0: a randomized run's rare Archsage / Dark Druid (magic classes) */
}

/* ------------------------------------------------------------------ Chapter 16's promotion (v1.16.1) */

/* The owner's decision of 2026-11-08: Chapter 16's "promote now?" answered yes opens the class choice
   for each twin (Master Lord, shown "Great Lord", or their FE7 lord), as the brace does. src/eventscr.c
   Event40_PromoteUnit (PROM) asks first (RFO_PROMOTE_MENU, include/bmunit.h); here, for a twin still in
   their own Lord class whose FE7 lord is loaded (the row's second choice, set at boot), instead of the
   event's direct promotion (EventPromoteUnitExt) the command starts this proc, blocking the event
   engine, and this proc starts the game's promotion handler (ProcScr_PromoHandler) as the preparations'
   brace use does (src/classchg-handler.c StartPrepScreenPromotion: PROMO_HANDLER_TYPE_BM, no item
   slot: the event's brace is not in the inventory). The handler shows the same choice screen, preview,
   description, and on "Change" runs the same promotion (ClassChgExecPromotionReal: ExecUnitPromotion
   with the class chosen, the animation with the brace (the event's item) and the same battle config,
   then the stat screen). B does nothing there (RFO_PROMO_NO_CANCEL, classchg-menuselect.c: the
   trainee screen's rule): the promotion is not optional. When the handler ends, the screen is reset
   as the event command's own end does (WaitEventPromoteDone: sub_8012324 and the event mark), and the
   event goes on (REMOVEPORTRAITS, BACG, FADU). Any other PROM (no FE7 ROM, a randomized or already
   promoted twin, another unit) is the game's. v1.21.0: in a randomizer version 11-14 run every twin still in
   an unpromoted class gets the same screen with the run's three choices (reforged_rando_classes.c
   Reforged_RandoEventPromoMenu; a promoted twin is skipped, Reforged_RandoPromoteJid), FE7 ROM or not; v1.23.0: the same in
   a game without the randomizer with the Game fix "Third promotion" on (the owner's three). */
extern CONST_DATA struct ProcCmd ProcScr_PromoHandler[]; /* src/classchg-handler.c */
void sub_8012324(void);                                 /* src/eventscr3.c */

struct Fe7EvPromoProc {
    PROC_HEADER;
    /* 2C */ struct Proc *engine;
    /* 30 */ u8 pid;
    /* 31 */ u8 jidBefore;
    /* 34 */ u32 unused[6]; /* 0x40: PromoMain_InitScreen stores the game lock in its handler's parent */
};

static void Fe7EvPromo_End(struct Fe7EvPromoProc *proc)
{
    struct Unit *unit = GetUnitFromCharId(proc->pid);

    PortLog("[fe7] chapter 16 promotion: pid 0x%02X class 0x%02X -> 0x%02X\n", proc->pid, proc->jidBefore,
        unit != NULL && unit->pClassData != NULL ? unit->pClassData->number : 0);
    sub_8012324();
    Proc_SetMark(proc->engine, PROC_MARK_EVENT);
}

static const struct ProcCmd sProcScr_Fe7EvPromo[] = {
    PROC_NAME("rfo_fe7_evpromo"),
    PROC_YIELD, /* the handler blocks this proc until it ends */
    PROC_CALL(Fe7EvPromo_End),
    PROC_END,
};

int Reforged_Fe7EventPromoMenu(void *evproc, struct Unit *unit, int jid, int item)
{
    struct Fe7EvPromoProc *proc;
    struct ProcPromoHandler *h;
    int lord, fe7, weapon;

    _Static_assert(offsetof(struct Fe7EvPromoProc, unused) <= offsetof(struct ProcPromoHandler, game_lock),
        "Fe7EvPromoProc: the handler's game lock store must land in the padding");
    if (unit == NULL || unit->pClassData == NULL || unit->pCharacterData == NULL)
        return 0;
    lord = unit->pClassData->number;
    if (jid != 0 && Reforged_RandoEventPromoMenu(unit))
        fe7 = 0; /* v1.21.0: a randomizer version 11-14 run, the twin's three choices (reforged_rando_classes.c) */
    else if (lord == CLASS_EIRIKA_LORD && jid == CLASS_EIRIKA_MASTER_LORD)
        fe7 = FE7_JID_BLADE_LORD_E;
    else if (lord == CLASS_EPHRAIM_LORD && jid == CLASS_EPHRAIM_MASTER_LORD)
        fe7 = FE7_JID_GREAT_LORD_E;
    else
        return 0;
    if (fe7 != 0 && (!Reforged_Fe7ClassAvailable(fe7) || gPromoJidLut[lord][0] != jid || gPromoJidLut[lord][1] != fe7))
        return 0;

    proc = Proc_StartBlocking(sProcScr_Fe7EvPromo, evproc);
    proc->engine = evproc;
    proc->pid = unit->pCharacterData->number;
    proc->jidBefore = lord;
    Proc_SetMark(evproc, PROC_MARK_EVENT_ANIM);
    SetUnitStatus(unit, 0); /* as Event40_PromoteUnit */

    /* StartPrepScreenPromotion's battle units: the brace before, the equipped weapon */
    gBattleActor.weaponBefore = gBattleTarget.weaponBefore = item;
    weapon = GetUnitEquippedWeapon(unit);
    gBattleActor.weapon = gBattleTarget.weapon = weapon;
    gBattleTarget.statusOut = -1;

    h = Proc_StartBlocking(ProcScr_PromoHandler, proc);
    h->bmtype = PROMO_HANDLER_TYPE_BM;
    h->u32 = 0;
    h->pid = proc->pid;
    h->unit = unit;
    h->item_slot = -1;
    if (fe7 != 0)
        PortLog("[fe7] chapter 16 promotion: pid 0x%02X, class choice (0x%02X or 0x%02X)\n", proc->pid, jid, fe7);
    else
        PortLog("[rando] chapter 16 promotion: pid 0x%02X, class 0x%02X: the three-choice screen\n", proc->pid, lord);
    return 1;
}

/* src/classchg-menuselect.c ClassChgMenuSelOnPressB: 1 for the handler started above (B does nothing). */
int Reforged_Fe7PromoNoCancel(void *handler)
{
    struct Proc *parent = handler != NULL ? ((struct Proc *)handler)->proc_parent : NULL;

    return parent != NULL && parent->proc_script == sProcScr_Fe7EvPromo;
}

/* ------------------------------------------------------------------ saves (D5) */

/* The FE8 class a save stores for an FE7 class (for a build or a run without the FE7 classes, which
   must never read an id past FE8's 0x7F): Nomad -> Archer (F), Blade Lord -> Swordmaster (F, by the
   unit's sex), Eirika's Blade Lord -> Eirika's Master Lord, Great Lord -> General (F, by sex), Ephraim's
   Great Lord -> Ephraim's Master Lord, Archsage -> Sage (F), Dark Druid -> Druid (F), Bard -> Dancer (v1.18.0;
   the stand-in keeps Dance); v1.19.0's FE6 classes: Thief F -> Thief, Nomad Trooper -> Ranger (F), King ->
   Swordmaster (F, by sex: a sword-only promoted class, so the King's sword rank stays usable); v1.20.0: Lion Lord ->
   Myrmidon (F), Master Lord -> Swordmaster (F) (sword-only classes of the same tier; not the twins' lord classes,
   which carry their own locks and events), Divine Dragon -> Myrrh's Manakete (the class that holds the
   Divinestone). Any other id: itself. */
int Reforged_Fe7Fallback(int jid, int female)
{
    switch (jid)
    {
    case FE7_JID_NOMAD:        return CLASS_ARCHER;
    case FE7_JID_NOMAD_F:      return CLASS_ARCHER_F;
    case FE7_JID_BLADE_LORD:   return female ? CLASS_SWORDMASTER_F : CLASS_SWORDMASTER;
    case FE7_JID_BLADE_LORD_E: return CLASS_EIRIKA_MASTER_LORD;
    case FE7_JID_GREAT_LORD:   return female ? CLASS_GENERAL_F : CLASS_GENERAL;
    case FE7_JID_GREAT_LORD_E: return CLASS_EPHRAIM_MASTER_LORD;
    case FE7_JID_ARCHSAGE:     return female ? CLASS_SAGE_F : CLASS_SAGE;
    case FE7_JID_DARK_DRUID:   return female ? CLASS_DRUID_F : CLASS_DRUID;
    case FE7_JID_BARD:         return CLASS_DANCER;
    case FE6_JID_THIEF_F:      return CLASS_THIEF;
    case FE6_JID_TROOPER:      return CLASS_RANGER;
    case FE6_JID_TROOPER_F:    return CLASS_RANGER_F;
    case FE6_JID_KING:         return female ? CLASS_SWORDMASTER_F : CLASS_SWORDMASTER;
    case FE6_JID_LION_LORD:    return female ? CLASS_MYRMIDON_F : CLASS_MYRMIDON;
    case FE6_JID_MASTER_LORD:  return female ? CLASS_SWORDMASTER_F : CLASS_SWORDMASTER;
    case FE6_JID_DIVINE_DRAGON: return CLASS_MANAKETE_MYRRH;
    }
    return jid;
}

static int IsForeign(int jid)
{
    return jid >= FE7_FIRST_JID && jid < FE7_FIRST_JID + FOREIGN_COUNT;
}

/* "FE7" or "FE6": the game foreign class jid comes from (for the log). */
static const char *GameOf(int jid)
{
    return IsForeign(jid) && sSpecs[jid - FE7_FIRST_JID].game == GAME_FE6 ? "FE6" : "FE7";
}

static const char *GameTag(int jid) /* the log prefix */
{
    return sGames[IsForeign(jid) ? sSpecs[jid - FE7_FIRST_JID].game : GAME_FE7].tag;
}

static int UnitFemale(const struct Unit *unit)
{
    return unit->pCharacterData != NULL && (unit->pCharacterData->attributes & CA_FEMALE) != 0;
}

/* The weapon kinds an FE7-class unit has a rank in that its stand-in class cannot use (Blade Lord: the
   bow, for a Swordmaster or Master Lord; Ephraim's Great Lord sword and axe; Archsage dark; Dark Druid
   light), as a mask of bits 0-7 (ITYPE_*); 0 for any other unit. A save
   writes 0 for those ranks, so a build or a run without the FE7 classes (v1.14.0, mGBA) loads a clean
   stand-in that cannot equip them; the real ranks go to the save mark ("<block>_fe7", Fe7MarkUnit). */
static int Fe7StripMask(const struct Unit *unit)
{
    const struct ClassData *fb;
    int c, t, mask = 0;

    if (unit == NULL || unit->pClassData == NULL || unit->pCharacterData == NULL)
        return 0;
    c = unit->pClassData->number;
    if (!IsForeign(c))
        return 0;
    fb = GetClassData(Reforged_Fe7Fallback(c, UnitFemale(unit)));
    for (t = 0; t < 8; t++)
        if (fb != NULL && fb->baseRanks[t] == 0 && unit->ranks[t] != 0)
            mask |= 1 << t;
    return mask;
}

/* The save copy's ranks (src/bmsave.c: the chapter save's wpnRanks, the suspend's ranks) of a unit in an
   FE7 class: 0 for each weapon kind its stand-in class cannot use. */
void Reforged_Fe7StripRanks(const struct Unit *unit, unsigned char *ranks)
{
    int t, mask = Fe7StripMask(unit);

    for (t = 0; t < 8; t++)
        if (mask & (1 << t))
            ranks[t] = 0;
}

/* The save mark's entry for a unit: 1 and *m when it is in an FE7 class, else 0. */
static int Fe7MarkUnit(const struct Unit *u, struct Reforged_Fe7Mark *m)
{
    int t;

    if (u->pCharacterData == NULL || u->pClassData == NULL || !IsForeign(u->pClassData->number))
        return 0;
    memset(m, 0, sizeof(*m));
    m->index = (u8)u->index;
    m->jid = (u8)u->pClassData->number;
    m->mask = (u8)Fe7StripMask(u);
    for (t = 0; t < 8; t++)
        if (m->mask & (1 << t))
            m->ranks[t] = u->ranks[t];
    return 1;
}

/* The chapter save's units in an FE7 class, as Reforged_Fe7PackUnit wrote them: the save mark's
   "game<n>_fe7", taken (and emptied) by reforged_savemark.c Reforged_SaveBlockWritten. */
static struct Reforged_Fe7Mark sPacked[REFORGED_FE7_MARKS];
static int sPackedCount;

/* src/bmsave.c WriteGameSave, before its units are packed: a pack done outside a game save (the link
   arena's team, WriteMultiArenaSaveTeam) leaves nothing for the next slot's line. */
void Reforged_Fe7PackReset(void)
{
    sPackedCount = 0;
}

int Reforged_Fe7TakePacked(struct Reforged_Fe7Mark *out, int max)
{
    int n = sPackedCount < max ? sPackedCount : max;

    memcpy(out, sPacked, n * sizeof(*out));
    sPackedCount = 0;
    return n;
}

/* The chapter save (src/bmsave.c WriteGameSavePackedUnit): a unit in an FE7 class is written with the
   fallback in its 7-bit jid and the FE7 class's slot (1-8) in flag bits 8-11, which the game never
   sets (it tests bits 0-7 only), and rank 0 for the weapon kinds the fallback cannot use (their real
   ranks: the save mark). *jid, *flag and ranks (the save copy's) are updated. v1.15.0 reads bits 8-10
   only: slots 5-7 are past its 4 classes and slot 8 reads as 0, so it keeps the stand-in either way.
   Slot 9 (the Bard, v1.18.0) is bit 12 alone, bits 8-11 zero: "1001" in bits 8-11 would read as slot 1
   (Nomad) in v1.15.0, while bit 12 (the packed flag's last, 13 bits) is read by no older build, so they
   all keep the stand-in (Dancer). v1.19.0's FE6 classes: their row's saveCode in bits 8-12, with bits 8-11
   13, 14 or 15 (v1.15.0 reads slot 5-7, v1.16.0-v1.18.0 slot 13-15: past their classes, so the stand-in
   stays; v1.18.0 reads bit 12 only when bits 8-11 are 0). Two classes may share a code when their stand-ins
   differ (Nomad Trooper M and F: Ranger, Ranger F): Reforged_Fe7UnpackUnit tells them apart by the
   stand-in the save holds. */
void Reforged_Fe7PackUnit(const struct Unit *unit, unsigned *jid, unsigned *flag, unsigned char *ranks)
{
    struct Reforged_Fe7Mark m;
    int c, i;

    if (unit == NULL || unit->pClassData == NULL || unit->pCharacterData == NULL)
        return;
    c = unit->pClassData->number;
    if (!IsForeign(c))
        return;
    *jid = (unsigned)Reforged_Fe7Fallback(c, UnitFemale(unit));
    if (sSpecs[c - FE7_FIRST_JID].game == GAME_FE6)
        *flag = (*flag & ~(0x1Fu << 8)) | ((unsigned)sSpecs[c - FE7_FIRST_JID].saveCode << 8);
    else if (c - FE7_FIRST_JID + 1 <= 8)
        *flag = (*flag & ~(0x1Fu << 8)) | ((unsigned)(c - FE7_FIRST_JID + 1) << 8);
    else
        *flag = (*flag & ~(0x1Fu << 8)) | (1u << 12); /* slot 9: bit 12 alone */
    Reforged_Fe7StripRanks(unit, ranks);
    if (Fe7MarkUnit(unit, &m))
    {
        for (i = 0; i < sPackedCount && sPacked[i].index != m.index; i++)
            ;
        if (i < REFORGED_FE7_MARKS)
        {
            sPacked[i] = m;
            if (i == sPackedCount)
                sPackedCount++;
        }
    }
}

/* src/bmsave.c LoadSavedUnit, after the fallback class is set: the FE7 class back when flag names one
   and it is built in this run (else the unit keeps the fallback). The ranks the save left out come
   back afterwards, from the save mark (Reforged_Fe7MarkRestore). */
void Reforged_Fe7UnpackUnit(struct Unit *unit, unsigned flag)
{
    unsigned code = (flag >> 8) & 0x1F;
    int slot = (int)(code & 0xF), c = 0, k;

    if (code == 0 || unit == NULL || unit->pClassData == NULL)
        return;
    if (slot >= 1 && slot <= 8 && !(code & 0x10))
        c = FE7_FIRST_JID + slot - 1; /* an FE7 class's slot */
    else if (code == 0x10)
        c = FE7_JID_BARD; /* the Bard: bit 12 alone */
    else
        for (k = FE7_CLASS_COUNT; k < FOREIGN_COUNT && c == 0; k++) /* an FE6 class's code (v1.19.0) */
            if (sSpecs[k].saveCode == code &&
                Reforged_Fe7Fallback(FE7_FIRST_JID + k, UnitFemale(unit)) == unit->pClassData->number)
                c = FE7_FIRST_JID + k;
    if (c == 0)
    {
        PortLog("[fe7] chapter save: pid 0x%02X has foreign class code 0x%02X, which this build does not know for class "
                "0x%02X: it stays that class\n", unit->pCharacterData != NULL ? unit->pCharacterData->number : 0, code,
            unit->pClassData->number);
        return;
    }
    if (!Reforged_Fe7ClassAvailable(c))
    {
        PortLog("[%s] chapter save: pid 0x%02X was %s class 0x%02X; the %s classes are not loaded, so it is "
                "class 0x%02X\n", GameTag(c),
            unit->pCharacterData != NULL ? unit->pCharacterData->number : 0, GameOf(c), c, GameOf(c),
            unit->pClassData->number);
        return;
    }
    unit->pClassData = GetClassData(c);
    PortLog("[%s] chapter save: pid 0x%02X is %s class 0x%02X again\n", GameTag(c),
        unit->pCharacterData != NULL ? unit->pCharacterData->number : 0, GameOf(c), c);
}

/* The suspend save's class byte (src/bmsave.c EncodeSuspendSavePackedUnit): the fallback; the real
   class goes to the save mark (reforged_savemark.c "<block>_fe7", Reforged_Fe7SuspendUnits). */
int Reforged_Fe7SuspendJid(const struct Unit *unit)
{
    return Reforged_Fe7Fallback(unit->pClassData->number, UnitFemale(unit));
}

/* The units in an FE7 class now, as the suspend save holds them (class, and the ranks the save left
   out); how many (at most max). */
int Reforged_Fe7SuspendUnits(struct Reforged_Fe7Mark *out, int max)
{
    static struct { struct Unit *units; int count; } const sArrays[3] = {
        { gUnitArrayBlue, UNIT_SAVE_AMOUNT_BLUE },
        { gUnitArrayRed, UNIT_SAVE_AMOUNT_RED },
        { gUnitArrayGreen, UNIT_SAVE_AMOUNT_GREEN },
    };
    int a, i, n = 0;

    for (a = 0; a < 3; a++)
        for (i = 0; i < sArrays[a].count && n < max; i++)
            n += Fe7MarkUnit(&sArrays[a].units[i], &out[n]);
    return n;
}

/* A block was read (reforged_savemark.c Reforged_SaveBlockRead) with "<block>_fe7" entries. A suspend
   block: each recorded unit that holds its class's fallback gets the FE7 class back, when it is built
   in this run. A game block: the class is already back from the unit's flag bits (Reforged_Fe7UnpackUnit).
   Either way a unit now in the recorded FE7 class gets back the ranks the save left out; one that kept
   the fallback (no FE7 classes in this run) stays a plain stand-in, without them. */
void Reforged_Fe7MarkRestore(const struct Reforged_Fe7Mark *list, int n, int suspend)
{
    int i, t;

    for (i = 0; i < n; i++)
    {
        struct Unit *u = GetUnit(list[i].index);
        int c = list[i].jid;

        if (u == NULL || u->pCharacterData == NULL || u->pClassData == NULL)
            continue;
        if (u->pClassData->number == Reforged_Fe7Fallback(c, UnitFemale(u)))
        {
            if (!Reforged_Fe7ClassAvailable(c))
            {
                if (suspend) /* (a chapter save's unit was logged by Reforged_Fe7UnpackUnit) */
                    PortLog("[%s] suspend: pid 0x%02X was %s class 0x%02X; the %s classes are not loaded, so "
                            "it is class 0x%02X\n", GameTag(c), u->pCharacterData->number,
                        GameOf(c), c, GameOf(c), u->pClassData->number);
                Reforged_FixArmedDancer(u); /* a Bard's stand-in Dancer: her light rank, with "Armed dancers" */
                continue;
            }
            if (suspend)
            {
                u->pClassData = GetClassData(c);
                PortLog("[%s] suspend: pid 0x%02X is %s class 0x%02X again\n", GameTag(c),
                    u->pCharacterData->number, GameOf(c), c);
            }
        }
        if (u->pClassData->number != c)
            continue; /* not the unit the mark was written for */
        for (t = 0; t < 8; t++)
            if (list[i].mask & (1 << t))
            {
                u->ranks[t] = list[i].ranks[t];
                PortLog("[fe7] %s: pid 0x%02X has weapon rank %d in weapon kind %d again\n",
                    suspend ? "suspend" : "chapter save", u->pCharacterData->number, list[i].ranks[t], t);
            }
    }
}

/* include/bmitem.h RFO_LANCE_CLASS (GetSpellAnimId): a thrown javelin's effect follows the borrowed lance
   animation (Great Lord: the General's throw needs the General's javelin, effect 0xB). */
int Reforged_Fe7LanceClass(int jid)
{
    if (!Reforged_Fe7ClassAvailable(jid) || sSpecs[jid - FE7_FIRST_JID].lanceFrom == 0)
        return jid;
    return sSpecs[jid - FE7_FIRST_JID].lanceFrom;
}

/* ------------------------------------------------------------------ skills (D6) */

/* The FE8 class whose skills an FE7 class has (reforged_skills.c, which has no rows past 0x7F): Nomad
   an Archer's, Blade Lord a Swordmaster's, Eirika's Blade Lord her Master Lord's (Charm), Great Lord a
   General's, Ephraim's Great Lord his Master Lord's, Archsage a Sage's, Dark Druid a Druid's, Bard a Dancer's
   (Dance); v1.19.0's FE6 classes their stand-ins': Thief F a Thief's, Nomad Trooper a Ranger's (F), King a
   Swordmaster's; v1.20.0: Lion Lord a Myrmidon's, Master Lord a Swordmaster's, Divine Dragon Myrrh's class's. */
int Reforged_Fe7SkillClass(int jid)
{
    if (!Reforged_Fe7ClassAvailable(jid))
        return jid;
    switch (jid)
    {
    case FE7_JID_NOMAD:        return CLASS_ARCHER;
    case FE7_JID_NOMAD_F:      return CLASS_ARCHER_F;
    case FE7_JID_BLADE_LORD:   return CLASS_SWORDMASTER;
    case FE7_JID_BLADE_LORD_E: return CLASS_EIRIKA_MASTER_LORD;
    case FE7_JID_GREAT_LORD:   return CLASS_GENERAL;
    case FE7_JID_GREAT_LORD_E: return CLASS_EPHRAIM_MASTER_LORD;
    case FE7_JID_ARCHSAGE:     return CLASS_SAGE;
    case FE7_JID_DARK_DRUID:   return CLASS_DRUID;
    case FE7_JID_BARD:         return CLASS_DANCER;
    case FE6_JID_THIEF_F:      return CLASS_THIEF;
    case FE6_JID_TROOPER:      return CLASS_RANGER;
    case FE6_JID_TROOPER_F:    return CLASS_RANGER_F;
    case FE6_JID_KING:         return CLASS_SWORDMASTER;
    case FE6_JID_LION_LORD:    return CLASS_MYRMIDON;
    case FE6_JID_MASTER_LORD:  return CLASS_SWORDMASTER;
    case FE6_JID_DIVINE_DRAGON: return CLASS_MANAKETE_MYRRH;
    }
    return jid;
}

/* ------------------------------------------------------------------ test switch */

/* FE8PC_TEST_UNIT_CLASS=pid:jid (played builds; for looking at classes, e.g. the FE7 ones' moving map
   sprites, canto and the promotion screen): at the first player phase of a chapter (src/bm.c
   BmMain_StartPhase on turn 1, or the first player phase after a suspended chapter is resumed,
   BmMain_StartPhase or BmMain_ResumePlayerPhase with gBmSt.just_resumed), once per chapter in a run, the player's unit pid takes class jid,
   with that class's base weapon ranks for the kinds it uses and none for the others (then the "Armed
   dancers" game fix's rank, when it is on and the class is a Dancer or the Bard). FE7 and FE6 classes
   (0x80-0x8F) only when they are loaded. v1.18.0: pid:jid:item also gives the unit that item (FE8's 0x01-0xCD, full uses;
   in its first empty slot, else in place of its last item). A replay desyncs once that unit acts
   differently. v1.23.0: FE8PC_TEST_UNIT_LEVEL=pid:level[,pid:level...] at the same moment sets those player units' levels
   (1-20; for a promotion item, which needs level 10, with the class switch's item), with or without the class switch. */
static void TestUnitLevel(void)
{
    static int sDoneChapter = -1;
    const char *env = getenv("FE8PC_TEST_UNIT_LEVEL"), *p;
    char *end;

    if (env == NULL || gPlaySt.faction != FACTION_BLUE || (gPlaySt.chapterTurnNumber != 1 && !gBmSt.just_resumed) ||
        sDoneChapter == gPlaySt.chapterIndex)
        return;
    sDoneChapter = gPlaySt.chapterIndex;
    for (p = env; *p != '\0';)
    {
        long pid = strtol(p, &end, 0), lv = 0;
        struct Unit *unit;

        if (end == p || *end != ':')
            break;
        lv = strtol(end + 1, &end, 0);
        unit = GetUnitFromCharId((int)pid);
        if (unit != NULL && UNIT_FACTION(unit) == FACTION_BLUE && lv >= 1 && lv <= 20)
        {
            unit->level = (u8)lv;
            PortLog("[reforged] unit level test: chapter %d, pid 0x%02X is now level %ld\n", gPlaySt.chapterIndex,
                (int)pid, lv);
        }
        else
            PortLog("[reforged] unit level test: pid 0x%02X level %ld refused (no player unit, or not 1-20)\n", (int)pid,
                lv);
        p = *end == ',' ? end + 1 : end;
    }
}

void Reforged_TestUnitClass(void)
{
    static int sDoneChapter = -1;
    const char *env = getenv("FE8PC_TEST_UNIT_CLASS");
    const struct ClassData *cd;
    struct Unit *unit;
    char *end;
    long pid, jid, item = 0;
    int t;

    TestUnitLevel();
    if (env == NULL || gPlaySt.faction != FACTION_BLUE || (gPlaySt.chapterTurnNumber != 1 && !gBmSt.just_resumed) ||
        sDoneChapter == gPlaySt.chapterIndex)
        return;
    sDoneChapter = gPlaySt.chapterIndex;
    pid = strtol(env, &end, 0);
    jid = *end == ':' ? strtol(end + 1, &end, 0) : 0;
    if (*end == ':')
        item = strtol(end + 1, NULL, 0);
    if (jid < 1 || jid >= FE7_FIRST_JID + FOREIGN_COUNT || (jid >= FE7_FIRST_JID && !Reforged_Fe7ClassAvailable(jid)))
    {
        PortLog("[reforged] unit class test: '%s' refused (%s)\n", env,
            !IsForeign((int)jid) ? "not pid:jid with a class 0x01-0x8F"
            : sSpecs[jid - FE7_FIRST_JID].game == GAME_FE6 ? "an FE6 class, and the FE6 classes are not loaded"
                                                           : "an FE7 class, and the FE7 classes are not loaded");
        return;
    }
    unit = GetUnitFromCharId((int)pid);
    if (unit == NULL || UNIT_FACTION(unit) != FACTION_BLUE)
    {
        PortLog("[reforged] unit class test: no player unit pid 0x%02X in chapter %d\n", (int)pid, gPlaySt.chapterIndex);
        return;
    }
    cd = GetClassData((int)jid);
    unit->pClassData = cd;
    for (t = 0; t < 8; t++)
        unit->ranks[t] = cd->baseRanks[t] == 0 ? 0 : unit->ranks[t] < cd->baseRanks[t] ? cd->baseRanks[t] : unit->ranks[t];
    Reforged_FixArmedDancer(unit); /* "Armed dancers": a Dancer's or the Bard's rank, when the fix is on */
    if (item > 0 && item <= 0xCD /* ITEM_UNK_CD, the last FE8 item */)
    {
        for (t = 0; t < UNIT_ITEM_COUNT - 1 && unit->items[t] != 0; t++)
            ;
        unit->items[t] = MakeNewItem((int)item);
    }
    RefreshUnitSprites();
    PortLog("[reforged] unit class test: chapter %d, pid 0x%02X (at %d,%d) is now class 0x%02X (%s)%s, ranks %d %d %d %d "
            "%d %d %d %d\n", gPlaySt.chapterIndex, (int)pid, unit->xPos, unit->yPos, cd->number,
        GetStringFromIndex(cd->nameTextId),
        item > 0 && item <= 0xCD ? ", item given" : "", unit->ranks[0], unit->ranks[1], unit->ranks[2], unit->ranks[3],
        unit->ranks[4], unit->ranks[5], unit->ranks[6], unit->ranks[7]);
}

#endif /* PORT_PLAYED_BUILD */
