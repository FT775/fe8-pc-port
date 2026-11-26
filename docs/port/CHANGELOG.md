# Changelog

## v1.34.0 (2026-11-26)

### In short
- With **Skills** on, every skill now says what it does in plain words: on the stat screen's Skills page, the help (R) of a promotion row explains each skill that promotion gives ("+Renewal: Heals 20% of its max HP at the start of each turn"), not just its name.
- The promotion screen also names what a new skill does when it fits ("Gains: Celerity (Move +1)").

### Changed
- **Skills page help** (the stat screen's 4th page). A promotion row's help now gives each skill gained with what it does, wrapped over the help's three lines when they all fit, else one line each in the short form; then what the unit keeps and loses in the lines left. Every skill now has a plain-words description for the help boxes (a skill's own row, the "Also:" row and the promotion rows); the short form stays on the page's rows, which have room for a few words only. The promotion screen's two lines try "Gains: Name (what it does)" first and fall back to the names alone when that does not fit.

### Technical
- `port/reforged/reforged_skills.c`: `sLongHelps` (one per skill) and `Reforged_SkillShownLongHelp`; the promotion text tries `SegmentHelp` first. `port/reforged/reforged_skills_page.c`: `WrapN`, `HelpLine` (the plain words when they fit a line, else the short form), the promotion row's help as above. Display only: the game is the same (the identity routes, skills off, are unchanged; checked with Skills on in Chapter 16 on Eirika's page).

## v1.33.0 (2026-11-26)

