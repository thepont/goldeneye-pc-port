#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define ge_mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define ge_mkdir(path, mode) mkdir(path, mode)
#endif

#include "bumpmap_decode.h"
#include "bumpmap_metadata.h"
#include "bumpmap_policy.h"
#include "libpdtex/pdtex.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define GE_ROM_HEADER_SIZE 4

struct ge_options {
    const char *rom_path;
    const char *output_path;
    const char *images_def_path;
    const char *imagelist_path;
    float strength;
    int include_default;
};

static int ge_load_image_defs(const char *path,
                              struct ge_image_def **definitions,
                              size_t *definition_count)
{
    FILE *file;
    char line[1024];
    size_t count = 0;
    size_t capacity = 256;
    struct ge_image_def *items;

    if (path == NULL || definitions == NULL || definition_count == NULL) {
        return 0;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "Unable to open image definitions '%s': %s\n",
                path, strerror(errno));
        return 0;
    }

    items = calloc(capacity, sizeof(*items));
    if (items == NULL) {
        fclose(file);
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        struct ge_image_def definition;
        int result = geBumpParseImageDefLine(line, &definition);

        if (result < 0) {
            fprintf(stderr, "Malformed IMAGE() definition in '%s': %s",
                    path, line);
            free(items);
            fclose(file);
            return 0;
        }
        if (result == 0) {
            continue;
        }

        if (count == capacity) {
            capacity *= 2;
            struct ge_image_def *grown =
                realloc(items, capacity * sizeof(*items));
            if (grown == NULL) {
                free(items);
                fclose(file);
                return 0;
            }
            items = grown;
        }

        items[count++] = definition;
    }

    fclose(file);
    *definitions = items;
    *definition_count = count;
    return 1;
}

static int ge_load_image_list(const char *path,
                              struct ge_image_record **records,
                              size_t *record_count)
{
    FILE *file;
    char line[1024];
    size_t count = 0;
    size_t capacity = 256;
    struct ge_image_record *items;

    if (path == NULL || records == NULL || record_count == NULL) {
        return 0;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "Unable to open image list '%s': %s\n",
                path, strerror(errno));
        return 0;
    }

    items = calloc(capacity, sizeof(*items));
    if (items == NULL) {
        fclose(file);
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        struct ge_image_record record;
        int result = geBumpParseImageRecord(line, &record);

        if (result < 0) {
            fprintf(stderr, "Malformed image-list row in '%s': %s",
                    path, line);
            free(items);
            fclose(file);
            return 0;
        }
        if (result == 0) {
            continue;
        }

        if (count == capacity) {
            capacity *= 2;
            struct ge_image_record *grown =
                realloc(items, capacity * sizeof(*items));
            if (grown == NULL) {
                free(items);
                fclose(file);
                return 0;
            }
            items = grown;
        }

        items[count++] = record;
    }

    fclose(file);
    *records = items;
    *record_count = count;
    return 1;
}

static int ge_make_directory(const char *path)
{
    char partial[PATH_MAX];
    size_t length;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    length = strlen(path);
    if (length >= sizeof(partial)) {
        return 0;
    }
    memcpy(partial, path, length + 1);

    for (size_t i = 1; i < length; ++i) {
        if (partial[i] != '/' && partial[i] != '\\') {
            continue;
        }
        partial[i] = '\0';
        if (partial[0] != '\0' && ge_mkdir(partial, 0755) != 0 &&
            errno != EEXIST) {
            return 0;
        }
        partial[i] = path[i];
    }

    if (ge_mkdir(partial, 0755) != 0 && errno != EEXIST) {
        return 0;
    }
    return 1;
}

