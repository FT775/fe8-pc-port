/*
 * Windows window build only (port/Makefile.mingw SDL=1: fe8pc-sdl.exe, built
 * with FE8PC_WINGUI; the headless fe8pc.exe has none of this file).
 *
 * fe8pc-sdl.exe is a GUI-subsystem exe (-mwindows), so a double-clicked exe
 * opens its window and no console window. Its log, which the console build
 * prints, then has nowhere to go, so this file finds it a place, before
 * main() runs the game (port/win32/posix_win32.c, __wrap_main):
 *
 *   - output already goes somewhere (started from WSL, or from a terminal
 *     with its output redirected, `fe8pc-sdl.exe > log.txt`): it stays there,
 *     and nothing below happens;
 *   - --console (anywhere before a "--"; taken off the command line before
 *     main_pc.c reads it): the log goes to the console the exe was started
 *     from (cmd, PowerShell), or to a new console window when there is none;
 *   - otherwise (double-clicked, a shortcut, a terminal without --console): the
 *     log goes to fe8pc-sdl.log next to the exe, or to %APPDATA%\fe8pc\ (the
 *     per-user folder of the save files, port/save/savefile_pc.c) when no file
 *     can be made there. A second copy running at the same time, which finds
 *     the first one's log in use, writes fe8pc-sdl-<process id>.log. The file
 *     is made new at every start; the first log line says where it is.
 *
 * When nobody sees the log (the log file, or a console window opened just for
 * this run), a message box says what went wrong, with the last lines of the
 * log and where the whole log is: no ROM found (what was looked for, and
 * where to put the ROM), the game stopping before it starts (a ROM that is
 * not the original FE8 USA ROM: another game, a dump cut short, a changed
 * ROM; a reservation of the GBA memory map that failed, a save file that
 * cannot be read, a bad command line), and the game stopping with an error
 * later (a fault, whose report main_pc.c writes and then calls _exit(3); any
 * exit() or _exit() with a code other than 0: linked with --wrap=exit and
 * --wrap=_exit). Closing the window ends the run with exit(0): no box.
 * Started with its output going somewhere, the exe shows no box at all, so a
 * test run never waits on one.
 *
 * SDL3.dll is delay-loaded (port/Makefile.mingw says why: it and the user32
 * and gdi32 it brings must load after memory.c has reserved the GBA map), so
 * a missing or too old SDL3.dll shows up at the first SDL call, as an
 * exception, not as Windows' "SDL3.dll was not found" dialog. A handler here
 * says so in the log and in a box, and ends the run. For the same reason this
 * file calls nothing in user32 or shell32 at start-up: user32 is loaded only
 * to show a box, and the per-user folder is %APPDATA%.
 *
 * v1.22.0, the first-start ROM picker (PortWinGui_FindRom; posix_win32.c asks
 * only when no original ROM was found in the usual places: the command line,
 * the current folder's baserom.gba, next to the exe, the roms folder; a file
 * there with only the FE8 header, a hack or a cut-short copy, is said in the
 * log and in a box first, and is used, for memory.c's message, on Cancel): the path
 * remembered from an earlier choice (rom-path.txt in the per-user folder,
 * %APPDATA%\fe8pc, or FE8PC_USER_DIR's), when that file is still the original
 * FE8 (USA) ROM; else, when nobody sees the log, an Open dialog ("Choose your
 * Fire Emblem: The Sacred Stones ROM", *.gba, starting in the roms folder).
 * The file chosen is checked as memory.c checks a ROM (header, size, SHA-1);
 * a wrong one gets memory.c's words in a box and the dialog opens again;
 * Cancel gives the "no ROM found" box. The path is remembered (rom-path.txt
 * holds that one line), never the ROM: nothing is copied. The chosen ROM's
 * save stays next to the exe (posix_win32.c). comdlg32 is loaded by name, as user32 for the boxes.
 * Test switch FE8PC_TEST_PICK_ROM=answer[|answer...]: the dialog's answers
 * (a path, or "cancel"), in order, without the dialog, for scripted runs.
 *
 * Nothing here changes what the game does; it only routes the log and finds
 * the ROM.
 */

#include <windows.h>
#include <commdlg.h>

#include "port_hw.h" /* PortRomFileKind (port/hw/memory.c) */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_NAME L"fe8pc-sdl"
#define BOX_TITLE L"FE8 PC port"
#define TAIL_LINES 8

