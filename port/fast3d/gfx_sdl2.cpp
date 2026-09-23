#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <time.h>
#include <dlfcn.h>

#include "platform.h"
#include "system.h"

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"
#include "input.h"
#include "video.h"
#include "ascension_postfx.h"
#include "gfx_wayland_surface_policy.h"
#include "glad/glad.h"

static SDL_Window* wnd;
static SDL_GLContext ctx;
static SDL_Renderer* renderer;
static int sdl_to_lus_table[512];
static bool vsync_enabled = true;
// OTRTODO: These are redundant. Info can be queried from SDL.
static int window_width = DESIRED_SCREEN_WIDTH;
static int window_height = DESIRED_SCREEN_HEIGHT;
static uint32_t fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
static bool fullscreen_state;
static bool maximized_state;
static bool is_running = true;
static void (*on_fullscreen_changed_callback)(bool is_now_fullscreen);
/* Wayland's event queue, window operations, context, and presentation share
 * one wl_display. The scheduler thread owns that complete lifecycle there;
 * X11 retains the host-thread pump because its native window contract differs. */
static bool wayland_backend;
static bool host_thread_pumps_events = true;
static GeWaylandSurfaceState wayland_surface;
static bool window_shown;
static bool render_thread_has_context;
static int requested_swap_interval = 1;
static bool wayland_uncapped_warning_logged;
static unsigned int wayland_trace_swaps;
static unsigned int wayland_trace_prepares;

static void gfx_sdl_trace_egl_config(void)
{
    void *library = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_NOLOAD);
    if (library == nullptr) {
        sysLogPrintf(LOG_NOTE, "EGL_TRACE: libEGL is not loaded");
        return;
    }
    using GetCurrentDisplay = void *(*)();
    using GetCurrentContext = void *(*)();
    using QueryContext = int (*)(void *, void *, int, int *);
    using QueryString = const char *(*)(void *, int);
    const auto get_display = reinterpret_cast<GetCurrentDisplay>(
        dlsym(library, "eglGetCurrentDisplay"));
    const auto get_context = reinterpret_cast<GetCurrentContext>(
        dlsym(library, "eglGetCurrentContext"));
    const auto query_context = reinterpret_cast<QueryContext>(
        dlsym(library, "eglQueryContext"));
    const auto query_string = reinterpret_cast<QueryString>(
        dlsym(library, "eglQueryString"));
    void *display = get_display != nullptr ? get_display() : nullptr;
    void *context = get_context != nullptr ? get_context() : nullptr;
    int config_id = -1;
    if (query_context != nullptr && display != nullptr && context != nullptr) {
        query_context(display, context, 0x3028, &config_id);
    }
    const char *extensions = display != nullptr && query_string != nullptr
        ? query_string(display, 0x3055)
        : nullptr;
    sysLogPrintf(LOG_NOTE, "EGL_TRACE: config=%d extensions=%s", config_id,
                 extensions != nullptr ? extensions : "unavailable");
    dlclose(library);
}

static constexpr unsigned long kWaylandSurfaceReadyTimeoutMs = 2000;

// GE port: frame pacing is owned by the cooperative thread kernel's vsync
// tick (libultra.c posts one VI retrace per frame at the console rate), so
// fast3d's internal busy-wait sync is disabled to avoid double-pacing. The
// GL swap still honors display vsync (vsync_enabled below).
static int target_fps = 0;
static uint64_t previous_time;
static uint64_t qpc_freq;

#define FRAME_INTERVAL_US_NUMERATOR 1000000
#define FRAME_INTERVAL_US_DENOMINATOR (target_fps)

static int32_t gfx_sdl_get_maximized_state(void) {
    return (int32_t)maximized_state;
}

static int32_t gfx_sdl_get_fullscreen_state(void) {
    return (int32_t)fullscreen_state;
}

static int32_t gfx_sdl_get_fullscreen_flag_mode(void) {
    return fullscreen_flag == SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : 1;
}

