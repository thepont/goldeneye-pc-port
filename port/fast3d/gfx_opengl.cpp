#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <cstring>
#include <cmath>
#include <limits>

#include <map>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "glad/glad.h"

#include "bump_mapping_policy.h"
#include "shadow_mapping_policy.h"
#include "gfx_cc.h"
#include "gfx_water_composite_policy.h"
#include "gfx_water_heightfield_policy.h"
#include "gfx_rendering_api.h"
#include "gfx_pc.h"

using namespace std;

struct ShaderProgram {
    GLuint opengl_program_id;
    uint8_t num_inputs;
    bool used_textures[SHADER_MAX_TEXTURES];
    uint8_t num_floats;
    GLint attrib_locations[16];
    uint8_t attrib_sizes[16];
    uint8_t num_attribs;
    GLint frame_count_location;
    GLint noise_scale_location;
    GLint three_point_filter_locations[2];
    GLint bump_map_location;
    GLint bump_map_enabled_location;
    GLint advanced_lighting_enabled_location;
    GLint advanced_light_direction_location;
    GLint advanced_light_color_location;
    GLint advanced_specular_color_location;
    GLint advanced_ambient_location;
    GLint advanced_shininess_location;
    GLint linear_dynamic_lighting_enabled_location;
    GLint shadow_map_location;
    GLint shadow_mapping_enabled_location;
    GLint shadow_light_space_location;
    GLint shadow_map_texel_size_location;
    bool advanced_lighting_eligible;
    bool linear_lighting_eligible;
    bool shadow_mapping_eligible;
};

struct Framebuffer {
    uint32_t width, height;
    bool has_depth_buffer;
    uint32_t msaa_level;
    bool invert_y;

    GLuint fbo, clrbuf, clrbuf_msaa, rbo;
};

static std::map<pair<uint64_t, uint32_t>, struct ShaderProgram> shader_program_pool;
static GLuint opengl_vbo;
static GLuint opengl_vao;
static bool current_depth_mask;

static uint32_t frame_count;

static std::vector<Framebuffer> framebuffers;
static size_t current_framebuffer;
static GLuint water_composite_program;
static GLint water_composite_position_location = -1;
static GLint water_composite_water_uv_location = -1;
static GLint water_composite_view_position_location = -1;
static GLint water_composite_world_position_location = -1;
static GLint water_composite_surface_plane_location = -1;
static GLint water_composite_view_plane_location = -1;
static GLint water_composite_tangent_location = -1;
static GLint water_composite_bitangent_location = -1;
static GLint water_composite_surface_id_location = -1;
static GLint water_composite_scene_location = -1;
static GLint water_composite_depth_location = -1;
static GLint water_composite_scene_depth_valid_location = -1;
static GLint water_composite_line_location = -1;
static GLint water_composite_time_location = -1;
static GLint water_composite_tier_location = -1;
static GLint water_composite_inverse_viewport_location = -1;
static GLint water_composite_scene_viewport_location = -1;
static GLint water_composite_framebuffer_viewport_location = -1;
static GLint water_composite_base_location = -1;
static GLint water_composite_normal_location = -1;
static GLint water_composite_reflection_location = -1;
static GLint water_composite_reflection_atlas_location = -1;
static GLint water_composite_viewport_location = -1;
static GLint water_composite_inverse_projection_location = -1;
static GLint water_composite_world_to_clip_location = -1;
static GLint water_composite_height_location = -1;
static GLint water_composite_height_texel_location = -1;
static GLint water_composite_heightfield_location = -1;
static GLint water_composite_heightfield_scale_location = -1;
static GLint water_composite_world_to_view_location = -1;
static GLint water_composite_forward_projection_location = -1;
static GLint water_composite_normal_matrix_location = -1;
static GLint water_composite_depth_params_location = -1;
static GLint water_composite_absorption_location = -1;
static GLint water_composite_displacement_location = -1;
static GLint water_composite_disturbances_location = -1;
static GLint water_composite_disturbance_count_location = -1;
static GLint water_composite_wave_length_location = -1;
static GLint water_composite_wave_amplitude_location = -1;
static GLint water_composite_wave_speed_location = -1;
static GLint water_composite_wave_direction_location = -1;
static GLint water_composite_contact_start_location = -1;
static GLint water_composite_contact_band_location = -1;
static GLint water_composite_feature_toggles_location = -1;
static GLint water_composite_reflection_valid_location = -1;
static GLint water_composite_light_direction_location = -1;
static GLint water_composite_light_color_location = -1;
static GLint water_composite_shininess_location = -1;
static GLuint water_composite_vbo;
static GLuint water_scene_fbo;
static GLuint water_scene_color;
static GLuint water_scene_depth;
static GLuint water_reflection_fbo;
static GLuint water_reflection_color;
static GLuint water_reflection_depth;
static GLuint water_material_base_texture;
static GLuint water_material_normal_texture;
static GLuint water_material_height_texture;
static GLuint water_heightfield_texture;
static GLint water_heightfield_texture_unit = -1;
static uint32_t water_scene_width;
static uint32_t water_scene_height;
static bool water_scene_hdr;
static unsigned int water_composite_geometry_trace_events;
static unsigned int water_composite_shader_trace_events;
static unsigned int water_heightfield_trace_events;
/* Reflection validity is tied to replayed geometry, not merely to an FBO that
 * was allocated and cleared. These flags are kept at the backend boundary so
 * the display-list replay can report an empty reflected scene deterministically. */
static bool water_reflection_capture_active;
static unsigned int water_reflection_draw_calls;
static GLuint postfx_depth_fbo;
static GLuint postfx_depth_texture;
static uint32_t postfx_depth_width;
static uint32_t postfx_depth_height;
static bool postfx_depth_valid;
static GLuint shadow_map_fbo;
static GLuint shadow_map_texture;
static GLuint shadow_map_vbo;
static GLuint shadow_map_vao;
static GLuint shadow_map_program;
static GLint shadow_map_position_location = -1;
static GLint shadow_map_matrix_location = -1;
static bool shadow_map_ready;
static bool shadow_resources_failed;
static GeShadowMatrix shadow_light_space = {};
static constexpr uint32_t kShadowMapResolution = 1024;
static uint32_t shadow_map_resolution = kShadowMapResolution;
static uint32_t shadow_map_allocated_resolution;
static float current_noise_scale;
static int current_anisotropy_level = 4;   /* B1: sane default (0 was invalid for GL_TEXTURE_MAX_ANISOTROPY) */
static FilteringMode current_filter_mode = FILTER_LINEAR;
static MipmapFilteringMode current_mipmap_filter_mode = MIPMAP_LINEAR;
static bool current_textures_linear_filter[3] = {false, false, false};
static bool current_bump_map_enabled = false;
static GLint current_bump_map_enabled_location = -1;

static int gl_glsl_version = 130;
static char gl_glsl_version_str[16] = "130";
static GLenum gl_mirror_clamp = GL_MIRROR_CLAMP_TO_EDGE;
static bool gl_es = false;
static bool gl_core_profile = false;
static bool framebuffer_srgb_control_available = false;

static bool gfx_opengl_is_framebuffer_srgb_enabled(void)
{
    return framebuffer_srgb_control_available &&
           glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_TRUE;
}

static void gfx_opengl_set_framebuffer_srgb(bool enabled)
{
    if (!framebuffer_srgb_control_available) {
        return;
    }
    if (enabled) {
        glEnable(GL_FRAMEBUFFER_SRGB);
    } else {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
}

static void gfx_opengl_release_water_resources(void);

extern "C" const char *gfx_opengl_get_glsl_version(void) {
    return gl_glsl_version_str;
}

static int gfx_opengl_get_max_texture_size() {
    GLint max_texture_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    return max_texture_size;
}

static const char* gfx_opengl_get_name() {
    return "OpenGL";
}

static struct GfxClipParameters gfx_opengl_get_clip_parameters(void) {
    return { false, framebuffers[current_framebuffer].invert_y };
}

static void gfx_opengl_vertex_array_set_attribs(struct ShaderProgram* prg) {
    size_t num_floats = prg->num_floats;
    size_t pos = 0;

    for (int i = 0; i < prg->num_attribs; i++) {
        if (prg->attrib_locations[i] >= 0) {
            glEnableVertexAttribArray(prg->attrib_locations[i]);
            glVertexAttribPointer(prg->attrib_locations[i], prg->attrib_sizes[i], GL_FLOAT, GL_FALSE,
                                num_floats * sizeof(float), (void*)(pos * sizeof(float)));
        }
        pos += prg->attrib_sizes[i];
    }
}

static void gfx_opengl_set_uniforms(struct ShaderProgram* prg) {
    if (prg->frame_count_location >= 0) {
        glUniform1i(prg->frame_count_location, frame_count);
    }
    if (prg->noise_scale_location >= 0) {
        glUniform1f(prg->noise_scale_location, current_noise_scale);
    }
    if (prg->three_point_filter_locations[0] >= 0) {
        glUniform1i(prg->three_point_filter_locations[0], current_textures_linear_filter[0]);
    }
    if (prg->three_point_filter_locations[1] >= 0) {
        glUniform1i(prg->three_point_filter_locations[1], current_textures_linear_filter[1]);
    }
    if (prg->bump_map_enabled_location >= 0) {
        glUniform1i(prg->bump_map_enabled_location,
                    current_bump_map_enabled ? 1 : 0);
    }
    if (prg->advanced_lighting_enabled_location >= 0) {
        const bool advanced_lighting =
            current_bump_map_enabled &&
            gfx_get_render_mode() >= GFX_RENDER_ENHANCED &&
            prg->advanced_lighting_eligible;
        glUniform1i(prg->advanced_lighting_enabled_location,
                    advanced_lighting ? 1 : 0);
    }
    if (prg->advanced_light_direction_location >= 0) {
        /* A stable view-space key light keeps the optional material pass
         * deterministic while the legacy RSP lights remain authoritative for
         * Original mode. The normal-map path is the single consumer of this
         * material light; future streamed world lights can replace these
         * values without changing the shader contract. */
        glUniform3f(prg->advanced_light_direction_location,
                    geShadowDefaultLightDirection[0],
                    geShadowDefaultLightDirection[1],
                    geShadowDefaultLightDirection[2]);
    }
    if (prg->advanced_light_color_location >= 0) {
        glUniform3f(prg->advanced_light_color_location,
                    0.92f, 0.96f, 1.0f);
    }
    if (prg->advanced_specular_color_location >= 0) {
        glUniform3f(prg->advanced_specular_color_location,
                    1.0f, 1.0f, 1.0f);
    }
    if (prg->advanced_ambient_location >= 0) {
        glUniform3f(prg->advanced_ambient_location,
                    0.34f, 0.38f, 0.44f);
    }
    if (prg->advanced_shininess_location >= 0) {
        glUniform1f(prg->advanced_shininess_location, 32.0f);
    }
    if (prg->linear_dynamic_lighting_enabled_location >= 0) {
        const bool linear_dynamic_lighting =
            gfx_get_render_mode() >= GFX_RENDER_ENHANCED &&
            prg->linear_lighting_eligible;
        glUniform1i(prg->linear_dynamic_lighting_enabled_location,
                    linear_dynamic_lighting ? 1 : 0);
    }
    if (prg->shadow_mapping_enabled_location >= 0) {
        const bool enabled = shadow_map_ready &&
                             gfx_get_render_mode() >= GFX_RENDER_ENHANCED &&
                             prg->shadow_mapping_eligible;
        glUniform1i(prg->shadow_mapping_enabled_location, enabled ? 1 : 0);
        if (prg->shadow_map_location >= 0) {
            glUniform1i(prg->shadow_map_location, 3);
        }
        if (prg->shadow_light_space_location >= 0) {
            glUniformMatrix4fv(prg->shadow_light_space_location, 1, GL_FALSE,
                               shadow_light_space.column_major);
        }
        if (prg->shadow_map_texel_size_location >= 0) {
            const float inverse_resolution =
                1.0f / static_cast<float>(shadow_map_resolution);
            glUniform2f(prg->shadow_map_texel_size_location,
                        inverse_resolution, inverse_resolution);
        }
        GLint active_texture = GL_TEXTURE0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, shadow_map_texture);
        glActiveTexture(static_cast<GLenum>(active_texture));
    }
}

static void gfx_opengl_unload_shader(struct ShaderProgram* old_prg) {
    if (old_prg != NULL) {
        for (int i = 0; i < old_prg->num_attribs; i++) {
            if (old_prg->attrib_locations[i] >= 0) {
                glDisableVertexAttribArray(old_prg->attrib_locations[i]);
            }
        }
    }
}

static void gfx_opengl_load_shader(struct ShaderProgram* new_prg) {
    // if (!new_prg) return;
    glUseProgram(new_prg->opengl_program_id);
    gfx_opengl_vertex_array_set_attribs(new_prg);
    current_bump_map_enabled_location = new_prg->bump_map_enabled_location;
    gfx_opengl_set_uniforms(new_prg);
}

static void append_str(char* buf, size_t* len, const char* str) {
    while (*str != '\0') {
        buf[(*len)++] = *str++;
    }
}

static void append_line(char* buf, size_t* len, const char* str) {
    while (*str != '\0') {
        buf[(*len)++] = *str++;
    }
    buf[(*len)++] = '\n';
}

#define RAND_NOISE "((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)"

static const char* shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha,
                                      bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_1:
                return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_TEXEL0:
                return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a"
                                           : (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)"
                                                         : "vec3(texVal0.a, texVal0.a, texVal0.a)");
            case SHADER_TEXEL1A:
                return hint_single_element ? "texVal1.a"
                                           : (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)"
                                                         : "vec3(texVal1.a, texVal1.a, texVal1.a)");
            case SHADER_TEXEL1:
                return with_alpha ? "texVal1" : "texVal1.rgb";
            case SHADER_COMBINED:
                return with_alpha ? "texel" : "texel.rgb";
            case SHADER_NOISE:
                return with_alpha ? "vec4(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")"
                                  : "vec3(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")";
        }
    } else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_1:
                return "1.0";
            case SHADER_INPUT_1:
                return "vInput1.a";
            case SHADER_INPUT_2:
                return "vInput2.a";
            case SHADER_INPUT_3:
                return "vInput3.a";
            case SHADER_INPUT_4:
                return "vInput4.a";
            case SHADER_TEXEL0:
                return "texVal0.a";
            case SHADER_TEXEL0A:
                return "texVal0.a";
            case SHADER_TEXEL1A:
                return "texVal1.a";
            case SHADER_TEXEL1:
                return "texVal1.a";
            case SHADER_COMBINED:
                return "texel.a";
            case SHADER_NOISE:
                return RAND_NOISE;
        }
    }
    return "";
}

#undef RAND_NOISE

static void append_formula(char* buf, size_t* len, uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix,
                           bool with_alpha, bool only_alpha, bool opt_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    } else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
    } else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, ")");
    } else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
}

static struct ShaderProgram* gfx_opengl_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1) {
    static int water_shader_trace = -1;
    static unsigned int water_shader_trace_events = 0;
    struct CCFeatures cc_features = { 0 };
    gfx_cc_get_features(shader_id0, shader_id1, &cc_features);
    const bool shadow_mapping =
        (shader_id1 & SHADER_OPT_SHADOW_MAPPING) != 0;
    const GeFast3dMaterialRequirements material_requirements =
        geFast3dMaterialRequirements(cc_features.used_textures[0],
                                     gfx_get_bump_maps_enabled(),
                                     shadow_mapping ? 1 : 0);
    const bool uses_view_position =
        material_requirements.uses_view_position != 0;
    const bool linear_lighting_eligible =
        !cc_features.opt_alpha && !cc_features.opt_fog &&
        !cc_features.opt_grayscale;
    const bool water_material = (shader_id1 & SHADER_OPT_WATER) != 0;
    const bool water_remaster = (shader_id1 & SHADER_OPT_WATER_REMASTER) != 0;

    if (water_material) {
        if (water_shader_trace < 0) {
            water_shader_trace = getenv("GE_WATER_TRACE") != nullptr;
        }
        if (water_shader_trace && water_shader_trace_events < 4) {
            sysLogPrintf(LOG_NOTE, "WATER_SHADER: tier=%d textures=%d uv=shared bump=height-gradient capture=opaque",
                         water_remaster ? 2 : 1,
                         cc_features.used_textures[0] ? 1 : 0);
            ++water_shader_trace_events;
        }
    }

    char vs_buf[2048];
    /* Bump-map declarations and the diffuse-only normal contribution are
     * emitted into this generated shader alongside the existing combiner
     * code. Keep headroom so a valid material cannot overrun the buffer. */
    char fs_buf[16384];
    size_t vs_len = 0;
    size_t fs_len = 0;
    size_t num_floats = 4;

    // Vertex shader

    vs_len += sprintf(vs_buf + vs_len, "#version %s\n", gl_glsl_version_str);

    if (gl_es) {
        append_line(vs_buf, &vs_len, "precision mediump float;");
    }

    if (gl_glsl_version >= 130) {
        append_line(vs_buf, &vs_len, "#define INPUT in");
        append_line(vs_buf, &vs_len, "#define OUTPUT out");
    } else {
        append_line(vs_buf, &vs_len, "#define INPUT attribute");
        append_line(vs_buf, &vs_len, "#define OUTPUT varying");
    }

