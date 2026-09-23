#include "../port/fast3d/fast3d_normal_policy.h"

#include <cassert>
#include <cmath>
#include <cstdlib>

static void require(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

#undef assert
#define assert(condition) require((condition))

static bool close(float actual, float expected)
{
    return std::fabs(actual - expected) < 0.0001f;
}

int main()
{
    const float identity[4][4] = {
        {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1},
    };
    float transform[3][3] = {};
    assert(geFast3dBuildNormalTransform(identity, transform) != 0);
    assert(close(transform[0][0], 1.0f) && close(transform[1][1], 1.0f) &&
           close(transform[2][2], 1.0f));
    assert(geFast3dBuildNormalTransform(identity, nullptr) == 0);

    const float non_uniform_scale[4][4] = {
        {2, 0, 0, 0}, {0, 3, 0, 0}, {0, 0, 4, 0}, {0, 0, 0, 1},
    };
    assert(geFast3dBuildNormalTransform(non_uniform_scale, transform) != 0);
    assert(close(transform[0][0], 0.5f) &&
           close(transform[1][1], 1.0f / 3.0f) &&
           close(transform[2][2], 0.25f));

    GeFast3dNormalVec3 transformed = {};
    assert(geFast3dTransformNormal(&transformed, {0, 0, 1},
                                   non_uniform_scale) != 0);
    assert(close(transformed.x, 0.0f) && close(transformed.y, 0.0f) &&
           close(transformed.z, 1.0f));

    assert(geFast3dTransformNormal(&transformed, {0, 0, 0}, identity) == 0);
    assert(close(transformed.x, 0.0f) && close(transformed.y, 0.0f) &&
           close(transformed.z, 1.0f));

    const float singular[4][4] = {
        {1, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1},
    };
    assert(geFast3dBuildNormalTransform(singular, transform) == 0);
    assert(geFast3dTransformNormal(&transformed, {1, 0, 0}, singular) == 0);
    assert(close(transformed.x, 0.0f) && close(transformed.y, 0.0f) &&
           close(transformed.z, 1.0f));

    float malformed[4][4] = {};
    malformed[0][0] = __builtin_nanf("");
    assert(geFast3dBuildNormalTransform(malformed, transform) == 0);

    const float overflow_transform[4][4] = {
        {2.0e-44f, 0, 0, 0}, {0, 1.0e19f, 0, 0},
        {0, 0, 1.0e19f, 0}, {0, 0, 0, 1},
    };
    assert(geFast3dBuildNormalTransform(overflow_transform, transform) == 0);

    assert(geFast3dTransformNormal(nullptr, {0, 0, 1}, identity) == 0);
    return 0;
}
