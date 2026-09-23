/* Regression contracts from README issues and docs/dev/LEVEL-STATUS.md.
 * These are unit-only seams; no game process is required to run them. */
#include "all_unlocked_policy.h"
#include "fast3d_pointer_policy.h"
#include "gfx_relocation_policy.h"
#include "gunshot_audio_policy.h"
#include "level_status_contract_policy.h"
#include "sky_water_policy.h"
#include "text_measure_policy.h"

#include <array>
#include <cstdint>
#include <cstdio>

/* BLANKSAVEDATA in src/bondconstants.h/file2.h: BOND_BROSNAN plus the reset
 * bit is 0x01, and DEFAULT_OPTIONS is 0x003A. Keep this tiny fixture local so
 * the C++ unit does not have to include the full C-only game type graph. */
struct BlankSaveDefaults {
    std::uint8_t completion_bitflags;
    std::uint8_t flag_007;
    std::uint8_t music_vol;
    std::uint8_t sfx_vol;
    std::uint16_t options;
};

struct FailureLog {
    int count = 0;

    void expect(bool condition, const char *message)
    {
        if (!condition)
        {
            ++count;
            std::fprintf(stderr, "RED: %s\n", message);
        }
    }
};

static void test_d245_water_shift_is_continuous(FailureLog &failures)
{
    /* At each boundary the current adaptive policy changes k after only one
     * S10.5 unit. A camera turn can move a water fan across exactly that
     * boundary, so a stable water draw must not switch its fan-wide shift. */
    for (int shift = 0; shift < GE_SKY_TC_MAX_SHIFT; ++shift)
    {
        const float boundary =
            32767.0f * static_cast<float>(1 << shift) - GE_SKY_TC_WRAP;
        const int before = geSkyWaterPickShift(boundary);
        const int after = geSkyWaterPickShift(boundary + 1.0f);
        char message[160];
        std::snprintf(message, sizeof(message),
                      "D245 water shift changed at boundary k=%d (%d -> %d)",
                      shift, before, after);
        failures.expect(before == after, message);
    }

    failures.expect(
        geSkyWaterPickShift(-GE_SKY_TC_WRAP) == 0,
        "D245 water shift did not take the smallest valid shift");
    failures.expect(
        geSkyWaterPickShift(32767.0f * (1 << (GE_SKY_TC_MAX_SHIFT - 1)) -
                             GE_SKY_TC_WRAP) == GE_SKY_TC_MAX_SHIFT - 1,
        "D245 water shift missed the highest in-range bucket");
    failures.expect(
        geSkyWaterPickShift(1.0e12f) == GE_SKY_TC_MAX_SHIFT,
        "D245 water shift did not expose the out-of-range clamp bucket");
}

static void test_d245_water_shift_hysteresis(FailureLog &failures)
{
    GeSkyWaterShiftState state = {};
    geSkyWaterShiftReset(&state);
    geSkyWaterShiftReset(nullptr);
    failures.expect(geSkyWaterShiftLimit(GE_SKY_TC_MAX_SHIFT) > 1.0e38f,
                    "D245 maximum shift must expose the clamp limit");

    const float shift_three_limit =
        32767.0f * static_cast<float>(1 << 3) - GE_SKY_TC_WRAP;
    const float hysteresis = shift_three_limit * 0.05f;

    /* A one-unit camera change at the old boundary must not re-quantize the
     * whole fan. The state is intentionally retained between calls. */
    const int initial = geSkyWaterPickShiftHysteretic(&state,
                                                       shift_three_limit + 1.0f);
    failures.expect(initial == 4,
                    "D245 hysteresis did not select the initial water bucket");

    const int held = geSkyWaterPickShiftHysteretic(
        &state, shift_three_limit - 1.0f);
    failures.expect(held == initial,
                    "D245 hysteresis changed shift at the lower seam");

    const int lowered = geSkyWaterPickShiftHysteretic(
        &state, shift_three_limit - hysteresis - 1.0f);
    failures.expect(lowered == 3,
                    "D245 hysteresis did not leave the bucket after its margin");

    const int raised = geSkyWaterPickShiftHysteretic(
        &state, shift_three_limit + hysteresis + 1.0f);
    failures.expect(raised == 4,
                    "D245 hysteresis did not re-enter the larger bucket");

    GeSkyWaterShiftState independent = {};
    geSkyWaterShiftReset(&independent);
    const int large = geSkyWaterPickShiftHysteretic(&independent, 1.0e12f);
    failures.expect(large == GE_SKY_TC_MAX_SHIFT,
                    "D245 hysteresis did not clamp an oversized fan");
    failures.expect(geSkyWaterPickShiftHysteretic(nullptr, 1.0f) == 0,
                    "D245 null hysteresis state must fail closed");
    GeSkyWaterShiftState invalid_span = {};
    failures.expect(geSkyWaterPickShiftHysteretic(
                        &invalid_span, __builtin_nanf("")) == 0,
                    "D245 non-finite span must fail closed");
    failures.expect(state.shift == 4,
                    "D245 S/T policy state was unexpectedly shared");
}

