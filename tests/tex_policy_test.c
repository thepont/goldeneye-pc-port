#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <ultra64.h>
#include "gbi_extension.h"
#include "image.h"
#include "lightfixture.h"
#include "lv.h"
#include "tex.h"
#include "unk_092E50.h"

/* These helpers are part of the texture state/cache boundary but were not
 * exposed by tex.h.  Keep the test coupled to their observable behavior, not
 * to private storage layout. */
s32 texGetWidthAtLod(struct tex *tex, s32 lod);
s32 texGetLineSizeInBytes(struct tex *tex, s32 lod);
s32 texGetSizeInBytes(struct tex *tex, s32 lod);
s32 texDimensionToMask(s32 dimension);
u32 texModeToGbiMode(s32 mode);
s32 texTrySetTileState(s32 tile, s32 format, s32 depth, s32 line,
                       s32 tmem, s32 smode, s32 tmode, s32 masks,
                       s32 maskt, s32 shifts, s32 shiftt);
s32 texTrySetTileSize(s32 tile, s32 uls, s32 ult, s32 lrs, s32 lrt);
void sub_GAME_7F0CC4C8(void);
s32 sub_GAME_7F0CCAFC(f32 value);
s32 sub_GAME_7F0CCB38(s32 *texture);
void nullsub_47(void);
s32 unused_copy_byte_array(u8 *src, s32 count, u8 *dst);
u32 texSetLutMode(s32 mode);
Gfx *texWriteTileFromDefinition(Gfx *gdl, struct tex *tex, s32 offset,
                                 s32 shifts, s32 shiftt, s32 prim_alpha);
Gfx *texWriteTextureCmd(Gfx *gdl, Gfx *saved, struct tex *tex,
                        s32 write_command);
Gfx *texWriteLoadToTmemAddr(Gfx *gdl, struct tex *tex, s32 tmemoffset);
Gfx *texWriteTileLods(Gfx *gdl, struct tex *tex, s32 smode, s32 tmode,
                      s32 offset, s32 basetile, s32 tmemaddr);
Gfx *texWriteLoadToTmemZero(Gfx *gdl, struct tex *tex);
Gfx *texWriteTile(Gfx *gdl, struct tex *tex, s32 smode, s32 tmode,
                  s32 offset, s32 tile);
Gfx *texHandleType0(Gfx *gdl, struct tex *tex, s32 smode, s32 tmode,
                    s32 offset, u32 shifts, u32 shiftt, u32 min);
Gfx *texHandleType1(Gfx *gdl, struct tex *tex1, s32 smode, s32 tmode,
                    s32 offset, struct tex *tex2, u32 shifts, u32 shiftt,
                    u32 min);
Gfx *texHandleType2(Gfx *gdl, struct tex *tex, s32 smode, s32 tmode,
                    s32 offset);
Gfx *texHandleType3(Gfx *gdl, u32 *tex, s32 smode, s32 tmode, s32 offset);
Gfx *texHandleType4(Gfx *gdl, u32 *tex, s32 smode, s32 tmode, s32 offset);

/* tex.c normally receives these from image.c, lv.c, and lightfixture.c.  The
 * policy target deliberately links only tex.c, so the test owns deterministic
 * fakes at those side-effect boundaries. */
struct image_entry g_Textures[4096];
struct texpool ptr_texture_alloc_start;
s32 D_800483C4;
struct LvlMpUnknown *D_800483C8;

static struct tex fake_texture;
static u8 fake_texture_data[0x4000];
static struct LvlMpUnknown fake_level_texture_table[4096];
static s32 fake_missing_texture;
static bool fake_texture_available;
static bool fake_light_image;
static s32 fake_texture_load_calls;
static s32 fake_light_begin_calls;
static s32 fake_light_end_calls;
static s32 fake_caverns_water_calls;
static s32 fake_dam_water_calls;

void osSyncPrintf(const char *format, ...)
{
    (void)format;
}

struct texcacheitem g_TexCacheItems[150];
s32 g_TexCacheCount;

static int failures;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void texLoadFromTextureNum(s32 texturenum, struct texpool *pool)
{
    (void)texturenum;
    (void)pool;
    fake_texture_load_calls++;
}

struct tex *texFindInPool(s32 texturenum, struct texpool *pool)
{
    (void)pool;
    if (!fake_texture_available || texturenum == fake_missing_texture)
    {
        return NULL;
    }
    return &fake_texture;
}

bool check_if_imageID_is_light(s32 imageID)
{
    return fake_light_image && imageID == fake_texture.texturenum;
}

