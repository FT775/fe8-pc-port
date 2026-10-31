    .section .data

	.global Img_EventMapAnimMaskfx_0
Img_EventMapAnimMaskfx_0:
	.incbin "baserom.gba", 0x99D6DC, 0x99E1A4 - 0x99D6DC

	.global Img_EventMapAnimMaskfx_1
Img_EventMapAnimMaskfx_1:
	.incbin "baserom.gba", 0x99E1A4, 0x99ED44 - 0x99E1A4

	.global Img_EventMapAnimMaskfx_2
Img_EventMapAnimMaskfx_2:
	.incbin "baserom.gba", 0x99ED44, 0x99F7D4 - 0x99ED44

	.global Img_EventMapAnimMaskfx_3
Img_EventMapAnimMaskfx_3:
	.incbin "baserom.gba", 0x99F7D4, 0x9A0154 - 0x99F7D4

	.global Img_EventMapAnimMaskfx_4
Img_EventMapAnimMaskfx_4:
	.incbin "baserom.gba", 0x9A0154, 0x9A0864 - 0x9A0154

	.global Img_EventMapAnimMaskfx_5
Img_EventMapAnimMaskfx_5:
	.incbin "baserom.gba", 0x9A0864, 0x9A0E84 - 0x9A0864

	.global Tsa_EventMapAnimMaskfx_0
Tsa_EventMapAnimMaskfx_0:
	.incbin "baserom.gba", 0x9A0E84, 0x9A1050 - 0x9A0E84

	.global Tsa_EventMapAnimMaskfx_1
Tsa_EventMapAnimMaskfx_1:
	.incbin "baserom.gba", 0x9A1050, 0x9A1228 - 0x9A1050

	.global Tsa_EventMapAnimMaskfx_2
Tsa_EventMapAnimMaskfx_2:
	.incbin "baserom.gba", 0x9A1228, 0x9A13EC - 0x9A1228

	.global Tsa_EventMapAnimMaskfx_3
Tsa_EventMapAnimMaskfx_3:
	.incbin "baserom.gba", 0x9A13EC, 0x9A15B4 - 0x9A13EC

	.global Tsa_EventMapAnimMaskfx_4
Tsa_EventMapAnimMaskfx_4:
	.incbin "baserom.gba", 0x9A15B4, 0x9A174C - 0x9A15B4

	.global Tsa_EventMapAnimMaskfx_5
Tsa_EventMapAnimMaskfx_5:
	.incbin "baserom.gba", 0x9A174C, 0x9A18D4 - 0x9A174C

	.global Pal_EventMapAnimMaskfx
Pal_EventMapAnimMaskfx:  @ 0x089A18D4
	.incbin "baserom.gba", 0x9A18D4, 0x20

	.global Img_Congratulations
Img_Congratulations:  @ 0x089A18F4
	.incbin "graphics/event/Gfx_Congratulations.4bpp.lz"

	.global Img_MapClear
Img_MapClear:  @ 0x089A1E70
	.incbin "graphics/event/Gfx_MapClear.4bpp.lz"

	.global Pal_Congratulations
Pal_Congratulations:  @ 0x089A230C
	.incbin "graphics/event/Gfx_Congratulations.gbapal", 0, 0x20

	.global Pal_MapClear
Pal_MapClear:  @ 0x089A232C
	.incbin "graphics/event/Gfx_MapClear.gbapal", 0, 0x20

	.global Img_DungeonRecordTitle
Img_DungeonRecordTitle:  @ 0x089A234C
	.incbin "baserom.gba", 0x9A234C, 0x468

	.global Tsa_DungeonRecordTitle
Tsa_DungeonRecordTitle:  @ 0x089A27B4
	.incbin "baserom.gba", 0x9A27B4, 0x12C

	.global Pal_DungeonRecordTitle
Pal_DungeonRecordTitle:  @ 0x089A28E0
	.incbin "baserom.gba", 0x9A28E0, 0x40
