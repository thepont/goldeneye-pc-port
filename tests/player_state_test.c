#include <stdio.h>
#include <string.h>

#include <fr.h>
#include <memp.h>
#include <random.h>
#include <ultra64.h>

#include "bondconstants.h"
#include "bondtypes.h"
#include "bondview.h"
#include "gun.h"
#include "player.h"
#include "propobj.h"
#include "unk_092E50.h"

/* player.c's runtime dependencies are deliberately fake ports in this test.
 * The state transitions stay real, while allocation, randomness and VI state
 * remain deterministic and observable. */
struct hand D_8003FDA0;
s32 g_VisibleToGuardsFlag;
s32 obj_collision_flag;

static struct player allocated_players[MAX_MULTIPLAYER];
static u32 allocation_bytes[MAX_MULTIPLAYER];
static u8 allocation_banks[MAX_MULTIPLAYER];
static int allocation_count;

static u32 random_values[8];
static int random_index;

static s16 fake_view_left;
static s16 fake_view_top;
static s16 fake_view_width;
static s16 fake_view_height;

static int failures;

static int weapon_flag_calls;
static ChrRecord *weapon_flag_chr;
static GUNHAND weapon_flag_hand;
static ITEM_IDS current_weapon_ids[GUNHANDS];
static int generate_calls;
static ChrRecord *generated_chr;
static s32 generated_prop;
static ITEM_IDS generated_item;
static s32 generated_flags;

static f32 absolute_float(f32 value)
{
    return value < 0.0f ? -value : value;
}

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_s32(s32 actual, s32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %d, expected %d)\n",
                message, actual, expected);
        failures++;
    }
}

static void expect_u32(u32 actual, u32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %u, expected %u)\n",
                message, actual, expected);
        failures++;
    }
}

static void expect_float(f32 actual, f32 expected, const char *message)
{
    if (absolute_float(actual - expected) > 0.0002f)
    {
        fprintf(stderr, "FAIL: %s (actual %.6f, expected %.6f)\n",
                message, actual, expected);
        failures++;
    }
}

static void reset_fixture(void)
{
    int i;

    memset(allocated_players, 0, sizeof(allocated_players));
    memset(allocation_bytes, 0, sizeof(allocation_bytes));
    memset(allocation_banks, 0, sizeof(allocation_banks));
    memset(g_playerPlayerData, 0, sizeof(g_playerPlayerData));
    memset(&D_8003FDA0, 0, sizeof(D_8003FDA0));
    memset(random_values, 0, sizeof(random_values));

    for (i = 0; i < MAX_MULTIPLAYER; i++)
    {
        g_playerPointers[i] = NULL;
        array_PLAYER_IDs[i] = 0;
    }

    allocation_count = 0;
    random_index = 0;
    fake_view_left = 0;
    fake_view_top = 0;
    fake_view_width = 320;
    fake_view_height = SCREEN_HEIGHT;
    g_CurrentPlayer = NULL;
    g_playerPerm = NULL;
    player_num = PLAYER_1;
    random_byte = 0;
    g_VisibleToGuardsFlag = 0;
    obj_collision_flag = 0;

    weapon_flag_calls = 0;
    weapon_flag_chr = NULL;
    weapon_flag_hand = GUNRIGHT;
    current_weapon_ids[GUNRIGHT] = ITEM_UNARMED;
    current_weapon_ids[GUNLEFT] = ITEM_UNARMED;
    generate_calls = 0;
    generated_chr = NULL;
    generated_prop = -1;
    generated_item = ITEM_NOTHING;
    generated_flags = 0;
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    if (allocation_count >= MAX_MULTIPLAYER)
    {
        return NULL;
    }

    allocation_bytes[allocation_count] = bytes;
    allocation_banks[allocation_count] = bank;
    return &allocated_players[allocation_count++];
}

u32 randomGetNext(void)
{
    u32 value = random_values[random_index < 8 ? random_index : 7];
    random_index++;
    return value;
}

s16 viGetViewLeft(void)
{
    return fake_view_left;
}

s16 viGetViewTop(void)
{
    return fake_view_top;
}

s16 viGetViewWidth(void)
{
    return fake_view_width;
}

s16 viGetViewHeight(void)
{
    return fake_view_height;
}

