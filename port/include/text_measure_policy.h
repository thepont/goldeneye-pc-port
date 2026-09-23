#ifndef GE007_TEXT_MEASURE_POLICY_H
#define GE007_TEXT_MEASURE_POLICY_H

/* textMeasure dereferences both font banks for the common zero-line-height
 * path. Treat the complete tuple as the boundary contract so a missing bank
 * cannot turn a harmless measurement request into a host crash. */
static inline int geTextMeasureInputsCurrentlyAccepted(const void *text,
                                                       const void *font_chars,
                                                       const void *font)
{
    return text != 0 && font_chars != 0 && font != 0;
}

#endif /* GE007_TEXT_MEASURE_POLICY_H */
