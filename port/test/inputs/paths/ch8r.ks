header Round 10 "paths" (port/test/inputs/paths/README.md).
header Start: boot both sides with GATE0_SRAM_IN=ch8r_start.sav (the save's first 32 KiB: FNV-1a 899c302c).
header ch8r_start is the SRAM of route_ch8.keys at tick 72,800: the suspend the game writes at turn 14's enemy phase
header start. Resumed: the boss (red) heals on his throne first thing, gBattleTarget not filled since boot: a red actor
header with a non-red target goes left, so the NULL target is on the right (D-48's right-side reads).
prefix port/test/inputs/resume.keys 2000
wait 1500
