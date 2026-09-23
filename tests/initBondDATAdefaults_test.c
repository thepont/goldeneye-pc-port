#include <stdio.h>
#include <string.h>

#include <ultra64.h>

#include "bondhead.h"
#include "bondview.h"
#include "chrobjdata.h"
#include "initanitable.h"
#include "initBondDATAdefaults.h"
#include "model.h"
#include "player.h"

struct player fake_player;
struct player *g_CurrentPlayer = &fake_player;
struct ModelFileHeader player_gait_object_header;
struct ModelSkeleton skeleton_guard;
struct init_bond_anim_unk g_BondMoveAnimationSetup[2];
struct animation_table_data fake_animation_table;
struct animation_table_data *ptr_animation_table = &fake_animation_table;
unsigned char *g_pc_animdata_base;

static int anim_init_calls;
static ModelFileHeader *anim_init_header;
static int scale_calls;
static f32 last_scale;
static int joint_sample_calls;
static int model_animation_calls;
static ModelAnimation *last_animation;
static s32 last_animation_flip;
static f32 last_animation_start;
static f32 last_animation_speed;
static f32 last_animation_merge;
static int position_calls;
static int identity_calls;
static int matrix_calls;
static int loop_calls;
static f32 last_loop_frame;
static int end_frame_calls;
static f32 last_end_frame;
static int flip_function_calls;
static void *last_flip_function;
static int idle_roll_calls;
static int failures;

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

    if (difference < -0.0005f || difference > 0.0005f)
    {
        fprintf(stderr, "FAIL: %s (actual %.6f expected %.6f)\n",
                message, actual, expected);
        failures++;
    }
}

static void reset_fixture(void)
{
    memset(&fake_player, 0, sizeof(fake_player));
    memset(&player_gait_object_header, 0, sizeof(player_gait_object_header));
    memset(&skeleton_guard, 0, sizeof(skeleton_guard));
    memset(&fake_animation_table, 0, sizeof(fake_animation_table));
    memset(g_BondMoveAnimationSetup, 0,
           sizeof(g_BondMoveAnimationSetup));

    g_CurrentPlayer = &fake_player;
    g_CurrentPlayer->headanim = 1;
    player_gait_object_header.numRecords = 0;
    g_BondMoveAnimationSetup[0].anim_id = 0x100;
    g_BondMoveAnimationSetup[0].loopframe = 10.0f;
    g_BondMoveAnimationSetup[0].endframe = 20.0f;
    g_BondMoveAnimationSetup[1].anim_id = 0x200;
    g_BondMoveAnimationSetup[1].loopframe = 5.0f;
    g_BondMoveAnimationSetup[1].endframe = 15.0f;
    g_pc_animdata_base = NULL;

    anim_init_calls = 0;
    anim_init_header = NULL;
    scale_calls = 0;
    last_scale = 0.0f;
    joint_sample_calls = 0;
    model_animation_calls = 0;
    last_animation = NULL;
    last_animation_flip = -1;
    last_animation_start = 0.0f;
    last_animation_speed = 0.0f;
    last_animation_merge = 0.0f;
    position_calls = 0;
    identity_calls = 0;
    matrix_calls = 0;
    loop_calls = 0;
    last_loop_frame = 0.0f;
    end_frame_calls = 0;
    last_end_frame = 0.0f;
    flip_function_calls = 0;
    last_flip_function = NULL;
    idle_roll_calls = 0;
}

void return_null(void)
{
}

void animInit(Model *model, ModelFileHeader *header, u32 *data)
{
    (void)model;
    (void)data;
    anim_init_calls++;
    anim_init_header = header;
}

void modelSetScale(Model *model, f32 scale)
{
    (void)model;
    scale_calls++;
    last_scale = scale;
}

u16 sub_GAME_7F06D2E4(s32 jointnum, s32 flip, ModelSkeleton *skeleton,
                      ModelAnimation *animation, s32 frame, coord16 *out)
{
    (void)jointnum;
    (void)flip;
    (void)animation;
    (void)frame;
    expect_true(skeleton == &skeleton_guard,
                "Bond default setup samples the guard skeleton");
    out->x = 1;
    out->y = 2;
    out->z = 3;
    joint_sample_calls++;
    return 0;
}

