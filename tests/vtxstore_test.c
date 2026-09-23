#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#ifdef __unix__
#include <sys/mman.h>
#endif

#include <ultra64.h>
#include "bondtypes.h"
#include "memp.h"

/* Keep the test boundary explicit so pointer identity is checked at the same
 * host width as the PC implementation rather than through an integer cast. */
extern uintptr_t vtxstore_allocate(s32 arg0, s32 type, s32 arg2, s32 arg3);
extern void sub_GAME_7F09B820(void);
extern void sub_GAME_7F09BBBC(void);
extern void sub_GAME_7F09C044(Vertex *arg0);
extern void sub_GAME_7F09BAC4(s32 find, s32 replacement);
extern Vertex *dword_CODE_bss_8007A0E0;
extern Vertex *dword_CODE_bss_8007A0E4;
extern s32 dword_CODE_bss_8007A0D0;
extern s32 dword_CODE_bss_8007A0D4;
extern s32 dword_CODE_bss_8007A0D8;
extern s32 dword_CODE_bss_8007A0DC;
extern s16 word_CODE_bss_8007A0F2;

static int failures;
static s32 fake_player_count = 1;
static s32 fake_stage = 10;
static size_t fake_pool_offset;
static u8 *fake_pool;
static int fake_pool_is_mapped;
static PropRecord *fake_active_tail;
static s32 fake_node_rwdata;
static int fake_compact_callback_count;

enum { FAKE_POOL_BYTES = 48000 };

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

s32 getPlayerCount(void)
{
    return fake_player_count;
}

s32 lvlGetCurrentStageToLoad(void)
{
    return fake_stage;
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    size_t aligned_size = ((size_t)bytes + 15u) & ~15u;
    void *result;

    (void)bank;
    if (fake_pool_offset + aligned_size > FAKE_POOL_BYTES)
    {
        return NULL;
    }

    result = fake_pool + fake_pool_offset;
    memset(result, 0, aligned_size);
    fake_pool_offset += aligned_size;
    return result;
}

