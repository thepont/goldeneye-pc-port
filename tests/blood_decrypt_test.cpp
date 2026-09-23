/* Deterministic tests for blood-frame decoding and image transforms. */
#include <array>
#include <cstdint>
#include <cstdio>

extern "C" {
uint8_t *decrypt_bleeding_animation_data(uint8_t *src, uint8_t width,
                                         uint8_t frame_count, uint8_t *dst,
                                         uint8_t *initial_value);
void sub_GAME_7F01CC94(uint8_t *src, uint16_t count, uint8_t *dst);
void sub_GAME_7F01CCEC(uint8_t *src, uint8_t width, uint8_t height,
                       uint8_t *dst, uint8_t radius);
void sub_GAME_7F01CEEC(uint8_t *src, int32_t height, uint8_t *dst);
void sub_GAME_7F01D02C(uint8_t *src, int32_t height, uint8_t *dst);
void bloodImgTranspose(uint8_t *src, int32_t width, int32_t height,
                       uint8_t *dst);
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

void test_animation_decode(TestState &tests)
{
    uint8_t packed[] = {2, 1};
    uint8_t output[4] = {};
    uint8_t initial = 0;
    uint8_t *end = decrypt_bleeding_animation_data(
        packed, 4, 1, output, &initial);
    tests.expect(initial == 2, "blood decoder returns the initial value");
    tests.expect(end == packed + 2, "blood decoder returns the next input byte");
    tests.expect(output[0] == 0xFF && output[1] == 0xFF &&
                     output[2] == 0xFF && output[3] == 0,
                 "blood decoder expands a packed frame");

    uint8_t repeated[] = {3, 0xFF, 2, 0xFF};
    std::fill(std::begin(output), std::end(output), 0);
    end = decrypt_bleeding_animation_data(
        repeated, 4, 1, output, &initial);
    tests.expect(end == repeated + 4,
                 "blood decoder consumes a repeated frame stream");
    tests.expect(output[0] == 0xFF && output[1] == 0xFF &&
                     output[2] == 0 && output[3] == 0,
                 "blood decoder fills repeated and padded pixels");
}

void test_nibble_and_neighbour_filters(TestState &tests)
{
    uint8_t packed[] = {0xAB, 0xCD, 0x12, 0x30};
    uint8_t unpacked[2] = {};
    sub_GAME_7F01CC94(packed, 2, unpacked);
    tests.expect(unpacked[0] == 0xAC && unpacked[1] == 0x13,
                 "blood decoder expands packed nibbles");

    uint8_t source[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    uint8_t filtered[9] = {};
    sub_GAME_7F01CCEC(source, 3, 3, filtered, 1);
    tests.expect(filtered[4] == 5,
                 "blood neighbour filter rounds the centre average");
    tests.expect(filtered[0] == 3,
                 "blood neighbour filter handles a corner window");
}

void test_transpose(TestState &tests)
{
    const uint8_t source[6] = {1, 2, 3, 4, 5, 6};
    uint8_t output[6] = {};
    bloodImgTranspose(const_cast<uint8_t *>(source), 3, 2, output);
    const uint8_t expected[6] = {1, 4, 2, 5, 3, 6};
    for (int i = 0; i < 6; ++i)
    {
        char message[100];
        std::snprintf(message, sizeof(message),
                      "blood transpose output pixel %d", i);
        tests.expect(output[i] == expected[i], message);
    }
}

void test_four_neighbour_filters(TestState &tests)
{
    /* The legacy helpers operate on 0x61-byte scanlines and leave a one-pixel
     * border untouched. Set up one interior sample with distinct neighbours. */
    std::array<uint8_t, 0x61 * 3> source = {};
    std::array<uint8_t, 0x61 * 3> first = {};
    std::array<uint8_t, 0x61 * 3> second = {};
    const int center = 0x61 + 10;
    source[center - 1] = 4;
    source[center] = 8;
    source[center - 0x61] = 12;
    source[center - 0x60] = 16;
    sub_GAME_7F01CEEC(source.data(), 3, first.data());
    tests.expect(first[center] == 10,
                 "blood first-row filter averages four neighbours");

    source[center + 1] = 20;
    source[center + 0x61] = 24;
    source[center + 0x60] = 28;
    sub_GAME_7F01D02C(source.data(), 3, second.data());
    tests.expect(second[center] == 20,
                 "blood second-row filter averages four neighbours");
}

} // namespace

int main()
{
    TestState tests;
    test_animation_decode(tests);
    test_nibble_and_neighbour_filters(tests);
    test_transpose(tests);
    test_four_neighbour_filters(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d blood-decoder contract(s) failed\n",
                     tests.failures);
        return 1;
    }
    std::puts("blood-decoder unit suite passed");
    return 0;
}
