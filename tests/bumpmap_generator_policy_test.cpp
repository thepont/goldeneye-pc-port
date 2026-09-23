#include "../tools/mktex/src/bumpmap_policy.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

static void geTestRequire(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

#undef assert
#define assert(condition) geTestRequire((condition))

static void expect_rgb(const uint8_t *rgb, uint8_t r, uint8_t g, uint8_t b)
{
    assert(rgb[0] == r);
    assert(rgb[1] == g);
    assert(rgb[2] == b);
}

static void expect_flat_normals(const std::vector<uint8_t> &normal)
{
    assert(normal.size() % 3 == 0);
    for (size_t i = 0; i < normal.size(); i += 3) {
        expect_rgb(&normal[i], 128, 128, 255);
    }
}

int main()
{
    assert(geBumpTextEquals(nullptr, "HIT_STONE") == 0);
    assert(geBumpTextEquals("HIT_STONE", nullptr) == 0);
    assert(geBumpTextEquals("", "") == 1);
    assert(geBumpTextEquals("", "HIT_STONE") == 0);
    assert(geBumpTextEquals("HIT_STONE", "") == 0);
    assert(geBumpTextContains(nullptr, "STONE") == 0);
    assert(geBumpTextContains("STONE", nullptr) == 0);
    assert(geBumpTextContains("STONE", "") == 0);
    assert(geBumpTextContains("STONE", "GLASS") == 0);
    assert(geBumpNameIsPresentationOnly(nullptr) == 0);
    assert(geBumpMaterialIsPhysical(nullptr) == 0);
    assert(geBumpMaterialIsUnsafe(nullptr) == 0);

    assert(geBumpMapEligibility("AZTEC_88", "HIT_STONE", "HIT_STONE",
                                 32, 32, false) == GE_BUMP_MAP_ELIGIBLE);
    assert(geBumpMapEligibility("GAZ_DOOR", "HIT_METAL", "HIT_METAL",
                                 32, 32, false) == GE_BUMP_MAP_ELIGIBLE);
    assert(geBumpMapEligibility("UNMARKED_WALL", "HIT_DEFAULT", "HIT_DEFAULT",
                                 32, 32, false) == GE_BUMP_MAP_UNTAGGED);
    assert(geBumpMapEligibility("UNMARKED_WALL", "HIT_DEFAULT", "HIT_DEFAULT",
                                 32, 32, true) == GE_BUMP_MAP_ELIGIBLE);
    assert(geBumpMapEligibility("CROSSHAIR", "HIT_STONE", "HIT_STONE",
                                 32, 32, true) == GE_BUMP_MAP_PRESENTATION);
    assert(geBumpMapEligibility("IMAGE_FIRE_0", "HIT_METAL", "HIT_METAL",
                                 32, 32, true) == GE_BUMP_MAP_PRESENTATION);
    assert(geBumpMapEligibility("BOND_HEAD", "HIT_CHR", "HIT_CHR",
                                 64, 64, true) == GE_BUMP_MAP_UNSAFE_MATERIAL);
    assert(geBumpMapEligibility("WATER_SURFACE", "HIT_WATER", "HIT_WATER",
                                 64, 64, true) == GE_BUMP_MAP_UNSAFE_MATERIAL);
    assert(geBumpMapEligibility("TINY_STONE", "HIT_STONE", "HIT_STONE",
                                 3, 32, false) == GE_BUMP_MAP_TOO_SMALL);
    assert(geBumpMapEligibility("TINY_STONE", "HIT_STONE", "HIT_STONE",
                                 32, 3, false) == GE_BUMP_MAP_TOO_SMALL);
    assert(geBumpMapEligibility("UNKNOWN", "HIT_UNKNOWN", "HIT_UNKNOWN",
                                 32, 32, false) == GE_BUMP_MAP_UNTAGGED);
    assert(geBumpMapEligibility(nullptr, "HIT_STONE", "HIT_STONE",
                                 32, 32, false) == GE_BUMP_MAP_INVALID);
    assert(geBumpMapEligibility("", "HIT_STONE", "HIT_STONE",
                                 32, 32, false) == GE_BUMP_MAP_INVALID);
    assert(geBumpMapEligibility("UNMARKED_WALL", "HIT_UNKNOWN", "HIT_DEFAULT",
                                 32, 32, true) == GE_BUMP_MAP_ELIGIBLE);

    assert(geBumpLuminance(255, 0, 0) == 77);
    assert(geBumpLuminance(0, 255, 0) == 149);
    assert(geBumpLuminance(0, 0, 255) == 29);
    assert(geBumpLuminance(255, 255, 255) == 255);

    const std::vector<uint8_t> flat_rgba = {
        128, 128, 128, 255,
        128, 128, 128, 255,
        128, 128, 128, 255,
        128, 128, 128, 255,
    };
    std::vector<uint8_t> flat_height(4, 0);
    geBumpGenerateHeight(flat_rgba.data(), 2, 2, flat_height.data());
    assert(flat_height == std::vector<uint8_t>({128, 128, 128, 128}));
    geBumpGenerateHeight(flat_rgba.data(), 2, 2, nullptr);

    std::vector<uint8_t> flat_normals(4 * 3, 0);
    geBumpGenerateNormal(flat_height.data(), 2, 2, 2.5f,
                         flat_normals.data());
    expect_flat_normals(flat_normals);

    const std::vector<uint8_t> transparent_rgba = {
        255, 0, 0, 0,
        0, 0, 0, 255,
        0, 0, 0, 255,
        0, 0, 0, 255,
    };
    std::vector<uint8_t> transparent_height(4, 0);
    geBumpGenerateHeight(transparent_rgba.data(), 2, 2,
                         transparent_height.data());
    assert(transparent_height[0] == 128);

    const std::vector<uint8_t> horizontal_rgba = {
        0, 0, 0, 255,
        128, 128, 128, 255,
        255, 255, 255, 255,
    };
    std::vector<uint8_t> horizontal_height(3, 0);
    geBumpGenerateHeight(horizontal_rgba.data(), 3, 1,
                         horizontal_height.data());
    assert(horizontal_height == std::vector<uint8_t>({0, 128, 255}));

    std::vector<uint8_t> horizontal_normals(3 * 3, 0);
    geBumpGenerateNormal(horizontal_height.data(), 3, 1, 2.5f,
                         horizontal_normals.data());
    assert(horizontal_normals[3] < 128);
    assert(horizontal_normals[4] == 128);
    assert(horizontal_normals[5] > 0);

    std::vector<uint8_t> one_pixel_height(1, 0);
    geBumpGenerateHeight(flat_rgba.data(), 0, 0, one_pixel_height.data());
    assert(one_pixel_height[0] == 0);
    geBumpGenerateNormal(one_pixel_height.data(), 0, 0, 2.5f,
                         one_pixel_height.data());

    std::vector<uint8_t> invalid_normals(3 * 2, 0);
    geBumpGenerateNormal(flat_height.data(), 2, 1, -1.0f,
                         invalid_normals.data());
    expect_flat_normals(invalid_normals);
    geBumpGenerateNormal(flat_height.data(), 2, 1, NAN,
                         invalid_normals.data());
    expect_flat_normals(invalid_normals);
    geBumpGenerateNormal(nullptr, 2, 1, 2.5f, invalid_normals.data());
    geBumpGenerateNormal(flat_height.data(), 2, 1, 2.5f, nullptr);

    assert(geBumpEncodeNormal(-INFINITY) == 0);
    std::vector<uint8_t> overflow_normals(2 * 3, 0);
    const std::vector<uint8_t> overflow_height = {0, 255};
    geBumpGenerateNormal(overflow_height.data(), 2, 1,
                         std::numeric_limits<float>::max(),
                         overflow_normals.data());
    expect_flat_normals(overflow_normals);

    assert(std::strcmp(geBumpMapEligibilityReason(GE_BUMP_MAP_ELIGIBLE),
                       "eligible") == 0);
    assert(std::strcmp(geBumpMapEligibilityReason(GE_BUMP_MAP_INVALID),
                       "invalid-name") == 0);
    assert(std::strcmp(geBumpMapEligibilityReason(GE_BUMP_MAP_TOO_SMALL),
                       "too-small") == 0);
    assert(std::strcmp(geBumpMapEligibilityReason(GE_BUMP_MAP_PRESENTATION),
                       "presentation") == 0);
    assert(std::strcmp(geBumpMapEligibilityReason(GE_BUMP_MAP_UNSAFE_MATERIAL),
                       "unsafe-material") == 0);
    assert(std::strcmp(geBumpMapEligibilityReason(GE_BUMP_MAP_UNTAGGED),
                       "untagged") == 0);
    assert(std::strcmp(geBumpMapEligibilityReason((GeBumpMapEligibility)99),
                       "unknown") == 0);

    return 0;
}
