#include "../port/fast3d/advanced_lighting_policy.h"

#include <cmath>
#include <cstdlib>
#include <limits>

static bool close(float actual, float expected, float epsilon = 0.0001f)
{
    return std::fabs(actual - expected) <= epsilon;
}

static void require(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

#define assert(condition) require(condition)

int main()
{
    const GeAdvancedLightingMaterial default_material =
        geAdvancedLightingDefaultMaterial();
    assert(close(default_material.specular_color[0], 1.0f));
    assert(close(default_material.specular_color[1], 1.0f));
    assert(close(default_material.specular_color[2], 1.0f));
    assert(close(default_material.shininess, 32.0f));

    const float normal[3] = {0.0f, 0.0f, 1.0f};
    const float light[3] = {0.6f, 0.0f, 0.8f};
    const float view[3] = {0.8f, 0.0f, 0.6f};

    assert(geAdvancedLightingFinite3(normal));
    assert(!geAdvancedLightingFinite3(nullptr));
    const float nan_component[3] = {
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f};
    const float inf_component[3] = {
        std::numeric_limits<float>::infinity(), 0.0f, 1.0f};
    assert(!geAdvancedLightingFinite3(nan_component));
    assert(!geAdvancedLightingFinite3(inf_component));

    assert(close(geAdvancedLightingDot3(normal, normal), 1.0f));
    assert(close(geAdvancedLightingDot3(nullptr, normal), 0.0f));
    assert(close(geAdvancedLightingDot3(normal, nan_component), 0.0f));
    const float huge_vector[3] = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    assert(close(geAdvancedLightingDot3(huge_vector, huge_vector), 0.0f));

    float normalized[3] = {9.0f, 9.0f, 9.0f};
    assert(geAdvancedLightingNormalize3(normal, normalized));
    assert(close(normalized[0], 0.0f) && close(normalized[1], 0.0f) &&
           close(normalized[2], 1.0f));
    assert(!geAdvancedLightingNormalize3(normal, nullptr));
    assert(!geAdvancedLightingNormalize3(nullptr, normalized));
    assert(!geAdvancedLightingNormalize3(nan_component, normalized));
    const float tiny_vector[3] = {0.0001f, 0.0f, 0.0f};
    assert(!geAdvancedLightingNormalize3(tiny_vector, normalized));
    assert(close(normalized[0], 0.0f) && close(normalized[1], 0.0f) &&
           close(normalized[2], 1.0f));
    assert(!geAdvancedLightingNormalize3(huge_vector, normalized));

    /* H = normalize(L + V) = (sqrt(.5), 0, sqrt(.5)); a low-exponent
     * Blinn highlight survives where the old Phong reflection test cuts off. */
    assert(close(geBlinnPhongSpecular(normal, light, view, 1.0f),
                 0.70710678f));

    const float scaled_normal[3] = {0.0f, 0.0f, 100.0f};
    const float scaled_light[3] = {6.0f, 0.0f, 8.0f};
    const float scaled_view[3] = {80.0f, 0.0f, 60.0f};
    assert(close(geBlinnPhongSpecular(scaled_normal, scaled_light,
                                      scaled_view, 1.0f),
                 0.70710678f));

    assert(close(geBlinnPhongSpecular(normal, light, view, 4.0f),
                 0.25f));
    assert(close(geBlinnPhongSpecular(normal, light, view, 32.0f),
                 std::pow(0.70710678f, 32.0f), 0.000001f));

    const float back_light[3] = {0.0f, 0.0f, -1.0f};
    assert(close(geBlinnPhongSpecular(normal, back_light, view, 32.0f),
                 0.0f));

    const float opposite_view[3] = {0.0f, 0.0f, -1.0f};
    assert(close(geBlinnPhongSpecular(normal, normal, opposite_view, 32.0f),
                 0.0f));

    const float zero[3] = {0.0f, 0.0f, 0.0f};
    assert(close(geBlinnPhongSpecular(normal, zero, view, 32.0f), 0.0f));
    const float nan_vector[3] = {
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f};
    const float inf_vector[3] = {
        std::numeric_limits<float>::infinity(), 0.0f, 1.0f};
    assert(close(geBlinnPhongSpecular(nan_vector, light, view, 32.0f), 0.0f));
    assert(close(geBlinnPhongSpecular(normal, light, inf_vector, 32.0f), 0.0f));
    assert(close(geBlinnPhongSpecular(normal, light, view, 0.0f), 0.0f));

    float bounded = 0.0f;
    assert(geAdvancedLightingSanitizeShininess(0.5f, &bounded));
    assert(close(bounded, GE_ADVANCED_SHININESS_MIN));
    assert(geAdvancedLightingSanitizeShininess(1000.0f, &bounded));
    assert(close(bounded, GE_ADVANCED_SHININESS_MAX));
    assert(!geAdvancedLightingSanitizeShininess(32.0f, nullptr));
    assert(!geAdvancedLightingSanitizeShininess(
        std::numeric_limits<float>::infinity(), &bounded));
    assert(!geAdvancedLightingSanitizeShininess(-1.0f, &bounded));
    assert(!geAdvancedLightingSanitizeShininess(
        std::numeric_limits<float>::quiet_NaN(), &bounded));

    assert(close(geAdvancedLightingDisplayToLinear(0.0f), 0.0f));
    assert(close(geAdvancedLightingDisplayToLinear(1.0f), 1.0f));
    assert(close(geAdvancedLightingLinearToDisplay(0.0f), 0.0f));
    assert(close(geAdvancedLightingLinearToDisplay(1.0f), 1.0f));
    assert(close(geAdvancedLightingDisplayToLinear(0.18f),
                 0.0272118f, 0.00001f));
    assert(close(geAdvancedLightingLinearToDisplay(0.18f),
                 0.461356f, 0.00001f));
    const float display_sample = 0.63f;
    assert(close(geAdvancedLightingLinearToDisplay(
                     geAdvancedLightingDisplayToLinear(display_sample)),
                 display_sample, 0.00001f));
    assert(close(geAdvancedLightingDisplayToLinear(
                     std::numeric_limits<float>::quiet_NaN()), 0.0f));
    assert(close(geAdvancedLightingLinearToDisplay(
                     std::numeric_limits<float>::infinity()), 0.0f));
    return 0;
}