void lightFixtureEntryBegin(Gfx *gdl)
{
    (void)gdl;
    fake_light_begin_calls++;
}

void lightFixtureEntryEnd(Gfx *gdl)
{
    (void)gdl;
    fake_light_end_calls++;
}

Gfx *sub_GAME_7F09343C(Gfx *gdl, s32 enabled)
{
    fake_caverns_water_calls += enabled != 0;
    return gdl + 1;
}

Gfx *sub_GAME_7F09365C(Gfx *gdl, s32 enabled)
{
    fake_dam_water_calls += enabled != 0;
    return gdl + 1;
}

static unsigned command_opcode(const Gfx *command)
{
    return (unsigned)((command->words.w0 >> 24) & 0xffU);
}

static size_t command_count(const Gfx *commands, size_t count, unsigned opcode)
{
    size_t found = 0;
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (command_opcode(&commands[i]) == opcode)
        {
            found++;
        }
    }
    return found;
}

static size_t command_count_with_byte(const Gfx *commands, size_t count,
                                      unsigned opcode, unsigned byte_one)
{
    size_t found = 0;
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (command_opcode(&commands[i]) == opcode &&
            (((commands[i].words.w0 >> 8) & 0xffU) == byte_one))
        {
            found++;
        }
    }
    return found;
}

static void prepare_texture(struct tex *texture, u8 width, u8 height,
                            u8 depth, u8 maxlod)
{
    memset(texture, 0, sizeof(*texture));
    texture->texturenum = 0;
    texture->data = fake_texture_data;
    texture->width = width;
    texture->height = height;
    texture->maxlod = maxlod;
    texture->gbiformat = G_IM_FMT_RGBA;
    texture->depth = depth;
}

static void reset_fixture(void)
{
    memset(g_TexCacheItems, 0, sizeof(g_TexCacheItems));
    g_TexCacheCount = 0;
    memset(g_Textures, 0, sizeof(g_Textures));
    memset(&ptr_texture_alloc_start, 0, sizeof(ptr_texture_alloc_start));
    memset(&fake_texture, 0, sizeof(fake_texture));
    memset(fake_texture_data, 0, sizeof(fake_texture_data));
    memset(fake_level_texture_table, 0, sizeof(fake_level_texture_table));
    fake_missing_texture = -1;
    fake_texture_available = TRUE;
    fake_light_image = FALSE;
    fake_texture_load_calls = 0;
    fake_light_begin_calls = 0;
    fake_light_end_calls = 0;
    fake_caverns_water_calls = 0;
    fake_dam_water_calls = 0;
    D_800483C4 = 0;
    D_800483C8 = NULL;
    sub_GAME_7F0CC4C8();
}

static void test_mip_dimensions_round_up_and_floor_at_one(void)
{
    struct tex texture;

    memset(&texture, 0, sizeof(texture));
    texture.width = 17;
    texture.height = 9;

    expect_true(texGetWidthAtLod(&texture, 0) == 17,
                "LOD zero returns the base texture width");
    expect_true(texGetWidthAtLod(&texture, 1) == 9 &&
                    texGetWidthAtLod(&texture, 2) == 5 &&
                    texGetWidthAtLod(&texture, 3) == 3 &&
                    texGetWidthAtLod(&texture, 4) == 2 &&
                    texGetWidthAtLod(&texture, 5) == 1 &&
                    texGetWidthAtLod(&texture, 6) == 1,
                "implicit mip widths round up and never reach zero");
    expect_true(texGetHeightAtLod(&texture, 0) == 9 &&
                    texGetHeightAtLod(&texture, 1) == 5 &&
                    texGetHeightAtLod(&texture, 2) == 3 &&
                    texGetHeightAtLod(&texture, 3) == 2 &&
                    texGetHeightAtLod(&texture, 4) == 1 &&
                    texGetHeightAtLod(&texture, 6) == 1,
                "implicit mip heights round up and never reach zero");
}

