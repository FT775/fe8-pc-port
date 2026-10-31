header Round 10 "paths" (port/test/inputs/paths/README.md), an edited start (test coverage only, not story play).
header Start: boot both sides with GATE0_SRAM_IN=pe14_start.sav (the save's first 32 KiB: FNV-1a a658fac9).
header Resume Chapter (resume.keys), Eirika's Chapter 14, turn 1. Moulder (Bishop, class 0x2B): stat screen, the Restore,
header Barrier, Silence, Warp and Rescue target selections (each opened, then B), the item menu. Natasha (Bishop, 0x2C):
header Sleep and Hammerne selections, a move next to a door, the Unlock selection (DoUsePutTrap), the item menu over her
header Lightning (the item panel's stats), her Vulnerary (one-unit heal). Colm (Dancer): Dance, the ring (the special
header dance's selection). The unit list. Dozla attacks the cracked wall with the Devil Axe: the backfire kills him (P-27).
prefix port/test/inputs/resume.keys 2000
wait 20
tap RIGHT
tap UP
tap UP
wait 20
mark statscreen
tap R 80
tap B 60
mark sel
tap A 20
tap A 30
mark menu
tap A 30
tap A 40
mark restore
tap B 40
tap A 30
tap DOWN
tap A 40
mark barrier
tap B 40
tap A 30
rep 2 DOWN
tap A 40
mark silence
tap B 40
tap A 30
rep 3 DOWN
tap A 40
mark warp
tap B 40
tap A 30
rep 4 DOWN
tap A 40
mark rescue
tap B 40
tap DOWN
tap A 40
mark itemmenu
tap B 40
tap UP
tap A 40
mark waited
wait 20
mark natasha
rep 3 RIGHT
tap UP
wait 10
tap A 20
tap A 30
mark nmenu
tap A 30
tap A 40
mark sleep
tap B 40
tap A 30
tap DOWN
tap A 40
mark hammerne
tap B 40
mark cancelmove
tap B 40
rep 2 UP
wait 10
tap A 50
mark moved
tap A 30
tap DOWN
tap A 40
mark unlock
tap B 40
tap DOWN
tap A 40
mark nitems
rep 5 DOWN 12
mark lightning
wait 20
rep 3 DOWN 8
wait 20
tap A 30
mark usemenu
tap A 40
mark vulnerary
wait 400
mark vdone
rep 3 LEFT
rep 5 DOWN
wait 20
mark colm
tap A 20
tap A 30
mark cmenu
tap A 40
mark dancemenu
tap DOWN 10
tap A 40
mark bless
tap B 40
tap UP 10
tap A 40
mark cwaited
wait 20
mark unitlist
rep 2 RIGHT
wait 10
tap A 30
mark mapmenu
tap A 80
mark ulist
wait 40
tap B 60
mark ulback
wait 10
mark dozla
rep 2 LEFT
rep 11 UP
wait 20
tap A 20
tap A 30
tap A 30
tap A 30
tap A 30
mark forecast
tap A 30
mark battle
wait 500
mark quote
tap A 60
tap A 60
wait 600
mark bdone
