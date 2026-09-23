#include <stdio.h>
#include <string.h>

#include <ultra64.h>

#include "image.h"
#include "memp.h"
#include "ob.h"
#include "objecthandler.h"
#include "assets/obseg/file_resource_id_enums.h"

resource_lookup_data_entry resource_lookup_data_array[OBJ_INDEX_MAX];

static int failures;
static int fake_index = 1;
static s32 fake_rom_remaining = 0x220;
static s32 fake_pc_remaining = 0x180;
#define FAKE_FILE_BUFFER_SIZE 1024
static u8 *fake_file_buffer;
static int addr_load_calls;
static int bank_load_calls;
static char *last_load_name;
static FILELOADMETHOD last_load_method;
static u8 *last_addr;
static s32 last_load_size;
static s32 last_bank_size;
static u8 last_bank;
static int file_set_size_calls;
static u8 *last_file_set_pointer;
static u32 last_file_set_size;
static s32 last_file_set_reallocate;
static int model_patch_calls;
static int background_patch_calls;
static int promote_calls;
static ModelFileHeader *last_promoted_header;
static s32 last_promoted_vma;
static uintptr_t last_promoted_address;
static int iterate_calls;
static int return_display_list;
static int return_child_node;
static int return_following_display_list;
static int texture_copy_calls;
static int texture_header_calls;
static int node_replace_calls;
static int gdl_load_calls;
static Gfx fake_gdl;
static ModelNode fake_node;

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

s32 fileGetIndex(char *name)
{
    (void)name;
    return fake_index;
}

s32 get_rom_remaining_buffer_for_index(s32 index)
{
    (void)index;
    return fake_rom_remaining;
}

s32 get_pc_remaining_buffer_for_index(s32 index)
{
    (void)index;
    return fake_pc_remaining;
}

void *_fileNameLoadToAddr(char *name, FILELOADMETHOD method,
                          u8 *target, s32 size)
{
    addr_load_calls++;
    last_load_name = name;
    last_load_method = method;
    last_addr = target;
    last_load_size = size;
    return fake_file_buffer;
}

void *_fileNameLoadToBank(char *name, FILELOADMETHOD method,
                          s32 size, u8 bank)
{
    bank_load_calls++;
    last_load_name = name;
    last_load_method = method;
    last_bank_size = size;
    last_bank = bank;
    return fake_file_buffer;
}

void fileSetSize(s32 index, u8 *pointer, u32 size, s32 reallocate)
{
    (void)index;
    file_set_size_calls++;
    last_file_set_pointer = pointer;
    last_file_set_size = size;
    last_file_set_reallocate = reallocate;
}

void pcmodelsPatchTable(void)
{
    model_patch_calls++;
}

void pccgPatchTable(void)
{
    background_patch_calls++;
}

void sub_GAME_7F075A90(ModelFileHeader *header, s32 vma, uintptr_t address)
{
    promote_calls++;
    last_promoted_header = header;
    last_promoted_vma = vma;
    last_promoted_address = address;
}

void modelIterateDisplayLists(ModelFileHeader *header,
                              ModelNode **nodeptr, Gfx **gdlptr)
{
    (void)header;
    iterate_calls++;
    if (iterate_calls == 1)
    {
        *gdlptr = return_display_list ? &fake_gdl : NULL;
        *nodeptr = return_child_node ? &fake_node : NULL;
    }
    else
    {
        *gdlptr = (iterate_calls == 2 && return_following_display_list) ?
                      &fake_gdl : NULL;
        *nodeptr = NULL;
    }
}

void texCopyGdls(Gfx *source, Gfx *destination, s32 size)
{
    (void)source;
    (void)destination;
    (void)size;
    texture_copy_calls++;
}

void texLoadFromModelFileHeader(ModelFileHeader *header, struct texpool *pool)
{
    (void)header;
    (void)pool;
    texture_header_calls++;
}

void modelNodeReplaceGdl(u32 header, ModelNode *node,
                         Gfx *find, Gfx *replacement)
{
    (void)header;
    (void)node;
    (void)find;
    (void)replacement;
    node_replace_calls++;
}

s32 texLoadFromGdl(Gfx *source, s32 source_size, Gfx *destination,
                   void *pool)
{
    (void)source;
    (void)source_size;
    (void)destination;
    (void)pool;
    gdl_load_calls++;
    return 16;
}

static void reset_fakes(void)
{
    memset(resource_lookup_data_array, 0,
           sizeof(resource_lookup_data_array));
    addr_load_calls = 0;
    bank_load_calls = 0;
    last_load_name = NULL;
    last_load_method = FILELOADMETHOD_EXTRAMEM;
    last_addr = NULL;
    last_load_size = 0;
    last_bank_size = 0;
    last_bank = 0;
    file_set_size_calls = 0;
    last_file_set_pointer = NULL;
    last_file_set_size = 0;
    last_file_set_reallocate = 0;
    model_patch_calls = 0;
    background_patch_calls = 0;
    promote_calls = 0;
    last_promoted_header = NULL;
    last_promoted_vma = 0;
    last_promoted_address = 0;
    iterate_calls = 0;
    return_display_list = 0;
    return_child_node = 0;
    return_following_display_list = 0;
    texture_copy_calls = 0;
    texture_header_calls = 0;
    node_replace_calls = 0;
    gdl_load_calls = 0;
}

static void initialize_header(ModelFileHeader *header, s16 switches,
                              s16 textures)
{
    memset(header, 0, sizeof(*header));
    header->numSwitches = switches;
    header->numtextures = textures;
}

