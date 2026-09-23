#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <png.h>
#include <string>
#include <system_error>

#include "../port/fast3d/bumpmap_runtime.h"

static void geTestRequire(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

#undef assert
#define assert(condition) geTestRequire((condition))

class ScopedTestDirectory {
public:
    ScopedTestDirectory()
    {
        const std::filesystem::path base =
            std::filesystem::temp_directory_path();
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            const auto ticks = std::chrono::steady_clock::now()
                                   .time_since_epoch().count();
            path_ = base / ("ge007-bumpmap-runtime-test-" +
                            std::to_string(ticks) + "-" +
                            std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error && error != std::errc::file_exists) {
                std::abort();
            }
        }
        std::abort();
    }

    ~ScopedTestDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path &path() const { return path_; }

    ScopedTestDirectory(const ScopedTestDirectory &) = delete;
    ScopedTestDirectory &operator=(const ScopedTestDirectory &) = delete;

private:
    std::filesystem::path path_;
};

static void write_manifest_row(std::ofstream &manifest,
                               const std::string &image_index,
                               const std::string &rom_offset,
                               const std::string &rom_size,
                               const std::string &lod,
                               const std::string &width,
                               const std::string &height,
                               const std::string &normal_path,
                               const std::string &status = "generated",
                               const std::string &name = "WALL")
{
    manifest << image_index << ",\"" << name << "\"," << rom_offset << ","
             << rom_size << "," << lod << "," << width << "," << height
             << ",\"wall.height.png\",\"" << normal_path << "\",\""
             << status << "\",\"eligible\",0123456789abcdef\n";
}

static void write_manifest_header(std::ofstream &manifest)
{
    manifest << "image_index,name,rom_offset,rom_size,lod,width,height,"
                "height_map,normal_map,status,reason,source_fnv1a64\n";
}

static void write_single_manifest(const std::filesystem::path &root,
                                  const std::string &normal_path,
                                  const std::string &name = "WALL")
{
    std::ofstream manifest(root / "manifest.csv");
    write_manifest_header(manifest);
    write_manifest_row(manifest, "7", "64", "128", "0", "1", "1",
                       normal_path, "generated", name);
}

static void write_png(const std::filesystem::path &path, png_uint_32 width,
                      png_uint_32 height, int format,
                      const void *pixels)
{
    png_image image = {};
    image.version = PNG_IMAGE_VERSION;
    image.width = width;
    image.height = height;
    image.format = static_cast<png_uint_32>(format);
    assert(png_image_write_to_file(&image, path.c_str(), 0, pixels, 0,
                                   nullptr));
}

static void test_manifest_matches_rom_slice_and_lod()
{
    const ScopedTestDirectory test_directory;
    const std::string root = test_directory.path().string();
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
    /* Enhanced mode may add a deliberate Blinn-Phong specular contribution
     * to the normal-map path. It must be explicitly gated and kept separate
     * from the water-only reflection/Fresnel treatment that caused the old
     * wet-plastic regression. */
    assert(bump_shader.find("uAdvancedLightingEnabled") != std::string::npos);
    assert(bump_shader.find("if (uAdvancedLightingEnabled != 0)") !=
           std::string::npos);
    assert(bump_shader.find("specular") != std::string::npos);
    assert(bump_shader.find("advancedDisplayToLinear") != std::string::npos);
    assert(bump_shader.find("advancedLinearToDisplay") != std::string::npos);
    assert(bump_shader.find("reflect(") == std::string::npos);
    assert(bump_shader.find("fresnel") == std::string::npos);
#endif
}

static void GivenNullManifestDirectory_WhenLoadingManifest_ThenRuntimeIsEmpty()
{
    /* Arrange */
    GeBumpMapRuntime runtime;

    /* Act */
    const bool loaded = runtime.load_manifest(nullptr);

    /* Assert */
    assert(!loaded);
    assert(!runtime.loaded());
    assert(runtime.entry_count() == 0);
    assert(runtime.directory().empty());
}

static void GivenEmptyManifestDirectory_WhenLoadingManifest_ThenRuntimeIsEmpty()
{
    /* Arrange */
    GeBumpMapRuntime runtime;

    /* Act */
    const bool loaded = runtime.load_manifest("");

    /* Assert */
    assert(!loaded);
    assert(!runtime.loaded());
    assert(runtime.entry_count() == 0);
    assert(runtime.directory().empty());
}

static void GivenMissingManifestFile_WhenLoadingManifest_ThenRuntimeIsEmpty()
{
    /* Arrange */
    const ScopedTestDirectory test_directory;
    GeBumpMapRuntime runtime;

    /* Act */
    const bool loaded = runtime.load_manifest(
        test_directory.path().string().c_str());

    /* Assert */
    assert(!loaded);
    assert(!runtime.loaded());
    assert(runtime.entry_count() == 0);
    assert(runtime.directory().empty());
}

