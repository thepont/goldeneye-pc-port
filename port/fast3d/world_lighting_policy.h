#ifndef GE007_WORLD_LIGHTING_POLICY_H
#define GE007_WORLD_LIGHTING_POLICY_H

#ifdef __cplusplus
#include <cmath>
#endif

/* Shared, allocation-free profiles for port-owned emissive effects. The
 * game-state adapter and tests use these same values; the renderer remains
 * responsible only for registering and evaluating the resulting lights. */
typedef struct GeWorldLightProfile {
    float radius;
    float intensity;
    float color[3];
    int priority;
} GeWorldLightProfile;

static inline float geWorldLightingClamp(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static inline int geWorldLightingIsFinite(float value)
{
#ifdef __cplusplus
    return std::isfinite(value);
#else
    /* Keep this policy header independent from the game's shadowed math.h. */
    return value == value && value < 3.402823466e+38F &&
           value > -3.402823466e+38F;
#endif
}

static inline GeWorldLightProfile geWorldMuzzleFlashLightProfile(void)
{
    GeWorldLightProfile profile = {
        240.0f,
        2.5f,
        {1.0f, 0.35f, 0.06f},
        90,
    };
    return profile;
}

/* visual_size comes from the active explosion particles. Age is deliberately
 * treated as a cosmetic input only: malformed or unavailable game state gets
 * a finite fallback rather than poisoning the renderer's vertex colours. */
static inline GeWorldLightProfile geWorldExplosionLightProfile(float visual_size,
                                                               float age)
{
    if (!geWorldLightingIsFinite(visual_size) || visual_size <= 0.0f) {
        visual_size = 80.0f;
    }
    if (!geWorldLightingIsFinite(age) || age < 0.0f) {
        age = 0.0f;
    }

    const float life = geWorldLightingClamp(age / 120.0f, 0.0f, 1.0f);
    const float fade = 1.0f - (0.65f * life);
    GeWorldLightProfile profile = {
        geWorldLightingClamp(180.0f + (visual_size * 2.0f), 180.0f, 900.0f),
        geWorldLightingClamp(2.6f * fade, 0.25f, 2.6f),
        {1.0f, 0.24f + (0.32f * fade), 0.025f},
        100,
    };
    return profile;
}

#endif /* GE007_WORLD_LIGHTING_POLICY_H */