static void gfx_sdl_set_fullscreen_flag(int32_t mode) {
    switch (mode) {
        case 0: {
            fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
        } break;
        case 1: {
            fullscreen_flag = SDL_WINDOW_FULLSCREEN;
        } break;
    }
}

static void set_fullscreen(bool on, bool call_callback) {
    fullscreen_state = on;
    SDL_SetWindowFullscreen(wnd, on ? fullscreen_flag : 0);
    if (call_callback && on_fullscreen_changed_callback) {
        on_fullscreen_changed_callback(on);
    }
}

static void set_maximize_window(bool on) {
	maximized_state = on;
	if (on) {
		SDL_MaximizeWindow(wnd);
	} else {
		SDL_RestoreWindow (wnd);
	}
}

static void gfx_sdl_get_active_window_refresh_rate(uint32_t* refresh_rate) {
    int display_in_use = SDL_GetWindowDisplayIndex(wnd);

    SDL_DisplayMode mode;
    SDL_GetCurrentDisplayMode(display_in_use, &mode);
    *refresh_rate = mode.refresh_rate;
}

static void gfx_sdl_init(const struct GfxWindowInitSettings *set) {
    window_width = set->width;
    window_height = set->height;

#ifdef SDL_HINT_VIDEO_HIGHDPI_DISABLED
    if (!set->allow_hidpi) {
        // HiDPI control, if available
        SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
#if defined(PLATFORM_WIN32) && defined(SDL_HINT_WINDOWS_DPI_AWARENESS)
        // if HiDPI is disabled, declare ourselves DPI aware to get 1:1 window size on Windows
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitor");
#endif
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        sysFatalError("Could not init SDL:\n%s", SDL_GetError());
    }

    const char *video_driver = SDL_GetCurrentVideoDriver();
    wayland_backend = video_driver != nullptr &&
                      strcmp(video_driver, "wayland") == 0;
    host_thread_pumps_events = !wayland_backend;
    wayland_surface = geWaylandSurfaceCreate(wayland_backend);
    window_shown = false;
    render_thread_has_context = false;
    requested_swap_interval = 1;
    wayland_uncapped_warning_logged = false;
    wayland_trace_swaps = 0;
    wayland_trace_prepares = 0;

    // Auto window size: a width/height <= 0 means "pick a sensible default from
    // the desktop" rather than the 640x480 native res. Fit a 4:3 window into
    // ~85% of the primary desktop -- big enough to actually judge mouse/menu
    // behaviour, and 4:3 so the pointer maps onto the render with no letterbox.
    if (window_width <= 0 || window_height <= 0) {
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
            int maxw = (int)(dm.w * 0.85);
            int maxh = (int)(dm.h * 0.85);
            if (maxw * 3 > maxh * 4) { window_height = maxh; window_width = maxh * 4 / 3; }
            else                     { window_width = maxw; window_height = maxw * 3 / 4; }
        }
        if (window_width < 640 || window_height < 480) { window_width = 640; window_height = 480; }
        sysLogPrintf(LOG_INFO, "SDL: auto window size %dx%d", window_width, window_height);
    }

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (sysArgCheck("--debug-gl")) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }

    int posX = set->x;
    int posY = set->y;

    if (set->centered) {
        SDL_DisplayMode mode = {};
        SDL_GetCurrentDisplayMode(0, &mode);
        posX = mode.w / 2 - window_width / 2;
        posY = mode.h / 2 - window_height / 2;
    }

    if (set->fullscreen_is_exclusive) {
        fullscreen_flag = SDL_WINDOW_FULLSCREEN;
    }

    // The window is shown after the context is transferred to the render
    // thread. This makes the first Wayland configure and every presentation
    // part of one thread-owned lifecycle instead of racing the host loop.
    Uint32 flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;

    // if fullscreen was requested, start the window in fullscreen right away
    if (set->fullscreen) {
        flags |= fullscreen_flag;
        fullscreen_state = true;
    }

    if (set->maximized) {
        flags |= SDL_WINDOW_MAXIMIZED;
        maximized_state = true;
    }

