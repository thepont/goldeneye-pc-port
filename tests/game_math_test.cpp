/*
 * Deterministic tests for the reusable game-side math primitives.
 *
 * These tests link the real src/game implementations. They deliberately do
 * not include the wider decomp header graph: the C ABI below is the small
 * value-type boundary consumed by these pure functions. No ROM, level,
 * renderer, display, or game loop is needed.
 */
#include <cmath>
#include <cstdint>
#include <cstdio>

extern "C" {
typedef float f32;
typedef uint16_t u16;

typedef u16 vec3u[3];
typedef f32 vec3f[3];
typedef f32 mat44f[4][4];
typedef f32 quatf[4];

f32 acosf(f32 cosinef);
f32 asinf(f32 sinef);
f32 atan2f(f32 y, f32 x);
f32 ceilFloat(f32 value);
int32_t ceilFloatToInt(f32 value);
f32 floorFloat(f32 value);
int32_t floorFloatToInt(f32 value);
f32 modelGetBendStretchScale(f32 halfangle);

void quaternion_set_rotation_around_xyz(vec3u angles, quatf q);
void quaternion_set_rotation_around_xyzf(vec3f angles, quatf q);
void quaternion_set_rotation_around_x(f32 angle, quatf q);
void quaternion_set_rotation_around_y(f32 angle, quatf q);
void quaternion_set_rotation_around_z(f32 angle, quatf q);
void quaternion_to_matrix(quatf q, mat44f matrix);
void quaternion_from_matrix(mat44f matrix, quatf q);
void quaternion_to_transform_matrix(vec3f position, quatf rotation,
                                    mat44f matrix);
void quaternion_slerp(quatf q1, quatf q2, f32 t, quatf result);
void quaternion_7F05BC68(quatf q, f32 t, quatf result);
void quaternion_ensure_shortest_path(quatf q1, quatf q2);
void quaternion_multiply(quatf lhs, quatf rhs, quatf result);
void quaternion_multiply_in_place(quatf lhs, quatf rhs);
void quaternion_7F05BFD4(quatf q1, quatf q2);
void quaternion_7F05C068(quatf q1, quatf q2);
void quaternion_7F05C138(quatf arg0, quatf arg1, quatf arg2, quatf result);
void quaternion_7F05C250(quatf q1, quatf q2, quatf q3, quatf q4,
                          f32 t, quatf result);
void quaternion_7F05C2F0(quatf q1, quatf q2, quatf q3, quatf q4,
                          f32 t, quatf result);
}

namespace
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = kPi * 0.5f;
constexpr float kTau = kPi * 2.0f;
constexpr float kEpsilon = 0.00025f;

struct TestState
{
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        if (!condition)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }

    void near(float actual, float expected, const char *message,
              float epsilon = kEpsilon)
    {
        if (!std::isfinite(actual) || std::fabs(actual - expected) > epsilon)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s (actual=%g expected=%g)\n",
                         message, actual, expected);
        }
    }
};

bool finite_quaternion(const quatf q)
{
    for (int i = 0; i < 4; ++i)
    {
        if (!std::isfinite(q[i]))
        {
            return false;
        }
    }
    return true;
}

bool finite_matrix(const mat44f matrix)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(matrix[column][row]))
            {
                return false;
            }
        }
    }
    return true;
}

void expect_quaternion(TestState &tests, const quatf actual,
                       const quatf expected, const char *message,
                       float epsilon = kEpsilon)
{
    for (int i = 0; i < 4; ++i)
    {
        char detail[160];
        std::snprintf(detail, sizeof(detail), "%s[%d]", message, i);
        tests.near(actual[i], expected[i], detail, epsilon);
    }
}

void expect_matrix(TestState &tests, const mat44f actual,
                   const mat44f expected, const char *message,
                   float epsilon = kEpsilon)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            char detail[160];
            std::snprintf(detail, sizeof(detail), "%s[%d][%d]", message,
                          column, row);
            tests.near(actual[column][row], expected[column][row], detail,
                       epsilon);
        }
    }
}

