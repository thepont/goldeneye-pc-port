#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>

#include "glass.h"
#include "image_bank.h"
#include "music.h"
#include "player.h"
#include "snd.h"

s32 g_ClockTimer;
ALBank *g_musicSfxBufferPtr;

void sub_GAME_7F0A1DA0(coord3d *pos, coord3d *xaxis, coord3d *yaxis,
                       coord3d *zaxis, f32 xmin, f32 xmax, f32 ymin,
                       f32 ymax, f32 arg8, f32 arg9);

static s32 failures;
static u32 random_values[32];
static size_t random_index;
static s32 sound_calls;
static s32 sound_event_calls;
static Mtx fake_matrices[4];
static s32 fake_matrix_count;
static Mtx fake_projection_matrix;
static Mtx fake_modelview_matrix;
static struct player fake_player;

struct sImageTableEntry fake_glass_images[2];
struct sImageTableEntry *glassoverlayimage = fake_glass_images;
struct player *g_CurrentPlayer = &fake_player;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_near(f32 actual, f32 expected, const char *message)
{
    f32 difference = actual - expected;

    if (difference < -0.0001f || difference > 0.0001f)
    {
        fprintf(stderr, "FAIL: %s (actual %.6f expected %.6f)\n",
                message, actual, expected);
        failures++;
    }
}

static void random_reset(u32 value)
{
    size_t i;

    for (i = 0; i < sizeof(random_values) / sizeof(random_values[0]); i++)
    {
        random_values[i] = value;
    }
    random_index = 0;
}

u32 randomGetNext(void)
{
    u32 value = random_values[random_index];

    random_index = (random_index + 1) %
                   (sizeof(random_values) / sizeof(random_values[0]));
    return value;
}

ALSoundState *sndPlaySfx(struct ALBankAlt_s *soundBank,
                         s16 soundIndex,
                         ALSoundState *pendingState)
{
    (void)soundBank;
    (void)soundIndex;
    (void)pendingState;
    sound_calls++;
    return NULL;
}

void chrobjSndCreatePostEventDefault()
{
    sound_event_calls++;
}

static void test_create_shard_populates_geometry_and_wraps(void)
{
    s_shattered_window_piece pieces[2];
    coord3d position = { 10.0f, -2.0f, 4.0f };

    memset(pieces, 0, sizeof(pieces));
    ptr_shattered_window_pieces = pieces;
    SHATTERED_WINDOW_PIECES_BUFFER_LEN = 2;
    g_NextShardNum = 0;
    random_reset(0);

    glassCreateShard(&position, 0.25f, 2.0f);

    expect_true(pieces[0].active == 1,
                "glass shard creation activates the selected ring slot");
    expect_near(pieces[0].pos.x, 10.0f,
                "glass shard creation copies the position X");
    expect_near(pieces[0].pos.y, -2.0f,
                "glass shard creation copies the position Y");
    expect_near(pieces[0].pos.z, 4.0f,
                "glass shard creation copies the position Z");
    expect_near(pieces[0].velocity.x, -1.5f,
                "glass shard creation applies the symmetric X impulse");
    expect_near(pieces[0].velocity.y, -0.36f,
                "glass shard creation applies the biased Y impulse");
    expect_near(pieces[0].velocity.z, -1.5f,
                "glass shard creation applies the symmetric Z impulse");
    expect_true(pieces[0].v1x == 2 && pieces[0].v1y == 2 &&
                    pieces[0].v2x == 2 && pieces[0].v2y == -2 &&
                    pieces[0].v3x == -2 && pieces[0].v3y == -2,
                "glass shard creation builds the three local triangle vertices");
    expect_true(pieces[0].v1r == 5 && pieces[0].v1g == 5 &&
                    pieces[0].v1b == 0x7E && pieces[0].v1a == 0xFF &&
                    pieces[0].v2r == 5 && pieces[0].v2g == 0xFB &&
                    pieces[0].v2b == 0x7E && pieces[0].v2a == 0xFF &&
                    pieces[0].v3r == 0xFB && pieces[0].v3g == 0xFB &&
                    pieces[0].v3b == 0x7E && pieces[0].v3a == 0xFF,
                "glass shard creation assigns the environment-map colour ramp");
    expect_near(pieces[0].rot.x, 0.25f,
                "glass shard creation preserves the supplied X rotation");
    expect_near(pieces[0].rot.y, 0.0f,
                "glass shard creation starts Y rotation at zero");
    expect_near(pieces[0].rot.z, 0.0f,
                "glass shard creation derives Z rotation from the random stream");
    expect_true(g_NextShardNum == 1,
                "glass shard creation advances the ring cursor");

    glassCreateShard(&position, 0.5f, 1.0f);
    expect_true(g_NextShardNum == 0,
                "glass shard creation wraps the ring cursor at capacity");
    expect_near(pieces[1].rot.x, 0.5f,
                "glass shard creation writes the next ring slot before wrapping");
}