#ifdef SDL_WINDOW_ALLOW_HIGHDPI
    if (set->allow_hidpi) {
        flags |= SDL_WINDOW_ALLOW_HIGHDPI;
    }
#endif

    // ideally we need 3.0 compat
    // if that doesn't work, try 3.2 core in case we're on mac, 2.1 compat as a last resort
    static u32 glver[][3] = {
        { 0, 0, 0                                    }, // for command line override
        { 3, 0, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY }, // 3.0: default, has all the features required
        { 4, 1, SDL_GL_CONTEXT_PROFILE_CORE          }, // 4.1core: macs only have core profile and this is the latest
        { 3, 2, SDL_GL_CONTEXT_PROFILE_CORE          }, // 3.2core: older macs will only have this at best
        { 3, 0, SDL_GL_CONTEXT_PROFILE_ES            }, // es3: don't really support ES properly, but we can try
        { 2, 1, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY }, // 2.1: absolute last resort, will still require GLSL130 as an extension
    };

    u32 verstart = 1;
    const u32 verend = sizeof(glver) / sizeof(*glver);
    const char *verstr = sysArgGetString("--gl-version");
    if (verstr && *verstr) {
        // user override
        glver[0][2] = strstr(verstr, "core") ? SDL_GL_CONTEXT_PROFILE_CORE :
            (strstr(verstr, "es") ? SDL_GL_CONTEXT_PROFILE_ES :
            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        sscanf(verstr, "%d.%d", &glver[0][0], &glver[0][1]);
        if (glver[0][0] >= 1 && glver[0][0] <= 4 && glver[0][1] < 9) {
            verstart = 0;
        }
    }

    ctx = NULL;
    u32 vmin = 0, vmaj = 0, vprof = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
    const char *vprofstr = "";
    for (u32 i = verstart; i < verend && !ctx; ++i) {
        vmaj = glver[i][0];
        vmin = glver[i][1];
        vprof = glver[i][2];
        vprofstr = (vprof == SDL_GL_CONTEXT_PROFILE_CORE ? "core" :
            (vprof == SDL_GL_CONTEXT_PROFILE_ES ? "es" : ""));

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, vmaj);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, vmin);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, vprof);

        wnd = SDL_CreateWindow(set->title, posX, posY, window_width, window_height, flags);
        if (!wnd) {
            sysLogPrintf(LOG_WARNING, "SDL: could not open SDL window for GL%d.%d%s:\n%s", vmaj, vmin, vprofstr, SDL_GetError());
            continue;
        }

        ctx = SDL_GL_CreateContext(wnd);
        if (!ctx) {
            sysLogPrintf(LOG_WARNING, "SDL: could not create GL%d.%d%s context: %s", vmaj, vmin, vprofstr, SDL_GetError());
            SDL_DestroyWindow(wnd);
            wnd = nullptr;
        }
    }

    if (!wnd || !ctx) {
        sysFatalError("Could not open SDL window with an OpenGL context of any supported version:\n%s", SDL_GetError());
    } else {
        sysLogPrintf(LOG_NOTE, "SDL: created GL%d.%d%s context", vmaj, vmin, vprofstr);
        if (getenv("GE_WAYLAND_TRACE_ATTRS") != nullptr) {
            int red = 0;
            int green = 0;
            int blue = 0;
            int alpha = 0;
            int depth = 0;
            int stencil = 0;
            int double_buffer = 0;
            int profile = 0;
            int major = 0;
            int minor = 0;
            int flags = 0;
            SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &red);
            SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &green);
            SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &blue);
            SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &alpha);
            SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depth);
            SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &stencil);
            SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &double_buffer);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &flags);
            sysLogPrintf(LOG_NOTE,
                         "SDL_GL_ATTRS: rgba=%d,%d,%d,%d depth=%d stencil=%d "
                         "double=%d profile=%d version=%d.%d flags=0x%x",
                         red, green, blue, alpha, depth, stencil,
                         double_buffer, profile, major, minor, flags);
            gfx_sdl_trace_egl_config();
        }
    }

    SDL_GL_MakeCurrent(wnd, ctx);
    if (getenv("GE_WAYLAND_TRACE_ATTRS") != nullptr) {
        sysLogPrintf(LOG_NOTE, "EGL_TRACE: after-gfx-sdl-make-current");
        gfx_sdl_trace_egl_config();
    }
    SDL_GL_SetSwapInterval(1);
    if (getenv("GE_WAYLAND_TRACE_ATTRS") != nullptr) {
        sysLogPrintf(LOG_NOTE, "EGL_TRACE: after-gfx-sdl-initial-swap-interval");
        gfx_sdl_trace_egl_config();
    }

    qpc_freq = SDL_GetPerformanceFrequency();
}

