#ifndef GE007_GFX_WATER_CPU_REFERENCE_H
#define GE007_GFX_WATER_CPU_REFERENCE_H

/* Deterministic, ROM-free water reference renderer.
 *
 * This is the functional core for water composition: it has no SDL, OpenGL,
 * game globals, allocation, wall clock, or random source.  The live backend
 * remains responsible for texture sampling and framebuffer ownership, while
 * this small reference scene makes the geometry/depth/reflection contracts
 * executable in a normal unit-test process. */

#include "gfx_water_composite_policy.h"

#include <stddef.h>
#include <stdint.h>

typedef struct GeWaterCpuReferenceScene {
    uint32_t width;
    uint32_t height;
    int render_mode;
    float water_line;
    float surface_depth;
    float occluder_left;
    float occluder_right;
    float occluder_top;
    float occluder_bottom;
    float occluder_depth;
} GeWaterCpuReferenceScene;

static inline GeWaterCpuReferenceScene geWaterCpuReferenceDefaultScene(void)
{
    const GeWaterCpuReferenceScene scene = {
        96U, 64U, GE_WATER_COMPOSITE_REMASTER,
        0.50f, 8.0f,
        0.35f, 0.65f, 0.34f, 0.74f, 6.5f,
    };
    return scene;
}

static inline float geWaterCpuReferenceClamp(float value)
{
    return geWaterClamp01(value);
}

static inline GeWaterVec3 geWaterCpuReferenceColorClamp(GeWaterVec3 color)
{
    const GeWaterVec3 bounded = {
        geWaterCpuReferenceClamp(color.x),
        geWaterCpuReferenceClamp(color.y),
        geWaterCpuReferenceClamp(color.z),
    };
    return bounded;
}

static inline int geWaterCpuReferenceInsideOccluder(
    const GeWaterCpuReferenceScene *scene, float u, float v)
{
    return scene != NULL && u >= scene->occluder_left &&
           u <= scene->occluder_right && v >= scene->occluder_top &&
           v <= scene->occluder_bottom;
}

/* Analytic scene colour stands in for the opaque world copy.  The rectangular
 * ship/dam fixture is deliberately above and below the water line so its
 * reflected silhouette and depth-tested occlusion are both observable. */
static inline GeWaterVec3 geWaterCpuReferenceSceneColor(
    const GeWaterCpuReferenceScene *scene, float u, float v)
{
    if (scene == NULL || !geWaterFinite(u) || !geWaterFinite(v)) {
        const GeWaterVec3 black = {0.0f, 0.0f, 0.0f};
        return black;
    }

    const float horizon = geWaterClamp01(1.0f - v);
    GeWaterVec3 color = {
        0.035f + 0.12f * horizon,
        0.080f + 0.22f * horizon,
        0.150f + 0.38f * horizon,
    };
    if (geWaterCpuReferenceInsideOccluder(scene, u, v)) {
        const float highlight =
            geWaterCompositeSmoothStep(scene->occluder_top,
                                        scene->occluder_top + 0.10f, v);
        color = {
            0.52f + 0.12f * highlight,
            0.16f + 0.10f * highlight,
            0.045f + 0.03f * highlight,
        };
    }
    return color;
}

static inline float geWaterCpuReferenceSceneDepth(
    const GeWaterCpuReferenceScene *scene, float u, float v)
{
    if (scene == NULL || !geWaterFinite(u) || !geWaterFinite(v)) {
        return 0.0f;
    }
    return geWaterCpuReferenceInsideOccluder(scene, u, v)
               ? scene->occluder_depth
               : 14.0f;
}

static inline GeWaterVec3 geWaterCpuReferenceMix(GeWaterVec3 left,
                                                   GeWaterVec3 right,
                                                   float amount)
{
    amount = geWaterClamp01(amount);
    const GeWaterVec3 mixed = {
        left.x + (right.x - left.x) * amount,
        left.y + (right.y - left.y) * amount,
        left.z + (right.z - left.z) * amount,
    };
    return mixed;
}

static inline uint8_t geWaterCpuReferenceByte(float value)
{
    /* The functional core stores material/scene values in linear RGB.  PPM
     * and screenshot consumers expect display-space bytes, matching the
     * explicit transfer in the live RGB8 composite target. */
    const float bounded = geWaterLinearToSrgb(
        geWaterCpuReferenceClamp(value));
    const float scaled = bounded * 255.0f + 0.5f;
    return (uint8_t)__builtin_floorf(scaled);
}

