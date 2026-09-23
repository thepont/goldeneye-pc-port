/*
 * Unit tests for the real game matrix implementation.
 *
 * These tests deliberately link src/game/matrixmath.c and
 * src/game/matrixmath_misc.c. The fixtures are plain matrices/vectors; no
 * ROM, model, display, SDL, or level process is involved.
 */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

/* Keep this test's C++ standard-library surface separate from the decomp's
 * C-only ultra64/bondtypes graph. These are the ABI-compatible value types
 * consumed by matrixmath.c, and every function below is the real C symbol. */
extern "C" {
typedef union
{
    float m[4][4];
    int32_t unused;
} Mtxf;

typedef union
{
    int32_t m[4][4];
    int64_t force_structure_alignment;
} Mtx;

typedef struct coord3d
{
    union
    {
        struct
        {
            float x;
            float y;
            float z;
        };
        float f[3];
    };
} coord3d;

void matrix_4x4_set_identity(Mtxf *matrix);
void matrix_4x4_copy(Mtxf *src, Mtxf *dst);
void matrix_4x4_set_position(coord3d *position, Mtxf *matrix);
void matrix_4x4_set_identity_and_position(coord3d *position, Mtxf *matrix);
void matrix_4x4_set_rotation_around_x(float angle, Mtxf *matrix);
void matrix_4x4_set_rotation_around_y(float angle, Mtxf *matrix);
void matrix_4x4_set_rotation_around_z(float angle, Mtxf *matrix);
void matrix_4x4_set_position_and_rotation_around_y(float *position,
                                                   float angle, Mtxf *matrix);
void matrix_4x4_set_rotation_around_xyz(coord3d *angles, Mtxf *matrix);
void matrix_4x4_get_rotation_around_xyz(Mtxf *matrix, coord3d *angles);
void matrix_4x4_set_position_and_rotation_around_xyz(coord3d *position,
                                                     coord3d *rotation,
                                                     Mtxf *matrix);
void matrix_4x4_set_basis_and_position_target(
    Mtxf *matrix, float pos_x, float pos_y, float pos_z,
    float target_x, float target_y, float target_z,
    float up_x, float up_y, float up_z);
void matrix_4x4_set_lookat(
    Mtxf *matrix, float eye_x, float eye_y, float eye_z,
    float forward_x, float forward_y, float forward_z,
    float up_x, float up_y, float up_z);
void matrix_4x4_set_lookat_target(
    Mtxf *matrix, float eye_x, float eye_y, float eye_z,
    float target_x, float target_y, float target_z,
    float up_x, float up_y, float up_z);
void mtx4RotateVecInPlace(Mtxf *matrix, coord3d *vector);
void matrix_4x4_transform_vector(Mtxf *matrix, coord3d *vector,
                                 coord3d *result);
void matrix_4x4_multiply(Mtxf *lhs, Mtxf *rhs, Mtxf *result);
void matrix_4x4_multiply_in_place(Mtxf *lhs, Mtxf *rhs);
void matrix_4x4_multiply_homogeneous(Mtxf *lhs, Mtxf *rhs, Mtxf *result);
void matrix_4x4_multiply_homogeneous_in_place(Mtxf *lhs, Mtxf *rhs);
void mtx4TransformVecInPlace(Mtxf *matrix, coord3d *vector);
void matrix_4x4_7F058274(Mtxf *lhs, Mtxf *rhs, Mtxf *result);
void matrix_4x4_set_rotation_inverse(Mtxf *rotation, Mtxf *transpose);
void matrix_4x4_invert_affine(Mtxf *matrix, Mtxf *result);
void matrix_4x4_invert(Mtxf *matrix, Mtxf *result);
void matrix_4x4_calc_adjugate(Mtxf *matrix, Mtxf *result);
float matrix_4x4_determinant(Mtxf *matrix);
float matrix_3x3_determinant(float a, float d, float g, float b, float e,
                             float h, float c, float f, float i);
float matrix_2x2_determinant(float a, float c, float b, float d);
void matrix_4x4_normalize_rotation(Mtxf *matrix, Mtxf *result);
void matrix_4x4_set_inverse_rotation_and_translation(Mtxf *matrix,
                                                      Mtxf *result);
void matrix_4x4_set_rotation_axis_angle(Mtxf *matrix, float angle,
                                        float x, float y, float z);
void matrix_4x4_align(Mtxf *matrix, float angle, float x, float y, float z);
void matrix_scalar_multiply(float scalar, float *matrix);
void matrix_scalar_multiply_2(float scalar, float *matrix);
void matrix_scalar_multiply_3(float scalar, float *matrix);
void matrix_column_1_scalar_multiply(float scalar, float *matrix);
void matrix_column_2_scalar_multiply(float scalar, float *matrix);
void matrix_column_3_scalar_multiply(float scalar, float *matrix);
void matrix_column_3_scalar_multiply_2(float scalar, float *matrix);
void matrix_row_3_scalar_multiply(float scalar, float *matrix);
uint32_t matrix_4x4_calc_depth_scale(float near_value, float far_value);
void matrix_4x4_set_projection(Mtxf *matrix, uint16_t *depth_scale,
                               float fovy, float aspect, float near_value,
                               float far_value, float scale);
void matrix_4x4_f32_to_s32(float mf[4][4], int32_t ms[4][4]);
void sub_GAME_7F058E78(Mtxf *src, Mtxf *dst);
void matrix_4x4_7F059044(Mtxf *arg0, Mtx *arg1);
void matrix_4x4_7F05914C(Mtx *arg0, Mtxf *arg1);
void sub_GAME_7F059244(Mtx *src, Mtx *dst);
void sub_GAME_7F059334(Mtx *src, Mtx *dst);
void matrix_4x4_7F059D30(Mtxf *matrix);
void matrix_4x4_7F058C4C(float arg0);
void matrix_4x4_7F058C64(void);
void matrix_4x4_7F058C88(void);
void vec3Lerp(coord3d *x, coord3d *y, float scaler, coord3d *result);
float scalarCatmullRomInterp(float p0, float p1, float p2, float p3,
                             float fraction);
void coord3dCubicSplineInterp(coord3d *prev, coord3d *start, coord3d *end,
                              coord3d *next, float fraction,
                              float tangent_scale, coord3d *result);
void coord3dCatmullRomInterp(coord3d *p0, coord3d *p1, coord3d *p2,
                             coord3d *p3, float fraction, float tangent_scale,
                             coord3d *result);
float scalarCubicHermiteInterp(float start, float end, float start_tangent,
                               float end_tangent, float fraction);
}