static void gfx_sdl_close(void) {
    ascensionPostFxShutdown();
    is_running = false;
}

static void gfx_sdl_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
    on_fullscreen_changed_callback = on_fullscreen_changed;
}

static void gfx_sdl_set_fullscreen(bool enable) {
    set_fullscreen(enable, true);
}

static void gfx_sdl_set_fullscreen_exclusive(bool enable) {
    const uint32_t newflag = enable ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (fullscreen_flag != newflag) {
        fullscreen_flag = newflag;
        // reset fullscreen to take new value into account if it already is in fullscreen
        if (fullscreen_state) {
            fullscreen_state = false;
            set_fullscreen(enable, true);
        }
    }
}

static void gfx_sdl_set_maximize_window(bool enable) {
    set_maximize_window(enable);
}

static void gfx_sdl_set_cursor_visibility(bool visible) {
    if (visible) {
        SDL_ShowCursor(SDL_ENABLE);
    } else {
        SDL_ShowCursor(SDL_DISABLE);
    }
}

static void get_centered_positions_native(int32_t width, int32_t height, int32_t *posX, int32_t *posY) {
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode mode = {};
    SDL_GetDesktopDisplayMode(disp_idx, &mode);
    *posX = mode.w / 2 - width / 2;
    *posY = mode.h / 2 - height / 2;
}

static void gfx_sdl_get_centered_positions(int32_t width, int32_t height, int32_t *posX, int32_t *posY) {
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode mode = {};
    SDL_GetCurrentDisplayMode(disp_idx, &mode);
    *posX = mode.w / 2 - width / 2;
    *posY = mode.h / 2 - height / 2;
}

static void gfx_sdl_set_closest_resolution(int32_t width, int32_t height, bool should_center) {
    const SDL_DisplayMode mode = {.w = width, .h = height};
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode closest = {};
    if (SDL_GetClosestDisplayMode(disp_idx, &mode, &closest)) {
        SDL_SetWindowDisplayMode(wnd, &closest);
        SDL_SetWindowSize(wnd, closest.w, closest.h);
        if (should_center) {
            int32_t posX = 0;
            int32_t posY = 0;
            get_centered_positions_native(closest.w, closest.h, &posX, &posY);
            SDL_SetWindowPosition(wnd, posX, posY);
        }
    }
}

static void gfx_sdl_set_dimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) {
    SDL_SetWindowSize(wnd, width, height);
    SDL_SetWindowPosition(wnd, posX, posY);
}

static void gfx_sdl_get_dimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    SDL_GL_GetDrawableSize(wnd, static_cast<int*>((void*)width), static_cast<int*>((void*)height));
    SDL_GetWindowPosition(wnd, static_cast<int*>(posX), static_cast<int*>(posY));
}

