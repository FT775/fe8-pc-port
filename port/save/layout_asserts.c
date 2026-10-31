/*
 * Host-only layout checks (port-plan/DIVERGENCES.md D-18). Every number here is
 * agbcc's, read from agbcc's own output by port/tools/layout_probe.py. The file
 * makes no code; it fails the PC build if a save struct, or a struct that
 * D-18 gave agbcc's alignment (AGBCC_STRUCT), stops matching the GBA layout.
 *
 * Save structs: size, 4-byte alignment and the offset of every member.
 * The other AGBCC_STRUCT structs in include/: size and alignment.
 * Not covered here: bitfield positions (Dungeon, GMapSaveInfo, PlttData and
 * others; C cannot test them at compile time) and the struct types local to
 * files in src/. `make -f port/Makefile FULL=1 layout-check` checks all of them
 * against agbcc directly.
 */
#include "global.h"
#include "muctrl.h" /* eventcall.h uses struct REDA without including it (port/include/port.h) */

#include "anime.h"
#include "bm.h"
#include "bmdifficulty.h"
#include "bmfx.h"
#include "bmguide.h"
#include "bmidoten.h"
#include "bmitem.h"
#include "bmreliance.h"
#include "bmsave.h"
#include "bmtrick.h"
#include "bmunit.h"
#include "bonusclaim.h"
#include "chap_title.h"
#include "chapterdata.h"
#include "cp_common.h"
#include "cp_data.h"
#include "ctc.h"
#include "ekrbattle.h"
#include "ekrdragon.h"
#include "ending_details.h"
#include "event.h"
#include "eventcall.h"
#include "eventinfo.h"
#include "fontgrp.h"
#include "gba/types.h"
#include "hardware.h"
#include "icon.h"
#include "mapanim.h"
#include "monstergen.h"
#include "opanim.h"
#include "player_interface.h"
#include "prepscreen.h"
#include "savemenu.h"
#include "sio.h"
#include "sio_core.h"
#include "soundwrapper.h"
#include "statscreen.h"
#include "sysutil.h"
#include "types.h"
#include "uiconfig.h"
#include "uimenu.h"
#include "uisupport.h"
#include "unitlistscreen.h"
#include "variables.h"
#include "worldmap.h"

#include <stddef.h>

