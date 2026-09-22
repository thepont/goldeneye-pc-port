#include "controller_mapping.h"

int geControllerAxisPastThreshold(int value, int threshold, int positive_direction)
{
    if (threshold < 0)
    {
        threshold = 0;
    }
    return positive_direction ? value > threshold : value < -threshold;
}

int geControllerTriggerIsActive(int value, int threshold)
{
    return value > threshold;
}

int geControllerScaleAxis(int value, int deadzone, int output_max)
{
    long scaled;

    if (deadzone < 0)
    {
        deadzone = 0;
    }
    if (deadzone > 30000)
    {
        deadzone = 30000;
    }
    if (output_max < 0)
    {
        output_max = -output_max;
    }
    if (value > -deadzone && value < deadzone)
    {
        return 0;
    }

    if (value < 0)
    {
        value += deadzone;
    }
    else
    {
        value -= deadzone;
    }
    scaled = (long)value * output_max / (32767 - deadzone);
    if (scaled > output_max)
    {
        scaled = output_max;
    }
    if (scaled < -output_max)
    {
        scaled = -output_max;
    }
    return (int)scaled;
}

unsigned geControllerMapDigitalButtons(GeControllerDigitalState state,
                                       int menu_mode)
{
    unsigned button = 0;

    if (state.a_pressed || state.x_pressed)
    {
        button |= GE_CONT_A;
    }
    if (state.b_pressed || state.y_pressed)
    {
        button |= GE_CONT_B;
    }
    if (state.right_trigger_active)
    {
        button |= GE_CONT_G;
    }
    if (state.left_trigger_active)
    {
        button |= GE_CONT_R;
    }

    /* Shoulder weapon-cycle edges are gameplay-only. LB is the backwards
     * A+Z edge; RB is the forwards A edge. */
    if (!menu_mode && state.right_shoulder_rising)
    {
        button |= GE_CONT_A;
    }
    if (!menu_mode && state.left_shoulder_rising)
    {
        button |= GE_CONT_A | GE_CONT_G;
    }

    if (state.start_pressed)
    {
        button |= GE_CONT_START;
    }
    if (state.dpad_up_pressed)
    {
        button |= GE_CONT_UP;
    }
    if (state.dpad_down_pressed)
    {
        button |= GE_CONT_DOWN;
    }
    if (state.dpad_left_pressed)
    {
        button |= GE_CONT_LEFT;
    }
    if (state.dpad_right_pressed)
    {
        button |= GE_CONT_RIGHT;
    }

    return button;
}
