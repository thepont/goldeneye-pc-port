#ifndef GE007_FAST3D_LIGHT_STATE_H
#define GE007_FAST3D_LIGHT_STATE_H

#include <stdint.h>

#include <PR/gbi.h>

/*
 * The RSP light register file is encoded by the F3DEX command stream rather
 * than by the host Light_t layout.  Keep the decoding rules in this small,
 * side-effect-free policy so the command interpreter and its tests share one
 * authority.
 */

static inline int geFast3dLightSlotFromMoveMem(uint8_t index, uint8_t *slot)
{
    if (slot == 0)
    {
        return 0;
    }

#ifdef F3DEX_GBI_2
    /* F3DEX2 uses G_MV_LIGHT plus an offset, not the old individual indices.
     * The current PC command path is the old F3DEX path; reject the other
     * encoding here rather than accidentally treating a viewport upload as a
     * light.  A future F3DEX2 adapter can add its offset policy separately. */
    (void)index;
    return 0;
#else
    if (index < G_MV_L0 || index > G_MV_L7 ||
        ((uint8_t)(index - G_MV_L0) & 1U) != 0)
    {
        return 0;
    }

    *slot = (uint8_t)((index - G_MV_L0) / 2U);
    return *slot < 8U;
#endif
}

static inline int geFast3dDecodeLightCount(uint32_t encoded,
                                           uint8_t *total_records,
                                           uint8_t *directional_count)
{
    if (total_records == 0 || directional_count == 0 ||
        (encoded & 0x80000000U) == 0)
    {
        return 0;
    }

    const uint32_t payload = encoded & 0x7fffffffU;
    if (payload == 0 || (payload % 32U) != 0)
    {
        return 0;
    }

    const uint32_t records = payload / 32U;
    /* One record is ambient; F3DEX supports at most eight directional
     * records, so the encoded aggregate is at most eight records here. */
    if (records < 1U || records > 8U)
    {
        return 0;
    }

    *total_records = (uint8_t)records;
    *directional_count = (uint8_t)(records - 1U);
    return 1;
}

static inline int geFast3dLightColorSlot(uint16_t offset, uint8_t *slot)
{
    if (slot == 0)
    {
        return 0;
    }

    static const uint16_t a_offsets[8] = {
        G_MWO_aLIGHT_1, G_MWO_aLIGHT_2, G_MWO_aLIGHT_3, G_MWO_aLIGHT_4,
        G_MWO_aLIGHT_5, G_MWO_aLIGHT_6, G_MWO_aLIGHT_7, G_MWO_aLIGHT_8};
    static const uint16_t b_offsets[8] = {
        G_MWO_bLIGHT_1, G_MWO_bLIGHT_2, G_MWO_bLIGHT_3, G_MWO_bLIGHT_4,
        G_MWO_bLIGHT_5, G_MWO_bLIGHT_6, G_MWO_bLIGHT_7, G_MWO_bLIGHT_8};

    for (uint8_t candidate = 0; candidate < 8U; ++candidate)
    {
        if (offset == a_offsets[candidate] ||
            offset == b_offsets[candidate])
        {
            *slot = candidate;
            return 1;
        }
    }

    return 0;
}

static inline int geFast3dApplyLightColor(Light_t *light, uint32_t color)
{
    if (light == 0)
    {
        return 0;
    }

    const unsigned char red = (unsigned char)((color >> 24) & 0xffU);
    const unsigned char green = (unsigned char)((color >> 16) & 0xffU);
    const unsigned char blue = (unsigned char)((color >> 8) & 0xffU);
    light->col[0] = red;
    light->col[1] = green;
    light->col[2] = blue;
    light->colc[0] = red;
    light->colc[1] = green;
    light->colc[2] = blue;
    return 1;
}

#endif /* GE007_FAST3D_LIGHT_STATE_H */
