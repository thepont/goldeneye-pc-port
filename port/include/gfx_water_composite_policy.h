#ifndef GE007_GFX_WATER_COMPOSITE_POLICY_H
#define GE007_GFX_WATER_COMPOSITE_POLICY_H

/* Pure policy shared by the OpenGL water composite and its contract tests.
 * Keep renderer allocation, GL state, and game display-list ownership out of
 * this header so malformed depth/geometry cases can be exercised cheaply. */

#include <stddef.h>

#define GE_WATER_COMPOSITE_DISABLED 0
#define GE_WATER_COMPOSITE_ENHANCED 1
#define GE_WATER_COMPOSITE_REMASTER 2

#define GE_WATER_PHASE_PREWORLD 0
#define GE_WATER_PHASE_WORLD 1
#define GE_WATER_PHASE_COMPOSITE 2
#define GE_WATER_PHASE_HUD 3

#define GE_WATER_DEPTH_EPSILON 0.001f
#define GE_WATER_CONTACT_START 0.002f
#define GE_WATER_CONTACT_BAND_ENHANCED 0.08f
#define GE_WATER_CONTACT_BAND_REMASTER 0.12f
/* The depth copy is linearized before this ramp. A ship/dam edge in the
 * bounded fixture is approximately 0.003 units apart; ending the ramp at
 * 0.05 made that real contact edge read as zero foam. Keep the noise floor
 * above tiny depth quantization while allowing the observed hull/wall edge to
 * reach a visible, bounded response. */
#define GE_WATER_EDGE_START 0.0005f
#define GE_WATER_EDGE_END 0.008f
#define GE_WATER_UV_MARGIN 0.001f
#define GE_WATER_BASE_REFLECTANCE 0.02f
#define GE_WATER_FRESNEL_POWER 5
#define GE_WATER_WAVE_COMPONENTS 2.0f
#define GE_WATER_REFRACTION_AIR_TO_WATER 0.75f
#define GE_WATER_REFRACTION_WATER_TO_AIR 1.3333333f
#define GE_WATER_VECTOR_EPSILON 0.000001f
#define GE_WATER_REFLECTION_OFFSET_SCALE 0.50f
#define GE_WATER_REFRACTION_OFFSET_SCALE 0.50f
#define GE_WATER_REFLECTION_ATLAS_COLUMNS 4U
#define GE_WATER_REFLECTION_ATLAS_ROWS 2U
#define GE_WATER_REFLECTION_ATLAS_CAPACITY \
    (GE_WATER_REFLECTION_ATLAS_COLUMNS * GE_WATER_REFLECTION_ATLAS_ROWS)

/* Low-light water is still expected to read as a surface.  These are linear
 * RGB values, not display-space colours: the floor is deliberately blue/teal
 * and ramps with optical depth so a black scene does not turn the sea into an
 * indistinguishable hole.  Bright reflection/transmission is never reduced
 * by this policy. */
#define GE_WATER_AMBIENT_SHALLOW_R 0.025f
#define GE_WATER_AMBIENT_SHALLOW_G 0.160f
#define GE_WATER_AMBIENT_SHALLOW_B 0.240f
#define GE_WATER_AMBIENT_DEEP_R 0.040f
#define GE_WATER_AMBIENT_DEEP_G 0.240f
#define GE_WATER_AMBIENT_DEEP_B 0.360f
#define GE_WATER_AMBIENT_DEPTH_SCALE 0.040f
#define GE_WATER_MIN_LINEAR_LUMINANCE 0.100f
#define GE_WATER_DEFAULT_WAVELENGTH 0.33f
#define GE_WATER_DEFAULT_AMPLITUDE 1.0f
#define GE_WATER_DEFAULT_SPEED 0.07f
#define GE_WATER_DEFAULT_DIRECTION_X 1.0f
#define GE_WATER_DEFAULT_DIRECTION_Y 0.65f

/* Test-only feature switches.  The live renderer enables every feature by
 * default; the diagnostic environment seams can clear one bit at a time so a
 * dark frame can be reduced to a reflection, refraction, normal, or contact
 * contribution without changing the shipped render mode. */
#define GE_WATER_FEATURE_REFLECTION (1U << 0)
#define GE_WATER_FEATURE_REFRACTION (1U << 1)
#define GE_WATER_FEATURE_NORMALS (1U << 2)
#define GE_WATER_FEATURE_FOAM (1U << 3)
#define GE_WATER_FEATURE_ALL (GE_WATER_FEATURE_REFLECTION | \
                              GE_WATER_FEATURE_REFRACTION | \
                              GE_WATER_FEATURE_NORMALS | \
                              GE_WATER_FEATURE_FOAM)

