#include "global.h"

/*
 * Reforged Options, "Unbreakable weapons" (port-plan/reforged-options-plan.md
 * section 4, item 3; the user's decision of 2026-10-29: the player's units
 * only). Played builds only; empty in every test build.
 *
 * Called in place of GetItemAfterUse where a use is taken and the owner is
 * known: a battle hit (bmbattle.c BattleGenerateHitEffects), a staff or item
 * used in battle (BattleApplyItemEffect) and a map-side use
 * (bmunit.c UnitUpdateUsedItem: staves, keys, promotion items, consumables).
 * The ballista copies the battle weapon's uses (BattleApplyBallistaUpdates),
 * so it follows.
 *
 * Three values (the user's choice of 2026-10-29): Off; "Your units": with a
 * blue (player) owner; "Everyone": any owner. Then a weapon or staff (IA_WEAPON |
 * IA_STAFF, max uses above 0, not IA_UNBREAKABLE) comes back at its maximum
 * uses, the test cheat's rule (port/test/pc/gate0_hook.c CheatRefill).
 * Ordinary weapons, every staff (Hammerne, ITEM_STAFF_REPAIR, too) and Myrrh's Dragonstone
 * (ITEM_DIVINESTONE) are covered.
 *
 * Not covered (the user's decision of 2026-10-29, DECISIONS.md "Unbreakable
 * weapons: what's covered"): the nine legendary weapons (Sieglinde, Siegmund,
 * Audhulma, Vidofnir, Nidhogg, Garm, Excalibur, Ivaldi, Gleipnir) keep their
 * normal uses and wear down as in the original, so Hammerne still has a job.
 * They are picked by item ID, not by IA_UNSELLABLE: the Dragonstone is
 * unsellable too.
 *
 * Everything else (and with "Your units" every other faction's unit) takes the retail
 * GetItemAfterUse.
 *
 * FE8PC_RFO_CHECK=1 (the test switch of reforged_dangerzone.c) logs, at the
 * first use taken, what this returns for a few items one use short of full,
 * with a made-up blue and red owner, and the battle forecast's use count
 * (Reforged_ForecastUses, src/bksel.c) for each.
 */

#if PORT_PLAYED_BUILD

#include <string.h>

#include "bmunit.h"
#include "bmitem.h"
#include "bmbattle.h"
#include "constants/items.h"

#include "port_reforged.h"
#include "reforged_skills.h"

/* The nine legendary weapons, which wear down even with the option on. */
static bool32 Reforged_IsLegendaryWeapon(int item)
{
    switch (ITEM_INDEX(item))
    {
    case ITEM_SWORD_SIEGLINDE:
    case ITEM_LANCE_SIEGMUND:
    case ITEM_SWORD_AUDHULMA:
    case ITEM_LANCE_VIDOFNIR:
    case ITEM_BOW_NIDHOGG:
    case ITEM_AXE_GARM:
    case ITEM_ANIMA_EXCALIBUR:
    case ITEM_LIGHT_IVALDI:
    case ITEM_DARK_GLEIPNIR:
        return TRUE;

    default:
        return FALSE;
    }
}

void PortLog(const char *fmt, ...);
char *getenv(const char *name);

/* FE8PC_RFO_CHECK=1: the rule for a few items, each one use short of full,
   with a blue and a red owner. The owners are made up (only their faction is
   read); nothing of the game's is written. */
