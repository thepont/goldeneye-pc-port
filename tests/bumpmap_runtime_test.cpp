#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <png.h>
#include <string>

#include "../port/fast3d/bumpmap_runtime.h"

static void geTestRequire(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

#undef assert
#define assert(condition) geTestRequire((condition))

static void test_manifest_matches_rom_slice_and_lod()
{
    const std::string root = "/tmp/ge007-bumpmap-runtime-test";
    std::filesystem::create_directories(root);
    png_image image = {};
    const uint8_t normal_pixels[] = {
        128, 128, 255, 255,
        64, 192, 255, 255,
    };
    image.version = PNG_IMAGE_VERSION;
    image.width = 2;
    image.height = 1;
    image.format = PNG_FORMAT_RGBA;
    assert(png_image_write_to_file(
        &image, (root + "/wall.normal.png").c_str(), 0, normal_pixels,
        0, nullptr));

    std::ofstream manifest(root + "/manifest.csv");
    manifest << "# rom_fnv1a64=0123456789abcdef\n"
             << "# strength=2.5\n"
             << "# include_default=0\n"
             << "image_index,name,rom_offset,rom_size,lod,width,height,"
                "height_map,normal_map,status,reason,source_fnv1a64\n"
             << "7,\"WALL\",64,128,0,32,32,\"wall.height.png\","
                "\"wall.normal.png\",\"generated\",\"eligible\","
                "0123456789abcdef\n"
             << "7,\"WALL\",64,128,1,16,16,\"wall-1.height.png\","
                "\"wall-1.normal.png\",\"generated\",\"eligible\","
                "0123456789abcdef\n";
    manifest.close();

    GeBumpMapRuntime runtime;
    assert(runtime.load_manifest(root.c_str()));
    assert(runtime.entry_count() == 2);

    const GeBumpMapEntry *base = runtime.find(64, 0);
    assert(base != nullptr);
    assert(base->rom_size == 128);
    assert(base->normal_path == "wall.normal.png");
    const GeBumpMapPixels *decoded = runtime.load_normal_map(64, 0);
    assert(decoded != nullptr);
    assert(decoded->width == 2);
    assert(decoded->height == 1);
    assert(decoded->rgba.size() == sizeof(normal_pixels));
    assert(decoded->rgba[0] == 128);
    assert(decoded->rgba[1] == 128);
    assert(decoded->rgba[2] == 255);
    assert(runtime.load_normal_map(64, 0) == decoded);
    assert(runtime.load_normal_map(65, 0) == nullptr);

    const GeBumpMapEntry *lod1 = runtime.find(64, 1);
    assert(lod1 != nullptr);
    assert(lod1->width == 16);
    assert(runtime.find(65, 0) == nullptr);
    assert(runtime.find(64, 2) == nullptr);
}

static void test_manifest_rejects_unsafe_sidecar_paths()
{
    assert(geBumpMapSafeRelativePath("wall.normal.png"));
    assert(geBumpMapSafeRelativePath("nested/wall.normal.png"));
    assert(!geBumpMapSafeRelativePath("../wall.normal.png"));
    assert(!geBumpMapSafeRelativePath("nested/../wall.normal.png"));
    assert(!geBumpMapSafeRelativePath("/tmp/wall.normal.png"));
    assert(!geBumpMapSafeRelativePath(""));
}

static void test_rom_cart_address_conversion()
{
    uint64_t offset = 0;
    assert(geBumpMapRomOffset(reinterpret_cast<const uint8_t *>(0x10000040u),
                              &offset));
    assert(offset == 0x40);
    assert(!geBumpMapRomOffset(reinterpret_cast<const uint8_t *>(0x70000040u),
                               &offset));
    assert(!geBumpMapRomOffset(nullptr, &offset));
    assert(!geBumpMapRomOffset(reinterpret_cast<const uint8_t *>(0x10000040u),
                               nullptr));
}

static void test_decompressed_texture_sources_keep_rom_identity()
{
    GeBumpMapSourceIndex sources;
    uint8_t texture_data[8] = {};
    uint64_t rom_offset = 0;

    sources.register_source(texture_data, 0x8f7df0);
    assert(sources.find(texture_data, &rom_offset));
    assert(rom_offset == 0x8f7df0);
    assert(!sources.find(texture_data + 1, &rom_offset));
    assert(!sources.find(nullptr, &rom_offset));
    assert(!sources.find(texture_data, nullptr));
    sources.register_source(texture_data, 0x953570);
    assert(sources.find(texture_data, &rom_offset));
    assert(rom_offset == 0x953570);
    sources.clear();
    assert(!sources.find(texture_data, &rom_offset));

#ifndef GE007_SOURCE_DIR
    std::abort();
#else
    std::ifstream image_source_file(std::string(GE007_SOURCE_DIR) +
                                     "/src/game/image.c");
    const std::string image_source(
        (std::istreambuf_iterator<char>(image_source_file)),
        std::istreambuf_iterator<char>());
    std::ifstream renderer_source_file(std::string(GE007_SOURCE_DIR) +
                                       "/port/fast3d/gfx_pc.cpp");
    const std::string renderer_source(
        (std::istreambuf_iterator<char>(renderer_source_file)),
        std::istreambuf_iterator<char>());
    assert(image_source.find("gfx_register_texture_source") !=
           std::string::npos);
    assert(renderer_source.find("g_bump_map_sources.find") !=
           std::string::npos);
    assert(renderer_source.find("loaded_texture.source_addr") !=
           std::string::npos);
#endif
}

static void test_bump_maps_are_linear_diffuse_data()
{
#ifndef GE007_SOURCE_DIR
    std::abort();
#else
    std::ifstream source_file(std::string(GE007_SOURCE_DIR) +
                              "/port/fast3d/gfx_opengl.cpp");
    const std::string source((std::istreambuf_iterator<char>(source_file)),
                             std::istreambuf_iterator<char>());
    assert(!source.empty());

    const std::size_t upload_start =
        source.find("static void gfx_opengl_upload_bump_texture");
    const std::size_t upload_end = source.find(
        "static uint32_t gfx_cm_to_opengl", upload_start);
    assert(upload_start != std::string::npos);
    assert(upload_end != std::string::npos);
    const std::string bump_upload =
        source.substr(upload_start, upload_end - upload_start);
    assert(bump_upload.find("GL_RGBA8") != std::string::npos);
    assert(bump_upload.find("GL_SRGB") == std::string::npos);
    assert(source.find("char fs_buf[16384]") != std::string::npos);

    assert(source.find("set_bump_texture") != std::string::npos);
    const std::size_t bump_binding_start = source.find(
        "static void gfx_opengl_set_bump_texture");
    const std::size_t bump_binding_end = source.find(
        "static void gfx_opengl_upload_texture", bump_binding_start);
    assert(bump_binding_start != std::string::npos);
    assert(bump_binding_end != std::string::npos);
    const std::string bump_binding = source.substr(
        bump_binding_start, bump_binding_end - bump_binding_start);
    /* Selecting the auxiliary unit must be transparent to the caller. The
     * base texture importer can call this while unit 0 is bound and must
     * still upload into that base texture. */
    assert(bump_binding.find("GL_ACTIVE_TEXTURE") != std::string::npos);
    assert(bump_binding.find("glActiveTexture((GLenum)active_texture") !=
           std::string::npos);
    const std::size_t shader_start = source.find(
        "if (uBumpMapEnabled != 0)");
    const std::size_t shader_end = source.find(
        "/* Enhanced water", shader_start);
    assert(shader_start != std::string::npos);
    assert(shader_end != std::string::npos);
    const std::string bump_shader =
        source.substr(shader_start, shader_end - shader_start);
    assert(bump_shader.find("bumpNormal") != std::string::npos);
    assert(bump_shader.find("specular") == std::string::npos);
    assert(bump_shader.find("reflect(") == std::string::npos);
    assert(bump_shader.find("fresnel") == std::string::npos);
#endif
}

int main()
{
    test_manifest_matches_rom_slice_and_lod();
    test_manifest_rejects_unsafe_sidecar_paths();
    test_rom_cart_address_conversion();
    test_decompressed_texture_sources_keep_rom_identity();
    test_bump_maps_are_linear_diffuse_data();
    return 0;
}
