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
    tests.expect(contains(bdd, "water_pixel_env=(GE_PCDUMP=91-191:100)") &&
                     contains(bdd, "water_render_probe") &&
                     contains(bdd, "GE_BDD_EXIT_AFTER_FRAMES:=200") &&
                     contains(bdd, "GE_WATER_TEST_FIXED_DELTA:=0.25") &&
                     contains(bdd, "water_dump_files | head -1") &&
                     contains(bdd, "timeout -k 2s 30s"),
                 "pixel water BDD must use a fixed water clock, stable first post-load spatial capture, and bounded temporal probe");
}

static void test_deterministic_completion(TestState &tests,
                                          std::string_view bdd)
{
    tests.expect(contains(bdd, "GE_BDD_EXIT_AFTER_FRAMES") &&
                     contains(bdd, "timeout -k 2s"),
                 "BDD probes must use deterministic frame completion with a timeout only as a hang guard");
}

static void test_menu_probe_completion(TestState &tests,
                                       std::string_view bdd)
{
    const std::string_view menu_probe = between(
        bdd, "menu_render_probe=", "\n");
    tests.expect(contains(menu_probe, "GE_BDD_EXIT_AFTER_FRAMES:=1800") &&
                     contains(menu_probe, "timeout -k 2s 35s"),
                 "menu BDD probes must terminate on a rendered-frame budget, with wall-clock timeout only as a hang guard");
}

int main()
{
#ifndef GE007_SOURCE_DIR
    std::cerr << "RED: water BDD contract has no source root\n";
    return 1;
#else
    const std::string bdd = read_text(
        fs::path(GE007_SOURCE_DIR) / "tests" / "run_pc_bdd.sh");
    TestState tests;
    tests.expect(!bdd.empty(), "water BDD contract could not read its harness");
    test_water_modes(tests, bdd);
    test_trace_and_failure_assertions(tests, bdd);
    test_viewport_and_readback_boundaries(tests, bdd);
    test_pixel_capture_window(tests, bdd);
    test_deterministic_completion(tests, bdd);
    test_menu_probe_completion(tests, bdd);

    if (tests.failures == 0) {
        std::cerr << "water-render BDD contract passed\n";
        return 0;
    }
    std::cerr << tests.failures
              << " water-render BDD contract(s) are red\n";
    return 1;
#endif
}
