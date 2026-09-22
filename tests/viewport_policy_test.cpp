#include "viewport_policy.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

int main()
{
    constexpr float screen = 240.0f;

    const float split_boundaries[] = {
        -1.0f, 0.0f, 1.0f, 1.0001f, 119.999f, 179.999f,
        180.0f, 180.001f, 239.999f, 240.0f, 480.0f,
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
    };
    for (float height : split_boundaries)
    {
        const bool expected = height > 1.0f && height < screen * 0.75f;
        if (geViewportIsSplit(height, screen) != expected)
        {
            return fail("split viewport boundary classification failed");
        }
    }

    const float invalid_screens[] = {
        -1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(),
    };
    for (float invalid_screen : invalid_screens)
    {
        if (geViewportIsSplit(100.0f, invalid_screen))
        {
            return fail("invalid screen height must not create a split viewport");
        }
    }

    struct RectCase {
        int first_y;
        int first_height;
        int second_y;
        int second_height;
        bool overlap;
    };
    const RectCase cases[] = {
        {10, 109, 121, 109, false},
        {0, 100, 100, 100, false},
        {0, 101, 100, 100, true},
        {100, 100, 0, 100, false},
        {100, 100, 50, 100, true},
        {-20, 20, 0, 1, false},
        {-20, 21, 0, 1, true},
        {0, 0, 0, 100, false},
        {0, 100, 0, 0, false},
        {0, -1, 0, 100, false},
        {std::numeric_limits<int>::max(), 1,
         std::numeric_limits<int>::max(), 1, true},
        {std::numeric_limits<int>::min(), 1,
         std::numeric_limits<int>::max(), 1, false},
    };
    for (const RectCase &test : cases)
    {
        if (geViewportRectsOverlap(test.first_y, test.first_height,
                                    test.second_y, test.second_height) !=
            test.overlap)
        {
            return fail("viewport rectangle overlap boundary failed");
        }
    }

    return 0;
}
