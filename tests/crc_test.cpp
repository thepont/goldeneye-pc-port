/* Deterministic tests for the save-slot checksum and its PRNG dependency. */
#include <array>
#include <cstdint>
#include <cstdio>

extern "C" {
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

void fileGenerateCRC(u8 *address_a, u8 *address_b, void *result);
u32 randomGetNextFrom(u64 *seed);

/* random.c keeps its optional GE_D318R diagnostics in the same translation
 * unit. Keep this unit test independent of the running game's timer/logging
 * infrastructure while still linking the real PRNG object. */
std::int32_t g_GlobalTimer = 0;
void osSyncPrintf(const char *, ...)
{
}
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

struct Checksums
{
    std::int32_t first;
    std::int32_t second;
};

void test_random_stream(TestState &tests)
{
    /* These are the first N64-compatible values from random.s' seed. The
     * full-width seed update is important: the old port's 32-bit emulation
     * produced a different stream from the first draw. */
    std::uint64_t seed = 0xAB8D9F7781280783ULL;
    const std::uint32_t expected[] = {
        0x40EC37CFU,
        0x630AEDD7U,
        0x1F58071EU,
    };

    for (std::uint32_t value : expected)
    {
        tests.expect(randomGetNextFrom(&seed) == value,
                     "randomGetNextFrom N64 golden vector");
    }
}

void test_save_crc_golden_vector(TestState &tests)
{
    /* This is the 88-byte CRC region of an otherwise blank save slot with
     * DEFAULT_OPTIONS (0x003A), matching the persisted save layout:
     * completion bits, flag, music, SFX, options, unlock bytes, padding, and
     * stage times. */
    std::array<std::uint8_t, 88> payload = {};
    payload[2] = 0xFF;
    payload[3] = 0xFF;
    payload[4] = 0x3A;

    const auto original = payload;
    Checksums result = {};
    fileGenerateCRC(payload.data(), payload.data() + payload.size(), &result);

    tests.expect(static_cast<std::uint32_t>(result.first) == 0x5F270F03U,
                 "save CRC first checksum golden vector");
    tests.expect(static_cast<std::uint32_t>(result.second) == 0x83F5C8F6U,
                 "save CRC second checksum golden vector");
    tests.expect(payload == original,
                 "save CRC does not mutate the checksum input region");
}

void test_save_crc_is_content_sensitive(TestState &tests)
{
    std::array<std::uint8_t, 88> original = {};
    std::array<std::uint8_t, 88> changed = original;
    changed.back() = 1;

    Checksums original_crc = {};
    Checksums changed_crc = {};
    fileGenerateCRC(original.data(), original.data() + original.size(),
                    &original_crc);
    fileGenerateCRC(changed.data(), changed.data() + changed.size(),
                    &changed_crc);

    tests.expect(original_crc.first != changed_crc.first ||
                     original_crc.second != changed_crc.second,
                 "save CRC changes when persisted payload changes");
}

} // namespace

int main()
{
    TestState tests;
    test_random_stream(tests);
    test_save_crc_golden_vector(tests);
    test_save_crc_is_content_sensitive(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d CRC contract(s) failed\n", tests.failures);
        return 1;
    }
    std::puts("CRC unit suite passed");
    return 0;
}
