#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>
#include <bondconstants.h>

#include "bgfog.h"

/* The repository's forced version header is included before this translation
 * unit, so the POSIX feature macro cannot expose these declarations through
 * <stdlib.h>. */
extern int setenv(const char *name, const char *value, int overwrite);
extern int unsetenv(const char *name);

/* These globals and the private details record are the state owned by
 * bgfog.c. The test drives that state through the real fog functions rather
 * than copying the fog formulas into a second helper. */
extern f32 g_ScaledFarFogIntensity;
extern CurrentEnvironmentRecord g_CurrentEnvironment;
extern NearFogRecord *g_NearFogValuesP;
extern EnvironmentRecord *g_EnvironmentFoundp;
extern EnvironmentRecord *g_EnvironmentMainp;
extern EnvironmentRecord *g_EnvironmentAltp;

extern f32 fogGetScaledFarFogIntensity(void);
extern void fogLoadCurrentEnvironment(EnvironmentRecord *environment);
extern void fogLoadFoglessCurrentEnvironment(EnvironmentFoglessRecord *environment);
extern void sub_GAME_7F0BA720(s32 a, s32 b);

static f32 fake_zrange[2] = {10.0f, 2000.0f};
static f32 fake_visibility_scale = 1.0f;
static f32 fake_draw_distance_multiplier = 1.0f;
static f32 last_zrange[2];
static int fake_player_count = 1;
static coord3d fake_player_position;
static Mtxf fake_world_to_screen;
static int failures;

static f32 absolute_float(f32 value)
{
    return value < 0.0f ? -value : value;
}

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_float(f32 actual, f32 expected, const char *message)
{
    if (absolute_float(actual - expected) > 0.001f)
    {
        fprintf(stderr, "FAIL: %s (actual %.7f, expected %.7f)\n",
                message, actual, expected);
        failures++;
    }
}

static int command_is(const Gfx *command, s32 opcode)
{
    return ((command->words.w0 >> 24) & 0xffu) == ((u32)opcode & 0xffu);
}

/* Deterministic ports for the video, background, player-count, and camera
 * boundaries used by bgfog.c. */
void viSetZRange(f32 near, f32 far)
{
    last_zrange[0] = near;
    last_zrange[1] = far;
}

void viGetZRange(f32 *zrange)
{
    zrange[0] = fake_zrange[0];
    zrange[1] = fake_zrange[1];
}

f32 bgGetLevelVisibilityScale(void)
{
    return fake_visibility_scale;
}

f32 portDrawDistanceMultiplier(void)
{
    return fake_draw_distance_multiplier;
}

s32 getPlayerCount(void)
{
    return fake_player_count;
}

coord3d *bondviewGetCurrentPlayersPosition(void)
{
    return &fake_player_position;
}

Mtxf *camGetWorldToScreenMtxf(void)
{
    return &fake_world_to_screen;
}

static void reset_state(void)
{
    memset(&g_CurrentEnvironment, 0, sizeof(g_CurrentEnvironment));
    memset(last_zrange, 0, sizeof(last_zrange));
    memset(&fake_player_position, 0, sizeof(fake_player_position));
    memset(&fake_world_to_screen, 0, sizeof(fake_world_to_screen));

    g_FogSkyIsEnabled = 0;
    g_NearFogValuesP = NULL;
    g_ScaledFarFogIntensity = 0.0f;
    fake_zrange[0] = 10.0f;
    fake_zrange[1] = 2000.0f;
    fake_visibility_scale = 1.0f;
    fake_draw_distance_multiplier = 1.0f;
    fake_player_count = 1;
}

