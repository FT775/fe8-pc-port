#ifndef _WIN32
/* stat with 64-bit sizes and inode numbers: a 32-bit stat fails (EOVERFLOW) on
   file systems whose inode numbers do not fit 32 bits. This file only. */
#define _FILE_OFFSET_BITS 64
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#endif

#include "port_hw.h"
#include "../platform.h"
#ifdef PORT_RFO_SAVEMARK
/* Reforged Options' save mark, the sidecar <sav>.reforged (played builds only:
   port/Makefile gives this object PORT_RFO_SAVEMARK=1 there, never in a test
   or Gate 0 build; port/reforged/reforged_savemark.c). The .sav is unchanged. */
#include "port_reforged.h"
#endif

/*
 * The player's save file (port-plan/PLAN.md phase 5; DIVERGENCES.md H-9):
 * FE8's 32 KiB SRAM kept
 * in a file between runs, in the GBA's own format, the raw SRAM image an
 * emulator keeps in its .sav (mGBA writes exactly these 32,768 bytes for FE8).
 * A .sav from mGBA loads here, and one written here loads in mGBA.
 *
 * Which file (port/hw/main_pc.c parses the command line):
 *   --save PATH        PATH.
 *   --no-save          none.
 *   FE8PC_SAVE=PATH    PATH; FE8PC_SAVE=none: none. Window build only.
 *   otherwise          window build (fe8pc-sdl): the ROM path with its
 *                      extension replaced by .sav (baserom.gba ->
 *                      baserom.sav), as mGBA names it. Headless builds
 *                      (fe8pc, the Gate 0 build, fe8pc.exe): none.
 * The headless builds are the test builds: they never read FE8PC_SAVE, so a
 * value left in the environment cannot reach a gate, and they keep a save
 * only when their own command line says --save.
 *
 * Window build, default file only (ChooseDefault). The save file is the one
 * next to the ROM when a file can be made in the ROM's folder; when not (a
 * read-only folder, a CD, a folder of another user), the same name in the
 * per-user folder (Platform_UserDir: the folder SDL's pref path names,
 * $XDG_DATA_HOME/fe8pc/ or ~/.local/share/fe8pc/ on Linux, %APPDATA%\fe8pc\
 * on Windows), and the log and the window title say so for the whole run.
 * The other place is
 * always looked at too, since a save can be in either (one written while the
 * ROM's folder was read-only, or one put next to the ROM by hand or by mGBA).
 * A file of 0 bytes holds no save (mGBA makes one before the game first
 * saves), so it counts as none here:
 *   - a save in both places (two files, not one file and a link to it): the
 *     save file is loaded; the log and the window title say "two save files:
 *     using X, other at Y" for the whole run. Nothing is guessed from dates.
 *   - a save only in the other place (the save file not there, or empty): it
 *     is loaded, and the writes go to the save file; the log and the title
 *     say so.
 *   - the other place has a file that is not an FE8 save (a size that is not
 *     32 or 64 KiB) or that cannot be looked at or read (an unreadable
 *     folder, a link loop): the game starts from the save file as usual, and
 *     the log and the title say "not loaded: X ..." or "could not check X";
 *     only a problem with the save file itself stops the run.
 * The other file is never written. The per-user folder is made only when the
 * save file goes there (Platform_MakeUserDir); looking for a save in it does
 * not make it, so a run that saves next to the ROM leaves no empty folder
 * behind. When it cannot be made, the log and the title say SAVE FAILED from
 * the start. If there is no per-user folder (none can be named; the backend
 * logs why), only the file next to the ROM is used, never the current folder
 * instead; when no file can be made next to the ROM either, that file is
 * loaded if it is there, and the log and the title say SAVE FAILED from the
 * start.
 * A file named with --save or FE8PC_SAVE is used as named.
 *
 * A save file that is there but may not be written (its permission: a
 * read-only file, even in a folder that can be written) is never replaced,
 * though a rename over it would work: it is loaded, the log and the window
 * title say SAVE FAILED from the start, and every write is refused as a
 * failed one (retried, so a file made writable during the run gets the next
 * write). mGBA leaves such a file alone too: mCoreAutoloadSave opens the .sav
 * for reading and writing, and when that fails its Qt frontend says "Failed
 * to open save file; in-game saves cannot be updated." and the game runs
 * with blank SRAM; here the file is loaded, since reading it is allowed.
 *
 * One writer per save file (any build, any way the file was named). Each
 * write replaces the whole file, so two copies of the game on one file would
 * each erase the other's newer saves (the last to write wins). So a run takes
 * an exclusive advisory lock for as long as it runs: on PATH.lock, next to the
 * file the writes replace (flock on Linux, LockFileEx on Windows; the system
 * lets it go when the process ends, however it ends). Not on the .sav itself:
 * each write renames a new file over it, which would leave the lock on the old
 * one. A copy started while another holds the lock loads the file but never
 * writes it: the log and the window title say "save in use by another copy:
 * not saving" from the start and for the whole run, and at the end the log
 * says its last changes are not in the file. The lock file is empty and is
 * left in place; it holds nothing (removing it while no copy runs is
 * harmless). Where it cannot be made (a read-only folder, where the writes
 * fail anyway) the run tries again before each write; on a file system with
 * no locks the run writes without one, and the check below still applies.
 *
 * Before each write, the file on disk is compared with what this run last
 * loaded from it or wrote to it (whether it is there, its size, its time and
 * an FNV-1a hash of its bytes, CheckUnchanged). When something else changed it
 * (mGBA, an older build of the port, a copy of the game on another machine
 * through a network share that does not pass locks on, a file copied in by
 * hand), it is not written over: the log and the title say "SAVE FAILED:
 * changed by another program" and this run writes nothing more, so that
 * program's saves stay. The same bytes with a new time (touched, or written
 * again unchanged) count as unchanged, since writing over them loses nothing;
 * a file removed during the run is written again. The check and the rename
 * are separate steps: a program that does not take the lock and writes in the
 * few milliseconds between them is not seen.
 *
 * Load (PortSave_Init, before AgbMain, so before SramInit, main.c:55): the
 * file's first 32 KiB go to SRAM (0x0E000000 under D1-a). A file of 0 bytes
 * (mGBA makes one before the game first saves) or no file at all leaves the
 * blank SRAM that PortMemInit set (0xFF, as mGBA). A 64 KiB file (a
 * GATE0_SRAM dump, or an emulator that keeps 64 KiB) gives its first 32 KiB;
 * the upper half is not FE8's SRAM (D-18). When the save file has any other
 * size, or is there but cannot be read, the run stops before the game starts:
 * the file is never written over with something the game did not load from
 * it.
 *
 * Write (PortSave_Frame, at the end of every frame step): when the 32 KiB
 * differ from what the file holds, they are written whole, to a temporary
 * file and then renamed over the save file (Platform_SaveFileAtomic; on
 * Windows MoveFileEx with MOVEFILE_REPLACE_EXISTING), so a crash or a power
 * cut leaves the old file or the new one, never part of one.
 *   - A save file that is a symbolic link is written through it, as mGBA
 *     (which writes its .sav in place) does: the link is followed once, at
 *     start-up, and the temporary file and the rename are next to the file
 *     it names, so the link stays a link and its target gets the bytes
 *     (ResolveLink; on Windows GetFinalPathNameByHandle, posix_win32.c).
 *   - A hard link is not kept: the rename puts a new file under the save
 *     file's name, so another name of the old file (a hard link made by
 *     hand) keeps the bytes it had and no longer follows the save. Nothing is
 *     lost (mGBA, which writes in place, keeps such a link). Use a symbolic
 *     link to keep a save under two names.
 *   - The temporary file is PATH.tmp.<pid>, one name per process. A kill
 *     between the temporary file and the rename leaves it behind; it is never
 *     loaded, and the save file is the old one, whole.
 * The end of a frame step is a safe point: the game's SRAM writes
 * (WriteAndVerifySramFast and the rest) are plain calls that finish within
 * the game's tick, so a save the game writes in one tick reaches the file
 * whole. Any write to SRAM counts, whichever function made it, since the
 * image is compared, not the calls counted. PortExit (window closed, frame
 * limit) writes once more (PortSave_Flush). Other ends do not: a fault
 * (_exit), a kill, or the Gate 0 hook's exit() after its last tick, which
 * comes right after a frame step, so the file already holds that tick's SRAM.
 * (No atexit(): gen_host_link.py cannot see MinGW's, which is in crt2.o, and
 * would stub it.) A failed write is retried 60 frame steps later; the log
 * says so, and in the window build the window title says "SAVE FAILED" until
 * a write works again, so a failed save is never silent.
 * Nothing here changes what the game sees: it reads and writes SRAM in memory
 * as before, and with no save file this code does nothing after start-up.
 */

