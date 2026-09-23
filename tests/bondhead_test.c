#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>

#include "bondhead.h"
#include "bondview.h"
#include "initanitable.h"
#include "lv.h"
#include "model.h"
#include "player.h"

/* The test links the real bondhead.c and supplies deterministic ports for its
 * model, clock, random, and breathing dependencies. */
struct player fake_player;
struct player *g_CurrentPlayer = &fake_player;
s32 g_ClockTimer;
f32 g_GlobalTimerDelta;

static struct animation_table_data fake_animation_table;
struct animation_table_data *ptr_animation_table = &fake_animation_table;

static f32 fake_abs_anim_speed;
static f32 fake_breathing;
static u32 fake_anim_merging_enabled;
static coord3d fake_suboffset;
static coord3d last_suboffset;
static u32 random_values[8];
static int random_index;

static int model_tick_calls;
static s32 last_model_tick_amount;
static s32 last_model_tick_update_chrstuff;
static int subcalcpos_calls;
static int matrix_identity_calls;
static int subcalcmatrices_calls;
static int set_merge_calls;
static s32 merge_history[4];

static int set_animation_calls;
static ModelAnimation *last_animation;
static s32 last_animation_flip;
static f32 last_animation_start;
static f32 last_animation_speed;
static f32 last_animation_merge;
static int set_looping_calls;
static f32 last_loop_frame;
static f32 last_loop_merge;
static int set_end_frame_calls;
static f32 last_end_frame;
static int set_flip_function_calls;
static void *last_flip_function;
static int set_speed_calls;
static f32 last_set_speed;
static f32 last_set_speed_start;

static int failures;

extern void bheadUpdatePos(coord3d *velocity);
extern void bheadUpdateRot(coord3d *look_velocity, coord3d *up_velocity);
extern void bheadSetdamp(f32 head_damp);
extern f32 bheadGetBreathingValue(void);

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

static void expect_float(f32 actual, f32 expected, const char *message)
{
    if (absolute_float(actual - expected) > 0.0003f)
    {
        fprintf(stderr, "FAIL: %s (actual %.7f, expected %.7f)\n",
                message, actual, expected);
        failures++;
    }
}

static void expect_vec(const coord3d *actual, const coord3d *expected,
                       const char *message)
{
    expect_float(actual->x, expected->x, message);
    expect_float(actual->y, expected->y, message);
    expect_float(actual->z, expected->z, message);
}

static void configure_animation_setup(void)
{
    /* initBondDATAdefaults populates these multipliers from ROM data in the
     * running game. Give this isolated test the equivalent post-init state so
     * bheadAdjustAnimation can be tested without loading a ROM. */
    g_BondMoveAnimationSetup[0].loopframe = 9.5f;
    g_BondMoveAnimationSetup[0].endframe = 27.0f;
    g_BondMoveAnimationSetup[0].speedMultiplier = 1.0f;
    g_BondMoveAnimationSetup[0].unk14 = 1.5f;
    g_BondMoveAnimationSetup[1].loopframe = 7.5f;
    g_BondMoveAnimationSetup[1].endframe = 17.0f;
    g_BondMoveAnimationSetup[1].speedMultiplier = 1.0f;
    g_BondMoveAnimationSetup[1].unk14 = 100.0f;
}

