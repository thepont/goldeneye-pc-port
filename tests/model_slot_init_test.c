#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "initunk_005520.h"
#include "model.h"
#include "objecthandler.h"

s32 g_MaxAnimModelSlots;
s32 g_MaxModelSlots;
s32 g_ModelIsLvResetting;
struct AnimModelSlot *g_AnimModelSlots;
struct ModelSlot *g_ModelSlots;

static unsigned char arena[256 * 1024];
static size_t arena_offset;
static int allocation_count;
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
    size_t aligned_offset = (arena_offset + 15u) & ~15u;
    (void)bank;
    allocation_count++;
    if (aligned_offset + bytes > sizeof(arena))
    {
        return NULL;
    }
    arena_offset = aligned_offset + bytes;
    return &arena[aligned_offset];
}

static void reset_allocator(void)
{
    memset(arena, 0xA5, sizeof(arena));
    arena_offset = 0;
    allocation_count = 0;
}

static void test_model_slots(void)
{
    s32 i;

    reset_allocator();
    modelmgrAllocateModelSlots(2);

    expect(g_MaxModelSlots == 32,
           "model-slot allocation reserves thirty spare slots");
    expect(g_ModelSlots != NULL && allocation_count == 31,
           "model-slot allocation creates the table and one pool per spare slot");
    for (i = 0; i < g_MaxModelSlots; ++i)
    {
        expect(g_ModelSlots[i].unk08 == 0,
               "model-slot allocation clears the in-use marker");
        if (i < 2)
        {
            expect(g_ModelSlots[i].unk10 == NULL,
                   "object-backed model slots do not claim spare rwdata");
        }
        else
        {
            expect(g_ModelSlots[i].unk10 != NULL &&
                       g_ModelSlots[i].unk02 == 0x38,
                   "spare model slots receive a host-sized rwdata pool");
        }
    }
}

static void test_animation_slots(void)
{
    s32 i;

    reset_allocator();
    modelmgrAllocateAnimModelSlots(1);

    expect(g_MaxAnimModelSlots == 11 && g_AnimModelSlots != NULL &&
               allocation_count == 11,
           "animation-slot allocation reserves ten spare slots and pools them");
    for (i = 0; i < g_MaxAnimModelSlots; ++i)
    {
        expect(g_AnimModelSlots[i].unk08 == 0,
               "animation-slot allocation clears the in-use marker");
        if (i == 0)
        {
            expect(g_AnimModelSlots[i].unk10 == NULL,
                   "animated object slots do not claim spare rwdata");
        }
        else
        {
            expect(g_AnimModelSlots[i].unk10 != NULL &&
                       g_AnimModelSlots[i].unk02 == 0xA8,
                   "spare animation slots receive their rwdata capacity");
        }
    }
}

static void test_state_and_invalid_counts(void)
{
    modelmgrResetSlotCounts();
    expect(g_MaxModelSlots == 0 && g_MaxAnimModelSlots == 0,
           "slot-count reset clears both model limits");
    modelmgrSetLevelResetting(TRUE);
    expect(g_ModelIsLvResetting == TRUE,
           "level-reset state setter stores true");
    modelmgrSetLevelResetting(FALSE);
    expect(g_ModelIsLvResetting == FALSE,
           "level-reset state setter stores false");

    reset_allocator();
    modelmgrAllocateModelSlots(-31);
    expect(g_MaxModelSlots == 30,
           "negative model counts clamp to the spare-slot baseline");
    reset_allocator();
    modelmgrAllocateAnimModelSlots(-11);
    expect(g_MaxAnimModelSlots == 10,
           "negative animation counts clamp to the spare-slot baseline");
}

int main(void)
{
    test_model_slots();
    test_animation_slots();
    test_state_and_invalid_counts();

    if (failures != 0)
    {
        fprintf(stderr, "%d model-slot initialization contract(s) failed\n", failures);
        return 1;
    }

    puts("model-slot initialization suite passed");
    return 0;
}
