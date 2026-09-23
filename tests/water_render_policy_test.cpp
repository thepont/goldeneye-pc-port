/* Water-composite behavior contracts.
 *
 * These tests deliberately stay independent of SDL and OpenGL.  The renderer
 * is allowed to change its implementation, but it must preserve the safety
 * and boundary behavior that makes a reflection/refraction/contact pass
 * usable around ships, dam walls, and other vertical geometry.
 */
#include "gfx_water_composite_policy.h"

#include <cstddef>
#include <cstdio>

struct FailureLog {
    int count = 0;

    void expect(bool condition, const char *message)
    {
        if (!condition) {
            ++count;
            std::fprintf(stderr, "RED: %s\n", message);
        }
    }
};

static bool approximately(float actual, float expected, float epsilon = 0.0001f)
{
    const float difference = actual - expected;
    return (difference < 0.0f ? -difference : difference) <= epsilon;
}

static bool vectorApproximately(GeWaterVec3 actual,
                                GeWaterVec3 expected,
                                float epsilon = 0.0001f)
{
    return approximately(actual.x, expected.x, epsilon) &&
           approximately(actual.y, expected.y, epsilon) &&
           approximately(actual.z, expected.z, epsilon);
}

static float luminance(GeWaterVec3 color)
{
    return color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
}

static void test_display_transfer(FailureLog &failures)
{
    failures.expect(approximately(geWaterLinearToSrgb(0.0f), 0.0f),
                    "zero linear water must remain zero after display transfer");
    failures.expect(approximately(geWaterLinearToSrgb(1.0f), 1.0f),
                    "unit linear water must remain unit after display transfer");
    failures.expect(approximately(geWaterLinearToSrgb(0.18f), 0.461356f,
                                  0.0002f),
                    "linear middle gray must use the sRGB transfer curve");
    failures.expect(approximately(geWaterLinearToSrgb(__builtin_nanf("")),
                                  0.0f),
                    "non-finite linear water must fail closed at the display boundary");
    failures.expect(approximately(geWaterSrgbToLinear(0.461356f), 0.18f,
                                  0.0002f),
                    "sRGB water samples must decode back to linear values");
    failures.expect(approximately(geWaterSrgbToLinear(0.01f),
                                  0.01f / 12.92f, 0.000001f),
                    "low sRGB water samples must use the linear transfer segment");
    failures.expect(approximately(geWaterSrgbToLinear(__builtin_inff()), 0.0f),
                    "non-finite sRGB water must fail closed at the linear boundary");

    const GeWaterVec3 linear = {0.0286f, 0.1792f, 0.2688f};
    const GeWaterVec3 encoded = geWaterLinearColorToSrgb(linear);
    const GeWaterVec3 decoded = geWaterSrgbColorToLinear(encoded);
    failures.expect(encoded.x > linear.x && encoded.y > linear.y &&
                        encoded.z > linear.z,
                    "readable linear water must brighten at the display boundary");
    failures.expect(vectorApproximately(decoded, linear, 0.0002f),
                    "water display transfer must be reversible within precision");
}

static void test_feature_switches(FailureLog &failures)
{
    failures.expect(geWaterFeatureEnabled(GE_WATER_FEATURE_ALL,
                                          GE_WATER_FEATURE_REFLECTION),
                    "all water features must enable reflection by default");
    failures.expect(geWaterFeatureEnabled(GE_WATER_FEATURE_ALL,
                                          GE_WATER_FEATURE_REFRACTION),
                    "all water features must enable refraction by default");
    failures.expect(!geWaterFeatureEnabled(
                        GE_WATER_FEATURE_ALL & ~GE_WATER_FEATURE_NORMALS,
                        GE_WATER_FEATURE_NORMALS),
                    "normal diagnostic switch must be able to disable normals");
    failures.expect(!geWaterFeatureEnabled(
                        GE_WATER_FEATURE_ALL & ~GE_WATER_FEATURE_FOAM,
                        GE_WATER_FEATURE_FOAM),
                    "foam diagnostic switch must be able to disable foam");
}

static void test_low_light_water_visibility(FailureLog &failures)
{
    const GeWaterVec3 black = {0.0f, 0.0f, 0.0f};
    const GeWaterVec3 shallow = geWaterCompositeApplyVisibilityFloor(
        black, 0.0f);
    const GeWaterVec3 deep = geWaterCompositeApplyVisibilityFloor(
        black, 100.0f);
    volatile float invalid_thickness = -1.0f;
    const GeWaterVec3 clamped = geWaterCompositeApplyVisibilityFloor(
        black, invalid_thickness);

    failures.expect(shallow.x >= GE_WATER_AMBIENT_SHALLOW_R &&
                       shallow.y >= GE_WATER_AMBIENT_SHALLOW_G &&
                       shallow.z >= GE_WATER_AMBIENT_SHALLOW_B,
                   "black scene water must retain a readable shallow ambient floor");
    failures.expect(luminance(shallow) >= GE_WATER_MIN_LINEAR_LUMINANCE,
                   "shallow water must not collapse to an unreadable black surface");
    failures.expect(vectorApproximately(clamped, shallow),
                   "negative water thickness must use the shallow ambient floor");
    failures.expect(deep.y > shallow.y && deep.z > shallow.z,
                   "deeper water must gain blue ambient energy instead of only becoming blacker");

    const GeWaterVec3 bright = {0.65f, 0.42f, 0.55f};
    const GeWaterVec3 preserved = geWaterCompositeApplyVisibilityFloor(
        bright, 0.0f);
    failures.expect(vectorApproximately(preserved, bright),
                   "visibility floor must not dim already bright reflection or transmission");

    const GeWaterVec3 malformed = {__builtin_nanf(""), 0.0f, 0.0f};
    const GeWaterVec3 recovered = geWaterCompositeApplyVisibilityFloor(
        malformed, 0.0f);
    failures.expect(geWaterCompositeVectorIsFinite(recovered) &&
                       luminance(recovered) >= GE_WATER_MIN_LINEAR_LUMINANCE,
                   "non-finite water colour must recover to the same readable floor");
}

