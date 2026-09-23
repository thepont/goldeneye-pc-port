#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>
#include "dyn.h"
#include "memp.h"

void dynCalculateMembarLength(const char *text, f32 numerator, f32 denominator);

extern u8 *g_GfxBuffers[DYN_BUFFER_SLOT_COUNT];
extern u8 *g_VtxBuffers[DYN_BUFFER_SLOT_COUNT];
extern u8 *g_GfxMemPos;
extern u8 g_GfxActiveBufferIndex;
extern s32 g_GfxRequestedDisplayList;
extern s32 g_GfxSizesByPlayerCount[];
extern s32 g_VtxSizesByPlayerCount[];

static int failures;
static int player_count;
static const char *gfx_token;
static const char *vtx_token;
static int debug_registration_calls;
static void *allocation_arena;
static size_t allocation_offset;
static u32 allocation_sizes[4];
static u8 allocation_banks[4];
static size_t allocation_count;

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
    static union
    {
        long double alignment;
        u8 bytes[1024 * 1024];
    } arena;

    allocation_arena = arena.bytes;
    allocation_offset = 0;
    allocation_count = 0;
    memset(allocation_sizes, 0, sizeof(allocation_sizes));
    memset(allocation_banks, 0, sizeof(allocation_banks));
    debug_registration_calls = 0;
    player_count = 2;
    gfx_token = NULL;
    vtx_token = NULL;
    g_GfxSizesByPlayerCount[0] = DYN_DEFAULT_BUFFER_BYTES_1P;
    g_GfxSizesByPlayerCount[1] = DYN_DEFAULT_BUFFER_BYTES_2P;
    g_GfxSizesByPlayerCount[2] = DYN_DEFAULT_BUFFER_BYTES_3P;
    g_GfxSizesByPlayerCount[3] = DYN_DEFAULT_BUFFER_BYTES_4P;
    g_VtxSizesByPlayerCount[0] = DYN_DEFAULT_BUFFER_BYTES_1P;
    g_VtxSizesByPlayerCount[1] = DYN_DEFAULT_BUFFER_BYTES_2P;
    g_VtxSizesByPlayerCount[2] = DYN_DEFAULT_BUFFER_BYTES_3P;
    g_VtxSizesByPlayerCount[3] = DYN_DEFAULT_BUFFER_BYTES_4P;
    memset(g_GfxBuffers, 0, sizeof(g_GfxBuffers));
    memset(g_VtxBuffers, 0, sizeof(g_VtxBuffers));
    g_GfxMemPos = NULL;
    g_GfxActiveBufferIndex = 0;
    g_GfxRequestedDisplayList = FALSE;
}

s32 getPlayerCount(void)
{
    return player_count;
}

const char *tokenFind(s32 index, const char *token)
{
    (void)index;

    if (strcmp(token, "-mgfx") == 0)
    {
        return gfx_token;
    }
    if (strcmp(token, "-mvtx") == 0)
    {
        return vtx_token;
    }
    return NULL;
}

void debTryAdd(void *data, const char *name)
{
    (void)data;
    (void)name;
    debug_registration_calls++;
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    void *result = (u8 *)allocation_arena + allocation_offset;

    if (allocation_count < sizeof(allocation_sizes) / sizeof(allocation_sizes[0]))
    {
        allocation_sizes[allocation_count] = bytes;
        allocation_banks[allocation_count] = bank;
    }
    allocation_count++;
    allocation_offset += bytes;
    return result;
}

static void test_debug_registration_and_memory_setup(void)
{
    size_t gfx_half;
    size_t vtx_half;

    reset_fixture();
    gfx_token = " 64";
    vtx_token = " 32";

    dynInit();
    dynInitMemory();

    gfx_half = (size_t)64 * DYN_BYTES_PER_KIBIBYTE *
               (sizeof(Gfx) / DYN_N64_GFX_COMMAND_BYTES);
    vtx_half = (size_t)32 * DYN_BYTES_PER_KIBIBYTE;

    expect(debug_registration_calls == 1,
           "dynamic allocator initialization registers one debug notice");
    expect(g_GfxSizesByPlayerCount[1] == 64 * DYN_BYTES_PER_KIBIBYTE &&
               g_VtxSizesByPlayerCount[1] == 32 * DYN_BYTES_PER_KIBIBYTE,
           "allocator token overrides apply only to the active player-count budget");
    expect(allocation_count == 2 &&
               allocation_sizes[0] == gfx_half * DYN_DOUBLE_BUFFER_COUNT &&
               allocation_sizes[1] == vtx_half * DYN_DOUBLE_BUFFER_COUNT &&
               allocation_banks[0] == MEMPOOL_STAGE &&
               allocation_banks[1] == MEMPOOL_STAGE,
           "PC allocator reserves two sized display-list and vertex halves");
    expect(g_GfxBuffers[1] == g_GfxBuffers[0] + gfx_half &&
               g_GfxBuffers[2] == g_GfxBuffers[1] + gfx_half,
           "display-list buffers are split at the host-sized Gfx half-width");
    expect(g_VtxBuffers[1] == g_VtxBuffers[0] + vtx_half &&
               g_VtxBuffers[2] == g_VtxBuffers[1] + vtx_half &&
               g_GfxMemPos == g_VtxBuffers[0] &&
               g_GfxActiveBufferIndex == 0 &&
               g_GfxRequestedDisplayList == FALSE,
           "vertex buffers and allocator state start on buffer zero");
}

