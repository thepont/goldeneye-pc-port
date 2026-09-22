#include "../port/fast3d/dynamic_lighting.h"
#include "../port/fast3d/postfx_policy.h"
#include "../port/fast3d/vector_text_layout.h"
#include "../port/fast3d/world_lighting_policy.h"

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
    const float nan_normal_y[3] = {
        0.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f};
    const float nan_normal_z[3] = {
        0.0f, 1.0f, std::numeric_limits<float>::quiet_NaN()};
    const float nan_vertex[3] = {
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    const float nan_vertex_y[3] = {
        0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f};
    const float nan_vertex_z[3] = {
        0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()};
    if (!near(geDynamicLightIntensity(facing, near_vertex, bright_light), 1.0f) ||
        !near(geDynamicLightIntensity(facing, near_vertex, nan_radius), 0.0f) ||
        !near(geDynamicLightIntensity(nan_normal, near_vertex, light), 0.0f) ||
        !near(geDynamicLightIntensity(nan_normal_y, near_vertex, light), 0.0f) ||
        !near(geDynamicLightIntensity(nan_normal_z, near_vertex, light), 0.0f) ||
        !near(geDynamicLightIntensity(facing, nan_vertex, light), 0.0f) ||
        !near(geDynamicLightIntensity(facing, nan_vertex_y, light), 0.0f) ||
        !near(geDynamicLightIntensity(facing, nan_vertex_z, light), 0.0f)) {
        std::fprintf(stderr, "dynamic light clamp/NaN contract failed\n");
        return 1;
    }

    GeDynamicLight invalid_colour = light;
    invalid_colour.color[0] = std::numeric_limits<float>::quiet_NaN();
    GeDynamicLight invalid_direction = light;
    invalid_direction.direction[1] = std::numeric_limits<float>::quiet_NaN();
    GeDynamicLight invalid_inner = light;
    invalid_inner.inner_cos = std::numeric_limits<float>::quiet_NaN();
    GeDynamicLight invalid_outer = light;
    invalid_outer.outer_cos = std::numeric_limits<float>::quiet_NaN();
    GeDynamicLight invalid_intensity = light;
    invalid_intensity.intensity = 0.0f;
    GeDynamicLight invalid_nan_intensity = light;
    invalid_nan_intensity.intensity = std::numeric_limits<float>::quiet_NaN();
    if (geDynamicLightValid(invalid_colour) ||
        geDynamicLightValid(invalid_direction) ||
        geDynamicLightValid(invalid_inner) ||
        geDynamicLightValid(invalid_outer) ||
        geDynamicLightValid(invalid_intensity) ||
        geDynamicLightValid(invalid_nan_intensity)) {
        std::fprintf(stderr, "dynamic light validation matrix failed\n");
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

    const float huge_position[3] = {
        std::numeric_limits<float>::max(), 0.0f, 0.0f};
    const float huge_vertex[3] = {
        -std::numeric_limits<float>::max(), 0.0f, 0.0f};
    const GeDynamicLight huge_position_light = {
        {huge_position[0], huge_position[1], huge_position[2]}, 500.0f, 1.0f};
    const float huge_normal[3] = {
        std::numeric_limits<float>::max(), 1.0f, 1.0f};
    if (!near(geDynamicLightIntensity(facing, huge_vertex, huge_position_light), 0.0f) ||
        !near(geDynamicLightIntensity(huge_normal, near_vertex, light), 0.0f)) {
        std::fprintf(stderr, "dynamic light overflow safety contract failed\n");
        return 1;
    }

    const GeDynamicLight zero_distance_light = {
        {near_vertex[0], near_vertex[1], near_vertex[2]}, 500.0f, 1.0f};
    if (!near(geDynamicLightIntensity(facing, near_vertex, zero_distance_light), 0.0f)) {
        std::fprintf(stderr, "dynamic light zero-distance contract failed\n");
        return 1;
    }

    GeDynamicLight coloured = light;
    coloured.color[0] = 1.0f;
    coloured.color[1] = 0.25f;
    coloured.color[2] = 0.0f;
    float contribution[3] = { 0.0f, 0.0f, 0.0f };
    geDynamicLightContribution(facing, near_vertex, coloured, contribution);
    if (!near(contribution[0], near_intensity) ||
        !near(contribution[1], near_intensity * 0.25f) ||
        !near(contribution[2], 0.0f)) {
        std::fprintf(stderr, "dynamic light colour contribution failed\n");
        return 1;
    }

    GeDynamicLight spot = light;
    spot.type = GeDynamicLightType::Spot;
    spot.direction[0] = 0.0f;
    spot.direction[1] = 0.0f;
    spot.direction[2] = -1.0f;
    spot.inner_cos = 0.95f;
    spot.outer_cos = 0.5f;
    const float spot_vertex[3] = { 0.0f, 0.0f, -100.0f };
    const float side_vertex[3] = { 100.0f, 0.0f, -100.0f };
    const float spot_on = geDynamicLightIntensity(facing, spot_vertex, spot);
    const float spot_side = geDynamicLightIntensity(facing, side_vertex, spot);
    if (spot_on <= 0.0f || spot_side >= spot_on || spot_side <= 0.0f) {
        std::fprintf(stderr, "dynamic spot light cone failed\n");
        return 1;
    }

    GeDynamicLight invalid_spot_order = spot;
    invalid_spot_order.inner_cos = -0.5f;
    invalid_spot_order.outer_cos = 0.5f;
    GeDynamicLight zero_direction_spot = spot;
    zero_direction_spot.direction[0] = 0.0f;
    zero_direction_spot.direction[1] = 0.0f;
    zero_direction_spot.direction[2] = 0.0f;
    GeDynamicLight flat_cone_spot = spot;
    flat_cone_spot.inner_cos = 0.5f;
    flat_cone_spot.outer_cos = 0.5f;
    if (geDynamicLightValid(invalid_spot_order) ||
        !near(geDynamicLightIntensity(facing, spot_vertex, zero_direction_spot), 0.0f) ||
        geDynamicLightIntensity(facing, spot_vertex, flat_cone_spot) < 0.0f) {
        std::fprintf(stderr, "dynamic spot light validation edge failed\n");
        return 1;
    }

    GeDynamicLight invalid_colour_contribution = coloured;
    invalid_colour_contribution.color[1] = std::numeric_limits<float>::quiet_NaN();
    float invalid_colour_output[3] = { 0.0f, 0.0f, 0.0f };
    geDynamicLightContribution(facing, near_vertex, invalid_colour_contribution,
                               invalid_colour_output);
    if (!near(invalid_colour_output[1], 0.0f)) {
        std::fprintf(stderr, "dynamic light invalid-colour contribution failed\n");
        return 1;
    }

    GeDynamicLightRegistry registry;
    if (!registry.add(light) || !registry.add(coloured) || registry.size() != 2) {
        std::fprintf(stderr, "dynamic light registry insertion failed\n");
        return 1;
    }
    registry.clear();
    for (std::size_t i = 0; i < GeDynamicLightRegistry::capacity(); ++i) {
        GeDynamicLight entry = light;
        entry.priority = static_cast<int>(i);
        if (!registry.add(entry)) {
            std::fprintf(stderr, "dynamic light registry capacity insertion failed\n");
            return 1;
        }
    }
    GeDynamicLight rejected = light;
    rejected.priority = 0;
    if (registry.add(rejected) || registry.size() != GeDynamicLightRegistry::capacity()) {
        std::fprintf(stderr, "dynamic light registry overflow policy failed\n");
        return 1;
    }
    if (registry.add(invalid_light) || registry.size() != GeDynamicLightRegistry::capacity()) {
        std::fprintf(stderr, "dynamic light registry validation policy failed\n");
        return 1;
    }
    GeDynamicLight promoted = light;
    promoted.priority = 100;
    if (!registry.add(promoted) || registry.size() != GeDynamicLightRegistry::capacity()) {
        std::fprintf(stderr, "dynamic light registry priority replacement failed\n");
        return 1;
    }

    registry.clear();
    for (std::size_t i = 0; i < GeDynamicLightRegistry::capacity(); ++i) {
        GeDynamicLight entry = light;
        entry.priority = 100 - static_cast<int>(i);
        if (!registry.add(entry)) {
            std::fprintf(stderr, "dynamic light descending-priority insertion failed\n");
            return 1;
        }
    }
    if (!registry.add(promoted) || registry.size() != GeDynamicLightRegistry::capacity()) {
        std::fprintf(stderr, "dynamic light non-first weakest replacement failed\n");
        return 1;
    }

    const GeWorldLightProfile muzzle = geWorldMuzzleFlashLightProfile();
    const GeWorldLightProfile explosion = geWorldExplosionLightProfile(200.0f, 0.0f);
    const GeWorldLightProfile faded_explosion = geWorldExplosionLightProfile(200.0f, 120.0f);
    const GeWorldLightProfile default_explosion = geWorldExplosionLightProfile(0.0f, -1.0f);
    const GeWorldLightProfile overlong_explosion = geWorldExplosionLightProfile(10000.0f, 1000.0f);
    if (muzzle.radius <= 0.0f || muzzle.intensity <= 0.0f ||
        muzzle.color[0] <= muzzle.color[1] || muzzle.priority <= 0 ||
        explosion.radius <= 0.0f || explosion.intensity <= faded_explosion.intensity ||
        faded_explosion.intensity <= 0.0f || default_explosion.radius <= 0.0f ||
        overlong_explosion.radius != 900.0f || overlong_explosion.intensity != 0.91f) {
        std::fprintf(stderr, "world effect light profile contract failed\n");
        return 1;
    }

    const GeWorldLightProfile malformed =
        geWorldExplosionLightProfile(std::numeric_limits<float>::quiet_NaN(),
                                     std::numeric_limits<float>::quiet_NaN());
    if (!std::isfinite(malformed.radius) || !std::isfinite(malformed.intensity) ||
        malformed.radius <= 0.0f || malformed.intensity <= 0.0f ||
        geWorldLightingClamp(-1.0f, 0.0f, 1.0f) != 0.0f) {
        std::fprintf(stderr, "world effect light profile validation failed\n");
        return 1;
    }

    const GeVectorTextScreenRect screen = {10, 20, 640, 480};
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    geVectorTextMapLogicalPoint(0.0f, 0.0f, 640.0f, 480.0f,
                                screen, &screen_x, &screen_y);
    if (!near(screen_x, 10.0f) || !near(screen_y, 20.0f)) {
        std::fprintf(stderr, "vector text origin mapping failed\n");
        return 1;
    }
    geVectorTextMapLogicalPoint(640.0f, 480.0f, 640.0f, 480.0f,
                                screen, &screen_x, &screen_y);
    if (!near(screen_x, 650.0f) || !near(screen_y, 500.0f)) {
        std::fprintf(stderr, "vector text extent mapping failed\n");
        return 1;
    }

    if (geVectorTextRasterPixelHeightForScreen(13, 640, 480, 640.0f, 480.0f) != 13 ||
        geVectorTextRasterPixelHeightForScreen(13, 1914, 1074, 640.0f, 480.0f) != 39 ||
        geVectorTextRasterPixelHeightForScreen(13, 320, 240, 640.0f, 480.0f) != 7 ||
        geVectorTextRasterPixelHeightForScreen(0, 0, 0, 0.0f, 0.0f) != 1 ||
        geVectorTextRasterPixelHeightForScreen(
            -1, -1, -1, -1.0f, -1.0f) != 1 ||
        geVectorTextRasterPixelHeightForScreen(
            1, 1, 1, std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()) != 1) {
        std::fprintf(stderr, "vector text output-scale policy failed\n");
        return 1;
    }

    geVectorTextMapLogicalPoint(10.0f, 20.0f, 0.0f, 0.0f,
                                screen, &screen_x, &screen_y);
    if (!near(screen_x, 6410.0f) || !near(screen_y, 9620.0f)) {
        std::fprintf(stderr, "vector text invalid logical extent policy failed\n");
        return 1;
    }

    if (!geVectorTextNeedsAtlasRebuild(0, 13) ||
        !geVectorTextNeedsAtlasRebuild(13, 39) ||
        geVectorTextNeedsAtlasRebuild(39, 39) ||
        geVectorTextNeedsAtlasRebuild(39, 0)) {
        std::fprintf(stderr, "vector text atlas regeneration policy failed\n");
        return 1;
    }

    if (gePostFxClampSharpen(-1) != 0 ||
        gePostFxClampSharpen(35) != 35 ||
        gePostFxClampSharpen(101) != 100 ||
        gePostFxShouldApply(0, 35) ||
        gePostFxShouldApply(1, 0) ||
        !gePostFxShouldApply(1, 35)) {
        std::fprintf(stderr, "postfx policy contract failed\n");
        return 1;
    }

    return 0;
}