static void test_zero_capacity_does_not_write_a_shard(void)
{
    s_shattered_window_piece piece;
    s_shattered_window_piece before;
    coord3d position = { 1.0f, 2.0f, 3.0f };

    memset(&piece, 0xA5, sizeof(piece));
    before = piece;
    ptr_shattered_window_pieces = &piece;
    SHATTERED_WINDOW_PIECES_BUFFER_LEN = 0;
    g_NextShardNum = 0;
    random_reset(0);

    glassCreateShard(&position, 1.0f, 1.0f);

    expect_true(memcmp(&piece, &before, sizeof(piece)) == 0,
                "glass shard creation ignores a zero-capacity buffer");
    expect_true(g_NextShardNum == 0,
                "glass shard creation leaves the cursor unchanged without storage");

    SHATTERED_WINDOW_PIECES_BUFFER_LEN = 1;
    ptr_shattered_window_pieces = NULL;
    glassCreateShard(&position, 1.0f, 1.0f);
    expect_true(g_NextShardNum == 0,
                "glass shard creation ignores a null buffer pointer");
}

static void test_update_applies_fixed_step_physics_and_expiry(void)
{
    s_shattered_window_piece pieces[2];

    memset(pieces, 0, sizeof(pieces));
    ptr_shattered_window_pieces = pieces;
    SHATTERED_WINDOW_PIECES_BUFFER_LEN = 2;
    g_ClockTimer = 2;

    pieces[0].active = 10;
    pieces[0].pos.x = 4.0f;
    pieces[0].pos.y = 10.0f;
    pieces[0].pos.z = -3.0f;
    pieces[0].velocity.x = 1.5f;
    pieces[0].velocity.y = 3.0f;
    pieces[0].velocity.z = -2.0f;
    pieces[0].rot.x = 1.0f;
    pieces[0].rot.y = 2.0f;
    pieces[0].rot.z = 3.0f;
    pieces[0].angvel.x = 0.1f;
    pieces[0].angvel.y = 0.2f;
    pieces[0].angvel.z = 0.3f;

    pieces[1].active = 0;
    pieces[1].pos.x = 99.0f;

    update_broken_windows();

    expect_true(pieces[0].active == 12,
                "window update advances active age by the frame timer");
    expect_near(pieces[0].pos.x, 7.0f,
                "window update advances horizontal X motion");
    expect_near(pieces[0].pos.z, -7.0f,
                "window update advances horizontal Z motion");
    expect_near(pieces[0].pos.y, 15.9f,
                "window update applies gravity once per elapsed tick");
    expect_near(pieces[0].velocity.y, 2.8f,
                "window update reduces vertical velocity per elapsed tick");
    expect_near(pieces[0].rot.x, 1.2f,
                "window update advances X angular motion");
    expect_near(pieces[0].rot.y, 2.4f,
                "window update advances Y angular motion");
    expect_near(pieces[0].rot.z, 3.6f,
                "window update advances Z angular motion");
    expect_true(pieces[1].active == 0 && pieces[1].pos.x == 99.0f,
                "window update leaves inactive shards untouched");

    pieces[0].active = 0x8C;
    pieces[0].pos.y = 0.0f;
    pieces[0].velocity.y = 0.0f;
    g_ClockTimer = 30;
    update_broken_windows();
    expect_true(pieces[0].active == 0,
                "window update expires shards at the lifetime boundary");

    pieces[0].active = 1;
    pieces[0].pos.y = 0.0f;
    pieces[0].velocity.y = 0.0f;
    g_ClockTimer = 0;
    update_broken_windows();
    expect_true(pieces[0].active == 1,
                "window update permits zero-time frames without expiring shards");

    pieces[0].active = 1;
    pieces[0].pos.y = -30001.0f;
    update_broken_windows();
    expect_true(pieces[0].active == 0,
                "window update removes shards below the supported altitude");

    pieces[0].active = 1;
    pieces[0].pos.y = 30001.0f;
    update_broken_windows();
    expect_true(pieces[0].active == 0,
                "window update removes shards above the supported altitude");
}