    append_line(vs_buf, &vs_len, "INPUT vec4 aVtxPos;");
    append_line(vs_buf, &vs_len, "INPUT vec3 aLinearDynamicLight;");
    append_line(vs_buf, &vs_len, "OUTPUT vec3 vLinearDynamicLight;");
    num_floats += 3;
    if (uses_view_position) {
        append_line(vs_buf, &vs_len, "INPUT vec3 aVtxViewPos;");
        append_line(vs_buf, &vs_len, "OUTPUT vec3 vVtxViewPos;");
        num_floats += 3;
    }

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            vs_len += sprintf(vs_buf + vs_len, "INPUT vec2 aTexCoord%d;\n", i);
            vs_len += sprintf(vs_buf + vs_len, "OUTPUT vec2 vTexCoord%d;\n", i);
            num_floats += 2;
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    vs_len += sprintf(vs_buf + vs_len, "INPUT float aTexClamp%s%d;\n", j == 0 ? "S" : "T", i);
                    vs_len += sprintf(vs_buf + vs_len, "OUTPUT float vTexClamp%s%d;\n", j == 0 ? "S" : "T", i);
                    num_floats += 1;
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "INPUT vec4 aFog;");
        append_line(vs_buf, &vs_len, "OUTPUT vec4 vFog;");
        num_floats += 4;
    }

    if (cc_features.opt_grayscale) {
        append_line(vs_buf, &vs_len, "INPUT vec4 aGrayscaleColor;");
        append_line(vs_buf, &vs_len, "OUTPUT vec4 vGrayscaleColor;");
        num_floats += 4;
    }

    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "INPUT vec%d aInput%d;\n", cc_features.opt_alpha ? 4 : 3, i + 1);
        vs_len += sprintf(vs_buf + vs_len, "OUTPUT vec%d vInput%d;\n", cc_features.opt_alpha ? 4 : 3, i + 1);
        num_floats += cc_features.opt_alpha ? 4 : 3;
    }

    append_line(vs_buf, &vs_len, "void main() {");
    append_line(vs_buf, &vs_len, "    vLinearDynamicLight = aLinearDynamicLight;");
    if (uses_view_position) {
        append_line(vs_buf, &vs_len, "    vVtxViewPos = aVtxViewPos;");
    }
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            vs_len += sprintf(vs_buf + vs_len, "    vTexCoord%d = aTexCoord%d;\n", i, i);
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    vs_len += sprintf(vs_buf + vs_len, "    vTexClamp%s%d = aTexClamp%s%d;\n", j == 0 ? "S" : "T", i,
                                      j == 0 ? "S" : "T", i);
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "    vFog = aFog;");
    }
    if (cc_features.opt_grayscale) {
        append_line(vs_buf, &vs_len, "    vGrayscaleColor = aGrayscaleColor;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "    vInput%d = aInput%d;\n", i + 1, i + 1);
    }

    append_line(vs_buf, &vs_len, "    gl_Position = aVtxPos;");
    if (!GLAD_GL_ARB_depth_clamp) {
        // HACK: workaround for no GL_DEPTH_CLAMP
        append_line(vs_buf, &vs_len, "    gl_Position.z *= 0.3f;");
    }
    append_line(vs_buf, &vs_len, "}");

    // Fragment shader

    fs_len += sprintf(fs_buf + fs_len, "#version %s\n", gl_glsl_version_str);

    if (gl_es) {
        append_line(fs_buf, &fs_len, "precision mediump float;");
    }

    if (gl_glsl_version >= 130) {
        append_line(fs_buf, &fs_len, "#define INPUT in");
        append_line(fs_buf, &fs_len, "#define OUTPUT_COLOR outColor");
        append_line(fs_buf, &fs_len, "#define SAMPLE_TEX(tex, uv) texture(tex, uv)");
    } else {
        append_line(fs_buf, &fs_len, "#define INPUT varying");
        append_line(fs_buf, &fs_len, "#define OUTPUT_COLOR gl_FragColor");
        append_line(fs_buf, &fs_len, "#define SAMPLE_TEX(tex, uv) texture2D(tex, uv)");
    }

    // Reference approach to color wrapping as per GLideN64
    // Return wrapped value of x in interval [low, high)
    append_line(fs_buf, &fs_len, "#define WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)");

    append_line(fs_buf, &fs_len, "#define TEX_OFFSET(tex, uv, texSize, off) SAMPLE_TEX(tex, uv - (off)/texSize)");

    // append_line(fs_buf, &fs_len, "precision mediump float;");
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            fs_len += sprintf(fs_buf + fs_len, "INPUT vec2 vTexCoord%d;\n", i);
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    fs_len += sprintf(fs_buf + fs_len, "INPUT float vTexClamp%s%d;\n", j == 0 ? "S" : "T", i);
                }
            }
        }
    }
    if (uses_view_position) {
        append_line(fs_buf, &fs_len, "INPUT vec3 vVtxViewPos;");
    }
    append_line(fs_buf, &fs_len, "INPUT vec3 vLinearDynamicLight;");
    if (cc_features.opt_fog) {
        append_line(fs_buf, &fs_len, "INPUT vec4 vFog;");
    }
    if (cc_features.opt_grayscale) {
        append_line(fs_buf, &fs_len, "INPUT vec4 vGrayscaleColor;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        fs_len += sprintf(fs_buf + fs_len, "INPUT vec%d vInput%d;\n", cc_features.opt_alpha ? 4 : 3, i + 1);
    }

    if (cc_features.used_textures[0]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex0;");
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "uniform int three_point_filter0;");
    }
    if (cc_features.used_textures[1]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex1;");
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "uniform int three_point_filter1;");
    }

    /* The optional ROM-derived normal map is kept outside the two N64
     * combiner texture units. Do not even add its auxiliary stream to the
     * compatibility shader when the feature is disabled: Original mode must
     * retain the committed position/texture vertex contract. */
    if (material_requirements.uses_bump_mapping) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uBumpMap;");
    }
    if (uses_view_position) {
        append_line(fs_buf, &fs_len, "uniform int uBumpMapEnabled;");
        append_line(fs_buf, &fs_len, "uniform int uAdvancedLightingEnabled;");
        append_line(fs_buf, &fs_len, "uniform vec3 uAdvancedLightDirection;");
        append_line(fs_buf, &fs_len, "uniform vec3 uAdvancedLightColor;");
        append_line(fs_buf, &fs_len, "uniform vec3 uAdvancedSpecularColor;");
        append_line(fs_buf, &fs_len, "uniform vec3 uAdvancedAmbient;");
        append_line(fs_buf, &fs_len, "uniform float uAdvancedShininess;");
    }
    if (shadow_mapping) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uShadowMap;");
        append_line(fs_buf, &fs_len, "uniform int uShadowMappingEnabled;");
        append_line(fs_buf, &fs_len, "uniform mat4 uShadowLightSpace;");
        append_line(fs_buf, &fs_len, "uniform vec2 uShadowMapTexelSize;");
    }
    append_line(fs_buf, &fs_len, "uniform int uLinearDynamicLightingEnabled;");
    if (uses_view_position || linear_lighting_eligible) {
        append_line(fs_buf, &fs_len, "vec3 advancedDisplayToLinear(vec3 value) {");
        append_line(fs_buf, &fs_len, "    value = clamp(value, vec3(0.0), vec3(1.0));");
        append_line(fs_buf, &fs_len, "    return vec3(value.r <= 0.04045 ? value.r / 12.92 : pow((value.r + 0.055) / 1.055, 2.4),");
        append_line(fs_buf, &fs_len, "        value.g <= 0.04045 ? value.g / 12.92 : pow((value.g + 0.055) / 1.055, 2.4),");
        append_line(fs_buf, &fs_len, "        value.b <= 0.04045 ? value.b / 12.92 : pow((value.b + 0.055) / 1.055, 2.4));");
        append_line(fs_buf, &fs_len, "}");
        append_line(fs_buf, &fs_len, "vec3 advancedLinearToDisplay(vec3 value) {");
        append_line(fs_buf, &fs_len, "    value = clamp(value, vec3(0.0), vec3(1.0));");
        append_line(fs_buf, &fs_len, "    return vec3(value.r <= 0.0031308 ? value.r * 12.92 : 1.055 * pow(value.r, 1.0 / 2.4) - 0.055,");
        append_line(fs_buf, &fs_len, "        value.g <= 0.0031308 ? value.g * 12.92 : 1.055 * pow(value.g, 1.0 / 2.4) - 0.055,");
        append_line(fs_buf, &fs_len, "        value.b <= 0.0031308 ? value.b * 12.92 : 1.055 * pow(value.b, 1.0 / 2.4) - 0.055);");
        append_line(fs_buf, &fs_len, "}");
        append_line(fs_buf, &fs_len, "vec3 advancedSafeNormalize(vec3 value) {");
        append_line(fs_buf, &fs_len, "    float lengthSquared = dot(value, value);");
        append_line(fs_buf, &fs_len, "    return lengthSquared > 0.000001 && lengthSquared < 1000000000000.0 ? value / sqrt(lengthSquared) : vec3(0.0, 0.0, 1.0);");
        append_line(fs_buf, &fs_len, "}");
    }

    if (shadow_mapping) {
        append_line(fs_buf, &fs_len, "float shadowPcf(vec3 surfaceNormal) {");
        append_line(fs_buf, &fs_len, "    vec4 lightClip = uShadowLightSpace * vec4(vVtxViewPos, 1.0);");
        append_line(fs_buf, &fs_len, "    if (lightClip.w <= 0.000001) return 0.0;");
        append_line(fs_buf, &fs_len, "    vec3 projected = lightClip.xyz / lightClip.w * 0.5 + 0.5;");
        append_line(fs_buf, &fs_len, "    if (projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0 || projected.z < 0.0 || projected.z > 1.0) return 0.0;");
        append_line(fs_buf, &fs_len, "    vec3 normal = advancedSafeNormalize(surfaceNormal);");
        append_line(fs_buf, &fs_len, "    vec3 lightDirection = advancedSafeNormalize(uAdvancedLightDirection);");
        append_line(fs_buf, &fs_len, "    float normalLight = clamp(dot(normal, lightDirection), 0.0, 1.0);");
        char shadow_bias_line[192] = {};
        snprintf(shadow_bias_line, sizeof(shadow_bias_line),
                 "    float shadowBias = clamp(max(%.8g, %.8g * (1.0 - normalLight)), %.8g, %.8g);",
                 geShadowMinimumBias, geShadowSlopeBiasScale,
                 geShadowMinimumBias, geShadowMaximumBias);
        append_line(fs_buf, &fs_len, shadow_bias_line);
        append_line(fs_buf, &fs_len, "    float shadow = 0.0;");
        append_line(fs_buf, &fs_len, "    for (int x = -1; x <= 1; ++x) {");
        append_line(fs_buf, &fs_len, "        for (int y = -1; y <= 1; ++y) {");
        append_line(fs_buf, &fs_len, "            vec2 sampleUv = projected.xy + vec2(float(x), float(y)) * uShadowMapTexelSize;");
        append_line(fs_buf, &fs_len, "            if (sampleUv.x >= 0.0 && sampleUv.x <= 1.0 && sampleUv.y >= 0.0 && sampleUv.y <= 1.0) {");
        append_line(fs_buf, &fs_len, "                float closestDepth = SAMPLE_TEX(uShadowMap, sampleUv).r;");
        append_line(fs_buf, &fs_len, "                shadow += projected.z - shadowBias > closestDepth ? 1.0 : 0.0;");
        append_line(fs_buf, &fs_len, "            }");
        append_line(fs_buf, &fs_len, "        }");
        append_line(fs_buf, &fs_len, "    }");
        append_line(fs_buf, &fs_len, "    return shadow / 9.0;");
        append_line(fs_buf, &fs_len, "}");
    }

    append_line(fs_buf, &fs_len, "uniform int frame_count;");
    append_line(fs_buf, &fs_len, "uniform float noise_scale;");

    append_line(fs_buf, &fs_len, "float random(in vec3 value) {");
    append_line(fs_buf, &fs_len, "    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));");
    append_line(fs_buf, &fs_len, "    return fract(sin(random) * 143758.5453);");
    append_line(fs_buf, &fs_len, "}");

    if (current_filter_mode == FILTER_THREE_POINT) {
        append_line(fs_buf, &fs_len, "vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {");
        append_line(fs_buf, &fs_len, "    vec2 offset = fract(texCoord*texSize - vec2(0.5));");
        append_line(fs_buf, &fs_len, "    offset -= step(1.0, offset.x + offset.y);");
        append_line(fs_buf, &fs_len, "    vec4 c0 = TEX_OFFSET(tex, texCoord, texSize, offset);");
        append_line(fs_buf, &fs_len, "    vec4 c1 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x - sign(offset.x), offset.y));");
        append_line(fs_buf, &fs_len, "    vec4 c2 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x, offset.y - sign(offset.y)));");
        append_line(fs_buf, &fs_len, "    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);");
        append_line(fs_buf, &fs_len, "}");
    }

    if (cc_features.opt_blur) {
        // blur filter, used for menu backgrounds
        // used to be two for loops from 0 to 4, but apparently intel drivers crashed trying to unroll it
        // used to have a const weight array, but apparently drivers for the GT620 don't like const array initializers

        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "lowp vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize, in int three_point_filter) {");
        else
            append_line(fs_buf, &fs_len, "lowp vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize) {");

        append_line(fs_buf, &fs_len, "    lowp vec4 cw = vec4(0.0);");
        append_line(fs_buf, &fs_len, "    for (int i = 0; i < 16; ++i) {");
        append_line(fs_buf, &fs_len, "        vec2 xy = vec2(float(i & 3), float(i >> 2));");
        append_line(fs_buf, &fs_len, "        lowp float w = 0.009947 - length(xy) * 0.001;");
        append_line(fs_buf, &fs_len, "        vec2 scaled_uv = uv + (vec2(-1.5) + xy) / texSize;");

        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "        lowp vec4 tex = mix(SAMPLE_TEX(t, scaled_uv), filter3point(t, scaled_uv, texSize), three_point_filter);");
        else
            append_line(fs_buf, &fs_len, "        lowp vec4 tex = SAMPLE_TEX(t, scaled_uv);");

        append_line(fs_buf, &fs_len, "        cw += vec4(tex.rgb * w, w);");
        append_line(fs_buf, &fs_len, "    }");
        append_line(fs_buf, &fs_len, "    return vec4(cw.rgb / cw.a, 1.0);");
        append_line(fs_buf, &fs_len, "}");
    } else {
        if (current_filter_mode == FILTER_THREE_POINT) {
            append_line(fs_buf, &fs_len, "vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize, in int three_point_filter) {");
            append_line(fs_buf, &fs_len, "    return mix(SAMPLE_TEX(tex, uv), filter3point(tex, uv, texSize), three_point_filter);");
            append_line(fs_buf, &fs_len, "}");
        } else {
            append_line(fs_buf, &fs_len, "vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize) {");
            append_line(fs_buf, &fs_len, "    return SAMPLE_TEX(tex, uv);");
            append_line(fs_buf, &fs_len, "}");
        }
    }

    if (gl_glsl_version >= 130) {
        append_line(fs_buf, &fs_len, "out vec4 outColor;");
    }

    append_line(fs_buf, &fs_len, "void main() {");

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            bool s = cc_features.clamp[i][0], t = cc_features.clamp[i][1];

            fs_len += sprintf(fs_buf + fs_len, "    vec2 texSize%d = vec2(textureSize(uTex%d, 0));\n", i, i);

            if (!s && !t) {
                fs_len += sprintf(fs_buf + fs_len, "    vec2 vTexCoordAdj%d = vTexCoord%d;\n", i, i);
            } else {
                if (s && t) {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = clamp(vTexCoord%d, 0.5 / texSize%d, "
                                      "vec2(vTexClampS%d, vTexClampT%d));\n",
                                      i, i, i, i, i);
                } else if (s) {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = vec2(clamp(vTexCoord%d.s, 0.5 / "
                                      "texSize%d.s, vTexClampS%d), vTexCoord%d.t);\n",
                                      i, i, i, i, i);
                } else {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = vec2(vTexCoord%d.s, clamp(vTexCoord%d.t, "
                                      "0.5 / texSize%d.t, vTexClampT%d));\n",
                                      i, i, i, i, i);
                }
            }

            if (current_filter_mode == FILTER_THREE_POINT)
                fs_len += sprintf(fs_buf + fs_len, "    vec4 texVal%d = hookTexture2D(uTex%d, vTexCoordAdj%d, texSize%d, three_point_filter%d);\n", i, i, i, i, i);
            else
                fs_len += sprintf(fs_buf + fs_len, "    vec4 texVal%d = hookTexture2D(uTex%d, vTexCoordAdj%d, texSize%d);\n", i, i, i, i);
        }
    }

    append_line(fs_buf, &fs_len, cc_features.opt_alpha ? "    vec4 texel;" : "    vec3 texel;");
    for (int c = 0; c < (cc_features.opt_2cyc ? 2 : 1); c++) {
        append_str(fs_buf, &fs_len, "    texel = ");
        if (!cc_features.color_alpha_same[c] && cc_features.opt_alpha) {
            append_str(fs_buf, &fs_len, "vec4(");
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][0],
                           cc_features.do_multiply[c][0], cc_features.do_mix[c][0], false, false, true);
            append_str(fs_buf, &fs_len, ", ");
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][1],
                           cc_features.do_multiply[c][1], cc_features.do_mix[c][1], true, true, true);
            append_str(fs_buf, &fs_len, ")");
        } else {
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][0],
                           cc_features.do_multiply[c][0], cc_features.do_mix[c][0], cc_features.opt_alpha, false,
                           cc_features.opt_alpha);
        }
        append_line(fs_buf, &fs_len, ";");

        if (c == 0) {
            append_line(fs_buf, &fs_len, "    texel = WRAP(texel, -1.01, 1.01);");
        }
    }

    append_line(fs_buf, &fs_len, "    texel = WRAP(texel, -0.51, 1.51);");
    append_line(fs_buf, &fs_len, "    texel = clamp(texel, 0.0, 1.0);");

    if (material_requirements.uses_bump_mapping) {
        append_line(fs_buf, &fs_len, "    if (uBumpMapEnabled != 0) {");
        append_line(fs_buf, &fs_len,
                    "        vec3 bumpSample = SAMPLE_TEX(uBumpMap, vTexCoordAdj0).xyz * 2.0 - 1.0;");
        append_line(fs_buf, &fs_len,
                    "        vec3 bumpDpDx = dFdx(vVtxViewPos);");
        append_line(fs_buf, &fs_len,
                    "        vec3 bumpDpDy = dFdy(vVtxViewPos);");
        append_line(fs_buf, &fs_len,
                    "        vec2 bumpDuDx = dFdx(vTexCoordAdj0);");
        append_line(fs_buf, &fs_len,
                    "        vec2 bumpDuDy = dFdy(vTexCoordAdj0);");
        append_line(fs_buf, &fs_len,
                    "        float bumpDet = bumpDuDx.x * bumpDuDy.y - bumpDuDx.y * bumpDuDy.x;");
        append_line(fs_buf, &fs_len,
                    "        float bumpValid = step(0.0001, abs(bumpDet));");
        append_line(fs_buf, &fs_len,
                    "        float bumpSafeDet = abs(bumpDet) < 0.0001 ? 0.0001 : bumpDet;");
        append_line(fs_buf, &fs_len,
                    "        vec3 bumpTangent = advancedSafeNormalize((bumpDpDx * bumpDuDy.y - bumpDpDy * bumpDuDx.y) / bumpSafeDet);");
        append_line(fs_buf, &fs_len,
                    "        vec3 bumpBitangent = advancedSafeNormalize((-bumpDpDx * bumpDuDy.x + bumpDpDy * bumpDuDx.x) / bumpSafeDet);");
        append_line(fs_buf, &fs_len,
                    "        vec3 bumpSurfaceNormal = advancedSafeNormalize(cross(bumpTangent, bumpBitangent));");
        append_line(fs_buf, &fs_len,
                    "        vec3 bumpNormal = advancedSafeNormalize(bumpTangent * bumpSample.x + bumpBitangent * bumpSample.y + bumpSurfaceNormal * bumpSample.z);");
        append_line(fs_buf, &fs_len,
                    "        bumpNormal = mix(vec3(0.0, 0.0, 1.0), bumpNormal, bumpValid);");
        append_line(fs_buf, &fs_len, "        if (uAdvancedLightingEnabled != 0) {");
        append_line(fs_buf, &fs_len, "            vec3 advancedLightDirection = advancedSafeNormalize(uAdvancedLightDirection);");
        append_line(fs_buf, &fs_len, "            vec3 advancedViewDirection = advancedSafeNormalize(-vVtxViewPos);");
        append_line(fs_buf, &fs_len, "            vec3 halfway = advancedSafeNormalize(advancedLightDirection + advancedViewDirection);");
        append_line(fs_buf, &fs_len, "            float diffuse = max(dot(bumpNormal, advancedLightDirection), 0.0);");
        append_line(fs_buf, &fs_len, "            float specular = diffuse > 0.0 ? pow(max(dot(bumpNormal, halfway), 0.0), clamp(uAdvancedShininess, 1.0, 128.0)) : 0.0;");
        append_line(fs_buf, &fs_len, "            vec3 linearTexel = advancedDisplayToLinear(texel.rgb);");
        if (shadow_mapping) {
            append_line(fs_buf, &fs_len, "            float shadow = uShadowMappingEnabled != 0 ? shadowPcf(bumpNormal) : 0.0;");
            append_line(fs_buf, &fs_len, "            vec3 linearColour = linearTexel * (uAdvancedAmbient + uAdvancedLightColor * diffuse * (1.0 - shadow)) + uAdvancedSpecularColor * specular * 0.32 * (1.0 - shadow);");
        } else {
            append_line(fs_buf, &fs_len, "            vec3 linearColour = linearTexel * (uAdvancedAmbient + uAdvancedLightColor * diffuse) + uAdvancedSpecularColor * specular * 0.32;");
        }
        append_line(fs_buf, &fs_len, "            if (uLinearDynamicLightingEnabled != 0) linearColour += vLinearDynamicLight;");
        append_line(fs_buf, &fs_len, "            texel.rgb = advancedLinearToDisplay(clamp(linearColour, vec3(0.0), vec3(1.0)));");
        append_line(fs_buf, &fs_len, "        } else {");
        append_line(fs_buf, &fs_len, "            float bumpLighting = clamp(0.82 + bumpNormal.z * 0.30, 0.65, 1.15);");
        append_line(fs_buf, &fs_len, "            texel.rgb *= bumpLighting;");
        append_line(fs_buf, &fs_len, "        }");
        append_line(fs_buf, &fs_len, "    }");
    }

    if (shadow_mapping && linear_lighting_eligible) {
        append_line(fs_buf, &fs_len, "    if (uShadowMappingEnabled != 0 && (uBumpMapEnabled == 0 || uAdvancedLightingEnabled == 0)) {");
        append_line(fs_buf, &fs_len, "        vec3 geometricNormal = advancedSafeNormalize(cross(dFdx(vVtxViewPos), dFdy(vVtxViewPos)));");
        append_line(fs_buf, &fs_len, "        if (!gl_FrontFacing) geometricNormal = -geometricNormal;");
        append_line(fs_buf, &fs_len, "        float shadow = shadowPcf(geometricNormal);");
        append_line(fs_buf, &fs_len, "        float direct = max(dot(geometricNormal, advancedSafeNormalize(uAdvancedLightDirection)), 0.0);");
        append_line(fs_buf, &fs_len, "        vec3 linearTexel = advancedDisplayToLinear(texel.rgb);");
        append_line(fs_buf, &fs_len, "        texel.rgb = advancedLinearToDisplay(clamp(linearTexel * (0.78 + 0.30 * direct * (1.0 - shadow)), vec3(0.0), vec3(1.0))); ");
        append_line(fs_buf, &fs_len, "    }");
    }

    if (linear_lighting_eligible) {
        append_line(fs_buf, &fs_len, "    if (uLinearDynamicLightingEnabled != 0");
        if (uses_view_position) {
            append_line(fs_buf, &fs_len,
                        "        && (uBumpMapEnabled == 0 || uAdvancedLightingEnabled == 0)");
        }
        append_line(fs_buf, &fs_len, "    ) {");
        append_line(fs_buf, &fs_len, "        vec3 linearTexel = advancedDisplayToLinear(texel.rgb);");
        append_line(fs_buf, &fs_len, "        vec3 linearColour = linearTexel + vLinearDynamicLight;");
        append_line(fs_buf, &fs_len, "        texel.rgb = advancedLinearToDisplay(clamp(linearColour, vec3(0.0), vec3(1.0)));");
        append_line(fs_buf, &fs_len, "    }");
    }

    /* Enhanced water is captured and shaded again after opaque world
     * geometry. Do not leave the pre-world water colour or depth in the
     * scene copy: otherwise reflection/refraction samples can just sample
     * the old water surface and the later pass cannot correctly occlude it
     * against boats, walls, or dam faces. */
    if (water_material) {
        append_line(fs_buf, &fs_len, "    discard;");
    }

    // TODO discard if alpha is 0?
    if (cc_features.opt_fog) {
        if (cc_features.opt_alpha) {
            append_line(fs_buf, &fs_len, "    texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);");
        } else {
            append_line(fs_buf, &fs_len, "    texel = mix(texel, vFog.rgb, vFog.a);");
        }
    }

    /* D266: CVG_X_ALPHA ("texedge") modelled on the RDP's actual semantics,
     * keyed on the alphacompare field. The inherited line quantized to
     * opaque/discard at a hardcoded 0.19: it kept texels the N64 discards and
     * forced survivors fully opaque, defeating the ALPHA_CVG_SEL blend.
     * GE's Surface 1 IA4 tree strip (D236/D265) rendered as a wall because of
     * it. Now: AC_DITHER -> dithered discard; AC_THRESHOLD -> discard only
     * fully-transparent; AC_NONE -> discard below 0x80 and let survivors keep
     * their alpha for blending. (AC_DECAL never sets SHADER_OPT_TEXTURE_EDGE,
     * see gfx_pc.cpp.) */
    if (cc_features.opt_texture_edge && cc_features.opt_alpha) {
        if (cc_features.opt_noise) {
            append_line(fs_buf, &fs_len,
                        "    if (texel.a < ((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)) discard;");
        } else if (cc_features.opt_alpha_threshold) {
            append_line(fs_buf, &fs_len, "    if (texel.a < 1.0 / 256.0) discard;");
        } else {
            append_line(fs_buf, &fs_len, "    if (texel.a < 0.5) discard;");
        }
    }

    if (cc_features.opt_alpha && cc_features.opt_noise && !cc_features.opt_texture_edge) {
        append_line(fs_buf, &fs_len,
                    "    texel.a *= floor(clamp(random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + "
                    "texel.a, 0.0, 1.0));");
    }

    if (cc_features.opt_grayscale) {
        append_line(fs_buf, &fs_len, "    float intensity = (texel.r + texel.g + texel.b) / 3.0;");
        append_line(fs_buf, &fs_len, "    vec3 new_texel = vGrayscaleColor.rgb * intensity;");
        append_line(fs_buf, &fs_len, "    texel.rgb = mix(texel.rgb, new_texel, vGrayscaleColor.a);");
    }

    if (cc_features.opt_alpha) {
        if (cc_features.opt_alpha_threshold) {
            append_line(fs_buf, &fs_len, "    if (texel.a < 8.0 / 256.0) discard;");
        }
        if (cc_features.opt_invisible) {
            append_line(fs_buf, &fs_len, "    texel.a = 0.0;");
        }

        append_line(fs_buf, &fs_len, "    OUTPUT_COLOR = texel;");
    } else {
        append_line(fs_buf, &fs_len, "    OUTPUT_COLOR = vec4(texel, 1.0);");
    }

    append_line(fs_buf, &fs_len, "}");

    vs_buf[vs_len] = '\0';
    fs_buf[fs_len] = '\0';

    const GLchar* sources[2] = { vs_buf, fs_buf };
    const GLint lengths[2] = { (GLint)vs_len, (GLint)fs_len };
    GLint success;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 1024;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to compile this vertex shader (ID %llx, %x):\n%s", shader_id0, shader_id1, vs_buf);
        sysFatalError("Vertex shader compilation failed:\n%s", error_log);
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 1024;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to compile this fragment shader (ID %llx, %x):\n%s", shader_id0, shader_id1, fs_buf);
        sysFatalError("Fragment shader compilation failed:\n%s", error_log);
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    glDetachShader(shader_program, vertex_shader);
    glDetachShader(shader_program, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    size_t cnt = 0;

    struct ShaderProgram* prg = &shader_program_pool[make_pair(shader_id0, shader_id1)];
    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aVtxPos");
    prg->attrib_sizes[cnt] = 4;
    ++cnt;

    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program,
                                                      "aLinearDynamicLight");
    prg->attrib_sizes[cnt] = 3;
    ++cnt;

    if (uses_view_position) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aVtxViewPos");
        prg->attrib_sizes[cnt] = 3;
        ++cnt;
    }

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            char name[32];
            sprintf(name, "aTexCoord%d", i);
            prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
            prg->attrib_sizes[cnt] = 2;
            ++cnt;

            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    sprintf(name, "aTexClamp%s%d", j == 0 ? "S" : "T", i);
                    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
                    prg->attrib_sizes[cnt] = 1;
                    ++cnt;
                }
            }
        }
    }

    if (cc_features.opt_fog) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aFog");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    if (cc_features.opt_grayscale) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aGrayscaleColor");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    for (int i = 0; i < cc_features.num_inputs; i++) {
        char name[16];
        sprintf(name, "aInput%d", i + 1);
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
        prg->attrib_sizes[cnt] = cc_features.opt_alpha ? 4 : 3;
        ++cnt;
    }

    prg->opengl_program_id = shader_program;
    prg->num_inputs = cc_features.num_inputs;
    prg->used_textures[0] = cc_features.used_textures[0];
    prg->used_textures[1] = cc_features.used_textures[1];
    prg->num_floats = num_floats;
    prg->num_attribs = cnt;
    /* Keep the explicit linear/display transfer at the edge of the opaque
     * material path. Fog, grayscale, and alpha blending have their own
     * display-space semantics in the legacy combiner and must not be mixed
     * with this first advanced-lighting slice. */
    prg->advanced_lighting_eligible = uses_view_position &&
                                      !cc_features.opt_alpha &&
                                      !cc_features.opt_fog &&
                                      !cc_features.opt_grayscale;
    prg->linear_lighting_eligible = linear_lighting_eligible;
    prg->shadow_mapping_eligible = shadow_mapping &&
                                   linear_lighting_eligible;

    glUseProgram(shader_program);

    if (cc_features.used_textures[0]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex0");
        glUniform1i(sampler_location, 0);
    }
    if (cc_features.used_textures[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex1");
        glUniform1i(sampler_location, 1);
    }

    prg->bump_map_location = glGetUniformLocation(shader_program, "uBumpMap");
    prg->bump_map_enabled_location =
        glGetUniformLocation(shader_program, "uBumpMapEnabled");
    prg->advanced_lighting_enabled_location =
        glGetUniformLocation(shader_program, "uAdvancedLightingEnabled");
    prg->advanced_light_direction_location =
        glGetUniformLocation(shader_program, "uAdvancedLightDirection");
    prg->advanced_light_color_location =
        glGetUniformLocation(shader_program, "uAdvancedLightColor");
    prg->advanced_specular_color_location =
        glGetUniformLocation(shader_program, "uAdvancedSpecularColor");
    prg->advanced_ambient_location =
        glGetUniformLocation(shader_program, "uAdvancedAmbient");
    prg->advanced_shininess_location =
        glGetUniformLocation(shader_program, "uAdvancedShininess");
    prg->linear_dynamic_lighting_enabled_location =
        glGetUniformLocation(shader_program,
                             "uLinearDynamicLightingEnabled");
    prg->shadow_map_location =
        glGetUniformLocation(shader_program, "uShadowMap");
    prg->shadow_mapping_enabled_location =
        glGetUniformLocation(shader_program, "uShadowMappingEnabled");
    prg->shadow_light_space_location =
        glGetUniformLocation(shader_program, "uShadowLightSpace");
    prg->shadow_map_texel_size_location =
        glGetUniformLocation(shader_program, "uShadowMapTexelSize");
    if (prg->shadow_map_location >= 0) {
        glUniform1i(prg->shadow_map_location, 3);
    }
    if (prg->bump_map_location >= 0) {
        glUniform1i(prg->bump_map_location, 2);
    }

    prg->frame_count_location = glGetUniformLocation(shader_program, "frame_count");
    prg->noise_scale_location = glGetUniformLocation(shader_program, "noise_scale");
    prg->three_point_filter_locations[0] = glGetUniformLocation(shader_program, "three_point_filter0");
    prg->three_point_filter_locations[1] = glGetUniformLocation(shader_program, "three_point_filter1");

    static int shader_trace = -1;
    static unsigned int shader_trace_events = 0;
    if (shader_trace < 0) shader_trace = getenv("GE_SHADER_TRACE") != nullptr;
    if (shader_trace && shader_trace_events < 12) {
        char attributes[512] = {};
        size_t attributes_length = 0;
        for (uint8_t index = 0; index < prg->num_attribs; ++index) {
            attributes_length += (size_t)snprintf(
                attributes + attributes_length,
                sizeof(attributes) - attributes_length,
                "%s%d/%u", index == 0 ? "" : ",", (int)prg->attrib_locations[index],
                (unsigned)prg->attrib_sizes[index]);
            if (attributes_length >= sizeof(attributes)) break;
        }
        sysLogPrintf(LOG_NOTE,
                     "GL_SHADER_TRACE: id=%llx/%x program=%u view=%d tex=%d,%d floats=%u attrs=%u [%s] bumpLoc=%d enabledLoc=%d",
                     (unsigned long long)shader_id0, shader_id1,
                     (unsigned)shader_program, uses_view_position ? 1 : 0,
                     cc_features.used_textures[0] ? 1 : 0,
                     cc_features.used_textures[1] ? 1 : 0,
                     (unsigned)prg->num_floats, (unsigned)prg->num_attribs,
                     attributes, (int)prg->bump_map_location,
                     (int)prg->bump_map_enabled_location);
        ++shader_trace_events;
    }

    gfx_opengl_load_shader(prg);

    return prg;
}

static struct ShaderProgram* gfx_opengl_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    auto it = shader_program_pool.find(make_pair(shader_id0, shader_id1));
    return it == shader_program_pool.end() ? nullptr : &it->second;
}

