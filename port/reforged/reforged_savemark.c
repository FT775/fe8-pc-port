#include "global.h"

/*
 * Reforged Options, "saves marked" (port-plan/reforged-next-briefs.md item 5;
 * the user's rule "a save made with gameplay-changing options or cheats is
 * marked as such", reforged-options-plan.md). Played builds only, empty in
 * every test build.
 *
 * The mark is a sidecar text file beside the save, "<sav>.reforged" (the file
 * the writes replace, port/save/savefile_pc.c sWritePath, plus ".reforged").
 * The .sav stays the raw 32 KiB image mGBA reads: nothing here touches SRAM.
 *
 * Per save block (game slots 1-3, the two suspend blocks; include/bmsave.h
 * SAVE_ID_GAME0..SAVE_ID_SUSPEND_ALT) it records, when the game writes that
 * block (src/bmsave.c WriteGameSave, WriteNewGameSave, CopyGameSave,
 * WriteSuspendSave; InvalidateGameSave and InvalidateSuspendSave clear it):
 *   - the gameplay options that were not at their defaults
 *     (Reforged_OptionIsGameplay, the settings table's "gameplay" column:
 *     everything but the display-only options, so every cheat counts;
 *     Reforged_OptionDefault), with their values;
 *     The Game fixes (RFO_FIX_*) on that line are the run's, fixed at New Game
 *     (reforged_gamefixes.c Reforged_RunFix), not the menu's; loading the block puts
 *     them back as the run's (none recorded: every fix off).
 *   - Casual mode's fallen list (reforged_casual.c), so a suspended chapter
 *     keeps it, and a game save made after the final chapter (no chapter
 *     change before the ending) still brings its fallen units back.
 *   - the randomizer's run (reforged_rando.c gRandoRun: its algorithm version,
 *     seed and every RFO_RANDO_* value) when the run being played is
 *     randomized, so the block's tables are rebuilt when it is loaded, whatever
 *     the Randomizer screen says by then; or, with no run applied, the run this
 *     build refused (another randomizer version, reforged_rando.c gRandoCarried),
 *     so saving such a game never erases its run. The RFO_RANDO_* options are not
 *     gameplay options of the generic line (Reforged_OptionIsGameplay is 0 for
 *     them): they are the menu's settings for the next new game, not the run's.
 *   - Skills' kept skills (reforged_skills.c Reforged_SkillKept: the skill each
 *     promoted character carried over from its previous class), "<block>_kept =
 *     14:Wrath 20:Str_+2" (character id:skill name, spaces as '_'), only when one
 *     is recorded; a file without the line (older ones) keeps nothing.
 *   - "Ruin floor select" (reforged_ruins.c): the best Lagdou floor ever cleared, "<block>_ruins =
 *     best=7" (" partial": the run being played started past floor 1), only when one is; none: 0.
 *     v1.8.0's "<block>_ruins = 3 post run=..." reads as best=3. Stored data, not a mark: a block
 *     with only this line gets an empty "<block> =" line and its fingerprint, and is never logged
 *     as marked.
 *   - The FE7 classes (reforged_fe7.c, v1.15.0): a block's units in an FE7 class, "<block>_fe7 =
 *     1:0x82 2:0x83:bow=31" (unit index:class, then the weapon ranks the save wrote as 0 because the
 *     stand-in class cannot use that kind, as kind=rank with ',' between). The save holds each one's FE8
 *     fallback class (Reforged_Fe7Fallback: SRAM never gets an id past 0x7F) and no such rank, so a build
 *     or a run without the FE7 classes reads a plain stand-in. Loading the block gives class and ranks
 *     back when the FE7 classes are loaded (else the unit stays the stand-in, without those ranks). A
 *     chapter save also keeps the class in the units' own flag bits (src/bmsave.c), so its class comes
 *     back without this line, only the ranks do not. Stored data, not a mark, as "_ruins".
 * Loading a block (ReadGameSave, ReadSuspendSave) puts its fallen list and kept skills back;
 * just before, Reforged_RandoWillRead takes its run (Rando_SaveMarkBlock).
 *
 * The file is (re)written just before a .sav write (savefile_pc.c Write, once
 * the .sav is known to be writable), when what it would hold changed,
 * atomically (PATH.tmp.<pid>, then a rename, as the save and the settings
 * file are). Before, not after: the .sav that holds a retreated unit as dead
 * is then never on disk before its fallen list was written (a sidecar write
 * that fails is logged and tried again at the next .sav write; the .sav is
 * written anyway). A sidecar left ahead of its .sav (a failed .sav write,
 * retried; the process killed between the two) does not match that .sav's
 * fingerprints and is dropped when read (below), as the other order would
 * have lost the new entry too. It exists only while some
 * block in the .sav was written with a gameplay option on, holds fallen
 * units or was written by a randomized run: a run that never turns one on
 * never makes it; when the last marked block is written again with every
 * gameplay option at its default and no randomizer (or erased), the file is
 * removed. Read once at start (PortSave_Init, before
 * the game loads anything); when that was the save loaded from the other
 * place (the per-user folder or the ROM's, savefile_pc.c loadOther), the
 * next .sav write writes it beside the new .sav even if nothing in it
 * changed. A bad line is skipped (logged).
 *
 * Each marked block also carries a fingerprint, <block>_check: the checksum the
 * game stored for that block (its SaveBlockInfo checksum32, which covers the
 * whole block) in the .sav this text was written for. When the sidecar is
 * read, an entry whose fingerprint is not that of the loaded .sav's block (the
 * .sav was played in mGBA, or a backup put back, while an old sidecar stayed
 * beside it), or that has none (a format 1 file), is dropped, marks, fallen
 * list and randomizer run all, so a stale sidecar never brings back a unit the
 * save holds as dead (and that block then loads with the vanilla tables).
 * The check is made there, once, with the .sav image just loaded and before the
 * game reads any block (so ReadGameSave / ReadSuspendSave, through
 * Reforged_SaveBlockRead, only ever see checked entries), and not at each read:
 * the game itself re-checksums blocks with no hook here (bmsave-bwl.c's loss
 * counter rewrites both the suspend's and the game slot's SaveBlockInfo).
 * The fingerprints written are those of the SRAM image being written, so the
 * game's own changes are followed at every write.
 *
 *     # comment
 *     format = 2
 *     game1 = casual_mode=1 unbreakable_weapons=1
 *     game1_check = 0x1A2B3C4D
 *     suspend = casual_mode=1
 *     suspend_check = 0x5E6F7081
 *     suspend_fallen = 4,3
 *     suspend_rando = v=1 seed=0x1234ABCD randomizer=1 randomizer_growths=1 ...
 *     suspend_kept = 14:Wrath 20:Str_+2
 *
 * <block>_rando lists every RFO_RANDO_* key (Reforged_OptionKey) with its value; on
 * reading, an unknown key is skipped, a missing one is 0, and the entry counts only
 * when it has both v= and seed=. A build without the randomizer skips the key.
 */

