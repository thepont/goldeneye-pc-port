/* Deterministic tests for the water heightfield functional core.
 *
 * The test deliberately does not create a window, load the ROM, or touch GL.
 * A stationary boat/dam is an occluder that receives waves; only a moving
 * boat is allowed to produce a wake impulse through the separate wake policy.
 */
#include "gfx_water_heightfield_policy.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

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

static bool approximately(float actual, float expected, float epsilon = 0.00001f)
{
    return std::fabs(actual - expected) <= epsilon;
}

static bool finite_heightfield(const GeWaterHeightfield &field)
{
    const GeWaterHeightfieldView view = field.view();
    if (view.width != GE_WATER_HEIGHTFIELD_SIZE ||
        view.height != GE_WATER_HEIGHTFIELD_SIZE ||
        view.heights == nullptr || view.normal_x == nullptr ||
        view.normal_z == nullptr) {
        return false;
    }

    for (std::size_t i = 0; i < GE_WATER_HEIGHTFIELD_CELL_COUNT; ++i) {
        if (!std::isfinite(view.heights[i]) ||
            !std::isfinite(view.normal_x[i]) ||
            !std::isfinite(view.normal_z[i])) {
            return false;
        }
    }
    return true;
}

static void test_reset_and_view(FailureLog &failures)
{
    GeWaterHeightfield field;
    const GeWaterHeightfieldView view = field.view();

    failures.expect(view.width == GE_WATER_HEIGHTFIELD_SIZE,
                    "heightfield view must expose the fixed simulation width");
    failures.expect(view.height == GE_WATER_HEIGHTFIELD_SIZE,
                    "heightfield view must expose the fixed simulation height");
    failures.expect(view.max_height == GE_WATER_HEIGHTFIELD_MAX_HEIGHT,
                    "heightfield view must expose the encoding bound");
    failures.expect(finite_heightfield(field),
                    "a new heightfield must expose finite storage");
    failures.expect(field.steps() == 0 && approximately(field.accumulator(), 0.0f) &&
                        !field.active(),
                    "a new heightfield must have no simulation history");

    for (std::size_t i = 0; i < GE_WATER_HEIGHTFIELD_CELL_COUNT; ++i) {
        failures.expect(approximately(view.heights[i], 0.0f),
                        "reset height must be flat");
        failures.expect(approximately(view.normal_x[i], 0.0f) &&
                            approximately(view.normal_z[i], 0.0f),
                        "reset normal must be vertical");
    }

    failures.expect(field.add_drop(0.5f, 0.5f, 0.12f, 0.04f),
                    "a finite drop must be accepted");
    failures.expect(field.active(),
                    "a finite drop must activate the heightfield simulation");
    field.advance(GE_WATER_HEIGHTFIELD_FIXED_STEP);
    field.reset();
    failures.expect(field.steps() == 0 && approximately(field.accumulator(), 0.0f) &&
                        !field.active(),
                    "reset must clear simulation time after a wave has run");
    failures.expect(finite_heightfield(field),
                    "reset must restore finite storage after a wave has run");
}