static void gfx_sdl_handle_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (wayland_backend && getenv("GE_WAYLAND_TRACE_EVENTS") != nullptr) {
            if (event.type == SDL_WINDOWEVENT) {
                sysLogPrintf(LOG_NOTE, "WAYLAND_EVENT: window=%u",
                             (unsigned)event.window.event);
            } else {
                sysLogPrintf(LOG_NOTE, "WAYLAND_EVENT: type=%u",
                             (unsigned)event.type);
            }
        }
        if (event.type == SDL_WINDOWEVENT && wayland_backend) {
            switch (event.window.event) {
                case SDL_WINDOWEVENT_SHOWN:
                case SDL_WINDOWEVENT_EXPOSED:
                case SDL_WINDOWEVENT_RESIZED:
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    geWaylandSurfaceNoteConfigure(&wayland_surface);
                    break;
                default:
                    break;
            }
        }

        /* Alt+Enter is a window-manager operation owned by this adapter. All
         * other input/quit/overlay handling has one authority in video.c; the
         * same handler is called here on Wayland and by the host loop on X11. */
        if (event.type == SDL_KEYDOWN &&
            event.key.keysym.sym == SDLK_RETURN &&
            (event.key.keysym.mod & KMOD_ALT)) {
            set_fullscreen(!fullscreen_state, true);
        }
        videoHandleSdlEvent(&event);
    }
}

/* The window/context are created on the host main thread, but the game's
 * scheduler thread (shedThread) does all rendering. Transfer the context to
 * that thread once and keep it current for subsequent frames. extern "C":
 * called from C (port/src/video.c). */
extern "C" void gfx_sdl_make_context_current(void) {
    if (wnd && ctx && !render_thread_has_context) {
        /* Note: SDL2's MakeCurrent return value is unreliable on this MinGW
         * toolchain (ABI artifact), so we don't branch on it. A genuine bind
         * failure will surface later as a GL call returning nothing / shader
         * compile failure, which is far louder. */
        SDL_GL_MakeCurrent(wnd, ctx);
        render_thread_has_context = true;
    }
}

/* Release the GL context from the calling (host main) thread. WGL only allows
 * a context to be current on one thread at a time, and while another thread
 * still holds it, wglMakeContextCurrent fails with "The requested resource is
 * in use" (SDL2 reports this via SDL_GetError). The host thread must release
 * after init so the game's scheduler thread can bind it per frame. */
extern "C" void gfx_sdl_release_context(void) {
    if (ctx) {
        if (getenv("GE_DIAG_KEEP_CONTEXT_CURRENT") != nullptr) {
            render_thread_has_context = true;
            return;
        }
        SDL_GL_MakeCurrent(NULL, NULL);
        render_thread_has_context = false;
    }
}

extern "C" int gfx_sdl_host_pumps_events(void) {
    return host_thread_pumps_events ? 1 : 0;
}

extern "C" int gfx_sdl_prepare_present(void) {
    if (wnd == nullptr || ctx == nullptr || !render_thread_has_context) {
        return 0;
    }

    if (!window_shown) {
        SDL_ShowWindow(wnd);
        window_shown = true;
        geWaylandSurfaceMarkShown(&wayland_surface);
    }

    if (wayland_backend && getenv("GE_WAYLAND_SKIP_READINESS") != nullptr) {
        /* Diagnostic seam: retain the single render-thread event owner while
         * bypassing the policy wait to isolate SDL/EGL's own configure path. */
        gfx_sdl_handle_events();
        return 1;
    }

    const Uint32 start_ticks = SDL_GetTicks();
    for (;;) {
        /* On Wayland this is the only event owner. On X11 it is harmlessly a
         * backstop for the host pump and preserves the old window behavior. */
        gfx_sdl_handle_events();

        int drawable_width = 0;
        int drawable_height = 0;
        SDL_GL_GetDrawableSize(wnd, &drawable_width, &drawable_height);
        if (wayland_backend && getenv("GE_WAYLAND_TRACE_PREP") != nullptr &&
            wayland_trace_prepares < 32) {
            int logical_width = 0;
            int logical_height = 0;
            SDL_GetWindowSize(wnd, &logical_width, &logical_height);
            sysLogPrintf(LOG_NOTE,
                         "WAYLAND_PREP: sample=%u drawable=%dx%d "
                         "logical=%dx%d phase=%d stable=%u configure=%d",
                         wayland_trace_prepares + 1,
                         drawable_width, drawable_height,
                         logical_width, logical_height,
                         (int)wayland_surface.phase,
                         wayland_surface.stable_samples,
                         wayland_surface.configure_seen ? 1 : 0);
            ++wayland_trace_prepares;
        }
        geWaylandSurfaceObserveDrawable(&wayland_surface,
                                        drawable_width,
                                        drawable_height);
        if (geWaylandSurfaceCanPresent(wayland_surface)) {
            return 1;
        }

        const unsigned long elapsed_ms =
            (unsigned long)(SDL_GetTicks() - start_ticks);
        if (geWaylandSurfaceTimedOut(
                elapsed_ms, kWaylandSurfaceReadyTimeoutMs)) {
            sysFatalError(
                "SDL: window surface did not become presentable within %lu ms "
                "(backend=%s drawable=%dx%d configure=%d)",
                kWaylandSurfaceReadyTimeoutMs,
                wayland_backend ? "wayland" : "non-wayland",
                drawable_width,
                drawable_height,
                wayland_surface.configure_seen ? 1 : 0);
            return 0;
        }
        SDL_Delay(1);
    }
}