void expect_identity(TestState &tests, const mat44f matrix,
                     const char *message, float epsilon = kEpsilon)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            char detail[160];
            std::snprintf(detail, sizeof(detail), "%s[%d][%d]", message,
                          column, row);
            tests.near(matrix[column][row], column == row ? 1.0f : 0.0f,
                       detail, epsilon);
        }
    }
}

float quaternion_norm(const quatf q)
{
    return std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] +
                     q[3] * q[3]);
}

/* The port intentionally supplies game-compatible names such as atan2f and
 * acosf. Calling through the function address prevents the host compiler from
 * constant-folding a test call to its libc builtin with different semantics. */
f32 game_acosf(f32 value)
{
    /* Keep GCC from folding this call to the host libc builtin. The game
     * implementation intentionally uses the N64 lookup-table approximation
     * and has different endpoint/angle semantics. */
    volatile f32 runtime_value = value;
    const auto function = &acosf;
    return function(runtime_value);
}

f32 game_asinf(f32 value)
{
    volatile f32 runtime_value = value;
    const auto function = &asinf;
    return function(runtime_value);
}

f32 game_atan2f(f32 y, f32 x)
{
    volatile f32 runtime_y = y;
    volatile f32 runtime_x = x;
    const auto function = &atan2f;
    return function(runtime_y, runtime_x);
}

void expect_unit_quaternion(TestState &tests, const quatf q,
                            const char *message)
{
    tests.expect(finite_quaternion(q), message);
    char detail[160];
    std::snprintf(detail, sizeof(detail), "%s has unit length", message);
    tests.near(quaternion_norm(q), 1.0f, detail, 0.001f);
}

void test_scalar_math_boundaries(TestState &tests)
{
    tests.near(ceilFloat(-2.75f), -2.0f, "ceil negative fraction");
    tests.near(ceilFloat(-2.0f), -2.0f, "ceil negative integer");
    tests.near(ceilFloat(0.0f), 0.0f, "ceil zero");
    tests.near(ceilFloat(2.0f), 2.0f, "ceil positive integer");
    tests.near(ceilFloat(2.75f), 3.0f, "ceil positive fraction");
    tests.expect(ceilFloatToInt(-2.75f) == -2,
                 "integer ceil negative fraction");
    tests.expect(ceilFloatToInt(2.0f) == 2,
                 "integer ceil positive integer");
    tests.expect(ceilFloatToInt(2.75f) == 3,
                 "integer ceil positive fraction");

    tests.near(floorFloat(-2.75f), -3.0f, "floor negative fraction");
    tests.near(floorFloat(-2.0f), -2.0f, "floor negative integer");
    tests.near(floorFloat(0.0f), 0.0f, "floor zero");
    tests.near(floorFloat(2.0f), 2.0f, "floor positive integer");
    tests.near(floorFloat(2.75f), 2.0f, "floor positive fraction");
    tests.expect(floorFloatToInt(-2.75f) == -3,
                 "integer floor negative fraction");
    tests.expect(floorFloatToInt(-2.0f) == -2,
                 "integer floor negative integer");
    tests.expect(floorFloatToInt(2.75f) == 2,
                 "integer floor positive fraction");

    tests.near(game_acosf(1.0f), 0.0f, "acosf positive clamp", 0.004f);
    tests.near(game_acosf(-1.0f), kPi, "acosf negative clamp", 0.004f);
    tests.near(game_acosf(2.0f), game_acosf(1.0f),
               "acosf above-range clamps to positive boundary", 0.0001f);
    tests.near(game_acosf(-2.0f), game_acosf(-1.0f),
               "acosf below-range clamps to negative boundary", 0.0001f);
    tests.near(game_acosf(0.5f), kPi / 3.0f, "acosf interior", 0.001f);

    tests.near(game_asinf(1.0f), kHalfPi, "asinf positive clamp", 0.004f);
    tests.near(game_asinf(-1.0f), -kHalfPi, "asinf negative clamp", 0.004f);
    tests.near(game_asinf(2.0f), game_asinf(1.0f),
               "asinf above-range clamps to positive boundary", 0.0001f);
    tests.near(game_asinf(-2.0f), game_asinf(-1.0f),
               "asinf below-range clamps to negative boundary", 0.0001f);
    tests.near(game_asinf(0.5f), kPi / 6.0f, "asinf interior", 0.001f);

    tests.near(game_atan2f(0.0f, 1.0f), 0.0f, "atan2f positive x axis");
    tests.near(game_atan2f(0.0f, -1.0f), kPi, "atan2f negative x axis", 0.001f);
    tests.near(game_atan2f(1.0f, 0.0f), kHalfPi, "atan2f positive y axis",
               0.001f);
    tests.near(game_atan2f(-1.0f, 0.0f), kTau - kHalfPi,
               "atan2f negative y axis", 0.001f);
    tests.near(game_atan2f(1.0f, 1.0f), kPi / 4.0f, "atan2f first quadrant",
               0.001f);
    tests.near(game_atan2f(1.0f, -2.0f), 2.677945f,
               "atan2f second quadrant", 0.001f);
    tests.near(game_atan2f(-1.0f, -2.0f), 3.605240f,
               "atan2f third quadrant", 0.001f);
    tests.near(game_atan2f(-2.0f, 1.0f), 5.176036f,
               "atan2f fourth quadrant", 0.001f);
    tests.near(game_atan2f(-1.0f, -0.5f), 4.248741f,
               "atan2f negative x correction", 0.001f);

    tests.near(modelGetBendStretchScale(0.0f), 1.0f,
               "bend stretch scale zero");
    tests.near(modelGetBendStretchScale(kPi / 4.0f), std::sqrt(2.0f),
               "bend stretch scale quarter turn", 0.001f);
}

