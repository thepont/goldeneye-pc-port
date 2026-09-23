#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "bg.h"
#include "bgroomtrans.h"
#include "bondview.h"

void matrix_4x4_set_identity(Mtxf *matrix);
void matrix_4x4_f32_to_s32(f32 mf[4][4], s32 ms[4][4]);

#define ROOM_CAPACITY 300
#define TEST_ROOM_COUNT (ROOM_CAPACITY + 1)

extern u8 roomStatusFlags[ROOM_CAPACITY];
extern s32 roomIndices[ROOM_CAPACITY];
extern s32 roomOwners[ROOM_CAPACITY];
extern Mtx roomMatrices[ROOM_CAPACITY];
s_room_info g_BgRoomInfo[TEST_ROOM_COUNT];
bg_room_data roomPositions[TEST_ROOM_COUNT];
bg_room_data *ptr_bgdata_room_fileposition_list = roomPositions;
f32 room_data_float2;
struct player players[2];
struct player extra_players[2];
struct player overflow_player;
struct player *g_playerPointers[MAX_PLAYER_COUNT + 1];
struct player *g_CurrentPlayer;

static s32 fake_player_count;
static s32 fake_max_room_count;
static int failures;

void matrix_4x4_set_identity(Mtxf *matrix)
{
    int row;
    int column;

    for (row = 0; row < 4; ++row)
    {
        for (column = 0; column < 4; ++column)
        {
            matrix->m[row][column] = row == column ? 1.0f : 0.0f;
        }
    }
}

void matrix_4x4_f32_to_s32(f32 mf[4][4], s32 ms[4][4])
{
    int row;
    int column;

    for (row = 0; row < 4; ++row)
    {
        for (column = 0; column < 4; ++column)
        {
            ms[row][column] = (s32)(mf[row][column] * 65536.0f);
        }
    }
}

void assignRoomIndexToRoomID(int mtx, int room);
void removeRoomIndexFromRoomID(int mtx, int room);
void resetRoomState(int roomIndex);
s32 findAvailableRoomIndex(void);
s32 setupRoomTransformationMatrix(s32 room);

s32 getPlayerCount(void)
{
    return fake_player_count;
}

s32 getMaxNumRooms(void)
{
    return fake_max_room_count;
}

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void reset_fixture(void)
{
    int i;

    memset(players, 0, sizeof(players));
    memset(extra_players, 0, sizeof(extra_players));
    memset(&overflow_player, 0, sizeof(overflow_player));
    memset(g_BgRoomInfo, 0, sizeof(g_BgRoomInfo));
    memset(roomPositions, 0, sizeof(roomPositions));
    g_playerPointers[0] = &players[0];
    g_playerPointers[1] = &players[1];
    g_playerPointers[2] = &extra_players[0];
    g_playerPointers[3] = &extra_players[1];
    g_playerPointers[4] = NULL;
    g_CurrentPlayer = &players[0];
    fake_player_count = 2;
    fake_max_room_count = TEST_ROOM_COUNT;

    for (i = 0; i < ROOM_CAPACITY; ++i)
    {
        roomIndices[i] = 1234;
        roomStatusFlags[i] = 99;
        roomOwners[i] = 1234;
    }
}

static void test_initialization_and_assignment(void)
{
    int i;

    reset_fixture();
    players[0].curRoomIndex = 10;
    players[1].curRoomIndex = 11;
    initializeRoomData();

    expect(players[0].curRoomIndex == -1 && players[1].curRoomIndex == -1,
           "room initialization clears every active player's room");
    for (i = 0; i < ROOM_CAPACITY; ++i)
    {
        expect(roomIndices[i] == -1 && roomStatusFlags[i] == 2 &&
                   roomOwners[i] == -1,
               "room initialization clears every matrix slot");
    }
    for (i = 0; i < ROOM_CAPACITY; ++i)
    {
        expect(g_BgRoomInfo[i].field_36 == -1,
               "room initialization clears each room's matrix assignment");
    }

    assignRoomIndexToRoomID(7, 3);
    expect(roomIndices[7] == 3 && g_BgRoomInfo[3].field_36 == 7,
           "room assignment updates both sides of the room-to-matrix map");
    removeRoomIndexFromRoomID(7, 3);
    expect(roomIndices[7] == -1 && g_BgRoomInfo[3].field_36 == -1,
           "room removal clears both sides of the room-to-matrix map");
}