static void test_d240_sound_deadline_is_inclusive(FailureLog &failures)
{
    const uint32_t now = 100;
    const uint32_t rates[] = {1, 2, 3, 10, 255};

    for (uint32_t rate : rates)
    {
        char message[160];
        std::snprintf(message, sizeof(message),
                      "D240 shot sound missed its deadline at rate %u", rate);
        failures.expect(
            geGunshotSoundIsDue(now, now, now - 1, rate, 0) != 0,
            message);
    }

    failures.expect(
        geGunshotSoundIsDue(now, now, now, 3, 0) == 0,
        "D240 same-tick opposite-hand suppression must remain explicit");
    failures.expect(
        geGunshotSoundIsDue(now, now - 1, now - 1, 3, 0) != 0,
        "D240 late sound deadlines must catch up instead of being dropped");
    failures.expect(
        geGunshotSoundIsDue(now, now + 1, now - 1, 3, 0) == 0,
        "D240 must not fire before the next sound deadline");

    failures.expect(
        geGunshotSoundIsDue(now, now, now, 0, 1) != 0,
        "zero-rate weapons must play on a firing edge");
    failures.expect(
        geGunshotSoundIsDue(now, now, now - 1, 0, 0) == 0,
        "zero-rate weapons must stay silent without a firing edge");
}

static void test_d281_fresh_all_unlocked_save_has_blank_settings(FailureLog &failures)
{
    constexpr BlankSaveDefaults blank = {0x01, 0x00, 0xFF, 0xFF, 0x003A};
    GeAllUnlockedFreshSaveFields fields = {};

    geAllUnlockedSeedFreshSaveDefaults(nullptr);

    failures.expect(
        geAllUnlockedSlotIsZero(&fields, sizeof(fields)) != 0,
        "D281 zero-save detection rejected an all-zero prefix");

    geAllUnlockedSeedFreshSaveDefaults(&fields);

    /* The AllUnlocked read-time patch must behave like fileResetSave for a
     * pristine slot, including completion flags and DEFAULT_OPTIONS. */
    failures.expect(
        fields.completion_bitflags == blank.completion_bitflags,
        "D281 fresh AllUnlocked save did not seed blank completion flags");
    failures.expect(
        fields.flag_007 == blank.flag_007,
        "D281 fresh AllUnlocked save did not seed flag_007");
    failures.expect(
        fields.music_vol == blank.music_vol,
        "D259 fresh AllUnlocked save did not seed music volume");
    failures.expect(
        fields.sfx_vol == blank.sfx_vol,
        "D259 fresh AllUnlocked save did not seed SFX volume");
    failures.expect(
        fields.options == blank.options,
        "D281 fresh AllUnlocked save did not seed DEFAULT_OPTIONS/aim state");

    std::array<std::uint8_t, 96> one_bit = {};
    one_bit[95] = 1;
    failures.expect(
        geAllUnlockedSlotIsZero(one_bit.data(), one_bit.size()) == 0,
        "D259 zero-save detection ignored a nonzero tail byte");
    failures.expect(
        geAllUnlockedSlotIsZero(nullptr, one_bit.size()) == 0,
        "D259 zero-save detection accepted a null slot");
    failures.expect(
        geAllUnlockedSlotIsZero(one_bit.data(), 0) != 0,
        "D259 zero-length save prefix was not treated as empty");
}

