#include "depth_of_field_policy.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>

static void geTestRequire(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

#undef assert
#define assert(condition) geTestRequire((condition))

static bool near(float actual, float expected, float epsilon = 0.0001f)
{
    return std::fabs(actual - expected) <= epsilon;
}

static GeDofConfig defaultConfig()
{
    return {0.50f, 0.10f, 0.40f, 0.60f, 0.90f, 8.0f};
}

static void expectInvalidDepth(float depth, const GeDofConfig &config)
{
    const GeDofResult result = geDofEvaluate(depth, config);
    assert(result.valid == 0);
    assert(near(result.near_blur, 0.0f));
    assert(near(result.far_blur, 0.0f));
    assert(near(result.radius_pixels, 0.0f));
    assert(near(geDofCircleOfConfusion(depth, config), 0.0f));
}

int main()
{
    const GeDofConfig config = defaultConfig();
    if (!geDofConfigIsValid(config)) return 1;
    if (!near(geDofClamp01(-1.0f), 0.0f) ||
        !near(geDofClamp01(2.0f), 1.0f) ||
        !near(geDofClamp01(0.25f), 0.25f)) {
        return 1;
    }
    volatile float degenerate_edge0 = 1.0f;
    volatile float degenerate_edge1 = 1.0f;
    volatile float degenerate_below = 0.5f;
    volatile float degenerate_above = 1.5f;
    const float degenerate_low_result = geDofSmoothstep(
        degenerate_edge0, degenerate_edge1, degenerate_below);
    const float degenerate_high_result = geDofSmoothstep(
        degenerate_edge0, degenerate_edge1, degenerate_above);
    if (!near(degenerate_low_result, 0.0f) ||
        !near(degenerate_high_result, 1.0f)) {
        return 1;
    }
    degenerate_edge1 = 0.0f;
    const float reversed_result = geDofSmoothstep(
        degenerate_edge0, degenerate_edge1, degenerate_below);
    if (!near(reversed_result, 1.0f)) {
        return 1;
    }

    const GeDofResult focused = geDofEvaluate(0.50f, config);
    assert(focused.valid == 1);
    assert(near(focused.near_blur, 0.0f));
    assert(near(focused.far_blur, 0.0f));
    assert(near(focused.radius_pixels, 0.0f));
    assert(near(geDofCircleOfConfusion(0.50f, config), 0.0f));

    const GeDofResult near_result = geDofEvaluate(0.05f, config);
    const GeDofResult near_mid_result = geDofEvaluate(0.25f, config);
    const GeDofResult far_mid_result = geDofEvaluate(0.75f, config);
    const GeDofResult far_result = geDofEvaluate(0.95f, config);
    assert(near_result.valid == 1 && near_result.near_blur == 1.0f);
    assert(near_mid_result.near_blur > 0.0f && near_mid_result.near_blur < 1.0f);
    assert(near_mid_result.far_blur == 0.0f);
    assert(far_mid_result.far_blur > 0.0f && far_mid_result.far_blur < 1.0f);
    assert(far_mid_result.near_blur == 0.0f);
    assert(far_result.far_blur == 1.0f);
    assert(near(geDofCircleOfConfusion(0.05f, config), 1.0f));
    assert(near(geDofCircleOfConfusion(0.95f, config), -1.0f));
    assert(near_result.radius_pixels == config.max_radius_pixels);
    assert(far_result.radius_pixels == config.max_radius_pixels);

    float previous_near = 1.0f;
    for (int i = 0; i <= 50; ++i) {
        const float depth = 0.10f + (0.40f * (float)i / 50.0f);
        const GeDofResult result = geDofEvaluate(depth, config);
        assert(result.near_blur <= previous_near + 0.0001f);
        previous_near = result.near_blur;
    }
    float previous_far = 0.0f;
    for (int i = 0; i <= 50; ++i) {
        const float depth = 0.60f + (0.40f * (float)i / 50.0f);
        const GeDofResult result = geDofEvaluate(depth, config);
        assert(result.far_blur + 0.0001f >= previous_far);
        previous_far = result.far_blur;
    }

    expectInvalidDepth(-0.01f, config);
    expectInvalidDepth(1.01f, config);
    expectInvalidDepth(std::numeric_limits<float>::quiet_NaN(), config);
    expectInvalidDepth(std::numeric_limits<float>::infinity(), config);

    GeDofConfig invalid = config;
    invalid.focus_depth = std::numeric_limits<float>::infinity();
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.near_blur_depth = std::numeric_limits<float>::quiet_NaN();
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.near_clear_depth = std::numeric_limits<float>::quiet_NaN();
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.far_clear_depth = std::numeric_limits<float>::quiet_NaN();
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.far_blur_depth = std::numeric_limits<float>::quiet_NaN();
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.max_radius_pixels = std::numeric_limits<float>::quiet_NaN();
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.near_blur_depth = -0.01f;
    if (geDofConfigIsValid(invalid)) return 1;
    invalid.near_blur_depth = invalid.near_clear_depth;
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.near_clear_depth = invalid.focus_depth + 0.01f;
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.far_clear_depth = invalid.focus_depth - 0.01f;
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.far_blur_depth = invalid.far_clear_depth;
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.far_blur_depth = 1.01f;
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.max_radius_pixels = -1.0f;
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.max_radius_pixels = GE_DOF_MAX_RADIUS_PIXELS + 1.0f;
    if (geDofConfigIsValid(invalid)) return 1;
    invalid = config;
    invalid.focus_depth = std::numeric_limits<float>::quiet_NaN();
    if (geDofConfigIsValid(invalid)) return 1;
    expectInvalidDepth(0.5f, invalid);

    if (!near(geDofDepthWeight(0.5f, 0.5f, 0.1f), 1.0f) ||
        !near(geDofDepthWeight(0.5f, 0.55f, 0.1f), 0.5f) ||
        !near(geDofDepthWeight(0.5f, 0.6f, 0.1f), 0.0f) ||
        !near(geDofDepthWeight(0.5f, 0.7f, 0.0f), 0.0f) ||
        !near(geDofDepthWeight(std::numeric_limits<float>::quiet_NaN(),
                               0.5f, 0.1f), 0.0f) ||
        !near(geDofDepthWeight(0.5f,
                               std::numeric_limits<float>::infinity(),
                               0.1f), 0.0f) ||
        !near(geDofDepthWeight(0.5f, 0.5f,
                               std::numeric_limits<float>::quiet_NaN()), 0.0f) ||
        !near(geDofDepthWeight(0.5f, 0.5f, -1.0f), 0.0f) ||
        !near(geDofDepthWeight(std::numeric_limits<float>::max(),
                               -std::numeric_limits<float>::max(), 0.1f), 0.0f)) {
        return 1;
    }

    const GeDofViewport viewport = {0.10f, 0.20f, 0.80f, 0.60f};
    if (!geDofViewportIsValid(viewport) ||
        !geDofViewportContains(viewport, 0.10f, 0.20f) ||
        !geDofViewportContains(viewport, 0.90f, 0.80f) ||
        geDofViewportContains(viewport, 0.05f, 0.50f) ||
        geDofViewportContains(viewport, 0.50f, 0.90f) ||
        geDofViewportContains(viewport,
                              std::numeric_limits<float>::quiet_NaN(), 0.5f) ||
        geDofViewportContains(viewport, 0.5f,
                              std::numeric_limits<float>::infinity()) ||
        geDofViewportContains(viewport, 0.5f, 0.1f) ||
        geDofViewportContains(viewport, 0.1f, 0.9f)) {
        return 1;
    }
    const GeDofUv clamped = geDofClampUvToViewport(0.05f, 0.95f, viewport);
    assert(near(clamped.u, 0.10f));
    assert(near(clamped.v, 0.80f));
    const GeDofUv unchanged = geDofClampUvToViewport(0.50f, 0.50f, viewport);
    assert(near(unchanged.u, 0.50f));
    assert(near(unchanged.v, 0.50f));
    const GeDofUv clamped_high = geDofClampUvToViewport(0.95f, 0.95f, viewport);
    assert(near(clamped_high.u, 0.90f));
    assert(near(clamped_high.v, 0.80f));
    const GeDofUv clamped_low = geDofClampUvToViewport(0.05f, 0.05f, viewport);
    assert(near(clamped_low.u, 0.10f));
    assert(near(clamped_low.v, 0.20f));

    GeDofViewport invalid_viewport = viewport;
    invalid_viewport.width = 0.0f;
    if (geDofViewportIsValid(invalid_viewport)) return 1;
    invalid_viewport = viewport;
    invalid_viewport.x = -0.01f;
    if (geDofViewportIsValid(invalid_viewport)) return 1;
    invalid_viewport = viewport;
    invalid_viewport.height = std::numeric_limits<float>::infinity();
    if (geDofViewportIsValid(invalid_viewport)) return 1;
    invalid_viewport = viewport;
    invalid_viewport.y = std::numeric_limits<float>::quiet_NaN();
    if (geDofViewportIsValid(invalid_viewport)) return 1;
    invalid_viewport = viewport;
    invalid_viewport.x = 0.30f;
    invalid_viewport.width = 0.80f;
    if (geDofViewportIsValid(invalid_viewport)) return 1;
    invalid_viewport = viewport;
    invalid_viewport.y = 0.30f;
    invalid_viewport.height = 0.80f;
    if (geDofViewportIsValid(invalid_viewport)) return 1;
    const GeDofUv unchanged_invalid = geDofClampUvToViewport(
        0.5f, 0.5f, invalid_viewport);
    assert(near(unchanged_invalid.u, 0.5f));
    assert(near(unchanged_invalid.v, 0.5f));

    if (geDofShouldApply(0, 1, 1) ||
        geDofShouldApply(1, 0, 1) ||
        geDofShouldApply(1, 1, 0) ||
        geDofShouldApply(1, 1, -1) ||
        geDofShouldApply(1, 1, GE_DOF_MAX_VIEWPORTS + 1) ||
        !geDofShouldApply(1, 1, 1) ||
        !geDofShouldApply(1, 1, 2)) {
        return 1;
    }

    return 0;
}
