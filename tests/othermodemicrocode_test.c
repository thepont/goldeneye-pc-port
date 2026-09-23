#include <stdio.h>
#include <fenv.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>

#include <ultra64.h>
#include "image.h"
#include "tex.h"

static int failures;
static void expect(int condition, const char *message);

static struct tex fake_texture;
static int fake_texture_available = 1;

void texLoad(s32 *updateword, struct texpool *pool)
{
    (void)updateword;
    (void)pool;
}

struct tex *texFindInPool(s32 texturenum, struct texpool *pool)
{
    (void)texturenum;
    (void)pool;
    return fake_texture_available ? &fake_texture : NULL;
}

void texGetDepthAndSize(struct tex *tex, s32 *deptharg, s32 *lenarg)
{
    (void)tex;
    *deptharg = G_IM_SIZ_16b;
    *lenarg = 32;
}

s32 texGetWidthAtLod(struct tex *tex, s32 lod)
{
    s32 width = tex->width;
    while (lod-- > 0 && width >= 2)
    {
        width >>= 1;
    }
    return width;
}

s32 texGetHeightAtLod(struct tex *tex, s32 lod)
{
    s32 height = tex->height;
    while (lod-- > 0 && height >= 2)
    {
        height >>= 1;
    }
    return height;
}

s32 is_less_than_certain_power_of_2(int number);
s32 ceil8000(s32 width);
s32 ceil4000(s32 width);
s32 ceil2000(s32 width);
s32 ceil1000(s32 width);
s32 sub_GAME_7F0767D8(s32 width, s32 height, s32 lod);
s32 sub_GAME_7F076848(s32 width, s32 height, s32 lod);
s32 sub_GAME_7F0768B8(s32 width, s32 height, s32 lod);
s32 sub_GAME_7F076928(s32 width, s32 height, s32 lod);
void texSetRenderMode(Gfx **gdlptr, s32 mode, s32 numcycles, s32 zbufferMode);

static void test_render_mode_matrix_covers_cycle_and_depth_variants(void)
{
    static const s32 modes[] = {0, 1, 2, 3, 4, 5};
    Gfx commands[8];
    size_t mode_index;
    s32 numcycles;
    s32 zbuffer_mode;

    for (numcycles = 1; numcycles <= 2; ++numcycles)
    {
        for (zbuffer_mode = 0; zbuffer_mode <= 2; ++zbuffer_mode)
        {
            for (mode_index = 0;
                 mode_index < sizeof(modes) / sizeof(modes[0]);
                 ++mode_index)
            {
                Gfx *cursor = commands;

                memset(commands, 0, sizeof(commands));
                texSetRenderMode(&cursor, modes[mode_index], numcycles,
                                 zbuffer_mode);
                expect(cursor == commands + 3,
                       "render-mode policy emits sync, cycle, and render commands");
            }
        }
    }
}

static void test_null_texture_selection_still_configures_render_state(void)
{
    Gfx commands[32];
    Gfx *cursor;

    memset(commands, 0, sizeof(commands));
    cursor = commands;
    texSelect(&cursor, NULL, 1, 1, 0);
    expect(cursor > commands,
           "null texture selection emits a one-cycle shaded fallback");

    memset(commands, 0, sizeof(commands));
    cursor = commands;
    texSelect(&cursor, NULL, 2, 2, 0);
    expect(cursor > commands,
           "null texture selection emits a two-cycle depth-aware fallback");
}

static void *map_legacy_texture_words(void)
{
    void *mapping;

#if defined(MAP_32BIT)
    mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
#else
    mapping = MAP_FAILED;
#endif
    if (mapping == MAP_FAILED || (uintptr_t)mapping > UINT32_MAX)
    {
        if (mapping != MAP_FAILED)
        {
            munmap(mapping, 4096);
        }
        return NULL;
    }

    memset(mapping, 0, 4096);
    return mapping;
}

static void run_texture_selection_case(void *legacy_mapping, s32 level,
                                       u8 format, u8 depth,
                                       s32 explicit_lods, s32 arg3,
                                       s32 texture_available)
{
    struct sImageTableEntry entry;
    Gfx commands[128];
    Gfx *cursor;
    u16 *image_words = (u16 *)legacy_mapping;

    image_words[0] = 0;
    memset(&entry, 0, sizeof(entry));
    entry.index = (u32)(uintptr_t)(image_words + 4);
    entry.width = 16;
    entry.height = 8;
    entry.level = (u8)level;
    entry.format = format;
    entry.depth = depth;
    entry.flagsS = G_TX_WRAP;
    entry.flagsT = G_TX_CLAMP;

    memset(&fake_texture, 0, sizeof(fake_texture));
    fake_texture.width = entry.width;
    fake_texture.height = entry.height;
    fake_texture.gbiformat = format;
    fake_texture.depth = depth;
    fake_texture.maxlod = (u8)(level > 0 ? level : 1);
    fake_texture.hasExplicitLods = explicit_lods != 0;
    fake_texture.lutmodeindex = format == G_IM_FMT_CI ? 2 : 0;
    fake_texture.unk0a = 1;
    fake_texture_available = texture_available;

    memset(commands, 0, sizeof(commands));
    cursor = commands;
    texSelect(&cursor, &entry, 1, arg3, 2);
    expect(cursor > commands,
           "texture selection emits commands for every supported format and LOD");
}