static void reset_fixture(void)
{
    int i;

    memset(&fake_player, 0, sizeof(fake_player));
    memset(&fake_animation_table, 0, sizeof(fake_animation_table));
    memset(random_values, 0, sizeof(random_values));
    memset(&last_suboffset, 0, sizeof(last_suboffset));
    memset(merge_history, 0, sizeof(merge_history));

    g_CurrentPlayer = &fake_player;
    g_ClockTimer = 0;
    g_GlobalTimerDelta = 1.0f;
    fake_abs_anim_speed = 0.0f;
    fake_breathing = 0.0f;
    fake_anim_merging_enabled = 7;
    fake_suboffset.x = 100.0f;
    fake_suboffset.y = 200.0f;
    fake_suboffset.z = 300.0f;
    random_index = 0;

    model_tick_calls = 0;
    last_model_tick_amount = 0;
    last_model_tick_update_chrstuff = 0;
    subcalcpos_calls = 0;
    matrix_identity_calls = 0;
    subcalcmatrices_calls = 0;
    set_merge_calls = 0;
    set_animation_calls = 0;
    last_animation = NULL;
    last_animation_flip = 0;
    last_animation_start = 0.0f;
    last_animation_speed = 0.0f;
    last_animation_merge = 0.0f;
    set_looping_calls = 0;
    last_loop_frame = 0.0f;
    last_loop_merge = 0.0f;
    set_end_frame_calls = 0;
    last_end_frame = 0.0f;
    set_flip_function_calls = 0;
    last_flip_function = NULL;
    set_speed_calls = 0;
    last_set_speed = 0.0f;
    last_set_speed_start = 0.0f;

    fake_player.headanim = -1;
    fake_player.headdamp = 0.5f;
    fake_player.resetheadpos = TRUE;
    fake_player.resetheadrot = TRUE;
    fake_player.resetheadtick = TRUE;
    fake_player.standheight = 10.0f;
    fake_player.standbodyoffset.x = 1.0f;
    fake_player.standbodyoffset.y = 2.0f;
    fake_player.standbodyoffset.z = 3.0f;
    fake_player.standcnt = 0;
    fake_player.standlook[0].x = 0.0f;
    fake_player.standlook[0].y = 0.0f;
    fake_player.standlook[0].z = 1.0f;
    fake_player.standlook[1].x = 0.2f;
    fake_player.standlook[1].y = 0.3f;
    fake_player.standlook[1].z = 1.0f;
    fake_player.standup[0].x = 0.0f;
    fake_player.standup[0].y = 1.0f;
    fake_player.standup[0].z = 0.0f;
    fake_player.standup[1].x = 0.1f;
    fake_player.standup[1].y = 1.0f;
    fake_player.standup[1].z = 0.2f;

    for (i = 0; i < 4; ++i)
    {
        fake_player.bondheadmatrices[i].m[0][0] = 1.0f;
        fake_player.bondheadmatrices[i].m[1][1] = 1.0f;
        fake_player.bondheadmatrices[i].m[2][2] = 1.0f;
        fake_player.bondheadmatrices[i].m[3][3] = 1.0f;
    }

    configure_animation_setup();
}

u32 randomGetNext(void)
{
    u32 value = random_values[random_index < 8 ? random_index : 7];
    random_index++;
    return value;
}

f32 bondviewGetBondBreathing(void)
{
    return fake_breathing;
}

void vec3Lerp(vec3d *from, vec3d *to, f32 fraction, vec3d *result)
{
    result->x = from->x + (to->x - from->x) * fraction;
    result->y = from->y + (to->y - from->y) * fraction;
    result->z = from->z + (to->z - from->z) * fraction;
}

f32 modelGetAbsAnimSpeed(Model *model)
{
    (void)model;
    return fake_abs_anim_speed;
}

u32 modelIsAnimMergingEnabled(void)
{
    return fake_anim_merging_enabled;
}

void modelSetAnimMergingEnabled(s32 enabled)
{
    if (set_merge_calls < (s32)(sizeof(merge_history) / sizeof(merge_history[0])))
    {
        merge_history[set_merge_calls] = enabled;
    }
    set_merge_calls++;
    fake_anim_merging_enabled = (u32)enabled;
}

void modelTickAnim(Model *model, s32 numticks, s32 update_chrstuff)
{
    (void)model;
    model_tick_calls++;
    last_model_tick_amount = numticks;
    last_model_tick_update_chrstuff = update_chrstuff;
}

void subcalcpos(Model *model)
{
    (void)model;
    subcalcpos_calls++;
}

void matrix_4x4_set_identity(Mtxf *matrix)
{
    s32 row;
    s32 column;

    matrix_identity_calls++;
    for (row = 0; row < 4; ++row)
    {
        for (column = 0; column < 4; ++column)
        {
            matrix->m[row][column] = row == column ? 1.0f : 0.0f;
        }
    }
}

void subcalcmatrices(ModelRenderData *render_data, Model *model)
{
    (void)render_data;
    (void)model;
    subcalcmatrices_calls++;
}

void getsuboffset(Model *model, coord3d *offset)
{
    (void)model;
    *offset = fake_suboffset;
}

void setsuboffset(Model *model, coord3d *offset)
{
    (void)model;
    last_suboffset = *offset;
}

void modelSetAnimation(Model *model, ModelAnimation *animation, s32 flip,
                       f32 startframe, f32 speed, f32 merge)
{
    (void)model;
    set_animation_calls++;
    last_animation = animation;
    last_animation_flip = flip;
    last_animation_start = startframe;
    last_animation_speed = speed;
    last_animation_merge = merge;
}