static void test_drop_shape_and_bounds(FailureLog &failures)
{
    GeWaterHeightfield field;
    failures.expect(field.add_drop(0.5f, 0.5f, 0.12f, 0.04f),
                    "center drop must be accepted");

    float center_height = 0.0f;
    float center_normal_x = 0.0f;
    float center_normal_z = 0.0f;
    failures.expect(field.sample(0.5f, 0.5f, &center_height,
                                 &center_normal_x, &center_normal_z),
                    "center of a valid drop must be sampleable");
    failures.expect(center_height > 0.0f &&
                        center_height <= GE_WATER_HEIGHTFIELD_MAX_HEIGHT,
                    "a positive drop must create bounded positive height");

    float near_height = 0.0f;
    failures.expect(field.sample(0.5f + 0.08f, 0.5f, &near_height, nullptr,
                                 nullptr),
                    "a point inside a drop must be sampleable");
    failures.expect(near_height > 0.0f && near_height < center_height,
                    "the smoothed drop must fall off away from its center");

    float left_height = 0.0f;
    float right_height = 0.0f;
    field.sample(0.5f - 0.06f, 0.5f, &left_height, nullptr, nullptr);
    field.sample(0.5f + 0.06f, 0.5f, &right_height, nullptr, nullptr);
    failures.expect(approximately(left_height, right_height, 0.0005f),
                    "a centered drop must be symmetric on the x axis");

    float clamped_edge = 0.0f;
    failures.expect(field.sample(-10.0f, 10.0f, &clamped_edge, nullptr, nullptr),
                    "out-of-range sample coordinates must clamp safely");
    failures.expect(std::isfinite(clamped_edge),
                    "clamped samples must remain finite");
    failures.expect(!field.sample(0.5f, 0.5f, nullptr, nullptr, nullptr),
                    "a missing height output must be rejected safely");

    const GeWaterHeightfieldView before_invalid = field.view();
    const float before = before_invalid.heights[GE_WATER_HEIGHTFIELD_SIZE / 2 *
                                                GE_WATER_HEIGHTFIELD_SIZE +
                                                GE_WATER_HEIGHTFIELD_SIZE / 2];
    failures.expect(!field.add_drop(std::numeric_limits<float>::quiet_NaN(),
                                    0.5f, 0.1f, 0.01f),
                    "NaN drop coordinates must be rejected");
    failures.expect(!field.add_drop(0.5f, 0.5f,
                                    std::numeric_limits<float>::infinity(),
                                    0.01f),
                    "infinite drop radius must be rejected");
    failures.expect(!field.add_drop(0.5f, 0.5f, 0.1f,
                                    std::numeric_limits<float>::quiet_NaN()),
                    "NaN drop strength must be rejected");
    const GeWaterHeightfieldView after_invalid = field.view();
    const float after = after_invalid.heights[GE_WATER_HEIGHTFIELD_SIZE / 2 *
                                               GE_WATER_HEIGHTFIELD_SIZE +
                                               GE_WATER_HEIGHTFIELD_SIZE / 2];
    failures.expect(approximately(before, after),
                    "rejected drop input must not mutate the field");

    failures.expect(field.add_drop(0.5f, 0.5f, 0.0f, 100.0f),
                    "finite extreme drops must be clamped and accepted");
    failures.expect(finite_heightfield(field),
                    "clamped extreme drops must remain finite");
    failures.expect(field.maximum_absolute_height() <=
                        GE_WATER_HEIGHTFIELD_MAX_HEIGHT + 0.000001f,
                    "extreme drops must respect the height bound");
}

static void test_step_propagation_and_normals(FailureLog &failures)
{
    const GeWaterHeightfieldNormal flat_normal =
        geWaterHeightfieldNormalFromGradient(0.0f, 0.0f);
    failures.expect(approximately(flat_normal.x, 0.0f) &&
                        approximately(flat_normal.z, 0.0f),
                    "a flat gradient must produce a vertical normal");

    const GeWaterHeightfieldNormal malformed_normal =
        geWaterHeightfieldNormalFromGradient(
            std::numeric_limits<float>::quiet_NaN(), 0.0f);
    failures.expect(approximately(malformed_normal.x, 0.0f) &&
                        approximately(malformed_normal.z, 0.0f),
                    "a non-finite gradient must use a finite normal fallback");

    GeWaterHeightfield field;
    field.add_drop(0.5f, 0.5f, 0.10f, 0.04f);

    const float before_neighbor = field.height_at(25, 32);
    field.advance(GE_WATER_HEIGHTFIELD_FIXED_STEP);
    const float after_neighbor = field.height_at(25, 32);
    failures.expect(approximately(before_neighbor, 0.0f),
                    "a drop must not pre-fill a distant neighbor");
    failures.expect(after_neighbor > 0.0f,
                    "one fixed step must propagate height to a neighbor");
    failures.expect(field.steps() == 1,
                    "one fixed step must perform exactly one simulation step");

    float height = 0.0f;
    float normal_x = 0.0f;
    float normal_z = 0.0f;
    failures.expect(field.sample(0.5f, 0.5f, &height, &normal_x, &normal_z),
                    "a stepped field must remain sampleable");
    failures.expect(std::isfinite(normal_x) && std::isfinite(normal_z) &&
                        std::fabs(normal_x) <= 1.0f &&
                        std::fabs(normal_z) <= 1.0f,
                    "finite-difference normals must stay normalized and bounded");
    failures.expect(finite_heightfield(field),
                    "a stepped field must contain no non-finite values");

    const std::size_t steps_before = field.steps();
    field.advance(1000.0f);
    failures.expect(field.steps() - steps_before <=
                        GE_WATER_HEIGHTFIELD_MAX_SUBSTEPS,
                    "a stalled frame must be bounded to the maximum substeps");
    failures.expect(finite_heightfield(field),
                    "a bounded stalled frame must remain finite");
}

