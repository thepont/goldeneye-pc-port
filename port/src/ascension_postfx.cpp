#include "ascension_postfx.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include <PR/gbi.h>

#include "config.h"
#include "platform.h"
#include "system.h"

#include "../fast3d/gfx_api.h"
#include "../fast3d/gfx_gl_state_guard.h"
#include "../fast3d/gfx_shader_source.h"
#include "../fast3d/glad/glad.h"
#include "../fast3d/depth_of_field_policy.h"
#include "../fast3d/postfx_policy.h"

/*
 * Ascension final presentation pass
 * ---------------------------------
 * Presentation only: this never changes GoldenEye display lists, RSP/RDP
 * state, depth data, world geometry, gameplay or timing.
 *
 * The shader is an original edge-adaptive sharpening pass inspired by the
 * design goals of modern contrast-adaptive sharpening filters: recover detail
 * after scaling/filtering while reducing gain around already-hard edges and
 * clamping the result to the local colour neighbourhood to avoid halos.
 *
 * Safety rules:
 *  - lazy initialization only after a valid GL context exists;
 *  - no allocations in the steady-state frame path;
 *  - resize allocation only when the drawable size changes;
 *  - GL bindings/state touched here are restored before returning;
 *  - fail-open: shader/driver trouble disables this pass, never the game.
 */

static int s_postFxEnabled = 1;
static int s_sharpenAmount = 35;
static int s_depthOfFieldEnabled = 0;

PD_CONSTRUCTOR static void ascensionPostFxConfigInit(void)
{
    configRegisterInt("Video.PostFX", &s_postFxEnabled, 0, 1);
    configRegisterInt("Video.Sharpen", &s_sharpenAmount, 0, 100);
    configRegisterInt("Video.DepthOfField", &s_depthOfFieldEnabled, 0, 1);
}

struct AscPostFxState {
    GLuint program;
    GLuint texture;
    GLuint vao;
    GLuint vbo;
    GLint sceneLoc;
    GLint depthLoc;
    GLint invSizeLoc;
    GLint strengthLoc;
    GLint dofEnabledLoc;
    GLint dofFocusLoc;
    GLint dofRangesLoc;
    GLint dofMaxRadiusLoc;
    GLint viewportCountLoc;
    GLint viewportRectsLoc;
    int width;
    int height;
    int initialized;
    int failed;
    int announced;
};

static AscPostFxState s_fx = {};

static const char *kVertexShaderBody = R"GLSL(
#ifdef GL_ES
precision mediump float;
#endif
#if __VERSION__ >= 130
in vec2 aPosition;
in vec2 aTexCoord;
out vec2 vUV;
#else
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vUV;
#endif

void main()
{
    vUV = aTexCoord;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)GLSL";

static const char *kFragmentShaderBody = R"GLSL(
#ifdef GL_ES
precision mediump float;
#endif
#if __VERSION__ >= 130
in vec2 vUV;
out vec4 fragColor;
#define GE007_FRAG_COLOR fragColor
#define GE007_TEXTURE texture
#else
varying vec2 vUV;
#define GE007_FRAG_COLOR gl_FragColor
#define GE007_TEXTURE texture2D
#endif
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform vec2 uInvSize;
uniform float uStrength;
uniform int uDofEnabled;
uniform float uDofFocus;
uniform vec4 uDofRanges;
uniform float uDofMaxRadius;
uniform int uViewportCount;
uniform vec4 uViewportRects[4];