static void test_render_tiers(FailureLog &failures)
{
    failures.expect(geWaterCompositeTier(-100) == GE_WATER_COMPOSITE_DISABLED,
                    "negative render mode must disable the water composite");
    failures.expect(geWaterCompositeTier(0) == GE_WATER_COMPOSITE_DISABLED,
                    "original render mode must keep the legacy water path");
    failures.expect(geWaterCompositeTier(1) == GE_WATER_COMPOSITE_ENHANCED,
                    "enhanced render mode must select tier one");
    failures.expect(geWaterCompositeTier(2) == GE_WATER_COMPOSITE_REMASTER,
                    "remaster render mode must select tier two");
    failures.expect(geWaterCompositeTier(99) == GE_WATER_COMPOSITE_REMASTER,
                    "future higher render modes must remain remaster compatible");

    failures.expect(!geWaterCompositeCanRun(0, 1, 1, 1),
                    "original mode must not run a post-world water composite");
    failures.expect(!geWaterCompositeCanRun(1, 0, 1, 1),
                    "missing scene color must fail closed");
    failures.expect(!geWaterCompositeCanRun(1, 1, 0, 1),
                    "missing scene depth must fail closed");
    failures.expect(!geWaterCompositeCanRun(1, 1, 1, 0),
                    "an empty water capture must not run a composite");
    failures.expect(geWaterCompositeCanRun(1, 1, 1, 1),
                    "enhanced mode must run with all required inputs");
    failures.expect(geWaterCompositeCanRun(2, 1, 1, 1),
                    "remaster mode must run with all required inputs");
    failures.expect(!geWaterCompositeCanRunWithDepthFallback(0, 1, 1),
                    "original mode must not use the optional-depth fallback");
    failures.expect(!geWaterCompositeCanRunWithDepthFallback(1, 0, 1),
                    "optional-depth fallback must still require scene colour");
    failures.expect(!geWaterCompositeCanRunWithDepthFallback(1, 1, 0),
                    "optional-depth fallback must still require captured geometry");
    failures.expect(geWaterCompositeCanRunWithDepthFallback(1, 1, 1),
                    "color-only framebuffer must remain eligible for the tested depth fallback");

    failures.expect(approximately(geWaterCompositeTierScale(0), 0.0f),
                    "original mode must have no composite strength");
    failures.expect(approximately(geWaterCompositeTierScale(1), 0.65f),
                    "enhanced mode must use the conservative composite strength");
    failures.expect(approximately(geWaterCompositeTierScale(2), 1.0f),
                    "remaster mode must use full composite strength");
    failures.expect(approximately(geWaterCompositeWaveScale(1), 0.004f),
                    "enhanced mode must use the lower wave distortion scale");
    failures.expect(approximately(geWaterCompositeWaveScale(2), 0.008f),
                    "remaster mode must use the higher wave distortion scale");
    failures.expect(approximately(geWaterCompositeContactBand(1),
                                  GE_WATER_CONTACT_BAND_ENHANCED),
                    "enhanced mode must use its own contact depth band");
    failures.expect(approximately(geWaterCompositeContactBand(2),
                                  GE_WATER_CONTACT_BAND_REMASTER),
                    "remaster mode must use its own contact depth band");
    failures.expect(approximately(geWaterCompositeContactBand(0), 0.0f),
                    "original mode must not have a contact band");

    for (int mode = -10; mode <= 10; ++mode) {
        const int tier = geWaterCompositeTier(mode);
        const float strength = geWaterCompositeTierScale(mode);
        const float wave_bound = geWaterCompositeWaveBound(mode);
        char message[128];
        std::snprintf(message, sizeof(message),
                      "render mode %d must produce a bounded composite tier", mode);
        failures.expect(tier >= GE_WATER_COMPOSITE_DISABLED &&
                            tier <= GE_WATER_COMPOSITE_REMASTER,
                        message);
        std::snprintf(message, sizeof(message),
                      "render mode %d must produce a bounded reflection scale", mode);
        failures.expect(strength >= 0.0f && strength <= 1.0f, message);
        std::snprintf(message, sizeof(message),
                      "render mode %d must produce a non-negative wave budget", mode);
        failures.expect(wave_bound >= 0.0f, message);
        if (mode <= 0) {
            std::snprintf(message, sizeof(message),
                          "original-compatible mode %d must disable composite math", mode);
            failures.expect(tier == GE_WATER_COMPOSITE_DISABLED &&
                                approximately(strength, 0.0f) &&
                                approximately(wave_bound, 0.0f), message);
        }
    }
}

static void test_scalar_and_projective_bounds(FailureLog &failures)
{
    failures.expect(approximately(geWaterClamp01(-1.0f), 0.0f),
                    "negative normalized values must clamp to zero");
    failures.expect(approximately(geWaterClamp01(2.0f), 1.0f),
                    "values above one must clamp to one");
    failures.expect(approximately(geWaterClamp01(__builtin_nanf("")), 0.0f),
                    "NaN normalized values must fail closed");
    failures.expect(approximately(geWaterClamp01(__builtin_inff()), 0.0f),
                    "infinite normalized values must fail closed");
    failures.expect(approximately(geWaterClampSigned(__builtin_nanf("")), 0.0f),
                    "non-finite signed values must fail closed");

    failures.expect(approximately(geWaterCompositeSampleUv(0.0f),
                                  GE_WATER_UV_MARGIN),
                    "projective samples must not address the texture border");
    failures.expect(approximately(geWaterCompositeSampleUv(1.0f),
                                  1.0f - GE_WATER_UV_MARGIN),
                    "projective samples must clamp the upper texture border");
    failures.expect(approximately(geWaterCompositeSampleUv(0.5f), 0.5f),
                    "interior projective samples must be unchanged");
    failures.expect(approximately(geWaterCompositeSampleUv(__builtin_nanf("")), 0.5f),
                    "invalid projective samples must fall back to the texture centre");

    failures.expect(approximately(geWaterCompositeClipToNdc(1.0f, 2.0f), 0.5f),
                    "homogeneous clip coordinates must divide by w");
    failures.expect(approximately(
                        geWaterCompositeClipToNdc(__builtin_nanf(""), 1.0f), 0.0f),
                    "invalid clip coordinates must fail closed");
    failures.expect(approximately(
                        geWaterCompositeClipToNdc(1.0f, 0.0f), 0.0f),
                    "zero-w clip coordinates must fail closed");
    failures.expect(approximately(
                        geWaterCompositeClipToNdc(3.4e38f, 1.0e-45f), 0.0f),
                    "overflowing clip division must fail closed");
    failures.expect(approximately(geWaterCompositeClipToUv(0.0f, 1.0f), 0.5f),
                    "the clip-space origin must map to the texture centre");
    failures.expect(approximately(geWaterCompositeClipToUv(2.0f, 1.0f),
                                  1.0f - GE_WATER_UV_MARGIN),
                    "projective UVs must clamp after perspective division");
    failures.expect(approximately(geWaterCompositeClipToUv(1.0f, 0.0f), 0.5f),
                    "zero-w projective coordinates must fail closed");
    failures.expect(approximately(geWaterCompositeClipToUv(1.0f, 1.0e-45f), 0.5f),
                    "overflowing perspective division must fail closed");

    for (int w_index = 1; w_index <= 8; ++w_index) {
        const float w = static_cast<float>(w_index) * 0.25f;
        for (int ndc_index = -8; ndc_index <= 8; ++ndc_index) {
            const float ndc = static_cast<float>(ndc_index) * 0.25f;
            const float uv = geWaterCompositeClipToUv(ndc * w, w);
            char message[128];
            std::snprintf(message, sizeof(message),
                          "perspective UV must remain bounded at w=%d/4 ndc=%d/4",
                          w_index, ndc_index);
            failures.expect(uv >= GE_WATER_UV_MARGIN &&
                                uv <= 1.0f - GE_WATER_UV_MARGIN,
                            message);
        }
    }
}

