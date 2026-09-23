#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>

#include "bg.h"
#include "bgroomtrans.h"
#include "bondview.h"
#include "dyn.h"
#include "glass.h"
#include "image_bank.h"

#define BULLET_SPARK_COUNT 20
#define MOVING_BULLET_SPARK_COUNT 50
#define HUD_SEGMENT_COUNT 46

extern s_bullet_spark g_BulletSparkArray[BULLET_SPARK_COUNT];
#ifndef VERSION_EU
extern s_moving_bullet_spark g_MovingBulletSparkArray[MOVING_BULLET_SPARK_COUNT];
#endif
extern struct rgba_u8 g_BulletSparkColors[8];

void bullet_sparks_reset(void);
void bullet_moving_sparks_reset(void);
#ifndef VERSION_EU
s_moving_bullet_spark *bullet_moving_spark_create(
    coord3d *position, coord3d *velocity, s32 type, f32 scale,
    f32 minimum_y, s16 room);
void bullet_moving_sparks_update(void);
void bullet_moving_sparks_render_all(Gfx *gdl, s32 zbufferMode);
#endif
f32 bullet_spark_get_depth(s_bullet_spark *spark);
void unused_7F0A3B70(s32 index, struct rgba_u8 *colour);
void unused_7F0A3BA4(s32 index, struct rgba_u8 *colour);

s32 g_ClockTimer;
f32 g_GlobalTimerDelta;

struct sImageTableEntry *explosion_smokeimages;
struct sImageTableEntry *scattered_explosions;
struct sImageTableEntry *flareimage2;

static struct sImageTableEntry smoke_images[4];
static struct sImageTableEntry scattered_images[4];
static struct sImageTableEntry flare_images[4];
static Mtxf fake_world_to_screen;
static int failures;
static int movement_calls;
static int render_visible;
static u32 random_value;
static Vtx render_vertices[4];
static coord3d render_room_position;
static Mtx render_projection;
static Mtxf render_view_to_world;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_int(long actual, long expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %ld expected %ld)\n",
                message, actual, expected);
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

u32 randomGetNext(void)
{
    return random_value;
}

Mtxf *camGetWorldToScreenMtxf(void)
{
    return &fake_world_to_screen;
}

void mtx4TransformVecInPlace(Mtxf *matrix, coord3d *vector)
{
    (void)matrix;
    vector->x += 1.0f;
    vector->y += 2.0f;
    vector->z *= 2.0f;
}

void sub_GAME_7F057D88(f32 *position, f32 *velocity, f32 delta)
{
    position[0] += velocity[0] * delta;
    position[1] += velocity[1] * delta;
    position[2] += velocity[2] * delta;
    movement_calls++;
}

bool camIsPosInScreen(coord3d *position, f32 margin)
{
    (void)position;
    (void)margin;
    return render_visible != 0;
}

Mtxf *currentPlayerGetViewToWorldMtxf(void)
{
    return &render_view_to_world;
}

Mtx *currentPlayerGetProjectionMatrix(void)
{
    return &render_projection;
}

s32 get_BONDdata_field_10E0(void)
{
    return 0;
}

Vtx *dynAllocateVertices(s32 count)
{
    expect_true(count == 4, "spark rendering requests a four-vertex billboard");
    return render_vertices;
}

Gfx *applyRoomMatrixToDisplayList(Gfx *gdl, s32 room)
{
    (void)room;
    gdl->words.w0 = 0x01000000;
    gdl->words.w1 = 0;
    return gdl + 1;
}

struct coord3d *getRoomPositionByIndex(s32 room)
{
    (void)room;
    return &render_room_position;
}

f32 get_room_data_float1(void)
{
    return 1.0f;
}

void texSelect(Gfx **gdlptr, struct sImageTableEntry *image,
               u32 arg2, s32 zbuffer_mode, u32 ulst)
{
    (void)image;
    (void)arg2;
    (void)zbuffer_mode;
    (void)ulst;
    (*gdlptr)->words.w0 = 0x02000000;
    (*gdlptr)->words.w1 = 0;
    *gdlptr += 1;
}

u32 osVirtualToPhysical(void *address)
{
    return (u32)(uintptr_t)address;
}

