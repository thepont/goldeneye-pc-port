#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>

#include "ob.h"
#include "memp.h"
#include "assets/obseg/file_resource_id_enums.h"

extern fileentry file_resource_table[];
extern resource_lookup_data_entry resource_lookup_data_array[];
extern s32 file_entry_max;

void obLoadBGFileBytesAtOffset(u8 *bgname, u8 *target, s32 offset, s32 len);
void *_fileIndexLoadToBank(s32 index, FILELOADMETHOD method, s32 size, u8 bank);
void *_fileIndexLoadToAddr(s32 index, FILELOADMETHOD method, u8 *target, int size);
void removed_loop_handle_filetable_entries(void);
void removed_loop_filetableentries(void);
void sub_GAME_7F0BD410(void);

static int failures;
static int fake_file_exists;
static s32 fake_file_size;
static int fake_check_calls;
static int fake_load_calls;
static int fake_rom_copy_calls;
static int fake_decompress_calls;
static u8 fake_file_payload[128];
static s32 fake_file_payload_size;
static u8 fake_decoded_payload[128];
static s32 fake_decoded_payload_size;
static u8 fake_bank_storage[256];
static s32 fake_bank_capacity;
static s32 fake_allocated_bytes;
static u8 fake_allocated_bank;
static int fake_reallocate_calls;
static s32 fake_reallocated_size;
static u8 fake_reallocated_bank;
static int fake_patch_model_calls;
static int fake_patch_bg_calls;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_int(s32 actual, s32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (expected %d, got %d)\n",
                message, expected, actual);
        failures++;
    }
}