#if PORT_PLAYED_BUILD

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "bmsave.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_rando.h"
#include "reforged_skills.h"
#include "../platform.h"

#define MARK_BLOCKS   (SAVE_ID_SUSPEND_ALT + 1)
#define MARK_FALLEN   64
#define MARK_TEXT_MAX 16384 /* 8192 before the randomizer's lines (five blocks of every RFO_RANDO_* key) */
#define MARK_SUFFIX   ".reforged"
#define MARK_KEPT     0x100 /* the kept skills by character id (reforged_skills.c SK_KEPT_PIDS) */
#define MARK_FE7      REFORGED_FE7_MARKS /* units in an FE7 class per block (reforged_fe7.c) */

struct MarkBlock {
    u8 values[RFO_COUNT]; /* the gameplay options' values when the block was written (others: their defaults) */
    u8 fallen[MARK_FALLEN];
    u8 fallenCount;
    u8 hasCheck;          /* the file gave <block>_check (used only while the file is read) */
    u32 check;
    u8 hasRando;          /* the block was written by a randomized run: rando is that run */
    struct RandoConfig rando;
    u8 kept[MARK_KEPT];   /* Skills' kept skill per character id (0: none) when the block was written */
    u8 ruins;             /* Ruin floor select: the best Lagdou floor cleared, + 0x10 a part-way run (Reforged_RuinsGet) */
    u8 fe7Count;          /* the FE7 classes: the block's units in one (class, ranks the save left out) */
    struct Reforged_Fe7Mark fe7[MARK_FE7];
};

static const char *const sBlockNames[MARK_BLOCKS] = {
    [SAVE_ID_GAME0] = "game1",
    [SAVE_ID_GAME1] = "game2",
    [SAVE_ID_GAME2] = "game3",
    [SAVE_ID_SUSPEND] = "suspend",
    [SAVE_ID_SUSPEND_ALT] = "suspend_alt",
};

static struct MarkBlock sBlocks[MARK_BLOCKS];

/* "<block>_fe7": the weapon kinds' names, by ITYPE_* */
static const char *const sFe7Kinds[8] = { "sword", "lance", "axe", "bow", "staff", "anima", "light", "dark" };

/* "Rewind" (reforged_rewind.c): the blocks as they are, and back. */
typedef char MarkStateFits[sizeof(sBlocks) <= REFORGED_SAVEMARK_STATE_MAX ? 1 : -1];

void Reforged_SaveMarkStateGet(void *out)
{
    memcpy(out, sBlocks, sizeof(sBlocks));
}

void Reforged_SaveMarkStateSet(const void *in)
{
    memcpy(sBlocks, in, sizeof(sBlocks));
}

void Reforged_SaveMarkStateSetSuspend(const void *in)
{
    const struct MarkBlock *b = (const struct MarkBlock *)in;

    sBlocks[SAVE_ID_SUSPEND] = b[SAVE_ID_SUSPEND];
    sBlocks[SAVE_ID_SUSPEND_ALT] = b[SAVE_ID_SUSPEND_ALT];
}

/* The Nuzlocke graveyards (reforged_rando_nuzlocke.c): not per block but per run, so a
   death stays whichever block is loaded later ("nuzlocke = <seed> <playthrough id>
   <pids>", one line each). Written with the blocks, and at once when one grows. */
#define MARK_GRAVES 8
struct Grave {
    u32 seed;
    u8 play;
    u8 count;
    u8 pids[MARK_FALLEN];
};
static struct Grave sGraves[MARK_GRAVES];
static int sGraveCount;
static char sSavPath[4096]; /* the .sav the game writes (savefile_pc.c sWritePath) */

static char sLastText[MARK_TEXT_MAX]; /* what the file holds ("" for no file) */
static char sScratch[MARK_TEXT_MAX];  /* BuildText's output */

/* The fingerprint of a save block in the SRAM image now: 1 and its stored
   checksum32 when the block's SaveBlockInfo is a valid game or suspend one,
   else 0. Read straight from the image, as the .sav holds it (PortSave_Init
   runs before the game sets up its SRAM functions). */
