#ifndef GFX_SDL_H
#define GFX_SDL_H

#include "gfx_window_manager_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct GfxWindowManagerAPI gfx_sdl;

/* Transfer the window's GL context to the calling thread once (rendering runs
 * on the game's scheduler thread, not the thread that created it). */
void gfx_sdl_make_context_current(void);

/* Release the GL context from the calling (host main) thread after init, so
 * the game's scheduler thread can bind it for the render loop (see
 * gfx_sdl2.cpp). */
void gfx_sdl_release_context(void);

/* Wayland must keep SDL event dispatch with GL presentation on the scheduler
 * thread; the host loop skips SDL_PollEvent in that configuration. */
int gfx_sdl_host_pumps_events(void);

/* Show the hidden surface on the presentation-owning thread and wait for a
 * bounded, configured, nonzero drawable before rendering or swapping. Returns
 * nonzero only when the caller may safely begin a frame. */
int gfx_sdl_prepare_present(void);

/* Refresh the cached drawable size after a window resize (called by the
 * host thread's event pump). */
void gfx_sdl_update_cached_size(void);

#ifdef __cplusplus
}
#endif

#endif
