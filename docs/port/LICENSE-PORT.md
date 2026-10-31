# Licence of the PC port's own work (DRAFT)

> The licence text below is in the git tree as the root `LICENSE`, with `LICENSE-PORT-FILES.txt` beside it. The owner chose MIT for the port's own code on 2026-10-28 and, on 2026-10-30, the holder, the scope over the port's own changes to the decompilation's files ("to the extent separable") and the form: **an "MIT License (scoped)" at the repository root, with the scope written into the licence terms**, the form of pokeemerald-expansion-native's licence that is quoted in `port/audio/m4a_1_pc.c:14` (its scope condition at `:69`). GitHub will show it as "Other", not "MIT", as it does for that project. This is not legal advice.

## What this licence covers

1. **The files in the `port/` folder** of the source tree, **except** the files below that carry their own notice.
2. **The port's own changes to the decompilation's files**, to the extent those changes are separable from the decompiled code. They are mostly the PC-only blocks under `PC_PORT` next to the original lines. The files are listed, marked `M`, in **`LICENSE-PORT-FILES.txt`** (beside `LICENSE` at the root): on the release commit, 338 files, 328 `M` and 10 `A`, made with `git diff --name-status --diff-filter=AM f2e1f060 1a411bc0 -- . ':!port'` (`f2e1f060` is the FEU merge base). The original text of those files stays the decompilation's.
3. **The files the port adds outside `port/`**, marked `A` in the same list: the root `LICENSE`, `LICENSE-PORT-FILES.txt` itself and the eight documents in `docs/port/`. The port also replaces the root `README.md` with its own (`M`), and `--diff-filter=DRCT` shows that nothing outside `port/` is deleted, renamed or copied. The list made from the release commit has 338 files, 328 `M` and 10 `A`. The decompilation's own README moves to `README-decomp.md`. Its text is the decompilation's, so it is left off the list (the command excludes it by name) and this licence does not cover it.

Files in `port/` with their own notice, which stays with them:

| File | Its notice |
|---|---|
| `port/audio/m4a_1_pc.c`, `port/audio/psg.c`, `port/audio/psg.h` | pokeemerald-expansion-native's scoped MIT licence, quoted verbatim in each file's header. |
| `port/test/bios/suite_vectors.h` | MIT, "Copyright (c) 2015 Jeffrey Pfau" (mgba-emu/suite), full text in the header. `port/test/bios/test_bios_math.c` uses these values and points to that notice. |

Two files in `port/` follow the decompilation's own code closely, so the licence covers them only to the extent they are separable from it, as the precedent does for its translations: `port/asm/arm_pc.c` (the C versions of the decompilation's `asm/arm.s` and `asm/arm_call.s`) and `port/asm/ramfunc_pc.c` (which replaces `src/ramfunc.c`).

## What it does not cover

The port claims no rights over any of these. They keep their own terms:

- **The game.** Nintendo's and Intelligent Systems' code, graphics, sound and text, all rights reserved. Players supply their own ROM. No game material is distributed (D1, source only).
- **The decompilation.** The FireEmblemUniverse `fireemblem8u` decompilation and its contributors' work: everything outside `port/` (`src/`, `include/`, `asm/`, `data/`, `graphics/`, `sound/`, `scripts/`, `tools/`, the top-level `Makefile` and the other build files), apart from the port's separable changes and the files it adds, listed in `LICENSE-PORT-FILES.txt`. It has no licence statement (U1). `include/gba_sprites.h` keeps its libgba LGPL-2.0-or-later notice (D4). The decomp's tools keep their own MIT, BSD-style and Boost notices.
- **Third-party software** the builds use or ship beside the exe: SDL3 (zlib; `LICENSE-SDL3.txt` goes beside `SDL3.dll`), the mingw-w64 runtime and the GCC runtime in a Windows exe, the C library, and the build- and test-only tools (agbcc, the compiler of the GBA build that checks the source; mGBA, Unicorn), none of which ship in a game build.

## The licence text (the root `LICENSE` file)

The MIT terms with the scope written in, in the form of the precedent above ("the Software" becomes "the Port", and the scope statement is one of the things every copy must keep). Holder: the owner's choice of 2026-10-30 (handle + contributors).

```text
MIT License (scoped)

Copyright (c) 2026 somecallmetim_ and the FE8 PC port contributors

Scope. This license covers only the work this project adds on top of the
fireemblem8u decompilation of Fire Emblem: The Sacred Stones (the "Port"):

- the files in port/, except port/audio/m4a_1_pc.c, port/audio/psg.c,
  port/audio/psg.h and port/test/bios/suite_vectors.h, which carry their
  own MIT notices and stay under them;
- the files this project adds outside port/, marked A in
  LICENSE-PORT-FILES.txt;
- the modifications this project made to the decompilation's own files,
  which are marked M in LICENSE-PORT-FILES.txt, to the extent those
  modifications are separable from the decompiled code.

Files of the Port that closely follow the decompilation's code
(port/asm/arm_pc.c, the C version of its asm/arm.s and asm/arm_call.s,
and port/asm/ramfunc_pc.c, which replaces its src/ramfunc.c) are covered
only to the extent they are separable from the code they follow.

It does not cover, and this project claims no rights over: the fireemblem8u
decompilation and its contributors' work; any code, graphics, sound, text
or other material owned by Nintendo or Intelligent Systems; or any
third-party software such as SDL3. Those remain under whatever terms apply
to them.

Permission is hereby granted, free of charge, to any person obtaining a
copy of the Port, to deal in the Port without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Port, and to permit
persons to whom the Port is furnished to do so, subject to the following
conditions:

The above copyright notice, this scope statement and this permission
notice shall be included in all copies or substantial portions of the
Port.

THE PORT IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE PORT OR THE USE OR OTHER DEALINGS IN THE
PORT.
```

The permission and warranty paragraphs are the standard MIT text with "the Software" replaced by "the Port", as the precedent replaces it with "the Platform Layer"; the condition adds "this scope statement", as the precedent's does.

## Release checks (2026-10-31)

- Holder, scope and form: done 2026-10-30 (above).
- `LICENSE-PORT-FILES.txt` is generated from the release commit with the command in its header; nothing outside `port/` is deleted, renamed or copied.
- Licence or copyright text in `port/` (outside the tests): only `port/audio/m4a_1_pc.c`, `psg.c` and `psg.h`, whose notices are kept. This grep is not a code-similarity audit.
- The decompilation has no licence. The port is released as is, with no ROM (owner's decision, 2026-10-31; CREDITS.md, section 1).
- The source is published as a fresh repository with one commit (owner's decision, 2026-10-31), so older work in the development history is not part of it.