void modelSetAnimLooping(Model *model, f32 loopframe, f32 loopmerge)
{
    (void)model;
    set_looping_calls++;
    last_loop_frame = loopframe;
    last_loop_merge = loopmerge;
}

void modelSetAnimEndFrame(Model *model, f32 endframe)
{
    (void)model;
    set_end_frame_calls++;
    last_end_frame = endframe;
}

void modelSetAnimFlipFunction(Model *model, void *callback)
{
    (void)model;
    set_flip_function_calls++;
    last_flip_function = callback;
}

void modelSetAnimSpeed(Model *model, f32 anim_speed, f32 startframe)
{
    (void)model;
    set_speed_calls++;
    last_set_speed = anim_speed;
    last_set_speed_start = startframe;
}

static void test_flip_and_idle_roll(void)
{
    f32 mult = 1.0f / UINT_MAX;

    reset_fixture();
    fake_player.animFlipFlag = 0;
    bheadFlipAnimation();
    expect(fake_player.animFlipFlag != 0, "flip helper enables the animation mirror");
    bheadFlipAnimation();
    expect(fake_player.animFlipFlag == 0, "flip helper toggles the animation mirror back");

    random_values[0] = 0;
    random_values[1] = UINT_MAX;
    random_values[2] = UINT_MAX / 2;
    random_values[3] = UINT_MAX;
    fake_player.standcnt = 0;
    bheadUpdateIdleRoll();
    expect_s32(fake_player.standcnt, 1, "idle roll alternates to the other stand sample");
    expect_float(fake_player.standlook[0].x, -0.01f, "idle roll samples look x around zero");
    expect_float(fake_player.standlook[0].y, -0.005f, "stand sample uses the zero-side sign");
    expect_float(fake_player.standlook[0].z, 1.0f, "stand look keeps its forward component");
    expect_float(fake_player.standup[0].x, 0.01f, "idle roll samples up x around zero");
    expect_float(fake_player.standup[0].y, 1.0f, "stand up keeps its vertical component");
    expect_float(fake_player.standup[0].z, 0.01f,
                 "stand sample preserves the random z amplitude");

    random_index = 0;
    fake_player.standcnt = 1;
    bheadUpdateIdleRoll();
    expect_s32(fake_player.standcnt, 0, "idle roll alternates from the second stand sample");
    expect(fake_player.standlook[1].y >= 0.0f && fake_player.standlook[1].y <= 0.01f,
           "one-side idle roll uses a non-negative look y");
    expect(fake_player.standup[1].z <= 0.0f && fake_player.standup[1].z >= -0.01f,
           "one-side idle roll uses a non-positive up z");
}

static void test_position_filter_reset_and_zero_ticks(void)
{
    coord3d velocity = {1.0f, 2.0f, 3.0f};
    f32 sum_x = 0.0f;
    f32 sum_y = velocity.y / 0.07f;
    f32 sum_z = 0.0f;
    coord3d expected;
    s32 i;

    reset_fixture();
    g_ClockTimer = 2;
    bheadUpdatePos(&velocity);

    for (i = 0; i < g_ClockTimer; ++i)
    {
        sum_x = 0.93f * sum_x + velocity.x;
        sum_y = 0.93f * sum_y + velocity.y;
        sum_z = 0.93f * sum_z + velocity.z;
    }
    expected.x = sum_x * 0.07f;
    expected.y = sum_y * 0.07f;
    expected.z = sum_z * 0.07f;
    expect_vec(&fake_player.headpos, &expected,
               "position filter integrates each elapsed clock tick");
    expect(fake_player.resetheadpos == FALSE,
           "position filter consumes its reset request");

    g_ClockTimer = 0;
    velocity.x = 40.0f;
    velocity.y = 50.0f;
    velocity.z = 60.0f;
    expected = fake_player.headpos;
    bheadUpdatePos(&velocity);
    expect_vec(&fake_player.headpos, &expected,
               "position filter does not integrate when no clock tick elapsed");
}