static void gfx_opengl_shader_get_info(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static void gfx_opengl_clear_shaders(void) {
    glUseProgram(0);
    for (auto& pair : shader_program_pool) {
        glDeleteProgram(pair.second.opengl_program_id);
    }
    shader_program_pool.clear();
}

static GLuint gfx_opengl_new_texture(void) {
    GLuint ret;
    glGenTextures(1, &ret);
    return ret;
}

static void gfx_opengl_delete_texture(uint32_t texID) {
    glDeleteTextures(1, &texID);
}

static void gfx_opengl_select_texture(int tile, GLuint texture_id, bool linear_filter) {
    glActiveTexture(GL_TEXTURE0 + tile);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    current_textures_linear_filter[tile] = linear_filter;
}

static void gfx_opengl_set_bump_texture(uint32_t texture_id, bool enabled)
{
    /* Binding the sidecar on unit 2 is an auxiliary operation. Importing a
     * base texture can call this immediately before glTexImage2D(), so leave
     * the caller's active unit untouched or the upload lands in texture 0 on
     * unit 2 while the real material texture on unit 0 remains empty. */
    GLint active_texture = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    current_bump_map_enabled = enabled && texture_id != 0;
    if (current_bump_map_enabled_location >= 0) {
        glUniform1i(current_bump_map_enabled_location,
                    current_bump_map_enabled ? 1 : 0);
    }
    glActiveTexture((GLenum)active_texture);
}

static void gfx_opengl_upload_texture(const uint8_t* rgba32_buf, uint32_t width, uint32_t height, bool gen_mipmaps) {
#ifdef PORT
    /* GE_TEXDUMP: PPM-dump every uploaded texture (first N) for B2/D161 triage.
     * D250: this runs on every texture upload -- cache the getenv() like the
     * gfx_pc.cpp import_texture() sites (uncached getenv() measured at ~50%
     * of the hot render thread's CPU time via a live profile). */
    static int ge_texdump_gl = -1;
    if (ge_texdump_gl < 0) ge_texdump_gl = getenv("GE_TEXDUMP") != NULL;
    if (ge_texdump_gl) {
        static int td = 0;
        static int td_fire = 0;
        /* D219: the 400-cap fills up during ordinary level load, long before
         * a player-triggered explosion. Never suppress the 16x14 RGBA fire
         * particle images (assets/oddtextures.c IMAGE_FIRE_N via
         * globalDL_0x078..0x9a8) -- that's the one shape we actually need. */
        const bool is_fire_shape = (width == 16 && height == 14);
        if ((is_fire_shape || td < 400) && width && height && width < 4096 && height < 4096) {
            char nm[128];
            if (is_fire_shape) {
                snprintf(nm, sizeof nm, "texdump/fire%03d_%ux%u_mip%d.ppm", td_fire++, width, height, (int)gen_mipmaps);
            } else {
                snprintf(nm, sizeof nm, "texdump/t%03d_%ux%u_mip%d.ppm", td++, width, height, (int)gen_mipmaps);
            }
            FILE* f = fopen(nm, "wb");
            if (f) {
                fprintf(f, "P6\n%u %u\n255\n", width, height);
                for (uint32_t p = 0; p < width * height; p++)
                    fwrite(rgba32_buf + 4 * p, 1, 3, f);
                fclose(f);
            }
        }
    }
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
	if (gen_mipmaps || current_filter_mode == FILTER_THREE_POINT) {
		glGenerateMipmap(GL_TEXTURE_2D);
	}
}

/* Normal maps are mathematical data, not display colour. Keep this upload
 * deliberately separate from any future sRGB presentation-texture path: an
 * RGBA8 internal format makes OpenGL return the encoded [0,1] normal values
 * directly to the shader, with no sRGB decode curve applied. */
static void gfx_opengl_upload_bump_texture(const uint8_t* rgba32_buf,
                                           uint32_t width, uint32_t height,
                                           bool gen_mipmaps)
{
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba32_buf);
    if (gen_mipmaps || current_filter_mode == FILTER_THREE_POINT) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
}

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    switch (val) {
        case G_TX_NOMIRROR | G_TX_CLAMP:
            return GL_CLAMP_TO_EDGE;
        case G_TX_MIRROR | G_TX_WRAP:
            return GL_MIRRORED_REPEAT;
        case G_TX_MIRROR | G_TX_CLAMP:
            return gl_mirror_clamp;
        case G_TX_NOMIRROR | G_TX_WRAP:
            return GL_REPEAT;
    }
    return 0;
}

static void gfx_opengl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps) {
    const GLint min_filters[3][3] = {
        // MIPMAP_DISABLED   MIPMAP_NEAREST               MIPMAP_LINEAR
        {  GL_NEAREST,       GL_NEAREST_MIPMAP_NEAREST,   GL_NEAREST_MIPMAP_LINEAR  }, // FILTER_NONE
        {  GL_LINEAR,        GL_LINEAR_MIPMAP_NEAREST,    GL_LINEAR_MIPMAP_LINEAR   }, // FILTER_BILINEAR
        {  GL_NEAREST,       GL_LINEAR_MIPMAP_NEAREST,    GL_LINEAR_MIPMAP_LINEAR   }, // FILTER_THREE_POINT
    };

    // B1: the shader does 3-point on magnification; for minification let the
    // hardware do trilinear mips. THREE_POINT force-generates mips at upload
    // (see gfx_opengl_upload_texture) so it can always sample them here -- this
    // is what kills the grazing-angle shimmer (Depot roof, docs/BRIEF-B2).
    if (current_filter_mode == FILTER_THREE_POINT) {
        mipmaps = true;
    }
    mipmaps = mipmaps && (current_mipmap_filter_mode != MIPMAP_DISABLED);
    const int mip_idx = mipmaps ? current_mipmap_filter_mode : 0;
    const GLint min_filter = linear_filter ? min_filters[current_filter_mode][mip_idx] : GL_NEAREST;
    const GLint max_filter = linear_filter && (current_filter_mode == FILTER_LINEAR) ? GL_LINEAR : GL_NEAREST;

    glActiveTexture(GL_TEXTURE0 + tile);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, max_filter);

    if (mipmaps) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, current_anisotropy_level);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
}

static void gfx_opengl_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode) {
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(depth_update ? GL_TRUE : GL_FALSE);
        current_depth_mask = depth_update;

        if (depth_compare) {
            switch (zmode) {
                case ZMODE_INTER:
                    glDepthFunc(GL_LEQUAL);
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(0, 0);
                    break;

                case ZMODE_OPA:
                case ZMODE_XLU:
                    if (depth_source_prim) {
                        glDepthFunc(GL_LEQUAL);
                    } else {
                        glDepthFunc(GL_LESS);
                    }
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(0, 0);
                    break;

                case ZMODE_DEC:
                    glDepthFunc(GL_LEQUAL);
                    glEnable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(-2, -2);
                    break;
            }
        } else {
            glDepthFunc(GL_ALWAYS);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(0, 0);
        }
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void gfx_opengl_set_depth_range(float znear, float zfar) {
    if (gl_es) {
        if (glDepthRangef != nullptr) {
            glDepthRangef(znear, zfar);
        }
        return;
    }
    if (glDepthRangef) {
        glDepthRangef(znear, zfar);
    } else {
        glDepthRange(znear, zfar);
    }
}

/* GLES 3 exposes draw-buffer selection through glDrawBuffers and reports the
 * active attachment as GL_DRAW_BUFFER0. The desktop entry points compile in
 * this translation unit but are invalid calls on an ES context; centralize
 * the profile split so FBO setup and state restoration use the same contract. */
static void gfx_opengl_set_draw_buffer(GLenum buffer) {
    if (gl_es && glDrawBuffers != nullptr) {
        glDrawBuffers(1, &buffer);
    } else {
        glDrawBuffer(buffer);
    }
}

static GLint gfx_opengl_get_draw_buffer(void) {
    GLint buffer = GL_NONE;
    glGetIntegerv(gl_es ? GL_DRAW_BUFFER0 : GL_DRAW_BUFFER, &buffer);
    return buffer;
}

static void gfx_opengl_set_viewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

static void gfx_opengl_set_scissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
}

static void gfx_opengl_set_use_alpha(bool use_alpha, bool modulate) {
    if (use_alpha) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (modulate) {
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

static void gfx_opengl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    // printf("flushing %d tris\n", buf_vbo_num_tris);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
    if (water_reflection_capture_active && buf_vbo_num_tris != 0) {
        ++water_reflection_draw_calls;
    }
    glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);
}

typedef void (APIENTRY *DEBUGPROC)(GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar *message,
    const void *userParam);

static void APIENTRY gl_debug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *msg, const void *p) {
    sysLogPrintf(LOG_WARNING, "GL: (%05x) %s", id, msg);
}

static void gfx_opengl_enable_debug(void) {
    if (GLAD_GL_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
    }
    if (glDebugMessageControl != NULL) {
        // enable everything except some specific spam messages
        const GLuint disable[] = {
            0x20061, /* "Framebuffer detailed info" */
            0x20071  /* "Buffer detailed info" */
        };
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
        glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 2, disable, GL_FALSE);
    }
    if (glDebugMessageCallback != NULL) {
        glDebugMessageCallback(gl_debug, NULL);
    }
}

static bool gfx_opengl_supports_framebuffers(void) {
    if (GLVersion.major > 2) {
        // GL3.0+ supports everything we need, but we'll still check it for sanity
        return (glad_glFramebufferRenderbuffer && glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    if (GLAD_GL_ARB_framebuffer_object) {
        // some implementations might be missing these
        return (glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    if (GLAD_GL_EXT_framebuffer_object && GLAD_GL_EXT_framebuffer_blit && GLAD_GL_EXT_framebuffer_multisample) {
        // sanity check
        return (glad_glFramebufferRenderbuffer && glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    // nothing
    return false;
}

static bool gfx_opengl_supports_shaders(void) {
    if (GLVersion.major > 2) {
        // should support GLSL130+
        return true;
    }

    // check supported GLSL version
    const char *ver = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (ver) {
        int maj = 0, min = 0;
        sscanf(ver, "%d.%d", &maj, &min);
        if (maj > 1 || (maj == 1 && min > 20)) {
            // above 120, should be fine
            return true;
        }
    }

    // check for extension that adds textureSize
    return GLAD_GL_EXT_gpu_shader4;
}

static void gfx_opengl_log_info(void) {
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *vendor = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *glsl_version = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    sysLogPrintf(LOG_NOTE, "GL: version: %s", version ? version : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: vendor: %s", vendor ? vendor : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: renderer: %s", renderer ? renderer : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: GLSL version: %s", glsl_version ? glsl_version : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: ARB_framebuffer_object: %s", gfx_opengl_supports_framebuffers() ? "yes" : "no");
    sysLogPrintf(LOG_NOTE, "GL: ARB_depth_clamp: %s", GLAD_GL_ARB_depth_clamp ? "yes" : "no");
    sysLogPrintf(LOG_NOTE, "GL: ARB_texture_mirror_clamp_to_edge: %s", GLAD_GL_ARB_texture_mirror_clamp_to_edge ? "yes" : "no");
}

static void *gl_load_proc(const char *name) {
    void *ret = SDL_GL_GetProcAddress(name);
    if (ret) {
        return ret;
    }

    // try with postfixes
    static const char *post[] = { "ARB", "EXT" };
    char tmp[256] = { 0 };
    for (size_t i = 0; i < sizeof(post) / sizeof(*post); ++i) {
        snprintf(tmp, sizeof(tmp), "%s%s", name, post[i]);
        ret = SDL_GL_GetProcAddress(tmp);
        if (ret) {
            return ret;
        }
    }

    sysLogPrintf(LOG_ERROR, "GL: could not find function: %s", name);

    return NULL;
}

static void gfx_opengl_init_extensions(void) {
    // patch up some extension values and pointers
    if (!GLAD_GL_ARB_depth_clamp) {
        if (GLAD_GL_EXT_depth_clamp || GLAD_GL_NV_depth_clamp) {
            GLAD_GL_ARB_depth_clamp = 1;
        } else if (!gl_es && GLVersion.major >= 3) {
            // GL3.2+ should have depth_clamp as part of the spec, but some devices don't report it for some reason
            GLAD_GL_ARB_depth_clamp = (GLVersion.major > 3 || (GLVersion.major == 3 && GLVersion.minor >= 2));
        }
    }

    if (!GLAD_GL_ARB_texture_mirror_clamp_to_edge) {
        GLAD_GL_ARB_texture_mirror_clamp_to_edge = GLAD_GL_EXT_texture_mirror_clamp_to_edge;
    }

    if (GLVersion.major < 3 && !GLAD_GL_ARB_framebuffer_object) {
        if (GLAD_GL_EXT_framebuffer_object && GLAD_GL_EXT_framebuffer_blit && GLAD_GL_EXT_framebuffer_multisample) {
            // because of the way glad works we'll have to copy the pointers over
            glad_glGenFramebuffers = glad_glGenFramebuffersEXT;
            glad_glGenRenderbuffers = glad_glGenRenderbuffersEXT;
            glad_glDeleteFramebuffers = glad_glDeleteFramebuffersEXT;
            glad_glDeleteRenderbuffers = glad_glDeleteRenderbuffersEXT;
            glad_glBindFramebuffer = glad_glBindFramebufferEXT;
            glad_glBindRenderbuffer = glad_glBindRenderbufferEXT;
            glad_glFramebufferRenderbuffer = glad_glFramebufferRenderbufferEXT;
            glad_glFramebufferTexture2D = glad_glFramebufferTexture2DEXT;
            glad_glRenderbufferStorage = glad_glRenderbufferStorageEXT;
            glad_glRenderbufferStorageMultisample = glad_glRenderbufferStorageMultisampleEXT;
            glad_glBlitFramebuffer = glad_glBlitFramebufferEXT;
        }
    }
}

static void gfx_opengl_init(void) {
    if (!gladLoadGLLoader(gl_load_proc) || glGetString == NULL || glEnable == NULL) {
        sysFatalError("Could not load OpenGL.\nReported SDL error: %s", SDL_GetError());
    }

    // check if we're using ES or core, which have more limited feature sets
    int val = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &val);
    gl_core_profile = (val == SDL_GL_CONTEXT_PROFILE_CORE);
    gl_es = (val == SDL_GL_CONTEXT_PROFILE_ES);
    const bool desktop_framebuffer_srgb_extension =
        SDL_GL_ExtensionSupported("GL_ARB_framebuffer_sRGB") == SDL_TRUE ||
        SDL_GL_ExtensionSupported("GL_EXT_framebuffer_sRGB") == SDL_TRUE;
    const bool gles_srgb_write_control_extension =
        SDL_GL_ExtensionSupported("GL_EXT_sRGB_write_control") == SDL_TRUE;
    framebuffer_srgb_control_available =
        geWaterFramebufferSrgbControlAvailable(
            gl_es ? 1 : 0,
            !gl_es && GLVersion.major >= 3 ? 1 : 0,
            !gl_es && desktop_framebuffer_srgb_extension ? 1 : 0,
            gl_es && gles_srgb_write_control_extension ? 1 : 0) != 0;

    gfx_opengl_init_extensions();

    if (sysArgCheck("--debug-gl")) {
        gfx_opengl_enable_debug();
        // dump version info as early as possible
        gfx_opengl_log_info();
    }

    if (GLVersion.major < 2 || (GLVersion.major == 2 && GLVersion.minor < 1)) {
        const char *ver = (const char *)glGetString(GL_VERSION);
        sysFatalError("Could not load OpenGL 2.1.\nReported version: %d.%d (%s)",
            GLVersion.major, GLVersion.minor, ver ? ver : "unknown");
    }

    if (!gfx_opengl_supports_shaders()) {
        sysLogPrintf(LOG_WARNING, "GL: GLSL 1.30 may be unsupported");
        // maybe replace this with sysFatalError, though the GLSL compiler will cause that later anyway
    }

    if (!gfx_framebuffers_enabled) {
        sysLogPrintf(LOG_WARNING, "GL: framebuffer effects disabled by user");
    } else if (!gfx_opengl_supports_framebuffers()) {
        sysLogPrintf(LOG_WARNING, "GL: GL_ARB_framebuffer_object unsupported, framebuffer effects disabled");
        gfx_framebuffers_enabled = false;
    }

    if ((GLVersion.major < 4 || GLVersion.minor < 4) && !GLAD_GL_ARB_texture_mirror_clamp_to_edge) {
        // GL_MIRROR_CLAMP_TO_EDGE unsupported
        gl_mirror_clamp = GL_MIRRORED_REPEAT;
    }

    // determine GLSL version
    if (gl_es) {
        // ES has its own numbering scheme, but it should support 300 even on 3.1 and 3.2
        gl_glsl_version = 300;
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d es", gl_glsl_version);
    } else if (!gl_core_profile) {
        /* Compatibility contexts on pre-3.0 hardware are the one place
         * where the renderer can legitimately expose GLSL 1.20. Keep the
         * water source free of derivative-only syntax in that profile; the
         * modern desktop and GLES paths retain the full LOD policy. */
        gl_glsl_version = (!gl_es && GLVersion.major == 2) ? 120 : 130;
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d", gl_glsl_version);
    } else {
        // otherwise we have to pick a specific version
        if (GLVersion.major == 3 && GLVersion.minor == 2) {
            // 3.2core is the earliest core version and it follows the old numbering scheme
            gl_glsl_version = 150;
        } else {
            // 3.3+ follow the new numbering scheme
            gl_glsl_version = GLVersion.major * 100 + GLVersion.minor * 10;
        }
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d core", gl_glsl_version);
    }
    sysLogPrintf(LOG_NOTE, "GL: using GLSL version %s", gl_glsl_version_str);

    glGenBuffers(1, &opengl_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);

    if (gl_core_profile || gl_es) {
        // warn user that funny things can happen
        sysLogPrintf(LOG_WARNING, "GL: using core profile or ES, watch out for errors");
        // core/es will explode if we don't use a VAO for our VBO
        glGenVertexArrays(1, &opengl_vao);
        glBindVertexArray(opengl_vao);
    }

    if (GLAD_GL_ARB_depth_clamp) {
        glEnable(GL_DEPTH_CLAMP);
    }
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    framebuffers.resize(1); // for the default screen buffer
}

static void gfx_opengl_on_resize(void) {
    gfx_opengl_release_water_resources();
    water_scene_width = 0;
    water_scene_height = 0;
    water_scene_hdr = false;
}

static void gfx_opengl_start_frame(void) {
    frame_count++;
}

static void gfx_opengl_end_frame(void) {
    glFlush();
}

/* Frame capture (dev tool, env GE_PCDUMP) — see gfx_opengl.h. TEMP D70: read
 * the default framebuffer (the window back buffer; the scene renders there
 * directly at 1:1 window size) and write a P6 PPM. */
extern "C" bool gfx_opengl_pcdump_enabled(void) {
    static int cached = -1;
    if (cached < 0) cached = getenv("GE_PCDUMP") != NULL;
    return cached != 0;
}

extern "C" bool gfx_opengl_dump_bound_fbo(uint32_t width, uint32_t height, const char* path) {
    size_t n = (size_t)width * height * 3;
    uint8_t* pixels = (uint8_t*)malloc(n);
    if (!pixels) return false;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glFinish();
    glReadPixels(0, 0, (GLsizei)width, (GLsizei)height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    {
        GLenum e = glGetError();
        if (e) sysLogPrintf(LOG_WARNING, "dump_bound_fbo: glReadPixels err 0x%x (%ux%u)", (unsigned)e, width, height);
    }
    FILE* f = fopen(path, "wb");
    if (!f) { free(pixels); return false; }
    fprintf(f, "P6\n%u %u\n255\n", width, height);
    /* D168: glReadPixels() returns rows bottom-to-top (GL framebuffer origin is
     * bottom-left); PPM P6 is top-row-first. Emit rows in reverse so the capture
     * is the right way up. Previously written unreversed -> every GE_PCDUMP was
     * vertically flipped (the source of the bogus D114/D116 "text X-mirror"). */
    {
        size_t stride = (size_t)width * 3;
        for (uint32_t row = 0; row < height; row++)
            fwrite(pixels + (size_t)(height - 1 - row) * stride, 1, stride, f);
    }
    fclose(f);
    free(pixels);
    return true;
}

static void gfx_opengl_finish_render(void) {
}

static int gfx_opengl_create_framebuffer() {
    size_t i = framebuffers.size();
    framebuffers.resize(i + 1);

    GLuint clrbuf;
    glGenTextures(1, &clrbuf);
    glBindTexture(GL_TEXTURE_2D, clrbuf);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    framebuffers[i].clrbuf = clrbuf;

    if (!gfx_framebuffers_enabled) {
        return i;
    }

    GLuint clrbuf_msaa;
    glGenRenderbuffers(1, &clrbuf_msaa);
    framebuffers[i].clrbuf_msaa = clrbuf_msaa;

    GLuint rbo;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 1, 1);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    framebuffers[i].rbo = rbo;

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    framebuffers[i].fbo = fbo;

    return i;
}

static void gfx_opengl_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                                     bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                                     bool can_extract_depth) {
    Framebuffer& fb = framebuffers[fb_id];

    width = max(width, 1U);
    height = max(height, 1U);

    if (gfx_framebuffers_enabled) {
        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

        if (fb_id != 0) {
            if (fb.width != width || fb.height != height || fb.msaa_level != msaa_level) {
                if (msaa_level <= 1) {
                    glBindTexture(GL_TEXTURE_2D, fb.clrbuf);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.clrbuf, 0);
                } else {
                    glBindRenderbuffer(GL_RENDERBUFFER, fb.clrbuf_msaa);
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_RGB8, width, height);
                    glBindRenderbuffer(GL_RENDERBUFFER, 0);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb.clrbuf_msaa);
                }
            }

            if (has_depth_buffer &&
                (fb.width != width || fb.height != height || fb.msaa_level != msaa_level || !fb.has_depth_buffer)) {
                glBindRenderbuffer(GL_RENDERBUFFER, fb.rbo);
                if (msaa_level <= 1) {
                    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
                } else {
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_DEPTH24_STENCIL8, width, height);
                }
                glBindRenderbuffer(GL_RENDERBUFFER, 0);
            }

            if (!fb.has_depth_buffer && has_depth_buffer) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.rbo);
            } else if (fb.has_depth_buffer && !has_depth_buffer) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
            }
        }
    } else {
        has_depth_buffer = false;
    }

    fb.width = width;
    fb.height = height;
    fb.has_depth_buffer = has_depth_buffer;
    fb.msaa_level = msaa_level;
    fb.invert_y = opengl_invert_y;
}

static bool gfx_opengl_ensure_postfx_depth(uint32_t width, uint32_t height)
{
    if (!gfx_framebuffers_enabled || width == 0 || height == 0 ||
        glGenFramebuffers == nullptr || glFramebufferTexture2D == nullptr ||
        glCheckFramebufferStatus == nullptr || glBlitFramebuffer == nullptr) {
        return false;
    }

    GLint old_draw_framebuffer = 0;
    GLint old_read_framebuffer = 0;
    GLint old_draw_buffer = GL_BACK;
    GLint old_read_buffer = GL_BACK;
    GLint old_active_texture = GL_TEXTURE0;
    GLint old_texture = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
    old_draw_buffer = gfx_opengl_get_draw_buffer();
    glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);

    if (postfx_depth_fbo == 0) {
        glGenFramebuffers(1, &postfx_depth_fbo);
        glGenTextures(1, &postfx_depth_texture);
        if (postfx_depth_fbo == 0 || postfx_depth_texture == 0) {
            return false;
        }
    }
    if (postfx_depth_width == width && postfx_depth_height == height) {
        return true;
    }

    glBindTexture(GL_TEXTURE_2D, postfx_depth_texture);
    /* GLES 3 requires an integer upload type for a DEPTH_COMPONENT24
     * allocation. Desktop GL accepts FLOAT here, but on llvmpipe's GLES
     * profile that combination leaves a zero-sized texture attached and the
     * depth-only FBO becomes incomplete. */
    const GLenum depth_upload_type = gl_es ? GL_UNSIGNED_INT : GL_FLOAT;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 (GLsizei)width, (GLsizei)height, 0,
                 GL_DEPTH_COMPONENT, depth_upload_type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, postfx_depth_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, postfx_depth_texture, 0);
    gfx_opengl_set_draw_buffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)old_draw_framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read_framebuffer);
    gfx_opengl_set_draw_buffer((GLenum)old_draw_buffer);
    glReadBuffer((GLenum)old_read_buffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
    glActiveTexture((GLenum)old_active_texture);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        postfx_depth_valid = false;
        sysLogPrintf(LOG_WARNING,
                     "graphics: PostFX depth framebuffer incomplete (0x%x)",
                     (unsigned)status);
        return false;
    }

    postfx_depth_width = width;
    postfx_depth_height = height;
    return true;
}

static bool gfx_opengl_capture_postfx_depth(void)
{
    if (current_framebuffer >= framebuffers.size()) {
        postfx_depth_valid = false;
        return false;
    }

    const Framebuffer &source = framebuffers[current_framebuffer];
    if (source.width == 0 || source.height == 0 ||
        (current_framebuffer != 0 && !source.has_depth_buffer) ||
        !gfx_opengl_ensure_postfx_depth(source.width, source.height)) {
        postfx_depth_valid = false;
        return false;
    }

    GLint old_draw_framebuffer = 0;
    GLint old_read_framebuffer = 0;
    GLint old_draw_buffer = GL_BACK;
    GLint old_read_buffer = GL_BACK;
    GLint old_active_texture = GL_TEXTURE0;
    GLint old_texture = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
    old_draw_buffer = gfx_opengl_get_draw_buffer();
    glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);

    const GLuint source_fbo = current_framebuffer == 0 ? 0 : source.fbo;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source_fbo);
    glReadBuffer(current_framebuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postfx_depth_fbo);
    gfx_opengl_set_draw_buffer(GL_NONE);
    glBlitFramebuffer(0, 0, (GLint)source.width, (GLint)source.height,
                      0, 0, (GLint)postfx_depth_width, (GLint)postfx_depth_height,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    const GLenum error = glGetError();

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)old_draw_framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read_framebuffer);
    gfx_opengl_set_draw_buffer((GLenum)old_draw_buffer);
    glReadBuffer((GLenum)old_read_buffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
    glActiveTexture((GLenum)old_active_texture);

    postfx_depth_valid = error == GL_NO_ERROR;
    return postfx_depth_valid;
}

static bool gfx_opengl_get_postfx_depth_texture(uintptr_t *texture,
                                                uint32_t *width,
                                                uint32_t *height)
{
    if (!postfx_depth_valid || postfx_depth_texture == 0 ||
        texture == nullptr || width == nullptr || height == nullptr) {
        return false;
    }
    *texture = (uintptr_t)postfx_depth_texture;
    *width = postfx_depth_width;
    *height = postfx_depth_height;
    return true;
}

bool gfx_opengl_start_draw_to_framebuffer(int fb_id, float noise_scale) {
    if (gfx_framebuffers_enabled && fb_id < (int)framebuffers.size()) {
        Framebuffer& fb = framebuffers[fb_id];
        if (noise_scale != 0.0f) {
            current_noise_scale = 1.0f / noise_scale;
        }
        /* fb_id 0 is the on-screen target: bind the window's default
         * framebuffer (GL name 0), not framebuffers[0].fbo (a generated but
         * attachment-less FBO -> incomplete -> nothing renders on a strict
         * GL implementation like Mesa/llvmpipe). */
        glBindFramebuffer(GL_FRAMEBUFFER, fb_id == 0 ? 0 : fb.fbo);
        current_framebuffer = fb_id;
        return true;
    } else {
        return false;
    }
}

void gfx_opengl_clear_framebuffer(bool clear_color, bool clear_depth) {
    glDisable(GL_SCISSOR_TEST);
    
    GLbitfield mask = 0;
    if (clear_color) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_depth) {
        glDepthMask(GL_TRUE);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(mask);
    if (clear_depth) {
        glDepthMask(current_depth_mask ? GL_TRUE : GL_FALSE);
    }

    glEnable(GL_SCISSOR_TEST);
}

void gfx_opengl_resolve_msaa_color_buffer(int fb_id_target, int fb_id_source) {
    if (!gfx_framebuffers_enabled) {
        return;
    }

    Framebuffer& fb_dst = framebuffers[fb_id_target];
    Framebuffer& fb_src = framebuffers[fb_id_source];
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_dst.fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_src.fbo);
    glBlitFramebuffer(0, 0, fb_src.width, fb_src.height, 0, 0, fb_dst.width, fb_dst.height, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, current_framebuffer);
    glEnable(GL_SCISSOR_TEST);
}

