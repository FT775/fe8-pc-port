# Modding the PC port

> Reforged Options is **built** and on the main branch (`port-d1a-spike`, 2026-10-31), with the label, the original-game bug fixes and the hidden "Option sets" row (integrated on `port-rfo-int8` `1a411bc0`, 2026-10-30; long check passed 2026-10-31). The Randomizer submenu is built on the `port-rfo-random` branch (2026-11). The options marked "planned" are **not built**.

## The code

- The game's logic is the decompilation's C source. Most of it is shared with the GBA build, and the GBA build must keep working after every change (BUILDING.md, section 1).
- Names follow the decompilation's own names, and the community's naming conventions where the port has applied them. The checked new names (a map of 1,468 lines on the `port-names11` branch, with its tool `port/tools/apply_names.py`) are on the main branch since 2026-10-31: 1,429 functions and variables renamed, and every check ran again on them, the long check included.
- Some game data is read from the player's own ROM when the game runs: graphics, maps and tilesets, the map asset table, and music and sound. The tables (characters, classes, items, chapter settings), the text and the event scripts are built from the source into the program. No ROM is shipped: players use their own (PLAYING.md).
- **ROM hacks:** the port accepts only the clean US ROM (header, size and SHA-1 `c25b145e…` are checked at start) and refuses a patched one. FEBuilder or UPS-patched ROMs are not supported, and would not work if the check were removed: table, text and event edits are compiled from source, and new assembly in a hack never runs. To change the game, edit the source and rebuild. A source change that resizes data stored on the ROM side (graphics, maps, music) is not yet supported, because the PC build reads those from the retail ROM at their retail addresses.

## Where the PC code lives

| Folder | Contents |
|---|---|
| `port/hw/` | Memory and register layout, frame step, IRQ dispatch, soft reset |
| `port/render/` | The software renderer |
| `port/bios/` | C versions of the GBA BIOS functions |
| `port/asm/` | C versions of the hand-written assembly |
| `port/audio/` | The sound driver's C version and its output |
| `port/save/` | Save files (`.sav`) and the SRAM driver |
| `port/sdl3/` | The SDL3 window, input and sound layer (the only code that includes SDL headers) |
| `port/win32/` | Windows-specific code |
| `port/reforged/` | Reforged Options (played builds only; see below) |
| `port/test/` | The test harness (not part of the game) |

In shared source files, PC-only code sits behind a `PC_PORT` guard, and the original GBA line stays in the `#else` branch. Search for `PC_PORT` to find every place the PC differs. ACCURACY.md explains the policy.

Game-code hooks for the bug fixes, the Extras label and the randomizer are macros defined at the end of `include/event.h`, `include/anime.h`, `include/savemenu.h` and `include/bmunit.h` (each under `#if defined(PC_PORT) && PORT_PLAYED_BUILD`), placed on existing lines (appended, or in place of the original return value), so the test builds and the GBA build stay byte-identical (an `#if` block in the middle of a file moves the debug line table). Search for `EVC_ERROR_UNIT_GONE`, `BANIM_PIERCE_KEEP_RIGHT`, `SAVEMENU_PORT_EXTRAS_LABEL` and `RANDO_` as well as `PC_PORT`.

Two switches exist for tests and played builds. Do not confuse them:

- `FE8PC_TESTBUILD`: test builds only. It adds test code. Players never get it.
- `PORT_PLAYED_BUILD`: played builds. It switches on the few deliberate differences the owner has chosen: the hidden Link Arena entry and Reforged Options. It is 0 in every test build, the Gate 0 binary included (`port/include/port.h`).

## Rules for shared code

- Shared code stays agbcc-compatible C89. Port-only code may use C99 or C11.
- Do not define `MODERN`. It removes `STRUCT_PAD`, and save layouts depend on it.
- Saves are raw copies of structures. Changing a saved structure's layout changes the save format. Check the size asserts in the port code, and see BUILDING.md.
- Keep the GBA gate passing (`fireemblem8.gba: OK`).

## Reforged Options (built)

One entry in the title screen's Extras menu, in place of Link Arena (listed after Sound Room and Support, before Map), in the builds players run only (`PORT_PLAYED_BUILD`; a test build has none of it). The name and the idea come from FE8 Reforged (CREDITS.md). Every option is off by default, except fast-forward, which acts only while its key is held, and the original-game bug fixes (the second stated exception; PATCH-NOTES.md): the Riev fix acts only where the original game freezes; the Pierce fix also gives every Pierce hit with battle animations on Sure Shot's pacing (the same results). With every option off the game is the GBA's. The code is in `port/reforged/`, and the game calls it only under `#if defined(PC_PORT) && PORT_PLAYED_BUILD`.

