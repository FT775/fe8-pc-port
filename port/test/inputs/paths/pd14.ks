header Integration 11 "nullunit2" (port/test/inputs/paths/README.md), an edited start (test coverage only, not story play).
header Start: boot both sides with GATE0_SRAM_IN=pd14_start.sav (the save's first 32 KiB: FNV-1a 36c43639).
header Resume Chapter (resume.keys), Eirika's Chapter 14, turn 1. Dozla (1 HP, a Devil Axe, and US_DROP_ITEM from
header pd14_prep.edit) attacks the cracked wall at (20,13); the backfire kills him, so BATTLE_HandleItemDrop reads
header UNIT_FACTION(unitB) with unitB = GetUnit(0) = NULL (then P-27's record). His death quote, then the free cursor.
prefix port/test/inputs/resume.keys 2000
wait 20
mark start
tap RIGHT
rep 11 UP
wait 20
mark dozla
tap A 20
tap A 30
mark menu
tap A 30
mark weapon
tap A 30
mark target
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
wait 10