void* gfx_opengl_get_framebuffer_texture_id(int fb_id) {
    return (void*)(uintptr_t)framebuffers[fb_id].clrbuf;
}

void gfx_opengl_select_texture_fb(int fb_id) {
    // glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, framebuffers[fb_id].clrbuf);

    current_textures_linear_filter[0] = true;
}

void gfx_opengl_copy_framebuffer(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back) {
    if (!gfx_framebuffers_enabled || fb_dst >= (int)framebuffers.size() || fb_src >= (int)framebuffers.size()) {
        return;
    }

    const Framebuffer& src = framebuffers[fb_src];
    const Framebuffer& dst = framebuffers[fb_dst];

    int srcX0, srcY0, srcX1, srcY1;
    int dstX0, dstY0, dstX1, dstY1;

    dstX0 = dstY0 = 0;
    dstX1 = dst.width;
    dstY1 = dst.height;

    if (left >= 0 && top >= 0) {
        // unscaled rect copy
        srcX0 = left;
        srcY0 = top;
        srcX1 = left + dst.width;
        srcY1 = top + dst.height;
    } else {
        // scaled full copy
        srcX0 = 0;
        srcY0 = 0;
        srcX1 = src.width;
        srcY1 = src.height;
    }

    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, src.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo);

    if (flip_y) {
        // flip the dst rect to mirror the image vertically
        std::swap(dstY0, dstY1);
    }

    if (fb_src == 0) {
        // GLES does not support GL_FRONT here
        glReadBuffer((use_back || gl_es) ? GL_BACK : GL_FRONT);
    } else {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[current_framebuffer].fbo);

    glReadBuffer(GL_BACK);

    glEnable(GL_SCISSOR_TEST);
}

void gfx_opengl_set_texture_filter(FilteringMode mode) {
    current_filter_mode = mode;
}

FilteringMode gfx_opengl_get_texture_filter(void) {
    return current_filter_mode;
}

void gfx_opengl_set_mipmap_filter(MipmapFilteringMode mode) {
    current_mipmap_filter_mode = mode;
}

static void gfx_opengl_release_water_resources(void)
{
    if (water_composite_program != 0) {
        glDeleteProgram(water_composite_program);
        water_composite_program = 0;
    }
    if (water_composite_vbo != 0) {
        glDeleteBuffers(1, &water_composite_vbo);
        water_composite_vbo = 0;
    }
    if (water_scene_fbo != 0) {
        glDeleteFramebuffers(1, &water_scene_fbo);
        water_scene_fbo = 0;
    }
    if (water_reflection_fbo != 0) {
        glDeleteFramebuffers(1, &water_reflection_fbo);
        water_reflection_fbo = 0;
    }
    const GLuint textures[] = {
        water_scene_color, water_scene_depth, water_reflection_color,
        water_reflection_depth, water_material_base_texture,
        water_material_normal_texture, water_material_height_texture,
        water_heightfield_texture,
    };
    glDeleteTextures((GLsizei)(sizeof(textures) / sizeof(textures[0])), textures);
    water_scene_color = 0;
    water_scene_depth = 0;
    water_reflection_color = 0;
    water_reflection_depth = 0;
    water_material_base_texture = 0;
    water_material_normal_texture = 0;
    water_material_height_texture = 0;
    water_heightfield_texture = 0;
    water_heightfield_texture_unit = -1;
    water_scene_width = 0;
    water_scene_height = 0;
    water_composite_position_location = -1;
    water_composite_water_uv_location = -1;
    water_composite_view_position_location = -1;
    water_composite_world_position_location = -1;
    water_composite_surface_plane_location = -1;
    water_composite_view_plane_location = -1;
    water_composite_tangent_location = -1;
    water_composite_bitangent_location = -1;
    water_composite_surface_id_location = -1;
    water_composite_scene_location = -1;
    water_composite_depth_location = -1;
    water_composite_scene_depth_valid_location = -1;
    water_composite_line_location = -1;
    water_composite_time_location = -1;
    water_composite_tier_location = -1;
    water_composite_inverse_viewport_location = -1;
    water_composite_scene_viewport_location = -1;
    water_composite_framebuffer_viewport_location = -1;
    water_composite_base_location = -1;
    water_composite_normal_location = -1;
    water_composite_reflection_location = -1;
    water_composite_reflection_atlas_location = -1;
    water_composite_viewport_location = -1;
    water_composite_inverse_projection_location = -1;
    water_composite_world_to_clip_location = -1;
    water_composite_height_location = -1;
    water_composite_height_texel_location = -1;
    water_composite_heightfield_location = -1;
    water_composite_heightfield_scale_location = -1;
    water_composite_world_to_view_location = -1;
    water_composite_forward_projection_location = -1;
    water_composite_normal_matrix_location = -1;
    water_composite_depth_params_location = -1;
    water_composite_absorption_location = -1;
    water_composite_displacement_location = -1;
    water_composite_disturbances_location = -1;
    water_composite_disturbance_count_location = -1;
    water_composite_wave_length_location = -1;
    water_composite_wave_amplitude_location = -1;
    water_composite_wave_speed_location = -1;
    water_composite_wave_direction_location = -1;
    water_composite_contact_start_location = -1;
    water_composite_contact_band_location = -1;
    water_composite_feature_toggles_location = -1;
    water_composite_reflection_valid_location = -1;
    water_composite_light_direction_location = -1;
    water_composite_light_color_location = -1;
    water_composite_shininess_location = -1;
}

