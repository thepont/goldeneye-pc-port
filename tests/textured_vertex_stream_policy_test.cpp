#include "../port/fast3d/bump_mapping_policy.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static std::string read_text(const fs::path &path)
{
    std::ifstream stream(path);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

static int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

int main()
{
    const GeFast3dMaterialRequirements no_features =
        geFast3dMaterialRequirements(0, 0, 0);
    const GeFast3dMaterialRequirements bump_without_texel0 =
        geFast3dMaterialRequirements(0, 1, 0);
    const GeFast3dMaterialRequirements ordinary_material =
        geFast3dMaterialRequirements(1, 0, 0);
    const GeFast3dMaterialRequirements bump_material =
        geFast3dMaterialRequirements(1, 1, 0);
    const GeFast3dMaterialRequirements shadow_only =
        geFast3dMaterialRequirements(0, 1, 1);
    const GeFast3dMaterialRequirements shadowed_material =
        geFast3dMaterialRequirements(1, 0, 1);
    if (no_features.uses_view_position != 0 ||
        no_features.uses_bump_mapping != 0 ||
        bump_without_texel0.uses_view_position != 0 ||
        bump_without_texel0.uses_bump_mapping != 0 ||
        ordinary_material.uses_view_position != 0 ||
        ordinary_material.uses_bump_mapping != 0 ||
        bump_material.uses_view_position != 1 ||
        bump_material.uses_bump_mapping != 1 ||
        shadow_only.uses_view_position != 1 ||
        shadow_only.uses_bump_mapping != 0 ||
        shadowed_material.uses_view_position != 1 ||
        shadowed_material.uses_bump_mapping != 0) {
        return fail("bump and shadow material requirements must remain independent");
    }

#ifndef GE007_SOURCE_DIR
    return fail("source-contract test has no source root");
#else
    const fs::path root = fs::path(GE007_SOURCE_DIR);
    const std::string opengl = read_text(root / "port/fast3d/gfx_opengl.cpp");
    const std::string pc = read_text(root / "port/fast3d/gfx_pc.cpp");
    if (opengl.find("geFast3dMaterialRequirements(") == std::string::npos) {
        return fail("shader generation does not use the shared material requirements");
    }
    const std::size_t bump_guard = opengl.find(
        "if (material_requirements.uses_bump_mapping) {");
    const std::size_t bump_body = opengl.find(
        "vec3 bumpSample = SAMPLE_TEX(uBumpMap, vTexCoordAdj0)");
    if (bump_guard == std::string::npos || bump_body == std::string::npos ||
        bump_guard > bump_body) {
        return fail("shadow-only view-position shaders must not emit the bump-map UV path");
    }
    if (pc.find("geFast3dMaterialRequirements(") == std::string::npos) {
        return fail("VBO packing does not use the shared material requirements");
    }
    return 0;
#endif
}
