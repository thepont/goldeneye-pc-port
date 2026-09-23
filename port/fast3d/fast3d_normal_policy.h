#ifndef GE007_FAST3D_NORMAL_POLICY_H
#define GE007_FAST3D_NORMAL_POLICY_H

#include <stdint.h>

typedef struct GeFast3dNormalVec3 {
    float x;
    float y;
    float z;
} GeFast3dNormalVec3;

static inline int geFast3dNormalFinite(float value)
{
    return __builtin_isfinite(value) != 0;
}

static inline int geFast3dBuildNormalTransform(const float matrix[4][4],
                                               float output[3][3])
{
    if (matrix == 0 || output == 0) {
        return 0;
    }
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (!geFast3dNormalFinite(matrix[row][column])) {
                return 0;
            }
        }
    }

    const float a00 = matrix[0][0], a01 = matrix[0][1], a02 = matrix[0][2];
    const float a10 = matrix[1][0], a11 = matrix[1][1], a12 = matrix[1][2];
    const float a20 = matrix[2][0], a21 = matrix[2][1], a22 = matrix[2][2];
    const float determinant = a00 * (a11 * a22 - a12 * a21)
                            - a01 * (a10 * a22 - a12 * a20)
                            + a02 * (a10 * a21 - a11 * a20);
    if (!geFast3dNormalFinite(determinant) ||
        (determinant > -0.000001f && determinant < 0.000001f)) {
        return 0;
    }

    /* Positions use row vectors in the Fast3D PC path. Their normal transform
     * is therefore the inverse of the upper-left model-view 3x3 (equivalent
     * to inverse-transpose in the conventional column-vector formulation). */
    output[0][0] = (a11 * a22 - a12 * a21) / determinant;
    output[0][1] = (a02 * a21 - a01 * a22) / determinant;
    output[0][2] = (a01 * a12 - a02 * a11) / determinant;
    output[1][0] = (a12 * a20 - a10 * a22) / determinant;
    output[1][1] = (a00 * a22 - a02 * a20) / determinant;
    output[1][2] = (a02 * a10 - a00 * a12) / determinant;
    output[2][0] = (a10 * a21 - a11 * a20) / determinant;
    output[2][1] = (a01 * a20 - a00 * a21) / determinant;
    output[2][2] = (a00 * a11 - a01 * a10) / determinant;

    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (!geFast3dNormalFinite(output[row][column])) {
                return 0;
            }
        }
    }
    return 1;
}

static inline int geFast3dTransformNormal(GeFast3dNormalVec3 *output,
                                          GeFast3dNormalVec3 input,
                                          const float matrix[4][4])
{
    if (output == 0 || !geFast3dNormalFinite(input.x) ||
        !geFast3dNormalFinite(input.y) || !geFast3dNormalFinite(input.z)) {
        return 0;
    }

    float transform[3][3] = {};
    if (!geFast3dBuildNormalTransform(matrix, transform)) {
        output->x = 0.0f;
        output->y = 0.0f;
        output->z = 1.0f;
        return 0;
    }

    output->x = input.x * transform[0][0] + input.y * transform[1][0] +
                input.z * transform[2][0];
    output->y = input.x * transform[0][1] + input.y * transform[1][1] +
                input.z * transform[2][1];
    output->z = input.x * transform[0][2] + input.y * transform[1][2] +
                input.z * transform[2][2];
    const float length = __builtin_sqrtf(output->x * output->x +
                                         output->y * output->y +
                                         output->z * output->z);
    if (!geFast3dNormalFinite(length) || length <= 0.000001f) {
        output->x = 0.0f;
        output->y = 0.0f;
        output->z = 1.0f;
        return 0;
    }
    output->x /= length;
    output->y /= length;
    output->z /= length;
    return 1;
}

#endif /* GE007_FAST3D_NORMAL_POLICY_H */
