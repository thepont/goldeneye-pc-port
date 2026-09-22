#ifndef GE007_VECTOR_TEXT_LAYOUT_H
#define GE007_VECTOR_TEXT_LAYOUT_H

#include <stdint.h>

typedef struct GeVectorTextScreenRect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} GeVectorTextScreenRect;

/* Choose the raster size for a logical font at the current output scale.
 * The larger mapped axis wins so neither dimension magnifies a permanently
 * low-resolution coverage bitmap on a widescreen or letterboxed output. The
 * font remains vector-backed: FreeType regenerates the atlas at this size. */
static inline int geVectorTextRasterPixelHeightForScreen(
    int logical_font_height,
    int32_t screen_width,
    int32_t screen_height,
    float logical_width,
    float logical_height)
{
    if (logical_font_height <= 0) {
        logical_font_height = 1;
    }
    if (screen_width <= 0) {
        screen_width = 1;
    }
    if (screen_height <= 0) {
        screen_height = 1;
    }
    if (logical_width <= 0.0f) {
        logical_width = 1.0f;
    }
    if (logical_height <= 0.0f) {
        logical_height = 1.0f;
    }
    const float x_scale = (float)screen_width / logical_width;
    const float y_scale = (float)screen_height / logical_height;
    const float scale = x_scale > y_scale ? x_scale : y_scale;
    const float scaled_height = (float)logical_font_height * scale;
    const int rounded_height = (int)(scaled_height + 0.5f);
    return rounded_height > 0 ? rounded_height : 1;
}

/* A vector atlas is valid only for the exact raster size it was generated
 * for. A zero requested size is invalid and must not trigger a replacement;
 * currentRasterPixelHeight() normalizes that case before this policy runs. */
static inline int geVectorTextNeedsAtlasRebuild(int current_pixel_height,
                                                int requested_pixel_height)
{
    return requested_pixel_height > 0 &&
           current_pixel_height != requested_pixel_height;
}

/* Convert the logical VI-space coordinates used by the game-owned overlay to
 * the currently mapped window/FBO rectangle. Keeping this transform separate
 * from FreeType/OpenGL makes layout deterministic and prevents a second,
 * subtly different coordinate conversion from appearing in the renderer. */
static inline void geVectorTextMapLogicalPoint(float logical_x,
                                               float logical_y,
                                               float logical_width,
                                               float logical_height,
                                               GeVectorTextScreenRect screen,
                                               float *screen_x,
                                               float *screen_y)
{
    if (logical_width <= 0.0f) {
        logical_width = 1.0f;
    }
    if (logical_height <= 0.0f) {
        logical_height = 1.0f;
    }
    *screen_x = (float)screen.x +
                (logical_x * (float)screen.width / logical_width);
    *screen_y = (float)screen.y +
                (logical_y * (float)screen.height / logical_height);
}

#endif /* GE007_VECTOR_TEXT_LAYOUT_H */