void chrSetWeaponFlag4(ChrRecord *chr, GUNHAND hand)
{
    weapon_flag_calls++;
    weapon_flag_chr = chr;
    weapon_flag_hand = hand;
}

ITEM_IDS getCurrentPlayerWeaponId(GUNHAND hand)
{
    return current_weapon_ids[hand];
}

PropRecord *something_with_generating_object(ChrRecord *self, s32 propid,
                                              ITEM_IDS itemid, s32 flags,
                                              WeaponObjRecord *weapon,
                                              ItemModelFileRecord *prop_header)
{
    (void)weapon;
    (void)prop_header;
    generate_calls++;
    generated_chr = self;
    generated_prop = propid;
    generated_item = itemid;
    generated_flags = flags;
    return NULL;
}

static void test_default_perspective_and_reset(void)
{
    int i;

    reset_fixture();
    for (i = 0; i < MAX_MULTIPLAYER; i++)
    {
        g_playerPointers[i] = &allocated_players[i];
        array_PLAYER_IDs[i] = (PLAYER_ID)(MAX_MULTIPLAYER - i - 1);
    }
    g_CurrentPlayer = g_playerPointers[2];
    g_playerPerm = &g_playerPlayerData[2];
    player_num = 2;
    random_byte = 99;

    reset_play_data_ptrs();

    for (i = 0; i < MAX_MULTIPLAYER; i++)
    {
        expect(g_playerPointers[i] == NULL, "reset clears every player pointer");
        expect_s32(array_PLAYER_IDs[i], i, "reset restores player shuffle ids");
    }
    expect(g_CurrentPlayer == NULL, "reset clears current player");
    expect(g_playerPerm == NULL, "reset clears permanent player data pointer");
    expect_s32(player_num, PLAYER_1, "reset selects player one");
    expect_s32(random_byte, 0, "reset clears random byte");

    default_player_perspective_and_height();
    for (i = 0; i < MAX_MULTIPLAYER; i++)
    {
        expect_float(g_playerPlayerData[i].player_perspective_height, 1.0f,
                     "default perspective height is one");
        expect_float(g_playerPlayerData[i].handicap, 1.0f,
                     "default handicap is one");
    }
}

static void test_player_initialization(void)
{
    struct player *player;
    u32 expected_bytes;
    int expected_viewport_v;

    reset_fixture();
    D_8003FDA0.weaponnum = ITEM_WPPK;
    D_8003FDA0.weaponnum_watchmenu = ITEM_KNIFE;
    D_8003FDA0.previous_weapon = ITEM_TT33;
    random_byte = 7;
    initBONDdataforPlayer(PLAYER_2);

    expected_bytes = (sizeof(struct player) + 0xFU) & ~0xFU;
    expect_s32(allocation_count, 1, "player initialization allocates one state block");
    expect_u32(allocation_bytes[0], expected_bytes,
               "player initialization allocates the complete host player state");
    expect_s32(allocation_banks[0], MEMPOOL_STAGE,
               "player initialization uses the stage memory bank");

    player = g_playerPointers[PLAYER_2];
    expect(player != NULL, "player initialization publishes its pointer");
    expect_s32(player->cameramode, 0, "player camera starts in default mode");
    expect_float(player->pos2.z, 1.0f, "player camera position has unit z");
    expect_float(player->offset.y, 1.0f, "player camera offset has unit y");
    expect(player->prop == NULL, "player starts without a prop");
    expect_s32(player->bonddead, FALSE, "player starts alive");
    expect_float(player->bondhealth, 1.0f, "player starts at full health");
    expect_float(player->bondarmour, 0.0f, "player starts without armour");
    expect_float(player->vv_verta, -4.0f, "player starts with the default vertical view");
    expect_float(player->vv_verta360, -229.1831f,
                 "player stores the converted default vertical view");

#if defined(VERSION_EU)
    expect_float(player->field_3B8.z, 8.417509f, "PAL player field has PAL default");
    expected_viewport_v = 0x220;
#else
    expect_float(player->field_3B8.z, 9.999998f, "NTSC player field has NTSC default");
    expected_viewport_v = 0x1E0;
#endif
    expect_s32(player->viewx, 100, "player starts with the temporary view width");
    expect_s32(player->viewy, 100, "player starts with the temporary view height");
    expect_s32(player->viewleft, 0, "player starts at viewport x zero");
    expect_s32(player->viewtop, 0, "player starts at viewport y zero");
    expect_s32(player->viewports[0].vp.vscale[1], expected_viewport_v,
               "player viewport scale uses the region default");
    expect_s32(player->viewports[1].vp.vtrans[1], expected_viewport_v,
               "both player viewports use the region default");
    expect_s32(player->hand_item[GUNRIGHT], ITEM_UNARMED,
               "right hand starts unarmed");
    expect_s32(player->field_2A44[GUNLEFT], -1,
               "left hand model starts unset");
    expect(player->hands[GUNRIGHT].weaponnum == ITEM_WPPK,
           "right hand copies the default weapon state");
    expect(player->hands[GUNLEFT].previous_weapon == ITEM_TT33,
           "left hand copies the complete default weapon state");
    expect_float(player->fovy, 60.0f, "player starts with the default FOV");
    expect_float(player->aspect, ASPECT_RATIO, "player starts with the default aspect");
    expect_s32(player->curRoomIndex, -1, "player starts outside a room");
    expect_s32(player->field_29E0, 7, "player receives the current random byte");
    expect_s32(random_byte, 8, "player initialization advances the random byte");
    expect_s32(g_VisibleToGuardsFlag, TRUE,
               "player initialization enables guard visibility");
    expect_s32(obj_collision_flag, TRUE,
               "player initialization enables object collision");
}