static void GivenMalformedAndInvalidManifestRows_WhenLoadingManifest_ThenValidRowsSurvive()
{
    /* Arrange */
    const ScopedTestDirectory test_directory;
    std::ofstream manifest(test_directory.path() / "manifest.csv");
    manifest << "\"unterminated\nshort\n";
    write_manifest_row(manifest, "7", "64", "128", "0", "1", "1",
                       "wall.normal.png", "skipped");
    write_manifest_row(manifest, "7", "64", "128", "0", "1", "1",
                       "../wall.normal.png");
    write_manifest_row(manifest, "not-a-number", "64", "128", "0", "1",
                       "1", "wall.normal.png");
    write_manifest_row(manifest, "", "64", "128", "0", "1", "1",
                       "wall.normal.png");
    write_manifest_row(manifest, "184467440737095516160", "64", "128", "0",
                       "1", "1", "wall.normal.png");
    write_manifest_row(manifest, "4294967296", "64", "128", "0", "1",
                       "1", "wall.normal.png");
    write_manifest_row(manifest, "7", "64", "0", "0", "1", "1",
                       "wall.normal.png");
    write_manifest_row(manifest, "7", "64", "128", "8", "1", "1",
                       "wall.normal.png");
    write_manifest_row(manifest, "7", "64", "128", "0", "0", "1",
                       "wall.normal.png");
    write_manifest_row(manifest, "7", "64", "128", "0", "1", "0",
                       "wall.normal.png");
    write_manifest_row(manifest, "7", "64", "128", "0", "16385", "1",
                       "wall.normal.png");
    write_manifest_row(manifest, "7", "64", "128", "0", "1", "16385",
                       "wall.normal.png");
    manifest << "7,\"WALL \"\"A\"\"\",64,128,0,1,1,"
                "\"wall.height.png\",\"wall.normal.png\","
                "\"generated\",\"eligible\",0123456789abcdef\n";
    manifest.close();
    GeBumpMapRuntime runtime;

    /* Act */
    const bool loaded = runtime.load_manifest(
        test_directory.path().string().c_str());

    /* Assert */
    assert(loaded);
    assert(runtime.entry_count() == 1);
    assert(runtime.find(64, 0) != nullptr);
}

static void GivenInvalidOnlyManifest_WhenLoadingManifest_ThenPreviousEntriesAreCleared()
{
    /* Arrange */
    const ScopedTestDirectory test_directory;
    write_single_manifest(test_directory.path(), "wall.normal.png");
    GeBumpMapRuntime runtime;
    assert(runtime.load_manifest(test_directory.path().string().c_str()));
    std::ofstream manifest(test_directory.path() / "manifest.csv");
    manifest << "short\n\"unterminated\n";
    manifest.close();

    /* Act */
    const bool loaded = runtime.load_manifest(
        test_directory.path().string().c_str());

    /* Assert */
    assert(!loaded);
    assert(!runtime.loaded());
    assert(runtime.entry_count() == 0);
    assert(runtime.directory().empty());
}

static void GivenMalformedPathForms_WhenCheckingSidecarPaths_ThenTheyAreRejected()
{
    /* Arrange */
    const std::array<const char *, 9> rejected = {
        nullptr, "", "/wall.normal.png", "wall\\normal.png", "wall:normal.png",
        ".", "..", "nested//wall.normal.png", "nested/../wall.normal.png",
    };

    /* Act */
    const bool all_rejected = std::all_of(rejected.begin(), rejected.end(), [](const char *path) { return !geBumpMapSafeRelativePath(path); });

    /* Assert */
    assert(all_rejected);
}

static void GivenNullSourceAddress_WhenRegisteringTextureSource_ThenNoSourceIsAdded()
{
    /* Arrange */
    GeBumpMapSourceIndex sources;
    uint8_t texture_data[1] = {};
    uint64_t rom_offset = 0;

    /* Act */
    sources.register_source(nullptr, 0x100);

    /* Assert */
    assert(!sources.find(texture_data, &rom_offset));
}

static void GivenMissingNormalSidecar_WhenLoadingItTwice_ThenFailureIsCached()
{
    /* Arrange */
    const ScopedTestDirectory test_directory;
    write_single_manifest(test_directory.path(), "missing.normal.png");
    GeBumpMapRuntime runtime;
    assert(runtime.load_manifest(test_directory.path().string().c_str()));

    /* Act */
    const bool remains_unavailable =
        runtime.load_normal_map(64, 0) == nullptr &&
        runtime.load_normal_map(64, 0) == nullptr;

    /* Assert */
    assert(remains_unavailable);
}

static void GivenCorruptNormalSidecar_WhenLoadingIt_ThenDecodeFailsClosed()
{
    /* Arrange */
    const ScopedTestDirectory test_directory;
    std::ofstream corrupt(test_directory.path() / "corrupt.normal.png",
                          std::ios::binary);
    corrupt << "not a PNG";
    corrupt.close();
    write_single_manifest(test_directory.path(), "corrupt.normal.png");
    GeBumpMapRuntime runtime;
    assert(runtime.load_manifest(test_directory.path().string().c_str()));

    /* Act */
    const GeBumpMapPixels *decoded = runtime.load_normal_map(64, 0);

    /* Assert */
    assert(decoded == nullptr);
}