/* The host thread owns SDL event pumping (see videoPumpEvents); when it sees
 * a window resize it calls this to refresh the cached drawable size that
 * fast3d uses for scaling. */
extern "C" void gfx_sdl_update_cached_size(void) {
    if (wnd) {
        SDL_GL_GetDrawableSize(wnd, &window_width, &window_height);
    }
}

static bool gfx_sdl_start_frame(void) {
    return true;
}

static uint64_t qpc_to_100ns(uint64_t qpc) {
    return qpc / qpc_freq * 10000000 + qpc % qpc_freq * 10000000 / qpc_freq;
}

static inline void sync_framerate_with_timer(void) {
    uint64_t t;
    t = qpc_to_100ns(SDL_GetPerformanceCounter());

    const int64_t next = previous_time + 10 * FRAME_INTERVAL_US_NUMERATOR / FRAME_INTERVAL_US_DENOMINATOR;
    int64_t left = next - t;
    // We want to exit a bit early, so we can busy-wait the rest to never miss the deadline
    left -= 15000UL;
    if (left > 0) {
        /* D250: `left` is in 100ns units (qpc_to_100ns); sysSleep() takes
         * microseconds. Passing it unconverted slept ~10x too long every
         * time this branch fired (e.g. an intended 2ms sleep became a real
         * ~20ms one), silently eating the rest of the frame budget whenever
         * per-frame work left little slack -- a resolution/GPU-independent
         * frame-pacing bug, not a real render cost. */
        sysSleep((uint32_t)(left / 10));
    }

    do {
        sysCpuRelax();
        t = qpc_to_100ns(SDL_GetPerformanceCounter());
    } while ((int64_t)t < next);

    t = qpc_to_100ns(SDL_GetPerformanceCounter());
    if (left > 0 && t - next < 10000) {
        // In case it takes some time for the application to wake up after sleep,
        // or inaccurate timer,
        // don't let that slow down the framerate.
        t = next;
    }
    previous_time = t;
}

/* Optional hook run with the freshly-composited frame still in the back
 * buffer, immediately before the swap. video.c uses it for GE_PCDUMP /
 * screenshots: reading the back buffer AFTER SDL_GL_SwapWindow is undefined
 * on buffer-exchange drivers (Mesa, WSLg) and yields a black capture. */
extern "C" void (*gfx_pre_swap_hook)(void) = NULL;

