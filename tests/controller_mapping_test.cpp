#include "controller_mapping.h"

#include <cstdio>
#include <initializer_list>

static int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

static GeControllerDigitalState stateFor(unsigned bits)
{
    GeControllerDigitalState state = {};
    state.a_pressed = (bits >> 0) & 1u;
    state.x_pressed = (bits >> 1) & 1u;
    state.b_pressed = (bits >> 2) & 1u;
    state.y_pressed = (bits >> 3) & 1u;
    state.right_trigger_active = (bits >> 4) & 1u;
    state.left_trigger_active = (bits >> 5) & 1u;
    state.left_shoulder_rising = (bits >> 6) & 1u;
    state.right_shoulder_rising = (bits >> 7) & 1u;
    state.start_pressed = (bits >> 8) & 1u;
    state.dpad_up_pressed = (bits >> 9) & 1u;
    state.dpad_down_pressed = (bits >> 10) & 1u;
    state.dpad_left_pressed = (bits >> 11) & 1u;
    state.dpad_right_pressed = (bits >> 12) & 1u;
    return state;
}

static unsigned expectedFor(GeControllerDigitalState state, int menu_mode)
{
    unsigned expected = 0;
    if (state.a_pressed || state.x_pressed) expected |= GE_CONT_A;
    if (state.b_pressed || state.y_pressed) expected |= GE_CONT_B;
    if (state.right_trigger_active) expected |= GE_CONT_G;
    if (state.left_trigger_active) expected |= GE_CONT_R;
    if (!menu_mode && state.right_shoulder_rising) expected |= GE_CONT_A;
    if (!menu_mode && state.left_shoulder_rising) expected |= GE_CONT_A | GE_CONT_G;
    if (state.start_pressed) expected |= GE_CONT_START;
    if (state.dpad_up_pressed) expected |= GE_CONT_UP;
    if (state.dpad_down_pressed) expected |= GE_CONT_DOWN;
    if (state.dpad_left_pressed) expected |= GE_CONT_LEFT;
    if (state.dpad_right_pressed) expected |= GE_CONT_RIGHT;
    return expected;
}

static int expectedScaled(int value, int deadzone, int output_max)
{
    if (deadzone < 0) deadzone = 0;
    if (deadzone > 30000) deadzone = 30000;
    if (output_max < 0) output_max = -output_max;
    if (value > -deadzone && value < deadzone) return 0;
    if (value < 0) value += deadzone;
    else value -= deadzone;
    long scaled = static_cast<long>(value) * output_max / (32767 - deadzone);
    if (scaled > output_max) scaled = output_max;
    if (scaled < -output_max) scaled = -output_max;
    return static_cast<int>(scaled);
}

int main()
{
    unsigned cases = 0;
    for (unsigned bits = 0; bits < (1u << 13); ++bits)
    {
        const GeControllerDigitalState state = stateFor(bits);
        for (int menu_mode : {0, 1, -1, 2})
        {
            const unsigned actual = geControllerMapDigitalButtons(state, menu_mode);
            if (actual != expectedFor(state, menu_mode) ||
                geControllerMapDigitalButtons(state, menu_mode) != actual)
            {
                return fail("exhaustive SDL controller button mapping failed");
            }
            ++cases;
        }
    }

    if (cases != (1u << 15))
    {
        return fail("controller mapping matrix did not execute all cases");
    }

    unsigned long long axis_cases = 0;
    const int thresholds[] = {-2, 0, 1, 16384, 32767, 40000};
    for (int value = -32768; value <= 32767; ++value)
    {
        for (int threshold : thresholds)
        {
            for (int positive : {0, 1, -1, 2})
            {
                const bool expected = positive ? value > (threshold < 0 ? 0 : threshold)
                                               : value < -(threshold < 0 ? 0 : threshold);
                if (geControllerAxisPastThreshold(value, threshold, positive) != expected)
                {
                    return fail("controller stick threshold matrix failed");
                }
                ++axis_cases;
            }
            for (int trigger_value : {-32768, -1, 0, threshold, 32766, 32767})
            {
                if (geControllerTriggerIsActive(trigger_value, threshold) !=
                    (trigger_value > threshold))
                {
                    return fail("controller trigger threshold matrix failed");
                }
                ++axis_cases;
            }
        }
    }

    const int deadzones[] = {-1, 0, 1, 7000, 30000, 30001};
    const int output_limits[] = {-80, 0, 1, 80, 127};
    for (int value = -32768; value <= 32767; ++value)
    {
        for (int deadzone : deadzones)
        {
            for (int output_max : output_limits)
            {
                if (geControllerScaleAxis(value, deadzone, output_max) !=
                    expectedScaled(value, deadzone, output_max))
                {
                    return fail("controller analog scaling matrix failed");
                }
                ++axis_cases;
            }
        }
    }

    if (axis_cases < 3000000ULL)
    {
        return fail("controller axis matrix did not execute enough cases");
    }

    return 0;
}
