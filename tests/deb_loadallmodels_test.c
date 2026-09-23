#include <stdio.h>
#include <string.h>

#include <ultra64.h>

#include "aicommands2.h"
#include "bondtypes.h"
#include "chr.h"
#include "deb_loadallmodels.h"
#include "loadobjectmodel.h"
#include "chrai.h"
#include "memp.h"

stagesetup g_CurrentSetup;
ChrRecord *g_ActiveChrs;
s32 g_ActiveChrsCount;
s32 objectiveregisters1;
f32 g_AiAccuracyModifier;
f32 g_AiDamageModifier;
f32 g_AiHealthModifier;
f32 g_AiReactionSpeed;
s32 g_SeenBondRecentlyGuardCount;

static ChrRecord active_chr_storage[4];
static AIListRecord ai_lists[4];
static AIRecord ai_code[1];
static int allocation_calls;
static u32 allocated_size;
static u8 allocated_bank;
static int model_load_calls;
static s32 model_load_ids[32];
static int projectile_load_calls;
static ITEM_IDS projectile_load_ids[8];
static int body_load_calls;
static s32 body_load_ids[8];
static int failures;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void reset_fixture(void)
{
    memset(&g_CurrentSetup, 0, sizeof(g_CurrentSetup));
    memset(active_chr_storage, 0, sizeof(active_chr_storage));
    memset(ai_lists, 0, sizeof(ai_lists));
    memset(ai_code, 0, sizeof(ai_code));
    allocation_calls = 0;
    allocated_size = 0;
    allocated_bank = 0;
    model_load_calls = 0;
    memset(model_load_ids, 0, sizeof(model_load_ids));
    projectile_load_calls = 0;
    memset(projectile_load_ids, 0, sizeof(projectile_load_ids));
    body_load_calls = 0;
    memset(body_load_ids, 0, sizeof(body_load_ids));
    g_ActiveChrs = NULL;
    g_ActiveChrsCount = 0;
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    allocation_calls++;
    allocated_size = bytes;
    allocated_bank = bank;
    return active_chr_storage;
}

s32 modelLoad(s32 model_id)
{
    if (model_load_calls < (int)(sizeof(model_load_ids) /
                                 sizeof(model_load_ids[0])))
    {
        model_load_ids[model_load_calls] = model_id;
    }
    model_load_calls++;
    return 1;
}

u32 weaponLoadProjectileModels(ITEM_IDS item)
{
    if (projectile_load_calls < (int)(sizeof(projectile_load_ids) /
                                     sizeof(projectile_load_ids[0])))
    {
        projectile_load_ids[projectile_load_calls] = item;
    }
    projectile_load_calls++;
    return 1;
}

s32 load_body_head_if_not_loaded(s32 model)
{
    if (body_load_calls < (int)(sizeof(body_load_ids) /
                               sizeof(body_load_ids[0])))
    {
        body_load_ids[body_load_calls] = model;
    }
    body_load_calls++;
    return 1;
}

s32 chraiitemsize(u8 *command, s32 offset)
{
    (void)offset;

    switch (command[0])
    {
        case AI_TRYDroppingItem:
            return AI_TRYDroppingItem_LENGTH;
        case AI_TRYSpawningChrAtPad:
            return AI_TRYSpawningChrAtPad_LENGTH;
        case AI_TRYSpawningChrNextToChr:
            return AI_TRYSpawningChrNextToChr_LENGTH;
        case AI_TRYGiveMeItem:
            return AI_TRYGiveMeItem_LENGTH;
        case AI_TRYGiveMeHat:
            return AI_TRYGiveMeHat_LENGTH;
        default:
            return 1;
    }
}

static void test_objective_register_defaults(void)
{
    reset_fixture();
    objectiveregisters1 = 0x1234;
    g_AiAccuracyModifier = 9.0f;
    g_AiDamageModifier = 8.0f;
    g_AiHealthModifier = 7.0f;
    g_AiReactionSpeed = 6.0f;
    g_SeenBondRecentlyGuardCount = 5;

    init_obj_register_difficulty_vals();

    expect_true(objectiveregisters1 == 0 && g_AiAccuracyModifier == 1.0f &&
                    g_AiDamageModifier == 1.0f && g_AiHealthModifier == 1.0f &&
                    g_AiReactionSpeed == 1.0f &&
                    g_SeenBondRecentlyGuardCount == 0,
                "debug model initialization restores all AI difficulty defaults");
}