static bool gfx_opengl_compile_water_composite_program(void)
{
    if (water_composite_program != 0) {
        return true;
    }

    const char *water_shader_profile = gl_glsl_version_str;

    const char *input = gl_glsl_version >= 130 ? "in" : "attribute";
    const char *output = gl_glsl_version >= 130 ? "out" : "varying";
    const char *sample = gl_glsl_version >= 130 ? "texture" : "texture2D";
    GLint max_vertex_texture_units = 0;
    if (!gl_es) {
        glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,
                      &max_vertex_texture_units);
    }
    const int water_vertex_height = max_vertex_texture_units > 5 ? 1 : 0;
    const int water_vertex_heightfield = max_vertex_texture_units > 6 ? 1 : 0;
    char vertex_source[8192];
    char fragment_source[24576];

    const int vertex_source_length = snprintf(vertex_source, sizeof(vertex_source),
                  "#version %s\n"
                  "%s\n"
                  "#define INPUT %s\n"
                  "#define OUTPUT %s\n"
                  "#define SAMPLE_TEX %s\n"
                  "#define WATER_VERTEX_HEIGHT %d\n"
                  "#define WATER_VERTEX_HEIGHTFIELD %d\n"
                  "INPUT vec4 aPosition;\n"
                  "INPUT vec2 aWaterUv;\n"
                  "INPUT vec3 aViewPosition;\n"
                  "INPUT vec3 aWorldPosition;\n"
                  "INPUT vec4 aSurfacePlane;\n"
                  "INPUT vec4 aViewPlane;\n"
                  "INPUT vec3 aTangent;\n"
                  "INPUT vec3 aBitangent;\n"
                  "INPUT float aSurfaceId;\n"
                  "uniform float uTime;\n"
                  "uniform float uWaterDisplacement;\n"
                  "uniform float uWaveLength;\n"
                  "uniform float uWaveAmplitude;\n"
                  "uniform float uWaveSpeed;\n"
                  "uniform vec2 uWaveDirection;\n"
                  "uniform vec2 uWaterHeightTexel;\n"
                  "uniform float uWaterHeightfieldScale;\n"
                  "uniform mat4 uWaterWorldToClip;\n"
                  "uniform mat4 uWorldToView;\n"
                  "#if WATER_VERTEX_HEIGHT\n"
                  "uniform sampler2D uWaterHeight;\n"
                  "#endif\n"
                  "#if WATER_VERTEX_HEIGHTFIELD\n"
                  "uniform sampler2D uWaterHeightfield;\n"
                  "#endif\n"
                  "OUTPUT vec2 vWaterUv;\n"
                  "OUTPUT vec3 vViewPosition;\n"
                  "OUTPUT vec3 vDisplacedViewPosition;\n"
                  "OUTPUT vec3 vWorldPosition;\n"
                  "OUTPUT vec4 vSurfacePlane;\n"
                  "OUTPUT vec4 vViewPlane;\n"
                  "OUTPUT vec3 vTangent;\n"
                  "OUTPUT vec3 vBitangent;\n"
                  "OUTPUT float vSurfaceId;\n"
                  "vec2 safeNormalize2(vec2 value) {\n"
                  "    float lengthSquared = dot(value, value);\n"
                  "    return lengthSquared > 0.000001 &&\n"
                  "           all(lessThan(abs(value), vec2(1000000.0)))\n"
                  "        ? value / sqrt(lengthSquared) : vec2(1.0, 0.0);\n"
                  "}\n"
                  "float safeWaterScalar(float value, float fallback) {\n"
                  "    return value == value && abs(value) < 1000000.0\n"
                  "        ? value : fallback;\n"
                  "}\n"
                  "vec3 waterWave(vec2 uv, float time, float scale) {\n"
                  "    float safeLength = max(safeWaterScalar(uWaveLength, 0.33), 0.001);\n"
                  "    float safeAmplitude = safeWaterScalar(uWaveAmplitude, 0.0);\n"
                  "    float safeSpeed = safeWaterScalar(uWaveSpeed, 0.0);\n"
                  "    float safeTime = safeWaterScalar(time, 0.0);\n"
                  "    float safeScale = safeWaterScalar(scale, 0.0);\n"
                  "    float frequency = 6.2831853 / safeLength;\n"
                  "    vec2 direction = safeNormalize2(uWaveDirection);\n"
                  "    vec2 crossDirection = vec2(-direction.y, direction.x);\n"
                  "    float p0 = dot(uv, direction) * frequency + safeTime * safeSpeed;\n"
                  "    float p1 = dot(uv, crossDirection) * frequency * 0.73 - safeTime * safeSpeed * 0.63;\n"
                  "    float p2 = dot(uv, direction + crossDirection) * frequency * 1.41 + safeTime * safeSpeed * 0.81;\n"
                  "    float height = safeAmplitude * safeScale * (0.50 * sin(p0) + 0.30 * cos(p1) + 0.20 * sin(p2));\n"
                  "    return vec3(height, 0.0, 0.0);\n"
                  "}\n"
                  "vec3 waterHeight(vec2 uv, float time, float scale) {\n"
                  "    return waterWave(uv, time, scale);\n"
                  "}\n"
                  "vec3 waterHeightField(vec2 uv, float time, float scale) {\n"
                  "    return waterHeight(uv, time, scale * 0.55) * 0.55 +\n"
                  "           waterHeight(uv * 1.73 + vec2(0.13, -0.07), time * 0.83, scale * 0.30) * 0.30 +\n"
                  "           waterHeight(uv * 2.91 + vec2(-0.21, 0.19), time * 1.17, scale * 0.15) * 0.15;\n"
                  "}\n"
                  "#if WATER_VERTEX_HEIGHT\n"
                  "float sampleHeightBilinear(vec2 uv) {\n"
                  "    vec2 texel = max(uWaterHeightTexel, vec2(0.000001));\n"
                  "    vec2 coordinate = fract(uv) / texel - vec2(0.5);\n"
                  "    vec2 baseUv = (floor(coordinate) + vec2(0.5)) * texel;\n"
                  "    vec2 fraction = fract(coordinate);\n"
                  "    float height00 = SAMPLE_TEX(uWaterHeight, baseUv).r;\n"
                  "    float height10 = SAMPLE_TEX(uWaterHeight, baseUv + vec2(texel.x, 0.0)).r;\n"
                  "    float height01 = SAMPLE_TEX(uWaterHeight, baseUv + vec2(0.0, texel.y)).r;\n"
                  "    float height11 = SAMPLE_TEX(uWaterHeight, baseUv + texel).r;\n"
                  "    return mix(mix(height00, height10, fraction.x),\n"
                  "               mix(height01, height11, fraction.x), fraction.y);\n"
                  "}\n"
                  "#endif\n"
                  "#if WATER_VERTEX_HEIGHTFIELD\n"
                  "float sampleHeightfieldBilinear(vec2 uv) {\n"
                  "    vec2 texel = max(uWaterHeightTexel, vec2(0.000001));\n"
                  "    vec2 coordinate = fract(uv) / texel - vec2(0.5);\n"
                  "    vec2 baseUv = (floor(coordinate) + vec2(0.5)) * texel;\n"
                  "    vec2 fraction = fract(coordinate);\n"
                  "    float height00 = SAMPLE_TEX(uWaterHeightfield, baseUv).r;\n"
                  "    float height10 = SAMPLE_TEX(uWaterHeightfield, baseUv + vec2(texel.x, 0.0)).r;\n"
                  "    float height01 = SAMPLE_TEX(uWaterHeightfield, baseUv + vec2(0.0, texel.y)).r;\n"
                  "    float height11 = SAMPLE_TEX(uWaterHeightfield, baseUv + texel).r;\n"
                  "    return mix(mix(height00, height10, fraction.x),\n"
                  "               mix(height01, height11, fraction.x), fraction.y) * 2.0 - 1.0;\n"
                  "}\n"
                  "#endif\n"
                  "void main() {\n"
                  "    vec2 materialUv = fract(aWaterUv);\n"
                  "    // aWaterWorldPosition is the authored surface coordinate carried by aWorldPosition.\n"
                  "    vec3 displacedWorld = aWorldPosition;\n"
                  "    float proceduralHeight = waterHeightField(materialUv, uTime, uWaterDisplacement).x;\n"
                  "    float authoredHeight = 0.0;\n"
                  "    float dynamicHeight = 0.0;\n"
                  "#if WATER_VERTEX_HEIGHT\n"
                  "    float heightLayer0 = sampleHeightBilinear(materialUv +\n"
                  "        vec2(safeWaterScalar(uTime, 0.0) * 0.00002, 0.0));\n"
                  "    float heightLayer1 = sampleHeightBilinear(materialUv * 1.73 +\n"
                  "        vec2(0.13, -0.07) + vec2(0.0, safeWaterScalar(uTime, 0.0) * 0.00003));\n"
                  "    float heightLayer2 = sampleHeightBilinear(materialUv * 2.91 +\n"
                  "        vec2(-0.21, 0.19) + vec2(safeWaterScalar(uTime, 0.0) * 0.000017));\n"
                  "    authoredHeight = (heightLayer0 * 0.55 + heightLayer1 * 0.30 +\n"
                  "        heightLayer2 * 0.15) * 2.0 - 1.0;\n"
                  "    authoredHeight *= uWaterDisplacement * 0.35;\n"
                  "#endif\n"
                  "#if WATER_VERTEX_HEIGHTFIELD\n"
                  "    dynamicHeight = sampleHeightfieldBilinear(materialUv);\n"
                  "#endif\n"
                  "    displacedWorld += aSurfacePlane.xyz * (proceduralHeight + authoredHeight +\n"
                  "        dynamicHeight * uWaterHeightfieldScale);\n"
                  "    gl_Position = uWaterWorldToClip * vec4(displacedWorld, 1.0);\n"
                  "    /* Keep the unwrapped world/material coordinate in the\n"
                  "     * varying. The fragment shader applies fract() after\n"
                  "     * interpolation, so a large camera-following sea does\n"
                  "     * not introduce a seam or move with the camera. */\n"
                  "    vWaterUv = aWaterUv;\n"
                  "    vViewPosition = aViewPosition;\n"
                  "    vDisplacedViewPosition = (uWorldToView * vec4(displacedWorld, 1.0)).xyz;\n"
                  "    vWorldPosition = displacedWorld;\n"
                  "    vSurfacePlane = aSurfacePlane;\n"
                  "    vViewPlane = aViewPlane;\n"
                  "    vTangent = aTangent;\n"
                  "    vBitangent = aBitangent;\n"
                  "    vSurfaceId = aSurfaceId;\n"
                  "}\n",
                  gl_glsl_version_str,
                  gl_es ? "precision mediump float;" : "",
                  input, output, sample, water_vertex_height,
                  water_vertex_heightfield);

    if (vertex_source_length < 0 ||
        (size_t)vertex_source_length >= sizeof(vertex_source)) {
        sysLogPrintf(LOG_WARNING,
                     "WATER_COMPOSITE_SHADER: vertex source truncated profile=%s",
                     water_shader_profile);
        return false;
    }

    const int fragment_source_length = snprintf(fragment_source, sizeof(fragment_source),
                  "#version %s\n"
                  "%s\n"
                  "#define INPUT %s\n"
                  "#define SAMPLE_TEX %s\n"
                  "#define WATER_HAS_DERIVATIVES %d\n"
                  "%s\n"
                  "uniform sampler2D uScene;\n"
                  "uniform sampler2D uSceneDepth;\n"
                  "uniform float uSceneDepthValid;\n"
                  "uniform sampler2D uWaterBase;\n"
                  "uniform sampler2D uWaterNormal;\n"
                  "uniform sampler2D uWaterHeight;\n"
                  "uniform sampler2D uWaterHeightfield;\n"
                  "uniform vec2 uWaterHeightTexel;\n"
                  "uniform float uWaterHeightfieldScale;\n"
                  "uniform sampler2D uReflection;\n"
                  "uniform vec2 uReflectionAtlasGrid;\n"
                  "uniform float uTime;\n"
                  "uniform float uTier;\n"
                  "uniform float uWaterDisplacement;\n"
                  "uniform vec2 uInverseViewport;\n"
                  "uniform vec4 uSceneViewport;\n"
                  "uniform vec4 uViewport;\n"
                  "uniform vec4 uFramebufferViewport;\n"
                  "uniform mat4 uInverseProjection;\n"
                  "uniform mat4 uForwardProjection;\n"
                  "uniform mat3 uWaterNormalMatrix;\n"
                  "uniform vec4 uDepthParams;\n"
                  "uniform float uWaterAbsorption;\n"
                  "uniform float uWaveLength;\n"
                  "uniform float uWaveAmplitude;\n"
                  "uniform float uWaveSpeed;\n"
                  "uniform vec2 uWaveDirection;\n"
                  "uniform vec4 uWaterDisturbances[8];\n"
                  "uniform int uWaterDisturbanceCount;\n"
                  "uniform float uWaterContactStart;\n"
                  "uniform float uWaterContactBand;\n"
                  "uniform vec4 uWaterFeatureToggles;\n"
                  "uniform float uWaterReflectionValid;\n"
                  "uniform vec3 uWaterLightDirection;\n"
                  "uniform vec3 uWaterLightColor;\n"
                  "uniform float uWaterShininess;\n"
                  "INPUT vec2 vWaterUv;\n"
                  "INPUT vec3 vViewPosition;\n"
                  "INPUT vec3 vDisplacedViewPosition;\n"
                  "INPUT vec3 vWorldPosition;\n"
                  "INPUT vec4 vSurfacePlane;\n"
                  "INPUT vec4 vViewPlane;\n"
                  "INPUT vec3 vTangent;\n"
                  "INPUT vec3 vBitangent;\n"
                  "INPUT float vSurfaceId;\n"
                  "vec2 clampWaterUv(vec2 uv) { return clamp(uv, vec2(%g), vec2(%g)); }\n"
                  "vec2 viewportToFramebufferUv(vec2 localUv) {\n"
                  "    return clampWaterUv((uViewport.xy + localUv * uViewport.zw - uFramebufferViewport.xy) /\n"
                  "        max(uFramebufferViewport.zw, vec2(1.0)));\n"
                  "}\n"
                  "vec2 reflectionAtlasUv(vec2 localUv) {\n"
                  "    vec2 grid = max(uReflectionAtlasGrid, vec2(1.0));\n"
                  "    float safeSurfaceId = clamp(floor(vSurfaceId + 0.5), 0.0, 7.0);\n"
                  "    float column = mod(safeSurfaceId, grid.x);\n"
                  "    float row = floor(safeSurfaceId / grid.x);\n"
                  "    return clampWaterUv((localUv + vec2(column, row)) / grid);\n"
                  "}\n"
                  "vec2 framebufferUv() {\n"
                  "    return clampWaterUv((gl_FragCoord.xy - uFramebufferViewport.xy) / max(uFramebufferViewport.zw, vec2(1.0)));\n"
                  "}\n"
                  "vec2 viewportUv() {\n"
                  "    vec2 viewportSpan = max(uViewport.zw, vec2(1.0));\n"
                  "    vec2 sceneSpan = max(uSceneViewport.zw, vec2(1.0));\n"
                  "    vec2 viewportPosition = (gl_FragCoord.xy - uViewport.xy) / viewportSpan;\n"
                  "    vec2 scenePosition = (gl_FragCoord.xy - uSceneViewport.xy) / sceneSpan;\n"
                  "    return clampWaterUv((viewportPosition + scenePosition) * 0.5);\n"
                  "}\n"
                  "float waterLinearToDisplay(float value) {\n"
                  "    value = clamp(value, 0.0, 1.0);\n"
                  "    return value <= 0.0031308 ? value * 12.92 :\n"
                  "        1.055 * pow(value, 1.0 / 2.4) - 0.055;\n"
                  "}\n"
                  "vec3 waterLinearToDisplay(vec3 value) {\n"
                  "    return vec3(waterLinearToDisplay(value.r),\n"
                  "        waterLinearToDisplay(value.g), waterLinearToDisplay(value.b));\n"
                  "}\n"
                  "float waterDisplayToLinear(float value) {\n"
                  "    value = clamp(value, 0.0, 1.0);\n"
                  "    return value <= 0.04045 ? value / 12.92 :\n"
                  "        pow((value + 0.055) / 1.055, 2.4);\n"
                  "}\n"
                  "vec3 waterDisplayToLinear(vec3 value) {\n"
                  "    return vec3(waterDisplayToLinear(value.r),\n"
                  "        waterDisplayToLinear(value.g), waterDisplayToLinear(value.b));\n"
                  "}\n"
                  "vec3 sceneColour(vec2 uv) { return waterDisplayToLinear(\n"
                  "    SAMPLE_TEX(uScene, clampWaterUv(uv)).rgb); }\n"
                  "float sceneDepth(vec2 uv) { return uSceneDepthValid > 0.5 ? SAMPLE_TEX(uSceneDepth, clampWaterUv(uv)).r : 1.0; }\n"
                  "vec3 reconstructViewPosition(vec2 uv, float deviceDepth) {\n"
                  "    vec2 ndc = uv * 2.0 - 1.0;\n"
                  "    if (uDepthParams.w > 0.5) ndc.y = -ndc.y;\n"
                  "    float ndcZ = uDepthParams.z > 0.5 ? deviceDepth : deviceDepth * 2.0 - 1.0;\n"
                  "    vec4 viewPosition = uInverseProjection *\n"
                  "        vec4(ndc, ndcZ, 1.0);\n"
                  "    float safeW = abs(viewPosition.w) < 0.0001 ? (viewPosition.w < 0.0 ? -0.0001 : 0.0001) : viewPosition.w;\n"
                  "    return viewPosition.xyz / safeW;\n"
                  "}\n"
                  "float linearizeDepth(vec2 uv, float deviceDepth) {\n"
                  "    vec3 reconstructed = reconstructViewPosition(uv, deviceDepth);\n"
                  "    float nearPlane = max(uDepthParams.x, 0.0001);\n"
                  "    float farPlane = max(uDepthParams.y, nearPlane + 0.001);\n"
                  "    float ndcDepth = uDepthParams.z > 0.5 ? deviceDepth : deviceDepth * 2.0 - 1.0;\n"
                  "    float fallbackDepth = (2.0 * nearPlane * farPlane) /\n"
                  "        max(farPlane + nearPlane - ndcDepth * (farPlane - nearPlane), 0.0001);\n"
                  "    return abs(reconstructed.z) > 0.0001 ? max(-reconstructed.z, 0.0001) : fallbackDepth;\n"
                  "}\n"
                  "vec2 safeNormalize2(vec2 value) {\n"
                  "    float lengthSquared = dot(value, value);\n"
                  "    return lengthSquared > 0.000001 &&\n"
                  "           all(lessThan(abs(value), vec2(1000000.0)))\n"
                  "        ? value / sqrt(lengthSquared) : vec2(1.0, 0.0);\n"
                  "}\n"
                  "float safeWaterScalar(float value, float fallback) {\n"
                  "    return value == value && abs(value) < 1000000.0\n"
                  "        ? value : fallback;\n"
                  "}\n"
                  "vec3 waterWave(vec2 uv, float time, float scale) {\n"
                  "    float safeLength = max(safeWaterScalar(uWaveLength, 0.33), 0.001);\n"
                  "    float safeAmplitude = safeWaterScalar(uWaveAmplitude, 0.0);\n"
                  "    float safeSpeed = safeWaterScalar(uWaveSpeed, 0.0);\n"
                  "    float safeTime = safeWaterScalar(time, 0.0);\n"
                  "    float safeScale = safeWaterScalar(scale, 0.0);\n"
                  "    float frequency = 6.2831853 / safeLength;\n"
                  "    vec2 direction = safeNormalize2(uWaveDirection);\n"
                  "    vec2 crossDirection = vec2(-direction.y, direction.x);\n"
                  "    float p0 = dot(uv, direction) * frequency + safeTime * safeSpeed;\n"
                  "    float p1 = dot(uv, crossDirection) * frequency * 0.73 - safeTime * safeSpeed * 0.63;\n"
                  "    float p2 = dot(uv, direction + crossDirection) * frequency * 1.41 + safeTime * safeSpeed * 0.81;\n"
                  "    float height = safeAmplitude * safeScale * (0.50 * sin(p0) + 0.30 * cos(p1) + 0.20 * sin(p2));\n"
                  "    float height_dx = safeAmplitude * safeScale * frequency * (0.50 * direction.x * cos(p0) - 0.30 * crossDirection.x * sin(p1) + 0.20 * (direction.x + crossDirection.x) * cos(p2) * 1.41);\n"
                  "    float height_dy = safeAmplitude * safeScale * frequency * (0.50 * direction.y * cos(p0) - 0.30 * crossDirection.y * sin(p1) + 0.20 * (direction.y + crossDirection.y) * cos(p2) * 1.41);\n"
                  "    return vec3(height, height_dx, height_dy);\n"
                  "}\n"
                  "vec3 waterRipple(vec2 uv, float time, vec2 center, float radius, float strength) {\n"
                  "    vec2 delta = uv - center;\n"
                  "    float distanceToCenter = length(delta);\n"
                  "    float radiusSquared = max(radius * radius, 0.0001);\n"
                  "    float envelope = exp(-distanceToCenter * distanceToCenter / radiusSquared);\n"
                  "    float phase = distanceToCenter * 85.0 - time * 0.004;\n"
                  "    float height = strength * envelope * sin(phase);\n"
                  "    float envelopeDerivative = -2.0 * distanceToCenter * envelope / max(radius * radius, 0.0001);\n"
                  "    vec2 radial = delta / max(distanceToCenter, 0.01);\n"
                  "    vec2 gradient = strength * radial * (envelope * cos(phase) * 85.0 +\n"
                  "                         envelopeDerivative * sin(phase));\n"
                  "    return vec3(height, gradient);\n"
                  "}\n"
                  "float sampleHeightBilinear(vec2 uv) {\n"
                  "    vec2 texel = max(uWaterHeightTexel, vec2(0.000001));\n"
                  "    vec2 coordinate = fract(uv) / texel - vec2(0.5);\n"
                  "    vec2 baseUv = (floor(coordinate) + vec2(0.5)) * texel;\n"
                  "    vec2 fraction = fract(coordinate);\n"
                  "    float height00 = SAMPLE_TEX(uWaterHeight, baseUv).r;\n"
                  "    float height10 = SAMPLE_TEX(uWaterHeight, baseUv + vec2(texel.x, 0.0)).r;\n"
                  "    float height01 = SAMPLE_TEX(uWaterHeight, baseUv + vec2(0.0, texel.y)).r;\n"
                  "    float height11 = SAMPLE_TEX(uWaterHeight, baseUv + texel).r;\n"
                  "    return mix(mix(height00, height10, fraction.x),\n"
                  "               mix(height01, height11, fraction.x), fraction.y);\n"
                  "}\n"
                  "float sampleHeightfieldBilinear(vec2 uv) {\n"
                  "    vec2 texel = max(uWaterHeightTexel, vec2(0.000001));\n"
                  "    vec2 coordinate = fract(uv) / texel - vec2(0.5);\n"
                  "    vec2 baseUv = (floor(coordinate) + vec2(0.5)) * texel;\n"
                  "    vec2 fraction = fract(coordinate);\n"
                  "    float height00 = SAMPLE_TEX(uWaterHeightfield, baseUv).r;\n"
                  "    float height10 = SAMPLE_TEX(uWaterHeightfield, baseUv + vec2(texel.x, 0.0)).r;\n"
                  "    float height01 = SAMPLE_TEX(uWaterHeightfield, baseUv + vec2(0.0, texel.y)).r;\n"
                  "    float height11 = SAMPLE_TEX(uWaterHeightfield, baseUv + texel).r;\n"
                  "    return mix(mix(height00, height10, fraction.x),\n"
                  "               mix(height01, height11, fraction.x), fraction.y) * 2.0 - 1.0;\n"
                  "}\n"
                  "vec2 sampleHeightfieldNormalBilinear(vec2 uv) {\n"
                  "    vec2 texel = max(uWaterHeightTexel, vec2(0.000001));\n"
                  "    vec2 coordinate = fract(uv) / texel - vec2(0.5);\n"
                  "    vec2 baseUv = (floor(coordinate) + vec2(0.5)) * texel;\n"
                  "    vec2 fraction = fract(coordinate);\n"
                  "    vec2 normal00 = SAMPLE_TEX(uWaterHeightfield, baseUv).gb;\n"
                  "    vec2 normal10 = SAMPLE_TEX(uWaterHeightfield, baseUv + vec2(texel.x, 0.0)).gb;\n"
                  "    vec2 normal01 = SAMPLE_TEX(uWaterHeightfield, baseUv + vec2(0.0, texel.y)).gb;\n"
                  "    vec2 normal11 = SAMPLE_TEX(uWaterHeightfield, baseUv + texel).gb;\n"
                  "    return (mix(mix(normal00, normal10, fraction.x),\n"
                  "                mix(normal01, normal11, fraction.x), fraction.y) * 2.0 - 1.0);\n"
                  "}\n"
                  "float authoredHeightSample(vec2 uv) {\n"
                  "    float heightLayer0 = sampleHeightBilinear(uv);\n"
                  "    float heightLayer1 = sampleHeightBilinear(uv * 1.73 + vec2(0.13, -0.07));\n"
                  "    float heightLayer2 = sampleHeightBilinear(uv * 2.91 + vec2(-0.21, 0.19));\n"
                  "    float heightLayer = heightLayer0 * 0.55 + heightLayer1 * 0.30 +\n"
                  "        heightLayer2 * 0.15;\n"
                  "    return heightLayer * 2.0 - 1.0;\n"
                  "}\n"
                  "vec3 safeNormalize(vec3 value) {\n"
                  "    float lengthSquared = dot(value, value);\n"
                  "    return lengthSquared > 0.000001 && all(lessThan(abs(value), vec3(1000000.0)))\n"
                  "        ? value / sqrt(lengthSquared) : vec3(0.0, 0.0, 1.0);\n"
                  "}\n"
                  "vec3 safeReflect(vec3 incident, vec3 normal, out float valid) {\n"
                  "    vec3 reflected = reflect(incident, normal);\n"
                  "    valid = dot(reflected, reflected) > 0.000001 ? 1.0 : 0.0;\n"
                  "    return safeNormalize(reflected);\n"
                  "}\n"
                  "vec3 safeRefract(vec3 incident, vec3 normal, out float valid) {\n"
                  "    float cosine = dot(-incident, normal);\n"
                  "    float airToWater = 1.0 / 1.333;\n"
                  "    float waterToAir = 1.333;\n"
                  "    float eta = cosine > 0.0 ? airToWater : waterToAir;\n"
                  "    vec3 adjustedNormal = cosine > 0.0 ? normal : -normal;\n"
                  "    cosine = abs(cosine);\n"
                  "    float k = 1.0 - eta * eta * (1.0 - cosine * cosine);\n"
                  "    float totalInternalReflection = step(k, 0.0);\n"
                  "    valid = 1.0 - totalInternalReflection;\n"
                  "    return valid > 0.5 ? safeNormalize(eta * incident + (eta * cosine - sqrt(max(k, 0.0))) * adjustedNormal) : vec3(0.0, 0.0, 1.0);\n"
                  "}\n"
                  "vec2 projectSurfaceToScreen(vec3 viewPosition, vec3 ray, out float valid) {\n"
                  "    vec4 projected = uForwardProjection * vec4(viewPosition + ray, 1.0);\n"
                  "    float safeW = abs(projected.w) < 0.0001 ? (projected.w < 0.0 ? -0.0001 : 0.0001) : projected.w;\n"
                  "    valid = projected.w > 0.0001 ? 1.0 : 0.0;\n"
                  "    return projected.xy / safeW * 0.5 + 0.5;\n"
                  "}\n"
                  "vec2 safeProjectRay(vec3 ray, out float valid) {\n"
                  "    valid = ray.z < -0.0001 ? 1.0 : 0.0;\n"
                  "    return ray.xy / max(abs(ray.z), 0.0001);\n"
                  "}\n"
                  "vec2 projectRayToScreen(vec3 ray) {\n"
                  "    float projectRayValid = 0.0;\n"
                  "    return safeProjectRay(ray, projectRayValid);\n"
                  "}\n"
                  "vec3 waterEnvironmentReflection(vec3 ray) {\n"
                  "    float horizon = clamp(ray.y * 0.5 + 0.5, 0.0, 1.0);\n"
                  "    vec3 horizonColour = mix(vec3(0.008, 0.025, 0.055),\n"
                  "        vec3(0.22, 0.38, 0.58), horizon);\n"
                  "    vec3 sunDirection = safeNormalize(vec3(-0.35, 0.78, 0.42));\n"
                  "    float sun = pow(max(dot(ray, sunDirection), 0.0), 64.0);\n"
                  "    return horizonColour + vec3(1.0, 0.86, 0.62) * sun * 0.18;\n"
                  "}\n"
                  "vec3 screenSpaceReflection(vec3 origin, vec3 ray, out float hit) {\n"
                  "    hit = 0.0;\n"
                  "    vec3 fallback = vec3(0.0);\n"
                  "    for (int stepIndex = 1; stepIndex <= 24; ++stepIndex) {\n"
                  "        float distance = float(stepIndex) * 0.18;\n"
                  "        vec3 samplePosition = origin + ray * distance;\n"
                  "        float projectedValid = 0.0;\n"
                  "        vec2 sampleUv = viewportToFramebufferUv(\n"
                  "            projectSurfaceToScreen(origin, ray * distance, projectedValid));\n"
                  "        if (projectedValid < 0.5) break;\n"
                  "        float sceneLinearDepth = linearizeDepth(sampleUv, sceneDepth(sampleUv));\n"
                  "        float rayLinearDepth = max(-samplePosition.z, 0.0001);\n"
                  "        fallback = sceneColour(sampleUv);\n"
                  "        if (sceneLinearDepth <= rayLinearDepth + 0.08) {\n"
                  "            hit = 1.0;\n"
                  "            return fallback;\n"
                  "        }\n"
                  "    }\n"
                  "    return fallback;\n"
                  "}\n"
                  "vec3 waterVisibilityFloor(float thickness) {\n"
                  "    float depthFactor = clamp(safeWaterScalar(thickness, 0.0) * %g, 0.0, 1.0);\n"
                  "    return mix(vec3(%g, %g, %g), vec3(%g, %g, %g), depthFactor);\n"
                  "}\n"
                  "void main() {\n"
                  "    float tierScale = uTier > 1.5 ? 1.18 : 1.0;\n"
                  "    float waveScale = uTier > 1.5 ? 0.008 : 0.004;\n"
                  "    float bumpStrength = uTier > 1.5 ? 1.35 : 0.9;\n"
                  "    vec2 localViewportUv = viewportUv();\n"
                  "    vec2 screenUv = framebufferUv();\n"
                  "    vec2 waveUv = fract(vWaterUv);\n"
                  "    float materialTime = safeWaterScalar(uTime, 0.0);\n"
                  "    vec3 wave = waterWave(waveUv, materialTime, waveScale);\n"
                  "    vec3 ripple = vec3(0.0);\n"
                  "    /* The fixed CPU heightfield is authoritative whenever\n"
                  "     * it reached the GPU. Keep the old analytic ripple only\n"
                  "     * for texture-unit constrained fallback hardware;\n"
                  "     * otherwise a wake would be applied twice. */\n"
                  "    float proceduralRippleWeight = 1.0 -\n"
                  "        step(0.000001, abs(uWaterHeightfieldScale));\n"
                  "    for (int disturbanceIndex = 0; disturbanceIndex < 8; ++disturbanceIndex) {\n"
                  "        if (disturbanceIndex < uWaterDisturbanceCount) {\n"
                  "            vec4 disturbance = uWaterDisturbances[disturbanceIndex];\n"
                  "            ripple += waterRipple(waveUv, materialTime, disturbance.xy,\n"
                  "                max(disturbance.z, 0.001), disturbance.w) *\n"
                  "                proceduralRippleWeight;\n"
                  "        }\n"
                  "    }\n"
                  "    wave += ripple;\n"
                  "    vec2 heightStep = vec2(1.0 / 64.0);\n"
                  "    float authoredHeight = authoredHeightSample(waveUv);\n"
                  "    float authoredHeightX = authoredHeightSample(waveUv + vec2(heightStep.x, 0.0));\n"
                  "    float authoredHeightY = authoredHeightSample(waveUv + vec2(0.0, heightStep.y));\n"
                  "    float authoredScale = uWaterDisplacement * 0.35;\n"
                  "    wave.x += authoredHeight * authoredScale;\n"
                  "    wave.y += (authoredHeightX - authoredHeight) * authoredScale / heightStep.x;\n"
                  "    wave.z += (authoredHeightY - authoredHeight) * authoredScale / heightStep.y;\n"
                  "    float dynamicHeight = sampleHeightfieldBilinear(waveUv);\n"
                  "    float dynamicHeightX = sampleHeightfieldBilinear(waveUv + vec2(heightStep.x, 0.0));\n"
                  "    float dynamicHeightY = sampleHeightfieldBilinear(waveUv + vec2(0.0, heightStep.y));\n"
                  "    wave.x += dynamicHeight * uWaterHeightfieldScale;\n"
                  "    wave.y += (dynamicHeightX - dynamicHeight) * uWaterHeightfieldScale / heightStep.x;\n"
                  "    wave.z += (dynamicHeightY - dynamicHeight) * uWaterHeightfieldScale / heightStep.y;\n"
                  "    vec2 dynamicNormalXZ = sampleHeightfieldNormalBilinear(waveUv);\n"
                  "    float dynamicNormalY = sqrt(max(1.0 - dot(dynamicNormalXZ, dynamicNormalXZ), 0.0625));\n"
                  "    vec3 dynamicNormal = safeNormalize(vec3(dynamicNormalXZ.x, dynamicNormalY, dynamicNormalXZ.y));\n"
                  "    float dynamicNormalValid = step(0.000001, abs(uWaterHeightfieldScale));\n"
                  "    dynamicNormal = mix(vec3(0.0, 1.0, 0.0), dynamicNormal, dynamicNormalValid);\n"
                  "#if WATER_HAS_DERIVATIVES\n"
                  "    vec2 waveDerivativeX = vec2(dFdx(waveUv.x), dFdx(waveUv.y));\n"
                  "    vec2 waveDerivativeY = vec2(dFdy(waveUv.x), dFdy(waveUv.y));\n"
                  "    float waveRho = max(length(waveDerivativeX), length(waveDerivativeY)) * 2048.0;\n"
                  "    float waterLod = clamp(log2(max(waveRho, 1.0)), 0.0, 5.0);\n"
                  "#else\n"
                  "    float waterLod = 0.0;\n"
                  "#endif\n"
                  "    float detailLodScale = 1.0 / (1.0 + waterLod * 0.08);\n"
                  "    vec2 viewportPhase = localViewportUv * 0.0001;\n"
                  "    vec2 detailUv0 = fract(waveUv * (4.0 * detailLodScale) +\n"
                  "                          vec2(materialTime * 0.00003, -materialTime * 0.00002) + viewportPhase);\n"
                  "    vec2 detailUv1 = fract(waveUv * (7.0 * detailLodScale) +\n"
                  "                          vec2(-materialTime * 0.000021, materialTime * 0.000037) +\n"
                  "                          vec2(0.37, 0.11));\n"
                  "    vec2 detailUv2 = fract(waveUv * (11.0 * detailLodScale) +\n"
                  "                          vec2(materialTime * 0.000017, materialTime * 0.000013) +\n"
                  "                          vec2(0.19, 0.63));\n"
                  "    vec2 detailUv3 = fract(waveUv * (17.0 * detailLodScale) +\n"
                  "                          vec2(-materialTime * 0.000011, materialTime * 0.000019) +\n"
                  "                          vec2(0.71, 0.29));\n"
                  "    vec3 normalLayer0 = SAMPLE_TEX(uWaterNormal, detailUv0).xyz * 2.0 - 1.0;\n"
                  "    vec3 normalLayer1 = SAMPLE_TEX(uWaterNormal, detailUv1).xyz * 2.0 - 1.0;\n"
                  "    vec3 normalLayer2 = SAMPLE_TEX(uWaterNormal, detailUv2).xyz * 2.0 - 1.0;\n"
                  "    vec3 normalLayer3 = SAMPLE_TEX(uWaterNormal, detailUv3).xyz * 2.0 - 1.0;\n"
                  "    vec3 sampledNormal = safeNormalize(normalLayer0 * 0.55 +\n"
                  "        normalLayer1 * 0.25 + normalLayer2 * 0.13 + normalLayer3 * 0.07);\n"
                  "    if (uWaterFeatureToggles.z < 0.5) {\n"
                  "        sampledNormal = vec3(0.0, 0.0, 1.0);\n"
                  "        dynamicNormal = vec3(0.0, 1.0, 0.0);\n"
                  "    }\n"
                  "    vec3 combinedNormal = safeNormalize(sampledNormal * 0.72 +\n"
                  "        dynamicNormal * 0.28);\n"
                  "    vec3 waterTangentNormal = normalize(vec3(\n"
                  "        -wave.y * bumpStrength + combinedNormal.x * 0.18,\n"
                  "        -wave.z * bumpStrength + combinedNormal.z * 0.18,\n"
                  "        max(combinedNormal.y, 0.25)));\n"
                  "    vec3 waterNormalWorld = safeNormalize(vTangent * waterTangentNormal.x +\n"
                  "        vBitangent * waterTangentNormal.y + vSurfacePlane.xyz * waterTangentNormal.z);\n"
                  "    vec3 waterNormalView = safeNormalize(uWaterNormalMatrix * waterNormalWorld);\n"
                  "    vec3 viewPosition = vDisplacedViewPosition;\n"
                  "    if (length(viewPosition) < 0.0001) {\n"
                  "        viewPosition = reconstructViewPosition(screenUv, gl_FragCoord.z);\n"
                  "    }\n"
                  "    vec3 viewRay = safeNormalize(viewPosition);\n"
                  "    vec3 waterLightDirection = safeNormalize(uWaterLightDirection);\n"
                  "    vec3 waterViewDirection = safeNormalize(-viewPosition);\n"
                  "    vec3 waterHalfway = safeNormalize(waterLightDirection + waterViewDirection);\n"
                  "    float directDiffuse = max(dot(waterNormalView, waterLightDirection), 0.0);\n"
                  "    float directSpecular = directDiffuse > 0.0\n"
                  "        ? pow(max(dot(waterNormalView, waterHalfway), 0.0),\n"
                  "            clamp(uWaterShininess, 1.0, 128.0))\n"
                  "        : 0.0;\n"
                  "    vec3 directHighlight = uWaterLightColor * directSpecular *\n"
                  "        (0.20 + 0.10 * tierScale);\n"
                  "    float reflectValid = 0.0;\n"
                  "    float refractValid = 0.0;\n"
                  "    vec3 reflectedRay = safeReflect(viewRay, waterNormalView, reflectValid);\n"
                  "    vec3 refractedRay = safeRefract(viewRay, waterNormalView, refractValid);\n"
                  "    float incidentProjectionValid = 1.0;\n"
                  "    float reflectedProjectionValid = 0.0;\n"
                  "    float refractedProjectionValid = 0.0;\n"
                  "    vec2 incidentScreenRay = localViewportUv;\n"
                  "    vec2 reflectedScreenRay = projectSurfaceToScreen(viewPosition, reflectedRay, reflectedProjectionValid);\n"
                  "    vec2 refractedScreenRay = projectSurfaceToScreen(viewPosition, refractedRay, refractedProjectionValid);\n"
                  "    reflectValid *= incidentProjectionValid * reflectedProjectionValid;\n"
                  "    refractValid *= incidentProjectionValid * refractedProjectionValid;\n"
                  "    vec2 reflectionOffset = (reflectedScreenRay - incidentScreenRay) *\n"
                  "        (0.035 * tierScale) * reflectValid + wave.yz * 0.02 * reflectValid;\n"
                  "    vec2 refractionOffset = (refractedScreenRay - incidentScreenRay) *\n"
                  "        (0.022 * tierScale) * refractValid + wave.yz * 0.035 * refractValid;\n"
                  "    vec2 refractLocalUv = clampWaterUv(localViewportUv + refractionOffset);\n"
                  "    vec2 reflectLocalUv = clampWaterUv(localViewportUv + reflectionOffset);\n"
                  "    vec2 refractUv = viewportToFramebufferUv(refractLocalUv);\n"
                  "    vec2 reflectUv = reflectionAtlasUv(reflectLocalUv);\n"
                  "    float surfaceDepth = -vDisplacedViewPosition.z;\n"
                  "    if (!(surfaceDepth > 0.0001)) {\n"
                  "        surfaceDepth = linearizeDepth(screenUv, gl_FragCoord.z);\n"
                  "    }\n"
                  "    vec3 refraction = sceneColour(refractUv);\n"
                  "    float reflectionHit = 0.0;\n"
                  "    vec3 screenReflection = screenSpaceReflection(viewPosition, reflectedRay, reflectionHit);\n"
                  "    vec3 reflectionAtlas = waterDisplayToLinear(\n"
                  "        SAMPLE_TEX(uReflection, reflectUv).rgb);\n"
                  "    vec3 environmentReflection = waterEnvironmentReflection(reflectedRay);\n"
                  "    float atlasEnergy = dot(reflectionAtlas, vec3(0.2126, 0.7152, 0.0722));\n"
                  "    vec3 reflectionWithEnvironment = mix(environmentReflection, reflectionAtlas,\n"
                  "        smoothstep(0.01, 0.06, atlasEnergy));\n"
                  "    float screenEnergy = dot(screenReflection, vec3(0.2126, 0.7152, 0.0722));\n"
                  "    vec3 screenReflectionWithEnvironment = mix(environmentReflection, screenReflection,\n"
                  "        smoothstep(0.01, 0.06, screenEnergy));\n"
                  "    vec3 reflection = mix(reflectionWithEnvironment,\n"
                  "        screenReflectionWithEnvironment, reflectionHit);\n"
                  "    float reflectionDiagnostic = step(1.5, uWaterFeatureToggles.x);\n"
                  "    vec4 surfacePlane = vViewPlane;\n"
                  "    float clipPlaneValid = step(0.000001, dot(surfacePlane.xyz, surfacePlane.xyz));\n"
                  "    float reflectionClip = step(0.0001, surfaceDepth) * clipPlaneValid;\n"
                  "    reflection *= reflectionClip * uWaterReflectionValid *\n"
                  "        uWaterFeatureToggles.x;\n"
                  "    float facing = clamp(dot(-viewRay, waterNormalView), 0.0, 1.0);\n"
                  "    float fresnel = 0.02 + 0.98 * pow(1.0 - facing, 5.0);\n"
                  "    float reflectionWeight = clamp(fresnel * tierScale * reflectValid *\n"
                  "        uWaterReflectionValid * uWaterFeatureToggles.x, 0.0, 1.0);\n"
                  "    float centerDeviceDepth = sceneDepth(screenUv);\n"
                  "    float centerDepth = linearizeDepth(screenUv, centerDeviceDepth);\n"
                  "    float depthRight = linearizeDepth(screenUv + vec2(uInverseViewport.x, 0.0),\n"
                  "        sceneDepth(screenUv + vec2(uInverseViewport.x, 0.0)));\n"
                  "    float depthLeft = linearizeDepth(screenUv - vec2(uInverseViewport.x, 0.0),\n"
                  "        sceneDepth(screenUv - vec2(uInverseViewport.x, 0.0)));\n"
                  "    float depthDown = linearizeDepth(screenUv + vec2(0.0, uInverseViewport.y),\n"
                  "        sceneDepth(screenUv + vec2(0.0, uInverseViewport.y)));\n"
                  "    float depthUp = linearizeDepth(screenUv - vec2(0.0, uInverseViewport.y),\n"
                  "        sceneDepth(screenUv - vec2(0.0, uInverseViewport.y)));\n"
                  "    float neighbourDepth = max(max(abs(centerDepth - depthRight),\n"
                  "                                 abs(centerDepth - depthLeft)),\n"
                  "                             max(abs(centerDepth - depthDown),\n"
                  "                                 abs(centerDepth - depthUp)));\n"
                  "    float nearestNeighbourDepth = min(min(depthRight, depthLeft),\n"
                  "        min(depthDown, depthUp));\n"
                  "    float depthAvailable = step(0.5, uSceneDepthValid);\n"
                  "    /* A missing depth attachment is not a far-plane ocean.\n"
                  "     * Use a bounded shallow fallback so transmission remains\n"
                  "     * visible on RGB-only/GLES targets and never discards the\n"
                  "     * entire water surface as an invalid occluder. */\n"
                  "    float waterThickness = mix(1.0, max(centerDepth - surfaceDepth, 0.0),\n"
                  "        depthAvailable);\n"
                  "    float occluded = depthAvailable * step(centerDepth + 0.01, surfaceDepth);\n"
                  "    float transmission = exp(-uWaterAbsorption * waterThickness);\n"
                  "    float refractedDepth = linearizeDepth(refractUv, sceneDepth(refractUv));\n"
                  "    float refractionMask = mix(1.0,\n"
                  "        step(surfaceDepth - 0.02, refractedDepth), depthAvailable);\n"
                  "    float transmissionWeight = clamp((1.0 - reflectionWeight) * transmission *\n"
                  "        refractValid * refractionMask * uWaterFeatureToggles.y, 0.0, 1.0);\n"
                  "    float weightSum = min(reflectionWeight + transmissionWeight, 1.0);\n"
                  "    vec3 baseMaterial = SAMPLE_TEX(uWaterBase, fract(waveUv * 4.0)).rgb;\n"
                  "    vec3 transmitted = refraction * transmissionWeight;\n"
                  "    vec3 deepWater = mix(vec3(0.008, 0.055, 0.085),\n"
                  "                         vec3(0.015, 0.20, 0.28), clamp(waterThickness * 0.04, 0.0, 1.0));\n"
                  "    vec3 specular = reflection * fresnel * (0.35 + 0.65 * baseMaterial);\n"
                  "    vec3 water = deepWater * (1.0 - weightSum) + transmitted +\n"
                  "        reflection * reflectionWeight;\n"
                  "    water *= 0.68 + baseMaterial * 0.72;\n"
                  "    water += specular * reflectionWeight;\n"
                  "    water = max(water, waterVisibilityFloor(waterThickness));\n"
                  "    water *= 0.98 + 0.02 * fract(vSurfaceId * 0.6180339);\n"
                  "    water += directHighlight;\n"
                  "    float contactDistance = max(nearestNeighbourDepth - surfaceDepth, 0.0);\n"
                  "    float proximity = 1.0 - smoothstep(uWaterContactStart, uWaterContactBand, contactDistance);\n"
                  "    float edge = smoothstep(%g, %g, neighbourDepth);\n"
                  "    /* The foreground boat/dam fragment is intentionally discarded.\n"
                  "     * Contact foam therefore belongs to the neighbouring visible\n"
                  "     * water pixels; using proximity from the occluded fragment\n"
                  "     * made the entire shoreline foam disappear. */\n"
                  "    float foam = clamp(edge * proximity * depthAvailable * (1.0 - occluded) * tierScale *\n"
                  "        uWaterFeatureToggles.w, 0.0, 1.0);\n"
                  "    water = mix(water, vec3(0.82, 0.93, 0.98), foam);\n"
                  "    float waterOpacity = clamp(0.28 +\n"
                  "        0.30 * (1.0 - transmission) + 0.22 * reflectionWeight +\n"
                  "        0.20 * foam + 0.04 * proximity, 0.25, 0.86);\n"
                  "    if (reflectionDiagnostic > 0.5) {\n"
                  "        water = reflectionAtlas;\n"
                  "        waterOpacity = 1.0;\n"
                  "    }\n"
                  "    if (occluded > 0.5) discard;\n"
                  "    %s = vec4(waterLinearToDisplay(water), waterOpacity);\n"
                  "}\n",
                  gl_glsl_version_str,
                  gl_es ? "precision mediump float;" : "",
                  input,
                  sample,
                  (gl_es || gl_glsl_version >= 130) ? 1 : 0,
                  gl_glsl_version >= 130 ? "out vec4 outColor;" : "#define outColor gl_FragColor",
                  (double)GE_WATER_UV_MARGIN,
                  (double)(1.0f - GE_WATER_UV_MARGIN),
                  (double)GE_WATER_AMBIENT_DEPTH_SCALE,
                  (double)GE_WATER_AMBIENT_SHALLOW_R,
                  (double)GE_WATER_AMBIENT_SHALLOW_G,
                  (double)GE_WATER_AMBIENT_SHALLOW_B,
                  (double)GE_WATER_AMBIENT_DEEP_R,
                  (double)GE_WATER_AMBIENT_DEEP_G,
                  (double)GE_WATER_AMBIENT_DEEP_B,
                  (double)GE_WATER_EDGE_START,
                  (double)GE_WATER_EDGE_END,
                  "outColor");

    if (fragment_source_length < 0 ||
        (size_t)fragment_source_length >= sizeof(fragment_source)) {
        sysLogPrintf(LOG_WARNING,
                     "WATER_COMPOSITE_SHADER: fragment source truncated profile=%s",
                     water_shader_profile);
        return false;
    }

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    if (vertex_shader == 0 || fragment_shader == 0) {
        if (vertex_shader != 0) glDeleteShader(vertex_shader);
        if (fragment_shader != 0) glDeleteShader(fragment_shader);
        sysLogPrintf(LOG_WARNING, "WATER_COMPOSITE_SHADER: shader allocation failed");
        return false;
    }
    const char *vertex_source_ptr = vertex_source;
    const char *fragment_source_ptr = fragment_source;
    glShaderSource(vertex_shader, 1, &vertex_source_ptr, nullptr);
    glShaderSource(fragment_shader, 1, &fragment_source_ptr, nullptr);
    glCompileShader(vertex_shader);
    glCompileShader(fragment_shader);

    GLint vertex_ok = GL_FALSE;
    GLint fragment_ok = GL_FALSE;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &vertex_ok);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &fragment_ok);
    if (vertex_ok != GL_TRUE || fragment_ok != GL_TRUE) {
        char log[2048] = {};
        GLuint failed_shader = vertex_ok == GL_TRUE ? fragment_shader : vertex_shader;
        glGetShaderInfoLog(failed_shader, sizeof(log) - 1, nullptr, log);
        sysLogPrintf(LOG_WARNING,
                     "WATER_COMPOSITE_SHADER: compile failed profile=%s: %s",
                     water_shader_profile, log);
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        sysLogPrintf(LOG_WARNING, "WATER_COMPOSITE_SHADER: program allocation failed");
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aWaterUv");
    glBindAttribLocation(program, 2, "aViewPosition");
    glBindAttribLocation(program, 3, "aWorldPosition");
    glBindAttribLocation(program, 4, "aSurfacePlane");
    glBindAttribLocation(program, 5, "aViewPlane");
    glBindAttribLocation(program, 6, "aTangent");
    glBindAttribLocation(program, 7, "aBitangent");
    glBindAttribLocation(program, 8, "aSurfaceId");
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char log[2048] = {};
        glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        sysLogPrintf(LOG_WARNING,
                     "WATER_COMPOSITE_SHADER: link failed profile=%s: %s",
                     water_shader_profile, log);
        glDeleteProgram(program);
        return false;
    }

    water_composite_program = program;
    water_composite_position_location = 0;
    water_composite_water_uv_location = 1;
    water_composite_view_position_location = 2;
    water_composite_world_position_location = 3;
    water_composite_surface_plane_location = 4;
    water_composite_view_plane_location = 5;
    water_composite_tangent_location = 6;
    water_composite_bitangent_location = 7;
    water_composite_surface_id_location = 8;
    water_composite_scene_location = glGetUniformLocation(program, "uScene");
    water_composite_depth_location = glGetUniformLocation(program, "uSceneDepth");
    water_composite_scene_depth_valid_location = glGetUniformLocation(program, "uSceneDepthValid");
    water_composite_line_location = -1;
    water_composite_time_location = glGetUniformLocation(program, "uTime");
    water_composite_tier_location = glGetUniformLocation(program, "uTier");
    water_composite_inverse_viewport_location = glGetUniformLocation(program, "uInverseViewport");
    water_composite_scene_viewport_location = glGetUniformLocation(program, "uSceneViewport");
    water_composite_framebuffer_viewport_location = glGetUniformLocation(program, "uFramebufferViewport");
    water_composite_base_location = glGetUniformLocation(program, "uWaterBase");
    water_composite_normal_location = glGetUniformLocation(program, "uWaterNormal");
    water_composite_reflection_location = glGetUniformLocation(program, "uReflection");
    water_composite_reflection_atlas_location = glGetUniformLocation(program, "uReflectionAtlasGrid");
    water_composite_viewport_location = glGetUniformLocation(program, "uViewport");
    water_composite_inverse_projection_location = glGetUniformLocation(program, "uInverseProjection");
    water_composite_world_to_clip_location = glGetUniformLocation(program, "uWaterWorldToClip");
    water_composite_height_location = glGetUniformLocation(program, "uWaterHeight");
    water_composite_height_texel_location = glGetUniformLocation(program, "uWaterHeightTexel");
    water_composite_heightfield_location = glGetUniformLocation(
        program, "uWaterHeightfield");
    water_composite_heightfield_scale_location = glGetUniformLocation(
        program, "uWaterHeightfieldScale");
    water_composite_world_to_view_location = glGetUniformLocation(program, "uWorldToView");
    water_composite_forward_projection_location = glGetUniformLocation(program, "uForwardProjection");
    water_composite_normal_matrix_location = glGetUniformLocation(program, "uWaterNormalMatrix");
    water_composite_depth_params_location = glGetUniformLocation(program, "uDepthParams");
    water_composite_absorption_location = glGetUniformLocation(program, "uWaterAbsorption");
    water_composite_displacement_location = glGetUniformLocation(program, "uWaterDisplacement");
    water_composite_disturbances_location = glGetUniformLocation(program, "uWaterDisturbances");
    water_composite_disturbance_count_location = glGetUniformLocation(program, "uWaterDisturbanceCount");
    water_composite_wave_length_location = glGetUniformLocation(program, "uWaveLength");
    water_composite_wave_amplitude_location = glGetUniformLocation(program, "uWaveAmplitude");
    water_composite_wave_speed_location = glGetUniformLocation(program, "uWaveSpeed");
    water_composite_wave_direction_location = glGetUniformLocation(program, "uWaveDirection");
    water_composite_contact_start_location = glGetUniformLocation(program, "uWaterContactStart");
    water_composite_contact_band_location = glGetUniformLocation(program, "uWaterContactBand");
    water_composite_feature_toggles_location = glGetUniformLocation(
        program, "uWaterFeatureToggles");
    water_composite_reflection_valid_location = glGetUniformLocation(
        program, "uWaterReflectionValid");
    water_composite_light_direction_location = glGetUniformLocation(
        program, "uWaterLightDirection");
    water_composite_light_color_location = glGetUniformLocation(
        program, "uWaterLightColor");
    water_composite_shininess_location = glGetUniformLocation(
        program, "uWaterShininess");
    const bool uniforms_ready = water_composite_scene_location >= 0 && water_composite_depth_location >= 0 &&
           water_composite_scene_depth_valid_location >= 0 &&
           water_composite_time_location >= 0 &&
           water_composite_tier_location >= 0 && water_composite_inverse_viewport_location >= 0 &&
           water_composite_scene_viewport_location >= 0 &&
           water_composite_framebuffer_viewport_location >= 0 &&
           water_composite_base_location >= 0 && water_composite_normal_location >= 0 &&
           water_composite_reflection_location >= 0 &&
           water_composite_reflection_atlas_location >= 0 &&
           water_composite_viewport_location >= 0 &&
           water_composite_inverse_projection_location >= 0 &&
           water_composite_world_to_clip_location >= 0 &&
           water_composite_height_texel_location >= 0 &&
           water_composite_heightfield_location >= 0 &&
           water_composite_heightfield_scale_location >= 0 &&
           water_composite_world_to_view_location >= 0 &&
           water_composite_forward_projection_location >= 0 &&
           water_composite_normal_matrix_location >= 0 &&
           water_composite_depth_params_location >= 0 &&
           water_composite_absorption_location >= 0 && water_composite_displacement_location >= 0 &&
           water_composite_disturbances_location >= 0 &&
           water_composite_disturbance_count_location >= 0 &&
           water_composite_wave_length_location >= 0 &&
           water_composite_wave_amplitude_location >= 0 &&
           water_composite_wave_speed_location >= 0 &&
           water_composite_wave_direction_location >= 0 &&
           water_composite_contact_start_location >= 0 &&
           water_composite_contact_band_location >= 0 &&
           water_composite_feature_toggles_location >= 0 &&
           water_composite_reflection_valid_location >= 0 &&
           water_composite_light_direction_location >= 0 &&
           water_composite_light_color_location >= 0 &&
           water_composite_shininess_location >= 0;
    if (!uniforms_ready) {
        if (getenv("GE_WATER_TRACE") != nullptr) {
            sysLogPrintf(LOG_WARNING,
                         "WATER_COMPOSITE: shader uniforms unavailable profile=%s",
                         water_shader_profile);
        }
        glDeleteProgram(program);
        water_composite_program = 0;
        water_composite_position_location = -1;
        water_composite_water_uv_location = -1;
        water_composite_view_position_location = -1;
        water_composite_world_position_location = -1;
        water_composite_surface_plane_location = -1;
        water_composite_view_plane_location = -1;
        water_composite_tangent_location = -1;
        water_composite_bitangent_location = -1;
        water_composite_surface_id_location = -1;
        water_composite_scene_location = -1;
        water_composite_depth_location = -1;
        water_composite_scene_depth_valid_location = -1;
        water_composite_time_location = -1;
        water_composite_tier_location = -1;
        water_composite_inverse_viewport_location = -1;
        water_composite_scene_viewport_location = -1;
        water_composite_framebuffer_viewport_location = -1;
        water_composite_base_location = -1;
        water_composite_normal_location = -1;
        water_composite_reflection_location = -1;
        water_composite_reflection_atlas_location = -1;
        water_composite_viewport_location = -1;
        water_composite_inverse_projection_location = -1;
        water_composite_world_to_clip_location = -1;
        water_composite_height_location = -1;
        water_composite_height_texel_location = -1;
        water_composite_heightfield_location = -1;
        water_composite_heightfield_scale_location = -1;
        water_composite_world_to_view_location = -1;
        water_composite_forward_projection_location = -1;
        water_composite_normal_matrix_location = -1;
        water_composite_depth_params_location = -1;
        water_composite_absorption_location = -1;
        water_composite_displacement_location = -1;
        water_composite_disturbances_location = -1;
        water_composite_disturbance_count_location = -1;
        water_composite_wave_length_location = -1;
        water_composite_wave_amplitude_location = -1;
        water_composite_wave_speed_location = -1;
        water_composite_wave_direction_location = -1;
        water_composite_contact_start_location = -1;
        water_composite_contact_band_location = -1;
        water_composite_feature_toggles_location = -1;
        water_composite_reflection_valid_location = -1;
        water_composite_light_direction_location = -1;
        water_composite_light_color_location = -1;
        water_composite_shininess_location = -1;
    }
    return uniforms_ready;
}

