#include "../port/fast3d/dynamic_lighting.h"

#include <cmath>
#include <cstdio>
#include <limits>

static bool near(float actual, float expected, float epsilon = 0.0001f)
{
    return std::fabs(actual - expected) <= epsilon;
}

int main()
{
    const GeDynamicLight light = {{0.0f, 0.0f, 0.0f}, 500.0f, 1.0f};
    const float facing[3] = {0.0f, 0.0f, 1.0f};
    const float away[3] = {0.0f, 0.0f, -1.0f};
    const float near_vertex[3] = {0.0f, 0.0f, -100.0f};
    const float far_vertex[3] = {0.0f, 0.0f, -400.0f};
    const float outside_vertex[3] = {0.0f, 0.0f, -501.0f};

    const float near_intensity = geDynamicLightIntensity(facing, near_vertex, light);
    const float far_intensity = geDynamicLightIntensity(facing, far_vertex, light);

    if (!near(near_intensity, 0.64f) || far_intensity <= 0.0f ||
        far_intensity >= near_intensity ||
        !near(geDynamicLightIntensity(away, near_vertex, light), 0.0f) ||
        !near(geDynamicLightIntensity(facing, outside_vertex, light), 0.0f)) {
        std::fprintf(stderr, "dynamic light direction/falloff contract failed\n");
        return 1;
    }

    const GeDynamicLight invalid_light = {{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f};
    const float zero_normal[3] = {0.0f, 0.0f, 0.0f};
    if (!near(geDynamicLightIntensity(facing, near_vertex, invalid_light), 0.0f) ||
        !near(geDynamicLightIntensity(zero_normal, near_vertex, light), 0.0f)) {
        std::fprintf(stderr, "dynamic light invalid-input contract failed\n");
        return 1;
    }

    const GeDynamicLight bright_light = {{0.0f, 0.0f, 0.0f}, 500.0f, 9.0f};
    const GeDynamicLight nan_radius = {{0.0f, 0.0f, 0.0f},
                                       std::numeric_limits<float>::quiet_NaN(), 1.0f};
    const float nan_normal[3] = {
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f};
    const float nan_vertex[3] = {
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    if (!near(geDynamicLightIntensity(facing, near_vertex, bright_light), 1.0f) ||
        !near(geDynamicLightIntensity(facing, near_vertex, nan_radius), 0.0f) ||
        !near(geDynamicLightIntensity(nan_normal, near_vertex, light), 0.0f) ||
        !near(geDynamicLightIntensity(facing, nan_vertex, light), 0.0f)) {
        std::fprintf(stderr, "dynamic light clamp/NaN contract failed\n");
        return 1;
    }

    float previous = 1.0f;
    for (int distance = 1; distance < 500; ++distance)
    {
        const float vertex[3] = {0.0f, 0.0f, -(float)distance};
        const float intensity = geDynamicLightIntensity(facing, vertex, light);
        if (intensity < 0.0f || intensity > 1.0f || intensity > previous + 0.0001f)
        {
            std::fprintf(stderr, "dynamic light distance monotonicity failed\n");
            return 1;
        }
        previous = intensity;
    }

    const float scaled_normal[3] = {0.0f, 0.0f, 100.0f};
    const float perpendicular[3] = {1.0f, 0.0f, 0.0f};
    const float invalid_position[3] = {
        std::numeric_limits<float>::infinity(), 0.0f, 0.0f};
    const GeDynamicLight invalid_position_light = {
        {invalid_position[0], invalid_position[1], invalid_position[2]}, 500.0f, 1.0f};
    if (!near(geDynamicLightIntensity(scaled_normal, near_vertex, light), near_intensity) ||
        !near(geDynamicLightIntensity(perpendicular, near_vertex, light), 0.0f) ||
        !near(geDynamicLightIntensity(facing, near_vertex, invalid_position_light), 0.0f)) {
        std::fprintf(stderr, "dynamic light normal/position matrix failed\n");
        return 1;
    }

    return 0;
}
