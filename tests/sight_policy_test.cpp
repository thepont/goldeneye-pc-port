#include "sight_policy.h"

#include <cstdio>
#include <initializer_list>

static int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

int main()
{
    if (!geSightIsAiming(0) || geSightIsAiming(GE_SIGHT_REASON_NOTAIMING) ||
        !geSightIsAiming(0x01) || geSightIsAiming(0x03)) {
        return fail("gunsight aiming bit policy failed");
    }

    if (!geSightShouldDraw(0, 0) || geSightShouldDraw(0, 1) ||
        geSightShouldDraw(0, 2) ||
        geSightShouldDraw(0x01, 0) || geSightShouldDraw(0x02, 0) ||
        geSightShouldDraw(0x03, 0) || geSightShouldDraw(0x10, 0)) {
        return fail("gunsight draw policy failed");
    }

    unsigned cases = 0;
    for (int mode = 0; mode <= 0xff; ++mode)
    {
        for (int menu : {0, 1, -1, 2})
        {
            const bool expected_aiming = (mode & GE_SIGHT_REASON_NOTAIMING) == 0;
            const bool expected_draw = mode == 0 && !menu;
            if (geSightIsAiming(mode) != expected_aiming ||
                geSightShouldDraw(mode, menu) != expected_draw)
            {
                return fail("exhaustive gunsight policy matrix failed");
            }
            ++cases;
        }
    }
    if (cases != 1024u)
    {
        return fail("gunsight policy matrix did not execute all cases");
    }

    return 0;
}
