#ifndef GE007_GFX_WATER_TIME_POLICY_H
#define GE007_GFX_WATER_TIME_POLICY_H

/* Pure clock policy for the water material.  The renderer supplies the host
 * clock, but elapsed-time accumulation and the deterministic test override
 * live here so they can be checked without SDL, OpenGL, or a running game. */

typedef struct GeWaterClockState {
    float elapsed;
    float delta;
    double previous;
    int initialized;
} GeWaterClockState;

static inline int geWaterClockFinite(double value)
{
    return __builtin_isfinite(value) != 0;
}

static inline void geWaterClockAdvance(GeWaterClockState *state,
                                       double now,
                                       double fixed_delta)
{
    if (state == 0) {
        return;
    }

    state->delta = 0.0f;
    if (!geWaterClockFinite(now)) {
        return;
    }

    /* A fixed step is an explicit test seam. Cap it to the same maximum as
     * the live host-clock path so a malformed fixture cannot jump the water
     * simulation through a different numerical regime. */
    if (geWaterClockFinite(fixed_delta) && fixed_delta > 0.0) {
        const double bounded = fixed_delta > 0.25 ? 0.25 : fixed_delta;
        state->delta = (float)bounded;
        state->elapsed += state->delta;
        if (!__builtin_isfinite(state->elapsed)) {
            state->elapsed = 0.0f;
        }
        state->previous = now;
        state->initialized = 1;
        return;
    }

    if (!state->initialized || now < state->previous) {
        state->elapsed = 0.0f;
    } else {
        double delta = now - state->previous;
        if (!geWaterClockFinite(delta) || delta < 0.0) {
            delta = 0.0;
        }
        if (delta > 0.25) {
            delta = 0.25;
        }
        state->delta = (float)delta;
        state->elapsed += state->delta;
        if (!__builtin_isfinite(state->elapsed)) {
            state->elapsed = 0.0f;
        }
    }
    state->previous = now;
    state->initialized = 1;
}

#endif /* GE007_GFX_WATER_TIME_POLICY_H */
