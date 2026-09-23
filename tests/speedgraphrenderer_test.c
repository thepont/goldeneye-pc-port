#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "speedgraphrenderer.h"

extern f32 gCurrentGraphPosition;
extern f32 gNextGraphPosition;
Gfx *DrawGraphBarWithSimpleOffset(Gfx *gdl, f32 offset, s32 r, s32 g, s32 b);
Gfx *DrawAdjustedGraphBar(Gfx *gdl, f32 offset, s32 r, s32 g, s32 b);
Gfx *DrawGraphBarWithOffset(Gfx *gdl, f32 offset, s32 r, s32 g, s32 b,
                            s32 width, s32 height);

static int failures;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static u8 opcode(const Gfx *command)
{
    return (u8)(command->words.w0 >> 24);
}

static size_t count_opcode(const Gfx *commands, size_t count, u8 wanted)
{
    size_t i;
    size_t matches = 0;

    for (i = 0; i < count; i++)
    {
        if (opcode(&commands[i]) == wanted)
        {
            matches++;
        }
    }
    return matches;
}

static int fill_left(const Gfx *command)
{
    return (int)((command->words.w1 >> 14) & 0x3ff);
}

static int fill_top(const Gfx *command)
{
    return (int)((command->words.w1 >> 2) & 0x3ff);
}

static int fill_right(const Gfx *command)
{
    return (int)((command->words.w0 >> 14) & 0x3ff);
}

static int fill_bottom(const Gfx *command)
{
    return (int)((command->words.w0 >> 2) & 0x3ff);
}

static const Gfx *first_fill(const Gfx *commands, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (opcode(&commands[i]) == (u8)G_FILLRECT)
        {
            return &commands[i];
        }
    }
    return NULL;
}

static void reset_graph(void)
{
    gCurrentGraphPosition = 0.0f;
    gNextGraphPosition = 0.0f;
}

static void test_initialize_resets_positions_and_pipeline(void)
{
    Gfx commands[16];
    Gfx *end;

    gCurrentGraphPosition = 31.0f;
    gNextGraphPosition = 42.0f;
    memset(commands, 0, sizeof(commands));
    end = InitializeSpeedGraphDL(commands);

    expect_true(gCurrentGraphPosition == 0.0f && gNextGraphPosition == 0.0f,
                "speed graph initialization resets both graph cursors");
    expect_true(end > commands && count_opcode(commands, (size_t)(end - commands),
                                                (u8)G_SETOTHERMODE_H) > 0,
                "speed graph initialization emits pipeline state");
}

static void test_nonpositive_bar_and_delta_are_noops(void)
{
    Gfx commands[16];
    Gfx *end;

    reset_graph();
    memset(commands, 0xa5, sizeof(commands));
    end = DrawGraphBar(commands, 0.0f, 1, 2, 3, 4, 5);
    expect_true(end == commands && gNextGraphPosition == 0.0f,
                "a zero-width graph bar does not consume commands or advance time");
    end = DrawGraphBar(commands, -1.0f, 1, 2, 3, 4, 5);
    expect_true(end == commands && gNextGraphPosition == 0.0f,
                "a negative graph-bar offset is ignored");

    UpdateGraphPosition(0.0f);
    UpdateGraphPosition(-4.0f);
    expect_true(gNextGraphPosition == 0.0f,
                "non-positive graph deltas do not move the graph cursor");
}

static void test_bar_clips_to_segment_bounds_and_advances(void)
{
    Gfx commands[16];
    Gfx *end;
    const Gfx *bar;

    reset_graph();
    memset(commands, 0, sizeof(commands));
    end = DrawGraphBar(commands, 5.0f, 255, 0, 0, 2, 1);
    bar = first_fill(commands, (size_t)(end - commands));

    expect_true(gNextGraphPosition == 5.0f,
                "a positive bar advances the next graph cursor");
    expect_true(count_opcode(commands, (size_t)(end - commands), (u8)G_FILLRECT) == 1,
                "a bar within one segment emits one fill rectangle");
    expect_true(bar != NULL && fill_left(bar) == 50 && fill_right(bar) == 159 &&
                    fill_top(bar) == 19 && fill_bottom(bar) == 20,
                "a one-segment bar uses the expected graph coordinates");
}

