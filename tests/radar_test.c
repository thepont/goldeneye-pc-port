#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "bondconstants.h"
#include "bondtypes.h"
#include "bondview.h"
#include "cheat.h"
#include "front.h"
#include "fr.h"
#include "image_bank.h"
#include "player.h"

Gfx *display_red_blue_on_radar(Gfx *DL);

struct player *g_playerPointers[MAX_MULTIPLAYER];
struct player_data g_playerPlayerData[MAX_MULTIPLAYER];
struct player *g_CurrentPlayer;
struct player_data *g_playerPerm;
s32 player_num;
s32 random_byte;
PLAYER_ID array_PLAYER_IDs[MAX_MULTIPLAYER];
struct sImageTableEntry *mpradarimages;

static struct player players[MAX_MULTIPLAYER];
static PropRecord props[MAX_MULTIPLAYER];

static s32 fake_player_count;
static s32 fake_current_player;
static enum MPSCENARIOS fake_scenario;
static bool fake_no_radar_cheat;
static s16 fake_view_left;
static s16 fake_view_top;
static s16 fake_view_width;
static s16 fake_view_height;

struct rectangle_call
{
    s32 ulx;
    s32 uly;
    s32 lrx;
    s32 lry;
    u32 color;
};

static struct rectangle_call rectangle_calls[16];
static int rectangle_call_count;
static int texture_select_count;
static int constructor_count;
static int combiner_count;
static int cheat_query_count;
static int failures;

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
        fprintf(stderr, "FAIL: %s (actual 0x%08x, expected 0x%08x)\n",
                message, actual, expected);
        failures++;
    }
}

static void reset_render_state(void)
{
    memset(players, 0, sizeof(players));
    memset(props, 0, sizeof(props));
    memset(g_playerPlayerData, 0, sizeof(g_playerPlayerData));
    memset(rectangle_calls, 0, sizeof(rectangle_calls));

    g_playerPointers[0] = &players[0];
    g_playerPointers[1] = &players[1];
    g_playerPointers[2] = &players[2];
    g_playerPointers[3] = &players[3];
    g_CurrentPlayer = &players[0];

    players[0].prop = &props[0];
    players[1].prop = &props[1];
    players[2].prop = &props[2];
    players[3].prop = &props[3];

    fake_player_count = 2;
    fake_current_player = 0;
    fake_scenario = SCENARIO_NORMAL;
    fake_no_radar_cheat = FALSE;
    fake_view_left = 10;
    fake_view_top = 20;
    fake_view_width = 320;
    fake_view_height = 220;

    rectangle_call_count = 0;
    texture_select_count = 0;
    constructor_count = 0;
    combiner_count = 0;
    cheat_query_count = 0;
}

static Gfx *render_radar(void)
{
    static Gfx commands[64];

    memset(commands, 0, sizeof(commands));
    return display_red_blue_on_radar(commands);
}

static u32 other_marker_color(void)
{
    expect(rectangle_call_count == 4,
           "two active players emit a base, local marker, and two remote marker rectangles");
    return rectangle_calls[3].color;
}

MPSCENARIOS get_scenario(void)
{
    return fake_scenario;
}

s32 get_cur_playernum(void)
{
    return fake_current_player;
}

s32 getPlayerCount(void)
{
    return fake_player_count;
}

bool cheatIsActive(CHEAT_ID cheat_id)
{
    expect(cheat_id == CHEAT_NO_RADAR_MP,
           "radar suppression queries only the multiplayer no-radar cheat");
    cheat_query_count++;
    return fake_no_radar_cheat;
}

s16 viGetViewLeft(void)
{
    return fake_view_left;
}

s16 viGetViewTop(void)
{
    return fake_view_top;
}

s16 viGetViewWidth(void)
{
    return fake_view_width;
}

s16 viGetViewHeight(void)
{
    return fake_view_height;
}