static void test_texture_selection_covers_formats_lods_and_missing_images(void)
{
    void *legacy_mapping = map_legacy_texture_words();

    expect(legacy_mapping != NULL,
           "texture selection fixture obtains a 32-bit-compatible image address");
    if (legacy_mapping == NULL)
    {
        return;
    }

    run_texture_selection_case(legacy_mapping, 0, G_IM_FMT_RGBA,
                               G_IM_SIZ_32b, FALSE, 1, TRUE);
    run_texture_selection_case(legacy_mapping, 0, G_IM_FMT_IA,
                               G_IM_SIZ_16b, FALSE, 2, TRUE);
    run_texture_selection_case(legacy_mapping, 0, G_IM_FMT_I,
                               G_IM_SIZ_8b, FALSE, 1, TRUE);
    run_texture_selection_case(legacy_mapping, 0, G_IM_FMT_CI,
                               G_IM_SIZ_4b, FALSE, 2, TRUE);
    run_texture_selection_case(legacy_mapping, 0, G_IM_FMT_CI,
                               G_IM_SIZ_8b, FALSE, 1, TRUE);

    run_texture_selection_case(legacy_mapping, 3, G_IM_FMT_RGBA,
                               G_IM_SIZ_32b, FALSE, 1, TRUE);
    run_texture_selection_case(legacy_mapping, 3, G_IM_FMT_IA,
                               G_IM_SIZ_16b, FALSE, 2, TRUE);
    run_texture_selection_case(legacy_mapping, 3, G_IM_FMT_I,
                               G_IM_SIZ_8b, FALSE, 1, TRUE);
    run_texture_selection_case(legacy_mapping, 3, G_IM_FMT_CI,
                               G_IM_SIZ_4b, FALSE, 2, TRUE);
    run_texture_selection_case(legacy_mapping, 3, G_IM_FMT_RGBA,
                               G_IM_SIZ_16b, TRUE, 1, TRUE);

    run_texture_selection_case(legacy_mapping, 0, G_IM_FMT_RGBA,
                               G_IM_SIZ_16b, FALSE, 1, FALSE);

    munmap(legacy_mapping, 4096);
    fake_texture_available = 1;
}

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void test_texture_mask_exponents_have_exact_boundaries(void)
{
    expect(is_less_than_certain_power_of_2(-1) == 0,
           "negative texture dimensions use the no-mask fallback");
    expect(is_less_than_certain_power_of_2(2) == 1 &&
               is_less_than_certain_power_of_2(3) == 2 &&
               is_less_than_certain_power_of_2(4) == 2,
           "texture mask exponents change only at power-of-two boundaries");
    expect(is_less_than_certain_power_of_2(16) == 4 &&
               is_less_than_certain_power_of_2(17) == 5 &&
               is_less_than_certain_power_of_2(128) == 7 &&
               is_less_than_certain_power_of_2(129) == 8,
           "texture mask exponent upper boundaries remain inclusive-safe");
}

static void test_texture_scale_rounds_up_to_tile_alignment(void)
{
    expect(ceil8000(1) == 2048 && ceil8000(16) == 2048 &&
               ceil8000(17) == 1024,
           "8000 texture scale rounds widths to sixteen-texel tiles");
    expect(ceil4000(8) == 2048 && ceil4000(9) == 1024,
           "4000 texture scale rounds widths to eight-texel tiles");
    expect(ceil2000(4) == 2048 && ceil2000(5) == 1024,
           "2000 texture scale rounds widths to four-texel tiles");
    expect(ceil1000(4) == 1024 && ceil1000(5) == 512,
           "1000 texture scale rounds widths to four-texel tiles");
}

static void test_invalid_texture_widths_do_not_divide_by_zero(void)
{
    s32 (*scale_helpers[])(s32) = {ceil8000, ceil4000, ceil2000, ceil1000};
    size_t i;

    for (i = 0; i < sizeof(scale_helpers) / sizeof(scale_helpers[0]); ++i)
    {
        feclearexcept(FE_DIVBYZERO);
        expect(scale_helpers[i](0) == 0 &&
                   (fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO) == 0,
               "non-positive zero width is rejected without division by zero");

        feclearexcept(FE_DIVBYZERO);
        expect(scale_helpers[i](-1) == 0 &&
                   (fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO) == 0,
               "non-positive negative width is rejected without division by zero");
    }
}

static void test_texture_size_accumulators_clamp_lod_to_one_pass(void)
{
    expect(sub_GAME_7F0767D8(16, 2, 0) == 8,
           "8000 byte-size helper treats zero LOD as one pass");
    expect(sub_GAME_7F076848(8, 2, -1) == 8,
           "4000 byte-size helper treats negative LOD as one pass");
    expect(sub_GAME_7F0768B8(4, 2, 1) == 8 &&
               sub_GAME_7F076928(4, 2, 1) == 8,
           "four-texel byte-size helpers agree at one pass");
}

int main(void)
{
    test_render_mode_matrix_covers_cycle_and_depth_variants();
    test_null_texture_selection_still_configures_render_state();
    test_texture_selection_covers_formats_lods_and_missing_images();
    test_texture_mask_exponents_have_exact_boundaries();
    test_texture_scale_rounds_up_to_tile_alignment();
    test_invalid_texture_widths_do_not_divide_by_zero();
    test_texture_size_accumulators_clamp_lod_to_one_pass();

    if (failures != 0)
    {
        return 1;
    }

    puts("other-mode microcode suite passed");
    return 0;
}