static void reset_fixture(void)
{
    memset(g_BulletSparkArray, 0, sizeof(g_BulletSparkArray));
#ifndef VERSION_EU
    memset(g_MovingBulletSparkArray, 0, sizeof(g_MovingBulletSparkArray));
#endif
    memset(smoke_images, 0, sizeof(smoke_images));
    memset(scattered_images, 0, sizeof(scattered_images));
    memset(flare_images, 0, sizeof(flare_images));
    explosion_smokeimages = smoke_images;
    scattered_explosions = scattered_images;
    flareimage2 = flare_images;
    random_value = 0;
    g_ClockTimer = 0;
    g_GlobalTimerDelta = 0.0f;
    movement_calls = 0;
    render_visible = 0;
    memset(render_vertices, 0, sizeof(render_vertices));
    memset(&render_room_position, 0, sizeof(render_room_position));
    memset(&render_projection, 0, sizeof(render_projection));
    memset(&render_view_to_world, 0, sizeof(render_view_to_world));
}

static void test_hud_segments_are_bounded_and_coloured(void)
{
    struct damage_display_val segments[HUD_SEGMENT_COUNT + 1];
    struct damage_display_val canary;
    s32 i;
    s32 full_segments;

    memset(segments, 0xA5, sizeof(segments));
    canary = segments[HUD_SEGMENT_COUNT];
    hudMakeDamageSegments(segments, 0, 0, 0.0f);

    full_segments = 0;
    for (i = 0; i < HUD_SEGMENT_COUNT; i++)
    {
        expect_int(segments[i].colour.a, 48,
                   "empty health keeps every HUD segment at the dim alpha");
        expect_int(segments[i].normal.x, 0,
                   "HUD health segments keep the normal X component zero");
        expect_int(segments[i].normal.y, 0,
                   "HUD health segments keep the normal Y component zero");
        expect_int(segments[i].normal.z, 0,
                   "HUD health segments keep the normal Z component zero");
        if (segments[i].colour.a == 255)
        {
            full_segments++;
        }
    }
    expect_true(memcmp(&segments[HUD_SEGMENT_COUNT], &canary, sizeof(canary)) == 0,
                "HUD generation does not write past its 46-vertex contract");
    expect_int(full_segments, 0,
               "empty health does not mark any segment fully filled");

    hudMakeDamageSegments(segments, 46, 1, 1.0f);
    expect_true(segments[0].colour.b == 255 && segments[0].colour.r != 255,
                "armour segments use the blue armour colour ramp");
    expect_true(segments[0].pos.x != segments[1].pos.x,
                "HUD segment pairs use distinct inner and outer radii");
    expect_true(segments[0].colour.a == 255 && segments[42].colour.a == 255,
                "full health fills the first 22 radial segment pairs");
    expect_int(segments[44].colour.a, 48,
               "the final radial segment remains dim at the full-health boundary");

    hudMakeDamageSegments(segments, 46, -1, 0.5f);
    expect_true(segments[0].colour.r == 255 && segments[0].colour.b != 255,
                "health segments use the red health colour ramp");
    expect_true(segments[0].colour.a == 255 && segments[2].colour.a == 255,
                "fractional health fills the completed leading pairs");
    expect_true(segments[4].colour.a >= 48 && segments[4].colour.a <= 255,
                "fractional health keeps the partial segment alpha bounded");
}

