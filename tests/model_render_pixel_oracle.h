#ifndef GE007_MODEL_RENDER_PIXEL_ORACLE_H
#define GE007_MODEL_RENDER_PIXEL_ORACLE_H

/* Deterministic color sentinels for the seeded co-op Dam framebuffer test. */

#include "rgb_image.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace geModelPixelOracle
{

struct NormalizedRect
{
    float left;
    float top;
    float right;
    float bottom;
};

/* These regions are anchored to the fixed GE_RSEED=deadbeef Dam co-op camera:
 * each split-screen view contains the other co-op player, and the lower view
 * also contains a green rectangular world prop. Normalized coordinates let
 * Xvfb and hardware captures scale. */
constexpr NormalizedRect kPlayerModelInTopViewport{0.245f, 0.255f, 0.315f, 0.365f};
constexpr NormalizedRect kPlayerModelInBottomViewport{0.445f, 0.715f, 0.535f, 0.825f};
constexpr NormalizedRect kGreenPropRegion{0.415f, 0.645f, 0.515f, 0.710f};
constexpr std::size_t kMinimumPlayerColorPixels        = 12;
constexpr std::size_t kMinimumDistantPlayerColorPixels = 12;
constexpr std::size_t kMinimumPropGreenPixels          = 24;

struct PixelRect
{
    int left;
    int top;
    int right;
    int bottom;
};

inline bool pixel_rect(const geTestImage::Image &image,
                       NormalizedRect            normalized,
                       PixelRect                &result)
{
    if (!geTestImage::valid(image) || !std::isfinite(normalized.left) ||
        !std::isfinite(normalized.top) || !std::isfinite(normalized.right) ||
        !std::isfinite(normalized.bottom) || normalized.left < 0.0f ||
        normalized.top < 0.0f || normalized.right > 1.0f ||
        normalized.bottom > 1.0f || normalized.left >= normalized.right ||
        normalized.top >= normalized.bottom)
    {
        return false;
    }
    result.left   = static_cast<int>(std::floor(normalized.left * image.width));
    result.top    = static_cast<int>(std::floor(normalized.top * image.height));
    result.right  = static_cast<int>(std::ceil(normalized.right * image.width));
    result.bottom = static_cast<int>(std::ceil(normalized.bottom * image.height));
    return result.left >= 0 && result.top >= 0 &&
           result.right <= image.width && result.bottom <= image.height &&
           result.left < result.right && result.top < result.bottom;
}

inline std::size_t count_player_color_pixels(
    const geTestImage::Image &image,
    NormalizedRect            normalized)
{
    PixelRect rect{};
    if (!pixel_rect(image, normalized, rect)) return 0;
    std::size_t count = 0;
    for (int y = rect.top; y < rect.bottom; ++y)
    {
        for (int x = rect.left; x < rect.right; ++x)
        {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * image.width + x) * 3;
            const auto red       = image.pixels[offset];
            const auto green     = image.pixels[offset + 1];
            const auto blue      = image.pixels[offset + 2];
            const auto darkest   = std::min({red, green, blue});
            const auto brightest = std::max({red, green, blue});
            if (brightest - darkest >= 24 &&
                geTestImage::luma(image, x, y) >= 0.16f)
            {
                ++count;
            }
        }
    }
    return count;
}

inline std::size_t count_green_prop_pixels(
    const geTestImage::Image &image,
    NormalizedRect            normalized)
{
    PixelRect rect{};
    if (!pixel_rect(image, normalized, rect)) return 0;
    std::size_t count = 0;
    for (int y = rect.top; y < rect.bottom; ++y)
    {
        for (int x = rect.left; x < rect.right; ++x)
        {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * image.width + x) * 3;
            const int red   = image.pixels[offset];
            const int green = image.pixels[offset + 1];
            const int blue  = image.pixels[offset + 2];
            if (green >= 28 && green - red >= 9 && green - blue >= 9)
            {
                ++count;
            }
        }
    }
    return count;
}

struct CaptureResult
{
    std::size_t top_view_player_color_pixels    = 0;
    std::size_t bottom_view_player_color_pixels = 0;
    std::size_t green_prop_pixels               = 0;

    bool        passes() const
    {
        return top_view_player_color_pixels >= kMinimumPlayerColorPixels &&
               bottom_view_player_color_pixels >=
                   kMinimumDistantPlayerColorPixels &&
               green_prop_pixels >= kMinimumPropGreenPixels;
    }
};

inline CaptureResult inspect(const geTestImage::Image &image)
{
    return {count_player_color_pixels(image, kPlayerModelInTopViewport),
            count_player_color_pixels(image, kPlayerModelInBottomViewport),
            count_green_prop_pixels(image, kGreenPropRegion)};
}

inline geTestImage::Image black_fixture(int width, int height)
{
    geTestImage::Image image;
    image.width  = width;
    image.height = height;
    if (width > 0 && height > 0)
    {
        image.pixels.resize(static_cast<std::size_t>(width) * height * 3, 0);
    }
    return image;
}

inline void fill_region(geTestImage::Image &image, NormalizedRect normalized, std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    PixelRect rect{};
    if (!pixel_rect(image, normalized, rect)) return;
    for (int y = rect.top; y < rect.bottom; ++y)
    {
        for (int x = rect.left; x < rect.right; ++x)
        {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * image.width + x) * 3;
            image.pixels[offset]     = red;
            image.pixels[offset + 1] = green;
            image.pixels[offset + 2] = blue;
        }
    }
}

} // namespace geModelPixelOracle

#endif /* GE007_MODEL_RENDER_PIXEL_ORACLE_H */
