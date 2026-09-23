/*
 * Contract checks for the live water BDD harness.
 *
 * The shell harness is the only current end-to-end entry point for the real
 * renderer.  These checks keep its three render tiers, shader diagnostics,
 * split-screen coverage, and future framebuffer-readback boundary visible.
 * They are intentionally separate from the live test so a missing display
 * can skip the expensive run without hiding a missing assertion in the test
 * definition itself.
 */
#include "water_render_contract_test_support.h"

#include <string>

using geWaterContractTest::TestState;
using geWaterContractTest::between;
using geWaterContractTest::contains;
using geWaterContractTest::read_text;
namespace fs = geWaterContractTest::fs;

static void test_water_modes(TestState &tests, std::string_view bdd)
{
    tests.expect(contains(bdd, "water_material_original"),
                 "BDD must retain an Original water scenario");
    tests.expect(contains(bdd, "water_material_enhanced"),
                 "BDD must retain an Enhanced water scenario");
    tests.expect(contains(bdd, "water_material_remaster"),
                 "BDD must retain a Remaster water scenario");
    tests.expect(contains(bdd, "GE_RENDER_MODE=0") &&
                     contains(bdd, "GE_RENDER_MODE=1") &&
                     contains(bdd, "GE_RENDER_MODE=2") &&
                     contains(bdd, "assert_absent water_material_original") &&
                     contains(bdd, "WATER_MATERIAL:"),
                 "water BDD must exercise the disabled Original boundary and both enhanced material tiers");
}

static void test_trace_and_failure_assertions(TestState &tests,
                                               std::string_view bdd)
{
    tests.expect(contains(bdd, "assert_absent") &&
                     contains(bdd, "WATER_COMPOSITE_SHADER"),
                 "BDD must reject shader compile and link diagnostics");
    tests.expect(contains(bdd, "WATER_COMPOSITE_GEOMETRY") &&
                     contains(bdd, "WATER_COMPOSITE:"),
                 "BDD must observe geometry capture and composite execution");
    tests.expect(contains(bdd, "fail_case \"${name} reported a crash\"") &&
                     contains(bdd, "FATAL|Crashed") &&
                     contains(bdd, "SIGSEGV"),
                 "BDD must turn renderer crashes into test failures");
}

static void test_viewport_and_readback_boundaries(TestState &tests,
                                                   std::string_view bdd)
{
    tests.expect(contains(bdd, "assert_split_render_viewports"),
                 "BDD must prove the two player rectangles do not overlap");
    tests.expect(contains(bdd, "GE_VIEWPORT_TRACE"),
                 "BDD must retain the renderer viewport trace boundary");
    tests.expect(contains(bdd, "GE_PCDUMP") ||
                     contains(bdd, "glReadPixels") ||
                     contains(bdd, "framebuffer pixel") ||
                     contains(bdd, "readback"),
                 "water BDD must eventually assert framebuffer pixels, not only logs");
    tests.expect(contains(bdd, "pixcount.py"),
                 "water BDD readback must reject an empty framebuffer, not only a created file");
}

static void test_pixel_capture_window(TestState &tests, std::string_view bdd)
{
    tests.expect(contains(bdd, "water_pixel_env=(GE_PCDUMP=191-291:100)") &&
                     contains(bdd, "water_render_probe") &&
                     contains(bdd, "GE_BDD_EXIT_AFTER_FRAMES:=300") &&
                     contains(bdd, "GE_WATER_TEST_FIXED_DELTA:=0.25") &&
                     contains(bdd, "water_dump_files | tail -1") &&
                     contains(bdd, "timeout -k 2s 30s"),
                 "pixel water BDD must use fixed water time and post-load spatial/temporal captures after the black startup frame");
}

static void test_deterministic_completion(TestState &tests,
                                          std::string_view bdd)
{
    tests.expect(contains(bdd, "GE_BDD_EXIT_AFTER_FRAMES") &&
                     contains(bdd, "timeout -k 2s"),
                 "BDD probes must use deterministic frame completion with a timeout only as a hang guard");
    tests.expect(!contains(bdd, "if [[ \"${status}\" -eq 124") &&
                     !contains(bdd, "if [[ \"${status}\" -eq 137"),
                 "BDD probes must fail when the hang-guard timeout fires instead of treating it as success");
    tests.expect(contains(bdd, "assert_render_frame_budget_completed") &&
                     contains(bdd, "TEST_FRAME_EXIT: frames="),
                 "successful frame-bounded probes must prove the engine reached its deterministic completion marker");
    tests.expect(contains(bdd, "GE_STAGE_SMOKE_EXIT") &&
                     contains(bdd, "GE_VIEWPORT_TRACE_EXIT") &&
                     contains(bdd, "GE_SIGHT_TRACE_EXIT"),
                 "render probes with an explicit stage, viewport, or reticle exit must be recognized as valid early-completion boundaries");
}

static void test_menu_probe_completion(TestState &tests,
                                       std::string_view bdd)
{
    const std::string_view menu_probe = between(
        bdd, "menu_render_probe=", "\n");
    tests.expect(contains(menu_probe, "GE_BDD_EXIT_AFTER_FRAMES:=1800") &&
                     contains(menu_probe, "timeout -k 2s 120s"),
                 "menu BDD probes must have a bounded rendered-frame budget and enough deterministic slow-renderer time");
    tests.expect(contains(bdd,
                          "GE_INPUTSCRIPT='12:DOWN;24:A;36:A;48:A;60:A'") &&
                     contains(bdd,
                              "coop_menu_script='60:DOWN;120:DOWN;180:A;300:A;460:A;620:A;780:A;940:A;1100:A;1260:A;1420:A'") &&
                     contains(bdd, "GE_BDD_EXIT_AFTER_FRAMES=1800"),
                 "menu BDD input scripts must retain their spaced transitions and fit the explicit frame budget");
}

