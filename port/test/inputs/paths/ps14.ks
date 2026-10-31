header Integration 11 "nullunit2" (port/test/inputs/paths/README.md), an edited start (test coverage only, not story play).
header Start: boot both sides with GATE0_SRAM_IN=ps14_start.sav (the save's first 32 KiB: FNV-1a 54cb2aa8).
header Resume Chapter (resume.keys), Eirika's Chapter 14, turn 1. Ewan (made a Summoner by ps14_prep.edit) summons his
header phantom (1 HP, an Iron Axe) onto (17,21); the phantom attacks the Soldier on the stairs at (17,20), whose counter
header kills it: UnitKill clears its pCharacterData while it is still gActiveUnit, and PlayerPhase_FinishAction's
header TryMakeCantoUnit reads its attributes through NULL (the audit's N2). Then the free cursor again.
prefix port/test/inputs/resume.keys 2000
wait 20
mark start
rep 3 UP
wait 20
mark ewan
tap A 30
tap A 30
mark menu
tap DOWN 10
tap A 40
mark summonsel
wait 10
tap LEFT 20
mark left
wait 10
tap A 30
mark summon
wait 300
mark summoned
tap LEFT 20
mark phantom
tap A 30
tap A 30
mark pmenu
tap A 30
mark weapon
tap A 30
mark target
tap A 30
mark battle
wait 600
mark after
tap RIGHT 20
tap DOWN 20
wait 200
mark end
wait 10