void test_quaternion_construction(TestState &tests)
{
    quatf identity = {};
    quaternion_set_rotation_around_x(0.0f, identity);
    const quatf expected_identity = {1.0f, 0.0f, 0.0f, 0.0f};
    expect_quaternion(tests, identity, expected_identity,
                      "X identity quaternion");

    quatf x = {};
    quatf y = {};
    quatf z = {};
    quaternion_set_rotation_around_x(kHalfPi, x);
    quaternion_set_rotation_around_y(kHalfPi, y);
    quaternion_set_rotation_around_z(kHalfPi, z);
    const float half_quarter = std::sqrt(0.5f);
    const quatf expected_x = {half_quarter, half_quarter, 0.0f, 0.0f};
    const quatf expected_y = {half_quarter, 0.0f, half_quarter, 0.0f};
    const quatf expected_z = {half_quarter, 0.0f, 0.0f, half_quarter};
    expect_quaternion(tests, x, expected_x, "X axis quaternion");
    expect_quaternion(tests, y, expected_y, "Y axis quaternion");
    expect_quaternion(tests, z, expected_z, "Z axis quaternion");

    vec3f radians = {0.2f, -0.4f, 0.6f};
    quatf from_float = {};
    quaternion_set_rotation_around_xyzf(radians, from_float);
    expect_unit_quaternion(tests, from_float,
                           "float Euler quaternion is normalized");

    vec3u encoded = {0x14D5, 0xF5B3, 0x2731};
    quatf from_encoded = {};
    quaternion_set_rotation_around_xyz(encoded, from_encoded);
    vec3f equivalent = {
        static_cast<float>(encoded[0]) * (kPi / 32768.0f),
        static_cast<float>(encoded[1]) * (kPi / 32768.0f),
        static_cast<float>(encoded[2]) * (kPi / 32768.0f),
    };
    quatf from_equivalent = {};
    quaternion_set_rotation_around_xyzf(equivalent, from_equivalent);
    expect_quaternion(tests, from_encoded, from_equivalent,
                      "encoded and float Euler rotations agree", 0.001f);
}

