# Playing the PC port

## What you need

- **Your own copy of the game:** *Fire Emblem: The Sacred Stones* (USA), a `.gba` file, 16 MiB, SHA-1 `c25b145e37456171ada4b0d440bf88a19f4d509f`. No ROM is included: the game reads its graphics, maps and music from your copy. Hacks, translations and cut-short copies are refused.
- **Windows:** the window build, which is three files from the build's output folder: `fe8pc-sdl.exe`, `SDL3.dll` and `LICENSE-SDL3.txt`. Tested on Windows 11; older versions are untested.
- **Linux:** no ready-made Linux build is released. Build the window build from the source (BUILDING.md, `make -f port/Makefile.sdl`) and run it the same way, with the ROM beside it.
- A keyboard, or any gamepad that SDL3 supports.

## Setting up (Windows)

1. Create a folder of your own, for example `C:\Games\FE8\`. Copy `fe8pc-sdl.exe`, `SDL3.dll` and `LICENSE-SDL3.txt` into it (or unzip the release there). `SDL3.dll` must stay beside the exe.
2. Copy your ROM into the **`roms`** folder that comes in the zip (v1.22.0; make one named `roms` beside the exe if it is not there), or into the exe's own folder as before. Any file name ending in `.gba` works. The optional FE7 and FE6 ROMs (below) can go in `roms` too. If a folder holds several `.gba` files, `baserom.gba` is taken first, and otherwise the original ROM is found by its SHA-1.
   - **Where the game looks, in order:** a ROM dropped onto the exe or named on the command line; `baserom.gba` in the current folder; the original ROM next to the exe; the original ROM in `roms`; then (the window build `fe8pc-sdl.exe`) the ROM you picked earlier (below); then a file with only the FE8 header (a hack or a damaged copy: the game then says what is wrong with it), next to the exe first. The original ROM always wins over a hack or a damaged copy, wherever each is. A ROM that started the game before v1.22.0 is still the one it starts with.
   - **If no original ROM is found** (none at all, or only a hack or damaged copy, which the game names in a message first), the game opens a **"Choose your Fire Emblem: The Sacred Stones ROM"** window (starting in `roms`). Pick your `.gba` file wherever it is: it is checked like any ROM (a wrong game, a hack or a cut-short copy is refused with the reason, and you can pick again). The game remembers where it is (in `%APPDATA%\fe8pc\rom-path.txt`; it never copies the ROM) and uses it at every start while no original ROM is in the places above; if the file is moved or deleted, it asks again. Cancel shows the "no ROM found" message (or, when a hack or damaged copy was found, what is wrong with it).
   - **Put your ROM in the `roms` folder or pick it: your save stays next to the game, where it always was.** For a ROM the game finds in `roms`, or the one you picked in the window (now or at an earlier start), the save is exactly the one it would use if that ROM file sat next to `fe8pc-sdl.exe`: the same name (`FE8.gba` -> `FE8.sav`), in the exe's folder, with the same fallback folder rules (see Saves). So moving your ROM from beside the exe into `roms` keeps your save, and nothing is ever written beside a picked ROM. Everything else is as before: a ROM next to the exe, `baserom.gba`, or a ROM dropped onto the exe or named on the command line keeps its save next to it, wherever it is. No save is ever moved, merged or renamed. (The console test build `fe8pc.exe` keeps a save only when its command line says `--save`.)
3. Double-click `fe8pc-sdl.exe`. You can also drop the ROM file onto the exe, or start it from a terminal:

   `fe8pc-sdl.exe [--save PATH | --no-save] [--console] [ROM]`

The `.debug` file that the build also produces is for developers. You do not need it to play.

## Controls

| GBA button | Keyboard | Gamepad |
|---|---|---|
| A | Z | The button labelled A (on PlayStation pads, cross; on Nintendo pads, the right one) |
| B | X | The button labelled B (on PlayStation pads, circle; on Nintendo pads, the bottom one) |
| L | Q | Left shoulder or left trigger |
| R | E | Right shoulder or right trigger |
| Start | Enter | Start (Menu, Options or Plus, depending on the pad) |
| Select | Tab (Backspace before v1.3.0) | Back (View, Share or Create, or Minus, depending on the pad) |
| D-pad | Arrow keys | The D-pad, or the left stick past halfway |

- Keys are read by their position on the keyboard, so the layout of your keyboard does not change them.
- Opposite directions held together count as neither, as on a real D-pad.
- Gamepads can be plugged in at any time.
- The keyboard controls cannot be changed yet. Rebindable controls are planned (see MODDING.md).
- **Space**, or a click of the right stick on a gamepad, held: fast-forward. It is not a GBA button. See Reforged Options below.

## Window and full screen

- **F11** switches full screen.
- The window starts at 3 times the GBA's size (720 x 480, or smaller if the screen is smaller). You can resize it.
- The picture is scaled by a whole number with sharp pixels.
- To set the starting size, set the environment variable `FE8PC_SCALE` to a value from 1 to 10.
- Closing the window quits the game and writes the save.

## Saves

- **Where:** next to the ROM. For `FE8.gba`, the save is `FE8.sav`. (v1.22.0, `fe8pc-sdl.exe`: for a ROM in the `roms` folder or picked in the window, next to the exe, as if the ROM were there; see "Put your ROM in the `roms` folder or pick it" above.)
- **Format:** the raw 32 KB SRAM image that mGBA uses, with nothing added. A save from mGBA loads in the port, and a save from the port loads in mGBA, suspended chapters included: copy the file, nothing to convert. Both look for the ROM's name with `.sav`, so rename the `.sav` if your ROM is named differently.
- **Fallback folder:** if the ROM's folder cannot be written (for example a read-only folder or a CD), the save goes to `%APPDATA%\fe8pc\` on Windows and to `$XDG_DATA_HOME/fe8pc` on Linux. The window title says so. When `XDG_DATA_HOME` is not set, the Linux folder is `~/.local/share/fe8pc/`.
- **Lock file:** an empty `FE8.sav.lock` stays beside the save.
- **Checks shown in the window title:**
  - a save exists in both places (the port says which one it is using and never picks one silently);
  - the `.sav` is read-only (it is loaded but never written);
  - a second copy of the game is running on the same save (that copy runs without saving);
  - another program changed the save while the game runs (that copy stops saving).
- `--save PATH` uses another file. `--no-save` keeps no save.
- **Reforged Options side file:** a file such as `FE8.sav.reforged` can appear beside the save. See "The side file next to your save" below.

## Reforged Options

Reforged Options is a PC-only screen of extra options. The name and the idea come from the FE8 Reforged project.

- **Where:** on the title menu, choose **Extras**. The entry is always in the Extras list, after Sound Room and Support and before Map, so Extras is always on the title menu.
- **The entry reads "Reforged Options",** in the same lettering as the other Extras entries. The PC version puts that label together from the original game's own menu lettering while the Extras list is on screen; the GBA game has no such entry.
- **During a chapter:** the map menu (A on an empty square or on a unit that has moved) has a **Reforged** entry after Options. It changes the options that are safe to change in the middle of a run (the rows below, the cheats and the Display options), and under **This run** shows what the game was started with (randomizer on or off, its seed, Nuzlocke, each game fix), which cannot be changed until the next New Game. It is not shown in the tutorial. The Randomizer, Game fixes, Controls and Option sets screens are on the title screen only.
- **Everything is OFF at first, except fast-forward and the original-game bug fixes.** With every option off, the game plays exactly like the GBA game. Fast-forward is on, but it does nothing until you hold its key. The bug fixes are on: the Riev fix acts only where the original game would freeze; the Pierce fix also makes every Pierce hit with battle animations on wait like Sure Shot (the results are the same). PATCH-NOTES.md lists both.
- **Keys on this screen:** Up and Down pick a row. Left and Right, or A, change it. B goes back. The screen shows 6 rows at a time. There is no scroll arrow: press Down past the 6th row to reach the last five rows. Up on the top row jumps to the last row, and Down on the last row jumps back to the top.
- A change works at once. When you leave the screen with B, your choices are written to the settings file (below). If you close the game without pressing B, the change is not written, and the next start uses the old values.

| Row | Choices (the default in bold) | What it does |
|---|---|---|
| Danger zone on Select | On / **Off** | Select shows where the enemies can attack. |
| Fast-forward | **On** / Off | Hold Space (or click the right stick) to go faster. |
| Unbreakable weapons | **Off** / Yours / All | Weapons and staves do not wear out. |
| Casual mode | On / **Off** | Fallen units come back at the next chapter. |
| Fast-forward speed | 2x / 3x / **4x** / 8x | How fast, while the key is held. |
| Fast-forward sound | **Mute** / Sped up | The sound while fast-forwarding. |
| Growth rates on Select | On / **Off** | Select on a unit's stat screen shows growth rates. |
| Original-game bug fixes | **On** / Off | Fixes three freezes of the original game (below). |
| HP bars & warnings | **Off** / Bars / Warnings / Both | HP bars on hurt units, "!" on dangerous enemies, or both. |
| Cheats > | | Opens the Cheats screen. |
| Randomizer > | | Opens the Randomizer screen (for the next New Game). |
| Game fixes > | | Opens the Game fixes screen: optional fixes to the original game's rules (for the next New Game). |
| Controls > | | Opens the Controls screen: choose your keys and gamepad buttons. |
| Display > | | Opens the Display screen: window size, scaling, smoothing, fullscreen. |
| Option sets > | | Opens the Option sets screen: save your options as a set, or load one. |

### Danger zone on Select

- **On the map** (no unit picked), **Select** (Tab, or Back on a gamepad) shows every enemy's range in red, as in the DS and later games. Move the cursor around freely. R shows the stat screen of the unit under the cursor and comes back to the zone. B or Select hides it. A switches to the enemies' staff range and back when some of them carry staves, and otherwise hides it.
- It also works after you pick a unit, while its range is shown (for your own unit, that is while you choose where it moves), and when you choose a target (attack, staff and the other target screens). Select shows the zone and Select again hides it.
- There, A, B, L or R hide the zone first, then do their normal job with the same press.
- While the zone shows, the path arrow is hidden, but it still follows the cursor. A then moves the unit to the cursor's square, as usual.
- The zone is worked out with your picked unit on the square it stands on now, not where you plan to move it. It always shows the enemies' weapon range, not their staff range.
- As in the original game's own danger zone, enemies hidden by fog or under a roof are left out.
- A Select pressed in the first two frames after you pick a unit is ignored. The original game ignores every key there. Press it again.
- It only changes the picture. It never changes the game.
- When it is Off, Select does nothing on the map or these screens, as in the original game.

### Fast-forward

- Hold **Space** on the keyboard, or click the **right stick** in on a gamepad. Let go and the game is back to normal speed at once.
- It works on every screen: the map, battles, scenes and menus.
- Speed: 2x, 3x, 4x (the default) or 8x.
- Sound: **Mute** (the default), or **Sped up** (the sound plays faster and higher). **At 8x the sound is always muted,** even with Sped up.
- With Sped up, the sound takes a short moment to change speed after you press or let go.
- The game itself plays exactly the same, only faster. Only every 2nd, 3rd, 4th or 8th picture is shown.
- On a slow PC, 8x may not reach its full speed.
- When Fast-forward is Off, Space and the stick click do nothing.

### Unbreakable weapons

- **Off:** weapons wear out as in the original game.
- **Yours:** your units' weapons and staves never wear out. Enemy units and other armies (green units) use theirs up as usual.
- **All:** everyone's weapons and staves never wear out, enemies included.
- Covered: ordinary weapons, every staff (Hammerne too) and Myrrh's Dragonstone.
- **Not covered: the nine legendary weapons.** Sieglinde, Siegmund, Audhulma, Vidofnir, Nidhogg, Garm, Excalibur, Ivaldi and Gleipnir lose a use each time, as in the original game. Hammerne can still repair them.
- **Items that get used up still get used up:** Vulneraries, Elixirs, keys, promotion items and the like.
- A weapon that is already part-used goes back to full the next time it is used.
- **No uses numbers.** A weapon or staff the option keeps at full shows "--" where its uses were (item menu, stat screen, trade, convoy, shops, preparations), as the original shows its own unbreakable items. With **Yours**, enemy and green units' weapons keep their numbers. The legendary weapons keep theirs, and the Hammerne repair screens always show the real numbers.
- The battle forecast shows the hits a never-wearing weapon really makes. For example, "x2" even when it has 1 use left.
- A save made with this option on is marked (see the side file below).

### Casual mode

- When one of your units falls in battle (your turn or the enemy's), or to a trap, gas or poison, it **retreats** instead of dying.
- For the rest of that chapter it counts as dead, exactly as in the original game. It leaves the map, and the chapter's events and records see it as fallen. If no unit is left, the game is over, as usual.
- When the chapter ends, the unit is back for the next one, with its level, stats, items and **supports**. Like every unit after a chapter, it starts with full HP. (Skirmishes and the Tower and Ruins should work the same way, but this has not been tested.)
- **These still die for real:** Eirika and Ephraim; the chapter's protected character; any unit whose death ends the game (the game is over, as usual); deaths in the arena; deaths that are part of the story; summoned phantoms; enemy and green units.
- The unit's death quote and the sad music still play when it falls.
- The scene at the end of the chapter can still talk about the unit as fallen. It is back in the next chapter anyway.
- The fall still counts as a death in that chapter's rank. The death count at the end of the game counts only units that are dead then, so a unit that came back is not counted there.
- **A unit that falls in the final chapter stays dead in the ending.** No new chapter starts before the ending, so it has no chance to come back first. It stays on the list in the save made after the ending, so it should come back in the post-game after the next map you finish. This has not been tested.
- If you turn Casual mode off before the chapter ends (for example: suspend, change it on the title screen, then resume), a retreat that already happened is not undone. That unit still comes back.
- The list of retreated units is kept in the side file next to your save. Keep that file with the `.sav` (see below).
- A save made with this option on is marked.

### Growth rates on Select

- On a unit's stat screen (the first page, with the stats), press **Select** (Tab, or Back on a gamepad). The stat numbers turn into the unit's growth rates: the chance, in %, that each stat rises at a level-up. Select again swaps back.
- HP's growth shows where the HP numbers were. Move and Con never grow, so they keep their numbers.
- The growth rates stay on screen while you turn pages or move to another unit with Up and Down. They are off again each time the stat screen opens. On the second and third pages Select does nothing.
- A green **+5** after a number means the unit used Metis' Tome. The Growths cheat's +25% or +50% shows there too (for example +30 with both).
- Your units' stat names are coloured by how good the growth is: white under 30%, grey 30-39%, blue 40-49%, gold 50-69%, green 70% and up. Enemy and other units show their numbers with the usual labels.
- It only changes the picture. It never changes the game.
- When it is Off, Select does nothing on the stat screen, as in the original game.

### Original-game bug fixes

- On by default. Three freezes of the original game are fixed (PATCH-NOTES.md):
  - **Riev, Chapter 20:** if Riev dies on turn 1, the original freezes when the enemy phase starts. With the fix, his scene skips pointing the camera and cursor at him (his line still shows) and the game goes on.
  - **Pierce:** with battle animations on, a hard Pierce hit that does not kill, followed by another hit, can freeze the battle. With the fix, Pierce waits for its effect and the HP bar before the next hit, as Sure Shot does. Pierce battles with animations on run a little longer; the results are the same.
  - Tested with your Wyvern Knight attacking; an enemy Wyvern Knight's Pierce goes through the same fixed code (the path the original's Sure Shot already takes for enemy Snipers) but was not tested.
  - **A Dancer with a tome:** if Tethys (or anyone in the Dancer class) fights with a tome (possible with the enemy control glitch or the randomizer), her dance animation froze the battle when she attacked twice in a row. With the fix, her battles with a weapon use the map animation. Her dance itself is unchanged.
- Off: all three freeze as on the GBA.
- It never marks a save.
- A reported Fimbulvetr freeze was investigated and could not be made to happen, so it has no fix (PATCH-NOTES.md).

### HP bars & warnings

Four choices: **Off**, **Bars** (only the HP bars), **Warnings** (only the "!" marks) or **Both**.

- **Bars:** every hurt unit on the map (yours, green and enemy) shows a small HP bar at the bottom of its square. A unit at full HP shows none. The bar has 11 steps. Its colour follows its length: green above 8/11 of the unit's maximum HP (about 73%), yellow above 5/11 (about 45%), orange above 3/11 (about 27%), red below. These four colours, and the dark blue of the bar's empty part, are the game's own map colours. The maximum is the one the game uses, with the equipped weapon's HP bonus.
- **Warnings:** while you choose where one of your units moves, enemies that threaten it get a small "!":
  - **white** when it is effective against your unit: the enemy's class is strong against your unit's class (a Bishop against monsters), or the enemy carries a weapon it can equip that is effective against your unit (a bow against a flier, for example);
  - **orange** when the enemy carries a weapon it can equip with a high critical rate of its own (more than 24: in the original game, the Killer weapons and the Shamshir).
  - An enemy shows one "!" at most. The class counts first, then the enemy's items in order; the first one that counts picks the colour.
  - The marks look only at the enemy's class and items, not at its HP, its range or where it stands. A weapon counts if the enemy could equip it (rank and locks as in the game; a silenced enemy cannot equip tomes). A Delphi Shield in your unit's items stops bows' effectiveness, as in battle. An enemy on a ballista is judged by its own items, not the ballista.
- Windows over the map (menus, the battle forecast, the unit and terrain boxes) cover the bars and marks as they cover the units. Battle scenes show none.
- It only draws on the screen. It never changes the game.

### Cheats

Choose **Cheats >** in Reforged Options. There is no code to unlock it. Every cheat starts off. The keys are the same as on the main screen; B goes back.

| Row | Choices (the default in bold) | What it does |
|---|---|---|
| Infinite gold | On / **Off** | Your gold stays at 999,999, and gems (only there to be sold) become useful items. |
| Growths | **Off** / Fixed / +25% / +50% | Changes how stats rise at a level-up. |
| EXP gained | **x1** / x2 / x3 | More experience. |

- **Infinite gold:** after you spend, the gold fills back up to 999,999. Ephraim's Chapter 5x, where the original game shows 0 gold, is left alone. While it is on, a Red, Blue, White, Black or Gold Gem from a chest, village, house or gift is swapped for a useful item of about its worth (a weapon up to rank A, a staff, a healing item, a stat booster, a promotion item or an accessory), the same one each time for the same place. Gems an enemy drops are not changed.
- The gold is really added to your save. If you turn the cheat off later, you keep the gold you have at that moment (up to 999,999).
- The game's funds rank counts this gold too.
- **Growths, Fixed:** each stat rises on a steady schedule that matches its growth rate on average, so there are no bad-luck streaks. **A Fixed level-up can still give no gains at all.**
- **Growths, +25% and +50%:** that much is added to every growth rate.
- Stat caps still apply.
- The Growths cheat only acts on your units' level-ups from a battle, a staff, a dance or another action. Units who join above their base level, enemy units, and Chapter 8's catch-up level-ups for the lords use the normal rules.
- **EXP gained:** x2 or x3 the experience from battles, staves, dancing and other actions. One gain is still at most 100, so **a unit still gains at most one level per battle.** A missed staff, and a battle where you did no damage, still give 1 EXP.
- A save made with any cheat on is marked.

### Randomizer

Choose **Randomizer >** in Reforged Options. It randomizes a new game from a seed, in the manner of the Universal FE Randomizer (UFER): the same seed with the same settings gives the same game. The randomizer starts off, with each row at its default (below).

- **It applies only to the next New Game.** The randomizing happens when you start a new game. A game you have already started, or a save you load, is not changed by this screen: each save keeps the randomizer settings and the seed it was started with (see the side file below). A game started with Randomizer Off plays as the original.
- **Keys:** Up and Down pick a row; the section names (Stats, Classes, Recruitment, Enemies, Items, Challenge) are skipped. On a row with choices, Left and Right, or A, change it. On a row with a number, Left and Right change it by 5 (or by 1 on the small ones); hold them to go faster. B goes back. The screen shows 6 rows at a time and scrolls.
- With Randomizer Off, the rows below Seed are shown in grey. You can still set them.
- **Seed:** 8 hex digits (0-9, A-F), or **Random**.
  - **A** edits it: Left and Right pick a digit (shown in gold), Up and Down change it, A or B end the edit.
  - **Select** picks a new seed from the PC's clock.
  - **L** or **R** set it back to Random.
  - With Random, a seed is picked when you start the new game, and the screen shows it from then on (it is written to the settings file).
  - To play the same randomized game as someone else, use their seed and their settings.

| Row | Choices (the default in bold) | What it does |
|---|---|---|
| Randomizer | On / **Off** | Randomize the next New Game. |
| Seed | **Random** / 8 hex digits | The seed (above). |
| **Stats** | | |
| Growths | **Off** / Redistribute / Delta / Full | Your characters' growth rates. Redistribute: the total moves by up to Growth variance and is shared out again. Delta: each growth moves by up to Growth variance. Full: each growth is new, between Growth min and Growth max. |
| Growth variance | 0-100%, **30%** | How far the growths move: for Redistribute, how far their total moves; for Delta, how far each one moves. |
| Growth min | 0-100%, **5%** | No randomized growth goes below this. |
| Growth max | 0-255%, **80%** | No randomized growth goes above this. |
| Adjust HP growths | On / **Off** | HP gets a bigger share of the growths. |
| Bases | **Off** / Redistribute / Delta | Your characters' personal base stats. Redistribute: the total moves by up to Base variance and is shared out again (Luck is rolled anew, 0-6). Delta: each one moves a little. |
| Base variance | 1-10, **5** | How far the bases move: for Redistribute, how far their total moves; for Delta, how far each one moves. |
| Class movement | On / **Off** | Each class gets a movement of 4 to 8; a promoted class never has less than the class it comes from. |
| Constitution | On / **Off** | Each character's Con moves a little (never below 3). |
| Affinity | On / **Off** | Each character gets a random support affinity. |
| **Classes** | | |
| Player classes | On / **Off** | Your characters get new classes, of the same tier (unpromoted or promoted). Weapons the new class cannot use become ones it can, each one different (never two Slim Lances); where the class has nothing else to give at that rank, the extra copy goes. |
| Include lords | On / **Off** | Eirika and Ephraim too. Eirika keeps swords and Ephraim lances. |
| Include thieves | On / **Off** | Colm and Rennac too, and they join the Recruitment shuffle. Off, no character is given a thief class (Thief, Rogue, Assassin). |
| Include dancer | On / **Off** | Tethys too (her new class is never Dancer, so the army has no dancer), and she joins the Recruitment shuffle. Myrrh never changes. |
| Monster classes | On / **Off** | Your characters may get monster classes. A character made a monster gets that monster's own attacks in place of its weapons, each one different (a Mogall: Evil Eye, then Crimson Eye), never two of the same; past the kinds there are, the extra weapons go. |
| Assign evenly | On / **Off** | Each class is used once before any class is used again. |
| Match gender | **On** / Off | Where a class has a male and a female version, characters get their own gender's. Classes with only one version (Pegasus Knight, Fighter and others) are open to anyone. |
| Class growths | **Keep** / Transfer | Transfer: a character's growths follow the new class (personal growth minus the old class's, plus the new class's). |
| Class bases | **Final** / Personal | Final: the starting stats stay about the same. Personal: the personal bases are kept and the new class's bases are added. |
| Random promotions | On / **Off** | Every class that promotes gets two random promoted classes to choose from at promotion (a Knight might become a Hero or a Wyvern Lord), different in every game. Magic users promote into magic classes (Sage, Mage Knight, Bishop, Druid, Valkyrie, and with monsters mixed the Arch Mogall and the Gorgon; never the Summoner, whose Summon only works for its own characters) and weapon users into weapon classes. The lords and the trainees keep their own; the Draco Zombie and the Demon King are never a choice. With Match gender on, a choice that has a male and a female version is the unit's own (a Pegasus Knight offered Paladin becomes a female Paladin). The two choices are never the male and female versions of one class. **Three choices (v1.21.0):** in games started in v1.21.0 or later the screen also shows a third: rarely a foreign class (see "FE7/FE6 classes"), otherwise a third random class under the same rules, never one of the two or the other-sex version of one. A promotion into a monster class (which has no promotion gains of its own) gives the gains of the unit's usual promotion. A unit that promotes into a class that can use none of its weapons gets a basic weapon (or staff) of its new class; its other items stay (with all five slots full, the first item it can no longer use goes to the convoy to make room). |
| Monster promos | **Separate** / Mixed | With Random promotions on. Separate: monster classes (yours with Monster classes on) promote into other monster classes, and the others into the others. Mixed: either can become either, so a Fighter may promote into a Cyclops and a Mogall into a Sage. |
| FE7/FE6 classes | On / **Off** | Called "FE7 classes" before v1.21.0. Only with your own FE7 and/or FE6 ROM loaded (see "FE7 classes" and "FE6 classes" below; with neither the row is grey, and a New Game started with it On plays with it Off). **v1.21.0:** every class those ROMs give can come up, rarely and only as an extra (each roll is made after the usual ones, so it replaces nobody else's pick): **promotion choices** (the third choice: Blade Lord or Master Lord for sword users, Armor Lord for axe and lance users on foot, the Nomad Trooper for bow users, Archsage or Dark Druid for magic users, see "Every promotion has three choices" below); **your units' classes** with Player classes On (about 1 time in 4 when the new class has a foreign counterpart: an Archer may be an FE7 Nomad, a Thief an FE6 female Thief (a woman, with Match gender), a Myrmidon an FE6 Lion Lord (a man, with Match gender), a Swordmaster a Blade Lord or Master Lord (with both ROMs in the game a woman a Blade Lord, a man a Master Lord; with one, its lord), a General an Armor Lord, a Ranger a Trooper, a Sage an Archsage, a Druid a Dark Druid; Tethys as a Dancer may be FE7's Bard and Myrrh FE6's Divine Dragon, 1 time in 8; the twins never; always a class that a game without the ROM loads as that same unit, so with Match gender Off a woman in the male Archer class may be the male Nomad, while a woman in the male General, Sage, Druid, Myrmidon or Swordmaster class (or a man in the female one) keeps her class); **late enemies** with Boss / Enemy classes, from Chapter 15 on: a boss in a promoted sword class may be FE6's **King** and a General boss an Armor Lord (1 time in 6), an Archer an FE7 Nomad and a Sniper or Ranger a Trooper (1 time in 8), an enemy thief a female Thief (1 time in 4; with Match gender only a woman). Only the ROMs loaded at New Game count for that game's randomizer picks. Match gender keeps only the female Thief and the Lion Lord to one sex; the other foreign classes have one version each and are open to anyone, as in the Match gender row. Before v1.21.0 (games of v1.17.0-v1.20.0, kept as they were): Brings in FE7's **Archsage** and **Dark Druid**, rarely, as extras (v1.17.0): they are no extra tier and replace nobody. With Random promotions, about 1 in 8 human magic classes (Mage, Shaman, Monk, Priest, Cleric, Troubadour, either sex; never a monster, a trainee or a lord) gets a third choice at promotion beside its two random ones: Archsage, or Dark Druid for the dark-magic classes. With Boss classes / Enemy classes, from Chapter 15 on (both routes, to the end), a boss whose new class is a Sage, Bishop or Druid becomes an Archsage or Dark Druid 1 time in 4, and such a regular enemy 1 time in 12, in FE7's enemy colours. They have FE7's power as it is. Everything else a seed gives is the same as with the row Off. |
| Keep colours | **On** / Off | A character in a new class keeps their own battle colours (hair, outfit, team colours) instead of the class's generic ones. A character made a monster gets the monster's body (a Mogall's eyeball, a skeleton's bones, a dog's fur) in a bright version of their own signature colour, mostly their hair; its eyes, teeth and weapons keep the monster's colours (games started in v1.2.0 or later). **Games started in v1.22.0 or later: every class.** The FE7/FE6 classes (with their ROMs), the classes that had no colour data (Soldier, the trainees, the female classes without a character of their own), Eirika and Ephraim in other classes (their blue hair, on the class's hair only; their own lord classes keep their look), and bosses put in another class get the character's colours too; the dragons (FE8's manakete dragon class, FE6's Divine Dragon) take the signature colour on their body. Display only. Games started earlier keep the colours they started with. |
| **Recruitment** | | |
| Recruitment | On / **Off** | Shuffles who joins where. A character takes another one's place: they join at that point in the story, and the dialogue, names and portraits follow (for example, with Garcia in Seth's place the prologue says "Don't be foolish, Garcia."). Eirika, Ephraim and Myrrh never move; Colm and Rennac only with Include thieves, Tethys only with Include dancer. Places whose story fights need it get a fitting character: Seth and Gilliam fight up close, Artur and Neimi from range, Garcia must attack; Vanessa, Cormag, Syrene and Tana's places go to fliers when one is left. |
| Recruit class | **Own** / Slot | Own: the character keeps their own class (promoted or unpromoted to match the place; a trainee starts in their first real class). Slot: they take the class of the person they replace. |
| Recruit stats | **Autolevel** / Slot | Autolevel: their own stats, levelled up or down with their own growths to the level of the place they take. Slot: the stats of the person they replace. |
| Cross-gender | On / **Off** | Off: men take men's places and women women's. On: anyone anywhere (dialogue still says he or she as written). |
| **Enemies** | | |
| Boss classes | On / **Off** | Bosses get new classes. In a chapter where the new class could not stand or walk where the boss must, or would be stuck or slowed to a crawl there (below), another is picked (up to 8 tries), or the boss keeps its own class for that chapter. |
| Enemy classes | On / **Off** | Generic enemies get new classes. A class that could not stand where the enemy stands, or would be stuck or slowed to a crawl there (below), is not used: another is picked (up to 8 tries), or the enemy keeps its own class. |
| Enemy growths | **Off** / Flat / Scaling | Raises the class growths enemies level with: Flat adds Enemy buff, Scaling adds Enemy buff percent of each growth. |
| Enemy buff | 0-100%, **10%** | How much Enemy growths adds. |
| Boss stats | **Off** / Linear / Ease | Bosses get extra stats, more the later they appear in the story. Ease: slow at first and at the end. |
| Boss max boost | 1-20, **5** | The most a boss's stat can gain. |
| Better enemy weapons | **Off** / 5-100% | The chance that an enemy gets a better weapon. |
| Tower & Ruins | On / **Off** | The monsters of the Tower of Valni, Lagdou Ruins and the world map's skirmishes become other monsters of the same rank: a floor of Revenants becomes a mix of Revenants, Bonewalkers, Baels, Mauthedoogs, Mogalls, Gargoyles and Tarvos; later floors' promoted monsters mix Entombed, Wights, Elder Baels, Gwyllgi, Arch Mogalls, Deathgoyles, Maelduins, Cyclopes and Gorgons. No monster becomes a Draco Zombie or the Demon King (the deepest floors' own Draco Zombies stay). A monster that could not walk the map as well as the original is rerolled (up to 8 tries). Each monster gets its new kind's own attacks; the item it drops stays the same. |
| **Items** | | |
| Weapon might | On / **Off** | Might moves by up to 3 (kept between 0 and 23). |
| Weapon hit | On / **Off** | Hit moves by up to 20 (kept between 55 and 100). |
| Weapon weight | On / **Off** | Weight moves by up to 5 (kept between 2 and 20). |
| Weapon uses | On / **Off** | Uses move by up to 20 (kept between 15 and 60; the long-range tomes, which start with few uses, can go as low as 1). |
| Weapon traits | **Off** / 5-100% | The chance that a weapon gets an extra trait. |
| Rewards | On / **Off** | Other items in chests, villages, houses and gifts. Within a chapter no village, house or chest gives the same item as another (games started in v1.1.4 or later). |
| Enemy drops | **Off** / 5-100% | The chance that an enemy drops an item. |
| Shops | On / **Off** | Shops and armories sell other items. |
| Master Seals only | On / **Off** | Every promotion item the game hands out is a Master Seal, which promotes any class: Hero Crests, Knight Crests, Orion's Bolts, Elysian Whips, Guiding Rings and Ocean Seals in chests, villages, gifts, shops, enemies' drops and units' starting items. The lords still promote in the story as usual. Works with the other rows off too (turn Randomizer on and leave the rest off). |
| **Music** | | |
| Random music | On / **Off** | Each chapter's map themes (your phase, the enemy's, the other armies') are random picks from all the game's map themes, and the battle themes (attack, defense, the boss themes) random picks from the battle songs. A chapter always sounds the same within a game. The "few enemies left" theme, the arena, the dance and the promotion songs stay as they are. |
| **Challenge** | | |
| Nuzlocke | On / **Off** | Deaths are final. Casual mode is ignored for this game. A unit that dies (in battle, the arena, or to a trap or poison) is written down at once in the side file next to the save; whenever you load this game again (Resume, Restart Chapter or an older save) that unit is dead again, so a reset never brings it back. A death that ends the game (a lord, or the chapter's must-survive character) is not written down: you go back as usual. To play a Nuzlocke without randomizing, turn Randomizer on and leave everything else off. |

- The default numbers are the Universal FE Randomizer's own defaults.
- **No stuck enemies.** For each enemy (and boss) the randomizer measures how far the new class could walk from where the enemy stands in 3 turns, with its own terrain costs (mountains, peaks, sand, forest, water), against the original class with the same movement. A new class that gets less than half as far, such as an armor knight in the desert or an archer on a mountain range, is not used. Since v1.1.5 the bar is three quarters as far, and the new class must also cross the ground the enemy stands on (a mountain, forest, sand) at least as easily as the original class, so a brigand on a mountain never becomes a mage or a cavalier there. Games started earlier keep the enemies they had (each save keeps its randomizer version).
- The randomizer never uses the game's own random numbers, so battles, level-ups and the rest still work exactly as the game does them.
- **Every promotion has three choices (v1.21.0, the owner's rule: "every promotion moment should have 3 choices").** In every randomized game started in v1.21.0 or later (any settings, with or without the FE7/FE6 ROMs) each promotion screen (Master Seal, crests, a trainee's level 10, the braces, Chapter 16's "yes") offers three different classes. First what the game offered before (with Random promotions its two random classes; the trainee's third once you have cleared both routes; a Myrmidon's Blade Lord and a Knight's Armor Lord with the FE7 ROM; Eirika's Blade Lord and Ephraim's Armor Lord as second choice with the FE7 ROM), then:
  - With **Random promotions On**: about 1 row in 4 gets a foreign class that fits the class (above), when the FE7/FE6 row was On with that ROM at New Game; otherwise a third random class under the same rules as the two.
  - With **Random promotions Off** (and for the rows it never changes: the twins', the trainees', the foreign classes'): the class's natural foreign promotion when its ROM is in the game (a Mercenary or Myrmidon a sword lord: FE7's Blade Lord for a woman, FE6's Master Lord for a man, or the one whose ROM the game includes when it has only one; a Fighter, Brigand or Pirate the Armor Lord; an Archer the Trooper; Mages, Monks, Priests, Clerics and Troubadours the Archsage; Shamans the Dark Druid; Eirika FE6's Master Lord; an FE6 Lion Lord FE7's Blade Lord; an FE7 Nomad the Trooper), else an FE8 class of the same kind of weapons and the unit's sex (the closest: a Fighter's is a Berserker, a Thief's a Swordmaster, Eirika's an Assassin or Swordmaster, Ephraim's a lance class), the same in every game with that seed.
  - A class with only one promotion (the twins' Lord without the FE7 ROM, the monsters, the FE6 Lion Lord) gets two more, and now opens the screen instead of promoting at once. Chapter 16's "yes" opens it for each twin still unpromoted (B does nothing there); a twin who took any promoted class with a brace is not promoted again. The twins keep their Rapier/Sieglinde and Reginleif/Siegmund in any class.
  - A unit that promotes into an added class that uses none of its weapons gets a basic weapon of the new class, as with Random promotions. B on the Master Seal screen still goes back without using the item.
- **Seeds and versions (v1.21.0, v1.22.0):** whether a game includes the foreign classes, and which ROMs, is kept as its randomizer version, decided at New Game: games started in v1.22.0 or later are 15 without them (or the row Off), 16 with FE7, 17 with FE6, 18 with both; games started in v1.21.0 keep 11-14 in the same order. 15-18 make exactly the same choices as 11-14; only Keep colours differs (every class, see the table above). Everything else a seed gives is the same as in a v1.17.0-v1.20.0 game of the same seed (version 10 with the FE7 ROM, 9 without), except where a foreign roll fired, the added third choices, and the twins' prf weapons. Games started earlier keep their version: two choices, and v1.17.0's Archsage and Dark Druid only. A ROM removed later makes that game's foreign units play as their stand-ins and its foreign promotion choices FE8 ones (never the other ROM's class); adding one later never brings the randomizer's foreign picks into that game. The choices the game offered before (a Myrmidon's Blade Lord, a Knight's Armor Lord, the twins' FE7 lords, an FE7 Nomad's Trooper) are the FE7/FE6 class features themselves and, as in an unrandomized game, follow the ROMs loaded now. v1.17.0-v1.20.0 can load a v1.21.0 game's saves: they keep the game's randomizer line and load it with the original tables, foreign units as stand-ins (or as their class when that version knows it and the ROM is there). In the same way v1.21.0 and older can load a v1.22.0 game's saves, but with the original tables (version 15-18 is newer than they know, even though it makes version 11-14's choices): play a v1.22.0 randomized game in v1.22.0 or later.
- **FE7 classes and seeds (v1.17.0):** whether a game includes them is decided at New Game and kept with the game, like the other rows: it is On only if the row is On and the FE7 ROM is loaded at that moment. It is kept as the game's randomizer version (10 with FE7 classes, 9 without), so an older version of the port that rewrites the side file cannot lose it. Adding the ROM later never brings them into that game; removing it later makes the FE7 units play as their stand-ins (an Archsage as a Sage, a Dark Druid as a Druid; a boss that would be one keeps the class the seed gave it otherwise). The FE7 picks are rolled on their own, after the usual ones, so a seed gives the same classes, promotions and items as in v1.16 wherever no FE7 class came up. Games started before v1.17.0 never get them.
- A randomized game's `.sav` is still a normal save. But **the randomizing itself is kept only in the side file** next to the save (below). If the side file is lost, or the slot is played in mGBA (the port then drops that slot's notes), the port loads that slot with the original game's tables: your units keep the classes and stats saved with them, but growths, weapons, new units, chests and shops from then on are the original game's.

### Game fixes

Choose **Game fixes >** in Reforged Options. Each row changes one of the original game's rules where it gives a result that looks unintended. They do not fix freezes (that is Original-game bug fixes, above). **Every one is Off at first**; off, the game does exactly what the original does.

- **Like the Randomizer, they are for the next New Game.** Starting a new game fixes them for that game, and its saves remember them. Changing this screen later never switches a fix on or off in a game you have already started, so a run can't be changed halfway. A save from before this screen existed plays with every fix off.
- A save made with any of them on is marked (see the side file below).

| Row | Choices (the default in bold) | What it does |
|---|---|---|
| Early kill EXP | On / **Off** | Prologue to Chapter 8: in the original, killing an enemy slightly stronger than your unit gives less EXP than killing an equal one (the early-game EXP bonus stops suddenly). On: a stronger enemy never gives less than an equal one. A little more EXP early on. |
| Promoted enemies | On / **Off** | Promoted generic enemies get 9 extra levels (19 on Hard) that the game passes as levels in their unpromoted class, but the original rolls them with the promoted class's lower growths. On: rolled with the unpromoted class's growths, as the code seems to intend. **Makes those enemies a little stronger** (also arena opponents, and promoted bosses on Easy and Normal). |
| AI healing | On / **Off** | In the original, enemy and green healers can "heal" a unit at full HP (every unit with a usable tome counts as wanting healing). On: they skip units at full HP and do something else. |
| Canto after Trade | On / **Off** | In the original, a mounted unit that opens Trade or Supply and backs out without moving anything, after the menu showed an Attack or Staff range, loses Canto for that turn (and the "squares moved" record gains 255). On: Canto is kept. |
| Devil at high Luck | On / **Off** | The Devil Axe backfires with a chance of 31 minus Luck. In the original, Luck 32 or more (only reachable with the randomizer's weapon traits) makes it backfire every time. On: 0% from Luck 31 up. |
| Ch15 staff drop | On / **Off** | Chapter 15 (both routes): the Troubadour who drops an item carries Sleep (or Berserk) and Mend, and a unit drops its last item, so you could only ever win the Mend. On: the two swap when the chapter loads, so she drops the Sleep or Berserk staff. Turn it on before the chapter starts. |
| Pair Up | On / **Off** | Fire Emblem Fates style pairing, built on Rescue (below). Not a fix: a new rule, kept here because it is fixed for the whole run. |
| Skills | **Off** / Class / Personal | Your units get skills from their class (Class); with Personal each character also gets one of their own (below). Not a fix: a new rule, fixed for the whole run. |
| Enemy skills | **Off** / Promoted / All | Enemy and green units get their class skills too: Promoted gives them only to promoted units and bosses, All to everyone. They never get personal skills. |
| Smarter enemies | On / **Off** | In the original, an enemy attacks whatever it can reach, even for no damage, or into a counter that kills it. On: regular enemies (not bosses, not green units, not berserk) skip an attack they can't really hurt with (under 1 damage expected) when the target hits back, and one that would likely get them killed (50% or more) unless they would likely kill first. The odds follow the battle's real order of strikes and the game's real hit chances (crits not counted). Among the attacks left, they prefer the safer ones. One that backs off and then walks on toward you stops outside the reach of the units it backed off from when it can (roughly their Move plus weapon range); enemies told to hold their ground keep holding it. |
| Myrrh buff | On / **Off** | Myrrh joins weaker than her looks and her Dragonstone (50 uses) runs out. On: her Dragonstone never breaks while she holds it (its uses show as "--" on her screens), and her Speed is +2. Not a fix: a balance change, fixed for the whole run. |
| Late joiners' ranks | On / **Off** | Characters who join later start with better weapon ranks, so they are usable at once: joining in Chapters 9-12 (Tana, Amelia, Gerik, Marisa, Ewan, L'Arachel, Dozla, and on the route where they come early Innes, Cormag, Duessel, Saleh), each weapon rank they have is one letter higher than their usual start; joining in Chapter 13 or later (Rennac, Knoll, Syrene, and on the route where they come late Gerik, Cormag, Duessel, Innes, Saleh), two. At most A (C for trainees); never a new weapon type, never S. Characters who already start at A gain nothing there. With the recruitment shuffle the boost goes with the place, not the character. Fixed for the whole run. |
| Armed dancers | On / **Off** | In the original a Dancer has no weapon rank, so Tethys can only dance and can never fight back. On: a Dancer starts with light magic at rank E, so she can use Lightning (and any other E-rank light tome); with the FE7 ROM, a Bard starts with anima (fire) magic at rank E and can use Fire. Their Attack and Dance commands are both in the unit menu, dancing still refreshes, and a fight gives EXP and weapon experience as for anyone. Their fights with a weapon (attacking or hitting back) always use the map animation, even with Animations on, because they have no attack animation (Tethys's dance animation can freeze a fight in which she strikes twice in a row). They still cannot enter the arena, as in the original: the arena only plays battle animations. Rank E is a start, not a cap: they gain weapon experience as usual. With Late joiners' ranks on too, Tethys's light rank gets her late-joiner boost (E on Eirika's route, D on Ephraim's). Fixed for the whole run. |
| Third promotion | On / **Off** | v1.23.0. In a game without the randomizer, every promotion screen offers three classes: the original's choices first, then a third from a fixed table (the twins get a second too), with FE7 and FE6 classes when you have those ROMs and FE8 classes in their place when you do not (below). Chapter 16's "yes" opens the screen for each twin. Not a fix: a new rule, fixed for the whole run. |

#### Pair Up

With **Pair Up** on, Rescue pairs two of your units instead of just carrying one:

- **Anyone can pair with anyone** of your own army: Rescue, Take and Give need no Aid or Con. Enemy and green units' rescues are the original's.
- **The lead (the unit on the map) keeps its full Skill and Speed** (no more halving) and **gets the partner's boost**: the partner's two best stats (of Str/Mag, Skill, Speed, Luck, Defense, Resistance) give the lead **+2 each, +3 if the partner is promoted, and +1 more per support rank** (C, B, A) between the two. The boost shows in green on the stat screen and counts in the battle forecast. The partner's support bonuses (hit, avoid and so on) count as if it stood next to the lead. The boost also counts in the arena (the gauge does not fill there).
- **Dual Guard:** the pair fills a guard gauge, **+10% for each hit the lead lands and +20% for each hit it takes**. At 100% the partner **blocks the next enemy hit completely** (no damage and no poison or other effect; the battle shows a shield flash), and the gauge empties. The stat screen's Cond line shows it ("Guard 60%", "Guard full") when the lead has no status.
- **Swap** (in the unit menu, before Wait; left out only when the menu already has its maximum of 11 commands): the partner becomes the lead on the same square and carries on the turn (it can attack, use items and so on, but not move). Free and as often as you like; a partner that has already acted this turn, or is asleep or berserk, cannot swap in. After a swap the move is final (B then ends the turn), as after a trade.
- **Supports grow twice as fast** for a pair (the extra does not count toward the chapter's tactics rank). Support conversations still need the two side by side, as in the original.
- Pairs break as in the original: Drop, Give, the lead's death (the partner is dropped next to it) and the end of a chapter.

#### Skills

With **Skills** on, every unit has the skills of its class; with **Personal**, each playable character also has one skill of their own. Skills belong to the class, so a randomized class (a monster too) simply brings that class's skills. Off, the game is exactly the original.

- **Promoting keeps the old skill:** when one of your units promotes, it keeps the skill its old class gave it, and the new class adds its promotion skill. A Fighter who becomes a Swordmaster keeps Wrath and gains Astra. Heroes are the exception: a Hero always has Astra and Sol, so a Mercenary gives up Armsthrift when it becomes one. This applies to your units only; enemies always have their class's pair. The kept skill is stored beside your save (in the `.reforged` file, with each save slot and suspend), so loading, Resume and Rewind keep it; a save from before this rule simply keeps nothing extra. A trainee's first step (Journeyman to Fighter and so on) is not a promotion: the new class's skill replaces Underdog.

- **Seeing them:** press R on the class name in a unit's stat screen for its skills and what they do. On the promotion screen, each choice shows what the unit **gains**, **keeps** and **loses**, so you never have to promote and reload to find out.
- **Enemy skills:** with Enemy skills off, only your side has skills, so the game gets easier after promotion. Promoted gives enemy and green promoted units and bosses their class skills; All gives every one of them their class skills.
- **Chance skills** (Luna, Sol, Astra, Pavise, Aegis, Miracle, Armsthrift, Despoil) use the battle's normal random numbers. The battle forecast and the enemy AI never count on them, as with the original's Great Shield.
- FE8's own skills (Canto, Steal, Sure Strike, Great Shield and so on) work exactly as before; they are just listed with the others.

**Each class's skills** (an unpromoted class has one; a promoted class keeps that one and adds a promotion skill, and your promoted units keep their old class's skill instead of the first one; FE8's own in brackets; both sexes of a class have the same skills):

| Unpromoted: skill | Promoted: skills |
|---|---|
| Lord (Eirika, Ephraim): Charm | Master Lord: Charm + Sol (Eirika's) or Luna (Ephraim's) (+ Canto) |
| Cavalier: Savior (+ Canto) | Paladin: Savior + Aegis; Great Knight: Savior + Pavise (+ Canto) |
| Armor Knight: Wary Fighter | General: Wary Fighter (+ Great Shield) |
| Thief: Pass (+ Steal, Locktouch) | Rogue: Pass + Poison Strike (+ Steal, Locktouch); Assassin: Pass (+ Silencer, Locktouch) |
| Mercenary: Armsthrift | Hero: Astra + Sol (always: a promoted Mercenary gives up Armsthrift) |
| Myrmidon: Vantage | Swordmaster: Vantage + Astra (+ Critical +15) |
| Archer: Patience | Sniper: Patience (+ Sure Strike); Ranger: Patience + Lifetaker (+ Canto) |
| Fighter: Wrath | Warrior: Wrath + Luna |
| Brigand, Pirate: Despoil | Berserker: Despoil (+ Critical +15) |
| Mage: Mag +2 | Sage: Mag +2 + Luna; Mage Knight: Mag +2 + Celerity (+ Canto) |
| Shaman: Hex | Druid: Hex + Anathema; Summoner: Hex (+ Summon) |
| Monk: Miracle | Bishop: Live to Serve (+ Slayer) |
| Priest, Cleric: Live to Serve | Bishop: Live to Serve (+ Slayer); Valkyrie: Savior + Renewal (+ Canto) |
| Troubadour: Savior (+ Canto) | Valkyrie: Savior + Renewal (+ Canto) |
| Pegasus Knight: Tantivy (+ Canto) | Falcon Knight: Tantivy + Bowbreaker (+ Canto) |
| Wyvern Rider: Str +2 (+ Canto) | Wyvern Lord: Str +2 + Swordbreaker; Wyvern Knight: Str +2 (+ Pierce) (+ Canto) |
| Journeyman, Pupil, Recruit (all tiers): Underdog | Pupil (3): Underdog + Luna; Journeyman (3), Recruit (3): Underdog (+ Critical +15) |
| Dancer: (Dance) | |
| Revenant: HP +5 | Entombed: HP +5 + Renewal |
| Bonewalker: Armsthrift | Wight: Armsthrift + Pavise |
| Bonewalker (bow): Patience | Wight (bow): Patience + Luna |
| Bael: Poison Strike | Elder Bael: Poison Strike + Lifetaker |
| Mauthe Doog: Vantage | Gwyllgi: Vantage + Sol |
| Tarvos: Celerity | Maelduin: Celerity + Luna |
| Mogall: Hex | Arch Mogall: Hex + Anathema |
| Gargoyle: Tantivy | Deathgoyle: Tantivy + Aegis |
| | Cyclops: Wrath + Wary Fighter; Gorgon: Hex + Anathema |

FE8's own skills are read from the game's class data, so the stat screen always shows the real list. Any other class (Soldier, Manakete, the civilians and so on) has no new skills, only FE8's own where it has one (the Necromancer's Summon, for example).

**Personal skills** (with Personal; ★ = only once the character's class is promoted):

| | | | |
|---|---|---|---|
| Eirika: Lancebreaker★ | Ephraim: Axebreaker★ | Seth: Charm | Franz: Discipline |
| Gilliam: Tomebreaker★ | Moulder: Savior | Vanessa: Bowbreaker★ | Ross: Wrath |
| Garcia: Swordbreaker★ | Neimi: Underdog | Colm: Celerity★ | Artur: Miracle |
| Lute: Discipline | Natasha: Renewal★ | Joshua: Despoil | Forde: Patience |
| Kyle: Def +2 | Tana: Charm | Amelia: Underdog | Innes: Tantivy |
| Gerik: Str +2 | Tethys: Pass | Marisa: Wrath | L'Arachel: Miracle |
| Dozla: HP +5 | Saleh: Mag +2 | Ewan: Discipline | Cormag: Lifetaker★ |
| Rennac: Despoil | Duessel: Aegis | Knoll: Hex | Myrrh: Renewal |
| Syrene: Savior | | | |

**What each skill does:**

| Skill | Effect |
|---|---|
| Charm | Allies within 2 squares: +5 Hit, +5 Avoid. |
| Savior | No Skill/Speed halving while rescuing. |
| Wary Fighter | Neither side gets a follow-up attack in its battles (brave weapons still hit twice). |
| Armsthrift | Luck x2 % chance a hit doesn't use up the weapon. |
| Vantage | At half HP or less, strikes first when attacked. |
| Patience | +10 Hit, +10 Avoid during the other side's phase. |
| Wrath | +20 Crit at half HP or less. |
| Despoil | A kill: Luck % chance of 300 gold (your units). |
| Miracle | Luck % chance to survive a killing hit on 1 HP (when above 1 HP). |
| Live to Serve | Healing an ally with a staff heals you by the same amount. |
| Str +2, Mag +2, Skl +2, Spd +2, Lck +2, Def +2, Res +2 | That stat +2 (Str and Mag are the same stat; magic classes show it as Mag +2). |
| HP +5 | Max HP +5. |
| Hex | Adjacent enemies: -15 Avoid. |
| Tantivy | No ally within 2 squares: +10 Hit, +10 Avoid. |
| Underdog | Against a higher-level foe (a promoted unit counts 20 levels more): +15 Hit, +15 Avoid. |
| Discipline | Weapon EXP x2. |
| Sol | Skill/2 % chance: heal HP equal to the damage dealt (after the battle; not in the arena). |
| Luna | Skill/2 % chance: the hit ignores half the foe's Def or Res. |
| Pavise | Skill % chance: halve damage from swords, lances, axes and physical monster attacks. |
| Aegis | Skill % chance: halve damage from bows and magic. |
| Lifetaker | A kill on your own attack: recover half your max HP. |
| Poison Strike | After a battle in which you hit, the foe loses 20% of its max HP (it can't kill). |
| Renewal | Start of your turn: recover 20% of max HP (adds to a fort's heal). |
| Celerity | Mov +1. |
| Anathema | Enemies within 3 squares: -10 Avoid, -10 crit evade. |
| Swordbreaker, Lancebreaker, Axebreaker, Bowbreaker, Tomebreaker | +30 Hit, +30 Avoid against that weapon type (Tomebreaker: any magic). |
| Astra | Skill/2 %, when the unit attacks: that attack becomes 5 strikes at half damage (crits still triple a strike first). At most once a battle. It plays as one attack: faded copies of the unit wind up the critical-hit move at different speeds and join the unit for one blow, and the HP bar drops once by all 5 strikes. Not with a ballista. |
| Pass | The unit can move through squares held by enemies (it still can't stop on one). In fog, it walks past enemies it can't see, unless one stands on the square it is moving to. |
| Nihil | The foe's skills do nothing in a battle against this unit, FE8's own too (Sure Strike, Pierce, Great Shield, Silencer, Critical +15). Stat bonuses, Celerity, Renewal and Discipline stay. With Enemy skills on (either mode), every enemy and green boss has it. |
| Canto, Steal, Locktouch, Critical +15, Silencer, Sure Strike, Pierce, Great Shield, Slayer, Summon, Dance | FE8's own, unchanged. Critical +15 is the unnamed +15 Crit of Swordmasters, Berserkers and the third-tier Journeyman and Recruit; Locktouch means the unit can use Lockpicks. |

#### Third promotion

With **Third promotion** on (v1.23.0; Off by default; for the next New Game, like every row here), **every promotion screen offers three classes** in a game without the randomizer: the game's own choices first, unchanged and in their usual order, then the classes below. It is the same in every game (no randomness). Every way to promote shows the same three: the crests and seals (Hero Crest, Knight Crest, Orion's Bolt, Elysian Whip, Guiding Ring, Ocean Seal, Master Seal, the twins' braces), the preparations' promotion, the skills preview on the promotion screen, and **Chapter 16's "promote now?" answered yes, which opens the three-choice screen for each twin still in their Lord class** (B does nothing there, as before). A twin who already promoted is not promoted again by that event. With the row on, **Eirika and Ephraim keep their own weapons in any class they promote into** (the Rapier and Sieglinde, Reginleif and Siegmund), since their third choice may be no lord class. Off, the screens are exactly as in v1.22.0: the original's (with the FE7 ROM: the Myrmidon's Blade Lord and the Knight's Armor Lord as before, with the descriptions they had). In a randomized game the row does nothing: the randomizer's own promotions apply (v1.21.0 and later already give every screen three classes), the in-chapter Reforged menu's **This run** shows the row Off, and the game's saves carry no mark for it.

The classes FE7 and FE6 add need your own FE7 or FE6 ROM (FE7 classes and FE6 classes, below). Without the needed ROM the screen shows an FE8 class in its place (marked * below): the class the missing one would be saved as without its ROM when that is not on the screen already and uses a weapon of the unit's class, else the FE8 class of the same tier and the unit's sex that is not on the screen and fits best (a weapon kind of the unit's class first, then the most of the missing class's weapon kinds and the fewest others, then the unit's kind of movement, then a fixed order). "Woman" / "man" is the unit, not the class: a woman gets the female version of a class (Swordmaster, Trooper and so on) wherever FE8 has one.

| Class | The game's own | Third (and the twins' second) with the ROM | No FE7 ROM | No FE6 ROM |
|---|---|---|---|---|
| Myrmidon (man or woman) | Assassin, Swordmaster | Blade Lord (FE7; the game's own third with the FE7 ROM) | Ranger* | - |
| Mercenary | Hero, Ranger | Master Lord (FE6) | - | Swordmaster* |
| Knight (man or woman) | General, Great Knight | Armor Lord (FE7; the game's own third with the FE7 ROM) | Paladin* | - |
| Fighter | Hero, Warrior | Armor Lord (FE7) | General* | - |
| Brigand, Pirate | Warrior, Berserker | Armor Lord (FE7) | General* | - |
| Cavalier | Paladin, Great Knight | Trooper (FE6, the unit's sex) | - | Ranger* |
| Archer | Sniper, Ranger | Trooper (FE6, the unit's sex) | - | a man: Warrior*; a woman: Swordmaster* (no female FE8 class left uses bows) |
| Nomad (FE7) | Ranger (with the FE6 ROM also Trooper) | Paladin | - | Ranger, Sniper*, Paladin |
| Mage | Sage, Mage Knight | Archsage (FE7) | Druid* | - |
| Monk | Bishop, Sage | Archsage (FE7) | a man: Druid*; a woman: Valkyrie* | - |
| Priest | Bishop, Sage | Archsage (FE7) | Druid* | - |
| Cleric | Bishop, Valkyrie | Archsage (FE7) | Sage* | - |
| Troubadour | Valkyrie, Mage Knight | Archsage (FE7) | Sage* | - |
| Shaman | Druid, Summoner | Dark Druid (FE7) | Sage* | - |
| Thief, Thief (female, FE6) | Assassin, Rogue | Swordmaster | (no ROM needed) | (no ROM needed) |
| Pegasus Knight | Falcoknight, Wyvern Knight | Wyvern Lord | (no ROM needed) | (no ROM needed) |
| Wyvern Rider | Wyvern Lord, Wyvern Knight | a woman: Falcoknight; a man: Great Knight | (no ROM needed) | (no ROM needed) |
| Eirika's Lord | Great Lord (with the FE7 ROM also Blade Lord) | second Blade Lord (FE7), third Master Lord (FE6) | Great Lord, Ranger*, then Master Lord or Swordmaster* | Great Lord, Blade Lord or Ranger*, Swordmaster* |
| Ephraim's Lord | Great Lord (with the FE7 ROM also Armor Lord) | second Armor Lord (FE7), third General | Great Lord, Great Knight*, General | (no FE6 class) |
| Lion Lord (FE6) | Master Lord | Great Lord (Ephraim's for a man, Eirika's for a woman), Blade Lord (FE7) | Master Lord, Great Lord, Swordmaster* | - |
| Trainees' second promotion (their third tier, once both routes are cleared): Recruit, Journeyman, Pupil | Paladin and Recruit; Hero and Journeyman; Sage and Pupil | the missing class of their line's own pair: Great Knight; Warrior (a woman: General*, the Warrior is a man's class); Mage Knight | (no ROM needed) | (no ROM needed) |

So with no extra ROM: Eirika gets **Great Lord, Ranger, Swordmaster**, Ephraim **Great Lord, Great Knight, General**; with both ROMs Eirika **Great Lord, Blade Lord, Master Lord**, Ephraim **Great Lord, Armor Lord, General**.

- **Weapons of an added class:** a class the row adds (the table's own, like the Thief's Swordmaster, the Wyvern Rider's Great Knight or the Cavalier's Trooper, or a * class) may not use the weapons of the unit's class. The unit keeps its ranks only in the weapon kinds the new class uses (an Archer made a Swordmaster has no bow rank left, a Shaman made a Sage no dark rank, a Cavalier made a Trooper no lance rank: the Trooper fights with swords and bows) and starts the new kinds at the class's own rank, as in any promotion. **A rank in a kind the new class uses is never lowered**: where the new class's starting rank is lower than the old class's (a Fighter made a General, a Priest made a Druid, an Archer made a Warrior) the unit keeps the rank it had, so it can still use what it holds. If it holds a weapon of its old class that it could use before the promotion and can no longer use, and none that it can, it gets **one basic weapon** of the new class, always the plain one of its kind: an Iron Sword, Iron Lance, Iron Axe, Iron Bow, Fire, Lightning or Flux (the Archer an Iron Sword, the Shaman a Fire or Lightning tome; a weapon it could not use before, such as a Silver Bow above its bow rank, earns nothing; with a full inventory the weapon it can no longer use goes to the convoy to make room). The game's own choices are as before (with the FE7 ROM also the Myrmidon's Blade Lord and the Knight's Armor Lord).
- **Wyvern Lord and Druid for a woman** (the Pegasus Knight's third; the Mage's * class) have no promotion gains in the game's data, since no woman promotes into them in the original: they give the male class's gains.
- **Where the unit stands:** a promotion does not move the unit, so a seal used on the map on a tile an added class could not stand on shows **the game's own choices only**. In practice: a man's Wyvern Rider over anything only fliers cross (a peak, the sea, a lake, a river, a cliff, a fence or a thicket: the Great Knight cannot be there), and a unit whose added class is the armoured Armor Lord or General on ground armour cannot enter: a Fighter or a woman's second-tier Journeyman on a mountain, a Brigand on a mountain, on a peak or in a river, a Pirate on a mountain, in a river or in any water, and Ephraim on a mountain or in a river (his Solar Brace there shows the game's own: Great Lord, with the FE7 ROM also Armor Lord). Use the seal on ground the class can enter, or in the preparations, for all three.
- **Trainees' first promotion** (Ross, Amelia and Ewan at level 10) is the original's: two classes, and the third only once both routes are cleared, as in the original.
- **Monsters** (only through the test switches) get their own promotion, then two monster classes of the same tier that fit best: Revenant Entombed, Elder Bael, Gwyllgi; Bonewalker Wight, Deathgoyle, Entombed; Bonewalker (bow) Wight (bow), Maelduin, Entombed; Bael Elder Bael, Entombed, Gwyllgi; Mauthe Doog Gwyllgi, Entombed, Elder Bael; Tarvos Maelduin, Cyclops, Entombed; Mogall Arch Mogall, Gorgon, Entombed; Gargoyle Deathgoyle, Wight, Entombed. The Gorgon Egg, the Dancer, the Soldier, the Manaketes, the Bard, the King and the Divine Dragon have no promotion screen and keep the original's.
- **Saves:** a unit promoted into a class of FE7 or FE6 is saved like any unit in that class (a stand-in class in the save, the real class in the side file; FE7 classes and FE6 classes, below): Blade Lord as Swordmaster, Master Lord as Swordmaster, Armor Lord as General, Trooper as Ranger, Archsage as Sage, Dark Druid as Druid (female versions for a woman). Loaded without that ROM, or by v1.22.0 or older, the unit plays as the stand-in. Older versions do not know the row, so a twin who took a class that is no lord class cannot use their own weapons there.
- With Skills on, the classes have their usual skills (a foreign class its stand-in's).

### Controls

Choose **Controls >** in Reforged Options to change which keyboard keys and gamepad buttons do what. A change works at once and is written to the settings file when you leave with B.

- **Rows:** A, B, Select, Start, Right, Left, Up, Down, R, L, Fast fwd (fast-forward, held) and Fullscreen, then **Reset to defaults**.
- **Columns:** two keyboard keys (Key, Key 2) and two gamepad buttons (Pad, Pad 2) for each row. A dash means none.
- **Keys on this screen:** Up and Down pick a row, Left and Right a column. **A** on a cell waits for the key (or, in a Pad column, the gamepad button) to put there; **Escape** cancels, **Delete** clears the cell, and after 10 seconds it gives up. **Select** clears the cell. **A** on Reset to defaults puts every key and button back. **B** goes back.
- **A key or button does one job.** Binding a key that another row already has moves it: that row gets this cell's old key instead (a swap).
- **You cannot lock yourself out.** A, B, Start and the four directions always keep at least one key and one gamepad button: clearing the last one, or a swap that would take it away, is refused, and the help line says why.
- **Gamepad buttons:** A and B are the buttons marked A and B (Cross and Circle on PlayStation pads), wherever your pad has them; West and North are the other two face buttons; ZL and ZR are the triggers; D-Up and the like the D-pad; LStk and RStk the sticks pressed in. The left stick always moves too, whatever the bindings.
- If something goes wrong, delete the `control_*` lines from the settings file (below), or choose Reset to defaults.

| Row | Keyboard (default) | Gamepad (default) |
|---|---|---|
| A | Z | A |
| B | X | B |
| Select | Tab | Back |
| Rewind (held) | Backspace | none |
| Start | Enter, Keypad Enter | Start |
| Right, Left, Up, Down | the arrow keys | the D-pad |
| R | E | R, ZR |
| L | Q | L, ZL |
| Fast fwd | Space | right stick pressed in |
| Fullscreen | F11 | none |

### Display

Choose **Display >** in Reforged Options. A change shows at once and is written to the settings file when you leave with B. These options never mark a save.

| Row | Choices (the default in bold) | What it does |
|---|---|---|
| Window size | **Auto** / 1x-8x | The window's size, in multiples of the GBA's 240x160. Auto: 3x, or less when the screen is too small. Lowered to fit your screen. The `FE8PC_SCALE` setting, when set, wins. |
| Scaling | **Pixel** / Fill / Stretch | Pixel: whole-number scaling, every pixel the same size, black bars around the picture. Fill: as large as the window allows, keeping the shape. Stretch: fills the window, changing the shape. |
| Smoothing | On / **Off** | Softens the picture in Fill and Stretch (pixels of uneven size look smoother). Pixel scaling is always sharp. |
| Fullscreen | On / **Off** | The game starts fullscreen. F11 (or its new key, under Controls) still switches. |

### Helpers

Choose **Helpers >** in Reforged Options (the in-chapter Reforged screen lists the same rows). Enemy phase, True hit, Arena odds, Skill banners, Myrrh look and Keep game Options only change how things look or how fast they go, so they never mark a save. Convoy anywhere, Pass skirmishes, Ruin floor select and Faster supports change play: a save made with one on is marked, as with Casual mode. Every row acts at once.

| Row | Choices (the default in bold) | What it does |
|---|---|---|
| Enemy phase | Normal / 2x / **4x** | Enemy and other-army turns run this much faster; the sound follows "Fast-forward sound". **Start** during an enemy turn skips the rest of it (8x, muted) until your turn. A boss's line during the enemy phase still waits for A. |
| True hit | **On** / Off | The battle forecast and the battle screen show the real chance to hit. The game rolls two numbers and averages them, so a shown 70 hits 82% of the time and a shown 30 only 18%. Off shows the game's own number. |
| Arena odds | **On** / Off | The arena shows its opponent before asking for the wager (the original only after you pay), with your unit's and the opponent's hit, damage ("x2" when one strikes twice) and crit, as the fight will use them (True hit's number when that is on). Display only. |
| Skill banners | **On** / Off | When a battle skill activates (Luna, Sol, Astra, Pavise, Aegis, Miracle, Vantage, Lifetaker), its name shows: in a battle animation a small banner slides in beside that unit's HP box as the attack starts, in a map animation a short popup appears next to the unit. Enemy skills show too. Display only. |
| Myrrh look | On / **Off** | Needs your own FE6 ROM (see FE6 classes below; without it the row is grey and Myrrh keeps FE8's look). Myrrh's dragon form in battle animations becomes FE6's Divine Dragon: its own transformation, the white dragon and its way back, in place of FE8's. Everything else about her fights stays the same (hits, damage, the order of the strikes); only Myrrh changes, and map animations are unchanged. Display only. |
| Convoy anywhere | On / **Off** | Every unit's menu has Supply wherever the chapter has the convoy, not only the lord's and the units next to the lord. |
| Rewind | On / **Off** | **Hold Backspace** (the Rewind key, under Controls) for a moment on the map, with no unit picked and no menu open, to go back to the start of your turn: everything done since is undone. Hold it again right away to go one more turn back (up to 8 turns in a chapter). It uses the save the game makes at the start of every turn. Not with Nuzlocke (its deaths stay), and not in the tutorial chapters. |
| Pass skirmishes | On / **Off** | On the world map, your army walks through a place held by monsters instead of stopping for a skirmish. Ending your move there, pressing A on your own place while monsters are on it, or monsters walking onto you still starts the fight. |
| Ruin floor select | On / **Off** | Entering the Lagdou Ruins asks which floor to start on: Left/Right picks from floor 1 up to one past the best floor you have ever cleared (at most 10), A goes in, B goes back to the map. Floor 1 is the original's full run (its records and Lyon's three full runs count as before). A run started higher up only adds its kills to the Ruins' total: the best records and clear counts stay. Glen (floor 5) and Valter (floor 7) join when their floor is played after you beat the game, so pick it then. The best floor is kept with each save (in the `.reforged` file beside it), even while the option is off; that alone does not mark the save. |
| Faster supports | **Off** / x2 / x3 | Support points come 2 or 3 times as fast, and two units who can support each other gain them when they end a turn within 3 squares, not only side by side (from turn 2, as in the original). Pairs (Pair Up) still gain twice as fast as two units side by side. Support conversations still need the two next to each other. |
| Keep game Options | On / **Off** | Every New Game starts with the game's own Options (Animation, Game Speed, Text Speed and the other rows of the in-game Options screen) as you last set them, instead of the original's defaults. They are remembered each time you leave the Options screen in a game, and at once when you switch this on in a chapter's Reforged screen (where it is also listed). Saves you already have keep their own Options. |

### Option sets

Choose **Option sets >** to save every option as it is now (the options screen, the Cheats screen, the Randomizer screen and the seed) as one of three sets, or to load a set back. Keys and Display are not part of a set: they belong to your computer, not to a run. Loading a set puts every option back as it was saved (an option the set does not name goes to its default) and writes the settings file. The sets are files named `option-set-1.txt` to `option-set-3.txt` beside the settings file, in the same format, so they can be copied or shared.

### Command line

The game also takes settings on its command line, for that run only (they are written to the settings file only if you then change an option in Reforged Options):

- `--set KEY=VALUE`, as often as you like: any line of the settings file (below), for example `--set casual_mode=1` or `--set control_a=C`.
- `--scale N` (window size N, 0 for Auto), `--fullscreen`, `--windowed`.
- `--seed X` (the next New Game's randomizer seed), `--randomizer` (the randomizer on).

### The settings file

- **Name:** `reforged-options.txt`.
- **Where:** on Windows, `%APPDATA%\fe8pc\`. On Linux, `$XDG_DATA_HOME/fe8pc/`, or `~/.local/share/fe8pc/` when `XDG_DATA_HOME` is not set. It is always there, wherever your ROM and save are.
- The file is made only after you change a value. It is written when you leave Reforged Options (or the Cheats or Randomizer screen) with B, and when a new game picks a seed for a Random seed.
- It is read once, when the game starts. If you edit it while the game runs, the change counts at the next start.
- It is plain text: one `key = value` per line. Lines starting with `#` or `;` are comments.