static void expect_bytes(const u8 *actual, const u8 *expected, size_t size,
                         const char *message)
{
    if (memcmp(actual, expected, size) != 0)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

u8 *indycommHostCheckFileExists(char *name, s32 *size)
{
    (void)name;
    fake_check_calls++;
    if (size != NULL)
    {
        *size = fake_file_size;
    }
    return fake_file_exists ? (u8 *)1 : NULL;
}

void debTryAdd(void *data, const char *name)
{
    (void)data;
    (void)name;
}

void indycommHostLoadFile(char *filename, u8 *targetloc)
{
    (void)filename;
    fake_load_calls++;
    memcpy(targetloc, fake_file_payload, (size_t)fake_file_payload_size);
}

void romCopy(void *target, void *source, u32 size)
{
    fake_rom_copy_calls++;
    memcpy(target, source, (size_t)size);
}

u32 decompressdata(u8 *source, u8 *target, struct huft *huffman_table)
{
    (void)source;
    (void)huffman_table;
    fake_decompress_calls++;
    memcpy(target, fake_decoded_payload, (size_t)fake_decoded_payload_size);
    return (u32)fake_decoded_payload_size;
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    fake_allocated_bytes = (s32)bytes;
    fake_allocated_bank = bank;
    return fake_bank_storage;
}

MEMP_ADD_ENTRY_RESULT mempAddEntryOfSizeToBank(void *allocation,
                                                s32 newsize,
                                                u8 poolnum)
{
    (void)allocation;
    fake_reallocate_calls++;
    fake_reallocated_size = newsize;
    fake_reallocated_bank = poolnum;
    return MEMP_ADD_ENTRY_SUCCESS;
}

s32 mempGetBankSizeLeft(u8 bank)
{
    (void)bank;
    return fake_bank_capacity;
}

void pcmodelsPatchTable(void)
{
    fake_patch_model_calls++;
}

void pccgPatchTable(void)
{
    fake_patch_bg_calls++;
}

static void test_missing_file_does_not_consume_table_slot(void)
{
    s32 original_max = file_entry_max;

    fake_file_exists = 0;
    fake_file_size = 0;
    fake_check_calls = 0;

    expect_int(fileGetIndex("missing/test.seg"), 0,
               "missing resource returns the null index");
    expect_int(file_entry_max, original_max,
               "missing resource does not consume a dynamic table slot");
    expect_int(fake_check_calls, 1,
               "missing resource is checked exactly once");
}

static s32 register_fake_resource(const char *name, s32 size)
{
    file_entry_max = OBJ_INDEX_END;
    fake_file_exists = 1;
    fake_file_size = size;
    fake_check_calls = 0;

    return fileGetIndex((char *)name);
}

static void test_resource_lookup_registration(void)
{
    const char *name = "dynamic/brief.seg";
    s32 index = register_fake_resource(name, 17);

    expect_int(index, OBJ_INDEX_END, "present resource gets the first dynamic index");
    expect_int(file_entry_max, OBJ_INDEX_END + 1,
               "present resource advances the table high-water mark");
    expect_true(file_resource_table[index].filename == name,
                "dynamic entry retains the requested resource name");
    expect_true(file_resource_table[index].hw_address == NULL,
                "dynamic entry uses the Indy loader marker");
    expect_int((s32)resource_lookup_data_array[index].pc_size, 32,
               "dynamic resource size is aligned to sixteen bytes");
    expect_int(fake_check_calls, 1,
               "present resource is checked once during registration");

    expect_int(fileGetIndex((char *)name), index,
               "repeated lookup returns the existing dynamic index");
    expect_int(fake_check_calls, 1,
               "repeated lookup does not probe the host filesystem again");
}

static void test_resource_lookup_rejects_null_and_full_table(void)
{
    s32 original_max = file_entry_max;

    fake_file_exists = 1;
    fake_file_size = 8;
    fake_check_calls = 0;
    expect_int(fileGetIndex(NULL), 0, "null resource names return the null index");
    expect_int(file_entry_max, original_max,
               "null resource names do not mutate the table");
    expect_int(fake_check_calls, 0,
               "null resource names do not call the host filesystem");

    file_entry_max = OBJ_INDEX_MAX;
    expect_int(fileGetIndex("dynamic/full.seg"), 0,
               "a full resource table returns the null index");
    expect_int(fake_check_calls, 0,
               "a full table does not probe for another resource");
    file_entry_max = original_max;
}

static void test_indy_resource_loading(void)
{
    fileentry source = {0, "dynamic/test.seg", NULL};
    resource_lookup_data_entry lookup = {0};
    u8 target[64] = {0};
    const u8 plain[] = {1, 2, 3, 4, 5, 6, 7, 8};
    const u8 decoded[] = {'D', 'E', 'C', 'O', 'D', 'E', 'D'};

    memcpy(fake_file_payload, plain, sizeof(plain));
    fake_file_payload_size = sizeof(plain);
    fake_file_size = sizeof(plain);
    fake_file_exists = 1;
    fake_load_calls = 0;
    fake_check_calls = 0;
    lookup.pc_size = sizeof(plain);
    resource_load_from_indy(target, 0, &source, &lookup);
    expect_int(fake_check_calls, 0,
               "direct Indy loads do not perform a redundant existence probe");
    expect_int(fake_load_calls, 1, "direct Indy loads read the resource once");
    expect_bytes(target, plain, sizeof(plain),
                 "direct Indy loads copy the complete uncompressed resource");

    memset(target, 0, sizeof(target));
    lookup.pc_size = 16;
    lookup.poolRemaining = 99;
    fake_file_size = 16;
    fake_file_payload_size = 16;
    fake_load_calls = 0;
    resource_load_from_indy(target, 23, &source, &lookup);
    expect_int(fake_load_calls, 0,
               "an Indy load with less than eight bytes of tail space is rejected");
    expect_int((s32)lookup.poolRemaining, 0,
               "an undersized Indy destination reports no remaining payload");

    memset(target, 0, sizeof(target));
    lookup.pc_size = 8;
    lookup.poolRemaining = 0;
    fake_file_size = 8;
    fake_file_payload_size = sizeof(plain);
    fake_decompress_calls = 0;
    resource_load_from_indy(target, 32, &source, &lookup);
    expect_int(fake_decompress_calls, 0,
               "plain Indy payloads bypass decompression");
    expect_int((s32)lookup.poolRemaining, 8,
               "plain Indy payloads report their copied size");
    expect_bytes(target, plain, sizeof(plain),
                 "plain Indy payloads are copied to the requested destination");

    memset(target, 0, sizeof(target));
    fake_file_payload[0] = 0x11;
    fake_file_payload[1] = 0x72;
    fake_file_payload_size = 4;
    fake_file_size = 4;
    memcpy(fake_decoded_payload, decoded, sizeof(decoded));
    fake_decoded_payload_size = sizeof(decoded);
    fake_decompress_calls = 0;
    lookup.pc_size = 4;
    resource_load_from_indy(target, 32, &source, &lookup);
    expect_int(fake_decompress_calls, 1,
               "RZIP-tagged Indy payloads use the decompressor");
    expect_int((s32)lookup.poolRemaining, (s32)sizeof(decoded),
               "decompressed Indy payloads report produced bytes");
    expect_bytes(target, decoded, sizeof(decoded),
                 "decompressed Indy payloads are written to the destination");
}

static void test_rom_resource_loading(void)
{
    u8 rom_source[16];
    u8 target[64] = {0};
    fileentry source = {0, "rom/test.seg", rom_source};
    resource_lookup_data_entry lookup = {0};
    const u8 decoded[] = {'R', 'O', 'M', 'D', 'A', 'T', 'A'};

    for (u8 i = 0; i < sizeof(rom_source); i++)
    {
        rom_source[i] = (u8)(0xA0 + i);
    }
    lookup.rom_size = sizeof(rom_source);
    fake_rom_copy_calls = 0;
    load_resource(target, 0, &source, &lookup);
    expect_int(fake_rom_copy_calls, 1, "direct ROM loads perform one ROM copy");
    expect_bytes(target, rom_source, sizeof(rom_source),
                 "direct ROM loads copy the source bytes");

    memset(target, 0, sizeof(target));
    lookup.poolRemaining = 0;
    memcpy(fake_decoded_payload, decoded, sizeof(decoded));
    fake_decoded_payload_size = sizeof(decoded);
    fake_decompress_calls = 0;
    load_resource(target, 32, &source, &lookup);
    expect_int(fake_decompress_calls, 1,
               "sufficient ROM tail space invokes decompression");
    expect_int((s32)lookup.poolRemaining, (s32)sizeof(decoded),
               "decompressed ROM payloads report produced bytes");
    expect_bytes(target, decoded, sizeof(decoded),
                 "decompressed ROM payloads are written to the destination");

    memset(target, 0, sizeof(target));
    lookup.poolRemaining = 99;
    fake_rom_copy_calls = 0;
    fake_decompress_calls = 0;
    load_resource(target, 23, &source, &lookup);
    expect_int(fake_rom_copy_calls, 0,
               "ROM loads with less than eight bytes of tail space are rejected");
    expect_int(fake_decompress_calls, 0,
               "rejected ROM loads do not invoke decompression");
    expect_int((s32)lookup.poolRemaining, 0,
               "rejected ROM loads report no remaining payload");
}

static void test_index_loading_paths(void)
{
    const u8 plain[] = {'I', 'N', 'D', 'Y', '-', 'D', 'A', 'T'};
    u8 target[64] = {0};
    u8 rom_source[16];
    fileentry saved_rom_entry = file_resource_table[1];
    s32 index = register_fake_resource("dynamic/address.seg", sizeof(plain));

    memcpy(fake_file_payload, plain, sizeof(plain));
    fake_file_payload_size = sizeof(plain);
    fake_file_size = sizeof(plain);
    fake_load_calls = 0;
    resource_lookup_data_array[index].poolRemaining = 0;
    expect_true(fileIndexLoadToAddr(index, FILELOADMETHOD_DEFAULT, target, 32) == target,
                "address loads return their destination pointer");
    expect_int(fake_load_calls, 1, "default address loads read an Indy resource");
    expect_bytes(target, plain, sizeof(plain),
                 "default address loads copy an uncompressed Indy resource");
    expect_int((s32)resource_lookup_data_array[index].rom_remaining, 32,
               "address loads retain the supplied buffer size");
    expect_int((s32)resource_lookup_data_array[index].poolRemaining,
               (s32)sizeof(plain),
               "address loads retain the produced resource size");

    memset(target, 0, sizeof(target));
    resource_lookup_data_array[index].poolRemaining = 0;
    resource_lookup_data_array[index].rom_remaining = 0;
    fake_load_calls = 0;
    fileIndexLoadToAddr(index, FILELOADMETHOD_UNK2, target, 0);
    expect_int(fake_load_calls, 1,
               "direct address loads use the zero-byte Indy path");
    expect_bytes(target, plain, sizeof(plain),
                 "direct address loads copy the complete resource");
    expect_int((s32)resource_lookup_data_array[index].rom_remaining, 0,
               "direct address loads do not invent a compressed buffer size");

    resource_lookup_data_array[index].poolRemaining = 0;
    fileIndexLoadToAddr(index, (FILELOADMETHOD)3, target, 0);
    expect_bytes(target, plain, sizeof(plain),
                 "the alternate address method uses the direct Indy path");

    for (u8 i = 0; i < sizeof(rom_source); i++)
    {
        rom_source[i] = (u8)(0xC0 + i);
    }
    file_entry_max = OBJ_INDEX_END;
    file_resource_table[1].hw_address = rom_source;
    resource_lookup_data_array[1].rom_size = sizeof(rom_source);
    resource_lookup_data_array[1].poolRemaining = 0;
    memcpy(fake_decoded_payload, "ROM-DEC", 7);
    fake_decoded_payload_size = 7;
    memset(target, 0, sizeof(target));
    fileIndexLoadToAddr(1, FILELOADMETHOD_DEFAULT, target, 32);
    expect_bytes(target, fake_decoded_payload, fake_decoded_payload_size,
                 "default address loads decode a ROM resource");

    memset(target, 0, sizeof(target));
    resource_lookup_data_array[1].poolRemaining = 0;
    fileIndexLoadToAddr(1, (FILELOADMETHOD)3, target, 0);
    expect_bytes(target, rom_source, sizeof(rom_source),
                 "the alternate address method directly loads a ROM resource");

    fake_bank_capacity = 32;
    fake_allocated_bytes = 0;
    fake_reallocate_calls = 0;
    memset(fake_bank_storage, 0, sizeof(fake_bank_storage));
    resource_lookup_data_array[index].poolRemaining = 0;
    void *bank_result = fileIndexLoadToBank(index, FILELOADMETHOD_DEFAULT, 0,
                                            MEMPOOL_STAGE);
    expect_true(bank_result == fake_bank_storage,
                "bank loads return the allocator's address");
    expect_int(fake_allocated_bytes, fake_bank_capacity,
               "default bank loads allocate the bank's remaining capacity");
    expect_int(fake_reallocate_calls, 1,
               "default bank loads shrink the final allocation");
    expect_int(fake_reallocated_size, (s32)sizeof(plain),
               "bank allocations are shrunk to the produced resource size");
    expect_int((s32)resource_lookup_data_array[index].rom_remaining,
               fake_bank_capacity,
               "bank loads retain the original allocation size");
    expect_bytes(fake_bank_storage, plain, sizeof(plain),
                 "bank loads copy an uncompressed Indy resource");

    fake_bank_capacity = 32;
    fake_reallocate_calls = 0;
    resource_lookup_data_array[1].poolRemaining = 0;
    fileIndexLoadToBank(1, FILELOADMETHOD_DEFAULT, 0, MEMPOOL_STAGE);
    expect_int(fake_reallocate_calls, 1,
               "default bank loads decode ROM resources before resizing");
    expect_bytes(fake_bank_storage, fake_decoded_payload,
                 fake_decoded_payload_size,
                 "default bank loads copy a decoded ROM resource");

    fake_bank_capacity = sizeof(rom_source);
    resource_lookup_data_array[1].poolRemaining = 0;
    fileIndexLoadToBank(1, (FILELOADMETHOD)3, 0, MEMPOOL_ME);
    expect_int((s32)resource_lookup_data_array[1].loaded_bank, MEMPOOL_ME,
               "alternate bank loads record a ROM resource's owner");
    expect_bytes(fake_bank_storage, rom_source, sizeof(rom_source),
                 "alternate bank loads directly copy a ROM resource");

    file_resource_table[1] = saved_rom_entry;
    file_entry_max = OBJ_INDEX_END;

    memset(fake_bank_storage, 0, sizeof(fake_bank_storage));
    fake_reallocate_calls = 0;
    resource_lookup_data_array[index].poolRemaining = fake_bank_capacity;
    fileIndexLoadToBank(index, FILELOADMETHOD_EXTRAMEM, 0, MEMPOOL_STAGE);
    expect_int(fake_reallocate_calls, 0,
               "extra-memory bank loads leave the allocation unshrunk");
    expect_bytes(fake_bank_storage, plain, sizeof(plain),
                 "extra-memory bank loads copy the resource");

    index = register_fake_resource("dynamic/direct-bank.seg", sizeof(plain));
    memcpy(fake_file_payload, plain, sizeof(plain));
    fake_file_payload_size = sizeof(plain);
    fake_file_size = sizeof(plain);
    fake_bank_capacity = sizeof(plain);
    fake_allocated_bytes = 0;
    memset(fake_bank_storage, 0, sizeof(fake_bank_storage));
    resource_lookup_data_array[index].poolRemaining = 0;
    fileIndexLoadToBank(index, (FILELOADMETHOD)3, 0, MEMPOOL_ME);
    expect_int(fake_allocated_bytes, 16,
               "direct bank loads allocate the aligned PC size");
    expect_int((s32)resource_lookup_data_array[index].loaded_bank, MEMPOOL_ME,
               "direct bank loads record the owning bank");
    expect_bytes(fake_bank_storage, plain, sizeof(plain),
                 "direct bank loads copy the complete resource");
}

static void test_initialization_and_resource_state(void)
{
    fileentry saved_one = file_resource_table[1];
    fileentry saved_two = file_resource_table[2];
    u8 rom_data[32] = {0};

    file_entry_max = 3;
    file_resource_table[1].hw_address = rom_data;
    file_resource_table[2].hw_address = rom_data + 24;
    resource_lookup_data_array[1].rom_size = 99;
    resource_lookup_data_array[1].poolRemaining = 77;
    resource_lookup_data_array[1].pc_size = 55;
    resource_lookup_data_array[1].rom_remaining = 44;
    fake_patch_model_calls = 0;
    fake_patch_bg_calls = 0;
    obInit();
    expect_int((s32)resource_lookup_data_array[1].rom_size, 24,
               "obInit derives a resource size from adjacent ROM addresses");
    expect_int((s32)resource_lookup_data_array[1].poolRemaining, 0,
               "obInit clears the decoded-size cache");
    expect_int((s32)resource_lookup_data_array[1].pc_size, 0,
               "obInit clears the PC-side size cache");
    expect_int((s32)resource_lookup_data_array[1].rom_remaining, 0,
               "obInit clears the active allocation size");
    expect_int(fake_patch_model_calls, 1,
               "obInit patches the PC model resource table");
    expect_int(fake_patch_bg_calls, 1,
               "obInit patches the PC background resource table");

    file_resource_table[1] = saved_one;
    file_resource_table[2] = saved_two;
    file_entry_max = OBJ_INDEX_END;

    fake_reallocate_calls = 0;
    fileSetSize(1, fake_bank_storage, 37, 1);
    expect_int((s32)resource_lookup_data_array[1].poolRemaining, 37,
               "fileSetSize updates the decoded-size cache");
    expect_int((s32)resource_lookup_data_array[1].rom_remaining, 37,
               "fileSetSize updates the active allocation size");
    expect_int(fake_reallocate_calls, 1,
               "fileSetSize can resize the owning memory-pool entry");
    expect_int(fake_reallocated_size, 37,
               "fileSetSize passes the requested size to the allocator");
    expect_int(fake_reallocated_bank, MEMPOOL_STAGE,
               "fileSetSize resizes the stage-pool entry");
    expect_int(get_pc_remaining_buffer_for_index(1), 37,
               "the indexed PC-size getter returns the cached size");
    expect_int(get_rom_remaining_buffer_for_index(1), 37,
               "the indexed ROM-size getter returns the active size");

    resource_lookup_data_array[1].poolRemaining = 41;
    expect_int(get_pc_buffer_remaining_value(
                   (u8 *)file_resource_table[1].filename), 41,
               "the name-based remaining-size getter resolves known resources");

    file_entry_max = 3;
    resource_lookup_data_array[1].loaded_bank = 2;
    resource_lookup_data_array[1].poolRemaining = 10;
    resource_lookup_data_array[2].loaded_bank = 5;
    resource_lookup_data_array[2].poolRemaining = 20;
    obBlankResourcesLoadedInBank(4);
    expect_int((s32)resource_lookup_data_array[1].loaded_bank, 0,
               "bank cleanup clears resources owned by an earlier bank");
    expect_int((s32)resource_lookup_data_array[2].loaded_bank, 5,
               "bank cleanup preserves resources owned by a later bank");
    expect_int((s32)resource_lookup_data_array[1].poolRemaining, 0,
               "stage-bank cleanup clears decoded resource sizes");
    expect_int((s32)resource_lookup_data_array[2].poolRemaining, 0,
               "stage-bank cleanup clears every decoded resource size");
    obBlankResourcesInBank5();
    expect_int((s32)resource_lookup_data_array[2].loaded_bank, 0,
               "bank-five cleanup clears resources owned by bank five");
    file_entry_max = OBJ_INDEX_END;
}

static void test_background_and_compatibility_wrappers(void)
{
    fileentry saved_one = file_resource_table[1];
    u8 source[16];
    u8 target[16] = {0};
    const u8 expected[] = {4, 5, 6, 7};

    for (u8 i = 0; i < sizeof(source); i++)
    {
        source[i] = i;
    }
    file_entry_max = OBJ_INDEX_END;
    file_resource_table[1].hw_address = source;
    resource_lookup_data_array[1].rom_size = sizeof(source);
    fake_rom_copy_calls = 0;
    obLoadBGFileBytesAtOffset((u8 *)file_resource_table[1].filename,
                              target, 4, sizeof(expected));
    expect_int(fake_rom_copy_calls, 1,
               "background offset loads issue one ROM copy");
    expect_bytes(target, expected, sizeof(expected),
                 "background offset loads copy the requested subrange");
    file_resource_table[1] = saved_one;

    const u8 plain[] = {'W', 'R', 'A', 'P'};
    s32 index = register_fake_resource("dynamic/wrapper.seg", sizeof(plain));
    memcpy(fake_file_payload, plain, sizeof(plain));
    fake_file_payload_size = sizeof(plain);
    fake_file_size = sizeof(plain);
    resource_lookup_data_array[index].pc_size = sizeof(plain);
    memset(target, 0, sizeof(target));
    _fileIndexLoadToAddr(index, FILELOADMETHOD_UNK2, target, 0);
    expect_bytes(target, plain, sizeof(plain),
                 "the legacy indexed address wrapper delegates to the loader");

    memset(target, 0, sizeof(target));
    _fileNameLoadToAddr("dynamic/wrapper.seg", FILELOADMETHOD_UNK2,
                        target, 0);
    expect_bytes(target, plain, sizeof(plain),
                 "the name-based address wrapper delegates to the loader");

    fake_bank_capacity = 16;
    memset(fake_bank_storage, 0, sizeof(fake_bank_storage));
    resource_lookup_data_array[index].poolRemaining = 0;
    _fileIndexLoadToBank(index, FILELOADMETHOD_UNK2, 0, MEMPOOL_ME);
    expect_bytes(fake_bank_storage, plain, sizeof(plain),
                 "the legacy indexed bank wrapper delegates to the loader");

    index = register_fake_resource("dynamic/wrapper-bank.seg", sizeof(plain));
    memcpy(fake_file_payload, plain, sizeof(plain));
    fake_file_payload_size = sizeof(plain);
    fake_file_size = sizeof(plain);
    resource_lookup_data_array[index].pc_size = sizeof(plain);
    resource_lookup_data_array[index].poolRemaining = 0;
    memset(fake_bank_storage, 0, sizeof(fake_bank_storage));
    _fileNameLoadToBank("dynamic/wrapper-bank.seg", FILELOADMETHOD_UNK2,
                        0, MEMPOOL_ME);
    expect_bytes(fake_bank_storage, plain, sizeof(plain),
                 "the name-based bank wrapper delegates to the loader");

    file_entry_max = 2;
    removed_loop_handle_filetable_entries();
    removed_loop_filetableentries();
    sub_GAME_7F0BD410();
    file_entry_max = OBJ_INDEX_END;
}

int main(void)
{
    test_missing_file_does_not_consume_table_slot();
    test_resource_lookup_registration();
    test_resource_lookup_rejects_null_and_full_table();
    test_indy_resource_loading();
    test_rom_resource_loading();
    test_index_loading_paths();
    test_initialization_and_resource_state();
    test_background_and_compatibility_wrappers();
    return failures == 0 ? 0 : 1;
}
