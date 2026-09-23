#include "bumpmap_runtime.h"

#include <png.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace {

static bool geParseUnsigned(const std::string &text, uint64_t *value)
{
    if (value == nullptr || text.empty()) {
        return false;
    }

    char *end = nullptr;
    errno = 0;
    const unsigned long long parsed =
        std::strtoull(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        return false;
    }
    *value = static_cast<uint64_t>(parsed);
    return true;
}

static bool geParseCsv(const std::string &line, std::vector<std::string> *fields)
{
    if (fields == nullptr) {
        return false;
    }

    fields->clear();
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            fields->push_back(field);
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    if (quoted) {
        return false;
    }
    fields->push_back(field);
    return true;
}

static bool geLoadPngRgba(const std::string &path, GeBumpMapPixels *pixels)
{
    if (pixels == nullptr) {
        return false;
    }

    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                              nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (png == nullptr || info == nullptr) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return false;
    }

    if (setjmp(png_jmpbuf(png)) != 0) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return false;
    }

    png_init_io(png, file);
    png_read_info(png, info);

    const png_uint_32 width = png_get_image_width(png, info);
    const png_uint_32 height = png_get_image_height(png, info);
    const int bit_depth = png_get_bit_depth(png, info);
    const int color_type = png_get_color_type(png, info);
    if (width == 0 || height == 0 || width > 16384 || height > 16384) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return false;
    }

    if (bit_depth == 16) {
        png_set_strip_16(png);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }

    const bool has_alpha = (color_type & PNG_COLOR_MASK_ALPHA) != 0 ||
                           png_get_valid(png, info, PNG_INFO_tRNS) != 0;
    if (png_get_valid(png, info, PNG_INFO_tRNS) != 0) {
        png_set_tRNS_to_alpha(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    if (!has_alpha) {
        png_set_filler(png, 0xff, PNG_FILLER_AFTER);
    }

    png_read_update_info(png, info);
    const png_size_t row_bytes = png_get_rowbytes(png, info);
    const std::size_t pixel_bytes = static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(height) * 4;
    if (row_bytes < static_cast<png_size_t>(width) * 4 ||
        pixel_bytes / 4 != static_cast<std::size_t>(width) * height) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return false;
    }

    GeBumpMapPixels decoded;
    decoded.width = width;
    decoded.height = height;
    decoded.rgba.resize(pixel_bytes);
    std::vector<png_bytep> rows(height);
    for (png_uint_32 y = 0; y < height; ++y) {
        rows[y] = decoded.rgba.data() +
                  static_cast<std::size_t>(y) * width * 4;
    }
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(file);

    *pixels = std::move(decoded);
    return true;
}

static bool geParseManifestEntry(const std::vector<std::string> &fields,
                                 GeBumpMapEntry *entry)
{
    if (entry == nullptr || fields.size() < 12 || fields[9] != "generated" ||
        !geBumpMapSafeRelativePath(fields[8].c_str())) {
        return false;
    }

    uint64_t image_index = 0;
    uint64_t rom_offset = 0;
    uint64_t rom_size = 0;
    uint64_t lod = 0;
    uint64_t width = 0;
    uint64_t height = 0;
    if (!geParseUnsigned(fields[0], &image_index) ||
        !geParseUnsigned(fields[2], &rom_offset) ||
        !geParseUnsigned(fields[3], &rom_size) ||
        !geParseUnsigned(fields[4], &lod) ||
        !geParseUnsigned(fields[5], &width) ||
        !geParseUnsigned(fields[6], &height) ||
        image_index > std::numeric_limits<uint32_t>::max() ||
        rom_size == 0 || lod >= 8 || width == 0 || height == 0 ||
        width > 16384 || height > 16384) {
        return false;
    }

    entry->rom_offset = rom_offset;
    entry->rom_size = rom_size;
    entry->lod = static_cast<uint32_t>(lod);
    entry->width = static_cast<uint32_t>(width);
    entry->height = static_cast<uint32_t>(height);
    entry->normal_path = fields[8];
    entry->normal_load_attempted = false;
    entry->normal_pixels = {};
    return true;
}

} // namespace