static void gfx_sdl_swap_buffers_begin(void) {
    if (target_fps) {
        sync_framerate_with_timer();
    }
    /* Apply presentation-only processing while the composited frame is still
     * in the back buffer. The existing capture hook follows it, so screenshots
     * represent the same image the player sees. */
    ascensionPostFxApply();
    if (gfx_pre_swap_hook) {
        gfx_pre_swap_hook();
    }
    /* A Wayland configure can arrive after frame preparation but before the
     * presentation boundary. Re-run the bounded readiness gate immediately
     * before the swap so a transient resize never enters EGL with a stale
     * drawable. */
    if (wayland_backend && getenv("GE_WAYLAND_SKIP_SWAP_PREP") == nullptr &&
        !gfx_sdl_prepare_present()) {
        return;
    }
    if (wayland_backend && getenv("GE_WAYLAND_TRACE") != nullptr &&
        wayland_trace_swaps < 8) {
        if (getenv("GE_WAYLAND_TRACE_STATE") != nullptr) {
            GLint draw_fbo = -1;
            GLint read_fbo = -1;
            GLint draw_buffer = -1;
            GLint read_buffer = -1;
            GLint active_texture = -1;
            GLint current_program = -1;
            GLint array_buffer = -1;
            GLint renderbuffer = -1;
            GLint texture0 = -1;
            GLint texture1 = -1;
            GLint texture2 = -1;
            GLint viewport[4] = {-1, -1, -1, -1};
            GLboolean depth_mask = GL_FALSE;
            int drawable_width = 0;
            int drawable_height = 0;
            SDL_GL_GetDrawableSize(wnd, &drawable_width, &drawable_height);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
            glGetIntegerv(GL_DRAW_BUFFER, &draw_buffer);
            glGetIntegerv(GL_READ_BUFFER, &read_buffer);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
            glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0);
            glActiveTexture(GL_TEXTURE1);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture1);
            glActiveTexture(GL_TEXTURE2);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2);
            glActiveTexture((GLenum)active_texture);
            glGetIntegerv(GL_VIEWPORT, viewport);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
            const GLenum gl_error = glGetError();
            const char *gl_vendor = reinterpret_cast<const char *>(
                glGetString(GL_VENDOR));
            const char *gl_renderer = reinterpret_cast<const char *>(
                glGetString(GL_RENDERER));
            sysLogPrintf(LOG_NOTE,
                         "WAYLAND_GL_STATE: draw-fbo=%d read-fbo=%d "
                         "draw=%d read=%d active=%d program=%d "
                         "array=%d rbo=%d tex=%d,%d,%d depth-mask=%d "
                         "viewport=%d,%d,%d,%d error=0x%x ctx=%p "
                         "drawable=%dx%d vendor=%s renderer=%s",
                         draw_fbo, read_fbo, draw_buffer, read_buffer,
                         active_texture, current_program,
                         array_buffer, renderbuffer, texture0, texture1,
                         texture2, depth_mask ? 1 : 0,
                         viewport[0], viewport[1], viewport[2], viewport[3],
                         (unsigned)gl_error,
                         SDL_GL_GetCurrentContext(),
                         drawable_width, drawable_height,
                         gl_vendor != nullptr ? gl_vendor : "unknown",
                         gl_renderer != nullptr ? gl_renderer : "unknown");
        }
        sysLogPrintf(LOG_NOTE,
                     "WAYLAND_TRACE: swap=%u interval=%d requested=%d error=%s",
                     wayland_trace_swaps + 1,
                     SDL_GL_GetSwapInterval(), requested_swap_interval,
                     SDL_GetError());
        ++wayland_trace_swaps;
    }
    if (wayland_backend && getenv("GE_DIAG_CLAMP_PRESENT_VIEWPORT") != nullptr) {
        int drawable_width = 0;
        int drawable_height = 0;
        SDL_GL_GetDrawableSize(wnd, &drawable_width, &drawable_height);
        glViewport(0, 0, drawable_width, drawable_height);
    }
    SDL_GL_SwapWindow(wnd);
}

static void gfx_sdl_swap_buffers_end(void) {

}

static double gfx_sdl_get_time(void) {
    return SDL_GetPerformanceCounter() / (double)qpc_freq;
}

static int32_t gfx_sdl_get_target_fps(void) {
    return target_fps;
}

static void gfx_sdl_set_target_fps(int fps) {
    /* D186: the pacing wait below runs inline on the game's scheduler thread
     * (libultra.c osSpTaskStartGo -> gfx_run -> swap_buffers_begin), so a low
     * cap stalls VI-retrace delivery and throttles the whole sim, not just
     * presentation. Until pacing is moved off that thread, refuse caps low
     * enough to be user-visible as a slowdown: treat 1..29 as uncapped. */
    if (fps > 0 && fps < 30) {
        sysLogPrintf(LOG_WARNING, "gfx_sdl: ignoring FpsCap=%d (< 30 throttles the sim); running uncapped", fps);
        fps = 0;
    }
    target_fps = fps;
}