typedef struct GeWaterVec3 {
    float x;
    float y;
    float z;
} GeWaterVec3;

typedef struct GeWaterVec2 {
    float x;
    float y;
} GeWaterVec2;

/* Declarations keep the colour-transfer helpers near the public value types;
 * the finite/clamp implementations remain with the other scalar policies
 * below. */
static inline int geWaterFinite(float value);
static inline float geWaterClamp01(float value);

/* Water policy values are linear.  The legacy PC presentation targets are
 * ordinary RGB8 buffers rather than guaranteed sRGB framebuffers, so the
 * boundary must be explicit instead of relying on driver state. */
static inline float geWaterLinearToSrgb(float linear)
{
    if (!geWaterFinite(linear)) {
        return 0.0f;
    }
    linear = geWaterClamp01(linear);
    if (linear <= 0.0031308f) {
        return 12.92f * linear;
    }
    const float encoded = 1.055f * __builtin_powf(linear, 1.0f / 2.4f) -
                          0.055f;
    return geWaterClamp01(encoded);
}

static inline float geWaterSrgbToLinear(float srgb)
{
    if (!geWaterFinite(srgb)) {
        return 0.0f;
    }
    srgb = geWaterClamp01(srgb);
    if (srgb <= 0.04045f) {
        return srgb / 12.92f;
    }
    return geWaterClamp01(__builtin_powf((srgb + 0.055f) / 1.055f,
                                         2.4f));
}

static inline GeWaterVec3 geWaterLinearColorToSrgb(GeWaterVec3 linear)
{
    const GeWaterVec3 encoded = {
        geWaterLinearToSrgb(linear.x),
        geWaterLinearToSrgb(linear.y),
        geWaterLinearToSrgb(linear.z),
    };
    return encoded;
}

static inline GeWaterVec3 geWaterSrgbColorToLinear(GeWaterVec3 srgb)
{
    const GeWaterVec3 linear = {
        geWaterSrgbToLinear(srgb.x),
        geWaterSrgbToLinear(srgb.y),
        geWaterSrgbToLinear(srgb.z),
    };
    return linear;
}

static inline int geWaterFeatureEnabled(unsigned int feature_mask,
                                        unsigned int feature)
{
    return (feature_mask & feature) != 0U;
}

typedef struct GeWaterReflectionAtlasRegion {
    float scale_x;
    float scale_y;
    float bias_x;
    float bias_y;
} GeWaterReflectionAtlasRegion;

/* Reflected-camera passes share one bounded texture atlas. Keeping the
 * address calculation here makes the tile ownership testable without a GL
 * context and gives the shader/backend the same row/column convention. */
static inline int geWaterReflectionAtlasRegion(
    unsigned int surface_index, unsigned int surface_count,
    GeWaterReflectionAtlasRegion *region)
{
    if (region == NULL || surface_count == 0 ||
        surface_count > GE_WATER_REFLECTION_ATLAS_CAPACITY ||
        surface_index >= surface_count) {
        return 0;
    }

    const unsigned int column =
        surface_index % GE_WATER_REFLECTION_ATLAS_COLUMNS;
    const unsigned int row =
        surface_index / GE_WATER_REFLECTION_ATLAS_COLUMNS;
    region->scale_x = 1.0f / (float)GE_WATER_REFLECTION_ATLAS_COLUMNS;
    region->scale_y = 1.0f / (float)GE_WATER_REFLECTION_ATLAS_ROWS;
    region->bias_x = (float)column * region->scale_x;
    region->bias_y = (float)row * region->scale_y;
    return 1;
}

static inline int geWaterFinite(float value)
{
    return __builtin_isfinite(value) != 0;
}

static inline float geWaterAbs(float value)
{
    return value < 0.0f ? -value : value;
}

static inline int geWaterCompositeVectorIsFinite(GeWaterVec3 vector)
{
    return geWaterFinite(vector.x) && geWaterFinite(vector.y) &&
           geWaterFinite(vector.z);
}

static inline float geWaterCompositeVectorDot(GeWaterVec3 left,
                                               GeWaterVec3 right)
{
    if (!geWaterCompositeVectorIsFinite(left) ||
        !geWaterCompositeVectorIsFinite(right)) {
        return 0.0f;
    }
    const float dot = left.x * right.x + left.y * right.y + left.z * right.z;
    return geWaterFinite(dot) ? dot : 0.0f;
}

