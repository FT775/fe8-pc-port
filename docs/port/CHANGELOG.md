# Changelog

## v1.23.0 (2026-11-09)

### New
- **Third promotion** (a new Game fixes row, the owner's table; Off by default, for the next New Game, marks saves): in a game without the randomizer, every promotion screen offers three classes, the same in every game. The game's own choices come first, unchanged, then a third from the owner's table: Myrmidons Blade Lord, Mercenaries Master Lord, Knights, Fighters, Brigands and Pirates Armor Lord, Cavaliers and Archers the Trooper of the unit's sex, the FE7 Nomads Paladin, Mages, Monks, Priests, Clerics and Troubadours Archsage, Shamans Dark Druid, Thieves Swordmaster, Pegasus Knights Wyvern Lord, Wyvern Riders Falcoknight (a woman) or Great Knight (a man); Eirika Blade Lord and Master Lord, Ephraim Armor Lord and General, the FE6 Lion Lord Great Lord and Blade Lord; the trainees' second promotion the missing class of their line's pair (Great Knight, Warrior, Mage Knight). A class that needs the FE7 or FE6 ROM you do not have gets an FE8 class of the unit's sex that is not already on the screen, with the missing class's weapons where FE8 has one (with no extra ROM: Eirika Great Lord, Ranger, Swordmaster; Ephraim Great Lord, Great Knight, General). The full table, with and without each ROM, is in PLAYING.md (Game fixes, Third promotion).
- Every way to promote shows the same three classes: the crests and seals, the twins' braces, the preparations, the skills preview, and Chapter 16's "yes", which opens the three-choice screen for each twin still in their Lord class (a twin who already promoted is not promoted again). With the row on the twins keep the Rapier, Sieglinde, Reginleif and Siegmund in any class. Trainees' first promotion keeps the original's rule (the third only once both routes are cleared).
- Third promotion, the classes the row adds: an added class that does not use the unit's weapons leaves no rank in a kind it cannot use and gives one basic weapon when the unit lost the use of its own (only a weapon it could use before the promotion counts: an Archer with a C bow rank holding only a Silver Bow, made a Warrior, gets nothing, as with the game's own Sniper), always the plain one of a kind the new class uses: Iron Sword, Iron Lance, Iron Axe, Iron Bow, Fire, Lightning or Flux (an Archer made a Swordmaster: an Iron Sword; a Cavalier made an FE6 Trooper keeps no lance rank, the Trooper having swords and bows only); an added class never lowers a rank in a weapon kind it uses (the game's promotion takes the old class's base rank off and adds the new class's, which is lower where the table or a stand-in crosses lines: a Fighter made a General, a Priest made a Druid and an Archer made a Warrior keep their axe, staff and bow rank instead of losing 30 points and the use of a D-rank weapon or staff); Wyvern Lord and Druid for a woman, which have no promotion gains in the game's data, give the male class's gains (before, the Pegasus Knight's Wyvern Lord raised no stat); and a seal used on the map on a tile an added class could not stand on (a man's Wyvern Rider over a peak, a river or the sea: Great Knight; a Fighter, Brigand, Pirate, Ephraim or a woman's second-tier Journeyman on a mountain, a Brigand, Pirate or Ephraim in a river, a Pirate in water: Armor Lord or General) shows the game's own choices only, since a promotion does not move the unit. The game's own choices, randomized games and games with the row off are unchanged; in a randomized game the row is not applied, the in-chapter menu's "This run" shows it Off and the saves carry no mark for it.
- **Class descriptions name the weapons** (the owner: "make sure any gained skills + weapon skills are mentioned"), in a game with the Third promotion row on: every FE7/FE6 class's description is two lines in the game's own style, ending with what the class can equip (for example Armor Lord: "Heavily armored lords of great might. Equip: Swords, lances, axes."; Archsage: "Equip: Anima, light, dark, staves."), on the promotion screen and in the stat screen's class help. Games with the row off and randomized games keep v1.22.0's texts on every screen: the FE7 ROM's text for the FE7 classes (flavour only) and the FE6 classes' three-line text, whose first line the promotion screen's two-line box scrolls away (Master Lord, Trooper). None of these classes has a battle skill in a normal game, so none is named; the female Thief's says "Skill: Steal.". With the row on, the promotion screen also shows a corrected text for the FE8 classes whose own description names no weapon or not all of them: Great Lord ("Equip: Lances." Ephraim's, "Equip: Swords." Eirika's), the third-tier Pupil ("Equip: Anima, light, dark.") and the promoted monsters (no promotion in such a game offers one today). With the Skills game fix on, the promotion screen keeps its "Gains / Keeps / Loses" skills text. Text only: nothing else about a class, a game or a save changes.
- Test switch `FE8PC_TEST_UNIT_LEVEL=pid:level[,pid:level...]`: sets those player units' levels when `FE8PC_TEST_UNIT_CLASS` would act (with its `pid:class:item`, for trying a promotion item on any class).