static void test_player_initialization_paths(void)
{
    reset_fixture();
    random_values[0] = 0xAB;
    init_player_data_ptrs_construct_viewports(2);
    expect_s32(allocation_count, 2, "co-op initialization creates two player states");
    expect_s32(getPlayerCount(), 2, "co-op initialization exposes two players");
    expect_s32(random_byte, 0xAD, "co-op initialization assigns distinct random bytes");
    expect(g_CurrentPlayer == g_playerPointers[PLAYER_1],
           "co-op initialization selects player one");
    expect(g_playerPerm == &g_playerPlayerData[PLAYER_1],
           "co-op initialization selects player one statistics");
    expect_s32(get_cur_playernum(), PLAYER_1,
               "co-op initialization sets the current player id");

    reset_fixture();
    random_values[0] = 0xCD;
    fake_view_left = 12;
    fake_view_top = 34;
    fake_view_width = 640;
    fake_view_height = 480;
    init_player_data_ptrs_construct_viewports(0);
    expect_s32(allocation_count, 1, "single-player initialization creates one state");
    expect_s32(getPlayerCount(), 1, "single-player initialization exposes one player");
    expect_s32(g_CurrentPlayer->viewx, 640,
               "single-player initialization applies the VI view width");
    expect_s32(g_CurrentPlayer->viewy, 480,
               "single-player initialization applies the VI view height");
    expect_s32(g_CurrentPlayer->viewleft, 12,
               "single-player initialization applies the VI viewport x");
    expect_s32(g_CurrentPlayer->viewtop, 34,
               "single-player initialization applies the VI viewport y");
}

static void test_player_accessors_and_sparse_slots(void)
{
    struct player sparse_players[3];
    PropRecord props[3];

    reset_fixture();
    memset(sparse_players, 0, sizeof(sparse_players));
    memset(props, 0, sizeof(props));
    sparse_players[0].prop = &props[0];
    sparse_players[1].prop = &props[1];
    sparse_players[2].prop = &props[2];
    g_playerPointers[PLAYER_1] = &sparse_players[0];
    /* A disconnected player slot is legal while a co-op participant leaves.
     * The target player remains in slot three. */
    g_playerPointers[PLAYER_3] = &sparse_players[2];

    expect_s32(getPlayerCount(), 2, "player count counts occupied sparse slots");
    expect_s32(getPlayerPointerIndex(&props[2]), PLAYER_3,
               "player lookup searches every multiplayer slot");
    expect_s32(getPlayerPointerIndex(&props[1]), -1,
               "player lookup rejects an absent prop");

    g_playerPointers[PLAYER_2] = &sparse_players[1];
    set_cur_player(PLAYER_2);
    expect(g_CurrentPlayer == &sparse_players[1],
           "set current player publishes the selected state");
    expect(g_playerPerm == &g_playerPlayerData[PLAYER_2],
           "set current player publishes the selected statistics");
    set_cur_player_screen_size(800, 600);
    set_cur_player_viewport_size(40, 50);
    set_cur_player_fovy(72.5f);
    set_cur_player_aspect(4.0f / 3.0f);
    expect_s32(sparse_players[1].viewx, 800, "screen width setter updates current player");
    expect_s32(sparse_players[1].viewy, 600, "screen height setter updates current player");
    expect_s32(sparse_players[1].viewleft, 40, "viewport x setter updates current player");
    expect_s32(sparse_players[1].viewtop, 50, "viewport y setter updates current player");
    expect_float(get_cur_player_fovy(), 72.5f, "FOV getter returns current player FOV");
    expect_float(sparse_players[1].aspect, 4.0f / 3.0f,
                 "aspect setter updates current player");
}

