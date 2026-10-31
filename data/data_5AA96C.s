    .section .data

	.global Img_LinkArenaMenuTitles
Img_LinkArenaMenuTitles:  @ 0x085AAE0C
    .incbin "baserom.gba", 0x5AAE0C, 0x54C

	.global Img_LinkArenaMenu
Img_LinkArenaMenu:  @ 0x085AB358
    .incbin "baserom.gba", 0x5AB358, 0xA10

	.global Img_TacticianSelObj
Img_TacticianSelObj:  @ 0x085ABD68
    .incbin "baserom.gba", 0x5ABD68, 0x89C

	.global Img_LinkArenaPressStart
Img_LinkArenaPressStart:  @ 0x085AC604
	.incbin "baserom.gba", 0x5AC604, 0x3D8

	.global Img_LinkArenaRankIcons
Img_LinkArenaRankIcons:  @ 0x085AC9DC
	.incbin "baserom.gba", 0x5AC9DC, 0x344

	.global Img_LinkArenaActiveBannerFx
Img_LinkArenaActiveBannerFx:  @ 0x085ACD20
	.incbin "baserom.gba", 0x5ACD20, 0x1DC

	.global Img_SioResultNewHSHighlight
Img_SioResultNewHSHighlight:  @ 0x085ACEFC
	.incbin "baserom.gba", 0x5ACEFC, 0x1D0

	.global Img_LinkArenaPlacementRanks
Img_LinkArenaPlacementRanks:  @ 0x085AD0CC
    .incbin "baserom.gba", 0x5AD0CC, 0x740

	.global Img_LAPointsBox
Img_LAPointsBox:  @ 0x085AD80C
	.incbin "baserom.gba", 0x5AD80C, 0x1C0

	.global Img_LinkArena_FogUnitPlaceholder
Img_LinkArena_FogUnitPlaceholder:  @ 0x085AD9CC
	.incbin "baserom.gba", 0x5AD9CC, 0x6C

	.global Img_LinkArenaPlayerBanners
Img_LinkArenaPlayerBanners:  @ 0x085ADA38
	.incbin "baserom.gba", 0x5ADA38, 0x1B0

	.global Pal_LinkArenaMenu
Pal_LinkArenaMenu:  @ 0x085ADBE8
	.incbin "baserom.gba", 0x5ADBE8, 0x60

	.global Pal_TacticianSelObj
Pal_TacticianSelObj:  @ 0x085ADC48
	.incbin "baserom.gba", 0x5ADC48, 0x80

	.global Pal_LinkArenaRankIcons
Pal_LinkArenaRankIcons:  @ 0x085ADCC8
	.incbin "baserom.gba", 0x5ADCC8, 0xE0

	.global Pal_LinkArenaInactiveBanner
Pal_LinkArenaInactiveBanner:  @ 0x085ADDA8
	.incbin "baserom.gba", 0x5ADDA8, 0x20

	.global Pal_LinkArenaPlacementRanks
Pal_LinkArenaPlacementRanks:  @ 0x085ADDC8
	.incbin "baserom.gba", 0x5ADDC8, 0x20

	.global Pal_LinkArenaSideMenuGlow
Pal_LinkArenaSideMenuGlow:  @ 0x085ADDE8
	.incbin "baserom.gba", 0x5ADDE8, 0x20

	.global Pal_LinkArenaActiveBannerFx
Pal_LinkArenaActiveBannerFx:  @ 0x085ADE08
	.incbin "baserom.gba", 0x5ADE08, 0x20

	.global Pal_LinkArenaNameEntryGlow
Pal_LinkArenaNameEntryGlow:  @ 0x085ADE28
	.incbin "baserom.gba", 0x5ADE28, 0x20

	.global Pal_LinkArenaNameBannerGlow
Pal_LinkArenaNameBannerGlow:  @ 0x085ADE48
	.incbin "baserom.gba", 0x5ADE48, 0x20

	.global Pal_085ADE68
Pal_085ADE68:  @ 0x085ADE68
	.incbin "baserom.gba", 0x5ADE68, 0x20

	.global Tsa_SioPostBattleOwnRow