/* The original water fan is an RSP texture-combine primitive. The modern
 * composite needs a stable base and normal input even when that legacy
 * texture is discarded from the opaque scene copy. Keep the fallback
 * material procedural and deterministic so it is available on every backend
 * that exposes the OpenGL water path; the authored UV stream still drives it. */
static bool gfx_opengl_load_water_material_asset(const char *path,
                                                 std::vector<uint8_t> &pixels,
                                                 int expected_width,
                                                 int expected_height)
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    SDL_Surface *loaded = SDL_LoadBMP(path);
    if (loaded == nullptr || loaded->w != expected_width ||
        loaded->h != expected_height) {
        if (loaded != nullptr) {
            SDL_FreeSurface(loaded);
        }
        return false;
    }
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (rgba == nullptr) {
        return false;
    }
    pixels.resize((size_t)expected_width * (size_t)expected_height * 4U);
    for (int row = 0; row < expected_height; ++row) {
        const uint8_t *source = (const uint8_t *)rgba->pixels + row * rgba->pitch;
        uint8_t *destination = pixels.data() + (size_t)row * expected_width * 4U;
        std::memcpy(destination, source, (size_t)expected_width * 4U);
    }
    SDL_FreeSurface(rgba);
    return true;
}

static bool gfx_opengl_ensure_water_material_textures(void)
{
    if (water_material_base_texture != 0 && water_material_normal_texture != 0 &&
        water_material_height_texture != 0 && water_heightfield_texture != 0) {
        return true;
    }

    constexpr int kWaterMaterialSize = 64;
    std::vector<uint8_t> base(kWaterMaterialSize * kWaterMaterialSize * 4);
    std::vector<uint8_t> normal(kWaterMaterialSize * kWaterMaterialSize * 4);
    std::vector<uint8_t> height(kWaterMaterialSize * kWaterMaterialSize * 4);
    const char *water_material_asset = getenv("GE_WATER_BASE");
    const char *water_normal_asset = getenv("GE_WATER_NORMAL");
    const char *water_height_asset = getenv("GE_WATER_HEIGHT");
    const bool authored_base = gfx_opengl_load_water_material_asset(
        water_material_asset, base, kWaterMaterialSize, kWaterMaterialSize);
    const bool authored_normal = gfx_opengl_load_water_material_asset(
        water_normal_asset, normal, kWaterMaterialSize, kWaterMaterialSize);
    const bool authored_height = gfx_opengl_load_water_material_asset(
        water_height_asset, height, kWaterMaterialSize, kWaterMaterialSize);
    const bool water_material_fallback_texture = !authored_base;
    if (water_material_fallback_texture && getenv("GE_WATER_TRACE") != nullptr) {
        sysLogPrintf(LOG_NOTE, "WATER_MATERIAL: fallback_texture=procedural");
    }
    for (int y = 0; y < kWaterMaterialSize; ++y) {
        for (int x = 0; x < kWaterMaterialSize; ++x) {
            /* Avoid making the renderer's link depend on libm just for this
             * fallback asset. The integer lattice still gives the material
             * enough repeatable variation to keep the normal-map path alive
             * on drivers where no authored water texture is available. */
            const size_t i = (size_t)(y * kWaterMaterialSize + x) * 4;
            if (!authored_base) {
                const float coarse_wave =
                    (float)((x / 4 * 17 + y / 4 * 29 + (x / 8) * (y / 8)) & 31) /
                    31.0f;
                /* The analytic wave/heightfield supplies motion and normal
                 * detail. Keep the fallback albedo a low-contrast water tint;
                 * a hash-like blue checker reads as glitter/noise once it is
                 * reflected and mip-filtered across a large sea. Authored
                 * GE_WATER_BASE assets still replace this completely. */
                base[i + 0] = (uint8_t)(18.0f + coarse_wave * 7.0f);
                base[i + 1] = (uint8_t)(78.0f + coarse_wave * 18.0f);
                base[i + 2] = (uint8_t)(118.0f + coarse_wave * 28.0f);
                base[i + 3] = 255;
            }
            if (!authored_normal) {
                const float dx = (float)(((x * 17 + y * 5) & 31) - 15) / 48.0f;
                const float dy = (float)(((x * 7 + y * 23) & 31) - 15) / 64.0f;
                /* Preserve only a restrained secondary micro-normal. The
                 * heightfield normal is the authoritative broad wave shape;
                 * this map should not overpower it with pixel-sized facets. */
                normal[i + 0] = (uint8_t)(128.0f - dx * 28.0f);
                normal[i + 1] = (uint8_t)(128.0f - dy * 24.0f);
                normal[i + 2] = 252;
                normal[i + 3] = 255;
            }
            if (!authored_height) {
                /* 50% is the neutral signed height. The procedural wave field
                 * remains the visible displacement when no authored map is
                 * supplied. */
                height[i + 0] = 128;
                height[i + 1] = 128;
                height[i + 2] = 128;
                height[i + 3] = 255;
            }
        }
    }

    glGenTextures(1, &water_material_base_texture);
    glGenTextures(1, &water_material_normal_texture);
    glGenTextures(1, &water_material_height_texture);
    const GLuint textures[3] = {
        water_material_base_texture, water_material_normal_texture,
        water_material_height_texture,
    };
    const uint8_t *pixels[3] = {base.data(), normal.data(), height.data()};
    for (int texture_index = 0; texture_index < 3; ++texture_index) {
        glBindTexture(GL_TEXTURE_2D, textures[texture_index]);
        GLint internal_format = GL_RGBA8;
#ifdef GL_SRGB8_ALPHA8
        if (texture_index == 0) {
            internal_format = GL_SRGB8_ALPHA8;
        }
#endif
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format, kWaterMaterialSize,
                     kWaterMaterialSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     pixels[texture_index]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    std::vector<uint8_t> heightfield_neutral(
        kWaterMaterialSize * kWaterMaterialSize * 4, 128U);
    for (int i = 0; i < kWaterMaterialSize * kWaterMaterialSize; ++i) {
        heightfield_neutral[(size_t)i * 4U + 3U] = 255U;
    }
    glGenTextures(1, &water_heightfield_texture);
    glBindTexture(GL_TEXTURE_2D, water_heightfield_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWaterMaterialSize,
                 kWaterMaterialSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 heightfield_neutral.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint max_fragment_texture_units = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_fragment_texture_units);
    water_heightfield_texture_unit = max_fragment_texture_units > 6 ? 6 : -1;
    if (water_heightfield_texture_unit < 0 && getenv("GE_WATER_TRACE") != nullptr) {
        sysLogPrintf(LOG_WARNING,
                     "WATER_HEIGHTFIELD: disabled; fragment texture units=%d",
                     max_fragment_texture_units);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return glGetError() == GL_NO_ERROR;
}

static bool gfx_opengl_upload_water_heightfield(
    const struct GfxWaterHeightfieldView *heightfield)
{
    if (water_heightfield_texture == 0 || water_heightfield_texture_unit < 0 ||
        heightfield == nullptr || heightfield->width != GE_WATER_HEIGHTFIELD_SIZE ||
        heightfield->height != GE_WATER_HEIGHTFIELD_SIZE ||
        heightfield->heights == nullptr || heightfield->normal_x == nullptr ||
        heightfield->normal_z == nullptr ||
        !std::isfinite(heightfield->max_height) ||
        heightfield->max_height <= 0.0f) {
        return false;
    }

    std::vector<uint8_t> pixels(
        GE_WATER_HEIGHTFIELD_CELL_COUNT * 4U, 128U);
    const float inverse_height = 1.0f / heightfield->max_height;
    for (std::size_t i = 0; i < GE_WATER_HEIGHTFIELD_CELL_COUNT; ++i) {
        const float signed_height = std::isfinite(heightfield->heights[i])
                                        ? heightfield->heights[i]
                                        : 0.0f;
        const float normal_x = std::isfinite(heightfield->normal_x[i])
                                   ? heightfield->normal_x[i]
                                   : 0.0f;
        const float normal_z = std::isfinite(heightfield->normal_z[i])
                                   ? heightfield->normal_z[i]
                                   : 0.0f;
        const float encoded_height = std::clamp(
            0.5f + 0.5f * signed_height * inverse_height, 0.0f, 1.0f);
        const float encoded_normal_x =
            std::clamp(0.5f + 0.5f * normal_x, 0.0f, 1.0f);
        const float encoded_normal_z =
            std::clamp(0.5f + 0.5f * normal_z, 0.0f, 1.0f);
        pixels[i * 4U + 0U] =
            (uint8_t)(encoded_height * 255.0f + 0.5f);
        pixels[i * 4U + 1U] =
            (uint8_t)(encoded_normal_x * 255.0f + 0.5f);
        pixels[i * 4U + 2U] =
            (uint8_t)(encoded_normal_z * 255.0f + 0.5f);
        pixels[i * 4U + 3U] = 255U;
    }

    glActiveTexture(GL_TEXTURE0 + water_heightfield_texture_unit);
    glBindTexture(GL_TEXTURE_2D, water_heightfield_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    (GLsizei)GE_WATER_HEIGHTFIELD_SIZE,
                    (GLsizei)GE_WATER_HEIGHTFIELD_SIZE,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return glGetError() == GL_NO_ERROR;
}

static bool gfx_opengl_ensure_water_scene(uint32_t width, uint32_t height)
{
    if (!gfx_framebuffers_enabled || width == 0 || height == 0) {
        return false;
    }
    if (water_scene_fbo == 0 || water_reflection_fbo == 0 ||
        water_scene_color == 0 || water_scene_depth == 0 ||
        water_reflection_color == 0 || water_reflection_depth == 0) {
        gfx_opengl_release_water_resources();
        glGenFramebuffers(1, &water_scene_fbo);
        glGenTextures(1, &water_scene_color);
        glGenTextures(1, &water_scene_depth);
        glGenFramebuffers(1, &water_reflection_fbo);
        glGenTextures(1, &water_reflection_color);
        glGenTextures(1, &water_reflection_depth);
        if (water_scene_fbo == 0 || water_scene_color == 0 ||
            water_scene_depth == 0 || water_reflection_fbo == 0 ||
            water_reflection_color == 0 || water_reflection_depth == 0) {
            gfx_opengl_release_water_resources();
            return false;
        }
    }
    if (water_scene_width == width && water_scene_height == height) {
        return gfx_opengl_ensure_water_material_textures();
    }

    /* linear_hdr: desktop water intermediates use floating-point RGBA so
     * Fresnel, absorption, and reflection accumulation do not quantize at the
     * scene-copy boundary. GLES stays on the RGB8 path because this renderer
     * already supports that attachment format across its ES targets. The
     * material base texture is sRGB and is decoded by the sampler; these
     * intermediate targets are deliberately linear. */
    const bool desktop_hdr = !gl_es && GLVersion.major >= 3;
    const GLenum color_internal_format = desktop_hdr ? GL_RGBA16F
                                                       : (gl_es ? GL_RGB8 : GL_RGBA8);
    const GLenum color_format = gl_es ? GL_RGB : GL_RGBA;
    const GLenum color_upload_type = desktop_hdr ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
    const GLenum depth_upload_type = gl_es ? GL_UNSIGNED_INT : GL_FLOAT;

    glBindFramebuffer(GL_FRAMEBUFFER, water_scene_fbo);
    glBindTexture(GL_TEXTURE_2D, water_scene_color);
    glTexImage2D(GL_TEXTURE_2D, 0, color_internal_format, width, height, 0,
                 color_format,
                 color_upload_type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           water_scene_color, 0);

    glBindTexture(GL_TEXTURE_2D, water_scene_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
                 GL_DEPTH_COMPONENT, depth_upload_type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           water_scene_depth, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    gfx_opengl_set_draw_buffer(GL_COLOR_ATTACHMENT0);
    const GLenum scene_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_FRAMEBUFFER, water_reflection_fbo);
    glBindTexture(GL_TEXTURE_2D, water_reflection_color);
    glTexImage2D(GL_TEXTURE_2D, 0, color_internal_format, width, height, 0,
                 color_format,
                 color_upload_type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           water_reflection_color, 0);
    glBindTexture(GL_TEXTURE_2D, water_reflection_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
                 GL_DEPTH_COMPONENT, depth_upload_type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           water_reflection_depth, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    gfx_opengl_set_draw_buffer(GL_COLOR_ATTACHMENT0);
    const GLenum reflection_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (scene_status == GL_FRAMEBUFFER_COMPLETE &&
        reflection_status == GL_FRAMEBUFFER_COMPLETE &&
        gfx_opengl_ensure_water_material_textures()) {
        water_scene_width = width;
        water_scene_height = height;
        water_scene_hdr = desktop_hdr;
        return true;
    }
    sysLogPrintf(LOG_WARNING,
                 "WATER_COMPOSITE: framebuffer incomplete scene=0x%x reflection=0x%x",
                 (unsigned)scene_status, (unsigned)reflection_status);
    return false;
}

static constexpr GLuint kWaterSavedVertexAttributes = 9;

struct WaterCompositeGLState {
    GLint program;
    GLint vertex_array;
    GLint array_buffer;
    GLint active_texture;
    GLint texture0;
    GLint texture1;
    GLint texture2;
    GLint texture3;
    GLint texture4;
    GLint texture5;
    GLint texture6;
    GLint draw_framebuffer;
    GLint read_framebuffer;
    GLint draw_buffer;
    GLint read_buffer;
    GLint viewport[4];
    GLint scissor_box[4];
    GLboolean depth_test;
    GLboolean blend;
    GLboolean cull_face;
    GLboolean scissor_test;
    GLboolean vertex_attrib_enabled[kWaterSavedVertexAttributes];
    GLint vertex_attrib_size[kWaterSavedVertexAttributes];
    GLint vertex_attrib_type[kWaterSavedVertexAttributes];
    GLint vertex_attrib_normalized[kWaterSavedVertexAttributes];
    GLint vertex_attrib_stride[kWaterSavedVertexAttributes];
    GLint vertex_attrib_array_buffer[kWaterSavedVertexAttributes];
    void *vertex_attrib_pointer[kWaterSavedVertexAttributes];
    GLboolean depth_mask;
    GLint depth_func;
    GLboolean color_mask[4];
    GLint blend_src_rgb;
    GLint blend_dst_rgb;
    GLint blend_src_alpha;
    GLint blend_dst_alpha;
    GLint blend_equation_rgb;
    GLint blend_equation_alpha;
    GLdouble depth_range[2];
    GLint stencil_func;
    GLint stencil_ref;
    GLint stencil_value_mask;
    GLint stencil_fail;
    GLint stencil_pass_depth_fail;
    GLint stencil_pass_depth_pass;
    GLint stencil_write_mask;
    GLfloat polygon_offset_factor;
    GLfloat polygon_offset_units;
    GLdouble depth_clear_value;
    GLboolean stencil_test;
    GLboolean framebuffer_srgb;
    GLboolean polygon_offset_fill;
};

static WaterCompositeGLState gfx_opengl_save_water_state(void)
{
    WaterCompositeGLState state = {};
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vertex_array);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.array_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture1);
    glActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture2);
    glActiveTexture(GL_TEXTURE3);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture3);
    glActiveTexture(GL_TEXTURE4);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture4);
    glActiveTexture(GL_TEXTURE5);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture5);
    glActiveTexture(GL_TEXTURE6);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture6);
    glActiveTexture(state.active_texture);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.read_framebuffer);
    state.draw_buffer = gfx_opengl_get_draw_buffer();
    glGetIntegerv(GL_READ_BUFFER, &state.read_buffer);
    glGetIntegerv(GL_VIEWPORT, state.viewport);
    glGetIntegerv(GL_SCISSOR_BOX, state.scissor_box);
    for (GLuint attribute = 0; attribute < kWaterSavedVertexAttributes; ++attribute) {
        GLint enabled = GL_FALSE;
        glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        state.vertex_attrib_enabled[attribute] = enabled ? GL_TRUE : GL_FALSE;
        glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_SIZE,
                           &state.vertex_attrib_size[attribute]);
        glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_TYPE,
                           &state.vertex_attrib_type[attribute]);
        glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
                           &state.vertex_attrib_normalized[attribute]);
        glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_STRIDE,
                           &state.vertex_attrib_stride[attribute]);
        glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,
                           &state.vertex_attrib_array_buffer[attribute]);
        glGetVertexAttribPointerv(attribute, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                                  &state.vertex_attrib_pointer[attribute]);
    }
    state.depth_test = glIsEnabled(GL_DEPTH_TEST);
    state.blend = glIsEnabled(GL_BLEND);
    state.cull_face = glIsEnabled(GL_CULL_FACE);
    state.scissor_test = glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depth_mask);
    glGetIntegerv(GL_DEPTH_FUNC, &state.depth_func);
    glGetBooleanv(GL_COLOR_WRITEMASK, state.color_mask);
    glGetIntegerv(GL_BLEND_SRC_RGB, &state.blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &state.blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &state.blend_dst_alpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.blend_equation_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.blend_equation_alpha);
    if (gl_es) {
        GLfloat depth_range[2] = {};
        glGetFloatv(GL_DEPTH_RANGE, depth_range);
        state.depth_range[0] = depth_range[0];
        state.depth_range[1] = depth_range[1];
        GLfloat clear_value = 1.0f;
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clear_value);
        state.depth_clear_value = clear_value;
    } else {
        glGetDoublev(GL_DEPTH_RANGE, state.depth_range);
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &state.depth_clear_value);
    }
    glGetIntegerv(GL_STENCIL_FUNC, &state.stencil_func);
    glGetIntegerv(GL_STENCIL_REF, &state.stencil_ref);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &state.stencil_value_mask);
    glGetIntegerv(GL_STENCIL_FAIL, &state.stencil_fail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &state.stencil_pass_depth_fail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &state.stencil_pass_depth_pass);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &state.stencil_write_mask);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &state.polygon_offset_factor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &state.polygon_offset_units);
    state.stencil_test = glIsEnabled(GL_STENCIL_TEST);
    state.framebuffer_srgb = gfx_opengl_is_framebuffer_srgb_enabled();
    state.polygon_offset_fill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    return state;
}