static void test_background_character_allocation_filters_ids(void)
{
    AIRecord low_id;
    AIRecord first_background;
    AIRecord second_background;

    reset_fixture();
    ai_lists[0].ailist = &low_id;
    ai_lists[0].ID = 0x0fff;
    ai_lists[1].ailist = &first_background;
    ai_lists[1].ID = 0x1000;
    ai_lists[2].ailist = &second_background;
    ai_lists[2].ID = 0x2000;
    ai_lists[3].ailist = NULL;
    g_CurrentSetup.ailists = ai_lists;

    alloc_false_GUARDdata_to_exec_global_action();

    expect_true(g_ActiveChrsCount == 2 && g_ActiveChrs == active_chr_storage,
                "background allocation counts only AI lists in the background range");
    expect_true(allocation_calls == 1 && allocated_bank == MEMPOOL_STAGE &&
                    allocated_size == ((2u * sizeof(ChrRecord) + 0xfu) & ~0xfu),
                "background allocation rounds the exact character storage request");
    expect_true(active_chr_storage[0].chrnum == 0xfe &&
                    active_chr_storage[0].ailist == &first_background &&
                    active_chr_storage[0].aioffset == 0 &&
                    active_chr_storage[0].aireturnlist == -1 &&
                    active_chr_storage[0].actiontype == ACT_NULL &&
                    active_chr_storage[1].ailist == &second_background,
                "background allocation initializes every selected character record");

    reset_fixture();
    g_ActiveChrs = active_chr_storage;
    g_ActiveChrsCount = 9;
    alloc_false_GUARDdata_to_exec_global_action();
    expect_true(g_ActiveChrs == NULL && g_ActiveChrsCount == 0 &&
                    allocation_calls == 0,
                "background allocation clears state and returns for an empty setup");
}

static int append_command(u8 *buffer, int offset, u8 command, u8 byte1,
                          u8 byte2, u8 byte3)
{
    buffer[offset] = command;
    buffer[offset + 1] = byte1;
    buffer[offset + 2] = byte2;
    buffer[offset + 3] = byte3;
    return offset + chraiitemsize(&buffer[offset], 0);
}

static void test_debug_command_walk_and_safe_empty_setup(void)
{
    u8 command_bytes[128];
    int offset = 0;

    reset_fixture();
    g_CurrentSetup.ailists = NULL;
    debug_object_load_all_models();
    expect_true(model_load_calls == 0 && body_load_calls == 0,
                "debug model walking accepts a setup with no AI lists");

    memset(command_bytes, 0, sizeof(command_bytes));
    offset = append_command(command_bytes, offset, AI_TRYDroppingItem,
                            0x01, 0x23, 0);
    offset = append_command(command_bytes, offset, AI_TRYSpawningChrAtPad,
                            0, 1, 0);
    offset = append_command(command_bytes, offset, AI_TRYSpawningChrNextToChr,
                            0, 1, 0);
    offset = append_command(command_bytes, offset, AI_TRYGiveMeItem,
                            0x02, 0x34, 0x56);
    offset = append_command(command_bytes, offset, AI_TRYGiveMeHat,
                            0x03, 0x45, 0);
    command_bytes[offset] = AI_EndList;

    ai_lists[0].ailist = (AIRecord *)command_bytes;
    ai_lists[1].ailist = NULL;
    g_CurrentSetup.ailists = ai_lists;
    debug_object_load_all_models();

    expect_true(model_load_calls == 3 && model_load_ids[0] == 0x0123 &&
                    model_load_ids[1] == 0x0234 && model_load_ids[2] == 0x0345,
                "debug model walking loads drop, held-item, and hat models");
    expect_true(body_load_calls == 4 && body_load_ids[0] == 0 &&
                    body_load_ids[1] == 1 && body_load_ids[2] == 0 &&
                    body_load_ids[3] == 1,
                "debug model walking loads both body/head combinations");
    expect_true(projectile_load_calls == 1 && projectile_load_ids[0] == 0x56,
                "debug model walking loads projectile models for held items");
}

static void test_debug_weapon_table_loads_every_projectile_model(void)
{
    reset_fixture();
    debug_weapon_load_table();

    expect_true(model_load_calls == 7 && model_load_ids[0] == PROJECTILES_TYPE_KNIFE &&
                    model_load_ids[1] == PROJECTILES_TYPE_GRENADE &&
                    model_load_ids[2] == PROJECTILES_TYPE_REMOTE_MINE &&
                    model_load_ids[3] == PROJECTILES_TYPE_PROX_MINE &&
                    model_load_ids[4] == PROJECTILES_TYPE_TIMED_MINE &&
                    model_load_ids[5] == PROJECTILES_TYPE_ROCKET_ROUND &&
                    model_load_ids[6] == PROJECTILES_TYPE_GLAUNCH_ROUND,
                "debug weapon loading visits every configured projectile model");
}

int main(void)
{
    test_objective_register_defaults();
    test_background_character_allocation_filters_ids();
    test_debug_command_walk_and_safe_empty_setup();
    test_debug_weapon_table_loads_every_projectile_model();

    if (failures != 0)
    {
        fprintf(stderr, "%d debug model-loading contract(s) failed\n", failures);
        return 1;
    }

    puts("debug model-loading suite passed");
    return 0;
}