static int sBoxes;                      /* 1: nobody sees the log, so a failure gets a message box */
static int sBoxShown;
static wchar_t sLogPathW[MAX_PATH * 2]; /* the log file this run writes ("" for none) */
static char sLogPath[MAX_PATH * 8];     /* the same in UTF-8, for the log and the boxes */

void __real_exit(int code) __attribute__((noreturn));
void __real__exit(int code) __attribute__((noreturn));
static LONG CALLBACK DelayLoadFailed(EXCEPTION_POINTERS *ep);

/* ------------------------------------------------------------------------- */
/* The log                                                                   */

static int HasOutput(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    return h != NULL && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
}

/* The folder of the exe, without the last backslash; 0 when it is not known. */
static int ExeDirW(wchar_t *out, DWORD outLen)
{
    DWORD n = GetModuleFileNameW(NULL, out, outLen);
    wchar_t *slash;

    if (n == 0 || n >= outLen)
        return 0;
    slash = wcsrchr(out, L'\\');
    if (slash == NULL)
        return 0;
    *slash = L'\0';
    return 1;
}

/* fe8pc-sdl.log in dir, or fe8pc-sdl-<pid>.log when another copy has that
   one open. Kept open for the whole run; others may read it meanwhile. */
static HANDLE OpenLogIn(const wchar_t *dir)
{
    HANDLE h;

    _snwprintf(sLogPathW, sizeof(sLogPathW) / sizeof(sLogPathW[0]), L"%ls\\%ls.log", dir, LOG_NAME);
    h = CreateFileW(sLogPathW, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION)
    {
        _snwprintf(sLogPathW, sizeof(sLogPathW) / sizeof(sLogPathW[0]), L"%ls\\%ls-%lu.log", dir, LOG_NAME,
            (unsigned long)GetCurrentProcessId());
        h = CreateFileW(sLogPathW, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
            NULL);
    }
    if (h == INVALID_HANDLE_VALUE)
        sLogPathW[0] = L'\0';
    return h;
}

static HANDLE OpenLog(void)
{
    wchar_t dir[MAX_PATH * 2];
    const wchar_t *appData = _wgetenv(L"APPDATA");
    HANDLE h = INVALID_HANDLE_VALUE;

    if (ExeDirW(dir, sizeof(dir) / sizeof(dir[0])))
        h = OpenLogIn(dir);
    /* The per-user folder, %APPDATA%\fe8pc, which SDL_GetPrefPath(NULL,
       "fe8pc") also names (from the shell's Roaming folder, which %APPDATA%
       is; shell32 is not loaded this early, see the top of this file). */
    if (h == INVALID_HANDLE_VALUE && appData != NULL && appData[0] != L'\0' &&
        wcslen(appData) + 8 < sizeof(dir) / sizeof(dir[0]))
    {
        _snwprintf(dir, sizeof(dir) / sizeof(dir[0]), L"%ls\\fe8pc", appData);
        CreateDirectoryW(dir, NULL); /* there already is fine */
        h = OpenLogIn(dir);
    }
    if (h != INVALID_HANDLE_VALUE &&
        !WideCharToMultiByte(CP_UTF8, 0, sLogPathW, -1, sLogPath, sizeof(sLogPath), NULL, NULL))
        sLogPath[0] = '\0';
    return h;
}

/* stderr unbuffered again, as the C library starts it: freopen gives it a
   buffer, and whatever sat in that buffer would be missing from a message
   box's log lines (main_pc.c's usage line, the renderer's tripwire lines),
   come after later stdout lines, and be lost on _exit. stdout keeps its
   buffer; PortLog and the SDL backend flush it after every line. */
static void UnbufferStderr(void)
{
    setvbuf(stderr, NULL, _IONBF, 0);
}

/* stdout and stderr (the C streams and the Win32 handles) to h. In a
   GUI-subsystem process the C library leaves both streams without a file
   descriptor, so each is first opened on NUL, which gives it one, and then
   that descriptor is made a copy of the log's. */
static int RedirectTo(HANDLE h)
{
    int fd = _open_osfhandle((intptr_t)h, _O_WRONLY | _O_BINARY);

    if (fd < 0)
        return -1;
    if (freopen("NUL", "w", stdout) == NULL || freopen("NUL", "w", stderr) == NULL ||
        _fileno(stdout) < 0 || _fileno(stderr) < 0)
        return -1;
    if (_dup2(fd, _fileno(stdout)) != 0 || _dup2(fd, _fileno(stderr)) != 0)
        return -1;
    SetStdHandle(STD_OUTPUT_HANDLE, h);
    SetStdHandle(STD_ERROR_HANDLE, h);
    _setmode(_fileno(stdout), _O_BINARY); /* "\n" line ends, as the Linux log */
    _setmode(_fileno(stderr), _O_BINARY);
    UnbufferStderr();
    return 0;
}