### In short
- **Updates** (the Display screen's row, now **On by default**): when a newer version is out, the title screen says "vX.Y.Z is out: Select installs it". Press Select and the game downloads it, checks it, installs it and restarts; your saves and settings stay as they are.
- Nothing is installed unless you press Select, and a download that does not match GitHub's checksum is refused. Turn the row Off and the game never contacts GitHub.

### Changed
- **Updates** (the Display screen's "Update notice" row, renamed; On by default at the owner's choice, an exception to the rule that new options start Off; settings key `updates`: v1.31.0's `update_notice` is no longer read, so the new default reaches everyone). On Windows a newer release is offered on the title screen for as long as it is shown; Select (which the title screen itself does not use) downloads the release's Windows zip from the port's own GitHub releases, checks its SHA-256 against the digest GitHub lists for that file (no digest, or another: nothing is installed), writes its `fe8pc-sdl.exe` and `SDL3.dll` beside the running ones as `.new` (a failure there changes nothing), then swaps them in with four renames (the running ones become `.old`, deleted at the next start; any failed rename is undone) and, on the title screen, starts the game again with the same command line; the new copy waits for the old one to end before it opens your save. If you started a game while it downloaded, it is never cut short: the new version starts next time. Saves, settings, the ROM and the docs folder are not touched. Linux and other builds only name the new version, as in v1.31.0.

### Technical
- `port/reforged/reforged_update.c`: the release JSON's `-windows.zip` asset (its `browser_download_url` must start with `https://github.com/FT775/fe8-pc-port/releases/download/`, its `digest` must come before that URL in the same asset), no `..`, `%`, `?`, `#` or backslash in it), SHA-256 (FIPS 180-4) of the download, Select read from `gKeyStatusPtr` on the title screen (read only), the install on a thread of its own, the restart 1.5 s after "installed" and only on the title screen. New platform calls (`port/platform.h`): `Platform_CanInstallUpdates`, `Platform_UpdateTempDir`, `Platform_HttpsDownload`, `Platform_InstallUpdate`, `Platform_RestartGame`, `Platform_UpdateCleanup` (Windows: `port/win32/posix_win32.c`: System32's `curl.exe` run with no window; each file taken with `tar.exe -xOf` from the checked zip (tar checks the member's CRC; its exit code must be 0) and written by the game with every write checked, as `.new`; `MoveFile` renames; `CreateProcessA` with `FE8PC_UPDATED=<pid>`, and `PortWin32WaitForUpdater` in `port/hw/main_pc.c` waits up to 20 s for that process before the save's lock is taken; elsewhere none). A scripted run (`FE8PC_FRAMES` set) asks nobody. Test switches (PB-9): `FE8PC_TEST_UPDATE_JSON=path` (the release JSON from a file; a `file://` download URL is then allowed), `FE8PC_TEST_UPDATE_REAL=1` (ask GitHub in a scripted run). An independent review's five findings were fixed before release (the restarted copy finding the save locked, a restart cutting a game short, extracted files not checked, a swap that could leave a mixed pair, `..` in the URL). Checked on Windows in a scratch folder: a fake newer release installed end to end (the exe replaced by the zip's, `.old` files, the restart; with this build as the update, the restarted copy waited, took the save's lock and saved, deleted the `.old` files and offered nothing again), a wrong SHA-256 refused with the game untouched, a game started during the install left running ("starts next time"), and GitHub's real release JSON and zip (3,960,307 bytes, its SHA-256 as GitHub lists it).

## v1.32.0 (2026-11-26)

### In short
- New **Sound** row on the Display screen: **Enhanced** mixes the music and sound effects again on the PC at 4 times the GBA's rate, smoother and without the GBA's hiss; **Original** (the default) is the GBA's own sound.
- The same songs, instruments, reverb and balance: only the sound quality changes, never the game or your saves.

### New
- **Enhanced sound** (the Display screen's new **Sound** row, Original / Enhanced; Original by default; display-only: never part of a save or an option set). The GBA mixes its music into 8-bit samples at 13,379 Hz with straight-line interpolation, and its sound chip cuts the result to 9 or 10 bits: the hiss and the dull, slightly grainy top end of the original. With Enhanced, the port mixes the same notes a second time for the speaker: at 53,516 Hz, each instrument's samples interpolated with a smooth cubic curve, the sum kept whole (no 8-bit wrap-around), each note's volume moving smoothly through the frame instead of in steps, the Game Boy channels taken before the 10-bit cut; the game's reverb and stereo routing as on the GBA. It follows the game's own mix slot by slot, so it is always in time and silent when the game is. In the comparison check (Chapter 6's music and battles) it matches the original second by second (correlation 0.96 to 0.99, levels within about 1 dB) with a noise floor about 4 dB lower in quiet passages and the treble above 6.7 kHz the GBA cannot hold.

### Technical
- New `port/audio/hq_pc.c` (played builds, `PORT_STATE_ON`; `RFO_AUDIO_HQ`, key `enhanced_sound`, in `sDisplayOnly` and `IsDisplaySubmenu`; applied by `Reforged_ApplyDisplay` through `PortHq_SetRow`). The GBA's pipeline is untouched and always runs (m4a_1_pc.c's SoundMainRAM, fifo_pc.c's FIFO model, psg.c's PSG and DAC): every byte of GBA memory, the frames, the saves and the original stream are the same with the row on or off. `m4a_1_pc.c` calls `PortHq_Frame` after SoundMainRAM's reverb or clear of a slot and `PortHq_Channel` for each channel just before it is mixed (its wave, position `cp` + `fw`, step `divFreq * freq`, loop, `er` / `el`, a new note), read only; hq_pc.c mixes a shadow of that slot (896 float frames a lane) with 4-point Catmull-Rom interpolation at a quarter step, volumes ramped from the previous slot's, and the GBA's reverb formula on the shadow slots (values below 1e-6 flushed to 0, as the GBA's integer echo dies), the sample before the loop start taken from the wave's end once a channel has gone round its loop; a save state's load clears the shadow (`PortHq_Reset`). `psg.c` hands each PWM tick's PSG level before the DAC to a hook (`PortPsg_SetTickHook`; psg.c stays integer-only, as `port/test/audio/psg.mk`'s no-float check wants), which hq_pc.c spreads over 896 bins by the cycles each tick covers, and `PortHq_Render` routes the shadow lanes the FIFOs play this VBlank (`PortHq_LaneAt`, from the DMA's source as fifo_pc.c finds it) and the PSG by SOUNDCNT_H, in the DAC's units, with a soft knee for the DAC's clip, to s16 at 53,516 Hz. `audio_out.c` `PortAudioOut_PushRate` follows a change of rate (`Platform_SetAudioRate`: SDL `SDL_SetAudioStreamFormat`; the queue's marks in that rate's frames); SDL resamples to the device. Test switch `FE8PC_AUDIO_DUMP_HQ=prefix` (PB-9): the enhanced stream as `prefix.lr16`, mixed with the row on or off. New `port/tools/soundcheck.sh` (with `soundcheck.py`): the thirdtier route with Sound Original against Enhanced: the same frames, `.sav` and original stream; the enhanced stream against the original second by second (level, correlation, no clipping).

## v1.31.0 (2026-11-26)

### In short
- New **Update notice** row on the Display screen (Off by default): with it on, when a newer version of the port is out on GitHub, the title screen says so for a few seconds.
- It only asks GitHub which version is the newest, once per run; it never downloads or installs anything, and without a network it shows nothing.
- With the row off nothing changes.

### New
- **Update notice** (the Display screen's new row; Off by default; display-only: never part of a save or an option set). With it on, the game asks GitHub once per run, in the background, for the newest release of the public port: one HTTPS request to api.github.com through Windows' own curl (Windows 10 and later; the system's curl on Linux), run with no window, carrying nothing but the request and a fixed program name. If that release is newer than this one, the title screen shows "vX.Y.Z is out (GitHub releases)" for 8 seconds, once per run (if the answer comes after the title screen, on the next one). The game never waits for the answer; with no network, a firewall or no curl, nothing is shown. Nothing is downloaded or run.

### Technical
- New `port/reforged/reforged_update.c` (played builds; `RFO_UPDATE_NOTICE`, key `update_notice`, in `sDisplayOnly` and `IsDisplaySubmenu`; applied by `Reforged_ApplyDisplay`). New platform calls (`port/platform.h`): `Platform_RunDetached` (SDL: a detached SDL thread; headless: none) and `Platform_HttpsGet` (SDL: `curl` through `popen`; on Windows `port/win32/posix_win32.c` `PortWin32RunHidden`, `CreateProcessA` of System32's `curl.exe` with `CREATE_NO_WINDOW` and a pipe; headless: none). The build's version is `PORT_VERSION`, the first `## vX.Y.Z` heading of `docs/port/CHANGELOG.md` (`port/Makefile`; `reforged_update.o` is rebuilt when the changelog changes). The line is drawn by the save states' toast (`PortState_ToastText`; the toast text may now be 47 characters). `port/hw/frame.c` calls `Reforged_UpdateFrame` once per frame step (it only reads a finished answer, and the title screen with `Proc_Find`). Test switch `FE8PC_TEST_UPDATE_TAG=vX.Y.Z` (PB-9): no request, that tag is the answer.

## v1.30.0 (2026-11-26)

### In short
- With **Mouse** on, a click in the trade screen or a shop's Buy or Sell list now picks the item you clicked (until now it picked the one the hand was on), and the hand follows the pointer there too.
- With **Widescreen** on, a see-through glow that fills the screen no longer blacks the side bars for half a second (Chapter 16's warp sparkles): the sides keep the map, with the glow carried on into them.
- Both rows are still off by default, and with them off everything is exactly as in v1.29.0.

### Changed
- **Mouse: the trade screen and shops.** In the trade screen a click on an item (either unit's side) moves the hand there, Left / Right and Up / Down as with the pad, then A: the first click picks the item to give, the second the item or empty slot to swap with. In a shop's Buy or Sell list a click on an item moves the hand there (a click on the list's bottom row scrolls it one line, as Down does; A waits until the page has stopped), then A: the shopkeeper names the price. In both, moving the pointer over an item moves the hand, and a click beside the items does nothing. The shops' browse-only list (A or B closes it) keeps the old rule.
- **Widescreen: screen-wide glows.** An effect on BG2 that reaches the strip's edges and is the alpha blend's first target over the map (a glow, a flash, or an effect whose screen-wide layer is black where it shows nothing) no longer blacks the side bars: each side line takes the effect's pixel at the strip's own edge on that line, blended the same way. On the lordpromo route the last 29 black-bar frames on the map (the warp sparkles at 99931) now show the map. An opaque effect layer that reaches the edges still gives black bars.

### Technical
- New `Reforged_MouseGrid` (`port/include/port_reforged.h`, `struct ReforgedMouseGrid`: up to two columns of item rows in game-screen pixels, the rows on screen and the first one's index, the hand, Up/Down going round, A not taken yet), with host-copy hooks under `#if defined(PC_PORT) && PORT_PLAYED_BUILD` in `src/bmtrade.c` `TradeMenu_OnLoopUnselected` / `_OnLoopSelected` (before `TradeMenu_UpdateSelection`; the cells from `gTradeItemDisplayTileLocation` and `hasItem`, the empty slot of the selected state included) and `src/bmshop.c` `Shop_Loop_BuyKeyHandler` / `_SellKeyHandler` (before `Shop_TryMoveHandPage`; rows from `gShopState`, busy while `IsShopPageScrolling` or the help box). `port/reforged/reforged_mouse.c`: screen kind "list", plan `MPLAN_GRID` (the column first, then the shorter way round or straight, A when there and not busy), hover, `GridHitTest` in the self-test. The GBA build and the test builds are unchanged. New mousecheck scenario `trade` (`port/test/inputs/mouse/trade_e16.mouse`, with its KEYREC replay); the shop lists were checked on `route_e11.keys`'s world-map armory (not a standing check: mousecheck's routes are Chapter 16's).
- `port/render/wide.c`: `Bg2Blended`, `sBg2Edge`, `SideBg2Edge`; `wide.log` flag 32; `widecheck.py` COVER item `edge`; new widecheck route `e16glow` (lordpromo 99880-100000, must cover `edge`).

## v1.29.0 (2026-11-24)

### In short
- New **Mouse** row on the Controls screen (Off by default): play with the mouse. Click a unit to select it, click a square to move there, click Attack, Wait or any menu item, and click a target twice (once to see the forecast, once to attack).
- Right-click goes back, middle-click shows a unit's stats or help, and the wheel moves through menus, lists and targets.
- The mouse only presses the buttons a gamepad would, so nothing about the game changes, and with the row off everything is exactly as in v1.28.0.

### New
- **Mouse controls** (the Controls screen's new **Mouse** row; Off by default). Left-click on the free map: a unit's square moves the cursor there and selects it (A); an empty square only moves the cursor, and a second click on it opens the map menu. Choosing the destination: a click moves the cursor there and confirms (A). While another unit's range is shown (an enemy's, or the danger zone), a click first closes it (B) and then acts as on the free map, so one click on your unit selects it. Menus (the unit's actions, the weapon list, the map menu, any game menu with a hand and up to 9 items): the hand follows the pointer, and a click on an item chooses it. Target selection: a click on a target makes it the chosen one (its forecast shows), a second click on the chosen target confirms (also when the camera had to scroll to the target). A double-click on a unit, a square or a menu item counts once (a double-clicked unit is selected, not selected and then told to stay). On the Controls screen a click is A only on the row the hand is on (elsewhere it does nothing, so a stray click neither switches the Mouse row off nor starts waiting for a key). Any other screen: a click inside the game's picture is A. Right-click is B everywhere, middle-click is R (on the map: the cursor to the square, then R), the wheel is Up / Down (a sideways wheel Left / Right), the side buttons do nothing. The cursor travels square by square and the hand item by item, exactly as with the pad; any key or gamepad button stops what the mouse was doing. A click that brings the window to the front is ignored. Works in every window size and in full screen, with Pixel, Fill and Stretch scaling and with Widescreen (a click in a side bar that shows the map picks that square). Not yet: pointing in the trade screen, the preparations screens, the world map, shops, the convoy and Reforged Options' own screens (there a click is A on whatever the hand is on, not on what the pointer is over: in the trade screen and shops move the hand with the wheel first; right-click B, the wheel Up / Down), dragging, camera scrolling without the cursor, mouse buttons on the Controls screen.

### Technical
- New `port/reforged/reforged_mouse.c` (played builds only; `RFO_MOUSE`, settings key `mouse_controls`, in `sDisplayOnly`, so it never marks a save, and in `IsDisplaySubmenu`, so it is never part of an option set). The mouse never writes game memory: a planner turns mouse events into GBA key presses (D-pad taps, A, B, R; press and release alternate, so each is a new press) which `port/sdl3/platform_sdl3.c` `Platform_ReadKeys` ORs into the live keys where the keyboard's and gamepads' go (`Reforged_MouseKeys`), so a mouse session is a key sequence a pad could make. Its clock is the game's own key reads (`Reforged_MouseKeyRead` in `src/hardware.c` `UpdateKeyStatus`); a mask is held until a read consumed it; no wall clock. To decide what to press it reads game state read-only (`gBmSt`, `gBmMapUnit`) and the host copies four hooks take: `src/playerphase.c` `PlayerPhase_MainIdle` / `PlayerPhase_RangeDisplayIdle` (`Reforged_MouseMapIdle`), `src/uimenu.c` `Menu_OnIdle` (`Reforged_MouseMenu`: the box, the items' rows, the hand), `src/uiselecttarget.c` `TargetSelection_Loop` (`Reforged_MouseTargets`: the targets' squares; a target list counts as the same one until another screen's hook runs or its squares change, so the camera holding the loop while it scrolls to a target does not start a new one). All under `#if defined(PC_PORT) && PORT_PLAYED_BUILD`: the GBA build and the test builds are unchanged. The Controls screen (port code) reports its hand's row (`Reforged_MouseHandRow`). The double-click rule counts the game's key reads (a second left click within 30 reads and 4 pixels of a first that chose something is dropped), never the clock. A plan waits (up to 120 reads) while no input screen's hook runs, and is dropped when another screen's hook runs instead, when a tap changes nothing, when it runs too long, at any key or button, at a right-click and at a save state's load (`Platform_ReleaseKeys` -> `Reforged_MouseReset`). Session data (`PORT_STATE_SESSION`). With the row off the hooks return at once and no mouse event is used.
- Coordinates: `SDL_ConvertEventToRenderCoordinates` gives the pixel of the picture shown for any window size, scaling mode, full screen and display scaling; Widescreen subtracts `gRenderWideOff` (the 240 strip's x in the 284 picture); a map square is taken with the camera of the picture clicked (a copy one frame behind `gBmSt.camera`), in the strip or in a side bar that shows the map (`gRenderWideMap`).
- Test switches (played window builds, PB-9): `FE8PC_TEST_MOUSE=FILE` (mouse events put into SDL's queue at given frame steps: move, down, up, click, wheel, focus, and the window's size and full screen; window pixels, or picture pixels with `FE8PC_TEST_MOUSE_PICTURE=1`), `FE8PC_MOUSE_LOG=1` (events, the screen each met, the keys pressed, screen changes with the cursor, menu items and targets), `FE8PC_MOUSE_SELFTEST=1|exit` (the pure helpers). Any build: `FE8PC_KEYREC=PATH` writes the keys each OnMain read as a key file (`FE8PC_KEYFILE`'s format).
- New `port/tools/mousecheck.sh` (with `mousecheck.py` and `port/test/inputs/mouse/*.mouse`): scripted mouse sessions on Chapter 16 run offscreen (select a unit, move it, hover and click Attack and a weapon, the wheel in the target list, a target clicked twice, the battle and the target's HP, End; the wheel on the map and in the map menu, menu hover, right-click cancels, middle-click R, a side button, the focus click, a key cutting a plan short; a target at the screen's edge (the camera scrolls) still taking two clicks; a click with an enemy's range shown selecting the unit clicked; a double-clicked unit selected once; a click in a Widescreen side bar; a click under Pixel, Fill and Stretch in a 1200x500 window and in full screen), each log checked in order, and each session's recorded keys replayed with the row off and no mouse: the same frames and `.sav`; and Chapter 16's route with the row on and no mouse event against the row off.

## v1.28.0 (2026-11-24)

### In short
- With **Widescreen** on, battle animations and the level-up screen are now centred; near a map edge the picture glides to the middle as the battle starts and back as it ends, instead of sitting off to one side. A battle drawn over the map keeps the map in the side bars.
- The side bars no longer blink black for a second when a staff, item or spell sparkles on the map, or when choosing a staff's target near the bottom of the map, and the phase banner's band and square wipe now run across them.
- With the row off nothing changes.

### New
- **Widescreen: the phase banner in the side bars** (step 4, polish). The banner's red band opens and closes across the whole 284-pixel picture, and its square wipe sweeps through the bars at the same step as the middle.

### Changed
- **Widescreen: battle screens centred.** A battle animation (and its level-up screen) is drawn over a suspended map; until v1.27.0 it kept the map's position, so near a map edge the battle sat up to 22 pixels off centre with a 44-pixel bar on one side and none on the other. It is now in the middle. When the battle is drawn over the map itself, the bars keep showing the map beside it (black where the map ends) whenever nothing else is in the way, through the opening effect, the battle, the level-up screen and the closing effect; a battle with its own background has 22-pixel black bars. Between the map and a battle over it the strip slides 3 pixels a frame with the map in the sides, so the map never jumps.
- **Widescreen: map effects.** An effect on BG2 that stays inside the middle (a staff's or item's sparkle, a map animation's spell) no longer blacks the side bars: they keep showing the map. An effect that fills the screen's height at an edge (a flash) still blacks them. An effect layer that can hardly be seen (the first step of a fade, at 1/16) or that shows nothing in the middle (also on a 512-pixel-wide layer, as the level-up screen's) no longer blacks them either. A tile change's cross-fade (a door or wall turning) shows the new map in the bars at once instead of blacking them. On the lordpromo route the black-bar blinks on the map went from 82 to 1 (a full-screen glow).
- **Widescreen: camera past the map's edge.** Choosing a staff's target near the bottom of the map scrolls the camera 4 pixels past the map so the help bar does not hide the last row; until v1.27.0 that made the picture jump 22 pixels to the middle with black bars and back. The bars now keep the map (black beyond its edge), for a camera up to a square past any edge.
- **Widescreen: attack squares** in the bars take the shade of the nearest attack square on the same row in the middle, so during the range's opening animation they grow in (or are full) like their neighbours.
- **Weather** (snow, rain, sandstorm, flames, clouds): no chapter of the game sets one (only the developers' debug menu can), so particles are not drawn in the bars; should one ever be set, the map is shown with black bars rather than particles that stop at the middle's edges.

### Technical
- `port/render/wide.c`: frames are classed map / map with BG2 in the way / suspended map (`gPortWide.suspended` with BG3 still the map) / other; the strip's x moves at most `WIDE_SLIDE` (3) pixels a frame between the map and a suspended map (the latch's 32x16 squares cover any x from 0 to 44), at once to and from any other screen; a suspended map's frames show the map in the sides whenever nothing else is in the way, as map frames do. `CheckSquare`: squares outside the map (a camera up to 16 pixels past an edge, which `Latch` now accepts, `off` clamped to 0..44) are skipped by the VRAM checks. `BgEmpty` and `BgEdgesClear` read text BGs of any size (`TextEntry`); `Bg2Faint`: BG2 as the alpha blend's first target at EVA 1 or 0 over BG3 and the backdrop, no window, is left out of the sides. `BgEdgesClear`: BG2 content with opaque pixels on at most 48 lines of the strip's outer 8 columns, on sides the picture has, is left out of the sides; `RingMostly`: BG2 matching the latched words on all but at most 16 squares (a tile change's old map) is left out too. `PhaseSquares`: BG1 unscrolled holding all 15x10 squares of `PhaseIntroSquares_InLoop`/`_OutLoop` at one step is matched and the side squares are drawn by the same rule, on lines where BG1 is the alpha blend's first target (on the banner's first frame `PhaseIntroVMatchMid`'s BLDCNT is still in effect above line 96, which drew a frame of opaque side squares). Side sprites may be affine (`obj_line.c`'s rule, parameters read from OAM). Attack squares use the per-row nearest shade (`sRowShade`), else the majority.
- `port/reforged/reforged_wide.c`: `PortWide_PushPhaseBand` pushes 8x32 affine copies of the band's outer tile columns (OBJ VRAM is 2D-mapped) across both sides; `PortWide_NoteMu` counts moving units pushed / hidden by the fog at a side; `PortWide_PushObjEx` and `PortWide_PutStandingMuSprite` return whether the push was kept; `PORT_WIDE_NO_WEATHER` for particle weathers. New guarded hook (`#if defined(PC_PORT) && PORT_PLAYED_BUILD`) in `src/phasechangefx.c` `PhaseIntroUnk_Loop`; `src/mu.c` reports its side pushes. The GBA build and the test builds are unchanged.
- `wide.log` lines go on with "objs mus fogs hps flags"; `FE8PC_WIDE_LOG=1` also logs moving units, fog-hidden enemies and HP bars in the sides. `port/tools/widecheck.py` checks that the strip only slides between the map and a battle and is centred for the battle, prints a COVER line and takes `--expect`; `widecheck.sh` routes name what they must cover, new routes `e16battle` (the first battle at the map's right edge), `e16late` (walking units across the seam at the left edge, hurt units' HP bars in a side, a tile change, the phase banner), `e16heal` (a staff's effect left out of the sides) and `e11fog` (Eirika's Chapter 11, fog: walking enemies, one hidden at a side; needs `$TESTDATA/saves/e11_start.sav`, skipped without it).

## v1.27.0 (2026-11-23)

### In short
- With **Widescreen** on, the blue move squares, the red attack squares and the danger zone now carry on into the side bars instead of stopping at the usual screen's edges.
- The movement arrow, a unit walking in or out of a side bar, and the HP bars and "!" warnings of units standing there now show in the side bars too.
- The phase banner and weather still only cover the middle; everything else is as in v1.26.0, and with the row off nothing changes.

### New
- **Widescreen: the player and enemy phases in the side bars** (step 3 of the Widescreen row). On the battle map the 22-pixel bars now also show the move and attack ranges and the Reforged danger zone (Select), with the same colours, the same grid and the same opening animation as in the middle; the movement arrow where it runs into a bar; a unit moving from or into a bar while it walks (enemy and player units; an enemy hidden by fog stays hidden; a unit wholly inside a bar keeps the walking frame it last showed in the middle, because the game does not animate units it does not draw); and the Reforged map HP bars and "!" warnings of the units standing in a bar. Still only in the middle 240x160 strip (a later step): the phase banner's text and square wipe, weather, and spell effects drawn on the map. Display only: the game, its saves and the 240x160 picture are the same with the row on or off.

### Technical
- `port/reforged/reforged_wide.c`: while `BM_FLAG_0` is set the VBlank latch keeps each of its 32x16 squares' range class by `DisplayMovementViewTile`'s rule from `gMapMovementSigned` / `gMapRangeSigned`, inside the map only (`gPortWide.rangeClass`); `PortWide_PushObjEx` (attr0 bits, a dying unit's blend), `PortWide_ReachesSide`, and `PortWide_PushHpBar` (an HP bar picture with its sprite's push, matched to its owner at the latch). `port/render/wide.c`: with the range layer up, every square on screen in BG2's ring must be of the class the maps give (movement `0x4280`.., attack `0x5280`.. or `0x5284`.., none 0: the class, not the exact words, since an attack square's words depend on what the square held before, design 2.3); then BG2 is drawn in the sides from the classes (`0x4280` words for a movement square; for an attack square the `0x5280` or `0x5284` words, whichever most of the strip's attack squares have, since the two differ only during the opening animation, where `0x5280` grows in and `0x5284` is full at once; through the VRAM tiles and palettes, so the opening animation and the colour cycling follow the middle); on a mismatch the sides show the map without the squares (logged with `FE8PC_WIDE_LOG=1`). Side sprites now record which push won each pixel, and the HP bar pictures are drawn with obj_line.c's `OvlDraw` rule.
- New guarded hooks (`#if defined(PC_PORT) && PORT_PLAYED_BUILD`; the GBA build and the test builds are unchanged): `src/bmpatharrowdisp.c` `DrawPathArrow` pushes arrow squares that reach a side before its 240-pixel cull; `src/mu.c` `PutMu` / `PutMuSMS` push a moving unit that reaches a side before `GetMuDisplayPosition`'s cull: the AP handle's current frame object list and tile base (what `AP_Display` would push), with `PutMu`'s fog and death-fade tests, never running `AP_Update`/`AP_ExecFrame`, never calling `SyncStandingMuFacing`, never writing OAM; `src/bmudisp.c` `PortWide_PutStandingMuSprite` (`PutStandingMuSprite`'s push without the cull). A moving unit the game culls whole (past `GetMuDisplayPosition`'s cull, or, for `PutMuSMS`, past what `PutStandingMuSprite` draws with the 9-bit x and 8-bit y it is handed) is kept only if none of its sprite falls on the 240 strip (`PortWide_PushObjEx`'s `sideOnly`, by the sprite's own size), so no half unit shows at a seam; such a unit off the map's squares is skipped, so the fog map is read only where `PutMu` itself reads it or inside the map; a standing unit in the death fade is drawn in neither the middle nor the sides. `port/reforged/reforged_hpbars.c` gives units whose square reaches a side their picture for the sides (the middle's list, counts and log lines are unchanged).
- `port/tools/widecheck.sh`: routes take extra settings lines; new route `e16wide` (`port/test/inputs/features/wide_e16.ks` / `.keys`, hand-made, PC only: Chapter 16's first player phase with the camera at the map's left edge, a range, the arrow and a move across the seam, the danger zone, End, the first enemy phase; HP bars and the danger zone on), and route `e16bars` (the lordpromo route's frames 65,000-66,000 with HP bars on: hurt units' bars across the left seam, staff target squares).

## v1.26.0 (2026-11-23)

### In short
- With **Widescreen** on, the battle map now shows more of the map in the two side bars: more terrain on both sides, wherever the map goes on.
- The units standing there show too, with their status, rescue and boss icons, so you see who waits just off the usual screen.
- Move and attack ranges, the danger zone, units walking in the sides and the HP bars there come in the next version; every other screen keeps its black bars.

### New
- **Widescreen on the battle map** (step 2 of the Widescreen row; Off by default). On the battle map the 22-pixel bars of the 284x160 picture show the map's terrain (fog included) and the standing units and their icons (poison, sleep, silence, berserk, stat boosts, rescue, boss and protect markers, the goal square's marker), with the same fades, dimming and screen shakes as the middle; as a battle starts the dimmed map stays in the bars until the battle screen is drawn, and the strip does not jump. Menus, windows and the cursor stay in the game's usual 240x160 strip. Near a map edge the open side shows more map instead of black, so the strip moves up to 22 pixels off the middle while the camera pans there; a map narrower than the picture is shown centred with black at its ends. Every screen that is not the plain battle map (battle animations, event pictures, menus over the whole screen, the world map, the chapter intro) keeps the black bars, as in v1.25.0. Not yet in the sides (the next version): the move and attack ranges and the danger zone (their squares stop at the strip's edges), the movement arrow, units while they walk, the HP bars and weather. Display only: the game, its saves and the 240x160 picture are the same with the row on or off.

### Technical
- `src/bm.c` `OnVBlank` calls `PortWide_VBlankLatch` (beside `Reforged_MapHpBarsVBlank`); `src/bmudisp.c` calls `PortWide_PushObj` before each 240-pixel cull of the standing sprites and their icons; `src/bmmap.c` `PortWide_BmTileWords` gives a map square's four BG3 words by `DisplayBmTile`'s rule, inside the map only. All three under `#if defined(PC_PORT) && PORT_PLAYED_BUILD`: the GBA build and the test builds are unchanged. `port/reforged/reforged_wide.c` keeps the side sprites and, at each VBlank, the camera, the BG3 ring origin and the words of 32x16 squares around it (`gPortWide`, session data). `port/render/wide.c` (played builds: `PORT_RFO_WIDE` for `render.o` and `wide.o`) checks at the start of each frame that BG3 in VRAM still holds that map at that origin and that BG2 is off, shows nothing at its own scroll, the map (the tile change cross-fade) or the range layer (a map suspended by `BMapDispSuspend` only continues a frame that showed it, and otherwise keeps the strip's x), then draws the sides line by line through the same compositor as the middle (blend, brightness, windows reaching x 0 or 240 extended); no GBA memory is written.
- `FE8PC_DUMP_WIDE`'s folder also gets `wide.log` (frame, the strip's x, whether the sides show the map, why not); `FE8PC_WIDE_LOG=1` logs each change. New `port/tools/widecheck.sh` (with `widecheck.py`): the identity routes with the row off and on, 240x160 frames, `.sav` and `.reforged` identical, each 284x160 picture holding the 240x160 frame at its strip's x, and how many frames show the map in the sides; `--sheets DIR` writes PNG contact sheets.

## v1.25.0 (2026-11-23)

### In short
- New **Widescreen** row on the Display screen (Off by default): a 16:9 window with the game centred and black bars at the sides, never stretched.
- This first step only changes the window's shape: more of the battle map in the side bars comes in a later version.
- With the row off, everything is exactly as in v1.24.1.

### New
- **Widescreen** (Display screen and the in-chapter Reforged screen's Display rows; Off by default; `widescreen` in the settings file). With it on the window is 284x160 times the window size (16:9 within 0.2%), and every screen, the battle map included, is the usual 240x160 picture in the middle with 22-pixel black bars on each side. Window size, Pixel/Fill/Stretch scaling, Smoothing and fullscreen work as before in the new shape; switching it shows at once. The save-state notes appear in the corner of the game's picture, not of the bars. Display only: it never marks a save, and the game, its saves and the 240x160 picture are the same with it on or off.

### Technical
- `port/render/render.h` `RENDER_WIDE_W` (284, one constant) and `RenderSetWide`; `render.c` keeps the 240x160 frame, its hash and `FE8PC_DUMP_FRAMES` as they were and, with the row on, copies each finished frame into the middle of `gRenderWide555` / `gRenderWideRgba` (session data, `PORT_STATE_SESSION`), which `port/hw/frame.c` shows instead. `port/platform.h` `Platform_SetWide` (SDL: the picture width replaces the fixed 240 in the texture, logical presentation, window and minimum size, scale choice and the frame size check; headless: nothing). `PortState_ToastRgba` takes the picture's stride and the 240 strip's x. With the row off none of this runs: the same buffers and the same present path as v1.24.1.
- New test switch `FE8PC_DUMP_WIDE=dir` (with the row on and `FE8PC_DUMP_FRAMES`): the same frames' 284x160 pictures as PPM files in `dir`, named as the 240x160 dumps, which it never changes.

## v1.24.1 (2026-11-20)

### Changed
- **Save states is on by default** (the owner: "make the default option for the savestates working to be ON"; an exception to the rule that new options start off). A settings file without the `save_states` line (every file from before v1.24.1 that never turned the row on) gets it on; `save_states = 0` keeps it off.
- **A save is marked only after a state was loaded.** With the row on by default, v1.24.0's rule (every save written while the row is on is marked) would have marked everyone's saves for nothing. Now the saves and suspends a game writes are marked `save_states=1` only once a state has been **loaded** in that game (loading is what changes play); the mark goes on through that game's later saves and suspends (resuming or loading a marked save and saving again keeps it, a rewind keeps it), and a state saved in a game that was already marked stays marked. Saving a state alone marks nothing; a new game, or loading a save that was never marked, starts unmarked. A save v1.24.0 marked stays marked. Nuzlocke is unchanged: Ctrl+S and Ctrl+L show "Not in Nuzlocke".
- **A hint with the row off** (the owner: "Yes, add the hint"): Ctrl+S or Ctrl+L (either Ctrl) shows "Save states are off (Helpers)" in the corner of the window for a moment, once per press (not again while held), and nothing else: the keys reach their bindings exactly as in v1.24.0 with the row off (a GBA button bound to S or L is still pressed).
- With the row on and Ctrl+S / Ctrl+L never pressed, the game, its saves (now without a mark) and every frame are the same as v1.24.0 with the row off and v1.23.0.

### Technical
- `reforged_settings.c` `RFO_DEFAULTS`: `[RFO_SAVE_STATES] = 1`. `reforged_savemark.c`: `sStateLoaded` (session data, `PORT_STATE_SESSION`), set by `Reforged_SaveMarkStateLoaded(1)` after a successful load (`reforged_savestate.c` `StLoad`), cleared at New Game (`Reforged_GameFixesNewGame`), written into a block as its `save_states` value (`Reforged_SaveBlockWritten`) and taken back from the block read (`Reforged_SaveBlockRead`; a rewind's own restart, `Reforged_RewindRestarting`, keeps a load made since). For `RFO_SAVE_STATES` the mark is the stored value 1, not "differs from the default" (`ValueMarked`), so the file format is v1.24.0's. Because the flag is session data, a load sets it after the state is put back and `FE8PC_STATE_CHECK`'s digest is unchanged.
- `PortState_OffHint` (`reforged_savestate.c`), from `platform_sdl3.c` `StateKeys`: on the press of Ctrl+S or Ctrl+L with the row off (edge-triggered per combination), the toast only; the keyboard the bindings see is not changed. The toast is drawn on the window's picture only, as the others (in dumped frames only with `FE8PC_TEST_STATE_TOAST_DUMP=1`).

## v1.24.0 (2026-11-14)

### New
- **Save states** (a new Helpers row, the owner's request: "can we add a save state option? ctrl+S = save, ctrl+L = load"; Off by default, one slot). With the row on, **Ctrl+S** (either Ctrl) saves the game exactly as it is at that moment and **Ctrl+L** puts that moment back, anywhere: on the map, in a battle animation, in a menu, during an event, on the world map, in the preparations, in a randomized game with FE7/FE6 classes. A small note in the top-left corner of the window says "State saved", "State loaded", "No state", "Refused (other version)", "Refused (other ROMs)", "Refused (damaged)" or "Not in Nuzlocke" for a moment (on the window only, never in the game's picture). The state is one file beside the save, `FE8.sav.state` (under 1 MB), written safely (a temporary file, then a rename).
- Your save is never touched: loading a state does not change the `.sav`, its slots or its suspended chapter, in the file or in memory. Saving, suspending and resuming work as usual after a load.
- While Ctrl is held, S and L press no button you bound to them. After a load every key you still hold is ignored until you let go of it, Space included: fast-forward stops at the load and starts again when you press it again (holding it does not stop Ctrl+S or Ctrl+L from working). Gamepads count again once nothing on any of them is held. The sound goes on from the loaded moment with a short fade, without a click.
- Loading a state keeps your options and controls as they are now (they are not part of a state). A change you made in a Reforged screen is written to the settings file at the load, so it is kept even though the loaded moment never left that screen; a Reforged screen open in the loaded moment shows the values it had when the state was saved until you leave it and open it again.
- Like Rewind, a save the game writes while the row is on is marked (`save_states=1` in the `.reforged` file), whether or not a state was used. In a Nuzlocke run Ctrl+S and Ctrl+L do nothing but show "Not in Nuzlocke".
- A state loads only in the same version of the game with the same FE7/FE6 ROMs: a state from another version (states do not survive an update), another set of ROMs or a missing ROM, or a damaged file, is refused and nothing changes. With the row off nothing changes at all.

### Technical
- A state is a photo of the whole game at one program point: `port/hw/frame.c` `VBlankIntrWait` with `gBmSt.main_loop_ended` set (the end of `OnMain`, `MainUpdate_8055C68` or `InBattleMainRoutine`, each of which returns right after it; the Gate 0 harness's sample point, `port/test/README.md` "Save states"). A key press only asks; the save or load happens at the next such point. The photo holds EWRAM, IWRAM, the I/O page, palette, VRAM and OAM (not SRAM), and every byte of the host `.data` and `.bss` of the game's and the port's own objects, between the new marker objects `port/statemark/begin.o` and `end.o`, which every played build links first and last (Windows: a second pair for MinGW's sorted `.data$*` sections). `port/tools/check_state_ranges.py` checks every link map: all own `.data`/`.bss` inside, nothing of the C runtime or the libraries (glibc's copy relocations, crtbegin, libgcc, mingw-w64, the SDL delay-load thunks) inside. Everything is restored except what a file names with `PORT_STATE_SESSION` (`port/include/port_state.h`): the settings and controls, the save file's and the save mark's records, the window, renderer and frame buffers, the audio device and queue, the pacer and fast-forward, input, logs and test switches, the FE7/FE6 loader's record and the FE6 copy, the DMA source and destination registers (write-only on the GBA; DMA0 and DMA3 may hold a host address of this process; DMA1 and DMA2 hold the sound buffer and FIFO addresses, which `port/audio/fifo_pc.c` reads every VBlank and which m4a's start-up sets to the same GBA addresses in every process). A new global is in every state unless it says otherwise.
- No game state may point into the heap: the FE6 ROM copy (`sFe6Rom`) and the FE7/FE6 animation pool's packed scripts and mode tables (`sKeep`) moved from the heap to static `.bss` buffers (reforged_fe7.c), at fixed addresses like everything else in the exe (no PIE on Linux, a fixed image base on Windows). Placed in `.bss` by name, so the Windows exe does not store them (MinGW keeps `-fdata-sections` zero data in the file).
- The file: "FE8STATE", format 1, the exe's SHA-1 (its build id), the FE8, FE7 and FE6 ROMs' SHA-1, the save's name, the photo's frame, chapter and turn, the parts' addresses and sizes, a CRC-32, and per part a bitmap of its non-zero 256-byte chunks with those chunks (session data written as zeros). Everything is read and checked before anything is put back. Rewind's kept turns come back with a state; the save mark's per-slot records stay (they mirror the `.sav`).
- After a load: the save file's retry after a failed write counts frame steps itself (`savefile_pc.c` `sRetryLeft`), so a load to an earlier frame no longer holds the next `.sav` write back until the frame counter climbs to where it was; the settings file is written if it differs from the options in memory (`Reforged_SettingsAfterStateLoad`); a state file cut short inside its header is refused as damaged, not as another version; fast-forward's release log line skips its frame count when a load came in between.
- New test switches (PB-9; each acts only with the row on, but `FE8PC_TEST_HOLD_KEYS`): `FE8PC_TEST_STATE_SAVE_AT`, `FE8PC_TEST_STATE_LOAD_AT`, `FE8PC_TEST_STATE_DUMP_DIR`, `FE8PC_TEST_STATE_TOAST_DUMP`, `FE8PC_TEST_STATE_EXIT_AFTER`, and two self-tests: `FE8PC_STATE_CHECK=1` (the program point after a load hashed against the one after the save: "0 of N chunks differ") and `FE8PC_STATE_CHECK_FILE=PATH` (a state compared with one another process saved at the same frame: "0 of N chunks differ", so nothing of a process's own is in it). `FE8PC_TEST_HOLD_KEYS` holds keys for the Ctrl+S / Ctrl+L path itself (window builds; it presses keys with the row on or off).

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