static inline float geWaterCompositeVectorLength(GeWaterVec3 vector)
{
    if (!geWaterCompositeVectorIsFinite(vector)) {
        return 0.0f;
    }

    float scale = geWaterAbs(vector.x);
    const float y_scale = geWaterAbs(vector.y);
    const float z_scale = geWaterAbs(vector.z);
    if (y_scale > scale) scale = y_scale;
    if (z_scale > scale) scale = z_scale;
    if (!geWaterFinite(scale) || scale <= 0.0f) {
        return 0.0f;
    }

    const float scaled_x = vector.x / scale;
    const float scaled_y = vector.y / scale;
    const float scaled_z = vector.z / scale;
    const float scaled_length = __builtin_sqrtf(
        scaled_x * scaled_x + scaled_y * scaled_y + scaled_z * scaled_z);
    const float length = scale * scaled_length;
    return geWaterFinite(length) ? length : 0.0f;
}

static inline GeWaterVec3 geWaterCompositeVectorNormalize(GeWaterVec3 vector)
{
    const float length = geWaterCompositeVectorLength(vector);
    if (!geWaterFinite(length) || length <= GE_WATER_VECTOR_EPSILON) {
        const GeWaterVec3 zero = {0.0f, 0.0f, 0.0f};
        return zero;
    }

    const GeWaterVec3 normalized = {
        vector.x / length,
        vector.y / length,
        vector.z / length,
    };
    return normalized;
}

/* GLSL's normalize() is undefined for a zero vector and can propagate a
 * non-finite uniform through every wave sample. GPU Gems' water examples
 * normalize the view/bump directions before using them for projected samples;
 * keep the same finite +X fallback in the backend-neutral policy so malformed
 * direction inputs cannot turn the material into a NaN source. */
static inline GeWaterVec2 geWaterCompositeSafeNormalize2(GeWaterVec2 vector)
{
    const GeWaterVec2 fallback = {1.0f, 0.0f};
    if (!geWaterFinite(vector.x) || !geWaterFinite(vector.y) ||
        geWaterAbs(vector.x) >= 1000000.0f ||
        geWaterAbs(vector.y) >= 1000000.0f) { return fallback; }

    float scale = geWaterAbs(vector.x);
    const float y_scale = geWaterAbs(vector.y);
    if (y_scale > scale) { scale = y_scale; }
    if (!geWaterFinite(scale) || scale <= 0.0f) {
        return fallback;
    }

    const float scaled_x = vector.x / scale;
    const float scaled_y = vector.y / scale;
    const float scaled_length = __builtin_sqrtf(
        scaled_x * scaled_x + scaled_y * scaled_y);
    const float length = scale * scaled_length;
    if (!geWaterFinite(length) || length <= GE_WATER_VECTOR_EPSILON) { return fallback; }

    const GeWaterVec2 normalized = {
        vector.x / length,
        vector.y / length,
    };
    return geWaterFinite(normalized.x) && geWaterFinite(normalized.y)
               ? normalized
               : fallback;
}

typedef struct GeWaterWaveSample {
    float height;
    float derivative_x;
    float derivative_y;
} GeWaterWaveSample;

/* CPU/reference form of the two-axis wave used by the generated GLSL.  Keep
 * the phase and analytic derivatives in one policy function so the ROM-free
 * harness and unit tests exercise the same wave contract as the renderer.
 * Invalid inputs are deliberately converted to a flat, finite sample. */