static void test_watch_geometry_and_display_lists(void)
{
    struct WatchVertex vertices[16];
    Gfx display_list[64];
    Gfx *end;
    struct WatchVertex *watch_end;
    s32 raw_vertices[4];

    memset(vertices, 0, sizeof(vertices));
    watch_end = setup_watch_rectangles(vertices, 10, 20, 30, 40, 1, 2);
    expect_true(watch_end == vertices + 4,
                "watch rectangle setup returns the first unused vertex");
    expect_int(vertices[0].coord1.x, 11,
               "watch rectangle setup applies the horizontal offset");
    expect_int(vertices[0].coord1.z, 22,
               "watch rectangle setup applies the vertical offset");
    expect_int(vertices[1].coord1.z, 62,
               "watch rectangle setup advances the first column vertically");
    expect_int(vertices[2].coord1.x, 41,
               "watch rectangle setup advances the second column horizontally");
    expect_int(vertices[0].color.r, 0x20,
               "watch rectangle setup writes the canonical green-red component");
    expect_int(vertices[0].color.g, 0x70,
               "watch rectangle setup writes the canonical green component");
    expect_int(vertices[0].color.a, 0xF0,
               "watch rectangle setup writes the canonical alpha");

    memset(vertices, 0, sizeof(vertices));
    sub_GAME_7F0A33F8(vertices, 16, 1.0f, 1);
    expect_int(vertices[0].coord1.x, 1,
               "watch ring generation seeds the centre vertex when requested");
    expect_int(vertices[0].color.g, 0x2C,
               "watch ring generation gives the centre vertex the green tint");
    expect_true(vertices[1].coord1.x != 1 || vertices[1].coord1.z != 0,
                "watch ring generation emits a scaled radial vertex");
    expect_int(vertices[1].color.a, 0xB0,
               "watch ring generation writes the translucent alpha");

    memset(display_list, 0, sizeof(display_list));
    end = buildGaugeBarDL(display_list, (uintptr_t)0x1234, 24);
    expect_true(end == display_list + 18,
                "gauge display-list construction emits the expected command count");
    expect_int(display_list[0].words.w0 >> 24, G_VTX,
               "gauge display-list construction starts with a vertex command");
    expect_int(display_list[17].words.w0 >> 24, (u8)G_ENDDL,
               "gauge display-list construction terminates with END_DL");

    memset(display_list, 0, sizeof(display_list));
    end = draw_watch_background(display_list, vertices, 0, 0);
    expect_true(end == display_list + 17,
                "watch background rectangle mode emits eight quads and END_DL");
    end = draw_watch_background(display_list, vertices, 0, 1);
    expect_true(end == display_list + 29,
                "watch background fan mode emits the centre fan and END_DL");

    memset(display_list, 0, sizeof(display_list));
    end = sub_GAME_7F0A3B40(display_list, raw_vertices);
    expect_true(end == display_list + 2,
                "watch rectangle display-list helper emits two commands");
    expect_int(display_list[1].words.w0, 0xB1000032,
               "watch rectangle display-list helper emits MODIFY_VTX");
}

static void test_spark_lifecycle_and_types(void)
{
    coord3d position = { 3.0f, 4.0f, 5.0f };
    coord3d velocity = { 2.0f, 3.0f, 4.0f };
    struct rgba_u8 colour = { 1, 2, 3, 4 };
    s_bullet_spark *spark;
    s32 i;

    reset_fixture();
    bullet_sparks_init(&g_BulletSparkArray[0], &position, 4, 2.0f, 7);
    expect_int(g_BulletSparkArray[0].lifetime, 1,
               "flare sparks use their one-frame lifetime");
    expect_near(g_BulletSparkArray[0].unk08, 1.0f,
                "flare sparks use one animation frame per age unit");
    expect_true(g_BulletSparkArray[0].unk0C == flareimage2,
                "flare sparks select the flare image bank");
    expect_near(g_BulletSparkArray[0].unk1c, 2.0f * M_SQRT2_F,
                "spark initialization applies the radial scale");
    expect_near(g_BulletSparkArray[0].unk20, 0.0f,
                "zero random angle produces zero perpendicular velocity");
    expect_int(g_BulletSparkArray[0].unk06, 7,
               "spark initialization preserves the room index");

    bullet_sparks_init(&g_BulletSparkArray[1], &position, 1, 1.0f, 2);
    expect_int(g_BulletSparkArray[1].lifetime, 11,
               "smoke sparks use the smoke animation lifetime");
    expect_true(g_BulletSparkArray[1].unk0C == explosion_smokeimages,
                "smoke sparks select the smoke image bank");
    bullet_sparks_init(&g_BulletSparkArray[2], &position, 3, 1.0f, 2);
    expect_int(g_BulletSparkArray[2].lifetime, 9,
               "scattered explosion sparks use their animation lifetime");
    expect_true(g_BulletSparkArray[2].unk0C == scattered_explosions,
                "scattered sparks select the scattered image bank");
    bullet_sparks_init(&g_BulletSparkArray[3], &position, 6, 1.0f, 2);
    expect_int(g_BulletSparkArray[3].lifetime, 100,
               "long-lived sparks use the 100-tick lifetime");
    expect_near(g_BulletSparkArray[3].unk08, 0.0f,
                "long-lived sparks do not advance animation frames");

    unused_7F0A3B70(1, &colour);
    expect_true(colour.r == 0xFF && colour.g == 0xFF &&
                    colour.b == 0xC8 && colour.a == 0xFF,
                "spark colour accessor reads the selected palette entry");
    colour.r = 10;
    colour.g = 20;
    colour.b = 30;
    colour.a = 40;
    unused_7F0A3BA4(7, &colour);
    memset(&colour, 0, sizeof(colour));
    unused_7F0A3B70(7, &colour);
    expect_true(colour.r == 10 && colour.g == 20 &&
                    colour.b == 30 && colour.a == 40,
                "spark colour accessor writes the selected palette entry");

    bullet_sparks_reset();
    spark = bullet_spark_create(&position, 2, 1.0f, 3);
    expect_true(spark == &g_BulletSparkArray[0],
                "spark creation takes the first inactive slot");
    expect_int(spark->lifetime, 11,
               "default spark creation uses the smoke lifetime");
    for (i = 0; i < BULLET_SPARK_COUNT; i++)
    {
        g_BulletSparkArray[i].lifetime = 1;
    }
    expect_true(bullet_spark_create(&position, 2, 1.0f, 3) == NULL,
                "spark creation reports exhaustion instead of overwriting active sparks");

    g_ClockTimer = 1;
    g_BulletSparkArray[0].age = 2;
    g_BulletSparkArray[0].lifetime = 3;
    g_BulletSparkArray[1].age = -2;
    g_BulletSparkArray[1].lifetime = 3;
    bullet_sparks_update();
    expect_int(g_BulletSparkArray[0].age, 3,
               "spark update advances active age by the frame timer");
    expect_int(g_BulletSparkArray[0].lifetime, 0,
               "spark update expires at the lifetime boundary");
    expect_int(g_BulletSparkArray[1].age, -1,
               "spark update preserves negative pre-roll age");
    expect_int(g_BulletSparkArray[1].lifetime, 3,
               "spark update does not expire a negative pre-roll spark");

    g_BulletSparkArray[0].unk10 = 2.0f;
    g_BulletSparkArray[0].unk14 = 3.0f;
    g_BulletSparkArray[0].unk18 = 4.0f;
    expect_near(bullet_spark_get_depth(&g_BulletSparkArray[0]), -8.0f,
                "spark depth is the negated transformed screen-space Z");
}