static bool gfx_sdl_can_disable_vsync(void) {
    return !wayland_backend;
}

static void *gfx_sdl_get_window_handle(void) {
    return (void *)wnd;
}

static void gfx_sdl_set_window_title(const char *title) {
    SDL_SetWindowTitle(wnd, title);
}

static int gfx_sdl_get_swap_interval(void) {
    return SDL_GL_GetSwapInterval();
}

static bool gfx_sdl_set_swap_interval(int interval) {
    const int effective_interval = geWaylandEffectiveSwapInterval(
        wayland_backend, interval);
    const int test_interval = getenv("GE_WAYLAND_TEST_SWAP_INTERVAL") != nullptr
        ? atoi(getenv("GE_WAYLAND_TEST_SWAP_INTERVAL"))
        : effective_interval;
    if (effective_interval != interval && !wayland_uncapped_warning_logged) {
        sysLogPrintf(LOG_WARNING,
                     "SDL: Wayland does not use an uncapped swap interval; "
                     "using compositor-paced presentation");
        wayland_uncapped_warning_logged = true;
    }

    const bool success = SDL_GL_SetSwapInterval(test_interval) >= 0;
    vsync_enabled = success && (test_interval != 0);
    if (success) {
        requested_swap_interval = test_interval;
    }
    if (!success) {
        sysLogPrintf(LOG_WARNING,
                     "SDL: failed to set swap interval requested=%d effective=%d: %s",
                     interval, test_interval, SDL_GetError());
    }
    return success;
}

int gfx_sdl_get_display_mode(int modenum, int *out_w, int *out_h) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode sdlmode;
    if (SDL_GetDisplayMode(display_in_use, modenum, &sdlmode) == 0) {
        *out_w = sdlmode.w;
        *out_h = sdlmode.h;
        return 1;
    }
    return 0;
}

int gfx_sdl_get_current_display_mode(int *out_w, int *out_h) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode sdlmode;
    if (SDL_GetCurrentDisplayMode(display_in_use, &sdlmode) == 0) {
        *out_w = sdlmode.w;
        *out_h = sdlmode.h;
        return 1;
    }
    return 0;
}

int gfx_sdl_get_num_display_modes(void) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    return SDL_GetNumDisplayModes(display_in_use);
}

struct GfxWindowManagerAPI gfx_sdl = {
    gfx_sdl_init,
    gfx_sdl_close,
    gfx_sdl_get_display_mode,
    gfx_sdl_get_current_display_mode,
    gfx_sdl_get_num_display_modes,
    gfx_sdl_get_fullscreen_state,
    gfx_sdl_set_fullscreen_changed_callback,
    gfx_sdl_set_fullscreen,
    gfx_sdl_set_fullscreen_exclusive,
    gfx_sdl_set_fullscreen_flag,
    gfx_sdl_get_fullscreen_flag_mode,
    gfx_sdl_get_maximized_state,
    gfx_sdl_set_maximize_window,
    gfx_sdl_get_active_window_refresh_rate,
    gfx_sdl_set_cursor_visibility,
    gfx_sdl_set_closest_resolution,
    gfx_sdl_set_dimensions,
    gfx_sdl_get_dimensions,
    gfx_sdl_get_centered_positions,
    gfx_sdl_handle_events,
    gfx_sdl_start_frame,
    gfx_sdl_swap_buffers_begin,
    gfx_sdl_swap_buffers_end,
    gfx_sdl_get_time,
    gfx_sdl_get_target_fps,
    gfx_sdl_set_target_fps,
    gfx_sdl_can_disable_vsync,
    gfx_sdl_get_window_handle,
    gfx_sdl_set_window_title,
    gfx_sdl_get_swap_interval,
    gfx_sdl_set_swap_interval,
};
