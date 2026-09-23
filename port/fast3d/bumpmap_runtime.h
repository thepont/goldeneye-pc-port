#ifndef GE007_BUMPMAP_RUNTIME_H
#define GE007_BUMPMAP_RUNTIME_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/* Runtime reader for the ROM-derived normal-map sidecar emitted by
 * tools_pc/create_bumpmaps.sh. It owns no OpenGL state: the renderer uploads
 * the decoded RGBA8 pixels through its normal texture API. */
struct GeBumpMapPixels {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

struct GeBumpMapEntry {
    uint64_t rom_offset = 0;
    uint64_t rom_size = 0;
    uint32_t lod = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string normal_path;

    /* The normal PNG is decoded once on first use. */
    mutable bool normal_load_attempted = false;
    mutable GeBumpMapPixels normal_pixels;
};

bool geBumpMapSafeRelativePath(const char *path);
bool geBumpMapRomOffset(const uint8_t *address, uint64_t *offset);

/* Decompressed game textures live at transient RDRAM/heap addresses while
 * the sidecar manifest is keyed by the original ROM image offset. This small
 * index keeps that identity across the texture upload boundary. */
class GeBumpMapSourceIndex {
public:
    void clear();
    void register_source(const uint8_t *source_address, uint64_t rom_offset);
    bool find(const uint8_t *source_address, uint64_t *rom_offset) const;

private:
    std::unordered_map<const uint8_t *, uint64_t> offsets_;
};

class GeBumpMapRuntime {
public:
    bool load_manifest(const char *directory);
    void clear();

    const GeBumpMapEntry *find(uint64_t rom_offset, uint32_t lod) const;
    const GeBumpMapPixels *load_normal_map(uint64_t rom_offset,
                                           uint32_t lod) const;

    std::size_t entry_count() const { return entries_.size(); }
    bool loaded() const { return loaded_; }
    const std::string &directory() const { return directory_; }

private:
    std::string directory_;
    std::vector<GeBumpMapEntry> entries_;
    bool loaded_ = false;
};

#endif /* GE007_BUMPMAP_RUNTIME_H */
