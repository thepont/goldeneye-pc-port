/*
 * Independent property checks for the backend-neutral water math.
 *
 * These tests deliberately use small reference calculations instead of
 * copying the implementation's branch structure.  They exercise the public
 * water policy boundary over many deterministic inputs and protect the
 * invariants that the future reflected-camera/height-field path must retain.
 */
#include "gfx_water_composite_policy.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

struct FailureLog {
    int count = 0;

    void expect(bool condition, const char *message)
    {
        if (!condition) {
            ++count;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }
};

static bool approximately(float actual, float expected, float epsilon = 0.0005f)
{
    return std::fabs(actual - expected) <= epsilon;
}

static GeWaterVec3 normalize_reference(GeWaterVec3 value)
{
    const float length = std::sqrt(value.x * value.x + value.y * value.y +
                                    value.z * value.z);
    if (!std::isfinite(length) || length <= 0.000001f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {value.x / length, value.y / length, value.z / length};
}

static float dot_reference(GeWaterVec3 left, GeWaterVec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

static GeWaterVec3 reflect_reference(GeWaterVec3 incident,
                                     GeWaterVec3 normal)
{
    incident = normalize_reference(incident);
    normal = normalize_reference(normal);
    const float scale = 2.0f * dot_reference(normal, incident);
    return normalize_reference({
        incident.x - scale * normal.x,
        incident.y - scale * normal.y,
        incident.z - scale * normal.z,
    });
}

static void test_fresnel_reference(FailureLog &failures)
{
    const float reflectances[] = {0.0f, 0.02f, 0.25f, 0.8f, 1.0f};
    for (float base_reflectance : reflectances) {
        float previous = 1.0f;
        for (int index = 0; index <= 100; ++index) {
            const float cosine = static_cast<float>(index) / 100.0f;
            const float one_minus_cosine = 1.0f - cosine;
            const float expected = base_reflectance +
                (1.0f - base_reflectance) *
                    std::pow(one_minus_cosine, 5.0f);
            const float actual = geWaterCompositeFresnel(
                cosine, base_reflectance);
            failures.expect(approximately(actual, expected),
                            "Fresnel must match the independent fifth-power reference");
            failures.expect(actual <= previous + 0.0001f,
                            "Fresnel must be monotonic toward grazing incidence");
            previous = actual;
        }
    }
}

static void test_reflection_properties(FailureLog &failures)
{
    const GeWaterVec3 normals[] = {
        {0.0f, 1.0f, 0.0f},
        {0.6f, 0.8f, 0.0f},
        {-0.25f, 0.9f, 0.35f},
    };
    const GeWaterVec3 incidents[] = {
        {-0.8f, -0.6f, 0.0f},
        {0.2f, -0.7f, -0.4f},
        {0.9f, -0.2f, 0.3f},
        {-0.15f, 0.4f, -0.8f},
    };

    for (GeWaterVec3 normal : normals) {
        for (GeWaterVec3 incident : incidents) {
            const GeWaterVec3 expected = reflect_reference(incident, normal);
            const GeWaterVec3 actual = geWaterCompositeReflectVector(
                incident, normal);
            failures.expect(approximately(actual.x, expected.x) &&
                                approximately(actual.y, expected.y) &&
                                approximately(actual.z, expected.z),
                            "reflection must match the normalized plane reference");
            failures.expect(approximately(
                                geWaterCompositeVectorLength(actual), 1.0f),
                            "valid reflection rays must remain normalized");

            const GeWaterVec3 n = normalize_reference(normal);
            const GeWaterVec3 i = normalize_reference(incident);
            const float normal_component_sum =
                dot_reference(actual, n) + dot_reference(i, n);
            failures.expect(std::fabs(normal_component_sum) <= 0.001f,
                            "reflection must reverse the normal component");
        }
    }
}

static void test_planar_reflection_properties(FailureLog &failures)
{
    const float horizontal_plane[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    const GeWaterVec3 above = {1.5f, 3.0f, -2.0f};
    const GeWaterVec3 reflected = geWaterCompositeReflectPointAcrossPlane(
        above, horizontal_plane);
    failures.expect(approximately(reflected.x, above.x) &&
                        approximately(reflected.y, -above.y) &&
                        approximately(reflected.z, above.z),
                    "planar reflection must mirror a point across the water plane");

    const GeWaterVec3 on_plane = {4.0f, 0.0f, -7.0f};
    const GeWaterVec3 fixed = geWaterCompositeReflectPointAcrossPlane(
        on_plane, horizontal_plane);
    failures.expect(approximately(fixed.x, on_plane.x) &&
                        approximately(fixed.y, on_plane.y) &&
                        approximately(fixed.z, on_plane.z),
                    "points on the water plane must remain fixed by reflection");

    const float tilted_plane[4] = {0.6f, 0.8f, 0.0f, -1.0f};
    const GeWaterVec3 twice = geWaterCompositeReflectPointAcrossPlane(
        geWaterCompositeReflectPointAcrossPlane(above, tilted_plane),
        tilted_plane);
    failures.expect(approximately(twice.x, above.x) &&
                        approximately(twice.y, above.y) &&
                        approximately(twice.z, above.z),
                    "planar reflection must be an involution");

    const float invalid_plane[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const GeWaterVec3 rejected = geWaterCompositeReflectPointAcrossPlane(
        above, invalid_plane);
    failures.expect(geWaterCompositeVectorLength(rejected) == 0.0f,
                    "degenerate reflection planes must fail closed");
}

static void test_plane_side_properties(FailureLog &failures)
{
    const float plane[4] = {0.0f, 1.0f, 0.0f, -2.0f};
    const GeWaterVec3 above = {0.0f, 3.0f, 0.0f};
    const GeWaterVec3 below = {0.0f, 1.0f, 0.0f};
    failures.expect(approximately(
                        geWaterCompositePlaneSignedDistance(above, plane),
                        1.0f),
                    "plane distance must be signed in the plane's normalized frame");
    failures.expect(approximately(
                        geWaterCompositePlaneSignedDistance(below, plane),
                        -1.0f),
                    "plane distance must preserve the below-water sign");
    failures.expect(geWaterCompositePlaneKeepsPoint(
                        above, plane, 1.0f, 0.0001f) &&
                        !geWaterCompositePlaneKeepsPoint(
                            below, plane, 1.0f, 0.0001f),
                    "reflection clipping must keep only the camera-facing plane side");
    failures.expect(geWaterCompositePlaneKeepsPoint(
                        below, plane, -1.0f, 0.0001f) &&
                        !geWaterCompositePlaneKeepsPoint(
                            above, plane, -1.0f, 0.0001f),
                    "reflection clipping must support the opposite plane orientation");

    const float degenerate[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    failures.expect(!geWaterCompositePlaneKeepsPoint(
                        above, degenerate, 1.0f, 0.0001f),
                    "degenerate clipping planes must fail closed");
}

static uint32_t next_random(uint32_t &state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

static float random_unit(uint32_t &state)
{
    const uint32_t sample = next_random(state) & 0xffffu;
    return static_cast<float>(sample) / 32767.5f - 1.0f;
}

static void test_refraction_properties(FailureLog &failures)
{
    uint32_t state = 0x007a7e11u;
    const float eta_values[] = {
        GE_WATER_REFRACTION_AIR_TO_WATER,
        1.0f,
        GE_WATER_REFRACTION_WATER_TO_AIR,
    };

    for (int sample = 0; sample < 512; ++sample) {
        const GeWaterVec3 incident = {
            random_unit(state), random_unit(state), random_unit(state),
        };
        const GeWaterVec3 normal = {
            random_unit(state), random_unit(state), random_unit(state),
        };
        for (float eta : eta_values) {
            const GeWaterVec3 refracted = geWaterCompositeRefractVector(
                incident, normal, eta);
            const float length = geWaterCompositeVectorLength(refracted);
            failures.expect(length == 0.0f || approximately(length, 1.0f),
                            "refraction must either fail closed or remain normalized");
            failures.expect(geWaterCompositeVectorIsFinite(refracted),
                            "refraction must never produce non-finite components");
        }
    }

    const GeWaterVec3 incident = normalize_reference({0.35f, -0.8f, 0.2f});
    const GeWaterVec3 normal = normalize_reference({0.1f, 0.98f, 0.12f});
    const GeWaterVec3 passthrough = geWaterCompositeRefractVector(
        incident, normal, 1.0f);
    failures.expect(approximately(passthrough.x, incident.x) &&
                        approximately(passthrough.y, incident.y) &&
                        approximately(passthrough.z, incident.z),
                    "equal media must leave a refracted ray unchanged");
}

static void test_projective_and_edge_properties(FailureLog &failures)
{
    const GeWaterVec3 rays[] = {
        {-0.8f, -0.4f, -0.5f},
        {0.2f, 0.6f, -1.0f},
        {1.4f, -0.7f, -2.5f},
    };
    for (GeWaterVec3 ray : rays) {
        const GeWaterVec2 base = geWaterCompositeProjectRayToScreen(ray);
        const GeWaterVec2 scaled = geWaterCompositeProjectRayToScreen({
            ray.x * 3.0f, ray.y * 3.0f, ray.z * 3.0f,
        });
        failures.expect(approximately(base.x, scaled.x) &&
                            approximately(base.y, scaled.y),
                        "projected ray UVs must be invariant under positive ray scaling");
    }

    const float depths[] = {0.01f, 0.1f, 0.5f, 1.0f, 10.0f};
    for (float center : depths) {
        const float edge = geWaterCompositeDepthEdge(
            center, center - 0.25f, center + 0.1f,
            center - 0.05f, center + 0.2f);
        failures.expect(edge >= 0.0f && edge <= 1.0f,
                        "depth edge strength must remain bounded for finite depths");
        const float permuted = geWaterCompositeDepthEdge(
            center, center + 0.2f, center - 0.05f,
            center + 0.1f, center - 0.25f);
        failures.expect(approximately(edge, permuted),
                        "depth edge strength must not depend on neighbour ordering");
    }

    failures.expect(geWaterCompositeClipToNdc(-2.0f, -1.0f) == 0.0f,
                    "negative homogeneous W must fail closed");
    failures.expect(geWaterCompositeClipToUv(1.0f, -1.0f) == 0.5f,
                    "negative homogeneous W must not address a texture edge");
}

static void test_wave_derivative_properties(FailureLog &failures)
{
    const GeWaterVec2 uv = {0.17f, 0.29f};
    const GeWaterVec2 direction = {1.0f, 0.65f};
    const GeWaterWaveSample sample = geWaterCompositeWaveSample(
        uv, 1.25f, GE_WATER_DEFAULT_WAVELENGTH,
        GE_WATER_DEFAULT_AMPLITUDE, GE_WATER_DEFAULT_SPEED, 0.008f,
        direction);
    failures.expect(geWaterFinite(sample.height) &&
                        geWaterFinite(sample.derivative_x) &&
                        geWaterFinite(sample.derivative_y),
                    "wave samples must remain finite");
    failures.expect(geWaterAbs(sample.height) <= 0.0085f,
                    "wave height must respect the sum-of-sines amplitude budget");

    const float epsilon = 0.001f;
    const GeWaterWaveSample left = geWaterCompositeWaveSample(
        {uv.x - epsilon, uv.y}, 1.25f, GE_WATER_DEFAULT_WAVELENGTH,
        GE_WATER_DEFAULT_AMPLITUDE, GE_WATER_DEFAULT_SPEED, 0.008f,
        direction);
    const GeWaterWaveSample right = geWaterCompositeWaveSample(
        {uv.x + epsilon, uv.y}, 1.25f, GE_WATER_DEFAULT_WAVELENGTH,
        GE_WATER_DEFAULT_AMPLITUDE, GE_WATER_DEFAULT_SPEED, 0.008f,
        direction);
    const float finite_difference_x =
        (right.height - left.height) / (2.0f * epsilon);
    failures.expect(std::fabs(finite_difference_x - sample.derivative_x) < 0.02f,
                    "analytic X wave derivative must match a central difference");

    const GeWaterWaveSample zero_scale = geWaterCompositeWaveSample(
        uv, 1.25f, GE_WATER_DEFAULT_WAVELENGTH,
        GE_WATER_DEFAULT_AMPLITUDE, GE_WATER_DEFAULT_SPEED, 0.0f,
        direction);
    failures.expect(zero_scale.height == 0.0f &&
                        zero_scale.derivative_x == 0.0f &&
                        zero_scale.derivative_y == 0.0f,
                    "zero wave scale must produce a flat surface");

    const GeWaterWaveSample later = geWaterCompositeWaveSample(
        uv, 7.25f, GE_WATER_DEFAULT_WAVELENGTH,
        GE_WATER_DEFAULT_AMPLITUDE, GE_WATER_DEFAULT_SPEED, 0.008f,
        direction);
    failures.expect(std::fabs(later.height - sample.height) > 0.000001f,
                    "wave phase must respond to deterministic elapsed time");

    const GeWaterWaveSample fallback_direction = geWaterCompositeWaveSample(
        uv, 1.25f, GE_WATER_DEFAULT_WAVELENGTH,
        GE_WATER_DEFAULT_AMPLITUDE, GE_WATER_DEFAULT_SPEED, 0.008f,
        {0.0f, 0.0f});
    const GeWaterWaveSample explicit_direction = geWaterCompositeWaveSample(
        uv, 1.25f, GE_WATER_DEFAULT_WAVELENGTH,
        GE_WATER_DEFAULT_AMPLITUDE, GE_WATER_DEFAULT_SPEED, 0.008f,
        {1.0f, 0.0f});
    failures.expect(approximately(fallback_direction.height,
                                  explicit_direction.height),
                    "zero wave direction must use the finite +X fallback");

    const GeWaterWaveSample malformed = geWaterCompositeWaveSample(
        {NAN, uv.y}, 1.25f, GE_WATER_DEFAULT_WAVELENGTH,
        GE_WATER_DEFAULT_AMPLITUDE, GE_WATER_DEFAULT_SPEED, 0.008f,
        direction);
    failures.expect(malformed.height == 0.0f &&
                        malformed.derivative_x == 0.0f &&
                        malformed.derivative_y == 0.0f,
                    "non-finite wave coordinates must fail closed");
}

static void test_malformed_input_properties(FailureLog &failures)
{
    const GeWaterVec3 non_finite_vectors[] = {
        {NAN, 0.0f, 0.0f},
        {0.0f, INFINITY, 0.0f},
        {0.0f, 0.0f, -INFINITY},
    };
    for (GeWaterVec3 value : non_finite_vectors) {
        failures.expect(!geWaterCompositeVectorIsFinite(value),
                        "malformed vectors must be rejected before water math");
        failures.expect(geWaterCompositeVectorDot(value,
                                                  {1.0f, 0.0f, 0.0f}) == 0.0f &&
                            geWaterCompositeVectorDot(
                                {1.0f, 0.0f, 0.0f}, value) == 0.0f,
                        "malformed vector dot products must fail closed");
        failures.expect(geWaterCompositeVectorLength(value) == 0.0f,
                        "malformed vector lengths must fail closed");
        failures.expect(geWaterCompositeVectorLength(
                            geWaterCompositeReflectVector(value,
                                                         {0.0f, 1.0f, 0.0f})) == 0.0f,
                            "malformed reflection inputs must fail closed");
    }

    const GeWaterVec3 finite_overflow_vector = {
        3.4e38f, 3.4e38f, 3.4e38f,
    };
    failures.expect(geWaterCompositeVectorIsFinite(finite_overflow_vector),
                    "large finite vectors must remain distinguishable from NaN/Inf input");
    failures.expect(geWaterCompositeVectorLength(finite_overflow_vector) == 0.0f,
                    "overflowing finite vector lengths must fail closed");

    failures.expect(geWaterCompositeRefractVector(
                        {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                        -1.0f).x == 0.0f,
                    "negative refractive indices must fail closed");
    failures.expect(geWaterCompositeRayScreenOffset(
                        {0.0f, 0.0f, -1.0f},
                        {0.0f, 0.0f, -INFINITY}).x == 0.0f,
                    "non-finite bent rays must not generate UV offsets");
}

int main()
{
    FailureLog failures;
    test_fresnel_reference(failures);
    test_reflection_properties(failures);
    test_planar_reflection_properties(failures);
    test_plane_side_properties(failures);
    test_refraction_properties(failures);
    test_projective_and_edge_properties(failures);
    test_wave_derivative_properties(failures);
    test_malformed_input_properties(failures);

    if (failures.count == 0) {
        std::fprintf(stderr, "water-render reference math suite passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d water-render reference math failure(s)\n",
                 failures.count);
    return 1;
}
