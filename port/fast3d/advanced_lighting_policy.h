#ifndef GE007_ADVANCED_LIGHTING_POLICY_H
#define GE007_ADVANCED_LIGHTING_POLICY_H

#include <algorithm>
#include <cmath>

/*
 * Functional core for the port's advanced lighting path.
 *
 * The renderer owns material/texture transport; this header owns the
 * normalized-vector and Blinn-Phong invariants. Keeping the policy free of
 * OpenGL, clocks, allocation, and renderer state makes the same equations
 * usable by CPU fixtures and future fragment-lighting adapters.
 */
static constexpr float GE_ADVANCED_SHININESS_MIN = 1.0f;
/* Keep the material exponent in the range supported by both the CPU policy
 * and the lowest common GLSL profile used by the generated compatibility
 * shader. */
static constexpr float GE_ADVANCED_SHININESS_MAX = 128.0f;

struct GeAdvancedLightingMaterial {
    float specular_color[3];
    float shininess;
};

static inline GeAdvancedLightingMaterial geAdvancedLightingDefaultMaterial()
{
    return {{1.0f, 1.0f, 1.0f}, 32.0f};
}

static inline float geAdvancedLightingDisplayToLinear(float value)
{
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

static inline float geAdvancedLightingLinearToDisplay(float value)
{
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

static inline bool geAdvancedLightingFinite3(const float value[3])
{
    return value != nullptr && std::isfinite(value[0]) &&
           std::isfinite(value[1]) && std::isfinite(value[2]);
}

static inline float geAdvancedLightingDot3(const float left[3],
                                           const float right[3])
{
    if (!geAdvancedLightingFinite3(left) ||
        !geAdvancedLightingFinite3(right)) {
        return 0.0f;
    }
    const float result = left[0] * right[0] + left[1] * right[1] +
                         left[2] * right[2];
    return std::isfinite(result) ? result : 0.0f;
}

static inline bool geAdvancedLightingNormalize3(const float input[3],
                                                float output[3])
{
    if (output == nullptr || !geAdvancedLightingFinite3(input)) {
        return false;
    }
    const float length_squared = geAdvancedLightingDot3(input, input);
    if (!std::isfinite(length_squared) || length_squared <= 0.000001f) {
        output[0] = output[1] = 0.0f;
        output[2] = 1.0f;
        return false;
    }
    const float length = std::sqrt(length_squared);
    /* length_squared is finite and strictly positive above, so its square
     * root is finite and nonzero. Keep this invariant explicit rather than
     * carrying an unreachable duplicate failure branch. */
    output[0] = input[0] / length;
    output[1] = input[1] / length;
    output[2] = input[2] / length;
    return std::isfinite(output[0]) && std::isfinite(output[1]) &&
           std::isfinite(output[2]);
}

static inline bool geAdvancedLightingSanitizeShininess(float input,
                                                       float *output)
{
    if (output == nullptr || !std::isfinite(input) || input <= 0.0f) {
        return false;
    }
    *output = std::clamp(input, GE_ADVANCED_SHININESS_MIN,
                         GE_ADVANCED_SHININESS_MAX);
    return true;
}

/*
 * Blinn-Phong specular response.
 *
 * `light_direction` and `view_direction` point away from the shaded point.
 * The view-space renderer supplies the camera direction from the vertex or
 * fragment to the origin. Back-facing lights are rejected before evaluating
 * the halfway vector, preventing a specular highlight through a wall.
 */
static inline float geBlinnPhongSpecular(const float normal[3],
                                         const float light_direction[3],
                                         const float view_direction[3],
                                         float shininess)
{
    float bounded_shininess = 0.0f;
    if (!geAdvancedLightingSanitizeShininess(shininess,
                                             &bounded_shininess)) {
        return 0.0f;
    }

    float normalized_normal[3] = {};
    float normalized_light[3] = {};
    float normalized_view[3] = {};
    if (!geAdvancedLightingNormalize3(normal, normalized_normal) ||
        !geAdvancedLightingNormalize3(light_direction, normalized_light) ||
        !geAdvancedLightingNormalize3(view_direction, normalized_view)) {
        return 0.0f;
    }

    const float normal_light = geAdvancedLightingDot3(
        normalized_normal, normalized_light);
    if (!std::isfinite(normal_light) || normal_light <= 0.0f) {
        return 0.0f;
    }

    const float halfway[3] = {
        normalized_light[0] + normalized_view[0],
        normalized_light[1] + normalized_view[1],
        normalized_light[2] + normalized_view[2],
    };
    float normalized_halfway[3] = {};
    if (!geAdvancedLightingNormalize3(halfway, normalized_halfway)) {
        return 0.0f;
    }

    const float normal_halfway = std::clamp(
        geAdvancedLightingDot3(normalized_normal, normalized_halfway),
        0.0f, 1.0f);
    const float result = std::pow(normal_halfway, bounded_shininess);
    return std::isfinite(result) ? std::clamp(result, 0.0f, 1.0f) : 0.0f;
}

#endif /* GE007_ADVANCED_LIGHTING_POLICY_H */