static inline GeWaterVec3 geWaterCpuReferenceWaterColor(
    const GeWaterCpuReferenceScene *scene, float u, float v, float time)
{
    const int render_mode = scene != NULL ? scene->render_mode : 0;
    const float wave_scale = geWaterCompositeWaveScale(render_mode);
    const GeWaterVec2 wave_uv = {u * 1.7f, v * 1.7f};
    const GeWaterVec2 direction = {
        GE_WATER_DEFAULT_DIRECTION_X,
        GE_WATER_DEFAULT_DIRECTION_Y,
    };
    const GeWaterWaveSample wave = geWaterCompositeWaveSample(
        wave_uv, time, GE_WATER_DEFAULT_WAVELENGTH,
        GE_WATER_DEFAULT_AMPLITUDE, GE_WATER_DEFAULT_SPEED,
        wave_scale, direction);

    const GeWaterVec3 incident = geWaterCompositeVectorNormalize({
        (u - 0.5f) * 0.70f,
        (v - (scene != NULL ? scene->water_line : 0.5f)) * 0.50f,
        -1.0f,
    });
    const GeWaterVec3 normal = geWaterCompositeVectorNormalize({
        -wave.derivative_x * 0.40f,
        -wave.derivative_y * 0.40f,
        1.0f,
    });
    const GeWaterVec3 reflected = geWaterCompositeReflectVector(
        incident, normal);
    const GeWaterVec3 refracted = geWaterCompositeRefractVector(
        incident, normal, GE_WATER_REFRACTION_AIR_TO_WATER);
    const GeWaterVec2 reflected_offset = geWaterCompositeRayScreenOffset(
        incident, reflected);
    const GeWaterVec2 refracted_offset = geWaterCompositeRayScreenOffset(
        incident, refracted);

    const float reflected_v = geWaterCompositeReflectV(
        v, scene != NULL ? scene->water_line : 0.5f);
    const float reflection_u = geWaterCompositeSampleUv(
        u + reflected_offset.x * 0.06f + wave.derivative_x * 0.045f);
    const float reflection_v = geWaterCompositeSampleUv(
        reflected_v + reflected_offset.y * 0.06f + wave.derivative_y * 0.045f);
    const float refraction_u = geWaterCompositeSampleUv(
        u + refracted_offset.x * 0.045f + wave.derivative_x * 0.035f);
    const float refraction_v = geWaterCompositeSampleUv(
        v + refracted_offset.y * 0.045f + wave.derivative_y * 0.035f);

    const GeWaterVec3 reflection = geWaterCpuReferenceSceneColor(
        scene, reflection_u, reflection_v);
    const GeWaterVec3 refraction = geWaterCpuReferenceSceneColor(
        scene, refraction_u, refraction_v);
    const float facing = geWaterClamp01(geWaterCompositeVectorDot({
        -incident.x, -incident.y, -incident.z,
    }, normal));
    const float fresnel = geWaterCompositeFresnel(
        facing, GE_WATER_BASE_REFLECTANCE);
    const float reflection_weight = geWaterCompositeReflectionWeight(
        render_mode, fresnel);
    const float scene_depth = geWaterCpuReferenceSceneDepth(scene, u, v);
    const float surface_depth = scene != NULL ? scene->surface_depth : 8.0f;
    const float thickness = geWaterCompositeWaterThickness(
        surface_depth, scene_depth);
    const float transmission = __builtin_expf(-0.035f * thickness);
    const float refraction_weight = geWaterClamp01(
        (1.0f - reflection_weight) * transmission);
    const float weight_sum = geWaterClamp01(
        reflection_weight + refraction_weight);
    const float deep_factor = geWaterClamp01(thickness * 0.04f);
    const GeWaterVec3 deep_water = {
        0.008f + 0.007f * deep_factor,
        0.055f + 0.145f * deep_factor,
        0.085f + 0.195f * deep_factor,
    };
    const GeWaterVec3 base = {
        0.20f + 0.06f * geWaterClamp01(0.5f + wave.height * 16.0f),
        0.44f + 0.10f * geWaterClamp01(0.5f + wave.height * 16.0f),
        0.62f + 0.13f * geWaterClamp01(0.5f + wave.height * 16.0f),
    };
    GeWaterVec3 water = geWaterCpuReferenceMix(
        deep_water, refraction, refraction_weight);
    water = geWaterCpuReferenceMix(water, reflection, reflection_weight);
    water.x *= 0.68f + base.x * 0.72f;
    water.y *= 0.68f + base.y * 0.72f;
    water.z *= 0.68f + base.z * 0.72f;

    /* A bounded phase response makes the CPU reference visibly exercise the
     * fixed-step clock even when an 8-bit image would quantize tiny normal
     * changes away. It represents the animated micro-specular layer in the
     * production material, not a second scene or random noise source. */
    const float phase_response = geWaterClamp01(
        0.5f + 0.5f * wave.height / (wave_scale > 0.001f ? wave_scale : 1.0f));
    water.x += 0.010f * phase_response;
    water.y += 0.035f * phase_response;
    water.z += 0.060f * phase_response;
    water = geWaterCompositeApplyVisibilityFloor(water, thickness);
    return geWaterCpuReferenceColorClamp(water);
}

static inline int geWaterCpuReferenceRender(
    const GeWaterCpuReferenceScene *scene, float time, uint8_t *rgb,
    size_t rgb_size)
{
    if (scene == NULL || rgb == NULL || scene->width == 0 ||
        scene->height == 0 || !geWaterFinite(time) ||
        rgb_size < (size_t)scene->width * scene->height * 3 ||
        !geWaterFinite(scene->water_line) || scene->water_line <= 0.0f ||
        scene->water_line >= 1.0f || scene->surface_depth <= 0.0f) {
        return 0;
    }

    for (uint32_t y = 0; y < scene->height; ++y) {
        const float v = ((float)y + 0.5f) / (float)scene->height;
        for (uint32_t x = 0; x < scene->width; ++x) {
            const float u = ((float)x + 0.5f) / (float)scene->width;
            GeWaterVec3 color = geWaterCpuReferenceSceneColor(scene, u, v);
            const float scene_depth = geWaterCpuReferenceSceneDepth(
                scene, u, v);
            if (v > scene->water_line &&
                !geWaterCompositeDepthIsOccluded(
                    scene->surface_depth, scene_depth,
                    GE_WATER_DEPTH_EPSILON)) {
                color = geWaterCpuReferenceWaterColor(scene, u, v, time);
            }
            const size_t offset =
                ((size_t)y * scene->width + x) * 3;
            rgb[offset + 0] = geWaterCpuReferenceByte(color.x);
            rgb[offset + 1] = geWaterCpuReferenceByte(color.y);
            rgb[offset + 2] = geWaterCpuReferenceByte(color.z);
        }
    }
    return 1;
}

#endif /* GE007_GFX_WATER_CPU_REFERENCE_H */