void modelSetAnimation(Model *model, ModelAnimation *animation, s32 flip,
                       f32 startframe, f32 speed, f32 merge)
{
    expect_true(model == &g_CurrentPlayer->model,
                "Bond default setup applies animation to the current player");
    model_animation_calls++;
    last_animation = animation;
    last_animation_flip = flip;
    last_animation_start = startframe;
    last_animation_speed = speed;
    last_animation_merge = merge;
}

void subcalcpos(Model *model)
{
    expect_true(model == &g_CurrentPlayer->model,
                "Bond default setup calculates the current model position");
    position_calls++;
}

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
    identity_calls++;
}

void subcalcmatrices(ModelRenderData *render_data, Model *model)
{
    expect_true(model == &g_CurrentPlayer->model &&
                    render_data->basemtx != NULL &&
                    render_data->mtxlist == g_CurrentPlayer->bondheadmatrices,
                "Bond default setup derives offsets from the current head matrices");
    render_data->mtxlist[0].m[3][1] = 100.0f;
    render_data->mtxlist[1].m[3][1] = 130.0f;
    render_data->mtxlist[1].m[3][2] = 7.0f;
    matrix_calls++;
}

void modelSetAnimLooping(Model *model, f32 loopframe, f32 loopmerge)
{
    (void)model;
    (void)loopmerge;
    loop_calls++;
    last_loop_frame = loopframe;
}

void modelSetAnimEndFrame(Model *model, f32 endframe)
{
    (void)model;
    end_frame_calls++;
    last_end_frame = endframe;
}

void modelSetAnimFlipFunction(Model *model, void *callback)
{
    (void)model;
    flip_function_calls++;
    last_flip_function = callback;
}

void bheadFlipAnimation(void)
{
}

void bheadUpdateIdleRoll(void)
{
    idle_roll_calls++;
}

static void test_bond_defaults_reset_animation_and_head_state(void)
{
    reset_fixture();
    fake_player.headpos.f[0] = 4.0f;
    fake_player.headlooksum.f[2] = -4.0f;
    fake_player.resetheadpos = FALSE;
    fake_player.resetheadrot = FALSE;
    fake_player.resetheadtick = FALSE;

    sets_a_bunch_of_BONDdata_values_to_default();

    expect_true(anim_init_calls == 1 && anim_init_header ==
                    &player_gait_object_header,
                "Bond default setup initializes the gait animation model");
    expect_true(scale_calls == 1 && last_scale == IDO_POINT_ONE,
                "Bond default setup applies the canonical model scale");
    expect_true(joint_sample_calls == 20,
                "Bond default setup samples both movement animations completely");
    expect_float(g_BondMoveAnimationSetup[0].speedMultiplier, 0.3f,
                 "Bond walk animation speed is derived from root motion");
    expect_float(g_BondMoveAnimationSetup[1].speedMultiplier, 0.3f,
                 "Bond sprint animation speed is derived from root motion");
    expect_true(fake_player.headanim == 0 && fake_player.resetheadpos &&
                    fake_player.resetheadrot && fake_player.resetheadtick,
                "Bond default setup resets head animation and reset flags");
    expect_float(fake_player.headlooksum.f[2], 14.285716f,
                 "Bond default setup restores the NTSC head-look sum");
    expect_true(position_calls == 1 && identity_calls == 1 && matrix_calls == 1,
                "Bond default setup calculates standing matrices from identity");
    expect_float(fake_player.standheight, 100.0f,
                 "Bond default setup records standing height from the root matrix");
    expect_float(fake_player.standbodyoffset.y, 30.0f,
                 "Bond default setup records vertical head offset");
    expect_float(fake_player.standbodyoffset.z, 7.0f,
                 "Bond default setup records depth head offset");
    expect_true(model_animation_calls == 2 && last_animation_flip == 0 &&
                    last_animation_start == 10.0f && last_animation_speed == 0.5f &&
                    last_animation_merge == 0.0f,
                "Bond default setup selects the reset walking animation");
    expect_true(loop_calls == 1 && last_loop_frame == 10.0f &&
                    end_frame_calls == 1 && last_end_frame == 20.0f &&
                    flip_function_calls == 1 &&
                    last_flip_function == (void *)bheadFlipAnimation &&
                    idle_roll_calls == 1,
                "Bond default setup configures looping and idle-roll callbacks");
}

int main(void)
{
    test_bond_defaults_reset_animation_and_head_state();

    if (failures != 0)
    {
        fprintf(stderr, "%d Bond default-state contract(s) failed\n", failures);
        return 1;
    }

    puts("Bond default-state suite passed");
    return 0;
}
