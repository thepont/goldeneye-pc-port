#ifndef GE007_SHADOW_MAPPING_POLICY_H
#define GE007_SHADOW_MAPPING_POLICY_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "gbiex.h"

struct GeShadowReplaySkip {
    bool ignored;
    std::size_t cursor_advance;
};

/* Return the number of Gfx commands consumed when shadow replay ignores an
 * RDP-only command. Keeping this cursor advance explicit prevents an ignored
 * command from trapping replay on the same display-list entry. */
static inline GeShadowReplaySkip geShadowReplayIgnoredCommand(unsigned opcode)
{
    switch (opcode) {
        case G_TEXRECT:
        case G_TEXRECTFLIP:
        case G_TEXRECT_WIDE_EXT:
        case G_IMAGERECT_EXT:
            return {true, 3};
        case G_FILLRECT_WIDE_EXT:
            return {true, 2};
        case G_FILLRECT:
        case G_SETTIMG_FB_EXT:
        case G_SETFB_EXT:
        case G_COPYFB_EXT:
        case G_INVALTEXCACHE_EXT:
        case G_CLEAR_DEPTH_EXT:
            return {true, 1};
        default:
            return {false, 0};
    }
}

/* Advance the interpreter's real cursor for an ignored packet. Production
 * replay and the bounded stream test share this operation so a policy-only
 * test cannot pass while the display-list cursor remains stationary. */
template <typename Command>
static inline bool geShadowReplayAdvanceIgnoredCommand(Command *&cursor,
                                                       unsigned opcode)
{
    const GeShadowReplaySkip skip = geShadowReplayIgnoredCommand(opcode);
    if (!skip.ignored || cursor == nullptr) {
        return false;
    }
    cursor += skip.cursor_advance;
    return true;
}

/* Shared by caster projection and material shading so both passes use an
 * identical view-space key light and shadow-bias policy. */
static constexpr float geShadowDefaultLightDirection[3] = {
    0.35f, 0.55f, 0.76f,
};
static constexpr float geShadowMinimumBias = 0.0008f;
static constexpr float geShadowSlopeBiasScale = 0.004f;
static constexpr float geShadowMaximumBias = 0.02f;
static constexpr std::size_t geShadowMaximumVertexCount = 1U << 20;

/* Backend-neutral orthographic shadow policy. Matrices are column-major so
 * they can be uploaded directly with glUniformMatrix4fv(..., GL_FALSE, ...).
 * The light direction points from a shaded point toward the light source. */
struct GeShadowMatrix {
    float column_major[16];
};

struct GeShadowProjectedPoint {
    float u;
    float v;
    float depth;
};

static inline bool geShadowFinite3(const float value[3])
{
    return value != nullptr && std::isfinite(value[0]) &&
           std::isfinite(value[1]) && std::isfinite(value[2]);
}

static inline float geShadowDot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline bool geShadowCasterEligible(bool alpha_blended,
                                          bool alpha_cutout,
                                          bool water_material,
                                          float projection_matrix_33)
{
    return !alpha_blended && !alpha_cutout && !water_material &&
           std::isfinite(projection_matrix_33) &&
           std::fabs(projection_matrix_33) < 0.5f;
}

static inline bool geShadowReceiverEligible(bool enhanced_mode,
                                             bool alpha_blended,
                                             bool texture_edge,
                                             bool fog,
                                             bool grayscale)
{
    return enhanced_mode && !alpha_blended && !texture_edge && !fog &&
           !grayscale;
}

static inline bool geShadowNormalize3(const float source[3], float result[3])
{
    if (!geShadowFinite3(source) || result == nullptr) {
        return false;
    }
    const float length_squared = geShadowDot3(source, source);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-12f) {
        return false;
    }
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    for (int axis = 0; axis < 3; ++axis) {
        result[axis] = source[axis] * inverse_length;
    }
    return geShadowFinite3(result);
}

static inline void geShadowCross3(const float a[3], const float b[3],
                                  float result[3])
{
    result[0] = a[1] * b[2] - a[2] * b[1];
    result[1] = a[2] * b[0] - a[0] * b[2];
    result[2] = a[0] * b[1] - a[1] * b[0];
}

