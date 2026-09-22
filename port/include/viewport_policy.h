#ifndef GE007_VIEWPORT_POLICY_H
#define GE007_VIEWPORT_POLICY_H

/* Keep the renderer's split-screen classification and its vertical overlap
 * rule independent from OpenGL state so both can be tested as pure policy. */
static inline int geViewportIsSplit(float viewport_height, float screen_height)
{
    return viewport_height > 1.0f &&
           screen_height > 0.0f &&
           viewport_height < screen_height * 0.75f;
}

static inline int geViewportRectsOverlap(int first_y,
                                         int first_height,
                                         int second_y,
                                         int second_height)
{
    long long first_end;
    long long second_end;

    if (first_height <= 0 || second_height <= 0)
    {
        return 0;
    }

    first_end = (long long)first_y + first_height;
    second_end = (long long)second_y + second_height;
    return (long long)first_y < second_end &&
           (long long)second_y < first_end;
}

#endif /* GE007_VIEWPORT_POLICY_H */