namespace
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = kPi * 0.5f;
constexpr float kEpsilon = 0.0002f;

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
        if (std::fabs(actual - expected) > epsilon)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s (actual=%g expected=%g)\n",
                         message, actual, expected);
        }
    }
};

bool finite_matrix(const Mtxf &matrix)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(matrix.m[column][row]))
            {
                return false;
            }
        }
    }
    return true;
}

void expect_matrix(TestState &tests, const Mtxf &actual, const Mtxf &expected,
                   const char *message, float epsilon = kEpsilon)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            char detail[160];
            std::snprintf(detail, sizeof(detail),
                          "%s at [%d][%d]", message, column, row);
            tests.near(actual.m[column][row], expected.m[column][row], detail,
                       epsilon);
        }
    }
}

void expect_identity(TestState &tests, const Mtxf &matrix, const char *message,
                     float epsilon = kEpsilon)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            char detail[160];
            std::snprintf(detail, sizeof(detail),
                          "%s at [%d][%d]", message, column, row);
            tests.near(matrix.m[column][row], column == row ? 1.0f : 0.0f,
                       detail, epsilon);
        }
    }
}

coord3d transform(const Mtxf &matrix, float x, float y, float z)
{
    coord3d input = {};
    input.x = x;
    input.y = y;
    input.z = z;
    coord3d output = {};
    matrix_4x4_transform_vector(const_cast<Mtxf *>(&matrix), &input, &output);
    return output;
}

coord3d rotate(const Mtxf &matrix, float x, float y, float z)
{
    coord3d output = {};
    output.x = x;
    output.y = y;
    output.z = z;
    mtx4RotateVecInPlace(const_cast<Mtxf *>(&matrix), &output);
    return output;
}

void expect_coord(TestState &tests, const coord3d &actual, float x, float y,
                  float z, const char *message, float epsilon = kEpsilon)
{
    char detail[160];
    std::snprintf(detail, sizeof(detail), "%s.x", message);
    tests.near(actual.x, x, detail, epsilon);
    std::snprintf(detail, sizeof(detail), "%s.y", message);
    tests.near(actual.y, y, detail, epsilon);
    std::snprintf(detail, sizeof(detail), "%s.z", message);
    tests.near(actual.z, z, detail, epsilon);
}

