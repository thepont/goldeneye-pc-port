#include <stddef.h>
#include <stdint.h>

#include "bumpmap_decode.h"

uint8_t geBumpExpand5(uint8_t value)
{
    return (uint8_t)((value << 3) | (value >> 2));
}

void geBumpSetRgba(uint8_t *destination,
                   uint8_t red,
                   uint8_t green,
                   uint8_t blue,
                   uint8_t alpha)
{
    destination[0] = red;
    destination[1] = green;
    destination[2] = blue;
    destination[3] = alpha;
}

void geBumpDecodeRgba16(uint16_t value, uint8_t *destination)
{
    geBumpSetRgba(destination,
                  geBumpExpand5((uint8_t)((value >> 11) & 0x1f)),
                  geBumpExpand5((uint8_t)((value >> 6) & 0x1f)),
                  geBumpExpand5((uint8_t)((value >> 1) & 0x1f)),
                  (value & 1) != 0 ? 255 : 0);
}

int geBumpDecodeImageRgba(const struct pd_image *image,
                          const struct pd_tex *texture,
                          uint8_t *rgba)
{
    size_t pixels;

    if (image == NULL || texture == NULL || rgba == NULL ||
        image->pixels == NULL || image->width <= 0 || image->height <= 0) {
        return 0;
    }
    pixels = (size_t)image->width * (size_t)image->height;

    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t *source = image->pixels;
        uint8_t *destination = &rgba[i * 4];

        switch (image->format) {
        case PDFORMAT_RGBA32:
            for (size_t channel = 0; channel < 4; ++channel) {
                destination[channel] = source[i * 4 + channel];
            }
            break;
        case PDFORMAT_RGB24:
            geBumpSetRgba(destination, source[i * 3], source[i * 3 + 1],
                           source[i * 3 + 2], 255);
            break;
        case PDFORMAT_RGBA16: {
            uint16_t value = ((uint16_t)source[i * 2] << 8) |
                             source[i * 2 + 1];
            geBumpDecodeRgba16(value, destination);
            break;
        }
        case PDFORMAT_RGB15: {
            uint16_t value = ((uint16_t)source[i * 2] << 8) |
                             source[i * 2 + 1];
            geBumpDecodeRgba16((uint16_t)(value | 1), destination);
            break;
        }
        case PDFORMAT_IA16:
            geBumpSetRgba(destination, source[i * 2], source[i * 2],
                          source[i * 2], source[i * 2 + 1]);
            break;
        case PDFORMAT_IA8:
            geBumpSetRgba(destination, (uint8_t)((source[i] >> 4) * 17),
                          (uint8_t)((source[i] >> 4) * 17),
                          (uint8_t)((source[i] >> 4) * 17),
                          (uint8_t)((source[i] & 0x0f) * 17));
            break;
        case PDFORMAT_IA4: {
            uint8_t value = (i & 1) == 0
                ? (uint8_t)(source[i / 2] >> 4)
                : (uint8_t)(source[i / 2] & 0x0f);
            geBumpSetRgba(destination,
                          (uint8_t)(((value >> 1) & 7) * 32),
                          (uint8_t)(((value >> 1) & 7) * 32),
                          (uint8_t)(((value >> 1) & 7) * 32),
                          (value & 1) != 0 ? 255 : 0);
            break;
        }
        case PDFORMAT_I8:
            geBumpSetRgba(destination, source[i], source[i], source[i], 255);
            break;
        case PDFORMAT_I4:
            geBumpSetRgba(destination, (uint8_t)((source[i] & 0x0f) * 16),
                          (uint8_t)((source[i] & 0x0f) * 16),
                          (uint8_t)((source[i] & 0x0f) * 16), 255);
            break;
        case PDFORMAT_RGBA16_CI8:
        case PDFORMAT_RGBA16_CI4: {
            const uint8_t index = source[i];
            if (texture->palette == NULL || texture->numcolours < 0 ||
                (int)index >= texture->numcolours) {
                return 0;
            }
            geBumpDecodeRgba16(
                (uint16_t)((uint16_t)texture->palette[index * 2] << 8 |
                           texture->palette[index * 2 + 1]),
                destination);
            break;
        }
        case PDFORMAT_IA16_CI8: {
            const uint8_t index = source[i];
            if (texture->palette == NULL || texture->numcolours < 0 ||
                (int)index >= texture->numcolours) {
                return 0;
            }
            geBumpSetRgba(destination,
                          texture->palette[index * 2],
                          texture->palette[index * 2],
                          texture->palette[index * 2],
                          texture->palette[index * 2 + 1]);
            break;
        }
        case PDFORMAT_IA4_CI4: {
            const uint8_t index = source[i];
            uint8_t value;
            if (texture->palette == NULL || texture->numcolours < 0 ||
                (int)index >= texture->numcolours) {
                return 0;
            }
            value = texture->palette[index];
            geBumpSetRgba(destination,
                          (uint8_t)(((value >> 5) & 7) * 32),
                          (uint8_t)(((value >> 5) & 7) * 32),
                          (uint8_t)(((value >> 5) & 7) * 32),
                          (value & 0x10) != 0 ? 255 : 0);
            break;
        }
        default:
            return 0;
        }
    }

    return 1;
}
