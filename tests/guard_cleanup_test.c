#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "chr.h"

void cleanupGuardData(void);

static ChrRecord guard_slots[3];
ChrRecord *g_ChrSlots = guard_slots;
s32 g_NumChrSlots;

static PropRecord *cleanup_order[3][4];
static int cleanup_order_count;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void chrpropCleanupForRemoval(PropRecord *prop)
{
    cleanup_order[cleanup_order_count][0] = prop;
}

void chrpropDelist(PropRecord *prop)
{
    cleanup_order[cleanup_order_count][1] = prop;
}

void chrpropDisable(PropRecord *prop)
{
    cleanup_order[cleanup_order_count][2] = prop;
}

void chrpropFree(PropRecord *prop)
{
    cleanup_order[cleanup_order_count][3] = prop;
    cleanup_order_count++;
}

int main(void)
{
    Model model = {};
    PropRecord prop_a = {};
    PropRecord prop_b = {};

    memset(guard_slots, 0, sizeof(guard_slots));
    memset(cleanup_order, 0, sizeof(cleanup_order));
    cleanup_order_count = 0;
    guard_slots[0].model = &model;
    guard_slots[0].prop = &prop_a;
    guard_slots[1].model = NULL;
    guard_slots[1].prop = &prop_b;
    guard_slots[2].model = &model;
    guard_slots[2].prop = &prop_b;
    g_NumChrSlots = 3;

    cleanupGuardData();

    expect(cleanup_order_count == 2,
           "guard cleanup visits only modeled guard slots");
    expect(cleanup_order[0][0] == &prop_a && cleanup_order[0][1] == &prop_a &&
               cleanup_order[0][2] == &prop_a && cleanup_order[0][3] == &prop_a &&
               cleanup_order[1][0] == &prop_b && cleanup_order[1][1] == &prop_b &&
               cleanup_order[1][2] == &prop_b && cleanup_order[1][3] == &prop_b,
           "guard cleanup applies all prop teardown stages in order");

    cleanup_order_count = 0;
    g_NumChrSlots = 0;
    cleanupGuardData();
    expect(cleanup_order_count == 0,
           "guard cleanup is a no-op when there are no guard slots");

    if (failures != 0)
    {
        fprintf(stderr, "%d guard-cleanup contract(s) failed\n", failures);
        return 1;
    }

    puts("guard-cleanup suite passed");
    return 0;
}
