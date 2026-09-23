#include "../port/fast3d/bump_mapping_policy.h"

#include <cassert>
#include <cmath>
#include <cstdlib>

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

static void expectFallback(GeBumpVec3 normal)
{
    assert(geBumpVectorIsFinite(normal));
    assert(near(normal.x, 0.0f));
    assert(near(normal.y, 0.0f));
    assert(near(normal.z, 1.0f));
}

static void expectOrthonormal(const GeBumpTangentFrame &frame)
{
    assert(frame.valid != 0);
    assert(near(geBumpVectorLength(frame.normal), 1.0f));
    assert(near(geBumpVectorLength(frame.tangent), 1.0f));
    assert(near(geBumpVectorLength(frame.bitangent), 1.0f));
    assert(near(geBumpDot(frame.normal, frame.tangent), 0.0f));
    assert(near(geBumpDot(frame.normal, frame.bitangent), 0.0f));
    assert(near(geBumpDot(frame.tangent, frame.bitangent), 0.0f));
}

int main()
{
    const GeBumpHeightCross flat = {0.5f, 0.5f, 0.5f, 0.5f};
    const GeBumpVec3 flat_normal = geBumpNormalFromHeight(flat, 2.5f);
    expectFallback(flat_normal);

    const GeBumpHeightCross horizontal_ridge = {1.0f, 0.0f, 0.5f, 0.5f};
    const GeBumpVec3 horizontal_normal =
        geBumpNormalFromHeight(horizontal_ridge, 2.5f);
    assert(horizontal_normal.x > 0.0f);
    assert(near(horizontal_normal.y, 0.0f));
    assert(horizontal_normal.z > 0.0f);
    assert(near(geBumpVectorLength(horizontal_normal), 1.0f));

    const GeBumpHeightCross vertical_ridge = {0.5f, 0.5f, 1.0f, 0.0f};
    const GeBumpVec3 vertical_normal =
        geBumpNormalFromHeight(vertical_ridge, 2.5f);
    assert(vertical_normal.y > 0.0f);
    assert(vertical_normal.x == 0.0f);
    assert(near(geBumpVectorLength(vertical_normal), 1.0f));

    const GeBumpHeightCross reverse_horizontal = {0.0f, 1.0f, 0.5f, 0.5f};
    const GeBumpVec3 reverse_horizontal_normal =
        geBumpNormalFromHeight(reverse_horizontal, 2.5f);
    assert(reverse_horizontal_normal.x < 0.0f);

    const GeBumpHeightCross reverse_vertical = {0.5f, 0.5f, 0.0f, 1.0f};
    const GeBumpVec3 reverse_vertical_normal =
        geBumpNormalFromHeight(reverse_vertical, 2.5f);
    assert(reverse_vertical_normal.y < 0.0f);

    expectFallback(geBumpNormalFromHeight(horizontal_ridge, 0.0f));

    const GeBumpVec3 stronger_normal =
        geBumpNormalFromHeight(horizontal_ridge,
                               GE_BUMP_WATER_NORMAL_STRENGTH_REMASTER);
    assert(stronger_normal.x > horizontal_normal.x);
    assert(stronger_normal.z < horizontal_normal.z);

    expectFallback(geBumpNormalFromHeight(
        {__builtin_nanf(""), 0.0f, 0.0f, 0.0f}, 2.5f));
    expectFallback(geBumpNormalFromHeight(
        {0.0f, __builtin_inff(), 0.0f, 0.0f}, 2.5f));
    expectFallback(geBumpNormalFromHeight(flat, -1.0f));
    expectFallback(geBumpNormalFromHeight(flat, __builtin_nanf("")));
    expectFallback(geBumpNormalFromHeight(
        {3.0e38f, 0.0f, 0.0f, 0.0f}, 1.0f));
    expectFallback(geBumpNormalFromHeight(
        {3.4e38f, -3.4e38f, 0.0f, 0.0f}, 1.0f));
    expectFallback(geBumpNormalFromHeight(
        {0.0f, 0.0f, 3.4e38f, -3.4e38f}, 1.0f));
    expectFallback(geBumpNormalFromHeight(
        {__builtin_nanf(""), __builtin_nanf(""),
         __builtin_nanf(""), __builtin_nanf(" ")},
        2.5f));

    const GeBumpTangentFrame frame = geBumpBuildTangentFrame(
        {0.0f, 0.0f, 1.0f}, {1.0f, 0.2f, 0.0f}, {0.1f, 1.0f, 0.0f},
        1.0f);
    expectOrthonormal(frame);
    assert(frame.handedness > 0.0f);

    const GeBumpTangentFrame mirrored = geBumpBuildTangentFrame(
        {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        -1.0f);
    expectOrthonormal(mirrored);
    assert(mirrored.handedness < 0.0f);
    assert(mirrored.bitangent.y < 0.0f);

    const GeBumpTangentFrame degenerate = geBumpBuildTangentFrame(
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
        1.0f);
    assert(degenerate.valid == 0);
    expectFallback(degenerate.normal);

    const GeBumpTangentFrame parallel_tangent = geBumpBuildTangentFrame(
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        1.0f);
    assert(parallel_tangent.valid == 0);
    expectFallback(parallel_tangent.normal);

    assert(GE_BUMP_WATER_NORMAL_STRENGTH_ENHANCED > 0.0f);
    assert(GE_BUMP_WATER_NORMAL_STRENGTH_REMASTER >
           GE_BUMP_WATER_NORMAL_STRENGTH_ENHANCED);

    assert(near(geBumpWaterNormalStrength(-1), 0.0f));
    assert(near(geBumpWaterNormalStrength(0), 0.0f));
    assert(near(geBumpWaterNormalStrength(1),
                GE_BUMP_WATER_NORMAL_STRENGTH_ENHANCED));
    assert(near(geBumpWaterNormalStrength(2),
                GE_BUMP_WATER_NORMAL_STRENGTH_REMASTER));
    assert(near(geBumpWaterNormalStrength(99),
                GE_BUMP_WATER_NORMAL_STRENGTH_REMASTER));

    return 0;
}
