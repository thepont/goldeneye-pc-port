#include "../port/fast3d/fast3d_light_state.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>

static void require(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

#undef assert
#define assert(condition) require((condition))

int main()
{
    uint8_t slot = 0;
    assert(geFast3dLightSlotFromMoveMem(G_MV_L0, &slot) != 0);
    assert(slot == 0);
    assert(geFast3dLightSlotFromMoveMem(G_MV_L7, &slot) != 0);
    assert(slot == 7);
    assert(geFast3dLightSlotFromMoveMem((uint8_t)(G_MV_L0 + 1), &slot) == 0);
    assert(geFast3dLightSlotFromMoveMem((uint8_t)(G_MV_L7 + 2), &slot) == 0);
    assert(geFast3dLightSlotFromMoveMem(G_MV_VIEWPORT, &slot) == 0);
    assert(geFast3dLightSlotFromMoveMem(G_MV_L0, nullptr) == 0);

    uint8_t total_records = 0;
    uint8_t directional_count = 0;
    for (uint8_t directional = 0; directional <= 7; ++directional) {
        const uint32_t encoded = 0x80000000U |
                                 ((uint32_t)(directional + 1) * 32U);
        assert(geFast3dDecodeLightCount(encoded, &total_records,
                                        &directional_count) != 0);
        assert(total_records == directional + 1);
        assert(directional_count == directional);
    }
    assert(geFast3dDecodeLightCount(0x00000020U, &total_records,
                                    &directional_count) == 0);
    assert(geFast3dDecodeLightCount(0x80000000U, &total_records,
                                    &directional_count) == 0);
    assert(geFast3dDecodeLightCount(0x80000120U, &total_records,
                                    &directional_count) == 0);
    assert(geFast3dDecodeLightCount(0x80000020U, nullptr,
                                    &directional_count) == 0);

    uint8_t light_slot = 0;
    assert(geFast3dLightColorSlot(G_MWO_aLIGHT_1, &light_slot) != 0);
    assert(light_slot == 0);
    assert(geFast3dLightColorSlot(G_MWO_bLIGHT_4, &light_slot) != 0);
    assert(light_slot == 3);
    assert(geFast3dLightColorSlot(G_MWO_aLIGHT_8, &light_slot) != 0);
    assert(light_slot == 7);
    assert(geFast3dLightColorSlot(0x18, &light_slot) == 0);
    assert(geFast3dLightColorSlot(G_MWO_aLIGHT_1, nullptr) == 0);

    Light_t light = {};
    light.col[0] = light.col[1] = light.col[2] = 1;
    light.colc[0] = light.colc[1] = light.colc[2] = 1;
    assert(geFast3dApplyLightColor(&light, 0x12345678U) != 0);
    assert(light.col[0] == 0x12 && light.col[1] == 0x34 &&
           light.col[2] == 0x56);
    assert(light.colc[0] == 0x12 && light.colc[1] == 0x34 &&
           light.colc[2] == 0x56);
    assert(geFast3dApplyLightColor(nullptr, 0) == 0);
    return 0;
}