static void test_safe_wave_direction(FailureLog &failures)
{
    const GeWaterVec2 unit_x = geWaterCompositeSafeNormalize2({1.0f, 0.0f});
    failures.expect(approximately(unit_x.x, 1.0f) &&
                        approximately(unit_x.y, 0.0f),
                    "unit wave direction must remain unchanged");

    const GeWaterVec2 diagonal = geWaterCompositeSafeNormalize2({3.0f, 4.0f});
    failures.expect(approximately(diagonal.x, 0.6f) &&
                        approximately(diagonal.y, 0.8f),
                    "wave direction must be normalized with finite length");

    volatile float y_dominant_x = 0.5f;
    volatile float y_dominant_y = 3.0f;
    const GeWaterVec2 y_dominant = geWaterCompositeSafeNormalize2(
        {y_dominant_x, y_dominant_y});
    failures.expect(approximately(y_dominant.x, 0.16439899f) &&
                        approximately(y_dominant.y, 0.9863939f),
                    "Y-dominant wave direction must scale by its largest component");

    const GeWaterVec2 zero = geWaterCompositeSafeNormalize2({0.0f, 0.0f});
    failures.expect(approximately(zero.x, 1.0f) &&
                        approximately(zero.y, 0.0f),
                    "zero wave direction must use the deterministic fallback");

    volatile float tiny_component = 1.0e-12f;
    const GeWaterVec2 tiny = geWaterCompositeSafeNormalize2(
        {tiny_component, tiny_component});
    failures.expect(approximately(tiny.x, 1.0f) &&
                        approximately(tiny.y, 0.0f),
                    "finite sub-epsilon wave direction must fail closed");

    volatile float oversized_component = 2000000.0f;
    const GeWaterVec2 oversized = geWaterCompositeSafeNormalize2(
        {1.0f, oversized_component});
    failures.expect(approximately(oversized.x, 1.0f) &&
                        approximately(oversized.y, 0.0f),
                    "oversized wave direction must fail closed to +X");

    const GeWaterVec2 malformed[] = {
        {__builtin_nanf(""), 0.0f},
        {0.0f, __builtin_inff()},
        {1000000.0f, 0.0f},
        {-1000000.0f, 0.0f},
    };
    for (GeWaterVec2 value : malformed) {
        const GeWaterVec2 safe = geWaterCompositeSafeNormalize2(value);
        failures.expect(approximately(safe.x, 1.0f) &&
                            approximately(safe.y, 0.0f),
                        "malformed wave direction must fail closed to +X");
    }
}

static void test_wave_octave_bounds(FailureLog &failures)
{
    failures.expect(approximately(geWaterCompositeWaveBound(0), 0.0f),
                    "original mode must have no displacement budget");
    failures.expect(approximately(geWaterCompositeWaveBound(1), 0.008f),
                    "enhanced mode must bound two wave components");
    failures.expect(approximately(geWaterCompositeWaveBound(2), 0.016f),
                    "remaster mode must bound two wave components");

    failures.expect(approximately(
                        geWaterCompositeWaveOffset(-1.0f, -1.0f, 0.004f),
                        -0.008f),
                    "negative wave extrema must remain inside the enhanced bound");
    failures.expect(approximately(
                        geWaterCompositeWaveOffset(1.0f, 1.0f, 0.008f),
                        0.016f),
                    "positive wave extrema must remain inside the remaster bound");
    failures.expect(approximately(
                        geWaterCompositeWaveOffset(-2.0f, 2.0f, 0.004f),
                        0.0f),
                    "wave components must clamp before they are summed");
    failures.expect(approximately(
                        geWaterCompositeWaveOffset(__builtin_nanf(""), 1.0f, 0.004f),
                        0.0f),
                    "invalid wave components must fail closed");
    failures.expect(approximately(
                        geWaterCompositeWaveOffset(1.0f, 1.0f, -1.0f),
                        0.0f),
                    "negative wave scales must fail closed");
    failures.expect(approximately(
                        geWaterCompositeWaveOffset(1.0f, 1.0f, 3.4e38f),
                        0.0f),
                    "overflowing wave offsets must fail closed");

    for (int i = -20; i <= 20; ++i) {
        const float component = static_cast<float>(i) / 10.0f;
        const float offset = geWaterCompositeWaveOffset(component, -component, 0.008f);
        char message[128];
        std::snprintf(message, sizeof(message),
                      "wave octave sum must stay bounded at sample %d", i);
        failures.expect(offset <= geWaterCompositeWaveBound(2) + 0.0001f &&
                            offset >= -geWaterCompositeWaveBound(2) - 0.0001f,
                        message);
    }
}