/* Build a fitted light-space transform from the submitted scene vertices.
 * Expanding each projected axis by padding_fraction protects edge samples
 * from being clipped while avoiding a fixed, scene-independent frustum. */
static inline bool geShadowBuildDirectionalMatrix(
    const float *view_space_vertices, std::size_t vertex_count,
    const float direction_to_light[3], float padding_fraction,
    GeShadowMatrix *matrix)
{
    if (matrix == nullptr) {
        return false;
    }
    *matrix = {};
    if (view_space_vertices == nullptr || vertex_count < 3 ||
        vertex_count % 3 != 0 || !geShadowFinite3(direction_to_light) ||
        !std::isfinite(padding_fraction) || padding_fraction < 0.0f ||
        padding_fraction > 1.0f) {
        return false;
    }

    float light[3] = {};
    if (!geShadowNormalize3(direction_to_light, light)) {
        return false;
    }
    const float up_reference[3] = {
        0.0f,
        std::fabs(light[1]) > 0.95f ? 0.0f : 1.0f,
        std::fabs(light[1]) > 0.95f ? 1.0f : 0.0f,
    };
    float right[3] = {};
    geShadowCross3(light, up_reference, right);
    /* A finite unit light and the selected non-parallel reference produce a
     * finite, non-zero cross product. The input validation above therefore
     * makes this normalization total for every accepted direction. */
    geShadowNormalize3(right, right);
    float up[3] = {};
    geShadowCross3(right, light, up);
    geShadowNormalize3(up, up);

    float minimum[3] = {
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
    };
    float maximum[3] = {
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    const float *axes[3] = {right, up, light};
    float largest_extent = 0.0f;
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        const float *position = view_space_vertices + vertex * 3;
        if (!geShadowFinite3(position)) {
            return false;
        }
        for (int axis = 0; axis < 3; ++axis) {
            const float coordinate = geShadowDot3(axes[axis], position);
            if (!std::isfinite(coordinate)) {
                return false;
            }
            minimum[axis] = std::min(minimum[axis], coordinate);
            maximum[axis] = std::max(maximum[axis], coordinate);
        }
    }

    for (int axis = 0; axis < 3; ++axis) {
        const float extent = maximum[axis] - minimum[axis];
        largest_extent = std::max(largest_extent, extent);
        const float padding = std::max(extent * padding_fraction, 0.0001f);
        minimum[axis] -= padding;
        maximum[axis] += padding;
        if (!std::isfinite(extent) || !std::isfinite(minimum[axis]) ||
            !std::isfinite(maximum[axis]) ||
            maximum[axis] - minimum[axis] <= 1.0e-6f) {
            return false;
        }
    }
    if (largest_extent <= 1.0e-6f) {
        return false;
    }

    const float range_x = maximum[0] - minimum[0];
    const float range_y = maximum[1] - minimum[1];
    const float range_depth = maximum[2] - minimum[2];
    const float scale_x = 2.0f / range_x;
    const float scale_y = 2.0f / range_y;
    const float scale_depth = -2.0f / range_depth;
    const float translation_x = -(maximum[0] + minimum[0]) / range_x;
    const float translation_y = -(maximum[1] + minimum[1]) / range_y;
    const float translation_depth =
        (maximum[2] + minimum[2]) / range_depth;

    /* Row 0: light-right; row 1: light-up; row 2: near-to-far depth.
     * A larger projection on the light vector is closer to the source. */
    matrix->column_major[0] = right[0] * scale_x;
    matrix->column_major[4] = right[1] * scale_x;
    matrix->column_major[8] = right[2] * scale_x;
    matrix->column_major[12] = translation_x;
    matrix->column_major[1] = up[0] * scale_y;
    matrix->column_major[5] = up[1] * scale_y;
    matrix->column_major[9] = up[2] * scale_y;
    matrix->column_major[13] = translation_y;
    matrix->column_major[2] = light[0] * scale_depth;
    matrix->column_major[6] = light[1] * scale_depth;
    matrix->column_major[10] = light[2] * scale_depth;
    matrix->column_major[14] = translation_depth;
    matrix->column_major[3] = 0.0f;
    matrix->column_major[7] = 0.0f;
    matrix->column_major[11] = 0.0f;
    matrix->column_major[15] = 1.0f;

    for (float value : matrix->column_major) {
        if (!std::isfinite(value)) {
            *matrix = {};
            return false;
        }
    }
    return true;
}

