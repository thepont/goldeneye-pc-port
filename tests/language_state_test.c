#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include <ultra64.h>

#include "bondconstants.h"
#include "language.h"
#include "memp.h"
#include "ob.h"

resource_lookup_data_entry resource_lookup_data_array[1];

extern s32 g_LangBanks[LANGUAGE_BANK_COUNT];
extern struct jpncharpixels *g_JpnCharCachePixels;
extern struct jpncacheitem *g_JpnCacheCacheItems;

static u8 jpn_pixel_storage[JPN_CACHE_PIXEL_BYTES];
static struct jpncacheitem jpn_cache_storage[JPN_CACHE_ENTRY_COUNT];
static u8 *fake_bank;
static int allocation_calls;
static int bank_load_calls;
static int address_load_calls;
static int lookup_calls;
static int fixup_calls;
static int rom_copy_calls;
static u32 last_rom_copy_size;
static int failures;

u8 _efontchardataSegmentRomStart;
u8 _jfontchardataSegmentRomStart;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_s32(s32 actual, s32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %d, expected %d)\n",
                message, actual, expected);
        failures++;
    }
}

static void expect_ptr(const void *actual, const void *expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %p, expected %p)\n",
                message, actual, expected);
        failures++;
    }
}

static void reset_fixture(void)
{
    memset(g_LangBanks, 0, sizeof(g_LangBanks));
    memset(jpn_pixel_storage, 0, sizeof(jpn_pixel_storage));
    memset(jpn_cache_storage, 0, sizeof(jpn_cache_storage));
    memset(&resource_lookup_data_array[0], 0,
           sizeof(resource_lookup_data_array[0]));
    allocation_calls = 0;
    bank_load_calls = 0;
    address_load_calls = 0;
    lookup_calls = 0;
    fixup_calls = 0;
    rom_copy_calls = 0;
    last_rom_copy_size = 0;
    g_JpnCharCachePixels = NULL;
    g_JpnCacheCacheItems = NULL;
    j_text_trigger = 0;
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    expect_true(bank == MEMPOOL_PERMANENT,
                "language initialization allocates Japanese cache data permanently");
    allocation_calls++;

    if (bytes == JPN_CACHE_PIXEL_BYTES)
    {
        return jpn_pixel_storage;
    }
    if (bytes == JPN_CACHE_ITEM_STORAGE_BYTES)
    {
        return jpn_cache_storage;
    }

    return NULL;
}

void * _fileNameLoadToBank(char *name, FILELOADMETHOD load_method,
                           s32 size, u8 bank)
{
    (void)name;
    expect_s32(load_method, FILELOADMETHOD_DEFAULT,
               "language bank loads use the default load method");
    expect_s32(size, LANG_BANK_LOAD_SIZE_HINT,
               "language bank loads use the expected allocation size");
    expect_s32(bank == MEMPOOL_PERMANENT || bank == MEMPOOL_STAGE, 1,
               "language bank loads use a known memory bank");
    bank_load_calls++;
    return fake_bank;
}

void * _fileNameLoadToAddr(char *name, FILELOADMETHOD load_method,
                           u8 *target, s32 size)
{
    (void)name;
    (void)size;
    expect_s32(load_method, FILELOADMETHOD_DEFAULT,
               "address language loads use the default load method");
    address_load_calls++;
    return target;
}

s32 fileGetIndex(char *name)
{
    (void)name;

    /* Exercise both sides of langFixupLoadedBank's lookup/size guards. */
    if (lookup_calls == 0)
    {
        lookup_calls++;
        return -1;
    }
    if (lookup_calls == 1)
    {
        resource_lookup_data_array[0].poolRemaining = 64;
        lookup_calls++;
        return 0;
    }
    if (lookup_calls == 2)
    {
        resource_lookup_data_array[0].poolRemaining = 0;
        lookup_calls++;
        return 0;
    }

    lookup_calls++;
    return -1;
}