static void test_held_item_prop_mapping(void)
{
    static const struct item_prop_mapping
    {
        ITEM_IDS item;
        PROP prop;
    } mappings[] = {
        { ITEM_KNIFE,         PROP_CHRKNIFE },
        { ITEM_WPPK,          PROP_CHRWPPK },
        { ITEM_SKORPION,      PROP_CHRSKORPION },
        { ITEM_AK47,          PROP_CHRKALASH },
        { ITEM_UZI,           PROP_CHRUZI },
        { ITEM_MP5K,          PROP_CHRMP5K },
        { ITEM_SPECTRE,       PROP_CHRSPECTRE },
        { ITEM_M16,           PROP_CHRM16 },
        { ITEM_FNP90,         PROP_CHRFNP90 },
        { ITEM_SHOTGUN,       PROP_CHRSHOTGUN },
        { ITEM_GRENADELAUNCH, PROP_CHRGRENADELAUNCH },
        { ITEM_GRENADE,       PROP_CHRGRENADE },
        { ITEM_REMOTEMINE,    PROP_CHRREMOTEMINE },
        { ITEM_PROXIMITYMINE, PROP_CHRPROXIMITYMINE },
        { ITEM_TIMEDMINE,     PROP_CHRTIMEDMINE },
        { ITEM_RUGER,         PROP_CHRRUGER },
        { ITEM_LASER,         PROP_CHRLASER },
        { ITEM_SNIPERRIFLE,   PROP_CHRSNIPERRIFLE },
        { ITEM_MP5KSIL,       PROP_CHRMP5KSIL },
        { ITEM_TT33,          PROP_CHRTT33 },
        { ITEM_WPPKSIL,       PROP_CHRWPPKSIL },
        { ITEM_THROWKNIFE,    PROP_CHRTHROWKNIFE },
        { ITEM_AUTOSHOT,      PROP_CHRAUTOSHOT },
        { ITEM_ROCKETLAUNCH,  PROP_CHRROCKETLAUNCH },
        { ITEM_GOLDENGUN,     PROP_CHRGOLDEN },
        { ITEM_SILVERWPPK,    PROP_CHRWPPK },
        { ITEM_GOLDWPPK,      PROP_CHRWPPK },
        { ITEM_TOKEN,         PROP_FLAG }
    };
    size_t i;

    for (i = 0; i < sizeof(mappings) / sizeof(mappings[0]); i++)
    {
        expect_s32(getPropForHeldItem(mappings[i].item), mappings[i].prop,
                   "held item maps to its multiplayer prop");
    }
    expect_s32(getPropForHeldItem(ITEM_UNARMED), -1,
               "unarmed has no held prop");
    expect_s32(getPropForHeldItem(ITEM_TASER), -1,
               "taser has no held prop");
}

