header Round 10 "paths" (port/test/inputs/paths/README.md), an edited start (test coverage only, not story play).
header Start: boot both sides with GATE0_SRAM_IN=pc7b_start.sav (the save's first 32 KiB: FNV-1a 20513832).
header Resume Chapter, Chapter 7, turn 1: Neimi rides the ballista at (2,10), Attack (StartUnitBallistaSelect), the
header ballista, a target, B: the back-out (D-49, UnitActionMenu_Attack(NULL, NULL), the ballista branch), the ballista
header again, a target, the attack.
prefix port/test/inputs/resume.keys 2000
wait 20
rep 7 UP
wait 20
mark sel
tap A 20
tap A 30
mark menu
tap A 60
mark rode
tap A 40
mark blstmenu
tap A 40
mark target
tap B 60
mark backout
tap A 40
mark target2
tap A 40
mark attack
wait 700
mark done
