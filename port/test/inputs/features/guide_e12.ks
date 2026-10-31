header Coverage 12 "features" (port/test/inputs/features/README.md): the Guide, hand-made.
header Start: boot both sides with GATE0_SRAM_IN=e12_start.sav (the save's first 32 KiB: FNV-1a 40dea610).
header route_f_support_e12.keys to Eirika's Chapter 12 first free cursor (tick 1,544), then the map menu's Guide:
header every category, two topics' texts in Basic Rules, a Combat topic, Sort, and back to the map.
prefix port/test/inputs/route_f_support_e12.keys 1544
wait 60
rep 6 UP 8
wait 40
tap A 120
tap DOWN 30
tap DOWN 30
mark guide
tap A 200
rep 16 DOWN 40
rep 16 UP 30
tap A 60
tap A 120
rep 4 DOWN 50
tap B 60
tap DOWN 40
tap A 120
rep 4 DOWN 50
tap B 60
tap B 60
tap DOWN 40
tap DOWN 40
tap A 60
rep 5 DOWN 30
tap A 120
rep 3 DOWN 50
tap B 60
tap SEL 100
rep 5 DOWN 30
tap SEL 100
tap B 60
tap B 100
tap B 200
mark map
wait 200