static void test_level_status_fast3d_pointer_and_gdl_alignment(
    FailureLog &failures)
{
    /* C2m/D75: the fast3d guard must reject the historical stale 32-bit
     * addresses recorded by LEVEL-STATUS, while accepting memory actually
     * owned by this host process. */
    const uintptr_t bad_addresses[] = {
        0,
        0xffff,
        static_cast<uintptr_t>(0x401c68e0ULL),
        static_cast<uintptr_t>(0x72181ee8ULL),
    };
    for (uintptr_t address : bad_addresses)
    {
        failures.expect(
            geFast3dPointerLooksMapped(address) == 0,
            "C2/C2m fast3d accepted an address outside a mapped host range");
    }

    std::uint8_t mapped_storage = 0;
    failures.expect(
        geFast3dPointerLooksMapped(
            reinterpret_cast<uintptr_t>(&mapped_storage)) != 0,
        "fast3d rejected a live host pointer");
    failures.expect(
        geFast3dPointerLooksMapped(
            static_cast<uintptr_t>(0x0000800000000000ULL) - 1) == 0,
        "fast3d accepted an unmapped canonical-looking address");
    failures.expect(
        geFast3dPointerLooksMapped(
            static_cast<uintptr_t>(0x0000800000000000ULL)) == 0,
        "fast3d accepted a non-canonical upper boundary");

    /* C2 Facility/D80/D82/D83: runtime GDL relocation still masks a source
     * offset without proving that the resulting PC Gfx address has the 16B
     * host command stride. */
    const uint32_t aligned_offsets[] = {0, 16, 0x00fffff0U};
    for (uint32_t encoded : aligned_offsets)
    {
        failures.expect(
            geGfxRelocationOffset(encoded) % GE_GFX_PC_STRIDE == 0,
            "aligned Gfx relocation offset was changed");
    }
    const uint32_t misaligned_offsets[] = {
        8,
        24,
        0x00fffff8U,
    };
    for (uint32_t encoded : misaligned_offsets)
    {
        failures.expect(
            geGfxRelocationOffset(encoded) % GE_GFX_PC_STRIDE != 0,
            "C2 Facility accepted an N64-only 8B Gfx relocation offset");
    }
    failures.expect(
        geGfxRelocationOffset(0xff000010U) == 0x10U,
        "Gfx relocation did not retain the low 24-bit source offset");
}

static void test_level_status_text_measure_preconditions(FailureLog &failures)
{
    const char text[] = "CUBA";
    const int fake_font_chars = 1;
    const int fake_font = 2;

    /* D129/D143: with a zero line-height, font1 and font2 are dereferenced
     * before the credits can be measured, so a missing text bank must reject
     * the whole input tuple. */
    failures.expect(
        geTextMeasureInputsCurrentlyAccepted(nullptr, &fake_font_chars,
                                              &fake_font) == 0,
        "D129 textMeasure accepted a NULL text source");
    failures.expect(
        geTextMeasureInputsCurrentlyAccepted(text, &fake_font_chars,
                                              &fake_font) != 0,
        "textMeasure rejected a complete text-bank tuple");
    failures.expect(
        geTextMeasureInputsCurrentlyAccepted(text, nullptr, &fake_font) == 0,
        "D129 textMeasure accepted missing font-character data");
    failures.expect(
        geTextMeasureInputsCurrentlyAccepted(text, &fake_font_chars,
                                              nullptr) == 0,
        "D129 textMeasure accepted a missing font metrics bank");
}

static void test_level_status_tile_walk_preconditions(FailureLog &failures)
{
    const int fake_tile = 1;

    /* C4/Depot: the second tile walk's return value is ignored before the
     * caller reads sp4C->room. A nonzero status is not proof that tileStack
     * contains a usable tile. */
    failures.expect(
        geTileWalkResultCurrentlyAccepted(0, nullptr) == 0,
        "tile walk rejected neither a failed walk nor a NULL tile");
    failures.expect(
        geTileWalkResultCurrentlyAccepted(1, &fake_tile) != 0,
        "tile walk rejected a successful walk with a tile");
    failures.expect(
        geTileWalkResultCurrentlyAccepted(1, nullptr) == 0,
        "C4 tile walk accepted a successful status with a NULL tile");
    failures.expect(
        geTileWalkResultCurrentlyAccepted(-1, &fake_tile) == 0,
        "C4 tile walk accepted a negative traversal result");
}

