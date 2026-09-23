#ifndef GE007_DYNAMIC_LIGHTING_H
#define GE007_DYNAMIC_LIGHTING_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "advanced_lighting_policy.h"

enum class GeDynamicLightType : uint8_t {
    Point = 0,
    Spot = 1,
};

/* Port-only realtime world light. Positions, directions, and normals are
 * expressed in the same view space as the software RSP's transformed
 * vertices. The defaults after intensity keep the original three-field point
 * light initializer source-compatible. */
struct GeDynamicLight {
    float position[3];
    float radius;
    float intensity;
    /* Visibility is supplied by the scene/occlusion adapter. A value of one
     * is the deterministic no-occluder default; zero suppresses both diffuse
     * and specular light transport without duplicating the visibility rule in
     * each shading path. */
    float visibility = 1.0f;

    float color[3] = { 1.0f, 1.0f, 1.0f };
    float direction[3] = { 0.0f, 0.0f, -1.0f };
    float inner_cos = 1.0f;
    float outer_cos = -1.0f;
    GeDynamicLightType type = GeDynamicLightType::Point;
    int priority = 0;
};

static inline bool geDynamicLightFinite3(const float value[3])
{
    return value != nullptr && std::isfinite(value[0]) && std::isfinite(value[1]) &&
           std::isfinite(value[2]);
}

static inline bool geDynamicLightValid(const GeDynamicLight& light)
{
    const bool base_valid =
        geDynamicLightFinite3(light.position) &&
        geDynamicLightFinite3(light.color) &&
        geDynamicLightFinite3(light.direction) &&
        std::isfinite(light.radius) && std::isfinite(light.intensity) &&
        std::isfinite(light.visibility) &&
        std::isfinite(light.inner_cos) && std::isfinite(light.outer_cos) &&
        light.radius > 0.0f && light.intensity > 0.0f &&
        light.visibility >= 0.0f && light.visibility <= 1.0f;
    if (!base_valid) {
        return false;
    }
    return light.type == GeDynamicLightType::Point ||
           (light.type == GeDynamicLightType::Spot &&
            light.inner_cos >= light.outer_cos);
}

static inline float geDynamicLightVisibility(const GeDynamicLight& light)
{
    return geDynamicLightValid(light)
        ? std::clamp(light.visibility, 0.0f, 1.0f)
        : 0.0f;
}

/* Return attenuation and the normalized direction from a vertex to a point
 * or spot light. Keeping this shared prevents diffuse and specular paths from
 * drifting into different radius/cone behavior. */
static inline float geDynamicLightAttenuation(const float vertex[3],
                                              const GeDynamicLight& light,
                                              float light_direction[3])
{
    if (!geDynamicLightValid(light) || !geDynamicLightFinite3(vertex) ||
        light_direction == nullptr) {
        return 0.0f;
    }

    const float to_light[3] = {
        light.position[0] - vertex[0],
        light.position[1] - vertex[1],
        light.position[2] - vertex[2],
    };
    const float distance_squared = to_light[0] * to_light[0] +
                                   to_light[1] * to_light[1] +
                                   to_light[2] * to_light[2];
    if (!std::isfinite(distance_squared) || distance_squared <= 0.0f) {
        return 0.0f;
    }

    const float distance = std::sqrt(distance_squared);
    if (distance >= light.radius) {
        return 0.0f;
    }

    const float inverse_distance = 1.0f / distance;
    light_direction[0] = to_light[0] * inverse_distance;
    light_direction[1] = to_light[1] * inverse_distance;
    light_direction[2] = to_light[2] * inverse_distance;
    const float falloff = 1.0f - distance / light.radius;

    float cone = 1.0f;
    if (light.type == GeDynamicLightType::Spot) {
        const float direction_length_squared =
            light.direction[0] * light.direction[0] +
            light.direction[1] * light.direction[1] +
            light.direction[2] * light.direction[2];
        if (!std::isfinite(direction_length_squared) ||
            direction_length_squared <= 0.0f) {
            return 0.0f;
        }
        const float inverse_direction_length = 1.0f / std::sqrt(direction_length_squared);
        const float to_vertex[3] = {
            -light_direction[0],
            -light_direction[1],
            -light_direction[2],
        };
        const float cosine =
            (light.direction[0] * to_vertex[0] +
             light.direction[1] * to_vertex[1] +
             light.direction[2] * to_vertex[2]) * inverse_direction_length;
        const float outer = std::clamp(light.outer_cos, -1.0f, 1.0f);
        const float inner = std::clamp(light.inner_cos, outer, 1.0f);
        const float span = inner - outer;
        cone = span > 0.0f ? std::clamp((cosine - outer) / span, 0.0f, 1.0f)
                           : (cosine >= inner ? 1.0f : 0.0f);
        cone = cone * cone * (3.0f - 2.0f * cone);
    }

    const float attenuation = falloff * falloff * cone * light.intensity *
                              geDynamicLightVisibility(light);
    return std::isfinite(attenuation)
        ? std::clamp(attenuation, 0.0f, 1.0f)
        : 0.0f;
}

