#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <ultra64.h>
#include <gbi_extension.h>
#include "bg.h"
#include "lightfixture.h"

s_room_info g_BgRoomInfo[4];
extern s_lightfixture light_fixture_table[];
extern s16 current_light_fixture_slot;
extern s16 current_light_fixture_room;
extern struct s_darkened_light darkened_light_table[];
extern s32 cur_entry_darkened_light_table;

extern s32 lightFixtureFindFreeSlot(void);
extern void lightFixtureInitTables(void);
extern Vtx *lightFindVertexBaseForTri(Gfx *gfx, s32 room_index);
extern void extract_vertex_indices_from_triangle(Gfx *gfx, u32 tri_type,
                                                  s32 *idx1, s32 *idx2,
                                                  s32 *idx3);
extern void extract_vertex_coords_from_triangle(Gfx *gfx, u32 tri_type,
                                                 s32 room_index,
                                                 coord16 *out1,
                                                 coord16 *out2,
                                                 coord16 *out3);
extern void darken_vertex_in_room(Vtx *vertex, s32 room_index);
extern s32 darkened_light_table_contains_vertex(Vtx *vertex, s32 room_index);
extern void darken_triangle_in_room(Gfx *gfx, u32 tri_type, s32 room_index);
extern s32 darkened_light_table_contains_triangle(Gfx *gfx, u32 tri_type,
                                                   s32 room_index);
extern s32 lightIsCoordNearDarkenedVertex(coord16 *coord, s32 room_index);
extern void redarken_lights_in_room(s32 room_index);
extern void clear_light_fixturetable_in_room(s32 room_index);
extern void lightFixtureBreak(Gfx *hit_gfx, u32 tri_type, s32 room_index);

static f32 test_room_data_float1 = 1.0f;
static f32 test_room_data_float2 = 1.0f;
static coord3d test_room_origin;
static int test_shard_count;
static coord3d test_last_shard_position;

f32 get_room_data_float1(void)
{
    return test_room_data_float1;
}

f32 get_room_data_float2(void)
{
    return test_room_data_float2;
}

void getRoomPositionScaledByIndex(s32 index, coord3d *position)
{
    (void)index;
    *position = test_room_origin;
}

void glassCreateShard(coord3d *position, f32 rotX, f32 shard_size)
{
    (void)rotX;
    (void)shard_size;
    test_shard_count++;
    test_last_shard_position = *position;
}

static int failures;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void test_light_image_classification(void)
{
    static const s32 light_images[] = {
        IMAGE_WALL_LAMP,
        IMAGE_203_LIGHT,
        IMAGE_205_LIGHT,
        IMAGE_252_LIGHT,
        IMAGE_PANEL_LAMP,
        IMAGE_255_LIGHT,
        IMAGE_256_LIGHT,
        IMAGE_HANGING_LAMP,
        IMAGE_NEON_LAMP,
        IMAGE_LINEAR_LAMP
    };
    size_t i;

    for (i = 0; i < sizeof(light_images) / sizeof(light_images[0]); ++i)
    {
        expect_true(check_if_imageID_is_light(light_images[i]),
                    "registered lamp image is classified as a breakable light");
    }

    expect_true(!check_if_imageID_is_light(-1),
                "unknown image IDs are not classified as lights");
}

static void test_fixture_table_lifecycle_and_capacity(void)
{
    Gfx commands[2];

    memset(commands, 0, sizeof(commands));
    lightFixtureInitTables();
    expect_true(lightFixtureFindFreeSlot() == 0,
                "light fixture initialization exposes the first free slot");

    current_light_fixture_room = 7;
    lightFixtureEntryBegin(&commands[0]);
    lightFixtureEntryEnd(&commands[1]);
    expect_true(light_fixture_table[0].room_index == 7 &&
                    light_fixture_table[0].ptr_start_pertinent_DL == &commands[0] &&
                    light_fixture_table[0].ptr_end_pertinent_DL == &commands[1],
                "light fixture begin/end records the room and display-list range");

    memset(light_fixture_table, 0, sizeof(s_lightfixture) * 100);
    expect_true(lightFixtureFindFreeSlot() == 0,
                "clearing a fixture slot makes it reusable");
    for (s32 i = 0; i < 100; ++i)
    {
        light_fixture_table[i].room_index = (s16)(i + 1);
    }
    expect_true(lightFixtureFindFreeSlot() == 100,
                "a full fixture table reports its bounded capacity");
}

