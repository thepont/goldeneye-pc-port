#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <ultra64.h>
#include <memp.h>
#include "chrai.h"
#include "propobj.h"
#include "initobjects.h"

void alloc_lookup_buffers(void);
void write_monitor_ani_control_blocks(void);
void initialize_proxy_mine_table(void);

u32 monAnim00Bond[1];
u32 monAnim34[1];
u32 monAnim35Taser[1];
WeaponObjRecord *proxy_mine_table[30];
f32 gasTimeToFullOpacity;
u32 gasDoesDamageFlag;
f32 difficulty;
stagesetup g_CurrentSetup;
s16 *ptr_list_object_lookup_indices;
PropRecord g_Props[MAX_PROPS];
PropRecord *g_ActivePropsTail;
PropRecord *g_ActivePropsHead;
PropRecord *g_FreeProps;
s32 g_OnScreenPropCount;
PropRecord **g_LastOnScreenProp;
PropRecord *g_OnScreenPropList[ONSCREEN_PROP_LIST_LEN];
PropRecord *g_InteractProp;
s16 *RoomPropListBlockIndices;
struct roomproplistblock *RoomPropListBlocks;
sfxRecord sfx_related[SFX_RELATED_LEN];
struct WeaponObjRecord g_WeaponSlots[MAX_WEAPON_SLOTS];
struct HatRecord g_HatSlots[MAX_HAT_SLOTS];
struct AmmoCrateRecord g_AmmoCrates[MAX_AMMO_CRATES];
struct Projectile g_Projectiles[PROJECTILES_ARR_MAX];
struct Embedment g_Embedments[EMBEDMENT_ARR_MAX];
coord3d flt_CODE_bss_80075B78;
coord3d flt_CODE_bss_80075B88;
MonitorRecord g_MonitorAnimController;
struct object_animation_controller g_UnknownAnimController;
struct object_animation_controller g_TaserAnimController;
stagesetup *g_ptrStageSetupFile;
char *setup_text_pointers[1];
s32 g_MaxNumRooms;

f32 F_80030B14;
f32 F_80030B18;
f32 g_AutogunPendingDamageTick;
f32 g_AutogunDamageScalar;
f32 F_80030B24;
f32 g_SoloAmmoMultiplier;
s32 alarm_timer;
s32 *ptr_alarm_sfx;
f32 toxic_gas_sound_timer;
s32 activate_gas_sound_timer;
coord3d gasLeakSource;
s32 D_80030ADC;
f32 gasLeakTimer;
ALSoundState *ptr_gas_sound;
s32 clock_drawn_flag;
s32 clock_enable;
f32 clock_time;
s32 g_RemoteMineOwnerTriggerFlag;
s32 g_NextWeaponSlot;
s32 g_NextHatSlot;
struct LinkRecord *g_LevelLoadPropSwitch;
LockDoorRecord *g_LevelLoadPropLockDoor;
SafeObjectRecord *g_LevelLoadPropSafeItem;
PropRecord *D_80030B0C;
s32 bodypartshot;

static union
{
    long double alignment;
    u8 bytes[0x10000];
} allocation_storage;
static size_t allocation_offset;
static int allocation_calls;
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
    void *result;
    (void)bank;
    allocation_calls++;
    result = &allocation_storage.bytes[allocation_offset];
    allocation_offset += bytes;
    return result;
}

static void test_sound_and_lookup_initialization(void)
{
    int i;

    for (i = 0; i < SFX_RELATED_LEN; ++i)
    {
        sfx_related[i].state = (ALSoundState *)(uintptr_t)0x1;
    }
    init_sound_effects_registers();
    for (i = 0; i < SFX_RELATED_LEN; ++i)
    {
        expect(sfx_related[i].state == AL_STOPPED,
               "object initialization stops every registered sound effect");
    }

    g_MaxNumRooms = 3;
    allocation_offset = 0;
    allocation_calls = 0;
    alloc_lookup_buffers();
    expect(allocation_calls == 3 && ptr_list_object_lookup_indices[0] == -1,
           "object initialization allocates and terminates the lookup index table");
    for (i = 0; i < g_MaxNumRooms; ++i)
    {
        expect(RoomPropListBlockIndices[i] == -1,
               "object initialization clears every room lookup index");
    }
    expect(RoomPropListBlocks[0].propnums[0] == -2 &&
               RoomPropListBlocks[0].propnums[1] == -1 &&
               RoomPropListBlocks[255].propnums[15] == -1,
           "object initialization writes room-block sentinels across the full table");
}

static void test_load_table_and_menu_reset(void)
{
    LinkRecord switch_link = {0};
    LockDoorRecord lock_link = {0};
    SafeObjectRecord safe_item = {0};
    int i;

    difficulty = 9.0f;
    g_ActivePropsHead = (PropRecord *)(uintptr_t)0x1;
    g_ActivePropsTail = (PropRecord *)(uintptr_t)0x2;
    g_OnScreenPropCount = 9;
    g_OnScreenPropList[0] = (PropRecord *)(uintptr_t)0x3;
    g_LastOnScreenProp = (PropRecord **)(uintptr_t)0x4;
    for (i = 0; i < MAX_PROPS - 1; ++i)
    {
        g_Props[i].prev = NULL;
    }
    allocation_offset = 0;
    init_load_objpos_table();
    expect(difficulty == 1.0f && g_ActivePropsHead == NULL &&
               g_ActivePropsTail == NULL && g_OnScreenPropCount == 0 &&
               g_OnScreenPropList[0] == NULL && g_LastOnScreenProp ==
                   (PropRecord **)g_OnScreenPropList && g_FreeProps == g_Props,
           "object initialization resets the active/free property lists");
    expect(g_Props[0].prev == &g_Props[1] &&
               g_Props[MAX_PROPS - 2].prev == &g_Props[MAX_PROPS - 1],
           "object initialization links every property slot to its successor");

    g_LevelLoadPropSwitch = NULL;
    g_LevelLoadPropLockDoor = NULL;
    g_LevelLoadPropSafeItem = NULL;
    initSetLevelLoadPropSwitch(&switch_link);
    initSetLevelLoadPropLockDoor(&lock_link);
    initSetLevelLoadPropSafeItem(&safe_item);
    expect(switch_link.next == NULL && g_LevelLoadPropSwitch == &switch_link &&
               lock_link.next == NULL && g_LevelLoadPropLockDoor == &lock_link &&
               safe_item.next == NULL && g_LevelLoadPropSafeItem == &safe_item,
           "object initialization prepends switch, lock-door, and safe-item records");
}

