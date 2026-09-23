/* Source-level integration contracts for the heightfield adapter.
 *
 * The numerical behavior is covered by water_heightfield_policy_test. These
 * checks protect the ownership boundary between that pure core, the PC frame
 * lifecycle, and the OpenGL upload/shader path without requiring a live GL
 * context in every unit-test run.
 */
#include "water_render_contract_test_support.h"

#include <string_view>

using geWaterContractTest::TestState;
using geWaterContractTest::contains;
using geWaterContractTest::count_occurrences;
using geWaterContractTest::read_text;
namespace fs = geWaterContractTest::fs;

static void test_public_backend_boundary(TestState &tests,
                                         std::string_view api,
                                         std::string_view material_api)
{
    tests.expect(contains(api, "struct GfxWaterHeightfieldView"),
                 "water rendering API must expose an immutable heightfield view");
    tests.expect(contains(api, "const struct GfxWaterHeightfieldView *heightfield"),
                 "water composite must receive the heightfield through the backend boundary");
    tests.expect(contains(api, "const float *normal_x") &&
                     contains(api, "const float *normal_z"),
                 "heightfield uploads must carry the deterministic normal fields");
    tests.expect(contains(material_api, "gfx_water_boat_wake") &&
                     contains(material_api, "stationary"),
                 "boat wake submission must document the stationary-body no-op boundary");
}

static void test_frontend_lifecycle(TestState &tests, std::string_view pc)
{
    tests.expect(contains(pc, "gfx_water_heightfield_policy.h") &&
                     contains(pc, "static GeWaterHeightfield g_water_heightfield"),
                 "PC frontend must own one shared deterministic water field");
    tests.expect(contains(pc, "g_water_heightfield.advance(g_water_delta_time)"),
                 "water field must advance from the bounded frame delta");
    tests.expect(contains(pc, "g_water_heightfield.add_impulse"),
                 "public ripple events must feed the heightfield authority");
    tests.expect(contains(pc, "g_water_heightfield.reset()") &&
                     contains(pc, "if (context_destroy)"),
                 "heightfield state must be cleared at context teardown");
    tests.expect(contains(pc, "geWaterBuildBoatWake") &&
                     contains(pc, "gfx_water_ripple(impulse.water_u"),
                 "boat wake policy must adapt into the existing bounded ripple port");
    tests.expect(contains(pc, "const GfxWaterHeightfieldView heightfield") &&
                     contains(pc, "&heightfield"),
                 "frontend must pass the field view for the duration of the composite call");
    tests.expect(contains(pc, "gfx_water_boat_wake(0.30f + phase"),
                 "the boat fixture must exercise the public wake adapter");
    tests.expect(contains(pc, "gfx_water_ripple(0.62f, 0.38f"),
                 "dam impact must remain a one-shot environmental impulse, not a wake");
}

static void test_production_disturbance_wiring(TestState &tests,
                                               std::string_view material_api,
                                               std::string_view propobj)
{
    tests.expect(contains(material_api, "gfx_water_boat_wake_world"),
                 "the gameplay adapter must accept world-space boat motion");
    tests.expect(contains(propobj, "PROP_SPEEDBOAT") &&
                     contains(propobj, "gfx_water_boat_wake_world"),
                 "the real speedboat update path must submit wakes, not only the test fixture");
}

static void test_gpu_upload_and_shader(TestState &tests, std::string_view opengl)
{
    tests.expect(contains(opengl, "static GLuint water_heightfield_texture") &&
                     contains(opengl, "gfx_opengl_upload_water_heightfield"),
                 "OpenGL must own a separate dynamic heightfield texture");
    tests.expect(contains(opengl, "glTexSubImage2D") &&
                     contains(opengl, "heightfield->max_height"),
                 "heightfield upload must encode bounded CPU values instead of inventing shader state");
    tests.expect(contains(opengl, "GL_TEXTURE0 + water_heightfield_texture_unit"),
                 "heightfield binding must use an explicit texture unit");
    tests.expect(contains(opengl, "uWaterHeightfield") &&
                     contains(opengl, "uWaterHeightfieldScale"),
                 "water shader must expose dynamic height and world-space scale uniforms");
    tests.expect(count_occurrences(opengl, "sampleHeightfieldBilinear") >= 2,
                 "dynamic height must be sampled in both displacement and shading stages");
    tests.expect(contains(opengl, "dynamicHeightX - dynamicHeight") &&
                     contains(opengl, "dynamicHeightY - dynamicHeight"),
                 "dynamic field normals must use finite differences, not only a flat tint");
    tests.expect(contains(opengl, "GE_WATER_HEIGHTFIELD_RENDER_SCALE_ENHANCED") &&
                     contains(opengl, "GE_WATER_HEIGHTFIELD_RENDER_SCALE_REMASTER"),
                 "render tiers must select bounded heightfield displacement scales");
    tests.expect(contains(opengl, "water_heightfield_texture_unit = -1") &&
                     contains(opengl, "heightfield_uploaded ?"),
                 "unsupported texture-unit hardware must disable only the dynamic layer");
    tests.expect(contains(opengl, "water_material_height_texture") &&
                     contains(opengl, "water_heightfield_texture"),
                 "authored and runtime height sources must remain separate resources");
}

int main()
{
    TestState tests;
    const fs::path root = GE007_SOURCE_DIR;
    const std::string api = read_text(root / "port/fast3d/gfx_rendering_api.h");
    const std::string material_api =
        read_text(root / "port/include/gfx_water_material_api.h");
    const std::string pc = read_text(root / "port/fast3d/gfx_pc.cpp");
    const std::string opengl = read_text(root / "port/fast3d/gfx_opengl.cpp");
    const std::string propobj = read_text(root / "src/game/propobj.c");
    const std::string policy =
        read_text(root / "port/include/gfx_water_heightfield_policy.h");

    tests.expect(!api.empty() && !material_api.empty() && !pc.empty() &&
                     !opengl.empty() && !policy.empty(),
                 "heightfield integration contract must load all owned sources");
    test_public_backend_boundary(tests, api, material_api);
    test_frontend_lifecycle(tests, pc);
    test_production_disturbance_wiring(tests, material_api, propobj);
    test_gpu_upload_and_shader(tests, opengl);
    tests.expect(contains(policy, "GE_WATER_HEIGHTFIELD_FIXED_STEP") &&
                     contains(policy, "GE_WATER_HEIGHTFIELD_DAMPING") &&
                     contains(policy, "GE_WATER_HEIGHTFIELD_STIFFNESS"),
                 "heightfield policy constants must remain explicit and testable");

    return tests.failures == 0 ? 0 : 1;
}