#ifndef VERSION_EU
static void test_moving_spark_lifecycle(void)
{
    coord3d position = { 1.0f, 10.0f, 3.0f };
    coord3d velocity = { 2.0f, 4.0f, 6.0f };
    s_moving_bullet_spark *spark;
    s32 i;

    reset_fixture();
    spark = bullet_moving_spark_create(&position, &velocity, 1, 1.0f,
                                       12.0f, 4);
    expect_true(spark == &g_MovingBulletSparkArray[0],
                "moving spark creation takes the first inactive slot");
    expect_near(spark->velocity.x, 2.0f,
                "moving spark creation copies velocity X");
    expect_near(spark->velocity.y, 4.0f,
                "moving spark creation copies velocity Y");
    expect_near(spark->unk38, 12.0f,
                "moving spark creation preserves the minimum-height cutoff");

    g_ClockTimer = 1;
    g_GlobalTimerDelta = 0.5f;
    bullet_moving_sparks_update();
    expect_int(movement_calls, 1,
               "moving spark update integrates an unexpired spark");
    expect_near(spark->unk00.unk10, 2.0f,
                "moving spark update advances position X");
    expect_near(spark->unk00.unk14, 12.0f,
                "moving spark update advances position Y");
    expect_true(spark->unk00.lifetime > 0,
                "moving spark survives exactly on the minimum-height boundary");

    spark->unk00.age = 0;
    spark->unk00.lifetime = 3;
    spark->unk00.unk14 = 0.0f;
    spark->velocity.y = 0.0f;
    spark->unk38 = 1.0f;
    bullet_moving_sparks_update();
    expect_int(spark->unk00.lifetime, 0,
               "moving spark expires when it falls below the minimum height");
    expect_int(movement_calls, 2,
               "moving spark performs its integration before the height check");

    spark->unk00.age = 2;
    spark->unk00.lifetime = 3;
    spark->unk00.unk14 = 10.0f;
    spark->unk38 = 0.0f;
    bullet_moving_sparks_update();
    expect_int(spark->unk00.lifetime, 0,
               "moving spark expires at its animation lifetime boundary");
    expect_int(movement_calls, 2,
               "moving spark does not integrate at its animation lifetime boundary");

    for (i = 0; i < MOVING_BULLET_SPARK_COUNT; i++)
    {
        g_MovingBulletSparkArray[i].unk00.lifetime = 1;
    }
    expect_true(bullet_moving_spark_create(&position, &velocity, 1, 1.0f,
                                           12.0f, 4) == NULL,
                "moving spark creation reports exhaustion without overwriting slots");

    bullet_moving_sparks_reset();
    for (i = 0; i < MOVING_BULLET_SPARK_COUNT; i++)
    {
        expect_int(g_MovingBulletSparkArray[i].unk00.lifetime, 0,
                   "moving spark reset clears every active slot");
    }
}
#endif

