#ifndef GUARD_PORT_SDL3_TEST_SDL_TESTHOOK_H
#define GUARD_PORT_SDL3_TEST_SDL_TESTHOOK_H

/*
 * Test builds only (FE8PC_TESTBUILD=1): the window build's test hooks
 * (sdl_testhook.c says what each one does). platform_sdl3.c calls them only
 * under #ifdef FE8PC_TESTBUILD, and port/Makefile.sdl and port/Makefile.mingw
 * (SDL=1) build sdl_testhook.c only into a test build, so a release build has
 * neither the calls nor the object.
 */

#include <stdint.h>

#include <SDL3/SDL.h>

SDL_WindowFlags PortSdlTest_WindowFlags(SDL_WindowFlags flags);
void PortSdlTest_AfterInit(SDL_Window *window, SDL_Renderer *renderer);
void PortSdlTest_Frame(void);
uint16_t PortSdlTest_Keys(uint16_t keys);
void PortSdlTest_BeforePresent(SDL_Renderer *renderer, const uint32_t *rgba, int width, int height);
void PortSdlTest_AfterPresent(void);
void PortSdlTest_AudioOpened(SDL_AudioStream *stream);
void PortSdlTest_Shutdown(void);

#endif /* GUARD_PORT_SDL3_TEST_SDL_TESTHOOK_H */
