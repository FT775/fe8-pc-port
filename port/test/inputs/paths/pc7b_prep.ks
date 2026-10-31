header Round 10 "paths" (port/test/inputs/paths/README.md): the way to the edited start pc7b_start (test coverage only, not story play).
header Start: boot both sides with GATE0_SRAM_IN=ch7_start.sav (the save's first 32 KiB: FNV-1a c6729800).
header route_ch7's way to its first free cursor (1,639 ticks), then Eirika waits in place: the game writes a suspend save
header after sram_boost.lua's edit (pc7b_prep.edit) at the end of tick 1,640; chain-save takes the SRAM at tick 1,900.
prefix port/test/inputs/route_ch7.keys 1639
wait 10
tap A 20
tap A 30
tap UP 10
tap A 10
wait 200
