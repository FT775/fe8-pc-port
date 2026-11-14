/*
 * Save states (port/include/port_state.h, port/reforged/reforged_savestate.c): the first object on the
 * link line of every played build (port/Makefile, Makefile.sdl, Makefile.mingw put it before the game's
 * objects; end.c is the last, after autostubs.o). The linker lays out .data and .bss in link order, so
 * these markers and end.c's bracket the game's and the port's own host data, and the C runtime's and the
 * libraries' (crt1/crt2, crtbegin, libgcc, mingw-w64, the SDL delay-load library) lie outside.
 * port/tools/check_state_ranges.py checks that in the link map at every link. Windows also has the
 * sorted ".data$*" sections (the game's -fdata-sections objects; MinGW's ld sorts them after every plain
 * .data), bracketed by a second pair whose names sort first and last. Empty in every test build.
 */
#if defined(PORT_RFO_STATE)
unsigned char gPortStateDataBegin[4] = { 'F', 'E', '8', '<' };
unsigned char gPortStateBssBegin[4];
#ifdef _WIN32
unsigned char gPortStateData2Begin[4] __attribute__((section(".data$0000fe8pc_state_begin"))) = { 'F', 'E', '8', '[' };
#endif
#endif
