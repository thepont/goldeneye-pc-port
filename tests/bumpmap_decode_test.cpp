#include "../tools/mktex/src/bumpmap_decode.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <vector>

static void geTestRequire(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

#undef assert
#define assert(condition) geTestRequire((condition))

static void expect_rgba(const std::vector<uint8_t> &actual,
                        uint8_t red,
                        uint8_t green,
                        uint8_t blue,
                        uint8_t alpha)
{
    assert(actual.size() >= 4);
    assert(actual[0] == red);
    assert(actual[1] == green);
    assert(actual[2] == blue);
    assert(actual[3] == alpha);
}

static struct pd_image one_pixel(enum pd_format format,
                                 std::vector<uint8_t> &pixels)
{
    struct pd_image image = {};
    image.exists = true;
    image.format = format;
    image.width = 1;
    image.height = 1;
    image.pixels = pixels.data();
    return image;
}

int main()
{
    struct pd_tex texture = {};
    std::vector<uint8_t> output(8, 0);
    std::vector<uint8_t> pixels;

    assert(geBumpDecodeImageRgba(nullptr, &texture, output.data()) == 0);
    assert(geBumpDecodeImageRgba(nullptr, nullptr, output.data()) == 0);

    pixels = {1, 2, 3, 4};
    struct pd_image rgba32 = one_pixel(PDFORMAT_RGBA32, pixels);
    output.assign(4, 0);
    assert(geBumpDecodeImageRgba(&rgba32, &texture, output.data()) == 1);
    expect_rgba(output, 1, 2, 3, 4);

    pixels = {5, 6, 7};
    struct pd_image rgb24 = one_pixel(PDFORMAT_RGB24, pixels);
    assert(geBumpDecodeImageRgba(&rgb24, &texture, output.data()) == 1);
    expect_rgba(output, 5, 6, 7, 255);

    pixels = {0xf8, 0x01};
    struct pd_image rgba16 = one_pixel(PDFORMAT_RGBA16, pixels);
    assert(geBumpDecodeImageRgba(&rgba16, &texture, output.data()) == 1);
    expect_rgba(output, 255, 0, 0, 255);

    pixels = {0xf8, 0x00};
    assert(geBumpDecodeImageRgba(&rgba16, &texture, output.data()) == 1);
    expect_rgba(output, 255, 0, 0, 0);

    struct pd_image rgb15 = one_pixel(PDFORMAT_RGB15, pixels);
    assert(geBumpDecodeImageRgba(&rgb15, &texture, output.data()) == 1);
    expect_rgba(output, 255, 0, 0, 255);

    pixels = {33, 222};
    struct pd_image ia16 = one_pixel(PDFORMAT_IA16, pixels);
    assert(geBumpDecodeImageRgba(&ia16, &texture, output.data()) == 1);
    expect_rgba(output, 33, 33, 33, 222);

    pixels = {0xf8};
    struct pd_image ia8 = one_pixel(PDFORMAT_IA8, pixels);
    assert(geBumpDecodeImageRgba(&ia8, &texture, output.data()) == 1);
    expect_rgba(output, 255, 255, 255, 136);

    pixels = {0xf1};
    struct pd_image ia4 = one_pixel(PDFORMAT_IA4, pixels);
    assert(geBumpDecodeImageRgba(&ia4, &texture, output.data()) == 1);
    expect_rgba(output, 224, 224, 224, 255);

    pixels = {0xf0};
    ia4.width = 2;
    output.assign(8, 0);
    assert(geBumpDecodeImageRgba(&ia4, &texture, output.data()) == 1);
    expect_rgba(output, 224, 224, 224, 255);
    assert(output[4] == 0);
    assert(output[5] == 0);
    assert(output[6] == 0);
    assert(output[7] == 0);
    ia4.width = 1;

    pixels = {99};
    struct pd_image i8 = one_pixel(PDFORMAT_I8, pixels);
    assert(geBumpDecodeImageRgba(&i8, &texture, output.data()) == 1);
    expect_rgba(output, 99, 99, 99, 255);

    pixels = {15};
    struct pd_image i4 = one_pixel(PDFORMAT_I4, pixels);
    assert(geBumpDecodeImageRgba(&i4, &texture, output.data()) == 1);
    expect_rgba(output, 240, 240, 240, 255);

    std::vector<uint8_t> palette(256 * 2, 0);
    palette[255 * 2] = 0xf8;
    palette[255 * 2 + 1] = 0x01;
    texture.palette = palette.data();
    texture.numcolours = 256;
    pixels = {255};
    struct pd_image ci8 = one_pixel(PDFORMAT_RGBA16_CI8, pixels);
    assert(geBumpDecodeImageRgba(&ci8, &texture, output.data()) == 1);
    expect_rgba(output, 255, 0, 0, 255);

    pixels = {0};
    texture.numcolours = 1;
    palette[0] = 55;
    palette[1] = 200;
    struct pd_image ia16_ci8 = one_pixel(PDFORMAT_IA16_CI8, pixels);
    assert(geBumpDecodeImageRgba(&ia16_ci8, &texture, output.data()) == 1);
    expect_rgba(output, 55, 55, 55, 200);

    palette[0] = 0xf0;
    struct pd_image ia4_ci4 = one_pixel(PDFORMAT_IA4_CI4, pixels);
    assert(geBumpDecodeImageRgba(&ia4_ci4, &texture, output.data()) == 1);
    expect_rgba(output, 224, 224, 224, 255);

    pixels = {1};
    assert(geBumpDecodeImageRgba(&ci8, &texture, output.data()) == 0);

    struct pd_image invalid_ia16_palette =
        one_pixel(PDFORMAT_IA16_CI8, pixels);
    texture.numcolours = -1;
    assert(geBumpDecodeImageRgba(&invalid_ia16_palette, &texture,
                                 output.data()) == 0);

    struct pd_image invalid_ia4_palette =
        one_pixel(PDFORMAT_IA4_CI4, pixels);
    assert(geBumpDecodeImageRgba(&invalid_ia4_palette, &texture,
                                 output.data()) == 0);
    texture.numcolours = 1;

    struct pd_image no_pixels = {};
    no_pixels.width = 1;
    no_pixels.height = 1;
    assert(geBumpDecodeImageRgba(&no_pixels, &texture, output.data()) == 0);

    struct pd_image zero_size = one_pixel(PDFORMAT_I8, pixels);
    zero_size.width = 0;
    assert(geBumpDecodeImageRgba(&zero_size, &texture, output.data()) == 0);

    zero_size.width = 1;
    zero_size.format = (enum pd_format)99;
    assert(geBumpDecodeImageRgba(&zero_size, &texture, output.data()) == 0);
    assert(geBumpDecodeImageRgba(&zero_size, &texture, nullptr) == 0);

    return 0;
}