static int BlockFingerprint(int block, u32 *out)
{
    struct SaveBlockInfo info;

    memcpy(&info, &gSram->saveBlockInfo[block], sizeof(info));
    if (info.magic16 != SAVEMAGIC16 || info.magic32 != SAVEMAGIC32 ||
        info.kind != (block >= SAVE_ID_SUSPEND ? SAVEBLOCK_KIND_SUSPEND : SAVEBLOCK_KIND_GAME))
        return 0;
    *out = info.checksum32;
    return 1;
}

/* 1 when the block was written with this option changed from its default
   (a gameplay option only). */
static int ValueMarked(const struct MarkBlock *b, int id)
{
    return Reforged_OptionIsGameplay(id) && b->values[id] != Reforged_OptionDefault(id);
}

static int BlockMarked(const struct MarkBlock *b)
{
    int id;

    if (b->fallenCount != 0 || b->hasRando)
        return 1;
    for (id = 0; id < MARK_KEPT; id++)
        if (b->kept[id] != 0)
            return 1;
    for (id = 0; id < RFO_COUNT; id++)
        if (ValueMarked(b, id))
            return 1;
    return 0;
}

/* The block has something to keep in the file: a mark, or Ruin floor select's best floor (stored data, not
   a gameplay mark: it is counted with the option off too). */
static int BlockHasData(const struct MarkBlock *b)
{
    return BlockMarked(b) || b->ruins != 0 || b->fe7Count != 0;
}

/* No mark: every option at its default, no fallen units, no fingerprint, no
   randomizer run. */
static void ClearBlock(struct MarkBlock *b)
{
    int id;

    memset(b, 0, sizeof(*b));
    for (id = 0; id < RFO_COUNT; id++)
        b->values[id] = (u8)Reforged_OptionDefault(id);
}

void Reforged_SaveBlockWritten(int block)
{
    struct MarkBlock *b;
    int id;

    if (block < 0 || block >= MARK_BLOCKS)
        return;
    b = &sBlocks[block];
    for (id = 0; id < RFO_COUNT; id++)
        b->values[id] = (u8)(Reforged_OptionIsGameplay(id) ? Reforged_GetOption(id) : Reforged_OptionDefault(id));
    /* the Game fixes: the run's (fixed at New Game), never the menu's */
    for (id = RFO_FIX_FIRST; id <= RFO_FIX_LAST; id++)
        b->values[id] = (u8)Reforged_RunFix(id);
    b->fallenCount = (u8)Reforged_CasualGetFallen(b->fallen, MARK_FALLEN);
    /* the run being played, never the menu's settings (those are for the next new game);
       with none, a run carried (refused: another randomizer version) is kept, not erased */
    b->hasRando = (u8)(gRandoRunOn != 0 || gRandoCarriedOn != 0);
    if (gRandoRunOn)
        b->rando = gRandoRun;
    else if (gRandoCarriedOn)
    {
        b->rando = gRandoCarried;
        PortLog("[rando] %s written with the vanilla tables: it keeps the run's randomizer entry (version %d, seed "
                "0x%08X) that this build (version %d) could not apply\n", sBlockNames[block], gRandoCarried.version,
            (unsigned)gRandoCarried.seed, RANDO_VERSION);
    }
    else
        memset(&b->rando, 0, sizeof(b->rando));
    /* Skills: the run's kept skills (all 0 when Skills is off) */
    for (id = 0; id < MARK_KEPT; id++)
        b->kept[id] = (u8)Reforged_SkillKept(id);
    b->ruins = (u8)Reforged_RuinsGet(); /* Ruin floor select: the best Lagdou floor cleared */
    /* the FE7 classes: the block's units in one (a game slot: as WriteGameSave packed them; none for a
       new game, which packs no unit) */
    b->fe7Count = (u8)(block >= SAVE_ID_SUSPEND ? Reforged_Fe7SuspendUnits(b->fe7, MARK_FE7)
                                                : Reforged_Fe7TakePacked(b->fe7, MARK_FE7));
}

const struct RandoConfig *Rando_SaveMarkBlock(int block)
{
    if (block < 0 || block >= MARK_BLOCKS || !sBlocks[block].hasRando)
        return NULL;
    return &sBlocks[block].rando;
}

void Reforged_SaveBlockRead(int block)
{
    Reforged_RewindForget(); /* "Rewind": a load other than a rewind's own forgets the kept turns */
    int i;

    if (block < 0 || block >= MARK_BLOCKS)
        return;
    /* the Game fixes this block's run was started with (none recorded: every fix off). FE8PC_FIXES_FORCE
       (a test switch, for scripted runs on saves with no save mark): the Game fixes screen's instead. */
    for (i = RFO_FIX_FIRST; i <= RFO_FIX_LAST; i++)
        Reforged_SetRunFix(i, getenv("FE8PC_FIXES_FORCE") != NULL ? Reforged_GetOption(i) : sBlocks[block].values[i]);
    Reforged_Promo3Tables(); /* "Third promotion" (v1.23.0): the twins' prf locks as this block's fix says */
    Reforged_CasualSetFallen(sBlocks[block].fallen, sBlocks[block].fallenCount);
    /* Skills: the block's kept skills (none recorded, an older file: nothing kept) */
    Reforged_SkillKeptReset();
    for (i = 1; i < MARK_KEPT; i++)
        if (sBlocks[block].kept[i] != 0)
            Reforged_SkillKeptSet(i, sBlocks[block].kept[i]);
    Reforged_RuinsSet(sBlocks[block].ruins); /* Ruin floor select (none recorded: none cleared) */
    if (sBlocks[block].fe7Count != 0)
        Reforged_Fe7MarkRestore(sBlocks[block].fe7, sBlocks[block].fe7Count, block >= SAVE_ID_SUSPEND);
    if (sBlocks[block].ruins != 0)
        PortLog("[reforged] ruins: %s loaded: best Lagdou floor cleared %d%s\n", sBlockNames[block],
            sBlocks[block].ruins & 0x0F, (sBlocks[block].ruins & 0x10) ? ", a run started part-way" : "");
    for (i = 0; i < sBlocks[block].fallenCount; i++)
        PortLog("[reforged] casual mode: %s loaded: pid 0x%02X is on the fallen list\n", sBlockNames[block],
            sBlocks[block].fallen[i]);
    if (block >= SAVE_ID_SUSPEND)
        Reforged_CasualTestFall();
    Reforged_RandoBlockLoaded(); /* Nuzlocke: the run's dead stay dead */
}

