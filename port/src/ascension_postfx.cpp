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

PD_CONSTRUCTOR static void ascensionPostFxConfigInit(void)
{
    configRegisterInt("Video.PostFX", &s_postFxEnabled, 0, 1);
    configRegisterInt("Video.Sharpen", &s_sharpenAmount, 0, 100);
}

struct AscPostFxState {
    GLuint program;
    GLuint texture;
    GLuint vao;
    GLuint vbo;
    GLint sceneLoc;
    GLint invSizeLoc;
    GLint strengthLoc;
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
in vec2 aPosition;
in vec2 aTexCoord;
out vec2 vUV;

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
uniform sampler2D uScene;
uniform vec2 uInvSize;
uniform float uStrength;
in vec2 vUV;
out vec4 fragColor;

float ascLuma(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    vec4 src = texture(uScene, vUV);
    vec3 c  = src.rgb;
    vec3 n  = texture(uScene, vUV + vec2(0.0,  uInvSize.y)).rgb;
    vec3 s  = texture(uScene, vUV + vec2(0.0, -uInvSize.y)).rgb;
    vec3 e  = texture(uScene, vUV + vec2( uInvSize.x, 0.0)).rgb;
    vec3 w  = texture(uScene, vUV + vec2(-uInvSize.x, 0.0)).rgb;
    vec3 ne = texture(uScene, vUV + vec2( uInvSize.x,  uInvSize.y)).rgb;
    vec3 nw = texture(uScene, vUV + vec2(-uInvSize.x,  uInvSize.y)).rgb;
    vec3 se = texture(uScene, vUV + vec2( uInvSize.x, -uInvSize.y)).rgb;
    vec3 sw = texture(uScene, vUV + vec2(-uInvSize.x, -uInvSize.y)).rgb;

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

    fragColor = vec4(clamp(sharpened, 0.0, 1.0), src.a);
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
        !glDrawBuffer) {
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
    s_fx.invSizeLoc = glGetUniformLocation(s_fx.program, "uInvSize");
    s_fx.strengthLoc = glGetUniformLocation(s_fx.program, "uStrength");

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

extern "C" void ascensionPostFxApply(void)
{
    const int sharpen = ascPostFxSharpen();
    if (!gePostFxShouldApply(ascPostFxEnabled(), sharpen)) return;
    if (!ascPostFxInit()) return;

    const int width = (int)gfx_current_window_dimensions.width;
    const int height = (int)gfx_current_window_dimensions.height;
    if (width <= 1 || height <= 1) return;

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
    glDrawBuffer(GL_BACK);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);

    glUseProgram(s_fx.program);
    glUniform1i(s_fx.sceneLoc, 0);
    glUniform2f(s_fx.invSizeLoc, 1.0f / (float)width, 1.0f / (float)height);
    glUniform1f(s_fx.strengthLoc, (float)sharpen / 100.0f);
    glBindVertexArray(s_fx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_fx.vbo);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (!s_fx.announced) {
        s_fx.announced = 1;
        sysLogPrintf(LOG_INFO,
            "graphics: Ascension adaptive PostFX active (%d%% sharpen, %dx%d)",
            sharpen, width, height);
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