static void test_render_emits_only_active_shards(void)
{
    s_shattered_window_piece pieces[2];
    Gfx commands[64];
    Gfx *end;

    memset(pieces, 0, sizeof(pieces));
    memset(&fake_player, 0, sizeof(fake_player));
    memset(fake_matrices, 0, sizeof(fake_matrices));
    memset(commands, 0, sizeof(commands));
    fake_matrix_count = 0;
    fake_player.current_model_pos.x = 10.0f;
    fake_player.current_model_pos.y = 20.0f;
    fake_player.current_model_pos.z = 30.0f;
    ptr_shattered_window_pieces = pieces;
    SHATTERED_WINDOW_PIECES_BUFFER_LEN = 2;
    pieces[0].active = 1;
    pieces[0].pos.x = 100.0f;
    pieces[0].pos.y = 200.0f;
    pieces[0].pos.z = 300.0f;
    pieces[0].rot.x = 0.25f;
    pieces[0].v1x = 1;
    pieces[0].v1y = 2;
    pieces[0].v2x = 3;
    pieces[0].v2y = 4;
    pieces[0].v3x = 5;
    pieces[0].v3y = 6;

    end = glassRenderShards(commands);

    expect_true(end > commands && end <= commands + 64,
                "glass shard rendering emits a bounded display-list pass");
    expect_true(fake_matrix_count == 1,
                "glass shard rendering allocates one model matrix per active shard");
    expect_near(((Mtxf *)&fake_matrices[0])->m[3][0], 90.0f,
                "glass shard rendering subtracts the player model X position");
    expect_near(((Mtxf *)&fake_matrices[0])->m[3][1], 180.0f,
                "glass shard rendering subtracts the player model Y position");
    expect_near(((Mtxf *)&fake_matrices[0])->m[3][2], 270.0f,
                "glass shard rendering subtracts the player model Z position");

    pieces[0].active = 0;
    fake_matrix_count = 0;
    end = glassRenderShards(commands);
    expect_true(end > commands && fake_matrix_count == 0,
                "glass shard rendering skips inactive shards");
}

static void test_window_grid_creates_one_shard_per_cell(void)
{
    s_shattered_window_piece pieces[2];
    coord3d position = { 0.0f, 0.0f, 0.0f };
    coord3d xaxis = { 1.0f, 0.0f, 0.0f };
    coord3d yaxis = { 0.0f, 1.0f, 0.0f };
    coord3d zaxis = { 0.0f, 0.0f, 1.0f };

    memset(pieces, 0, sizeof(pieces));
    ptr_shattered_window_pieces = pieces;
    SHATTERED_WINDOW_PIECES_BUFFER_LEN = 2;
    g_NextShardNum = 0;
    random_reset(0);
    sound_calls = 0;
    sound_event_calls = 0;

    sub_GAME_7F0A1DA0(&position, &xaxis, &yaxis, &zaxis,
                      0.0f, 4.0f, 0.0f, 4.0f, 99.0f, 99.0f);

    expect_true(sound_calls == 1 && sound_event_calls == 1,
                "window grid creation emits one shatter sound event");
    expect_true(pieces[0].active == 1 && g_NextShardNum == 1,
                "window grid creation creates the shard for its single cell");
    expect_near(pieces[0].pos.x, 2.0f,
                "window grid creation centers the shard on the X cell");
    expect_near(pieces[0].pos.y, 2.0f,
                "window grid creation centers the shard on the Y cell");
    expect_near(pieces[0].pos.z, 0.0f,
                "window grid creation preserves the plane origin");
}

int main(void)
{
    test_create_shard_populates_geometry_and_wraps();
    test_zero_capacity_does_not_write_a_shard();
    test_update_applies_fixed_step_physics_and_expiry();
    test_render_emits_only_active_shards();
    test_window_grid_creates_one_shard_per_cell();

    if (failures != 0)
    {
        fprintf(stderr, "%d glass contract(s) failed\n", failures);
        return 1;
    }

    puts("glass shard suite passed");
    return 0;
}

Mtx *dynAllocateMatrix(void)
{
    return &fake_matrices[fake_matrix_count++];
}

void texSelect(Gfx **gdlptr, struct sImageTableEntry *image,
               u32 arg2, s32 arg3, u32 ulst)
{
    (void)image;
    (void)arg2;
    (void)arg3;
    (void)ulst;
    (*gdlptr)++;
}

void matrix_4x4_set_position_and_rotation_around_xyz(coord3d *position,
                                                      coord3d *rotation,
                                                      Mtxf *matrix)
{
    (void)rotation;
    memset(matrix, 0, sizeof(*matrix));
    matrix->m[0][0] = 1.0f;
    matrix->m[1][1] = 1.0f;
    matrix->m[2][2] = 1.0f;
    matrix->m[3][3] = 1.0f;
    matrix->m[3][0] = position->x;
    matrix->m[3][1] = position->y;
    matrix->m[3][2] = position->z;
}

void matrix_4x4_f32_to_s32(f32 mf[4][4], s32 ms[4][4])
{
    memcpy(ms, mf, sizeof(f32) * 16);
}

u32 osVirtualToPhysical(void *address)
{
    return (u32)(uintptr_t)address;
}

s32 get_BONDdata_field_10E0(void)
{
    return 0;
}

Mtx *currentPlayerGetProjectionMatrix(void)
{
    return &fake_projection_matrix;
}

Mtx *currentPlayerGetMatrix10C8(void)
{
    return &fake_modelview_matrix;
}