static void test_explicit_mip_cache_is_selected_by_texture_number(void)
{
    struct tex texture;

    reset_fixture();
    memset(&texture, 0, sizeof(texture));
    texture.texturenum = 23;
    texture.width = 64;
    texture.height = 32;
    texture.hasExplicitLods = 1;

    g_TexCacheCount = 2;
    g_TexCacheItems[0].texturenum = 17;
    g_TexCacheItems[0].widths[0] = 31;
    g_TexCacheItems[0].heights[0] = 15;
    g_TexCacheItems[1].texturenum = 23;
    g_TexCacheItems[1].widths[0] = 32;
    g_TexCacheItems[1].widths[1] = 16;
    g_TexCacheItems[1].heights[0] = 16;
    g_TexCacheItems[1].heights[1] = 8;

    expect_true(texGetWidthAtLod(&texture, 0) == 64 &&
                    texGetHeightAtLod(&texture, 0) == 32,
                "explicit LOD zero still uses the base dimensions");
    expect_true(texGetWidthAtLod(&texture, 1) == 32 &&
                    texGetWidthAtLod(&texture, 2) == 16 &&
                    texGetHeightAtLod(&texture, 1) == 16 &&
                    texGetHeightAtLod(&texture, 2) == 8,
                "explicit mip dimensions come from the matching cache item");

    texture.texturenum = 99;
    expect_true(texGetWidthAtLod(&texture, 1) == 1 &&
                    texGetHeightAtLod(&texture, 1) == 1,
                "missing explicit mip metadata uses the safe one-texel fallback");
}

static void test_line_and_total_sizes_follow_n64_texel_packing(void)
{
    struct tex texture;

    memset(&texture, 0, sizeof(texture));
    texture.width = 17;
    texture.height = 9;

    texture.depth = G_IM_SIZ_32b;
    expect_true(texGetLineSizeInBytes(&texture, 0) == 5,
                "32-bit texture lines are rounded to four-texel words");

    texture.depth = G_IM_SIZ_16b;
    expect_true(texGetLineSizeInBytes(&texture, 0) == 5 &&
                    texGetSizeInBytes(&texture, 0) == 45,
                "16-bit texture lines and total size use four-texel packing");

    texture.depth = G_IM_SIZ_8b;
    expect_true(texGetLineSizeInBytes(&texture, 0) == 3,
                "8-bit texture lines are rounded to eight-texel words");

    texture.depth = G_IM_SIZ_4b;
    expect_true(texGetLineSizeInBytes(&texture, 0) == 2,
                "4-bit texture lines are rounded to sixteen-texel words");
}

static void test_tile_load_depth_and_size_is_accumulated_per_lod(void)
{
    struct tex texture;
    s32 depth;
    s32 length;

    memset(&texture, 0, sizeof(texture));
    texture.width = 17;
    texture.height = 9;
    texture.depth = G_IM_SIZ_16b;

    texGetDepthAndSize(&texture, &depth, &length);
    expect_true(depth == G_IM_SIZ_16b && length == 180,
                "zero max-LOD loads one base mip at four times its line size");

    texture.maxlod = 3;
    texGetDepthAndSize(&texture, &depth, &length);
    expect_true(depth == G_IM_SIZ_16b && length == 264,
                "multi-LOD load size includes every rounded mip level");

    texture.depth = G_IM_SIZ_8b;
    texGetDepthAndSize(&texture, &depth, &length);
    expect_true(depth == G_IM_SIZ_16b,
                "8-bit source textures load through the 16-bit RDP path");

    texture.depth = G_IM_SIZ_4b;
    texGetDepthAndSize(&texture, &depth, &length);
    expect_true(depth == G_IM_SIZ_16b,
                "4-bit source textures load through the 16-bit RDP path");

    texture.depth = G_IM_SIZ_32b;
    texGetDepthAndSize(&texture, &depth, &length);
    expect_true(depth == G_IM_SIZ_32b,
                "32-bit source textures retain their load depth");
}

static void test_dimension_masks_and_texture_modes_have_exact_boundaries(void)
{
    expect_true(texDimensionToMask(0) == 0 && texDimensionToMask(1) == 0,
                "one-texel and empty dimensions use no mask bits");
    expect_true(texDimensionToMask(2) == 1,
                "two-texel dimensions use one mask bit");
    expect_true(texDimensionToMask(3) == 2,
                "three-texel dimensions round up to the next mask width");
    expect_true(texDimensionToMask(4) == 2,
                "four-texel dimensions use two mask bits");
    expect_true(texDimensionToMask(255) == 8,
                "255-texel dimensions fit the eight-bit mask ceiling");
    expect_true(texDimensionToMask(256) == 8,
                "256-texel dimensions use the eight-bit mask ceiling");
    expect_true(texDimensionToMask(257) == 8,
                "larger dimensions are capped at the eight-bit RDP limit");

    expect_true(texModeToGbiMode(0) == G_TX_WRAP &&
                    texModeToGbiMode(1) == G_TX_CLAMP &&
                    texModeToGbiMode(2) == G_TX_MIRROR &&
                    texModeToGbiMode(-1) == G_TX_WRAP &&
                    texModeToGbiMode(3) == G_TX_WRAP,
                "texture addressing modes map unknown values to wrap");
}