static void test_depth_classification(FailureLog &failures)
{
    failures.expect(!geWaterCompositeDepthIsOccluded(0.5f, 0.5f, 0.001f),
                    "equal scene and surface depth is not occluded");
    failures.expect(!geWaterCompositeDepthIsOccluded(0.5f, 0.4995f, 0.001f),
                    "epsilon must suppress depth noise at the water surface");
    failures.expect(geWaterCompositeDepthIsOccluded(0.5f, 0.498f, 0.001f),
                    "near ship/dam geometry must classify as occluding water");
    failures.expect(!geWaterCompositeDepthIsOccluded(0.5f, 0.75f, 0.001f),
                    "geometry behind the water must not occlude it");
    failures.expect(!geWaterCompositeDepthIsOccluded(__builtin_nanf(""), 0.2f, 0.001f),
                    "non-finite surface depth must fail closed");
    failures.expect(!geWaterCompositeDepthIsOccluded(0.5f, __builtin_inff(), 0.001f),
                    "non-finite scene depth must fail closed");
    failures.expect(!geWaterCompositeDepthIsOccluded(0.5f, 0.2f, -1.0f),
                    "negative depth epsilon must fail closed");
    failures.expect(!geWaterCompositeDepthIsOccluded(0.5f, 0.2f, __builtin_nanf("")),
                    "non-finite depth epsilon must fail closed");

    const float surface = 0.5f;
    for (int i = -5; i <= 5; ++i) {
        const float scene = surface - static_cast<float>(i) * 0.001f;
        const bool expected = i > 1;
        const bool actual = geWaterCompositeDepthIsOccluded(
            surface, scene, GE_WATER_DEPTH_EPSILON) != 0;
        char message[128];
        std::snprintf(message, sizeof(message),
                      "depth occlusion epsilon boundary must be stable at sample %d", i);
        failures.expect(actual == expected, message);
    }
}

static void test_surface_depth_and_thickness(FailureLog &failures)
{
    /* GPU Gems' refraction pass measures the distance from the water surface
     * to the first opaque sample behind it.  The surface depth must therefore
     * remain the water depth when the scene sample is farther away. */
    failures.expect(approximately(
                        geWaterCompositeSurfaceDepth(2.0f, 10.0f), 2.0f),
                    "water surface depth must not be replaced by farther scene depth");
    failures.expect(approximately(
                        geWaterCompositeWaterThickness(2.0f, 10.0f), 8.0f),
                    "water thickness must measure background depth from the surface");
    failures.expect(approximately(
                        geWaterCompositeWaterThickness(10.0f, 2.0f), 0.0f),
                    "water thickness must clamp when geometry is in front of the surface");
    failures.expect(approximately(
                        geWaterCompositeWaterThickness(2.0f,
                                                       __builtin_nanf("")),
                        0.0f),
                    "invalid scene depth must fail closed before thickness math");
    failures.expect(approximately(
                        geWaterCompositeSurfaceDepth(__builtin_nanf(""), 10.0f),
                        10.0f),
                    "invalid surface depth must use a finite scene-depth fallback");
    failures.expect(approximately(
                        geWaterCompositeSurfaceDepth(__builtin_nanf(""),
                                                     __builtin_nanf("")),
                        0.0f),
                    "invalid surface and scene depths must fail closed");
}

static void test_contact_foam_boundaries(FailureLog &failures)
{
    const float surface = 0.5f;
    const float band = 0.1f;

    failures.expect(approximately(
                        geWaterCompositeContactFoam(surface, 0.5f, 1.0f, band),
                        0.0f),
                    "foam must be absent when no geometry crosses the water plane");
    failures.expect(approximately(
                        geWaterCompositeContactFoam(surface, 0.4f, 0.0f, band),
                        0.0f),
                    "foam must be absent when the depth edge has no contrast");
    failures.expect(approximately(
                        geWaterCompositeContactFoam(surface, 0.4f, 1.0f, band),
                        0.0f),
                    "foam must end at the configured contact band");
    failures.expect(approximately(
                        geWaterCompositeContactFoam(surface, 0.49f, 1.0f, band),
                        0.9811f, 0.001f),
                    "foam must be strongest near the water/geometry contact");
    failures.expect(approximately(
                        geWaterCompositeContactFoam(surface, 0.45f, 1.0f, band),
                        0.5153f, 0.001f),
                    "mid-band ship/dam contact must follow the smooth shoreline response");
    failures.expect(approximately(
                        geWaterCompositeContactFoam(surface, 0.45f, 2.0f, band),
                        0.5153f, 0.001f),
                    "edge contrast must clamp rather than overbrighten foam");
    failures.expect(approximately(
                        geWaterCompositeContactFoam(surface, 0.45f, 1.0f, 0.0f),
                        0.0f),
                    "zero-width contact bands must fail closed");
    failures.expect(approximately(
                        geWaterCompositeContactFoam(__builtin_nanf(""), 0.45f, 1.0f, band),
                        0.0f),
                    "non-finite contact input must fail closed");

    float previous = geWaterCompositeContactFoam(surface, 0.49f, 1.0f, band);
    for (int i = 1; i <= 10; ++i) {
        const float scene = surface - band * static_cast<float>(i) / 10.0f;
        const float current = geWaterCompositeContactFoam(surface, scene, 1.0f, band);
        char message[128];
        std::snprintf(message, sizeof(message),
                      "foam strength must not increase away from a contact edge at sample %d", i);
        failures.expect(current <= previous + 0.0001f, message);
        previous = current;
    }
}

