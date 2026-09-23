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

static void test_depth_buffer_image_offsets(void)
{
    enum { PLAYER_VIEW_HEIGHT = SCREEN_WIDTH * SCREEN_HEIGHT };
    static u8 depth_storage[PLAYER_VIEW_HEIGHT * 2 + PLAYER_VIEW_HEIGHT + 128];
    Gfx commands[2] = {{0}};
    u8 *base = depth_storage + PLAYER_VIEW_HEIGHT + 64;
    const struct
    {
        s32 players;
        s32 player;
        u8 *expected;
        const char *message;
    } cases[] = {
        {1, 0, base, "single-player depth image starts at the buffer base"},
        {2, 0, base, "top split-screen depth image starts at the buffer base"},
        {2, 1, base - PLAYER_VIEW_HEIGHT,
         "bottom split-screen depth image uses the preceding frame-sized slice"},
        {3, 1, base, "middle player keeps the base depth image"},
        {3, 2, base - PLAYER_VIEW_HEIGHT,
         "four-player-style lower view uses the preceding frame-sized slice"},
    };

    zbufSetBuffer(base, SCREEN_WIDTH, SCREEN_HEIGHT);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        player_count = cases[i].players;
        current_player = cases[i].player;
        commands[0].words.w0 = 0;
        commands[0].words.w1 = 0;
        commands[1].words.w0 = 0;
        commands[1].words.w1 = 0;

        expect(zbufInit(commands) == commands + 2 &&
                   commands[1].words.w0 == _SHIFTL(G_SETZIMG, 24, 8) &&
                   commands[1].words.w1 == (uintptr_t)cases[i].expected,
               cases[i].message);
    }
}

static void test_depth_buffer_init_allocates_lazily(void)
{
    Gfx commands[2] = {{0}};
    uintptr_t raw;

    resolution = 0;
    player_count = 2;
    current_player = 0;
    allocation_offset = 0;
    z_buffer = NULL;

    expect(zbufInit(commands) == commands + 2 && z_buffer != NULL,
           "depth-buffer initialization allocates when no buffer is bound");
    raw = (uintptr_t)&allocation_storage.bytes[7];
    raw = (raw + 0x3fU) & ~((uintptr_t)0x3fU);
    expect(commands[1].words.w1 == raw &&
               z_buffer_width == SCREEN_WIDTH &&
               z_buffer_height == Z_BUFFER_HEIGHT,
           "lazy depth-buffer initialization binds the allocated split-screen buffer");
}

int main(void)
{
    test_depth_buffer_allocation_modes();
    test_depth_buffer_binding_and_commands();
    test_depth_buffer_image_offsets();
    test_depth_buffer_init_allocates_lazily();

    if (failures != 0)
    {
        fprintf(stderr, "%d viewport-depth-buffer contract(s) failed\n", failures);
        return 1;
    }

    puts("viewport-depth-buffer suite passed");
    return 0;
}