#define SAVE_SIZE 0x8000u   /* FE8's SRAM, 32 KiB (DIVERGENCES.md D-18) */
#define SAVE_SIZE_64K 0x10000u
#define RETRY_FRAMES 60
#define MAX_FAIL_LOGS 3
#define PATH_SIZE 4096
#define MAX_LINKS 40        /* as Linux's limit on symbolic links in one lookup */

#ifdef _WIN32
/* port/win32/posix_win32.c */
int PortWin32FinalPath(const char *path, char *out, size_t outSize);
int PortWin32FileStamp(const char *path, uint64_t *size, int64_t *mtime);
int PortWin32LockFile(const char *path);
int PortWin32SameFile(const char *a, const char *b);
#endif

/* The file the writes replace, as this run last loaded or wrote it
   (CheckUnchanged). */
struct Stamp
{
    int there;          /* 0: no file */
    int known;          /* 0: it could not be read (any file there then counts as changed) */
    uint64_t size;
    int64_t mtime;      /* last change: ns on Linux, 100 ns units on Windows */
    uint64_t hash;      /* FNV-1a 64 of all its bytes */
};

enum { LOCK_NONE, LOCK_HELD, LOCK_BUSY };
enum { SAVING, NOSAVE_BUSY, NOSAVE_CHANGED };