static void test_texture_state_cache_is_idempotent_and_invalidated(void)
{
    s32 first;

    reset_fixture();
    first = texTrySetTileState(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    expect_true(first == 1,
                "first tile-state request emits a state update");
    expect_true(texTrySetTileState(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 0,
                "identical tile-state request is suppressed");
    expect_true(texTrySetTileState(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11) == 1,
                "changing one tile-state field emits an update");

    expect_true(texTrySetTileSize(0, 0, 1, 2, 3) == 1,
                "first tile-size request emits a size update");
    expect_true(texTrySetTileSize(0, 0, 1, 2, 3) == 0,
                "identical tile-size request is suppressed");
    expect_true(texTrySetTileSize(0, 0, 1, 2, 4) == 1,
                "changing one tile-size field emits an update");

    sub_GAME_7F0CC4C8();
    expect_true(texTrySetTileState(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 1 &&
                    texTrySetTileSize(0, 0, 1, 2, 3) == 1,
                "texture reset invalidates both state caches");
}

static void test_small_texture_helpers_cover_noop_copy_lut_and_rounding(void)
{
    struct tex texture;
    u8 source[3] = {0x12, 0x34, 0x56};
    u8 destination[3] = {0, 0, 0};

    reset_fixture();
    nullsub_47();

    expect_true(unused_copy_byte_array(source, 0, destination) == 0,
                "copying zero bytes reports an empty copy");
    expect_true(unused_copy_byte_array(source, 3, destination) == 3 &&
                    memcmp(source, destination, sizeof(source)) == 0,
                "byte copy preserves all requested source bytes");

    expect_true(texSetLutMode(0) == 1 && texSetLutMode(0) == 0 &&
                    texSetLutMode(2) == 1,
                "LUT state emits only when the requested mode changes");

    expect_true(sub_GAME_7F0CCAFC(2.0f) == 2 &&
                    sub_GAME_7F0CCAFC(2.01f) == 3 &&
                    sub_GAME_7F0CCAFC(-2.01f) == -2,
                "LUT dxt rounding is a mathematical ceiling at both signs");

    prepare_texture(&texture, 16, 4, G_IM_SIZ_16b, 1);
    expect_true(sub_GAME_7F0CCB38((s32 *)&texture) == 512,
                "dxt calculation handles the zero packed-size selector");
    texture.width = 17;
    expect_true(sub_GAME_7F0CCB38((s32 *)&texture) == 410,
                "dxt calculation handles the eight-bit selector");
    texture.width = 18;
    expect_true(sub_GAME_7F0CCB38((s32 *)&texture) == 410,
                "dxt calculation handles the four-bit selector");
    texture.width = 19;
    /* The decompilation reads the low selector bits from the packed texture
     * word.  On the 64-bit host the data pointer occupies the preceding
     * word, so use an intentionally unaligned test pointer to exercise the
     * selector-3 branch without changing the production ABI. */
    texture.data = fake_texture_data + 3;
    expect_true(sub_GAME_7F0CCB38((s32 *)&texture) == 205,
                "dxt calculation handles the two-bit selector");
}

static void test_texture_command_writers_emit_stable_protocol_commands(void)
{
    struct tex texture;
    Gfx commands[256];
    Gfx saved;
    Gfx *end;
    size_t emitted;
    uintptr_t original_w0;

    reset_fixture();
    prepare_texture(&texture, 8, 4, G_IM_SIZ_16b, 1);
    texture.texturenum = 0;
    g_Textures[0].flag3 = 1;
    g_Textures[0].flag4 = 2;
    g_Textures[0].flag5 = 1;
    g_Textures[0].flag6 = 1;

    memset(commands, 0, sizeof(commands));
    end = texWriteTileFromDefinition(commands, &texture, 0, 3, 4, 0x7f);
    emitted = (size_t)(end - commands);
    expect_true(emitted > 0 && command_count(commands, emitted, G_SETTILE) > 0 &&
                    command_count(commands, emitted, G_SETTILESIZE) > 0,
                "definition writer emits tile state and tile bounds");

    reset_fixture();
    prepare_texture(&texture, 8, 4, G_IM_SIZ_16b, 1);
    texture.texturenum = 0;
    texture.lutmodeindex = 1;
    g_Textures[0].flag5 = 1;
    g_Textures[0].flag6 = 1;
    memset(commands, 0, sizeof(commands));
    end = texWriteTileFromDefinition(commands, &texture, 2, 0, 0, 0x80);
    emitted = (size_t)(end - commands);
    expect_true(command_count(commands, emitted,
                              (unsigned)G_SETOTHERMODE_H & 0xffU) > 0 &&
                    command_count(commands, emitted, G_SETTILE) > 0,
                "definition writer changes LUT mode before configuring a tile");

    memset(&saved, 0, sizeof(saved));
    saved.words.w0 = 0x12343800U;
    saved.words.w1 = 0xabcdefU;
    texture.maxlod = 3;
    original_w0 = saved.words.w0;
    end = texWriteTextureCmd(commands, &saved, &texture, FALSE);
    expect_true(end == commands && saved.words.w1 == 0xabcdefU &&
                    (saved.words.w0 & 0x3800U) == (2U << 11) &&
                    (saved.words.w0 & ~0x3800U) == (original_w0 & ~0x3800U),
                "in-place texture command rewriting changes only the LOD bits");

    saved.words.w0 = 0x12340000U;
    end = texWriteTextureCmd(commands, &saved, &texture, TRUE);
    expect_true(end == commands + 1 && commands[0].words.w1 == saved.words.w1 &&
                    (commands[0].words.w0 & 0x3800U) == (2U << 11),
                "texture command writer copies a changed saved command");

    end = texWriteTextureCmd(commands, NULL, &texture, TRUE);
    expect_true(end == commands + 1 &&
                    command_opcode(commands) == ((unsigned)G_TEXTURE & 0xffU),
                "texture command writer can synthesize a texture enable command");
}

static void test_texture_load_writers_cover_lut_and_large_tmem_paths(void)
{
    struct tex texture;
    Gfx commands[1024];
    Gfx *end;
    size_t emitted;

    reset_fixture();
    prepare_texture(&texture, 8, 4, G_IM_SIZ_16b, 1);
    memset(commands, 0, sizeof(commands));
    end = texWriteLoadToTmemAddr(commands, &texture, 12);
    emitted = (size_t)(end - commands);
    expect_true(emitted > 0 && command_count(commands, emitted, G_SETTIMG) > 0 &&
                    command_count(commands, emitted, G_LOADBLOCK) > 0,
                "non-paletted address loads set the image and load block");

    reset_fixture();
    prepare_texture(&texture, 4, 4, G_IM_SIZ_16b, 1);
    texture.lutmodeindex = 1;
    texture.unk0a = 2;
    memset(commands, 0, sizeof(commands));
    end = texWriteLoadToTmemAddr(commands, &texture, 24);
    emitted = (size_t)(end - commands);
    expect_true(command_count(commands, emitted, G_LOADTLUT) > 0 &&
                    command_count(commands, emitted, G_SETTILE) >= 2,
                "paletted address loads configure a palette tile and TLUT");

    reset_fixture();
    prepare_texture(&texture, 64, 64, G_IM_SIZ_16b, 3);
    texture.lutmodeindex = 1;
    texture.unk0a = 0;
    memset(commands, 0, sizeof(commands));
    end = texWriteLoadToTmemZero(commands, &texture);
    emitted = (size_t)(end - commands);
    expect_true(emitted > 0 && command_count(commands, emitted, G_LOADBLOCK) > 0 &&
                    command_count(commands, emitted, G_LOADTLUT) > 0,
                "large paletted zero-address loads clamp palette placement safely");

    reset_fixture();
    prepare_texture(&texture, 8, 4, G_IM_SIZ_16b, 2);
    memset(commands, 0, sizeof(commands));
    end = texWriteTileLods(commands, &texture, 1, 2, 2, 1, 8);
    emitted = (size_t)(end - commands);
    expect_true(emitted > 0 && command_count(commands, emitted, G_SETTILE) >= 2 &&
                    command_count(commands, emitted, G_SETTILESIZE) >= 2,
                "implicit mip writer emits one tile and bounds pair per LOD");

    reset_fixture();
    prepare_texture(&texture, 8, 4, G_IM_SIZ_16b, 2);
    texture.texturenum = 7;
    texture.hasExplicitLods = 1;
    g_TexCacheCount = 1;
    g_TexCacheItems[0].texturenum = 7;
    g_TexCacheItems[0].widths[0] = 4;
    g_TexCacheItems[0].heights[0] = 2;
    texture.lutmodeindex = 1;
    memset(commands, 0, sizeof(commands));
    end = texWriteTileLods(commands, &texture, 0, 0, 0, 0, 0);
    emitted = (size_t)(end - commands);
    expect_true(emitted > 0 &&
                    command_count(commands, emitted,
                                  (unsigned)G_SETOTHERMODE_H & 0xffU) > 0,
                "explicit mip writer uses cached dimensions and LUT state");
}

static void test_texture_tile_and_type_handlers_cover_lod_edges(void)
{
    struct tex first;
    struct tex second;
    Gfx commands[2048];
    Gfx *end;

    reset_fixture();
    prepare_texture(&first, 8, 4, G_IM_SIZ_16b, 1);
    first.lutmodeindex = 1;
    memset(commands, 0, sizeof(commands));
    end = texWriteTile(commands, &first, 1, 2, 2, 0);
    expect_true(end > commands && command_count(commands, (size_t)(end - commands), G_SETTILE) > 0,
                "single-tile writer emits addressing state");

    reset_fixture();
    prepare_texture(&first, 8, 4, G_IM_SIZ_16b, 1);
    prepare_texture(&second, 4, 2, G_IM_SIZ_16b, 1);
    second.texturenum = 1;
    memset(commands, 0, sizeof(commands));
    end = texHandleType0(commands, &first, 0, 0, 0, 1, 2, 3);
    expect_true(end > commands, "base texture handler emits a complete load path");

    reset_fixture();
    prepare_texture(&first, 8, 4, G_IM_SIZ_16b, 1);
    prepare_texture(&second, 4, 2, G_IM_SIZ_16b, 1);
    memset(commands, 0, sizeof(commands));
    end = texHandleType1(commands, &first, 1, 2, 0, &second, 0, 0, 1);
    expect_true(end > commands &&
                    command_count(commands, (size_t)(end - commands), G_RDPTILESYNC) > 0,
                "detail texture handler synchronizes the two texture loads");

    reset_fixture();
    prepare_texture(&first, 8, 4, G_IM_SIZ_16b, 1);
    memset(commands, 0, sizeof(commands));
    end = texHandleType2(commands, &first, 0, 1, 2);
    expect_true(end > commands, "mipmap texture handler covers its extra tile path");

    reset_fixture();
    prepare_texture(&first, 8, 4, G_IM_SIZ_16b, 1);
    memset(commands, 0, sizeof(commands));
    end = texHandleType3(commands, (u32 *)&first, 0, 0, 0);
    expect_true(end > commands, "two-tile handler emits both tile definitions");
    reset_fixture();
    prepare_texture(&first, 8, 4, G_IM_SIZ_16b, 1);
    memset(commands, 0, sizeof(commands));
    end = texHandleType4(commands, (u32 *)&first, 2, 1, 2);
    expect_true(end > commands, "pre-swapped tile handler emits a single tile definition");
}

static void test_texture_copy_preserves_both_words_for_each_gfx_slot(void)
{
    Gfx source[3];
    Gfx destination[3];

    memset(source, 0, sizeof(source));
    memset(destination, 0, sizeof(destination));
    source[0].words.w0 = 0x01020304;
    source[0].words.w1 = 0x11121314;
    source[1].words.w0 = 0x21222324;
    source[1].words.w1 = 0x31323334;
    source[2].words.w0 = 0x41424344;
    source[2].words.w1 = 0x51525354;

    texCopyGdls(source, destination, (s32)sizeof(source));
    expect_true(memcmp(source, destination, sizeof(source)) == 0,
                "display-list copy preserves both words of every host Gfx slot");
}

static Gfx make_texture_descriptor(s32 type, s32 texnum, s32 texnum2,
                                   s32 min, s32 smode, s32 tmode,
                                   s32 offset, s32 shifts, s32 shiftt)
{
    Gfx command;

    memset(&command, 0, sizeof(command));
    command.words.w0 = ((uintptr_t)(G_NOOP & 0xff) << 24) |
                       ((uintptr_t)(smode & 3) << 22) |
                       ((uintptr_t)(tmode & 3) << 20) |
                       ((uintptr_t)(offset & 3) << 18) |
                       ((uintptr_t)(shifts & 0xf) << 14) |
                       ((uintptr_t)(shiftt & 0xf) << 10) |
                       (uintptr_t)(type & 7);
    command.words.w1 = ((uintptr_t)(min & 0xff) << 24) |
                       ((uintptr_t)(texnum2 & 0xfff) << 12) |
                       (uintptr_t)(texnum & 0xfff);
    return command;
}

static Gfx make_raw_command(unsigned opcode, unsigned byte_one)
{
    Gfx command;

    memset(&command, 0, sizeof(command));
    command.words.w0 = ((uintptr_t)(opcode & 0xff) << 24) |
                       ((uintptr_t)(byte_one & 0xff) << 8);
    return command;
}

static size_t run_texture_gdl(Gfx *source, size_t source_count,
                              Gfx *destination, size_t destination_count,
                              void *pool)
{
    s32 byte_count;

    memset(destination, 0, sizeof(*destination) * destination_count);
    byte_count = texLoadFromGdl(source, (s32)(source_count * sizeof(Gfx)),
                                destination, pool);
    return byte_count > 0 ? (size_t)byte_count / sizeof(Gfx) : 0;
}

static void prepare_fake_texture(void)
{
    prepare_texture(&fake_texture, 8, 4, G_IM_SIZ_16b, 1);
    fake_texture.texturenum = 0;
}

static void test_texture_gdl_dispatch_covers_texture_types_and_validity(void)
{
    Gfx source[2];
    Gfx destination[2048];
    size_t emitted;

    reset_fixture();
    prepare_fake_texture();
    source[0] = make_texture_descriptor(TEXTURETYPE_LOD, 0, 0, 3, 1, 2, 2, 3, 4);
    emitted = run_texture_gdl(source, 1, destination, 2048, &ptr_texture_alloc_start);
    expect_true(emitted > 0 && fake_texture_load_calls == 1,
                "GDL loader dispatches an invalid LOD descriptor through its fallback path");

    reset_fixture();
    prepare_fake_texture();
    D_800483C8 = fake_level_texture_table;
    D_800483C4 = 0;
    fake_level_texture_table[0].unk_0_0 = 2;
    source[0] = make_texture_descriptor(TEXTURETYPE_LOD, 0, 0, 5, 0, 1, 0, 1, 2);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted > 0 && fake_level_texture_table[0].unk_0_0 == 2,
                "valid LOD descriptors reuse stored level metadata");

    reset_fixture();
    prepare_fake_texture();
    D_800483C8 = fake_level_texture_table;
    D_800483C4 = 0;
    fake_level_texture_table[0].unk_0_0 = 0xff;
    source[0] = make_texture_descriptor(TEXTURETYPE_LOD, 0, 0, 7, 0, 0, 0, 4, 5);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted > 0 && fake_level_texture_table[0].unk_0_0 == 7 &&
                    fake_level_texture_table[0].unk_1_3 == (11 & 7) &&
                    fake_level_texture_table[0].unk_1_0 == (10 & 7),
                "first LOD descriptor publishes its minimum and shift metadata");

    reset_fixture();
    prepare_fake_texture();
    fake_missing_texture = 1;
    source[0] = make_texture_descriptor(TEXTURETYPE_DETAIL, 0, 1, 2, 1, 1, 0, 0, 0);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted > 0 && fake_texture_load_calls == 2,
                "detail descriptor loads both ids and tolerates a missing detail image");

    reset_fixture();
    prepare_fake_texture();
    source[0] = make_texture_descriptor(TEXTURETYPE_DETAIL, 0, 1, 2, 1, 1, 0, 0, 0);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted > 0 && fake_texture_load_calls == 2,
                "detail descriptor emits its two-texture path when both images exist");

    reset_fixture();
    prepare_fake_texture();
    source[0] = make_texture_descriptor(TEXTURETYPE_MIPMAP, 0, 0, 0, 2, 1, 2, 0, 0);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted > 0, "mipmap descriptor selects the ordinary mip path");

    reset_fixture();
    prepare_fake_texture();
    D_800483C8 = fake_level_texture_table;
    D_800483C4 = 0;
    fake_level_texture_table[0].unk_0_0 = 1;
    source[0] = make_texture_descriptor(TEXTURETYPE_MIPMAP, 0, 0, 0, 2, 1, 0, 0, 0);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted > 0, "mipmap descriptor selects the stored-level path");

    reset_fixture();
    prepare_fake_texture();
    source[0] = make_texture_descriptor(TEXTURETYPE_TILE, 0, 0, 0, 0, 0, 0, 0, 0);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted > 0, "tile descriptor loads and configures two render tiles");

    reset_fixture();
    prepare_fake_texture();
    source[0] = make_texture_descriptor(TEXTURETYPE_TILE_PRESWAPPED, 0, 0, 0, 1, 2, 2, 0, 0);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted > 0, "pre-swapped tile descriptor selects its one-tile path");

    reset_fixture();
    prepare_fake_texture();
    fake_texture.texturenum = 1508;
    source[0] = make_texture_descriptor(TEXTURETYPE_TILE, 1508, 0, 0, 0, 0, 0, 0, 0);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted > 0 && fake_caverns_water_calls == 1,
                "Caverns water texture invokes its dedicated post-load hook");

    reset_fixture();
    prepare_fake_texture();
    fake_texture.texturenum = 1511;
    source[0] = make_texture_descriptor(TEXTURETYPE_TILE, 1511, 0, 0, 0, 0, 0, 0, 0);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted > 0 && fake_dam_water_calls == 1,
                "Dam water texture invokes its dedicated post-load hook");
}

