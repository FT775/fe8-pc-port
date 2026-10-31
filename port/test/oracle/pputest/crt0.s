@ PPU test ROM (pputest.c): entry and the two header bytes mGBA
@ looks for (an ARM branch at 0, 0x96 at 0xB2). The HLE BIOS starts here.
    .section .text.start, "ax"
    .arm
    .global _start
_start:
    b reset
    .fill 0xB2 - 4, 1, 0
    .byte 0x96
    .fill 0xC0 - 0xB3, 1, 0
reset:
    ldr sp, =0x03007F00
    ldr r0, =Main
    bx r0
    .ltorg
