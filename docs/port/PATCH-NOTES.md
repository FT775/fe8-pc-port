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

## Planned (not built)

- **More freeze fixes, on by default** once each one is first reproduced on the GBA and on the PC: some monster and dark weapons with battle animations on; Fimbulvetr's snow effect overloading the animation memory. Neither has been reproduced yet.
- **Fixes that change the game, off by default:** the EXP penalty in Chapters 1-8, promoted enemies without their promotion bonus, enemy healers wasting turns on units at full HP, a mounted unit losing the rest of its move (Canto) after opening the trade or convoy menu without using it, the overflow bugs (Devil Axe with very high Luck, Nosferatu, arena opponents above level 31), and the Mend staves that block two Sleep/Berserk staff drops.
- **Not planned:** the pegasus triangle-attack freeze. No source confirms it; the reports are probably the Pierce freeze.

## Other differences

The other deliberate differences from the GBA game (the Extras entry for Reforged Options, the hidden Link Arena entry, the save file beside the ROM) are listed in ACCURACY.md, section 6.

## Credits for the fixes

The Pierce fix follows the description of the FEUniverse "Pierce Fix" ([thread](https://feuniverse.us/t/pierce-fix/699), posted by Brendor in 2015), with the bug's descriptions on Serenes Forest and the Fire Emblem Wiki. The Riev softlock is described on the Fire Emblem Wiki ("Glitch"). Only the ideas were used: both fixes are our own code, written from the decompilation (CREDITS.md, section 8).
