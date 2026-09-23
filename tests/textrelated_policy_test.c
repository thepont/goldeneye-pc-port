#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <ultra64.h>
#include "textrelated.h"
#include "game/language.h"

s32 j_text_trigger;

s16 viGetX(void)
{
    return 320;
}

s16 viGetY(void)
{
    return 240;
}

u32 osVirtualToPhysical(void *address)
{
    return (u32)(uintptr_t)address;
}

struct jpncharpixels *langGetJpnCharPixels(s32 codepoint)
{
    static struct jpncharpixels pixels;
    (void)codepoint;
    return &pixels;
}

static int failures;

/* textMeasure's vector route is disabled for this deterministic bitmap-font
 * contract.  Keeping the port seam fake makes the test independent of SDL or
 * a live OpenGL context. */
int gfx_vector_text_enabled(void)
{
    return 0;
}

int gfx_vector_text_measure(const char *text)
{
    (void)text;
    return 0;
}

int gfx_vector_text_hud_height(int fallback_height)
{
    return fallback_height;
}

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void make_font_fixture(struct fontchar *chars, struct font *font)
{
    s32 i;

    memset(chars, 0, sizeof(struct fontchar) * 94);
    memset(font, 0, sizeof(*font));
    for (i = 0; i < 94; ++i)
    {
        chars[i].index = i + 0x21;
        chars[i].baseline = 8;
        chars[i].height = 10;
        chars[i].width = 5;
        chars[i].kerningindex = 0;
    }
}

static void test_text_state_setters_are_explicit_and_reversible(void)
{
    extern s32 text_spacing;
    extern s32 text_orientation;
    extern s32 text_wordwrap;
    extern s32 overlap_correction;

    setTextSpacingInverted(3);
    setTextOrientation(ROT_90CW);
    setTextWordWrap(4);
    setTextOverlapCorrection(7);
    expect_true(text_spacing == -3 && text_orientation == ROT_90CW &&
                    text_wordwrap == 4 && overlap_correction == 7,
                "text state setters store spacing, orientation, indent, and correction");

    setTextSpacingInverted(0);
    setTextOrientation(ROT_NORMAL);
    setTextWordWrap(0);
    setTextOverlapCorrection(-1);
    expect_true(text_spacing == 0 && text_orientation == ROT_NORMAL &&
                    text_wordwrap == 0 && overlap_correction == -1,
                "text state setters can restore the baseline layout state");
}

static void test_text_measure_handles_lines_spacing_and_missing_banks(void)
{
    struct fontchar chars[94];
    struct font font;
    s32 height;
    s32 width;
    char one_line[] = "AB";
    char two_lines[] = "A\nB";

    make_font_fixture(chars, &font);
    j_text_trigger = 0;
    setTextSpacingInverted(0);
    textMeasure(&height, &width, one_line, chars, &font, 0);
    expect_true(width == 12 && height == 0,
                "bitmap text measurement sums character widths on one line");

    textMeasure(&height, &width, two_lines, chars, &font, 20);
    expect_true(width == 6 && height == 20,
                "bitmap text measurement returns the longest line and line step");

    height = 99;
    width = 99;
    textMeasure(&height, &width, NULL, chars, &font, 20);
    expect_true(width == 0 && height == 0,
                "missing text is rejected without dereferencing font banks");

    textMeasure(&height, &width, one_line, NULL, &font, 20);
    expect_true(width == 0 && height == 0,
                "missing character bank is rejected before measurement");
    textMeasure(&height, &width, one_line, chars, NULL, 20);
    expect_true(width == 0 && height == 0,
                "missing font metrics are rejected before measurement");
}

static void test_text_wrap_preserves_words_longer_than_legacy_scratch_buffer(void)
{
    struct fontchar chars[94];
    struct font font;
    char source[96];
    char destination[128];
    size_t i;

    make_font_fixture(chars, &font);
    for (i = 0; i < sizeof(source) - 1; ++i)
    {
        source[i] = 'A';
    }
    source[sizeof(source) - 1] = '\0';
    memset(destination, 0xcc, sizeof(destination));

    textWrap(1000, source, destination, chars, &font);
    expect_true(strcmp(destination, source) == 0,
                "text wrapping preserves a word longer than 32 bytes");
}