static void test_segmented_vertex_resolution_keeps_host_pointer_width(void)
{
    Gfx commands[2];
    Vtx vertices[4];
    const uintptr_t segmented_address = (uintptr_t)0x0e000010u;

    memset(commands, 0, sizeof(commands));
    memset(vertices, 0, sizeof(vertices));
    g_BgRoomInfo[0].vertices = vertices;

    /* gSPVertex stores the segmented N64 address in the display list. The
     * room base is a real host pointer and may be above the 32-bit address
     * range, so resolution must add the 24-bit offset without truncating the
     * base pointer. */
    gSPVertex(&commands[0], (Vtx *)(uintptr_t)segmented_address, 1, 0);
    expect_true(lightFindVertexBaseForTri(&commands[1], 0) == &vertices[1],
                "segmented room vertex resolution preserves a 64-bit host base");
}

static void test_triangle_index_decoding_is_portable_and_bounded(void)
{
    Gfx commands[2];
    s32 idx1;
    s32 idx2;
    s32 idx3;

    memset(commands, 0, sizeof(commands));
    gSPVertex(&commands[0], (Vtx *)(uintptr_t)0x0e000000u, 3, 0);
    gSP1Triangle(&commands[1], 1, 2, 3, 0);

    idx1 = idx2 = idx3 = -1;
    extract_vertex_indices_from_triangle(&commands[1], 0,
                                         &idx1, &idx2, &idx3);
    expect_true(idx1 == 1 && idx2 == 2 && idx3 == 3,
                "ordinary triangle decoding preserves vertex order on the PC layout");

    idx1 = idx2 = idx3 = -1;
    extract_vertex_indices_from_triangle(&commands[1], 99,
                                         &idx1, &idx2, &idx3);
    expect_true(idx1 == 0 && idx2 == 0 && idx3 == 0,
                "an unknown triangle encoding cannot leak uninitialized vertex indices");
}

static void test_tri4_index_decoding_uses_all_four_packed_triangles(void)
{
    static const s32 expected[4][3] = {
        {1, 2, 3},
        {4, 5, 6},
        {7, 8, 9},
        {10, 11, 12}
    };
    Gfx command;
    s32 tri_type;

    memset(&command, 0, sizeof(command));
    gSP4Triangles(&command,
                  1, 2, 3,
                  4, 5, 6,
                  7, 8, 9,
                  10, 11, 12);

    for (tri_type = 1; tri_type <= 4; ++tri_type)
    {
        s32 idx1 = -1;
        s32 idx2 = -1;
        s32 idx3 = -1;

        extract_vertex_indices_from_triangle(&command, (u32)tri_type,
                                             &idx1, &idx2, &idx3);
        expect_true(idx1 == expected[tri_type - 1][0] &&
                        idx2 == expected[tri_type - 1][1] &&
                        idx3 == expected[tri_type - 1][2],
                    "G_TRI4 decoding returns each packed triangle in source order");
    }
}