void texSelect(Gfx **gdlptr, struct sImageTableEntry *tconfig,
               u32 arg2, s32 arg3, u32 ulst)
{
    (void)gdlptr;
    (void)tconfig;
    (void)arg2;
    (void)arg3;
    (void)ulst;
    texture_select_count++;
}

Gfx *microcode_constructor(Gfx *gdl)
{
    constructor_count++;
    return gdl + 1;
}

Gfx *microcode_constructor_related_to_menus(Gfx *gdl, s32 ulx, s32 uly,
                                             s32 lrx, s32 lry, u32 color)
{
    if (rectangle_call_count < (int)(sizeof(rectangle_calls) /
                                    sizeof(rectangle_calls[0])))
    {
        rectangle_calls[rectangle_call_count].ulx = ulx;
        rectangle_calls[rectangle_call_count].uly = uly;
        rectangle_calls[rectangle_call_count].lrx = lrx;
        rectangle_calls[rectangle_call_count].lry = lry;
        rectangle_calls[rectangle_call_count].color = color;
    }
    rectangle_call_count++;
    return gdl + 1;
}

Gfx *combiner_bayer_lod_perspective(Gfx *gdl)
{
    combiner_count++;
    return gdl + 1;
}

static void test_invalid_player_count_does_not_draw(void)
{
    Gfx commands[64];
    Gfx *end;

    reset_render_state();
    fake_player_count = 0;
    memset(commands, 0xa5, sizeof(commands));

    end = display_red_blue_on_radar(commands);

    expect(end == commands,
           "zero-player state leaves the display-list cursor untouched");
    expect(rectangle_call_count == 0 && texture_select_count == 0 &&
               constructor_count == 0 && combiner_count == 0,
           "zero-player state emits no radar commands");

    reset_render_state();
    fake_player_count = MIN_MULTIPLAYER - 1;
    end = display_red_blue_on_radar(commands);

    expect(end == commands && rectangle_call_count == 0,
           "counts below MIN_MULTIPLAYER leave the display list untouched");

    reset_render_state();
    fake_player_count = MAX_MULTIPLAYER + 1;
    end = display_red_blue_on_radar(commands);

    expect(end == commands && rectangle_call_count == 0 &&
               texture_select_count == 0 && constructor_count == 0 &&
               combiner_count == 0,
           "player counts above MAX_MULTIPLAYER leave the display list untouched");
}

static void test_single_player_and_suppressed_states_do_not_draw(void)
{
    Gfx commands[64];
    Gfx *end;

    reset_render_state();
    fake_player_count = 1;
    end = display_red_blue_on_radar(commands);
    expect(end == commands && rectangle_call_count == 0,
           "single-player state suppresses the multiplayer radar");

    reset_render_state();
    players[0].mpmenuon = 1;
    end = display_red_blue_on_radar(commands);
    expect(end == commands && rectangle_call_count == 0,
           "the multiplayer menu suppresses radar geometry");

    reset_render_state();
    players[0].bonddead = 1;
    end = display_red_blue_on_radar(commands);
    expect(end == commands && rectangle_call_count == 0,
           "a dead current player suppresses radar geometry");

    reset_render_state();
    fake_no_radar_cheat = TRUE;
    end = display_red_blue_on_radar(commands);
    expect(end == commands && rectangle_call_count == 0 && cheat_query_count == 1,
           "the no-radar multiplayer cheat suppresses radar geometry");
}

