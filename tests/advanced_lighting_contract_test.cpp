/*
 * Red contracts for the next advanced-lighting slice.
 *
 * LearnOpenGL's Advanced Lighting chapter replaces the fragile Phong
 * reflection-vector test with Blinn-Phong's normalized halfway vector. The
 * The source checks are intentionally narrow: they require the policy and
 * its renderer call site, rather than accepting a comment or a test-only
 * formula. Keeping these contracts in the suite prevents later renderer
 * changes from silently regressing the advanced path back to diffuse-only
 * tinting.
 */
#include "water_render_contract_test_support.h"

#include <filesystem>
#include <iostream>
#include <string>

using geWaterContractTest::TestState;
using geWaterContractTest::between;
using geWaterContractTest::contains;
using geWaterContractTest::contains_any;
using geWaterContractTest::read_text;
namespace fs = geWaterContractTest::fs;

static void test_blinn_phong_policy(TestState &tests,
                                    std::string_view lighting_header)
{
    tests.expect(contains_any(lighting_header,
                              {"geBlinnPhong", "halfwayDir",
                               "halfway_vector", "specular_power"}),
                 "advanced lighting must expose a Blinn-Phong halfway-vector policy");
    tests.expect(contains_any(lighting_header,
                              {"shininess", "specular_exponent",
                               "material_shininess"}),
                 "advanced lighting must carry a bounded material shininess input");
    tests.expect(contains_any(lighting_header,
                              {"max(dot(normal, halfway", "max(dot(n, halfway",
                               "specular ="}) ||
                     (contains(lighting_header, "normal_halfway") &&
                      contains(lighting_header, "std::clamp") &&
                      contains(lighting_header, "std::pow")),
                 "Blinn-Phong specular response must clamp the normal/halfway dot product");
}

static void test_renderer_uses_view_dependent_specular(TestState &tests,
                                                        std::string_view pc_source,
                                                        std::string_view lighting_source,
                                                        std::string_view policy_source)
{
    tests.expect(contains_any(pc_source,
                              {"geBlinnPhongSpecular",
                               "geDynamicLightSpecular",
                               "geAdvancedLightSpecular"}) &&
                     contains_any(pc_source,
                                  {"view_direction", "viewDirection",
                                   "view_position", "camera_position"}),
                 "dynamic-light rendering must evaluate a view-dependent specular term");
    tests.expect(contains_any(lighting_source,
                              {"halfwayDir", "halfway_vector",
                               "halfway"}) ||
                     contains_any(policy_source,
                                  {"halfwayDir", "halfway_vector",
                                   "halfway"}),
                 "the renderer must use the normalized halfway vector instead of only diffuse lighting");
}

static void test_normal_map_feeds_lighting(TestState &tests,
                                           std::string_view compatibility_shader)
{
    tests.expect(contains(compatibility_shader, "bumpNormal") &&
                     contains_any(compatibility_shader,
                                  {"dot(bumpNormal", "normal * halfway",
                                   "specular"}),
                 "per-fragment normal-map normals must feed the lighting equation, not only a diffuse tint");
}

static void test_linear_lighting_boundary(TestState &tests,
                                          std::string_view compatibility_shader)
{
    tests.expect(contains_any(compatibility_shader,
                              {"GL_FRAMEBUFFER_SRGB", "linearToSrgb",
                               "linear_to_srgb", "advancedLinearToDisplay",
                               "advancedDisplayToLinear", "pow(linear"}),
                 "advanced lighting must define one explicit linear-to-display transfer boundary");
}

static void test_dynamic_light_stays_linear(TestState &tests,
                                            std::string_view pc_source,
                                            std::string_view compatibility_shader)
{
    const std::string_view vertex_path = between(
        pc_source, "static void gfx_sp_vertex",
        "static void gfx_sp_modify_vertex");
    tests.expect(contains(vertex_path, "linear_dynamic_light") &&
                     contains(vertex_path, "geDynamicLightContribution"),
                 "dynamic light transport must retain a separate linear vertex contribution");
    tests.expect(!contains(vertex_path, "r += contribution[0] * 255.0f") &&
                     !contains(vertex_path, "g += contribution[1] * 255.0f") &&
                     !contains(vertex_path, "b += contribution[2] * 255.0f"),
                 "linear dynamic light must not be folded into the 8-bit display-space vertex colour");
    tests.expect(contains(compatibility_shader, "aLinearDynamicLight") &&
                     contains(compatibility_shader, "vLinearDynamicLight") &&
                     contains(compatibility_shader, "uLinearDynamicLightingEnabled"),
                 "the compatibility shader must receive a separate linear dynamic-light stream");
    tests.expect(contains(compatibility_shader, "advancedDisplayToLinear(texel.rgb)") &&
                     contains(compatibility_shader, "vLinearDynamicLight") &&
                     contains(compatibility_shader, "advancedLinearToDisplay"),
                 "dynamic light must be accumulated after sRGB decode and encoded once at the output boundary");
}