static void LogItemRules(void)
{
    static const u8 items[] = {
        ITEM_SWORD_IRON, ITEM_AXE_IRON, ITEM_STAFF_HEAL, ITEM_STAFF_REPAIR,
        ITEM_DIVINESTONE, ITEM_VULNERARY,
        ITEM_SWORD_SIEGLINDE, ITEM_LANCE_SIEGMUND, ITEM_SWORD_AUDHULMA,
        ITEM_LANCE_VIDOFNIR, ITEM_BOW_NIDHOGG, ITEM_AXE_GARM,
        ITEM_ANIMA_EXCALIBUR, ITEM_LIGHT_IVALDI, ITEM_DARK_GLEIPNIR,
    };
    static struct Unit blue, red;
    unsigned i;

    blue.index = FACTION_BLUE | 1;
    red.index = FACTION_RED | 1;
    PortLog("[reforged] unbreakable weapons check, mode %d\n", Reforged_GetOption(RFO_UNBREAKABLE));
    for (i = 0; i < sizeof(items) / sizeof(items[0]); i++)
    {
        int item = items[i] | ((GetItemMaxUses(items[i]) - 1) << 8);
        u16 b = Reforged_ItemAfterUse(item, &blue);
        u16 r = Reforged_ItemAfterUse(item, &red);

        PortLog("[reforged] item 0x%02X %d/%d -> blue 0x%02X %d, red 0x%02X %d; forecast uses blue %d, red %d; "
                "shown blue %d/%d, red %d/%d, no owner %d/%d\n",
            items[i], GetItemUses(item), GetItemMaxUses(item), ITEM_INDEX(b), b >> 8, ITEM_INDEX(r), r >> 8,
            Reforged_ForecastUses(item, &blue), Reforged_ForecastUses(item, &red),
            Reforged_ShownUses(item, &blue), Reforged_ShownMaxUses(item, &blue), Reforged_ShownUses(item, &red),
            Reforged_ShownMaxUses(item, &red), Reforged_ShownUses(item, NULL), Reforged_ShownMaxUses(item, NULL));
    }
}

/* Whether the option gives this item back at its maximum uses after a use (or the Game fix "Myrrh
   buff" keeps Myrrh's Dragonstone so). */
static bool32 Reforged_Refills(int item, struct Unit *owner)
{
    int mode = Reforged_GetOption(RFO_UNBREAKABLE); /* 1 your units, 2 everyone */

    if (Reforged_MyrrhStone(item, owner))
        return TRUE;

    return (mode == 2 || (mode == 1 && owner != NULL && UNIT_FACTION(owner) == FACTION_BLUE))
        && (GetItemAttributes(item) & (IA_WEAPON | IA_STAFF))
        && !(GetItemAttributes(item) & IA_UNBREAKABLE)
        && !Reforged_IsLegendaryWeapon(item)
        && GetItemMaxUses(item) > 0;
}

/* FE8PC_RFO_CHECK=1: the rules, once, at the first use taken or uses drawn. */
static void CheckOnce(void)
{
    static int checked;

    if (!checked)
    {
        checked = 1;
        if (getenv("FE8PC_RFO_CHECK") != NULL)
            LogItemRules();
    }
}

u16 Reforged_ItemAfterUse(int item, struct Unit *owner)
{
    CheckOnce();

    if (Reforged_Refills(item, owner))
        return ITEM_INDEX(item) | (GetItemMaxUses(item) << 8);

    return GetItemAfterUse(item);
}

/* The uses the battle forecast counts hits from (src/bksel.c
   InitBattleForecastBattleStats, retail: GetItemUses(weaponBefore)). A weapon
   the option refills is back at its maximum after every hit, so it never runs
   out in the battle: the forecast counts it as unlimited (review
   rv-unbreakable-sav161 UB-FORECAST: "x1" shown for a refilling weapon with
   1 use left, which really hits twice). Anything else: its real uses, as
   retail. */
int Reforged_ForecastUses(int item, struct Unit *owner)
{
    if (GetItemUses(item) > 0 && Reforged_Refills(item, owner))
        return 0xFF;

    return GetItemUses(item);
}

/*
 * The uses the screens show (include/bmitem.h RFO_SHOWN_USES and friends): "--"
 * (0xFF, PutNumberOrBlank's dashes, as the original shows its own unbreakable
 * items) for an item the option keeps at full; the real uses otherwise. owner
 * NULL is a screen of the player's own things (their units, the convoy, a
 * shop), so "Your units" hides it too; a known non-blue owner (the stat screen
 * of an enemy or green unit, a steal target) keeps its numbers in that mode.
 * The Hammerne screens keep the real numbers (repairing is about them; the
 * legendary weapons, which still wear down, show theirs everywhere).
 * Display only: nothing here is written but the owner the draws read.
 */
static struct Unit *sShownOwner;

void Reforged_SetShownItemOwner(struct Unit *owner)
{
    sShownOwner = owner;
}

struct Unit *Reforged_ShownItemOwner(void)
{
    return sShownOwner;
}