static void test_bar_crossing_segments_emits_each_clipped_piece(void)
{
    Gfx commands[32];
    Gfx *end;
    size_t i;
    size_t fill_count;

    reset_graph();
    memset(commands, 0, sizeof(commands));
    end = DrawGraphBar(commands, 20.0f, 0, 255, 0, 3, 2);
    fill_count = count_opcode(commands, (size_t)(end - commands), (u8)G_FILLRECT);

    expect_true(fill_count == 3,
                "a bar crossing the graph width emits one clipped piece per segment");
    expect_true(gNextGraphPosition == 20.0f,
                "a multi-segment bar advances by its complete offset");

    for (i = 0; i < (size_t)(end - commands); i++)
    {
        if (opcode(&commands[i]) == (u8)G_FILLRECT)
        {
            expect_true(fill_left(&commands[i]) >= 50 && fill_left(&commands[i]) <= 269 &&
                            fill_right(&commands[i]) >= fill_left(&commands[i]) &&
                            fill_right(&commands[i]) <= 269,
                        "every graph segment is clipped inside the graph rectangle");
        }
    }
}

static void test_position_helpers_share_the_same_cursor_rules(void)
{
    Gfx commands[32];
    Gfx *end;

    reset_graph();
    UpdateGraphPosition(23.0f);
    AlignGraphPosition();
    expect_true(gNextGraphPosition == 30.0f && gCurrentGraphPosition == 30.0f,
                "aligning a graph position rounds up to the next ten-unit boundary");

    reset_graph();
    memset(commands, 0, sizeof(commands));
    end = DrawGraphBarWithSimpleOffset(commands, 5.0f, 1, 2, 3);
    expect_true(count_opcode(commands, (size_t)(end - commands), (u8)G_FILLRECT) == 1,
                "the simple-offset helper delegates to the bar renderer");

    gCurrentGraphPosition = 5.0f;
    gNextGraphPosition = 0.0f;
    memset(commands, 0, sizeof(commands));
    end = DrawAdjustedGraphBar(commands, 0.0f, 1, 2, 3);
    expect_true(count_opcode(commands, (size_t)(end - commands), (u8)G_FILLRECT) == 1,
                "the adjusted-bar helper renders the current-to-next delta");

    gCurrentGraphPosition = 0.0f;
    gNextGraphPosition = 0.0f;
    memset(commands, 0, sizeof(commands));
    end = DrawGraphBarWithOffset(commands, 5.0f, 1, 2, 3, 4, 5);
    expect_true(count_opcode(commands, (size_t)(end - commands), (u8)G_FILLRECT) == 1,
                "the explicit-offset helper renders relative to the shared cursor");
}

static void test_vertical_lines_cover_visible_segments(void)
{
    Gfx commands[128];
    Gfx *end;

    reset_graph();
    memset(commands, 0, sizeof(commands));
    end = DrawVerticalLines(commands, 255, 255, 255);
    expect_true(count_opcode(commands, (size_t)(end - commands), (u8)G_FILLRECT) == 11,
                "vertical graph lines draw eleven markers in the first segment");

    gNextGraphPosition = 25.0f;
    memset(commands, 0, sizeof(commands));
    end = DrawVerticalLines(commands, 255, 255, 255);
    expect_true(count_opcode(commands, (size_t)(end - commands), (u8)G_FILLRECT) == 33,
                "vertical graph lines repeat markers for every visible segment");
}

int main(void)
{
    test_initialize_resets_positions_and_pipeline();
    test_nonpositive_bar_and_delta_are_noops();
    test_bar_clips_to_segment_bounds_and_advances();
    test_bar_crossing_segments_emits_each_clipped_piece();
    test_position_helpers_share_the_same_cursor_rules();
    test_vertical_lines_cover_visible_segments();

    if (failures != 0)
    {
        fprintf(stderr, "%d speed-graph test(s) failed\n", failures);
        return 1;
    }

    puts("speed-graph renderer suite passed");
    return 0;
}
