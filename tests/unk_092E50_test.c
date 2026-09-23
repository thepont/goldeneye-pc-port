#include <math.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>

#include "unk_092E50.h"

extern Gfx MipMap2C_Something_Setup[];
extern Gfx MipMap2C_Something2_Setup[];

s32 g_ClockTimer;

static int failures;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_float(f32 actual, f32 expected, const char *message)
{
    f32 difference = actual - expected;

    if (difference < -0.0005f || difference > 0.0005f)
    {
        fprintf(stderr, "FAIL: %s (actual %.6f expected %.6f)\n",
                message, actual, expected);
        failures++;
    }
}

static void test_water_animation_wraps_both_directions(void)
{
    memset(MipMap2C_Something_Setup, 0,
           sizeof(Gfx) * 12);
    memset(MipMap2C_Something2_Setup, 0,
           sizeof(Gfx) * 12);

    g_ClockTimer = 100;
    flt_CODE_bss_80079E80 = 250.0f;
    flt_CODE_bss_80079E84 = 250.0f;
    flt_CODE_bss_80079E88 = 6.0f;
    sub_GAME_7F092E50();

    expect_float(flt_CODE_bss_80079E80, 19.0f,
                 "water animation wraps the S coordinate at 256");
    expect_float(flt_CODE_bss_80079E84, 4.0f,
                 "water animation wraps the T coordinate at 256");
    expect_float(flt_CODE_bss_80079E88, 3.7168148f,
                 "water animation wraps the phase at one full turn");
    expect_float(MipMap2C_Something_Setup[2].loadtile.sl, 19.0f,
                 "water animation publishes S to the first texture setup");
    expect_float(MipMap2C_Something2_Setup[2].loadtile.tl, 4.0f,
                 "water animation publishes T to the second texture setup");
    expect_true((MipMap2C_Something_Setup[4].words.w0 & 0xFFu)
                    == (u32)(sinf(flt_CODE_bss_80079E88) * 127.0f + 128.0f),
                "water animation publishes the LOD to the first prim-color command");
    expect_true((MipMap2C_Something2_Setup[4].words.w0 & 0xFFu)
                    == (u32)(sinf(flt_CODE_bss_80079E88) * 127.0f + 128.0f),
                "water animation publishes the LOD to the second prim-color command");

    g_ClockTimer = 0;
    flt_CODE_bss_80079E80 = -1.0f;
    flt_CODE_bss_80079E84 = -2.0f;
    flt_CODE_bss_80079E88 = -0.5f;
    sub_GAME_7F092E50();

    expect_float(flt_CODE_bss_80079E80, 255.0f,
                 "water animation repairs a negative S coordinate");
    expect_float(flt_CODE_bss_80079E84, 254.0f,
                 "water animation repairs a negative T coordinate");
    expect_float(flt_CODE_bss_80079E88, 5.7831855f,
                 "water animation repairs a negative phase");
}

static void test_water_display_list_routes_setup_or_explicit_state(void)
{
    Gfx display_list[32];
    Gfx *end;

    memset(display_list, 0, sizeof(display_list));
    flt_CODE_bss_80079E88 = 1.0f;

    end = sub_GAME_7F09343C(display_list, 1);
    expect_true(end == display_list + 1,
                "RGBA water rendering routes the prebuilt setup list");

    end = sub_GAME_7F09343C(display_list, 0);
    expect_true(end == display_list + 12,
                "RGBA water rendering emits the explicit tile state");

    end = sub_GAME_7F09365C(display_list, 1);
    expect_true(end == display_list + 1,
                "CI water rendering routes the prebuilt setup list");

    end = sub_GAME_7F09365C(display_list, 0);
    expect_true(end == display_list + 12,
                "CI water rendering emits the explicit tile state");
}

int main(void)
{
    test_water_animation_wraps_both_directions();
    test_water_display_list_routes_setup_or_explicit_state();

    if (failures != 0)
    {
        fprintf(stderr, "%d water animation/display-list contract(s) failed\n",
                failures);
        return 1;
    }

    puts("water animation/display-list suite passed");
    return 0;
}
