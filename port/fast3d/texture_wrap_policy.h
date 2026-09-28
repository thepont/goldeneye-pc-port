#ifndef GE007_TEXTURE_WRAP_POLICY_H
#define GE007_TEXTURE_WRAP_POLICY_H

inline unsigned geFast3dTileExtent(unsigned start, unsigned end)
{
    return end < start ? 0u : (end - start) / 4u + 1u;
}

inline bool geFast3dTileRectangleValid(unsigned uls, unsigned lrs,
                                       unsigned ult, unsigned lrt)
{
    return lrs >= uls && lrt >= ult;
}

inline bool geFast3dUseSubtileWrap(unsigned image_extent,
                                  unsigned tile_extent,
                                  unsigned tile_start,
                                  unsigned tile_end,
                                  unsigned mask_bits)
{
    if (tile_extent == 0 || tile_extent >= image_extent) {
        return false;
    }

    // Some authored materials leave the tile rectangle unset while selecting
    // the complete image through its coordinate mask. Treating 0..0 as a
    // literal one-texel sub-tile flattens Dam's animated CI8 water.
    if (tile_start == 0 && tile_end == 0 && mask_bits > 0 &&
        mask_bits < 31 && (1u << mask_bits) == image_extent) {
        return false;
    }
    return true;
}

#endif
