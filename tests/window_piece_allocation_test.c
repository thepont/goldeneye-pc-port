#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "bondconstants.h"
#include "glass.h"

void alloc_shattered_window_pieces(void);

s32 SHATTERED_WINDOW_PIECES_BUFFER_LEN;
s_shattered_window_piece *ptr_shattered_window_pieces;
s32 g_NextShardNum;

static unsigned char allocation[128 * 1024];
static u32 allocation_bytes;
static int configured_players;
static s32 configured_stage;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    (void)bank;
    allocation_bytes = bytes;
    return allocation;
}

s32 getPlayerCount(void)
{
    return configured_players;
}

s32 lvlGetCurrentStageToLoad(void)
{
    return configured_stage;
}

static void reset_fixture(void)
{
    memset(allocation, 0xA5, sizeof(allocation));
    allocation_bytes = 0;
    SHATTERED_WINDOW_PIECES_BUFFER_LEN = -1;
    ptr_shattered_window_pieces = NULL;
    g_NextShardNum = 99;
}

static void test_normal_and_level_specific_budgets(void)
{
    reset_fixture();
    configured_players = 1;
    configured_stage = LEVELID_DAM;
    alloc_shattered_window_pieces();
    expect(SHATTERED_WINDOW_PIECES_BUFFER_LEN == 200,
           "solo non-special levels keep the full shard budget");
    expect(allocation_bytes ==
               ((200u * sizeof(s_shattered_window_piece) + 0xfu) & ~0xfu),
           "shard allocation rounds its byte count to 16 bytes");
    expect(ptr_shattered_window_pieces[0].active == 0 &&
               ptr_shattered_window_pieces[199].active == 0 &&
               g_NextShardNum == 0,
           "shard allocation clears every slot and resets the ring cursor");

    reset_fixture();
    configured_players = 2;
    configured_stage = LEVELID_STREETS;
    alloc_shattered_window_pieces();
    expect(SHATTERED_WINDOW_PIECES_BUFFER_LEN == 50,
           "streets multiplayer applies the reduced shard budget");
    expect(ptr_shattered_window_pieces[0].active == 0 &&
               ptr_shattered_window_pieces[49].active == 0,
           "reduced shard allocation initializes its complete active range");
}

static void test_zero_player_startup_is_safe(void)
{
    reset_fixture();
    configured_players = 0;
    configured_stage = LEVELID_DAM;

    alloc_shattered_window_pieces();

    expect(SHATTERED_WINDOW_PIECES_BUFFER_LEN == 200,
           "window allocation falls back to a solo budget before players exist");
    expect(ptr_shattered_window_pieces != NULL && allocation_bytes != 0,
           "window allocation still creates storage before player assignment");
}

int main(void)
{
    test_normal_and_level_specific_budgets();
    test_zero_player_startup_is_safe();

    if (failures != 0)
    {
        fprintf(stderr, "%d window-piece allocation contract(s) failed\n", failures);
        return 1;
    }

    puts("window-piece allocation suite passed");
    return 0;
}
