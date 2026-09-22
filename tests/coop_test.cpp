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

    const struct {
        const char *value;
        int expected;
    } parse_cases[] = {
        {nullptr, 99}, {"", 99}, {"solo", 11}, {"multi", 22},
        {"coop", 33}, {"Solo", 99}, {" multi", 99}, {"coop ", 99},
        {"deathmatch", 99}, {"co-op", 99},
    };
    for (const auto &test : parse_cases)
    {
        if (geCoopParseMode(test.value, 11, 22, 33, 99) != test.expected)
        {
            return fail("co-op parser boundary matrix failed");
        }
    }

    for (int controllers = -8; controllers <= 8; ++controllers)
    {
        const int expected = controllers >= GE_COOP_MAX_PLAYERS ?
            GE_COOP_MAX_PLAYERS : 0;
        if (geCoopPlayerCount(controllers) != expected)
        {
            return fail("co-op controller-count boundary matrix failed");
        }
    }

    for (int saved = -2; saved <= 5; ++saved)
    {
        for (int controllers = -2; controllers <= 5; ++controllers)
        {
            int expected = 11;
            if (saved == 11 || saved == 22)
            {
                expected = saved;
            }
            else if (saved == 33 && controllers >= GE_COOP_MAX_PLAYERS)
            {
                expected = 33;
            }
            if (geCoopResumeMode(saved, controllers, 11, 22, 33) != expected)
            {
                return fail("co-op resume boundary matrix failed");
            }
        }
    }

    for (int players = -2; players <= 5; ++players)
    {
        for (int authored = -2; authored <= 5; ++authored)
        {
            const int expected = players >= GE_COOP_MAX_PLAYERS &&
                                 authored < GE_COOP_MAX_PLAYERS;
            if (geCoopSpawnPadNeedsFallback(players, authored) != expected)
            {
                return fail("co-op spawn-fallback boundary matrix failed");
            }
        }
    }

    for (int dx = -4; dx <= 4; ++dx)
    {
        for (int dz = -4; dz <= 4; ++dz)
        {
            const int expected = dx * dx + dz * dz >= 1;
            if (geCoopSpawnPadIsDistinct((float)dx, (float)dz, 1.0f) != expected)
            {
                return fail("co-op spawn-distance boundary matrix failed");
            }
        }
    }
    if (geCoopSpawnPadIsDistinct(2.0f, 0.0f, 2.0f) == 0 ||
        geCoopSpawnPadIsDistinct(1.999f, 0.0f, 2.0f) != 0 ||
        geCoopSpawnPadIsDistinct(0.0f, 0.0f, -1.0f) != 0 ||
        geCoopSpawnPadIsDistinct(0.0f, 0.0f,
                                  std::numeric_limits<float>::quiet_NaN()) != 0)
    {
        return fail("co-op spawn-distance exact-boundary contract failed");
    }

    for (int players = -2; players <= 5; ++players)
    {
        for (int game_mode = -1; game_mode <= 4; ++game_mode)
        {
            for (int deathmatch_mode = -1; deathmatch_mode <= 4; ++deathmatch_mode)
            {
                const int expected = players >= GE_COOP_MAX_PLAYERS &&
                                     game_mode == deathmatch_mode;
                if (geCoopShouldUseDeathmatchSetup(players, game_mode,
                                                   deathmatch_mode) != expected)
                {
                    return fail("co-op stage-setup boundary matrix failed");
                }
            }
        }
    }

    const int one_selected[] = {7};
    const int two_selected[] = {0, 63};
    for (int candidate = -2; candidate <= GE_COOP_CHARACTER_COUNT + 1; ++candidate)
    {
        const int valid_candidate = candidate >= 0 &&
                                    candidate < GE_COOP_CHARACTER_COUNT;
        if (geCoopCanSelectCharacter(nullptr, 0, candidate) != valid_candidate ||
            geCoopCanSelectCharacter(one_selected, 1, candidate) !=
                (valid_candidate && candidate != 7) ||
            geCoopCanSelectCharacter(two_selected, 2, candidate) !=
                (valid_candidate && candidate != 0 && candidate != 63))
        {
            return fail("co-op roster boundary matrix failed");
        }
    }
    const int invalid_selected[] = {-1, GE_COOP_CHARACTER_COUNT};
    if (geCoopCanSelectCharacter(invalid_selected, 1, 3) != 0 ||
        geCoopCanSelectCharacter(invalid_selected + 1, 1, 3) != 0 ||
        geCoopCanSelectCharacter(two_selected, -1, 3) != 0 ||
        geCoopCanSelectCharacter(two_selected, GE_COOP_MAX_PLAYERS + 1, 3) != 0)
    {
        return fail("co-op roster invalid-state matrix failed");
    }

    for (int players = -2; players <= 5; ++players)
    {
        for (int dead = -2; dead <= 6; ++dead)
        {
            const int expected = players == GE_COOP_MAX_PLAYERS && dead >= players;
            if (geCoopShouldMissionFail(players, dead) != expected)
            {
                return fail("co-op mission-failure boundary matrix failed");
            }
        }
    }

    return 0;
}