Mtxf sample_affine()
{
    Mtxf matrix = {};
    float position[] = {4.0f, -2.0f, 7.0f};
    matrix_4x4_set_position_and_rotation_around_y(position, 0.37f, &matrix);
    return matrix;
}

Mtxf sample_general()
{
    Mtxf matrix = {{
        {2.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 3.0f, 0.0f, 0.0f},
        {4.0f, 5.0f, 5.0f, 0.0f},
        {6.0f, 7.0f, 8.0f, 7.0f},
    }};
    return matrix;
}

void test_identity_copy_and_position(TestState &tests)
{
    Mtxf matrix = {};
    matrix_4x4_set_identity(&matrix);
    expect_identity(tests, matrix, "identity matrix");

    coord3d position = {};
    position.x = 11.0f;
    position.y = -3.0f;
    position.z = 29.0f;
    matrix_4x4_set_position(&position, &matrix);
    tests.near(matrix.m[3][0], 11.0f, "set_position x");
    tests.near(matrix.m[3][1], -3.0f, "set_position y");
    tests.near(matrix.m[3][2], 29.0f, "set_position z");
    tests.near(matrix.m[0][0], 1.0f, "set_position preserves rotation");
    tests.near(matrix.m[1][1], 1.0f, "set_position preserves rotation y");
    tests.near(matrix.m[2][2], 1.0f, "set_position preserves rotation z");

    Mtxf copy = {};
    matrix_4x4_copy(&matrix, &copy);
    expect_matrix(tests, copy, matrix, "matrix copy");

    Mtxf positioned = {};
    matrix_4x4_set_identity_and_position(&position, &positioned);
    expect_matrix(tests, positioned, matrix, "identity and position");
}

void test_axis_rotations_and_transforms(TestState &tests)
{
    Mtxf matrix = {};
    coord3d output;

    matrix_4x4_set_rotation_around_x(kHalfPi, &matrix);
    output = transform(matrix, 0.0f, 1.0f, 0.0f);
    expect_coord(tests, output, 0.0f, 0.0f, 1.0f,
                 "positive X rotation transforms +Y");

    matrix_4x4_set_rotation_around_y(kHalfPi, &matrix);
    output = transform(matrix, 1.0f, 0.0f, 0.0f);
    expect_coord(tests, output, 0.0f, 0.0f, -1.0f,
                 "positive Y rotation transforms +X");

    matrix_4x4_set_rotation_around_z(kHalfPi, &matrix);
    output = transform(matrix, 1.0f, 0.0f, 0.0f);
    expect_coord(tests, output, 0.0f, 1.0f, 0.0f,
                 "positive Z rotation transforms +X");

    float position[] = {3.0f, 4.0f, 5.0f};
    matrix_4x4_set_position_and_rotation_around_y(position, kHalfPi, &matrix);
    output = transform(matrix, 1.0f, 0.0f, 0.0f);
    expect_coord(tests, output, 3.0f, 4.0f, 4.0f,
                 "position and Y rotation transform");
    output = {{1.0f, 0.0f, 0.0f}};
    mtx4TransformVecInPlace(&matrix, &output);
    expect_coord(tests, output, 3.0f, 4.0f, 4.0f,
                 "in-place transform applies rotation and translation");
    tests.expect(finite_matrix(matrix), "axis rotation matrix is finite");
}