static void test_rotation_filter_and_damping_transition(void)
{
    coord3d look = {1.0f, 2.0f, 3.0f};
    coord3d up = {-1.0f, 0.5f, 4.0f};
    coord3d expected_look;
    coord3d expected_up;
    f32 look_sum[3];
    f32 up_sum[3];
    s32 i;

    reset_fixture();
    g_ClockTimer = 2;
    for (i = 0; i < 3; ++i)
    {
        look_sum[i] = look.f[i] / 0.5f;
        up_sum[i] = up.f[i] / 0.5f;
    }
    bheadUpdateRot(&look, &up);
    for (i = 0; i < 2; ++i)
    {
        look_sum[0] = 0.5f * look_sum[0] + look.x;
        look_sum[1] = 0.5f * look_sum[1] + look.y;
        look_sum[2] = 0.5f * look_sum[2] + look.z;
        up_sum[0] = 0.5f * up_sum[0] + up.x;
        up_sum[1] = 0.5f * up_sum[1] + up.y;
        up_sum[2] = 0.5f * up_sum[2] + up.z;
    }
    expected_look.x = look_sum[0] * 0.5f;
    expected_look.y = look_sum[1] * 0.5f;
    expected_look.z = look_sum[2] * 0.5f;
    expected_up.x = up_sum[0] * 0.5f;
    expected_up.y = up_sum[1] * 0.5f;
    expected_up.z = up_sum[2] * 0.5f;
    expect_vec(&fake_player.headlook, &expected_look,
               "rotation filter integrates look velocity");
    expect_vec(&fake_player.headup, &expected_up,
               "rotation filter integrates up velocity");
    expect(fake_player.resetheadrot == FALSE,
           "rotation filter consumes its reset request");

    fake_player.headlooksum.x = 2.0f;
    fake_player.headlooksum.y = 4.0f;
    fake_player.headlooksum.z = 6.0f;
    fake_player.headupsum.x = -2.0f;
    fake_player.headupsum.y = -4.0f;
    fake_player.headupsum.z = -6.0f;
    fake_player.headdamp = 0.5f;
    bheadSetdamp(0.25f);
    expect_float(fake_player.headlooksum.x, 2.0f * 0.5f / 0.75f,
                 "damping transition preserves look x output");
    expect_float(fake_player.headupsum.z, -6.0f * 0.5f / 0.75f,
                 "damping transition preserves up z output");
    expect_float(fake_player.headdamp, 0.25f,
                 "damping transition stores the new filter coefficient");

    expected_look = fake_player.headlooksum;
    expected_up = fake_player.headupsum;
    bheadSetdamp(0.25f);
    expect_vec(&fake_player.headlooksum, &expected_look,
               "setting the same damping coefficient is idempotent for look state");
    expect_vec(&fake_player.headupsum, &expected_up,
               "setting the same damping coefficient is idempotent for up state");
}

static void test_breathing_value_boundaries(void)
{
    reset_fixture();
    fake_breathing = 0.4f;
    fake_player.headanim = -1;
    expect_float(bheadGetBreathingValue(), 0.0f,
                 "death state has no breathing value");

    fake_player.headanim = 0;
    fake_abs_anim_speed = 0.0f;
    expect_float(bheadGetBreathingValue(), 0.009166668f,
                 "idle head animation uses the base breathing value");

    fake_abs_anim_speed = 0.05f;
    expect_float(bheadGetBreathingValue(), 0.009166668f,
                 "slow head animation is clamped to base breathing");

    fake_abs_anim_speed = 1.75f;
    expect_float(bheadGetBreathingValue(), 0.1f,
                 "moving head animation derives breathing from its frame span");

    fake_player.headanim = 1;
    fake_abs_anim_speed = 0.95f;
    expect_float(bheadGetBreathingValue(), 0.1f,
                 "sprint head animation uses its own frame span");
}

