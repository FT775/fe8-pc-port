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
 * Nothing here changes what the game does; it only routes the log.
 */

#include <windows.h>

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
           "Fire Emblem: The Sacred Stones (USA) .gba file next to the exe ('%s')\n", cwd, exeDir);
    fflush(stdout);
    if (sBoxes)
    {
        snprintf(text, sizeof(text),
            "No Fire Emblem: The Sacred Stones (USA) ROM was found.\n\n"
            "Put your ROM file (.gba) in the same folder as fe8pc-sdl.exe:\n%s\n\n"
            "or drop the ROM file onto fe8pc-sdl.exe.\n\n"
            "The game is not included: the port runs the ROM of your own cartridge.", exeDir);
        Box(text);
    }
    return 1;
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
