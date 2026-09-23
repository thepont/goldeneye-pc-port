#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "bgfog.h"
#include "bondview.h"
#include "image_bank.h"
#include "unk_092E50.h"
#include "sky.h"

extern void skyGetWorldPosFromScreenPos(f32 offset_x, f32 offset_y,
                                        coord3d *out);
extern bool skyIsScreenCornerInSky(coord3d *corner3dpos, coord3d *dstpos,
                                   f32 *dstfrac);
extern bool skyIsCornerInWater(coord3d *corner3dpos, coord3d *dstpos,
                               f32 *dstfrac);
extern void skyChooseCloudVtxColour(SkyRelated18 *out, f32 blend);
extern void skyChooseWaterVtxColour(SkyRelated18 *out, f32 blend);
extern s32 g_SkyStageNum;
extern f32 skyClamp(f32 value, f32 lower, f32 upper);
extern f32 skyRound(f32 value);
extern u32 sub_GAME_7F094298(f32 value);
extern void skyCalculateEdgeVertex(coord3d *base, coord3d *reference,
                                   coord3d *out);
extern bool skyVerticesAreTheSame(SkyRelated38 *left, SkyRelated38 *right);

static CurrentEnvironmentRecord fake_environment;
static coord3d fake_eye;
static Mtxf fake_view_to_world;
static f32 fake_screen_left;
static f32 fake_screen_top;
static f32 fake_screen_width;
static f32 fake_screen_height;
static f32 fake_projection_y_bias;
static f32 fake_projection_z_from_screen_y;
static f32 fake_projection_y_from_world_z;
static s32 fake_player_count;
static s16 fake_view_left;
static s16 fake_view_top;
static s16 fake_view_width;
static s16 fake_view_height;
static coord2d last_projection_input;
static f32 last_projection_length;
static int transform_call_count;
static int rotate_call_count;
static bool fake_corner_pattern_enabled;
static u8 fake_corner_pattern;
static Vtx fake_vertex_storage[128];
static Mtx fake_matrix_storage[32];
static s32 fake_vertex_storage_used;
static s32 fake_matrix_storage_used;
static struct sImageTableEntry fake_sky_image;
static struct player fake_current_player;

struct player *g_CurrentPlayer = &fake_current_player;
struct sImageTableEntry *skywaterimages = &fake_sky_image;

/* These are deliberately small fake ports. The tests exercise sky's public
 * policy while keeping camera, fog and frame timing deterministic. */
CurrentEnvironmentRecord *fogGetCurrentEnvironmentp(void)
{
    return &fake_environment;
}

coord3d *bondviewGetCurrentPlayersPosition(void)
{
    return &fake_eye;
}

Mtxf *currentPlayerGetViewToWorldMtxf(void)
{
    return &fake_view_to_world;
}

f32 getPlayer_c_screenleft(void)
{
    return fake_screen_left;
}

f32 getPlayer_c_screentop(void)
{
    return fake_screen_top;
}

f32 getPlayer_c_screenwidth(void)
{
    return fake_screen_width;
}

f32 getPlayer_c_screenheight(void)
{
    return fake_screen_height;
}

f32 get_room_data_float1(void)
{
    return 30.0f;
}

