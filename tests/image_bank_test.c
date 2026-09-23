#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/mman.h>

#include <ultra64.h>
#include "image.h"
#include "image_bank.h"
#include "gimgfixup.h"
#include "memp.h"

/* Keep the segment markers close together and deterministic.  texReset()
 * intentionally uses the linked marker distance, just as the game does. */
__asm__(
    ".section .ge007_image_bank,\"aw\",@nobits\n"
    ".p2align 12\n"
    ".global _GlobalimagetableSegmentStart\n"
    "_GlobalimagetableSegmentStart:\n"
    ".zero 0x100\n"
    ".global _GlobalimagetableSegmentEnd\n"
    "_GlobalimagetableSegmentEnd:\n"
);

unsigned char _GlobalimagetableSegmentRomStart[0x100];

extern unsigned char _GlobalimagetableSegmentStart[];
extern unsigned char _GlobalimagetableSegmentEnd[];

enum event_kind
{
    EVENT_ALLOC = 1,
    EVENT_ROM_COPY,
    EVENT_FIXUP,
    EVENT_DISPLAY_LIST,
    EVENT_TEXTURE,
    EVENT_SYNC
};

static int failures;
static int events[64];
static int event_count;
static void *mapped_storage;
static size_t mapped_storage_size;
static u32 allocated_bytes;
static u8 allocated_bank;
static void *allocated_pointer;
static void *rom_copy_target;
static void *rom_copy_source;
static u32 rom_copy_size;
static u8 *fixup_base;
static u8 *sync_base;
static uintptr_t display_list_arguments[17];
static int display_list_calls;
static uintptr_t texture_arguments[12];
static struct texpool *texture_pools[12];
static int texture_calls;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void record_event(int event)
{
    if (event_count < (int)(sizeof(events) / sizeof(events[0])))
    {
        events[event_count++] = event;
    }
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    allocated_bytes = bytes;
    allocated_bank = bank;
    allocated_pointer = (u8 *)mapped_storage + 0x123;
    record_event(EVENT_ALLOC);
    return allocated_pointer;
}

void romCopy(void *target, void *source, u32 size)
{
    rom_copy_target = target;
    rom_copy_source = source;
    rom_copy_size = size;
    memcpy(target, source, size);
    record_event(EVENT_ROM_COPY);
}

void gimgFixupGlobalimagetable(u8 *base)
{
    fixup_base = base;
    record_event(EVENT_FIXUP);
}

void gimgSyncCompiledGlobalDLs(u8 *base)
{
    sync_base = base;
    record_event(EVENT_SYNC);
}

void texLoadFromDisplayList(Gfx *gdl, struct texpool *pool)
{
    if (display_list_calls <
        (int)(sizeof(display_list_arguments) / sizeof(display_list_arguments[0])))
    {
        display_list_arguments[display_list_calls] = (uintptr_t)gdl;
    }
    expect_true(pool == NULL,
                "global image display-list loads use the default texture pool");
    display_list_calls++;
    record_event(EVENT_DISPLAY_LIST);
}

void texLoad(s32 *updateword, struct texpool *pool)
{
    if (texture_calls < (int)(sizeof(texture_arguments) /
                             sizeof(texture_arguments[0])))
    {
        texture_arguments[texture_calls] = (uintptr_t)updateword;
        texture_pools[texture_calls] = pool;
    }
    texture_calls++;
    record_event(EVENT_TEXTURE);
}

static void reset_fakes(void)
{
    memset(events, 0, sizeof(events));
    event_count = 0;
    allocated_bytes = 0;
    allocated_bank = 0;
    allocated_pointer = NULL;
    rom_copy_target = NULL;
    rom_copy_source = NULL;
    rom_copy_size = 0;
    fixup_base = NULL;
    sync_base = NULL;
    memset(display_list_arguments, 0, sizeof(display_list_arguments));
    display_list_calls = 0;
    memset(texture_arguments, 0, sizeof(texture_arguments));
    memset(texture_pools, 0, sizeof(texture_pools));
    texture_calls = 0;
}

static void test_bitstream_refill_and_reset(void)
{
    u8 bitstream[] = {0xd2, 0x6b, 0x5c, 0xa1};
    u8 replacement[] = {0xf0};

    texSetBitstring((uintptr_t)bitstream);
    expect_true(texReadBits(0) == 0,
                "zero-width texture reads return zero without consuming input");
    expect_true(texReadBits(4) == 0xd,
                "texture bit reads return the high nibble first");
    expect_true(texReadBits(8) == 0x26,
                "texture bit reads span a byte refill at the current cursor");
    expect_true(texReadBits(12) == 0xb5c,
                "texture bit reads handle a multi-byte refill and non-byte width");
    expect_true(texReadBits(4) == 0xa,
                "texture bit reads retain the low bits after a multi-byte read");

    texSetBitstring((uintptr_t)replacement);
    expect_true(texReadBits(8) == 0xf0 && img_bitcount == 0,
                "setting a new texture bitstream resets both cursor state values");
}