void test_target_basis_and_lookat_orientation(TestState &tests)
{
    Mtxf basis = {};
    matrix_4x4_set_basis_and_position_target(
        &basis, 4.0f, 2.0f, 3.0f, 4.0f, 2.0f, 8.0f,
        0.0f, 1.0f, 0.0f);

    /* GoldenEye's model convention uses local -Z as the forward sample. The
     * basis helper negates the target vector, so this is the exact invariant
     * used by setupCctv: the camera's local forward sample points at the pad. */
    coord3d camera_forward = rotate(basis, 0.0f, 0.0f, -1.0f);
    expect_coord(tests, camera_forward, 0.0f, 0.0f, 1.0f,
                 "CCTV target basis points local forward at the look pad");
    tests.near(basis.m[3][0], 4.0f, "target basis position x");
    tests.near(basis.m[3][1], 2.0f, "target basis position y");
    tests.near(basis.m[3][2], 3.0f, "target basis position z");

    Mtxf lookat = {};
    matrix_4x4_set_lookat_target(
        &lookat, 10.0f, 2.0f, 5.0f, 10.0f, 2.0f, 10.0f,
        0.0f, 1.0f, 0.0f);
    coord3d eye_in_view = transform(lookat, 10.0f, 2.0f, 5.0f);
    expect_coord(tests, eye_in_view, 0.0f, 0.0f, 0.0f,
                 "lookat maps the eye to the origin");
    tests.expect(finite_matrix(lookat), "lookat matrix is finite");

    Mtxf degenerate = {};
    matrix_4x4_set_basis_and_position_target(
        &degenerate, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f);
    tests.expect(finite_matrix(degenerate),
                 "coincident target does not create a NaN matrix");

    Mtxf vertical = {};
    matrix_4x4_set_basis_and_position_target(
        &vertical, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f,
        0.0f, 1.0f, 0.0f);
    expect_coord(tests, rotate(vertical, 0.0f, 0.0f, -1.0f),
                 0.0f, 1.0f, 0.0f,
                 "parallel up vector selects a stable fallback");
    tests.expect(finite_matrix(vertical),
                 "parallel up vector produces a finite basis");

    Mtxf horizontal = {};
    matrix_4x4_set_basis_and_position_target(
        &horizontal, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f,
        0.0f, 0.0f, 1.0f);
    expect_coord(tests, rotate(horizontal, 0.0f, 0.0f, -1.0f),
                 0.0f, 0.0f, 1.0f,
                 "parallel horizontal up selects a stable fallback");
    tests.expect(finite_matrix(horizontal),
                 "parallel horizontal up produces a finite basis");

    Mtxf invalid = {};
    matrix_4x4_set_lookat(
        &invalid, 2.0f, 3.0f, 4.0f,
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f);
    tests.expect(finite_matrix(invalid),
                 "non-finite lookat direction falls back safely");

    Mtxf infinite = {};
    matrix_4x4_set_lookat(
        &infinite, 0.0f, 0.0f, 0.0f,
        std::numeric_limits<float>::infinity(), 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f);
    tests.expect(finite_matrix(infinite),
                 "infinite lookat direction falls back safely");
}

void test_multiplication_and_in_place_variants(TestState &tests)
{
    Mtxf lhs = {};
    Mtxf rhs = {};
    float lhs_position[] = {4.0f, 2.0f, -3.0f};
    float rhs_position[] = {-1.0f, 5.0f, 7.0f};
    matrix_4x4_set_position_and_rotation_around_y(
        lhs_position, 0.25f, &lhs);
    matrix_4x4_set_rotation_around_z(-0.4f, &rhs);
    coord3d rhs_position_coord = {{rhs_position[0], rhs_position[1],
                                   rhs_position[2]}};
    matrix_4x4_set_position(&rhs_position_coord, &rhs);

    Mtxf product = {};
    matrix_4x4_multiply(&lhs, &rhs, &product);
    const coord3d input = {{2.0f, -1.0f, 3.0f}};
    coord3d first = transform(rhs, input.x, input.y, input.z);
    first = transform(lhs, first.x, first.y, first.z);
    const coord3d combined = transform(product, input.x, input.y, input.z);
    expect_coord(tests, combined, first.x, first.y, first.z,
                 "full matrix multiplication composes transforms");

    Mtxf in_place = rhs;
    matrix_4x4_multiply_in_place(&lhs, &in_place);
    expect_matrix(tests, in_place, product, "in-place matrix multiplication");

    Mtxf homogeneous = {};
    matrix_4x4_multiply_homogeneous(&lhs, &rhs, &homogeneous);
    expect_matrix(tests, homogeneous, product,
                  "homogeneous affine multiplication", 0.0003f);

    Mtxf homogeneous_in_place = rhs;
    matrix_4x4_multiply_homogeneous_in_place(&lhs, &homogeneous_in_place);
    expect_matrix(tests, homogeneous_in_place, product,
                  "in-place homogeneous affine multiplication", 0.0003f);
}

void test_legacy_component_product(TestState &tests)
{
    Mtxf lhs = {};
    matrix_4x4_set_identity(&lhs);
    lhs.m[0][0] = 2.0f;
    lhs.m[1][1] = 3.0f;
    lhs.m[2][2] = 4.0f;
    lhs.m[2][3] = 5.0f;
    lhs.m[3][2] = 6.0f;

    Mtxf rhs = {};
    matrix_4x4_set_identity(&rhs);
    Mtxf result = {};
    matrix_4x4_7F058274(&lhs, &rhs, &result);

    const Mtxf expected = {{
        {2.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 3.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 4.0f, 5.0f},
        {0.0f, 0.0f, 6.0f, 0.0f},
    }};
    expect_matrix(tests, result, expected,
                  "legacy component product preserves its N64 layout");
}

