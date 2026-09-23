#include <cmath>
#include <cstdint>
#include <cstdio>

extern "C" {
typedef union
{
    float m[4][4];
    int32_t unused;
} Mtxf;

void matrix_4x4_set_rotation_axis_angle(Mtxf *matrix, float angle,
                                         float x, float y, float z);
}

int main()
{
    /* gunfire.c passes angle=0 to this legacy helper to build a basis whose
     * local forward column points along the muzzle direction. */
    Mtxf matrix = {};
    const float x = 1.0f;
    const float y = 2.0f;
    const float z = 3.0f;
    const float inverse_length = 1.0f / std::sqrt(x * x + y * y + z * z);
    matrix_4x4_set_rotation_axis_angle(&matrix, 0.0f, x, y, z);

    const bool aligned =
        std::fabs(matrix.m[0][2] + x * inverse_length) < 0.0002f &&
        std::fabs(matrix.m[1][2] + y * inverse_length) < 0.0002f &&
        std::fabs(matrix.m[2][2] + z * inverse_length) < 0.0002f;
    if (!aligned) {
        std::fprintf(stderr,
                     "muzzle-flash axis helper lost the legacy direction basis\n");
        return 1;
    }
    return 0;
}