static void reset_fake_pool(void)
{
    if (fake_pool == NULL)
    {
#ifdef __unix__
        fake_pool = mmap(NULL, FAKE_POOL_BYTES, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (fake_pool == MAP_FAILED)
        {
            fake_pool = NULL;
        }
        else
        {
            fake_pool_is_mapped = 1;
        }
#endif
        if (fake_pool == NULL)
        {
            fake_pool = malloc((size_t)FAKE_POOL_BYTES);
        }
    }
    fake_pool_offset = 0;
    memset(fake_pool, 0, FAKE_POOL_BYTES);
}

PropRecord *chrpropGetActiveTail(void)
{
    return fake_active_tail;
}

union ModelRwData *modelGetNodeRwData(Model *model, ModelNode *node)
{
    (void)model;
    (void)node;
    return (union ModelRwData *)&fake_node_rwdata;
}

void sub_GAME_7F056690(void)
{
    fake_compact_callback_count++;
}

static void test_reset_selects_expected_pool_sizes(void)
{
    fake_player_count = 1;
    fake_stage = 10;
    reset_fake_pool();
    sub_GAME_7F09B820();
    expect_true(dword_CODE_bss_8007A0D0 == 500 &&
                    dword_CODE_bss_8007A0D4 == 20 &&
                    dword_CODE_bss_8007A0D8 == 1500 &&
                    dword_CODE_bss_8007A0DC == 40,
                "single-player ordinary stages use the ordinary vertex-store capacities");

    fake_stage = 0x1E;
    reset_fake_pool();
    sub_GAME_7F09B820();
    expect_true(dword_CODE_bss_8007A0D0 == 500 &&
                    dword_CODE_bss_8007A0D4 == 20 &&
                    dword_CODE_bss_8007A0D8 == 500 &&
                    dword_CODE_bss_8007A0DC == 20,
                "stage 0x1E selects the constrained vertex-store capacities");

    fake_player_count = 1;
    fake_stage = 0x1D;
    reset_fake_pool();
    sub_GAME_7F09B820();
    expect_true(dword_CODE_bss_8007A0D8 == 500 &&
                    dword_CODE_bss_8007A0DC == 20,
                "stage 0x1D uses the same constrained secondary pool as stage 0x1E");

    fake_player_count = 2;
    fake_stage = 10;
    reset_fake_pool();
    sub_GAME_7F09B820();
    expect_true(dword_CODE_bss_8007A0D0 == 3000 &&
                    dword_CODE_bss_8007A0D4 == 80 &&
                    dword_CODE_bss_8007A0D8 == 500 &&
                    dword_CODE_bss_8007A0DC == 20,
                "multiplayer uses the expanded primary and constrained secondary pools");
}

static void test_allocate_preserves_pointer_width_and_splits_free_blocks(void)
{
    uintptr_t first;
    uintptr_t second;

    fake_player_count = 1;
    fake_stage = 10;
    reset_fake_pool();
    sub_GAME_7F09B820();

    first = vtxstore_allocate(4, 0xCCCC, 11, 22);
    second = vtxstore_allocate(4, 0xCCCC, 33, 44);
    expect_true(first == (uintptr_t)dword_CODE_bss_8007A0E0,
                "the first vertex-store allocation returns the full host pointer");
    expect_true(second == first + (4u * sizeof(Vertex)),
                "a second allocation consumes the next split free block");
}

static void test_allocate_rejects_bad_requests_and_reuses_merged_space(void)
{
    uintptr_t first;
    uintptr_t second;
    uintptr_t reused;
    uintptr_t secondary;

    fake_player_count = 1;
    fake_stage = 10;
    reset_fake_pool();
    sub_GAME_7F09B820();

    expect_true(vtxstore_allocate(4, 0x1234, 0, 0) == 0,
                "an unknown vertex-store type is rejected");
    expect_true(vtxstore_allocate(501, 0xCCCC, 0, 0) == 0,
                "an allocation larger than the free block is rejected");

    secondary = vtxstore_allocate(4, 0xB0B, 0, 0);
    expect_true(secondary == (uintptr_t)dword_CODE_bss_8007A0E4,
                "the secondary vertex-store pool returns its own full-width base");

    /* Reinitialize the primary pool so the free-list merge is isolated from
     * the secondary allocation above. */
    reset_fake_pool();
    sub_GAME_7F09B820();
    first = vtxstore_allocate(4, 0xCCCC, 0, 0);
    second = vtxstore_allocate(4, 0xCCCC, 0, 0);
    sub_GAME_7F09C044((Vertex *)first);
    sub_GAME_7F09C044((Vertex *)second);
    reused = vtxstore_allocate(8, 0xCCCC, 0, 0);
    expect_true(reused == first,
                "freeing adjacent allocations merges them back into the original block");
}

static void test_free_releases_secondary_pool_space(void)
{
    uintptr_t secondary;
    uintptr_t reused;

    fake_player_count = 1;
    fake_stage = 10;
    reset_fake_pool();
    sub_GAME_7F09B820();

    secondary = vtxstore_allocate(4, 0xB0B, 0, 0);
    expect_true(secondary == (uintptr_t)dword_CODE_bss_8007A0E4,
                "secondary-pool allocation starts at the secondary base");
    expect_true(word_CODE_bss_8007A0F2 == 1496,
                "secondary-pool allocation reduces the secondary free count");

    sub_GAME_7F09C044((Vertex *)secondary);
    expect_true(word_CODE_bss_8007A0F2 == 1500,
                "freeing a secondary-pool allocation restores its free count");

    reused = vtxstore_allocate(8, 0xB0B, 0, 0);
    expect_true(reused == secondary,
                "a released secondary-pool block is immediately reusable");
}

static void test_free_preserves_internal_free_list_links(void)
{
    uintptr_t blocks[6];
    uintptr_t reused;
    int i;

    fake_player_count = 1;
    fake_stage = 10;
    reset_fake_pool();
    sub_GAME_7F09B820();

    for (i = 0; i < 6; i++)
    {
        blocks[i] = vtxstore_allocate(4, 0xCCCC, 0, 0);
        expect_true(blocks[i] != 0,
                    "the primary pool supplies the internal free-list fixture blocks");
    }

    /* Leave block 0 free, then create a free block whose successor is still
     * live.  Freeing block 2 must merge with block 3 and update the live
     * successor's back-link; freeing block 1 then exercises the predecessor
     * merge and its successor back-link update. */
    sub_GAME_7F09C044((Vertex *)blocks[0]);
    sub_GAME_7F09C044((Vertex *)blocks[3]);
    sub_GAME_7F09C044((Vertex *)blocks[2]);
    sub_GAME_7F09C044((Vertex *)blocks[1]);

    reused = vtxstore_allocate(8, 0xCCCC, 0, 0);
    expect_true(reused == blocks[0],
                "an internal merged free block can be split again from its original address");
    expect_true(vtxstore_allocate(4, 0xCCCC, 0, 0) == blocks[0] + 8u * sizeof(Vertex),
                "splitting an internal free block keeps the following live block linked");
}

static void test_fix_refs_walks_object_models_without_pointer_narrowing(void)
{
    PropRecord prop;
    ObjectRecord object;
    Model model;
    ModelFileHeader header;
    ModelNode root;
    ModelNode child;
    ModelNode sibling;

    memset(&prop, 0, sizeof(prop));
    memset(&object, 0, sizeof(object));
    memset(&model, 0, sizeof(model));
    memset(&header, 0, sizeof(header));
    memset(&root, 0, sizeof(root));
    memset(&child, 0, sizeof(child));
    memset(&sibling, 0, sizeof(sibling));
    prop.type = 1;
    prop.obj = &object;
    object.model = &model;
    model.obj = &header;
    header.RootNode = &root;
    root.Opcode = 0x18;
    fake_active_tail = &prop;
    fake_node_rwdata = 0x11223344;
#if UINTPTR_MAX > UINT32_MAX
    expect_true((uintptr_t)&model > UINT32_MAX,
                "reference-repair fixture places the model above the 32-bit address range");
#endif

    sub_GAME_7F09BAC4(0x11223344, 0x55667788);
    expect_true(fake_node_rwdata == 0x55667788,
                "vtxstore reference repair updates object model node data through the full pointer");

    /* The node walk is depth-first: a non-reference node must descend into
     * Child before it searches a sibling through Next/Parent.  Keep the
     * model pointer host-width fixture in place while exercising both edges. */
    root.Opcode = 0x01;
    root.Child = &child;
    child.Parent = &root;
    child.Opcode = 0x18;
    fake_node_rwdata = 0x22334455;
    sub_GAME_7F09BAC4(0x22334455, 0x66778899);
    expect_true(fake_node_rwdata == 0x66778899,
                "vtxstore reference repair descends through a child node");

    root.Child = NULL;
    root.Next = &sibling;
    sibling.Parent = &root;
    sibling.Opcode = 0x18;
    fake_node_rwdata = 0x33445566;
    sub_GAME_7F09BAC4(0x33445566, 0x778899AA);
    expect_true(fake_node_rwdata == 0x778899AA,
                "vtxstore reference repair follows a sibling through its parent walk");

    prop.type = 3;
    fake_node_rwdata = 0x11223344;
    sub_GAME_7F09BAC4(0x11223344, 0x55667788);
    expect_true(fake_node_rwdata == 0x11223344,
                "vtxstore reference repair ignores non-object active props");

    fake_active_tail = NULL;
}

static void test_tick_merges_duplicate_secondary_batches(void)
{
    uintptr_t first;
    uintptr_t second;
    uintptr_t merged;

    fake_player_count = 1;
    fake_stage = 10;
    reset_fake_pool();
    sub_GAME_7F09B820();
    fake_active_tail = NULL;
    fake_compact_callback_count = 0;

    first = vtxstore_allocate(1200, 0xB0B, 77, 88);
    second = vtxstore_allocate(100, 0xB0B, 77, 88);
    expect_true(first == (uintptr_t)dword_CODE_bss_8007A0E4 &&
                    second == first + (1200u * sizeof(Vertex)) &&
                    word_CODE_bss_8007A0F2 == 200,
                "duplicate secondary batches can be allocated before the merge tick");

    sub_GAME_7F09BBBC();
    expect_true(word_CODE_bss_8007A0F2 == 300 &&
                    fake_compact_callback_count == 1,
                "the merge tick combines duplicate batches and invokes low-space compaction");

    merged = vtxstore_allocate(250, 0xB0B, 0, 0);
    expect_true(merged == first + (1200u * sizeof(Vertex)),
                "the merged free block is reusable at the end of the surviving batch");
}

int main(void)
{
    test_reset_selects_expected_pool_sizes();
    test_allocate_preserves_pointer_width_and_splits_free_blocks();
    test_allocate_rejects_bad_requests_and_reuses_merged_space();
    test_free_releases_secondary_pool_space();
    test_free_preserves_internal_free_list_links();
    test_fix_refs_walks_object_models_without_pointer_narrowing();
    test_tick_merges_duplicate_secondary_batches();

    if (failures != 0)
    {
#ifdef __unix__
        if (fake_pool_is_mapped)
        {
            munmap(fake_pool, FAKE_POOL_BYTES);
        }
        else
#endif
        {
            free(fake_pool);
        }
        fprintf(stderr, "%d vtxstore test(s) failed\n", failures);
        return 1;
    }

#ifdef __unix__
    if (fake_pool_is_mapped)
    {
        munmap(fake_pool, FAKE_POOL_BYTES);
    }
    else
#endif
    {
        free(fake_pool);
    }
    puts("vtxstore tests passed");
    return 0;
}