float ascLuma(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float ascDofAmount(float depth)
{
    float nearBlur = 1.0 - smoothstep(uDofRanges.x, uDofRanges.y, depth);
    float farBlur = smoothstep(uDofRanges.z, uDofRanges.w, depth);
    if (depth < uDofFocus) return clamp(nearBlur, 0.0, 1.0);
    if (depth > uDofFocus) return clamp(farBlur, 0.0, 1.0);
    return 0.0;
}

float ascDofDepthWeight(float centerDepth, float sampleDepth, float threshold)
{
    float gap = abs(centerDepth - sampleDepth);
    if (threshold <= 0.0 || gap >= threshold) return 0.0;
    return clamp(1.0 - gap / threshold, 0.0, 1.0);
}

int ascDofViewport(vec2 uv)
{
    for (int i = 0; i < 4; ++i) {
        if (i >= uViewportCount) break;
        vec4 viewport = uViewportRects[i];
        if (uv.x >= viewport.x && uv.x <= viewport.x + viewport.z &&
            uv.y >= viewport.y && uv.y <= viewport.y + viewport.w) {
            return i;
        }
    }
    return -1;
}

vec2 ascDofClampToViewport(vec2 uv, int viewportIndex)
{
    vec4 viewport = uViewportRects[viewportIndex];
    return clamp(uv, viewport.xy, viewport.xy + viewport.zw);
}

vec3 ascDofBlur(vec2 uv, vec3 center, float centerDepth,
                float radius, int viewportIndex)
{
    vec2 offsets[8];
    offsets[0] = vec2( 1.0,  0.0);
    offsets[1] = vec2(-1.0,  0.0);
    offsets[2] = vec2( 0.0,  1.0);
    offsets[3] = vec2( 0.0, -1.0);
    offsets[4] = vec2( 0.7071,  0.7071);
    offsets[5] = vec2(-0.7071,  0.7071);
    offsets[6] = vec2( 0.7071, -0.7071);
    offsets[7] = vec2(-0.7071, -0.7071);

    vec3 sum = center;
    float weight = 1.0;
    float depthThreshold = max(0.02, radius * 0.005);
    for (int i = 0; i < 8; ++i) {
        vec2 sampleUv = ascDofClampToViewport(
            uv + offsets[i] * uInvSize * radius, viewportIndex);
        float sampleDepth = GE007_TEXTURE(uDepth, sampleUv).r;
        float sampleWeight = ascDofDepthWeight(
            centerDepth, sampleDepth, depthThreshold);
        sum += GE007_TEXTURE(uScene, sampleUv).rgb * sampleWeight;
        weight += sampleWeight;
    }
    return sum / max(weight, 0.0001);
}

void main()
{
    vec4 src = GE007_TEXTURE(uScene, vUV);
    vec3 c  = src.rgb;
    vec3 n  = GE007_TEXTURE(uScene, vUV + vec2(0.0,  uInvSize.y)).rgb;
    vec3 s  = GE007_TEXTURE(uScene, vUV + vec2(0.0, -uInvSize.y)).rgb;
    vec3 e  = GE007_TEXTURE(uScene, vUV + vec2( uInvSize.x, 0.0)).rgb;
    vec3 w  = GE007_TEXTURE(uScene, vUV + vec2(-uInvSize.x, 0.0)).rgb;
    vec3 ne = GE007_TEXTURE(uScene, vUV + vec2( uInvSize.x,  uInvSize.y)).rgb;
    vec3 nw = GE007_TEXTURE(uScene, vUV + vec2(-uInvSize.x,  uInvSize.y)).rgb;
    vec3 se = GE007_TEXTURE(uScene, vUV + vec2( uInvSize.x, -uInvSize.y)).rgb;
    vec3 sw = GE007_TEXTURE(uScene, vUV + vec2(-uInvSize.x, -uInvSize.y)).rgb;

    /* Cross-weighted low-pass keeps the filter compact and stable on N64 UI. */
    vec3 blur = (n + s + e + w) * 0.1875 + (ne + nw + se + sw) * 0.0625;
    vec3 detail = c - blur;

    vec3 localMin = min(c, min(min(n, s), min(e, w)));
    localMin = min(localMin, min(min(ne, nw), min(se, sw)));
    vec3 localMax = max(c, max(max(n, s), max(e, w)));
    localMax = max(localMax, max(max(ne, nw), max(se, sw)));

    float lMin = min(ascLuma(localMin), ascLuma(c));
    float lMax = max(ascLuma(localMax), ascLuma(c));
    float localContrast = clamp((lMax - lMin) * 2.25, 0.0, 1.0);

    /* Reduce sharpening on strong transitions where halos are most visible. */
    float adaptiveGain = mix(1.0, 0.52, smoothstep(0.18, 0.82, localContrast));
    float gain = clamp(uStrength, 0.0, 1.0) * adaptiveGain * 1.85;
    vec3 sharpened = c + detail * gain;

    /* Anti-ringing neighbourhood guard. Small tolerance keeps fine highlights. */
    vec3 tolerance = vec3(mix(0.018, 0.008, localContrast));
    sharpened = clamp(sharpened, localMin - tolerance, localMax + tolerance);

    vec3 presented = sharpened;
    if (uDofEnabled != 0) {
        int viewportIndex = ascDofViewport(vUV);
        if (viewportIndex >= 0) {
            float depth = GE007_TEXTURE(uDepth, vUV).r;
            float radius = clamp(ascDofAmount(depth) * uDofMaxRadius,
                                 0.0, uDofMaxRadius);
            if (radius > 0.5) {
                presented = ascDofBlur(vUV, sharpened, depth,
                                       radius, viewportIndex);
            }
        }
    }

    GE007_FRAG_COLOR = vec4(clamp(presented, 0.0, 1.0), src.a);
}
)GLSL";

