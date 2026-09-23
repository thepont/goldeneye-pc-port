/* Deterministic contract tests for the PC RZ/raw-deflate adapter. */
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "realzlib.h"

extern "C" {
std::uint32_t decompressdata(std::uint8_t *src, std::uint8_t *dst,
                             void *huffman_table);
std::uintptr_t rzipGetSomething();
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

std::vector<std::uint8_t> raw_deflate(const std::vector<std::uint8_t> &input)
{
    z_stream stream = {};
    const int init_status = deflateInit2(&stream, Z_BEST_COMPRESSION,
                                         Z_DEFLATED, -15, 8,
                                         Z_DEFAULT_STRATEGY);
    if (init_status != Z_OK)
    {
        return {};
    }

    std::vector<std::uint8_t> compressed(128 + input.size());
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

void test_round_trip(TestState &tests, const std::vector<std::uint8_t> &input,
                     const char *message)
{
    const std::vector<std::uint8_t> compressed = raw_deflate(input);
    tests.expect(!compressed.empty(), "host raw-deflate fixture is valid");

    std::array<std::uint8_t, 8192> source_storage = {};
    tests.expect(compressed.size() + 2 <= source_storage.size(),
                 "RZ source fixture fits in its high-address stack buffer");
    std::uint8_t *source = source_storage.data();
    source[0] = 0x11;
    source[1] = 0x72;
    std::copy(compressed.begin(), compressed.end(), source + 2);

    std::vector<std::uint8_t> output(input.size() + 32, 0xCD);
    const std::uint32_t produced = decompressdata(
        source, output.data(), nullptr);
    tests.expect(produced == input.size(), message);
    tests.expect(std::equal(input.begin(), input.end(), output.begin()),
                 "RZ adapter preserves decompressed bytes");
    const auto end_address = reinterpret_cast<std::uintptr_t>(
        source + compressed.size() + 2);
    tests.expect(end_address > UINT32_MAX,
                 "RZ fixture places the consumed source above the N64 address width");
    tests.expect(rzipGetSomething() == end_address,
                 "RZ adapter records the complete host consumed input position");
}

void test_empty_and_repetitive_streams(TestState &tests)
{
    test_round_trip(tests, {}, "RZ adapter handles an empty raw-deflate stream");

    std::vector<std::uint8_t> repetitive(4096, 0xA5);
    test_round_trip(tests, repetitive,
                    "RZ adapter handles a highly repetitive stream");

    const std::vector<std::uint8_t> mixed = {
        0x00, 0x11, 0x72, 0xFF, 0x80, 0x7F, 0x01, 0x02, 0x03,
        0x00, 0x11, 0x72, 0xFE, 0x81, 0x7E, 0x04, 0x05, 0x06,
    };
    test_round_trip(tests, mixed,
                    "RZ adapter handles mixed binary resource bytes");
}

void test_malformed_stream_is_bounded(TestState &tests)
{
    /* The adapter receives mapped resource memory in production. Give the
     * malformed fixture the same generous readable ceiling it advertises to
     * zlib, so this failure-path test cannot turn a bad stream into a host
     * out-of-bounds read. */
    std::vector<std::uint8_t> source(2 + 0x400000, 0);
    source[0] = 0x11;
    source[1] = 0x72;
    std::vector<std::uint8_t> output(0x400000, 0xCD);
    tests.expect(decompressdata(source.data(), output.data(), nullptr) == 0,
                 "RZ adapter returns zero for malformed input");
}

} // namespace

int main()
{
    TestState tests;
    test_empty_and_repetitive_streams(tests);
    test_malformed_stream_is_bounded(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d RZ decompressor contract(s) failed\n",
                     tests.failures);
        return 1;
    }
    std::puts("RZ decompressor unit suite passed");
    return 0;
}