static void test_damping_and_determinism(FailureLog &failures)
{
    GeWaterHeightfield damped;
    damped.add_drop(0.5f, 0.5f, 0.10f, 0.04f);
    const float initial_energy = damped.energy();
    for (int i = 0; i < 240; ++i) {
        damped.advance(GE_WATER_HEIGHTFIELD_FIXED_STEP);
    }
    failures.expect(damped.energy() < initial_energy,
                    "damping must reduce wave energy over time");
    failures.expect(damped.energy() >= 0.0f && std::isfinite(damped.energy()),
                    "damped wave energy must remain finite and non-negative");

    GeWaterHeightfield one_step;
    GeWaterHeightfield split_steps;
    one_step.add_drop(0.37f, 0.63f, 0.08f, 0.025f);
    split_steps.add_drop(0.37f, 0.63f, 0.08f, 0.025f);
    one_step.advance(GE_WATER_HEIGHTFIELD_FIXED_STEP);
    split_steps.advance(GE_WATER_HEIGHTFIELD_FIXED_STEP * 0.5f);
    split_steps.advance(GE_WATER_HEIGHTFIELD_FIXED_STEP * 0.5f);
    failures.expect(one_step.steps() == split_steps.steps(),
                    "fixed-step partitioning must produce the same step count");
    for (std::size_t i = 0; i < GE_WATER_HEIGHTFIELD_CELL_COUNT; ++i) {
        failures.expect(approximately(one_step.view().heights[i],
                                      split_steps.view().heights[i], 0.00001f),
                        "fixed-step partitioning must be deterministic");
    }

    GeWaterHeightfield invalid_delta;
    invalid_delta.add_drop(0.5f, 0.5f, 0.1f, 0.02f);
    const float energy_before = invalid_delta.energy();
    invalid_delta.advance(std::numeric_limits<float>::quiet_NaN());
    invalid_delta.advance(-1.0f);
    failures.expect(invalid_delta.steps() == 0 &&
                        approximately(invalid_delta.energy(), energy_before),
                    "invalid time deltas must not mutate the simulation");

    GeWaterHeightfield near_step;
    near_step.add_drop(0.5f, 0.5f, 0.1f, 0.02f);
    near_step.advance(GE_WATER_HEIGHTFIELD_FIXED_STEP - 0.00000005f);
    failures.expect(near_step.accumulator() >= 0.0f &&
                        near_step.accumulator() <
                            GE_WATER_HEIGHTFIELD_FIXED_STEP,
                    "sub-step rounding must leave a bounded accumulator");

    GeWaterHeightfield negligible_wave;
    negligible_wave.add_drop(0.5f, 0.5f, GE_WATER_HEIGHTFIELD_MIN_DROP_RADIUS,
                             0.000001f);
    negligible_wave.advance(GE_WATER_HEIGHTFIELD_FIXED_STEP);
    failures.expect(!negligible_wave.active() &&
                        approximately(negligible_wave.energy(), 0.0f),
                    "a numerically negligible wave must clear its state after stepping");
}

static void test_boat_wake_policy(FailureLog &failures)
{
    const GeWaterBoatWakeInput stationary = {
        0.50f, 0.50f, 1.0f, 0.0f, 0.0f, 0.20f};
    const GeWaterBoatWakeInput slow = {
        0.50f, 0.50f, 1.0f, 0.0f,
        GE_WATER_BOAT_WAKE_SPEED_THRESHOLD * 0.5f, 0.20f};
    const GeWaterBoatWakeInput moving = {
        0.50f, 0.50f, 1.0f, 0.0f, 0.80f, 0.20f};
    GeWaterBoatWakeBatch batch = {};

    failures.expect(!geWaterBuildBoatWake(moving, nullptr),
                    "a missing wake batch must fail closed");

    failures.expect(geWaterBuildBoatWake(stationary, &batch) &&
                        batch.count == 0,
                    "a stationary boat must not continuously emit a wake");
    failures.expect(geWaterBuildBoatWake(slow, &batch) && batch.count == 0,
                    "sub-threshold boat motion must not emit a wake");
    failures.expect(geWaterBuildBoatWake(moving, &batch) &&
                        batch.count == GE_WATER_BOAT_WAKE_IMPULSE_COUNT,
                    "moving boats must emit a bounded bow and stern wake");

    failures.expect(batch.impulses[0].water_u > moving.water_u,
                    "the first wake impulse must be ahead of a right-facing boat");
    failures.expect(batch.impulses[1].water_v < moving.water_v &&
                        batch.impulses[2].water_v > moving.water_v,
                    "stern wake impulses must diverge to either side");
    failures.expect(approximately(batch.impulses[1].water_u,
                                  batch.impulses[2].water_u, 0.00001f) &&
                        approximately(batch.impulses[1].water_v +
                                          batch.impulses[2].water_v,
                                      2.0f * moving.water_v, 0.00001f),
                    "stern wake impulses must be symmetric around the boat");
    for (std::size_t i = 0; i < batch.count; ++i) {
        failures.expect(std::isfinite(batch.impulses[i].water_u) &&
                            std::isfinite(batch.impulses[i].water_v) &&
                            std::isfinite(batch.impulses[i].radius) &&
                            std::isfinite(batch.impulses[i].strength),
                        "wake impulses must contain finite values");
        failures.expect(batch.impulses[i].radius >=
                            GE_WATER_HEIGHTFIELD_MIN_DROP_RADIUS &&
                            batch.impulses[i].radius <=
                            GE_WATER_HEIGHTFIELD_MAX_DROP_RADIUS &&
                            std::fabs(batch.impulses[i].strength) <=
                            GE_WATER_HEIGHTFIELD_MAX_DROP_STRENGTH,
                        "wake impulses must stay within field input bounds");
    }

    const GeWaterBoatWakeInput high_speed = {
        0.5f, 0.5f, 1.0f, 0.0f, 100000.0f, 100000.0f};
    failures.expect(geWaterBuildBoatWake(high_speed, &batch) &&
                        batch.count == GE_WATER_BOAT_WAKE_IMPULSE_COUNT,
                    "extreme finite boat inputs must be clamped, not rejected");
    for (std::size_t i = 0; i < batch.count; ++i) {
        failures.expect(batch.impulses[i].strength <=
                            GE_WATER_HEIGHTFIELD_MAX_DROP_STRENGTH,
                        "extreme boat speed must not increase wake strength unboundedly");
    }

    const GeWaterBoatWakeInput malformed = {
        std::numeric_limits<float>::quiet_NaN(), 0.5f,
        1.0f, 0.0f, 0.8f, 0.2f};
    failures.expect(!geWaterBuildBoatWake(malformed, &batch) && batch.count == 0,
                    "malformed boat state must fail closed without stale impulses");

    const GeWaterBoatWakeInput zero_heading = {
        0.5f, 0.5f, 0.0f, 0.0f, 0.8f, 0.2f};
    failures.expect(!geWaterBuildBoatWake(zero_heading, &batch) &&
                        batch.count == 0,
                    "a zero-length boat heading must fail closed");
}

