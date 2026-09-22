#include "input_harness.h"

#include <cstdio>

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

    return 0;
}
