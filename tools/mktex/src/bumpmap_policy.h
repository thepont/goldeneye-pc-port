#ifndef GE007_BUMPMAP_POLICY_H
#define GE007_BUMPMAP_POLICY_H

/*
 * ROM-independent bump-map generation policy.
 *
 * This header is deliberately usable from both the C command-line generator
 * and the C++ unit tests. It owns the decisions that must stay deterministic:
 * which image classes are safe to process, how source colour becomes height,
 * how transparent texels become a neutral surface, and how height gradients
 * become encoded tangent-space normals.
 */

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>

typedef enum GeBumpMapEligibility {
    GE_BUMP_MAP_ELIGIBLE = 0,
    GE_BUMP_MAP_INVALID,
    GE_BUMP_MAP_TOO_SMALL,
    GE_BUMP_MAP_PRESENTATION,
    GE_BUMP_MAP_UNSAFE_MATERIAL,
    GE_BUMP_MAP_UNTAGGED,
} GeBumpMapEligibility;

static inline int geBumpTextEquals(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return 0;
    }

    while (*left != '\0' && *right != '\0') {
        const unsigned char left_char = (unsigned char)*left;
        const unsigned char right_char = (unsigned char)*right;
        if (tolower(left_char) != tolower(right_char)) {
            return 0;
        }
        ++left;
        ++right;
    }

    return *left == '\0' && *right == '\0';
}

static inline int geBumpTextContains(const char *value, const char *needle)
{
    if (value == NULL || needle == NULL || *needle == '\0') {
        return 0;
    }

    for (const char *start = value; *start != '\0'; ++start) {
        const char *value_cursor = start;
        const char *needle_cursor = needle;
        while (*value_cursor != '\0' && *needle_cursor != '\0' &&
               tolower((unsigned char)*value_cursor) ==
                   tolower((unsigned char)*needle_cursor)) {
            ++value_cursor;
            ++needle_cursor;
        }
        if (*needle_cursor == '\0') {
            return 1;
        }
    }

    return 0;
}

static inline int geBumpNameIsPresentationOnly(const char *name)
{
    static const char *const blocked_tokens[] = {
        "ICON", "CROSSHAIR", "RADAR", "FONT", "TEXT", "BRIEF",
        "MENU", "SELECT", "AMMO", "LOGO", "FIRE", "SMOKE", "EXPLOS",
        "BLOOD", "FLARE", "SPARK", "VERTICALSTRIPES", "7SEG", "MUGSHOT",
    };

    if (name == NULL) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(blocked_tokens) / sizeof(blocked_tokens[0]); ++i) {
        if (geBumpTextContains(name, blocked_tokens[i])) {
            return 1;
        }
    }

    return 0;
}

static inline int geBumpMaterialIsPhysical(const char *material)
{
    static const char *const physical_materials[] = {
        "HIT_STONE", "HIT_WOOD", "HIT_METAL", "HIT_METALOBJ",
        "HIT_SNOW", "HIT_DIRT", "HIT_MUD", "HIT_TILE",
    };

    if (material == NULL) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(physical_materials) / sizeof(physical_materials[0]); ++i) {
        if (geBumpTextEquals(material, physical_materials[i])) {
            return 1;
        }
    }

    return 0;
}

static inline int geBumpMaterialIsUnsafe(const char *material)
{
    static const char *const unsafe_materials[] = {
        "HIT_WATER", "HIT_GLASS", "HIT_GLASS_XLU", "HIT_CHR",
    };

    if (material == NULL) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(unsafe_materials) / sizeof(unsafe_materials[0]); ++i) {
        if (geBumpTextEquals(material, unsafe_materials[i])) {
            return 1;
        }
    }

    return 0;
}

static inline GeBumpMapEligibility geBumpMapEligibility(
    const char *name,
    const char *hit_texture,
    const char *hit_sound,
    uint32_t width,
    uint32_t height,
    int include_default)
{
    if (name == NULL || *name == '\0') {
        return GE_BUMP_MAP_INVALID;
    }
    if (width < 4 || height < 4) {
        return GE_BUMP_MAP_TOO_SMALL;
    }
    if (geBumpNameIsPresentationOnly(name)) {
        return GE_BUMP_MAP_PRESENTATION;
    }
    if (geBumpMaterialIsUnsafe(hit_texture) ||
        geBumpMaterialIsUnsafe(hit_sound)) {
        return GE_BUMP_MAP_UNSAFE_MATERIAL;
    }
    if (geBumpMaterialIsPhysical(hit_texture) ||
        geBumpMaterialIsPhysical(hit_sound)) {
        return GE_BUMP_MAP_ELIGIBLE;
    }
    if (include_default &&
        (geBumpTextEquals(hit_texture, "HIT_DEFAULT") ||
         geBumpTextEquals(hit_sound, "HIT_DEFAULT"))) {
        return GE_BUMP_MAP_ELIGIBLE;
    }

    return GE_BUMP_MAP_UNTAGGED;
}