static void gfx_opengl_restore_water_state(const WaterCompositeGLState &state)
{
    glUseProgram((GLuint)state.program);
    glBindVertexArray((GLuint)state.vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)state.array_buffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)state.texture0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, (GLuint)state.texture1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, (GLuint)state.texture2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, (GLuint)state.texture3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, (GLuint)state.texture4);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, (GLuint)state.texture5);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, (GLuint)state.texture6);
    glActiveTexture(state.active_texture);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)state.draw_framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)state.read_framebuffer);
    gfx_opengl_set_draw_buffer((GLenum)state.draw_buffer);
    glReadBuffer((GLenum)state.read_buffer);
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    glScissor(state.scissor_box[0], state.scissor_box[1],
              state.scissor_box[2], state.scissor_box[3]);
    if (state.depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (state.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (state.cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (state.scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glColorMask(state.color_mask[0], state.color_mask[1],
                state.color_mask[2], state.color_mask[3]);
    if (glBlendFuncSeparate != nullptr) {
        glBlendFuncSeparate((GLenum)state.blend_src_rgb,
                            (GLenum)state.blend_dst_rgb,
                            (GLenum)state.blend_src_alpha,
                            (GLenum)state.blend_dst_alpha);
    } else if (glBlendFunc != nullptr) {
        glBlendFunc((GLenum)state.blend_src_rgb,
                    (GLenum)state.blend_dst_rgb);
    }
    if (glBlendEquationSeparate != nullptr) {
        glBlendEquationSeparate((GLenum)state.blend_equation_rgb,
                                (GLenum)state.blend_equation_alpha);
    } else if (glBlendEquation != nullptr) {
        glBlendEquation((GLenum)state.blend_equation_rgb);
    }
    gfx_opengl_set_depth_range((float)state.depth_range[0],
                               (float)state.depth_range[1]);
    if (gl_es && glClearDepthf != nullptr) {
        glClearDepthf((GLfloat)state.depth_clear_value);
    } else if (glClearDepth != nullptr) {
        glClearDepth(state.depth_clear_value);
    }
    if (state.stencil_test) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    glStencilFunc((GLenum)state.stencil_func, state.stencil_ref,
                  (GLuint)state.stencil_value_mask);
    glStencilOp((GLenum)state.stencil_fail,
                (GLenum)state.stencil_pass_depth_fail,
                (GLenum)state.stencil_pass_depth_pass);
    glStencilMask((GLuint)state.stencil_write_mask);
    glPolygonOffset(state.polygon_offset_factor, state.polygon_offset_units);
    gfx_opengl_set_framebuffer_srgb(state.framebuffer_srgb);
    if (state.polygon_offset_fill) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
    for (GLuint attribute = 0; attribute < kWaterSavedVertexAttributes; ++attribute) {
        glBindBuffer(GL_ARRAY_BUFFER,
                     (GLuint)state.vertex_attrib_array_buffer[attribute]);
        glVertexAttribPointer(attribute, state.vertex_attrib_size[attribute],
                              (GLenum)state.vertex_attrib_type[attribute],
                              state.vertex_attrib_normalized[attribute] ? GL_TRUE : GL_FALSE,
                              state.vertex_attrib_stride[attribute],
                              state.vertex_attrib_pointer[attribute]);
        if (state.vertex_attrib_enabled[attribute]) {
            glEnableVertexAttribArray(attribute);
        } else {
            glDisableVertexAttribArray(attribute);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)state.array_buffer);
    glDepthMask(state.depth_mask);
    glDepthFunc((GLenum)state.depth_func);
}

static WaterCompositeGLState shadow_capture_saved_gl_state = {};
static bool shadow_capture_gl_state_saved = false;
static void gfx_opengl_end_shadow_scene_capture(void);

static bool gfx_opengl_shadow_supported(void)
{
    if (!gfx_framebuffers_enabled || glGenFramebuffers == nullptr ||
        glBindFramebuffer == nullptr || glFramebufferTexture2D == nullptr ||
        glCheckFramebufferStatus == nullptr || glReadBuffer == nullptr ||
        glGenTextures == nullptr ||
        glTexImage2D == nullptr || glCreateShader == nullptr ||
        glCreateProgram == nullptr || glBindBuffer == nullptr ||
        glBufferData == nullptr || glDrawArrays == nullptr ||
        glUseProgram == nullptr || glActiveTexture == nullptr) {
        return false;
    }
    GLint texture_units = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texture_units);
    if (texture_units < 7) {
        return false;
    }
    if ((gl_core_profile || gl_es) &&
        (glGenVertexArrays == nullptr || glBindVertexArray == nullptr)) {
        return false;
    }
    return true;
}

static GLuint gfx_opengl_compile_shadow_stage(GLenum stage,
                                               const char *source)
{
    const GLuint shader = glCreateShader(stage);
    if (shader == 0) {
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }
    char log[1024] = {};
    GLsizei log_length = 0;
    glGetShaderInfoLog(shader, sizeof(log), &log_length, log);
    sysLogPrintf(LOG_WARNING,
                 "graphics: optional shadow shader compilation failed: %s",
                 log_length > 0 ? log : "no compiler log");
    glDeleteShader(shader);
    return 0;
}

static bool gfx_opengl_ensure_shadow_program(void)
{
    if (shadow_map_program != 0) {
        return true;
    }
    char vertex_source[512] = {};
    char fragment_source[256] = {};
    const char *input_qualifier = gl_glsl_version >= 130 ? "in" : "attribute";
    const char *vertex_precision = gl_es ? "precision highp float;\n" : "";
    const char *fragment_precision = gl_es ? "precision mediump float;\n" : "";
    snprintf(vertex_source, sizeof(vertex_source),
             "#version %s\n%s%s vec3 aShadowPosition;\n"
             "uniform mat4 uShadowLightSpace;\n"
             "void main() { gl_Position = uShadowLightSpace * vec4(aShadowPosition, 1.0); }\n",
             gl_glsl_version_str, vertex_precision, input_qualifier);
    snprintf(fragment_source, sizeof(fragment_source),
             "#version %s\n%svoid main() { }\n",
             gl_glsl_version_str, fragment_precision);

    const GLuint vertex_shader =
        gfx_opengl_compile_shadow_stage(GL_VERTEX_SHADER, vertex_source);
    if (vertex_shader == 0) {
        return false;
    }
    const GLuint fragment_shader =
        gfx_opengl_compile_shadow_stage(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return false;
    }

    const GLuint program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "aShadowPosition");
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glDetachShader(program, vertex_shader);
    glDetachShader(program, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    if (linked != GL_TRUE) {
        char log[1024] = {};
        GLsizei log_length = 0;
        glGetProgramInfoLog(program, sizeof(log), &log_length, log);
        sysLogPrintf(LOG_WARNING,
                     "graphics: optional shadow program link failed: %s",
                     log_length > 0 ? log : "no linker log");
        glDeleteProgram(program);
        return false;
    }
    shadow_map_position_location = glGetAttribLocation(program,
                                                        "aShadowPosition");
    shadow_map_matrix_location = glGetUniformLocation(program,
                                                       "uShadowLightSpace");
    if (shadow_map_position_location < 0 || shadow_map_matrix_location < 0) {
        glDeleteProgram(program);
        shadow_map_position_location = -1;
        shadow_map_matrix_location = -1;
        return false;
    }
    shadow_map_program = program;
    return true;
}

static bool gfx_opengl_ensure_shadow_resources(void)
{
    if (shadow_resources_failed || !gfx_opengl_shadow_supported()) {
        return false;
    }
    if (!gfx_opengl_ensure_shadow_program()) {
        shadow_resources_failed = true;
        return false;
    }
    GLint max_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    if (max_texture_size < 256) {
        shadow_resources_failed = true;
        return false;
    }
    const uint32_t resolution = static_cast<uint32_t>(
        std::min<GLint>(max_texture_size, kShadowMapResolution));

    if (shadow_map_fbo == 0) {
        glGenFramebuffers(1, &shadow_map_fbo);
    }
    if (shadow_map_texture == 0) {
        glGenTextures(1, &shadow_map_texture);
    }
    if (shadow_map_vbo == 0) {
        glGenBuffers(1, &shadow_map_vbo);
    }
    if ((gl_core_profile || gl_es) && shadow_map_vao == 0) {
        glGenVertexArrays(1, &shadow_map_vao);
    }
    if (shadow_map_fbo == 0 || shadow_map_texture == 0 || shadow_map_vbo == 0 ||
        ((gl_core_profile || gl_es) && shadow_map_vao == 0)) {
        shadow_resources_failed = true;
        return false;
    }

    if (shadow_map_allocated_resolution == resolution) {
        return true;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadow_map_texture);
    const GLenum depth_upload_type = gl_es ? GL_UNSIGNED_INT : GL_FLOAT;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 static_cast<GLsizei>(resolution),
                 static_cast<GLsizei>(resolution), 0, GL_DEPTH_COMPONENT,
                 depth_upload_type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, shadow_map_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, shadow_map_texture, 0);
    gfx_opengl_set_draw_buffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        shadow_map_ready = false;
        shadow_resources_failed = true;
        sysLogPrintf(LOG_WARNING,
                     "graphics: shadow depth framebuffer incomplete (0x%x)",
                     static_cast<unsigned>(status));
        return false;
    }
    shadow_map_resolution = resolution;
    shadow_map_allocated_resolution = resolution;
    return true;
}

static bool gfx_opengl_begin_shadow_scene_capture(void)
{
    if (shadow_capture_gl_state_saved || shadow_resources_failed ||
        !gfx_opengl_shadow_supported()) {
        return false;
    }
    shadow_capture_saved_gl_state = gfx_opengl_save_water_state();
    shadow_capture_gl_state_saved = true;
    if (!gfx_opengl_ensure_shadow_resources()) {
        gfx_opengl_end_shadow_scene_capture();
        return false;
    }
    return true;
}

static void gfx_opengl_end_shadow_scene_capture(void)
{
    if (!shadow_capture_gl_state_saved) {
        return;
    }
    gfx_opengl_restore_water_state(shadow_capture_saved_gl_state);
    shadow_capture_gl_state_saved = false;
}

static bool gfx_opengl_render_shadow_map(const float *view_space_vertices,
                                         size_t vertex_count,
                                         const float direction_to_light[3])
{
    shadow_map_ready = false;
    if (view_space_vertices == nullptr || vertex_count < 3 ||
        vertex_count % 3 != 0 ||
        vertex_count > geShadowMaximumVertexCount ||
        direction_to_light == nullptr ||
        vertex_count > static_cast<size_t>(std::numeric_limits<GLsizei>::max()) ||
        vertex_count > static_cast<size_t>(
            std::numeric_limits<GLsizeiptr>::max() / (3 * sizeof(float)))) {
        return false;
    }
    static bool stale_gl_error_reported = false;
    GLenum stale_error = GL_NO_ERROR;
    for (int drain = 0; drain < 16; ++drain) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) {
            break;
        }
        if (stale_error == GL_NO_ERROR) {
            stale_error = error;
        }
    }
    if (stale_error != GL_NO_ERROR && !stale_gl_error_reported) {
        sysLogPrintf(LOG_WARNING,
                     "graphics: clearing pre-existing GL error 0x%x before optional shadow pass",
                     static_cast<unsigned>(stale_error));
        stale_gl_error_reported = true;
    }
    WaterCompositeGLState saved = gfx_opengl_save_water_state();
    GeShadowMatrix light_space = {};
    if (!geShadowBuildDirectionalMatrix(view_space_vertices, vertex_count,
                                        direction_to_light, 0.05f,
                                        &light_space) ||
        !gfx_opengl_ensure_shadow_resources()) {
        gfx_opengl_restore_water_state(saved);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, shadow_map_fbo);
    gfx_opengl_set_draw_buffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glViewport(0, 0, static_cast<GLsizei>(shadow_map_resolution),
               static_cast<GLsizei>(shadow_map_resolution));
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    gfx_opengl_set_depth_range(0.0f, 1.0f);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    if (gl_es && glClearDepthf != nullptr) {
        glClearDepthf(1.0f);
    } else if (glClearDepth != nullptr) {
        glClearDepth(1.0);
    }
    glClear(GL_DEPTH_BUFFER_BIT);

    if (shadow_map_vao != 0) {
        glBindVertexArray(shadow_map_vao);
    }
    glUseProgram(shadow_map_program);
    glUniformMatrix4fv(shadow_map_matrix_location, 1, GL_FALSE,
                       light_space.column_major);
    glBindBuffer(GL_ARRAY_BUFFER, shadow_map_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertex_count * 3 * sizeof(float)),
                 view_space_vertices, GL_STREAM_DRAW);
    glEnableVertexAttribArray(
        static_cast<GLuint>(shadow_map_position_location));
    glVertexAttribPointer(static_cast<GLuint>(shadow_map_position_location),
                          3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertex_count));

    const GLenum render_error = glGetError();
    gfx_opengl_restore_water_state(saved);
    if (render_error != GL_NO_ERROR) {
        shadow_resources_failed = true;
        sysLogPrintf(LOG_WARNING,
                     "graphics: shadow depth pass failed with GL error 0x%x",
                     static_cast<unsigned>(render_error));
        return false;
    }
    shadow_light_space = light_space;
    shadow_map_ready = true;
    return true;
}

static void gfx_opengl_release_shadow_resources(void)
{
    shadow_map_ready = false;
    if (shadow_map_program != 0) {
        glDeleteProgram(shadow_map_program);
        shadow_map_program = 0;
    }
    if (shadow_map_vao != 0) {
        glDeleteVertexArrays(1, &shadow_map_vao);
        shadow_map_vao = 0;
    }
    if (shadow_map_vbo != 0) {
        glDeleteBuffers(1, &shadow_map_vbo);
        shadow_map_vbo = 0;
    }
    if (shadow_map_fbo != 0) {
        glDeleteFramebuffers(1, &shadow_map_fbo);
        shadow_map_fbo = 0;
    }
    if (shadow_map_texture != 0) {
        glDeleteTextures(1, &shadow_map_texture);
        shadow_map_texture = 0;
    }
    shadow_map_position_location = -1;
    shadow_map_matrix_location = -1;
    shadow_map_resolution = kShadowMapResolution;
    shadow_map_allocated_resolution = 0;
    shadow_resources_failed = false;
}

static WaterCompositeGLState water_reflection_saved_state = {};
static bool water_reflection_state_saved = false;
static bool water_reflection_populated = false;
static bool water_reflection_saved_texture_filters[3] = {};
static bool water_reflection_saved_bump_map_enabled = false;
static GLint water_reflection_saved_bump_map_enabled_location = -1;

/* Diagnostic switches are intentionally opt-in and presence-based.  A
 * value of "0" keeps a feature enabled, which makes shell matrices easy to
 * generate without rebuilding the binary. */
static bool gfx_water_diagnostic_feature_enabled(const char *disable_name)
{
    const char *value = disable_name != nullptr ? getenv(disable_name) : nullptr;
    return value == nullptr || value[0] == '\0' || strcmp(value, "0") == 0;
}

static unsigned int gfx_water_diagnostic_feature_mask(void)
{
    unsigned int mask = GE_WATER_FEATURE_ALL;
    if (!gfx_water_diagnostic_feature_enabled("GE_WATER_DISABLE_REFLECTION")) {
        mask &= ~GE_WATER_FEATURE_REFLECTION;
    }
    if (!gfx_water_diagnostic_feature_enabled("GE_WATER_DISABLE_REFRACTION")) {
        mask &= ~GE_WATER_FEATURE_REFRACTION;
    }
    if (!gfx_water_diagnostic_feature_enabled("GE_WATER_DISABLE_NORMALS")) {
        mask &= ~GE_WATER_FEATURE_NORMALS;
    }
    if (!gfx_water_diagnostic_feature_enabled("GE_WATER_DISABLE_FOAM")) {
        mask &= ~GE_WATER_FEATURE_FOAM;
    }
    return mask;
}

static bool gfx_water_diagnostic_reflection_only(void)
{
    const char *value = getenv("GE_WATER_DEBUG_REFLECTION");
    return value != nullptr && strcmp(value, "1") == 0;
}

static bool gfx_opengl_begin_reflected_camera(
    const struct GfxWaterCompositeCamera *camera,
    const float view_plane[4],
    uint32_t surface_index,
    uint32_t surface_count)
{
    water_reflection_populated = false;
    water_reflection_capture_active = false;
    water_reflection_draw_calls = 0;
    if (camera == nullptr || view_plane == nullptr ||
        camera->viewport_width == 0 || camera->viewport_height == 0 ||
        surface_count == 0 ||
        surface_count > GE_WATER_REFLECTION_ATLAS_CAPACITY ||
        surface_index >= surface_count ||
        !gfx_framebuffers_enabled || current_framebuffer >= framebuffers.size()) {
        return false;
    }
    for (int component = 0; component < 4; ++component) {
        if (!std::isfinite(view_plane[component])) {
            return false;
        }
    }

    water_reflection_saved_state = gfx_opengl_save_water_state();
    water_reflection_state_saved = true;
    for (int texture = 0; texture < 3; ++texture) {
        water_reflection_saved_texture_filters[texture] =
            current_textures_linear_filter[texture];
    }
    water_reflection_saved_bump_map_enabled = current_bump_map_enabled;
    water_reflection_saved_bump_map_enabled_location =
        current_bump_map_enabled_location;

    const Framebuffer &source = framebuffers[current_framebuffer];
    if (source.width == 0 || source.height == 0 ||
        !gfx_opengl_ensure_water_scene(source.width, source.height)) {
        gfx_opengl_restore_water_state(water_reflection_saved_state);
        water_reflection_state_saved = false;
        return false;
    }

    /* The private target is a bounded 4x2 atlas. Each non-coplanar surface
     * receives an independent reflected-camera tile; clear only that tile so
     * the next surface cannot sample stale pixels from a previous replay. */
    const uint32_t atlas_columns = camera->reflection_atlas_columns != 0
                                       ? camera->reflection_atlas_columns
                                       : GE_WATER_REFLECTION_ATLAS_COLUMNS;
    const uint32_t atlas_rows = camera->reflection_atlas_rows != 0
                                    ? camera->reflection_atlas_rows
                                    : GE_WATER_REFLECTION_ATLAS_ROWS;
    if (atlas_columns * atlas_rows < surface_count || atlas_columns == 0 ||
        atlas_rows == 0) {
        gfx_opengl_restore_water_state(water_reflection_saved_state);
        water_reflection_state_saved = false;
        return false;
    }
    const uint32_t tile_width = std::max(source.width / atlas_columns, 1U);
    const uint32_t tile_height = std::max(source.height / atlas_rows, 1U);
    const uint32_t tile_column = surface_index % atlas_columns;
    const uint32_t tile_row = surface_index / atlas_columns;
    const uint32_t tile_x = tile_column * tile_width;
    const uint32_t tile_y = tile_row * tile_height;
    if (tile_x >= source.width || tile_y >= source.height) {
        gfx_opengl_restore_water_state(water_reflection_saved_state);
        water_reflection_state_saved = false;
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, water_reflection_fbo);
    gfx_opengl_set_draw_buffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport((GLint)tile_x, (GLint)tile_y,
               (GLsizei)std::min(tile_width, source.width - tile_x),
               (GLsizei)std::min(tile_height, source.height - tile_y));
    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)tile_x, (GLint)tile_y,
              (GLsizei)std::min(tile_width, source.width - tile_x),
              (GLsizei)std::min(tile_height, source.height - tile_y));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    /* Mirroring the camera reverses winding. The replay's clipper owns the
     * water-plane boundary, so disabling culling here keeps both original
     * front-facing and mirrored back-facing polygons available. */
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (glGetError() != GL_NO_ERROR) {
        gfx_opengl_restore_water_state(water_reflection_saved_state);
        water_reflection_state_saved = false;
        return false;
    }
    water_reflection_capture_active = true;
    return true;
}

static bool gfx_opengl_end_reflected_camera(void)
{
    if (!water_reflection_state_saved) {
        return false;
    }
    const unsigned int draw_calls = water_reflection_draw_calls;
    water_reflection_capture_active = false;
    water_reflection_populated = draw_calls > 0;
    if (getenv("GE_WATER_TRACE") != nullptr) {
        sysLogPrintf(LOG_NOTE,
                     "WATER_REFLECTION: replay_draw_calls=%u populated=%d",
                     draw_calls, water_reflection_populated ? 1 : 0);
    }
    gfx_opengl_restore_water_state(water_reflection_saved_state);
    for (int texture = 0; texture < 3; ++texture) {
        current_textures_linear_filter[texture] =
            water_reflection_saved_texture_filters[texture];
    }
    current_bump_map_enabled = water_reflection_saved_bump_map_enabled;
    current_bump_map_enabled_location =
        water_reflection_saved_bump_map_enabled_location;
    water_reflection_state_saved = false;
    return water_reflection_populated;
}

/* The legacy sky water is usually only two large fan triangles. A procedural
 * height field sampled at those three vertices cannot produce a stable crest,
 * normal, or contact edge across the surface. Tessellate each captured
 * triangle in its own barycentric waterMesh before the modern composite draw;
 * the source display-list ordering and all authored surface attributes remain
 * unchanged. */
static GfxWaterCompositeVertex gfx_water_interpolate_vertex(
    const GfxWaterCompositeVertex &a, const GfxWaterCompositeVertex &b,
    const GfxWaterCompositeVertex &c, float wa, float wb, float wc)
{
    static_assert(sizeof(GfxWaterCompositeVertex) == sizeof(float) * 27,
                  "water vertex interpolation expects a packed float stream");
    GfxWaterCompositeVertex result = {};
    constexpr size_t kWaterVertexFloatCount = sizeof(result) / sizeof(float);
    float a_values[kWaterVertexFloatCount];
    float b_values[kWaterVertexFloatCount];
    float c_values[kWaterVertexFloatCount];
    float result_values[kWaterVertexFloatCount];
    std::memcpy(a_values, &a, sizeof(a_values));
    std::memcpy(b_values, &b, sizeof(b_values));
    std::memcpy(c_values, &c, sizeof(c_values));
    for (size_t index = 0; index < kWaterVertexFloatCount; ++index) {
        result_values[index] = a_values[index] * wa + b_values[index] * wb +
                               c_values[index] * wc;
    }
    std::memcpy(&result, result_values, sizeof(result_values));
    return result;
}

static std::vector<GfxWaterCompositeVertex> gfx_water_tessellate_mesh(
    const GfxWaterCompositeVertex *vertices, size_t vertex_count)
{
    /* Wallace's reference renderer uses a dense water mesh because vertex
     * displacement cannot change a triangle silhouette. Sixteen barycentric
     * subdivisions keep that property on the large legacy two-triangle sea
     * without requiring a new scene mesh asset. */
    constexpr size_t kWaterMeshSubdivisions = 16;
    const size_t triangles = vertex_count / 3;
    std::vector<GfxWaterCompositeVertex> tessellated;
    tessellated.reserve(triangles * kWaterMeshSubdivisions *
                        kWaterMeshSubdivisions * 3);

    const auto lattice_index = [](size_t row, size_t column) {
        /* Rows are indexed by the barycentric weight of vertex B. */
        return row * (kWaterMeshSubdivisions + 1) -
               row * (row - 1) / 2 + column;
    };

    for (size_t triangle = 0; triangle < triangles; ++triangle) {
        const GfxWaterCompositeVertex *source = vertices + triangle * 3;
        std::vector<GfxWaterCompositeVertex> lattice(
            (kWaterMeshSubdivisions + 1) * (kWaterMeshSubdivisions + 2) / 2);
        for (size_t row = 0; row <= kWaterMeshSubdivisions; ++row) {
            for (size_t column = 0;
                 column + row <= kWaterMeshSubdivisions; ++column) {
                const float wb = (float)row / (float)kWaterMeshSubdivisions;
                const float wc = (float)column / (float)kWaterMeshSubdivisions;
                const float wa = 1.0f - wb - wc;
                lattice[lattice_index(row, column)] =
                    gfx_water_interpolate_vertex(source[0], source[1], source[2],
                                                  wa, wb, wc);
            }
        }
        for (size_t row = 0; row < kWaterMeshSubdivisions; ++row) {
            for (size_t column = 0;
                 column + row < kWaterMeshSubdivisions; ++column) {
                const GfxWaterCompositeVertex &p00 =
                    lattice[lattice_index(row, column)];
                const GfxWaterCompositeVertex &p10 =
                    lattice[lattice_index(row + 1, column)];
                const GfxWaterCompositeVertex &p01 =
                    lattice[lattice_index(row, column + 1)];
                tessellated.push_back(p00);
                tessellated.push_back(p10);
                tessellated.push_back(p01);
                if (column + row + 1 < kWaterMeshSubdivisions) {
                    const GfxWaterCompositeVertex &p11 =
                        lattice[lattice_index(row + 1, column + 1)];
                    tessellated.push_back(p10);
                    tessellated.push_back(p11);
                    tessellated.push_back(p01);
                }
            }
        }
    }
    return tessellated;
}