static uint64_t ge_fnv1a_update(uint64_t hash,
                                const uint8_t *bytes,
                                size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int ge_open_rom(const char *path,
                       FILE **rom,
                       uint64_t *rom_size,
                       uint64_t *rom_hash)
{
    uint8_t header[GE_ROM_HEADER_SIZE];
    uint8_t buffer[1024 * 1024];
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t bytes_read;
    long end;
    FILE *file;

    if (path == NULL || rom == NULL || rom_size == NULL || rom_hash == NULL) {
        return 0;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Unable to open ROM '%s': %s\n",
                path, strerror(errno));
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0 ||
        (end = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0 ||
        end < GE_ROM_HEADER_SIZE) {
        fprintf(stderr, "Unable to determine ROM size for '%s'\n", path);
        fclose(file);
        return 0;
    }

    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        fprintf(stderr, "Unable to read ROM header from '%s'\n", path);
        fclose(file);
        return 0;
    }
    if (header[0] != 0x80 || header[1] != 0x37 ||
        header[2] != 0x12 || header[3] != 0x40) {
        fprintf(stderr,
                "ROM '%s' is not a big-endian .z64 image (expected 80 37 12 40)\n",
                path);
        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        hash = ge_fnv1a_update(hash, buffer, bytes_read);
    }
    if (ferror(file)) {
        fprintf(stderr, "Unable to hash ROM '%s'\n", path);
        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    *rom = file;
    *rom_size = (uint64_t)end;
    *rom_hash = hash;
    return 1;
}

static uint8_t *ge_read_rom_slice(FILE *rom,
                                  uint64_t rom_size,
                                  uint64_t offset,
                                  uint64_t size)
{
    uint8_t *buffer;

    if (rom == NULL || size == 0 || size > SIZE_MAX ||
        offset > rom_size || size > rom_size - offset ||
        offset > (uint64_t)LONG_MAX) {
        return NULL;
    }

    buffer = malloc((size_t)size);
    if (buffer == NULL) {
        return NULL;
    }
    if (fseek(rom, (long)offset, SEEK_SET) != 0 ||
        fread(buffer, 1, (size_t)size, rom) != (size_t)size) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static int ge_write_png(const char *path,
                        uint32_t width,
                        uint32_t height,
                        int format,
                        const uint8_t *pixels)
{
    png_image image;

    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    image.width = width;
    image.height = height;
    image.format = format;

    if (!png_image_write_to_file(&image, path, 0, pixels, 0, NULL)) {
        fprintf(stderr, "Unable to write PNG '%s': %s\n", path, image.message);
        return 0;
    }
    return 1;
}

static void ge_sanitize_name(const char *name, char *sanitized, size_t size)
{
    size_t output = 0;

    if (size == 0) {
        return;
    }
    while (name != NULL && *name != '\0' && output + 1 < size) {
        unsigned char value = (unsigned char)*name++;
        if ((value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') ||
            value == '_' || value == '-') {
            sanitized[output++] = (char)value;
        } else {
            sanitized[output++] = '_';
        }
    }
    sanitized[output] = '\0';
}

static int ge_make_map_name(char *path,
                            size_t path_size,
                            const char *output_directory,
                            size_t image_index,
                            const char *image_name,
                            size_t lod,
                            const char *suffix)
{
    char sanitized[GE_BUMP_MAX_IMAGE_NAME];
    ge_sanitize_name(image_name, sanitized, sizeof(sanitized));

    int written = snprintf(path, path_size, "%s/image_%04zu_%s_lod%zu.%s.png",
                           output_directory, image_index, sanitized, lod,
                           suffix);
    return written >= 0 && (size_t)written < path_size;
}

static void ge_write_csv_field(FILE *manifest, const char *value)
{
    fputc('"', manifest);
    if (value != NULL) {
        for (const char *cursor = value; *cursor != '\0'; ++cursor) {
            if (*cursor == '"') {
                fputc('"', manifest);
            }
            fputc(*cursor, manifest);
        }
    }
    fputc('"', manifest);
}

static void ge_write_manifest_header(FILE *manifest,
                                     const struct ge_options *options,
                                     uint64_t rom_hash)
{
    fprintf(manifest, "# rom_fnv1a64=%016" PRIx64 "\n", rom_hash);
    fprintf(manifest, "# strength=%.6g\n", options->strength);
    fprintf(manifest, "# include_default=%d\n", options->include_default);
    fprintf(manifest,
            "image_index,name,rom_offset,rom_size,lod,width,height,"
            "height_map,normal_map,status,reason,source_fnv1a64\n");
}

static void ge_write_manifest_row(FILE *manifest,
                                  size_t image_index,
                                  const struct ge_image_def *definition,
                                  const struct ge_image_record *record,
                                  size_t lod,
                                  const struct pd_image *image,
                                  const char *height_map,
                                  const char *normal_map,
                                  const char *status,
                                  const char *reason,
                                  uint64_t source_hash)
{
    fprintf(manifest, "%zu,", image_index);
    ge_write_csv_field(manifest, definition->name);
    fprintf(manifest, ",%" PRIu64 ",%" PRIu64 ",%zu,%d,%d,",
            record->offset, record->size, lod,
            image == NULL ? 0 : image->width,
            image == NULL ? 0 : image->height);
    ge_write_csv_field(manifest, height_map);
    fputc(',', manifest);
    ge_write_csv_field(manifest, normal_map);
    fputc(',', manifest);
    ge_write_csv_field(manifest, status);
    fputc(',', manifest);
    ge_write_csv_field(manifest, reason);
    fprintf(manifest, ",%016" PRIx64 "\n", source_hash);
}

static int ge_parse_float(const char *value, float *result)
{
    char *end;
    float parsed;

    if (value == NULL || result == NULL) {
        return 0;
    }
    errno = 0;
    parsed = strtof(value, &end);
    if (errno != 0 || end == value || *end != '\0' ||
        !__builtin_isfinite(parsed) || parsed < 0.0f) {
        return 0;
    }
    *result = parsed;
    return 1;
}

static int ge_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s --rom ROM.z64 --out DIRECTORY [options]\n"
            "\n"
            "Generate deterministic height and tangent-space normal maps from\n"
            "ROM textures selected by assets/images.def.\n"
            "\n"
            "Options:\n"
            "  --images-def PATH    Image metadata (default: assets/images.def)\n"
            "  --imagelist PATH     ROM offsets (default: imagelist.u.csv)\n"
            "  --strength VALUE     Height-gradient strength (default: 2.5)\n"
            "  --include-default    Include untagged HIT_DEFAULT materials\n"
            "  --help               Show this help\n",
            program);
    return 2;
}

static int ge_parse_args(int argc, char **argv, struct ge_options *options)
{
    options->rom_path = NULL;
    options->output_path = NULL;
    options->images_def_path = "assets/images.def";
    options->imagelist_path = "imagelist.u.csv";
    options->strength = 2.5f;
    options->include_default = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            return ge_usage(argv[0]);
        }
        if (strcmp(argv[i], "--include-default") == 0) {
            options->include_default = 1;
            continue;
        }
        if ((strcmp(argv[i], "--rom") == 0 ||
             strcmp(argv[i], "--out") == 0 ||
             strcmp(argv[i], "--images-def") == 0 ||
             strcmp(argv[i], "--imagelist") == 0 ||
             strcmp(argv[i], "--strength") == 0) &&
            i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(argv[i - 1], "--rom") == 0) {
                options->rom_path = value;
            } else if (strcmp(argv[i - 1], "--out") == 0) {
                options->output_path = value;
            } else if (strcmp(argv[i - 1], "--images-def") == 0) {
                options->images_def_path = value;
            } else if (strcmp(argv[i - 1], "--imagelist") == 0) {
                options->imagelist_path = value;
            } else if (!ge_parse_float(value, &options->strength)) {
                fprintf(stderr, "Invalid --strength value '%s'\n", value);
                return 0;
            }
            continue;
        }

        fprintf(stderr, "Unknown or incomplete option '%s'\n", argv[i]);
        return 0;
    }

    if (options->rom_path == NULL || options->output_path == NULL) {
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    struct ge_options options;
    struct ge_image_def *definitions = NULL;
    struct ge_image_record *records = NULL;
    size_t definition_count = 0;
    size_t record_count = 0;
    size_t generated = 0;
    size_t skipped = 0;
    size_t failed = 0;
    uint64_t rom_size;
    uint64_t rom_hash;
    FILE *rom = NULL;
    FILE *manifest = NULL;
    char manifest_path[PATH_MAX];

    int parse_result = ge_parse_args(argc, argv, &options);
    if (parse_result == 2) {
        return 0;
    }
    if (parse_result == 0) {
        return ge_usage(argv[0]);
    }

    if (!ge_make_directory(options.output_path) ||
        !ge_load_image_defs(options.images_def_path, &definitions,
                            &definition_count) ||
        !ge_load_image_list(options.imagelist_path, &records, &record_count) ||
        !ge_open_rom(options.rom_path, &rom, &rom_size, &rom_hash)) {
        free(definitions);
        free(records);
        return 1;
    }

    if (record_count < definition_count) {
        fprintf(stderr,
                "Warning: image list has %zu rows for %zu definitions; "
                "processing the common prefix\n",
                record_count, definition_count);
    }
    if (definition_count < record_count) {
        fprintf(stderr,
                "Warning: image definitions have %zu rows for %zu image-list "
                "entries; processing the common prefix\n",
                definition_count, record_count);
    }

    if (snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.csv",
                 options.output_path) >= (int)sizeof(manifest_path)) {
        fprintf(stderr, "Output path is too long\n");
        fclose(rom);
        free(definitions);
        free(records);
        return 1;
    }
    manifest = fopen(manifest_path, "w");
    if (manifest == NULL) {
        fprintf(stderr, "Unable to create manifest '%s': %s\n",
                manifest_path, strerror(errno));
        fclose(rom);
        free(definitions);
        free(records);
        return 1;
    }
    ge_write_manifest_header(manifest, &options, rom_hash);

    size_t count = definition_count < record_count
        ? definition_count
        : record_count;
    for (size_t image_index = 0; image_index < count; ++image_index) {
        const struct ge_image_def *definition = &definitions[image_index];
        const struct ge_image_record *record = &records[image_index];
        GeBumpMapEligibility initial_eligibility =
            geBumpMapEligibility(definition->name, definition->hit_texture,
                                 definition->hit_sound, 4, 4,
                                 options.include_default);

        if (initial_eligibility != GE_BUMP_MAP_ELIGIBLE) {
            ge_write_manifest_row(
                manifest, image_index, definition, record, 0, NULL, NULL, NULL,
                "skipped", geBumpMapEligibilityReason(initial_eligibility), 0);
            ++skipped;
            continue;
        }

        uint8_t *encoded = ge_read_rom_slice(rom, rom_size,
                                              record->offset, record->size);
        if (encoded == NULL) {
            fprintf(stderr, "Unable to read ROM slice for image %zu (%s)\n",
                    image_index, definition->name);
            ge_write_manifest_row(manifest, image_index, definition, record, 0,
                                  NULL, NULL, NULL, "failed", "rom-read", 0);
            ++failed;
            continue;
        }
        uint64_t source_hash = ge_fnv1a_update(
            UINT64_C(1469598103934665603), encoded, (size_t)record->size);

        FILE *temporary = tmpfile();
        struct pd_tex *texture = pdtex_allocate();
        int decoded = 0;
        if (temporary != NULL && texture != NULL &&
            fwrite(encoded, 1, (size_t)record->size, temporary) ==
                (size_t)record->size &&
            fseek(temporary, 0, SEEK_SET) == 0 &&
            pdtex_read_stream(texture, temporary) == 0) {
            decoded = 1;
        }
        free(encoded);
        if (temporary != NULL) {
            fclose(temporary);
        }

        if (!decoded) {
            fprintf(stderr, "Unable to decode image %zu (%s)\n",
                    image_index, definition->name);
            ge_write_manifest_row(manifest, image_index, definition, record, 0,
                                  NULL, NULL, NULL, "failed", "decode",
                                  source_hash);
            pdtex_free(texture);
            ++failed;
            continue;
        }

        for (size_t lod = 0; lod < PDTEX_MAX_IMAGES; ++lod) {
            struct pd_image *image = &texture->images[lod];
            GeBumpMapEligibility eligibility;
            size_t pixels;
            uint8_t *rgba;
            uint8_t *height_map;
            uint8_t *normal_map;
            char height_path[PATH_MAX];
            char normal_path[PATH_MAX];
            char height_name[PATH_MAX];
            char normal_name[PATH_MAX];

            if (!image->exists) {
                continue;
            }
            eligibility = geBumpMapEligibility(
                definition->name, definition->hit_texture, definition->hit_sound,
                (uint32_t)(image->width < 0 ? 0 : image->width),
                (uint32_t)(image->height < 0 ? 0 : image->height),
                options.include_default);
            if (eligibility != GE_BUMP_MAP_ELIGIBLE) {
                ge_write_manifest_row(
                    manifest, image_index, definition, record, lod, image, NULL,
                    NULL, "skipped", geBumpMapEligibilityReason(eligibility),
                    source_hash);
                ++skipped;
                continue;
            }

            if ((size_t)image->width > SIZE_MAX / (size_t)image->height ||
                (pixels = (size_t)image->width * (size_t)image->height) >
                    SIZE_MAX / 4) {
                ge_write_manifest_row(manifest, image_index, definition, record,
                                      lod, image, NULL, NULL, "failed",
                                      "size-overflow", source_hash);
                ++failed;
                continue;
            }

            rgba = malloc(pixels * 4);
            height_map = malloc(pixels);
            normal_map = malloc(pixels * 3);
            if (rgba == NULL || height_map == NULL || normal_map == NULL ||
                !geBumpDecodeImageRgba(image, texture, rgba)) {
                free(rgba);
                free(height_map);
                free(normal_map);
                ge_write_manifest_row(manifest, image_index, definition, record,
                                      lod, image, NULL, NULL, "failed",
                                      "pixel-format", source_hash);
                ++failed;
                continue;
            }

            geBumpGenerateHeight(rgba, (uint32_t)image->width,
                                 (uint32_t)image->height, height_map);
            geBumpGenerateNormal(height_map, (uint32_t)image->width,
                                 (uint32_t)image->height, options.strength,
                                 normal_map);

            if (!ge_make_map_name(height_name, sizeof(height_name),
                                  options.output_path, image_index,
                                  definition->name, lod, "height") ||
                !ge_make_map_name(normal_name, sizeof(normal_name),
                                  options.output_path, image_index,
                                  definition->name, lod, "normal")) {
                free(rgba);
                free(height_map);
                free(normal_map);
                ge_write_manifest_row(manifest, image_index, definition, record,
                                      lod, image, NULL, NULL, "failed",
                                      "path-too-long", source_hash);
                ++failed;
                continue;
            }

            if (!ge_write_png(height_name, (uint32_t)image->width,
                              (uint32_t)image->height, PNG_FORMAT_GRAY,
                              height_map) ||
                !ge_write_png(normal_name, (uint32_t)image->width,
                              (uint32_t)image->height, PNG_FORMAT_RGB,
                              normal_map)) {
                ge_write_manifest_row(manifest, image_index, definition, record,
                                      lod, image, NULL, NULL, "failed",
                                      "png-write", source_hash);
                ++failed;
            } else {
                const char *height_basename = strrchr(height_name, '/');
                const char *normal_basename = strrchr(normal_name, '/');
                height_basename = height_basename == NULL
                    ? height_name
                    : height_basename + 1;
                normal_basename = normal_basename == NULL
                    ? normal_name
                    : normal_basename + 1;
                ge_write_manifest_row(manifest, image_index, definition, record,
                                      lod, image, height_basename,
                                      normal_basename, "generated", "eligible",
                                      source_hash);
                ++generated;
            }

            free(rgba);
            free(height_map);
            free(normal_map);
        }

        pdtex_free(texture);
    }

    fclose(manifest);
    fclose(rom);
    free(definitions);
    free(records);
    fprintf(stdout, "Generated %zu map pairs; skipped %zu; failed %zu\n",
            generated, skipped, failed);
    return failed == 0 ? 0 : 1;
}
