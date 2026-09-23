#ifndef GE007_VECTOR_TEXT_ROUTE_POLICY_H
#define GE007_VECTOR_TEXT_ROUTE_POLICY_H

/* The vector atlas currently covers the printable ASCII face used by the
 * English front end. Keep unsupported encoded text on the original bitmap
 * path instead of silently dropping glyphs from a briefing. */
static inline int geVectorTextCanReplaceBitmap(int vector_enabled,
                                               const char *text)
{
    const unsigned char *cursor;

    if (!vector_enabled || text == 0 || text[0] == '\0') {
        return 0;
    }
    for (cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
        if (*cursor == '\n') {
            continue;
        }
        if (*cursor < 0x20 || *cursor >= 0x7f) {
            return 0;
        }
    }
    return 1;
}

/* The original textMeasure contract reports one line-height for each
 * explicit newline.  Several briefing layouts use that value as the Y step
 * for the next objective, so returning only the vector width makes every
 * objective share the same origin. */
static inline int geVectorTextMeasureHeight(const char *text, int lineheight)
{
    int newlines = 0;

    if (text == 0 || lineheight <= 0) {
        return 0;
    }
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor != '\0'; ++cursor) {
        if (*cursor == '\n') {
            ++newlines;
        }
    }
    return newlines * lineheight;
}

#endif /* GE007_VECTOR_TEXT_ROUTE_POLICY_H */
