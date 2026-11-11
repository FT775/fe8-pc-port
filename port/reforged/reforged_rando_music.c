#include "global.h"

/*
 * Reforged Options randomizer: "Random music" (RFO_RANDO_MUSIC, the user's request of
 * 2026-11-06: "random battle music is pretty fun"). Off by default; only what is heard
 * changes. In a run with it on:
 *   - Map music: each chapter's player, enemy and other-army themes (the songs
 *     GetCurrentMapMusicIndex gives: bm.c StartMapSongBgm, bmmind.c) are each a song from
 *     the pool of every map theme the chapters use, picked by the run's seed for that
 *     chapter and song, so a chapter always sounds the same in a run. "Grasp at
 *     Victory" (the few-enemies-left theme) stays, as a signal.
 *   - Battle music: the battle scene's attack and defense themes and the bosses' themes
 *     (banim-efxsound.c EkrPlayMainBGM) are each one of those battle songs, the same way.
 *     The arena, Tethys's dance, the staff and promotion songs stay.
 */

#if PORT_PLAYED_BUILD

#include "chapterdata.h"
#include "constants/songs.h"

#include "port_reforged.h"
#include "reforged_rando.h"

#define CHAPTERS   0x4F /* CHAPTER_0 .. CHAPTER_4E */
#define POOL_MAX   64

static u16 sMapPool[POOL_MAX];
static int sMapCount = -1;

static const u16 sBattlePool[] = {
    SONG_ATTACK, SONG_DEFENSE, SONG_THE_BATTLE_MUST_BE_WON, SONG_POWERFUL_FOE, SONG_THE_PRINCES_DESPAIR,
    SONG_RETURN_OF_THE_DEMON_KING,
};
#define BATTLE_COUNT ((int)(sizeof(sBattlePool) / sizeof(sBattlePool[0])))

static int InPool(const u16 *pool, int n, int song)
{
    int i;

    for (i = 0; i < n; i++)
        if (pool[i] == song)
            return 1;
    return 0;
}

static void BuildMapPool(void)
{
    static const u8 sSlots[] = { MAP_BGM_BLUE, MAP_BGM_RED, MAP_BGM_GREEN, MAP_BGM_BLUE_GREEN_ALT, MAP_BGM_RED_ALT };
    int ch, s;

    sMapCount = 0;
    for (ch = 0; ch < CHAPTERS; ch++)
        for (s = 0; s < (int)sizeof(sSlots); s++)
        {
            int song = GetROMChapterStruct(ch)->mapBgmIds[sSlots[s]];

            if (song == 0 || song == 0xFFFF || song == SONG_GRASP_AT_VICTORY || InPool(sBattlePool, BATTLE_COUNT, song))
                continue;
            if (!InPool(sMapPool, sMapCount, song) && sMapCount < POOL_MAX)
                sMapPool[sMapCount++] = (u16)song;
        }
}

static int On(void)
{
    return gRandoRunOn && Rando_Opt(RFO_RANDO_MUSIC);
}

/* The last substitutions (a song played in place of another), so PlaySong's Sound Room
   unlock (soundwrapper.c) records the song the game would have played: a random pick
   never unlocks a song not reached yet (the final chapters' themes). */
#define SUBS 4
static u16 sSubSong[SUBS], sSubOrig[SUBS];
static int sSubNext;

static int sPeek;

static int Note(int orig, int song)
{
    if (song != orig && !sPeek)
    {
        sSubSong[sSubNext] = (u16)song;
        sSubOrig[sSubNext] = (u16)orig;
        sSubNext = (sSubNext + 1) % SUBS;
    }
    return song;
}

int Reforged_RandoSoundRoomSong(int song)
{
    int i, j;

    if (!On())
        return song;
    for (i = 0; i < SUBS; i++)
    {
        j = (sSubNext - 1 - i + 2 * SUBS) % SUBS; /* newest first */
        if (sSubSong[j] != 0 && sSubSong[j] == song)
        {
            sSubSong[j] = 0;
            return sSubOrig[j];
        }
    }
    return song;
}

/* Reforged_RandoMapSongPeek: a comparison (phasechangefx.c), nothing played, nothing noted */
int Reforged_RandoMapSongPeek(int song)
{
    int r;

    sPeek = 1;
    r = Reforged_RandoMapSong(song);
    sPeek = 0;
    return r;
}

int Reforged_RandoMapSong(int song)
{
    if (!On())
        return song;
    if (sMapCount < 0)
        BuildMapPool();
    if (sMapCount == 0 || !InPool(sMapPool, sMapCount, song))
        return song;
    return Note(song, sMapPool[Rando_HashInt(Rando_Hash(RANDO_SALT_MUSIC, (u32)gPlaySt.chapterIndex, (u32)song, 0), sMapCount)]);
}

int Reforged_RandoBattleSong(int song)
{
    if (!On() || !InPool(sBattlePool, BATTLE_COUNT, song))
        return song;
    return Note(song, sBattlePool[Rando_HashInt(Rando_Hash(RANDO_SALT_MUSIC + 1, (u32)gPlaySt.chapterIndex, (u32)song, 0),
        BATTLE_COUNT)]);
}

/* The randomizer's self-test: off, every song is itself; on, every map theme stays a map
   theme and every battle song a battle song, the same twice. */
int RandoMusic_SelfTest(void)
{
    int fails = 0, i, ch;

    if (sMapCount < 0)
        BuildMapPool();
    if (sMapCount < 4)
    {
        PortLog("[rando] check: music: only %d map themes found\n", sMapCount);
        fails++;
    }
    if (!On())
    {
        for (i = 0; i < sMapCount; i++)
            fails += Reforged_RandoMapSong(sMapPool[i]) != sMapPool[i];
        return fails;
    }
    for (ch = 0; ch < CHAPTERS; ch += 7)
    {
        u8 saved = gPlaySt.chapterIndex;

        gPlaySt.chapterIndex = (u8)ch;
        for (i = 0; i < sMapCount; i++)
        {
            int out = Reforged_RandoMapSong(sMapPool[i]);

            fails += !InPool(sMapPool, sMapCount, out) || out != Reforged_RandoMapSong(sMapPool[i]);
        }
        for (i = 0; i < BATTLE_COUNT; i++)
            fails += !InPool(sBattlePool, BATTLE_COUNT, Reforged_RandoBattleSong(sBattlePool[i]));
        fails += Reforged_RandoMapSong(SONG_GRASP_AT_VICTORY) != SONG_GRASP_AT_VICTORY;
        gPlaySt.chapterIndex = saved;
    }
    for (i = 0; i < SUBS; i++)
        sSubSong[i] = 0; /* the checks' picks are not songs played */
    if (fails)
        PortLog("[rando] check: music: %d wrong picks\n", fails);
    return fails;
}

#endif /* PORT_PLAYED_BUILD */