void test_quaternion_matrix_round_trip(TestState &tests)
{
    quatf rotations[4] = {};
    quaternion_set_rotation_around_x(0.7f, rotations[0]);
    quaternion_set_rotation_around_y(-0.8f, rotations[1]);
    quaternion_set_rotation_around_z(1.1f, rotations[2]);
    vec3f euler = {0.2f, -0.4f, 0.6f};
    quaternion_set_rotation_around_xyzf(euler, rotations[3]);

    for (int i = 0; i < 4; ++i)
    {
        mat44f matrix = {};
        quaternion_to_matrix(rotations[i], matrix);
        tests.expect(finite_matrix(matrix),
                     "quaternion matrix conversion is finite");

        quatf recovered = {};
        quaternion_from_matrix(matrix, recovered);
        expect_unit_quaternion(tests, recovered,
                               "matrix conversion recovers unit quaternion");

        mat44f round_trip = {};
        quaternion_to_matrix(recovered, round_trip);
        expect_matrix(tests, round_trip, matrix,
                      "quaternion matrix round trip", 0.001f);
    }

    quatf half_turn = {};
    mat44f half_turn_matrix = {};
    quaternion_set_rotation_around_x(kPi, half_turn);
    quaternion_to_matrix(half_turn, half_turn_matrix);
    quatf recovered = {};
    quaternion_from_matrix(half_turn_matrix, recovered);
    expect_unit_quaternion(tests, recovered,
                           "negative-trace X matrix conversion");

    quaternion_set_rotation_around_y(kPi, half_turn);
    quaternion_to_matrix(half_turn, half_turn_matrix);
    quaternion_from_matrix(half_turn_matrix, recovered);
    expect_unit_quaternion(tests, recovered,
                           "negative-trace Y matrix conversion");

    quaternion_set_rotation_around_z(kPi, half_turn);
    quaternion_to_matrix(half_turn, half_turn_matrix);
    quaternion_from_matrix(half_turn_matrix, recovered);
    expect_unit_quaternion(tests, recovered,
                           "negative-trace Z matrix conversion");

    mat44f transform = {};
    vec3f position = {3.0f, -4.0f, 5.0f};
    quatf rotation = {};
    quaternion_set_rotation_around_y(0.3f, rotation);
    quaternion_to_transform_matrix(position, rotation, transform);
    tests.near(transform[3][0], 3.0f, "quaternion transform position x");
    tests.near(transform[3][1], -4.0f, "quaternion transform position y");
    tests.near(transform[3][2], 5.0f, "quaternion transform position z");
    tests.near(transform[3][3], 1.0f, "quaternion transform homogeneous w");
}

void test_quaternion_degenerate_inputs(TestState &tests)
{
    quatf zero = {0.0f, 0.0f, 0.0f, 0.0f};
    mat44f matrix = {};
    quaternion_to_matrix(zero, matrix);
    tests.expect(finite_matrix(matrix),
                 "zero quaternion conversion does not create NaNs");
    expect_identity(tests, matrix,
                    "zero quaternion conversion falls back to identity");

    mat44f zero_matrix = {};
    quatf recovered = {};
    quaternion_from_matrix(zero_matrix, recovered);
    tests.expect(finite_quaternion(recovered),
                 "zero matrix conversion does not create NaNs");
    const quatf expected_identity = {1.0f, 0.0f, 0.0f, 0.0f};
    expect_quaternion(tests, recovered, expected_identity,
                      "zero matrix conversion falls back to identity");
}