static void test_smooth_depth_response(FailureLog &failures)
{
    failures.expect(approximately(
                        geWaterCompositeSmoothStep(0.0f, 1.0f, 0.0f),
                        0.0f),
                    "smooth depth response must start at its lower threshold");
    failures.expect(approximately(
                        geWaterCompositeSmoothStep(0.0f, 1.0f, 1.0f),
                        1.0f),
                    "smooth depth response must end at its upper threshold");
    failures.expect(approximately(
                        geWaterCompositeSmoothStep(0.0f, 1.0f, 0.5f),
                        0.5f),
                    "smooth depth response must preserve its midpoint");
    failures.expect(approximately(
                        geWaterCompositeSmoothStep(0.0f, 1.0f, -1.0f),
                        0.0f),
                    "smooth depth response must clamp below its range");
    failures.expect(approximately(
                        geWaterCompositeSmoothStep(0.0f, 1.0f, 2.0f),
                        1.0f),
                    "smooth depth response must clamp above its range");
    failures.expect(approximately(
                        geWaterCompositeSmoothStep(1.0f, 0.0f, 0.5f),
                        0.0f),
                    "reversed smoothstep ranges must fail closed");
    failures.expect(approximately(
                        geWaterCompositeSmoothStep(__builtin_nanf(""), 1.0f, 0.5f),
                        0.0f),
                    "non-finite lower smoothstep bounds must fail closed");
    failures.expect(approximately(
                        geWaterCompositeSmoothStep(0.0f, __builtin_nanf(""), 0.5f),
                        0.0f),
                    "non-finite upper smoothstep bounds must fail closed");
    failures.expect(approximately(
                        geWaterCompositeSmoothStep(0.0f, 1.0f, __builtin_nanf("")),
                        0.0f),
                    "non-finite smoothstep input must fail closed");
    failures.expect(approximately(
                        geWaterCompositeSmoothStep(0.0f, 1.0f, __builtin_inff()),
                        1.0f),
                    "positive overflow must saturate a smooth depth response");
    failures.expect(approximately(
                        geWaterCompositeSmoothStep(0.0f, 1.0f, -__builtin_inff()),
                        0.0f),
                    "negative overflow must clamp a smooth depth response");

    const float band = GE_WATER_CONTACT_BAND_REMASTER;
    failures.expect(approximately(
                        geWaterCompositeContactFoam(0.5f,
                                                    0.5f - GE_WATER_CONTACT_START * 0.5f,
                                                    1.0f, band),
                        0.0f),
                    "contact foam must stay off below its noise threshold");
    failures.expect(geWaterCompositeContactFoam(
                        0.5f, 0.5f - (GE_WATER_CONTACT_START + 0.001f),
                        1.0f, band) > 0.0f,
                    "contact foam must turn on above its noise threshold");

    failures.expect(approximately(
                        geWaterCompositeDepthEdge(0.5f, 0.5f - GE_WATER_EDGE_START,
                                                   0.5f, 0.5f, 0.5f),
                        0.0f),
                    "sub-threshold depth noise must not create an edge");
    failures.expect(approximately(
                        geWaterCompositeDepthEdge(0.5f, 0.5f - GE_WATER_EDGE_END,
                                                   0.5f, 0.5f, 0.5f),
                        1.0f),
                    "large depth steps must saturate the edge response");

    float previous = 0.0f;
    for (int i = 0; i <= 100; ++i) {
        const float value = static_cast<float>(i) / 100.0f;
        const float current = geWaterCompositeSmoothStep(0.0f, 1.0f, value);
        char message[128];
        std::snprintf(message, sizeof(message),
                      "smooth depth response must be monotonic at sample %d", i);
        failures.expect(current + 0.0001f >= previous, message);
        previous = current;
    }
}

static void test_depth_edge_strength(FailureLog &failures)
{
    failures.expect(approximately(geWaterCompositeDepthEdge(0.5f, 0.5f, 0.5f, 0.5f, 0.5f), 0.0f),
                    "flat depth must have no contact edge");
    failures.expect(approximately(geWaterCompositeDepthEdge(0.5f, 0.5f, 0.5f, 0.5f, 0.5f), 0.0f),
                    "repeated flat depth must remain idempotent");
    failures.expect(geWaterCompositeDepthEdge(0.5f, 0.1f, 0.5f, 0.5f, 0.5f) > 0.0f,
                    "a vertical geometry depth step must expose an edge");
    failures.expect(geWaterCompositeDepthEdge(0.5f, 0.1f, 0.5f, 0.5f, 0.5f) <= 1.0f,
                    "depth edge strength must be normalized");
    failures.expect(approximately(geWaterCompositeDepthEdge(__builtin_nanf(""), 0.1f, 0.5f, 0.5f, 0.5f), 0.0f),
                    "invalid center depth must fail closed");
    failures.expect(approximately(geWaterCompositeDepthEdge(0.5f, __builtin_inff(), 0.5f, 0.5f, 0.5f), 0.0f),
                    "invalid neighbor depth must fail closed");

    const float weak = geWaterCompositeDepthEdge(0.5f, 0.49f, 0.5f, 0.5f, 0.5f);
    const float strong = geWaterCompositeDepthEdge(0.5f, 0.1f, 0.5f, 0.5f, 0.5f);
    failures.expect(strong >= weak,
                    "larger ship/dam depth discontinuities must not weaken the edge");
    failures.expect(geWaterCompositeDepthEdge(
                        0.5f, 0.5f - 0.003143f, 0.5f, 0.5f, 0.5f) > 0.0f,
                    "the measured boat/dam depth edge must produce visible contact foam");
}

