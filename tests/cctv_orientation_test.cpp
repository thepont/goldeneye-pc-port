/* Unit seam for the D307 CCTV orientation defect.
 *
 * A CCTV setup record has two pad IDs on the PC layout:
 *   ObjectRecord::pad at 0x06  = where the physical prop is placed
 *   CCTVRecord::lookpad at 0x90 = the pad the camera head should face
 *
 * This test intentionally uses a raw fixture like the converted setup stream
 * so it does not need a ROM, model renderer, level boot, or game loop. */
#include "cctv.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{

constexpr std::size_t kObjectPadOffset = 0x06;
constexpr std::size_t kCctvLookPadOffset = 0x90;
constexpr std::size_t kCctvRecordBytes = 272;

struct FailureLog
{
    int count = 0;

    void expect(bool condition, const char *message)
    {
        if (!condition)
        {
            ++count;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }
};

void write_s16(std::array<std::uint8_t, kCctvRecordBytes> &record,
               std::size_t offset, std::int16_t value)
{
    std::memcpy(record.data() + offset, &value, sizeof(value));
}

void write_s32(std::array<std::uint8_t, kCctvRecordBytes> &record,
               std::size_t offset, std::int32_t value)
{
    std::memcpy(record.data() + offset, &value, sizeof(value));
}

void test_bunker_records_use_the_separate_look_pad(FailureLog &failures)
{
    struct Fixture
    {
        std::int16_t placement_pad;
        std::int32_t look_pad;
    };

    /* UsetupsevbunkerZ.c records 48, 50, 52, and 54. */
    constexpr Fixture bunker_cameras[] = {
        {10018, 88},
        {10019, 95},
        {10020, 80},
        {10021, 63},
    };

    for (const Fixture &fixture : bunker_cameras)
    {
        alignas(std::max_align_t)
            std::array<std::uint8_t, kCctvRecordBytes> record{};
        write_s16(record, kObjectPadOffset, fixture.placement_pad);
        write_s32(record, kCctvLookPadOffset, fixture.look_pad);

        const auto *cctv = reinterpret_cast<const CCTVRecord *>(record.data());
        failures.expect(
            cctvGetLookPad(cctv) == fixture.look_pad,
            "D307 CCTV orientation selected the placement pad instead of the look pad");
    }
}

void test_look_pad_is_not_derived_from_placement_pad(FailureLog &failures)
{
    alignas(std::max_align_t)
        std::array<std::uint8_t, kCctvRecordBytes> record{};
    write_s16(record, kObjectPadOffset, 10018);
    write_s32(record, kCctvLookPadOffset, 88);

    const auto *cctv = reinterpret_cast<const CCTVRecord *>(record.data());
    failures.expect(cctvGetLookPad(cctv) != 10018,
                    "CCTV look-pad selection aliased the placement pad");
}

} // namespace

int main()
{
    FailureLog failures;
    test_bunker_records_use_the_separate_look_pad(failures);
    test_look_pad_is_not_derived_from_placement_pad(failures);
    return failures.count == 0 ? 0 : 1;
}
