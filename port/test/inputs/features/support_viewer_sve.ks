header Coverage 12 "features" (port/test/inputs/features/README.md): the Extras support viewer, hand-made.
header Start: boot both sides with GATE0_SRAM_IN=sv_e_start.sav (the save's first 32 KiB: FNV-1a c67cf1cb; the SRAM
header at the end of route_f_supports_tower_e.keys from e_clear.sav, sram_capture SRAM_AT=tick:120069: Eirika and
header Ephraim's C, B and A seen, so the global support record has their A).
header The title menu's Extras, Support Conversation, Eirika, Ephraim: the A conversation watched to its end with A,
header the B conversation watched (170 presses) then skipped with Start, back to the character list, Extras, the title.
prefix port/test/inputs/resume.keys 1400
wait 150
rep 5 DOWN 20
wait 60
tap A 200
tap DOWN 40
tap DOWN 40
mark support
tap A 300
mark eirika
tap A 200
tap RIGHT 40
tap RIGHT 40
mark conv_a
tap A 300
rep 83 A 20
wait 100
mark conv_b
tap LEFT 40
tap A 300
rep 170 A 20
wait 60
mark skip
tap START 300
tap B 200
tap B 200
mark title
tap B 200
wait 200
