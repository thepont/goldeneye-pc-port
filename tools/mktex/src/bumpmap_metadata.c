#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "bumpmap_metadata.h"

extern unsigned long long strtoull(const char *, char **, int);

static void geBumpTrim(char *value)
{
    size_t length;
    size_t start = 0;

    length = strlen(value);
    while (start < length &&
           (value[start] == ' ' || value[start] == '\t' ||
            value[start] == '\r' || value[start] == '\n')) {
        ++start;
    }

    while (length > start &&
           (value[length - 1] == ' ' || value[length - 1] == '\t' ||
            value[length - 1] == '\r' || value[length - 1] == '\n')) {
        --length;
    }

    if (start != 0) {
        memmove(value, value + start, length - start);
    }
    value[length - start] = '\0';
}

static int geBumpCopyField(char *destination,
                           size_t destination_size,
                           const char *source,
                           size_t source_size)
{
    if (source_size >= destination_size) {
        return 0;
    }

    memcpy(destination, source, source_size);
    destination[source_size] = '\0';
    geBumpTrim(destination);
    return destination[0] != '\0';
}

int geBumpParseImageDefLine(const char *line,
                            struct ge_image_def *definition)
{
    const char *open;
    const char *close;
    const char *field_start;
    char fields[4][GE_BUMP_MAX_IMAGE_NAME];
    size_t field = 0;

    if (line == NULL || definition == NULL) {
        return 0;
    }

    open = strstr(line, "IMAGE(");
    if (open == NULL) {
        return 0;
    }
    open += strlen("IMAGE(");
    close = strchr(open, ')');
    if (close == NULL) {
        return -1;
    }

    field_start = open;
    for (const char *cursor = open; cursor <= close; ++cursor) {
        if (*cursor == ',' || *cursor == ')') {
            if (field < 4 &&
                !geBumpCopyField(fields[field], sizeof(fields[field]),
                                 field_start,
                                 (size_t)(cursor - field_start))) {
                return -1;
            }
            ++field;
            field_start = cursor + 1;
            if (*cursor == ')') {
                break;
            }
        }
    }

    if (field < 4) {
        return -1;
    }

    if (!geBumpCopyField(definition->name, sizeof(definition->name),
                         fields[0], strlen(fields[0])) ||
        !geBumpCopyField(definition->hit_sound, sizeof(definition->hit_sound),
                         fields[2], strlen(fields[2])) ||
        !geBumpCopyField(definition->hit_texture,
                         sizeof(definition->hit_texture),
                         fields[3], strlen(fields[3]))) {
        return -1;
    }

    return 1;
}

int geBumpParseImageRecord(const char *line,
                           struct ge_image_record *record)
{
    const char *cursor;
    char *end;
    unsigned long long offset;
    unsigned long long size;

    if (line == NULL || record == NULL) {
        return 0;
    }

    cursor = line;
    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }
    if (*cursor == '\0' || *cursor == '#' || *cursor == '\n') {
        return 0;
    }

    errno = 0;
    offset = strtoull(cursor, &end, 10);
    if (errno != 0 || end == cursor) {
        return -1;
    }
    cursor = end;
    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }
    if (*cursor != ',') {
        return -1;
    }
    ++cursor;
    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }

    errno = 0;
    size = strtoull(cursor, &end, 10);
    if (errno != 0 || end == cursor) {
        return -1;
    }

    record->offset = (uint64_t)offset;
    record->size = (uint64_t)size;
    return 1;
}