/* Return the diffuse contribution of a point or spot light at a vertex. The
 * quadratic falloff reaches zero at radius; invalid or back-facing inputs
 * contribute nothing. */
static inline float geDynamicLightIntensity(const float normal[3],
                                            const float vertex[3],
                                            const GeDynamicLight& light)
{
    if (!geDynamicLightFinite3(normal)) {
        return 0.0f;
    }

    float light_direction[3] = {};
    const float attenuation = geDynamicLightAttenuation(
        vertex, light, light_direction);
    if (attenuation <= 0.0f) {
        return 0.0f;
    }

    float normalized_normal[3] = {};
    if (!geAdvancedLightingNormalize3(normal, normalized_normal)) {
        return 0.0f;
    }
    const float diffuse = std::max(
        geAdvancedLightingDot3(normalized_normal, light_direction), 0.0f);
    return std::clamp(diffuse * attenuation, 0.0f, 1.0f);
}

/* View-space vertices are expressed relative to the camera origin, so the
 * view direction is simply -vertex. This is the CPU fallback/material path;
 * a future fragment path can call the same pure Blinn-Phong policy with its
 * interpolated position and normal-map normal. */
static inline float geDynamicLightSpecular(
    const float normal[3], const float vertex[3], const GeDynamicLight& light,
    const GeAdvancedLightingMaterial& material)
{
    float light_direction[3] = {};
    const float attenuation = geDynamicLightAttenuation(
        vertex, light, light_direction);
    if (attenuation <= 0.0f || !geDynamicLightFinite3(vertex)) {
        return 0.0f;
    }
    const float view_direction[3] = {-vertex[0], -vertex[1], -vertex[2]};
    const float specular = geBlinnPhongSpecular(
        normal, light_direction, view_direction, material.shininess);
    return std::clamp(specular * attenuation, 0.0f, 1.0f);
}

/* Convert the scalar diffuse response into the RGB contribution consumed by
 * the RSP's existing vertex-colour path. Keeping this in one policy function
 * prevents each renderer/backend from implementing colour and clamping rules
 * independently. */
static inline void geDynamicLightContribution(const float normal[3],
                                              const float vertex[3],
                                              const GeDynamicLight& light,
                                              float contribution[3])
{
    const float intensity = geDynamicLightIntensity(normal, vertex, light);
    for (int channel = 0; channel < 3; ++channel) {
        const float colour = std::isfinite(light.color[channel])
            ? std::max(0.0f, light.color[channel])
            : 0.0f;
        contribution[channel] = std::clamp(intensity * colour, 0.0f, 1.0f);
    }
}

/* Accumulate a bounded linear-light term without routing it through the
 * legacy 8-bit/display-space vertex colour. The fragment boundary performs
 * the final sRGB encoding after all terms have been combined. */
static inline void geDynamicLightAccumulateLinear(float target[3],
                                                  const float contribution[3])
{
    if (target == nullptr || contribution == nullptr) {
        return;
    }
    for (int channel = 0; channel < 3; ++channel) {
        const float existing = std::isfinite(target[channel])
            ? std::max(target[channel], 0.0f) : 0.0f;
        const float incoming = std::isfinite(contribution[channel])
            ? std::max(contribution[channel], 0.0f) : 0.0f;
        target[channel] = std::clamp(existing + incoming, 0.0f, 1.0f);
    }
}

/* Fixed-capacity light registry. It is allocation-free because lights are
 * submitted from the render thread at frame rate. Priority-based replacement
 * makes overflow deterministic: a high-value explosion light can displace a
 * low-priority ambient helper, while equal-priority overflow is rejected. */
class GeDynamicLightRegistry {
public:
    static constexpr std::size_t kCapacity = 16;
    static constexpr std::size_t capacity() { return kCapacity; }

    void clear() { count_ = 0; }

    bool add(const GeDynamicLight& light)
    {
        if (!geDynamicLightValid(light)) {
            return false;
        }
        if (count_ < capacity()) {
            lights_[count_++] = light;
            return true;
        }

        std::size_t weakest = 0;
        for (std::size_t i = 1; i < count_; ++i) {
            if (lights_[i].priority < lights_[weakest].priority) {
                weakest = i;
            }
        }
        if (light.priority <= lights_[weakest].priority) {
            return false;
        }
        lights_[weakest] = light;
        return true;
    }

    std::size_t size() const { return count_; }
    const GeDynamicLight& operator[](std::size_t index) const { return lights_[index]; }

private:
    std::array<GeDynamicLight, kCapacity> lights_{};
    std::size_t count_ = 0;
};

#endif /* GE007_DYNAMIC_LIGHTING_H */