| Key | Values |
|---|---|
| `danger_zone_select` | 0 = off, 1 = on |
| `fast_forward` | 0 = off, 1 = on |
| `unbreakable_weapons` | 0 = off, 1 = your units, 2 = everyone |
| `casual_mode` | 0 = off, 1 = on |
| `fast_forward_speed` | 2, 3, 4 or 8 |
| `fast_forward_sound` | 0 = mute, 1 = sped up |
| `growth_rates_select` | 0 = off, 1 = on |
| `original_game_bug_fixes` | 1 = on (the default), 0 = off |
| `map_hp_bars_warnings` | 0 = off, 1 = bars, 2 = warnings, 3 = both (also `off`, `bars`, `warnings`, `both`) |
| `window_size` | 0 = auto, 1 to 8 (also `auto`, `3x` and the like) |
| `scaling` | 0 = pixel, 1 = fill, 2 = stretch (also the names) |
| `smoothing`, `fullscreen` | 0 = off, 1 = on |
| `fix_early_kill_exp`, `fix_promoted_enemy_levels`, `fix_ai_heal_full_hp`, `fix_canto_after_trade`, `fix_devil_high_luck`, `fix_ch15_staff_drop`, `fix_pair_up`, `fix_smart_enemies`, `fix_myrrh_buff`, `fix_join_ranks`, `fix_armed_dancers`, `fix_third_promotion` | 0 = off (the default), 1 = on (the Game fixes screen) |
| `fix_skills` | 0 = off (the default), 1 = class, 2 = class + personal (also `off`, `class`, `personal`) |
| `fix_enemy_skills` | 0 = off (the default), 1 = promoted enemies and bosses, 2 = all (also `off`, `promoted`, `all`) |
| `cheat_infinite_gold` | 0 = off, 1 = on |
| `cheat_growths` | 0 = off, 1 = fixed, 2 = +25%, 3 = +50% |
| `cheat_exp` | 0 = x1, 1 = x2, 2 = x3 |
| `randomizer` | 0 = off, 1 = on |
| `randomizer_seed` | the seed; 0 = Random. The game writes it as `0x` and 8 hex digits. It reads `0x` and hex digits, or exactly 8 hex digits as the Seed row shows them (`01234567` is the hex seed 0x01234567), or else a decimal number (leading zeros allowed, never octal). A negative number, other characters or more than `0xFFFFFFFF` give Random (logged). |
| `randomizer_growths` | 0 = off, 1 = redistribute, 2 = delta, 3 = full |
| `randomizer_growth_variance`, `randomizer_growth_min` | 0 to 100 (30 and 5) |
| `randomizer_growth_max` | 0 to 255 (80) |
| `randomizer_adjust_hp_growths` | 0 = off, 1 = on |
| `randomizer_bases` | 0 = off, 1 = redistribute, 2 = delta |
| `randomizer_base_variance` | 1 to 10 (5) |
| `randomizer_class_movement`, `randomizer_constitution`, `randomizer_affinity` | 0 = off, 1 = on |
| `randomizer_player_classes`, `randomizer_include_lords`, `randomizer_include_thieves`, `randomizer_include_dancer`, `randomizer_monster_classes`, `randomizer_assign_evenly` | 0 = off, 1 = on |
| `randomizer_match_gender` | 1 = on (the default), 0 = off |
| `randomizer_class_growths` | 0 = keep, 1 = transfer |
| `randomizer_class_bases` | 0 = final, 1 = personal |
| `randomizer_keep_colours` | 1 = on (the default), 0 = off |
| `randomizer_recruitment`, `randomizer_recruit_cross_gender` | 0 = off, 1 = on |
| `randomizer_recruit_class` | 0 = own, 1 = slot |
| `randomizer_recruit_stats` | 0 = autolevel, 1 = slot |
| `randomizer_nuzlocke` | 0 = off, 1 = on |
| `control_a`, `control_b`, `control_select`, `control_start`, `control_right`, `control_left`, `control_up`, `control_down`, `control_r`, `control_l`, `control_fast_forward`, `control_fullscreen` | four bindings, `key \| key 2 \| gamepad \| gamepad 2`, each a name or `none`. Keys by their name (`Z`, `Return`, `Keypad Enter`, `Left Shift`, `F11`, ...); gamepad: `a`, `b`, `west`, `north`, `back`, `start`, `guide`, `l`, `r`, `zl`, `zr`, `dpup`, `dpdown`, `dpleft`, `dpright`, `lstick`, `rstick`. A name the game does not know keeps that binding's default (logged). |
| `randomizer_boss_classes`, `randomizer_enemy_classes` | 0 = off, 1 = on |
| `randomizer_enemy_growths` | 0 = off, 1 = flat, 2 = scaling |
| `randomizer_enemy_buff` | 0 to 100 (10) |
| `randomizer_boss_stats` | 0 = off, 1 = linear, 2 = ease |
| `randomizer_boss_max_boost` | 1 to 20 (5) |
| `randomizer_enemy_weapons`, `randomizer_weapon_traits`, `randomizer_enemy_drops` | a chance, 0 (off) to 100 |
| `randomizer_weapon_might`, `randomizer_weapon_hit`, `randomizer_weapon_weight`, `randomizer_weapon_uses`, `randomizer_rewards`, `randomizer_shops` | 0 = off, 1 = on |