bool geBumpMapSafeRelativePath(const char *path)
{
    if (path == nullptr || *path == '\0' || path[0] == '/' ||
        std::strchr(path, '\\') != nullptr || std::strchr(path, ':') != nullptr) {
        return false;
    }

    const char *cursor = path;
    while (*cursor != '\0') {
        const char *slash = std::strchr(cursor, '/');
        const std::size_t length = slash == nullptr
            ? std::strlen(cursor)
            : static_cast<std::size_t>(slash - cursor);
        if (length == 0 || (length == 1 && cursor[0] == '.') ||
            (length == 2 && cursor[0] == '.' && cursor[1] == '.')) {
            return false;
        }
        if (slash == nullptr) {
            break;
        }
        cursor = slash + 1;
    }
    return true;
}

bool geBumpMapRomOffset(const uint8_t *address, uint64_t *offset)
{
    constexpr uintptr_t kCartBase = static_cast<uintptr_t>(0x10000000u);
    constexpr uintptr_t kCartLimit = static_cast<uintptr_t>(0x20000000u);
    if (address == nullptr || offset == nullptr) {
        return false;
    }

    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    if (value < kCartBase || value >= kCartLimit) {
        return false;
    }
    *offset = static_cast<uint64_t>(value - kCartBase);
    return true;
}

void GeBumpMapSourceIndex::clear()
{
    offsets_.clear();
}

void GeBumpMapSourceIndex::register_source(const uint8_t *source_address,
                                           uint64_t rom_offset)
{
    if (source_address != nullptr) {
        offsets_[source_address] = rom_offset;
    }
}

bool GeBumpMapSourceIndex::find(const uint8_t *source_address,
                                uint64_t *rom_offset) const
{
    if (source_address == nullptr || rom_offset == nullptr) {
        return false;
    }

    const auto entry = offsets_.find(source_address);
    if (entry == offsets_.end()) {
        return false;
    }
    *rom_offset = entry->second;
    return true;
}

bool GeBumpMapRuntime::load_manifest(const char *directory)
{
    clear();
    if (directory == nullptr || *directory == '\0') {
        return false;
    }

    directory_ = directory;
    const std::string path = directory_ + "/manifest.csv";
    std::ifstream manifest(path);
    if (!manifest) {
        clear();
        return false;
    }

    std::string line;
    while (std::getline(manifest, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::vector<std::string> fields;
        if (!geParseCsv(line, &fields)) {
            continue;
        }
        if (!fields.empty() && fields[0] == "image_index") {
            continue;
        }

        GeBumpMapEntry entry;
        if (geParseManifestEntry(fields, &entry)) {
            entries_.push_back(std::move(entry));
        }
    }

    loaded_ = !entries_.empty();
    if (!loaded_) {
        clear();
    }
    return loaded_;
}

void GeBumpMapRuntime::clear()
{
    directory_.clear();
    entries_.clear();
    loaded_ = false;
}

const GeBumpMapEntry *GeBumpMapRuntime::find(uint64_t rom_offset,
                                             uint32_t lod) const
{
    for (const GeBumpMapEntry &entry : entries_) {
        if (entry.rom_offset == rom_offset && entry.lod == lod) {
            return &entry;
        }
    }
    return nullptr;
}

const GeBumpMapPixels *GeBumpMapRuntime::load_normal_map(
    uint64_t rom_offset, uint32_t lod) const
{
    const GeBumpMapEntry *entry = find(rom_offset, lod);
    if (entry == nullptr) {
        return nullptr;
    }
    if (entry->normal_load_attempted) {
        return entry->normal_pixels.rgba.empty() ? nullptr :
                                                     &entry->normal_pixels;
    }

    entry->normal_load_attempted = true;
    const std::string path = directory_ + "/" + entry->normal_path;
    if (!geLoadPngRgba(path, &entry->normal_pixels)) {
        entry->normal_pixels = {};
        return nullptr;
    }
    return &entry->normal_pixels;
}