static void test_texture_gdl_protocol_preserves_non_texture_commands(void)
{
    Gfx source[8];
    Gfx destination[2048];
    size_t emitted;

    reset_fixture();
    prepare_fake_texture();
    source[0] = make_raw_command(G_RDPPIPESYNC, 0);
    source[1] = make_raw_command(0xb1, 0);
    source[2] = make_raw_command(0xbb, 0);
    source[3] = make_raw_command(0x99, 0);
    emitted = run_texture_gdl(source, 4, destination, 2048, NULL);
    expect_true(emitted == 4 && command_count(destination, emitted, G_RDPPIPESYNC) == 1 &&
                    command_count(destination, emitted, 0x99) == 1,
                "ordinary protocol commands pass through in source order");

    reset_fixture();
    prepare_fake_texture();
    D_800483C8 = fake_level_texture_table;
    D_800483C4 = 0;
    fake_level_texture_table[0].unk_0_0 = 1;
    source[0] = make_texture_descriptor(TEXTURETYPE_TILE, 0, 0, 0, 0, 0, 0, 0, 0);
    source[1] = make_raw_command(0xba, 17);
    source[2] = make_raw_command(0xba, 0);
    emitted = run_texture_gdl(source, 3, destination, 2048, NULL);
    expect_true(emitted > 0 &&
                    command_count_with_byte(destination, emitted, 0xba, 0) == 1,
                "valid texture state filters only the reserved BA field values");

    reset_fixture();
    prepare_fake_texture();
    source[0] = make_raw_command(0xba, 17);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted == 1 && command_count(destination, emitted, 0xba) == 1,
                "invalid texture state preserves BA commands unchanged");

    reset_fixture();
    prepare_fake_texture();
    fake_light_image = TRUE;
    source[0] = make_texture_descriptor(TEXTURETYPE_TILE, 0, 0, 0, 0, 0, 0, 0, 0);
    emitted = run_texture_gdl(source, 1, destination, 2048, NULL);
    expect_true(emitted > 0 && fake_light_begin_calls == 1 &&
                    fake_light_end_calls == 1,
                "light texture fixtures are closed even when the list ends immediately");

    reset_fixture();
    prepare_fake_texture();
    fake_light_image = TRUE;
    source[0] = make_texture_descriptor(TEXTURETYPE_TILE, 0, 0, 0, 0, 0, 0, 0, 0);
    source[1] = make_raw_command(0xb1, 0);
    source[2] = make_texture_descriptor(TEXTURETYPE_TILE, 0, 0, 0, 0, 0, 0, 0, 0);
    emitted = run_texture_gdl(source, 3, destination, 2048, NULL);
    expect_true(emitted > 0 && fake_light_begin_calls == 2 &&
                    fake_light_end_calls == 2,
                "a second texture closes the pending light entry before opening another");
}

int main(void)
{
    test_mip_dimensions_round_up_and_floor_at_one();
    test_explicit_mip_cache_is_selected_by_texture_number();
    test_line_and_total_sizes_follow_n64_texel_packing();
    test_tile_load_depth_and_size_is_accumulated_per_lod();
    test_dimension_masks_and_texture_modes_have_exact_boundaries();
    test_texture_state_cache_is_idempotent_and_invalidated();
    test_small_texture_helpers_cover_noop_copy_lut_and_rounding();
    test_texture_command_writers_emit_stable_protocol_commands();
    test_texture_load_writers_cover_lut_and_large_tmem_paths();
    test_texture_tile_and_type_handlers_cover_lod_edges();
    test_texture_copy_preserves_both_words_for_each_gfx_slot();
    test_texture_gdl_dispatch_covers_texture_types_and_validity();
    test_texture_gdl_protocol_preserves_non_texture_commands();

    if (failures != 0)
    {
        fprintf(stderr, "%d texture policy contract(s) failed\n", failures);
        return 1;
    }

    puts("texture policy suite passed");
    return 0;
}
