#include "input_harness.h"

#include <cstdio>

static int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

int main()
{
    constexpr unsigned fire = 0x2000u;
    constexpr unsigned aim = 0x0010u;
    constexpr unsigned action = 0x8000u;
    constexpr unsigned cancel = 0x4000u;

    if (geInputApplyMouseButtons(0, GE_INPUT_MOUSE_RIGHT, 1, 0, 0) != aim ||
        geInputApplyMouseButtons(0, GE_INPUT_MOUSE_LEFT, 1, 0, 0) != fire ||
        geInputApplyMouseButtons(0, GE_INPUT_MOUSE_LEFT | GE_INPUT_MOUSE_RIGHT,
                                 1, 0, 0) != (fire | aim)) {
        return fail("captured mouse buttons must map LMB->fire and RMB->aim");
    }

    if (geInputApplyMouseButtons(0, GE_INPUT_MOUSE_RIGHT, 0, 0, 0) != 0 ||
        geInputApplyMouseButtons(fire, GE_INPUT_MOUSE_RIGHT, 0, 0, 0) != fire) {
        return fail("uncaptured stage mouse buttons must be suppressed");
    }

    if (geInputApplyMouseButtons(fire | aim, GE_INPUT_MOUSE_LEFT |
                                 GE_INPUT_MOUSE_RIGHT, 0, 1, 0) !=
            (action | cancel) ||
        geInputApplyMouseButtons(0, GE_INPUT_MOUSE_RIGHT, 0, 1, 0) != cancel ||
        geInputApplyMouseButtons(0, GE_INPUT_MOUSE_LEFT, 0, 1, 0) != action) {
        return fail("menu mouse buttons must map to action/cancel, not fire/aim");
    }

    if (geInputApplyMouseButtons(0, GE_INPUT_MOUSE_RIGHT, 0, 0, 1) != aim) {
        return fail("absolute-aim suspension must preserve intentional RMB input");
    }

    if (geInputApplyMouseButtons(0, GE_INPUT_MOUSE_LEFT |
                                 GE_INPUT_MOUSE_RIGHT, 0, 0, 1) !=
            (fire | aim) ||
        geInputApplyMouseButtons(action, 0, 0, 1, 0) != action ||
        geInputApplyMouseButtons(cancel, 0, 0, 1, 0) != cancel) {
        return fail("mouse policy must preserve unrelated buttons at boundaries");
    }

    const unsigned base_masks[] = {
        0u, fire, aim, fire | aim, action, cancel, action | cancel, 0xffffu,
    };
    const int truth_values[] = {0, 1, -1, 2};
    unsigned cases = 0;
    for (unsigned base : base_masks)
    {
        for (unsigned mouse = 0; mouse < 8; ++mouse)
        {
            for (int grabbed : truth_values)
            {
                for (int menu : truth_values)
                {
                    for (int suspended : truth_values)
                    {
                        unsigned expected = base;
                        unsigned effective_mouse = mouse;
                        if (!grabbed && !menu && !suspended)
                        {
                            effective_mouse = 0;
                        }

                        if (menu)
                        {
                            expected &= ~(fire | aim);
                            if (effective_mouse & GE_INPUT_MOUSE_LEFT)
                            {
                                expected |= action;
                            }
                            if (effective_mouse & GE_INPUT_MOUSE_RIGHT)
                            {
                                expected |= cancel;
                            }
                        }
                        else
                        {
                            if (effective_mouse & GE_INPUT_MOUSE_LEFT)
                            {
                                expected |= fire;
                            }
                            if (effective_mouse & GE_INPUT_MOUSE_RIGHT)
                            {
                                expected |= aim;
                            }
                        }

                        const unsigned actual = geInputApplyMouseButtons(
                            base, mouse, grabbed, menu, suspended);
                        if (actual != expected ||
                            geInputApplyMouseButtons(actual, mouse, grabbed,
                                                      menu, suspended) != actual)
                        {
                            return fail("exhaustive mouse policy matrix failed");
                        }
                        ++cases;
                    }
                }
            }
        }
    }
    if (cases != 4096u)
    {
        return fail("mouse policy matrix did not execute all boundary cases");
    }

    return 0;
}