static int sOn;
static char sPath[PATH_SIZE];      /* the save file, as named */
static char sWritePath[PATH_SIZE]; /* the file the writes replace: sPath, or the file its link names */
static char sTmpPath[PATH_SIZE];   /* the temporary file of a write: sWritePath.tmp.<pid> */
static char sLockPath[PATH_SIZE];  /* the lock file: sWritePath.lock */
static char sTitleNote[2 * PATH_SIZE + 64]; /* the window title's note for the whole run ("" for none) */
static char sFailNote[PATH_SIZE + 128];     /* "SAVE FAILED: ..." while saves fail ("" when they work) */
static int sTitleFailed;           /* the window title says SAVE FAILED */
static uint8_t sFileImage[SAVE_SIZE]; /* what the file holds (or will hold) */
static uint8_t sCheckBuf[SAVE_SIZE_64K + 1];
static struct Stamp sStamp;        /* the file the writes replace, as last loaded or written */
static int sLock;                  /* LOCK_*: this run's lock on sLockPath */
static int sLockErr;               /* errno of the last try that got no lock (LOCK_NONE) */
static int sNoSaving;              /* NOSAVE_*: this run writes nothing more (for the whole run) */
static int sTouchedLogged, sGoneLogged;
#ifndef _WIN32
static int sLockFd = -1;           /* open for the whole run: closing it would let the lock go */
#endif
static uint32_t sRetryFrame;
static unsigned sWrites, sFailures, sFailRun;

static uint8_t *Sram(void)
{
    return (uint8_t *)(uintptr_t)PORT_SRAM_START;
}

static uint64_t Hash(const uint8_t *p, size_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;

    while (n-- != 0)
        h = (h ^ *p++) * 0x100000001B3ull;
    return h;
}