static void test_triangle_coordinates_and_darkening_share_decoded_indices(void)
{
    Gfx commands[2];
    Vtx vertices[4];
    coord16 out1;
    coord16 out2;
    coord16 out3;
    coord16 near_coord;
    coord16 boundary_coord;

    memset(commands, 0, sizeof(commands));
    memset(vertices, 0, sizeof(vertices));
    g_BgRoomInfo[1].vertices = vertices;
    vertices[0].v.ob[0] = 10;
    vertices[0].v.ob[1] = 20;
    vertices[0].v.ob[2] = 30;
    vertices[1].v.ob[0] = 100;
    vertices[1].v.ob[1] = 200;
    vertices[1].v.ob[2] = 300;
    vertices[2].v.ob[0] = -100;
    vertices[2].v.ob[1] = -200;
    vertices[2].v.ob[2] = -300;
    vertices[3].v.ob[0] = 400;
    vertices[3].v.ob[1] = 500;
    vertices[3].v.ob[2] = 600;
    gSPVertex(&commands[0], vertices, 4, 0);
    gSP1Triangle(&commands[1], 1, 2, 3, 0);

    extract_vertex_coords_from_triangle(&commands[1], 0, 1,
                                        &out1, &out2, &out3);
    expect_true(out1.x == 100 && out1.y == 200 && out1.z == 300 &&
                    out2.x == -100 && out2.y == -200 && out2.z == -300 &&
                    out3.x == 400 && out3.y == 500 && out3.z == 600,
                "triangle coordinate extraction follows the decoded vertex order");

    lightFixtureInitTables();
    vertices[1].v.cn[0] = 64;
    vertices[1].v.cn[1] = 64;
    vertices[1].v.cn[2] = 64;
    vertices[1].v.cn[3] = 64;
    vertices[2].v.cn[0] = 80;
    vertices[2].v.cn[1] = 80;
    vertices[2].v.cn[2] = 80;
    vertices[2].v.cn[3] = 80;
    vertices[3].v.cn[0] = 96;
    vertices[3].v.cn[1] = 96;
    vertices[3].v.cn[2] = 96;
    vertices[3].v.cn[3] = 96;
    darken_triangle_in_room(&commands[1], 0, 1);
    expect_true(vertices[1].v.cn[0] == 16 && vertices[2].v.cn[0] == 20 &&
                    vertices[3].v.cn[0] == 24 &&
                    darkened_light_table_contains_triangle(&commands[1], 0, 1) == 3,
                "darkening a triangle records and darkens each of its vertices");

    darken_triangle_in_room(&commands[1], 0, 1);
    expect_true(vertices[1].v.cn[0] == 16 && vertices[2].v.cn[0] == 20 &&
                    vertices[3].v.cn[0] == 24,
                "darkening a triangle twice remains idempotent");

    near_coord.x = vertices[1].v.ob[0];
    near_coord.y = vertices[1].v.ob[1];
    near_coord.z = vertices[1].v.ob[2] + 99;
    boundary_coord = near_coord;
    boundary_coord.z = vertices[1].v.ob[2] + 100;
    expect_true(lightIsCoordNearDarkenedVertex(&near_coord, 1) == 1 &&
                    lightIsCoordNearDarkenedVertex(&boundary_coord, 1) == 0,
                "near-light matching uses a strict room-scale Manhattan boundary");
    expect_true(lightIsCoordNearDarkenedVertex(&near_coord, 2) == 0,
                "near-light matching never crosses room boundaries");
}

static void test_room_clear_only_releases_matching_fixture_entries(void)
{
    lightFixtureInitTables();
    light_fixture_table[0].room_index = 4;
    light_fixture_table[1].room_index = 5;
    light_fixture_table[2].room_index = 4;

    clear_light_fixturetable_in_room(4);
    expect_true(light_fixture_table[0].room_index == 0 &&
                    light_fixture_table[1].room_index == 5 &&
                    light_fixture_table[2].room_index == 0 &&
                    current_light_fixture_room == 4,
                "room unload clears only that room's fixture entries");
}

