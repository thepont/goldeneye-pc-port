#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "bondwalk2.h"

void draw_textured_rectangle(Gfx **gdlptr, f32 *position, f32 *size,
                             s32 width, s32 height, s32 flipXY, s32 flipX,
                             s32 flipY);

static int failures;

static void expect(int condition, const char *message)
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

static int rectangle_left(const Gfx *command)
{
    return (int)((command->words.w1 >> 12) & 0xfff);
}

static int rectangle_top(const Gfx *command)
{
    return (int)(command->words.w1 & 0xfff);
}

static void test_invalid_size_does_not_consume_commands(void)
{
    Gfx commands[2];
    Gfx *cursor = commands;
    f32 position[] = {10.0f, 10.0f};
    f32 invalid_size[] = {0.0f, 4.0f};

    memset(commands, 0xa5, sizeof(commands));
    draw_textured_rectangle(&cursor, position, invalid_size, 32, 16, 0, 0, 0);

    expect(cursor == commands && commands[0].words.w0 == (uintptr_t)0xa5a5a5a5a5a5a5a5ULL,
           "non-positive rectangle dimensions leave the display-list cursor untouched");
}

static void test_unflipped_rectangle_encodes_screen_coordinates(void)
{
    Gfx commands[8] = {{0}};
    Gfx *cursor = commands;
    f32 position[] = {40.0f, 30.0f};
    f32 size[] = {10.0f, 5.0f};

    draw_textured_rectangle(&cursor, position, size, 64, 32, 0, 0, 0);

    expect(cursor == commands + 5,
           "visible unflipped rectangles emit perspective state, rectangle, and restore state");
    expect(opcode(&commands[1]) == (u8)G_TEXRECT &&
               rectangle_left(&commands[1]) == 120 &&
               rectangle_top(&commands[1]) == 100,
           "unflipped rectangles encode their unclipped left and top coordinates");
    expect(opcode(&commands[4]) == (u8)G_SETOTHERMODE_H &&
               commands[4].words.w1 != commands[0].words.w1,
           "visible rectangles restore perspective correction after drawing");
}

static void test_clipping_adjusts_unflipped_texture_origin(void)
{
    Gfx commands[8] = {{0}};
    Gfx *cursor = commands;
    f32 position[] = {2.0f, 2.0f};
    f32 size[] = {5.0f, 5.0f};

    draw_textured_rectangle(&cursor, position, size, 64, 32, 0, 0, 0);

    expect(cursor == commands + 5 && opcode(&commands[1]) == (u8)G_TEXRECT,
           "clipped unflipped rectangles still emit a normal texture rectangle");
    expect(rectangle_left(&commands[1]) == 0 && rectangle_top(&commands[1]) == 0,
           "negative rectangle origins are clipped to the visible screen");
    expect(commands[2].words.w1 != 0 && commands[3].words.w1 != 0,
           "unflipped clipping writes adjusted texture coordinate increments");
}

static void test_flipped_rectangle_adjusts_axes_and_uses_flip_opcode(void)
{
    Gfx commands[8] = {{0}};
    Gfx *cursor = commands;
    f32 position[] = {2.0f, 2.0f};
    f32 size[] = {5.0f, 5.0f};

    draw_textured_rectangle(&cursor, position, size, 64, 32, 1, 1, 1);

    expect(cursor == commands + 5 && opcode(&commands[1]) == (u8)G_TEXRECTFLIP,
           "flipXY rectangles use the flipped texture-rectangle opcode");
    expect(rectangle_left(&commands[1]) == 0 && rectangle_top(&commands[1]) == 0,
           "flipXY clipping also clamps both negative screen origins");
    expect(commands[2].words.w0 != 0 && commands[3].words.w1 != 0,
           "horizontal and vertical flips alter texture coordinates and scale");
}

static void test_fully_offscreen_rectangles_emit_no_geometry(void)
{
    Gfx commands[8] = {{0}};
    Gfx *cursor = commands;
    f32 left_position[] = {-10.0f, 0.0f};
    f32 top_position[] = {0.0f, -10.0f};
    f32 size[] = {2.0f, 2.0f};

    draw_textured_rectangle(&cursor, left_position, size, 16, 16, 0, 0, 0);
    expect(cursor == commands + 2,
           "rectangles entirely left of the viewport emit only perspective state");

    cursor = commands;
    draw_textured_rectangle(&cursor, top_position, size, 16, 16, 0, 0, 0);
    expect(cursor == commands + 2,
           "rectangles entirely above the viewport emit only perspective state");
}

static void test_image_display_modes_and_invalid_size(void)
{
    Gfx commands[16] = {{0}};
    Gfx *cursor;
    f32 position[] = {40.0f, 30.0f};
    f32 size[] = {10.0f, 5.0f};
    f32 invalid_size[] = {-1.0f, 5.0f};

    cursor = commands;
    display_image_at_position(&cursor, position, size, 64, 32, 0, 0, 0,
                              1, 2, 3, 4, 1, 0);
    expect(cursor == commands + 7 && opcode(&commands[0]) == (u8)G_SETENVCOLOR &&
               opcode(&commands[1]) == (u8)G_SETCOMBINE,
           "mipmapped image mode emits environment and trilinear combine state");

    cursor = commands;
    display_image_at_position(&cursor, position, size, 64, 32, 0, 0, 0,
                              1, 2, 3, 4, 0, 1);
    expect(cursor == commands + 7 && opcode(&commands[1]) == (u8)G_SETCOMBINE,
           "highlight image mode emits its alternate combine state");

    cursor = commands;
    display_image_at_position(&cursor, position, size, 64, 32, 0, 0, 0,
                              1, 2, 3, 4, 0, 0);
    expect(cursor == commands + 7,
           "ordinary image mode emits the standard combine state and rectangle");

    cursor = commands;
    memset(commands, 0xa5, sizeof(commands));
    display_image_at_position(&cursor, position, invalid_size, 64, 32, 0, 0, 0,
                              1, 2, 3, 4, 0, 0);
    expect(cursor == commands && commands[0].words.w0 == (uintptr_t)0xa5a5a5a5a5a5a5a5ULL,
           "invalid image dimensions skip both combine setup and rectangle drawing");
}

int main(void)
{
    test_invalid_size_does_not_consume_commands();
    test_unflipped_rectangle_encodes_screen_coordinates();
    test_clipping_adjusts_unflipped_texture_origin();
    test_flipped_rectangle_adjusts_axes_and_uses_flip_opcode();
    test_fully_offscreen_rectangles_emit_no_geometry();
    test_image_display_modes_and_invalid_size();

    if (failures != 0)
    {
        return 1;
    }

    puts("bondwalk2 display suite passed");
    return 0;
}
