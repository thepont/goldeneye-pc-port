#include "water_render_contract_test_support.h"

#include <iostream>
#include <string>

using geWaterContractTest::TestState;
using geWaterContractTest::contains;
using geWaterContractTest::read_text;
namespace fs = geWaterContractTest::fs;

int main()
{
#ifndef GE007_SOURCE_DIR
    std::cerr << "RED: shadow-mapping contract has no source root\n";
    return 1;
#else
    const fs::path root = GE007_SOURCE_DIR;
    const std::string api = read_text(
        root / "port" / "fast3d" / "gfx_rendering_api.h");
    const std::string pc = read_text(
        root / "port" / "fast3d" / "gfx_pc.cpp");
    const std::string shadow_policy = read_text(
        root / "port" / "fast3d" / "shadow_mapping_policy.h");
    const std::string opengl = read_text(
        root / "port" / "fast3d" / "gfx_opengl.cpp");
    const std::string shader_options = read_text(
        root / "port" / "fast3d" / "gfx_cc.h");

    TestState tests;
    tests.expect(contains(api, "render_shadow_map") &&
                     contains(api, "begin_shadow_scene_capture"),
                 "rendering backend must expose a shadow-map pass and validity boundary");
    tests.expect(contains(pc, "g_shadow_capture_active") &&
                     contains(pc, "g_shadow_capture_vertices") &&
                     contains(pc, "render_shadow_map") &&
                     contains(pc, "gfx_sp_material_uses_alpha()") &&
                     contains(pc, "geShadowCasterEligible") &&
                     contains(pc, "geShadowReceiverEligible") &&
                     contains(pc, "gfx_sp_shadow_material_has_alpha_cutout"),
                 "frontend must replay scene geometry into a bounded shadow-caster stream");
    tests.expect(contains(shader_options, "SHADER_OPT_SHADOW_MAPPING"),
                 "shadow-enabled shader variants must have a distinct cache key");
    tests.expect(contains(opengl, "uShadowMap") &&
                     contains(opengl, "uShadowLightSpace") &&
                     contains(opengl, "uShadowMappingEnabled") &&
                     contains(opengl, "shadowPcf") &&
                     contains(opengl, "shadowBias"),
                 "fragment shader must project, bias, and filter light-space depth comparisons");
    tests.expect(contains(opengl, "GL_DEPTH_ATTACHMENT") &&
                     contains(opengl, "GL_NONE") &&
                     contains(opengl, "GL_DEPTH_BUFFER_BIT"),
                 "shadow pass must render into a depth-only framebuffer");
    tests.expect(contains(opengl, "GL_TEXTURE3") &&
                     contains(opengl, "advancedDisplayToLinear") &&
                     contains(opengl, "advancedLinearToDisplay"),
                 "shadowed lighting must use its own texture unit and preserve linear-light output");
    tests.expect(contains(pc, "GFX_RENDER_ENHANCED") &&
                     contains(pc, "g_shadow_capture_active"),
                 "shadow capture must be opt-in to enhanced rendering and isolated from recursive replay");
    tests.expect(contains(pc, "if (g_shadow_capture_active) {\n                    break;") &&
                     contains(pc, "g_shadow_saved_water_material_enabled"),
                 "shadow replay must exclude water compositing commands and restore frontend material state");
    tests.expect(contains(pc, "geShadowReplayAdvanceIgnoredCommand(cmd, opcode)") &&
                     contains(shadow_policy,
                              "cursor += skip.cursor_advance;") &&
                     contains(shadow_policy, "case G_IMAGERECT_EXT:") &&
                     contains(shadow_policy, "case G_FILLRECT_WIDE_EXT:") &&
                     contains(shadow_policy, "case G_CLEAR_DEPTH_EXT:"),
                 "shadow replay must use complete RDP packet widths including depth clears and extended rectangles");
    tests.expect(contains(opengl, "gfx_opengl_ensure_shadow_resources") &&
                     contains(opengl, "glBufferData(GL_ARRAY_BUFFER") &&
                     contains(opengl, "glDrawArrays(GL_TRIANGLES"),
                 "backend must rasterize the captured caster stream into the depth target");
    tests.expect(contains(opengl, "shadow_resources_failed") &&
                     contains(opengl, "gfx_opengl_restore_water_state(saved)"),
                 "unsupported depth targets must fail closed and restore caller GL state");

    if (tests.failures == 0) {
        std::cerr << "shadow-mapping contracts passed\n";
        return 0;
    }
    std::cerr << tests.failures << " shadow-mapping contract(s) are red\n";
    return 1;
#endif
}