static void test_break_path_darkens_once_and_spawns_deterministic_shards(void)
{
    Gfx commands[2];
    Vtx vertices[3];

    memset(commands, 0, sizeof(commands));
    memset(vertices, 0, sizeof(vertices));
    g_BgRoomInfo[1].vertices = vertices;
    vertices[0].v.ob[0] = 0;
    vertices[0].v.ob[1] = 0;
    vertices[0].v.ob[2] = 0;
    vertices[1].v.ob[0] = 20;
    vertices[1].v.ob[1] = 0;
    vertices[1].v.ob[2] = 0;
    vertices[2].v.ob[0] = 0;
    vertices[2].v.ob[1] = 20;
    vertices[2].v.ob[2] = 0;
    vertices[0].v.cn[0] = vertices[0].v.cn[1] = 100;
    vertices[0].v.cn[2] = vertices[0].v.cn[3] = 100;
    vertices[1].v.cn[0] = vertices[1].v.cn[1] = 100;
    vertices[1].v.cn[2] = vertices[1].v.cn[3] = 100;
    vertices[2].v.cn[0] = vertices[2].v.cn[1] = 100;
    vertices[2].v.cn[2] = vertices[2].v.cn[3] = 100;
    gSPVertex(&commands[0], vertices, 3, 0);
    gSP1Triangle(&commands[1], 0, 1, 2, 0);

    lightFixtureInitTables();
    light_fixture_table[0].room_index = 7;
    light_fixture_table[0].ptr_start_pertinent_DL = &commands[0];
    light_fixture_table[0].ptr_end_pertinent_DL = &commands[1];
    light_fixture_table[1].room_index = 1;
    light_fixture_table[1].ptr_start_pertinent_DL = &commands[1];
    light_fixture_table[1].ptr_end_pertinent_DL = &commands[2];
    test_room_origin.x = 1.0f;
    test_room_origin.y = 2.0f;
    test_room_origin.z = 3.0f;
    test_room_data_float1 = 1.0f;
    test_room_data_float2 = 1.0f;
    test_shard_count = 0;

    lightFixtureBreak(&commands[1], 0, 1);
    expect_true(vertices[0].v.cn[0] == 25 && vertices[1].v.cn[0] == 25 &&
                    vertices[2].v.cn[0] == 25 && test_shard_count > 0 &&
                    test_last_shard_position.x != 0.0f,
                "breaking a fixture darkens its triangle and emits edge shards");

    {
        int shard_count_after_first_break = test_shard_count;
        lightFixtureBreak(&commands[1], 0, 1);
        expect_true(test_shard_count == shard_count_after_first_break &&
                        vertices[0].v.cn[0] == 25,
                    "breaking an already-dark fixture does not duplicate effects");
    }
}

static void test_darkened_vertices_are_idempotent_and_restored(void)
{
    Vtx vertices[3];

    memset(vertices, 0, sizeof(vertices));
    g_BgRoomInfo[3].vertices = vertices;
    lightFixtureInitTables();
    vertices[2].v.cn[0] = 100;
    vertices[2].v.cn[1] = 80;
    vertices[2].v.cn[2] = 60;
    vertices[2].v.cn[3] = 40;

    darken_vertex_in_room(&vertices[2], 3);
    expect_true(vertices[2].v.cn[0] == 25 && vertices[2].v.cn[1] == 20 &&
                    vertices[2].v.cn[2] == 15 && vertices[2].v.cn[3] == 10 &&
                    cur_entry_darkened_light_table == 1,
                "breaking a light darkens each vertex channel once");

    darken_vertex_in_room(&vertices[2], 3);
    expect_true(vertices[2].v.cn[0] == 25 &&
                    cur_entry_darkened_light_table == 1,
                "breaking the same light twice does not darken it twice");

    vertices[2].v.cn[0] = 100;
    vertices[2].v.cn[1] = 80;
    vertices[2].v.cn[2] = 60;
    vertices[2].v.cn[3] = 40;
    redarken_lights_in_room(3);
    expect_true(vertices[2].v.cn[0] == 25 && vertices[2].v.cn[1] == 20 &&
                    vertices[2].v.cn[2] == 15 && vertices[2].v.cn[3] == 10,
                "reloading a room reapplies its persistent darkened-light record");
}

int main(void)
{
    test_light_image_classification();
    test_fixture_table_lifecycle_and_capacity();
    test_segmented_vertex_resolution_keeps_host_pointer_width();
    test_triangle_index_decoding_is_portable_and_bounded();
    test_tri4_index_decoding_uses_all_four_packed_triangles();
    test_triangle_coordinates_and_darkening_share_decoded_indices();
    test_room_clear_only_releases_matching_fixture_entries();
    test_break_path_darkens_once_and_spawns_deterministic_shards();
    test_darkened_vertices_are_idempotent_and_restored();

    if (failures != 0)
    {
        fprintf(stderr, "%d light fixture test(s) failed\n", failures);
        return 1;
    }

    puts("light fixture tests passed");
    return 0;
}
