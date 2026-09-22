#include "input_harness.h"

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
