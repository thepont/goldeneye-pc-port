#include "../port/fast3d/vector_text_route_policy.h"

#include <cstdio>

static int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

int main()
{
    if (geVectorTextCanReplaceBitmap(0, "Agent briefing") != 0 ||
        geVectorTextCanReplaceBitmap(1, nullptr) != 0 ||
        geVectorTextCanReplaceBitmap(1, "") != 0) {
        return fail("vector briefing route accepted disabled or empty text");
    }
    if (geVectorTextCanReplaceBitmap(1, "Agent briefing\nQ branch") != 1 ||
        geVectorTextCanReplaceBitmap(1, "0123456789 !?.,-()") != 1) {
        return fail("vector briefing route rejected supported ASCII text");
    }

    const char high_bit[] = "Agent briefing\xC3\xA9";
    const char control[] = "Agent briefing\rtext";
    if (geVectorTextCanReplaceBitmap(1, high_bit) != 0 ||
        geVectorTextCanReplaceBitmap(1, control) != 0) {
        return fail("vector briefing route accepted unsupported text");
    }

    if (geVectorTextMeasureHeight("objective", 15) != 0 ||
        geVectorTextMeasureHeight("objective\n", 15) != 15 ||
        geVectorTextMeasureHeight("objective\nsecond line\n", 15) != 30 ||
        geVectorTextMeasureHeight(nullptr, 15) != 0 ||
        geVectorTextMeasureHeight("objective\n", 0) != 0) {
        return fail("vector briefing route did not preserve legacy newline height");
    }

    return 0;
}
