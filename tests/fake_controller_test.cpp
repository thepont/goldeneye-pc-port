#include "input_harness.h"

#include <cstdio>
#include <string>

static int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

int main()
{
    if (geInputFakeControllerCount(nullptr) != 0 ||
        geInputFakeControllerCount("") != 0 ||
        geInputFakeControllerCount("garbage") != 0 ||
        geInputFakeControllerCount("0") != 0 ||
        geInputFakeControllerCount("-1") != 0 ||
        geInputFakeControllerCount("+2") != 0 ||
        geInputFakeControllerCount(" 2") != 0 ||
        geInputFakeControllerCount("2 ") != 0 ||
        geInputFakeControllerCount("1x") != 0) {
        return fail("fake-controller opt-in contract failed");
    }

    if (geInputFakeControllerCount("1") != 1 ||
        geInputFakeControllerCount("2") != 2 ||
        geInputFakeControllerCount("4") != 4 ||
        geInputFakeControllerCount("5") != 4 ||
        geInputFakeControllerCount("999") != 4) {
        return fail("fake-controller bounds contract failed");
    }

    for (int requested = -10; requested <= 10; ++requested)
    {
        const std::string value = std::to_string(requested);
        const int expected = requested < 1 ? 0 : requested > 4 ? 4 : requested;
        if (geInputFakeControllerCount(value.c_str()) != expected)
        {
            return fail("fake-controller numeric boundary matrix failed");
        }
    }

    if (geInputFakeControllerCount("00") != 0 ||
        geInputFakeControllerCount("01") != 1 ||
        geInputFakeControllerCount("0002") != 2)
    {
        return fail("fake-controller leading-zero contract failed");
    }

    const char *malformed[] = {
        "2.0", "2\n", "2\t", " 2", "2 ", "+1", "-0", "-2", "1e2",
        "0x2",
    };
    for (const char *value : malformed)
    {
        if (geInputFakeControllerCount(value) != 0)
        {
            return fail("fake-controller malformed-input matrix failed");
        }
    }

    return 0;
}
