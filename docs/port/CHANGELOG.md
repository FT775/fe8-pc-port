# Changelog

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
