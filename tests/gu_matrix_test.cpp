/* Deterministic tests for the renderer-facing libultra matrix constructors. */
#include <cmath>
#include <cstdint>
#include <cstdio>

struct Mtx
{
    std::int32_t m[4][4];
};

extern "C" {
void guRotateF(float matrix[4][4], float angle, float x, float y, float z);
void guRotate(Mtx *matrix, float angle, float x, float y, float z);
void guScaleF(float matrix[4][4], float x, float y, float z);
void guScale(Mtx *matrix, float x, float y, float z);
void guTranslateF(float matrix[4][4], float x, float y, float z);
void guTranslate(Mtx *matrix, float x, float y, float z);
void guOrthoF(float matrix[4][4], float left, float right, float bottom,
             float top, float near_plane, float far_plane, float scale);
void guOrtho(Mtx *matrix, float left, float right, float bottom, float top,
             float near_plane, float far_plane, float scale);
void guPerspectiveF(float matrix[4][4], std::uint16_t *persp_norm,
                    float fovy, float aspect, float near_plane,
                    float far_plane, float scale);
void guPerspective(Mtx *matrix, std::uint16_t *persp_norm, float fovy,
                   float aspect, float near_plane, float far_plane,
                   float scale);
void guLookAtF(float matrix[4][4], float eye_x, float eye_y, float eye_z,
               float at_x, float at_y, float at_z, float up_x, float up_y,
               float up_z);
void guLookAt(Mtx *matrix, float eye_x, float eye_y, float eye_z, float at_x,
              float at_y, float at_z, float up_x, float up_y, float up_z);
void guAlignF(float matrix[4][4], float angle, float x, float y, float z);
void guAlign(Mtx *matrix, float angle, float x, float y, float z);
void guMtxF2L(float matrix[4][4], Mtx *packed);
void guMtxL2F(float matrix[4][4], Mtx *packed);
void guMtxIdent(Mtx *packed);
signed short sins(unsigned short angle);
signed short coss(unsigned short angle);
}