static struct Grave *FindGrave(unsigned seed, int play)
{
    int i;

    for (i = 0; i < sGraveCount; i++)
        if (sGraves[i].seed == seed && sGraves[i].play == (u8)play)
            return &sGraves[i];
    return NULL;
}

int Reforged_GraveGet(unsigned seed, int play, unsigned char *out, int max)
{
    const struct Grave *g = FindGrave(seed, play);
    int i;

    for (i = 0; g != NULL && i < g->count && i < max; i++)
        out[i] = g->pids[i];
    return i;
}

void Reforged_GraveClear(unsigned seed, int play)
{
    struct Grave *g = FindGrave(seed, play);

    if (g == NULL)
        return;
    *g = sGraves[--sGraveCount];
}

void Reforged_GraveAdd(unsigned seed, int play, int pid)
{
    struct Grave *g = FindGrave(seed, play);
    int i;

    if (pid <= 0 || pid >= 0x100)
        return;
    if (g == NULL)
    {
        if (sGraveCount == MARK_GRAVES)
        {
            memmove(&sGraves[0], &sGraves[1], sizeof(sGraves[0]) * (MARK_GRAVES - 1)); /* the oldest run goes */
            sGraveCount--;
        }
        g = &sGraves[sGraveCount++];
        memset(g, 0, sizeof(*g));
        g->seed = seed;
        g->play = (u8)play;
    }
    for (i = 0; i < g->count; i++)
        if (g->pids[i] == pid)
            return;
    if (g->count < MARK_FALLEN)
        g->pids[g->count++] = (u8)pid;
    if (sSavPath[0] != 0)
        Reforged_SaveMarkWritten(sSavPath); /* now: a reset must not bring the unit back */
}

void Reforged_SaveBlockCopied(int from, int to)
{
    if (from < 0 || from >= MARK_BLOCKS || to < 0 || to >= MARK_BLOCKS)
        return;
    sBlocks[to] = sBlocks[from];
}

void Reforged_SaveBlockCleared(int block)
{
    if (block < 0 || block >= MARK_BLOCKS)
        return;
    ClearBlock(&sBlocks[block]);
}

/* "<block>_kept = 14:Wrath 20:Str_+2": Skills' kept skills by character id (decimal) and skill
   name (spaces written as '_'), only when the block has one; len unchanged when it has none. */
static int KeptLine(char *buf, size_t size, int len, int block)
{
    const struct MarkBlock *b = &sBlocks[block];
    int pid, any = 0;
    const char *name;

    for (pid = 1; pid < MARK_KEPT && len < (int)size; pid++)
    {
        if (b->kept[pid] == 0 || (name = Reforged_SkillName(b->kept[pid]))[0] == 0)
            continue;
        if (!any)
            len += snprintf(buf + len, size - len, "%s_kept =", sBlockNames[block]);
        any = 1;
        if (len < (int)size)
            len += snprintf(buf + len, size - len, " %d:", pid);
        for (; *name != 0 && len < (int)size - 1; name++)
            buf[len++] = *name == ' ' ? '_' : *name;
        if (len < (int)size)
            buf[len] = 0;
    }
    if (any && len < (int)size)
        len += snprintf(buf + len, size - len, "\n");
    return len;
}

