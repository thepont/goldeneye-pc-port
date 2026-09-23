/*
 * BDD/TDD contract for the production world-water path.
 *
 * The old enhanced-water tests can pass while a real IsWater stage still
 * renders only the projected sky fan.  This contract keeps the vertical slice
 * honest: the level submits a physical surface, the renderer captures that
 * source, and the live BDD requires the resulting composite.
 */
#include "water_render_contract_test_support.h"

#include <string>

using geWaterContractTest::TestState;
using geWaterContractTest::contains;
using geWaterContractTest::read_text;
namespace fs = geWaterContractTest::fs;

static void test_public_port_boundary(TestState &tests,
                                      std::string_view api)
{
    tests.expect(contains(api, "gfx_emit_world_water_surface"),
                 "water API must expose a world-surface submission boundary");
    tests.expect(contains(api, "gfx_water_world_surface_enabled"),
                 "water API must expose the modern/legacy path decision");
}

static void test_level_ordering(TestState &tests, std::string_view level)
{
    const std::size_t world_surface =
        level.find("gfx_emit_world_water_surface");
    const std::size_t opaque_world = level.find("DL = bgLevelRender(DL);");
    const std::size_t composite = level.find("DL = gfx_emit_water_composite(DL);");

    tests.expect(world_surface != std::string_view::npos,
                 "level rendering must submit a world water surface");
    tests.expect(opaque_world != std::string_view::npos &&
                     composite != std::string_view::npos &&
                     world_surface > opaque_world && world_surface < composite,
                 "world water must be submitted after opaque geometry and before composite");
    tests.expect(contains(level, "bossGetStageNum()") &&
                     contains(level, "WaterRepeat"),
                 "world water must use the authored stage and water plane height");
}

static void test_sky_is_legacy_only(TestState &tests, std::string_view sky)
{
    tests.expect(contains(sky, "gfx_water_world_surface_enabled"),
                 "sky rendering must consult the modern world-water decision");
    tests.expect(contains(sky, "!gfx_water_world_surface_enabled()"),
                 "the projected sky water fan must remain a legacy fallback");
    tests.expect(contains(sky, "gfx_emit_water_material"),
                 "the original sky-water material must remain available for original mode");
}

static void test_renderer_source_identity(TestState &tests,
                                          std::string_view fast3d,
                                          std::string_view gbiex)
{
    tests.expect(contains(gbiex, "G_SUBMITWORLDWATER_EXT"),
                 "world water needs a distinct display-list command");
    tests.expect(contains(fast3d, "gfx_submit_world_water_surface"),
                 "renderer must execute a physical world-water submission");
    tests.expect(contains(fast3d, "WATER_WORLD_SURFACE: stage="),
                 "renderer must trace the authored world-water source");
    tests.expect(contains(fast3d, "WATER_CAPTURE: source=world-surface"),
                 "world-water capture must be distinguishable from the sky fan");
    tests.expect(contains(fast3d, "world_to_clip") &&
                     contains(fast3d, "water_height"),
                 "world-water geometry must be projected from a physical plane");

    const std::string_view world_command = geWaterContractTest::between(
        fast3d, "case G_SUBMITWORLDWATER_EXT", "case G_SETWATERMATERIAL_EXT");
    tests.expect(contains(world_command, "g_water_reflection_replay"),
                 "reflection replay must not submit the world-water surface twice");
}

static void test_live_bdd(TestState &tests, std::string_view bdd)
{
    tests.expect(contains(bdd, "water_world_surface_frigate"),
                 "live BDD must include a Frigate world-water scenario");
    tests.expect(contains(bdd, "WATER_WORLD_SURFACE: stage=26") &&
                     contains(bdd, "WATER_CAPTURE: source=world-surface") &&
                     contains(bdd, "source=world-surface"),
                 "live BDD must require world-surface capture and composite evidence");
    tests.expect(contains(bdd, "assert_absent water_world_surface_frigate") &&
                     contains(bdd, "WATER_MATERIAL: enabled=1 render_mode=2"),
                 "modern Frigate BDD must reject the legacy sky-water material path");
}

int main()
{
#ifndef GE007_SOURCE_DIR
    std::cerr << "RED: world-water contract has no source root\n";
    return 1;
#else
    const fs::path root = GE007_SOURCE_DIR;
    const std::string api = read_text(
        root / "port" / "include" / "gfx_water_material_api.h");
    const std::string level = read_text(root / "src" / "game" / "lv.c");
    const std::string sky = read_text(root / "src" / "game" / "sky.c");
    const std::string fast3d = read_text(root / "port" / "fast3d" / "gfx_pc.cpp");
    const std::string gbiex = read_text(root / "port" / "fast3d" / "gbiex.h");
    const std::string bdd = read_text(root / "tests" / "run_pc_bdd.sh");

    TestState tests;
    tests.expect(!api.empty() && !level.empty() && !sky.empty() &&
                     !fast3d.empty() && !gbiex.empty() && !bdd.empty(),
                 "world-water contract could not read the production sources");
    test_public_port_boundary(tests, api);
    test_level_ordering(tests, level);
    test_sky_is_legacy_only(tests, sky);
    test_renderer_source_identity(tests, fast3d, gbiex);
    test_live_bdd(tests, bdd);

    if (tests.failures == 0) {
        std::cerr << "world-water surface contract passed\n";
        return 0;
    }
    std::cerr << tests.failures << " world-water surface contract(s) are red\n";
    return 1;
#endif
}