static void test_tex_reset_rebases_and_initializes_global_image_bank(void)
{
    static const u32 display_list_offsets[] = {
        0x000, 0x078, 0x120, 0x1c8, 0x270, 0x318, 0x3c0, 0x468,
        0x510, 0x5b8, 0x660, 0x708, 0x7b0, 0x858, 0x900, 0x9a8,
        0xa50
    };
    uintptr_t segment_start = (uintptr_t)_GlobalimagetableSegmentStart;
    uintptr_t segment_end = (uintptr_t)_GlobalimagetableSegmentEnd;
    uintptr_t segment_size = segment_end - segment_start;
    uintptr_t base;
    int i;

    for (i = 0; i < (int)sizeof(_GlobalimagetableSegmentRomStart); ++i)
    {
        _GlobalimagetableSegmentRomStart[i] = (unsigned char)(0x31 + i);
    }

    reset_fakes();
    texReset();

    base = (uintptr_t)pGlobalimagetable;
    expect_true(segment_size == 0x100,
                "the test global-image segment has a non-empty linked size");
    expect_true(allocated_bytes == (u32)(segment_size + 0x1000) &&
                    allocated_bank == MEMPOOL_STAGE,
                "global-image reset allocates the segment plus alignment slack");
    expect_true(allocated_pointer != NULL && (base & 0xfffU) == 0,
                "global-image reset rounds the allocation up to a page boundary");
    expect_true(rom_copy_target == (void *)base &&
                    rom_copy_source == (void *)_GlobalimagetableSegmentRomStart &&
                    rom_copy_size == (u32)segment_size,
                "global-image reset copies exactly the linked ROM segment");
    expect_true(memcmp((void *)base, _GlobalimagetableSegmentRomStart,
                       segment_size) == 0,
                "global-image reset preserves the copied ROM bytes");
    expect_true(fixup_base == (u8 *)base && sync_base == (u8 *)base,
                "global-image reset fixes and synchronizes the same rebased bank");
    expect_true(display_list_calls == 17,
                "global-image reset visits every compiled global display list");
    expect_true(texture_calls == 12,
                "global-image reset loads generic, explosion, and scattered images");
    expect_true(event_count == 1 + 1 + 1 + 17 + 12 + 1,
                "global-image reset performs one complete initialization sequence");
    expect_true(events[0] == EVENT_ALLOC && events[1] == EVENT_ROM_COPY &&
                    events[2] == EVENT_FIXUP &&
                    events[3] == EVENT_DISPLAY_LIST &&
                    events[20] == EVENT_TEXTURE && events[32] == EVENT_SYNC,
                "global-image reset orders conversion, display lists, textures, and sync");

    for (i = 0; i < (int)(sizeof(display_list_offsets) /
                          sizeof(display_list_offsets[0])); ++i)
    {
        expect_true(display_list_arguments[i] == base + display_list_offsets[i],
                    "global display-list pointers are rebased to the copied segment");
    }

    expect_true((uintptr_t)genericimage == base + 0xac8 &&
                    (uintptr_t)explosion_smokeimages == base + 0xbc4 &&
                    (uintptr_t)scattered_explosions == base + 0xc0c,
                "global image tables are published at their segment offsets");
    expect_true(texture_arguments[0] == base + 0xac8,
                "the generic image table is loaded from its rebased address");
    for (i = 0; i < 6; ++i)
    {
        expect_true(texture_arguments[1 + i] ==
                        base + 0xbc4 + (uintptr_t)i * sizeof(sImageTableEntry),
                    "explosion image entries are loaded in table order");
        expect_true(texture_pools[1 + i] == NULL,
                    "explosion image loads use the default texture pool");
    }
    for (i = 0; i < 5; ++i)
    {
        expect_true(texture_arguments[7 + i] ==
                        base + 0xc0c + (uintptr_t)i * sizeof(sImageTableEntry),
                    "scattered explosion entries are loaded in table order");
    }
    expect_true((u32)globalbank_rdram_offset == (u32)base + 0xfe000000U,
                "global image rebasing preserves the N64 segment offset equation");
}

int main(void)
{
#if !defined(MAP_32BIT)
    fprintf(stderr, "image-bank suite requires a low-address mmap implementation\n");
    return 1;
#else
    mapped_storage_size = 0x8000;
    mapped_storage = mmap(NULL, mapped_storage_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (mapped_storage == MAP_FAILED || (uintptr_t)mapped_storage > UINT32_MAX)
    {
        fprintf(stderr, "image-bank suite could not allocate a low test bank\n");
        return 1;
    }

    test_bitstream_refill_and_reset();
    test_tex_reset_rebases_and_initializes_global_image_bank();

    munmap(mapped_storage, mapped_storage_size);

    if (failures != 0)
    {
        fprintf(stderr, "%d image-bank contract(s) failed\n", failures);
        return 1;
    }

    puts("image bank suite passed");
    return 0;
#endif
}