/* The file's text for sBlocks, or "" when no block is marked. */
static int BuildText(char *buf, size_t size)
{
    int len = 0, block, id, i, any = 0, anyRando = 0, anyKept = 0;

    for (block = 0; block < MARK_BLOCKS; block++)
    {
        any |= BlockHasData(&sBlocks[block]);
        anyRando |= sBlocks[block].hasRando;
        for (id = 1; id < MARK_KEPT; id++)
            anyKept |= sBlocks[block].kept[id] != 0;
    }
    any |= sGraveCount != 0;
    buf[0] = 0;
    if (!any)
        return 0;

    /* (the randomizer's comment line only when a block has a run, so a file without
       one is the same text as before the randomizer) */
    len = snprintf(buf, size,
        "# FE8 PC port: Reforged Options save mark (not read by the GBA game or by mGBA).\n"
        "# game1-game3 are the save menu's slots 1-3; suspend and suspend_alt the two\n"
        "# suspend blocks. Each block below was written with these gameplay options on;\n"
        "# <block>_fallen: Casual mode's fallen units (character ids) that come back\n"
        "# at the next chapter change. <block>_check: the block's checksum in the .sav\n"
        "# this was written for; an entry that does not match the .sav is ignored.\n"
        "%s%s"
        "format = 2\n",
        anyRando ? "# <block>_rando: the randomized run the block was written by (randomizer\n"
                   "# version v, seed, its options); its tables are rebuilt when the block is loaded.\n"
                 : "",
        anyKept ? "# <block>_kept: Skills' kept skills of promoted units (character id:skill).\n" : "");
    for (block = 0; block < MARK_BLOCKS && len < (int)size; block++)
    {
        const struct MarkBlock *b = &sBlocks[block];

        if (!BlockHasData(b))
            continue;
        len += snprintf(buf + len, size - len, "%s =", sBlockNames[block]);
        for (id = 0; id < RFO_COUNT && len < (int)size; id++)
            if (ValueMarked(b, id) && Reforged_OptionKey(id) != NULL) /* (every id has a key today) */
                len += snprintf(buf + len, size - len, " %s=%d", Reforged_OptionKey(id), b->values[id]);
        if (len < (int)size)
            len += snprintf(buf + len, size - len, "\n");
        if (len < (int)size)
        {
            u32 check;

            if (BlockFingerprint(block, &check))
                len += snprintf(buf + len, size - len, "%s_check = 0x%08X\n", sBlockNames[block], (unsigned)check);
            else
                len += snprintf(buf + len, size - len, "%s_check = none\n", sBlockNames[block]);
        }
        if (b->fallenCount != 0 && len < (int)size)
        {
            len += snprintf(buf + len, size - len, "%s_fallen = ", sBlockNames[block]);
            for (i = 0; i < b->fallenCount && len < (int)size; i++)
                len += snprintf(buf + len, size - len, i ? ",%d" : "%d", b->fallen[i]);
            if (len < (int)size)
                len += snprintf(buf + len, size - len, "\n");
        }
        if (b->hasRando && len < (int)size)
        {
            len += snprintf(buf + len, size - len, "%s_rando = v=%d seed=0x%08X", sBlockNames[block],
                b->rando.version, (unsigned)b->rando.seed);
            for (id = RFO_RANDO_FIRST; id <= RFO_RANDO_LAST && len < (int)size; id++)
                if (Reforged_OptionKey(id) != NULL)
                    len += snprintf(buf + len, size - len, " %s=%d", Reforged_OptionKey(id),
                        b->rando.v[id - RFO_RANDO_FIRST]);
            if (len < (int)size)
                len += snprintf(buf + len, size - len, "\n");
        }
        if (len < (int)size)
            len = KeptLine(buf, size, len, block);
        if (b->ruins != 0 && len < (int)size)
            len += snprintf(buf + len, size - len, "%s_ruins = best=%d%s\n", sBlockNames[block], b->ruins & 0x0F,
                (b->ruins & 0x10) ? " partial" : "");
        if (b->fe7Count != 0 && len < (int)size)
        {
            len += snprintf(buf + len, size - len, "%s_fe7 =", sBlockNames[block]);
            for (i = 0; i < b->fe7Count && len < (int)size; i++)
            {
                int t, any = 0;

                len += snprintf(buf + len, size - len, " %d:0x%02X", b->fe7[i].index, b->fe7[i].jid);
                for (t = 0; t < 8 && len < (int)size; t++)
                    if (b->fe7[i].mask & (1 << t))
                        len += snprintf(buf + len, size - len, "%c%s=%d", any++ ? ',' : ':', sFe7Kinds[t],
                            b->fe7[i].ranks[t]);
            }
            if (len < (int)size)
                len += snprintf(buf + len, size - len, "\n");
        }
    }
    for (i = 0; i < sGraveCount && len < (int)size; i++)
    {
        int k;

        len += snprintf(buf + len, size - len, "nuzlocke = 0x%08X %d ", (unsigned)sGraves[i].seed, sGraves[i].play);
        for (k = 0; k < sGraves[i].count && len < (int)size; k++)
            len += snprintf(buf + len, size - len, k ? ",%d" : "%d", sGraves[i].pids[k]);
        if (len < (int)size)
            len += snprintf(buf + len, size - len, "\n");
    }
    if (len >= (int)size)
    {
        buf[0] = 0;
        return -1;
    }
    return len;
}

/* "0x1234ABCD 3 4,5,9": a run's graveyard. */
static void ParseGrave(char *val)
{
    char *end, *p;
    unsigned long seed = strtoul(val, &end, 0);
    long play;
    struct Grave *g;

    if (end == val || sGraveCount == MARK_GRAVES)
        return;
    play = strtol(end, &p, 0);
    if (p == end || play < 0 || play > 255)
        return;
    g = &sGraves[sGraveCount++];
    memset(g, 0, sizeof(*g));
    g->seed = (u32)seed;
    g->play = (u8)play;
    while (*p != 0 && g->count < MARK_FALLEN)
    {
        long pid = strtol(p, &end, 0);

        if (end == p)
        {
            p++;
            continue;
        }
        if (pid > 0 && pid < 0x100)
            g->pids[g->count++] = (u8)pid;
        p = end;
    }
}

static int MarkPath(const char *savPath, char *out, size_t size)
{
    return savPath != NULL && snprintf(out, size, "%s%s", savPath, MARK_SUFFIX) < (int)size;
}

static char *Trim(char *s)
{
    char *end;

    while (*s == ' ' || *s == '\t')
        s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        *--end = 0;
    return s;
}

enum { LINE_VALUES, LINE_FALLEN, LINE_CHECK, LINE_RANDO, LINE_KEPT, LINE_RUINS, LINE_FE7 };

