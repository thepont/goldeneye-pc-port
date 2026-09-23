#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "chr.h"

void init_guards(void);
void alloc_init_GUARDdata_entries(s32 count);
void bodiesReset(u32 stagenum);

f32 animation_rate;
s32 D_8002C904;
s32 g_AnimationTablePointerCountRelated;
s32 D_8002C90C;
s32 D_8002C910;
s32 D_8002CC58;
s32 show_patrols_flag;
s32 player1_guardID;
ChrRecord *g_ChrSlots;
s32 g_NumChrSlots;

static ChrRecord guard_storage[64];
static u32 allocation_bytes;
static int init_register_calls;
static int failures;
static ModelFileHeader model_headers[2];
static ModelNode model_roots[2];
static u32 random_values[3];
static int random_index;

struct ChrModelFileRecord c_item_entries[3];
s32 list_of_bodies[8];
s32 random_male_heads[8];
s32 random_female_heads[8];
u32 num_bodies;
u32 num_male_heads;
u32 num_female_heads;
u32 current_random_body;
u32 current_random_male_head;
u32 current_random_female_head;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void init_obj_register_difficulty_vals(void)
{
    init_register_calls++;
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    (void)bank;
    allocation_bytes = bytes;
    return guard_storage;
}

u32 randomGetNext(void)
{
    return random_values[random_index++];
}

static void test_guard_state_reset(void)
{
    animation_rate = 4.0f;
    D_8002C904 = 1;
    g_AnimationTablePointerCountRelated = 2;
    D_8002C90C = 3;
    D_8002C910 = 4;
    D_8002CC58 = 5;
    show_patrols_flag = TRUE;
    player1_guardID = 7;
    g_ChrSlots = guard_storage;
    g_NumChrSlots = 9;
    init_register_calls = 0;

    init_guards();

    expect(animation_rate == 1.0f && D_8002C904 == 0 &&
               g_AnimationTablePointerCountRelated == 0 && D_8002C90C == 0 &&
               D_8002C910 == 0 && D_8002CC58 == 0 &&
               show_patrols_flag == FALSE && player1_guardID == 0x1388 &&
               g_ChrSlots == NULL && g_NumChrSlots == 0,
           "guard initialization resets the complete guard state");
    expect(init_register_calls == 1,
           "guard initialization resets objective difficulty values once");
}

static void test_guard_slot_allocation(void)
{
    int i;

    memset(guard_storage, 0xA5, sizeof(guard_storage));
    allocation_bytes = 0;
    alloc_init_GUARDdata_entries(3);

    expect(g_NumChrSlots == 13,
           "guard slot allocation includes the ten special guard slots");
    expect(allocation_bytes ==
               ((13u * sizeof(ChrRecord) + 0xfu) & ~0xfu),
           "guard slot allocation rounds its storage request");
    for (i = 0; i < 13; ++i)
    {
        expect(g_ChrSlots[i].model == NULL,
               "guard slot allocation clears every model pointer");
    }

    alloc_init_GUARDdata_entries(-11);
    expect(g_NumChrSlots == 10,
           "negative guard counts clamp to the ten special slots");
}

static void test_body_reset_updates_models_and_random_state(void)
{
    model_headers[0].RootNode = &model_roots[0];
    model_headers[1].RootNode = &model_roots[1];
    c_item_entries[0].header = &model_headers[0];
    c_item_entries[1].header = &model_headers[1];
    c_item_entries[2].header = NULL;

    num_male_heads = 3;
    num_female_heads = 2;
    num_bodies = 4;
    random_values[0] = 8;
    random_values[1] = 5;
    random_values[2] = 6;
    random_index = 0;
    current_random_male_head = 99;
    current_random_female_head = 99;
    current_random_body = 99;

    bodiesReset(0);

    expect(model_headers[0].RootNode == NULL && model_headers[1].RootNode == NULL,
           "body reset clears every loaded character model root");
    expect(random_index == 3 && current_random_male_head == 2 &&
               current_random_female_head == 1 && current_random_body == 2,
           "body reset chooses bounded random body and head indices");
}

static void test_body_reset_handles_empty_model_pools(void)
{
    num_male_heads = 0;
    num_female_heads = 0;
    num_bodies = 0;
    random_index = 0;
    current_random_male_head = 17;
    current_random_female_head = 18;
    current_random_body = 19;

    bodiesReset(0);

    expect(current_random_male_head == 0 && current_random_female_head == 0 &&
               current_random_body == 0,
           "body reset uses zero indices when a model pool is empty");
}

int main(void)
{
    test_guard_state_reset();
    test_guard_slot_allocation();
    test_body_reset_updates_models_and_random_state();
    test_body_reset_handles_empty_model_pools();

    if (failures != 0)
    {
        fprintf(stderr, "%d guard-initialization contract(s) failed\n", failures);
        return 1;
    }

    puts("guard-initialization suite passed");
    return 0;
}