void test_quaternion_interpolation_and_composition(TestState &tests)
{
    quatf identity = {1.0f, 0.0f, 0.0f, 0.0f};
    quatf quarter_turn = {};
    quaternion_set_rotation_around_z(kHalfPi, quarter_turn);

    quatf result = {};
    quaternion_slerp(identity, quarter_turn, 0.0f, result);
    expect_quaternion(tests, result, identity, "slerp start");
    quaternion_slerp(identity, quarter_turn, 1.0f, result);
    expect_quaternion(tests, result, quarter_turn, "slerp end");
    quaternion_slerp(identity, quarter_turn, 0.5f, result);
    expect_unit_quaternion(tests, result, "slerp midpoint");

    quatf opposite = {-1.0f, 0.0f, 0.0f, 0.0f};
    quaternion_slerp(identity, opposite, 0.5f, result);
    expect_quaternion(tests, result, identity,
                      "slerp antipodal fallback follows stable path");

    quatf near_identity = {0.9999999f, 0.0001f, 0.0f, 0.0f};
    quaternion_slerp(identity, near_identity, 0.5f, result);
    expect_unit_quaternion(tests, result, "slerp near-identical fallback");

    quatf path = {-0.5f, 0.5f, 0.0f, 0.0f};
    quatf path_result = {};
    quaternion_7F05BC68(path, 0.0f, path_result);
    const quatf expected_negative_identity = {-1.0f, 0.0f, 0.0f, 0.0f};
    expect_quaternion(tests, path_result, expected_negative_identity,
                      "single quaternion interpolation preserves sign", 0.001f);
    quaternion_7F05BC68(path, 1.0f, path_result);
    expect_quaternion(tests, path_result, path,
                      "single quaternion interpolation end", 0.001f);
    quaternion_7F05BC68(identity, 0.5f, path_result);
    expect_quaternion(tests, path_result, identity,
                      "single quaternion interpolation identity branch",
                      0.001f);

    quatf flipped = {-0.5f, -0.5f, 0.0f, 0.0f};
    quaternion_ensure_shortest_path(identity, flipped);
    const quatf expected_flipped = {0.5f, 0.5f, 0.0f, 0.0f};
    expect_quaternion(tests, flipped, expected_flipped,
                      "shortest path flips negative dot");
    quaternion_ensure_shortest_path(identity, quarter_turn);
    const quatf expected_quarter = {std::sqrt(0.5f), 0.0f, 0.0f,
                                    std::sqrt(0.5f)};
    expect_quaternion(tests, quarter_turn, expected_quarter,
                      "shortest path preserves positive dot");

    quatf product = {};
    quaternion_multiply(identity, quarter_turn, product);
    expect_quaternion(tests, product, quarter_turn,
                      "quaternion multiplication identity");
    quaternion_multiply_in_place(identity, product);
    expect_quaternion(tests, product, quarter_turn,
                      "in-place quaternion multiplication identity");

    quatf logarithm = {};
    quaternion_7F05BFD4(identity, logarithm);
    const quatf expected_zero = {0.0f, 0.0f, 0.0f, 0.0f};
    expect_quaternion(tests, logarithm, expected_zero,
                      "quaternion logarithm identity");
    quaternion_7F05BFD4(quarter_turn, logarithm);
    tests.expect(finite_quaternion(logarithm),
                 "quaternion logarithm nonzero rotation is finite");

    quatf exponential = {};
    quaternion_7F05C068(logarithm, exponential);
    expect_unit_quaternion(tests, exponential,
                           "quaternion exponential is normalized");
    quatf zero_log = {0.0f, 0.0f, 0.0f, 0.0f};
    quaternion_7F05C068(zero_log, exponential);
    expect_quaternion(tests, exponential, identity,
                      "quaternion exponential identity");

    quatf control_a = {};
    quatf control_b = {};
    quaternion_set_rotation_around_x(0.2f, control_a);
    quaternion_set_rotation_around_y(0.3f, control_b);
    quatf squad_tangent = {};
    quaternion_7F05C138(identity, control_a, control_b, squad_tangent);
    expect_unit_quaternion(tests, squad_tangent,
                           "quaternion squad tangent");

    quatf squad = {};
    quaternion_7F05C250(identity, control_a, control_b, quarter_turn, 0.5f,
                        squad);
    expect_unit_quaternion(tests, squad, "quaternion squad interpolation");
    quaternion_7F05C2F0(identity, control_a, control_b, quarter_turn, 0.5f,
                        squad);
    expect_unit_quaternion(tests, squad,
                           "nested quaternion squad interpolation");
}

} // namespace

int main()
{
    TestState tests;
    test_scalar_math_boundaries(tests);
    test_quaternion_construction(tests);
    test_quaternion_matrix_round_trip(tests);
    test_quaternion_degenerate_inputs(tests);
    test_quaternion_interpolation_and_composition(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d game-math contract(s) failed\n",
                     tests.failures);
        return 1;
    }
    std::puts("game-math unit suite passed");
    return 0;
}
