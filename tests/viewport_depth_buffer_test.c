#include <stdio.h>
#include <stdint.h>

#include <ultra64.h>
#include "fr.h"

void zbufAllocate(void);
void zbufDeallocate(void);
void zbufSetBuffer(u8 *buffer, s32 width, s32 height);
Gfx *zbufInit(Gfx *gdl);
Gfx *zbufClearCurrentPlayer(Gfx *gdl);

extern u8 *z_buffer;
extern s32 z_buffer_width;
extern s32 z_buffer_height;
s32 resolution;
static int player_count;
static int current_player;
static size_t allocation_offset;
static u32 allocation_size;
static union
{
    long double alignment;
    u8 bytes[0x100000 + 64];
} allocation_storage;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void *mempAllocBytesInBank(u32 size, u8 bank)
{
    (void)bank;
    allocation_size = size;
    return &allocation_storage.bytes[7 + allocation_offset];
}

s32 getPlayerCount(void)
{
    return player_count;
}

s32 get_cur_playernum(void)
{
    return current_player;
}

static void test_depth_buffer_allocation_modes(void)
{
    uintptr_t raw;

    resolution = 1;
    player_count = 1;
    allocation_offset = 0;
    z_buffer = NULL;
    zbufAllocate();
    raw = (uintptr_t)&allocation_storage.bytes[7];
    expect(z_buffer == (u8 *)((raw + 0x3fU) & ~0x3fU) &&
               z_buffer_width == Z_BUFFER_4_3_WIDTH &&
               z_buffer_height == Z_BUFFER_4_3_HEIGHT &&
               allocation_size == (u32)(Z_BUFFER_4_3_WIDTH * Z_BUFFER_4_3_HEIGHT * 2 + 64),
           "depth-buffer allocation uses the 4:3 dimensions and preserves pointer width");

    resolution = 0;
    player_count = 1;
    allocation_offset = 0;
    zbufAllocate();
    expect(z_buffer_width == SCREEN_WIDTH && z_buffer_height == SCREEN_HEIGHT,
           "single-player depth-buffer allocation uses the single-player height");

    player_count = 2;
    allocation_offset = 0;
    zbufAllocate();
    expect(z_buffer_width == SCREEN_WIDTH && z_buffer_height == Z_BUFFER_HEIGHT,
           "multiplayer depth-buffer allocation uses the split-screen height");

    zbufDeallocate();
    expect(z_buffer == NULL, "depth-buffer deallocation clears the owned pointer");
}

static void test_depth_buffer_binding_and_commands(void)
{
    Gfx commands[8] = {{0}};
    Gfx *end;
    u8 external_buffer[0x1000];

    zbufSetBuffer(external_buffer, 320, 240);
    expect(z_buffer == external_buffer && z_buffer_width == 320 &&
               z_buffer_height == 240,
           "depth-buffer binding stores an external buffer and dimensions");

    current_player = 0;
    player_count = 1;
    end = zbufInit(commands);
    expect(end == commands + 2 && commands[1].words.w1 != 0,
           "depth-buffer initialization emits pipe-sync and depth-image commands");

    end = zbufClearCurrentPlayer(commands);
    expect(end == commands + 1 && commands[0].words.w0 ==
               _SHIFTL(0x44, 24, 8) && commands[0].words.w1 == 0,
           "PC depth-buffer clearing emits the dedicated depth-clear command");
}

int main(void)
{
    test_depth_buffer_allocation_modes();
    test_depth_buffer_binding_and_commands();

    if (failures != 0)
    {
        fprintf(stderr, "%d viewport-depth-buffer contract(s) failed\n", failures);
        return 1;
    }

    puts("viewport-depth-buffer suite passed");
    return 0;
}
