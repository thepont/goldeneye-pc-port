#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "bondtypes.h"

void initModelHitEntryFreeList(void);

#define MODEL_HIT_ENTRY_COUNT 600

char g_ModelHitEntries[MODEL_HIT_ENTRY_COUNT * sizeof(ModelHitEntry)]
    __attribute__((aligned(_Alignof(ModelHitEntry))));
char g_ModelHitEntriesPenultimate[1];
ModelHitEntry *g_ModelHitFreeList;

static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

int main(void)
{
    ModelHitEntry *entries = (ModelHitEntry *)g_ModelHitEntries;
    int i;

    memset(g_ModelHitEntries, 0xA5, sizeof(g_ModelHitEntries));
    g_ModelHitFreeList = NULL;

    initModelHitEntryFreeList();

    expect(g_ModelHitFreeList == &entries[0],
           "free-list head points to the first pool entry");
    expect(entries[0].prev == NULL,
           "free-list head has no predecessor after reinitialization");

    for (i = 0; i < MODEL_HIT_ENTRY_COUNT; ++i)
    {
        if (i + 1 < MODEL_HIT_ENTRY_COUNT)
        {
            expect(entries[i].next == &entries[i + 1],
                   "every non-tail entry links to the next pool entry");
        }
        else
        {
            expect(entries[i].next == NULL,
                   "free-list tail has no stale successor");
        }

        if (i > 0)
        {
            expect(entries[i].prev == &entries[i - 1],
                   "every non-head entry links to the previous pool entry");
        }
    }

    if (failures != 0)
    {
        fprintf(stderr, "%d model-hit free-list contract(s) failed\n", failures);
        return 1;
    }

    puts("model-hit free-list suite passed");
    return 0;
}