static bool32 UsesHidden(int item, struct Unit *owner)
{
    int mode = Reforged_GetOption(RFO_UNBREAKABLE); /* 1 your units, 2 everyone */

    if (Reforged_MyrrhStone(item, owner))
        return TRUE; /* Myrrh buff: shown as never breaking ("--") on her own screens */
    if (mode == 0 || item == 0)
        return FALSE;
    if (mode == 1 && owner != NULL && UNIT_FACTION(owner) != FACTION_BLUE)
        return FALSE;
    return (GetItemAttributes(item) & (IA_WEAPON | IA_STAFF))
        && !(GetItemAttributes(item) & IA_UNBREAKABLE)
        && !Reforged_IsLegendaryWeapon(item)
        && GetItemMaxUses(item) > 0;
}

int Reforged_ShownUses(int item, struct Unit *owner)
{
    CheckOnce();
    return UsesHidden(item, owner) ? 0xFF : GetItemUses(item);
}

int Reforged_ShownMaxUses(int item, struct Unit *owner)
{
    return UsesHidden(item, owner) ? 0xFF : GetItemMaxUses(item);
}

/*
 * FE7's items left in FE8's item table with the name "Dummy" (text 0x403) and the help
 * "DUMMY" (0x4AB) (the user's request of 2026-11-04: "all dummy items need to be removed
 * or redefined"). The original never hands them out; the randomizer did (Mine, Light Rune
 * as healing items before version 6, Luce as a B light tome before version 8). FE8 still
 * has their code (src/bmitemuse.c, bmusemind.c, bmtrick.c: the Mine and the Light Rune are
 * placed on a square next to the user, the rune blocks it 3 turns), so they are named
 * here: each name and help gets a virtual text id past the game's own (RFO_VMSG_BASE),
 * which GetStringFromIndex fills from this table instead of the message table. The
 * blank ones (0xBC-0xCD: no icon, no stats) are never given and keep theirs.
 */
#define RFO_VMSG_BASE 0x7F00

static const struct { u8 iid; const char *name, *desc; } sFe7Items[] = {
    /* each help line at most 90 pixels: the item menu's use-help panel is 12 tiles */
    { ITEM_SWORD_MKATTI, "Mani Katti", "Lyn's sacred blade.\x01" "Effective against\x01" "cavalry and armor." },
    { ITEM_ANIMA_FORBLAZE, "Forblaze", "Legendary fire tome." },
    { ITEM_LIGHT_LUCE, "Luce", "Light tome. Effective\x01" "against dragons." },
    { ITEM_MINE, "Mine", "Place next to you.\x01" "Hurts whoever\x01" "steps on it." },
    { ITEM_LIGHTRUNE, "Light Rune", "Place next to you.\x01" "Blocks that square\x01" "for 3 turns." },
    { ITEM_FILLAS_MIGHT, "Fila's Might", "A dancer's ring:\x01" "raises strength." },
    { ITEM_NINISS_GRACE, "Ninis's Grace", "A dancer's ring:\x01" "raises defense and\x01" "resistance." },
    { ITEM_THORS_IRE, "Thor's Ire", "A dancer's ring:\x01" "raises critical hits." },
    { ITEM_SETS_LITANY, "Set's Litany", "A dancer's ring:\x01" "raises dodge." },
    { ITEM_HEAVENSEAL, "Heaven Seal", "Lets a lord promote." },
};
#define FE7_ITEM_COUNT ((int)(sizeof(sFe7Items) / sizeof(sFe7Items[0])))

int Reforged_ItemTextId(int item, int id, int which)
{
    int iid = ITEM_INDEX(item), k;

    if (id != 0x403 && id != 0x4AB && id != 0)
        return id; /* the item has its own text */
    if (which == 2 && id == 0 && (GetItemAttributes(item) & IA_WEAPON))
        return 0; /* a weapon has no use help (the prep Use screen tests for 0) */
    for (k = 0; k < FE7_ITEM_COUNT; k++)
        if (sFe7Items[k].iid == iid)
            return RFO_VMSG_BASE + k * 2 + (which == 0 ? 0 : 1); /* the use help: the help */
    return id;
}