void test_euler_roundtrip_and_inverse(TestState &tests)
{
    coord3d angles = {{0.23f, -0.41f, 0.67f}};
    Mtxf rotation = {};
    matrix_4x4_set_rotation_around_xyz(&angles, &rotation);
    coord3d recovered = {};
    matrix_4x4_get_rotation_around_xyz(&rotation, &recovered);
    expect_coord(tests, recovered, angles.x, angles.y, angles.z,
                 "Euler rotation round trip", 0.0004f);

    coord3d gimbal_angles = {{0.0f, kHalfPi, 0.0f}};
    matrix_4x4_set_rotation_around_xyz(&gimbal_angles, &rotation);
    matrix_4x4_get_rotation_around_xyz(&rotation, &recovered);
    tests.near(recovered.x, 0.0f, "gimbal-lock extraction chooses zero roll");
    tests.near(recovered.y, kHalfPi, "gimbal-lock extraction keeps pitch");
    tests.near(recovered.z, 0.0f, "gimbal-lock extraction keeps yaw");

    coord3d position = {{8.0f, -6.0f, 3.0f}};
    Mtxf affine = {};
    matrix_4x4_set_position_and_rotation_around_xyz(
        &position, &angles, &affine);
    Mtxf inverse = {};
    matrix_4x4_invert_affine(&affine, &inverse);
    Mtxf identity = {};
    matrix_4x4_multiply(&affine, &inverse, &identity);
    expect_identity(tests, identity, "affine inverse product", 0.0005f);

    Mtxf general = sample_general();
    Mtxf general_inverse = {};
    matrix_4x4_invert(&general, &general_inverse);
    matrix_4x4_multiply(&general, &general_inverse, &identity);
    expect_identity(tests, identity, "general inverse product", 0.001f);

    Mtxf adjugate = {};
    matrix_4x4_calc_adjugate(&general, &adjugate);
    const float determinant = matrix_4x4_determinant(&general);
    matrix_4x4_multiply(&general, &adjugate, &identity);
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            char message[160];
            std::snprintf(message, sizeof(message),
                          "adjugate product at [%d][%d]", column, row);
            tests.near(identity.m[column][row],
                       column == row ? determinant : 0.0f, message,
                       0.02f);
        }
    }

    Mtxf singular = {};
    Mtxf safe_inverse = {};
    matrix_4x4_set_inverse_rotation_and_translation(&singular, &safe_inverse);
    tests.expect(finite_matrix(safe_inverse),
                 "singular rotation inverse does not create NaNs");
    expect_identity(tests, safe_inverse,
                    "singular rotation inverse falls back to identity");

    matrix_4x4_invert_affine(&singular, &safe_inverse);
    tests.expect(finite_matrix(safe_inverse),
                 "singular affine inverse does not create NaNs");
    expect_identity(tests, safe_inverse,
                    "singular affine inverse falls back to identity");

    matrix_4x4_invert(&singular, &safe_inverse);
    tests.expect(finite_matrix(safe_inverse),
                 "singular general inverse does not create NaNs");
    expect_identity(tests, safe_inverse,
                    "singular general inverse falls back to identity");
}

void test_rotation_normalization_and_axis_helpers(TestState &tests)
{
    Mtxf scaled = {};
    matrix_4x4_set_identity(&scaled);
    scaled.m[0][0] = 2.0f;
    scaled.m[1][1] = 2.0f;
    scaled.m[2][2] = 2.0f;
    Mtxf normalized = {};
    matrix_4x4_normalize_rotation(&scaled, &normalized);
    expect_identity(tests, normalized,
                    "normalize_rotation returns unit rotation");

    Mtxf inverse_rotation = {};
    matrix_4x4_set_inverse_rotation_and_translation(&scaled,
                                                     &inverse_rotation);
    Mtxf identity = {};
    matrix_4x4_multiply(&scaled, &inverse_rotation, &identity);
    expect_identity(tests, identity,
                    "inverse rotation and translation handles uniform scale",
                    0.0005f);

    Mtxf rotation = {};
    Mtxf transposed = {};
    matrix_4x4_set_rotation_around_y(0.37f, &rotation);
    matrix_4x4_set_rotation_inverse(&rotation, &transposed);
    matrix_4x4_multiply(&rotation, &transposed, &identity);
    expect_identity(tests, identity, "rotation transpose inverse", 0.0005f);

    Mtxf zero_scale = {};
    matrix_4x4_set_identity(&zero_scale);
    zero_scale.m[0][0] = 0.0f;
    matrix_4x4_normalize_rotation(&zero_scale, &normalized);
    expect_identity(tests, normalized,
                    "normalize_rotation handles a zero-length basis");

    Mtxf axis = {};
    matrix_4x4_set_rotation_axis_angle(&axis, kHalfPi, 1.0f, 0.0f, 0.0f);
    tests.expect(finite_matrix(axis), "axis-angle X matrix is finite");
    const coord3d axis_result = transform(axis, 0.0f, 1.0f, 0.0f);
    expect_coord(tests, axis_result, 1.0f, 0.0f, 0.0f,
                 "legacy axis-angle direction basis");

    Mtxf zero_axis = {};
    matrix_4x4_set_rotation_axis_angle(&zero_axis, 1.0f, 0.0f, 0.0f, 0.0f);
    expect_identity(tests, zero_axis, "zero axis falls back to identity");

    Mtxf aligned = {};
    matrix_4x4_align(&aligned, 0.0f, 1.0f, 0.0f, 0.0f);
    tests.expect(finite_matrix(aligned), "align matrix is finite");
}

