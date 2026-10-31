header Round 10 "paths" (port/test/inputs/paths/README.md).
header Start: boot both sides with GATE0_SRAM_IN=ch2g_start.sav (the save's first 32 KiB: FNV-1a aa7e9be3).
header ch2g_start is the SRAM of monkey/ch2_start_s1.keys at tick 7,340: the suspend the game writes at turn 1's green
header phase start. Resumed: Ross (green) heals on his fort first thing, gBattleTarget not filled since boot (a green
header actor goes right, the NULL target left).
prefix port/test/inputs/resume.keys 2000
wait 1500
