    .section .data

	.global ProcScr_OpAnimScrollCharBg
ProcScr_OpAnimScrollCharBg:  @ 0x08AA704C
        @ PROC_REPEAT
        .short 0x3, 0x0
        .word OpAnimScrollCharBgMain
        @ PROC_END
        .short 0x0, 0x0
        .word 0x0

	.global ProcScr_OpAnimBg2FadeFromDark
ProcScr_OpAnimBg2FadeFromDark:  @ 0x08AA705C
        @ PROC_SET_END_CB
        .short 0x4, 0x0
        .word OpAnimBg2FadeFromDarkEnd
        @ PROC_CALL
        .short 0x2, 0x0
        .word OpAnimBg2FadeFromDarkInit
        @ PROC_REPEAT
        .short 0x3, 0x0
        .word OpAnimBg2FadeFromDarkMain
        @ PROC_END
        .short 0x0, 0x0
        .word 0x0


	.global ProcScr_OpAnimBg2FadeToDark
ProcScr_OpAnimBg2FadeToDark:  @ 0x08AA707C
        @ PROC_SET_END_CB
        .short 0x4, 0x0
        .word OpAnimBg2FadeToDarkEnd
        @ PROC_CALL
        .short 0x2, 0x0
        .word OpAnimBg2FadeToDarkInit
        @ PROC_REPEAT
        .short 0x3, 0x0
        .word OpAnimBg2FadeToDarkMain
        @ PROC_END
        .short 0x0, 0x0
        .word 0x0


	.global Obj_OpAnimFace
Obj_OpAnimFace:  @ 0x08AA709C
	.incbin "baserom.gba", 0xAA709C, 0x20

	.global Obj_OpAnimFaceWindow96x48
Obj_OpAnimFaceWindow96x48:  @ 0x08AA70BC
	.incbin "baserom.gba", 0xAA70BC, 0x20

	.global Obj_OpAnimFaceWindow96x64
Obj_OpAnimFaceWindow96x64:  @ 0x08AA70DC
	.incbin "baserom.gba", 0xAA70DC, 0xE

	.global Obj_OpAnimFaceWindow112x48
Obj_OpAnimFaceWindow112x48:  @ 0x08AA70EA
	.incbin "baserom.gba", 0xAA70EA, 0x2C

	.global Obj_OpAnimFaceWindow64x80
Obj_OpAnimFaceWindow64x80:  @ 0x08AA7116
	.incbin "baserom.gba", 0xAA7116, 0x14

	.global Obj_OpAnimFaceWindow56x80
Obj_OpAnimFaceWindow56x80:  @ 0x08AA712A
	.incbin "baserom.gba", 0xAA712A, 0x32

	.global Obj_OpAnimFaceWindow192x48
Obj_OpAnimFaceWindow192x48:  @ 0x08AA715C
	.incbin "baserom.gba", 0xAA715C, 0x38

	.global Obj_OpAnimFaceWindow112x80
Obj_OpAnimFaceWindow112x80:  @ 0x08AA7194
	.incbin "baserom.gba", 0xAA7194, 0x34