void test_scaling_helpers(TestState &tests)
{
    auto numbered = [] {
        Mtxf matrix = {};
        float value = 1.0f;
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                matrix.m[column][row] = value++;
            }
        }
        return matrix;
    };

    Mtxf scalar = numbered();
    matrix_scalar_multiply(2.0f, scalar.m[0]);
    for (int i = 0; i < 12; ++i)
    {
        char message[100];
        std::snprintf(message, sizeof(message),
                      "scalar multiply element %d", i);
        tests.near(reinterpret_cast<float *>(scalar.m)[i],
                   static_cast<float>((i + 1) * 2), message);
    }
    for (int i = 12; i < 16; ++i)
    {
        char message[100];
        std::snprintf(message, sizeof(message),
                      "scalar multiply leaves element %d", i);
        tests.near(reinterpret_cast<float *>(scalar.m)[i],
                   static_cast<float>(i + 1), message);
    }

    Mtxf columns = numbered();
    matrix_column_1_scalar_multiply(2.0f, columns.m[0]);
    matrix_column_2_scalar_multiply(3.0f, columns.m[0]);
    matrix_column_3_scalar_multiply(4.0f, columns.m[0]);
    tests.near(columns.m[0][0], 2.0f, "column 1 helper");
    tests.near(columns.m[0][1], 4.0f, "column 1 helper preserves offset");
    tests.near(columns.m[1][0], 15.0f, "column 2 helper");
    tests.near(columns.m[1][1], 18.0f, "column 2 helper");
    tests.near(columns.m[2][0], 36.0f, "column 3 helper");
    tests.near(columns.m[2][3], 48.0f, "column 3 helper includes fourth value");

    Mtxf columns_without_position = numbered();
    matrix_column_3_scalar_multiply_2(4.0f, columns_without_position.m[0]);
    tests.near(columns_without_position.m[2][0], 36.0f,
               "column 3 no-position helper");
    tests.near(columns_without_position.m[2][3], 12.0f,
               "column 3 no-position leaves fourth value");

    Mtxf scalar2 = numbered();
    matrix_scalar_multiply_2(2.0f, scalar2.m[0]);
    tests.near(scalar2.m[0][0], 2.0f, "scalar2 first column");
    tests.near(scalar2.m[1][0], 10.0f, "scalar2 second column");
    tests.near(scalar2.m[2][0], 18.0f, "scalar2 third column");
    tests.near(scalar2.m[0][3], 4.0f, "scalar2 leaves homogeneous row");

    Mtxf row = numbered();
    matrix_row_3_scalar_multiply(3.0f, row.m[0]);
    tests.near(row.m[0][2], 9.0f, "row 3 helper first row");
    tests.near(row.m[1][2], 21.0f, "row 3 helper second row");
    tests.near(row.m[2][2], 33.0f, "row 3 helper third row");
    tests.near(row.m[3][2], 45.0f, "row 3 helper fourth row");

    Mtxf scalar3 = numbered();
    matrix_scalar_multiply_3(2.0f, scalar3.m[0]);
    tests.near(scalar3.m[0][0], 2.0f, "scalar3 first column");
    tests.near(scalar3.m[0][1], 4.0f, "scalar3 second row");
    tests.near(scalar3.m[0][2], 6.0f, "scalar3 third row");
    tests.near(scalar3.m[0][3], 4.0f, "scalar3 leaves row fourth column");
    tests.near(scalar3.m[3][0], 26.0f, "scalar3 preserves fourth column");
}

