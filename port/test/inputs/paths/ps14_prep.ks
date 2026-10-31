header Integration 11 "nullunit2" (port/test/inputs/paths/README.md): the way to the edited start ps14_start (test coverage only, not story play).
header Start: boot both sides with GATE0_SRAM_IN=e14_start.sav (the save's first 32 KiB: FNV-1a e97d186b).
header route_e14's way to its first free cursor (1,725 ticks), then Eirika waits in place: the game writes a suspend save
header after sram_boost.lua's edit (ps14_prep.edit) at the end of tick 1,726; chain-save takes the SRAM at tick 1,990.
header (The same presses as pe14_prep.keys; only the edit differs.)
prefix port/test/inputs/route_e14.keys 1725
wait 10
tap A 20
tap A 30
tap UP 10
tap A 10
wait 200