static int FindBlock(const char *name, int *what)
{
    int block;
    size_t n;

    for (block = 0; block < MARK_BLOCKS; block++)
    {
        n = strlen(sBlockNames[block]);
        if (strncmp(name, sBlockNames[block], n) != 0)
            continue;
        if (name[n] == 0)
        {
            *what = LINE_VALUES;
            return block;
        }
        if (strcmp(name + n, "_fallen") == 0)
        {
            *what = LINE_FALLEN;
            return block;
        }
        if (strcmp(name + n, "_check") == 0)
        {
            *what = LINE_CHECK;
            return block;
        }
        if (strcmp(name + n, "_rando") == 0)
        {
            *what = LINE_RANDO;
            return block;
        }
        if (strcmp(name + n, "_kept") == 0)
        {
            *what = LINE_KEPT;
            return block;
        }
        if (strcmp(name + n, "_ruins") == 0)
        {
            *what = LINE_RUINS;
            return block;
        }
        if (strcmp(name + n, "_fe7") == 0)
        {
            *what = LINE_FE7;
            return block;
        }
    }
    return -1;
}

/* "0x1A2B3C4D" (any strtoul base-0 number); "none" or a bad value: no fingerprint. */
static void ParseCheck(struct MarkBlock *b, const char *val)
{
    char *end;
    unsigned long v = strtoul(val, &end, 0);

    b->hasCheck = end != val && *end == 0;
    b->check = (u32)v;
}

static void ParseValues(struct MarkBlock *b, char *val, const char *path, int lineNo)
{
    char *tok, *p = val, *eq;
    int id;

    for (;;)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == 0)
            break;
        tok = p;
        while (*p != 0 && *p != ' ' && *p != '\t')
            p++;
        if (*p != 0)
            *p++ = 0;
        eq = strchr(tok, '=');
        if (eq != NULL)
            *eq++ = 0;
        for (id = 0; id < RFO_COUNT; id++)
            if (Reforged_OptionKey(id) != NULL && strcmp(tok, Reforged_OptionKey(id)) == 0)
                break;
        if (id == RFO_COUNT)
        {
            PortLog("[reforged] %s line %d: unknown option '%s': skipped\n", path, lineNo, tok);
            continue;
        }
        /* the value as written ("key" alone: 1); one at the option's default
           (a hand edit, "casual_mode=0") marks nothing */
        b->values[id] = (u8)(eq != NULL ? strtol(eq, NULL, 10) : 1);
    }
}

/* "v=1 seed=0x1234ABCD key=val ...": the run is kept only with both v and seed; an
   RFO_RANDO_* key that is missing stays 0, an unknown one is skipped. */
static void ParseRando(struct MarkBlock *b, char *val, const char *path, int lineNo)
{
    char *tok, *p = val, *eq, *end;
    int id, haveVersion = 0, haveSeed = 0;
    long n;

    memset(&b->rando, 0, sizeof(b->rando));
    b->hasRando = 0;
    for (;;)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == 0)
            break;
        tok = p;
        while (*p != 0 && *p != ' ' && *p != '\t')
            p++;
        if (*p != 0)
            *p++ = 0;
        eq = strchr(tok, '=');
        if (eq == NULL)
        {
            PortLog("[reforged] %s line %d: '%s' has no value: skipped\n", path, lineNo, tok);
            continue;
        }
        *eq++ = 0;
        if (strcmp(tok, "v") == 0)
        {
            n = strtol(eq, &end, 0);
            haveVersion = end != eq && *end == 0 && n >= 0 && n <= 255;
            b->rando.version = (u8)n;
            continue;
        }
        if (strcmp(tok, "seed") == 0)
        {
            unsigned long s = strtoul(eq, &end, 0);

            haveSeed = end != eq && *end == 0;
            b->rando.seed = (u32)s;
            continue;
        }
        for (id = RFO_RANDO_FIRST; id <= RFO_RANDO_LAST; id++)
            if (Reforged_OptionKey(id) != NULL && strcmp(tok, Reforged_OptionKey(id)) == 0)
                break;
        if (id > RFO_RANDO_LAST)
        {
            PortLog("[reforged] %s line %d: unknown randomizer option '%s': skipped\n", path, lineNo, tok);
            continue;
        }
        n = strtol(eq, NULL, 10);
        b->rando.v[id - RFO_RANDO_FIRST] = (u8)(n < 0 ? 0 : n > 255 ? 255 : n);
    }
    if (haveVersion && haveSeed)
        b->hasRando = 1;
    else
    {
        memset(&b->rando, 0, sizeof(b->rando));
        PortLog("[reforged] %s line %d: the randomizer entry has no %s: ignored (that block loads with the vanilla "
                "tables)\n", path, lineNo, haveVersion ? "seed" : "version");
    }
}