#define CHECK_SIZE(T, n) \
    _Static_assert(sizeof(T) == (n) && _Alignof(T) == 4, #T ": agbcc size " #n ", alignment 4")
#define CHECK_OFF(T, m, n) \
    _Static_assert(offsetof(T, m) == (n), #T "." #m ": agbcc offset " #n)

/* Save structs. */
_Static_assert(sizeof(struct SuspendSaveBlock) == 0x1F78, "SuspendSaveBlock is 0x1F78 bytes");
_Static_assert(sizeof(struct GameSaveBlock) == 0xDC8, "GameSaveBlock is 0xDC8 bytes");

CHECK_SIZE(struct GlobalSaveInfo, 0x64);
CHECK_OFF(struct GlobalSaveInfo, name, 0x0);
CHECK_OFF(struct GlobalSaveInfo, magic32, 0x8);
CHECK_OFF(struct GlobalSaveInfo, magic16, 0xC);
CHECK_OFF(struct GlobalSaveInfo, unk10, 0x10);
CHECK_OFF(struct GlobalSaveInfo, unk12, 0x12);
CHECK_OFF(struct GlobalSaveInfo, cleared_playthroughs, 0x14);
CHECK_OFF(struct GlobalSaveInfo, SuppordRecord, 0x20);
CHECK_OFF(struct GlobalSaveInfo, charKnownFlags, 0x40);
CHECK_OFF(struct GlobalSaveInfo, checksum, 0x60);
CHECK_OFF(struct GlobalSaveInfo, last_game_save_id, 0x62);
CHECK_OFF(struct GlobalSaveInfo, last_suspend_slot, 0x63);

CHECK_SIZE(struct SaveBlockInfo, 0x10);
CHECK_OFF(struct SaveBlockInfo, magic32, 0x0);
CHECK_OFF(struct SaveBlockInfo, magic16, 0x4);
CHECK_OFF(struct SaveBlockInfo, kind, 0x6);
CHECK_OFF(struct SaveBlockInfo, _pad_07, 0x7);
CHECK_OFF(struct SaveBlockInfo, offset, 0x8);
CHECK_OFF(struct SaveBlockInfo, size, 0xA);
CHECK_OFF(struct SaveBlockInfo, checksum32, 0xC);

CHECK_SIZE(struct GameRankSaveData, 0x18);
CHECK_OFF(struct GameRankSaveData, tactician_name, 0xC);
CHECK_OFF(struct GameRankSaveData, luckydog, 0x17);

CHECK_SIZE(struct GameRankSaveDataPacks, 0x94);
CHECK_OFF(struct GameRankSaveDataPacks, pack, 0x0);
CHECK_OFF(struct GameRankSaveDataPacks, magic0, 0x90);
CHECK_OFF(struct GameRankSaveDataPacks, unk92, 0x92);

CHECK_SIZE(struct SoundRoomSaveData, 0x24);
CHECK_OFF(struct SoundRoomSaveData, flags, 0x0);
CHECK_OFF(struct SoundRoomSaveData, magic1, 0x20);
CHECK_OFF(struct SoundRoomSaveData, magic2, 0x22);

CHECK_SIZE(struct bmsave_unkstruct2, 0x14);
CHECK_OFF(struct bmsave_unkstruct2, unk, 0x0);
CHECK_OFF(struct bmsave_unkstruct2, magic1, 0x10);
CHECK_OFF(struct bmsave_unkstruct2, magic2, 0x12);

CHECK_SIZE(struct BonusClaimSaveData, 0x144);
CHECK_OFF(struct BonusClaimSaveData, bonus, 0x0);
CHECK_OFF(struct BonusClaimSaveData, cksum16, 0x140);

CHECK_SIZE(struct UnitUsageStats, 0x10);

CHECK_SIZE(struct ChapterStats, 0x4);

CHECK_SIZE(struct GameSavePackedUnit, 0x24);
CHECK_OFF(struct GameSavePackedUnit, pid, 0x14);
CHECK_OFF(struct GameSavePackedUnit, wpnRanks, 0x15);
CHECK_OFF(struct GameSavePackedUnit, supports, 0x1D);

CHECK_SIZE(struct SuspendSavePackedUnit, 0x34);
CHECK_OFF(struct SuspendSavePackedUnit, pid, 0x0);
CHECK_OFF(struct SuspendSavePackedUnit, jid, 0x1);
CHECK_OFF(struct SuspendSavePackedUnit, ai1, 0x2);
CHECK_OFF(struct SuspendSavePackedUnit, rescue, 0x3);
CHECK_OFF(struct SuspendSavePackedUnit, state, 0x4);
CHECK_OFF(struct SuspendSavePackedUnit, item1, 0x8);
CHECK_OFF(struct SuspendSavePackedUnit, item2, 0xA);
CHECK_OFF(struct SuspendSavePackedUnit, item3, 0xC);
CHECK_OFF(struct SuspendSavePackedUnit, maxHP, 0xE);
CHECK_OFF(struct SuspendSavePackedUnit, curHP, 0xF);
CHECK_OFF(struct SuspendSavePackedUnit, exp, 0x10);
CHECK_OFF(struct SuspendSavePackedUnit, aiFlags, 0x11);
CHECK_OFF(struct SuspendSavePackedUnit, ranks, 0x12);
CHECK_OFF(struct SuspendSavePackedUnit, supports, 0x1A);
CHECK_OFF(struct SuspendSavePackedUnit, ai_a_pc, 0x21);
CHECK_OFF(struct SuspendSavePackedUnit, ai2, 0x22);
CHECK_OFF(struct SuspendSavePackedUnit, ai_b_pc, 0x23);
CHECK_OFF(struct SuspendSavePackedUnit, ballistaIndex, 0x30);
CHECK_OFF(struct SuspendSavePackedUnit, ai_counter, 0x31);
CHECK_OFF(struct SuspendSavePackedUnit, ai_config, 0x32);

CHECK_SIZE(struct MultiArenaRankingEnt, 0x14);
CHECK_OFF(struct MultiArenaRankingEnt, name, 0x4);

CHECK_SIZE(struct MultiArenaSaveTeam, 0xC4);
CHECK_OFF(struct MultiArenaSaveTeam, name, 0x0);
CHECK_OFF(struct MultiArenaSaveTeam, units, 0x10);

CHECK_SIZE(struct MultiArenaSaveBlock, 0x874);
CHECK_OFF(struct MultiArenaSaveBlock, teams, 0x0);
CHECK_OFF(struct MultiArenaSaveBlock, config, 0x7A8);
CHECK_OFF(struct MultiArenaSaveBlock, rankings, 0x7AC);

CHECK_SIZE(struct ExtraMapSaveHead, 0x1C);
CHECK_OFF(struct ExtraMapSaveHead, xmap_magic, 0x0);
CHECK_OFF(struct ExtraMapSaveHead, xmap_size, 0x4);
CHECK_OFF(struct ExtraMapSaveHead, xmap_checksum, 0x6);
CHECK_OFF(struct ExtraMapSaveHead, save_magic32, 0x8);
CHECK_OFF(struct ExtraMapSaveHead, _pad_0C, 0xC);
CHECK_OFF(struct ExtraMapSaveHead, unk0F, 0xF);
CHECK_OFF(struct ExtraMapSaveHead, map_sram, 0x10);
CHECK_OFF(struct ExtraMapSaveHead, map_size, 0x14);
CHECK_OFF(struct ExtraMapSaveHead, info_size, 0x16);
CHECK_OFF(struct ExtraMapSaveHead, info_sram, 0x18);

CHECK_SIZE(struct ExtraMapInfo, 0x40);
CHECK_OFF(struct ExtraMapInfo, chapter_info, 0x0);
CHECK_OFF(struct ExtraMapInfo, map_change_info, 0x4);
CHECK_OFF(struct ExtraMapInfo, event_info, 0x8);
CHECK_OFF(struct ExtraMapInfo, chapter_title, 0xC);
CHECK_OFF(struct ExtraMapInfo, msg_10, 0x10);
CHECK_OFF(struct ExtraMapInfo, msg_14, 0x14);
CHECK_OFF(struct ExtraMapInfo, play_st, 0x18);
CHECK_OFF(struct ExtraMapInfo, bm_st, 0x1C);
CHECK_OFF(struct ExtraMapInfo, active_unit, 0x20);
CHECK_OFF(struct ExtraMapInfo, unit_lut, 0x24);
CHECK_OFF(struct ExtraMapInfo, bu_a, 0x28);
CHECK_OFF(struct ExtraMapInfo, bu_b, 0x2C);
CHECK_OFF(struct ExtraMapInfo, battle_hits, 0x30);
CHECK_OFF(struct ExtraMapInfo, traps, 0x34);
CHECK_OFF(struct ExtraMapInfo, permanent_flags, 0x38);
CHECK_OFF(struct ExtraMapInfo, chapter_frags, 0x3C);

CHECK_SIZE(struct GMapSaveInfo, 0x24);
CHECK_OFF(struct GMapSaveInfo, nodes, 0x0);
CHECK_OFF(struct GMapSaveInfo, paths, 0x8);
CHECK_OFF(struct GMapSaveInfo, units, 0xC);
CHECK_OFF(struct GMapSaveInfo, xCursor, 0x1A);
CHECK_OFF(struct GMapSaveInfo, yCursor, 0x1B);
CHECK_OFF(struct GMapSaveInfo, skirmishes, 0x1D);
CHECK_OFF(struct GMapSaveInfo, skirmishState, 0x20);

CHECK_SIZE(struct Dungeon, 0xC);

CHECK_SIZE(struct GameSaveBlock, 0xDC8);
CHECK_OFF(struct GameSaveBlock, playSt, 0x0);
CHECK_OFF(struct GameSaveBlock, units, 0x4C);
CHECK_OFF(struct GameSaveBlock, gmUnit, 0x778);
CHECK_OFF(struct GameSaveBlock, supplyItems, 0x79C);
CHECK_OFF(struct GameSaveBlock, pidStats, 0x84C);
CHECK_OFF(struct GameSaveBlock, chapterStats, 0xCAC);
CHECK_OFF(struct GameSaveBlock, permanentFlags, 0xD6C);
CHECK_OFF(struct GameSaveBlock, bonusClaimFlags, 0xD88);
CHECK_OFF(struct GameSaveBlock, wmStuff, 0xD8C);
CHECK_OFF(struct GameSaveBlock, dungeons, 0xDB0);

CHECK_SIZE(struct SuspendSaveBlock, 0x1F78);
CHECK_OFF(struct SuspendSaveBlock, playSt, 0x0);
CHECK_OFF(struct SuspendSaveBlock, action, 0x4C);
CHECK_OFF(struct SuspendSaveBlock, blueUnits, 0x84);
CHECK_OFF(struct SuspendSaveBlock, wmMonsterUnit, 0xAE0);
CHECK_OFF(struct SuspendSaveBlock, redUnits, 0xB14);
CHECK_OFF(struct SuspendSaveBlock, greenUnits, 0x153C);
CHECK_OFF(struct SuspendSaveBlock, traps, 0x1744);
CHECK_OFF(struct SuspendSaveBlock, supplyItems, 0x1944);
CHECK_OFF(struct SuspendSaveBlock, pidStats, 0x19F4);
CHECK_OFF(struct SuspendSaveBlock, chapterStats, 0x1E54);
CHECK_OFF(struct SuspendSaveBlock, menuOverride, 0x1F14);
CHECK_OFF(struct SuspendSaveBlock, permanentFlags, 0x1F24);
CHECK_OFF(struct SuspendSaveBlock, chapterFlags, 0x1F3D);
CHECK_OFF(struct SuspendSaveBlock, wmStuff, 0x1F44);
CHECK_OFF(struct SuspendSaveBlock, dungeon, 0x1F68);
CHECK_OFF(struct SuspendSaveBlock, eventSlotCnt, 0x1F74);

CHECK_SIZE(struct SaveBlocks, 0x741C);
CHECK_OFF(struct SaveBlocks, globalSaveInfo, 0x0);
CHECK_OFF(struct SaveBlocks, saveBlockInfo, 0x64);
CHECK_OFF(struct SaveBlocks, suspendSaveBlocks, 0xD4);
CHECK_OFF(struct SaveBlocks, gameSaveBlocks, 0x3FC4);
CHECK_OFF(struct SaveBlocks, multiArenaBlock, 0x691C);
CHECK_OFF(struct SaveBlocks, gameRankSave, 0x7190);
CHECK_OFF(struct SaveBlocks, soundRoomSave, 0x7224);
CHECK_OFF(struct SaveBlocks, unkstruct2, 0x7248);
CHECK_OFF(struct SaveBlocks, bonusClaim, 0x725C);
CHECK_OFF(struct SaveBlocks, reserved, 0x73A0);
CHECK_OFF(struct SaveBlocks, _pad_, 0x73A4);
CHECK_OFF(struct SaveBlocks, xmap, 0x7400);

CHECK_SIZE(struct GameSaveBlockEwram, 0xDA4);
CHECK_OFF(struct GameSaveBlockEwram, playSt, 0x0);
CHECK_OFF(struct GameSaveBlockEwram, unk_4C, 0x4C);
CHECK_OFF(struct GameSaveBlockEwram, buffer_58, 0x58);
CHECK_OFF(struct GameSaveBlockEwram, buffer_D74, 0xD74);

CHECK_SIZE(struct SuspendSaveBlockEwram, 0x1F58);
CHECK_OFF(struct SuspendSaveBlockEwram, playSt, 0x0);
CHECK_OFF(struct SuspendSaveBlockEwram, unk_4C, 0x4C);
CHECK_OFF(struct SuspendSaveBlockEwram, buffer_58, 0x58);
CHECK_OFF(struct SuspendSaveBlockEwram, buffer_1F1C, 0x1F1C);

CHECK_SIZE(struct SaveBlocksEwram, 0x72F4);
CHECK_OFF(struct SaveBlocksEwram, globalSaveInfo, 0x0);
CHECK_OFF(struct SaveBlocksEwram, saveBlockInfo, 0x64);
CHECK_OFF(struct SaveBlocksEwram, suspendSaveBlocks, 0xD4);
CHECK_OFF(struct SaveBlocksEwram, gameSaveBlocks, 0x3F84);
CHECK_OFF(struct SaveBlocksEwram, offset_5, 0x6870);
CHECK_OFF(struct SaveBlocksEwram, gameRankSave, 0x70E4);
CHECK_OFF(struct SaveBlocksEwram, soundRoomSave, 0x7178);
CHECK_OFF(struct SaveBlocksEwram, unkstruct2, 0x719C);
CHECK_OFF(struct SaveBlocksEwram, bonusClaim, 0x71B0);

CHECK_SIZE(struct PlaySt, 0x4C);
CHECK_OFF(struct PlaySt, time_saved, 0x0);
CHECK_OFF(struct PlaySt, time_chapter_started, 0x4);
CHECK_OFF(struct PlaySt, partyGoldAmount, 0x8);
CHECK_OFF(struct PlaySt, gameSaveSlot, 0xC);
CHECK_OFF(struct PlaySt, chapterVisionRange, 0xD);
CHECK_OFF(struct PlaySt, chapterIndex, 0xE);
CHECK_OFF(struct PlaySt, faction, 0xF);
CHECK_OFF(struct PlaySt, chapterTurnNumber, 0x10);
CHECK_OFF(struct PlaySt, xCursor, 0x12);
CHECK_OFF(struct PlaySt, yCursor, 0x13);
CHECK_OFF(struct PlaySt, chapterStateBits, 0x14);
CHECK_OFF(struct PlaySt, chapterWeatherId, 0x15);
CHECK_OFF(struct PlaySt, chapterTotalSupportGain, 0x16);
CHECK_OFF(struct PlaySt, playthroughIdentifier, 0x18);
CHECK_OFF(struct PlaySt, unk19, 0x19);
CHECK_OFF(struct PlaySt, lastUnitSortType, 0x1A);
CHECK_OFF(struct PlaySt, chapterModeIndex, 0x1B);
CHECK_OFF(struct PlaySt, unk1C, 0x1C);
CHECK_OFF(struct PlaySt, unk1E, 0x1E);
CHECK_OFF(struct PlaySt, unk1F, 0x1F);
CHECK_OFF(struct PlaySt, playerName, 0x20);
CHECK_OFF(struct PlaySt, unk_30, 0x30);
CHECK_OFF(struct PlaySt, config, 0x40);
CHECK_OFF(struct PlaySt, unk48, 0x48);
CHECK_OFF(struct PlaySt, tutorial_counter, 0x4B);

CHECK_SIZE(struct ActionData, 0x38);
CHECK_OFF(struct ActionData, _u00, 0x0);
CHECK_OFF(struct ActionData, item, 0x6);
CHECK_OFF(struct ActionData, unk08, 0x8);
CHECK_OFF(struct ActionData, unk0A, 0xA);
CHECK_OFF(struct ActionData, subjectIndex, 0xC);
CHECK_OFF(struct ActionData, targetIndex, 0xD);
CHECK_OFF(struct ActionData, xMove, 0xE);
CHECK_OFF(struct ActionData, yMove, 0xF);
CHECK_OFF(struct ActionData, moveCount, 0x10);
CHECK_OFF(struct ActionData, unitActionType, 0x11);
CHECK_OFF(struct ActionData, itemSlotIndex, 0x12);
CHECK_OFF(struct ActionData, xOther, 0x13);
CHECK_OFF(struct ActionData, yOther, 0x14);
CHECK_OFF(struct ActionData, trapType, 0x15);
CHECK_OFF(struct ActionData, suspendPointType, 0x16);
CHECK_OFF(struct ActionData, scriptedBattleHits, 0x18);
CHECK_OFF(struct ActionData, script_hits, 0x1C);

CHECK_SIZE(struct Trap, 0x8);
CHECK_OFF(struct Trap, xPos, 0x0);
CHECK_OFF(struct Trap, yPos, 0x1);
CHECK_OFF(struct Trap, type, 0x2);
CHECK_OFF(struct Trap, extra, 0x3);
CHECK_OFF(struct Trap, data, 0x4);

CHECK_SIZE(struct BonusClaimEnt, 0x14);
CHECK_OFF(struct BonusClaimEnt, unseen, 0x0);
CHECK_OFF(struct BonusClaimEnt, kind, 0x1);
CHECK_OFF(struct BonusClaimEnt, itemId, 0x2);
CHECK_OFF(struct BonusClaimEnt, str, 0x3);

CHECK_SIZE(struct BonusClaimItemEnt, 0x4);
CHECK_OFF(struct BonusClaimItemEnt, unk_00, 0x0);
CHECK_OFF(struct BonusClaimItemEnt, claimable, 0x1);

/* The other structs and unions in include/ that end with AGBCC_STRUCT. */
CHECK_SIZE(struct AnimSpriteData, 0xC);  /* include/anime.h */
CHECK_SIZE(struct CamMoveProc, 0x44);  /* include/bm.h */
CHECK_SIZE(struct UnkMapCursorProc, 0x3C);  /* include/bm.h */
CHECK_SIZE(struct Outer080D7FD0, 0x48);  /* include/bmdifficulty.h */
CHECK_SIZE(struct Struct020038C8, 0x140);  /* include/bmdifficulty.h */
CHECK_SIZE(struct Struct030017A0, 0x28);  /* include/bmdifficulty.h */
CHECK_SIZE(struct Struct080D7FD0, 0x8);  /* include/bmdifficulty.h */
CHECK_SIZE(struct VectorBmfx, 0x4);  /* include/bmfx.h */
CHECK_SIZE(struct GuideEnt, 0xC);  /* include/bmguide.h */
CHECK_SIZE(struct GuideSt, 0xF4);  /* include/bmguide.h */
CHECK_SIZE(struct MovMapFillStateExt, 0x4);  /* include/bmidoten.h */
CHECK_SIZE(struct ItemStatBonuses, 0xC);  /* include/bmitem.h */
CHECK_SIZE(struct SupportBonuses, 0x8);  /* include/bmreliance.h */
CHECK_SIZE(struct SupportData, 0x18);  /* include/bmreliance.h */
CHECK_SIZE(struct ChapterTitleFxSt, 0x4);  /* include/chap_title.h */
CHECK_SIZE(struct ChapterMap, 0x8);  /* include/chapterdata.h */
CHECK_SIZE(struct AiDecision, 0xC);  /* include/cp_common.h */
CHECK_SIZE(struct AiCombatScoreCoefficients, 0x14);  /* include/cp_data.h */
CHECK_SIZE(struct AiCountEnemiesInRangeArg, 0x4);  /* include/cp_data.h */
CHECK_SIZE(struct AiEscapePt, 0x4);  /* include/cp_data.h */
CHECK_SIZE(struct AiHealThreshold, 0x4);  /* include/cp_data.h */
CHECK_SIZE(struct OAM_Attr0, 0x4);  /* include/ctc.h */
CHECK_SIZE(struct oam2_data, 0x4);  /* include/ctc.h */
CHECK_SIZE(struct BanimRoundScript, 0x4);  /* include/ekrbattle.h */
CHECK_SIZE(struct BattleAnimDef, 0x4);  /* include/ekrbattle.h */
CHECK_SIZE(struct EkrDragonQuakePriv, 0x3C);  /* include/ekrdragon.h */
CHECK_SIZE(struct EndingDefeatEnt, 0x4);  /* include/ending_details.h */
CHECK_SIZE(struct Struct03000428, 0x8);  /* include/event.h */
CHECK_SIZE(struct Unknown03000600, 0x4);  /* include/eventcall.h */
CHECK_SIZE(struct ForceDeploymentEnt, 0x4);  /* include/eventinfo.h */
CHECK_SIZE(struct Text, 0x8);  /* include/fontgrp.h */
CHECK_SIZE(struct ObjAffineSrcData, 0x8);  /* include/gba/types.h */
CHECK_SIZE(struct PlttData, 0x4);  /* include/gba/types.h */
CHECK_SIZE(struct SioMultiCnt, 0x4);  /* include/gba/types.h */
CHECK_SIZE(struct WaitCnt, 0x4);  /* include/gba/types.h */
CHECK_SIZE(struct KeyStatusBuffer, 0x14);  /* include/hardware.h */
CHECK_SIZE(struct IconStruct, 0x4);  /* include/icon.h */
CHECK_SIZE(struct MADebugInfo, 0xD0);  /* include/mapanim.h */
CHECK_SIZE(struct MADebugInfoEntry, 0x64);  /* include/mapanim.h */
CHECK_SIZE(struct MapAnimSpellfxConf, 0x4);  /* include/mapanim.h */
CHECK_SIZE(struct Unk082058B4, 0x4);  /* include/mapanim.h */
CHECK_SIZE(struct Unk089A3798, 0x8);  /* include/mapanim.h */
CHECK_SIZE(struct MonsterClassWeights, 0xC);  /* include/monstergen.h */
CHECK_SIZE(struct MonsterItemsByClassEntry, 0x20);  /* include/monstergen.h */
CHECK_SIZE(struct OpAnimSt, 0x10);  /* include/opanim.h */
CHECK_SIZE(struct PlayerInterfaceConfigEntry, 0x8);  /* include/player_interface.h */
CHECK_SIZE(struct PlayerInterfaceProc, 0x5C);  /* include/player_interface.h */
CHECK_SIZE(struct PrepItemTypePageEnt, 0x4);  /* include/prepscreen.h */
CHECK_SIZE(struct PrepScreenItemListEnt, 0x4);  /* include/prepscreen.h */
CHECK_SIZE(struct SioPidPool, 0x8);  /* include/prepscreen.h */
CHECK_SIZE(struct Win1H, 0x4);  /* include/prepscreen.h */
CHECK_SIZE(struct SaveMenuRTextData, 0x4);  /* include/savemenu.h */
CHECK_SIZE(struct LAPointsBoxProc, 0x4C);  /* include/sio.h */
CHECK_SIZE(struct LinkArenaStMaybe, 0xF0);  /* include/sio_core.h */
CHECK_SIZE(struct SioData, 0x88);  /* include/sio_core.h */
CHECK_SIZE(struct SioMessage, 0x4);  /* include/sio_core.h */
CHECK_SIZE(struct SioPending, 0x8C);  /* include/sio_core.h */
CHECK_SIZE(struct SioSt, 0x1B80);  /* include/sio_core.h */
CHECK_SIZE(struct SoundSt, 0xC);  /* include/soundwrapper.h */
CHECK_SIZE(struct StatScreenInfo, 0x4);  /* include/statscreen.h */
CHECK_SIZE(struct ProcSysBrownBox, 0x54);  /* include/sysutil.h */
CHECK_SIZE(struct SysBrownBoxConf, 0x8);  /* include/sysutil.h */
CHECK_SIZE(struct BgCoords, 0x4);  /* include/types.h */
CHECK_SIZE(struct EfxFrameConfig, 0x4);  /* include/types.h */
CHECK_SIZE(struct MsgBuffer, 0x1000);  /* include/types.h */
CHECK_SIZE(struct Struct0858791C, 0x8);  /* include/types.h */
CHECK_SIZE(struct Struct202B6B0, 0x8C);  /* include/types.h */
CHECK_SIZE(struct Vec2, 0x4);  /* include/types.h */
CHECK_SIZE(struct Vec2u, 0x4);  /* include/types.h */
CHECK_SIZE(struct ConfigScreen, 0xB0);  /* include/uiconfig.h */
CHECK_SIZE(struct GameOption, 0x2C);  /* include/uiconfig.h */
CHECK_SIZE(struct Selector, 0x8);  /* include/uiconfig.h */
CHECK_SIZE(struct MenuProc, 0x6C);  /* include/uimenu.h */
CHECK_SIZE(struct MenuRect, 0x4);  /* include/uimenu.h */
CHECK_SIZE(struct SupportScreenUnit, 0x18);  /* include/uisupport.h */
CHECK_SIZE(struct Unknown_02013460, 0x14);  /* include/unitlistscreen.h */
CHECK_SIZE(struct Unknown_08A209FC, 0x4);  /* include/variables.h */
CHECK_SIZE(struct GMNode, 0x4);  /* include/worldmap.h */
CHECK_SIZE(struct GMUnit, 0x4);  /* include/worldmap.h */
CHECK_SIZE(struct GMapData, 0xD0);  /* include/worldmap.h */
CHECK_SIZE(struct GMapMuPrimProc, 0x58);  /* include/worldmap.h */
CHECK_SIZE(struct GMapMuPrimProc_Unk_34, 0x4);  /* include/worldmap.h */
CHECK_SIZE(struct GMapNodeLink, 0x8);  /* include/worldmap.h */
CHECK_SIZE(struct GmapTimeMonsConf, 0x4);  /* include/worldmap.h */
CHECK_SIZE(struct OpenPaths, 0x24);  /* include/worldmap.h */
CHECK_SIZE(struct ProcGmapTimeMons, 0x48);  /* include/worldmap.h */
CHECK_SIZE(struct Unknown0201B100, 0x24);  /* include/worldmap.h */
CHECK_SIZE(struct Unknown_3001DA8, 0x8);  /* include/worldmap.h */
CHECK_SIZE(struct WMSongTableEnt, 0x4);  /* include/worldmap.h */
