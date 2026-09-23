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
    if (geFast3dMaterialUsesViewPosition(0, 0) != 0 ||
        geFast3dMaterialUsesViewPosition(0, 1) != 0 ||
        geFast3dMaterialUsesViewPosition(1, 0) != 0 ||
        geFast3dMaterialUsesViewPosition(1, 1) != 1) {
        return fail("view-position stream policy matrix is wrong");
    }

#ifndef GE007_SOURCE_DIR
    return fail("source-contract test has no source root");
#else
    const fs::path root = fs::path(GE007_SOURCE_DIR);
    const std::string opengl = read_text(root / "port/fast3d/gfx_opengl.cpp");
    const std::string pc = read_text(root / "port/fast3d/gfx_pc.cpp");
    if (opengl.find("geFast3dMaterialUsesViewPosition") == std::string::npos) {
        return fail("shader generation does not use the shared stream policy");
    }
    if (pc.find("geFast3dMaterialUsesViewPosition") == std::string::npos) {
        return fail("VBO packing does not use the shared stream policy");
    }
    return 0;
#endif
}