/* "14:Wrath 20:Str_+2" (a skill id number is taken too): a bad pair is skipped (logged). */
static void ParseKept(struct MarkBlock *b, char *val, const char *path, int lineNo)
{
    char *tok, *p = val, *colon, *end, *q;
    long pid, n;
    int sk;
    char name[32];

    memset(b->kept, 0, sizeof(b->kept));
    for (;;)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == 0)
            break;
        tok = p;
        while (*p != 0 && *p != ' ' && *p != '\t')
            p++;
        if (*p != 0)
            *p++ = 0;
        colon = strchr(tok, ':');
        end = tok;
        pid = colon != NULL ? strtol(tok, &end, 0) : 0;
        if (colon == NULL || end != colon || pid <= 0 || pid >= MARK_KEPT)
        {
            PortLog("[reforged] %s line %d: kept skill '%s' is not pid:skill: skipped\n", path, lineNo, tok);
            continue;
        }
        colon++;
        n = strtol(colon, &end, 0);
        if (end != colon && *end == 0)
            sk = n >= SK_FIRST_NEW && n < SK_COUNT ? (int)n : 0; /* FE8's own skills are never kept */
        else
        {
            snprintf(name, sizeof(name), "%s", colon);
            for (q = name; *q != 0; q++)
                if (*q == '_')
                    *q = ' ';
            for (sk = SK_FIRST_NEW; sk < SK_COUNT; sk++)
                if (strcasecmp(name, Reforged_SkillName(sk)) == 0)
                    break;
            if (sk == SK_COUNT)
                sk = 0;
        }
        if (sk == 0)
        {
            PortLog("[reforged] %s line %d: unknown kept skill '%s': skipped\n", path, lineNo, colon);
            continue;
        }
        b->kept[pid] = (u8)sk;
    }
}

static void ParseFallen(struct MarkBlock *b, char *val)
{
    char *p = val, *end;
    long pid;

    b->fallenCount = 0;
    while (*p != 0 && b->fallenCount < MARK_FALLEN)
    {
        pid = strtol(p, &end, 0);
        if (end == p)
        {
            p++;
            continue;
        }
        if (pid > 0 && pid < 0x100)
            b->fallen[b->fallenCount++] = (u8)pid;
        p = end;
    }
}

/* "1:0x82 2:0x83:bow=31": unit index:class, each an FE7 class (0x80-0x88) or, v1.19.0, an FE6 class (0x89-0x8C; v1.20.0: 0x8F)
   (v1.15.0: 0x80-0x83, v1.16.0-v1.17.0: 0x80-0x87, v1.18.0: 0x80-0x88 and v1.19.0: 0x80-0x8C, which skip the later ones, so its
   units stay their stand-ins there), and the weapon ranks the
   save left out (kind=rank, ',' between; v1.15.0 test builds wrote none); anything else is skipped. */
static void ParseFe7(struct MarkBlock *b, char *val)
{
    char *p = val, *end;
    long idx, jid, v;
    int t;

    b->fe7Count = 0;
    while (*p != 0 && b->fe7Count < MARK_FE7)
    {
        idx = strtol(p, &end, 0);
        if (end == p || *end != ':')
        {
            p = end == p ? p + 1 : end;
            continue;
        }
        p = end + 1;
        jid = strtol(p, &end, 0);
        if (end == p)
            continue;
        p = end;
        memset(&b->fe7[b->fe7Count], 0, sizeof(b->fe7[0]));
        while (*p == ':' || *p == ',')
        {
            size_t n = 0;

            p++;
            for (t = 0; t < 8; t++)
            {
                n = strlen(sFe7Kinds[t]);
                if (strncmp(p, sFe7Kinds[t], n) == 0 && p[n] == '=')
                    break;
            }
            if (t == 8)
                break;
            p += n + 1;
            v = strtol(p, &end, 0);
            if (end == p)
                break;
            p = end;
            if (v > 0 && v < 0x100)
            {
                b->fe7[b->fe7Count].mask |= (u8)(1 << t);
                b->fe7[b->fe7Count].ranks[t] = (u8)v;
            }
        }
        while (*p != 0 && *p != ' ' && *p != '\t')
            p++; /* the rest of a token not understood */
        if (idx > 0 && idx < 0x100 && jid >= 0x80 && jid <= REFORGED_FOREIGN_LAST_JID)
        {
            b->fe7[b->fe7Count].index = (u8)idx;
            b->fe7[b->fe7Count].jid = (u8)jid;
            b->fe7Count++;
        }
    }
}

