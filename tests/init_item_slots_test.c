#include <stdio.h>

#include <ultra64.h>
#include "bondinv.h"
#include "memp.h"
#include "player.h"

void reinit_gunheld_totaltime(void);
void alloc_additional_item_slots(s32 additionalentries);

struct player *g_CurrentPlayer;

static unsigned char allocation[1024];
static u32 allocation_bytes;
static u8 allocation_bank;
static int inventory_reset_calls;
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
    allocation_bytes = bytes;
    allocation_bank = bank;
    return allocation;
}

void bondinvReinitInv(void)
{
    inventory_reset_calls++;
}

static void test_reinitialize_gun_cache(void)
{
    struct player player = {};
    int i;

    g_CurrentPlayer = &player;
    player.equipallguns = TRUE;
    for (i = 0; i < 10; ++i)
    {
        player.gunheldarr[i].totaltime = i + 10;
    }

    reinit_gunheld_totaltime();

    expect(player.equipallguns == FALSE,
           "gun-cache reinitialization disables the all-guns flag");
    for (i = 0; i < 10; ++i)
    {
        expect(player.gunheldarr[i].totaltime == -1,
               "gun-cache reinitialization invalidates every cached time");
    }
}

static void test_allocate_item_slots(void)
{
    struct player player = {};

    g_CurrentPlayer = &player;
    inventory_reset_calls = 0;
    allocation_bytes = 0;
    allocation_bank = 0;

    alloc_additional_item_slots(1);

    expect(player.equipmaxitems == 0x1f,
           "item-slot allocation adds the fixed inventory baseline");
    expect(player.p_itemcur == (InvItem *)allocation,
           "item-slot allocation stores the returned inventory pointer");
    expect(allocation_bytes == 0x270,
           "item-slot allocation rounds the inventory byte count to 16 bytes");
    expect(allocation_bank == MEMPOOL_STAGE,
           "item-slot allocation uses the stage memory bank");
    expect(inventory_reset_calls == 1,
           "item-slot allocation reinitializes inventory exactly once");
}

int main(void)
{
    test_reinitialize_gun_cache();
    test_allocate_item_slots();

    if (failures != 0)
    {
        fprintf(stderr, "%d item-slot contract(s) failed\n", failures);
        return 1;
    }

    puts("item-slot edge suite passed");
    return 0;
}
