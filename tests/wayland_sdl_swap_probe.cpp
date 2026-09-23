#include <SDL.h>

#include "glad/glad.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <memory>
#include <string>
#include <thread>

namespace {

constexpr int kSwapCount = 8;
constexpr auto kDeadline = std::chrono::seconds(5);

void skip(const char *message)
{
    std::fprintf(stdout, "SKIP: %s\n", message);
    std::exit(77);
}

void fail(const char *message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

int positive_environment_dimension(const char *name, int fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }

    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 1 || parsed > 16384) {
        fail("invalid Wayland probe window dimension");
    }
    return static_cast<int>(parsed);
}

void *game_style_gl_loader(const char *name)
{
    void *result = SDL_GL_GetProcAddress(name);
    if (result != nullptr) {
        return result;
    }
    char suffixed[256] = {};
    for (const char *suffix : {"ARB", "EXT"}) {
        std::snprintf(suffixed, sizeof(suffixed), "%s%s", name, suffix);
        result = SDL_GL_GetProcAddress(suffixed);
        if (result != nullptr) {
            return result;
        }
    }
    return nullptr;
}

void trace_egl_extensions()
{
    void *library = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_NOLOAD);
    if (library == nullptr) {
        std::fprintf(stdout, "EGL_TRACE: libEGL is not loaded\n");
        return;
    }
    using GetCurrentDisplay = void *(*)();
    using GetCurrentContext = void *(*)();
    using QueryString = const char *(*)(void *, int);
    using QueryContext = int (*)(void *, void *, int, int *);
    const auto get_current_display = reinterpret_cast<GetCurrentDisplay>(
        dlsym(library, "eglGetCurrentDisplay"));
    const auto get_current_context = reinterpret_cast<GetCurrentContext>(
        dlsym(library, "eglGetCurrentContext"));
    const auto query_string = reinterpret_cast<QueryString>(
        dlsym(library, "eglQueryString"));
    const auto query_context = reinterpret_cast<QueryContext>(
        dlsym(library, "eglQueryContext"));
    constexpr int kEglExtensions = 0x3055;
    constexpr int kEglConfigId = 0x3028;
    int config_id = -1;
    void *display = get_current_display != nullptr ? get_current_display() : nullptr;
    void *context = get_current_context != nullptr ? get_current_context() : nullptr;
    if (query_context != nullptr && display != nullptr && context != nullptr) {
        query_context(display, context, kEglConfigId, &config_id);
    }
    const char *extensions = display != nullptr &&
                             query_string != nullptr
        ? query_string(display, kEglExtensions)
        : nullptr;
    std::fprintf(stdout, "EGL_TRACE: config=%d extensions=%s\n",
                 config_id,
                 extensions != nullptr ? extensions : "unavailable");
    dlclose(library);
}

} // namespace

