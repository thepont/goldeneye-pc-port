/* Regression contracts for the live outdoor-lighting failure.
 *
 * These checks are intentionally unit-only. They pin the two seams found by
 * the debugger without launching a level: the level pass must restore the
 * RSP lighting geometry bit after sky rendering, and the PC light-register
 * decoder must use an ambient-sized copy for Ambient_t.
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <PR/mbi.h>
#include "../port/fast3d/fast3d_ambient_lighting.h"

namespace fs = std::filesystem;

struct TestState
{
    int failures = 0;

    void expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "RED: " << message << '\n';
        }
    }
};

static std::string read_text(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return {};
    }
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

static std::string_view between(std::string_view source,
                                std::string_view begin,
                                std::string_view end)
{
    const std::size_t begin_pos = source.find(begin);
    if (begin_pos == std::string_view::npos)
    {
        return {};
    }

    const std::size_t content_pos = begin_pos + begin.size();
    const std::size_t end_pos = source.find(end, content_pos);
    if (end_pos == std::string_view::npos)
    {
        return source.substr(content_pos);
    }
    return source.substr(content_pos, end_pos - content_pos);
}

static void test_ambient_copy_and_slot_matrix(TestState &tests)
{
    tests.expect(sizeof(Ambient_t) < sizeof(Light_t),
                 "Ambient_t must remain smaller than Light_t");

    tests.expect(geFast3dIsAmbientLightUpload(G_MV_L1, 2) != 0,
                 "one-directional-light ambient slot was not recognised");
    tests.expect(geFast3dIsAmbientLightUpload(G_MV_L2, 3) != 0,
                 "two-directional-light ambient slot was not recognised");
    tests.expect(geFast3dIsAmbientLightUpload(G_MV_L0, 1) != 0,
                 "zero-directional-light ambient slot was not recognised");
    tests.expect(geFast3dIsAmbientLightUpload(G_MV_L0, 2) == 0,
                 "directional slot was mistaken for one-light ambient");
    tests.expect(geFast3dIsAmbientLightUpload(G_MV_L2, 0) == 0,
                 "zero-light state accepted an ambient upload");

    Ambient_t source = {};
    source.col[0] = 150;
    source.col[1] = 140;
    source.col[2] = 130;
    source.colc[0] = 150;
    source.colc[1] = 140;
    source.colc[2] = 130;

    Light_t destination = {};
    destination.dir[0] = 77;
    destination.dir[1] = 77;
    destination.dir[2] = 46;
    geFast3dCopyAmbientLight(&destination, &source);

    for (int channel = 0; channel < 3; ++channel)
    {
        tests.expect(destination.col[channel] == source.col[channel],
                     "ambient diffuse colour was not copied exactly");
        tests.expect(destination.colc[channel] == source.colc[channel],
                     "ambient colour cache was not copied exactly");
        tests.expect(destination.dir[channel] == 0,
                     "ambient upload leaked directional data");
    }

    geFast3dCopyAmbientLight(nullptr, &source);
    geFast3dCopyAmbientLight(&destination, nullptr);
}

struct FakeRspGeometryState
{
    uint32_t mode = G_LIGHTING | G_ZBUFFER;

    void clear(uint32_t bits)
    {
        mode &= ~bits;
    }

    void set(uint32_t bits)
    {
        mode |= bits;
    }
};

static void test_geometry_mode_transition(TestState &tests)
{
    FakeRspGeometryState state;
    state.clear(G_LIGHTING);
    tests.expect((state.mode & G_LIGHTING) == 0,
                 "fake sky pass did not clear G_LIGHTING");
    state.set(G_LIGHTING);
    tests.expect((state.mode & G_LIGHTING) != 0,
                 "fake room pass did not restore G_LIGHTING");

    Gfx restore_command = {};
    gSPSetGeometryMode(&restore_command, G_LIGHTING);
    tests.expect((static_cast<uint32_t>(restore_command.words.w1) &
                  G_LIGHTING) != 0,
                 "room restore command did not carry G_LIGHTING");
}

static void test_lit_and_unlit_base_colour_policy(TestState &tests)
{
    const uint8_t ambient[3] = {150, 140, 130};
    const uint8_t authored[3] = {0, 0, 0};
    uint8_t output[3] = {0, 0, 0};

    geFast3dSelectVertexBaseColor(G_LIGHTING, ambient, authored, output);
    tests.expect(output[0] == 150 && output[1] == 140 && output[2] == 130,
                 "lit vertices did not start from ambient colour");

    geFast3dSelectVertexBaseColor(0, ambient, authored, output);
    tests.expect(output[0] == 0 && output[1] == 0 && output[2] == 0,
                 "unlit vertices did not preserve authored colour");

    const uint8_t authored_nonzero[3] = {7, 8, 9};
    geFast3dSelectVertexBaseColor(G_ZBUFFER, ambient, authored_nonzero,
                                  output);
    tests.expect(output[0] == 7 && output[1] == 8 && output[2] == 9,
                 "non-lighting geometry mode changed authored colour");

    geFast3dSelectVertexBaseColor(G_LIGHTING, nullptr, authored, output);
    tests.expect(output[0] == 0 && output[1] == 0 && output[2] == 0,
                 "missing ambient colour did not fail closed");
    geFast3dSelectVertexBaseColor(0, ambient, nullptr, output);
    tests.expect(output[0] == 0 && output[1] == 0 && output[2] == 0,
                 "missing authored colour did not fail closed");
    geFast3dSelectVertexBaseColor(G_LIGHTING, ambient, authored, nullptr);
}

static void test_room_lighting_restore_respects_render_mode(TestState &tests)
{
    tests.expect(geFast3dShouldRestoreRoomLighting(0) == 0,
                 "Original mode must preserve the ROM room-lighting state");
    tests.expect(geFast3dShouldRestoreRoomLighting(1) != 0,
                 "Enhanced mode must restore room lighting after the sky pass");
    tests.expect(geFast3dShouldRestoreRoomLighting(2) != 0,
                 "Remaster mode must restore room lighting after the sky pass");
    tests.expect(geFast3dShouldRestoreRoomLighting(-1) == 0,
                 "invalid lower render modes must not add lighting state");
}

int main()
{
#ifndef GE007_SOURCE_DIR
    std::cerr << "RED: ambient-lighting regression test has no source root\n";
    return 1;
#else
    const fs::path root = fs::path(GE007_SOURCE_DIR);
    const std::string bg = read_text(root / "src/game/bg.c");
    const std::string sky = read_text(root / "src/game/sky.c");
    const std::string pipeline = read_text(root / "assets/font_dl.c");
    const std::string renderer =
        read_text(root / "port/fast3d/gfx_pc.cpp");

    TestState tests;
    test_ambient_copy_and_slot_matrix(tests);
    test_geometry_mode_transition(tests);
    test_lit_and_unlit_base_colour_policy(tests);
    test_room_lighting_restore_respects_render_mode(tests);
    tests.expect(!bg.empty(), "ambient-lighting test could not read bg.c");
    tests.expect(!sky.empty(), "ambient-lighting test could not read sky.c");
    tests.expect(!pipeline.empty(),
                 "ambient-lighting test could not read font_dl.c");
    tests.expect(!renderer.empty(),
                 "ambient-lighting test could not read gfx_pc.cpp");

    tests.expect(sky.find("gSPClearGeometryMode(gdl++, G_LIGHTING") !=
                     std::string::npos,
                 "sky regression fixture must clear G_LIGHTING");
    tests.expect(pipeline.find("gsSPClearGeometryMode") !=
                     std::string::npos &&
                     pipeline.find("G_LIGHTING") != std::string::npos,
                 "frame pipeline must document its G_LIGHTING reset");

    const std::string_view level_render = between(
        bg, "Gfx *bgLevelRender", "f32 sub_GAME_7F0B4F9C");
    const std::size_t lights = level_render.find("gSPSetLights1");
    const std::size_t restore = level_render.find(
        "gSPSetGeometryMode", lights == std::string_view::npos ? 0 : lights);
    const std::size_t look_at = level_render.find("gSPLookAt");

    tests.expect(lights != std::string_view::npos,
                 "level render must submit the authored GlobalLight");
    tests.expect(level_render.find("geFast3dShouldRestoreRoomLighting") !=
                     std::string_view::npos,
                 "room-lighting restore must be gated by render mode");
    tests.expect(restore != std::string_view::npos &&
                     level_render.substr(restore, 96).find("G_LIGHTING") !=
                         std::string_view::npos,
                 "level render must restore G_LIGHTING after sky rendering");
    tests.expect(restore != std::string_view::npos &&
                     look_at != std::string_view::npos && restore < look_at,
                 "G_LIGHTING must be restored before the room display list");

    const std::string_view movemem = between(
        renderer, "static void gfx_sp_movemem", "static void gfx_sp_moveword");
    tests.expect(movemem.find("geFast3dLightSlotFromMoveMem") !=
                     std::string_view::npos,
                 "light upload must identify each light by its register slot");
    tests.expect(movemem.find("geFast3dCopyAmbientLight") !=
                     std::string_view::npos,
                 "ambient upload must use an Ambient_t-sized copy");
    tests.expect(movemem.find("light_upload_mask") != std::string_view::npos,
                 "light upload must not enable stale partial light state");

    const std::string_view vertex = between(
        renderer, "static void gfx_sp_vertex", "static void gfx_sp_tri1");
    tests.expect(vertex.find("geFast3dSelectVertexBaseColor") !=
                     std::string_view::npos,
                 "vertex shading must use the tested lit/unlit base-colour policy");

    if (tests.failures == 0)
    {
        std::cerr << "ambient-lighting regression contracts passed\n";
        return 0;
    }

    std::cerr << tests.failures
              << " ambient-lighting regression contract(s) are red\n";
    return 1;
#endif
}