/* The console this exe was started from, or a new one. 1: a console is
   there now; *made: a new window, which closes with the exe. */
static int UseConsole(int *made)
{
    *made = 0;
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
    {
        if (!AllocConsole())
            return 0;
        *made = 1;
    }
    if (freopen("CONOUT$", "w", stdout) == NULL || freopen("CONOUT$", "w", stderr) == NULL)
        return 0;
    UnbufferStderr();
    return 1;
}

/* Takes --console off the command line (before a "--"). 1 when it was there. */
static int TakeConsoleOption(int *argc, char **argv)
{
    int i, j, found = 0;

    for (i = j = 1; i < *argc; i++)
    {
        if (strcmp(argv[i], "--") == 0)
        {
            while (i < *argc)
                argv[j++] = argv[i++];
            break;
        }
        if (strcmp(argv[i], "--console") == 0)
            found = 1;
        else
            argv[j++] = argv[i];
    }
    *argc = j;
    argv[j] = NULL;
    return found;
}

void PortWinGui_Start(int *argc, char ***argv)
{
    int console = TakeConsoleOption(argc, *argv), made;
    HANDLE h;

    AddVectoredExceptionHandler(0, DelayLoadFailed); /* last: it only looks at the delay-load codes */
    if (HasOutput())
        return; /* WSL, or redirected: the output stays where it goes */
    if (console && UseConsole(&made))
    {
        sBoxes = made;
        return;
    }
    sBoxes = 1;
    h = OpenLog();
    if (h != INVALID_HANDLE_VALUE && RedirectTo(h) == 0)
    {
        printf("[win32] log file '%s' (fe8pc-sdl.exe has no console; run it with --console to see the log "
               "there instead)\n", sLogPath);
        fflush(stdout);
    }
    else
        sLogPath[0] = '\0';
}

/* ------------------------------------------------------------------------- */
/* Message boxes                                                             */

