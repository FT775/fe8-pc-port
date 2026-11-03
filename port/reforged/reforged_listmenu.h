#ifndef GUARD_REFORGED_LISTMENU_H
#define GUARD_REFORGED_LISTMENU_H

/*
 * Reforged Options' list screens (port/reforged/reforged_randomizer_menu.c draws
 * them): a title, a scrolling list of rows (each an option of port_reforged.h, a
 * section header, the randomizer's seed, or an action) and a help line. The
 * Randomizer, Display and Option sets screens are tables of rows. Played builds
 * only.
 */

#include "global.h"

/* Row kinds. */
enum { RR_HEADER, RR_CHOICE, RR_NUMBER, RR_SEED, RR_ACTION };

/* How a number row shows its value. */
enum {
    RN_PLAIN,   /* 5 */
    RN_PERCENT, /* 30% */
    RN_CHANCE,  /* 25%, and Off for 0 */
    RN_PLUS,    /* +5 */
    RN_SCALE,   /* 3x, and Auto for 0 */
};

/* Each row: its kind, option, label and help line. A choice row lists its
   values left to right (order: the value stored for each); a number row has
   its smallest value and step (its largest is Reforged_OptionMax). An action
   row runs action(arg) on A (its return: a help line to show, or NULL) and
   shows actionValue(arg) as its value (NULL: none). */
struct RandoRow {
    u8 kind;
    u8 option;
    u8 count;  /* choice: the number of values */
    u8 format; /* number: RN_* */
    u8 min;    /* number */
    u8 step;   /* number */
    const char *label;
    const char *help;
    const char *values[4];
    u8 order[4];
    const char *(*action)(int arg);
    const char *(*actionValue)(int arg);
    u8 arg;
};

#define RR_HEAD(label) { RR_HEADER, 0, 0, 0, 0, 0, label, NULL, { NULL }, { 0 }, NULL, NULL, 0 }
#define RR_SWITCH(opt, label, help) { RR_CHOICE, opt, 2, 0, 0, 0, label, help, { "On", "Off" }, { 1, 0 }, NULL, NULL, 0 }
#define RR_NUM(opt, fmt, min, step, label, help) \
    { RR_NUMBER, opt, 0, fmt, min, step, label, help, { NULL }, { 0 }, NULL, NULL, 0 }
#define RR_ACT(fn, valueFn, arg, label, help) \
    { RR_ACTION, 0, 0, 0, 0, 0, label, help, { NULL }, { 0 }, fn, valueFn, arg }

struct ReforgedListScreen {
    const char *title;               /* at most 18 tiles */
    const struct RandoRow *rows;
    int count;
    int greyOption;                  /* rows from greyFrom on are grey while this option is 0; -1: never */
    int greyFrom;
    void (*changed)(int option);     /* after a row's value changed (NULL: nothing) */
    void (*ended)(void *parent);     /* on the way out, behind the fade (NULL: the options screen, the
                                        parent, draws itself again: Reforged_MenuRedraw) */
};

/* The screen, a blocking child of the options screen (as the Cheats screen is). */
void *StartReforgedListScreen(void *parent, const struct ReforgedListScreen *screen);

#endif /* GUARD_REFORGED_LISTMENU_H */