static inline GeWaterWaveSample geWaterCompositeWaveSample(
    GeWaterVec2 uv, float time, float wavelength, float amplitude,
    float speed, float scale, GeWaterVec2 direction)
{
    GeWaterWaveSample zero = {0.0f, 0.0f, 0.0f};
    if (!geWaterFinite(uv.x) || !geWaterFinite(uv.y) ||
        !geWaterFinite(time) || !geWaterFinite(wavelength) ||
        !geWaterFinite(amplitude) || !geWaterFinite(speed) ||
        !geWaterFinite(scale) || scale < 0.0f) {
        return zero;
    }

    const float safe_length = wavelength > 0.001f ? wavelength : 0.001f;
    const GeWaterVec2 unit_direction =
        geWaterCompositeSafeNormalize2(direction);
    const GeWaterVec2 cross_direction = {
        -unit_direction.y, unit_direction.x,
    };
    const float frequency = 6.28318530717958647692f / safe_length;
    const float p0 = (uv.x * unit_direction.x + uv.y * unit_direction.y) *
                         frequency + time * speed;
    const float p1 = (uv.x * cross_direction.x + uv.y * cross_direction.y) *
                         frequency * 0.73f - time * speed * 0.63f;
    const float p2 = (uv.x * (unit_direction.x + cross_direction.x) +
                      uv.y * (unit_direction.y + cross_direction.y)) *
                         frequency * 1.41f + time * speed * 0.81f;
    const float common = amplitude * scale;
    GeWaterWaveSample sample = {
        common * (0.50f * __builtin_sinf(p0) +
                  0.30f * __builtin_cosf(p1) +
                  0.20f * __builtin_sinf(p2)),
        common * frequency *
            (0.50f * unit_direction.x * __builtin_cosf(p0) -
             0.30f * cross_direction.x * __builtin_sinf(p1) +
             0.20f * (unit_direction.x + cross_direction.x) *
                 __builtin_cosf(p2) * 1.41f),
        common * frequency *
            (0.50f * unit_direction.y * __builtin_cosf(p0) -
             0.30f * cross_direction.y * __builtin_sinf(p1) +
             0.20f * (unit_direction.y + cross_direction.y) *
                 __builtin_cosf(p2) * 1.41f),
    };
    return geWaterFinite(sample.height) &&
                   geWaterFinite(sample.derivative_x) &&
                   geWaterFinite(sample.derivative_y)
               ? sample
               : zero;
}

static inline int geWaterCompositeRayIsProjectable(GeWaterVec3 ray)
{
    return geWaterCompositeVectorIsFinite(ray) &&
           ray.z < -GE_WATER_VECTOR_EPSILON &&
           geWaterFinite(ray.x / -ray.z) &&
           geWaterFinite(ray.y / -ray.z);
}

/* Project a view-space ray onto the camera's unit-depth screen plane. The
 * renderer's water UVs represent screen x/y, so using x/z here would mix the
 * depth axis into the vertical texture coordinate and make reflections read
 * as arbitrary warping. */
static inline GeWaterVec2 geWaterCompositeProjectRayToScreen(GeWaterVec3 ray)
{
    if (!geWaterCompositeRayIsProjectable(ray)) {
        const GeWaterVec2 zero = {0.0f, 0.0f};
        return zero;
    }
    const GeWaterVec2 projected = {
        ray.x / -ray.z,
        ray.y / -ray.z,
    };
    return projected;
}

static inline GeWaterVec2 geWaterCompositeRayScreenOffset(GeWaterVec3 incident,
                                                           GeWaterVec3 bent)
{
    if (!geWaterCompositeRayIsProjectable(incident) ||
        !geWaterCompositeRayIsProjectable(bent)) {
        const GeWaterVec2 zero = {0.0f, 0.0f};
        return zero;
    }
    const GeWaterVec2 incident_screen = geWaterCompositeProjectRayToScreen(incident);
    const GeWaterVec2 bent_screen = geWaterCompositeProjectRayToScreen(bent);
    const GeWaterVec2 offset = {
        bent_screen.x - incident_screen.x,
        bent_screen.y - incident_screen.y,
    };
    if (!geWaterFinite(offset.x) || !geWaterFinite(offset.y)) {
        const GeWaterVec2 zero = {0.0f, 0.0f};
        return zero;
    }
    return offset;
}

/* These are the same normalized-vector contracts used by GLSL reflect() and
 * refract(). Keeping the reference implementation here makes the screen-space
 * approximation testable without requiring an OpenGL context. */
static inline GeWaterVec3 geWaterCompositeReflectVector(GeWaterVec3 incident,
                                                          GeWaterVec3 normal)
{
    incident = geWaterCompositeVectorNormalize(incident);
    normal = geWaterCompositeVectorNormalize(normal);
    if (geWaterCompositeVectorLength(incident) <= GE_WATER_VECTOR_EPSILON ||
        geWaterCompositeVectorLength(normal) <= GE_WATER_VECTOR_EPSILON) {
        const GeWaterVec3 zero = {0.0f, 0.0f, 0.0f};
        return zero;
    }

    const float scale = 2.0f * geWaterCompositeVectorDot(normal, incident);
    const GeWaterVec3 reflected = {
        incident.x - scale * normal.x,
        incident.y - scale * normal.y,
        incident.z - scale * normal.z,
    };
    return geWaterCompositeVectorNormalize(reflected);
}

/* Reflect a world-space point through a general water plane
 * n.x + d = 0.  The capture path normally stores a unit normal, but accepting
 * a scaled plane here keeps the camera/reflection contract safe for adapters
 * that derive their plane from unnormalized triangle edges. */