/* The last TAIL_LINES lines of this run's log (UTF-8), or "" when there is none. */
static void LogTail(char *out, size_t outSize)
{
    char buf[4096];
    DWORD got = 0;
    LARGE_INTEGER size, pos;
    HANDLE h;
    char *p;
    int lines = 0;

    out[0] = '\0';
    if (sLogPathW[0] == L'\0')
        return;
    fflush(stdout);
    fflush(stderr);
    h = CreateFileW(sLogPathW, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    if (GetFileSizeEx(h, &size))
    {
        pos.QuadPart = size.QuadPart > (LONGLONG)sizeof(buf) - 1 ? size.QuadPart - (LONGLONG)sizeof(buf) + 1 : 0;
        if (SetFilePointerEx(h, pos, NULL, FILE_BEGIN) && ReadFile(h, buf, sizeof(buf) - 1, &got, NULL))
            buf[got] = '\0';
    }
    CloseHandle(h);
    if (got == 0)
        return;
    while (got > 0 && (buf[got - 1] == '\n' || buf[got - 1] == '\r'))
        buf[--got] = '\0';
    for (p = buf + got; p > buf; p--)
        if (p[-1] == '\n' && ++lines == TAIL_LINES)
            break;
    snprintf(out, outSize, "%s", p);
}

static void Box(const char *text)
{
    typedef int(WINAPI * MessageBoxWFn)(HWND, LPCWSTR, LPCWSTR, UINT);
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    HMODULE user32;
    MessageBoxWFn messageBox;
    wchar_t *w;

    if (sBoxShown || n <= 0)
        return;
    sBoxShown = 1;
    user32 = LoadLibraryW(L"user32.dll"); /* not imported: see the top of this file */
    messageBox = user32 != NULL ? (MessageBoxWFn)(void *)GetProcAddress(user32, "MessageBoxW") : NULL;
    if (messageBox == NULL || (w = malloc((size_t)n * sizeof(*w))) == NULL)
        return;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, w, n);
    messageBox(NULL, w, BOX_TITLE, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    free(w);
}

static void BoxWithLog(const char *what)
{
    char tail[4096], text[sizeof(tail) + sizeof(sLogPath) + 512];

    LogTail(tail, sizeof(tail));
    snprintf(text, sizeof(text), "%s%s%s%s%s", what, tail[0] != '\0' ? "\n\nThe last lines of the log:\n\n" : "",
        tail, sLogPath[0] != '\0' ? "\n\nThe whole log is in:\n" : "", sLogPath);
    Box(text);
}

int PortWinGui_NoRom(void)
{
    char exeDir[MAX_PATH * 8] = "", cwd[MAX_PATH * 8] = "", text[sizeof(exeDir) + 1024];
    wchar_t dirW[MAX_PATH * 2];

    if (ExeDirW(dirW, sizeof(dirW) / sizeof(dirW[0])))
        WideCharToMultiByte(CP_UTF8, 0, dirW, -1, exeDir, sizeof(exeDir), NULL, NULL);
    if (!GetCurrentDirectoryA(sizeof(cwd), cwd))
        cwd[0] = '\0';
    printf("[win32] no ROM found: no ROM path given, no baserom.gba in the current folder ('%s'), and no "
           "Fire Emblem: The Sacred Stones (USA) .gba file next to the exe ('%s') or in its roms folder\n", cwd,
        exeDir);
    fflush(stdout);
    if (sBoxes)
    {
        snprintf(text, sizeof(text),
            "No Fire Emblem: The Sacred Stones (USA) ROM was found.\n\n"
            "Put your ROM file (.gba) in the roms folder next to fe8pc-sdl.exe:\n%s\\roms\n\n"
            "(or in the same folder as fe8pc-sdl.exe), or drop the ROM file onto fe8pc-sdl.exe.\n\n"
            "The game is not included: the port runs the ROM of your own cartridge.", exeDir);
        Box(text);
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* The first-start ROM picker (v1.22.0)                                      */

/* A box that may come more than once (the picker's "wrong file"), unlike Box. */
static void PickerBox(const char *text)
{
    typedef int(WINAPI * MessageBoxWFn)(HWND, LPCWSTR, LPCWSTR, UINT);
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    MessageBoxWFn messageBox = user32 != NULL ? (MessageBoxWFn)(void *)GetProcAddress(user32, "MessageBoxW") : NULL;
    wchar_t *w;

    if (messageBox == NULL || n <= 0 || (w = malloc((size_t)n * sizeof(*w))) == NULL)
        return;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, w, n);
    messageBox(NULL, w, BOX_TITLE, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    free(w);
}

/* The file that remembers the chosen ROM: rom-path.txt in FE8PC_USER_DIR (a test switch), else in
   %APPDATA%\fe8pc (the per-user folder of the settings, port/sdl3/platform_sdl3.c Platform_UserDir). mkdir:
   make the folder. 0 when there is none. */
static int RememberFile(wchar_t *out, size_t outLen, int mkdir)
{
    const wchar_t *env = _wgetenv(L"FE8PC_USER_DIR");
    const wchar_t *appData = _wgetenv(L"APPDATA");
    wchar_t dir[MAX_PATH * 2];
    size_t n;

    if (env != NULL && env[0] != L'\0')
        _snwprintf(dir, sizeof(dir) / sizeof(dir[0]), L"%ls", env);
    else if (appData != NULL && appData[0] != L'\0')
        _snwprintf(dir, sizeof(dir) / sizeof(dir[0]), L"%ls\\fe8pc", appData);
    else
        return 0;
    dir[sizeof(dir) / sizeof(dir[0]) - 1] = L'\0';
    n = wcslen(dir);
    while (n > 0 && (dir[n - 1] == L'\\' || dir[n - 1] == L'/'))
        dir[--n] = L'\0';
    if (mkdir)
        CreateDirectoryW(dir, NULL); /* there already is fine */
    _snwprintf(out, outLen, L"%ls\\rom-path.txt", dir);
    out[outLen - 1] = L'\0';
    return 1;
}

/* What is wrong with a chosen file, in memory.c's words; "" when it is the original FE8 (USA) ROM. */
static void RomProblem(const char *path, char *out, size_t outSize)
{
    char sha1[41];

    switch (PortRomFileKind(path, sha1))
    {
    case PORT_ROM_FE8:
        out[0] = '\0';
        break;
    case PORT_ROM_WRONG_SIZE:
        snprintf(out, outSize, "'%s' has the FE8 (USA) header but not its size, 16,777,216 bytes (16 MiB): an "
                 "incomplete or damaged copy of the ROM. Copy (dump) the ROM from the cartridge again.", path);
        break;
    case PORT_ROM_WRONG_SHA1:
        snprintf(out, outSize, "'%s' has the FE8 (USA) header and size, but not the original ROM's contents: its "
                 "SHA-1 is %s, the original's c25b145e37456171ada4b0d440bf88a19f4d509f. It is a changed ROM (a hack, a "
                 "translation or a patch) or a damaged copy; the port runs only the original FE8 (USA) ROM.", path, sha1);
        break;
    default:
        snprintf(out, outSize, "'%s' is not FE8 (USA): header mismatch (it is not a Fire Emblem: The Sacred Stones "
                 "(USA) ROM, or not a ROM at all).", path);
        break;
    }
}

/* One answer of the Open dialog (UTF-8), or of FE8PC_TEST_PICK_ROM. 0: Cancel. */
static int AskForRom(char *out, size_t outSize)
{
    typedef BOOL(WINAPI * GetOpenFileNameWFn)(LPOPENFILENAMEW);
    static int sTestAt;
    const char *test = getenv("FE8PC_TEST_PICK_ROM");
    wchar_t file[MAX_PATH * 2] = L"", dir[MAX_PATH * 2];
    OPENFILENAMEW ofn;
    HMODULE comdlg;
    GetOpenFileNameWFn getOpen;

    if (test != NULL)
    {
        const char *p = test;
        int k;
        size_t n;

        for (k = 0; k < sTestAt && p != NULL; k++)
            p = (p = strchr(p, '|')) != NULL ? p + 1 : NULL;
        sTestAt++;
        if (p == NULL)
            p = "cancel";
        n = strcspn(p, "|");
        snprintf(out, outSize, "%.*s", (int)n, p);
        printf("[win32] ROM picker (FE8PC_TEST_PICK_ROM answer %d): %s\n", sTestAt, out);
        fflush(stdout);
        return strcmp(out, "cancel") != 0;
    }
    if (!sBoxes)
    {
        printf("[win32] ROM picker not shown: the log is seen (a terminal or a redirect), so nobody may be at the "
               "window to answer it\n");
        fflush(stdout);
        return 0;
    }
    comdlg = LoadLibraryW(L"comdlg32.dll"); /* not imported: see the top of this file */
    getOpen = comdlg != NULL ? (GetOpenFileNameWFn)(void *)GetProcAddress(comdlg, "GetOpenFileNameW") : NULL;
    if (getOpen == NULL)
    {
        printf("[win32] ROM picker: comdlg32's GetOpenFileNameW is not there\n");
        return 0;
    }
    dir[0] = L'\0';
    if (ExeDirW(dir, sizeof(dir) / sizeof(dir[0])))
    {
        size_t n = wcslen(dir);
        DWORD at;

        _snwprintf(dir + n, sizeof(dir) / sizeof(dir[0]) - n, L"\\roms");
        dir[sizeof(dir) / sizeof(dir[0]) - 1] = L'\0';
        at = GetFileAttributesW(dir);
        if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY))
            dir[n] = L'\0'; /* no roms folder: the exe's */
    }
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Game Boy Advance ROM (*.gba)\0*.gba\0All files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file) / sizeof(file[0]);
    ofn.lpstrInitialDir = dir[0] != L'\0' ? dir : NULL;
    ofn.lpstrTitle = L"Choose your Fire Emblem: The Sacred Stones ROM";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    printf("[win32] no ROM found: the ROM picker is open (\"Choose your Fire Emblem: The Sacred Stones ROM\")\n");
    fflush(stdout);
    if (!getOpen(&ofn))
        return 0;
    return WideCharToMultiByte(CP_UTF8, 0, file, -1, out, (int)outSize, NULL, NULL) != 0;
}