static void test_fresnel(FailureLog &failures)
{
    const float f0 = GE_WATER_BASE_REFLECTANCE;
    volatile float non_finite = __builtin_nanf("");
    volatile float infinite = __builtin_inff();
    const float normal = geWaterCompositeFresnel(1.0f, f0);
    const float grazing = geWaterCompositeFresnel(0.0f, f0);

    failures.expect(approximately(normal, f0),
                    "normal incidence must retain the water base reflectance");
    failures.expect(grazing > normal,
                    "grazing incidence must increase reflection");
    failures.expect(geWaterCompositeFresnel(-1.0f, f0) <= 1.0f,
                    "negative cosine must remain bounded");
    failures.expect(geWaterCompositeFresnel(2.0f, f0) >= 0.0f,
                    "cosine above one must remain bounded");
    failures.expect(approximately(geWaterCompositeFresnel(0.5f, -1.0f), 0.0f),
                    "negative base reflectance must fail closed");
    failures.expect(approximately(geWaterCompositeFresnel(non_finite, f0), 0.0f),
                    "non-finite cosine must fail closed");
    failures.expect(approximately(geWaterCompositeFresnel(0.5f, infinite), 0.0f),
                    "non-finite base reflectance must fail closed");
    failures.expect(approximately(geWaterCompositeFresnel(0.5f, 2.0f), 1.0f),
                    "base reflectance above one must clamp");
    failures.expect(approximately(geWaterCompositeFresnel(1.0f, f0), f0),
                    "Schlick Fresnel must return f0 at normal incidence");
    failures.expect(approximately(geWaterCompositeFresnel(0.0f, f0), 1.0f),
                    "Schlick Fresnel must reach one at grazing incidence");
    failures.expect(approximately(geWaterCompositeFresnel(0.5f, f0), 0.050625f),
                    "Schlick Fresnel must match its fifth-power midpoint");
    failures.expect(approximately(geWaterCompositeReflectionWeight(0, 1.0f), 0.0f),
                    "original mode must not receive a reflection weight");
    failures.expect(approximately(geWaterCompositeReflectionWeight(1, 1.0f), 0.65f),
                    "enhanced reflection weight must respect its tier scale");
    failures.expect(approximately(geWaterCompositeReflectionWeight(2, 1.0f), 1.0f),
                    "remaster reflection weight must reach full Fresnel strength");
    failures.expect(approximately(geWaterCompositeReflectionWeight(2, __builtin_nanf("")), 0.0f),
                    "non-finite reflection weight must fail closed");

    float previous = 1.0f;
    for (int i = 0; i <= 100; ++i) {
        const float cosine = static_cast<float>(i) / 100.0f;
        const float current = geWaterCompositeFresnel(cosine, f0);
        char message[128];
        std::snprintf(message, sizeof(message),
                      "Fresnel must not increase toward normal incidence at sample %d", i);
        failures.expect(current <= previous + 0.0001f, message);
        previous = current;
    }
}

static void test_reflection_coordinates(FailureLog &failures)
{
    failures.expect(approximately(geWaterCompositeReflectV(0.25f, 0.75f),
                                  1.0f - GE_WATER_UV_MARGIN),
                    "reflection must mirror a sample across the water line");
    failures.expect(approximately(geWaterCompositeReflectV(0.75f, 0.75f), 0.75f),
                    "a sample on the water line must remain on the line");
    failures.expect(approximately(geWaterCompositeReflectV(0.9f, 0.2f),
                                  GE_WATER_UV_MARGIN),
                    "reflected coordinates must clamp at the lower texture edge");
    failures.expect(approximately(geWaterCompositeReflectV(0.1f, 0.2f), 0.3f),
                    "reflected coordinates must preserve the mirror distance");
    failures.expect(approximately(
                        geWaterCompositeReflectV(
                            geWaterCompositeReflectV(0.3f, 0.5f), 0.5f),
                        0.3f),
                    "interior reflection coordinates must be an involution");
    failures.expect(approximately(geWaterCompositeReflectV(__builtin_nanf(""), 0.2f), 0.0f),
                    "invalid reflection coordinates must fail closed");

    for (int line_index = 1; line_index <= 9; ++line_index) {
        const float line = static_cast<float>(line_index) / 10.0f;
        for (int sample_index = 1; sample_index <= 9; ++sample_index) {
            const float sample = static_cast<float>(sample_index) / 10.0f;
            const float reflected = geWaterCompositeReflectV(sample, line);
            char message[128];
            std::snprintf(message, sizeof(message),
                          "reflection sample must remain addressable at line %d/sample %d",
                          line_index, sample_index);
            failures.expect(reflected >= GE_WATER_UV_MARGIN &&
                                reflected <= 1.0f - GE_WATER_UV_MARGIN,
                            message);
        }
    }
}