static inline GeWaterVec3 geWaterCompositeReflectPointAcrossPlane(
    GeWaterVec3 point, const float plane[4])
{
    const GeWaterVec3 zero = {0.0f, 0.0f, 0.0f};
    if (!geWaterCompositeVectorIsFinite(point) || plane == NULL ||
        !geWaterFinite(plane[0]) || !geWaterFinite(plane[1]) ||
        !geWaterFinite(plane[2]) || !geWaterFinite(plane[3])) {
        return zero;
    }

    const GeWaterVec3 normal = {plane[0], plane[1], plane[2]};
    const float normal_length = geWaterCompositeVectorLength(normal);
    if (normal_length <= GE_WATER_VECTOR_EPSILON ||
        !geWaterFinite(normal_length)) {
        return zero;
    }

    const GeWaterVec3 unit_normal = {
        normal.x / normal_length,
        normal.y / normal_length,
        normal.z / normal_length,
    };
    const float signed_distance = geWaterCompositeVectorDot(
        unit_normal, point) + plane[3] / normal_length;
    if (!geWaterFinite(signed_distance)) {
        return zero;
    }

    const GeWaterVec3 reflected = {
        point.x - 2.0f * signed_distance * unit_normal.x,
        point.y - 2.0f * signed_distance * unit_normal.y,
        point.z - 2.0f * signed_distance * unit_normal.z,
    };
    return geWaterCompositeVectorIsFinite(reflected) ? reflected : zero;
}

static inline float geWaterCompositePlaneSignedDistance(
    GeWaterVec3 point, const float plane[4])
{
    if (!geWaterCompositeVectorIsFinite(point) || plane == NULL ||
        !geWaterFinite(plane[0]) || !geWaterFinite(plane[1]) ||
        !geWaterFinite(plane[2]) || !geWaterFinite(plane[3])) {
        return 0.0f;
    }
    const GeWaterVec3 normal = {plane[0], plane[1], plane[2]};
    const float normal_length = geWaterCompositeVectorLength(normal);
    if (!geWaterFinite(normal_length) ||
        normal_length <= GE_WATER_VECTOR_EPSILON) {
        return 0.0f;
    }
    const float distance = (geWaterCompositeVectorDot(normal, point) +
                            plane[3]) / normal_length;
    return geWaterFinite(distance) ? distance : 0.0f;
}

/* Return whether a point belongs to the side retained by the reflected
 * camera. `visible_side` is +1 or -1 depending on which side contains the
 * original camera; accepting both orientations avoids baking a triangle's
 * winding into the clip policy. */
static inline int geWaterCompositePlaneKeepsPoint(
    GeWaterVec3 point, const float plane[4], float visible_side,
    float epsilon)
{
    if (!geWaterFinite(visible_side) || geWaterAbs(visible_side) <=
            GE_WATER_VECTOR_EPSILON || !geWaterFinite(epsilon) ||
        epsilon < 0.0f) {
        return 0;
    }
    if (plane == NULL || !geWaterFinite(plane[0]) ||
        !geWaterFinite(plane[1]) || !geWaterFinite(plane[2]) ||
        !geWaterFinite(plane[3])) {
        return 0;
    }
    const GeWaterVec3 normal = {plane[0], plane[1], plane[2]};
    if (geWaterCompositeVectorLength(normal) <= GE_WATER_VECTOR_EPSILON) {
        return 0;
    }
    const float distance = geWaterCompositePlaneSignedDistance(point, plane);
    if (!geWaterFinite(distance)) {
        return 0;
    }
    return distance * (visible_side < 0.0f ? -1.0f : 1.0f) >= -epsilon;
}

static inline GeWaterVec3 geWaterCompositeRefractVector(GeWaterVec3 incident,
                                                          GeWaterVec3 normal,
                                                          float eta)
{
    incident = geWaterCompositeVectorNormalize(incident);
    normal = geWaterCompositeVectorNormalize(normal);
    if (geWaterCompositeVectorLength(incident) <= GE_WATER_VECTOR_EPSILON ||
        geWaterCompositeVectorLength(normal) <= GE_WATER_VECTOR_EPSILON ||
        !geWaterFinite(eta) || eta <= 0.0f) {
        const GeWaterVec3 zero = {0.0f, 0.0f, 0.0f};
        return zero;
    }

    float cosine = geWaterCompositeVectorDot(normal, incident);
    if (cosine > 0.0f) {
        normal.x = -normal.x;
        normal.y = -normal.y;
        normal.z = -normal.z;
        cosine = -cosine;
    }
    const float k = 1.0f - eta * eta * (1.0f - cosine * cosine);
    if (!geWaterFinite(k) || k < 0.0f) {
        const GeWaterVec3 zero = {0.0f, 0.0f, 0.0f};
        return zero;
    }

    const float normal_scale = eta * cosine + __builtin_sqrtf(k);
    const GeWaterVec3 refracted = {
        eta * incident.x - normal_scale * normal.x,
        eta * incident.y - normal_scale * normal.y,
        eta * incident.z - normal_scale * normal.z,
    };
    return geWaterCompositeVectorNormalize(refracted);
}