static void test_room_state_lifecycle(void)
{
    reset_fixture();
    initializeRoomData();

    assignRoomIndexToRoomID(4, 2);
    roomStatusFlags[4] = 0;
    roomOwners[4] = 17;
    roomStatusFlags[5] = 0;
    roomOwners[5] = -1;

    updateRoomStatusFlags();
    expect(roomStatusFlags[4] == 1 && roomIndices[4] == 2,
           "room status update ages an owned room without resetting it early");
    expect(roomStatusFlags[5] == 0,
           "room status update leaves unowned matrix slots untouched");

    updateRoomStatusFlags();
    expect(roomStatusFlags[4] == 2 && roomOwners[4] == -1 &&
               roomIndices[4] == -1 && g_BgRoomInfo[2].field_36 == -1,
           "room status update releases an aged room and its mapping");

    roomOwners[6] = 33;
    roomStatusFlags[6] = 0;
    resetRoomState(6);
    expect(roomOwners[6] == -1 && roomStatusFlags[6] == 2,
           "resetRoomState resets an unassigned matrix slot safely");
}

static void test_initialization_clamps_provider_counts(void)
{
    reset_fixture();
    g_playerPointers[MAX_PLAYER_COUNT] = &overflow_player;
    overflow_player.curRoomIndex = 77;
    fake_player_count = MAX_PLAYER_COUNT + 1;
    fake_max_room_count = ROOM_CAPACITY + 1;
    g_BgRoomInfo[ROOM_CAPACITY].field_36 = 66;

    initializeRoomData();

    expect(overflow_player.curRoomIndex == 77,
           "room initialization does not walk past the fixed player-pointer table");
    expect(g_BgRoomInfo[ROOM_CAPACITY].field_36 == 66,
           "room initialization does not walk past the fixed room-matrix table");

    reset_fixture();
    fake_player_count = -1;
    fake_max_room_count = -1;
    g_BgRoomInfo[0].field_36 = 55;
    initializeRoomData();
    expect(g_BgRoomInfo[0].field_36 == 55,
           "room initialization treats negative provider counts as empty");
}

static void test_available_slot_and_transform_reuse(void)
{
    Gfx display_list[2] = {{0, 0}};
    s32 first;
    s32 second;
    coord3d scaled;

    reset_fixture();
    initializeRoomData();
    expect(findAvailableRoomIndex() == 0,
           "room allocator returns the first free matrix slot");

    memset(roomStatusFlags, 0, sizeof(roomStatusFlags));
    expect(findAvailableRoomIndex() == 0,
           "room allocator has a deterministic fallback when no slot is free");

    g_CurrentPlayer->curRoomIndex = 7;
    g_CurrentPlayer->current_model_pos.x = 10.0f;
    g_CurrentPlayer->current_model_pos.y = 20.0f;
    g_CurrentPlayer->current_model_pos.z = 30.0f;
    room_data_float2 = 0.5f;
    roomPositions[1].pos.x = 100.0f;
    roomPositions[1].pos.y = 200.0f;
    roomPositions[1].pos.z = 300.0f;
    roomStatusFlags[0] = 2;
    roomOwners[0] = -1;
    g_BgRoomInfo[1].field_36 = -1;

    first = setupRoomTransformationMatrix(1);
    expect(first == 0 && g_BgRoomInfo[1].field_36 == 0 &&
               roomOwners[0] == 7 && roomStatusFlags[0] == 0,
           "room transform allocates and owns a matrix for a new room");

    second = setupRoomTransformationMatrix(1);
    expect(second == first && roomStatusFlags[0] == 0,
           "room transform reuses a matrix for the same player and room");

    g_CurrentPlayer->curRoomIndex = 8;
    roomStatusFlags[1] = 2;
    roomOwners[1] = -1;
    second = setupRoomTransformationMatrix(1);
    expect(second == 1 && roomIndices[0] == -1 && g_BgRoomInfo[1].field_36 == 1 &&
               roomOwners[1] == 8,
           "room transform moves a room to a free matrix when ownership changes");

    expect(getRoomPositionByIndex(1) == &roomPositions[1].pos,
           "room position lookup returns the indexed position");
    getRoomPositionScaledByIndex(1, &scaled);
    expect(scaled.x == 50.0f && scaled.y == 100.0f && scaled.z == 150.0f,
           "room position lookup applies the active level scale");
    expect(applyRoomMatrixToDisplayList(display_list, 1) == &display_list[1],
           "room matrix application advances the display-list cursor");
}

static void test_player_room_setter(void)
{
    reset_fixture();
    g_CurrentPlayer = &players[1];
    setPlayerRoomField(42);
    expect(players[1].curRoomIndex == 42,
           "player room setter updates the current player only");
}

int main(void)
{
    test_initialization_and_assignment();
    test_room_state_lifecycle();
    test_initialization_clamps_provider_counts();
    test_available_slot_and_transform_reuse();
    test_player_room_setter();

    if (failures != 0)
    {
        return 1;
    }

    puts("background room transform suite passed");
    return 0;
}