int main()
{
    const char *video_driver = std::getenv("SDL_VIDEODRIVER");
    if (video_driver == nullptr || std::string(video_driver) != "wayland") {
        skip("SDL_VIDEODRIVER is not wayland");
    }

    const char *wayland_display = std::getenv("WAYLAND_DISPLAY");
    const char *runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (wayland_display == nullptr || runtime_dir == nullptr) {
        skip("Wayland environment is unavailable");
    }

    /* SDL2 uses the application name as the Wayland app_id. Match the game
     * binary so compositor rules and grouping cannot hide a difference in
     * this control test. The opt-out is diagnostic: it isolates whether the
     * explicit app-id hint changes NVIDIA's EGL capability discovery. */
    if (std::getenv("GE_WAYLAND_PROBE_NO_APP_NAME") == nullptr) {
        SDL_SetHint(SDL_HINT_APP_NAME, "ge007.x86_64");
    }
    if (std::getenv("GE_WAYLAND_PROBE_GAME_HINTS") != nullptr) {
        SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stdout, "SKIP: SDL video unavailable: %s\n", SDL_GetError());
        return 77;
    }

    if (std::getenv("GE_WAYLAND_PROBE_GAME_PRECONTEXT") != nullptr) {
        /* Keep this sequence aligned with gfx_sdl_init(): the game queries
         * the selected backend before setting its framebuffer attributes, and
         * resolves its auto-sized, centered window before setting
         * depth/stencil/double-buffer before the requested GL version. */
        const char *driver = SDL_GetCurrentVideoDriver();
        std::fprintf(stdout, "PRECONTEXT_DRIVER: %s\n",
                     driver != nullptr ? driver : "unavailable");
        if (std::getenv("GE_WAYLAND_PROBE_QUERY_DISPLAY") != nullptr) {
            SDL_DisplayMode desktop_mode = {};
            SDL_DisplayMode current_mode = {};
            const int desktop_result = SDL_GetDesktopDisplayMode(0, &desktop_mode);
            const int current_result = SDL_GetCurrentDisplayMode(0, &current_mode);
            std::fprintf(stdout,
                         "PRECONTEXT_DISPLAY: desktop=%d:%dx%d current=%d:%dx%d\n",
                         desktop_result, desktop_mode.w, desktop_mode.h,
                         current_result, current_mode.w, current_mode.h);
        }
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    } else {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    }

    const int window_width = positive_environment_dimension(
        "GE_WAYLAND_PROBE_WIDTH", 640);
    const int window_height = positive_environment_dimension(
        "GE_WAYLAND_PROBE_HEIGHT", 480);
    int window_x = SDL_WINDOWPOS_UNDEFINED;
    int window_y = SDL_WINDOWPOS_UNDEFINED;
    if (std::getenv("GE_WAYLAND_PROBE_FIXED_POSITION") != nullptr) {
        window_x = 0;
        window_y = 0;
    } else if (std::getenv("GE_WAYLAND_PROBE_CENTERED_POSITION") != nullptr) {
        SDL_DisplayMode mode = {};
        if (SDL_GetCurrentDisplayMode(0, &mode) != 0 ||
            mode.w <= 0 || mode.h <= 0) {
            fail("could not query display mode for centered probe");
        }
        window_x = mode.w / 2 - window_width / 2;
        window_y = mode.h / 2 - window_height / 2;
        std::fprintf(stdout, "WINDOW_POSITION: centered=%d,%d\n",
                     window_x, window_y);
    }
    SDL_Window *window = SDL_CreateWindow(
        std::getenv("GE_WAYLAND_PROBE_GAME_HINTS") != nullptr
            ? "GoldenEye 007" : "GE007 Wayland SDL swap probe",
        window_x, window_y,
        window_width, window_height,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (window == nullptr) {
        const char *error = SDL_GetError();
        SDL_Quit();
        fail(error);
    }

    if (std::getenv("GE_WAYLAND_PROBE_SHOW_BEFORE_CONTEXT") != nullptr) {
        SDL_ShowWindow(window);
        std::fprintf(stdout, "WINDOW_LIFECYCLE: shown-before-context\n");
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        const char *error = SDL_GetError();
        SDL_DestroyWindow(window);
        SDL_Quit();
        fail(error);
    }

    if (std::getenv("GE_WAYLAND_PROBE_TRACE_EGL_BEFORE_GLAD") != nullptr) {
        if (std::getenv("GE_WAYLAND_PROBE_ATTRS_BEFORE_GLAD") != nullptr) {
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
            std::fprintf(stdout,
                         "SDL_GL_ATTRS_BEFORE_GLAD: rgba=%d,%d,%d,%d "
                         "depth=%d stencil=%d double=%d profile=%d "
                         "version=%d.%d flags=0x%x\n",
                         red, green, blue, alpha, depth, stencil,
                         double_buffer, profile, major, minor, flags);
        }
        std::fprintf(stdout, "EGL_TRACE: before-glad\n");
        trace_egl_extensions();
    }

    if (std::getenv("GE_WAYLAND_PROBE_REBIND_BEFORE_GLAD") != nullptr &&
        SDL_GL_MakeCurrent(window, context) != 0) {
        fail("pre-GLAD context rebind failed");
    }

    if (std::getenv("GE_WAYLAND_PROBE_INTERVAL_BEFORE_GLAD") != nullptr &&
        SDL_GL_SetSwapInterval(1) < 0) {
        std::fprintf(stdout,
                     "NOTE: pre-GLAD swap interval unavailable: %s\n",
                     SDL_GetError());
    }

    const GLADloadproc loader = std::getenv("GE_WAYLAND_PROBE_GAME_GL_LOADER")
        ? reinterpret_cast<GLADloadproc>(game_style_gl_loader)
        : reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress);
    if (!gladLoadGLLoader(loader)) {
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        fail("glad could not load the SDL OpenGL context");
    }
    if (std::getenv("GE_WAYLAND_PROBE_TRACE_ATTRS") != nullptr) {
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
        std::fprintf(stdout,
                     "SDL_GL_ATTRS: rgba=%d,%d,%d,%d depth=%d stencil=%d "
                     "double=%d profile=%d version=%d.%d flags=0x%x\n",
                     red, green, blue, alpha, depth, stencil,
                     double_buffer, profile, major, minor, flags);
    }
    if (std::getenv("GE_WAYLAND_PROBE_TRACE_EGL") != nullptr) {
        trace_egl_extensions();
    }

    /* Match the game's context handoff and a small amount of its backend
     * state. This is deliberately a control path, not a renderer duplicate:
     * its only contract is that SDL can present repeatedly after the context
     * moves from the creator thread to a render thread. */
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_CLAMP);

    if (std::getenv("GE_WAYLAND_PROBE_GAME_GL_INIT") != nullptr) {
        /* Mirror the renderer's pre-show GL work closely enough to test
         * whether driver capability discovery changes NVIDIA's Wayland
         * explicit-sync path. This is still a probe, not production logic. */
        for (int i = 0; i < 2; ++i) {
            GLuint color = 0;
            GLuint color_msaa = 0;
            GLuint depth = 0;
            GLuint fbo = 0;
            glGenTextures(1, &color);
            glBindTexture(GL_TEXTURE_2D, color);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB,
                         GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
            glGenRenderbuffers(1, &color_msaa);
            glGenRenderbuffers(1, &depth);
            glBindRenderbuffer(GL_RENDERBUFFER, depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 1, 1);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glGenFramebuffers(1, &fbo);
        }
        glUseProgram(0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        GLfloat max_anisotropy = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_anisotropy);
    }

    std::unique_ptr<unsigned char[]> upload_buffer;
    if (std::getenv("GE_WAYLAND_PROBE_LARGE_UPLOAD_BUFFER") != nullptr) {
        upload_buffer.reset(new unsigned char[8192u * 8192u * 4u]);
    }

    /* Optional production-state control. The real renderer updates its game
     * MSAA targets before every frame, binds that target, then returns to the
     * Wayland default framebuffer for presentation. Keep this deliberately
     * opt-in so the basic SDL lifecycle probe remains small while a failing
     * production comparison can exercise the same ownership boundary. */
    const bool production_state =
        std::getenv("GE_WAYLAND_PROBE_PRODUCTION_STATE") != nullptr;
    GLuint game_fbo = 0;
    GLuint game_color = 0;
    GLuint game_depth = 0;
    GLuint resolved_fbo = 0;
    GLuint resolved_color = 0;
    if (production_state) {
        glGenFramebuffers(1, &game_fbo);
        glGenRenderbuffers(1, &game_color);
        glGenRenderbuffers(1, &game_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, game_color);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGB8,
                                         window_width, window_height);
        glBindRenderbuffer(GL_RENDERBUFFER, game_depth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4,
                                         GL_DEPTH24_STENCIL8,
                                         window_width, window_height);
        glBindFramebuffer(GL_FRAMEBUFFER, game_fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, game_color);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, game_depth);

        glGenFramebuffers(1, &resolved_fbo);
        glGenTextures(1, &resolved_color);
        glBindTexture(GL_TEXTURE_2D, resolved_color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, window_width, window_height,
                     0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, resolved_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, resolved_color, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    if (SDL_GL_SetSwapInterval(1) < 0) {
        std::fprintf(stdout, "NOTE: initial swap interval unavailable: %s\n",
                     SDL_GetError());
    }
    if (std::getenv("GE_WAYLAND_PROBE_TRACE_EGL") != nullptr) {
        std::fprintf(stdout, "EGL_TRACE: after-initial-swap-interval\n");
        trace_egl_extensions();
    }
    if (std::getenv("GE_WAYLAND_PROBE_SET_VSYNC_TWICE") != nullptr &&
        SDL_GL_SetSwapInterval(1) < 0) {
        std::fprintf(stdout, "NOTE: repeated swap interval unavailable: %s\n",
                     SDL_GetError());
    }
    if (std::getenv("GE_WAYLAND_PROBE_TRACE_EGL") != nullptr &&
        std::getenv("GE_WAYLAND_PROBE_SET_VSYNC_TWICE") != nullptr) {
        std::fprintf(stdout, "EGL_TRACE: after-repeated-swap-interval\n");
        trace_egl_extensions();
    }
    if (std::getenv("GE_WAYLAND_PROBE_DISABLE_VSYNC") != nullptr) {
        SDL_GL_SetSwapInterval(0);
    }

    if (SDL_GL_MakeCurrent(window, nullptr) != 0) {
        const char *error = SDL_GetError();
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        fail(error);
    }

    std::atomic<int> swaps{0};
    std::atomic<bool> finished{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> resize_event_seen{false};
    std::atomic<bool> query_thread_stop{false};
    std::thread query_thread;
    if (std::getenv("GE_WAYLAND_PROBE_CONCURRENT_QUERIES") != nullptr) {
        query_thread = std::thread([&]() {
            while (!query_thread_stop.load(std::memory_order_acquire)) {
                int x = 0;
                int y = 0;
                int width = 0;
                int height = 0;
                SDL_GetKeyboardState(nullptr);
                SDL_GetMouseState(&x, &y);
                SDL_GetRelativeMouseState(&x, &y);
                SDL_GetWindowSize(window, &width, &height);
                SDL_GetWindowFlags(window);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    std::thread render_thread([&]() {
        if (SDL_GL_MakeCurrent(window, context) != 0) {
            failed.store(true, std::memory_order_release);
            finished.store(true, std::memory_order_release);
            return;
        }

        if (std::getenv("GE_WAYLAND_PROBE_DELAY_SHOW_MS") != nullptr) {
            const int delay_ms = positive_environment_dimension(
                "GE_WAYLAND_PROBE_DELAY_SHOW_MS", 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }

        /* Match the game: the thread that owns the context also owns the
         * initial Wayland show/configure/presentation lifecycle. */
        SDL_ShowWindow(window);

        if (production_state &&
            std::getenv("GE_WAYLAND_PROBE_TARGET_ON_RENDER") != nullptr) {
            /* gfx_start_frame() allocates/resizes the game target after the
             * context handoff. Exercise that ordering too, using the
             * compositor's current drawable rather than the requested window
             * size. */
            int target_width = 1;
            int target_height = 1;
            SDL_GL_GetDrawableSize(window, &target_width, &target_height);
            glBindRenderbuffer(GL_RENDERBUFFER, game_color);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGB8,
                                             target_width, target_height);
            glBindRenderbuffer(GL_RENDERBUFFER, game_depth);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4,
                                             GL_DEPTH24_STENCIL8,
                                             target_width, target_height);
            glBindFramebuffer(GL_FRAMEBUFFER, game_fbo);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_RENDERBUFFER, game_color);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                      GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, game_depth);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, resolved_color);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, target_width, target_height,
                         0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        if (std::getenv("GE_WAYLAND_PROBE_DISABLE_VSYNC") != nullptr) {
            SDL_GL_SetSwapInterval(0);
        }
        for (int frame = 1; frame <= kSwapCount; ++frame) {
            if (frame == 3 &&
                std::getenv("GE_WAYLAND_PROBE_RESIZE") != nullptr) {
                SDL_SetWindowSize(window, window_width + 64,
                                  window_height + 48);
            }

            if (std::getenv("GE_WAYLAND_PROBE_NO_POLL") == nullptr) {
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_QUIT) {
                        failed.store(true, std::memory_order_release);
                        finished.store(true, std::memory_order_release);
                        return;
                    }
                    if (event.type == SDL_WINDOWEVENT &&
                        (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                         event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                        resize_event_seen.store(true, std::memory_order_release);
                    }
                }
            }

            int width = 1;
            int height = 1;
            SDL_GL_GetDrawableSize(window, &width, &height);
            if (production_state) {
                /* Match gfx_start_frame/gfx_run: update the target, render to
                 * it, then select the window's default framebuffer. */
                glBindFramebuffer(GL_FRAMEBUFFER, game_fbo);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            if (std::getenv("GE_WAYLAND_PROBE_RENDERER_STATE") != nullptr) {
                /* videoApplyTexFilter()/videoApplyImageOptions() leave the
                 * renderer with no program and the optional bump-map unit
                 * selected before the first scheduler frame. */
                glUseProgram(0);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            if (std::getenv("GE_WAYLAND_PROBE_NO_VIEWPORT") == nullptr) {
                glViewport(0, 0, width, height);
            }
            if (!production_state) {
                glDrawBuffer(GL_BACK);
                glReadBuffer(GL_BACK);
            }
            if (std::getenv("GE_WAYLAND_PROBE_RENDERER_STATE") != nullptr) {
                glDisable(GL_SCISSOR_TEST);
            }
            glClearColor(0.02f, 0.04f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (std::getenv("GE_WAYLAND_PROBE_GAME_CLEAR") != nullptr) {
                glDisable(GL_SCISSOR_TEST);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glDepthMask(GL_TRUE);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glDepthMask(GL_FALSE);
                glEnable(GL_SCISSOR_TEST);
            }
            if (std::getenv("GE_WAYLAND_PROBE_RENDERER_STATE") != nullptr) {
                glDepthMask(GL_FALSE);
                glEnable(GL_SCISSOR_TEST);
            }
            glFlush();
            if (std::getenv("GE_WAYLAND_PROBE_TRACE_STATE") != nullptr) {
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
                std::fprintf(stdout,
                             "WAYLAND_PROBE_GL_STATE: draw-fbo=%d "
                             "read-fbo=%d draw=%d read=%d active=%d "
                             "program=%d array=%d rbo=%d tex=%d,%d,%d "
                             "depth-mask=%d viewport=%d,%d,%d,%d error=0x%x "
                             "drawable=%dx%d vendor=%s renderer=%s\n",
                             draw_fbo, read_fbo, draw_buffer, read_buffer,
                             active_texture, current_program,
                             array_buffer, renderbuffer, texture0, texture1,
                             texture2, depth_mask ? 1 : 0,
                             viewport[0], viewport[1], viewport[2], viewport[3],
                             (unsigned)gl_error,
                             width, height,
                             gl_vendor != nullptr ? gl_vendor : "unknown",
                             gl_renderer != nullptr ? gl_renderer : "unknown");
            }
            SDL_GL_SwapWindow(window);
            swaps.store(frame, std::memory_order_release);
        }
        finished.store(true, std::memory_order_release);
    });

    const auto deadline = std::chrono::steady_clock::now() + kDeadline;
    while (!finished.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!finished.load(std::memory_order_acquire)) {
        std::fprintf(stderr,
                     "FAIL: SDL Wayland control path stalled after %d swap(s)\n",
                     swaps.load(std::memory_order_acquire));
        std::_Exit(1);
    }

    render_thread.join();
    query_thread_stop.store(true, std::memory_order_release);
    if (query_thread.joinable()) {
        query_thread.join();
    }
    const int completed = swaps.load(std::memory_order_acquire);
    const bool ok = !failed.load(std::memory_order_acquire) &&
                    completed == kSwapCount &&
                    (std::getenv("GE_WAYLAND_PROBE_RESIZE") == nullptr ||
                     resize_event_seen.load(std::memory_order_acquire));

    SDL_GL_MakeCurrent(window, nullptr);
    if (production_state) {
        glDeleteFramebuffers(1, &game_fbo);
        glDeleteRenderbuffers(1, &game_color);
        glDeleteRenderbuffers(1, &game_depth);
        glDeleteFramebuffers(1, &resolved_fbo);
        glDeleteTextures(1, &resolved_color);
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (!ok) {
        std::fprintf(stderr,
                     "FAIL: SDL Wayland control path completed %d/%d swaps "
                     "resize_event=%d\n",
                     completed, kSwapCount,
                     resize_event_seen.load(std::memory_order_acquire) ? 1 : 0);
        return 1;
    }

    std::printf("PASS: SDL Wayland control path completed %d swaps\n", completed);
    return 0;
}