static void test_reinit_clears_runtime_tables(void)
{
    int i;

    alarm_timer = 9;
    ptr_alarm_sfx = (s32 *)(uintptr_t)0x1;
    toxic_gas_sound_timer = 9.0f;
    activate_gas_sound_timer = 9;
    gasLeakSource.f[0] = 9.0f;
    gasLeakSource.f[1] = 9.0f;
    gasLeakSource.f[2] = 9.0f;
    D_80030ADC = 9;
    gasLeakTimer = 9.0f;
    ptr_gas_sound = (ALSoundState *)(uintptr_t)0x1;
    clock_drawn_flag = 0;
    clock_enable = 9;
    clock_time = 9.0f;
    g_RemoteMineOwnerTriggerFlag = 9;
    g_NextWeaponSlot = 9;
    g_NextHatSlot = 9;
    D_80030B0C = (PropRecord *)(uintptr_t)0x1;
    bodypartshot = 9;
    for (i = 0; i < MAX_WEAPON_SLOTS; ++i)
    {
        g_WeaponSlots[i].prop = (PropRecord *)(uintptr_t)0x1;
    }
    for (i = 0; i < MAX_HAT_SLOTS; ++i)
    {
        g_HatSlots[i].prop = (PropRecord *)(uintptr_t)0x1;
    }
    for (i = 0; i < MAX_AMMO_CRATES; ++i)
    {
        g_AmmoCrates[i].prop = (PropRecord *)(uintptr_t)0x1;
    }
    for (i = 0; i < PROJECTILES_ARR_MAX; ++i)
    {
        g_Projectiles[i].flags = 0;
        g_Projectiles[i].sounds[0] = (struct ALSoundState *)(uintptr_t)0x1;
        g_Projectiles[i].sounds[1] = (struct ALSoundState *)(uintptr_t)0x1;
    }
    for (i = 0; i < EMBEDMENT_ARR_MAX; ++i)
    {
        g_Embedments[i].flags = 0;
    }
    initialize_proxy_mine_table();
    reinit_between_menus();

    expect(alarm_timer == 0 && ptr_alarm_sfx == NULL &&
               toxic_gas_sound_timer == 0.0f && activate_gas_sound_timer == 0 &&
               gasLeakSource.f[0] == 0.0f && gasLeakSource.f[1] == 0.0f &&
               gasLeakSource.f[2] == 0.0f && gasLeakTimer == 0.0f &&
               ptr_gas_sound == NULL && clock_drawn_flag == 1 && clock_enable == 0 &&
               clock_time == 0.0f && g_RemoteMineOwnerTriggerFlag == 0 &&
               g_NextWeaponSlot == 0 && g_NextHatSlot == 0 && bodypartshot == -1,
           "menu reinitialization resets global object timers and ownership state");
    expect(g_WeaponSlots[0].prop == NULL && g_WeaponSlots[MAX_WEAPON_SLOTS - 1].prop == NULL &&
               g_HatSlots[0].prop == NULL && g_HatSlots[MAX_HAT_SLOTS - 1].prop == NULL &&
               g_AmmoCrates[0].prop == NULL &&
               g_AmmoCrates[MAX_AMMO_CRATES - 1].prop == NULL,
           "menu reinitialization clears every weapon, hat, and ammo-crate slot");
    expect(g_Projectiles[0].flags == 0x80000000 &&
               g_Projectiles[0].sounds[0] == NULL &&
               g_Projectiles[PROJECTILES_ARR_MAX - 1].sounds[1] == NULL &&
               g_Embedments[0].flags == 1 &&
               g_Embedments[EMBEDMENT_ARR_MAX - 1].flags == 1 &&
               proxy_mine_table[0] == NULL && proxy_mine_table[29] == NULL,
           "menu reinitialization restores projectile, embedment, and mine sentinels");
    expect(F_80030B14 == 1.0f && F_80030B18 == 1.0f &&
               g_AutogunPendingDamageTick == 1.0f && g_AutogunDamageScalar == 1.0f &&
               F_80030B24 == 1.0f && g_SoloAmmoMultiplier == 1.0f,
           "menu reinitialization restores damage multipliers");
}

static void test_monitor_controller_copies(void)
{
    write_monitor_ani_control_blocks();
    expect(g_MonitorAnimController.cmdlist == monAnim00Bond &&
               g_UnknownAnimController.ptranimation == monAnim34 &&
               g_TaserAnimController.ptranimation == monAnim35Taser,
           "monitor initialization copies each canonical animation controller");
}

int main(void)
{
    test_sound_and_lookup_initialization();
    test_load_table_and_menu_reset();
    test_reinit_clears_runtime_tables();
    test_monitor_controller_copies();

    if (failures != 0)
    {
        fprintf(stderr, "%d object-initialization contract(s) failed\n", failures);
        return 1;
    }

    puts("object-initialization suite passed");
    return 0;
}
