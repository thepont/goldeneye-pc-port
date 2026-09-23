#ifndef GE007_BUMPMAP_METADATA_H
#define GE007_BUMPMAP_METADATA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GE_BUMP_MAX_IMAGE_NAME 128
#define GE_BUMP_MAX_HIT_NAME 32

struct ge_image_def {
    char name[GE_BUMP_MAX_IMAGE_NAME];
    char hit_sound[GE_BUMP_MAX_HIT_NAME];
    char hit_texture[GE_BUMP_MAX_HIT_NAME];
};

struct ge_image_record {
    uint64_t offset;
    uint64_t size;
};

/*
 * Return 1 for a parsed IMAGE row, 0 for a non-IMAGE/comment row, and -1 for
 * malformed input. Only the name and material fields are retained; additional
 * IMAGE macro fields remain deliberately ignored.
 */
int geBumpParseImageDefLine(const char *line, struct ge_image_def *definition);

/*
 * Return 1 for a data row, 0 for blank/comment rows, and -1 for malformed
 * offsets or sizes.
 */
int geBumpParseImageRecord(const char *line,
                           struct ge_image_record *record);

#ifdef __cplusplus
}
#endif

#endif
