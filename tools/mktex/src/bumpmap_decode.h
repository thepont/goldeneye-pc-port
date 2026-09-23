#ifndef GE007_BUMPMAP_DECODE_H
#define GE007_BUMPMAP_DECODE_H

/*
 * Convert the formats returned by libpdtex into a stable RGBA8 buffer.
 *
 * The implementation lives in bumpmap_decode.c so the normal-map converter
 * has one linkable authority and receives an ordinary gcov report.
 */

#include <stdint.h>
#include "libpdtex/pdtex.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t geBumpExpand5(uint8_t value);

void geBumpSetRgba(uint8_t *destination,
                   uint8_t red,
                   uint8_t green,
                   uint8_t blue,
                   uint8_t alpha);

void geBumpDecodeRgba16(uint16_t value, uint8_t *destination);

int geBumpDecodeImageRgba(const struct pd_image *image,
                          const struct pd_tex *texture,
                          uint8_t *rgba);

#ifdef __cplusplus
}
#endif

#endif