static GLuint ascCompileShader(GLenum type, const char *source, const char *label)
{
    GLuint shader = glCreateShader(type);
    if (!shader) {
        sysLogPrintf(LOG_WARNING, "graphics: PostFX could not create %s shader", label);
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLchar log[2048];
        GLsizei len = 0;
        log[0] = '\0';
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log) - 1, &len, log);
        log[(len >= 0 && len < (GLsizei)sizeof(log)) ? len : (GLsizei)sizeof(log) - 1] = '\0';
        sysLogPrintf(LOG_WARNING, "graphics: PostFX %s shader compile failed: %s", label, log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static int ascPostFxInit(void)
{
    if (s_fx.initialized) return 1;
    if (s_fx.failed) return 0;

    /* Fast3D's normal Windows path is GL 3.x. If a very old fallback context
     * lacks the VAO entry points, keep the original frame instead of risking
     * a null driver function call. */
    if (!glGenVertexArrays || !glBindVertexArray || !glDeleteVertexArrays ||
        !glCopyTexSubImage2D || !glBindFramebuffer || !glReadBuffer ||
        !(glDrawBuffers != nullptr || glDrawBuffer != nullptr)) {
        sysLogPrintf(LOG_WARNING,
            "graphics: PostFX unavailable on this OpenGL context; using original presentation");
        s_fx.failed = 1;
        return 0;
    }

    const std::string vertex_source =
        geGlslSourceWithRendererVersion(kVertexShaderBody);
    const std::string fragment_source =
        geGlslSourceWithRendererVersion(kFragmentShaderBody);
    if (vertex_source.empty() || fragment_source.empty()) {
        sysLogPrintf(LOG_WARNING,
            "graphics: PostFX could not determine the active GLSL dialect");
        s_fx.failed = 1;
        return 0;
    }

    GLuint vs = ascCompileShader(GL_VERTEX_SHADER, vertex_source.c_str(), "vertex");
    GLuint fs = ascCompileShader(GL_FRAGMENT_SHADER, fragment_source.c_str(), "fragment");
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        s_fx.failed = 1;
        return 0;
    }

    s_fx.program = glCreateProgram();
    if (!s_fx.program) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        s_fx.failed = 1;
        return 0;
    }

    glAttachShader(s_fx.program, vs);
    glAttachShader(s_fx.program, fs);
    glBindAttribLocation(s_fx.program, 0, "aPosition");
    glBindAttribLocation(s_fx.program, 1, "aTexCoord");
    glLinkProgram(s_fx.program);

    glDetachShader(s_fx.program, vs);
    glDetachShader(s_fx.program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(s_fx.program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLchar log[2048];
        GLsizei len = 0;
        log[0] = '\0';
        glGetProgramInfoLog(s_fx.program, (GLsizei)sizeof(log) - 1, &len, log);
        log[(len >= 0 && len < (GLsizei)sizeof(log)) ? len : (GLsizei)sizeof(log) - 1] = '\0';
        sysLogPrintf(LOG_WARNING, "graphics: PostFX shader link failed: %s", log);
        glDeleteProgram(s_fx.program);
        s_fx.program = 0;
        s_fx.failed = 1;
        return 0;
    }

    s_fx.sceneLoc = glGetUniformLocation(s_fx.program, "uScene");
    s_fx.depthLoc = glGetUniformLocation(s_fx.program, "uDepth");
    s_fx.invSizeLoc = glGetUniformLocation(s_fx.program, "uInvSize");
    s_fx.strengthLoc = glGetUniformLocation(s_fx.program, "uStrength");
    s_fx.dofEnabledLoc = glGetUniformLocation(s_fx.program, "uDofEnabled");
    s_fx.dofFocusLoc = glGetUniformLocation(s_fx.program, "uDofFocus");
    s_fx.dofRangesLoc = glGetUniformLocation(s_fx.program, "uDofRanges");
    s_fx.dofMaxRadiusLoc = glGetUniformLocation(s_fx.program, "uDofMaxRadius");
    s_fx.viewportCountLoc = glGetUniformLocation(s_fx.program, "uViewportCount");
    s_fx.viewportRectsLoc = glGetUniformLocation(s_fx.program, "uViewportRects[0]");

    static const GLfloat quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };

    /* First-use initialization must not poison Fast3D's cached binding state.
     * Capture the bindings before creating/configuring our private objects and
     * restore them before returning. */
    GLint oldVao = 0;
    GLint oldArrayBuffer = 0;
    GLint oldActiveTexture = 0;
    GLint oldTexture = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldArrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);

    glGenVertexArrays(1, &s_fx.vao);
    glGenBuffers(1, &s_fx.vbo);
    glGenTextures(1, &s_fx.texture);
    if (!s_fx.vao || !s_fx.vbo || !s_fx.texture) {
        sysLogPrintf(LOG_WARNING, "graphics: PostFX GPU resource creation failed; disabling pass");
        if (s_fx.vbo) glDeleteBuffers(1, &s_fx.vbo);
        if (s_fx.vao) glDeleteVertexArrays(1, &s_fx.vao);
        if (s_fx.texture) glDeleteTextures(1, &s_fx.texture);
        if (s_fx.program) glDeleteProgram(s_fx.program);
        s_fx.program = s_fx.texture = s_fx.vao = s_fx.vbo = 0;
        glBindVertexArray((GLuint)oldVao);
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)oldArrayBuffer);
        glBindTexture(GL_TEXTURE_2D, (GLuint)oldTexture);
        glActiveTexture((GLenum)oldActiveTexture);
        s_fx.failed = 1;
        return 0;
    }

    glBindVertexArray(s_fx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_fx.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const void *)(2 * sizeof(GLfloat)));

    glBindTexture(GL_TEXTURE_2D, s_fx.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindVertexArray((GLuint)oldVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)oldArrayBuffer);
    glBindTexture(GL_TEXTURE_2D, (GLuint)oldTexture);
    glActiveTexture((GLenum)oldActiveTexture);

    s_fx.width = 0;
    s_fx.height = 0;
    s_fx.initialized = 1;
    return 1;
}