static void fill_environment(EnvironmentRecord *environment)
{
    memset(environment, 0, sizeof(*environment));
    environment->Visibility.BlendMultiplier = 100.0f;
    environment->Visibility.FarFog = 1000.0f;
    environment->Visibility.Nfd.NearFog = 10.0f;
    environment->Visibility.Nfd.MaxVisRange = 500.0f;
    environment->Visibility.Nfd.MaxObfuscationRange = 250.0f;
    environment->Fog.DifferenceFromFarIntensity = 200;
    environment->Fog.FarIntensity = 800;
    environment->Sky.Red = 16;
    environment->Sky.Green = 32;
    environment->Sky.Blue = 48;
    environment->Sky.Clouds = 1;
    environment->Sky.CloudRepeat = 2.0f;
    environment->Sky.SkyImageId = 3;
    environment->Sky.CloudRed = 4.0f;
    environment->Sky.CloudGreen = 5.0f;
    environment->Sky.CloudBlue = 6.0f;
    environment->Sky.IsWater = 1;
    environment->Sky.WaterRepeat = 7.0f;
    environment->Sky.WaterImageId = 8;
    environment->Sky.WaterRed = 9.0f;
    environment->Sky.WaterGreen = 10.0f;
    environment->Sky.WaterBlue = 11.0f;
    environment->Sky.WaterConcavity = 12.0f;
}

static void test_scalar_and_pointer_contracts(void)
{
    NearFogRecord near_fog;

    reset_state();
    g_ScaledFarFogIntensity = 3.0f;
    expect_float(fogGetScaledFarFogIntensity(), 3.0f,
                 "scaled far fog getter returns the active distance");
    expect_float(fogGetScaledFarFogIntensitySquared(), 9.0f,
                 "scaled far fog square uses the active distance");

    g_ScaledFarFogIntensity = -4.0f;
    expect_float(fogGetScaledFarFogIntensitySquared(), 16.0f,
                 "scaled far fog square handles negative distances symmetrically");

    expect(fogGetCurrentEnvironmentp() == &g_CurrentEnvironment,
           "current environment getter returns the owned environment");
    expect(fogGetNearFogValuesP() == NULL,
           "near-fog getter starts empty");

    near_fog.NearFog = 1.0f;
    g_NearFogValuesP = &near_fog;
    expect(fogGetNearFogValuesP() == &near_fog,
           "near-fog getter returns the loaded record");

    /* These original no-op entry points remain callable and must not mutate
     * the fog state while compatibility callers still reference them. */
    g_FogSkyIsEnabled = 1;
    sub_GAME_7F0BA720(12, 34);
    fogRemoved7F0BAA5C(56);
    expect(g_FogSkyIsEnabled == 1,
           "compatibility no-op entry points preserve fog state");
}

static void test_environment_loading(void)
{
    EnvironmentRecord environment;

    reset_state();
    fill_environment(&environment);
    fake_draw_distance_multiplier = 1.25f;
    fake_visibility_scale = 2.0f;

    fogLoadCurrentEnvironment(&environment);

    expect_float(last_zrange[0], 100.0f,
                 "environment load sends authored blend multiplier to the near clip");
    expect_float(last_zrange[1], 1250.0f,
                 "environment load applies draw-distance scaling to the far clip");
    expect_float(fogGetScaledFarFogIntensity(), 801.0f,
                 "environment load derives scaled far fog from the active z range");
    expect(g_CurrentEnvironment.Red == 16 &&
               g_CurrentEnvironment.Green == 32 &&
               g_CurrentEnvironment.Blue == 48,
           "environment load copies the sky color");
    expect(g_CurrentEnvironment.IsWater == 1 &&
               g_CurrentEnvironment.WaterImageId == 8,
           "environment load copies the water settings");
    expect(g_NearFogValuesP == &environment.Visibility.Nfd,
           "nonzero near fog keeps the authored near-fog record");
    expect(g_FogSkyIsEnabled == 1,
           "environment load enables fog rendering");

    environment.Visibility.Nfd.NearFog = 0.0f;
    fogLoadCurrentEnvironment(&environment);
    expect(g_NearFogValuesP == NULL,
           "zero near fog disables the near-fog record");
}

