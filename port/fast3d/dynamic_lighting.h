#ifndef GE007_DYNAMIC_LIGHTING_H
#define GE007_DYNAMIC_LIGHTING_H

#include <algorithm>
#include <cmath>

/* A port-only point light. Positions and normals are expressed in the same
 * view space as the software RSP's transformed vertices. */
struct GeDynamicLight {
    float position[3];
    float radius;
    float intensity;
};

/* Return the diffuse contribution of light at vertex. The quadratic falloff
 * reaches zero at radius; invalid or back-facing inputs contribute nothing. */
static inline float geDynamicLightIntensity(const float normal[3],
                                            const float vertex[3],
                                            const GeDynamicLight& light)
{
    if (!std::isfinite(light.radius) || !std::isfinite(light.intensity) ||
        light.radius <= 0.0f || light.intensity <= 0.0f) {
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
    return std::clamp(diffuse * falloff * falloff * light.intensity, 0.0f, 1.0f);
}

#endif /* GE007_DYNAMIC_LIGHTING_H */