static void test_default_budget_for_each_player_count(void)
{
    static const s32 expected[] = {
        DYN_DEFAULT_BUFFER_BYTES_1P,
        DYN_DEFAULT_BUFFER_BYTES_2P,
        DYN_DEFAULT_BUFFER_BYTES_3P,
        DYN_DEFAULT_BUFFER_BYTES_4P
    };
    int i;

    for (i = 0; i < DYN_PLAYER_COUNT_BUCKETS; i++)
    {
        reset_fixture();
        player_count = i + 1;
        dynInitMemory();

        expect(g_GfxSizesByPlayerCount[i] == expected[i] &&
                   g_VtxSizesByPlayerCount[i] == expected[i],
               "each player-count bucket retains its default byte budget");
        expect(allocation_sizes[0] ==
                   (u32)(expected[i] * (sizeof(Gfx) / DYN_N64_GFX_COMMAND_BYTES) *
                         DYN_DOUBLE_BUFFER_COUNT) &&
                   allocation_sizes[1] ==
                   (u32)(expected[i] * DYN_DOUBLE_BUFFER_COUNT),
               "each player-count bucket allocates two correctly sized halves");
    }
}

static void test_display_list_queries_and_allocations(void)
{
    Gfx *master;
    Vtx *vertices;
    Mtx *matrix;
    Light *lights;
    u8 *bytes;
    Gfx *gdl;

    master = dynGetMasterDisplayList();
    expect(master == (Gfx *)g_GfxBuffers[0] && g_GfxRequestedDisplayList == TRUE,
           "master display-list access marks the frame as requested");

    gdl = master + 4;
    expect(dynGetFreeGfx(gdl) == (s32)(g_GfxBuffers[1] - (u8 *)gdl) / (s32)sizeof(Gfx) &&
               dynGetFreeGfx2(gdl) == (s32)(g_GfxBuffers[1] - (u8 *)gdl) / (s32)sizeof(Gfx),
           "display-list free-space queries report the inactive half in Gfx units");

    vertices = dynAllocateVertices(2);
    expect((u8 *)vertices == g_VtxBuffers[0] &&
               g_GfxMemPos == g_VtxBuffers[0] + 2 * sizeof(Vtx),
           "vertex allocation advances the shared vertex cursor by Vtx size");

    matrix = dynAllocateMatrix();
    expect((u8 *)matrix == g_VtxBuffers[0] + 2 * sizeof(Vtx) &&
               g_GfxMemPos == (u8 *)matrix + sizeof(Mtx),
           "matrix allocation follows vertices and advances by one Mtx");

    lights = dynAllocateLights(2);
    expect((u8 *)lights == (u8 *)matrix + sizeof(Mtx) &&
               g_GfxMemPos == (u8 *)lights + 2 * sizeof(Light),
           "light allocation advances by the requested light count");

    bytes = dynAllocate(1);
    expect(bytes == (u8 *)lights + 2 * sizeof(Light) &&
               g_GfxMemPos == bytes + 16,
           "generic allocation rounds its request up to sixteen-byte alignment");
}

static void test_buffer_swap_and_free_vertex_queries(void)
{
    s32 free_bytes;

    free_bytes = dynGetFreeVtx();
    expect(free_bytes == (s32)(g_VtxBuffers[1] - g_GfxMemPos),
           "free vertex space reports the remaining active-half bytes");

    dynSwapBuffers();
    expect(g_GfxActiveBufferIndex == 1 && g_GfxRequestedDisplayList == FALSE &&
               g_GfxMemPos == g_VtxBuffers[1],
           "buffer swap selects the other half and resets the vertex cursor");

    dynSwapBuffers();
    expect(g_GfxActiveBufferIndex == 0 && g_GfxMemPos == g_VtxBuffers[0],
           "a second buffer swap returns to buffer zero");

    dynRemovedFunc(NULL);
    dynDrawMembars((Gfx *)g_GfxBuffers[0]);
    dynCalculateMembarLength("abc", -1.0f, 1.0f);
    dynCalculateMembarLength("a", 1.0f, 1.0f);
    expect(1, "debug memory-bar hooks remain callable without side effects");
}

int main(void)
{
    reset_fixture();
    test_default_budget_for_each_player_count();
    test_debug_registration_and_memory_setup();
    test_display_list_queries_and_allocations();
    test_buffer_swap_and_free_vertex_queries();

    if (failures != 0)
    {
        return 1;
    }

    puts("dynamic allocator suite passed");
    return 0;
}