static void GivenRgbNormalSidecar_WhenLoadingIt_ThenAlphaDefaultsOpaque()
{
    /* Arrange */
    const ScopedTestDirectory test_directory;
    const uint8_t pixels[] = {32, 64, 96};
    write_png(test_directory.path() / "rgb.normal.png", 1, 1,
              PNG_FORMAT_RGB, pixels);
    write_single_manifest(test_directory.path(), "rgb.normal.png");
    GeBumpMapRuntime runtime;
    assert(runtime.load_manifest(test_directory.path().string().c_str()));

    /* Act */
    const GeBumpMapPixels *decoded = runtime.load_normal_map(64, 0);

    /* Assert */
    assert(decoded != nullptr);
    assert(decoded->width == 1);
    assert(decoded->height == 1);
    assert(decoded->rgba == std::vector<uint8_t>({32, 64, 96, 255}));
}

static void GivenGrayscaleAlphaNormalSidecar_WhenLoadingIt_ThenGrayIsExpandedToRgb()
{
    /* Arrange */
    const ScopedTestDirectory test_directory;
    const uint8_t pixels[] = {77, 123};
    write_png(test_directory.path() / "gray-alpha.normal.png", 1, 1,
              PNG_FORMAT_GA, pixels);
    write_single_manifest(test_directory.path(), "gray-alpha.normal.png");
    GeBumpMapRuntime runtime;
    assert(runtime.load_manifest(test_directory.path().string().c_str()));

    /* Act */
    const GeBumpMapPixels *decoded = runtime.load_normal_map(64, 0);

    /* Assert */
    assert(decoded != nullptr);
    assert(decoded->rgba == std::vector<uint8_t>({77, 77, 77, 123}));
}

static void GivenSixteenBitNormalSidecar_WhenLoadingIt_ThenSamplesAreReducedToRgba8()
{
    /* Arrange */
    const ScopedTestDirectory test_directory;
    const uint16_t pixels[] = {0x1000, 0x2000, 0x3000, 0xffff};
    write_png(test_directory.path() / "linear.normal.png", 1, 1,
              PNG_FORMAT_LINEAR_RGB_ALPHA, pixels);
    write_single_manifest(test_directory.path(), "linear.normal.png");
    GeBumpMapRuntime runtime;
    assert(runtime.load_manifest(test_directory.path().string().c_str()));

    /* Act */
    const GeBumpMapPixels *decoded = runtime.load_normal_map(64, 0);

    /* Assert */
    assert(decoded != nullptr);
    assert(decoded->rgba == std::vector<uint8_t>({16, 32, 48, 255}));
}

static void GivenOversizedNormalSidecar_WhenLoadingIt_ThenDecodeIsRejected()
{
    /* Arrange */
    const ScopedTestDirectory test_directory;
    const std::vector<uint8_t> pixels(16385U * 4U, 255);
    write_png(test_directory.path() / "oversized.normal.png", 16385, 1,
              PNG_FORMAT_RGBA, pixels.data());
    write_single_manifest(test_directory.path(), "oversized.normal.png");
    GeBumpMapRuntime runtime;
    assert(runtime.load_manifest(test_directory.path().string().c_str()));

    /* Act */
    const GeBumpMapPixels *decoded = runtime.load_normal_map(64, 0);

    /* Assert */
    assert(decoded == nullptr);
}

int main()
{
    test_manifest_matches_rom_slice_and_lod();
    test_manifest_rejects_unsafe_sidecar_paths();
    test_rom_cart_address_conversion();
    test_decompressed_texture_sources_keep_rom_identity();
    test_bump_maps_are_linear_diffuse_data();
    GivenNullManifestDirectory_WhenLoadingManifest_ThenRuntimeIsEmpty();
    GivenEmptyManifestDirectory_WhenLoadingManifest_ThenRuntimeIsEmpty();
    GivenMissingManifestFile_WhenLoadingManifest_ThenRuntimeIsEmpty();
    GivenMalformedAndInvalidManifestRows_WhenLoadingManifest_ThenValidRowsSurvive();
    GivenInvalidOnlyManifest_WhenLoadingManifest_ThenPreviousEntriesAreCleared();
    GivenMalformedPathForms_WhenCheckingSidecarPaths_ThenTheyAreRejected();
    GivenNullSourceAddress_WhenRegisteringTextureSource_ThenNoSourceIsAdded();
    GivenMissingNormalSidecar_WhenLoadingItTwice_ThenFailureIsCached();
    GivenCorruptNormalSidecar_WhenLoadingIt_ThenDecodeFailsClosed();
    GivenRgbNormalSidecar_WhenLoadingIt_ThenAlphaDefaultsOpaque();
    GivenGrayscaleAlphaNormalSidecar_WhenLoadingIt_ThenGrayIsExpandedToRgb();
    GivenSixteenBitNormalSidecar_WhenLoadingIt_ThenSamplesAreReducedToRgba8();
    GivenOversizedNormalSidecar_WhenLoadingIt_ThenDecodeIsRejected();
    return 0;
}
