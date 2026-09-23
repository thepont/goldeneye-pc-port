#include <stdio.h>

#include <ultra64.h>
#include <bondconstants.h>

#include "music_0D2720.h"

struct music_setup
{
    s16 stage_id;
    s16 main_music;
    s16 bg_sound;
    s16 xtrack;
};

extern struct music_setup music_setup_entries[];

static u32 fake_random_value;
static s32 random_calls;
static int failures;

static void expect_s32(s32 actual, s32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %d, expected %d)\n",
                message, actual, expected);
        failures++;
    }
}

u32 randomGetNext(void)
{
    random_calls++;
    return fake_random_value;
}

static void reset_rng(u32 value)
{
    fake_random_value = value;
    random_calls = 0;
}

static void test_known_stage_routes(void)
{
    reset_rng(99);

    /* Given a known single-player stage, the main track is table-driven. */
    expect_s32(getmusictrack_or_randomtrack(LEVELID_DAM), M_DAM,
               "Dam selects its configured main track");
    expect_s32(random_calls, 0,
               "known main-track lookup does not consume random state");

    expect_s32(musicGetBgTrackForStage(LEVELID_DAM), -1,
               "Dam keeps its configured background track sentinel");
    expect_s32(musicGetXTrackForStage(LEVELID_DAM), M_WIND,
               "Dam selects its configured cross-fade track");

    expect_s32(getmusictrack_or_randomtrack(LEVELID_SURFACE2), M_SURFACE2,
               "Surface 2 selects its configured main track");
    expect_s32(musicGetBgTrackForStage(LEVELID_SURFACE2), M_WIND,
               "Surface 2 selects its configured background track");
    expect_s32(musicGetXTrackForStage(LEVELID_SURFACE2), M_SURFACE2END,
               "Surface 2 selects its configured ending track");
}

static void test_unknown_stage_uses_deterministic_random_fallback(void)
{
    /* When a stage is absent, the random pool is selected modulo its count. */
    reset_rng(0);
    expect_s32(getmusictrack_or_randomtrack(0x7fff), M_TRAIN,
               "unknown stage selects the first random track");
    expect_s32(random_calls, 1,
               "unknown stage consumes exactly one random value");

    reset_rng(1);
    expect_s32(getmusictrack_or_randomtrack(0x7fff), M_DEPOT,
               "unknown stage advances through the random track pool");
    expect_s32(random_calls, 1,
               "second unknown-stage lookup consumes one random value");

    expect_s32(musicGetBgTrackForStage(0x7fff), -1,
               "unknown stage has no background track");
    expect_s32(musicGetXTrackForStage(0x7fff), -1,
               "unknown stage has no cross-fade track");
}

static void test_configured_random_main_track(void)
{
    s16 saved_stage = music_setup_entries[0].stage_id;
    s16 saved_main = music_setup_entries[0].main_music;

    /* The production table currently has no -1 main entry, so use a local
     * table mutation to exercise that supported data-driven branch without a
     * ROM or a level load. */
    music_setup_entries[0].stage_id = 0x7ffe;
    music_setup_entries[0].main_music = -1;
    reset_rng(2);
    expect_s32(getmusictrack_or_randomtrack(0x7ffe), M_MPTHEME,
               "a configured random-main stage uses the random pool");
    expect_s32(random_calls, 1,
               "configured random-main stage consumes one random value");

    music_setup_entries[0].stage_id = saved_stage;
    music_setup_entries[0].main_music = saved_main;
}

int main(void)
{
    test_known_stage_routes();
    test_unknown_stage_uses_deterministic_random_fallback();
    test_configured_random_main_track();

    if (failures != 0)
    {
        fprintf(stderr, "%d music-track contract(s) failed\n", failures);
        return 1;
    }

    puts("music tracks suite passed");
    return 0;
}