static void test_world_anchored_surface_uv(FailureLog &failures)
{
    float water_u = 0.0f;
    float water_v = 0.0f;
    failures.expect(geWaterSurfaceUvFromWorld(512.0f, -256.0f,
                                              &water_u, &water_v),
                    "finite world coordinates must map to water UVs");
    failures.expect(approximately(water_u, -1.0f) &&
                        approximately(water_v, 2.0f),
                    "world water UVs must use the stable z/x surface basis");

    float translated_u = 0.0f;
    float translated_v = 0.0f;
    failures.expect(geWaterSurfaceUvFromWorld(512.0f, -256.0f,
                                              &translated_u, &translated_v),
                    "the same world point must remain mappable after camera movement");
    failures.expect(approximately(water_u, translated_u) &&
                        approximately(water_v, translated_v),
                    "camera movement must not change a stationary world point's water UV");
    failures.expect(!geWaterSurfaceUvFromWorld(
                        std::numeric_limits<float>::quiet_NaN(), 0.0f,
                        &water_u, &water_v),
                    "non-finite world coordinates must fail closed");
    failures.expect(!geWaterSurfaceUvFromWorld(0.0f, 0.0f, nullptr, &water_v),
                    "a missing water U output must fail closed");
    failures.expect(approximately(
                        geWaterHeightfieldWrap01(
                            std::numeric_limits<float>::quiet_NaN()),
                        0.0f),
                    "non-finite wrapped coordinates must fail closed to zero");
}

static void test_world_repeat_alignment(FailureLog &failures)
{
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    failures.expect(geWaterSurfaceUvFromWorld(64.0f, 64.0f, &u0, &v0) &&
                        geWaterSurfaceUvFromWorld(
                            64.0f + GE_WATER_SURFACE_REPEAT_METERS, 64.0f,
                            &u1, &v1),
                    "periodic world points must produce valid water coordinates");

    GeWaterHeightfield first;
    GeWaterHeightfield repeated;
    failures.expect(first.add_drop(u0, v0, 0.12f, 0.04f) &&
                        repeated.add_drop(u1, v1, 0.12f, 0.04f),
                    "periodic world points must accept identical disturbances");
    const GeWaterHeightfieldView first_view = first.view();
    const GeWaterHeightfieldView repeated_view = repeated.view();
    for (std::size_t index = 0; index < GE_WATER_HEIGHTFIELD_CELL_COUNT;
         ++index) {
        failures.expect(approximately(first_view.heights[index],
                                      repeated_view.heights[index], 0.000001f),
                        "world-repeat disturbances must produce identical heightfields");
    }
}

int main()
{
    FailureLog failures;
    test_reset_and_view(failures);
    test_drop_shape_and_bounds(failures);
    test_step_propagation_and_normals(failures);
    test_damping_and_determinism(failures);
    test_boat_wake_policy(failures);
    test_world_anchored_surface_uv(failures);
    test_world_repeat_alignment(failures);

    if (failures.count != 0) {
        std::fprintf(stderr, "%d water heightfield contract(s) failed\n",
                     failures.count);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
