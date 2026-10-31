#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../platform.h"

/*
 * Headless backend (port-plan/PLAN.md section 3.7): no window, no audio
 * device and no live keys. It is the backend of port/Makefile's builds
 * (fe8pc and the Gate 0 test build), so their behaviour is what it was before
 * the platform interface existed:
 *   - keys come only from FE8PC_AUTOPRESS (port/hw/frame.c) or the Gate 0
 *     key file (port/test/pc/gate0_hook.c);
 *   - frames are not shown; the tests read them from the renderer
 *     (FE8PC_DUMP_FRAMES, GATE0_SHOTDIR);
 *   - the pacer is off unless FE8PC_PACE=1 (port/hw/pacer.c), so frames run
 *     back to back.
 * port/Makefile.sdl builds port/sdl3/platform_sdl3.c instead of this file.
 */

int Platform_Init(void)
{
    return 0;
}

void Platform_Shutdown(void)
{
}

int Platform_IsInteractive(void)
{
    return 0;
}

void Platform_PresentFrame(const uint32_t *rgba, int width, int height)
{
    (void)rgba;
    (void)width;
    (void)height;
}

int Platform_PollEvents(void)
{
    return 0;
}

uint16_t Platform_ReadKeys(void)
{
    return 0;
}

/* Audio: no device. The samples are still made every VBlank; FE8PC_AUDIO_DUMP
   writes them to files (port/audio/fifo_pc.c). */
int Platform_OpenAudio(uint32_t rateHz, uint32_t channels)
{
    (void)rateHz;
    (void)channels;
    return 0;
}

void Platform_QueueAudio(const int16_t *lr, uint32_t frames)
{
    (void)lr;
    (void)frames;
}

uint32_t Platform_AudioQueued(void)
{
    return 0;
}

void Platform_SetAudioRatio(double ratio)
{
    (void)ratio;
}

void Platform_PauseAudio(int pause)
{
    (void)pause;
}

uint64_t Platform_NowNs(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

void Platform_SleepUntilNs(uint64_t deadlineNs)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(deadlineNs / 1000000000u);
    ts.tv_nsec = (long)(deadlineNs % 1000000000u);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
        ;
}

void *Platform_LoadFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long len;
    void *data = NULL;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0)
    {
        data = malloc(len > 0 ? (size_t)len : 1);
        if (data != NULL && fread(data, 1, (size_t)len, f) != (size_t)len)
        {
            free(data);
            data = NULL;
        }
        if (data != NULL && size != NULL)
            *size = (size_t)len;
    }
    fclose(f);
    return data;
}

void Platform_FreeFile(void *data)
{
    free(data);
}

int Platform_SaveFileAtomic(const char *path, const char *tmp, const void *data, size_t size)
{
    FILE *f;
    int ok;

    f = fopen(tmp, "wb");
    if (f == NULL)
        return -1;
    ok = fwrite(data, 1, size, f) == size;
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) != 0)
    {
        remove(tmp);
        return -1;
    }
    return 0;
}

/* No window: the log has the line the window build puts in its title. */
void Platform_SetTitleNote(const char *note)
{
    (void)note;
}

/* FE8PC_USER_DIR, or none (NULL; never the current directory in its place).
   Nothing headless uses it: a headless build keeps a save only where --save
   names it, so this logs nothing. */
const char *Platform_UserDir(void)
{
    static char sDir[4096];
    const char *dir = getenv("FE8PC_USER_DIR");
    size_t n;

    if (dir == NULL || dir[0] == '\0')
        return NULL;
    n = strlen(dir);
    if (n + 2 > sizeof(sDir))
        return NULL;
    memcpy(sDir, dir, n);
    if (sDir[n - 1] != '/')
        sDir[n++] = '/';
    sDir[n] = '\0';
    return sDir;
}

/* Nothing to make: the folder FE8PC_USER_DIR names is used as it is (never
   called headless, as above). */
int Platform_MakeUserDir(void)
{
    return Platform_UserDir() != NULL ? 0 : -1;
}