static void test_framebuffer_srgb_is_capability_gated(
    TestState &tests, std::string_view opengl)
{
    tests.expect(contains(opengl,
                          "SDL_GL_ExtensionSupported(\"GL_EXT_sRGB_write_control\")") &&
                     contains(opengl,
                              "geWaterFramebufferSrgbControlAvailable"),
                 "OpenGL initialization must detect optional GLES framebuffer-sRGB write control");
    tests.expect(contains(opengl,
                          "state.framebuffer_srgb = gfx_opengl_is_framebuffer_srgb_enabled();") &&
                     contains(opengl,
                              "gfx_opengl_set_framebuffer_srgb(state.framebuffer_srgb);") &&
                     contains(opengl,
                              "gfx_opengl_set_framebuffer_srgb(false);"),
                 "water state capture, restore, and compositing must gate every framebuffer-sRGB call");
}

static void test_pixel_captures_are_run_local(TestState &tests,
                                               std::string_view bdd,
                                               std::string_view video)
{
    tests.expect(contains(video, "GE_PCDUMP_DIR") &&
                     contains(video, "getenv(\"GE_PCDUMP_DIR\")"),
                 "framebuffer readback must support an isolated output directory");
    tests.expect(contains(bdd, "\"GE_PCDUMP_DIR=${WATER_DUMP_DIR}\"") &&
                     contains(bdd, "find \"${WATER_DUMP_DIR}\"") &&
                     !contains(bdd, "-newer \"${WATER_PIXEL_MARKER}\"") &&
                     !contains(bdd, "${ROOT_DIR}/ppm"),
                 "parallel water BDD runs must not select captures from a shared repository directory");
}

static void test_pixel_failure_artifacts_can_be_preserved(
    TestState &tests, std::string_view bdd)
{
    tests.expect(contains(bdd, "GE_BDD_KEEP_TMP") &&
                     contains(bdd, "BDD artifacts preserved at") &&
                     contains(bdd, "rm -rf -- \"${TMP_DIR}\""),
                 "pixel BDD diagnostics must optionally preserve and report only their owned temporary artifacts");
}

static void test_gameplay_model_pixels_are_an_unconditional_bdd_gate(
    TestState &tests, std::string_view bdd)
{
    const std::string_view scenario = between(
        bdd, "# Unlike the structural viewport case above",
        "run_case resume_coop_as_solo");
    tests.expect(contains(scenario, "run_case coop_model_pixels_remaster") &&
                     contains(scenario, "GE_RENDER_MODE=2") &&
                     contains(scenario, "GE_RSEED=deadbeef") &&
                     contains(scenario, "GE_PCDUMP=100-100") &&
                     contains(scenario, "assert_coop_model_pixels") &&
                     !contains(scenario, "GE_BDD_PIXEL_ASSERT"),
                 "the seeded remaster co-op player/prop framebuffer assertion must run in the ordinary BDD suite");
    tests.expect(contains(bdd, "--check-coop-dam") &&
                     contains(bdd, "MODEL_PIXEL_DUMP_DIR") &&
                     contains(bdd, "GE_BDD_MODEL_PIXEL_ORACLE"),
                 "the real-gameplay BDD must route its PPM capture through the model-pixel oracle");
}

static void test_implicit_display_is_isolated(TestState &tests,
                                               std::string_view bdd)
{
    tests.expect(contains(bdd, "exec xvfb-run -a") &&
                     !contains(bdd, "/tmp/.X11-unix/X1"),
                 "PC renderer BDD must use a private Xvfb when the caller has not explicitly selected a display");
}

int main()
{
#ifndef GE007_SOURCE_DIR
    std::cerr << "RED: water BDD contract has no source root\n";
    return 1;
#else
    const std::string bdd = read_text(
        fs::path(GE007_SOURCE_DIR) / "tests" / "run_pc_bdd.sh");
    const std::string opengl = read_text(
        fs::path(GE007_SOURCE_DIR) / "port" / "fast3d" / "gfx_opengl.cpp");
    const std::string video = read_text(
        fs::path(GE007_SOURCE_DIR) / "port" / "src" / "video.c");
    TestState tests;
    tests.expect(!bdd.empty(), "water BDD contract could not read its harness");
    test_water_modes(tests, bdd);
    test_trace_and_failure_assertions(tests, bdd);
    test_viewport_and_readback_boundaries(tests, bdd);
    test_pixel_capture_window(tests, bdd);
    test_deterministic_completion(tests, bdd);
    test_menu_probe_completion(tests, bdd);
    test_framebuffer_srgb_is_capability_gated(tests, opengl);
    test_pixel_captures_are_run_local(tests, bdd, video);
    test_pixel_failure_artifacts_can_be_preserved(tests, bdd);
    test_gameplay_model_pixels_are_an_unconditional_bdd_gate(tests, bdd);
    test_implicit_display_is_isolated(tests, bdd);

    if (tests.failures == 0) {
        std::cerr << "water-render BDD contract passed\n";
        return 0;
    }
    std::cerr << tests.failures
              << " water-render BDD contract(s) are red\n";
    return 1;
#endif
}
