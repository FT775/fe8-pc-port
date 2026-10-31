header Coverage 12 "features" (port/test/inputs/features/README.md): the Sound Room, hand-made.
header Start: boot both sides with GATE0_SRAM_IN=e_clear.sav (the save's first 32 KiB: FNV-1a 44491be0; Eirika's real-army clear save).
header The title menu's Extras, Sound Room: songs played from the list (the visualizer), the list scrolled
header to its end, Random (Select), Stop (Start), back to the Extras menu and the title menu.
prefix port/test/inputs/resume.keys 1400
wait 150
rep 4 DOWN 20
wait 60
tap A 200
tap DOWN 40
mark soundroom
tap A 300
tap A 600
tap RIGHT 30
tap A 600
rep 4 DOWN 30
tap A 600
rep 12 DOWN 30
tap A 600
rep 12 DOWN 30
tap A 400
tap SEL 600
tap START 200
tap SEL 400
tap START 200
tap B 200
mark extras
tap B 200
wait 200