void test_projection_depth_and_fixed_point_helpers(TestState &tests)
{
    tests.expect(matrix_4x4_calc_depth_scale(0.0f, 0.0f) == 0xffff,
                 "depth scale clamps the near-zero range");
    tests.expect(matrix_4x4_calc_depth_scale(1.0f, 1.0f) == 0xffff,
                 "depth scale includes the sum-two boundary");
    tests.expect(matrix_4x4_calc_depth_scale(1.0f, 2.0f) == 0xaaaa,
                 "depth scale computes the normal range");
    tests.expect(matrix_4x4_calc_depth_scale(1.0e9f, 1.0e9f) == 1,
                 "depth scale clamps an underflowed result");

    Mtxf projection = {};
    uint16_t depth_scale = 0;
    matrix_4x4_set_projection(&projection, &depth_scale, kHalfPi, 2.0f,
                              1.0f, 100.0f, 1.0f);
    tests.near(projection.m[0][0], 32768.0f, "projection horizontal scale");
    tests.near(projection.m[1][1], 65536.0f, "projection vertical scale");
    tests.near(projection.m[2][3], -65536.0f, "projection homogeneous term");
    tests.expect(depth_scale == matrix_4x4_calc_depth_scale(1.0f, 100.0f),
                 "projection returns depth scale");
    matrix_4x4_set_projection(&projection, NULL, kHalfPi, 2.0f,
                              1.0f, 100.0f, 1.0f);

    Mtxf source = {{
        {0.5f, -0.25f, 1.0f, 0.0f},
        {1.25f, -1.5f, 0.125f, 0.0f},
        {2.0f, -2.25f, 0.75f, 0.0f},
        {3.0f, -3.5f, 1.5f, 1.0f},
    }};
    source.m[0][0] = 0.12345f;
    Mtx packed = {};
    const float original_first_value = source.m[0][0];
    matrix_4x4_f32_to_s32(source.m, packed.m);
    Mtxf unpacked = {};
    sub_GAME_7F058E78(reinterpret_cast<Mtxf *>(&packed), &unpacked);
    expect_matrix(tests, unpacked, source, "packed fixed-point round trip",
                  1.0f / 65536.0f + 0.00001f);

    Mtx fixed = {};
    matrix_4x4_7F059044(&source, &fixed);
    Mtxf from_fixed = {};
    matrix_4x4_7F05914C(&fixed, &from_fixed);
    expect_matrix(tests, from_fixed, source, "Mtx fixed conversion");

    Mtxf copied = {};
    matrix_4x4_7F058C4C(0.5f);
    matrix_4x4_f32_to_s32(source.m, packed.m);
    sub_GAME_7F058E78(reinterpret_cast<Mtxf *>(&packed), &copied);
    tests.expect(std::fabs(copied.m[0][0] - original_first_value) > 0.000001f,
                 "fixed-point scale override changes packed precision");

    Mtx word_stream = {};
    Mtx planar_stream = {};
    Mtx word_round_trip = {};
    for (int i = 0; i < 16; ++i)
    {
        reinterpret_cast<uint32_t *>(word_stream.m)[i] =
            0x10203040U + static_cast<uint32_t>(i) * 0x01010101U;
    }
    sub_GAME_7F059244(&word_stream, &planar_stream);
    sub_GAME_7F059334(&planar_stream, &word_round_trip);
    for (int i = 0; i < 16; ++i)
    {
        char message[120];
        std::snprintf(message, sizeof(message),
                      "fixed-point word permutation round trip %d", i);
        tests.expect(
            reinterpret_cast<uint32_t *>(word_round_trip.m)[i] ==
                reinterpret_cast<uint32_t *>(word_stream.m)[i],
            message);
    }

    matrix_4x4_7F059D30(&source);
    matrix_4x4_7F058C64();
    matrix_4x4_7F058C88();
    matrix_4x4_7F058C4C(1.0f);
}

