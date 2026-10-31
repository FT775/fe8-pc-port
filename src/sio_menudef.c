#include "global.h"

#include "bmunit.h"
#include "bmitem.h"
#include "uimenu.h"
#include "hardware.h"

#include "sio.h"

//! FE8U = 0x0804D858
u8 SioAttackItemMenu_Usability(const struct MenuItemDef * def, int number)
{
    int item = gActiveUnit->items[number];

    if ((GetItemAttributes(item) & IA_WEAPON) == 0)
    {
        return MENU_NOTSHOWN;
    }

    if (GetItemMinRange(item) > 2)
    {
        return MENU_DISABLED;
    }

    if (!CanUnitUseWeapon(gActiveUnit, item))
    {
        return MENU_NOTSHOWN;
    }

    return MENU_ENABLED;
}

//! FE8U = 0x0804D8A4
u8 SioAttackItemMenu_Effect(struct MenuProc * menu, struct MenuItemProc * menuItem)
{
    if (menuItem->availability == MENU_DISABLED)
    {
        return MENU_ACT_SND6B;
    }

    gSioBmSt.unk_07 = menuItem->itemNumber;

    return MENU_ACT_SND6A | MENU_ACT_DOOM;
}

//! FE8U = 0x0804D8C8
u8 SioAttackTypeMenu_Effect(struct MenuProc * menu, struct MenuItemProc * menuItem)
{
    gSioBmSt.unk_06 = menuItem->itemNumber + 1;
    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6A | MENU_ACT_CLEAR;
}

//! FE8U = 0x0804D8DC
int SioAttackItemMenu_OnDraw(struct MenuProc * menu, struct MenuItemProc * menuItem)
{
    int item = gActiveUnit->items[menuItem->itemNumber];

    s8 color = CanUnitUseWeapon(gActiveUnit, item);

    if (GetItemMinRange(item) > 2)
    {
        color = 0;
    }

    DrawItemMenuLine(
        &menuItem->text, item, color, TILEMAP_LOCATED(gBG0TilemapBuffer, menuItem->xTile, menuItem->yTile));
    BG_EnableSyncByMask(BG0_SYNC_BIT);

    // return;
}

//! FE8U = 0x0804D940
u8 SioAttackMenu_OnBPress(struct MenuProc * menu, struct MenuItemProc * menuItem)
{
    gSioBmSt.unk_06 = 0;
    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6B | MENU_ACT_CLEAR;
}

//! FE8U = 0x0804D950
u8 SioAttackMenu_CloseAndClearRect_unused(struct MenuProc * menu)
{
    gSioBmSt.unk_06 = 0;

    TileMap_FillRect(TILEMAP_LOCATED(gBG0TilemapBuffer, menu->rect.x, menu->rect.y), menu->rect.w, menu->rect.h, 0);
    TileMap_FillRect(TILEMAP_LOCATED(gBG1TilemapBuffer, menu->rect.x, menu->rect.y), menu->rect.w, menu->rect.h, 0);

    BG_EnableSyncByMask(BG0_SYNC_BIT | BG1_SYNC_BIT);

    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6B;
}

// clang-format off

struct MenuItemDef CONST_DATA MenuItems_SioAttackType[] =
{
    {
        .name = "　直接攻撃",
        .overrideId = 1,
        .isAvailable = MenuAlwaysEnabled,
        .onSelected = SioAttackTypeMenu_Effect,
    },

    {
        .name = "　間接攻撃",
        .overrideId = 2,
        .isAvailable = MenuAlwaysEnabled,
        .onSelected = SioAttackTypeMenu_Effect,
    },

    { 0 }, // end
};

struct MenuItemDef CONST_DATA MenuItems_SioAttackItem[] =
{
    {
        .name = "",
        .overrideId = 3,
        .isAvailable = SioAttackItemMenu_Usability,
        .onDraw = SioAttackItemMenu_OnDraw,
        .onSelected = SioAttackItemMenu_Effect,
    },

    {
        .name = "",
        .overrideId = 4,
        .isAvailable = SioAttackItemMenu_Usability,
        .onDraw = SioAttackItemMenu_OnDraw,
        .onSelected = SioAttackItemMenu_Effect,
    },

    {
        .name = "",
        .overrideId = 5,
        .isAvailable = SioAttackItemMenu_Usability,
        .onDraw = SioAttackItemMenu_OnDraw,
        .onSelected = SioAttackItemMenu_Effect,
    },

    {
        .name = "",
        .overrideId = 6,
        .isAvailable = SioAttackItemMenu_Usability,
        .onDraw = SioAttackItemMenu_OnDraw,
        .onSelected = SioAttackItemMenu_Effect,
    },

    {
        .name = "",
        .overrideId = 7,
        .isAvailable = SioAttackItemMenu_Usability,
        .onDraw = SioAttackItemMenu_OnDraw,
        .onSelected = SioAttackItemMenu_Effect,
    },

    { 0 }, // end
};

struct MenuItemDef CONST_DATA MenuItems_085AAD34[] =
{
    { 0 }, // end
};

struct MenuItemDef CONST_DATA MenuItems_085AAD58[] =
{
    { 0 }, // end
};

struct MenuDef CONST_DATA MenuDef_SioAttackType =
{
    .rect = { 16, 2, 8, 0 },
    .menuItems = MenuItems_SioAttackType,
    .onBPress = SioAttackMenu_OnBPress,
};

struct MenuDef CONST_DATA MenuDef_SioAttackItem =
{
    .rect = { 1, 1, 14, 0 },
    .menuItems = MenuItems_SioAttackItem,
    .onBPress = SioAttackMenu_OnBPress,
};

struct MenuDef CONST_DATA gUnknown_085AADC4 =
{
    .rect = { 1, 2, 12, 0 },
    .menuItems = MenuItems_085AAD34,
    .onBPress = MenuCancelSelect,
};

struct MenuDef CONST_DATA gUnknown_085AADE8 =
{
    .rect = { 1, 2, 22, 0 },
    .menuItems = MenuItems_085AAD58,
    .onBPress = MenuCancelSelect,
};

// clang-format on