static void test_normal_radar_uses_current_viewport_and_clamps_remote_range(void)
{
    Gfx *end;

    reset_render_state();
    props[1].pos.f[0] = 100.0f;
    props[1].pos.f[2] = 0.0f;

    end = render_radar();

    expect(end != 0 && texture_select_count == 1 && constructor_count == 1 &&
               combiner_count == 1,
           "active multiplayer radar sets up texture, combiner, and marker geometry");
    expect(rectangle_call_count == 4,
           "normal two-player radar emits exactly one remote marker");
    expect(rectangle_calls[0].ulx == 287 && rectangle_calls[0].uly == 44 &&
               rectangle_calls[0].lrx == 291 && rectangle_calls[0].lry == 48,
           "radar base is positioned relative to the active viewport");
    expect_u32(rectangle_calls[1].color, (u32)-0x60,
               "normal scenarios use the neutral local marker color");
    expect_u32(other_marker_color(), 0xFFFF00A0,
               "nearby remote players use the bright normal-scenario marker");

    props[1].pos.f[0] = 0.0f;
    props[1].pos.f[2] = 4000.0f;
    reset_render_state();
    props[1].pos.f[0] = 0.0f;
    props[1].pos.f[2] = 4000.0f;
    expect(render_radar() != 0, "far-player radar still renders successfully");
    expect_u32(other_marker_color(), 0xFFFF0060,
               "far remote players use the dim normal-scenario marker");
    expect(rectangle_calls[2].ulx == 287 && rectangle_calls[2].ulx <= 305,
           "far remote markers are clamped to the radar radius");
}

static void test_team_radar_colors_token_state_and_dead_players(void)
{
    reset_render_state();
    fake_scenario = SCENARIO_2v2;
    g_playerPlayerData[0].have_token_or_goldengun = 1;
    g_playerPlayerData[1].have_token_or_goldengun = 0;
    props[1].pos.f[2] = 1000.0f;
    render_radar();
    expect_u32(rectangle_calls[1].color, 0x8888FFFF,
               "team scenarios use the token-bearing local marker color");
    expect_u32(other_marker_color(), 0xFF0000A0,
               "nearby non-token opponents use the bright red marker");

    reset_render_state();
    fake_scenario = SCENARIO_TLD;
    g_playerPlayerData[1].have_token_or_goldengun = 1;
    props[1].pos.f[2] = 1000.0f;
    render_radar();
    expect_u32(other_marker_color(), 0x2828FFFF,
               "nearby token-bearing opponents use the blue marker");

    reset_render_state();
    fake_scenario = SCENARIO_MWTGG;
    props[1].pos.f[2] = 4000.0f;
    render_radar();
    expect_u32(other_marker_color(), 0xFF000060,
               "far non-token opponents use the dim red marker");

    reset_render_state();
    fake_scenario = SCENARIO_3v1;
    g_playerPlayerData[1].have_token_or_goldengun = 1;
    props[1].pos.f[2] = 4000.0f;
    render_radar();
    expect_u32(other_marker_color(), 0x2828FFB0,
               "far token-bearing opponents use the dim blue marker");

    reset_render_state();
    fake_scenario = SCENARIO_2v2;
    players[2].bonddead = 1;
    fake_player_count = 3;
    props[1].pos.f[2] = 1000.0f;
    render_radar();
    expect(rectangle_call_count == 4,
           "dead remote players do not emit a marker");
}

static void test_three_player_layout_offsets_even_and_odd_viewports(void)
{
    reset_render_state();
    fake_player_count = 3;
    fake_current_player = 0;
    fake_view_top = 121;
    players[2].bonddead = 1;
    render_radar();

    expect(rectangle_calls[0].ulx == 302 && rectangle_calls[0].uly == 145,
           "the even three-player viewport applies the horizontal radar offset");

    reset_render_state();
    fake_player_count = 3;
    fake_current_player = 1;
    g_CurrentPlayer = &players[1];
    players[2].bonddead = 1;
    render_radar();

    expect(rectangle_calls[0].ulx == 287 && rectangle_calls[0].uly == 44,
           "the odd three-player viewport does not inherit the even-player offset");
}

int main(void)
{
    test_invalid_player_count_does_not_draw();
    test_single_player_and_suppressed_states_do_not_draw();
    test_normal_radar_uses_current_viewport_and_clamps_remote_range();
    test_team_radar_colors_token_state_and_dead_players();
    test_three_player_layout_offsets_even_and_odd_viewports();

    if (failures != 0)
    {
        return 1;
    }

    puts("radar suite passed");
    return 0;
}