static inline const char *geBumpMapEligibilityReason(GeBumpMapEligibility code)
{
    switch (code) {
        case GE_BUMP_MAP_ELIGIBLE:
            return "eligible";
        case GE_BUMP_MAP_INVALID:
            return "invalid-name";
        case GE_BUMP_MAP_TOO_SMALL:
            return "too-small";
        case GE_BUMP_MAP_PRESENTATION:
            return "presentation";
        case GE_BUMP_MAP_UNSAFE_MATERIAL:
            return "unsafe-material";
        case GE_BUMP_MAP_UNTAGGED:
            return "untagged";
    }

    return "unknown";
}

static inline uint8_t geBumpLuminance(uint8_t red, uint8_t green, uint8_t blue)
{
    /* Integer Rec.601 weights avoid platform-dependent colour conversion. */
    const unsigned value = 77u * red + 150u * green + 29u * blue;
    return (uint8_t)((value + 128u) >> 8);
}

static inline void geBumpGenerateHeight(const uint8_t *rgba,
                                        uint32_t width,
                                        uint32_t height,
                                        uint8_t *height_map)
{
    if (rgba == NULL || height_map == NULL || width == 0 || height == 0) {
        return;
    }

    const size_t pixels = (size_t)width * height;
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t *pixel = &rgba[i * 4];
        /* Transparent texels must remain a neutral plane, not a black hole. */
        height_map[i] = pixel[3] < 16
            ? 128
            : geBumpLuminance(pixel[0], pixel[1], pixel[2]);
    }
}

static inline uint8_t geBumpEncodeNormal(float value)
{
    float encoded = (value * 0.5f + 0.5f) * 255.0f;
    if (!__builtin_isfinite(encoded) || encoded <= 0.0f) {
        return 0;
    }
    if (encoded >= 255.0f) {
        return 255;
    }
    return (uint8_t)(encoded + 0.5f);
}

static inline void geBumpGenerateNormal(const uint8_t *height_map,
                                        uint32_t width,
                                        uint32_t height,
                                        float strength,
                                        uint8_t *normal_rgb)
{
    if (normal_rgb == NULL || width == 0 || height == 0) {
        return;
    }

    const size_t pixels = (size_t)width * height;
    if (height_map == NULL || !__builtin_isfinite(strength) || strength < 0.0f) {
        for (size_t i = 0; i < pixels; ++i) {
            normal_rgb[i * 3 + 0] = 128;
            normal_rgb[i * 3 + 1] = 128;
            normal_rgb[i * 3 + 2] = 255;
        }
        return;
    }

    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t down_y = y == 0 ? 0 : y - 1;
        const uint32_t up_y = y + 1 < height ? y + 1 : height - 1;
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t left_x = x == 0 ? 0 : x - 1;
            const uint32_t right_x = x + 1 < width ? x + 1 : width - 1;
            const float gradient_x =
                ((float)height_map[y * width + left_x] -
                 (float)height_map[y * width + right_x]) / 255.0f * strength;
            const float gradient_y =
                ((float)height_map[down_y * width + x] -
                 (float)height_map[up_y * width + x]) / 255.0f * strength;
            const float length = __builtin_sqrtf(gradient_x * gradient_x +
                                                 gradient_y * gradient_y + 1.0f);
            const size_t output = ((size_t)y * width + x) * 3;

            if (!__builtin_isfinite(length) || length <= 0.0f) {
                normal_rgb[output + 0] = 128;
                normal_rgb[output + 1] = 128;
                normal_rgb[output + 2] = 255;
                continue;
            }

            normal_rgb[output + 0] = geBumpEncodeNormal(gradient_x / length);
            normal_rgb[output + 1] = geBumpEncodeNormal(gradient_y / length);
            normal_rgb[output + 2] = geBumpEncodeNormal(1.0f / length);
        }
    }
}

#endif /* GE007_BUMPMAP_POLICY_H */