static void test_light_visibility_and_mode_policy(TestState &tests,
                                                  std::string_view pc_source,
                                                  std::string_view lighting_source,
                                                  std::string_view video_apply)
{
    tests.expect(contains_any(pc_source,
                              {"shadowMap", "shadow_factor", "lightVisibility",
                               "occluderVisibility"}) ||
                     contains_any(lighting_source,
                                  {"geDynamicLightVisibility", "lightVisibility",
                                   "occluderVisibility"}),
                 "dynamic lights must have a tested visibility/occlusion boundary instead of lighting through walls");
    tests.expect(contains(video_apply,
                          "gfx_set_dynamic_lighting(!disable_dynamic_lighting && cfgDynamicLighting &&") ||
                     contains(video_apply, "geWorldLightingEnabledForRenderMode"),
                 "dynamic-light policy must explicitly cover Original versus Enhanced/Remaster modes");
    tests.expect(contains(pc_source, "geWorldLightingEnabledForRenderMode"),
                 "renderer-side dynamic-light submission must enforce the render-mode policy too");
}

static void test_water_direct_highlight(TestState &tests,
                                        std::string_view water_shader)
{
    tests.expect(contains_any(water_shader,
                              {"uWaterShininess", "waterShininess",
                               "waterDirectSpecular", "directSpecular"}) &&
                     contains_any(water_shader,
                                  {"halfwayDir", "halfway_vector",
                                   "waterHalfway", "normalize(lightDir + viewDir)"}),
                 "water must expose a material-controlled direct Blinn-Phong highlight in addition to Fresnel");
}

int main()
{
#ifndef GE007_SOURCE_DIR
    std::cerr << "RED: advanced-lighting contract has no source root\n";
    return 1;
#else
    const fs::path root = GE007_SOURCE_DIR;
    const std::string lighting = read_text(
        root / "port" / "fast3d" / "dynamic_lighting.h");
    const std::string advanced_policy = read_text(
        root / "port" / "fast3d" / "advanced_lighting_policy.h");
    const std::string pc = read_text(root / "port" / "fast3d" / "gfx_pc.cpp");
    const std::string opengl = read_text(
        root / "port" / "fast3d" / "gfx_opengl.cpp");
    const std::string video = read_text(root / "port" / "src" / "video.c");
    const std::string video_apply = std::string(between(
        video, "static void videoApplyImageOptions(void)",
        "static void videoApplyTexFilter"));
    const std::string compatibility = std::string(between(
        opengl, "static struct ShaderProgram* gfx_opengl_create_and_load_new_shader",
        "/* Enhanced water"));
    const std::string water = std::string(between(
        opengl, "static bool gfx_opengl_compile_water_composite_program(void)",
        "static bool gfx_opengl_ensure_water_scene"));

    TestState tests;
    tests.expect(!lighting.empty(),
                 "advanced-lighting contract could not read dynamic lighting policy");
    tests.expect(!advanced_policy.empty(),
                 "advanced-lighting contract could not read Blinn-Phong policy");
    tests.expect(!pc.empty(),
                 "advanced-lighting contract could not read the PC renderer");
    tests.expect(!opengl.empty(),
                 "advanced-lighting contract could not read the OpenGL renderer");
    tests.expect(!video.empty(),
                 "advanced-lighting contract could not read video mode policy");

    test_blinn_phong_policy(tests, advanced_policy);
    test_renderer_uses_view_dependent_specular(tests, pc, lighting,
                                               advanced_policy);
    test_normal_map_feeds_lighting(tests, compatibility);
    test_linear_lighting_boundary(tests, compatibility);
    test_dynamic_light_stays_linear(tests, pc, compatibility);
    test_light_visibility_and_mode_policy(tests, pc, lighting, video_apply);
    test_water_direct_highlight(tests, water);

    if (tests.failures == 0) {
        std::cerr << "advanced-lighting contracts passed\n";
        return 0;
    }
    std::cerr << tests.failures
              << " advanced-lighting contract(s) are red\n";
    return 1;
#endif
}