static int ascPostFxEnabled(void)
{
    const char *env = std::getenv("GE_POSTFX");
    if (env && *env) return std::atoi(env) != 0;
    return s_postFxEnabled != 0;
}

static int ascPostFxSharpen(void)
{
    int amount = s_sharpenAmount;
    const char *env = std::getenv("GE_SHARPEN");
    if (env && *env) amount = std::atoi(env);
    return gePostFxClampSharpen(amount);
}

static int ascPostFxDepthOfFieldEnabled(void)
{
    const char *env = std::getenv("GE_DOF");
    if (env && *env) return std::atoi(env) != 0;
    return s_depthOfFieldEnabled != 0;
}

static GeDofConfig ascPostFxDepthOfFieldConfig(void)
{
    /* The default focus plane is deliberately conservative. Depth is sampled
     * from the native scene buffer, so keeping a clear band around the plane
     * avoids turning the HUD and nearby geometry into a shimmering blur. */
    const GeDofConfig config = {0.50f, 0.10f, 0.40f, 0.60f, 0.90f, 8.0f};
    return config;
}

extern "C" int ascensionPostFxWantsDepth(void)
{
    return ascPostFxDepthOfFieldEnabled() != 0 ? 1 : 0;
}

extern "C" void ascensionPostFxApply(void)
{
    const int postfx_enabled = ascPostFxEnabled();
    const int sharpen = ascPostFxSharpen();
    const int dof_enabled = ascPostFxDepthOfFieldEnabled();

    uintptr_t depth_texture = 0;
    uint32_t depth_width = 0;
    uint32_t depth_height = 0;
    const int depth_available = gfx_get_postfx_depth_source(
        &depth_texture, &depth_width, &depth_height);

    const int width = (int)gfx_current_window_dimensions.width;
    const int height = (int)gfx_current_window_dimensions.height;
    if (width <= 1 || height <= 1) return;

    GfxPostFxViewport viewports[4] = {};
    size_t viewport_count = gfx_get_postfx_viewports(viewports, 4);
    if (viewport_count == 0) {
        viewports[0] = {0, 0, (uint32_t)width, (uint32_t)height};
        viewport_count = 1;
    }
    const int dof_apply = geDofShouldApply(
        dof_enabled, depth_available, (int)viewport_count);
    if (!gePostFxShouldApply(postfx_enabled, sharpen) && !dof_apply) return;
    if (!ascPostFxInit()) return;

    GLint maxTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    if (width > maxTexture || height > maxTexture) {
        if (!s_fx.failed) {
            sysLogPrintf(LOG_WARNING,
                "graphics: PostFX drawable %dx%d exceeds GL max texture size %d; disabling pass",
                width, height, maxTexture);
        }
        s_fx.failed = 1;
        return;
    }

    GeGlStateGuard state_guard;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_fx.texture);

    if (s_fx.width != width || s_fx.height != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        s_fx.width = width;
        s_fx.height = height;
    }

    /* The final Fast3D composite is on the default back buffer at this seam. */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    geGlSetDrawBuffer(GL_BACK);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);

    glUseProgram(s_fx.program);
    glUniform1i(s_fx.sceneLoc, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, dof_apply ? (GLuint)depth_texture : 0);
    glUniform1i(s_fx.depthLoc, 1);
    glActiveTexture(GL_TEXTURE0);
    glUniform2f(s_fx.invSizeLoc, 1.0f / (float)width, 1.0f / (float)height);
    const int effective_sharpen = postfx_enabled ? sharpen : 0;
    glUniform1f(s_fx.strengthLoc, (float)effective_sharpen / 100.0f);
    const GeDofConfig dof_config = ascPostFxDepthOfFieldConfig();
    glUniform1i(s_fx.dofEnabledLoc, dof_apply);
    glUniform1f(s_fx.dofFocusLoc, dof_config.focus_depth);
    glUniform4f(s_fx.dofRangesLoc,
                dof_config.near_blur_depth,
                dof_config.near_clear_depth,
                dof_config.far_clear_depth,
                dof_config.far_blur_depth);
    glUniform1f(s_fx.dofMaxRadiusLoc, dof_config.max_radius_pixels);
    glUniform1i(s_fx.viewportCountLoc, (GLint)viewport_count);
    float viewport_rects[16] = {};
    const size_t rect_count = viewport_count < 4 ? viewport_count : 4;
    for (size_t i = 0; i < rect_count; ++i) {
        viewport_rects[i * 4 + 0] = (float)viewports[i].x / (float)width;
        viewport_rects[i * 4 + 1] = (float)viewports[i].y / (float)height;
        viewport_rects[i * 4 + 2] = (float)viewports[i].width / (float)width;
        viewport_rects[i * 4 + 3] = (float)viewports[i].height / (float)height;
    }
    glUniform4fv(s_fx.viewportRectsLoc, 4, viewport_rects);
    glBindVertexArray(s_fx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_fx.vbo);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (!s_fx.announced) {
        s_fx.announced = 1;
        sysLogPrintf(LOG_INFO,
            "graphics: Ascension adaptive PostFX active (%d%% sharpen, dof=%d depth=%ux%u viewports=%zu, %dx%d)",
            effective_sharpen, dof_apply, depth_width, depth_height, viewport_count,
            width, height);
    }
}

extern "C" void ascensionPostFxShutdown(void)
{
    if (s_fx.vbo) glDeleteBuffers(1, &s_fx.vbo);
    if (s_fx.vao) glDeleteVertexArrays(1, &s_fx.vao);
    if (s_fx.texture) glDeleteTextures(1, &s_fx.texture);
    if (s_fx.program) glDeleteProgram(s_fx.program);

    const int failed = s_fx.failed;
    std::memset(&s_fx, 0, sizeof(s_fx));
    s_fx.failed = failed;
}