- A switch also takes `on`/`off`, `yes`/`no` or `true`/`false`, in any case.
- Some keys also take words: `unbreakable_weapons` takes `yours` or `all`; `fast_forward_speed` takes `4x` and the like; `fast_forward_sound` takes `mute` or `sped_up`; `cheat_growths` takes `fixed`, `+25%` or `+50%`; `cheat_exp` takes `x1`, `x2` or `x3`. The randomizer's modes take their names (`redistribute`, `delta`, `full`, `keep`, `transfer`, `final`, `personal`, `own`, `slot`, `autolevel`, `flat`, `scaling`, `linear`, `ease`), its numbers may end in `%`, and its chances take `off`.
- The `randomizer` keys are the settings for the next New Game only. They never mark a save; each save keeps its own copy in the side file.
- A missing file, a missing key or a bad value gives that option's default.
- `fast_forward = 0` counts only in a file that also has a `format = 2` line or a `fast_forward_speed` line. The game always writes both. (A file from an older test build could hold `fast_forward = 0` without the player choosing it; that line is ignored.)
- The settings are never put in the `.sav`.

### The side file next to your save

- **Name:** the save's name plus `.reforged`, for example `FE8.sav.reforged`, beside the `.sav`.
- It appears only when the game saves while a gameplay option is on (Unbreakable weapons, Casual mode or any cheat), while retreated units are waiting to come back, or in a randomized game. Danger zone, fast-forward, growth rates on Select, HP bars & warnings and the original-game bug fixes never mark a save.
- It says which save slots (1 to 3, and the suspend save) were made with which options. It also holds Casual mode's list of retreated units that are waiting to come back.
- **For a randomized game it holds the randomizing:** a line such as `game1_rando = v=1 seed=0x1234ABCD randomizer=1 ...` for each slot, with the seed and every Randomizer setting the game was started with. When you load that slot, the port randomizes the game again from that line, whatever the Randomizer screen says by then. Without the line (the side file lost, or the slot changed in mGBA), the slot loads with the original game's tables (see Randomizer above).
- **For a Nuzlocke game it holds the graveyard:** a line such as `nuzlocke = 0x1234ABCD 3 2,5` (the seed, the game's playthrough number, the dead characters' ids). It is written the moment a unit dies.
- The game does not show the mark on screen. It is only in this file.
- When no slot is marked any more, the game removes the file at its next save.
- **The `.sav` itself does not change.** It stays the normal save that mGBA reads.
- **Keep the side file next to the `.sav`.** If you copy or move the `.sav`, copy the `.reforged` file with it. Without it, retreated units that are still waiting (for example in a suspended chapter) do not come back, and a randomized game goes on with the original game's tables.
- **If you play the `.sav` in mGBA:** mGBA plays it as usual and ignores the side file. Units that retreated in a suspended chapter are dead there, because mGBA does not know the list. The side file keeps a fingerprint of each slot. When mGBA (or a backup you put back) changes a slot, the port throws away that slot's notes at its next start. Those units then stay dead for good, as in the original game, and a randomized slot goes on with the original game's tables. mGBA itself always plays a randomized save with the original game's tables.
- **For a unit in an FE7 class** it holds that unit's real class and any weapon rank the stand-in class cannot have (a line such as `suspend_fe7 = 2:0x83:bow=31` or `game1_fe7 = 2:0x83:bow=31`; see FE7 classes below); v1.19.0: the FE6 classes use the same line, for example `game1_fe7 = 3:0x8C`).

