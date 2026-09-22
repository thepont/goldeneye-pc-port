#ifndef GE007_DYNAMIC_LIGHTING_H
#define GE007_DYNAMIC_LIGHTING_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

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

    float color[3] = { 1.0f, 1.0f, 1.0f };
    float direction[3] = { 0.0f, 0.0f, -1.0f };
    float inner_cos = 1.0f;
    float outer_cos = -1.0f;
    GeDynamicLightType type = GeDynamicLightType::Point;
    int priority = 0;
};

static inline bool geDynamicLightFinite3(const float value[3])
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
           std::isfinite(value[2]);
}

static inline bool geDynamicLightValid(const GeDynamicLight& light)
{
    const bool base_valid =
        geDynamicLightFinite3(light.position) &&
        geDynamicLightFinite3(light.color) &&
        geDynamicLightFinite3(light.direction) &&
        std::isfinite(light.radius) && std::isfinite(light.intensity) &&
        std::isfinite(light.inner_cos) && std::isfinite(light.outer_cos) &&
        light.radius > 0.0f && light.intensity > 0.0f;
    if (!base_valid) {
        return false;
    }
    return light.type == GeDynamicLightType::Point ||
           (light.type == GeDynamicLightType::Spot &&
            light.inner_cos >= light.outer_cos);
}

/* Return the diffuse contribution of a point or spot light at a vertex. The
 * quadratic falloff reaches zero at radius; invalid or back-facing inputs
 * contribute nothing. */
static inline float geDynamicLightIntensity(const float normal[3],
                                            const float vertex[3],
                                            const GeDynamicLight& light)
{
    if (!geDynamicLightValid(light) || !geDynamicLightFinite3(normal) ||
        !geDynamicLightFinite3(vertex)) {
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

    const float normal_length_squared = normal[0] * normal[0] +
                                        normal[1] * normal[1] +
                                        normal[2] * normal[2];
    if (!std::isfinite(normal_length_squared) || normal_length_squared <= 0.0f) {
        return 0.0f;
    }

    const float inverse_distance = 1.0f / distance;
    const float inverse_normal_length = 1.0f / std::sqrt(normal_length_squared);
    const float diffuse = std::max(0.0f,
        (normal[0] * to_light[0] + normal[1] * to_light[1] +
         normal[2] * to_light[2]) * inverse_normal_length * inverse_distance);
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
            -to_light[0] * inverse_distance,
            -to_light[1] * inverse_distance,
            -to_light[2] * inverse_distance,
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

    const float contribution = diffuse * falloff * falloff * cone * light.intensity;
    return std::clamp(contribution, 0.0f, 1.0f);
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
