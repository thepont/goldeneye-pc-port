#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "model.h"

static int failures;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static u16 expected_angle(const u8 *bitstream, u8 width, u32 bit_offset)
{
    u32 value = 0;
    u8 i;

    for (i = 0; i < width; ++i)
    {
        u32 absolute_bit = bit_offset + i;
        u8 bit = (u8)((bitstream[absolute_bit / 8] >>
                       (7 - (absolute_bit % 8))) & 1);
        value = (value << 1) | bit;
    }

    return (u16)(value << (16 - width));
}

static void test_bit_reader_boundaries_and_crossings(void)
{
    static const u8 bitstream[] = {
        0x00, 0x01, 0x7e, 0x81, 0xa5, 0x3c, 0xff, 0x42,
        0x18, 0xc3, 0x5a, 0xe7, 0x96, 0x2d, 0xb4, 0x69
    };
    u8 width;

    for (width = 1; width <= 16; ++width)
    {
        u32 bit_offset;
        const u32 last_offset = (u32)(sizeof(bitstream) * 8u) - width;

        for (bit_offset = 0; bit_offset <= last_offset; ++bit_offset)
        {
            u16 actual = (u16)modelAnimReadBitsAsU16Angle(
                (u8 *)bitstream, width, bit_offset);
            u16 expected = expected_angle(bitstream, width, bit_offset);

            if (actual != expected)
            {
                fprintf(stderr,
                        "FAIL: bit reader width=%u offset=%u "
                        "expected 0x%04x, got 0x%04x\n",
                        width, bit_offset, expected, actual);
                failures++;
            }
        }
    }
}

static void test_animation_frame_clamping_and_wrapping(void)
{
    ModelAnimation anim;

    memset(&anim, 0, sizeof(anim));
    anim.unk04 = 10;

    expect_true(modelConstrainOrWrapAnimFrame(0, &anim, -1.0f) == 0,
                "non-looping animation keeps its first frame");
    expect_true(modelConstrainOrWrapAnimFrame(9, &anim, -1.0f) == 9,
                "non-looping animation keeps its last valid frame");
    expect_true(modelConstrainOrWrapAnimFrame(10, &anim, -1.0f) == 9,
                "non-looping animation clamps the frame-count boundary");
    expect_true(modelConstrainOrWrapAnimFrame(100, &anim, -1.0f) == 9,
                "non-looping animation clamps large positive frames");
    expect_true(modelConstrainOrWrapAnimFrame(-1, &anim, -1.0f) == 0,
                "non-looping animation clamps negative frames");

    anim.unk07 = 1;
    expect_true(modelConstrainOrWrapAnimFrame(10, &anim, -1.0f) == 0,
                "looping animation wraps at the frame-count boundary");
    expect_true(modelConstrainOrWrapAnimFrame(21, &anim, -1.0f) == 1,
                "looping animation wraps large positive frames");
    expect_true(modelConstrainOrWrapAnimFrame(-1, &anim, -1.0f) == 9,
                "looping animation wraps one frame before zero");
    expect_true(modelConstrainOrWrapAnimFrame(-9, &anim, -1.0f) == 1,
                "looping animation wraps negative frames across a cycle");

    expect_true(modelConstrainOrWrapAnimFrame(8, &anim, 3.0f) == 3,
                "end-frame constraint wins before looping at the end");
    expect_true(modelConstrainOrWrapAnimFrame(8, &anim, 3.25f) == 4,
                "fractional end-frame constraint uses ceiling semantics");
}

int main(void)
{
    test_bit_reader_boundaries_and_crossings();
    test_animation_frame_clamping_and_wrapping();

    if (failures != 0)
    {
        fprintf(stderr, "%d model animation contract(s) failed\n", failures);
        return 1;
    }

    puts("model animation suite passed");
    return 0;
}
