#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/mman.h>

#include <ultra64.h>

#include "initanitable.h"
#include "memp.h"
#include "objecthandler.h"

/* The real ROM linker supplies these adjacent segment markers.  The test
 * supplies the same observable boundary with a deterministic 0x40-byte
 * segment, without needing a ROM or the asset linker script. */
__asm__(
    ".section .ge007_animation_segment,\"a\",@nobits\n"
    ".p2align 12\n"
    ".global _animation_dataSegmentStart\n"
    "_animation_dataSegmentStart:\n"
    ".zero 0x40\n"
    ".global _animation_dataSegmentEnd\n"
    "_animation_dataSegmentEnd:\n"
);

unsigned char _animation_dataSegmentRomStart[0x40];
unsigned char _animation_entriesSegmentRomStart[4];
char animations_frame_buffer[0x2d0];

extern unsigned char _animation_dataSegmentStart[];
extern unsigned char _animation_dataSegmentEnd[];

void expand_ani_table_entries(s32 **arg0);
void alloc_load_expand_ani_table(void);

struct anim_entry_fixture
{
    s32 unk00;
    s32 unk04;
    s32 unk08;
    s32 unk0c;
    s32 unk10;
};

static void *mapped_animation;
static size_t mapped_animation_size;
static int failures;
static int create_queue_calls;
static OSMesgQueue *created_queue;
static OSMesg *created_messages;
static s32 created_message_count;
static int init_buffer_calls;
static struct bondstruct_unk_animation_related *initialized_buffer;
static OSMesgQueue *initialized_queue;
static int allocation_calls;
static u32 allocated_size;
static u8 allocated_bank;
static int rom_copy_calls;
static void *rom_copy_destination;
static void *rom_copy_source;
static u32 rom_copy_size;
static int fixup_calls;
static u8 *fixup_blob;
static u32 fixup_size;
static const s32 *fixup_table_a;
static const s32 *fixup_table_b;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void reset_fakes(void)
{
    create_queue_calls = 0;
    created_queue = NULL;
    created_messages = NULL;
    created_message_count = 0;
    init_buffer_calls = 0;
    initialized_buffer = NULL;
    initialized_queue = NULL;
    allocation_calls = 0;
    allocated_size = 0;
    allocated_bank = 0;
    rom_copy_calls = 0;
    rom_copy_destination = NULL;
    rom_copy_source = NULL;
    rom_copy_size = 0;
    fixup_calls = 0;
    fixup_blob = NULL;
    fixup_size = 0;
    fixup_table_a = NULL;
    fixup_table_b = NULL;
}

void osCreateMesgQueue(OSMesgQueue *queue, OSMesg *messages, s32 count)
{
    create_queue_calls++;
    created_queue = queue;
    created_messages = messages;
    created_message_count = count;
}

void initAnimationsBuffer(struct bondstruct_unk_animation_related *buffer,
                          OSMesgQueue *queue, s32 unused)
{
    (void)unused;
    init_buffer_calls++;
    initialized_buffer = buffer;
    initialized_queue = queue;
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    allocation_calls++;
    allocated_size = bytes;
    allocated_bank = bank;
    return mapped_animation;
}

void romCopy(void *destination, void *source, u32 size)
{
    rom_copy_calls++;
    rom_copy_destination = destination;
    rom_copy_source = source;
    rom_copy_size = size;
    memcpy(destination, source, size);
}

void romdataFixupAnimationData(u8 *blob, u32 size, const s32 *table_a,
                               const s32 *table_b)
{
    s32 *mutable_a = (s32 *)table_a;
    s32 *mutable_b = (s32 *)table_b;

    fixup_calls++;
    fixup_blob = blob;
    fixup_size = size;
    fixup_table_a = table_a;
    fixup_table_b = table_b;

    /* Keep this orchestration test independent of the full asset table. */
    mutable_a[0] = 1;
    mutable_a[1] = 0;
    mutable_b[0] = 1;
    mutable_b[1] = 0;
}

