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

#include "bmunit.h"
#include "bmitem.h"
#include "constants/items.h"

#include "port_reforged.h"

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

        PortLog("[reforged] item 0x%02X %d/%d -> blue 0x%02X %d, red 0x%02X %d; forecast uses blue %d, red %d\n",
            items[i], GetItemUses(item), GetItemMaxUses(item), ITEM_INDEX(b), b >> 8, ITEM_INDEX(r), r >> 8,
            Reforged_ForecastUses(item, &blue), Reforged_ForecastUses(item, &red));
    }
}

/* Whether the option gives this item back at its maximum uses after a use. */
static bool32 Reforged_Refills(int item, struct Unit *owner)
{
    int mode = Reforged_GetOption(RFO_UNBREAKABLE); /* 1 your units, 2 everyone */

    return (mode == 2 || (mode == 1 && owner != NULL && UNIT_FACTION(owner) == FACTION_BLUE))
        && (GetItemAttributes(item) & (IA_WEAPON | IA_STAFF))
        && !(GetItemAttributes(item) & IA_UNBREAKABLE)
        && !Reforged_IsLegendaryWeapon(item)
        && GetItemMaxUses(item) > 0;
}

u16 Reforged_ItemAfterUse(int item, struct Unit *owner)
{
    static int checked;

    if (!checked)
    {
        checked = 1;
        if (getenv("FE8PC_RFO_CHECK") != NULL)
            LogItemRules();
    }

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

#endif /* PORT_PLAYED_BUILD */