static void test_animation_controls_and_phase_remap(void)
{
    ModelAnimation death_animation;

    reset_fixture();
    memset(&death_animation, 0, sizeof(death_animation));
    fake_player.animFlipFlag = 1;
    bheadStartDeathAnimation(&death_animation, 1, 3.0f, 1.2f);
    expect_s32(set_animation_calls, 1,
               "death animation is installed exactly once");
    expect(last_animation == &death_animation,
           "death animation forwards the requested animation object");
    expect_s32(last_animation_flip, 1,
               "death animation forwards its mirror flag");
    expect_float(last_animation_start, 3.0f,
                 "death animation forwards its start frame");
    expect_float(last_animation_speed, 0.6f,
                 "death animation halves its requested speed");
    expect_float(last_animation_merge, 12.0f,
                 "death animation uses the head transition time");
    expect_s32(fake_player.headanim, -1,
               "death animation switches to the death head state");

    bheadSetSpeed(1.4f);
    expect_s32(set_speed_calls, 1,
               "head speed helper delegates exactly once");
    expect_float(last_set_speed, 0.7f,
                 "head speed helper halves its requested speed");
    expect_float(last_set_speed_start, 0.0f,
                 "head speed helper starts from the current frame");

    fake_player.headanim = -1;
    bheadAdjustAnimation(1.0f);
    expect_s32(fake_player.headanim, 0,
               "slow movement selects the first head animation");
    expect_s32(set_animation_calls, 2,
               "selecting a new head animation installs it");
    expect_float(last_animation_start, 0.0f,
                 "first head animation starts at frame zero");
    expect_float(last_animation_speed, 0.5f,
                 "first head animation receives normalized speed");
    expect_float(last_loop_frame, 9.5f,
                 "first head animation receives its loop frame");
    expect_float(last_end_frame, 27.0f,
                 "first head animation receives its end frame");
    expect_s32(set_flip_function_calls, 1,
               "head animation installs its mirror callback");
    expect(last_flip_function != NULL,
           "head animation mirror callback is non-null");

    fake_player.field_5C0 = 18.25f;
    bheadAdjustAnimation(10.0f);
    expect_s32(fake_player.headanim, 1,
               "fast movement selects the sprint head animation");
    expect_float(last_animation_start, 12.25f,
                 "head animation transition preserves normalized phase");
    expect_float(last_set_speed, 5.0f,
                 "sprint animation receives normalized speed");
    expect_float(last_loop_frame, 7.5f,
                 "sprint animation receives its loop frame");
    expect_float(last_end_frame, 17.0f,
                 "sprint animation receives its end frame");

    set_animation_calls = 0;
    bheadAdjustAnimation(10.0f);
    expect_s32(set_animation_calls, 0,
               "keeping the current head animation does not restart it");
    expect_float(last_set_speed, 5.0f,
                 "keeping the current head animation still updates speed");
}

static void prepare_moving_update(s32 head_animation, f32 animation_speed)
{
    reset_fixture();
    fake_player.headanim = head_animation;
    fake_player.headwalkingtime60 = 0;
    fake_abs_anim_speed = animation_speed;
    g_ClockTimer = 1;
    g_GlobalTimerDelta = 2.0f;
    fake_player.bondheadmatrices[0].m[1][0] = 0.1f;
    fake_player.bondheadmatrices[0].m[1][1] = 0.9f;
    fake_player.bondheadmatrices[0].m[1][2] = 0.2f;
    fake_player.bondheadmatrices[0].m[2][0] = 0.2f;
    fake_player.bondheadmatrices[0].m[2][1] = 0.3f;
    fake_player.bondheadmatrices[0].m[2][2] = 1.1f;
    fake_player.bondheadmatrices[0].m[3][0] = 4.0f;
    fake_player.bondheadmatrices[0].m[3][1] = 12.0f;
    fake_player.bondheadmatrices[0].m[3][2] = 8.0f;
}

static void expect_common_update_calls(void)
{
    expect_s32(model_tick_calls, 1, "head update ticks the model once");
    expect_s32(last_model_tick_amount, g_ClockTimer,
               "head update forwards elapsed ticks");
    expect_s32(last_model_tick_update_chrstuff, 1,
               "head update requests character animation state");
    expect_s32(subcalcpos_calls, 1, "head update recalculates model position");
    expect_s32(matrix_identity_calls, 1,
               "head update initializes the temporary base matrix");
    expect_s32(subcalcmatrices_calls, 1,
               "head update recalculates head matrices");
    expect_s32(set_merge_calls, 2,
               "head update disables and restores animation merging");
    expect_s32(merge_history[0], 0,
               "head update disables animation merging before ticking");
    expect_s32(merge_history[1], 7,
               "head update restores the prior animation merge state");
    expect_float(last_suboffset.x, 96.0f,
                 "head update removes the head matrix x offset");
    expect_float(last_suboffset.z, 292.0f,
                 "head update removes the head matrix z offset");
    expect_float(fake_player.headbodyoffset.x, 1.0f,
                 "head update copies the standing body x offset");
    expect_float(fake_player.headbodyoffset.y, 2.0f,
                 "head update copies the standing body y offset");
    expect_float(fake_player.headbodyoffset.z, 3.0f,
                 "head update copies the standing body z offset");
    expect(fake_player.resetheadtick == FALSE,
           "head update consumes the head tick reset state");
}

