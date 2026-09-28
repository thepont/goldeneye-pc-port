#include "../port/fast3d/texture_wrap_policy.h"

#include <iostream>

namespace {

int failures;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_animated_water_window_does_not_underflow()
{
    expect(!geFast3dTileRectangleValid(16, 0, 0, 0),
           "an S window ending before its animated origin must be invalid");
    expect(!geFast3dTileRectangleValid(0, 0, 16, 0),
           "a T window ending before its animated origin must be invalid");
    expect(geFast3dTileExtent(16, 0) == 0,
           "an invalid animated window must have no derived extent");
}

void test_unset_full_image_window_is_not_a_subtile()
{
    expect(!geFast3dUseSubtileWrap(32, 1, 0, 0, 5),
           "Dam's unset 32-texel water window must retain full-image wrapping");
    expect(!geFast3dUseSubtileWrap(64, 1, 0, 0, 6),
           "an unset 64-texel window must retain full-image wrapping");
}

void test_explicit_windows_remain_distinguishable()
{
    expect(geFast3dTileRectangleValid(0, 0, 0, 0),
           "a one-point tile rectangle is valid");
    expect(geFast3dTileExtent(0, 0) == 1,
           "a one-point tile rectangle spans one texel");
    expect(geFast3dTileExtent(4, 64) == 16,
           "an explicit U10.2 window reports its inclusive texel extent");
    expect(geFast3dUseSubtileWrap(32, 16, 0, 60, 5),
           "an explicit half-image window must use sub-tile wrapping");
    expect(!geFast3dUseSubtileWrap(32, 32, 0, 124, 5),
           "a full-image window must not use sub-tile wrapping");
}

} // namespace

int main()
{
    test_animated_water_window_does_not_underflow();
    test_unset_full_image_window_is_not_a_subtile();
    test_explicit_windows_remain_distinguishable();

    if (failures != 0) {
        std::cerr << failures << " texture-wrap contract(s) failed\n";
        return 1;
    }
    std::cout << "texture-wrap policy suite passed\n";
    return 0;
}
