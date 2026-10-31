# Modding the PC port

> Reforged Options is **built** and on the main branch (`port-d1a-spike`, 2026-10-31), with the label, the original-game bug fixes and the hidden "Option sets" row (integrated on `port-rfo-int8` `1a411bc0`, 2026-10-30; long check passed 2026-10-31). The options marked "planned" are **not built**.

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

Game-code hooks for the bug fixes and the Extras label are macros defined at the end of `include/event.h`, `include/anime.h` and `include/savemenu.h` (each under `#if defined(PC_PORT) && PORT_PLAYED_BUILD`), placed on existing lines (appended, or in place of the original return value), so the test builds and the GBA build stay byte-identical (an `#if` block in the middle of a file moves the debug line table). Search for `EVC_ERROR_UNIT_GONE`, `BANIM_PIERCE_KEEP_RIGHT` and `SAVEMENU_PORT_EXTRAS_LABEL` as well as `PC_PORT`.

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
| **Original-game bug fixes** | Fixes for retail freezes, each reproduced on the GBA and the PC first: Riev's Chapter 20 turn-1 softlock (an event command pointing at a unit that is gone is skipped, `include/event.h`) and the Pierce freeze (Pierce acts as Sure Shot in the battle animation, `include/anime.h`; this also paces every Pierce hit with animations on, with the same results). Never marks a save. Key `original_game_bug_fixes`. | On, Off |
| **HP bars & warnings** | Bars: HP bars on hurt units on the map. Warnings: while you move a unit, one "!" on each enemy whose class or weapons are a danger to it, wherever that enemy stands (white: effective against it; orange: a weapon with more than 24 crit). Both: the two together. Display only. | Off, Bars, Warnings, Both |
| **Cheats >** (submenu, open from the start; there is no Konami code) | **Infinite gold** (gold refilled to 999,999); **Growths** (Fixed: average gains, or +25%, +50%; only your units' level-ups from a battle, a staff, a dance or another action; other level-ups, such as the ones the game gives units when they join, stay as in the original); **EXP gained** (x2, x3, still one level per battle). | Each Off (x1) |

The settings are kept in a plain-text file, `reforged-options.txt`, in the per-user folder (`%APPDATA%\fe8pc\` on Windows; `$XDG_DATA_HOME/fe8pc/` or `~/.local/share/fe8pc/` on Linux), one `key = value` per line, so players and modders can edit it. It is never in the `.sav`.

The test builds keep a Konami code of their own, for the game's leftover debug menu (`src/playerphase.c`, `FE8PC_TESTBUILD` only). It is not part of Reforged Options.

## Planned options (not built)

These are planned. Their design may change.

| Option | Plan | Status |
|---|---|---|
| **More original-game bug fixes** | Gameplay-changing fixes (EXP formula, promoted-enemy bonus, healer AI, Canto, overflows, Mend staves) off by default; more freeze fixes (monster/dark weapon animations, Fimbulvetr) on by default once each reproduces on the GBA and the PC (PATCH-NOTES.md). | Planned. The two freeze fixes above are built. |
| **Option sets** | Saved bundles of options for each run. | Planned. Its menu row is hidden until the bundles exist (`RFO_SHOW_OPTION_SETS` in `port/reforged/reforged_menu.c`). |
| **Command-line flags** | The same settings as the menu and the settings file. | Planned. Not built: the settings come from the file and the menu only. |
| **Randomizer** | A new-game setting, with a seed shown and saved. Separate switches for classes, growths, bases, items, recruits and enemies. Nuzlocke-style rule switches. Off by default. | Design only. Its option list follows the Universal FE Randomizer (MIT). |
| **Rebindable controls** | Choose the buttons, in the menu and in the settings file. | Planned. |
| **Window scaling options** | Window size and scaling settings in the menu. | Planned. Today only `FE8PC_SCALE=1` to `10` sets the start size (PLAYING.md). |

## Save files and mods

A save made with a gameplay option or a cheat on is marked: a text file beside the save, `<save>.reforged`, records for each save slot and suspend the options that were not at their defaults (`port/reforged/reforged_savemark.c`). The display-only options, fast-forward and the original-game bug fixes do not mark a save. The `.sav` itself stays the raw 32 KiB image that mGBA reads. This lets bug reports say which settings a game was played with. Gameplay-changing mods should do the same.