void romdataFixupLangBank(u8 *blob, u32 blob_size)
{
    expect_ptr(blob, fake_bank,
               "language fixup receives the bank returned by the loader");
    expect_s32(blob_size, 64,
               "language fixup receives the resource's decompressed size");
    fixup_calls++;
}

void romCopy(void *target, void *source, u32 size)
{
    (void)source;
    rom_copy_calls++;
    last_rom_copy_size = size;
    memset(target, 0xa5, size);
}

void osSyncPrintf(const char *format, ...)
{
    (void)format;
}

static void test_stage_to_language_bank_mapping(void)
{
    static const struct
    {
        LEVELID stage;
        LEVELID bank;
    } cases[] = {
        {LEVELID_DAM, LDAM},
        {LEVELID_FACILITY, LARK},
        {LEVELID_RUNWAY, LRUN},
        {LEVELID_SURFACE, LSEVX},
        {LEVELID_BUNKER1, LSEV},
        {LEVELID_SILO, LSILO},
        {LEVELID_FRIGATE, LDEST},
        {LEVELID_SURFACE2, LSEVXB},
        {LEVELID_BUNKER2, LSEVB},
        {LEVELID_STATUE, LSTAT},
        {LEVELID_ARCHIVES, LARCH},
        {LEVELID_STREETS, LPETE},
        {LEVELID_DEPOT, LDEPO},
        {LEVELID_TRAIN, LTRA},
        {LEVELID_JUNGLE, LJUN},
        {LEVELID_CONTROL, LAREC},
        {LEVELID_CAVERNS, LCAVE},
        {LEVELID_CRADLE, LCRAD},
        {LEVELID_AZTEC, LAZT},
        {LEVELID_EGYPT, LCRYP},
        {LEVELID_TEMPLE, LDISH},
        {LEVELID_COMPLEX, LREF},
        {LEVELID_LIBRARY, LAME},
        {LEVELID_BASEMENT, LIMP},
        {LEVELID_STACK, LASH},
        {LEVELID_CAVES, LOAT},
        {LEVELID_CUBA, LLEN}
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        expect_s32(langGetLangBankIndexFromStagenum(cases[i].stage),
                   cases[i].bank, "stage maps to its language bank");
    }
}

static void test_language_initialization_and_load_helpers(void)
{
    u8 target[32];

    reset_fixture();
    expect_true(fake_bank != NULL, "the low-address language bank was allocated");
    j_text_trigger = 1;
    langInit();

    expect_s32(allocation_calls, 2,
               "Japanese initialization allocates both cache buffers");
    expect_s32(bank_load_calls, 7,
               "language initialization loads the seven startup banks");
    expect_s32(fixup_calls, 1,
               "only a loaded language bank with a nonzero resource size is fixed up");
    expect_ptr(g_JpnCharCachePixels, jpn_pixel_storage,
               "Japanese pixel cache points at the permanent allocation");
    expect_ptr(g_JpnCacheCacheItems, jpn_cache_storage,
               "Japanese item cache points at the permanent allocation");
    expect_ptr((void *)(uintptr_t)g_LangBanks[LGUN], fake_bank,
               "startup language bank stores the loader result");

    /* Keep the helper-load checks focused on their loader calls.  Their
     * legacy s32 bank slot cannot round-trip a high stack pointer on the
     * 64-bit host, so do not route that fixture through the ROM fixup hook. */
    lookup_calls = 3;
    langLoadToAddr(LDAM);
    expect_s32(bank_load_calls, 8,
               "langLoadToAddr loads one additional bank");

    langLoadToBank(LDAM, target, sizeof(target));
    expect_s32(address_load_calls, 1,
               "langLoadToBank uses the address-loading port");
    expect_ptr((void *)(uintptr_t)(u32)g_LangBanks[LDAM],
               (void *)(uintptr_t)(u32)(uintptr_t)target,
               "langLoadToBank stores the target's N64-width address");

    langClearBank(LDAM);
    expect_s32(g_LangBanks[LDAM], 0,
               "langClearBank removes a loaded bank");
}