static void test_expand_rebases_records_and_honours_sentinel(void)
{
    s32 table[] = {0x100, 1, 0};
    struct anim_entry_fixture *record;
    s32 base;
    s32 entries_rom;

    memset(mapped_animation, 0, mapped_animation_size);
    ptr_animation_table = (struct animation_table_data *)mapped_animation;
    record = (struct anim_entry_fixture *)((u8 *)mapped_animation + 0x100);
    record->unk00 = 0x20;
    record->unk08 = 7;
    record->unk10 = 9;
    base = (s32)(uintptr_t)&ptr_animation_table->data;
    entries_rom = (s32)(uintptr_t)_animation_entriesSegmentRomStart;

    expand_ani_table_entries((s32 **)(void *)table);

    expect_true(table[0] == base + 0x100,
                "animation expansion converts a table offset to a data pointer");
    expect_true(table[1] == 1 && table[2] == 0,
                "animation expansion stops at the one-valued sentinel");
    expect_true(record->unk08 == 7 + base && record->unk10 == 9 + base,
                "animation expansion rebases embedded record pointers");
    expect_true(record->unk00 == 0x20 + entries_rom,
                "animation expansion rebases the record entry segment pointer");
}

static void test_alloc_load_orchestrates_queue_copy_fixup_and_expansion(void)
{
    int i;

    for (i = 0; i < (int)sizeof(_animation_dataSegmentRomStart); ++i)
    {
        _animation_dataSegmentRomStart[i] = (unsigned char)(0x80 + i);
    }
    memset(mapped_animation, 0, mapped_animation_size);
    reset_fakes();

    alloc_load_expand_ani_table();

    expect_true(create_queue_calls == 1 && created_queue != NULL &&
                    created_messages != NULL && created_message_count == 8,
                "animation loading creates the fixed animation message queue");
    expect_true(init_buffer_calls == 1 && initialized_buffer != NULL &&
                    initialized_queue == created_queue,
                "animation loading initializes the animation frame buffer port");
    expect_true(allocation_calls == 1 &&
                    allocated_size == (u32)((uintptr_t)_animation_dataSegmentEnd -
                                            (uintptr_t)_animation_dataSegmentStart) &&
                    allocated_bank == MEMPOOL_PERMANENT,
                "animation loading allocates exactly the linked data segment");
    expect_true(ptr_animation_table == (struct animation_table_data *)mapped_animation,
                "animation loading publishes the allocated animation table");
    expect_true(rom_copy_calls == 1 &&
                    rom_copy_destination == mapped_animation &&
                    rom_copy_source == (void *)_animation_dataSegmentRomStart &&
                    rom_copy_size == allocated_size,
                "animation loading copies the complete ROM animation segment");
    expect_true(memcmp(mapped_animation, _animation_dataSegmentRomStart,
                       allocated_size) == 0,
                "animation loading preserves the ROM segment before fixup");
    expect_true(fixup_calls == 1 && fixup_blob == mapped_animation &&
                    fixup_size == allocated_size &&
                    fixup_table_a == animation_table_ptrs1 &&
                    fixup_table_b == animation_table_ptrs2,
                "animation loading fixes the copied data before offset expansion");
    expect_true(animation_table_ptrs1[0] == 1 && animation_table_ptrs1[1] == 0 &&
                    animation_table_ptrs2[0] == 1 && animation_table_ptrs2[1] == 0,
                "animation loading leaves the fixup-provided sentinels intact");
}

int main(void)
{
#if !defined(MAP_32BIT)
    fprintf(stderr, "animation-table suite requires a low-address mmap implementation\n");
    return 1;
#else
    mapped_animation_size = 0x20000;
    mapped_animation = mmap(NULL, mapped_animation_size,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (mapped_animation == MAP_FAILED ||
        (uintptr_t)mapped_animation > UINT32_MAX)
    {
        fprintf(stderr, "animation-table suite could not allocate a low table\n");
        return 1;
    }

    test_expand_rebases_records_and_honours_sentinel();
    test_alloc_load_orchestrates_queue_copy_fixup_and_expansion();

    munmap(mapped_animation, mapped_animation_size);

    if (failures != 0)
    {
        fprintf(stderr, "%d animation-table contract(s) failed\n", failures);
        return 1;
    }

    puts("animation table suite passed");
    return 0;
#endif
}
