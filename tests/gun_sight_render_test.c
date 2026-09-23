#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>

#include "bondview.h"
#include "image_bank.h"
#include "options.h"
#include "othermodemicrocode.h"
#include "bondwalk2.h"

struct player *g_CurrentPlayer;
struct sImageTableEntry *crosshairimage;

static struct player current_player;
static struct sImageTableEntry crosshair;
static int tex_select_calls;
static int image_draw_calls;
static Gfx *tex_select_input;
static Gfx *tex_select_output;
static struct sImageTableEntry *selected_image;
static f32 captured_xy[2];
static f32 captured_halfed[2];
static s32 captured_format;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_s32(s32 actual, s32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %d, expected %d)\n",
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
    memset(&crosshair, 0, sizeof(crosshair));
    g_CurrentPlayer = &current_player;
    crosshairimage = &crosshair;
    crosshair.level = 1;
    tex_select_calls = 0;
    image_draw_calls = 0;
    tex_select_input = NULL;
    tex_select_output = NULL;
    selected_image = NULL;
    captured_xy[0] = 0.0f;
    captured_xy[1] = 0.0f;
    captured_halfed[0] = 0.0f;
    captured_halfed[1] = 0.0f;
    captured_format = -1;
}

s32 get_cur_playernum(void)
{
    return PLAYER_1;
}

SCREEN_RATIO_OPTION get_screen_ratio(void)
{
    return SCREEN_RATIO_NORMAL;
}

void osSyncPrintf(const char *format, ...)
{
    (void)format;
}

void texSelect(Gfx **gdlptr,
               struct sImageTableEntry *image,
               u32 mode,
               s32 arg3,
               u32 ulst)
{
    (void)mode;
    (void)arg3;
    (void)ulst;
    tex_select_calls++;
    tex_select_input = *gdlptr;
    selected_image = image;
    *gdlptr = *gdlptr + 1;
    tex_select_output = *gdlptr;
}

void display_image_at_position(Gfx **gdlptr,
                               f32 *xypos,
                               f32 *halfedxy,
                               s32 width,
                               s32 height,
                               s32 rotateleft90,
                               s32 fliph,
                               s32 flipv,
                               s32 red,
                               s32 green,
                               s32 blue,
                               s32 alpha,
                               s32 format,
                               s32 param_14)
{
    (void)width;
    (void)height;
    (void)rotateleft90;
    (void)fliph;
    (void)flipv;
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
    (void)param_14;
    image_draw_calls++;
    captured_xy[0] = xypos[0];
    captured_xy[1] = xypos[1];
    captured_halfed[0] = halfedxy[0];
    captured_halfed[1] = halfedxy[1];
    captured_format = format;
    *gdlptr = *gdlptr + 2;
}

extern void gunSetSightVisible(s32 reason, bool visible);
extern void gunDrawSight(s32 *gdl);

static void test_sight_visibility_reason_is_reversible(void)
{
    reset_fixture();

    expect_s32(current_player.gunsightmode, 0,
               "sight reasons start clear");
    gunSetSightVisible(0x04, FALSE);
    expect_s32(current_player.gunsightmode, 0x04,
               "hiding the sight sets the requested reason bit");
    gunSetSightVisible(0x04, TRUE);
    expect_s32(current_player.gunsightmode, 0,
               "showing the sight clears the requested reason bit");

    gunSetSightVisible(0x01, FALSE);
    gunSetSightVisible(0x04, FALSE);
    gunSetSightVisible(0x01, TRUE);
    expect_s32(current_player.gunsightmode, 0x04,
               "showing one sight reason preserves other hidden reasons");
}

static void test_aiming_renders_one_crosshair_into_the_callers_display_list(void)
{
    Gfx display_list_storage[8];
    Gfx *cursor = &display_list_storage[0];

    reset_fixture();
    current_player.gunsightmode = 0;
    current_player.mpmenuon = 0;
    current_player.crosshair_angle.f[0] = 123.5f;
    current_player.crosshair_angle.f[1] = 45.25f;

    gunDrawSight((s32 *)&cursor);

    expect_s32(tex_select_calls, 1,
               "aiming selects the crosshair texture once");
    expect_s32(image_draw_calls, 1,
               "aiming submits one crosshair image");
    expect(selected_image == crosshairimage,
           "aiming submits the configured crosshair texture");
    expect(tex_select_input == &display_list_storage[0],
           "texture selection starts at the caller display-list cursor");
    expect(tex_select_output == &display_list_storage[1],
           "texture selection advances its local display-list cursor");
    expect(cursor == &display_list_storage[3],
           "gun sight writes the advanced cursor back to the caller");
    expect_float(captured_xy[0], 123.5f,
                 "crosshair uses the player's horizontal aim coordinate");
    expect_float(captured_xy[1], 45.25f,
                 "crosshair uses the player's vertical aim coordinate");
    expect_float(captured_halfed[0], 16.0f,
                 "normal aspect crosshair keeps its horizontal half-size");
    expect_float(captured_halfed[1], 16.0f,
                 "normal aspect crosshair keeps its vertical half-size");
    expect_s32(captured_format, 1,
               "crosshair forwards the configured detail level as its image format");

    crosshair.level = 0;
    gunDrawSight((s32 *)&cursor);
    expect_s32(captured_format, 0,
               "crosshair uses the base image format when no detail level is present");
}

static void test_sight_is_hidden_by_multiplayer_menu_or_reason(void)
{
    Gfx display_list_storage[4];
    Gfx *cursor;

    reset_fixture();
    current_player.gunsightmode = 0;
    current_player.mpmenuon = 1;
    cursor = &display_list_storage[0];
    gunDrawSight((s32 *)&cursor);
    expect_s32(image_draw_calls, 0,
               "multiplayer menu suppresses the crosshair");
    expect(cursor == &display_list_storage[0],
           "suppressed sight leaves the display-list cursor unchanged");

    current_player.mpmenuon = 0;
    current_player.gunsightmode = 0x02;
    gunDrawSight((s32 *)&cursor);
    expect_s32(image_draw_calls, 0,
               "not-aiming reason suppresses the crosshair");
    expect(cursor == &display_list_storage[0],
           "not-aiming suppression leaves the display-list cursor unchanged");
}

int main(void)
{
    test_sight_visibility_reason_is_reversible();
    test_aiming_renders_one_crosshair_into_the_callers_display_list();
    test_sight_is_hidden_by_multiplayer_menu_or_reason();

    if (failures != 0)
    {
        fprintf(stderr, "%d gun sight test(s) failed\n", failures);
        return 1;
    }

    puts("gun sight render tests passed");
    return 0;
}