static void test_reflection_and_refraction_vectors(FailureLog &failures)
{
    const GeWaterVec3 up = {0.0f, 1.0f, 0.0f};
    const GeWaterVec3 down = {0.0f, -1.0f, 0.0f};
    const GeWaterVec3 tangent = {1.0f, 0.0f, 0.0f};

    const GeWaterVec2 incident_screen = geWaterCompositeProjectRayToScreen(
        {0.3f, -0.2f, -1.0f});
    failures.expect(approximately(incident_screen.x, 0.3f) &&
                        approximately(incident_screen.y, -0.2f),
                    "a unit-depth camera ray must project to its screen xy");
    const GeWaterVec2 deeper_screen = geWaterCompositeProjectRayToScreen(
        {0.6f, -0.4f, -2.0f});
    failures.expect(approximately(deeper_screen.x, 0.3f) &&
                        approximately(deeper_screen.y, -0.2f),
                    "perspective ray projection must divide screen xy by depth");
    const GeWaterVec2 bent_offset = geWaterCompositeRayScreenOffset(
        {0.3f, -0.2f, -1.0f}, {0.6f, -0.4f, -0.75f});
    failures.expect(approximately(bent_offset.x, 0.5f) &&
                        approximately(bent_offset.y, -0.33333334f),
                    "ray screen offsets must use projected xy rather than depth as uv");
    const GeWaterVec2 unchanged_offset = geWaterCompositeRayScreenOffset(
        {0.3f, -0.2f, -1.0f}, {0.3f, -0.2f, -1.0f});
    failures.expect(approximately(unchanged_offset.x, 0.0f) &&
                        approximately(unchanged_offset.y, 0.0f),
                    "an unchanged camera ray must produce no screen offset");
    const GeWaterVec2 invalid_depth = geWaterCompositeProjectRayToScreen(
        {0.3f, -0.2f, 0.0f});
    failures.expect(approximately(invalid_depth.x, 0.0f) &&
                        approximately(invalid_depth.y, 0.0f),
                    "rays pointing at or behind the camera must fail closed");
    const GeWaterVec2 invalid_offset = geWaterCompositeRayScreenOffset(
        {0.3f, -0.2f, -1.0f}, {__builtin_nanf(""), 0.0f, -1.0f});
    failures.expect(approximately(invalid_offset.x, 0.0f) &&
                        approximately(invalid_offset.y, 0.0f),
                    "non-finite bent rays must fail closed");
    const GeWaterVec2 overflow_offset = geWaterCompositeRayScreenOffset(
        {3.4e38f, 0.0f, -1.0f}, {-3.4e38f, 0.0f, -1.0f});
    failures.expect(approximately(overflow_offset.x, 0.0f) &&
                        approximately(overflow_offset.y, 0.0f),
                    "overflowing projected offsets must fail closed");

    failures.expect(approximately(
                        geWaterCompositeVectorLength({3.0f, 4.0f, 0.0f}), 5.0f),
                    "vector length must use the Euclidean norm");
    failures.expect(approximately(
                        geWaterCompositeVectorLength({3.0e38f, 3.0e38f, 0.0f}), 0.0f),
                    "overflowing vector lengths must fail closed");
    failures.expect(approximately(
                        geWaterCompositeVectorDot({1.0f, 2.0f, 3.0f},
                                                   {-2.0f, 1.0f, 0.5f}),
                        1.5f),
                    "vector dot products must be component-wise");

    failures.expect(approximately(
                        geWaterCompositeVectorDot(
                            {__builtin_nanf(""), 0.0f, 0.0f}, up),
                        0.0f),
                    "invalid left vector dots must fail closed");
    failures.expect(approximately(
                        geWaterCompositeVectorDot(
                            up, {__builtin_nanf(""), 0.0f, 0.0f}),
                        0.0f),
                    "invalid right vector dots must fail closed");
    failures.expect(approximately(
                        geWaterCompositeVectorDot(
                            {3.4e38f, 0.0f, 0.0f},
                            {3.4e38f, 0.0f, 0.0f}),
                        0.0f),
                    "overflowing vector dots must fail closed");
    failures.expect(approximately(
                        geWaterCompositeVectorLength({0.0f, 0.0f, 2.0f}),
                        2.0f),
                    "vector length must account for a dominant z component");
    failures.expect(approximately(
                        geWaterCompositeVectorLength(
                            {3.4e38f, 3.4e38f, 3.4e38f}),
                        0.0f),
                    "overflowing vector lengths must fail closed");

    failures.expect(vectorApproximately(
                        geWaterCompositeReflectVector(down, up),
                        up),
                    "normal-incidence reflection must reverse its vertical direction");
    failures.expect(vectorApproximately(
                        geWaterCompositeReflectVector(tangent, up),
                        tangent),
                    "a tangent ray must reflect without changing direction");
    failures.expect(vectorApproximately(
                        geWaterCompositeReflectVector({0.6f, -0.8f, 0.0f}, up),
                        {0.6f, 0.8f, 0.0f}),
                    "oblique reflection must mirror only the normal component");
    failures.expect(vectorApproximately(
                        geWaterCompositeReflectVector(down, {0.0f, 2.0f, 0.0f}),
                        up),
                    "reflection must normalize a non-unit surface normal");
    failures.expect(approximately(
                        geWaterCompositeVectorLength(
                            geWaterCompositeReflectVector({0.6f, -0.8f, 0.0f}, up)),
                        1.0f),
                    "reflection must preserve a normalized ray length");
    const GeWaterVec3 invalid_reflection = geWaterCompositeReflectVector(
        {__builtin_nanf(""), 0.0f, 0.0f}, up);
    failures.expect(approximately(
                        geWaterCompositeVectorLength(invalid_reflection), 0.0f),
                    "invalid incident vectors must fail closed to zero");

    const GeWaterVec3 normal_refracted = geWaterCompositeRefractVector(
        down, up, GE_WATER_REFRACTION_AIR_TO_WATER);
    failures.expect(vectorApproximately(normal_refracted, down),
                    "normal-incidence refraction must preserve its direction");
    failures.expect(approximately(
                        geWaterCompositeVectorLength(normal_refracted), 1.0f),
                    "refracted directions must remain normalized");

    const GeWaterVec3 oblique_refracted = geWaterCompositeRefractVector(
        {0.6f, -0.8f, 0.0f}, up, GE_WATER_REFRACTION_AIR_TO_WATER);
    failures.expect(oblique_refracted.x < 0.6f && oblique_refracted.x > 0.0f,
                    "air-to-water refraction must bend an oblique ray toward the normal");
    failures.expect(oblique_refracted.y < -0.8f,
                    "air-to-water refraction must increase the normal component");
    failures.expect(approximately(
                        geWaterCompositeVectorLength(oblique_refracted), 1.0f),
                    "oblique refracted directions must remain normalized");
    failures.expect(vectorApproximately(
                        oblique_refracted,
                        {0.45f, -0.89302856f, 0.0f}),
                    "air-to-water refraction must match Snell's vector equation");
    failures.expect(vectorApproximately(
                        geWaterCompositeRefractVector({0.6f, -0.8f, 0.0f}, up, 1.0f),
                        {0.6f, -0.8f, 0.0f}),
                    "equal refractive indices must leave the ray unchanged");

    const GeWaterVec3 flipped_normal_refracted = geWaterCompositeRefractVector(
        up, up, GE_WATER_REFRACTION_AIR_TO_WATER);
    failures.expect(approximately(
                        geWaterCompositeVectorLength(flipped_normal_refracted),
                        1.0f),
                    "refraction must handle an incident ray aligned with the normal");

    const GeWaterVec3 total_internal_reflection = geWaterCompositeRefractVector(
        {0.9f, 0.4358899f, 0.0f}, {0.0f, -1.0f, 0.0f},
        GE_WATER_REFRACTION_WATER_TO_AIR);
    failures.expect(approximately(
                        geWaterCompositeVectorLength(total_internal_reflection), 0.0f),
                    "water-to-air total internal reflection must fail closed to zero");
    failures.expect(approximately(
                        geWaterCompositeVectorLength(
                            geWaterCompositeRefractVector(down, up, 0.0f)), 0.0f),
                    "zero refractive indices must fail closed");
    failures.expect(approximately(
                        geWaterCompositeVectorLength(
                            geWaterCompositeRefractVector(down, up, __builtin_inff())), 0.0f),
                    "non-finite refractive indices must fail closed");
    failures.expect(approximately(
                        geWaterCompositeVectorLength(
                            geWaterCompositeRefractVector({0.0f, 0.0f, 0.0f}, up, 0.75f)),
                        0.0f),
                    "zero incident rays must fail closed");

    failures.expect(geWaterCompositeVectorIsFinite(
                        {0.0f, __builtin_nanf(""), 0.0f}) == 0,
                    "non-finite y components must be rejected");
    failures.expect(geWaterCompositeVectorIsFinite(
                        {0.0f, 0.0f, __builtin_inff()}) == 0,
                    "non-finite z components must be rejected");
}

