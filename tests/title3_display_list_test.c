#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "title3.h"

static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static u8 opcode(const Gfx *command)
{
    return (u8)(command->words.w0 >> 24);
}

static void test_small_vertex_counts_emit_no_geometry(void)
{
    const s32 counts[] = {-1, 0, 2};
    Vtx vertices[32];
    Gfx commands[3];
    size_t i;

    memset(vertices, 0, sizeof(vertices));

    for (i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i)
    {
        memset(commands, 0xa5, sizeof(commands));

        expect(sub_GAME_7F01BFF8(commands, vertices, counts[i]) == commands + 1,
               "vertex counts below three consume only the end-list command");
        expect(opcode(&commands[0]) == (u8)G_ENDDL,
               "vertex counts below three do not emit a vertex command");
        expect(commands[0].words.w1 == 0,
               "the end-list command clears its payload word");
        expect(commands[1].words.w0 == (uintptr_t)0xa5a5a5a5a5a5a5a5ULL,
               "small vertex counts do not write a second display-list command");
    }
}

static void test_three_vertices_emit_one_triangle(void)
{
    Vtx vertices[32];
    Gfx commands[3];
    Gfx *end;

    memset(vertices, 0, sizeof(vertices));
    memset(commands, 0, sizeof(commands));

    end = sub_GAME_7F01BFF8(commands, vertices, 3);

    expect(end == commands + 3,
           "three vertices emit a vertex command, one triangle, and end-list");
    expect(opcode(&commands[0]) == (u8)G_VTX,
           "three vertices start with a vertex command");
    expect(opcode(&commands[1]) == (u8)G_TRI1,
           "three vertices emit a triangle command");
    expect(opcode(&commands[2]) == (u8)G_ENDDL,
           "three vertices terminate the display list");
}

static void test_gunbarrel_hole_handles_nonzero_segments(void)
{
    struct s_display_list_something records[4];
    struct s_display_list_something sentinel;

    memset(records, 0, sizeof(records));
    sentinel = records[3];
    createGunbarrelRenderHole(records, 4);

    expect(records[0].unk0 == 0 && records[0].unk2 == -64,
           "gunbarrel hole starts at the expected angle");
    expect(records[1].unk0 == 64 && records[2].unk0 == -64,
           "gunbarrel hole emits both sides of an interior segment");
    expect(records[3].unk0 == 0 && records[3].unk2 == 64,
           "gunbarrel hole closes at the expected angle");
    expect(records[3].unkF == 0,
           "gunbarrel hole initializes the final record alpha field");
    expect(memcmp(&records[3], &sentinel, sizeof(sentinel)) != 0,
           "gunbarrel hole writes the expected positive segment records");
}

static void test_gunbarrel_hole_rejects_zero_segments(void)
{
    struct s_display_list_something record;
    unsigned char before[sizeof(record)];

    memset(&record, 0xa5, sizeof(record));
    memcpy(before, &record, sizeof(record));

    createGunbarrelRenderHole(&record, 0);

    expect(memcmp(&record, before, sizeof(record)) == 0,
           "a zero segment count does not divide by zero or write a record");
}

int main(void)
{
    test_small_vertex_counts_emit_no_geometry();
    test_three_vertices_emit_one_triangle();
    test_gunbarrel_hole_handles_nonzero_segments();
    test_gunbarrel_hole_rejects_zero_segments();

    if (failures != 0)
    {
        return 1;
    }

    puts("title3 display-list suite passed");
    return 0;
}
