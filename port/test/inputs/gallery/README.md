# The item gallery (coverage 12): every item of the game used once

**Boosted, edited and cheat-assisted: test coverage only, not story play, not fair play.**

`port/test/inputs/route_gallery.keys` takes every item of the rolling convoy's list (the
`convoy` test cheat's 177 items, port/test/README.md "Test cheats") from the supply once and
uses it once, with the battle animations on, and the GBA and the plain PC are compared on every
traced field and every frame of the whole file. Weapons attack an enemy (every sword, lance,
axe, bow, tome, the legendary weapons, the Divinestone, the dark items that are not
monster-locked); staves are cast on a valid target; promotion items, stat boosters, heal items,
Pure Water, Antitoxin, the Torch, the Metis Tome, the Juna Fruit and the keys are used on their
user, door or chest; the items that have no Use command (gems, gold, the cards, the shields, the
notes) are taken and carried. Made by the bot's gallery mode (`route_bot.lua`, `L.GAL`,
`ROUTE_GALLERY=1`) from `gal_start`.

## The start: `gal_start` (boosted and edited)

`port/test/Makefile` `CHAIN_SAVES` entry `gal_start:gallery/gal_prep:edit:1513-1700:i21_start`:
`sram_boost.lua` plays `gallery/gal_prep.keys` (the bot's own `ROUTE_SUSPEND=1` run from
`i21_start`: Ephraim's final chapter, part 1, chapterIndex 0x22, turn 1; the map menu's Suspend at
tick 1,513), applies `gallery/gal_prep.edit` (`BOOST_EDIT_FILE`) at the end of tick 1,513 and keeps
the SRAM at tick 1,700, after the game's own suspend save. Resume Chapter on it is turn 1 of that
map with the edited army. `make -f port/test/Makefile chain-save CHAIN_SAVE=gal_start` makes it
(from `i21_start`, which the chain makes too); its `.sav.json` says `"what":"boosted and edited
start (test coverage only, not story play)"`.

What the edit does (each line of `gal_prep.edit` says why):
- The supply's lord on Ephraim's route is Ephraim (`SupplyUsability`, src/bmmenu.c). He stands on
  (11,18); the four tiles next to him are the supply's stations.
- The army: the four living units and 22 of the dead ones of `i21_start` put back on the map
  (`state=0`), in the south half of the map (rows 14 to 24, where no AREA event of
  src/events/ch21b-eventinfo.h can start a reinforcement), every item taken away (every item comes
  from the supply), power 0 (so that few enemies die), some hurt (for the heals), Colm and Kyle
  poisoned (Antitoxin, Restore), Eirika and Ephraim back in their unpromoted Lord classes (Lunar and
  Solar Brace), units at level 10 in the classes the promotion items fit (Marisa, Garcia, Forde,
  Kyle, Neimi, Tana, Lute, Colm, Amelia), L'Arachel as a Valkyrie, Ewan as a Druid.
- Weapon ranks: S (251) in at most one type per unit, for the promoted unit that uses that type's
  S items (Seth swords, Franz lances, Dozla axes, Innes bows, Knoll anima, Saleh light, Ewan dark,
  L'Arachel staves), A (181) elsewhere. The game keeps an S rank only while no other type of the
  unit is at S, and an unpromoted unit drops to A after its first battle
  (`GetBattleUnitUpdatedWeaponExp`, src/bmbattle.c): a first try with S in every type lost the
  S items after the first battles.
- Every red unit: no item (no enemy attacks or counters), 60 HP, defence and resistance 30, luck
  30, AI 3 and 3; twelve Wights moved round Ephraim's tile and the south room, two next to each
  station and one two tiles off it (for the bows). The suspend save keeps 5 bits of a unit's
  defence and resistance: 40 came back as 8, so 30.
- Fog of war (`PLAY vision=3`, `gPlaySt.chapterVisionRange`), for the Torch staff and item.
- The convoy emptied but for an Antitoxin (`CONVOY clear`, `CONVOY add 6F`): the rolling convoy
  then holds the cheat list's first 99 items; the Antitoxin is item 108 of the list, which would
  come after the poison (7 turns) has worn off.
- The edit file's new words (`sram_boost.lua`'s header): `U<hex>` (a unit by its index byte, any
  faction), `state`, `caps`, `ai1`, `ai2`, `PLAY vision=N`, `CONVOY clear`, `CONVOY add XX`.

## The bot's gallery mode (`ROUTE_GALLERY=1`, `L.GAL` in route_bot.lua)

```
make -f port/test/Makefile chain-save CHAIN_SAVE=gal_start
GATE0_SRAM_IN=port/test/saves/gal_start.sav ROUTE_OUT=port/test/inputs/route_gallery.keys ROUTE_STOP=gallery:300 \
  ROUTE_TICK=capture ROUTE_GALLERY=1 ROUTE_CHEAT=durability+funds+convoy ROUTE_MAX_TURN=60 MGBA_RUN_BP=$(GBA_BP) \
  build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/route_bot.lua 400000000
```

- **One item a supply visit.** The supply screen's take rewrites the convoy from the screen's own
  list (`PrepItemSupply_TakeItemFromSupply` calls `sub_80982B8`: `ClearSupplyItems`, then the list
  back), so an item the rolling convoy cheat puts in while the screen is open is lost at the next
  take: k takes in one visit lose k(k-1)/2 of the list's items (they come back only when the list
  goes round). In a try with up to four takes a visit (scratch `g3`) the Guiding Ring, the keys and
  most gold items vanished from the convoy this way before anyone took them. One take a visit loses none.