/* The last path element (after the last / or \). */
static const char *BaseName(const char *path)
{
    const char *base = path, *p;

    for (p = path; *p != '\0'; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    return base;
}

/* baserom.gba -> baserom.sav: the extension of the last path element
   replaced, or .sav added when it has none (mGBA's naming). Never the ROM's
   own path: a ROM named x.sav gives x.sav.sav. */
static int DefaultPath(const char *romPath, char *out, size_t outSize)
{
    const char *base = BaseName(romPath), *dot;
    size_t stem;

    dot = strrchr(base, '.');
    stem = (dot != NULL && dot != base) ? (size_t)(dot - romPath) : strlen(romPath);
    if (snprintf(out, outSize, "%.*s.sav", (int)stem, romPath) >= (int)outSize)
        return -1;
    if (strcmp(out, romPath) == 0 && snprintf(out, outSize, "%s.sav", romPath) >= (int)outSize)
        return -1;
    return 0;
}

/* The file a write replaces: path itself, or, when path is a symbolic link
   (or a chain of them), the file the last link names, which need not exist
   yet (mGBA's write through a dangling link creates it too). A relative link
   is relative to the link's own folder. 0, or -1 with errno set when the
   chain cannot be followed (a loop, a name too long, a folder that cannot be
   read). */
static int ResolveLink(const char *path, char *out, size_t outSize)
{
#ifdef _WIN32
    int r = PortWin32FinalPath(path, out, outSize);

    if (r < 0)
        return -1;
    if (r == 0 && snprintf(out, outSize, "%s", path) >= (int)outSize)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
#else
    char target[PATH_SIZE];
    int links;

    if (snprintf(out, outSize, "%s", path) >= (int)outSize)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    for (links = 0; links < MAX_LINKS; links++)
    {
        ssize_t n = readlink(out, target, sizeof(target) - 1);
        const char *slash;

        if (n < 0)
            return errno == EINVAL || errno == ENOENT ? 0 : -1; /* not a link, or nothing there yet */
        target[n] = '\0';
        slash = strrchr(out, '/');
        if (target[0] == '/' || slash == NULL)
        {
            if (snprintf(out, outSize, "%s", target) >= (int)outSize)
            {
                errno = ENAMETOOLONG;
                return -1;
            }
        }
        else
        {
            size_t dir = (size_t)(slash - out) + 1; /* keep "folder/" */

            if (dir + (size_t)n + 1 > outSize)
            {
                errno = ENAMETOOLONG;
                return -1;
            }
            memcpy(out + dir, target, (size_t)n + 1);
        }
    }
    errno = ELOOP;
    return -1;
#endif
}

/* sWritePath, sTmpPath and sLockPath for sPath. 0, or -1 (logged): stop the
   run. */
static int SetWritePaths(void)
{
    if (ResolveLink(sPath, sWritePath, sizeof(sWritePath)) != 0)
    {
        PortLog("[save] cannot follow the save file '%s' to the file it names: %s\n", sPath, strerror(errno));
        return -1;
    }
    if (snprintf(sTmpPath, sizeof(sTmpPath), "%s.tmp.%ld", sWritePath, (long)getpid()) >= (int)sizeof(sTmpPath) ||
        snprintf(sLockPath, sizeof(sLockPath), "%s.lock", sWritePath) >= (int)sizeof(sLockPath))
    {
        PortLog("[save] the save file path is too long\n");
        return -1;
    }
    if (strcmp(sWritePath, sPath) != 0)
        PortLog("[save] save file '%s' is a link to '%s': the writes go there, through '%s'\n", sPath, sWritePath,
            BaseName(sTmpPath));
    return 0;
}

/* Whether a file can be made in the folder of the next write (the temporary
   file's): made and removed again. errno says why not. */
static int CanMakeTmp(void)
{
    FILE *f = fopen(sTmpPath, "wb");

    if (f == NULL)
        return 0;
    fclose(f);
    remove(sTmpPath);
    return 1;
}

/* Size and time of the file at path (through links). 0; 1 when there is no
   file; -1 (errno set) when it cannot be looked at. On Windows by
   GetFileAttributesEx: MinGW's stat is _stat32, which gen_host_link.py would
   stub. */
static int StatFile(const char *path, uint64_t *size, int64_t *mtime)
{
#ifdef _WIN32
    return PortWin32FileStamp(path, size, mtime);
#else
    struct stat st;

    if (stat(path, &st) != 0)
        return errno == ENOENT ? 1 : -1;
    *size = (uint64_t)st.st_size;
    *mtime = (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
    return 0;
#endif
}

/* The hash of the file at path, which stat said is size bytes (at most
   SAVE_SIZE_64K). 0; 1 when it no longer has that size or is gone; -1 (errno
   set) when it cannot be read. */
static int HashFile(const char *path, uint64_t size, uint64_t *hash)
{
    FILE *f = fopen(path, "rb");
    size_t got;
    int bad;

    if (f == NULL)
        return errno == ENOENT ? 1 : -1;
    got = fread(sCheckBuf, 1, (size_t)size + 1, f);
    bad = ferror(f);
    fclose(f);
    if (bad)
    {
        errno = EIO;
        return -1;
    }
    if (got != size)
        return 1;
    *hash = Hash(sCheckBuf, got);
    return 0;
}

/* The stamp of the file at path, read from the disk now. */
static void ReadStamp(const char *path, struct Stamp *st)
{
    int r;

    memset(st, 0, sizeof(*st));
    r = StatFile(path, &st->size, &st->mtime);
    if (r == 1)
    {
        st->known = 1;
        return;
    }
    st->there = 1;
    if (r == 0 && st->size == 0)
    {
        st->known = 1;
        st->hash = Hash(NULL, 0);
        return;
    }
    st->known = r == 0 && st->size <= SAVE_SIZE_64K && HashFile(path, st->size, &st->hash) == 0;
}

/* Whether a and b, both there, are one file: one a link to the other, a hard
   link, or one name spelt two ways (a ROM kept in the per-user folder). On
   Windows by the volume serial number and file index (posix_win32.c), since
   struct stat has no inode there (and MinGW's stat is _stat32, which
   gen_host_link.py would stub: it is not a glibc name); so other letter case
   or / for \ is one file there, as Windows' file names are. The names the
   links lead to are compared only when that cannot be asked. */
static int SameFile(const char *a, const char *b)
{
    char ra[PATH_SIZE], rb[PATH_SIZE];
#ifdef _WIN32
    int r = PortWin32SameFile(a, b);

    if (r >= 0)
        return r;
#else
    struct stat sa, sb;

    if (stat(a, &sa) == 0 && stat(b, &sb) == 0)
        return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
    if (ResolveLink(a, ra, sizeof(ra)) != 0 || ResolveLink(b, rb, sizeof(rb)) != 0)
        return 0;
    return strcmp(ra, rb) == 0;
}

/* Whether the file the writes replace is there and may not be written (its
   permission, or a read-only file system): it is then never replaced, though
   a rename over it would work in a folder that can be written. errno says
   why. */
static int NotWritable(void)
{
    if (access(sWritePath, W_OK) == 0)
        return 0;
    return errno != ENOENT;
}

/* The window title's note: the SAVE FAILED note while saves fail, then the
   run's note. The headless build has no title; its log has the same lines. */
static void ShowTitle(void)
{
    char title[sizeof(sFailNote) + sizeof(sTitleNote) + 4];

    snprintf(title, sizeof(title), "%s%s%s", sFailNote,
        sFailNote[0] != '\0' && sTitleNote[0] != '\0' ? " - " : "", sTitleNote);
    Platform_SetTitleNote(title[0] != '\0' ? title : NULL);
}

/* SAVE FAILED in the window title until a write works ("SAVE FAILED: what
   path"). Not once this run has stopped saving: that note stays. */
static void SetFailNote(const char *what, const char *path)
{
    if (sNoSaving != SAVING)
        return;
    snprintf(sFailNote, sizeof(sFailNote), "SAVE FAILED: %s %s", what, path);
    sTitleFailed = 1;
    ShowTitle();
}

/* This run writes nothing more (another copy holds the lock, or another
   program changed the file); the note stays in the title for the whole run. */
static void StopSaving(int why, const char *note)
{
    sNoSaving = why;
    snprintf(sFailNote, sizeof(sFailNote), "%s", note);
    sTitleFailed = 1;
    ShowTitle();
}

/* One try for the lock on the save file (an exclusive advisory lock on
   sLockPath, kept until the process ends). LOCK_HELD, LOCK_BUSY (another
   process holds it), or LOCK_NONE (sLockErr: the lock file cannot be opened
   or made, or the file system has no locks). */
static int TryLock(void)
{
#ifdef _WIN32
    int r = PortWin32LockFile(sLockPath);

    if (r > 0)
        return LOCK_HELD;
    if (r == 0)
        return LOCK_BUSY;
    sLockErr = errno;
    return LOCK_NONE;
#else
    int fd = open(sLockPath, O_RDWR | O_CREAT | O_CLOEXEC, 0666), err = errno;

    /* A lock file this user may not write (another user's): flock needs only
       an open file. */
    if (fd < 0 && (err == EACCES || err == EPERM || err == EROFS))
        fd = open(sLockPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        sLockErr = errno == ENOENT ? err : errno;
        return LOCK_NONE;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
    {
        sLockFd = fd;
        return LOCK_HELD;
    }
    err = errno;
    close(fd);
    if (err == EWOULDBLOCK)
        return LOCK_BUSY;
    sLockErr = err;
    return LOCK_NONE;
#endif
}

/* The file system has no locks: the writes go on without one. */
static int NoLocksHere(int err)
{
    return err == ENOLCK || err == EINVAL || err == ENOSYS
#ifdef EOPNOTSUPP
        || err == EOPNOTSUPP
#endif
#if defined(ENOTSUP) && (!defined(EOPNOTSUPP) || ENOTSUP != EOPNOTSUPP)
        || err == ENOTSUP
#endif
        ;
}

/* Takes the lock if this run has none yet (at start-up, and before each write
   until it has it), and says what happened. Another copy holding it stops
   this run's saving for the whole run. */
static void Lock(void)
{
    static int sLoggedErr = -1;

    if (sLock != LOCK_NONE || sNoSaving != SAVING)
        return;
    sLock = TryLock();
    if (sLock == LOCK_HELD)
    {
        PortLog("[save] lock '%s' taken: no other copy of the game writes '%s' while this one runs\n", sLockPath,
            sWritePath);
        return;
    }
    if (sLock == LOCK_BUSY)
    {
        PortLog("[save] save in use by another copy: another running copy of the game holds '%s', so this copy "
                "never writes '%s' (its in-game saves are not kept; close the other copy, then start this one "
                "again to save)\n", sLockPath, sWritePath);
        StopSaving(NOSAVE_BUSY, "save in use by another copy: not saving");
        return;
    }
    if (sLockErr != sLoggedErr)
    {
        sLoggedErr = sLockErr;
        if (NoLocksHere(sLockErr))
            PortLog("[save] no lock on '%s' (%s): this file system has no locks, so the writes go on without one; "
                    "the check before each write still keeps another program's changes\n", sLockPath,
                strerror(sLockErr));
        else
            PortLog("[save] no lock on '%s' (%s): tried again before each write\n", sLockPath, strerror(sLockErr));
    }
}

/* Whether the file the writes replace is as this run last loaded or wrote it
   (sStamp). 0: it is (same bytes, perhaps with a new time), or it is gone;
   1 (logged): something else changed it; -1 (errno set): it cannot be looked
   at or read to tell. */
static int CheckUnchanged(void)
{
    uint64_t size = 0, hash = 0;
    int64_t mtime = 0;
    int r = StatFile(sWritePath, &size, &mtime), err = errno;

    if (r == 1)
    {
        if (sStamp.there && !sGoneLogged)
        {
            sGoneLogged = 1;
            PortLog("[save] frame %u: '%s' was removed while the game ran; it is written again\n", gPortFrameCount,
                sWritePath);
        }
        return 0;
    }
    if (r == 0 && sStamp.there && sStamp.known && size == sStamp.size)
    {
        r = HashFile(sWritePath, size, &hash);
        err = errno;
        if (r == 0 && hash == sStamp.hash)
        {
            if (mtime != sStamp.mtime && !sTouchedLogged)
            {
                sTouchedLogged = 1;
                PortLog("[save] frame %u: '%s' has a new time but the same bytes as this run last saw: written\n",
                    gPortFrameCount, sWritePath);
            }
            return 0;
        }
    }
    if (r < 0)
    {
        errno = err;
        return -1;
    }
    if (!sStamp.there)
        PortLog("[save] frame %u: SAVE FAILED: '%s' was made by another program while the game ran (%llu bytes); "
                "it is not written over, so it stays as that program left it, and this copy writes nothing more "
                "(its saves from now on are not kept)\n", gPortFrameCount, sWritePath, (unsigned long long)size);
    else
        PortLog("[save] frame %u: SAVE FAILED: '%s' was changed by another program since this copy %s it (%llu "
                "bytes, now %llu%s); it is not written over, so that program's saves stay, and this copy writes "
                "nothing more (its saves from now on are not kept)\n", gPortFrameCount, sWritePath,
            sWrites != 0 ? "last wrote" : "loaded", (unsigned long long)sStamp.size, (unsigned long long)size,
            size == sStamp.size ? ", other bytes" : "");
    return 1;
}

/* Load path into SRAM, and the file's stamp into *st. 0: loaded, or no file
   there (the save file only); -1 (logged): it cannot be loaded, SRAM
   untouched. save: path is the save file, which the writes would replace, so
   a file that cannot be loaded stops the run; otherwise it is the other
   place's file, never written, and the run goes on without it. */
static int Load(const char *path, struct Stamp *st, int save)
{
    size_t size = 0;
    uint8_t *data;

    /* The time before the bytes: a change in between shows as a new time. */
    memset(st, 0, sizeof(*st));
    st->known = 1;
    if (StatFile(save ? sWritePath : path, &st->size, &st->mtime) != 0)
        st->mtime = 0;
    data = Platform_LoadFile(path, &size);
    if (data == NULL)
    {
        /* No file yet, or one that cannot be read: only the first may be
           written over. Platform_LoadFile does not say which, so ask stdio. */
        FILE *f = fopen(path, "rb");
        int err = errno;

        if (f != NULL || err != ENOENT || !save)
        {
            if (f != NULL)
                fclose(f);
            PortLog("[save] cannot read %s'%s'%s%s; %s\n", save ? "the save file " : "", path, f == NULL ? ": " : "",
                f == NULL ? strerror(err) : "",
                save ? "stopping, so it is not written over" : "it is not loaded, and never written");
            return -1;
        }
        PortLog("[save] save file '%s': none yet; it is written when the game first writes SRAM\n", path);
        st->size = 0;
        return 0;
    }
    if (size != 0 && size != SAVE_SIZE && size != SAVE_SIZE_64K)
    {
        if (save)
            PortLog("[save] '%s' is %lu bytes; an FE8 save is 32,768 (or a 65,536-byte dump). Stopping, so it "
                    "is not written over (--save PATH for another file, --no-save for none)\n",
                path, (unsigned long)size);
        else
            PortLog("[save] '%s' is %lu bytes, not an FE8 save: it is not loaded, and never written\n", path,
                (unsigned long)size);
        Platform_FreeFile(data);
        return -1;
    }
    st->there = 1;
    st->size = size;
    st->hash = Hash(data, size);
    if (size == 0)
        PortLog("[save] save file '%s': empty, so blank SRAM\n", path);
    else
    {
        memcpy(Sram(), data, SAVE_SIZE);
        PortLog("[save] save file '%s': loaded (%lu bytes%s%s)\n", path, (unsigned long)size,
            size == SAVE_SIZE ? "" : "; its first 32 KiB, the rest is not FE8's SRAM",
            size == SAVE_SIZE || !save ? "" : "; the next write makes it a 32 KiB file");
    }
    Platform_FreeFile(data);
    return 0;
}

/* What is at one of the two places (ChooseDefault). */
enum { FILE_NONE, FILE_EMPTY, FILE_SAVE, FILE_NOT_SAVE, FILE_UNKNOWN };

static int Examine(const char *path, uint64_t *size, int *err)
{
    int64_t mtime;
    int r = StatFile(path, size, &mtime);

    *err = errno;
    if (r == 1)
        return FILE_NONE;
    if (r != 0)
        return FILE_UNKNOWN;
    if (*size == 0)
        return FILE_EMPTY;
    return *size == SAVE_SIZE || *size == SAVE_SIZE_64K ? FILE_SAVE : FILE_NOT_SAVE;
}

/* Window build, default file (see the top of this file). sPath is the file
   next to the ROM, with its write paths set. Picks the save file (the one
   next to the ROM, or the per-user one when no file can be made in the ROM's
   folder) and looks at the other place too. other: the other place's file;
   *loadOther: load it instead of the save file (a save only there). The other
   file is never written. 0, or -1 (logged): stop the run. */
static int ChooseDefault(char *other, int *loadOther)
{
    char romSide[PATH_SIZE];
    const char *dir, *otherWhere = "in the per-user folder";
    int romDirOk = CanMakeTmp(), err = errno, p, o, perr, oerr;
    uint64_t psize = 0, osize = 0;

    *loadOther = 0;
    other[0] = '\0';
    memcpy(romSide, sPath, sizeof(romSide));
    dir = Platform_UserDir();
    if (dir == NULL)
    {
        if (romDirOk)
        {
            PortLog("[save] no per-user folder (see above), so no save file is looked for there\n");
            return 0;
        }
        PortLog("[save] SAVE FAILED: cannot make files in the ROM's folder (next to '%s': %s) and there is no "
                "per-user folder (see above), so the game's saves are not kept while that folder cannot be "
                "written; '%s' is loaded if it is there\n", romSide, strerror(err), romSide);
        SetFailNote("no per-user folder, cannot make files next to", romSide);
        return 0;
    }
    if (snprintf(other, PATH_SIZE, "%s%s", dir, BaseName(romSide)) >= PATH_SIZE)
    {
        PortLog("[save] the save file path is too long\n");
        return -1;
    }
    if (!romDirOk)
    {
        memcpy(sPath, other, sizeof(sPath));
        memcpy(other, romSide, sizeof(romSide));
        otherWhere = "next to the ROM";
        PortLog("[save] cannot make files in the ROM's folder (next to '%s': %s), so the save file is '%s' "
                "(the per-user folder) for this run\n", romSide, strerror(err), sPath);
        snprintf(sTitleNote, sizeof(sTitleNote), "save file %s", sPath);
        /* Made only now that the save file goes there; a failure is logged,
           and CanMakeTmp below then says SAVE FAILED. */
        Platform_MakeUserDir();
        if (SetWritePaths() != 0)
            return -1;
        if (!CanMakeTmp())
        {
            PortLog("[save] SAVE FAILED: cannot make files in '%s' either (%s): the game's saves are not kept "
                    "while it cannot be written\n", dir, strerror(errno));
            SetFailNote("cannot write", sWritePath);
        }
    }
    p = Examine(sPath, &psize, &perr);
    o = Examine(other, &osize, &oerr);
    if ((o == FILE_SAVE || o == FILE_EMPTY || o == FILE_NOT_SAVE) && p != FILE_NONE && p != FILE_UNKNOWN &&
        SameFile(sPath, other))
        o = FILE_NONE; /* one file under two names */
    if (o == FILE_SAVE && (p == FILE_NONE || p == FILE_EMPTY))
    {
        PortLog("[save] '%s' %s: loading '%s' (%s); the writes go to '%s', and '%s' is never written\n", sPath,
            p == FILE_EMPTY ? "is empty (0 bytes, no save in it)" : "does not exist yet", other, otherWhere, sPath,
            other);
        snprintf(sTitleNote, sizeof(sTitleNote), "save file %s (loaded from %s)", sPath, other);
        *loadOther = 1;
    }
    else if (o == FILE_SAVE)
    {
        PortLog("[save] two save files: using '%s'; the other, '%s' (%s), is not loaded and never written "
                "(to play it, remove '%s': the next run loads the other one)\n", sPath, other, otherWhere, sPath);
        snprintf(sTitleNote, sizeof(sTitleNote), "two save files: using %s, other at %s", sPath, other);
    }
    else if (o == FILE_NOT_SAVE)
    {
        PortLog("[save] '%s' (%s) is %llu bytes, not an FE8 save (32,768 or 65,536): it is not loaded, and never "
                "written; the save file is '%s'\n", other, otherWhere, (unsigned long long)osize, sPath);
        snprintf(sTitleNote, sizeof(sTitleNote), "not loaded: %s (%llu bytes, not an FE8 save)", other,
            (unsigned long long)osize);
    }
    else if (o == FILE_UNKNOWN)
    {
        PortLog("[save] could not check '%s' (%s): %s; it is not loaded, and never written; the save file is '%s'\n",
            other, otherWhere, strerror(oerr), sPath);
        snprintf(sTitleNote, sizeof(sTitleNote), "could not check %s", other);
    }
    ShowTitle();
    return 0;
}

int PortSave_Init(const char *romPath, const char *savePath, int noSave)
{
    const char *env = Platform_IsInteractive() ? getenv("FE8PC_SAVE") : NULL;
    const char *why = NULL;
    static char other[PATH_SIZE];
    struct Stamp loaded;
    int isDefault = 0, loadOther = 0;

    if (noSave)
        why = "--no-save";
    else if (savePath != NULL)
    {
        if (snprintf(sPath, sizeof(sPath), "%s", savePath) >= (int)sizeof(sPath))
            goto too_long;
    }
    else if (env != NULL && env[0] != '\0')
    {
        if (strcmp(env, "none") == 0)
            why = "FE8PC_SAVE=none";
        else if (snprintf(sPath, sizeof(sPath), "%s", env) >= (int)sizeof(sPath))
            goto too_long;
    }
    else if (Platform_IsInteractive())
    {
        if (DefaultPath(romPath, sPath, sizeof(sPath)) != 0)
            goto too_long;
        isDefault = 1;
    }
    else
        why = "headless build; --save PATH keeps one";

    if (why != NULL)
    {
        PortLog("[save] no save file (%s)\n", why);
        return 0;
    }
    if (strcmp(sPath, romPath) == 0)
    {
        PortLog("[save] the save file cannot be the ROM ('%s')\n", sPath);
        return -1;
    }
    if (SetWritePaths() != 0)
        return -1;
    if (isDefault && ChooseDefault(other, &loadOther) != 0)
        return -1;
    /* The lock before the load: what this run loads is then what no other
       copy writes while it runs. */
    Lock();
    if (loadOther && Load(other, &loaded, 0) != 0)
    {
        snprintf(sTitleNote, sizeof(sTitleNote), "could not check %s", other);
        ShowTitle();
        loadOther = 0;
    }
    if (!loadOther && Load(sPath, &loaded, 1) != 0)
        return -1;
    if (loadOther)
        ReadStamp(sWritePath, &sStamp); /* the save file, empty or not there */
    else
        sStamp = loaded;
    if (NotWritable())
    {
        PortLog("[save] SAVE FAILED: '%s' may not be written (%s): it is loaded, but the game's saves do not "
                "replace it (as mGBA, which leaves a .sav it cannot open for writing alone); make it writable "
                "to keep them\n", sWritePath, strerror(errno));
        SetFailNote("read-only file", sWritePath);
    }
    memcpy(sFileImage, Sram(), SAVE_SIZE);
    sOn = 1;
#ifdef PORT_RFO_SAVEMARK
    Reforged_SaveMarkLoad(loadOther ? other : sWritePath, loadOther);
#endif
    return 0;

too_long:
    PortLog("[save] the save file path is too long\n");
    return -1;
}

static void Write(void)
{
    uint64_t t0 = Platform_NowNs();
    unsigned changed = 0, i;
    const uint8_t *sram = Sram();
    int readOnly, check = 0, err;

    Lock();
    if (sNoSaving != SAVING)
        return;
    for (i = 0; i < SAVE_SIZE; i++)
        changed += sram[i] != sFileImage[i];
    readOnly = NotWritable();
    err = errno;
    if (!readOnly)
    {
        /* Never over a file something else changed (see the top of this file). */
        check = CheckUnchanged();
        err = errno;
        if (check > 0)
        {
            sFailures++;
            StopSaving(NOSAVE_CHANGED, "SAVE FAILED: changed by another program, not saving");
            return;
        }
    }
#ifdef PORT_RFO_SAVEMARK
    /* The Reforged Options save mark, for this image, before the .sav
       (reforged_savemark.c says why), and only when the .sav may be written. */
    if (!readOnly && check == 0)
        Reforged_SaveMarkWritten(sWritePath);
#endif
    if (readOnly || check < 0 || Platform_SaveFileAtomic(sWritePath, sTmpPath, sram, SAVE_SIZE) != 0)
    {
        sFailures++;
        if (sFailRun++ < MAX_FAIL_LOGS)
        {
            if (readOnly)
                PortLog("[save] frame %u: '%s' may not be written (%s), so it is not replaced; trying again in "
                        "%u frames%s\n", gPortFrameCount, sWritePath, strerror(err), RETRY_FRAMES,
                    sFailRun == MAX_FAIL_LOGS ? " (no more of these lines until a write works)" : "");
            else if (check < 0)
                PortLog("[save] frame %u: cannot look at '%s' (%s) to check it before writing, so it is not "
                        "replaced; trying again in %u frames%s\n", gPortFrameCount, sWritePath, strerror(err),
                    RETRY_FRAMES, sFailRun == MAX_FAIL_LOGS ? " (no more of these lines until a write works)" : "");
            else
                PortLog("[save] frame %u: cannot write '%s' (through '%s'); trying again in %u frames%s\n",
                    gPortFrameCount, sWritePath, sTmpPath, RETRY_FRAMES,
                    sFailRun == MAX_FAIL_LOGS ? " (no more of these lines until a write works)" : "");
        }
        if (!sTitleFailed)
            SetFailNote(readOnly ? "read-only file" : "cannot write", sWritePath);
        sRetryFrame = gPortFrameCount + RETRY_FRAMES;
        return;
    }
    memcpy(sFileImage, sram, SAVE_SIZE);
    sStamp.there = 1;
    sStamp.known = 1;
    sStamp.size = SAVE_SIZE;
    sStamp.hash = Hash(sram, SAVE_SIZE);
    if (StatFile(sWritePath, &sStamp.size, &sStamp.mtime) != 0 || sStamp.size != SAVE_SIZE)
    {
        sStamp.size = SAVE_SIZE; /* gone or changed already: the next check says so */
        sStamp.mtime = 0;
    }
    sWrites++;
    PortLog("[save] frame %u: wrote '%s' (%u bytes changed, %.1f ms)%s\n", gPortFrameCount, sWritePath, changed,
        (double)(Platform_NowNs() - t0) / 1e6, sFailRun != 0 ? " after failed writes" : "");
    if (sTitleFailed)
    {
        sFailNote[0] = '\0';
        sTitleFailed = 0;
        ShowTitle();
    }
    sFailRun = 0;
    sRetryFrame = 0;
}

void PortSave_Frame(void)
{
    if (!sOn || sNoSaving != SAVING || memcmp(Sram(), sFileImage, SAVE_SIZE) == 0)
        return;
    if (sRetryFrame != 0 && (int32_t)(gPortFrameCount - sRetryFrame) < 0)
        return;
    Write();
}

void PortSave_Flush(void)
{
    int behind;

    if (!sOn)
        return;
    if (sNoSaving == SAVING && memcmp(Sram(), sFileImage, SAVE_SIZE) != 0)
        Write();
    behind = memcmp(Sram(), sFileImage, SAVE_SIZE) != 0;
    if (sFailures != 0 || sNoSaving != SAVING || behind)
        PortLog("[save] '%s': %u writes, %u failed%s%s\n", sWritePath, sWrites, sFailures,
            sNoSaving == NOSAVE_BUSY      ? "; not saving: in use by another copy"
            : sNoSaving == NOSAVE_CHANGED ? "; not saving: changed by another program"
                                          : "",
            behind ? "; THE LAST SRAM CHANGES ARE NOT IN THE FILE" : "");
}