static inline bool geShadowProject(const GeShadowMatrix &matrix,
                                   const float position[3],
                                   GeShadowProjectedPoint *projected)
{
    if (projected == nullptr || !geShadowFinite3(position)) {
        return false;
    }
    const float *m = matrix.column_major;
    const float clip_x = m[0] * position[0] + m[4] * position[1] +
                         m[8] * position[2] + m[12];
    const float clip_y = m[1] * position[0] + m[5] * position[1] +
                         m[9] * position[2] + m[13];
    const float clip_z = m[2] * position[0] + m[6] * position[1] +
                         m[10] * position[2] + m[14];
    const float clip_w = m[3] * position[0] + m[7] * position[1] +
                         m[11] * position[2] + m[15];
    if (!std::isfinite(clip_x) || !std::isfinite(clip_y) ||
        !std::isfinite(clip_z) || !std::isfinite(clip_w) ||
        std::fabs(clip_w) <= 1.0e-8f) {
        return false;
    }
    const float inverse_w = 1.0f / clip_w;
    projected->u = clip_x * inverse_w * 0.5f + 0.5f;
    projected->v = clip_y * inverse_w * 0.5f + 0.5f;
    projected->depth = clip_z * inverse_w * 0.5f + 0.5f;
    return std::isfinite(projected->u) && std::isfinite(projected->v) &&
           std::isfinite(projected->depth);
}

static inline bool geShadowProjectedPointOutside(const float ndc[3])
{
    if (ndc == nullptr || !geShadowFinite3(ndc)) {
        return false;
    }
    return ndc[0] < -1.0f || ndc[0] > 1.0f ||
           ndc[1] < -1.0f || ndc[1] > 1.0f ||
           ndc[2] < -1.0f || ndc[2] > 1.0f;
}

static inline float geShadowSlopeBias(float normal_light_dot,
                                      float minimum_bias,
                                      float slope_scale,
                                      float maximum_bias)
{
    if (!std::isfinite(normal_light_dot) ||
        !std::isfinite(minimum_bias) || !std::isfinite(slope_scale) ||
        !std::isfinite(maximum_bias) || minimum_bias < 0.0f ||
        slope_scale < 0.0f || maximum_bias < minimum_bias) {
        return 0.0f;
    }
    const float facing = std::clamp(normal_light_dot, 0.0f, 1.0f);
    return std::clamp(std::max(minimum_bias,
                               slope_scale * (1.0f - facing)),
                      minimum_bias, maximum_bias);
}

/* Return true when the receiver is behind the nearest stored light-space
 * depth. Invalid numbers fail open so malformed inputs cannot black out a
 * surface. */
static inline bool geShadowIsOccluded(float receiver_depth,
                                      float closest_depth, float bias)
{
    if (!std::isfinite(receiver_depth) || !std::isfinite(closest_depth) ||
        !std::isfinite(bias)) {
        return false;
    }
    return receiver_depth - std::max(bias, 0.0f) > closest_depth;
}

/* Return the fraction of shadowed comparisons for one 3x3 PCF footprint. */
static inline float geShadowPcf3x3(float receiver_depth, float bias,
                                   const float samples[9])
{
    if (!std::isfinite(receiver_depth) || !std::isfinite(bias) ||
        samples == nullptr) {
        return 0.0f;
    }
    unsigned int shadowed = 0;
    for (int sample = 0; sample < 9; ++sample) {
        if (geShadowIsOccluded(receiver_depth, samples[sample], bias)) {
            ++shadowed;
        }
    }
    return static_cast<float>(shadowed) / 9.0f;
}

#endif /* GE007_SHADOW_MAPPING_POLICY_H */