- Each turn: the lord takes one item (only what he can use from his tile, while the others still
  need the supply); a unit holding an untried item it can use somewhere in reach goes and uses it
  (off the stations if it can); a unit with items to take goes to a free station (not two turns
  running, so that the stations go round), discards the items it has used (not the unsellable ones,
  "Treasure can't be dropped"), takes one (the items the fewest units can use first; the Antitoxin
  and Restore before all), and uses it there if it can; a unit that cannot reach its use or a station
  this turn walks toward it; a unit with nothing to do is not selected.
- Uses: weapons attack the nearest enemy in range; Heal, Mend, Recover and Physic the least hurt
  ally (so that Fortify, then Latona, still find hurt ones); Restore a poisoned ally; Silence, Sleep
  and Berserk an enemy; Warp an ally to the nearest free tile of its warp range; Rescue an ally two or
  more tiles off; Barrier an ally; Torch a tile of its range; Unlock a door; the Door Key next to a
  door; the Chest Key and the Lockpick (a thief) on a chest; every other usable item on its user.
- Each use is logged: the tick it was planned, the tick of the next free cursor, the unit, how, and
  what changed (a battle animation seen, the target's HP and status, the user's stats, class and
  level, a door's or chest's terrain, the item used up). The key file's header lists every item
  (`# Item gallery`), so the file is its own record; the table below is the same.

## Not tried (16 of the 177, by rule)

- **The 11 monster-locked weapons** (`IA_LOCK_3`: Sharp Claw 0x8B, Wretched Air 0x90, Demonstone
  0xA7, Demonlight 0xA8, Ravager 0xA9, Rotten Claw 0xAD, Fetid Claw 0xAE, Venin Claw 0xAF, Lethal
  Talon 0xB0, Fire Fang 0xB1, Hell Fang 0xB2): the user's decision. (By the game's rule
  `CanUnitUseWeapon` lets any unit whose class has `CA_LOCK_3` use them, Myrrh's Manakete among them,
  so they could be tried with her later.) The dark-type monster items have no lock and are tried
  (Demon Surge 0xAB, Shadowshot 0xAC, Evil Eye 0xB3, Crimson Eye 0xB4, Stone 0xB5: Ewan and Knoll).
- **Hammerne (0x57):** with the durability cheat no item is ever worn, so it has no target
  (`TryAddUnitToHammerneTargetList`).
- **The three ballistae (0x35-0x37) as inventory items:** a first try (scratch `g1`, tick 88,364)
  had Innes, a Sniper, attack with a Ballista he had taken from the supply: the retail game, in
  mGBA, jumped to 0x01000130 and ran no further ("Jumped to invalid address", "Illegal opcode").
  Left for a check of its own (what the PC does there is not known).
- **Chest Key (5 uses) (0x79):** the map's two doors and two chests go to the Door Key, Unlock, the
  Chest Key and the Lockpick; it shares the Chest Key's code (`CanUnitUseChestKeyItem`,
  `ExecKeyItem`).
- The convoy cheat's list itself leaves out the 28 "Dummy" ids (Mani Katti, Forblaze, Luce, the
  Mine, the Light Rune, the four FE7 rings, the Heaven Seal, 0xBC-0xCD).

## PC check

