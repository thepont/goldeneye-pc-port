#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>

#include "debugmenu_handler.h"

extern s32 g_DMenuSelectedOption;
extern s32 g_DMenuNumOptions;
extern s32 g_DMenuNumGroups;

void debmenuSetScaleIndex(s32 value);
void debmenuSetMenu(char **labels, s32 (*positions)[2], s32 *offsets);
s32 get_highlighted_debug_option(void);
void set_highlighted_debug_option(s32 value);
void gotoAboveDebugOption(void);
void gotoBelowDebugOption(void);
void gotoLeftDebugOption(void);
void gotoRightDebugOption(void);
void sub_GAME_7F09000C(int a0, int a1, int a2);

static int failures;
static int colour_calls;
static int position_calls;
static int string_calls;
static int error_calls;
static s32 colours[8][4];
static s32 positions[8][2];
static const unsigned char *strings[8];

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_int(long actual, long expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %ld expected %ld)\n",
                message, actual, expected);
        failures++;
    }
}

void debmenuSetFgColour(s32 red, s32 green, s32 blue, s32 alpha)
{
    if (colour_calls < 8)
    {
        colours[colour_calls][0] = red;
        colours[colour_calls][1] = green;
        colours[colour_calls][2] = blue;
        colours[colour_calls][3] = alpha;
    }
    colour_calls++;
}

void debmenuSetPos(s32 x, s32 y)
{
    if (position_calls < 8)
    {
        positions[position_calls][0] = x;
        positions[position_calls][1] = y;
    }
    position_calls++;
}

void debmenuPrintString(const unsigned char *string)
{
    if (string_calls < 8)
    {
        strings[string_calls] = string;
    }
    string_calls++;
}

void osSyncPrintf(const char *format, ...)
{
    (void)format;
    error_calls++;
}

static void reset_calls(void)
{
    memset(colours, 0, sizeof(colours));
    memset(positions, 0, sizeof(positions));
    memset(strings, 0, sizeof(strings));
    colour_calls = 0;
    position_calls = 0;
    string_calls = 0;
    error_calls = 0;
}

static void test_grouped_navigation_wraps_without_crossing_groups(void)
{
    char *labels[] = { "A", "B", "C", "D", "E", "F" };
    s32 positions_table[][2] = {
        { 4, 7 }, { 8, 14 }, { 12, 21 },
        { 16, 28 }, { 20, 35 }, { 24, 42 }
    };
    s32 offsets[] = { 2, 4, 6, -1 };

    debmenuSetMenu(labels, positions_table, offsets);
    expect_int(g_DMenuNumOptions, 6,
               "debug menu setup records the final group end as option count");
    expect_int(g_DMenuNumGroups, 3,
               "debug menu setup counts groups before the negative sentinel");

    set_highlighted_debug_option(0);
    expect_int(get_highlighted_debug_option(), 0,
               "debug menu selection setter updates the public selection");
    gotoAboveDebugOption();
    expect_int(get_highlighted_debug_option(), 1,
               "up from the first group wraps to its last option");
    gotoBelowDebugOption();
    expect_int(get_highlighted_debug_option(), 0,
               "down from the first group last option wraps to its first");

    set_highlighted_debug_option(2);
    gotoAboveDebugOption();
    expect_int(get_highlighted_debug_option(), 3,
               "up at a later group boundary wraps to that group tail");
    set_highlighted_debug_option(2);
    gotoBelowDebugOption();
    expect_int(get_highlighted_debug_option(), 3,
               "down enters the next option within a later group");
    gotoBelowDebugOption();
    expect_int(get_highlighted_debug_option(), 2,
               "down at a middle group boundary wraps to that group start");

    set_highlighted_debug_option(0);
    gotoRightDebugOption();
    expect_int(get_highlighted_debug_option(), 2,
               "right preserves the option offset in the next group");
    gotoRightDebugOption();
    expect_int(get_highlighted_debug_option(), 4,
               "right preserves the option offset in the final group");
    gotoRightDebugOption();
    expect_int(get_highlighted_debug_option(), 0,
               "right from the final group wraps to the first group");

    gotoLeftDebugOption();
    expect_int(get_highlighted_debug_option(), 4,
               "left from the first group wraps to the final group");
    gotoLeftDebugOption();
    expect_int(get_highlighted_debug_option(), 2,
               "left preserves the option offset in the previous group");
}

static void test_render_routes_scale_selection_and_highlight(void)
{
    char *labels[] = { "A", "B", "C", "D", "E", "F" };
    s32 positions_table[][2] = {
        { 4, 7 }, { 8, 14 }, { 12, 21 },
        { 16, 28 }, { 20, 35 }, { 24, 42 }
    };
    s32 offsets[] = { 2, 4, 6, -1 };
    Gfx display_list[1];

    debmenuSetMenu(labels, positions_table, offsets);
    debmenuSetScaleIndex(2);
    set_highlighted_debug_option(3);
    reset_calls();

    expect_true(debugmenuRender(display_list) == display_list,
                "debug menu renderer preserves the display-list cursor");
    expect_int(colour_calls, 6,
               "debug menu renderer visits each configured option");
    expect_int(position_calls, 6,
               "debug menu renderer positions each configured option");
    expect_int(string_calls, 6,
               "debug menu renderer prints each configured label");
    expect_true(colours[3][0] == 0xC0 && colours[3][1] == 0 &&
                    colours[3][2] == 0 && colours[3][3] == 0xC0,
                "debug menu renderer uses the red highlight colour");
    expect_true(colours[2][0] == 0xC0 && colours[2][1] == 0xC0 &&
                    colours[2][2] == 0xC0 && colours[2][3] == 0xC0,
                "debug menu renderer uses the grey inactive colour");
    expect_int(positions[0][0], -1,
               "debug menu renderer applies the selected X scale");
    expect_int(positions[0][1], 5,
               "debug menu renderer applies the selected Y scale");
    expect_true(strings[3] == (const unsigned char *)labels[3],
                "debug menu renderer forwards labels without changing ownership");

    reset_calls();
    sub_GAME_7F09000C(0, 0, 0);
    expect_int(colour_calls, 6,
               "legacy debug menu renderer visits each option");
    expect_int(position_calls, 6,
               "legacy debug menu renderer positions each option");
    expect_int(string_calls, 6,
               "legacy debug menu renderer prints each option");
    expect_int(error_calls, 6,
               "legacy debug menu renderer reports its font fallback per option");
}

int main(void)
{
    test_grouped_navigation_wraps_without_crossing_groups();
    test_render_routes_scale_selection_and_highlight();

    if (failures != 0)
    {
        fprintf(stderr, "%d debug-menu contract(s) failed\n", failures);
        return 1;
    }

    puts("debug-menu suite passed");
    return 0;
}