As built on main (`port-d1a-spike`, 2026-10-31): the 2026-10-30 branches `port-rfo-polish` `8c690589`, `port-rfo-label` `8f99eb49` and `port-retailfix` `7b08e3aa` merged through `port-rfo-int8` `1a411bc0` (rows from `port/reforged/reforged_menu.c` and `reforged_cheats_menu.c`; defaults from `reforged_settings.c`):

| Option | What it does | Values (default first) |
|---|---|---|
| **Danger zone on Select** | Select shows the enemies' range while moving a unit or choosing a target. Display only. | Off, On |
| **Fast-forward** | Hold Space, or press in a gamepad's right stick: the game runs faster while it is held. Speed only, never the game. | On, Off |
| **Fast-forward speed** | How fast. | 4x, or 2x, 3x, 8x |
| **Fast-forward sound** | Muted, or played faster. 8x is always muted. | Mute, Sped up |
| **Unbreakable weapons** | Weapons, staves and Myrrh's Dragonstone keep their uses. The nine legendary weapons still wear down; consumables still run out. | Off, Yours (your units), All |
| **Casual mode** | A player unit that falls retreats instead of dying and is back for the next chapter. Not covered, so they fall as in the original: the lords (Eirika, Ephraim), the chapter's protected unit, enemies, green units, phantoms, arena deaths, scripted deaths (an event that kills a unit) and any death that ends the game. | Off, On |
| **Growth rates on Select** | Select on a unit's stat screen shows its growth rates, coloured by tier. Display only. | Off, On |
| **Original-game bug fixes** | Fixes for retail freezes, each reproduced on the GBA and the PC first: Riev's Chapter 20 turn-1 softlock (an event command pointing at a unit that is gone is skipped, `include/event.h`) the Pierce freeze (Pierce acts as Sure Shot in the battle animation, `include/anime.h`; this also paces every Pierce hit with animations on, with the same results) and the Dancer-with-a-tome freeze (her dance animation never hands over to her second round: a Dancer fighting with a weapon gets the map animation, `include/anime.h` `BANIM_DANCER_WEAPON_MAPANIM`, `port/reforged/reforged_gamefixes.c`). Never marks a save. Key `original_game_bug_fixes`. | On, Off |
| **HP bars & warnings** | Bars: HP bars on hurt units on the map. Warnings: while you move a unit, one "!" on each enemy whose class or weapons are a danger to it, wherever that enemy stands (white: effective against it; orange: a weapon with more than 24 crit). Both: the two together. Display only. | Off, Bars, Warnings, Both |
| **Cheats >** (submenu, open from the start; there is no Konami code) | **Infinite gold** (gold refilled to 999,999); **Growths** (Fixed: average gains, or +25%, +50%; only your units' level-ups from a battle, a staff, a dance or another action; other level-ups, such as the ones the game gives units when they join, stay as in the original); **EXP gained** (x2, x3, still one level per battle). | Each Off (x1) |
| **Randomizer >** (submenu; branch `port-rfo-random`) | A seeded randomizer for the next New Game, after the Universal FE Randomizer (UFER, MIT; its ideas and algorithms, no code): growths, bases, class movement, Con, affinity; player, boss and enemy classes; enemy growth and boss buffs, better enemy weapons; weapon might, hit, weight, uses and traits; rewards, enemy drops, shops; characters' own battle colours on new classes; recruitment (who joins where); Nuzlocke (deaths final through resets). Each save keeps its own run (below). PLAYING.md lists every row. | Off |
| **Game fixes >** (submenu) | Six optional changes to the original's rules, fixed per run like the randomizer (a new game copies the screen's values into the run, `Reforged_GameFixesNewGame`; each save block records the run's on its save-mark line and loading it puts them back, `Reforged_RunFix`; none recorded: off; test switch `FE8PC_FIXES_FORCE` takes the screen's on any load), each a gameplay option (marks a save): early kill EXP never lower for a stronger enemy (`include/bmbattle.h` `RFO_EARLY_KILL_EXP`), promoted generics' pre-promotion levels with the base class's growths (`include/bmunit.h` `RFO_PROMO_AUTOLEVEL`), AI healers skip full-HP units (`include/cp_common.h` `RFO_AI_HEAL_WANTS`), Canto kept after backing out of Trade/Supply (`include/playerphase.h` `RFO_CANTO_MOVECOUNT`), the Devil backfire chance stops at 0% (`RFO_DEVIL_CHANCE`), Chapter 15's Troubadour drops its status staff (`RFO_STAFF_DROP_ORDER`). Same-line macros, the original code in every other build; `port/reforged/reforged_gamefixes.c` (and its self-check under `FE8PC_RFO_CHECK`), `reforged_gamefixes_menu.c`; keys `fix_*`. | Each Off |
| **Reforged** (map menu, during a chapter) | The run-safe options (the main rows, cheats, Display) and a read-only "This run" list (randomizer, seed, Nuzlocke, game fixes). `port/reforged/reforged_field_menu.c`; the PC build reads the map menu from the ROM, so `include/playerphase.h` `RFO_MAP_MENU_DEF` (on `src/playerphase.c`'s two `StartOrphanMenuAdjusted` lines) hands it a host copy with the entry after Options (override id 0x79, hidden in the tutorial). The screen is a list screen run like the game's own Options from the map (`ProcScr_Config_Field`'s steps; `ReforgedListScreen.ended` gives the map back). | - |
| **Controls >** (submenu) | Rebindable controls: two keys and two gamepad buttons for each GBA button, fast-forward and fullscreen; capture, swap on conflict, A/B/Start/directions always bound, reset to defaults. `port/include/port_controls.h` (`port/hw/controls.c`: the bindings as names, every build; the window backend resolves them), `port/reforged/reforged_controls_menu.c` (the screen), `control_*` keys in the settings file. | the original keys |

### Randomizer

- **Files:** `port/reforged/reforged_rando.h` (the interface between the files and the rules they all keep), `reforged_rando.c` (the run, the seeded PRNG and hashes, the boot-time copy of the original tables and the apply pipeline, in UFER's order), `reforged_rando_classes.c` (classes, weapon ranks, inventories, boss buffs, enemy weapons, drops, promotions), `reforged_rando_items.c` (weapon stats and traits, rewards, shops), `reforged_rando_recruit.c` (recruitment: slots, fills, names in text, portraits), `reforged_rando_palettes.c` (Keep colours: per-character battle palettes built for new classes), `reforged_rando_nuzlocke.c` (Nuzlocke: the graveyard), `reforged_randomizer_menu.c` (the screen), `reforged_settings.c` (the keys and the seed) and `reforged_savemark.c` (the run saved with each save block).
- **The run:** a new game (`src/bmsave.c` WriteNewGameSave, `Reforged_RandoNewGame`) copies the screen's settings and seed into the run; the tables (`gCharacterData`, `gClassData`, `gItemData`) are put back to the original first and then rebuilt from the run alone. A seed of 0 is picked then, from the host clock, and written back to the settings file.
- **Determinism:** every choice is a function of the run (seed, algorithm version `RANDO_VERSION`, values) and of fixed content (ids, the chapter index, map coordinates). The game's own RNG is never called: its state is in the suspend save and drives battles. A change to what a seed makes must bump `RANDO_VERSION`.
- **Saves:** the run is written to the save mark as `<block>_rando = v=... seed=0x... randomizer=1 ...` for each save block. Loading a block (ReadGameSave / ReadSuspendSave, before InitUnits) rebuilds that block's tables. With no valid line (the sidecar lost, or a fingerprint that no longer matches the `.sav`) the block loads with the original tables. The `.sav` itself is unchanged.
- **Game hooks:** macros at the end of `include/bmunit.h`, empty (or the value itself) in the test and GBA builds:

| Macro | Where | What it does |
|---|---|---|
| `RANDO_HOOK_UNITDEF` | `src/bmunit.c` LoadUnit, after the genMonster block | May point the unit definition at a copy with another class or items. |
| `RANDO_HOOK_UNITLOADED` | `src/bmunit.c` LoadUnit, before UnitCheckStatCaps | Inventory and rank fixes, enemy buffs, drops. |
| `RANDO_HOOK_WILLREAD` | `src/bmsave.c` ReadGameSave / ReadSuspendSave, before InitUnits | Builds the tables of that save block's run. |
| `RANDO_HOOK_CHEST` | `src/eventinfo.c` | A chest's set item. |
| `RANDO_GIFT_ITEM` | `src/eventscr.c` Event37_GiveItem | A given item (GIVEITEMTO). |
| `RANDO_PROMOTE_JID` | `src/eventscr.c` Event40_PromoteUnit | The class of a scripted promotion. |
| `RANDO_HOOK_EVTBATTLE_ITEM` | `src/eventscr3.c` StartEventBattle | A forced weapon the unit cannot use is dropped. |
| `RANDO_HOOK_PROMO_ITEM` | `src/bmitemuse.c` CanUnitUsePromotionItem | Promotion items for the new classes. |
| `RANDO_HOOK_SHOP` | `src/bmshop.c` StartShopScreen | The list a shop shows. |
| `RANDO_CHARA_PAL` | `src/banim-ekrmain.c`, `src/banim-ekrmainmini.c` | A character battle palette set by index; 0x80 and up are the randomizer's built sets. |
| `RANDO_HOOK_TEXT` | `src/msg.c` GetStringFromIndex | Recruitment: each slot's name becomes its fill's in every decoded text. |
| `RANDO_FACE` | `src/face.c` GetPortraitData | Recruitment: a slot's portrait shows its fill's. |
| `RANDO_HOOK_DEATH` | `src/bmmind.c` KillUnitOnArenaDeathMaybe | Nuzlocke: an arena death is recorded (other deaths through casual mode's hooks). |

- **Settings keys** (`reforged-options.txt`, the next new game's settings; they never mark a save): `randomizer`, `randomizer_seed` (a u32 of its own, written `0x%08X`, read as hex or decimal), `randomizer_growths`, `randomizer_growth_variance`, `randomizer_growth_min`, `randomizer_growth_max`, `randomizer_adjust_hp_growths`, `randomizer_bases`, `randomizer_base_variance`, `randomizer_class_movement`, `randomizer_constitution`, `randomizer_affinity`, `randomizer_player_classes`, `randomizer_include_lords`, `randomizer_include_thieves`, `randomizer_include_dancer`, `randomizer_monster_classes`, `randomizer_assign_evenly`, `randomizer_match_gender`, `randomizer_class_growths`, `randomizer_class_bases`, `randomizer_keep_colours`, `randomizer_recruitment`, `randomizer_recruit_class`, `randomizer_recruit_stats`, `randomizer_recruit_cross_gender`, `randomizer_boss_classes`, `randomizer_enemy_classes`, `randomizer_enemy_growths`, `randomizer_enemy_buff`, `randomizer_boss_stats`, `randomizer_boss_max_boost`, `randomizer_enemy_weapons`, `randomizer_weapon_might`, `randomizer_weapon_hit`, `randomizer_weapon_weight`, `randomizer_weapon_uses`, `randomizer_weapon_traits`, `randomizer_rewards`, `randomizer_enemy_drops`, `randomizer_shops`, `randomizer_nuzlocke`. The ids are `RFO_RANDO_*` in `port/include/port_reforged.h`, with their ranges and defaults; PLAYING.md has the values.
- **Test switch:** `FE8PC_RANDO_CHECK=1` runs the randomizer's self-test at start (many seeds, every option, the results checked and logged); `=exit` then ends the process with 1 on a failure.
- **Test switch:** `FE8PC_RANDO_FORCE=<seed>` (hex with `0x`, or decimal) makes every save load and new game ignore the save mark and the menu's seed and apply the Randomizer screen's settings with that seed, the randomizer on (for scripted runs on saves with no save mark; the settings file is not changed). Unset, it is one look-up that changes nothing.
- **Test switch:** `FE8PC_RANDO_PALDUMP=<file>` writes every class's generic battle palette and every character set (the vanilla ones at start, the run's after each randomized start) as hex, for offline checks.
- **Nuzlocke's graveyard** is per run (the seed and the save's playthrough id), not per block: `nuzlocke = 0x<seed> <id> <pids>` in the save mark, written at once when a unit dies, applied after any block of that run is read.

The settings are kept in a plain-text file, `reforged-options.txt`, in the per-user folder (`%APPDATA%\fe8pc\` on Windows; `$XDG_DATA_HOME/fe8pc/` or `~/.local/share/fe8pc/` on Linux), one `key = value` per line, so players and modders can edit it. It is never in the `.sav`.

The test builds keep a Konami code of their own, for the game's leftover debug menu (`src/playerphase.c`, `FE8PC_TESTBUILD` only). It is not part of Reforged Options.

## Planned options (not built)

These are planned. Their design may change.

| Option | Plan | Status |
|---|---|---|
| **More original-game bug fixes** | A freeze fix is added (on by default) only once the freeze reproduces on the GBA and the PC. The "monster and dark weapons" report turned out to be the Dancer freeze (fixed above); Fimbulvetr was investigated and not reproduced (PATCH-NOTES.md). | None pending. |

Built since the plan: **Display >** (window size, scaling, smoothing, fullscreen: `RFO_WINDOW_SCALE` ... `RFO_FULLSCREEN`, display-only; `Platform_SetWindowScale` / `Platform_SetScaling` / `Platform_SetFullscreen` in `port/platform.h`), **Option sets >** (`option-set-N.txt`; `Reforged_SaveOptionSet` / `Reforged_LoadOptionSet`), and **command-line settings** (`--set KEY=VALUE` and the short forms, `port/hw/main_pc.c`; applied after the settings file by `reforged_settings.c`). The Display and Option sets screens are tables for the Randomizer screen's list code (`port/reforged/reforged_listmenu.h`): a new list screen is a table of rows.

## Save files and mods

A save made with a gameplay option or a cheat on is marked: a text file beside the save, `<save>.reforged`, records for each save slot and suspend the options that were not at their defaults (`port/reforged/reforged_savemark.c`). The display-only options, fast-forward and the original-game bug fixes do not mark a save. A randomized game's run (`<block>_rando`) is kept there too, and only there. The `.sav` itself stays the raw 32 KiB image that mGBA reads. This lets bug reports say which settings a game was played with. Gameplay-changing mods should do the same.
