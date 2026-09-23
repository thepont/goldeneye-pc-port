#include <stdio.h>
#include <string.h>

#include <music.h>
#include <snd.h>
#include <ultra64.h>

#include "bondconstants.h"
#include "bondview.h"
#include "language.h"
#include "options.h"
#include "player.h"

struct player *g_CurrentPlayer;
s32 j_text_trigger;
ALBank *g_musicSfxBufferPtr;

static struct player current_player;
static s32 fake_controller_count;
static s8 fake_stick_x;
static u16 fake_pressed_buttons;
static u16 last_sound_index;
static u16 applied_music_volume;
static int sound_calls;
static int music_volume_calls;
static int failures;

extern u32 controller_options_index;
extern u32 controlstick_lr_enabled;
extern s32 watch_item_is_actively_selected;
extern void game_option_select_value(u32 *value, u32 selected_value);
extern void game_option_toggle_input(s32 option_index);
extern u32 controllerCheckDualControllerTypesAllowed(void);

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_u32(u32 actual, u32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %u, expected %u)\n",
                message, actual, expected);
        failures++;
    }
}

static void expect_float(f32 actual, f32 expected, const char *message)
{
    f32 difference = actual - expected;
    if (difference < 0.0f)
    {
        difference = -difference;
    }

    if (difference > 0.0001f)
    {
        fprintf(stderr, "FAIL: %s (actual %.6f, expected %.6f)\n",
                message, actual, expected);
        failures++;
    }
}

static void reset_fixture(void)
{
    memset(&current_player, 0, sizeof(current_player));
    g_CurrentPlayer = &current_player;
    j_text_trigger = FALSE;
    fake_controller_count = 1;
    fake_stick_x = 0;
    fake_pressed_buttons = 0;
    last_sound_index = 0;
    applied_music_volume = 0;
    sound_calls = 0;
    music_volume_calls = 0;
    controller_options_index = CONTROLLER_OPTIONS_INDEX_STYLE;
    controlstick_lr_enabled = 1;
}

s8 joyGetControllerCount(void)
{
    return (s8)fake_controller_count;
}

s8 joyGetStickX(s8 player)
{
    (void)player;
    return fake_stick_x;
}

u16 joyGetButtonsPressedThisFrame(s8 player, u16 mask)
{
    (void)player;
    return fake_pressed_buttons & mask;
}

ALSoundState *sndPlaySfx(struct ALBankAlt_s *sound_bank,
                         s16 sound_index,
                         ALSoundState *pending_state)
{
    (void)sound_bank;
    (void)pending_state;
    sound_calls++;
    last_sound_index = (u16)sound_index;
    return NULL;
}

void musicTrack2ApplySeqpVol(u16 volume)
{
    music_volume_calls++;
    applied_music_volume = volume;
}

static void test_initial_option_values_and_round_trip_setters(void)
{
    reset_fixture();

    expect_u32(get_cur_player_look_vertical_inverted(), 0,
               "look direction starts uninverted");
    expect_u32(cur_player_get_autoaim(), 1,
               "auto-aim starts enabled");
    expect_u32(cur_player_get_lookahead(), 1,
               "look-ahead starts enabled");
    expect_u32(cur_player_get_aim_control(), 0,
               "aim control starts in hold mode");
    expect_u32(cur_player_get_sight_onscreen_control(), 1,
               "on-screen sight starts enabled");
    expect_u32(cur_player_get_ammo_onscreen_setting(), 1,
               "on-screen ammo starts enabled");
    expect_u32(cur_player_get_screen_setting(), SCREEN_SIZE_FULLSCREEN,
               "screen size starts fullscreen");
    expect_u32(get_screen_ratio(), SCREEN_RATIO_NORMAL,
               "screen ratio starts normal");

    set_cur_player_look_vertical_inverted(1);
    cur_player_set_autoaim(0);
    cur_player_set_lookahead(0);
    cur_player_set_aim_control(1);
    cur_player_set_sight_onscreen_control(1);
    cur_player_set_ammo_onscreen_setting(0);
    cur_player_set_screen_setting(SCREEN_SIZE_CINEMA);
    set_screen_ratio(SCREEN_RATIO_16_9);

    expect_u32(get_cur_player_look_vertical_inverted(), 1,
               "look direction setter enables inversion");
    expect_u32(cur_player_get_autoaim(), 0,
               "auto-aim setter persists disabled state");
    expect_u32(cur_player_get_lookahead(), 0,
               "look-ahead setter persists disabled state");
    expect_u32(cur_player_get_aim_control(), 1,
               "aim control setter persists toggle mode");
    expect_u32(cur_player_get_sight_onscreen_control(), 1,
               "sight setter persists enabled state");
    expect_u32(cur_player_get_ammo_onscreen_setting(), 0,
               "ammo setter persists disabled state");
    expect_u32(cur_player_get_screen_setting(), SCREEN_SIZE_CINEMA,
               "screen size setter persists cinema mode");
    expect_u32(get_screen_ratio(), SCREEN_RATIO_16_9,
               "screen ratio setter persists widescreen ratio");
}

static void test_control_style_updates_all_runtime_representations(void)
{
    reset_fixture();

    cur_player_set_control_type(CONTROLLER_CONFIG_PLENTY);

    expect_u32((u32)cur_player_get_control_type(), CONTROLLER_CONFIG_PLENTY,
               "control style getter follows the selected style");
    expect_u32((u32)current_player.cur_player_control_type_1,
               CONTROLLER_CONFIG_PLENTY,
               "control style updates the display selection");
    expect_float(current_player.cur_player_control_type_2,
                 (f32)CONTROLLER_CONFIG_PLENTY,
                 "control style updates the interpolated selection");
    expect(current_player.has_set_control_type_data == TRUE,
           "control style marks the selection as changed");
    expect(current_player.neg_vspacing_for_control_type_entry == -40,
           "english control style spacing uses ten pixels per entry");

    reset_fixture();
    j_text_trigger = TRUE;
    cur_player_set_control_type(CONTROLLER_CONFIG_GOODHEAD);
    expect(current_player.neg_vspacing_for_control_type_entry == -98,
           "multibyte control style spacing uses fourteen pixels per entry");
}

