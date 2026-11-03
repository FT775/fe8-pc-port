# Patch notes

> It describes main (`port-d1a-spike`, 2026-10-31): the 2026-10-30 branches `port-rfo-polish`, `port-rfo-label`, `port-retailfix` and `port-savechk2` merged in through `port-rfo-int8` `1a411bc0`.

The PC port plays as the GBA game. Everything in Reforged Options (title menu, Extras) is **off at first**, with two exceptions, which this file lists. You can switch each of them off in Reforged Options.

## On by default

### Fast-forward

- Hold **Space**, or click the gamepad's **right stick**, to run the game faster (4x at first; 2x, 3x or 8x in Reforged Options). Let go and the game is back to normal speed at once.
- Without that key held it does nothing: the game runs at the GBA's speed. While it is held, the game plays exactly the same, only faster.
- To switch it off: Reforged Options, "Fast-forward", Off. PLAYING.md, "Fast-forward", has the details.

### Original-game bug fixes

Fixes for bugs of the original game that stop it for good (a freeze or a softlock). Each one was first made to happen on the GBA game (in mGBA) and on the PC, in the same way, before it was fixed. To switch them off and play the original's behaviour: Reforged Options, "Original-game bug fixes", Off. They never mark a save.

- **Riev's Chapter 20 softlock:** if Riev was killed on turn 1 of Chapter 20 (Darkling Woods), the game froze as soon as the enemy phase started (its scene looks for Riev to point the camera at him). Now the camera and cursor steps are skipped: Riev's line still shows, and the game goes on. The same fix covers any other scene that points the camera or cursor at a unit that is gone (each would freeze the original the same way). Riev's case was reproduced by editing a save to hold what his death leaves, not by a played kill.
- **Pierce freeze:** with battle animations on, a Wyvern Knight's Pierce that hit hard without killing, followed by another hit, could freeze the battle for good (US and European versions). Now Pierce waits for its effect and the HP bar before the next hit, as Sure Shot does, so Pierce battles with animations on run a little longer, with the same results, and no longer freeze.
  - This changes the pace of **every** Pierce hit with battle animations on, not only the ones that would have frozen. Who wins, the damage and the random numbers are the same as in the original, and so is the save, apart from the play time it records. The owner chose to keep it this way (2026-10-30) rather than give Pierce a switch of its own.
  - Tested with your Wyvern Knight attacking. An enemy Wyvern Knight's Pierce goes through the same fixed code (the path the original's Sure Shot already takes for enemy Snipers), but was not tested.
- **A Dancer fighting with a tome (the "monster and dark weapons" freeze):** the dark monster tomes (Evil Eye, Demon Surge and the like) need no weapon rank, so the enemy control glitch, or the randomizer's gifts and drops, can put one in Tethys's hands, and Stone's weapon experience then opens the other dark tomes. Her class's only attack animation is her dance, which never hands over to a second attack of hers: with battle animations on, the battle froze for good when she attacked twice in a row (she doubles a foe that does not counter). Reproduced on the GBA (mGBA, the retail-built ROM) and on the PC. Now a Dancer who fights with a weapon is shown with the map animation; the battle's results are the same. Her dance (Refresh) keeps its animation.

### Game fixes (off by default)

Changes to the original's rules where its result looks unintended, each its own switch on Reforged Options' **Game fixes >** screen, every one off at first (then the game is the original). Like the Randomizer, they are chosen for the next New Game and stay with that game's saves: they cannot be switched on or off in the middle of a run. Each marks a save made with it on. Each was worked out from the decompiled code; PLAYING.md explains each one.

- **Early kill EXP** (Prologue to Chapter 8): a kill of a slightly stronger enemy gave less EXP than a kill of an equal one. Reported as an "EXP penalty"; the code has a bonus with a sudden cut-off, not a penalty.
- **Promoted enemies:** the extra levels a promoted generic enemy gets were rolled with the promoted class's growths, not the unpromoted class's the code passes. Reported as "no promotion bonus"; they do get one, a little weaker than intended. On makes them a little stronger.
- **AI healing:** enemy and green healers could pick a unit at full HP.
- **Canto after Trade:** backing out of Trade or Supply after the menu showed an Attack or Staff range lost Canto.
- **Devil at high Luck:** Luck 32 or more made the Devil Axe backfire every time. Nosferatu and arena opponents above level 31, reported with it, are not bugs in the code (their values cannot overflow).
- **Chapter 15 staff drop:** the Troubadour drops Sleep or Berserk instead of the Mend the English release added. (Chapter 14's Priest has the same pattern, but is left alone: no source says it was a change.)

## Planned (not built)

- **Fimbulvetr: investigated, not reproduced, no fix.** Its snow effect does start 32 sprites at once, the most of any spell, and the game never checks for running out of its 64 process slots or 50 animation slots. But in every case tried (a critical hit, a kill, both sides casting it, against the Demon King and a Draco Zombie, and in the enemy phase) at least 5 process slots and 11 animation slots stayed free, nothing leaked between casts, and no source reports a Fimbulvetr freeze in the retail game. It stays as the original unless a case is found that freezes on the GBA.
- **Not planned:** the pegasus triangle-attack freeze. No source confirms it; the reports are probably the Pierce freeze.

## Other differences

The other deliberate differences from the GBA game (the Extras entry for Reforged Options, the hidden Link Arena entry, the save file beside the ROM) are listed in ACCURACY.md, section 6.

## Credits for the fixes

The Pierce fix follows the description of the FEUniverse "Pierce Fix" ([thread](https://feuniverse.us/t/pierce-fix/699), posted by Brendor in 2015), with the bug's descriptions on Serenes Forest and the Fire Emblem Wiki. The Riev softlock is described on the Fire Emblem Wiki ("Glitch"). Only the ideas were used: both fixes are our own code, written from the decompilation (CREDITS.md, section 8).
