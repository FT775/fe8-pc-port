	.section .data

    .global ProcScr_OpAnim
ProcScr_OpAnim:  @ 0x08AA71C8
    @ PROC_YIELD
    .short 0xe, 0x0
    .word 0x0
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimInit
    @ PROC_YIELD
    .short 0xe, 0x0
    .word 0x0
    @ PROC_CALL
    .short 0x2, 0x0
    .word SetupOpAnimWorldMapfx
    @ PROC_START_CHILD
    .short 0x5, 0x0
    .word ProcScr_OpAnimBLDALPHA
    @ PROC_WHILE
    .short 0x14, 0x0
    .word OpAnimBldAlphaExists
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimUpdateScreen1
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimWorldMapfxMain
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupCharacterScene
    @ PROC_YIELD
    .short 0xe, 0x0
    .word 0x0
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimPreparefxEphraim
    @ PROC_CALL
    .short 0x2, 0x0
    .word NewOpAnimSplitLineLatch
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimEphraimfxFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimEphraimSetupSplitWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnim1AdvanceSplitLine
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimEphraimMergeShadow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimEphraimDisplayName
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimEphraimExit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimPreparefxEirika
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimEirikafxFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimEirikaSetupSplitWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimEirikaAdvanceSplitLine
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimEirikaMergeShadow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimEirikaDisplayName
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimEirikaExit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimFadeToBlack
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_START_CHILD
    .short 0x5, 0x0
    .word ProcScr_OpAnimBg2FadeFromDark
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimPreparefxJoshua
    @ PROC_START_CHILD
    .short 0x5, 0x0
    .word ProcScr_OpAnimScrollCharBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToCharBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToCharBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupJoshuaFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyMid
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyOut
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToScrollBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToScrollBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimPreparefxLArachel
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToCharBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToCharBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupLArachelFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyMid
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyOut
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToScrollBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToScrollBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimPreparefxSeth
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToCharBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToCharBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupSethFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyMid
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyOut
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToScrollBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToScrollBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimPreparefxMyrrh
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToCharBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToCharBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupMyrrhFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyMid
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyOut
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToScrollBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToScrollBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimPreparefxSaleh
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToCharBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToCharBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupSalehFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyMid
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyOut
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToScrollBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToScrollBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimPreparefxTethys
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToCharBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToCharBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupTethysFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyMid
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCharFlyOut
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCrossfadeToScrollBgInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimCrossfadeToScrollBg
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_START_CHILD
    .short 0x5, 0x0
    .word ProcScr_OpAnimBg2FadeToDark
    @ PROC_END_EACH
    .short 0x9, 0x0
    .word ProcScr_OpAnimScrollCharBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimScrollWait
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimDevilEyesInit
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimDevilEyesMain
    @ PROC_CALL
    .short 0x2, 0x0
    .word sub_80CA92C
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupRennac
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupRennac
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupEwan
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupEwan
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupGarcia
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupGarcia
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupVanessa
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupVanessa
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupGilliam
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupGilliam
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupColm
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupColm
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupKnoll
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupKnoll
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupLute
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupLute
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupDuessel
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupDuessel
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupKyle
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupKyle
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupValter
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupValter
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimLoadFaceGroupLyon
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimSetupFacePanelWindow
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimSlideInFaceGroupLyon
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimRefreshScrollingBg
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimShiningRingFadeIn
    @ PROC_REPEAT
    .short 0x3, 0x0
    .word OpAnimFadeBgToWhite
    @ PROC_SLEEP
    .short 0xe, 0x20
    .word 0x0
    @ PROC_LABEL
    .short 0xb, 0x63
    .word 0x0
    @ PROC_CALL
    .short 0x2, 0x0
    .word OpAnimCleanup
    @ PROC_SLEEP
    .short 0xe, 0x1
    .word 0x0
    @ PROC_END
    .short 0x0, 0x0
    .word 0x0