int Reforged_VirtualMsg(int index, char *buf)
{
    int k = index - RFO_VMSG_BASE;
    const char *s;

    /* Pair Up's Swap command (src/menu_def.c: RFO_VMSG_SWAP, _HELP) */
    if (index == RFO_VMSG_BASE + 0x40 || index == RFO_VMSG_BASE + 0x41)
    {
        strcpy(buf, index == RFO_VMSG_BASE + 0x40 ? "Swap" : "Switch the lead and\x01" "the partner.");
        return 1;
    }

    /* Skills (reforged_skills.c): the promotion screen's and the stat screen's composed texts,
       0x7F50-0x7F5F (never cached: include/bmitem.h RFO_VMSG_CACHEABLE); v1.23.0: 0x7F60-0x7F6F, the promotion
       screen's texts for FE8 classes whose own description names no weapon */
    if (index >= RFO_VMSG_BASE + 0x50 && index <= RFO_VMSG_BASE + 0x6F)
        return Reforged_SkillVirtualMsg(index - (RFO_VMSG_BASE + 0x50), buf);

    /* FE7 classes (reforged_fe7.c): their names and descriptions, 0x7F14-0x7F25; v1.19.0's FE6 classes',
       0x7F26-0x7F33 (two ids a class, 0x80 to REFORGED_FOREIGN_LAST_JID) */
    if (index >= RFO_VMSG_BASE + 0x14 && index <= RFO_VMSG_BASE + 0x14 + 2 * (REFORGED_FOREIGN_LAST_JID - 0x80 + 1) - 1)
        return Reforged_Fe7VirtualMsg(index - (RFO_VMSG_BASE + 0x14), buf);

    if (k < 0 || k >= FE7_ITEM_COUNT * 2)
        return 0;
    s = (k & 1) ? sFe7Items[k / 2].desc : sFe7Items[k / 2].name;
    strcpy(buf, s);
    return 1;
}

/* Luce and Forblaze have no spell of their own in FE8 (spellassoc-data.c): they cast
   Divine's and Elfire's, the nearest light and fire tomes, instead of none. */
int Reforged_SpellItem(int iid)
{
    if (iid == ITEM_LIGHT_LUCE)
        return ITEM_LIGHT_DIVINE;
    if (iid == ITEM_ANIMA_FORBLAZE)
        return ITEM_ANIMA_ELFIRE;
    return iid;
}


/*
 * "True hit" (RFO_TRUE_HIT, the user's request of 2026-11-06: "accurate forecasts are very
 * important"). The game hits when the average of two random numbers (0-99) is below the
 * hit rate (rng.c Roll2RN), so a shown 70 hits 82.3% of the time and a shown 30 only
 * 17.7%. With the option on, the battle forecast and the battle scene's gauge show that
 * real chance, rounded; 0 and 100 stay, as do "--" (0xFF, -1). Display only.
 */
/* The real chance in % (rounded) of a hit rolled with two numbers at hit rate rate (0-100; others
   as they are), whatever the display option. Also the smart AI's (reforged_gamefixes.c). */
int Reforged_TrueHitChance(int rate)
{
    static u8 sTrue[101];
    static int sBuilt;

    if (rate < 0 || rate > 100)
        return rate;
    if (!sBuilt)
    {
        int h, a, b;

        for (h = 0; h <= 100; h++)
        {
            int n = 0;

            for (a = 0; a < 100; a++)
                for (b = 0; b < 100; b++)
                    n += (a + b) / 2 < h;
            sTrue[h] = (u8)((n + 50) / 100); /* of 10000 pairs, in % */
        }
        sBuilt = 1;
    }
    return sTrue[rate];
}

int Reforged_ShownHit(int rate)
{
    int t;

    if (!Reforged_GetOption(RFO_TRUE_HIT) || rate < 0 || rate > 100)
        return rate;
    t = Reforged_TrueHitChance(rate);
    if (rate > 0 && rate < 100) /* a chance is never shown as none, nor as sure */
        t = t < 1 ? 1 : t > 99 ? 99 : t;
    return t;
}

/* The battle scene's gauge: a status staff's hit is rolled once (bmusemind.c ExecStatusStaff
   Roll1RN), so it shows as it is. */
int Reforged_ShownGaugeHit(int rate)
{
    if (GetItemAttributes(gBattleActor.weaponBefore) & IA_STAFF)
        return rate;
    return Reforged_ShownHit(rate);
}

#endif /* PORT_PLAYED_BUILD */