/* The path rom-path.txt remembers (its one line), checked or not; 0 when there is none. */
static int RememberedRom(char *out, size_t outSize)
{
    char text[MAX_PATH * 8];
    wchar_t memo[MAX_PATH * 2];
    FILE *fp;
    size_t n;

    out[0] = '\0';
    if (!RememberFile(memo, sizeof(memo) / sizeof(memo[0]), 0) || (fp = _wfopen(memo, L"rb")) == NULL)
        return 0;
    n = fread(text, 1, sizeof(text) - 1, fp);
    fclose(fp);
    text[n] = '\0';
    n = strcspn(text, "\r\n");
    if (n >= outSize)
        return 0;
    memcpy(out, text, n);
    out[n] = '\0';
    return out[0] != '\0';
}

/* posix_win32.c, when no original ROM is found in the usual places: the remembered ROM, else the one the
   player chooses (remembered for the next start). notOriginal: the file found there with only the FE8 header
   (NULL: none), said before asking. 1: out is the original FE8 (USA) ROM; 0: none (Cancel, or no dialog). */
int PortWinGui_FindRom(char *out, size_t outSize, const char *notOriginal)
{
    wchar_t memo[MAX_PATH * 2];
    char problem[MAX_PATH * 8 + 512];
    FILE *fp;

    if (RememberedRom(out, outSize))
    {
        RomProblem(out, problem, sizeof(problem));
        if (problem[0] == '\0')
        {
            printf("[win32] the ROM chosen at an earlier start: '%s' (remembered in rom-path.txt)%s\n", out,
                notOriginal != NULL ? ", not the hacked or cut-short FE8 file found next to the exe or in roms" : "");
            fflush(stdout);
            return 1;
        }
        printf("[win32] the remembered ROM '%s' is gone or no longer the original ROM: asking again\n", out);
        fflush(stdout);
    }
    if (notOriginal != NULL) /* only a changed or cut-short FE8 file was found: say so, then ask */
    {
        RomProblem(notOriginal, problem, sizeof(problem));
        printf("[win32] the only FE8 file found is not the original ROM (%s): asking for the original\n", problem);
        fflush(stdout);
        if (sBoxes && getenv("FE8PC_TEST_PICK_ROM") == NULL)
        {
            char text[sizeof(problem) + 160];

            snprintf(text, sizeof(text), "%s\n\nChoose your original ROM in the next window, or Cancel to stop.",
                problem);
            PickerBox(text);
        }
    }
    while (AskForRom(out, outSize))
    {
        RomProblem(out, problem, sizeof(problem));
        if (problem[0] == '\0')
        {
            if (RememberFile(memo, sizeof(memo) / sizeof(memo[0]), 1) && (fp = _wfopen(memo, L"wb")) != NULL)
            {
                fprintf(fp, "%s\n", out);
                fclose(fp);
                printf("[win32] ROM chosen: '%s' (remembered for the next start; the ROM itself is not copied)\n", out);
            }
            else
                printf("[win32] ROM chosen: '%s' (it could not be remembered)\n", out);
            fflush(stdout);
            return 1;
        }
        printf("[win32] ROM picker: refused: %s\n", problem);
        fflush(stdout);
        if (sBoxes && getenv("FE8PC_TEST_PICK_ROM") == NULL)
        {
            char text[sizeof(problem) + 128];

            snprintf(text, sizeof(text), "%s\n\nChoose another file, or Cancel.", problem);
            PickerBox(text);
        }
    }
    return 0;
}

