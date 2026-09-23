#ifndef GE007_BUMP_MAPPING_POLICY_H
#define GE007_BUMP_MAPPING_POLICY_H

/* Pure height-gradient bump mapping contracts. The renderer evaluates its
 * bounded water height field in the composite shader, so this policy stays
 * independent of OpenGL, texture objects, and display-list state. */

#define GE_BUMP_WATER_NORMAL_STRENGTH_ENHANCED 2.5f
#define GE_BUMP_WATER_NORMAL_STRENGTH_REMASTER 3.5f

/* The ordinary Fast3D stream must remain the legacy position/texture stream
 * unless the optional material path is actually enabled. Keeping this pure
 * decision beside the bump policy gives shader generation and VBO packing one
 * authoritative contract instead of two subtly different `used_textures`
 * checks. */
static inline int geFast3dMaterialUsesBumpMapping(int used_texture0,
                                                 int bump_maps_enabled)
{
    return used_texture0 != 0 && bump_maps_enabled != 0;
}

/* Shadow receivers need view-space position even when the N64 combiner does
 * not consume TEXEL0. Keep that requirement distinct from bump mapping, whose
 * tangent frame specifically needs TEXEL0 coordinates. */
struct GeFast3dMaterialRequirements {
    int uses_view_position;
    int uses_bump_mapping;
};

static inline GeFast3dMaterialRequirements geFast3dMaterialRequirements(
    int used_texture0, int bump_maps_enabled, int shadow_mapping_enabled)
{
    GeFast3dMaterialRequirements requirements = {};
    requirements.uses_bump_mapping =
        geFast3dMaterialUsesBumpMapping(used_texture0, bump_maps_enabled);
    requirements.uses_view_position =
        requirements.uses_bump_mapping || shadow_mapping_enabled != 0;
    return requirements;
}

static inline float geBumpWaterNormalStrength(int render_tier)
{
    if (render_tier <= 0) {
        return 0.0f;
    }
    return render_tier >= 2 ? GE_BUMP_WATER_NORMAL_STRENGTH_REMASTER
                            : GE_BUMP_WATER_NORMAL_STRENGTH_ENHANCED;
}

typedef struct GeBumpVec3 {
    float x;
    float y;
    float z;
} GeBumpVec3;

typedef struct GeBumpTangentFrame {
    GeBumpVec3 normal;
    GeBumpVec3 tangent;
    GeBumpVec3 bitangent;
    float handedness;
    int valid;
} GeBumpTangentFrame;

typedef struct GeBumpHeightCross {
    float left;
    float right;
    float down;
    float up;
} GeBumpHeightCross;

static inline GeBumpVec3 geBumpFallbackNormal(void);

static inline int geBumpFinite(float value)
{
    return __builtin_isfinite(value) != 0;
}

static inline int geBumpVectorIsFinite(GeBumpVec3 vector)
{
    return geBumpFinite(vector.x) & geBumpFinite(vector.y) &
           geBumpFinite(vector.z);
}

static inline float geBumpVectorLength(GeBumpVec3 vector)
{
    return __builtin_sqrtf(vector.x * vector.x +
                           vector.y * vector.y +
                           vector.z * vector.z);
}

static inline float geBumpDot(GeBumpVec3 left, GeBumpVec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

static inline GeBumpVec3 geBumpSubtract(GeBumpVec3 left, GeBumpVec3 right)
{
    const GeBumpVec3 result = {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z};
    return result;
}

static inline GeBumpVec3 geBumpScale(GeBumpVec3 value, float scale)
{
    const GeBumpVec3 result = {
        value.x * scale,
        value.y * scale,
        value.z * scale};
    return result;
}

static inline GeBumpVec3 geBumpCross(GeBumpVec3 left, GeBumpVec3 right)
{
    const GeBumpVec3 result = {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
    return result;
}

static inline GeBumpVec3 geBumpNormalize(GeBumpVec3 value, int *valid)
{
    const float length = geBumpVectorLength(value);
    if (valid == 0 || !geBumpVectorIsFinite(value) ||
        !geBumpFinite(length) || length <= 0.000001f) {
        if (valid != 0) {
            *valid = 0;
        }
        return geBumpFallbackNormal();
    }

    const GeBumpVec3 result = geBumpScale(value, 1.0f / length);
    *valid = 1;
    return result;
}

static inline GeBumpVec3 geBumpFallbackNormal(void)
{
    const GeBumpVec3 normal = {0.0f, 0.0f, 1.0f};
    return normal;
}

/* Construct the one authoritative tangent frame used by material and water
 * normal mapping. Tangent is Gram-Schmidt corrected against the geometric
 * normal; bitangent is reconstructed from the cross product so non-orthogonal
 * captured streams cannot leak into the shader. */
static inline GeBumpTangentFrame geBumpBuildTangentFrame(
    GeBumpVec3 normal,
    GeBumpVec3 tangent,
    GeBumpVec3 bitangent_hint,
    float handedness)
{
    GeBumpTangentFrame frame = {};
    int normal_valid = 0;
    int tangent_valid = 0;

    frame.normal = geBumpNormalize(normal, &normal_valid);
    if (!normal_valid || !geBumpVectorIsFinite(tangent) ||
        !geBumpVectorIsFinite(bitangent_hint) ||
        !geBumpFinite(handedness) ||
        (handedness != 1.0f && handedness != -1.0f)) {
        frame.normal = geBumpFallbackNormal();
        frame.tangent = geBumpFallbackNormal();
        frame.bitangent = geBumpFallbackNormal();
        frame.handedness = 1.0f;
        frame.valid = 0;
        return frame;
    }

    tangent = geBumpSubtract(
        tangent, geBumpScale(frame.normal, geBumpDot(frame.normal, tangent)));
    frame.tangent = geBumpNormalize(tangent, &tangent_valid);
    if (!tangent_valid) {
        frame.normal = geBumpFallbackNormal();
        frame.tangent = geBumpFallbackNormal();
        frame.bitangent = geBumpFallbackNormal();
        frame.handedness = 1.0f;
        frame.valid = 0;
        return frame;
    }

    frame.handedness = handedness;
    frame.bitangent = geBumpScale(
        geBumpCross(frame.normal, frame.tangent), handedness);
    /* Both inputs are finite unit vectors and handedness is restricted to
     * +/-1 above, so the cross product is finite by construction. */
    frame.valid = 1;
    return frame;
}

static inline GeBumpVec3 geBumpNormalFromHeight(GeBumpHeightCross samples,
                                                 float strength)
{
    const int valid_inputs =
        geBumpFinite(samples.left) & geBumpFinite(samples.right) &
        geBumpFinite(samples.down) & geBumpFinite(samples.up) &
        geBumpFinite(strength);
    if (!valid_inputs || strength < 0.0f) {
        return geBumpFallbackNormal();
    }

    const float gradient_x = (samples.left - samples.right) * strength;
    const float gradient_y = (samples.down - samples.up) * strength;
    if (!(geBumpFinite(gradient_x) & geBumpFinite(gradient_y))) {
        return geBumpFallbackNormal();
    }

    const float length = __builtin_sqrtf(
        gradient_x * gradient_x + gradient_y * gradient_y + 1.0f);
    if (!geBumpFinite(length)) {
        return geBumpFallbackNormal();
    }

    const GeBumpVec3 normal = {
        gradient_x / length,
        gradient_y / length,
        1.0f / length};
    return normal;
}

#endif /* GE007_BUMP_MAPPING_POLICY_H */