static void test_fogless_environment_loading(void)
{
    EnvironmentFoglessRecord environment;

    reset_state();
    memset(&environment, 0, sizeof(environment));
    environment.Red = 21;
    environment.Green = 34;
    environment.Blue = 55;
    environment.Clouds = 1;
    environment.CloudRepeat = 2.5f;
    environment.SkyImageId = 6;
    environment.CloudRed = 7.0f;
    environment.CloudGreen = 8.0f;
    environment.CloudBlue = 9.0f;
    environment.IsWater = 1;
    environment.WaterRepeat = 10.0f;
    environment.WaterImageId = 11;
    environment.WaterRed = 12.0f;
    environment.WaterGreen = 13.0f;
    environment.WaterBlue = 14.0f;
    environment.WaterConcavity = 15.0f;

    fogLoadFoglessCurrentEnvironment(&environment);

    expect(g_CurrentEnvironment.Red == 21 &&
               g_CurrentEnvironment.Green == 34 &&
               g_CurrentEnvironment.Blue == 55 &&
               g_CurrentEnvironment.Clouds == 1,
           "fogless load copies sky color and cloud state");
    expect_float(g_CurrentEnvironment.CloudRepeat, 2.5f,
                 "fogless load copies cloud repeat");
    expect(g_CurrentEnvironment.SkyImageId == 6 &&
               g_CurrentEnvironment.WaterImageId == 11,
           "fogless load copies sky and water image ids");
    expect_float(g_CurrentEnvironment.WaterConcavity, 15.0f,
                 "fogless load copies water concavity");
}

static void test_level_environment_selection(void)
{
    const s32 unknown_level = 0x7fff;

    reset_state();
    fake_draw_distance_multiplier = 1.0f;
    setenv("GE_D176", "1", 1);

    /* Single-player levels select the ordinary stage row. */
    fake_player_count = 1;
    fogLoadLevelEnvironment(LEVELID_DAM, 0);
    expect(g_EnvironmentFoundp != NULL &&
               g_EnvironmentFoundp->Id == LEVELID_DAM,
           "single-player level selection finds the ordinary row");
    expect(g_FogSkyIsEnabled == 1,
           "ordinary level selection enables fog");

    /* The alternate argument selects the cinema row before the ordinary
     * player-count lookup. */
    fogLoadLevelEnvironment(LEVELID_DAM, 1);
    expect(g_EnvironmentFoundp != NULL &&
               g_EnvironmentFoundp->Id == LEVELID_DAM + ENVIRONMENTDATA_CINEMA,
           "alternate level selection finds the cinema row");

    /* Two-player rows are selected by the level plus the player token. */
    fake_player_count = 2;
    fogLoadLevelEnvironment(LEVELID_BUNKER1, 0);
    expect(g_EnvironmentFoundp != NULL &&
               g_EnvironmentFoundp->Id == LEVELID_BUNKER1 + ENVIRONMENTDATA_PLAYERS_2,
           "two-player level selection finds the dedicated row");

    /* An unknown two-player level falls back to the default two-player row. */
    fogLoadLevelEnvironment(unknown_level, 0);
    expect(g_EnvironmentFoundp != NULL &&
               g_EnvironmentFoundp->Id == LEVELID_DEFAULT + ENVIRONMENTDATA_PLAYERS_2,
           "unknown multiplayer level selects the default player-count row");

    /* A single-player level with no fog row uses the fogless stage table. */
    fake_player_count = 1;
    fogLoadLevelEnvironment(LEVELID_FRIGATE, 0);
    expect(g_EnvironmentFoundp == NULL && g_FogSkyIsEnabled == 0,
           "fogless level fallback disables fog and clears the found row");
    expect(g_CurrentEnvironment.Red == 0x10 &&
               g_CurrentEnvironment.Green == 0x30 &&
               g_CurrentEnvironment.Blue == 0x60,
           "fogless level fallback loads the stage-specific sky");

    /* Unknown fogless stages use the first/default fogless record. */
    fogLoadLevelEnvironment(unknown_level, 0);
    expect(g_EnvironmentFoundp == NULL && g_CurrentEnvironment.Red == 0,
           "unknown fogless level falls back to the default sky");
    expect_float(last_zrange[0], 15.0f,
                 "fogless fallback restores the default near clip");
    expect_float(last_zrange[1], 10000.0f,
                 "fogless fallback restores the default far clip");

    unsetenv("GE_D176");
}