/* Optional integration probe for the Dam/ship occluder fixture. Read a small
 * deterministic depth grid from the opaque scene copy, rather than claiming
 * that a water tint proves foreground geometry survived the composite. The
 * probe is disabled for normal play and restores the framebuffer bindings
 * before the actual water draw continues. */
static void gfx_opengl_trace_water_occluder_fixture(
    GLint viewport_x, GLint viewport_y, GLint viewport_width,
    GLint viewport_height)
{
    const char *fixture = getenv("GE_WATER_TEST_OCCLUDER");
    if (fixture == nullptr || fixture[0] == '\0' || water_scene_fbo == 0 ||
        viewport_width <= 0 || viewport_height <= 0) {
        return;
    }

    static unsigned int trace_events;
    if (trace_events >= 4 || getenv("GE_WATER_TRACE") == nullptr) {
        return;
    }

    GLint old_draw_framebuffer = 0;
    GLint old_read_framebuffer = 0;
    GLint old_draw_buffer = GL_BACK;
    GLint old_read_buffer = GL_BACK;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
    old_draw_buffer = gfx_opengl_get_draw_buffer();
    glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, water_scene_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    float minimum_depth = 1.0f;
    float maximum_depth = 0.0f;
    unsigned int valid_samples = 0;
    for (int row = 0; row < 16; ++row) {
        for (int column = 0; column < 16; ++column) {
            const GLint sample_x = viewport_x +
                (column * std::max(viewport_width - 1, 0)) / 15;
            const GLint sample_y = viewport_y +
                (row * std::max(viewport_height - 1, 0)) / 15;
            float depth = 1.0f;
            glReadPixels(sample_x, sample_y, 1, 1, GL_DEPTH_COMPONENT,
                         GL_FLOAT, &depth);
            if (std::isfinite(depth) && depth >= 0.0f && depth <= 1.0f) {
                minimum_depth = std::min(minimum_depth, depth);
                maximum_depth = std::max(maximum_depth, depth);
                ++valid_samples;
            }
        }
    }
    const GLenum read_error = glGetError();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)old_draw_framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read_framebuffer);
    gfx_opengl_set_draw_buffer((GLenum)old_draw_buffer);
    glReadBuffer((GLenum)old_read_buffer);

    const bool has_depth_edge =
        read_error == GL_NO_ERROR && valid_samples >= 2 &&
        maximum_depth - minimum_depth > 0.0005f;
    sysLogPrintf(LOG_NOTE,
                 "WATER_OCCLUDER: type=%s depth-mask=%d foreground_depth_difference=%0.6f samples=%u",
                 fixture, has_depth_edge ? 1 : 0,
                 (double)(maximum_depth - minimum_depth), valid_samples);
    ++trace_events;
}

static bool gfx_opengl_draw_water_composite(const struct GfxWaterCompositeVertex *vertices,
                                            size_t vertex_count, int render_tier,
                                            const struct GfxWaterDisturbanceBatch *disturbances,
                                            const struct GfxWaterHeightfieldView *heightfield,
                                            const struct GfxWaterCompositeCamera *camera,
                                            float elapsed_time)
{
    if (vertices == nullptr || vertex_count < 3 || (vertex_count % 3) != 0 ||
        geWaterCompositeTier(render_tier) == GE_WATER_COMPOSITE_DISABLED ||
        current_framebuffer >= framebuffers.size() || camera == nullptr ||
        !std::isfinite(elapsed_time) ||
        !std::isfinite(camera->near_plane) || !std::isfinite(camera->far_plane) ||
        camera->near_plane <= 0.0f || camera->far_plane <= camera->near_plane ||
        camera->viewport_width == 0 || camera->viewport_height == 0) {
        return false;
    }
    if (glBlitFramebuffer == nullptr) {
        return false;
    }

    const Framebuffer &source = framebuffers[current_framebuffer];
    if (source.width == 0 || source.height == 0) {
        return false;
    }
    /* The destination water scene is single-sample. A multisample source is
     * resolved by the colour/depth blit below before it is sampled by the
     * shader; rejecting it here would disable water whenever the normal game
     * framebuffer uses MSAA. */
    const bool resolve_msaa = source.msaa_level > 1;
    const GLint active_viewport_x = std::max<GLint>(0, camera->viewport_x);
    const GLint active_viewport_y = std::max<GLint>(0, camera->viewport_y);
    const GLint active_viewport_width = std::min<GLint>(
        (GLint)source.width - active_viewport_x, (GLint)camera->viewport_width);
    const GLint active_viewport_height = std::min<GLint>(
        (GLint)source.height - active_viewport_y, (GLint)camera->viewport_height);
    if (active_viewport_width <= 0 || active_viewport_height <= 0) {
        return false;
    }

    const std::vector<GfxWaterCompositeVertex> tessellated_vertices =
        gfx_water_tessellate_mesh(vertices, vertex_count);
    if (tessellated_vertices.empty()) {
        return false;
    }
    const GfxWaterCompositeVertex *render_vertices = tessellated_vertices.data();
    const size_t render_vertex_count = tessellated_vertices.size();

    const WaterCompositeGLState saved = gfx_opengl_save_water_state();
    if (!gfx_opengl_ensure_water_scene(source.width, source.height) ||
        !gfx_opengl_compile_water_composite_program()) {
        gfx_opengl_restore_water_state(saved);
        return false;
    }
    const bool heightfield_uploaded =
        gfx_opengl_upload_water_heightfield(heightfield);
    if (getenv("GE_WATER_TRACE") != nullptr &&
        water_heightfield_trace_events < 8) {
        const float render_scale = heightfield_uploaded
            ? (geWaterCompositeTier(render_tier) > GE_WATER_COMPOSITE_ENHANCED
                   ? GE_WATER_HEIGHTFIELD_RENDER_SCALE_REMASTER
                   : GE_WATER_HEIGHTFIELD_RENDER_SCALE_ENHANCED)
            : 0.0f;
        sysLogPrintf(LOG_NOTE,
                     "WATER_HEIGHTFIELD: uploaded=%d unit=%d scale=%0.4f cells=%ux%u",
                     heightfield_uploaded ? 1 : 0,
                     heightfield_uploaded ? water_heightfield_texture_unit : 0,
                     (double)render_scale,
                     heightfield != nullptr ? heightfield->width : 0U,
                     heightfield != nullptr ? heightfield->height : 0U);
        ++water_heightfield_trace_events;
    }
    /* The water shader owns the linear-to-sRGB conversion explicitly.  Do
     * not let an inherited GL_FRAMEBUFFER_SRGB state encode it a second time;
     * the saved state is restored on every exit path below. */
    gfx_opengl_set_framebuffer_srgb(false);
    if (getenv("GE_WATER_TRACE") != nullptr &&
        water_composite_shader_trace_events < 8) {
        sysLogPrintf(LOG_NOTE,
                     "WATER_COMPOSITE_SHADER: profile=%s tier=%d status=ready",
                     gl_glsl_version_str,
                     geWaterCompositeTier(render_tier));
        ++water_composite_shader_trace_events;
    }

    const GLuint source_fbo = current_framebuffer == 0 ? 0 : source.fbo;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, water_scene_fbo);
    glReadBuffer(current_framebuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
    gfx_opengl_set_draw_buffer(GL_COLOR_ATTACHMENT0);
    /* GLES rejects a depth blit between the game's packed depth-stencil
     * renderbuffer and the sampled depth texture used by the water shader.
     * Keep the colour resolve (the visible scene/refraction source) and clear
     * the sampled depth to far instead of letting an invalid blit disable the
     * entire water pass. The final composite still uses the live destination
     * depth buffer for ship/dam occlusion. */
    const bool scene_depth_available = !gl_es && source.has_depth_buffer;
    const GLbitfield water_scene_blit_mask =
        GL_COLOR_BUFFER_BIT |
        (scene_depth_available ? GL_DEPTH_BUFFER_BIT : 0);
    glBlitFramebuffer(active_viewport_x, active_viewport_y,
                      active_viewport_x + active_viewport_width,
                      active_viewport_y + active_viewport_height,
                      active_viewport_x, active_viewport_y,
                      active_viewport_x + active_viewport_width,
                      active_viewport_y + active_viewport_height,
                      water_scene_blit_mask, GL_NEAREST);
    if (!scene_depth_available) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, water_scene_fbo);
        glDepthMask(GL_TRUE);
        if (glClearDepthf != nullptr) {
            glClearDepthf(1.0f);
        } else {
            glClearDepth(1.0);
        }
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    if (glGetError() != GL_NO_ERROR) {
        gfx_opengl_restore_water_state(saved);
        return false;
    }

    gfx_opengl_trace_water_occluder_fixture(
        active_viewport_x, active_viewport_y,
        active_viewport_width, active_viewport_height);

    /* exclude_water: the opaque scene copy is captured before the second
     * water pass, so water cannot recursively sample its own composite. A
     * reflected-camera replay has already populated the planar target when
     * available. Only the failure path uses the old bounded vertical copy;
     * this keeps a driver/FBO failure visible without pretending it is a
     * reflected world render. */
    const bool force_reflection_fallback =
        getenv("GE_WATER_FORCE_REFLECTION_FALLBACK") != nullptr &&
        strcmp(getenv("GE_WATER_FORCE_REFLECTION_FALLBACK"), "1") == 0;
    const bool reflected_camera_target =
        water_reflection_populated && !force_reflection_fallback;
    bool reflection_texture_valid = reflected_camera_target;
    const char *reflection_source = reflected_camera_target
        ? "reflected-camera" : "screen-space-fallback";
    const bool water_reflection_clip_plane = reflected_camera_target;
    if (!reflected_camera_target || !water_reflection_clip_plane) {
        const GLenum reflection_color_filter =
            source.msaa_level > 1 ? GL_NEAREST : GL_LINEAR;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, source_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, water_reflection_fbo);
        glReadBuffer(current_framebuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
        gfx_opengl_set_draw_buffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(active_viewport_x, active_viewport_y,
                          active_viewport_x + active_viewport_width,
                          active_viewport_y + active_viewport_height,
                          active_viewport_x,
                          active_viewport_y + active_viewport_height,
                          active_viewport_x + active_viewport_width,
                          active_viewport_y,
                          GL_COLOR_BUFFER_BIT, reflection_color_filter);
        const GLbitfield reflection_blit_mask =
            (!gl_es && source.has_depth_buffer) ? GL_DEPTH_BUFFER_BIT : 0;
        if (reflection_blit_mask != 0) {
            glBlitFramebuffer(active_viewport_x, active_viewport_y,
                              active_viewport_x + active_viewport_width,
                              active_viewport_y + active_viewport_height,
                              active_viewport_x,
                              active_viewport_y + active_viewport_height,
                              active_viewport_x + active_viewport_width,
                              active_viewport_y,
                              reflection_blit_mask, GL_NEAREST);
        } else {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, water_reflection_fbo);
            glDepthMask(GL_TRUE);
            if (glClearDepthf != nullptr) {
                glClearDepthf(1.0f);
            } else {
                glClearDepth(1.0);
            }
            glClear(GL_DEPTH_BUFFER_BIT);
        }
        if (glGetError() != GL_NO_ERROR) {
            gfx_opengl_restore_water_state(saved);
            water_reflection_populated = false;
            return false;
        }
        reflection_texture_valid = true;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, source_fbo);
    if (water_composite_vbo == 0) {
        glGenBuffers(1, &water_composite_vbo);
        if (water_composite_vbo == 0 || glGetError() == GL_OUT_OF_MEMORY) {
            water_composite_vbo = 0;
            gfx_opengl_restore_water_state(saved);
            return false;
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, water_composite_vbo);
    glBufferData(GL_ARRAY_BUFFER, render_vertex_count * sizeof(*render_vertices),
                 render_vertices, GL_STREAM_DRAW);

    float water_line = 0.0f;
    float water_min_line = 1.0f;
    for (size_t i = 0; i < render_vertex_count; ++i) {
        if (!geWaterCompositeVertexIsValid(render_vertices[i].x, render_vertices[i].y,
                                           render_vertices[i].z, render_vertices[i].w)) {
            continue;
        }
        const float uv_y = geWaterCompositeClipToUv(render_vertices[i].y,
                                                    render_vertices[i].w);
        if (uv_y > water_line) water_line = uv_y;
        if (uv_y < water_min_line) water_min_line = uv_y;
    }
    if (water_line <= 0.0f) {
        gfx_opengl_restore_water_state(saved);
        return false;
    }

    if (getenv("GE_WATER_TRACE") != nullptr &&
        water_composite_geometry_trace_events < 8) {
        sysLogPrintf(LOG_NOTE,
                     "WATER_COMPOSITE_GEOMETRY: uv-min=%0.4f uv-max=%0.4f mirror-line=%0.4f",
                     (double)water_min_line, (double)water_line,
                     (double)water_line);
        ++water_composite_geometry_trace_events;
    }

    glUseProgram(water_composite_program);
    glEnableVertexAttribArray((GLuint)water_composite_position_location);
    glVertexAttribPointer((GLuint)water_composite_position_location, 4, GL_FLOAT,
                          GL_FALSE, sizeof(*render_vertices), nullptr);
    glEnableVertexAttribArray((GLuint)water_composite_water_uv_location);
    glVertexAttribPointer((GLuint)water_composite_water_uv_location, 2, GL_FLOAT,
                          GL_FALSE, sizeof(*render_vertices),
                          (const void *)offsetof(GfxWaterCompositeVertex, water_u));
    glEnableVertexAttribArray((GLuint)water_composite_view_position_location);
    glVertexAttribPointer((GLuint)water_composite_view_position_location, 3, GL_FLOAT,
                          GL_FALSE, sizeof(*render_vertices),
                          (const void *)offsetof(GfxWaterCompositeVertex, view_x));
    glEnableVertexAttribArray((GLuint)water_composite_world_position_location);
    glVertexAttribPointer((GLuint)water_composite_world_position_location, 3, GL_FLOAT,
                          GL_FALSE, sizeof(*render_vertices),
                          (const void *)offsetof(GfxWaterCompositeVertex, world_x));
    glEnableVertexAttribArray((GLuint)water_composite_surface_plane_location);
    glVertexAttribPointer((GLuint)water_composite_surface_plane_location, 4, GL_FLOAT,
                          GL_FALSE, sizeof(*render_vertices),
                          (const void *)offsetof(GfxWaterCompositeVertex, surface_plane));
    glEnableVertexAttribArray((GLuint)water_composite_view_plane_location);
    glVertexAttribPointer((GLuint)water_composite_view_plane_location, 4, GL_FLOAT,
                          GL_FALSE, sizeof(*render_vertices),
                          (const void *)offsetof(GfxWaterCompositeVertex, view_plane));
    glEnableVertexAttribArray((GLuint)water_composite_tangent_location);
    glVertexAttribPointer((GLuint)water_composite_tangent_location, 3, GL_FLOAT,
                          GL_FALSE, sizeof(*render_vertices),
                          (const void *)offsetof(GfxWaterCompositeVertex, tangent));
    glEnableVertexAttribArray((GLuint)water_composite_bitangent_location);
    glVertexAttribPointer((GLuint)water_composite_bitangent_location, 3, GL_FLOAT,
                          GL_FALSE, sizeof(*render_vertices),
                          (const void *)offsetof(GfxWaterCompositeVertex, bitangent));
    glEnableVertexAttribArray((GLuint)water_composite_surface_id_location);
    glVertexAttribPointer((GLuint)water_composite_surface_id_location, 1, GL_FLOAT,
                          GL_FALSE, sizeof(*render_vertices),
                          (const void *)offsetof(GfxWaterCompositeVertex, surface_id));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, water_scene_color);
    glUniform1i(water_composite_scene_location, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, water_scene_depth);
    glUniform1i(water_composite_depth_location, 1);
    glUniform1f(water_composite_scene_depth_valid_location,
                scene_depth_available ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, water_reflection_color);
    glUniform1i(water_composite_reflection_location, 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, water_material_base_texture);
    glUniform1i(water_composite_base_location, 3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, water_material_normal_texture);
    glUniform1i(water_composite_normal_location, 4);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, water_material_height_texture);
    if (water_composite_height_location >= 0) {
        glUniform1i(water_composite_height_location, 5);
    }
    glUniform2f(water_composite_height_texel_location,
                1.0f / 64.0f, 1.0f / 64.0f);
    const int heightfield_sampler =
        heightfield_uploaded && water_heightfield_texture_unit >= 0
            ? water_heightfield_texture_unit : 0;
    glActiveTexture(GL_TEXTURE0 + heightfield_sampler);
    if (heightfield_uploaded) {
        glBindTexture(GL_TEXTURE_2D, water_heightfield_texture);
    } else {
        /* Unit zero is always valid. Scale is zero on this path, but binding
         * a known texture keeps the sampler well-defined on strict drivers. */
        glBindTexture(GL_TEXTURE_2D, water_scene_color);
    }
    if (water_composite_heightfield_location >= 0) {
        glUniform1i(water_composite_heightfield_location, heightfield_sampler);
    }
    if (water_composite_heightfield_scale_location >= 0) {
        const float heightfield_scale = heightfield_uploaded
            ? (geWaterCompositeTier(render_tier) > GE_WATER_COMPOSITE_ENHANCED
                   ? GE_WATER_HEIGHTFIELD_RENDER_SCALE_REMASTER
                   : GE_WATER_HEIGHTFIELD_RENDER_SCALE_ENHANCED)
            : 0.0f;
        glUniform1f(water_composite_heightfield_scale_location,
                    heightfield_scale);
    }
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(water_composite_time_location, elapsed_time);
    glUniform1f(water_composite_tier_location, (float)geWaterCompositeTier(render_tier));
    glUniform2f(water_composite_inverse_viewport_location,
                1.0f / (float)source.width, 1.0f / (float)source.height);
    glUniform4f(water_composite_scene_viewport_location,
                (float)camera->viewport_x, (float)camera->viewport_y,
                (float)camera->viewport_width, (float)camera->viewport_height);
    glUniform4f(water_composite_viewport_location,
                (float)camera->viewport_x, (float)camera->viewport_y,
                (float)camera->viewport_width, (float)camera->viewport_height);
    glUniform4f(water_composite_framebuffer_viewport_location,
                0.0f, 0.0f, (float)source.width, (float)source.height);
    glUniformMatrix4fv(water_composite_inverse_projection_location, 1, GL_FALSE,
                       camera->inverse_projection);
    glUniformMatrix4fv(water_composite_world_to_clip_location, 1, GL_FALSE,
                       camera->world_to_clip);
    glUniformMatrix4fv(water_composite_world_to_view_location, 1, GL_FALSE,
                       camera->world_to_view);
    glUniformMatrix4fv(water_composite_forward_projection_location, 1, GL_FALSE,
                       camera->forward_projection);
    glUniformMatrix3fv(water_composite_normal_matrix_location, 1, GL_FALSE,
                       camera->normal_matrix);
    glUniform4f(water_composite_depth_params_location,
                camera->near_plane, camera->far_plane,
                camera->depth_convention ? 1.0f : 0.0f,
                camera->invert_y ? 1.0f : 0.0f);
    const float reflection_grid_x = reflected_camera_target
        ? (float)std::max<uint32_t>(camera->reflection_atlas_columns, 1)
        : 1.0f;
    const float reflection_grid_y = reflected_camera_target
        ? (float)std::max<uint32_t>(camera->reflection_atlas_rows, 1)
        : 1.0f;
    glUniform2f(water_composite_reflection_atlas_location,
                reflection_grid_x, reflection_grid_y);
    glUniform1f(water_composite_absorption_location, 0.035f);
    glUniform1f(water_composite_displacement_location,
                geWaterCompositeTier(render_tier) > GE_WATER_COMPOSITE_ENHANCED
                    ? 0.004f : 0.0025f);
    /* No disturbance is a valid state. A permanent line-derived ripple makes
     * every water surface look like it is being struck at one fixed point and
     * hides whether boat wakes or dam impacts were actually submitted. */
    const GfxWaterDisturbanceBatch empty_batch = {};
    const GfxWaterDisturbanceBatch &active_batch =
        disturbances != nullptr ? *disturbances : empty_batch;
    const GLint disturbance_count = (GLint)std::min(
        (uint32_t)GFX_WATER_MAX_DISTURBANCES, active_batch.count);
    glUniform4fv(water_composite_disturbances_location, disturbance_count,
                 reinterpret_cast<const GLfloat *>(active_batch.disturbances));
    glUniform1i(water_composite_disturbance_count_location, disturbance_count);
    glUniform1f(water_composite_wave_length_location,
                GE_WATER_DEFAULT_WAVELENGTH);
    glUniform1f(water_composite_wave_amplitude_location,
                GE_WATER_DEFAULT_AMPLITUDE);
    glUniform1f(water_composite_wave_speed_location, GE_WATER_DEFAULT_SPEED);
    glUniform2f(water_composite_wave_direction_location,
                GE_WATER_DEFAULT_DIRECTION_X, GE_WATER_DEFAULT_DIRECTION_Y);
    glUniform1f(water_composite_contact_start_location,
                GE_WATER_CONTACT_START);
    glUniform1f(water_composite_contact_band_location,
                geWaterCompositeContactBand(render_tier));
    const unsigned int feature_mask = gfx_water_diagnostic_feature_mask();
    const bool reflection_only = gfx_water_diagnostic_reflection_only();
    glUniform4f(water_composite_feature_toggles_location,
                reflection_only
                    ? 2.0f
                    : (geWaterFeatureEnabled(feature_mask, GE_WATER_FEATURE_REFLECTION)
                           ? 1.0f : 0.0f),
                geWaterFeatureEnabled(feature_mask, GE_WATER_FEATURE_REFRACTION)
                    ? 1.0f : 0.0f,
                geWaterFeatureEnabled(feature_mask, GE_WATER_FEATURE_NORMALS)
                    ? 1.0f : 0.0f,
                geWaterFeatureEnabled(feature_mask, GE_WATER_FEATURE_FOAM)
                    ? 1.0f : 0.0f);
    glUniform1f(water_composite_reflection_valid_location,
                reflection_texture_valid ? 1.0f : 0.0f);
    glUniform3f(water_composite_light_direction_location,
                -0.35f, 0.78f, 0.42f);
    glUniform3f(water_composite_light_color_location,
                1.0f, 0.86f, 0.62f);
    glUniform1f(water_composite_shininess_location,
                geWaterCompositeTier(render_tier) > GE_WATER_COMPOSITE_ENHANCED
                    ? 96.0f : 64.0f);
    if (getenv("GE_WATER_TRACE") != nullptr &&
        water_composite_shader_trace_events < 8) {
        sysLogPrintf(LOG_NOTE,
                     "WATER_FEATURES: mask=0x%x reflection-valid=%d reflection-source=%s transfer=linear-to-srgb bump-env=%d",
                     feature_mask,
                     reflection_texture_valid ? 1 : 0,
                     reflection_source,
                     getenv("GE_DISABLE_BUMP_MAPS") == nullptr ? 1 : 0);
    }
    /* The scene copy is opaque-world colour/depth. Let the real depth buffer
     * reject water fragments behind a ship or dam wall before the shader can
     * apply a strong grazing-angle reflection over that geometry. The sampled
     * depth remains useful for the contact/foam policy, but it is not a
     * substitute for the fixed-function depth test at the occluder boundary. */
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    if (glBlendFuncSeparate != nullptr) {
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    if (glBlendEquationSeparate != nullptr) {
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    }
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)render_vertex_count);

    const GLenum error = glGetError();
    gfx_opengl_restore_water_state(saved);
    water_reflection_populated = false;
    return error == GL_NO_ERROR;
}

static int gfx_opengl_get_max_anisotropy_level() {
	GLfloat max_aniso_level;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_aniso_level);
	return (int)max_aniso_level;
}

static void gfx_opengl_set_anisotropy_level(int level) {
	current_anisotropy_level = level;
}

static void gfx_opengl_destroy(void) {
    gfx_opengl_release_shadow_resources();
    gfx_opengl_release_water_resources();
    if (opengl_vbo != 0) {
        glDeleteBuffers(1, &opengl_vbo);
        opengl_vbo = 0;
    }
    if (opengl_vao != 0) {
        glDeleteVertexArrays(1, &opengl_vao);
        opengl_vao = 0;
    }
    shader_program_pool.clear();
    framebuffers.clear();
}

struct GfxRenderingAPI gfx_opengl_api = {
    gfx_opengl_get_name,
    gfx_opengl_get_max_texture_size,
    gfx_opengl_get_clip_parameters,
    gfx_opengl_unload_shader,
    gfx_opengl_load_shader,
    gfx_opengl_create_and_load_new_shader,
    gfx_opengl_lookup_shader,
    gfx_opengl_shader_get_info,
    gfx_opengl_clear_shaders,
    gfx_opengl_new_texture,
    gfx_opengl_select_texture,
    gfx_opengl_upload_texture,
    gfx_opengl_upload_bump_texture,
    gfx_opengl_set_bump_texture,
    gfx_opengl_set_sampler_parameters,
    gfx_opengl_set_depth_mode,
    gfx_opengl_set_depth_range,
    gfx_opengl_set_viewport,
    gfx_opengl_set_scissor,
    gfx_opengl_set_use_alpha,
    gfx_opengl_draw_triangles,
    gfx_opengl_init,
    gfx_opengl_on_resize,
    gfx_opengl_start_frame,
    gfx_opengl_end_frame,
    gfx_opengl_finish_render,
    gfx_opengl_create_framebuffer,
    gfx_opengl_update_framebuffer_parameters,
    gfx_opengl_start_draw_to_framebuffer,
    gfx_opengl_copy_framebuffer,
    gfx_opengl_clear_framebuffer,
    gfx_opengl_resolve_msaa_color_buffer,
    gfx_opengl_get_framebuffer_texture_id,
    gfx_opengl_select_texture_fb,
    gfx_opengl_delete_texture,
    gfx_opengl_set_texture_filter,
    gfx_opengl_get_texture_filter,
    gfx_opengl_set_mipmap_filter,
    gfx_opengl_set_anisotropy_level,
    gfx_opengl_get_max_anisotropy_level,
    gfx_opengl_draw_water_composite,
    gfx_opengl_begin_reflected_camera,
    gfx_opengl_end_reflected_camera,
    gfx_opengl_capture_postfx_depth,
    gfx_opengl_get_postfx_depth_texture,
    gfx_opengl_destroy,
    gfx_opengl_begin_shadow_scene_capture,
    gfx_opengl_end_shadow_scene_capture,
    gfx_opengl_render_shadow_map
};