Run 2026-10-23/24 (coverage 12): `make -f port/test/Makefile chain-route CHAIN=gallery` with
`GATE0_SHOT_TICKS=0-159705` (**every frame**), the plain PC (`build/pc-gate0`, this branch's game code),
the cheats on both sides from the file's header, `OUT=build/gate0-gal`; then the PC lag replay again
with the NULL probe and the compare by hand.

- **The plain PC faults at tick 148,848**, right after Moulder's Warp on Colm: SIGSEGV in `GetUnitMu`
  (`src/mu.c:1490`, reading `0x0000002C`): `GetMu(i)` gives NULL for a free MU slot and the loop reads
  `proc->unit` from it; the caller is `PostWarpStaff_RefreshMap` (`src/bmusemind.c:241`, `ProcScr_PostWarpStaffAction`,
  which ends the warped unit's MU). The GBA reads the BIOS's open-bus latch there and goes on. A new site
  for the fix round (the NULL probe classes it "relocate read", latch `E3A02004`, post-SWI); photo of both
  sides 8 ticks before: `~/fe8-snapshots/c12-gallery/tick148840` (parts gba, pclag; `savestate.py check`
  OK).
- **A harness limit, fixed:** the first lag replay stopped at tick 5,018 with "more lag VBlanks in one tick
  than the hook keeps". Each enemy phase here starts with one main-loop pass of 380 to 1,685 VBlanks on the
  GBA (39 such ticks, 28,369 lag VBlanks in all; the same with AI1 6, DoNothing, scratch try `g8`), where the
  hook kept 256: `port/test/pc/gate0_hook.c` now keeps 4,096 (`LAG_USED_MAX`; only the array bounds change).
- **With the NULL probe** (`FE8PC_NULLPROBE=1`: that read gets the latch and the run goes on; it is the only
  new site, the 9 others are the known H-1 drops of a write to `0x34`) the lag replay ran all 159,706
  ticks: **GATE 0 (LAG REPLAY vs GBA): DIFFERENT**, first difference at tick 27,278, the lag replay leaves
  the GBA's sequence at tick 94,511. Every traced field is equal on every tick (`gBmSt`, `gPlaySt`, the RNG,
  the clock, the palettes, OAM, BG2, BG3, `sTrapPool`, the cheats' `cheat_items`, `cheat_refills`,
  `cheat_funds`, `cheat_convoy`) except BG0 and BG1 on 13 ticks; the main-loop events are the GBA's except
  on 16 ticks; 28,369 GBA lag VBlanks run at the GBA's positions. Frames: 159,706 compared, 1,392
  raster-timing frames (P-17, labelled), **156 other frames**:

| Ticks | Item or moment | What differs |
|---|---|---|
| 27,278 | Ivaldi (Saleh attacks) | 1 frame: with the GBA's timing the PC still draws another frame |
| 52,555 to 52,617 | Torch, the item (Ephraim; fog) | 54 frames: the GBA stored nothing watched while it drew them |
| 94,511 to 110,507 | the map cursor resting on a unit (Eirika on (11,17), Franz (11,14), Myrrh (8,14), Marisa (8,16), Ephraim (11,18)), turns 21 to 26, 7 times | the PC's main loop one event more or fewer (16 ticks), the BG0 and BG1 tilemaps (13 ticks), 11 frames; photo `tick94505` |
| 120,882 to 120,894 | Gleipnir (Ewan) | 3 frames |
| 127,456 | Recover (Natasha) | 1 frame |
| 129,436 to 129,492 | Naglfar (Ewan) | 6 frames |
| 130,087 | Physic (Moulder) | 1 frame |
| 142,528 to 142,591 | Torch, the staff (Lute; fog) | 56 frames |
| 148,848 | Warp (Moulder on Colm) | the plain PC faults (above) |
| 157,190 to 157,317 | Unlock (Natasha, the door at (18,19)) | 23 frames |

Every other use matched on every traced field and every frame (labelled raster-timing frames aside).
Photos for the first frame difference and the Torch item's: `~/fe8-snapshots/c12-gallery/tick27270`,
`tick52550` (see the pending note for the ones made). Scratch: `~/gate0-scratch/c12-gallery/cmp/`
(`compare_probe.log`, `pc_lagreplay_probe.log`, `pc_lagreplay_plain.log`, `route.log`); the run's
`compare.json` is in `build/gate0-gal/`.

Byte for byte with the committed bot: `route_gallery.keys` made again from `gal_start` (IDENTICAL), and,
for the gating, `route_f_suspend_ch6.keys` (capture rule) and `route_ch0_prologue.keys` (frame rule)
(IDENTICAL).

## Every item (from the key file's header)

| Id | Item (items.h) | Kind | Unit | How | Ticks | Result |
|---|---|---|---|---|---|---|
| 0x01 | SWORD_IRON | weapon | Seth | attacks the enemy 8c (class 56) at (12,17) | 29419-30104 | a battle (its animation), no damage |
| 0x02 | SWORD_SLIM | weapon | Eirika | attacks the enemy 8c (class 56) at (12,17) | 33828-34407 | a battle (its animation), no damage |
| 0x03 | SWORD_STEEL | weapon | Franz | attacks the enemy 8d (class 56) at (10,19) | 49812-50596 | a battle (its animation), no damage |
| 0x04 | SWORD_SILVER | weapon | Eirika | attacks the enemy 8c (class 56) at (12,17) | 53298-54007 | a battle (its animation), no damage |
| 0x05 | BLADE_IRON | weapon | Seth | attacks the enemy 8c (class 56) at (12,17) | 58161-58855 | a battle (its animation), no damage |
| 0x06 | BLADE_STEEL | weapon | Franz | attacks the enemy 8d (class 56) at (10,19) | 58875-59714 | a battle (its animation), no damage |
| 0x07 | BLADE_SILVER | weapon | Eirika | attacks the enemy 8c (class 56) at (12,17) | 63493-64122 | a battle (its animation), no damage |
| 0x08 | SWORD_VENIN | weapon | Seth | attacks the enemy 8c (class 56) at (12,17) | 68157-68867 | a battle (its animation), target status 00 -> 51, no damage |
| 0x09 | SWORD_RAPIER | weapon | Eirika | attacks the enemy 8c (class 56) at (12,17) | 7361-7962 | a battle (its animation), no damage |
| 0x0B | SWORD_BRAVE | weapon | Franz | attacks the enemy 8d (class 56) at (10,19) | 68887-70056 | a battle (its animation), no damage |
| 0x0C | SWORD_SHAMSIR | weapon | Eirika | attacks the enemy 8b (class 56) at (10,17) | 15818-16455 | a battle (its animation), no damage |
| 0x0D | SWORD_KILLER | weapon | Eirika | attacks the enemy 8b (class 56) at (10,17) | 73400-74173 | a battle (its animation), no damage |
| 0x0E | SWORD_ARMORSLAYER | weapon | Seth | attacks the enemy 8b (class 56) at (10,17) | 77953-78667 | a battle (its animation), no damage |
| 0x0F | SWORD_WYRMSLAYER | weapon | Franz | attacks the enemy 8d (class 56) at (10,19) | 78687-79546 | a battle (its animation), no damage |
| 0x10 | SWORD_LIGHTBRAND | weapon | Eirika | attacks the enemy 8b (class 56) at (10,17) | 81993-82828 | a battle (its animation), no damage |
| 0x11 | SWORD_RUNESWORD | weapon | Seth | attacks the enemy 8b (class 56) at (10,17) | 86106-86925 | a battle (its animation), no damage |
| 0x12 | SWORD_LANCEREAVER | weapon | Franz | attacks the enemy 8d (class 56) at (10,19) | 86945-87854 | a battle (its animation), no damage |
| 0x13 | SWORD_ZANBATO | weapon | Eirika | attacks the enemy 8b (class 56) at (10,17) | 90271-91146 | a battle (its animation), no damage |
| 0x14 | LANCE_IRON | weapon | Seth | attacks the enemy 8b (class 56) at (10,17) | 94991-95729 | a battle (its animation), no damage |
| 0x15 | LANCE_SLIM | weapon | Franz | attacks the enemy 8b (class 56) at (10,17) | 95749-96627 | a battle (its animation), no damage |
| 0x16 | LANCE_STEEL | weapon | Seth | attacks the enemy 8b (class 56) at (10,17) | 1341-1987 | a battle (its animation), no damage |
| 0x17 | LANCE_SILVER | weapon | Franz | attacks the enemy 8c (class 56) at (12,17) | 2007-2748 | a battle (its animation), no damage |
| 0x18 | LANCE_VENIN | weapon | Seth | attacks the enemy 8b (class 56) at (10,17) | 102952-103726 | a battle (its animation), target status 00 -> 51, no damage |
| 0x19 | LANCE_BRAVE | weapon | Seth | attacks the enemy 8b (class 56) at (10,17) | 11128-12232 | a battle (its animation), no damage |
| 0x1A | LANCE_KILLER | weapon | Franz | attacks the enemy 8b (class 56) at (10,17) | 12252-13115 | a battle (its animation), no damage |
| 0x1B | LANCE_HORSESLAYER | weapon | Seth | attacks the enemy 8b (class 56) at (10,17) | 20100-20828 | a battle (its animation), no damage |
| 0x1C | LANCE_JAVELIN | weapon | Ephraim | attacks the enemy 8b (class 56) at (10,17) | 5464-6051 | a battle (its animation), no damage |
| 0x1D | LANCE_SPEAR | weapon | Ephraim | attacks the enemy 8b (class 56) at (10,17) | 751-1321 | a battle (its animation), no damage |
| 0x1E | LANCE_AXEREAVER | weapon | Franz | attacks the enemy 8b (class 56) at (10,17) | 20848-21728 | a battle (its animation), no damage |
| 0x1F | AXE_IRON | weapon | Duessel | attacks the enemy 8b (class 56) at (10,17) | 2768-3431 | a battle (its animation), no damage |
| 0x20 | AXE_STEEL | weapon | Gerik | attacks the enemy 8d (class 56) at (10,19) | 3451-4132 | a battle (its animation), no damage |
| 0x21 | AXE_SILVER | weapon | Duessel | attacks the enemy 8d (class 56) at (10,19) | 13597-14385 | a battle (its animation), no damage |
| 0x22 | AXE_VENIN | weapon | Duessel | attacks the enemy 8d (class 56) at (10,19) | 22278-23102 | a battle (its animation), no damage |
| 0x23 | AXE_BRAVE | weapon | Duessel | attacks the enemy 8e (class 56) at (12,19) | 31547-32608 | a battle (its animation), no damage |
| 0x24 | AXE_KILLER | weapon | Duessel | attacks the enemy 8e (class 56) at (12,19) | 40000-40787 | a battle (its animation), no damage |
| 0x25 | AXE_HALBERD | weapon | Duessel | attacks the enemy 8e (class 56) at (12,19) | 50966-51773 | a battle (its animation), no damage |
| 0x26 | AXE_HAMMER | weapon | Duessel | attacks the enemy 8e (class 56) at (12,19) | 59742-60550 | a battle (its animation), no damage |
| 0x27 | AXE_DEVIL | weapon | Duessel | attacks the enemy 8d (class 56) at (10,19) | 71355-72039 | a battle (its animation), no damage |
| 0x28 | AXE_HANDAXE | weapon | Duessel | attacks the enemy 8d (class 56) at (10,19) | 79939-80760 | a battle (its animation), no damage |
| 0x29 | AXE_TOMAHAWK | weapon | Duessel | attacks the enemy 8d (class 56) at (10,19) | 88298-89111 | a battle (its animation), no damage |
| 0x2A | AXE_SWORDREAVER | weapon | Duessel | attacks the enemy 8d (class 56) at (10,19) | 97091-97914 | a battle (its animation), no damage |
| 0x2B | AXE_SWORDSLAYER | weapon | Duessel | attacks the enemy 8d (class 56) at (10,19) | 105138-105976 | a battle (its animation), no damage |
| 0x2C | AXE_HATCHET | weapon | Duessel | attacks the enemy 8d (class 56) at (10,19) | 112854-113732 | a battle (its animation), no damage |
| 0x2D | BOW_IRON | weapon | Innes | attacks the enemy 92 (class 56) at (11,21) | 7982-8611 | a battle (its animation), no damage |
| 0x2E | BOW_STEEL | weapon | Innes | attacks the enemy 9e (class 56) at (8,18) | 16475-17187 | a battle (its animation), no damage |
| 0x2F | BOW_SILVER | weapon | Innes | attacks the enemy 9e (class 56) at (8,18) | 25052-25786 | a battle (its animation), no damage |
| 0x30 | BOW_VENIN | weapon | Innes | attacks the enemy 9e (class 56) at (8,18) | 34427-35135 | a battle (its animation), target status 00 -> 51, no damage |
| 0x31 | BOW_KILLER | weapon | Innes | attacks the enemy 9e (class 56) at (8,18) | 54027-54731 | a battle (its animation), no damage |
| 0x32 | BOW_BRAVE | weapon | Innes | attacks the enemy 9e (class 56) at (8,18) | 64142-65100 | a battle (its animation), no damage |
| 0x33 | BOW_SHORTBOW | weapon | Innes | attacks the enemy 9e (class 56) at (8,18) | 74193-74908 | a battle (its animation), no damage |
| 0x34 | BOW_LONGBOW | weapon | Innes | attacks the enemy 9e (class 56) at (8,18) | 82848-83595 | a battle (its animation), no damage |
| 0x35 | BALLISTA_REGULAR | not tried |  | not tried by rule: ballista item: an Attack with it from a unit's items makes the retail game jump to 0x01000130 (mGBA), so it is left for its own check |  |  |
| 0x36 | BALLISTA_LONG | not tried |  | not tried by rule: ballista item: an Attack with it from a unit's items makes the retail game jump to 0x01000130 (mGBA), so it is left for its own check |  |  |
| 0x37 | BALLISTA_KILLER | not tried |  | not tried by rule: ballista item: an Attack with it from a unit's items makes the retail game jump to 0x01000130 (mGBA), so it is left for its own check |  |  |
| 0x38 | ANIMA_FIRE | weapon | Saleh | attacks the enemy 8e (class 56) at (12,19) | 66069-67036 | a battle (its animation), no damage |
| 0x39 | ANIMA_THUNDER | weapon | Saleh | attacks the enemy 8d (class 56) at (10,19) | 17885-18794 | a battle (its animation), no damage |
| 0x3A | ANIMA_ELFIRE | weapon | Saleh | attacks the enemy 8e (class 56) at (12,19) | 35536-36463 | a battle (its animation), no damage |
| 0x3B | ANIMA_BOLTING | weapon | Saleh | attacks the enemy 8c (class 56) at (12,17) | 46385-47679 | a battle (its animation), no damage |
| 0x3C | ANIMA_FIMBULVETR | weapon | Saleh | attacks the enemy 8e (class 56) at (12,19) | 55653-56777 | a battle (its animation), no damage |
| 0x3E | ANIMA_EXCALIBUR | weapon | Knoll | attacks the enemy 8d (class 56) at (10,19) | 92780-93804 | a battle (its animation), target HP 38 -> 12 |
| 0x3F | LIGHT_LIGHTNING | weapon | L'Arachel | attacks the enemy 8c (class 56) at (12,17) | 17215-17865 | a battle (its animation), no damage |
| 0x40 | LIGHT_SHINE | weapon | L'Arachel | attacks the enemy 8c (class 56) at (12,17) | 25806-26541 | a battle (its animation), no damage |
| 0x41 | LIGHT_DIVINE | weapon | L'Arachel | attacks the enemy 8e (class 56) at (12,19) | 45638-46357 | a battle (its animation), no damage |
| 0x42 | LIGHT_PURGE | weapon | L'Arachel | attacks the enemy 8d (class 56) at (10,19) | 54759-55625 | a battle (its animation), no damage |
| 0x43 | LIGHT_AURA | weapon | L'Arachel | attacks the enemy 8e (class 56) at (12,19) | 65128-66041 | a battle (its animation), no damage |
| 0x45 | DARK_FLUX | weapon | Knoll | attacks the enemy 8d (class 56) at (10,19) | 100875-101912 | a battle (its animation), no damage |
| 0x46 | DARK_LUNA | weapon | Knoll | attacks the enemy 8d (class 56) at (10,19) | 108668-109812 | a battle (its animation), no damage |
| 0x47 | DARK_NOSFERATU | weapon | Knoll | attacks the enemy 8c (class 56) at (12,17) | 115942-116903 | a battle (its animation), no damage |
| 0x48 | DARK_ECLIPSE | weapon | Knoll | attacks the enemy 8d (class 56) at (10,19) | 123244-124260 | a battle (its animation), target HP 12 -> 6 |
| 0x49 | DARK_FENRIR | weapon | Knoll | attacks the enemy 8c (class 56) at (12,17) | 134550-135864 | a battle (its animation), no damage |
| 0x4A | DARK_GLEIPNIR | weapon | Ewan | attacks the enemy 8c (class 56) at (12,17) | 120260-121238 | a battle (its animation), no damage |
| 0x4B | STAFF_HEAL | staff | L'Arachel | heals the ally 0e at (13,18) | 74936-75765 | a battle (its animation), target HP 5 -> 15 |
| 0x4C | STAFF_MEND | staff | Saleh | heals the ally 0e at (13,18) | 83623-84434 | a battle (its animation), target HP 15 -> 35 |
| 0x4D | STAFF_RECOVER | staff | Natasha | heals the ally 17 at (12,19) | 127260-127979 | a battle (its animation), target HP 5 -> 18 |
| 0x4E | STAFF_PHYSIC | staff | Moulder | heals the ally 1e at (11,20) | 129912-130655 | a battle (its animation), target HP 22 -> 25 |
| 0x4F | STAFF_FORTIFY | staff | L'Arachel | heals the allies in range (Fortify) | 138690-139727 | a battle (its animation) |
| 0x50 | STAFF_RESTORE | staff | L'Arachel | cures the status of the ally 1e at (14,19) | 10303-11025 | a battle (its animation), target status 51 -> 00 |
| 0x51 | STAFF_SILENCE | staff | Saleh | on the enemy 8b at (10,17) | 142744-143459 | a battle (its animation) |
| 0x52 | STAFF_SLEEP | staff | Natasha | on the enemy 8b at (10,17) | 143479-144430 | a battle (its animation) |
| 0x53 | STAFF_BERSERK | staff | L'Arachel | on the enemy 8b at (10,17) | 145152-145856 | a battle (its animation) |
| 0x54 | STAFF_WARP | staff | Moulder | warps the ally 13 at (14,16) | 148082-148849 | target moved (14,16) -> (15,17), maxHp 20 -> 21, pow 0 -> 1, level 3 -> 4 |
| 0x55 | STAFF_RESCUE | staff | Saleh | brings next to it the ally 02 at (9,15) | 148869-149787 | target moved (9,15) -> (10,18), pow 0 -> 1, level 7 -> 8 |
| 0x56 | STAFF_TORCH | staff | Lute | lights a tile (fog) | 142212-142724 | done |
| 0x57 | STAFF_REPAIR | not tried |  | not tried by rule: Hammerne: with the durability cheat no item is ever worn, so it has no target |  |  |
| 0x58 | STAFF_UNLOCK | staff | Natasha | opens the door at (4,19) | 157069-157485 | terrain at (4,19) 1e -> 17 |
| 0x59 | STAFF_BARRIER | staff | L'Arachel | puts a barrier on the ally 10 at (10,16) | 153460-154431 | a battle (its animation), target torch/barrier 00 -> 70, skl 7 -> 8, spd 11 -> 12, res 8 -> 9, lck 14 -> 15, level 5 -> 6 |
| 0x5A | AXE_DRAGON | weapon | Garcia | attacks the enemy 8d (class 56) at (10,19) | 116923-117536 | a battle (its animation), no damage |
| 0x5B | BOOSTER_HP | use | Ephraim | uses it (boost) | 9564-10069 | hp 38 -> 45, maxHp 38 -> 45 |
| 0x5C | BOOSTER_POW | use | Myrrh | uses it (boost) | 13135-13577 | pow 0 -> 2 |
| 0x5D | BOOSTER_SKL | use | Ephraim | uses it (boost) | 14861-15306 | skl 18 -> 20 |
| 0x5E | BOOSTER_SPD | use | Ephraim | uses it (boost) | 19195-19648 | spd 17 -> 19 |
| 0x5F | BOOSTER_LCK | use | Myrrh | uses it (boost) | 21756-22250 | lck 10 -> 12 |
| 0x60 | BOOSTER_DEF | use | Ephraim | uses it (boost) | 23333-23778 | def 16 -> 18 |
| 0x61 | BOOSTER_RES | use | Ephraim | uses it (boost) | 28514-28967 | res 11 -> 13 |
| 0x62 | BOOSTER_MOV | use | Myrrh | uses it (boost) | 31025-31519 | mov 0 -> 2 |
| 0x63 | BOOSTER_CON | use | Ephraim | uses it (boost) | 32839-33284 | con 0 -> 2 |
| 0x64 | HEROCREST | use | Marisa | uses it (promo) | 60570-61926 | a battle (its animation), hp 26 -> 28, maxHp 26 -> 28, pow 0 -> 1, skl 14 -> 15, spd 15 -> 16, def 4 -> 6, res 3 -> 4, level 10 -> 1, class 20 -> 24 |
| 0x65 | KNIGHTCREST | use | Forde | uses it (promo) | 135884-137288 | a battle (its animation), hp 5 -> 7, maxHp 25 -> 27, pow 0 -> 1, skl 10 -> 11, spd 8 -> 9, def 9 -> 11, res 3 -> 4, level 10 -> 1, class 5 -> 7 |
| 0x66 | ORIONSBOLT | use | Neimi | uses it (promo) | 132285-133650 | a battle (its animation), hp 5 -> 9, maxHp 17 -> 21, pow 0 -> 3, skl 5 -> 6, spd 6 -> 7, def 3 -> 5, res 2 -> 4, level 10 -> 1, class 26 -> 28 |
| 0x67 | ELYSIANWHIP | use | Tana | uses it (promo) | 124280-125831 | a battle (its animation), hp 5 -> 10, maxHp 24 -> 29, pow 0 -> 2, spd 16 -> 18, def 6 -> 8, res 8 -> 10, level 10 -> 1, class 72 -> 73 |
| 0x68 | GUIDINGRING | use | Lute | uses it (promo) | 130675-132269 | a battle (its animation), hp 18 -> 21, maxHp 18 -> 21, pow 0 -> 1, skl 6 -> 7, def 3 -> 6, res 5 -> 8, level 10 -> 1, class 38 -> 40 |
| 0x69 | CHESTKEY | key | Myrrh | opens the chest at (21,18) | 61962-62221 | used up |
| 0x6A | DOORKEY | key | Myrrh | opens the door at (18,19) | 52960-53099 | terrain at (18,19) 1e -> 17, used up |
| 0x6B | LOCKPICK | key | Marisa | opens the chest at (2,18) | 158935-159182 | done |
| 0x6C | VULNERARY | use | Innes | uses it (heal) | 99904-100460 | hp 5 -> 15 |
| 0x6D | ELIXIR | use | Innes | uses it (heal) | 107637-108257 | hp 15 -> 39 |
| 0x6E | PUREWATER | use | Ephraim | uses it (purewater) | 36858-37310 | sb 0 -> 112 |
| 0x6F | ANTITOXIN | use | Colm | uses it (antitoxin) | 6811-7341 | st 97 -> 0 |
| 0x70 | TORCH | use | Ephraim | uses it (torch) | 52168-52722 | sb 64 -> 68 |
| 0x71 | DELPHISHIELD | carry | Ephraim | took it from the supply and carries it (no Use command | 62751-62997 | kept, sold or passive) |
| 0x72 | MEMBERCARD | carry | Ephraim | took it from the supply and carries it (no Use command | 67427-67725 | kept, sold or passive) |
| 0x73 | SILVERCARD | carry | Ephraim | took it from the supply and carries it (no Use command | 72566-72852 | kept, sold or passive) |
| 0x74 | WHITEGEM | carry | Ephraim | took it from the supply and carries it (no Use command | 77207-77505 | kept, sold or passive) |
| 0x75 | BLUEGEM | carry | Myrrh | took it from the supply and carries it (no Use command | 79574-79919 | kept, sold or passive) |
| 0x76 | REDGEM | carry | Ephraim | took it from the supply and carries it (no Use command | 81155-81441 | kept, sold or passive) |
| 0x77 | GOLD | carry | Ephraim | took it from the supply and carries it (no Use command | 85345-85638 | kept, sold or passive) |
| 0x78 | LANCE_REGINLEIF | weapon | Ephraim | attacks the enemy 8b (class 56) at (10,17) | 156054-156756 | a battle (its animation), no damage |
| 0x79 | CHESTKEY_BUNDLE | not tried |  | not tried by rule: Chest Key (5 uses): the map has two doors and two chests, which the Door Key, Unlock, the Chest Key and the Lockpick open; it shares the Chest Key's code (CanUnitUseChestKeyItem, ExecKeyItem) |  |  |
| 0x7C | HOPLON_SHIELD | carry | Myrrh | took it from the supply and carries it (no Use command | 87882-88278 | kept, sold or passive) |
| 0x81 | SWORD_SHADOWKILLR | weapon | Marisa | attacks the enemy 8c (class 56) at (12,17) | 91928-92760 | a battle (its animation), no damage |
| 0x82 | LANCE_BRIGHTLANCE | weapon | Franz | attacks the enemy 8b (class 56) at (10,17) | 103746-104679 | a battle (its animation), no damage |
| 0x83 | AXE_FIENDCLEAVER | weapon | Duessel | attacks the enemy 8c (class 56) at (12,17) | 118589-119478 | a battle (its animation), target HP 23 -> 19 |
| 0x84 | BOW_BEACONBOW | weapon | Innes | attacks the enemy 9e (class 56) at (8,18) | 91166-91908 | a battle (its animation), no damage |
| 0x85 | SWORD_SIEGLINDE | weapon | Eirika | attacks the enemy 8b (class 56) at (10,17) | 24286-25032 | a battle (its animation), target HP 60 -> 45 |
| 0x86 | AXE_BATTLEAXE | weapon | Gerik | attacks the enemy 8d (class 56) at (10,19) | 121258-122068 | a battle (its animation), no damage |
| 0x87 | LIGHT_IVALDI | weapon | Saleh | attacks the enemy 8d (class 56) at (10,19) | 26565-28283 | a battle (its animation), target HP 60 -> 38 |
| 0x88 | MASTERSEAL | use | Amelia | uses it (promo) | 137304-138674 | a battle (its animation), hp 3 -> 5, maxHp 16 -> 18, pow 0 -> 1, skl 3 -> 4, spd 4 -> 5, def 2 -> 4, level 10 -> 1, class 71 -> 10 |
| 0x89 | METISSTOME | use | Ephraim | uses it (metis) | 57096-57729 | done |
| 0x8B | MONSTER_SHARPCLAW | not tried |  | not tried by rule: monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried) |  |  |
| 0x8C | STAFF_LATONA | staff | L'Arachel | heals every ally (Latona) | 140053-140913 | a battle (its animation) |
| 0x8D | LANCE_DRAGON | weapon | Franz | attacks the enemy 8b (class 56) at (10,17) | 30124-30997 | a battle (its animation), no damage |
| 0x8E | LANCE_VIDOFNIR | weapon | Franz | attacks the enemy 8b (class 56) at (10,17) | 38638-39636 | a battle (its animation), target HP 45 -> 29 |
| 0x8F | DARK_NAGLFAR | weapon | Ewan | attacks the enemy 8c (class 56) at (12,17) | 128931-129892 | a battle (its animation), no damage |
| 0x90 | MONSTER_WRETCHAIR | not tried |  | not tried by rule: monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried) |  |  |
| 0x91 | SWORD_AUDHULMA | weapon | Seth | attacks the enemy 8c (class 56) at (12,17) | 37738-38618 | a battle (its animation), target HP 60 -> 32 |
| 0x92 | LANCE_SIEGMUND | weapon | Ephraim | attacks the enemy 8b (class 56) at (10,17) | 157716-158561 | a battle (its animation), target gone (killed) |
| 0x93 | AXE_GARM | weapon | Dozla | attacks the enemy 8b (class 56) at (10,17) | 119498-120240 | a battle (its animation), target HP 21 -> 3 |
| 0x94 | BOW_NIDHOGG | weapon | Innes | attacks the enemy 9e (class 56) at (8,18) | 44789-45610 | a battle (its animation), target HP 54 -> 26 |
| 0x95 | LANCE_HEAVYSPEAR | weapon | Seth | attacks the enemy 8c (class 56) at (12,17) | 49084-49792 | a battle (its animation), no damage |
| 0x96 | LANCE_SHORTSPEAR | weapon | Ephraim | attacks the enemy 8e (class 56) at (12,19) | 48074-48632 | a battle (its animation), no damage |
| 0x97 | OCEANSEAL | use | Colm | uses it (promo) | 125847-127244 | a battle (its animation), hp 18 -> 21, maxHp 20 -> 23, pow 0 -> 1, def 5 -> 7, res 1 -> 3, level 10 -> 1, class 13 -> 23 |
| 0x98 | LUNARBRACE | use | Eirika | uses it (promo) | 43215-44773 | a battle (its animation), hp 27 -> 31, maxHp 27 -> 31, pow 0 -> 2, skl 14 -> 16, spd 15 -> 16, def 11 -> 14, res 11 -> 16, level 2 -> 1, class 2 -> 4 |
| 0x99 | SOLARBRACE | use | Ephraim | uses it (promo) | 41182-42667 | a battle (its animation), hp 45 -> 49, maxHp 45 -> 49, pow 0 -> 2, skl 20 -> 23, spd 19 -> 21, def 18 -> 20, res 13 -> 18, level 4 -> 1, class 1 -> 3 |
| 0x9A | 1G | carry | Ephraim | took it from the supply and carries it (no Use command | 89409-89715 | kept, sold or passive) |
| 0x9B | 5G | carry | Ephraim | took it from the supply and carries it (no Use command | 94181-94499 | kept, sold or passive) |
| 0x9C | 10G | carry | Myrrh | took it from the supply and carries it (no Use command | 96655-97071 | kept, sold or passive) |
| 0x9D | 50G | carry | Ephraim | took it from the supply and carries it (no Use command | 98145-98451 | kept, sold or passive) |
| 0x9E | 100G | carry | Marisa | took it from the supply and carries it (no Use command | 100480-100859 | kept, sold or passive) |
| 0x9F | 3000G | carry | Ephraim | took it from the supply and carries it (no Use command | 102143-102456 | kept, sold or passive) |
| 0xA0 | 5000G | carry | Myrrh | took it from the supply and carries it (no Use command | 104707-105118 | kept, sold or passive) |
| 0xA1 | SWORD_WINDSWORD | weapon | Eirika | attacks the enemy 8b (class 56) at (10,17) | 98999-99884 | a battle (its animation), no damage |
| 0xA2 | VULNERARY_2 | use | Seth | uses it (heal) | 111001-111616 | hp 5 -> 15 |
| 0xA3 | UNK_GREENNOTE | carry | Ephraim | took it from the supply and carries it (no Use command | 106371-106677 | kept, sold or passive) |
| 0xA4 | UNK_REDNOTE | carry | Eirika | took it from the supply and carries it (no Use command | 107225-107617 | kept, sold or passive) |
| 0xA5 | DANCE | carry | Marisa | took it from the supply and carries it (no Use command | 108277-108652 | kept, sold or passive) |
| 0xA6 | NIGHTMARE | carry | Ephraim | took it from the supply and carries it (no Use command | 110207-110493 | kept, sold or passive) |
| 0xA7 | DEMONSTONE | not tried |  | not tried by rule: monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried) |  |  |
| 0xA8 | DEMONLIGHT | not tried |  | not tried by rule: monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried) |  |  |
| 0xA9 | RAVAGER | not tried |  | not tried by rule: monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried) |  |  |
| 0xAA | DIVINESTONE | weapon | Myrrh | attacks the enemy 8e (class 56) at (12,19) | 70096-71335 | a battle (its animation), target gone (killed) |
| 0xAB | MONSTER_DEMONSURG | weapon | Ewan | attacks the enemy 8c (class 56) at (12,17) | 141358-142192 | a battle (its animation), no damage |
| 0xAC | MONSTER_SHADOWSHT | weapon | Knoll | attacks the enemy 8c (class 56) at (12,17) | 145876-147335 | a battle (its animation), no damage |
| 0xAD | MONSTER_ROTTENCLW | not tried |  | not tried by rule: monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried) |  |  |
| 0xAE | MONSTER_FETIDCLW | not tried |  | not tried by rule: monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried) |  |  |
| 0xAF | MONSTER_VENINCLW | not tried |  | not tried by rule: monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried) |  |  |
| 0xB0 | MONSTER_LTHLTALON | not tried |  | not tried by rule: monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried) |  |  |
| 0xB1 | MONSTER_FIREFANG | not tried |  | not tried by rule: monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried) |  |  |
| 0xB2 | MONSTER_HELLFANG | not tried |  | not tried by rule: monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried) |  |  |
| 0xB3 | MONSTER_EVILEYE | weapon | Ewan | attacks the enemy 8d (class 56) at (10,19) | 149890-150677 | a battle (its animation), no damage |
| 0xB4 | MONSTER_CRIMSNEYE | weapon | Knoll | attacks the enemy 8d (class 56) at (10,19) | 151819-153225 | a battle (its animation), no damage |
| 0xB5 | MONSTER_STONE | weapon | Ewan | attacks the enemy 8c (class 56) at (12,17) | 154594-155486 | a battle (its animation), target status 00 -> 5b, no damage |
| 0xB6 | ANIMA_AIRCALIBUR | weapon | Saleh | attacks the enemy 8d (class 56) at (10,19) | 75793-76812 | a battle (its animation), no damage |
| 0xB7 | JUNAFRUIT | use | Franz | uses it (juna) | 111632-112355 | level 17 -> 15 |
| 0xB8 | 150G | carry | Myrrh | took it from the supply and carries it (no Use command | 112383-112834 | kept, sold or passive) |
| 0xB9 | 200G | carry | Ephraim | took it from the supply and carries it (no Use command | 114127-114468 | kept, sold or passive) |
| 0xBA | BLACKGEM | carry | Eirika | took it from the supply and carries it (no Use command | 115020-115455 | kept, sold or passive) |
| 0xBB | GOLDGEM | carry | Innes | took it from the supply and carries it (no Use command | 115475-115918 | kept, sold or passive) |