void test_determinants_and_interpolation(TestState &tests)
{
    tests.near(matrix_2x2_determinant(2.0f, 3.0f, 5.0f, 7.0f), -1.0f,
               "2x2 determinant");
    tests.near(matrix_3x3_determinant(
                   1.0f, 2.0f, 3.0f,
                   0.0f, 4.0f, 5.0f,
                   1.0f, 0.0f, 6.0f),
               22.0f, "3x3 determinant");

    Mtxf identity = {};
    matrix_4x4_set_identity(&identity);
    tests.near(matrix_4x4_determinant(&identity), 1.0f,
               "4x4 identity determinant");
    Mtxf general = sample_general();
    tests.near(matrix_4x4_determinant(&general), 210.0f,
               "4x4 general determinant");

    coord3d first = {{0.0f, 10.0f, -2.0f}};
    coord3d second = {{10.0f, 0.0f, 8.0f}};
    coord3d result = {};
    vec3Lerp(&first, &second, 0.0f, &result);
    expect_coord(tests, result, first.x, first.y, first.z,
                 "vector lerp start");
    vec3Lerp(&first, &second, 0.5f, &result);
    expect_coord(tests, result, 5.0f, 5.0f, 3.0f, "vector lerp midpoint");
    vec3Lerp(&first, &second, 1.0f, &result);
    expect_coord(tests, result, second.x, second.y, second.z,
                 "vector lerp end");

    /* The scalar forms are part of the same game-side math module. Exercise
     * both interpolation boundaries and extrapolation, not only the vector
     * wrappers used by the current camera code. */
    tests.near(scalarCatmullRomInterp(-2.0f, 0.0f, 10.0f, 12.0f, 0.0f),
               0.0f, "scalar Catmull-Rom start");
    tests.near(scalarCatmullRomInterp(-2.0f, 0.0f, 10.0f, 12.0f, 0.5f),
               5.0f, "scalar Catmull-Rom midpoint");
    tests.near(scalarCatmullRomInterp(-2.0f, 0.0f, 10.0f, 12.0f, 1.0f),
               10.0f, "scalar Catmull-Rom end");
    tests.near(scalarCatmullRomInterp(-2.0f, 0.0f, 10.0f, 12.0f, -0.5f),
               1.0f, "scalar Catmull-Rom lower extrapolation");
    tests.near(scalarCatmullRomInterp(-2.0f, 0.0f, 10.0f, 12.0f, 1.5f),
               9.0f, "scalar Catmull-Rom upper extrapolation");

    tests.near(scalarCubicHermiteInterp(0.0f, 10.0f, 2.0f, 4.0f, 0.0f),
               0.0f, "scalar Hermite start");
    tests.near(scalarCubicHermiteInterp(0.0f, 10.0f, 2.0f, 4.0f, 0.5f),
               4.75f, "scalar Hermite midpoint");
    tests.near(scalarCubicHermiteInterp(0.0f, 10.0f, 2.0f, 4.0f, 1.0f),
               10.0f, "scalar Hermite end");

    coord3d p0 = {{-1.0f, -1.0f, -1.0f}};
    coord3d p1 = {{0.0f, 2.0f, 4.0f}};
    coord3d p2 = {{10.0f, 4.0f, 8.0f}};
    coord3d p3 = {{11.0f, 1.0f, 9.0f}};
    coord3d spline = {};
    coord3dCatmullRomInterp(&p0, &p1, &p2, &p3, 0.0f, 0.5f, &spline);
    expect_coord(tests, spline, p1.x, p1.y, p1.z,
                 "Catmull-Rom interpolation start");
    coord3dCatmullRomInterp(&p0, &p1, &p2, &p3, 1.0f, 0.5f, &spline);
    expect_coord(tests, spline, p2.x, p2.y, p2.z,
                 "Catmull-Rom interpolation end");

    coord3dCubicSplineInterp(&p0, &p1, &p2, &p3, 0.0f, 0.5f, &spline);
    expect_coord(tests, spline, p1.x, p1.y, p1.z,
                 "cubic spline interpolation start");
    coord3dCubicSplineInterp(&p0, &p1, &p2, &p3, 1.0f, 0.5f, &spline);
    expect_coord(tests, spline, p2.x, p2.y, p2.z,
                 "cubic spline interpolation end");
}

} // namespace

int main()
{
    TestState tests;
    test_identity_copy_and_position(tests);
    test_axis_rotations_and_transforms(tests);
    test_target_basis_and_lookat_orientation(tests);
    test_multiplication_and_in_place_variants(tests);
    test_legacy_component_product(tests);
    test_euler_roundtrip_and_inverse(tests);
    test_rotation_normalization_and_axis_helpers(tests);
    test_scaling_helpers(tests);
    test_projection_depth_and_fixed_point_helpers(tests);
    test_determinants_and_interpolation(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d matrix-math contract(s) failed\n",
                     tests.failures);
        return 1;
    }
    std::puts("matrix-math unit suite passed");
    return 0;
}
