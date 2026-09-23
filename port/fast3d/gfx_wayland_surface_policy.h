#ifndef GE007_GFX_WAYLAND_SURFACE_POLICY_H
#define GE007_GFX_WAYLAND_SURFACE_POLICY_H

/*
 * Pure presentation lifecycle policy for the SDL/Wayland adapter.
 *
 * Wayland does not make a newly-created hidden surface immediately
 * presentable. SDL can report a nonzero requested size before the compositor
 * has completed the initial configure/commit cycle, and a resize can briefly
 * report a zero drawable while that cycle is repeated. Keeping those rules in
 * a small State-pattern policy lets the SDL adapter own side effects while
 * unit tests exercise every transition without a compositor or GL context.
 */

enum GeWaylandSurfacePhase {
    GE_WAYLAND_SURFACE_CREATED,
    GE_WAYLAND_SURFACE_SHOWN,
    GE_WAYLAND_SURFACE_READY,
};

/* SDL exposes several window notifications during the initial show. Only the
 * two size notifications correspond to a compositor configure for this
 * policy. Keeping the event classification here makes the State transition
 * explicit and testable without pulling SDL into the pure lifecycle code. */
enum GeWaylandSurfaceEvent {
    GE_WAYLAND_SURFACE_EVENT_SHOWN,
    GE_WAYLAND_SURFACE_EVENT_EXPOSED,
    GE_WAYLAND_SURFACE_EVENT_RESIZED,
    GE_WAYLAND_SURFACE_EVENT_SIZE_CHANGED,
};

static inline bool geWaylandSurfaceEventIsConfigure(
    GeWaylandSurfaceEvent event)
{
    return event == GE_WAYLAND_SURFACE_EVENT_RESIZED ||
           event == GE_WAYLAND_SURFACE_EVENT_SIZE_CHANGED;
}

struct GeWaylandSurfaceState {
    bool is_wayland;
    GeWaylandSurfacePhase phase;
    int width;
    int height;
    unsigned int stable_samples;
    bool configure_seen;
};

static inline GeWaylandSurfaceState geWaylandSurfaceCreate(bool is_wayland)
{
    const GeWaylandSurfaceState state = {
        is_wayland,
        GE_WAYLAND_SURFACE_CREATED,
        0,
        0,
        0,
        false,
    };
    return state;
}

static inline void geWaylandSurfaceMarkShown(GeWaylandSurfaceState *state)
{
    if (state == nullptr) {
        return;
    }
    state->phase = GE_WAYLAND_SURFACE_SHOWN;
    state->width = 0;
    state->height = 0;
    state->stable_samples = 0;
    state->configure_seen = false;
}

static inline void geWaylandSurfaceNoteConfigure(GeWaylandSurfaceState *state)
{
    if (state != nullptr) {
        state->configure_seen = true;
    }
}

static inline bool geWaylandSurfaceObserveDrawable(
    GeWaylandSurfaceState *state,
    int width,
    int height)
{
    if (state == nullptr) {
        return false;
    }

    if (width <= 0 || height <= 0) {
        state->width = 0;
        state->height = 0;
        state->stable_samples = 0;
        state->phase = state->is_wayland
            ? GE_WAYLAND_SURFACE_SHOWN
            : GE_WAYLAND_SURFACE_CREATED;
        return false;
    }

    if (!state->is_wayland) {
        state->width = width;
        state->height = height;
        state->stable_samples = 2;
        state->phase = GE_WAYLAND_SURFACE_READY;
        return true;
    }

    /* A hidden Wayland surface may have a requested size, but it cannot be
     * presented until the render-thread-owned show/configure sequence starts. */
    if (state->phase == GE_WAYLAND_SURFACE_CREATED) {
        return false;
    }

    const bool same_drawable = state->width == width &&
                               state->height == height &&
                               state->stable_samples > 0;
    state->width = width;
    state->height = height;
    state->stable_samples = same_drawable ? state->stable_samples + 1 : 1;

    if (state->phase == GE_WAYLAND_SURFACE_READY && !same_drawable) {
        state->phase = GE_WAYLAND_SURFACE_SHOWN;
    }

    if (state->configure_seen && state->stable_samples >= 2) {
        state->phase = GE_WAYLAND_SURFACE_READY;
    }
    return state->phase == GE_WAYLAND_SURFACE_READY;
}

static inline bool geWaylandSurfaceCanPresent(
    const GeWaylandSurfaceState &state)
{
    return state.phase == GE_WAYLAND_SURFACE_READY &&
           state.width > 0 && state.height > 0;
}

/* A zero timeout disables the bounded wait. This is useful for callers that
 * want to express “no deadline” explicitly without special-casing arithmetic. */
static inline bool geWaylandSurfaceTimedOut(unsigned long elapsed_ms,
                                            unsigned long timeout_ms)
{
    return timeout_ms != 0 && elapsed_ms >= timeout_ms;
}

/* Some NVIDIA EGL/Wayland combinations do not provide a reliable
 * uncapped-swap path: interval 0 can leave the compositor surface waiting in
 * eglSwapBuffers instead of returning a frame. Wayland already owns frame
 * pacing through its compositor callback, so the safe portable policy is to
 * use a paced interval there. X11 and other backends retain the caller's
 * requested interval. */
static inline int geWaylandEffectiveSwapInterval(bool is_wayland,
                                                 int requested_interval)
{
    if (is_wayland && requested_interval <= 0) {
        return 1;
    }
    return requested_interval;
}

#endif /* GE007_GFX_WAYLAND_SURFACE_POLICY_H */
