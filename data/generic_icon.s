    .include "macro.inc"

    .section .data

    .global Img_FactionMiniCard0
Img_FactionMiniCard0:  @ 0x08599734
    .incbin "graphics/generic_icon/0.4bpp"

    .global Img_FactionMiniCard1
Img_FactionMiniCard1:  @ 0x08599934
    .incbin "graphics/generic_icon/1.4bpp"

    .global Img_FactionMiniCard2
Img_FactionMiniCard2:  @ 0x08599B34
    .incbin "graphics/generic_icon/2.4bpp"

    .global Img_FactionMiniCard3
Img_FactionMiniCard3:  @ 0x08599D34
    .incbin "graphics/generic_icon/3.4bpp"

    .global Pal_FactionMiniCard_Blue
Pal_FactionMiniCard_Blue:  @ 0x08599F34
    .incbin "graphics/generic_icon/0.gbapal" @ 1st team color [blue]

    .global Pal_FactionMiniCard_Orange
Pal_FactionMiniCard_Orange:  @ 0x08599F54
    .incbin "graphics/generic_icon/1.gbapal" @ 2nd team color [orange]

    .global Pal_FactionMiniCard_Green
Pal_FactionMiniCard_Green:  @ 0x08599F74
    .incbin "graphics/generic_icon/2.gbapal" @ 3rd team color [green]

    @ 4th team [purple] ends up being unused as there is no way to have generics in the arena, which is the only place where the 4th team exists.
    .incbin "graphics/generic_icon/3.gbapal"
