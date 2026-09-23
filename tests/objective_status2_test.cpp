/* Deterministic tests for the small physics/matrix helpers in objective_status2.c. */
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

extern "C" {
struct coord3d
{
    float f[3];
};

struct Mtxf
{
    float m[4][4];
};

void mtxLoadRandomRotation(Mtxf *matrix);
void sub_GAME_7F057C14(coord3d *coord, Mtxf *matrix);
void sub_GAME_7F057D44(float *position, float *velocity, float delta);
void sub_GAME_7F057D88(float *position, float *velocity, float delta);
void sub_GAME_7F057DF8(Mtxf *lhs, Mtxf *rhs, std::int32_t count);
void sub_GAME_7F057E58(float *position, float *velocity, Mtxf *lhs,
                       Mtxf *rhs, std::int32_t delta);

/* Fakes at the pure helper boundary. The test does not duplicate matrix
 * arithmetic; it verifies that objective_status2 passes the right values and
 * repeats the requested operation the right number of times. */
void matrix_4x4_set_rotation_around_xyz(coord3d *angles, Mtxf *matrix);
void matrix_4x4_multiply_homogeneous_in_place(Mtxf *lhs, Mtxf *rhs);
std::uint32_t randomGetNext();
}

namespace
{

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
              float epsilon = 0.00001f)
    {
        if (!std::isfinite(actual) || std::fabs(actual - expected) > epsilon)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s (actual=%g expected=%g)\n",
                         message, actual, expected);
        }
    }
};

std::array<std::uint32_t, 16> g_random_values = {};
std::size_t g_random_index = 0;
int g_rotation_calls = 0;
int g_multiply_calls = 0;
coord3d g_last_rotation = {};

void reset_fakes()
{
    g_random_values.fill(0);
    g_random_index = 0;
    g_rotation_calls = 0;
    g_multiply_calls = 0;
    g_last_rotation = {};
}

void test_random_rotation_helpers(TestState &tests)
{
    reset_fakes();
    Mtxf matrix = {};
    mtxLoadRandomRotation(&matrix);

    tests.expect(g_random_index == 3,
                 "random rotation consumes three random values");
    tests.expect(g_rotation_calls == 1,
                 "random rotation dispatches one matrix construction");
    tests.near(g_last_rotation.f[0], -0.09817477f,
               "zero random value produces the lower X angle");
    tests.near(g_last_rotation.f[1], -0.09817477f,
               "zero random value produces the lower Y angle");
    tests.near(g_last_rotation.f[2], -0.09817477f,
               "zero random value produces the lower Z angle");

    reset_fakes();
    g_random_values[0] = 0xFFFFFFFFU;
    g_random_values[1] = 0x80000000U;
    g_random_values[2] = 0x40000000U;
    coord3d offset = {};
    sub_GAME_7F057C14(&offset, &matrix);

    tests.expect(g_random_index == 6,
                 "debris helper consumes position and rotation random values");
    tests.near(offset.f[0], 3.3333333f,
               "debris X offset uses the full random range", 0.0001f);
    tests.near(offset.f[1], 3.3333333f,
               "debris Y offset uses the full random range", 0.0001f);
    tests.near(offset.f[2], -1.6666666f,
               "debris Z offset uses the full random range", 0.0001f);
}

void test_velocity_integration(TestState &tests)
{
    float position = 10.0f;
    float velocity = 3.0f;
    sub_GAME_7F057D44(&position, &velocity, 6.0f);
    tests.near(position, 23.0f,
               "D44 integrates position using the average velocity");
    tests.near(velocity, 1.3333333f,
               "D44 applies the fixed-frame deceleration");

    float positions[3] = {0.0f, 10.0f, 20.0f};
    float velocities[3] = {1.0f, 3.0f, 5.0f};
    sub_GAME_7F057D88(positions, velocities, 6.0f);
    tests.near(positions[0], 6.0f, "D88 integrates horizontal X velocity");
    tests.near(positions[1], 23.0f, "D88 delegates vertical integration");
    tests.near(positions[2], 50.0f, "D88 integrates horizontal Z velocity");
    tests.near(velocities[1], 1.3333333f,
               "D88 preserves D44's updated vertical velocity");
}

void test_repeated_matrix_dispatch(TestState &tests)
{
    reset_fakes();
    Mtxf lhs = {};
    Mtxf rhs = {};
    lhs.m[0][0] = 2.0f;
    rhs.m[0][0] = 1.0f;

    sub_GAME_7F057DF8(&lhs, &rhs, 0);
    tests.expect(g_multiply_calls == 0,
                 "DF8 does not multiply for a zero count");

    sub_GAME_7F057DF8(&lhs, &rhs, 3);
    tests.expect(g_multiply_calls == 3,
                 "DF8 repeats the homogeneous multiply exactly count times");
    tests.near(lhs.m[0][0], 5.0f,
               "DF8 applies each repeated matrix operation");

    reset_fakes();
    float position[3] = {0.0f, 10.0f, 20.0f};
    float velocity[3] = {1.0f, 3.0f, 5.0f};
    sub_GAME_7F057E58(position, velocity, &lhs, &rhs, 2);
    tests.near(position[0], 2.0f, "E58 integrates X before matrix updates");
    tests.near(position[1], 15.444444f,
               "E58 integrates Y before matrix updates");
    tests.near(position[2], 30.0f, "E58 integrates Z before matrix updates");
    tests.near(velocity[1], 2.4444444f,
               "E58 preserves D44's fixed-frame update");
    tests.expect(g_multiply_calls == 2,
                 "E58 forwards its count to the matrix helper");
}

} // namespace

extern "C" std::uint32_t randomGetNext()
{
    const std::uint32_t value = g_random_values[g_random_index %
                                                g_random_values.size()];
    ++g_random_index;
    return value;
}

extern "C" void matrix_4x4_set_rotation_around_xyz(coord3d *angles,
                                                     Mtxf *matrix)
{
    g_last_rotation = *angles;
    ++g_rotation_calls;
    matrix->m[0][0] = 1.0f;
}

extern "C" void matrix_4x4_multiply_homogeneous_in_place(Mtxf *lhs,
                                                           Mtxf *rhs)
{
    rhs->m[0][0] += lhs->m[0][0];
    ++g_multiply_calls;
}

int main()
{
    TestState tests;
    test_random_rotation_helpers(tests);
    test_velocity_integration(tests);
    test_repeated_matrix_dispatch(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d objective-status contract(s) failed\n",
                     tests.failures);
        return 1;
    }
    std::puts("Objective-status unit suite passed");
    return 0;
}
