#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Include the production translation unit so this unit test can exercise the
 * PORT-only spawn-selection seam without launching a level.  Function/data
 * sections plus --gc-sections leave the unrelated intro renderer out of the
 * link; the wrapper below reaches only the co-op pad policy. */
#include "../src/game/bondview_r.c"

GAMEMODE gamemode = GAMEMODE_SOLO;
stagesetup g_CurrentSetup;
s32 startpadcount;
PadRecord *g_Startpad[16];

static s32 fake_player_count;
static s32 fallback_log_count;

s32 getPlayerCount(void)
{
    return fake_player_count;
}

void osSyncPrintf(const char *fmt, ...)
{
    if (fmt != NULL && strstr(fmt, "COOP_SPAWN_FALLBACK") != NULL)
    {
        fallback_log_count++;
    }
}

static void set_pad(PadRecord *pad, f32 x, f32 z, f32 look_x, f32 look_z,
                    int has_stan, int linked)
{
    memset(pad, 0, sizeof(*pad));
    pad->pos.f[0] = x;
    pad->pos.f[2] = z;
    pad->look.f[0] = look_x;
    pad->look.f[2] = look_z;
    pad->stan = has_stan ? (StandTile *)(uintptr_t)1 : NULL;
    pad->plink = linked ? (char *)(uintptr_t)1 : NULL;
}

static void reset_fixture(PadRecord *pads, s32 pad_count)
{
    s32 index;

    memset(&g_CurrentSetup, 0, sizeof(g_CurrentSetup));
    memset(g_Startpad, 0, sizeof(g_Startpad));
    for (index = 0; index < pad_count; index++)
    {
        set_pad(&pads[index], 0.0f, 0.0f, 0.0f, 0.0f, 1, 1);
    }
    set_pad(&pads[pad_count], 0.0f, 0.0f, 0.0f, 0.0f, 0, 0);
    g_CurrentSetup.pads = pads;
    startpadcount = 0;
    gamemode = GAMEMODE_COOP;
    fake_player_count = GE_COOP_MAX_PLAYERS;
    fallback_log_count = 0;
}

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static int test_guard_conditions(void)
{
    PadRecord pads[2];
    int failures = 0;

    reset_fixture(pads, 1);
    g_Startpad[0] = &pads[0];
    startpadcount = 1;

    gamemode = GAMEMODE_SOLO;
    bondviewEnsureDistinctCoopStartPads();
    failures += expect(startpadcount == 1,
                       "solo mode must not alter authored start pads");

    gamemode = GAMEMODE_COOP;
    fake_player_count = 1;
    bondviewEnsureDistinctCoopStartPads();
    failures += expect(startpadcount == 1,
                       "single-player co-op setup must not create a second pad");

    fake_player_count = GE_COOP_MAX_PLAYERS;
    g_CurrentSetup.pads = NULL;
    bondviewEnsureDistinctCoopStartPads();
    failures += expect(startpadcount == 1,
                       "missing pad setup must fail closed");
    return failures;
}

static int test_selects_nearest_distinct_pad(void)
{
    PadRecord pads[5];
    int failures = 0;

    reset_fixture(pads, 4);
    set_pad(&pads[0], 0.0f, 0.0f, 1.0f, 0.0f, 1, 1);
    set_pad(&pads[1], 50.0f, 0.0f, 1.0f, 0.0f, 1, 1);
    set_pad(&pads[2], 300.0f, 0.0f, 1.0f, 0.0f, 1, 1);
    set_pad(&pads[3], 125.0f, 0.0f, 1.0f, 0.0f, 1, 1);
    g_Startpad[0] = &pads[0];
    startpadcount = 1;

    bondviewEnsureDistinctCoopStartPads();
    failures += expect(startpadcount == 2,
                       "co-op must add a second authored navigation pad");
    failures += expect(g_Startpad[1] == &pads[3],
                       "co-op must choose the nearest distinct candidate");
    failures += expect(fallback_log_count == 0,
                       "an authored candidate must not use the synthetic fallback");
    return failures;
}

static int test_bootstraps_missing_first_pad(void)
{
    PadRecord pads[3];
    int failures = 0;

    reset_fixture(pads, 2);
    set_pad(&pads[0], 10.0f, 20.0f, 1.0f, 0.0f, 1, 1);
    set_pad(&pads[1], 210.0f, 20.0f, 1.0f, 0.0f, 1, 1);

    bondviewEnsureDistinctCoopStartPads();
    failures += expect(startpadcount == 2,
                       "co-op must bootstrap the first pad from setup data");
    failures += expect(g_Startpad[0] == &pads[0] && g_Startpad[1] == &pads[1],
                       "bootstrapped co-op pads must preserve setup order");
    return failures;
}

static int test_synthesizes_safe_fallback(void)
{
    PadRecord pads[2];
    int failures = 0;

    reset_fixture(pads, 1);
    set_pad(&pads[0], 10.0f, 20.0f, 0.0f, 0.0f, 1, 1);
    g_Startpad[0] = &pads[0];
    startpadcount = 1;

    bondviewEnsureDistinctCoopStartPads();
    failures += expect(startpadcount == 2,
                       "co-op must synthesize a second pad when setup has no candidate");
    failures += expect(g_Startpad[1] != &pads[0] && g_Startpad[1] != NULL,
                       "synthetic co-op fallback must be a separate pad");
    failures += expect(geCoopSpawnPadIsDistinct(
                           g_Startpad[1]->pos.f[0] - pads[0].pos.f[0],
                           g_Startpad[1]->pos.f[2] - pads[0].pos.f[2],
                           100.0f),
                       "synthetic co-op fallback must meet the minimum separation");
    failures += expect(fallback_log_count == 1,
                       "synthetic co-op fallback must emit one diagnostic record");
    return failures;
}

static int test_preserves_valid_authored_pair_and_capacity(void)
{
    PadRecord pads[2];
    int failures = 0;
    s32 index;

    reset_fixture(pads, 1);
    set_pad(&pads[0], 0.0f, 0.0f, 1.0f, 0.0f, 1, 1);
    set_pad(&pads[1], 100.0f, 0.0f, 1.0f, 0.0f, 1, 0);
    g_Startpad[0] = &pads[0];
    g_Startpad[1] = &pads[1];
    startpadcount = 2;
    bondviewEnsureDistinctCoopStartPads();
    failures += expect(startpadcount == 2 && g_Startpad[1] == &pads[1],
                       "valid authored split starts must remain unchanged");

    reset_fixture(pads, 1);
    for (index = 0; index < 16; index++)
    {
        g_Startpad[index] = &pads[0];
    }
    startpadcount = 16;
    bondviewEnsureDistinctCoopStartPads();
    failures += expect(startpadcount == 16,
                       "full start-pad storage must not overflow for malformed setup data");
    return failures;
}

int main(void)
{
    int failures = 0;

    failures += test_guard_conditions();
    failures += test_selects_nearest_distinct_pad();
    failures += test_bootstraps_missing_first_pad();
    failures += test_synthesizes_safe_fallback();
    failures += test_preserves_valid_authored_pair_and_capacity();

    if (failures == 0)
    {
        fprintf(stderr, "bondview co-op spawn suite passed\n");
        return 0;
    }
    fprintf(stderr, "%d bondview co-op spawn contract(s) are red\n", failures);
    return 1;
}
