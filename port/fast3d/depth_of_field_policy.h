#ifndef GE007_DEPTH_OF_FIELD_POLICY_H
#define GE007_DEPTH_OF_FIELD_POLICY_H

/* Pure depth-of-field policy. The OpenGL adapter consumes the same bounded
 * values in its shader, while this header keeps focus, edge rejection, and
 * split-viewport rules testable without a GL context or a running level. */

#define GE_DOF_MAX_RADIUS_PIXELS 16.0f
#define GE_DOF_MAX_VIEWPORTS 4

typedef struct GeDofConfig {
    float focus_depth;
    float near_blur_depth;
    float near_clear_depth;
    float far_clear_depth;
    float far_blur_depth;
    float max_radius_pixels;
} GeDofConfig;

typedef struct GeDofResult {
    float near_blur;
    float far_blur;
    float radius_pixels;
    int valid;
} GeDofResult;

typedef struct GeDofViewport {
    float x;
    float y;
    float width;
    float height;
} GeDofViewport;

typedef struct GeDofUv {
    float u;
    float v;
} GeDofUv;

static inline int geDofFinite(float value)
{
    return __builtin_isfinite(value) != 0;
}

static inline float geDofClamp01(float value)
{
    if (value <= 0.0f) {
        return 0.0f;
    }
    if (value >= 1.0f) {
        return 1.0f;
    }
    return value;
}

static inline float geDofSmoothstep(float edge0, float edge1, float value)
{
    if (edge1 <= edge0) {
        return value < edge1 ? 0.0f : 1.0f;
    }
    const float t = geDofClamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

static inline int geDofConfigIsValid(GeDofConfig config)
{
    return geDofFinite(config.focus_depth) &&
           geDofFinite(config.near_blur_depth) &&
           geDofFinite(config.near_clear_depth) &&
           geDofFinite(config.far_clear_depth) &&
           geDofFinite(config.far_blur_depth) &&
           geDofFinite(config.max_radius_pixels) &&
           config.near_blur_depth >= 0.0f &&
           config.near_blur_depth < config.near_clear_depth &&
           config.near_clear_depth < config.focus_depth &&
           config.focus_depth < config.far_clear_depth &&
           config.far_clear_depth < config.far_blur_depth &&
           config.far_blur_depth <= 1.0f &&
           config.max_radius_pixels >= 0.0f &&
           config.max_radius_pixels <= GE_DOF_MAX_RADIUS_PIXELS;
}

static inline GeDofResult geDofNoBlur(void)
{
    const GeDofResult result = {0.0f, 0.0f, 0.0f, 0};
    return result;
}

static inline GeDofResult geDofEvaluate(float depth, GeDofConfig config)
{
    if (!geDofFinite(depth) || depth < 0.0f || depth > 1.0f ||
        !geDofConfigIsValid(config)) {
        return geDofNoBlur();
    }

    float near_blur = 0.0f;
    float far_blur = 0.0f;
    if (depth < config.focus_depth) {
        near_blur = 1.0f - geDofSmoothstep(
            config.near_blur_depth, config.near_clear_depth, depth);
    } else if (depth > config.focus_depth) {
        far_blur = geDofSmoothstep(
            config.far_clear_depth, config.far_blur_depth, depth);
    }

    const float amount = near_blur > far_blur ? near_blur : far_blur;
    const GeDofResult result = {
        near_blur,
        far_blur,
        amount * config.max_radius_pixels,
        1};
    return result;
}

/* Signed CoC convention: positive is in front of the focus plane and
 * negative is behind it. The shader only needs the magnitude, but retaining
 * the sign makes the policy useful to diagnostics and future bokeh paths. */
static inline float geDofCircleOfConfusion(float depth, GeDofConfig config)
{
    const GeDofResult result = geDofEvaluate(depth, config);
    return result.valid ? result.near_blur - result.far_blur : 0.0f;
}

/* Fade a blur sample out across a depth discontinuity. This prevents a
 * foreground silhouette from pulling background colour through its edge. */
static inline float geDofDepthWeight(float center_depth,
                                     float sample_depth,
                                     float threshold)
{
    if (!geDofFinite(center_depth) || !geDofFinite(sample_depth) ||
        !geDofFinite(threshold) || threshold <= 0.0f) {
        return 0.0f;
    }
    const float gap = __builtin_fabsf(center_depth - sample_depth);
    if (!geDofFinite(gap) || gap >= threshold) {
        return 0.0f;
    }
    return geDofClamp01(1.0f - gap / threshold);
}

static inline int geDofViewportIsValid(GeDofViewport viewport)
{
    return geDofFinite(viewport.x) && geDofFinite(viewport.y) &&
           geDofFinite(viewport.width) && geDofFinite(viewport.height) &&
           viewport.x >= 0.0f && viewport.y >= 0.0f &&
           viewport.width > 0.0f && viewport.height > 0.0f &&
           viewport.x + viewport.width <= 1.0f &&
           viewport.y + viewport.height <= 1.0f;
}

static inline int geDofViewportContains(GeDofViewport viewport,
                                        float u,
                                        float v)
{
    return geDofViewportIsValid(viewport) && geDofFinite(u) && geDofFinite(v) &&
           u >= viewport.x && u <= viewport.x + viewport.width &&
           v >= viewport.y && v <= viewport.y + viewport.height;
}

static inline GeDofUv geDofClampUvToViewport(float u,
                                             float v,
                                             GeDofViewport viewport)
{
    if (!geDofViewportIsValid(viewport)) {
        const GeDofUv unchanged = {u, v};
        return unchanged;
    }

    const float right = viewport.x + viewport.width;
    const float top = viewport.y + viewport.height;
    const GeDofUv clamped = {
        u < viewport.x ? viewport.x : (u > right ? right : u),
        v < viewport.y ? viewport.y : (v > top ? top : v)};
    return clamped;
}

static inline int geDofShouldApply(int enabled,
                                   int depth_available,
                                   int viewport_count)
{
    return enabled != 0 && depth_available != 0 &&
           viewport_count > 0 && viewport_count <= GE_DOF_MAX_VIEWPORTS;
}

#endif /* GE007_DEPTH_OF_FIELD_POLICY_H */