static void test_dual_controller_gate_requires_two_pads_and_a_two_handed_style(void)
{
    reset_fixture();
    current_player.cur_player_control_type_0 = CONTROLLER_CONFIG_PLENTY;

    fake_controller_count = 1;
    expect_u32(controllerCheckDualControllerTypesAllowed(), 0,
               "one connected controller cannot enable a dual-controller style");

    fake_controller_count = 2;
    expect_u32(controllerCheckDualControllerTypesAllowed(), 1,
               "two connected controllers enable a dual-controller style");

    current_player.cur_player_control_type_0 = CONTROLLER_CONFIG_HONEY;
    expect_u32(controllerCheckDualControllerTypesAllowed(), 0,
               "two connected controllers do not make a one-controller style dual");
}

static void test_option_selection_disables_horizontal_navigation_and_beeps(void)
{
    u32 value;

    reset_fixture();
    value = 0;
    game_option_select_value(&value, 2);

    expect_u32(value, 2, "selecting an option stores the selected value");
    expect_u32(controlstick_lr_enabled, 0,
               "selecting an option returns navigation to vertical mode");
    expect(sound_calls == 1, "selecting an option emits one confirmation sound");
    expect_u32(last_sound_index, OPTION_CHOOSE_SFX,
               "selecting an option uses the option confirmation sound");
}

static void test_toggle_input_obeys_selection_edges_and_option_boundaries(void)
{
    reset_fixture();

    /* A highlighted row is the only state in which a direction edits it. */
    watch_item_is_actively_selected = 0;
    game_options_entries[PLAYER_OPTION_AUTOAIM].current_value = 1;
    fake_pressed_buttons = R_JPAD;
    game_option_toggle_input(PLAYER_OPTION_AUTOAIM);
    expect_u32(cur_player_get_autoaim(), 1,
               "unselected rows ignore a controller toggle direction");

    watch_item_is_actively_selected = 1;
    fake_pressed_buttons = L_JPAD;
    game_option_toggle_input(PLAYER_OPTION_AUTOAIM);
    expect_u32(cur_player_get_autoaim(), 0,
               "left direction turns a binary option off");

    fake_pressed_buttons = R_JPAD;
    game_option_toggle_input(PLAYER_OPTION_AUTOAIM);
    expect_u32(cur_player_get_autoaim(), 1,
               "right direction turns a binary option on");

    /* The screen-size row has three states; its right edge must not wrap. */
    game_options_entries[PLAYER_OPTION_SCREEN].current_value = 1;
    game_option_toggle_input(PLAYER_OPTION_SCREEN);
    expect_u32(cur_player_get_screen_setting(), 2,
               "right direction advances a three-state option");
    game_option_toggle_input(PLAYER_OPTION_SCREEN);
    expect_u32(cur_player_get_screen_setting(), 2,
               "right direction does not wrap a three-state option");

    fake_pressed_buttons = L_JPAD;
    game_option_toggle_input(PLAYER_OPTION_SCREEN);
    expect_u32(cur_player_get_screen_setting(), 1,
               "left direction retreats a three-state option");
}

static void test_volume_and_numeric_helpers_preserve_boundary_contracts(void)
{
    reset_fixture();

    expect_u32(get_mTrack2Vol(), 0x7fff,
               "music volume retains its initialized maximum");
    set_mTrack2Vol(0);
    expect_u32(get_mTrack2Vol(), 0,
               "music volume accepts the silent boundary");
    expect(music_volume_calls == 1 && applied_music_volume == 0,
           "music volume setter applies the exact selected volume");

    expect_float(watchWrapAroundPI((f32)M_PI_F + 0.25f),
                 (f32)(-M_PI_F + 0.25f),
                 "angles above positive pi wrap once into range");
    expect_float(watchWrapAroundPI((f32)M_MINUS_PI_F - 0.25f),
                 (f32)(M_PI_F - 0.25f),
                 "angles below negative pi wrap once into range");
    expect_float(watchWrapAroundPI(0.25f), 0.25f,
                 "angles already in range remain unchanged");

    expect_float(sub_GAME_7F0A95C4(0.0f, 1.0f, 4.0f), 0.25f,
                 "interpolation advances toward a greater target");
    expect_float(sub_GAME_7F0A95C4(1.0f, 0.0f, 4.0f), 0.75f,
                 "interpolation retreats toward a lesser target");
    expect_float(sub_GAME_7F0A95C4(1.0f, 1.0f, 4.0f), 1.0f,
                 "interpolation leaves an equal value unchanged");
}

int main(void)
{
    test_initial_option_values_and_round_trip_setters();
    test_control_style_updates_all_runtime_representations();
    test_dual_controller_gate_requires_two_pads_and_a_two_handed_style();
    test_option_selection_disables_horizontal_navigation_and_beeps();
    test_toggle_input_obeys_selection_edges_and_option_boundaries();
    test_volume_and_numeric_helpers_preserve_boundary_contracts();

    if (failures != 0)
    {
        fprintf(stderr, "%d option state test(s) failed\n", failures);
        return 1;
    }

    puts("options state tests passed");
    return 0;
}
