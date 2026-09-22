#include <cstdio>
#include <limits>

#include "coop.h"
#include <bondconstants.h>

static int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

int main()
{
    if (geCoopParseMode(nullptr, 1, 2, 3, -1) != -1 ||
        geCoopParseMode("", 1, 2, 3, -1) != -1 ||
        geCoopParseMode("solo", 1, 2, 3, -1) != 1 ||
        geCoopParseMode("multi", 1, 2, 3, -1) != 2 ||
        geCoopParseMode("coop", 1, 2, 3, -1) != 3 ||
        geCoopParseMode("COOP", 1, 2, 3, -1) != -1 ||
        geCoopParseMode("garbage", 1, 2, 3, -1) != -1) {
        return fail("co-op session-mode parser contract failed");
    }

    if (static_cast<int>(PROP_NONE) != -1 ||
        static_cast<int>(PROP_ALARM1) != 0 ||
        static_cast<int>(PROP_MAX) <= static_cast<int>(PROP_ALARM1)) {
        return fail("co-op held-item PROP sentinel contract failed");
    }

    if (geCoopPlayerCount(-1) != 0 || geCoopPlayerCount(0) != 0 || geCoopPlayerCount(1) != 0 ||
        geCoopPlayerCount(2) != 2 || geCoopPlayerCount(4) != 2) {
        return fail("co-op controller-count contract failed");
    }

    if (geCoopResumeMode(3, 2, 1, 2, 3) != 3 ||
        geCoopResumeMode(3, 1, 1, 2, 3) != 1 ||
        geCoopResumeMode(3, 0, 1, 2, 3) != 1 ||
        geCoopResumeMode(1, 1, 1, 2, 3) != 1 ||
        geCoopResumeMode(2, 2, 1, 2, 3) != 2 ||
        geCoopResumeMode(-1, 2, 1, 2, 3) != 1) {
        return fail("co-op resume-mode contract failed");
    }

    if (!geCoopSpawnPadNeedsFallback(2, 0) ||
        !geCoopSpawnPadNeedsFallback(2, 1) ||
        geCoopSpawnPadNeedsFallback(2, 2) ||
        geCoopSpawnPadNeedsFallback(1, 1) ||
        geCoopSpawnPadNeedsFallback(0, 0) ||
        !geCoopSpawnPadIsDistinct(100.0f, 0.0f, 100.0f) ||
        geCoopSpawnPadIsDistinct(99.0f, 0.0f, 100.0f) ||
        geCoopSpawnPadIsDistinct(0.0f, 0.0f, 100.0f) ||
        geCoopSpawnPadIsDistinct(100.0f, 0.0f, 0.0f) ||
        !geCoopSpawnPadIsDistinct(-100.0f, 0.0f, 100.0f) ||
        geCoopSpawnPadIsDistinct(std::numeric_limits<float>::infinity(), 0.0f, 100.0f) ||
        geCoopSpawnPadIsDistinct(1.0e30f, 0.0f, 100.0f) ||
        geCoopSpawnPadIsDistinct(100.0f, 0.0f,
                                  std::numeric_limits<float>::infinity())) {
        return fail("co-op distinct-spawn policy contract failed");
    }

    if (geCoopShouldUseDeathmatchSetup(1, 1, 2) ||
        geCoopShouldUseDeathmatchSetup(2, 1, 2) ||
        !geCoopShouldUseDeathmatchSetup(2, 2, 2) ||
        !geCoopShouldUseDeathmatchSetup(4, 2, 2) ||
        geCoopShouldUseDeathmatchSetup(2, 3, 2)) {
        return fail("co-op stage-setup routing contract failed");
    }

    const int selected[] = {0, 7};
    const int too_many_selected[] = {0, 7, 12};
    if (!geCoopCanSelectCharacter(nullptr, 0, 0) ||
        !geCoopCanSelectCharacter(selected, 2, 1) ||
        !geCoopCanSelectCharacter(selected, 2, 63) ||
        geCoopCanSelectCharacter(selected, 2, 0) ||
        geCoopCanSelectCharacter(selected, 2, 7) ||
        geCoopCanSelectCharacter(selected, 2, -1) ||
        geCoopCanSelectCharacter(selected, 2, 64) ||
        geCoopCanSelectCharacter(too_many_selected, 3, 3) ||
        geCoopCanSelectCharacter(nullptr, 1, 1)) {
        return fail("co-op roster-selection contract failed");
    }

    if (geCoopShouldMissionFail(0, 0) ||
        geCoopShouldMissionFail(1, 1) ||
        geCoopShouldMissionFail(2, -1) ||
        geCoopShouldMissionFail(2, 0) ||
        geCoopShouldMissionFail(2, 1) ||
        !geCoopShouldMissionFail(2, 2) ||
        !geCoopShouldMissionFail(2, 3) ||
        geCoopShouldMissionFail(3, 3)) {
        return fail("co-op mission-failure contract failed");
    }

    return 0;
}