static void test_environment_transition(void)
{
    EnvironmentRecord main_environment;
    EnvironmentRecord alternate_environment;

    reset_state();
    fill_environment(&main_environment);
    fill_environment(&alternate_environment);

    main_environment.Visibility.BlendMultiplier = 100.0f;
    main_environment.Visibility.FarFog = 1000.0f;
    main_environment.Fog.DifferenceFromFarIntensity = 100;
    main_environment.Fog.FarIntensity = 800;
    main_environment.Sky.Red = 10;
    main_environment.Sky.Green = 20;
    main_environment.Sky.Blue = 30;
    main_environment.Visibility.Nfd.NearFog = 0.0f;

    alternate_environment.Visibility.BlendMultiplier = 300.0f;
    alternate_environment.Visibility.FarFog = 2000.0f;
    alternate_environment.Fog.DifferenceFromFarIntensity = 300;
    alternate_environment.Fog.FarIntensity = 900;
    alternate_environment.Sky.Red = 20;
    alternate_environment.Sky.Green = 30;
    alternate_environment.Sky.Blue = 40;
    alternate_environment.Visibility.Nfd.NearFog = 0.0f;

    g_EnvironmentMainp = &main_environment;
    g_EnvironmentAltp = &alternate_environment;
    fake_visibility_scale = 1.0f;

    fogSwitchToSolosky2(0.5f);

    expect_float(last_zrange[0], 200.0f,
                 "environment transition interpolates the blend multiplier");
    expect_float(last_zrange[1], 1500.0f,
                 "environment transition interpolates the far fog distance");
    expect(g_CurrentEnvironment.Red == 8 &&
               g_CurrentEnvironment.Green == 24 &&
               g_CurrentEnvironment.Blue == 32,
           "environment transition quantizes interpolated sky colors");
    expect(g_CurrentEnvironment.DifferenceFromFarIntensity == 200 &&
               g_CurrentEnvironment.FarIntensity == 850,
           "environment transition interpolates fog intensities");
}

static void test_fog_commands(void)
{
    Gfx commands[4];
    Gfx *end;

    reset_state();
    memset(commands, 0, sizeof(commands));
    end = fogSetRenderFogColor(commands, 0);
    expect(end == commands,
           "disabled fog color setup does not consume display-list commands");

    g_FogSkyIsEnabled = 1;
    g_CurrentEnvironment.Red = 0x12;
    g_CurrentEnvironment.Green = 0x34;
    g_CurrentEnvironment.Blue = 0x56;
    g_CurrentEnvironment.DifferenceFromFarIntensity = 100;
    g_CurrentEnvironment.FarIntensity = 900;

    memset(commands, 0, sizeof(commands));
    end = fogSetRenderFogColor(commands, 1);
    expect(end == commands + 4,
           "enabled fog color setup emits four display-list commands");
    expect((commands[0].words.w0 >> 24) == G_SETFOGCOLOR &&
               commands[0].words.w1 == 0x123456ff,
           "fog color command contains the current sky color");
    expect(command_is(&commands[1], G_MOVEWORD),
           "fog setup emits a fog-position move-word command");
    expect(command_is(&commands[2], G_SETGEOMETRYMODE) &&
               commands[2].words.w1 == G_FOG,
           "fog setup enables the fog geometry mode");
    expect(command_is(&commands[3], G_SETOTHERMODE_H),
           "fog setup selects alpha noise dithering");

    /* The alternate argument currently has the same command contract. */
    memset(commands, 0, sizeof(commands));
    end = fogSetRenderFogColor(commands, 0);
    expect(end == commands + 4 &&
               commands[0].words.w1 == 0x123456ff,
           "both fog color paths emit the same current color");

    memset(commands, 0, sizeof(commands));
    end = fogRenderClearFogMode(commands);
    expect(end == commands + 1 &&
               command_is(&commands[0], G_CLEARGEOMETRYMODE) &&
               commands[0].words.w1 == G_FOG,
           "enabled fog clear emits the fog geometry clear command");

    g_FogSkyIsEnabled = 0;
    end = fogRenderClearFogMode(commands);
    expect(end == commands,
           "disabled fog clear leaves the supplied cursor unchanged");
}