## FE7 classes

If you also own **Fire Emblem (the GBA game known as FE7, USA version)**, the port can borrow some of its classes, with their battle animations, map sprites and names (in a game with the Game fix Third promotion on, v1.23.0, their descriptions are the port's own: two lines ending with what the class can equip, e.g. "Equip: Swords, bows.", like the game's own; in every other game they show the FE7 game's text, as before):

| Class | Who gets it |
|---|---|
| **Nomad** (male and female) | Mounted bow users with Canto. They promote to **Ranger** with an Orion's Bolt or a Master Seal (no choice to make). Horse-slaying weapons work on them. **Only in randomized games** (v1.21.0, "FE7/FE6 classes"): a unit whose new class is an Archer may be a Nomad, and late enemy Archers may be. |
| **Blade Lord** | A third choice for **Myrmidons** (both sexes) when they promote with a Hero Crest or a Master Seal, beside Assassin and Swordmaster. |
| **Blade Lord (Eirika)** | Eirika's **Lunar Brace** asks: Master Lord or Blade Lord. As a Blade Lord she keeps her Rapier and Sieglinde. Chapter 16's story promotion does not promote her again. |
| **Armor Lord** (v1.16.0) | Hector's Great Lord from FE7, shown as Armor Lord because FE8 already calls the twins' promoted class "Great Lord". A third choice for **Knights** (both sexes; the art is Hector's) when they promote with a Knight Crest or a Master Seal, beside General and Great Knight. Swords and axes (hand axes are thrown), and a lance rank kept from the Knight: with a lance he fights with the General's animation. Armored movement. |
| **Armor Lord (Ephraim)** (v1.16.0) | Ephraim's **Solar Brace** asks: Great Lord (FE8's own, mounted) or Armor Lord. As an Armor Lord he keeps Reginleif and Siegmund, has a sword and axe rank, and his lance rank as FE8's Great Lord would. On foot with armored movement. With a lance he fights with his mounted Great Lord animation. Chapter 16's story promotion does not promote him again. |
| **Archsage** (v1.16.0) | **Only in randomized games** with the Randomizer's "FE7/FE6 classes" row On (v1.17.0): a rare third promotion choice for magic classes with Random promotions (v1.21.0: also the third choice of Mages, Monks, Priests, Clerics and Troubadours without Random promotions, and a unit whose new class is a Sage), and some late magic bosses and enemies (Randomizer, above). Not in regular games. A Sage's weapon ranks plus dark magic at D, stats a little above a Sage's. |
| **Dark Druid** (v1.16.0) | **Only in randomized games**, as the Archsage (for the dark-magic classes: Shamans; late Druid bosses and enemies). A Druid's weapon ranks plus light magic at D, stats a little above a Druid's. |
| **Bard** (v1.18.0) | Nils's class from FE7, with his map sprites and his playing animation. **Nobody can become a Bard yet** (only the test switches give the class). **Only in randomized games** (v1.21.0): Tethys as a Dancer may be a Bard (1 time in 8, Player classes On). A Bard has FE7's Play as FE8's **Dance**: the same command, the same refresh (and the dancer's rings) as a Dancer, with the Dancer's refresh sparkle in battle animations. No weapon ranks, unless the Game fix "Armed dancers" is on: then anima magic at E (Fire), and its fights with a weapon use the map animation. Saves store a Dancer as the stand-in. |