namespace
{

constexpr float kPi = 3.14159265358979323846f;
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

void test_basic_constructors(TestState &tests)
{
    float matrix[4][4] = {};
    guScaleF(matrix, 2.0f, 3.0f, 4.0f);
    tests.near(matrix[0][0], 2.0f, "scale x");
    tests.near(matrix[1][1], 3.0f, "scale y");
    tests.near(matrix[2][2], 4.0f, "scale z");
    tests.near(matrix[3][3], 1.0f, "scale homogeneous coordinate");

    guTranslateF(matrix, 11.0f, -7.0f, 5.0f);
    tests.near(matrix[3][0], 11.0f, "translation x");
    tests.near(matrix[3][1], -7.0f, "translation y");
    tests.near(matrix[3][2], 5.0f, "translation z");
    tests.near(matrix[0][0], 1.0f, "translation keeps identity x basis");
    tests.near(matrix[3][3], 1.0f, "translation homogeneous coordinate");

    Mtx packed = {};
    guScale(&packed, 2.0f, 3.0f, 4.0f);
    guMtxL2F(matrix, &packed);
    tests.near(matrix[0][0], 2.0f, "packed scale x");
    tests.near(matrix[1][1], 3.0f, "packed scale y");
    tests.near(matrix[2][2], 4.0f, "packed scale z");

    guTranslate(&packed, 11.0f, -7.0f, 5.0f);
    guMtxL2F(matrix, &packed);
    tests.near(matrix[3][0], 11.0f, "packed translation x");
    tests.near(matrix[3][1], -7.0f, "packed translation y");
    tests.near(matrix[3][2], 5.0f, "packed translation z");

    guMtxIdent(&packed);
    guMtxL2F(matrix, &packed);
    tests.near(matrix[0][0], 1.0f, "packed identity diagonal");
    tests.near(matrix[0][1], 0.0f, "packed identity off-diagonal");
}

void test_rotation_and_orthographic_projection(TestState &tests)
{
    float matrix[4][4] = {};
    guRotateF(matrix, 90.0f, 0.0f, 0.0f, 1.0f);
    tests.near(matrix[0][0], 0.0f, "Z rotation cosine", 0.001f);
    tests.near(matrix[0][1], 1.0f, "Z rotation positive sine", 0.001f);
    tests.near(matrix[1][0], -1.0f, "Z rotation negative sine", 0.001f);
    tests.near(matrix[1][1], 0.0f, "Z rotation second cosine", 0.001f);
    tests.near(matrix[2][2], 1.0f, "Z rotation preserves Z", 0.001f);
    tests.near(matrix[3][3], 1.0f, "Z rotation homogeneous coordinate");

    Mtx packed = {};
    guRotate(&packed, 90.0f, 0.0f, 0.0f, 1.0f);
    guMtxL2F(matrix, &packed);
    tests.near(matrix[0][1], 1.0f, "packed Z rotation sine", 0.001f);

    guAlignF(matrix, 0.0f, 1.0f, 0.0f, 0.0f);
    tests.near(matrix[2][2], 0.0f, "align rotates a non-vertical axis");
    guAlignF(matrix, 0.0f, 0.0f, 1.0f, 0.0f);
    tests.near(matrix[0][0], 1.0f, "align vertical-axis fallback is finite");
    guAlign(&packed, 0.0f, 1.0f, 0.0f, 0.0f);
    guMtxL2F(matrix, &packed);
    tests.near(matrix[3][3], 1.0f, "packed align homogeneous coordinate");

    guOrthoF(matrix, -1.0f, 1.0f, -2.0f, 2.0f, 1.0f, 11.0f, 2.0f);
    tests.near(matrix[0][0], 2.0f, "orthographic X scale");
    tests.near(matrix[1][1], 1.0f, "orthographic Y scale");
    tests.near(matrix[2][2], -0.4f, "orthographic depth scale");
    tests.near(matrix[3][2], -2.4f, "orthographic depth translation");
    tests.near(matrix[3][3], 2.0f, "orthographic scale factor");
}

void test_perspective_and_lookat(TestState &tests)
{
    float matrix[4][4] = {};
    std::uint16_t persp_norm = 0;
    guPerspectiveF(matrix, &persp_norm, 90.0f, 1.0f, 1.0f, 100.0f, 1.0f);
    tests.near(matrix[0][0], 1.0f, "perspective horizontal cotangent", 0.002f);
    tests.near(matrix[1][1], 1.0f, "perspective vertical cotangent", 0.002f);
    tests.near(matrix[2][2], -101.0f / 99.0f,
               "perspective depth scale", 0.002f);
    tests.near(matrix[2][3], -1.0f, "perspective projective term");
    tests.near(matrix[3][2], -200.0f / 99.0f,
               "perspective depth translation", 0.002f);
    tests.expect(persp_norm == static_cast<std::uint16_t>(131072.0 / 101.0),
                 "perspective normalization value");

    guPerspectiveF(matrix, nullptr, 90.0f, 1.0f, 0.5f, 1.0f, 2.0f);
    tests.near(matrix[3][3], 0.0f, "perspective homogeneous zero");
    guPerspectiveF(matrix, &persp_norm, 90.0f, 1.0f, 0.5f, 1.0f, 2.0f);
    tests.expect(persp_norm == 0xFFFF,
                 "near perspective uses saturated normalization");

    Mtx packed = {};
    guOrtho(&packed, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 3.0f, 1.0f);
    guMtxL2F(matrix, &packed);
    tests.near(matrix[0][0], 1.0f, "packed orthographic scale");
    guPerspective(&packed, nullptr, 90.0f, 1.0f, 1.0f, 10.0f, 1.0f);
    guMtxL2F(matrix, &packed);
    tests.near(matrix[2][3], -1.0f, "packed perspective projective term");

    guLookAtF(matrix, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,
              0.0f);
    tests.near(matrix[0][0], 1.0f, "look-at right basis");
    tests.near(matrix[1][1], 1.0f, "look-at up basis");
    tests.near(matrix[2][2], 1.0f, "look-at forward basis");
    tests.near(matrix[3][3], 1.0f, "look-at homogeneous coordinate");

    guLookAt(&packed, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,
             0.0f);
    guMtxL2F(matrix, &packed);
    tests.near(matrix[2][2], 1.0f, "packed look-at forward basis");
}

void test_lookup_trigonometry(TestState &tests)
{
    tests.expect(sins(0x0000) == 0, "N64 sine table zero");
    tests.expect(sins(0x4000) > 32700, "N64 sine table positive quarter turn");
    tests.expect(sins(0x8000) == 0, "N64 sine table half turn");
    tests.expect(sins(0xC000) < -32700, "N64 sine table negative quarter turn");
    tests.expect(coss(0x0000) > 32700, "N64 cosine table zero");
    tests.expect(std::abs(coss(0x4000)) < 100, "N64 cosine table quarter turn");
    tests.expect(coss(0x8000) < -32700, "N64 cosine table half turn");
}

} // namespace

int main()
{
    TestState tests;
    test_basic_constructors(tests);
    test_rotation_and_orthographic_projection(tests);
    test_perspective_and_lookat(tests);
    test_lookup_trigonometry(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d libultra matrix contract(s) failed\n",
                     tests.failures);
        return 1;
    }
    std::puts("libultra matrix unit suite passed");
    return 0;
}