Tsa_SioPostBattleOwnRow:  @ 0x085ADE88
	.incbin "baserom.gba", 0x5ADE88, 0xB8

	.global Tsa_SioTeamListWindow
Tsa_SioTeamListWindow:  @ 0x085ADF40
	.incbin "baserom.gba", 0x5ADF40, 0x250

	.global Tsa_TacticianCharGridWindow
Tsa_TacticianCharGridWindow:  @ 0x085AE190
	.incbin "baserom.gba", 0x5AE190, 0x2D4

	.global Tsa_SioResultRankings
Tsa_SioResultRankings:  @ 0x085AE464
	.incbin "baserom.gba", 0x5AE464, 0x314

	.global Tsa_LinkArenaRules
Tsa_LinkArenaRules:  @ 0x085AE778
	.incbin "baserom.gba", 0x5AE778, 0x74

	.global Img_LinkArenaPlayerPhase
Img_LinkArenaPlayerPhase:  @ 0x085AE7EC
	.incbin "baserom.gba", 0x5AE7EC, 0x5E8    @ 0x5AEDD4 - 0x5AE7EC

	.global Img_LinkArenaPlayerPhaseNum1
Img_LinkArenaPlayerPhaseNum1:  @ 0x085AEDD4
	.incbin "baserom.gba", 0x5AEDD4, 0xBC    @ 0x5AEE90 - 0x5AEDD4

	.global Img_LinkArenaPlayerPhaseNum2
Img_LinkArenaPlayerPhaseNum2:  @ 0x085AEE90
	.incbin "baserom.gba", 0x5AEE90, 0xC4    @ 0x5AEF54 - 0x5AEE90

	.global Img_LinkArenaPlayerPhaseNum3
Img_LinkArenaPlayerPhaseNum3:  @ 0x085AEF54
	.incbin "baserom.gba", 0x5AEF54, 0xD8    @ 0x5AF02C - 0x5AEF54

	.global Img_LinkArenaPlayerPhaseNum4
Img_LinkArenaPlayerPhaseNum4:  @ 0x085AF02C
	.incbin "baserom.gba", 0x5AF02C, 0xC4    @ 0x5AF0F0 - 0x5AF02C

	.global Pal_LinkArenaPlayerPhaseBlue
Pal_LinkArenaPlayerPhaseBlue:  @ 0x085AF0F0
	.incbin "baserom.gba", 0x5AF0F0, 0x20    @ 0x5AF110 - 0x5AF0F0

	.global Pal_LinkArenaPlayerPhaseRed
Pal_LinkArenaPlayerPhaseRed:  @ 0x085AF110
	.incbin "baserom.gba", 0x5AF110, 0x20    @ 0x5AF130 - 0x5AF110

	.global Pal_LinkArenaPlayerPhaseGreen
Pal_LinkArenaPlayerPhaseGreen:  @ 0x085AF130
	.incbin "baserom.gba", 0x5AF130, 0x20    @ 0x5AF150 - 0x5AF130

	.global Pal_LinkArenaPlayerPhaseYellow
Pal_LinkArenaPlayerPhaseYellow:  @ 0x085AF150
	.incbin "baserom.gba", 0x5AF150, 0x20    @ 0x5AF170 - 0x5AF150

	.global Img_LinkArenaPostBattleBg
Img_LinkArenaPostBattleBg:  @ 0x085AF170
    .incbin "baserom.gba", 0x5AF170, 0x16AC

	.global Pal_LinkArenaPostBattleBg
Pal_LinkArenaPostBattleBg:  @ 0x085B081C
	.incbin "baserom.gba", 0x5B081C, 0x80

	.global Tsa_LinkArenaPostBattleBg
Tsa_LinkArenaPostBattleBg:  @ 0x085B089C
	.incbin "baserom.gba", 0x5B089C, 0x54C

	.global Img_LinkArenaTitleBannerBg
Img_LinkArenaTitleBannerBg:  @ 0x085B0DE8
	.incbin "baserom.gba", 0x5B0DE8, 0x144

	.global Tsa_LinkArenaTitleBannerBg
Tsa_LinkArenaTitleBannerBg:  @ 0x085B0F2C
    .incbin "baserom.gba", 0x5B0F2C, 0x5504
