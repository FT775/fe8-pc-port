header Round 10 "paths" (port/test/inputs/paths/README.md).
header Start: boot both sides with GATE0_SRAM_IN=i14_start.sav (the save's first 32 KiB: FNV-1a 83286ce8).
header route_i14's way to its first free cursor (1,735 ticks), then R on each of the two enemy Bishops (class 0x2B) at
header (21,11) and (7,11): their stat screens (BattleGenerateUiStats, IsUnitEffectiveAgainst with gBattleTarget's
header pClassData NULL, D-48).
prefix port/test/inputs/route_i14.keys 1735
wait 10
rep 17 UP
tap LEFT
wait 20
mark bishop1
tap R 80
mark stat1
tap B 60
rep 14 LEFT
wait 20
mark bishop2
tap R 80
mark stat2
tap B 60
mark done
wait 20
