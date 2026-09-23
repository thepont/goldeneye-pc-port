#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "image.h"

s32 texAlignIndices(u8 *src, s32 width, s32 height, s32 format, u8 *dst);
s32 texInflateLookupFromBuffer(u8 *src, s32 width, s32 height, u8 *dst,
                               u8 *lookup, s32 numcolours, s32 format);
s32 texReadUncompressed(u8 *dst, s32 width, s32 height, s32 format);
s32 texGetBitSize(s32 decimal);
void texSetBitstring(uintptr_t position);
u32 texReadBits(s32 bit_count);

static int failures;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static uintptr_t align_up_8(uintptr_t address)
{
    return (address + 7u) & ~(uintptr_t)7u;
}

static void test_pool_alignment_and_lookup_boundaries(void)
{
    u8 storage[128];
    struct texpool pool;
    struct tex entries[3];
    uintptr_t expected_start;

    memset(storage, 0, sizeof(storage));
    memset(entries, 0, sizeof(entries));
    texInitPool(&pool, storage + 1, (s32)(sizeof(storage) - 1));
    expected_start = align_up_8((uintptr_t)(storage + 1));

    expect_true((uintptr_t)pool.start == expected_start &&
                    (uintptr_t)pool.leftpos == expected_start,
                "texture pools align the base before row-padded allocations");

    pool.rightpos = (struct tex *)(storage + 32);
    expect_true(texFreeBytesInBuffer(&pool) ==
                    (s32)((u8 *)pool.rightpos - pool.leftpos),
                "texture pool free-byte accounting follows the host pointers");

    entries[0].texturenum = 11;
    entries[1].texturenum = 23;
    entries[2].texturenum = 37;
    pool.rightpos = &entries[0];
    pool.end = &entries[3];
    expect_true(texFindInPool(23, &pool) == &entries[1] &&
                    texFindInPool(99, &pool) == NULL,
                "texture pool lookup scans the active half-open entry range");
}

static void test_index_rows_align_without_narrowing_host_addresses(void)
{
    u8 source[] = {1, 2, 3, 4, 5, 6};
    u8 storage[32];
    u8 *destination = storage + 1;
    uintptr_t first_row;
    uintptr_t expected_end;
    s32 written;

    memset(storage, 0xcc, sizeof(storage));
    first_row = align_up_8((uintptr_t)(destination + 3));
    expected_end = align_up_8(first_row + 3);
    written = texAlignIndices(source, 3, 2, TEXFORMAT_RGBA16_CI8,
                               destination);

    expect_true(written == (s32)(expected_end - (uintptr_t)destination),
                "index-row padding uses the complete host destination address");
    expect_true(destination[0] == 1 && destination[1] == 2 &&
                    destination[2] == 3 &&
                    ((u8 *)first_row)[0] == 4 &&
                    ((u8 *)first_row)[1] == 5 &&
                    ((u8 *)first_row)[2] == 6,
                "index-row alignment copies every row before padding");
}

static void test_odd_width_ci4_rows_do_not_consume_the_next_index(void)
{
    u8 source[] = {1, 2, 3, 0xee};
    u16 lookup[256];
    u8 destination[8];
    s32 i;

    for (i = 0; i < 256; ++i)
    {
        lookup[i] = (u16)(i & 0x0f);
    }
    memset(destination, 0, sizeof(destination));

    expect_true(texInflateLookupFromBuffer(source, 3, 1, destination,
                                           (u8 *)lookup, 256,
                                           TEXFORMAT_I4) == 8,
                "odd-width I4 rows retain their eight-byte row alignment");
    expect_true(destination[0] == 0x12 && destination[1] == 0x30,
                "odd-width I4 rows leave the unused low nibble clear");
}

static void test_bitstream_source_preserves_host_pointer_width(void)
{
    u8 bitstream[] = {0xa5};

    texSetBitstring((uintptr_t)bitstream);
    expect_true(texReadBits(8) == 0xa5,
                "texture bitstream setup preserves the full host source address");
}

static void test_uncompressed_destination_alignment_preserves_host_pointer_width(void)
{
    u8 bitstream[] = {0x5a};
    u8 storage[32];
    u8 *destination = storage + 1;
    uintptr_t expected_destination =
        (uintptr_t)(destination + 7) & ~(uintptr_t)7;

    memset(storage, 0, sizeof(storage));
    texSetBitstring((uintptr_t)bitstream);
    expect_true(texReadUncompressed(destination, 1, 1, TEXFORMAT_I8) == 8,
                "uncompressed I8 output keeps the N64 row-size contract");
    expect_true(*(u8 *)expected_destination == 0x5a,
                "uncompressed I8 output writes to the aligned host destination");
}

static void test_palette_bit_width_and_empty_inputs_are_bounded(void)
{
    u16 rgba_palette[] = {0x0001, 0x7c01, 0xffff};
    u16 ia_palette[] = {0x0000, 0x8080, 0xffff};

    expect_true(texGetBitSize(1) == 0 && texGetBitSize(2) == 1 &&
                    texGetBitSize(3) == 2 && texGetBitSize(4) == 2 &&
                    texGetBitSize(256) == 8,
                "palette index bit widths follow ceil(log2(colour count))");
    expect_true(texFindClosestColourIndexRGBA(rgba_palette, 3, 31, 0, 0, 1) == 1 &&
                    texFindClosestColourIndexRGBA(rgba_palette, 0, 0, 0, 0, 0) == 0,
                "RGBA palette lookup handles exact and empty palettes");
    expect_true(texFindClosestColourIndexIA(ia_palette, 3, 128, 128) == 1 &&
                    texFindClosestColourIndexIA(ia_palette, 0, 0, 0) == 0,
                "IA palette lookup handles exact and empty palettes");
}

int main(void)
{
    test_pool_alignment_and_lookup_boundaries();
    test_index_rows_align_without_narrowing_host_addresses();
    test_odd_width_ci4_rows_do_not_consume_the_next_index();
    test_bitstream_source_preserves_host_pointer_width();
    test_uncompressed_destination_alignment_preserves_host_pointer_width();
    test_palette_bit_width_and_empty_inputs_are_bounded();

    if (failures != 0)
    {
        fprintf(stderr, "%d image-texture contract(s) failed\n", failures);
        return 1;
    }

    puts("image texture suite passed");
    return 0;
}