s32 getPlayerCount(void)
{
    return fake_player_count;
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

Gfx *viSetFillColor(Gfx *gdl, s32 r, s32 g, s32 b)
{
    (void)r;
    (void)g;
    (void)b;
    return gdl + 1;
}

Mtxf *currentPlayerGetProjectionMatrixF(void)
{
    return &fake_view_to_world;
}

Mtxf *camGetWorldToScreenMtxf(void)
{
    return &fake_view_to_world;
}

void matrix_4x4_multiply(Mtxf *lhs, Mtxf *rhs, Mtxf *result)
{
    (void)lhs;
    (void)rhs;
    memset(result, 0, sizeof(*result));
    result->m[0][0] = 1.0f;
    result->m[1][1] = 1.0f;
    result->m[2][2] = 1.0f;
    result->m[3][3] = 1.0f;
    result->m[2][1] = fake_projection_y_from_world_z;
}

void guScaleF(float matrix[4][4], float x, float y, float z)
{
    memset(matrix, 0, sizeof(float) * 16);
    matrix[0][0] = x;
    matrix[1][1] = y;
    matrix[2][2] = z;
    matrix[3][3] = 1.0f;
}

void guMtxIdentF(float matrix[4][4])
{
    memset(matrix, 0, sizeof(float) * 16);
    matrix[0][0] = 1.0f;
    matrix[1][1] = 1.0f;
    matrix[2][2] = 1.0f;
    matrix[3][3] = 1.0f;
}

void guMtxF2L(float matrix[4][4], Mtx *out)
{
    (void)matrix;
    memset(out, 0, sizeof(*out));
}

void guMtxIdent(Mtx *out)
{
    memset(out, 0, sizeof(*out));
}

u32 osVirtualToPhysical(void *address)
{
    return (u32)(uintptr_t)address;
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

Gfx *sub_GAME_7F09343C(Gfx *gdl, s32 arg1)
{
    (void)arg1;
    return gdl;
}

int gfx_water_world_surface_enabled(void)
{
    return 0;
}

Gfx *gfx_emit_water_material(Gfx *gdl, int on)
{
    (void)on;
    return gdl;
}

Vtx *dynAllocateVertices(s32 count)
{
    Vtx *result = &fake_vertex_storage[fake_vertex_storage_used];

    fake_vertex_storage_used += count;
    return result;
}

Mtx *dynAllocateMatrix(void)
{
    return &fake_matrix_storage[fake_matrix_storage_used++];
}

void transformAndNormalizeByLength2Dto3D(coord2d *in, coord3d *out,
                                         f32 length)
{
    int corner_index = transform_call_count;

    last_projection_input = *in;
    last_projection_length = length;
    transform_call_count++;
    out->x = in->x;
    if (fake_corner_pattern_enabled && corner_index < 4)
    {
        out->y = (fake_corner_pattern & (1u << (3 - corner_index)))
                     ? 80.0f
                     : -80.0f;
    }
    else
    {
        out->y = in->y + fake_projection_y_bias;
    }
    out->z = length + in->y * fake_projection_z_from_screen_y;
}

void mtx4RotateVecInPlace(Mtxf *matrix, coord3d *vector)
{
    (void)matrix;
    rotate_call_count++;
    vector->x += 1.0f;
    vector->y += 2.0f;
    vector->z += 3.0f;
}

s32 g_ClockTimer;
f32 g_GlobalTimerDelta;
f32 g_SkyCloudOffset;

static int failures;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_close(f32 actual, f32 expected, f32 tolerance,
                         const char *message)
{
    f32 difference = actual - expected;

    if (difference < 0.0f)
    {
        difference = -difference;
    }

    if (difference > tolerance)
    {
        fprintf(stderr, "FAIL: %s (expected %.6f, got %.6f)\n",
                message, expected, actual);
        failures++;
    }
}

static void reset_sky_fakes(void)
{
    memset(&fake_environment, 0, sizeof(fake_environment));
    memset(&fake_eye, 0, sizeof(fake_eye));
    memset(&fake_view_to_world, 0, sizeof(fake_view_to_world));
    fake_screen_left = 0.0f;
    fake_screen_top = 0.0f;
    fake_screen_width = 320.0f;
    fake_screen_height = 240.0f;
    fake_projection_y_bias = 0.0f;
    fake_projection_z_from_screen_y = 0.0f;
    fake_projection_y_from_world_z = 0.0f;
    fake_player_count = 1;
    fake_view_left = 0;
    fake_view_top = 0;
    fake_view_width = 320;
    fake_view_height = 240;
    fake_corner_pattern_enabled = FALSE;
    fake_corner_pattern = 0;
    memset(&fake_current_player, 0, sizeof(fake_current_player));
    fake_current_player.viewleft = fake_view_left;
    fake_current_player.viewtop = fake_view_top;
    fake_current_player.viewx = fake_view_width;
    fake_current_player.viewy = fake_view_height;
    fake_vertex_storage_used = 0;
    fake_matrix_storage_used = 0;
    memset(&last_projection_input, 0, sizeof(last_projection_input));
    last_projection_length = 0.0f;
    transform_call_count = 0;
    rotate_call_count = 0;
    g_ClockTimer = 0;
    g_GlobalTimerDelta = 0.0f;
}

static void test_sky_world_position_uses_viewport_and_water_concavity(void)
{
    coord3d result = {0.0f, 0.0f, 0.0f};

    reset_sky_fakes();
    fake_screen_left = 100.0f;
    fake_screen_top = 20.0f;
    fake_environment.WaterConcavity = 7.0f;

    skyGetWorldPosFromScreenPos(2.0f, 3.0f, &result);

    expect_close(last_projection_input.x, 102.0f, 0.0f,
                 "sky world-position conversion adds the viewport left edge");
    expect_close(last_projection_input.y, 30.0f, 0.0f,
                 "sky world-position conversion includes top edge and water concavity");
    expect_close(last_projection_length, 100.0f, 0.0f,
                 "sky world-position conversion uses its fixed ray length");
    expect_true(transform_call_count == 1 && rotate_call_count == 1,
                "sky world-position conversion transforms and rotates exactly once");
    expect_close(result.x, 103.0f, 0.0f,
                 "sky world-position conversion rotates the x component");
    expect_close(result.y, 32.0f, 0.0f,
                 "sky world-position conversion rotates the y component");
    expect_close(result.z, 103.0f, 0.0f,
                 "sky world-position conversion rotates the z component");
}

static void test_sky_corner_intersections_cover_air_water_and_range_cap(void)
{
    coord3d sky_corner = {40.0f, 10.0f, 0.0f};
    coord3d water_corner = {40.0f, -10.0f, 0.0f};
    coord3d zero_height_corner = {40.0f, 0.0f, 0.0f};
    coord3d wrong_side = {40.0f, -1.0f, 0.0f};
    coord3d destination = {0.0f, 0.0f, 0.0f};
    f32 fraction = 0.0f;

    reset_sky_fakes();
    fake_eye.x = 10.0f;
    fake_eye.y = 20.0f;
    fake_eye.z = 30.0f;
    fake_environment.CloudRepeat = 100.0f;
    fake_environment.WaterRepeat = -20.0f;

    expect_true(skyIsScreenCornerInSky(&sky_corner, &destination, &fraction),
                "positive sky corner intersects the cloud plane");
    expect_close(fraction, 0.5f, 0.0001f,
                 "cloud intersection reports the sky interpolation fraction");
    expect_close(destination.x, 330.0f, 0.0001f,
                 "cloud intersection projects x to the cloud plane");
    expect_close(destination.y, 100.0f, 0.0001f,
                 "cloud intersection projects y to the cloud plane");
    expect_close(destination.z, 30.0f, 0.0001f,
                 "cloud intersection preserves the eye z when the ray is axis aligned");

    expect_true(!skyIsScreenCornerInSky(&wrong_side, &destination, &fraction),
                "negative sky corner does not intersect the cloud plane");

    expect_true(skyIsCornerInWater(&water_corner, &destination, &fraction),
                "negative water corner intersects the water plane");
    expect_close(fraction, 0.5f, 0.0001f,
                 "water intersection reports the water interpolation fraction");
    expect_close(destination.x, 170.0f, 0.0001f,
                 "water intersection projects x to the water plane");
    expect_close(destination.y, -20.0f, 0.0001f,
                 "water intersection projects y to the water plane");

    expect_true(!skyIsCornerInWater(&sky_corner, &destination, &fraction),
                "positive water corner does not intersect the water plane");

    fake_environment.CloudRepeat = 100.0f;
    expect_true(skyIsScreenCornerInSky(&zero_height_corner, &destination, &fraction),
                "zero-height sky corner uses the epsilon plane instead of dividing by zero");
    expect_close(destination.x, 300010.0f, 0.01f,
                 "cloud intersection caps an excessively distant projection");
}

static void test_sky_colour_policy_blends_environment_and_layer(void)
{
    SkyRelated18 cloud;
    SkyRelated18 water;

    reset_sky_fakes();
    fake_environment.Red = 100;
    fake_environment.Green = 150;
    fake_environment.Blue = 200;
    fake_environment.CloudRed = 50.0f;
    fake_environment.CloudGreen = 60.0f;
    fake_environment.CloudBlue = 70.0f;
    fake_environment.WaterRed = 20.0f;
    fake_environment.WaterGreen = 30.0f;
    fake_environment.WaterBlue = 40.0f;

    skyChooseCloudVtxColour(&cloud, 0.0f);
    expect_true(cloud.r == 130 && cloud.g == 174 && cloud.b == 215 && cloud.a == 0xff,
                "cloud colour blends the cloud layer with the environment at full strength");

    skyChooseWaterVtxColour(&water, 1.0f);
    expect_true(water.r == 100 && water.g == 150 && water.b == 200 && water.a == 0xff,
                "water colour falls back to the environment at zero layer strength");
}

static void test_sky_projection_handles_identity_zero_and_negative_w(void)
{
    SkyRelated18 source;
    SkyRelated38 projected;
    Mtxf matrix;

    reset_sky_fakes();
    fake_screen_left = 10.0f;
    fake_screen_top = 20.0f;
    fake_screen_width = 100.0f;
    fake_screen_height = 50.0f;
    fake_environment.WaterConcavity = 3.0f;
    memset(&source, 0, sizeof(source));
    source.unk00 = 1.0f;
    source.unk04 = 2.0f;
    source.unk08 = 3.0f;
    source.unk0c = 4.0f;
    source.unk10 = 5.0f;
    source.r = 11;
    source.g = 22;
    source.b = 33;
    source.a = 44;
    memset(&matrix, 0, sizeof(matrix));
    matrix.m[0][0] = 1.0f;
    matrix.m[1][1] = 1.0f;
    matrix.m[2][2] = 1.0f;
    matrix.m[3][3] = 1.0f;

    sub_GAME_7F097388(&source, &matrix, 32768, 65536.0f, 131072.0f,
                      &projected);
    expect_close(projected.unk28, 440.0f, 0.001f,
                 "sky projection maps identity x into the viewport");
    expect_close(projected.unk2c, -32.0f, 0.001f,
                 "sky projection applies screen top and water concavity");
    expect_close(projected.unk30, 2044.0f, 0.001f,
                 "sky projection maps depth into the N64 depth range");
    expect_close(projected.unk34, 2.0f, 0.001f,
                 "sky projection records the reciprocal homogeneous w");
    expect_true(projected.r == source.r && projected.g == source.g &&
                    projected.b == source.b && projected.a == source.a,
                "sky projection carries vertex colour without changing it");

    matrix.m[3][3] = 0.0f;
    sub_GAME_7F097388(&source, &matrix, 32768, 65536.0f, 65536.0f,
                      &projected);
    expect_close(projected.unk34, 32767.0f, 0.001f,
                 "sky projection uses the saturated reciprocal for zero w");
    expect_true(projected.unk28 == 4090.0f && projected.unk2c == -4102.0f,
                "sky projection clamps screen coordinates at zero w");

    matrix.m[3][3] = -1.0f;
    sub_GAME_7F097388(&source, &matrix, 32768, 65536.0f, 65536.0f,
                      &projected);
    expect_close(projected.unk34, -2.0f, 0.001f,
                 "sky projection preserves negative reciprocal w for diagnostics");
    expect_true(projected.unk28 == 4090.0f && projected.unk2c == -4102.0f,
                "sky projection clamps coordinates produced by negative w");
}

static void test_sky_stage_and_tick_state_are_deterministic(void)
{
    reset_sky_fakes();
    skySetStageNum(42);
    expect_true(g_SkyStageNum == 42,
                "sky stage setter stores the active stage number");

    g_SkyCloudOffset = 4095.0f;
    g_ClockTimer = 2;
    skyTick();
    expect_close(g_SkyCloudOffset, 1.0f, 0.0f,
                 "sky tick wraps the cloud offset after one full period");

    g_SkyCloudOffset = 4096.0f;
    g_ClockTimer = 0;
    skyTick();
    expect_close(g_SkyCloudOffset, 4096.0f, 0.0f,
                 "sky tick leaves an exact period endpoint unchanged");
}

static void test_sky_render_clears_each_viewport_shape(void)
{
    Gfx commands[32];
    Gfx *end;

    reset_sky_fakes();
    fake_environment.Clouds = 0;
    fake_environment.Red = 12;
    fake_environment.Green = 34;
    fake_environment.Blue = 56;
    fake_view_left = 7;
    fake_view_top = 9;
    fake_view_width = 100;
    fake_view_height = 80;

    end = skyRender(commands);
    expect_true(end > commands && end <= commands + 32,
                "single-player sky rendering emits a complete fill viewport pass");

    reset_sky_fakes();
    fake_environment.Clouds = 0;
    fake_player_count = 2;
    fake_current_player.viewleft = 11;
    fake_current_player.viewtop = 13;
    fake_current_player.viewx = 90;
    fake_current_player.viewy = 70;

    end = skyRender(commands);
    expect_true(end > commands && end <= commands + 32,
                "multiplayer sky rendering emits a complete current-player fill viewport pass");
}

static void test_sky_render_covers_every_corner_topology(void)
{
    Gfx commands[512];
    u8 pattern;

    for (pattern = 0; pattern < 16; pattern++)
    {
        Gfx *end;

        reset_sky_fakes();
        fake_environment.Clouds = 1;
        fake_environment.IsWater = 0;
        fake_environment.Red = 96;
        fake_environment.Green = 112;
        fake_environment.Blue = 128;
        fake_environment.CloudRed = 32.0f;
        fake_environment.CloudGreen = 40.0f;
        fake_environment.CloudBlue = 48.0f;
        fake_environment.CloudRepeat = 200.0f;
        fake_environment.WaterRepeat = -200.0f;
        fake_screen_width = 320.0f;
        fake_screen_height = 240.0f;
        fake_corner_pattern_enabled = TRUE;
        fake_corner_pattern = pattern;
        g_SkyCloudOffset = 0.0f;

        end = skyRender(commands);
        expect_true(end > commands && end <= commands + 512,
                    "each sky corner topology emits a bounded display-list pass");
        expect_true(fake_vertex_storage_used <= 128 &&
                        fake_matrix_storage_used <= 32,
                    "each sky corner topology stays within its deterministic dynamic buffers");
    }
}

static void test_sky_render_legacy_water_path_emits_textured_fan(void)
{
    Gfx commands[1024];
    Gfx *end;

    reset_sky_fakes();
    fake_environment.Clouds = 1;
    fake_environment.IsWater = 1;
    fake_environment.Red = 24;
    fake_environment.Green = 40;
    fake_environment.Blue = 56;
    fake_environment.WaterRed = 16.0f;
    fake_environment.WaterGreen = 24.0f;
    fake_environment.WaterBlue = 32.0f;
    fake_environment.WaterRepeat = -1.0f;
    fake_projection_y_bias = -300.0f;
    fake_projection_z_from_screen_y = 1.0f;
    fake_projection_y_from_world_z = 0.1f;
    fake_corner_pattern_enabled = FALSE;
    fake_corner_pattern = 0;
    g_SkyStageNum = 7;

    end = skyRender(commands);

    expect_true(end > commands && end <= commands + 1024,
                "legacy water rendering emits a bounded textured fan pass");
    expect_true(fake_vertex_storage_used >= 6 && fake_matrix_storage_used >= 4,
                "legacy water rendering allocates both textured triangle batches");
}

static SkyRelated38 make_sky_render_vertex(f32 x, f32 y, f32 s, f32 t)
{
    SkyRelated38 vertex;

    memset(&vertex, 0, sizeof(vertex));
    vertex.unk0c = 1000.0f;
    vertex.unk20 = s;
    vertex.unk24 = t;
    vertex.unk28 = x;
    vertex.unk2c = y;
    vertex.unk30 = 1000.0f;
    vertex.unk34 = 1.0f;
    vertex.r = 80.0f;
    vertex.g = 96.0f;
    vertex.b = 112.0f;
    vertex.a = 255.0f;
    return vertex;
}

static void test_sky_render_primitives_reject_degenerate_geometry(void)
{
    Gfx commands[64];
    SkyRelated38 a = make_sky_render_vertex(0.0f, 0.0f, 0.0f, 0.0f);
    SkyRelated38 b = make_sky_render_vertex(100.0f, 100.0f, 100.0f, 0.0f);
    SkyRelated38 c = make_sky_render_vertex(200.0f, 200.0f, 0.0f, 100.0f);

    reset_sky_fakes();
    expect_true(skyRenderTri(commands, &a, &a, &c, 65536.0f, TRUE) == commands,
                "sky triangle renderer rejects coincident vertices");
    expect_true(skyRenderTri(commands, &a, &b, &c, 65536.0f, TRUE) == commands,
                "sky triangle renderer rejects collinear vertices");
}

static void test_sky_render_primitives_emit_port_geometry(void)
{
    Gfx commands[256];
    SkyRelated38 a = make_sky_render_vertex(0.0f, 0.0f, 0.0f, 0.0f);
    SkyRelated38 b = make_sky_render_vertex(400.0f, 0.0f, 2048.0f, 0.0f);
    SkyRelated38 c = make_sky_render_vertex(0.0f, 400.0f, 0.0f, 2048.0f);
    SkyRelated38 d = make_sky_render_vertex(400.0f, 400.0f, 2048.0f, 2048.0f);
    Gfx *tri_end;
    Gfx *full_end;

    reset_sky_fakes();
    tri_end = skyRenderTri(commands, &a, &b, &c, 65536.0f, TRUE);
    expect_true(tri_end > commands && tri_end <= commands + 256,
                "sky triangle renderer emits bounded port geometry for a valid triangle");

    full_end = skyRenderFull(commands, &a, &b, &c, &d, 65536.0f);
    expect_true(full_end > commands && full_end <= commands + 256,
                "sky quad renderer emits bounded port geometry for a valid quad");
    expect_true(fake_vertex_storage_used >= 7 && fake_matrix_storage_used >= 4,
                "sky primitive renderers allocate geometry and projection state");
}

static void test_sky_clamp_and_round_boundaries(void)
{
    expect_close(skyClamp(-1.0f, 0.0f, 10.0f), 0.0f, 0.0f,
                 "sky clamp floors values below its lower bound");
    expect_close(skyClamp(0.0f, 0.0f, 10.0f), 0.0f, 0.0f,
                 "sky clamp keeps its lower bound inclusive");
    expect_close(skyClamp(5.0f, 0.0f, 10.0f), 5.0f, 0.0f,
                 "sky clamp preserves values inside the range");
    expect_close(skyClamp(10.0f, 0.0f, 10.0f), 10.0f, 0.0f,
                 "sky clamp keeps its upper bound inclusive");
    expect_close(skyClamp(11.0f, 0.0f, 10.0f), 10.0f, 0.0f,
                 "sky clamp caps values above its upper bound");

    expect_close(skyRound(0.49f), 0.0f, 0.0f,
                 "sky round truncates a value below the half step");
    expect_close(skyRound(0.5f), 1.0f, 0.0f,
                 "sky round rounds an exact positive half step upward");
    expect_close(skyRound(1.49f), 1.0f, 0.0f,
                 "sky round keeps a value below the next half step down");
    expect_close(skyRound(1.5f), 2.0f, 0.0f,
                 "sky round rounds the next positive half step upward");
}

static void test_sky_fixed_point_conversion_is_saturated_and_signed(void)
{
    u32 positive_limit;
    u32 negative_limit;

    expect_true(sub_GAME_7F094298(0.0f) == 0u,
                "sky fixed-point conversion maps zero to zero");
    expect_true(sub_GAME_7F094298(1.0f) == 0x00010000u,
                "sky fixed-point conversion uses 16 fractional bits");
    expect_true(sub_GAME_7F094298(1.5f) == 0x00018000u,
                "sky fixed-point conversion preserves positive fractions");
    expect_true(sub_GAME_7F094298(-1.5f) == 0xfffe8000u,
                "sky fixed-point conversion preserves negative two's-complement fractions");

    positive_limit = sub_GAME_7F094298(32767.9f);
    negative_limit = sub_GAME_7F094298(-32767.9f);
    expect_true(sub_GAME_7F094298(40000.0f) == positive_limit,
                "sky fixed-point conversion saturates positive overflow");
    expect_true(sub_GAME_7F094298(-40000.0f) == negative_limit,
                "sky fixed-point conversion saturates negative overflow");
}

static void test_sky_edge_intersection_preserves_geometry(void)
{
    coord3d base = {0.0f, 10.0f, 0.0f};
    coord3d reference = {10.0f, -10.0f, 20.0f};
    coord3d result = {99.0f, 99.0f, 99.0f};

    skyCalculateEdgeVertex(&base, &reference, &result);
    expect_close(result.x, 5.0f, 0.0001f,
                 "sky edge intersection interpolates the x coordinate");
    expect_close(result.y, 0.0f, 0.0001f,
                 "sky edge intersection lies on the waterline");
    expect_close(result.z, 10.0f, 0.0001f,
                 "sky edge intersection interpolates the z coordinate");
}

static void test_sky_vertex_equality_uses_euclidean_pixel_distance(void)
{
    SkyRelated38 left;
    SkyRelated38 right;

    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    expect_true(skyVerticesAreTheSame(&left, &right),
                "identical sky vertices compare equal");

    right.unk28 = 0.99f;
    expect_true(skyVerticesAreTheSame(&left, &right),
                "sub-pixel sky vertices compare equal");

    right.unk28 = 1.0f;
    expect_true(!skyVerticesAreTheSame(&left, &right),
                "sky vertex equality excludes a one-pixel separation");

    right.unk28 = 0.6f;
    right.unk2c = 0.8f;
    expect_true(!skyVerticesAreTheSame(&left, &right),
                "sky vertex equality uses radial rather than per-axis distance");
}

int main(void)
{
    test_sky_world_position_uses_viewport_and_water_concavity();
    test_sky_corner_intersections_cover_air_water_and_range_cap();
    test_sky_colour_policy_blends_environment_and_layer();
    test_sky_projection_handles_identity_zero_and_negative_w();
    test_sky_stage_and_tick_state_are_deterministic();
    test_sky_render_clears_each_viewport_shape();
    test_sky_render_covers_every_corner_topology();
    test_sky_render_legacy_water_path_emits_textured_fan();
    test_sky_render_primitives_reject_degenerate_geometry();
    test_sky_render_primitives_emit_port_geometry();
    test_sky_clamp_and_round_boundaries();
    test_sky_fixed_point_conversion_is_saturated_and_signed();
    test_sky_edge_intersection_preserves_geometry();
    test_sky_vertex_equality_uses_euclidean_pixel_distance();

    if (failures != 0)
    {
        fprintf(stderr, "%d sky policy test(s) failed\n", failures);
        return 1;
    }

    puts("sky policy tests passed");
    return 0;
}
