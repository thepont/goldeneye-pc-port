/* Red-first contracts for live shader/capture behavior that pure water math
 * cannot see. These checks are intentionally narrow: they protect the
 * reflection validity gate, shared contact constants, and truthful fallback
 * diagnostics without requiring a GL context. */
#include "water_render_contract_test_support.h"

#include <string_view>

using geWaterContractTest::TestState;
using geWaterContractTest::between;
using geWaterContractTest::contains;
using geWaterContractTest::read_text;
namespace fs = geWaterContractTest::fs;

static void test_reflection_validity_gate(TestState &tests,
                                          std::string_view opengl)
{
    const std::size_t weight = opengl.find("reflectionWeight");
    const std::size_t next_statement = opengl.find(";", weight);
    const std::string_view weight_expression =
        weight == std::string_view::npos || next_statement == std::string_view::npos
            ? std::string_view{}
            : opengl.substr(weight, next_statement - weight);
    tests.expect(contains(weight_expression, "uWaterReflectionValid"),
                 "invalid reflection targets must contribute zero Fresnel weight");
    tests.expect(contains(opengl, "reflectionWeight = clamp") &&
                     contains(opengl, "uWaterReflectionValid"),
                 "reflection validity must gate both colour and energy accounting");
}

static void test_contact_uniform_policy(TestState &tests,
                                        std::string_view opengl)
{
    tests.expect(contains(opengl, "water_composite_contact_start_location,\n"
                                 "                GE_WATER_CONTACT_START") ||
                     contains(opengl, "water_composite_contact_start_location, GE_WATER_CONTACT_START"),
                 "live contact start must use the shared water policy constant");
    tests.expect(contains(opengl, "geWaterCompositeContactBand(render_tier)"),
                 "live contact band must use the tier policy instead of a second set of literals");
}

static void test_truthful_reflection_trace(TestState &tests,
                                           std::string_view pc,
                                           std::string_view opengl)
{
    tests.expect(contains(pc, "reflection_source") &&
                     contains(pc, "reflected-camera") &&
                     contains(pc, "screen-space-fallback"),
                 "frontend water trace must identify the actual reflection path");
    tests.expect(contains(opengl, "reflection_source") &&
                     contains(opengl, "reflected-camera") &&
                     contains(opengl, "screen-space-fallback"),
                 "backend water trace must distinguish replay from fallback");
    tests.expect(contains(opengl, "reflection_texture_valid") &&
                     contains(opengl, "reflection_grid"),
                 "fallback reflection copies must remain valid and use a full-texture sample grid");
}

static void test_world_anchored_heightfield(TestState &tests,
                                            std::string_view pc,
                                            std::string_view opengl)
{
    tests.expect(contains(pc, "geWaterSurfaceUvFromWorld"),
                 "world water surfaces must derive UVs from world coordinates, not the camera position");
    tests.expect(contains(opengl, "vec2 waveUv = fract(vWaterUv);"),
                 "water wave coordinates must remain anchored to the world-space surface stream");
    tests.expect(!contains(opengl, "vWorldPosition.xz * 0.00001"),
                 "the fragment wave coordinate must not add a second camera-relative/material-relative drift term");
}

static void test_heightfield_normal_authority(TestState &tests,
                                               std::string_view opengl)
{
    tests.expect(contains(opengl, "sampleHeightfieldNormalBilinear") &&
                     contains(opengl, ".g") && contains(opengl, ".b"),
                 "the uploaded heightfield normal channels must be sampled by the water shader");
    tests.expect(contains(opengl, "dynamicNormal") &&
                     contains(opengl, "waterTangentNormal") &&
                     contains(opengl, "uWaterHeightfieldScale"),
                 "heightfield normals must contribute to the final water normal");
    tests.expect(contains(opengl, "dynamicNormalValid") &&
                     contains(opengl, "mix(vec3(0.0, 1.0, 0.0), dynamicNormal"),
                 "unsupported heightfield uploads must not sample scene colour as a normal map");
}

static void test_water_mesh_resolution(TestState &tests,
                                       std::string_view opengl)
{
    const std::string_view mesh = between(
        opengl, "gfx_water_tessellate_mesh", "gfx_opengl_trace_water_occluder_fixture");
    tests.expect(contains(mesh, "kWaterMeshSubdivisions = 16"),
                 "the water mesh must be dense enough for heightfield displacement to affect its silhouette");
}

static void test_contact_foam_authority(TestState &tests,
                                        std::string_view opengl)
{
    const std::size_t foam = opengl.find("float foam = clamp");
    const std::size_t statement_end = opengl.find(";", foam);
    const std::string_view foam_expression =
        foam == std::string_view::npos || statement_end == std::string_view::npos
            ? std::string_view{}
            : opengl.substr(foam, statement_end - foam);
    tests.expect(contains(foam_expression, "proximity"),
                 "foam must require contact proximity as well as an arbitrary depth edge");
    tests.expect(contains(opengl, "nearestNeighbourDepth") &&
                     contains(opengl, "uWaterContactStart"),
                 "contact foam must classify the nearest neighbour against the water plane");
}

