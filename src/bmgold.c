#include "global.h"

#if defined(PC_PORT) && PORT_PLAYED_BUILD
#include "port_reforged.h" /* PC played builds: Reforged Options, the "Infinite gold" cheat */
#endif

u32 GetPartyGoldAmount() {
    s8 id = gPlaySt.chapterIndex;
    if (id == 5) {
        return 0;
    }
    else {
#if defined(PC_PORT) && PORT_PLAYED_BUILD
        /* PC played builds: Reforged Options "Infinite gold" (back to 999,999) */
        Reforged_CheatGoldRefill();
#endif
        return gPlaySt.partyGoldAmount;
    }
}

void SetPartyGoldAmount(s32 amt) {
    gPlaySt.partyGoldAmount = amt;
    if (amt > 999999) {
        gPlaySt.partyGoldAmount = 999999;
    }
}

// addToPartyGold
void AddPartyGoldAmount(u32 amt) {
    s32 new_amt = gPlaySt.partyGoldAmount + amt;
    gPlaySt.partyGoldAmount = new_amt;
    if (new_amt > 999999) {
        gPlaySt.partyGoldAmount = 999999;
    }
}