static void expect_header_layout(ModelFileHeader *header, s16 switches,
                                 s16 textures)
{
    expect_true(header->Switches == (ModelNode **)fake_file_buffer,
                "model header stores the loaded switch-array address");
    expect_true((u8 *)header->Textures ==
                    fake_file_buffer + sizeof(ModelNode *) * switches,
                "PC model headers use pointer-width switch slots");
    expect_true(header->RootNode ==
                    (ModelNode *)&header->Textures[textures],
                "model headers place the root node after texture records");
}

static void test_custom_buffer_header_layout(void)
{
    ModelFileHeader header;
    u8 destination[1024] = {0};
    const char name[] = "CtestModelZ";
    struct texpool pool = {0};

    reset_fakes();
    initialize_header(&header, 3, 2);
    resource_lookup_data_array[fake_index].poolRemaining = 77;
    load_object_fill_header(&header, (u8 *)name, destination, 456, &pool);

    expect_int(addr_load_calls, 1,
               "custom-buffer model loads use the address loader once");
    expect_true(last_load_name == name,
                "custom-buffer model loads preserve the resource name");
    expect_int(last_load_method, FILELOADMETHOD_EXTRAMEM,
               "model loads retain the extra-memory load method");
    expect_true(last_addr == destination,
                "custom-buffer model loads pass the caller's destination");
    expect_int(last_load_size, 456,
               "custom-buffer model loads pass the caller's size");
    expect_int(bank_load_calls, 0,
               "custom-buffer model loads do not allocate a bank buffer");
    expect_int((s32)resource_lookup_data_array[fake_index].poolRemaining, 77,
               "custom-buffer loads preserve an existing pool size");
    expect_header_layout(&header, 3, 2);
    expect_int(model_patch_calls, 1,
               "model loads patch the PC model table");
    expect_int(background_patch_calls, 1,
               "model loads patch the PC background table");
    expect_int(promote_calls, 1,
               "model loads promote serialized node offsets");
    expect_true(last_promoted_header == &header,
                "promotion receives the loaded model header");
    expect_int(last_promoted_vma, 0x5000000,
               "promotion retains the model virtual base");
    expect_int(iterate_calls, 1,
               "a model without display lists is inspected once");
    expect_int(file_set_size_calls, 0,
               "a model without display lists does not compact GDL data");
}

static void test_bank_load_resets_stale_pool(void)
{
    ModelFileHeader header;
    const char name[] = "CbankModelZ";

    reset_fakes();
    initialize_header(&header, 1, 1);
    resource_lookup_data_array[fake_index].poolRemaining = 1234;
    fileLoad(&header, (char *)name);

    expect_int(bank_load_calls, 1,
               "fileLoad uses the bank loader for the default path");
    expect_true(last_load_name == name,
                "fileLoad preserves the model resource name");
    expect_int(last_load_method, FILELOADMETHOD_EXTRAMEM,
               "fileLoad uses the extra-memory load method");
    expect_int(last_bank_size, 0x100,
               "fileLoad retains the default model load size hint");
    expect_int(last_bank, MEMPOOL_STAGE,
               "fileLoad allocates models from the stage bank");
    expect_int((s32)resource_lookup_data_array[fake_index].poolRemaining, 0,
               "fresh bank loads clear stale decoded-size state");
    expect_header_layout(&header, 1, 1);
}

static void test_compaction_preserves_host_pointer(void)
{
    ModelFileHeader header;
    const char name[] = "CcompactModelZ";

    reset_fakes();
    initialize_header(&header, 0, 0);
    return_display_list = 1;
    load_object_fill_header(&header, (u8 *)name, fake_file_buffer,
                            FAKE_FILE_BUFFER_SIZE, NULL);

    expect_int(file_set_size_calls, 1,
               "a loaded display list reports its compacted size");
    expect_true(last_file_set_pointer == fake_file_buffer,
                "GDL compaction passes the full host allocation pointer");
    expect_int(last_file_set_reallocate, 0,
               "custom-buffer compaction does not rewind the bank");
    expect_int(texture_copy_calls, 1,
               "GDL compaction mirrors the original display list");
    expect_int(texture_header_calls, 1,
               "GDL compaction loads model textures");
}

static void test_compaction_walks_following_nodes(void)
{
    ModelFileHeader header;
    const char name[] = "CwalkModelZ";

    reset_fakes();
    initialize_header(&header, 0, 0);
    return_display_list = 1;
    return_child_node = 1;
    return_following_display_list = 1;
    load_object_fill_header(&header, (u8 *)name, fake_file_buffer,
                            FAKE_FILE_BUFFER_SIZE, NULL);

    expect_int(iterate_calls, 2,
               "GDL compaction iterates until the node chain ends");
    expect_int(node_replace_calls, 1,
               "GDL compaction replaces each following node display list");
    expect_int(gdl_load_calls, 1,
               "GDL compaction expands each following display list");
}

static void test_legacy_memory_wrapper(void)
{
    ModelFileHeader header;
    const char name[] = "ClegacyModelZ";

    reset_fakes();
    initialize_header(&header, 0, 0);
    load_object_into_memory_unused_maybe(
        &header, (int *)name, (int *)fake_file_buffer,
        FAKE_FILE_BUFFER_SIZE);

    expect_int(addr_load_calls, 1,
               "the legacy memory wrapper delegates to address loading");
    expect_true(last_addr == fake_file_buffer,
                "the legacy memory wrapper preserves the target pointer");
}

int main(void)
{
    u8 fixture[FAKE_FILE_BUFFER_SIZE] = {0};
    fake_file_buffer = fixture;

    test_custom_buffer_header_layout();
    test_bank_load_resets_stale_pool();
    test_compaction_preserves_host_pointer();
    test_compaction_walks_following_nodes();
    test_legacy_memory_wrapper();
    return failures == 0 ? 0 : 1;
}
