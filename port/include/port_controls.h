#ifndef GUARD_PORT_CONTROLS_H
#define GUARD_PORT_CONTROLS_H

/*
 * Rebindable controls (port/hw/controls.c): which keyboard keys and gamepad
 * buttons give each GBA button, fast-forward and fullscreen. Library-neutral:
 * a binding is a name, the window backend (port/sdl3/platform_sdl3.c) turns it
 * into a key or a button. Every build starts from the defaults (the keys the
 * port always had); the played builds' Reforged Options "Controls" screen
 * (port/reforged/reforged_controls_menu.c) and settings file
 * (port/reforged/reforged_settings.c, control_* keys) change them.
 *
 * Names: a keyboard key by SDL's key name ("Z", "Return", "Keypad Enter",
 * "Left Shift", "F11"; Platform_BindingValid checks one); a gamepad button by
 * the port's own names: "a" and "b" (the face buttons marked A / Cross and
 * B / Circle, wherever the pad has them), "west" and "north" (the other two
 * face buttons), "back", "start", "guide", "l", "r" (the shoulders), "zl",
 * "zr" (the triggers), "dpup", "dpdown", "dpleft", "dpright", "lstick",
 * "rstick" (the sticks pressed in). "" is no binding.
 */

enum {
    CTL_A, CTL_B, CTL_SELECT, CTL_START, CTL_RIGHT, CTL_LEFT, CTL_UP, CTL_DOWN, CTL_R, CTL_L,
    CTL_FAST_FORWARD, CTL_FULLSCREEN,
    CTL_COUNT
};
#define CTL_GBA_KEYS 10 /* CTL_A..CTL_L: bit i is GBA key i (PLATFORM_KEY_A << i) */

enum { CTL_KEY1, CTL_KEY2, CTL_PAD1, CTL_PAD2, CTL_SLOTS };
#define CTL_SLOT_IS_PAD(slot) ((slot) >= CTL_PAD1)
#define CTL_NAME_MAX 24

/* The binding of an action's slot ("" for none), and its default. */
const char *Controls_Get(int action, int slot);
const char *Controls_Default(int action, int slot);
/* Binds name to the slot. A key or button is bound once: an action slot of the
   same kind (keyboard or pad) that had it gets this slot's old binding instead
   (a swap, so nothing is lost). "" clears the slot. Names longer than
   CTL_NAME_MAX - 1 are refused (0); 1 when done. */
int Controls_Set(int action, int slot, const char *name);
/* As Controls_Set, but no swap (the settings file, read as written). */
int Controls_Load(int action, int slot, const char *name);
/* 1 when every binding is its default. */
int Controls_IsDefault(void);
/* A, B, Start and the four directions: they always keep a key and a pad button
   (the Controls screen refuses to take the last one away; Controls_Tidy puts one back). */
int Controls_IsEssential(int action);
/* How many slots of `action` of the kind of slot `but` (keys or pads), but `but`, are
   bound to something other than name (NULL: to anything). */
int Controls_OthersOfKind(int action, int but, const char *name);
/* After a settings file: a key or button bound twice keeps only its first binding,
   and an essential action left without a key or pad button gets its default back.
   The counts of each fix; their sum is returned. */
int Controls_Tidy(int *dupes, int *restored);
void Controls_ResetDefaults(void);
/* Changes with every change of a binding (the backend re-resolves its table). */
unsigned Controls_Generation(void);
/* The settings file's key of an action: "control_a", ..., "control_fullscreen". */
const char *Controls_SettingsKey(int action);
/* The name the menu shows: "A", "B", ..., "Fast-forward", "Fullscreen". */
const char *Controls_ActionLabel(int action);

#endif /* GUARD_PORT_CONTROLS_H */
