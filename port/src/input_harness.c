#include "input_harness.h"
#include "controller_mapping.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

/* The N64 stdlib shim intentionally exposes only the original libc surface. */
extern long strtol(const char *value, char **end, int base);

#define GE_INPUT_MAX_CONTROLLERS 4

int geInputFakeControllerCount(const char *value)
{
    char *end = NULL;
    long requested;
    const char *cursor;

    if (value == NULL || *value == '\0') {
        return 0;
    }

    for (cursor = value; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return 0;
        }
    }

    errno = 0;
    requested = strtol(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' || requested < 1) {
        return 0;
    }

    if (requested > GE_INPUT_MAX_CONTROLLERS) {
        requested = GE_INPUT_MAX_CONTROLLERS;
    }
    return (int)requested;
}

unsigned geInputApplyMouseButtons(unsigned button,
                                  unsigned mouse_buttons,
                                  int mouse_grabbed,
                                  int menu_mode,
                                  int absolute_aim_suspended)
{
    if (!mouse_grabbed && !menu_mode && !absolute_aim_suspended)
    {
        mouse_buttons = 0;
    }

    if (menu_mode)
    {
        /* Keyboard fire/aim are also swallowed by the front end. */
        button &= ~(GE_CONT_G | GE_CONT_R);
        if (mouse_buttons & GE_INPUT_MOUSE_LEFT)
        {
            button |= GE_CONT_A;
        }
        if (mouse_buttons & GE_INPUT_MOUSE_RIGHT)
        {
            button |= GE_CONT_B;
        }
        return button;
    }

    if (mouse_buttons & GE_INPUT_MOUSE_LEFT)
    {
        button |= GE_CONT_G;
    }
    if (mouse_buttons & GE_INPUT_MOUSE_RIGHT)
    {
        button |= GE_CONT_R;
    }
    return button;
}