static void test_visibility_and_prop_fog(void)
{
    EnvironmentRecord environment;
    coord3d position;
    PropRecord prop;
    rgba_f32 color;

    reset_state();
    position.x = 100.0f;
    position.y = 20.0f;
    position.z = 30.0f;
    fake_player_position.x = 10.0f;
    fake_player_position.y = 20.0f;
    fake_player_position.z = 30.0f;
    memset(&fake_world_to_screen, 0, sizeof(fake_world_to_screen));
    fake_world_to_screen.m[0][0] = 1.0f;
    g_ScaledFarFogIntensity = 90.0f;

    expect(fogPositionIsVisibleThroughFog(&position, 0.0f) == 1,
           "disabled fog treats every position as visible");

    g_FogSkyIsEnabled = 1;
    expect(fogPositionIsVisibleThroughFog(&position, 0.0f) == 1,
           "position at the fog boundary remains visible");
    position.x = 101.0f;
    expect(fogPositionIsVisibleThroughFog(&position, 0.0f) == 0,
           "position beyond the fog boundary is hidden");
    position.x = 100.0f;
    expect(fogPositionIsVisibleThroughFog(&position, 5.0f) == 1,
           "range allowance extends the visible fog boundary");

    /* Use the public environment-loading path to establish the prop-fog
     * transfer function. The test does not reach into its private derived
     * coefficients; it observes the documented depth categories instead. */
    fill_environment(&environment);
    environment.Sky.Red = 64;
    environment.Sky.Green = 128;
    environment.Sky.Blue = 255;
    fake_draw_distance_multiplier = 1.25f;
    fake_visibility_scale = 2.0f;
    fogLoadCurrentEnvironment(&environment);

    memset(&prop, 0, sizeof(prop));
    memset(&color, 0xa5, sizeof(color));
    g_FogSkyIsEnabled = 0;
    expect(fogGetPropDistColor(&prop, &color) == 2,
           "disabled fog leaves prop coloring disabled");

    g_FogSkyIsEnabled = 1;
    prop.zDepth = -1.0f;
    expect(fogGetPropDistColor(&prop, &color) == 2,
           "prop behind the camera receives no fog color");

    prop.zDepth = 100.0f;
    expect(fogGetPropDistColor(&prop, &color) == 1,
           "prop at the full-fog boundary receives fog color");
    expect_float(color.r, 64.0f / 255.0f,
                 "prop fog color uses the environment red channel");
    expect_float(color.g, 128.0f / 255.0f,
                 "prop fog color uses the environment green channel");
    expect_float(color.b, 1.0f,
                 "prop fog color uses the environment blue channel");
    expect(color.a > 0.0f && color.a < 1.0f,
           "prop fog alpha is partial at an intermediate depth");

    prop.zDepth = 100000.0f;
    expect(fogGetPropDistColor(&prop, &color) == 0,
           "near prop is completely obscured when alpha exceeds one");

    prop.zDepth = 1.0f;
    expect(fogGetPropDistColor(&prop, &color) == 2,
           "negative fog alpha does not apply a prop color");
}

int main(void)
{
    test_scalar_and_pointer_contracts();
    test_environment_loading();
    test_fogless_environment_loading();
    test_level_environment_selection();
    test_environment_transition();
    test_fog_commands();
    test_visibility_and_prop_fog();

    if (failures != 0)
    {
        fprintf(stderr, "%d bgfog contract(s) failed\n", failures);
        return 1;
    }

    puts("bgfog suite passed");
    return 0;
}
