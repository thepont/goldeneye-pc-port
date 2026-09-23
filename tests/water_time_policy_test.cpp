#include "gfx_water_time_policy.h"

#include <cmath>
#include <cstdlib>
#include <limits>

static void require(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

int main()
{
    GeWaterClockState state = {};

    /* The first live sample establishes the clock without inventing a jump. */
    geWaterClockAdvance(&state, 10.0, 0.0);
    require(state.initialized == 1);
    require(state.elapsed == 0.0f);
    require(state.delta == 0.0f);

    /* Host time is capped so a debugger stop or scheduler stall cannot make
     * one frame leap through the wave phase. */
    geWaterClockAdvance(&state, 10.5, 0.0);
    require(std::fabs(state.delta - 0.25f) < 0.000001f);
    require(std::fabs(state.elapsed - 0.25f) < 0.000001f);

    /* The explicit test clock is independent of wall-clock spacing and is
     * itself capped to the live path's maximum step. */
    state = {};
    geWaterClockAdvance(&state, 100.0, 0.125);
    geWaterClockAdvance(&state, 100.001, 0.125);
    require(std::fabs(state.delta - 0.125f) < 0.000001f);
    require(std::fabs(state.elapsed - 0.25f) < 0.000001f);
    geWaterClockAdvance(&state, 100.002, 99.0);
    require(std::fabs(state.delta - 0.25f) < 0.000001f);
    require(std::fabs(state.elapsed - 0.50f) < 0.000001f);

    /* Invalid fixed steps fall back to host-time behavior; invalid host time
     * leaves the accumulated state safe and does not produce NaN. */
    state = {};
    geWaterClockAdvance(&state, 1.0, -1.0);
    geWaterClockAdvance(&state, 1.1, std::numeric_limits<double>::quiet_NaN());
    require(std::isfinite(state.elapsed));
    require(std::isfinite(state.delta));
    geWaterClockAdvance(&state, std::numeric_limits<double>::quiet_NaN(), 0.1);
    require(state.delta == 0.0f);
    require(std::isfinite(state.elapsed));

    /* A corrupted previous timestamp must not propagate NaN into the clock. */
    state = {};
    state.initialized = 1;
    state.previous = std::numeric_limits<double>::quiet_NaN();
    geWaterClockAdvance(&state, 2.0, -1.0);
    require(state.delta == 0.0f);
    require(std::isfinite(state.elapsed));

    /* Accumulation is also a trust boundary: an already-invalid fixture is
     * repaired on either clock path rather than remaining contagious. */
    state = {};
    state.elapsed = std::numeric_limits<float>::infinity();
    geWaterClockAdvance(&state, 2.0, 0.1);
    require(state.elapsed == 0.0f);

    state = {};
    state.initialized = 1;
    state.previous = 2.0;
    state.elapsed = std::numeric_limits<float>::infinity();
    geWaterClockAdvance(&state, 2.1, -1.0);
    require(state.elapsed == 0.0f);

    geWaterClockAdvance(nullptr, 1.0, 0.1);
    return 0;
}
