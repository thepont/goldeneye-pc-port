#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ultra64.h>
#include <memp.h>
#include "image.h"
#include "title.h"

void set_mt_tex_alloc(void);
void alloc_intro_matrices(void);

struct texpool ptr_texture_alloc_start;
s32 g_TexCacheCount;
u32 bytes;

Mtx *matrixBufferRareLogo0;
Mtx *matrixBufferGunbarrel0;
Mtx *matrixBufferRareLogo1;
Mtx *matrixBufferRareLogo2;
Mtx *matrixBufferGunbarrel1;
Mtx *matrixBufferIntroBackdrop;
Mtx *matrixBufferIntroBond;
u32 D_8002A7D0;

static union
{
    long double alignment;
    u8 bytes[0x80000];
} allocation_storage;
static size_t allocation_offset;
static u32 allocation_sizes[16];
static u8 allocation_banks[16];
static int allocation_count;
static int failures;
static const char *mt_option;
static int token_calls;
static struct texpool *initialized_pool;
static u8 *initialized_pool_start;
static s32 initialized_pool_size;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void reset_allocator(void)
{
    allocation_offset = 0;
    allocation_count = 0;
    memset(allocation_sizes, 0, sizeof(allocation_sizes));
    memset(allocation_banks, 0, sizeof(allocation_banks));
    initialized_pool = NULL;
    initialized_pool_start = NULL;
    initialized_pool_size = 0;
}

void *mempAllocBytesInBank(u32 size, u8 bank)
{
    void *result;

    if (allocation_count < (int)(sizeof(allocation_sizes) / sizeof(allocation_sizes[0])))
    {
        allocation_sizes[allocation_count] = size;
        allocation_banks[allocation_count] = bank;
    }
    allocation_count++;

    if (size > sizeof(allocation_storage.bytes) - allocation_offset)
    {
        return NULL;
    }

    result = &allocation_storage.bytes[allocation_offset];
    allocation_offset += size;
    return result;
}

const char *tokenFind(s32 token_index, const char *token)
{
    expect(token_index == 1 && strcmp(token, "-mt") == 0,
           "texture allocation queries the -mt option in the expected token slot");
    token_calls++;
    return mt_option;
}

void texInitPool(struct texpool *pool, u8 *start, s32 size)
{
    initialized_pool = pool;
    initialized_pool_start = start;
    initialized_pool_size = size;
}

static void test_texture_pool_default_and_override(void)
{
    g_TexCacheCount = 31;
    bytes = 0x6DDD0;
    mt_option = NULL;
    token_calls = 0;
    reset_allocator();

    set_mt_tex_alloc();

    expect(g_TexCacheCount == 0,
           "texture allocation reset clears the texture cache count");
    expect(bytes == 0x6DDD0 && allocation_count == 1 &&
               allocation_sizes[0] == 0x6DDD0 && allocation_banks[0] == MEMPOOL_STAGE,
           "texture allocation uses the default stage-pool size when -mt is absent");
    expect(token_calls == 1 && initialized_pool == &ptr_texture_alloc_start &&
               initialized_pool_start == &allocation_storage.bytes[0] &&
               initialized_pool_size == (s32)0x6DDD0,
           "texture allocation initializes the global texture pool");

    g_TexCacheCount = 9;
    bytes = 0x6DDD0;
    mt_option = "96";
    token_calls = 0;
    reset_allocator();

    set_mt_tex_alloc();

    expect(g_TexCacheCount == 0 && bytes == 96u * 1024u &&
               allocation_sizes[0] == 96u * 1024u,
           "texture allocation converts a valid -mt value from KiB to bytes");
    expect(token_calls == 1,
           "texture allocation reads the command-line option once");
}

static void test_texture_pool_rejects_invalid_override(void)
{
    bytes = 0x24000;
    mt_option = "-1";
    token_calls = 0;
    reset_allocator();

    set_mt_tex_alloc();

    expect(bytes == 0x24000 && allocation_sizes[0] == 0x24000,
           "texture allocation ignores a negative -mt size instead of wrapping");

    bytes = 0x24000;
    mt_option = "not-a-number";
    reset_allocator();

    set_mt_tex_alloc();

    expect(bytes == 0x24000 && allocation_sizes[0] == 0x24000,
           "texture allocation ignores a malformed -mt size");
}

static void test_intro_matrix_allocations(void)
{
    static const u32 expected_sizes[] = {
        0x80, 0x40, 0x80, 0x80, 0x80, 0x80, 0x80
    };
    size_t i;

    D_8002A7D0 = 7;
    matrixBufferRareLogo0 = (Mtx *)(uintptr_t)0x1;
    matrixBufferGunbarrel0 = (Mtx *)(uintptr_t)0x1;
    matrixBufferRareLogo1 = (Mtx *)(uintptr_t)0x1;
    matrixBufferRareLogo2 = (Mtx *)(uintptr_t)0x1;
    matrixBufferGunbarrel1 = (Mtx *)(uintptr_t)0x1;
    matrixBufferIntroBackdrop = (Mtx *)(uintptr_t)0x1;
    matrixBufferIntroBond = (Mtx *)(uintptr_t)0x1;
    reset_allocator();

    alloc_intro_matrices();

    expect(D_8002A7D0 == 0 && allocation_count == 7,
           "intro matrix allocation resets the active matrix index and allocates all buffers");
    for (i = 0; i < sizeof(expected_sizes) / sizeof(expected_sizes[0]); ++i)
    {
        expect(allocation_sizes[i] == expected_sizes[i] &&
                   allocation_banks[i] == MEMPOOL_STAGE,
               "intro matrix allocation uses the expected aligned stage-pool size");
    }
    expect(matrixBufferRareLogo0 == (Mtx *)&allocation_storage.bytes[0] &&
               matrixBufferGunbarrel0 == (Mtx *)&allocation_storage.bytes[0x80] &&
               matrixBufferRareLogo1 == (Mtx *)&allocation_storage.bytes[0xC0] &&
               matrixBufferRareLogo2 == (Mtx *)&allocation_storage.bytes[0x140] &&
               matrixBufferGunbarrel1 == (Mtx *)&allocation_storage.bytes[0x1C0] &&
               matrixBufferIntroBackdrop == (Mtx *)&allocation_storage.bytes[0x240] &&
               matrixBufferIntroBond == (Mtx *)&allocation_storage.bytes[0x2C0],
           "intro matrix allocation assigns each result to its matching buffer");
}

int main(void)
{
    test_texture_pool_default_and_override();
    test_texture_pool_rejects_invalid_override();
    test_intro_matrix_allocations();

    if (failures != 0)
    {
        fprintf(stderr, "%d initialization allocation contract(s) failed\n", failures);
        return 1;
    }

    puts("initialization-allocation suite passed");
    return 0;
}