static void test_language_tick_expires_japanese_cache_entries(void)
{
    int i;

    reset_fixture();
    g_JpnCacheCacheItems = jpn_cache_storage;
    j_text_trigger = 1;
    for (i = 0; i < JPN_CACHE_ENTRY_COUNT; i++)
    {
        jpn_cache_storage[i].ttl = (i & 1) ? 2 : 0;
    }

    langTick();

    for (i = 0; i < JPN_CACHE_ENTRY_COUNT; i++)
    {
        expect_s32(jpn_cache_storage[i].ttl, (i & 1) ? 1 : 0,
                   "language tick decrements live cache entries only");
    }
}

static void reset_japanese_cache(u16 ttl)
{
    int i;

    g_JpnCharCachePixels = (struct jpncharpixels *)jpn_pixel_storage;
    g_JpnCacheCacheItems = jpn_cache_storage;
    for (i = 0; i < JPN_CACHE_ENTRY_COUNT; i++)
    {
        jpn_cache_storage[i].ttl = ttl;
        jpn_cache_storage[i].codepoint = 0;
    }
    rom_copy_calls = 0;
    last_rom_copy_size = 0;
}

static void test_japanese_cache_hit_and_miss_paths(void)
{
    struct jpncharpixels *pixels;
    s32 single_codepoint = 0x246;
    s32 multi_codepoint = 0x2246;

    reset_fixture();
    reset_japanese_cache(1);
    jpn_cache_storage[4].codepoint = single_codepoint >> 1;
    pixels = langGetJpnCharPixels(single_codepoint);
    expect_ptr(pixels, &g_JpnCharCachePixels[4 * JPN_CACHE_PIXEL_STRIDE],
               "single-byte Japanese cache hit returns its glyph pixels");
    expect_s32(jpn_cache_storage[4].ttl, 2,
               "single-byte cache hit refreshes its ttl");
    expect_s32(rom_copy_calls, 0,
               "single-byte cache hit does not copy from the ROM");

    reset_japanese_cache(1);
    jpn_cache_storage[6].codepoint = multi_codepoint >> 1;
    jpn_cache_storage[7].codepoint = multi_codepoint >> 1;
    pixels = langGetJpnCharPixels(multi_codepoint);
    expect_ptr(pixels, &g_JpnCharCachePixels[6 * JPN_CACHE_PIXEL_STRIDE],
               "multibyte Japanese cache hit returns the first glyph pixels");
    expect_s32(jpn_cache_storage[6].ttl, 2,
               "multibyte cache hit refreshes the first ttl");
    expect_s32(jpn_cache_storage[7].ttl, 2,
               "multibyte cache hit refreshes the second ttl");

    reset_japanese_cache(1);
    jpn_cache_storage[10].ttl = 0;
    pixels = langGetJpnCharPixels(single_codepoint);
    expect_ptr(pixels, &g_JpnCharCachePixels[10 * JPN_CACHE_PIXEL_STRIDE],
               "single-byte cache miss uses the available single slot");
    expect_s32(jpn_cache_storage[10].codepoint, single_codepoint >> 1,
               "single-byte cache miss records its codepoint");
    expect_s32(last_rom_copy_size, JPN_SINGLE_GLYPH_ROM_BYTES,
               "single-byte cache miss copies one glyph from the ROM");

    reset_japanese_cache(1);
    jpn_cache_storage[20].ttl = 0;
    jpn_cache_storage[21].ttl = 0;
    pixels = langGetJpnCharPixels(multi_codepoint);
    expect_ptr(pixels, &g_JpnCharCachePixels[20 * JPN_CACHE_PIXEL_STRIDE],
               "multibyte cache miss uses an adjacent pair of free slots");
    expect_s32(jpn_cache_storage[20].codepoint, multi_codepoint >> 1,
               "multibyte cache miss records the first codepoint");
    expect_s32(jpn_cache_storage[21].codepoint, multi_codepoint >> 1,
               "multibyte cache miss records the second codepoint");
    expect_s32(last_rom_copy_size, JPN_MULTIBYTE_GLYPH_ROM_BYTES,
               "multibyte cache miss copies two-byte glyph data");
}