### Technical
- The row uses v1.21.0's one promotion list (`Reforged_PromoList`: the screen, its menu, the handler, Chapter 16's event and the skills preview), now also for a game without the randomizer with the row on (`Promo3ListEx`, reforged_rando_classes.c). Randomized games of every version and games with the row off take exactly the paths they took before. A unit promoted into an FE7/FE6 class is saved through the existing stand-ins (`<block>_fe7` in the side file): checked for Blade Lord, Master Lord, Armor Lord, Trooper, Archsage and Dark Druid, suspend and chapter saves, loaded with and without the ROMs by this build and by v1.22.0. After a promotion into a class the row added, `Reforged_RandoPromoted` gives the randomizer's after-care (`Promo3Care`; gains from the class's other-sex twin, unused ranks cleared, a basic weapon for one lost; a loaded FE7/FE6 class through its save stand-in's facts and its own ranks, `sP3Foreign`, as in a version 11-18 run); `Reforged_RunFix(RFO_FIX_PROMO3)` is 0 in a randomized or carried game, so the save mark and the "This run" row say what is applied; `P3TileBars` / `P3UnitTerrain` are the tile rule. `FE8PC_PROMO3_CHECK` now also takes every added class through the real `ApplyUnitPromotion` ("promotions tried"; "foreign promotions tried": the FE7/FE6 classes, by a unit with trained ranks holding a weapon of a kind the class lacks) and checks the tile rule (mountains and rivers too), and that a unit holding only a weapon above its rank is given nothing ("a weapon given for one the unit could never use"; `Reforged_RandoPromoting` notes which items the unit could use, `P3WasUsable`).
- The descriptions: every `sSpecs` row (reforged_fe7.c) has a `desc3` (this port's words), which `Reforged_Fe7VirtualMsg` returns only while `Reforged_Promo3Active()` (the row on, no randomizer); otherwise it returns what v1.22.0 did (the FE7 ROM's decoded description, the FE6 rows' `desc`). The FE8 classes' promotion-screen texts are virtual text ids 0x7F60-0x7F6C (`sPromoDescFix`, reforged_skills.c, through the existing `RFO_SKILL_PROMO_DESC` hook), under the same condition; the game's text data and the GBA build are untouched.
- The added classes' ranks and weapon: `Reforged_RandoPromoting` remembers the unit's eight ranks; after the game's rank arithmetic `P3KeepRanks` sets each kind the new class uses to the highest of the rank before, the game's result worked out without its u8 wrap, and the class's base; `P3BasicWeapon` picks the kind as before (the unit's and class's fixed hash) and gives that kind's plain weapon instead of one from the randomizer's E-rank pool (which holds the Slim weapons and the monster-slayers). Only in the row's own after-care: the randomizer's paths are untouched. `FE8PC_PROMO3_CHECK` checks all of it for every added class (no rank lowered in a used kind, a rank under the old base does not wrap, a given weapon is its kind's plain one, the descriptions follow the row and stay v1.22.0's in a randomized game).
- New self-test `FE8PC_PROMO3_CHECK=1` (`=exit` quits with its result): every class with a promotion row, for either sex and each set of ROMs the loaded ones allow (none, FE7, FE6, both): three different classes, the game's own first, the table's class wherever its ROM is loaded, never a class of a missing ROM, the row's tier, the unit's sex and a weapon kind of the unit's class (logged where no class of that sex and tier has one), every foreign choice with a battle animation for each weapon kind it has and a save stand-in of the unit's sex, tier and weapons, the weapon kinds used without a ROM equal to the loaded classes', the twins' locks on only with the row; it logs the whole table for each ROM set.
- The Game fixes block gained one option (`fix_third_promotion` in the settings file); the options after it moved by one in memory only (the settings file, the side file and the randomizer's lines are keyed by name).

## v1.22.0 (2026-11-09)

### New
- **Keep colours on every class** (randomizer, the owner's request: "make all classes get custom palettes per character's color combinations"). In randomized games started in v1.22.0 or later with Keep colours On (the default), every class a character is shown in has that character's colours: the FE7/FE6 classes (Nomad, Blade Lord, Armor Lord, Archsage, Dark Druid, Bard, female Thief, Trooper, King, Lion Lord, Master Lord, Divine Dragon), the FE8 classes no character had colours for (Soldier, Journeyman, Recruit, Pupil, the female Druid, Wyvern Rider F and the others), Eirika and Ephraim in other classes (their hair), and enemies and NPCs with colours of their own (the bosses) in a class of the run. Generic soldiers keep the generic colours. Display only: nothing a seed decides changes.
- **A `roms` folder**: the release zip has an empty `roms` folder with "PUT YOUR ROMS HERE.txt". The game looks there for your FE8 ROM, and for the optional FE7 and FE6 ROMs, after every place it looked before (so a ROM that started the game still does).
- **Your save stays next to the game**: put your ROM in the `roms` folder or pick it (below): your save stays next to the game, where it always was. For a ROM the game finds in `roms`, or one you picked (or picked at an earlier start), the save is exactly the one it would use if that ROM file sat next to `fe8pc-sdl.exe`: the same file name (`FE8.gba` -> `FE8.sav`), in the exe's folder, with the same per-user folder rules as before. So moving your ROM from beside the exe into `roms` keeps your save, and a picked ROM never gets a save beside it. Everything else is as in v1.21.0: a ROM next to the exe, `baserom.gba`, or a ROM dropped onto the exe or named on the command line (wherever it is) keeps its save next to it. No save is ever moved, merged or renamed. The console build `fe8pc.exe` still keeps a save only when its command line says `--save`.
- **First-start ROM picker** (Windows): when no original ROM is found (none, or only a hack or cut-short copy: named in a message first, and still used, for its error message, on Cancel), a "Choose your Fire Emblem: The Sacred Stones ROM" window opens (starting in `roms`). The file is checked as before (wrong game, hack or cut-short copy refused with the reason; pick again or Cancel), and its path is remembered in `%APPDATA%\fe8pc\rom-path.txt` for the next starts (asked again if the file is gone). The ROM is never copied. Cancel shows the old "no ROM found" message.

### How the colours are made
- A class with colour data keeps learning its roles from the characters' sets as before. A class with none gets them from its own generic palette: its team colours are the parts its faction rows paint blue / red / green (FE8's scheme: the clothes that turn red on the enemy side), its personal colours the other parts its rows change (a generic unit's hair) and, for a class drawn as one character (the lords, the King, the female Thief, the Bard, the Recruit and Pupil), its hair; remaining coloured parts are matched by colour similarity against an FE8 class with data, the one of a short list whose generic palette is nearest (chosen: Nomad and Trooper Ranger, Blade Lord Myrmidon F, Armor Lord General, King Swordmaster, Archsage Sage, Dark Druid Druid, Bard Dancer, Thief F Thief, Lion Lord Myrmidon, Master Lord Hero, Soldier Fighter, Journeyman Pirate, Recruit Cavalier F, Pupil Shaman). Hair takes the character's hair (their most colourful own ramp, preferring where their class's generic hair is), so nobody gets team colours on their hair. Eirika and Ephraim bring only their hair, and it goes only on the class's hair (a generic unit's hair, else FE8's usual hair colours; under a helmet the part the vanilla characters' own sets recolour, such as the Paladin's barding, never the horse's coat or mane), and on a part the class's rows colour by faction only in the player's row. Nobody's colours go on the outline, and a class's skin takes only a character's skin tone.
- The sets are built when a battle (or the promotion screen) first shows them and kept in a 126-slot cache that reuses the slot used longest ago: a fight shows at most four sets, so any number of characters and classes fits the byte-sized palette index (0x81-0xFE).

### Technical
- Randomizer versions 15-18 (`RANDO_VERSION_ALLCOLOURS`): the same games as 11-14 (15 = 11, 16 = 12, 17 = 13, 18 = 14), with the colours above. Games started in 11-14 or earlier keep their old colours (an older version looks and plays as it did). Keep colours Off and games without the randomizer are unchanged. v1.21.0 and older builds load a v1.22.0 randomized save with the original tables (they keep its randomizer line but do not know version 15-18), as v1.17.0-v1.20.0 do with v1.21.0's.
- Fixed: in a version 12-14 game (v1.21.0) a character whose class is an FE7/FE6 one could, after the Controls menu had been opened in that session, show battle colours built from unrelated memory (Keep colours read past its tables for classes above 0x7F). Those classes now always show their generic colours in version 1-14 games, as PLAYING.md says.
- The randomizer self-test runs its configs as versions 11 and 14 again (the colours v1.21.0's saves show, built at the start) and each also as 15 / 18, checking the same tables, ROM sets and class picks.
- New hook `RANDO_CHARA_ENSURE` (battle intro, triangle attack, promotion screen): empty in the GBA build and in every game but a version 15-18 one with Keep colours.
- The randomizer self-test checks every character with colours on every class with a battle palette, FE8 and loaded foreign (both ROMs: 69 characters x 115 classes): a set for each, built through the cache (every index 0x81-0xFE, slots reused thousands of times, a fight's sets never reused), as built, different from the class's generic colours. `FE8PC_RANDO_PALDUMP=<file>` also writes `<file>.all` with every one of those sets.
- Test switches `FE8PC_TEST_KEEP_COLOURS=1` (the version 15 colours in any game) and `FE8PC_TEST_PAL_SRC=a[:b]` (player units show character a's colours, the others b's), for looking at the colours with the class test switches. `FE8PC_TEST_PICK_ROM=answer|answer...` answers the ROM picker in scripted runs.
- The Windows exe loads `comdlg32.dll` by name for the picker, as it does user32 for its message boxes.

## v1.21.0 (2026-11-09)

### New
- **Every promotion has three choices** (randomizer; the owner's rule: "every promotion moment should have 3 choices, allows for more agency even with a 'bad roll' for promotions"). In every randomized game started in v1.21.0 or later, whatever its settings and with or without the FE7/FE6 ROMs, each promotion screen offers three different classes: what it offered before first, then a third (with Random promotions: rarely a foreign class that fits, else a third random class under the same rules; without: the class's natural foreign promotion when that ROM is in the game, else an FE8 class of the same weapons and sex). Classes with one promotion (the twins' Lord without FE7, monsters, the Lion Lord) and trainees whose third is still locked get the screen too; Chapter 16's "yes" opens it for each unpromoted twin.
- **"FE7/FE6 classes"** (the Randomizer's "FE7 classes" row, renamed; Off by default; needs your own FE7 and/or FE6 ROM): every loaded foreign class can come up, rarely and only as an extra: as a promotion choice, as one of your units' classes (Nomad, female Thief, Lion Lord, Blade Lord, Master Lord, Armor Lord, Trooper, Archsage, Dark Druid; Tethys's Bard, Myrrh's Divine Dragon), and as late enemies from Chapter 15 (King and Armor Lord bosses, Nomad, Trooper and female Thief enemies).
- Test switch `FE8PC_TEST_ARENA_FOE_CLASS=class[:item][:crit]`: the arena opponent takes that class (and item; `:crit`: Skill 120), for looking at a class's battle animation in enemy colours.
- Test switch `FE8PC_RANDO_SCAN=first:count` (with `FE8PC_RANDO_SCAN_ROWS`, `FE8PC_RANDO_SCAN_UNITS`): logs what each seed gives (foreign players, promotion lists, a unit's class), for finding seeds.

### Rarities and rules
- Promotions: with Random promotions about 1 row in 4 gets a foreign third (magic rows: version 10's 1 in 8, then 1 in 7 more); sword users get Blade Lord (a woman's) or Master Lord (a man's), or the one lord of a game with only one of those ROMs (a ROM removed later gives the FE8 choice, never the other ROM's lord), lance/axe users on foot Armor Lord, bow users the Trooper of their sex, magic users Archsage or Dark Druid. The King is never a player choice.
- Players (Player classes On): 1 in 4 when the new class has a foreign counterpart whose save stand-in is that class (Archer, Thief, Myrmidon, Swordmaster, General, Ranger, Sage, Druid); Tethys's Bard and Myrrh's Divine Dragon 1 in 8; the twins never. The stand-in goes by the unit's sex, so with Match gender Off a unit in a class of the other sex gets only a counterpart that still saves as that class: the Nomad and the Trooper of the class's sex (a woman in the male Archer class: the male Nomad), never an Armor Lord, Archsage, Dark Druid, Lion Lord or sword lord (which would save as the General F, Sage F, Druid F, Myrmidon F or Swordmaster F, a class she is not in, and differ from the same seed without the ROM).
- Enemies (Boss / Enemy classes, from Chapter 15): King for a boss in a promoted human sword class, Armor Lord for a General boss (1 in 6); Nomad for an Archer, Trooper for a Sniper or Ranger (1 in 8); female Thief for an enemy thief (1 in 4; with Match gender only a woman).

### Technical
- Randomizer versions 11-14: 11 + the ROMs recorded at New Game (12 FE7, 13 FE6, 14 both); 11 with the row Off or no ROM. The ROM set is the version, so an older build that rewrites the side file cannot lose it. Every foreign roll and the third choices have their own salts (60040 + 0-5), rolled after every version 10 choice: a seed gives version 10's (9's) game but where a foreign roll fired. A foreign class must have a battle animation for each weapon kind it is given. A foreign enemy must fit its map both as the class it plays (an FE7 Nomad moves as a horseman) and as its stand-in.
- The promotion screen's choices come from one list (`Rando_PromoList`; the screen, its menu, the handler, Chapter 16's event and the skills preview ask it); single-promotion rows open the screen in such a game.
- The randomizer self-test checks versions 11-14 against 9 and 10 (tables, units, players, promotion rows and lists, every list three different classes, foreign picks only with their ROM, sex and weapons, animations for the items) and the save stand-ins: every fired player roll saves as its class of the run (and every player counterpart, for either sex and Match gender, as the class it replaces), every unit and boss roll as its class of the run or a class of its tier and weapons (the map-checked stand-in: a King's Swordmaster, a Sniper's Trooper's Ranger), every foreign promotion choice as a class of the row's tier, the unit's sex and the choice's weapons.

### Saves
- Foreign enemies and units save through the stand-ins as before (checked: a suspend with a King boss, a Trooper and a Nomad enemy; a suspend with an FE7 Nomad player; a chapter save with Eirika as FE6's Master Lord; each loaded with and without the ROMs, and by v1.20.0, which carries the version 14 game and keeps the stand-ins or the classes it knows).

## v1.20.0 (2026-11-09)

### New
- **More FE6 classes** (needs your own FE6 ROM, as v1.19.0's; test switch only for now):
  - **Lion Lord** (Roy's class) and **Master Lord** (Roy's promoted class): swords, FE6's stats and art. Lion Lord promotes to Master Lord with a Master Seal or a Hero Crest, gaining what FE6 gives (HP +4, Str +2, Skl +3, Spd +2, Def +2, Res +5, Con +2, Move +1). Master Lord swings every sword with the Binding Blade's animation.
  - **Divine Dragon**: FE6's white dragon as a Dragonstone class (no transformation), FE8's dragon map sprite; flies like Myrrh and, like her, takes extra damage from bows and anti-dragon weapons.
- **Myrrh look** (Helpers, and the in-chapter Reforged screen; Off by default, display only, never marks a save): Myrrh's dragon form in battle animations is FE6's Divine Dragon, with FE6's own transformation and way back. Grey, with its own help line, without the FE6 ROM.
- Test switches: `FE8PC_TEST_UNIT_CLASS` / `FE8PC_TEST_ARENA_CLASS` accept 0x8D-0x8F; `FE8PC_TEST_MYRRH_LOOK_PID` lets a test unit in Myrrh's class count as Myrrh.

### Technical
- FE6 class rows can now carry their own animation list (Master Lord: FE6's Binding Blade swing, roym_sw1, for every sword; all its commands mean the same in FE8, its flame effect included), take promotion gains from FE6's rule (promoted bases minus base-class bases) and show an FE8 class's map sprites.
- Myrrh look keeps FE8's whole transformation code (dragon status, background, death) and only swaps the three animations it plays, FE8's fifd_he1 / fifd_mg1 / fifd_hk1 for FE6's godd_he1 / godd_mg1 / godd_hk1 (FE6's manakete code runs the same flow), and the transformation's palettes. godd_hk1's unused modes point past its script in FE6 and are played as mode 0. 30 of the 40 animation slots are now in use with both ROMs.

### Saves
- Stand-ins: Myrmidon (female) for a Lion Lord, Swordmaster (female) for a Master Lord, Myrrh's Manakete class for a Divine Dragon, with chapter-save codes that v1.15.0-v1.19.0 read as "not one of mine" (they keep the stand-in). Myrrh look is a display option: it is not written to saves.

## v1.19.0 (2026-11-08)

### New
- **FE6 classes** (needs your own FE6 ROM: the Japanese Binding Blade or a fan translation of it; found next to the exe, the FE8 ROM or the FE7 ROM, or through `FE8PC_FE6_ROM`). Read at start-up and never copied anywhere; without the file nothing changes. The ROM is recognised by its header and by checks on the tables the port reads, so translated copies work. Log lines start with `[fe6]`.
  - **Thief (female)**, Cath's class: FE6's stats and art, FE8's Steal and lock picking; promotes to Assassin (female) or Rogue (Ocean Seal, Master Seal).
  - **Trooper** (Nomad Trooper, male and female): with the FE7 ROM too, the FE7 Nomad's promotion offers Trooper beside Ranger. Bow and sword animations, Ranger's promotion gains.
  - **King**, Zephiel's class: sword A, FE6's large animation. A boss class for later; for now only the test switch gives it.
- Test switches `FE8PC_TEST_UNIT_CLASS` and `FE8PC_TEST_ARENA_CLASS` accept the FE6 classes 0x89-0x8C.

### Technical
- The FE7 loader now has a second profile for FE6 (one code path: class rows, battle animations, map sprites, saves). FE6's file is read into memory and its pointers rebased (the emulated ROM area is full). FE6's class rows are converted (no promotion gains in FE6: the template class's or Ranger's; weapon ranks rescaled to FE8's scale; FE6 attribute bits FE8 reads differently are cleared). Two FE6 animation commands are rewritten: C3D (a sound in FE6, a blocking screen shake in FE8 that froze the King's attack) to C2B, which plays the same sound, and C43 (a sound FE8 has no command for) to nothing. The shared battle animation pool grew from 24 to 40 slots.

### Saves
- A unit in an FE6 class is saved as its stand-in (Thief, Ranger, Ranger female, Swordmaster), with the real class in the side file's `<block>_fe7` line and, in a chapter save, in the unit's spare flag bits as a code older versions read as "not one of mine" (they keep the stand-in). Without the FE6 ROM such a unit plays as its stand-in.

## v1.18.0 (2026-11-08)

### New
- **Armed dancers** (Game fixes screen; off by default, fixed for the run): dancers can fight instead of only dancing. Tethys (any Dancer) starts with light magic at rank E and can use Lightning; a Bard (below) starts with anima magic at rank E and can use Fire. Attack and Dance are both in the unit menu; dancing still refreshes. Their fights with a weapon always use the map animation, even with Animations on (they have no attack animation; Tethys's dance could freeze a fight in which she struck twice in a row). They still cannot enter the arena, as in the original.
- **Bard** (FE7 classes; needs your own FE7 USA ROM, as the other FE7 classes): Nils's class, with FE7's map sprites, name, description and playing animation. FE7's Play works as FE8's Dance (the same command, refresh and rings as a Dancer, and the Dancer's refresh sparkle). No unit becomes a Bard yet: the class is reachable only through the test switches for now. Without the FE7 ROM nothing changes.
- Test switch `FE8PC_TEST_UNIT_CLASS=pid:jid:item`: an optional third value also gives the unit that item (FE8 items 0x01-0xCD); the log line now shows the unit's position and weapon ranks. With "Armed dancers" on, the test switches give a Dancer or Bard the fix's rank.

### Saves
- A Bard is saved as a Dancer (the stand-in), with its anima rank in the side file, as for the other FE7 classes. In a chapter save its slot is the packed unit's flag bit 12 (bits 8-11 stay 0), which older versions ignore, so they load a plain Dancer. A save made with "Armed dancers" on is marked as usual.

## v1.17.0 (2026-11-08)

### New
- **Randomizer: FE7 classes** (new row in the Classes section; off by default, needs your own FE7 USA ROM): Archsage and Dark Druid come into randomized games as rare extras. They are no third tier and replace nobody; regular games never see them.
  - With **Random promotions**, about 1 in 8 human magic classes (Mage, Shaman, Monk, Priest, Cleric, Troubadour) gets a third choice at promotion: Archsage, or Dark Druid for Shamans. The promotion screen shows its preview, description and (with Skills) its skills; a staff-only class that promotes into one gets a tome.
  - With **Boss classes** / **Enemy classes**, from Chapter 15 on, a boss whose new class is a Sage, Bishop or Druid becomes an Archsage or Dark Druid 1 time in 4, and such a regular enemy 1 time in 12, with FE7's enemy colours, map sprite and power. A suspended chapter keeps them (the save holds a Sage or Druid stand-in, the side file the real class, as for your units).
  - Without the FE7 ROM the row is grey and has no effect. Whether a game includes FE7 classes is fixed when you start it: adding or removing the ROM later never changes its picks.

### Saves
- Randomizer versions 9 and 10: a game started with FE7 classes (row On and the FE7 ROM loaded) is version 10, any other new game version 9. The version itself records whether a game includes FE7 classes, so the choice survives the side file being rewritten by an older version of the port, which keeps a game's version and seed but drops settings it does not know. The side file's randomizer line also shows `randomizer_fe7`, but only as a copy of what the version says. Every pick a seed made in version 8 stays the same in both: the FE7 rolls are separate, made after the usual ones, and only add a promotion choice or change that one unit's class. Games started in older versions keep their own randomizer (no FE7 classes). Older versions of the port load a version 9 or 10 game with the original tables and keep its randomizer entry unchanged.

## v1.16.1 (2026-11-08)

### Changed
- **Chapter 16's story promotion now lets you choose** (with the FE7 ROM): answering "yes" to "promote now?" opens the promotion screen for each twin, the same one their brace shows. Eirika picks Great Lord or Blade Lord, Ephraim Great Lord or Armor Lord, with the same preview, description, stat gains and "You can now use" messages as the brace. The promotion is not optional there, so B does nothing on that screen. Great Lord gives exactly the unit the old automatic promotion gave. Answering "no" and using the brace works as before.
- Without the FE7 ROM nothing changes: "yes" promotes both twins to Great Lord at once, as in the original game. A twin the randomizer gave another class, or who is already promoted, is handled as before.

## v1.16.0 (2026-11-08)

### New
- **More FE7 classes** (optional, as in v1.15.0: they need your own Fire Emblem USA ROM): **Hector's Great Lord**, **Archsage** and **Dark Druid**, with FE7's battle animations, map sprites, names and descriptions. Hector's class is shown as **Armor Lord**, because FE8 already calls Eirika's and Ephraim's promoted class "Great Lord".
  - **Knights** (both sexes) can promote to Armor Lord as a third choice beside General and Great Knight. An Armor Lord uses swords and axes (with Hector's animations, hand axes thrown) and keeps a lance rank; with a lance he fights with the General's animation.
  - **Ephraim's Solar Brace** offers Great Lord (FE8's own, mounted) or Armor Lord. As an Armor Lord, Ephraim keeps Reginleif and Siegmund, and fights with his mounted Great Lord animation when he uses a lance.
  - **Archsage** and **Dark Druid** are in the game but nobody can promote to them yet (they are meant for the randomizer, in a later version). You can try them with the test switches. An Archsage has a Sage's weapon ranks plus dark magic, a Dark Druid a Druid's plus light magic (no S ranks from the start); their stats are a little above FE8's Sage and Druid.
  - Skills treat them as General, Ephraim's Great Lord, Sage and Druid. Saves store General / Ephraim's Great Lord / Sage / Druid as the stand-in, with any rank the stand-in cannot use kept in the side file, as in v1.15.0.
  - Chapter 16's story promotion (answering "yes") still promotes Eirika and Ephraim at once to FE8's own class (Great Lord). To get Blade Lord or Armor Lord, answer "no" and use the brace.
- Test switch `FE8PC_TEST_ARENA_WEAPON=item` (with `FE8PC_TEST_ARENA_CLASS`): the arena fighter uses that weapon, when the class can (`item:2` fights at range 2).

### Changed
- FE7 battle animations no longer show FE8's cape effect (command 0x47) or run FE7's dark aura command (0x3D), which FE8 does not have. Neither occurs in the v1.15.0 classes' animations.

### Saves
- A save holding one of the new classes still loads in v1.15.0, older versions and mGBA, as the stand-in class.

## v1.15.0 (2026-11-08)

### New
- **FE7 classes** (optional; needs your own Fire Emblem USA ROM next to the exe or the FE8 ROM, or named by `FE8PC_FE7_ROM`): Nomad, Blade Lord and a Blade Lord for Eirika, with FE7's battle animations, map sprites, names and descriptions. Myrmidons can promote to Blade Lord as a third choice; Eirika's Lunar Brace offers Master Lord or Blade Lord; Nomads promote to Ranger. Skills treat them as Archer, Swordmaster and Master Lord. Saves store a stand-in FE8 class without any weapon rank it cannot use (a Blade Lord's bow rank is kept in the side file and comes back with the FE7 class), so they still load without the FE7 ROM, in older versions and in mGBA. The FE7 ROM is only read at start-up and nothing from it is shipped; without it nothing changes, and randomizer seeds are the same either way. See PLAYING.md, "FE7 classes".
- Test switch `FE8PC_TEST_UNIT_CLASS=pid:class`: gives a player unit a class at the start of a chapter.

### Fixed
- The arena test switch (`FE8PC_TEST_ARENA_CLASS`) now gives the fighter its own class back when the arena closes (and saves its own class in a suspend made during the fight), and works out the arena's range and wager again when it hands out a different weapon.

## v1.14.0 (2026-11-08)

### New
- **Skill banners** (Helpers screen; on by default, display only): when Luna, Sol, Astra, Pavise, Aegis, Miracle, Vantage or Lifetaker activates, its name shows. In battle animations a banner slides in beside the unit's HP box as the attack starts (an Astra attack shows each skill once); in map animations a short popup appears by the unit.

### Fixed
- **Replays play out the same every time**: a few spots in the original code read a value that was never set (on the GBA these always get the same leftover value). On the PC they could pick up something different on each run; for example an enemy choosing its weapon could sometimes "equip" an empty item slot. Such values now always start at zero, so a recorded key file replays the same way every time.
- **Sol in the arena**: Sol no longer activates in the arena. It used to proc there without healing (the arena's rounds share one HP gauge, which decides the wager, so a heal between rounds can't be shown correctly).
- **Pass self-check**: the game fixes self-check (FE8PC_RFO_CHECK) now also checks that a unit boxed in by enemies moves only with Pass.

## v1.13.0 (2026-11-08)

### New
- **Late joiners' ranks** (Game fixes screen; off by default, fixed for the run): characters joining in Chapters 9-12 start with each weapon rank one letter higher, from Chapter 13 two letters higher (at most A; C for trainees).

## v1.12.0 (2026-11-08)

### New
- **Three more skills** (with Skills on):
  - **Astra** (Heroes and Swordmasters): Skill/2 % that an attack becomes 5 strikes at half damage, once a battle. It plays as one attack: faded copies of the unit wind up the critical-hit move at different speeds and join it for one blow, with one HP drop. Heroes now have Astra + Sol (a promoted Mercenary gives up Armsthrift); Swordmasters Vantage + Astra.
  - **Pass** (Thieves, Rogues, Assassins, and Tethys): move through enemy squares (not stop on them). It replaces their Spd +2 and Tethys's Charm; thieves promoted on older saves get Pass too.
  - **Nihil** (enemy and green bosses, with Enemy skills on): the foe's skills do nothing against them, FE8's own included.

## v1.11.0 (2026-11-08)

### New
- **Arena odds** (Helpers screen; on by default, display only): the arena shows its opponent before the wager, with both sides' hit, damage and crit.

### Changed
- **Skip ruin floors is now Ruin floor select** (Helpers screen; off by default): entering the Lagdou Ruins lets you pick any floor from 1 up to one past the best floor you've cleared. A run from floor 1 counts as before; a run started higher up only adds its kills to the total (the best records and Lyon's full-run count stay).

## v1.10.0 (2026-11-07)

### New
- **Myrrh buff** (Game fixes screen; off by default, fixed for the run): Myrrh's Dragonstone never breaks while she holds it, and her Speed is +2.

## v1.9.0 (2026-11-07)

### New
- **Faster supports** (Helpers screen: Off / x2 / x3; off by default): support points come 2 or 3 times as fast, and units within 3 squares of each other gain them, not only side by side. Pair Up's pairs still gain twice as fast as neighbours.

## v1.8.0 (2026-11-07)

### New
- **Skip ruin floors** (Helpers screen; off by default): the Lagdou Ruins start at the first floor you haven't cleared yet instead of floor 1. Clearing floor 10 starts the count over, so Lyon still needs three full runs; floors cleared before beating the game don't count afterwards, so Glen and Valter can still be recruited. The count is kept with each save.

## v1.7.0 (2026-11-07)

### New
- **Smarter enemies** (Game fixes screen; off by default, fixed for the run). Regular enemies stop throwing themselves away: they skip attacks that would do no real damage while the target hits back, and attacks whose counter would likely kill them (unless they would likely kill first), and prefer the safer of the attacks left. One that backs off and walks on stops outside the reach of the units it backed off from. Bosses, green units and berserk units fight as before.

## v1.6.0 (2026-11-07)

### New
- **Skills** (Game fixes screen: Off / Class / Personal; off by default, fixed for the run). Every unit gets its class's skills: one for an unpromoted class, a kept one plus a promotion skill for a promoted class (Sol, Luna, Pavise, Aegis, Lifetaker and others). With Personal, each playable character also gets a skill of their own (some only once promoted). FE8's own skills (Canto, Steal, Sure Strike and the rest) work as before and are listed with the others. The full list is in PLAYING.md.
  - **A Skills page** (the stat screen's 4th page, shown when the unit has skills): each skill and what it does, and "On promotion" lines saying what each promotion choice would add (+), keep (=) or take away (-). R on a line gives the full text. R on the class name also lists the skills.
  - **The promotion screen** shows, for each choice, which skills the unit gains, keeps and loses.
  - **Promoting keeps the old skill:** your promoted units keep the skill their old class gave them (a Fighter turned Hero keeps Wrath and gains Sol) instead of the new class's first skill. It is kept with each save and suspend in the `.reforged` file beside the save; older saves load as before.
- **Enemy skills** (Game fixes screen: Off / Promoted / All; off by default, fixed for the run): enemy and green units get their class skills too, either only promoted units and bosses or all of them. They never get personal skills.

### Changed
- **Pair Up:** the partner now shows as a shaded unit behind its lead on the map.

## v1.5.0 (2026-11-07)

### New
- **Pair Up** (Game fixes screen; off by default, fixed for the run like the other rows). Fates-style pairing built on Rescue, for your own units:
  - Anyone can pair with anyone (no Aid/Con limit).
  - The lead keeps full Skill and Speed and gets the partner's boost: its two best stats give +2 each (+3 if promoted), +1 per support rank. The partner's support bonuses always count.
  - **Dual Guard:** +10% per hit the lead lands, +20% per hit it takes; at 100% the partner blocks the next enemy hit completely. Shown on the stat screen ("Guard 60%").
  - **Swap** (unit menu): the partner takes the lead on the same square and can act. Free, as often as you like.
  - Pairs gain support points twice as fast.

## v1.4.0 (2026-11-07)

### New
- Four new randomizer rows (for new games; the switches are off by default, Monster promos is Separate):
  - **Random promotions** (Classes): every class that promotes offers two random promoted classes at promotion instead of its usual two: magic users get magic classes, weapon users weapon classes. The lords and trainees keep theirs; Match gender is respected. A unit whose new class can use none of its weapons gets a basic one.
  - **Monster promos** (Classes, with Random promotions): Separate keeps monster and other classes apart; Mixed lets either promote into either. A promotion into a monster gives the stat gains of the unit's usual promotion.
  - **Tower & Ruins** (Enemies): the Tower of Valni, Lagdou Ruins and skirmish monsters become other monsters of the same rank, so a floor is a mix instead of twenty of one kind, and no monster ever becomes a Draco Zombie or the Demon King (the deepest floors' own Draco Zombies stay). Drops stay the same. Each monster gets its own kind's attacks and is rerolled if it couldn't walk the map.
  - **Random music** (a new Music section): each chapter's map and battle themes are random picks from the game's own songs, the same for that chapter all game.

## v1.3.0 (2026-11-06)

### New
- **Helpers >** in Reforged Options, a new screen (the in-chapter Reforged screen lists the same rows):
  - **Enemy phase** (on at 4x by default): enemy turns run 2x or 4x faster. **Press Start during an enemy turn to skip the rest of it** (8x until your turn). It only speeds the game up: what happens is the same.
  - **True hit** (on by default): the battle forecast and the battle screen show the real chance to hit. The game rolls twice and averages, so a shown 70 is really 82% and a shown 30 is 18%.
  - **Convoy anywhere** (off by default): every unit can use Supply, not only the lord and the units next to the lord.
  - **Pass skirmishes** (off by default): on the world map, walk past monsters instead of being stopped by them; you fight them only when you stop on their place or they reach you.
  - **Rewind** (off by default): **hold Backspace** on the map to go back to the start of your turn; hold it again to go back one more turn (up to 8 in a chapter). It reloads the save the game itself makes at the start of each turn. Not with Nuzlocke.
  - **Keep game Options** moved here from Display.

### Changed
- **Select's default key is now Tab** (Backspace is the new Rewind key). A settings file that already names Select's key keeps it.
- Story scenes that play during an enemy turn (a chapter ending, a death quote, game over) run at normal speed with sound, even while the enemy phase is sped up or skipped.

## v1.2.1 (2026-11-04)

### Fixed
- **No more "Dummy" items.** FE8's item table still holds ten items from FE7 under the name "Dummy" (help "DUMMY"): the Mine, Light Rune, Luce, Forblaze, Mani Katti, the four dance rings and the Heaven Seal. The original never hands them out, but the randomizer could (a Mine or Light Rune before v1.1.9, a Luce light tome until now).
  - **Named:** each now shows its own name and help everywhere (Mine: "Place next to you. Hurts whoever steps on it."; Light Rune: blocks a square for 3 turns; Luce: a light tome, effective against dragons). The Mine and Light Rune still work as FE7's did, from FE8's own code.
  - **Safe in battle:** Luce and Forblaze, which have no spell of their own in FE8, cast Divine's and Elfire's.
  - **Removed:** new randomized games (randomizer version 8) never give any of them as a reward, shop item, drop or weapon. A game started earlier keeps its picks, but any of these it gives now has its proper name.

## v1.2.0 (2026-11-04)

### New
- **Monsters wear their character's colours.** With Keep colours on, a character given a monster class used to show the monster's stock colours. Now the monster's body takes a bright version of the character's own signature colour (mostly their hair): a Mogall's eyeball, a skeleton's bones, a spider's legs, a dog's fur, a Gorgon's hair, a gargoyle's stone, a Cyclops's or Tarvos's skin. Each colour keeps its own light and shade, and the eyes, teeth, weapons and outline keep the monster's colours. A weapon a monster borrows another class's sprite for (a Maelduin's Hatchet uses the Great Knight's) keeps that sprite's own colours. Applies to games started in v1.2.0 or later (randomizer version 7); a game started earlier keeps its look.

## v1.1.9 (2026-11-04)

### Fixed
- **Randomized rewards could be a "Dummy".** FE8's item table still holds two FE7 items, the Mine and the Light Rune, under the name "Dummy" (help text "DUMMY"); the original game never hands them out, but the randomizer counted them as healing items, so a village, chest, shop or drop could give one. They are now left out: for games started in v1.1.9 or later (randomizer version 6), and at once for Infinite gold's gem swap. A game started earlier keeps its own picks.

## v1.1.8 (2026-11-04)

### Changed
- **Infinite gold swaps gems for useful items.** With the cheat on, gold is no use, so a Red, Blue, White, Black or Gold Gem from a chest, village, house or gift now becomes a random useful item of about the gem's worth: a weapon (up to rank A), staff, healing item, stat booster, promotion item or accessory. The pick is fixed for each place, so a reload or a second visit gives the same item. Switch the cheat off and gems are gems again. Works in any game, randomized or not.

## v1.1.7 (2026-11-04)

### Changed
- **No more duplicate weapons from a class change.** A randomized character (or enemy) whose new class could not use its weapons got each one replaced on its own, so two could become the same weapon (Gilliam with two Slim Lances). Now each replacement is a weapon the unit does not already have; when the class has nothing else to give at that rank (a Shaman's only starting tome is Flux), the extra copy is dropped instead. The "Better enemy weapons" upgrade no longer makes copies either. Applies to games started in v1.1.7 or later (randomizer version 5); a game started earlier keeps what it had.

## v1.1.6 (2026-11-04)

### New
- **Keep game Options** (Reforged Options > Display, and the in-chapter Reforged screen). With it on, every New Game starts with the in-game Options you last chose (for example Animation 2, Game Speed Fast, Text Speed Max) instead of the original's defaults. Switching it on during a chapter remembers that game's Options at once; after that, leaving the Options screen in any game remembers them again. Off by default; existing saves keep their own Options.

## v1.1.5 (2026-11-04)

### Fixed
- **No more slow enemies on mountains.** A randomized enemy or boss could still be given a class that moves badly where it stands, such as Chapter 2's brigand boss made a Mage on his mountain: v1.1.3 only refused a class that got less than half as far as the original. Now the new class must get three quarters as far, and must cross the ground the unit stands on (mountain, forest, sand) at least as easily as the original class. Bosses now get another try, like enemies, before keeping their own class.

### Changed
- **Monsters get different attacks, not copies.** A character made a monster turned every weapon into the same attack (two Evil Eyes). Now each weapon becomes a different attack of that monster's kind: Mogalls Evil Eye and Crimson Eye, skeletons Rotten and Fetid Claw, spiders Sharp Claw, Venin Claw and Lethal Talon, dogs Fiery Fang and Hellfang, Gorgons Demon Surge and Stone, the Demon King Demon Light and Ravager. Weapons past the kinds there are are dropped. Enemies made monsters get the same.

Both apply to games started in v1.1.5 or later (randomizer version 4); a game started earlier keeps what it had, so nothing changes in the middle of a run.

## v1.1.4 (2026-11-03)

### New
- **Randomizer: "Master Seals only"** (Items section). Every promotion item the game hands out becomes a Master Seal, which promotes any class, so promoting no longer depends on finding the right crest. Covers chests, villages, gifts, shops, enemy drops and units' starting items. For new games.

### Changed
- **Randomized rewards never repeat within a chapter.** With Rewards on, each village, house and chest in a chapter now gives a different item (a run had two of Chapter 2's three villages give an Antitoxin, by chance). The rewards are worked out for the whole chapter at once from its own map events, so the order you visit in, or a reload, changes nothing. Games started before v1.1.4 keep the rewards they had.

## v1.1.3 (2026-11-03)

### Fixed
- **Randomized enemies no longer turn into duds on rough terrain.** An enemy could be given a class that stands where it stands but can barely move there, such as an armor knight or cavalier in the desert sand or an archer on a mountain range, and spend the chapter crawling. Now each new class is checked against how far the original class gets from that spot in 3 turns; one that gets less than half as far is not used, and another is picked (bosses keep their own class). Applies to games started in v1.1.3 or later; a game started earlier keeps the enemies it had, so nothing changes in the middle of a run.

### Changed
- **Unbreakable weapons hides the uses.** With the option on, a weapon or staff it keeps at full shows "--" instead of a uses number (as the original shows its own unbreakable items), in the item menu, stat screen, trade, convoy, shops and preparations. "Your units" hides only the player's; enemies' weapons keep their numbers. The nine legendary weapons, which still wear down, keep theirs, and the Hammerne repair screens show the real numbers.
- **Danger zone on Select now works straight from the map**, as in the DS and later games: with no unit picked, Select shows every enemy's range at once. Before, you had to pick a unit (or an enemy) first. Move the cursor freely; R shows a unit's stat screen and returns to the zone; B or Select hides it. It uses the original game's own danger-zone view, which the game had built in but no button reached. It only changes the picture: the game's state is the same before and after.
- **Plain-English help on the Randomizer screen.** Every setting's description now says what it does in everyday words, on two lines (for example Growths: "Level-up chances. Redistribute: same total, new mix. Delta: nudge each. Full: all new."). The list screens (Randomizer, Game fixes, Display and the in-chapter Reforged screen) now show 5 rows at a time to make room for the bigger help box.

## v1.1.2 (2026-11-03)

### Fixed
- **A squeak when fast-forward starts.** With the fast-forward sound muted (the default), the last moment of normal sound before the speed-up played sped up and rising in pitch. Now it plays out at normal pitch and fades, and the speed changes over silence. Together with v1.1.1's fix for the end, fast-forward is now silent at both ends. ("Sped up" sound still glides, as it should.) The game itself plays exactly the same.

## v1.1.1 (2026-11-03)

### Fixed
- **Leaving the Display or Option sets screen could crash the game** (Windows: "stopped because of an error (exit code 3)", a segmentation fault). v1.1's build left those two screens compiled against an older layout of the shared list-screen code, so going back read past their data. A clean build fixes it, and the build rule now rebuilds every Reforged screen when that shared code changes. Saves and settings are not affected.
- **A whine after fast-forward.** With the fast-forward sound muted (the default), letting go of the key played the first moments of normal sound sped up and sliding down in pitch for about a third of a second. Now the muted sound still queued is dropped and normal sound fades straight back in at normal pitch. (A very short tap, under a quarter of a second, still glides; "Sped up" sound still glides down from the fast pitch, as it should.) The game itself plays exactly the same.

## v1.1 (2026-11-03)

Everything new is in **Reforged Options** (title screen: Extras). With every new option at its default, the game plays as v1.0 and as the GBA game. PLAYING.md explains each one.

### New
- **Randomizer >** (for the next New Game, seeded, in the manner of the Universal FE Randomizer): growths, bases, class movement, Con and affinity; player, boss and enemy classes; enemy and boss buffs, better enemy weapons; weapon might, hit, weight, uses and traits; rewards, enemy drops and shops; characters keep their own battle colours on new classes; **recruitment** (who joins where, with names, portraits and dialogue following); **Nuzlocke** (deaths stay through any reset). Each save keeps the randomization it was started with.
- **Game fixes >** (for the next New Game): six optional fixes to the original's rules (early kill EXP, promoted enemies' growths, AI healing full-HP units, Canto after a cancelled Trade, the Devil Axe at very high Luck, the Chapter 15 staff drop). All off by default.
- **Controls >**: two keys and two gamepad buttons for every GBA button, fast-forward and fullscreen.
- **Display >**: window size, scaling (pixel-perfect, fill, stretch), smoothing, start fullscreen.
- **Option sets >**: save and load three sets of options.
- **During a chapter**: the map menu's **Reforged** entry changes the options that are safe to change mid-run (and the cheats and Display), and shows what the run was started with (randomizer, seed, Nuzlocke, game fixes). Randomizer and game-fix settings can never be changed in the middle of a run.
- **Command line**: `--set KEY=VALUE`, `--scale N`, `--fullscreen`, `--windowed`, `--seed X`, `--randomizer`.

### Fixed
- **A Dancer fighting with a tome froze the battle** when she attacked twice in a row with animations on (reachable with the enemy control glitch or the randomizer). Her battles with a weapon now use the map animation; the results are the same. Part of "Original-game bug fixes" (on by default). Reproduced on the GBA and the PC first.
- Investigated, not reproduced, left as the original: a reported Fimbulvetr freeze.

### Checked
- Saves from late in both stories and the post-game (Chapter 20, both final chapters, the clear save's world map, Tower of Valni floor 8, Lagdou Ruins floor 10) match mGBA's on the Linux and Windows builds, and move between the PC and mGBA both ways (229 save checks).
- The GBA build of the source is still byte for byte the original ROM.

### Keep in mind
- A randomized game's randomization lives in the small `.reforged` file next to its `.sav`. Keep the two together (PLAYING.md, "The side file next to your save").