static void test_reset_and_update_facades(void)
{
    reset_fixture();
    g_BulletSparkArray[0].lifetime = 2;
    g_BulletSparkArray[0].age = 1;
#ifndef VERSION_EU
    g_MovingBulletSparkArray[0].unk00.lifetime = 2;
#endif
    bullet_sparks_reset_all();
    expect_int(g_BulletSparkArray[0].lifetime, 0,
               "all-spark reset clears stationary sparks");
#ifndef VERSION_EU
    expect_int(g_MovingBulletSparkArray[0].unk00.lifetime, 0,
               "all-spark reset clears moving sparks");
#endif

    g_BulletSparkArray[0].lifetime = 3;
    g_BulletSparkArray[0].age = 2;
    g_ClockTimer = 1;
    bullet_sparks_update_all();
    expect_int(g_BulletSparkArray[0].lifetime, 0,
               "all-spark update expires stationary sparks through the facade");
}

static void test_spark_rendering_respects_visibility_and_colour(void)
{
    coord3d position = { 2.0f, 3.0f, 4.0f };
    Gfx display_list[32];
    Gfx *cursor;
    s_bullet_spark *spark;

    reset_fixture();
    memset(display_list, 0, sizeof(display_list));

    cursor = display_list;
    bullet_sparks_render_all((Gfx *)&cursor, 1);
    expect_true(cursor == display_list,
                "spark render wrappers leave the command cursor unchanged for inactive sparks");

    spark = bullet_spark_create(&position, 1, 1.0f, 3);
    expect_true(spark != NULL, "spark render fixture creates a smoke spark");

    spark->age = -1;
    cursor = display_list;
    bullet_sparks_render((Gfx *)&cursor, 1);
    expect_true(cursor == display_list,
                "spark renderer ignores negative pre-roll age");

    spark->age = 0;
    render_visible = 0;
    cursor = display_list;
    bullet_sparks_render((Gfx *)&cursor, 1);
    expect_true(cursor == display_list,
                "spark renderer ignores billboards outside the camera screen");

    smoke_images[0].width = 3;
    smoke_images[0].height = 5;
    render_visible = 1;
    cursor = display_list;
    bullet_sparks_render((Gfx *)&cursor, 1);
    expect_true(cursor > display_list,
                "visible spark rendering appends commands to the display list");
    expect_true(render_vertices[0].v.cn[0] == spark->unk28 &&
                    render_vertices[0].v.cn[1] == spark->unk29 &&
                    render_vertices[0].v.cn[2] == spark->unk2A &&
                    render_vertices[0].v.cn[3] == spark->unk2B,
                "visible spark rendering preserves the spark RGBA colour");
    expect_int(render_vertices[0].v.tc[0], 3 << 5,
               "visible spark rendering uses the image width for S coordinates");
    expect_int(render_vertices[2].v.tc[1], 5 << 5,
               "visible spark rendering uses the image height for T coordinates");

    spark->lifetime = 0;
    cursor = display_list;
    bullet_moving_sparks_render_all((Gfx *)&cursor, 1);
    expect_true(cursor == display_list,
                "moving spark renderer skips all inactive moving sparks");
}

int main(void)
{
    test_hud_segments_are_bounded_and_coloured();
    test_watch_geometry_and_display_lists();
    test_spark_lifecycle_and_types();
#ifndef VERSION_EU
    test_moving_spark_lifecycle();
#endif
    test_reset_and_update_facades();
    test_spark_rendering_respects_visibility_and_colour();

    if (failures != 0)
    {
        fprintf(stderr, "%d glass2 contract(s) failed\n", failures);
        return 1;
    }

    puts("glass2 suite passed");
    return 0;
}
