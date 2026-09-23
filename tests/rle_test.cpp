/* Deterministic tests for the game's RLE image decoders. */
#include <cstdint>
#include <cstdio>

extern "C" {
void rle_expand_8bit(uint8_t *src, uint8_t *dst);
void rle_expand_rgb_to_u16_5551(uint8_t *src, uint16_t *dst);
void rle_expand_rgb_to_rgba32(uint8_t *src, uint8_t *dst);
}

namespace
{

struct TestState
{
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        if (!condition)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }
};

void test_expand_8bit(TestState &tests)
{
    /* The first ten bytes are the original image header; RLE begins at +10. */
    uint8_t source[14] = {4, 0, 2, 0, 0, 0, 0, 0, 0, 0,
                          3, 0xAA, 5, 0xBB};
    uint8_t output[8] = {};
    rle_expand_8bit(source, output);

    const uint8_t expected[8] = {
        0xAA, 0xAA, 0xAA, 0xBB,
        0xBB, 0xBB, 0xBB, 0xBB,
    };
    for (int i = 0; i < 8; ++i)
    {
        char message[100];
        std::snprintf(message, sizeof(message),
                      "8-bit RLE output pixel %d", i);
        tests.expect(output[i] == expected[i], message);
    }
}

void test_expand_rgb_to_5551(TestState &tests)
{
    uint8_t source[18] = {
        2, 0, 2, 0, 0, 0, 0, 0, 0, 0,
        2, 255, 128, 0,
        2, 0, 64, 255,
    };
    uint16_t output[4] = {};
    rle_expand_rgb_to_u16_5551(source, output);

    const uint16_t first = static_cast<uint16_t>(
        ((255 >> 3) << 1) | ((128 >> 3) << 6) | 1);
    const uint16_t second = static_cast<uint16_t>(
        ((255 >> 3) << 11) | ((64 >> 3) << 6) | 1);
    tests.expect(output[0] == first, "RGB RLE first 5551 pixel");
    tests.expect(output[1] == first, "RGB RLE repeated 5551 pixel");
    tests.expect(output[2] == second, "RGB RLE second 5551 pixel");
    tests.expect(output[3] == second, "RGB RLE repeated second 5551 pixel");
}

void test_expand_rgb_to_rgba32(TestState &tests)
{
    uint8_t source[18] = {
        3, 0, 1, 0, 0, 0, 0, 0, 0, 0,
        1, 10, 20, 30,
        2, 40, 50, 60,
    };
    uint8_t output[12] = {};
    rle_expand_rgb_to_rgba32(source, output);

    const uint8_t expected[12] = {
        30, 20, 10, 0xFF,
        60, 50, 40, 0xFF,
        60, 50, 40, 0xFF,
    };
    for (int i = 0; i < 12; ++i)
    {
        char message[100];
        std::snprintf(message, sizeof(message),
                      "RGBA32 RLE output byte %d", i);
        tests.expect(output[i] == expected[i], message);
    }
}

} // namespace

int main()
{
    TestState tests;
    test_expand_8bit(tests);
    test_expand_rgb_to_5551(tests);
    test_expand_rgb_to_rgba32(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d RLE contract(s) failed\n", tests.failures);
        return 1;
    }
    std::puts("RLE unit suite passed");
    return 0;
}
