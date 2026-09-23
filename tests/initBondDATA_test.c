#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <memp.h>
#include <ultra64.h>

#include "bondconstants.h"
#include "bondview.h"
#include "gun.h"
#include "initBondDATA.h"
#include "player.h"

struct player fake_player;
struct player_data fake_player_data;
struct player *g_CurrentPlayer = &fake_player;
struct player_data *g_playerPerm = &fake_player_data;

u32 size_item_buffer[2];
WeaponStats sniperrifle_stats;
WeaponStats camera_stats;

static int failures;
static s32 fake_player_count;
static int allocation_calls;
static u32 allocation_sizes[2];
static u8 allocation_banks[2];
static ModelFileHeader fake_weapon_headers[2];
static int blend_calls;
static GUNHAND blend_hands[8];

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_float(f32 actual, f32 expected, const char *message)
{
    f32 difference = actual - expected;

    if (difference < -0.00001f || difference > 0.00001f)
    {
        fprintf(stderr, "FAIL: %s (actual %.6f expected %.6f)\n",
                message, actual, expected);
        failures++;
    }
}

static void reset_fixture(void)
{
    memset(&fake_player, 0xa5, sizeof(fake_player));
    memset(&fake_player_data, 0xa5, sizeof(fake_player_data));
    memset(&sniperrifle_stats, 0, sizeof(sniperrifle_stats));
    memset(&camera_stats, 0, sizeof(camera_stats));
    memset(allocation_sizes, 0, sizeof(allocation_sizes));
    memset(allocation_banks, 0, sizeof(allocation_banks));
    memset(blend_hands, 0, sizeof(blend_hands));
    memset(fake_weapon_headers, 0, sizeof(fake_weapon_headers));

    g_CurrentPlayer = &fake_player;
    g_playerPerm = &fake_player_data;
    fake_player_count = 1;
    size_item_buffer[0] = 0x1111;
    size_item_buffer[1] = 0x2222;
    sniperrifle_stats.Zoom = 17.5f;
    camera_stats.Zoom = 29.25f;

    fake_player.ptr_hand_weapon_buffer[GUNRIGHT] =
        (ModelFileHeader *)(uintptr_t)0x11112222;
    fake_player.ptr_hand_weapon_buffer[GUNLEFT] =
        (ModelFileHeader *)(uintptr_t)0x33334444;
    allocation_calls = 0;
    blend_calls = 0;
}

s32 getPlayerCount(void)
{
    return fake_player_count;
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    int call = allocation_calls;

    if (call < (int)(sizeof(allocation_sizes) / sizeof(allocation_sizes[0])))
    {
        allocation_sizes[call] = bytes;
        allocation_banks[call] = bank;
    }
    allocation_calls++;
    return call < 2 ? &fake_weapon_headers[call] : NULL;
}

void bgunCalculateBlend(enum GUNHAND handnum)
{
    if (blend_calls < (int)(sizeof(blend_hands) / sizeof(blend_hands[0])))
    {
        blend_hands[blend_calls] = handnum;
    }
    blend_calls++;
}

