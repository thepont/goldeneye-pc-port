#ifndef GE007_FAST3D_AMBIENT_LIGHTING_H
#define GE007_FAST3D_AMBIENT_LIGHTING_H

#include <stdint.h>

#include <PR/gbi.h>

/* F3DEX numbers the ambient record after the directional records. The PC
 * decoder receives only the MOVEMEM index, so derive the ambient slot from
 * the current light count before choosing the copy width. */
static inline int geFast3dIsAmbientLightUpload(uint8_t index,
                                                uint8_t current_num_lights)
{
    if (current_num_lights == 0)
    {
        return 0;
    }

    return index == (uint8_t)(G_MV_L0 +
                              2 * (current_num_lights - 1));
}

/* Ambient_t is intentionally smaller than Light_t. Copy its two colour
 * triplets field-by-field so the decoder never reads the following record in
 * a LightsN aggregate while filling the PC-side light register. */
static inline void geFast3dCopyAmbientLight(Light_t *destination,
                                             const Ambient_t *source)
{
    if (destination == 0 || source == 0)
    {
        return;
    }

    for (int channel = 0; channel < 3; ++channel)
    {
        destination->col[channel] = source->col[channel];
        destination->colc[channel] = source->colc[channel];
        destination->dir[channel] = 0;
    }

    destination->pad1 = 0;
    destination->pad2 = 0;
    destination->pad3 = 0;
}

/* Select the colour that seeds the RSP vertex result before directional-light
 * contributions are added. This keeps the lit/unlit branch observable in a
 * small deterministic unit seam instead of leaving it embedded in the GL
 * renderer. */
static inline void geFast3dSelectVertexBaseColor(uint32_t geometry_mode,
                                                  const uint8_t *ambient,
                                                  const uint8_t *authored,
                                                  uint8_t output[3])
{
    if (output == 0)
    {
        return;
    }

    const uint8_t *source = (geometry_mode & G_LIGHTING) != 0
        ? ambient
        : authored;
    for (int channel = 0; channel < 3; ++channel)
    {
        output[channel] = source == 0 ? 0 : source[channel];
    }
}

/* The ROM room stream inherits the unlit state left by the sky pass.  Keep
 * Original mode byte-for-byte compatible with that command-state behavior;
 * only the modern presentation tiers opt into the port's room-lighting
 * restore. */
static inline int geFast3dShouldRestoreRoomLighting(int render_mode)
{
    return render_mode == 1 || render_mode == 2;
}

#endif /* GE007_FAST3D_AMBIENT_LIGHTING_H */
