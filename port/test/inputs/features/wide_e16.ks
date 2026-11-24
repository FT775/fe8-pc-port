header Widescreen milestone M2 (v1.27.0, port/tools/widecheck.sh route e16wide), hand-made, PC only (never run on the GBA).
header Start: e16_start.sav. route_f_lordpromo_e16.keys to Chapter 16's first free cursor (tick 5,882, on Eirika), then:
header the cursor 3 down to Gerik, selected; the cursor far left (the camera to the map's left edge, Gerik and his
header range in the right side), back right inside his range (the arrow across the seam), the danger zone on Select
header and off (Reforged danger_zone_select = 1), his move from the side across the seam, Wait, the map menu's End
header and the first enemy phase (camera at the map's right edge, the standing units in the left side).
prefix port/test/inputs/route_f_lordpromo_e16.keys 5882
wait 40
rep 3 DOWN 8
wait 20
mark select
tap A 40
rep 15 LEFT 8
wait 30
rep 9 RIGHT 10
mark arrow
wait 60
tap SEL 80
mark zone
wait 40
tap SEL 80
mark move
tap A 120
tap UP 20
tap A 120
mark end
wait 40
rep 3 LEFT 10
tap A 40
tap UP 20
tap A 60
mark enemy
wait 1500