static void test_depth_copy_capability(TestState &tests,
                                       std::string_view opengl)
{
    const std::string_view scene_copy = between(
        opengl, "const GLbitfield water_scene_blit_mask", "glBlitFramebuffer");
    tests.expect(contains(scene_copy, "scene_depth_available"),
                 "scene depth must be blitted only when the source framebuffer owns depth");
    const std::string_view reflection_copy = between(
        opengl, "const GLbitfield reflection_blit_mask", "glBlitFramebuffer");
    tests.expect(contains(reflection_copy, "source.has_depth_buffer"),
                 "fallback reflection depth must follow the source framebuffer capability");
}

static void test_disturbance_single_application(TestState &tests,
                                                std::string_view pc,
                                                std::string_view opengl)
{
    tests.expect(contains(pc, "g_water_heightfield.add_impulse") &&
                     contains(opengl, "proceduralRippleWeight"),
                 "water disturbances must have one authoritative heightfield path and an explicit fallback path");
    tests.expect(contains(opengl, "ripple += waterRipple") &&
                     contains(opengl, "proceduralRippleWeight"),
                 "the procedural ripple fallback must be disabled when the uploaded heightfield is valid");
}

static void test_environment_reflection_fallback(TestState &tests,
                                                 std::string_view opengl)
{
    tests.expect(contains(opengl, "waterEnvironmentReflection") &&
                     contains(opengl, "reflectionWithEnvironment"),
                 "reflection rays that miss the scene must resolve to a bounded sky/environment colour");
    tests.expect(contains(opengl, "GE_WATER_FORCE_REFLECTION_FALLBACK"),
                 "the reflection diagnostic matrix must be able to exercise fallback copies deterministically");
}

static void test_per_surface_composite_plane(TestState &tests,
                                              std::string_view opengl)
{
    const std::string_view shader = between(
        opengl, "float reflectionDiagnostic", "float facing");
    tests.expect(contains(shader, "vec4 surfacePlane = vViewPlane") &&
                     contains(shader, "dot(surfacePlane.xyz, surfacePlane.xyz)"),
                 "composite clipping validity must use the interpolated surface plane");
    tests.expect(!contains(opengl,
                           "glUniform4fv(water_composite_clip_plane_location, 1,\n"
                           "                 camera->surfaces[0].view_plane)"),
                 "multi-surface composite must not gate every surface with surface zero's plane");
}

static void test_runtime_capability_forwarding(TestState &tests,
                                               std::string_view pc)
{
    tests.expect(contains(pc, "geWaterCompositeCanRunWithDepthFallback") &&
                     !contains(pc,
                               "geWaterCompositeCanRun(g_render_mode, 1, 1, triangle_count)"),
                 "frontend water admission must use the explicit color-only depth fallback policy");
}

static void test_partial_reflection_failure(TestState &tests,
                                            std::string_view api,
                                            std::string_view pc)
{
    tests.expect(contains(api, "bool (*end_water_reflection)(void)"),
                 "reflection tile completion must report whether replayed geometry populated the tile");
    tests.expect(contains(pc, "const bool tile_populated = gfx_rapi->end_water_reflection()") &&
                     contains(pc, "if (tile_populated)"),
                 "a blank reflection tile must not be reported as a valid reflected-camera result");
}

int main()
{
    TestState tests;
    const fs::path root = GE007_SOURCE_DIR;
    const std::string opengl = read_text(root / "port/fast3d/gfx_opengl.cpp");
    const std::string pc = read_text(root / "port/fast3d/gfx_pc.cpp");
    const std::string api = read_text(root / "port/fast3d/gfx_rendering_api.h");
    tests.expect(!opengl.empty() && !pc.empty(),
                 "live water shader contract must load renderer sources");
    test_reflection_validity_gate(tests, opengl);
    test_contact_uniform_policy(tests, opengl);
    test_truthful_reflection_trace(tests, pc, opengl);
    test_world_anchored_heightfield(tests, pc, opengl);
    test_heightfield_normal_authority(tests, opengl);
    test_water_mesh_resolution(tests, opengl);
    test_contact_foam_authority(tests, opengl);
    test_depth_copy_capability(tests, opengl);
    test_disturbance_single_application(tests, pc, opengl);
    test_environment_reflection_fallback(tests, opengl);
    test_per_surface_composite_plane(tests, opengl);
    test_runtime_capability_forwarding(tests, pc);
    test_partial_reflection_failure(tests, api, pc);
    return tests.failures == 0 ? 0 : 1;
}