void PortWinGui_StartFailed(int code)
{
    if (sBoxes && code != 0)
        BoxWithLog("FE8 PC port could not start the game.");
}

static void Stopped(int code)
{
    char what[128];

    if (!sBoxes || code == 0)
        return;
    snprintf(what, sizeof(what), "FE8 PC port stopped because of an error (exit code %d).", code);
    BoxWithLog(what);
}

/* The delay-load helper's exceptions (MinGW's delayimp.h: VcppException with
   ERROR_MOD_NOT_FOUND or ERROR_PROC_NOT_FOUND): SDL3.dll is not there, or is
   an SDL older than the one this exe was built against. */
#define DELAYLOAD_NO_DLL  0xC06D007Eu
#define DELAYLOAD_NO_PROC 0xC06D007Fu

static LONG CALLBACK DelayLoadFailed(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    const char *why;
    char text[sizeof(sLogPath) + 512];

    if (code != DELAYLOAD_NO_DLL && code != DELAYLOAD_NO_PROC)
        return EXCEPTION_CONTINUE_SEARCH;
    why = code == DELAYLOAD_NO_DLL
              ? "SDL3.dll could not be loaded: it must be in the same folder as fe8pc-sdl.exe (it comes with it)."
              : "SDL3.dll is too old: it lacks a function fe8pc-sdl.exe uses (it needs SDL 3.4.2 or later; use "
                "the SDL3.dll that comes with it).";
    printf("[win32] %s\n", why);
    fflush(stdout);
    if (sBoxes)
    {
        snprintf(text, sizeof(text), "%s%s%s", why, sLogPath[0] != '\0' ? "\n\nThe log is in:\n" : "", sLogPath);
        Box(text);
    }
    __real__exit(1);
}

/* Linked with --wrap=exit and --wrap=_exit (port/Makefile.mingw SDL=1). */
void __wrap_exit(int code)
{
    Stopped(code);
    __real_exit(code);
}

void __wrap__exit(int code)
{
    Stopped(code);
    __real__exit(code);
}