static inline float geWaterClamp01(float value)
{
    if (!geWaterFinite(value)) {
        return 0.0f;
    }
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static inline float geWaterClampSigned(float value)
{
    if (!geWaterFinite(value)) {
        return 0.0f;
    }
    if (value < -1.0f) {
        return -1.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static inline GeWaterVec3 geWaterCompositeVisibilityFloor(float water_thickness)
{
    if (!geWaterFinite(water_thickness) || water_thickness < 0.0f) {
        water_thickness = 0.0f;
    }
    const float depth_factor = geWaterClamp01(
        water_thickness * GE_WATER_AMBIENT_DEPTH_SCALE);
    const GeWaterVec3 shallow = {
        GE_WATER_AMBIENT_SHALLOW_R,
        GE_WATER_AMBIENT_SHALLOW_G,
        GE_WATER_AMBIENT_SHALLOW_B,
    };
    const GeWaterVec3 deep = {
        GE_WATER_AMBIENT_DEEP_R,
        GE_WATER_AMBIENT_DEEP_G,
        GE_WATER_AMBIENT_DEEP_B,
    };
    const GeWaterVec3 floor = {
        shallow.x + (deep.x - shallow.x) * depth_factor,
        shallow.y + (deep.y - shallow.y) * depth_factor,
        shallow.z + (deep.z - shallow.z) * depth_factor,
    };
    return floor;
}

static inline GeWaterVec3 geWaterCompositeApplyVisibilityFloor(
    GeWaterVec3 color, float water_thickness)
{
    const GeWaterVec3 floor = geWaterCompositeVisibilityFloor(water_thickness);
    if (!geWaterCompositeVectorIsFinite(color)) {
        return floor;
    }
    const GeWaterVec3 visible = {
        color.x < floor.x ? floor.x : color.x,
        color.y < floor.y ? floor.y : color.y,
        color.z < floor.z ? floor.z : color.z,
    };
    return geWaterCompositeVectorIsFinite(visible) ? visible : floor;
}

static inline int geWaterCompositeTier(int render_mode)
{
    if (render_mode <= 0) {
        return GE_WATER_COMPOSITE_DISABLED;
    }
    return render_mode >= 2 ? GE_WATER_COMPOSITE_REMASTER
                            : GE_WATER_COMPOSITE_ENHANCED;
}

static inline int geWaterCompositeCanRun(int render_mode,
                                         int has_scene_color,
                                         int has_scene_depth,
                                         size_t triangle_count)
{
    return geWaterCompositeTier(render_mode) != GE_WATER_COMPOSITE_DISABLED &&
           has_scene_color != 0 && has_scene_depth != 0 && triangle_count != 0;
}

/* The OpenGL backend can preserve the visible colour path when the source
 * framebuffer has no sampleable depth: it clears the private depth target to
 * far and disables depth-derived contact/refraction thickness. Keep that
 * degraded capability explicit instead of making the frontend lie about a
 * depth attachment it cannot inspect through the generic API. */
static inline int geWaterCompositeCanRunWithDepthFallback(
    int render_mode, int has_scene_color, size_t triangle_count)
{
    return geWaterCompositeTier(render_mode) != GE_WATER_COMPOSITE_DISABLED &&
           has_scene_color != 0 && triangle_count != 0;
}

static inline float geWaterCompositeTierScale(int render_mode)
{
    switch (geWaterCompositeTier(render_mode)) {
        case GE_WATER_COMPOSITE_ENHANCED:
            return 0.65f;
        case GE_WATER_COMPOSITE_REMASTER:
            return 1.0f;
        default:
            return 0.0f;
    }
}

static inline float geWaterCompositeWaveScale(int render_mode)
{
    switch (geWaterCompositeTier(render_mode)) {
        case GE_WATER_COMPOSITE_ENHANCED:
            return 0.004f;
        case GE_WATER_COMPOSITE_REMASTER:
            return 0.008f;
        default:
            return 0.0f;
    }
}

/* The compositor has two bounded sinusoidal components per axis. Keep this
 * bound in the policy layer so the shader and tests agree on the maximum
 * screen-space displacement instead of relying on an accidental sine range. */
static inline float geWaterCompositeWaveBound(int render_mode)
{
    return GE_WATER_WAVE_COMPONENTS * geWaterCompositeWaveScale(render_mode);
}

static inline float geWaterCompositeWaveOffset(float component_a,
                                               float component_b,
                                               float scale)
{
    if (!geWaterFinite(component_a) || !geWaterFinite(component_b) ||
        !geWaterFinite(scale) || scale < 0.0f) {
        return 0.0f;
    }
    const float offset = (geWaterClampSigned(component_a) +
                          geWaterClampSigned(component_b)) * scale;
    return geWaterFinite(offset) ? offset : 0.0f;
}

static inline float geWaterCompositeSampleUv(float coordinate)
{
    if (!geWaterFinite(coordinate)) {
        return 0.5f;
    }
    if (coordinate < GE_WATER_UV_MARGIN) {
        return GE_WATER_UV_MARGIN;
    }
    if (coordinate > 1.0f - GE_WATER_UV_MARGIN) {
        return 1.0f - GE_WATER_UV_MARGIN;
    }
    return coordinate;
}

static inline float geWaterCompositeClipToNdc(float clip, float w)
{
    if (!geWaterFinite(clip) || !geWaterFinite(w) || w <= 0.0f) {
        return 0.0f;
    }
    const float ndc = clip / w;
    return geWaterFinite(ndc) ? ndc : 0.0f;
}

static inline float geWaterCompositeClipToUv(float clip, float w)
{
    if (!geWaterFinite(clip) || !geWaterFinite(w) || w <= 0.0f) {
        return 0.5f;
    }
    const float ndc = clip / w;
    if (!geWaterFinite(ndc)) {
        return 0.5f;
    }
    return geWaterCompositeSampleUv(ndc * 0.5f + 0.5f);
}

static inline float geWaterCompositeContactBand(int render_mode)
{
    switch (geWaterCompositeTier(render_mode)) {
        case GE_WATER_COMPOSITE_ENHANCED:
            return GE_WATER_CONTACT_BAND_ENHANCED;
        case GE_WATER_COMPOSITE_REMASTER:
            return GE_WATER_CONTACT_BAND_REMASTER;
        default:
            return 0.0f;
    }
}

static inline float geWaterCompositeSmoothStep(float edge0,
                                                float edge1,
                                                float value)
{
    if (!geWaterFinite(edge0) || !geWaterFinite(edge1) || edge1 <= edge0) {
        return 0.0f;
    }
    if (!geWaterFinite(value)) {
        return value != value || value < 0.0f ? 0.0f : 1.0f;
    }
    if (value <= edge0) {
        return 0.0f;
    }
    if (value >= edge1) {
        return 1.0f;
    }
    const double t = ((double)value - (double)edge0) /
                     ((double)edge1 - (double)edge0);
    return (float)(t * t * (3.0 - 2.0 * t));
}

static inline float geWaterCompositeReflectionWeight(int render_mode,
                                                      float fresnel)
{
    if (!geWaterFinite(fresnel)) {
        return 0.0f;
    }
    return geWaterClamp01(geWaterClamp01(fresnel) *
                          geWaterCompositeTierScale(render_mode));
}

static inline int geWaterCompositeDepthIsOccluded(float surface_depth,
                                                   float scene_depth,
                                                   float epsilon)
{
    if (!geWaterFinite(surface_depth) || !geWaterFinite(scene_depth) ||
        !geWaterFinite(epsilon) || epsilon < 0.0f) {
        return 0;
    }
    return scene_depth + epsilon < surface_depth;
}

/* Refraction thickness is measured from the displaced water surface to the
 * first opaque sample behind it.  Do not replace the surface depth with the
 * scene depth when the latter is farther away: that turns every open-water
 * sample into zero thickness and removes the absorption/depth cue. */
static inline float geWaterCompositeSurfaceDepth(float surface_depth,
                                                  float scene_depth)
{
    if (geWaterFinite(surface_depth) &&
        surface_depth > GE_WATER_DEPTH_EPSILON) {
        return surface_depth;
    }
    if (geWaterFinite(scene_depth) && scene_depth > GE_WATER_DEPTH_EPSILON) {
        return scene_depth;
    }
    return 0.0f;
}

static inline float geWaterCompositeWaterThickness(float surface_depth,
                                                    float scene_depth)
{
    if (!geWaterFinite(scene_depth) || scene_depth <= GE_WATER_DEPTH_EPSILON) {
        return 0.0f;
    }
    const float resolved_surface = geWaterCompositeSurfaceDepth(
        surface_depth, scene_depth);
    /* scene_depth is finite and above the epsilon here. The resolver returns
     * either a valid surface depth or that same scene depth, so a non-positive
     * resolved surface is unreachable without violating its contract. */
    const float thickness = scene_depth - resolved_surface;
    return geWaterFinite(thickness) && thickness > 0.0f ? thickness : 0.0f;
}

static inline float geWaterCompositeContactFoam(float surface_depth,
                                                 float scene_depth,
                                                 float edge_strength,
                                                 float depth_band)
{
    if (!geWaterFinite(surface_depth) || !geWaterFinite(scene_depth) ||
        !geWaterFinite(edge_strength) || !geWaterFinite(depth_band) ||
        depth_band <= 0.0f || edge_strength <= 0.0f) {
        return 0.0f;
    }

    const float gap = surface_depth - scene_depth;
    if (gap <= GE_WATER_CONTACT_START || gap >= depth_band ||
        depth_band <= GE_WATER_CONTACT_START) {
        return 0.0f;
    }
    const float proximity = 1.0f - geWaterCompositeSmoothStep(
        GE_WATER_CONTACT_START, depth_band, gap);
    return proximity * geWaterClamp01(edge_strength);
}

static inline float geWaterCompositeDepthEdge(float center,
                                               float left,
                                               float right,
                                               float up,
                                               float down)
{
    if (!geWaterFinite(center) || !geWaterFinite(left) ||
        !geWaterFinite(right) || !geWaterFinite(up) || !geWaterFinite(down)) {
        return 0.0f;
    }

    float edge = geWaterAbs(center - left);
    const float right_edge = geWaterAbs(center - right);
    const float up_edge = geWaterAbs(center - up);
    const float down_edge = geWaterAbs(center - down);
    if (right_edge > edge) edge = right_edge;
    if (up_edge > edge) edge = up_edge;
    if (down_edge > edge) edge = down_edge;
    return geWaterCompositeSmoothStep(GE_WATER_EDGE_START,
                                      GE_WATER_EDGE_END, edge);
}

static inline float geWaterCompositeFresnel(float cosine, float base_reflectance)
{
    if (!geWaterFinite(cosine) || !geWaterFinite(base_reflectance) ||
        base_reflectance < 0.0f) {
        return 0.0f;
    }
    cosine = geWaterClamp01(cosine);
    base_reflectance = geWaterClamp01(base_reflectance);
    const float one_minus_cosine = 1.0f - cosine;
    float grazing_power = 1.0f;
    for (int i = 0; i < GE_WATER_FRESNEL_POWER; ++i) {
        grazing_power *= one_minus_cosine;
    }
    return base_reflectance + (1.0f - base_reflectance) * grazing_power;
}

static inline float geWaterCompositeReflectV(float uv_v, float water_line)
{
    if (!geWaterFinite(uv_v) || !geWaterFinite(water_line)) {
        return 0.0f;
    }
    return geWaterCompositeSampleUv(2.0f * water_line - uv_v);
}

static inline int geWaterCompositeVertexIsValid(float x, float y,
                                                float z, float w)
{
    return geWaterFinite(x) && geWaterFinite(y) && geWaterFinite(z) &&
           geWaterFinite(w) && w > 0.0f &&
           geWaterFinite(x / w) && geWaterFinite(y / w);
}

static inline float geWaterCompositeTriangleArea(float x0, float y0,
                                                  float x1, float y1,
                                                  float x2, float y2)
{
    if (!geWaterFinite(x0) || !geWaterFinite(y0) || !geWaterFinite(x1) ||
        !geWaterFinite(y1) || !geWaterFinite(x2) || !geWaterFinite(y2)) {
        return 0.0f;
    }
    const float twice_area = (x1 - x0) * (y2 - y0) -
                             (y1 - y0) * (x2 - x0);
    return geWaterFinite(twice_area) ? geWaterAbs(twice_area) * 0.5f : 0.0f;
}

static inline int geWaterCompositePhaseCanAdvance(int current_phase,
                                                    int next_phase)
{
    return current_phase >= GE_WATER_PHASE_PREWORLD &&
           current_phase < GE_WATER_PHASE_HUD &&
           next_phase == current_phase + 1;
}

#endif /* GE007_GFX_WATER_COMPOSITE_POLICY_H */
