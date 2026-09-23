#include <stdio.h>

#include <ultra64.h>
#include "bondtypes.h"
#include "stan.h"

s32 init_pathtable_something(struct PadRecord *pad, char *tilename,
                             struct StandTile **tile_stack);

static StandTile *matched_tile;
static StandTile *fallback_tile;
static s32 inside_result;
static s32 walk_result;
static int match_calls;
static int inside_calls;
static int fallback_calls;
static int walk_calls;
static char *last_tile_name;
static f32 walked_start_x;
static f32 walked_start_z;
static f32 walked_dest_x;
static f32 walked_dest_z;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

StandTilePoint *stanMatchTileName(char *id)
{
    match_calls++;
    last_tile_name = id;
    return (StandTilePoint *)matched_tile;
}

s32 isPointInsideTriStandTileUnscaled_Maybe(StandTile *tile, f32 p_x, f32 p_z)
{
    (void)tile;
    (void)p_x;
    (void)p_z;
    inside_calls++;
    return inside_result;
}

StandTile *sub_GAME_7F0AFB78(f32 *x, f32 *y, f32 *z, f32 arg3)
{
    (void)x;
    (void)y;
    (void)z;
    (void)arg3;
    fallback_calls++;
    return fallback_tile;
}

s32 walkTilesBetweenPoints_NoCallback(StandTile **tile_stack,
                                      f32 start_x, f32 start_z,
                                      f32 dest_x, f32 dest_z)
{
    walk_calls++;
    walked_start_x = start_x;
    walked_start_z = start_z;
    walked_dest_x = dest_x;
    walked_dest_z = dest_z;
    expect(tile_stack != NULL && *tile_stack == fallback_tile,
           "path-table fallback walks from the selected fallback tile");
    return walk_result;
}

static void reset_fakes(void)
{
    matched_tile = NULL;
    fallback_tile = NULL;
    inside_result = 0;
    walk_result = 0;
    match_calls = 0;
    inside_calls = 0;
    fallback_calls = 0;
    walk_calls = 0;
    last_tile_name = NULL;
    walked_start_x = walked_start_z = 0.0f;
    walked_dest_x = walked_dest_z = 0.0f;
}

int main(void)
{
    PadRecord pad = {};
    StandTile tile_a = {};
    StandTile tile_b = {};
    StandTile *tile_stack = NULL;
    char tile_name[] = "A1";

    pad.pos.x = 12.0f;
    pad.pos.y = 3.0f;
    pad.pos.z = -8.0f;

    reset_fakes();
    matched_tile = &tile_a;
    inside_result = 1;
    expect(init_pathtable_something(&pad, tile_name, &tile_stack) == 1,
           "an already containing named tile is accepted directly");
    expect(tile_stack == &tile_a && match_calls == 1 && inside_calls == 1 &&
               fallback_calls == 0 && walk_calls == 0 && last_tile_name == tile_name,
           "direct path-table lookup does not run fallback traversal");

    reset_fakes();
    fallback_tile = &tile_b;
    walk_result = 1;
    expect(init_pathtable_something(&pad, tile_name, &tile_stack) == 2,
           "a fallback tile with a clear path is accepted");
    expect(tile_stack == &tile_b && fallback_calls == 1 && walk_calls == 1 &&
               walked_start_x == pad.pos.x && walked_start_z == pad.pos.z &&
               walked_dest_x == pad.pos.x && walked_dest_z == pad.pos.z,
           "fallback traversal uses the pad position as both endpoints");

    reset_fakes();
    matched_tile = &tile_a;
    inside_result = 0;
    fallback_tile = NULL;
    expect(init_pathtable_something(&pad, tile_name, &tile_stack) == 0 &&
               tile_stack == NULL,
           "an outside named tile with no fallback is rejected");
    expect(inside_calls == 1 && fallback_calls == 1 && walk_calls == 0,
           "an outside named tile attempts fallback without walking null data");

    reset_fakes();
    fallback_tile = &tile_b;
    walk_result = 0;
    expect(init_pathtable_something(&pad, tile_name, &tile_stack) == 0 &&
               tile_stack == NULL,
           "a blocked fallback path is rejected and clears the output");
    expect(walk_calls == 1,
           "blocked fallback paths are checked before being cleared");

    if (failures != 0)
    {
        fprintf(stderr, "%d path-table contract(s) failed\n", failures);
        return 1;
    }

    puts("path-table edge suite passed");
    return 0;
}