static void test_capture_geometry(FailureLog &failures)
{
    failures.expect(geWaterCompositeVertexIsValid(0.0f, 0.0f, 0.5f, 1.0f),
                    "ordinary projected water vertices must be capturable");
    failures.expect(!geWaterCompositeVertexIsValid(0.0f, 0.0f, 0.5f, 0.0f),
                    "zero-w vertices must not reach the composite pass");
    failures.expect(!geWaterCompositeVertexIsValid(0.0f, 0.0f, 0.5f, -1.0f),
                    "behind-camera vertices must not reach the composite pass");
    failures.expect(!geWaterCompositeVertexIsValid(__builtin_nanf(""), 0.0f, 0.5f, 1.0f),
                    "non-finite projected vertices must be rejected");
    failures.expect(!geWaterCompositeVertexIsValid(1.0f, 0.0f, 0.5f, 1.0e-45f),
                    "overflowing homogeneous division must be rejected");
    failures.expect(geWaterCompositeTriangleArea(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f) > 0.0f,
                    "a real water triangle must have positive projected area");
    failures.expect(approximately(
                        geWaterCompositeTriangleArea(0.0f, 0.0f, 1.0f, 0.0f, 2.0f, 0.0f),
                        0.0f),
                    "collinear capture vertices must be rejected as degenerate");
    failures.expect(approximately(
                        geWaterCompositeTriangleArea(__builtin_nanf(""), 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
                        0.0f),
                    "non-finite triangle area must fail closed");
    failures.expect(approximately(
                        geWaterCompositeTriangleArea(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f),
                        geWaterCompositeTriangleArea(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f)),
                    "triangle area must be invariant under winding reversal");
    failures.expect(approximately(
                        geWaterCompositeTriangleArea(0.0f, 0.0f,
                                                      3.0e38f, 0.0f,
                                                      0.0f, 3.0e38f),
                        0.0f),
                    "overflowing projected area must fail closed");
}

static void test_phase_order(FailureLog &failures)
{
    failures.expect(geWaterCompositePhaseCanAdvance(
                        GE_WATER_PHASE_PREWORLD, GE_WATER_PHASE_WORLD),
                    "water capture must advance from pre-world to world");
    failures.expect(geWaterCompositePhaseCanAdvance(
                        GE_WATER_PHASE_WORLD, GE_WATER_PHASE_COMPOSITE),
                    "water composite must run after opaque world geometry");
    failures.expect(geWaterCompositePhaseCanAdvance(
                        GE_WATER_PHASE_COMPOSITE, GE_WATER_PHASE_HUD),
                    "HUD must run after water composite");
    failures.expect(!geWaterCompositePhaseCanAdvance(
                        GE_WATER_PHASE_PREWORLD, GE_WATER_PHASE_HUD),
                    "HUD must not bypass the post-world composite boundary");
    failures.expect(!geWaterCompositePhaseCanAdvance(
                        GE_WATER_PHASE_COMPOSITE, GE_WATER_PHASE_WORLD),
                    "the composite phase must not run a second world pass");
    failures.expect(!geWaterCompositePhaseCanAdvance(-1, GE_WATER_PHASE_WORLD),
                    "invalid current phases must fail closed");
    failures.expect(!geWaterCompositePhaseCanAdvance(
                        GE_WATER_PHASE_HUD, GE_WATER_PHASE_COMPOSITE),
                    "a late composite must not paint over the HUD");
}

static void test_reflection_atlas_policy(FailureLog &failures)
{
    GeWaterReflectionAtlasRegion first = {};
    failures.expect(geWaterReflectionAtlasRegion(0, 1, &first) != 0,
                    "the first water surface must receive a reflection atlas region");
    failures.expect(approximately(first.scale_x, 0.25f) &&
                        approximately(first.scale_y, 0.5f) &&
                        approximately(first.bias_x, 0.0f) &&
                        approximately(first.bias_y, 0.0f),
                    "surface zero must map to the lower-left reflection tile");

    GeWaterReflectionAtlasRegion fourth = {};
    failures.expect(geWaterReflectionAtlasRegion(3, 8, &fourth) != 0,
                    "each supported surface must receive an atlas region");
    failures.expect(approximately(fourth.bias_x, 0.75f) &&
                        approximately(fourth.bias_y, 0.0f),
                    "reflection atlas columns must preserve surface-local UVs");

    GeWaterReflectionAtlasRegion fifth = {};
    failures.expect(geWaterReflectionAtlasRegion(4, 8, &fifth) != 0,
                    "the second reflection row must be addressable");
    failures.expect(approximately(fifth.bias_x, 0.0f) &&
                        approximately(fifth.bias_y, 0.5f),
                    "surface four must map to the second reflection row");

    GeWaterReflectionAtlasRegion invalid = {};
    failures.expect(geWaterReflectionAtlasRegion(8, 8, &invalid) == 0,
                    "surface indices outside the bounded atlas must fail closed");
    failures.expect(geWaterReflectionAtlasRegion(0, 0, &invalid) == 0,
                    "an empty surface batch must not produce a reflection region");
    failures.expect(geWaterReflectionAtlasRegion(0, 1, nullptr) == 0,
                    "a missing reflection region output must fail closed");
}

int main()
{
    FailureLog failures;
    test_display_transfer(failures);
    test_feature_switches(failures);
    test_low_light_water_visibility(failures);
    test_render_tiers(failures);
    test_scalar_and_projective_bounds(failures);
    test_safe_wave_direction(failures);
    test_wave_octave_bounds(failures);
    test_depth_classification(failures);
    test_surface_depth_and_thickness(failures);
    test_contact_foam_boundaries(failures);
    test_smooth_depth_response(failures);
    test_depth_edge_strength(failures);
    test_fresnel(failures);
    test_reflection_coordinates(failures);
    test_reflection_and_refraction_vectors(failures);
    test_capture_geometry(failures);
    test_phase_order(failures);
    test_reflection_atlas_policy(failures);

    if (failures.count == 0) {
        std::fprintf(stderr, "water-render policy suite passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d water-render contract(s) are red\n", failures.count);
    return 1;
}
