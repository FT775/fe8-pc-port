A PC port of **Fire Emblem: The Sacred Stones** (GBA, USA), built from the FireEmblemUniverse `fireemblem8u` decompilation.

**No ROM is included.** You need your own copy of the game: the USA `.gba`, SHA-1 `c25b145e37456171ada4b0d440bf88a19f4d509f`. The game refuses other versions, hacks and patched copies.

### Windows: how to play
1. Unzip `fe8-pc-port-{VERSION}-windows.zip` into a folder of your own (not inside the zip, Program Files or a cloud-sync folder).
2. Put your `.gba` file in the `roms` folder (or next to `fe8pc-sdl.exe`). If the game finds no ROM, it asks you to pick the file and remembers where it is; the ROM is never copied.
3. Run `fe8pc-sdl.exe`. The exe isn't signed, so Windows may warn you the first time. Choose "More info", then "Run anyway".

Updating: replace `fe8pc-sdl.exe` and `SDL3.dll`. Your saves and settings carry over (keep the small `.reforged` file next to your `.sav`).

### What's new in {VERSION}
{WHATSNEW}

Everything since v1.1 (save states, skills, Pair Up, FE7/FE6 classes with your own ROMs, the randomizer's additions, Third promotion, and more): [CHANGELOG.md](https://github.com/FT775/fe8-pc-port/blob/main/docs/port/CHANGELOG.md). Guide: [PLAYING.md](https://github.com/FT775/fe8-pc-port/blob/main/docs/port/PLAYING.md). Fixes: [PATCH-NOTES.md](https://github.com/FT775/fe8-pc-port/blob/main/docs/port/PATCH-NOTES.md).

How it was tested: [ACCURACY.md](https://github.com/FT775/fe8-pc-port/blob/main/docs/port/ACCURACY.md). Credits: [CREDITS.md](https://github.com/FT775/fe8-pc-port/blob/main/docs/port/CREDITS.md). Found a bug? Open an issue and attach `fe8pc-sdl.log`.
