#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "realzlib.h"
#include "zlib.h"

extern "C" {
int zlib_inflate();
int zlib_inflate_block(s32 *last_block);
int zlib_inflate_stored();
int zlib_inflate_fixed();
int zlib_inflate_dynamic();
extern u32 rz_hufts;
s32 zlib_huft_build(u32 *lengths, u32 count, u32 literal_limit,
                    u16 *distance_base, u8 *distance_extra,
                    struct huft **table, s32 *table_bits);
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

    template <typename T>
    void expect_equal(T actual, T expected, const char *message)
    {
        if (actual != expected)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }
};

std::vector<std::uint8_t> raw_deflate(const std::vector<std::uint8_t> &input,
                                      int level, int strategy)
{
    z_stream stream = {};
    if (deflateInit2(&stream, level, Z_DEFLATED, -15, 8, strategy) != Z_OK)
    {
        return {};
    }

    std::vector<std::uint8_t> compressed(input.size() + 256);
    stream.next_in = const_cast<Bytef *>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = compressed.data();
    stream.avail_out = static_cast<uInt>(compressed.size());
    const int status = deflate(&stream, Z_FINISH);
    const std::size_t produced = stream.total_out;
    deflateEnd(&stream);
    if (status != Z_STREAM_END)
    {
        return {};
    }

    compressed.resize(produced);
    return compressed;
}

bool inflate_with_game_decoder(const std::vector<std::uint8_t> &compressed,
                               std::vector<std::uint8_t> *output,
                               int *result)
{
    std::vector<struct huft> table(65536);

    output->assign(32768, 0xCD);
    rz_hlist = table.data();
    rz_inbuf = const_cast<u8 *>(compressed.data());
    rz_outbuf = output->data();
    rz_inptr = 0;
    rz_wp = 0;
    *result = zlib_inflate();
    output->resize(rz_wp);
    return *result == 0;
}

void test_huffman_table_boundaries(TestState &tests)
{
    std::vector<struct huft> table(64);
    struct huft *root = reinterpret_cast<struct huft *>(uintptr_t(1));
    s32 table_bits = 5;
    u32 empty_lengths[] = { 0, 0, 0 };

    rz_hlist = table.data();
    rz_hufts = 0;
    const s32 empty_result = zlib_huft_build(
        empty_lengths, 3, 0, nullptr, nullptr, &root, &table_bits);
    tests.expect_equal(empty_result, s32(0),
                       "empty Huffman alphabets return success");
    tests.expect(root == nullptr,
                 "empty Huffman alphabets publish a null root table");
    tests.expect_equal(table_bits, s32(0),
                       "empty Huffman alphabets publish zero lookup bits");

    u32 literal_lengths[] = { 1, 1 };
    root = nullptr;
    table_bits = 1;
    rz_hufts = 0;
    const s32 literal_result = zlib_huft_build(
        literal_lengths, 2, 2, nullptr, nullptr, &root, &table_bits);
    tests.expect_equal(literal_result, s32(0),
                       "complete two-symbol Huffman alphabets build cleanly");
    tests.expect(root != nullptr,
                 "complete Huffman alphabets publish a lookup table");
    tests.expect(table[0].e == 16 || table[1].e == 16,
                 "literal Huffman entries are marked as literal operations");
}

void test_raw_block_modes(TestState &tests)
{
    const std::vector<std::uint8_t> input = {
        0x00, 0x11, 0x72, 0xFF, 0x80, 0x7F, 0x01, 0x02, 0x03,
        0x00, 0x11, 0x72, 0xFE, 0x81, 0x7E, 0x04, 0x05, 0x06,
    };

    const struct
    {
        int level;
        int strategy;
        const char *message;
    } modes[] = {
        { Z_NO_COMPRESSION, Z_DEFAULT_STRATEGY,
          "stored deflate blocks round-trip through the game inflater" },
        { Z_BEST_SPEED, Z_FIXED,
          "fixed Huffman blocks round-trip through the game inflater" },
        { Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY,
          "dynamic Huffman blocks round-trip through the game inflater" },
    };

    for (const auto &mode : modes)
    {
        const std::vector<std::uint8_t> compressed = raw_deflate(
            input, mode.level, mode.strategy);
        std::vector<std::uint8_t> output;
        int result = -1;

        tests.expect(!compressed.empty(), "raw-deflate fixture is valid");
        const bool succeeded = inflate_with_game_decoder(
            compressed, &output, &result);
        tests.expect(succeeded, mode.message);
        tests.expect(output == input,
                     "game inflater preserves every decoded byte");
    }
}

void test_repeated_copy_and_large_output(TestState &tests)
{
    std::vector<std::uint8_t> input(4096, 0xA5);
    const std::vector<std::uint8_t> compressed = raw_deflate(
        input, Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY);
    std::vector<std::uint8_t> output;
    int result = -1;

    tests.expect(inflate_with_game_decoder(compressed, &output, &result),
                 "length-distance copies decode without a host overlap fault");
    tests.expect(output == input,
                 "length-distance copies reproduce a repetitive output stream");
}

void test_invalid_block_type_is_rejected(TestState &tests)
{
    const std::vector<std::uint8_t> invalid_block = { 0x07 };
    std::vector<std::uint8_t> output;
    int result = -1;

    tests.expect(!inflate_with_game_decoder(invalid_block, &output, &result),
                 "reserved deflate block types do not decode as success");
    tests.expect_equal(result, 2,
                       "reserved deflate block types return the documented error code");
}

} // namespace

int main()
{
    TestState tests;
    test_huffman_table_boundaries(tests);
    test_raw_block_modes(tests);
    test_repeated_copy_and_large_output(tests);
    test_invalid_block_type_is_rejected(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d game zlib contract(s) failed\n",
                     tests.failures);
        return 1;
    }

    std::puts("game zlib suite passed");
    return 0;
}
