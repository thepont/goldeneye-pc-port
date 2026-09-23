#include "gfx_wayland_surface_policy.h"

#include <cstdlib>

static void require(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

int main()
{
    /* Visibility is not configuration. A shown/exposed notification may be
     * emitted before the compositor has delivered the xdg size configure and
     * must not make an early requested drawable presentable. */
    require(!geWaylandSurfaceEventIsConfigure(
        GE_WAYLAND_SURFACE_EVENT_SHOWN));
    require(!geWaylandSurfaceEventIsConfigure(
        GE_WAYLAND_SURFACE_EVENT_EXPOSED));
    require(geWaylandSurfaceEventIsConfigure(
        GE_WAYLAND_SURFACE_EVENT_RESIZED));
    require(geWaylandSurfaceEventIsConfigure(
        GE_WAYLAND_SURFACE_EVENT_SIZE_CHANGED));

    /* Null ports are valid failure-path inputs for the render adapter and must
     * remain side-effect free. These cases also keep the defensive policy
     * branches covered independently of a compositor. */
    geWaylandSurfaceMarkShown(nullptr);
    geWaylandSurfaceNoteConfigure(nullptr);
    require(!geWaylandSurfaceObserveDrawable(nullptr, 640, 480));

    GeWaylandSurfaceState state = geWaylandSurfaceCreate(true);
    require(state.phase == GE_WAYLAND_SURFACE_CREATED);
    require(!geWaylandSurfaceCanPresent(state));

    /* A drawable reported before the surface is shown must not make a hidden
     * Wayland window eligible for its first swap. */
    require(!geWaylandSurfaceObserveDrawable(&state, 640, 480));
    require(!geWaylandSurfaceCanPresent(state));

    geWaylandSurfaceMarkShown(&state);
    require(state.phase == GE_WAYLAND_SURFACE_SHOWN);
    require(!geWaylandSurfaceCanPresent(state));

    /* Wayland configure events and drawable observations are separate: one
     * observation is not enough to race the compositor's initial configure. */
    geWaylandSurfaceNoteConfigure(&state);
    require(!geWaylandSurfaceObserveDrawable(&state, 640, 480));
    require(state.stable_samples == 1);
    require(!geWaylandSurfaceCanPresent(state));
    require(geWaylandSurfaceObserveDrawable(&state, 640, 480));
    require(state.phase == GE_WAYLAND_SURFACE_READY);
    require(geWaylandSurfaceCanPresent(state));

    /* A zero-sized configure invalidates readiness and never becomes a valid
     * sample. The next nonzero size must stabilize again. */
    require(!geWaylandSurfaceObserveDrawable(&state, 0, 0));
    require(state.phase == GE_WAYLAND_SURFACE_SHOWN);
    require(state.stable_samples == 0);
    require(!geWaylandSurfaceCanPresent(state));
    require(!geWaylandSurfaceObserveDrawable(&state, 800, 600));
    require(geWaylandSurfaceObserveDrawable(&state, 800, 600));
    require(geWaylandSurfaceCanPresent(state));
    require(state.width == 800 && state.height == 600);

    /* A real resize follows the same safe path as startup and cannot present
     * with a stale or half-configured drawable. */
    require(!geWaylandSurfaceObserveDrawable(&state, 1024, 768));
    require(state.phase == GE_WAYLAND_SURFACE_SHOWN);
    require(!geWaylandSurfaceCanPresent(state));
    require(geWaylandSurfaceObserveDrawable(&state, 1024, 768));
    require(geWaylandSurfaceCanPresent(state));

    /* Non-Wayland backends keep their existing immediate presentation policy,
     * but still reject an impossible zero-sized drawable. */
    GeWaylandSurfaceState non_wayland = geWaylandSurfaceCreate(false);
    require(geWaylandSurfaceObserveDrawable(&non_wayland, 640, 480));
    require(geWaylandSurfaceCanPresent(non_wayland));
    require(!geWaylandSurfaceObserveDrawable(&non_wayland, 0, 480));
    require(!geWaylandSurfaceCanPresent(non_wayland));

    /* Timeout decisions remain deterministic and side-effect free. */
    require(!geWaylandSurfaceTimedOut(1999, 2000));
    require(geWaylandSurfaceTimedOut(2000, 2000));
    require(geWaylandSurfaceTimedOut(2500, 2000));
    require(!geWaylandSurfaceTimedOut(100, 0));

    /* NVIDIA's Wayland EGL path in this environment is not stable when the
     * application asks SDL for an uncapped swap interval. The adapter must
     * retain the requested interval on X11, but use compositor-paced
     * presentation on Wayland rather than entering that driver path. */
    require(geWaylandEffectiveSwapInterval(false, 0) == 0);
    require(geWaylandEffectiveSwapInterval(false, 1) == 1);
    require(geWaylandEffectiveSwapInterval(true, 1) == 1);
    require(geWaylandEffectiveSwapInterval(true, 0) == 1);
    require(geWaylandEffectiveSwapInterval(true, -1) == 1);

    return 0;
}