With Skills on (Game fixes), a Nomad has an Archer's skills, a Blade Lord a Swordmaster's, Eirika's Blade Lord her Master Lord's (FE8's Great Lord), an Armor Lord a General's, Ephraim's Armor Lord his Master Lord's, an Archsage a Sage's, a Dark Druid a Druid's and a Bard a Dancer's. (These notes say Master Lord for the twins' own promoted class, which the game names Great Lord.)

**Chapter 16 and the twins:** at the end of Chapter 16 the game asks whether Eirika and Ephraim should promote now. **Answering "yes" opens the promotion screen for each twin in turn (v1.16.1)**, the same one the Lunar Brace / Solar Brace shows: Eirika picks Great Lord (FE8's Master Lord) or Blade Lord, Ephraim Great Lord or Armor Lord, with the preview, the description, the stat gains and "You can now use". The promotion is not optional here, so B does nothing on that screen (as for a trainee); picking Great Lord gives exactly what the original game's "yes" gives. Answering "no" works as before: they get their brace, and using it later gives the same choice. Without the FE7 ROM, "yes" promotes them to Great Lord at once, as in the original game. With the Game fix **Third promotion** on (v1.23.0), "yes" opens the three-choice screen for each twin, FE7 ROM or not (Game fixes, above). A twin who is already a Blade Lord or Armor Lord is not promoted again by that event. If the randomizer gave a twin another class ("Include lords"), "yes" works as in the original game for them and the brace has no FE7 choice.

**How to turn it on:** put your own FE7 USA ROM file (`.gba`) next to the exe, next to your FE8 ROM, or (v1.22.0) in the `roms` folder. You can also set the environment variable `FE8PC_FE7_ROM` to its full path. The game checks that it is the unchanged USA ROM (exactly 16 MiB, SHA-1 `c735fdbb9e8abe19e0c6a44708df19acc962e204`); any other file is ignored. The log says what it found (lines starting with `[fe7]`).

- The FE7 ROM is only read, each time the game starts. Nothing from it is copied, saved or shipped: the port's download has none of FE7's data. Without the file, the game is exactly as before.
- The randomizer uses the FE7 classes only with its "FE7/FE6 classes" row On, and only for games started with the FE7 ROM loaded (v1.17.0-v1.20.0: Archsage and Dark Druid; v1.21.0: all of them, rarely: promotion choices, your units' classes, late enemies; Randomizer, above); otherwise seeds give the same game with or without the FE7 ROM. Enemies in an FE7 class are saved like your units in a suspended chapter (the stand-in in the save, the real class in the side file).
- **Saves:** a save never holds an FE7 class number, so older versions of the port and mGBA can still read it. It stores a stand-in class (Archer for a Nomad, Swordmaster for a Blade Lord, Master Lord for Eirika or Ephraim, General for an Armor Lord, Sage for an Archsage, Druid for a Dark Druid, female versions for female units) and a note of the real class (in the unit's spare save bits for a chapter save, in the side file for a suspended chapter). A weapon rank the stand-in class cannot use (a Blade Lord's bow rank, Ephraim's Armor Lord sword and axe ranks, an Archsage's dark rank, a Dark Druid's light rank) is saved as none, and the real rank is kept in the side file. v1.15.0 loads a save with one of the v1.16.0 classes as its stand-in class. Loaded with the FE7 ROM, the unit gets its FE7 class and that rank back; loaded without it, it plays as the plain stand-in class, with no bow rank. A save written while the FE7 ROM was missing keeps the stand-in for good. Without the side file, a chapter save still gives the FE7 class back, but not that rank.
- **Test switch** (for trying the classes out): `FE8PC_TEST_UNIT_CLASS=pid:class`, for example `FE8PC_TEST_UNIT_CLASS=0x20:0x80`, gives the player's character `pid` that class at the first player phase of each chapter, with that class's starting weapon ranks. FE7 classes are 0x80 (Nomad), 0x81 (Nomad, female), 0x82 (Blade Lord), 0x83 (Eirika's Blade Lord), 0x84 (Armor Lord), 0x85 (Ephraim's Armor Lord), 0x86 (Archsage) and 0x87 (Dark Druid), and work only when the FE7 ROM is loaded. It changes your game; use a copy of your save. v1.23.0: `FE8PC_TEST_UNIT_LEVEL=pid:level[,pid:level...]`, at the same moment, sets those units' levels (1-20), for example to try a promotion item given with `pid:class:item` (`0x88` is a Master Seal). (`FE8PC_TEST_ARENA_CLASS=class` does the same for an arena fight only; the unit gets its own class back when the arena closes, and a suspend made during the fight saves its own class. Add `FE8PC_TEST_ARENA_WEAPON=item` to choose the fighter's weapon, for example `0x45` for Flux, or `0x28:2` for a Hand Axe thrown from range 2; it is used only when the class has a rank in that weapon type. v1.21.0: `FE8PC_TEST_ARENA_FOE_CLASS=class[:item][:crit]` gives the arena opponent that class instead, in enemy colours, for example `0x8C:0x0D` for an FE6 King with a Killing Edge; `:crit` gives it Skill 120, so it hits and lands critical hits.)

## FE6 classes

If you also own **Fire Emblem: The Binding Blade (FE6, the Japanese GBA game, or a fan translation of it)**, the port can borrow some of its classes too (v1.19.0; Roy's line and the Divine Dragon from v1.20.0), with their battle animations and map sprites. This works with or without the FE7 ROM.

| Class | What it is |
|---|---|
| **Thief** (female) | Cath's class from FE6, with her map sprites and battle animation: FE8 has no female thief. FE6's stats; Steal and lock picking like FE8's Thief (Lockpick, chests, doors). Promotes to Assassin (female) or Rogue with an Ocean Seal or a Master Seal. **Only in randomized games** (v1.21.0): a character whose new class is a Thief may be one (with Match gender only a woman), and late enemy thieves may be (with Match gender only female ones). |
| **Trooper** (Nomad Trooper, male and female) | FE6's promoted Nomad, with its bow and sword animations. Shown as "Trooper" (the full name does not fit some windows). **With the FE7 ROM too**, an FE7 Nomad promoting with an Orion's Bolt or a Master Seal chooses between FE8's Ranger (as before) and the Trooper. FE6's stats, Ranger's promotion gains, a sword and a bow rank, canto and mounted movement. **In randomized games** (v1.21.0): the third promotion choice of Archers (and of bow users with Random promotions), a unit whose new class is a Ranger, late enemy Snipers and Rangers. |
| **King** | Zephiel's class from FE6, with his large caped battle animation and map sprite. Sword A, FE6's stats (HP cap 60 here). A boss class: **only in randomized games** (v1.21.0, "FE7/FE6 classes" with Boss classes): from Chapter 15 on, a boss in a promoted sword class becomes a King 1 time in 6. Never a player's class or promotion. |
| **Lion Lord** (v1.20.0) | Roy's class from FE6, with his map sprite and sword animation. Sword D, FE6's stats. Promotes to Master Lord with a Master Seal or a Hero Crest (level 10 or more, as FE8's swordsmen). **Only in randomized games** (v1.21.0): a character whose new class is a Myrmidon may be one (with Match gender only a man) (in a randomized game its promotion offers Master Lord, FE7's Blade Lord and an FE8 sword class). |
| **Master Lord** (v1.20.0) | Roy's promoted class, with his caped map sprite. Every sword swings with the Binding Blade's animation, flames and all (in FE6 only the Binding Blade does). Sword A. The promotion gives what FE6 gives: HP +4, Str +2, Skl +3, Spd +2, Def +2, Res +5, Con +2, Move +1. **In randomized games** (v1.21.0): the Lion Lord's promotion, Eirika's third choice at her brace or in Chapter 16, the third choice of Mercenaries and Myrmidons (a man's first), and a unit whose new class is a Swordmaster. |
| **Divine Dragon** (v1.20.0) | FE6's Divine Dragon as a class of its own: a Dragonstone fighter shown as the white dragon in battle (no transformation), with FE8's dragon map sprite (FE6's own is only a head). Flies like Myrrh, and like her takes extra damage from bows and from anti-dragon weapons (FE8's rule for a flying dragon; in FE6 it had no weakness to bows). **Only in randomized games** (v1.21.0): Myrrh may be one, 1 time in 8 (Player classes On), else test switch only. The same art can also be Myrrh's dragon form: **Myrrh look** under Helpers. |

**How to turn it on:** put your FE6 ROM file (`.gba`) next to the exe, next to your FE8 ROM, next to your FE7 ROM or (v1.22.0) in the `roms` folder, or set the environment variable `FE8PC_FE6_ROM` to its full path. Translated copies differ in size and checksum, so the game does not check the whole file: it checks the header (`FIREEMBLEM6`, game code `AFEJ`) and that the class, battle animation and map sprite tables it reads are exactly FE6's. The log says what it found (lines starting with `[fe6]`): loaded, not found, or why a file was refused (for example `FE8PC_FE6_ROM` pointing at another game).

- The FE6 ROM is only read, each time the game starts, and kept in memory. Nothing from it is copied, saved or shipped. Without the file, the game is exactly as before (with or without the FE7 ROM).
- The randomizer uses the FE6 classes (v1.21.0) only with its "FE7/FE6 classes" row On, and only for games started with the FE6 ROM loaded (Randomizer, above); otherwise seeds give the same game with or without the FE6 ROM. Enemies in an FE6 class are saved like the FE7 ones (the stand-in in the save, the real class in the side file).
- **Saves:** as for the FE7 classes, a save never holds an FE6 class number. It stores a stand-in class (Thief for the female Thief, Ranger for a Trooper, Ranger (female) for a female Trooper, Swordmaster for a King; v1.20.0: Myrmidon for a Lion Lord, Swordmaster for a Master Lord, the female ones for a female unit, and Myrrh's Manakete class for a Divine Dragon) and a note of the real class (in the unit's spare save bits for a chapter save, in the side file for a suspended chapter). Loaded with the FE6 ROM, the unit gets its FE6 class back; loaded without it, it plays as the stand-in. Older versions of the port (v1.15.0 to v1.18.0; v1.19.0 for the v1.20.0 classes) and mGBA load such a save as the plain stand-in.
- **Test switch:** `FE8PC_TEST_UNIT_CLASS=pid:class` (see FE7 classes) also takes the FE6 classes: 0x89 (Thief, female), 0x8A (Trooper), 0x8B (Trooper, female), 0x8C (King), 0x8D (Lion Lord), 0x8E (Master Lord) and 0x8F (Divine Dragon), only when the FE6 ROM is loaded. So does `FE8PC_TEST_ARENA_CLASS` (the arena needs a weapon rank, so not for the Divine Dragon or Myrrh's class, which have none). `FE8PC_TEST_MYRRH_LOOK_PID=pid` makes that character count as Myrrh for Myrrh look (for a test unit given Myrrh's class 0x3C and a Dragonstone, 0xAA).

## Troubleshooting

| Symptom | What to check |
|---|---|
| A message box says no ROM was found | Put your `.gba` file in the `roms` folder or the exe's folder, or pick it in the "Choose your ROM" window that opens first. |
| A message box says the ROM is not the original FE8 USA ROM | The port needs the unchanged USA ROM. Hacks, translations and cut-short copies are not supported. |
| A message box says `SDL3.dll` is missing or too old | Keep `SDL3.dll` beside `fe8pc-sdl.exe`. Use the copy from the same download. |
| A gamepad does nothing | Plug it in and try again. SDL3 supports most common gamepads; the owner's gamepad was tested on Windows. |
| The game does not start, or stops with an error | Read the log (below). |
| Windows SmartScreen or an antivirus warns about the exe | The exe is not signed, so Windows may warn the first time. Choose "More info", then "Run anyway", or build the exe yourself (BUILDING.md). |

**The log.** The Windows exe has no console window. It writes `fe8pc-sdl.log` next to the exe, or to `%APPDATA%\fe8pc\` if that folder cannot be written. The first line of the log says where it is. Run `fe8pc-sdl.exe --console` to print the log in the terminal you start it from. Attach the log to any bug report. On Linux the log is printed to the terminal you start the game from.

**Where to report a problem:** open an issue on the project's GitHub page (the Issues tab), and attach the log.

## Macs

Some Mac users may try the Windows build through CrossOver or Whisky. **The project has not tested this.** Results are unknown.