static void test_moving_head_amplitude_branches(void)
{
    prepare_moving_update(0, 0.8f);
    bheadUpdate(0.5f, 2.0f);
    expect_float(fake_player.headamplitude, 1.0f,
                 "fast walking uses full head amplitude");
    expect_float(fake_player.sideamplitude, 1.0f,
                 "fast walking uses full side amplitude");
    expect_float(fake_player.bondheadmatrices[0].m[3][0], 3.0f,
                 "moving head divides lateral displacement by frame delta");
    expect_float(fake_player.bondheadmatrices[0].m[3][2], 2.0f,
                 "moving head applies percentage and frame delta to depth");
    expect_float(fake_player.headpos.y, 12.0f,
                 "moving head preserves standing height around matrix motion");
    expect_float(fake_player.headdamp, 0.99f,
                 "early walking uses the short-term damping coefficient");
    expect_common_update_calls();

    prepare_moving_update(0, 0.5f);
    bheadUpdate(1.0f, 0.0f);
    expect_float(fake_player.headamplitude, 0.8f,
                 "mid-speed walking interpolates head amplitude");

    prepare_moving_update(0, 0.1f);
    bheadUpdate(1.0f, 0.0f);
    expect_float(fake_player.headamplitude, 0.4f,
                 "near-zero walking uses the minimum head amplitude");

    prepare_moving_update(1, 0.8f);
    fake_player.headwalkingtime60 = 60;
    bheadUpdate(1.0f, 0.0f);
    expect_float(fake_player.headamplitude, 0.9f,
                 "sprint animation uses its fixed head amplitude");
    expect_float(fake_player.sideamplitude, 0.5f,
                 "sprint animation uses its fixed side amplitude");
    expect_s32(fake_player.headwalkingtime60, 61,
               "walking time advances for an active head animation");
    expect_float(fake_player.headdamp, 0.93f,
                 "long walking uses the steady-state damping coefficient");
}

static void test_death_and_idle_head_updates(void)
{
    prepare_moving_update(-1, 0.8f);
    bheadUpdate(1.0f, 0.0f);
    expect_float(fake_player.headamplitude, 1.0f,
                 "death animation uses full head amplitude");
    expect_float(fake_player.sideamplitude, 1.0f,
                 "death animation uses full side amplitude");
    expect_float(fake_player.headdamp, 0.85f,
                 "death animation uses its dedicated damping coefficient");
    expect_s32(fake_player.headwalkingtime60, 0,
               "death animation does not advance walking time");

    reset_fixture();
    fake_abs_anim_speed = 0.0f;
    fake_breathing = 0.4f;
    fake_player.headanim = 0;
    fake_player.standfrac = 0.99f;
    fake_player.standcnt = 0;
    fake_player.standlook[0].x = 0.0f;
    fake_player.standlook[0].y = 0.0f;
    fake_player.standlook[0].z = 1.0f;
    fake_player.standlook[1].x = 0.2f;
    fake_player.standlook[1].y = 0.3f;
    fake_player.standlook[1].z = 1.0f;
    g_ClockTimer = 0;
    g_GlobalTimerDelta = 1.0f;
    random_values[0] = UINT_MAX;
    random_values[1] = 0;
    random_values[2] = UINT_MAX;
    random_values[3] = 0;

    bheadUpdate(1.0f, 0.0f);
    expect_s32(fake_player.headwalkingtime60, 0,
               "idle head update resets walking time");
    expect_float(fake_player.headpos.x, 0.0f,
                 "idle head update returns to the center x position");
    expect_float(fake_player.headpos.y, 10.0f,
                 "idle head update returns to standing height");
    expect_float(fake_player.headpos.z, 0.0f,
                 "idle head update returns to the center z position");
    expect(fake_player.standfrac < 1.0f,
           "idle head update wraps the stand interpolation fraction");
    expect_s32(fake_player.standcnt, 1,
               "idle head update refreshes the next stand sample");
    expect_float(fake_player.headdamp, 0.99f,
                 "idle head update uses idle damping");
}

int main(void)
{
    test_flip_and_idle_roll();
    test_position_filter_reset_and_zero_ticks();
    test_rotation_filter_and_damping_transition();
    test_breathing_value_boundaries();
    test_animation_controls_and_phase_remap();
    test_moving_head_amplitude_branches();
    test_death_and_idle_head_updates();

    if (failures != 0)
    {
        fprintf(stderr, "%d bondhead contract(s) failed\n", failures);
        return 1;
    }

    puts("bondhead suite passed");
    return 0;
}