static void test_level_status_portal_preconditions(FailureLog &failures)
{
    const int fake_points = 1;

    /* C5/Control: the runtime path dereferences offset_portal->numPoints and
     * then treats the points as a polygon. Test the sentinel and the minimum
     * polygon cardinality separately so NULL/zero-point regressions cannot be
     * hidden by a single happy-path check. */
    failures.expect(
        gePortalPointsCurrentlyAccepted(nullptr, 0) == 0,
        "portal geometry accepted a NULL point list");
    failures.expect(
        gePortalPointsCurrentlyAccepted(&fake_points, 3) != 0,
        "portal geometry rejected a valid triangle");
    failures.expect(
        gePortalPointsCurrentlyAccepted(&fake_points, 0) == 0,
        "C5 portal geometry accepted a zero-point list");
    failures.expect(
        gePortalPointsCurrentlyAccepted(&fake_points, 2) == 0,
        "C5 portal geometry accepted a line as a polygon");
}

static void test_level_status_model_and_sound_preconditions(
    FailureLog &failures)
{
    const int fake_header = 1;
    const int fake_sound = 2;
    const int fake_keymap = 3;
    const int fake_envelope = 4;
    constexpr std::size_t model_table_count = 341;

    /* C6/Surface2: modelLoad indexes PitemZ_entries before proving that the
     * model id is in the table and that the entry/header is usable. */
    failures.expect(
        geModelLoadCurrentlyAccepted(-1, model_table_count, &fake_header) == 0,
        "C6 model load accepted a negative model id");
    failures.expect(
        geModelLoadCurrentlyAccepted(0, model_table_count, &fake_header) != 0,
        "model load rejected the first valid model entry");
    failures.expect(
        geModelLoadCurrentlyAccepted(340, model_table_count, &fake_header) != 0,
        "model load rejected the last valid model entry");
    failures.expect(
        geModelLoadCurrentlyAccepted(341, model_table_count, &fake_header) == 0,
        "C6 model load accepted an out-of-range model id");
    failures.expect(
        geModelLoadCurrentlyAccepted(10, model_table_count, nullptr) == 0,
        "C6 model load accepted a NULL model header");

    /* C7/Sound1: sndSetupSound reads keyMap and envelope immediately. The
     * caller-side bogus-pointer guard does not make direct setup safe. */
    failures.expect(
        geSoundSetupCurrentlyAccepted(nullptr, &fake_keymap,
                                      &fake_envelope) == 0,
        "C7 sound setup accepted a NULL sound");
    failures.expect(
        geSoundSetupCurrentlyAccepted(&fake_sound, &fake_keymap,
                                      &fake_envelope) != 0,
        "sound setup rejected complete sound inputs");
    failures.expect(
        geSoundSetupCurrentlyAccepted(&fake_sound, nullptr,
                                      &fake_envelope) == 0,
        "C7 sound setup accepted a NULL key map");
    failures.expect(
        geSoundSetupCurrentlyAccepted(&fake_sound, &fake_keymap, nullptr) == 0,
        "C7 sound setup accepted a NULL envelope");
}

int main()
{
    FailureLog failures;
    test_d245_water_shift_is_continuous(failures);
    test_d245_water_shift_hysteresis(failures);
    test_d240_sound_deadline_is_inclusive(failures);
    test_d281_fresh_all_unlocked_save_has_blank_settings(failures);
    test_level_status_fast3d_pointer_and_gdl_alignment(failures);
    test_level_status_text_measure_preconditions(failures);
    test_level_status_tile_walk_preconditions(failures);
    test_level_status_portal_preconditions(failures);
    test_level_status_model_and_sound_preconditions(failures);

    if (failures.count == 0)
    {
        std::fprintf(stderr, "defect-contract unit suite passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d defect contract(s) are red\n",
                 failures.count);
    return 1;
}
