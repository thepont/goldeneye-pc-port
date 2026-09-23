#ifndef GE007_SKY_WATER_POLICY_H
#define GE007_SKY_WATER_POLICY_H

/* These values are the PC renderer's water/sky texture-coordinate policy.
 * Keep the arithmetic here free of renderer state so the D245 seam can be
 * tested without starting SDL or the game. */
#define GE_SKY_TC_WRAP 2048
#define GE_SKY_TC_MAX_SHIFT 5

typedef struct GeSkyWaterShiftState {
    int shift;
    int initialized;
} GeSkyWaterShiftState;

/* Pick the smallest RDP tile shift that keeps one fan's texture-coordinate
 * span representable as a signed 16-bit value. The lower buckets use a strict
 * boundary so an S10.5 rounding step cannot toggle k between adjacent input
 * samples. The final in-range bucket retains its inclusive endpoint before
 * the explicit clamp bucket, preserving the representable maximum. */
static inline int geSkyWaterPickShift(float span)
{
    int shift;

    for (shift = 0; shift < GE_SKY_TC_MAX_SHIFT; ++shift)
    {
        const float scaled =
            (span + (float)GE_SKY_TC_WRAP) / (float)(1 << shift);
        const int is_final_in_range_bucket =
            shift == GE_SKY_TC_MAX_SHIFT - 1;
        if (scaled < 32767.0f ||
            (is_final_in_range_bucket && scaled <= 32767.0625f))
        {
            break;
        }
    }
    return shift;
}

/* Keep the selected shift stable while a moving camera hovers around an s16
 * boundary. The legacy policy is intentionally still exposed above for
 * diagnostics and for callers that need a stateless answer. Enhanced water
 * owns one of these states per axis and retains it between frames. */
static inline void geSkyWaterShiftReset(GeSkyWaterShiftState *state)
{
    if (state != 0) {
        state->shift = 0;
        state->initialized = 0;
    }
}

static inline float geSkyWaterShiftLimit(int shift)
{
    if (shift >= GE_SKY_TC_MAX_SHIFT) {
        return 3.402823466e+38F;
    }
    return 32767.0f * (float)(1 << shift) - (float)GE_SKY_TC_WRAP;
}

static inline float geSkyWaterShiftMargin(int shift)
{
    const float limit = geSkyWaterShiftLimit(shift);
    const float margin = limit * 0.05f;
    /* Every valid shift bucket has a margin larger than one texel. */
    return margin;
}

static inline int geSkyWaterPickShiftHysteretic(GeSkyWaterShiftState *state,
                                                 float span)
{
    if (!(span >= 0.0f)) {
        span = 0.0f;
    }

    if (state == 0) {
        return geSkyWaterPickShift(span);
    }

    if (!state->initialized) {
        state->shift = geSkyWaterPickShift(span);
        state->initialized = 1;
        return state->shift;
    }

    /* Increase only after the current bucket is safely exceeded. A large
     * jump may cross more than one bucket, so converge in one call. */
    while (state->shift < GE_SKY_TC_MAX_SHIFT &&
           span > geSkyWaterShiftLimit(state->shift) +
                       geSkyWaterShiftMargin(state->shift)) {
        ++state->shift;
    }

    /* Decrease only after the next smaller bucket has a clear margin. */
    while (state->shift > 0 &&
           span < geSkyWaterShiftLimit(state->shift - 1) -
                       geSkyWaterShiftMargin(state->shift - 1)) {
        --state->shift;
    }

    return state->shift;
}

#endif /* GE007_SKY_WATER_POLICY_H */