static void test_missing_render_text_is_a_noop(void)
{
    struct fontchar chars[94];
    struct font font;
    Gfx display_list[8];
    s32 x = 20;
    s32 y = 30;
    Gfx *start = display_list;

    make_font_fixture(chars, &font);
    expect_true(textRender(start, &x, &y, NULL, chars, &font,
                           0xffffffff, 320, 240, 0, 0) == start &&
                    x == 20 && y == 30,
                "missing plain text does not mutate the display list or layout");
    expect_true(textRenderOutlined(start, &x, &y, NULL, chars, &font,
                                   0xffffffff, 0, 320, 240, 0, 0) == start &&
                    x == 20 && y == 30,
                "missing outlined text does not mutate the display list or layout");

    x = 20;
    y = 30;
    expect_true(textRender(start, &x, &y, "A", NULL, NULL,
                           0xffffffff, 320, 240, 0, 0) == start &&
                    x == 20 && y == 30,
                "missing plain font tables do not dereference absent assets");
    expect_true(textRenderOutlined(start, &x, &y, "A", NULL, NULL,
                                   0xffffffff, 0, 320, 240, 0, 0) == start &&
                    x == 20 && y == 30,
                "missing outlined font tables do not dereference absent assets");
}

static void test_ascii_rendering_advances_layout_without_overlapping_glyphs(void)
{
    struct fontchar chars[94];
    struct font font;
    static u8 glyph_pixels[256];
    Gfx display_list[128];
    Gfx *end;
    s32 x = 10;
    s32 y = 10;
    char plain[] = "AB";
    char outlined[] = "A\nB";
    s32 i;

    make_font_fixture(chars, &font);
    memset(glyph_pixels, 0xff, sizeof(glyph_pixels));
    for (i = 0; i < 94; ++i)
    {
        chars[i].pixeldata = glyph_pixels;
    }

    setTextOrientation(ROT_NORMAL);
    end = textRender(display_list, &x, &y, plain, chars, &font,
                     0xffffffff, 320, 240, 0, 0);
    expect_true(end > display_list && x == 22 && y == 10,
                "plain ASCII rendering advances each glyph from the prior position");

    x = 10;
    y = 10;
    end = textRenderOutlined(display_list, &x, &y, outlined, chars, &font,
                             0xffffffff, 0x000000ff, 320, 240, 0, 0);
    expect_true(end > display_list && x == 16 && y == 28,
                "outlined newlines reset x and advance y exactly once");

    x = 10;
    y = 10;
    setTextOrientation(ROT_90CW);
    end = textRender(display_list, &x, &y, plain, chars, &font,
                     0xffffffff, 320, 240, 0, 0);
    expect_true(end > display_list && x == 22,
                "rotated rendering uses the same logical advance as normal text");
    setTextOrientation(ROT_NORMAL);
}

static void test_text_measure_and_wrap_cover_layout_boundaries(void)
{
    struct fontchar chars[94];
    struct font font;
    s32 height;
    s32 width;
    char trigger_text[] = "A\nB";
    char space_before_newline[] = "A \nB";
    char single_byte_multibyte[] = {(char)0x80, 0x01, '\0'};
    char double_byte_multibyte[] = {(char)0xc0, 0x01, '\0'};
    char source[] = "A B C";
    char destination[32];

    make_font_fixture(chars, &font);
    setTextSpacingInverted(0);
    j_text_trigger = 1;
    textMeasure(&height, &width, trigger_text, chars, &font, 5);
    expect_true(height == 14,
                "Japanese text trigger enforces the minimum line height");
    j_text_trigger = 0;

    textMeasure(&height, &width, space_before_newline, chars, &font, 20);
    expect_true(width == 6,
                "a space immediately before a newline does not widen the line");

    textMeasure(&height, &width, single_byte_multibyte, chars, &font, 0);
    expect_true(width == 12,
                "single-byte encoded multibyte glyphs use the narrow advance");
    textMeasure(&height, &width, double_byte_multibyte, chars, &font, 0);
    expect_true(width == 16,
                "double-byte encoded multibyte glyphs use the wide advance");

    memset(destination, 0, sizeof(destination));
    setTextWordWrap(2);
    textWrap(12, source, destination, chars, &font);
    expect_true(strcmp(destination, "A \n  B \n  C") == 0,
                "word wrapping inserts stable newline and indentation boundaries");
    setTextWordWrap(0);
}

int main(void)
{
    test_text_state_setters_are_explicit_and_reversible();
    test_text_measure_handles_lines_spacing_and_missing_banks();
    test_text_wrap_preserves_words_longer_than_legacy_scratch_buffer();
    test_missing_render_text_is_a_noop();
    test_ascii_rendering_advances_layout_without_overlapping_glyphs();
    test_text_measure_and_wrap_cover_layout_boundaries();

    if (failures != 0)
    {
        fprintf(stderr, "%d text-related contract(s) failed\n", failures);
        return 1;
    }

    puts("text-related suite passed");
    return 0;
}