void Reforged_SaveMarkLoad(const char *savPath, int loadedOther)
{
    char path[4200];
    size_t size = 0;
    char *data, *text, *line, *next;
    int lineNo = 0, block, what, len;

    for (block = 0; block < MARK_BLOCKS; block++)
        ClearBlock(&sBlocks[block]);
    sGraveCount = 0;
    sLastText[0] = 0;
    if (!MarkPath(savPath, path, sizeof(path)))
        return;
    data = Platform_LoadFile(path, &size);
    if (data == NULL)
        return; /* no mark: no block was written with a gameplay option on */
    if (size >= MARK_TEXT_MAX)
    {
        PortLog("[reforged] %s is larger than %d bytes: not read\n", path, MARK_TEXT_MAX - 1);
        Platform_FreeFile(data);
        return;
    }
    text = malloc(size + 1);
    if (text == NULL)
    {
        Platform_FreeFile(data);
        return;
    }
    memcpy(text, data, size);
    text[size] = 0;
    memcpy(sLastText, text, size + 1);
    Platform_FreeFile(data);

    for (line = text; line != NULL; line = next)
    {
        char *eq, *key, *val;

        next = strchr(line, '\n');
        if (next != NULL)
            *next++ = 0;
        lineNo++;
        line = Trim(line);
        if (*line == 0 || *line == '#' || *line == ';')
            continue;
        eq = strchr(line, '=');
        if (eq == NULL)
        {
            PortLog("[reforged] %s line %d: no '=': skipped\n", path, lineNo);
            continue;
        }
        *eq = 0;
        key = Trim(line);
        val = Trim(eq + 1);
        if (strcmp(key, "format") == 0)
            continue;
        if (strcmp(key, "nuzlocke") == 0)
        {
            ParseGrave(val);
            continue;
        }
        block = FindBlock(key, &what);
        if (block < 0)
        {
            PortLog("[reforged] %s line %d: unknown key '%s': skipped\n", path, lineNo, key);
            continue;
        }
        if (what == LINE_FALLEN)
            ParseFallen(&sBlocks[block], val);
        else if (what == LINE_CHECK)
            ParseCheck(&sBlocks[block], val);
        else if (what == LINE_RANDO)
            ParseRando(&sBlocks[block], val, path, lineNo);
        else if (what == LINE_KEPT)
            ParseKept(&sBlocks[block], val, path, lineNo);
        else if (what == LINE_FE7)
            ParseFe7(&sBlocks[block], val);
        else if (what == LINE_RUINS)
        {
            /* "best=7" ("partial": a run started part-way); v1.8.0's "3" / "3 post run=..." (the run's
               floors cleared) counts as best=3. A bad value is none. */
            const char *best = strstr(val, "best=");
            long n = strtol(best != NULL ? best + 5 : val, NULL, 10);

            sBlocks[block].ruins = (u8)(n > 0 && n <= 10 ? n | (best != NULL && strstr(val, "partial") != NULL ? 0x10 : 0) : 0);
        }
        else
            ParseValues(&sBlocks[block], val, path, lineNo);
    }
    free(text);

    /* Drop every entry that is not for the loaded .sav's block (see the top of
       this file). The text then differs from sLastText, so the next .sav write
       rewrites (or removes) the file. */
    for (block = 0; block < MARK_BLOCKS; block++)
    {
        struct MarkBlock *b = &sBlocks[block];
        u32 now = 0;
        int valid;

        if (!BlockHasData(b))
            continue;
        valid = BlockFingerprint(block, &now);
        if (b->hasCheck && valid && b->check == now)
            continue;
        if (!b->hasCheck)
            PortLog("[reforged] save mark: %s has no fingerprint (an older file): its mark and fallen list are "
                    "ignored\n", sBlockNames[block]);
        else if (!valid)
            PortLog("[reforged] save mark: %s is not a valid block in the .sav: its mark and fallen list are "
                    "ignored\n", sBlockNames[block]);
        else
            PortLog("[reforged] save mark: %s was written for another .sav (checksum 0x%08X, the .sav's 0x%08X: "
                    "played elsewhere, or a backup put back): its mark and fallen list are ignored\n",
                sBlockNames[block], (unsigned)b->check, (unsigned)now);
        if (b->hasRando)
            PortLog("[reforged] save mark: %s's randomizer run (seed 0x%08X) is dropped with it: that save will "
                    "load with the vanilla tables\n", sBlockNames[block], (unsigned)b->rando.seed);
        ClearBlock(b);
    }

    for (block = 0; block < MARK_BLOCKS; block++)
    {
        const struct MarkBlock *b = &sBlocks[block];

        if (!BlockMarked(b))
            continue;
        if (b->hasRando)
            PortLog("[reforged] save mark: %s was written by a randomized run (randomizer version %d, seed "
                    "0x%08X)%s (from %s)\n", sBlockNames[block], b->rando.version, (unsigned)b->rando.seed,
                b->fallenCount ? ", fallen units waiting" : "", path);
        else
            PortLog("[reforged] save mark: %s was written with gameplay options on%s (from %s)\n",
                sBlockNames[block], b->fallenCount ? ", fallen units waiting" : "", path);
    }
    /* compare with what this run would write, so an unchanged mark is not rewritten */
    len = BuildText(sScratch, sizeof(sScratch));
    if (len >= 0 && strcmp(sScratch, sLastText) != 0)
        sLastText[0] = '\x01'; /* differs (comments, order): the next .sav write rewrites it */
    /* Read from the other place: the writes go to another .sav, which has no
       sidecar yet (rfo-casual refute bug 2), so the next one writes it. */
    if (loadedOther && sLastText[0] != 0)
        sLastText[0] = '\x01';
}

void Reforged_SaveMarkSetWritePath(const char *savPath)
{
    if (savPath != NULL)
        snprintf(sSavPath, sizeof(sSavPath), "%s", savPath);
}

void Reforged_SaveMarkWritten(const char *savPath)
{
    char *text = sScratch;
    char path[4200], tmp[4300];
    int len;

    if (savPath != NULL && savPath != sSavPath)
        snprintf(sSavPath, sizeof(sSavPath), "%s", savPath);
    len = BuildText(text, sizeof(sScratch));
    if (len < 0)
    {
        PortLog("[reforged] the save mark is too long: not written\n");
        return;
    }
    if (strcmp(text, sLastText) == 0)
        return;
    if (!MarkPath(savPath, path, sizeof(path)) ||
        snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(tmp))
        return;

    if (len == 0)
    {
        int rc = remove(path);

        if (rc != 0 && errno != ENOENT) /* none there: a save loaded from the other place */
            PortLog("[reforged] could not remove the save mark %s\n", path);
        else if (rc == 0)
            PortLog("[reforged] no save block is marked now: removed %s\n", path);
        sLastText[0] = 0;
        return;
    }
    if (Platform_SaveFileAtomic(path, tmp, text, (size_t)len) != 0)
    {
        PortLog("[reforged] could not write the save mark %s (tried again at the next save)\n", path);
        return;
    }
    memcpy(sLastText, text, (size_t)len + 1);
    PortLog("[reforged] save mark written: %s\n", path);
}

#endif /* PORT_PLAYED_BUILD */