static void expect_common_player_reset(void)
{
    int i;

    expect_true(fake_player.hand_invisible[GUNRIGHT] == 0 &&
                    fake_player.hand_invisible[GUNLEFT] == 0,
                "player initialization clears both hand visibility flags");
    expect_true(fake_player.hand_item[GUNRIGHT] == ITEM_UNARMED &&
                    fake_player.hand_item[GUNLEFT] == ITEM_UNARMED,
                "player initialization equips both hands as unarmed");
    expect_true(fake_player.field_2A44[GUNRIGHT] == -1 &&
                    fake_player.field_2A44[GUNLEFT] == -1 &&
                    fake_player.lock_hand_model[GUNRIGHT] == 0 &&
                    fake_player.lock_hand_model[GUNLEFT] == 0,
                "player initialization clears per-hand selection state");
    expect_true(fake_player.hands[0].weaponnum_watchmenu == (ITEM_IDS)-1 &&
                    fake_player.hands[0].field_87E == 1 &&
                    fake_player.hands[0].field_8EC.m[0][0] == 1.0f &&
                    fake_player.hands[0].field_8EC.m[3][3] == 1.0f &&
                    fake_player.hands[0].sway_look_z == -1.0f &&
                    fake_player.hands[0].spring_look_z == -19.999996f &&
                    fake_player.hands[0].field_A40 == 1000.0f,
                "player initialization copies the complete hand default state");
    expect_true(memcmp(&fake_player.hands[0], &fake_player.hands[1],
                       sizeof(fake_player.hands[0])) == 0,
                "both hand records start from the same stable template");

    for (i = 0; i < 30; ++i)
    {
        expect_true(fake_player.ammoheldarr[i] == 0,
                    "player initialization clears every ammo slot");
    }
    for (i = 0; i < 7; ++i)
    {
        expect_true(fake_player_data.shot_count[i] == 0,
                    "player initialization clears every shot register");
    }

    expect_true(fake_player_data.killed_civilians == 0 &&
                    fake_player_data.kill_count == 0 &&
                    fake_player_data.killed_gg_owner_count == 0 &&
                    fake_player.deathcount == 0 &&
                    fake_player.num_suicides == 0,
                "player initialization clears death and kill counters");
    expect_float(fake_player.gunposamplitude, 1.0f,
                 "player initialization restores gun position amplitude");
    expect_float(fake_player.gunxamplitude, 1.0f,
                 "player initialization restores gun X amplitude");
    expect_true(fake_player.trigger_released == 0 &&
                    fake_player.trigger_down == 0 &&
                    fake_player.prev_trigger_down == 0 &&
                    fake_player.z_trigger_timer == 0 &&
                    fake_player.current_trigger_hand == 0,
                "player initialization clears trigger edge state");
    expect_true(fake_player.tileColor.rgba[0] == 0xff &&
                    fake_player.tileColor.rgba[1] == 0xff &&
                    fake_player.tileColor.rgba[2] == 0xff &&
                    fake_player.tileColor.rgba[3] == 0,
                "player initialization restores the opaque white tile color");
    expect_true(fake_player.resetshadecol == TRUE && fake_player.aimtype == 0,
                "player initialization requests a fresh shade and aim state");
    expect_float(fake_player.guncrossdamp, 0.9f,
                 "player initialization restores crosshair damping");
    expect_float(fake_player.gunaimdamp, 0.9f,
                 "player initialization restores gun aim damping");
    expect_float(fake_player.field_1010.f[1], -3.1415927f,
                 "player initialization restores the centered aim angle");
    expect_true(fake_player.copiedgoldeneye == FALSE &&
                    fake_player.magnetattracttime == -1 &&
                    fake_player.syncoffset == 0,
                "player initialization clears copied-device and sync state");
    expect_true(fake_player.gunammooff == FALSE && fake_player.gunsightmode == 2,
                "player initialization restores ammo display and sight mode");
    expect_float(fake_player.sniper_zoom, sniperrifle_stats.Zoom,
                 "player initialization copies sniper zoom from weapon data");
    expect_float(fake_player.camera_zoom, camera_stats.Zoom,
                 "player initialization copies camera zoom from weapon data");
    expect_true(blend_calls == 6 && blend_hands[0] == GUNRIGHT &&
                    blend_hands[1] == GUNRIGHT && blend_hands[2] == GUNRIGHT &&
                    blend_hands[3] == GUNLEFT && blend_hands[4] == GUNLEFT &&
                    blend_hands[5] == GUNLEFT,
                "player initialization recalculates each hand blend three times");
}

static void test_solo_allocates_both_weapon_buffers(void)
{
    reset_fixture();
    fake_player_count = 1;

    init_player_BONDdata_stats();

    expect_true(allocation_calls == 2 && allocation_sizes[0] == 0x1111 &&
                    allocation_sizes[1] == 0x2222 &&
                    allocation_banks[0] == MEMPOOL_STAGE &&
                    allocation_banks[1] == MEMPOOL_STAGE,
                "solo initialization allocates right and left weapon buffers");
    expect_true(fake_player.ptr_hand_weapon_buffer[GUNRIGHT] ==
                    &fake_weapon_headers[0] &&
                    fake_player.ptr_hand_weapon_buffer[GUNLEFT] ==
                        &fake_weapon_headers[1],
                "solo initialization publishes both allocated buffers");
    expect_common_player_reset();
}

static void test_multiplayer_allocates_only_right_buffer(void)
{
    ModelFileHeader *left_before;

    reset_fixture();
    fake_player_count = 2;
    left_before = fake_player.ptr_hand_weapon_buffer[GUNLEFT];

    init_player_BONDdata_stats();

    expect_true(allocation_calls == 1 && allocation_sizes[0] == 0x1111 &&
                    allocation_banks[0] == MEMPOOL_STAGE,
                "multiplayer initialization allocates only the right weapon buffer");
    expect_true(fake_player.ptr_hand_weapon_buffer[GUNRIGHT] ==
                    &fake_weapon_headers[0] &&
                    fake_player.ptr_hand_weapon_buffer[GUNLEFT] == left_before,
                "multiplayer initialization preserves the non-owned left buffer");
    expect_common_player_reset();
}

int main(void)
{
    test_solo_allocates_both_weapon_buffers();
    test_multiplayer_allocates_only_right_buffer();

    if (failures != 0)
    {
        fprintf(stderr, "%d Bond-data initialization contract(s) failed\n",
                failures);
        return 1;
    }

    puts("Bond-data initialization suite passed");
    return 0;
}