static void test_shuffle_helpers(void)
{
    int i;
    int j;

    reset_fixture();
    for (i = 0; i < MAX_MULTIPLAYER; i++)
    {
        g_playerPointers[i] = &allocated_players[i];
    }
    shuffle_player_ids();
    expect_s32(random_index, MAX_MULTIPLAYER - 1,
               "shuffle consumes one random value per swap");
    for (i = 0; i < MAX_MULTIPLAYER; i++)
    {
        expect(array_PLAYER_IDs[i] >= 0 && array_PLAYER_IDs[i] < MAX_MULTIPLAYER,
               "shuffle keeps player ids in range");
        for (j = i + 1; j < MAX_MULTIPLAYER; j++)
        {
            expect(array_PLAYER_IDs[i] != array_PLAYER_IDs[j],
                   "shuffle keeps every player id unique");
        }
    }

    array_PLAYER_IDs[0] = PLAYER_3;
    array_PLAYER_IDs[1] = PLAYER_1;
    array_PLAYER_IDs[2] = PLAYER_4;
    array_PLAYER_IDs[3] = PLAYER_2;
    g_playerPointers[PLAYER_1] = &allocated_players[PLAYER_1];
    g_playerPointers[PLAYER_2] = NULL;
    g_playerPointers[PLAYER_3] = &allocated_players[PLAYER_3];
    g_playerPointers[PLAYER_4] = NULL;
    expect_s32(get_player_position_in_shuffled(PLAYER_4), 2,
               "shuffle position counts only active players before the id");
    expect_s32(get_nth_player_from_shuffled(0), PLAYER_3,
               "shuffle helper returns the first active player");
    expect_s32(get_nth_player_from_shuffled(1), PLAYER_1,
               "shuffle helper returns the second active player");
    expect_s32(get_nth_player_from_shuffled(2), PLAYER_1,
               "shuffle helper has a stable fallback when out of range");
}

static void test_held_weapon_side_effects(void)
{
    struct player current_player;
    PropRecord player_prop;
    ChrRecord chr;
    PropRecord held_weapon;

    reset_fixture();
    memset(&current_player, 0, sizeof(current_player));
    memset(&player_prop, 0, sizeof(player_prop));
    memset(&chr, 0, sizeof(chr));
    memset(&held_weapon, 0, sizeof(held_weapon));
    current_player.prop = &player_prop;
    player_prop.chr = &chr;
    g_CurrentPlayer = &current_player;

    sub_GAME_7F09B368(GUNLEFT);
    expect_s32(weapon_flag_calls, 1, "weapon flag helper forwards the hand");
    expect(weapon_flag_chr == &chr, "weapon flag helper forwards the character");
    expect_s32(weapon_flag_hand, GUNLEFT, "weapon flag helper forwards left hand");

    current_weapon_ids[GUNRIGHT] = ITEM_WPPK;
    sub_GAME_7F09B398(GUNRIGHT);
    expect_s32(generate_calls, 1, "empty right hand generates its weapon prop");
    expect(generated_chr == &chr, "weapon generation forwards the character");
    expect_s32(generated_prop, PROP_CHRWPPK,
               "weapon generation maps the right hand item");
    expect_s32(generated_item, ITEM_WPPK,
               "weapon generation forwards the right hand item");
    expect_s32(generated_flags, 0, "right hand weapon has no left-handed flag");

    current_weapon_ids[GUNLEFT] = ITEM_WPPKSIL;
    sub_GAME_7F09B398(GUNLEFT);
    expect_s32(generate_calls, 2, "empty left hand generates its weapon prop");
    expect_s32(generated_prop, PROP_CHRWPPKSIL,
               "weapon generation maps the left hand item");
    expect_s32(generated_flags, PROPFLAG_WEAPON_LEFTHANDED,
               "left hand weapon receives the left-handed flag");

    current_weapon_ids[GUNRIGHT] = ITEM_UNARMED;
    sub_GAME_7F09B398(GUNRIGHT);
    expect_s32(generate_calls, 2, "unarmed hand does not generate a prop");

    chr.weapons_held[GUNRIGHT] = &held_weapon;
    current_weapon_ids[GUNRIGHT] = ITEM_WPPK;
    sub_GAME_7F09B398(GUNRIGHT);
    expect_s32(generate_calls, 2, "occupied hand does not generate a duplicate prop");
}

int main(void)
{
    test_default_perspective_and_reset();
    test_player_initialization();
    test_player_initialization_paths();
    test_player_accessors_and_sparse_slots();
    test_held_item_prop_mapping();
    test_shuffle_helpers();
    test_held_weapon_side_effects();

    if (failures != 0)
    {
        fprintf(stderr, "%d player-state test(s) failed\n", failures);
        return 1;
    }

    puts("player state tests passed");
    return 0;
}