static void test_last_single_cache_slot_does_not_read_past_the_cache(void)
{
    struct jpncharpixels *pixels;

    reset_fixture();
    reset_japanese_cache(1);
    jpn_cache_storage[JPN_CACHE_ENTRY_COUNT - 1].ttl = 0;

    pixels = langGetJpnCharPixels(0x246);

    expect_ptr(pixels,
               &g_JpnCharCachePixels[(JPN_CACHE_ENTRY_COUNT - 1) * JPN_CACHE_PIXEL_STRIDE],
               "the final cache slot remains usable without a phantom pair");
    expect_s32(jpn_cache_storage[JPN_CACHE_ENTRY_COUNT - 1].ttl, 2,
               "the final single cache slot is refreshed");
}

static void GivenJapaneseCacheHasNoFreeSlot_WhenRequestingUncachedSingleGlyph_ThenFallbackPixelsAreReturned(void)
{
    struct jpncharpixels *pixels;

    /* Arrange */
    reset_fixture();
    reset_japanese_cache(1);

    /* Act */
    pixels = langGetJpnCharPixels(0x246);

    /* Assert */
    expect_ptr(pixels, g_JpnCharCachePixels,
               "a full Japanese cache returns its stable fallback pixel region");
    expect_s32(rom_copy_calls, 0,
               "a full Japanese cache does not copy a glyph into a busy slot");
}

static void test_language_string_bank_guards(void)
{
    u32 *bank_words;
    u8 *string;

    reset_fixture();
    bank_words = (u32 *)fake_bank;
    bank_words[3] = 32;
    string = fake_bank + 32;
    memcpy(string, "Dam", 4);
    g_LangBanks[LDAM] = (s32)(uintptr_t)fake_bank;

    expect_ptr(langGet(getStringID(LDAM, 3)), string,
               "langGet resolves a present string within a loaded bank");
    bank_words[4] = 0;
    expect_ptr(langGet(getStringID(LDAM, 4)), NULL,
               "langGet returns NULL for an empty string slot");
    bank_words[LANGUAGE_TEXT_SLOT_MASK] = 32;
    expect_ptr(langGet(getStringID(LDAM, LANGUAGE_TEXT_SLOT_MASK)), string,
               "langGet resolves the highest text slot in a bank");
    expect_ptr(langGet(-1), NULL,
               "langGet rejects a negative slot id");
    expect_ptr(langGet(LANGUAGE_BANK_COUNT << LANGUAGE_TEXT_SLOT_SHIFT), NULL,
               "langGet rejects the first bank outside the bank table");
    expect_ptr(langGet(getStringID(LNULL, 0)), NULL,
               "langGet rejects an unloaded bank");

    g_LangBanks[LDAM] = 1;
    expect_ptr(langGet(getStringID(LDAM, 0)), NULL,
               "langGet rejects an implausibly low bank address");
    g_LangBanks[LDAM] = -1;
    expect_ptr(langGet(getStringID(LDAM, 0)), NULL,
               "langGet rejects a sign-extended bank address");
}

int main(void)
{
    fake_bank = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    expect_true(fake_bank != MAP_FAILED,
                "a low-address fixture is available for the N64 bank pointer representation");
    if (fake_bank == MAP_FAILED)
    {
        return 1;
    }

    test_stage_to_language_bank_mapping();
    test_language_initialization_and_load_helpers();
    test_language_tick_expires_japanese_cache_entries();
    test_japanese_cache_hit_and_miss_paths();
    test_last_single_cache_slot_does_not_read_past_the_cache();
    GivenJapaneseCacheHasNoFreeSlot_WhenRequestingUncachedSingleGlyph_ThenFallbackPixelsAreReturned();
    test_language_string_bank_guards();

    munmap(fake_bank, 4096);
    expect_true(failures == 0, "all language state tests pass");
    return failures == 0 ? 0 : 1;
}
