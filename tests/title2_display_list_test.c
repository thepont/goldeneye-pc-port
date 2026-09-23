#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <ultra64.h>
#include "bondtypes.h"
#include "title2.h"

Gfx *sub_GAME_7F01B6E0(Gfx *gdl, u8 *image, s32 arg2);

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

static size_t find_opcode(const Gfx *commands, size_t count, u8 wanted)
{
    size_t i;

    for (i = 0; i < count; ++i)
    {
        if (opcode(&commands[i]) == wanted)
        {
            return i;
        }
    }

    return count;
}

static size_t find_last_opcode(const Gfx *commands, size_t count, u8 wanted)
{
    size_t i = count;

    while (i > 0)
    {
        --i;
        if (opcode(&commands[i]) == wanted)
        {
            return i;
        }
    }

    return count;
}

static void test_folder_background_uses_both_horizontal_origins(void)
{
    enum { ROWS = 299, COMMANDS_PER_ROW = 11, COMMAND_COUNT = ROWS * COMMANDS_PER_ROW };
    static u8 image[440 * 300];
    static Gfx commands[COMMAND_COUNT];
    struct FolderSelectColour top = {10, 20, 30};
    struct FolderSelectColour bottom = {110, 120, 130};
    Gfx *end;
    size_t first_rectangle;
    size_t last_rectangle;
    size_t first_prim_color;
    size_t first_image_command;

    memset(image, 0x3c, sizeof(image));
    memset(commands, 0xa5, sizeof(commands));

    end = titleRenderFolderMenuBackgroundLines(commands, image, -5, &top, &bottom);

    expect(end == commands + COMMAND_COUNT,
           "folder background emits a fixed display-list budget for every scanline");

    first_image_command = find_opcode(commands, 20, (u8)G_SETTIMG);
    expect(first_image_command < 20 &&
               commands[first_image_command].words.w1 == (uintptr_t)image,
           "folder background begins each texture upload with the source image address");

    first_rectangle = find_opcode(commands, 20, (u8)G_TEXRECT);
    expect(first_rectangle < 20,
           "folder background emits a texture rectangle for the first scanline");
    if (first_rectangle < 20)
    {
        expect(((commands[first_rectangle].words.w1 >> 12) & 0xfff) == 0,
               "negative x-offset clamps the first rectangle to the left display edge");
        expect(((commands[first_rectangle].words.w1) & 0xfff) == (0x10 << 2),
               "folder background starts at the expected top scanline");

        first_prim_color = find_opcode(commands, first_rectangle, (u8)G_SETPRIMCOLOR);
        expect(first_prim_color < first_rectangle &&
                   commands[first_prim_color].words.w1 ==
                       ((uintptr_t)10 << 24 | (uintptr_t)20 << 16 |
                        (uintptr_t)30 << 8 | 255),
               "folder background starts its gradient at the supplied top color");
    }

    last_rectangle = find_last_opcode(commands, COMMAND_COUNT, (u8)G_TEXRECT);
    expect(last_rectangle < COMMAND_COUNT &&
               ((commands[last_rectangle].words.w1) & 0xfff) == ((298 + 0x10) << 2),
           "folder background ends on the final expected scanline");

    memset(commands, 0xa5, sizeof(commands));
    end = titleRenderFolderMenuBackgroundLines(commands, image, 5, &top, &bottom);
    first_rectangle = find_opcode(commands, 20, (u8)G_TEXRECT);

    expect(end == commands + COMMAND_COUNT,
           "positive x-offset preserves the same display-list budget");
    expect(first_rectangle < 20 &&
               ((commands[first_rectangle].words.w1 >> 12) & 0xfff) == (5 << 2),
           "positive x-offset is encoded as the rectangle's left coordinate");
}

static void test_secondary_background_builder_interpolates_shared_color(void)
{
    enum { ROWS = 217, COMMANDS_PER_ROW = 11, COMMAND_COUNT = ROWS * COMMANDS_PER_ROW };
    static u8 image[320 * 218 * 2];
    static Gfx commands[COMMAND_COUNT];
    Gfx *end;
    size_t first_rectangle;
    size_t first_prim_color;

    memset(image, 0x5a, sizeof(image));
    memset(commands, 0, sizeof(commands));

    end = sub_GAME_7F01B6E0(commands, image, 77);

    expect(end == commands + COMMAND_COUNT,
           "secondary title background emits every expected scanline command");

    first_rectangle = find_opcode(commands, 20, (u8)G_TEXRECT);
    expect(first_rectangle < 20 &&
               ((commands[first_rectangle].words.w1 >> 12) & 0xfff) == 0,
           "secondary background keeps its zero horizontal origin");
    if (first_rectangle < 20)
    {
        first_prim_color = find_opcode(commands, first_rectangle,
                                       (u8)G_SETPRIMCOLOR);
        expect(first_prim_color < first_rectangle &&
                   commands[first_prim_color].words.w1 ==
                       ((uintptr_t)77 << 24 | (uintptr_t)77 << 16 |
                        (uintptr_t)77 << 8 | 255),
               "secondary background initializes all gradient channels from arg2");
    }
}

int main(void)
{
    test_folder_background_uses_both_horizontal_origins();
    test_secondary_background_builder_interpolates_shared_color();

    if (failures != 0)
    {
        return 1;
    }

    puts("title2 display-list suite passed");
    return 0;
}
