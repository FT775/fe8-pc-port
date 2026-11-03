# Playing the PC port

## What you need

- **Your own copy of the game:** *Fire Emblem: The Sacred Stones* (USA), a `.gba` file, 16 MiB, SHA-1 `c25b145e37456171ada4b0d440bf88a19f4d509f`. No ROM is included: the game reads its graphics, maps and music from your copy. Hacks, translations and cut-short copies are refused.
- **Windows:** the window build, which is three files from the build's output folder: `fe8pc-sdl.exe`, `SDL3.dll` and `LICENSE-SDL3.txt`. Tested on Windows 11; older versions are untested.
- **Linux:** no ready-made Linux build is released. Build the window build from the source (BUILDING.md, `make -f port/Makefile.sdl`) and run it the same way, with the ROM beside it.
- A keyboard, or any gamepad that SDL3 supports.

## Setting up (Windows)

1. Create a folder of your own, for example `C:\Games\FE8\`. Copy `fe8pc-sdl.exe`, `SDL3.dll` and `LICENSE-SDL3.txt` into it. `SDL3.dll` must stay beside the exe.
2. Copy your ROM into the same folder. Any file name ending in `.gba` works. If the folder holds several `.gba` files, `baserom.gba` is taken first, and otherwise the original ROM is found by its SHA-1.
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
| Select | Backspace | Back (View, Share or Create, or Minus, depending on the pad) |
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

- **Where:** next to the ROM. For `FE8.gba`, the save is `FE8.sav`.
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

- It works in two places: after you pick a unit, while its range is shown (for your own unit, that is while you choose where it moves), and when you choose a target (attack, staff and the other target screens).
- **Select** (Backspace, or Back on a gamepad) shows the enemies' danger zone in red. Select again hides it.
- A, B, L or R hide the zone first, then do their normal job with the same press.
- While the zone shows, the path arrow is hidden, but it still follows the cursor. A then moves the unit to the cursor's square, as usual.
- The zone is worked out with your picked unit on the square it stands on now, not where you plan to move it. It always shows the enemies' weapon range, not their staff range.
- As in the original game's own danger zone, enemies hidden by fog or under a roof are left out.
- A Select pressed in the first two frames after you pick a unit is ignored. The original game ignores every key there. Press it again.
- It only changes the picture. It never changes the game.
- When it is Off, Select does nothing on these screens, as in the original game.

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

- On a unit's stat screen (the first page, with the stats), press **Select** (Backspace, or Back on a gamepad). The stat numbers turn into the unit's growth rates: the chance, in %, that each stat rises at a level-up. Select again swaps back.
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
| Infinite gold | On / **Off** | Your gold stays at 999,999. |
| Growths | **Off** / Fixed / +25% / +50% | Changes how stats rise at a level-up. |
| EXP gained | **x1** / x2 / x3 | More experience. |

- **Infinite gold:** after you spend, the gold fills back up to 999,999. Ephraim's Chapter 5x, where the original game shows 0 gold, is left alone.
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
| Growth variance | 0-100%, **30%** | How far Redistribute and Delta move the growths. |
| Growth min | 0-100%, **5%** | No randomized growth goes below this. |
| Growth max | 0-255%, **80%** | No randomized growth goes above this. |
| Adjust HP growths | On / **Off** | HP gets a bigger share of the growths. |
| Bases | **Off** / Redistribute / Delta | Your characters' personal base stats: shared out again, or each moved a little. |
| Base variance | 1-10, **5** | How far the bases move. |
| Class movement | On / **Off** | Each class gets a movement of 4 to 8; a promoted class never has less than the class it comes from. |
| Constitution | On / **Off** | Each character's Con moves a little (never below 3). |
| Affinity | On / **Off** | Each character gets a random support affinity. |
| **Classes** | | |
| Player classes | On / **Off** | Your characters get new classes, of the same tier (unpromoted or promoted). |
| Include lords | On / **Off** | Eirika and Ephraim too. Eirika keeps swords and Ephraim lances. |
| Include thieves | On / **Off** | Colm and Rennac too. |
| Include dancer | On / **Off** | Tethys too. Myrrh never changes. |
| Monster classes | On / **Off** | Your characters may get monster classes. |
| Assign evenly | On / **Off** | Each class is used once before any class is used again. |
| Match gender | **On** / Off | Characters get classes of their own gender. |
| Class growths | **Keep** / Transfer | Transfer: a character's growths follow the new class (personal growth minus the old class's, plus the new class's). |
| Class bases | **Final** / Personal | Final: the starting stats stay about the same. Personal: the personal bases are kept and the new class's bases are added. |
| Keep colours | **On** / Off | A character in a new class keeps their own battle colours (hair, outfit, team colours) instead of the class's generic ones. A few classes have no colour data to work from (monsters, Soldier, some female classes) and stay generic, as do Eirika and Ephraim. |
| **Recruitment** | | |
| Recruitment | On / **Off** | Shuffles who joins where. A character takes another one's place: they join at that point in the story, and the dialogue, names and portraits follow (for example, with Garcia in Seth's place the prologue says "Don't be foolish, Garcia."). Eirika, Ephraim and Myrrh never move; Colm and Rennac only with Include thieves, Tethys only with Include dancer. Places whose story fights need it get a fitting character: Seth and Gilliam fight up close, Artur and Neimi from range, Garcia must attack; Vanessa, Cormag, Syrene and Tana's places go to fliers when one is left. |
| Recruit class | **Own** / Slot | Own: the character keeps their own class (promoted or unpromoted to match the place; a trainee starts in their first real class). Slot: they take the class of the person they replace. |
| Recruit stats | **Autolevel** / Slot | Autolevel: their own stats, levelled up or down with their own growths to the level of the place they take. Slot: the stats of the person they replace. |
| Cross-gender | On / **Off** | Off: men take men's places and women women's. On: anyone anywhere (dialogue still says he or she as written). |
| **Enemies** | | |
| Boss classes | On / **Off** | Bosses get new classes. |
| Enemy classes | On / **Off** | Generic enemies get new classes. |
| Enemy growths | **Off** / Flat / Scaling | Raises the class growths enemies level with: Flat adds Enemy buff, Scaling adds Enemy buff percent of each growth. |
| Enemy buff | 0-100%, **10%** | How much Enemy growths adds. |
| Boss stats | **Off** / Linear / Ease | Bosses get extra stats, more the later they appear in the story. Ease: slow at first and at the end. |
| Boss max boost | 1-20, **5** | The most a boss's stat can gain. |
| Better enemy weapons | **Off** / 5-100% | The chance that an enemy gets a better weapon. |
| **Items** | | |
| Weapon might | On / **Off** | Might moves by up to 3 (kept between 0 and 23). |
| Weapon hit | On / **Off** | Hit moves by up to 20 (kept between 55 and 100). |
| Weapon weight | On / **Off** | Weight moves by up to 5 (kept between 2 and 20). |
| Weapon uses | On / **Off** | Uses move by up to 20 (kept between 15 and 60). |
| Weapon traits | **Off** / 5-100% | The chance that a weapon gets an extra trait. |
| Rewards | On / **Off** | Other items in chests, villages, houses and gifts. |
| Enemy drops | **Off** / 5-100% | The chance that an enemy drops an item. |
| Shops | On / **Off** | Shops and armories sell other items. |
| **Challenge** | | |
| Nuzlocke | On / **Off** | Deaths are final. Casual mode is ignored for this game. A unit that dies (in battle, the arena, or to a trap or poison) is written down at once in the side file next to the save; whenever you load this game again (Resume, Restart Chapter or an older save) that unit is dead again, so a reset never brings it back. A death that ends the game (a lord, or the chapter's must-survive character) is not written down: you go back as usual. To play a Nuzlocke without randomizing, turn Randomizer on and leave everything else off. |

- The default numbers are the Universal FE Randomizer's own defaults.
- The randomizer never uses the game's own random numbers, so battles, level-ups and the rest still work exactly as the game does them.
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
| Select | Backspace | Back |
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
| `fix_early_kill_exp`, `fix_promoted_enemy_levels`, `fix_ai_heal_full_hp`, `fix_canto_after_trade`, `fix_devil_high_luck`, `fix_ch15_staff_drop` | 0 = off (the default), 1 = on (the Game fixes screen) |
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

## Troubleshooting

| Symptom | What to check |
|---|---|
| A message box says no ROM was found | Put your `.gba` file in the same folder as the exe. |
| A message box says the ROM is not the original FE8 USA ROM | The port needs the unchanged USA ROM. Hacks, translations and cut-short copies are not supported. |
| A message box says `SDL3.dll` is missing or too old | Keep `SDL3.dll` beside `fe8pc-sdl.exe`. Use the copy from the same download. |
| A gamepad does nothing | Plug it in and try again. SDL3 supports most common gamepads; the owner's gamepad was tested on Windows. |
| The game does not start, or stops with an error | Read the log (below). |
| Windows SmartScreen or an antivirus warns about the exe | The exe is not signed, so Windows may warn the first time. Choose "More info", then "Run anyway", or build the exe yourself (BUILDING.md). |

**The log.** The Windows exe has no console window. It writes `fe8pc-sdl.log` next to the exe, or to `%APPDATA%\fe8pc\` if that folder cannot be written. The first line of the log says where it is. Run `fe8pc-sdl.exe --console` to print the log in the terminal you start it from. Attach the log to any bug report. On Linux the log is printed to the terminal you start the game from.

**Where to report a problem:** open an issue on the project's GitHub page (the Issues tab), and attach the log.

## Macs

Some Mac users may try the Windows build through CrossOver or Whisky. **The project has not tested this.** Results are unknown.
